// flxp-settings: Display Properties (XP's desk.cpl / themeui) for the lhde session.
// A property sheet with Themes / Desktop / Appearance / Shell over liblhde's theme files, the current layer and the
// settings store (docs/THEME-FORMAT.md sections 2a and 3, docs/SETTINGS.md), as Windows has it:
//   Themes      theme files only (the shipped ones and the user's) whose visual style is imported (the selected one
//               always), and "NAME (Modified)" while the current layer has keys; Save As writes a theme file, Delete
//               removes one of the user's
//   Desktop     the background (Desktop.wallpaper, Desktop.mode) and the icon set (Desktop.icons)
//   Appearance  XP's: "Windows and buttons" = the visual style (the styles some theme file names, Windows Classic style,
//               and the selected style when it is neither),
//               "Color scheme" (the style's colours, or a classic scheme), "Font size"; Effects... (font smoothing),
//               Advanced (classic: the per-item editor writing Colors.* / Metrics.*)
//   Shell       the Longhorn extras: Explorer chrome, navigation bar and its size, taskbar, start menu, window frames,
//               composition, animations; the sidebar (per user)
// Nothing is written before OK or Apply. Apply selects the theme (lhde_theme_select: another theme's layer dropped),
// writes the pending current layer (settings.ini [current], lhde_current_set) and the per-user keys, sets _LH_THEME for a
// theme change and sends _LH_SETTINGS_CHANGED; no theme file is written except by Save As. Templates: flxp/rc/settings.rc.
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
#include <fontconfig/fontconfig.h>
#include <algorithm>
#include <map>
#include <set>
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
using Layer = std::map<std::string, std::string>;

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

void set_text(HWND dlg, int id, const std::string& s) { SetDlgItemTextW(dlg, id, (LPCWSTR)W(s).c_str()); }

// ---- state -------------------------------------------------------------------------

// The dialog's state: the theme selected and the current layer (theme keys on top of it, "Section.key"), both pending
// until Apply; the per-user settings.ini keys (sidebar, font smoothing, menu shadows). rec is what the session will look like (the theme
// with the pending layer, lhde_theme_resolve_with), files the theme as its files give it (what a key "returns to").
struct State {
    std::string theme;
    Layer layer;
    std::string sbmode, sbstack, sbclassic, smoothing;
    std::string shadows;          // [effects] menu_shadows: "1" | "0"
    std::string cues;             // [effects] keyboard_cues: "0" (hide the underlines until Alt) | "1"
};
State committed, cur;
std::string stash_theme;          // the "(Modified)" state while another theme is picked (XP: pick it again to get it back)
Layer stash_layer;
shell::SessionTheme rec, files;
std::vector<store::ThemeEntry> themes;
HWND pages[4];                    // Themes, Desktop, Appearance, Shell (once created)
bool filling;                     // controls being set from the state: no PSM_CHANGED

void recompute()
{
    if (!shell::resolve_theme_with(cur.theme, cur.layer, rec)) rec = shell::SessionTheme();
    if (!shell::resolve_theme_files(cur.theme, files)) files = shell::SessionTheme();
}

std::string value(const std::string& key) { return rec.value(key); }

// a theme key changed in the dialog: into the pending layer, or out of it when the theme's files give that value
void set_key(const std::string& key, const std::string& v)
{
    auto w = files.why.find(key);
    bool fallback = w != files.why.end() && w->second.compare(0, 9, "fallback:") == 0;
    if (files.value(key) == v && !fallback && files.keys.count(key)) cur.layer.erase(key);
    else cur.layer[key] = v;
}

void layer_from_store(State& s)
{
    s.layer.clear();
    for (const store::KeyValue& kv : store::current_layer()) s.layer[kv.key] = kv.value;
}


State load_state()
{
    State s;
    // the root property wins for the running session (SETTINGS.md "Conflicts")
    s.theme = store::get("theme", "name");
    if (s.theme.empty()) s.theme = shell::session_theme().name;
    std::string canon;
    if (shell::lookup_theme(s.theme, canon)) s.theme = canon;   // an alias is its theme
    layer_from_store(s);
    s.sbmode = store::get("sidebar", "mode");
    s.sbstack = store::sidebar_stacking();
    s.sbclassic = store::flag("sidebar", "with_classic", false) ? "1" : "0";
    s.smoothing = shell::font_smoothing();
    if (s.smoothing.empty()) s.smoothing = "standard";
    if (s.smoothing == "0") s.smoothing = "off";
    s.shadows = store::flag("effects", "menu_shadows", true) ? "1" : "0";
    s.cues = store::flag("effects", "keyboard_cues", false) ? "1" : "0";
    return s;
}

const store::ThemeEntry* entry(const std::string& name)
{
    for (const store::ThemeEntry& e : themes) if (e.name == name) return &e;
    return nullptr;
}

std::string theme_title(const std::string& name)
{
    const store::ThemeEntry* e = entry(name);
    if (e && !e->title.empty()) return e->title;
    shell::SessionTheme st;
    return shell::resolve_theme_files(name, st) ? st.title() : name;
}

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
std::string source_text(const std::string& key)
{
    auto it = rec.why.find(key);
    std::string why = it == rec.why.end() ? "" : it->second;
    if (why.compare(0, 8, "current:") == 0) {
        bool pending = cur.layer.count(key) && (!committed.layer.count(key) || committed.layer.at(key) != cur.layer.at(key));
        return pending ? "Changed: Apply keeps it in \"" + theme_title(cur.theme) + " (Modified)\"" : "Your change (Save As on the Themes tab keeps it)";
    }
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
    if (why.compare(0, 9, "settings:") == 0) return "Switched off for every theme (Show the wallpaper)";
    if (why.compare(0, 7, "scheme ") == 0) return "From the scheme";
    return why;
}

// ---- combo helpers -----------------------------------------------------------------

struct Item { std::string label, value; };

// a combo with the items; a value not among them is added as it is (a theme may name an icon set of its own)
void fill_list(HWND dlg, int id, std::vector<Item>& items, const std::string& value)
{
    HWND c = GetDlgItem(dlg, id);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    int sel = -1;
    for (size_t i = 0; i < items.size(); i++) if (items[i].value == value) sel = (int)i;
    if (sel < 0 && !value.empty()) { items.push_back(Item{ value, value }); sel = (int)items.size() - 1; }
    for (const Item& it : items) { std::u16string t = W(it.label); SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)t.c_str()); }
    SendMessageW(c, CB_SETCURSEL, (WPARAM)(sel < 0 ? 0 : sel), 0);
}

std::string list_value(HWND dlg, int id, const std::vector<Item>& items, const std::string& was)
{
    int i = (int)SendDlgItemMessageW(dlg, id, CB_GETCURSEL, 0, 0);
    return i >= 0 && i < (int)items.size() ? items[(size_t)i].value : was;
}

std::vector<Item> items(std::initializer_list<Item> l) { return std::vector<Item>(l); }

// ---- visual styles: display names, colours, sizes ------------------------------------

// themes.ini (a v3 style's TEXTFILE THEMES_INI, UTF-16): [SECTION] KEY's value
std::string themes_ini(ut_theme* t, const std::string& section, const std::string& key)
{
    const void* data;
    size_t size;
    if (!ut_get_resource(t, "TEXTFILE", "THEMES_INI", &data, &size) || !data) return "";
    const unsigned char* d = (const unsigned char*)data;
    std::string text;
    for (size_t i = 0; i + 1 < size; i += 2) { unsigned c = d[i] | (d[i + 1] << 8); text += c < 0x80 ? (char)c : '?'; }
    std::string want = "[" + section + "]";
    size_t p = 0;
    bool in = false;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string l = text.substr(p, e - p);
        p = e + 1;
        while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
        size_t a = l.find_first_not_of(" \t");
        if (a == std::string::npos || l[a] == ';') continue;
        l = l.substr(a);
        if (l[0] == '[') { in = !strcasecmp(l.c_str(), want.c_str()); continue; }
        if (!in) continue;
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string k = l.substr(0, eq);
        while (!k.empty() && k.back() == ' ') k.pop_back();
        if (strcasecmp(k.c_str(), key.c_str())) continue;
        std::string v = l.substr(eq + 1);
        size_t b = v.find_first_not_of(" \t");
        v = b == std::string::npos ? "" : v.substr(b);
        size_t sc = v.find(';');
        if (sc != std::string::npos) v.erase(sc);
        while (!v.empty() && (unsigned char)v.back() <= ' ') v.pop_back();
        return v;
    }
    return "";
}

// "NormalColor" -> XP's "Default (blue)", "LargeFonts" -> "Large Fonts", else the CamelCase split
std::string pretty(const std::string& n)
{
    static const char* const known[][2] = {
        { "NormalColor", "Default (blue)" }, { "HomeStead", "Olive Green" }, { "Metallic", "Silver" },
        { "NormalSize", "Normal" }, { "LargeFonts", "Large Fonts" }, { "ExtraLargeFonts", "Extra Large Fonts" },
        { "Large", "Large Fonts" }, { "ExtraLarge", "Extra Large Fonts" } };
    for (auto& k : known) if (n == k[0]) return k[1];
    std::string out;
    for (size_t i = 0; i < n.size(); i++) {
        if (i && isupper((unsigned char)n[i]) && islower((unsigned char)n[i - 1])) out += ' ';
        out += n[i];
    }
    return out;
}

struct Style { std::string name, path, label; bool named = false; };
std::vector<Style> styles;        // "classic" first, then every style in styles/ by label; named: a theme file uses it

std::string style_label(const std::string& name, const std::string& path)
{
    std::string label;
    if (ut_theme* t = ut_open(path.c_str(), nullptr, nullptr)) {
        char buf[256] = "";
        int p = ut_prop(t, "DISPLAYNAME");
        if (p && ut_get_string(t, "documentation", 0, 0, p, buf, sizeof buf) && buf[0]) label = buf;
        if (label.empty()) label = themes_ini(t, "documentation", "DisplayName");
        ut_close(t);
        while (!label.empty() && (unsigned char)label.back() <= ' ') label.pop_back();   // Zune's ends in a tab
    }
    if (label.empty() || label[0] == '@') {   // the file name: luna -> Luna
        label = name;
        if (!label.empty()) label[0] = (char)toupper((unsigned char)label[0]);
    }
    // the build of a build-qualified name (jade4074 -> "Jade (4074)")
    std::string digits;
    for (size_t i = 0; i < name.size(); i++) {
        size_t j = i;
        while (j < name.size() && isdigit((unsigned char)name[j])) j++;
        if (j - i == 4) { digits = name.substr(i, 4); break; }
        if (j > i) i = j;
    }
    if (!digits.empty() && label.find(digits) == std::string::npos) label += " (" + digits + ")";
    return label;
}

void style_one(const char* name, const char* path, void*) { styles.push_back(Style{ name, path, "" }); }

void load_styles()
{
    if (!styles.empty()) return;
    lhde_style_list(style_one, nullptr);
    std::map<std::string, int> count;
    for (Style& s : styles) { s.label = style_label(s.name, s.path); count[s.label]++; }
    for (Style& s : styles) if (count[s.label] > 1) s.label += " [" + s.name + "]";   // two copies of one style
    std::sort(styles.begin(), styles.end(), [](const Style& a, const Style& b) { return strcasecmp(a.label.c_str(), b.label.c_str()) < 0; });
    styles.insert(styles.begin(), Style{ "classic", "", "Windows Classic style", true });
    // the curated set: the styles some theme file (shipped or the user's) resolves to; every other imported file is
    // offered only while it is the one selected
    std::set<std::string> named;
    for (const store::ThemeEntry& e : store::theme_files())
        if (lhde_theme* t = lhde_theme_resolve_flags(e.name.c_str(), LHDE_RESOLVE_FILES, nullptr, 0)) {
            if (const char* v = lhde_theme_get(t, "VisualStyle.style")) named.insert(v);
            lhde_theme_free(t);
        }
    for (Style& s : styles) {
        if (named.count(s.name)) s.named = true;
        // a style dropped in by hand (lhde-import --style: the Windows tree's local/themes) is the user's own choice: offered too
        char real[PATH_MAX];
        if (!s.named && !s.path.empty() && realpath(s.path.c_str(), real) && strstr(real, "/local/themes/")) s.named = true;
    }
}

const Style* style_named(const std::string& n)
{
    for (const Style& s : styles) if (s.name == n) return &s;
    return nullptr;
}

// a style's colour schemes and sizes, value = the style's name for it
void style_variants(const std::string& path, std::vector<Item>& colors, std::vector<Item>& sizes)
{
    colors.clear(); sizes.clear();
    ut_theme* t = path.empty() ? nullptr : ut_open(path.c_str(), nullptr, nullptr);
    if (!t) return;
    const char* names[64];
    int n = ut_list_colors(t, names, 64);
    for (int i = 0; i < n && i < 64; i++) {
        std::string d = themes_ini(t, std::string("ColorScheme.") + names[i], "DisplayName");
        colors.push_back(Item{ d.empty() ? pretty(names[i]) : d, names[i] });
    }
    n = ut_list_sizes(t, names, 64);
    for (int i = 0; i < n && i < 64; i++) {
        std::string d = themes_ini(t, std::string("Size.") + names[i], "DisplayName");
        sizes.push_back(Item{ d.empty() ? pretty(names[i]) : d, names[i] });
    }
    ut_close(t);
}

// the classic schemes: Windows Standard (imported, else the built-in colours), then every imported one by title
struct Scheme { std::string name, title, path; };
std::vector<Scheme> schemes;
void scheme_one(const char* n, const char* t, const char* p, void*) { schemes.push_back(Scheme{ n, t, p }); }
void load_schemes()
{
    schemes.clear();
    lhde_scheme_list(scheme_one, nullptr);
}
std::string standard_scheme()
{
    for (const Scheme& s : schemes) if (s.name == "windows-standard") return s.name;
    return "none";
}
std::vector<Item> scheme_items()
{
    std::vector<Item> out;
    bool std_imported = standard_scheme() != "none";
    if (!std_imported) out.push_back(Item{ "Windows Standard", "none" });
    for (const Scheme& s : schemes) out.push_back(Item{ s.title, s.name });
    return out;
}
std::vector<Item> scheme_sizes(const std::string& scheme)
{
    std::vector<Item> out;
    for (const Scheme& s : schemes) {
        if (s.name != scheme) continue;
        lhde_ini* ini = lhde_ini_load(s.path.c_str());
        char buf[256] = "";
        lhde_ini_get(ini, "Scheme", "sizes", buf, sizeof buf);
        lhde_ini_free(ini);
        std::string v = buf;
        size_t i = 0;
        while (i < v.size()) {
            size_t j = v.find(',', i);
            if (j == std::string::npos) j = v.size();
            std::string x = v.substr(i, j - i);
            while (!x.empty() && x[0] == ' ') x.erase(0, 1);
            while (!x.empty() && x.back() == ' ') x.pop_back();
            if (!x.empty()) out.push_back(Item{ x, x });
            i = j + 1;
        }
    }
    if (out.empty()) out.push_back(Item{ "Normal", "Normal" });
    return out;
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

void record_thumbnail(const shell::SessionTheme& st, Pixels& out)
{
    std::string file = st.value("Desktop.wallpaper") == "none" || shell::wallpaper_hidden() ? "" : st.value("path.wallpaper");
    shell::Wallpaper w;
    shell::theme_wallpaper(st, w);   // the colour: Desktop.color, or the classic look's Colors.background
    desktop_thumbnail(file, st.value("Desktop.mode"), w.color, out);
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

// A window drawn by libuxtheme from the record's style (caption, frame, caption buttons, a push button, a scroll bar) in
// L,T,R,B. The style is read directly (also a Longhorn-compiled one).
void style_sample(const shell::SessionTheme& st, Pixels& out, int L, int T, int R, int B, const char* caption = nullptr)
{
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
    std::string title = caption ? caption : st.title();
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
    if (by2 > ct + 28) {
        bg("Button", BP_PUSHBUTTON, PBS_DEFAULTED, bx, by2, bx + bw, by2 + bh);
        ut_rect btr = { bx, by2, bx + bw, by2 + bh };
        ut_draw_text(t, "Button", BP_PUSHBUTTON, PBS_DEFAULTED, px, W, H, stride, "OK", 0x25, &btr);
    }
    ut_close(t);
}

// ---- the classic look (docs/THEME-FORMAT.md section 2a) -------------------------------

// Windows Standard: the built-in classic colours (wux's table) and metrics, under the record's Colors.* / Metrics.*
const COLORREF kStandard[31] = {
    RGB(212, 208, 200), RGB(58, 110, 165), RGB(10, 36, 106), RGB(128, 128, 128), RGB(212, 208, 200), RGB(255, 255, 255),
    RGB(0, 0, 0), RGB(0, 0, 0), RGB(0, 0, 0), RGB(255, 255, 255), RGB(212, 208, 200), RGB(212, 208, 200),
    RGB(128, 128, 128), RGB(10, 36, 106), RGB(255, 255, 255), RGB(212, 208, 200), RGB(128, 128, 128), RGB(128, 128, 128),
    RGB(0, 0, 0), RGB(212, 208, 200), RGB(255, 255, 255), RGB(64, 64, 64), RGB(212, 208, 200), RGB(0, 0, 0),
    RGB(255, 255, 225), RGB(181, 181, 181), RGB(0, 0, 128), RGB(166, 202, 240), RGB(192, 192, 192), RGB(10, 36, 106),
    RGB(212, 208, 200) };
const char* const kStdMetrics[][2] = {
    { "borderwidth", "1" }, { "scrollwidth", "16" }, { "scrollheight", "16" }, { "captionwidth", "18" }, { "captionheight", "18" },
    { "smcaptionwidth", "12" }, { "smcaptionheight", "12" }, { "menuwidth", "18" }, { "menuheight", "18" },
    { "captionfont", "Tahoma, -11, bold" }, { "smcaptionfont", "Tahoma, -11, bold" }, { "menufont", "Tahoma, -11" },
    { "statusfont", "Tahoma, -11" }, { "messagefont", "Tahoma, -11" }, { "icontitlefont", "Tahoma, -11" } };

int color_index(const std::string& n)
{
    for (int i = 0; lhde_color_name(i); i++) if (n == lhde_color_name(i)) return i;
    return -1;
}

COLORREF look_color(const shell::SessionTheme& st, const std::string& name)
{
    int i = color_index(name);
    std::string v = st.value("Colors." + name);
    int r, g, b;
    if (!v.empty() && sscanf(v.c_str(), "%d %d %d", &r, &g, &b) == 3) return RGB(r, g, b);
    return i >= 0 && i < 31 ? kStandard[i] : 0;
}

std::string look_metric(const shell::SessionTheme& st, const std::string& name)
{
    std::string v = st.value("Metrics." + name);
    if (!v.empty()) return v;
    for (auto& m : kStdMetrics) if (name == m[0]) return m[1];
    return "";
}

int look_int(const shell::SessionTheme& st, const std::string& name) { return atoi(look_metric(st, name).c_str()); }

struct FontSpec { std::string face; int height = -11; bool bold = false, italic = false; };   // height: LOGFONT's

FontSpec parse_font(const std::string& v)
{
    FontSpec f;
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= v.size()) {
        size_t j = v.find(',', i);
        if (j == std::string::npos) j = v.size();
        std::string p = v.substr(i, j - i);
        while (!p.empty() && p[0] == ' ') p.erase(0, 1);
        while (!p.empty() && p.back() == ' ') p.pop_back();
        parts.push_back(p);
        i = j + 1;
    }
    f.face = parts.empty() || parts[0].empty() ? "Tahoma" : parts[0];
    if (parts.size() > 1 && !parts[1].empty()) { int h = atoi(parts[1].c_str()); f.height = h > 0 ? -MulDiv(h, 96, 72) : h; }
    for (size_t k = 2; k < parts.size(); k++) {
        if (!strcasecmp(parts[k].c_str(), "bold")) f.bold = true;
        if (!strcasecmp(parts[k].c_str(), "italic")) f.italic = true;
    }
    return f;
}

int font_points(const FontSpec& f) { return f.height < 0 ? (-f.height * 72 + 48) / 96 : f.height; }

std::string font_value(const FontSpec& f)
{
    std::string s = f.face + ", " + std::to_string(font_points(f));
    if (f.bold) s += ", bold";
    if (f.italic) s += ", italic";
    return s;
}

HFONT make_font(const FontSpec& f)
{
    LOGFONTW lf = {};
    lf.lfHeight = f.height;
    lf.lfWeight = f.bold ? FW_BOLD : FW_NORMAL;
    lf.lfItalic = f.italic;
    lf.lfCharSet = DEFAULT_CHARSET;
    std::u16string face = W(f.face);
    for (size_t i = 0; i < face.size() && i < LF_FACESIZE - 1; i++) lf.lfFaceName[i] = face[i];
    return CreateFontIndirectW(&lf);
}

// GDI into the pixels: a DIB section holding them for fn, copied back
template<class F> void with_dc(Pixels& p, F fn)
{
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = p.w; bi.bmiHeader.biHeight = -p.h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP b = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (b && bits) {
        memcpy(bits, p.px.data(), p.px.size() * 4);
        HGDIOBJ old = SelectObject(dc, b);
        fn(dc, (uint32_t*)bits);

        memcpy(p.px.data(), bits, p.px.size() * 4);
        SelectObject(dc, old);
        DeleteObject(b);
    }
    DeleteDC(dc);
}

void gfill(HDC dc, int l, int t, int r, int b, COLORREF c)
{
    RECT rc = { l, t, r, b };
    HBRUSH br = CreateSolidBrush(c);
    FillRect(dc, &rc, br);
    DeleteObject(br);
}

void gline(HDC dc, int x0, int y0, int x1, int y1, COLORREF c)
{
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ o = SelectObject(dc, pen);
    MoveToEx(dc, x0, y0, nullptr);
    LineTo(dc, x1, y1);
    SelectObject(dc, o);
    DeleteObject(pen);
}

// The classic look's 31 system colours (lhde's Colors.* order = COLOR_* order) for libwuxtheme's classic drawing helpers, which
// draw the frame, the caption and its buttons exactly as luxwm draws a Windows Classic frame (one implementation, ut.h)
struct LookColors { uint32_t c[UT_SYSCOLORS]; };
LookColors look_colors(const shell::SessionTheme& st)
{
    static const char* const names[UT_SYSCOLORS] = {
        "scrollbar", "background", "activecaption", "inactivecaption", "menu", "window", "windowframe", "menutext", "windowtext",
        "captiontext", "activeborder", "inactiveborder", "appworkspace", "highlight", "highlighttext", "btnface", "btnshadow",
        "graytext", "btntext", "inactivecaptiontext", "btnhighlight", "3ddkshadow", "3dlight", "infotext", "infobk",
        "alternatebtnface", "hotlight", "gradientactivecaption", "gradientinactivecaption", "menuhilight", "menubar" };
    LookColors lc;
    for (int i = 0; i < UT_SYSCOLORS; i++) { COLORREF r = look_color(st, names[i]); lc.c[i] = GetRValue(r) << 16 | GetGValue(r) << 8 | GetBValue(r); }
    return lc;
}

// a pixel buffer the preview draws in (the DIB's bits while with_dc runs)
struct Px { uint32_t* px; int w, h; int stride() const { return w * 4; } };

// DrawEdge EDGE_RAISED / EDGE_SUNKEN (BF_RECT) with the look's colours
void gedge(const Px& P, int l, int t, int r, int b, const LookColors& lc, bool raised)
{
    ut_rect rc = { l, t, r, b };
    ut_draw_edge(P.px, P.w, P.h, P.stride(), &rc, raised ? UT_EDGE_RAISED : UT_EDGE_SUNKEN, UT_BF_RECT, lc.c);
}

void gtext(HDC dc, const std::string& s, int l, int t, int r, int b, COLORREF c, const FontSpec& f, UINT flags)
{
    HFONT font = make_font(f);
    HGDIOBJ o = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    RECT rc = { l, t, r, b };
    std::u16string w = W(s);
    DrawTextW(dc, (LPCWSTR)w.c_str(), -1, &rc, flags);
    SelectObject(dc, o);
    DeleteObject(font);
}

// a caption button (DrawFrameControl DFC_CAPTION): kind 0 minimize, 1 maximize, 2 close
void gcapbutton(const Px& P, int l, int t, int r, int b, int kind, const LookColors& lc)
{
    ut_rect rc = { l, t, r, b };
    ut_draw_frame_control(P.px, P.w, P.h, P.stride(), &rc, UT_DFC_CAPTION,
                          kind == 0 ? UT_DFCS_CAPTIONMIN : kind == 1 ? UT_DFCS_CAPTIONMAX : UT_DFCS_CAPTIONCLOSE, lc.c);
}

// a classic window frame + caption in l,t,r,b, laid out as user32 does (luxwm's classic frame: cnBorders = 3 + borderwidth,
// caption captionheight + 1, buttons (captionwidth - 2) x (captionheight - 4), close 2 px from the right, CXEDGE, max and min
// touching); returns the client's top
int gwindow(const Px& P, int l, int t, int r, int b, const std::string& title, bool active, const shell::SessionTheme& st, bool small = false)
{
    LookColors lc = look_colors(st);
    int bw = (std::max)(1, look_int(st, "borderwidth"));
    ut_rect fr = { l, t, r, b };
    ut_draw_edge(P.px, P.w, P.h, P.stride(), &fr, UT_EDGE_RAISED, UT_BF_RECT | UT_BF_ADJUST, lc.c);
    auto ring = [&](uint32_t c, int k) {   // DrawFrame: k px in colour c, the rectangle shrinks
        uint32_t v = 0xff000000u | c;
        for (int y = fr.top; y < fr.bottom; y++) for (int x = fr.left; x < fr.right; x++)
            if (y < fr.top + k || y >= fr.bottom - k || x < fr.left + k || x >= fr.right - k) if (x >= 0 && y >= 0 && x < P.w && y < P.h) P.px[(size_t)y * P.w + x] = v;
        fr.left += k; fr.top += k; fr.right -= k; fr.bottom -= k;
    };
    ring(lc.c[active ? 10 : 11], bw);   // activeborder / inactiveborder
    ring(lc.c[15], 1);                  // btnface
    int ch = (std::max)(8, look_int(st, small ? "smcaptionheight" : "captionheight"));
    int cw = (std::max)(8, look_int(st, small ? "smcaptionwidth" : "captionwidth"));
    int ct = fr.top, cb = ct + ch, cl = fr.left, cr = fr.right;
    bool gradient = lc.c[active ? 2 : 3] != lc.c[active ? 27 : 28];
    // the bar's background under the buttons (the gradient's end colour), the separator line
    { uint32_t v = 0xff000000u | lc.c[gradient ? (active ? 27 : 28) : (active ? 2 : 3)];
      for (int y = ct; y < cb && y < P.h; y++) for (int x = cl; x < cr && x < P.w; x++) P.px[(size_t)y * P.w + x] = v;
      if (cb < P.h) for (int x = cl; x < cr && x < P.w; x++) P.px[(size_t)cb * P.w + x] = 0xff000000u | lc.c[15]; }
    int bh = ch - 4, bwid = cw - 2, by = ct + 2;
    int x = cr - 2 - bwid;
    gcapbutton(P, x, by, x + bwid, by + bh, 2, lc);
    if (!small) {
        x -= 2 + bwid; gcapbutton(P, x, by, x + bwid, by + bh, 1, lc);
        x -= bwid; gcapbutton(P, x, by, x + bwid, by + bh, 0, lc);
    }
    FontSpec fs = parse_font(look_metric(st, small ? "smcaptionfont" : "captionfont"));
    ut_font f; memset(&f, 0, sizeof f);
    snprintf(f.face, sizeof f.face, "%s", fs.face.c_str()); f.height = fs.height; f.weight = fs.bold ? 700 : 400; f.italic = fs.italic;
    ut_rect cap = { cl, ct, x - 2, cb };
    ut_draw_caption(P.px, P.w, P.h, P.stride(), &cap, (active ? UT_DC_ACTIVE : 0) | (gradient ? UT_DC_GRADIENT : 0) | UT_DC_TEXT,
                    title.c_str(), nullptr, 0, 0, &f, lc.c);
    return cb + 1;
}

// XP's classic Appearance preview: an inactive window, an active one with its menu bar, window text and a scroll bar, a
// message box; over the desktop colour (background = true) or the pixels already there
void classic_sample(const shell::SessionTheme& st, Pixels& p, bool background)
{
    with_dc(p, [&](HDC dc, uint32_t* bits) {
        Px P{ bits, p.w, p.h }; LookColors lc = look_colors(st);
        int PW = p.w, PH = p.h;
        if (background) gfill(dc, 0, 0, PW, PH, look_color(st, "background"));
        int bw = 3 + (std::max)(1, look_int(st, "borderwidth"));   // cnBorders: the edge, the sizing border, the btnface ring
        // the inactive window
        int il = PW / 24, it = PH / 24, ir = PW - PW / 6, ib = PH * 3 / 5;
        int ict = gwindow(P, il, it, ir, ib, "Inactive Window", false, st);
        gfill(dc, il + bw, ict, ir - bw, ib - bw, look_color(st, "window"));
        // the active window
        int ch = look_int(st, "captionheight");
        int al = il + PW / 20, at = it + ch + 8, ar = PW - PW / 24, ab = PH - PH / 20;
        int act = gwindow(P, al, at, ar, ab, "Active Window", true, st);
        int mh = (std::max)(8, look_int(st, "menuheight"));
        int ml = al + bw, mr = ar - bw;
        gfill(dc, ml, act, mr, act + mh, look_color(st, "menu"));
        FontSpec mf = parse_font(look_metric(st, "menufont"));
        HFONT f = make_font(mf);
        HDC tdc = dc;
        HGDIOBJ o = SelectObject(tdc, f);
        auto width = [&](const char* s) { SIZE sz = {}; std::u16string w16 = W(s); GetTextExtentPoint32W(tdc, (LPCWSTR)w16.c_str(), (int)w16.size(), &sz); return (int)sz.cx; };
        int wn = width("Normal"), wd = width("Disabled"), ws = width("Selected");
        SelectObject(tdc, o);
        DeleteObject(f);
        int x = ml + 6;
        gtext(dc, "Normal", x, act, x + wn + 12, act + mh, look_color(st, "menutext"), mf, DT_SINGLELINE | DT_VCENTER);
        x += wn + 12;
        gtext(dc, "Disabled", x + 1, act + 1, x + wd + 13, act + mh + 1, look_color(st, "btnhighlight"), mf, DT_SINGLELINE | DT_VCENTER);
        gtext(dc, "Disabled", x, act, x + wd + 12, act + mh, look_color(st, "graytext"), mf, DT_SINGLELINE | DT_VCENTER);
        x += wd + 12;
        gfill(dc, x - 4, act, x + ws + 6, act + mh, look_color(st, "highlight"));
        gtext(dc, "Selected", x, act, x + ws + 6, act + mh, look_color(st, "highlighttext"), mf, DT_SINGLELINE | DT_VCENTER);
        // the client: window colour, sunken, window text, a scroll bar
        int cl = ml, ct = act + mh + 1, cr = mr, cb = ab - bw;
        gfill(dc, cl, ct, cr, cb, look_color(st, "btnface"));
        gedge(P, cl, ct, cr, cb, lc, false);
        gfill(dc, cl + 2, ct + 2, cr - 2, cb - 2, look_color(st, "window"));
        int sw = (std::max)(8, look_int(st, "scrollwidth"));
        int sx = cr - 2 - sw;
        gfill(dc, sx, ct + 2, cr - 2, cb - 2, look_color(st, "scrollbar"));
        for (int k = 0; k < 2; k++) {
            int y0 = k ? cb - 2 - sw : ct + 2;
            gfill(dc, sx, y0, sx + sw, y0 + sw, look_color(st, "btnface"));
            gedge(P, sx, y0, sx + sw, y0 + sw, lc, true);
            int mx = sx + sw / 2, my = y0 + sw / 2, s = sw / 5;
            for (int d = 0; d <= s; d++) gline(dc, mx - d, k ? my - s / 2 + (s - d) : my - s / 2 + d, mx + d + 1, k ? my - s / 2 + (s - d) : my - s / 2 + d, look_color(st, "btntext"));
        }
        gtext(dc, "Window Text", cl + 8, ct + 4, sx - 4, ct + 24, look_color(st, "windowtext"), parse_font(look_metric(st, "messagefont")), DT_SINGLELINE | DT_VCENTER);
        // the message box
        FontSpec msg = parse_font(look_metric(st, "messagefont"));
        int obh = (std::max)(14, -msg.height + 9);
        int mbw = PW * 2 / 5, mbh = (std::max)(PH / 3, ch + 2 * bw + 1 + 20 + obh + 8);
        int mbl = PW / 2 - mbw / 3, mbt = (std::max)(ct + 24, cb - mbh - 6), mbr = mbl + mbw, mbb = mbt + mbh;
        if (mbb > PH - 2) { mbb = PH - 2; mbt = mbb - mbh; }
        int mct = gwindow(P, mbl, mbt, mbr, mbb, "Message Box", true, st);
        gfill(dc, mbl + bw, mct, mbr - bw, mbb - bw, look_color(st, "btnface"));
        gtext(dc, "Message Text", mbl + bw + 6, mct + 2, mbr - bw - 4, mct + 18, look_color(st, "windowtext"), msg, DT_SINGLELINE | DT_VCENTER);
        int obw = (std::min)(50, mbw / 3), obx = (mbl + mbr - obw) / 2, oby = mbb - bw - obh - 4;
        if (oby > mct + 16) {
            gline(dc, obx - 1, oby - 1, obx + obw, oby - 1, look_color(st, "windowframe"));
            gline(dc, obx - 1, oby + obh, obx + obw + 1, oby + obh, look_color(st, "windowframe"));
            gline(dc, obx - 1, oby - 1, obx - 1, oby + obh, look_color(st, "windowframe"));
            gline(dc, obx + obw, oby - 1, obx + obw, oby + obh, look_color(st, "windowframe"));
            gfill(dc, obx, oby, obx + obw, oby + obh, look_color(st, "btnface"));
            gedge(P, obx, oby, obx + obw, oby + obh, lc, true);
            gtext(dc, "OK", obx, oby, obx + obw, oby + obh, look_color(st, "btntext"), msg, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        }
    });
}

// Owner-drawn preview statics: pixels into the item rectangle with a sunken edge
void paint_pixels(const DRAWITEMSTRUCT* di, void (*make)(Pixels&))
{
    RECT r = di->rcItem;
    Pixels p;
    p.w = r.right - r.left - 2; p.h = r.bottom - r.top - 2;
    if (p.w <= 0 || p.h <= 0) return;
    p.px.assign((size_t)p.w * p.h, 0xff808080u);
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

bool is_classic(const shell::SessionTheme& st) { return st.classic || st.value("style.kind") == "classic"; }

// The Themes tab's sample: the theme's background with a window in its look
shell::SessionTheme sample_theme;
void make_theme_sample(Pixels& p)
{
    record_thumbnail(sample_theme, p);
    if (is_classic(sample_theme)) {
        Pixels q; q.w = p.w * 4 / 5; q.h = p.h * 3 / 4; q.px.assign((size_t)q.w * q.h, 0);
        // the windows of the classic preview, drawn over the wallpaper's middle
        for (int y = 0; y < q.h; y++) memcpy(&q.px[(size_t)y * q.w], &p.px[(size_t)(y + p.h / 8) * p.w + p.w / 10], (size_t)q.w * 4);
        classic_sample(sample_theme, q, false);
        for (int y = 0; y < q.h; y++) memcpy(&p.px[(size_t)(y + p.h / 8) * p.w + p.w / 10], &q.px[(size_t)y * q.w], (size_t)q.w * 4);
    } else style_sample(sample_theme, p, p.w / 10, p.h / 8, p.w - p.w / 10, p.h - p.h / 10);
}
void make_desktop_sample(Pixels& p) { record_thumbnail(rec, p); }
void make_appearance_sample(Pixels& p)
{
    if (is_classic(rec)) { classic_sample(rec, p, true); return; }
    fill_rect(p, 0, 0, p.w, p.h, shell::parse_color(rec.value("Desktop.color")));
    style_sample(rec, p, p.w / 12, p.h / 10, p.w - p.w / 12, p.h - p.h / 12, "Active Window");
}

// ---- pages -------------------------------------------------------------------------

void fill_theme_list(HWND dlg);
void fill_desktop(HWND dlg);
void fill_appearance(HWND dlg);
void fill_shell(HWND dlg);

void refresh_all()
{
    recompute();
    sample_theme = rec;
    if (pages[0]) { fill_theme_list(pages[0]); InvalidateRect(GetDlgItem(pages[0], IDC_TH_PREVIEW), nullptr, FALSE); }
    if (pages[1]) fill_desktop(pages[1]);
    if (pages[2]) fill_appearance(pages[2]);
    if (pages[3]) fill_shell(pages[3]);
}

// ---- apply -------------------------------------------------------------------------

void remember_wallpaper(const std::string& path);

void apply()
{
    std::vector<store::Edit> edits;
    if (cur.sbmode != committed.sbmode) edits.push_back(store::Edit{ "sidebar", "mode", cur.sbmode });
    if (cur.sbstack != committed.sbstack) edits.push_back(store::Edit{ "sidebar", "stacking", cur.sbstack });
    if (cur.sbclassic != committed.sbclassic) edits.push_back(store::Edit{ "sidebar", "with_classic", cur.sbclassic });
    if (cur.smoothing != committed.smoothing) edits.push_back(store::Edit{ "fonts", "smoothing", cur.smoothing });
    if (cur.shadows != committed.shadows) edits.push_back(store::Edit{ "effects", "menu_shadows", cur.shadows });
    if (cur.cues != committed.cues) edits.push_back(store::Edit{ "effects", "keyboard_cues", cur.cues });
    bool theme_changed = cur.theme != committed.theme;
    bool layer_changed = cur.layer != committed.layer;
    bool wall = cur.layer.count("Desktop.wallpaper") && (!committed.layer.count("Desktop.wallpaper") ||
                                                          committed.layer.at("Desktop.wallpaper") != cur.layer.at("Desktop.wallpaper"));
    if (wall && value("Desktop.wallpaper") != "none" && shell::wallpaper_hidden()) edits.push_back(store::Edit{ "desktop", "wallpaper", "" });
    if (!theme_changed && !layer_changed && edits.empty()) return;
    bool ok = edits.empty() || store::set(edits);
    // order (SETTINGS.md): the theme selected (another theme's layer dropped), the layer, then _LH_THEME and one message
    if (theme_changed) ok = lhde_theme_select(cur.theme.c_str()) >= 0 && ok;
    if (theme_changed || layer_changed) {
        std::vector<store::KeyValue> kv;
        for (auto& x : cur.layer) kv.push_back(store::KeyValue{ x.first, x.second });
        ok = store::set_current_layer(kv) && ok;
    }
    if (!ok)
        filedlg::message_box(pages[0], "Display Properties", "Cannot write the settings (" + store::path() + ").", MB_OK | MB_ICONEXCLAMATION);
    if (wall) remember_wallpaper(value("path.wallpaper"));
    if (theme_changed) store::apply_theme(cur.theme);
    else store::notify(layer_changed ? "theme" : (edits.size() == 1 ? edits[0].section : ""));
    // what the store holds now (keys the theme gives anyway are gone)
    layer_from_store(cur);
    stash_theme.clear();
    stash_layer.clear();
    committed = cur;
    refresh_all();
}

// ---- Themes ------------------------------------------------------------------------

struct ThemeItem { std::string name; bool modified; };
std::vector<ThemeItem> theme_items;

void fill_theme_list(HWND dlg)
{
    themes = store::theme_files();
    HWND c = GetDlgItem(dlg, IDC_TH_LIST);
    filling = true;
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    theme_items.clear();
    std::string mod_theme = !cur.layer.empty() ? cur.theme : stash_theme;
    int sel = -1;
    bool listed = false;
    for (const store::ThemeEntry& e : themes) {
        if (e.style_missing && e.name != cur.theme && e.name != mod_theme) continue;   // its style is not imported
        theme_items.push_back(ThemeItem{ e.name, false });
        if (e.name == cur.theme && cur.layer.empty()) sel = (int)theme_items.size() - 1;
        if (e.name == cur.theme) listed = true;
        if (e.name == mod_theme) {
            theme_items.push_back(ThemeItem{ e.name, true });
            if (!cur.layer.empty()) sel = (int)theme_items.size() - 1;
        }
    }
    if (!listed && !cur.theme.empty()) {   // a bare style or a name no file has: shown as it is
        theme_items.push_back(ThemeItem{ cur.theme, !cur.layer.empty() });
        sel = (int)theme_items.size() - 1;
    }
    for (const ThemeItem& it : theme_items) {
        std::string t = theme_title(it.name) + (it.modified ? " (Modified)" : "");
        std::u16string w = W(t);
        SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)w.c_str());
    }
    SendMessageW(c, CB_SETCURSEL, (WPARAM)sel, 0);
    EnableWindow(GetDlgItem(dlg, IDC_TH_DELETE), shell::is_user_theme(cur.theme) && cur.layer.empty());
    filling = false;
}

void pick_theme(HWND dlg, int i)
{
    if (i < 0 || i >= (int)theme_items.size()) return;
    const ThemeItem& it = theme_items[(size_t)i];
    if (it.modified) {
        if (cur.layer.empty() && !stash_theme.empty()) { cur.theme = stash_theme; cur.layer = stash_layer; stash_theme.clear(); stash_layer.clear(); }
    } else {
        if (it.name == cur.theme && cur.layer.empty()) return;
        // a theme brings all of its own (Windows): the changes go, kept aside as "NAME (Modified)" until Apply
        if (!cur.layer.empty()) { stash_theme = cur.theme; stash_layer = cur.layer; }
        cur.theme = it.name;
        cur.layer.clear();
    }
    refresh_all();
    changed(dlg);
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

// Save As... (docs/THEME-FORMAT.md section 3): the pending changes applied, then the selected theme with its current
// layer written as the user's theme file (lhde_theme_save_as), selected, the layer cleared
void save_theme(HWND dlg)
{
    apply();
    std::string title = theme_title(cur.theme);
    const std::string mod = " (modified)";
    if (title.size() > mod.size() && !strcasecmp(title.c_str() + title.size() - mod.size(), mod.c_str())) title.resize(title.size() - mod.size());
    if (!shell::is_user_theme(cur.theme) || !cur.layer.empty()) title = "My " + title;
    if (DialogBoxParamW(module(), MAKEINTRESOURCEW(IDD_SAVEAS), GetParent(dlg), saveas_proc, (LPARAM)&title) != IDOK) return;
    std::string file = theme_file_name(title), canon;
    if (file.empty()) {
        filedlg::message_box(GetParent(dlg), "Save Theme As", "\"" + title + "\" cannot be used as a theme name.", MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    if (shell::lookup_theme(file, canon) && !shell::is_user_theme(file)) {
        filedlg::message_box(GetParent(dlg), "Save Theme As", "\"" + file + "\" is a built-in theme.", MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    if (file != cur.theme && shell::is_user_theme(file) &&
        filedlg::message_box(GetParent(dlg), "Save Theme As", "The theme \"" + file + "\" exists. Replace it?", MB_YESNO | MB_ICONEXCLAMATION) != IDYES)
        return;
    std::string err;
    if (!store::save_theme_as(file, title, &err)) {
        filedlg::message_box(GetParent(dlg), "Save Theme As", "Cannot save the theme: " + err + ".", MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    State keep = cur;
    cur = load_state();
    cur.sbmode = keep.sbmode; cur.sbstack = keep.sbstack; cur.sbclassic = keep.sbclassic; cur.smoothing = keep.smoothing;
    cur.shadows = keep.shadows; cur.cues = keep.cues;
    committed = cur;
    refresh_all();
}

// Delete: one of the user's theme files; when it is the one in force its base is selected (liblhde)
void delete_theme(HWND dlg)
{
    if (!shell::is_user_theme(cur.theme)) return;
    if (filedlg::message_box(GetParent(dlg), "Delete Theme", "Delete the theme \"" + theme_title(cur.theme) + "\"?",
                             MB_YESNO | MB_ICONEXCLAMATION) != IDYES) return;
    bool in_force = cur.theme == committed.theme;
    std::string base = files.value("Theme.base");
    store::delete_theme(cur.theme);
    if (in_force) {
        State keep = cur;
        cur = load_state();
        cur.sbmode = keep.sbmode; cur.sbstack = keep.sbstack; cur.sbclassic = keep.sbclassic; cur.smoothing = keep.smoothing;
        cur.shadows = keep.shadows; cur.cues = keep.cues;
        committed = cur;
    } else {
        cur.theme = base.empty() ? committed.theme : base;
        cur.layer.clear();
        changed(dlg);
    }
    refresh_all();
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
    if (pages[3]) fill_shell(pages[3]);
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
    // desk.cpl's Display icon (IDI_DISPLAY 100) on the sheet, as the window and taskbar icon (_NET_WM_ICON); the icon
    // theme's "display" key, none when the theme has no source for it (the window manager's default then)
    std::string src = shell::icon_file("display");
    const int sizes[3] = { 16, 48, 32 };   // ICON_SMALL, ICON_BIG, ICON_SMALL2
    for (int i = 0; i < 3 && !src.empty(); i++)
        if (HICON ic = (HICON)uiicons::load_themed(src, IMAGE_ICON, sizes[i], sizes[i], 0))
            SendMessageW(GetParent(page), WM_SETICON, (WPARAM)i, (LPARAM)ic);
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
            if (HIWORD(wp) == CBN_SELCHANGE && !filling) pick_theme(dlg, (int)SendDlgItemMessageW(dlg, IDC_TH_LIST, CB_GETCURSEL, 0, 0));
            return TRUE;
        case IDC_TH_SAVE: save_theme(dlg); return TRUE;
        case IDC_TH_DELETE: delete_theme(dlg); return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

// ---- Desktop: the background list (XP's: names, the preview above) and the icon set ----

struct Wall { std::string label, value, file; };   // value: Desktop.wallpaper (none, 4074/Bliss, a path); file: the picture
std::vector<Wall> walls;
std::vector<Item> icon_items;
const std::vector<Item> kModes = { { "Fill", "fill" }, { "Fit", "fit" }, { "Stretch", "stretch" }, { "Center", "center" }, { "Tile", "tile" } };

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

std::string wall_file() { return value("Desktop.wallpaper") == "none" || shell::wallpaper_hidden() ? "" : value("path.wallpaper"); }
std::string wall_value() { return shell::wallpaper_hidden() ? "none" : value("Desktop.wallpaper"); }

void build_walls()
{
    walls.clear();
    walls.push_back(Wall{ "(None)", "none", "" });
    // the theme's own background (its files, without the layer): choosing it takes the change back
    shell::Wallpaper tb;
    if (shell::theme_wallpaper(files, tb) && !tb.file.empty())
        walls.push_back(Wall{ stem(tb.file) + " (theme background)", files.value("Desktop.wallpaper"), tb.file });
    auto add = [&](const std::string& p, const std::string& label) {
        for (const Wall& w : walls) if (w.file == p) return;
        walls.push_back(Wall{ label, shell::wallpaper_value(p), p });
    };
    struct stat st;
    for (const std::string& p : recent_wallpapers()) if (stat(expand(p).c_str(), &st) == 0) add(expand(p), stem(p));
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
    if (!wall_file().empty()) add(wall_file(), stem(wall_file()));   // a picture from elsewhere
    // XP's order: "(None)" first, then every name alphabetically (the recent pictures are listed, not put on top)
    std::stable_sort(walls.begin() + 1, walls.end(), [](const Wall& a, const Wall& b) { return strcasecmp(a.label.c_str(), b.label.c_str()) < 0; });
}

// the entry of the background in force
int wall_selected()
{
    std::string wf = wall_file(), wv = wall_value();
    for (size_t i = 0; i < walls.size(); i++)
        if (wv == "none" ? walls[i].value == "none" : (!walls[i].file.empty() && walls[i].file == wf)) return (int)i;
    return 0;
}

void fill_walls(HWND dlg)
{
    HWND lv = GetDlgItem(dlg, IDC_DT_LIST);
    build_walls();
    uiicons::sync();   // the icon set of the theme in force (it changes with Apply)
    SendMessageW(lv, WM_SETREDRAW, FALSE, 0);
    SendMessageW(lv, LVM_DELETEALLITEMS, 0, 0);
    int image = uiicons::index("image"), sel = wall_selected();
    for (size_t i = 0; i < walls.size(); i++) {
        std::u16string t = W(walls[i].label);
        LVITEMW it = {};
        it.mask = LVIF_TEXT | LVIF_IMAGE;
        it.iItem = (int)i;
        it.pszText = (LPWSTR)t.c_str();
        it.iImage = walls[i].file.empty() ? I_IMAGENONE : image;
        SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&it);
    }
    LVITEMW st = {};
    st.stateMask = st.state = LVIS_SELECTED | LVIS_FOCUSED;
    SendMessageW(lv, LVM_SETITEMSTATE, (WPARAM)sel, (LPARAM)&st);
    SendMessageW(lv, WM_SETREDRAW, TRUE, 0);
    // after the redraw is on again: the list's scroll range is only updated then (Wine's LISTVIEW_UpdateScroll), before it
    // the scroll to an entry past the first page stays at the top
    SendMessageW(lv, LVM_ENSUREVISIBLE, (WPARAM)sel, FALSE);
    InvalidateRect(lv, nullptr, TRUE);
}

void fill_desktop(HWND dlg)
{
    filling = true;
    fill_walls(dlg);
    std::vector<Item> modes = kModes;
    fill_list(dlg, IDC_DT_MODE, modes, value("Desktop.mode").empty() ? "fill" : value("Desktop.mode"));
    EnableWindow(GetDlgItem(dlg, IDC_DT_MODE), wall_value() != "none");
    icon_items = items({ { "Longhorn", "longhorn4074" }, { "Windows XP", "xp" }, { "Windows 2000", "2k" }, { "Wine", "wine" } });
    fill_list(dlg, IDC_DT_ICONS, icon_items, value("Desktop.icons"));
    set_text(dlg, IDC_DT_ICONS_SRC, source_text("Desktop.icons"));
    filling = false;
    InvalidateRect(GetDlgItem(dlg, IDC_DT_PREVIEW), nullptr, FALSE);
}

void browse(HWND dlg)
{
    filedlg::Options o;
    o.title = "Browse";
    o.filters = { filedlg::Filter{ "Pictures (*.bmp;*.jpg;*.jpeg;*.png;*.webp)", { "*.bmp", "*.jpg", "*.jpeg", "*.png", "*.webp" }, {} },
                  filedlg::Filter{ "All Files", { "*" }, {} } };
    if (!wall_file().empty()) o.initial_dir = shell::dir_of(wall_file());
    else o.initial_dir = shell::home_dir() + "/Pictures";
    filedlg::Result r = filedlg::run(GetParent(dlg), o);
    // the sheet was disabled while the dialog ran: repaint its controls enabled
    RedrawWindow(GetParent(dlg), nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    if (!r.ok || r.paths.empty()) return;
    set_key("Desktop.wallpaper", shell::wallpaper_value(r.paths[0]));
    refresh_all();
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
                if (w.value != wall_value() || w.file != wall_file()) {
                    set_key("Desktop.wallpaper", w.value);
                    recompute();
                    EnableWindow(GetDlgItem(dlg, IDC_DT_MODE), wall_value() != "none");
                    InvalidateRect(GetDlgItem(dlg, IDC_DT_PREVIEW), nullptr, FALSE);
                    if (pages[0]) { sample_theme = rec; fill_theme_list(pages[0]); InvalidateRect(GetDlgItem(pages[0], IDC_TH_PREVIEW), nullptr, FALSE); }
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
                if (LOWORD(wp) == IDC_DT_MODE) set_key("Desktop.mode", list_value(dlg, IDC_DT_MODE, kModes, "fill"));
                else set_key("Desktop.icons", list_value(dlg, IDC_DT_ICONS, icon_items, value("Desktop.icons")));
                refresh_all();
                changed(dlg);
            }
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

// ---- Appearance: Windows and buttons, Color scheme, Font size -------------------------

std::vector<Item> style_items, color_items, size_items;

void fill_appearance(HWND dlg)
{
    filling = true;
    load_styles();
    load_schemes();
    style_items.clear();
    bool classic = is_classic(rec);
    std::string style = classic ? "classic" : value("VisualStyle.style");
    for (const Style& s : styles) if (s.named || s.name == style) style_items.push_back(Item{ s.label, s.name });
    fill_list(dlg, IDC_AP_STYLE, style_items, style);
    if (classic) {
        color_items = scheme_items();
        std::string sc = value("VisualStyle.scheme");
        if (sc.empty()) sc = "none";
        fill_list(dlg, IDC_AP_COLOR, color_items, sc);
        size_items = scheme_sizes(sc);
        std::string sz = value("VisualStyle.size");
        fill_list(dlg, IDC_AP_SIZE, size_items, sz.empty() || sz == "NormalSize" ? "Normal" : sz);
    } else {
        const Style* s = style_named(style);
        style_variants(s ? s->path : rec.msstyles, color_items, size_items);
        fill_list(dlg, IDC_AP_COLOR, color_items, value("VisualStyle.color"));
        fill_list(dlg, IDC_AP_SIZE, size_items, value("VisualStyle.size"));
    }
    EnableWindow(GetDlgItem(dlg, IDC_AP_ADVANCED), classic);
    filling = false;
    InvalidateRect(GetDlgItem(dlg, IDC_AP_PREVIEW), nullptr, FALSE);
}

// "Windows and buttons" changed: the style with a colour and size of its own (the current ones when it has them); the
// window frames follow the style (its own frame set, as a bare style gets it), the rest of the theme stays
void pick_style(const std::string& name)
{
    std::string theme_style = files.value("VisualStyle.style");
    if (name == "classic") {
        set_key("VisualStyle.style", "classic");
        std::string sc = value("VisualStyle.scheme");
        set_key("VisualStyle.scheme", is_classic(rec) && !sc.empty() && sc != "none" ? sc : standard_scheme());
        set_key("VisualStyle.size", "Normal");
        set_key("Glass.frames", "none");
        set_key("Glass.frames_classic", "none");
        return;
    }
    const Style* s = style_named(name);
    std::vector<Item> colors, sizes;
    style_variants(s ? s->path : "", colors, sizes);
    std::string c = value("VisualStyle.color"), z = value("VisualStyle.size");
    auto has = [](const std::vector<Item>& l, const std::string& v) { for (const Item& i : l) if (i.value == v) return true; return false; };
    set_key("VisualStyle.style", name);
    set_key("VisualStyle.color", has(colors, c) ? c : colors.empty() ? "" : colors[0].value);
    set_key("VisualStyle.size", has(sizes, z) ? z : sizes.empty() ? "" : sizes[0].value);
    if (name == theme_style) {   // back to the theme's own style: its frames again
        cur.layer.erase("Glass.frames");
        cur.layer.erase("Glass.frames_classic");
        cur.layer.erase("VisualStyle.scheme");
    } else {
        shell::SessionTheme bare;
        if (shell::resolve_theme_files(name, bare)) {
            set_key("Glass.frames", bare.value("Glass.frames"));
            set_key("Glass.frames_classic", bare.value("Glass.frames_classic"));
        }
    }
    for (auto it = cur.layer.begin(); it != cur.layer.end(); ) it = it->second.empty() ? cur.layer.erase(it) : std::next(it);
}

// ---- Effects... (the user's: settings.ini [fonts] smoothing, [effects] menu_shadows, keyboard_cues) -----------

const std::vector<Item> kMethods = { { "Standard", "standard" }, { "ClearType", "cleartype" } };

INT_PTR CALLBACK effects_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM)
{
    switch (msg) {
    case WM_INITDIALOG: {
        CheckDlgButton(dlg, IDC_EF_SMOOTH, cur.smoothing != "off" ? BST_CHECKED : BST_UNCHECKED);
        std::vector<Item> m = kMethods;
        fill_list(dlg, IDC_EF_METHOD, m, cur.smoothing == "cleartype" ? "cleartype" : "standard");
        EnableWindow(GetDlgItem(dlg, IDC_EF_METHOD), cur.smoothing != "off");
        CheckDlgButton(dlg, IDC_EF_SHADOWS, cur.shadows != "0" ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_EF_HIDEKEYS, cur.cues != "1" ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_EF_SMOOTH) {
            EnableWindow(GetDlgItem(dlg, IDC_EF_METHOD), IsDlgButtonChecked(dlg, IDC_EF_SMOOTH) == BST_CHECKED);
            return TRUE;
        }
        if (LOWORD(wp) == IDOK) {
            bool on = IsDlgButtonChecked(dlg, IDC_EF_SMOOTH) == BST_CHECKED;
            int i = (int)SendDlgItemMessageW(dlg, IDC_EF_METHOD, CB_GETCURSEL, 0, 0);
            cur.smoothing = on ? (i == 1 ? "cleartype" : "standard") : "off";
            cur.shadows = IsDlgButtonChecked(dlg, IDC_EF_SHADOWS) == BST_CHECKED ? "1" : "0";
            cur.cues = IsDlgButtonChecked(dlg, IDC_EF_HIDEKEYS) == BST_CHECKED ? "0" : "1";
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) { EndDialog(dlg, IDCANCEL); return TRUE; }
        return FALSE;
    }
    return FALSE;
}

// ---- Advanced Appearance (classic): XP's per-item editor --------------------------------

struct AdvItem { const char *label, *size, *size2, *color1, *color2, *font, *fontcolor; };
const AdvItem kAdv[] = {
    { "3D Objects", nullptr, nullptr, "btnface", nullptr, nullptr, "btntext" },
    { "Active Title Bar", "captionheight", nullptr, "activecaption", "gradientactivecaption", "captionfont", "captiontext" },
    { "Active Window Border", "borderwidth", nullptr, "activeborder", nullptr, nullptr, nullptr },
    { "Application Background", nullptr, nullptr, "appworkspace", nullptr, nullptr, nullptr },
    { "Caption Buttons", "captionwidth", nullptr, nullptr, nullptr, nullptr, nullptr },
    { "Desktop", nullptr, nullptr, "background", nullptr, nullptr, nullptr },
    { "Disabled Menu Item", nullptr, nullptr, nullptr, nullptr, nullptr, "graytext" },
    { "Icon", nullptr, nullptr, nullptr, nullptr, "icontitlefont", nullptr },
    { "Inactive Title Bar", "captionheight", nullptr, "inactivecaption", "gradientinactivecaption", "captionfont", "inactivecaptiontext" },
    { "Inactive Window Border", "borderwidth", nullptr, "inactiveborder", nullptr, nullptr, nullptr },
    { "Menu", "menuheight", "menuwidth", "menu", nullptr, "menufont", "menutext" },
    { "Message Box", nullptr, nullptr, nullptr, nullptr, "messagefont", "windowtext" },
    { "Palette Title", "smcaptionheight", nullptr, nullptr, nullptr, "smcaptionfont", nullptr },
    { "Scrollbar", "scrollwidth", "scrollheight", "scrollbar", nullptr, nullptr, nullptr },
    { "Selected Items", "menuheight", "menuwidth", "highlight", nullptr, "menufont", "highlighttext" },
    { "Small Caption Buttons", "smcaptionwidth", nullptr, nullptr, nullptr, nullptr, nullptr },
    { "ToolTip", nullptr, nullptr, "infobk", nullptr, "statusfont", "infotext" },
    { "Window", nullptr, nullptr, "window", nullptr, nullptr, "windowtext" },
};
const int kAdvCount = (int)(sizeof kAdv / sizeof *kAdv);

Layer adv_layer;                  // the dialog's copy of the pending layer
shell::SessionTheme adv_rec;
int adv_item = 5;                 // Desktop
bool adv_filling;
std::vector<std::string> font_faces;

void adv_recompute()
{
    if (!shell::resolve_theme_with(cur.theme, adv_layer, adv_rec)) adv_rec = rec;
}

void adv_set(const std::string& key, const std::string& v) { adv_layer[key] = v; adv_recompute(); }

COLORREF adv_color(const char* name) { return look_color(adv_rec, name); }

std::string rgb_text(COLORREF c) { return std::to_string(GetRValue(c)) + " " + std::to_string(GetGValue(c)) + " " + std::to_string(GetBValue(c)); }

void load_faces()
{
    if (!font_faces.empty()) return;
    FcPattern* pat = FcPatternCreate();
    FcObjectSet* os = FcObjectSetBuild(FC_FAMILY, nullptr);
    FcFontSet* fs = FcFontList(nullptr, pat, os);
    std::set<std::string> seen;
    for (int i = 0; fs && i < fs->nfont; i++) {
        FcChar8* fam = nullptr;
        if (FcPatternGetString(fs->fonts[i], FC_FAMILY, 0, &fam) == FcResultMatch && fam && fam[0] != '.') seen.insert((const char*)fam);
    }
    if (fs) FcFontSetDestroy(fs);
    FcObjectSetDestroy(os);
    FcPatternDestroy(pat);
    font_faces.assign(seen.begin(), seen.end());
    std::sort(font_faces.begin(), font_faces.end(), [](const std::string& a, const std::string& b) { return strcasecmp(a.c_str(), b.c_str()) < 0; });
}

void adv_show(HWND dlg)
{
    adv_filling = true;
    const AdvItem& it = kAdv[adv_item];
    auto show = [&](int id, int label, bool on) { EnableWindow(GetDlgItem(dlg, id), on); EnableWindow(GetDlgItem(dlg, label), on); };
    show(IDC_ADV_SIZE, IDC_ADV_SIZE_L, it.size != nullptr);
    EnableWindow(GetDlgItem(dlg, IDC_ADV_SIZEUD), it.size != nullptr);
    SetDlgItemTextW(dlg, IDC_ADV_SIZE, (LPCWSTR)W(it.size ? std::to_string(look_int(adv_rec, it.size)) : "").c_str());
    show(IDC_ADV_COLOR1, IDC_ADV_COLOR1_L, it.color1 != nullptr);
    show(IDC_ADV_COLOR2, IDC_ADV_COLOR2_L, it.color2 != nullptr);
    show(IDC_ADV_FONT, IDC_ADV_FONT_L, it.font != nullptr);
    show(IDC_ADV_FONTSIZE, IDC_ADV_FONTSIZE_L, it.font != nullptr);
    show(IDC_ADV_FONTCOLOR, IDC_ADV_FONTCOLOR_L, it.fontcolor != nullptr);
    EnableWindow(GetDlgItem(dlg, IDC_ADV_BOLD), it.font != nullptr);
    EnableWindow(GetDlgItem(dlg, IDC_ADV_ITALIC), it.font != nullptr);
    HWND fc = GetDlgItem(dlg, IDC_ADV_FONT), fz = GetDlgItem(dlg, IDC_ADV_FONTSIZE);
    SendMessageW(fc, CB_RESETCONTENT, 0, 0);
    SendMessageW(fz, CB_RESETCONTENT, 0, 0);
    if (it.font) {
        FontSpec f = parse_font(look_metric(adv_rec, it.font));
        load_faces();
        std::vector<std::string> faces = font_faces;
        if (std::find(faces.begin(), faces.end(), f.face) == faces.end()) faces.insert(faces.begin(), f.face);
        int sel = 0;
        for (size_t i = 0; i < faces.size(); i++) {
            std::u16string w = W(faces[i]);
            SendMessageW(fc, CB_ADDSTRING, 0, (LPARAM)w.c_str());
            if (faces[i] == f.face) sel = (int)i;
        }
        SendMessageW(fc, CB_SETCURSEL, (WPARAM)sel, 0);
        static const int sizes[] = { 6, 7, 8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36 };
        int pts = font_points(f), zs = -1, k = 0;
        for (int s : sizes) {
            if (zs < 0 && s >= pts) { if (s != pts) { std::u16string w = W(std::to_string(pts)); SendMessageW(fz, CB_ADDSTRING, 0, (LPARAM)w.c_str()); k++; } zs = k - (s != pts ? 1 : 0); }
            std::u16string w = W(std::to_string(s));
            SendMessageW(fz, CB_ADDSTRING, 0, (LPARAM)w.c_str());
            k++;
        }
        if (zs < 0) { std::u16string w = W(std::to_string(pts)); SendMessageW(fz, CB_ADDSTRING, 0, (LPARAM)w.c_str()); zs = k; }
        SendMessageW(fz, CB_SETCURSEL, (WPARAM)zs, 0);
        CheckDlgButton(dlg, IDC_ADV_BOLD, f.bold ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_ADV_ITALIC, f.italic ? BST_CHECKED : BST_UNCHECKED);
    }
    for (int id : { IDC_ADV_COLOR1, IDC_ADV_COLOR2, IDC_ADV_FONTCOLOR }) InvalidateRect(GetDlgItem(dlg, id), nullptr, FALSE);
    InvalidateRect(GetDlgItem(dlg, IDC_ADV_PREVIEW), nullptr, FALSE);
    adv_filling = false;
}

void make_adv_sample(Pixels& p) { classic_sample(adv_rec, p, true); }

// the font of the item from the controls
void adv_font_changed(HWND dlg)
{
    const AdvItem& it = kAdv[adv_item];
    if (!it.font || adv_filling) return;
    FontSpec f = parse_font(look_metric(adv_rec, it.font));
    int i = (int)SendDlgItemMessageW(dlg, IDC_ADV_FONT, CB_GETCURSEL, 0, 0);
    if (i >= 0) {
        WCHAR buf[128] = {};
        SendDlgItemMessageW(dlg, IDC_ADV_FONT, CB_GETLBTEXT, (WPARAM)i, (LPARAM)buf);
        f.face = w32::utf8_of(buf);
    }
    i = (int)SendDlgItemMessageW(dlg, IDC_ADV_FONTSIZE, CB_GETCURSEL, 0, 0);
    if (i >= 0) {
        WCHAR buf[16] = {};
        SendDlgItemMessageW(dlg, IDC_ADV_FONTSIZE, CB_GETLBTEXT, (WPARAM)i, (LPARAM)buf);
        int pts = atoi(w32::utf8_of(buf).c_str());
        if (pts > 0) f.height = -MulDiv(pts, 96, 72);
    }
    f.bold = IsDlgButtonChecked(dlg, IDC_ADV_BOLD) == BST_CHECKED;
    f.italic = IsDlgButtonChecked(dlg, IDC_ADV_ITALIC) == BST_CHECKED;
    adv_set(std::string("Metrics.") + it.font, font_value(f));
    InvalidateRect(GetDlgItem(dlg, IDC_ADV_PREVIEW), nullptr, FALSE);
}

// the colour picker: XP's 20 static colours and R, G, B
COLORREF pick_result;
const COLORREF kSwatches[20] = {
    RGB(0, 0, 0), RGB(128, 0, 0), RGB(0, 128, 0), RGB(128, 128, 0), RGB(0, 0, 128), RGB(128, 0, 128), RGB(0, 128, 128),
    RGB(192, 192, 192), RGB(192, 220, 192), RGB(166, 202, 240), RGB(255, 251, 240), RGB(160, 160, 164), RGB(128, 128, 128),
    RGB(255, 0, 0), RGB(0, 255, 0), RGB(255, 255, 0), RGB(0, 0, 255), RGB(255, 0, 255), RGB(0, 255, 255), RGB(255, 255, 255) };

void draw_swatch(const DRAWITEMSTRUCT* di, COLORREF c, bool enabled)
{
    RECT r = di->rcItem;
    HBRUSH face = GetSysColorBrush(COLOR_BTNFACE);
    FillRect(di->hDC, &r, face);
    DrawEdge(di->hDC, &r, (di->itemState & ODS_SELECTED) ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
    if (enabled) {
        RECT in = { r.left + 4, r.top + 3, r.right - 4, r.bottom - 3 };
        HBRUSH b = CreateSolidBrush(c);
        FillRect(di->hDC, &in, b);
        DeleteObject(b);
        FrameRect(di->hDC, &in, (HBRUSH)GetStockObject(BLACK_BRUSH));
    }
    if (di->itemState & ODS_FOCUS) { RECT f = { r.left + 2, r.top + 2, r.right - 2, r.bottom - 2 }; DrawFocusRect(di->hDC, &f); }
}

void cp_show(HWND dlg, COLORREF c)
{
    adv_filling = true;
    set_text(dlg, IDC_CP_R, std::to_string(GetRValue(c)));
    set_text(dlg, IDC_CP_G, std::to_string(GetGValue(c)));
    set_text(dlg, IDC_CP_B, std::to_string(GetBValue(c)));
    adv_filling = false;
    pick_result = c;
    InvalidateRect(GetDlgItem(dlg, IDC_CP_SAMPLE), nullptr, FALSE);
}

INT_PTR CALLBACK colorpick_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: cp_show(dlg, (COLORREF)lp); return TRUE;
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT* di = (const DRAWITEMSTRUCT*)lp;
        if (wp >= IDC_CP_SWATCH0 && wp < IDC_CP_SWATCH0 + 20) { draw_swatch(di, kSwatches[wp - IDC_CP_SWATCH0], true); return TRUE; }
        if (wp == IDC_CP_SAMPLE) {
            HBRUSH b = CreateSolidBrush(pick_result);
            FillRect(di->hDC, &di->rcItem, b);
            DeleteObject(b);
            FrameRect(di->hDC, &di->rcItem, (HBRUSH)GetStockObject(BLACK_BRUSH));
            return TRUE;
        }
        return FALSE;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id >= IDC_CP_SWATCH0 && id < IDC_CP_SWATCH0 + 20) { cp_show(dlg, kSwatches[id - IDC_CP_SWATCH0]); return TRUE; }
        if ((id == IDC_CP_R || id == IDC_CP_G || id == IDC_CP_B) && HIWORD(wp) == EN_CHANGE && !adv_filling) {
            int r = (std::min)(255, (int)GetDlgItemInt(dlg, IDC_CP_R, nullptr, FALSE)), g = (std::min)(255, (int)GetDlgItemInt(dlg, IDC_CP_G, nullptr, FALSE)),
                b = (std::min)(255, (int)GetDlgItemInt(dlg, IDC_CP_B, nullptr, FALSE));
            pick_result = RGB(r, g, b);
            InvalidateRect(GetDlgItem(dlg, IDC_CP_SAMPLE), nullptr, FALSE);
            return TRUE;
        }
        if (id == IDOK) { EndDialog(dlg, IDOK); return TRUE; }
        if (id == IDCANCEL) { EndDialog(dlg, IDCANCEL); return TRUE; }
        return FALSE;
    }
    }
    return FALSE;
}

void adv_pick_color(HWND dlg, const char* name)
{
    if (!name) return;
    if (DialogBoxParamW(module(), MAKEINTRESOURCEW(IDD_COLORPICK), dlg, colorpick_proc, (LPARAM)adv_color(name)) != IDOK) return;
    adv_set(std::string("Colors.") + name, rgb_text(pick_result));
    adv_show(dlg);
}

INT_PTR CALLBACK advanced_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        adv_layer = cur.layer;
        adv_recompute();
        HWND c = GetDlgItem(dlg, IDC_ADV_ITEM);
        for (int i = 0; i < kAdvCount; i++) { std::u16string w = W(kAdv[i].label); SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)w.c_str()); }
        SendMessageW(c, CB_SETCURSEL, (WPARAM)adv_item, 0);
        SendDlgItemMessageW(dlg, IDC_ADV_SIZEUD, UDM_SETRANGE32, 0, 100);
        adv_show(dlg);
        return TRUE;
    }
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT* di = (const DRAWITEMSTRUCT*)lp;
        const AdvItem& it = kAdv[adv_item];
        if (wp == IDC_ADV_PREVIEW) { paint_pixels(di, make_adv_sample); return TRUE; }
        if (wp == IDC_ADV_COLOR1) { draw_swatch(di, it.color1 ? adv_color(it.color1) : 0, it.color1 != nullptr); return TRUE; }
        if (wp == IDC_ADV_COLOR2) { draw_swatch(di, it.color2 ? adv_color(it.color2) : 0, it.color2 != nullptr); return TRUE; }
        if (wp == IDC_ADV_FONTCOLOR) { draw_swatch(di, it.fontcolor ? adv_color(it.fontcolor) : 0, it.fontcolor != nullptr); return TRUE; }
        return FALSE;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        const AdvItem& it = kAdv[adv_item];
        switch (id) {
        case IDC_ADV_ITEM:
            if (code == CBN_SELCHANGE) {
                int i = (int)SendDlgItemMessageW(dlg, IDC_ADV_ITEM, CB_GETCURSEL, 0, 0);
                if (i >= 0 && i < kAdvCount) { adv_item = i; adv_show(dlg); }
            }
            return TRUE;
        case IDC_ADV_SIZE:
            if (code == EN_CHANGE && !adv_filling && it.size) {
                // the text, not GetDlgItemInt: the shim's returned "2" for "26" here (with the up-down buddy)
                std::string t = get_text(GetDlgItem(dlg, IDC_ADV_SIZE));
                int v = atoi(t.c_str());
                if (v > 0 && v <= 100) {
                    adv_set(std::string("Metrics.") + it.size, std::to_string(v));
                    if (it.size2) adv_set(std::string("Metrics.") + it.size2, std::to_string(v));
                    InvalidateRect(GetDlgItem(dlg, IDC_ADV_PREVIEW), nullptr, FALSE);
                }
            }
            return TRUE;
        case IDC_ADV_COLOR1: adv_pick_color(dlg, it.color1); return TRUE;
        case IDC_ADV_COLOR2: adv_pick_color(dlg, it.color2); return TRUE;
        case IDC_ADV_FONTCOLOR: adv_pick_color(dlg, it.fontcolor); return TRUE;
        case IDC_ADV_FONT:
        case IDC_ADV_FONTSIZE:
            if (code == CBN_SELCHANGE) adv_font_changed(dlg);
            return TRUE;
        case IDC_ADV_BOLD:
        case IDC_ADV_ITALIC:
            adv_font_changed(dlg);
            return TRUE;
        case IDOK: EndDialog(dlg, IDOK); return TRUE;
        case IDCANCEL: EndDialog(dlg, IDCANCEL); return TRUE;
        }
        return FALSE;
    }
    }
    return FALSE;
}

void advanced(HWND dlg)
{
    if (DialogBoxParamW(module(), MAKEINTRESOURCEW(IDD_ADVANCED), GetParent(dlg), advanced_proc, 0) != IDOK) return;
    // the edited Colors.* / Metrics.* into the pending layer (only the current layer is ever written)
    for (auto& x : adv_layer) if (!cur.layer.count(x.first) || cur.layer[x.first] != x.second) set_key(x.first, x.second);
    refresh_all();
    changed(dlg);
}

INT_PTR CALLBACK appearance_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    INT_PTR r;
    if (common(dlg, msg, lp, r)) return r;
    switch (msg) {
    case WM_INITDIALOG:
        pages[2] = dlg;
        SendDlgItemMessageW(dlg, IDC_AP_STYLE, CB_SETMINVISIBLE, 16, 0);   // a scrolled list, not one as tall as the screen
        watch_settings(dlg);
        fill_appearance(dlg);
        return TRUE;
    case WM_DRAWITEM:
        if (wp == IDC_AP_PREVIEW) { paint_pixels((const DRAWITEMSTRUCT*)lp, make_appearance_sample); return TRUE; }
        return FALSE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_AP_STYLE:
        case IDC_AP_COLOR:
        case IDC_AP_SIZE:
            if (HIWORD(wp) == CBN_SELCHANGE && !filling) {
                bool classic = is_classic(rec);
                if (LOWORD(wp) == IDC_AP_STYLE) pick_style(list_value(dlg, IDC_AP_STYLE, style_items, classic ? "classic" : value("VisualStyle.style")));
                else if (LOWORD(wp) == IDC_AP_COLOR) {
                    if (classic) set_key("VisualStyle.scheme", list_value(dlg, IDC_AP_COLOR, color_items, value("VisualStyle.scheme")));
                    else set_key("VisualStyle.color", list_value(dlg, IDC_AP_COLOR, color_items, value("VisualStyle.color")));
                    if (classic) {   // a scheme without the size chosen: its first
                        std::vector<Item> sz = scheme_sizes(cur.layer.count("VisualStyle.scheme") ? cur.layer["VisualStyle.scheme"] : value("VisualStyle.scheme"));
                        bool ok = false;
                        for (const Item& i : sz) ok = ok || i.value == value("VisualStyle.size");
                        if (!ok) set_key("VisualStyle.size", sz[0].value);
                    }
                } else set_key("VisualStyle.size", list_value(dlg, IDC_AP_SIZE, size_items, value("VisualStyle.size")));
                refresh_all();
                changed(dlg);
            }
            return TRUE;
        case IDC_AP_EFFECTS: {
            State was = cur;
            DialogBoxParamW(module(), MAKEINTRESOURCEW(IDD_EFFECTS), GetParent(dlg), effects_proc, 0);
            if (cur.smoothing != was.smoothing || cur.shadows != was.shadows || cur.cues != was.cues) changed(dlg);
            return TRUE;
        }
        case IDC_AP_ADVANCED: advanced(dlg); return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

// ---- Shell: the Longhorn extras (theme keys) and the sidebar (per user) ---------------

struct ShellKey { const char* key; int id; };
const ShellKey kShell[] = {
    { "Explorer.chrome", IDC_AP_FRAME }, { "Explorer.navbar", IDC_AP_NAVBAR }, { "Explorer.navbar_preset", IDC_AP_PRESET },
    { "Shell.taskbar", IDC_AP_TASKBAR }, { "Glass.frames", IDC_AP_FRAMES },
    { "Glass.composition", IDC_AP_COMP }, { "Glass.animations", IDC_AP_ANIM } };
std::map<int, std::vector<Item>> shell_items;
const std::vector<Item> kSbModes = { { "Full", "full" }, { "Minimized", "minimized" }, { "Pinned", "pinned" }, { "None", "none" } };
// SETTINGS.md [sidebar] stacking: luxshell's two labels
const std::vector<Item> kSbStacks = { { "On top of windows", "top" }, { "Under windows (desktop)", "bottom" } };

std::vector<Item> items_for(int id)
{
    switch (id) {
    case IDC_AP_FRAME: return items({ { "Automatic (Longhorn when luxwm runs)", "auto" }, { "Windows XP", "xp" }, { "Longhorn header", "longhorn" } });
    case IDC_AP_NAVBAR: return items({ { "Longhorn 4074", "4074" }, { "Longhorn 4051", "4051" }, { "Vision (luxnav)", "vision" }, { "None (XP toolbars)", "none" } });
    case IDC_AP_PRESET:
        if (value("Explorer.navbar") == "vision") return items({ { "Automatic", "auto" }, { "Small", "small" }, { "Big", "big" }, { "Concept", "concept" } });
        return items({ { "Automatic", "auto" }, { "Small", "small" }, { "Medium", "medium" }, { "Large", "large" }, { "Extra large", "xlarge" } });
    case IDC_AP_TASKBAR: return items({ { "Avalon (luxshell)", "avalon" }, { "Classic (flxp-taskbar)", "classic" }, { "None", "none" } });
    case IDC_AP_FRAMES: {
        std::vector<Item> out = { { "None (the style's own frames)", "none" } };
        for (const std::string& d : shell::lhde_subdirs("frames")) {
            std::string n = shell::base_of(d);
            out.push_back(Item{ n, n });
        }
        return out;
    }
    case IDC_AP_COMP: return items({ { "Automatic", "auto" }, { "On", "1" }, { "Off", "0" } });
    default: return items({ { "4074 plus", "4074plus" }, { "4074", "4074" }, { "Invented", "invented" }, { "5048", "5048" }, { "None", "none" } });
    }
}

void fill_shell(HWND dlg)
{
    filling = true;
    for (const ShellKey& k : kShell) {
        shell_items[k.id] = items_for(k.id);
        std::string v = value(k.key);
        if (k.id == IDC_AP_FRAME && v.empty()) v = "auto";
        fill_list(dlg, k.id, shell_items[k.id], v);
        set_text(dlg, k.id + 20, source_text(k.key));
    }
    EnableWindow(GetDlgItem(dlg, IDC_AP_PRESET), value("Explorer.navbar") != "none");
    std::string mode = cur.sbmode.empty() ? value("Shell.sidebar") : cur.sbmode;
    std::vector<Item> m = kSbModes, s = kSbStacks;
    fill_list(dlg, IDC_AP_SBMODE, m, mode.empty() ? "full" : mode);
    set_text(dlg, IDC_AP_SBMODE_SRC, cur.sbmode != committed.sbmode ? "Changed: Apply keeps it for every theme"
                                     : cur.sbmode.empty() ? "The theme's (Shell.sidebar); yours once changed" : "Your setting, for every theme");
    fill_list(dlg, IDC_AP_SBSTACK, s, cur.sbstack);
    CheckDlgButton(dlg, IDC_AP_SBCLASSIC, cur.sbclassic == "1" ? BST_CHECKED : BST_UNCHECKED);
    filling = false;
}

INT_PTR CALLBACK shell_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    INT_PTR r;
    if (common(dlg, msg, lp, r)) return r;
    switch (msg) {
    case WM_INITDIALOG:
        pages[3] = dlg;
        watch_settings(dlg);
        fill_shell(dlg);
        return TRUE;
    case WM_COMMAND:
        if ((HIWORD(wp) == CBN_SELCHANGE || (HIWORD(wp) == BN_CLICKED && LOWORD(wp) == IDC_AP_SBCLASSIC)) && !filling) {
            int id = LOWORD(wp);
            for (const ShellKey& k : kShell)
                if (k.id == id) set_key(k.key, list_value(dlg, id, shell_items[id], value(k.key)));
            if (id == IDC_AP_SBMODE) cur.sbmode = list_value(dlg, id, kSbModes, cur.sbmode);
            if (id == IDC_AP_SBSTACK) cur.sbstack = list_value(dlg, id, kSbStacks, cur.sbstack);
            if (id == IDC_AP_SBCLASSIC) cur.sbclassic = IsDlgButtonChecked(dlg, IDC_AP_SBCLASSIC) == BST_CHECKED ? "1" : "0";
            refresh_all();
            changed(dlg);
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

// --list desktop: the Desktop tab's lists as the dialog fills them, without a window (tests/settings-lists.sh): every
// "wallpaper LABEL"; the entry in force marked with a leading "*"
void list_desktop()
{
    build_walls();
    int sel = wall_selected();
    for (size_t i = 0; i < walls.size(); i++) printf("%swallpaper %s\n", (int)i == sel ? "*" : "", walls[i].label.c_str());
}

void usage(FILE* f)
{
    fputs("usage: flxp-settings [--tab themes|desktop|appearance|shell | --list desktop]\n"
          "Display Properties: the session theme (theme files; your changes are settings.ini [current], \"NAME (Modified)\",\n"
          "until Save As writes ~/.config/lhde/themes/NAME.theme) and the per-user settings (~/.config/lhde/settings.ini).\n", f);
}

int tab_of(const std::string& t)
{
    return t == "desktop" ? 1 : t == "appearance" || t == "effects" ? 2 : t == "shell" ? 3 : 0;
}

} // namespace

int main(int argc, char** argv)
{
    int start = 0;
    bool list = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if ((a == "--tab" || a == "-t") && i + 1 < argc) start = tab_of(argv[++i]);
        else if (a == "--list" && i + 1 < argc && !strcmp(argv[i + 1], "desktop")) { list = true; i++; }
        else if (a.rfind("--tab=", 0) == 0) start = tab_of(a.substr(6));
        else if (a == "--help" || a == "-h") { usage(stdout); return 0; }
        else { usage(stderr); return 2; }
    }
    if (list) { cur = load_state(); recompute(); list_desktop(); return 0; }   // no display needed
    shell::setup_fonts();
    shell::setup_theme();
    w32::init();
    committed = cur = load_state();
    recompute();
    sample_theme = rec;

    static const struct { int id; DLGPROC proc; } defs[] = {
        { IDD_THEMES, themes_proc }, { IDD_DESKTOP, desktop_proc }, { IDD_APPEARANCE, appearance_proc }, { IDD_SHELL, shell_proc },
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
