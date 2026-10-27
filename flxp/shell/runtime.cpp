#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <fontconfig/fontconfig.h>
#include "runtime.h"
#include "config.h"
#include "gdi/font.h"
#include "lhtheme.h"

namespace shell {

static bool exists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }

static std::vector<std::string> split_path(const std::string& s)
{
    std::vector<std::string> out;
    size_t a = 0;
    while (a <= s.size()) {
        size_t b = s.find(':', a);
        if (b == std::string::npos) b = s.size();
        if (b > a) {
            std::string d = s.substr(a, b - a);
            if (d[0] == '~') d = home_dir() + d.substr(1);
            out.push_back(d);
        }
        a = b + 1;
    }
    return out;
}

static const char template_text[] =
    "# flxp settings. An empty value means automatic. Any key can be overridden\n"
    "# for one run by the environment variable FLXP_<KEY> (FLXP_THEME=classic).\n"
    "[flxp]\n"
    "\n"
    "# Extra font directories (colon-separated), searched before the system's.\n"
    "# flxp draws with Tahoma, Microsoft Sans Serif and Marlett (Trebuchet MS and\n"
    "# Verdana for some visual styles). Auto: fontconfig, then Wine's font\n"
    "# directories, then whatever sans font fontconfig offers. Real Windows fonts\n"
    "# can simply go into ~/.fonts instead.\n"
    "font_path =\n"
    "\n"
    "# Smooth edges of screen fonts (XP's Display Properties > Appearance >\n"
    "# Effects): off, standard or cleartype (auto: standard, XP's default).\n"
    "# Standard is grayscale where the font's gasp table allows it: Tahoma 8 stays\n"
    "# crisp, the Luna start button and captions are smoothed. cleartype is drawn\n"
    "# as standard for now.\n"
    "font_smoothing =\n"
    "# Icon theme: xp, 2k, longhorn, wine, or a path to a .theme file.\n"
    "# Auto: xp, falling back to wine when its files cannot be found.\n"
    "icons =\n"
    "# The Windows XP SP1 source tree (the xp and 2k icon themes read its .ico/.bmp\n"
    "# files; not needed after 'make install-icons').\n"
    "nt5src =\n"
    "# An extracted Longhorn WINDOWS/system32 (the longhorn icon theme).\n"
    "longhorn_system32 =\n"
    "# A directory of extracted Longhorn builds, <build>/<n>/WINDOWS/... (the\n"
    "# longhorn header's images when longhorn_system32= does not have them).\n"
    "longhorn_wim =\n"
    "\n"
    "# Visual style: luna, another name found in the theme directories, a\n"
    "# .msstyles path, or classic. Auto: luna if it can be found, else classic.\n"
    "theme =\n"
    "# Colour scheme and size inside the .msstyles (auto: the style's default).\n"
    "theme_color =\n"
    "theme_size =\n"
    "# More directories holding <name>/<name>.msstyles (colon-separated); always\n"
    "# searched: ~/.local/share/flxp/themes, <datadir>/themes,\n"
    "# ~/.wine/drive_c/windows/resources/themes, ~/.themes.\n"
    "theme_path =\n"
    "# Follow the Longhorn session theme (the root property _LH_THEME, lhwm's\n"
    "# themes.ini): its visual style replaces theme= and changes live with it;\n"
    "# 0 = keep theme= (auto: 1). --theme for one run does not follow either.\n"
    "theme_follow =\n"
    "\n"
    "# Toolbar buttons: large or small (auto: large).\n"
    "toolbar_icons =\n"
    "# The window's client size and position, WxH+X+Y.\n"
    "geometry =\n"
    "# Open Terminal Here: a /bin/sh command, %d = the quoted directory (auto: urxvt).\n"
    "terminal =\n"
    "# Window frame: xp (the window manager's frame), longhorn (the Longhorn 4074\n"
    "# header with Back/Forward, title, breadcrumb and search box, on lhwm's glass\n"
    "# when it composites, else opaque in the colours of lhwm's caption or Slate,\n"
    "# and the 4051 menu/command bar), auto: longhorn when lhwm runs at start.\n"
    "frame =\n"
    "# The longhorn frame's menu bar row: the folder-type gradient of Longhorn's\n"
    "# slate.msstyles (default, document, music, picture, video, contact, game;\n"
    "# auto: document, read from longhorn_system32/../Resources/Themes/Slate) or\n"
    "# gradient (drawn: menubar_gradient = edge centre [right], #rrggbb each;\n"
    "# auto: #24438e #5572bb). menubar_text: #rrggbb (auto: white).\n"
    "# menubar_msstyles: another Longhorn slate.msstyles (4051 or 4074).\n"
    "menubar_style =\n"
    "menubar_gradient =\n"
    "menubar_text =\n"
    "menubar_msstyles =\n"
    "# The longhorn header (View > Header; 4074's Toolbar Size and Toolbars):\n"
    "# small (36 rows under the caption, which keeps the title), medium (4051's\n"
    "# 54-row navigation bar), large, xlarge (32 / 49 more rows); auto: medium (the\n"
    "# file dialog: filedlg_header_size, auto small). The elements: 1 or 0 (auto: 1).\n"
    "header_size =\n"
    "header_buttons =\n"
    "header_title =\n"
    "header_breadcrumb =\n"
    "header_search =\n"
    "header_favorites =\n"
    "# The small size's Back/Forward: even (the style's same-size Small buttons,\n"
    "# 4074), uneven (the big pair scaled: bigger Back, smaller Forward, 4051 and\n"
    "# the concepts); auto: even where the style has Small buttons, else uneven.\n"
    "header_small_buttons =\n"
    "# Which header draws: lhnav (the concept art's vision header, ~/fun/longhorn/\n"
    "# x11/NAVBAR.md), flxp (the 4051/4074 header above); auto: lhnav when lhwm's\n"
    "# frame theme has a nav.ini (Aero vision). header_preset: one of nav.ini's\n"
    "# [preset NAME] sections (auto: nav.ini's own; the Small size: small).\n"
    "# header_crumb_menu: siblings (4074, auto) or children (Vista) for a chevron.\n"
    "header_engine =\n"
    "header_preset =\n"
    "header_crumb_menu =\n"
    "# Thumbnails view: the square's size in pixels, 32..256 (auto: 96, XP's).\n"
    "# Thumbnails are shared with other programs in ~/.cache/thumbnails.\n"
    "thumbnail_size =\n"
    "# The view of folders never opened (Tools > Folder Options): details, icons,\n"
    "# smallicons, list, thumbnails, with +thumbnails and @px (the zoom) as in\n"
    "# views; auto: details (XP frame), thumbnails@48 = Icons at 48 px with\n"
    "# thumbnails (Longhorn). remember_views: each folder's view kept (auto: 1).\n"
    "default_view =\n"
    "default_view_longhorn =\n"
    "remember_views =\n"
    "# Hidden files and folders shown: 1 or 0 (auto: 0).\n"
    "show_hidden =\n"
    "# The Folders tree (View > Explorer Bar > Folders, remembered here): 1 or 0\n"
    "# (auto: 0, the places pane instead).\n"
    "folders =\n"
    "# The places pane (Places and Bookmarks) where the Folders tree is when that\n"
    "# is off: on or off (auto: on).\n"
    "places_pane =\n"
    "\n"
    "# flxp-desktop: the wallpaper (BMP, PNG, JPEG or WebP; auto: XP's plain blue) and how\n"
    "# it covers the screen: stretch, center, tile or fill (auto: stretch).\n"
    "wallpaper =\n"
    "wallpaper_mode =\n";

bool write_settings_template()
{
    std::string p = settings_path();
    if (exists(p)) return false;
    return write_file_atomic(p, template_text);
}

// ---------------------------------------------------------------------------
// Fonts

struct WantedFont { const char* family; const char* file; bool required; };
static const WantedFont wanted[] = {
    { "Tahoma", "tahoma.ttf", true },
    { "Microsoft Sans Serif", "micross.ttf", true },
    { "Marlett", "marlett.ttf", true },
    { "Trebuchet MS", "trebuc.ttf", false },
    { "Verdana", "verdana.ttf", false },
};
static const int n_wanted = sizeof wanted / sizeof wanted[0];
static std::string g_found[n_wanted];    // file supplied by an added directory

// The file in dir called name, ignoring case ("" if none).
static std::string find_in_dir(const std::string& dir, const char* name)
{
    std::string out;
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    while (struct dirent* e = readdir(d))
        if (!strcasecmp(e->d_name, name)) { out = dir + "/" + e->d_name; break; }
    closedir(d);
    return out;
}

// fontconfig's system match for family: the file, and whether it is that family.
static std::string system_match(const char* family, bool& exact)
{
    exact = false;
    FcInit();
    FcPattern* p = FcPatternCreate();
    FcPatternAddString(p, FC_FAMILY, (const FcChar8*)family);
    FcPatternAddBool(p, FC_SCALABLE, FcTrue);
    FcConfigSubstitute(nullptr, p, FcMatchPattern);
    FcDefaultSubstitute(p);
    FcResult res;
    FcPattern* m = FcFontMatch(nullptr, p, &res);
    std::string file;
    if (m) {
        FcChar8* s;
        if (FcPatternGetString(m, FC_FILE, 0, &s) == FcResultMatch) file = (const char*)s;
        for (int i = 0; FcPatternGetString(m, FC_FAMILY, i, &s) == FcResultMatch; i++)
            if (!strcasecmp((const char*)s, family)) exact = true;
        FcPatternDestroy(m);
    }
    FcPatternDestroy(p);
    return file;
}

static std::vector<std::string> wine_font_dirs()
{
    std::vector<std::string> out;
    const char* wp = getenv("WINEPREFIX");
    out.push_back((wp && *wp ? std::string(wp) : home_dir() + "/.wine") + "/drive_c/windows/Fonts");
    for (const char* d : { "/usr/share/wine/fonts", "/usr/local/share/wine/fonts", "/usr/lib/wine/fonts",
                           "/usr/lib64/wine/fonts", "/opt/wine-stable/share/wine/fonts",
                           "/opt/wine-staging/share/wine/fonts", "/opt/wine-devel/share/wine/fonts" })
        out.push_back(d);
    return out;
}

void setup_fonts()
{
    static bool done;
    if (done) return;
    done = true;
    std::string sm = setting("font_smoothing");
    gdi::set_font_smoothing(sm == "off" || sm == "0" ? gdi::SMOOTH_OFF :
                            sm == "cleartype" ? gdi::SMOOTH_CLEARTYPE : gdi::SMOOTH_STANDARD);
    for (const std::string& d : split_path(setting("font_path"))) {
        gdi::add_font_dir(d.c_str());
        for (int i = 0; i < n_wanted; i++)
            if (g_found[i].empty()) g_found[i] = find_in_dir(d, wanted[i].file);
    }
    // Families neither font_path nor the system has: Wine's own font directories.
    for (const std::string& d : wine_font_dirs()) {
        if (!exists(d)) continue;
        bool use = false;
        for (int i = 0; i < n_wanted; i++) {
            if (!g_found[i].empty()) continue;
            bool exact;
            system_match(wanted[i].family, exact);
            if (exact) continue;
            std::string f = find_in_dir(d, wanted[i].file);
            if (!f.empty()) { g_found[i] = f; use = true; }
        }
        if (use) gdi::add_font_dir(d.c_str());
    }
    for (int i = 0; i < n_wanted; i++) {
        if (!wanted[i].required || !g_found[i].empty()) continue;
        bool exact;
        std::string f = system_match(wanted[i].family, exact);
        if (!exact && i == 0)
            fprintf(stderr, "flxp: no Tahoma font; using %s (put tahoma.ttf in ~/.fonts or set font_path= in %s)\n",
                    f.empty() ? "fontconfig's default" : f.c_str(), settings_path().c_str());
    }
}

std::vector<FontInfo> font_report()
{
    std::vector<FontInfo> out;
    for (int i = 0; i < n_wanted; i++) {
        FontInfo fi;
        fi.family = wanted[i].family;
        if (!g_found[i].empty()) { fi.file = g_found[i]; fi.exact = true; }
        else fi.file = system_match(wanted[i].family, fi.exact);
        out.push_back(fi);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Visual styles

std::vector<std::string> theme_dirs()
{
    std::vector<std::string> dirs = split_path(setting("theme_path"));
    dirs.push_back(data_home() + "/flxp/themes");
    std::string d = data_path("themes");
    if (!d.empty()) dirs.push_back(d);
    const char* wp = getenv("WINEPREFIX");
    dirs.push_back((wp && *wp ? std::string(wp) : home_dir() + "/.wine") + "/drive_c/windows/resources/themes");
    dirs.push_back(home_dir() + "/.themes");
    return dirs;
}

std::string find_msstyles(const std::string& name)
{
    if (name.empty()) return "";
    if (name.find('/') != std::string::npos) {
        std::string p = name[0] == '~' ? home_dir() + name.substr(1) : name;
        return exists(p) ? p : "";
    }
    std::string lower = name;
    for (char& c : lower) if (c >= 'A' && c <= 'Z') c += 32;
    std::string upper = lower;
    if (upper[0] >= 'a' && upper[0] <= 'z') upper[0] -= 32;
    for (const std::string& d : theme_dirs()) {
        for (const std::string& v : { name, lower, upper }) {
            std::string f = d + "/" + v + "/" + lower + ".msstyles";
            if (exists(f)) return f;
            f = d + "/" + v + "/" + v + ".msstyles";
            if (exists(f)) return f;
        }
        std::string f = d + "/" + lower + ".msstyles";
        if (exists(f)) return f;
    }
    return "";
}

static void export_env(const char* var, const std::string& v)
{
    if (v.empty()) unsetenv(var); else setenv(var, v.c_str(), 1);
}

std::string setup_theme()
{
    // the Longhorn session theme wins over theme= unless theme_follow = 0 or
    // a theme was given for this run
    if (follows_session_theme()) {
        std::string name, ini;
        SessionTheme t;
        if (read_session_properties(name, ini)) {
            if (!lookup_session_theme(name, ini, t)) {
                if (is_preset_name(name)) fprintf(stderr, "flxp: preset %s: no base theme in %s; using theme=\n", name.c_str(), ini.c_str());
                else fprintf(stderr, "flxp: session theme \"%s\" is not in %s; using theme=\n", name.c_str(), ini.c_str());
            }
            else {
                set_session_theme(t);
                if (t.msstyles.empty())
                    fprintf(stderr, "flxp: no visual style flxp can load for session theme %s (%s: not a v3/v4 msstyles; a "
                            "flxp_msstyles or an xp-backport is needed); using theme=\n", t.name.c_str(), t.path("msstyles").c_str());
                else {
                    export_env("FLXP_THEME", t.msstyles);
                    export_env("FLXP_THEME_COLOR", t.color);
                    export_env("FLXP_THEME_SIZE", t.size);
                    export_env("FLXP_THEME_FOLLOWED", t.name);
                    return t.msstyles;
                }
            }
        }
    }
    if (getenv("FLXP_THEME_FOLLOWED")) {
        // inherited from a parent that followed a session theme that is gone now
        unsetenv("FLXP_THEME"); unsetenv("FLXP_THEME_COLOR"); unsetenv("FLXP_THEME_SIZE"); unsetenv("FLXP_THEME_FOLLOWED");
    }
    std::string want = setting("theme");
    bool asked = !want.empty();
    if (!asked) want = "luna";
    std::string path;
    if (strcasecmp(want.c_str(), "classic") && strcasecmp(want.c_str(), "none") && strcasecmp(want.c_str(), "off")) {
        path = find_msstyles(want);
        if (path.empty() && asked)
            fprintf(stderr, "flxp: visual style \"%s\" not found (theme_path= in %s); using classic\n",
                    want.c_str(), settings_path().c_str());
    }
    export_env("FLXP_THEME", path);
    export_env("FLXP_THEME_COLOR", path.empty() ? "" : setting("theme_color"));
    export_env("FLXP_THEME_SIZE", path.empty() ? "" : setting("theme_size"));
    return path;
}

} // namespace shell
