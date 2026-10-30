#include "viewmodel.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

namespace shell {
namespace viewmodel {

// xaml_enhancements_viewpopup.xaml (4074): ViewMode, ImageSize, ImageSizeMin/Max, SizeAscending
static const Stop k_stops[STOPS] = {
    { Mode::Details, 16, 32, true, 16, "Details" },
    { Mode::Tiles, 32, 96, true, 48, "Tiles" },
    { Mode::Icons, 128, 256, false, 128, "Large Icons" },
    { Mode::Icons, 48, 128, false, 48, "Medium Icons" },
    { Mode::Icons, 16, 48, false, 16, "Small Icons" },
};

const Stop& stop(int i) { return k_stops[i < 0 ? 0 : i >= STOPS ? STOPS - 1 : i]; }

int min_size(Mode m) { return m == Mode::Tiles ? 32 : 16; }
int max_size(Mode m) { return m == Mode::Details ? 32 : m == Mode::Tiles ? 96 : 256; }

View clamped(View v)
{
    if (v.size < min_size(v.mode)) v.size = min_size(v.mode);
    if (v.size > max_size(v.mode)) v.size = max_size(v.mode);
    return v;
}

const char* mode_name(Mode m) { return m == Mode::Details ? "details" : m == Mode::Tiles ? "tiles" : "icons"; }

std::string format(const View& v)
{
    return std::string(mode_name(v.mode)) + "@" + std::to_string(v.size) + (v.hide_names ? "+nonames" : "");
}

static bool ends_with(std::string& s, const char* tail)
{
    size_t n = strlen(tail);
    if (s.size() < n || s.compare(s.size() - n, n, tail) != 0) return false;
    s.resize(s.size() - n);
    return true;
}

bool parse(const std::string& text, View& out, bool* sized)
{
    std::string n = text;
    int size = 0;
    size_t at = n.find('@');
    if (at != std::string::npos) {
        std::string rest = n.substr(at + 1);
        size = atoi(rest.c_str());
        n.resize(at);
        size_t plus = rest.find('+');
        if (plus != std::string::npos) n += rest.substr(plus);   // "icons@96+nonames"
    }
    View v;
    for (;;) {   // flags, in any order
        if (ends_with(n, "+nonames")) v.hide_names = true;
        else if (ends_with(n, "+thumbnails")) {}   // the earlier per-folder toggle: thumbnails are a setting now
        else break;
    }
    if (n == "details" || n == "list") v.mode = Mode::Details;
    else if (n == "tiles") v.mode = Mode::Tiles;
    else if (n == "icons" || n == "thumbnails") v.mode = Mode::Icons;
    else if (n == "smallicons") { v.mode = Mode::Icons; if (!size) size = 16; }
    else return false;
    if (n == "list") size = 0;   // (List had no size)
    bool has = size >= min_size(v.mode) && size <= max_size(v.mode);
    if (!has && size > 0) { v.size = size; v = clamped(v); has = true; }
    else v.size = has ? size : k_stops[v.mode == Mode::Details ? STOP_DETAILS : v.mode == Mode::Tiles ? STOP_TILES : STOP_MEDIUM].start;
    if (sized) *sized = has;
    out = v;
    return true;
}

int stop_of(const View& v)
{
    View c = clamped(v);
    for (int i = 0; i < STOPS; i++)
        if (k_stops[i].mode == c.mode && c.size >= k_stops[i].min && c.size <= k_stops[i].max) return i;
    return c.mode == Mode::Details ? STOP_DETAILS : c.mode == Mode::Tiles ? STOP_TILES : STOP_MEDIUM;
}

int size_at(int i, int y, int h)
{
    const Stop& s = stop(i);
    if (h < 1) h = 1;
    if (y < 0) y = 0;
    if (y > h) y = h;
    int size = s.min + (s.max - s.min) * y / h;
    return s.ascending ? size : s.max - (size - s.min);
}

int pos_of(int i, int size, int h)
{
    const Stop& s = stop(i);
    if (size < s.min) size = s.min;
    if (size > s.max) size = s.max;
    if (!s.ascending) size = s.max - (size - s.min);
    return s.max > s.min ? (size - s.min) * h / (s.max - s.min) : 0;
}

std::vector<int> levels(Mode m)
{
    std::vector<int> out;
    out.push_back(min_size(m));
    for (int pos = 0; pos <= 100; pos += 10) {
        int px = (int)floor(16.0 * pow(16.0, pos / 100.0) + 1e-9);
        if (px > out.back() && px < max_size(m)) out.push_back(px);
    }
    if (out.back() != max_size(m)) out.push_back(max_size(m));
    return out;
}

View step(const View& v, int dir)
{
    View c = clamped(v), out = c;
    for (int px : levels(c.mode)) {
        if (dir > 0 && px > c.size) { out.size = px; break; }
        if (dir < 0 && px < c.size) out.size = px;
    }
    return out;
}

Memory::Memory() { for (int i = 0; i < STOPS; i++) size[i] = k_stops[i].start; }

void Memory::shown(const View& v)
{
    View c = clamped(v);
    int i = stop_of(c);
    size[i] = c.size;
    if (c.mode == Mode::Icons) icons = i;
}

View Memory::at(int i) const
{
    View v;
    v.mode = stop(i).mode;
    v.size = size[i < 0 ? 0 : i >= STOPS ? STOPS - 1 : i];
    return clamped(v);
}

View Memory::recall(Mode m) const
{
    return at(m == Mode::Details ? STOP_DETAILS : m == Mode::Tiles ? STOP_TILES : icons);
}

View Memory::next(const View& v) const
{
    Mode m = v.mode == Mode::Details ? Mode::Tiles : v.mode == Mode::Tiles ? Mode::Icons : Mode::Details;
    View n = recall(m);
    n.hide_names = v.hide_names;   // the folder's choice goes along (it shows in Icons only)
    return n;
}

static const char* k_keys[STOPS] = { "details", "tiles", "large", "medium", "small" };

std::string Memory::save() const
{
    std::string out;
    for (int i = 0; i < STOPS; i++) out += std::string(k_keys[i]) + "=" + std::to_string(size[i]) + " ";
    return out + "icons=" + k_keys[icons];
}

void Memory::load(const std::string& text)
{
    size_t at = 0;
    while (at < text.size()) {
        size_t sp = text.find_first_of(" \t\n", at);
        std::string word = text.substr(at, sp == std::string::npos ? std::string::npos : sp - at);
        at = sp == std::string::npos ? text.size() : sp + 1;
        size_t eq = word.find('=');
        if (eq == std::string::npos) continue;
        std::string k = word.substr(0, eq), val = word.substr(eq + 1);
        for (int i = 0; i < STOPS; i++) {
            if (k == k_keys[i]) {
                int px = atoi(val.c_str());
                if (px >= k_stops[i].min && px <= k_stops[i].max) size[i] = px;
            }
            if (k == "icons" && val == k_keys[i] && k_stops[i].mode == Mode::Icons) icons = i;
        }
    }
}

} // namespace viewmodel
} // namespace shell
