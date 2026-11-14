// Default programs by role (docs/SETTINGS.md "Default programs"): XP's Set Program Access and Defaults over the
// freedesktop files. A role (roles.ini: the text editor, the image viewer, the web browser, ...) is a set of file types
// one program opens; the user's choice is settings.ini [programs] ROLE = ID.desktop, and apply_roles writes it as the
// [Default Applications] line of every type of the role into the role file, $XDG_DATA_HOME/applications/lhde-mimeapps.list:
// read after the user's ~/.config/mimeapps.list (a choice for one type wins) and before lhde's and the distribution's
// files in the data directories. tools/lhdemime.py is the same model (lhde-mime roles); both write the same file, byte for
// byte. No win32 here.
#pragma once
#include <map>
#include <string>
#include <vector>
#include "apps.h"

namespace shell {

struct Role {
    std::string id;                          // the section: editor, images, pdf, browser, mail, audio, video, archives, folders
    std::string label, question, current;    // "Text editor", "Choose a default text editor:", "Use my current text editor"
    std::vector<std::string> types, descendants, exclude, prefer, categories;   // roles.ini's keys (roles.ini says what each does)
};

// roles.ini: the user's ~/.config/lhde/applications/roles.ini key by key over the first shipped one (lhde's data
// directories); the shipped file's order, roles only the user's file has after them
std::vector<Role> roles();
std::vector<std::string> roles_ini_files();          // the files read, the user's first
// {role id: its types, sorted}: a type named in types= (a pattern matches the database's types) belongs to the first role
// naming it; a type no role names to the first role whose descendants= reach it; exclude= takes types out of its role
std::map<std::string, std::vector<std::string>> role_types(const std::vector<Role>& rs);
// the role of a type (null: none), from roles() and role_types()
const Role* role_of(const std::string& mime);

// settings.ini [programs] ROLE: a desktop file id, "none" (XP's "Use my current ...": the role sets nothing) or ""
std::string role_setting(const Role& r);
// the program the role sets its types to ("" for none) and why: "your choice", "lhde's default", ...
struct RoleChoice { std::string app, how; bool user = false; };
RoleChoice role_choice(const Role& r);
std::string role_default(const Role& r);             // the first installed program of prefer=, else ""
// the installed programs (not NoDisplay) that claim one of the types and have one of categories= (or are in prefer=):
// prefer='s order first, then by how many of the types they claim, then by name
std::vector<const App*> role_candidates(const Role& r, const std::vector<std::string>& types);

std::string roles_file();                            // $XDG_DATA_HOME/applications/lhde-mimeapps.list
std::string roles_text();                            // what the role file says for the roles as they are
// the role file written from roles_text() (unchanged: not touched); then the association cache reloaded
bool apply_roles(std::string* err = nullptr);
// settings.ini [programs] ROLE = value ("" removes the key: lhde's default), then apply_roles
bool set_role(const std::string& role, const std::string& value, std::string* err = nullptr);

} // namespace shell
