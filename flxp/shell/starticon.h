// The picture in place of the Windows flag (docs/THEME-FORMAT.md, Shell.start_icon; docs/SETTINGS.md, [taskbar]
// start_icon): flxp-taskbar's Start button (explorer.exe's IDB_START / IDB_STARTCLASSIC) and the Explorer's brand band
// (shell32's IDB_BRAND_BITMAP_MICRO frame 0). No win32 here; the callers make their bitmaps from the image.
//
// The value: windows (the imported flag, the default), windows:BUILD (that build's own flag: resources/BUILD/explorer.exe's
// IDB_START 143 and IDB_STARTCLASSIC 176, the brand band's frame from its micro brand; 4074, whose explorer.exe has none: the
// white flag of jade4074's CompositedWindow.DEFAULTICON; a build not imported, or without a flag, is windows), lhde, linux, freebsd (share/icons/flags/NAME-SIZE.png, drawn by
// tools/start_icons.py), auto (linux or freebsd by uname's system name, lhde elsewhere), file:PACK/PATH (icons/PACK/PATH of
// the data directories, as the icon set recipes take it), a path (absolute or ~/: a PNG, ICO, BMP, JPEG), else an icon key
// of the icon sets (computer, notepad, ...). What cannot be loaded draws the flag.
#pragma once
#include <string>
#include <vector>

namespace gdi { struct Bitmap; }

namespace shell {

// settings.ini [taskbar] start_icon, else the session theme's Shell.start_icon, else "windows"
std::string start_icon_setting();
// auto -> linux | freebsd | lhde by uname's sysname ($LHDE_UNAME instead when set: the tests); the rest as given, the
// names lower-cased; "" -> windows
std::string start_icon_choice(const std::string& value);
// the source file for a choice at size px: NAME-SIZE.png of the named icons (NAME-32.png when no drawing of that size is
// shipped), a file: or path value's file, an icon key's source in the icon sets, windows:BUILD's "pe:EXPLORER#ID" or
// "style:MSSTYLES#CompositedWindow.DEFAULTICON"; "" for windows and what is not found
std::string start_icon_source(const std::string& choice, int size);
// the image: size x size, straight alpha, the source resampled from its 32 px (or largest) image with gdi::resample and
// centred keeping its aspect ratio; a named icon's own drawing at that size as it is. Null for windows or unreadable.
gdi::Bitmap* start_icon_image(const std::string& choice, int size);

// windows:BUILD. The build of a choice ("2k" for windows:2k, "" for anything else)
std::string start_flag_build(const std::string& choice);
// resources/BUILD/explorer.exe is imported and has IDB_START 143 (reactos never: its 143 is ReactOS's logo), or the build's
// style flag is imported (4074: styles/jade4074.msstyles with CompositedWindow.DEFAULTICON)
bool start_flag_available(const std::string& build);
// the imported builds that have a flag: 2k and xp first, then the rest by name
std::vector<std::string> start_flag_builds();
// "Windows 2000 flag", "Windows XP flag", "Longhorn 4074 flag", ...
std::string start_flag_label(const std::string& build);
// the build's flag in the Start button's cell, straight alpha: under a visual style 25x20 (143 as it is when it has that
// size, a smaller flag at (2, 2), where XP's 20 px flag would be centred); classic 16x16 (176; without it a 16 px 143, else
// 143's left 20 columns resampled to 16). A bitmap without alpha is keyed: 192 192 192 and magenta are transparent. A style
// flag (4074's 20 x 17) is centred in the styled cell's left 20 x 20 as it is, shrunk to fit the classic 16 x 16. Null when
// the build has no flag.
gdi::Bitmap* start_flag_image(const std::string& build, bool styled);
// frame 0 of the build's micro brand (XP: shell32's IDB_BRAND_BITMAP_MICRO 240; 2000: browseui's 306), size x size
// (resampled when the frame has another size); null when the build has none
gdi::Bitmap* start_flag_brand(const std::string& build, int size);

// The Shell tab's Start icon list as { label, value }: the theme's flag (windows), each imported build's flag
// (windows:BUILD), lhde, linux, freebsd, auto
std::vector<std::pair<std::string, std::string>> start_icon_items();

} // namespace shell
