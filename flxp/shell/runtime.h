// Start-up plumbing shared by flxp's programs: the settings template written
// on first run, the fonts (font_path or auto-detection through fontconfig)
// and the visual style (theme name -> .msstyles). No win32 here; the theme
// choice is handed to the shim through FLXP_THEME before w32::init.
#pragma once
#include <string>
#include <vector>

namespace shell {

// Writes a commented flxp.ini listing every key (empty = automatic) if there
// is none yet. True if it wrote one.
bool write_settings_template();

// Fonts: the font_path directories are searched before the system's. With no
// font_path, the Windows families flxp uses (Tahoma, Microsoft Sans Serif,
// Marlett) that fontconfig does not know are looked for in Wine's font
// directories; whatever is still missing falls back to fontconfig's choice.
void setup_fonts();
struct FontInfo { std::string family, file; bool exact; };
std::vector<FontInfo> font_report();       // what each wanted family resolves to

// Visual style: theme=, theme_color=, theme_size=, theme_path= (default
// "luna" when it can be found, else classic). Exports the result as
// FLXP_THEME[_COLOR,_SIZE] for the shim's init (and child processes).
// Returns the .msstyles path ("" = classic).
std::string setup_theme();
std::string find_msstyles(const std::string& name);   // "" if not found
std::vector<std::string> theme_dirs();

} // namespace shell
