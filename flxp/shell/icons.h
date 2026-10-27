// Icon themes: which files supply the shell's icons and toolbar images.
// A theme is a small ini file (res/icons/<name>.theme, found with
// data_path); the one selected by the icons setting (icons= in flxp.ini or
// FLXP_ICONS; default "xp", falling back to "wine" when its files are not
// there) maps icon keys to .ico files or to icon
// resources inside a PE file ("pe:/path/shell32.dll#4"). No win32 here; the
// explorer does the loading.
#pragma once
#include <string>
#include <vector>

namespace shell {

struct ToolbarTheme {
    std::string strip, hot_strip;   // bitmap strips (file or "pe:path#id"), cells of `size` px
    std::string small_strip, small_hot_strip;   // the small-icon variant, if any
    int size = 24, small_size = 16;
    int back = -1, forward = -1, up = -1, search = -1, folders = -1, views = -1;
    std::string brand;              // throbber strip (square frames, frame 0 = the static logo)
    std::string go, go_hot;         // the address bar's Go glyph
    int go_size = 20;
    std::string close;              // explorer bar close/auto-hide strip (13x11 cells, X = cell 1)
    bool valid() const { return !strip.empty(); }
};

struct IconTheme {
    std::string name;
    std::string base;               // directory relative paths resolve against
    int depth = 32;                 // preferred icon colour depth: 32 for XP, 4 for the Windows 2000 look
    ToolbarTheme toolbar;
    std::vector<std::pair<std::string, std::string>> icons;   // key -> source
};

bool load_icon_theme(const std::string& name_or_path);   // selects the theme; false if not found
// Selects a theme, checking its files exist: falls back to "wine" with a
// warning on stderr; missing toolbar strips are dropped. False on fallback.
bool select_icon_theme(const std::string& name);
const IconTheme& icon_theme();
// The icons setting again (it is per theme: x11/SETTINGS.md "Per-theme settings"):
// selects that theme when it names another one. True if it did.
bool refresh_icon_theme();
int icon_theme_generation();                             // + 1 on every selection
std::string icon_file(const std::string& key);           // resolved source for a key ("" if unknown)
void set_icon_dir(const std::string& dir);               // where <name>.theme files and the wine icons live

} // namespace shell
