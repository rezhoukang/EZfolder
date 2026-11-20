// Standalone timing fixture: host two real Windows shell views in one window.
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#define NTDDI_VERSION 0x0A000000
#include <windows.h>
#include <shlobj.h>
#include <string>

struct View {
    HWND pane = nullptr;
    IExplorerBrowser* browser = nullptr;
    HRESULT navigate(const std::wstring& path) {
        IShellItem* item = nullptr;
        HRESULT hr = SHCreateItemFromParsingName(path.c_str(),nullptr,IID_PPV_ARGS(&item));
        if (SUCCEEDED(hr)) {
            hr = browser->BrowseToObject(item,SBSP_ABSOLUTE);
            item->Release();
        }
        return hr;
    }
    HRESULT create(HWND parent,RECT bounds,const std::wstring& path) {
        pane = CreateWindowExW(0,L"STATIC",nullptr,WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,
            0,0,bounds.right,bounds.bottom,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
        if (!pane) return E_FAIL;
        HRESULT hr = CoCreateInstance(CLSID_ExplorerBrowser,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&browser));
        FOLDERSETTINGS settings{FVM_DETAILS,FWF_AUTOARRANGE};
        if (SUCCEEDED(hr)) hr = browser->SetOptions(static_cast<EXPLORER_BROWSER_OPTIONS>(EBO_NOPERSISTVIEWSTATE|EBO_NOTRAVELLOG));
        if (SUCCEEDED(hr)) hr = browser->Initialize(pane,&bounds,&settings);
        if (SUCCEEDED(hr)) hr = navigate(path);
        return hr;
    }
    void close() {
        if (browser) { browser->Destroy(); browser->Release(); browser=nullptr; }
    }
};
static View views[2];
static std::wstring base;
static LRESULT CALLBACK windowProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    switch (msg) {
    case WM_APP+1: // Reveal the previously navigated pane.
        return wp<2 && SetWindowPos(views[wp].pane,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    case WM_APP+2: // First visit in an initialized native shell view.
        return wp<50 && SUCCEEDED(views[0].navigate(base+L"\\native-"+std::to_wstring(wp)));
    case WM_CLOSE:
        for (auto& view : views) view.close();
        DestroyWindow(w);
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: return DefWindowProcW(w,msg,wp,lp);
    }
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(),&argc);
    if (!argv || argc!=2) return 1;
    base = argv[1];
    LocalFree(argv);
    if (FAILED(OleInitialize(nullptr))) return 1;
    WNDCLASSW cls{};
    cls.hInstance=instance; cls.lpfnWndProc=windowProc; cls.lpszClassName=L"EZFolder.LatencyFixture";
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    RegisterClassW(&cls);
    HWND w=CreateWindowW(cls.lpszClassName,L"EZFolder folder-view timing",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,680,410,nullptr,nullptr,instance,nullptr);
    RECT r{};
    if (!w || !GetClientRect(w,&r)) return 1;
    bool ok=true;
    for (int i=0;i<2 && ok;++i) ok=SUCCEEDED(views[i].create(w,r,base+L"\\known-"+std::to_wstring(i)));
    if (ok) {
        ShowWindow(w,SW_SHOWNORMAL);
        MSG msg{};
        while (GetMessageW(&msg,nullptr,0,0)>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    } else SendMessageW(w,WM_CLOSE,0,0);
    OleUninitialize();
    return ok ? 0 : 1;
}
