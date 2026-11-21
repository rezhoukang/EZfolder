#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#define NTDDI_VERSION 0x0A000000
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shlguid.h>
#include <oleacc.h>
#include <algorithm>
#include <memory>
#include <new>
#include <string>
#include <tuple>
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
#include "desktop_open.h"
#include "band_theme.h"
#include "settings.h"

class Band final : public IDeskBand, public IObjectWithSite, public IInputObject, public IPersistStream {
    LONG refs = 1;
    Com<IUnknown> site;
    Com<IShellBrowser> browser;
    HWND bar = nullptr, tabs = nullptr, add = nullptr;
    SIZE stripSize{-1,-1}, itemSize{-1,-1};
    HWND tooltip = nullptr;
    TOOLINFOW tipTool{};
    int hoverIndex = -1;
    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> tipTitle{nullptr,CoTaskMemFree};
    HFONT font = nullptr;
    BandTheme theme;
    ULONGLONG nextThemeCheck = 0;
    UINT timerPeriod = 0;
    struct Tab { Location folder; std::wstring title, path; };
    std::vector<Tab> folders;
    int active = -1, destination = -1;
    // The default page after closing the last tab has no tab of its own.
    Location nativeLocation, closingTo;
    bool closeAfterSwitch = false;
    bool rememberRemoval = false, restoring = false, copyToWindow = false, dragging = false;
    std::vector<Location> closed;
    std::wstring dragText;
    HWND statusBar = nullptr, statusHost = nullptr, loadingCover = nullptr, bandParent = nullptr;
    ULONGLONG loadingSince = 0;
    int pendingNew = -1;
    int pressedTab = -1;
    bool pressedClose = false;
    POINT pressPoint{};
    int separating = -1;
    HWND independent = nullptr;
    Location separationFolder;
    ULONGLONG separationDeadline = 0;
    POINT separationPoint{};
    ULONGLONG deadline = 0;
    enum { NewTab = 100, TabControl = 102 };
    static constexpr UINT importMessage = WM_APP + 27;
    static constexpr UINT refreshMessage = WM_APP + 28;
    static constexpr UINT separateMessage = WM_APP + 29;
    static constexpr UINT shortcutMessage = WM_APP + 30;
    static constexpr UINT navigateMessage = WM_APP + 31;
    int queuedTab = -1;
    bool presentQueued = false;
    bool watchingKeys = false;
    Band* nextOnThread = nullptr;
    inline static thread_local HHOOK threadKeys = nullptr;
    inline static thread_local Band* threadBands = nullptr;
    HWND incoming = nullptr;
    HWND awaitingAck = nullptr;
    LPARAM incomingBirth = 0;
    int importIndex = -1;
    bool settled = false, offered = false;
    ULONGLONG opened = 0;
    HWND foregroundAtOpen = nullptr;
    DWORD inputAtOpen = 0;
    Location offeredFolder;
    merge::LocalOffer localOffer;
    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> offeredName{nullptr,CoTaskMemFree};
    bool storing = false;
    int nextImported = -1;
    ULONGLONG activateAfter = 0, firstImportedAt = 0;
    HWND confirming = nullptr;
    LPARAM confirmingBirth = 0;
    bool confirmingStored = false, nameMatched = false;
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
    // Ordinary handoffs store a background tab. Only the final tab in a burst
    // needs a shell view. Keep the older navigation handshake for older peers.
    bool canStore(HWND source, LPARAM birth) const {
        return settled && !closingMergedFrame && !closingTo && (active >= 0 || nativeLocation) && !incoming && separating < 0 &&
            IsWindowEnabled(merge::frame(bar)) && validOffer(source,birth);
    }
    bool matchesOffer(HWND host, const COPYDATASTRUCT& packet) {
        if (!offered || settled || !offeredName || packet.dwData != merge::verifyName() ||
            !validSourceAck(host,reinterpret_cast<LPARAM>(GetPropW(bar,merge::birthProp)))) return false;
        size_t bytes = (wcslen(offeredName.get())+1)*sizeof(wchar_t);
        if (packet.cbData != bytes || !packet.lpData || memcmp(packet.lpData,offeredName.get(),bytes)) return false;
        nameMatched = true;
        return true;
    }
    bool storeOffer(HWND source, LPARAM birth) {
        if (storing || !canStore(source,birth)) return false;
        storing = true;
        struct Reset { bool& busy; ~Reset() { busy = false; } } reset{storing};
        Location id = merge::LocalOffer::read(source,bar,birth);
        if (!id) {
            // ShellWindows supplies the PIDL. Confirm its absolute name against the
            // source snapshot, since the source can navigate while COM pumps messages.
            id = merge::folderOf(source);
            PWSTR name = nullptr;
            if (!id || FAILED(SHGetNameFromIDList(id.get(),SIGDN_DESKTOPABSOLUTEPARSING,&name)) || !name) return false;
            std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(name,CoTaskMemFree);
            COPYDATASTRUCT packet{merge::verifyName(),static_cast<DWORD>((wcslen(name)+1)*sizeof(wchar_t)),name};
            DWORD_PTR matches = 0;
            if (!SendMessageTimeoutW(source,WM_COPYDATA,reinterpret_cast<WPARAM>(bar),reinterpret_cast<LPARAM>(&packet),
                                     SMTO_ABORTIFHUNG | SMTO_BLOCK,200,&matches) || !matches) return false;
        }
        if (!canStore(source,birth)) return false;
        merge::markReceiver(bar);
        bool added=false;
        int index = tabForOpen(id,&added);
        if (index < 0) return false;
        merge::LocalOffer::confirm(source,bar,birth,id ? id.get() : folders[index].folder.get());
        if (!PostMessageW(source,merge::storedTab(),reinterpret_cast<WPARAM>(bar),birth)) {
            if (added) erase(index);
            controls();
            return false;
        }
        index=moveTabToEnd(index);
        auto now = GetTickCount64();
        MergeVisibility::stored(bar,now);
        HWND foreground = GetForegroundWindow();
        LASTINPUTINFO input{sizeof(input),0};
        if (foreground == merge::frame(source) || (foreground && foreground == GetPropW(source,merge::foregroundProp) &&
            GetLastInputInfo(&input) && ULONG_PTR(input.dwTime)+1 == reinterpret_cast<ULONG_PTR>(GetPropW(source,merge::inputProp)))) {
            HWND root = merge::frame(bar);
            if (IsIconic(root)) ShowWindow(root,SW_RESTORE);
            if (foreground != root) SetForegroundWindow(root);
        }
        bool single = nextImported < 0 && !MergeVisibility::pending(bar,merge::frame(source));
        if (nextImported < 0) firstImportedAt = now;
        nextImported = index;
        if (index!=active || destination>=0) showLoading(index);
        activateAfter = single ? now : now+120;
        schedule(25);
        if (single && destination < 0 && (!awaitingAck || GetPropW(awaitingAck,merge::offerProp) != bar))
            select(index);
        return true;
    }
    void endImport(bool success) {
        if (!incoming) return;
        if (success && validOffer(incoming, incomingBirth)) {
            merge::LocalOffer::confirm(incoming, bar, incomingBirth, folders[active].folder.get());
            if (PostMessageW(incoming, merge::received(), reinterpret_cast<WPARAM>(bar), incomingBirth))
                awaitingAck = incoming;
        }
        if (!success && importIndex >= 0 && importIndex != active) erase(importIndex);
        incoming = nullptr;
        importIndex = -1;
    }
    LRESULT reserve(HWND source, LPARAM birth) {
        if (!settled || storing || (active < 0 && !nativeLocation) || destination >= 0 || incoming ||
            (awaitingAck && GetPropW(awaitingAck, merge::offerProp) == bar) || separating >= 0 ||
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
        if (!validOffer(source, incomingBirth)) {
            if (validOffer(source,incomingBirth)) PostMessageW(source,merge::retry(),reinterpret_cast<WPARAM>(bar),incomingBirth);
            endImport(false);
            return;
        }
        if (!id) {
            // A visible view can precede its registration in ShellWindows.
            PostMessageW(source, merge::retry(), reinterpret_cast<WPARAM>(bar), incomingBirth);
            endImport(false);
            return;
        }
        bool added=false;
        int index=tabForOpen(id,&added);
        if (index>=0) index=moveTabToEnd(index);
        importIndex=added ? index : -1;
        if (index<0) {
            PostMessageW(source,merge::retry(),reinterpret_cast<WPARAM>(bar),incomingBirth);
            endImport(false);
        } else if (index==active) endImport(true);
        else select(index);
    }
    void settle() {
        if (!closingMergedFrame) presentation.finish();
        settled = true;
        if (bar && !closingMergedFrame) {
            SetPropW(bar,merge::readyProp,reinterpret_cast<HANDLE>(1));
            RemovePropW(bar,merge::startingProp);
        }
        confirming = nullptr;
        if (bar) RemovePropW(bar, merge::offerProp);
        localOffer.clear();
        offeredFolder.reset();
        offeredName.reset();
    }
    bool hasSelection() {
        Com<IShellView> view;
        Com<IFolderView> folder;
        int count = 0;
        return browser.p && SUCCEEDED(browser->QueryActiveShellView(&view.p)) && view.p &&
            SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&folder.p))) &&
            SUCCEEDED(folder->ItemCount(SVGIO_SELECTION, &count)) && count > 0;
    }
    void checkNewWindow() {
        if (settled) return;
        if (GetPropW(merge::frame(bar), merge::keepFrame) || folders.size() > 1 || GetTickCount64() - opened > 20000) { settle(); return; }
        if (offered || active < 0 || !merge::live(bar)) return;
        HWND host = merge::target(bar,false,nullptr,folders[active].folder.get());
        if (!host) {
            // Explorer briefly disables frames while it is creating a sibling.
            if (HWND candidate = merge::target(bar,true)) presentation.begin(merge::frame(bar),candidate);
            else if (GetTickCount64() - opened >= 250) settle();
            return;
        }
        presentation.begin(merge::frame(bar),host);
        if (GetPropW(host,merge::startingProp)) return;
        if (!offeredFolder || !ILIsEqual(offeredFolder.get(), folders[active].folder.get())) {
            offeredFolder.reset(ILCloneFull(folders[active].folder.get()));
            return;
        }
        if (hasSelection()) { settle(); return; }
        LPARAM birth = reinterpret_cast<LPARAM>(GetPropW(bar, merge::birthProp));
        bool local = localOffer.publish(bar, host, birth, offeredFolder.get());
        bool modern = GetPropW(host,merge::readyProp) != nullptr;
        nameMatched = false;
        offeredName.reset();
        if (!local && modern) {
            PWSTR name = nullptr;
            if (SUCCEEDED(SHGetNameFromIDList(offeredFolder.get(),SIGDN_DESKTOPABSOLUTEPARSING,&name))) offeredName.reset(name);
            modern = offeredName != nullptr;
        }
        if (!local && !modern) {
            // The cross-process receiver reads the folder published by ShellWindows.
            Location published = merge::folderOf(bar);
            if (!published || !ILIsEqual(published.get(), offeredFolder.get())) return;
        }
        if (!SetPropW(bar, merge::offerProp, host)) { settle(); return; }
        if (modern) {
            // The shell may have granted the new Explorer process foreground rights.
            // Share those rights only with the Explorer process receiving this open.
            DWORD targetPid = 0;
            GetWindowThreadProcessId(host,&targetPid);
            if (targetPid && targetPid != GetCurrentProcessId() && shouldActivate(merge::frame(bar)))
                AllowSetForegroundWindow(targetPid);
            offered = PostMessageW(host,merge::storeTab(),reinterpret_cast<WPARAM>(bar),birth) != FALSE;
            if (!offered) RemovePropW(bar,merge::offerProp);
            return;
        }
        DWORD_PTR result = 0;
        if (!SendMessageTimeoutW(host, merge::request(), reinterpret_cast<WPARAM>(bar), birth,
                                 SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &result)) {
            RemovePropW(bar, merge::offerProp);
            return;
        }
        if (result) offered = true;
        else RemovePropW(bar, merge::offerProp); // A concurrent transfer may own the target this tick.
    }
    void received(HWND host, LPARAM birth, bool stored = false) {
        if (!offered || settled || !validSourceAck(host, birth)) return;
        bool confirmedLocally = localOffer.confirmed() || (stored && nameMatched);
        if (stored && !confirmedLocally) return;
        Location landed = confirmedLocally ? Location{} : merge::folderOf(host);
        Location actual = current();
        HWND root = merge::frame(bar);
        if (!root || folders.size() != 1 || !offeredFolder ||
            (actual && !ILIsEqual(actual.get(), offeredFolder.get())) || hasSelection() || !validSourceAck(host, birth)) {
            settle();
            return;
        }
        if (!actual || !IsWindowEnabled(root) || (!confirmedLocally && (!landed || !ILIsEqual(actual.get(), landed.get())))) {
            confirming = host;
            confirmingBirth = birth;
            confirmingStored = stored;
            schedule(25);
            return;
        }
        closingMergedFrame = true;
        settle();
        HWND target = merge::frame(host);
        if (target && shouldActivate(root)) {
            if (IsIconic(target)) ShowWindowAsync(target, SW_RESTORE);
            if (GetForegroundWindow() != target) SetForegroundWindow(target);
        }
        if (!PostMessageW(root, WM_CLOSE, 0, 0)) presentation.finish();
    }
    bool validSourceAck(HWND host, LPARAM birth) const {
        return !GetPropW(merge::frame(bar), merge::keepFrame) && merge::live(host) && merge::live(bar) && GetPropW(bar, merge::offerProp) == host &&
            reinterpret_cast<LPARAM>(GetPropW(bar, merge::birthProp)) == birth;
    }
    bool shouldActivate(HWND source) const {
        HWND foreground = GetForegroundWindow();
        LASTINPUTINFO input{sizeof(input),0};
        return foreground == source || (foreground == foregroundAtOpen &&
            GetLastInputInfo(&input) && input.dwTime == inputAtOpen);
    }
    void tick() {
        const auto now = GetTickCount64();
        if (!IsWindowVisible(bar)) {
            // ShowDW can precede the first display of the parent frame.
            schedule(!settled && now - opened < 2000 ? 25 : 500);
            return;
        }
        if (now >= nextThemeCheck) {
            theme.refresh(bar, tooltip, false);
            if ((!statusBar && !statusHost) || !bandParent) attachMenus();
            nextThemeCheck = now + 1000;
        }
        sync();
        if (queuedTab>=0 && destination<0 && separating<0) navigateQueued();
        if ((destination>=0 || nextImported>=0 || queuedTab>=0) && now>=loadingSince+150 && loadingSince) {
            theme.loading=queuedTab>=0 ? queuedTab : nextImported>=0 ? nextImported : destination;
            InvalidateRect(tabs,nullptr,FALSE);
        }
        checkSeparation();
        checkNewWindow();
        if (loadingSince && queuedTab<0 && destination<0 && nextImported<0) { clearLoading(); controls(); }
        if (confirming) received(confirming, confirmingBirth, confirmingStored);
        if (nextImported >= 0 && destination < 0 && !incoming && separating < 0 && now >= activateAfter &&
            (!awaitingAck || GetPropW(awaitingAck,merge::offerProp) != bar)) {
            if (now-firstImportedAt < 4000 && MergeVisibility::pending(bar)) activateAfter = now+80;
            else select(nextImported);
        }
        UINT period = 500;
        if (nextImported >= 0) period = 25;
        else if (separating >= 0 || confirming || destination >= 0 || (!settled && !offered))
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
    bool writeTitle(int i, bool insert=false) {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = folders[i].title.data();
        return insert ? TabCtrl_InsertItem(tabs,i,&item)>=0 : TabCtrl_SetItem(tabs,i,&item)!=FALSE;
    }
    static std::wstring filePath(PCIDLIST_ABSOLUTE id) {
        PWSTR raw=nullptr;
        SHGetNameFromIDList(id,SIGDN_FILESYSPATH,&raw);
        std::unique_ptr<wchar_t,decltype(&CoTaskMemFree)> path(raw,CoTaskMemFree);
        return raw ? raw : L"";
    }
    bool label(int i, bool insert) {
        if (i == hoverIndex) hideTitle();
        PWSTR raw = nullptr;
        SHGetNameFromIDList(folders[i].folder.get(), SIGDN_NORMALDISPLAY, &raw);
        std::unique_ptr<wchar_t,decltype(&CoTaskMemFree)> name(raw,CoTaskMemFree);
        folders[i].title = raw && *raw ? raw : L"文件夹";
        folders[i].path = filePath(folders[i].folder.get());
        return writeTitle(i,insert);
    }
    int matchingTab(PCIDLIST_ABSOLUTE id) const {
        // Explicitly created tabs can already share a location. Prefer the active
        // view, then the rightmost match, without disturbing the other copies.
        if (active>=0 && !(closeAfterSwitch && destination>=0) && ILIsEqual(folders[active].folder.get(),id)) return active;
        for (int i=static_cast<int>(folders.size())-1;i>=0;--i)
            if (i!=active && ILIsEqual(folders[i].folder.get(),id)) return i;
        // This PC can expose the same disk directory through a different namespace PIDL.
        auto path=filePath(id);
        if (!path.empty()) {
            if (active>=0 && !(closeAfterSwitch && destination>=0) && folders[active].path==path) return active;
            for (int i=static_cast<int>(folders.size())-1;i>=0;--i)
                if (i!=active && folders[i].path==path) return i;
        }
        return -1;
    }
    int moveTabToEnd(int index, int* tracked=nullptr) {
        int last=static_cast<int>(folders.size())-1;
        if (index==last) return last;
        hideTitle();
        std::rotate(folders.begin()+index,folders.begin()+index+1,folders.end());
        for (int* slot : {&active,&destination,&queuedTab,&nextImported,&pendingNew,&importIndex,&pressedTab,&theme.loading,&separating,tracked}) {
            if (!slot) continue;
            if (*slot==index) *slot=last;
            else if (*slot>index) --*slot;
        }
        for (int i=index;i<=last;++i) writeTitle(i);
        controls();
        return last;
    }
    int tabForOpen(Location& id,bool* added=nullptr) {
        // Explorer can navigate inside the active tab between timer ticks.
        sync();
        int match=matchingTab(id.get());
        if (added) *added=match<0;
        if (match>=0) return match;
        return hasRoom() ? append(std::move(id),false) : -1;
    }
    void controls() {
        if (!tabs || !add) return;
        int shown = loadingSince && queuedTab>=0 ? queuedTab : loadingSince && nextImported>=0 ? nextImported :
            loadingSince && destination>=0 ? destination : active;
        if (TabCtrl_GetCurSel(tabs) != shown) TabCtrl_SetCurSel(tabs,shown);
        EnableWindow(tabs, destination < 0 && separating < 0);
        layout();
        InvalidateRect(tabs, nullptr, FALSE);
    }
    int append(Location location, bool activate = true) {
        if (!location) return -1;
        int index = static_cast<int>(folders.size());
        folders.push_back({std::move(location),{},{}});
        // Reserve space before the native control decides whether to show its overflow buttons.
        layout();
        if (!label(index, true)) { folders.pop_back(); layout(); return -1; }
        if (activate) {
            active = index;
            nativeLocation.reset();
        }
        controls();
        return index;
    }
    void erase(int index) {
        if (index == nextImported) nextImported = -1;
        else if (index < nextImported) --nextImported;
        if (index == queuedTab) queuedTab=-1;
        else if (index < queuedTab) --queuedTab;
        TabCtrl_DeleteItem(tabs, index);
        folders.erase(folders.begin() + index);
        if (active > index) --active;
    }
    void remember(int index) {
        Location snapshot(ILCloneFull(folders[index].folder.get()));
        if (!snapshot) return;
        closed.push_back(std::move(snapshot));
        if (closed.size()>10) closed.erase(closed.begin());
    }
    void clearLoading() {
        if (loadingCover) DestroyWindow(loadingCover);
        loadingCover=nullptr;
        loadingSince=0;
        theme.loading=-1;
    }
    void showLoading(int index) {
        if (!loadingCover) {
            Com<IShellView> view;
            HWND content=nullptr;
            if (SUCCEEDED(browser->QueryActiveShellView(&view.p)) && view.p && SUCCEEDED(view->GetWindow(&content)) && content) {
                HWND parent=GetParent(content);
                RECT r{};
                GetWindowRect(content,&r);
                MapWindowPoints(nullptr,parent,reinterpret_cast<POINT*>(&r),2);
                loadingCover=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"",WS_CHILD|WS_CLIPSIBLINGS,
                    r.left,r.top,r.right-r.left,r.bottom-r.top,parent,nullptr,module,nullptr);
                if (loadingCover) {
                    SetWindowSubclass(loadingCover,coverProc,1,reinterpret_cast<DWORD_PTR>(this));
                    SetWindowPos(loadingCover,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW|SWP_NOACTIVATE);
                    UpdateWindow(loadingCover);
                }
            }
        }
        loadingSince=GetTickCount64();
        theme.loading=-1;
        TabCtrl_SetCurSel(tabs,index);
        RedrawWindow(tabs,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
    }
    void navigateQueued() {
        if (queuedTab<0) return;
        if (presentQueued) {
            presentQueued=false;
            HWND root=merge::frame(bar);
            if (IsIconic(root)) ShowWindow(root,SW_RESTORE);
            if (GetForegroundWindow()!=root) SetForegroundWindow(root);
            if (queuedTab!=active || destination>=0) showLoading(queuedTab);
            PostMessageW(bar,navigateMessage,0,0);
            return;
        }
        if (destination>=0 || separating>=0 || incoming) return;
        int index=queuedTab;
        queuedTab=-1;
        select(index);
        if (destination<0 && nextImported<0 && queuedTab<0) clearLoading();
    }
    bool acceptPath(HWND source,const COPYDATASTRUCT& packet) {
        if (!merge::live(bar) || !settled || closingMergedFrame || separating>=0 ||
            incoming || storing || closingTo || (active<0 && !nativeLocation) || !IsWindowEnabled(merge::frame(bar))) return false;
        bool move=packet.dwData==merge::movePath();
        if (move && (!merge::live(source) || source==bar || GetPropW(source,merge::offerProp)!=bar)) return false;
        if (!packet.lpData || packet.cbData<2*sizeof(wchar_t) || packet.cbData>65536 || packet.cbData%sizeof(wchar_t)) return false;
        auto path=static_cast<const wchar_t*>(packet.lpData);
        size_t length=packet.cbData/sizeof(wchar_t)-1;
        if (path[length] || wmemchr(path,0,length)) return false;
        bool disk=(length>=3 && path[1]==L':' && path[2]==L'\\') || !wcsncmp(path,L"\\\\",2);
        bool query=packet.dwData==merge::containsPath();
        if (!disk && !((move || query) && !wcsncmp(path,L"::{",3))) return false;
        PIDLIST_ABSOLUTE parsed=nullptr;
        SFGAOF attrs=SFGAO_FOLDER;
        if (FAILED(SHParseDisplayName(path,nullptr,&parsed,SFGAO_FOLDER,&attrs))) return false;
        Location id(parsed);
        if (!(attrs&SFGAO_FOLDER) || !bar || separating>=0 || incoming || closingTo) return false;
        if (query) { sync(); return matchingTab(id.get())>=0; }
        if (!move) merge::markReceiver(bar);
        if (move && !hasRoom()) return false;
        int index=move ? append(std::move(id),false) : tabForOpen(id);
        if (index<0) return false;
        index=moveTabToEnd(index);
        queuedTab=index;
        nextImported=-1;
        presentQueued=true;
        // Return ownership before activation. Changing foreground can synchronously
        // notify the sending thread, which is still waiting for this receipt.
        TabCtrl_SetCurSel(tabs,index);
        PostMessageW(bar,navigateMessage,0,0);
        schedule(25);
        return true;
    }
    static LRESULT CALLBACK coverProc(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data) {
        auto* self=reinterpret_cast<Band*>(data);
        if (msg==WM_ERASEBKGND) { self->theme.erase(reinterpret_cast<HDC>(wp),w); return 1; }
        if (msg==WM_PAINT) {
            PAINTSTRUCT ps{}; HDC dc=BeginPaint(w,&ps); self->theme.erase(dc,w); EndPaint(w,&ps); return 0;
        }
        if (msg==WM_NCDESTROY && self->loadingCover==w) self->loadingCover=nullptr;
        return DefSubclassProc(w,msg,wp,lp);
    }
    void completeSwitch() {
        int previous = active;
        active = destination;
        destination = -1;
        if (closingTo) {
            int last=active;
            if (rememberRemoval) remember(last);
            active=-1;
            erase(last);
            nativeLocation=std::move(closingTo);
        } else if (closeAfterSwitch) {
            if (rememberRemoval) remember(previous);
            erase(previous);
        }
        if (active>=0) nativeLocation.reset();
        if (restoring && !closed.empty()) closed.pop_back();
        restoring=rememberRemoval=false;
        if (queuedTab<0 && nextImported<0) clearLoading();
        closeAfterSwitch = false;
        pendingNew = -1;
        endImport(true);
        controls();
    }
    void sync(int* tracked=nullptr) {
        if (!tabs) return;
        Location actual = current();
        if (destination >= 0) {
            if (actual && ILIsEqual(actual.get(), closingTo ? closingTo.get() : folders[destination].folder.get())) {
                completeSwitch();
                return;
            }
            if (GetTickCount64() < deadline) return;
            destination = -1;
            closingTo.reset();
            closeAfterSwitch = false;
            restoring=rememberRemoval=false;
            clearLoading();
            endImport(false);
            if (pendingNew >= 0) { erase(pendingNew); pendingNew = -1; }
            controls();
            MessageBeep(MB_ICONWARNING);
        }
        if (!actual) return;
        if (nativeLocation && ILIsEqual(actual.get(),nativeLocation.get())) return;
        if (folders.empty()) append(std::move(actual));
        else if (active<0 || !ILIsEqual(actual.get(), folders[active].folder.get())) {
            nextImported = -1;
            int match=matchingTab(actual.get());
            if (match>=0 && match!=active) {
                // Explorer already displays the destination. Adopt its view without
                // navigating again or replacing the folder stored in the source tab.
                active=match;
                nativeLocation.reset();
                if (!ILIsEqual(actual.get(),folders[match].folder.get())) {
                    folders[match].folder=std::move(actual);
                    label(match,false);
                }
                if (match+1==static_cast<int>(folders.size())) controls();
                else moveTabToEnd(match,tracked);
                return;
            }
            if (active<0) append(std::move(actual));
            else {
                folders[active].folder = std::move(actual);
                label(active, false);
            }
        }
    }
    void select(int index, bool removePrevious = false, bool saveClosed = false, Location closeTarget = {}) {
        nextImported = -1;
        if (destination >= 0 || index < 0 || index >= static_cast<int>(folders.size())) return;
        if (pendingNew<0 && !removePrevious && !closeTarget) sync(&index);
        if ((!closeTarget && index == active) || (active < 0 && !nativeLocation)) return;
        closingTo=std::move(closeTarget);
        destination = index;
        closeAfterSwitch = removePrevious;
        rememberRemoval = saveClosed;
        deadline = GetTickCount64() + 15000;
        auto from=active>=0 ? folders[active].folder.get() : nativeLocation.get();
        auto to=closingTo ? closingTo.get() : folders[index].folder.get();
        Location target(ILCloneFull(to));
        if (ILIsEqual(from,to)) {
            if (!incoming) { completeSwitch(); return; }
            sync();
            if (destination < 0) return;
        }
        schedule(25);
        // Keep the source tab until Explorer confirms the target, including on a slow share.
        controls();
        HRESULT hr = target ? browser->BrowseObject(target.get(), SBSP_ABSOLUTE | SBSP_SAMEBROWSER)
                            : E_OUTOFMEMORY;
        if (FAILED(hr) && destination >= 0) {
            destination = -1;
            closingTo.reset();
            closeAfterSwitch = false;
            restoring=rememberRemoval=false;
            clearLoading();
            endImport(false);
            if (pendingNew >= 0) { erase(pendingNew); pendingNew = -1; }
            controls();
            MessageBeep(MB_ICONWARNING);
        } else sync();
    }
    Location defaultLocation() {
        PIDLIST_ABSOLUTE id = nullptr;
        HRESULT hr = settings::quickAccess()
            ? SHParseDisplayName(L"shell:::{679f85cb-0220-4080-b29b-5540cc05aab6}",nullptr,&id,0,nullptr)
            : SHGetKnownFolderIDList(FOLDERID_ComputerFolder,0,nullptr,&id);
        if (FAILED(hr)) MessageBeep(MB_ICONWARNING);
        return Location(id);
    }
    void newTab() {
        if (destination >= 0 || separating >= 0 || incoming || !hasRoom()) return;
        settle();
        sync();
        if (active < 0 && !nativeLocation) return;
        Location target=defaultLocation();
        if (!target) return;
        pendingNew = append(std::move(target),false);
        if (pendingNew >= 0) select(pendingNew);
    }
    void closeTab(int index,bool saveClosed=true) {
        if (destination >= 0 || separating >= 0 || index < 0 || index >= static_cast<int>(folders.size())) return;
        sync(&index);
        if (folders.size() == 1) {
            if (queuedTab>=0 || incoming || storing) return;
            settle();
            Location target=defaultLocation();
            if (target) select(index,false,saveClosed,std::move(target));
            return;
        }
        if (index == active) select(index > 0 ? index - 1 : 1, true,saveClosed);
        else { if (saveClosed) remember(index); erase(index); controls(); }
    }
    void checkSeparation() {
        if (separating < 0 || !independent) return;
        HWND root = independent;
        bool owned = GetPropW(root, merge::keepFrame) == bar;
        Location actual = owned ? merge::folderOf(root) : Location{};
        if (root != independent || separating < 0) return; // COM may pump messages.
        bool landed = actual && IsWindowVisible(root) && ILIsEqual(actual.get(),separationFolder.get());
        if (owned && !landed && GetTickCount64() < separationDeadline) return;
        int index = separating;
        separating = -1;
        independent = nullptr;
        bool original = index < static_cast<int>(folders.size()) &&
            ILIsEqual(folders[index].folder.get(),separationFolder.get());
        separationFolder.reset();
        if (landed && original && !copyToWindow) closeTab(index,false);
        else if (!landed) MessageBeep(MB_ICONWARNING);
        controls();
    }
    void separate(int index, POINT drop,bool copy=false) {
        if (destination >= 0 || separating >= 0 || index < 0 || index >= static_cast<int>(folders.size())) return;
        settle();
        nextImported = -1;
        HWND source = merge::frame(bar);
        copyToWindow=copy;
        if (!copy && folders.size() == 1) {
            if (IsZoomed(source)) ShowWindow(source,SW_RESTORE);
            SetWindowPos(source,nullptr,drop.x-scale(65),drop.y-scale(50),0,0,SWP_NOSIZE|SWP_NOZORDER);
            return;
        }
        // Reserve this exact, initially hidden Explorer instance before it can offer a tab.
        separationFolder.reset(ILCloneFull(folders[index].folder.get()));
        if (!separationFolder) return;
        separating = index;
        separationPoint = drop;
        if (!PostMessageW(bar,separateMessage,0,0)) {
            separating = -1;
            separationFolder.reset();
            return;
        }
        controls();
    }
    void openSeparateWindow() {
        if (separating < 0 || independent || !separationFolder) return;
        HWND source = merge::frame(bar);
        POINT drop = separationPoint;
        Com<IWebBrowser2> web;
        HRESULT hr = CoCreateInstance(CLSID_ShellBrowserWindow,nullptr,CLSCTX_LOCAL_SERVER,IID_PPV_ARGS(&web.p));
        SHANDLE_PTR handle = 0;
        if (SUCCEEDED(hr)) hr = web->get_HWND(&handle);
        HWND root = reinterpret_cast<HWND>(handle);
        if (SUCCEEDED(hr) && (!bar || !separationFolder || !root || root == source ||
            !SetPropW(root,merge::keepFrame,bar))) hr = E_FAIL;
        VARIANT target{};
        if (SUCCEEDED(hr)) {
            UINT bytes = ILGetSize(separationFolder.get());
            target.parray = SafeArrayCreateVector(VT_UI1,0,bytes);
            if (!target.parray) hr = E_OUTOFMEMORY;
            else {
                target.vt = VT_ARRAY | VT_UI1;
                memcpy(target.parray->pvData,separationFolder.get(),bytes);
                VARIANT empty{};
                hr = web->Navigate2(&target,&empty,&empty,&empty,&empty);
            }
        }
        VariantClear(&target);
        if (SUCCEEDED(hr)) {
            SetWindowPos(root,nullptr,drop.x-scale(65),drop.y-scale(50),0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
            hr = web->put_Visible(VARIANT_TRUE);
        }
        if (FAILED(hr) || !bar || separating < 0) {
            if (web.p && !IsWindowVisible(root)) web->Quit();
            separating = -1;
            separationFolder.reset();
            if (bar) controls();
            MessageBeep(MB_ICONWARNING);
            return;
        }
        independent = root;
        separationDeadline = GetTickCount64()+15000;
        SetForegroundWindow(root);
        schedule(25);
    }
    enum MenuCommand { Settings=200, NewWindow, Restore };
    void restoreClosedTab() {
        if (closed.empty() || !hasRoom() || destination>=0 || separating>=0 || incoming || queuedTab>=0 || storing) return;
        sync();
        pendingNew=append(Location(ILCloneFull(closed.back().get())),false);
        if (pendingNew>=0) { restoring=true; select(pendingNew); }
    }
    void tabMenu(POINT point) {
        if (!tabs || !IsWindowVisible(bar)) return;
        sync();
        bool keyboard=point.x==-1 && point.y==-1;
        if (keyboard) {
            RECT r{}; TabCtrl_GetItemRect(tabs,std::max(active,0),&r);
            point={r.left,r.bottom}; ClientToScreen(tabs,&point);
        }
        POINT client=point; ScreenToClient(tabs,&client);
        TCHITTESTINFO hit{client,0};
        int index=keyboard ? active : TabCtrl_HitTest(tabs,&hit);
        if (index<0) index=active;
        bool idle=destination<0 && separating<0 && !incoming && queuedTab<0 && !storing;
        Location clicked(index>=0 ? ILCloneFull(folders[index].folder.get()) : nullptr);
        HMENU menu=CreatePopupMenu();
        if (!menu) return;
        const wchar_t* names[]={L"设置",L"在新窗口打开此标签页",L"恢复上个关闭的标签页"};
        bool enabled[]={true,idle && clicked,idle && hasRoom() && !closed.empty()};
        MSAAMENUINFO accessible[ARRAYSIZE(names)]{};
        HBRUSH bg=theme.darkMenu()?CreateSolidBrush(RGB(43,43,43)):nullptr;
        if (bg) {
            MENUINFO info{}; info.cbSize=sizeof(info); info.fMask=MIM_BACKGROUND; info.hbrBack=bg;
            SetMenuInfo(menu,&info);
        }
        for (UINT i=0;i<ARRAYSIZE(names);++i) {
            if (i==1) AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
            MENUITEMINFOW entry{}; entry.cbSize=sizeof(entry); entry.wID=Settings+i;
            entry.fMask=MIIM_ID|MIIM_FTYPE|MIIM_STRING|MIIM_STATE;
            entry.dwTypeData=const_cast<PWSTR>(names[i]);
            entry.fState=enabled[i]?MFS_ENABLED:MFS_DISABLED;
            if (bg) {
                accessible[i]={MSAA_MENU_SIG,static_cast<DWORD>(wcslen(names[i])),entry.dwTypeData};
                entry.fType=MFT_OWNERDRAW;
                entry.fMask|=MIIM_DATA; entry.dwItemData=reinterpret_cast<ULONG_PTR>(&accessible[i]);
            }
            InsertMenuItemW(menu,GetMenuItemCount(menu),TRUE,&entry);
        }
        UINT choice=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,bar,nullptr);
        DestroyMenu(menu);
        if (bg) DeleteObject(bg);
        if (!bar) return;
        if (choice==Settings) {
            HWND manager=FindWindowExW(HWND_MESSAGE,nullptr,L"EZFolder.EntryManager",nullptr);
            if (manager) PostMessageW(manager,merge::openSettings(),reinterpret_cast<WPARAM>(merge::frame(bar)),0);
            return;
        }
        if (destination>=0 || separating>=0 || incoming || queuedTab>=0) return;
        if (choice==NewWindow && index>=0 && index<static_cast<int>(folders.size()) && clicked && ILIsEqual(clicked.get(),folders[index].folder.get())) {
            RECT r{}; GetWindowRect(merge::frame(bar),&r);
            separate(index,{r.left+scale(96),r.top+scale(80)},true);
        } else if (choice==Restore) {
            restoreClosedTab();
        }
    }
    static LRESULT CALLBACK hostMenuProc(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data) {
        auto* self=reinterpret_cast<Band*>(data);
        if (msg==WM_NCDESTROY) {
            if (self->statusBar==w) self->statusBar=nullptr;
            if (self->statusHost==w) self->statusHost=nullptr;
            if (self->bandParent==w) self->bandParent=nullptr;
        }
        if (self->bar && IsWindowVisible(self->bar) && (msg==WM_CONTEXTMENU || msg==WM_RBUTTONUP || msg==WM_RBUTTONDOWN)) {
            POINT p{short(LOWORD(lp)),short(HIWORD(lp))};
            if (msg!=WM_CONTEXTMENU) ClientToScreen(w,&p);
            bool inside=w==self->statusBar;
            if (w==self->statusHost && WindowFromPoint(p)==w) {
                // DirectUI hosts both the folder layout and its windowless status bar.
                Com<IShellView> view;
                HWND content=nullptr;
                RECT items{},host{};
                inside=self->browser.p && SUCCEEDED(self->browser->QueryActiveShellView(&view.p)) && view.p &&
                    SUCCEEDED(view->GetWindow(&content)) && GetWindowRect(content,&items) &&
                    GetWindowRect(w,&host) && PtInRect(&host,p) && p.y>=items.bottom &&
                    host.bottom-items.bottom<=self->scale(40);
            }
            if (w==self->bandParent) {
                int bands=static_cast<int>(SendMessageW(w,RB_GETBANDCOUNT,0,0));
                POINT local=p; ScreenToClient(w,&local);
                for (int i=0;i<bands && !inside;++i) {
                    REBARBANDINFOW info{}; info.cbSize=sizeof(info); info.fMask=RBBIM_CHILD;
                    RECT r{};
                    if (SendMessageW(w,RB_GETBANDINFOW,i,reinterpret_cast<LPARAM>(&info)) &&
                        (info.hwndChild==self->bar || IsChild(info.hwndChild,self->bar)) &&
                        SendMessageW(w,RB_GETRECT,i,reinterpret_cast<LPARAM>(&r))) inside=PtInRect(&r,local);
                }
            }
            if (inside) {
                if (msg!=WM_RBUTTONDOWN) { self->AddRef(); self->tabMenu(p); self->Release(); }
                return 0;
            }
        }
        return DefSubclassProc(w,msg,wp,lp);
    }
    void attachMenus() {
        HWND status=nullptr;
        if (!statusHost) {
            HWND frame=merge::frame(bar);
            HWND tab=frame ? FindWindowExW(frame,nullptr,L"ShellTabWindowClass",nullptr) : nullptr;
            HWND layout=tab ? FindWindowExW(tab,nullptr,L"DUIViewWndClassName",nullptr) : nullptr;
            HWND host=layout ? FindWindowExW(layout,nullptr,L"DirectUIHWND",nullptr) : nullptr;
            if (host && GetWindowThreadProcessId(host,nullptr)==GetCurrentThreadId() &&
                SetWindowSubclass(host,hostMenuProc,reinterpret_cast<UINT_PTR>(this),reinterpret_cast<DWORD_PTR>(this))) statusHost=host;
        }
        if (!statusBar && SUCCEEDED(browser->GetControlWindow(FCW_STATUS,&status)) && status &&
            GetWindowThreadProcessId(status,nullptr)==GetCurrentThreadId() &&
            SetWindowSubclass(status,hostMenuProc,reinterpret_cast<UINT_PTR>(this),reinterpret_cast<DWORD_PTR>(this))) statusBar=status;
        for (HWND w=GetParent(bar); !bandParent && w; w=GetParent(w)) {
            wchar_t name[32]{};
            if (GetClassNameW(w,name,32) && !_wcsicmp(name,REBARCLASSNAMEW) &&
                SetWindowSubclass(w,hostMenuProc,reinterpret_cast<UINT_PTR>(this),reinterpret_cast<DWORD_PTR>(this))) bandParent=w;
        }
    }
    HWND dropBand(POINT p) const {
        HWND root=GetAncestor(WindowFromPoint(p),GA_ROOT);
        HWND other=static_cast<HWND>(GetPropW(root,merge::bandProp));
        return other!=bar && merge::live(other) ? other : nullptr;
    }
    bool acceptsDrop(HWND other) const {
        return merge::hasView(other) && GetPropW(other,merge::readyProp) &&
            !GetPropW(other,merge::fullProp) && IsWindowEnabled(merge::frame(other));
    }
    void dragFeedback(POINT p) {
        HWND other=dropBand(p);
        RECT r{}; GetWindowRect(bar,&r);
        const wchar_t* hint=other ? (acceptsDrop(other)?L"松开以合并到此窗口":L"此窗口无法接收标签") :
            PtInRect(&r,p) ? L"拖出标签栏以分离" : L"松开以分离为新窗口";
        if (!dragging || dragText!=hint) {
            dragText=hint;
            tipTool.lpszText=dragText.data();
            SendMessageW(tooltip,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&tipTool));
        }
        SendMessageW(tooltip,TTM_TRACKPOSITION,0,MAKELPARAM(p.x+scale(16),p.y+scale(20)));
        if (!dragging) SendMessageW(tooltip,TTM_TRACKACTIVATE,TRUE,reinterpret_cast<LPARAM>(&tipTool));
        dragging=true;
        SetCursor(LoadCursorW(nullptr,other && !acceptsDrop(other)?IDC_NO:IDC_SIZEALL));
    }
    void endDrag() {
        if (dragging && tooltip) SendMessageW(tooltip,TTM_TRACKACTIVATE,FALSE,reinterpret_cast<LPARAM>(&tipTool));
        dragging=false;
    }
    void moveTo(int index,HWND other) {
        if (!acceptsDrop(other) || destination>=0 || separating>=0 || index<0 || index>=static_cast<int>(folders.size())) return;
        sync(&index);
        PWSTR path=nullptr;
        Location original(ILCloneFull(folders[index].folder.get()));
        if (!original || FAILED(SHGetNameFromIDList(original.get(),SIGDN_DESKTOPABSOLUTEPARSING,&path))) return;
        separating=index;
        bool ok=SetPropW(bar,merge::offerProp,other) && merge::sendPath(other,bar,merge::movePath(),path);
        CoTaskMemFree(path);
        RemovePropW(bar,merge::offerProp);
        index=separating;
        separating=-1;
        if (!ok || index<0 || index>=static_cast<int>(folders.size()) || !ILIsEqual(original.get(),folders[index].folder.get())) return;
        if (folders.size()==1) PostMessageW(merge::frame(bar),WM_CLOSE,0,0);
        else closeTab(index,false);
    }
    bool tabMouse(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_CAPTURECHANGED || msg == WM_CANCELMODE || (msg == WM_KEYDOWN && wp == VK_ESCAPE)) {
            if (pressedTab < 0) return false;
            pressedTab = -1;
            endDrag();
            if (GetCapture() == w) ReleaseCapture();
            return msg == WM_KEYDOWN;
        }
        if (msg != WM_LBUTTONDOWN && msg != WM_LBUTTONUP && msg!=WM_MOUSEMOVE) return false;
        TCHITTESTINFO hit{};
        hit.pt = {static_cast<short>(LOWORD(lp)),static_cast<short>(HIWORD(lp))};
        int index = TabCtrl_HitTest(w,&hit);
        if (msg==WM_MOUSEMOVE) {
            if (pressedTab<0 || pressedClose) return false;
            if (dragging || abs(hit.pt.x-pressPoint.x)>=GetSystemMetrics(SM_CXDRAG) || abs(hit.pt.y-pressPoint.y)>=GetSystemMetrics(SM_CYDRAG)) {
                POINT screen=hit.pt; ClientToScreen(w,&screen); dragFeedback(screen);
            }
            return true;
        }
        RECT item{};
        bool onClose = false;
        if (index >= 0 && TabCtrl_GetItemRect(w,index,&item)) {
            RECT box = BandTheme::closeRect(item,scale(96),true);
            onClose = PtInRect(&box,hit.pt);
        }
        if (msg == WM_LBUTTONDOWN) {
            if (index < 0) return false;
            SetFocus(w);
            pressedTab = index;
            pressedClose = onClose;
            pressPoint = hit.pt;
            SetCapture(w);
            return true;
        }
        int pressed = pressedTab;
        if (pressed < 0) return false;
        pressedTab = -1;
        endDrag();
        ReleaseCapture();
        POINT screen = hit.pt;
        ClientToScreen(w,&screen);
        RECT strip{};
        GetWindowRect(bar,&strip);
        if (pressedClose) {
            if (onClose && index == pressed) closeTab(pressed);
        } else if (!PtInRect(&strip,screen) &&
            (abs(hit.pt.x-pressPoint.x) >= GetSystemMetrics(SM_CXDRAG) ||
             abs(hit.pt.y-pressPoint.y) >= GetSystemMetrics(SM_CYDRAG))) {
            if (HWND other=dropBand(screen)) moveTo(pressed,other);
            else separate(pressed,screen);
        } else if (index == pressed && !onClose) select(index);
        return true;
    }
    void hideTitle() {
        if (hoverIndex < 0) return;
        hoverIndex = -1;
        if (tooltip) SendMessageW(tooltip,TTM_TRACKACTIVATE,FALSE,reinterpret_cast<LPARAM>(&tipTool));
    }
    void trackTitle(UINT msg, LPARAM lp) {
        if (!tooltip) return;
        if (msg == WM_MOUSELEAVE || msg == WM_LBUTTONDOWN || msg == WM_MBUTTONDOWN || pressedTab >= 0) {
            hideTitle();
            return;
        }
        TCHITTESTINFO hit{};
        hit.pt = {static_cast<short>(LOWORD(lp)),static_cast<short>(HIWORD(lp))};
        int i = TabCtrl_HitTest(tabs,&hit);
        if (i != hoverIndex) {
            hideTitle();
            if (i < 0) return;
            hoverIndex = i;
            TRACKMOUSEEVENT track{sizeof(track),TME_HOVER|TME_LEAVE,tabs,HOVER_DEFAULT};
            TrackMouseEvent(&track);
        }
        if (msg != WM_MOUSEHOVER || i < 0 || i >= static_cast<int>(folders.size())) return;
        RECT r{};
        if (!TabCtrl_GetItemRect(tabs,i,&r)) return;
        PWSTR name = nullptr;
        if (FAILED(SHGetNameFromIDList(folders[i].folder.get(),SIGDN_NORMALDISPLAY,&name)) || !name) return;
        tipTitle.reset(name);
        RECT label = BandTheme::labelRect(r,scale(96),true);
        HDC dc = GetDC(tabs);
        if (!dc) return;
        HFONT face = reinterpret_cast<HFONT>(SendMessageW(tabs,WM_GETFONT,0,0));
        HGDIOBJ old = face ? SelectObject(dc,face) : nullptr;
        SIZE size{};
        GetTextExtentPoint32W(dc,name,lstrlenW(name),&size);
        if (old) SelectObject(dc,old);
        ReleaseDC(tabs,dc);
        if (size.cx <= label.right-label.left) return;
        tipTool.lpszText = name;
        SendMessageW(tooltip,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&tipTool));
        RECT strip{};
        GetWindowRect(bar,&strip);
        POINT anchor{r.left,0};
        ClientToScreen(tabs,&anchor);
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        POINT pos{anchor.x,strip.bottom+scale(2)};
        if (GetMonitorInfoW(MonitorFromPoint(pos,MONITOR_DEFAULTTONEAREST),&monitor)) {
            pos.x = std::max(monitor.rcWork.left,std::min(pos.x,monitor.rcWork.right-size.cx-scale(16)));
        }
        SendMessageW(tooltip,TTM_TRACKPOSITION,0,MAKELPARAM(pos.x,pos.y));
        SendMessageW(tooltip,TTM_TRACKACTIVATE,TRUE,reinterpret_cast<LPARAM>(&tipTool));
    }
    int capacity(int barWidth) const {
        return std::max(1,(barWidth-scale(32)-scale(6))/scale(24));
    }
    bool hasRoom() const {
        RECT r{};
        return bar && GetClientRect(bar,&r) && static_cast<int>(folders.size()) < capacity(r.right);
    }
    void layout() {
        RECT r{};
        GetClientRect(bar, &r);
        if (!tabs || !add || r.right <= 0) return;
        int button = scale(32);
        int available = std::max(0,static_cast<int>(r.right)-button);
        int count = std::max(1,static_cast<int>(folders.size()));
        if (merge::number(bar,merge::countProp)!=folders.size()) {
            SetPropW(bar,merge::countSinceProp,reinterpret_cast<HANDLE>(GetTickCount64()+1));
            SetPropW(bar,merge::countProp,reinterpret_cast<HANDLE>(folders.size()));
        }
        hideTitle();
        bool room = static_cast<int>(folders.size()) < capacity(r.right);
        if (room) RemovePropW(bar,merge::fullProp);
        else SetPropW(bar,merge::fullProp,reinterpret_cast<HANDLE>(1));
        bool plain=nativeLocation && folders.empty();
        if (plain!=static_cast<bool>(GetPropW(bar,merge::emptyProp))) {
            if (plain) SetPropW(bar,merge::emptyProp,reinterpret_cast<HANDLE>(1));
            else RemovePropW(bar,merge::emptyProp);
            if (plain && GetFocus()==tabs) SetFocus(add);
            ShowWindow(tabs,plain ? SW_HIDE : SW_SHOWNA);
        }
        EnableWindow(add,room && (active >= 0 || nativeLocation) && destination < 0 && separating < 0);
        int item = std::clamp((available-scale(6))/count,1,scale(132));
        int height = std::max(1,static_cast<int>(r.bottom)-scale(3));
        bool resize = stripSize.cx != available || stripSize.cy != r.bottom;
        // Widen the control before growing its items, shrink the items before narrowing it.
        if (resize && available >= stripSize.cx) MoveWindow(tabs,0,0,available,r.bottom,FALSE);
        if (itemSize.cx != item || itemSize.cy != height) {
            TabCtrl_SetItemSize(tabs,item,height);
            itemSize = {item,height};
        }
        if (resize) {
            if (available < stripSize.cx) MoveWindow(tabs,0,0,available,r.bottom,FALSE);
            MoveWindow(add,available,0,button,r.bottom,FALSE);
            stripSize = {available,r.bottom};
            RedrawWindow(bar,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
        }
    }
    int shortcutFor(WPARAM vk) const {
        if ((GetKeyState(VK_LWIN)|GetKeyState(VK_RWIN)) & 0x8000) return -1;
        DWORD chord = static_cast<DWORD>(vk);
        if (GetKeyState(VK_CONTROL) & 0x8000) chord |= settings::control;
        if (GetKeyState(VK_SHIFT) & 0x8000) chord |= settings::shift;
        if (GetKeyState(VK_MENU) & 0x8000) chord |= settings::alt;
        if (chord == vk && (vk < VK_F1 || vk > VK_F24)) return -1;
        auto prefs = settings::load();
        for (int i=0; i<settings::ActionCount; ++i) if (prefs.shortcuts[i]==chord) return i;
        return -1;
    }
    bool runShortcut(int action) {
        if (action == settings::New) newTab();
        else if (action == settings::Close) closeTab(active);
        else if (action == settings::Restore) restoreClosedTab();
        else if (!folders.empty() && (action == settings::Next || action == settings::Previous)) {
            int count = static_cast<int>(folders.size());
            select((active + (action == settings::Previous ? count-1 : 1)) % count);
        } else return false;
        return true;
    }
    // Explorer translates Ctrl+W before dispatching to the folder view. Resolve
    // the owning band from the actual message window, even on a shared UI thread.
    static LRESULT CALLBACK focusedMessage(int code,WPARAM mode,LPARAM data) {
        if (code==HC_ACTION && mode==PM_REMOVE) {
            auto* msg=reinterpret_cast<MSG*>(data);
            if ((msg->message==WM_KEYDOWN || msg->message==WM_SYSKEYDOWN) && msg->hwnd && msg->hwnd==GetFocus()) {
                HWND root = GetAncestor(msg->hwnd,GA_ROOT);
                HWND band = static_cast<HWND>(GetPropW(root,merge::bandProp));
                if (root==GetForegroundWindow() && band && IsWindowVisible(band) && IsWindowEnabled(root)) {
                    auto* self = threadBands;
                    while (self && self->bar != band) self = self->nextOnThread;
                    if (!self) return CallNextHookEx(nullptr,code,mode,data);
                    int action = self->shortcutFor(msg->wParam);
                    bool windowShortcut=action==settings::Close || action==settings::Restore;
                    if (windowShortcut || (action>=0 && (msg->hwnd==self->tabs || msg->hwnd==self->add))) {
                        bool repeat = windowShortcut && (msg->lParam & (1L<<30));
                        msg->hwnd=band;
                        msg->message=repeat ? WM_NULL : shortcutMessage;
                        msg->wParam=action;
                    }
                }
            }
        }
        return CallNextHookEx(nullptr,code,mode,data);
    }
    static LRESULT CALLBACK childProc(HWND w, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
        auto* self = reinterpret_cast<Band*>(data);
        try {
            if (msg==WM_RBUTTONDOWN) return 0;
            if (msg==WM_CONTEXTMENU || msg==WM_RBUTTONUP) {
                POINT p{short(LOWORD(lp)),short(HIWORD(lp))};
                if (msg==WM_RBUTTONUP) ClientToScreen(w,&p);
                Com<IDeskBand> guard; self->AddRef(); guard.p=self;
                self->tabMenu(p); return 0;
            }
            if (msg == WM_ERASEBKGND) return 1;
            if (msg == WM_PAINT || msg == WM_PRINTCLIENT) {
                PAINTSTRUCT ps{};
                HDC dc = msg == WM_PAINT ? BeginPaint(w, &ps) : reinterpret_cast<HDC>(wp);
                if (dc) self->theme.paint(w, self->tabs, dc, self->scale(96), msg==WM_PAINT ? &ps.rcPaint : nullptr);
                if (msg == WM_PAINT) EndPaint(w, &ps);
                return 0;
            }
            if (msg == WM_MOUSEMOVE || msg == WM_MOUSELEAVE) self->theme.pointer(w, self->tabs, msg, lp);
            if (w == self->tabs && (msg == WM_MOUSEMOVE || msg == WM_MOUSEHOVER || msg == WM_MOUSELEAVE ||
                msg == WM_LBUTTONDOWN || msg == WM_MBUTTONDOWN)) self->trackTitle(msg,lp);
            if (msg == WM_ENABLE || msg == WM_SETFOCUS || msg == WM_KILLFOCUS ||
                msg == WM_UPDATEUISTATE || msg == BM_SETSTATE || msg == TCM_SETCURSEL ||
                msg == WM_LBUTTONUP || msg == WM_LBUTTONDOWN) InvalidateRect(w, nullptr, FALSE);
            if (w == self->tabs && self->tabMouse(w,msg,wp,lp)) return 0;
            if (msg == WM_MBUTTONUP && w == self->tabs) {
                TCHITTESTINFO hit{};
                hit.pt = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
                self->closeTab(TabCtrl_HitTest(w, &hit));
                return 0;
            }
            if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && self->runShortcut(self->shortcutFor(wp))) return 0;
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
            if (msg==WM_MEASUREITEM && lp) {
                auto* item=reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
                if (item->CtlType==ODT_MENU) {
                    item->itemWidth=self->scale(188);
                    item->itemHeight=self->scale(26);
                    return TRUE;
                }
            }
            if (msg==WM_DRAWITEM && lp) {
                auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);
                // Buttons can redraw synchronously on state changes, outside WM_PAINT.
                if (item->CtlType==ODT_BUTTON && item->CtlID==NewTab) {
                    self->theme.paint(item->hwndItem,self->tabs,item->hDC,self->scale(96));
                    return TRUE;
                }
                if (item->CtlType==ODT_MENU) { self->theme.menuItem(*item,self->scale(96)); return TRUE; }
            }
            if (msg==WM_RBUTTONDOWN) return 0;
            if (msg==WM_CONTEXTMENU || msg==WM_RBUTTONUP) {
                POINT p{short(LOWORD(lp)),short(HIWORD(lp))};
                if (msg==WM_RBUTTONUP) ClientToScreen(w,&p);
                Com<IDeskBand> guard; self->AddRef(); guard.p=self;
                self->tabMenu(p); return 0;
            }
            if (msg==navigateMessage) {
                Com<IDeskBand> guard; self->AddRef(); guard.p=self;
                self->navigateQueued(); return 0;
            }
            if (msg==shortcutMessage) {
                if (IsWindowVisible(w) && merge::frame(w)==GetForegroundWindow()) self->runShortcut(static_cast<int>(wp));
                return 0;
            }
            if (msg == WM_COPYDATA && lp) {
                auto& packet=*reinterpret_cast<COPYDATASTRUCT*>(lp);
                if (packet.dwData==merge::openPath() || packet.dwData==merge::movePath() || packet.dwData==merge::containsPath()) {
                    Com<IDeskBand> guard; self->AddRef(); guard.p=self;
                    return self->acceptPath(reinterpret_cast<HWND>(wp),packet);
                }
                return self->matchesOffer(reinterpret_cast<HWND>(wp),packet);
            }
            if (msg == merge::storeTab() || msg == merge::storedTab()) {
                Com<IDeskBand> guard;
                self->AddRef();
                guard.p = self;
                HWND peer = reinterpret_cast<HWND>(wp);
                if (msg == merge::storedTab()) self->received(peer,lp,true);
                else if (!self->storeOffer(peer,lp) && self->validOffer(peer,lp))
                    PostMessageW(peer,merge::retry(),reinterpret_cast<WPARAM>(w),lp);
                return 0;
            }
            if (msg == merge::request()) return self->reserve(reinterpret_cast<HWND>(wp), lp);
            if (msg == merge::retry()) {
                if (self->validSourceAck(reinterpret_cast<HWND>(wp), lp)) {
                    self->offered = false;
                    self->schedule(GetTickCount64() - self->opened < 1000 ? 25 : 100);
                }
                return 0;
            }
            if (msg == importMessage || msg == separateMessage || msg == merge::received() || msg == WM_TIMER || msg == refreshMessage) {
                Com<IDeskBand> guard;
                self->AddRef();
                guard.p = self;
                if (msg == importMessage) self->importWindow();
                else if (msg == separateMessage) self->openSeparateWindow();
                else if (msg == WM_TIMER || msg == refreshMessage) self->tick();
                else self->received(reinterpret_cast<HWND>(wp), lp);
                return 0;
            }
            switch (msg) {
            case WM_THEMECHANGED: case WM_SYSCOLORCHANGE: case WM_SETTINGCHANGE:
                self->theme.refresh(w, self->tooltip, true);
                break;
            case WM_ERASEBKGND:
                self->theme.erase(reinterpret_cast<HDC>(wp),w); return 1;
            case WM_PAINT: {
                PAINTSTRUCT ps{};
                HDC dc = BeginPaint(w,&ps);
                if (dc) self->theme.erase(dc,w);
                EndPaint(w,&ps);
                return 0;
            }
            case WM_PRINTCLIENT:
                self->theme.erase(reinterpret_cast<HDC>(wp),w); return 0;
            case WM_SIZE: self->layout(); break;
            case WM_SETFOCUS: SetFocus(self->tabs); break;
            case WM_COMMAND:
                if (HIWORD(wp) == BN_CLICKED) {
                    if (LOWORD(wp) == NewTab) self->newTab();

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
                self->bar = self->tabs = self->add = self->tooltip = nullptr;
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
        foregroundAtOpen = GetForegroundWindow();
        LASTINPUTINFO input{sizeof(input),0};
        inputAtOpen = GetLastInputInfo(&input) ? input.dwTime : 0;
        if (foregroundAtOpen) SetPropW(bar,merge::foregroundProp,foregroundAtOpen);
        SetPropW(bar,merge::inputProp,reinterpret_cast<HANDLE>(ULONG_PTR(inputAtOpen)+1));
        HWND root = merge::frame(bar);
        settled = !root || GetPropW(root, merge::keepFrame);
        offered = false;
        closingMergedFrame = false;
        SetPropW(bar, merge::birthProp, reinterpret_cast<HANDLE>(opened + 1));
        SetPropW(bar,settled ? merge::readyProp : merge::startingProp,reinterpret_cast<HANDLE>(1));
        if (root) SetPropW(root, merge::bandProp, bar);
        if (root) {
            if (!threadKeys) threadKeys=SetWindowsHookExW(WH_GETMESSAGE,focusedMessage,nullptr,GetCurrentThreadId());
            if (threadKeys) { nextOnThread=threadBands; threadBands=this; watchingKeys=true; }
        }
        if (!settled) {
            if (HWND candidate = merge::target(bar,true)) presentation.begin(root,candidate);
        }
        auto child = [&](LPCWSTR type, LPCWSTR title, int id, DWORD style) {
            return CreateWindowExW(0, type, title, WS_VISIBLE | WS_CHILD | WS_TABSTOP | style,
                    0, 0, 0, 0, bar, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), module, nullptr);
        };
        // The multiline variant has no pager. layout() fits the items into a single visible row.
        tabs = child(WC_TABCONTROLW, L"文件夹标签", TabControl,
                     TCS_MULTILINE | TCS_RAGGEDRIGHT | TCS_FIXEDWIDTH | TCS_FOCUSONBUTTONDOWN | WS_CLIPCHILDREN);
        add = child(WC_BUTTONW, L"新建标签页", NewTab, BS_OWNERDRAW);
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
            font = CreateFontIndirectW(&metrics.lfMessageFont);
        for (HWND w : {tabs, add}) {
            if (!w || !SetWindowSubclass(w, childProc, 1, reinterpret_cast<DWORD_PTR>(this))) return E_FAIL;
            SendMessageW(w, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : GetStockObject(DEFAULT_GUI_FONT)), FALSE);
        }
        SendMessageW(tabs,TCM_SETMINTABWIDTH,0,1);
        tooltip = CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,TOOLTIPS_CLASSW,nullptr,
            WS_POPUP|TTS_NOPREFIX|TTS_ALWAYSTIP,0,0,0,0,bar,nullptr,module,nullptr);
        if (!tooltip) return E_FAIL;
        tipTool.cbSize = TTTOOLINFOW_V2_SIZE;
        tipTool.hwnd = tabs;
        tipTool.uId = 1;
        tipTool.uFlags = TTF_TRACK|TTF_ABSOLUTE;
        tipTool.lpszText = const_cast<wchar_t*>(L"");
        if (!SendMessageW(tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tipTool))) return E_FAIL;
        theme.refresh(bar, tooltip, true);
        attachMenus();
        controls();
        sync();
        if (!settled && active >= 0) offeredFolder.reset(ILCloneFull(folders[active].folder.get()));
        layout();
        return schedule(settled ? 500 : 25) ? S_OK : E_FAIL;
    }
    void detach() {
        for (HWND w : {statusBar,statusHost,bandParent}) if (w) RemoveWindowSubclass(w,hostMenuProc,reinterpret_cast<UINT_PTR>(this));
        statusBar=statusHost=bandParent=nullptr;
        endDrag();
        clearLoading();
        closed.clear();
        restoring=rememberRemoval=false;
        queuedTab=-1;
        if (watchingKeys) {
            watchingKeys=false;
            auto** link=&threadBands;
            while (*link != this) link=&(*link)->nextOnThread;
            *link=nextOnThread;
            nextOnThread=nullptr;
            if (!threadBands) { UnhookWindowsHookEx(threadKeys); threadKeys=nullptr; }
        }
        settle();
        presentation.finish();
        incoming = nullptr;
        awaitingAck = nullptr;
        importIndex = -1;
        nextImported = -1;
        hideTitle();
        if (tooltip) DestroyWindow(tooltip);
        tooltip = nullptr;
        tipTitle.reset();
        if (bar) DestroyWindow(bar);
        bar = tabs = add = nullptr;
        stripSize = itemSize = {-1,-1};
        if (font) { DeleteObject(font); font = nullptr; }
        folders.clear();
        nativeLocation.reset();
        closingTo.reset();
        active = destination = pendingNew = pressedTab = separating = -1;
        independent = nullptr;
        separationFolder.reset();
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
            if (!show) { hideTitle(); if (!closingMergedFrame) presentation.finish(); }
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
    STDMETHODIMP UIActivateIO(BOOL on, MSG*) override { if (on && tabs) SetFocus(IsWindowVisible(tabs) ? tabs : add); return S_OK; }
    STDMETHODIMP HasFocusIO() override { HWND w = GetFocus(); return bar && (w == bar || IsChild(bar,w)) ? S_OK : S_FALSE; }
    STDMETHODIMP TranslateAcceleratorIO(MSG* m) override {
        if (!m || (m->message != WM_KEYDOWN && m->message != WM_SYSKEYDOWN) || HasFocusIO() != S_OK) return S_FALSE;
        try { return runShortcut(shortcutFor(m->wParam)) ? S_OK : S_FALSE; } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    }
    STDMETHODIMP GetClassID(CLSID* out) override { if (!out) return E_POINTER; *out = clsid; return S_OK; }
    STDMETHODIMP IsDirty() override { return S_FALSE; }
    STDMETHODIMP Load(IStream*) override { return S_OK; }
    STDMETHODIMP Save(IStream*, BOOL) override { return S_OK; }
    STDMETHODIMP GetSizeMax(ULARGE_INTEGER* out) override { if (!out) return E_POINTER; out->QuadPart = 0; return S_OK; }
};

class Factory final : public IClassFactory {
    LONG refs = 1;
    LPCWSTR folderVerb;
public:
    explicit Factory(LPCWSTR verb) : folderVerb(verb) { InterlockedIncrement(&objects); }
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
        IUnknown* obj = folderVerb ? static_cast<IExecuteCommand*>(new(std::nothrow) FolderOpen(folderVerb)) :
            static_cast<IUnknown*>(static_cast<IDeskBand*>(new(std::nothrow) Band));
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
    LPCWSTR verb = nullptr;
    if (id==desktopOpenId) verb=L"open";
    else if (id==exploreFolderId) verb=L"explore";
    else if (id!=clsid) return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new(std::nothrow) Factory(verb);
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
