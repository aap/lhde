// Applications and file associations, freedesktop style: .desktop entries
// (Desktop Entry spec) from $XDG_DATA_HOME/applications and
// $XDG_DATA_DIRS/*/applications, defaults and associations from the
// mimeapps.list files (MIME Applications Associations spec), launching with
// Exec field codes. Programs are started directly (no xdg-open), detached, in
// the file's directory. No win32 here: the explorer's menus, a Properties
// dialog or the tree can all use it.
#pragma once
#include <string>
#include <vector>

namespace shell {

struct App {
    std::string id;              // desktop file id: "leafpad.desktop", "org.gnome.Foo.desktop"
    std::string file;            // the .desktop file
    std::string name, generic_name, comment, icon, exec, path;   // path = Path= (working directory override)
    bool terminal = false;       // Terminal=true: run inside the configured terminal
    bool no_display = false;     // NoDisplay=true: usable for associations, not listed for "Choose Program"
    std::vector<std::string> mime_types;
    std::vector<std::string> categories;   // Categories= (the taskbar's start menu groups by them)
};

void reload_apps();                                     // forget the cached desktop files and mimeapps.list
const std::vector<App>& all_apps();                     // every installed application, by name (NoDisplay ones included)
const App* find_app(const std::string& id);
const App* default_app(const std::string& mime);        // the type's default, else its nearest ancestor's; null if none
std::vector<const App*> apps_for(const std::string& mime);   // default first, then the other associations (incl. ancestors')
bool set_default_app(const std::string& mime, const std::string& app_id, std::string* err = nullptr);   // edits $XDG_CONFIG_HOME/mimeapps.list
std::string mimeapps_path();                            // the file set_default_app writes

// Exec= with the field codes expanded: one argv per process to start
// (%f/%u start one process per file, %F/%U one for all; no code: files appended).
std::vector<std::vector<std::string>> exec_argvs(const App& app, const std::vector<std::string>& files);
bool launch(const App& app, const std::vector<std::string>& files, std::string* err = nullptr);

// Start a program detached (new session, double fork) in cwd; reports exec failures.
bool spawn(const std::vector<std::string>& argv, const std::string& cwd, std::string* err = nullptr);

// The terminal: setting "terminal" (FLXP_TERMINAL or terminal= in flxp.ini), default "urxvt".
// It is a /bin/sh command; %d is replaced by the quoted directory, and it runs with cwd = dir.
std::string terminal_command();
bool open_terminal(const std::string& dir, std::string* err = nullptr);

// A native program the user means to run when opening it (an executable ELF file).
bool is_program(const std::string& path, const std::string& mime);

// Open files the way a double click does: programs run, everything else
// goes to its type's default application; grouped so one app gets all its files.
bool open_files(const std::vector<std::string>& paths, std::string* err = nullptr);

} // namespace shell
