// The picture in place of the Windows flag (docs/THEME-FORMAT.md, Shell.start_icon; docs/SETTINGS.md, [taskbar]
// start_icon): flxp-taskbar's Start button (explorer.exe's IDB_START / IDB_STARTCLASSIC) and the Explorer's brand band
// (shell32's IDB_BRAND_BITMAP_MICRO frame 0). No win32 here; the callers make their bitmaps from the image.
//
// The value: windows (the imported flag, the default), lhde, linux, freebsd (share/icons/flags/NAME-SIZE.png, drawn by
// tools/start_icons.py), auto (linux or freebsd by uname's system name, lhde elsewhere), file:PACK/PATH (icons/PACK/PATH of
// the data directories, as the icon set recipes take it), a path (absolute or ~/: a PNG, ICO, BMP, JPEG), else an icon key
// of the icon sets (computer, notepad, ...). What cannot be loaded draws the flag.
#pragma once
#include <string>

namespace gdi { struct Bitmap; }

namespace shell {

// settings.ini [taskbar] start_icon, else the session theme's Shell.start_icon, else "windows"
std::string start_icon_setting();
// auto -> linux | freebsd | lhde by uname's sysname ($LHDE_UNAME instead when set: the tests); the rest as given, the
// names lower-cased; "" -> windows
std::string start_icon_choice(const std::string& value);
// the source file for a choice at size px: NAME-SIZE.png of the named icons (NAME-32.png when no drawing of that size is
// shipped), a file: or path value's file, an icon key's source in the icon sets; "" for windows and what is not found
std::string start_icon_source(const std::string& choice, int size);
// the image: size x size, straight alpha, the source resampled from its 32 px (or largest) image with gdi::resample and
// centred keeping its aspect ratio; a named icon's own drawing at that size as it is. Null for windows or unreadable.
gdi::Bitmap* start_icon_image(const std::string& choice, int size);

} // namespace shell
