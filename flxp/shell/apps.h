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
// one .desktop file outside the data directories (the Quick Launch folder), id = its file name; false if it is no
// application (Hidden, Type, Exec, Name, TryExec)
bool read_desktop_file(const std::string& path, App& out);
// The files read, most important first (the spec's order): $XDG_CONFIG_HOME, each of $XDG_CONFIG_DIRS, then
// $XDG_DATA_HOME/applications and each of $XDG_DATA_DIRS' applications/, in each $desktop-mimeapps.list for every entry of
// XDG_CURRENT_DESKTOP (lower-cased: lhde-session sets "lhde", so lhde's own lhde-mimeapps.list is read) before
// mimeapps.list, a data directory's deprecated defaults.list after them. A type's default (GLib's order): the first
// installed, not removed [Default Applications] entry of the type, else its first association (Added Associations, then
// MimeType= in data-directory order); else the same for its nearest ancestor, and so on.
const App* default_app(const std::string& mime);        // null if none
// where the default comes from: layer "user" (the user's files), "lhde" (a lhde-mimeapps.list), "desktop" (another
// desktop's file), "system" (the distribution's), "claimant" (no file sets one: the first association; file = its
// desktop file), "" (none); type = the type or ancestor it was found for
struct AppSource { const App* app = nullptr; std::string type, file, layer; };
AppSource default_source(const std::string& mime);
std::vector<const App*> apps_for(const std::string& mime);   // default first, then the other associations (incl. ancestors')
bool set_default_app(const std::string& mime, const std::string& app_id, std::string* err = nullptr);   // edits $XDG_CONFIG_HOME/mimeapps.list
// the type's lines out of the user's file (Default, Added and Removed Associations, as GLib's reset): lhde's or the
// distribution's default again
bool reset_default_app(const std::string& mime, std::string* err = nullptr);
bool user_default_set(const std::string& mime);         // the user's file names a default for exactly this type
std::string mimeapps_path();                            // the file set_default_app writes
std::vector<std::string> mimeapps_files();              // the files read that exist, most important first
std::vector<std::string> current_desktops();            // XDG_CURRENT_DESKTOP's entries, lower-cased

// Exec= with the field codes expanded: one argv per process to start
// (%f/%u start one process per file, %F/%U one for all; no code: files appended).
std::vector<std::vector<std::string>> exec_argvs(const App& app, const std::vector<std::string>& files);
bool launch(const App& app, const std::vector<std::string>& files, std::string* err = nullptr);

// Start a program detached (new session, double fork) in cwd; reports exec failures.
bool spawn(const std::vector<std::string>& argv, const std::string& cwd, std::string* err = nullptr);

// The terminal: settings.ini [commands] terminal, else the first of cmd, urxvt, xterm, x-terminal-emulator on PATH
// (liblhde lhde_terminal_command). It is a /bin/sh command; %d is replaced by the quoted directory, and it runs with
// cwd = dir; a Terminal=true program runs as TERMINAL -e PROGRAM ARGS.
std::string terminal_command();
bool open_terminal(const std::string& dir, std::string* err = nullptr);

// A native program the user means to run when opening it (an executable ELF file).
bool is_program(const std::string& path, const std::string& mime);

// A file's own command (user.lux.run, docs/ANNOTATIONS.md; "" when not set): what a double click runs instead of its
// type's program. run_file runs such a command on the file (details::run_command: /bin/sh -c, %f the quoted path),
// detached, in the file's folder.
std::string run_as(const std::string& path);
bool run_file(const std::string& path, const std::string& command, std::string* err = nullptr);

// Open files the way a double click does: a file with its own command (run_as) runs it, programs run, everything
// else goes to its type's default application; grouped so one app gets all its files.
bool open_files(const std::vector<std::string>& paths, std::string* err = nullptr);

} // namespace shell
