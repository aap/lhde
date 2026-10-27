// The Longhorn session's shared settings store (x11/SETTINGS.md):
// $XDG_CONFIG_HOME/longhorn/settings.ini, the user presets in themes/NAME.theme
// and the _LH_SETTINGS_CHANGED root message. Shared by lhwm, lux and the flxp
// programs. No win32 here; the notification uses a short X connection of its own.
//
// Precedence for a flxp program: FLXP_<KEY> (the run's options) > flxp.ini >
// settings.ini > the session theme (its flxp_<key>, wallpaper, ...) > the default.
// shell::setting / file_setting consult settings.ini for the flxp keys mapped
// below (store_value_for), so readers need nothing new.
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

std::string dir();                                // $XDG_CONFIG_HOME/longhorn
std::string path();                               // dir()/settings.ini

// settings.ini now (re-read when the file changed); "" = unset -> fallback
std::string get(const std::string& section, const std::string& key, const std::string& fallback = "");
struct Edit { std::string section, key, value; };
// read, modify, write back the whole file (unknown keys, comments, order kept)
bool set(const std::vector<Edit>& edits);
bool set(const std::string& section, const std::string& key, const std::string& value);

// set() for a settings UI: a key that maps to a flxp.ini key which flxp.ini
// sets (and so would win) is rewritten in flxp.ini as well.
bool set_shared(const std::vector<Edit>& edits);

// settings.ini's value for a flxp.ini key: icons, wallpaper, wallpaper_mode,
// font_smoothing, pinned, start_menu (flxp -> "", lux -> lhshell), taskbar_kind,
// frame. "" if not mapped or not set. icons, frame, taskbar_kind and start_menu
// are per-theme: theme_value for the session theme.
std::string value_for(const std::string& flxp_key);
// the (section, key) a flxp key maps to; false if none
bool mapping(const std::string& flxp_key, std::string& section, std::string& key);

// Per-theme settings (x11/SETTINGS.md "Per-theme settings"): icons, frame,
// taskbar, start, composition, animations. The value for theme NAME (a registry
// section or a preset), the first one set of:
//   1 settings.ini [override:NAME] KEY                  (Source::Override)
//   2 the preset's own ([Desktop] icons, [Explorer] frame, [Shell] taskbar/start, [Composition] ...)  (Preset)
//   3 settings.ini [override] KEY: chosen "for all themes"  (AllThemes)
//   4 the registry entry (flxp_icons, flxp_frame, taskbar, start, animations; composition: none)  (Theme)
//   5 the global settings.ini key ([desktop] icons, [explorer] frame, [taskbar] kind, [start] menu,
//     [theme] composition / animations)                (Global)
//   6 the built-in default                              (Default)
enum class Source { Override, Preset, AllThemes, Theme, Global, Default };
std::string theme_value(const std::string& theme, const std::string& key, const std::string& registry, Source* src = nullptr);
// what the theme gives without its own value (1, 2: a preset counts its base's); without_all: from 4 on
std::string theme_default(const std::string& theme, const std::string& key, const std::string& registry, bool without_all = false);
// Stores per-theme values for NAME: into the preset when NAME is one, else into settings.ini
// [override:NAME]; a value equal to theme_default removes the key. flxp.ini's key of the same
// meaning (icons, frame, taskbar_kind, start_menu) is blanked when it is set, as it would win.
struct KeyValue { std::string key, value; };
bool set_theme_values(const std::string& theme, const std::vector<KeyValue>& kv, const std::string& registry);
// "All themes": [override] KEY and the global key = value, and every [override:NAME] KEY and every
// preset's own KEY removed (flxp.ini's key blanked), so each theme shows the value. An empty value
// is "each theme's own": [override] KEY and the per-theme keys removed, the global key kept.
bool set_all_themes_values(const std::vector<KeyValue>& kv);
// The registry section NAME is (a section, or in a section's `aliases`; case-insensitive), "" if none
std::string registry_section(const std::string& registry, const std::string& name);
// the names an [override:...] section may carry for theme NAME: the section's name first, then its aliases
// (x11/SETTINGS.md: [aero4074] is [aero-patch] with the alias aero4074); a preset or unknown name: just itself
std::vector<std::string> theme_names(const std::string& theme, const std::string& registry);
// [sidebar] stacking: top | bottom, a global key (not per theme; Win+S in lhwm flips it). The setter also removes
// the older per-theme sidebar_stacking keys ([override], [override:*], presets' [Shell]); "" only cleans.
std::string sidebar_stacking();
bool set_sidebar_stacking(const std::string& value);
// the preset's base registry section (_LH_THEME_RESOLVED's base= while it describes NAME), "" if NAME is no preset
std::string preset_base(const std::string& name);

// Presets: themes/NAME.theme
std::string preset_dir();
std::string preset_path(const std::string& name);  // "" if NAME is not a safe file name
std::vector<std::string> preset_names();           // sorted
bool read_preset(const std::string& name, Ini& out);
bool delete_preset(const std::string& name);

// The registry: _LH_THEMES_INI (root), else $LH_THEMES_INI, else flxp.ini's themes_ini
std::string registry_path();
struct ThemeEntry { std::string name, title; bool preset; };
std::vector<ThemeEntry> theme_list(const std::string& registry);   // registry sections, then presets

// _LH_SETTINGS_CHANGED to the root (data[0] serial: _LH_SETTINGS_SERIAL + 1,
// data[1] the section hint's atom or 0). False without a display.
bool notify(const std::string& section_hint = "");
// A theme change in the documented order: settings.ini [theme] name, _LH_THEME, notify(hint).
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
