// The shell namespace: what the explorer browses. Modelled on the NT shell's
// IShellFolder/pidl design, in plain C++: a folder enumerates items, binds to
// child folders, and items carry a stable identity (their path) so views can
// be refreshed, sorted and navigated. No win32 types here; the explorer maps
// icon keys to image-list indices itself.
#pragma once
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>
#include <memory>

namespace shell {

enum ItemFlags : uint32_t {
    IsFolder = 1, IsHidden = 2, IsLink = 4, IsReadOnly = 8, IsDrive = 16, IsVirtual = 32,
    // An entry of a slow folder (Folder::slow: a network mount), listed from readdir alone: the UI thread reads nothing
    // of it itself. IsPending while its stat has not come in from the worker (shell/lister.h; size and mtime 0, IsFolder
    // from the entry's d_type, type and icon from its name); IsStored once its stored properties (xattrs) were read,
    // whether any is set or not.
    IsSlow = 64, IsPending = 128, IsStored = 256,
    // A catalogue's item (luxfind): an edit of its annotations is in the catalogue and not on the file yet (HasEdits), or
    // the file changed under one (HasConflict); docs/CATALOGUE.md "Attributes: observed, desired, pending"
    HasEdits = 512, HasConflict = 1024,
};

struct Item {
    std::string name;        // display name
    std::string path;        // identity; a filesystem path or a virtual "::name" path
    uint32_t flags = 0;
    uint64_t size = 0;       // bytes, files only
    int64_t mtime = 0;       // unix seconds
    std::string type;        // "File Folder", "Text Document", ...
    std::string icon;        // icon key: "folder", "document", "drive", "computer", ... (see icons.h)
    // the stored properties of shell/details.h that are set (attribute name, value);
    // read only when a view needs them (its Description/Tags/Comment columns or that sort)
    std::vector<std::pair<std::string, std::string>> xattrs;
    bool is_folder() const { return flags & IsFolder; }
    const std::string& stored(const std::string& xattr) const;   // "" when not set (or not read)
};

// What a list sorts by and what its Details columns are: the first four are
// derived from the item, the next four are stored properties (shell/details.h
// column_property); Folder and Pending are a search's (luxfind: XP's Search Results
// "In Folder", and whether an edit waits in the catalogue), never a folder's
enum class SortBy { Name, Size, Type, Date, Description, Tags, Comment, Color, Folder, Pending };

class Folder {
public:
    virtual ~Folder() = default;
    virtual std::string path() const = 0;
    virtual std::string name() const = 0;
    virtual std::string icon() const = 0;
    virtual bool enumerate(std::vector<Item>& out, bool include_hidden) = 0;
    // A slow folder's entries from readdir alone (IsSlow | IsPending, see ItemFlags): no round trip per entry, so a view
    // shows them at once and shell::Lister fills them in. Any other folder: enumerate.
    virtual bool enumerate_names(std::vector<Item>& out, bool include_hidden) { return enumerate(out, include_hidden); }
    // On a network file system (lhde_fs_is_remote, asked once per folder object)
    virtual bool slow() const { return false; }
    virtual std::shared_ptr<Folder> bind(const Item& child) = 0;     // child must be a folder
    virtual std::shared_ptr<Folder> parent() = 0;                     // null at the root
    virtual bool is_filesystem() const { return false; }
    virtual bool has_subfolders_hint() const { return true; }        // for tree "+" buttons
};

// Resolve any path (filesystem or virtual) to a folder; null if it isn't one.
std::shared_ptr<Folder> folder_for(const std::string& path);
std::shared_ptr<Folder> desktop();           // the root of the namespace
std::shared_ptr<Folder> home();

void sort_items(std::vector<Item>& items, SortBy by, bool ascending);
// A filesystem entry's stat into it: IsFolder, IsLink, IsReadOnly, size and mtime (a symlink followed, a dangling one as
// itself), IsPending cleared; type and icon stay (the caller's, on the UI thread: the MIME database has no lock). False
// when the entry is gone. Any thread. The read-only flag comes from the mode bits for the user and its groups, as
// access(2) answers without ACLs (which would be one more round trip).
bool stat_entry(Item& it);
std::string format_size(uint64_t bytes);     // "1.2 MB" like Explorer's details view ("12 KB")
std::string format_date(int64_t t);          // "10/23/2026 12:45 PM"
std::string type_name(const std::string& filename, bool folder);
std::string icon_key(const std::string& filename, bool folder);

} // namespace shell
