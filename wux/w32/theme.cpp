// Visual styles: wiring the vendored uxtheme into the shim. A theme is
// selected the way Windows does it, through the ThemeManager registry key,
// which here lives in the shim's in-memory registry. The Longhorn session
// theme (root _LH_THEME, shell/lhtheme) is followed at run time: a change
// reloads the style and sends WM_THEMECHANGED to every window.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <string>
#include <vector>
#include "w32.h"
#include "kernel.h"
#include "image.h"
#include "shell/lhtheme.h"
#include "shell/config.h"
#include "gdi/font.h"
extern "C" {
#include "winreg.h"
void UXTHEME_InitSystem(HINSTANCE hInst);
void UXTHEME_UninitSystem(void);
void UXTHEME_ReloadTheme(void);
}

namespace w32 {

static bool g_theme_inited;
static std::string g_path, g_color, g_size;     // what is loaded

// "luna" -> the first <dir>/luna/luna.msstyles found in the usual places.
static std::string resolve_theme(const char* name)
{
    std::string n = name;
    if (n.find('/') != std::string::npos) return n;
    std::vector<std::string> dirs;
    // theme_path= in flxp.ini (or $FLXP_THEME_PATH), then wine's and ~/.themes
    { std::string p = shell::setting("theme_path"); size_t a = 0; while (a <= p.size()) { size_t b = p.find(':', a); if (b == std::string::npos) b = p.size(); if (b > a) dirs.push_back(p.substr(a, b - a)); a = b + 1; } }
    if (const char* h = getenv("HOME")) {
        dirs.push_back(std::string(h) + "/.wine/drive_c/windows/resources/themes");
        dirs.push_back(std::string(h) + "/.themes");
    }
    std::string lower = n; for (char& c : lower) if (c >= 'A' && c <= 'Z') c += 32;
    std::string upper = lower; upper[0] = (char)toupper(upper[0]);
    for (const std::string& d : dirs)
        for (const std::string& v : {n, lower, upper}) {
            std::string f = d + "/" + v + "/" + lower + ".msstyles";
            FILE* t = fopen(f.c_str(), "rb");
            if (t) { fclose(t); return f; }
        }
    return n;
}

bool set_theme(const char* path_or_name, const char* color, const char* size)
{
    std::string resolved = path_or_name ? resolve_theme(path_or_name) : std::string();
    const char* path = resolved.c_str();
    const char* key = "Software\\Microsoft\\Windows\\CurrentVersion\\ThemeManager";
    if (!path || !*path) {
        reg_set_string(HKEY_CURRENT_USER, key, "ThemeActive", "0");
        reg_set_string(HKEY_CURRENT_USER, key, "DllName", "");
    } else {
        reg_set_string(HKEY_CURRENT_USER, key, "DllName", path);
        reg_set_string(HKEY_CURRENT_USER, key, "ColorName", color ? color : "");
        reg_set_string(HKEY_CURRENT_USER, key, "SizeName", size ? size : "");
        reg_set_string(HKEY_CURRENT_USER, key, "ThemeActive", "1");
    }
    g_path = path ? path : ""; g_color = color ? color : ""; g_size = size ? size : "";
    if (g_theme_inited) UXTHEME_ReloadTheme();      // load it, WM_THEMECHANGED to every window
    return true;
}

static void set_env(const char* var, const std::string& v)
{
    if (v.empty()) unsetenv(var); else setenv(var, v.c_str(), 1);
}

static BOOL CALLBACK post_theme_changed(HWND h, LPARAM top)
{
    PostMessageW(h, WM_THEMECHANGED, 0, 0);
    if (top) EnumChildWindows(h, post_theme_changed, 0);
    return TRUE;
}

static BOOL CALLBACK redraw_all(HWND h, LPARAM)
{
    RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    return TRUE;
}

// The session theme changed (or the registry path): look it up and reload the
// visual style if it is another one. Windows: the theme service's
// WM_THEMECHANGED; here the root PropertyNotify, coalesced by the toolkit.
static void session_theme_changed()
{
    std::string name = win::root_string("_LH_THEME"), ini = win::root_string("_LH_THEMES_INI");
    if (name.empty()) return;                        // nobody publishes a theme: keep ours
    const shell::SessionTheme& cur = shell::session_theme();
    shell::SessionTheme t;
    if (!shell::lookup_session_theme(name, ini, t)) {
        fprintf(stderr, shell::is_preset_name(name) ? "flxp: preset %s: no base theme in %s; keeping %s\n"
                : "flxp: session theme \"%s\" is not in %s; keeping %s\n", name.c_str(), ini.c_str(),
                cur.name.empty() ? "the current style" : cur.name.c_str());
        return;
    }
    if (t.name == cur.name && t.ini == cur.ini && t.value("base") == cur.value("base")) return;   // an alias replaced by its name, a rewrite
    shell::set_session_theme(t);
    set_env("FLXP_THEME_FOLLOWED", t.name);               // children follow too
    bool style = !t.msstyles.empty() && (t.msstyles != g_path || t.color != g_color || t.size != g_size);
    if (t.msstyles.empty())
        fprintf(stderr, "flxp: no visual style flxp can load for session theme %s; keeping the current one\n", t.name.c_str());
    if (style) {
        set_env("FLXP_THEME", t.msstyles); set_env("FLXP_THEME_COLOR", t.color); set_env("FLXP_THEME_SIZE", t.size);
        reset_syscolors();                               // the new style's [SysMetrics] start from classic
        set_theme(t.msstyles.c_str(), t.color.c_str(), t.size.c_str());
        if (getenv("FLXP_DEBUG")) fprintf(stderr, "flxp: session theme %s: %s (%s/%s), window text %06x\n", t.name.c_str(),
                                          t.msstyles.c_str(), t.color.c_str(), t.size.c_str(), (unsigned)GetSysColor(COLOR_WINDOWTEXT));
    } else {
        // the same style (aero4074 and jade4074 both load Jade): still a theme
        // change for the windows (flxp_* keys, lhwm's frame)
        EnumWindows(post_theme_changed, 1);
    }
    // repaint everything: system colours, non-client areas, unthemed parts
    EnumWindows(redraw_all, 0);
}

// _LH_SETTINGS_CHANGED (x11/SETTINGS.md): the settings every program follows
// live. Font smoothing ([fonts] smoothing, under flxp.ini's font_smoothing):
// the glyph caches are keyed by it, so a repaint is all it takes.
static void settings_changed()
{
    std::string sm = shell::setting("font_smoothing");
    int mode = sm == "off" || sm == "0" ? gdi::SMOOTH_OFF : sm == "cleartype" ? gdi::SMOOTH_CLEARTYPE : gdi::SMOOTH_STANDARD;
    if (mode == gdi::font_smoothing()) return;
    gdi::set_font_smoothing(mode);
    EnumWindows(redraw_all, 0);
}

void init_theme()
{
    if (g_theme_inited) return;
    const char* t = getenv("FLXP_THEME");
    if (t && *t) set_theme(t, getenv("FLXP_THEME_COLOR"), getenv("FLXP_THEME_SIZE"));
    UXTHEME_InitSystem(nullptr);
    g_theme_inited = true;
    win::on_root_property_change("_LH_SETTINGS_CHANGED", settings_changed);
    if (shell::follows_session_theme()) {
        win::on_root_property_change("_LH_THEME", session_theme_changed);
        win::on_root_property_change("_LH_THEMES_INI", session_theme_changed);
        win::on_root_property_change("_LH_THEME_RESOLVED", session_theme_changed);   // a preset's base, from lhwm/lux
    }
}

} // namespace w32
