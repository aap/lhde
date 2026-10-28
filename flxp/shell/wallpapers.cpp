// A theme's desktop background: see wallpapers.h and x11/THEMES.md "Wallpapers".
#include "wallpapers.h"
#include <stdlib.h>
#include <sys/stat.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include "config.h"
#include "lhdata.h"
#include "settings.h"

namespace shell {

namespace {

std::string root_prop(const char* prop)
{
    const char* d = getenv("DISPLAY");
    if (!d || !*d) return "";
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return "";
    std::string out;
    Atom a = XInternAtom(dpy, prop, True);
    if (a != None) {
        Atom type; int format; unsigned long count, after; unsigned char* data = nullptr;
        if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), a, 0, 4096, False, AnyPropertyType, &type, &format, &count,
                               &after, &data) == Success && data) {
            if (format == 8) out.assign((const char*)data, count);
            XFree(data);
        }
    }
    XCloseDisplay(dpy);
    return out;
}

std::string under(const std::string& file, const std::string& root)
{
    if (file.empty() || file[0] == '/' || file[0] == '~') return file;
    return root + "/" + file;
}

} // namespace

bool own_wallpaper(const SessionTheme& st, Wallpaper& out)
{
    if (st.name.empty()) return false;
    for (const std::string& n : store::theme_names(st.name, st.ini)) {
        std::string f = store::get("override:" + n, "wallpaper");
        if (f.empty()) continue;
        out.file = f;
        out.mode = store::get("override:" + n, "mode");
        return true;
    }
    store::Ini p;
    if (!st.value("base").empty() && store::read_preset(st.name, p) && p.has("Desktop", "wallpaper")) {
        out.file = st.value("wallpaper");   // lookup_session_theme made it absolute
        out.mode = st.value("wallpaper_mode");
        return !out.file.empty();
    }
    return false;
}

bool wallpapers_ini(const SessionTheme& st, Wallpaper& out)
{
    std::string path;
    if (const char* e = getenv("LH_WALLPAPERS_INI")) path = e;
    if (path.empty() && !st.ini.empty()) path = dir_of(st.ini) + "/wallpapers.ini";
    struct stat sb;
    if ((path.empty() || stat(path.c_str(), &sb) != 0) && !getenv("LH_WALLPAPERS_INI")) path = lhde_find("wallpapers.ini");
    store::Ini ini;
    if (path.empty() || !ini.load(path)) return false;
    // the section: the registry name (a preset's base), then the entry's aliases, then [default]
    std::string sec = st.value("base").empty() ? st.name : st.value("base");
    std::vector<std::string> names = store::theme_names(sec, st.ini);
    std::string use;
    for (const std::string& n : names) if (!ini.keys(n).empty()) { use = n; break; }
    std::string root = ini.get("default", "root", "../resources/wallpapers");
    if (root[0] != '/' && root[0] != '~') {
        // beside wallpapers.ini (a copy with its pictures), else lhde's data directories (wallpapers/)
        std::string r = dir_of(path) + "/" + root;
        root = stat(r.c_str(), &sb) == 0 ? r : lhde_resolve(root);
    }
    else if (root[0] == '~') root = home_dir() + root.substr(1);
    while (root.size() > 1 && root.back() == '/') root.pop_back();
    std::string file = use.empty() ? "" : ini.get(use, "file");
    std::string mode = file.empty() ? "" : ini.get(use, "mode");
    if (file.empty()) { file = ini.get("default", "file"); mode = ini.get("default", "mode"); }
    if (file.empty()) return false;
    out.file = under(file, root);
    out.mode = mode;
    return true;
}

bool theme_wallpaper(const SessionTheme& st, Wallpaper& out)
{
    if (st.name.empty()) return false;
    // lux's view, when it describes the theme in force
    std::string cur = root_prop("_LH_THEME"), r = root_prop("_LH_THEME_RESOLVED");
    bool same = false;   // the view must describe this theme (the dialog also asks for others)
    for (const std::string& n : store::theme_names(st.name, st.ini)) same = same || n == cur;
    if (same && !r.empty() && resolved_field(r, "name") == cur && !resolved_field(r, "wallpaper").empty()) {
        out.file = resolved_field(r, "wallpaper");
        out.mode = resolved_field(r, "mode");
        return true;
    }
    Wallpaper w;
    bool have = false;
    std::string key = st.path("wallpaper");   // a themes.ini key (a preset's own was taken by own_wallpaper)
    if (!key.empty()) { w.file = key; have = true; }
    else have = wallpapers_ini(st, w);
    if (!have) return false;
    std::string pm = st.value("base").empty() ? "" : st.value("wallpaper_mode");   // a preset's [Desktop] mode
    out.file = w.file;
    out.mode = pm.empty() ? w.mode : pm;
    return true;
}

} // namespace shell
