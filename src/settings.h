#pragma once
namespace settings {
constexpr wchar_t key[] = L"Software\\EZFolder";
enum Action { New, Close, Next, Previous, Restore, ActionCount };
constexpr DWORD control = 0x100, shift = 0x200, alt = 0x400;
struct Preferences {
    DWORD quick = 0;
    DWORD shortcuts[ActionCount] = {control|'T',control|'W',control|VK_TAB,control|shift|VK_TAB,control|shift|'T'};
};
inline Preferences load() {
    Preferences prefs;
    DWORD size = sizeof(prefs);
    if (RegGetValueW(HKEY_CURRENT_USER,key,L"PreferencesV1",RRF_RT_REG_BINARY,nullptr,&prefs,&size) == ERROR_SUCCESS &&
        prefs.quick <= 1) {
        if (size == sizeof(prefs)) return prefs;
        if (size == sizeof(prefs)-sizeof(DWORD)) {
            prefs.shortcuts[Restore]=Preferences{}.shortcuts[Restore];
            for (int i=0;i<Restore;++i)
                if (prefs.shortcuts[i]==prefs.shortcuts[Restore]) prefs.shortcuts[Restore]=0;
            return prefs;
        }
    }
    prefs = Preferences{};
    DWORD value = 0, bytes = sizeof(value);
    prefs.quick = RegGetValueW(HKEY_CURRENT_USER, key, L"NewTabLocation", RRF_RT_REG_DWORD,
                        nullptr, &value, &bytes) == ERROR_SUCCESS && value == 1;
    return prefs;
}
inline bool quickAccess() { return load().quick != 0; }
}
