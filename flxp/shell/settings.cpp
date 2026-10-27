// The shared settings store: see settings.h and x11/SETTINGS.md.
#include "settings.h"
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

struct Map { const char* flxp; const char* section; const char* key; };
const Map maps[] = {
    { "icons", "desktop", "icons" },
    { "wallpaper", "desktop", "wallpaper" },
    { "wallpaper_mode", "desktop", "mode" },
    { "font_smoothing", "fonts", "smoothing" },
    { "pinned", "start", "pinned" },
    { "start_menu", "start", "menu" },
    { "taskbar_kind", "taskbar", "kind" },
    { "frame", "explorer", "frame" },
};

// per-theme keys: override/registry key, the preset's section and key, the global settings.ini key, flxp.ini's key,
// the built-in default
struct ThemeKey { const char* key; const char* reg; const char* psec; const char* pkey; const char* gsec; const char* gkey; const char* flxp; const char* dflt; };
const ThemeKey theme_keys[] = {
    { "icons", "flxp_icons", "Desktop", "icons", "desktop", "icons", "icons", "xp" },
    { "frame", "flxp_frame", "Explorer", "frame", "explorer", "frame", "frame", "auto" },
    { "taskbar", "taskbar", "Shell", "taskbar", "taskbar", "kind", "taskbar_kind", "avalon" },
    { "start", "start", "Shell", "start", "start", "menu", "start_menu", "flxp" },
    { "composition", nullptr, "Composition", "composition", "theme", "composition", nullptr, "follow" },
    { "animations", "animations", "Composition", "animations", "theme", "animations", nullptr, "4074plus" },
    // lux's sidebar (SETTINGS.md [sidebar]): per theme like the taskbar, the global [sidebar] keys below the registry
    // (stacking is not per theme any more: sidebar_stacking() / set_sidebar_stacking())
    { "sidebar", "sidebar", "Shell", "sidebar", "sidebar", "mode", nullptr, "full" },
    { "sidebar_with_classic", "sidebar_with_classic", "Shell", "sidebar_with_classic", "sidebar", "with_classic", nullptr, "0" },
};

const ThemeKey* theme_key(const std::string& key)
{
    for (const ThemeKey& t : theme_keys) if (key == t.key) return &t;
    return nullptr;
}

const ThemeKey* theme_key_flxp(const std::string& flxp_key)
{
    for (const ThemeKey& t : theme_keys) if (t.flxp && flxp_key == t.flxp) return &t;
    return nullptr;
}

const Ini& registry_ini(const std::string& registry)
{
    static Ini reg;
    static std::string reg_path;
    static time_t reg_mtime;
    struct stat st;
    if (registry.empty() || stat(registry.c_str(), &st) != 0) { reg.lines.clear(); reg_path.clear(); return reg; }
    if (registry != reg_path || st.st_mtime != reg_mtime) { reg.load(registry); reg_path = registry; reg_mtime = st.st_mtime; }
    return reg;
}

std::vector<std::string> split_aliases(const std::string& b)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (i < b.size()) {
        size_t j = b.find_first_of(" \t,", i);
        if (j == std::string::npos) j = b.size();
        if (j > i) out.push_back(b.substr(i, j - i));
        i = j + 1;
    }
    return out;
}

// a registry entry's key: NAME a section or in a section's aliases
std::string registry_value(const std::string& registry, const std::string& name, const std::string& key)
{
    const Ini& reg = registry_ini(registry);
    std::string sec = registry_section(registry, name);
    return sec.empty() ? "" : reg.get(sec, key);
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

bool safe_name(const std::string& n)
{
    if (n.empty() || n[0] == '.' || n.size() > 100) return false;
    for (char c : n) if (c == '/' || c == '\n' || c == '\0') return false;
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

std::string dir() { return config_home() + "/longhorn"; }
std::string path() { return dir() + "/settings.ini"; }

std::string get(const std::string& section, const std::string& key, const std::string& fallback)
{
    return current().get(section, key, fallback);
}

bool set(const std::vector<Edit>& edits)
{
    Ini ini;
    ini.load(path());
    if (ini.lines.empty()) ini.lines.push_back("; Longhorn session settings (x11/SETTINGS.md)");
    for (const Edit& e : edits) ini.set(e.section, e.key, e.value);
    bool ok = ini.save(path());
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

static std::string theme_chain(const std::string& theme, const ThemeKey* tk, const std::string& registry, int from, Source* src);

std::string value_for(const std::string& flxp_key)
{
    std::string s, k;
    if (!mapping(flxp_key, s, k)) return "";
    std::string v;
    if (const ThemeKey* tk = theme_key_flxp(flxp_key)) {
        const SessionTheme& st = session_theme();
        v = theme_chain(st.name, tk, st.ini, 1, nullptr);   // no session theme: [override], the global key
    } else {
        v = get(s, k);
    }
    if (flxp_key == "start_menu") return v == "lux" ? "lhshell" : "";   // flxp: our own default (panel)
    if (flxp_key == "wallpaper" && !v.empty() && v[0] == '~') v = home_dir() + v.substr(1);
    return v;
}

bool set_shared(const std::vector<Edit>& edits)
{
    bool ok = set(edits);
    Ini flxp;
    if (!flxp.load(settings_path())) return ok;
    for (const Edit& e : edits)
        for (const Map& m : maps) {
            if (!iequal(e.section, m.section) || !iequal(e.key, m.key) || flxp.get("flxp", m.flxp).empty()) continue;
            std::string v = e.value;
            if (!strcmp(m.flxp, "start_menu")) v = v == "lux" ? "lhshell" : "panel";
            ok = write_setting(m.flxp, v) && ok;
        }
    return ok;
}

// ---- per-theme settings -----------------------------------------------------------

std::string preset_base(const std::string& name)
{
    Ini p;
    if (name.empty() || name.find('/') != std::string::npos || !read_preset(name, p)) return "";
    if (Display* dpy = open_display()) {
        std::string r = root_string(dpy, "_LH_THEME") == name ? root_string(dpy, "_LH_THEME_RESOLVED") : "";
        XCloseDisplay(dpy);
        if (!r.empty() && resolved_field(r, "name") == name && !resolved_field(r, "base").empty()) return resolved_field(r, "base");
    }
    return p.get("Theme", "base");
}

// the chain without the built-in default ("" = nothing sets it). from: 1 the override and the preset's own
// value count, 3 from [override] on, 4 from the registry on
static std::string theme_chain(const std::string& theme, const ThemeKey* tk, const std::string& registry, int from, Source* src)
{
    auto found = [&](Source s, const std::string& v) { if (src) *src = s; return v; };
    if (from <= 1 && !theme.empty()) {
        std::string v;
        for (const std::string& n : theme_names(theme, registry)) {   // the section's name, then its aliases
            v = get("override:" + n, tk->key);
            if (!v.empty()) return found(Source::Override, v);
        }
        Ini p;
        if (theme.find('/') == std::string::npos && read_preset(theme, p)) {
            v = p.get(tk->psec, tk->pkey);
            if (!v.empty()) return found(Source::Preset, v);
        }
    }
    if (from <= 3) {
        std::string v = get("override", tk->key);
        if (!v.empty()) return found(Source::AllThemes, v);
    }
    std::string base = preset_base(theme);
    std::string v = tk->reg && !theme.empty() ? registry_value(registry, base.empty() ? theme : base, tk->reg) : "";
    if (!v.empty()) return found(Source::Theme, v);
    v = get(tk->gsec, tk->gkey);
    return found(v.empty() ? Source::Default : Source::Global, v);
}

std::string theme_default(const std::string& theme, const std::string& key, const std::string& registry, bool without_all)
{
    const ThemeKey* tk = theme_key(key);
    if (!tk) return "";
    std::string v = theme_chain(theme, tk, registry, without_all ? 4 : 3, nullptr);
    return v.empty() ? tk->dflt : v;
}

std::string theme_value(const std::string& theme, const std::string& key, const std::string& registry, Source* src)
{
    const ThemeKey* tk = theme_key(key);
    if (!tk) return "";
    std::string v = theme_chain(theme, tk, registry, 1, src);
    return v.empty() ? tk->dflt : v;
}

bool set_all_themes_values(const std::vector<KeyValue>& kv)
{
    if (kv.empty()) return true;
    Ini ini;
    ini.load(path());
    if (ini.lines.empty()) ini.lines.push_back("; Longhorn session settings (x11/SETTINGS.md)");
    // every [override:NAME] section
    std::vector<std::string> sections;
    std::string a, b;
    for (const std::string& l : ini.lines)
        if (classify(l, a, b) == 1 && a.size() > 9 && iequal(a.substr(0, 9), "override:")) sections.push_back(a);
    Ini flxp;
    flxp.load(settings_path());
    bool ok = true;
    std::vector<std::string> presets = preset_names();
    std::vector<Ini> pini(presets.size());
    std::vector<bool> pdirty(presets.size(), false);
    for (size_t i = 0; i < presets.size(); i++) read_preset(presets[i], pini[i]);
    for (const KeyValue& e : kv) {
        const ThemeKey* tk = theme_key(e.key);
        if (!tk) continue;
        for (const std::string& sec : sections) ini.remove(sec, tk->key);
        for (size_t i = 0; i < presets.size(); i++)
            if (pini[i].has(tk->psec, tk->pkey)) { pini[i].remove(tk->psec, tk->pkey); pdirty[i] = true; }
        if (e.value.empty()) {
            ini.remove("override", tk->key);
        } else {
            ini.set("override", tk->key, e.value);
            ini.set(tk->gsec, tk->gkey, e.value);   // readers that know only the global key (below the registry)
        }
        if (tk->flxp && !flxp.get("flxp", tk->flxp).empty()) ok = write_setting(tk->flxp, "") && ok;
    }
    for (size_t i = 0; i < presets.size(); i++)
        if (pdirty[i]) ok = pini[i].save(preset_path(presets[i])) && ok;
    ok = ini.save(path()) && ok;
    g_cache.size = -1;
    return ok;
}

bool set_theme_values(const std::string& theme, const std::vector<KeyValue>& kv, const std::string& registry)
{
    if (theme.empty() || kv.empty()) return true;
    Ini p;
    bool preset = theme.find('/') == std::string::npos && read_preset(theme, p);
    Ini ini;
    if (!preset) {
        ini.load(path());
        if (ini.lines.empty()) ini.lines.push_back("; Longhorn session settings (x11/SETTINGS.md)");
    }
    Ini flxp;
    flxp.load(settings_path());
    bool ok = true;
    // an override lives under the section's name; one an alias carried (before the rename) goes away
    std::vector<std::string> names = theme_names(theme, registry);
    const std::string sect = "override:" + names[0];
    for (const KeyValue& e : kv) {
        const ThemeKey* tk = theme_key(e.key);
        if (!tk) continue;
        bool dflt = e.value == theme_default(theme, e.key, registry);
        if (!preset) for (size_t i = 1; i < names.size(); i++) ini.remove("override:" + names[i], e.key);
        if (preset) {
            if (dflt) p.remove(tk->psec, tk->pkey);
            else p.set(tk->psec, tk->pkey, e.value);
        } else {
            if (dflt) ini.remove(sect, e.key);
            else ini.set(sect, e.key, e.value);
        }
        // flxp.ini would win over every theme: the dialog's choice moves it to the theme
        if (tk->flxp && !flxp.get("flxp", tk->flxp).empty()) ok = write_setting(tk->flxp, "") && ok;
    }
    ok = (preset ? p.save(preset_path(theme)) : ini.save(path())) && ok;
    g_cache.size = -1;
    return ok;
}

// ---- presets ---------------------------------------------------------------------

std::string preset_dir() { return dir() + "/themes"; }

std::string preset_path(const std::string& name)
{
    return safe_name(name) ? preset_dir() + "/" + name + ".theme" : "";
}

std::vector<std::string> preset_names()
{
    std::vector<std::string> out;
    DIR* d = opendir(preset_dir().c_str());
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n.size() > 6 && n.compare(n.size() - 6, 6, ".theme") == 0 && n[0] != '.') out.push_back(n.substr(0, n.size() - 6));
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

bool read_preset(const std::string& name, Ini& out)
{
    std::string p = preset_path(name);
    return !p.empty() && out.load(p);
}

bool delete_preset(const std::string& name)
{
    std::string p = preset_path(name);
    return !p.empty() && unlink(p.c_str()) == 0;
}

// ---- the registry ----------------------------------------------------------------

std::string registry_path()
{
    if (Display* dpy = open_display()) {
        std::string r = root_string(dpy, "_LH_THEMES_INI");
        XCloseDisplay(dpy);
        if (!r.empty()) return r;
    }
    if (const char* e = getenv("LH_THEMES_INI")) if (*e) return e;
    return setting("themes_ini");
}

std::vector<ThemeEntry> theme_list(const std::string& registry)
{
    std::vector<ThemeEntry> out;
    Ini ini;
    if (!registry.empty() && ini.load(registry)) {
        std::string a, b;
        for (const std::string& l : ini.lines)
            if (classify(l, a, b) == 1) out.push_back(ThemeEntry{ a, "", false });
        for (ThemeEntry& e : out) e.title = ini.get(e.name, "title", e.name);
    }
    for (const std::string& n : preset_names()) {
        Ini p;
        read_preset(n, p);
        out.push_back(ThemeEntry{ n, p.get("Theme", "name", n), true });
    }
    return out;
}

std::string registry_section(const std::string& registry, const std::string& name)
{
    const Ini& reg = registry_ini(registry);
    std::string cur, a, b, alias_of;
    for (const std::string& l : reg.lines) {
        int k = classify(l, a, b);
        if (k == 1) { cur = a; if (iequal(a, name)) return a; }
        else if (k == 2 && iequal(a, "aliases") && alias_of.empty())
            for (const std::string& al : split_aliases(b)) if (al == name) { alias_of = cur; break; }
    }
    return alias_of;
}

std::vector<std::string> theme_names(const std::string& theme, const std::string& registry)
{
    std::string sec = registry_section(registry, theme);
    if (sec.empty()) return { theme };
    std::vector<std::string> out = { sec };
    for (const std::string& al : split_aliases(registry_ini(registry).get(sec, "aliases"))) out.push_back(al);
    if (std::find(out.begin(), out.end(), theme) == out.end()) out.push_back(theme);
    return out;
}

// ---- the sidebar's stacking (a global key, x11/SETTINGS.md) ----

std::string sidebar_stacking() { return get("sidebar", "stacking") == "bottom" ? "bottom" : "top"; }

bool set_sidebar_stacking(const std::string& value)
{
    Ini ini;
    ini.load(path());
    if (ini.lines.empty()) ini.lines.push_back("; Longhorn session settings (x11/SETTINGS.md)");
    // the per-theme keys of the first version of the Sidebar group are gone: stacking is global
    std::string cur, a, b;
    for (size_t i = 0; i < ini.lines.size(); i++) {
        int k = classify(ini.lines[i], a, b);
        if (k == 1) cur = a;
        else if (k == 2 && iequal(a, "sidebar_stacking") && (iequal(cur, "override") || (cur.size() > 9 && iequal(cur.substr(0, 9), "override:"))))
            ini.lines.erase(ini.lines.begin() + (long)i--);
    }
    if (!value.empty()) ini.set("sidebar", "stacking", value == "bottom" ? "bottom" : "top");
    bool ok = true;
    for (const std::string& n : preset_names()) {
        Ini p;
        if (read_preset(n, p) && p.has("Shell", "sidebar_stacking")) { p.remove("Shell", "sidebar_stacking"); ok = p.save(preset_path(n)) && ok; }
    }
    ok = ini.save(path()) && ok;
    g_cache.size = -1;
    return ok;
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
    bool ok = set("theme", "name", name);
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
