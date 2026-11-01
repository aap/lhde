#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <map>
#include "namespace.h"
#include "details.h"
#include "mime.h"
#include "util.h"

namespace shell {

// ---- helpers ----------------------------------------------------------------

static std::string basename_of(const std::string& p)
{
    if (p == "/") return "/";
    size_t i = p.find_last_of('/');
    return i == std::string::npos ? p : p.substr(i + 1);
}

static std::string extension(const std::string& name)
{
    size_t i = name.find_last_of('.');
    if (i == std::string::npos || i == 0) return "";
    return lower(name.substr(i + 1));
}

struct TypeInfo { const char* ext; const char* type; const char* icon; };
static const TypeInfo types[] = {
    {"txt", "Text Document", "text"}, {"md", "Markdown Document", "text"}, {"log", "Text Document", "text"},
    {"c", "C Source File", "text"}, {"h", "C Header File", "text"}, {"cpp", "C++ Source File", "text"},
    {"cc", "C++ Source File", "text"}, {"py", "Python Script", "text"}, {"sh", "Shell Script", "text"},
    {"html", "HTML Document", "html"}, {"htm", "HTML Document", "html"},
    {"png", "PNG Image", "image"}, {"jpg", "JPEG Image", "image"}, {"jpeg", "JPEG Image", "image"},
    {"gif", "GIF Image", "image"}, {"bmp", "Bitmap Image", "image"}, {"ico", "Icon", "image"}, {"svg", "SVG Image", "image"},
    {"mp3", "MP3 Audio", "audio"}, {"wav", "Wave Sound", "audio"}, {"ogg", "Ogg Audio", "audio"}, {"flac", "FLAC Audio", "audio"},
    {"mp4", "MP4 Video", "video"}, {"mkv", "Matroska Video", "video"}, {"avi", "Video Clip", "video"},
    {"zip", "Compressed (zipped) Folder", "zip"}, {"gz", "GZ Archive", "zip"}, {"xz", "XZ Archive", "zip"},
    {"tar", "TAR Archive", "zip"}, {"7z", "7-Zip Archive", "zip"},
    {"pdf", "PDF Document", "pdf"}, {"exe", "Application", "exe"}, {"dll", "Application Extension", "dll"},
    {"msstyles", "Visual Style", "dll"}, {"iso", "Disc Image", "disc"}, {"ini", "Configuration Settings", "settings"},
    {"o", "Object File", "binary"}, {"a", "Static Library", "binary"}, {"so", "Shared Library", "dll"},
};

std::string type_name(const std::string& filename, bool folder)
{
    if (folder) return "File Folder";
    std::string e = extension(filename);
    for (const TypeInfo& t : types) if (e == t.ext) return t.type;
    // not one of Explorer's own names: the shared-mime-info description ("Rust source code")
    std::string m = mime_type_for_name(filename);
    if (!m.empty()) { std::string c = mime_comment(m); if (!c.empty()) { if (c[0] >= 'a' && c[0] <= 'z') c[0] -= 32; return c; } }
    if (e.empty()) return "File";
    std::string u = e; for (char& c : u) if (c >= 'a' && c <= 'z') c -= 32;
    return u + " File";
}

std::string icon_key(const std::string& filename, bool folder)
{
    if (folder) return "folder";
    std::string e = extension(filename);
    for (const TypeInfo& t : types) if (e == t.ext) return t.icon;
    std::string m = mime_type_for_name(filename);
    if (!m.empty()) { std::string k = mime_icon_key(m); if (!k.empty()) return k; }
    return "document";
}

std::string format_size(uint64_t bytes)
{
    char buf[32];
    // Explorer's details view: KB rounded up, until it gets large
    if (bytes < 1024 * 1024) { snprintf(buf, sizeof buf, "%llu KB", (unsigned long long)((bytes + 1023) / 1024)); return buf; }
    if (bytes < 1024ULL * 1024 * 1024) { snprintf(buf, sizeof buf, "%llu KB", (unsigned long long)((bytes + 1023) / 1024)); return buf; }
    snprintf(buf, sizeof buf, "%.2f GB", bytes / (1024.0 * 1024 * 1024));
    return buf;
}

std::string format_date(int64_t t)
{
    time_t tt = (time_t)t;
    struct tm tm; localtime_r(&tt, &tm);
    char buf[64];
    int h = tm.tm_hour % 12; if (h == 0) h = 12;
    snprintf(buf, sizeof buf, "%d/%d/%d %d:%02d %s", tm.tm_mon + 1, tm.tm_mday, tm.tm_year + 1900, h, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
    return buf;
}

static int natural_compare(const std::string& a, const std::string& b)
{
    // case-insensitive with numeric runs compared by value, like Explorer
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (isdigit((unsigned char)a[i]) && isdigit((unsigned char)b[j])) {
            size_t i2 = i, j2 = j;
            while (i2 < a.size() && isdigit((unsigned char)a[i2])) i2++;
            while (j2 < b.size() && isdigit((unsigned char)b[j2])) j2++;
            long long x = atoll(a.substr(i, i2 - i).c_str()), y = atoll(b.substr(j, j2 - j).c_str());
            if (x != y) return x < y ? -1 : 1;
            i = i2; j = j2;
            continue;
        }
        char x = a[i], y = b[j];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return x < y ? -1 : 1;
        i++; j++;
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

const std::string& Item::stored(const std::string& xattr) const
{
    static const std::string none;
    for (const auto& kv : xattrs) if (kv.first == xattr) return kv.second;
    return none;
}

void sort_items(std::vector<Item>& items, SortBy by, bool ascending)
{
    const details::Property* prop = details::column_property(by);   // a stored property's column
    std::stable_sort(items.begin(), items.end(), [&](const Item& a, const Item& b) {
        if (a.is_folder() != b.is_folder()) return a.is_folder();   // folders first, always
        int c = 0;
        switch (by) {
        case SortBy::Name: c = natural_compare(a.name, b.name); break;
        case SortBy::Size: c = a.size < b.size ? -1 : a.size > b.size ? 1 : natural_compare(a.name, b.name); break;
        case SortBy::Type: c = a.type.compare(b.type); if (!c) c = natural_compare(a.name, b.name); break;
        case SortBy::Date: c = a.mtime < b.mtime ? -1 : a.mtime > b.mtime ? 1 : natural_compare(a.name, b.name); break;
        default:   // a stored property: empty ones first (XP's order for a column without a value), then by name
            if (prop) c = natural_compare(a.stored(prop->xattr), b.stored(prop->xattr));
            if (!c) c = natural_compare(a.name, b.name);
            break;
        }
        return ascending ? c < 0 : c > 0;
    });
}

// ---- filesystem folders -------------------------------------------------------

class FsFolder : public Folder {
public:
    explicit FsFolder(const std::string& p) : path_(p) {}
    std::string path() const override { return path_; }
    std::string name() const override
    {
        const char* home = getenv("HOME");
        if (home && path_ == home) return "Home";
        if (path_ == "/") return "Local Disk (/)";   // as My Computer lists it (breadcrumb, title, window name)
        return basename_of(path_);
    }
    std::string icon() const override
    {
        const char* home = getenv("HOME");
        if (path_ == "/") return "drive";
        if (home && path_ == home) return "home";
        return "folder";
    }
    bool is_filesystem() const override { return true; }

    bool enumerate(std::vector<Item>& out, bool include_hidden) override
    {
        DIR* d = opendir(path_.c_str());
        if (!d) return false;
        struct dirent* e;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            bool hidden = e->d_name[0] == '.';
            if (hidden && !include_hidden) continue;
            if (strstr(e->d_name, ".flxp-part")) continue;   // a file being copied (fileops): it shows when it is complete
            Item it;
            it.name = e->d_name;
            it.path = path_ == "/" ? "/" + it.name : path_ + "/" + it.name;
            struct stat st;
            bool is_link = lstat(it.path.c_str(), &st) == 0 && S_ISLNK(st.st_mode);
            if (stat(it.path.c_str(), &st) != 0) { if (lstat(it.path.c_str(), &st) != 0) continue; }
            if (S_ISDIR(st.st_mode)) it.flags |= IsFolder;
            if (hidden) it.flags |= IsHidden;
            if (is_link) it.flags |= IsLink;
            if (access(it.path.c_str(), W_OK) != 0) it.flags |= IsReadOnly;
            it.size = (uint64_t)st.st_size;
            it.mtime = (int64_t)st.st_mtime;
            it.type = type_name(it.name, it.is_folder());
            it.icon = icon_key(it.name, it.is_folder());
            out.push_back(std::move(it));
        }
        closedir(d);
        return true;
    }

    std::shared_ptr<Folder> bind(const Item& child) override
    {
        if (!child.is_folder()) return nullptr;
        return std::make_shared<FsFolder>(child.path);
    }

    std::shared_ptr<Folder> parent() override;

private:
    std::string path_;
};

// ---- virtual folders: Desktop (root) and My Computer ------------------------------

class VirtualFolder : public Folder {
public:
    VirtualFolder(const std::string& path, const std::string& name, const std::string& icon)
        : path_(path), name_(name), icon_(icon) {}
    std::string path() const override { return path_; }
    std::string name() const override { return name_; }
    std::string icon() const override { return icon_; }
    void add(const std::string& name, const std::string& path, const std::string& icon, uint32_t flags = IsFolder | IsVirtual)
    {
        Item it; it.name = name; it.path = path; it.icon = icon; it.flags = flags; it.type = "File Folder";
        items_.push_back(it);
    }
    bool enumerate(std::vector<Item>& out, bool) override { out.insert(out.end(), items_.begin(), items_.end()); return true; }
    std::shared_ptr<Folder> bind(const Item& child) override { return folder_for(child.path); }
    std::shared_ptr<Folder> parent() override { return path_ == "::desktop" ? nullptr : desktop(); }
protected:
    std::string path_, name_, icon_;
    std::vector<Item> items_;
};

static std::shared_ptr<Folder> make_my_computer()
{
    auto f = std::make_shared<VirtualFolder>("::computer", "My Computer", "computer");
    f->add("Local Disk (/)", "/", "drive", IsFolder | IsDrive);
    // mounted filesystems that look like user volumes
    FILE* m = fopen("/proc/mounts", "r");
    if (m) {
        char line[1024];
        while (fgets(line, sizeof line, m)) {
            char dev[256], mnt[512], fs[64];
            if (sscanf(line, "%255s %511s %63s", dev, mnt, fs) != 3) continue;
            std::string mp = mnt;
            if (mp == "/" || strncmp(dev, "/dev/", 5) != 0) continue;
            if (mp.rfind("/boot", 0) == 0 || mp.rfind("/snap", 0) == 0) continue;
            f->add(basename_of(mp) + " (" + mp + ")", mp, "drive", IsFolder | IsDrive);
        }
        fclose(m);
    }
    return f;
}

static std::shared_ptr<Folder> make_desktop()
{
    auto f = std::make_shared<VirtualFolder>("::desktop", "Desktop", "desktop");
    f->add("My Computer", "::computer", "computer");
    const char* home = getenv("HOME");
    if (home) {
        f->add("Home", home, "home");
        std::string d = std::string(home) + "/Desktop";
        struct stat st;
        if (stat(d.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) f->add("Desktop", d, "desktop");
    }
    return f;
}

std::shared_ptr<Folder> desktop()
{
    static std::shared_ptr<Folder> d;
    if (!d) d = make_desktop();
    return d;
}

std::shared_ptr<Folder> home()
{
    const char* h = getenv("HOME");
    return folder_for(h ? h : "/");
}

std::shared_ptr<Folder> FsFolder::parent()
{
    const char* home = getenv("HOME");
    if (path_ == "/") return folder_for("::computer");
    if (home && path_ == home) return desktop();
    size_t i = path_.find_last_of('/');
    if (i == std::string::npos) return nullptr;
    return std::make_shared<FsFolder>(i == 0 ? "/" : path_.substr(0, i));
}

std::shared_ptr<Folder> folder_for(const std::string& path)
{
    if (path == "::desktop" || path.empty()) return desktop();
    if (path == "::computer") { static std::shared_ptr<Folder> c; if (!c) c = make_my_computer(); return c; }
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return nullptr;
    std::string p = path;
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return std::make_shared<FsFolder>(p);
}

} // namespace shell
