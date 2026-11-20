#pragma once
#include <uxtheme.h>

// Preserve the native tab control's focus, scrolling and accessibility.
class BandTheme {
    COLORREF background{}, surface{}, hover{}, edge{}, text{}, selectedText{};
    HWND hotWindow = nullptr;
    int hotTab = -1;
    static void fill(HDC dc, const RECT& r, COLORREF color) {
        SetDCBrushColor(dc, color);
        FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    }
    static void rounded(HDC dc, RECT r, COLORREF color, int radius) {
        SetDCBrushColor(dc, color);
        SelectObject(dc, GetStockObject(DC_BRUSH));
        SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, r.left,r.top,r.right,r.bottom,radius,radius);
    }
    static void line(HDC dc, int x, int y, int x2, int y2, COLORREF color) {
        SelectObject(dc, GetStockObject(DC_PEN));
        SetDCPenColor(dc,color);
        MoveToEx(dc,x,y,nullptr);
        LineTo(dc,x2,y2);
    }
    void draw(HWND w, HWND tabs, HDC dc, RECT bounds, int dpi) const {
        auto px = [dpi](int n) { return MulDiv(n,dpi,96); };
        fill(dc,bounds,background);
        SetBkMode(dc,TRANSPARENT);
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(w,WM_GETFONT,0,0));
        if (font) SelectObject(dc,font);
        bool focus = GetFocus() == w && !(SendMessageW(w,WM_QUERYUISTATE,0,0) & UISF_HIDEFOCUS);
        if (w != tabs) {
            RECT r = bounds;
            InflateRect(&r,-px(3),-px(3));
            if (hotWindow == w) rounded(dc,r,hover,px(8));
            int x = bounds.right/2, y = bounds.bottom/2, arm = px(6);
            COLORREF ink = IsWindowEnabled(w) ? text : edge;
            line(dc,x-arm,y,x+arm+1,y,ink);
            line(dc,x,y-arm,x,y+arm+1,ink);
            if (focus) DrawFocusRect(dc,&r);
            return;
        }
        int selected = TabCtrl_GetCurSel(tabs), count = TabCtrl_GetItemCount(tabs);
        for (int i=0; i<count; ++i) {
            RECT r{};
            if (!TabCtrl_GetItemRect(tabs,i,&r) || r.right <= 0 || r.left >= bounds.right) continue;
            r.top = px(2);
            r.bottom = bounds.bottom - px(1);
            bool active = i == selected, hot = w == hotWindow && i == hotTab;
            COLORREF ink = active ? selectedText : text;
            if (active || hot) rounded(dc,r,active ? surface : hover,px(10));
            if (!active && i+1 < count && i+1 != selected)
                line(dc,r.right-1,px(6),r.right-1,r.bottom-px(5),edge);
            wchar_t title[512]{};
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = title;
            item.cchTextMax = ARRAYSIZE(title);
            TabCtrl_GetItem(tabs,i,&item);
            RECT close = closeRect(r,dpi,hot);
            RECT label = labelRect(r,dpi,hot);
            if (i==loading && label.right-label.left>=px(16)) {
                BOOL animate=TRUE;
                SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animate,0);
                int x=label.left+px(5), y=(r.top+r.bottom)/2;
                static const POINT dots[]={{0,-4},{3,-3},{4,0},{3,3},{0,4},{-3,3},{-4,0},{-3,-3}};
                int phase=animate ? static_cast<int>((GetTickCount64()/90)%8) : 0;
                for (int j=0;j<8;++j) {
                    int dx=x+px(dots[j].x),dy=y+px(dots[j].y);
                    RECT dot{dx,dy,dx+px(2),dy+px(2)};
                    fill(dc,dot,((j-phase+8)%8)<3 ? ink : edge);
                }
                label.left+=px(15);
            }
            SetTextColor(dc,ink);
            UINT flags = DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX;
            if (label.right-label.left >= px(30)) flags |= DT_END_ELLIPSIS;
            DrawTextW(dc,title,-1,&label,flags);
            if (!IsRectEmpty(&close)) {
                int x = (close.left+close.right)/2, y = (r.top+r.bottom)/2, arm = px(3);
                line(dc,x-arm,y-arm,x+arm+1,y+arm+1,ink);
                line(dc,x-arm,y+arm,x+arm+1,y-arm-1,ink);
            }
            if (focus && i == TabCtrl_GetCurFocus(tabs)) { InflateRect(&r,-px(3),-px(3)); DrawFocusRect(dc,&r); }
        }
    }
public:
    int loading=-1;
    bool darkMenu() const { return background==RGB(0,0,0) && surface==RGB(48,48,48); }
    void menuItem(const DRAWITEMSTRUCT& item,int dpi) const {
        auto* data=reinterpret_cast<const MSAAMENUINFO*>(item.itemData);
        if (!data) return;
        int saved=SaveDC(item.hDC);
        RECT r=item.rcItem;
        fill(item.hDC,r,item.itemState&ODS_SELECTED ? RGB(65,65,65) : RGB(43,43,43));
        r.left+=MulDiv(23,dpi,96);
        line(item.hDC,r.left,r.top,r.left,r.bottom,edge);
        r.left+=MulDiv(8,dpi,96);
        SetTextColor(item.hDC,item.itemState&ODS_DISABLED ? GetSysColor(COLOR_GRAYTEXT) : text);
        SetBkMode(item.hDC,TRANSPARENT);
        const wchar_t* split=wcschr(data->pszWText,L'\t');
        DrawTextW(item.hDC,data->pszWText,split ? int(split-data->pszWText) : -1,&r,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
        if (split) {
            r.right-=MulDiv(10,dpi,96);
            DrawTextW(item.hDC,split+1,-1,&r,DT_RIGHT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
        }
        RestoreDC(item.hDC,saved);
    }
    static RECT closeRect(RECT r, int dpi, bool hot) {
        int width = MulDiv(r.right-r.left,96,dpi);
        int button = width >= 80 ? 23 : width >= 48 ? 18 : (hot && width >= 24 ? 11 : 0);
        r.left = r.right - MulDiv(button,dpi,96);
        return r;
    }
    static RECT labelRect(RECT r, int dpi, bool hot) {
        int padding = r.right-r.left < MulDiv(48,dpi,96) ? 2 : 7;
        r.right = closeRect(r,dpi,hot).left;
        r.left += MulDiv(padding,dpi,96);
        return r;
    }
    void refresh(HWND bar, HWND tip, bool force) {
        DWORD light=1, bytes=sizeof(light);
        RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&bytes);
        HIGHCONTRASTW hc{};
        hc.cbSize=sizeof(hc);
        SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(hc),&hc,0);
        bool contrast = (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
        bool dark = !light && !contrast;
        COLORREF bg = dark ? RGB(0,0,0) : GetSysColor(COLOR_WINDOW);
        COLORREF nextSurface = contrast ? GetSysColor(COLOR_HIGHLIGHT) : dark ? RGB(48,48,48) : RGB(226,226,226);
        if (!force && bg == background && nextSurface == surface) return;
        background = bg;
        surface = nextSurface;
        text = dark ? RGB(242,242,242) : GetSysColor(COLOR_WINDOWTEXT);
        selectedText = contrast ? GetSysColor(COLOR_HIGHLIGHTTEXT) : text;
        hover = contrast ? bg : dark ? RGB(31,31,31) : RGB(241,241,241);
        edge = dark ? RGB(86,86,86) : GetSysColor(COLOR_GRAYTEXT);
        if (tip) {
            SetWindowTheme(tip,dark ? L"" : nullptr,dark ? L"" : nullptr);
            SendMessageW(tip,TTM_SETTIPBKCOLOR,dark ? RGB(42,42,42) : GetSysColor(COLOR_INFOBK),0);
            SendMessageW(tip,TTM_SETTIPTEXTCOLOR,dark ? text : GetSysColor(COLOR_INFOTEXT),0);
        }
        RedrawWindow(bar,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
    }
    void pointer(HWND w, HWND tabs, UINT msg, LPARAM lp) {
        HWND next = msg == WM_MOUSELEAVE ? nullptr : w;
        int index = -1;
        if (next == tabs) {
            TCHITTESTINFO hit{};
            hit.pt = {static_cast<short>(LOWORD(lp)),static_cast<short>(HIWORD(lp))};
            index = TabCtrl_HitTest(tabs,&hit);
        }
        if (next == hotWindow && index == hotTab) return;
        if (next && next != hotWindow) {
            TRACKMOUSEEVENT event{sizeof(event),TME_LEAVE,next,0};
            TrackMouseEvent(&event);
        }
        HWND old = hotWindow;
        hotWindow = next;
        hotTab = index;
        if (old) InvalidateRect(old,nullptr,FALSE);
        if (next) InvalidateRect(next,nullptr,FALSE);
    }
    void paint(HWND w, HWND tabs, HDC target, int dpi) const {
        RECT r{};
        GetClientRect(w,&r);
        if (IsRectEmpty(&r)) return;
        HDC buffer = CreateCompatibleDC(target);
        HBITMAP bitmap = buffer ? CreateCompatibleBitmap(target,r.right,r.bottom) : nullptr;
        if (bitmap) {
            HGDIOBJ old = SelectObject(buffer,bitmap);
            draw(w,tabs,buffer,r,dpi);
            BitBlt(target,0,0,r.right,r.bottom,buffer,0,0,SRCCOPY);
            SelectObject(buffer,old);
            DeleteObject(bitmap);
        } else {
            int saved = SaveDC(target);
            draw(w,tabs,target,r,dpi);
            RestoreDC(target,saved);
        }
        if (buffer) DeleteDC(buffer);
    }
    void erase(HDC dc, HWND bar) const { RECT r{}; GetClientRect(bar,&r); fill(dc,r,background); }
};
