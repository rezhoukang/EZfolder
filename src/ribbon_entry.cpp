#define _UNICODE
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <commctrl.h>
#include <exdisp.h>
#include <strsafe.h>
#include <algorithm>
#include <new>

namespace {
constexpr wchar_t managerClass[] = L"EZFolder.EntryManager";
constexpr wchar_t entryClass[] = L"EZFolder.RibbonEntry";
constexpr wchar_t bandId[] = L"{5129B68D-A44B-427B-AEE1-53137D3989EA}";
constexpr UINT toggleMessage = WM_APP + 1;
HINSTANCE instance;
HWND manager;
bool dark, highContrast;

template<class T> struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T* operator->() const { return p; }
};

bool isClass(HWND w, const wchar_t* wanted) {
    wchar_t name[48]{};
    return GetClassNameW(w, name, 48) && !wcscmp(name, wanted);
}
bool bandVisible(HWND root) {
    HWND bar = static_cast<HWND>(GetPropW(root, L"EZFolder.Band.2"));
    return bar && IsWindowVisible(bar);
}
HRESULT toggleBand(HWND root) {
    Com<IShellWindows> windows;
    HRESULT hr = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows.p));
    if (FAILED(hr)) return hr;
    long count = 0;
    windows->get_Count(&count);
    for (long i = 0; i < count; ++i) {
        VARIANT index{};
        index.vt = VT_I4; index.lVal = i;
        Com<IDispatch> item;
        Com<IWebBrowser2> web;
        SHANDLE_PTR handle = 0;
        if (FAILED(windows->Item(index, &item.p)) || !item.p ||
            FAILED(item->QueryInterface(IID_PPV_ARGS(&web.p))) ||
            FAILED(web->get_HWND(&handle)) || reinterpret_cast<HWND>(handle) != root) continue;
        VARIANT id{}, show{}, size{};
        id.vt = VT_BSTR;
        id.bstrVal = SysAllocString(bandId);
        if (!id.bstrVal) return E_OUTOFMEMORY;
        bool opening = !bandVisible(root);
        show.vt = VT_BOOL;
        show.boolVal = opening ? VARIANT_TRUE : VARIANT_FALSE;
        if (opening && !SetPropW(root, L"EZFolder.KeepExistingFrame", reinterpret_cast<HANDLE>(1))) {
            VariantClear(&id);
            return E_ACCESSDENIED;
        }
        hr = web->ShowBrowserBar(&id, &show, &size);
        if (opening && SUCCEEDED(hr) && !GetPropW(root, L"EZFolder.Band.2")) {
            show.boolVal = VARIANT_FALSE;
            web->ShowBrowserBar(&id, &show, &size);
            show.boolVal = VARIANT_TRUE;
            hr = web->ShowBrowserBar(&id, &show, &size);
        }
        if (opening) RemovePropW(root, L"EZFolder.KeepExistingFrame");
        VariantClear(&id);
        return hr;
    }
    return E_FAIL;
}
UINT dpiOf(HWND w) {
    using GetDpi = UINT (WINAPI*)(HWND);
    static const auto getDpi = reinterpret_cast<GetDpi>(reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    return getDpi ? getDpi(w) : 96;
}
struct DockDpiContext {
    using SetContext = HANDLE (WINAPI*)(HANDLE);
    SetContext set = nullptr;
    HANDLE previous = nullptr;
    explicit DockDpiContext(HWND dock) {
        using GetContext = HANDLE (WINAPI*)(HWND);
        static HMODULE user = GetModuleHandleW(L"user32.dll");
        static auto get = reinterpret_cast<GetContext>(reinterpret_cast<void*>(GetProcAddress(user,"GetWindowDpiAwarenessContext")));
        static auto setter = reinterpret_cast<SetContext>(reinterpret_cast<void*>(GetProcAddress(user,"SetThreadDpiAwarenessContext")));
        set = setter;
        if (get && set) previous = set(get(dock));
    }
    ~DockDpiContext() { if (previous) set(previous); }
};
struct Entry {
    HWND button = nullptr, tooltip = nullptr;
    UINT dpi = 0;
    bool on = false, hot = false, busy = false;
};
Entry* state(HWND w) { return reinterpret_cast<Entry*>(GetWindowLongPtrW(w, GWLP_USERDATA)); }
const wchar_t* action(bool on) { return on ? L"隐藏 EZFolder 标签栏" : L"显示 EZFolder 标签栏"; }
void describe(Entry& e) {
    SetWindowTextW(e.button, action(e.on));
    if (e.tooltip) {
        TOOLINFOW tool{};
        tool.cbSize = sizeof(tool);
        tool.hwnd = GetParent(e.button);
        tool.uId = reinterpret_cast<UINT_PTR>(e.button);
        tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tool.lpszText = const_cast<wchar_t*>(action(e.on));
        SendMessageW(e.tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
    }
    InvalidateRect(e.button, nullptr, FALSE);
}
void paint(const DRAWITEMSTRUCT& d, const Entry& e) {
    bool down = (d.itemState & ODS_SELECTED) != 0;
    bool active = down || e.hot;
    COLORREF bg = highContrast ? GetSysColor(active ? COLOR_HIGHLIGHT : COLOR_WINDOW)
        : dark ? (down ? RGB(71,71,71) : active ? RGB(48,48,48) : RGB(0,0,0))
               : (active ? RGB(221,235,247) : GetSysColor(COLOR_WINDOW));
    COLORREF fg = highContrast ? GetSysColor(active ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT)
        : dark ? RGB(246,246,246) : GetSysColor(COLOR_WINDOWTEXT);
    SetDCBrushColor(d.hDC, bg);
    FillRect(d.hDC, &d.rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    int saved = SaveDC(d.hDC);
    auto px = [&](int v) { return MulDiv(v, e.dpi ? e.dpi : 96, 96); };
    int x = (d.rcItem.right - px(14))/2;
    int y = (d.rcItem.bottom - px(11))/2;
    HPEN pen = CreatePen(PS_SOLID, std::max(1,px(1)), fg);
    HGDIOBJ old = SelectObject(d.hDC, pen);
    SelectObject(d.hDC, GetStockObject(NULL_BRUSH));
    Rectangle(d.hDC, x, y, x+px(14), y+px(11));
    MoveToEx(d.hDC,x,y+px(3),nullptr); LineTo(d.hDC,x+px(13),y+px(3));
    MoveToEx(d.hDC,x+px(6),y,nullptr); LineTo(d.hDC,x+px(6),y+px(3));
    SelectObject(d.hDC,old);
    DeleteObject(pen);
    if (e.on) {
        RECT marker{px(4),d.rcItem.bottom-px(2),d.rcItem.right-px(4),d.rcItem.bottom};
        SetDCBrushColor(d.hDC, highContrast ? fg : GetSysColor(COLOR_HIGHLIGHT));
        FillRect(d.hDC,&marker,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    }
    if ((d.itemState & ODS_FOCUS) && !(d.itemState & ODS_NOFOCUSRECT)) {
        RECT focus = d.rcItem; InflateRect(&focus,-2,-2); DrawFocusRect(d.hDC,&focus);
    }
    RestoreDC(d.hDC,saved);
}
LRESULT CALLBACK buttonProc(HWND w, UINT m, WPARAM a, LPARAM b, UINT_PTR, DWORD_PTR data) {
    Entry& e = *reinterpret_cast<Entry*>(data);
    if (m == WM_MOUSEMOVE && !e.hot) {
        e.hot = true;
        TRACKMOUSEEVENT ev{sizeof(ev),TME_LEAVE,w,0}; TrackMouseEvent(&ev);
        InvalidateRect(w,nullptr,FALSE);
    } else if (m == WM_MOUSELEAVE) { e.hot = false; InvalidateRect(w,nullptr,FALSE); }
    return DefSubclassProc(w,m,a,b);
}
LRESULT CALLBACK entryProc(HWND w, UINT m, WPARAM a, LPARAM b) {
    Entry* e = state(w);
    if (m == WM_CREATE) {
        e = new(std::nothrow) Entry;
        if (!e) return -1;
        SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(e));
        e->button = CreateWindowExW(0,L"BUTTON",action(false),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            0,0,1,1,w,reinterpret_cast<HMENU>(1),instance,nullptr);
        if (!e->button || !SetWindowSubclass(e->button,buttonProc,1,reinterpret_cast<DWORD_PTR>(e))) return -1;
        e->tooltip = CreateWindowExW(WS_EX_TOOLWINDOW,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,
            0,0,0,0,w,nullptr,instance,nullptr);
        if (e->tooltip) {
            TOOLINFOW t{}; t.cbSize=sizeof(t); t.uFlags=TTF_IDISHWND|TTF_SUBCLASS;
            t.hwnd=w; t.uId=reinterpret_cast<UINT_PTR>(e->button); t.lpszText=const_cast<wchar_t*>(action(false));
            SendMessageW(e->tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&t));
        }
        return 0;
    }
    if (!e) return DefWindowProcW(w,m,a,b);
    switch(m) {
    case WM_DRAWITEM: paint(*reinterpret_cast<DRAWITEMSTRUCT*>(b),*e); return TRUE;
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: MoveWindow(e->button,0,0,LOWORD(b),HIWORD(b),TRUE); return 0;
    case WM_COMMAND:
        if (reinterpret_cast<HWND>(b)==e->button && HIWORD(a)==BN_CLICKED && !e->busy) {
            e->busy = PostMessageW(manager,toggleMessage,reinterpret_cast<WPARAM>(w),0) != FALSE;
        }
        return 0;
    case WM_NCDESTROY:
        if (e->tooltip && IsWindow(e->tooltip)) DestroyWindow(e->tooltip);
        SetWindowLongPtrW(w,GWLP_USERDATA,0);
        delete e;
        break;
    }
    return DefWindowProcW(w,m,a,b);
}
BOOL CALLBACK findDock(HWND w,LPARAM data) {
    if (!isClass(w,L"UIRibbonCommandBarDock") || !IsWindowVisible(w)) return TRUE;
    wchar_t name[32]{};
    if (!GetWindowTextW(w,name,32) || wcscmp(name,L"UIRibbonDockTop")) return TRUE;
    *reinterpret_cast<HWND*>(data)=w;
    return FALSE;
}
BOOL CALLBACK place(HWND root, LPARAM themeChanged) {
    if (!isClass(root,L"CabinetWClass") && !isClass(root,L"ExploreWClass")) return TRUE;
    if (!IsWindowVisible(root) || IsIconic(root)) return TRUE;
    HWND dock=nullptr;
    EnumChildWindows(root,findDock,reinterpret_cast<LPARAM>(&dock));
    if (!dock || !IsWindowVisible(dock)) return TRUE;
    DockDpiContext context(dock);
    HWND w=FindWindowExW(dock,nullptr,entryClass,nullptr);
    if (!w) {
        DWORD process=0;
        GetWindowThreadProcessId(root,&process);
        HANDLE handle=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,process);
        wchar_t exe[MAX_PATH]{}, expected[MAX_PATH]{};
        DWORD size=MAX_PATH;
        if (!handle) return TRUE;
        BOOL queried=QueryFullProcessImageNameW(handle,0,exe,&size);
        CloseHandle(handle);
        if (!queried || !GetWindowsDirectoryW(expected,MAX_PATH) ||
            FAILED(StringCchCatW(expected,MAX_PATH,L"\\explorer.exe")) || _wcsicmp(expected,exe)) return TRUE;
        w=CreateWindowExW(WS_EX_NOPARENTNOTIFY,entryClass,L"EZFolder",WS_CHILD|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,
            0,0,1,1,dock,nullptr,instance,nullptr);
    }
    if (!w) return TRUE;
    DWORD pid=0;
    GetWindowThreadProcessId(w,&pid);
    if (pid!=GetCurrentProcessId()) return TRUE;
    Entry* e=state(w);
    if (!e) return TRUE;
    UINT dpi=dpiOf(dock);
    auto px=[dpi](int n){return MulDiv(n,dpi,96);};
    RECT r{}; GetClientRect(dock,&r);
    if (r.right < px(300) || r.bottom < px(19)) { ShowWindow(w,SW_HIDE); return TRUE; }
    int width=px(24);
    int height=std::min(static_cast<int>(r.bottom),px(25));
    int x=r.right-px(42)-width, y=0;
    RECT actual{}; GetWindowRect(w,&actual);
    MapWindowPoints(nullptr,dock,reinterpret_cast<POINT*>(&actual),2);
    if (!IsWindowVisible(w) || actual.left!=x || actual.top!=y || actual.right-actual.left!=width ||
        actual.bottom-actual.top!=height || GetWindow(dock,GW_CHILD)!=w)
        SetWindowPos(w,HWND_TOP,x,y,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    if (dpi!=e->dpi) {
        e->dpi=dpi;
        InvalidateRect(e->button,nullptr,FALSE);
    }
    bool on=bandVisible(root);
    if (on!=e->on || themeChanged) { e->on=on; describe(*e); }
    return TRUE;
}
void refresh() {
    HIGHCONTRASTW hc{}; hc.cbSize=sizeof(hc);
    SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(hc),&hc,0);
    DWORD light=1,bytes=sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&bytes);
    bool changed=dark!=(light==0) || highContrast!=bool(hc.dwFlags&HCF_HIGHCONTRASTON);
    dark=light==0; highContrast=(hc.dwFlags&HCF_HIGHCONTRASTON)!=0;
    EnumWindows(place,changed);
}
LRESULT CALLBACK managerProc(HWND w,UINT m,WPARAM a,LPARAM b) {
    if (m==WM_TIMER) { refresh(); return 0; }
    if (m==toggleMessage) {
        HWND host=reinterpret_cast<HWND>(a);
        DWORD pid=0;
        GetWindowThreadProcessId(host,&pid);
        if(pid!=GetCurrentProcessId() || !isClass(host,entryClass)) return 0;
        HWND root=GetAncestor(host,GA_ROOT);
        HRESULT hr=toggleBand(root);
        if(IsWindow(host)) {
            Entry* e=state(host);
            if(e){e->busy=false; e->on=bandVisible(root); describe(*e);}
        }
        if(FAILED(hr)) MessageBeep(MB_ICONWARNING);
        return 0;
    }
    if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(w,m,a,b);
}
}
int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,LPWSTR args,int) {
    if(!wcscmp(args,L"/stop")){
        HWND running=FindWindowExW(HWND_MESSAGE,nullptr,managerClass,nullptr);
        if(running) PostMessageW(running,WM_CLOSE,0,0);
        return 0;
    }
    HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\EZFolder.RibbonEntry");
    if(!mutex || GetLastError()==ERROR_ALREADY_EXISTS){if(mutex) CloseHandle(mutex);return 0;}
    if(FAILED(OleInitialize(nullptr))){CloseHandle(mutex);return 1;}
    instance=inst;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSW c{};c.hInstance=inst;c.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    c.lpszClassName=entryClass;c.lpfnWndProc=entryProc;RegisterClassW(&c);
    c.lpszClassName=managerClass;c.lpfnWndProc=managerProc;RegisterClassW(&c);
    manager=CreateWindowExW(0,managerClass,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,inst,nullptr);
    if(manager && SetTimer(manager,1,300,nullptr)){
        refresh();
        MSG msg{};
        while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    OleUninitialize();
    CloseHandle(mutex);
    return 0;
}
