// The Longhorn lab's session theme (~/fun/longhorn/x11/THEMES.md): the root
// property _LH_THEME holds a theme name, _LH_THEMES_INI the path of the
// registry (themes.ini) that defines the names. flxp follows it: the visual
// style comes from the theme's msstyles/color/size, and the flxp_* keys of
// the same section carry what flxp's Longhorn chrome needs per theme. No
// win32 here; the run-time reload is the shim's (w32/theme.cpp).
#pragma once
#include <map>
#include <string>

namespace shell {

struct SessionTheme {
    std::string name;            // the registry section ("" = none)
    std::string ini;             // the registry file
    std::map<std::string, std::string> keys;   // the section's keys (paths not resolved)
    std::string msstyles;        // what our uxtheme loads ("" = no loadable style found)
    std::string color, size;     // as the registry says (uxtheme falls back to the style's first)
    std::string value(const std::string& key) const;     // "" if absent
    std::string path(const std::string& key) const;      // a path key, made absolute (relative to the registry)
};

// Reads the root properties now (its own short X connection; for start-up,
// before the toolkit opens the display). False if there is no _LH_THEME.
bool read_session_properties(std::string& name, std::string& ini);

// Looks a name (a section, an alias, or a path whose last component is one)
// up in the registry file and resolves the visual style. False if the name
// is not in the registry.
bool lookup_session_theme(const std::string& name, const std::string& ini, SessionTheme& out);
// a user preset (themes/NAME.theme) of that name exists: never "unknown" in messages
bool is_preset_name(const std::string& name);
// One field of an _LH_THEME_RESOLVED value ("name=...;base=...;...", '\\' escapes; x11/SETTINGS.md)
std::string resolved_field(const std::string& resolved, const std::string& key);

// The theme in force for this process (set by setup_theme at start-up and by
// the shim on a switch); name "" when flxp does not follow one. Setting it
// also adds the section's `fonts` directories (THEMES.md "Fonts": 5112's
// Segoe UI) to the font path; they stay for the life of the process.
const SessionTheme& session_theme();
void set_session_theme(const SessionTheme& t);

// A per-theme setting: flxp.ini / FLXP_<KEY> when set there, else the session
// theme's flxp_<key>, else the fallback.
std::string theme_setting(const std::string& key, const std::string& fallback = "");

// True if flxp follows the session theme: theme_follow (default 1) and no
// explicit theme for this run (--theme / FLXP_THEME that setup_theme did not
// put there itself).
bool follows_session_theme();

// The visual style to load for an msstyles path: the file itself if our
// uxtheme can read it (XP's format, PACKTHEM_VERSION 3, or Longhorn's compiled
// version 4), else an XP backport of it (<registry dir>/../themes/xp-backport/<name>/<name>.msstyles
// or the theme directories). "" if there is none.
std::string loadable_msstyles(const std::string& path, const std::string& registry);

} // namespace shell
