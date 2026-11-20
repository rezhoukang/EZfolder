#pragma once
#include <shlwapi.h>

// Called by the Directory default verb. Pass the original Shell command its
// original site, selection and options so in-window navigation stays native.
static constexpr CLSID desktopOpenId={0x885e4b09,0xda21,0x42f4,{0xbd,0x05,0x8a,0x91,0xfb,0xe8,0xfb,0xa6}};
class DesktopOpen final : public IExecuteCommand, public IObjectWithSelection, public IObjectWithSite {
    LONG refs=1;
    Com<IShellItemArray> selection;
    Com<IUnknown> site;
    using ShellString=std::unique_ptr<wchar_t,decltype(&CoTaskMemFree)>;
    ShellString parameters{nullptr,CoTaskMemFree},directory{nullptr,CoTaskMemFree};
    DWORD keys=0;
    int show=SW_SHOWNORMAL;
    bool silent=false;

    static HRESULT setString(ShellString& value,LPCWSTR text) {
        PWSTR copy=nullptr;
        if (text && *text && FAILED(SHStrDupW(text,&copy))) return E_OUTOFMEMORY;
        value.reset(copy);
        return S_OK;
    }
    HRESULT openNative() {
        if (!selection.p) return E_UNEXPECTED;
        DWORD count=0;
        HRESULT hr=selection->GetCount(&count);
        for (DWORD i=0;SUCCEEDED(hr) && i<count;++i) {
            Com<IShellItem> item;
            hr=selection->GetItemAt(i,&item.p);
            PIDLIST_ABSOLUTE raw=nullptr;
            if (SUCCEEDED(hr)) hr=SHGetIDListFromObject(item.p,&raw);
            Location folder(raw);
            if (FAILED(hr)) break;
            // Let ShellExecute initialize the system Folder verb with its own
            // association context. Naming the verb also avoids re-entering our default.
            SHELLEXECUTEINFOW execute{};
            execute.cbSize=sizeof(execute);
            execute.fMask=SEE_MASK_CLASSNAME|SEE_MASK_IDLIST|SEE_MASK_NOASYNC;
            if (silent) execute.fMask|=SEE_MASK_FLAG_NO_UI;
            if (site.p) {
                execute.fMask|=SEE_MASK_FLAG_HINST_IS_SITE;
                execute.hInstApp=reinterpret_cast<HINSTANCE>(site.p);
                Com<IOleWindow> owner;
                if (SUCCEEDED(site->QueryInterface(IID_PPV_ARGS(&owner.p)))) owner->GetWindow(&execute.hwnd);
            }
            execute.lpClass=L"Folder";
            execute.lpVerb=L"open";
            execute.lpIDList=folder.get();
            execute.lpParameters=parameters.get();
            execute.lpDirectory=directory.get();
            execute.nShow=show;
            if (!ShellExecuteExW(&execute)) hr=HRESULT_FROM_WIN32(GetLastError());
        }
        return hr;
    }
    bool inFolderWindow() const {
        Com<IServiceProvider> services;
        Com<IShellBrowser> browser;
        HWND owner=nullptr;
        return site.p && SUCCEEDED(site->QueryInterface(IID_PPV_ARGS(&services.p))) && services.p &&
            SUCCEEDED(services->QueryService(SID_STopLevelBrowser,IID_PPV_ARGS(&browser.p))) && browser.p &&
            SUCCEEDED(browser->GetWindow(&owner)) && merge::frame(owner);
    }
    bool openDesktopFolder() {
        if (keys&(MK_CONTROL|MK_SHIFT) || show==SW_HIDE || parameters || !selection.p || inFolderWindow()) return false;
        Com<IShellItem> item,parent;
        DWORD count=0;
        SFGAOF attrs=0;
        if (FAILED(selection->GetCount(&count)) || count!=1 || FAILED(selection->GetItemAt(0,&item.p)) ||
            FAILED(item->GetAttributes(SFGAO_FOLDER|SFGAO_FILESYSTEM|SFGAO_LINK,&attrs)) ||
            attrs!=(SFGAO_FOLDER|SFGAO_FILESYSTEM) || FAILED(item->GetParent(&parent.p))) return false;
        PWSTR raw=nullptr;
        if (FAILED(parent->GetDisplayName(SIGDN_FILESYSPATH,&raw))) return false;
        std::unique_ptr<wchar_t,decltype(&CoTaskMemFree)> directory(raw,CoTaskMemFree);
        bool desktop=false;
        for (auto known : {FOLDERID_Desktop,FOLDERID_PublicDesktop}) {
            if (FAILED(SHGetKnownFolderPath(known,KF_FLAG_DONT_VERIFY,nullptr,&raw))) continue;
            std::unique_ptr<wchar_t,decltype(&CoTaskMemFree)> expected(raw,CoTaskMemFree);
            if (CompareStringOrdinal(directory.get(),-1,expected.get(),-1,TRUE)==CSTR_EQUAL) { desktop=true; break; }
        }
        if (!desktop) return false;
        HWND target=merge::target(nullptr);
        if (!target || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&raw))) return false;
        std::unique_ptr<wchar_t,decltype(&CoTaskMemFree)> path(raw,CoTaskMemFree);
        DWORD pid=0; GetWindowThreadProcessId(target,&pid);
        if (pid) AllowSetForegroundWindow(pid);
        return merge::sendPath(target,nullptr,merge::openPath(),path.get());
    }
public:
    DesktopOpen() { InterlockedIncrement(&objects); }
    ~DesktopOpen() { InterlockedDecrement(&objects); }
    STDMETHODIMP QueryInterface(REFIID iid,void** out) override {
        if (!out) return E_POINTER;
        *out=nullptr;
        if (iid==IID_IUnknown || iid==IID_IExecuteCommand) *out=static_cast<IExecuteCommand*>(this);
        else if (iid==IID_IObjectWithSelection) *out=static_cast<IObjectWithSelection*>(this);
        else if (iid==IID_IObjectWithSite) *out=static_cast<IObjectWithSite*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs); }
    STDMETHODIMP_(ULONG) Release() override { ULONG n=InterlockedDecrement(&refs); if (!n) delete this; return n; }
    STDMETHODIMP SetSite(IUnknown* s) override {
        if (s) s->AddRef();
        site.reset(); site.p=s; return S_OK;
    }
    STDMETHODIMP GetSite(REFIID iid,void** out) override {
        if (!out) return E_POINTER;
        *out=nullptr; return site.p ? site->QueryInterface(iid,out) : E_FAIL;
    }
    STDMETHODIMP SetSelection(IShellItemArray* s) override {
        if (s) s->AddRef();
        selection.reset(); selection.p=s; return S_OK;
    }
    STDMETHODIMP GetSelection(REFIID iid,void** out) override {
        if (!out) return E_POINTER;
        *out=nullptr; return selection.p ? selection->QueryInterface(iid,out) : E_FAIL;
    }
    STDMETHODIMP SetKeyState(DWORD k) override { keys=k; return S_OK; }
    STDMETHODIMP SetParameters(LPCWSTR p) override { return setString(parameters,p); }
    STDMETHODIMP SetPosition(POINT) override { return S_OK; }
    STDMETHODIMP SetShowWindow(int n) override { show=n; return S_OK; }
    STDMETHODIMP SetNoShowUI(BOOL b) override { silent=b; return S_OK; }
    STDMETHODIMP SetDirectory(LPCWSTR p) override { return setString(directory,p); }
    STDMETHODIMP Execute() override { return openDesktopFolder() ? S_OK : openNative(); }
};
