// Shell automation supplies the folder of another Explorer window, even when the
// two windows live in different processes. Window messages only carry identities.
#include <exdisp.h>
#include <shldisp.h>

namespace merge {
// Most Explorer windows share a process but run in separate COM apartments.
// Only immutable PIDLs cross threads; no shell interface or caller-supplied pointer does.
class LocalOffer {
    inline static SRWLOCK lock = SRWLOCK_INIT;
    inline static LocalOffer* first = nullptr;
    LocalOffer* next = nullptr;
    HWND from = nullptr, to = nullptr;
    LPARAM generation = 0;
    Location folder;
    bool acknowledged = false;

    static LocalOffer* find(HWND source, HWND host, LPARAM birth) {
        for (auto* e = first; e; e = e->next)
            if (e->from == source && e->to == host && e->generation == birth) return e;
        return nullptr;
    }
public:
    void clear() {
        AcquireSRWLockExclusive(&lock);
        if (from) {
            auto** link = &first;
            while (*link != this) link = &(*link)->next;
            *link = next;
            from = nullptr;
            folder.reset();
        }
        acknowledged = false;
        ReleaseSRWLockExclusive(&lock);
    }
    ~LocalOffer() { clear(); }
    bool publish(HWND source, HWND host, LPARAM birth, PCIDLIST_ABSOLUTE id) {
        clear();
        DWORD pid = 0;
        GetWindowThreadProcessId(host, &pid);
        if (pid != GetCurrentProcessId()) return false;
        Location copy(ILCloneFull(id));
        if (!copy) return false;
        AcquireSRWLockExclusive(&lock);
        folder = std::move(copy);
        from = source; to = host; generation = birth;
        next = first; first = this;
        ReleaseSRWLockExclusive(&lock);
        return true;
    }
    static Location read(HWND source, HWND host, LPARAM birth) {
        AcquireSRWLockShared(&lock);
        auto* offer = find(source, host, birth);
        Location copy(offer ? ILCloneFull(offer->folder.get()) : nullptr);
        ReleaseSRWLockShared(&lock);
        return copy;
    }
    static void confirm(HWND source, HWND host, LPARAM birth, PCIDLIST_ABSOLUTE landed) {
        AcquireSRWLockExclusive(&lock);
        auto* e = find(source, host, birth);
        if (e) {
            UINT size = ILGetSize(landed);
            e->acknowledged = size == ILGetSize(e->folder.get()) && !memcmp(landed, e->folder.get(), size);
        }
        ReleaseSRWLockExclusive(&lock);
    }
    bool confirmed() const {
        AcquireSRWLockShared(&lock);
        bool result = acknowledged;
        ReleaseSRWLockShared(&lock);
        return result;
    }
};
constexpr wchar_t keepFrame[] = L"EZFolder.KeepExistingFrame";
constexpr wchar_t bandProp[] = L"EZFolder.Band.2";
constexpr wchar_t fullProp[] = L"EZFolder.Full";
constexpr wchar_t birthProp[] = L"EZFolder.Created.2";
constexpr wchar_t offerProp[] = L"EZFolder.MergeTarget.2";
constexpr wchar_t startingProp[] = L"EZFolder.Starting.3";
constexpr wchar_t readyProp[] = L"EZFolder.Ready.3";
constexpr wchar_t foregroundProp[] = L"EZFolder.OpenForeground";
constexpr wchar_t inputProp[] = L"EZFolder.OpenInput";
constexpr wchar_t countProp[] = L"EZFolder.TabCount";
constexpr wchar_t emptyProp[] = L"EZFolder.NativeView";
inline bool hasView(HWND band) { return GetPropW(band,countProp) || GetPropW(band,emptyProp); }
constexpr wchar_t countSinceProp[] = L"EZFolder.TabCountSince";
constexpr wchar_t primaryProp[] = L"EZFolder.Primary";
inline UINT openPath() { static UINT id = RegisterWindowMessageW(L"EZFolder.OpenFolder.4"); return id; }
inline UINT movePath() { static UINT id = RegisterWindowMessageW(L"EZFolder.MoveTab.4"); return id; }
inline UINT openSettings() { static UINT id = RegisterWindowMessageW(L"EZFolder.Settings.4"); return id; }
inline UINT storeTab() { static UINT id = RegisterWindowMessageW(L"EZFolder.StoreTab.3"); return id; }
inline UINT storedTab() { static UINT id = RegisterWindowMessageW(L"EZFolder.StoredTab.3"); return id; }
inline UINT verifyName() { static UINT id = RegisterWindowMessageW(L"EZFolder.VerifyFolderName.3"); return id; }
inline UINT request() { static UINT id = RegisterWindowMessageW(L"EZFolder.RequestTab.2"); return id; }
inline UINT received() { static UINT id = RegisterWindowMessageW(L"EZFolder.ReceivedTab.2"); return id; }
inline UINT retry() { static UINT id = RegisterWindowMessageW(L"EZFolder.RetryTab.2"); return id; }

inline HWND frame(HWND band) {
    HWND root = GetAncestor(band, GA_ROOT);
    wchar_t name[32]{};
    GetClassNameW(root, name, 32);
    return !wcscmp(name, L"CabinetWClass") || !wcscmp(name, L"ExploreWClass") ? root : nullptr;
}
inline bool live(HWND band) {
    HWND root = frame(band);
    return root && GetPropW(root, bandProp) == band && IsWindowVisible(band);
}
inline ULONG_PTR number(HWND w,const wchar_t* key) { return reinterpret_cast<ULONG_PTR>(GetPropW(w,key)); }
struct Search { HWND source; HWND result; bool includeDisabled; bool markedOnly; };
inline BOOL CALLBACK olderWindow(HWND root, LPARAM data) {
    auto& s = *reinterpret_cast<Search*>(data);
    HWND candidate = static_cast<HWND>(GetPropW(root, bandProp));
    if (!candidate || !live(candidate)) return TRUE;
    if (s.markedOnly) {
        if (!GetPropW(candidate,primaryProp)) return TRUE;
        s.result=candidate; return FALSE;
    }
    if (GetPropW(candidate,fullProp) || (!s.includeDisabled && !IsWindowEnabled(root))) return TRUE;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(root, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;
    auto birth = reinterpret_cast<ULONG_PTR>(GetPropW(candidate, birthProp));
    auto own = reinterpret_cast<ULONG_PTR>(GetPropW(s.source, birthProp));
    if (s.source && !(birth < own || (birth == own && reinterpret_cast<UINT_PTR>(candidate) < reinterpret_cast<UINT_PTR>(s.source)))) return TRUE;
    if (!s.source && (!hasView(candidate) || !GetPropW(candidate,readyProp))) return TRUE;
    auto rank=[](HWND w) {
        return std::make_tuple(!GetPropW(w,startingProp),!!GetPropW(w,primaryProp),number(w,countProp),
            ~number(w,countSinceProp),~number(w,birthProp),~reinterpret_cast<UINT_PTR>(w));
    };
    if (!s.result || rank(candidate)>rank(s.result))
        s.result = candidate;
    return TRUE;
}
inline HWND target(HWND source, bool includeDisabled = false) {
    Search search{source, nullptr, includeDisabled,false};
    EnumWindows(olderWindow, reinterpret_cast<LPARAM>(&search));
    return search.result;
}
inline void markReceiver(HWND bar) {
    Search search{nullptr,nullptr,true,true};
    EnumWindows(olderWindow,reinterpret_cast<LPARAM>(&search));
    if (!search.result && bar==target(nullptr)) SetPropW(bar,primaryProp,reinterpret_cast<HANDLE>(1));
}
inline bool sendPath(HWND target,HWND source,UINT type,const wchar_t* path) {
    COPYDATASTRUCT packet{type,static_cast<DWORD>((wcslen(path)+1)*sizeof(wchar_t)),const_cast<wchar_t*>(path)};
    DWORD_PTR result=0;
    return SendMessageTimeoutW(target,WM_COPYDATA,reinterpret_cast<WPARAM>(source),reinterpret_cast<LPARAM>(&packet),
        SMTO_ABORTIFHUNG|SMTO_BLOCK,3000,&result) && result==1;
}
inline Location folderOf(HWND band) {
    HWND root = frame(band);
    Com<IShellWindows> windows;
    if (!root || FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows.p)))) return {};
    long count = 0;
    windows->get_Count(&count);
    for (long i = 0; i < count; ++i) {
        VARIANT index{};
        index.vt = VT_I4;
        index.lVal = i;
        Com<IDispatch> window, doc;
        Com<IWebBrowser2> web;
        SHANDLE_PTR handle = 0;
        if (FAILED(windows->Item(index, &window.p)) || !window.p ||
            FAILED(window->QueryInterface(IID_PPV_ARGS(&web.p))) ||
            FAILED(web->get_HWND(&handle)) || reinterpret_cast<HWND>(handle) != root) continue;
        Com<IShellFolderViewDual> view;
        Com<Folder> folder;
        Com<Folder2> folder2;
        Com<FolderItem> item;
        if (FAILED(web->get_Document(&doc.p)) || !doc.p || FAILED(doc->QueryInterface(IID_PPV_ARGS(&view.p))) ||
            FAILED(view->get_Folder(&folder.p)) || !folder.p || FAILED(folder->QueryInterface(IID_PPV_ARGS(&folder2.p))) ||
            FAILED(folder2->get_Self(&item.p)) || !item.p) return {};
        PIDLIST_ABSOLUTE id = nullptr;
        if (FAILED(SHGetIDListFromObject(item.p, &id))) {
            BSTR path = nullptr;
            if (SUCCEEDED(item->get_Path(&path)) && path) SHParseDisplayName(path, nullptr, &id, 0, nullptr);
            SysFreeString(path);
        }
        return Location(id);
    }
    return {};
}
}
