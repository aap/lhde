// lhde's settings store for flxp: see settings.h and docs/SETTINGS.md.
#include "settings.h"
#include "lhdata.h"
#include <limits.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include "apps.h"
#include "config.h"
#include "util.h"
#include "lhtheme.h"
extern "C" {
#include "lhde/settings.h"
#include "lhde/theme.h"
}

namespace shell {

namespace store {

namespace {

Display* open_display()
{
    const char* d = getenv("DISPLAY");
    if (!d || !*d) return nullptr;
    return XOpenDisplay(nullptr);
}

bool send_changed(Display* dpy, const std::string& hint)
{
    Window root = DefaultRootWindow(dpy);
    Atom serial_a = XInternAtom(dpy, "_LH_SETTINGS_SERIAL", False);
    long serial = 0;
    Atom type; int format; unsigned long count, after; unsigned char* data = nullptr;
    if (XGetWindowProperty(dpy, root, serial_a, 0, 1, False, XA_CARDINAL, &type, &format, &count, &after, &data) == Success && data) {
        if (format == 32 && count == 1) serial = *(long*)data;
        XFree(data);
    }
    serial = (serial + 1) & 0x7fffffff;
    XChangeProperty(dpy, root, serial_a, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&serial, 1);
    XEvent e = {};
    e.xclient.type = ClientMessage;
    e.xclient.window = root;
    e.xclient.message_type = XInternAtom(dpy, "_LH_SETTINGS_CHANGED", False);
    e.xclient.format = 32;
    e.xclient.data.l[0] = serial;
    e.xclient.data.l[1] = hint.empty() ? 0 : (long)XInternAtom(dpy, hint.c_str(), False);
    XSendEvent(dpy, root, False, SubstructureNotifyMask, &e);
    XSync(dpy, False);
    return true;
}

} // namespace

// ---- Ini: liblhde's lhde_ini ---------------------------------------------------------

void Ini::Free::operator()(lhde_ini* d) const { lhde_ini_free(d); }

Ini::Ini() : doc(lhde_ini_new()) {}

void Ini::parse(const std::string& text) { doc.reset(lhde_ini_parse(text.c_str())); }

bool Ini::load(const std::string& p)
{
    FILE* f = fopen(p.c_str(), "r");
    if (f) fclose(f);
    doc.reset(f ? lhde_ini_load(p.c_str()) : lhde_ini_new());
    return f && doc;
}

std::string Ini::text() const
{
    char* t = lhde_ini_text(doc.get());
    std::string out = t ? t : "";
    free(t);
    return out;
}

bool Ini::save(const std::string& p) const { return lhde_ini_save(doc.get(), p.c_str()) == 0; }

bool Ini::has(const std::string& section, const std::string& key) const
{
    char b[8];   // only whether it is there
    return lhde_ini_get(doc.get(), section.c_str(), key.c_str(), b, sizeof b) != nullptr;
}

std::string Ini::get(const std::string& section, const std::string& key, const std::string& fallback) const
{
    char b[4096];
    const char* v = lhde_ini_get(doc.get(), section.c_str(), key.c_str(), b, sizeof b);
    return v && *v ? v : fallback;
}

std::vector<std::string> Ini::keys(const std::string& section) const
{
    std::vector<std::string> out;
    lhde_ini_keys(doc.get(), section.c_str(), [](const char* k, const char*, void* ud) { ((std::vector<std::string>*)ud)->push_back(k); }, &out);
    return out;
}

void Ini::set(const std::string& section, const std::string& key, const std::string& value)
{
    lhde_ini_set(doc.get(), section.c_str(), key.c_str(), value.c_str());
}

void Ini::remove(const std::string& section, const std::string& key) { lhde_ini_set(doc.get(), section.c_str(), key.c_str(), nullptr); }

// ---- settings.ini ----------------------------------------------------------------

std::string dir() { char b[PATH_MAX]; return lhde_config_dir(b, sizeof b) ? b : config_home() + "/lhde"; }
std::string path() { char b[PATH_MAX]; return lhde_settings_path(b, sizeof b) ? b : dir() + "/settings.ini"; }

std::string get(const std::string& section, const std::string& key, const std::string& fallback)
{
    char b[4096];
    const char* v = lhde_settings_get(section.c_str(), key.c_str(), b, sizeof b);   // NULL: absent or empty
    return v ? v : fallback;
}

bool set(const std::vector<Edit>& edits)
{
    bool ok = true;
    lhde_settings_reload();   // another writer (luxshell) may have changed it since
    for (const Edit& e : edits)
        ok = lhde_settings_set(e.section.c_str(), e.key.c_str(), e.value.empty() ? nullptr : e.value.c_str()) >= 0 && ok;
    return ok;
}

bool flag(const std::string& section, const std::string& key, bool fallback)
{
    int v = flag_value(get(section, key), "settings.ini [" + section + "] " + key);
    return v < 0 ? fallback : v == 1;
}

bool set(const std::string& section, const std::string& key, const std::string& value)
{
    return set(std::vector<Edit>{ Edit{ section, key, value } });
}

// ---- the sidebar's stacking (a per-user key) ----

std::string sidebar_stacking() { return get("sidebar", "stacking") == "bottom" ? "bottom" : "top"; }

bool set_sidebar_stacking(const std::string& value)
{
    return value.empty() || set("sidebar", "stacking", value == "bottom" ? "bottom" : "top");
}

// ---- theme keys ------------------------------------------------------------------

bool set_theme_keys(const std::string& theme, const std::vector<KeyValue>& kv, std::string* selected, std::string* err, bool notify_now)
{
    std::string name = theme;
    bool changed = false, ok = true;
    for (const KeyValue& e : kv) {
        char sel[256] = "";
        int r = lhde_theme_set_key(name.c_str(), e.key.c_str(), e.value.empty() ? nullptr : e.value.c_str(), sel, sizeof sel);
        if (r < 0) {
            ok = false;
            if (err) *err = "cannot write " + e.key + " for the theme " + name + " (" + user_theme_path(name + "-modified") + ")";
            continue;
        }
        if (r > 0) changed = true;
        if (sel[0]) name = sel;   // the first change selects NAME-modified: the rest goes there too
    }
    if (selected) *selected = name;
    if (notify_now && (changed || name != theme)) {
        if (name != theme) apply_theme(name);   // settings.ini [theme] name, _LH_THEME, _LH_SETTINGS_CHANGED
        else notify("theme");
    }
    return ok;
}

static void list_one(const char* name, const char* title, const char* file, int group, void* ud)
{
    ((std::vector<ThemeEntry>*)ud)->push_back(ThemeEntry{ name, title ? title : name, file ? file : "", group });
}

std::vector<ThemeEntry> theme_list()
{
    std::vector<ThemeEntry> out;
    lhde_theme_list(list_one, &out);
    return out;
}

std::vector<ThemeEntry> theme_files()
{
    std::vector<ThemeEntry> out;
    for (ThemeEntry& e : theme_list()) if (e.group < 2) { e.style_missing = lhde_theme_has_style(e.name.c_str()) == 0; out.push_back(e); }
    return out;
}

// ---- the current layer -----------------------------------------------------------

static void layer_one(const char* k, const char* v, void* ud) { ((std::vector<KeyValue>*)ud)->push_back(KeyValue{ k, v ? v : "" }); }

std::vector<KeyValue> current_layer()
{
    std::vector<KeyValue> out;
    lhde_current_keys(layer_one, &out);
    return out;
}

bool set_current_layer(const std::vector<KeyValue>& kv)
{
    std::vector<const char*> a;
    for (const KeyValue& e : kv) if (!e.value.empty()) { a.push_back(e.key.c_str()); a.push_back(e.value.c_str()); }
    return lhde_current_set(a.data(), (int)a.size() / 2) >= 0;
}

bool set_current_keys(const std::vector<KeyValue>& kv, bool notify_now, const std::string& hint)
{
    std::vector<KeyValue> layer = current_layer();
    for (const KeyValue& e : kv) {
        auto it = std::find_if(layer.begin(), layer.end(), [&](const KeyValue& x) { return iequal(x.key, e.key); });
        if (it != layer.end()) layer.erase(it);
        if (!e.value.empty()) layer.push_back(e);
    }
    bool ok = set_current_layer(layer);
    if (ok && notify_now) notify(hint);
    return ok;
}

static bool announce_selected()
{
    std::string name = get("theme", "name");
    Display* dpy = open_display();
    if (!dpy) return false;
    Atom a = XInternAtom(dpy, "_LH_THEME", False);
    XChangeProperty(dpy, DefaultRootWindow(dpy), a, XInternAtom(dpy, "UTF8_STRING", False), 8, PropModeReplace,
                    (const unsigned char*)name.data(), (int)name.size());
    send_changed(dpy, "theme");
    XCloseDisplay(dpy);
    return true;
}

bool save_theme_as(const std::string& file, const std::string& title, std::string* err)
{
    char e[512] = "";
    if (lhde_theme_save_as(file.c_str(), title.c_str(), e, sizeof e)) { if (err) *err = e; return false; }
    announce_selected();
    return true;
}

bool delete_theme(const std::string& name)
{
    std::string before = get("theme", "name");
    int r = lhde_theme_delete(name.c_str());
    if (r <= 0) return false;
    if (before == name) announce_selected();
    return true;
}

// ---- notification ----------------------------------------------------------------

bool notify(const std::string& hint)
{
    Display* dpy = open_display();
    if (!dpy) return false;
    send_changed(dpy, hint);
    XCloseDisplay(dpy);
    return true;
}

bool apply_theme(const std::string& name, const std::string& hint)
{
    bool ok = lhde_theme_select(name.c_str()) >= 0 || set("theme", "name", name);
    Display* dpy = open_display();
    if (!dpy) return false;
    Atom a = XInternAtom(dpy, "_LH_THEME", False);
    XChangeProperty(dpy, DefaultRootWindow(dpy), a, XInternAtom(dpy, "UTF8_STRING", False), 8, PropModeReplace,
                    (const unsigned char*)name.data(), (int)name.size());
    send_changed(dpy, hint);
    XCloseDisplay(dpy);
    return ok;
}

bool lux_running()
{
    Display* dpy = open_display();
    if (!dpy) return false;
    char name[32];
    snprintf(name, sizeof name, "_LHSHELL_S%d", DefaultScreen(dpy));
    bool r = XGetSelectionOwner(dpy, XInternAtom(dpy, name, False)) != None;
    XCloseDisplay(dpy);
    return r;
}

// ---- the program -----------------------------------------------------------------

std::string settings_program()
{
    std::string e = exe_dir();
    // installed next to us (the build tree has every program in build/bin as well)
    std::vector<std::string> cand;
    if (!e.empty()) cand = { e + "/flxp-settings" };
    if (const char* path = getenv("PATH")) {
        std::string p = path;
        size_t i = 0;
        while (i <= p.size()) {
            size_t j = p.find(':', i);
            if (j == std::string::npos) j = p.size();
            if (j > i) cand.push_back(p.substr(i, j - i) + "/flxp-settings");
            i = j + 1;
        }
    }
    for (const std::string& p : cand)
        if (access(p.c_str(), X_OK) == 0) return p;
    return "flxp-settings";
}

bool launch_settings(const std::string& tab)
{
    std::vector<std::string> argv = { settings_program() };
    if (!tab.empty()) { argv.push_back("--tab"); argv.push_back(tab); }
    std::string err;
    if (!spawn(argv, home_dir(), &err)) { fprintf(stderr, "flxp: cannot start %s: %s\n", argv[0].c_str(), err.c_str()); return false; }
    return true;
}

} // namespace store
} // namespace shell
