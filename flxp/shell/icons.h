// Icon themes: which files supply the shell's icons and toolbar images.
// A theme is a small ini file, a recipe: lhde's icons/<name>.theme (data/icons,
// found in the data directories) or flxp's res/icons/<name>.theme (wine, the
// fallback); the one selected by the icons setting (the theme's Desktop.icons,
// icons= in flxp.ini or FLXP_ICONS; default "xp", falling back to "wine" when
// its files are not there) maps icon keys to .ico files or to resources inside
// a PE file ("pe:shell32.dll#4"), resolved against [theme] resources =
// resources/<build> (lhde-import's copy of that build's DLLs). No win32 here;
// the explorer does the loading.
#pragma once
#include <string>
#include <vector>

namespace gdi { struct Bitmap; }

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
std::string icon_theme_file(const std::string& name);    // the recipe a name selects ("" if none)

// Windows art by resource id from lhde-import's resources/<build> (docs/flxp-NOTES.md
// "XP and 2000 art by resource id"): resource_file("xp", "explorer.exe") is that file in
// the first data directory that has it, "" when the build is not imported.
std::string resource_file(const std::string& build, const std::string& file);
// "pe:PATH#ID" for that file's resource ID, "" when the file is not imported
std::string resource_src(const std::string& build, const std::string& file, int id);
// XP's shellstyle.dll for a visual style: <style dir>/Shell/<color>/shellstyle.dll or
// styles/NAME/Shell/<color>/ (lhde-import's copy beside styles/NAME.msstyles), the
// colour matched case-insensitively; with no style (classic) system32's from resources/xp.
// "" if there is none.
std::string shellstyle_dll(const std::string& msstyles, const std::string& color);
// An image from a source: "pe:PATH#ID" (that PE's RT_BITMAP ID) or an image file. Null if missing.
gdi::Bitmap* load_art(const std::string& src);

} // namespace shell
