// lhde's data directories for flxp (lhtheme/lhde_data.h, docs/LAYOUT.md "Asset
// policy"): where the art the user imported with lhde-import is found, so that
// flxp.ini needs no paths. The search order is lhde_data's: $LHDE_DATA (else
// ${XDG_DATA_HOME:-~/.local/share}/lhde), the install's share/lhde,
// $XDG_DATA_DIRS/lhde, the source tree's data/. flxp.ini's path keys stay as
// overrides (theme_setting, setting); these are the defaults under them.
//
//   themes.ini            the registry (when no _LH_THEMES_INI is published)
//   styles/NAME.msstyles  visual styles by name; styles/NAME/ (the XP backports)
//   resources/<build>     a build's system32 (longhorn_system32's default: the
//                         session theme's build), its Microsoft.Windows.Client.dll
//   resources/demo        the PDC demo's images (the task band's aurora)
//   fonts/<build>         the session theme's fonts (the registry's fonts key);
//                         font_path's default: the imported Fonts with Tahoma
//   wallpapers/<build>    the Desktop tab's list; wallpapers.ini's root
#pragma once
#include <string>
#include <vector>

namespace shell {

struct SessionTheme;

std::vector<std::string> lhde_dirs();                 // the data directories, in search order
// the first data directory's DIR/REL that exists, "" if none
std::string lhde_find(const std::string& rel);
// the data directory that has REL ("" = not imported)
std::string lhde_where(const std::string& rel);
// a registry value: absolute stays, ~/ is the home, else lhde_find (not found: the user dir's path)
std::string lhde_resolve(const std::string& v);
// every directory DIR/REL/* (sorted by name; the first data directory wins for a name)
std::vector<std::string> lhde_subdirs(const std::string& rel);
// the source registry (themes.ini: $LH_THEMES_INI, else the data directories), "" if none
std::string lhde_registry();

// The Longhorn build a theme belongs to ("4074", "4051", "5112"; "" if none can be told): the
// registry's flxp_longhorn_system32 or fonts (resources/4074, fonts/4074), else the digits in the
// section's name or its style's (slate4051, luna4051, aero5112), else the style's XP backport.
std::string theme_build(const SessionTheme& t);
// longhorn_system32's default: resources/<build> of the session theme (or of FLXP_THEME's
// style when no theme is followed), when imported; "" otherwise
std::string default_longhorn_system32();
// font_path's default: the imported fonts/<build> directories that have tahoma.ttf
std::vector<std::string> default_font_dirs();
// wallpaper_dirs' default: ~/Pictures/Wallpapers, then every wallpapers/<build>
std::string default_wallpaper_dirs();

// flxp --info: every data lookup, which data directory answered or "not imported"
std::string lhde_report();

} // namespace shell
