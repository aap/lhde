// Geometry measured on the reference (~/fun/longhorn/screenshots/4074-dwm.png,
// the Documents window; client contents from y=151, client left x=254), all
// relative to the bottom of the header (C) and the client's left edge:
//   title "Documents"   ink x 77, cap top C-51, baseline C-39, 12 px caps,
//                       84 px wide, thin: Segoe UI 13pt (h17; seguibk.ttf of
//                       the 4074 Fonts) gives exactly 84 px and 12 rows on
//                       glass (ours: x 77..160 against 331..414 there)
//   breadcrumb          "Documents" ink 57 px, 9 px caps, ink x 89, baseline
//                       C-14: Segoe UI 9pt (h12) has the same rows, 59 px;
//                       Tahoma at the same sizes without Segoe UI (heavier)
//   separator           a 4x7 triangle (50,50,50), 24 px after the text, the
//                       next segment 23 px after it
//   text colour         navy (5,6,62) on glass with a white TextGlow,
//                       (0,2,92) on the opaque Slate header (lh4074-181415.png)
//   right part          a vertical divider R-91 (glass: a light line; Slate: a
//                       dark line with a light one left of it), the search
//                       and favourites buttons right of it
// The window manager's caption (luxwm aero: 27 rows) sits above the client, so
// the header in the client is 76 - 27 = 49 rows; the title is moved down until
// its capitals are inside (4074 draws it partly over the caption rows).
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <vector>
#include "header.h"
#include "navbar.h"
#include "gdi/png.h"
#include "shell/config.h"
#include "shell/lhdata.h"
#include "shell/lhtheme.h"
#include "shell/netres.h"
#include "uiicons.h"
#include "commands.h"
#include "verbs.h"
#include "w32/user.h"
#include <sys/stat.h>
#include "wuxtheme/ut.h"
#include <lhnav.h>      // luxnav (flxp.mk)
extern "C" {
#include "commctrl.h"
#include "w32/flxp_dwmapi.h"
}
#undef small      // rpcndr.h (through dwmapi.h)

namespace header {

namespace {

const int REF_HEADER = 76;        // frame top to client contents in 4074-dwm.png
// the navigation buttons (4074's "framelet": Microsoft.Windows.Client.dll's
// framelet_{back,forward}_{rest,hover,pressed,disabled}.png, 38x54 and 27x54,
// the forward plate continuing the back plate's right edge), then directly the
// title over the breadcrumb (4074 has no folder icon in the header)
const int NAV_X = 2, BACK_W = 38, FWD_W = 27, NAV_H = 54, NAV_W = BACK_W + FWD_W;
const int TITLE_X = 75, CRUMB_X = 88;
const int ARROW_GAP_L = 23, ARROW_W = 4, ARROW_GAP_R = 21;
const int SEARCH_W = 160, SEARCH_H = 20, SEARCH_MARGIN = 8, DIVIDER_GAP = 9;
const int SEG_PAD = 6, ARROW_BOX = 12, FIELD_DROP_W = 15;   // the breadcrumb's segments and field
const COLORREF GLASS_TEXT = RGB(5, 6, 62), SLATE_TEXT = RGB(0, 2, 92), ARROW = RGB(50, 50, 50);

struct Crumb { std::string name, path, shown; RECT text; };

HWND g_wnd, g_edit;
WNDPROC g_edit_old;
Callbacks g_cb;
HFONT g_title_font, g_title_bold, g_crumb_font, g_crumb_hot;
int g_height = REF_HEADER - 27;
std::string g_title;
std::string g_fixed_title;     // set_title: the host's title instead of the folder's name (the file dialog)
std::vector<Crumb> g_crumbs;
int g_hot = -1, g_pressed = -1;      // segment under the mouse / pressed (its name part)
bool g_hot_arrow;                    // the mouse is on g_hot's arrow part
int g_open = -1;                     // the segment whose drop-down is open
bool g_bar_hot;                      // the mouse is over the bar: the field shows (lh4074-224731.png)
HWND g_addr;                         // the bar as an edit field (Ctrl+L, a click on its empty part)
WNDPROC g_addr_old;
bool g_editing;
bool g_tracking, g_quiet;
int g_divider;

// 4074's Toolbar Size and Toolbars (the system menu of lh4074-1952*.png), our
// View > Toolbars: the size and which elements show (header.h). Read from
// <prefix>header_size / <prefix>header_<element> (the file dialog: filedlg_).
int g_hsize = SIZE_MEDIUM;
bool g_show[ELEMENTS] = { true, true, true, true, true };
std::string g_prefix, g_default_size = "medium";
const char* const SIZE_NAMES[4] = { "small", "medium", "large", "xlarge" };
const char* const ELEMENT_KEYS[ELEMENTS] = { "header_buttons", "header_title", "header_breadcrumb", "header_search", "header_favorites" };
// the Large and Extra-Large sizes: Medium's rows plus empty ones below (lh4074-195250/195258.png:
// 108 and 125 rows from the frame's top edge against 76)
int extra_rows() { return g_hsize == SIZE_LARGE ? 32 : g_hsize == SIZE_XLARGE ? 49 : 0; }
bool small_size() { return g_hsize == SIZE_SMALL; }

// the host's glyph buttons (set_tools): left of the magnifier on the crumbs' row
struct ToolBtn { Tool t; navbar::Img img; bool enabled = true; RECT r = {}; };
std::vector<ToolBtn> g_tools;
int g_tool_hot = -1, g_tool_pressed = -1;
const int TOOL_W = 24, TOOL_DROP_W = 11, TOOL_H = 22, TOOL_GAP = 2;
int tools_width()
{
    int w = 0;
    for (const ToolBtn& b : g_tools) w += TOOL_W + (b.t.dropdown ? TOOL_DROP_W : 0) + TOOL_GAP;
    return w ? w + 8 : 0;
}
bool small_right() { return g_hsize == SIZE_SMALL || g_hsize == SIZE_LARGE; }   // the small magnifier and star

// luxwm's caption text for the running frame theme (the theme.ini in
// _LHWM_THEME, the colours of _LHWM_FRAME_METRICS): the header's title and
// breadcrumb take its face, weight, colours, shadow (msstyles frames) or glow
// (DWM frames) and antialiasing, at the header's own sizes. Not valid without
// luxwm: then the 4074/navigation-bar look stays.
struct CaptionLook {
    bool valid = false;
    std::u16string face;           // the first of the theme's families we have, "" = keep ours
    int weight = FW_NORMAL;
    BYTE quality = ANTIALIASED_QUALITY;    // text_antialias = none: NONANTIALIASED_QUALITY
    COLORREF text[2] = {};         // active, inactive
    int sh_type[2] = {};           // caption.shadow.1/.2: 0 none, 1 single, 2 continuous
    int sh_dx[2] = {}, sh_dy[2] = {};
    COLORREF sh_rgb[2] = {};
    bool glow = false;             // DWM theme: TextGlow (glow = #rrggbb)
    COLORREF glow_rgb = RGB(255, 255, 255);
    // maximised (luxwm's text_max_active/_inactive, glow_max; default the above):
    // aero/Jade's white title, no glow, on the dark LGFRAMEMAX plate
    COLORREF max_text[2] = {};
    bool max_glow = false;
    COLORREF max_glow_rgb = RGB(255, 255, 255);
    bool max_sh = false;           // maxcaption.shadow.1/.2 (msstyles MAXCAPTION) given
    int max_sh_type[2] = {}, max_sh_dx[2] = {}, max_sh_dy[2] = {};
    COLORREF max_sh_rgb[2] = {};
};
CaptionLook g_look;

// navigation buttons: parts and images
enum Part { NONE, BACK, BACK_DROP, FWD, FWD_DROP, FAV, SEARCH };
enum State { REST, HOVER, PRESSED, DISABLED };
HBITMAP g_nav[2][4];          // [back, forward][state]: premultiplied 32bpp DIB sections
bool g_can[2];                // back, forward enabled
int g_nav_hot = NONE, g_nav_pressed = NONE;

// how the header is painted: on luxwm's glass; opaque, the theme's navigation
// bar (navbar.h); opaque, luxwm's header pixmap under the glass layout; the
// drawn Slate gradient (no luxwm)
enum Mode { GLASS, NAVBAR, PIXMAP };
Mode g_mode = GLASS;
bool g_active = true;
HBITMAP g_pix;                       // luxwm's _LHWM_HEADER_PIXMAP, copied
int g_pix_w, g_pix_h;
bool g_pix_stale = true;
bool g_search_open;                  // navigation bar: the search box is out
HFONT g_nb_title, g_nb_crumb, g_nb_crumb_hot;
int g_nb_title_off, g_nb_crumb_off;  // their capitals' first ink row below the text top
int g_title_bold_off;                // the same for g_title_bold (4051 under Luna)
HBITMAP g_mag[2];                    // the magnifier: [colour], drawn once
HBITMAP g_mag_icon;                  // shell32's Search icon (32x32), premultiplied
bool g_mag_looked;
const int MAG_W = 30, MAG_H = 32, MAG_CX = 19, MAG_CY = 12;   // the lens centre in the cell

// The vision mode: the header's contents through lhnav (docs/NAVBAR.md "flxp's
// header"), when the frame theme has a
// nav.ini (header_engine = auto | lhnav | flxp). Its code is below ("vision").
struct Vision {
    bool on = false;
    lhnav* nav = nullptr;
    ut_theme* style = nullptr;           // the session's msstyles, opened for lhnav (in-process libuxtheme)
    std::string style_path, nav_ini, why;
    lhnav_metrics base;                  // lhnav's defaults + nav.ini, before luxwm's numbers
    HBITMAP dib = nullptr;               // lhnav paints here (32bpp premultiplied, top-down)
    uint32_t* bits = nullptr;
    int dw = 0, dh = 0;
    HWND tip = nullptr;                  // comctl32's tooltip with lhnav_tip's text
    int tip_part = 0;
    std::u16string tip_text;
    bool tracking = false, search_on = false;
    HFONT edit_font = nullptr;           // the EDITs over lhnav's fields
    std::vector<int> tool_ids;           // per lhnav toolbar item: the host's command (0 = none)
};
Vision g_v;
bool vision() { return g_v.on; }
#define LHNAV_SEP_OF(i) (LHNAV_SEP + (i))
void vision_layout();
void vision_paint(HDC dc);
bool vision_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp, LRESULT* r);

std::u16string W(const std::string& s) { return w32::utf16_of(s.c_str()); }

HFONT make_font(const wchar_t* face, int h, bool underline, int weight = FW_NORMAL, BYTE quality = ANTIALIASED_QUALITY)
{
    LOGFONTW lf = {};
    lf.lfHeight = -h;
    lf.lfWeight = weight;
    lf.lfUnderline = underline;
    lf.lfQuality = quality;                 // 4074 draws the header antialiased
    for (int i = 0; face[i] && i < LF_FACESIZE - 1; i++) lf.lfFaceName[i] = face[i];
    return CreateFontIndirectW(&lf);
}

bool have_face(const wchar_t* face)
{
    HFONT f = make_font(face, 12, false);
    HDC dc = GetDC(nullptr);
    HGDIOBJ old = SelectObject(dc, f);
    WCHAR got[LF_FACESIZE] = {};
    GetTextFaceW(dc, LF_FACESIZE, got);
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    DeleteObject(f);
    return lstrcmpiW(got, face) == 0;
}

bool glass()
{
    BOOL on = FALSE;
    DwmIsCompositionEnabled(&on);
    return on;
}

bool navbar_mode() { return g_mode == NAVBAR; }
const navbar::Skin& sk() { return navbar::skin(); }
std::string g_buttons_setting = "auto";   // header_button_images: auto/theme, or framelets
// header_button_images (the setting was header_buttons before that became the
// Back/Forward toggle; header_buttons=framelets is still understood)
std::string button_images_setting()
{
    std::string v = shell::theme_setting("header_button_images");
    if (v.empty() && shell::theme_setting("header_buttons") == "framelets") v = "framelets";
    return v.empty() ? "auto" : v;
}
// The theme's Back/Forward/Favorites (TravelBack/TravelFwd/Favorites::
// AnimationButton) without the band as well: 4074's composited header is the
// same browseui navbar without its background (COMPOSITION.md 16.5), and
// 4051 under Luna draws Luna's own travel buttons on its header
// (lh4051-232843.png), so the buttons are the style's own in every mode
// (aero/Jade: the grey glass ones, green when hot). The Avalon framelets
// only when the style has none (or header_button_images=framelets).
bool themed_free() { return g_mode != NAVBAR && g_buttons_setting != "framelets" && navbar::skin().buttons; }
bool nb_buttons() { return navbar_mode() || themed_free(); }

// The rows, top down, in every mode (glass, navigation bar, luxwm's pixmap):
// with CLIENT_CAPTION the caption's rows that luxwm leaves to the client
// (top_off), then the band - 4051's navigation bar (doc/4051-explorer-
// research.md section 1): Medium 54 rows (Slate 4074's 49-row art: 53), Small
// 36 (lh4074-195235.png) -, then Large's / Extra-Large's empty rows. The
// travel buttons end on the band's last row (their drop-down flush with it);
// the header's last row is the band's dark line (paint_bottom).
const int SMALL_BAND = 36;
int medium_band() { return nb_buttons() ? sk().height : NAV_H; }
int band_rows() { return small_size() ? SMALL_BAND : medium_band(); }
int top_off() { int t = g_height - band_rows() - extra_rows(); return t > 0 ? t : 0; }
int base_height() { return top_off() + band_rows(); }      // below the band (Large/XL: their empty rows follow)
int title_baseline() { return base_height() - 39; }       // the framelets' layout (4074-dwm.png)
int travel_h();
int btn_top() { return base_height() - travel_h(); }      // the travel buttons' first row: their last on the band's
int nav_top() { return btn_top(); }
const int SMALL_DISC_CY = 14;        // the small buttons' circle centre (rows 5..23 of Slate's 32, 3..24 of Jade's 36, the scaled pair's 5..23)
int g_nb_crumb_asc;                  // the navigation bar crumb font's ascent
int crumb_baseline()
{
    if (small_size()) return btn_top() + SMALL_DISC_CY + 5;
    if (nb_buttons() && g_nb_crumb) return top_off() + sk().crumb_cap - g_nb_crumb_off + g_nb_crumb_asc;
    return base_height() - 14;
}

bool zoomed() { return g_wnd && IsZoomed(GetParent(g_wnd)); }

int variant()
{
    bool max = zoomed();
    return max ? (g_active ? navbar::MAX : navbar::MAX_INACTIVE) : (g_active ? navbar::ACTIVE : navbar::INACTIVE);
}

// the navigation bar's fonts and places wherever the theme's buttons are (glass too: the same layout as the bar)
bool nb_fonts() { return nb_buttons() && g_nb_title; }
HFONT title_font() { return nb_fonts() ? g_nb_title : g_title_font; }
HFONT crumb_font(bool hot) { return nb_fonts() ? (hot ? g_nb_crumb_hot : g_nb_crumb) : (hot ? g_crumb_hot : g_crumb_font); }
// the text's left edge: the measured ink starts one column right of it
int travel_right();
int title_x() { return !g_show[SHOW_BUTTONS] ? 14 : nb_buttons() ? sk().title_x - 1 : TITLE_X; }
int crumb_x()
{
    if (small_size()) return g_show[SHOW_BUTTONS] ? travel_right() + 14 : 12;   // lh4074-195235.png: ink 15 px after the buttons
    return !g_show[SHOW_BUTTONS] ? 14 : nb_buttons() ? sk().crumb_x - 1 : CRUMB_X;
}
// the separator's room: gap, glyph, gap
int arrow_left_gap() { return navbar_mode() ? sk().arrow_gap_l : ARROW_GAP_L; }
int arrow_width() { return navbar_mode() ? sk().arrow[0].w : ARROW_W; }
int arrow_right_gap() { return navbar_mode() ? sk().arrow_gap_r : ARROW_GAP_R; }
// The right part with the theme's buttons: where the gripper, the magnifier,
// the star and the search box (while open) go, by size and elements shown.
// Medium (4074-dwm.png, lh4074-181608.png): gripper, magnifier and star at the
// navigation bar's places; Large and Extra-Large: no gripper, the small (Large)
// or normal (Extra-Large) magnifier and star at the bottom; Small: the small
// ones on the buttons' row (lh4074-195235/195250/195258.png).
struct Right {
    int grip_x = -1;                 // -1: none
    int grip_y = 0;                  // the gripper's rows are the theme's, this many rows down
    int fav_x = 0, fav_y = 0;        // the star's image: fav[state]
    const navbar::Img* fav = nullptr;
    int mag_x = 0, mag_y = 0, mag_w = 0, mag_h = 0;   // the magnifier's cell (scaled for the small one)
    bool mag = false;
    int edit_x = 0;                  // the search box's left while open
    int left = 0;                    // the first column used: the crumbs end before it
};
Right right_layout(int R);
// where the crumbs must end: before the divider, the search box or the gripper
int crumb_limit(int R)
{
    if (!nb_buttons()) return (g_show[SHOW_SEARCH] ? g_divider - 12 : R - 12) - tools_width();
    return right_layout(R).left - 12 - tools_width();
}

int text_width(HDC dc, HFONT f, const std::string& s, int* ascent)
{
    HGDIOBJ old = SelectObject(dc, f);
    std::u16string w = W(s);
    SIZE sz = {};
    GetTextExtentPoint32W(dc, (LPCWSTR)w.c_str(), (int)w.size(), &sz);
    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    if (ascent) *ascent = tm.tmAscent;
    SelectObject(dc, old);
    return sz.cx;
}

void layout()
{
    if (!g_wnd) return;
    if (vision()) { vision_layout(); return; }
    RECT rc; GetClientRect(g_wnd, &rc);
    int R = rc.right;
    int sw = SEARCH_W;
    if (R < 500) sw = R / 4 > 60 ? R / 4 : 60;
    int sy = crumb_baseline() - 4 - SEARCH_H / 2;
    if (!g_show[SHOW_SEARCH]) {
        if (g_edit) ShowWindow(g_edit, SW_HIDE);
    } else if (nb_buttons()) {
        // out only while searching: left of the gripper, on the crumbs' row
        if (!small_size()) sy = top_off() + sk().crumb_cap + 4 - SEARCH_H / 2;
        if (g_edit) SetWindowPos(g_edit, nullptr, right_layout(R).edit_x, sy, sw, SEARCH_H,
                                 SWP_NOZORDER | SWP_NOACTIVATE | (g_search_open ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    } else if (g_edit) {
        SetWindowPos(g_edit, nullptr, R - SEARCH_MARGIN - sw, sy, sw, SEARCH_H, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    g_divider = g_show[SHOW_SEARCH] ? R - SEARCH_MARGIN - sw - DIVIDER_GAP : R - SEARCH_MARGIN;
    // breadcrumb segments; leading ones give way to a "..." segment (which
    // goes to the last hidden one) when they do not fit before the divider
    HDC dc = GetDC(g_wnd);
    // room for the last segment's arrow and the field's drop button
    int asc = 0, base = crumb_baseline(), limit = crumb_limit(R) - (SEG_PAD + ARROW_BOX + FIELD_DROP_W - 4);
    const int step = arrow_left_gap() + arrow_width() + arrow_right_gap();
    HFONT cf = crumb_font(false);
    size_t n = g_crumbs.size();
    std::vector<int> w(n);
    for (size_t i = 0; i < n; i++) w[i] = text_width(dc, cf, g_crumbs[i].name, &asc);
    int ell = text_width(dc, cf, "...", &asc);
    int top = nb_fonts() && !small_size() ? top_off() + sk().crumb_cap - g_nb_crumb_off : base - asc;
    auto right = [&](size_t f) {
        int x = crumb_x() + (f ? ell + step : 0);
        for (size_t i = f; i < n; i++) x += w[i] + (i + 1 < n ? step : 0);
        return x;
    };
    size_t first = 0;
    while (first + 1 < n && right(first) > limit) first++;
    int x = crumb_x();
    for (size_t i = 0; i < n; i++) {
        Crumb& c = g_crumbs[i];
        if (i + 1 < first) { SetRectEmpty(&c.text); c.shown.clear(); continue; }
        bool ellipsis = first && i + 1 == first;
        c.shown = ellipsis ? "..." : c.name;
        int cw = ellipsis ? ell : w[i];
        c.text = RECT{ x, top, x + cw, top + asc + 4 };
        x += cw + step;
    }
    ReleaseDC(g_wnd, dc);
    // the tools: right to left from where the crumbs must end, on their row
    int tx = crumb_limit(R) + 12 + tools_width() - 8, ty = crumb_baseline() - 5 - TOOL_H / 2;
    for (int i = (int)g_tools.size() - 1; i >= 0; i--) {
        ToolBtn& b = g_tools[(size_t)i];
        int w = TOOL_W + (b.t.dropdown ? TOOL_DROP_W : 0);
        tx -= w;
        b.r = RECT{ tx, ty, tx + w, ty + TOOL_H };
        tx -= TOOL_GAP;
    }
    InvalidateRect(g_wnd, nullptr, TRUE);
}

void gradient(HDC dc, int x0, int x1, int y0, int y1, COLORREF a, COLORREF b)
{
    if (x1 <= x0) return;
    TRIVERTEX v[2] = {
        { x0, y0, (COLOR16)(GetRValue(a) << 8), (COLOR16)(GetGValue(a) << 8), (COLOR16)(GetBValue(a) << 8), 0xff00 },
        { x1, y1, (COLOR16)(GetRValue(b) << 8), (COLOR16)(GetGValue(b) << 8), (COLOR16)(GetBValue(b) << 8), 0xff00 },
    };
    GRADIENT_RECT r = { 0, 1 };
    GradientFill(dc, v, 2, &r, 1, GRADIENT_FILL_RECT_H);
}

void fill(HDC dc, int x0, int y0, int x1, int y1, COLORREF c)
{
    RECT r = { x0, y0, x1, y1 };
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

void blend(HDC dc, int x0, int y0, int x1, int y1, COLORREF c, int a);

// A 1 px vertical line of colour c whose alpha rises from 0 at both ends to
// `peak` in the middle (premultiplied, AlphaBlend'ed: translucent on glass).
void soft_line(HDC dc, int x, int y0, int y1, COLORREF c, int peak)
{
    int h = y1 - y0;
    if (h <= 0) return;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = 1; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) return;
    uint8_t* p = (uint8_t*)bits;
    for (int i = 0; i < h; i++) {
        int d = i < h / 2 ? i : h - 1 - i;
        int a = d * 2 * peak / (h > 1 ? h : 1);
        if (a > peak) a = peak;
        p[i * 4 + 0] = (uint8_t)(GetBValue(c) * a / 255);
        p[i * 4 + 1] = (uint8_t)(GetGValue(c) * a / 255);
        p[i * 4 + 2] = (uint8_t)(GetRValue(c) * a / 255);
        p[i * 4 + 3] = (uint8_t)a;
    }
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, bmp);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(dc, x, y0, 1, h, mem, 0, 0, 1, h, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(bmp);
}

// The opaque header of the non-composited 4074 Explorer (lh4074-181415.png,
// row 124): a horizontal gradient that brightens towards a light band left of
// the divider, darkens to it, and rises again to the right edge.
void paint_slate(HDC dc, int R, int H)
{
    int D = g_divider;
    gradient(dc, 0, D - 96, 0, H, RGB(216, 225, 245), RGB(228, 236, 255));
    fill(dc, D - 96, 0, D - 94, H, RGB(236, 242, 255));
    fill(dc, D - 94, 0, D - 52, H, RGB(241, 246, 255));
    gradient(dc, D - 52, D - 1, 0, H, RGB(226, 233, 250), RGB(194, 202, 220));
    fill(dc, D - 1, 0, D, H, RGB(234, 237, 243));
    // the divider: dark in the middle, fading to the background at both ends
    TRIVERTEX v[3] = {
        { D, 0, 0xc100, 0xc900, 0xdb00, 0xff00 }, { D + 1, H / 2, 0x9a00, 0xa100, 0xaf00, 0xff00 }, { D, H, 0xbd00, 0xc500, 0xd700, 0xff00 },
    };
    GRADIENT_RECT g1 = { 0, 1 };
    GradientFill(dc, v, 2, &g1, 1, GRADIENT_FILL_RECT_V);
    TRIVERTEX v2[2] = { { D, H / 2, 0x9a00, 0xa100, 0xaf00, 0xff00 }, { D + 1, H, 0xbd00, 0xc500, 0xd700, 0xff00 } };
    GradientFill(dc, v2, 2, &g1, 1, GRADIENT_FILL_RECT_V);
    int mid = D + 1 + (R - D) * 3 / 5;
    gradient(dc, D + 1, mid, 0, H, RGB(192, 200, 218), RGB(159, 166, 187));
    gradient(dc, mid, R, 0, H, RGB(159, 166, 187), RGB(207, 214, 232));
}

// ---- navigation buttons -------------------------------------------------------

// A premultiplied 32bpp DIB section from a straight-alpha gdi bitmap.
HBITMAP dib_from(const gdi::Bitmap* b)
{
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = b->width; bi.bmiHeader.biHeight = -b->height; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) return nullptr;
    uint32_t* d = (uint32_t*)bits;
    for (int y = 0; y < b->height; y++)
        for (int x = 0; x < b->width; x++) {
            uint32_t p = b->row(y)[x], a = p >> 24;
            uint32_t r = ((p >> 16) & 255) * a / 255, g = ((p >> 8) & 255) * a / 255, bl = (p & 255) * a / 255;
            d[y * b->width + x] = a << 24 | r << 16 | g << 8 | bl;
        }
    return bmp;
}

// 4074's images, read from its Avalon assembly. 4051 and 4074 have the same
// images and no per-theme variants (Microsoft.Windows.Client.dll holds the only
// framelet_* set). Looked for, in order: beside the session theme's
// path.resources; lhde's resources/<build> (shell/lhdata.h: the theme's
// build, then 4074, then the others). The first file with all eight images wins.
struct Candidate { std::string path, origin; };

std::vector<Candidate> framelet_candidates()
{
    std::vector<Candidate> out;
    auto add_sys = [&](const std::string& sys, const std::string& origin) {
        if (sys.empty()) return;
        for (const char* r : { "/../Microsoft.NET/Avalon/Microsoft.Windows.Client.dll", "/../Microsoft.NET/Avalon/microsoft.windows.client.dll",
                               "/Microsoft.Windows.Client.dll" })
            out.push_back({ sys + r, origin });
    };
    const shell::SessionTheme& st = shell::session_theme();
    if (!st.name.empty()) add_sys(shell::longhorn_resources(), "theme " + st.name + " path.resources");
    // lhde's resources/<build> (lhde-import: a build's system32, its Microsoft.NET beside it):
    // the theme's build, then the others, 4074 first (the header's reference)
    std::string dflt = shell::build_resources();
    if (!dflt.empty()) add_sys(dflt, "lhde resources of the theme's build");
    std::vector<std::string> builds = shell::lhde_subdirs("resources");
    std::stable_sort(builds.begin(), builds.end(), [](const std::string& a, const std::string& b) {
        return (shell::base_of(a) == "4074") > (shell::base_of(b) == "4074"); });
    for (const std::string& r : builds) if (r != dflt) add_sys(r, "lhde resources");
    return out;
}

const char* FRAMELET_NAMES[2] = { "back", "forward" };
const char* FRAMELET_STATES[4] = { "rest", "hover", "pressed", "disabled" };

// all eight images of one file, or false (nothing kept)
bool framelets_from(const std::string& data, HBITMAP got[2][4])
{
    bool all = true;
    for (int i = 0; i < 2; i++)
        for (int s = 0; s < 4; s++) {
            got[i][s] = nullptr;
            std::string png;
            if (shell::net_resource_in(data, std::string("framelet_") + FRAMELET_NAMES[i] + "_" + FRAMELET_STATES[s] + ".png", png)) {
                gdi::Bitmap* b = gdi::decode_png((const uint8_t*)png.data(), png.size());
                if (b && b->width == (i ? FWD_W : BACK_W) && b->height == NAV_H) got[i][s] = dib_from(b);
                if (b) gdi::destroy_bitmap(b);
            }
            all = all && got[i][s];
        }
    if (!all)
        for (int i = 0; i < 2; i++) for (int s = 0; s < 4; s++) if (got[i][s]) { DeleteObject(got[i][s]); got[i][s] = nullptr; }
    return all;
}

std::string g_framelet_source;       // "<path> (<origin>)", "" = drawn
std::string g_framelet_tried;

// the first candidate with all images into got (if given); its description, "" if none
std::string find_framelets(HBITMAP (*got)[4], std::string* tried)
{
    std::string seen;
    for (const Candidate& c : framelet_candidates()) {
        std::string d, png;
        if (!shell::read_file(c.path, d) || !shell::net_resource_in(d, "framelet_back_rest.png", png)) {
            if (tried) *tried += (tried->empty() ? "" : ", ") + c.path;
            continue;
        }
        if (got) {
            HBITMAP tmp[2][4];
            if (!framelets_from(d, tmp)) {
                if (tried) *tried += (tried->empty() ? "" : ", ") + c.path + " (incomplete)";
                continue;
            }
            memcpy(got, tmp, sizeof tmp);
        }
        return c.path + " (" + c.origin + ")";
    }
    return "";
}

bool load_framelets()
{
    g_framelet_tried.clear();
    g_framelet_source = find_framelets(g_nav, &g_framelet_tried);
    if (g_framelet_source.empty()) {
        static bool warned;
        if (!warned)
            fprintf(stderr, "flxp: no Microsoft.Windows.Client.dll with 4074's framelet images (lhde-import resources/4074); "
                    "drawing lookalike Back/Forward buttons\n");
        warned = true;
        return false;
    }
    return true;
}

// Without 4074's files: drawn lookalikes on the same 38x54 / 27x54 cells (round
// blue glass buttons on a pale plate, a white arrow, the history drop-down
// under the button when hot), straight alpha, 4x4 supersampled.
struct Canvas {
    gdi::Bitmap* b;
    // src-over of colour (r,g,b) at coverage a (0..1)
    void put(int x, int y, double r, double g, double bl, double a)
    {
        if (x < 0 || y < 0 || x >= b->width || y >= b->height || a <= 0) return;
        uint32_t& p = b->row(y)[x];
        double da = (p >> 24) / 255.0, oa = a + da * (1 - a);
        if (oa <= 0) return;
        double dr = (p >> 16 & 255), dg = (p >> 8 & 255), db = (p & 255);
        double nr = (r * a + dr * da * (1 - a)) / oa, ng = (g * a + dg * da * (1 - a)) / oa, nb = (bl * a + db * da * (1 - a)) / oa;
        p = (uint32_t)lround(oa * 255) << 24 | (uint32_t)lround(nr) << 16 | (uint32_t)lround(ng) << 8 | (uint32_t)lround(nb);
    }
};

// coverage of pixel (x,y) by a disc
double disc(int x, int y, double cx, double cy, double r)
{
    int n = 0;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            double px = x + (i + 0.5) / 4 - cx, py = y + (j + 0.5) / 4 - cy;
            if (px * px + py * py <= r * r) n++;
        }
    return n / 16.0;
}

// coverage of pixel (x,y) by a left- (dir -1) or right-pointing (dir 1) arrow centred at (cx,cy), size s
double arrow(int x, int y, double cx, double cy, double s, int dir)
{
    int n = 0;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            double px = (x + (i + 0.5) / 4 - cx) * dir, py = y + (j + 0.5) / 4 - cy;
            // head: triangle from px=s (tip) back to px=0, half height s; shaft: px -s..0, half height s*0.38
            bool head = px >= 0 && px <= s && fabs(py) <= (s - px);
            bool shaft = px >= -s * 0.95 && px < 0.5 && fabs(py) <= s * 0.38;
            if (head || shaft) n++;
        }
    return n / 16.0;
}

gdi::Bitmap* draw_button(bool fwd, State st)
{
    int w = fwd ? FWD_W : BACK_W;
    gdi::Bitmap* b = gdi::create_bitmap(w, NAV_H);
    for (int y = 0; y < NAV_H; y++) for (int x = 0; x < w; x++) b->row(y)[x] = 0;
    Canvas c{ b };
    double cx = fwd ? 11.5 : 20.5, cy = fwd ? 22 : 23.5, r = fwd ? 10.5 : 16.5;
    double pcx = fwd ? 0 : 20.5, pr = fwd ? 13 : 20;
    // the plate: pale, translucent, joined to the neighbour (back: a capsule to the right edge)
    for (int y = 0; y < NAV_H; y++)
        for (int x = 0; x < w; x++) {
            double a = fwd ? (x < 14 ? (y >= 9 && y <= 35 ? 1.0 : 0.0) : disc(x, y, 13, 22, 13))
                           : (x >= (int)pcx && y >= 9 && y <= 35) ? 1.0 : disc(x, y, pcx, 22, pr);
            if (a > 0) {
                double t = (y - 2) / 40.0;
                double v = 238 - 40 * t;
                c.put(x, y, v, v + 2, v + 8, a * 0.55);
            }
        }
    bool hot = st == HOVER, pressed = st == PRESSED, dis = st == DISABLED;
    if (fwd && dis) return b;            // no forward button, only the plate (as 4074)
    // history drop-down under the button
    if (hot || pressed) {
        int x0 = fwd ? 0 : 4, x1 = fwd ? 25 : 36, y0 = fwd ? 42 : 43, y1 = fwd ? 52 : 54;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++) {
                bool edge = x == x0 || x == x1 - 1 || y == y1 - 1;
                double v = edge ? 120 : 236 - 3 * (y - y0);
                c.put(x, y, v, v + 2, v + 8, edge ? 0.6 : 0.85);
            }
        int ax = (x0 + x1) / 2, ay = y0 + 4;
        for (int k = 0; k < 3; k++) for (int x = ax - 2 + k; x <= ax + 2 - k; x++) c.put(x, ay + k, 40, 40, 50, 1);
    }
    // dark rim, then the blue body with a vertical gradient, then a glossy highlight
    for (int y = 0; y < NAV_H; y++)
        for (int x = 0; x < w; x++) {
            double rim = disc(x, y, cx, cy, r + 1);
            if (rim > 0) c.put(x, y, 40, 50, 110, rim * (dis ? 0.5 : 0.85));
            double body = disc(x, y, cx, cy, r);
            if (body > 0) {
                double t = (y - (cy - r)) / (2 * r);
                double R = 70 + 40 * (1 - t), G = 100 + 50 * (1 - t), B = 215 + 30 * (1 - t);
                if (hot) { R += 30; G += 30; B += 10; }
                if (pressed) { R -= 30; G -= 40; B -= 30; }
                if (dis) { double gr = (R + G + B) / 3; R = (R + gr * 2) / 3 + 30; G = (G + gr * 2) / 3 + 30; B = (B + gr * 2) / 3 + 35; }
                c.put(x, y, R > 255 ? 255 : R, G > 255 ? 255 : G, B > 255 ? 255 : B, body);
            }
            double gloss = disc(x, y, cx, cy - r * 0.45, r * 0.62) * disc(x, y, cx, cy, r - 1);
            if (gloss > 0) c.put(x, y, 255, 255, 255, gloss * (pressed ? 0.15 : 0.35));
        }
    for (int y = 0; y < NAV_H; y++)
        for (int x = 0; x < w; x++) {
            double a = arrow(x, y, cx + (fwd ? -1 : 1) * r * 0.1, cy, r * 0.55, fwd ? 1 : -1);
            if (a > 0) c.put(x, y, 255, 255, 255, a * (dis ? 0.6 : 1));
        }
    return b;
}

// 4074-dwm.png shows the disabled Back button grey and translucent over the
// glass (about (170,170,170) at 62 %), not the blue of the disabled image:
// Avalon's disabled look. Done to the disabled images, premultiplied.
void grey_out(HBITMAP b)
{
    DIBSECTION ds = {};
    if (!b || GetObjectW(b, sizeof ds, &ds) != sizeof ds || !ds.dsBm.bmBits) return;
    int w = ds.dsBm.bmWidth, h = ds.dsBm.bmHeight < 0 ? -ds.dsBm.bmHeight : ds.dsBm.bmHeight;
    uint32_t* p = (uint32_t*)ds.dsBm.bmBits;
    for (int i = 0; i < w * h; i++) {
        uint32_t v = p[i], a = v >> 24, r = (v >> 16) & 255, g = (v >> 8) & 255, bl = v & 255;
        uint32_t y = (r * 77 + g * 151 + bl * 28) >> 8;
        y = y * 62 / 100; a = a * 62 / 100;
        p[i] = a << 24 | y << 16 | y << 8 | y;
    }
}

void draw_framelets()
{
    for (int i = 0; i < 2; i++)
        for (int s = 0; s < 4; s++) {
            gdi::Bitmap* b = draw_button(i == 1, (State)s);
            g_nav[i][s] = dib_from(b);
            gdi::destroy_bitmap(b);
        }
}

// ---- the travel buttons by size -----------------------------------------------------
// Small (header_small_buttons = auto | even | uneven): even = the theme's
// SmallTravelBack/Fwd (4074's styles: the same size, as 4074 has them);
// uneven = the big pair - bigger Back, smaller Forward, as 4051's art and the
// concepts (image9.jpeg, lh_winhec_proto_10.jpg) - box-filtered to the small
// height, the drop-down's rows kept 1:1 at the bottom (navbar::shrink). auto:
// even where the style has the Small parts, else uneven (4051, the framelets).
// The style is independent of glass. `even` without Small parts is uneven.
std::string g_small_setting = "auto";
bool even_small() { return small_size() && nb_buttons() && sk().small && g_small_setting != "uneven"; }
bool scaled() { return small_size() && !even_small(); }

// the uneven small images, made once per theme from the images in use
struct Shrunk {
    bool built = false;
    navbar::Img back[4][5], fwd[4][5];   // the theme's pair, by variant and state
    navbar::Img fav[4][4];               // the theme's star at two thirds (no SmallFavorites: 4051)
    navbar::Img fr[2][4];                // the framelets [back, forward][state]
    int T = 0, fwd_x = 0, split = 0, margin = 4;          // the theme's: height, Forward's left, drop-down's first row
    int fr_T = 0, fr_fwd_x = 0, fr_split = 0, fr_margin = 4;
    std::string glyph;                   // how the drop-down glyph was found (--info)
};
Shrunk g_shrunk;

void free_shrunk()
{
    for (auto& v : g_shrunk.back) for (navbar::Img& i : v) navbar::free_image(i);
    for (auto& v : g_shrunk.fwd) for (navbar::Img& i : v) navbar::free_image(i);
    for (auto& v : g_shrunk.fav) for (navbar::Img& i : v) navbar::free_image(i);
    for (auto& v : g_shrunk.fr) for (navbar::Img& i : v) navbar::free_image(i);
    g_shrunk = Shrunk();
}

// the glyph's columns of a hot image, else 7 columns about the split's centre
void glyph_of(const navbar::Img& hot, int split, int* g0, int* g1, bool* found)
{
    *found = navbar::glyph_columns(hot, split, g0, g1);
    if (!*found) { *g0 = hot.w / 2 - 3; *g1 = hot.w / 2 + 3; }
}

const Shrunk& shrunk()
{
    if (g_shrunk.built) return g_shrunk;
    Shrunk& u = g_shrunk;
    u.built = true;
    const navbar::Skin& k = sk();
    if (k.buttons) {
        int h = k.back[0][navbar::NORMAL].h;
        // the height of the theme's own small art where it has it (Slate 4074's 32: in the same place), else the band's
        u.T = k.small ? k.sback[0][navbar::NORMAL].h : SMALL_BAND;
        double f = (double)u.T / h;
        int split = k.split_top > 0 && k.split_top < h ? k.split_top : h - 10;
        u.split = u.T - (h - split);
        u.fwd_x = (int)lround(k.fwd_x * f);
        u.margin = (int)lround(4 * f);
        int b0, b1, f0, f1;
        bool bf, ff;
        glyph_of(k.back[0][navbar::HOT], split, &b0, &b1, &bf);
        glyph_of(k.fwd[0][navbar::HOT], split, &f0, &f1, &ff);
        u.glyph = bf && ff ? "the drop-down glyph found in the hot images" : "the drop-down glyph assumed centred";
        for (int v = 0; v < 4; v++) {
            for (int s = 0; s < 5; s++) {
                u.back[v][s] = navbar::shrink(k.back[v][s], u.T, split, b0, b1);
                u.fwd[v][s] = navbar::shrink(k.fwd[v][s], u.T, split, f0, f1);
            }
            for (int s = 0; s < 4; s++) {
                const navbar::Img& src = k.fav[v][s];
                u.fav[v][s] = navbar::shrink(src, (src.h * 2 + 1) / 3, src.h, 0, -1);
            }
        }
    }
    // the framelets (38x54, 27x54; the drop-down from row 43)
    u.fr_T = SMALL_BAND;
    double f = (double)u.fr_T / NAV_H;
    u.fr_split = u.fr_T - (NAV_H - 43);
    u.fr_fwd_x = (int)lround(BACK_W * f);
    u.fr_margin = (int)lround(4 * f);
    for (int i = 0; i < 2; i++) {
        navbar::Img hot = { g_nav[i][HOVER], i ? FWD_W : BACK_W, NAV_H };
        int g0, g1; bool found;
        glyph_of(hot, 43, &g0, &g1, &found);
        for (int s = 0; s < 4; s++) {
            navbar::Img src = { g_nav[i][s], i ? FWD_W : BACK_W, NAV_H };
            u.fr[i][s] = navbar::shrink(src, u.fr_T, 43, g0, g1);
        }
    }
    return u;
}

int travel_x0() { return nb_buttons() ? sk().travel_x : NAV_X; }
int fwd_dx()
{
    if (even_small()) return sk().sfwd_x;
    if (scaled()) return nb_buttons() ? shrunk().fwd_x : shrunk().fr_fwd_x;
    return nb_buttons() ? sk().fwd_x : BACK_W;
}
const navbar::Img& back_img(int v, int st) { return even_small() ? sk().sback[v][st] : scaled() ? shrunk().back[v][st] : sk().back[v][st]; }
const navbar::Img& fwd_img(int v, int st) { return even_small() ? sk().sfwd[v][st] : scaled() ? shrunk().fwd[v][st] : sk().fwd[v][st]; }
int fwd_w()
{
    if (nb_buttons()) return fwd_img(0, navbar::NORMAL).w;
    return scaled() ? shrunk().fr[1][REST].w : FWD_W;
}
int travel_h()
{
    if (even_small()) return sk().sback[0][navbar::NORMAL].h;
    if (scaled()) return nb_buttons() ? shrunk().T : shrunk().fr_T;
    return nb_buttons() ? sk().back[0][navbar::NORMAL].h : NAV_H;
}
// the drop-down's first row in the button image, and its inset from the button's sides
int split_row()
{
    if (even_small()) return sk().ssplit_top;
    if (scaled()) return nb_buttons() ? shrunk().split : shrunk().fr_split;
    return nb_buttons() ? sk().split_top : 42;
}
int drop_margin() { return scaled() ? (nb_buttons() ? shrunk().margin : shrunk().fr_margin) : 4; }
double scf() { return scaled() ? (double)shrunk().fr_T / NAV_H : 1.0; }    // the framelets' hit circles
int travel_right() { return travel_x0() + fwd_dx() + fwd_w(); }

int part_at(POINT p)
{
    if (nb_buttons()) {
        // the theme's images: their opaque pixels, the drop-down below split_top
        const navbar::Skin& k = sk();
        RECT rc; GetClientRect(g_wnd, &rc);
        Right r = right_layout(rc.right);
        int v = variant();
        if (r.fav && navbar::alpha_at(r.fav[navbar::NORMAL], p.x - r.fav_x, p.y - r.fav_y) > 96) return FAV;
        if (r.mag) {
            int mx = p.x - r.mag_x, my = p.y - r.mag_y;
            if (mx >= r.mag_w / 15 && mx < r.mag_w - 1 && my >= 1 && my < r.mag_h - 1) return SEARCH;
        }
        if (!g_show[SHOW_BUTTONS]) return NONE;
        p.y -= btn_top();
        int tx = p.x - k.travel_x, fd = fwd_dx(), m = drop_margin();
        if (p.y >= split_row() && p.y < travel_h()) {
            if (g_can[0] && tx >= m && tx < fd - m) return BACK_DROP;
            if (g_can[1] && tx >= fd && tx < fd + fwd_w() - 2) return FWD_DROP;
        }
        if (navbar::alpha_at(back_img(v, navbar::NORMAL), tx, p.y) > 96) return BACK;
        if (navbar::alpha_at(fwd_img(v, navbar::NORMAL), tx - fd, p.y) > 96) return FWD;
        return NONE;
    }
    if (!g_show[SHOW_BUTTONS]) return NONE;
    int t = nav_top();
    double f = scf();
    double bx = p.x - (NAV_X + 20.5 * f), by = p.y - (t + 23.5 * f);
    if (bx * bx + by * by <= 17.5 * 17.5 * f * f) return BACK;
    double fx = p.x - (NAV_X + (BACK_W + 11.5) * f), fy = p.y - (t + 22 * f);
    if (fx * fx + fy * fy <= 11.5 * 11.5 * f * f) return FWD;
    if (p.y >= t + split_row() && p.y < t + travel_h()) {
        if (g_can[0] && p.x >= NAV_X + 4 * f && p.x < NAV_X + 36 * f) return BACK_DROP;
        if (g_can[1] && p.x >= NAV_X + BACK_W * f && p.x < NAV_X + (BACK_W + 25) * f) return FWD_DROP;
    }
    return NONE;
}

bool is_back(int part) { return part == BACK || part == BACK_DROP; }
bool is_travel(int part) { return part >= BACK && part <= FWD_DROP; }
// a part that works: the travel buttons with history, the star and the magnifier always
bool enabled(int part) { return part == FAV || part == SEARCH || (is_travel(part) && g_can[is_back(part) ? 0 : 1]); }

// the state of a button made of parts a (the button) and b (its drop-down)
int part_state(int a, int b)
{
    bool hot = g_nav_hot == a || g_nav_hot == b;
    bool pressed = g_nav_pressed != NONE && (g_nav_pressed == a || g_nav_pressed == b) && g_nav_hot == g_nav_pressed;
    if (pressed) return g_nav_pressed == b ? navbar::SPLITPRESSED : navbar::PRESSED;
    return hot ? navbar::HOT : navbar::NORMAL;
}

void paint_nav(HDC dc)
{
    if (!g_show[SHOW_BUTTONS]) return;
    if (nb_buttons()) {
        const navbar::Skin& k = sk();
        int v = variant(), y = btn_top();
        int b = g_can[0] ? part_state(BACK, BACK_DROP) : navbar::DISABLED;
        int f = g_can[1] ? part_state(FWD, FWD_DROP) : navbar::DISABLED;
        navbar::draw(dc, fwd_img(v, f), k.travel_x + fwd_dx(), y);   // the back plate overlaps the forward one
        navbar::draw(dc, back_img(v, b), k.travel_x, y);
        return;
    }
    int t = nav_top();
    HDC mem = CreateCompatibleDC(dc);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    for (int i = 1; i >= 0; i--) {          // forward first: the back plate overlaps it
        int st = REST;
        if (!g_can[i]) st = DISABLED;
        else if (g_nav_pressed != NONE && is_back(g_nav_pressed) == (i == 0) && g_nav_hot == g_nav_pressed) st = PRESSED;
        else if (g_nav_hot != NONE && is_back(g_nav_hot) == (i == 0)) st = HOVER;
        if (scaled()) { navbar::draw(dc, shrunk().fr[i][st], NAV_X + (i ? fwd_dx() : 0), t); continue; }
        HBITMAP b = g_nav[i][st];
        if (!b) continue;
        HGDIOBJ old = SelectObject(mem, b);
        int w = i ? FWD_W : BACK_W;
        AlphaBlend(dc, NAV_X + (i ? BACK_W : 0), t, w, NAV_H, mem, 0, 0, w, NAV_H, bf);
        SelectObject(mem, old);
    }
    DeleteDC(mem);
}

// ---- the opaque header ------------------------------------------------------------
// Without composition 4074's header is browseui's navigation bar (COMPOSITION.md
// 16): the theme's Navbar::Rebar band over the client width with its splitter,
// the travel and favorites buttons, a gripper and the magnifier, fonts and
// colours from the theme by focus and maximise state (navbar.h). Styles
// without one (Luna) get luxwm's _LHWM_HEADER_PIXMAP: the caption band as the
// frame draws it, continued down over the header (active or inactive by focus),
// under the glass layout as 4051 drew its header under Luna (lh4051-232843.png).

Mode choose_mode()
{
    if (glass()) return GLASS;
    std::string bg = shell::theme_setting("header_background", "auto");
    if ((bg == "auto" || bg == "navbar" || bg.empty()) && navbar::skin().valid) return NAVBAR;
    return PIXMAP;      // the Slate gradient when luxwm gives none
}

// the first row with ink of the capitals when the text's top is 0
int cap_offset(HFONT f)
{
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = 40; bi.bmiHeader.biHeight = -48; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP b = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!b) { DeleteDC(dc); return 0; }
    HGDIOBJ ob = SelectObject(dc, b), of = SelectObject(dc, f);
    RECT r = { 0, 0, 40, 48 };
    FillRect(dc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    TextOutW(dc, 4, 0, L"H", 1);
    int top = 0;
    const uint32_t* p = (const uint32_t*)bits;
    for (int y = 0; y < 48 && !top; y++)
        for (int x = 0; x < 40; x++) if ((p[y * 40 + x] & 0xffffff) < 0x808080) { top = y; break; }
    SelectObject(dc, of); SelectObject(dc, ob);
    DeleteObject(b); DeleteDC(dc);
    return top;
}

COLORREF hex_colour(const std::string& v, COLORREF dflt)
{
    unsigned r, g, b;
    if (sscanf(v.c_str(), " #%02x%02x%02x", &r, &g, &b) == 3) return RGB(r, g, b);
    return dflt;
}

// "Trebuchet MS,Tahoma Bold 13px", "Segoe UI 9": families, style words, size
void parse_pango_font(const std::string& desc, CaptionLook& l)
{
    std::vector<std::string> t;
    size_t i = 0;
    while (i < desc.size()) {
        size_t j = desc.find(' ', i);
        if (j == std::string::npos) j = desc.size();
        if (j > i) t.push_back(desc.substr(i, j - i));
        i = j + 1;
    }
    if (!t.empty() && isdigit((unsigned char)t.back()[0])) t.pop_back();
    while (!t.empty()) {
        std::string w = t.back();
        for (char& c : w) c = (char)tolower((unsigned char)c);
        if (w == "bold" || w == "heavy" || w == "semi-bold" || w == "semibold") l.weight = w == "heavy" ? 900 : w[0] == 's' ? 600 : FW_BOLD;
        else if (w != "regular" && w != "normal" && w != "book" && w != "medium" && w != "light") break;
        t.pop_back();
    }
    std::string fams;
    for (size_t k = 0; k < t.size(); k++) fams += (k ? " " : "") + t[k];
    i = 0;
    while (i <= fams.size()) {
        size_t j = fams.find(',', i);
        if (j == std::string::npos) j = fams.size();
        std::string f = fams.substr(i, j - i);
        while (!f.empty() && f[0] == ' ') f.erase(0, 1);
        while (!f.empty() && f.back() == ' ') f.pop_back();
        std::u16string u = W(f);
        if (!f.empty() && have_face((const wchar_t*)u.c_str())) { l.face = u; return; }
        i = j + 1;
    }
}

void load_look()
{
    g_look = CaptionLook();
    WCHAR buf[2048] = {};
    if (FlxpDwmGetFrameThemePath(buf, 2048) != S_OK) return;
    std::string spec = w32::utf8_of(buf);
    std::string dir = spec.substr(0, spec.find('|'));     // dir|msstyles|colour|size
    std::string ini;
    if (dir.empty() || !shell::read_file(dir + "/theme.ini", ini)) return;
    CaptionLook l;
    std::string font;
    bool set_ma = false, set_mi = false, set_mg = false;
    size_t pos = 0;
    while (pos < ini.size()) {
        size_t e = ini.find('\n', pos);
        if (e == std::string::npos) e = ini.size();
        std::string line = ini.substr(pos, e - pos);
        pos = e + 1;
        size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || line[0] == ';' || eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        while (!k.empty() && k.back() == ' ') k.pop_back();
        while (!v.empty() && v[0] == ' ') v.erase(0, 1);
        if (v.find(';') != std::string::npos) v = v.substr(0, v.find(';'));   // trailing comment
        while (!v.empty() && (v.back() == ' ' || v.back() == '\r')) v.pop_back();
        if (k == "caption_font") font = v;
        else if (k == "font") { if (font.empty()) font = v; }      // a DWM frame's
        else if (k == "caption_text" || k == "text_active") l.text[0] = hex_colour(v, l.text[0]);
        else if (k == "caption_text_inactive" || k == "text_inactive") l.text[1] = hex_colour(v, l.text[1]);
        else if (k == "text_antialias") l.quality = v == "none" ? NONANTIALIASED_QUALITY : ANTIALIASED_QUALITY;
        else if (k == "glow") { l.glow = v != "none"; l.glow_rgb = hex_colour(v, l.glow_rgb); }
        else if (k == "text_max_active") { l.max_text[0] = hex_colour(v, 0); set_ma = true; }
        else if (k == "text_max_inactive") { l.max_text[1] = hex_colour(v, 0); set_mi = true; }
        else if (k == "glow_max") { l.max_glow = v != "none"; l.max_glow_rgb = hex_colour(v, l.max_glow_rgb); set_mg = true; }
        else if (k == "caption.shadow.1" || k == "caption.shadow.2" || k == "maxcaption.shadow.1" || k == "maxcaption.shadow.2") {
            int s = k.back() - '1';
            bool mx = k[0] == 'm';
            char type[32] = {}, col[32] = {};
            int dx, dy;
            if (sscanf(v.c_str(), "%31s %d %d %31s", type, &dx, &dy, col) == 4) {
                int t = !strcmp(type, "continuous") ? 2 : !strcmp(type, "single") ? 1 : 0;
                if (mx) { l.max_sh = true; l.max_sh_type[s] = t; l.max_sh_dx[s] = dx; l.max_sh_dy[s] = dy; l.max_sh_rgb[s] = hex_colour(col, 0); }
                else { l.sh_type[s] = t; l.sh_dx[s] = dx; l.sh_dy[s] = dy; l.sh_rgb[s] = hex_colour(col, 0); }
            }
        }
    }
    if (font.empty()) return;
    parse_pango_font(font, l);
    // luxwm's own colours win (they are what it draws with)
    FLXP_DWM_FRAME_METRICS m = {}; m.cbSize = sizeof m;
    FlxpDwmGetFrameMetrics(&m);
    if (m.bValid) { l.text[0] = m.crText; l.text[1] = m.crInactiveText; }
    if (!set_ma) l.max_text[0] = l.text[0];
    if (!set_mi) l.max_text[1] = l.text[1];
    if (!set_mg) { l.max_glow = l.glow; l.max_glow_rgb = l.glow_rgb; }
    if (m.bMaxValid) {        // _LHWM_FRAME_METRICS 14..16
        l.max_text[0] = m.crMaxText; l.max_text[1] = m.crMaxInactiveText;
        if (l.glow || set_mg) { l.max_glow = !m.bMaxGlowNone; if (l.max_glow) l.max_glow_rgb = m.crMaxGlow; }
    }
    l.valid = true;
    g_look = l;
}

// the glass/pixmap layout's fonts: Segoe UI where it can be found (the 4074
// Fonts have it), else Tahoma (heavier, about as wide); under luxwm its caption
// face, weight and smoothing at these sizes
void make_fonts()
{
    load_look();
    for (HFONT* f : { &g_title_font, &g_title_bold, &g_crumb_font, &g_crumb_hot }) if (*f) { DeleteObject(*f); *f = nullptr; }
    bool segoe = have_face(L"Segoe UI");
    const wchar_t* face = segoe ? L"Segoe UI" : L"Tahoma";
    const wchar_t* bold = L"Tahoma";
    int tw = FW_NORMAL, bw = FW_BOLD;
    BYTE q = ANTIALIASED_QUALITY;
    if (g_look.valid) {
        if (!g_look.face.empty()) face = bold = (const wchar_t*)g_look.face.c_str();
        tw = bw = g_look.weight; q = g_look.quality;
    }
    g_title_font = make_font(face, 17, false, tw, q);
    g_title_bold = make_font(bold, 16, false, bw, q);   // 4051 without glass: the caption's bold Tahoma
    g_crumb_font = make_font(face, 12, false, FW_NORMAL, q);
    g_crumb_hot = make_font(face, 12, true, FW_NORMAL, q);
    g_title_bold_off = cap_offset(g_title_bold);
}

void make_navbar_fonts()
{
    for (HFONT* f : { &g_nb_title, &g_nb_crumb, &g_nb_crumb_hot }) if (*f) { DeleteObject(*f); *f = nullptr; }
    const navbar::Skin& k = sk();
    if (!k.buttons) return;      // the bar's fonts go with its buttons (glass, luxwm's pixmap: the same layout)
    const wchar_t* tf = have_face(k.title_face) ? k.title_face : L"Tahoma";
    const wchar_t* cf = have_face(k.crumb_face) ? k.crumb_face : L"Tahoma";
    int tw = k.title_weight;
    BYTE q = ANTIALIASED_QUALITY;
    if (g_look.valid) {            // the Navbar's sizes, luxwm's face, weight and smoothing
        if (!g_look.face.empty()) tf = cf = (const wchar_t*)g_look.face.c_str();
        tw = g_look.weight; q = g_look.quality;
    }
    g_nb_title = make_font(tf, k.title_px, false, tw, q);
    g_nb_crumb = make_font(cf, k.crumb_px, false, FW_NORMAL, q);
    g_nb_crumb_hot = make_font(cf, k.crumb_px, true, FW_NORMAL, q);
    g_nb_title_off = cap_offset(g_nb_title);
    g_nb_crumb_off = cap_offset(g_nb_crumb);
    HDC dc = GetDC(nullptr);
    HGDIOBJ old = SelectObject(dc, g_nb_crumb);
    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    g_nb_crumb_asc = tm.tmAscent;
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
}

// coverage of pixel (x,y) by a segment of width w
double segment(int x, int y, double x0, double y0, double x1, double y1, double w)
{
    int n = 0;
    double dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            double px = x + (i + 0.5) / 4 - x0, py = y + (j + 0.5) / 4 - y0;
            double t = len2 > 0 ? (px * dx + py * dy) / len2 : 0;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            double ex = px - t * dx, ey = py - t * dy;
            if (ex * ex + ey * ey <= w * w / 4) n++;
        }
    return n / 16.0;
}

// The search button: 4074's grey glass magnifier (lh4074-181608.png: a lens
// of about 20 px with a dark rim and a light line across it, a thin light
// handle down to the left), 4051's coloured one (light blue lens, orange
// handle); drawn, 4x4 supersampled - only without the Longhorn files now
// (search_icon(): shell32's own icon).
HBITMAP draw_magnifier(bool colour)
{
    gdi::Bitmap* b = gdi::create_bitmap(MAG_W, MAG_H);
    for (int y = 0; y < MAG_H; y++) for (int x = 0; x < MAG_W; x++) b->row(y)[x] = 0;
    Canvas c{ b };
    const double cx = MAG_CX, cy = MAG_CY, r = 9.5;
    const double hx0 = cx - r * 0.72, hy0 = cy + r * 0.72, hx1 = 4.5, hy1 = 28.5;
    for (int y = 0; y < MAG_H; y++)
        for (int x = 0; x < MAG_W; x++) {
            double out = segment(x, y, hx0, hy0, hx1, hy1, colour ? 5.0 : 3.6);
            double in = segment(x, y, hx0, hy0, hx1, hy1, colour ? 3.0 : 1.8);
            if (out > 0) c.put(x, y, 40, 40, 44, out * 0.8);
            if (in > 0) {
                if (colour) c.put(x, y, 245, 150, 45, in);
                else c.put(x, y, 222, 224, 228, in);
            }
            double rim = disc(x, y, cx, cy, r + 0.6);
            if (rim > 0) c.put(x, y, colour ? 70 : 52, colour ? 100 : 54, colour ? 160 : 58, rim * 0.9);
            double lens = disc(x, y, cx, cy, r - 1.2);
            if (lens > 0) {
                double t = (y - (cy - r)) / (2 * r);
                if (colour) c.put(x, y, 150 + 80 * (1 - t), 200 + 45 * (1 - t), 245, lens);
                else c.put(x, y, 150 - 30 * t, 152 - 30 * t, 156 - 30 * t, lens * 0.9);
            }
            if (!colour && y == (int)cy && lens > 0.5) c.put(x, y, 205, 207, 210, 0.8);   // the light line across
            double gloss = disc(x, y, cx - 2.5, cy - 3, r * 0.45) * lens;
            if (gloss > 0) c.put(x, y, 255, 255, 255, gloss * (colour ? 0.7 : 0.25));
        }
    HBITMAP h = dib_from(b);
    gdi::destroy_bitmap(b);
    return h;
}

// The real one (doc/4051-explorer-research.md section 3): not a theme part
// but shell32.dll's icon group 23 (Search), its 32x32 image, which 4051 draws
// at (cw-81, 11); from the theme's Longhorn resources (slate4051: 4051's
// coloured glass, the 4074 themes: 4074's). Null without the Longhorn files:
// the drawn one.
HBITMAP search_icon()
{
    if (g_mag_looked) return g_mag_icon;
    g_mag_looked = true;
    std::string sys = shell::longhorn_resources();
    if (sys.empty()) return nullptr;
    HICON ic = (HICON)uiicons::load_themed("pe:" + sys + "/shell32.dll#23", IMAGE_ICON, 32, 32, 0);
    if (!ic) return nullptr;
    ICONINFO ii = {};
    if (GetIconInfo(ic, &ii) && ii.hbmColor) {
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = 32; bi.bmiHeader.biHeight = -32; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        std::vector<uint32_t> px(32 * 32), mask(32 * 32);
        HDC dc = GetDC(nullptr);
        bool ok = GetDIBits(dc, ii.hbmColor, 0, 32, px.data(), &bi, DIB_RGB_COLORS) != 0;
        bool alpha = false;
        for (uint32_t p : px) if (p >> 24) { alpha = true; break; }
        if (ok && !alpha && ii.hbmMask) GetDIBits(dc, ii.hbmMask, 0, 32, mask.data(), &bi, DIB_RGB_COLORS);
        ReleaseDC(nullptr, dc);
        void* bits = nullptr;
        if (ok) g_mag_icon = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (g_mag_icon && bits) {
            uint32_t* d = (uint32_t*)bits;
            for (int i = 0; i < 32 * 32; i++) {
                uint32_t p = px[(size_t)i], a = alpha ? p >> 24 : ((mask[(size_t)i] & 0xffffff) ? 0 : 255);
                d[i] = a << 24 | (((p >> 16) & 255) * a / 255) << 16 | (((p >> 8) & 255) * a / 255) << 8 | (p & 255) * a / 255;
            }
        }
    }
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    DestroyIcon(ic);
    return g_mag_icon;
}

// The header's last row, once, in every mode, size and state: 4051's clean
// edge (doc/4051-explorer-research.md section 2). 4074 draws Rebar.SPLITTER
// (a dark and a light row) over its navigation bar's last four rows, which
// end in the bar's own dark row: three lines (lh4074-195242.png y 168..171);
// 4051 has only the bar's dark row (33,36,36). So: the theme's Navbar (or
// InactiveNavbar) background's own last row - its maximised variants have
// none (Slate's MaxNavbar ends in the band colour) and take the restored
// one's -, else 4051's colour; opaque on glass as well (luxwm's plate under
// it would add its own light/dark lines there: the frame is extended one row
// further, browser.cpp extend_frame, so they lie under this row and the bar).
// Drawn over the travel buttons: their drop-down's bottom row (4051's art ends
// on the band's last row) gives way to the line, so the box sits on it.
void paint_bottom(HDC dc, int R, int H)
{
    const navbar::Skin& k = sk();
    const navbar::Img& bg = k.bg[g_active ? navbar::ACTIVE : navbar::INACTIVE];
    const RECT& m = k.bg_margins[g_active ? navbar::ACTIVE : navbar::INACTIVE];
    if (k.buttons && bg.bmp && m.bottom > 0) navbar::draw_row(dc, bg, bg.h - 1, RECT{ 0, H - 1, R, H }, m.left, m.right);
    else blend(dc, 0, H - 1, R, H, RGB(33, 36, 36), 255);
}

void paint_right(HDC dc, int R);
void paint_navbar(HDC dc, int R, int H)
{
    const navbar::Skin& k = sk();
    int v = variant();
    RECT all = { 0, 0, R, H };
    navbar::draw_nine(dc, k.bg[v], all, k.bg_margins[v]);     // no Rebar.SPLITTER (4074's double line)
    paint_nav(dc);
    paint_right(dc, R);
}

Right right_layout(int R)
{
    const navbar::Skin& k = sk();
    Right r;
    int v = variant();
    bool fav = g_show[SHOW_FAVORITES], mag = g_show[SHOW_SEARCH];
    int sw = R < 500 ? (R / 4 > 60 ? R / 4 : 60) : SEARCH_W;
    if (g_hsize == SIZE_MEDIUM) {
        // the navigation bar's places (dy: the buttons' row on glass); what is
        // hidden gives its room to what is left of it
        int dy = top_off(), shift = fav ? 0 : 40;       // the band's top
        if (fav) { r.fav = k.fav[v]; r.fav_x = R - k.fav_right; r.fav_y = dy + k.fav_y; }
        r.mag = mag;
        r.mag_x = R - k.mag_right - MAG_CX + shift; r.mag_y = dy + k.mag_cy - MAG_CY; r.mag_w = MAG_W; r.mag_h = MAG_H;
        r.grip_x = R - k.grip_right + shift + (mag ? 0 : 34);
        r.grip_y = dy;
        r.edit_x = r.grip_x - 8 - sw;
        r.left = g_search_open && mag ? r.grip_x - 8 - SEARCH_W : r.grip_x;
        return r;
    }
    // Small, Large: the small star (4074's SmallFavorites, else the normal one
    // box-filtered to two thirds) and magnifier; Extra-Large: the normal ones;
    // at the bottom (Large, Extra-Large) or on the buttons' row (Small), no gripper
    bool sm = small_right();
    const navbar::Img* fv = sm ? (k.small ? k.sfav[v] : shrunk().fav[v]) : k.fav[v];
    int fw = fv[navbar::NORMAL].w, fh = fv[navbar::NORMAL].h;
    int pad = navbar_mode() && k.splitter.bmp ? 5 : 3;
    int fy = small_size() ? btn_top() : g_height - pad - fh;
    int fx = R - fw - (sm ? 4 : 6);
    if (fav) { r.fav = fv; r.fav_x = fx; r.fav_y = fy; }
    int star_cx = fx + fw / 2, star_cy = fy + (sm ? 14 : fh * 21 / 49);
    r.mag = mag;
    r.mag_w = sm ? MAG_W * 2 / 3 : MAG_W; r.mag_h = sm ? MAG_H * 2 / 3 : MAG_H;
    int mcx = sm ? MAG_CX * 2 / 3 : MAG_CX, mcy = sm ? MAG_CY * 2 / 3 : MAG_CY;
    int lens_cx = fav ? star_cx - (sm ? 25 : 37) : star_cx;
    r.mag_x = lens_cx - mcx; r.mag_y = star_cy - 2 - mcy;
    int first = mag ? r.mag_x + 2 : fav ? fx : R - 4;
    r.edit_x = first - 6 - sw;
    r.left = g_search_open && mag ? first - 6 - SEARCH_W : first;
    return r;
}

// the gripper, the star and the magnifier (the search box hides behind it)
void paint_right(HDC dc, int R)
{
    const navbar::Skin& k = sk();
    int v = variant();
    Right r = right_layout(R);
    int dy = r.grip_y;
    if (r.grip_x >= 0 && k.gripper.bmp && (navbar_mode() || !k.gripper_tile)) {
        int gx = r.grip_x;
        if (k.gripper_tile) {
            // Rebar.GRIPPER, TILE with sizing margins 2 1 2 1: the rows between the
            // margins repeat (4051: a dot every 7 rows)
            HDC mem = CreateCompatibleDC(dc);
            HGDIOBJ old = SelectObject(mem, k.gripper.bmp);
            BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            int period = k.gripper.h - 3;
            for (int y = k.grip_y0; y < k.grip_y1 && period > 0; y++) {
                int r = y - k.grip_y0, sy = r < 2 ? r : 2 + (r - 2) % period;
                AlphaBlend(dc, gx, dy + y, k.gripper.w, 1, mem, 0, sy, k.gripper.w, 1, bf);
            }
            SelectObject(mem, old);
            DeleteDC(mem);
        } else {
            RECT gr = { gx, dy + k.grip_y0, gx + k.gripper.w, dy + k.grip_y1 }, m = { 0, 14, 0, 14 };
            navbar::draw_nine(dc, k.gripper, gr, m, 0, 3);
        }
    }
    int fs = part_state(FAV, FAV);
    if (fs == navbar::SPLITPRESSED) fs = navbar::PRESSED;
    (void)v;
    if (r.fav) navbar::draw(dc, r.fav[fs], r.fav_x, r.fav_y);
    if (!r.mag) return;
    bool colour = k.colour_magnifier;
    HBITMAP icon = search_icon();
    if (!icon && !g_mag[colour]) g_mag[colour] = draw_magnifier(colour);
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, icon ? icon : g_mag[colour]);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)(g_nav_hot == SEARCH ? 255 : 230), AC_SRC_ALPHA };
    int my = r.mag_y + (g_nav_pressed == SEARCH && g_nav_hot == SEARCH ? 1 : 0);
    if (icon) {   // 32x32 over the drawn one's 30x32 cell (scaled with it: the small sizes)
        int w = r.mag_w * 32 / MAG_W, h = r.mag_h;
        AlphaBlend(dc, r.mag_x - (w - r.mag_w) / 2, my, w, h, mem, 0, 0, 32, 32, bf);
    } else
        AlphaBlend(dc, r.mag_x, my, r.mag_w, r.mag_h, mem, 0, 0, MAG_W, MAG_H, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
}

// luxwm's header pixmap: its last H rows (its last row lies against the bottom
// of our extension). False when there is none (yet): the gradient instead.
// luxwm's pixmap, its last H rows, into the header's rows (false: none)
bool blit_pixmap(HDC dc, int R, int H)
{
    if (g_pix_stale) {
        g_pix_stale = false;
        if (g_pix) DeleteObject(g_pix);
        g_pix = FlxpDwmGetHeaderBitmap(GetParent(g_wnd));
        g_pix_w = g_pix_h = 0;
        BITMAP bm = {};
        if (g_pix && GetObjectW(g_pix, sizeof bm, &bm)) { g_pix_w = bm.bmWidth; g_pix_h = bm.bmHeight < 0 ? -bm.bmHeight : bm.bmHeight; }
    }
    if (!g_pix || g_pix_w <= 0 || g_pix_h <= 0) return false;
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, g_pix);
    int sy = g_pix_h - H, dy = 0, h = H;
    if (sy < 0) { dy = -sy; h = g_pix_h; sy = 0; }
    int w = R < g_pix_w ? R : g_pix_w;
    BitBlt(dc, 0, dy, w, h, mem, 0, sy, SRCCOPY);
    if (w < R) StretchBlt(dc, w, dy, R - w, h, mem, g_pix_w - 1, sy, 1, h, SRCCOPY);   // a resize under way
    if (dy) StretchBlt(dc, 0, 0, R, dy, dc, 0, dy, R, 1, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
    return true;
}

bool paint_pixmap(HDC dc, int R, int H)
{
    if (!blit_pixmap(dc, R, H)) return false;
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, g_pix);
    // the divider: a light and a dark soft line in the band's colour there
    COLORREF at = GetPixel(mem, g_divider < g_pix_w ? g_divider : g_pix_w - 1, g_pix_h - H / 2 > 0 ? g_pix_h - H / 2 : 0);
    SelectObject(mem, old);
    DeleteDC(mem);
    auto shade = [](COLORREF c, COLORREF to, int pct) {
        return RGB(GetRValue(c) + (GetRValue(to) - GetRValue(c)) * pct / 100, GetGValue(c) + (GetGValue(to) - GetGValue(c)) * pct / 100,
                   GetBValue(c) + (GetBValue(to) - GetBValue(c)) * pct / 100);
    };
    // the theme's NavbarBase gripper instead; not Rebar.GRIPPER's dots (4051
    // under Luna has a plain line there: lh4051-232843.png)
    if (themed_free() && !sk().gripper_tile) return true;
    if (themed_free() ? right_layout(R).grip_x < 0 : !g_show[SHOW_SEARCH]) return true;
    int dx = themed_free() ? right_layout(R).grip_x : g_divider;
    soft_line(dc, dx, 2, H - 4, shade(at, RGB(255, 255, 255), 75), 255);
    soft_line(dc, dx + 1, 2, H - 4, shade(at, RGB(0, 0, 0), 45), 255);
    return true;
}

// comctl32's DrawShadowText TST_CONTINUOUS as luxwm draws it (frame.c
// ms_shadow_text): the text into a (w+10)x(h+10) buffer at (5,5), every touched
// pixel 255, a box over the next four pixels / 4, then the row and the four
// above / 4, laid at (x - 4 + dx, top - 8 + dy); TST_SINGLE: one offset copy.
void draw_shadow(HDC dc, const std::u16string& w, int x, int top, int type, int dx, int dy, COLORREF c)
{
    if (type == 1) {
        COLORREF old = SetTextColor(dc, c);
        TextOutW(dc, x + dx, top + dy, (LPCWSTR)w.c_str(), (int)w.size());
        SetTextColor(dc, old);
        return;
    }
    SIZE sz = {};
    GetTextExtentPoint32W(dc, (LPCWSTR)w.c_str(), (int)w.size(), &sz);
    int cx = sz.cx + 10, cy = sz.cy + 10;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = cx; bi.bmiHeader.biHeight = -cy; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP b = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!b) { DeleteDC(mem); return; }
    memset(bits, 0, (size_t)cx * cy * 4);
    HGDIOBJ ob = SelectObject(mem, b), of = SelectObject(mem, GetCurrentObject(dc, OBJ_FONT));
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(255, 255, 255));
    TextOutW(mem, 5, 5, (LPCWSTR)w.c_str(), (int)w.size());
    uint32_t* p = (uint32_t*)bits;
    std::vector<int> hb((size_t)cx * cy);
    for (int r = 0; r < cy; r++)
        for (int i = 0; i < cx; i++) {
            int v = 0;
            if (i < cx - 5) { for (int k = 1; k <= 4; k++) v += (p[r * cx + i + k] & 0xffffff) ? 255 : 0; v >>= 2; }
            else v = (p[r * cx + i] & 0xffffff) ? 255 : 0;
            hb[(size_t)r * cx + i] = v;
        }
    for (int r = 0; r < cy; r++)
        for (int i = 0; i < cx; i++) {
            int v = hb[(size_t)r * cx + i];
            if (r > 4) { v = 0; for (int k = 0; k <= 4; k++) v += hb[(size_t)(r - k) * cx + i]; v >>= 2; if (v > 255) v = 255; }
            p[r * cx + i] = (uint32_t)v << 24 | (GetRValue(c) * v / 255) << 16 | (GetGValue(c) * v / 255) << 8 | GetBValue(c) * v / 255;
        }
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(dc, x - 4 + dx, top - 8 + dy, cx, cy, mem, 0, 0, cx, cy, bf);
    SelectObject(mem, of); SelectObject(mem, ob);
    DeleteObject(b); DeleteDC(mem);
}

void draw_text(HDC dc, HFONT f, const std::string& s, int x, int top, bool on_glass)
{
    HGDIOBJ old = SelectObject(dc, f);
    std::u16string w = W(s);
    RECT r = { x, top, x + 2000, top + 200 };
    int v = g_active ? 0 : 1;
    bool max = zoomed();
    if (!on_glass && g_look.valid) {
        // maximised: luxwm draws MAXCAPTION's shadow (maxcaption.shadow.*) when the theme has one
        const CaptionLook& L = g_look;
        bool m = max && L.max_sh;
        int t = m ? L.max_sh_type[v] : L.sh_type[v];
        if (t) draw_shadow(dc, w, x, top, t, m ? L.max_sh_dx[v] : L.sh_dx[v], m ? L.max_sh_dy[v] : L.sh_dy[v], m ? L.max_sh_rgb[v] : L.sh_rgb[v]);
    }
    COLORREF glow = on_glass && g_look.valid ? (max ? g_look.max_glow_rgb : g_look.glow_rgb) : RGB(255, 255, 255);
    bool halo = on_glass && (!g_look.valid || (max ? g_look.max_glow : g_look.glow));
    FlxpDrawGlowTextW(dc, (LPCWSTR)w.c_str(), (int)w.size(), &r, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX, halo ? 2 : 0, glow);
    SelectObject(dc, old);
}

void apply_mode(Mode m)
{
    g_mode = m;
    g_search_open = false;
    make_navbar_fonts();
    g_nav_hot = g_nav_pressed = NONE;
}

// ---- the breadcrumb bar as 4074 builds it (lh4074-224731.png, NOTES) --------------
// A field behind the segments (shown while the mouse is over it, a drop-down is
// open or it is being edited), each segment a split button: the name
// (navigates) and an arrow part over the separator after it (drops the
// folders beside it, itself bold); the last segment's arrow only shows when
// hot. A drop button at the field's right end (history). The theme's
// BreadcrumbBarToolbar SPLITBUTTON/SPLITBUTTONDROPDOWN where the style has them
// (aero.msstyles), else 4074's drawn look.


// a top (a) to bottom (b) gradient, row by row
void vgradient(HDC dc, int x0, int x1, int y0, int y1, COLORREF a, COLORREF b)
{
    int n = y1 - y0;
    for (int y = y0; y < y1 && x1 > x0; y++) {
        int k = y - y0, d = n > 1 ? n - 1 : 1;
        fill(dc, x0, y, x1, y + 1, RGB(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * k / d,
                                       GetGValue(a) + (GetGValue(b) - GetGValue(a)) * k / d,
                                       GetBValue(a) + (GetBValue(b) - GetBValue(a)) * k / d));
    }
}

// colour c at alpha a (0..255) over the rectangle: translucent on glass too
void blend(HDC dc, int x0, int y0, int x1, int y1, COLORREF c, int a)
{
    if (x1 <= x0 || y1 <= y0) return;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = 1; bi.bmiHeader.biHeight = 1;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP b = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!b) return;
    *(uint32_t*)bits = (uint32_t)a << 24 | (uint32_t)(GetRValue(c) * a / 255) << 16 | (uint32_t)(GetGValue(c) * a / 255) << 8 | (uint32_t)(GetBValue(c) * a / 255);
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, b);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(dc, x0, y0, x1 - x0, y1 - y0, mem, 0, 0, 1, 1, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(b);
}

void frame_blend(HDC dc, const RECT& r, int a_top, int a_rest)
{
    blend(dc, r.left, r.top, r.right, r.top + 1, RGB(0, 0, 0), a_top);
    blend(dc, r.left, r.bottom - 1, r.right, r.bottom, RGB(0, 0, 0), a_rest);
    blend(dc, r.left, r.top + 1, r.left + 1, r.bottom - 1, RGB(0, 0, 0), a_rest);
    blend(dc, r.right - 1, r.top + 1, r.right, r.bottom - 1, RGB(0, 0, 0), a_rest);
}

// a down-pointing triangle w wide, its top row at y, centred at cx
void down_glyph(HDC dc, int cx, int y, int w, COLORREF c)
{
    for (int k = 0; k <= w / 2; k++) fill(dc, cx - w / 2 + k, y + k, cx + w / 2 - k + 1, y + k + 1, c);
}

int last_shown()
{
    for (int i = (int)g_crumbs.size() - 1; i >= 0; i--) if (!IsRectEmpty(&g_crumbs[(size_t)i].text)) return i;
    return -1;
}

int first_shown()
{
    for (size_t i = 0; i < g_crumbs.size(); i++) if (!IsRectEmpty(&g_crumbs[i].text)) return (int)i;
    return -1;
}

// the segments' rows: 23, centred on the text (the field is 2 more above and below: 27)
void seg_rows(int* top, int* bottom)
{
    int i = first_shown();
    RECT t = i >= 0 ? g_crumbs[(size_t)i].text : RECT{ 0, crumb_baseline() - 12, 0, crumb_baseline() + 4 };
    int c = (t.top + t.bottom) / 2;
    *top = c - 11; *bottom = c + 12;
}

RECT arrow_box(int i)
{
    const Crumb& c = g_crumbs[(size_t)i];
    int cx = i == last_shown() ? c.text.right + SEG_PAD + ARROW_BOX / 2
                               : c.text.right + arrow_left_gap() + arrow_width() / 2;
    int t, b; seg_rows(&t, &b);
    return RECT{ cx - ARROW_BOX / 2 - 2, t, cx + ARROW_BOX / 2, b };
}

RECT seg_rect(int i)
{
    RECT a = arrow_box(i);
    return RECT{ g_crumbs[(size_t)i].text.left - SEG_PAD, a.top, a.right, a.bottom };
}

RECT field_rect()
{
    if (!g_wnd || first_shown() < 0) return RECT{};
    RECT rc; GetClientRect(g_wnd, &rc);
    int t, b; seg_rows(&t, &b);
    return RECT{ crumb_x() - SEG_PAD - 3, t - 2, crumb_limit(rc.right) + 4, b + 2 };
}

struct Hit { int seg = -1; bool arrow = false, drop = false, field = false; };

Hit hit(POINT p)
{
    Hit h;
    RECT f = field_rect();
    if (!PtInRect(&f, p)) return h;
    h.field = true;
    if (g_editing) return h;
    if (p.x >= f.right - FIELD_DROP_W) { h.drop = true; return h; }
    for (int i = 0; i < (int)g_crumbs.size(); i++) {
        if (IsRectEmpty(&g_crumbs[(size_t)i].text)) continue;
        RECT r = seg_rect(i);
        if (!PtInRect(&r, p)) continue;
        RECT a = arrow_box(i);
        h.seg = i;
        h.arrow = PtInRect(&a, p) != 0;
        return h;
    }
    return h;
}

void invalidate_field()
{
    if (!g_wnd) return;
    if (vision()) { InvalidateRect(g_wnd, nullptr, FALSE); return; }
    RECT f = field_rect();
    InflateRect(&f, 2, 2);
    InvalidateRect(g_wnd, &f, TRUE);
}

// a segment: state 1 hot, 2 pressed; arrow_state likewise for its arrow part
void paint_segment(HDC dc, int i, int state, int arrow_state)
{
    RECT s = seg_rect(i), a = arrow_box(i);
    RECT btn = { s.left, s.top, a.left, s.bottom };
    const navbar::Skin& k = sk();
    if (k.crumb_btn.bmp) {
        RECT mb = k.crumb_btn_margins, md = k.crumb_drop_margins;
        if (!mb.left && !mb.right) mb = RECT{ 3, 3, 3, 3 };
        if (!md.left && !md.right) md = RECT{ 2, 3, 2, 3 };
        navbar::draw_nine(dc, k.crumb_btn, btn, mb, state, 6);
        navbar::draw_nine(dc, k.crumb_drop, a, md, arrow_state, 6);
        return;
    }
    if (state == 2 || arrow_state == 2) {
        // pressed/open: sunken, the dark edge and a light inner top (Codex on lh4074-224731.png)
        vgradient(dc, s.left + 1, a.left, s.top + 1, s.bottom - 1, RGB(164, 172, 174), RGB(122, 130, 132));
        vgradient(dc, a.left, s.right - 1, s.top + 1, s.bottom - 1, RGB(122, 130, 132), RGB(157, 165, 167));
        fill(dc, s.left + 1, s.top + 1, s.right - 1, s.top + 2, RGB(203, 211, 214));
        HBRUSH br = CreateSolidBrush(RGB(75, 75, 75));
        FrameRect(dc, &s, br);
        DeleteObject(br);
        fill(dc, a.left, s.top + 1, a.left + 1, s.bottom - 1, RGB(75, 75, 75));
    } else {
        blend(dc, s.left + 1, s.top + 1, s.right - 1, s.bottom - 1, RGB(255, 255, 255), 160);
        frame_blend(dc, s, 70, 110);
        blend(dc, a.left, s.top + 2, a.left + 1, s.bottom - 2, RGB(0, 0, 0), 60);
    }
}

void paint_field(HDC dc)
{
    RECT f = field_rect();
    if (IsRectEmpty(&f)) return;
    // 4074: a darker field (#7a7a7b on the grey header), a light top rule (150), a dark bottom (60)
    if (g_editing) blend(dc, f.left + 1, f.top + 1, f.right - 1, f.bottom - 1, RGB(255, 255, 255), 255);
    else blend(dc, f.left + 1, f.top + 1, f.right - 1, f.bottom - 1, RGB(0, 0, 0), 50);
    blend(dc, f.left, f.top, f.right, f.top + 1, RGB(255, 255, 255), 90);
    blend(dc, f.left, f.bottom - 1, f.right, f.bottom, RGB(0, 0, 0), 170);
    blend(dc, f.left, f.top + 1, f.left + 1, f.bottom - 1, RGB(0, 0, 0), 110);
    blend(dc, f.right - 1, f.top + 1, f.right, f.bottom - 1, RGB(0, 0, 0), 110);
    if (g_editing) return;
    // the drop button: pale, white at the top, a dark chevron
    int x0 = f.right - FIELD_DROP_W, x1 = f.right - 1;
    vgradient(dc, x0 + 1, x1, f.top + 1, f.bottom - 1, RGB(255, 255, 255), RGB(203, 204, 218));
    blend(dc, x0, f.top + 1, x0 + 1, f.bottom - 1, RGB(0, 0, 0), 110);
    down_glyph(dc, (x0 + x1) / 2, (f.top + f.bottom) / 2 - 2, 7, RGB(60, 64, 80));
}

void vision_menu_begin(int part, int* x);
void vision_menu_end();
bool vision_children_menu();

// segment i's drop-down: its siblings, itself bold (4074); in vision mode with
// header_crumb_menu = children its subfolders, the next segment bold (Vista)
void open_menu(int i)
{
    bool kids_of = vision() && vision_children_menu();
    std::string here = g_crumbs[(size_t)i].path;
    if (kids_of) here = (size_t)i + 1 < g_crumbs.size() ? g_crumbs[(size_t)i + 1].path : "";
    auto f = shell::folder_for(g_crumbs[(size_t)i].path);
    auto parent = kids_of ? f : f ? f->parent() : nullptr;
    std::vector<shell::Item> kids;
    if (parent) parent->enumerate(kids, false);
    shell::sort_items(kids, shell::SortBy::Name, true);
    HMENU m = CreatePopupMenu();
    std::vector<std::string> paths;
    for (const shell::Item& it : kids) {
        if (!it.is_folder()) continue;
        paths.push_back(it.path);
        AppendMenuW(m, MF_STRING, (UINT)paths.size(), (LPCWSTR)W(it.name).c_str());
        if (it.path == here) SetMenuDefaultItem(m, (UINT)paths.size(), FALSE);   // bold: the one on the path
        if (paths.size() >= 250) break;
    }
    if (paths.empty()) AppendMenuW(m, MF_STRING | MF_GRAYED, 0, L"(Empty)");
    POINT pt = { 0, g_height };    // flush under the header (lh4074-193810.png)
    if (vision()) {
        vision_menu_begin(LHNAV_SEP_OF(i), (int*)&pt.x);
    } else {
        g_open = i;
        invalidate_field();
        UpdateWindow(g_wnd);
        RECT s = seg_rect(i);
        pt.x = s.left;
    }
    ClientToScreen(g_wnd, &pt);
    int cmd = TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, pt.x, pt.y, 0, GetParent(g_wnd), nullptr);
    DestroyMenu(m);
    if (vision()) {
        vision_menu_end();
        if (cmd > 0 && (size_t)cmd <= paths.size() && g_cb.navigate) g_cb.navigate(paths[(size_t)cmd - 1]);
        return;
    }
    g_open = -1;
    POINT cur; GetCursorPos(&cur); ScreenToClient(g_wnd, &cur);
    Hit h = hit(cur);
    g_hot = h.seg; g_hot_arrow = h.arrow; g_bar_hot = h.field;
    invalidate_field();
    if (cmd > 0 && (size_t)cmd <= paths.size() && g_cb.navigate) g_cb.navigate(paths[(size_t)cmd - 1]);
}

void end_edit(bool go);

LRESULT CALLBACK addr_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE)) { end_edit(wp == VK_RETURN); return 0; }
    if (msg == WM_CHAR && (wp == '\r' || wp == 0x1b)) return 0;
    if (msg == WM_KILLFOCUS) {
        LRESULT r = CallWindowProcW(g_addr_old, h, msg, wp, lp);
        end_edit(false);
        return r;
    }
    return CallWindowProcW(g_addr_old, h, msg, wp, lp);
}

void vision_begin_edit();

void begin_edit()
{
    if (!g_wnd || g_crumbs.empty() || g_editing) return;
    if (vision()) { vision_begin_edit(); return; }
    RECT f = field_rect();
    if (IsRectEmpty(&f)) return;
    if (!g_addr) {
        g_addr = CreateWindowExW(0, L"Edit", nullptr, WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, g_wnd, nullptr, nullptr, nullptr);
        SendMessageW(g_addr, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), FALSE);
        g_addr_old = (WNDPROC)SetWindowLongPtrW(g_addr, GWLP_WNDPROC, (LONG_PTR)addr_proc);
    }
    g_editing = true;
    g_hot = g_pressed = -1;
    int eh = 16, ey = f.top + (f.bottom - f.top - eh) / 2;
    SetWindowPos(g_addr, HWND_TOP, f.left + 4, ey, f.right - f.left - 8, eh, SWP_SHOWWINDOW);
    SetWindowTextW(g_addr, (LPCWSTR)W(g_crumbs.back().path).c_str());
    SendMessageW(g_addr, EM_SETSEL, 0, -1);
    SetFocus(g_addr);
    invalidate_field();
}

void end_edit(bool go)
{
    if (!g_editing) return;
    g_editing = false;
    int n = GetWindowTextLengthW(g_addr);
    std::vector<WCHAR> buf((size_t)n + 1);
    GetWindowTextW(g_addr, buf.data(), n + 1);
    std::string text = w32::utf8_of(buf.data());
    bool had_focus = GetFocus() == g_addr;
    ShowWindow(g_addr, SW_HIDE);
    if (vision()) lhnav_set_field(g_v.nav, LHNAV_ADDRESS, nullptr, -1, 0);
    invalidate_field();
    if (had_focus) SetFocus(GetParent(g_wnd));       // the frame gives it to the list
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.pop_back();
    if (!text.empty() && text[0] == '~') text = shell::home_dir() + text.substr(1);
    if (go && !text.empty() && g_cb.navigate) g_cb.navigate(text);
}

// a frame as the breadcrumb's hot segment (4074's look): light inside, a dark rim;
// pressed or open darker
void paint_tools(HDC dc)
{
    for (size_t i = 0; i < g_tools.size(); i++) {
        const ToolBtn& b = g_tools[i];
        RECT r = b.r;
        bool hot = (int)i == g_tool_hot && b.enabled, down = (int)i == g_tool_pressed;
        if (down) {
            blend(dc, r.left + 1, r.top + 1, r.right - 1, r.bottom - 1, RGB(0, 0, 0), 60);
            frame_blend(dc, r, 110, 150);
        } else if (hot) {
            blend(dc, r.left + 1, r.top + 1, r.right - 1, r.bottom - 1, RGB(255, 255, 255), 160);
            frame_blend(dc, r, 70, 110);
        }
        if (b.img.bmp) {
            HDC mem = CreateCompatibleDC(dc);
            HGDIOBJ old = SelectObject(mem, b.img.bmp);
            BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)(b.enabled ? 255 : 90), AC_SRC_ALPHA };
            int ix = r.left + (TOOL_W - b.img.w) / 2 + (down ? 1 : 0), iy = r.top + (TOOL_H - b.img.h) / 2 + (down ? 1 : 0);
            AlphaBlend(dc, ix, iy, b.img.w, b.img.h, mem, 0, 0, b.img.w, b.img.h, bf);
            SelectObject(mem, old);
            DeleteDC(mem);
        }
        if (b.t.dropdown) {
            if (hot || down) blend(dc, r.left + TOOL_W, r.top + 3, r.left + TOOL_W + 1, r.bottom - 3, RGB(0, 0, 0), 60);
            down_glyph(dc, r.left + TOOL_W + TOOL_DROP_W / 2, (r.top + r.bottom) / 2 - 1, 5, b.enabled ? ARROW : RGB(150, 150, 150));
        }
    }
}

void paint(HDC dc)
{
    if (vision()) { vision_paint(dc); return; }
    // Robust against a missed or late composition change (_LHWM_COMPOSITED
    // flipping while we were not looking): the mode is checked on every paint;
    // a wrong one is corrected and the frame told to refit the height and the
    // extension (the glass header must never get the navigation bar or v.v.)
    Mode want = choose_mode();
    if (want != g_mode) {
        apply_mode(want);
        layout();
        PostMessageW(GetParent(g_wnd), WM_DWMCOMPOSITIONCHANGED, 0, 0);
    }
    RECT rc; GetClientRect(g_wnd, &rc);
    int R = rc.right, H = rc.bottom;
    bool on_glass = g_mode == GLASS;
    bool caption = false;            // luxwm's caption band under the glass layout
    if (on_glass) {
        FlxpClearRect(dc, &rc);   // luxwm's plate shows through
        if (themed_free()) paint_right(dc, R);   // 4074-dwm.png: gripper, magnifier, star
        else if (g_show[SHOW_SEARCH]) soft_line(dc, g_divider, top_off() + 2, base_height() - 4, RGB(255, 255, 255), 210);
    } else if (navbar_mode()) {
        paint_navbar(dc, R, H);
    } else {
        caption = paint_pixmap(dc, R, H);
        if (!caption) paint_slate(dc, R, H);
        if (themed_free()) paint_right(dc, R);
    }
    if (!navbar_mode()) paint_nav(dc);
    // over the buttons: their drop-down (hot, pressed) ends on the line, which stays whole
    paint_bottom(dc, R, H);
    SetBkMode(dc, TRANSPARENT);
    COLORREF title_c, crumb_c;
    int v = variant();
    if (g_look.valid && (navbar_mode() || caption || on_glass)) {
        // luxwm's caption colours, its maximised ones when zoomed (aero: white)
        title_c = crumb_c = zoomed() ? g_look.max_text[g_active ? 0 : 1] : g_look.text[g_active ? 0 : 1];
    } else if (navbar_mode()) {
        title_c = sk().title_color[v]; crumb_c = sk().crumb_color[v];
    } else if (caption) {
        // 4051 under Luna: the caption's text colour for the title, black crumbs
        FLXP_DWM_FRAME_METRICS fm = {}; fm.cbSize = sizeof fm;
        FlxpDwmGetFrameMetrics(&fm);
        title_c = fm.bValid ? (g_active ? fm.crText : fm.crInactiveText) : RGB(255, 255, 255);
        crumb_c = RGB(0, 0, 0);
    } else {
        title_c = crumb_c = on_glass ? GLASS_TEXT : SLATE_TEXT;
    }
    SetTextColor(dc, title_c);
    HRGN clip = CreateRectRgn(0, 0, crumb_limit(R) + 8, H);
    SelectClipRgn(dc, clip);
    int top;
    HFONT tf = caption ? g_title_bold : title_font();
    if (nb_buttons() && (tf == g_nb_title || tf == g_title_bold)) {
        // the navigation bar's measured capitals, in every mode
        top = top_off() + sk().title_cap - (tf == g_nb_title ? g_nb_title_off : g_title_bold_off);
    } else {
        int asc = 0;
        HDC mdc = GetDC(g_wnd);
        text_width(mdc, tf, "X", &asc);
        ReleaseDC(g_wnd, mdc);
        top = title_baseline() - asc;
    }
    if (draws_title()) draw_text(dc, tf, g_title, title_x(), top, on_glass);
    SetTextColor(dc, crumb_c);
    if (g_bar_hot || g_open >= 0 || g_editing) paint_field(dc);
    for (size_t i = 0; i < g_crumbs.size() && !g_editing && g_show[SHOW_BREADCRUMB]; i++) {
        const Crumb& c = g_crumbs[i];
        if (IsRectEmpty(&c.text)) continue;
        int si = (int)i;
        bool open = si == g_open, hot = si == g_hot, last = si == last_shown();
        if (open || hot || si == g_pressed)
            paint_segment(dc, si, open || si == g_pressed ? 2 : 1, open ? 2 : 1);
        draw_text(dc, crumb_font(false), c.shown, c.text.left, c.text.top, on_glass);
        if (open || (last && hot)) {
            // the arrow part: open, it points down (4074); the last segment's only while hot
            RECT a = arrow_box(si);
            if (sk().crumb_down.bmp) {
                const navbar::Img& g = sk().crumb_down;
                int gh = g.h / 6;
                navbar::draw(dc, g, (a.left + a.right - g.w) / 2, (a.top + a.bottom - gh) / 2, open ? 2 : 1, 6);
            } else {
                down_glyph(dc, (a.left + a.right) / 2, (a.top + a.bottom) / 2 - 2, 7,
                           open && !sk().crumb_btn.bmp ? RGB(230, 230, 230) : ARROW);
            }
            continue;
        }
        if (last) continue;
        if (navbar_mode()) {
            // the theme's separator glyph (6 states, normal first), centred on the capitals
            const navbar::Img& a = sk().arrow[v];
            int caph = sk().crumb_px * 3 / 4;
            int gh = a.h / 6, cap = small_size() ? c.text.top + g_nb_crumb_off : top_off() + sk().crumb_cap;
            navbar::draw(dc, a, c.text.right + arrow_left_gap(), cap + (caph - gh) / 2, 0, 6);
            continue;
        }
        // the separator: a right-pointing 4x7 triangle on the text's lower rows,
        // outlined 1 px white (the triangle grown by its 4 neighbours, as measured)
        int ax = c.text.right + ARROW_GAP_L, ab = crumb_baseline();
        for (int k = 0; k < ARROW_W; k++) {
            fill(dc, ax + k - 1, ab - 7 + k, ax + k, ab - k, RGB(253, 254, 251));
            fill(dc, ax + k, ab - 8 + k, ax + k + 1, ab - k + 1, RGB(253, 254, 251));
            fill(dc, ax + k + 1, ab - 7 + k, ax + k + 2, ab - k, RGB(253, 254, 251));
        }
        for (int k = 0; k < ARROW_W; k++) fill(dc, ax + k, ab - 7 + k, ax + k + 1, ab - k, ARROW);
    }
    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
    paint_tools(dc);
}

// Hover repaints only what changes: on glass every repainted rectangle is
// damage the compositor re-blurs, and the less of the header's top edge that
// is redrawn, the less a blur seam along luxwm's caption can appear there.
void invalidate_tools()
{
    for (const ToolBtn& b : g_tools) { RECT r = b.r; InflateRect(&r, 1, 1); InvalidateRect(g_wnd, &r, TRUE); }
}

int tool_at(POINT p)
{
    for (size_t i = 0; i < g_tools.size(); i++) if (PtInRect(&g_tools[i].r, p)) return (int)i;
    return -1;
}

void invalidate_crumb(int i)
{
    if (i < 0 || i >= (int)g_crumbs.size()) return;
    RECT r = seg_rect(i);
    InflateRect(&r, 2, 2);              // the glow
    InvalidateRect(g_wnd, &r, TRUE);
}

void invalidate_nav()
{
    int t = nb_buttons() ? btn_top() : nav_top();
    RECT r = { 0, t, (std::max)(NAV_X + NAV_W, travel_right()) + 2, t + (std::max)(NAV_H, travel_h()) };
    InvalidateRect(g_wnd, &r, TRUE);
    if (nb_buttons()) {        // the magnifier and the star
        RECT rc; GetClientRect(g_wnd, &rc);
        Right rl = right_layout(rc.right);
        int x = (std::min)(rc.right - sk().mag_right - MAG_CX, (std::min)(rl.mag_x, rl.fav ? rl.fav_x : rc.right));
        RECT b = { x, 0, rc.right, rc.bottom };
        InvalidateRect(g_wnd, &b, TRUE);
    }
}

void set_nav_hot(int part)
{
    if (part != NONE && !enabled(part)) part = NONE;
    if (part == g_nav_hot) return;
    g_nav_hot = part;
    invalidate_nav();
}

// ---- vision: the header through lhnav --------------------------------------------------
// docs/NAVBAR.md "flxp's header": lhnav lays out, paints and hit-tests the
// travel orb, the title in luxwm's 15-row deco, the breadcrumb, the search box
// and the right-hand items (nav.ini `tools`); flxp keeps what a click does (the
// history and its menus, the crumbs' drop-down menus, the bookmarks, New Folder,
// Views, the filter), the address and search EDITs placed over lhnav's boxes,
// and the background under it (glass, luxwm's _LHWM_HEADER_PIXMAP, else the
// theme's navigation bar). The header window covers lhnav_extend_top() rows:
// the header plus the two rows on which luxwm draws the plate's line pair.

// luxwm's frame theme directory (_LHWM_THEME "DIR|STYLE|COLOR|SIZE"), "" without luxwm
std::string frame_theme_dir()
{
    WCHAR buf[1024];
    if (FlxpDwmGetFrameThemePath(buf, 1024) != S_OK) return "";
    std::string s = w32::utf8_of(buf);
    size_t bar = s.find('|');
    if (bar != std::string::npos) s.resize(bar);
    return s;
}

std::string prefixed_setting(const std::string& key)
{
    std::string v = shell::theme_setting(g_prefix + key);
    if (v.empty() && !g_prefix.empty()) v = shell::theme_setting(key);
    return v;
}

bool vision_children_menu() { return prefixed_setting("header_crumb_menu") == "children"; }

// Which header draws: flxp.ini's header_engine = lhnav | flxp when set (a developer's choice), else the session
// theme's Explorer.navbar (docs/THEME-FORMAT.md): vision = luxnav with path.navbar (share/navbar/vision.ini),
// 4051 | 4074 = flxp's own header. Without a session theme: luxnav when luxwm's frame set has a nav.ini.
bool vision_wanted(std::string* ini, std::string* why)
{
    std::string e = shell::setting(g_prefix + "header_engine");
    if (e.empty() && !g_prefix.empty()) e = shell::setting("header_engine");
    const shell::SessionTheme& st = shell::session_theme();
    std::string nb = st.value("Explorer.navbar"), f = nb == "vision" ? st.value("path.navbar") : "";
    struct stat sb;
    if (f.empty() || stat(f.c_str(), &sb) != 0) {
        std::string dir = frame_theme_dir();
        f = dir.empty() ? "" : dir + "/nav.ini";
    }
    bool have = !f.empty() && stat(f.c_str(), &sb) == 0;
    *ini = have ? f : "";
    if (e == "flxp") { *why = "flxp (" + g_prefix + "header_engine=flxp)"; return false; }
    if (e == "lhnav") {
        *why = "lhnav (" + g_prefix + "header_engine=lhnav, " + (have ? f : std::string("no nav.ini: lhnav's defaults")) + ")";
        return true;
    }
    if (nb == "vision") {
        *why = "lhnav (Explorer.navbar=vision, " + (have ? f : std::string("no navbar file: lhnav's defaults")) + ")";
        return true;
    }
    if (!nb.empty()) { *why = "flxp (Explorer.navbar=" + nb + ")"; return false; }
    if (have) { *why = "lhnav (no session theme; " + f + ")"; return true; }
    *why = "flxp (no session theme, no nav.ini of luxwm's frame set)";
    return false;
}

// <prefix>header_preset, else `small` for the Small header size (the dialog's
// default) when nav.ini has one, else nav.ini's own `preset`
std::string vision_preset(const std::string& ini)
{
    std::string p = prefixed_setting("header_preset");
    if (p == "auto") p.clear();
    if (!p.empty() || g_hsize != SIZE_SMALL) return p;
    char names[32][48];
    int n = lhnav_presets(ini.c_str(), names, 32);
    for (int i = 0; i < n; i++) if (!strcmp(names[i], "small")) return "small";
    return "";
}

// the session's visual style for lhnav (the file the shim loaded), nav.ini's preset
void vision_load(const std::string& ini)
{
    // the theme's visual style, as navdemo and the navigation bar (navbar.cpp) open it
    std::string path = shell::theme_setting("navbar_msstyles");
    if (path.empty()) path = shell::session_theme().msstyles;
    if (path != g_v.style_path || !g_v.nav) {
        ut_theme* old = g_v.style;
        g_v.style = path.empty() ? nullptr : ut_open(path.c_str(), nullptr, nullptr);
        g_v.style_path = path;
        if (g_v.nav) lhnav_set_style(g_v.nav, g_v.style);
        if (old) ut_close(old);       // after lhnav let go of it
    }
    // nav.ini is re-read on every luxwm theme (re)load: its _LHWM_THEME rewrite comes as a refit
    lhnav_metrics_default(&g_v.base);
    if (!ini.empty()) {
        std::string preset = vision_preset(ini);
        lhnav_metrics_load_preset(&g_v.base, ini.c_str(), preset.empty() ? nullptr : preset.c_str());
    }
    g_v.nav_ini = ini;
    if (!g_v.nav) g_v.nav = lhnav_new(g_v.style, &g_v.base);
}

// the items of a `tools` list
std::vector<std::string> tool_items(const char* spec)
{
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false, any = false;
    for (const char* c = spec; ; c++) {
        if (*c == '"') { quoted = !quoted; any = true; continue; }
        if (!*c || (!quoted && (*c == ' ' || *c == '\t' || *c == ','))) {
            if (any || !cur.empty()) out.push_back(cur);
            cur.clear(); any = false;
            if (!*c) break;
            continue;
        }
        cur += *c;
    }
    return out;
}

// nav.ini's toolbar for this header: the host's glyph buttons (the dialog's New
// Folder and Views) first, the elements switched off in View > Toolbars left out
std::string vision_tools(const char* spec)
{
    std::string out;
    auto add = [&](const std::string& it) {
        if (!out.empty()) out += ' ';
        out += it.find(' ') != std::string::npos ? '"' + it + '"' : it;
    };
    bool nf = false, vw = false;
    for (const ToolBtn& b : g_tools) { if (b.t.dropdown) vw = true; else nf = true; }
    if (nf) add("newfolder");
    if (vw) add("views");
    for (const std::string& it : tool_items(spec)) {
        if ((it == "newfolder" && nf) || (it == "views" && vw)) continue;
        if ((it == "search" || it == "search_field") && !g_show[SHOW_SEARCH]) continue;
        if (it == "favorites" && !g_show[SHOW_FAVORITES]) continue;
        add(it);
    }
    return out;
}

// the host's command for toolbar item i: the dialog's own buttons, else the explorer's commands
int host_tool(int kind, bool* enabled)
{
    *enabled = true;
    for (const ToolBtn& b : g_tools)
        if ((kind == LHNAV_TOOL_VIEWS) == b.t.dropdown) { *enabled = b.enabled; return b.t.id; }
    return kind == LHNAV_TOOL_VIEWS ? ID_VIEWS : kind == LHNAV_TOOL_NEWFOLDER ? ID_NEW_FOLDER : 0;
}

// luxwm's numbers into lhnav (navdemo apply_metrics): where our row 0 lies in
// the frame (border_t, _LHWM_FRAME_METRICS 12/13), where the deco ends (18
// deco_h, else the caption), the caption buttons (_LHWM_CAPTION_RECT), the
// plain text colour; then the state flxp has (title, path, history, focus)
void vision_metrics()
{
    if (!g_v.nav) return;
    lhnav_metrics lm = g_v.base;
    long m[19] = {};
    int n = FlxpGetRootCardinals("_LHWM_FRAME_METRICS", m, 19);
    bool max = zoomed();
    if (!lm.icon_dir[0]) snprintf(lm.icon_dir, sizeof lm.icon_dir, "%s", shell::longhorn_resources().c_str());
    long cap = n > 0 ? m[0] : 27;
    lm.border_t = n > 13 ? (int)m[max ? 13 : 12] : n > 0 ? 0 : lm.deco_h;   // no luxwm: under an ordinary caption
    lm.border_l = max ? 0 : n > 1 ? (int)m[1] : 0;
    if (n > 18 && m[18] > 0) lm.deco_h = (int)m[18];
    else if (n > 0) lm.deco_h = (int)(max && n > 8 ? m[8] : cap);
    if (max) lm.resize_rows = 0;
    // without composition all text in the caption's colour (inactive: its inactive one)
    lm.plain_rgb = n > 6 ? (uint32_t)(max && n > 15 ? m[g_active ? 14 : 15] : m[g_active ? 4 : 6]) : 0x1e1e1e;
    // maximised: luxwm's maximised caption look (aero vision: white, no glow, on the dark plate)
    if (max && n > 16 && (uint32_t)m[16] == 0xFFFFFFFFu) { lm.title_rgb = lm.crumb_rgb = (uint32_t)m[g_active ? 14 : 15]; lm.glow_strength = 0; }
    RECT r = {};
    if (g_wnd && FlxpDwmGetCaptionRect(GetParent(g_wnd), &r) == S_OK) {
        lm.caption_x = r.left; lm.caption_y = r.top; lm.caption_w = r.right - r.left; lm.caption_h = r.bottom - r.top;
    } else {
        lm.caption_x = lm.caption_y = lm.caption_w = lm.caption_h = 0;
    }
    snprintf(lm.tools, sizeof lm.tools, "%s", vision_tools(g_v.base.tools).c_str());
    lhnav_set_metrics(g_v.nav, &lm);
    g_v.tool_ids.clear();
    for (int i = 0; i < lhnav_tool_count(g_v.nav); i++) {
        bool on;
        int k = lhnav_tool(g_v.nav, i, nullptr, nullptr);
        g_v.tool_ids.push_back(k == LHNAV_TOOL_VIEWS || k == LHNAV_TOOL_NEWFOLDER ? host_tool(k, &on) : 0);
    }
    for (const ToolBtn& b : g_tools) {
        const uint32_t* px = navbar::pixels(b.img);
        if (px) lhnav_set_icon(g_v.nav, b.t.dropdown ? "views" : "newfolder", px, b.img.w, b.img.h);
    }
    lhnav_set_frame_state(g_v.nav, g_active, max);
    lhnav_set_title(g_v.nav, g_show[SHOW_TITLE] ? g_title.c_str() : nullptr);
    std::vector<const char*> segs;
    for (const Crumb& c : g_crumbs) segs.push_back(c.name.c_str());
    lhnav_set_path(g_v.nav, segs.data(), (int)segs.size());
    lhnav_set_history(g_v.nav, g_can[0], g_can[1]);
}

int vision_extend() { return g_v.nav ? lhnav_extend_top(g_v.nav) : 0; }

HFONT vision_edit_font()
{
    if (!g_v.edit_font)
        g_v.edit_font = have_face(L"Segoe UI") ? make_font(L"Segoe UI", 12, false) : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    return g_v.edit_font;
}

// an EDIT over lhnav's field `part`: from the field's text origin to its end
// (the search box keeps its magnifier), vertically centred
void vision_place(HWND e, int part, int right_pad)
{
    int x, y, w, h;
    if (!e || !lhnav_rect(g_v.nav, part, &x, &y, &w, &h)) { if (e) ShowWindow(e, SW_HIDE); return; }
    HDC dc = GetDC(e);
    HGDIOBJ old = SelectObject(dc, vision_edit_font());
    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(e, dc);
    int fx = lhnav_field_x(g_v.nav, part, "", 0), eh = tm.tmHeight > 0 ? tm.tmHeight : 15;
    SetWindowPos(e, HWND_TOP, fx, y + (h - eh) / 2, (std::max)(10, x + w - right_pad - fx), eh, SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

void vision_layout()
{
    if (!g_wnd || !g_v.nav) return;
    RECT rc; GetClientRect(g_wnd, &rc);
    lhnav_layout(g_v.nav, rc.right);
    if (getenv("FLXP_VISION_DEBUG")) {      // the final rects, as navdemo's NAVDEMO_DEBUG
        fprintf(stderr, "vision: %dx%d height %d extend %d hidden %d\n", rc.right, rc.bottom, lhnav_height(g_v.nav), lhnav_extend_top(g_v.nav), lhnav_hidden(g_v.nav));
        static const int parts[] = { LHNAV_TITLE, LHNAV_BACK, LHNAV_FWD, LHNAV_BACK_MENU, LHNAV_FWD_MENU, LHNAV_ADDRESS, LHNAV_OVERFLOW, LHNAV_SEARCH };
        for (int p : parts) { int x, y, w, h; if (lhnav_rect(g_v.nav, p, &x, &y, &w, &h)) fprintf(stderr, "  part %d  %d,%d %dx%d\n", p, x, y, w, h); }
        for (int i = 0; i < 64; i++) {
            int x, y, w, h;
            if (lhnav_rect(g_v.nav, LHNAV_CRUMB + i, &x, &y, &w, &h)) fprintf(stderr, "  crumb%d  %d,%d %dx%d\n", i, x, y, w, h);
            if (lhnav_rect(g_v.nav, LHNAV_SEP + i, &x, &y, &w, &h)) fprintf(stderr, "  sep%d  %d,%d %dx%d\n", i, x, y, w, h);
        }
        for (int i = 0; i < lhnav_tool_count(g_v.nav); i++) { int x, y, w, h; if (lhnav_rect(g_v.nav, LHNAV_TOOL + i, &x, &y, &w, &h)) fprintf(stderr, "  tool%d  %d,%d %dx%d kind %d\n", i, x, y, w, h, lhnav_tool(g_v.nav, i, nullptr, nullptr)); }
    }
    if (g_editing) vision_place(g_addr, LHNAV_ADDRESS, 3);
    if (g_v.search_on) vision_place(g_edit, LHNAV_SEARCH, 22);
    else if (g_edit) ShowWindow(g_edit, SW_HIDE);
    g_v.tip_part = -1;               // the tool rectangles again on the next move
    InvalidateRect(g_wnd, nullptr, FALSE);
}

// the rectangle a part's look changes in (the breadcrumb's parts: the whole
// box, whose field shows while it is hovered), with room for the glow
bool vision_part_rect(int part, RECT* r)
{
    int x, y, w, h;
    if (part == LHNAV_NONE || part == LHNAV_CAPTION || part == LHNAV_TITLE || part == LHNAV_RESIZE_TOP || part == LHNAV_BUTTONS) return false;
    if (part == LHNAV_OVERFLOW || LHNAV_IS_CRUMB(part) || LHNAV_IS_SEP(part)) part = LHNAV_ADDRESS;
    if (part == LHNAV_BACK || part == LHNAV_FWD || part == LHNAV_BACK_MENU || part == LHNAV_FWD_MENU) {
        int x2, y2, w2, h2;
        if (!lhnav_rect(g_v.nav, LHNAV_BACK, &x, &y, &w, &h)) return false;
        if (lhnav_rect(g_v.nav, LHNAV_FWD, &x2, &y2, &w2, &h2)) { int r2 = (std::max)(x + w, x2 + w2), b2 = (std::max)(y + h, y2 + h2); w = r2 - x; h = b2 - y; }
    } else if (!lhnav_rect(g_v.nav, part, &x, &y, &w, &h)) {
        return false;
    }
    *r = RECT{ x - 6, y - 6, x + w + 6, y + h + 6 };
    return true;
}

void vision_invalidate(int part)
{
    RECT r;
    if (g_wnd && vision_part_rect(part, &r)) InvalidateRect(g_wnd, &r, FALSE);
}

// the tooltip of the part under the pointer (lhnav_tip), a comctl32 tooltip
// whose tool is that part's rectangle (a new tool per part, so moving to the
// next part shows its tip after the delay again)
void vision_tip(int part)
{
    if (part == g_v.tip_part) return;
    if (!g_v.tip) {
        g_v.tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                  CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, g_wnd, nullptr, nullptr, nullptr);
        if (!g_v.tip) return;
    }
    TTTOOLINFOW ti = {};
    ti.cbSize = sizeof ti;
    ti.hwnd = g_wnd;
    if (g_v.tip_part > 0) { ti.uId = (UINT_PTR)g_v.tip_part; SendMessageW(g_v.tip, TTM_DELTOOLW, 0, (LPARAM)&ti); }
    g_v.tip_part = part;
    const char* t = part == LHNAV_ADDRESS || part == LHNAV_SEARCH || part <= LHNAV_TITLE ? nullptr : lhnav_tip(g_v.nav, part);
    std::string own;
    if (LHNAV_IS_SEP(part) && !vision_children_menu()) {      // flxp's list there: the segment's siblings
        size_t i = (size_t)(part - LHNAV_SEP);
        if (i < g_crumbs.size()) { own = i ? "Folders next to " + g_crumbs[i].name : ""; t = own.empty() ? nullptr : own.c_str(); }
    }
    int x, y, w, h;
    if (!t || !*t || !lhnav_rect(g_v.nav, part, &x, &y, &w, &h)) return;
    g_v.tip_text = W(t);
    ti.uId = (UINT_PTR)part;
    ti.rect = RECT{ x, y, x + w, y + h };
    ti.lpszText = (LPWSTR)g_v.tip_text.c_str();
    SendMessageW(g_v.tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

void vision_relay(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (!g_v.tip) return;
    MSG m = {};
    m.hwnd = h; m.message = msg; m.wParam = wp; m.lParam = lp;
    SendMessageW(g_v.tip, TTM_RELAYEVENT, 0, (LPARAM)&m);
}

// lhnav's pointer state; the parts whose look changed are repainted
void vision_pointer(int x, int y, int buttons)
{
    int oh = lhnav_hover(g_v.nav), op = lhnav_pressed(g_v.nav);
    if (!lhnav_pointer(g_v.nav, x, y, buttons)) return;
    vision_invalidate(oh); vision_invalidate(op);
    vision_invalidate(lhnav_hover(g_v.nav)); vision_invalidate(lhnav_pressed(g_v.nav));
}

// back to where the pointer is now (after a menu: the press went to it)
void vision_resync()
{
    POINT c; GetCursorPos(&c); ScreenToClient(g_wnd, &c);
    RECT rc; GetClientRect(g_wnd, &rc);
    vision_pointer(-1, -1, 0);
    if (PtInRect(&rc, c)) vision_pointer(c.x, c.y, 0);
}

// a part's list is up (flxp's menus): drawn pressed / open meanwhile; *x = its left edge
void vision_menu_begin(int part, int* x)
{
    int px = 0, py, pw, ph;
    if (LHNAV_IS_SEP(part) && !vision_children_menu()) {
        // the siblings' list starts under the segment it is about (4074)
        if (!lhnav_rect(g_v.nav, LHNAV_CRUMB + (part - LHNAV_SEP), &px, &py, &pw, &ph)) lhnav_rect(g_v.nav, part, &px, &py, &pw, &ph);
    } else if (!lhnav_rect(g_v.nav, part, &px, &py, &pw, &ph)) {
        lhnav_rect(g_v.nav, part == LHNAV_BACK_MENU ? LHNAV_BACK : part == LHNAV_FWD_MENU ? LHNAV_FWD : LHNAV_ADDRESS, &px, &py, &pw, &ph);
    }
    *x = px;
    vision_tip(LHNAV_NONE);
    if (g_v.tip) SendMessageW(g_v.tip, TTM_POP, 0, 0);
    lhnav_set_open(g_v.nav, part);
    vision_invalidate(part);
    UpdateWindow(g_wnd);
}

void vision_menu_end()
{
    int was = lhnav_open(g_v.nav);
    lhnav_set_open(g_v.nav, LHNAV_NONE);
    vision_invalidate(was);
    vision_resync();
}

void vision_history(bool back)
{
    if (!g_can[back ? 0 : 1] || !g_cb.history) return;
    POINT s = { 0, g_height };          // flush under the header's lower edge, as the other headers' menus
    vision_menu_begin(back ? LHNAV_BACK_MENU : LHNAV_FWD_MENU, (int*)&s.x);
    ClientToScreen(g_wnd, &s);
    g_cb.history(back, s.x, s.y);
    vision_menu_end();
}

// "«": the collapsed ancestors, the nearest first
void vision_overflow()
{
    int hidden = lhnav_hidden(g_v.nav);
    if (hidden <= 0) return;
    HMENU m = CreatePopupMenu();
    for (int j = hidden - 1; j >= 0 && (size_t)j < g_crumbs.size(); j--)
        AppendMenuW(m, MF_STRING, (UINT)j + 1, (LPCWSTR)W(g_crumbs[(size_t)j].name).c_str());
    POINT s = { 0, g_height };
    vision_menu_begin(LHNAV_OVERFLOW, (int*)&s.x);
    ClientToScreen(g_wnd, &s);
    int cmd = TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, s.x, s.y, 0, GetParent(g_wnd), nullptr);
    DestroyMenu(m);
    vision_menu_end();
    if (cmd > 0 && (size_t)cmd <= g_crumbs.size() && g_cb.navigate) { std::string p = g_crumbs[(size_t)cmd - 1].path; g_cb.navigate(p); }
}

// a drop-down tool (Favorites, Views): the host's menu under the header
void vision_tool_menu(int part, int kind)
{
    POINT s = { 0, g_height };
    vision_menu_begin(part, (int*)&s.x);
    ClientToScreen(g_wnd, &s);
    bool on = true;
    if (kind == LHNAV_TOOL_FAVORITES) { if (g_cb.favorites) g_cb.favorites(s.x, s.y); }
    else {
        int id = host_tool(kind, &on);
        if (on && g_cb.tool) g_cb.tool(id, s.x, s.y);
    }
    vision_menu_end();
}

void vision_tool_click(int i)
{
    const char *arg = nullptr, *label = nullptr;
    int k = lhnav_tool(g_v.nav, i, &arg, &label);
    if (k == LHNAV_TOOL_SEARCH) { open_search(); return; }
    if (k == LHNAV_TOOL_BOOKMARK && arg && g_cb.navigate) {
        std::string p = arg;
        if (!p.empty() && p[0] == '~') p = shell::home_dir() + p.substr(1);
        g_cb.navigate(p);
        return;
    }
    if (k == LHNAV_TOOL_NEWFOLDER) {
        bool on = true;
        int id = host_tool(k, &on);
        if (!on) return;
        int x = 0, y = 0, w = 0, h = 0;
        lhnav_rect(g_v.nav, LHNAV_TOOL + i, &x, &y, &w, &h);
        POINT s = { x, y + h };
        ClientToScreen(g_wnd, &s);
        if (g_cb.tool) g_cb.tool(id, s.x, s.y);
        else if (g_cb.command) g_cb.command(id);
    }
}

void vision_begin_edit()
{
    if (!g_addr) {
        g_addr = CreateWindowExW(0, L"Edit", nullptr, WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, g_wnd, nullptr, nullptr, nullptr);
        g_addr_old = (WNDPROC)SetWindowLongPtrW(g_addr, GWLP_WNDPROC, (LONG_PTR)addr_proc);
    }
    SendMessageW(g_addr, WM_SETFONT, (WPARAM)vision_edit_font(), FALSE);
    SendMessageW(g_addr, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    g_editing = true;
    lhnav_set_field(g_v.nav, LHNAV_ADDRESS, "", -1, 1);     // the opaque field, no text: the EDIT shows it
    vision_place(g_addr, LHNAV_ADDRESS, 3);
    SetWindowTextW(g_addr, (LPCWSTR)W(g_crumbs.back().path).c_str());
    SendMessageW(g_addr, EM_SETSEL, 0, -1);
    SetFocus(g_addr);
    vision_invalidate(LHNAV_ADDRESS);
}

// the search EDIT over lhnav's search box while it has the focus; else lhnav
// draws the text (or "Search") itself
void vision_open_search()
{
    if (!g_edit || !g_show[SHOW_SEARCH]) return;
    if (!lhnav_rect(g_v.nav, LHNAV_SEARCH, nullptr, nullptr, nullptr, nullptr)) {
        lhnav_set_search_open(g_v.nav, 1);         // a `search` item: its field comes out
        RECT rc; GetClientRect(g_wnd, &rc);
        lhnav_layout(g_v.nav, rc.right);
        InvalidateRect(g_wnd, nullptr, FALSE);
        if (!lhnav_rect(g_v.nav, LHNAV_SEARCH, nullptr, nullptr, nullptr, nullptr)) return;
    }
    SendMessageW(g_edit, WM_SETFONT, (WPARAM)vision_edit_font(), FALSE);
    SendMessageW(g_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    lhnav_set_field(g_v.nav, LHNAV_SEARCH, "", -1, 1);
    g_v.search_on = true;
    vision_place(g_edit, LHNAV_SEARCH, 22);
    vision_invalidate(LHNAV_SEARCH);
    SetFocus(g_edit);
    SendMessageW(g_edit, EM_SETSEL, 0, -1);
}

void vision_close_search()
{
    if (!g_v.search_on) return;
    g_v.search_on = false;
    int n = GetWindowTextLengthW(g_edit);
    std::vector<WCHAR> buf((size_t)n + 1);
    GetWindowTextW(g_edit, buf.data(), n + 1);
    std::string text = w32::utf8_of(buf.data());
    ShowWindow(g_edit, SW_HIDE);
    lhnav_set_field(g_v.nav, LHNAV_SEARCH, text.c_str(), -1, 0);
    if (text.empty() && lhnav_search_open(g_v.nav)) { lhnav_set_search_open(g_v.nav, 0); vision_layout(); }
    vision_invalidate(LHNAV_SEARCH);
}

void vision_on()
{
    g_v.on = true;
    g_hot = g_pressed = -1; g_open = -1; g_bar_hot = false;
    g_nav_hot = g_nav_pressed = NONE;
    g_tool_hot = g_tool_pressed = -1;
    if (g_editing) { g_editing = false; if (g_addr) ShowWindow(g_addr, SW_HIDE); }
    if (g_edit) {
        // lhnav draws the box: the EDIT without its edge, shown only while searching
        LONG_PTR ex = GetWindowLongPtrW(g_edit, GWL_EXSTYLE);
        SetWindowLongPtrW(g_edit, GWL_EXSTYLE, ex & ~(LONG_PTR)WS_EX_CLIENTEDGE);
        SetWindowPos(g_edit, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_HIDEWINDOW);
    }
    g_v.search_on = false;
    g_v.tip_part = -1;
    if (g_edit && g_v.nav) {      // a filter typed before the switch stays visible
        int n = GetWindowTextLengthW(g_edit);
        std::vector<WCHAR> buf((size_t)n + 1);
        GetWindowTextW(g_edit, buf.data(), n + 1);
        std::string text = w32::utf8_of(buf.data());
        lhnav_set_field(g_v.nav, LHNAV_SEARCH, text.empty() ? nullptr : text.c_str(), -1, 0);
    }
}

void vision_off()
{
    g_v.on = false;
    if (g_v.tip) { DestroyWindow(g_v.tip); g_v.tip = nullptr; }
    g_v.tip_part = 0;
    if (g_editing) { g_editing = false; if (g_addr) ShowWindow(g_addr, SW_HIDE); }
    g_v.search_on = false;
    if (g_edit) {
        LONG_PTR ex = GetWindowLongPtrW(g_edit, GWL_EXSTYLE);
        SetWindowLongPtrW(g_edit, GWL_EXSTYLE, ex | WS_EX_CLIENTEDGE);
        SetWindowPos(g_edit, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        SendMessageW(g_edit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), FALSE);
    }
    if (g_addr) SendMessageW(g_addr, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), FALSE);
    if (g_v.nav) { lhnav_free(g_v.nav); g_v.nav = nullptr; }
    if (g_v.style) { ut_close(g_v.style); g_v.style = nullptr; }
    g_v.style_path.clear();
    if (g_v.dib) { DeleteObject(g_v.dib); g_v.dib = nullptr; g_v.bits = nullptr; g_v.dw = g_v.dh = 0; }
    g_mode = GLASS;        // the classic header's fonts and mode again (composition_changed)
}

// whether the header goes through lhnav now; switches it on or off. Called
// on every refit (height_for_caption): theme, composition, metrics, luxwm's reload
bool vision_update()
{
    std::string ini, why;
    bool want = vision_wanted(&ini, &why);
    g_v.why = why;
    if (!want) { if (g_v.on) vision_off(); return false; }
    vision_load(ini);
    if (!g_v.on) vision_on();
    vision_metrics();
    return true;
}

void vision_paint(HDC dc)
{
    RECT rc; GetClientRect(g_wnd, &rc);
    int R = rc.right, H = rc.bottom;
    if (R <= 0 || H <= 0 || !g_v.nav) return;
    if (!g_v.dib || g_v.dw != R || g_v.dh != H) {
        if (g_v.dib) DeleteObject(g_v.dib);
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = R; bi.bmiHeader.biHeight = -H; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        g_v.dib = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        g_v.bits = (uint32_t*)bits;
        g_v.dw = R; g_v.dh = H;
        if (!g_v.dib || !g_v.bits) { g_v.dib = nullptr; g_v.bits = nullptr; return; }
    }
    bool comp = glass();
    // lhnav over transparent, then OVER the background (the same as painting onto it)
    memset(g_v.bits, 0, (size_t)R * H * 4);
    lhnav_paint(g_v.nav, g_v.bits, R, H, R * 4, comp);
    RECT cr = {};
    if (FlxpDwmGetCaptionRect(GetParent(g_wnd), &cr) == S_OK) {     // luxwm's caption buttons stay free
        int x0 = (std::max)(0, (int)cr.left), x1 = (std::min)(R, (int)cr.right), y1 = (std::min)(H, (int)cr.bottom);
        for (int y = (std::max)(0, (int)cr.top); y < y1; y++)
            for (int x = x0; x < x1; x++) g_v.bits[(size_t)y * R + x] = 0;
    }
    if (comp) {
        FlxpClearRect(dc, &rc);                  // luxwm's plate shows through
    } else if (!blit_pixmap(dc, R, H)) {         // luxwm's band (with its caption buttons under CLIENT_CAPTION)
        int v = variant();
        if (sk().valid && sk().bg[v].bmp) {
            navbar::draw_nine(dc, sk().bg[v], rc, sk().bg_margins[v]);
        } else {
            for (int y = 0; y < H; y++) {        // navdemo's gradient
                double t = H > 1 ? (double)y / (H - 1) : 0;
                fill(dc, 0, y, R, y + 1, RGB((int)(237 - 38 * t), (int)(242 - 28 * t), (int)(250 - 15 * t)));
            }
        }
    }
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, g_v.dib);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(dc, 0, 0, R, H, mem, 0, 0, R, H, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
}

bool vision_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp, LRESULT* r)
{
    lhnav* n = g_v.nav;
    if (!n) return false;
    int x = (SHORT)LOWORD(lp), y = (SHORT)HIWORD(lp);
    *r = 0;
    switch (msg) {
    case WM_NCHITTEST: {
        POINT p = { x, y };
        ScreenToClient(h, &p);
        if (GetCapture() == h) { *r = HTCLIENT; return true; }
        int part = lhnav_hit(n, p.x, p.y);
        // the caption, the title, the top resize rows and luxwm's buttons are the
        // frame's (browser::header_hittest: HTCAPTION / HTTOP); the rest is ours
        *r = part == LHNAV_NONE || part == LHNAV_CAPTION || part == LHNAV_TITLE || part == LHNAV_RESIZE_TOP || part == LHNAV_BUTTONS
             ? HTTRANSPARENT : HTCLIENT;
        return true;
    }
    case WM_MOUSEMOVE:
        vision_relay(h, msg, wp, lp);
        vision_pointer(x, y, (wp & MK_LBUTTON) ? 1 : 0);
        if (!(wp & MK_LBUTTON)) vision_tip(lhnav_hit(n, x, y));
        if (!g_v.tracking) {
            TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 };
            g_v.tracking = TrackMouseEvent(&t);
        }
        return true;
    case WM_MOUSELEAVE:
        g_v.tracking = false;
        if (GetCapture() != h) { vision_pointer(-1, -1, 0); vision_tip(LHNAV_NONE); }
        return true;
    case WM_LBUTTONDOWN: {
        vision_relay(h, msg, wp, lp);
        vision_tip(LHNAV_NONE);
        int part = lhnav_hit(n, x, y);
        vision_pointer(x, y, 1);
        if (part == LHNAV_BACK_MENU || part == LHNAV_FWD_MENU) { vision_history(part == LHNAV_BACK_MENU); return true; }   // on the press
        if (part == LHNAV_OVERFLOW) { vision_overflow(); return true; }
        if (LHNAV_IS_SEP(part)) {
            size_t i = (size_t)(part - LHNAV_SEP);
            if (i < g_crumbs.size()) open_menu((int)i);
            else vision_resync();
            return true;
        }
        if (LHNAV_IS_TOOL(part)) {
            int k = lhnav_tool(n, part - LHNAV_TOOL, nullptr, nullptr);
            if (k == LHNAV_TOOL_FAVORITES || k == LHNAV_TOOL_VIEWS) { vision_tool_menu(part, k); return true; }
        }
        if (part == LHNAV_ADDRESS) { vision_pointer(x, y, 0); begin_edit(); return true; }
        if (part == LHNAV_SEARCH) { vision_pointer(x, y, 0); open_search(); return true; }
        if (part != LHNAV_NONE) SetCapture(h);       // back, forward, segments, tools: on the release
        return true;
    }
    case WM_LBUTTONUP: {
        int was = lhnav_pressed(n), part = lhnav_hit(n, x, y);
        vision_pointer(x, y, 0);
        if (GetCapture() == h) ReleaseCapture();
        if (was == LHNAV_NONE || was != part) return true;
        if (part == LHNAV_BACK || part == LHNAV_FWD) { if (g_can[part == LHNAV_BACK ? 0 : 1] && g_cb.command) g_cb.command(part == LHNAV_BACK ? ID_BACK : ID_FORWARD); }
        else if (LHNAV_IS_CRUMB(part)) {
            size_t i = (size_t)(part - LHNAV_CRUMB);
            if (i < g_crumbs.size() && g_cb.navigate) { std::string p = g_crumbs[i].path; g_cb.navigate(p); }
        } else if (LHNAV_IS_TOOL(part)) vision_tool_click(part - LHNAV_TOOL);
        return true;
    }
    case WM_RBUTTONDOWN: {           // the history list on a right click, in either variant
        int part = lhnav_hit(n, x, y);
        if (part == LHNAV_BACK || part == LHNAV_FWD || part == LHNAV_BACK_MENU || part == LHNAV_FWD_MENU)
            vision_history(part == LHNAV_BACK || part == LHNAV_BACK_MENU);
        return true;
    }
    case WM_CAPTURECHANGED:
        if (lhnav_pressed(n) != LHNAV_NONE) vision_resync();
        return true;
    case WM_FLXP_DRAGOVER:
    case WM_FLXP_DROP: {
        // a breadcrumb segment is a drop target: the folder it names (hot while over it)
        const FLXPDROP* d = (const FLXPDROP*)lp;
        POINT p = { d->x, d->y };
        ScreenToClient(h, &p);
        int part = g_editing ? LHNAV_NONE : lhnav_hit(n, p.x, p.y);
        if (!LHNAV_IS_CRUMB(part) || (size_t)(part - LHNAV_CRUMB) >= g_crumbs.size()) { vision_pointer(-1, -1, 0); return true; }
        vision_pointer(p.x, p.y, 0);
        const std::string path = g_crumbs[(size_t)(part - LHNAV_CRUMB)].path;
        *r = msg == WM_FLXP_DROP ? verbs::drop(d, path) : verbs::drop_effect(d, path);
        return true;
    }
    case WM_FLXP_DRAGLEAVE:
        vision_pointer(-1, -1, 0);
        return true;
    case WM_COMMAND:
        if ((HWND)lp == g_edit && HIWORD(wp) == EN_KILLFOCUS) { vision_close_search(); return true; }
        return false;                // EN_CHANGE: the filter, as always
    }
    return false;
}

LRESULT CALLBACK edit_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE)) {
        if (wp == VK_ESCAPE) SetWindowTextW(h, L"");   // EN_CHANGE: the filter goes
        if (g_cb.done) g_cb.done(wp == VK_RETURN);
        return 0;
    }
    if (msg == WM_CHAR && (wp == '\r' || wp == 0x1b)) return 0;   // no beep
    return CallWindowProcW(g_edit_old, h, msg, wp, lp);
}

LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    LRESULT vr;
    if (vision() && vision_proc(h, msg, wp, lp, &vr)) return vr;
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        paint(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SIZE: layout(); return 0;
    case WM_NCHITTEST: {
        // the breadcrumbs are buttons; everything else is the frame's caption
        POINT p = { (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
        ScreenToClient(h, &p);
        if (hit(p).field || GetCapture() == h) return HTCLIENT;   // the bar is a control (4074)
        if (part_at(p) != NONE) return HTCLIENT;      // the buttons (also disabled: no caption drag there)
        if (tool_at(p) >= 0) return HTCLIENT;
        return HTTRANSPARENT;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
            SetCursor(LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE: {
        POINT p = { (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
        Hit ht = hit(p);
        if (ht.field != g_bar_hot || ht.seg != g_hot || ht.arrow != g_hot_arrow) {
            g_bar_hot = ht.field; g_hot = ht.seg; g_hot_arrow = ht.arrow;
            invalidate_field();
        }
        set_nav_hot(part_at(p));
        int th = tool_at(p);
        if (th != g_tool_hot) { g_tool_hot = th; invalidate_tools(); }
        if (!g_tracking) {
            TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 };
            g_tracking = TrackMouseEvent(&t);
        }
        return 0;
    }
    case WM_FLXP_DRAGOVER:
    case WM_FLXP_DROP: {
        // a breadcrumb segment is a drop target: the folder it names (hot while over it)
        const FLXPDROP* d = (const FLXPDROP*)lp;
        POINT p = { d->x, d->y };
        ScreenToClient(h, &p);
        Hit ht = g_editing ? Hit() : hit(p);
        int seg = ht.seg >= 0 && !ht.arrow ? ht.seg : -1;
        if (seg != g_hot || g_hot_arrow) { g_hot = seg; g_hot_arrow = false; g_bar_hot = seg >= 0; invalidate_field(); }
        if (seg < 0) return 0;
        const std::string path = g_crumbs[(size_t)seg].path;
        return msg == WM_FLXP_DROP ? verbs::drop(d, path) : verbs::drop_effect(d, path);
    }
    case WM_FLXP_DRAGLEAVE:
        if (g_hot >= 0 || g_bar_hot) { g_hot = -1; g_bar_hot = false; invalidate_field(); }
        return 0;
    case WM_MOUSELEAVE:
        g_tracking = false;
        if (GetCapture() != h) {
            if (g_bar_hot || g_hot >= 0) { g_bar_hot = false; g_hot = -1; invalidate_field(); }
            set_nav_hot(NONE);
            if (g_tool_hot >= 0) { g_tool_hot = -1; invalidate_tools(); }
        }
        return 0;
    case WM_LBUTTONDOWN: {
        POINT p = { (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
        int ti = tool_at(p);
        if (ti >= 0) {
            ToolBtn& b = g_tools[(size_t)ti];
            if (!b.enabled) return 0;
            g_tool_pressed = ti;
            invalidate_tools();
            if (b.t.dropdown) {       // a menu at once, under the button
                UpdateWindow(h);
                POINT s = { b.r.left, b.r.bottom };
                ClientToScreen(h, &s);
                int id = b.t.id;
                if (g_cb.tool) g_cb.tool(id, s.x, s.y);
                g_tool_pressed = -1;
                POINT c; GetCursorPos(&c); ScreenToClient(h, &c);
                g_tool_hot = tool_at(c);
                invalidate_tools();
                return 0;
            }
            SetCapture(h);
            return 0;
        }
        int part = part_at(p);
        if (part != NONE) {
            if (!enabled(part)) return 0;
            if (part == BACK_DROP || part == FWD_DROP) {
                // the history drop-down opens on the press, as a toolbar's split button
                g_nav_pressed = g_nav_hot = part;
                invalidate_nav();
                UpdateWindow(h);
                // flush under the header (4051: the drop-down is the band's last rows)
                POINT s = { travel_x0() + (part == BACK_DROP ? drop_margin() : fwd_dx()), g_height };
                ClientToScreen(h, &s);
                if (g_cb.history) g_cb.history(part == BACK_DROP, s.x, s.y);
                g_nav_pressed = NONE;
                POINT c; GetCursorPos(&c); ScreenToClient(h, &c);
                g_nav_hot = NONE;
                set_nav_hot(part_at(c));
                invalidate_nav();
                return 0;
            }
            g_nav_pressed = g_nav_hot = part;
            SetCapture(h);
            invalidate_nav();
            return 0;
        }
        Hit ht = hit(p);
        if (ht.seg >= 0 && ht.arrow) { open_menu(ht.seg); return 0; }   // on the press, as a split button
        if (ht.drop) {
            RECT f = field_rect();
            POINT s = { f.left, g_height };
            ClientToScreen(h, &s);
            if (g_cb.history) g_cb.history(true, s.x, s.y);
            return 0;
        }
        if (ht.field && ht.seg < 0) { begin_edit(); return 0; }
        g_pressed = ht.seg;
        if (g_pressed >= 0) { SetCapture(h); invalidate_crumb(g_pressed); }
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT p = { (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
        if (g_tool_pressed >= 0) {
            int was = g_tool_pressed;
            g_tool_pressed = -1;
            if (GetCapture() == h) ReleaseCapture();
            invalidate_tools();
            if (tool_at(p) == was && g_cb.tool) {
                const ToolBtn& b = g_tools[(size_t)was];
                POINT s = { b.r.left, b.r.bottom };
                ClientToScreen(h, &s);
                g_cb.tool(b.t.id, s.x, s.y);
            }
            return 0;
        }
        if (g_nav_pressed != NONE) {
            int part = part_at(p), was = g_nav_pressed;
            g_nav_pressed = NONE;
            if (GetCapture() == h) ReleaseCapture();
            g_nav_hot = part;
            invalidate_nav();
            if (part != was) return 0;
            if (was == SEARCH) open_search();
            else if (was == FAV) {
                RECT rc; GetClientRect(h, &rc);
                Right rl = right_layout(rc.right);
                POINT s = { rl.fav_x, g_height };
                ClientToScreen(h, &s);
                if (g_cb.favorites) g_cb.favorites(s.x, s.y);
            } else if (g_cb.command) g_cb.command(was == BACK ? ID_BACK : ID_FORWARD);
            return 0;
        }
        Hit ht = hit(p);
        int i = ht.arrow ? -1 : ht.seg, was = g_pressed;
        g_pressed = -1;
        if (GetCapture() == h) ReleaseCapture();
        g_hot = ht.seg; g_hot_arrow = ht.arrow; g_bar_hot = ht.field;
        invalidate_field();
        if (i >= 0 && i == was && g_cb.navigate) {
            std::string path = g_crumbs[(size_t)i].path;   // navigate rebuilds the crumbs
            g_cb.navigate(path);
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (g_tool_pressed >= 0) { g_tool_pressed = -1; invalidate_tools(); }
        if (g_nav_pressed != NONE) { g_nav_pressed = NONE; invalidate_nav(); }
        if (g_pressed >= 0) { g_pressed = -1; invalidate_field(); }
        return 0;
    case WM_COMMAND:
        if ((HWND)lp == g_edit && HIWORD(wp) == EN_KILLFOCUS && nb_buttons() && g_search_open && !GetWindowTextLengthW(g_edit)) {
            g_search_open = false;      // back behind the magnifier
            layout();
            return 0;
        }
        if ((HWND)lp == g_edit && HIWORD(wp) == EN_CHANGE && !g_quiet && g_cb.filter) {
            int n = GetWindowTextLengthW(g_edit);
            std::vector<WCHAR> buf((size_t)n + 1);
            GetWindowTextW(g_edit, buf.data(), n + 1);
            g_cb.filter(w32::utf8_of(buf.data()));
        }
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

bool g_settings_read;


// <prefix>header_size and the elements; the dialog's (prefix filedlg_) fall
// back to the explorer's for the elements. The explorer's size without a
// header_size of its own: the session theme's Explorer.navbar_preset (4074:
// small | medium | large | xlarge; docs/THEME-FORMAT.md)
void read_settings()
{
    g_settings_read = true;
    std::string dflt = g_default_size;
    const shell::SessionTheme& st = shell::session_theme();
    std::string np = st.value("Explorer.navbar_preset");
    if (g_prefix.empty() && st.value("Explorer.navbar") != "vision" &&
        (np == "small" || np == "medium" || np == "large" || np == "xlarge")) dflt = np;
    std::string sz = shell::setting(g_prefix + "header_size", dflt);
    g_hsize = SIZE_MEDIUM;
    if (sz == "small" || sz == "tiny") g_hsize = SIZE_SMALL;
    else if (sz == "large") g_hsize = SIZE_LARGE;
    else if (sz == "xlarge" || sz == "extralarge" || sz == "extra-large" || sz == "huge") g_hsize = SIZE_XLARGE;
    for (int i = 0; i < ELEMENTS; i++) {
        int v = shell::setting_flag_auto(g_prefix + ELEMENT_KEYS[i]);
        if (v < 0 && !g_prefix.empty()) v = shell::setting_flag_auto(ELEMENT_KEYS[i]);
        g_show[i] = v != 0;
    }
    // the small buttons' style (the dialog's falls back to the explorer's; flxp.ini or the theme)
    std::string sb = shell::theme_setting(g_prefix + "header_small_buttons");
    if (sb.empty() && !g_prefix.empty()) sb = shell::theme_setting("header_small_buttons");
    g_small_setting = sb == "even" || sb == "same" || sb == "symmetric" ? "even" : sb == "uneven" || sb == "scaled" ? "uneven" : "auto";
}

} // namespace

namespace {
bool classic_client_caption()
{
    return glass() && g_hsize != SIZE_SMALL && g_show[SHOW_TITLE] && shell::setting_flag("client_caption", true);
}
int classic_height(int caption_h);
}

// vision: always (lhnav's title lies in luxwm's deco, composited or not)
bool client_caption()
{
    if (!g_settings_read) read_settings();
    return vision() || classic_client_caption();
}

// The same rows in every mode (top_off(), band_rows()): glass and luxwm's
// pixmap have the navigation bar's metrics, under luxwm's caption as the bar is.
// Small (lh4074-195235.png): 36 rows, the caption keeps the title (59 rows from
// a 24-row caption's top edge to the dark line); Medium: the band, 54 (Slate
// 4074: 53; 4051: 24 + 54 = 78 from the frame's top edge); Large / Extra-Large:
// 32 / 49 more. CLIENT_CAPTION (glass, the title in the header): luxwm gives the
// client its caption rows too (from the top border); they stay empty above the
// band, so the band is where it is without the flag - 4074-dwm.png's own
// placement (the title and Back button across the caption rows, 76 rows from
// the frame's top) gave way to the one layout of the navigation bar.
int height_for_caption(int caption_h)
{
    if (!g_settings_read) read_settings();
    if (vision_update()) return vision_extend();     // the header plus luxwm's line rows (lhnav_extend_top)
    return classic_height(caption_h);
}

namespace {
int classic_height(int caption_h)
{
    if (g_hsize == SIZE_SMALL) return SMALL_BAND;
    const navbar::Skin& k = navbar::skin();
    bool nb = !glass() && shell::theme_setting("header_background", "auto") != "pixmap" && k.valid;
    bool theme_buttons = nb || (k.buttons && g_buttons_setting != "framelets");
    int more = g_hsize == SIZE_LARGE ? 32 : g_hsize == SIZE_XLARGE ? 49 : 0;
    int band = theme_buttons ? k.height : NAV_H, off = 0;
    if (!nb && classic_client_caption()) {
        // the client starts at luxwm's top border (DWM frames: the side border width)
        FLXP_DWM_FRAME_METRICS m = {}; m.cbSize = sizeof m;
        FlxpDwmGetFrameMetrics(&m);
        int border = m.bValid && m.cxLeftBorder > 0 ? m.cxLeftBorder : 9;
        off = (caption_h > 0 ? caption_h : 27) - border;
        if (off < 0) off = 0;
    }
    return off + band + more;
}
}

int extend_top()
{
    if (vision()) return g_height;        // lhnav_extend_top: luxwm draws the line pair on its last two rows
    BOOL comp = FALSE;
    DwmIsCompositionEnabled(&comp);
    return g_height + (comp ? 1 : 0);     // glass: luxwm's lines under the header's dark row and the bar
}

bool resize_hit(int x, int y)
{
    return vision() && g_v.nav && lhnav_hit(g_v.nav, x, y) == LHNAV_RESIZE_TOP;
}

void caption_changed()
{
    if (!vision() || !g_wnd) return;
    vision_metrics();
    layout();
}

// ---- size and elements (View > Toolbars) ----------------------------------------------

void configure(const std::string& prefix, const std::string& default_size)
{
    g_prefix = prefix;
    g_default_size = default_size;
    read_settings();
}

Size size() { if (!g_settings_read) read_settings(); return (Size)g_hsize; }
bool shown(Element e) { if (!g_settings_read) read_settings(); return e >= 0 && e < ELEMENTS && g_show[e]; }
bool draws_title() { if (!g_settings_read) read_settings(); return vision() || (g_hsize != SIZE_SMALL && g_show[SHOW_TITLE]); }

void set_size(Size sz)
{
    g_hsize = sz;
    shell::write_setting(g_prefix + "header_size", SIZE_NAMES[sz]);
}

void set_shown(Element e, bool on)
{
    if (e < 0 || e >= ELEMENTS) return;
    g_show[e] = on;
    shell::write_setting(g_prefix + ELEMENT_KEYS[e], on ? "1" : "0");
    if (e == SHOW_SEARCH && !on) g_search_open = false;
    if (g_wnd) { layout(); InvalidateRect(g_wnd, nullptr, TRUE); }
}

void append_menu(HMENU toolbars, UINT first)
{
    static const wchar_t* const sizes[4] = { L"&Small", L"&Medium", L"&Large", L"E&xtra-Large" };
    static const wchar_t* const elements[ELEMENTS] = { L"&Back/Forward Buttons", L"&Title", L"A&ddress (Breadcrumb)", L"Sea&rch", L"&Favorites" };
    for (int i = 0; i < ELEMENTS; i++) AppendMenuW(toolbars, MF_STRING, first + 4 + (UINT)i, elements[i]);
    AppendMenuW(toolbars, MF_SEPARATOR, 0, nullptr);
    HMENU m = CreatePopupMenu();
    for (int i = 0; i < 4; i++) AppendMenuW(m, MF_STRING, first + (UINT)i, sizes[i]);
    AppendMenuW(toolbars, MF_POPUP, (UINT_PTR)m, L"Toolbar Si&ze");
    update_menu(toolbars, first);
}

void update_menu(HMENU toolbars, UINT first)
{
    if (!toolbars) return;
    if (!g_settings_read) read_settings();
    HMENU m = nullptr;   // Toolbar Size
    for (int i = GetMenuItemCount(toolbars) - 1; i >= 0 && !m; i--)
        if (HMENU sub = GetSubMenu(toolbars, i)) if (GetMenuState(sub, first, MF_BYCOMMAND) != (UINT)-1) m = sub;
    if (m) CheckMenuRadioItem(m, first, first + 3, first + (UINT)g_hsize, MF_BYCOMMAND);
    for (int i = 0; i < ELEMENTS; i++) CheckMenuItem(toolbars, first + 4 + (UINT)i, MF_BYCOMMAND | (g_show[i] ? MF_CHECKED : MF_UNCHECKED));
    // the title shows in the frame's caption at the small size
    EnableMenuItem(toolbars, first + 4 + SHOW_TITLE, MF_BYCOMMAND | (g_hsize == SIZE_SMALL ? MF_GRAYED : MF_ENABLED));
}

bool menu_command(UINT id, UINT first)
{
    if (id < first || id >= first + MENU_IDS) return false;
    UINT i = id - first;
    if (i < 4) set_size((Size)i);
    else set_shown((Element)(i - 4), !g_show[i - 4]);
    return true;
}

int height() { return g_height; }

void edit_address() { begin_edit(); }

HWND create(HWND frame, const Callbacks& cb)
{
    g_cb = cb;
    g_buttons_setting = button_images_setting();
    FLXP_DWM_FRAME_METRICS m = {};
    m.cbSize = sizeof m;
    FlxpDwmGetFrameMetrics(&m);
    g_height = height_for_caption(m.bValid ? m.cyCaption : 0);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = proc;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"flxp_header";
    RegisterClassW(&wc);
    make_fonts();
    g_buttons_setting = button_images_setting();
    read_settings();
    if (!load_framelets()) draw_framelets();
    grey_out(g_nav[0][DISABLED]); grey_out(g_nav[1][DISABLED]);
    g_wnd = CreateWindowExW(0, L"flxp_header", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 0, g_height, frame, (HMENU)IDC_HEADER, nullptr, nullptr);
    g_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"Edit", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, SEARCH_W, SEARCH_H, g_wnd, (HMENU)IDC_SEARCH, nullptr, nullptr);
    SendMessageW(g_edit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), FALSE);
    SendMessageW(g_edit, EM_SETCUEBANNER, FALSE, (LPARAM)L"Search");
    g_edit_old = (WNDPROC)SetWindowLongPtrW(g_edit, GWLP_WNDPROC, (LONG_PTR)edit_proc);
    g_mode = choose_mode();
    make_navbar_fonts();
    if (vision()) { vision_on(); vision_metrics(); }
    return g_wnd;
}

void destroy()
{
    if (vision()) vision_off();
    g_fixed_title.clear();
    set_tools({});
    if (g_wnd) DestroyWindow(g_wnd);      // the search box with it
    g_wnd = g_edit = g_addr = nullptr;
    g_editing = g_bar_hot = false;
    g_open = -1;
    g_crumbs.clear();
    g_hot = g_pressed = -1;
    g_nav_hot = g_nav_pressed = NONE;
    g_tracking = g_search_open = false;
}

void set_folder(const std::shared_ptr<shell::Folder>& f)
{
    if (!g_wnd || !f) return;
    g_title = g_fixed_title.empty() ? f->name() : g_fixed_title;
    // the path from the namespace root, without the Desktop itself (4074 starts at Computer)
    std::vector<Crumb> chain;
    auto add = [&](const std::string& name, const std::string& path) {
        Crumb c;
        c.name = name.empty() ? path : name;
        c.path = path;
        SetRectEmpty(&c.text);
        chain.insert(chain.begin(), c);
    };
    // flxp.ini breadcrumb_home = 0 (Folder Options' "Display the full path"): a folder under the home folder as the real path,
    // Computer > Local Disk (/) > u > aap > ..., instead of the namespace's Home > ...
    const char* home = getenv("HOME");
    std::string fp = f->path();
    bool full = home && *home && fp[0] == '/' && !shell::setting_flag("breadcrumb_home", true) &&
                (fp == home || (fp.size() > strlen(home) && !fp.compare(0, strlen(home), home) && fp[strlen(home)] == '/'));
    if (full) {
        for (std::string p = fp; p.size() > 1; p = p.substr(0, std::max<size_t>(p.rfind('/'), 1)))
            add(p.substr(p.rfind('/') + 1), p);
        if (auto r = shell::folder_for("/")) add(r->name(), "/");
        if (auto c = shell::folder_for("::computer")) add(c->name(), "::computer");
    } else {
        for (auto p = f; p; p = p->parent()) {
            if (p->path() == "::desktop" && !chain.empty()) break;
            add(p->name(), p->path());
        }
    }
    g_crumbs = chain;
    g_hot = -1;
    if (g_editing) { g_editing = false; ShowWindow(g_addr, SW_HIDE); }
    if (vision()) {
        lhnav_set_field(g_v.nav, LHNAV_ADDRESS, nullptr, -1, 0);
        vision_metrics();
    }
    layout();
}

void set_title(const std::string& title)
{
    g_fixed_title = title;
    if (!title.empty()) g_title = title;
    if (vision() && g_wnd) { vision_metrics(); layout(); }
    if (g_wnd) InvalidateRect(g_wnd, nullptr, TRUE);
}

void clear_filter()
{
    if (!g_edit) return;
    g_quiet = true;
    SetWindowTextW(g_edit, L"");
    g_quiet = false;
    if (vision() && !g_v.search_on) {
        lhnav_set_field(g_v.nav, LHNAV_SEARCH, nullptr, -1, 0);
        if (lhnav_search_open(g_v.nav)) { lhnav_set_search_open(g_v.nav, 0); layout(); }
        else vision_invalidate(LHNAV_SEARCH);
    }
}

void relayout(int width, int height)
{
    if (!g_wnd) return;
    g_height = height;
    SetWindowPos(g_wnd, nullptr, 0, 0, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    layout();
}

void composition_changed()
{
    g_pix_stale = true;
    if (vision()) {
        if (g_wnd) { vision_metrics(); layout(); }
        return;
    }
    make_fonts();               // luxwm's caption look may have changed with its theme
    Mode m = choose_mode();
    if (m != g_mode) apply_mode(m);
    else make_navbar_fonts();
    if (g_wnd) { layout(); InvalidateRect(g_wnd, nullptr, TRUE); }
}

void state_changed()
{
    if (vision() && g_wnd) {
        // maximised: border_t 0, no resize rows, the maximised colours; a new
        // height goes through the host's refit (the extension follows)
        vision_metrics();
        if (vision_extend() != g_height) PostMessageW(GetParent(g_wnd), WM_DWMCOLORIZATIONCOLORCHANGED, 0, 0);
        layout();
        return;
    }
    if (g_wnd) InvalidateRect(g_wnd, nullptr, TRUE);
}

void set_active(bool active)
{
    if (active == g_active) return;
    g_active = active;
    // the navigation bar's variant and the title colours change now; luxwm's
    // pixmap variant arrives as WM_FLXP_DWMHEADERCHANGED (re-read then)
    g_pix_stale = true;
    if (vision() && g_wnd) vision_metrics();
    if (g_wnd) InvalidateRect(g_wnd, nullptr, TRUE);
}

void pixmap_changed()
{
    g_pix_stale = true;
    if (g_wnd && g_mode == PIXMAP) InvalidateRect(g_wnd, nullptr, TRUE);
}

void open_search()
{
    if (!g_edit) return;
    if (vision()) { vision_open_search(); return; }
    if (nb_buttons() && !g_search_open) { g_search_open = true; layout(); }
    SetFocus(g_edit);
    SendMessageW(g_edit, EM_SETSEL, 0, -1);
}

void theme_changed()
{
    g_v.style_path.clear();     // the style again on the refit (another colour of the same file too)
    free_shrunk();
    for (int i = 0; i < 2; i++)
        for (int s = 0; s < 4; s++) { if (g_nav[i][s]) DeleteObject(g_nav[i][s]); g_nav[i][s] = nullptr; }
    if (!load_framelets()) draw_framelets();
    grey_out(g_nav[0][DISABLED]); grey_out(g_nav[1][DISABLED]);
    navbar::reload();
    g_buttons_setting = button_images_setting();
    if (g_mag_icon) DeleteObject(g_mag_icon);   // the theme's Longhorn resources may be another build's
    g_mag_icon = nullptr; g_mag_looked = false;
    g_mode = GLASS;             // fonts and layout again for whatever comes now
    read_settings();            // the theme's navbar_preset may give another size
    composition_changed();
}

HWND search_box() { return g_edit; }

std::string background_source()
{
    if (glass()) return "glass (luxwm's plate under a transparent header)";
    std::string bg = shell::theme_setting("header_background", "auto");
    if ((bg == "auto" || bg == "navbar" || bg.empty()) && navbar::skin().valid)
        return std::string("navigation bar, ") + navbar::skin().name + " layout, the theme's parts from " + navbar::skin().path + (bg == "auto" || bg.empty() ? "" : " (header_background=" + bg + ")");
    std::string why = bg == "pixmap" ? "header_background=pixmap" : "no Longhorn navigation bar in the theme's msstyles";
    return "luxwm's _LHWM_HEADER_PIXMAP, else the drawn Slate gradient (" + why + ")";
}

std::string buttons_source()
{
    std::string set = g_wnd ? g_buttons_setting : button_images_setting();
    const navbar::Skin& k = navbar::skin();
    std::string theme = k.buttons ? std::string("the theme's TravelBack/TravelFwd/Favorites::AnimationButton, ") + k.path
                                   + " (" + k.name + " layout)" : "";
    std::string fr = "the Avalon framelets (" + framelet_source() + ")";
    if (theme.empty()) return fr + " (the style has no travel buttons)";
    if (set == "framelets") return "glass/pixmap: " + fr + " (header_button_images=framelets); navigation bar: " + theme;
    return theme + (k.valid ? ", in every mode" : ", on glass and luxwm's pixmap (the style has no navigation bar)");
}

// which header draws (flxp --info "header engine"): lhnav's vision header
// (nav.ini, preset, its rows) or flxp's own, and why
static std::string engine_info()
{
    std::string ini, why;
    bool want = vision_wanted(&ini, &why);
    std::string out = "header engine " + why;
    if (want) {
        lhnav_metrics m;
        lhnav_metrics_default(&m);
        std::string preset = ini.empty() ? "" : vision_preset(ini);
        if (!ini.empty()) lhnav_metrics_load_preset(&m, ini.c_str(), preset.empty() ? nullptr : preset.c_str());
        char b[1100];
        snprintf(b, sizeof b, ": preset %s (%s variant), deco %d rows, tools \"%s\"", m.preset[0] ? m.preset : "(none)",
                 m.variant == LHNAV_BIG ? "big" : "small", m.deco_h, m.tools);
        out += b;
        if (g_v.on && g_v.nav) {
            snprintf(b, sizeof b, "; now %d client rows (lhnav_height) + %d line rows = the extension's %d, style %s",
                     lhnav_height(g_v.nav), lhnav_extend_top(g_v.nav) - lhnav_height(g_v.nav), lhnav_extend_top(g_v.nav),
                     g_v.style_path.empty() ? "(none: drawn art)" : g_v.style_path.c_str());
            out += b;
        }
        out += "; the flxp header's rows below apply when it is not used";
    }
    return out + "\n";
}

// The header's rows and buttons at each size as they are (or would be) laid
// out now: the settings, the session theme, luxwm's caption (flxp --info)
std::string metrics_info()
{
    if (!g_settings_read) read_settings();
    int keep_h = g_height, keep_size = g_hsize;
    Mode keep_mode = g_mode;
    if (!g_wnd) { g_mode = choose_mode(); g_buttons_setting = button_images_setting(); }
    FLXP_DWM_FRAME_METRICS m = {}; m.cbSize = sizeof m;
    FlxpDwmGetFrameMetrics(&m);
    int cap = m.bValid ? m.cyCaption : 0, border = m.bValid && m.cxLeftBorder > 0 ? m.cxLeftBorder : 9;
    std::string rows, others, travel;
    char b[640];
    for (int sz = 0; sz < 4; sz++) {
        g_hsize = sz;
        g_height = classic_height(cap);
        bool cc = classic_client_caption() && top_off() > 0;
        int from_top = (cc ? border : cap) + g_height;
        if (sz == keep_size) {
            snprintf(b, sizeof b, "%s (%sheader_size): %d rows", SIZE_NAMES[sz], g_prefix.c_str(), g_height);
            rows = b;
            if (cc) snprintf(b, sizeof b, " from luxwm's %d-row top border (CLIENT_CAPTION: the %d caption rows stay empty, then the %d-row band)",
                             border, top_off(), band_rows());
            else if (cap > 0) snprintf(b, sizeof b, " under the %d-row caption", cap);
            else b[0] = 0;
            rows += b;
            if (cap > 0) { snprintf(b, sizeof b, ", the dark line %d rows below the frame's top edge", from_top - 1); rows += b; }
        } else {
            snprintf(b, sizeof b, "%s %s %d", others.empty() ? "; the others:" : ",", SIZE_NAMES[sz], g_height);
            others += b;
        }
        if (sz != SIZE_SMALL && sz != SIZE_MEDIUM) continue;
        const navbar::Img& bi = nb_buttons() ? back_img(0, navbar::NORMAL) : navbar::Img();
        int bw = nb_buttons() ? bi.w : scaled() ? shrunk().fr[0][REST].w : BACK_W;
        snprintf(b, sizeof b, "%s%s Back %dx%d at (%d,%d), Forward %dx%d at (%d,%d), the drop-down rows %d..%d", travel.empty() ? "" : "; ",
                 SIZE_NAMES[sz], bw, travel_h(), travel_x0(), btn_top(), fwd_w(), travel_h(), travel_x0() + fwd_dx(), btn_top(),
                 btn_top() + split_row(), btn_top() + travel_h() - 1);
        travel += b;
        if (sz == SIZE_SMALL) {
            const char* style = even_small() ? "even (the style's SmallTravelBack/SmallTravelFwd)" : "uneven (the big pair box-filtered, the drop-down rows 1:1)";
            std::string why = g_small_setting == "auto" ? (sk().small ? "auto: the style has Small travel buttons" : "auto: no Small travel buttons in the style")
                            : g_small_setting == "even" && !even_small() ? "even asked, but no Small travel buttons: uneven" : g_small_setting;
            snprintf(b, sizeof b, ", %s, %sheader_small_buttons=%s", style, g_prefix.c_str(), why.c_str());
            travel += b;
            if (scaled() && nb_buttons() && !shrunk().glyph.empty()) travel += ", " + shrunk().glyph;
        }
    }
    g_hsize = keep_size; g_height = keep_h; g_mode = keep_mode;
    const navbar::Skin& k = sk();
    std::string edge = k.buttons && k.bg[0].bmp && k.bg_margins[0].bottom > 0 ? "the last row of " + std::string(k.path.substr(k.path.rfind('/') + 1)) + "'s Navbar::Rebar.BACKGROUND"
                                                                                : "4051's 33,36,36";
    edge += ", no Rebar.SPLITTER";
    return engine_info() + "header size  " + rows + others + "\ntravel       " + travel + "\nlower edge   " + edge + "\n";
}

std::string framelet_source()
{
    if (g_wnd) return g_framelet_source.empty() ? "drawn lookalikes (tried: " + g_framelet_tried + ")" : g_framelet_source;
    std::string tried, src = find_framelets(nullptr, &tried);
    return src.empty() ? "drawn lookalikes (no Microsoft.Windows.Client.dll with the framelet images; tried: " + tried + ")" : src;
}

void set_history(bool can_back, bool can_forward)
{
    if (g_can[0] == can_back && g_can[1] == can_forward) return;
    g_can[0] = can_back; g_can[1] = can_forward;
    if (vision()) {
        lhnav_set_history(g_v.nav, can_back, can_forward);
        vision_invalidate(LHNAV_BACK);
        return;
    }
    if (g_nav_hot != NONE && !g_can[is_back(g_nav_hot) ? 0 : 1]) g_nav_hot = NONE;
    if (g_wnd) invalidate_nav();
}

// an icon as a premultiplied 32bpp image (its alpha, else its mask)
static navbar::Img img_of_icon(HICON ic, int size)
{
    navbar::Img out;
    if (!ic) return out;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = size; bi.bmiHeader.biHeight = -size; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP b = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!b) return out;
    uint32_t* px = (uint32_t*)bits;
    ICONINFO ii = {};
    bool ok = false;
    if (GetIconInfo(ic, &ii)) {
        HDC dc = GetDC(nullptr);
        if (ii.hbmColor && GetDIBits(dc, ii.hbmColor, 0, (UINT)size, px, &bi, DIB_RGB_COLORS) > 0) {
            bool alpha = false;
            for (int i = 0; i < size * size; i++) if (px[i] >> 24) { alpha = true; break; }
            if (!alpha) {
                std::vector<uint32_t> mask((size_t)size * size);
                if (ii.hbmMask && GetDIBits(dc, ii.hbmMask, 0, (UINT)size, mask.data(), &bi, DIB_RGB_COLORS) > 0)
                    for (int i = 0; i < size * size; i++) px[i] = (mask[(size_t)i] & 0xffffff) ? 0 : (px[i] | 0xff000000u);
                else for (int i = 0; i < size * size; i++) px[i] |= 0xff000000u;
            }
            for (int i = 0; i < size * size; i++) {      // premultiply
                uint32_t v = px[i], a = v >> 24;
                px[i] = a << 24 | ((v >> 16 & 255) * a / 255) << 16 | ((v >> 8 & 255) * a / 255) << 8 | (v & 255) * a / 255;
            }
            ok = true;
        }
        ReleaseDC(nullptr, dc);
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
        if (ii.hbmMask) DeleteObject(ii.hbmMask);
    }
    if (!ok) { DeleteObject(b); return out; }
    out.bmp = b; out.w = size; out.h = size;
    return out;
}

void set_tools(const std::vector<Tool>& tools)
{
    for (ToolBtn& b : g_tools) if (b.img.bmp) DeleteObject(b.img.bmp);
    g_tools.clear();
    for (const Tool& t : tools) {
        ToolBtn b;
        b.t = t;
        b.img = img_of_icon(t.icon, 16);
        g_tools.push_back(b);
    }
    g_tool_hot = g_tool_pressed = -1;
    if (vision() && g_wnd) vision_metrics();
    if (g_wnd) layout();
}

void enable_tool(int id, bool on)
{
    for (ToolBtn& b : g_tools)
        if (b.t.id == id && b.enabled != on) { b.enabled = on; if (g_wnd) InvalidateRect(g_wnd, &b.r, TRUE); }
}

bool tool_rect(int id, RECT* screen)
{
    if (vision() && g_wnd) {
        for (size_t i = 0; i < g_v.tool_ids.size(); i++) {
            int x, y, w, h;
            if (g_v.tool_ids[i] != id || !lhnav_rect(g_v.nav, LHNAV_TOOL + (int)i, &x, &y, &w, &h)) continue;
            *screen = RECT{ x, y, x + w, g_height };      // its menu flush under the header
            MapWindowPoints(g_wnd, nullptr, (POINT*)screen, 2);
            return true;
        }
        return false;
    }
    for (ToolBtn& b : g_tools)
        if (b.t.id == id && g_wnd) { *screen = b.r; MapWindowPoints(g_wnd, nullptr, (POINT*)screen, 2); return true; }
    return false;
}

} // namespace header
