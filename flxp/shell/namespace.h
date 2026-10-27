// The shell namespace: what the explorer browses. Modelled on the NT shell's
// IShellFolder/pidl design, in plain C++: a folder enumerates items, binds to
// child folders, and items carry a stable identity (their path) so views can
// be refreshed, sorted and navigated. No win32 types here; the explorer maps
// icon keys to image-list indices itself.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include <memory>

namespace shell {

enum ItemFlags : uint32_t {
    IsFolder = 1, IsHidden = 2, IsLink = 4, IsReadOnly = 8, IsDrive = 16, IsVirtual = 32,
};

struct Item {
    std::string name;        // display name
    std::string path;        // identity; a filesystem path or a virtual "::name" path
    uint32_t flags = 0;
    uint64_t size = 0;       // bytes, files only
    int64_t mtime = 0;       // unix seconds
    std::string type;        // "File Folder", "Text Document", ...
    std::string icon;        // icon key: "folder", "document", "drive", "computer", ... (see icons.h)
    bool is_folder() const { return flags & IsFolder; }
};

enum class SortBy { Name, Size, Type, Date };

class Folder {
public:
    virtual ~Folder() = default;
    virtual std::string path() const = 0;
    virtual std::string name() const = 0;
    virtual std::string icon() const = 0;
    virtual bool enumerate(std::vector<Item>& out, bool include_hidden) = 0;
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
std::string format_size(uint64_t bytes);     // "1.2 MB" like Explorer's details view ("12 KB")
std::string format_date(int64_t t);          // "10/23/2026 12:45 PM"
std::string type_name(const std::string& filename, bool folder);
std::string icon_key(const std::string& filename, bool folder);

} // namespace shell
