// Visual styles: wiring the vendored uxtheme into the shim. A theme is
// selected the way Windows does it, through the ThemeManager registry key,
// which here lives in the shim's in-memory registry. The session theme (root
// _LH_THEME resolved by liblhde, shell/lhtheme) is followed at run time: a
// change reloads the style and sends WM_THEMECHANGED to every window.
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
#include "shell/runtime.h"
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

// "luna" -> the first <dir>/luna/luna.msstyles (or lhde's styles/luna-xp.msstyles, ...) found in
// the usual places: shell::find_msstyles (theme_path=, lhde's styles/, wine's and ~/.themes)
static std::string resolve_theme(const char* name)
{
    std::string n = name;
    if (n.find('/') != std::string::npos) return n;
    std::string f = shell::find_msstyles(n);
    return f.empty() ? n : f;
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

// the record without the keys only flxp-desktop reads (the wallpaper) and the theme's identity (a switch to
// NAME-modified.theme for a new wallpaper): a change there is no theme change for the windows
static std::string look_of(const shell::SessionTheme& t)
{
    static const char* const skip[] = { "path.wallpaper", "theme", "file", "chain", "Theme.name", "Theme.base", "Theme.aliases" };
    std::string out;
    for (auto& kv : t.keys) {
        bool own = kv.first.compare(0, 8, "Desktop.") == 0 && kv.first != "Desktop.icons";
        for (const char* k : skip) own = own || kv.first == k;
        if (!own) out += kv.first + "=" + kv.second + "\n";
    }
    return out;
}

// The session theme changed (_LH_THEME, the published record, or a theme file: _LH_SETTINGS_CHANGED): resolve it
// (lhtheme.h) and reload the visual style if it is another one. Windows: the theme service's WM_THEMECHANGED; here
// the root PropertyNotify, coalesced by the toolkit.
void session_theme_refresh()
{
    if (!shell::follows_session_theme()) return;
    std::string name = win::root_string("_LH_THEME");
    if (name.empty()) return;                        // nobody publishes a theme: keep ours
    long serial = 0;
    if (win::root_cardinals("_LH_SETTINGS_SERIAL", &serial, 1) != 1) serial = 0;
    const shell::SessionTheme& cur = shell::session_theme();
    shell::SessionTheme t;
    std::string err;
    if (!shell::session_theme_from(name, win::root_string("_LH_THEME_RESOLVED"), (unsigned long)serial, t, &err)) {
        fprintf(stderr, "flxp: session theme \"%s\": %s; keeping %s\n", name.c_str(), err.c_str(),
                cur.name.empty() ? "the current style" : cur.name.c_str());
        return;
    }
    if (t.body == cur.body) return;                  // the same record (republished at another serial)
    bool look = look_of(t) != look_of(cur);
    shell::set_session_theme(t);
    set_env("FLXP_THEME_FOLLOWED", t.name);               // children follow too
    if (!look) return;                                    // the wallpaper only: flxp-desktop's business
    bool style = t.classic ? !g_path.empty()
                           : !t.msstyles.empty() && (t.msstyles != g_path || t.color != g_color || t.size != g_size);
    if (t.msstyles.empty() && !t.classic)
        fprintf(stderr, "flxp: no visual style for session theme %s (%s); keeping the current one\n", t.name.c_str(),
                t.value("VisualStyle.style").c_str());
    if (style) {
        set_env("FLXP_THEME", t.msstyles); set_env("FLXP_THEME_COLOR", t.color); set_env("FLXP_THEME_SIZE", t.size);
        reset_syscolors();                               // the new style's [SysMetrics] start from classic
        set_theme(t.classic ? nullptr : t.msstyles.c_str(), t.color.c_str(), t.size.c_str());
        if (getenv("FLXP_DEBUG")) fprintf(stderr, "flxp: session theme %s: %s (%s/%s), window text %06x\n", t.name.c_str(),
                                          t.classic ? "classic" : t.msstyles.c_str(), t.color.c_str(), t.size.c_str(),
                                          (unsigned)GetSysColor(COLOR_WINDOWTEXT));
    } else {
        // the same style (aero4074 and aerovision both load aero-patch): still a theme
        // change for the windows (chrome, navbar, icons, luxwm's frame)
        EnumWindows(post_theme_changed, 1);
    }
    // repaint everything: system colours, non-client areas, unthemed parts
    EnumWindows(redraw_all, 0);
}

// _LH_SETTINGS_CHANGED (docs/SETTINGS.md): the settings every program follows
// live. A theme file may have changed (NAME-modified.theme): the record again.
// Font smoothing (settings.ini [fonts] smoothing, else the theme's
// Fonts.smoothing, under flxp.ini's font_smoothing): the glyph caches are keyed
// by it, so a repaint is all it takes.
static void settings_changed()
{
    session_theme_refresh();
    std::string sm = shell::theme_setting("font_smoothing");
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
        win::on_root_property_change("_LH_THEME", session_theme_refresh);
        win::on_root_property_change("_LH_THEME_RESOLVED", session_theme_refresh);   // luxwm's / luxshell's record
    }
}

} // namespace w32
