#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <map>
#include "icons.h"
#include "config.h"
#include "lhtheme.h"

namespace shell {

static std::string g_dir;
static IconTheme g_theme;
static bool g_loaded;
static std::string g_requested;   // the name select_icon_theme was given
static int g_generation;

static bool exists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }

static const std::string& icon_dir()
{
    if (g_dir.empty()) {
        g_dir = data_path("res/icons");
        if (g_dir.empty()) g_dir = "res/icons";
    }
    return g_dir;
}

void set_icon_dir(const std::string& dir) { g_dir = dir; }

static std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

static std::string expand(const std::string& v)
{
    // ~ and $VAR
    std::string out;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i] == '~' && i == 0) { const char* h = getenv("HOME"); out += h ? h : ""; }
        else if (v[i] == '$') {
            size_t j = i + 1; while (j < v.size() && (isalnum((unsigned char)v[j]) || v[j] == '_')) j++;
            std::string var = v.substr(i + 1, j - i - 1);
            if (var.rfind("FLXP_", 0) == 0) {   // flxp settings: $FLXP_NT5SRC is nt5src= in flxp.ini
                std::string key = var.substr(5);
                for (char& c : key) if (c >= 'A' && c <= 'Z') c += 32;
                out += theme_setting(key);   // or the session theme's flxp_<key>
            } else {
                const char* e = getenv(var.c_str());
                out += e ? e : "";
            }
            i = j - 1;
        } else out += v[i];
    }
    return out;
}

// Resolve a source: absolute, pe:..., or relative to the theme base.
static std::string resolve(const IconTheme& t, const std::string& src)
{
    if (src.empty()) return src;
    std::string s = expand(src);
    if (s.rfind("pe:", 0) == 0) {
        std::string rest = s.substr(3);
        if (!rest.empty() && rest[0] != '/') rest = t.base + "/" + rest;
        return "pe:" + rest;
    }
    if (s[0] == '/') return s;
    return (t.base.empty() ? icon_dir() : t.base) + "/" + s;
}

bool load_icon_theme(const std::string& name)
{
    std::string path = name.find('/') != std::string::npos ? name : icon_dir() + "/" + name + ".theme";
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    IconTheme t;
    t.name = name;
    std::string section;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        std::string l = trim(line);
        if (l.empty() || l[0] == ';' || l[0] == '#') continue;
        if (l[0] == '[') { section = l.substr(1, l.find(']') - 1); continue; }
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(l.substr(0, eq)), v = trim(l.substr(eq + 1));
        size_t c = v.find(" ;"); if (c != std::string::npos) v = trim(v.substr(0, c));
        if (section == "theme") {
            if (k == "name") t.name = v;
            else if (k == "base") {
                t.base = expand(v);
                if (!t.base.empty() && t.base[0] != '/') t.base = dir_of(path) + "/" + t.base;   // relative to the .theme file
            }
            else if (k == "depth") t.depth = atoi(v.c_str());
        } else if (section == "icons") {
            t.icons.push_back({k, v});
        } else if (section == "toolbar") {
            ToolbarTheme& tb = t.toolbar;
            if (k == "strip") tb.strip = v; else if (k == "hot") tb.hot_strip = v;
            else if (k == "small") tb.small_strip = v; else if (k == "small_hot") tb.small_hot_strip = v;
            else if (k == "size") tb.size = atoi(v.c_str()); else if (k == "small_size") tb.small_size = atoi(v.c_str());
            else if (k == "back") tb.back = atoi(v.c_str()); else if (k == "forward") tb.forward = atoi(v.c_str());
            else if (k == "up") tb.up = atoi(v.c_str()); else if (k == "search") tb.search = atoi(v.c_str());
            else if (k == "folders") tb.folders = atoi(v.c_str()); else if (k == "views") tb.views = atoi(v.c_str());
            else if (k == "brand") tb.brand = v; else if (k == "go") tb.go = v; else if (k == "go_hot") tb.go_hot = v;
            else if (k == "go_size") tb.go_size = atoi(v.c_str());
            else if (k == "close") tb.close = v;
        }
    }
    fclose(f);
    // resolve sources now
    for (auto& kv : t.icons) kv.second = resolve(t, kv.second);
    t.toolbar.strip = resolve(t, t.toolbar.strip);
    t.toolbar.hot_strip = resolve(t, t.toolbar.hot_strip);
    t.toolbar.small_strip = resolve(t, t.toolbar.small_strip);
    t.toolbar.small_hot_strip = resolve(t, t.toolbar.small_hot_strip);
    t.toolbar.brand = resolve(t, t.toolbar.brand);
    t.toolbar.go = resolve(t, t.toolbar.go);
    t.toolbar.go_hot = resolve(t, t.toolbar.go_hot);
    t.toolbar.close = resolve(t, t.toolbar.close);
    g_theme = t;
    g_loaded = true;
    return true;
}

// The file behind a resolved source exists ("pe:/x/shell32.dll#4" -> /x/shell32.dll).
static bool source_exists(const std::string& src)
{
    if (src.empty()) return false;
    std::string p = src.rfind("pe:", 0) == 0 ? src.substr(3) : src;
    if (src.rfind("pe:", 0) == 0) { size_t h = p.find('#'); if (h != std::string::npos) p = p.substr(0, h); }
    return exists(p);
}

static bool theme_usable(const IconTheme& t)
{
    for (auto& kv : t.icons)
        if (kv.first == "folder" || kv.first == "document")
            if (!source_exists(kv.second)) return false;
    return !t.icons.empty();
}

// Toolbar images that are missing are dropped (comctl32's own bitmaps stand in).
static void drop_missing_toolbar(ToolbarTheme& tb)
{
    if (!source_exists(tb.strip)) tb.strip.clear(), tb.hot_strip.clear();
    if (!source_exists(tb.small_strip)) tb.small_strip.clear(), tb.small_hot_strip.clear();
    if (!tb.hot_strip.empty() && !source_exists(tb.hot_strip)) tb.hot_strip.clear();
    if (!tb.small_hot_strip.empty() && !source_exists(tb.small_hot_strip)) tb.small_hot_strip.clear();
    for (std::string* s : { &tb.brand, &tb.go, &tb.go_hot, &tb.close })
        if (!s->empty() && !source_exists(*s)) s->clear();
}

bool select_icon_theme(const std::string& name)
{
    g_requested = name;
    g_generation++;
    std::string want = name.empty() ? "xp" : name;
    if (load_icon_theme(want) && theme_usable(g_theme)) {
        drop_missing_toolbar(g_theme.toolbar);
        return true;
    }
    if (want != "wine") {
        const char* hint = want == "longhorn" ? "set longhorn_system32= (and nt5src=) in " :
                           (want == "xp" || want == "2k") ? "set nt5src= in " : "check icons= in ";
        fprintf(stderr, "flxp: icon theme \"%s\" has no icon files here; using the Wine icons (%s%s%s)\n",
                want.c_str(), hint, settings_path().c_str(), (want == "xp" || want == "2k") ? ", or make install-icons" : "");
    }
    if (load_icon_theme("wine") && theme_usable(g_theme)) { drop_missing_toolbar(g_theme.toolbar); return want == "wine"; }
    g_theme = IconTheme();
    g_theme.name = "none";
    g_loaded = true;
    return false;
}

const IconTheme& icon_theme()
{
    if (!g_loaded) select_icon_theme(theme_setting("icons", "xp"));
    return g_theme;
}

bool refresh_icon_theme()
{
    std::string want = theme_setting("icons", "xp");
    if (g_loaded && want == g_requested) return false;
    select_icon_theme(want);
    return true;
}

int icon_theme_generation() { icon_theme(); return g_generation; }

std::string icon_file(const std::string& key)
{
    const IconTheme& t = icon_theme();
    for (auto& kv : t.icons) if (kv.first == key) return kv.second;
    for (auto& kv : t.icons) if (kv.first == "document") return kv.second;
    return "";
}

} // namespace shell
