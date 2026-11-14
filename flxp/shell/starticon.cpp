// The picture in place of the Windows flag: starticon.h
#include "starticon.h"
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <vector>
#include "gdi/bitmap.h"
#include "gdi/image.h"
#include "config.h"
#include "icons.h"
#include "lhdata.h"
#include "lhtheme.h"
#include "settings.h"

namespace shell {

namespace {

bool named(const std::string& c) { return c == "lhde" || c == "linux" || c == "freebsd"; }
bool is_path(const std::string& v) { return !v.empty() && (v[0] == '/' || v.compare(0, 2, "~/") == 0); }

// an icon key's image from the icon sets at 32 px, as a bitmap
gdi::Bitmap* key_bitmap(const std::string& key)
{
    std::vector<unsigned long> im = key_icon(key, 32);
    if (im.size() < 2) return nullptr;
    int w = (int)im[0], h = (int)im[1];
    if (w <= 0 || h <= 0 || im.size() < 2 + (size_t)w * h) return nullptr;
    gdi::Bitmap* b = gdi::create_bitmap(w, h);
    if (!b) return nullptr;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) b->row(y)[x] = (uint32_t)im[2 + (size_t)y * w + x];
    return b;
}

} // namespace

std::string start_icon_setting()
{
    std::string v = trim(store::get("taskbar", "start_icon"));
    if (v.empty()) v = trim(session_theme().value("Shell.start_icon"));
    return v.empty() ? "windows" : v;
}

std::string start_icon_choice(const std::string& value)
{
    std::string v = trim(value), l = lower(v);
    if (l.empty() || l == "windows") return "windows";
    if (l == "auto") {
        const char* fake = getenv("LHDE_UNAME");
        std::string sys;
        struct utsname u;
        if (fake && *fake) sys = fake;
        else if (uname(&u) == 0) sys = u.sysname;
        sys = lower(sys);
        return sys == "linux" ? "linux" : sys == "freebsd" ? "freebsd" : "lhde";
    }
    if (named(l)) return l;
    return v;
}

std::string start_icon_source(const std::string& choice, int size)
{
    if (choice.empty() || choice == "windows") return "";
    if (named(choice)) {
        std::string f = lhde_find("icons/flags/" + choice + "-" + std::to_string(size) + ".png");
        return f.empty() ? lhde_find("icons/flags/" + choice + "-32.png") : f;
    }
    if (choice.compare(0, 5, "file:") == 0) return lhde_find("icons/" + choice.substr(5));
    if (is_path(choice)) { std::string f = lhde_resolve(choice); return is_file(f) ? f : ""; }
    return key_icon(choice, 32).empty() ? "" : "key:" + choice;
}

gdi::Bitmap* start_icon_image(const std::string& choice, int size)
{
    std::string src = start_icon_source(choice, size);
    if (src.empty() || size <= 0) return nullptr;
    gdi::Bitmap* im = src.compare(0, 4, "key:") == 0 ? key_bitmap(src.substr(4)) : gdi::load_image(src);
    if (!im) return nullptr;
    if (im->width == size && im->height == size) return im;
    // fitted into size x size keeping the aspect ratio (a larger image shrinks, a smaller one grows), centred
    int w = 0, h = 0;
    gdi::fit_size(im->width, im->height, size, size, true, &w, &h);
    gdi::Bitmap* fitted = gdi::resample(im, w, h);
    gdi::destroy_bitmap(im);
    if (!fitted) return nullptr;
    gdi::Bitmap* out = gdi::create_bitmap(size, size);
    if (!out) { gdi::destroy_bitmap(fitted); return nullptr; }
    for (int y = 0; y < size; y++) memset(out->row(y), 0, (size_t)size * 4);
    int ox = (size - w) / 2, oy = (size - h) / 2;
    for (int y = 0; y < h; y++) memcpy(out->row(oy + y) + ox, fitted->row(y), (size_t)w * 4);
    gdi::destroy_bitmap(fitted);
    return out;
}

} // namespace shell
