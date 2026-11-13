// Icon themes: which files supply the shell's icons and toolbar images.
// A theme is a small ini file, a recipe: lhde's icons/<name>.theme (share/icons,
// found in the data directories) or flxp's res/icons/<name>.theme (wine, the
// fallback); the one the theme names (Desktop.icons, shell::theme_icons;
// default "xp", falling back to "wine" when
// its files are not there) maps icon keys to .ico files or to resources inside
// a PE file ("pe:shell32.dll#4"), resolved against [theme] resources =
// resources/<build> (lhde-import's copy of that build's DLLs), or to a file of
// an imported icon pack ("file:xp-hires/My Computer.ico": index/icons/PACK/PATH,
// lhde-import --icons). [theme] fallback = RECIPE... : the first usable one fills
// the keys this recipe lacks or whose files are missing, and its toolbar when
// this one has none. No win32 here; the explorer does the loading.
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
// The icons setting again (a theme key, Desktop.icons: docs/THEME-FORMAT.md):
// selects that theme when it names another one. True if it did.
bool refresh_icon_theme();
int icon_theme_generation();                             // + 1 on every selection
std::string icon_file(const std::string& key);           // resolved source for a key ("" if unknown); a swatch key is its own
// A colour swatch as an icon key and source ("swatch:ff0000"): load_image draws it (a square of 3/4 of the size, a 1 px
// dark border, centred, transparent around it), so image lists take it as any other icon
std::string swatch_key(unsigned rgb);
std::string icon_theme_file(const std::string& name);    // the recipe a name selects ("" if none)

// Windows art by resource id from lhde-import's resources/<build>: resource_file("xp", "explorer.exe") is that file in
// the first data directory that has it, "" when the build is not imported.
std::string resource_file(const std::string& build, const std::string& file);
// XP's art by resource id, named as XP has it (file, id, the resource type: RT_BITMAP 2, RT_GROUP_ICON 14, 0 for "AVI"),
// from the first imported source that has that resource: xp, then 2k, then reactos (the free baseline: ReactOS keeps XP's
// ids, and where it does not, the per-source table in icons.cpp maps them). "pe:PATH#ID" or "" when none has it.
std::string xp_art(const std::string& file, int id, int type);
// XP's shellstyle.dll for a visual style: <style dir>/Shell/<color>/shellstyle.dll or
// styles/NAME/Shell/<color>/ (lhde-import's copy beside styles/NAME.msstyles), the
// colour matched case-insensitively; with no style (classic) system32's from resources/xp.
// "" if there is none.
std::string shellstyle_dll(const std::string& msstyles, const std::string& color);
// An image from a source: "pe:PATH#ID" (that PE's RT_BITMAP ID) or an image file. Null if missing.
gdi::Bitmap* load_art(const std::string& src);

// The icon of a window or a program, the answer every lhde consumer shares (liblhde lhde_app_icon, docs/THEME-FORMAT.md
// section 9): the class map of WM_CLASS (wm_class, instance), the desktop file (desktop_id, a .desktop id, or found by the
// class), the generic application icon (generic false: not that one). One image of px x px in _NET_WM_ICON's layout
// { px, px, ARGB (straight alpha)... }, scaled from the source's nearest size at or above; empty when nothing answers. The
// keys come from the selected icon set (select_icon_theme hands it to liblhde).
std::vector<unsigned long> app_icon(const std::string& wm_class, const std::string& instance, const std::string& desktop_id,
                                    int px, bool generic = true);
// An icon key of the icon sets in the same layout (liblhde: this set, its fallbacks, the free chain): empty when none has it
std::vector<unsigned long> key_icon(const std::string& key, int px);
// A window's icon when it has no _NET_WM_ICON (the caller reads that): the class map and desktop file of its WM_CLASS, then
// hint(window) (its WM_HINTS pixmap as such an array, the caller's), then the generic icon; display is the X Display*
std::vector<unsigned long> window_icon(void* display, unsigned long window, int px, std::vector<unsigned long> (*hint)(unsigned long));

} // namespace shell
