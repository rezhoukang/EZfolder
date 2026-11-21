#pragma once
#include <uxtheme.h>

// Preserve the native tab control's focus, scrolling and accessibility.
class BandTheme {
    COLORREF background{}, surface{}, hover{}, edge{}, text{}, selectedText{};
    COLORREF closeHover{};
    bool highContrast=false, showClose=true;
    HWND hotWindow = nullptr;
    int hotTab = -1;
    POINT hotPoint{};
    int paintDpi = 96;
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
    struct TabSurface {
        BITMAPINFO info{};
        COLORREF inside{}, outside{};
        int diameter=0;
        std::vector<DWORD> pixels;

        // A 32-bit BI_RGB DIB stores the blue channel in the low byte.
        static DWORD blend(COLORREF fg, COLORREF bg, int coverage) {
            auto channel=[coverage](int a,int b) { return (coverage*a+(64-coverage)*b+32)/64; };
            return RGB(channel(GetBValue(fg),GetBValue(bg)),channel(GetGValue(fg),GetGValue(bg)),
                       channel(GetRValue(fg),GetRValue(bg)));
        }
        void paint(HDC dc, const RECT& r, int curve, COLORREF fg, COLORREF bg) {
            // Match the footprint of the original penless GDI RoundRect.
            int width=r.right-r.left-1, height=r.bottom-r.top-1;
            if (width<=0 || height<=0) return;
            curve=std::min({curve,width,height});
            auto& header=info.bmiHeader;
            if (header.biWidth!=width || header.biHeight!=-height || diameter!=curve || inside!=fg || outside!=bg) {
                header.biSize=sizeof(header);
                header.biWidth=width;
                header.biHeight=-height;
                header.biPlanes=1;
                header.biBitCount=32;
                diameter=curve;
                inside=fg;
                outside=bg;
                pixels.assign(size_t(width)*height,blend(fg,bg,64));
                // Sample one quadrant on an 8x8 grid and mirror it to all four corners.
                const int radius=curve*8;
                for (int y=0;y<(curve+1)/2;++y) {
                    for (int x=0;x<(curve+1)/2;++x) {
                        int coverage=0;
                        for (int sy=0;sy<8;++sy) {
                            int dy=std::max(0,radius-(16*y+2*sy+1));
                            for (int sx=0;sx<8;++sx) {
                                int dx=std::max(0,radius-(16*x+2*sx+1));
                                coverage+=dx*dx+dy*dy<=radius*radius;
                            }
                        }
                        DWORD color=blend(fg,bg,coverage);
                        int right=width-1-x, bottom=height-1-y;
                        pixels[y*width+x]=pixels[y*width+right]=color;
                        pixels[bottom*width+x]=pixels[bottom*width+right]=color;
                    }
                }
            }
            SetDIBitsToDevice(dc,r.left,r.top,width,height,0,0,0,height,pixels.data(),&info,DIB_RGB_COLORS);
        }
    };
    TabSurface selectedSurface, hoverSurface;
    static void line(HDC dc, int x, int y, int x2, int y2, COLORREF color) {
        SelectObject(dc, GetStockObject(DC_PEN));
        SetDCPenColor(dc,color);
        MoveToEx(dc,x,y,nullptr);
        LineTo(dc,x2,y2);
    }
    void draw(HWND w, HWND tabs, HDC dc, RECT bounds, RECT dirty, int dpi) {
        auto px = [dpi](int n) { return MulDiv(n,dpi,96); };
        fill(dc,dirty,background);
        SetBkMode(dc,TRANSPARENT);
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(w,WM_GETFONT,0,0));
        if (font) SelectObject(dc,font);
        bool focus = GetFocus() == w && !(SendMessageW(w,WM_QUERYUISTATE,0,0) & UISF_HIDEFOCUS);
        if (w != tabs) {
            RECT r = bounds;
            InflateRect(&r,-px(3),-px(3));
            // GDI's penless RoundRect paints through right-2 and bottom-2.
            // Give its painted bounds an odd pixel count to match the plus.
            r.right += (r.right-r.left) & 1;
            r.bottom += (r.bottom-r.top) & 1;
            if (hotWindow == w) rounded(dc,r,hover,px(8));
            int x = (r.left+r.right-2)/2, y = (r.top+r.bottom-2)/2, arm = px(6);
            COLORREF ink = IsWindowEnabled(w) ? text : edge;
            line(dc,x-arm,y,x+arm+1,y,ink);
            line(dc,x,y-arm,x,y+arm+1,ink);
            if (focus) DrawFocusRect(dc,&r);
            return;
        }
        int selected = TabCtrl_GetCurSel(tabs), count = TabCtrl_GetItemCount(tabs);
        int first=0, end=count;
        RECT hotItem{};
        if (hotWindow==tabs && hotTab>=0 && TabCtrl_GetItemRect(tabs,hotTab,&hotItem) &&
            dirty.left>=hotItem.left && dirty.right<=hotItem.right) {
            first=hotTab;
            end=hotTab+1;
        }
        for (int i=first; i<end; ++i) {
            RECT r{};
            if (!TabCtrl_GetItemRect(tabs,i,&r) || r.right <= dirty.left || r.left >= dirty.right) continue;
            bool active = i == selected, hot = w == hotWindow && i == hotTab;
            RECT close = closeRect(r,dpi,hot);
            r.top = px(2);
            r.bottom = bounds.bottom - px(1);
            COLORREF ink = active ? selectedText : text;
            if (active || hot) {
                auto& cached=active ? selectedSurface : hoverSurface;
                cached.paint(dc,r,px(10),active ? surface : hover,background);
            }
            if (!active && i+1 < count && i+1 != selected)
                line(dc,r.right-1,px(6),r.right-1,r.bottom-px(5),edge);
            wchar_t title[512]{};
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = title;
            item.cchTextMax = ARRAYSIZE(title);
            TabCtrl_GetItem(tabs,i,&item);
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
                if (hot && PtInRect(&close,hotPoint)) {
                    int radius=std::min({px(8),int(close.right-close.left-2)/2,int(r.bottom-r.top-2)/2});
                    if (radius>0) {
                        RECT tile{x-radius,y-radius,x+radius+2,y+radius+2};
                        rounded(dc,tile,highContrast ? ink : closeHover,px(5));
                        if (highContrast) ink=active ? surface : hover;
                    }
                }
                line(dc,x-arm,y-arm,x+arm+1,y+arm+1,ink);
                line(dc,x-arm,y+arm,x+arm+1,y-arm-1,ink);
            }
            if (focus && i == TabCtrl_GetCurFocus(tabs)) { InflateRect(&r,-px(3),-px(3)); DrawFocusRect(dc,&r); }
        }
    }
public:
    int loading=-1;
    bool darkMenu() const { return background==RGB(0,0,0) && surface==RGB(48,48,48); }
    void menuItem(const DRAWITEMSTRUCT& item,int dpi,HFONT font) const {
        auto* data=reinterpret_cast<const MSAAMENUINFO*>(item.itemData);
        if (!data) return;
        int saved=SaveDC(item.hDC);
        RECT r=item.rcItem;
        fill(item.hDC,r,item.itemState&ODS_SELECTED ? RGB(65,65,65) : RGB(43,43,43));
        r.left+=MulDiv(8,dpi,96);
        SelectObject(item.hDC,font ? font : GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(item.hDC,item.itemState&ODS_DISABLED ? GetSysColor(COLOR_GRAYTEXT) : text);
        SetBkMode(item.hDC,TRANSPARENT);
        DrawTextW(item.hDC,data->pszWText,-1,&r,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
        RestoreDC(item.hDC,saved);
    }
    RECT closeRect(RECT r, int dpi, bool hot) const {
        if (!showClose) { r.left=r.right; return r; }
        int width = MulDiv(r.right-r.left,96,dpi);
        int button = width >= 80 ? 23 : width >= 48 ? 18 : (hot && width >= 24 ? 11 : 0);
        r.left = r.right - MulDiv(button,dpi,96);
        return r;
    }
    RECT labelRect(RECT r, int dpi, bool hot) const {
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
        bool nextClose=(settings::load().options & settings::showClose)!=0;
        if (!force && bg == background && nextSurface == surface && nextClose==showClose) return;
        showClose=nextClose;
        background = bg;
        surface = nextSurface;
        highContrast=contrast;
        closeHover=dark ? RGB(72,72,72) : RGB(202,202,202);
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
        if (msg==WM_MOUSELEAVE && w!=hotWindow) return;
        HWND next = msg == WM_MOUSELEAVE ? nullptr : w;
        int index = -1;
        POINT point{short(LOWORD(lp)),short(HIWORD(lp))};
        if (next == tabs) {
            TCHITTESTINFO hit{};
            hit.pt = point;
            index = TabCtrl_HitTest(tabs,&hit);
        }
        if (next == hotWindow && index == hotTab) {
            RECT item{};
            if (index>=0 && TabCtrl_GetItemRect(tabs,index,&item)) {
                RECT close=closeRect(item,paintDpi,true);
                if (PtInRect(&close,hotPoint)!=PtInRect(&close,point)) InvalidateRect(tabs,&close,FALSE);
            }
            hotPoint=point;
            return;
        }
        if (next && next != hotWindow) {
            TRACKMOUSEEVENT event{sizeof(event),TME_LEAVE,next,0};
            TrackMouseEvent(&event);
        }
        auto invalidateHover=[tabs](HWND control,int tab) {
            if (!control) return;
            if (control!=tabs) { InvalidateRect(control,nullptr,FALSE); return; }
            RECT item{},strip{};
            if (tab>=0 && TabCtrl_GetItemRect(tabs,tab,&item)) {
                GetClientRect(tabs,&strip);
                item.top=strip.top;
                item.bottom=strip.bottom;
                InvalidateRect(tabs,&item,FALSE);
            }
        };
        invalidateHover(hotWindow,hotTab);
        hotWindow = next;
        hotTab = index;
        hotPoint=point;
        invalidateHover(next,index);
    }
    void paint(HWND w, HWND tabs, HDC target, int dpi, const RECT* update=nullptr) {
        paintDpi=dpi;
        RECT r{};
        GetClientRect(w,&r);
        RECT dirty=update ? *update : r;
        int width=dirty.right-dirty.left, height=dirty.bottom-dirty.top;
        if (width<=0 || height<=0) return;
        HDC buffer = CreateCompatibleDC(target);
        HBITMAP bitmap = buffer ? CreateCompatibleBitmap(target,width,height) : nullptr;
        if (bitmap) {
            HGDIOBJ old = SelectObject(buffer,bitmap);
            SetViewportOrgEx(buffer,-dirty.left,-dirty.top,nullptr);
            draw(w,tabs,buffer,r,dirty,dpi);
            SetViewportOrgEx(buffer,0,0,nullptr);
            BitBlt(target,dirty.left,dirty.top,width,height,buffer,0,0,SRCCOPY);
            SelectObject(buffer,old);
            DeleteObject(bitmap);
        } else {
            int saved = SaveDC(target);
            draw(w,tabs,target,r,dirty,dpi);
            RestoreDC(target,saved);
        }
        if (buffer) DeleteDC(buffer);
    }
    void erase(HDC dc, HWND bar) const { RECT r{}; GetClientRect(bar,&r); fill(dc,r,background); }
};
