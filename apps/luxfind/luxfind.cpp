// luxfind: the query window over the catalogues (docs/CATALOGUE.md "luxfind", docs/USAGE.md). A search pane on the left
// and the Explorer's list on the right with the results as they come, the count of matches in the status bar.
//
// The look. No screenshot of 4051's query views (the "All Pictures and Videos", "All Documents" virtual folders with
// their filter-by-property header) or of XP's Search Companion results is at hand, so the layout is a choice: XP's
// Search Companion as it is remembered, a pane of labelled boxes on the left ("All or part of the file name:", "Look
// in:"), the results in the Explorer's Details list with XP's Search Results column "In Folder" after Name, the status
// bar under both. The pane's other boxes (Tags, Type) follow the same pattern; the Pending column is ours. The list is
// the browser widget of flxp and its file dialog (browser.h), the context menu the Explorer's (verbs.h).
//
// What it does, besides showing:
//   - the name box searches as typed (250 ms after the last key), a part of the name, ASCII case ignored, every
//     catalogue's name index (lhcat_query.name); Type, Tags (all of them must hold) and Look in (every root, one, or a
//     folder under one: Browse...) narrow it; an empty query lists everything. A page of 2,000 rows, more as the list
//     is scrolled to its end; a column header click sorts what is loaded.
//   - double click and Enter open as the Explorer opens (verbs::run(ID_OPEN)) when the root's directory answers a stat
//     (model.h Reach, on a thread of its own: never the UI's); a root that does not answer is said in the status bar and
//     a message box says why an open cannot be done. Open Containing Folder is flxp --select.
//   - Add Tag, the Properties sheet's Metadata page and Tag Selected go through the catalogue: on a reachable root whose
//     files are the authority, the file is written and the write recorded, as the Explorer does; on a root not
//     reachable, or whose catalogue is the authority, the value is recorded as an operation that waits (the Pending
//     column, "Not written") until Commit writes it, on a thread, when the root is reachable again.
//
// LUXFIND_TRACE=1 prints what it shows on stdout, a line each (status, pending, commit, reach, opened), for the tests.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>
// the Explorer's widgets first: their w32 headers before wux.h's win32 macros
#include "explorer/browser.h"
#include "explorer/commands.h"
#include "explorer/thumbview.h"
#include "explorer/uiicons.h"
#include "explorer/verbs.h"
#include "shell/shell.h"
// wux/wux.h's WuxInit and UTF-8 helpers (w32's conversions): the whole of wux.h after the w32 headers the Explorer's
// widgets need would define the OLE drop effects twice
extern "C" void WINAPI WuxInit(void);
static std::u16string WuxWide(const std::string& s) { return w32::utf16_of(s); }
static std::string WuxUtf8(const WCHAR* w) { return w ? w32::utf8_of((const char16_t*)w) : std::string(); }
static std::string WuxWindowText(HWND h)
{
    int n = GetWindowTextLengthW(h);
    std::u16string w((size_t)n + 1, 0);
    GetWindowTextW(h, (WCHAR*)&w[0], n + 1);
    return WuxUtf8((const WCHAR*)w.c_str());
}
#include "model.h"
#undef min
#undef max

namespace {

using shell::Item;

enum {
    IDC_NAME = 1001, IDC_TAGS, IDC_TYPE, IDC_LOOKIN, IDC_BROWSE, IDC_TAGSEL, IDC_COMMIT, IDC_LIST, IDC_STATUS,
    ID_LF_OPENFOLDER = 2001, ID_LF_COMMIT_FIRST = 2100, ID_LF_VIEW_DETAILS = 2200, ID_LF_VIEW_THUMBS, ID_LF_CLOSE,
    ID_LF_SELECTALL, ID_LF_FOCUSNAME, ID_LF_FOCUSTAGS, ID_LF_FOCUSTYPE, ID_LF_FOCUSLOOKIN,
    WM_LF_RESULTS = WM_APP + 0x51, WM_LF_REACH, WM_LF_COMMITTED,
    TIMER_QUERY = 1, TIMER_MORE = 2, TIMER_REACH = 3,
    PANE_W = 230,
};

struct Committed { std::string root, text; bool ok; };

struct Find : browser::Browser {
    int icon(const std::string& key) override { return uiicons::index(key); }
    std::string column_text(const Item& it, int id) override;
    void filled() override;
};

struct G {
    HWND wnd = nullptr, pane = nullptr, status = nullptr;
    HWND name = nullptr, tags = nullptr, type = nullptr, lookin = nullptr, browse = nullptr, tagsel = nullptr, commit = nullptr;
    std::vector<HWND> labels;
    HFONT font = nullptr;
    Find b;
    luxfind::Search* search = nullptr;
    luxfind::Reach* reach = nullptr;
    std::vector<luxfind::Root> roots;
    struct Look { std::string root, in, label; };
    std::vector<Look> looks;              // the Look in entries (root "" every root)
    luxfind::Status st;
    bool sorted = false;                  // the user sorted by a column: new rows are sorted in
    std::map<std::string, int64_t> pending;   // per root
    std::string open_waiting;             // a root whose check an open waits for
    bool trace = false;
    // the Tags box narrows the results by what was chosen from its list or ended with Enter; what is typed alone is
    // what Tag Selected adds (so typing a new tag does not empty the list it is meant for)
    std::string applied_tags;
} g;

void trace(const char* fmt, ...)
{
    if (!g.trace) return;
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fflush(stdout);
}

std::u16string Wide(const std::string& s) { return WuxWide(s); }
std::string text_of(HWND h) { return WuxWindowText(h); }

const luxfind::Root* root_named(const std::string& name)
{
    for (const luxfind::Root& r : g.roots) if (r.name == name) return &r;
    return nullptr;
}

const luxfind::Root* root_of(const std::string& path, std::string* rel = nullptr)
{
    return luxfind::root_for(g.roots, path, rel);
}

std::string Find::column_text(const Item& it, int id)
{
    if (id != (int)shell::SortBy::Folder) return "";
    std::string rel;
    const luxfind::Root* r = root_of(it.path, &rel);
    if (!r) return "";
    size_t k = rel.rfind('/');
    return r->name + ": " + (k == std::string::npos ? "" : rel.substr(0, k));
}

// ---- reachability -------------------------------------------------------------------------------------------------------

luxfind::Reach::State reach_of(const luxfind::Root& r, std::string* why = nullptr, bool force = false)
{
    if (r.offline) { if (why) *why = "added offline"; return luxfind::Reach::Unreachable; }
    return g.reach->state(r.name, r.path, force, why);
}

bool reachable(const std::string& root)
{
    const luxfind::Root* r = root_named(root);
    return r && reach_of(*r) == luxfind::Reach::Reachable;
}

// ---- the status bar -------------------------------------------------------------------------------------------------------

std::string thousands(int64_t n) { return shell::details::thousands((uint64_t)n); }

void update_status()
{
    std::string count;
    int64_t shown = (int64_t)g.b.items.size();
    if (!g.st.note.empty()) count = g.st.note;
    else if (g.st.count >= 0 && shown >= g.st.count) count = thousands(g.st.count) + (g.st.count == 1 ? " item" : " items");
    else if (g.st.count >= 0) count = thousands(shown) + " of " + thousands(g.st.count) + " items";
    else count = thousands(shown) + (g.st.done ? (shown == 1 ? " item" : " items") : " items so far (counting...)");
    if (!g.st.error.empty()) count += "  " + g.st.error;
    std::string roots;
    for (const luxfind::Root& r : g.roots) {
        std::string why;
        luxfind::Reach::State s = reach_of(r, &why);
        std::string t = s == luxfind::Reach::Unreachable ? r.name + ": not reachable (" + why + ")"
                      : s == luxfind::Reach::Checking ? r.name + ": checking..." : "";
        if (!t.empty()) roots += (roots.empty() ? "" : "; ") + t;
    }
    int64_t pend = 0;
    for (auto& kv : g.pending) pend += kv.second > 0 ? kv.second : 0;
    std::string pending = pend ? thousands(pend) + (pend == 1 ? " edit not written" : " edits not written") : "";
    SendMessageW(g.status, SB_SETTEXTW, 0, (LPARAM)Wide(count).c_str());
    SendMessageW(g.status, SB_SETTEXTW, 1, (LPARAM)Wide(pending).c_str());
    SendMessageW(g.status, SB_SETTEXTW, 2, (LPARAM)Wide(roots).c_str());
    static std::string last;
    std::string line = "status " + count + " | " + pending + " | " + roots;
    if (line != last) { trace("%s", line.c_str()); last = line; }
}

// the Commit button: the first root with edits not written (the File menu lists them all)
void update_commit()
{
    std::string root;
    int64_t n = 0;
    for (auto& kv : g.pending) if (kv.second > 0) { root = kv.first; n = kv.second; break; }
    if (root.empty()) { ShowWindow(g.commit, SW_HIDE); return; }
    std::string t = "&Commit " + thousands(n) + (n == 1 ? " edit" : " edits") + " to " + root;
    SetWindowTextW(g.commit, (LPCWSTR)Wide(t).c_str());
    SetPropW(g.commit, L"luxfind_root", nullptr);
    ShowWindow(g.commit, SW_SHOW);
}

void count_pending()
{
    for (const luxfind::Root& r : g.roots) {
        int64_t n = luxfind::pending_count(r.name);
        if (n != g.pending[r.name]) trace("pending %s %lld", r.name.c_str(), (long long)n);
        g.pending[r.name] = n;
    }
    update_commit();
    update_status();
}

void Find::filled() { update_status(); }

// ---- the rows ----------------------------------------------------------------------------------------------------------------

bool registry_attr(const std::string& xattr)
{
    for (const shell::details::Property& p : shell::details::registry()) if (!p.xattr.empty() && p.xattr == xattr) return true;
    return false;
}

void set_attrs(Item& it, const std::vector<std::pair<std::string, std::string>>& attrs, int pending, int conflicts)
{
    it.xattrs.clear();
    for (const auto& kv : attrs) if (registry_attr(kv.first)) it.xattrs.push_back(kv);
    it.flags |= shell::IsStored;
    it.flags &= ~(uint32_t)(shell::HasEdits | shell::HasConflict);
    if (pending) it.flags |= shell::HasEdits;
    if (conflicts) it.flags |= shell::HasConflict;
}

Item item_of(const luxfind::Row& r)
{
    Item it;
    it.path = r.path;
    it.name = r.rel.substr(r.rel.rfind('/') + 1);
    bool dir = r.kind == 'd' || r.kind == 'm';
    it.flags = dir ? (uint32_t)shell::IsFolder : 0u;
    if (r.kind == 'l') it.flags |= shell::IsLink;
    if (!it.name.empty() && it.name[0] == '.') it.flags |= shell::IsHidden;
    it.size = r.size > 0 ? (uint64_t)r.size : 0;
    it.mtime = r.mtime > 0 ? r.mtime : 0;
    it.type = shell::type_name(it.name, dir);
    it.icon = shell::icon_key(it.name, dir);
    set_attrs(it, r.attrs, r.pending, r.conflicts);
    // a root not reachable: nothing is read from its files (the thumbnails' worker skips a pending entry)
    if (!reachable(r.root)) it.flags |= shell::IsPending;
    return it;
}

void take_results()
{
    std::vector<luxfind::Row> rows;
    luxfind::Status st;
    g.search->take(rows, st);
    if (st.gen != g.st.gen) {   // a new search: the list emptied
        g.b.items.clear();
        g.b.all_items.clear();
        browser::fill_list(g.b);
    }
    g.st = st;
    if (!rows.empty()) {
        size_t from = g.b.items.size();
        for (const luxfind::Row& r : rows) {
            if (r.rel.empty()) continue;   // (the root itself)
            g.b.items.push_back(item_of(r));
        }
        g.b.all_items = g.b.items;
        if (g.sorted) browser::fill_list(g.b); else browser::append_list(g.b, from);
    }
    update_status();
}

void results_notify(void*) { PostMessageW(g.wnd, WM_LF_RESULTS, 0, 0); }
void reach_notify(void*) { PostMessageW(g.wnd, WM_LF_REACH, 0, 0); }

// ---- the query -----------------------------------------------------------------------------------------------------------------

struct TypeChoice { const char* label; const char* type; };
const TypeChoice TYPES[] = {
    { "(all files and folders)", "" },
    { "Folders", "folders" },
    { "Pictures", "image/*" },
    { "Music", "audio/*" },
    { "Video", "video/*" },
    { "Documents", "text/*,application/pdf,application/rtf,application/msword,application/vnd.oasis.opendocument.text,"
                   "application/vnd.oasis.opendocument.spreadsheet,application/vnd.oasis.opendocument.presentation,"
                   "application/vnd.openxmlformats-officedocument.wordprocessingml.document,"
                   "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet,application/vnd.ms-excel,"
                   "application/vnd.ms-powerpoint,application/postscript" },
};

std::string type_query()
{
    int sel = (int)SendMessageW(g.type, CB_GETCURSEL, 0, 0);
    std::string typed = shell::trim(text_of(g.type));
    for (const TypeChoice& t : TYPES) if (typed == t.label) return t.type;
    if (sel >= 0 && sel < (int)(sizeof TYPES / sizeof TYPES[0]) && typed.empty()) return TYPES[sel].type;
    return typed;   // a MIME type or a group typed ("image/png", "text/*")
}

void run_query()
{
    KillTimer(g.wnd, TIMER_QUERY);
    luxfind::Query q;
    q.name = shell::trim(text_of(g.name));
    q.type = type_query();
    q.tags = g.applied_tags;
    int li = (int)SendMessageW(g.lookin, CB_GETCURSEL, 0, 0);
    if (li >= 0 && li < (int)g.looks.size()) { q.root = g.looks[(size_t)li].root; q.in = g.looks[(size_t)li].in; }
    trace("query name=%s type=%s tags=%s root=%s in=%s", q.name.c_str(), q.type.c_str(), q.tags.c_str(), q.root.c_str(), q.in.c_str());
    g.search->start(q);
}

void query_soon() { SetTimer(g.wnd, TIMER_QUERY, 250, nullptr); }

// the list scrolled near its end: the next page
void maybe_more()
{
    if (g.st.done || g.st.listing) return;
    int n = (int)g.b.items.size();
    int top = (int)SendMessageW(g.b.list, LVM_GETTOPINDEX, 0, 0), per = (int)SendMessageW(g.b.list, LVM_GETCOUNTPERPAGE, 0, 0);
    if (g.b.view != LVS_REPORT) {   // the icon views: the last item's place
        RECT rc, cl;
        GetClientRect(g.b.list, &cl);
        if (n == 0 || !SendMessageW(g.b.list, LVM_GETITEMRECT, (WPARAM)(n - 1), (LPARAM)&(rc = RECT{ LVIR_BOUNDS, 0, 0, 0 }))) return;
        if (rc.top > cl.bottom * 3) return;
    } else if (top + 3 * std::max(per, 10) < n) return;
    g.search->more();
}

// ---- Look in ----------------------------------------------------------------------------------------------------------------------

void fill_lookin(int select)
{
    SendMessageW(g.lookin, CB_RESETCONTENT, 0, 0);
    for (const G::Look& l : g.looks) SendMessageW(g.lookin, CB_ADDSTRING, 0, (LPARAM)Wide(l.label).c_str());
    SendMessageW(g.lookin, CB_SETCURSEL, (WPARAM)select, 0);
}

// a folder (absolute) as a Look in entry, selected; false when no root holds it
bool look_in_folder(const std::string& dir)
{
    std::string rel;
    const luxfind::Root* r = root_of(dir, &rel);
    if (!r) return false;
    for (size_t i = 0; i < g.looks.size(); i++)
        if (g.looks[i].root == r->name && g.looks[i].in == rel) { fill_lookin((int)i); return true; }
    g.looks.push_back(G::Look{ r->name, rel, rel.empty() ? r->name + " (" + r->path + ")" : r->name + ": " + rel });
    fill_lookin((int)g.looks.size() - 1);
    return true;
}

void browse()
{
    filedlg::Options o;
    o.mode = filedlg::Mode::Folder;
    o.title = "Look in";
    int li = (int)SendMessageW(g.lookin, CB_GETCURSEL, 0, 0);
    if (li > 0 && li < (int)g.looks.size())
        if (const luxfind::Root* r = root_named(g.looks[(size_t)li].root))
            if (reach_of(*r) == luxfind::Reach::Reachable)
                o.initial_dir = g.looks[(size_t)li].in.empty() ? r->path : r->path + "/" + g.looks[(size_t)li].in;
    filedlg::Result res = filedlg::run(g.wnd, o);
    if (!res.ok || res.paths.empty()) return;
    if (!look_in_folder(res.paths[0])) {
        shell::message_box(g.wnd, "luxfind", "No catalogue holds " + res.paths[0] + ".\n\nlhde-cat add-root NAME PATH makes one "
                           "(docs/CATALOGUE.md).", MB_OK | MB_ICONINFORMATION);
        return;
    }
    run_query();
}

// ---- the route: the annotations of a root that is not reachable, or whose catalogue is the authority ---------------------------

struct Route : shell::details::Route {
    bool takes(const std::string& path) override
    {
        const luxfind::Root* r = root_of(path);
        if (!r) return false;
        return r->catalogue || reach_of(*r) != luxfind::Reach::Reachable;
    }
    bool read(const std::string& path, const std::string& xattr, std::string* value) override
    {
        std::string rel;
        const luxfind::Root* r = root_of(path, &rel);
        std::vector<std::pair<std::string, std::string>> attrs;
        value->clear();
        if (!r || !luxfind::desired(r->name, rel, &attrs, nullptr, nullptr)) return false;
        for (const auto& kv : attrs) if (kv.first == xattr) *value = kv.second;
        return true;
    }
    std::vector<std::string> names(const std::string& path) override
    {
        std::string rel;
        const luxfind::Root* r = root_of(path, &rel);
        std::vector<std::pair<std::string, std::string>> attrs;
        std::vector<std::string> out;
        if (r && luxfind::desired(r->name, rel, &attrs, nullptr, nullptr))
            for (const auto& kv : attrs) if (kv.first.compare(0, 5, "user.") == 0) out.push_back(kv.first);
        return out;
    }
    bool set(const std::string& path, const std::string& xattr, const std::string& value, std::string* err) override
    {
        std::string rel;
        const luxfind::Root* r = root_of(path, &rel);
        if (!r) { *err = "No catalogue holds " + path + "."; return false; }
        bool ok = luxfind::defer_set(r->name, rel, xattr, value, err);
        trace("deferred %s %s %s", r->name.c_str(), rel.c_str(), ok ? "ok" : err->c_str());
        return ok;
    }
    bool tags(const std::string& path, const std::vector<std::string>& add, const std::vector<std::string>& remove,
              std::string* err) override
    {
        std::string rel;
        const luxfind::Root* r = root_of(path, &rel);
        if (!r) { *err = "No catalogue holds " + path + "."; return false; }
        bool ok = luxfind::defer_tags(r->name, rel, add, remove, err);
        trace("deferred %s %s %s", r->name.c_str(), rel.c_str(), ok ? "ok" : err->c_str());
        return ok;
    }
} g_route;

// the rows of these paths again from their catalogues (after an edit)
void refresh_rows(const std::vector<std::string>& paths)
{
    std::set<std::string> want(paths.begin(), paths.end());
    for (size_t i = 0; i < g.b.items.size(); i++) {
        Item& it = g.b.items[i];
        if (!want.count(it.path)) continue;
        std::string rel;
        const luxfind::Root* r = root_of(it.path, &rel);
        std::vector<std::pair<std::string, std::string>> attrs;
        int p = 0, k = 0;
        if (r && luxfind::desired(r->name, rel, &attrs, &p, &k)) set_attrs(it, attrs, p, k);
        browser::update_item(g.b, i);
    }
    g.b.all_items = g.b.items;
    count_pending();
}

// ---- the verbs' host ------------------------------------------------------------------------------------------------------------

struct Host : verbs::Host {
    HWND window() override { return g.wnd; }
    std::string current_dir() override { return ""; }
    Item folder_item() override { return Item(); }
    void open_folder(const Item& it, bool) override
    {
        std::string err;
        if (!shell::spawn({ shell::exe_dir() + "/flxp", it.path }, "", &err) && !shell::spawn({ "flxp", it.path }, "", &err))
            shell::message_box(g.wnd, "luxfind", "The folder could not be opened: " + err, MB_OK | MB_ICONEXCLAMATION);
        trace("opened folder %s", it.path.c_str());
    }
    void changed(const std::vector<std::string>&, const std::string&, bool) override {}
    void begin_rename() override {}
    void stored_changed(const std::vector<std::string>& paths) override { refresh_rows(paths); }
} g_host;

// the roots of these items that cannot be reached now, as a sentence ("" all reachable)
std::string unreachable(const std::vector<Item>& items)
{
    std::set<std::string> seen;
    std::string out;
    for (const Item& it : items) {
        const luxfind::Root* r = root_of(it.path);
        if (!r || seen.count(r->name)) continue;
        seen.insert(r->name);
        std::string why;
        luxfind::Reach::State s = reach_of(*r, &why);
        if (s == luxfind::Reach::Reachable) continue;
        out += (out.empty() ? "" : "\n") + r->name + " (" + r->path + ") " +
               (s == luxfind::Reach::Checking ? "is being checked; try again in a moment." : "is not reachable: " + why + ".");
    }
    return out;
}

// a verb on the selection: what needs the files waits for them to be reachable; the edits go through the route
void run_verb(int id, const std::vector<Item>& items)
{
    if (items.empty()) return;
    bool offline_ok = id == ID_ADD_TAG || id == ID_COPY_PATH;
    if (!offline_ok) {
        std::string why = unreachable(items);
        if (!why.empty()) {
            std::string what = id == ID_PROPERTIES || id == ID_METADATA
                ? "The Properties sheet needs the files themselves. Add Tag works without them: the tags wait in the catalogue."
                : "This needs the files themselves.";
            shell::message_box(g.wnd, "luxfind", why + "\n\n" + what, MB_OK | MB_ICONEXCLAMATION);
            return;
        }
    }
    if (id == ID_LF_OPENFOLDER) {
        std::string err;
        if (!shell::spawn({ shell::exe_dir() + "/flxp", "--select", items[0].path }, "", &err) &&
            !shell::spawn({ "flxp", "--select", items[0].path }, "", &err))
            shell::message_box(g.wnd, "luxfind", "The folder could not be opened: " + err, MB_OK | MB_ICONEXCLAMATION);
        trace("opened containing %s", items[0].path.c_str());
        return;
    }
    verbs::g_ctx = verbs::Ctx();
    verbs::g_ctx.items = items;
    if (id == ID_OPEN) trace("opened %s", items[0].path.c_str());
    verbs::run(id);
    if (id == ID_ADD_TAG || id == ID_PROPERTIES || id == ID_METADATA) {
        std::vector<std::string> paths;
        for (const Item& it : items) paths.push_back(it.path);
        refresh_rows(paths);
    }
}

// Enter, a double click: open, once the roots have answered
void open_selection()
{
    std::vector<Item> sel = browser::selection(g.b);
    if (sel.empty()) return;
    for (const Item& it : sel)
        if (const luxfind::Root* r = root_of(it.path))
            if (reach_of(*r) == luxfind::Reach::Checking) { g.open_waiting = r->name; update_status(); return; }
    run_verb(ID_OPEN, sel);
}

// Tag Selected: the Tags box's tags added to the selected results
void tag_selected()
{
    std::vector<std::string> tags = shell::details::split_list(text_of(g.tags));
    std::vector<Item> sel = browser::selection(g.b);
    if (tags.empty() || sel.empty()) { MessageBeep(0); return; }
    std::string err;
    std::vector<std::string> paths;
    for (const Item& it : sel) {
        std::string e;
        if (!shell::details::change_tags(it.path, tags, {}, &e)) { if (err.find(e) == std::string::npos) err += (err.empty() ? "" : "\n") + e; }
        paths.push_back(it.path);
    }
    shell::details::remember_tags(tags);
    trace("tagged %zu", sel.size());
    refresh_rows(paths);
    if (!err.empty()) shell::message_box(g.wnd, "luxfind", err, MB_OK | MB_ICONEXCLAMATION);
}

// ---- commits ------------------------------------------------------------------------------------------------------------------------

// (the commit's thread: the result goes to the UI thread in the message)
void committed(void*, const std::string& root, bool ok, const std::string& text)
{
    PostMessageW(g.wnd, WM_LF_COMMITTED, 0, (LPARAM)new Committed{ root, text, ok });
}

void commit_root(const std::string& root)
{
    const luxfind::Root* r = root_named(root);
    if (!r) return;
    std::string why;
    luxfind::Reach::State s = reach_of(*r, &why, true);
    if (s == luxfind::Reach::Checking) {   // asked again now: wait for the answer (short), never the UI
        for (int i = 0; i < 40 && s == luxfind::Reach::Checking; i++) { Sleep(50); s = reach_of(*r, &why); }
    }
    if (s != luxfind::Reach::Reachable) {
        trace("commit %s unreachable", root.c_str());
        shell::message_box(g.wnd, "luxfind", root + " (" + r->path + ") is not reachable" + (why.empty() ? "" : ": " + why) +
                           ".\n\nThe edits stay in the catalogue until it is.", MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    SendMessageW(g.status, SB_SETTEXTW, 1, (LPARAM)Wide("Writing the edits to " + root + "...").c_str());
    EnableWindow(g.commit, FALSE);
    luxfind::commit(root, committed, nullptr);
}

void take_committed(Committed* one)
{
    std::vector<Committed> v = { *one };
    delete one;
    EnableWindow(g.commit, TRUE);
    std::vector<std::string> paths;
    for (const Item& it : g.b.items) if (it.flags & (shell::HasEdits | shell::HasConflict)) paths.push_back(it.path);
    for (const Committed& c : v) {
        trace("commit %s %s %s", c.root.c_str(), c.ok ? "ok" : "fail", c.text.c_str());
        // the rows now carry what the file has: the reachable files are read as usual again
        for (Item& it : g.b.items) if (const luxfind::Root* r = root_of(it.path)) if (r->name == c.root) it.flags &= ~(uint32_t)shell::IsPending;
        if (!c.ok || !getenv("LUXFIND_TEST")) shell::message_box(g.wnd, "luxfind", c.text, MB_OK | (c.ok ? MB_ICONINFORMATION : MB_ICONEXCLAMATION));
    }
    refresh_rows(paths);
}

// ---- menus ----------------------------------------------------------------------------------------------------------------------------

void context_menu(int x, int y, bool keyboard)
{
    std::vector<Item> sel = browser::selection(g.b);
    if (sel.empty()) return;
    if (keyboard) {
        int f = (int)SendMessageW(g.b.list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED | LVNI_FOCUSED);
        RECT r = {}; r.left = LVIR_ICON;
        POINT pt = { 4, 4 };
        if (f >= 0 && SendMessageW(g.b.list, LVM_GETITEMRECT, (WPARAM)f, (LPARAM)&r)) { pt.x = (r.left + r.right) / 2; pt.y = (r.top + r.bottom) / 2; }
        ClientToScreen(g.b.list, &pt);
        x = pt.x; y = pt.y;
    }
    verbs::g_ctx = verbs::Ctx();
    verbs::g_ctx.items = sel;
    HMENU m = CreatePopupMenu();
    verbs::build_item_menu(m);
    // XP's search results add Open Containing Folder after Open
    int at = 0, n = GetMenuItemCount(m);
    for (int i = 0; i < n; i++) if (GetMenuItemID(m, (UINT)i) == ID_OPEN) at = i + 1;
    InsertMenuW(m, (UINT)at, MF_BYPOSITION | MF_STRING | (sel.size() == 1 ? 0 : MF_GRAYED), ID_LF_OPENFOLDER, L"Open Containing &Folder");
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, x, y, 0, g.wnd, nullptr);
    DestroyMenu(m);
    if (cmd) run_verb(cmd, sel);
}

HMENU make_menu()
{
    HMENU bar = CreateMenu(), file = CreatePopupMenu(), edit = CreatePopupMenu(), view = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, ID_OPEN, L"&Open\tEnter");
    AppendMenuW(file, MF_STRING, ID_LF_OPENFOLDER, L"Open Containing &Folder");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ID_ADD_TAG, L"Add Ta&g...");
    AppendMenuW(file, MF_STRING, ID_PROPERTIES, L"P&roperties\tAlt+Enter");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_POPUP, (UINT_PTR)CreatePopupMenu(), L"Co&mmit Pending Edits");   // filled when opened
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ID_LF_CLOSE, L"&Close");
    AppendMenuW(edit, MF_STRING, ID_LF_SELECTALL, L"Select &All\tCtrl+A");
    AppendMenuW(edit, MF_STRING, ID_LF_FOCUSNAME, L"&Find\tCtrl+F");
    AppendMenuW(view, MF_STRING, ID_LF_VIEW_THUMBS, L"T&humbnails");
    AppendMenuW(view, MF_STRING, ID_LF_VIEW_DETAILS, L"&Details");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)edit, L"&Edit");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)view, L"&View");
    return bar;
}

void init_menu(HMENU m)
{
    bool sel = !browser::selection(g.b).empty();
    for (UINT id : { (UINT)ID_OPEN, (UINT)ID_LF_OPENFOLDER, (UINT)ID_ADD_TAG, (UINT)ID_PROPERTIES })
        EnableMenuItem(m, id, MF_BYCOMMAND | (sel ? MF_ENABLED : MF_GRAYED));
    int n = GetMenuItemCount(m);
    for (int i = 0; i < n; i++) {
        HMENU sub = GetSubMenu(m, i);
        if (!sub || GetMenuItemID(m, (UINT)i) != (UINT)-1) continue;
        MENUITEMINFOW mi = {};
        mi.cbSize = sizeof mi;
        mi.fMask = MIIM_STRING;
        WCHAR buf[64] = {};
        mi.dwTypeData = buf;
        mi.cch = 63;
        GetMenuItemInfoW(m, (UINT)i, TRUE, &mi);
        if (WuxUtf8(buf).find("Commit") == std::string::npos) continue;
        while (GetMenuItemCount(sub) > 0) DeleteMenu(sub, 0, MF_BYPOSITION);
        int k = 0;
        for (const luxfind::Root& r : g.roots) {
            int64_t p = g.pending[r.name];
            std::string t = r.name + (p > 0 ? " (" + thousands(p) + (p == 1 ? " edit)" : " edits)") : " (nothing pending)");
            AppendMenuW(sub, MF_STRING | (p > 0 ? 0 : MF_GRAYED), (UINT_PTR)(ID_LF_COMMIT_FIRST + k++), (LPCWSTR)Wide(t).c_str());
        }
    }
    CheckMenuRadioItem(m, ID_LF_VIEW_DETAILS, ID_LF_VIEW_THUMBS, g.b.view == LVS_REPORT ? ID_LF_VIEW_DETAILS : ID_LF_VIEW_THUMBS, MF_BYCOMMAND);
}

// ---- layout -------------------------------------------------------------------------------------------------------------------------------

void layout()
{
    RECT rc;
    GetClientRect(g.wnd, &rc);
    SendMessageW(g.status, WM_SIZE, 0, 0);
    RECT sr;
    GetWindowRect(g.status, &sr);
    int sh = sr.bottom - sr.top, h = rc.bottom - sh;
    int parts[3] = { std::max(200, (int)rc.right * 2 / 5), std::max(320, (int)rc.right * 3 / 5), -1 };
    SendMessageW(g.status, SB_SETPARTS, 3, (LPARAM)parts);
    MoveWindow(g.pane, 0, 0, PANE_W, h, TRUE);
    MoveWindow(g.b.list, PANE_W, 0, std::max(0, (int)rc.right - PANE_W), h, TRUE);
    // the pane: a label, its box under it (XP's Search Companion: 8 px margins)
    int x = 10, w = PANE_W - 20, y = 10;
    HWND boxes[] = { g.name, g.tags, g.type, g.lookin };
    for (size_t i = 0; i < 4; i++) {
        MoveWindow(g.labels[i], x, y, w, 16, TRUE);
        y += 17;
        MoveWindow(boxes[i], x, y, w, i < 1 ? 22 : 200, TRUE);
        y += 30;
    }
    MoveWindow(g.browse, x + w - 80, y - 4, 80, 23, TRUE);
    MoveWindow(g.tagsel, x, y - 4, 100, 23, TRUE);
    y += 30;
    MoveWindow(g.commit, x, y, w, 23, TRUE);
}

HWND label(HWND parent, const char* text)
{
    HWND h = CreateWindowExW(0, L"STATIC", (LPCWSTR)Wide(text).c_str(), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, parent, nullptr, nullptr, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)g.font, TRUE);
    g.labels.push_back(h);
    return h;
}

HWND control(HWND parent, const wchar_t* cls, DWORD style, int id, DWORD ex = 0)
{
    HWND h = CreateWindowExW(ex, (LPCWSTR)cls, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, 0, 0, 0, 0, parent,
                             (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)g.font, TRUE);
    return h;
}

LRESULT CALLBACK pane_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND:
    case WM_NOTIFY:
        return SendMessageW(g.wnd, msg, wp, lp);
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(h, &rc);
        FillRect((HDC)wp, &rc, GetSysColorBrush(COLOR_BTNFACE));
        return 1;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void create_children(HWND wnd)
{
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);
    WNDCLASSW pc = {};
    pc.lpfnWndProc = pane_proc;
    pc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    pc.lpszClassName = L"luxfind_pane";
    RegisterClassW(&pc);
    g.pane = CreateWindowExW(WS_EX_CONTROLPARENT, L"luxfind_pane", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0,
                             wnd, nullptr, nullptr, nullptr);
    label(g.pane, "All or part of the file &name:");
    g.name = control(g.pane, L"EDIT", ES_AUTOHSCROLL, IDC_NAME, WS_EX_CLIENTEDGE);
    shell::one_line_edit(g.name);
    label(g.pane, "&Tags (all of them):");
    g.tags = control(g.pane, L"COMBOBOX", CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL, IDC_TAGS);
    label(g.pane, "T&ype:");
    g.type = control(g.pane, L"COMBOBOX", CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL, IDC_TYPE);
    for (const TypeChoice& t : TYPES) SendMessageW(g.type, CB_ADDSTRING, 0, (LPARAM)Wide(t.label).c_str());
    SendMessageW(g.type, CB_SETCURSEL, 0, 0);
    label(g.pane, "&Look in:");
    g.lookin = control(g.pane, L"COMBOBOX", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_LOOKIN);
    g.browse = control(g.pane, L"BUTTON", BS_PUSHBUTTON, IDC_BROWSE);
    SetWindowTextW(g.browse, L"B&rowse...");
    g.tagsel = control(g.pane, L"BUTTON", BS_PUSHBUTTON, IDC_TAGSEL);
    SetWindowTextW(g.tagsel, L"T&ag Selected");
    g.commit = control(g.pane, L"BUTTON", BS_PUSHBUTTON, IDC_COMMIT);
    ShowWindow(g.commit, SW_HIDE);

    uiicons::init();
    g.b.small = uiicons::small();
    g.b.large = uiicons::large();
    g.b.remember_views = false;
    g.b.infotips = true;
    // the columns: XP's Search Results (Name, In Folder, Size, Type, Date Modified), then Tags, Description and Pending
    shell::columns::Layout l = shell::columns::builtin();
    for (shell::columns::Column& c : l)
        if (c.id == (int)shell::SortBy::Tags || c.id == (int)shell::SortBy::Description) c.shown = true;
    l.insert(l.begin() + 1, shell::columns::Column{ (int)shell::SortBy::Folder, true, 180 });
    l.push_back(shell::columns::Column{ (int)shell::SortBy::Pending, true, 80 });
    std::vector<shell::columns::Column> order;
    for (int id : { 0, (int)shell::SortBy::Folder, 1, 2, 3, (int)shell::SortBy::Tags, (int)shell::SortBy::Description, (int)shell::SortBy::Pending })
        for (const shell::columns::Column& c : l) if (c.id == id) order.push_back(c);
    for (const shell::columns::Column& c : l) if (std::find(order.begin(), order.end(), c) == order.end()) order.push_back(c);
    g.b.column_layout = g.b.column_default = order;
    static const int widths[4] = { 200, 80, 120, 120 };
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SHAREIMAGELISTS | LVS_SHOWSELALWAYS;
    browser::create_list(g.b, wnd, IDC_LIST, style, WS_EX_CLIENTEDGE, widths, g.font);
    SendMessageW(g.b.list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_LABELTIP | LVS_EX_FULLROWSELECT, LVS_EX_LABELTIP | LVS_EX_FULLROWSELECT);
    thumbview::init(g.b.list, wnd);
    g.status = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, wnd,
                               (HMENU)(INT_PTR)IDC_STATUS, nullptr, nullptr);
}

void set_view(bool thumbs)
{
    browser::set_view(g.b, thumbs ? VIEW_THUMBNAILS : LVS_REPORT);
    browser::fill_list(g.b);
}

LRESULT on_notify(NMHDR* nm)
{
    if (nm->idFrom != IDC_LIST) return 0;
    switch (nm->code) {
    case NM_DBLCLK: open_selection(); return 0;
    case NM_RETURN: open_selection(); return 0;
    case LVN_COLUMNCLICK:
        browser::sort_by_column(g.b, ((NMLISTVIEW*)nm)->iSubItem);
        g.sorted = true;
        return 0;
    case LVN_GETDISPINFOW: browser::subitem_image(g.b, (NMLVDISPINFOW*)nm); return 0;
    case LVN_KEYDOWN: {
        WORD vk = ((NMLVKEYDOWN*)nm)->wVKey;
        if (vk == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000)) run_verb(ID_PROPERTIES, browser::selection(g.b));
        else if (vk == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            LVITEMW st = {}; st.stateMask = LVIS_SELECTED; st.state = LVIS_SELECTED;
            SendMessageW(g.b.list, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&st);
        } else if (vk == VK_APPS || (vk == VK_F10 && (GetKeyState(VK_SHIFT) & 0x8000))) context_menu(0, 0, true);
        return 0;
    }
    }
    return 0;
}

LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE: if (g.status) layout(); return 0;
    case WM_SETFOCUS: SetFocus(g.name); return 0;
    case WM_INITMENUPOPUP: init_menu((HMENU)wp); return 0;
    case WM_CONTEXTMENU:
        if ((HWND)wp == g.b.list) {
            int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
            context_menu(x, y, x == -1 && y == -1);
            return 0;
        }
        break;
    case WM_NOTIFY: return on_notify((NMHDR*)lp);
    case WM_TIMER:
        if (wp == TIMER_QUERY) run_query();
        else if (wp == TIMER_MORE) maybe_more();
        else if (wp == TIMER_REACH) update_status();
        return 0;
    case WM_LF_RESULTS: take_results(); return 0;
    case WM_LF_REACH:
        update_status();
        if (!g.open_waiting.empty()) {
            const luxfind::Root* r = root_named(g.open_waiting);
            if (!r || reach_of(*r) != luxfind::Reach::Checking) { g.open_waiting.clear(); open_selection(); }
        }
        for (Item& it : g.b.items)   // a root that became reachable: its files are read as usual (thumbnails)
            if ((it.flags & shell::IsPending)) if (const luxfind::Root* r = root_of(it.path)) if (reach_of(*r) == luxfind::Reach::Reachable)
                it.flags &= ~(uint32_t)shell::IsPending;
        return 0;
    case WM_LF_COMMITTED: take_committed((Committed*)lp); return 0;
    case thumbview::WM_THUMBS: thumbview::on_ready(); return 0;
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id == IDC_NAME && code == EN_CHANGE) { query_soon(); return 0; }
        if (id == IDC_TAGS && code == CBN_SELCHANGE) {   // (the edit shows the chosen tag only after this)
            int sel = (int)SendMessageW(g.tags, CB_GETCURSEL, 0, 0);
            WCHAR buf[512] = {};
            if (sel >= 0 && SendMessageW(g.tags, CB_GETLBTEXTLEN, (WPARAM)sel, 0) < 511) SendMessageW(g.tags, CB_GETLBTEXT, (WPARAM)sel, (LPARAM)buf);
            g.applied_tags = WuxUtf8(buf);
            run_query();
            return 0;
        }
        if (id == IDOK && GetFocus() == g.b.list) { open_selection(); return 0; }   // Enter on the results
        if (id == IDOK) {   // Enter in a box of the pane: the query now, the Tags box's text applied
            g.applied_tags = shell::trim(text_of(g.tags));
            run_query();
            return 0;
        }
        if (id == IDC_TYPE && (code == CBN_EDITCHANGE || code == CBN_SELCHANGE)) {
            if (code == CBN_SELCHANGE) PostMessageW(h, WM_TIMER, TIMER_QUERY, 0); else query_soon();
            return 0;
        }
        if (id == IDC_LOOKIN && code == CBN_SELCHANGE) { run_query(); return 0; }
        if (id == IDC_BROWSE) { browse(); return 0; }
        if (id == IDC_TAGSEL) { tag_selected(); return 0; }
        if (id == IDC_COMMIT) {
            for (auto& kv : g.pending) if (kv.second > 0) { commit_root(kv.first); break; }
            return 0;
        }
        if (id >= ID_LF_COMMIT_FIRST && id < ID_LF_COMMIT_FIRST + (int)g.roots.size()) { commit_root(g.roots[(size_t)(id - ID_LF_COMMIT_FIRST)].name); return 0; }
        if (id == ID_LF_VIEW_DETAILS || id == ID_LF_VIEW_THUMBS) { set_view(id == ID_LF_VIEW_THUMBS); return 0; }
        if (id == ID_LF_CLOSE) { DestroyWindow(h); return 0; }
        if (id == ID_LF_FOCUSNAME) { SetFocus(g.name); SendMessageW(g.name, EM_SETSEL, 0, -1); return 0; }
        if (id == ID_LF_FOCUSTAGS || id == ID_LF_FOCUSTYPE) {
            HWND c = id == ID_LF_FOCUSTAGS ? g.tags : g.type;
            SetFocus(c);
            SendMessageW(c, CB_SETEDITSEL, 0, MAKELPARAM(0, -1));
            return 0;
        }
        if (id == ID_LF_FOCUSLOOKIN) { SetFocus(g.lookin); return 0; }
        if (id == ID_LF_SELECTALL) {
            LVITEMW st = {}; st.stateMask = LVIS_SELECTED; st.state = LVIS_SELECTED;
            SendMessageW(g.b.list, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&st);
            SetFocus(g.b.list);
            return 0;
        }
        if (id == ID_OPEN) { open_selection(); return 0; }
        if (id == ID_LF_OPENFOLDER || id == ID_ADD_TAG || id == ID_PROPERTIES) { run_verb(id, browser::selection(g.b)); return 0; }
        break;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void usage(FILE* f)
{
    fprintf(f, "usage: luxfind [--root NAME] [--name PART] [--tag TAGS] [--type TYPE] [DIR]\n"
               "  the query window over the catalogues (docs/CATALOGUE.md); DIR: Look in that folder (under a root)\n");
}

} // namespace

int main(int argc, char** argv)
{
    std::string root, name, tag, type, dir;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--help" || a == "-h") { usage(stdout); return 0; }
        if ((a == "--root" || a == "--name" || a == "--tag" || a == "--type") && i + 1 < argc) {
            (a == "--root" ? root : a == "--name" ? name : a == "--tag" ? tag : type) = argv[++i];
            continue;
        }
        if (!a.empty() && a[0] == '-') { fprintf(stderr, "luxfind: unknown option %s\n", a.c_str()); usage(stderr); return 2; }
        dir = a;
    }
    g.trace = getenv("LUXFIND_TRACE") && *getenv("LUXFIND_TRACE") == '1';
    WuxInit();
    g.roots = luxfind::roots();
    g.reach = new luxfind::Reach(reach_notify, nullptr);
    g.search = new luxfind::Search(results_notify, nullptr);
    shell::details::set_route(&g_route);
    verbs::set_host(&g_host);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = wnd_proc;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"luxfind";
    RegisterClassW(&wc);
    g.wnd = CreateWindowExW(WS_EX_CONTROLPARENT, L"luxfind", L"Search Results", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            1000, 640, nullptr, make_menu(), nullptr, nullptr);
    if (!g.wnd) { fprintf(stderr, "luxfind: no window\n"); return 1; }
    shell::set_window_icon_key(g.wnd, "search");
    create_children(g.wnd);
    g.looks.push_back(G::Look{ "", "", "Every root" });
    for (const luxfind::Root& r : g.roots) g.looks.push_back(G::Look{ r.name, "", r.name + " (" + r.path + ")" });
    fill_lookin(0);
    if (!root.empty()) for (size_t i = 0; i < g.looks.size(); i++) if (g.looks[i].root == root && g.looks[i].in.empty()) fill_lookin((int)i);
    if (!dir.empty() && !look_in_folder(dir)) fprintf(stderr, "luxfind: no catalogue holds %s; looking in every root\n", dir.c_str());
    for (const std::string& t : luxfind::catalogue_tags(g.roots, 200)) SendMessageW(g.tags, CB_ADDSTRING, 0, (LPARAM)Wide(t).c_str());
    SetWindowTextW(g.name, (LPCWSTR)Wide(name).c_str());
    SetWindowTextW(g.tags, (LPCWSTR)Wide(tag).c_str());
    g.applied_tags = tag;
    if (!type.empty()) SetWindowTextW(g.type, (LPCWSTR)Wide(type).c_str());
    for (const luxfind::Root& r : g.roots) reach_of(r);   // the checks start
    count_pending();
    layout();
    ShowWindow(g.wnd, SW_SHOWNORMAL);
    UpdateWindow(g.wnd);
    SetFocus(g.name);
    trace("mapped");
    run_query();
    SetTimer(g.wnd, TIMER_MORE, 200, nullptr);
    SetTimer(g.wnd, TIMER_REACH, 5000, nullptr);

    // the pane's underlined letters as accelerators too (they reach the pane from the results as well)
    ACCEL acc[] = { { FVIRTKEY | FCONTROL, 'F', ID_LF_FOCUSNAME }, { FVIRTKEY | FCONTROL, 'W', ID_LF_CLOSE },
                    { FVIRTKEY | FALT, 'N', ID_LF_FOCUSNAME }, { FVIRTKEY | FALT, 'T', ID_LF_FOCUSTAGS },
                    { FVIRTKEY | FALT, 'Y', ID_LF_FOCUSTYPE }, { FVIRTKEY | FALT, 'L', ID_LF_FOCUSLOOKIN },
                    { FVIRTKEY | FALT, 'A', IDC_TAGSEL }, { FVIRTKEY | FALT, 'C', IDC_COMMIT }, { FVIRTKEY | FALT, 'R', IDC_BROWSE } };
    HACCEL ha = CreateAcceleratorTableW(acc, (int)(sizeof acc / sizeof acc[0]));
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (TranslateAcceleratorW(g.wnd, ha, &m)) {
            // (wux's DefWindowProc takes the Alt key's release for the menu bar unless it saw the other key go down)
            if (m.message == WM_SYSKEYDOWN) DefWindowProcW(m.hwnd, m.message, m.wParam, m.lParam);
            continue;
        }
        if (IsDialogMessageW(g.wnd, &m)) continue;   // Tab, the pane's mnemonics, Enter as IDOK
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    delete g.search;   // its threads end at their next row
    g.search = nullptr;
    shell::details::set_route(nullptr);
    luxfind::close_handles();
    trace("exit");
    return 0;
}
