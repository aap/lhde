// Bookmarks in the GTK file chooser's format, for interop with GTK apps:
// $XDG_CONFIG_HOME/gtk-3.0/bookmarks (falling back to ~/.gtk-bookmarks for
// reading), one "URI[ label]" per line, paths percent-encoded. Entries that
// are not local folders (sftp://, smb://) are kept as they are.
#pragma once
#include <string>
#include <vector>

namespace shell {

struct Bookmark {
    std::string uri;      // "file:///home/u/src"
    std::string label;    // optional display name
    std::string path() const;           // the local path, "" for remote URIs
    std::string display_name() const;   // label, else the last path component
};

std::string bookmarks_path();
std::vector<Bookmark> load_bookmarks();
bool save_bookmarks(const std::vector<Bookmark>& list);
bool add_bookmark(const std::string& path, const std::string& label = "");   // no duplicates; false on error

} // namespace shell
