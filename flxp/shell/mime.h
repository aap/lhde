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
std::string mime_comment(const std::string& type);               // "PNG image"; "" if unknown
std::string mime_icon_key(const std::string& type);              // our icon key ("image", "text", ...); "" if no mapping

} // namespace shell
