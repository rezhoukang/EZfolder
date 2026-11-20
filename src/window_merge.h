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
constexpr wchar_t settings[] = L"Software\\EZFolder";
constexpr wchar_t bandProp[] = L"EZFolder.Band.2";
constexpr wchar_t birthProp[] = L"EZFolder.Created.2";
constexpr wchar_t offerProp[] = L"EZFolder.MergeTarget.2";
inline UINT request() { static UINT id = RegisterWindowMessageW(L"EZFolder.RequestTab.2"); return id; }
inline UINT received() { static UINT id = RegisterWindowMessageW(L"EZFolder.ReceivedTab.2"); return id; }
inline UINT retry() { static UINT id = RegisterWindowMessageW(L"EZFolder.RetryTab.2"); return id; }

inline bool enabled() {
    DWORD value = 1, bytes = sizeof(value);
    LSTATUS result = RegGetValueW(HKEY_CURRENT_USER, settings, L"MergeNewWindows",
                                  RRF_RT_REG_DWORD, nullptr, &value, &bytes);
    return result == ERROR_FILE_NOT_FOUND || (result == ERROR_SUCCESS && value != 0);
}
inline bool setEnabled(bool value) {
    DWORD setting = value;
    return RegSetKeyValueW(HKEY_CURRENT_USER, settings, L"MergeNewWindows", REG_DWORD,
                           &setting, sizeof(setting)) == ERROR_SUCCESS;
}
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
struct Search { HWND source; HWND result; };
inline BOOL CALLBACK olderWindow(HWND root, LPARAM data) {
    auto& s = *reinterpret_cast<Search*>(data);
    HWND candidate = static_cast<HWND>(GetPropW(root, bandProp));
    if (!candidate || !live(candidate) || !IsWindowEnabled(root)) return TRUE;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(root, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;
    auto birth = reinterpret_cast<ULONG_PTR>(GetPropW(candidate, birthProp));
    auto own = reinterpret_cast<ULONG_PTR>(GetPropW(s.source, birthProp));
    if (birth < own || (birth == own && reinterpret_cast<UINT_PTR>(candidate) < reinterpret_cast<UINT_PTR>(s.source))) {
        s.result = candidate;
        return FALSE;
    }
    return TRUE;
}
inline HWND target(HWND source) {
    Search search{source, nullptr};
    EnumWindows(olderWindow, reinterpret_cast<LPARAM>(&search));
    return search.result;
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
