// A theme's desktop background (x11/THEMES.md "Wallpapers", x11/wallpapers.ini).
// The order, first set: settings.ini [desktop] wallpaper = none; [override:NAME] wallpaper/mode;
// the preset's [Desktop]; [desktop] wallpaper/mode; a themes.ini `wallpaper` key; wallpapers.ini [NAME]
// (then its aliases, a preset's base), [default]. Callers handle the user's own layers (none, [desktop]);
// these give the theme's: own_wallpaper = the [override:NAME] / preset layers, theme_wallpaper = the rest.
#pragma once
#include <string>
#include "lhtheme.h"

namespace shell {

struct Wallpaper { std::string file, mode; };   // file absolute (or "~/..."); mode may be ""

// settings.ini [override:NAME] wallpaper/mode (NAME, then its aliases), else the preset's [Desktop]
bool own_wallpaper(const SessionTheme& st, Wallpaper& out);
// _LH_THEME_RESOLVED's wallpaper=/mode= when its name= is the current _LH_THEME, else the themes.ini
// key, else wallpapers.ini. Mode "" = fill. A preset's [Desktop] mode applies to the file as well.
bool theme_wallpaper(const SessionTheme& st, Wallpaper& out);
// wallpapers.ini only (LH_WALLPAPERS_INI, else next to the registry)
bool wallpapers_ini(const SessionTheme& st, Wallpaper& out);

} // namespace shell
