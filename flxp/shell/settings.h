// lhde's settings store for flxp (docs/SETTINGS.md): liblhde's
// ~/.config/lhde/settings.ini (lhde/settings.h) with the per-user keys, the
// theme keys written into theme files (lhde_theme_set_key), the theme list, and
// the _LH_SETTINGS_CHANGED root message. Shared by luxwm, luxshell and the flxp
// programs. No win32 here; the notification uses a short X connection of its own.
//
// The desktop's shared keys ([fonts], [effects], [start], [commands], [taskbar],
// ...) are read here and only here; flxp.ini (config.h) holds flxp's own
// preferences, the theme's keys come from the session theme's record
// (shell::theme_setting, lhtheme.h).
#pragma once
#include <memory>
#include <string>
#include <vector>

struct lhde_ini;   // liblhde's line-keeping ini document (lhde/settings.h)

namespace shell {
namespace store {

// An ini file kept line by line: liblhde's lhde_ini (the reader and writer of
// settings.ini for every program). Sections and keys compare case-insensitively;
// a duplicate key: the last one counts. set() rewrites a value in place
// (indentation and a trailing comment kept, an earlier duplicate dropped), else
// appends the key to its section, else the section to the file. Move-only.
struct Ini {
    Ini();
    bool load(const std::string& path);           // false if unreadable (the document left empty)
    void parse(const std::string& text);
    std::string text() const;
    bool save(const std::string& path) const;     // atomic, through a symlink, mkdir -p
    bool has(const std::string& section, const std::string& key) const;
    // "" (absent or empty) -> fallback
    std::string get(const std::string& section, const std::string& key, const std::string& fallback = "") const;
    void set(const std::string& section, const std::string& key, const std::string& value);
    void remove(const std::string& section, const std::string& key);   // and the section when no key is left
    std::vector<std::string> keys(const std::string& section) const;
private:
    struct Free { void operator()(lhde_ini* d) const; };
    std::unique_ptr<lhde_ini, Free> doc;
};

std::string dir();                                // liblhde's lhde_config_dir(): ~/.config/lhde
std::string path();                               // lhde_settings_path(): dir()/settings.ini ($LHDE_SETTINGS)

// settings.ini's value (lhde_settings_get: re-read when the file changed); "" = unset -> fallback
std::string get(const std::string& section, const std::string& key, const std::string& fallback = "");
struct Edit { std::string section, key, value; };   // value "" removes the key
// through liblhde's store (lhde_settings_set: unknown keys, comments, order kept)
bool set(const std::vector<Edit>& edits);
bool set(const std::string& section, const std::string& key, const std::string& value);
// a settings.ini switch: 1 or 0 (shell::flag_value); unset or another value: fallback
bool flag(const std::string& section, const std::string& key, bool fallback);

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
struct ThemeEntry { std::string name, title, file; int group; bool style_missing = false; };
std::vector<ThemeEntry> theme_list();
// the theme files only (groups 0 and 1): what Display Properties' Themes tab and the start menu's Theme list offer;
// style_missing set where the theme's visual style is not imported (lhde_theme_has_style): the lists leave those out,
// except the selected theme
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

// luxshell runs: _LHSHELL_S<screen> is owned
bool lux_running();

// The Display Properties program: flxp-settings next to this one (installed, or build/bin
// in the build tree), then $PATH; "flxp-settings"
// when none is found (spawn reports it).
std::string settings_program();
bool launch_settings(const std::string& tab = "");

} // namespace store
} // namespace shell
