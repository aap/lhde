// A theme's desktop background: see wallpapers.h and docs/THEME-FORMAT.md.
#include "wallpapers.h"
#include <dirent.h>
#include <algorithm>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "config.h"
#include "lhdata.h"
#include "settings.h"

namespace shell {

uint32_t parse_color(const std::string& v, uint32_t fallback)
{
    unsigned r, g, b;
    char extra;
    if (sscanf(v.c_str(), " %u %u %u %c", &r, &g, &b, &extra) == 3 && r < 256 && g < 256 && b < 256) return r << 16 | g << 8 | b;
    std::string h = v;
    if (!h.empty() && h[0] == '#') h = h.substr(1);
    else if (h.size() > 2 && h[0] == '0' && (h[1] == 'x' || h[1] == 'X')) h = h.substr(2);
    if (h.size() == 6 && h.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos) return (uint32_t)strtoul(h.c_str(), nullptr, 16);
    return fallback;
}

bool theme_wallpaper(const SessionTheme& st, Wallpaper& out)
{
    out = Wallpaper();
    out.color = parse_color(st.value("Desktop.color"));
    // the classic look (THEME-FORMAT section 2a): the desktop is COLOR_BACKGROUND, a scheme's or an Advanced edit's
    if (st.classic && !st.value("Colors.background").empty()) out.color = parse_color(st.value("Colors.background"), out.color);
    out.mode = st.value("Desktop.mode");
    std::string v = st.value("Desktop.wallpaper"), f = st.value("path.wallpaper");
    if (v == "none" || f.empty()) return false;
    struct stat sb;
    if (stat(f.c_str(), &sb) != 0) return false;
    out.file = f;
    return true;
}

bool wallpaper_hidden() { return strcasecmp(store::get("desktop", "wallpaper").c_str(), "none") == 0; }

std::string wallpaper_value(const std::string& file)
{
    for (const std::string& d : lhde_dirs()) {
        std::string root = d + "/wallpapers/";
        if (file.compare(0, root.size(), root)) continue;
        std::string rel = file.substr(root.size());
        if (std::count(rel.begin(), rel.end(), '/') > 1) break;
        size_t dot = rel.rfind('.');
        if (dot == std::string::npos || dot < rel.rfind('/') + 1) break;
        std::string stem = rel.substr(0, dot), dir = dir_of(file), sb = base_of(stem);
        // another picture of the same stem (Bliss.jpg and Bliss.bmp): the path is unambiguous
        int same = 0;
        if (DIR* dd = opendir(dir.c_str())) {
            while (dirent* e = readdir(dd)) {
                std::string n = e->d_name;
                size_t p = n.rfind('.');
                if (p != std::string::npos && !strcasecmp(n.substr(0, p).c_str(), sb.c_str())) same++;
            }
            closedir(dd);
        }
        return same == 1 ? stem : file;
    }
    return file;
}

bool set_desktop_background(const std::string& file, const std::string& mode, std::string* err)
{
    const SessionTheme& st = session_theme();
    std::string theme = st.name;
    if (theme.empty()) theme = store::get("theme", "name");
    if (theme.empty()) { if (err) *err = "no theme is selected (_LH_THEME, settings.ini [theme] name)"; return false; }
    std::vector<store::KeyValue> kv = { { "Desktop.wallpaper", file == "none" ? "none" : wallpaper_value(file) } };
    if (!mode.empty()) kv.push_back({ "Desktop.mode", mode });
    // the user's "no picture" would hide it
    if (file != "none" && wallpaper_hidden()) store::set("desktop", "wallpaper", "");
    // the selected theme's current layer (docs/THEME-FORMAT.md section 3): no theme file is written
    if (store::get("theme", "name") != theme) store::apply_theme(theme);
    bool ok = store::set_current_keys(kv, true, "desktop");
    if (!ok && err) *err = "cannot write " + store::path();
    return ok;
}

} // namespace shell
