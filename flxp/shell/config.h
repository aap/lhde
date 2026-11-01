// XDG base directories, flxp's own settings file and the small string helpers
// (URIs, shell quoting) the shell model shares. No win32 here.
#pragma once
#include <string>
#include <vector>
#include "util.h"   // trim, lower, iequal, exists, is_dir, is_file, read_file

namespace shell {

std::string home_dir();                       // $HOME, else the passwd entry
std::string config_home();                    // $XDG_CONFIG_HOME or ~/.config
std::string state_home();                     // $XDG_STATE_HOME or ~/.local/state
std::string state_file(const char* name);     // $XDG_STATE_HOME/lhde/NAME: what flxp remembers on its own (views, icon positions)
std::string data_home();                      // $XDG_DATA_HOME or ~/.local/share
std::vector<std::string> config_dirs();       // $XDG_CONFIG_DIRS or /etc/xdg
std::vector<std::string> data_dirs();         // $XDG_DATA_DIRS or /usr/local/share:/usr/share
// An XDG user directory (xdg-user-dirs: $XDG_<NAME>_DIR, else XDG_<NAME>_DIR in $XDG_CONFIG_HOME/user-dirs.dirs, $HOME
// expanded; NAME is "PICTURES", "DOCUMENTS", "DESKTOP", ...), else $HOME/fallback ("Pictures"); not created
std::string user_dir(const std::string& name, const std::string& fallback);

// flxp settings: $XDG_CONFIG_HOME/lhde/flxp.ini (docs/SETTINGS.md "Files"), plain "key=value" lines
// ([sections] and #/; comments ignored). An environment variable
// FLXP_<KEY> (upper case) overrides the file. An empty value (in the file
// or the environment) means unset: the fallback, i.e. automatic. flxp.ini holds
// flxp's own preferences only: what the desktop shares is settings.ini's
// (shell::store, settings.h), what the theme decides the session theme's (lhtheme.h).
std::string setting(const std::string& key, const std::string& fallback = "");
// A switch: 1 or 0, nothing else (an empty or other value is automatic: the fallback; another value is
// reported once on stderr). setting_flag_auto: 1, 0, or -1 for automatic. flag_value parses one value
// (WHERE names it in the report): the one boolean parser of flxp, settings.ini's too (store::flag).
bool setting_flag(const std::string& key, bool fallback);
int setting_flag_auto(const std::string& key);
int flag_value(const std::string& v, const std::string& where);
std::string file_setting(const std::string& key, const std::string& fallback = "");   // flxp.ini only, no FLXP_<KEY>
std::string settings_path();                  // $XDG_CONFIG_HOME/lhde/flxp.ini
// Sets key = value in flxp.ini, keeping the rest of the file (comments, order,
// the other keys): the first "key =" line is rewritten in place, else the line
// is appended (the file is created with a [flxp] section if missing). False if
// it cannot be written.
bool write_setting(const std::string& key, const std::string& value);

// flxp's program data (res/icons) is looked up, relative path appended, in:
// the compiled-in FLXP_DATADIR ($(PREFIX)/share/lhde), <exe>/../share/lhde
// (a moved install), <exe>/../../flxp and <exe>/../../../flxp (an uninstalled
// build: build/bin, build/tests[/w32]). "" if none has it. Everything else
// (styles, resources, wallpapers, themes) comes through liblhde (lhdata.h).
std::string data_path(const std::string& rel);
std::vector<std::string> data_search_dirs();
std::string exe_dir();                        // dirname(/proc/self/exe)

std::string uri_from_path(const std::string& path);    // "file:///a%20b"
std::string path_from_uri(const std::string& uri);     // "" if not a file: URI
std::string percent_decode(const std::string& s);
std::string shell_quote(const std::string& s);         // 'it'\''s'
std::string dir_of(const std::string& path);           // "/a/b" -> "/a", "/a" -> "/"
std::string base_of(const std::string& path);          // "/a/b" -> "b"
bool write_file_atomic(const std::string& path, const std::string& data);   // tmp + rename, mkdir -p the parent
bool make_dirs(const std::string& dir);

} // namespace shell
