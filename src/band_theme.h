#pragma once
#include <uxtheme.h>
#include <vssym32.h>

// Keep the real tab and button controls for navigation, accessibility and hit testing.
// The Win10 tab control has no dark ribbon renderer, so only its dark surface is drawn here.
class BandTheme {
    HTHEME checkTheme = nullptr;
    COLORREF background = 0, surface = 0, hover = 0, edge = 0, text = 0, muted = 0;
    HWND hotWindow = nullptr;
    int hotTab = -1;
    static COLORREF mix(COLORREF a, COLORREF b, int percent) {
        auto channel = [percent](int x, int y) { return (x * (100 - percent) + y * percent) / 100; };
        return RGB(channel(GetRValue(a), GetRValue(b)), channel(GetGValue(a), GetGValue(b)), channel(GetBValue(a), GetBValue(b)));
    }
    static void fill(HDC dc, RECT r, COLORREF color) {
        SetDCBrushColor(dc, color);
        FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    }
    static void outline(HDC dc, RECT r, COLORREF color) {
        SetDCBrushColor(dc, color);
        FrameRect(dc, &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    }
    static bool darkApps() {
        HIGHCONTRASTW hc{};
        hc.cbSize = sizeof(hc);
        if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) || (hc.dwFlags & HCF_HIGHCONTRASTON)) return false;
        DWORD light = 1, size = sizeof(light);
        return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size) == ERROR_SUCCESS && !light;
    }
    void draw(HWND w, HWND tabs, HWND check, HDC dc, const RECT& bounds) const {
        fill(dc, bounds, background);
        SetBkMode(dc, TRANSPARENT);
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(w, WM_GETFONT, 0, 0));
        if (font) SelectObject(dc, font);
        const bool focus = GetFocus() == w && !(SendMessageW(w, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS);
        RECT line = bounds;
        line.top = line.bottom - 1;
        fill(dc, line, edge);
        if (w == tabs) {
            const int selected = TabCtrl_GetCurSel(tabs);
            const int keyboard = TabCtrl_GetCurFocus(tabs);
            for (int i = 0, count = TabCtrl_GetItemCount(tabs); i < count; ++i) {
                RECT r{};
                if (!TabCtrl_GetItemRect(tabs, i, &r) || r.right <= 0 || r.left >= bounds.right) continue;
                r.top = 2;
                r.bottom = bounds.bottom;
                if (i == selected) {
                    fill(dc, r, surface);
                    RECT border = r;
                    ++border.bottom;
                    outline(dc, border, edge);
                } else if (w == hotWindow && i == hotTab) {
                    --r.bottom;
                    fill(dc, r, hover);
                }
                wchar_t name[512]{};
                TCITEMW item{};
                item.mask = TCIF_TEXT;
                item.pszText = name;
                item.cchTextMax = 512;
                TabCtrl_GetItem(tabs, i, &item);
                RECT label = r;
                InflateRect(&label, -4, 0);
                SetTextColor(dc, IsWindowEnabled(w) ? text : muted);
                DrawTextW(dc, name, -1, &label, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
                if (focus && i == keyboard) { InflateRect(&r, -3, -3); DrawFocusRect(dc, &r); }
            }
            return;
        }
        RECT area = bounds;
        --area.bottom;
        bool enabled = IsWindowEnabled(w) != FALSE;
        bool pressed = (SendMessageW(w, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0;
        bool hot = enabled && w == hotWindow;
        if (pressed || hot) fill(dc, area, pressed ? edge : hover);
        wchar_t name[32]{};
        GetWindowTextW(w, name, 32);
        UINT align = DT_CENTER;
        if (w == check) {
            bool checked = SendMessageW(w, BM_GETCHECK, 0, 0) == BST_CHECKED;
            int state = (checked ? CBS_CHECKEDNORMAL : CBS_UNCHECKEDNORMAL) +
                        (!enabled ? 3 : pressed ? 2 : hot ? 1 : 0);
            int dpi = GetDeviceCaps(dc, LOGPIXELSX);
            SIZE glyph{MulDiv(13, dpi, 96), MulDiv(13, dpi, 96)};
            if (checkTheme) GetThemePartSize(checkTheme, dc, BP_CHECKBOX, state, nullptr, TS_TRUE, &glyph);
            int left = MulDiv(6, dpi, 96), top = (area.bottom - glyph.cy) / 2;
            RECT box{left, top, left + glyph.cx, top + glyph.cy};
            if (!checkTheme || FAILED(DrawThemeBackground(checkTheme, dc, BP_CHECKBOX, state, &box, nullptr)))
                DrawFrameControl(dc, &box, DFC_BUTTON, DFCS_BUTTONCHECK | (checked ? DFCS_CHECKED : 0));
            area.left = box.right + MulDiv(5, dpi, 96);
            align = DT_LEFT;
        }
        SetTextColor(dc, enabled ? text : muted);
        DrawTextW(dc, name, -1, &area, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | align);
        if (focus) { RECT r = bounds; InflateRect(&r, -3, -3); DrawFocusRect(dc, &r); }
    }
public:
    bool dark = false;
    ~BandTheme() { if (checkTheme) CloseThemeData(checkTheme); }
    void refresh(HWND bar, bool force) {
        bool next = darkApps();
        if (!force && next == dark) return;
        dark = next;
        if (checkTheme) { CloseThemeData(checkTheme); checkTheme = nullptr; }
        if (dark) {
            background = RGB(0,0,0);
            text = RGB(244,244,244);
            HTHEME tree = OpenThemeData(bar, L"DarkMode_Explorer::TreeView");
            COLORREF themed;
            if (tree) {
                if (SUCCEEDED(GetThemeColor(tree, TVP_TREEITEM, TREIS_NORMAL, TMT_TEXTCOLOR, &themed)) &&
                    GetRValue(themed) > 175 && GetGValue(themed) > 175 && GetBValue(themed) > 175) text = themed;
                CloseThemeData(tree);
            }
            surface = mix(background, text, 13);
            hover = mix(background, text, 20);
            edge = mix(background, text, 32);
            muted = mix(background, text, 54);
            checkTheme = OpenThemeData(bar, L"DarkMode_Explorer::Button");
        }
        hotWindow = nullptr;
        hotTab = -1;
        RedrawWindow(bar, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
    void pointer(HWND w, HWND tabs, UINT msg, LPARAM lp) {
        HWND next = msg == WM_MOUSELEAVE ? nullptr : w;
        int index = -1;
        if (next == tabs) {
            TCHITTESTINFO hit{};
            hit.pt = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
            index = TabCtrl_HitTest(tabs, &hit);
        }
        if (next == hotWindow && index == hotTab) return;
        HWND old = hotWindow;
        if (next && next != old) {
            TRACKMOUSEEVENT event{sizeof(event), TME_LEAVE, next, 0};
            TrackMouseEvent(&event);
        }
        hotWindow = next;
        hotTab = index;
        if (old) InvalidateRect(old, nullptr, FALSE);
        if (next) InvalidateRect(next, nullptr, FALSE);
    }
    void paint(HWND w, HWND tabs, HWND check, HDC target) const {
        RECT r{};
        GetClientRect(w, &r);
        if (IsRectEmpty(&r)) return;
        HDC buffer = CreateCompatibleDC(target);
        HBITMAP bitmap = buffer ? CreateCompatibleBitmap(target, r.right, r.bottom) : nullptr;
        if (bitmap) {
            HGDIOBJ old = SelectObject(buffer, bitmap);
            draw(w, tabs, check, buffer, r);
            BitBlt(target, 0, 0, r.right, r.bottom, buffer, 0, 0, SRCCOPY);
            SelectObject(buffer, old);
            DeleteObject(bitmap);
        } else {
            int saved = SaveDC(target);
            draw(w, tabs, check, target, r);
            RestoreDC(target, saved);
        }
        if (buffer) DeleteDC(buffer);
    }
    void erase(HDC dc, HWND bar) const { RECT r{}; GetClientRect(bar, &r); fill(dc, r, background); }
};
