// MIME types from the freedesktop shared-mime-info database: globs2 (all
// data dirs, user first), aliases, subclasses, generic icons and the type
// descriptions in <type>.xml. Content sniffing is a short built-in list of
// common signatures (not the full magic file) plus a text/binary check, used
// when no glob matches. No win32 here.
#pragma once
#include <string>
#include <vector>

namespace shell {

// The type of a file: "inode/directory" for folders, globs by name, else a
// sniff of the first bytes (sniff=false: name only, no I/O beyond stat).
std::string mime_type(const std::string& path, bool sniff = true);
std::string mime_type_for_name(const std::string& filename);     // globs only; "" if none
std::string mime_canonical(const std::string& type);             // resolves aliases
std::vector<std::string> mime_parents(const std::string& type);  // all ancestors, nearest first (text/* -> text/plain, -> application/octet-stream)
bool mime_is_a(const std::string& type, const std::string& ancestor);
// a type's own parents: subclasses' lines for it, text/plain for a text/* type, application/octet-stream for a type with
// neither (not inode/*); canonical
std::vector<std::string> mime_direct_parents(const std::string& type);
// every type the database knows: its types files, the globs' types, both sides of subclasses, the aliases' targets;
// canonical, sorted
std::vector<std::string> mime_all_types();
std::string mime_comment(const std::string& type);               // "PNG image"; "" if unknown
std::string mime_icon_key(const std::string& type);              // our icon key ("image", "text", ...); "" if no mapping

// The registered extensions (Folder Options' File Types): every "*.EXT" glob whose EXT has no wildcard ("txt", "tar.gz"),
// lower-cased, with the type its best glob names (highest weight, then the most important directory), sorted by EXT
struct Extension { std::string ext, type; int weight; };
std::vector<Extension> mime_extensions();
void reload_mime();                                              // forget the database (after update-mime-database)
// The user's own extensions (Folder Options' New): $XDG_DATA_HOME/mime/packages/lhde-extensions.xml, one
// <mime-type><glob pattern="*.EXT"/></mime-type> per extension, compiled into $XDG_DATA_HOME/mime by update-mime-database
std::string user_extensions_file();
std::vector<Extension> user_extensions();
bool add_user_extension(const std::string& ext, const std::string& type, std::string* err = nullptr);
bool remove_user_extension(const std::string& ext, std::string* err = nullptr);

} // namespace shell
