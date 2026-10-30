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
#include "lhdata.h"

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
    "# Every Windows file comes from the imported Windows tree through the session\n"
    "# theme (lhde-theme explain; flxp --info shows what was found): no path keys\n"
    "# live here. For one run: FLXP_FONT_PATH, FLXP_LONGHORN_SYSTEM32, FLXP_THEME_PATH.\n"
    "\n"
    "# Smooth edges of screen fonts (XP's Display Properties > Appearance >\n"
    "# Effects): off, standard or cleartype (auto: standard, XP's default).\n"
    "# Standard is grayscale where the font's gasp table allows it: Tahoma 8 stays\n"
    "# crisp, the Luna start button and captions are smoothed. cleartype is drawn\n"
    "# as standard for now.\n"
    "font_smoothing =\n"
    "# The theme decides the icon set, the Explorer frame and navigation bar, the\n"
    "# taskbar, the start menu and the wallpaper (Desktop.icons, Explorer.chrome,\n"
    "# Explorer.navbar, Shell.taskbar, Shell.start, Desktop.wallpaper: docs/\n"
    "# THEME-FORMAT.md; Display Properties writes the current layer). flxp.ini's\n"
    "# icons, frame, taskbar_kind, wallpaper and wallpaper_mode are ignored; for\n"
    "# one run FLXP_ICONS, FLXP_FRAME (--frame), ... still count.\n"
    "\n"
    "# Visual style: luna, another name found in the theme directories, a\n"
    "# .msstyles path, or classic. Auto: luna if it can be found, else classic.\n"
    "theme =\n"
    "# Colour scheme and size inside the .msstyles (auto: the style's default).\n"
    "theme_color =\n"
    "theme_size =\n"
    "# Follow the session theme (the root property _LH_THEME, a theme file that\n"
    "# liblhde resolves): its visual style replaces theme= and changes live with it;\n"
    "# 0 = keep theme= (auto: 1). --theme for one run does not follow either.\n"
    "theme_follow =\n"
    "\n"
    "# Toolbar buttons: large or small (auto: large).\n"
    "toolbar_icons =\n"
    "# The window's client size and position, WxH+X+Y.\n"
    "geometry =\n"
    "# Open Terminal Here: a /bin/sh command, %d = the quoted directory (auto: urxvt).\n"
    "terminal =\n"

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
    "# theme's Explorer.navbar_preset, else medium; the file dialog:\n"
    "# filedlg_header_size, auto small). The elements: 1 or 0 (auto: 1).\n"
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
    "# Which header draws: lhnav (luxnav, the vision header, docs/NAVBAR.md), flxp\n"
    "# (the 4051/4074 header above); auto: the theme's Explorer.navbar (vision:\n"
    "# lhnav). header_preset: one of the navbar file's [preset NAME] sections\n"
    "# (auto: the theme's Explorer.navbar_preset, else the file's own; Small: small).\n"
    "# header_crumb_menu: siblings (4074, auto) or children (Vista) for a chevron.\n"
    "header_engine =\n"
    "header_preset =\n"
    "header_crumb_menu =\n"
    "# The XP frame's Thumbnails view: the square's size in pixels, 32..256 (auto:\n"
    "# 96, XP's). Thumbnails are shared with other programs in ~/.cache/thumbnails.\n"
    "thumbnail_size =\n"
    "# The Longhorn views' thumbnails, in every view (View > Show Thumbnails): 1 or 0\n"
    "# (auto: 1); thumbnails_network: also on network mounts, NFS, SMB, sshfs, ...\n"
    "# (auto: 0, every picture would be read over the network).\n"
    "thumbnails =\n"
    "thumbnails_network =\n"
    "# The Longhorn views' Show in Groups (by type, Tiles and Icons): 1 or 0 (auto: 0).\n"
    "groups =\n"
    "# The view of folders never opened (Tools > Folder Options), as the views file\n"
    "# names them: XP frame (default_view) details, icons, smallicons, list,\n"
    "# thumbnails; Longhorn (default_view_longhorn) details, tiles or icons with @px,\n"
    "# the size (details 16..32, tiles 32..96, icons 16..256; none: the size the\n"
    "# view had last), icons also +nonames (Hide File Names); auto: details (XP),\n"
    "# icons@48 (Longhorn). remember_views: each folder's view kept (auto: 1).\n"
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
    "places_pane =\n";

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
    std::string sm = theme_setting("font_smoothing");
    gdi::set_font_smoothing(sm == "off" || sm == "0" ? gdi::SMOOTH_OFF :
                            sm == "cleartype" ? gdi::SMOOTH_CLEARTYPE : gdi::SMOOTH_STANDARD);
    // font_path, else the imported Windows fonts (lhde's fonts/<build> with Tahoma)
    std::vector<std::string> fp = split_path(setting("font_path"));
    if (fp.empty()) fp = default_font_dirs();
    for (const std::string& d : fp) {
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
            fprintf(stderr, "flxp: no Tahoma font; using %s (lhde-import fonts/<build>, put tahoma.ttf in ~/.fonts or set font_path= in %s)\n",
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
    for (const std::string& d : lhde_dirs()) dirs.push_back(d + "/styles");   // lhde-import's styles/
    const char* wp = getenv("WINEPREFIX");
    dirs.push_back((wp && *wp ? std::string(wp) : home_dir() + "/.wine") + "/drive_c/windows/resources/themes");
    dirs.push_back(home_dir() + "/.themes");
    return dirs;
}

// the one .msstyles in DIR, "" if none or several
static std::string only_msstyles(const std::string& dir)
{
    std::string out;
    int n = 0;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            size_t l = strlen(e->d_name);
            if (l > 9 && !strcasecmp(e->d_name + l - 9, ".msstyles")) { out = dir + "/" + e->d_name; n++; }
        }
        closedir(d);
    }
    return n == 1 ? out : "";
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
        // lhde's styles/NAME/ with one style of another name (styles/slate4074-xp/slate.msstyles)
        f = only_msstyles(d + "/" + name);
        if (!f.empty()) return f;
    }
    return "";   // no -xp backport fallback: every component uses the real style (THEME-FORMAT section 10)
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
        SessionTheme t;
        std::string err;
        bool got = read_session_theme(t, &err);
        if (getenv("FLXP_DEBUG"))
            fprintf(stderr, "flxp: session theme %s (%s): style.kind %s, path.style %s\n", got ? t.name.c_str() : "-",
                    err.c_str(), t.value("style.kind").c_str(), t.value("path.style").c_str());
        if (got) {
            set_session_theme(t);
            if (t.classic) {
                // VisualStyle.style = classic: no visual style
                export_env("FLXP_THEME", ""); export_env("FLXP_THEME_COLOR", ""); export_env("FLXP_THEME_SIZE", "");
                export_env("FLXP_THEME_FOLLOWED", t.name);
                return "";
            }
            if (t.msstyles.empty())
                fprintf(stderr, "flxp: session theme %s: no visual style (%s: %s); using theme=\n", t.name.c_str(),
                        t.value("VisualStyle.style").c_str(), t.why.count("path.style") ? t.why.at("path.style").c_str() : "not found");
            else {
                export_env("FLXP_THEME", t.msstyles);
                export_env("FLXP_THEME_COLOR", t.color);
                export_env("FLXP_THEME_SIZE", t.size);
                export_env("FLXP_THEME_FOLLOWED", t.name);
                return t.msstyles;
            }
        } else if (err != "no _LH_THEME" && err != "no display")
            fprintf(stderr, "flxp: session theme: %s; using theme=\n", err.c_str());
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
            fprintf(stderr, "flxp: visual style \"%s\" not found (lhde's styles/, theme_path= in %s); using classic\n",
                    want.c_str(), settings_path().c_str());
    }
    export_env("FLXP_THEME", path);
    export_env("FLXP_THEME_COLOR", path.empty() ? "" : setting("theme_color"));
    export_env("FLXP_THEME_SIZE", path.empty() ? "" : setting("theme_size"));
    return path;
}

} // namespace shell
