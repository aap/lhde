// flxp-taskbar: the classic Windows XP taskbar (explorer's CTray, tray.cpp,
// traynot.cpp, trayclok.cpp in the XP SP1 source, read as the spec), drawn
// through uxtheme from the session's visual style: the TaskBar background,
// the Start button (Start::Button), the task band (taskband.cpp, a comctl32
// toolbar themed TaskBand::Toolbar), the notification area (TrayNotify, an
// XEmbed system tray) and the clock (TrayNotify::Clock). Without a visual
// style it is the classic 3D bar. One EWMH DOCK window on a screen edge with
// struts; the window list and the tray are xwm.cpp's (its own X thread).
//
//   flxp-taskbar [--edge bottom|top|left|right] [--theme NAME|PATH] [--classic] [--no-tray] [--quit-after MS]
#include <langinfo.h>
#include <math.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <map>
#include <string>
#include "shell/config.h"
#include "shell/icons.h"
#include "shell/settings.h"
#include "shell/lhtheme.h"
#include "shell/runtime.h"
#include "sys/sys.h"
#include "taskbar.h"
#include "w32/user.h"
extern "C" {
#include "w32/flxp_shell.h"
}

namespace tb {

// a bitmap of XP's explorer.exe: IDB_START 143, IDB_STARTCLASSIC 176, IDB_LOGOFF_NORMAL / _HOT 7010 / 7011, from the
// first imported source that has it (shell::xp_art: xp, 2k, reactos with its own ids); null when none has it
HBITMAP xp_explorer_bitmap(int id, UINT flags)
{
    static std::map<std::string, HINSTANCE> mods;
    std::string src = shell::xp_art("explorer.exe", id, 2);
    size_t hash = src.rfind('#');
    if (src.empty() || hash == std::string::npos) return nullptr;
    std::string f = src.substr(3, hash - 3);
    auto it = mods.find(f);
    if (it == mods.end()) it = mods.emplace(f, LoadLibraryExW((LPCWSTR)W(f).c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE)).first;
    return it->second ? (HBITMAP)LoadImageW(it->second, MAKEINTRESOURCEW(atoi(src.c_str() + hash + 1)), IMAGE_BITMAP, 0, 0, flags) : nullptr;
}

Bar g;

std::u16string W(const std::string& s) { return w32::utf16_of(s.c_str()); }
std::string U(const WCHAR* s) { return w32::utf8_of(s); }
std::string setting(const char* key, const char* fallback) { return shell::theme_setting(key, fallback); }

namespace {

enum { TIMER_CLOCK = 1, TIMER_QUIT = 2 };
const int TRAY_SLOT = 22;               // one notification icon: 16 px in a 22 px cell (traynot.cpp: 16 px buttons, TB_SETPADDING 2)
const int CLASSIC_EDGE = 2;             // the raised edge of the classic bar (SM_CXEDGE)

HTHEME th_start, th_tray, th_clock;
HIMAGELIST flag_list;                   // the start button's flag (explorer's IDB_START / IDB_STARTCLASSIC)
SIZE flag_size;
bool start_hot, start_down;
SIZE start_size;                        // the start button's ideal size
int start_image_cy;                     // Start::Button's own image height (Luna, Slate: 33), 0 classic
int clock_width;
std::string clock_text, clock_tip;
HWND clock_tip_wnd;
bool clock_hidden;                      // _LHSHELL_CLOCK_VISIBLE = 1 (luxshell's sidebar shows one)
bool tray_on = true;
int tray_icons;                         // icons shown in the tray
UINT w_clock_visible;
UINT w_settings;                        // _LH_SETTINGS_CHANGED (the per-theme start menu)

// ---- metrics ------------------------------------------------------------------

void make_fonts()
{
    if (g.font_caption) DeleteObject(g.font_caption);
    if (g.font_caption_bold) DeleteObject(g.font_caption_bold);
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    // explorer's _hfontCapNormal / _hfontCapBold (taskband.cpp _InitFonts, trayclok.cpp)
    ncm.lfCaptionFont.lfWeight = FW_NORMAL;
    g.font_caption = CreateFontIndirectW(&ncm.lfCaptionFont);
    ncm.lfCaptionFont.lfWeight = FW_BOLD;
    g.font_caption_bold = CreateFontIndirectW(&ncm.lfCaptionFont);
}

// Our own start flag, for a desktop without XP's or Windows 2000's explorer.exe (the free baseline; ReactOS's IDB_START is
// ReactOS's logo, which is not ours to show): a generic four-pane waving flag rendered here, no bitmap of anyone's. The
// cell is the one the bitmap it stands in for has (startclassic.bmp 16x16, start.bmp 25x20), straight alpha like theirs,
// with a soft shadow down and right. The panes are red-orange, green, blue, yellow; each pane lighter towards its top left,
// the cloth arched (the right column lower, the left edge leaning) and shaded along the wave. Taste, not a measurement.
HBITMAP drawn_flag(int w, int h)
{
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = h;          // bottom-up, as the resource bitmaps
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) return bmp;
    // the flag's box in the cell, and its wave: the arch (A), the right column's drop (B), the left edge's lean (C), all in px
    bool small = w < 20;
    double x0 = small ? 0.6 : 2.0, y0 = small ? 0.8 : 1.2, bw = small ? 13.6 : 19.0, bh = small ? 12.2 : 15.0;
    double A = small ? 1.0 : 1.4, B = small ? 1.4 : 2.0, C = small ? 1.0 : 1.6;
    static const double pane[4][3] = { { 240, 82, 28 }, { 122, 190, 24 }, { 34, 112, 236 }, { 252, 192, 8 } };
    const int SS = 5;                   // supersampling per axis
    std::vector<double> cov((size_t)w * h), col((size_t)w * h * 3);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            double a = 0, r = 0, gg = 0, b = 0;
            for (int sy = 0; sy < SS; sy++)
                for (int sx = 0; sx < SS; sx++) {
                    double px = x + (sx + 0.5) / SS, py = y + (sy + 0.5) / SS;
                    // invert the wave (two rounds: u and v depend on each other)
                    double u = (px - x0) / bw, v = (py - y0) / bh;
                    for (int it = 0; it < 3; it++) {
                        u = (px - x0 + C * (v - 0.5)) / bw;
                        v = (py - y0 + A * sin(M_PI * u) - B * u) / bh;
                    }
                    if (u < 0 || u >= 1 || v < 0 || v >= 1) continue;
                    double gu = 0.9 / bw, gv = 0.9 / bh;            // the gap between the panes: about a pixel
                    if (fabs(u - 0.5) < gu / 2 || fabs(v - 0.5) < gv / 2) continue;
                    int k = (u > 0.5) + 2 * (v > 0.5);
                    double lu = u > 0.5 ? (u - 0.5) * 2 : u * 2, lv = v > 0.5 ? (v - 0.5) * 2 : v * 2;
                    double shade = 1.18 - 0.30 * (0.6 * lu + 0.4 * lv) + 0.10 * cos(2 * M_PI * u);
                    a += 1;
                    r += fmin(255, pane[k][0] * shade); gg += fmin(255, pane[k][1] * shade); b += fmin(255, pane[k][2] * shade);
                }
            size_t i = (size_t)y * w + x;
            cov[i] = a / (SS * SS);
            if (a > 0) { col[i * 3] = r / a; col[i * 3 + 1] = gg / a; col[i * 3 + 2] = b / a; }
        }
    uint8_t* p = (uint8_t*)bits;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            size_t i = (size_t)y * w + x;
            double fa = cov[i];
            double sa = x > 0 && y > 0 ? 0.35 * cov[(size_t)(y - 1) * w + x - 1] : 0;   // the shadow, one pixel down and right
            double oa = fa + sa * (1 - fa);
            double cr = oa > 0 ? col[i * 3] * fa / oa : 0, cg = oa > 0 ? col[i * 3 + 1] * fa / oa : 0, cb = oa > 0 ? col[i * 3 + 2] * fa / oa : 0;
            uint8_t* q = p + ((size_t)(h - 1 - y) * w + x) * 4;
            q[0] = (uint8_t)lround(cb); q[1] = (uint8_t)lround(cg); q[2] = (uint8_t)lround(cr); q[3] = (uint8_t)lround(oa * 255);
        }
    return bmp;
}

void load_flag()
{
    if (flag_list) ImageList_Destroy(flag_list);
    flag_list = nullptr;
    flag_size = SIZE{ g.theme ? 25 : 16, g.theme ? 20 : 16 };
    HBITMAP bmp = xp_explorer_bitmap(g.theme ? 143 : 176, LR_CREATEDIBSECTION);   // start.bmp / startclassic.bmp
    if (!bmp) bmp = drawn_flag(flag_size.cx, flag_size.cy);
    if (!bmp) return;
    BITMAP bm = {};
    GetObjectW(bmp, sizeof bm, &bm);
    flag_size = SIZE{ bm.bmWidth, bm.bmHeight < 0 ? -bm.bmHeight : bm.bmHeight };
    flag_list = ImageList_Create(flag_size.cx, flag_size.cy, ILC_COLOR32, 1, 1);
    ImageList_Add(flag_list, bmp, nullptr);
    DeleteObject(bmp);
}

// Without a bitmap at all (no DIB): the four panes of the flag as plain squares
void draw_flag(HDC hdc, int x, int y)
{
    if (flag_list) { ImageList_Draw(flag_list, 0, hdc, x, y, ILD_TRANSPARENT); return; }
    static const COLORREF c[4] = { RGB(240, 80, 30), RGB(120, 190, 30), RGB(20, 160, 240), RGB(255, 190, 0) };
    int w = flag_size.cx / 2 - 1, h = flag_size.cy / 2 - 1;
    for (int i = 0; i < 4; i++) {
        RECT r = { x + (i & 1) * (w + 2), y + (i >> 1) * (h + 2), 0, 0 };
        r.right = r.left + w; r.bottom = r.top + h;
        HBRUSH b = CreateSolidBrush(c[i]);
        FillRect(hdc, &r, b);
        DeleteObject(b);
    }
}

const WCHAR* start_text() { return g.theme ? L"start" : L"Start"; }   // IDS_START / IDS_STARTCLASSIC

HFONT start_font(HDC hdc, bool& owned)
{
    owned = false;
    LOGFONTW lf;
    if (th_start && SUCCEEDED(GetThemeFont(th_start, hdc, BP_PUSHBUTTON, PBS_NORMAL, TMT_FONT, &lf))) {
        owned = true;
        return CreateFontIndirectW(&lf);
    }
    return g.font_caption_bold;     // tray.cpp _CreateStartFont: the caption font, bold
}

// Button_GetIdealSize of the start button: flag + text + the content margins
void measure_start()
{
    HDC hdc = GetDC(g.wnd);
    bool owned;
    HFONT f = start_font(hdc, owned);
    HGDIOBJ old = SelectObject(hdc, f);
    SIZE ts = {};
    GetTextExtentPoint32W(hdc, start_text(), lstrlenW(start_text()), &ts);
    SelectObject(hdc, old);
    if (owned) DeleteObject(f);
    ReleaseDC(g.wnd, hdc);
    if (th_start) {
        MARGINS m = {};
        GetThemeMargins(th_start, nullptr, BP_PUSHBUTTON, PBS_NORMAL, TMT_CONTENTMARGINS, nullptr, &m);
        start_size.cx = m.cxLeftWidth + flag_size.cx + ts.cx + m.cxRightWidth;
        start_size.cy = (ts.cy > flag_size.cy ? ts.cy : flag_size.cy) + m.cyTopHeight + m.cyBottomHeight;
        SIZE ps = {};
        start_image_cy = SUCCEEDED(GetThemePartSize(th_start, nullptr, BP_PUSHBUTTON, PBS_NORMAL, nullptr, TS_TRUE, &ps)) ? ps.cy : 0;
        // a TRUESIZE button whose image carries the flag and the word (Watercolor Lite, the ReactOS styles: a 1-px FONT and a
        // negative left content margin) is at least the image's width. Measured, not XP's code: their margins are tuned to
        // Windows' text extent of "start" in that 1-px font (about 6 px Arial, 10 Tahoma Bold; FreeType's is 2), so on XP
        // the button holds the whole image (2k3-205316.png: Watercolor Lite's art 4 px in; WinXP-193456/193504/193512.png:
        // Lautus, Lunar, Mizu 2 px further right than with the bare ideal size, which centred the wider image off the bar)
        int st = 0;
        if (SUCCEEDED(GetThemeEnumValue(th_start, BP_PUSHBUTTON, PBS_NORMAL, TMT_SIZINGTYPE, &st)) && st == ST_TRUESIZE &&
            start_size.cx < ps.cx)
            start_size.cx = ps.cx;
    } else {
        start_image_cy = 0;
        // the classic push button: flag 4 px in (2 px edge + 2), 3 px to the text, 4 px after it
        start_size.cx = 4 + flag_size.cx + 3 + ts.cx + 4;
        start_size.cy = 22;
    }
}

std::string clock_format()
{
    std::string f = setting("clock_format");
    if (!f.empty()) return f;
    // the locale's time without seconds (XP: LOCALE_STIMEFORMAT with TIME_NOSECONDS)
    std::string t = nl_langinfo(T_FMT);
    std::string ampm = nl_langinfo(T_FMT_AMPM);
    const char* am = nl_langinfo(AM_STR);
    if (am && *am && !ampm.empty() && (t == "%r" || t.find("%p") != std::string::npos || t.find("%I") != std::string::npos)) t = ampm;
    std::string out;
    for (size_t i = 0; i < t.size(); i++) {
        if (t[i] == '%' && i + 1 < t.size()) {
            char c = t[i + 1];
            if (c == 'r') { out += "%I:%M %p"; i++; continue; }
            if (c == 'T') { out += "%H:%M"; i++; continue; }
            if (c == 'S') {        // drop the seconds and the separator before them
                while (!out.empty() && out.back() != 'M') out.pop_back();
                i++;
                continue;
            }
            if (c == 'I') { out += "%l"; i++; continue; }   // XP's h:mm tt: no leading zero
        }
        out += t[i];
    }
    return out;
}

std::string format_time(const struct tm& tm, const std::string& f)
{
    char buf[128];
    size_t n = strftime(buf, sizeof buf, f.c_str(), &tm);
    std::string s(buf, n);
    while (!s.empty() && s[0] == ' ') s.erase(0, 1);
    return s;
}

bool show_date() { return setting("clock_date", "0") == "1"; }

void update_clock_text()
{
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    clock_text = format_time(tm, clock_format());
    if (show_date()) clock_text += ", " + format_time(tm, "%b %e");   // 4051's sidebar clock: "7:22 AM, Oct 23"
    std::string d = setting("clock_tip_format", "%A, %B %e, %Y");     // XP's tooltip: the long date
    clock_tip = format_time(tm, d);
    for (size_t p; (p = clock_tip.find("  ")) != std::string::npos;) clock_tip.erase(p, 1);
}

void measure_clock()
{
    if (clock_hidden) { clock_width = 0; return; }
    HDC hdc = GetDC(g.clock);
    HGDIOBJ old = SelectObject(hdc, g.font_caption);
    // trayclok.cpp _GetMaxTimeSize: 11 and 23 o'clock with "00" of slop
    struct tm tm = {};
    tm.tm_hour = 11; tm.tm_min = 59; tm.tm_mday = 28; tm.tm_mon = 8; tm.tm_year = 100;
    int w = 0;
    for (int h : { 11, 23 }) {
        tm.tm_hour = h;
        std::string s = format_time(tm, clock_format());
        if (show_date()) s += ", " + format_time(tm, "%b %e");
        s += "00";
        RECT r = {};
        std::u16string ws = W(s);
        if (th_clock) GetThemeTextExtent(th_clock, hdc, CLP_TIME, 0, (LPCWSTR)ws.c_str(), -1, 0, nullptr, &r);
        else { SIZE sz; GetTextExtentPoint32W(hdc, (LPCWSTR)ws.c_str(), (int)ws.size(), &sz); r.right = sz.cx; }
        if (r.right - r.left > w) w = r.right - r.left;
    }
    SelectObject(hdc, old);
    ReleaseDC(g.clock, hdc);
    clock_width = w;
}

void arm_clock_timer()
{
    time_t now = time(nullptr);
    UINT ms = (UINT)(60 - now % 60) * 1000 + 50;     // re-armed for the next minute, as trayclok.cpp
    SetTimer(g.wnd, TIMER_CLOCK, ms, nullptr);
}

// ---- layout ---------------------------------------------------------------------

struct TrayGeometry { RECT tray, icons, clock; };

// The notification area's width (horizontal) or height (vertical) and its parts in its own coordinates
TrayGeometry tray_geometry(int across)
{
    TrayGeometry t = {};
    bool h = g.horizontal();
    MARGINS m = {};
    if (th_tray) GetThemeMargins(th_tray, nullptr, TNP_BACKGROUND, 0, TMT_CONTENTMARGINS, nullptr, &m);
    else { m.cxLeftWidth = 1; m.cyTopHeight = 1; m.cxRightWidth = 1 + 2; m.cyBottomHeight = 1 + 2; }   // traynot.cpp: border + 2
    int n = tray_on ? tray_icons : 0;
    if (h) {
        int inner_h = across - m.cyTopHeight - m.cyBottomHeight;
        int icons_w = n * TRAY_SLOT;
        int len = m.cxLeftWidth + icons_w + clock_width + m.cxRightWidth;
        if (th_tray && n == 0 && clock_width == 0) len = 0;
        if (!th_tray && n == 0 && clock_width == 0) len = 0;
        t.tray = RECT{ 0, 0, len, across };
        int slot_h = inner_h - inner_h % TRAY_SLOT;
        if (slot_h <= 0) slot_h = inner_h;
        t.icons = RECT{ (LONG)m.cxLeftWidth, (LONG)(m.cyTopHeight + (inner_h - slot_h) / 2), (LONG)(m.cxLeftWidth + icons_w),
                        (LONG)(m.cyTopHeight + (inner_h - slot_h) / 2 + slot_h) };
        t.clock = RECT{ t.icons.right, (LONG)m.cyTopHeight, (LONG)(t.icons.right + clock_width), (LONG)(across - m.cyBottomHeight) };
    } else {
        int inner_w = across - m.cxLeftWidth - m.cxRightWidth;
        int per_row = inner_w / TRAY_SLOT; if (per_row < 1) per_row = 1;
        int rows = (n + per_row - 1) / per_row;
        int clock_h = clock_width ? 2 * GetSystemMetrics(SM_CYSIZE) / 2 + 4 : 0;
        int len = m.cyTopHeight + rows * TRAY_SLOT + clock_h + m.cyBottomHeight;
        t.tray = RECT{ 0, 0, across, len };
        int iw = (n < per_row ? n : per_row) * TRAY_SLOT;
        t.icons = RECT{ (LONG)(m.cxLeftWidth + (inner_w - iw) / 2), (LONG)m.cyTopHeight, 0, (LONG)(m.cyTopHeight + rows * TRAY_SLOT) };
        t.icons.right = t.icons.left + iw;
        t.clock = RECT{ (LONG)m.cxLeftWidth, t.icons.bottom, (LONG)(across - m.cxRightWidth), (LONG)(t.icons.bottom + clock_h) };
    }
    return t;
}

void render_tray_background(const RECT& tray_in_bar, const RECT& icons)
{
    int w = icons.right - icons.left, h = icons.bottom - icons.top;
    if (w <= 0 || h <= 0) return;
    int tw = tray_in_bar.right - tray_in_bar.left, th = tray_in_bar.bottom - tray_in_bar.top;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = tw;
    bi.bmiHeader.biHeight = -th;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) { DeleteDC(dc); return; }
    HGDIOBJ old = SelectObject(dc, bmp);
    RECT client;
    GetClientRect(g.wnd, &client);
    POINT org;
    SetWindowOrgEx(dc, tray_in_bar.left, tray_in_bar.top, &org);
    paint_bar_background(dc, client);
    SetWindowOrgEx(dc, 0, 0, nullptr);
    RECT tr = { 0, 0, tw, th };
    if (th_tray) DrawThemeBackground(th_tray, dc, TNP_BACKGROUND, 0, &tr, nullptr);
    const uint32_t* px = (const uint32_t*)bits;
    xwm::tray_background(px + (size_t)icons.top * tw + icons.left, w, h, tw);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
}

void layout()
{
    if (g.menu_only) {      // a 1x1 override-redirect window off the screen: the menus' owner and keyboard target
        SetWindowPos(g.wnd, nullptr, -16, -16, 1, 1, SWP_NOZORDER | SWP_NOACTIVATE);
        return;
    }
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    bool h = g.horizontal();
    std::string ts = setting(h ? "taskbar_height" : "taskbar_width");
    // XP: 30 px with a visual style, 28 visible rows classic (the window reaches 2 px off the screen there)
    int thick = atoi(ts.c_str());
    if (thick <= 0) thick = h ? (g.theme ? 30 : 28) : 0;
    measure_start();
    if (!h && thick <= 0) thick = start_size.cx > 100 ? start_size.cx : 100;
    g.thickness = thick;
    switch (g.edge) {
    case EdgeBottom: g.screen = RECT{ 0, sh - thick, sw, sh }; break;
    case EdgeTop: g.screen = RECT{ 0, 0, sw, thick }; break;
    case EdgeLeft: g.screen = RECT{ 0, 0, thick, sh }; break;
    case EdgeRight: g.screen = RECT{ sw - thick, 0, sw, sh }; break;
    }
    int bw = g.screen.right - g.screen.left, bh = g.screen.bottom - g.screen.top;
    SetWindowPos(g.wnd, nullptr, g.screen.left, g.screen.top, bw, bh, SWP_NOZORDER | SWP_NOACTIVATE);
    xwm::set_struts(g.edge, thick, 0, (h ? sw : sh) - 1);

    // the inner rectangle: classic bars lose their raised edge on the screen's inside
    RECT in = { 0, 0, bw, bh };
    if (!g.theme) {
        switch (g.edge) {
        case EdgeBottom: in.top += CLASSIC_EDGE; break;
        case EdgeTop: in.bottom -= CLASSIC_EDGE; break;
        case EdgeLeft: in.right -= CLASSIC_EDGE; break;
        case EdgeRight: in.left += CLASSIC_EDGE; break;
        }
        InflateRect(&in, -2, -2);
    }
    // task buttons across the bar: themed the bar's full thickness from its top (the TaskBand::Toolbar BUTTON images
    // carry their own transparent rows: Luna 3 above and 2 below a 25-row plate; 4051-vm-now.png: the Slate plate from
    // row 2, i.e. a 30-row button at the bar's top); classic 22 px (SM_CYSIZE + 2 * SM_CYEDGE) in 2 px of space
    if (g.theme) { g.button_top = 0; g.button_height = h ? thick : 25; }
    else { g.button_top = in.top; g.button_height = h ? in.bottom - in.top : 22; }

    measure_clock();
    tray_icons = xwm::tray_count();
    RECT start;
    TrayGeometry tg = tray_geometry(h ? (g.theme ? bh : in.bottom - in.top) : (g.theme ? bw : in.right - in.left));
    RECT tray = tg.tray;
    if (h) {
        int sx = g.theme ? 0 : in.left;
        // themed: at least the image's own height from the bar's top, the rows below the bar clipped. Luna's
        // 99x33 Start::Button is a 31-row green body over 2 rows of a lighter blue than the bar's bottom rows
        // (sizing margins 13/14): squeezed into 30 rows those 2 rows showed as a blue line under the button;
        // 4051-vm-now.png's Slate button is the image unsqueezed with its last rows off the screen
        int scy = start_image_cy > bh ? start_image_cy : bh;
        start = g.theme ? RECT{ 0, 0, start_size.cx, scy } : RECT{ sx, in.top, sx + start_size.cx, in.bottom };
        int tw = tray.right;
        OffsetRect(&tray, g.theme ? bw - tw : in.right - tw, g.theme ? 0 : in.top);
        // tray.cpp: the task view starts at _sizeStart.cx + g_cxFrame + 1
        RECT band = { start.right + GetSystemMetrics(SM_CXFRAME) + 1, g.button_top, tray.left - 3, g.button_top + g.button_height };
        band::place(band);
    } else {
        int sh2 = g.theme ? (start_size.cy > 30 ? start_size.cy : 30) : 22;
        start = g.theme ? RECT{ 0, 0, bw, sh2 } : RECT{ in.left, in.top, in.right, in.top + sh2 };
        int th2 = tray.bottom;
        OffsetRect(&tray, g.theme ? 0 : in.left, g.theme ? bh - th2 : in.bottom - th2);
        RECT band = { g.theme ? 0 : in.left, start.bottom + 4, g.theme ? bw : in.right, tray.top - 3 };
        band::place(band);
    }
    SetWindowPos(g.start, nullptr, start.left, start.top, start.right - start.left, start.bottom - start.top, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g.tray, nullptr, tray.left, tray.top, tray.right - tray.left, tray.bottom - tray.top, SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(g.tray, tray.right > tray.left ? SW_SHOWNA : SW_HIDE);
    SetWindowPos(g.clock, nullptr, tg.clock.left, tg.clock.top, tg.clock.right - tg.clock.left, tg.clock.bottom - tg.clock.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(g.clock, clock_width ? SW_SHOWNA : SW_HIDE);
    if (tray_on) {
        RECT ic = tg.icons;
        OffsetRect(&ic, tray.left, tray.top);
        xwm::tray_layout(ic.left, ic.top, ic.right - ic.left, ic.bottom - ic.top, TRAY_SLOT, 16, !h);
        render_tray_background(tray, tg.icons);
    }
    InvalidateRect(g.wnd, nullptr, TRUE);
}

void open_themes()
{
    if (g.theme) CloseThemeData(g.theme);
    if (th_start) CloseThemeData(th_start);
    if (th_tray) CloseThemeData(th_tray);
    if (th_clock) CloseThemeData(th_clock);
    g.theme = OpenThemeData(g.wnd, L"TaskBar");
    th_start = g.theme ? OpenThemeData(g.start, L"Button") : nullptr;   // SetWindowTheme "Start": Start::Button
    SetWindowTheme(g.tray, g.horizontal() ? L"TrayNotifyHoriz" : L"TrayNotifyVert", nullptr);
    th_tray = g.theme ? OpenThemeData(g.tray, L"TrayNotify") : nullptr;
    th_clock = g.theme ? OpenThemeData(g.clock, L"Clock") : nullptr;     // SetWindowTheme "TrayNotify": TrayNotify::Clock
    make_fonts();
    load_flag();
}

// ---- painting --------------------------------------------------------------------

void paint_start(HWND hwnd, HDC hdc)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    DrawThemeParentBackground(hwnd, hdc, &rc);
    bool owned;
    HFONT f = start_font(hdc, owned);
    HGDIOBJ oldf = SelectObject(hdc, f);
    SetBkMode(hdc, TRANSPARENT);
    const WCHAR* text = start_text();
    if (th_start) {
        int state = start_down ? PBS_PRESSED : start_hot ? PBS_HOT : PBS_NORMAL;
        DrawThemeBackground(th_start, hdc, BP_PUSHBUTTON, state, &rc, nullptr);
        RECT c;
        GetThemeBackgroundContentRect(th_start, hdc, BP_PUSHBUTTON, state, &rc, &c);
        draw_flag(hdc, c.left, c.top + (c.bottom - c.top - flag_size.cy) / 2);
        RECT tr = c;
        tr.left += flag_size.cx;
        COLORREF col = RGB(255, 255, 255), shadow = RGB(0, 0, 0);
        GetThemeColor(th_start, BP_PUSHBUTTON, state, TMT_TEXTCOLOR, &col);
        POINT off = { 0, 0 };
        if (SUCCEEDED(GetThemePosition(th_start, BP_PUSHBUTTON, state, TMT_TEXTSHADOWOFFSET, &off)) &&
            SUCCEEDED(GetThemeColor(th_start, BP_PUSHBUTTON, state, TMT_TEXTSHADOWCOLOR, &shadow)))
            DrawShadowText(hdc, text, (UINT)lstrlenW(text), &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, col, shadow, off.x, off.y);
        else {
            SetTextColor(hdc, col);
            DrawTextW(hdc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    } else {
        DrawFrameControl(hdc, &rc, DFC_BUTTON, DFCS_BUTTONPUSH | (start_down ? DFCS_PUSHED : 0));
        int d = start_down ? 1 : 0;
        int x = rc.left + 4 + d;
        draw_flag(hdc, x, rc.top + (rc.bottom - rc.top - flag_size.cy) / 2 + d);
        RECT tr = { x + flag_size.cx + 3, rc.top + d, rc.right - 2 + d, rc.bottom + d };
        SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
        DrawTextW(hdc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(hdc, oldf);
    if (owned) DeleteObject(f);
}

// settings.ini [taskbar] start_menu = panel (XP's start panel, the default) | classic (the classic popup menu): what the classic
// bar's Start button opens, read at every open; the start-menu provider next to luxshell's Avalon bar is the panel
std::string start_menu_mode() { return !g.menu_only && shell::store::get("taskbar", "start_menu") == "classic" ? "classic" : "panel"; }

// Our menu is the display's start menu: own _LH_STARTMENU_S<screen> so that luxshell forwards its Avalon Start button and
// the Super key to us
void update_menu_provider() { xwm::provide_start_menu(true); }

// anchor: a Start button elsewhere (luxshell's Avalon one, from _LHSHELL_START) or null for ours
void open_start_menu(const RECT* anchor = nullptr)
{
    if (start_down) return;
    start_down = true;
    InvalidateRect(g.start, nullptr, FALSE);
    UpdateWindow(g.start);
    RECT r;
    GetWindowRect(g.start, &r);
    if (g.menu_only && !anchor) {
        // no button of ours: bottom-left above the screen's bottom edge
        static RECT corner;
        corner = RECT{ 0, GetSystemMetrics(SM_CYSCREEN), 0, GetSystemMetrics(SM_CYSCREEN) };
        anchor = &corner;
    }
    std::string mode = start_menu_mode();
    // both publish _LH_STARTMENU_OPEN while they are up (xwm::set_start_menu_open)
    if (mode == "classic") startmenu::show(anchor ? *anchor : r, anchor != nullptr);
    else startpanel::show(anchor ? *anchor : r, anchor != nullptr);
    start_down = false;
    start_hot = false;
    InvalidateRect(g.start, nullptr, FALSE);
}

LRESULT CALLBACK start_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        paint_start(h, hdc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (!start_hot) {
            start_hot = true;
            TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 };
            TrackMouseEvent(&t);
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        start_hot = false;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:    // XP opens the menu on the press
        open_start_menu();
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT CALLBACK tray_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wp;
        RECT rc;
        GetClientRect(h, &rc);
        DrawThemeParentBackground(h, hdc, &rc);
        if (th_tray) DrawThemeBackground(th_tray, hdc, TNP_BACKGROUND, 0, &rc, nullptr);
        else DrawEdge(hdc, &rc, BDR_SUNKENOUTER, BF_RECT);   // the classic notification area: WS_EX_STATICEDGE
        return 1;
    }
    case WM_PRINTCLIENT:
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT CALLBACK clock_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        DrawThemeParentBackground(h, hdc, &rc);
        HGDIOBJ old = SelectObject(hdc, g.font_caption);
        SetBkMode(hdc, TRANSPARENT);
        std::u16string t = W(clock_text);
        UINT fl = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
        if (th_clock) {
            DrawThemeText(th_clock, hdc, CLP_TIME, 0, (LPCWSTR)t.c_str(), -1, fl, 0, &rc);
        } else {
            SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
            DrawTextW(hdc, (LPCWSTR)t.c_str(), -1, &rc, fl);
        }
        SelectObject(hdc, old);
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void set_clock_tip()
{
    if (!clock_tip_wnd) {
        clock_tip_wnd = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                        0, 0, 0, 0, g.wnd, nullptr, nullptr, nullptr);
        TTTOOLINFOW ti = {};
        ti.cbSize = sizeof ti;
        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd = g.wnd;
        ti.uId = (UINT_PTR)g.clock;
        ti.lpszText = (LPWSTR)L"";
        SendMessageW(clock_tip_wnd, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    }
    std::u16string t = W(clock_tip);
    TTTOOLINFOW ti = {};
    ti.cbSize = sizeof ti;
    ti.uFlags = TTF_IDISHWND;
    ti.hwnd = g.wnd;
    ti.uId = (UINT_PTR)g.clock;
    ti.lpszText = (LPWSTR)t.c_str();
    SendMessageW(clock_tip_wnd, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
}

// _LHSHELL_START for us: lhwm's root broadcast (only while luxshell does not run; start_key = 0 ignores it) or
// luxshell's message to our _LH_STARTMENU_S owner window (its Avalon Start button, the Super key it received)
void start_request(const xwm::StartRequest& q)
{
    static uint32_t done;
    if (q.serial == done) return;
    done = q.serial;
    if (q.from_root && setting("start_key", "1") != "1") return;
    if (start_down) {                       // open: a toggle closes it, a plain "open" leaves it
        if (q.toggle) { startpanel::close(); startmenu::close(); }
        return;
    }
    if (q.w > 0 && q.h > 0) {
        RECT a = { q.x, q.y, q.x + q.w, q.y + q.h };
        RECT own;
        GetWindowRect(g.start, &own);
        open_start_menu(EqualRect(&a, &own) && !g.menu_only ? nullptr : &a);
    } else {
        open_start_menu();
    }
}

void read_clock_visible()
{
    long v = 0;
    clock_hidden = FlxpGetRootCardinals("_LHSHELL_CLOCK_VISIBLE", &v, 1) >= 1 && v == 1;
    if (setting("clock", "1") == "0") clock_hidden = true;
}

void on_x_changed()
{
    xwm::Snapshot s;
    xwm::snapshot(s);
    if (!g.menu_only) band::update(s);      // menu only: no buttons, no _NET_WM_ICON_GEOMETRY next to luxshell's bar
    if ((s.changes & xwm::ChangeTray) && xwm::tray_count() != tray_icons) layout();
    if (s.changes & xwm::ChangeStartKey) start_request(s.start);
}

void notify_from_x()
{
    PostMessageW(g.wnd, WM_TB_XCHANGED, 0, 0);
}

LRESULT CALLBACK bar_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_MEASUREITEM: if (startmenu::measure_item((MEASUREITEMSTRUCT*)lp)) return TRUE; break;   // the Programs menus
    case WM_DRAWITEM: if (startmenu::draw_item((const DRAWITEMSTRUCT*)lp)) return TRUE; break;
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(h, &rc);
        paint_bar_background((HDC)wp, rc);
        return 1;
    }
    case WM_PRINTCLIENT:
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_TB_XCHANGED:
        on_x_changed();
        return 0;
    case WM_NOTIFY: {
        NMHDR* n = (NMHDR*)lp;
        if (n->hwndFrom == g.band) return band::notify(n);
        return 0;
    }
    case WM_COMMAND:
        if (band::command(LOWORD(wp))) return 0;
        break;
    case WM_TIMER:
        if (wp == TIMER_CLOCK) {
            update_clock_text();
            set_clock_tip();
            InvalidateRect(g.clock, nullptr, FALSE);
            arm_clock_timer();
        } else if (wp == TIMER_QUIT) {
            PostQuitMessage(0);
        } else {
            band::timer(wp);
        }
        return 0;
    case WM_THEMECHANGED:
        open_themes();
        band::theme_changed();
        layout();
        update_menu_provider();     // claims _LH_STARTMENU_S if we do not hold it yet
        InvalidateRect(h, nullptr, TRUE);
        return 0;
    case WM_FLXP_ROOTPROPERTY:
        if ((UINT)wp == w_settings) update_menu_provider();   // claims _LH_STARTMENU_S if we do not hold it yet
        if ((UINT)wp == w_clock_visible) {
            bool was = clock_hidden;
            read_clock_visible();
            if (was != clock_hidden) layout();
        }
        return 0;
    case WM_SETTINGCHANGE:
        make_fonts();
        layout();
        return 0;
    case WM_CLOSE:
        return 0;           // the session's shell: Log Off ends it, not Alt+F4
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// SIGTERM/SIGINT/SIGHUP: leave the message loop, so the tray icons are handed back (xwm::stop)
void on_signal(int)
{
    PostQuitMessage(0);
    sys::wake();
}

void usage(FILE* f)
{
    fprintf(f, "usage: flxp-taskbar [--edge bottom|top|left|right] [--theme NAME|PATH] [--classic] [--no-tray] [--start-menu-only]\n"
               "                    [--quit-after MS]\n");
}

} // namespace

void paint_bar_background(HDC hdc, const RECT& rc)
{
    if (g.theme) {
        static const int part[4] = { TBP_BACKGROUNDLEFT, TBP_BACKGROUNDTOP, TBP_BACKGROUNDRIGHT, TBP_BACKGROUNDBOTTOM };
        DrawThemeBackground(g.theme, hdc, part[g.edge], 0, &rc, nullptr);
        return;
    }
    FillRect(hdc, &rc, GetSysColorBrush(COLOR_3DFACE));
    // XP's classic bar reaches past the screen edge: only the raised edge on the inside shows
    RECT r = rc;
    switch (g.edge) {
    case EdgeBottom: r.left -= 2; r.right += 2; r.bottom += 2; break;
    case EdgeTop: r.left -= 2; r.right += 2; r.top -= 2; break;
    case EdgeLeft: r.left -= 2; r.top -= 2; r.bottom += 2; break;
    case EdgeRight: r.right += 2; r.top -= 2; r.bottom += 2; break;
    }
    DrawEdge(hdc, &r, EDGE_RAISED, BF_RECT);
}

} // namespace tb

using namespace tb;

int main(int argc, char** argv)
{
    int quit_ms = 0;
    std::string edge;
    // flxp-startmenu (a symlink to us): the start menu alone, luxshell's start-menu provider next to its Avalon taskbar
    if (const char* sl = strrchr(argv[0], '/'); std::string(sl ? sl + 1 : argv[0]) == "flxp-startmenu") setenv("FLXP_START_MENU_ONLY", "1", 1);
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto value = [&]() -> const char* {
            if (i + 1 >= argc) { fprintf(stderr, "flxp-taskbar: %s needs a value\n", a.c_str()); exit(2); }
            return argv[++i];
        };
        if (a == "--help" || a == "-h") { usage(stdout); return 0; }
        else if (a == "--edge") edge = value();
        else if (a == "--theme") setenv("FLXP_THEME", value(), 1), unsetenv("FLXP_THEME_FOLLOWED");
        else if (a == "--classic") setenv("FLXP_THEME", "classic", 1), unsetenv("FLXP_THEME_FOLLOWED");
        else if (a == "--no-tray") setenv("FLXP_TRAY", "0", 1);
        else if (a == "--start-menu-only") setenv("FLXP_START_MENU_ONLY", "1", 1);
        else if (a == "--quit-after") quit_ms = atoi(value());
        else { fprintf(stderr, "flxp-taskbar: unknown option %s\n", a.c_str()); usage(stderr); return 2; }
    }
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGHUP, on_signal);
    setlocale(LC_ALL, "");
    setlocale(LC_NUMERIC, "C");
    xwm::init_threads();
    shell::write_settings_template();
    shell::setup_fonts();
    shell::setup_theme();
    w32::init();
    if (edge.empty()) edge = setting("taskbar_edge", "bottom");
    g.edge = edge == "top" ? EdgeTop : edge == "left" ? EdgeLeft : edge == "right" ? EdgeRight : EdgeBottom;
    tray_on = setting("tray", "1") != "0";
    // only the start menu (flxp-startmenu, next to luxshell's Avalon taskbar): no bar, no struts, no tray, no task buttons
    g.menu_only = setting("start_menu_only", "0") == "1";
    // settings.ini [taskbar] kind (start-up only): none, or avalon while lux draws its bar -> only the menu
    std::string kind = shell::setting("start_menu_only").empty() ? setting("taskbar_kind") : "";
    if (kind == "none" || (kind == "avalon" && shell::store::lux_running())) g.menu_only = true;
    if (g.menu_only) tray_on = false;

    WNDCLASSW wc = {};
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpfnWndProc = bar_proc;
    wc.lpszClassName = L"Shell_TrayWnd";     // XP's names
    RegisterClassW(&wc);
    wc.lpfnWndProc = start_proc;
    wc.lpszClassName = L"FlxpStartButton";
    RegisterClassW(&wc);
    wc.lpfnWndProc = tray_proc;
    wc.lpszClassName = L"TrayNotifyWnd";
    RegisterClassW(&wc);
    wc.lpfnWndProc = clock_proc;
    wc.lpszClassName = L"TrayClockWClass";
    RegisterClassW(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    g.wnd = CreateWindowExW((g.menu_only ? 0 : WS_EX_FLXP_DOCK) | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"Shell_TrayWnd", L"Taskbar",
                            WS_POPUP | WS_CLIPCHILDREN, 0, sh - 30, sw, 30, nullptr, nullptr, nullptr, nullptr);
    if (!g.wnd) { fprintf(stderr, "flxp-taskbar: no window\n"); return 1; }
    g.own = sys::native_id(sys::window_for_user((void*)g.wnd));
    g.start = CreateWindowExW(0, L"FlxpStartButton", L"start", WS_CHILD | WS_VISIBLE, 0, 0, 1, 1, g.wnd, nullptr, nullptr, nullptr);
    SetWindowTheme(g.start, L"Start", nullptr);
    g.tray = CreateWindowExW(0, L"TrayNotifyWnd", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 1, 1, g.wnd, nullptr, nullptr, nullptr);
    g.clock = CreateWindowExW(0, L"TrayClockWClass", nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 1, 1, g.tray, nullptr, nullptr, nullptr);
    SetWindowTheme(g.clock, L"TrayNotify", nullptr);
    g.band = band::create(g.wnd);
    open_themes();
    band::theme_changed();
    w_clock_visible = FlxpWatchRootProperty("_LHSHELL_CLOCK_VISIBLE");
    w_settings = FlxpWatchRootProperty("_LH_SETTINGS_CHANGED");
    read_clock_visible();
    update_clock_text();

    if (!xwm::start(g.own, notify_from_x)) { fprintf(stderr, "flxp-taskbar: no X connection\n"); return 1; }
    if (tray_on) xwm::tray_start(setting("tray_takeover", "0") == "1", !g.horizontal());
    update_menu_provider();
    layout();
    set_clock_tip();
    arm_clock_timer();
    ShowWindow(g.wnd, SW_SHOWNA);
    if (quit_ms) SetTimer(g.wnd, TIMER_QUIT, (UINT)quit_ms, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (startmenu::dialog_message(&msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    xwm::stop();
    return 0;
}
