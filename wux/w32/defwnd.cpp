// user32 shim, part 2: DefWindowProc, the nonclient area (borders, edges,
// scrollbars; captions belong to the window manager), control colours, the
// user API hook, and registration of the built-in classes.
#include <stdio.h>
#include <string.h>
#include <map>
#include <vector>
#include "user.h"
#include "sys/sys.h"
extern "C" {
#include "comctl32.h"
}

using namespace w32;

extern "C" {
LRESULT WINAPI DefDlgProcW(HWND, UINT, WPARAM, LPARAM);
BOOL WINAPI COMCTL32_DllMain(HINSTANCE, DWORD, LPVOID);
void BUTTON_Register(void); void COMBO_Register(void); void COMBOLBOX_Register(void);
void EDIT_Register(void); void LISTBOX_Register(void); void STATIC_Register(void);
}

// ---- user API hook --------------------------------------------------------

static LRESULT WINAPI default_defdlg(HWND h, UINT msg, WPARAM wp, LPARAM lp, BOOL) { return DefDlgProcW(h, msg, wp, lp); }
static void WINAPI default_ncbutton(HWND, HDC, enum NONCLIENT_BUTTON_TYPE, RECT, BOOL, BOOL) {}

static struct user_api_hook original_user_api = { default_defdlg, default_ncbutton, USER_ScrollBarDraw, USER_ScrollBarProc };
static struct user_api_hook hooked_user_api;
extern "C" struct user_api_hook* flxp_user_api = &original_user_api;

extern "C" BOOL WINAPI RegisterUserApiHook(const struct user_api_hook* nw, struct user_api_hook* old)
{
    if (!nw) return FALSE;
    hooked_user_api = *nw;
    flxp_user_api = &hooked_user_api;
    if (old) *old = original_user_api;
    return TRUE;
}

extern "C" void WINAPI UnregisterUserApiHook(void) { flxp_user_api = &original_user_api; }

// ---- nonclient geometry ---------------------------------------------------

#define HAS_DLGFRAME(style, ex) (((ex) & WS_EX_DLGMODALFRAME) || (((style) & WS_CAPTION) == WS_DLGFRAME))
#define HAS_THICKFRAME(style, ex) (((style) & WS_THICKFRAME) && !(((style) & (WS_DLGFRAME | WS_BORDER)) == WS_DLGFRAME))
#define HAS_THINFRAME(style) (((style) & WS_BORDER) || !((style) & (WS_CHILD | WS_POPUP)))
#define HAS_BIGFRAME(style, ex) (((style) & (WS_THICKFRAME | WS_DLGFRAME)) || ((ex) & WS_EX_DLGMODALFRAME))
#define HAS_STATICOUTERFRAME(style, ex) (((ex) & (WS_EX_STATICEDGE | WS_EX_DLGMODALFRAME)) == WS_EX_STATICEDGE)

// Frame drawn by us: everything for children and undecorated popups; for
// window-manager decorated toplevels only the inside parts (client edge,
// scrollbars).
static bool own_frame(win::Window* w) { return !w->wm_decorated; }

namespace w32 {

LRESULT nc_calc_size(HWND h, WPARAM wparam, RECT* r)
{
    win::Window* w = window_of(h);
    if (!r || !win::is_window(w)) return 0;
    LRESULT result = 0;
    LONG cls_style = (LONG)GetClassLongW(h, GCL_STYLE);
    LONG style = w->style, ex = w->ex_style;
    if (cls_style & CS_VREDRAW) result |= WVR_VREDRAW;
    if (cls_style & CS_HREDRAW) result |= WVR_HREDRAW;
    if (own_frame(w)) {
        RECT tmp = {0, 0, 0, 0};
        adjust_frame_rect(&tmp, style, FALSE, ex & ~WS_EX_CLIENTEDGE);
        r->left -= tmp.left; r->top -= tmp.top; r->right -= tmp.right; r->bottom -= tmp.bottom;
    }
    if (((style & (WS_CHILD | WS_POPUP)) != WS_CHILD) && GetMenu(h))
        r->top += MENU_GetMenuBarHeight(h, r->right - r->left, 0, 0);
    if (ex & WS_EX_CLIENTEDGE)
        if (r->right - r->left > 2 * GetSystemMetrics(SM_CXEDGE) && r->bottom - r->top > 2 * GetSystemMetrics(SM_CYEDGE))
            InflateRect(r, -GetSystemMetrics(SM_CXEDGE), -GetSystemMetrics(SM_CYEDGE));
    if (style & WS_VSCROLL)
        if (r->right - r->left >= GetSystemMetrics(SM_CXVSCROLL)) {
            if (!wparam && (ex & WS_EX_LAYOUTRTL)) ex ^= WS_EX_LEFTSCROLLBAR;
            if (ex & WS_EX_LEFTSCROLLBAR) r->left += GetSystemMetrics(SM_CXVSCROLL);
            else r->right -= GetSystemMetrics(SM_CXVSCROLL);
        }
    if (style & WS_HSCROLL)
        if (r->bottom - r->top > GetSystemMetrics(SM_CYHSCROLL)) r->bottom -= GetSystemMetrics(SM_CYHSCROLL);
    if (r->top > r->bottom) r->bottom = r->top;
    if (r->left > r->right) r->right = r->left;
    return result;
}


// ---- the window icon: WM_SETICON -> _NET_WM_ICON (flxp) ----
struct WindowIcons { HICON h[3] = {}; };
static std::map<HWND, WindowIcons> g_icons;

static void icon_pixels(HICON ic, std::vector<unsigned long>& out)
{
    ICONINFO ii = {};
    if (!ic || !GetIconInfo(ic, &ii)) return;
    BITMAP bm = {};
    HBITMAP src = ii.hbmColor ? ii.hbmColor : ii.hbmMask;
    if (src && GetObjectW(src, sizeof bm, &bm) && bm.bmWidth > 0 && bm.bmWidth <= 256) {
        int cx = bm.bmWidth, cy = ii.hbmColor ? bm.bmHeight : bm.bmHeight / 2;
        std::vector<unsigned int> px((size_t)cx * cy), mask((size_t)cx * cy);
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = cx; bi.bmiHeader.biHeight = -cy; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        HDC dc = GetDC(nullptr);
        bool color = ii.hbmColor && GetDIBits(dc, ii.hbmColor, 0, (UINT)cy, px.data(), &bi, DIB_RGB_COLORS);
        bool alpha = false;
        if (color) for (unsigned int p : px) if (p >> 24) { alpha = true; break; }
        if (color && !alpha && ii.hbmMask) GetDIBits(dc, ii.hbmMask, 0, (UINT)cy, mask.data(), &bi, DIB_RGB_COLORS);
        ReleaseDC(nullptr, dc);
        if (color) {
            out.push_back((unsigned long)cx);
            out.push_back((unsigned long)cy);
            for (size_t i = 0; i < px.size(); i++) {
                unsigned int p = px[i];
                if (!alpha) p = (mask[i] & 0xffffff) ? 0 : (p | 0xff000000u);   // the mask: white = transparent
                out.push_back((unsigned long)p);
            }
        }
    }
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
}

static void publish_icons(HWND h)
{
    std::vector<unsigned long> data;
    for (HICON ic : g_icons[h].h) icon_pixels(ic, data);
    sys::set_icon(sys::window_for_user((void*)h), data);
}

LRESULT nc_hit_test(HWND h, POINT pt)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return HTNOWHERE;
    RECT rect, rcClient;
    WIN_GetRectangles(h, COORDS_SCREEN, &rect, &rcClient);
    if (!PtInRect(&rect, pt)) return HTNOWHERE;
    LONG style = w->style, ex = w->ex_style;
    if (PtInRect(&rcClient, pt)) return HTCLIENT;
    if (own_frame(w)) {
        if (HAS_THICKFRAME(style, ex)) {
            InflateRect(&rect, -GetSystemMetrics(SM_CXFRAME), -GetSystemMetrics(SM_CYFRAME));
            if (!PtInRect(&rect, pt)) {
                if (pt.y < rect.top) {
                    if (pt.x < rect.left + GetSystemMetrics(SM_CXSIZE)) return HTTOPLEFT;
                    if (pt.x >= rect.right - GetSystemMetrics(SM_CXSIZE)) return HTTOPRIGHT;
                    return HTTOP;
                }
                if (pt.y >= rect.bottom) {
                    if (pt.x < rect.left + GetSystemMetrics(SM_CXSIZE)) return HTBOTTOMLEFT;
                    if (pt.x >= rect.right - GetSystemMetrics(SM_CXSIZE)) return HTBOTTOMRIGHT;
                    return HTBOTTOM;
                }
                if (pt.x < rect.left) {
                    if (pt.y < rect.top + GetSystemMetrics(SM_CYSIZE)) return HTTOPLEFT;
                    if (pt.y >= rect.bottom - GetSystemMetrics(SM_CYSIZE)) return HTBOTTOMLEFT;
                    return HTLEFT;
                }
                if (pt.x >= rect.right) {
                    if (pt.y < rect.top + GetSystemMetrics(SM_CYSIZE)) return HTTOPRIGHT;
                    if (pt.y >= rect.bottom - GetSystemMetrics(SM_CYSIZE)) return HTBOTTOMRIGHT;
                    return HTRIGHT;
                }
            }
        } else {
            if (HAS_DLGFRAME(style, ex)) InflateRect(&rect, -GetSystemMetrics(SM_CXDLGFRAME), -GetSystemMetrics(SM_CYDLGFRAME));
            else if (HAS_THINFRAME(style)) InflateRect(&rect, -GetSystemMetrics(SM_CXBORDER), -GetSystemMetrics(SM_CYBORDER));
            if (!PtInRect(&rect, pt)) return HTBORDER;
        }
    }
    if (HAS_MENU(h, style) && pt.y < rcClient.top && pt.y >= rcClient.top - GetSystemMetrics(SM_CYMENU) - 1 &&
        pt.x >= rcClient.left && pt.x < rcClient.right)
        return HTMENU;
    // scroll bars
    if (ex & WS_EX_LAYOUTRTL) ex ^= WS_EX_LEFTSCROLLBAR;
    if (style & WS_VSCROLL) {
        if (ex & WS_EX_LEFTSCROLLBAR) rcClient.left -= GetSystemMetrics(SM_CXVSCROLL);
        else rcClient.right += GetSystemMetrics(SM_CXVSCROLL);
        if (PtInRect(&rcClient, pt)) return HTVSCROLL;
    }
    if (style & WS_HSCROLL) {
        rcClient.bottom += GetSystemMetrics(SM_CYHSCROLL);
        if (PtInRect(&rcClient, pt)) {
            if ((style & WS_VSCROLL) &&
                ((((ex & WS_EX_LEFTSCROLLBAR) != 0) && (pt.x <= rcClient.left + GetSystemMetrics(SM_CXVSCROLL))) ||
                 (((ex & WS_EX_LEFTSCROLLBAR) == 0) && (pt.x >= rcClient.right - GetSystemMetrics(SM_CXVSCROLL)))))
                return HTSIZE;
            return HTHSCROLL;
        }
    }
    return HTNOWHERE;
}

// Thin and dialog frames (Wine's NC_DrawFrame).
static void draw_frame(HDC hdc, RECT* rect, BOOL active, DWORD style, DWORD ex)
{
    INT width, height;
    if (style & WS_THICKFRAME) {
        width = GetSystemMetrics(SM_CXFRAME) - GetSystemMetrics(SM_CXDLGFRAME);
        height = GetSystemMetrics(SM_CYFRAME) - GetSystemMetrics(SM_CYDLGFRAME);
    } else {
        width = GetSystemMetrics(SM_CXDLGFRAME) - GetSystemMetrics(SM_CXEDGE);
        height = GetSystemMetrics(SM_CYDLGFRAME) - GetSystemMetrics(SM_CYEDGE);
    }
    if (style & WS_THICKFRAME) SelectObject(hdc, GetSysColorBrush(active ? COLOR_ACTIVEBORDER : COLOR_INACTIVEBORDER));
    else SelectObject(hdc, GetSysColorBrush(active ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION));
    if (HAS_THICKFRAME(style, ex) || HAS_DLGFRAME(style, ex)) {
        PatBlt(hdc, rect->left, rect->top, rect->right - rect->left, height, PATCOPY);
        PatBlt(hdc, rect->left, rect->top, width, rect->bottom - rect->top, PATCOPY);
        PatBlt(hdc, rect->left, rect->bottom - 1, rect->right - rect->left, -height, PATCOPY);
        PatBlt(hdc, rect->right - 1, rect->top, -width, rect->bottom - rect->top, PATCOPY);
        InflateRect(rect, -width, -height);
    } else if (HAS_THINFRAME(style)) {
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, rect->left, rect->top, rect->right, rect->bottom);
        InflateRect(rect, -1, -1);
    }
}

extern "C" void SCROLL_DrawNCScrollBar(HWND hwnd, HDC hdc, BOOL draw_horizontal, BOOL draw_vertical);

void nc_paint(HWND h, HRGN clip)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w) || !(w->style & WS_VISIBLE)) return;
    DWORD style = w->style, ex = w->ex_style;
    bool active = true;
    RECT rectClient;
    WIN_GetRectangles(h, COORDS_SCREEN, nullptr, &rectClient);
    HRGN hrgn = CreateRectRgnIndirect(&rectClient);
    HDC hdc;
    if (clip > (HRGN)1) {
        CombineRgn(hrgn, clip, hrgn, RGN_DIFF);
        hdc = GetDCEx(h, hrgn, DCX_USESTYLE | DCX_WINDOW | DCX_INTERSECTRGN);
    } else {
        hdc = GetDCEx(h, hrgn, DCX_USESTYLE | DCX_WINDOW | DCX_EXCLUDERGN);
    }
    DeleteObject(hrgn);
    if (!hdc) return;
    RECT rect;
    WIN_GetRectangles(h, COORDS_WINDOW, &rect, nullptr);
    SelectObject(hdc, SYSCOLOR_GetPen(COLOR_WINDOWFRAME));
    if (own_frame(w)) {
        if (HAS_STATICOUTERFRAME(style, ex)) DrawEdge(hdc, &rect, BDR_SUNKENOUTER, BF_RECT | BF_ADJUST);
        else if (HAS_BIGFRAME(style, ex)) DrawEdge(hdc, &rect, EDGE_RAISED, BF_RECT | BF_ADJUST);
        draw_frame(hdc, &rect, active, style, ex);
    }
    if (HAS_MENU(h, style)) {
        RECT r = rect;
        r.bottom = rect.top + GetSystemMetrics(SM_CYMENU);
        rect.top += MENU_DrawMenuBar(hdc, &r, h) + 1;
    }
    if (ex & WS_EX_CLIENTEDGE) DrawEdge(hdc, &rect, EDGE_SUNKEN, BF_RECT | BF_ADJUST);
    SCROLL_DrawNCScrollBar(h, hdc, style & WS_HSCROLL, style & WS_VSCROLL);
    if ((style & WS_VSCROLL) && (style & WS_HSCROLL)) {
        RECT r = rect;
        if (ex & WS_EX_LEFTSCROLLBAR) r.right = r.left + GetSystemMetrics(SM_CXVSCROLL) + 1;
        else r.left = r.right - GetSystemMetrics(SM_CXVSCROLL) + 1;
        r.top = r.bottom - GetSystemMetrics(SM_CYHSCROLL) + 1;
        FillRect(hdc, &r, GetSysColorBrush(COLOR_SCROLLBAR));
    }
    ReleaseDC(h, hdc);
}

} // namespace w32

// ---- control colours ------------------------------------------------------

extern "C" HBRUSH DEFWND_ControlColor(HDC hdc, UINT ctl)
{
    if (ctl == CTLCOLOR_SCROLLBAR) {
        HBRUSH hb = GetSysColorBrush(COLOR_SCROLLBAR);
        COLORREF bk = GetSysColor(COLOR_3DHILIGHT);
        SetTextColor(hdc, GetSysColor(COLOR_3DFACE));
        SetBkColor(hdc, bk);
        if (bk == RGB(255, 255, 255)) {
            SetBkColor(hdc, GetSysColor(COLOR_3DFACE));
            return SYSCOLOR_Get55AABrush();
        }
        return hb;
    }
    SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
    if (ctl == CTLCOLOR_EDIT || ctl == CTLCOLOR_LISTBOX) SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
    else { SetBkColor(hdc, GetSysColor(COLOR_3DFACE)); return GetSysColorBrush(COLOR_3DFACE); }
    return GetSysColorBrush(COLOR_WINDOW);
}

// ---- DefWindowProc --------------------------------------------------------

extern "C" void SCROLL_TrackScrollBar(HWND hwnd, INT scrollbar, POINT pt);

static void set_text_utf16(win::Window* w, LPCWSTR s)
{
    w->text = s ? utf8_of(s) : std::string();
    win::set_text(w, w->text.c_str());   // lets the win layer update the native title
}

static LRESULT get_text_utf16(win::Window* w, WPARAM n, LPWSTR buf)
{
    if (!buf || n == 0) return 0;
    std::u16string s = utf16_of(w->text.c_str());
    size_t len = s.size() < n - 1 ? s.size() : n - 1;
    memcpy(buf, s.data(), len * 2);
    buf[len] = 0;
    return (LRESULT)len;
}

#define KEYDATA_ALT 0x2000
static int g_f10_key, g_menu_sys_key;

extern "C" LRESULT NC_HandleSysCommand(HWND h, WPARAM wp, LPARAM lp)
{
    POINT pt = { (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
    switch (wp & 0xfff0) {
    case SC_VSCROLL: SCROLL_TrackScrollBar(h, SB_VERT, pt); break;
    case SC_HSCROLL: SCROLL_TrackScrollBar(h, SB_HORZ, pt); break;
    case SC_MOUSEMENU: MENU_TrackMouseMenuBar(h, wp & 0x000F, pt); break;
    case SC_KEYMENU: MENU_TrackKbdMenuBar(h, (UINT)wp, (WCHAR)lp); break;
    case SC_CLOSE: return SendMessageW(h, WM_CLOSE, 0, 0);
    // Window-manager framed toplevels: the WM runs the move/size loop
    // (_NET_WM_MOVERESIZE) and maximises (_NET_WM_STATE); a client area that
    // answers HTCAPTION/HTLEFT/... to WM_NCHITTEST gets here too.
    case SC_MOVE: {
        win::Window* w = window_of(h);
        if (win::is_window(w) && w->top && w->wm_decorated)
            win::begin_move_resize(w, (wp & 0x000f) ? (intptr_t)HTCAPTION : win::MOVE_KEYBOARD);
        break;
    }
    case SC_SIZE: {
        win::Window* w = window_of(h);
        UINT side = wp & 0x000f;    // WMSZ_LEFT..WMSZ_BOTTOMRIGHT, 0 = keyboard
        if (win::is_window(w) && w->top && w->wm_decorated)
            win::begin_move_resize(w, side ? (intptr_t)(side - WMSZ_LEFT + HTLEFT) : win::SIZE_KEYBOARD);
        break;
    }
    case SC_MAXIMIZE:
    case SC_RESTORE: {
        win::Window* w = window_of(h);
        if (win::is_window(w) && w->top && w->wm_decorated)
            win::set_maximized(w, (wp & 0xfff0) == SC_MAXIMIZE ? 1 : 0);
        break;
    }
    }
    return 0;
}

extern "C" LRESULT WINAPI DefWindowProcW(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return 0;
    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        if (cs && cs->lpszName && ((ULONG_PTR)cs->lpszName >> 16)) w->text = utf8_of(cs->lpszName);
        return TRUE;
    }
    case WM_NCCALCSIZE:
        return nc_calc_size(h, wp, (RECT*)lp);
    case WM_NCHITTEST: {
        POINT pt = { (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
        return nc_hit_test(h, pt);
    }
    case WM_NCPAINT:
        nc_paint(h, (HRGN)wp);
        return 0;
    case WM_NCACTIVATE:
        return TRUE;
    case WM_NCLBUTTONDOWN:
        switch (wp) {
        case HTVSCROLL: SendMessageW(h, WM_SYSCOMMAND, SC_VSCROLL + HTVSCROLL, lp); break;
        case HTHSCROLL: SendMessageW(h, WM_SYSCOMMAND, SC_HSCROLL + HTHSCROLL, lp); break;
        case HTMENU: SendMessageW(h, WM_SYSCOMMAND, SC_MOUSEMENU + HTMENU, lp); break;
        case HTCAPTION: if (w->top && w->wm_decorated) SendMessageW(h, WM_SYSCOMMAND, SC_MOVE + HTCAPTION, lp); break;
        case HTLEFT: case HTRIGHT: case HTTOP: case HTTOPLEFT: case HTTOPRIGHT:
        case HTBOTTOM: case HTBOTTOMLEFT: case HTBOTTOMRIGHT:
            // as Wine's NC_HandleNCLButtonDown: SC_SIZE + WMSZ_*
            if (w->top && w->wm_decorated) SendMessageW(h, WM_SYSCOMMAND, SC_SIZE + wp - (HTLEFT - WMSZ_LEFT), lp);
            break;
        }
        return 0;
    case WM_NCLBUTTONDBLCLK:
        if (wp == HTCAPTION && w->top && w->wm_decorated) {
            // NC_HandleNCLButtonDblClk
            if (w->style & WS_MAXIMIZEBOX)
                SendMessageW(h, WM_SYSCOMMAND, win::is_maximized(w) ? SC_RESTORE : SC_MAXIMIZE, lp);
            return 0;
        }
        return DefWindowProcW(h, WM_NCLBUTTONDOWN, wp, lp);
    case WM_SYSCOMMAND:
        return NC_HandleSysCommand(h, wp, lp);
    case WM_KEYDOWN:
        if (wp == VK_F10) g_f10_key = VK_F10;
        return 0;
    case WM_SYSKEYDOWN:
        // XP: the first Alt press shows the window's keyboard cues (WM_CHANGEUISTATE to the top level, which tells
        // every child); they stay for that window
        if ((wp == VK_MENU || wp == VK_LMENU || wp == VK_RMENU) && !(lp & 0x40000000))
            SendMessageW(GetAncestor(h, GA_ROOT), WM_CHANGEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEACCEL), 0);
        if (HIWORD(lp) & KEYDATA_ALT) {
            if ((wp == VK_MENU || wp == VK_LMENU || wp == VK_RMENU) && !g_menu_sys_key) g_menu_sys_key = 1;
            else g_menu_sys_key = 0;
            g_f10_key = 0;
            if (wp == VK_F4) {
                HWND top = GetAncestor(h, GA_ROOT);
                if (!(GetClassLongW(top, GCL_STYLE) & CS_NOCLOSE)) PostMessageW(top, WM_SYSCOMMAND, SC_CLOSE, 0);
            }
        } else if (wp == VK_F10) {
            if (GetKeyState(VK_SHIFT) & 0x8000) SendMessageW(h, WM_CONTEXTMENU, (WPARAM)h, (LPARAM)-1);
            g_f10_key = 1;
        } else if (wp == VK_ESCAPE && (GetKeyState(VK_SHIFT) & 0x8000)) {
            SendMessageW(h, WM_SYSCOMMAND, SC_KEYMENU, ' ');
        }
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (((wp == VK_MENU || wp == VK_LMENU || wp == VK_RMENU) && g_menu_sys_key) || (wp == VK_F10 && g_f10_key))
            SendMessageW(GetAncestor(h, GA_ROOT), WM_SYSCOMMAND, SC_KEYMENU, 0);
        g_menu_sys_key = g_f10_key = 0;
        return 0;
    case WM_SYSCHAR:
        g_menu_sys_key = 0;
        if ((HIWORD(lp) & KEYDATA_ALT) && wp) {
            if (wp == '\t' || wp == '\x1b') return 0;
            if (wp == ' ' && (w->style & WS_CHILD)) SendMessageW(GetParent(h), msg, wp, lp);
            // as for Alt alone (WM_SYSKEYUP above) the toplevel gets it: user32 lets the child's
            // MENU_TrackKbdMenuBar walk up to it, our menu band lives in the frame's WM_SYSCOMMAND
            else SendMessageW(GetAncestor(h, GA_ROOT), WM_SYSCOMMAND, SC_KEYMENU, wp);
        }
        return 0;
    case WM_MENUCHAR:
        return MAKELRESULT(0, MNC_CLOSE);
    // the keyboard cues (XP's UI state, UISF_HIDEACCEL / UISF_HIDEFOCUS): hidden while SPI_GETKEYBOARDCUES is off, until
    // the keyboard is used. A child hands WM_CHANGEUISTATE to its parent; the top level applies it and tells its children
    case WM_CHANGEUISTATE: {
        UINT action = LOWORD(wp), flags = HIWORD(wp) & (UISF_HIDEACCEL | UISF_HIDEFOCUS);
        if (action != UIS_SET && action != UIS_CLEAR && action != UIS_INITIALIZE) return 0;
        if ((w->style & WS_CHILD) && GetParent(h)) return SendMessageW(GetParent(h), msg, wp, lp);
        Window* d = wdata(h);
        if (!d) return 0;
        if (action == UIS_INITIALIZE) {
            action = win::last_input_keyboard() ? UIS_CLEAR : UIS_SET;
            flags = UISF_HIDEACCEL | UISF_HIDEFOCUS;
        }
        if (win::keyboard_cues() && action == UIS_SET) flags &= ~UISF_HIDEACCEL;
        UINT next = action == UIS_SET ? d->uistate | flags : d->uistate & ~flags;
        if (next != d->uistate) SendMessageW(h, WM_UPDATEUISTATE, MAKEWPARAM(action, flags), 0);
        return 0;
    }
    case WM_UPDATEUISTATE: {
        UINT action = LOWORD(wp), flags = HIWORD(wp) & (UISF_HIDEACCEL | UISF_HIDEFOCUS);
        if (Window* d = wdata(h)) {
            if (action == UIS_SET) d->uistate |= flags;
            else if (action == UIS_CLEAR) d->uistate &= ~flags;
        }
        for (HWND c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) SendMessageW(c, WM_UPDATEUISTATE, wp, lp);
        return 0;
    }
    case WM_QUERYUISTATE: {
        Window* d = wdata(h);
        return d ? d->uistate : 0;
    }
    case WM_RBUTTONUP: {
        POINT pt = { (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
        ClientToScreen(h, &pt);
        SendMessageW(h, WM_CONTEXTMENU, (WPARAM)h, MAKELPARAM(pt.x, pt.y));
        return 0;
    }
    case WM_NCRBUTTONUP:
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
    case WM_ICONERASEBKGND: {
        RECT rect;
        HDC hdc = (HDC)wp;
        HBRUSH hbr = (HBRUSH)GetClassLongPtrW(h, GCLP_HBRBACKGROUND);
        if (!hbr) return 0;
        if (GetClassLongW(h, GCL_STYLE) & CS_PARENTDC) {
            GetClientRect(h, &rect);
            DPtoLP(hdc, (LPPOINT)&rect, 2);
        } else {
            GetClipBox(hdc, &rect);
        }
        FillRect(hdc, &rect, hbr);
        return 1;
    }
    case WM_CTLCOLORMSGBOX: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG: case WM_CTLCOLORSCROLLBAR: case WM_CTLCOLORSTATIC:
        return (LRESULT)DEFWND_ControlColor((HDC)wp, msg - WM_CTLCOLORMSGBOX);
    case WM_SETTEXT:
        set_text_utf16(w, (LPCWSTR)lp);
        if (w->parent && own_frame(w) && (w->style & WS_CAPTION) == WS_CAPTION) win::invalidate_frame(w);
        return 1;
    case WM_GETTEXT:
        return get_text_utf16(w, wp, (LPWSTR)lp);
    case WM_GETTEXTLENGTH:
        return (LRESULT)utf16_of(w->text.c_str()).size();
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_WINDOWPOSCHANGED: {
        const WINDOWPOS* p = (const WINDOWPOS*)lp;
        if (!(p->flags & SWP_NOCLIENTSIZE) && !(p->flags & SWP_NOSIZE))
            SendMessageW(h, WM_SIZE, SIZE_RESTORED, MAKELONG(w->client.width(), w->client.height()));
        if (!(p->flags & SWP_NOCLIENTMOVE) && !(p->flags & SWP_NOMOVE))
            SendMessageW(h, WM_MOVE, 0, MAKELONG(w->rect.left + w->client.left, w->rect.top + w->client.top));
        return 0;
    }
    case WM_WINDOWPOSCHANGING:
        return 0;
    case WM_SETCURSOR: {
        if (w->parent && !(w->style & WS_POPUP)) {
            if (SendMessageW(h ? hwnd_of(w->parent) : nullptr, WM_SETCURSOR, wp, lp)) return TRUE;
        }
        HCURSOR cur = (HCURSOR)GetClassLongPtrW(h, GCLP_HCURSOR);
        switch ((SHORT)LOWORD(lp)) {
        case HTLEFT: case HTRIGHT: cur = LoadCursorW(nullptr, (LPCWSTR)IDC_SIZEWE); break;
        case HTTOP: case HTBOTTOM: cur = LoadCursorW(nullptr, (LPCWSTR)IDC_SIZENS); break;
        case HTTOPLEFT: case HTBOTTOMRIGHT: cur = LoadCursorW(nullptr, (LPCWSTR)IDC_SIZENWSE); break;
        case HTTOPRIGHT: case HTBOTTOMLEFT: cur = LoadCursorW(nullptr, (LPCWSTR)IDC_SIZENESW); break;
        case HTCLIENT: break;
        default: cur = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW); break;
        }
        if (!cur) cur = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        SetCursor(cur);
        return TRUE;
    }
    case WM_MOUSEACTIVATE:
        if (w->parent && !(w->style & WS_POPUP)) {
            LRESULT r = SendMessageW(hwnd_of(w->parent), WM_MOUSEACTIVATE, wp, lp);
            if (r) return r;
        }
        return (LOWORD(lp) >= HTCLIENT) ? MA_ACTIVATE : MA_NOACTIVATE;
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE && !GetFocus()) SetFocus(h);
        return 0;
    case WM_SETREDRAW:
        if (wp) WIN_SetStyle(h, WS_VISIBLE, 0);
        else { RedrawWindow(h, nullptr, nullptr, RDW_ALLCHILDREN | RDW_VALIDATE); WIN_SetStyle(h, 0, WS_VISIBLE); }
        return 0;
    case WM_SHOWWINDOW:
        return 0;
    case WM_CANCELMODE:
        // Wine's DefWindowProc: MENU_EndMenu ends tracking when h is the menu's
        // popup or its owner (the old test only matched the popup)
        if (MENU_IsMenuActive()) MENU_EndMenu(h);
        if (GetCapture() == h) ReleaseCapture();
        return 0;
    case WM_HELP:
    case WM_CONTEXTMENU:
    case WM_APPCOMMAND:
        if (w->parent) return SendMessageW(hwnd_of(w->parent), msg, wp, lp);
        return 0;
    case WM_NOTIFYFORMAT:
        return NFR_UNICODE;
    case WM_QUERYOPEN:
    case WM_QUERYENDSESSION:
        return 1;
    case WM_GETDLGCODE:
        return 0;
    case WM_PRINT:
        if ((lp & PRF_CHECKVISIBLE) && !IsWindowVisible(h)) return 0;
        if (lp & PRF_CLIENT) SendMessageW(h, WM_PRINTCLIENT, wp, lp);
        return 0;
    case WM_PRINTCLIENT:
        return 0;
    case WM_GETICON:
        return wp <= 2 ? (LRESULT)g_icons[h].h[wp] : 0;
    case WM_SETICON: {
        // flxp: a toplevel's icons (ICON_SMALL, ICON_BIG, ICON_SMALL2) become its
        // _NET_WM_ICON, every size at once, for the window manager and the taskbar
        if (wp > 2) return 0;
        HICON old = g_icons[h].h[wp];
        g_icons[h].h[wp] = (HICON)lp;
        if (!w->parent) publish_icons(h);
        return (LRESULT)old;
    }
    case WM_MOUSEWHEEL:
    case win::WM_FLXP_DRAGOVER:     // drops: the frame answers for its controls
    case win::WM_FLXP_DRAGLEAVE:
    case win::WM_FLXP_DROP:
        if (w->parent) return SendMessageW(hwnd_of(w->parent), msg, wp, lp);
        return 0;
    default:
        return 0;
    }
}

extern "C" LRESULT WINAPI DefWindowProcA(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_SETTEXT) {
        std::u16string s = lp ? utf16_of((const char*)lp) : std::u16string();
        return DefWindowProcW(h, msg, wp, (LPARAM)s.c_str());
    }
    if (msg == WM_GETTEXT) {
        win::Window* w = window_of(h);
        if (!win::is_window(w) || !lp || !wp) return 0;
        char* buf = (char*)lp;
        size_t len = w->text.size() < wp - 1 ? w->text.size() : wp - 1;
        memcpy(buf, w->text.data(), len); buf[len] = 0;
        return (LRESULT)len;
    }
    if (msg == WM_GETTEXTLENGTH) { win::Window* w = window_of(h); return win::is_window(w) ? (LRESULT)w->text.size() : 0; }
    return DefWindowProcW(h, msg, wp, lp);
}

// ---- built-in classes -----------------------------------------------------

static LRESULT WINAPI dialog_class_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    return flxp_user_api->pDefDlgProc(h, msg, wp, lp, TRUE);
}

static LRESULT WINAPI popupmenu_class_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    return PopupMenuWndProc(h, msg, wp, lp);
}

static LRESULT WINAPI scrollbar_class_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    return ScrollBarWndProc_common(h, msg, wp, lp, TRUE);
}

static void register_builtin(const struct builtin_class_descr* d, WNDPROC proc)
{
    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.style = d->style;
    wc.lpfnWndProc = proc;
    wc.cbWndExtra = d->extra;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)d->cursor);
    wc.hbrBackground = d->brush;
    wc.lpszClassName = d->name;
    RegisterClassW(&wc);
}

namespace w32 {

void register_builtin_classes()
{
    register_builtin(&DIALOG_builtin_class, dialog_class_proc);
    register_builtin(&SCROLL_builtin_class, scrollbar_class_proc);
    register_builtin(&POPUPMENU_builtin_class, popupmenu_class_proc);
    COMCTL32_DllMain((HINSTANCE)comctl32_module(), DLL_PROCESS_ATTACH, nullptr);   // registers the common controls
    BUTTON_Register();
    STATIC_Register();
    EDIT_Register();
    LISTBOX_Register();
    COMBOLBOX_Register();
    // Wine's ComboLBox has Vista's CS_DROPSHADOW; XP's drop-down lists have no shadow (only the menu class and comctl32 v6's
    // tooltips ask for one there)
    if (Class* c = find_class(L"ComboLBox")) c->style &= ~CS_DROPSHADOW;
    COMBO_Register();
}

} // namespace w32
