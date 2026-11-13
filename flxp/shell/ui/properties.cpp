// shell::properties (shell/ui.h): XP's file Properties sheet, one implementation for flxp's Properties verb and luxattr.
// Two pages of PropertySheetW (wux's comctl32) from the embedded flxp/rc/shell.res:
//   General   shell32's General page: icon, name, type, Opens with (+ the caller's Change...), location, size, contains,
//             dates, permissions, a link's target; several items as XP shows them
//   Metadata  docprop's Summary page in its two views: the simple one's fields for the registry's stored properties
//             (Description one line, Tags a list with a drop-down to add from, Color a row of swatches, Comment several
//             lines, Run as a command with Browse...); Advanced >> every user.* attribute in a report list, edited in place
// The model is the attributes of every file: a value shown when the files agree, typed values pending until Apply / OK
// write them with details::write (the Explorer's writer), an error in shell::message_box. Tags are kept per file: what
// is added goes to every file, what is removed leaves the files that have it, the others' tags stay (change_tags).
// The controls are wux's comctl32 ones: edits, a CBS_DROPDOWN combo box, an owner-drawn list box (a tag only some of the
// files have in grey), BS_OWNERDRAW buttons for the swatches, the report list view of the Advanced view.
#include "res/res.h"   // before the win32 headers: their macros clash with its names
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include "shell/apps.h"
#include "shell/config.h"
#include "shell/details.h"
#include "shell/fileops.h"
#include "shell/mime.h"
#include "shell/namespace.h"
#include "w32/w32.h"
extern "C" {
#include "commctrl.h"
#include "commdlg.h"
#include "prsht.h"
#include "uxtheme.h"
}
#include "shell/ui.h"
#include "resource.h"

asm(".section .rodata\n"
    ".global flxp_shell_res_start\n.global flxp_shell_res_end\n"
    "flxp_shell_res_start:\n.incbin \"flxp/rc/shell.res\"\nflxp_shell_res_end:\n"
    ".previous\n");
extern "C" const unsigned char flxp_shell_res_start[], flxp_shell_res_end[];

namespace shell {

namespace {

namespace details = shell::details;

HINSTANCE module()
{
    static res::Module* m;
    if (!m) m = res::load_memory(flxp_shell_res_start, (size_t)(flxp_shell_res_end - flxp_shell_res_start));
    return (HINSTANCE)m;
}

// ---- the model -------------------------------------------------------------------------

// One attribute over the files: the value when they all agree (a file without it counts as ""), else several.
struct Attr {
    std::string value;
    bool agree = true, failed = false;
    int have = 0;                      // files that have it
    std::string error;                 // the first read error
};

// The names the Advanced list puts first: the registry's stored properties, then luxfs's (docs/ANNOTATIONS.md), which the
// simple view does not show
std::vector<std::string> first_names()
{
    std::vector<std::string> out;
    for (const details::Property& p : details::registry())
        if (!p.xattr.empty() && p.editable) out.push_back(p.xattr);
    for (const char* n : { "user.lux.author", "user.lux.source", "user.lux.build", "user.lux.notes", "user.lux.todo" })
        if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
    return out;
}

const char* const TAGS = "user.xdg.tags";
const char* const COLOR = "user.lux.color";
// the simple view's text fields: the edit and the property it writes
const struct { int id; const char* prop; } TEXT_FIELDS[] = { { IDC_DESC, "description" }, { IDC_COMMENT, "comment" }, { IDC_RUN, "run" } };
const int NSWATCH = 11;   // None, then the palette

struct State {
    std::vector<std::string> files;
    const PropertiesOptions* opts = nullptr;
    std::string title;                          // the error boxes' caption: "NAME Properties"
    std::map<std::string, Attr> attrs;          // every user.* name on any file, and the first names
    std::vector<std::string> order;             // the list's rows: the first names, then the others by name
    std::map<std::string, std::string> pending; // name -> the text to write ("" removes it)
    std::vector<std::string> rows;              // the list's rows as shown (name per index)
    std::vector<std::vector<std::string>> file_tags;   // each file's tags as read
    std::vector<std::string> tag_add, tag_remove;      // the tags added to every file, removed from those that have them
    bool advanced = false, filling = false, wrote = false;
    HWND page = nullptr, edit = nullptr;        // the Metadata page; the in-place editor over a row
    int edit_row = -1;
    HICON icon = nullptr;
};
State* g;

// the property an attribute is written as: the registry's (a list's format, one line or several), else a text under its
// own name, kept as typed
details::Property property_of(const std::string& xattr)
{
    for (const details::Property& p : details::registry()) if (p.xattr == xattr && p.editable) return p;
    details::Property p = { xattr, xattr, details::ValueType::Text, xattr, true, "" };
    p.multiline = true;
    return p;
}

void load()
{
    g->attrs.clear();
    g->order.clear();
    std::vector<std::vector<std::string>> names(g->files.size());
    std::vector<std::string> all = first_names();
    size_t nfields = all.size();
    for (size_t i = 0; i < g->files.size(); i++) {
        details::list_attrs(g->files[i], &names[i]);
        for (const std::string& n : names[i]) if (std::find(all.begin(), all.end(), n) == all.end()) all.push_back(n);
    }
    std::sort(all.begin() + (long)nfields, all.end());
    for (const std::string& n : all) {
        Attr a;
        for (size_t i = 0; i < g->files.size(); i++) {
            std::string v;
            int e = 0;
            details::Read r = details::read_attr(g->files[i], n, &v, &e);
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
        g->attrs[n] = a;
        g->order.push_back(n);
    }
    g->file_tags.assign(g->files.size(), {});
    for (size_t i = 0; i < g->files.size(); i++) {
        std::string v;
        if (details::read_attr(g->files[i], TAGS, &v) == details::Read::Value) g->file_tags[i] = details::split_list(v);
    }
}

// file i's tags as they will be written: as read (or as typed in the Advanced list), with the additions and removals
std::vector<std::string> tags_of(size_t i)
{
    auto pend = g->pending.find(TAGS);
    std::vector<std::string> t = pend != g->pending.end() ? details::split_list(pend->second) : g->file_tags[i];
    for (const std::string& a : g->tag_add) if (std::find(t.begin(), t.end(), a) == t.end()) t.push_back(a);
    for (const std::string& r : g->tag_remove) t.erase(std::remove(t.begin(), t.end(), r), t.end());
    return t;
}

// the tag list's rows: every file's tags in their order (the first file's first), each with how many files have it
struct TagRow { std::string tag; size_t have; };
std::vector<TagRow> tag_rows()
{
    std::vector<TagRow> out;
    for (size_t i = 0; i < g->files.size(); i++)
        for (const std::string& t : tags_of(i)) {
            auto r = std::find_if(out.begin(), out.end(), [&](const TagRow& x) { return x.tag == t; });
            if (r == out.end()) out.push_back(TagRow{ t, 1 }); else r->have++;
        }
    return out;
}

bool tags_changed() { return !g->tag_add.empty() || !g->tag_remove.empty(); }

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

std::u16string W(const std::string& s) { return w32::utf16_of(s.c_str()); }
void set_text(HWND dlg, int id, const std::string& s) { SetDlgItemTextW(dlg, id, (LPCWSTR)W(s).c_str()); }
std::string window_text(HWND h)
{
    int n = GetWindowTextLengthW(h);
    std::u16string b((size_t)n + 1, u'\0');
    n = GetWindowTextW(h, (LPWSTR)&b[0], n + 1);
    return w32::utf8_of((const WCHAR*)b.c_str(), n);
}
std::string get_text(HWND dlg, int id) { return window_text(GetDlgItem(dlg, id)); }
void show(HWND dlg, int id, bool on) { ShowWindow(GetDlgItem(dlg, id), on ? SW_SHOW : SW_HIDE); }

std::string base_name(const std::string& p)
{
    std::string b = base_of(p);
    return b.empty() ? p : b;
}

void error_box(HWND owner, const std::string& text) { message_box(owner, g->title, text, MB_OK | MB_ICONEXCLAMATION); }

// ---- General -----------------------------------------------------------------------------

// "Opens with:" the program's 16 px icon, then its name (XP's General page); the icon is a static made beside the name the
// first time, the name moved right of it
void show_opens_with(HWND dlg, const std::string& mime)
{
    const App* a = default_app(mime);
    set_text(dlg, IDC_OPENS, a ? a->name : "(none)");
    HWND name = GetDlgItem(dlg, IDC_OPENS), ic = GetDlgItem(dlg, IDC_OPENSICON);
    if (!ic && name) {
        RECT r; GetWindowRect(name, &r); MapWindowPoints(nullptr, dlg, (POINT*)&r, 2);
        int y = (r.top + r.bottom) / 2 - 8;
        ic = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | SS_ICON | SS_REALSIZEIMAGE, r.left, y, 16, 16, dlg,
                             (HMENU)(INT_PTR)IDC_OPENSICON, nullptr, nullptr);
        MoveWindow(name, r.left + 20, r.top, r.right - r.left - 20, r.bottom - r.top, TRUE);
    }
    if (!ic) return;
    HICON old = (HICON)SendMessageW(ic, STM_SETICON, (WPARAM)(a ? app_hicon(a->id, 16) : nullptr), 0);
    if (old) DestroyIcon(old);
    ShowWindow(ic, a ? SW_SHOW : SW_HIDE);
}

void init_general(HWND dlg)
{
    if (g->icon) SendDlgItemMessageW(dlg, IDC_GICON, STM_SETICON, (WPARAM)g->icon, 0);
    const PropertiesOptions& o = *g->opts;
    if (g->files.size() == 1) {
        const std::string& path = g->files[0];
        FileInfo fi = file_info(path, true);
        std::string name = o.name.empty() ? base_name(path) : o.name, mime = mime_type(path);
        set_text(dlg, IDC_NAME, name);
        std::string type = o.type.empty() ? type_name(base_name(path), fi.is_dir) : o.type;
        set_text(dlg, IDC_TYPE, type + " (" + mime + ")");
        bool change = !fi.is_dir && o.change_program;
        if (fi.is_dir) {
            show(dlg, IDC_OPENSL, false);
            show(dlg, IDC_OPENS, false);
            set_text(dlg, IDC_CONTAINS, details::thousands(fi.files) + " Files, " + details::thousands(fi.folders) + " Folders");
        } else {
            show_opens_with(dlg, mime);
            show(dlg, IDC_CONTAINSL, false);
        }
        if (!change) {
            // without Change... the program's name has the row to itself (as Type's)
            show(dlg, IDC_CHANGE, false);
            RECT r = { 70, 55, 220, 63 };
            MapDialogRect(dlg, &r);
            SetWindowPos(GetDlgItem(dlg, IDC_OPENS), nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        set_text(dlg, IDC_LOCATION, dir_of(path));
        if (fi.ok) {
            set_text(dlg, IDC_GSIZE, details::size_and_bytes(fi.size));
            set_text(dlg, IDC_MODIFIED, details::long_date(fi.mtime));
            set_text(dlg, IDC_ACCESSED, details::long_date(fi.atime));
            set_text(dlg, IDC_CHANGED, details::long_date(fi.ctime));
            set_text(dlg, IDC_PERMS, fi.perms + "   " + fi.owner + ":" + fi.group);
        }
        if (fi.is_link) { set_text(dlg, IDC_EXTRAL, "Link target:"); set_text(dlg, IDC_EXTRA, fi.link_target); }
        return;
    }
    // XP's page for several items: "N Files, M Folders", the type they share, where they are, the total size
    uint64_t files = 0, folders = 0, size = 0;
    std::string type, dir;
    bool one_type = true, one_dir = true;
    for (size_t i = 0; i < g->files.size(); i++) {
        FileInfo fi = file_info(g->files[i], true);
        (fi.is_dir ? folders : files)++;
        size += fi.size;
        std::string t = type_name(base_name(g->files[i]), fi.is_dir), d = dir_of(g->files[i]);
        if (i == 0) { type = t; dir = d; }
        if (t != type) one_type = false;
        if (d != dir) one_dir = false;
    }
    set_text(dlg, IDC_NAME, details::thousands(files) + (files == 1 ? " File, " : " Files, ") + details::thousands(folders) +
                            (folders == 1 ? " Folder" : " Folders"));
    set_text(dlg, IDC_TYPEL, "Type:");
    set_text(dlg, IDC_TYPE, one_type ? "All of type " + type : "Multiple Types");
    set_text(dlg, IDC_LOCATION, one_dir ? "All in " + dir : "Various Folders");
    set_text(dlg, IDC_GSIZE, details::size_and_bytes(size));
    for (int id : { IDC_OPENSL, IDC_OPENS, IDC_CHANGE, IDC_CONTAINSL, IDC_CONTAINS, IDC_SEP3, IDC_MODIFIEDL, IDC_MODIFIED,
                    IDC_ACCESSEDL, IDC_ACCESSED, IDC_CHANGEDL, IDC_CHANGED, IDC_SEP4, IDC_PERMSL, IDC_PERMS })
        show(dlg, id, false);
}

INT_PTR CALLBACK general_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM)
{
    if (msg == WM_INITDIALOG) { if (!g->opts->window_icon.empty()) set_window_icon_key(GetParent(dlg), g->opts->window_icon); init_general(dlg); return TRUE; }
    if (msg == WM_COMMAND && LOWORD(wp) == IDC_CHANGE && g->opts->change_program && g->files.size() == 1) {
        std::string mime = mime_type(g->files[0]);
        if (g->opts->change_program(dlg, g->files[0], mime)) show_opens_with(dlg, mime);
        return TRUE;
    }
    return FALSE;
}

// ---- Metadata ----------------------------------------------------------------------------

void changed() { if (!g->filling && g->page) SendMessageW(GetParent(g->page), PSM_CHANGED, (WPARAM)g->page, 0); }

// a change of the tags as a whole (typed in the Advanced list, removed there) replaces the additions and removals
void set_pending(const std::string& name, const std::string& value)
{
    g->pending[name] = value;
    if (name == TAGS) { g->tag_add.clear(); g->tag_remove.clear(); }
}

// LF as CR LF for a multi-line edit
std::string crlf(const std::string& v)
{
    std::string out;
    for (char c : v) { if (c == '\n') out += '\r'; out += c; }
    return out;
}

// the colour the swatches show: the pending one, else the files' when they agree; *several when they do not
std::string color_value(bool* several)
{
    *several = false;
    auto pend = g->pending.find(COLOR);
    if (pend != g->pending.end()) return pend->second;
    const Attr& a = g->attrs[COLOR];
    if (!a.agree) { *several = true; return ""; }
    return a.value;
}

// the swatch a value selects: 0 None, 1.. the palette, -1 an unknown name (none drawn chosen)
int swatch_of(const std::string& v)
{
    if (v.empty()) return 0;
    const std::vector<details::Color>& p = details::palette();
    for (size_t i = 0; i < p.size() && (int)i + 1 < NSWATCH; i++) if (strcasecmp(p[i].name, v.c_str()) == 0) return (int)i + 1;
    return -1;
}

void fill_color()
{
    bool several;
    std::string v = color_value(&several);
    const details::Color* c = details::color(v);
    std::string label = "Co&lor:";
    if (several) label += " (several)";
    else if (c) label += std::string(" ") + c->label;
    else if (!v.empty()) label += " " + v;
    set_text(g->page, IDC_COLORL, label);
    for (int i = 0; i < NSWATCH; i++) InvalidateRect(GetDlgItem(g->page, IDC_SWATCH0 + i), nullptr, FALSE);
}

void fill_tags(const std::vector<std::string>& select = {})
{
    HWND list = GetDlgItem(g->page, IDC_TAGLIST);
    g->filling = true;
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const TagRow& r : tag_rows()) {
        int i = (int)SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)W(r.tag).c_str());
        SendMessageW(list, LB_SETITEMDATA, (WPARAM)i, r.have < g->files.size() ? 1 : 0);   // on some of the files only
        if (std::find(select.begin(), select.end(), r.tag) != select.end()) SendMessageW(list, LB_SETSEL, TRUE, i);
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
    EnableWindow(GetDlgItem(g->page, IDC_TAGREMOVE), SendMessageW(list, LB_GETCOUNT, 0, 0) > 0);
    g->filling = false;
}

// the simple view from the model and what is pending
void fill_fields()
{
    HWND dlg = g->page;
    g->filling = true;
    for (const auto& f : TEXT_FIELDS) {
        const details::Property* p = details::property(f.prop);
        const Attr& a = g->attrs[p->xattr];
        HWND e = GetDlgItem(dlg, f.id);
        auto pend = g->pending.find(p->xattr);
        std::string text = pend != g->pending.end() ? pend->second : a.agree ? a.value : std::string();
        if (p->multiline) text = crlf(text);
        SetWindowTextW(e, (LPCWSTR)W(text).c_str());
        std::string cue = a.failed ? "(could not be read: " + a.error + ")" : a.agree || pend != g->pending.end() ? "" : "(several values)";
        SendMessageW(e, EM_SETCUEBANNER, TRUE, (LPARAM)W(cue).c_str());
        SendMessageW(e, EM_SETMODIFY, FALSE, 0);
    }
    g->filling = false;
    fill_tags();
    fill_color();
}

// what was typed into the simple view's edits becomes pending
void take_fields()
{
    if (!g->page) return;
    for (const auto& f : TEXT_FIELDS) {
        HWND e = GetDlgItem(g->page, f.id);
        if (!SendMessageW(e, EM_GETMODIFY, 0, 0)) continue;
        g->pending[details::property(f.prop)->xattr] = window_text(e);
        SendMessageW(e, EM_SETMODIFY, FALSE, 0);
    }
}

// ---- the tag list

void add_typed_tags()
{
    HWND box = GetDlgItem(g->page, IDC_TAGEDIT);
    std::vector<std::string> tags = details::split_list(window_text(box));
    if (tags.empty()) { MessageBeep(0); return; }
    for (const std::string& t : tags) {
        g->tag_remove.erase(std::remove(g->tag_remove.begin(), g->tag_remove.end(), t), g->tag_remove.end());
        if (std::find(g->tag_add.begin(), g->tag_add.end(), t) == g->tag_add.end()) g->tag_add.push_back(t);
    }
    SetWindowTextW(box, L"");
    fill_tags(tags);
    changed();
}

void remove_selected_tags()
{
    HWND list = GetDlgItem(g->page, IDC_TAGLIST);
    int n = (int)SendMessageW(list, LB_GETSELCOUNT, 0, 0);
    if (n <= 0) { MessageBeep(0); return; }
    std::vector<int> sel((size_t)n);
    n = (int)SendMessageW(list, LB_GETSELITEMS, (WPARAM)n, (LPARAM)sel.data());
    int first = sel.empty() ? 0 : sel[0];
    for (int i = 0; i < n; i++) {
        int len = (int)SendMessageW(list, LB_GETTEXTLEN, (WPARAM)sel[(size_t)i], 0);
        std::u16string b((size_t)len + 1, u'\0');
        SendMessageW(list, LB_GETTEXT, (WPARAM)sel[(size_t)i], (LPARAM)&b[0]);
        std::string t = w32::utf8_of((const WCHAR*)b.c_str(), len);
        g->tag_add.erase(std::remove(g->tag_add.begin(), g->tag_add.end(), t), g->tag_add.end());
        if (std::find(g->tag_remove.begin(), g->tag_remove.end(), t) == g->tag_remove.end()) g->tag_remove.push_back(t);
    }
    fill_tags();
    // the row now where the first removed one was keeps the selection, for the next Delete
    int count = (int)SendMessageW(list, LB_GETCOUNT, 0, 0);
    if (count > 0) {
        int at = first < count ? first : count - 1;
        SendMessageW(list, LB_SETSEL, TRUE, at);
        SendMessageW(list, LB_SETCARETINDEX, (WPARAM)at, FALSE);
    }
    changed();
}

// Enter in the tag box adds what is typed (with its drop-down closed), instead of the sheet's OK
LRESULT CALLBACK tag_edit_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    auto adding = [&]() {
        HWND box = GetParent(h);
        return GetWindowTextLengthW(h) > 0 && !SendMessageW(box, CB_GETDROPPEDSTATE, 0, 0);
    };
    switch (msg) {
    case WM_GETDLGCODE:
        if (wp == VK_RETURN && adding()) return DLGC_WANTALLKEYS | DefSubclassProc(h, msg, wp, lp);
        break;
    case WM_KEYDOWN:
        if (wp == VK_RETURN && adding()) { add_typed_tags(); return 0; }
        break;
    case WM_CHAR:
        if (wp == '\r') return 0;
        break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// ---- the swatches and the tag list, drawn

COLORREF rgb_of(unsigned rgb) { return RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }

void frame(HDC dc, RECT r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    FrameRect(dc, &r, b);
    DeleteObject(b);
}

// A 16 x 16 square with a 1 px dark border in the button's middle ("None" white with a red diagonal); the chosen one
// framed in the selection colour, two pixels out; the focus as a dotted rectangle around that
void draw_swatch(const DRAWITEMSTRUCT* di)
{
    int i = (int)di->CtlID - IDC_SWATCH0;
    HDC dc = di->hDC;
    RECT rc = di->rcItem;
    DrawThemeParentBackground(di->hwndItem, dc, &rc);
    int cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2;
    RECT sq = { cx - 8, cy - 8, cx + 8, cy + 8 };
    COLORREF fill = i == 0 ? RGB(255, 255, 255) : rgb_of(details::palette()[(size_t)i - 1].rgb);
    HBRUSH b = CreateSolidBrush(fill);
    FillRect(dc, &sq, b);
    DeleteObject(b);
    if (i == 0) {
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(0xD0, 0, 0));
        HGDIOBJ old = SelectObject(dc, pen);
        MoveToEx(dc, sq.left + 1, sq.bottom - 2, nullptr);
        LineTo(dc, sq.right - 1, sq.top);
        SelectObject(dc, old);
        DeleteObject(pen);
    }
    frame(dc, sq, (di->itemState & ODS_DISABLED) ? GetSysColor(COLOR_GRAYTEXT) : RGB(0x50, 0x50, 0x50));
    bool several;
    if (swatch_of(color_value(&several)) == i && !several) {
        RECT ring = { sq.left - 2, sq.top - 2, sq.right + 2, sq.bottom + 2 };
        frame(dc, ring, GetSysColor(COLOR_HIGHLIGHT));
        InflateRect(&ring, -1, -1);
        frame(dc, ring, GetSysColor(COLOR_HIGHLIGHT));
    }
    if ((di->itemState & ODS_FOCUS) && !(di->itemState & ODS_NOFOCUSRECT)) {
        RECT f = { sq.left - 3, sq.top - 3, sq.right + 3, sq.bottom + 3 };
        IntersectRect(&f, &f, &rc);
        DrawFocusRect(dc, &f);
    }
}

// a tag row: grey when only some of the files have it
void draw_tag(const DRAWITEMSTRUCT* di)
{
    if (di->itemID == (UINT)-1) {   // an empty list with the focus: the caret's rectangle alone
        if (di->itemState & ODS_FOCUS) DrawFocusRect(di->hDC, &di->rcItem);
        return;
    }
    HDC dc = di->hDC;
    bool sel = (di->itemState & ODS_SELECTED) != 0, some = di->itemData != 0;
    HBRUSH bg = GetSysColorBrush(sel ? COLOR_HIGHLIGHT : COLOR_WINDOW);
    FillRect(dc, &di->rcItem, bg);
    int len = (int)SendMessageW(di->hwndItem, LB_GETTEXTLEN, di->itemID, 0);
    std::u16string b((size_t)len + 1, u'\0');
    SendMessageW(di->hwndItem, LB_GETTEXT, di->itemID, (LPARAM)&b[0]);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(sel ? COLOR_HIGHLIGHTTEXT : some ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT));
    RECT r = di->rcItem;
    r.left += 2;
    DrawTextW(dc, (LPCWSTR)b.c_str(), len, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    if (di->itemState & ODS_FOCUS) DrawFocusRect(dc, &di->rcItem);
}

// ---- Run as: Browse...

// a path as a word of a /bin/sh command line: as it is when nothing in it is special, else quoted
std::string sh_word(const std::string& p)
{
    for (char c : p)
        if (!(isalnum((unsigned char)c) || strchr("/._-+,:@%", c))) return shell_quote(p);
    return p;
}

void browse_program()
{
    WCHAR file[4096] = {};
    std::u16string dir = W(access("/usr/bin", X_OK) == 0 ? "/usr/bin" : "/");
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = GetParent(g->page);
    ofn.lpstrFilter = L"All Files (*.*)\0*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof file / sizeof file[0];
    ofn.lpstrInitialDir = (LPCWSTR)dir.c_str();
    ofn.lpstrTitle = L"Run As";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) {
        if (CommDlgExtendedError()) MessageBeep(0);   // no file dialog in this program
        return;
    }
    std::string cmd = sh_word(w32::utf8_of(file)) + " %f";
    HWND e = GetDlgItem(g->page, IDC_RUN);
    SetWindowTextW(e, (LPCWSTR)W(cmd).c_str());
    SendMessageW(e, EM_SETMODIFY, TRUE, 0);
    changed();
    SetFocus(e);
}

// ---- the Advanced view

// the Advanced list's value of a name: the tags as every file will have them (the files differing: "(several values)")
std::string shown_value(const std::string& name)
{
    if (name == TAGS && tags_changed()) {
        std::string v0 = details::join_list(tags_of(0));
        for (size_t i = 1; i < g->files.size(); i++) if (details::join_list(tags_of(i)) != v0) return "(several values)";
        return printable(v0);
    }
    auto pend = g->pending.find(name);
    if (pend != g->pending.end()) return printable(pend->second);
    const Attr& a = g->attrs[name];
    if (a.failed) return "(could not be read: " + a.error + ")";
    return a.agree ? printable(a.value) : "(several values)";
}

// whether a name is a row of the list: some file has it, or will
bool there(const std::string& n)
{
    if (n == TAGS && tags_changed()) {
        for (size_t i = 0; i < g->files.size(); i++) if (!tags_of(i).empty()) return true;
        return false;
    }
    auto pend = g->pending.find(n);
    return pend != g->pending.end() ? !pend->second.empty() : g->attrs[n].have > 0 || g->attrs[n].failed;
}

void fill_list(const std::string& select = "")
{
    HWND list = GetDlgItem(g->page, IDC_LIST);
    g->filling = true;
    SendMessageW(list, LVM_DELETEALLITEMS, 0, 0);
    g->rows.clear();
    for (const std::string& n : g->order) if (there(n)) g->rows.push_back(n);
    for (size_t i = 0; i < g->rows.size(); i++) {
        std::u16string n = W(g->rows[i]), v = W(shown_value(g->rows[i]));
        LVITEMW it = {};
        it.mask = LVIF_TEXT;
        it.iItem = (int)i;
        it.pszText = (LPWSTR)n.c_str();
        SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&it);
        it.iSubItem = 1;
        it.pszText = (LPWSTR)v.c_str();
        SendMessageW(list, LVM_SETITEMTEXTW, i, (LPARAM)&it);
        if (g->rows[i] == select) {
            LVITEMW st = {};
            st.stateMask = st.state = LVIS_SELECTED | LVIS_FOCUSED;
            SendMessageW(list, LVM_SETITEMSTATE, i, (LPARAM)&st);
            SendMessageW(list, LVM_ENSUREVISIBLE, i, FALSE);
        }
    }
    g->filling = false;
    EnableWindow(GetDlgItem(g->page, IDC_REMOVE), !g->rows.empty());
}

void end_edit(bool commit);

const int SIMPLE_IDS[] = { IDC_DESCL, IDC_DESC, IDC_TAGSL, IDC_TAGEDIT, IDC_TAGADD, IDC_TAGLIST, IDC_TAGREMOVE, IDC_COLORL,
                           IDC_COMMENTL, IDC_COMMENT, IDC_RUNL, IDC_RUN, IDC_RUNBROWSE, IDC_RUNHINT, IDC_ADVANCED };

void set_advanced(bool on)
{
    if (on) take_fields(); else end_edit(true);
    g->advanced = on;
    HWND dlg = g->page;
    if (on) fill_list(); else fill_fields();
    for (int id : SIMPLE_IDS) show(dlg, id, !on);
    for (int i = 0; i < NSWATCH; i++) show(dlg, IDC_SWATCH0 + i, !on);
    for (int id : { IDC_LIST, IDC_NEW, IDC_REMOVE, IDC_SIMPLE }) show(dlg, id, on);
    SetFocus(GetDlgItem(dlg, on ? IDC_LIST : IDC_DESC));
}

// ---- the list's in-place editor (XP's Advanced view edits a value where it is shown)

WNDPROC g_edit_proc;
enum { WM_END_EDIT = WM_APP + 1 };

LRESULT CALLBACK edit_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS | CallWindowProcW(g_edit_proc, h, msg, wp, lp);
    case WM_KEYDOWN:
        if (wp == VK_RETURN || wp == VK_ESCAPE) { PostMessageW(g->page, WM_END_EDIT, wp == VK_RETURN, 0); return 0; }
        break;
    case WM_CHAR:
        if (wp == '\r' || wp == 27) return 0;   // no beep
        break;
    case WM_KILLFOCUS: PostMessageW(g->page, WM_END_EDIT, TRUE, 0); break;
    }
    return CallWindowProcW(g_edit_proc, h, msg, wp, lp);
}

void begin_edit(int row)
{
    if (row < 0 || row >= (int)g->rows.size()) return;
    end_edit(true);
    HWND list = GetDlgItem(g->page, IDC_LIST);
    SendMessageW(list, LVM_ENSUREVISIBLE, row, FALSE);
    RECT r = {};
    r.top = 1;              // the value's column
    r.left = LVIR_BOUNDS;
    if (!SendMessageW(list, LVM_GETSUBITEMRECT, row, (LPARAM)&r)) return;
    const std::string& name = g->rows[(size_t)row];
    auto pend = g->pending.find(name);
    const Attr& a = g->attrs[name];
    std::string shown = shown_value(name);
    bool several = shown == "(several values)";
    std::string text = name == TAGS && tags_changed() ? (several ? "" : details::join_list(tags_of(0)))
                     : pend != g->pending.end() ? pend->second : a.agree ? a.value : "";
    g->edit = CreateWindowExW(0, L"Edit", (LPCWSTR)W(text).c_str(), WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                              r.left, r.top - 1, r.right - r.left, r.bottom - r.top + 2, list, nullptr, nullptr, nullptr);
    SendMessageW(g->edit, WM_SETFONT, SendMessageW(list, WM_GETFONT, 0, 0), FALSE);
    if (several) SendMessageW(g->edit, EM_SETCUEBANNER, TRUE, (LPARAM)u"(several values)");
    g_edit_proc = (WNDPROC)SetWindowLongPtrW(g->edit, GWLP_WNDPROC, (LONG_PTR)edit_proc);
    g->edit_row = row;
    ShowWindow(g->edit, SW_SHOW);
    SetFocus(g->edit);
    SendMessageW(g->edit, EM_SETSEL, 0, -1);
}

void end_edit(bool commit)
{
    if (!g->edit) return;
    HWND e = g->edit;
    g->edit = nullptr;
    std::string name = g->edit_row >= 0 && g->edit_row < (int)g->rows.size() ? g->rows[(size_t)g->edit_row] : "";
    bool modified = SendMessageW(e, EM_GETMODIFY, 0, 0) != 0;
    std::string text = window_text(e);
    bool had_focus = GetFocus() == e;
    DestroyWindow(e);
    if (commit && modified && !name.empty()) {
        set_pending(name, text);
        changed();
        fill_list(name);
    }
    if (had_focus) SetFocus(GetDlgItem(g->page, IDC_LIST));
}

int selected_row()
{
    return (int)SendDlgItemMessageW(g->page, IDC_LIST, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
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
            std::string n = trim(get_text(dlg, IDC_ATTRNAME)), v = get_text(dlg, IDC_ATTRVALUE);
            if (n.compare(0, 5, "user.") != 0) n = "user." + n;   // only the user namespace is ours to write
            if (n.size() <= 5) { error_box(dlg, "Type the attribute's name."); SetFocus(GetDlgItem(dlg, IDC_ATTRNAME)); return TRUE; }
            if (n.size() > 255) { error_box(dlg, "The name is too long: an attribute's name has at most 255 bytes."); return TRUE; }
            if (trim(v).empty()) { error_box(dlg, "Type a value: an empty one removes the attribute."); SetFocus(GetDlgItem(dlg, IDC_ATTRVALUE)); return TRUE; }
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
    if (DialogBoxParamW(module(), MAKEINTRESOURCEW(IDD_NEWATTR), GetParent(g->page), new_proc, (LPARAM)&na) != IDOK) return;
    if (!g->attrs.count(na.name)) { g->attrs[na.name] = Attr(); g->order.push_back(na.name); }
    set_pending(na.name, na.value);
    changed();
    fill_list(na.name);
    SetFocus(GetDlgItem(g->page, IDC_LIST));
}

void remove_selected()
{
    end_edit(true);
    HWND list = GetDlgItem(g->page, IDC_LIST);
    int next = -1;
    bool any = false;
    for (int i = -1; (i = (int)SendMessageW(list, LVM_GETNEXTITEM, (WPARAM)i, LVNI_SELECTED)) >= 0;) {
        set_pending(g->rows[(size_t)i], "");
        next = i;
        any = true;
    }
    if (!any) { MessageBeep(0); return; }
    // the row after the last removed one keeps the selection
    std::string sel;
    for (size_t k = (size_t)next + 1; k < g->rows.size(); k++)
        if (there(g->rows[k])) { sel = g->rows[k]; break; }
    changed();
    fill_list(sel);
    SetFocus(list);
}

// Apply and OK: every pending value to every file, as the Explorer writes it; the tags per file. False after an error
// (the sheet stays).
bool apply()
{
    if (g->advanced) end_edit(true); else take_fields();
    std::string errors;
    auto fail = [&](const std::string& e) { if (errors.find(e) == std::string::npos) errors += (errors.empty() ? "" : "\n") + e; };
    bool whole_tags = g->pending.count(TAGS) != 0;
    if (whole_tags) g->pending[TAGS] = details::join_list(tags_of(0));   // typed in the list, then changed in the simple view
    for (const auto& kv : g->pending) {
        details::Property p = property_of(kv.first);
        for (const std::string& f : g->files) {
            std::string e;
            if (details::write(f, p, kv.second, &e)) g->wrote = true; else fail(e);
        }
    }
    if (!whole_tags && tags_changed())
        for (const std::string& f : g->files) {
            std::string e;
            if (details::change_tags(f, g->tag_add, g->tag_remove, &e)) g->wrote = true; else fail(e);
        }
    if (!g->tag_add.empty()) details::remember_tags(g->tag_add);
    g->pending.clear();
    g->tag_add.clear();
    g->tag_remove.clear();
    load();
    if (g->advanced) fill_list(); else fill_fields();
    if (!errors.empty()) { error_box(GetParent(g->page), errors); return false; }
    return true;
}

void init_metadata(HWND dlg)
{
    g->page = dlg;
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
    // the tag box: the known tags in its drop-down, Enter adding what is typed
    HWND box = GetDlgItem(dlg, IDC_TAGEDIT);
    for (const std::string& t : details::known_tags(g->files)) SendMessageW(box, CB_ADDSTRING, 0, (LPARAM)W(t).c_str());
    for (HWND ch = GetWindow(box, GW_CHILD); ch; ch = GetWindow(ch, GW_HWNDNEXT)) {
        WCHAR cls[16] = {};
        GetClassNameW(ch, cls, 16);
        if (lstrcmpiW(cls, L"Edit") == 0) SetWindowSubclass(ch, tag_edit_proc, 1, 0);
    }
    one_line_edit(GetDlgItem(dlg, IDC_DESC));
    one_line_edit(GetDlgItem(dlg, IDC_RUN));
    fill_fields();
}

INT_PTR CALLBACK metadata_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: init_metadata(dlg); return TRUE;
    case WM_END_EDIT: end_edit(wp != 0); return TRUE;
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT* mi = (MEASUREITEMSTRUCT*)lp;
        if (mi->CtlID != IDC_TAGLIST) return FALSE;
        HDC dc = GetDC(dlg);
        HGDIOBJ old = SelectObject(dc, (HFONT)SendMessageW(dlg, WM_GETFONT, 0, 0));
        TEXTMETRICW tm = {};
        GetTextMetricsW(dc, &tm);
        SelectObject(dc, old);
        ReleaseDC(dlg, dc);
        mi->itemHeight = (UINT)(tm.tmHeight + 2);
        return TRUE;
    }
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT* di = (const DRAWITEMSTRUCT*)lp;
        if (di->CtlID >= IDC_SWATCH0 && di->CtlID < IDC_SWATCH0 + NSWATCH) { draw_swatch(di); return TRUE; }
        if (di->CtlID == IDC_TAGLIST) { draw_tag(di); return TRUE; }
        return FALSE;
    }
    case WM_VKEYTOITEM:
        if ((HWND)lp == GetDlgItem(dlg, IDC_TAGLIST) && LOWORD(wp) == VK_DELETE) { remove_selected_tags(); return -2; }
        return -1;
    case WM_CHARTOITEM:   // the list's own search by first letter (-1); the DEL character wux sends after Delete: nothing
        return LOWORD(wp) == 0x7f ? -2 : -1;
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if ((id == IDC_DESC || id == IDC_COMMENT || id == IDC_RUN) && HIWORD(wp) == EN_CHANGE) { changed(); return TRUE; }
        if (id >= IDC_SWATCH0 && id < IDC_SWATCH0 + NSWATCH && HIWORD(wp) == BN_CLICKED) {
            int i = id - IDC_SWATCH0;
            g->pending[COLOR] = i == 0 ? "" : details::palette()[(size_t)i - 1].name;
            fill_color();
            changed();
            return TRUE;
        }
        if (id == IDC_TAGLIST && HIWORD(wp) == LBN_SELCHANGE) return TRUE;
        switch (id) {
        case IDC_TAGADD: add_typed_tags(); SetFocus(GetDlgItem(dlg, IDC_TAGEDIT)); return TRUE;
        case IDC_TAGREMOVE: remove_selected_tags(); return TRUE;
        case IDC_RUNBROWSE: browse_program(); return TRUE;
        case IDCANCEL:   // Escape in the comment (a multi-line edit closes its dialog): the sheet's Cancel
            PostMessageW(GetParent(dlg), PSM_PRESSBUTTON, PSBTN_CANCEL, 0);
            return TRUE;
        case IDC_ADVANCED: set_advanced(true); return TRUE;
        case IDC_SIMPLE: set_advanced(false); return TRUE;
        case IDC_NEW: new_attribute(); return TRUE;
        case IDC_REMOVE: remove_selected(); return TRUE;
        }
        break;
    }
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

// a one-line edit's paste: the clipboard's lines joined by spaces (the edit control alone keeps the first line)
LRESULT CALLBACK one_line_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    if (msg != WM_PASTE || (GetWindowLongW(h, GWL_STYLE) & ES_READONLY)) return DefSubclassProc(h, msg, wp, lp);
    std::u16string text;
    if (OpenClipboard(h)) {
        if (HANDLE d = GetClipboardData(CF_UNICODETEXT)) {
            if (const char16_t* p = (const char16_t*)GlobalLock(d)) {
                for (; *p; p++) {
                    bool br = *p == u'\r' || *p == u'\n';
                    if (!br) text += *p;
                    else if (!text.empty() && text.back() != u' ') text += u' ';
                }
                GlobalUnlock(d);
            }
        }
        CloseClipboard();
    }
    while (!text.empty() && text.back() == u' ') text.pop_back();
    SendMessageW(h, EM_REPLACESEL, TRUE, (LPARAM)text.c_str());
    return 0;
}

} // namespace

void one_line_edit(HWND edit)
{
    if (edit) SetWindowSubclass(edit, one_line_proc, 0x4f4c, 0);
}

bool properties(HWND owner, const std::vector<std::string>& paths, const PropertiesOptions& o)
{
    if (paths.empty()) return false;
    State st;
    State* outer = g;
    g = &st;
    st.files = paths;
    st.opts = &o;
    load();
    bool one = paths.size() == 1;
    bool dir0 = one && is_dir(paths[0]);
    st.icon = icon_for_key(one ? (o.icon_key.empty() ? icon_key(base_name(paths[0]), dir0) : o.icon_key) : "document", 32);

    PROPSHEETPAGEW pages[2] = {};
    pages[0].dwSize = pages[1].dwSize = sizeof pages[0];
    pages[0].dwFlags = pages[1].dwFlags = PSP_DEFAULT;
    pages[0].hInstance = pages[1].hInstance = module();
    pages[0].pszTemplate = MAKEINTRESOURCEW(IDD_GENERAL);
    pages[0].pfnDlgProc = general_proc;
    pages[1].pszTemplate = MAKEINTRESOURCEW(IDD_METADATA);
    pages[1].pfnDlgProc = metadata_proc;
    // XP's caption, PSH_PROPTITLE's "NAME Properties"; several: the first names and "..."
    std::string caption = one && !o.name.empty() ? o.name : base_name(paths[0]);
    if (!one) caption += ", " + base_name(paths[1]) + (paths.size() > 2 ? ", ..." : "");
    st.title = caption + " Properties";
    std::u16string cap = W(caption);
    PROPSHEETHEADERW psh = {};
    psh.dwSize = sizeof psh;
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_PROPTITLE | PSH_NOCONTEXTHELP;
    psh.hwndParent = owner;
    psh.hInstance = module();
    psh.pszCaption = (LPCWSTR)cap.c_str();
    psh.nPages = 2;
    psh.nStartPage = o.page == 1 ? 1 : 0;
    psh.ppsp = pages;
    PropertySheetW(&psh);
    if (st.icon) DestroyIcon(st.icon);
    g = outer;
    return st.wrote;
}

} // namespace shell
