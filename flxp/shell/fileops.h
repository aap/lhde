// File operations the explorer offers: move to the freedesktop Trash, rename,
// new folder / new empty file with Explorer's "New Folder (2)" naming, and a
// simple paste (move or copy of plain files and directory trees). No win32.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace shell {

// Freedesktop Trash spec 1.0: $XDG_DATA_HOME/Trash/{files,info} for the home
// filesystem, else $topdir/.Trash-$uid on the file's own filesystem.
bool trash(const std::string& path, std::string* err = nullptr);
std::string trash_dir_for(const std::string& path);   // which Trash a path would go to

std::string unique_name(const std::string& dir, const std::string& base, const std::string& ext);   // "New Folder", "New Folder (2)", ...
bool new_folder(const std::string& dir, std::string* path, std::string* err = nullptr);
bool new_text_file(const std::string& dir, std::string* path, std::string* err = nullptr);

// Paste: cut moves (rename, or copy + delete across filesystems), copy copies;
// names that exist get Explorer's "Copy of x" naming. A wrapper over
// fileops::run (fileops_job.h: replace confirmation, progress, worker Job);
// err gets the summary of every failed item.
bool paste_items(const std::vector<std::string>& sources, const std::string& dest_dir, bool move, std::string* err = nullptr);
// A program with a user interface sets one for paste_items (and so drop_items):
// the explorer's (explorer/fileopui.h) asks XP's questions, shows the progress
// dialog and the errors as they happen; err then gets only what it did not show
// ("" when the user cancelled or saw every error).
typedef bool (*PasteUI)(const std::vector<std::string>& sources, const std::string& dest_dir, bool move, std::string* err);
void set_paste_ui(PasteUI ui);

// Drag and drop (the effects are win32's DROPEFFECT_* values). drop_effect is
// what a drop of these files into dest_dir does, as XP decides it: Ctrl or
// Shift copies (Shift: the user's choice; XP's Shift moves), Ctrl+Shift links,
// plain moves on the same filesystem and copies across; 0 when it cannot
// (into itself or below, a move to where they are, an unwritable folder), and
// only effects in allowed. drop_items does it: paste_items for copy/move,
// link_items (symbolic links, "Shortcut to x" when the name is taken) for links.
enum : uint32_t { EffectCopy = 1, EffectMove = 2, EffectLink = 4 };
bool same_filesystem(const std::string& a, const std::string& b);
uint32_t drop_effect(const std::vector<std::string>& paths, const std::string& dest_dir, bool shift, bool ctrl, uint32_t allowed);
bool link_items(const std::vector<std::string>& sources, const std::string& dest_dir, std::string* err = nullptr);
bool drop_items(const std::vector<std::string>& sources, const std::string& dest_dir, uint32_t effect, std::string* err = nullptr);

struct FileInfo {
    bool ok = false, is_dir = false, is_link = false;
    uint64_t size = 0;           // bytes; for folders the sum of the tree (bounded)
    uint64_t files = 0, folders = 0;
    int64_t mtime = 0, atime = 0, ctime = 0;
    std::string perms;           // "drwxr-xr-x"
    std::string owner, group, link_target;
};
FileInfo file_info(const std::string& path, bool deep);   // deep: walk folders for their size

} // namespace shell
