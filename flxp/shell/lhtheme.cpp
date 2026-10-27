// The Longhorn lab's session theme: the registry (themes.ini) and the root
// properties _LH_THEME / _LH_THEMES_INI. Format and rules: x11/THEMES.md.
#include "lhtheme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <set>
#include <vector>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include "config.h"
#include "gdi/font.h"
#include "runtime.h"
#include "res/res.h"
#include "settings.h"

namespace shell {

namespace {

SessionTheme g_theme;

std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

// a ';' or '#' after whitespace starts a comment
std::string strip_comment(const std::string& v)
{
    for (size_t i = 1; i < v.size(); i++)
        if ((v[i] == ';' || v[i] == '#') && (v[i - 1] == ' ' || v[i - 1] == '\t')) return trim(v.substr(0, i));
    return trim(v);
}

typedef std::map<std::string, std::map<std::string, std::string>> Sections;

Sections parse(const std::string& text, std::vector<std::string>& order)
{
    Sections out;
    std::string cur;
    size_t i = 0;
    while (i < text.size()) {
        size_t j = text.find('\n', i);
        if (j == std::string::npos) j = text.size();
        std::string line = trim(text.substr(i, j - i));
        i = j + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            size_t e = line.find(']');
            cur = trim(line.substr(1, e == std::string::npos ? std::string::npos : e - 1));
            if (!out.count(cur)) order.push_back(cur);
            out[cur];
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos || cur.empty()) continue;
        out[cur][trim(line.substr(0, eq))] = strip_comment(line.substr(eq + 1));
    }
    return out;
}

bool exists(const std::string& p)
{
    struct stat st;
    return !p.empty() && stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string absolute(const std::string& p, const std::string& registry)
{
    if (p.empty() || p[0] == '/') return p;
    if (p[0] == '~') return home_dir() + p.substr(1);
    return dir_of(registry) + "/" + p;
}

// 3 for an XP-format visual style, 4 for Longhorn's compiled one, 0 if none
int packthem_version(const std::string& path)
{
    res::Module* m = res::load(path.c_str());
    if (!m) return 0;
    res::Blob b;
    int v = 0;
    if (res::find(m, res::Id("PACKTHEM_VERSION"), res::Id(1u), b) && b.size == 2) v = b.data[0] | b.data[1] << 8;
    res::unload(m);
    return v;
}

std::string root_string(Display* dpy, const char* prop)
{
    Atom a = XInternAtom(dpy, prop, True);
    if (a == None) return "";
    Atom type; int format; unsigned long count, after; unsigned char* data = nullptr;
    std::string out;
    if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), a, 0, 4096, False, AnyPropertyType, &type, &format, &count,
                           &after, &data) == Success && data) {
        if (format == 8) out.assign((const char*)data, count);
        XFree(data);
    }
    return out;
}

} // namespace

std::string SessionTheme::value(const std::string& key) const
{
    auto i = keys.find(key);
    return i == keys.end() ? "" : i->second;
}

std::string SessionTheme::path(const std::string& key) const { return absolute(value(key), ini); }

bool read_session_properties(std::string& name, std::string& ini)
{
    name.clear(); ini.clear();
    const char* d = getenv("DISPLAY");
    if (!d || !*d) return false;
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return false;
    name = root_string(dpy, "_LH_THEME");
    ini = root_string(dpy, "_LH_THEMES_INI");
    XCloseDisplay(dpy);
    return !name.empty();
}

std::string loadable_msstyles(const std::string& path, const std::string& registry)
{
    int v = packthem_version(path);
    // XP's format and Longhorn's compiled one (PACKTHEM_VERSION 4: the v4 reader
    // in the vendored msstyles.c) load as they are; flxp_msstyles still overrides
    if (v == 3 || v == 4) return path;
    if (path.empty()) return "";
    // not a style we read (a missing file, another version): an XP backport of the same name
    std::string b = base_of(path);
    size_t dot = b.rfind('.');
    if (dot != std::string::npos) b = b.substr(0, dot);
    for (char& c : b) if (c >= 'A' && c <= 'Z') c += 32;
    std::vector<std::string> dirs;
    if (!registry.empty()) dirs.push_back(dir_of(dir_of(registry)) + "/themes/xp-backport");
    for (const std::string& d : theme_dirs()) dirs.push_back(d);
    std::string upper = b;
    if (!upper.empty() && upper[0] >= 'a' && upper[0] <= 'z') upper[0] -= 32;
    for (const std::string& d : dirs)
        for (const std::string& sub : { b, upper }) {
            std::string f = d + "/" + sub + "/" + b + ".msstyles";
            if (exists(f) && packthem_version(f) == 3) return f;
        }
    return "";
}

// A user preset (settings.ini's themes/NAME.theme, x11/SETTINGS.md): the base
// registry section with the preset's overrides. Paths in the preset are
// absolute or relative to the preset file.
// lhwm/lux publish the registry section a preset in _LH_THEME resolves to as
// _LH_THEME_RESOLVED: that wins for the base section (ours is the fallback).
static std::string resolved_for(const std::string& name)
{
    const char* d = getenv("DISPLAY");
    if (!d || !*d) return "";
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return "";
    std::string r = root_string(dpy, "_LH_THEME") == name ? root_string(dpy, "_LH_THEME_RESOLVED") : "";
    XCloseDisplay(dpy);
    // "name=...;base=...;..." (SETTINGS.md): the base when it describes this name
    if (r.find('=') == std::string::npos) return r;   // an older bare section name
    return resolved_field(r, "name") == name ? resolved_field(r, "base") : "";
}

std::string resolved_field(const std::string& r, const std::string& key)
{
    size_t i = 0;
    while (i < r.size()) {
        std::string k, v;
        bool in_value = false;
        for (; i < r.size() && r[i] != ';'; i++) {
            char c = r[i];
            if (c == '\\' && i + 1 < r.size()) c = r[++i];
            else if (c == '=' && !in_value) { in_value = true; continue; }
            (in_value ? v : k) += c;
        }
        i++;
        if (k == key) return v;
    }
    return "";
}

template<class Find>
static bool lookup_preset(const std::string& name, const std::string& ini, Sections& s, Find find, SessionTheme& out)
{
    store::Ini p;
    std::string res = resolved_for(name), rsec = res.empty() ? "" : find(res);
    if (!store::read_preset(name, p)) {
        if (rsec.empty()) return false;
        // a preset we cannot read (another user's directory): the resolved section as it is
        out = SessionTheme();
        out.name = name;
        out.ini = ini;
        out.keys = s[rsec];
        out.keys["base"] = rsec;
        out.color = out.value("color");
        out.size = out.value("size");
        std::string own = out.path("flxp_msstyles");
        out.msstyles = !own.empty() && exists(own) ? own : loadable_msstyles(out.path("msstyles"), ini);
        return true;
    }
    std::string sec = !rsec.empty() ? rsec : find(p.get("Theme", "base"));
    if (sec.empty()) {
        fprintf(stderr, "flxp: preset %s: base theme '%s' is not in the registry\n", name.c_str(), p.get("Theme", "base").c_str());
        return false;
    }
    out = SessionTheme();
    out.name = name;
    out.ini = ini;
    out.keys = s[sec];
    out.keys["base"] = sec;
    out.keys["title"] = p.get("Theme", "name", name);
    std::string pdir = dir_of(store::preset_path(name));
    auto put = [&](const char* psec, const char* pkey, const std::string& key, bool is_path) {
        std::string v = p.get(psec, pkey);
        if (v.empty()) return;
        if (is_path && v[0] == '~') v = home_dir() + v.substr(1);
        else if (is_path && v[0] != '/') v = pdir + "/" + v;
        out.keys[key] = v;
    };
    put("VisualStyle", "msstyles", "msstyles", true);
    put("VisualStyle", "color", "color", false);
    put("VisualStyle", "size", "size", false);
    put("Composition", "composition", "composition", false);
    put("Composition", "animations", "animations", false);
    put("Desktop", "wallpaper", "wallpaper", true);
    put("Desktop", "mode", "wallpaper_mode", false);
    put("Desktop", "icons", "flxp_icons", false);
    put("Frames", "frame", "frame", true);
    put("Frames", "frame_classic", "frame_classic", true);
    put("Shell", "shell", "shell", true);
    put("Shell", "startpanel", "startpanel", false);
    put("Fonts", "fonts", "fonts", false);
    out.color = out.value("color");
    out.size = out.value("size");
    std::string own = p.get("VisualStyle", "msstyles").empty() ? out.path("flxp_msstyles") : "";
    if (!own.empty() && exists(own)) out.msstyles = own;
    else out.msstyles = loadable_msstyles(out.path("msstyles"), ini);
    return true;
}

bool is_preset_name(const std::string& name)
{
    store::Ini p;
    return !name.empty() && name.find('/') == std::string::npos && store::read_preset(name, p);
}

bool lookup_session_theme(const std::string& name, const std::string& ini, SessionTheme& out)
{
    std::string text;
    if (name.empty() || !read_file(ini, text)) return false;
    std::vector<std::string> order;
    Sections s = parse(text, order);
    auto find = [&](const std::string& n) -> std::string {
        if (s.count(n)) return n;
        for (const std::string& sec : order) {
            std::string al = s[sec]["aliases"];
            size_t i = 0;
            while (i < al.size()) {
                size_t j = al.find_first_of(" \t,", i);
                if (j == std::string::npos) j = al.size();
                if (al.substr(i, j - i) == n) return sec;
                i = j + 1;
            }
        }
        return "";
    };
    std::string sec = find(name);
    if (sec.empty() && name.find('/') != std::string::npos) sec = find(base_of(name));   // "themes/aero"
    if (sec.empty()) return lookup_preset(name, ini, s, find, out);
    out = SessionTheme();
    out.name = sec;
    out.ini = ini;
    out.keys = s[sec];
    out.color = out.value("color");
    out.size = out.value("size");
    // flxp_msstyles names a style explicitly (an XP backport); else msstyles
    // itself (v3 or v4), or an XP backport of its name
    std::string own = out.path("flxp_msstyles");
    if (!own.empty() && exists(own)) out.msstyles = own;
    else out.msstyles = loadable_msstyles(out.path("msstyles"), ini);
    return true;
}

const SessionTheme& session_theme() { return g_theme; }
void set_session_theme(const SessionTheme& t)
{
    g_theme = t;
    // the theme's own faces (5112's Segoe UI): `;`-separated directories or files
    static std::set<std::string> added;
    std::string f = t.value("fonts");
    size_t i = 0;
    while (i < f.size()) {
        size_t j = f.find(';', i);
        if (j == std::string::npos) j = f.size();
        std::string d = absolute(trim(f.substr(i, j - i)), t.ini);
        i = j + 1;
        if (exists(d)) d = dir_of(d);
        struct stat st;
        if (d.empty() || stat(d.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || !added.insert(d).second) continue;
        gdi::add_font_dir(d.c_str());
    }
}

std::string theme_setting(const std::string& key, const std::string& fallback)
{
    std::string v = setting(key);
    if (!v.empty()) return v;
    v = g_theme.value("flxp_" + key);
    if (v.empty()) return fallback;
    // path-valued keys are relative to the registry
    if (v[0] == '.' || v.find('/') != std::string::npos) return absolute(v, g_theme.ini);
    return v;
}

static bool follows_now()
{
    std::string f = setting("theme_follow", "1");
    if (f == "0" || f == "no" || f == "off" || f == "false") return false;
    // FLXP_THEME from --theme or the caller's environment is explicit; the one
    // setup_theme exported for a followed theme (FLXP_THEME_FOLLOWED) is not
    const char* t = getenv("FLXP_THEME");
    const char* fol = getenv("FLXP_THEME_FOLLOWED");
    if (t && *t && !(fol && *fol)) return false;
    return true;
}

bool follows_session_theme()
{
    // decided once, before setup_theme exports FLXP_THEME
    static int known = -1;
    if (known < 0) known = follows_now();
    return known == 1;
}

} // namespace shell
