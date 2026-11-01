// lhde's data directories for flxp (liblhde's lhde/data.h, docs/LAYOUT.md "Asset
// policy"): where the art the user imported with lhde-import is found, so that
// flxp.ini has no paths. The search order is lhde_data's: $LHDE_DATA (else
// ${XDG_DATA_HOME:-~/.local/share}/lhde), the install's share/lhde,
// $XDG_DATA_DIRS/lhde, the source tree's share/.
//
//   styles/NAME.msstyles  visual styles by name (--theme / FLXP_THEME)
//   resources/<build>     a build's system32 (the session theme's path.resources:
//                         shell::longhorn_resources), its Microsoft.Windows.Client.dll
//   resources/demo        the PDC demo's images (the task band's aurora)
//   fonts/<build>         the session theme's fonts (path.fonts); the imported
//                         Fonts with Tahoma, always on the font path
//   wallpapers/<build>    the Desktop tab's list
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
// a value naming a data file: absolute stays, ~/ is the home, else lhde_find (not found: the user dir's path)
std::string lhde_resolve(const std::string& v);
// every directory DIR/REL/* (sorted by name; the first data directory wins for a name)
std::vector<std::string> lhde_subdirs(const std::string& rel);

// The Longhorn build a theme belongs to ("4074", "4051", "5112"; "" if none can be told): its
// Theme.build, else path.resources' build (resources/4074), else the digits in the theme's name,
// its style's or its fonts' (slate4051, luna4051, fonts/5112).
std::string theme_build(const SessionTheme& t);
// resources/<build> of the session theme's build (or of FLXP_THEME's style when no theme is followed), when imported;
// "" otherwise
std::string build_resources();
// the imported fonts/<build> directories that have tahoma.ttf (flxp's font path)
std::vector<std::string> default_font_dirs();
// wallpaper_dirs' default: ~/Pictures/Wallpapers, then every wallpapers/<build>
std::string default_wallpaper_dirs();

// flxp --info: every data lookup, which data directory answered or "not imported"
std::string lhde_report();

} // namespace shell
