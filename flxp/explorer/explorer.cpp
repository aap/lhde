// flxp explorer: a re-interpretation of the Windows 2000/XP Explorer frame on
// this toolkit. Rebar with the standard toolbar and address band, the Folders
// tree, the file list, a status bar, menus, history. Browses shell::Folder.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "shell/namespace.h"
#include "shell/icons.h"
#include "shell/apps.h"
#include "shell/bookmarks.h"
#include "shell/config.h"
#include "shell/settings.h"
#include "shell/fileops.h"
#include "shell/mime.h"
#include "shell/runtime.h"
#include "shell/lhdata.h"
#include "shell/lhtheme.h"
#include "shell/details.h"
#include "shell/thumbs.h"
#include "commands.h"
#include "dialogs.h"
#include "fileopui.h"
#include "header.h"
#include "lhbar.h"
#include "thumbview.h"
#include "viewbutton.h"
#include "places.h"
#include "taskpane.h"
#include "uiicons.h"
#include "verbs.h"
#include "w32/w32.h"
#include "w32/user.h"
#include "menuband.h"
#include "uxtheme.h"
#include "vssym32.h"
extern "C" {
#include "commctrl.h"
#include "w32/flxp_dwmapi.h"
#include "w32/flxp_shell.h"
#undef small      // rpcndr.h (through dwmapi.h)
#ifndef TB_SETDROPDOWNGAP
#define TB_SETDROPDOWNGAP (WM_USER + 100)   // comctrlp.h (XP private)
#endif
}
#include "browser.h"

using shell::Item;

// The frame around the shared browsing widget (browser.h: the list, its
// views, filter and history live in browser::Browser).
struct Explorer : browser::Browser {
    HMENU menu = nullptr;
    HWND bandsite = nullptr;
    HWND frame = nullptr, rebar = nullptr, toolbar = nullptr, address = nullptr, tree = nullptr, status = nullptr;
    HFONT font = nullptr;                            // DEFAULT_GUI_FONT: the address bar
    HFONT item_font = nullptr, status_font = nullptr;   // the style's ICONTITLEFONT (tree, list) and STATUSFONT (fonts_changed)
    bool longhorn = false;                           // frame=longhorn: the 4074 header on lhwm's glass
    bool show_folders = false;        // folders= / View > Explorer Bar > Folders (remembered); off = the places pane
    bool show_status = true;
    bool folders_once = false;        // the Explore verb opens the tree for this window only
    bool show_taskpane = false;       // taskpane= / --taskpane / View > Task Band (Longhorn only)
    HWND taskband = nullptr;
    int tree_width = 220;   // XP's default Folders bar width
    bool syncing_tree = false;
    std::list<std::string> tree_paths;       // stable storage for tree item lParams
    HTREEITEM tree_root = nullptr;
    bool dragging_splitter = false;
    bool large_toolbar = true;
    HMENU bookmarks_menu = nullptr, file_menu = nullptr, edit_menu = nullptr;
    std::vector<shell::Bookmark> bookmarks;          // as last shown in the Bookmarks menu
    bool focus_active = true;                        // WM_ACTIVATE: the window's own state
    UINT w_active = 0;                               // the root _NET_ACTIVE_WINDOW watch
    UINT w_settings = 0;                             // _LH_SETTINGS_CHANGED (per-theme icons, frame)

    int icon(const std::string& key) override;
    HIMAGELIST icons_at(int px) override;
    bool ghosted(const Item& it) override;
    void filled() override;
    void shown() override;
    void view_changed() override;
};

static Explorer g;

static std::u16string W(const std::string& s) { return w32::utf16_of(s.c_str()); }
#define WS(s) ((LPCWSTR)W(s).c_str())

// ---- icons ------------------------------------------------------------------

// Icon sources and image lists are shared with the desktop (uiicons).
static HANDLE load_themed(const std::string& src, UINT type, int cx, int cy, UINT extra) { return uiicons::load_themed(src, type, cx, cy, extra); }
static int icon_for(const std::string& key) { return uiicons::index(key); }

// Toolbar images from the theme's strip, or comctl32's own if it has none.
static void layout();

static HIMAGELIST strip_list(const std::string& src, int size)
{
    HBITMAP strip = (HBITMAP)load_themed(src, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    if (!strip) return nullptr;
    // A strip with a real alpha channel is added as is; anything else is colour-keyed
    // on its top-left pixel. (DIB sections always report 32bpp here, so look at the bits.)
    DIBSECTION ds = {};
    bool has_alpha = false;
    if (GetObjectW(strip, sizeof ds, &ds) == sizeof ds && ds.dsBm.bmBits && ds.dsBm.bmBitsPixel == 32) {
        const uint8_t* p = (const uint8_t*)ds.dsBm.bmBits;
        size_t n = (size_t)ds.dsBm.bmWidth * (size_t)(ds.dsBmih.biHeight < 0 ? -ds.dsBmih.biHeight : ds.dsBmih.biHeight);
        for (size_t i = 0; i < n && !has_alpha; i++) if (p[i * 4 + 3] != 0xff) has_alpha = true;
    }
    HIMAGELIST il = ImageList_Create(size, size, (has_alpha ? ILC_COLOR32 : ILC_COLOR24) | ILC_MASK, 48, 8);
    if (has_alpha) ImageList_Add(il, strip, nullptr);
    else ImageList_AddMasked(il, strip, CLR_DEFAULT);
    DeleteObject(strip);
    return il;
}

static bool apply_toolbar_images()
{
    const shell::ToolbarTheme& tb = shell::icon_theme().toolbar;
    if (!tb.valid()) return false;
    bool small = !g.large_toolbar && !tb.small_strip.empty();
    int size = small ? tb.small_size : tb.size;
    HIMAGELIST normal = strip_list(small ? tb.small_strip : tb.strip, size);
    if (!normal) return false;
    HIMAGELIST hot = strip_list(small ? tb.small_hot_strip : tb.hot_strip, size);
    HIMAGELIST old = (HIMAGELIST)SendMessageW(g.toolbar, TB_SETIMAGELIST, 0, (LPARAM)normal);
    HIMAGELIST oldhot = (HIMAGELIST)SendMessageW(g.toolbar, TB_SETHOTIMAGELIST, 0, (LPARAM)hot);
    SendMessageW(g.toolbar, TB_SETBITMAPSIZE, 0, MAKELONG(size, size));
    if (old) ImageList_Destroy(old);
    if (oldhot) ImageList_Destroy(oldhot);
    return true;
}

static HMENU g_xbar_menu, g_arrange_menu, g_bars_menu, g_zoom_menu;   // View's submenus
static HMENU g_icon_menu;   // View > Toolbars > Icon Options (the XP frame's)

static void set_toolbar_size(bool large)
{
    g.large_toolbar = large;
    if (!apply_toolbar_images()) return;
    SendMessageW(g.toolbar, TB_AUTOSIZE, 0, 0);
    SIZE sz = {0, 0};
    SendMessageW(g.toolbar, TB_GETMAXSIZE, 0, (LPARAM)&sz);
    RECT item = {0, 0, 0, 0};
    SendMessageW(g.toolbar, TB_GETITEMRECT, 0, (LPARAM)&item);
    REBARBANDINFOW rb = {};
    rb.cbSize = sizeof rb;
    rb.fMask = RBBIM_CHILDSIZE;
    rb.cxMinChild = (UINT)sz.cx; rb.cyMinChild = (UINT)(item.bottom - item.top); rb.cyChild = rb.cyMinChild;
    int band = (int)SendMessageW(g.rebar, RB_IDTOINDEX, IDC_TOOLBAR, 0);
    SendMessageW(g.rebar, RB_SETBANDINFOW, (WPARAM)band, (LPARAM)&rb);
    if (g_icon_menu) CheckMenuRadioItem(g_icon_menu, ID_TB_LARGE, ID_TB_SMALL, large ? ID_TB_LARGE : ID_TB_SMALL, MF_BYCOMMAND);
    layout();
    InvalidateRect(g.rebar, nullptr, TRUE);
}

static bool load_toolbar_theme() { return apply_toolbar_images(); }

// The icon set changed (settings.ini / the theme's, per theme): the image lists
// are reloaded in place, the toolbar strip and its indices are the new set's
static void icons_changed()
{
    if (!uiicons::sync()) return;
    if (apply_toolbar_images()) {
        const shell::ToolbarTheme& t = shell::icon_theme().toolbar;
        const int ids[][2] = { { ID_BACK, t.back }, { ID_FORWARD, t.forward }, { ID_UP, t.up }, { ID_SEARCH, t.search },
                               { ID_FOLDERS, t.folders }, { ID_VIEWS, t.views } };
        for (auto& b : ids) if (b[1] >= 0) SendMessageW(g.toolbar, TB_CHANGEBITMAP, (WPARAM)b[0], MAKELPARAM(b[1], 0));
        set_toolbar_size(g.large_toolbar);
    }
    taskpane::theme_changed();
    RedrawWindow(g.frame, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

// ---- Go button ----------------------------------------------------------------
// XP browseui address.cpp: the Go button is a one-button list toolbar inside the
// ComboBoxEx window, which is subclassed so that its inner combo leaves room on
// the right (cx - cxGo - 2) and the button sits at cx - cxGo, vertically centred
// on the inner combo. The strip under the button shows the rebar background.

namespace gobutton {

HWND g_tb = nullptr;
WNDPROC g_old = nullptr;

static void place(HWND cbx, int cx)
{
    RECT rc = {}; SendMessageW(g_tb, TB_GETITEMRECT, 0, (LPARAM)&rc);
    int cxGo = rc.right - rc.left, cyGo = rc.bottom - rc.top;
    HWND combo = (HWND)SendMessageW(cbx, CBEM_GETCOMBOCONTROL, 0, 0);
    RECT cr = {}; GetWindowRect(combo ? combo : cbx, &cr);
    int y = (cr.bottom - cr.top - cyGo) / 2;
    SetWindowPos(g_tb, nullptr, cx - cxGo, y, cxGo, cyGo, SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOZORDER);
}

static int go_width() { RECT rc = {}; SendMessageW(g_tb, TB_GETITEMRECT, 0, (LPARAM)&rc); return rc.right - rc.left; }

static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_WINDOWPOSCHANGING: {
        WINDOWPOS* pwp = (WINDOWPOS*)lp;
        if (pwp->flags & SWP_NOSIZE) break;
        WINDOWPOS wp2 = *pwp;
        int cx = pwp->cx;
        wp2.cx -= go_width() + 2;
        LRESULT r = CallWindowProcW(g_old, h, msg, wp, (LPARAM)&wp2);
        pwp->cy = wp2.cy;   // Wine's comboex fits its height to the combo
        RECT inv = { cx - go_width() - 2, 0, cx, pwp->cy };
        InvalidateRect(h, &inv, TRUE);
        place(h, cx);
        return r;
    }
    case WM_SIZE: {
        int cx = LOWORD(lp), cy = HIWORD(lp);
        LRESULT r = CallWindowProcW(g_old, h, msg, wp, MAKELPARAM(cx - go_width() - 2, cy));
        place(h, cx);
        return r;
    }
    case WM_ERASEBKGND: {
        // forward to the rebar so the strip under the button is its background
        HWND parent = GetParent(h);
        POINT pt = {0, 0};
        MapWindowPoints(h, parent, &pt, 1);
        HDC dc = (HDC)wp;
        POINT old;
        OffsetWindowOrgEx(dc, pt.x, pt.y, &old);
        LRESULT r = SendMessageW(parent, WM_ERASEBKGND, wp, 0);
        SetWindowOrgEx(dc, old.x, old.y, nullptr);
        return r;
    }
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lp;
        if (nm->hwndFrom == g_tb) {
            if (nm->code == NM_CLICK) {   // as XP: simulate Enter in the edit
                HWND edit = (HWND)SendMessageW(h, CBEM_GETEDITCONTROL, 0, 0);
                if (edit) { SendMessageW(edit, WM_KEYDOWN, VK_RETURN, 0); SendMessageW(edit, WM_KEYUP, VK_RETURN, 0); }
            }
            return 0;
        }
        break;
    }
    }
    return CallWindowProcW(g_old, h, msg, wp, lp);
}

static HIMAGELIST glyphs(const std::string& src, int size)
{
    HBITMAP bmp = (HBITMAP)load_themed(src, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    if (!bmp) return nullptr;
    // The SP1 bitmap is 24-bit; the XP (SP3) one the screenshots show has
    // rounded alpha corners: per corner 0.05 at the corner pixel, 0.56 next
    // to it, 0.93 one further (measured), everything else opaque.
    DIBSECTION ds = {};
    if (GetObjectW(bmp, sizeof ds, &ds) == sizeof ds && ds.dsBm.bmBits && ds.dsBm.bmBitsPixel == 32 && ds.dsBm.bmWidth >= 3) {
        int w = ds.dsBm.bmWidth, h = ds.dsBm.bmHeight < 0 ? -ds.dsBm.bmHeight : ds.dsBm.bmHeight;
        uint8_t* bits = (uint8_t*)ds.dsBm.bmBits;
        for (int i = 0; i < w * h; i++) bits[i * 4 + 3] = 0xff;
        static const struct { int dx, dy; uint8_t a; } corner[] = { {0,0,13}, {1,0,143}, {0,1,143}, {2,0,237}, {0,2,237} };
        for (auto c : corner)
            for (int k = 0; k < 4; k++) {
                int x = (k & 1) ? w - 1 - c.dx : c.dx, y = (k & 2) ? h - 1 - c.dy : c.dy;
                bits[((size_t)y * w + x) * 4 + 3] = c.a;
            }
        HIMAGELIST il = ImageList_Create(size, size, ILC_COLOR32, 1, 1);
        ImageList_Add(il, bmp, nullptr);
        DeleteObject(bmp);
        return il;
    }
    HIMAGELIST il = ImageList_Create(size, size, ILC_COLOR24 | ILC_MASK, 1, 1);
    ImageList_AddMasked(il, bmp, RGB(255, 0, 255));   // XP's crMask
    DeleteObject(bmp);
    return il;
}

static void create(HWND cbx)
{
    const shell::ToolbarTheme& t = shell::icon_theme().toolbar;
    if (t.go.empty()) return;
    HIMAGELIST def = glyphs(t.go, t.go_size), hot = glyphs(t.go_hot.empty() ? t.go : t.go_hot, t.go_size);
    if (!def) return;
    g_old = (WNDPROC)SetWindowLongPtrW(cbx, GWLP_WNDPROC, (LONG_PTR)proc);
    g_tb = CreateWindowExW(WS_EX_TOOLWINDOW, TOOLBARCLASSNAMEW, nullptr,
        WS_CHILD | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | TBSTYLE_LIST | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | CCS_NODIVIDER | CCS_NOPARENTALIGN | CCS_NORESIZE,
        0, 0, 0, 0, cbx, nullptr, nullptr, nullptr);
    SendMessageW(g_tb, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(g_tb, TB_SETMAXTEXTROWS, 1, 0);
    SendMessageW(g_tb, TB_SETBUTTONWIDTH, 0, MAKELONG(0, 500));
    SendMessageW(g_tb, TB_SETIMAGELIST, 0, (LPARAM)def);
    SendMessageW(g_tb, TB_SETHOTIMAGELIST, 0, (LPARAM)hot);
    TBBUTTON b = { 0, 1, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Go" };
    SendMessageW(g_tb, TB_ADDBUTTONSW, 1, (LPARAM)&b);
    RECT rc; GetClientRect(cbx, &rc);
    SetWindowPos(cbx, nullptr, 0, 0, rc.right, rc.bottom, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

} // namespace gobutton

// ---- Folders explorer bar --------------------------------------------------------
// XP browseui browbs.cpp (CBrowserBandSite): the tree is a band in a vertical
// band site whose band header carries the title ("Folders", menu font, at x 6)
// and an etch line at the header's bottom; a flat toolbar with the close (X)
// button sits at the top right. The band site is a rebar, so under a visual
// style the whole bar gets the Rebar background and the band starts after the
// Rebar.Band margin (2px, the vertical bar turns it into a top margin).

static void on_command(int id);

namespace bandsite {

const int TITLE = 22;          // _uTitle: text height 13 + CY_ETCH 2 + CY_FLUFF 7
HWND g_close = nullptr;
HFONT g_font = nullptr;

static int top_margin()
{
    HTHEME th = OpenThemeData(g.bandsite, L"Rebar");
    MARGINS m = {};
    if (th) { GetThemeMargins(th, nullptr, RP_BAND, 0, TMT_CONTENTMARGINS, nullptr, &m); CloseThemeData(th); }
    return m.cxLeftWidth;   // vertical band site: the left margin runs along y
}

int header() { return top_margin() + TITLE; }

static void layout_children()
{
    RECT rc; GetClientRect(g.bandsite, &rc);
    int h = header();
    if (g.tree) SetWindowPos(g.tree, nullptr, 0, h, rc.right, rc.bottom - h, SWP_NOZORDER | SWP_NOACTIVATE);
    if (g_close) {
        // _PositionToolbars: x = right - width - 1, y = the band's top margin
        // (0). This matches three of the four XP screenshots; in the Luna/large
        // one the X sits 4px further left (unexplained).
        RECT br = {}; SendMessageW(g_close, TB_GETITEMRECT, 0, (LPARAM)&br);
        int w = br.right - br.left, bh = br.bottom - br.top;
        int x = rc.right - w - 1;
        SetWindowPos(g_close, nullptr, x, 0, w, bh, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE: layout_children(); return 0;
    case WM_THEMECHANGED: {                 // another visual style: its menu font, its band margins
        NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof ncm;
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
        if (g_font) DeleteObject(g_font);
        g_font = CreateFontIndirectW(&ncm.lfMenuFont);
        layout_children();
        InvalidateRect(h, nullptr, TRUE);
        return 0;
    }
    case WM_ERASEBKGND: {
        HDC dc = (HDC)wp;
        RECT rc; GetClientRect(h, &rc);
        HTHEME th = OpenThemeData(h, L"Rebar");
        if (th) { DrawThemeBackground(th, dc, 0, 0, &rc, nullptr); CloseThemeData(th); }
        else FillRect(dc, &rc, GetSysColorBrush(COLOR_BTNFACE));
        // title and etch line (browbs.cpp _OnCDNotify)
        int top = top_margin();
        HGDIOBJ of = SelectObject(dc, g_font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
        SIZE ts = {}; GetTextExtentPoint32W(dc, L"Folders", 7, &ts);
        TextOutW(dc, 6, top + ((TITLE - 2) - ts.cy) / 2, L"Folders", 7);   // CX_TEXTOFFSET, centred above the etch
        SelectObject(dc, of);
        RECT e = { 0, top + TITLE - 2, rc.right, top + TITLE - 1 };
        FillRect(dc, &e, GetSysColorBrush(COLOR_BTNSHADOW));
        OffsetRect(&e, 0, 1);
        FillRect(dc, &e, GetSysColorBrush(COLOR_BTNHIGHLIGHT));
        return 1;
    }
    case WM_NOTIFY: return SendMessageW(g.frame, WM_NOTIFY, wp, lp);
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_BANDCLOSE) { on_command(ID_VIEW_FOLDERS); return 0; }
        return SendMessageW(g.frame, WM_COMMAND, wp, lp);
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void create(HWND frame)
{
    WNDCLASSW wc = {};
    wc.lpfnWndProc = proc;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"flxp_bandsite";
    RegisterClassW(&wc);
    g.bandsite = CreateWindowExW(0, L"flxp_bandsite", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 0, 0, frame, (HMENU)IDC_BANDSITE, nullptr, nullptr);
    NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    g_font = CreateFontIndirectW(&ncm.lfMenuFont);   // _GetTitleFont: the menu font
    // the close button: browseui's browtb.bmp (13x11 cells, the X is cell 1),
    // drawn navy (0,2,92) on XP SP3 where SP1's bitmap has it black
    g_close = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
        WS_VISIBLE | WS_CHILD | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | CCS_NODIVIDER | CCS_NOMOVEY | CCS_NOPARENTALIGN | CCS_NORESIZE,
        0, 0, 30, 18, g.bandsite, (HMENU)IDC_BANDCLOSE, nullptr, nullptr);
    SetWindowTheme(g_close, L"", L"");   // measured: the button keeps its classic size under Luna
    SendMessageW(g_close, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(g_close, TB_SETBITMAPSIZE, 0, MAKELONG(13, 11));
    HBITMAP bmp = (HBITMAP)load_themed(shell::icon_theme().toolbar.close, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    if (!bmp) {
        // icon themes without browtb.bmp (longhorn, 2k, wine): the same X drawn here,
        // XP's cell 1 pixel for pixel, navy as below, on the strip's grey mask colour
        static const char* const x[11] = { "", "", "...##....##", "....##..##", ".....####", "......##",
                                           ".....####", "....##..##", "...##....##", "", "" };
        BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = 39; bi.bmiHeader.biHeight = -11; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (bmp && bits) {
            uint32_t* px = (uint32_t*)bits;
            for (int i = 0; i < 39 * 11; i++) px[i] = 0xc0c0c0;
            for (int y = 0; y < 11; y++)
                for (int c = 0; x[y][c]; c++) if (x[y][c] == '#') px[y * 39 + 13 + c] = 0x00025c;
        }
    }
    HIMAGELIST il = ImageList_Create(13, 11, ILC_COLOR24 | ILC_MASK, 3, 1);
    if (bmp) {
        DIBSECTION ds = {};
        if (GetObjectW(bmp, sizeof ds, &ds) == sizeof ds && ds.dsBm.bmBits && ds.dsBm.bmBitsPixel == 32) {
            uint32_t* px = (uint32_t*)ds.dsBm.bmBits;
            size_t n = (size_t)ds.dsBm.bmWidth * (size_t)(ds.dsBm.bmHeight < 0 ? -ds.dsBm.bmHeight : ds.dsBm.bmHeight);
            for (size_t i = 0; i < n; i++) if ((px[i] & 0xffffff) == 0) px[i] = (px[i] & 0xff000000) | 0x00025c;   // 0xRRGGBB (0,2,92)
        }
        ImageList_AddMasked(il, bmp, RGB(192, 192, 192));
        DeleteObject(bmp);
    }
    SendMessageW(g_close, TB_SETIMAGELIST, 0, (LPARAM)il);
    SendMessageW(g_close, TB_SETMAXTEXTROWS, 0, 0);
    TBBUTTON b = { 1, IDC_BANDCLOSE, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Close" };
    SendMessageW(g_close, TB_ADDBUTTONSW, 1, (LPARAM)&b);
    SendMessageW(g_close, TB_SETINDENT, 0, 0);
}

} // namespace bandsite

// ---- brand band -------------------------------------------------------------
// XP browseui brand.cpp: a fixed-size band the rebar keeps at the right end of
// the first row. The "micro" brand is a strip of square frames (shell32
// IDB_BRAND_BITMAP_MICRO, 22x22, frame 0 = the static logo) painted centred on
// the strip's top-left colour (clrBkStat).

namespace brand {

HBITMAP g_strip = nullptr;
int g_size = 0;
COLORREF g_bk = RGB(255, 255, 255);

static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HBRUSH bk = CreateSolidBrush(g_bk);
        FillRect(dc, &rc, bk);
        DeleteObject(bk);
        if (g_strip) {
            int x = rc.left, y = rc.top, cx = rc.right - rc.left, cy = rc.bottom - rc.top;
            if (cx > g_size) x += (cx - g_size) / 2;
            if (cy > g_size) y += (cy - g_size) / 2;
            HDC mem = CreateCompatibleDC(dc);
            HGDIOBJ old = SelectObject(mem, g_strip);
            BitBlt(dc, x, y, g_size, g_size, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteDC(mem);
        }
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// Adds the band to the first row; nothing if the icon theme has no brand.
static void create(HWND rebar)
{
    const std::string& src = shell::icon_theme().toolbar.brand;
    if (src.empty()) return;
    g_strip = (HBITMAP)load_themed(src, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    if (!g_strip) return;
    BITMAP bm = {};
    GetObjectW(g_strip, sizeof bm, &bm);
    g_size = bm.bmWidth;
    HDC mem = CreateCompatibleDC(nullptr);
    HGDIOBJ old = SelectObject(mem, g_strip);
    g_bk = GetPixel(mem, 0, 0);
    SelectObject(mem, old);
    DeleteDC(mem);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = proc;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"flxp_brand";
    RegisterClassW(&wc);
    HWND h = CreateWindowExW(0, L"flxp_brand", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, rebar, (HMENU)IDC_BRAND, nullptr, nullptr);
    REBARBANDINFOW rb = {};
    rb.cbSize = sizeof rb;
    rb.fMask = RBBIM_STYLE | RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_SIZE | RBBIM_ID;
    rb.fStyle = RBBS_FIXEDSIZE | RBBS_NOGRIPPER;
    rb.hwndChild = h;
    rb.wID = IDC_BRAND;
    // measured on XP: the white brand area is 38 wide (+ the etch's white column), the row 22 high
    rb.cxMinChild = 38; rb.cyMinChild = (UINT)g_size; rb.cyChild = (UINT)g_size; rb.cx = 38;
    SendMessageW(rebar, RB_INSERTBANDW, 1, (LPARAM)&rb);
}

static void destroy(HWND rebar)
{
    int i = (int)SendMessageW(rebar, RB_IDTOINDEX, IDC_BRAND, 0);
    if (i < 0) return;
    REBARBANDINFOW rb = {}; rb.cbSize = sizeof rb; rb.fMask = RBBIM_CHILD;
    SendMessageW(rebar, RB_GETBANDINFOW, (WPARAM)i, (LPARAM)&rb);
    SendMessageW(rebar, RB_DELETEBAND, (WPARAM)i, 0);
    if (rb.hwndChild) DestroyWindow(rb.hwndChild);
    if (g_strip) DeleteObject(g_strip);
    g_strip = nullptr;
}

} // namespace brand

// ---- the details: XP's Details section, 4051's band ---------------------------

enum { WM_APP_DETAILS = WM_APP + 2 };
static bool g_details_pending;

// What shell/details says about the folder or the selection, to the panes that
// show it: computed once per burst of changes (a selection of many items
// sends a notification per item)
static void schedule_details()
{
    if (g_details_pending || !g.frame) return;
    g_details_pending = true;
    PostMessageW(g.frame, WM_APP_DETAILS, 0, 0);
}

static void update_details()
{
    g_details_pending = false;
    bool xp = places::window() && !g.longhorn;
    if (!g.folder || (!xp && !g.taskband)) return;
    std::vector<Item> sel = browser::selection(g);
    int focus = -1;
    int fi = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_FOCUSED);
    if (fi >= 0 && fi < (int)g.items.size())
        for (size_t i = 0; i < sel.size(); i++) if (sel[i].path == g.items[(size_t)fi].path) focus = (int)i;
    shell::details::Info in = shell::details::describe(sel, shell::details::location_item(*g.folder), (int)g.items.size(), focus);
    // pictures: where thumbnails are allowed (thumbnails, thumbnails_network)
    bool pictures = g.folder->is_filesystem() && shell::thumbs::enabled_for(g.folder->path());
    // XP: no mini preview in the Thumbnails view (CDUIView::ShouldShowMiniPreview)
    if (xp) places::set_details(in, pictures && browser::view_code(g) != VIEW_THUMBNAILS, g.folder->path() == "::computer");
    if (g.taskband) taskpane::set_details(in, pictures);
}

// ---- status bar -------------------------------------------------------------

static void update_status()
{
    schedule_details();
    if (!g.status) return;
    int sel = (int)SendMessageW(g.list, LVM_GETSELECTEDCOUNT, 0, 0);
    char buf[128];
    uint64_t bytes = 0;
    std::string text;
    if (sel > 0) {
        snprintf(buf, sizeof buf, "%d object%s selected", sel, sel == 1 ? "" : "s");
        text = buf;
        int i = -1, last = -1;
        while ((i = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)i, LVNI_SELECTED)) >= 0) {
            if (i < (int)g.items.size() && !g.items[(size_t)i].is_folder()) bytes += g.items[(size_t)i].size;
            last = i;
        }
        // one file: XP shows its type here; we add the program that opens it
        if (sel == 1 && last >= 0 && last < (int)g.items.size() && !g.items[(size_t)last].is_folder() && !(g.items[(size_t)last].flags & shell::IsVirtual)) {
            const Item& it = g.items[(size_t)last];
            std::string mime = shell::mime_type(it.path);
            text = "Type: " + it.type;
            if (shell::is_program(it.path, mime)) text += "   (program)";
            else if (const shell::App* a = shell::default_app(mime)) text += "   Opens with: " + a->name;
        }
    } else {
        snprintf(buf, sizeof buf, "%d object%s", (int)g.items.size(), g.items.size() == 1 ? "" : "s");
        text = buf;
        for (const Item& it : g.items) if (!it.is_folder()) bytes += it.size;
    }
    SendMessageW(g.status, SB_SETTEXTW, 0, (LPARAM)WS(text));
    SendMessageW(g.status, SB_SETTEXTW, 1, (LPARAM)WS(bytes ? shell::format_size(bytes) : ""));
    std::string where = g.folder && g.folder->is_filesystem() ? "My Computer" : (g.folder ? g.folder->name() : "");
    SendMessageW(g.status, SB_SETTEXTW, 2, (LPARAM)WS(where));
}

// ---- list view --------------------------------------------------------------

static void fill_list() { browser::fill_list(g); }
int Explorer::icon(const std::string& key) { return icon_for(key); }
HIMAGELIST Explorer::icons_at(int px) { return uiicons::at_size(px); }
bool Explorer::ghosted(const Item& it) { return verbs::is_cut(it.path); }
void Explorer::filled() { update_status(); }

namespace lhtool { static void changed(); }

// the view as the menus see it: LVS_*, VIEW_THUMBNAILS or VIEW_TILES (XP), the view code (Longhorn: Tiles, Hide File Names)
static int menu_view() { return browser::view_code(g); }

static void set_view(int view, bool navigating = false) { browser::set_view(g, view, navigating); }

// the Longhorn frame's views (browser.h: Details, Tiles, Icons; thumbnails a setting; Show in
// Groups, flxp.ini groups), XP's otherwise
static void sync_thumbs_mode()
{
    g.zoomable = g.longhorn;
    g.grouped = g.longhorn && shell::setting("groups", "0") == "1";
    verbs::thumbs_toggle = g.longhorn ? (shell::setting("thumbnails", "1") != "0" ? 1 : 0) : -1;
}

// The View menu's views, made again each time it opens (the frame can change): XP's
// Thumbnails, Tiles, Icons, Small Icons, List, Details; the Longhorn frame's Details, Tiles,
// Icons, Hide File Names and Show Thumbnails; then Default View
static void view_items(HMENU view)
{
    static const UINT ids[] = { ID_VIEW_THUMBNAILS, ID_VIEW_ICONS, ID_VIEW_SMALLICONS, ID_VIEW_LIST, ID_VIEW_DETAILS, ID_VIEW_TILES,
                                ID_VIEW_HIDENAMES, ID_VIEW_LARGEICONS, ID_VIEW_MEDIUMICONS, ID_VIEW_GROUPS, ID_VIEW_DEFAULT };
    int at = -1;
    for (int i = GetMenuItemCount(view) - 1; i >= 0; i--) {
        UINT id = GetMenuItemID(view, i);
        if (std::find(std::begin(ids), std::end(ids), id) == std::end(ids)) continue;
        DeleteMenu(view, (UINT)i, MF_BYPOSITION);
        at = i;
    }
    if (at < 0) return;
    auto add = [&](UINT id, const wchar_t* text) { InsertMenuW(view, (UINT)at++, MF_BYPOSITION | MF_STRING, id, text); };
    if (g.zoomable) {   // 4074's View menu: Details, Tiles, Icons ... Hide File Names
        add(ID_VIEW_DETAILS, L"&Details\tCtrl+1");
        add(ID_VIEW_TILES, L"Tile&s\tCtrl+2");
        add(ID_VIEW_ICONS, L"Ico&ns");
        add(ID_VIEW_HIDENAMES, L"Hide Fi&le Names");
        add(ID_VIEW_THUMBNAILS, L"Sho&w Thumbnails");
        add(ID_VIEW_GROUPS, L"Show in &Groups");   // 4074's (by type here); Tiles and Icons
        CheckMenuItem(view, ID_VIEW_GROUPS, MF_BYCOMMAND | (g.grouped ? MF_CHECKED : MF_UNCHECKED));
        EnableMenuItem(view, ID_VIEW_GROUPS, MF_BYCOMMAND | (browser::group_view_applies(g) ? MF_ENABLED : MF_GRAYED));
    } else {
        add(ID_VIEW_THUMBNAILS, L"T&humbnails\tCtrl+3");
        add(ID_VIEW_TILES, L"Tile&s");
        add(ID_VIEW_ICONS, L"Ico&ns\tCtrl+2");
        add(ID_VIEW_SMALLICONS, L"S&mall Icons\tCtrl+5");
        add(ID_VIEW_LIST, L"&List\tCtrl+4");
        add(ID_VIEW_DETAILS, L"&Details\tCtrl+1");
    }
    add(ID_VIEW_DEFAULT, L"D&efault View\tCtrl+0");
    verbs::check_view_items(view, menu_view());
}

void Explorer::view_changed()
{
    sync_thumbs_mode();
    verbs::check_view_items(GetSubMenu(menu, 2), menu_view());
    lhtool::changed();
}

// ---- tree -------------------------------------------------------------------

static const std::string* tree_path(HTREEITEM h)
{
    TVITEMW tvi = {};
    tvi.mask = TVIF_PARAM; tvi.hItem = h;
    if (!SendMessageW(g.tree, TVM_GETITEMW, 0, (LPARAM)&tvi)) return nullptr;
    return (const std::string*)tvi.lParam;
}

static HTREEITEM tree_insert(HTREEITEM parent, const std::string& name, const std::string& path, const std::string& icon, bool may_have_children)
{
    g.tree_paths.push_back(path);
    TVINSERTSTRUCTW ins = {};
    ins.hParent = parent;
    ins.hInsertAfter = TVI_LAST;
    ins.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM | TVIF_CHILDREN;
    std::u16string n = W(name);
    ins.item.pszText = (LPWSTR)n.c_str();
    ins.item.iImage = icon_for(icon);
    ins.item.iSelectedImage = icon == "folder" ? icon_for("folder_open") : ins.item.iImage;
    ins.item.lParam = (LPARAM)&g.tree_paths.back();
    ins.item.cChildren = may_have_children ? 1 : 0;
    return (HTREEITEM)SendMessageW(g.tree, TVM_INSERTITEMW, 0, (LPARAM)&ins);
}

// Populate a node's children on first expansion.
static void tree_fill(HTREEITEM h)
{
    if (SendMessageW(g.tree, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)h)) return;   // already filled
    const std::string* p = tree_path(h);
    if (!p) return;
    auto f = shell::folder_for(*p);
    if (!f) return;
    std::vector<Item> kids;
    f->enumerate(kids, g.show_hidden);
    shell::sort_items(kids, shell::SortBy::Name, true);
    int n = 0;
    for (const Item& it : kids) {
        if (!it.is_folder()) continue;
        tree_insert(h, it.name, it.path, it.icon, true);
        n++;
    }
    if (!n) {
        TVITEMW tvi = {}; tvi.mask = TVIF_CHILDREN; tvi.hItem = h; tvi.cChildren = 0;
        SendMessageW(g.tree, TVM_SETITEMW, 0, (LPARAM)&tvi);
    }
}

// Expand the tree along the current folder's ancestry and select it.
static void sync_tree()
{
    if (!g.folder || !g.tree) return;
    std::vector<std::string> chain;
    for (auto f = g.folder; f; f = f->parent()) chain.push_back(f->path());
    HTREEITEM node = g.tree_root;
    g.syncing_tree = true;
    for (int i = (int)chain.size() - 2; i >= 0 && node; i--) {
        tree_fill(node);
        SendMessageW(g.tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)node);
        HTREEITEM c = (HTREEITEM)SendMessageW(g.tree, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)node);
        HTREEITEM found = nullptr;
        for (; c; c = (HTREEITEM)SendMessageW(g.tree, TVM_GETNEXTITEM, TVGN_NEXT, (LPARAM)c)) {
            const std::string* p = tree_path(c);
            if (p && *p == chain[(size_t)i]) { found = c; break; }
        }
        node = found;
    }
    if (node) {
        SendMessageW(g.tree, TVM_SELECTITEM, TVGN_CARET, (LPARAM)node);
        SendMessageW(g.tree, TVM_ENSUREVISIBLE, 0, (LPARAM)node);
    }
    g.syncing_tree = false;
}

// ---- navigation ---------------------------------------------------------------

// (browser.h: the filter, refresh, navigation and history are the shared widget's)
static void apply_filter() { browser::apply_filter(g); }
static void refresh() { browser::refresh(g); }

// after refresh: the frame's title, the Address band, the toolbar, the header,
// the task band and the tree follow the folder
void Explorer::shown()
{
    std::string title = folder->name();
    SetWindowTextW(frame, WS(title));
    // address band: current path with its icon
    COMBOBOXEXITEMW ci = {};
    ci.mask = CBEIF_TEXT | CBEIF_IMAGE | CBEIF_SELECTEDIMAGE;
    ci.iItem = -1;
    std::u16string addr = W(folder->is_filesystem() ? folder->path() : folder->name());
    if (const char* t = getenv("FLXP_ADDRESS_TEXT")) addr = W(t);   // testing: show what a reference screenshot shows
    ci.pszText = (LPWSTR)addr.c_str();
    ci.iImage = ci.iSelectedImage = icon_for(folder->icon());
    SendMessageW(address, CBEM_SETITEMW, 0, (LPARAM)&ci);
    SendMessageW(toolbar, TB_ENABLEBUTTON, ID_BACK, MAKELONG(!back.empty(), 0));
    SendMessageW(toolbar, TB_ENABLEBUTTON, ID_FORWARD, MAKELONG(!forward.empty(), 0));
    SendMessageW(toolbar, TB_ENABLEBUTTON, ID_UP, MAKELONG(folder->parent() != nullptr, 0));
    if (header) { header::set_history(!back.empty(), !forward.empty()); header::set_folder(folder); }
    places::folder_changed();
    browser::set_window_icon(frame, folder->icon());
    if (taskband) taskpane::set_folder(folder);
    schedule_details();
    sync_tree();
}

static bool navigate(const std::string& path, bool record = true) { return browser::navigate(g, path, record); }
static void go_back() { browser::go_back(g); }
static void go_forward() { browser::go_forward(g); }
static void go_up() { browser::go_up(g); }

// The Views drop-down.
static void views_menu(int x, int y)
{
    HMENU m = verbs::view_submenu(menu_view(), true);
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN, x, y, 0, g.frame, nullptr);   // (WM_COMMAND: on_command)
    DestroyMenu(m);
}

static void history_menu(bool back, int x, int y) { browser::history_menu(g, g.frame, back, x, y); }

static void open_item(int i)
{
    if (i < 0 || i >= (int)g.items.size()) return;
    verbs::open_items({ g.items[(size_t)i] });
}

// ---- layout -------------------------------------------------------------------

static HWND g_viewstrip;   // 4074's view button under the left pane (the Longhorn frame; viewbutton.h)

static void layout()
{
    RECT rc; GetClientRect(g.frame, &rc);
    int top = 0, bottom = rc.bottom;
    if (g.header) {
        header::relayout(rc.right, header::height());
        top = header::height();
    }
    if (g.rebar) {
        int h = (int)SendMessageW(g.rebar, RB_GETBARHEIGHT, 0, 0);
        if (getenv("FLXP_DEBUG")) {
            fprintf(stderr, "rebar height %d\n", h);
            HWND ws[] = { g.rebar, g.toolbar, g.address, (HWND)SendMessageW(g.address, CBEM_GETCOMBOCONTROL, 0, 0), (HWND)SendMessageW(g.address, CBEM_GETEDITCONTROL, 0, 0) };
            const char* names[] = { "rebar", "toolbar", "comboex", "combo", "edit" };
            for (int i = 0; i < 5; i++) { RECT r = {}; if (ws[i]) GetWindowRect(ws[i], &r); fprintf(stderr, "  %-8s %p (%ld,%ld)-(%ld,%ld)\n", names[i], (void*)ws[i], (long)r.left, (long)r.top, (long)r.right, (long)r.bottom); }
        }
        SetWindowPos(g.rebar, nullptr, 0, top, rc.right, h, SWP_NOZORDER);
        top += h;
    }
    bool band_bottom = g.taskband && taskpane::bottom();   // Explorer.taskpane = bottom: under the list and the left pane
    if (g.taskband) {
        taskpane::set_room(bottom - top - (g.status && g.show_status ? 22 : 0));
        if (!band_bottom) {
            SetWindowPos(g.taskband, nullptr, 0, top, rc.right, taskpane::height(), SWP_NOZORDER);
            top += taskpane::height();
        }
    }
    if (g.status) {
        if (g.show_status) {
            SendMessageW(g.status, WM_SIZE, 0, 0);
            RECT sr; GetWindowRect(g.status, &sr);
            bottom -= sr.bottom - sr.top;
            int parts[3] = { rc.right - 260, rc.right - 140, -1 };
            SendMessageW(g.status, SB_SETPARTS, 3, (LPARAM)parts);
            ShowWindow(g.status, SW_SHOW);
        } else {
            ShowWindow(g.status, SW_HIDE);
        }
    }
    if (band_bottom) {
        bottom -= taskpane::height();
        SetWindowPos(g.taskband, nullptr, 0, bottom, rc.right, taskpane::height(), SWP_NOZORDER);
    }
    int x = 0;
    // 4074's view button: a strip at the bottom of the left pane (the tree's or the places pane's); with the band under the
    // list it stays clear of the 14 rows the band's preview hangs over, and the pane runs on under it to the band
    HWND vb = g_viewstrip;
    bool left = g.show_folders || places::window();
    int hang = band_bottom ? 14 : 0;
    int pane_bottom = vb && left ? bottom - hang - viewbutton::STRIP_H : bottom;
    int pane_end = band_bottom ? bottom : pane_bottom;   // where the tree or the places pane ends
    if (vb) {
        if (left) SetWindowPos(vb, HWND_TOP, 0, pane_bottom, g.tree_width, viewbutton::STRIP_H, 0);
        ShowWindow(vb, left ? SW_SHOW : SW_HIDE);
    }
    if (g.show_folders) {
        SetWindowPos(g.bandsite, nullptr, 0, top, g.tree_width, pane_end - top, SWP_NOZORDER);
        ShowWindow(g.bandsite, SW_SHOW);
        x = g.tree_width + 4;
    } else {
        ShowWindow(g.bandsite, SW_HIDE);
    }
    if (HWND pl = places::window()) {   // the places pane takes the tree's slot when it is off (XP: the task pane)
        if (!g.show_folders) {
            SetWindowPos(pl, nullptr, 0, top, g.tree_width, pane_end - top, SWP_NOZORDER);
            ShowWindow(pl, SW_SHOW);
            x = g.tree_width + 4;
        } else ShowWindow(pl, SW_HIDE);
    }
    SetWindowPos(g.list, nullptr, x, top, rc.right - x, bottom - top, SWP_NOZORDER);
}

// ---- creation -----------------------------------------------------------------


static HMENU build_menu()
{
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu(), edit = CreatePopupMenu(), view = CreatePopupMenu(), bm = CreatePopupMenu(), tools = CreatePopupMenu(), help = CreatePopupMenu();
    g.file_menu = file; g.edit_menu = edit; g.bookmarks_menu = bm;   // File and Bookmarks are filled on WM_INITMENUPOPUP
    HMENU bars = CreatePopupMenu(), xbar = CreatePopupMenu(), arrange = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, ID_FILE_CLOSE, L"&Close");
    AppendMenuW(edit, MF_STRING, ID_CUT, L"Cu&t\tCtrl+X");
    AppendMenuW(edit, MF_STRING, ID_COPY, L"&Copy\tCtrl+C");
    AppendMenuW(edit, MF_STRING, ID_PASTE, L"&Paste\tCtrl+V");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, ID_EDIT_SELECTALL, L"Select &All\tCtrl+A");
    AppendMenuW(edit, MF_STRING, ID_EDIT_INVERT, L"&Invert Selection");
    AppendMenuW(bars, MF_STRING | MF_CHECKED | MF_GRAYED, 0, L"&Standard Buttons");
    AppendMenuW(bars, MF_STRING | MF_CHECKED | MF_GRAYED, 0, L"&Address Bar");
    AppendMenuW(bars, MF_SEPARATOR, 0, nullptr);
    // XP's Customize Toolbar dialog's "Icon options" (Large icons / Small icons), here without the dialog
    g_icon_menu = CreatePopupMenu();
    AppendMenuW(g_icon_menu, MF_STRING, ID_TB_LARGE, L"&Large Icons");
    AppendMenuW(g_icon_menu, MF_STRING, ID_TB_SMALL, L"&Small Icons");
    CheckMenuRadioItem(g_icon_menu, ID_TB_LARGE, ID_TB_SMALL, g.large_toolbar ? ID_TB_LARGE : ID_TB_SMALL, MF_BYCOMMAND);
    g_bars_menu = bars;   // the size part is the frame's (fill_bars_menu)
    AppendMenuW(view, MF_POPUP, (UINT_PTR)bars, L"&Toolbars");
    AppendMenuW(view, MF_STRING | (g.show_status ? MF_CHECKED : 0), ID_VIEW_STATUSBAR, L"Status &Bar");
    AppendMenuW(view, MF_STRING | MF_GRAYED, ID_VIEW_TASKPANE, L"Task Ba&nd");
    AppendMenuW(view, MF_STRING | MF_GRAYED, ID_VIEW_PREVIEW, L"Pre&view");
    AppendMenuW(xbar, MF_STRING | (g.show_folders ? MF_CHECKED : 0), ID_VIEW_FOLDERS, L"&Folders");
    AppendMenuW(xbar, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(xbar, MF_STRING | MF_GRAYED, ID_VIEW_PLACES_ICONS, L"&Icons in the Pane");   // the Longhorn frame's groups look
    AppendMenuW(view, MF_POPUP, (UINT_PTR)xbar, L"&Explorer Bar");
    g_xbar_menu = xbar; g_arrange_menu = arrange;
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, ID_VIEW_THUMBNAILS, L"T&humbnails\tCtrl+3");
    AppendMenuW(view, MF_STRING, ID_VIEW_TILES, L"Tile&s");
    AppendMenuW(view, MF_STRING, ID_VIEW_ICONS, L"Ico&ns\tCtrl+2");
    AppendMenuW(view, MF_STRING, ID_VIEW_SMALLICONS, L"S&mall Icons\tCtrl+5");
    AppendMenuW(view, MF_STRING, ID_VIEW_LIST, L"&List\tCtrl+4");
    AppendMenuW(view, MF_STRING, ID_VIEW_DETAILS, L"&Details\tCtrl+1");
    AppendMenuW(view, MF_STRING, ID_VIEW_DEFAULT, L"D&efault View\tCtrl+0");
    g_zoom_menu = CreatePopupMenu();                  // filled on WM_INITMENUPOPUP (the Longhorn frame's)
    AppendMenuW(view, MF_POPUP, (UINT_PTR)g_zoom_menu, L"&Zoom");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(arrange, MF_STRING, ID_SORT_NAME, L"&Name");
    AppendMenuW(arrange, MF_STRING, ID_SORT_SIZE, L"&Size");
    AppendMenuW(arrange, MF_STRING, ID_SORT_TYPE, L"&Type");
    AppendMenuW(arrange, MF_STRING, ID_SORT_DATE, L"&Modified");
    AppendMenuW(view, MF_POPUP, (UINT_PTR)arrange, L"Arrange &Icons by");
    AppendMenuW(view, MF_STRING, ID_VIEW_CHOOSEDETAILS, L"Choose De&tails...");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, ID_SEARCH, L"&Filter by Name...");   // (the 4051 bar's magnifier is the zoom)
    AppendMenuW(view, MF_STRING, ID_FILTER_CLEAR, L"&Clear Filter");
    AppendMenuW(view, MF_STRING | (g.show_hidden ? MF_CHECKED : 0), ID_VIEW_HIDDEN, L"Show &Hidden Files");
    AppendMenuW(view, MF_STRING, ID_VIEW_REFRESH, L"&Refresh\tF5");
    AppendMenuW(bm, MF_STRING | MF_GRAYED, 0, L"(Empty)");
    AppendMenuW(tools, MF_STRING, ID_TOOLS_FOLDEROPTIONS, L"Folder &Options...");
    AppendMenuW(help, MF_STRING, ID_HELP_ABOUT, L"&About flxp");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)edit, L"&Edit");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)view, L"&View");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)bm, L"&Bookmarks");   // where XP has Favorites
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)tools, L"&Tools");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Help");
    CheckMenuRadioItem(view, ID_VIEW_ICONS, ID_VIEW_DETAILS, ID_VIEW_DETAILS, MF_BYCOMMAND);
    CheckMenuRadioItem(arrange, ID_SORT_NAME, ID_SORT_DATE, ID_SORT_NAME, MF_BYCOMMAND);
    return bar;
}

static HMENU build_menu();

// ---- the Longhorn header (frame=longhorn) ---------------------------------------

static void header_navigate(const std::string& path)
{
    if (g.folder && g.folder->path() == path) { refresh(); return; }
    if (!navigate(path)) MessageBeep(0);
}

static void header_filter(const std::string& text) { browser::set_filter(g, text); }

static void header_done(bool) { SetFocus(g.list); }

// the places pane (tree off)
static std::string current_dir();
static std::string places_current() { return current_dir(); }
static std::string places_path() { return g.folder ? g.folder->path() : ""; }
static void places_done() { SetFocus(g.list); }
static void on_command(int id);
static void header_command(int id) { on_command(id); }
static void header_history(bool back, int x, int y) { history_menu(back, x, y); }
static void header_favorites(int x, int y)   // the navigation bar's star: the Bookmarks menu
{
    if (g.bookmarks_menu) TrackPopupMenu(g.bookmarks_menu, TPM_LEFTALIGN | TPM_TOPALIGN, x, y, 0, g.frame, nullptr);
}
// the vision header's Views and New Folder items (nav.ini `tools`)
static void header_tool(int id, int x, int y) { if (id == ID_VIEWS) views_menu(x, y); else on_command(id); }

// The frame extends lhwm's glass by the header height and draws its own title
// (browser.h, shared with the file dialog).
static void sync_active();
static void extend_frame(HWND frame) { browser::extend_frame(frame); }
static void caption_hole(HWND frame) { browser::caption_hole(frame); }
static void header_refit(HWND frame) { browser::header_refit(frame, layout); }

// ---- the Longhorn 4051 command bar (frame=longhorn) ------------------------------
// lh4051-*.png: the menu band and, on the same row after a gap, a small list
// toolbar - a red-crossed page, Search, Folders, Views (drop-down), a magnifier
// with a drop-down and a details-view button shown checked - all on the folder
// type's coloured gradient (lhbar). No Back/Forward (the header has them),
// no Address bar (the breadcrumb). Icons: shell32's 16 px strips of the
// Longhorn system32 (#216/#217, XP's tb_sh_def_16 layout; #206/#207 the
// browser strip with the crossed page), else the icon theme's small strip.
namespace lhtool {

// shell32 strip 216/217 (doc/4051-explorer-research.md section 3): 17 the
// glass (Search and the zoom's drop-down), 40 the preview pane button; IE's 206/207: 2 Stop
enum { SHELL_DELETE = 10, SHELL_SEARCH = 17, SHELL_DETAILS = 23, SHELL_PREVIEW = 40, SHELL_VIEWS = 32, SHELL_FOLDERS = 43, IE_STOP = 2 };

HWND g_tb;

static bool checked(int id)
{
    if (id == ID_FOLDERS) return g.show_folders;
    if (id == ID_DETAILS_TOGGLE) return g.taskband && !taskpane::compact();   // View > Preview
    return false;
}

static LRESULT menu_draw(NMTBCUSTOMDRAW* cd, bool active) { return lhbar::custom_draw(cd, active, nullptr); }

// the strip's cell `index` appended to il (as an icon, keeping alpha)
static int add_cell(HIMAGELIST il, HIMAGELIST from, int index)
{
    if (!from || index >= ImageList_GetImageCount(from)) return -1;
    HICON ic = ImageList_GetIcon(from, index, ILD_NORMAL);
    if (!ic) return -1;
    int i = ImageList_ReplaceIcon(il, -1, ic);
    DestroyIcon(ic);
    return i;
}

// Stop is enabled while thumbnails are being made (navigation itself is synchronous)
enum { TIMER_STOP = 77 };
static void CALLBACK stop_timer(HWND, UINT, UINT_PTR, DWORD)
{
    if (!g_tb) return;
    bool busy = thumbview::busy();
    if (busy != ((SendMessageW(g_tb, TB_GETSTATE, ID_STOP, 0) & TBSTATE_ENABLED) != 0))
        SendMessageW(g_tb, TB_ENABLEBUTTON, ID_STOP, MAKELONG(busy, 0));
}

static void create()
{
    std::string sys = shell::theme_setting("longhorn_system32");
    HIMAGELIST normal = nullptr, hot = nullptr, ie = nullptr, iehot = nullptr;
    if (!sys.empty()) {
        normal = strip_list("pe:" + sys + "/shell32.dll#216", 16);
        hot = strip_list("pe:" + sys + "/shell32.dll#217", 16);
        ie = strip_list("pe:" + sys + "/shell32.dll#206", 16);
        iehot = strip_list("pe:" + sys + "/shell32.dll#207", 16);
    }
    if (!normal || ImageList_GetImageCount(normal) < 48) {
        if (normal) ImageList_Destroy(normal);
        if (hot) ImageList_Destroy(hot);
        hot = nullptr;
        const shell::ToolbarTheme& t = shell::icon_theme().toolbar;
        normal = t.small_strip.empty() ? nullptr : strip_list(t.small_strip, 16);
        if (normal && !t.small_hot_strip.empty()) hot = strip_list(t.small_hot_strip, 16);
    }
    int i_del = SHELL_DELETE, i_search = SHELL_SEARCH, i_folders = SHELL_FOLDERS, i_views = SHELL_VIEWS, i_details = SHELL_DETAILS;
    if (normal && ImageList_GetImageCount(normal) > SHELL_PREVIEW && !sys.empty()) i_details = SHELL_PREVIEW;
    g_tb = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | TBSTYLE_LIST | TBSTYLE_TRANSPARENT | CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN,
        0, 0, 0, 0, g.rebar, (HMENU)IDC_LHTOOLBAR, nullptr, nullptr);
    if (lhbar::enabled()) SetWindowTheme(g_tb, L"", L"");   // drawn by lhbar on the gradient
    SendMessageW(g_tb, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(g_tb, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS | TBSTYLE_EX_DRAWDDARROWS | TBSTYLE_EX_HIDECLIPPEDBUTTONS);
    SendMessageW(g_tb, TB_SETMAXTEXTROWS, 1, 0);
    SendMessageW(g_tb, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), FALSE);
    SendMessageW(g_tb, TB_SETBITMAPSIZE, 0, MAKELONG(16, 16));
    if (normal) {
        if (ie) { int k = add_cell(normal, ie, IE_STOP); if (k >= 0) i_del = k; }
        if (hot && iehot) add_cell(hot, iehot, IE_STOP);
        SendMessageW(g_tb, TB_SETIMAGELIST, 0, (LPARAM)normal);
        if (hot && ImageList_GetImageCount(hot) == ImageList_GetImageCount(normal)) SendMessageW(g_tb, TB_SETHOTIMAGELIST, 0, (LPARAM)hot);
    } else {
        int std = (int)SendMessageW(g_tb, TB_LOADIMAGES, IDB_STD_SMALL_COLOR, (LPARAM)HINST_COMMCTRL);
        int view = (int)SendMessageW(g_tb, TB_LOADIMAGES, IDB_VIEW_SMALL_COLOR, (LPARAM)HINST_COMMCTRL);
        i_del = std + STD_DELETE; i_search = std + STD_FIND; i_folders = view + VIEW_NETCONNECT;
        i_views = view + VIEW_VIEWMENU; i_details = view + VIEW_DETAILS;
    }
    if (ie) ImageList_Destroy(ie);
    if (iehot) ImageList_Destroy(iehot);
    TBBUTTON tb[] = {
        { i_del, ID_STOP, 0, BTNS_BUTTON | BTNS_AUTOSIZE, {0}, 0, (INT_PTR)L"Stop" },   // enabled while loading
        { I_IMAGENONE, 0, 0, BTNS_BUTTON, {0}, 0, 0 },   // gaps: disabled empty buttons (a flat separator would draw its etch)
        { i_search, ID_SEARCH, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT, {0}, 0, (INT_PTR)L"Search" },
        { i_folders, ID_FOLDERS, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT, {0}, 0, (INT_PTR)L"Folders" },
        { I_IMAGENONE, 0, 0, BTNS_BUTTON, {0}, 0, 0 },
        { i_views, ID_VIEWS, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_WHOLEDROPDOWN, {0}, 0, (INT_PTR)L"Views" },
        { i_search, ID_FILTER, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_WHOLEDROPDOWN, {0}, 0, (INT_PTR)L"Zoom" },
        { i_details, ID_DETAILS_TOGGLE, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE, {0}, 0, (INT_PTR)L"Preview" },
    };
    SendMessageW(g_tb, TB_ADDBUTTONSW, sizeof tb / sizeof tb[0], (LPARAM)tb);
    // the gaps measured on lh4051-225221.png: 14 after the crossed page, 10 before Views
    const int gaps[][2] = { { 1, 14 }, { 4, 10 } };
    for (auto& gp : gaps) {
        TBBUTTONINFOW bi = {};
        bi.cbSize = sizeof bi; bi.dwMask = TBIF_BYINDEX | TBIF_SIZE; bi.cx = (WORD)gp[1];
        SendMessageW(g_tb, TB_SETBUTTONINFOW, (WPARAM)gp[0], (LPARAM)&bi);
    }
    SendMessageW(g_tb, TB_AUTOSIZE, 0, 0);
    SIZE sz = {0, 0};
    SendMessageW(g_tb, TB_GETMAXSIZE, 0, (LPARAM)&sz);
    RECT item = {0, 0, 0, 0};
    SendMessageW(g_tb, TB_GETITEMRECT, 0, (LPARAM)&item);
    REBARBANDINFOW rb = {};
    rb.cbSize = sizeof rb;
    rb.fMask = RBBIM_STYLE | RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_SIZE | RBBIM_ID | RBBIM_HEADERSIZE;
    rb.fStyle = RBBS_NOGRIPPER | RBBS_USECHEVRON;      // same row as the menu band
    rb.hwndChild = g_tb;
    rb.wID = IDC_LHTOOLBAR;
    rb.cxHeader = 30;                                  // 4051: about 40 px from Help to the first button
    rb.cxMinChild = (UINT)sz.cx; rb.cyMinChild = (UINT)(item.bottom - item.top); rb.cyChild = rb.cyMinChild; rb.cx = (UINT)sz.cx;
    SendMessageW(g.rebar, RB_INSERTBANDW, 1, (LPARAM)&rb);
    SendMessageW(g.rebar, RB_SHOWBAND, (WPARAM)SendMessageW(g.rebar, RB_IDTOINDEX, IDC_TOOLBAR, 0), FALSE);
    if (lhbar::enabled()) {
        lhbar::attach(g.rebar);
        menuband::set_custom_draw(menu_draw);
    }
    SetTimer(g.frame, TIMER_STOP, 250, stop_timer);
    changed();
}

// back to the XP bar (a theme switch): the band goes, the rebar draws itself again
static void destroy()
{
    if (!g_tb) return;
    int i = (int)SendMessageW(g.rebar, RB_IDTOINDEX, IDC_LHTOOLBAR, 0);
    if (i >= 0) SendMessageW(g.rebar, RB_DELETEBAND, (WPARAM)i, 0);
    DestroyWindow(g_tb);
    g_tb = nullptr;
    KillTimer(g.frame, TIMER_STOP);
    lhbar::detach(g.rebar);
    menuband::set_custom_draw(nullptr);
}

static void changed()
{
    if (!g_tb) return;
    if (!lhbar::enabled()) {   // the visual style draws checked buttons itself
        SendMessageW(g_tb, TB_CHECKBUTTON, ID_FOLDERS, MAKELONG(checked(ID_FOLDERS), 0));
        SendMessageW(g_tb, TB_CHECKBUTTON, ID_DETAILS_TOGGLE, MAKELONG(checked(ID_DETAILS_TOGGLE), 0));
    }
    InvalidateRect(g_tb, nullptr, TRUE);
}

} // namespace lhtool

static void sync_taskpane();
// the status bar: on in the XP frame, off in the Longhorn one (4051's Explorer
// default, CShellBrowser2::_LoadStatusBar); View > Status Bar remembers it per frame
static bool status_setting()
{
    return g.longhorn ? shell::setting("statusbar_longhorn", "0") == "1" : shell::setting("statusbar", "1") != "0";
}
static void layout();
static void taskpane_resized() { sync_taskpane(); layout(); }
// The fonts the visual style names ([SysMetrics], or the classic scheme's Metrics): XP's Folders tree and defview's list take
// comctl32's default, the icon title font (ICONTITLEFONT: Segoe UI 9 under Jade and Aero, Tahoma 8 under Luna and Slate), the
// status bar the status font; the menu band its menu font (menuband.cpp). Again on WM_THEMECHANGED.
static void fonts_changed()
{
    LOGFONTW lf = {};
    SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof lf, &lf, 0);
    HFONT item = CreateFontIndirectW(&lf);
    NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    HFONT status = CreateFontIndirectW(&ncm.lfStatusFont);
    if (g.tree) SendMessageW(g.tree, WM_SETFONT, (WPARAM)item, TRUE);
    if (g.list) SendMessageW(g.list, WM_SETFONT, (WPARAM)item, TRUE);
    if (g.status) SendMessageW(g.status, WM_SETFONT, (WPARAM)status, TRUE);
    if (g.item_font) DeleteObject(g.item_font);
    if (g.status_font) DeleteObject(g.status_font);
    g.item_font = item; g.status_font = status;
}
static void create_children(HWND frame)
{
    g.frame = frame;
    g.font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if (g.longhorn) {
        header::Callbacks cb = { header_navigate, header_filter, header_done, header_command, header_history, header_favorites, header_tool };
        g.header = header::create(frame, cb);
        extend_frame(frame);
    }
    uiicons::init();
    g.small = uiicons::small();
    g.large = uiicons::large();

    // rebar + toolbar + address band
    g.rebar = CreateWindowExW(WS_EX_TOOLWINDOW, REBARCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | RBS_VARHEIGHT | RBS_BANDBORDERS | RBS_AUTOSIZE | CCS_NODIVIDER | CCS_NOPARENTALIGN,
        0, 0, 0, 0, frame, (HMENU)IDC_REBAR, nullptr, nullptr);
    REBARINFO rbi = { sizeof rbi, 0, nullptr };
    SendMessageW(g.rebar, RB_SETBARINFO, 0, (LPARAM)&rbi);
    g.menu = build_menu();
    menuband::create(g.rebar, frame, g.menu, IDC_MENUBAND);
    if (!g.longhorn) brand::create(g.rebar);   // 4051 has no throbber on its bar

    g.toolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | TBSTYLE_LIST | TBSTYLE_TRANSPARENT | CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN,
        0, 0, 0, 0, g.rebar, (HMENU)IDC_TOOLBAR, nullptr, nullptr);
    SendMessageW(g.toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(g.toolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS | TBSTYLE_EX_DRAWDDARROWS | TBSTYLE_EX_HIDECLIPPEDBUTTONS);
    SendMessageW(g.toolbar, TB_SETMAXTEXTROWS, 1, 0);
    SendMessageW(g.toolbar, TB_SETDROPDOWNGAP, (WPARAM)(GetSystemMetrics(SM_CXEDGE) / 2), 0);   // browseui itbar.cpp
    SendMessageW(g.toolbar, TB_SETBITMAPSIZE, 0, MAKELONG(24, 24));
    int i_back = HIST_BACK, i_fwd = HIST_FORWARD, i_up, i_search = HIST_FAVORITES, i_folders = HIST_VIEWTREE, i_views;
    if (load_toolbar_theme()) {
        const shell::ToolbarTheme& t = shell::icon_theme().toolbar;
        i_back = t.back; i_fwd = t.forward; i_up = t.up; i_search = t.search; i_folders = t.folders; i_views = t.views;
    } else {
        SendMessageW(g.toolbar, TB_LOADIMAGES, IDB_HIST_LARGE_COLOR, (LPARAM)HINST_COMMCTRL);
        int view = (int)SendMessageW(g.toolbar, TB_LOADIMAGES, IDB_VIEW_LARGE_COLOR, (LPARAM)HINST_COMMCTRL);
        i_up = view + VIEW_PARENTFOLDER; i_views = view + VIEW_VIEWMENU;
    }
    static const wchar_t* labels[] = { L"Back", L"Forward", L"Up", L"Search", L"Folders", L"Views" };
    TBBUTTON tb[] = {
        // XP's list-style toolbar autosizes every button (browseui's c_tbExplorer relies on it)
        { i_back, ID_BACK, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT | BTNS_DROPDOWN, {0}, 0, (INT_PTR)labels[0] },
        { i_fwd, ID_FORWARD, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_DROPDOWN, {0}, 0, (INT_PTR)labels[1] },
        { i_up, ID_UP, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE, {0}, 0, (INT_PTR)labels[2] },
        { 6, 0, TBSTATE_ENABLED, BTNS_SEP, {0}, 0, 0 },   // browseui CX_SEPARATOR
        { i_search, ID_SEARCH, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT, {0}, 0, (INT_PTR)labels[3] },
        { i_folders, ID_FOLDERS, (BYTE)(TBSTATE_ENABLED | (g.show_folders ? TBSTATE_CHECKED : 0)), BTNS_CHECK | BTNS_AUTOSIZE | BTNS_SHOWTEXT, {0}, 0, (INT_PTR)labels[4] },
        { 6, 0, TBSTATE_ENABLED, BTNS_SEP, {0}, 0, 0 },
        { i_views, ID_VIEWS, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_WHOLEDROPDOWN, {0}, 0, (INT_PTR)labels[5] },
    };
    SendMessageW(g.toolbar, TB_ADDBUTTONSW, sizeof tb / sizeof tb[0], (LPARAM)tb);
    SendMessageW(g.toolbar, TB_AUTOSIZE, 0, 0);
    SIZE tbsize = {0, 0};
    SendMessageW(g.toolbar, TB_GETMAXSIZE, 0, (LPARAM)&tbsize);
    RECT tbitem = {0, 0, 0, 0};
    SendMessageW(g.toolbar, TB_GETITEMRECT, 0, (LPARAM)&tbitem);
    if (getenv("FLXP_DEBUG")) fprintf(stderr, "toolbar max size %ldx%ld button height %ld\n", (long)tbsize.cx, (long)tbsize.cy, (long)(tbitem.bottom - tbitem.top));
    if (getenv("FLXP_DEBUG"))
        for (int i = 0; i < (int)(sizeof tb / sizeof tb[0]); i++) {
            RECT r = {}; SendMessageW(g.toolbar, TB_GETITEMRECT, (WPARAM)i, (LPARAM)&r);
            fprintf(stderr, "  button %d: x %ld..%ld w %ld h %ld\n", i, (long)r.left, (long)r.right, (long)(r.right - r.left), (long)(r.bottom - r.top));
        }

    g.address = CreateWindowExW(0, WC_COMBOBOXEXW, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL,
        0, 0, 0, 250, g.rebar, (HMENU)IDC_ADDRESS, nullptr, nullptr);
    SendMessageW(g.address, CBEM_SETIMAGELIST, 0, (LPARAM)g.small);
    SendMessageW(g.address, WM_SETFONT, (WPARAM)g.font, TRUE);
    gobutton::create(g.address);

    REBARBANDINFOW rb = {};
    rb.cbSize = sizeof rb;
    rb.fMask = RBBIM_STYLE | RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_SIZE | RBBIM_ID;
    rb.fStyle = RBBS_NOGRIPPER | RBBS_USECHEVRON | RBBS_BREAK;   // own row below the menu band
    rb.hwndChild = g.toolbar;
    rb.wID = IDC_TOOLBAR;
    // XP (browseui itbar.cpp GetBandInfo): band height = first button's height
    // (30 for 24px glyphs classic, 38 under Luna whose button sizing margins add 4px per side)
    rb.cxMinChild = tbsize.cx; rb.cyMinChild = (UINT)(tbitem.bottom - tbitem.top); rb.cyChild = rb.cyMinChild; rb.cx = tbsize.cx;
    // Measured on XP/Luna (large and small icons): the toolbar sits 4px further
    // right than the menu band (both start after the 2px Rebar.Band margin).
    // Not explained by the SP1 rebar/itbar source; emulated with a header.
    if (GetWindowTheme(g.rebar)) { rb.fMask |= RBBIM_HEADERSIZE; rb.cxHeader = 4; }
    SendMessageW(g.rebar, RB_INSERTBANDW, (WPARAM)-1, (LPARAM)&rb);
    rb.fMask = RBBIM_STYLE | RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_SIZE | RBBIM_TEXT | RBBIM_ID;
    rb.fStyle = RBBS_NOGRIPPER | RBBS_BREAK;
    rb.hwndChild = g.address;
    rb.wID = IDC_ADDRESS;
    rb.lpText = (LPWSTR)L"A&ddress";   // browseui IDS_BAND_ADDRESS2
    HWND combo = (HWND)SendMessageW(g.address, CBEM_GETCOMBOCONTROL, 0, 0);
    int item_h = (int)SendMessageW(combo ? combo : g.address, CB_GETITEMHEIGHT, (WPARAM)-1, 0);
    // XP address.cpp GetBandInfo: the band is as high as the inner combo box
    RECT crc = {}; GetWindowRect(combo ? combo : g.address, &crc);
    rb.cxMinChild = 200; rb.cyMinChild = (UINT)(crc.bottom - crc.top); rb.cx = 600;
    (void)item_h;
    SendMessageW(g.rebar, RB_INSERTBANDW, (WPARAM)-1, (LPARAM)&rb);
    if (g.longhorn) {
        // 4074: the breadcrumb in the header replaces the Address bar (View > Toolbars brings it back)
        SendMessageW(g.rebar, RB_SHOWBAND, (WPARAM)SendMessageW(g.rebar, RB_IDTOINDEX, IDC_ADDRESS, 0), FALSE);
        HMENU bars = GetSubMenu(GetSubMenu(g.menu, 2), 0);
        ModifyMenuW(bars, 1, MF_BYPOSITION | MF_STRING | MF_UNCHECKED, ID_TB_ADDRESS, L"&Address Bar");
        // 4051: the menu band and a small toolbar share one coloured row
        lhtool::create();
        g_viewstrip = viewbutton::create(frame, g);   // 4074: the view button at the bottom of the left pane
    }
    if (getenv("FLXP_DEBUG")) {
        for (int i = 0; i < 2; i++) {
            REBARBANDINFOW bi = {}; bi.cbSize = sizeof bi; bi.fMask = RBBIM_CHILDSIZE | RBBIM_SIZE | RBBIM_STYLE;
            SendMessageW(g.rebar, RB_GETBANDINFOW, (WPARAM)i, (LPARAM)&bi);
            fprintf(stderr, "band %d: cxMin=%u cyMin=%u cyChild=%u cx=%u style=%x\n", i, bi.cxMinChild, bi.cyMinChild, bi.cyChild, bi.cx, bi.fStyle);
        }
        fprintf(stderr, "rows=%d row0=%d row1=%d\n", (int)SendMessageW(g.rebar, RB_GETROWCOUNT, 0, 0), (int)SendMessageW(g.rebar, RB_GETROWHEIGHT, 0, 0), (int)SendMessageW(g.rebar, RB_GETROWHEIGHT, 1, 0));
    }

    // folders tree
    bandsite::create(frame);
    if (shell::setting("places_pane", "on") != "off") {
        places::Callbacks pcb = { header_navigate, places_current, header_filter, places_done, nullptr, places_path };
        places::Options po;
        po.groups = g.longhorn;              // the Longhorn frame: the dialog's groups look; XP: the task pane
        po.remember = "places_collapsed";
        po.details = true;                   // XP's Details section
        places::create(frame, IDC_PLACES, pcb, po);
    }
    // XP's Folders tree has no client edge inside the band site
    g.tree = CreateWindowExW(0, WC_TREEVIEWW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS | TVS_EDITLABELS,
        0, 0, 0, 0, g.bandsite, (HMENU)IDC_TREE, nullptr, nullptr);
    SendMessageW(g.tree, TVM_SETIMAGELIST, TVSIL_NORMAL, (LPARAM)g.small);
    auto root = shell::desktop();
    g.tree_root = tree_insert(TVI_ROOT, root->name(), root->path(), root->icon(), true);

    // file list
    // XP's defview list has no client edge in Explorer (measured)
    g.list = CreateWindowExW(0, WC_LISTVIEWW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS | LVS_AUTOARRANGE | LVS_EDITLABELS,
        0, 0, 0, 0, frame, (HMENU)IDC_LIST, nullptr, nullptr);
    // Explorer::ListView where the msstyles has it (Longhorn/Vista: themed item
    // selection, LVP_LISTITEM); XP's styles have no such class, nothing changes there
    SetWindowTheme(g.list, L"Explorer", nullptr);
    SendMessageW(g.list, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)g.small);
    SendMessageW(g.list, LVM_SETIMAGELIST, LVSIL_NORMAL, (LPARAM)g.large);
    SendMessageW(g.list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_HEADERDRAGDROP | LVS_EX_LABELTIP, LVS_EX_HEADERDRAGDROP | LVS_EX_LABELTIP);
    thumbview::init(g.list, frame);
    // the columns as chosen last (XP's details defaults: Name, Size, Type, Date Modified,
    // browser::all_columns), the header's menu and Choose Details, the infotip
    g.choose_columns = true;
    g.infotips = true;
    browser::attach_list(g);
    browser::load_columns(g);

    // status bar
    g.status = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SBARS_SIZEGRIP,
        0, 0, 0, 0, frame, (HMENU)IDC_STATUS, nullptr, nullptr);
    fonts_changed();
    g.show_taskpane = taskpane::wanted();
    sync_taskpane();
}

// ---- item verbs: context menus, Open With, file operations ------------------------
// The menus and the verbs themselves are shared with the desktop (verbs.cpp);
// the explorer is their Host: folders open in this window, refreshes refill the
// list and the tree.

enum { WM_APP_REFRESH = WM_APP + 1 };

using verbs::g_ctx;
using verbs::Ctx;
using verbs::is_fs;
using verbs::error_box;
static std::string g_reload_dir;  // tree node to reload on WM_APP_REFRESH

static std::string current_dir() { return g.folder && g.folder->is_filesystem() ? g.folder->path() : ""; }

static std::vector<Item> selection() { return browser::selection(g); }
static int list_index(const std::string& path) { return browser::list_index(g, path); }

static Item tree_item(HTREEITEM h)
{
    Item it;
    const std::string* p = tree_path(h);
    if (!p) return it;
    WCHAR buf[512] = {};
    TVITEMW tvi = {}; tvi.mask = TVIF_TEXT; tvi.hItem = h; tvi.pszText = buf; tvi.cchTextMax = 511;
    SendMessageW(g.tree, TVM_GETITEMW, 0, (LPARAM)&tvi);
    it.name = w32::utf8_of(buf);
    it.path = *p;
    it.flags = shell::IsFolder | (p->compare(0, 2, "::") == 0 ? (uint32_t)shell::IsVirtual : 0u);
    it.type = "File Folder";
    it.icon = "folder";
    return it;
}

// the menu is for this tree item
static void ctx_tree_node(HTREEITEM h)
{
    g_ctx.node = h;
    g_ctx.node_expanded = (SendMessageW(g.tree, TVM_GETITEMSTATE, (WPARAM)h, TVIS_EXPANDED) & TVIS_EXPANDED) != 0;
    g_ctx.items.push_back(tree_item(h));
}

static HTREEITEM tree_find(HTREEITEM h, const std::string& path)
{
    for (; h; h = (HTREEITEM)SendMessageW(g.tree, TVM_GETNEXTITEM, TVGN_NEXT, (LPARAM)h)) {
        const std::string* p = tree_path(h);
        if (p && *p == path) return h;
        if (p && (p->compare(0, 2, "::") == 0 || path.compare(0, p->size(), *p) == 0)) {
            HTREEITEM c = (HTREEITEM)SendMessageW(g.tree, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)h);
            if (HTREEITEM f = tree_find(c, path)) return f;
        }
    }
    return nullptr;
}

// Re-read a folder's children in the tree (after a new folder, rename or delete in it).
static void tree_reload(const std::string& path)
{
    if (path.empty()) return;
    HTREEITEM h = tree_find(g.tree_root, path);
    if (!h) return;
    bool expanded = (SendMessageW(g.tree, TVM_GETITEMSTATE, (WPARAM)h, TVIS_EXPANDED) & TVIS_EXPANDED) != 0;
    g.syncing_tree = true;
    while (HTREEITEM c = (HTREEITEM)SendMessageW(g.tree, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)h))
        SendMessageW(g.tree, TVM_DELETEITEM, 0, (LPARAM)c);
    TVITEMW tvi = {}; tvi.mask = TVIF_CHILDREN | TVIF_STATE; tvi.hItem = h; tvi.cChildren = 1;
    tvi.stateMask = TVIS_EXPANDEDONCE | TVIS_EXPANDED; tvi.state = 0;
    SendMessageW(g.tree, TVM_SETITEMW, 0, (LPARAM)&tvi);
    if (expanded) { tree_fill(h); SendMessageW(g.tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)h); }
    g.syncing_tree = false;
    sync_tree();
}

static void begin_rename()
{
    if (g_ctx.node) { SetFocus(g.tree); SendMessageW(g.tree, TVM_EDITLABELW, 0, (LPARAM)g_ctx.node); return; }
    if (g_ctx.items.size() != 1) return;
    int i = list_index(g_ctx.items[0].path);
    if (i < 0) return;
    SetFocus(g.list);
    SendMessageW(g.list, LVM_EDITLABELW, (WPARAM)i, 0);
}

// After a rename the explorer may be inside the renamed folder.
static void renamed(const std::string& from, const std::string& to)
{
    if (g.folder && g.folder->is_filesystem()) {
        std::string cur = g.folder->path();
        if (cur == from || cur.compare(0, from.size() + 1, from + "/") == 0) navigate(to + cur.substr(from.size()), false);
    }
    verbs::clip_renamed(from, to);
}

static Item folder_item()
{
    Item it;
    if (!g.folder) return it;
    it.path = g.folder->path();
    it.name = g.folder->name();
    it.flags = shell::IsFolder | (g.folder->is_filesystem() ? 0u : (uint32_t)shell::IsVirtual);
    it.type = "File Folder";
    it.icon = g.folder->icon();
    return it;
}

struct ExplorerHost : verbs::Host {
    HWND window() override { return g.frame; }
    std::string current_dir() override { return ::current_dir(); }
    Item folder_item() override { return ::folder_item(); }
    void open_folder(const Item& it, bool explore) override
    {
        if (explore && !g.show_folders) { g.folders_once = true; on_command(ID_VIEW_FOLDERS); g.folders_once = false; }
        navigate(it.path);
    }
    void changed(const std::vector<std::string>& dirs, const std::string& select, bool rename) override
    {
        g.select_after_refresh = select;
        g.rename_after_refresh = rename;
        refresh();
        for (const std::string& d : dirs) tree_reload(d);
    }
    void begin_rename() override { ::begin_rename(); }
    void clip_changed() override
    {
        for (size_t i = 0; i < g.items.size(); i++) {   // ghost the cut items (LVIS_CUT), keeping the selection
            LVITEMW st = {}; st.stateMask = LVIS_CUT; st.state = verbs::is_cut(g.items[i].path) ? LVIS_CUT : 0;
            SendMessageW(g.list, LVM_SETITEMSTATE, (WPARAM)i, (LPARAM)&st);
        }
    }
    void trashed(const Item& it) override
    {
        // the folder being shown (or one above it) went away: show what contained it
        if (it.is_folder() && g.folder && (g.folder->path() == it.path || g.folder->path().compare(0, it.path.size() + 1, it.path + "/") == 0))
            navigate(shell::dir_of(it.path), false);
    }
    void updated() override { update_status(); }
    // the list's rows again (their columns, the infotip) and the panes' details; the
    // selection stays
    void stored_changed(const std::vector<std::string>&) override
    {
        browser::update(g);
        schedule_details();
    }
};
static ExplorerHost g_host;

static void verb(int id)
{
    if (id == ID_EXPAND) {
        HTREEITEM node = (HTREEITEM)g_ctx.node;
        if (node) { tree_fill(node); SendMessageW(g.tree, TVM_EXPAND, TVE_TOGGLE, (LPARAM)node); }
        return;
    }
    verbs::run(id);
}

// ---- menus ----

static void track(HMENU m, POINT pt)
{
    int cmd = TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, g.frame, nullptr);
    DestroyMenu(m);
    if (cmd <= 0) return;
    if (cmd >= ID_VIEW_STATUSBAR && cmd <= ID_VIEW_MEDIUMICONS) on_command(cmd);   // (the View submenu's views)
    else verb(cmd);
}

// WM_CONTEXTMENU from the list or the tree; pt is (-1,-1) from the keyboard.
static void context_menu(HWND from, POINT pt)
{
    bool keyboard = pt.x == -1 && pt.y == -1;
    g_ctx = Ctx();
    HMENU m = CreatePopupMenu();
    if (from == g.tree) {
        HTREEITEM h = nullptr;
        if (keyboard) {
            h = (HTREEITEM)SendMessageW(g.tree, TVM_GETNEXTITEM, TVGN_CARET, 0);
            RECT r = {}; *(HTREEITEM*)&r = h;
            if (h && SendMessageW(g.tree, TVM_GETITEMRECT, TRUE, (LPARAM)&r)) { pt.x = r.left; pt.y = r.bottom; ClientToScreen(g.tree, &pt); }
        } else {
            TVHITTESTINFO hti = {}; hti.pt = pt; ScreenToClient(g.tree, &hti.pt);
            h = (HTREEITEM)SendMessageW(g.tree, TVM_HITTEST, 0, (LPARAM)&hti);
            if (!(hti.flags & TVHT_ONITEM)) h = nullptr;
        }
        if (!h) { DestroyMenu(m); return; }
        ctx_tree_node(h);
        verbs::build_item_menu(m);
        SendMessageW(g.tree, TVM_SELECTITEM, TVGN_DROPHILITE, (LPARAM)h);   // XP marks the item the menu is for
        track(m, pt);
        if (IsWindow(g.tree)) SendMessageW(g.tree, TVM_SELECTITEM, TVGN_DROPHILITE, 0);
        return;
    }
    bool on_item;
    if (keyboard) {
        int f = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED | LVNI_FOCUSED);
        if (f < 0) f = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
        on_item = f >= 0;
        RECT r = {}; r.left = LVIR_ICON;
        if (on_item && SendMessageW(g.list, LVM_GETITEMRECT, (WPARAM)f, (LPARAM)&r)) { pt.x = (r.left + r.right) / 2; pt.y = (r.top + r.bottom) / 2; }
        else { pt.x = 4; pt.y = 4; }
        ClientToScreen(g.list, &pt);
    } else {
        LVHITTESTINFO hti = {}; hti.pt = pt; ScreenToClient(g.list, &hti.pt);
        on_item = SendMessageW(g.list, LVM_HITTEST, 0, (LPARAM)&hti) >= 0 && (hti.flags & LVHT_ONITEM);
    }
    if (on_item) {
        g_ctx.items = selection();
        if (g_ctx.items.empty()) { DestroyMenu(m); return; }
        verbs::build_item_menu(m);
    } else {
        verbs::build_background_menu(m, verbs::BG_VIEW | verbs::BG_ARRANGE | verbs::BG_THUMBNAILS, menu_view(), (int)g.sort);
    }
    track(m, pt);
}

// The File menu is the selection's context menu (XP), or the folder's.
static void init_file_menu(HMENU m)
{
    while (GetMenuItemCount(m) > 0) DeleteMenu(m, 0, MF_BYPOSITION);
    g_ctx = Ctx();
    g_ctx.items = selection();
    if (!g_ctx.items.empty()) verbs::build_item_menu(m);
    else verbs::build_background_menu(m, 0, g.view, (int)g.sort);
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_FILE_CLOSE, L"&Close");
}

static void init_edit_menu(HMENU m)
{
    bool sel = false;
    for (const Item& it : selection()) if (is_fs(it)) sel = true;
    EnableMenuItem(m, ID_CUT, MF_BYCOMMAND | (sel ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, ID_COPY, MF_BYCOMMAND | (sel ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, ID_PASTE, MF_BYCOMMAND | ((!verbs::can_paste() || current_dir().empty()) ? MF_GRAYED : MF_ENABLED));
}

// Bookmarks: Add, Organize, then the entries with folder icons (owner-drawn bitmaps).
static void append_icon_item(HMENU m, UINT id, const std::string& text, int icon, bool grayed)
{
    std::u16string t = W(text);
    MENUITEMINFOW mii = {};
    mii.cbSize = sizeof mii;
    mii.fMask = MIIM_STRING | MIIM_ID | MIIM_BITMAP | MIIM_DATA | MIIM_STATE;
    mii.wID = id;
    mii.dwTypeData = (LPWSTR)t.c_str();
    mii.hbmpItem = HBMMENU_CALLBACK;    // WM_MEASUREITEM/WM_DRAWITEM; itemData = image + 1 (0: blank)
    mii.dwItemData = (ULONG_PTR)(icon + 1);
    mii.fState = grayed ? MFS_GRAYED : 0;
    InsertMenuItemW(m, (UINT)GetMenuItemCount(m), TRUE, &mii);
}

// Bookmarks: Add, Organize, then the entries with folder icons. Every item gets
// a (possibly blank) callback bitmap so the texts line up in one column.
static void init_bookmarks_menu(HMENU m)
{
    while (GetMenuItemCount(m) > 0) DeleteMenu(m, 0, MF_BYPOSITION);
    append_icon_item(m, ID_BM_ADD, "&Add to Bookmarks", -1, current_dir().empty());
    append_icon_item(m, ID_BM_ORGANIZE, "&Organize Bookmarks...", -1, false);
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    g.bookmarks = shell::load_bookmarks();
    if (g.bookmarks.size() > (size_t)(ID_BM_LAST - ID_BM_FIRST)) g.bookmarks.resize((size_t)(ID_BM_LAST - ID_BM_FIRST));
    for (size_t i = 0; i < g.bookmarks.size(); i++) {
        const shell::Bookmark& b = g.bookmarks[i];
        std::string label = b.display_name();
        for (size_t k = 0; (k = label.find('&', k)) != std::string::npos; k += 2) label.insert(k, "&");
        // remote (sftp://...) bookmarks are kept but not browsable yet
        append_icon_item(m, ID_BM_FIRST + (UINT)i, label, icon_for(b.path().empty() ? "network" : "folder"), b.path().empty());
    }
    if (g.bookmarks.empty()) append_icon_item(m, 0, "(Empty)", -1, true);
}

static void go_bookmark(size_t i)
{
    if (i >= g.bookmarks.size()) return;
    std::string p = g.bookmarks[i].path();
    if (p.empty() || !navigate(p)) error_box("The bookmarked folder " + (p.empty() ? g.bookmarks[i].uri : p) + " is not available.");
}

// ---- notifications --------------------------------------------------------------

// ---- drag and drop (flxp_shell.h, NOTES "Drag and drop") ----------------------------
static HTREEITEM g_drop_node;       // the tree node (TVGN_DROPHILITE); the list's item: browser::g_drop_item

static void drop_highlight(int item, HTREEITEM node)
{
    browser::drop_highlight(g, item);
    if (node != g_drop_node && g.tree) { SendMessageW(g.tree, TVM_SELECTITEM, TVGN_DROPHILITE, (LPARAM)node); g_drop_node = node; }
}

// where a drop at d's point goes: a folder item of the list or a tree node (*item /
// *node to highlight), the list's background: the folder shown; "" elsewhere
static std::string drop_dest(const FLXPDROP* d, int* item, HTREEITEM* node)
{
    *item = -1; *node = nullptr;
    HWND t = d->hwndTarget;
    if (g.tree && (t == g.tree || IsChild(g.tree, t))) {
        TVHITTESTINFO ht = {}; ht.pt = { d->x, d->y }; ScreenToClient(g.tree, &ht.pt);
        HTREEITEM h = (HTREEITEM)SendMessageW(g.tree, TVM_HITTEST, 0, (LPARAM)&ht);
        const std::string* p = h && (ht.flags & TVHT_ONITEM) ? tree_path(h) : nullptr;
        if (!p || p->empty() || (*p)[0] != '/') return "";
        *node = h;
        return *p;
    }
    return browser::drop_dest(g, d, item);
}

static LRESULT drop_message(UINT msg, const FLXPDROP* d)
{
    if (msg == WM_FLXP_DRAGLEAVE) { drop_highlight(-1, nullptr); return 0; }
    int item; HTREEITEM node;
    std::string dest = drop_dest(d, &item, &node);
    DWORD e = verbs::drop_effect(d, dest);
    if (msg == WM_FLXP_DROP) {
        drop_highlight(-1, nullptr);    // the refill after the drop renumbers the items
        return e ? verbs::drop(d, dest) : 0;
    }
    drop_highlight(e ? item : -1, e ? node : nullptr);
    return e;
}

static void drag_from_list(int grabbed) { browser::drag_from_list(g, grabbed); }

static void drag_from_tree(HTREEITEM h)
{
    Item it = tree_item(h);
    if (!is_fs(it) || it.path == "/") return;
    verbs::drag_items(g.tree, { it }, g.large, { icon_for(it.icon) });
}

static LRESULT on_notify(NMHDR* nm)
{
    if (nm->hwndFrom == g.rebar && nm->code == RBN_HEIGHTCHANGE) { layout(); return 0; }
    if (nm->hwndFrom == g.list) {
        if (getenv("FLXP_DEBUG") && (nm->code == NM_CLICK || nm->code == NM_DBLCLK || nm->code == NM_RCLICK)) fprintf(stderr, "list notify %d\n", (int)nm->code);
        switch (nm->code) {
        case NM_DBLCLK: {
            NMITEMACTIVATE* ia = (NMITEMACTIVATE*)nm;
            if (getenv("FLXP_DEBUG")) fprintf(stderr, "list dblclk item %d\n", ia->iItem);
            open_item(ia->iItem);
            return 0;
        }
        case LVN_BEGINDRAG:
            drag_from_list(((NMLISTVIEW*)nm)->iItem);
            return 0;
        case LVN_ITEMCHANGED:
            update_status();
            return 0;
        case LVN_KEYDOWN: {
            NMLVKEYDOWN* kd = (NMLVKEYDOWN*)nm;
            if (kd->wVKey == VK_RETURN) { g_ctx = Ctx(); g_ctx.items = selection(); if (!g_ctx.items.empty()) verb(ID_OPEN); }
            else if (kd->wVKey == VK_BACK) go_up();
            else if (kd->wVKey == VK_F5) refresh();
            else if (kd->wVKey == VK_DELETE) { g_ctx = Ctx(); g_ctx.items = selection(); verb(ID_DELETE); }
            else if (kd->wVKey == VK_F2) { g_ctx = Ctx(); g_ctx.items = selection(); verb(ID_RENAME); }
            return 0;
        }
        case LVN_BEGINLABELEDITW: {
            NMLVDISPINFOW* di = (NMLVDISPINFOW*)nm;
            int i = di->item.iItem;
            return i < 0 || i >= (int)g.items.size() || !is_fs(g.items[(size_t)i]);   // TRUE cancels
        }
        case LVN_ENDLABELEDITW: {
            NMLVDISPINFOW* di = (NMLVDISPINFOW*)nm;
            int i = di->item.iItem;
            if (!di->item.pszText || i < 0 || i >= (int)g.items.size()) return FALSE;
            std::string from = g.items[(size_t)i].path, to;
            std::string name = w32::utf8_of((const WCHAR*)di->item.pszText);
            if (name == g.items[(size_t)i].name) return FALSE;
            // XP: refused or failed, the label is edited again with what was typed
            if (!fileopui::rename(g.frame, from, name, &to)) { fileopui::edit_again(g.list, i, name); return FALSE; }
            if (to == from) return FALSE;
            // refill after the list view has finished with its edit control
            g.select_after_refresh = to;
            g_reload_dir = shell::dir_of(from);
            renamed(from, to);
            PostMessageW(g.frame, WM_APP_REFRESH, 0, 0);
            return TRUE;
        }
        case LVN_COLUMNCLICK: {
            browser::sort_by_column(g, ((NMLISTVIEW*)nm)->iSubItem);
            // Arrange Icons by has the derived four (a stored column's sort checks none)
            for (int id = ID_SORT_NAME; id <= ID_SORT_DATE; id++)
                CheckMenuItem(g_arrange_menu, (UINT)id, MF_BYCOMMAND | (id - ID_SORT_NAME == (int)g.sort ? MF_CHECKED : MF_UNCHECKED));
            if ((int)g.sort <= 3) CheckMenuRadioItem(g_arrange_menu, ID_SORT_NAME, ID_SORT_DATE, ID_SORT_NAME + (int)g.sort, MF_BYCOMMAND);
            return 0;
        }
        }
    } else if (nm->hwndFrom == g.tree) {
        switch (nm->code) {
        case TVN_BEGINDRAGW:
            drag_from_tree(((NMTREEVIEWW*)nm)->itemNew.hItem);
            return 0;
        case TVN_ITEMEXPANDINGW: {
            NMTREEVIEWW* tv = (NMTREEVIEWW*)nm;
            if (tv->action == TVE_EXPAND) tree_fill(tv->itemNew.hItem);
            return FALSE;
        }
        case TVN_KEYDOWN: {
            NMTVKEYDOWN* kd = (NMTVKEYDOWN*)nm;
            HTREEITEM h = (HTREEITEM)SendMessageW(g.tree, TVM_GETNEXTITEM, TVGN_CARET, 0);
            if (!h || (kd->wVKey != VK_DELETE && kd->wVKey != VK_F2)) return 0;
            g_ctx = Ctx(); ctx_tree_node(h);
            verb(kd->wVKey == VK_DELETE ? ID_DELETE : ID_RENAME);
            return 0;
        }
        case TVN_BEGINLABELEDITW: {
            NMTVDISPINFOW* di = (NMTVDISPINFOW*)nm;
            const std::string* p = tree_path(di->item.hItem);
            const char* home = getenv("HOME");
            // the namespace's own nodes (Desktop, My Computer, Home, drives) keep their names
            return !p || (*p)[0] != '/' || *p == "/" || (home && *p == home) || di->item.hItem == g.tree_root;
        }
        case TVN_ENDLABELEDITW: {
            NMTVDISPINFOW* di = (NMTVDISPINFOW*)nm;
            const std::string* p = tree_path(di->item.hItem);
            if (!di->item.pszText || !p) return FALSE;
            std::string from = *p, to;
            std::string name = w32::utf8_of((const WCHAR*)di->item.pszText);
            if (name == shell::base_of(from)) return FALSE;
            if (!fileopui::rename(g.frame, from, name, &to) || to == from) return FALSE;
            g_reload_dir = shell::dir_of(from);
            renamed(from, to);
            PostMessageW(g.frame, WM_APP_REFRESH, 0, 0);
            return TRUE;
        }
        case TVN_SELCHANGEDW: {
            if (g.syncing_tree) return 0;
            NMTREEVIEWW* tv = (NMTREEVIEWW*)nm;
            const std::string* p = tree_path(tv->itemNew.hItem);
            if (p && (!g.folder || g.folder->path() != *p)) navigate(*p);
            return 0;
        }
        }
    } else if (lhtool::g_tb && nm->hwndFrom == lhtool::g_tb) {
        switch (nm->code) {
        case NM_CUSTOMDRAW:
            if (!lhbar::enabled()) return CDRF_DODEFAULT;
            return lhbar::custom_draw((NMTBCUSTOMDRAW*)nm, menuband::active(), lhtool::checked);
        case TBN_DROPDOWN: {
            NMTOOLBARW* t = (NMTOOLBARW*)nm;
            RECT r; SendMessageW(lhtool::g_tb, TB_GETRECT, (WPARAM)t->iItem, (LPARAM)&r);
            MapWindowPoints(lhtool::g_tb, nullptr, (POINT*)&r, 2);
            if (t->iItem == ID_VIEWS) views_menu(r.left, r.bottom);
            else if (t->iItem == ID_FILTER) browser::zoom_popup(g, g.frame, (r.left + r.right) / 2, r.bottom);   // 4051's slider
            return TBDDRET_DEFAULT;
        }
        case TBN_GETINFOTIPW: {
            NMTBGETINFOTIPW* ti = (NMTBGETINFOTIPW*)nm;
            const wchar_t* tip = ti->iItem == ID_STOP ? L"Stop" : ti->iItem == ID_SEARCH ? L"Search" : ti->iItem == ID_FOLDERS ? L"Folders" :
                                 ti->iItem == ID_VIEWS ? L"Views" : ti->iItem == ID_FILTER ? L"Zoom" : ti->iItem == ID_DETAILS_TOGGLE ? L"Preview" : L"";
            size_t n = 0; while (tip[n]) n++;
            if (ti->cchTextMax > (int)n) memcpy(ti->pszText, tip, (n + 1) * sizeof(wchar_t));
            return 0;
        }
        }
    } else if (nm->hwndFrom == g.toolbar) {
        switch (nm->code) {
        case TBN_DROPDOWN: {
            NMTOOLBARW* t = (NMTOOLBARW*)nm;
            RECT r; SendMessageW(g.toolbar, TB_GETRECT, (WPARAM)t->iItem, (LPARAM)&r);
            MapWindowPoints(g.toolbar, nullptr, (POINT*)&r, 2);
            if (t->iItem == ID_VIEWS) {
                views_menu(r.left, r.bottom);
            } else if (t->iItem == ID_BACK || t->iItem == ID_FORWARD) {
                history_menu(t->iItem == ID_BACK, r.left, r.bottom);
            }
            return TBDDRET_DEFAULT;
        }
        case TBN_GETINFOTIPW: {
            NMTBGETINFOTIPW* ti = (NMTBGETINFOTIPW*)nm;
            const wchar_t* tip = ti->iItem == ID_BACK ? L"Back" : ti->iItem == ID_FORWARD ? L"Forward" : ti->iItem == ID_UP ? L"Up" :
                                 ti->iItem == ID_SEARCH ? L"Search" : ti->iItem == ID_FOLDERS ? L"Folders" : L"Views";
            size_t n = 0; while (tip[n]) n++;
            if (ti->cchTextMax > (int)n) memcpy(ti->pszText, tip, (n + 1) * sizeof(wchar_t));
            return 0;
        }
        }
    } else if (nm->hwndFrom == g.address) {
        if (nm->code == CBEN_ENDEDITW) {
            NMCBEENDEDITW* ee = (NMCBEENDEDITW*)nm;
            if (ee->iWhy == CBENF_RETURN) {
                std::string path = w32::utf8_of((const WCHAR*)ee->szText);
                if (!path.empty() && path[0] == '~') { const char* h = getenv("HOME"); if (h) path = h + path.substr(1); }
                if (!navigate(path)) MessageBeep(0);
                return FALSE;
            }
        }
    }
    return 0;
}

// Tools > Folder Options: the default view of the frame (default_view or
// default_view_longhorn), Apply to All / Reset All Folders, remember_views, show_hidden
static void fo_apply_all(dialogs::FolderOptions& o)
{
    int v = o.current_view;
    browser::views::set_default(g.zoomable, v == browser::views::builtin_default(g.zoomable) ? -1 : v);
    browser::views::forget_all();
}

static void fo_reset_all(dialogs::FolderOptions&)
{
    browser::views::set_default(g.zoomable, -1);
    browser::views::forget_all();
}

static void fo_applied(dialogs::FolderOptions& o)
{
    browser::views::set_default(g.zoomable, o.default_view == browser::views::builtin_default(g.zoomable) ? -1 : o.default_view);
    shell::write_setting("remember_views", o.remember ? "1" : "0");
    if (o.full_path != (shell::setting("breadcrumb_home", "1") == "0")) {
        shell::write_setting("breadcrumb_home", o.full_path ? "0" : "1");
        if (g.header && g.folder) { header::set_folder(g.folder); header_refit(g.frame); }
    }
    if (o.hidden != g.show_hidden) on_command(ID_VIEW_HIDDEN);   // (writes show_hidden, refreshes)
    if (g.zoomable && o.thumbnails != (shell::setting("thumbnails", "1") != "0")) on_command(ID_VIEW_THUMBNAILS);
}

static void folder_options()
{
    dialogs::FolderOptions o;
    o.longhorn = g.zoomable;
    o.default_view = browser::views::default_view(g.zoomable);
    o.builtin_default = browser::views::builtin_default(g.zoomable);
    o.current_view = browser::view_code(g);
    if (!g.zoomable) o.current_view &= ZOOM_VIEW & ~VIEW_THUMBS_FLAG;
    o.zoom_levels.push_back(0);
    for (int z : browser::zoom_levels()) o.zoom_levels.push_back(z);
    o.remember = shell::setting("remember_views", "1") != "0";
    o.hidden = g.show_hidden;
    o.full_path = shell::setting("breadcrumb_home", "1") == "0";
    o.thumbnails = shell::setting("thumbnails", "1") != "0";
    o.apply_all = fo_apply_all;
    o.reset_all = fo_reset_all;
    o.applied = fo_applied;
    dialogs::folder_options(g.frame, o);
}

static void on_command(int id)
{
    if (header::menu_command((UINT)id, ID_VIEW_HEADER_FIRST)) {   // View > Toolbars
        if (g.header) header_refit(g.frame);
        return;
    }
    if (browser::zoom_command(g, id, ID_VIEW_ZOOM_FIRST)) return;   // View > Zoom
    switch (id) {
    case ID_BACK: go_back(); break;
    case ID_SEARCH:   // the 4051 bar's Search / Filter by Name: the header's search box
        if (g.header) header::open_search();
        break;
    case ID_FILTER_CLEAR:
        if (!g.filter.empty()) { header::clear_filter(); g.filter.clear(); apply_filter(); }
        break;
    case ID_DETAILS_TOGGLE: on_command(ID_VIEW_PREVIEW); break;   // the 4051 bar's preview pane button
    case ID_STOP: thumbview::stop(); lhtool::changed(); break;
    case ID_FORWARD: go_forward(); break;
    case ID_UP: go_up(); break;
    case ID_FOLDERS:
    case ID_VIEW_FOLDERS: {
        g.show_folders = !g.show_folders;
        if (!g.folders_once) shell::write_setting("folders", g.show_folders ? "1" : "0");   // remembered (Explore is not)
        SendMessageW(g.toolbar, TB_CHECKBUTTON, ID_FOLDERS, MAKELONG(g.show_folders, 0));
        lhtool::changed();
        CheckMenuItem(g_xbar_menu, ID_VIEW_FOLDERS, MF_BYCOMMAND | (g.show_folders ? MF_CHECKED : MF_UNCHECKED));
        layout();
        break;
    }
    case ID_VIEW_PLACES_ICONS:
        places::set_icons(!places::icons());
        CheckMenuItem(g_xbar_menu, ID_VIEW_PLACES_ICONS, MF_BYCOMMAND | (places::icons() ? MF_CHECKED : MF_UNCHECKED));
        break;
    case ID_VIEW_PREVIEW:       // 4051: the band expanded or compact (the band comes with it when off)
        if (!g.longhorn) break;
        if (!g.taskband) { g.show_taskpane = true; shell::write_setting("taskpane", "1"); taskpane::set_compact(false); }
        else taskpane::set_compact(!taskpane::compact());
        sync_taskpane();
        layout();
        break;
    case ID_VIEW_TASKPANE:
        g.show_taskpane = !g.show_taskpane;
        shell::write_setting("taskpane", g.show_taskpane ? "1" : "0");   // remembered: it takes room
        sync_taskpane();
        layout();
        break;
    case ID_VIEW_STATUSBAR:
        g.show_status = !g.show_status;
        shell::write_setting(g.longhorn ? "statusbar_longhorn" : "statusbar", g.show_status ? "1" : "0");
        CheckMenuItem(GetSubMenu(g.menu, 2), ID_VIEW_STATUSBAR, MF_BYCOMMAND | (g.show_status ? MF_CHECKED : MF_UNCHECKED));
        layout();
        break;
    case ID_VIEW_THUMBNAILS:      // Longhorn: the thumbnails setting
        if (g.zoomable) { browser::view_command(g, browser::VC_THUMBNAILS); sync_thumbs_mode(); }
        else set_view(VIEW_THUMBNAILS);
        break;
    case ID_VIEW_DEFAULT: browser::view_key(g, '0'); break;
    case ID_VIEW_BIGICONS: browser::view_key(g, '3'); break;
    case ID_TOOLS_FOLDEROPTIONS: folder_options(); break;
    case ID_VIEW_ICONS: if (!browser::view_command(g, browser::VC_ICONS)) set_view(LVS_ICON); break;
    case ID_VIEW_LIST: if (!browser::view_command(g, browser::VC_DETAILS)) set_view(LVS_LIST); break;
    case ID_VIEW_SMALLICONS: if (!browser::view_command(g, browser::VC_SMALL)) set_view(LVS_SMALLICON); break;
    case ID_VIEW_DETAILS: if (!browser::view_command(g, browser::VC_DETAILS)) set_view(LVS_REPORT); break;
    case ID_VIEW_TILES: if (!browser::view_command(g, browser::VC_TILES)) set_view(VIEW_TILES); break;
    case ID_VIEW_LARGEICONS: browser::view_command(g, browser::VC_LARGE); break;
    case ID_VIEW_MEDIUMICONS: browser::view_command(g, browser::VC_MEDIUM); break;
    case ID_VIEW_HIDENAMES: browser::view_command(g, browser::VC_HIDENAMES); break;
    case ID_VIEW_GROUPS:
        browser::set_grouped(g, !g.grouped);
        shell::write_setting("groups", g.grouped ? "1" : "0");
        break;
    case ID_SORT_NAME: case ID_SORT_SIZE: case ID_SORT_TYPE: case ID_SORT_DATE:
        g.sort = (shell::SortBy)(id - ID_SORT_NAME); g.ascending = true;
        CheckMenuRadioItem(g_arrange_menu, ID_SORT_NAME, ID_SORT_DATE, id, MF_BYCOMMAND);
        fill_list();
        break;
    case ID_VIEW_HIDDEN:
        g.show_hidden = !g.show_hidden;
        shell::write_setting("show_hidden", g.show_hidden ? "1" : "0");   // global, as Folder Options' setting
        CheckMenuItem(GetSubMenu(g.menu, 2), ID_VIEW_HIDDEN, MF_BYCOMMAND | (g.show_hidden ? MF_CHECKED : MF_UNCHECKED));
        refresh();
        break;
    case ID_VIEW_REFRESH: refresh(); break;
    case ID_VIEW_CHOOSEDETAILS: browser::choose_details(g, g.frame); break;
    case ID_TB_LARGE: set_toolbar_size(true); shell::write_setting("toolbar_icons", "large"); break;
    case ID_TB_ADDRESS: {
        HMENU bars = GetSubMenu(GetSubMenu(g.menu, 2), 0);
        bool show = !(GetMenuState(bars, ID_TB_ADDRESS, MF_BYCOMMAND) & MF_CHECKED);
        CheckMenuItem(bars, ID_TB_ADDRESS, MF_BYCOMMAND | (show ? MF_CHECKED : MF_UNCHECKED));
        SendMessageW(g.rebar, RB_SHOWBAND, (WPARAM)SendMessageW(g.rebar, RB_IDTOINDEX, IDC_ADDRESS, 0), show);
        layout();
        break;
    }
    case ID_TB_SMALL: set_toolbar_size(false); shell::write_setting("toolbar_icons", "small"); break;
    case ID_EDIT_SELECTALL: {
        LVITEMW lvi = {}; lvi.stateMask = LVIS_SELECTED; lvi.state = LVIS_SELECTED;
        SendMessageW(g.list, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&lvi);
        break;
    }
    case ID_EDIT_INVERT: {
        int n = (int)SendMessageW(g.list, LVM_GETITEMCOUNT, 0, 0);
        for (int i = 0; i < n; i++) {
            UINT st = (UINT)SendMessageW(g.list, LVM_GETITEMSTATE, (WPARAM)i, LVIS_SELECTED);
            LVITEMW lvi = {}; lvi.stateMask = LVIS_SELECTED; lvi.state = st ? 0 : LVIS_SELECTED;
            SendMessageW(g.list, LVM_SETITEMSTATE, (WPARAM)i, (LPARAM)&lvi);
        }
        break;
    }
    case ID_HELP_ABOUT:
        MessageBoxW(g.frame, L"flxp explorer: a re-interpretation of the Windows XP Explorer on a unix-native toolkit.", L"About flxp", MB_OK);
        break;
    case ID_FILE_CLOSE: DestroyWindow(g.frame); break;
    case ID_CUT: case ID_COPY:
        g_ctx = Ctx(); g_ctx.items = selection(); verb(id); break;
    case ID_PASTE:   // Edit > Paste and Ctrl+V: into the folder shown
        g_ctx = Ctx(); verb(id); break;
    case ID_BM_ADD: verbs::add_bookmark(current_dir()); places::bookmarks_changed(); break;
    case ID_BM_ORGANIZE: dialogs::organize_bookmarks(g.frame, g.small, icon_for("folder")); places::bookmarks_changed(); break;
    default:
        if (id >= ID_BM_FIRST && id <= ID_BM_LAST) go_bookmark((size_t)(id - ID_BM_FIRST));
        else if (id >= ID_OPEN && id <= ID_OPENWITH_LAST) verb(id);   // from the File menu (g_ctx set when it opened)
        break;
    }
}

// Active or inactive as the window manager draws the frame: the root
// _NET_ACTIVE_WINDOW (lhwm sets it when it paints a frame active), not our
// focus, which differs e.g. while the desktop window or a popup holds the
// focus. The header (navigation bar variant, title colours, lhwm's pixmap
// variant) and the menu bar follow it; without an EWMH window manager the
// focus (WM_ACTIVATE) decides.
static void sync_active()
{
    if (!g.frame) return;
    bool active = browser::frame_active(g.frame, g.focus_active);
    menuband::on_activate(active);
    if (g.header) header::set_active(active);
}

// frame= xp | longhorn | auto (default): browser.h
static bool frame_auto() { return browser::frame_auto(); }
static bool want_longhorn(std::string* why = nullptr) { return browser::want_longhorn(why); }

// A theme switch between an XP and a Longhorn entry: the header, the 4051
// command bar and the places pane's look come or go; the toolbar, Address
// and brand bands of the XP look go or come back. Nothing is recreated
// besides those.
// The task band (Longhorn only): there while wanted and in the Longhorn frame
static void taskpane_command(int id, bool on_selection)
{
    g_ctx = Ctx();
    if (on_selection) g_ctx.items = selection();
    if (id == ID_SEARCH) on_command(ID_SEARCH);
    else verb(id);
}

static void taskpane_more(bool on_selection, int x, int y)
{
    g_ctx = Ctx();
    HMENU m = CreatePopupMenu();
    if (on_selection && !(g_ctx.items = selection()).empty()) verbs::build_item_menu(m);
    else verbs::build_background_menu(m, verbs::BG_VIEW | verbs::BG_ARRANGE | verbs::BG_THUMBNAILS, menu_view(), (int)g.sort);
    track(m, POINT{ x, y });
}

static void sync_taskpane()
{
    bool want = g.longhorn && g.show_taskpane;
    if (want && !g.taskband) {
        taskpane::Callbacks cb = { taskpane_command, taskpane_more, taskpane_resized };
        g.taskband = taskpane::create(g.frame, cb);
        if (g.folder) taskpane::set_folder(g.folder);
        places::set_top_inset(taskpane::bottom() ? 0 : 14);   // the band's preview hangs over the left pane's top
        schedule_details();
    } else if (!want && g.taskband) {
        taskpane::destroy();
        g.taskband = nullptr;
        places::set_top_inset(0);
    }
    HMENU view = GetSubMenu(g.menu, 2);
    EnableMenuItem(view, ID_VIEW_TASKPANE, MF_BYCOMMAND | (g.longhorn ? MF_ENABLED : MF_GRAYED));
    CheckMenuItem(view, ID_VIEW_TASKPANE, MF_BYCOMMAND | (g.show_taskpane ? MF_CHECKED : MF_UNCHECKED));
    EnableMenuItem(view, ID_VIEW_PREVIEW, MF_BYCOMMAND | (g.longhorn ? MF_ENABLED : MF_GRAYED));
    CheckMenuItem(view, ID_VIEW_PREVIEW, MF_BYCOMMAND | (g.taskband && !taskpane::compact() ? MF_CHECKED : MF_UNCHECKED));
    lhtool::changed();
    EnableMenuItem(g_xbar_menu, ID_VIEW_PLACES_ICONS, MF_BYCOMMAND | (g.longhorn && places::window() ? MF_ENABLED : MF_GRAYED));
    CheckMenuItem(g_xbar_menu, ID_VIEW_PLACES_ICONS, MF_BYCOMMAND | (places::icons() ? MF_CHECKED : MF_UNCHECKED));
}

static void set_frame_mode(bool lh)
{
    if (lh == g.longhorn || !g.frame) return;
    g.longhorn = lh;
    g.show_status = status_setting();
    CheckMenuItem(GetSubMenu(g.menu, 2), ID_VIEW_STATUSBAR, MF_BYCOMMAND | (g.show_status ? MF_CHECKED : MF_UNCHECKED));
    sync_thumbs_mode();
    verbs::check_view_items(GetSubMenu(g.menu, 2), menu_view());
    HWND frame = g.frame;
    HMENU bars = GetSubMenu(GetSubMenu(g.menu, 2), 0);
    if (lh) {
        header::Callbacks cb = { header_navigate, header_filter, header_done, header_command, header_history, header_favorites, header_tool };
        g.header = header::create(frame, cb);
        extend_frame(frame);
        brand::destroy(g.rebar);
        SendMessageW(g.rebar, RB_SHOWBAND, (WPARAM)SendMessageW(g.rebar, RB_IDTOINDEX, IDC_ADDRESS, 0), FALSE);
        ModifyMenuW(bars, 1, MF_BYPOSITION | MF_STRING | MF_UNCHECKED, ID_TB_ADDRESS, L"&Address Bar");
        lhtool::create();
        if (!g_viewstrip) g_viewstrip = viewbutton::create(frame, g);
        header::set_history(!g.back.empty(), !g.forward.empty());
        if (g.folder) header::set_folder(g.folder);
        sync_active();
    } else {
        lhtool::destroy();
        viewbutton::destroy(g_viewstrip);
        g_viewstrip = nullptr;
        header::destroy();
        g.header = nullptr;
        if (!g.filter.empty()) { g.filter.clear(); apply_filter(); }
        browser::unextend_frame(frame);                 // lhwm's own caption with title and icon again
        SendMessageW(g.rebar, RB_SHOWBAND, (WPARAM)SendMessageW(g.rebar, RB_IDTOINDEX, IDC_TOOLBAR, 0), TRUE);
        SendMessageW(g.rebar, RB_SHOWBAND, (WPARAM)SendMessageW(g.rebar, RB_IDTOINDEX, IDC_ADDRESS, 0), TRUE);
        ModifyMenuW(bars, 1, MF_BYPOSITION | MF_STRING | MF_CHECKED | MF_GRAYED, 0, L"&Address Bar");
        brand::create(g.rebar);
    }
    places::set_longhorn(lh);
    sync_taskpane();
    // the folder in the frame's views (XP's or the Longhorn ones), as navigating to it would
    if (g.folder) {
        int v = g.remember_views ? browser::views::lookup(g.folder->path(), g.zoomable) : -1;
        set_view(v >= 0 ? v : browser::views::default_view(g.zoomable), true);
        fill_list();
    }
    layout();
    RedrawWindow(frame, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

// View > Toolbars: Standard Buttons, Address Bar, a separator, then what sizes
// the frame's toolbar has, as that era named it, and nothing that does not
// apply: XP's Icon Options (Large Icons / Small Icons, toolbar_icons) or the
// Longhorn header's elements and 4074's Toolbar Size (header_size)
static void fill_bars_menu()
{
    HMENU bars = g_bars_menu;
    if (!bars) return;
    while (GetMenuItemCount(bars) > 3) {
        if (GetSubMenu(bars, 3) == g_icon_menu) RemoveMenu(bars, 3, MF_BYPOSITION);   // kept for the XP frame
        else DeleteMenu(bars, 3, MF_BYPOSITION);
    }
    if (g.header) header::append_menu(bars, ID_VIEW_HEADER_FIRST);
    else AppendMenuW(bars, MF_POPUP, (UINT_PTR)g_icon_menu, L"&Icon Options");
}

static LRESULT CALLBACK frame_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        create_children(h);
        return 0;
    case WM_SIZE:
        layout();
        if (g.header) caption_hole(h);     // the input shape is the window's size
        return 0;
    case WM_NCHITTEST:
        if (g.header) {
            // the header's empty parts (it answers HTTRANSPARENT there) are the caption
            LRESULT ht;
            if (browser::header_hittest(h, lp, &ht)) return ht;
        }
        break;
    case WM_DWMCOMPOSITIONCHANGED:          // glass or the opaque header
    case WM_DWMCOLORIZATIONCOLORCHANGED:    // another lhwm theme: its caption height decides the header's
        // luxwm started after us (Explorer.chrome auto): the header now; it
        // is not taken away when lhwm goes (a restart deletes its properties)
        if (!g.longhorn && frame_auto() && want_longhorn()) set_frame_mode(true);
        if (g.header) header_refit(h);
        sync_active();
        return 0;
    case WM_FLXP_DWMCAPTIONRECTCHANGED:     // CLIENT_CAPTION: where lhwm's caption buttons are now
        caption_hole(h);
        return 0;
    case WM_FLXP_DWMSTATECHANGED:           // maximised/restored: lhwm's maximised caption colours
        if (g.header) header::state_changed();
        return 0;
    case WM_FLXP_DWMHEADERCHANGED:          // lhwm re-rendered the header background (focus, size, theme)
        if (g.header) header::pixmap_changed();
        return 0;
    case WM_THEMECHANGED:                   // the session theme: the header's images, the command bar
        fonts_changed();                    // the style's fonts (tree, list, status bar, menu band)
        menuband::fonts_changed();
        lhbar::theme_changed();             // before a 4051 command bar is made for this theme
        taskpane::theme_changed();          // (Explorer.taskpane: the band at the top or the bottom)
        if (g.taskband) { places::set_top_inset(taskpane::bottom() ? 0 : 14); layout(); }
        set_frame_mode(want_longhorn());    // Explorer.chrome (Luna: the XP explorer)
        icons_changed();                    // the theme's icon set (Desktop.icons)
        if (g.header) {
            header::theme_changed();
            header_refit(h);
            RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        }
        sync_active();
        break;
    case WM_NOTIFY: {
        bool handled; LRESULT r = menuband::on_notify((NMHDR*)lp, &handled);
        if (handled) return r;
        return on_notify((NMHDR*)lp);
    }
    case WM_MENUSELECT:
        menuband::on_menuselect(wp, lp);
        return 0;
    case WM_INITMENUPOPUP:
        if ((HMENU)wp == g.bookmarks_menu) init_bookmarks_menu((HMENU)wp);
        else if ((HMENU)wp == g.file_menu) init_file_menu((HMENU)wp);
        else if ((HMENU)wp == g.edit_menu) init_edit_menu((HMENU)wp);
        else if ((HMENU)wp == g_zoom_menu) browser::zoom_menu(g, g_zoom_menu, ID_VIEW_ZOOM_FIRST);
        else if ((HMENU)wp == g_bars_menu) fill_bars_menu();
        else if ((HMENU)wp == GetSubMenu(g.menu, 2)) {
            HMENU view = (HMENU)wp;
            for (int i = 0; i < GetMenuItemCount(view); i++)
                if (GetSubMenu(view, i) == g_zoom_menu) EnableMenuItem(view, (UINT)i, MF_BYPOSITION | (g.zoomable ? MF_ENABLED : MF_GRAYED));
            fill_bars_menu();
            view_items(view);
            EnableMenuItem(view, ID_SEARCH, MF_BYCOMMAND | (g.header ? MF_ENABLED : MF_GRAYED));        // the header's search box filters
            EnableMenuItem(view, ID_FILTER_CLEAR, MF_BYCOMMAND | (g.header && !g.filter.empty() ? MF_ENABLED : MF_GRAYED));
        }
        return 0;
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT* mi = (MEASUREITEMSTRUCT*)lp;
        if (mi->CtlType != ODT_MENU) break;
        mi->itemWidth = 16; mi->itemHeight = 16;   // HBMMENU_CALLBACK: a small icon
        return TRUE;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lp;
        if (di->CtlType != ODT_MENU) break;
        int idx = (int)di->itemData - 1;
        if (idx >= 0) {
            int x = di->rcItem.left + (di->rcItem.right - di->rcItem.left - 16) / 2, y = di->rcItem.top + (di->rcItem.bottom - di->rcItem.top - 16) / 2;
            ImageList_Draw(g.small, idx, di->hDC, x, y, ILD_TRANSPARENT);
        }
        return TRUE;
    }
    case WM_CONTEXTMENU: {
        HWND from = (HWND)wp;
        POINT pt = { (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
        if (from == g.list || from == g.tree) { context_menu(from, pt); return 0; }
        break;
    }
    case thumbview::WM_THUMBS:              // thumbnails finished on the worker threads
        thumbview::on_ready();
        return 0;
    case WM_FLXP_DRAGOVER:                  // drops on the list, the tree (the header and places pane answer themselves)
    case WM_FLXP_DROP:
    case WM_FLXP_DRAGLEAVE:
        return drop_message(msg, (const FLXPDROP*)lp);
    case WM_APP_DETAILS:
        update_details();
        return 0;
    case WM_APP_REFRESH:
        refresh();
        if (!g_reload_dir.empty()) { tree_reload(g_reload_dir); g_reload_dir.clear(); }
        return 0;
    case WM_ACTIVATE:
        g.focus_active = LOWORD(wp) != WA_INACTIVE;
        sync_active();
        break;
    case WM_FLXP_ROOTPROPERTY:
        if ((UINT)wp == g.w_active) sync_active();
        if ((UINT)wp == g.w_settings) {     // _LH_SETTINGS_CHANGED: the theme's icons or frame overridden
            icons_changed();
            set_frame_mode(want_longhorn());
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_KEYMENU && menuband::key_menu((WCHAR)lp)) return 0;
        break;
    case WM_COMMAND:
        if (lhtool::g_tb && (HWND)lp == lhtool::g_tb && LOWORD(wp) == ID_DELETE) {   // the 4051 bar: the selection
            g_ctx = Ctx(); g_ctx.items = selection();
            if (!g_ctx.items.empty()) verb(ID_DELETE);
            return 0;
        }
        on_command(LOWORD(wp));
        return 0;
    case WM_SETFOCUS:
        SetFocus(g.list);
        return 0;
    case WM_LBUTTONDOWN: {
        POINT pt = { (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
        if ((g.show_folders || places::window()) && pt.x >= g.tree_width && pt.x < g.tree_width + 4) { g.dragging_splitter = true; SetCapture(h); }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g.dragging_splitter) {
            int x = (SHORT)LOWORD(lp);
            RECT rc; GetClientRect(h, &rc);
            if (x > 60 && x < rc.right - 60) { g.tree_width = x; layout(); }
        }
        return 0;
    case WM_LBUTTONUP:
        if (g.dragging_splitter) { g.dragging_splitter = false; ReleaseCapture(); }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(h, &pt);
            if ((g.show_folders || places::window()) && pt.x >= g.tree_width && pt.x < g.tree_width + 4) { SetCursor(LoadCursorW(nullptr, (LPCWSTR)IDC_SIZEWE)); return TRUE; }
        }
        break;
    case WM_SYSKEYDOWN:
        if (wp == VK_LEFT) { go_back(); return 0; }
        if (wp == VK_RIGHT) { go_forward(); return 0; }
        break;
    case WM_DESTROY:
        if (g.list) browser::save_columns(g);   // (the widths dragged to)
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void CALLBACK quit_timer(HWND h, UINT, UINT_PTR, DWORD) { DestroyWindow(h); }

static void usage(FILE* f)
{
    fprintf(f,
        "usage: flxp [options] [DIR|FILE...]\n"
        "Open DIR in an Explorer window (default: the current directory); a FILE is\n"
        "opened with its default application instead.\n"
        "  --theme NAME|PATH   visual style (luna, a name in the theme directories, a .msstyles file)\n"
        "  --classic           no visual style\n"
        "  --icons NAME|PATH   icon theme: xp, 2k, longhorn, wine or a .theme file\n"
        "  --small-toolbar     small toolbar buttons\n"
        "  --frame xp|longhorn|auto  window frame: XP (the window manager's), the Longhorn\n"
        "                      4074 header on lhwm's glass, or longhorn when lhwm composites\n"
        "  --longhorn          the same as --frame longhorn\n"
        "  --taskpane          Longhorn: the task band under the command bar (taskpane=1)\n"
        "  --geometry WxH+X+Y  window client size and position\n"
        "  --info              print the settings, data directory, fonts and theme found, and exit\n"
        "  --help              this text\n"
        "Options override %s for this run; every key there can also be set\n"
        "with the environment variable FLXP_<KEY>.\n", shell::settings_path().c_str());
}

static void print_info()
{
    printf("settings     %s\n", shell::settings_path().c_str());
    std::string icons = shell::data_path("res/icons");
    printf("data         %s\n", icons.empty() ? "(not found)" : shell::dir_of(shell::dir_of(icons)).c_str());
    std::string theme = getenv("FLXP_THEME") ? getenv("FLXP_THEME") : "";
    printf("theme        %s\n", theme.empty() ? "classic" : theme.c_str());
    const shell::SessionTheme& st = shell::session_theme();
    if (!st.name.empty()) printf("session      %s (%s, followed; style %s, chrome %s, navbar %s %s)\n", st.name.c_str(), st.value("file").c_str(),
                                 st.msstyles.empty() ? "classic" : st.msstyles.c_str(), st.value("Explorer.chrome").c_str(),
                                 st.value("Explorer.navbar").c_str(), st.value("Explorer.navbar_preset").c_str());
    else printf("session      %s\n", shell::follows_session_theme() ? "(no _LH_THEME)" : "(not followed: theme_follow=0 or --theme)");
    printf("framelets    %s\n", header::framelet_source().c_str());
    printf("%s", shell::lhde_report().c_str());   // lhde's data directories: what each lookup found
    const shell::IconTheme& it = shell::icon_theme();
    printf("icons        %s%s\n", it.name.c_str(), it.toolbar.valid() ? "" : " (comctl32 toolbar images)");
    for (const shell::FontInfo& f : shell::font_report())
        printf("font         %-22s %s%s\n", f.family.c_str(), f.file.c_str(), f.exact ? "" : "  (substitute)");
    // what the window manager says: needs the display
    const char* d = getenv("DISPLAY");
    if (!d || !*d) { printf("display      (none: frame, composition and header unknown)\n"); return; }
    fflush(stdout);
    w32::init();
    std::string why;
    bool lh = want_longhorn(&why);
    printf("frame        %s (%s)\n", lh ? "longhorn: the 4074 header" : "xp: toolbar and Address bar", why.c_str());
    BOOL comp = FALSE;
    DwmIsCompositionEnabled(&comp);
    long c = -1;
    int nc = FlxpGetRootCardinals("_LHWM_COMPOSITED", &c, 1);
    std::string lt = win::frame_theme();
    printf("composited   %s (_LHWM_COMPOSITED %s%s%s)\n", comp ? "yes" : "no", nc < 1 ? "absent" : c ? "1" : "0",
           lt.empty() ? "" : ", lhwm theme ", lt.c_str());
    if (lh) printf("header       %s\n", header::background_source().c_str());
    else printf("header       (none in the xp frame)\n");
    if (lh) printf("buttons      %s\n", header::buttons_source().c_str());
    if (lh) printf("%s", header::metrics_info().c_str());
    if (lh) printf("task band    %s, %s; %s\n", taskpane::wanted() ? "on" : "off (taskpane=1, --taskpane or View > Task Band)",
                   shell::theme_setting("taskpane_position", "top") == "bottom" ? "under the list (Explorer.taskpane = bottom)"
                                                                               : "under the command bar", taskpane::source().c_str());
}

// the keys the frame handles before a control sees them (also while a file operation runs: fileopui)
static bool loop_keys(MSG& msg)
{
    HWND focus = GetFocus();
    bool in_view = focus == g.list || focus == g.tree;
    if (msg.message == WM_KEYDOWN && msg.wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) && in_view) { on_command(ID_EDIT_SELECTALL); return true; }
    if (in_view && ((msg.message == WM_KEYDOWN && msg.wParam == VK_F10 && (GetKeyState(VK_SHIFT) & 0x8000)) ||
                    (msg.message == WM_KEYUP && msg.wParam == VK_APPS))) {
        context_menu(focus, POINT{ -1, -1 });
        return true;
    }
    if (in_view && msg.message == WM_KEYDOWN && msg.wParam == VK_APPS) return true;
    if (in_view && msg.message == WM_KEYUP && msg.wParam == VK_F10 && (GetKeyState(VK_SHIFT) & 0x8000)) return true;   // no menu-bar activation after Shift+F10
    if (focus == g.list && msg.message == WM_SYSKEYDOWN && msg.wParam == VK_RETURN) {   // Alt+Enter: Properties
        g_ctx = Ctx(); g_ctx.items = selection(); verb(ID_PROPERTIES);
        return true;
    }
    if (focus == g.list && msg.message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000) && (msg.wParam == 'X' || msg.wParam == 'C' || msg.wParam == 'V')) {
        on_command(msg.wParam == 'X' ? ID_CUT : msg.wParam == 'C' ? ID_COPY : ID_PASTE);
        return true;
    }
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_F5) { refresh(); return true; }
    if (g.header && msg.message == WM_KEYDOWN && msg.wParam == 'L' && (GetKeyState(VK_CONTROL) & 0x8000)) { header::edit_address(); return true; }
    if (g.zoomable && msg.message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000) && (focus == g.list || focus == g.frame) &&
        (msg.wParam == VK_OEM_PLUS || msg.wParam == VK_ADD || msg.wParam == VK_OEM_MINUS || msg.wParam == VK_SUBTRACT)) {
        browser::zoom_step(g, msg.wParam == VK_OEM_PLUS || msg.wParam == VK_ADD ? 1 : -1);
        return true;
    }
    if (msg.message == WM_KEYDOWN && msg.wParam >= '0' && msg.wParam <= '5' && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) {
        // XP: Ctrl+0 default view, 1 Details, 2 Icons, 3 Thumbnails (browser::view_key), 4 List, 5 Small Icons;
        // Longhorn: 0 default view, 1..5 the slider's stops (Details, Tiles, Large, Medium, Small Icons)
        static const int keys[] = { ID_VIEW_DEFAULT, ID_VIEW_DETAILS, ID_VIEW_ICONS, ID_VIEW_BIGICONS, ID_VIEW_LIST, ID_VIEW_SMALLICONS };
        if (!g.zoomable || !browser::view_key(g, (int)msg.wParam)) on_command(keys[msg.wParam - '0']);
        return true;
    }
    return false;
}

int main(int argc, char** argv)
{
    // Command line: options become FLXP_<KEY> overrides for this run (children inherit them).
    std::vector<std::string> args;
    bool info = false;
    int quit_ms = 0;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto value = [&](const char* opt) -> const char* {
            if (i + 1 >= argc) { fprintf(stderr, "flxp: %s needs a value\n", opt); exit(2); }
            return argv[++i];
        };
        size_t eq = a.find('=');
        std::string opt = a.rfind("--", 0) == 0 && eq != std::string::npos ? a.substr(0, eq) : a;
        auto val = [&](const char* o) -> std::string { return eq != std::string::npos && opt != a ? a.substr(eq + 1) : value(o); };
        if (opt == "--help" || opt == "-h") { usage(stdout); return 0; }
        else if (opt == "--theme") setenv("FLXP_THEME", val("--theme").c_str(), 1), unsetenv("FLXP_THEME_FOLLOWED");
        else if (opt == "--classic") setenv("FLXP_THEME", "classic", 1), unsetenv("FLXP_THEME_FOLLOWED");
        else if (opt == "--icons") setenv("FLXP_ICONS", val("--icons").c_str(), 1);
        else if (opt == "--small-toolbar") setenv("FLXP_TOOLBAR_ICONS", "small", 1);
        else if (opt == "--longhorn") setenv("FLXP_FRAME", "longhorn", 1);
        else if (opt == "--taskpane") setenv("FLXP_TASKPANE", "1", 1);
        else if (opt == "--frame") setenv("FLXP_FRAME", val("--frame").c_str(), 1);
        else if (opt == "--geometry") setenv("FLXP_GEOMETRY", val("--geometry").c_str(), 1);
        else if (opt == "--info") info = true;
        else if (opt == "--settings") {   // Display Properties: flxp --settings [--tab NAME]
            std::string prog = shell::store::settings_program();
            std::vector<char*> av = { (char*)prog.c_str() };
            for (i++; i < argc; i++) av.push_back(argv[i]);
            av.push_back(nullptr);
            execvp(av[0], av.data());
            fprintf(stderr, "flxp: cannot run %s\n", prog.c_str());
            return 1;
        }
        else if (opt == "--quit-after") quit_ms = atoi(val("--quit-after").c_str());   // testing
        else if (opt == "--") { for (i++; i < argc; i++) args.push_back(argv[i]); }
        else if (a.size() > 1 && a[0] == '-') { fprintf(stderr, "flxp: unknown option %s\n", a.c_str()); usage(stderr); return 2; }
        else args.push_back(a);
    }
    // legacy test form: explorer DIR QUIT_MS
    struct stat st0;
    if (args.size() == 2 && stat(args[1].c_str(), &st0) != 0 && !args[1].empty() && strspn(args[1].c_str(), "0123456789") == args[1].size()) {
        quit_ms = atoi(args[1].c_str());
        args.pop_back();
    }

    if (shell::write_settings_template() && !info)
        fprintf(stderr, "flxp: wrote %s (every setting, empty = automatic)\n", shell::settings_path().c_str());

    // Split the arguments: folders get windows, files are opened.
    std::vector<std::string> dirs, files;
    for (std::string a : args) {
        std::string p = shell::path_from_uri(a);
        if (!p.empty()) a = p;
        struct stat st;
        if (a.rfind("::", 0) == 0 || (stat(a.c_str(), &st) == 0 && S_ISDIR(st.st_mode))) dirs.push_back(a);
        else if (stat(a.c_str(), &st) == 0) files.push_back(a);
        else fprintf(stderr, "flxp: %s: no such file or folder\n", a.c_str());
    }
    if (!args.empty() && dirs.empty() && files.empty()) return 1;
    std::string open_err;
    if (info) files.clear(), dirs.clear();
    if (!files.empty() && !shell::open_files(files, &open_err)) fprintf(stderr, "flxp: %s\n", open_err.c_str());
    if (!files.empty() && dirs.empty()) {
        if (open_err.empty()) return 0;
        dirs.push_back(shell::dir_of(files[0]));   // nothing could open it: show where it is
    }
    for (size_t i = 1; i < dirs.size(); i++) {   // one window per process
        std::string err;
        shell::spawn({ "/proc/self/exe", dirs[i] }, shell::home_dir(), &err);
    }

    shell::setup_fonts();
    shell::setup_theme();
    if (info) { print_info(); return 0; }
    w32::init();
    verbs::set_host(&g_host);
    {
        std::string ts = shell::setting("toolbar_icons");
        if (!ts.empty()) g.large_toolbar = ts != "small";
        g.longhorn = want_longhorn();
        sync_thumbs_mode();
        g.show_status = status_setting();
        g.show_folders = shell::setting("folders", "0") == "1";
        g.show_hidden = shell::setting("show_hidden", "0") == "1";   // Folder Options, View > Show Hidden Files
    }
    WNDCLASSW wc = {};
    wc.lpfnWndProc = frame_proc;
    wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"flxp_explorer";
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassW(&wc);
    std::string start = dirs.empty() ? std::string() : dirs[0];
    if (start.empty()) {     // no folder given: the current directory (Windows' Explorer would open Documents)
        char cwd[4096];
        start = getcwd(cwd, sizeof cwd) ? cwd : shell::home_dir();
    }
    int gx = 100, gy = 60, gw = 780, gh = 520;
    std::string geo = shell::setting("geometry");
    if (!geo.empty()) sscanf(geo.c_str(), "%dx%d+%d+%d", &gw, &gh, &gx, &gy);   // client size and position
    HWND frame = CreateWindowExW(0, L"flxp_explorer", L"flxp", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, gx, gy, gw, gh, nullptr, nullptr, nullptr, nullptr);
    if (const char* b = getenv("FLXP_BACK")) navigate(b);   // testing: a page to go back to (screenshots compare with Back enabled)
    if (!navigate(start)) navigate("::desktop");
    g.w_active = FlxpWatchRootProperty("_NET_ACTIVE_WINDOW");   // lhwm's active frame (sync_active)
    g.w_settings = FlxpWatchRootProperty("_LH_SETTINGS_CHANGED");
    ShowWindow(frame, SW_SHOW);
    sync_active();
    SetFocus(g.list);
    if (!open_err.empty()) error_box(open_err);
    if (quit_ms) SetTimer(frame, 1, (UINT)quit_ms, quit_timer);
    fileopui::set_message_filter(loop_keys);   // the same keys while a copy runs
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (loop_keys(msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
