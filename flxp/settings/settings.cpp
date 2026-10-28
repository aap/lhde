// flxp-settings: Display Properties (XP's desk.cpl) for the Longhorn session.
// A property sheet with Themes / Desktop / Appearance / Effects over the shared
// store (shell/settings.h, x11/SETTINGS.md). Nothing is written before OK or
// Apply; Apply writes settings.ini, sets _LH_THEME for a theme change and sends
// _LH_SETTINGS_CHANGED, and the dialog stays open (it follows the theme itself,
// as every flxp window does). Templates: rc/settings.rc.
#include "res/res.h"   // before the win32 headers: their macros clash with its names
#include "gdi/image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
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

// theme, wallpaper, mode, smoothing and the sidebar's stacking (a global key: lhwm's Win+S flips it) are global; icons, frame, kind, menu, comp
// and anim belong to the theme (store::theme_value, SETTINGS.md "Per-theme settings")
// Each per-theme value has a scope (This theme only / All themes / Theme defaults) and a
// source shown under it (store::Source, or flxp.ini).
enum { K_ICONS, K_FRAME, K_KIND, K_MENU, K_COMP, K_ANIM, K_SBMODE, K_SBCLASSIC, K_COUNT };
struct PerKey { const char* key; const char* flxp; int page; int value_id; };
const PerKey kKeys[K_COUNT] = {
    { "icons", "icons", 1, IDC_DT_ICONS },
    { "frame", "frame", 2, IDC_AP_FRAME },
    { "taskbar", "taskbar_kind", 2, IDC_AP_TASKBAR },
    { "start", "start_menu", 2, IDC_AP_START },
    { "composition", nullptr, 2, IDC_AP_COMP },
    { "animations", nullptr, 2, IDC_AP_ANIM },
    { "sidebar", nullptr, 2, IDC_AP_SBMODE },
    { "sidebar_with_classic", nullptr, 2, IDC_AP_SBCLASSIC },
};
int scope_id(int k) { return kKeys[k].value_id == IDC_DT_ICONS ? IDC_DT_ICONS_SC : kKeys[k].value_id + 10; }
int source_id(int k) { return kKeys[k].value_id == IDC_DT_ICONS ? IDC_DT_ICONS_SRC : kKeys[k].value_id + 20; }
enum { SCOPE_THEME, SCOPE_ALL, SCOPE_OWN };
const char* const kScopes[][2] = { { "This theme only", "theme" }, { "All themes", "all" }, { "Theme defaults", "own" } };

struct Values {
    std::string theme, wallpaper, mode, icons, frame, kind, menu, comp, anim, sbmode, sbstack, sbclassic, smoothing;
    int scope[K_COUNT] = {};
    bool dirty[K_COUNT] = {};          // value or scope touched since loaded
    std::string source[K_COUNT];       // what the line under the value says
    std::string& value(int k)
    {
        switch (k) {
        case K_ICONS: return icons;
        case K_FRAME: return frame;
        case K_KIND: return kind;
        case K_MENU: return menu;
        case K_COMP: return comp;
        case K_SBMODE: return sbmode;
        case K_SBCLASSIC: return sbclassic;
        default: return anim;
        }
    }
};
Values committed, cur;            // what the store holds / what the dialog shows
std::string registry;
std::vector<store::ThemeEntry> themes;
HWND pages[4];                    // Themes, Desktop, Appearance, Effects (once created)
bool filling;                     // controls being set from the state: no PSM_CHANGED

const char* const kModes[][2] = { { "Fill", "fill" }, { "Fit", "fit" }, { "Stretch", "stretch" }, { "Center", "center" }, { "Tile", "tile" } };
const char* const kIcons[][2] = { { "Longhorn", "longhorn" }, { "Windows XP", "xp" }, { "Windows 2000", "2k" } };
const char* const kFrames[][2] = { { "Automatic", "auto" }, { "Windows XP", "xp" }, { "Longhorn header", "longhorn" } };
const char* const kKinds[][2] = { { "Avalon (lux)", "avalon" }, { "Classic (flxp-taskbar)", "classic" }, { "None", "none" } };
const char* const kMenus[][2] = { { "flxp start panel", "flxp" }, { "lux start menu", "lux" } };
const char* const kComps[][2] = { { "Follow the theme", "follow" }, { "On", "on" }, { "Off", "off" } };
const char* const kAnims[][2] = { { "4074 plus", "4074plus" }, { "4074", "4074" }, { "Invented", "invented" }, { "5048", "5048" }, { "None", "none" } };
const char* const kSbModes[][2] = { { "Full", "full" }, { "Minimized", "minimized" }, { "Pinned", "pinned" } };
// SETTINGS.md [sidebar] stacking: lux's two labels
const char* const kSbStacks[][2] = { { "Sidebar on top of windows", "top" }, { "Sidebar under windows (desktop)", "bottom" } };
const char* const kMethods[][2] = { { "Standard", "standard" }, { "ClearType", "cleartype" } };

template<size_t N>
void fill_combo(HWND dlg, int id, const char* const (&items)[N][2], const std::string& value)
{
    HWND c = GetDlgItem(dlg, id);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    int sel = 0;
    for (size_t i = 0; i < N; i++) {
        std::u16string t = W(items[i][0]);
        SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)t.c_str());
        if (value == items[i][1]) sel = (int)i;
    }
    SendMessageW(c, CB_SETCURSEL, (WPARAM)sel, 0);
}

template<size_t N>
std::string combo_value(HWND dlg, int id, const char* const (&items)[N][2])
{
    int i = (int)SendDlgItemMessageW(dlg, id, CB_GETCURSEL, 0, 0);
    return i >= 0 && i < (int)N ? items[i][1] : items[0][1];
}

void changed(HWND page)
{
    if (!filling) SendMessageW(GetParent(page), PSM_CHANGED, (WPARAM)page, 0);
}

// What a flxp.ini key holds ("" = not set there): it wins over settings.ini, so
// the dialog shows it, and Apply moves it into the theme (store::set_theme_values)
std::string flxp_ini(const char* key)
{
    store::Ini f;
    f.load(shell::settings_path());
    return f.get("flxp", key);
}

std::string source_text(store::Source s, const std::string& theme)
{
    switch (s) {
    case store::Source::Override: return "Set for this theme";
    case store::Source::Preset: return "Set in this theme's file";
    case store::Source::AllThemes: return "Set for all themes";
    case store::Source::Theme: return "The theme's default (" + theme + ")";
    case store::Source::Global: return "The global setting (no theme default)";
    default: return "The built-in default";
    }
}

// with_classic as written: 1 or 0
std::string bool_value(const std::string& v) { return v == "1" || v == "true" || v == "yes" || v == "on" ? "1" : "0"; }

// The selected theme's own values, as the programs see them, and where each comes from
void load_theme_values(Values& v)
{
    for (int k = 0; k < K_COUNT; k++) {
        std::string f = kKeys[k].flxp ? flxp_ini(kKeys[k].flxp) : "";
        store::Source src = store::Source::Default;
        v.value(k) = f.empty() ? store::theme_value(v.theme, kKeys[k].key, registry, &src) : f;
        v.source[k] = f.empty() ? source_text(src, store::preset_base(v.theme).empty() ? v.theme : store::preset_base(v.theme))
                                : "flxp.ini sets it for every theme";
        v.scope[k] = f.empty() && src == store::Source::AllThemes ? SCOPE_ALL : SCOPE_THEME;
        v.dirty[k] = false;
    }
    if (v.menu == "lhshell") v.menu = "lux";
    else if (v.menu != "lux") v.menu = "flxp";
    v.sbclassic = bool_value(v.sbclassic);
}

Values load_values()
{
    Values v;
    // the root property wins for the running session (SETTINGS.md "Conflicts")
    v.theme = shell::session_theme().name;
    if (v.theme.empty()) v.theme = store::get("theme", "name");
    std::string sec = store::registry_section(registry, v.theme);   // an alias is its section
    if (!sec.empty()) v.theme = sec;
    v.sbstack = store::sidebar_stacking();
    v.wallpaper = shell::file_setting("wallpaper");
    v.mode = shell::file_setting("wallpaper_mode", "fill");
    v.smoothing = shell::file_setting("font_smoothing", "standard");
    if (v.smoothing == "0") v.smoothing = "off";
    load_theme_values(v);
    return v;
}

const store::ThemeEntry* entry(const std::string& name)
{
    for (const store::ThemeEntry& e : themes) if (e.name == name) return &e;
    return nullptr;
}

// The selected theme, resolved (registry section or preset)
bool resolve(const std::string& name, shell::SessionTheme& t)
{
    return !name.empty() && shell::lookup_session_theme(name, registry, t);
}

// ---- pixels ------------------------------------------------------------------------

struct Pixels {
    int w = 0, h = 0;
    std::vector<uint32_t> px;
};

// The theme's own background (x11/THEMES.md "Wallpapers": [override:NAME], the preset, a themes.ini key,
// wallpapers.ini), as the desktop takes it
bool theme_background(const std::string& name, shell::Wallpaper& w)
{
    shell::SessionTheme t;
    return resolve(name, t) && (shell::own_wallpaper(t, w) || shell::theme_wallpaper(t, w));
}

// What the desktop shows for a background (the theme's when `file` is empty), at w x h
// as a scaled-down screen
void desktop_thumbnail(const std::string& file_in, const std::string& mode_in, const std::string& theme, Pixels& out)
{
    std::string file = file_in, mode = mode_in;
    if (file == "none") file.clear();   // no picture: the background colour
    else if (file.empty()) {
        shell::Wallpaper w;
        if (theme_background(theme, w)) { file = w.file; mode = w.mode; }
        if (mode.empty()) mode = "fill";
    }
    static std::string last_file;
    static wallpaper::Image img;
    if (file != last_file) {
        img = wallpaper::Image();
        if (!file.empty()) wallpaper::load(file[0] == '~' ? shell::home_dir() + file.substr(1) : file, img);
        last_file = file;
    }
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    if (sw <= 0 || sh <= 0) { sw = 1024; sh = 768; }
    wallpaper::Image screen;
    screen.width = sw; screen.height = sh;
    screen.px.resize((size_t)sw * sh);
    wallpaper::compose(img, wallpaper::mode_from(mode), 0x3a6ea5, screen.px.data(), sw, sh, 0, 0, sw, sh);
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
    desktop_thumbnail(name == cur.theme ? cur.wallpaper : "", name == cur.theme ? cur.mode : "", name, out);
    shell::SessionTheme st;
    if (!resolve(name, st)) return;
    std::string path = st.path("msstyles");
    ut_theme* t = ut_open(path.c_str(), st.color.empty() ? nullptr : st.color.c_str(), st.size.empty() ? nullptr : st.size.c_str());
    if (!t && !st.msstyles.empty()) t = ut_open(st.msstyles.c_str(), nullptr, nullptr);
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
    std::string title = entry(name) ? entry(name)->title : name;
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
void make_desktop_sample(Pixels& p) { desktop_thumbnail(cur.wallpaper, cur.mode, cur.theme, p); }

// ---- apply -------------------------------------------------------------------------

void remember_wallpaper(const std::string& path);

void fill_desktop(HWND dlg);
void fill_appearance(HWND dlg);

void apply()
{
    std::vector<store::Edit> edits;
    std::string hint;
    int sections = 0;
    auto put = [&](const char* sec, const char* key, const std::string& now, const std::string& was) {
        if (now == was) return;
        edits.push_back(store::Edit{ sec, key, now });
        if (hint != sec) { hint = sec; sections++; }
    };
    put("desktop", "wallpaper", cur.wallpaper, committed.wallpaper);
    put("desktop", "mode", cur.mode, committed.mode);
    put("fonts", "smoothing", cur.smoothing, committed.smoothing);
    // the theme's own values: whatever differs from what it gives now (or what flxp.ini forces)
    // "All themes" / "Theme defaults" only when touched (they rewrite every theme's choice)
    std::vector<store::KeyValue> kv, all;
    for (int k = 0; k < K_COUNT; k++) {
        const std::string& now = cur.value(k);
        if (cur.scope[k] == SCOPE_ALL) { if (cur.dirty[k]) all.push_back(store::KeyValue{ kKeys[k].key, now }); }
        else if (cur.scope[k] == SCOPE_OWN) { if (cur.dirty[k]) all.push_back(store::KeyValue{ kKeys[k].key, "" }); }
        else if (now != store::theme_value(cur.theme, kKeys[k].key, registry) || (kKeys[k].flxp && !flxp_ini(kKeys[k].flxp).empty()))
            kv.push_back(store::KeyValue{ kKeys[k].key, now });
    }
    bool theme = cur.theme != committed.theme;
    bool stack = cur.sbstack != committed.sbstack;
    if (stack && hint != "sidebar") { hint = "sidebar"; sections++; }
    if (edits.empty() && kv.empty() && all.empty() && !theme && !stack) return;
    if ((!kv.empty() || !all.empty()) && hint != "theme") { hint = "theme"; sections++; }   // the themes' values
    bool ok = edits.empty() || store::set_shared(edits);
    if (stack) ok = store::set_sidebar_stacking(cur.sbstack) && ok;   // global only
    ok = store::set_all_themes_values(all) && ok;
    ok = store::set_theme_values(cur.theme, kv, registry) && ok;
    if (!ok)
        filedlg::message_box(pages[0], "Display Properties", "Cannot write " + store::path() + ".", MB_OK | MB_ICONEXCLAMATION);
    // the desktop's session override (_FLXP_WALLPAPER, Set as Desktop Background) follows the dialog
    if (cur.wallpaper != committed.wallpaper || cur.mode != committed.mode) {
        FlxpSetRootString("_FLXP_WALLPAPER", cur.wallpaper.empty() ? "" : cur.wallpaper == "none" ? "none" : (cur.mode + " " + cur.wallpaper).c_str());
        remember_wallpaper(cur.wallpaper);
    }
    // order (SETTINGS.md): settings.ini written, _LH_THEME (once), then one message
    if (theme) store::apply_theme(cur.theme, sections ? "" : "theme");
    else store::notify(sections > 1 ? "" : hint);
    // show where the values come from now
    load_theme_values(cur);
    if (pages[1]) fill_desktop(pages[1]);
    if (pages[2]) fill_appearance(pages[2]);
    committed = cur;
}

// ---- pages -------------------------------------------------------------------------

// ---- the Desktop tab's background list (XP's: names, the preview above) --------------

struct Wall { std::string label, value; };   // value: a path, "" = the theme's, "none"
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
    if (path.empty() || path == "none") return;
    std::string dir = shell::dir_of(expand(path));
    for (const std::string& d : wallpaper_dirs()) if (d == dir) return;   // listed anyway
    std::vector<std::string> r = recent_wallpapers();
    std::string v = path;
    int n = 1;
    for (const std::string& p : r)
        if (p != path && n < 10) { v += ";" + p; n++; }
    shell::write_setting("wallpapers", v);
}

void build_walls()
{
    walls.clear();
    walls.push_back(Wall{ "(None)", "none" });
    shell::Wallpaper tb;
    std::string tw = theme_background(cur.theme, tb) ? tb.file : "";
    walls.push_back(Wall{ tw.empty() ? "(Theme background)" : stem(tw) + " (theme background)", "" });
    auto add = [&](const std::string& p) {
        for (const Wall& w : walls) if (w.value == p) return;
        walls.push_back(Wall{ stem(p), p });
    };
    struct stat st;
    for (const std::string& p : recent_wallpapers()) if (stat(expand(p).c_str(), &st) == 0) add(p);
    if (!cur.wallpaper.empty() && cur.wallpaper != "none") add(cur.wallpaper);
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
            bool have = false;
            for (const Wall& w : walls) have = have || w.value == p;
            if (!have) walls.push_back(Wall{ by_build ? stem(p) + " (" + shell::base_of(d) + ")" : stem(p), p });
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
        it.iImage = walls[i].value.empty() || walls[i].value == "none" ? I_IMAGENONE : image;
        SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&it);
        if (walls[i].value == cur.wallpaper) sel = (int)i;
    }
    LVITEMW st = {};
    st.stateMask = st.state = LVIS_SELECTED | LVIS_FOCUSED;
    SendMessageW(lv, LVM_SETITEMSTATE, (WPARAM)sel, (LPARAM)&st);
    SendMessageW(lv, LVM_ENSUREVISIBLE, (WPARAM)sel, FALSE);
    SendMessageW(lv, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(lv, nullptr, TRUE);
}

void fill_scope(HWND dlg, int k)
{
    fill_combo(dlg, scope_id(k), kScopes, kScopes[cur.scope[k]][1]);
    SetDlgItemTextW(dlg, source_id(k), (LPCWSTR)W(cur.source[k]).c_str());
}

void fill_desktop(HWND dlg)
{
    filling = true;
    fill_walls(dlg);
    fill_combo(dlg, IDC_DT_MODE, kModes, cur.mode);
    EnableWindow(GetDlgItem(dlg, IDC_DT_MODE), cur.wallpaper != "none");
    fill_combo(dlg, IDC_DT_ICONS, kIcons, cur.icons);
    fill_scope(dlg, K_ICONS);
    filling = false;
    InvalidateRect(GetDlgItem(dlg, IDC_DT_PREVIEW), nullptr, FALSE);
}

// a per-theme value into its control
void show_value(HWND dlg, int k, const std::string& v)
{
    switch (k) {
    case K_ICONS: fill_combo(dlg, IDC_DT_ICONS, kIcons, v); break;
    case K_FRAME: fill_combo(dlg, IDC_AP_FRAME, kFrames, v.empty() ? "auto" : v); break;
    case K_KIND: fill_combo(dlg, IDC_AP_TASKBAR, kKinds, v); break;
    case K_MENU: fill_combo(dlg, IDC_AP_START, kMenus, v); break;
    case K_COMP: fill_combo(dlg, IDC_AP_COMP, kComps, v); break;
    case K_ANIM: fill_combo(dlg, IDC_AP_ANIM, kAnims, v); break;
    case K_SBMODE: fill_combo(dlg, IDC_AP_SBMODE, kSbModes, v); break;
    case K_SBCLASSIC: CheckDlgButton(dlg, IDC_AP_SBCLASSIC, v == "1" ? BST_CHECKED : BST_UNCHECKED); break;
    }
}

void fill_appearance(HWND dlg)
{
    filling = true;
    for (int k = K_FRAME; k < K_COUNT; k++) { show_value(dlg, k, cur.value(k)); fill_scope(dlg, k); }
    fill_combo(dlg, IDC_AP_SBSTACK, kSbStacks, cur.sbstack);
    filling = false;
}

// a value or scope combo of a per-theme key changed; "Theme defaults" shows what the themes give
bool per_theme_command(HWND dlg, int id)
{
    for (int k = 0; k < K_COUNT; k++) {
        if (id != kKeys[k].value_id && id != scope_id(k)) continue;
        cur.scope[k] = (int)SendDlgItemMessageW(dlg, scope_id(k), CB_GETCURSEL, 0, 0);
        if (id == scope_id(k) && cur.scope[k] == SCOPE_OWN) {
            std::string d = store::theme_default(cur.theme, kKeys[k].key, registry, true);
            if (k == K_MENU) d = d == "lux" ? "lux" : "flxp";
            if (k == K_SBCLASSIC) d = bool_value(d);
            cur.value(k) = d;
            filling = true;
            show_value(dlg, k, d);
            filling = false;
        }
        cur.dirty[k] = true;
        cur.source[k] = "Changed: Apply stores it";
        SetDlgItemTextW(dlg, source_id(k), (LPCWSTR)W(cur.source[k]).c_str());
        return true;
    }
    return false;
}

void fill_theme_list(HWND dlg)
{
    themes = store::theme_list(registry);
    HWND c = GetDlgItem(dlg, IDC_TH_LIST);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    int sel = -1;
    for (size_t i = 0; i < themes.size(); i++) {
        std::string t = themes[i].title + (themes[i].preset ? " (custom)" : "");
        std::u16string w = W(t);
        SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)w.c_str());
        if (themes[i].name == cur.theme) sel = (int)i;
    }
    if (sel < 0 && !cur.theme.empty()) {   // not in the list (an alias): show the name
        std::u16string w = W(cur.theme);
        sel = (int)SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)w.c_str());
        themes.push_back(store::ThemeEntry{ cur.theme, cur.theme, false });
    }
    SendMessageW(c, CB_SETCURSEL, (WPARAM)sel, 0);
    const store::ThemeEntry* e = entry(cur.theme);
    EnableWindow(GetDlgItem(dlg, IDC_TH_DELETE), e && e->preset);
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

// Save As...: a preset from the dialog's current state (SETTINGS.md themes/NAME.theme)
void save_preset(HWND dlg)
{
    const store::ThemeEntry* e = entry(cur.theme);
    std::string name = e ? e->title + " (custom)" : "My Theme";
    if (DialogBoxParamW(module(), MAKEINTRESOURCEW(IDD_SAVEAS), GetParent(dlg), saveas_proc, (LPARAM)&name) != IDOK) return;
    std::string file;
    for (char c : name) file += (c == '/' || c == '\\') ? '-' : c;
    while (!file.empty() && (file[0] == '.' || file[0] == ' ')) file.erase(0, 1);
    if (store::preset_path(file).empty()) {
        filedlg::message_box(GetParent(dlg), "Save Theme As", "\"" + name + "\" cannot be used as a theme name.", MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    for (const store::ThemeEntry& t : themes)
        if (!t.preset && t.name == file) {
            filedlg::message_box(GetParent(dlg), "Save Theme As", "\"" + file + "\" is a built-in theme.", MB_OK | MB_ICONEXCLAMATION);
            return;
        }
    store::Ini p;
    std::string base = cur.theme;
    if (e && e->preset) {   // a preset of a preset: its base and overrides carried over
        store::read_preset(cur.theme, p);
        base = p.get("Theme", "base");
    } else {
        p.lines = { "; a Longhorn theme preset (x11/SETTINGS.md), written by Display Properties" };
    }
    p.set("Theme", "name", name);
    p.set("Theme", "base", base);
    // only what differs from the base: the base's own defaults keep applying to the rest
    struct { const char* key; const char* sec; const char* pkey; const std::string& v; } own[] = {
        { "icons", "Desktop", "icons", cur.icons }, { "frame", "Explorer", "frame", cur.frame },
        { "taskbar", "Shell", "taskbar", cur.kind }, { "start", "Shell", "start", cur.menu },
        { "composition", "Composition", "composition", cur.comp }, { "animations", "Composition", "animations", cur.anim },
        { "sidebar", "Shell", "sidebar", cur.sbmode },
        { "sidebar_with_classic", "Shell", "sidebar_with_classic", cur.sbclassic },
    };
    for (auto& o : own) {
        if (o.v == store::theme_default(base, o.key, registry)) p.remove(o.sec, o.pkey);
        else p.set(o.sec, o.pkey, o.v);
    }
    if (!cur.wallpaper.empty()) { p.set("Desktop", "wallpaper", cur.wallpaper); p.set("Desktop", "mode", cur.mode); }
    if (!p.save(store::preset_path(file))) {
        filedlg::message_box(GetParent(dlg), "Save Theme As", "Cannot write " + store::preset_path(file) + ".", MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    cur.theme = file;
    fill_theme_list(dlg);
    InvalidateRect(GetDlgItem(dlg, IDC_TH_PREVIEW), nullptr, FALSE);
    changed(dlg);
}

void delete_preset(HWND dlg)
{
    const store::ThemeEntry* e = entry(cur.theme);
    if (!e || !e->preset) return;
    if (filedlg::message_box(GetParent(dlg), "Delete Theme", "Delete the theme \"" + e->title + "\"?",
                             MB_YESNO | MB_ICONEXCLAMATION) != IDYES) return;
    store::Ini p;
    store::read_preset(cur.theme, p);
    std::string base = p.get("Theme", "base");
    store::delete_preset(cur.theme);
    if (!base.empty()) cur.theme = base;
    fill_theme_list(dlg);
    InvalidateRect(GetDlgItem(dlg, IDC_TH_PREVIEW), nullptr, FALSE);
    changed(dlg);
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

// _LH_SETTINGS_CHANGED while the dialog is open: the sidebar's stacking is flipped by Win+S (lhwm, saved by lux),
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
                    cur.theme = themes[(size_t)i].name;
                    // a theme brings its background, as on Windows (the Desktop tab can still change it),
                    // and its own icons, frame, taskbar, start menu, composition and animations
                    cur.wallpaper.clear();
                    load_theme_values(cur);
                    if (pages[1]) fill_desktop(pages[1]);
                    if (pages[2]) fill_appearance(pages[2]);
                    EnableWindow(GetDlgItem(dlg, IDC_TH_DELETE), themes[(size_t)i].preset);
                    InvalidateRect(GetDlgItem(dlg, IDC_TH_PREVIEW), nullptr, FALSE);
                    changed(dlg);
                }
            }
            return TRUE;
        case IDC_TH_SAVE: save_preset(dlg); return TRUE;
        case IDC_TH_DELETE: delete_preset(dlg); return TRUE;
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
    if (!cur.wallpaper.empty() && cur.wallpaper != "none") o.initial_dir = shell::dir_of(expand(cur.wallpaper));
    else o.initial_dir = shell::home_dir() + "/Pictures";
    filedlg::Result r = filedlg::run(GetParent(dlg), o);
    // the sheet was disabled while the dialog ran: repaint its controls enabled
    RedrawWindow(GetParent(dlg), nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    if (!r.ok || r.paths.empty()) return;
    cur.wallpaper = r.paths[0];
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
            if ((nl->uNewState & LVIS_SELECTED) && !(nl->uOldState & LVIS_SELECTED) && nl->iItem >= 0 && nl->iItem < (int)walls.size()
                && walls[(size_t)nl->iItem].value != cur.wallpaper) {
                cur.wallpaper = walls[(size_t)nl->iItem].value;
                EnableWindow(GetDlgItem(dlg, IDC_DT_MODE), cur.wallpaper != "none");
                InvalidateRect(GetDlgItem(dlg, IDC_DT_PREVIEW), nullptr, FALSE);
                changed(dlg);
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
        case IDC_DT_ICONS_SC:
            if (HIWORD(wp) == CBN_SELCHANGE && !filling) {
                cur.mode = combo_value(dlg, IDC_DT_MODE, kModes);
                cur.icons = combo_value(dlg, IDC_DT_ICONS, kIcons);
                per_theme_command(dlg, LOWORD(wp));
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
            cur.frame = combo_value(dlg, IDC_AP_FRAME, kFrames);
            cur.kind = combo_value(dlg, IDC_AP_TASKBAR, kKinds);
            cur.menu = combo_value(dlg, IDC_AP_START, kMenus);
            cur.comp = combo_value(dlg, IDC_AP_COMP, kComps);
            cur.anim = combo_value(dlg, IDC_AP_ANIM, kAnims);
            per_theme_command(dlg, LOWORD(wp));
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
          "Display Properties: the Longhorn session's shared settings ($XDG_CONFIG_HOME/longhorn/settings.ini).\n", f);
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
    registry = store::registry_path();
    committed = cur = load_values();

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
