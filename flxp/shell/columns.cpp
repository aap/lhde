// The Details columns' layout and the per-folder records (columns.h).
#include <stdlib.h>
#include <sys/stat.h>
#include <algorithm>
#include <map>
#include "columns.h"
#include "config.h"

namespace shell {
namespace columns {

static const struct { const char* name; int width; bool shown; } KNOWN[] = {
    { "name", 180, true }, { "size", 96, true }, { "type", 120, true }, { "modified", 120, true },
    { "description", 180, false }, { "tags", 120, false }, { "comment", 150, false },
};
static const int N_KNOWN = (int)(sizeof KNOWN / sizeof KNOWN[0]);

const char* name_of(int id) { return id >= 0 && id < N_KNOWN ? KNOWN[id].name : nullptr; }

static int id_of(const std::string& name)
{
    for (int i = 0; i < N_KNOWN; i++) if (name == KNOWN[i].name) return i;
    return -1;
}

Layout builtin()
{
    Layout l;
    for (int i = 0; i < N_KNOWN; i++) l.push_back(Column{ i, KNOWN[i].shown, KNOWN[i].width });
    return l;
}

static Column* find(Layout& l, int id)
{
    for (Column& c : l) if (c.id == id) return &c;
    return nullptr;
}

Layout parse(const std::string& text, const Layout& base_in)
{
    Layout base = base_in;
    for (int i = 0; i < N_KNOWN; i++) if (!find(base, i)) base.push_back(Column{ i, KNOWN[i].shown, KNOWN[i].width });
    Layout out;
    size_t i = 0;
    while (i < text.size()) {
        size_t e = text.find_first_of(" \t\n", i);
        if (e == std::string::npos) e = text.size();
        std::string w = text.substr(i, e - i);
        i = e + 1;
        bool hidden = !w.empty() && w[0] == '-';
        if (hidden) w.erase(0, 1);
        size_t colon = w.find(':');
        int id = id_of(w.substr(0, colon));
        if (id < 0 || find(out, id)) continue;
        Column c = *find(base, id);
        c.shown = !hidden;
        int px = colon == std::string::npos ? 0 : atoi(w.c_str() + colon + 1);
        if (px > 0 && px < 4000) c.width = px;
        out.push_back(c);
    }
    bool named = !out.empty();
    for (const Column& b : base)
        if (!find(out, b.id)) { Column c = b; if (named) c.shown = false; out.push_back(c); }   // not named: hidden (an older file)
    if (Column* n = find(out, 0)) n->shown = true;
    return out;
}

std::string format(const Layout& l)
{
    std::string s;
    for (const Column& c : l) {
        const char* n = name_of(c.id);
        if (!n) continue;
        s += (s.empty() ? "" : " ") + std::string(c.shown ? "" : "-") + n + ":" + std::to_string(c.width);
    }
    return s;
}

std::vector<int> shown(const Layout& l)
{
    std::vector<int> ids;
    for (const Column& c : l) if (c.shown) ids.push_back(c.id);
    return ids;
}

bool is_shown(const Layout& l, int id)
{
    for (const Column& c : l) if (c.id == id) return c.shown;
    return false;
}

int width(const Layout& l, int id)
{
    for (const Column& c : l) if (c.id == id) return c.width;
    return 0;
}

void set_shown(Layout& l, int id, bool on)
{
    if (Column* c = find(l, id)) c->shown = on || id == 0;
}

void set_width(Layout& l, int id, int px)
{
    if (px <= 0 || px >= 4000) return;
    if (Column* c = find(l, id)) c->width = px;
}

void reorder_shown(Layout& l, const std::vector<int>& order)
{
    std::vector<size_t> slots;
    for (size_t i = 0; i < l.size(); i++) if (l[i].shown) slots.push_back(i);
    std::vector<Column> cols;
    for (int id : order) {
        Column* c = find(l, id);
        if (c && c->shown && std::find_if(cols.begin(), cols.end(), [&](const Column& x) { return x.id == id; }) == cols.end()) cols.push_back(*c);
    }
    for (size_t s : slots)   // shown ones the order leaves out keep their relative order after the named ones
        if (std::find_if(cols.begin(), cols.end(), [&](const Column& x) { return x.id == l[s].id; }) == cols.end()) cols.push_back(l[s]);
    for (size_t k = 0; k < slots.size(); k++) l[slots[k]] = cols[k];
}

} // namespace columns

namespace folderviews {

static std::map<std::string, Record> g_records;
static struct stat g_stamp;      // the file as last read (other windows write it too)

static std::string path() { return state_file("flxp-views"); }

static void load()
{
    struct stat st = {};
    stat(path().c_str(), &st);
    if (st.st_mtim.tv_sec == g_stamp.st_mtim.tv_sec && st.st_mtim.tv_nsec == g_stamp.st_mtim.tv_nsec &&
        st.st_size == g_stamp.st_size && st.st_ino == g_stamp.st_ino) return;
    g_stamp = st;
    g_records.clear();
    std::string data;
    if (!read_file(path(), data)) return;
    size_t at = 0;
    while (at < data.size()) {
        size_t nl = data.find('\n', at);
        std::string line = data.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        at = nl == std::string::npos ? data.size() : nl + 1;
        Record r;
        size_t tab = line.rfind('\t');
        if (tab == std::string::npos || !tab) continue;
        if (line.compare(tab + 1, 8, "columns=") == 0) {
            r.columns = line.substr(tab + 9);
            line.resize(tab);
            tab = line.rfind('\t');
            if (tab == std::string::npos || !tab) continue;
        }
        r.view = line.substr(tab + 1);
        if (!r.view.empty() || !r.columns.empty()) g_records[line.substr(0, tab)] = r;
    }
}

static void save()
{
    std::string out;
    for (const auto& r : g_records) {
        if (r.second.view.empty() && r.second.columns.empty()) continue;
        out += r.first + '\t' + r.second.view + (r.second.columns.empty() ? "" : "\tcolumns=" + r.second.columns) + '\n';
    }
    write_file_atomic(path(), out);
    stat(path().c_str(), &g_stamp);
}

Record get(const std::string& folder)
{
    load();
    auto it = g_records.find(folder);
    return it == g_records.end() ? Record() : it->second;
}

static void set(const std::string& folder, std::string Record::*field, const std::string& value)
{
    if (folder.empty()) return;
    load();
    auto it = g_records.find(folder);
    if (it == g_records.end() ? value.empty() : it->second.*field == value) return;
    Record& r = g_records[folder];
    r.*field = value;
    if (r.view.empty() && r.columns.empty()) g_records.erase(folder);
    save();
}

void set_view(const std::string& folder, const std::string& view) { set(folder, &Record::view, view); }
void set_columns(const std::string& folder, const std::string& layout) { set(folder, &Record::columns, layout); }

static void forget(std::string Record::*field)
{
    load();
    bool any = false;
    for (auto it = g_records.begin(); it != g_records.end();) {
        if (!(it->second.*field).empty()) { (it->second.*field).clear(); any = true; }
        if (it->second.view.empty() && it->second.columns.empty()) it = g_records.erase(it); else ++it;
    }
    if (any) save();
}

void forget_views() { forget(&Record::view); }
void forget_columns() { forget(&Record::columns); }

} // namespace folderviews
} // namespace shell
