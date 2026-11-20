#pragma once
#include <dwmapi.h>

// The shell view keeps loading normally while DWM omits this frame from composition.
// The watchdog runs independently of Explorer's UI thread, including during a slow COM call.
class MergeVisibility {
    static constexpr wchar_t ownerProp[] = L"EZFolder.PendingPresentation";
    static constexpr wchar_t targetProp[] = L"EZFolder.PresentationTarget.3";
    static constexpr wchar_t progressProp[] = L"EZFolder.LastStored.3";
    HWND frame = nullptr;
    HWND target = nullptr;
    ULONGLONG started = 0, visibleSince = 0;
    bool burst = false;
    SRWLOCK checkLock = SRWLOCK_INIT;
    PTP_TIMER watchdog = nullptr;

    void reveal() {
        if (frame && GetPropW(frame, ownerProp) == this) {
            BOOL cloak = FALSE;
            DwmSetWindowAttribute(frame, DWMWA_CLOAK, &cloak, sizeof(cloak));
            RemovePropW(frame, ownerProp);
            RemovePropW(frame, targetProp);
        }
    }
    struct Search { HWND host, except; bool found; };
    static BOOL CALLBACK otherPending(HWND root, LPARAM data) {
        auto& s = *reinterpret_cast<Search*>(data);
        if (root == s.except || GetPropW(root,targetProp) != s.host) return TRUE;
        s.found = true;
        return FALSE;
    }
    static void CALLBACK expired(PTP_CALLBACK_INSTANCE, void* context, PTP_TIMER) {
        auto& self = *static_cast<MergeVisibility*>(context);
        if (!TryAcquireSRWLockExclusive(&self.checkLock)) return;
        if (GetPropW(self.frame,ownerProp) == &self) {
            auto now = GetTickCount64();
            if (!self.visibleSince && IsWindowVisible(self.frame)) self.visibleSince = now;
            if (!self.burst) self.burst = pending(self.target,self.frame);
            const auto progress = reinterpret_cast<ULONG_PTR>(GetPropW(self.target,progressProp));
            bool progressing = self.burst && progress && progress <= now && now-progress < 2000;
            if (!IsWindow(self.target) || now-self.started >= 8000 ||
                (self.visibleSince && now-self.visibleSince >= (self.burst ? 5000u : 1000u) && !progressing))
                self.reveal();
        }
        ReleaseSRWLockExclusive(&self.checkLock);
    }
public:
    static void stored(HWND host, ULONGLONG now) {
        SetPropW(host,progressProp,reinterpret_cast<HANDLE>(now));
    }
    static bool pending(HWND host, HWND except = nullptr) {
        Search search{host,except,false};
        EnumWindows(otherPending,reinterpret_cast<LPARAM>(&search));
        return search.found;
    }
    void begin(HWND root, HWND host) {
        if (watchdog || !root || GetPropW(root, ownerProp)) return;
        DWORD pid = 0, cloaked = 0;
        GetWindowThreadProcessId(root, &pid);
        if (pid != GetCurrentProcessId() ||
            FAILED(DwmGetWindowAttribute(root, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) || cloaked) return;
        watchdog = CreateThreadpoolTimer(expired, this, nullptr);
        if (!watchdog) return;
        frame = root;
        target = host;
        started = GetTickCount64();
        visibleSince = IsWindowVisible(root) ? started : 0;
        burst = false;
        BOOL cloak = TRUE;
        if (!SetPropW(root, ownerProp, this) ||
            FAILED(DwmSetWindowAttribute(root, DWMWA_CLOAK, &cloak, sizeof(cloak)))) {
            if (GetPropW(root, ownerProp) == this) RemovePropW(root, ownerProp);
            return;
        }
        SetPropW(root,targetProp,host);
        LARGE_INTEGER relative{};
        relative.QuadPart = -100LL * 10000;
        FILETIME due{relative.LowPart, static_cast<DWORD>(relative.HighPart)};
        SetThreadpoolTimer(watchdog, &due, 100, 0);
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
