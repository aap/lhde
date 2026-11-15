// The details model (details.h). The rules are XP's (shell32's details and
// properties, shlwapi's StrFormatByteSizeW and SHFormatDateTimeW) and 4051's
// managed preview pane.
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <algorithm>
#include <map>
#include <mutex>
#include "liblhde/compat.h"
#include "lhcat.h"
#include "config.h"
#include "details.h"
#include "thumbs.h"

namespace shell {
namespace details {

static const char* const MONTHS[] = { "January", "February", "March", "April", "May", "June", "July",
                                      "August", "September", "October", "November", "December" };
static const char* const DAYS[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

std::string byte_size(uint64_t n)
{
    char buf[48];
    if (n < 1024) { snprintf(buf, sizeof buf, "%llu bytes", (unsigned long long)n); return buf; }
    static const char* const ORDERS[] = { "KB", "MB", "GB", "TB", "PB", "EB" };
    int order = 0;
    while (order < 5 && n >= 1000ULL * 1024) { n >>= 10; order++; }
    unsigned whole = (unsigned)(n >> 10);
    int digits = whole >= 100 ? 3 : whole >= 10 ? 2 : 1;
    if (digits == 3) { snprintf(buf, sizeof buf, "%u %s", whole, ORDERS[order]); return buf; }
    // the fraction's first one or two digits, cut, not rounded
    unsigned frac = (unsigned)((n - ((uint64_t)whole << 10)) * 1000 / 1024) / 10;
    if (digits == 2) frac /= 10;
    snprintf(buf, sizeof buf, "%u.%0*u %s", whole, 3 - digits, frac, ORDERS[order]);
    return buf;
}

static struct tm local(int64_t t)
{
    time_t tt = (time_t)t;
    struct tm tm;
    localtime_r(&tt, &tm);
    return tm;
}

static std::string time_of_day(const struct tm& tm)
{
    char buf[32];
    int h = tm.tm_hour % 12;
    snprintf(buf, sizeof buf, "%d:%02d %s", h ? h : 12, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
    return buf;
}

std::string friendly_date(int64_t t, int64_t now)
{
    struct tm tm = local(t), today = local(now), yesterday = local(now - 86400);
    auto same = [&](const struct tm& a) { return a.tm_year == tm.tm_year && a.tm_mon == tm.tm_mon && a.tm_mday == tm.tm_mday; };
    // the US long date ("dddd, MMMM dd, yyyy"), its weekday replaced by Today or Yesterday
    const char* day = same(today) ? "Today" : same(yesterday) ? "Yesterday" : DAYS[tm.tm_wday];
    char buf[96];
    snprintf(buf, sizeof buf, "%s, %s %02d, %d, ", day, MONTHS[tm.tm_mon], tm.tm_mday, tm.tm_year + 1900);
    return buf + time_of_day(tm);
}

std::string thousands(uint64_t n)
{
    std::string s = std::to_string(n);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert((size_t)i, ",");
    return s;
}

std::string size_and_bytes(uint64_t bytes)
{
    return byte_size(bytes) + " (" + thousands(bytes) + " bytes)";
}

std::string long_date(int64_t t)
{
    struct tm tm = local(t);
    char buf[96];
    int h = tm.tm_hour % 12;
    snprintf(buf, sizeof buf, "%s, %s %02d, %d, %d:%02d:%02d %s", DAYS[tm.tm_wday], MONTHS[tm.tm_mon], tm.tm_mday, tm.tm_year + 1900,
             h ? h : 12, tm.tm_min, tm.tm_sec, tm.tm_hour < 12 ? "AM" : "PM");
    return buf;
}

std::string short_date(int64_t t)
{
    struct tm tm = local(t);
    char buf[32];
    snprintf(buf, sizeof buf, "%d/%d/%d", tm.tm_mon + 1, tm.tm_mday, tm.tm_year + 1900);
    return buf;
}

std::string attributes(const Item& it)
{
    std::string out;
    auto add = [&](const char* a) { out += (out.empty() ? "" : ", "); out += a; };
    if ((it.flags & IsReadOnly) && !it.is_folder()) add("Read-only");
    if (it.flags & IsHidden) add("Hidden");
    return out;
}

std::string items_selected_xp(size_t n) { return std::to_string(n) + " items selected."; }

std::string lh_items_word(int n) { return n == 1 ? " item" : " items"; }

static bool virtual_path(const std::string& p) { return p.compare(0, 2, "::") == 0; }

Facts probe(const Item& it)
{
    Facts f;
    if (virtual_path(it.path)) { f.kind = Facts::System; return f; }
    if (it.path.empty() || it.path[0] != '/') return f;
    f.network = (it.flags & IsSlow) || thumbs::on_network(it.path);
    if (it.flags & IsDrive) {
        f.kind = Facts::Drive;
        struct statvfs st;
        if (statvfs(it.path.c_str(), &st) == 0 && st.f_blocks) {
            f.total = (uint64_t)st.f_blocks * st.f_frsize;
            f.free = (uint64_t)st.f_bavail * st.f_frsize;
        }
        // the file system: the last mount on the mount point
        struct At { const std::string* path; std::string* fs; } at = { &it.path, &f.fs };
        lhde_mount_each([](const char*, const char* mnt, const char* fs, void* c) {
            At* a = (At*)c;
            if (*a->path == mnt) *a->fs = fs;
            return 0;
        }, &at);
        return f;
    }
    // a slow folder's entry: what the list read of it (shell/lister.h), no round trip here; no Dimensions
    bool slow = it.flags & IsSlow;
    f.xattrs = slow ? it.xattrs : read_stored(it.path);
    if (it.is_folder()) return f;
    std::string key = icon_key(it.name, false);
    if (key == "audio") f.kind = Facts::Audio;
    if (key == "image") {
        f.kind = Facts::Picture;
        if (!slow) thumbs::picture_size(it.path, &f.width, &f.height);
    }
    f.thumbnail = thumbs::can_thumbnail(it.path);
    if (f.thumbnail && f.kind == Facts::Other && thumbs::own_picture(it.path)) f.kind = Facts::Picture;   // a picture by another name
    return f;
}

Item location_item(const Folder& folder)
{
    Item it;
    it.name = folder.name();
    it.path = folder.path();
    it.icon = folder.icon();
    it.flags = IsFolder;
    if (virtual_path(it.path)) { it.flags |= IsVirtual; it.type = "System Folder"; return it; }
    it.type = type_name(it.name, true);
    struct stat st;
    if (folder.slow()) it.flags |= IsSlow;   // (its date: not asked on the UI thread)
    else if (stat(it.path.c_str(), &st) == 0) it.mtime = st.st_mtime;
    std::string base = it.path.substr(it.path.find_last_of('/') + 1);
    if (!base.empty() && base[0] == '.') it.flags |= IsHidden;
    // a drive of My Computer: its name there ("Local Disk (/)")
    if (auto c = folder_for("::computer")) {
        std::vector<Item> drives;
        c->enumerate(drives, false);
        for (const Item& d : drives)
            if ((d.flags & IsDrive) && d.path == it.path) { it.name = d.name; it.icon = d.icon; it.flags |= IsDrive; }
    }
    return it;
}

// ---- the stored properties on disk ----------------------------------------------------

static std::string base_name(const std::string& path) { return path.substr(path.find_last_of('/') + 1); }

// ---- the catalogue (liblhcat, docs/CATALOGUE.md) -------------------------------------------------------------------------
// A path under a root with a catalogue: the catalogue's handle (opened once per process) and the path relative to its root
static std::mutex g_cat_lock;
static lhcat* catalogue_for(const std::string& path, std::string* rel)
{
    char real[PATH_MAX], name[LHCAT_NAME_MAX + 1];
    const char* r = nullptr;
    if (path.empty() || path[0] != '/' || !realpath(path.c_str(), real)) return nullptr;
    if (lhcat_root_for(real, name, sizeof name, &r) != 0) return nullptr;
    static std::map<std::string, lhcat*> open;
    auto it = open.find(name);
    lhcat* c = it != open.end() ? it->second : (open[name] = lhcat_open(name, nullptr, 0));
    if (c && rel) *rel = r;
    return c;
}

// what put() wrote, into the catalogue that holds the file (an operation in its journal, the file observed again)
static void follow(const std::string& path, const std::string& xattr, const std::string& old, const std::string& value)
{
    std::lock_guard<std::mutex> hold(g_cat_lock);
    std::string rel;
    lhcat* c = catalogue_for(path, &rel);
    if (c && lhcat_record_write(c, rel.c_str(), xattr.c_str(), old.c_str(), value.c_str()) != 0)
        fprintf(stderr, "%s: the catalogue %s did not follow: %s\n", lhde_progname(), lhcat_name(c), lhcat_error(c));
}

// the size first, then a buffer of that size; ERANGE: it grew in between, ask again
Read read_attr(const std::string& path, const std::string& xattr, std::string* value, int* error)
{
    value->clear();
    if (error) *error = 0;
    for (int tries = 0; tries < 8; tries++) {
        ssize_t n = lhde_xattr_get(path.c_str(), xattr.c_str(), nullptr, 0);
        if (n > 0) {
            value->resize((size_t)n);
            n = lhde_xattr_get(path.c_str(), xattr.c_str(), &(*value)[0], value->size());
            if (n < 0 && errno == ERANGE) continue;
        }
        if (n < 0) {
            int e = errno;
            value->clear();
            if (e == LHDE_ENOATTR || e == ENOTSUP || e == ENOENT) return Read::Missing;
            if (error) *error = e;
            return Read::Failed;
        }
        value->resize((size_t)n);
        while (!value->empty() && (value->back() == '\0' || value->back() == '\n')) value->pop_back();   // a C string's NUL, echo's newline
        return value->empty() ? Read::Missing : Read::Value;
    }
    value->clear();
    if (error) *error = ERANGE;
    return Read::Failed;
}

Read list_attrs(const std::string& path, std::vector<std::string>* names, int* error)
{
    names->clear();
    if (error) *error = 0;
    for (int tries = 0; tries < 8; tries++) {
        ssize_t n = lhde_xattr_list(path.c_str(), nullptr, 0);
        std::string buf;
        if (n > 0) {
            buf.resize((size_t)n);
            n = lhde_xattr_list(path.c_str(), &buf[0], buf.size());
            if (n < 0 && errno == ERANGE) continue;   // grew in between
        }
        if (n < 0) {
            int e = errno;
            if (e == ENOTSUP || e == ENOENT) return Read::Missing;
            if (error) *error = e;
            return Read::Failed;
        }
        for (size_t a = 0; a < (size_t)n;) {   // NUL-separated names
            size_t b = buf.find('\0', a);
            if (b == std::string::npos || b > (size_t)n) b = (size_t)n;
            if (buf.compare(a, 5, "user.") == 0) names->push_back(buf.substr(a, b - a));
            a = b + 1;
        }
        return names->empty() ? Read::Missing : Read::Value;
    }
    if (error) *error = ERANGE;
    return Read::Failed;
}

Stored read_stored(const std::string& path)
{
    Stored out;
    if (path.empty() || path[0] != '/') return out;
    for (const Property& p : registry()) {
        if (p.xattr.empty()) continue;
        std::string v;
        if (read_attr(path, p.xattr, &v) == Read::Value) out.push_back({ p.xattr, v });
    }
    return out;
}

bool read_current(const std::string& path, const Property& p, std::string* value, std::string* err)
{
    int e = 0;
    if (read_attr(path, p.xattr, value, &e) != Read::Failed) return true;
    if (err) {
        std::string what = p.label;
        for (char& c : what) c = (char)tolower((unsigned char)c);
        *err = "The " + what + " of '" + base_name(path) + "' could not be read (" + strerror(e) + "), so nothing was changed.";
    }
    return false;
}

void read_stored(Item& it)
{
    if (it.flags & (IsVirtual | IsDrive)) { it.xattrs.clear(); return; }
    it.xattrs = read_stored(it.path);
}

namespace {
struct CatRow { int64_t size, mtime, ctime_ns; int kind, coverage; Stored attrs; };
void cat_row(void* ctx, const struct lhcat_entry* e)
{
    CatRow r { e->size, e->mtime, e->ctime_ns, e->kind, e->coverage, {} };
    for (int i = 0; i < e->nattrs; i++) r.attrs.push_back({ e->attrs[2 * i], e->attrs[2 * i + 1] });
    (*static_cast<std::map<std::string, CatRow>*>(ctx))[e->name] = std::move(r);
}
}

void read_stored(std::vector<Item>& items, const std::string& folder)
{
    std::map<std::string, CatRow> rows;
    {
        std::lock_guard<std::mutex> hold(g_cat_lock);
        std::string rel;
        lhcat* c = catalogue_for(folder, &rel);
        if (c && lhcat_list_dir(c, rel.c_str(), cat_row, &rows) != 0) rows.clear();
    }
    for (Item& it : items) {
        if (it.flags & (IsVirtual | IsDrive)) { it.xattrs.clear(); continue; }
        auto r = rows.find(base_name(it.path));
        struct stat st;
        // the catalogue's values while the file is as it observed it (an attribute changed elsewhere changes the ctime)
        if (r != rows.end() && (r->second.coverage & LHCAT_ATTRS) && lstat(it.path.c_str(), &st) == 0 &&
            r->second.mtime == (int64_t)st.st_mtime &&
            r->second.ctime_ns == (int64_t)st.st_ctim.tv_sec * 1000000000 + st.st_ctim.tv_nsec &&
            (S_ISDIR(st.st_mode) || r->second.size == (int64_t)st.st_size)) {
            it.xattrs.clear();
            for (const Property& p : registry()) {
                if (p.xattr.empty()) continue;
                for (const auto& kv : r->second.attrs) if (kv.first == p.xattr && !kv.second.empty()) it.xattrs.push_back(kv);
            }
        } else read_stored(it);
    }
}

std::vector<std::string> split_list(const std::string& v)
{
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&]() {
        size_t a = cur.find_first_not_of(" \t\r"), b = cur.find_last_not_of(" \t\r");
        if (a != std::string::npos) {
            std::string t = cur.substr(a, b - a + 1);
            if (std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
        }
        cur.clear();
    };
    for (char c : v) { if (c == ',' || c == ';' || c == '\n') flush(); else cur += c; }
    flush();
    return out;
}

std::string join_list(const std::vector<std::string>& items)
{
    std::string out;
    for (const std::string& t : items) out += (out.empty() ? "" : ",") + t;
    return out;
}

// setxattr, or removexattr for an empty value (not being there is fine then)
static bool put(const std::string& path, const std::string& xattr, const std::string& value, std::string* err)
{
    int rc = value.empty() ? lhde_xattr_remove(path.c_str(), xattr.c_str()) : lhde_xattr_set(path.c_str(), xattr.c_str(), value.data(), value.size());
    if (rc == 0 || (value.empty() && errno == LHDE_ENOATTR)) return true;
    if (!err) return false;
    std::string n = "'" + base_name(path) + "'";
    switch (errno) {
    case ENOTSUP: *err = "The file system of " + n + " does not keep descriptions and tags (extended attributes)."; break;
    case EACCES: case EPERM: case EROFS: *err = n + " cannot be changed: it is read-only, or not yours."; break;
    case E2BIG: case ENOSPC: case ERANGE: *err = "The value is too long to be kept with " + n + "."; break;
    case ENOENT: *err = n + " is not there any more."; break;
    default: *err = "The value could not be kept with " + n + ": " + strerror(errno) + "."; break;
    }
    return false;
}

bool write(const std::string& path, const Property& p, const std::string& text, std::string* err)
{
    if (p.xattr.empty() || !p.editable) { if (err) *err = "'" + p.label + "' cannot be changed."; return false; }
    std::string v;
    if (p.type == ValueType::List) v = join_list(split_list(text));
    else if (p.multiline) {
        for (size_t i = 0; i < text.size(); i++)   // CR LF (an edit control's) and a lone CR as LF
            if (text[i] == '\r') { v += '\n'; if (i + 1 < text.size() && text[i + 1] == '\n') i++; }
            else v += text[i];
    } else {
        for (char c : text) {   // one line: each run of line breaks a space
            bool br = c == '\r' || c == '\n';
            if (!br) v += c;
            else if (!v.empty() && v.back() != ' ') v += ' ';
        }
    }
    if (p.type != ValueType::List) {
        size_t a = v.find_first_not_of(" \t\r\n"), b = v.find_last_not_of(" \t\r\n");
        v = a == std::string::npos ? "" : v.substr(a, b - a + 1);
    }
    if (p.type == ValueType::Color)
        if (const Color* c = color(v)) v = c->name;
    std::string old;
    if (!read_current(path, p, &old, err)) return false;   // the value it replaces was not seen
    if (!put(path, p.xattr, v, err)) return false;
    follow(path, p.xattr, old, v);
    return true;
}

bool change_tags(const std::string& path, const std::vector<std::string>& add, const std::vector<std::string>& remove,
                 std::string* err)
{
    const Property* p = property("tags");
    std::string old;
    if (!read_current(path, *p, &old, err)) return false;
    std::vector<std::string> have = split_list(old);
    for (const std::string& t : add)
        for (const std::string& u : split_list(t))
            if (std::find(have.begin(), have.end(), u) == have.end()) have.push_back(u);
    for (const std::string& t : remove)
        have.erase(std::remove(have.begin(), have.end(), t), have.end());
    std::string v = join_list(have);
    if (v == join_list(split_list(old))) return true;
    if (!put(path, p->xattr, v, err)) return false;
    follow(path, p->xattr, old, v);
    return true;
}

bool add_tags(const std::string& path, const std::vector<std::string>& tags, std::string* err)
{
    return change_tags(path, tags, {}, err);
}

bool remove_tag(const std::string& path, const std::string& tag, std::string* err)
{
    return change_tags(path, {}, { tag }, err);
}

static std::string recent_tags_file() { return state_file("flxp-tags"); }

static std::vector<std::string> recent_tags()
{
    std::vector<std::string> out;
    FILE* f = fopen(recent_tags_file().c_str(), "r");
    if (!f) return out;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        std::string t = line;
        while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.pop_back();
        if (!t.empty() && std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
    }
    fclose(f);
    return out;
}

void remember_tags(const std::vector<std::string>& tags)
{
    std::vector<std::string> list;
    for (const std::string& t : tags)
        for (const std::string& u : split_list(t))
            if (std::find(list.begin(), list.end(), u) == list.end()) list.push_back(u);
    if (list.empty()) return;
    for (const std::string& t : recent_tags())
        if (std::find(list.begin(), list.end(), t) == list.end()) list.push_back(t);
    if (list.size() > 100) list.resize(100);
    std::string text;
    for (const std::string& t : list) text += t + "\n";
    write_file_atomic(recent_tags_file(), text);   // best effort: a convenience
}

std::vector<std::string> known_tags(const std::vector<std::string>& paths)
{
    std::vector<std::string> out;
    auto add = [&](const std::string& value) {
        for (const std::string& t : split_list(value))
            if (std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
    };
    std::string v;
    for (size_t i = 0; i < paths.size() && i < 500; i++)
        if (read_attr(paths[i], "user.xdg.tags", &v) == Read::Value) add(v);
    for (const std::string& t : recent_tags()) add(t);
    std::vector<std::string> dirs;
    for (const std::string& p : paths) {
        std::string d = p.substr(0, p.find_last_of('/'));
        if (d.empty()) d = "/";
        if (std::find(dirs.begin(), dirs.end(), d) == dirs.end() && dirs.size() < 8) dirs.push_back(d);
    }
    for (const std::string& d : dirs) {
        DIR* dir = opendir(d.c_str());
        if (!dir) continue;
        int n = 0;
        while (struct dirent* e = readdir(dir)) {
            if (++n > 2000) break;
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            std::string f = (d == "/" ? "" : d) + "/" + e->d_name;
            if (read_attr(f, "user.xdg.tags", &v) == Read::Value) add(v);
        }
        closedir(dir);
    }
    return out;
}

std::string first_line(const std::string& text)
{
    size_t nl = text.find('\n');
    if (nl == std::string::npos) return text;
    std::string line = text.substr(0, nl);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (text.find_first_not_of(" \t\r\n", nl) == std::string::npos) return line;   // only blank lines follow
    return line + "\xe2\x80\xa6";
}

std::string shown(const Property& p, const std::string& v)
{
    if (p.type == ValueType::List) return tag_list(v);
    if (p.type == ValueType::Color) { const Color* c = color(v); return c ? c->label : v; }
    if (p.multiline) return first_line(v);
    return v;
}

const std::vector<Color>& palette()
{
    static const std::vector<Color> p = {
        { "red", "Red", 0xFF0000 }, { "orange", "Orange", 0xFF8C00 }, { "yellow", "Yellow", 0xFFFF00 },
        { "green", "Green", 0x008000 }, { "teal", "Teal", 0x008080 }, { "blue", "Blue", 0x0000FF },
        { "purple", "Purple", 0x800080 }, { "pink", "Pink", 0xFF69B4 }, { "brown", "Brown", 0x8B4513 },
        { "gray", "Gray", 0x808080 },
    };
    return p;
}

const Color* color(const std::string& value)
{
    for (const Color& c : palette()) if (!strcasecmp(c.name, value.c_str())) return &c;
    return nullptr;
}

std::string run_command(const std::string& command, const std::string& path)
{
    std::string out, q = shell_quote(path);
    bool file = false;
    for (size_t i = 0; i < command.size(); i++) {
        if (command[i] == '%' && i + 1 < command.size() && command[i + 1] == 'f') { out += q; file = true; i++; continue; }
        if (command[i] == '%' && i + 1 < command.size() && command[i + 1] == '%') { out += '%'; i++; continue; }
        out += command[i];
    }
    if (!file) out += " " + q;
    return out;
}

std::string run_program(const std::string& command)
{
    size_t a = command.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    std::string prog;
    if (command[a] == '\'' || command[a] == '"') {   // a quoted program: up to its closing quote
        size_t b = command.find(command[a], a + 1);
        prog = command.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
    } else {
        size_t b = command.find_first_of(" \t", a);
        prog = command.substr(a, b == std::string::npos ? std::string::npos : b - a);
    }
    size_t slash = prog.find_last_of('/');
    return slash == std::string::npos || slash + 1 == prog.size() ? prog : prog.substr(slash + 1);
}

std::string infotip(const Item& it)
{
    std::string out;
    auto line = [&](const std::string& label, const std::string& v) {
        if (!v.empty()) out += (out.empty() ? "" : "\n") + label + ": " + v;
    };
    bool fs = !(it.flags & (IsVirtual | IsDrive));
    if (!it.is_folder()) {
        line("Type", it.type);
        if (it.mtime) line("Date Modified", format_date(it.mtime));
        if (fs) line("Size", byte_size(it.size));
    } else if (fs && it.mtime) {
        line("Date Modified", format_date(it.mtime));
    }
    Stored s = it.xattrs;
    if (s.empty() && fs && !(it.flags & IsSlow)) s = read_stored(it.path);   // (a slow entry: what the list read)
    for (const Property& p : registry()) {
        if (p.xattr.empty()) continue;
        for (const auto& kv : s) {
            if (kv.first != p.xattr) continue;
            if (p.type == ValueType::Command) line("Runs with", run_program(kv.second));
            else line(p.label, shown(p, kv.second));
        }
    }
    return out;
}

// ---- the registry and the item kinds' lists --------------------------------------

const std::vector<Property>& registry()
{
    static const std::vector<Property> r = {
        // derived
        { "type", "Type", ValueType::Text, "", false, "" },
        { "attributes", "Attributes", ValueType::Text, "", false, "" },
        { "modified", "Date Modified", ValueType::Date, "", false, "" },
        { "size", "Size", ValueType::Size, "", false, "" },
        { "dimensions", "Dimensions", ValueType::Text, "", false, "" },
        { "filesystem", "File System", ValueType::Text, "", false, "" },
        { "freespace", "Free Space", ValueType::Size, "", false, "" },
        { "capacity", "Total Size", ValueType::Size, "", false, "" },
        // stored (docs/ANNOTATIONS.md)
        { "description", "Description", ValueType::Text, "user.lux.desc", true, "Add a description" },
        { "tags", "Tags", ValueType::List, "user.xdg.tags", true, "Add tags" },
        { "comment", "Comment", ValueType::Text, "user.xdg.comment", true, "Add comments", true },
        { "color", "Color", ValueType::Color, "user.lux.color", true, "" },
        { "run", "Run as", ValueType::Command, "user.lux.run", true, "" },
    };
    return r;
}

const Property* property(const std::string& id)
{
    for (const Property& p : registry()) if (p.id == id) return &p;
    return nullptr;
}

// the list's columns beyond the derived four, in SortBy's order
static const char* const COLUMN_IDS[] = { "description", "tags", "comment", "color" };
static const int N_COLUMN_IDS = 4;

const Property* column_property(SortBy col)
{
    int k = (int)col - (int)SortBy::Description;
    return k >= 0 && k < N_COLUMN_IDS ? property(COLUMN_IDS[k]) : nullptr;
}

int column_of(const std::string& id)
{
    static const char* const DERIVED[] = { "name", "size", "type", "modified" };
    for (int k = 0; k < 4; k++) if (id == DERIVED[k]) return k;
    for (int k = 0; k < N_COLUMN_IDS; k++) if (id == COLUMN_IDS[k]) return (int)SortBy::Description + k;
    return -1;
}

// Per item kind the properties each presentation lists, in its order
struct KindList { std::vector<const char*> xp, lh; };
static const KindList& kind_list(const Item& it, const Facts& f)
{
    static const KindList FILE_ = { { "type", "attributes", "modified", "size", "description", "tags", "comment", "color" },
                                    { "size", "modified", "type", "description", "tags", "comment" } };
    static const KindList PICTURE = { { "type", "dimensions", "size", "modified", "description", "tags", "comment", "color" },
                                      { "size", "modified", "dimensions", "description", "tags", "comment" } };
    static const KindList SONG = { { "size", "type", "description", "tags", "comment", "color" },
                                   { "size", "modified", "type", "description", "tags", "comment" } };
    static const KindList FOLDER = { { "type", "attributes", "modified", "description", "tags", "comment", "color" },
                                     { "modified", "type", "description", "tags", "comment" } };
    static const KindList DRIVE = { { "type", "filesystem", "freespace", "capacity" }, { "type", "freespace" } };
    static const KindList SYSTEM = { { "type" }, { "type" } };
    switch (f.kind) {
    case Facts::System: return SYSTEM;
    case Facts::Drive: return DRIVE;
    case Facts::Picture: return it.is_folder() ? FOLDER : PICTURE;
    case Facts::Audio: return SONG;
    default: return it.is_folder() ? FOLDER : FILE_;
    }
}

// XP's type for a drive: shell32's drive descriptions
static std::string drive_type(const Facts& f) { return f.network ? "Network Drive" : "Local Disk"; }

static std::string stored(const Facts& f, const std::string& xattr)
{
    for (const auto& kv : f.xattrs) if (kv.first == xattr) return kv.second;
    return "";
}

// a property's value for one item, in XP's format (xp) or 4051's
static std::string value(const Property& p, const Item& it, const Facts& f, bool xp, int64_t now)
{
    if (!p.xattr.empty()) {
        std::string v = stored(f, p.xattr);
        return shown(p, v);
    }
    const std::string& id = p.id;
    if (id == "type") {
        if (f.kind == Facts::Drive) return drive_type(f);
        if (f.kind == Facts::System) return it.type.empty() ? "System Folder" : it.type;
        return it.type;
    }
    if (id == "attributes") return attributes(it);
    if (id == "modified") {
        if (!it.mtime) return "";
        if (xp) return friendly_date(it.mtime, now);
        return f.kind == Facts::Picture && !it.is_folder() ? short_date(it.mtime) : format_date(it.mtime);
    }
    if (id == "size") {
        if (it.is_folder()) return "";
        if (xp) return it.size > 0 ? byte_size(it.size) : "";   // XP shows no 0 byte sizes
        return format_size(it.size);
    }
    if (id == "dimensions") return f.width > 0 && f.height > 0 ? std::to_string(f.width) + " x " + std::to_string(f.height) : "";
    if (id == "filesystem") return f.fs;
    if (id == "freespace") {
        if (!f.total) return "";
        return xp ? byte_size(f.free) : byte_size(f.free) + " free of " + byte_size(f.total);
    }
    if (id == "capacity") return f.total ? byte_size(f.total) : "";
    return "";
}

static std::vector<Row> rows_for(const Item& it, const Facts& f, int64_t now)
{
    const KindList& k = kind_list(it, f);
    std::vector<Row> rows;
    auto row = [&](const char* id) -> Row* {
        for (Row& r : rows) if (r.id == id) return &r;
        const Property* p = property(id);
        if (!p) return nullptr;
        Row r;
        r.id = id;
        r.label = r.id == "type" ? "" : p->label;
        r.xp = value(*p, it, f, true, now);
        r.lh = value(*p, it, f, false, now);
        if (!p->xattr.empty()) { std::string v = stored(f, p->xattr); r.raw = p->type == ValueType::List ? tag_list(v) : v; }
        r.editable = p->editable && !p->xattr.empty() && f.kind != Facts::System && f.kind != Facts::Drive;
        r.invitation = p->invitation;
        rows.push_back(r);
        return &rows.back();
    };
    for (size_t i = 0; i < k.lh.size(); i++) if (Row* r = row(k.lh[i])) r->lh_rank = (int)i;
    for (size_t i = 0; i < k.xp.size(); i++) if (Row* r = row(k.xp[i])) r->xp_rank = (int)i;
    return rows;
}

std::string tag_list(const std::string& v)
{
    std::string out;
    for (const std::string& t : split_list(v)) out += (out.empty() ? "" : ", ") + t;
    return out;
}

// ---- the Filter box ----------------------------------------------------------------

static std::string ascii_lower(std::string s)
{
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

bool Filter::stored() const
{
    for (const Word& w : words) if (w.field != Name) return true;
    return false;
}

Filter parse_filter(const std::string& text)
{
    static const struct { const char* prefix; Filter::Field field; } FIELDS[] = {
        { "tag:", Filter::Tags }, { "tags:", Filter::Tags }, { "desc:", Filter::Desc }, { "description:", Filter::Desc },
        { "comment:", Filter::Comment }, { "name:", Filter::Name },
    };
    Filter f;
    size_t i = 0, n = text.size();
    while (i < n) {
        while (i < n && (text[i] == ' ' || text[i] == '\t')) i++;
        if (i >= n) break;
        // a word: up to the next space outside double quotes
        std::string raw;
        bool quoted = false;
        for (; i < n && (quoted || (text[i] != ' ' && text[i] != '\t')); i++) {
            if (text[i] == '"') { quoted = !quoted; continue; }
            raw += text[i];
        }
        Filter::Word w;
        std::string low = ascii_lower(raw);
        bool fielded = false;
        for (const auto& fd : FIELDS) {
            size_t len = strlen(fd.prefix);
            if (low.compare(0, len, fd.prefix) == 0) { w.field = fd.field; w.text = low.substr(len); fielded = true; break; }
        }
        if (!fielded) w.text = low;
        if (w.field == Filter::Any && w.text.empty()) continue;   // "" alone: nothing to look for
        f.words.push_back(w);
    }
    return f;
}

static bool has(const std::string& value, const std::string& word) { return ascii_lower(value).find(word) != std::string::npos; }

static bool word_matches(const Filter::Word& w, const Item& it)
{
    static const Property* desc = property("description");
    static const Property* tags = property("tags");
    static const Property* comment = property("comment");
    auto field = [&](Filter::Field f) -> bool {
        if (f == Filter::Name) return w.text.empty() ? true : has(it.name, w.text);
        const Property* p = f == Filter::Desc ? desc : f == Filter::Tags ? tags : comment;
        if (!p) return false;
        const std::string& v = it.stored(p->xattr);
        if (w.text.empty()) return !v.empty();
        if (f != Filter::Tags) return has(v, w.text);
        for (const std::string& t : split_list(v)) if (has(t, w.text)) return true;   // within one tag, not across the commas
        return false;
    };
    if (w.field != Filter::Any) return field(w.field);
    return field(Filter::Name) || field(Filter::Desc) || field(Filter::Tags) || field(Filter::Comment);
}

bool filter_matches(const Filter& f, const Item& it)
{
    for (const Filter::Word& w : f.words) if (!word_matches(w, it)) return false;
    return true;
}

// ---- the two presentations ----------------------------------------------------------

std::string xp_title(const Info& in) { return in.kind == Kind::Many ? "" : in.name; }

std::vector<Line> xp_lines(const Info& in)
{
    std::vector<Line> out;
    if (in.kind == Kind::Many) {
        out.push_back(Line{ "", items_selected_xp((size_t)in.count) });
        if (in.total > 0) {
            out.push_back(Line{ "", "" });
            out.push_back(Line{ "Total File Size", byte_size(in.total) });
        }
        return out;
    }
    std::vector<const Row*> rs;
    for (const Row& r : in.rows) if (r.xp_rank >= 0 && !r.xp.empty()) rs.push_back(&r);
    std::sort(rs.begin(), rs.end(), [](const Row* a, const Row* b) { return a->xp_rank < b->xp_rank; });
    for (const Row* r : rs) out.push_back(Line{ r->label, r->xp });
    return out;
}

std::string lh_title(const Info& in)
{
    if (in.kind == Kind::Many) return std::to_string(in.count) + " items selected";
    return in.kind == Kind::One ? in.name : "";
}

std::vector<LhLine> lh_lines(const Info& in)
{
    std::vector<const Row*> rs;
    for (const Row& r : in.rows) if (r.lh_rank >= 0) rs.push_back(&r);
    std::sort(rs.begin(), rs.end(), [](const Row* a, const Row* b) { return a->lh_rank < b->lh_rank; });
    std::vector<LhLine> out;
    for (const Row* r : rs) {
        LhLine l;
        l.editable = r->editable && in.kind == Kind::One;
        l.id = r->id;
        l.raw = r->raw;
        if (!r->lh.empty()) l.text = r->lh;
        else if (r->editable && !r->invitation.empty() && in.kind == Kind::One) { l.text = r->invitation; l.invitation = true; }
        else continue;
        out.push_back(l);
    }
    return out;
}

// ---- the model ---------------------------------------------------------------------------

Info describe(const std::vector<Item>& sel, const std::vector<Facts>& facts, const Item& location,
              const Facts& location_facts, int count, int64_t now, int focus)
{
    Info in;
    auto fact = [&](size_t i) { return i < facts.size() ? facts[i] : Facts(); };
    if (sel.empty()) {
        in.kind = Kind::Location;
        in.count = count;
        in.icon = location.icon;
        in.name = location.name;
        in.path = location.path;
        in.rows = rows_for(location, location_facts, now);
        for (Row& r : in.rows) r.lh_rank = -1;   // 4051 shows the count alone
        return in;
    }
    size_t f0 = focus >= 0 && (size_t)focus < sel.size() ? (size_t)focus : 0;
    in.count = (int)sel.size();
    in.icon = sel[f0].icon;
    if (sel.size() == 1) {
        const Item& it = sel[0];
        Facts f = fact(0);
        in.kind = Kind::One;
        in.name = it.name;
        in.path = it.path;
        in.rows = rows_for(it, f, now);
        in.xp_preview = !it.is_folder() && f.thumbnail && !f.network;
        in.folder = it.is_folder();
        if (!it.is_folder() && f.thumbnail) in.previews.push_back(it.path);
        return in;
    }
    in.kind = Kind::Many;
    // XP: the files' total, when there are at most 100
    if (sel.size() <= 100)
        for (const Item& it : sel) if (!it.is_folder()) in.total += it.size;
    // 4051: the shared values or "(multiple values)": the size (when files are
    // selected) and the date
    bool files = false, sizes_same = true, dates_same = true;
    std::string size0, date0;
    for (size_t i = 0; i < sel.size(); i++) {
        const Item& it = sel[i];
        std::string d = it.mtime ? short_date(it.mtime) : "";
        if (i == 0) date0 = d; else if (d != date0) dates_same = false;
        if (it.is_folder()) continue;
        std::string s = format_size(it.size);
        if (!files) size0 = s; else if (s != size0) sizes_same = false;
        files = true;
    }
    auto add = [&](const char* id, const std::string& v) {
        Row r; r.id = id; r.label = property(id)->label; r.lh = v; r.lh_rank = (int)in.rows.size();
        in.rows.push_back(r);
    };
    if (files) add("size", sizes_same ? size0 : "(multiple values)");
    if (!date0.empty() || !dates_same) add("modified", dates_same ? date0 : "(multiple values)");
    // the stack: up to 7 pictures, the item of interest first
    for (size_t k = 0; k < sel.size() && in.previews.size() < 7; k++) {
        size_t i = (f0 + k) % sel.size();
        if (!sel[i].is_folder() && fact(i).thumbnail) in.previews.push_back(sel[i].path);
    }
    return in;
}

Info describe(const std::vector<Item>& sel, const Item& location, int count, int focus)
{
    std::vector<Facts> facts;
    // probed: the one item, or for several the files among the first 64 (the
    // stack's pictures; a header each) - XP's lines for several need no facts
    for (size_t i = 0; i < sel.size() && i < 64; i++) facts.push_back(sel.size() == 1 || !sel[i].is_folder() ? probe(sel[i]) : Facts());
    Facts lf = sel.empty() ? probe(location) : Facts();
    return describe(sel, facts, location, lf, count, (int64_t)time(nullptr), focus);
}

} // namespace details
} // namespace shell
