#pragma once
namespace settings {
constexpr wchar_t key[] = L"Software\\EZFolder";
inline bool quickAccess() {
    DWORD value = 0, bytes = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, key, L"NewTabLocation", RRF_RT_REG_DWORD,
                        nullptr, &value, &bytes) == ERROR_SUCCESS && value == 1;
}
}
