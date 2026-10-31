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
#include "lhtheme.h"
extern "C" {
#include "lhde/settings.h"
#include "lhde/theme.h"
}

namespace shell {

// config.cpp's hook (weak there): settings.ini under flxp.ini
std::string store_value_for(const std::string& key) { return store::value_for(key); }

namespace store {

namespace {

std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

bool iequal(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

// where a value ends: a ';' or '#' after whitespace starts a comment
size_t value_end(const std::string& line, size_t from)
{
    for (size_t i = from + 1; i < line.size(); i++)
        if ((line[i] == ';' || line[i] == '#') && (line[i - 1] == ' ' || line[i - 1] == '\t')) return i;
    return line.size();
}

// classify a line: 0 other, 1 section (name), 2 key (key, value, position of '=')
int classify(const std::string& raw, std::string& a, std::string& b, size_t* eqpos = nullptr)
{
    std::string t = trim(raw);
    if (t.empty() || t[0] == ';' || t[0] == '#') return 0;
    if (t[0] == '[') {
        size_t e = t.find(']');
        a = trim(t.substr(1, e == std::string::npos ? std::string::npos : e - 1));
        return 1;
    }
    size_t eq = raw.find('=');
    if (eq == std::string::npos) return 0;
    a = trim(raw.substr(0, eq));
    b = trim(raw.substr(eq + 1, value_end(raw, eq) - eq - 1));
    if (eqpos) *eqpos = eq;
    return 2;
}

struct Cache { Ini ini; std::string path; time_t mtime = 0; off_t size = -1; long nsec = -1; };
Cache g_cache;

const Ini& current()
{
    std::string p = path();
    struct stat st;
    if (stat(p.c_str(), &st) != 0) { g_cache.ini.lines.clear(); g_cache.path = p; g_cache.size = -1; return g_cache.ini; }
    if (p != g_cache.path || st.st_mtime != g_cache.mtime || st.st_size != g_cache.size || st.st_mtim.tv_nsec != g_cache.nsec) {
        g_cache.ini.load(p);
        g_cache.path = p; g_cache.mtime = st.st_mtime; g_cache.size = st.st_size; g_cache.nsec = st.st_mtim.tv_nsec;
    }
    return g_cache.ini;
}

// the per-user keys flxp.ini shares with settings.ini (flxp.ini, when it sets one, wins)
struct Map { const char* flxp; const char* section; const char* key; };
const Map maps[] = {
    { "font_smoothing", "fonts", "smoothing" },
    { "menu_shadows", "effects", "menu_shadows" },
    { "pinned", "start", "pinned" },
};

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

// ---- Ini -------------------------------------------------------------------------

void Ini::parse(const std::string& text)
{
    lines.clear();
    size_t i = 0;
    while (i < text.size()) {
        size_t j = text.find('\n', i);
        if (j == std::string::npos) j = text.size();
        lines.push_back(text.substr(i, j - i));
        i = j + 1;
    }
}

bool Ini::load(const std::string& p)
{
    std::string t;
    lines.clear();
    if (!read_file(p, t)) return false;
    parse(t);
    return true;
}

std::string Ini::text() const
{
    std::string out;
    for (const std::string& l : lines) { out += l; out += '\n'; }
    return out;
}

bool Ini::save(const std::string& p) const { return write_file_atomic(p, text()); }

bool Ini::has(const std::string& section, const std::string& key) const
{
    std::string cur, a, b;
    for (const std::string& l : lines) {
        int k = classify(l, a, b);
        if (k == 1) cur = a;
        else if (k == 2 && iequal(cur, section) && iequal(a, key)) return true;
    }
    return false;
}

std::string Ini::get(const std::string& section, const std::string& key, const std::string& fallback) const
{
    std::string cur, a, b;
    for (const std::string& l : lines) {
        int k = classify(l, a, b);
        if (k == 1) cur = a;
        else if (k == 2 && iequal(cur, section) && iequal(a, key)) return b.empty() ? fallback : b;
    }
    return fallback;
}

std::vector<std::string> Ini::keys(const std::string& section) const
{
    std::vector<std::string> out;
    std::string cur, a, b;
    for (const std::string& l : lines) {
        int k = classify(l, a, b);
        if (k == 1) cur = a;
        else if (k == 2 && iequal(cur, section)) out.push_back(a);
    }
    return out;
}

void Ini::set(const std::string& section, const std::string& key, const std::string& value)
{
    std::string cur, a, b;
    long sec_last = -1;   // the section's last key line (or its header)
    for (size_t i = 0; i < lines.size(); i++) {
        size_t eq = 0;
        int k = classify(lines[i], a, b, &eq);
        if (k == 1) { cur = a; if (iequal(cur, section)) sec_last = (long)i; continue; }
        if (k != 2 || !iequal(cur, section)) continue;
        sec_last = (long)i;
        if (!iequal(a, key)) continue;
        std::string& l = lines[i];
        size_t ve = value_end(l, eq);
        std::string comment = ve < l.size() ? " " + l.substr(ve) : "";
        size_t ind = 0;
        while (ind < l.size() && (l[ind] == ' ' || l[ind] == '\t')) ind++;
        l = l.substr(0, ind) + key + " = " + value + comment;
        return;
    }
    std::string line = key + " = " + value;
    if (sec_last >= 0) { lines.insert(lines.begin() + sec_last + 1, line); return; }
    while (!lines.empty() && trim(lines.back()).empty()) lines.pop_back();
    if (!lines.empty()) lines.push_back("");
    lines.push_back("[" + section + "]");
    lines.push_back(line);
}

void Ini::remove(const std::string& section, const std::string& key)
{
    std::string cur, a, b;
    long header = -1;
    int left = 0;
    for (size_t i = 0; i < lines.size(); i++) {
        int k = classify(lines[i], a, b);
        if (k == 1) {
            if (header >= 0) break;
            cur = a;
            if (iequal(cur, section)) header = (long)i;
            continue;
        }
        if (k != 2 || header < 0) continue;
        if (iequal(a, key)) { lines.erase(lines.begin() + (long)i); i--; }
        else left++;
    }
    if (header >= 0 && left == 0) {
        lines.erase(lines.begin() + header);
        // the blank line before it goes too
        if (header > 0 && header <= (long)lines.size() && trim(lines[(size_t)header - 1]).empty()) lines.erase(lines.begin() + header - 1);
    }
}

// ---- settings.ini ----------------------------------------------------------------

std::string dir() { char b[PATH_MAX]; return lhde_config_dir(b, sizeof b) ? b : config_home() + "/lhde"; }
std::string path() { char b[PATH_MAX]; return lhde_settings_path(b, sizeof b) ? b : dir() + "/settings.ini"; }

std::string get(const std::string& section, const std::string& key, const std::string& fallback)
{
    return current().get(section, key, fallback);
}

bool set(const std::vector<Edit>& edits)
{
    bool ok = true;
    lhde_settings_reload();   // another writer (luxshell) may have changed it since
    for (const Edit& e : edits)
        ok = lhde_settings_set(e.section.c_str(), e.key.c_str(), e.value.empty() ? nullptr : e.value.c_str()) >= 0 && ok;
    g_cache.size = -1;   // re-read next time
    return ok;
}

bool set(const std::string& section, const std::string& key, const std::string& value)
{
    return set(std::vector<Edit>{ Edit{ section, key, value } });
}

bool mapping(const std::string& flxp_key, std::string& section, std::string& key)
{
    for (const Map& m : maps)
        if (flxp_key == m.flxp) { section = m.section; key = m.key; return true; }
    return false;
}

std::string value_for(const std::string& flxp_key)
{
    std::string s, k;
    return mapping(flxp_key, s, k) ? get(s, k) : "";
}

bool set_shared(const std::vector<Edit>& edits)
{
    bool ok = set(edits);
    Ini flxp;
    if (!flxp.load(settings_path())) return ok;
    for (const Edit& e : edits)
        for (const Map& m : maps)
            if (iequal(e.section, m.section) && iequal(e.key, m.key) && !flxp.get("flxp", m.flxp).empty())
                ok = write_setting(m.flxp, e.value) && ok;
    return ok;
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
