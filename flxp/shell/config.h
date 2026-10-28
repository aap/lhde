// XDG base directories, flxp's own settings file and the small string helpers
// (URIs, shell quoting) the shell model shares. No win32 here.
#pragma once
#include <string>
#include <vector>

namespace shell {

std::string home_dir();                       // $HOME, else the passwd entry
std::string config_home();                    // $XDG_CONFIG_HOME or ~/.config
std::string data_home();                      // $XDG_DATA_HOME or ~/.local/share
std::vector<std::string> config_dirs();       // $XDG_CONFIG_DIRS or /etc/xdg
std::vector<std::string> data_dirs();         // $XDG_DATA_DIRS or /usr/local/share:/usr/share

// flxp settings: $XDG_CONFIG_HOME/flxp/flxp.ini, plain "key=value" lines
// ([sections] and #/; comments ignored). An environment variable
// FLXP_<KEY> (upper case) overrides the file. An empty value (in the file
// or the environment) means unset: the fallback, i.e. automatic. Keys that the
// shared store maps (shell/settings.h: font_smoothing, pinned) fall back to
// ~/.config/lhde/settings.ini before the fallback. The keys that duplicated
// theme keys (icons, frame, taskbar_kind, wallpaper, wallpaper_mode, themes_ini)
// are ignored in flxp.ini (one note on stderr); the theme decides (lhtheme.h).
std::string setting(const std::string& key, const std::string& fallback = "");
std::string file_setting(const std::string& key, const std::string& fallback = "");   // flxp.ini (then settings.ini), no FLXP_<KEY>
std::string settings_path();                  // $XDG_CONFIG_HOME/flxp/flxp.ini
// Sets key = value in flxp.ini, keeping the rest of the file (comments, order,
// the other keys): the first "key =" line is rewritten in place, else the line
// is appended (the file is created with a [flxp] section if missing). False if
// it cannot be written.
bool write_setting(const std::string& key, const std::string& value);

// Run-time data (res/icons, ...) is looked up, relative path appended, in:
// $FLXP_DATA, the compiled-in FLXP_DATADIR ($(PREFIX)/share/lhde),
// <exe>/../share/lhde (a moved install), <exe>/../../flxp and
// <exe>/../../../flxp (an uninstalled build: build/bin, build/tests[/w32]),
// <exe>/../share/flxp (flxp's old data directory). "" if none has it.
std::string data_path(const std::string& rel);
std::vector<std::string> data_search_dirs();
std::string exe_dir();                        // dirname(/proc/self/exe)

std::string uri_from_path(const std::string& path);    // "file:///a%20b"
std::string path_from_uri(const std::string& uri);     // "" if not a file: URI
std::string percent_decode(const std::string& s);
std::string shell_quote(const std::string& s);         // 'it'\''s'
std::string dir_of(const std::string& path);           // "/a/b" -> "/a", "/a" -> "/"
std::string base_of(const std::string& path);          // "/a/b" -> "b"
bool read_file(const std::string& path, std::string& out);
bool write_file_atomic(const std::string& path, const std::string& data);   // tmp + rename, mkdir -p the parent
bool make_dirs(const std::string& dir);

} // namespace shell
