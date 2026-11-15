// The picture in place of the Windows flag: starticon.h
#include "res/res.h"   // before anything that pulls in win32 names
#include "starticon.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <vector>
#include "gdi/bitmap.h"
#include "gdi/bmp.h"
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

// windows:BUILD's build: lower case, letters and digits ("" when the value is not windows:BUILD)
std::string flag_build_of(const std::string& v)
{
    std::string l = lower(trim(v));
    if (l.compare(0, 8, "windows:") != 0) return "";
    std::string b = trim(l.substr(8));
    for (char c : b) if (!isalnum((unsigned char)c)) return "";
    return b;
}

// the module of resources/BUILD/FILE (null when the build or the file is not there): free with res::unload
res::Module* build_module(const std::string& build, const char* file)
{
    std::string f = resource_file(build, file);
    return f.empty() ? nullptr : res::load(f.c_str());
}

// RT_BITMAP id of resources/BUILD/FILE, decoded; null when the build, the file or the bitmap is not there
gdi::Bitmap* build_bitmap(const std::string& build, const char* file, int id)
{
    res::Module* m = build_module(build, file);
    if (!m) return nullptr;
    res::Blob b;
    gdi::Bitmap* out = nullptr;
    if (res::find(m, res::Id(res::RT_BITMAP), res::Id((uint32_t)id), b)) out = gdi::decode_bmp(b.data, b.size);
    res::unload(m);
    return out;
}

bool has_bitmap(const std::string& build, const char* file, int id)
{
    res::Module* m = build_module(build, file);
    if (!m) return false;
    res::Blob b;
    bool has = res::find(m, res::Id(res::RT_BITMAP), res::Id((uint32_t)id), b);
    res::unload(m);
    return has;
}

// a bitmap without alpha (2000's 4-bit flag; a 32-bit one whose fourth byte is the same everywhere) gets it from its key
// colours: the button face grey 192 192 192 and magenta are transparent, the rest opaque
void key_alpha(gdi::Bitmap* b)
{
    uint32_t first = b->row(0)[0] >> 24;
    for (int y = 0; y < b->height; y++)
        for (int x = 0; x < b->width; x++)
            if ((b->row(y)[x] >> 24) != first) return;
    for (int y = 0; y < b->height; y++)
        for (int x = 0; x < b->width; x++) {
            uint32_t c = b->row(y)[x] & 0xffffffu;
            b->row(y)[x] = c == 0xc0c0c0u || c == 0xff00ffu ? 0 : c | 0xff000000u;
        }
}

gdi::Bitmap* blank(int w, int h)
{
    gdi::Bitmap* b = gdi::create_bitmap(w, h);
    if (b) for (int y = 0; y < h; y++) memset(b->row(y), 0, (size_t)w * 4);
    return b;
}

// w x h of src at (sx, sy) copied to (dx, dy) of dst, clipped
void copy_rect(const gdi::Bitmap* src, int sx, int sy, int w, int h, gdi::Bitmap* dst, int dx, int dy)
{
    for (int y = 0; y < h; y++) {
        if (sy + y < 0 || sy + y >= src->height || dy + y < 0 || dy + y >= dst->height) continue;
        for (int x = 0; x < w; x++)
            if (sx + x >= 0 && sx + x < src->width && dx + x >= 0 && dx + x < dst->width) dst->row(dy + y)[dx + x] = src->row(sy + y)[sx + x];
    }
}

// the left square of src (side: its height, at most its width)
gdi::Bitmap* left_square(gdi::Bitmap* src)
{
    if (!src || src->width == src->height) return src;
    int side = src->height < src->width ? src->height : src->width;
    gdi::Bitmap* sq = blank(side, side);
    if (sq) copy_rect(src, 0, 0, side, side, sq, 0, 0);
    gdi::destroy_bitmap(src);
    return sq;
}

// src resampled to size x size (src itself when it has that size); takes src
gdi::Bitmap* to_square(gdi::Bitmap* src, int size)
{
    if (!src || (src->width == size && src->height == size)) return src;
    gdi::Bitmap* out = gdi::resample(src, size, size);
    gdi::destroy_bitmap(src);
    return out;
}

const std::vector<std::pair<std::string, std::string>> kNamed = {
    { "lhde", "lhde" }, { "Linux penguin", "linux" }, { "FreeBSD daemon", "freebsd" }, { "Automatic (by the system)", "auto" } };

} // namespace

std::string start_flag_build(const std::string& choice) { return flag_build_of(choice); }

bool start_flag_available(const std::string& build)
{
    if (build.empty() || build == "reactos") return false;
    return has_bitmap(build, "explorer.exe", 143);
}

std::vector<std::string> start_flag_builds()
{
    std::vector<std::string> out;
    for (const char* b : { "2k", "xp" }) if (start_flag_available(b)) out.push_back(b);
    for (const std::string& d : lhde_subdirs("resources")) {
        std::string b = base_of(d);
        if (b == "2k" || b == "xp" || flag_build_of("windows:" + b) != b) continue;
        if (start_flag_available(b)) out.push_back(b);
    }
    return out;
}

std::string start_flag_label(const std::string& build)
{
    if (build == "2k") return "Windows 2000 flag";
    if (build == "xp") return "Windows XP flag";
    size_t n = 0;
    while (n < build.size() && isdigit((unsigned char)build[n])) n++;
    if (n == 4 && (build[0] == '4' || build[0] == '5')) return "Longhorn " + build.substr(0, 4) + " flag";
    return "Windows " + build + " flag";
}

gdi::Bitmap* start_flag_image(const std::string& build, bool styled)
{
    if (!start_flag_available(build)) return nullptr;
    gdi::Bitmap* start = build_bitmap(build, "explorer.exe", 143);
    if (!start) return nullptr;
    key_alpha(start);
    if (styled) {
        if (start->width == 25 && start->height == 20) return start;
        // a smaller flag (2000's one bitmap, 16 px) centred where XP's 20 px flag is: the cell's left 20 x 20
        gdi::Bitmap* cell = blank(25, 20);
        if (cell) {
            int w = start->width < 20 ? start->width : 20, h = start->height < 20 ? start->height : 20;
            copy_rect(start, 0, 0, w, h, cell, (20 - w) / 2, (20 - h) / 2);
        }
        gdi::destroy_bitmap(start);
        return cell;
    }
    if (gdi::Bitmap* classic = build_bitmap(build, "explorer.exe", 176)) {
        gdi::destroy_bitmap(start);
        key_alpha(classic);
        return to_square(classic, 16);
    }
    // no classic flag: 143 at 16 px, its left square (the flag's 20 columns of a 25 x 20 cell) resampled
    return to_square(left_square(start), 16);
}

gdi::Bitmap* start_flag_brand(const std::string& build, int size)
{
    if (!start_flag_available(build) || size <= 0) return nullptr;
    gdi::Bitmap* strip = build == "2k" ? build_bitmap(build, "browseui.dll", 306) : build_bitmap(build, "shell32.dll", 240);
    if (!strip) return nullptr;
    // the strip's frames are squares stacked from the top; frame 0 is the still logo
    gdi::Bitmap* frame = strip->width > 0 && strip->width <= strip->height ? blank(strip->width, strip->width) : nullptr;
    if (frame) {
        copy_rect(strip, 0, 0, frame->width, frame->height, frame, 0, 0);
        for (int y = 0; y < frame->height; y++) for (int x = 0; x < frame->width; x++) frame->row(y)[x] |= 0xff000000u;
    }
    gdi::destroy_bitmap(strip);
    return to_square(frame, size);
}

std::vector<std::pair<std::string, std::string>> start_icon_items()
{
    std::vector<std::pair<std::string, std::string>> out = { { "Windows flag (the theme's)", "windows" } };
    for (const std::string& b : start_flag_builds()) out.push_back({ start_flag_label(b), "windows:" + b });
    out.insert(out.end(), kNamed.begin(), kNamed.end());
    return out;
}

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
    if (l.compare(0, 8, "windows:") == 0) {
        std::string b = flag_build_of(l);
        return start_flag_available(b) ? "windows:" + b : "windows";   // not imported, or without a flag: the theme's
    }
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
    std::string build = flag_build_of(choice);
    if (!build.empty()) {
        if (!start_flag_available(build)) return "";
        int id = size <= 16 && has_bitmap(build, "explorer.exe", 176) ? 176 : 143;
        return "pe:" + resource_file(build, "explorer.exe") + "#" + std::to_string(id);
    }
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
    std::string build = flag_build_of(choice);
    if (!build.empty())   // the flag alone: the classic cell, or the styled cell's left square, at size
        return size > 0 ? to_square(left_square(start_flag_image(build, size > 16)), size) : nullptr;
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
