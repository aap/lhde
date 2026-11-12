// luxattr: the attributes editor (docs/USAGE.md "Descriptions and tags"), an application on the public entry point
// (wux/wux.h, shell/shell.h, apps/app.mk; docs/APPLICATIONS.md) that needs no lhde session.
//
//   luxattr [--general] FILE...
//
// One property sheet for the files given (a directory is a file too), XP's file Properties: a General page (icon, name,
// type, location, size, dates; several files: "N Files, M Folders", the type they share, the folder they share, the
// total size) and a Summary page (XP's Summary tab, which it opens on) with one edit per stored property: the registry's
// (Description, Tags, Comment: shell::details) and luxfs's own names that docs/ANNOTATIONS.md lists (Author, Source,
// Build, Notes, To do). Advanced >> switches to the list of every user.* attribute with its raw value, where a row is
// edited in place (F2 or a double click), added (New Attribute...) or removed. Several files: a value is shown when they
// all agree, else the edit is empty with "(several values)" in grey, and what is typed goes to every file. OK writes,
// Cancel drops, Apply writes and stays; an emptied value removes the attribute. Every value is written by
// shell::details::write, as the Explorer writes it (a list split and joined with commas, a text trimmed); errors in a
// message box. Exit status: 0 something was written, 1 nothing, 2 bad usage or a file that is not there.
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include "wux/wux.h"
#include "shell/shell.h"
#include "resource.h"

WUX_RESOURCES(luxattr_res, "apps/luxattr/luxattr.res");

namespace {

namespace details = shell::details;

// ---- the model -------------------------------------------------------------------------

// One attribute over the files: the value when they all agree (a file without it counts as ""), else several.
struct Attr {
    std::string value;
    bool agree = true, failed = false;
    int have = 0;                      // files that have it
    std::string error;                 // the first read error
};

// The Summary page's fields: the registry's stored properties, then luxfs's names (docs/ANNOTATIONS.md) that the
// registry does not carry (flxp does not show them), as Text.
const int NFIELDS = 8;
std::vector<details::Property> fields()
{
    std::vector<details::Property> out;
    for (const details::Property& p : details::registry())
        if (!p.xattr.empty() && p.editable) out.push_back(p);
    static const char* const lux[][3] = {
        { "author", "Author", "user.lux.author" }, { "source", "Source", "user.lux.source" }, { "build", "Build", "user.lux.build" },
        { "notes", "Notes", "user.lux.notes" }, { "todo", "To do", "user.lux.todo" },
    };
    for (auto& l : lux) {
        bool known = false;
        for (const details::Property& p : out) if (p.xattr == l[2]) known = true;
        if (!known) out.push_back({ l[0], l[1], details::ValueType::Text, l[2], true, "" });
    }
    if (out.size() > (size_t)NFIELDS) out.resize(NFIELDS);
    return out;
}

// the property an attribute is written as: the registry's (a list's format), else a text under its own name
details::Property property_of(const std::string& xattr)
{
    for (const details::Property& p : details::registry()) if (p.xattr == xattr && p.editable) return p;
    for (const details::Property& p : fields()) if (p.xattr == xattr) return p;
    return { xattr, xattr, details::ValueType::Text, xattr, true, "" };
}

struct State {
    std::vector<std::string> files;
    std::vector<details::Property> fields;
    std::map<std::string, Attr> attrs;          // every user.* name on any file, and the fields'
    std::vector<std::string> order;             // the list's rows: the fields' names first, then the others by name
    std::map<std::string, std::string> pending; // name -> the text to write ("" removes it)
    std::vector<std::string> rows;              // the list's rows as shown (name per index)
    bool advanced = false, filling = false, wrote = false;
    HWND summary = nullptr, edit = nullptr;     // the Summary page; the in-place editor over a row
    int edit_row = -1;
    HINSTANCE res = nullptr;
    HICON icon = nullptr;
    int start = 1;                              // the page shown first (Summary)
};
State g;

void load()
{
    g.attrs.clear();
    g.order.clear();
    std::vector<std::vector<std::string>> names(g.files.size());
    std::vector<std::string> all;
    for (const details::Property& p : g.fields) all.push_back(p.xattr);
    size_t nfields = all.size();
    for (size_t i = 0; i < g.files.size(); i++) {
        details::list_attrs(g.files[i], &names[i]);
        for (const std::string& n : names[i]) if (std::find(all.begin(), all.end(), n) == all.end()) all.push_back(n);
    }
    std::sort(all.begin() + (long)nfields, all.end());
    for (const std::string& n : all) {
        Attr a;
        for (size_t i = 0; i < g.files.size(); i++) {
            std::string v;
            int e = 0;
            details::Read r = details::read_attr(g.files[i], n, &v, &e);
            if (r == details::Read::Failed) {
                a.failed = true;
                if (a.error.empty()) a.error = strerror(e);
                continue;
            }
            if (r == details::Read::Value) a.have++;
            if (i == 0) a.value = v;
            else if (v != a.value) a.agree = false;
        }
        if (a.failed) a.agree = false;
        if (!a.agree) a.value.clear();
        g.attrs[n] = a;
        g.order.push_back(n);
    }
}

// a value as one line of the list: control characters as \n, \t, \xNN
std::string printable(const std::string& v)
{
    std::string out;
    for (unsigned char c : v) {
        if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20 || c == 0x7f) { char b[8]; snprintf(b, sizeof b, "\\x%02x", c); out += b; }
        else out += (char)c;
    }
    return out;
}

// ---- small things ------------------------------------------------------------------------

void set_text(HWND dlg, int id, const std::string& s) { SetDlgItemTextW(dlg, id, (LPCWSTR)WuxWide(s).c_str()); }
std::string get_text(HWND dlg, int id) { return WuxWindowText(GetDlgItem(dlg, id)); }
void show(HWND dlg, int id, bool on) { ShowWindow(GetDlgItem(dlg, id), on ? SW_SHOW : SW_HIDE); }

std::string base_name(const std::string& p)
{
    size_t s = p.find_last_of('/');
    std::string b = s == std::string::npos ? p : p.substr(s + 1);
    return b.empty() ? p : b;
}

void error_box(HWND owner, const std::string& text)
{
    shell::message_box(owner, "Attributes", text, MB_OK | MB_ICONEXCLAMATION);
}

// ---- General -----------------------------------------------------------------------------
// flxp's Properties page (flxp/explorer/dialogs.cpp): XP's sizes and long dates

std::string commas(uint64_t n)
{
    std::string s = std::to_string(n);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert((size_t)i, ",");
    return s;
}

std::string size_text(uint64_t b)
{
    char buf[64];
    if (b < 1024) snprintf(buf, sizeof buf, "%llu bytes", (unsigned long long)b);
    else if (b < 1024 * 1024) snprintf(buf, sizeof buf, "%.1f KB (%s bytes)", b / 1024.0, commas(b).c_str());
    else if (b < 1024ULL * 1024 * 1024) snprintf(buf, sizeof buf, "%.1f MB (%s bytes)", b / 1048576.0, commas(b).c_str());
    else snprintf(buf, sizeof buf, "%.2f GB (%s bytes)", b / 1073741824.0, commas(b).c_str());
    return buf;
}

std::string long_date(int64_t t)
{
    // XP: "Friday, October 23, 2026, 1:45:12 PM"
    time_t tt = (time_t)t;
    struct tm tm;
    localtime_r(&tt, &tm);
    char day[48], buf[96];
    strftime(day, sizeof day, "%A, %B %e, %Y", &tm);
    std::string d = day;
    for (size_t k; (k = d.find("  ")) != std::string::npos;) d.erase(k, 1);
    int h = tm.tm_hour % 12;
    snprintf(buf, sizeof buf, "%s, %d:%02d:%02d %s", d.c_str(), h ? h : 12, tm.tm_min, tm.tm_sec, tm.tm_hour < 12 ? "AM" : "PM");
    return buf;
}

void init_general(HWND dlg)
{
    if (g.icon) SendDlgItemMessageW(dlg, IDC_GICON, STM_SETICON, (WPARAM)g.icon, 0);
    if (g.files.size() == 1) {
        const std::string& path = g.files[0];
        shell::FileInfo fi = shell::file_info(path, true);
        std::string name = base_name(path), mime = shell::mime_type(path);
        set_text(dlg, IDC_NAME, name);
        set_text(dlg, IDC_TYPE, shell::type_name(name, fi.is_dir) + " (" + mime + ")");
        if (fi.is_dir) {
            show(dlg, IDC_OPENSL, false);
            show(dlg, IDC_OPENS, false);
            set_text(dlg, IDC_CONTAINS, commas(fi.files) + " Files, " + commas(fi.folders) + " Folders");
        } else {
            const shell::App* a = shell::default_app(mime);
            set_text(dlg, IDC_OPENS, a ? a->name : "(none)");
            show(dlg, IDC_CONTAINSL, false);
        }
        set_text(dlg, IDC_LOCATION, shell::dir_of(path));
        if (fi.ok) {
            set_text(dlg, IDC_GSIZE, size_text(fi.size));
            set_text(dlg, IDC_MODIFIED, long_date(fi.mtime));
            set_text(dlg, IDC_ACCESSED, long_date(fi.atime));
            set_text(dlg, IDC_CHANGED, long_date(fi.ctime));
            set_text(dlg, IDC_PERMS, fi.perms + "   " + fi.owner + ":" + fi.group);
        }
        if (fi.is_link) { set_text(dlg, IDC_EXTRAL, "Link target:"); set_text(dlg, IDC_EXTRA, fi.link_target); }
        return;
    }
    // XP's page for several items: "N Files, M Folders", the type they share, where they are, the total size
    uint64_t files = 0, folders = 0, size = 0;
    std::string type, dir;
    bool one_type = true, one_dir = true;
    for (size_t i = 0; i < g.files.size(); i++) {
        shell::FileInfo fi = shell::file_info(g.files[i], true);
        (fi.is_dir ? folders : files)++;
        size += fi.size;
        std::string t = shell::type_name(base_name(g.files[i]), fi.is_dir), d = shell::dir_of(g.files[i]);
        if (i == 0) { type = t; dir = d; }
        if (t != type) one_type = false;
        if (d != dir) one_dir = false;
    }
    set_text(dlg, IDC_NAME, commas(files) + (files == 1 ? " File, " : " Files, ") + commas(folders) + (folders == 1 ? " Folder" : " Folders"));
    set_text(dlg, IDC_TYPEL, "Type:");
    set_text(dlg, IDC_TYPE, one_type ? "All of type " + type : "Multiple Types");
    set_text(dlg, IDC_LOCATION, one_dir ? "All in " + dir : "Various Folders");
    set_text(dlg, IDC_GSIZE, size_text(size));
    for (int id : { IDC_OPENSL, IDC_OPENS, IDC_CONTAINSL, IDC_CONTAINS, IDC_SEP3, IDC_MODIFIEDL, IDC_MODIFIED, IDC_ACCESSEDL,
                    IDC_ACCESSED, IDC_CHANGEDL, IDC_CHANGED, IDC_SEP4, IDC_PERMSL, IDC_PERMS })
        show(dlg, id, false);
}

INT_PTR CALLBACK general_proc(HWND dlg, UINT msg, WPARAM, LPARAM)
{
    if (msg == WM_INITDIALOG) { init_general(dlg); return TRUE; }
    return FALSE;
}

// ---- Summary -----------------------------------------------------------------------------

void changed() { if (!g.filling && g.summary) SendMessageW(GetParent(g.summary), PSM_CHANGED, (WPARAM)g.summary, 0); }

// the simple view's edits from the model and what is pending
void fill_fields()
{
    HWND dlg = g.summary;
    g.filling = true;
    for (int i = 0; i < NFIELDS; i++) {
        bool on = i < (int)g.fields.size();
        show(dlg, IDC_LABEL0 + i, on && !g.advanced);
        show(dlg, IDC_FIELD0 + i, on && !g.advanced);
        if (!on) continue;
        const details::Property& p = g.fields[(size_t)i];
        const Attr& a = g.attrs[p.xattr];
        HWND e = GetDlgItem(dlg, IDC_FIELD0 + i);
        auto pend = g.pending.find(p.xattr);
        std::string text = pend != g.pending.end() ? pend->second
                         : !a.agree ? std::string()
                         : p.type == details::ValueType::List ? details::tag_list(a.value) : a.value;
        SetWindowTextW(e, (LPCWSTR)WuxWide(text).c_str());
        std::string cue = a.failed ? "(could not be read: " + a.error + ")" : a.agree ? "" : "(several values)";
        SendMessageW(e, EM_SETCUEBANNER, TRUE, (LPARAM)WuxWide(cue).c_str());
        SendMessageW(e, EM_SETMODIFY, FALSE, 0);
    }
    g.filling = false;
}

// what was typed into the simple view becomes pending
void take_fields()
{
    for (int i = 0; i < NFIELDS && i < (int)g.fields.size(); i++) {
        HWND e = GetDlgItem(g.summary, IDC_FIELD0 + i);
        if (!SendMessageW(e, EM_GETMODIFY, 0, 0)) continue;
        g.pending[g.fields[(size_t)i].xattr] = WuxWindowText(e);
        SendMessageW(e, EM_SETMODIFY, FALSE, 0);
    }
}

std::string shown_value(const std::string& name)
{
    auto pend = g.pending.find(name);
    if (pend != g.pending.end()) return printable(pend->second);
    const Attr& a = g.attrs[name];
    if (a.failed) return "(could not be read: " + a.error + ")";
    return a.agree ? printable(a.value) : "(several values)";
}

void fill_list(const std::string& select = "")
{
    HWND list = GetDlgItem(g.summary, IDC_LIST);
    g.filling = true;
    SendMessageW(list, LVM_DELETEALLITEMS, 0, 0);
    g.rows.clear();
    for (const std::string& n : g.order) {
        auto pend = g.pending.find(n);
        bool there = pend != g.pending.end() ? !pend->second.empty() : g.attrs[n].have > 0 || g.attrs[n].failed;
        if (there) g.rows.push_back(n);
    }
    for (size_t i = 0; i < g.rows.size(); i++) {
        std::u16string n = WuxWide(g.rows[i]), v = WuxWide(shown_value(g.rows[i]));
        LVITEMW it = {};
        it.mask = LVIF_TEXT;
        it.iItem = (int)i;
        it.pszText = (LPWSTR)n.c_str();
        SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&it);
        it.iSubItem = 1;
        it.pszText = (LPWSTR)v.c_str();
        SendMessageW(list, LVM_SETITEMTEXTW, i, (LPARAM)&it);
        if (g.rows[i] == select) {
            LVITEMW st = {};
            st.stateMask = st.state = LVIS_SELECTED | LVIS_FOCUSED;
            SendMessageW(list, LVM_SETITEMSTATE, i, (LPARAM)&st);
            SendMessageW(list, LVM_ENSUREVISIBLE, i, FALSE);
        }
    }
    g.filling = false;
    EnableWindow(GetDlgItem(g.summary, IDC_REMOVE), !g.rows.empty());
}

void end_edit(bool commit);

void set_advanced(bool on)
{
    if (on) take_fields(); else end_edit(true);
    g.advanced = on;
    HWND dlg = g.summary;
    if (on) fill_list(); else fill_fields();
    for (int i = 0; i < NFIELDS; i++) {
        show(dlg, IDC_LABEL0 + i, !on && i < (int)g.fields.size());
        show(dlg, IDC_FIELD0 + i, !on && i < (int)g.fields.size());
    }
    show(dlg, IDC_NOTE, !on);
    show(dlg, IDC_ADVANCED, !on);
    for (int id : { IDC_LIST, IDC_NEW, IDC_REMOVE, IDC_SIMPLE }) show(dlg, id, on);
    SetFocus(GetDlgItem(dlg, on ? IDC_LIST : IDC_FIELD0));
}

// ---- the list's in-place editor (XP's Advanced view edits a value where it is shown)

WNDPROC g_edit_proc;
enum { WM_END_EDIT = WM_APP + 1 };

LRESULT CALLBACK edit_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS | CallWindowProcW(g_edit_proc, h, msg, wp, lp);
    case WM_KEYDOWN:
        if (wp == VK_RETURN || wp == VK_ESCAPE) { PostMessageW(g.summary, WM_END_EDIT, wp == VK_RETURN, 0); return 0; }
        break;
    case WM_CHAR:
        if (wp == '\r' || wp == 27) return 0;   // no beep
        break;
    case WM_KILLFOCUS: PostMessageW(g.summary, WM_END_EDIT, TRUE, 0); break;
    }
    return CallWindowProcW(g_edit_proc, h, msg, wp, lp);
}

void begin_edit(int row)
{
    if (row < 0 || row >= (int)g.rows.size()) return;
    end_edit(true);
    HWND list = GetDlgItem(g.summary, IDC_LIST);
    SendMessageW(list, LVM_ENSUREVISIBLE, row, FALSE);
    RECT r = {};
    r.top = 1;              // the value's column
    r.left = LVIR_BOUNDS;
    if (!SendMessageW(list, LVM_GETSUBITEMRECT, row, (LPARAM)&r)) return;
    const std::string& name = g.rows[(size_t)row];
    auto pend = g.pending.find(name);
    const Attr& a = g.attrs[name];
    std::string text = pend != g.pending.end() ? pend->second : a.agree ? a.value : "";
    g.edit = CreateWindowExW(0, L"Edit", (LPCWSTR)WuxWide(text).c_str(), WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                             r.left, r.top - 1, r.right - r.left, r.bottom - r.top + 2, list, nullptr, nullptr, nullptr);
    SendMessageW(g.edit, WM_SETFONT, SendMessageW(list, WM_GETFONT, 0, 0), FALSE);
    if (!a.agree) SendMessageW(g.edit, EM_SETCUEBANNER, TRUE, (LPARAM)u"(several values)");
    g_edit_proc = (WNDPROC)SetWindowLongPtrW(g.edit, GWLP_WNDPROC, (LONG_PTR)edit_proc);
    g.edit_row = row;
    ShowWindow(g.edit, SW_SHOW);
    SetFocus(g.edit);
    SendMessageW(g.edit, EM_SETSEL, 0, -1);
}

void end_edit(bool commit)
{
    if (!g.edit) return;
    HWND e = g.edit;
    g.edit = nullptr;
    std::string name = g.edit_row >= 0 && g.edit_row < (int)g.rows.size() ? g.rows[(size_t)g.edit_row] : "";
    bool modified = SendMessageW(e, EM_GETMODIFY, 0, 0) != 0;
    std::string text = WuxWindowText(e);
    bool had_focus = GetFocus() == e;
    DestroyWindow(e);
    if (commit && modified && !name.empty()) {
        g.pending[name] = text;
        changed();
        fill_list(name);
    }
    if (had_focus) SetFocus(GetDlgItem(g.summary, IDC_LIST));
}

int selected_row()
{
    return (int)SendDlgItemMessageW(g.summary, IDC_LIST, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
}

// ---- New Attribute

struct NewAttr { std::string name, value; };

INT_PTR CALLBACK new_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    static NewAttr* na;
    switch (msg) {
    case WM_INITDIALOG:
        na = (NewAttr*)lp;
        set_text(dlg, IDC_ATTRNAME, "user.");
        SetFocus(GetDlgItem(dlg, IDC_ATTRNAME));
        SendDlgItemMessageW(dlg, IDC_ATTRNAME, EM_SETSEL, 5, 5);
        return FALSE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            std::string n = shell::trim(get_text(dlg, IDC_ATTRNAME)), v = get_text(dlg, IDC_ATTRVALUE);
            if (n.compare(0, 5, "user.") != 0) n = "user." + n;   // only the user namespace is ours to write
            if (n.size() <= 5) { error_box(dlg, "Type the attribute's name."); SetFocus(GetDlgItem(dlg, IDC_ATTRNAME)); return TRUE; }
            if (n.size() > 255) { error_box(dlg, "The name is too long: an attribute's name has at most 255 bytes."); return TRUE; }
            if (shell::trim(v).empty()) { error_box(dlg, "Type a value: an empty one removes the attribute."); SetFocus(GetDlgItem(dlg, IDC_ATTRVALUE)); return TRUE; }
            na->name = n;
            na->value = v;
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) { EndDialog(dlg, IDCANCEL); return TRUE; }
        break;
    }
    return FALSE;
}

void new_attribute()
{
    end_edit(true);
    NewAttr na;
    if (DialogBoxParamW(g.res, MAKEINTRESOURCEW(IDD_NEWATTR), GetParent(g.summary), new_proc, (LPARAM)&na) != IDOK) return;
    if (!g.attrs.count(na.name)) { g.attrs[na.name] = Attr(); g.order.push_back(na.name); }
    g.pending[na.name] = na.value;
    changed();
    fill_list(na.name);
    SetFocus(GetDlgItem(g.summary, IDC_LIST));
}

void remove_selected()
{
    end_edit(true);
    HWND list = GetDlgItem(g.summary, IDC_LIST);
    int next = -1;
    bool any = false;
    for (int i = -1; (i = (int)SendMessageW(list, LVM_GETNEXTITEM, (WPARAM)i, LVNI_SELECTED)) >= 0;) {
        g.pending[g.rows[(size_t)i]] = "";
        next = i;
        any = true;
    }
    if (!any) { MessageBeep(0); return; }
    // the row after the last removed one keeps the selection
    std::string sel;
    for (size_t k = (size_t)next + 1; k < g.rows.size(); k++) {
        auto p = g.pending.find(g.rows[k]);
        if (p == g.pending.end() || !p->second.empty()) { sel = g.rows[k]; break; }
    }
    changed();
    fill_list(sel);
    SetFocus(list);
}

// Apply and OK: every pending value to every file, as the Explorer writes it. False after an error (the sheet stays).
bool apply()
{
    if (g.advanced) end_edit(true); else take_fields();
    std::string errors;
    for (const auto& kv : g.pending) {
        details::Property p = property_of(kv.first);
        for (const std::string& f : g.files) {
            std::string e;
            if (details::write(f, p, kv.second, &e)) { g.wrote = true; continue; }
            if (errors.find(e) == std::string::npos) errors += (errors.empty() ? "" : "\n") + e;
        }
    }
    g.pending.clear();
    load();
    if (g.advanced) fill_list(); else fill_fields();
    if (!errors.empty()) { error_box(GetParent(g.summary), errors); return false; }
    return true;
}

void init_summary(HWND dlg)
{
    g.summary = dlg;
    // the labels with their mnemonics, as the template has them for the defaults; another registry's own label
    static const char* const mnemonic[] = { "&Description:", "&Tags:", "Co&mment:", "A&uthor:", "&Source:", "&Build:", "&Notes:", "T&o do:" };
    for (int i = 0; i < NFIELDS && i < (int)g.fields.size(); i++) {
        std::string want = g.fields[(size_t)i].label + ":", m = mnemonic[i], plain;
        for (char c : m) if (c != '&') plain += c;
        set_text(dlg, IDC_LABEL0 + i, plain == want ? m : want);
    }
    HWND list = GetDlgItem(dlg, IDC_LIST);
    SendMessageW(list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);
    RECT rc;
    GetClientRect(list, &rc);
    int w = rc.right - rc.left - GetSystemMetrics(SM_CXVSCROLL);
    LVCOLUMNW c = {};
    c.mask = LVCF_TEXT | LVCF_WIDTH;
    c.pszText = (LPWSTR)u"Attribute";
    c.cx = w * 2 / 5;
    SendMessageW(list, LVM_INSERTCOLUMNW, 0, (LPARAM)&c);
    c.pszText = (LPWSTR)u"Value";
    c.cx = w - w * 2 / 5;
    SendMessageW(list, LVM_INSERTCOLUMNW, 1, (LPARAM)&c);
    fill_fields();
}

INT_PTR CALLBACK summary_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: init_summary(dlg); return TRUE;
    case WM_END_EDIT: end_edit(wp != 0); return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) >= IDC_FIELD0 && LOWORD(wp) < IDC_FIELD0 + NFIELDS && HIWORD(wp) == EN_CHANGE) { changed(); return TRUE; }
        switch (LOWORD(wp)) {
        case IDC_ADVANCED: set_advanced(true); return TRUE;
        case IDC_SIMPLE: set_advanced(false); return TRUE;
        case IDC_NEW: new_attribute(); return TRUE;
        case IDC_REMOVE: remove_selected(); return TRUE;
        }
        break;
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lp;
        if (nm->idFrom == IDC_LIST) {
            if (nm->code == NM_DBLCLK) { begin_edit(((NMITEMACTIVATE*)lp)->iItem); return TRUE; }
            if (nm->code == LVN_KEYDOWN) {
                WORD k = ((NMLVKEYDOWN*)lp)->wVKey;
                if (k == VK_F2) begin_edit(selected_row());
                else if (k == VK_DELETE) remove_selected();
                return TRUE;
            }
            if (nm->code == LVN_BEGINSCROLL) { end_edit(true); return TRUE; }
            return FALSE;
        }
        if (nm->code == PSN_APPLY) {
            SetWindowLongPtrW(dlg, DWLP_MSGRESULT, apply() ? PSNRET_NOERROR : PSNRET_INVALID_NOCHANGEPAGE);
            return TRUE;
        }
        if (nm->code == PSN_KILLACTIVE) { end_edit(true); SetWindowLongPtrW(dlg, DWLP_MSGRESULT, FALSE); return TRUE; }
        break;
    }
    }
    return FALSE;
}

void usage(FILE* f)
{
    fputs("usage: luxattr [--general] FILE...\n"
          "  The attributes of the files (user.* extended attributes) in a Properties sheet: the Summary page edits the\n"
          "  description (user.lux.desc), tags (user.xdg.tags), comment (user.xdg.comment) and luxfs's names, Advanced >>\n"
          "  every user.* attribute. --general opens it on the General page.\n"
          "Exit status: 0 something was written, 1 nothing, 2 usage error or a file that is not there.\n", f);
}

} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> args;
    bool opts = true;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (opts && a == "--") { opts = false; continue; }
        if (opts && (a == "--help" || a == "-h")) { usage(stdout); return 0; }
        if (opts && a == "--general") { g.start = 0; continue; }
        if (opts && a.size() > 1 && a[0] == '-') { fprintf(stderr, "luxattr: unknown option %s\n", a.c_str()); usage(stderr); return 2; }
        args.push_back(a);
    }
    if (args.empty()) { usage(stderr); return 2; }
    std::string cwd;
    { char buf[PATH_MAX]; if (getcwd(buf, sizeof buf)) cwd = buf; }
    for (std::string a : args) {
        struct stat st;
        if (lstat(a.c_str(), &st) != 0) { fprintf(stderr, "luxattr: %s: %s\n", a.c_str(), strerror(errno)); return 2; }
        if (a[0] != '/') a = cwd + "/" + a;
        while (a.size() > 1 && a.back() == '/') a.pop_back();
        if (std::find(g.files.begin(), g.files.end(), a) == g.files.end()) g.files.push_back(a);
    }

    WuxInit();
    g.res = WuxLoadResources(luxattr_res_start, (size_t)(luxattr_res_end - luxattr_res_start));
    g.fields = fields();
    load();
    bool dir0 = g.files.size() == 1 && shell::is_dir(g.files[0]);
    g.icon = shell::icon_for_key(g.files.size() == 1 ? shell::icon_key(base_name(g.files[0]), dir0) : "document", 32);

    PROPSHEETPAGEW pages[2] = {};
    pages[0].dwSize = pages[1].dwSize = sizeof pages[0];
    pages[0].dwFlags = pages[1].dwFlags = PSP_DEFAULT;
    pages[0].hInstance = pages[1].hInstance = g.res;
    pages[0].pszTemplate = MAKEINTRESOURCEW(IDD_GENERAL);
    pages[0].pfnDlgProc = general_proc;
    pages[1].pszTemplate = MAKEINTRESOURCEW(IDD_SUMMARY);
    pages[1].pfnDlgProc = summary_proc;
    // XP's caption: "NAME Properties"; several: the first names and "..."
    std::string caption = base_name(g.files[0]);
    if (g.files.size() > 1) caption += ", " + base_name(g.files[1]) + (g.files.size() > 2 ? ", ..." : "");
    std::u16string cap = WuxWide(caption);
    PROPSHEETHEADERW psh = {};
    psh.dwSize = sizeof psh;
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_PROPTITLE | PSH_NOCONTEXTHELP;
    psh.hInstance = g.res;
    psh.pszCaption = (LPCWSTR)cap.c_str();
    psh.nPages = 2;
    psh.nStartPage = (UINT)g.start;
    psh.ppsp = pages;
    PropertySheetW(&psh);
    if (g.icon) DestroyIcon(g.icon);
    return g.wrote ? 0 : 1;
}
