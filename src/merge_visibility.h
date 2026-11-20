#pragma once
#include <dwmapi.h>

// The shell view keeps loading normally while DWM omits this frame from composition.
// The watchdog runs independently of Explorer's UI thread, including during a slow COM call.
class MergeVisibility {
    static constexpr wchar_t ownerProp[] = L"EZFolder.PendingPresentation";
    HWND frame = nullptr;
    PTP_TIMER watchdog = nullptr;

    void reveal() {
        if (frame && GetPropW(frame, ownerProp) == this) {
            BOOL cloak = FALSE;
            DwmSetWindowAttribute(frame, DWMWA_CLOAK, &cloak, sizeof(cloak));
            RemovePropW(frame, ownerProp);
        }
    }
    static void CALLBACK expired(PTP_CALLBACK_INSTANCE, void* context, PTP_TIMER) {
        static_cast<MergeVisibility*>(context)->reveal();
    }
public:
    void begin(HWND root) {
        if (watchdog || !root || GetPropW(root, ownerProp)) return;
        DWORD pid = 0, cloaked = 0;
        GetWindowThreadProcessId(root, &pid);
        if (pid != GetCurrentProcessId() ||
            FAILED(DwmGetWindowAttribute(root, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) || cloaked) return;
        watchdog = CreateThreadpoolTimer(expired, this, nullptr);
        if (!watchdog) return;
        frame = root;
        BOOL cloak = TRUE;
        if (!SetPropW(root, ownerProp, this) ||
            FAILED(DwmSetWindowAttribute(root, DWMWA_CLOAK, &cloak, sizeof(cloak)))) {
            if (GetPropW(root, ownerProp) == this) RemovePropW(root, ownerProp);
            return;
        }
        LARGE_INTEGER relative{};
        relative.QuadPart = -1000LL * 10000; // At most one second waiting for a local handoff.
        FILETIME due{relative.LowPart, static_cast<DWORD>(relative.HighPart)};
        SetThreadpoolTimer(watchdog, &due, 0, 0);
    }
    void finish() {
        if (!watchdog) return;
        SetThreadpoolTimer(watchdog, nullptr, 0, 0);
        WaitForThreadpoolTimerCallbacks(watchdog, TRUE);
        reveal();
        CloseThreadpoolTimer(watchdog);
        watchdog = nullptr;
        frame = nullptr;
    }
    ~MergeVisibility() { finish(); }
};
