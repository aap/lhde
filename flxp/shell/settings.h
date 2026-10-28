// lhde's settings store for flxp (docs/SETTINGS.md "lhde (step 4)"): liblhde's
// ~/.config/lhde/settings.ini (lhde/settings.h) with the per-user keys, the
// theme keys written into theme files (lhde_theme_set_key), the theme list, and
// the _LH_SETTINGS_CHANGED root message. Shared by luxwm, luxshell and the flxp
// programs. No win32 here; the notification uses a short X connection of its own.
//
// Precedence for a flxp program: FLXP_<KEY> (the run's options) > flxp.ini >
// settings.ini (font_smoothing, pinned: value_for) > the default; the theme's
// keys come from the session theme's record (shell::theme_setting, lhtheme.h).
#pragma once
#include <string>
#include <vector>

namespace shell {
namespace store {

// An ini file kept line by line: set() rewrites a value in place (indentation
// and a trailing "; comment" kept), else appends the key to its section, else
// the section to the file. Sections and keys compare case-insensitively.
struct Ini {
    std::vector<std::string> lines;
    bool load(const std::string& path);           // false if unreadable (lines left empty)
    void parse(const std::string& text);
    std::string text() const;
    bool save(const std::string& path) const;     // atomic, mkdir -p
    bool has(const std::string& section, const std::string& key) const;
    std::string get(const std::string& section, const std::string& key, const std::string& fallback = "") const;
    void set(const std::string& section, const std::string& key, const std::string& value);
    void remove(const std::string& section, const std::string& key);   // and the section when no key is left
    std::vector<std::string> keys(const std::string& section) const;
};

std::string dir();                                // liblhde's lhde_config_dir(): ~/.config/lhde
std::string path();                               // lhde_settings_path(): dir()/settings.ini ($LHDE_SETTINGS)

// settings.ini now (re-read when the file changed); "" = unset -> fallback
std::string get(const std::string& section, const std::string& key, const std::string& fallback = "");
struct Edit { std::string section, key, value; };   // value "" removes the key
// through liblhde's store (lhde_settings_set: unknown keys, comments, order kept)
bool set(const std::vector<Edit>& edits);
bool set(const std::string& section, const std::string& key, const std::string& value);

// set() for a settings UI: a key that maps to a flxp.ini key which flxp.ini
// sets (and so would win) is rewritten in flxp.ini as well.
bool set_shared(const std::vector<Edit>& edits);

// settings.ini's value for a flxp.ini key, the per-user ones only: font_smoothing ([fonts] smoothing),
// pinned ([start] pinned). "" if not mapped or not set. The theme keys (icons, frame, taskbar_kind,
// start_menu, wallpaper) are the theme's now: shell::theme_setting, the session theme's record.
std::string value_for(const std::string& flxp_key);
// the (section, key) a flxp key maps to; false if none
bool mapping(const std::string& flxp_key, std::string& section, std::string& key);

// [sidebar] stacking: top | bottom, a per-user key (Win+S in luxwm flips it)
std::string sidebar_stacking();
bool set_sidebar_stacking(const std::string& value);

// Theme keys (docs/THEME-FORMAT.md section 3): KEY = VALUE pairs ("Desktop.wallpaper") written with lhde_theme_set_key
// into THEME's file when it is the user's own, else into THEME-modified.theme (created with base = THEME, selected);
// a value equal to the base's removes the key, "" removes it too. Then the order of SETTINGS.md: the theme selected
// (apply_theme) when that is another name, else _LH_SETTINGS_CHANGED. selected gets the theme in force afterwards.
// False when a file could not be written (err: which).
struct KeyValue { std::string key, value; };
bool set_theme_keys(const std::string& theme, const std::vector<KeyValue>& kv, std::string* selected = nullptr,
                    std::string* err = nullptr, bool notify_now = true);

// The themes as liblhde lists them: the user's theme files, the shipped ones, the bare styles (group 0, 1, 2)
struct ThemeEntry { std::string name, title, file; int group; };
std::vector<ThemeEntry> theme_list();
// the theme files only (groups 0 and 1): what Display Properties' Themes tab and the start menu's Theme list offer
std::vector<ThemeEntry> theme_files();

// The current layer (docs/THEME-FORMAT.md section 3, Windows' "NAME (Modified)"): settings.ini [current], the unsaved
// theme keys on top of the selected theme. Display Properties and Set as Desktop Background write here, not a theme file.
std::vector<KeyValue> current_layer();            // the selected theme's (lhde_current_keys)
// the whole layer replaced (lhde_current_set: keys the theme gives anyway are dropped); no notification
bool set_current_layer(const std::vector<KeyValue>& kv);
// keys merged into the layer ("" removes one), then _LH_SETTINGS_CHANGED when notify_now
bool set_current_keys(const std::vector<KeyValue>& kv, bool notify_now = true, const std::string& hint = "theme");
// Save As: the selected theme with its layer as ~/.config/lhde/themes/FILE.theme (lhde_theme_save_as), selected, announced
bool save_theme_as(const std::string& file, const std::string& title, std::string* err = nullptr);
// Delete one of the user's theme files (lhde_theme_delete); the selected one: its base selected and announced
bool delete_theme(const std::string& name);

// _LH_SETTINGS_CHANGED to the root (data[0] serial: _LH_SETTINGS_SERIAL + 1,
// data[1] the section hint's atom or 0). False without a display.
bool notify(const std::string& section_hint = "");
// A theme change in the documented order: settings.ini [theme] name (lhde_theme_select: the current layer of another
// theme is dropped), _LH_THEME, notify(hint).
bool apply_theme(const std::string& name, const std::string& hint = "theme");

// lux (lhshell) runs: _LHSHELL_S<screen> is owned
bool lux_running();

// The Display Properties program: flxp-settings next to this one (installed, or build/bin
// in the build tree), then $PATH; "flxp-settings"
// when none is found (spawn reports it).
std::string settings_program();
bool launch_settings(const std::string& tab = "");

} // namespace store
} // namespace shell
