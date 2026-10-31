// The session theme through liblhde: see lhtheme.h and docs/THEME-FORMAT.md.
#include "lhtheme.h"
#include <limits.h>
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
#include "settings.h"
extern "C" {
#include "lhde/theme.h"
}

namespace shell {

namespace {

SessionTheme g_theme;

bool is_dir(const std::string& p) { struct stat st; return !p.empty() && stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }

std::string root_string(Display* dpy, const char* prop)
{
    Atom a = XInternAtom(dpy, prop, True);
    if (a == None) return "";
    Atom type; int format; unsigned long count, after; unsigned char* data = nullptr;
    std::string out;
    if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), a, 0, 16384, False, AnyPropertyType, &type, &format, &count,
                           &after, &data) == Success && data) {
        if (format == 8) out.assign((const char*)data, count);
        XFree(data);
    }
    return out;
}

unsigned long root_serial(Display* dpy)
{
    Atom a = XInternAtom(dpy, "_LH_SETTINGS_SERIAL", True);
    if (a == None) return 0;
    Atom type; int format; unsigned long count, after; unsigned char* data = nullptr;
    unsigned long v = 0;
    if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), a, 0, 1, False, XA_CARDINAL, &type, &format, &count, &after, &data) == Success && data) {
        if (format == 32 && count == 1) v = (unsigned long)*(long*)data;
        XFree(data);
    }
    return v;
}

// the record's first line theme=NAME
std::string record_theme(const std::string& rec)
{
    if (rec.compare(0, 6, "theme=")) return "";
    size_t e = rec.find('\n');
    return rec.substr(6, e == std::string::npos ? std::string::npos : e - 6);
}

// a liblhde record into ours; t is freed
bool take(lhde_theme* t, SessionTheme& out)
{
    if (!t) return false;
    out = SessionTheme();
    const char *k, *v, *w;
    for (int i = 0; lhde_theme_item(t, i, &k, &v, &w); i++) {
        out.keys[k] = v ? v : "";
        out.why[k] = w ? w : "";
    }
    char* text = lhde_theme_serialize(t, 0);
    if (text) {
        // without the serial= line: the same record compares equal at any serial
        std::string s = text;
        free(text);
        size_t p = s.find("\nserial=");
        if (p != std::string::npos) {
            size_t e = s.find('\n', p + 1);
            s.erase(p, e == std::string::npos ? std::string::npos : e - p);
        }
        out.body = s;
    }
    lhde_theme_free(t);
    out.name = out.value("theme");
    out.color = out.value("VisualStyle.color");
    out.size = out.value("VisualStyle.size");
    out.classic = out.value("VisualStyle.style") == "classic" || out.value("style.kind") == "classic";
    out.msstyles = out.classic ? "" : out.value("path.style");
    return !out.name.empty();
}

// path.resources when it is a Longhorn build's (resources/4051, resources/5112), not XP's or 2000's
std::string longhorn_resources(const SessionTheme& t)
{
    std::string r = t.value("path.resources");
    std::string b = base_of(r);
    if (r.empty() || b.size() != 4 || b.find_first_not_of("0123456789") != std::string::npos) return "";
    return is_dir(r) ? r : "";
}

} // namespace

std::string SessionTheme::value(const std::string& key) const
{
    auto i = keys.find(key);
    return i == keys.end() ? "" : i->second;
}

std::string SessionTheme::title() const
{
    std::string t = value("Theme.name");
    return t.empty() ? name : t;
}

bool resolve_theme(const std::string& name, SessionTheme& out, std::string* err)
{
    char e[512] = "";
    if (name.empty()) { if (err) *err = "no theme name"; return false; }
    lhde_theme* t = lhde_theme_resolve(name.c_str(), e, sizeof e);
    if (!t && name.find('/') != std::string::npos) t = lhde_theme_resolve(base_of(name).c_str(), e, sizeof e);   // "themes/aero"
    if (!t) { if (err) *err = e; return false; }
    return take(t, out);
}

bool resolve_theme_files(const std::string& name, SessionTheme& out, std::string* err)
{
    char e[512] = "";
    lhde_theme* t = name.empty() ? nullptr : lhde_theme_resolve_flags(name.c_str(), LHDE_RESOLVE_FILES, e, sizeof e);
    if (!t) { if (err) *err = name.empty() ? "no theme name" : e; return false; }
    return take(t, out);
}

bool resolve_theme_with(const std::string& name, const std::map<std::string, std::string>& layer, SessionTheme& out, std::string* err)
{
    char e[512] = "";
    std::vector<const char*> kv;
    for (auto& x : layer) { kv.push_back(x.first.c_str()); kv.push_back(x.second.c_str()); }
    lhde_theme* t = name.empty() ? nullptr : lhde_theme_resolve_with(name.c_str(), kv.data(), (int)kv.size() / 2, e, sizeof e);
    if (!t) { if (err) *err = name.empty() ? "no theme name" : e; return false; }
    return take(t, out);
}

bool session_theme_from(const std::string& name, const std::string& published, unsigned long serial, SessionTheme& out, std::string* err)
{
    if (name.empty()) { if (err) *err = "no _LH_THEME"; return false; }
    // the published record when it describes this name at the current serial (luxwm, luxshell write it)
    if (!published.empty() && record_theme(published) == name) {
        unsigned long s = 0;
        lhde_theme* t = lhde_theme_parse(published.c_str(), &s);
        if (t && s == serial) return take(t, out);
        if (t) lhde_theme_free(t);
    }
    return resolve_theme(name, out, err);
}

bool read_session_theme(SessionTheme& out, std::string* err)
{
    const char* d = getenv("DISPLAY");
    if (!d || !*d) { if (err) *err = "no display"; return false; }
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) { if (err) *err = "cannot open the display"; return false; }
    std::string name = root_string(dpy, "_LH_THEME"), rec = root_string(dpy, "_LH_THEME_RESOLVED");
    unsigned long serial = root_serial(dpy);
    XCloseDisplay(dpy);
    return session_theme_from(name, rec, serial, out, err);
}

bool lookup_theme(const std::string& name, std::string& canon, std::string* file)
{
    char c[256], f[PATH_MAX];
    if (name.empty() || !lhde_theme_lookup(name.c_str(), c, sizeof c, f, sizeof f)) return false;
    canon = c;
    if (file) *file = f;
    return true;
}

std::string user_theme_path(const std::string& name)
{
    if (name.empty() || name[0] == '.' || name.size() > 100 || name.find_first_of("/\n") != std::string::npos) return "";
    char d[PATH_MAX];
    if (!lhde_theme_user_dir(d, sizeof d)) return "";
    return std::string(d) + "/" + name + ".theme";
}

bool is_user_theme(const std::string& name)
{
    std::string canon, file, mine = user_theme_path(name);
    return !mine.empty() && lookup_theme(name, canon, &file) && canon == name && file == mine;
}

const SessionTheme& session_theme() { return g_theme; }

void set_session_theme(const SessionTheme& t)
{
    g_theme = t;
    // the theme's own faces (5112's Segoe UI): path.fonts, a directory (or a file in one)
    static std::set<std::string> added;
    std::string d = t.value("path.fonts");
    if (!d.empty() && !is_dir(d)) d = dir_of(d);
    if (is_dir(d) && added.insert(d).second) gdi::add_font_dir(d.c_str());
    // and asked first: Jade 4074's MENUFONT "Segoe UI, 9" is 4074's seguibk.ttf, not 5112's newer segoeui.ttf
    gdi::set_theme_font_dir(is_dir(d) ? d.c_str() : nullptr);
}

std::string theme_setting(const std::string& key, const std::string& fallback)
{
    // a run's option (FLXP_<KEY>) wins over the theme
    std::string var = "FLXP_";
    for (char c : key) var += (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    if (const char* e = getenv(var.c_str())) if (*e) return e;
    const SessionTheme& t = g_theme;
    auto or_fallback = [&](const std::string& v) { return v.empty() ? fallback : v; };
    if (key == "icons") {
        std::string v = t.value("Desktop.icons");
        if (v.empty() || v == "none") return fallback;
        // flxp's icon recipes are res/icons/{longhorn,xp,2k,wine}.theme; lhde's names are build-qualified
        if (data_path("res/icons/" + v + ".theme").empty() && v.compare(0, 8, "longhorn") == 0) v = "longhorn";
        return v;
    }
    if (key == "frame") {
        if (t.name.empty()) return fallback;
        std::string nb = t.value("Explorer.navbar");
        if (nb == "none") return "xp";              // no navigation bar: XP's toolbars
        return or_fallback(t.value("Explorer.chrome"));
    }
    if (key == "taskbar_kind") return or_fallback(t.value("Shell.taskbar"));
    if (key == "start_menu") {
        std::string v = setting(key);               // the user's style: panel | classic
        return v.empty() || v == "lhshell" ? fallback : v;
    }
    if (key == "font_smoothing") {
        std::string v = setting(key);               // settings.ini [fonts] smoothing (the store), the user's
        return v.empty() ? or_fallback(t.value("Fonts.smoothing")) : v;
    }
    std::string v = setting(key);
    if (!v.empty()) return v;
    if (key == "longhorn_system32") return or_fallback(longhorn_resources(t));
    if (key == "menubar_style") return t.value("Explorer.chrome") == "longhorn" ? "document" : fallback;
    if (key == "header_engine") {
        std::string nb = t.value("Explorer.navbar");
        return nb == "vision" ? "lhnav" : nb == "4051" || nb == "4074" ? "flxp" : fallback;
    }
    if (key == "header_preset") {
        std::string p = t.value("Explorer.navbar_preset");
        return t.value("Explorer.navbar") == "vision" && !p.empty() && p != "auto" ? p : fallback;
    }
    return fallback;
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
