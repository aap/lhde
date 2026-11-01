// The details model (details.h). The rules are XP's and 4051's, read from
// shell32's duiinfo.cpp, prop.cpp and selfreg.inx, shlwapi's
// StrFormatByteSizeW and SHFormatDateTimeW (XP SP1), and 4051's managed
// preview pane (taskpane/managed.md in the Longhorn research); written anew.
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/xattr.h>
#include <time.h>
#include <algorithm>
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
    f.network = thumbs::on_network(it.path);
    if (it.flags & IsDrive) {
        f.kind = Facts::Drive;
        struct statvfs st;
        if (statvfs(it.path.c_str(), &st) == 0 && st.f_blocks) {
            f.total = (uint64_t)st.f_blocks * st.f_frsize;
            f.free = (uint64_t)st.f_bavail * st.f_frsize;
        }
        // the file system: the last /proc/mounts line for the mount point
        FILE* m = fopen("/proc/mounts", "r");
        if (m) {
            char line[1024], dev[256], mnt[512], fs[64];
            while (fgets(line, sizeof line, m))
                if (sscanf(line, "%255s %511s %63s", dev, mnt, fs) == 3 && it.path == mnt) f.fs = fs;
            fclose(m);
        }
        return f;
    }
    f.xattrs = read_stored(it.path);
    if (it.is_folder()) return f;
    std::string key = icon_key(it.name, false);
    if (key == "audio") f.kind = Facts::Audio;
    if (key == "image") {
        f.kind = Facts::Picture;
        thumbs::picture_size(it.path, &f.width, &f.height);
    }
    f.thumbnail = thumbs::can_thumbnail(it.path);
    if (f.thumbnail && f.kind == Facts::Other) f.kind = Facts::Picture;   // by its bytes
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
    if (stat(it.path.c_str(), &st) == 0) it.mtime = st.st_mtime;
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

// the size first, then a buffer of that size; ERANGE: it grew in between, ask again
Read read_attr(const std::string& path, const std::string& xattr, std::string* value, int* error)
{
    value->clear();
    if (error) *error = 0;
    for (int tries = 0; tries < 8; tries++) {
        ssize_t n = getxattr(path.c_str(), xattr.c_str(), nullptr, 0);
        if (n > 0) {
            value->resize((size_t)n);
            n = getxattr(path.c_str(), xattr.c_str(), &(*value)[0], value->size());
            if (n < 0 && errno == ERANGE) continue;
        }
        if (n < 0) {
            int e = errno;
            value->clear();
            if (e == ENODATA || e == ENOTSUP || e == ENOENT) return Read::Missing;
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
    int rc = value.empty() ? removexattr(path.c_str(), xattr.c_str()) : setxattr(path.c_str(), xattr.c_str(), value.data(), value.size(), 0);
    if (rc == 0 || (value.empty() && errno == ENODATA)) return true;
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
    std::string v = text;
    if (p.type == ValueType::List) v = join_list(split_list(text));
    else {
        size_t a = v.find_first_not_of(" \t\r\n"), b = v.find_last_not_of(" \t\r\n");
        v = a == std::string::npos ? "" : v.substr(a, b - a + 1);
    }
    std::string old;
    if (!read_current(path, p, &old, err)) return false;   // the value it replaces was not seen
    return put(path, p.xattr, v, err);
}

bool add_tags(const std::string& path, const std::vector<std::string>& tags, std::string* err)
{
    const Property* p = property("tags");
    std::string old;
    if (!read_current(path, *p, &old, err)) return false;
    std::vector<std::string> have = split_list(old);
    for (const std::string& t : tags)
        for (const std::string& u : split_list(t))
            if (std::find(have.begin(), have.end(), u) == have.end()) have.push_back(u);
    std::string v = join_list(have);
    if (v == old) return true;
    return put(path, p->xattr, v, err);
}

bool remove_tag(const std::string& path, const std::string& tag, std::string* err)
{
    const Property* p = property("tags");
    std::string old;
    if (!read_current(path, *p, &old, err)) return false;
    std::vector<std::string> have = split_list(old);
    auto i = std::find(have.begin(), have.end(), tag);
    if (i == have.end()) return true;
    have.erase(i);
    return put(path, p->xattr, join_list(have), err);
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
    if (s.empty() && fs) s = read_stored(it.path);
    for (const Property& p : registry()) {
        if (p.xattr.empty()) continue;
        for (const auto& kv : s)
            if (kv.first == p.xattr) line(p.label, p.type == ValueType::List ? tag_list(kv.second) : kv.second);
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
        { "comment", "Comment", ValueType::Text, "user.xdg.comment", true, "Add comments" },
    };
    return r;
}

const Property* property(const std::string& id)
{
    for (const Property& p : registry()) if (p.id == id) return &p;
    return nullptr;
}

// the list's columns beyond the derived four, in SortBy's order
static const char* const COLUMN_IDS[] = { "description", "tags", "comment" };

const Property* column_property(SortBy col)
{
    int k = (int)col - (int)SortBy::Description;
    return k >= 0 && k < 3 ? property(COLUMN_IDS[k]) : nullptr;
}

int column_of(const std::string& id)
{
    static const char* const DERIVED[] = { "name", "size", "type", "modified" };
    for (int k = 0; k < 4; k++) if (id == DERIVED[k]) return k;
    for (int k = 0; k < 3; k++) if (id == COLUMN_IDS[k]) return (int)SortBy::Description + k;
    return -1;
}

// Per item kind the properties each presentation lists, in its order
struct KindList { std::vector<const char*> xp, lh; };
static const KindList& kind_list(const Item& it, const Facts& f)
{
    static const KindList FILE_ = { { "type", "attributes", "modified", "size", "description", "tags", "comment" },
                                    { "size", "modified", "type", "description", "tags", "comment" } };
    static const KindList PICTURE = { { "type", "dimensions", "size", "modified", "description", "tags", "comment" },
                                      { "size", "modified", "dimensions", "description", "tags", "comment" } };
    static const KindList SONG = { { "size", "type", "description", "tags", "comment" },
                                   { "size", "modified", "type", "description", "tags", "comment" } };
    static const KindList FOLDER = { { "type", "attributes", "modified", "description", "tags", "comment" },
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
        return p.type == ValueType::List ? tag_list(v) : v;
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
        if (!p->xattr.empty()) r.raw = r.lh;
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
