// lhde's data directories for flxp: see lhdata.h.
#include "lhdata.h"
#include <dirent.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <algorithm>
#include <set>
#include "config.h"
#include "lhtheme.h"
extern "C" {
#include "lhde/data.h"
}

namespace shell {

namespace {

bool is_dir(const std::string& p) { struct stat st; return !p.empty() && stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
bool is_file(const std::string& p) { struct stat st; return !p.empty() && stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode); }

// the first run of 4 digits in s ("slate4051" -> "4051"), "" if none
std::string digits4(const std::string& s)
{
    for (size_t i = 0; i + 4 <= s.size(); i++) {
        size_t j = i;
        while (j < s.size() && s[j] >= '0' && s[j] <= '9') j++;
        if (j - i == 4) return s.substr(i, 4);
        if (j > i) i = j;
    }
    return "";
}

// "resources/4074" -> "4074" (the component after PREFIX/), "" when the value is not one
std::string build_of(const std::string& v, const char* prefix)
{
    size_t p = v.find(prefix);
    if (p == std::string::npos || (p && v[p - 1] != '/')) return "";
    return digits4(v.substr(p + strlen(prefix)));
}

} // namespace

std::vector<std::string> lhde_dirs()
{
    const char* d[LHDE_DATA_MAX];
    int n = lhde_data_dirs(d, LHDE_DATA_MAX);
    return std::vector<std::string>(d, d + n);
}

std::string lhde_find(const std::string& rel)
{
    char out[PATH_MAX];
    return lhde_data_find(rel.c_str(), out, sizeof out) ? out : "";
}

std::string lhde_where(const std::string& rel)
{
    for (const std::string& d : lhde_dirs()) {
        struct stat st;
        if (stat((d + "/" + rel).c_str(), &st) == 0) return d;
    }
    return "";
}

std::string lhde_resolve(const std::string& v)
{
    char out[PATH_MAX];
    return lhde_data_resolve(v.c_str(), out, sizeof out);
}

std::vector<std::string> lhde_subdirs(const std::string& rel)
{
    std::set<std::string> names;
    std::vector<std::pair<std::string, std::string>> all;
    for (const std::string& d : lhde_dirs()) {
        std::string base = d + "/" + rel;
        DIR* dd = opendir(base.c_str());
        if (!dd) continue;
        while (dirent* e = readdir(dd)) {
            if (e->d_name[0] == '.' || !is_dir(base + "/" + e->d_name) || !names.insert(e->d_name).second) continue;
            all.push_back({ e->d_name, base + "/" + e->d_name });
        }
        closedir(dd);
    }
    std::sort(all.begin(), all.end());
    std::vector<std::string> out;
    for (auto& a : all) out.push_back(a.second);
    return out;
}

std::string theme_build(const SessionTheme& t)
{
    std::string b = t.value("Theme.build");
    if (b.empty()) b = digits4(base_of(t.value("path.resources")));   // resources/4074
    if (b.empty()) b = digits4(t.name);
    if (b.empty()) b = digits4(base_of(t.value("path.style")));
    if (b.empty()) b = digits4(base_of(t.value("path.fonts")));
    return b;
}

std::string default_longhorn_system32()
{
    std::string b;
    const SessionTheme& t = session_theme();
    if (!t.name.empty()) b = theme_build(t);
    else if (const char* s = getenv("FLXP_THEME")) {
        // a style chosen by name or path: styles/slate4074-xp/slate.msstyles, .../slate4051.msstyles
        std::string p = s;
        b = digits4(base_of(p));
        if (b.empty()) b = digits4(base_of(dir_of(p)));
    }
    if (b.empty()) return "";
    std::string r = lhde_find("resources/" + b);
    return is_dir(r) ? r : "";
}

std::vector<std::string> default_font_dirs()
{
    // ReactOS's free faces last: a Windows build's Tahoma (or Marlett) wins where both are imported
    std::vector<std::string> out, free_;
    for (const std::string& d : lhde_subdirs("fonts"))
        if (is_file(d + "/tahoma.ttf") || is_file(d + "/TAHOMA.TTF")) (d.substr(d.rfind('/') + 1) == "reactos" ? free_ : out).push_back(d);
    out.insert(out.end(), free_.begin(), free_.end());
    return out;
}

std::string default_wallpaper_dirs()
{
    std::string v = "~/Pictures/Wallpapers";
    for (const std::string& d : lhde_subdirs("wallpapers")) v += ":" + d;
    return v;
}

std::string lhde_report()
{
    std::string out;
    auto line = [&](const char* what, const std::string& v) { out += std::string(what) + std::string(13 - std::min<size_t>(12, strlen(what)), ' ') + v + "\n"; };
    std::vector<std::string> dirs = lhde_dirs();
    for (size_t i = 0; i < dirs.size(); i++) line(i ? "" : "lhde data", std::to_string(i + 1) + ". " + dirs[i]);
    if (dirs.empty()) line("lhde data", std::string("(none; the user's would be ") + lhde_data_user() + ")");
    // REL in the data directories: which answered, else "not imported"
    auto look = [&](const char* what, const std::string& rel, const std::string& note = "") {
        std::string w = lhde_where(rel);
        line(what, rel + ": " + (w.empty() ? std::string("not imported") : w) + (note.empty() ? "" : " (" + note + ")"));
    };
    const SessionTheme& t = session_theme();
    std::string b = t.name.empty() ? "" : theme_build(t);
    if (!t.name.empty()) {
        line("theme", t.name + " (" + t.value("file") + ")");
        auto key = [&](const char* what, const char* k) {
            std::string v = t.value(k);
            auto w = t.why.find(k);
            line(what, (v.empty() ? std::string("(none)") : v) + (w != t.why.end() && !w->second.empty() ? "  [" + w->second + "]" : ""));
        };
        key("style", "path.style");
        key("icons", "path.icons");
        key("wallpaper", "path.wallpaper");
        key("navbar", "path.navbar");
        key("theme fonts", "path.fonts");
        key("theme res", "path.resources");
        line("build", b.empty() ? "(none)" : b);
    }
    bool have_b = false;
    std::vector<std::string> res = lhde_subdirs("resources");
    for (size_t i = 0; i < res.size(); i++) {
        std::string n = base_of(res[i]);
        have_b = have_b || n == b;
        line(i ? "" : "resources", "resources/" + n + ": " + dir_of(dir_of(res[i])) + (n == b ? " (the theme's build)" : ""));
    }
    if (res.empty()) line("resources", "resources/: not imported");
    if (!b.empty() && !have_b) line("", "resources/" + b + ": not imported (the theme's build)");
    std::string set = setting("longhorn_system32"), sys = theme_setting("longhorn_system32");
    line("system32", sys.empty() ? std::string("(none)") : sys + (!set.empty() ? " (flxp.ini / FLXP_LONGHORN_SYSTEM32)" : " (the theme's path.resources)"));
    look("demo", "resources/demo/docLib", "the task band's aurora");
    look("", "resources/demo/aurora_strip.png");
    std::vector<std::string> fd = default_font_dirs();
    line("fonts", fd.empty() ? std::string("fonts/*/tahoma.ttf: not imported") : fd[0] + " (font_path's default)");
    std::vector<std::string> wd = lhde_subdirs("wallpapers");
    line("wallpapers", wd.empty() ? std::string("wallpapers/: not imported") : std::to_string(wd.size()) + " directories in " + dir_of(wd[0]));
    look("styles", "styles");
    look("XP art", "resources/xp", "the xp icons, toolbar strips, start flag, task pane, Log Off buttons");
    look("", "resources/2k", "the 2k icons and toolbar");
    look("", "resources/reactos", "the free stand-in: ReactOS's icons, toolbar strips and start flag by XP's ids");
    return out;
}

} // namespace shell
