// A theme's desktop background (docs/THEME-FORMAT.md [Desktop]): the resolved record's path.wallpaper
// (Desktop.wallpaper; none = colour only), Desktop.mode and Desktop.color; above it the user's
// settings.ini [desktop] wallpaper = none (luxshell's "Show the wallpaper" off). Set as Desktop
// Background and Display Properties write Desktop.wallpaper / Desktop.mode into the theme's modified copy.
#pragma once
#include <stdint.h>
#include <string>
#include "lhtheme.h"

namespace shell {

struct Wallpaper {
    std::string file;            // absolute; "" = no picture (none, or not found)
    std::string mode;            // fill | fit | center | tile | stretch ("" = fill)
    uint32_t color = 0x3a6ea5;   // 0xRRGGBB under and around the picture
};

// the theme's background; false when it has no picture (out.color is still its colour)
bool theme_wallpaper(const SessionTheme& st, Wallpaper& out);
// "R G B" (the format's), "#rrggbb" or "0xrrggbb" -> 0xRRGGBB; fallback when it is none of them
uint32_t parse_color(const std::string& v, uint32_t fallback = 0x3a6ea5);
// settings.ini [desktop] wallpaper = none: the user hides every theme's picture
bool wallpaper_hidden();
// the Desktop.wallpaper value for a picture: GROUP/STEM when it lies in a data directory's wallpapers/ (and no other
// picture there has that stem), else the absolute path
std::string wallpaper_value(const std::string& file);
// Set as Desktop Background: Desktop.wallpaper (and Desktop.mode when not empty) of the session theme into its modified
// copy (shell::store::set_theme_keys), the user's [desktop] wallpaper = none removed; the theme selected and announced.
bool set_desktop_background(const std::string& file, const std::string& mode, std::string* err = nullptr);

} // namespace shell
