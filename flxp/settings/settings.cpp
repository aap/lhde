// flxp-settings: Display Properties (XP's desk.cpl) for the lhde session.
// A property sheet with Themes / Desktop / Appearance / Effects over liblhde's
// theme files and settings store (docs/THEME-FORMAT.md, docs/SETTINGS.md).
// Nothing is written before OK or Apply; Apply writes the changed theme keys
// with lhde_theme_set_key (NAME-modified.theme, selected on the first change),
// the per-user keys into settings.ini, sets _LH_THEME for a theme change and
// sends _LH_SETTINGS_CHANGED, and the dialog stays open (it follows the theme
// itself, as every flxp window does). Templates: flxp/rc/settings.rc.
#include "res/res.h"   // before the win32 headers: their macros clash with its names
#include "gdi/image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <limits.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <algorithm>
#include <string>
#include <vector>
#include "w32/w32.h"
#include "w32/user.h"
#include "w32/flxp_shell.h"
#include "resource.h"
#include "desktop/wallpaper.h"
#include "explorer/uiicons.h"
#include "filedlg/filedlg.h"
#include "shell/lhdata.h"
#include "shell/config.h"
#include "shell/icons.h"
#include "shell/lhtheme.h"
#include "shell/runtime.h"
#include "shell/settings.h"
#include "shell/wallpapers.h"
#include "wuxtheme/ut.h"
extern "C" {
#include "lhde/settings.h"
#include "lhde/theme.h"
#include "commctrl.h"
#include "prsht.h"
#include "vsstyle.h"
}

asm(".section .rodata\n"
    ".global flxp_settings_res_start\n.global flxp_settings_res_end\n"
    "flxp_settings_res_start:\n.incbin \"flxp/rc/settings.res\"\nflxp_settings_res_end:\n"
    ".previous\n");
extern "C" const unsigned char flxp_settings_res_start[], flxp_settings_res_end[];

namespace {

namespace store = shell::store;

HINSTANCE module()
{
    static res::Module* m;
    if (!m) m = res::load_memory(flxp_settings_res_start, (size_t)(flxp_settings_res_end - flxp_settings_res_start));
    return (HINSTANCE)m;
}

std::u16string W(const std::string& s) { return w32::utf16_of(s.c_str()); }

std::string get_text(HWND h)
{
    int n = GetWindowTextLengthW(h);
    std::vector<WCHAR> buf((size_t)n + 1);
    GetWindowTextW(h, buf.data(), n + 1);
    return w32::utf8_of(buf.data());
}

// ---- state -------------------------------------------------------------------------

// The theme keys the dialog edits (docs/THEME-FORMAT.md): the values the selected theme's record gives, and where each
// came from (lhde_theme_why). Apply writes the changed ones with lhde_theme_set_key: into the theme's file when it is the
// user's own, else into NAME-modified.theme, which is then selected. The background (Desktop.wallpaper, Desktop.mode) is
// a theme key too. Per user (settings.ini): the sidebar's mode, place and "with the classic taskbar", font smoothing.
enum { K_ICONS, K_FRAME, K_NAVBAR, K_KIND, K_MENU, K_COMP, K_ANIM, K_COUNT };
struct ThemeKey { const char* key; int value_id; };
const ThemeKey kKeys[K_COUNT] = {
    { "Desktop.icons", IDC_DT_ICONS },
    { "Explorer.chrome", IDC_AP_FRAME },
    { "Explorer.navbar", IDC_AP_NAVBAR },
    { "Shell.taskbar", IDC_AP_TASKBAR },
    { "Shell.start", IDC_AP_START },
    { "Glass.composition", IDC_AP_COMP },
    { "Glass.animations", IDC_AP_ANIM },
};
int source_id(int k) { return kKeys[k].value_id == IDC_DT_ICONS ? IDC_DT_ICONS_SRC : kKeys[k].value_id + 20; }

struct Values {
    std::string theme;
    std::string val[K_COUNT];          // the theme keys
    std::string source[K_COUNT];       // the line under each
    std::string wallpaper, wall_file;  // Desktop.wallpaper as written (none, 4074/Bliss, a path) and its file
    std::string mode;                  // Desktop.mode
    uint32_t color = 0x3a6ea5;         // Desktop.color
    std::string sbmode, sbmode_source, sbstack, sbclassic, smoothing;   // per user
};
Values committed, cur;            // what is in force / what the dialog shows
Values given;                     // what cur.theme's record gives (a change is what differs from it)
std::vector<store::ThemeEntry> themes;
HWND pages[4];                    // Themes, Desktop, Appearance, Effects (once created)
bool filling;                     // controls being set from the state: no PSM_CHANGED

const char* const kModes[][2] = { { "Fill", "fill" }, { "Fit", "fit" }, { "Stretch", "stretch" }, { "Center", "center" }, { "Tile", "tile" } };
const char* const kIcons[][2] = { { "Longhorn", "longhorn4074" }, { "Windows XP", "xp" }, { "Windows 2000", "2k" }, { "Wine", "wine" } };
const char* const kFrames[][2] = { { "Automatic (Longhorn when luxwm runs)", "auto" }, { "Windows XP", "xp" }, { "Longhorn header", "longhorn" } };
const char* const kNavbars[][2] = { { "Longhorn 4074", "4074" }, { "Longhorn 4051", "4051" }, { "Vision (luxnav)", "vision" }, { "None (XP toolbars)", "none" } };
const char* const kKinds[][2] = { { "Avalon (luxshell)", "avalon" }, { "Classic (flxp-taskbar)", "classic" }, { "None", "none" } };
const char* const kMenus[][2] = { { "flxp start panel", "flxp" }, { "luxshell start menu", "lux" } };
const char* const kComps[][2] = { { "Automatic", "auto" }, { "On", "on" }, { "Off", "off" } };
const char* const kAnims[][2] = { { "4074 plus", "4074plus" }, { "4074", "4074" }, { "Invented", "invented" }, { "5048", "5048" }, { "None", "none" } };
const char* const kSbModes[][2] = { { "Full", "full" }, { "Minimized", "minimized" }, { "Pinned", "pinned" }, { "None", "none" } };
// SETTINGS.md [sidebar] stacking: luxshell's two labels
const char* const kSbStacks[][2] = { { "On top of windows", "top" }, { "Under windows (desktop)", "bottom" } };
const char* const kMethods[][2] = { { "Standard", "standard" }, { "ClearType", "cleartype" } };

const char* const (*items_of(int k, size_t& n))[2]
{
    switch (k) {
    case K_ICONS: n = 4; return kIcons;
    case K_FRAME: n = 3; return kFrames;
    case K_NAVBAR: n = 4; return kNavbars;
    case K_KIND: n = 3; return kKinds;
    case K_MENU: n = 2; return kMenus;
    case K_COMP: n = 3; return kComps;
    default: n = 5; return kAnims;
    }
}

// a combo with the items; a value not among them is added as it is (a theme may name an icon set of its own)
void fill_items(HWND dlg, int id, const char* const (*items)[2], size_t n, const std::string& value)
{
    HWND c = GetDlgItem(dlg, id);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    int sel = -1;
    for (size_t i = 0; i < n; i++) {
        std::u16string t = W(items[i][0]);
        SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)t.c_str());
        if (value == items[i][1]) sel = (int)i;
    }
    if (sel < 0 && !value.empty()) {
        std::u16string t = W(value);
        sel = (int)SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)t.c_str());
    }
    SendMessageW(c, CB_SETCURSEL, (WPARAM)(sel < 0 ? 0 : sel), 0);
}

template<size_t N>
void fill_combo(HWND dlg, int id, const char* const (&items)[N][2], const std::string& value) { fill_items(dlg, id, items, N, value); }

// the selected item's value; one the list did not have (added by fill_items) stays what it was
std::string items_value(HWND dlg, int id, const char* const (*items)[2], size_t n, const std::string& was)
{
    int i = (int)SendDlgItemMessageW(dlg, id, CB_GETCURSEL, 0, 0);
    return i >= 0 && i < (int)n ? items[i][1] : was;
}

template<size_t N>
std::string combo_value(HWND dlg, int id, const char* const (&items)[N][2]) { return items_value(dlg, id, items, N, items[0][1]); }

void changed(HWND page)
{
    if (!filling) SendMessageW(GetParent(page), PSM_CHANGED, (WPARAM)page, 0);
}

// flxp.ini's own value of a key ("" = not set there)
std::string flxp_ini(const char* key)
{
    store::Ini f;
    f.load(shell::settings_path());
    return f.get("flxp", key);
}

// lhde_theme_why's text for the line under a value
std::string source_text(const std::string& why)
{
    if (why.compare(0, 5, "file ") == 0) {
        std::string f = why.substr(5);
        char d[PATH_MAX];
        bool mine = lhde_theme_user_dir(d, sizeof d) && f.compare(0, strlen(d), d) == 0;
        return mine ? "Set in your theme file (" + shell::base_of(f) + ")" : "Set by the theme";
    }
    if (why.compare(0, 5, "base ") == 0) {
        std::string b = why.substr(5);
        return "From the theme " + b.substr(0, b.find(' '));
    }
    if (why.compare(0, 7, "default") == 0) return "The style's default";
    if (why.compare(0, 9, "fallback:") == 0) return "Fallback:" + why.substr(9);
    if (why.compare(0, 9, "settings:") == 0) return "A legacy settings.ini override";
    return why;
}

std::string bool_value(const std::string& v) { return v == "1" || v == "true" || v == "yes" || v == "on" ? "1" : "0"; }

// The selected theme's values, as its record gives them, and where each comes from
void load_theme_values(Values& v)
{
    shell::SessionTheme st;
    if (!shell::resolve_theme(v.theme, st)) st = shell::SessionTheme();
    for (int k = 0; k < K_COUNT; k++) {
        v.val[k] = st.value(kKeys[k].key);
        auto w = st.why.find(kKeys[k].key);
        v.source[k] = w == st.why.end() ? "" : source_text(w->second);
    }
    if (v.val[K_FRAME].empty()) v.val[K_FRAME] = "auto";
    v.wallpaper = st.value("Desktop.wallpaper");
    v.wall_file = v.wallpaper == "none" ? "" : st.value("path.wallpaper");
    if (shell::wallpaper_hidden()) { v.wallpaper = "none"; v.wall_file.clear(); }   // the user's "no picture"
    v.mode = st.value("Desktop.mode");
    if (v.mode.empty()) v.mode = "fill";
    v.color = shell::parse_color(st.value("Desktop.color"));
    // the sidebar's mode: the user's [sidebar] mode, else the theme's Shell.sidebar
    std::string m = store::get("sidebar", "mode");
    v.sbmode = m.empty() ? st.value("Shell.sidebar") : m;
    if (v.sbmode.empty()) v.sbmode = "full";
    v.sbmode_source = m.empty() ? "The theme's (Shell.sidebar); yours once changed" : "Your setting, for every theme";
}

Values load_values()
{
    Values v;
    // the root property wins for the running session (SETTINGS.md "Conflicts")
    v.theme = shell::session_theme().name;
    if (v.theme.empty()) v.theme = store::get("theme", "name");
    std::string canon;
    if (shell::lookup_theme(v.theme, canon)) v.theme = canon;   // an alias is its theme
    v.sbstack = store::sidebar_stacking();
    v.sbclassic = bool_value(store::get("sidebar", "with_classic"));
    v.smoothing = shell::theme_setting("font_smoothing", "standard");
    if (v.smoothing == "0") v.smoothing = "off";
    load_theme_values(v);
    return v;
}

const store::ThemeEntry* entry(const std::string& name)
{
    for (const store::ThemeEntry& e : themes) if (e.name == name) return &e;
    return nullptr;
}

// ---- pixels ------------------------------------------------------------------------

struct Pixels {
    int w = 0, h = 0;
    std::vector<uint32_t> px;
};

// What the desktop shows for a background file ("" = none: the colour) at w x h as a scaled-down screen
void desktop_thumbnail(const std::string& file, const std::string& mode, uint32_t color, Pixels& out)
{
    static std::string last_file;
    static wallpaper::Image img;
    if (file != last_file) {
        img = wallpaper::Image();
        if (!file.empty()) wallpaper::load(file, img);
        last_file = file;
    }
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    if (sw <= 0 || sh <= 0) { sw = 1024; sh = 768; }
    wallpaper::Image screen;
    screen.width = sw; screen.height = sh;
    screen.px.resize((size_t)sw * sh);
    wallpaper::compose(img, wallpaper::mode_from(mode.empty() ? "fill" : mode), color, screen.px.data(), sw, sh, 0, 0, sw, sh);
    out.px.assign((size_t)out.w * out.h, 0);
    wallpaper::compose(screen, wallpaper::Mode::Stretch, 0, out.px.data(), out.w, out.h, 0, 0, out.w, out.h);
}

// a theme colour from [SysMetrics], else the fallback (0xRRGGBB)
uint32_t sys_color(ut_theme* t, const char* name, uint32_t fallback)
{
    uint32_t c;
    int p = ut_prop(t, name);
    return p && ut_get_color(t, "sysmetrics", 0, 0, p, &c) ? c : fallback;
}

void fill_rect(Pixels& p, int x0, int y0, int x1, int y1, uint32_t rgb)
{
    for (int y = y0 < 0 ? 0 : y0; y < y1 && y < p.h; y++)
        for (int x = x0 < 0 ? 0 : x0; x < x1 && x < p.w; x++) p.px[(size_t)y * p.w + x] = 0xff000000u | rgb;
}

// The Themes tab's sample: the theme's background with a window drawn by
// libuxtheme from its style (caption, frame, caption buttons, a push button, a
// scroll bar). The style is read directly (also a Longhorn-compiled one).
void theme_sample(const std::string& name, Pixels& out)
{
    shell::SessionTheme st;
    if (!shell::resolve_theme(name, st)) return;
    if (name == cur.theme) desktop_thumbnail(cur.wall_file, cur.mode, cur.color, out);
    else {
        shell::Wallpaper w;
        shell::theme_wallpaper(st, w);
        desktop_thumbnail(w.file, w.mode, w.color, out);
    }
    if (st.msstyles.empty()) return;
    ut_theme* t = ut_open(st.msstyles.c_str(), st.color.empty() ? nullptr : st.color.c_str(), st.size.empty() ? nullptr : st.size.c_str());
    if (!t) t = ut_open(st.msstyles.c_str(), nullptr, nullptr);
    if (!t) return;
    uint32_t* px = out.px.data();
    int W = out.w, H = out.h, stride = W * 4;
    auto bg = [&](const char* cls, int part, int state, int l, int tp, int r, int b) {
        ut_rect rc = { l, tp, r, b };
        return ut_draw_background(t, cls, part, state, px, W, H, stride, &rc, nullptr);
    };
    int cap = 0, bl = 4, bb = 4;
    int p = ut_prop(t, "CAPTIONBARHEIGHT");
    if (!p || !ut_get_int(t, "sysmetrics", 0, 0, p, &cap) || cap <= 0) cap = 18;
    int cx, cy;
    ut_rect big = { 0, 0, 400, 400 };
    if (ut_part_size(t, "Window", WP_FRAMELEFT, FS_ACTIVE, 1, &big, &cx, &cy) && cx > 0 && cx < 20) bl = cx;
    if (ut_part_size(t, "Window", WP_FRAMEBOTTOM, FS_ACTIVE, 1, &big, &cx, &cy) && cy > 0 && cy < 20) bb = cy;
    int caph = cap + bl + 2;
    int L = W / 10, T = H / 8, R = W - W / 10, B = H - H / 10;
    // client area first, then the frame over its edges
    fill_rect(out, L, T + caph, R, B, sys_color(t, "BTNFACE", 0xece9d8));
    bg("Window", WP_CAPTION, CS_ACTIVE, L, T, R, T + caph);
    bg("Window", WP_FRAMELEFT, FS_ACTIVE, L, T + caph, L + bl, B - bb);
    bg("Window", WP_FRAMERIGHT, FS_ACTIVE, R - bl, T + caph, R, B - bb);
    bg("Window", WP_FRAMEBOTTOM, FS_ACTIVE, L, B - bb, R, B);
    int bs = cap - 3, by = T + (caph - bs) / 2 + 1;
    bg("Window", WP_CLOSEBUTTON, CBS_NORMAL, R - bl - 2 - bs, by, R - bl - 2, by + bs);
    bg("Window", WP_MAXBUTTON, MAXBS_NORMAL, R - bl - 4 - 2 * bs, by, R - bl - 4 - bs, by + bs);
    bg("Window", WP_MINBUTTON, MINBS_NORMAL, R - bl - 6 - 3 * bs, by, R - bl - 6 - 2 * bs, by + bs);
    ut_rect tr = { L + bl + 4, T, R - bl - 8 - 3 * bs, T + caph };
    std::string title = st.title();
    ut_draw_text(t, "Window", WP_CAPTION, CS_ACTIVE, px, W, H, stride, title.c_str(), 0x24 | 0x8000 /* VCENTER|SINGLELINE|END_ELLIPSIS */, &tr);
    // a scroll bar along the client's right edge
    int sbw = 17, cl = L + bl, cr = R - bl, ct = T + caph, cb = B - bb;
    int sx = cr - sbw;
    bg("ScrollBar", SBP_ARROWBTN, ABS_UPNORMAL, sx, ct, cr, ct + sbw);
    bg("ScrollBar", SBP_UPPERTRACKVERT, SCRBS_NORMAL, sx, ct + sbw, cr, cb - sbw);
    bg("ScrollBar", SBP_THUMBBTNVERT, SCRBS_NORMAL, sx, ct + sbw, cr, ct + sbw + (cb - ct) / 3);
    bg("ScrollBar", SBP_GRIPPERVERT, SCRBS_NORMAL, sx, ct + sbw, cr, ct + sbw + (cb - ct) / 3);
    bg("ScrollBar", SBP_ARROWBTN, ABS_DOWNNORMAL, sx, cb - sbw, cr, cb);
    // window text and a push button
    ut_rect wt = { cl + 10, ct + 8, sx - 6, ct + 26 };
    ut_draw_text(t, "Button", BP_PUSHBUTTON, PBS_NORMAL, px, W, H, stride, "Window Text", 0x20, &wt);
    int bw = 75, bh = 23, bx = (cl + sx - bw) / 2, by2 = cb - bh - 12;
    bg("Button", BP_PUSHBUTTON, PBS_DEFAULTED, bx, by2, bx + bw, by2 + bh);
    ut_rect btr = { bx, by2, bx + bw, by2 + bh };
    ut_draw_text(t, "Button", BP_PUSHBUTTON, PBS_DEFAULTED, px, W, H, stride, "OK", 0x25, &btr);
    ut_close(t);
}

// Owner-drawn preview statics: pixels into the item rectangle with a sunken edge
void paint_pixels(const DRAWITEMSTRUCT* di, void (*make)(Pixels&))
{
    RECT r = di->rcItem;
    Pixels p;
    p.w = r.right - r.left - 2; p.h = r.bottom - r.top - 2;
    if (p.w <= 0 || p.h <= 0) return;
    make(p);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = p.w; bi.bmiHeader.biHeight = -p.h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC mem = CreateCompatibleDC(di->hDC);
    HBITMAP b = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (b && bits) {
        memcpy(bits, p.px.data(), p.px.size() * 4);
        HGDIOBJ old = SelectObject(mem, b);
        BitBlt(di->hDC, r.left + 1, r.top + 1, p.w, p.h, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(b);
    }
    DeleteDC(mem);
    DrawEdge(di->hDC, &r, BDR_SUNKENOUTER, BF_RECT);
}

void make_theme_sample(Pixels& p) { theme_sample(cur.theme, p); }
void make_desktop_sample(Pixels& p) { desktop_thumbnail(cur.wall_file, cur.mode, cur.color, p); }

// ---- apply -------------------------------------------------------------------------

void remember_wallpaper(const std::string& path);
void fill_desktop(HWND dlg);
void fill_appearance(HWND dlg);
void fill_theme_list(HWND dlg);

// the dialog's state for theme NAME (another one picked, or the one Apply selected)
void select_theme(const std::string& name)
{
    cur.theme = name;
    load_theme_values(cur);
    given = cur;
    if (pages[1]) fill_desktop(pages[1]);
    if (pages[2]) fill_appearance(pages[2]);
}

void apply()
{
    // the theme keys that differ from what the selected theme gives: into its file or its modified copy
    std::vector<store::KeyValue> kv;
    for (int k = 0; k < K_COUNT; k++)
        if (cur.val[k] != given.val[k]) kv.push_back(store::KeyValue{ kKeys[k].key, cur.val[k] });
    bool wall = cur.wallpaper != given.wallpaper;
    if (wall) kv.push_back(store::KeyValue{ "Desktop.wallpaper", cur.wallpaper });
    if (cur.mode != given.mode) kv.push_back(store::KeyValue{ "Desktop.mode", cur.mode });
    // per user: settings.ini
    std::vector<store::Edit> edits;
    if (cur.sbmode != given.sbmode) edits.push_back(store::Edit{ "sidebar", "mode", cur.sbmode });
    if (cur.sbstack != committed.sbstack) edits.push_back(store::Edit{ "sidebar", "stacking", cur.sbstack });
    if (cur.sbclassic != committed.sbclassic) edits.push_back(store::Edit{ "sidebar", "with_classic", cur.sbclassic });
    if (cur.smoothing != committed.smoothing) edits.push_back(store::Edit{ "fonts", "smoothing", cur.smoothing });
    if (wall && cur.wallpaper != "none" && shell::wallpaper_hidden()) edits.push_back(store::Edit{ "desktop", "wallpaper", "" });
    if (kv.empty() && edits.empty() && cur.theme == committed.theme) return;
    bool ok = edits.empty() || store::set_shared(edits);
    std::string err, name = cur.theme;
    if (!kv.empty()) ok = store::set_theme_keys(cur.theme, kv, &name, &err, false) && ok;
    if (!ok)
        filedlg::message_box(pages[0], "Display Properties", "Cannot write the settings" + (err.empty() ? " (" + store::path() + ")" : ": " + err) + ".",
                             MB_OK | MB_ICONEXCLAMATION);
    if (wall) remember_wallpaper(cur.wall_file);
    // order (SETTINGS.md): the files written, _LH_THEME (once), then one message
    if (name != committed.theme) store::apply_theme(name);
    else store::notify(kv.empty() ? (edits.size() == 1 ? edits[0].section : "") : "theme");
    // the theme in force now (NAME-modified after the first change), its values and their sources
    Values keep = cur;
    select_theme(name);
    cur.sbstack = keep.sbstack; cur.sbclassic = keep.sbclassic; cur.smoothing = keep.smoothing;
    if (pages[0]) { fill_theme_list(pages[0]); InvalidateRect(GetDlgItem(pages[0], IDC_TH_PREVIEW), nullptr, FALSE); }
    if (pages[2]) fill_appearance(pages[2]);
    committed = cur;
}

// ---- pages -------------------------------------------------------------------------

// ---- the Desktop tab's background list (XP's: names, the preview above) --------------

struct Wall { std::string label, value, file; };   // value: Desktop.wallpaper (none, 4074/Bliss, a path); file: the picture
std::vector<Wall> walls;

std::string expand(const std::string& p) { return !p.empty() && p[0] == '~' ? shell::home_dir() + p.substr(1) : p; }

std::string stem(const std::string& path)
{
    std::string b = shell::base_of(path);
    size_t dot = b.rfind('.');
    return dot == std::string::npos || dot == 0 ? b : b.substr(0, dot);
}

bool is_picture(const std::string& name)
{
    size_t dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    std::string e = name.substr(dot + 1);
    for (char& c : e) c = (char)tolower((unsigned char)c);
    return e == "bmp" || e == "jpg" || e == "jpeg" || e == "png" || (e == "webp" && gdi::webp_supported());
}

// wallpaper_dirs (flxp.ini, ':' separated): every picture directly in them is listed; default
// ~/Pictures/Wallpapers and lhde's wallpapers/<build> (shell/lhdata.h)
std::vector<std::string> wallpaper_dirs()
{
    std::string v = shell::setting("wallpaper_dirs");
    if (v.empty()) v = shell::default_wallpaper_dirs();
    std::vector<std::string> out;
    size_t i = 0;
    while (i <= v.size()) {
        size_t j = v.find(':', i);
        if (j == std::string::npos) j = v.size();
        std::string d = expand(v.substr(i, j - i));
        struct stat st;
        if (!d.empty() && stat(d.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) out.push_back(d);
        i = j + 1;
    }
    return out;
}

// the user's own choices (Browse...), newest first: flxp.ini wallpapers = a;b;...
std::vector<std::string> recent_wallpapers()
{
    std::vector<std::string> out;
    std::string v = flxp_ini("wallpapers");
    size_t i = 0;
    while (i < v.size()) {
        size_t j = v.find(';', i);
        if (j == std::string::npos) j = v.size();
        if (j > i) out.push_back(v.substr(i, j - i));
        i = j + 1;
    }
    return out;
}

void remember_wallpaper(const std::string& path)
{
    if (path.empty()) return;
    std::string dir = shell::dir_of(expand(path));
    for (const std::string& d : wallpaper_dirs()) if (d == dir) return;   // listed anyway
    std::vector<std::string> r = recent_wallpapers();
    std::string v = path;
    int n = 1;
    for (const std::string& p : r)
        if (p != path && n < 10) { v += ";" + p; n++; }
    shell::write_setting("wallpapers", v);
}

// the background the theme brings without the user's change: its base's for NAME-modified, else its own
shell::Wallpaper theme_background(std::string* value)
{
    shell::SessionTheme st;
    shell::Wallpaper w;
    std::string name = cur.theme, base;
    if (shell::is_user_theme(name) && name.size() > 9 && name.compare(name.size() - 9, 9, "-modified") == 0 &&
        shell::resolve_theme(name, st)) base = st.value("Theme.base");
    if (!shell::resolve_theme(base.empty() ? name : base, st)) return w;
    shell::theme_wallpaper(st, w);
    if (value) *value = st.value("Desktop.wallpaper");
    return w;
}

void build_walls()
{
    walls.clear();
    walls.push_back(Wall{ "(None)", "none", "" });
    std::string tv;
    shell::Wallpaper tb = theme_background(&tv);
    if (!tb.file.empty()) walls.push_back(Wall{ stem(tb.file) + " (theme background)", tv, tb.file });
    auto add = [&](const std::string& p, const std::string& label) {
        for (const Wall& w : walls) if (w.file == p) return;
        walls.push_back(Wall{ label, shell::wallpaper_value(p), p });
    };
    struct stat st;
    for (const std::string& p : recent_wallpapers()) if (stat(expand(p).c_str(), &st) == 0) add(expand(p), stem(p));
    if (!cur.wall_file.empty()) add(cur.wall_file, stem(cur.wall_file));
    std::vector<std::string> lhde_walls = shell::lhde_subdirs("wallpapers");
    for (const std::string& d : wallpaper_dirs()) {
        // lhde's wallpapers/<build>: the build after the name (4015, 4051 and 4074 each have a Bliss)
        bool by_build = std::find(lhde_walls.begin(), lhde_walls.end(), d) != lhde_walls.end();
        std::vector<std::string> names;
        if (DIR* dd = opendir(d.c_str())) {
            while (dirent* e = readdir(dd)) if (e->d_name[0] != '.' && is_picture(e->d_name)) names.push_back(e->d_name);
            closedir(dd);
        }
        std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return strcasecmp(a.c_str(), b.c_str()) < 0; });
        for (const std::string& n : names) {
            std::string p = d + "/" + n;
            add(p, by_build ? stem(p) + " (" + shell::base_of(d) + ")" : stem(p));
        }
    }
}

void fill_walls(HWND dlg)
{
    HWND lv = GetDlgItem(dlg, IDC_DT_LIST);
    build_walls();
    uiicons::sync();   // the icon set of the theme in force (it changes with Apply)
    SendMessageW(lv, WM_SETREDRAW, FALSE, 0);
    SendMessageW(lv, LVM_DELETEALLITEMS, 0, 0);
    int image = uiicons::index("image"), sel = 0;
    for (size_t i = 0; i < walls.size(); i++) {
        std::u16string t = W(walls[i].label);
        LVITEMW it = {};
        it.mask = LVIF_TEXT | LVIF_IMAGE;
        it.iItem = (int)i;
        it.pszText = (LPWSTR)t.c_str();
        it.iImage = walls[i].file.empty() ? I_IMAGENONE : image;
        SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&it);
        if (cur.wallpaper == "none" ? walls[i].value == "none" : (!walls[i].file.empty() && walls[i].file == cur.wall_file)) sel = (int)i;
    }
    LVITEMW st = {};
    st.stateMask = st.state = LVIS_SELECTED | LVIS_FOCUSED;
    SendMessageW(lv, LVM_SETITEMSTATE, (WPARAM)sel, (LPARAM)&st);
    SendMessageW(lv, LVM_ENSUREVISIBLE, (WPARAM)sel, FALSE);
    SendMessageW(lv, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(lv, nullptr, TRUE);
}

void show_source(HWND dlg, int k)
{
    std::string s = cur.val[k] != given.val[k] ? "Changed: Apply keeps it in your copy of the theme" : cur.source[k];
    SetDlgItemTextW(dlg, source_id(k), (LPCWSTR)W(s).c_str());
}

void show_value(HWND dlg, int k)
{
    size_t n;
    const char* const (*items)[2] = items_of(k, n);
    fill_items(dlg, kKeys[k].value_id, items, n, cur.val[k]);
    show_source(dlg, k);
}

void fill_desktop(HWND dlg)
{
    filling = true;
    fill_walls(dlg);
    fill_combo(dlg, IDC_DT_MODE, kModes, cur.mode);
    EnableWindow(GetDlgItem(dlg, IDC_DT_MODE), cur.wallpaper != "none");
    show_value(dlg, K_ICONS);
    filling = false;
    InvalidateRect(GetDlgItem(dlg, IDC_DT_PREVIEW), nullptr, FALSE);
}

void fill_appearance(HWND dlg)
{
    filling = true;
    for (int k = K_FRAME; k < K_COUNT; k++) show_value(dlg, k);
    fill_combo(dlg, IDC_AP_SBMODE, kSbModes, cur.sbmode);
    SetDlgItemTextW(dlg, IDC_AP_SBMODE_SRC, (LPCWSTR)W(cur.sbmode != given.sbmode ? "Changed: Apply keeps it for every theme" : cur.sbmode_source).c_str());
    fill_combo(dlg, IDC_AP_SBSTACK, kSbStacks, cur.sbstack);
    CheckDlgButton(dlg, IDC_AP_SBCLASSIC, cur.sbclassic == "1" ? BST_CHECKED : BST_UNCHECKED);
    filling = false;
}

// a theme key's combo changed
bool theme_key_command(HWND dlg, int id)
{
    for (int k = 0; k < K_COUNT; k++) {
        if (id != kKeys[k].value_id) continue;
        size_t n;
        const char* const (*items)[2] = items_of(k, n);
        cur.val[k] = items_value(dlg, id, items, n, cur.val[k]);
        show_source(dlg, k);
        return true;
    }
    return false;
}

void fill_theme_list(HWND dlg)
{
    themes = store::theme_list();
    HWND c = GetDlgItem(dlg, IDC_TH_LIST);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    int sel = -1;
    for (size_t i = 0; i < themes.size(); i++) {
        std::string t = themes[i].title + (themes[i].group == 2 ? " (style)" : "");
        std::u16string w = W(t);
        SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)w.c_str());
        if (themes[i].name == cur.theme) sel = (int)i;
    }
    if (sel < 0 && !cur.theme.empty()) {   // not in the list: show the name
        std::u16string w = W(cur.theme);
        sel = (int)SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)w.c_str());
        themes.push_back(store::ThemeEntry{ cur.theme, cur.theme, "", 1 });
    }
    SendMessageW(c, CB_SETCURSEL, (WPARAM)sel, 0);
    EnableWindow(GetDlgItem(dlg, IDC_TH_DELETE), shell::is_user_theme(cur.theme));
}

INT_PTR CALLBACK saveas_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    static std::string* out;
    switch (msg) {
    case WM_INITDIALOG:
        out = (std::string*)lp;
        SetDlgItemTextW(dlg, IDC_SA_NAME, (LPCWSTR)W(*out).c_str());
        SendDlgItemMessageW(dlg, IDC_SA_NAME, EM_SETSEL, 0, -1);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) { *out = get_text(GetDlgItem(dlg, IDC_SA_NAME)); EndDialog(dlg, IDOK); return TRUE; }
        if (LOWORD(wp) == IDCANCEL) { EndDialog(dlg, IDCANCEL); return TRUE; }
        return FALSE;
    }
    return FALSE;
}

// a theme's file name from its title: lower case, [a-z0-9._-] (THEME-FORMAT section 1), spaces as '-'
std::string theme_file_name(const std::string& title)
{
    std::string out;
    for (char c : title) {
        unsigned char u = (unsigned char)tolower((unsigned char)c);
        if ((u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || u == '.' || u == '_' || u == '-') out += (char)u;
        else if ((u == ' ' || u == '/' || u == '(' || u == ')') && !out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && (out.back() == '-' || out.back() == '.')) out.pop_back();
    while (!out.empty() && (out[0] == '-' || out[0] == '.')) out.erase(0, 1);
    return out;
}

// Save As... (docs/THEME-FORMAT.md section 3): the dialog's pending changes applied first; then NAME-modified.theme is
// renamed to the new name, another theme file of the user's is copied, a shipped theme or a bare style gets a new file
// with base = it. The new theme is selected.
void save_theme(HWND dlg)
{
    apply();   // pending changes into the modified copy (selected)
    const store::ThemeEntry* e = entry(cur.theme);
    std::string title = e ? e->title : cur.theme;
    const std::string mod = " (modified)";
    if (title.size() > mod.size() && title.compare(title.size() - mod.size(), mod.size(), mod) == 0) title.resize(title.size() - mod.size());
    title += " (custom)";
    if (DialogBoxParamW(module(), MAKEINTRESOURCEW(IDD_SAVEAS), GetParent(dlg), saveas_proc, (LPARAM)&title) != IDOK) return;
    std::string file = theme_file_name(title), canon, path = shell::user_theme_path(file);
    if (file.empty() || path.empty()) {
        filedlg::message_box(GetParent(dlg), "Save Theme As", "\"" + title + "\" cannot be used as a theme name.", MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    if (file != cur.theme && shell::lookup_theme(file, canon) && !shell::is_user_theme(file)) {
        filedlg::message_box(GetParent(dlg), "Save Theme As", "\"" + file + "\" is a built-in theme.", MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    if (file != cur.theme && shell::is_user_theme(file) &&
        filedlg::message_box(GetParent(dlg), "Save Theme As", "The theme \"" + file + "\" exists. Replace it?", MB_YESNO | MB_ICONEXCLAMATION) != IDYES)
        return;
    bool mine = shell::is_user_theme(cur.theme);
    std::string src = shell::user_theme_path(cur.theme);
    lhde_ini* ini = mine ? lhde_ini_load(src.c_str()) : lhde_ini_new();
    if (!ini) return;
    if (!mine) {
        lhde_ini_header(ini, "; a theme of your own (docs/THEME-FORMAT.md), written by Display Properties");
        lhde_ini_set(ini, "Theme", "base", cur.theme.c_str());
    }
    lhde_ini_set(ini, "Theme", "name", title.c_str());
    lhde_ini_set(ini, "Theme", "aliases", nullptr);
    bool ok = lhde_ini_save(ini, path.c_str()) == 0;
    lhde_ini_free(ini);
    if (!ok) {
        filedlg::message_box(GetParent(dlg), "Save Theme As", "Cannot write " + path + ".", MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    // renamed: the modified copy goes
    if (mine && file != cur.theme && cur.theme.size() > 9 && cur.theme.compare(cur.theme.size() - 9, 9, "-modified") == 0) unlink(src.c_str());
    store::apply_theme(file);
    select_theme(file);
    committed = cur;
    fill_theme_list(dlg);
    InvalidateRect(GetDlgItem(dlg, IDC_TH_PREVIEW), nullptr, FALSE);
}

// Delete: one of the user's theme files; its base (or the theme it came from) is selected
void delete_theme(HWND dlg)
{
    if (!shell::is_user_theme(cur.theme)) return;
    const store::ThemeEntry* e = entry(cur.theme);
    if (filedlg::message_box(GetParent(dlg), "Delete Theme", "Delete the theme \"" + (e ? e->title : cur.theme) + "\"?",
                             MB_YESNO | MB_ICONEXCLAMATION) != IDYES) return;
    shell::SessionTheme st;
    std::string base = shell::resolve_theme(cur.theme, st) ? st.value("Theme.base") : "";
    if (base.empty()) base = store::get("theme", "name") != cur.theme ? store::get("theme", "name") : "";
    unlink(shell::user_theme_path(cur.theme).c_str());
    bool in_force = cur.theme == committed.theme;
    select_theme(base);
    if (in_force && !base.empty()) { store::apply_theme(base); committed = cur; }
    fill_theme_list(dlg);
    InvalidateRect(GetDlgItem(dlg, IDC_TH_PREVIEW), nullptr, FALSE);
    if (!in_force) changed(dlg);
}

bool common(HWND dlg, UINT msg, LPARAM lp, INT_PTR& ret)
{
    if (msg == WM_NOTIFY && ((NMHDR*)lp)->code == PSN_APPLY) {
        apply();
        SetWindowLongPtrW(dlg, DWLP_MSGRESULT, PSNRET_NOERROR);
        ret = TRUE;
        return true;
    }
    return false;
}

// _LH_SETTINGS_CHANGED while the dialog is open: the sidebar's stacking is flipped by Win+S (luxwm, saved by luxshell),
// so it is read again; a pending change of ours stays pending
UINT w_settings;
WNDPROC sheet_old;

void stacking_changed()
{
    std::string now = store::sidebar_stacking();
    if (now == committed.sbstack) return;
    if (cur.sbstack == committed.sbstack) cur.sbstack = now;
    committed.sbstack = now;
    if (!pages[2]) return;
    filling = true;
    fill_combo(pages[2], IDC_AP_SBSTACK, kSbStacks, cur.sbstack);
    filling = false;
}

LRESULT CALLBACK sheet_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_FLXP_ROOTPROPERTY && w_settings && (UINT)wp == w_settings) stacking_changed();
    return CallWindowProcW(sheet_old, h, msg, wp, lp);
}

void watch_settings(HWND page)
{
    if (sheet_old) return;
    w_settings = FlxpWatchRootProperty("_LH_SETTINGS_CHANGED");
    sheet_old = (WNDPROC)SetWindowLongPtrW(GetParent(page), GWLP_WNDPROC, (LONG_PTR)sheet_proc);
}

INT_PTR CALLBACK themes_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    INT_PTR r;
    if (common(dlg, msg, lp, r)) return r;
    switch (msg) {
    case WM_INITDIALOG:
        pages[0] = dlg;
        watch_settings(dlg);
        fill_theme_list(dlg);
        return TRUE;
    case WM_DRAWITEM:
        if (wp == IDC_TH_PREVIEW) { paint_pixels((const DRAWITEMSTRUCT*)lp, make_theme_sample); return TRUE; }
        return FALSE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_TH_LIST:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                int i = (int)SendDlgItemMessageW(dlg, IDC_TH_LIST, CB_GETCURSEL, 0, 0);
                if (i >= 0 && i < (int)themes.size() && themes[(size_t)i].name != cur.theme) {
                    // a theme brings all of its own: background, icons, frame, navigation bar, taskbar, ...
                    select_theme(themes[(size_t)i].name);
                    EnableWindow(GetDlgItem(dlg, IDC_TH_DELETE), shell::is_user_theme(cur.theme));
                    InvalidateRect(GetDlgItem(dlg, IDC_TH_PREVIEW), nullptr, FALSE);
                    changed(dlg);
                }
            }
            return TRUE;
        case IDC_TH_SAVE: save_theme(dlg); return TRUE;
        case IDC_TH_DELETE: delete_theme(dlg); return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

void browse(HWND dlg)
{
    filedlg::Options o;
    o.title = "Browse";
    o.filters = { filedlg::Filter{ "Pictures (*.bmp;*.jpg;*.jpeg;*.png;*.webp)", { "*.bmp", "*.jpg", "*.jpeg", "*.png", "*.webp" }, {} },
                  filedlg::Filter{ "All Files", { "*" }, {} } };
    if (!cur.wall_file.empty()) o.initial_dir = shell::dir_of(cur.wall_file);
    else o.initial_dir = shell::home_dir() + "/Pictures";
    filedlg::Result r = filedlg::run(GetParent(dlg), o);
    // the sheet was disabled while the dialog ran: repaint its controls enabled
    RedrawWindow(GetParent(dlg), nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    if (!r.ok || r.paths.empty()) return;
    cur.wall_file = r.paths[0];
    cur.wallpaper = shell::wallpaper_value(cur.wall_file);
    fill_desktop(dlg);
    changed(dlg);
}

INT_PTR CALLBACK desktop_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    INT_PTR r;
    if (common(dlg, msg, lp, r)) return r;
    switch (msg) {
    case WM_INITDIALOG: {
        pages[1] = dlg;
        watch_settings(dlg);
        HWND lv = GetDlgItem(dlg, IDC_DT_LIST);
        uiicons::init();
        SendMessageW(lv, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)uiicons::small());
        SendMessageW(lv, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);
        RECT rc;
        GetClientRect(lv, &rc);
        LVCOLUMNW col = {};
        col.mask = LVCF_WIDTH;
        col.cx = rc.right - GetSystemMetrics(SM_CXVSCROLL);
        SendMessageW(lv, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);
        fill_desktop(dlg);
        return TRUE;
    }
    case WM_THEMECHANGED:
        if (uiicons::sync()) InvalidateRect(GetDlgItem(dlg, IDC_DT_LIST), nullptr, TRUE);
        return FALSE;
    case WM_DRAWITEM:
        if (wp == IDC_DT_PREVIEW) { paint_pixels((const DRAWITEMSTRUCT*)lp, make_desktop_sample); return TRUE; }
        return FALSE;
    case WM_NOTIFY: {
        const NMHDR* nh = (const NMHDR*)lp;
        if (nh->idFrom == IDC_DT_LIST && nh->code == LVN_ITEMCHANGED && !filling) {
            const NMLISTVIEW* nl = (const NMLISTVIEW*)lp;
            if ((nl->uNewState & LVIS_SELECTED) && !(nl->uOldState & LVIS_SELECTED) && nl->iItem >= 0 && nl->iItem < (int)walls.size()) {
                const Wall& w = walls[(size_t)nl->iItem];
                if (w.value != cur.wallpaper || w.file != cur.wall_file) {
                    cur.wallpaper = w.value;
                    cur.wall_file = w.file;
                    EnableWindow(GetDlgItem(dlg, IDC_DT_MODE), cur.wallpaper != "none");
                    InvalidateRect(GetDlgItem(dlg, IDC_DT_PREVIEW), nullptr, FALSE);
                    changed(dlg);
                }
            }
            return TRUE;
        }
        return FALSE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_DT_BROWSE: browse(dlg); return TRUE;
        case IDC_DT_MODE:
        case IDC_DT_ICONS:
            if (HIWORD(wp) == CBN_SELCHANGE && !filling) {
                cur.mode = combo_value(dlg, IDC_DT_MODE, kModes);
                theme_key_command(dlg, LOWORD(wp));
                InvalidateRect(GetDlgItem(dlg, IDC_DT_PREVIEW), nullptr, FALSE);
                changed(dlg);
            }
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

INT_PTR CALLBACK appearance_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    INT_PTR r;
    if (common(dlg, msg, lp, r)) return r;
    switch (msg) {
    case WM_INITDIALOG:
        pages[2] = dlg;
        watch_settings(dlg);
        fill_appearance(dlg);
        return TRUE;
    case WM_COMMAND:
        if ((HIWORD(wp) == CBN_SELCHANGE || (HIWORD(wp) == BN_CLICKED && LOWORD(wp) == IDC_AP_SBCLASSIC)) && !filling) {
            cur.sbmode = combo_value(dlg, IDC_AP_SBMODE, kSbModes);
            cur.sbstack = combo_value(dlg, IDC_AP_SBSTACK, kSbStacks);
            cur.sbclassic = IsDlgButtonChecked(dlg, IDC_AP_SBCLASSIC) == BST_CHECKED ? "1" : "0";
            if (LOWORD(wp) == IDC_AP_SBMODE)
                SetDlgItemTextW(dlg, IDC_AP_SBMODE_SRC, (LPCWSTR)W(cur.sbmode != given.sbmode ? "Changed: Apply keeps it for every theme" : cur.sbmode_source).c_str());
            theme_key_command(dlg, LOWORD(wp));
            changed(dlg);
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

INT_PTR CALLBACK effects_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    INT_PTR r;
    if (common(dlg, msg, lp, r)) return r;
    switch (msg) {
    case WM_INITDIALOG:
        pages[3] = dlg;
        filling = true;
        CheckDlgButton(dlg, IDC_EF_SMOOTH, cur.smoothing != "off" ? BST_CHECKED : BST_UNCHECKED);
        fill_combo(dlg, IDC_EF_METHOD, kMethods, cur.smoothing == "cleartype" ? "cleartype" : "standard");
        EnableWindow(GetDlgItem(dlg, IDC_EF_METHOD), cur.smoothing != "off");
        filling = false;
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_EF_SMOOTH || (LOWORD(wp) == IDC_EF_METHOD && HIWORD(wp) == CBN_SELCHANGE)) {
            bool on = IsDlgButtonChecked(dlg, IDC_EF_SMOOTH) == BST_CHECKED;
            EnableWindow(GetDlgItem(dlg, IDC_EF_METHOD), on);
            cur.smoothing = on ? combo_value(dlg, IDC_EF_METHOD, kMethods) : "off";
            changed(dlg);
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

void usage(FILE* f)
{
    fputs("usage: flxp-settings [--tab themes|desktop|appearance|effects]\n"
          "Display Properties: the session theme (theme files, ~/.config/lhde/themes/NAME-modified.theme for a change)\n"
          "and the per-user settings (~/.config/lhde/settings.ini).\n", f);
}

} // namespace

int main(int argc, char** argv)
{
    int start = 0;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if ((a == "--tab" || a == "-t") && i + 1 < argc) {
            std::string t = argv[++i];
            start = t == "desktop" ? 1 : t == "appearance" ? 2 : t == "effects" ? 3 : 0;
        } else if (a.rfind("--tab=", 0) == 0) {
            std::string t = a.substr(6);
            start = t == "desktop" ? 1 : t == "appearance" ? 2 : t == "effects" ? 3 : 0;
        } else if (a == "--help" || a == "-h") { usage(stdout); return 0; }
        else { usage(stderr); return 2; }
    }
    shell::setup_fonts();
    shell::setup_theme();
    w32::init();
    committed = cur = given = load_values();

    static const struct { int id; DLGPROC proc; } defs[] = {
        { IDD_THEMES, themes_proc }, { IDD_DESKTOP, desktop_proc }, { IDD_APPEARANCE, appearance_proc }, { IDD_EFFECTS, effects_proc },
    };
    PROPSHEETPAGEW pg[4] = {};
    for (int i = 0; i < 4; i++) {
        pg[i].dwSize = sizeof pg[i];
        pg[i].dwFlags = PSP_DEFAULT;
        pg[i].hInstance = module();
        pg[i].pszTemplate = MAKEINTRESOURCEW(defs[i].id);
        pg[i].pfnDlgProc = defs[i].proc;
    }
    PROPSHEETHEADERW psh = {};
    psh.dwSize = sizeof psh;
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_NOCONTEXTHELP;
    psh.hInstance = module();
    psh.pszCaption = L"Display Properties";
    psh.nPages = 4;
    psh.nStartPage = (UINT)start;
    psh.ppsp = pg;
    PropertySheetW(&psh);
    return 0;
}
