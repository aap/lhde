// The session theme (docs/THEME-FORMAT.md): the root property _LH_THEME names a
// theme, liblhde resolves it into one flat record (Section.key = value, the
// derived path.* and style.* keys), and luxwm / luxshell publish that record as
// _LH_THEME_RESOLVED. flxp follows it: the record when it is current (its
// theme= is _LH_THEME and its serial= is _LH_SETTINGS_SERIAL), else its own
// lhde_theme_resolve(_LH_THEME). No win32 here; the run-time reload is the
// shim's (wux/w32/theme.cpp).
#pragma once
#include <map>
#include <string>

namespace shell {

struct SessionTheme {
    std::string name;            // the theme's name (the record's theme=; aliases resolved), "" = none
    std::string body;            // the record as text, without serial= (two records compare by it)
    std::map<std::string, std::string> keys;   // the record: "Explorer.navbar", "path.style", "style.kind", ...
    std::map<std::string, std::string> why;    // where each key came from (lhde_theme_why; "published" for a parsed record)
    std::string msstyles;        // the visual style our uxtheme loads (path.style), "" = none or classic
    std::string color, size;     // VisualStyle.color / size (uxtheme falls back to the style's first)
    bool classic = false;        // VisualStyle.style = classic: no visual style at all
    std::string value(const std::string& key) const;     // "" if absent ("Section.key" as in the record)
    std::string title() const;   // Theme.name, else the name
};

// Reads the root properties now (its own short X connection; for start-up,
// before the toolkit opens the display) and resolves the session theme. False
// when there is no _LH_THEME or it names no theme (err: why).
bool read_session_theme(SessionTheme& out, std::string* err = nullptr);
// The same from property values the caller read (the shim, through the toolkit): the published record is
// used when it is current, else NAME is resolved
bool session_theme_from(const std::string& name, const std::string& published, unsigned long serial, SessionTheme& out,
                        std::string* err = nullptr);
// Any theme by name (a theme file, an alias, a bare style; Display Properties' list and sample)
bool resolve_theme(const std::string& name, SessionTheme& out, std::string* err = nullptr);
// The same without the current layer (the theme files only: "the theme as shipped"), or with a layer of the caller's
// instead of the stored one (Display Properties' preview of changes not applied yet; KEY = "Section.key")
bool resolve_theme_files(const std::string& name, SessionTheme& out, std::string* err = nullptr);
bool resolve_theme_with(const std::string& name, const std::map<std::string, std::string>& layer, SessionTheme& out,
                        std::string* err = nullptr);
// A theme's name after aliases and its file ("" for a bare style); false when NAME is no theme
bool lookup_theme(const std::string& name, std::string& canon, std::string* file = nullptr);
// NAME is one of the user's own theme files (~/.config/lhde/themes/NAME.theme): Save As renames, Delete removes it
bool is_user_theme(const std::string& name);
std::string user_theme_path(const std::string& name);    // that file ("" when NAME is no safe file name)

// The theme in force for this process (set by setup_theme at start-up and by
// the shim on a switch); name "" when flxp does not follow one. Setting it
// also adds the theme's fonts directory (path.fonts: 5112's Segoe UI) to the
// font path; it stays for the life of the process. That directory is also the
// one searched first (gdi::set_theme_font_dir), until the next theme replaces it.
const SessionTheme& session_theme();
void set_session_theme(const SessionTheme& t);

// What the theme decides alone, from the session theme's record:
//   theme_icons        Desktop.icons (longhorn4074 -> longhorn while flxp's res/icons has no file of that name), else fallback
//   theme_chrome       Explorer.chrome: auto | xp | longhorn (Explorer.navbar none -> xp; no theme or none given: auto)
//   theme_taskbar      Shell.taskbar: avalon | classic | none, "" = not given
std::string theme_icons(const std::string& fallback = "");
std::string theme_chrome();
std::string theme_taskbar();

// flxp's keys with a theme default under them, from the session theme's record (FLXP_<KEY> in the environment, then
// flxp.ini, win). The mapping:
//   menubar_style      flxp.ini, else document for chrome = longhorn
//   header_engine      flxp.ini, else Explorer.navbar: vision -> lhnav, 4051 | 4074 -> flxp
//   header_preset      flxp.ini, else Explorer.navbar_preset for navbar = vision
// any other key: setting(key, fallback) (FLXP_<KEY>, flxp.ini).
std::string theme_setting(const std::string& key, const std::string& fallback = "");
// The session theme's path.resources when it is a Longhorn build's (resources/4051, resources/4074, ...: that build's
// system32), else "": shell32's Longhorn art, the framelets beside it, slate.msstyles under its Resources
std::string longhorn_resources();
// settings.ini [fonts] smoothing (the user's), else the theme's Fonts.smoothing; "" = neither (off | standard | cleartype)
std::string font_smoothing();

// True if flxp follows the session theme: no explicit visual style for this
// run (--theme / FLXP_THEME that setup_theme did not
// put there itself).
bool follows_session_theme();

} // namespace shell
