#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#define NTDDI_VERSION 0x0A000000
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shlguid.h>
#include <algorithm>
#include <memory>
#include <new>
#include <vector>

// Shared with install.ps1. Every Explorer window creates its own Band.
static constexpr CLSID clsid = {0x5129b68d,0xa44b,0x427b,{0xae,0xe1,0x53,0x13,0x7d,0x39,0x89,0xea}};
static LONG objects;
static HINSTANCE module;

template<class T> struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    ~Com() { reset(); }
    void reset() { if (p) { T* previous = p; p = nullptr; previous->Release(); } }
    T* operator->() const { return p; }
};
struct FreePidl { void operator()(ITEMIDLIST* p) const { CoTaskMemFree(p); } };
using Location = std::unique_ptr<ITEMIDLIST, FreePidl>;
#include "merge_visibility.h"
#include "window_merge.h"
#include "band_theme.h"

class Band final : public IDeskBand, public IObjectWithSite, public IInputObject, public IPersistStream {
    LONG refs = 1;
    Com<IUnknown> site;
    Com<IShellBrowser> browser;
    HWND bar = nullptr, tabs = nullptr, add = nullptr, close = nullptr, mergeButton = nullptr;
    HFONT font = nullptr;
    BandTheme theme;
    ULONGLONG nextThemeCheck = 0;
    UINT timerPeriod = 0;
    std::vector<Location> folders;
    int active = -1, destination = -1;
    bool closeAfterSwitch = false;
    ULONGLONG deadline = 0;
    enum { NewTab = 100, CloseTab, TabControl, MergeWindows };
    static constexpr UINT importMessage = WM_APP + 27;
    static constexpr UINT refreshMessage = WM_APP + 28;
    HWND incoming = nullptr;
    HWND awaitingAck = nullptr;
    LPARAM incomingBirth = 0;
    int importIndex = -1;
    bool settled = false, offered = false;
    ULONGLONG opened = 0;
    Location offeredFolder;
    merge::LocalOffer localOffer;
    HWND confirming = nullptr;
    LPARAM confirmingBirth = 0;
    MergeVisibility presentation;
    bool closingMergedFrame = false;

    bool schedule(UINT period) {
        if (timerPeriod == period) return true;
        if (!bar || !SetTimer(bar, 1, period, nullptr)) return false;
        timerPeriod = period;
        return true;
    }

    bool validOffer(HWND source, LPARAM birth) const {
        return merge::live(bar) && merge::live(source) && source != bar &&
            GetPropW(source, merge::offerProp) == bar &&
            reinterpret_cast<LPARAM>(GetPropW(source, merge::birthProp)) == birth;
    }
    void endImport(bool success) {
        if (!incoming) return;
        if (success && validOffer(incoming, incomingBirth)) {
            merge::LocalOffer::confirm(incoming, bar, incomingBirth, folders[active].get());
            if (PostMessageW(incoming, merge::received(), reinterpret_cast<WPARAM>(bar), incomingBirth))
                awaitingAck = incoming;
        }
        if (!success && importIndex >= 0 && importIndex != active) erase(importIndex);
        incoming = nullptr;
        importIndex = -1;
    }
    LRESULT reserve(HWND source, LPARAM birth) {
        if (!settled || active < 0 || destination >= 0 || incoming ||
            (awaitingAck && GetPropW(awaitingAck, merge::offerProp) == bar) || !merge::enabled() ||
            !validOffer(source, birth)) return 0;
        incoming = source;
        incomingBirth = birth;
        if (!PostMessageW(bar, importMessage, 0, 0)) { endImport(false); return 0; }
        return 1;
    }
    void importWindow() {
        HWND source = incoming;
        if (!source) return;
        Location id = merge::LocalOffer::read(source, bar, incomingBirth);
        if (!id) id = merge::folderOf(source);
        if (!validOffer(source, incomingBirth) || !merge::enabled()) { endImport(false); return; }
        if (!id) {
            // A visible view can precede its registration in ShellWindows.
            PostMessageW(source, merge::retry(), reinterpret_cast<WPARAM>(bar), incomingBirth);
            endImport(false);
            return;
        }
        importIndex = append(std::move(id), false);
        if (importIndex < 0) { endImport(false); return; }
        select(importIndex);
    }
    void settle() {
        if (!closingMergedFrame) presentation.finish();
        settled = true;
        confirming = nullptr;
        if (bar) RemovePropW(bar, merge::offerProp);
        localOffer.clear();
        offeredFolder.reset();
    }
    bool hasSelection() {
        Com<IShellView> view;
        Com<IFolderView> folder;
        int count = 0;
        return browser.p && SUCCEEDED(browser->QueryActiveShellView(&view.p)) && view.p &&
            SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&folder.p))) &&
            SUCCEEDED(folder->ItemCount(SVGIO_SELECTION, &count)) && count > 0;
    }
    void checkNewWindow(bool enabled) {
        if (settled) return;
        if (!enabled || folders.size() > 1 || GetTickCount64() - opened > 20000) { settle(); return; }
        if (offered || active < 0 || !merge::live(bar)) return;
        HWND host = merge::target(bar);
        if (!host) {
            presentation.finish();
            // Explorer briefly disables frames while it is creating a sibling.
            if (GetTickCount64() - opened >= 250) settle();
            return;
        }
        if (!offeredFolder || !ILIsEqual(offeredFolder.get(), folders[active].get())) {
            offeredFolder.reset(ILCloneFull(folders[active].get()));
            return;
        }
        if (hasSelection()) { settle(); return; }
        LPARAM birth = reinterpret_cast<LPARAM>(GetPropW(bar, merge::birthProp));
        if (!localOffer.publish(bar, host, birth, offeredFolder.get())) {
            // The cross-process receiver reads the folder published by ShellWindows.
            Location published = merge::folderOf(bar);
            if (!published || !ILIsEqual(published.get(), offeredFolder.get())) return;
        }
        if (!SetPropW(bar, merge::offerProp, host)) { settle(); return; }
        DWORD_PTR result = 0;
        if (!SendMessageTimeoutW(host, merge::request(), reinterpret_cast<WPARAM>(bar), birth,
                                 SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &result)) {
            RemovePropW(bar, merge::offerProp);
            return;
        }
        if (result) offered = true;
        else RemovePropW(bar, merge::offerProp); // A concurrent transfer may own the target this tick.
    }
    void received(HWND host, LPARAM birth) {
        if (!offered || settled || !validSourceAck(host, birth)) return;
        bool confirmedLocally = localOffer.confirmed();
        Location landed = confirmedLocally ? Location{} : merge::folderOf(host);
        Location actual = current();
        HWND root = merge::frame(bar);
        if (!root || !IsWindowEnabled(root) || folders.size() != 1 || !offeredFolder ||
            (actual && !ILIsEqual(actual.get(), offeredFolder.get())) || hasSelection() || !validSourceAck(host, birth)) {
            settle();
            return;
        }
        if (!actual || (!confirmedLocally && (!landed || !ILIsEqual(actual.get(), landed.get())))) {
            confirming = host;
            confirmingBirth = birth;
            schedule(25);
            return;
        }
        closingMergedFrame = true;
        settle();
        HWND target = merge::frame(host);
        if (IsIconic(target)) ShowWindowAsync(target, SW_RESTORE);
        if (GetForegroundWindow() == root) SetForegroundWindow(target);
        if (!PostMessageW(root, WM_CLOSE, 0, 0)) presentation.finish();
    }
    bool validSourceAck(HWND host, LPARAM birth) const {
        return merge::enabled() && merge::live(host) && merge::live(bar) && GetPropW(bar, merge::offerProp) == host &&
            reinterpret_cast<LPARAM>(GetPropW(bar, merge::birthProp)) == birth;
    }
    void tick() {
        const auto now = GetTickCount64();
        if (!IsWindowVisible(bar)) {
            // ShowDW can precede the first display of the parent frame.
            schedule(!settled && now - opened < 2000 ? 25 : 500);
            return;
        }
        if (now >= nextThemeCheck) {
            theme.refresh(bar, false);
            nextThemeCheck = now + 1000;
        }
        bool enabled = merge::enabled();
        const int state = enabled ? BST_CHECKED : BST_UNCHECKED;
        if (SendMessageW(mergeButton, BM_GETCHECK, 0, 0) != state)
            SendMessageW(mergeButton, BM_SETCHECK, state, 0);
        sync();
        checkNewWindow(enabled);
        if (confirming) received(confirming, confirmingBirth);
        UINT period = 500;
        if (confirming || destination >= 0 || (!settled && !offered))
            period = destination >= 0 || now - opened < 1000 ? 25 : 100;
        schedule(period);
    }

    int scale(int n) const {
        HDC dc = GetDC(bar);
        int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
        if (dc) ReleaseDC(bar, dc);
        return MulDiv(n, dpi, 96);
    }
    Location current() {
        Com<IShellView> view;
        Com<IFolderView> folderView;
        Com<IPersistFolder2> folder;
        PIDLIST_ABSOLUTE id = nullptr;
        if (browser.p && SUCCEEDED(browser->QueryActiveShellView(&view.p)) && view.p &&
            SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&folderView.p))) &&
            SUCCEEDED(folderView->GetFolder(IID_PPV_ARGS(&folder.p))) && folder.p) {
            folder->GetCurFolder(&id);
        }
        return Location(id);
    }
    bool label(int i, bool insert) {
        PWSTR name = nullptr;
        SHGetNameFromIDList(folders[i].get(), SIGDN_NORMALDISPLAY, &name);
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = name && *name ? name : const_cast<wchar_t*>(L"文件夹");
        bool ok = insert ? TabCtrl_InsertItem(tabs, i, &item) >= 0
                         : TabCtrl_SetItem(tabs, i, &item) != FALSE;
        CoTaskMemFree(name);
        return ok;
    }
    void controls() {
        TabCtrl_SetCurSel(tabs, active);
        EnableWindow(tabs, destination < 0);
        EnableWindow(add, active >= 0 && destination < 0);
        EnableWindow(close, folders.size() > 1 && destination < 0);
    }
    int append(Location location, bool activate = true) {
        if (!location) return -1;
        int index = static_cast<int>(folders.size());
        folders.push_back(std::move(location));
        if (!label(index, true)) { folders.pop_back(); return -1; }
        if (activate) active = index;
        controls();
        return index;
    }
    void erase(int index) {
        TabCtrl_DeleteItem(tabs, index);
        folders.erase(folders.begin() + index);
        if (active > index) --active;
    }
    void completeSwitch() {
        int previous = active;
        active = destination;
        destination = -1;
        if (closeAfterSwitch) erase(previous);
        closeAfterSwitch = false;
        endImport(true);
        controls();
    }
    void sync() {
        if (!tabs) return;
        Location actual = current();
        if (destination >= 0) {
            if (actual && ILIsEqual(actual.get(), folders[destination].get())) {
                completeSwitch();
                return;
            }
            if (GetTickCount64() < deadline) return;
            destination = -1;
            closeAfterSwitch = false;
            endImport(false);
            controls();
            MessageBeep(MB_ICONWARNING);
        }
        if (!actual) return;
        if (active < 0) append(std::move(actual));
        else if (!ILIsEqual(actual.get(), folders[active].get())) {
            folders[active] = std::move(actual);
            label(active, false);
        }
    }
    void select(int index, bool removePrevious = false) {
        if (destination >= 0 || index < 0 || index >= static_cast<int>(folders.size())) return;
        sync();
        if (index == active || active < 0) return;
        destination = index;
        closeAfterSwitch = removePrevious;
        deadline = GetTickCount64() + 15000;
        if (ILIsEqual(folders[active].get(), folders[index].get())) {
            if (!incoming) { completeSwitch(); return; }
            sync();
            if (destination < 0) return;
        }
        schedule(25);
        // Keep the source tab until Explorer confirms the target, including on a slow share.
        controls();
        Location target(ILCloneFull(folders[index].get()));
        HRESULT hr = target ? browser->BrowseObject(target.get(), SBSP_ABSOLUTE | SBSP_SAMEBROWSER)
                            : E_OUTOFMEMORY;
        if (FAILED(hr) && destination >= 0) {
            destination = -1;
            closeAfterSwitch = false;
            endImport(false);
            controls();
            MessageBeep(MB_ICONWARNING);
        } else sync();
    }
    void newTab() {
        if (destination < 0) {
            settle();
            sync();
            if (active >= 0) append(Location(ILCloneFull(folders[active].get())));
        }
    }
    void closeTab(int index) {
        if (destination >= 0 || folders.size() <= 1 || index < 0) return;
        if (index == active) select(index > 0 ? index - 1 : 1, true);
        else { erase(index); controls(); }
    }
    void layout() {
        RECT r{};
        GetClientRect(bar, &r);
        int mergeWidth = std::min(scale(102), static_cast<int>(r.right) / 3);
        int buttonWidth = std::min(scale(52), static_cast<int>(r.right) / 5);
        int width = r.right - 2 * buttonWidth - mergeWidth;
        MoveWindow(tabs, 0, 0, width, r.bottom, TRUE);
        MoveWindow(add, width, 0, buttonWidth, r.bottom, TRUE);
        MoveWindow(close, width + buttonWidth, 0, buttonWidth, r.bottom, TRUE);
        MoveWindow(mergeButton, width + 2 * buttonWidth, 0, mergeWidth, r.bottom, TRUE);
    }
    bool key(WPARAM vk) {
        if (!(GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000)) return false;
        if (vk == 'T') newTab();
        else if (vk == 'W') closeTab(active);
        else if (vk == VK_TAB && !folders.empty()) {
            int count = static_cast<int>(folders.size());
            select((active + ((GetKeyState(VK_SHIFT) & 0x8000) ? count - 1 : 1)) % count);
        } else return false;
        return true;
    }
    static LRESULT CALLBACK childProc(HWND w, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
        auto* self = reinterpret_cast<Band*>(data);
        try {
            if (self->theme.dark) {
                if (msg == WM_ERASEBKGND) return 1;
                if (msg == WM_PAINT || msg == WM_PRINTCLIENT) {
                    PAINTSTRUCT ps{};
                    HDC dc = msg == WM_PAINT ? BeginPaint(w, &ps) : reinterpret_cast<HDC>(wp);
                    if (dc) self->theme.paint(w, self->tabs, self->mergeButton, dc);
                    if (msg == WM_PAINT) EndPaint(w, &ps);
                    return 0;
                }
                if (msg == WM_MOUSEMOVE || msg == WM_MOUSELEAVE) self->theme.pointer(w, self->tabs, msg, lp);
                if (msg == WM_ENABLE || msg == WM_SETFOCUS || msg == WM_KILLFOCUS ||
                    msg == WM_UPDATEUISTATE || msg == BM_SETCHECK || msg == BM_SETSTATE ||
                    msg == TCM_SETCURSEL || msg == WM_LBUTTONUP || msg == WM_LBUTTONDOWN)
                    InvalidateRect(w, nullptr, FALSE);
            }
            if (msg == WM_MBUTTONUP && w == self->tabs) {
                TCHITTESTINFO hit{};
                hit.pt = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
                self->closeTab(TabCtrl_HitTest(w, &hit));
                return 0;
            }
            if (msg == WM_KEYDOWN && self->key(wp)) return 0;
            if ((msg == WM_SETFOCUS || msg == WM_KILLFOCUS) && self->site.p) {
                Com<IInputObjectSite> input;
                if (SUCCEEDED(self->site->QueryInterface(IID_PPV_ARGS(&input.p))))
                    input->OnFocusChangeIS(static_cast<IDeskBand*>(self), msg == WM_SETFOCUS);
            }
        } catch (const std::bad_alloc&) { MessageBeep(MB_ICONWARNING); }
        return DefSubclassProc(w, msg, wp, lp);
    }
    static LRESULT CALLBACK barProc(HWND w, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
        auto* self = reinterpret_cast<Band*>(data);
        try {
            if (msg == merge::request()) return self->reserve(reinterpret_cast<HWND>(wp), lp);
            if (msg == merge::retry()) {
                if (self->validSourceAck(reinterpret_cast<HWND>(wp), lp)) {
                    self->offered = false;
                    self->schedule(GetTickCount64() - self->opened < 1000 ? 25 : 100);
                }
                return 0;
            }
            if (msg == importMessage || msg == merge::received() || msg == WM_TIMER || msg == refreshMessage) {
                Com<IDeskBand> guard;
                self->AddRef();
                guard.p = self;
                if (msg == importMessage) self->importWindow();
                else if (msg == WM_TIMER || msg == refreshMessage) self->tick();
                else self->received(reinterpret_cast<HWND>(wp), lp);
                return 0;
            }
            switch (msg) {
            case WM_THEMECHANGED: case WM_SYSCOLORCHANGE: case WM_SETTINGCHANGE:
                self->theme.refresh(w, true);
                break;
            case WM_ERASEBKGND:
                if (self->theme.dark) { self->theme.erase(reinterpret_cast<HDC>(wp), w); return 1; }
                break;
            case WM_SIZE: self->layout(); break;
            case WM_SETFOCUS: SetFocus(self->tabs); break;
            case WM_COMMAND:
                if (HIWORD(wp) == BN_CLICKED) {
                    if (LOWORD(wp) == NewTab) self->newTab();
                    if (LOWORD(wp) == CloseTab) self->closeTab(self->active);
                    if (LOWORD(wp) == MergeWindows) {
                        self->settle();
                        if (!merge::setEnabled(SendMessageW(self->mergeButton, BM_GETCHECK, 0, 0) == BST_CHECKED))
                            MessageBeep(MB_ICONWARNING);
                        self->tick();
                    }
                }
                break;
            case WM_NOTIFY: {
                auto* note = reinterpret_cast<NMHDR*>(lp);
                if (note->hwndFrom == self->tabs && note->code == TCN_SELCHANGE)
                    self->select(TabCtrl_GetCurSel(self->tabs));
                break;
            }
            case WM_NCDESTROY: {
                HWND root = merge::frame(w);
                if (root && GetPropW(root, merge::bandProp) == w) RemovePropW(root, merge::bandProp);
                RemovePropW(w, merge::birthProp);
                RemovePropW(w, merge::offerProp);
                self->bar = self->tabs = self->add = self->close = self->mergeButton = nullptr;
                break;
            }
            }
        } catch (const std::bad_alloc&) { MessageBeep(MB_ICONWARNING); }
        return DefSubclassProc(w, msg, wp, lp);
    }
    HRESULT attach(IUnknown* host) {
        Com<IOleWindow> owner;
        Com<IServiceProvider> services;
        HWND parent = nullptr;
        HRESULT hr = host->QueryInterface(IID_PPV_ARGS(&owner.p));
        if (FAILED(hr)) return hr;
        hr = owner->GetWindow(&parent);
        if (FAILED(hr) || !parent) return E_FAIL;
        hr = host->QueryInterface(IID_PPV_ARGS(&services.p));
        if (FAILED(hr)) return hr;
        hr = services->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser.p));
        if (FAILED(hr)) hr = services->QueryService(SID_SShellBrowser, IID_PPV_ARGS(&browser.p));
        if (FAILED(hr)) return hr;
        site.p = host;
        host->AddRef();
        INITCOMMONCONTROLSEX init{sizeof(init), ICC_TAB_CLASSES};
        if (!InitCommonControlsEx(&init)) return E_FAIL;
        bar = CreateWindowExW(WS_EX_CONTROLPARENT, L"STATIC", L"EZFolder", WS_CHILD | WS_CLIPCHILDREN,
                              0, 0, 0, 0, parent, nullptr, module, nullptr);
        if (!bar || !SetWindowSubclass(bar, barProc, 1, reinterpret_cast<DWORD_PTR>(this))) return E_FAIL;
        opened = GetTickCount64();
        HWND root = merge::frame(bar);
        const bool mergeEnabled = merge::enabled();
        settled = !mergeEnabled || !root || GetPropW(root, L"EZFolder.KeepExistingFrame");
        offered = false;
        closingMergedFrame = false;
        SetPropW(bar, merge::birthProp, reinterpret_cast<HANDLE>(opened + 1));
        if (root) SetPropW(root, merge::bandProp, bar);
        if (!settled && merge::target(bar)) presentation.begin(root);
        auto child = [&](LPCWSTR type, LPCWSTR title, int id, DWORD style) {
            return CreateWindowExW(0, type, title, WS_VISIBLE | WS_CHILD | WS_TABSTOP | style,
                    0, 0, 0, 0, bar, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), module, nullptr);
        };
        tabs = child(WC_TABCONTROLW, L"文件夹标签", TabControl, TCS_SINGLELINE | TCS_FOCUSONBUTTONDOWN | WS_CLIPCHILDREN);
        add = child(WC_BUTTONW, L"新建", NewTab, BS_PUSHBUTTON);
        close = child(WC_BUTTONW, L"关闭", CloseTab, BS_PUSHBUTTON);
        mergeButton = child(WC_BUTTONW, L"合并新窗口", MergeWindows, BS_AUTOCHECKBOX);
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
            font = CreateFontIndirectW(&metrics.lfMessageFont);
        for (HWND w : {tabs, add, close, mergeButton}) {
            if (!w || !SetWindowSubclass(w, childProc, 1, reinterpret_cast<DWORD_PTR>(this))) return E_FAIL;
            SendMessageW(w, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : GetStockObject(DEFAULT_GUI_FONT)), FALSE);
        }
        TabCtrl_SetPadding(tabs, scale(11), scale(3));
        theme.refresh(bar, true);
        SendMessageW(mergeButton, BM_SETCHECK, mergeEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        controls();
        sync();
        if (!settled && active >= 0) offeredFolder.reset(ILCloneFull(folders[active].get()));
        layout();
        return schedule(settled ? 500 : 25) ? S_OK : E_FAIL;
    }
    void detach() {
        settle();
        presentation.finish();
        incoming = nullptr;
        awaitingAck = nullptr;
        importIndex = -1;
        if (bar) DestroyWindow(bar);
        bar = tabs = add = close = mergeButton = nullptr;
        if (font) { DeleteObject(font); font = nullptr; }
        folders.clear();
        active = destination = -1;
        closeAfterSwitch = false;
        timerPeriod = 0;
        browser.reset();
        site.reset();
    }
public:
    Band() { InterlockedIncrement(&objects); }
    ~Band() { detach(); InterlockedDecrement(&objects); }
    STDMETHODIMP QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == IID_IUnknown || id == IID_IOleWindow || id == IID_IDockingWindow || id == IID_IDeskBand)
            *out = static_cast<IDeskBand*>(this);
        else if (id == IID_IObjectWithSite) *out = static_cast<IObjectWithSite*>(this);
        else if (id == IID_IInputObject) *out = static_cast<IInputObject*>(this);
        else if (id == IID_IPersist || id == IID_IPersistStream) *out = static_cast<IPersistStream*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs); }
    STDMETHODIMP_(ULONG) Release() override { ULONG n = InterlockedDecrement(&refs); if (!n) delete this; return n; }
    STDMETHODIMP SetSite(IUnknown* host) override {
        detach();
        if (!host) return S_OK;
        HRESULT hr;
        try { hr = attach(host); } catch (const std::bad_alloc&) { hr = E_OUTOFMEMORY; }
        if (FAILED(hr)) detach();
        return hr;
    }
    STDMETHODIMP GetSite(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return site.p ? site->QueryInterface(id, out) : E_FAIL;
    }
    STDMETHODIMP GetWindow(HWND* out) override { if (!out) return E_POINTER; *out = bar; return bar ? S_OK : E_FAIL; }
    STDMETHODIMP ContextSensitiveHelp(BOOL) override { return E_NOTIMPL; }
    STDMETHODIMP ShowDW(BOOL show) override {
        if (bar) {
            if (!show && !closingMergedFrame) presentation.finish();
            ShowWindow(bar, show ? SW_SHOWNA : SW_HIDE);
            if (show) PostMessageW(bar, refreshMessage, 0, 0);
        }
        return S_OK;
    }
    STDMETHODIMP CloseDW(DWORD) override { detach(); return S_OK; }
    STDMETHODIMP ResizeBorderDW(LPCRECT, IUnknown*, BOOL) override { return E_NOTIMPL; }
    STDMETHODIMP GetBandInfo(DWORD, DWORD, DESKBANDINFO* info) override {
        if (!info) return E_POINTER;
        LONG height = scale(30);
        if (info->dwMask & DBIM_MINSIZE) info->ptMinSize = {scale(290), height};
        if (info->dwMask & DBIM_MAXSIZE) info->ptMaxSize = {-1, height};
        if (info->dwMask & DBIM_INTEGRAL) info->ptIntegral = {1, 1};
        if (info->dwMask & DBIM_ACTUAL) info->ptActual = {scale(520), height};
        if (info->dwMask & DBIM_TITLE) info->wszTitle[0] = 0;
        if (info->dwMask & DBIM_MODEFLAGS) info->dwModeFlags = DBIMF_NORMAL;
        info->dwMask &= ~DBIM_BKCOLOR;
        return S_OK;
    }
    STDMETHODIMP UIActivateIO(BOOL on, MSG*) override { if (on && tabs) SetFocus(tabs); return S_OK; }
    STDMETHODIMP HasFocusIO() override { HWND w = GetFocus(); return bar && (w == bar || IsChild(bar,w)) ? S_OK : S_FALSE; }
    STDMETHODIMP TranslateAcceleratorIO(MSG* m) override {
        if (!m || m->message != WM_KEYDOWN || HasFocusIO() != S_OK) return S_FALSE;
        try { return key(m->wParam) ? S_OK : S_FALSE; } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    }
    STDMETHODIMP GetClassID(CLSID* out) override { if (!out) return E_POINTER; *out = clsid; return S_OK; }
    STDMETHODIMP IsDirty() override { return S_FALSE; }
    STDMETHODIMP Load(IStream*) override { return S_OK; }
    STDMETHODIMP Save(IStream*, BOOL) override { return S_OK; }
    STDMETHODIMP GetSizeMax(ULARGE_INTEGER* out) override { if (!out) return E_POINTER; out->QuadPart = 0; return S_OK; }
};

class Factory final : public IClassFactory {
    LONG refs = 1;
public:
    Factory() { InterlockedIncrement(&objects); }
    ~Factory() { InterlockedDecrement(&objects); }
    STDMETHODIMP QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id != IID_IUnknown && id != IID_IClassFactory) return E_NOINTERFACE;
        *out = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs); }
    STDMETHODIMP_(ULONG) Release() override { ULONG n = InterlockedDecrement(&refs); if (!n) delete this; return n; }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        auto* obj = new(std::nothrow) Band;
        if (!obj) return E_OUTOFMEMORY;
        HRESULT hr = obj->QueryInterface(id, out);
        obj->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL lock) override {
        if (lock) InterlockedIncrement(&objects); else InterlockedDecrement(&objects);
        return S_OK;
    }
};

extern "C" __declspec(dllexport) HRESULT WINAPI DllGetClassObject(REFCLSID id, REFIID iid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (id != clsid) return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new(std::nothrow) Factory;
    if (!factory) return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(iid, out);
    factory->Release();
    return hr;
}
extern "C" __declspec(dllexport) HRESULT WINAPI DllCanUnloadNow() {
    return InterlockedCompareExchange(&objects, 0, 0) ? S_FALSE : S_OK;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) module = instance;
    return TRUE;
}
