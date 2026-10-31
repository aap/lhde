#include "res/res.h"   // before anything that pulls in win32 names
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <strings.h>
#include <map>
#include "icons.h"
#include "config.h"
#include "lhdata.h"
#include "lhtheme.h"
#include "gdi/bmp.h"
#include "gdi/image.h"

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
            if (var.rfind("FLXP_", 0) == 0) {   // flxp settings: $FLXP_LONGHORN_SYSTEM32 is longhorn_system32=
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

// A relative path: "resources/BUILD/..." names another imported build's file (the longhorn4074
// recipe's toolbar is XP's: pe:resources/xp/shell32.dll#214), anything else is relative to the base.
static std::string relative(const IconTheme& t, const std::string& p)
{
    if (p.rfind("resources/", 0) == 0) {
        std::string f = lhde_find(p);
        return f.empty() ? "/nonexistent/" + p : f;   // not imported: missing, dropped like any missing file
    }
    return (t.base.empty() ? icon_dir() : t.base) + "/" + p;
}

// Resolve a source: absolute, pe:..., or relative (above).
static std::string resolve(const IconTheme& t, const std::string& src)
{
    if (src.empty()) return src;
    std::string s = expand(src);
    if (s.rfind("pe:", 0) == 0) {
        std::string rest = s.substr(3);
        size_t hash = rest.rfind('#');
        std::string id = hash == std::string::npos ? "" : rest.substr(hash);
        if (!rest.empty() && rest[0] != '/') rest = relative(t, rest.substr(0, rest.size() - id.size())) + id;
        return "pe:" + rest;
    }
    if (s[0] == '/') return s;
    return relative(t, s);
}

std::string icon_theme_file(const std::string& name)
{
    if (name.find('/') != std::string::npos) return name;
    // lhde's recipes first (data/icons: xp, 2k, longhorn4074), then flxp's own (wine); the
    // older name "longhorn" is longhorn4074
    std::string n = name == "longhorn" ? "longhorn4074" : name;
    std::string f = lhde_find("icons/" + n + ".theme");
    if (!f.empty()) return f;
    f = icon_dir() + "/" + name + ".theme";
    return exists(f) ? f : "";
}

bool load_icon_theme(const std::string& name)
{
    std::string path = icon_theme_file(name);
    if (path.empty()) return false;
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    IconTheme t;
    t.name = name;
    bool has_resources = false;
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
            else if (k == "resources") {   // resources/BUILD: lhde-import's DLLs, the base of pe: sources
                t.base = lhde_find(expand(v));
                if (t.base.empty()) t.base = "/nonexistent/" + v;   // not imported: the theme is unusable
                has_resources = true;
            }
            else if (k == "base" && !has_resources) {   // a user's recipe: a directory of its own
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
        std::string file = icon_theme_file(want);
        if (file.empty())
            fprintf(stderr, "flxp: no icon theme \"%s\" (icons/%s.theme in lhde's data directories); using the Wine icons\n",
                    want.c_str(), want.c_str());
        else
            fprintf(stderr, "flxp: icon theme \"%s\" has no icon files here (%s: its resources/<build> not imported? "
                    "lhde-import, docs/USAGE.md \"Getting the Windows resources\"); using the Wine icons\n", want.c_str(), file.c_str());
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

std::string resource_file(const std::string& build, const std::string& file)
{
    return lhde_find("resources/" + build + "/" + file);
}

std::string resource_src(const std::string& build, const std::string& file, int id)
{
    std::string f = resource_file(build, file);
    return f.empty() ? "" : "pe:" + f + "#" + std::to_string(id);
}

// Where a source keeps XP's art under another file or id (the rest are XP's own), or has a picture of its own under XP's id
// (to_id 0: not taken from it): ReactOS's explorer.exe IDB_START 143 is ReactOS's logo, so neither start button takes it
// (flxp-taskbar draws its own flag); its toolbar strips, Go button and brand are browseui's (the icon recipe
// data/icons/reactos.theme). What no source but XP has (the Log Off buttons 7010/7011, the chess tile logonui.exe 114,
// shellstyle.dll) stays "" and its caller draws without it.
struct ArtMap { const char* source; const char* file; int id; const char* to_file; int to_id; };
static const ArtMap art_map[] = {
    { "reactos", "explorer.exe", 143, "explorer.exe", 0 },
    { "reactos", "explorer.exe", 176, "explorer.exe", 0 },
};

std::string xp_art(const std::string& file, int id, int type)
{
    for (const char* src : { "xp", "2k", "reactos" }) {
        std::string f = file;
        int i = id;
        for (const ArtMap& m : art_map)
            if (!strcmp(m.source, src) && !strcasecmp(m.file, file.c_str()) && m.id == id) { f = m.to_file; i = m.to_id; }
        if (!i) continue;
        std::string path = resource_file(src, f);
        if (path.empty()) continue;
        res::Module* mod = res::load(path.c_str());
        if (!mod) continue;
        res::Blob b;
        bool has = res::find(mod, type ? res::Id((uint32_t)type) : res::Id("AVI"), res::Id((uint32_t)i), b);
        res::unload(mod);
        if (has) return "pe:" + path + "#" + std::to_string(i);
    }
    return "";
}

// DIR/NAME matched case-insensitively ("" if none)
static std::string find_nocase(const std::string& dir, const std::string& name)
{
    DIR* d = opendir(dir.c_str());
    if (!d) return "";
    std::string out;
    while (struct dirent* e = readdir(d))
        if (!strcasecmp(e->d_name, name.c_str())) { out = dir + "/" + e->d_name; break; }
    closedir(d);
    return out;
}

std::string shellstyle_dll(const std::string& msstyles, const std::string& color)
{
    if (msstyles.empty()) return resource_file("xp", "shellstyle.dll");
    // XP: %ResourceDir%\Themes\Luna\Shell\NormalColor\shellstyle.dll beside luna.msstyles;
    // lhde-import: styles/NAME.msstyles with styles/NAME/Shell/<color>/
    std::string dir = dir_of(msstyles), base = msstyles.substr(dir.size() + 1);
    size_t dot = base.rfind('.');
    if (dot != std::string::npos) base.resize(dot);
    for (std::string shell : { find_nocase(dir, "Shell"), find_nocase(dir + "/" + base, "Shell") }) {
        if (shell.empty()) continue;
        std::string c = find_nocase(shell, color);
        std::string f = c.empty() ? "" : find_nocase(c, "shellstyle.dll");
        if (!f.empty()) return f;
    }
    return "";
}

gdi::Bitmap* load_art(const std::string& src)
{
    if (src.rfind("pe:", 0) != 0) return src.empty() ? nullptr : gdi::load_image(src);
    size_t hash = src.rfind('#');
    if (hash == std::string::npos) return nullptr;
    res::Module* m = res::load(src.substr(3, hash - 3).c_str());
    if (!m) return nullptr;
    res::Blob b;
    gdi::Bitmap* out = nullptr;
    if (res::find(m, res::Id(res::RT_BITMAP), res::Id((uint32_t)atoi(src.c_str() + hash + 1)), b)) out = gdi::decode_bmp(b.data, b.size);
    res::unload(m);
    return out;
}

} // namespace shell
