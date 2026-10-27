// The browsing widget shared by the explorer and the file dialog (browser.h).
// Moved out of explorer.cpp (2026-10-25) without changes to what the
// explorer does; the hooks carry what differs between the hosts.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <algorithm>
#include <math.h>
#include "shell/namespace.h"
#include "shell/config.h"
#include "shell/lhtheme.h"
#include "shell/dirwatch.h"
#include "sys/sys.h"
#include "header.h"
#include "places.h"
#include "thumbview.h"
#include "uiicons.h"
#include "verbs.h"
#include "w32/w32.h"
#include "uxtheme.h"
#include "vssym32.h"
extern "C" {
#include "commctrl.h"
#include "w32/flxp_dwmapi.h"
#include "w32/flxp_shell.h"
#undef small      // rpcndr.h (through dwmapi.h)
}
#include "browser.h"

namespace browser {

static std::u16string W(const std::string& s) { return w32::utf16_of(s.c_str()); }
#define WS(s) ((LPCWSTR)W(s).c_str())

// ---- the frame ---------------------------------------------------------------------

bool frame_auto()
{
    std::string fr = shell::setting("frame", "auto");
    return fr != "xp" && fr != "longhorn";
}

bool want_longhorn(std::string* why)
{
    std::string fr = shell::setting("frame");   // per theme (settings.ini override, the registry's flxp_frame)
    if (fr == "xp" || fr == "longhorn") {
        if (why) *why = std::string(getenv("FLXP_FRAME") && *getenv("FLXP_FRAME") ? "FLXP_FRAME / --frame" : "flxp.ini frame") + "=" + fr;
        return fr == "longhorn";
    }
    std::string t = fr.empty() ? shell::session_theme().value("flxp_frame") : "";   // "auto" set for the theme: automatic
    if (t == "xp" || t == "longhorn") {
        if (why) *why = "session theme " + shell::session_theme().name + ": flxp_frame=" + t;
        return t == "longhorn";
    }
    BOOL comp = FALSE;
    DwmIsCompositionEnabled(&comp);
    FLXP_DWM_FRAME_METRICS fm = {}; fm.cbSize = sizeof fm;
    FlxpDwmGetFrameMetrics(&fm);
    WCHAR th[8];
    bool lhwm = comp || fm.bValid || FlxpDwmGetFrameThemePath(th, 8) == S_OK;
    if (why) *why = lhwm ? "frame=auto, lhwm runs" : "frame=auto, no lhwm on this display";
    return lhwm;
}

// The frame extends lhwm's glass by the header height and draws its own title.
void extend_frame(HWND frame)
{
    // on glass one row further: lhwm ends the extension with its plate's light
    // and dark lines (frame.c plate_explorer), which then lie under the header's
    // opaque dark last row and the bar below it (the clean 4051 edge); the
    // vision header ends in them itself (header::extend_top)
    MARGINS m = { 0, 0, header::extend_top(), 0 };
    DwmExtendFrameIntoClientArea(frame, &m);
    DWORD flags = FLXP_DWMFF_NO_ICON;      // browseui: WTNCA_NODRAWCAPTION | NODRAWICON
    // the caption keeps the title when the header does not show it (the Small
    // size, lh4074-195235.png, or the title switched off)
    if (header::draws_title()) flags |= FLXP_DWMFF_NO_TITLE;
    if (header::client_caption()) flags |= FLXP_DWMFF_CLIENT_CAPTION;
    DWORD was = 0;
    DwmGetWindowAttribute(frame, DWMWA_FLXP_FRAME_FLAGS, &was, sizeof was);
    if (was != flags) DwmSetWindowAttribute(frame, DWMWA_FLXP_FRAME_FLAGS, &flags, sizeof flags);
    caption_hole(frame);
}

// CLIENT_CAPTION: lhwm's caption buttons lie under our window; clicks there go
// through to them (the header leaves the rectangle transparent anyway)
void caption_hole(HWND frame)
{
    RECT r = {};
    DWORD flags = 0;
    DwmGetWindowAttribute(frame, DWMWA_FLXP_FRAME_FLAGS, &flags, sizeof flags);
    if (header::search_box() && (flags & FLXP_DWMFF_CLIENT_CAPTION) && FlxpDwmGetCaptionRect(frame, &r) == S_OK) FlxpDwmSetInputHole(frame, &r);
    else FlxpDwmSetInputHole(frame, nullptr);
    if (header::search_box()) header::caption_changed();    // the vision header keeps lhwm's buttons free
}

void unextend_frame(HWND frame)
{
    MARGINS none = { 0, 0, 0, 0 };
    DwmExtendFrameIntoClientArea(frame, &none);     // lhwm's own caption with title and icon again
    DWORD flags = 0;
    DwmSetWindowAttribute(frame, DWMWA_FLXP_FRAME_FLAGS, &flags, sizeof flags);
    FlxpDwmSetInputHole(frame, nullptr);
}

void header_refit(HWND frame, void (*layout)())
{
    FLXP_DWM_FRAME_METRICS m = {}; m.cbSize = sizeof m;
    FlxpDwmGetFrameMetrics(&m);
    int hh = header::height_for_caption(m.bValid ? m.cyCaption : 0);
    RECT rc; GetClientRect(frame, &rc);
    if (hh != header::height()) header::relayout(rc.right, hh);
    extend_frame(frame);                   // also the CLIENT_CAPTION flag, which follows glass
    layout();
    header::composition_changed();
}

bool header_hittest(HWND frame, LPARAM lp, LRESULT* result)
{
    if (!header::search_box()) return false;
    LRESULT ht = DefWindowProcW(frame, WM_NCHITTEST, 0, lp);
    POINT p = { (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
    ScreenToClient(frame, &p);
    *result = ht == HTCLIENT && p.y >= 0 && p.y < header::height() ? HTCAPTION : ht;
    if (*result == HTCAPTION && header::resize_hit(p.x, p.y)) *result = HTTOP;   // the vision header's top rows
    return true;
}

bool frame_active(HWND frame, bool focus_active)
{
    BOOL a = FALSE;
    bool active = FlxpDwmIsWindowActive(frame, &a) == S_OK ? a != FALSE : focus_active;
    if (getenv("FLXP_INACTIVE")) active = false;   // testing: draw as an inactive window
    return active;
}

void set_window_icon(HWND frame, const std::string& key)
{
    static std::map<HWND, std::string> shown;
    std::string k = key.empty() ? "folder" : key;
    if (shown[frame] == k) return;
    shown[frame] = k;
    const int sizes[3] = { 16, 48, 32 };   // ICON_SMALL, ICON_BIG, ICON_SMALL2
    for (int i = 0; i < 3; i++) {
        HICON ic = uiicons::load_icon(k, sizes[i]);
        HICON old = (HICON)SendMessageW(frame, WM_SETICON, (WPARAM)i, (LPARAM)ic);
        if (old) DestroyIcon(old);
    }
}

// ---- views remembered per folder ------------------------------------------------------

namespace views {

static const struct { const char* name; int view; } k_names[] = {
    { "thumbnails", VIEW_THUMBNAILS }, { "icons", LVS_ICON }, { "smallicons", LVS_SMALLICON }, { "list", LVS_LIST }, { "details", LVS_REPORT },
    { "smallicons+thumbnails", LVS_SMALLICON | VIEW_THUMBS_FLAG }, { "list+thumbnails", LVS_LIST | VIEW_THUMBS_FLAG },
    { "details+thumbnails", LVS_REPORT | VIEW_THUMBS_FLAG },
};
static std::map<std::string, int> g_views;
static std::map<std::string, int> g_shown;   // this window: the view each folder was last shown in
static struct stat g_stamp;                  // the file as last read (other windows write it too)

static bool enabled() { return shell::setting("remember_views", "1") != "0"; }

// "icons@64" (the zoom: the Longhorn hosts), "details+thumbnails", ...; -1 if not a view
int parse(const std::string& text)
{
    std::string name = text;
    int zoom = 0;
    size_t at = name.find('@');
    if (at != std::string::npos) { zoom = atoi(name.c_str() + at + 1); name.resize(at); }
    if (zoom < 0 || zoom > 256) zoom = 0;
    for (const auto& n : k_names) if (name == n.name) return n.view | zoom << ZOOM_SHIFT;
    return -1;
}

std::string name_of(int view)
{
    int zoom = view >> ZOOM_SHIFT;
    for (const auto& n : k_names)
        if (n.view == (view & ZOOM_VIEW)) return n.name + (zoom ? "@" + std::to_string(zoom) : std::string());
    return "";
}

// The view of a folder never shown (XP's global default, g_dfs): default_view
// (the XP frame) or default_view_longhorn in flxp.ini, set by Folder Options'
// Apply to All Folders; empty = flxp's own: Details in the XP frame (XP's
// Icons/Tiles are not ours), Icons at 48 px with thumbnails in the Longhorn
// hosts (the user's "default should be icon view with thumbnails")
static const char* default_key(bool zoomable) { return zoomable ? "default_view_longhorn" : "default_view"; }
int builtin_default(bool zoomable) { return zoomable ? (int)VIEW_THUMBNAILS | 48 << ZOOM_SHIFT : (int)LVS_REPORT; }
int default_view(bool zoomable)
{
    int v = parse(shell::setting(default_key(zoomable)));
    return v >= 0 ? v : builtin_default(zoomable);
}
void set_default(bool zoomable, int view) { shell::write_setting(default_key(zoomable), view < 0 ? "" : name_of(view)); }
static std::string path() { return shell::config_home() + "/flxp/views"; }

// (re)read the file when it changed since the last read
static void load()
{
    struct stat st = {};
    stat(path().c_str(), &st);
    if (st.st_mtim.tv_sec == g_stamp.st_mtim.tv_sec && st.st_mtim.tv_nsec == g_stamp.st_mtim.tv_nsec &&
        st.st_size == g_stamp.st_size && st.st_ino == g_stamp.st_ino) return;
    g_stamp = st;
    g_views.clear();
    std::string data;
    if (!shell::read_file(path(), data)) return;
    size_t at = 0;
    while (at < data.size()) {
        size_t nl = data.find('\n', at);
        std::string line = data.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        at = nl == std::string::npos ? data.size() : nl + 1;
        size_t tab = line.rfind('\t');
        if (tab == std::string::npos || !tab) continue;
        int v = parse(line.substr(tab + 1));
        if (v >= 0) g_views[line.substr(0, tab)] = v;
    }
}

// the view to show a folder in: the stored one, else the one it was last shown
// in by this window, else the default view (XP: a folder without a bag gets the
// default, not the view of the folder it came from; only with remember_views
// off does the current view carry over, = -1, see NOTES "How XP picks")
int lookup(const std::string& folder, bool zoomable)
{
    if (!enabled() || folder.empty()) return -1;
    load();
    auto it = g_views.find(folder);
    if (it != g_views.end()) return it->second;
    auto s = g_shown.find(folder);
    return s == g_shown.end() ? default_view(zoomable) : s->second;
}

// leaving a folder: remember its view for this window (not stored)
void leaving(const std::string& folder, int view) { if (!folder.empty()) g_shown[folder] = view; }

static void save()
{
    std::string out;
    for (const auto& v : g_views) {
        std::string n = name_of(v.second);
        if (!n.empty()) out += v.first + '\t' + n + '\n';
    }
    shell::write_file_atomic(path(), out);
    stat(path().c_str(), &g_stamp);
}

void store(const std::string& folder, int view)
{
    if (!enabled() || folder.empty()) return;
    load();
    if (g_views.count(folder) && g_views[folder] == view) return;
    g_views[folder] = view;
    g_shown[folder] = view;
    save();
}

// Ctrl+0: the folder goes back to the default view, as a folder never shown
void forget(const std::string& folder)
{
    g_shown.erase(folder);
    load();
    if (!g_views.erase(folder)) return;
    save();
}

// Apply to All Folders / Reset All Folders (XP deletes the Bags key): every folder's view forgotten
void forget_all()
{
    g_shown.clear();
    load();
    g_views.clear();
    save();
}

} // namespace views

// ---- the list ------------------------------------------------------------------------

std::string lower(const std::string& s)
{
    std::string out = s;
    for (char& c : out) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return out;
}

// Ctrl+wheel over the list: the zoom (the Longhorn hosts), as Vista's
static LRESULT CALLBACK list_subclass(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref)
{
    Browser& b = *(Browser*)ref;
    if (msg == WM_MOUSEWHEEL && ((GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) || (GetKeyState(VK_CONTROL) & 0x8000)) && b.zoomable) {
        static int acc;
        acc += GET_WHEEL_DELTA_WPARAM(wp);
        while (acc >= WHEEL_DELTA) { acc -= WHEEL_DELTA; zoom_step(b, 1); }
        while (acc <= -WHEEL_DELTA) { acc += WHEEL_DELTA; zoom_step(b, -1); }
        return 0;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

void attach_list(Browser& b) { SetWindowSubclass(b.list, list_subclass, 1, (DWORD_PTR)&b); }

HWND create_list(Browser& b, HWND parent, int id, DWORD style, DWORD exstyle, const int widths[4], HFONT font)
{
    b.list = CreateWindowExW(exstyle, WC_LISTVIEWW, nullptr, style, 0, 0, 0, 0, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    attach_list(b);
    b.view = (int)(style & LVS_TYPEMASK);
    if (font) SendMessageW(b.list, WM_SETFONT, (WPARAM)font, TRUE);
    // Explorer::ListView where the msstyles has it (Longhorn/Vista: themed item
    // selection, LVP_LISTITEM); XP's styles have no such class, nothing changes there
    SetWindowTheme(b.list, L"Explorer", nullptr);
    SendMessageW(b.list, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)b.small);
    SendMessageW(b.list, LVM_SETIMAGELIST, LVSIL_NORMAL, (LPARAM)b.large);
    static const struct { const wchar_t* name; int fmt; } cols[] = {
        { L"Name", LVCFMT_LEFT }, { L"Size", LVCFMT_RIGHT }, { L"Type", LVCFMT_LEFT }, { L"Date Modified", LVCFMT_LEFT },
    };
    for (int i = 0; i < 4; i++) {
        LVCOLUMNW c = {};
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT | LVCF_SUBITEM;
        c.pszText = (LPWSTR)cols[i].name; c.cx = widths[i]; c.fmt = cols[i].fmt; c.iSubItem = i;
        SendMessageW(b.list, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&c);
    }
    return b.list;
}

bool group_view_applies(const Browser& b) { return b.view == LVS_ICON || b.view == LVS_SMALLICON; }

static std::string group_title(const Item& it)
{
    if (it.is_folder()) return "Folders";
    return it.type.empty() ? "Other" : it.type;
}

// Group view: one group per type, folders first, the rest by name; the
// collapsed state the user left (by title) is kept across refills
static void make_groups(Browser& b, std::vector<int>& group_of)
{
    for (size_t i = 0; i < b.group_titles.size(); i++) {
        UINT st = (UINT)SendMessageW(b.list, LVM_GETGROUPSTATE, (WPARAM)i, LVGS_COLLAPSED);
        b.collapsed[b.group_titles[i]] = (st & LVGS_COLLAPSED) != 0;
    }
    if (!b.group_titles.empty()) SendMessageW(b.list, LVM_REMOVEALLGROUPS, 0, 0);
    b.group_titles.clear();
    bool on = b.grouped && group_view_applies(b);
    SendMessageW(b.list, LVM_ENABLEGROUPVIEW, on, 0);
    if (!on) return;
    std::vector<std::string> titles;
    for (const Item& it : b.items) titles.push_back(group_title(it));
    std::vector<std::string> order = titles;
    std::sort(order.begin(), order.end(), [](const std::string& x, const std::string& y) {
        if ((x == "Folders") != (y == "Folders")) return x == "Folders";
        return lower(x) < lower(y);
    });
    order.erase(std::unique(order.begin(), order.end()), order.end());
    for (size_t g = 0; g < order.size(); g++) {
        LVGROUP lg = {};
        lg.cbSize = sizeof lg;
        lg.mask = LVGF_HEADER | LVGF_GROUPID | LVGF_STATE;
        std::u16string t = W(order[g]);
        lg.pszHeader = (LPWSTR)t.c_str();
        lg.iGroupId = (int)g;
        lg.stateMask = LVGS_COLLAPSIBLE | LVGS_COLLAPSED;
        lg.state = LVGS_COLLAPSIBLE | (b.collapsed[order[g]] ? LVGS_COLLAPSED : 0);
        SendMessageW(b.list, LVM_INSERTGROUP, (WPARAM)-1, (LPARAM)&lg);
        b.group_titles.push_back(order[g]);
    }
    group_of.resize(b.items.size());
    for (size_t i = 0; i < b.items.size(); i++)
        group_of[i] = (int)(std::find(order.begin(), order.end(), titles[i]) - order.begin());
}

// list item i from b.items[i] (lParam i): inserted, or set again (a change on disk)
static void put_item(Browser& b, size_t i, int group, bool insert)
{
    const Item& it = b.items[i];
    LVITEMW lvi = {};
    lvi.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
    lvi.iItem = (int)i;
    std::u16string name = W(it.name);
    lvi.pszText = (LPWSTR)name.c_str();
    lvi.iImage = b.icon(it.icon);
    lvi.lParam = (LPARAM)i;
    bool cut = b.ghosted(it);
    if (cut || !insert) { lvi.mask |= LVIF_STATE; lvi.stateMask = LVIS_CUT; lvi.state = cut ? LVIS_CUT : 0; }
    if (group >= 0) { lvi.mask |= LVIF_GROUPID; lvi.iGroupId = group; }
    SendMessageW(b.list, insert ? LVM_INSERTITEMW : LVM_SETITEMW, 0, (LPARAM)&lvi);
    std::u16string size = b.size_shown(it) ? W(shell::format_size(it.size)) : std::u16string();
    std::u16string type = W(it.type);
    std::u16string date = it.mtime ? W(shell::format_date(it.mtime)) : std::u16string();
    LVITEMW sub = {};
    sub.mask = LVIF_TEXT; sub.iItem = (int)i;
    sub.iSubItem = 1; sub.pszText = (LPWSTR)size.c_str(); SendMessageW(b.list, LVM_SETITEMW, 0, (LPARAM)&sub);
    sub.iSubItem = 2; sub.pszText = (LPWSTR)type.c_str(); SendMessageW(b.list, LVM_SETITEMW, 0, (LPARAM)&sub);
    sub.iSubItem = 3; sub.pszText = (LPWSTR)date.c_str(); SendMessageW(b.list, LVM_SETITEMW, 0, (LPARAM)&sub);
}

void fill_list(Browser& b)
{
    b.filling = true;
    SendMessageW(b.list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(b.list, LVM_DELETEALLITEMS, 0, 0);
    shell::sort_items(b.items, b.sort, b.ascending);
    std::vector<int> group_of;
    if (b.grouped || !b.group_titles.empty()) make_groups(b, group_of);
    for (size_t i = 0; i < b.items.size(); i++) put_item(b, i, group_of.empty() ? -1 : group_of[i], true);
    thumbview::fill(b.items);   // Thumbnails view: the squares instead of the icons
    if (!group_of.empty()) SendMessageW(b.list, LVM_ARRANGE, LVA_DEFAULT, 0);
    SendMessageW(b.list, WM_SETREDRAW, TRUE, 0);
    if (!b.select_after_refresh.empty()) {
        for (size_t i = 0; i < b.items.size(); i++) {
            if (b.items[i].path != b.select_after_refresh) continue;
            LVITEMW st = {}; st.stateMask = LVIS_SELECTED | LVIS_FOCUSED; st.state = st.stateMask;
            SendMessageW(b.list, LVM_SETITEMSTATE, (WPARAM)i, (LPARAM)&st);
            SendMessageW(b.list, LVM_ENSUREVISIBLE, (WPARAM)i, FALSE);
            if (b.rename_after_refresh) { SetFocus(b.list); SendMessageW(b.list, LVM_EDITLABELW, (WPARAM)i, 0); }
            break;
        }
        b.select_after_refresh.clear();
        b.rename_after_refresh = false;
    }
    sort_feedback(b);
    InvalidateRect(b.list, nullptr, TRUE);
    b.filled();
    b.filling = false;
}

// XP defview _SetSortFeedback: the sort column gets HDF_SORTUP/DOWN in the
// header and becomes the list's selected (tinted) column
void sort_feedback(Browser& b)
{
    HWND hdr = (HWND)SendMessageW(b.list, LVM_GETHEADER, 0, 0);
    if (!hdr) return;
    int col = (int)b.sort;
    int n = (int)SendMessageW(hdr, HDM_GETITEMCOUNT, 0, 0);
    for (int i = 0; i < n; i++) {
        HDITEMW hi = {}; hi.mask = HDI_FORMAT;
        SendMessageW(hdr, HDM_GETITEMW, (WPARAM)i, (LPARAM)&hi);
        hi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (i == col) hi.fmt |= b.ascending ? HDF_SORTUP : HDF_SORTDOWN;
        SendMessageW(hdr, HDM_SETITEMW, (WPARAM)i, (LPARAM)&hi);
    }
    SendMessageW(b.list, LVM_SETSELECTEDCOLUMN, (WPARAM)col, 0);
}

// The list shows the folder's items whose names contain the search box's text
// (ASCII case-insensitive, as you type; 4074's "Filter by" box).
void apply_filter(Browser& b)
{
    b.items.clear();
    for (const Item& it : b.all_items)
        if (b.visible(it) && (b.filter.empty() || lower(it.name).find(b.filter) != std::string::npos)) b.items.push_back(it);
    fill_list(b);
}

bool set_filter(Browser& b, const std::string& text)
{
    std::string f = lower(text);
    if (f == b.filter) return false;
    b.filter = f;
    apply_filter(b);
    return true;
}

void refresh(Browser& b)
{
    if (!b.folder) return;
    b.all_items.clear();
    b.folder->enumerate(b.all_items, b.show_hidden);
    apply_filter(b);
    follow(b);
    b.shown();
}

// ---- following the folder on disk (inotify) ----------------------------------------------
// refresh() watches the folder shown (shell::DirWatch); a change sets a 150 ms timer, so a
// burst (a copy, an unpack) is one update; update() then changes the list item by item.

enum { FOLLOW_TIMER = 0x464c5850 };
static std::map<HWND, Browser*> g_following;   // list -> its browser (the timer's way back)

static bool same_item(const Item& x, const Item& y)
{
    return x.name == y.name && x.size == y.size && x.mtime == y.mtime && x.type == y.type && x.icon == y.icon && x.flags == y.flags;
}

static void CALLBACK follow_timer(HWND h, UINT, UINT_PTR, DWORD)
{
    KillTimer(h, FOLLOW_TIMER);
    auto f = g_following.find(h);
    if (f == g_following.end()) return;
    Browser& b = *f->second;
    b.follow_armed = false;
    int what = b.follow_pending;
    // not under the user's hands: a rename in progress, a drop being aimed at an item
    if (b.filling || SendMessageW(b.list, LVM_GETEDITCONTROL, 0, 0) || g_drop_item >= 0) {
        b.follow_armed = true;
        SetTimer(h, FOLLOW_TIMER, 300, follow_timer);
        return;
    }
    b.follow_pending = 0;
    if (what & shell::DirWatch::Gone) {
        // the folder went (deleted, moved, unmounted): the nearest folder above it that is still there
        std::string p = b.folder ? b.folder->path() : "";
        struct stat st;
        while (!p.empty() && p != "/" && (stat(p.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))) {
            size_t sl = p.rfind('/');
            p = sl == 0 ? "/" : p.substr(0, sl);
        }
        if (!p.empty() && navigate(b, p)) return;
        update(b);
        return;
    }
    if (what) update(b);
}

static void follow_ready(int, void* user)
{
    Browser& b = *(Browser*)user;
    if (!b.dirwatch) return;
    int what = b.dirwatch->take();
    if (!what || !b.list) return;
    b.follow_pending |= what;
    if (!b.follow_armed) { b.follow_armed = true; SetTimer(b.list, FOLLOW_TIMER, 150, follow_timer); }
}

void follow(Browser& b)
{
    if (!b.follow_disk || !b.list) return;
    if (!b.dirwatch) b.dirwatch.reset(new shell::DirWatch);
    g_following[b.list] = &b;
    bool fs = b.folder && b.folder->is_filesystem();
    if (!fs || !b.dirwatch->watch(b.folder->path())) b.dirwatch->clear();
    if (b.dirwatch->fd() >= 0) sys::watch_fd(b.dirwatch->fd(), follow_ready, &b);
}

Browser::~Browser() { unfollow(*this); }

void unfollow(Browser& b)
{
    if (b.dirwatch) {
        if (b.dirwatch->fd() >= 0) sys::unwatch_fd(b.dirwatch->fd());
        b.dirwatch.reset();
    }
    if (b.list) { KillTimer(b.list, FOLLOW_TIMER); g_following.erase(b.list); }
    b.follow_pending = 0;
    b.follow_armed = false;
}

// The folder enumerated again and the list brought in line item by item: gone
// entries deleted, new ones inserted where the sort puts them, changed ones
// set again; the selection, the focus, the scroll position and the thumbnails
// already made stay. Group view (a type's group may come or go) and an order
// the sort changes (a date sort when a file is written) refill instead, with
// the selection, focus and scroll position put back.
void update(Browser& b)
{
    if (!b.folder || !b.list) return;
    std::vector<Item> all;
    if (!b.folder->enumerate(all, b.show_hidden) && all.empty()) return;
    std::vector<Item> items;
    for (const Item& it : all)
        if (b.visible(it) && (b.filter.empty() || lower(it.name).find(b.filter) != std::string::npos)) items.push_back(it);
    shell::sort_items(items, b.sort, b.ascending);
    std::map<std::string, size_t> at_new, at_old;
    for (size_t i = 0; i < items.size(); i++) at_new[items[i].path] = i;
    for (size_t i = 0; i < b.items.size(); i++) at_old[b.items[i].path] = i;
    // the kept items must keep their order (then deletions and insertions are enough)
    bool ordered = !b.grouped || b.group_titles.empty();
    size_t last = 0; bool first = true;
    bool any = items.size() != b.items.size();
    for (size_t i = 0; i < b.items.size() && ordered; i++) {
        auto n = at_new.find(b.items[i].path);
        if (n == at_new.end()) { any = true; continue; }
        if (!first && n->second < last) ordered = false;
        if (!same_item(b.items[i], items[n->second])) any = true;
        last = n->second; first = false;
    }
    if (!any && ordered) { b.all_items = all; return; }   // nothing the list shows
    b.filling = true;
    if (!ordered) {
        std::vector<std::string> sel;
        std::string focus;
        int i = -1;
        while ((i = (int)SendMessageW(b.list, LVM_GETNEXTITEM, (WPARAM)i, LVNI_SELECTED)) >= 0)
            if ((size_t)i < b.items.size()) sel.push_back(b.items[(size_t)i].path);
        i = (int)SendMessageW(b.list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_FOCUSED);
        if (i >= 0 && (size_t)i < b.items.size()) focus = b.items[(size_t)i].path;
        POINT org = {};
        SendMessageW(b.list, LVM_GETORIGIN, 0, (LPARAM)&org);
        int top = (int)SendMessageW(b.list, LVM_GETTOPINDEX, 0, 0);
        RECT ir = {};
        int row = 0;
        if (b.view == LVS_REPORT || b.view == LVS_LIST) {
            ir.left = LVIR_BOUNDS;
            if (SendMessageW(b.list, LVM_GETITEMRECT, 0, (LPARAM)&ir)) row = ir.bottom - ir.top;
        }
        b.all_items = all;
        b.items = items;
        SendMessageW(b.list, WM_SETREDRAW, FALSE, 0);
        fill_list(b);   // (filled() too)
        b.filling = true;
        for (size_t k = 0; k < b.items.size(); k++) {
            bool s = std::find(sel.begin(), sel.end(), b.items[k].path) != sel.end();
            bool f = b.items[k].path == focus;
            if (!s && !f) continue;
            LVITEMW st = {};
            st.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
            st.state = (s ? LVIS_SELECTED : 0) | (f ? LVIS_FOCUSED : 0);
            SendMessageW(b.list, LVM_SETITEMSTATE, (WPARAM)k, (LPARAM)&st);
        }
        if (b.view == LVS_REPORT && row > 0) SendMessageW(b.list, LVM_SCROLL, 0, (LPARAM)(top * row));
        else if (b.view != LVS_REPORT) SendMessageW(b.list, LVM_SCROLL, (WPARAM)org.x, (LPARAM)org.y);
        SendMessageW(b.list, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(b.list, nullptr, TRUE);
        b.filling = false;
        b.changed_on_disk();
        return;
    }
    // deletions (from the end: the indices before stay)
    for (size_t i = b.items.size(); i-- > 0;)
        if (!at_new.count(b.items[i].path)) SendMessageW(b.list, LVM_DELETEITEM, (WPARAM)i, 0);
    std::vector<Item> old;
    old.swap(b.items);
    b.items = items;
    b.all_items = all;
    // insertions and changes, in the new order; then every lParam is its index again
    for (size_t i = 0; i < b.items.size(); i++) {
        auto o = at_old.find(b.items[i].path);
        if (o == at_old.end()) put_item(b, i, -1, true);
        else if (!same_item(old[o->second], b.items[i])) put_item(b, i, -1, false);
    }
    for (size_t i = 0; i < b.items.size(); i++) {
        LVITEMW lvi = {}; lvi.mask = LVIF_PARAM; lvi.iItem = (int)i; lvi.lParam = (LPARAM)i;
        SendMessageW(b.list, LVM_SETITEMW, 0, (LPARAM)&lvi);
    }
    thumbview::fill(b.items);   // thumbnails: the cached ones by path and mtime, new or changed files asked for
    if (b.view != LVS_REPORT) SendMessageW(b.list, LVM_ARRANGE, LVA_DEFAULT, 0);
    InvalidateRect(b.list, nullptr, TRUE);
    b.filling = false;
    b.changed_on_disk();
}

bool navigate(Browser& b, const std::string& path, bool record)
{
    auto f = b.resolve(path);
    if (!f) return false;
    bool changed = !b.folder || b.folder->path() != f->path();
    if (record && b.folder && (changed || b.record_same)) { b.back.push_back(b.folder->path()); b.forward.clear(); }
    if (b.folder && changed && !b.filter.empty()) { b.filter.clear(); header::clear_filter(); places::clear_filter(); b.filter_cleared(); }
    if (changed) thumbview::folder_changed();
    if (changed && b.folder && b.remember_views) views::leaving(b.folder->path(), view_code(b));
    std::string from = b.folder ? b.folder->path() : "";
    b.folder = f;
    if (changed && b.remember_views) {
        int v = views::lookup(f->path(), b.zoomable);
        if (v >= 0 && v != view_code(b)) set_view(b, v, true);
    }
    if (b.select_parent && changed && !from.empty()) b.select_after_refresh = from;   // going up selects the folder we came from
    refresh(b);
    if (getenv("FLXP_DEBUG"))
        fprintf(stderr, "navigate %s view %d style %d\n", f->path().c_str(), menu_view(b), (int)(GetWindowLongW(b.list, GWL_STYLE) & LVS_TYPEMASK));
    return true;
}

void go_back(Browser& b)
{
    while (!b.back.empty()) {
        std::string p = b.back.back(); b.back.pop_back();
        std::string cur = b.folder ? b.folder->path() : "";
        if (!cur.empty()) b.forward.push_back(cur);
        if (navigate(b, p, false)) return;
        if (!cur.empty()) b.forward.pop_back();   // gone: try the one before it
    }
}

void go_forward(Browser& b)
{
    while (!b.forward.empty()) {
        std::string p = b.forward.back(); b.forward.pop_back();
        std::string cur = b.folder ? b.folder->path() : "";
        if (!cur.empty()) b.back.push_back(cur);
        if (navigate(b, p, false)) return;
        if (!cur.empty()) b.back.pop_back();
    }
}

void go_up(Browser& b)
{
    if (!b.folder) return;
    auto p = b.folder->parent();
    if (p) navigate(b, p->path());
}

// The Back/Forward drop-down: the last ten places, nearest first.
void history_menu(Browser& b, HWND owner, bool back, int x, int y)
{
    std::vector<std::string>& hist = back ? b.back : b.forward;
    if (hist.empty()) return;
    HMENU m = CreatePopupMenu();
    for (size_t i = 0; i < hist.size() && i < 10; i++) {
        auto f = b.resolve(hist[hist.size() - 1 - i]);
        AppendMenuW(m, MF_STRING, 5000 + (UINT)i, WS(f ? f->name() : hist[hist.size() - 1 - i]));
    }
    int cmd = TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, x, y, 0, owner, nullptr);
    DestroyMenu(m);
    if (cmd >= 5000) {
        int n = cmd - 5000 + 1;
        for (int k = 0; k < n; k++) { if (back) go_back(b); else go_forward(b); }
    }
}

// the view as the menus see it: LVS_* or VIEW_THUMBNAILS
int menu_view(const Browser& b) { return thumbview::active() ? (int)VIEW_THUMBNAILS : b.view; }

int view_code(const Browser& b)
{
    int zoom = b.zoomable && b.zoom ? b.zoom << ZOOM_SHIFT : 0;
    if (b.thumbs && b.view == LVS_ICON) return VIEW_THUMBNAILS | zoom;
    return b.view | (b.thumbs && b.thumbs_toggle ? (int)VIEW_THUMBS_FLAG : 0) | zoom;
}

void set_thumbnails(Browser& b, bool on)
{
    if (b.zoomable) {   // a toggle for the way the items are drawn, in every view (4051)
        b.thumbs = on;
        set_view(b, b.view | (on ? (int)VIEW_THUMBS_FLAG : 0));
        return;
    }
    if (on) { set_view(b, VIEW_THUMBNAILS); return; }
    b.thumbs = false;
    set_view(b, b.view);
}

// ---- zoom (the Longhorn hosts) ----------------------------------------------------

// 4051's slider (shell32 CIconSizeSlider, doc/4051-explorer-research.md section
// 4): positions 0..100, size = floor(16 * 16^(pos/100)); its ticks every 10
// are the levels: 16 21 27 36 48 64 84 111 147 194 256
int zoom_at(int pos)
{
    if (pos < 0) pos = 0;
    if (pos > 100) pos = 100;
    int px = (int)floor(16.0 * pow(16.0, pos / 100.0) + 1e-9);
    return px < 16 ? 16 : px > 256 ? 256 : px;
}

int zoom_pos(int px)
{
    if (px <= 16) return 0;
    if (px >= 256) return 100;
    int best = 0;
    for (int p = 0; p <= 100; p++) if (zoom_at(p) <= px) best = p;
    return best;
}

std::vector<int> zoom_levels()
{
    std::vector<int> z;
    for (int pos = 0; pos <= 100; pos += 10) z.push_back(zoom_at(pos));
    return z;
}

int icon_px(const Browser& b)
{
    if (b.zoomable && b.zoom) return b.zoom;
    if (b.thumbs) return thumbview::default_size();
    int cx = 32, cy = 32;
    if (b.large) ImageList_GetIconSize(b.large, &cx, &cy);
    return cx;
}

// the icons back on the items after thumbnails (their images were indices into the squares)
static void restore_icons(Browser& b)
{
    int n = (int)SendMessageW(b.list, LVM_GETITEMCOUNT, 0, 0);
    for (int i = 0; i < n; i++) {
        LVITEMW lvi = {}; lvi.mask = LVIF_PARAM; lvi.iItem = i;
        if (!SendMessageW(b.list, LVM_GETITEMW, 0, (LPARAM)&lvi) || lvi.lParam < 0 || (size_t)lvi.lParam >= b.items.size()) continue;
        lvi.mask = LVIF_IMAGE; lvi.iImage = b.icon(b.items[(size_t)lvi.lParam].icon);
        SendMessageW(b.list, LVM_SETITEMW, 0, (LPARAM)&lvi);
    }
}

// What the items show: thumbnails (XP's Thumbnails view: in Icons only; the
// Longhorn hosts' toggle: in every view - at the zoom in Icons, 16 px in the
// others) or the icons (the Longhorn hosts: at the zoom in Icons). True when
// the list was filled again.
static bool apply_images(Browser& b, bool navigating)
{
    bool thumbs = b.thumbs && (b.view == LVS_ICON || b.zoomable);
    bool refilled = false;
    HIMAGELIST large = b.zoomable ? b.icons_at(icon_px(b)) : b.large;
    if (thumbs) {
        bool small_slot = b.view != LVS_ICON;
        bool was = thumbview::active();
        bool changed = b.zoomable ? thumbview::enter_at(small_slot ? 16 : icon_px(b), small_slot, large, b.small)
                                  : (!was ? (thumbview::enter(), true) : false);
        if (changed && !navigating) { fill_list(b); refilled = true; }   // refilled: Wine keeps the old scroll extent otherwise
        return refilled;
    }
    if (!b.zoomable) {   // back from a zoomed Longhorn frame (a frame switch): the large icons
        if (!thumbview::active() && (HIMAGELIST)SendMessageW(b.list, LVM_GETIMAGELIST, LVSIL_NORMAL, 0) != b.large) {
            SendMessageW(b.list, LVM_SETIMAGELIST, LVSIL_NORMAL, (LPARAM)b.large);
            SendMessageW(b.list, LVM_SETICONSPACING, 0, (LPARAM)-1);
        }
    } else {
        int px = icon_px(b);
        HIMAGELIST cur = (HIMAGELIST)SendMessageW(b.list, LVM_GETIMAGELIST, LVSIL_NORMAL, 0);
        if (cur != large) {
            SendMessageW(b.list, LVM_SETIMAGELIST, LVSIL_NORMAL, (LPARAM)large);
            // XP's 75 px cells up to the large icons, then the icon and 43 more
            if (large == b.large) SendMessageW(b.list, LVM_SETICONSPACING, 0, (LPARAM)-1);
            else SendMessageW(b.list, LVM_SETICONSPACING, 0, MAKELPARAM(px <= 32 ? 75 : px + 43, px + 43));
        }
    }
    return false;
}

void set_view(Browser& b, int view, bool navigating)
{
    // the view, its zoom and whether thumbnails show in it
    int zoom = (view >> ZOOM_SHIFT) & 0x1ff;
    view &= ZOOM_VIEW;
    bool th;
    if (view == VIEW_THUMBNAILS) { th = true; view = LVS_ICON; }
    else if (view & VIEW_THUMBS_FLAG) { th = true; view &= ~VIEW_THUMBS_FLAG; }
    else th = b.thumbs_toggle && !navigating ? b.thumbs : false;   // a remembered plain view: off
    if (navigating || zoom) b.zoom = zoom;                         // a remembered view brings its zoom (none: the default)
    b.thumbs = th;
    bool was_grouped = !b.group_titles.empty();
    if (!(th && (view == LVS_ICON || b.zoomable)) && thumbview::active()) {
        // the items' images are indices into the thumbnail list: give them their
        // icons back before the small/large lists show them (else wrong icons until F5)
        thumbview::leave(b.zoomable ? b.icons_at(icon_px(b)) : b.large, b.small);
        if (!navigating) restore_icons(b);
    }
    b.view = view;
    if (!navigating && b.folder && b.remember_views) views::store(b.folder->path(), view_code(b));
    DWORD style = (DWORD)GetWindowLongW(b.list, GWL_STYLE);
    style = (style & ~LVS_TYPEMASK) | (DWORD)view;
    SetWindowLongW(b.list, GWL_STYLE, (LONG)style);
    bool refilled = apply_images(b, navigating);
    if (!refilled && !navigating && b.grouped && was_grouped != group_view_applies(b)) fill_list(b);   // groups come or go with the view
    if (view != LVS_REPORT) SendMessageW(b.list, LVM_ARRANGE, LVA_DEFAULT, 0);
    InvalidateRect(b.list, nullptr, TRUE);
    b.view_changed();
}

bool view_key(Browser& b, int key)
{
    switch (key) {
    case '0':
        set_view(b, views::default_view(b.zoomable));
        if (b.folder) views::forget(b.folder->path());   // (after set_view, which stores)
        return true;
    case '1': set_view(b, LVS_REPORT); return true;
    case '2': set_view(b, b.zoomable ? (int)LVS_ICON | 48 << ZOOM_SHIFT : (int)LVS_ICON); return true;
    case '3': set_view(b, b.zoomable ? (int)VIEW_THUMBNAILS | thumbview::default_size() << ZOOM_SHIFT : (int)VIEW_THUMBNAILS); return true;
    }
    return false;
}

void set_zoom(Browser& b, int px)
{
    if (!b.zoomable) return;
    if (px == icon_px(b) && b.view == LVS_ICON) return;
    b.zoom = px;
    // the zoom is the icon views': from List or Details it goes to Icons (Vista)
    set_view(b, LVS_ICON | (b.thumbs ? (int)VIEW_THUMBS_FLAG : 0) | px << ZOOM_SHIFT);
}

void zoom_menu(const Browser& b, HMENU m, int first)
{
    while (GetMenuItemCount(m) > 0) DeleteMenu(m, 0, MF_BYPOSITION);
    std::vector<int> z = zoom_levels();
    int cur = icon_px(b);
    UINT gray = b.zoomable ? 0 : MF_GRAYED;
    AppendMenuW(m, MF_STRING | gray | (b.view == LVS_ICON && cur >= z.back() ? MF_GRAYED : 0), (UINT)first, L"Zoom &In\tCtrl++");
    AppendMenuW(m, MF_STRING | gray | (b.view == LVS_ICON && cur <= z.front() ? MF_GRAYED : 0), (UINT)first + 1, L"Zoom &Out\tCtrl+-");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    for (size_t i = 0; i < z.size(); i++) {
        std::string t = std::to_string(z[i]) + " x " + std::to_string(z[i]);
        AppendMenuW(m, MF_STRING | gray, (UINT)(first + 2 + (int)i), WS(t));
        if (b.view == LVS_ICON && z[i] == cur) CheckMenuRadioItem(m, (UINT)(first + 2), (UINT)(first + 1 + (int)z.size()), (UINT)(first + 2 + (int)i), MF_BYCOMMAND);
    }
}

bool zoom_command(Browser& b, int id, int first)
{
    std::vector<int> z = zoom_levels();
    if (id < first || id >= first + 2 + (int)z.size()) return false;
    if (id == first) zoom_step(b, 1);
    else if (id == first + 1) zoom_step(b, -1);
    else set_zoom(b, z[(size_t)(id - first - 2)]);
    return true;
}

// The magnifier's drop-down (4051's CIconSizeSlider): a 74x150 popup with a
// 40x125 vertical trackbar at (17, 12), 0..100 with ticks every 10, the big
// end at the top; the zoom follows the thumb (snapped to the ticks, the
// levels). Escape, Enter or a click elsewhere close it.
static Browser* g_zoom_b;
static LRESULT CALLBACK zoom_popup_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_VSCROLL: {
        HWND tb = (HWND)lp;
        int pos = 100 - (int)SendMessageW(tb, TBM_GETPOS, 0, 0);
        pos = (pos + 5) / 10 * 10;
        if (LOWORD(wp) == TB_ENDTRACK || LOWORD(wp) == TB_THUMBPOSITION) SendMessageW(tb, TBM_SETPOS, TRUE, 100 - pos);
        if (g_zoom_b && zoom_at(pos) != icon_px(*g_zoom_b)) set_zoom(*g_zoom_b, zoom_at(pos));
        return 0;
    }
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(h, &rc);
        HTHEME th = OpenThemeData(h, L"Menu");   // 4051 draws the menu's popup background
        if (th && IsThemePartDefined(th, MENU_POPUPBACKGROUND, 0)) DrawThemeBackground(th, (HDC)wp, MENU_POPUPBACKGROUND, 0, &rc, nullptr);
        else FillRect((HDC)wp, &rc, GetSysColorBrush(COLOR_MENU));
        if (th) CloseThemeData(th);
        FrameRect((HDC)wp, &rc, GetSysColorBrush(COLOR_BTNSHADOW));
        return 1;
    }
    case WM_CTLCOLORSTATIC:   // the trackbar asks for its background
        return (LRESULT)GetSysColorBrush(COLOR_MENU);
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void zoom_popup(Browser& b, HWND owner, int x, int y)
{
    if (!b.zoomable) return;
    static bool registered;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = zoom_popup_proc;
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.lpszClassName = L"flxp_zoomslider";
        RegisterClassW(&wc);
        registered = true;
    }
    const int W_ = 74, H_ = 150;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    x -= W_ / 2;
    if (x + W_ > sw) x = sw - W_;
    if (x < 0) x = 0;
    if (y + H_ > sh) y = sh - H_;
    HWND pop = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"flxp_zoomslider", nullptr, WS_POPUP,   // (no WS_BORDER: the toolkit would hand it to the window manager)
                               x, y, W_, H_, owner, nullptr, nullptr, nullptr);
    HWND tb = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | TBS_VERT | TBS_AUTOTICKS | TBS_BOTH | TBS_FIXEDLENGTH,
                              17, 12, 40, 125, pop, nullptr, nullptr, nullptr);
    SendMessageW(tb, TBM_SETRANGE, FALSE, MAKELPARAM(0, 100));
    SendMessageW(tb, TBM_SETTICFREQ, 10, 0);
    SendMessageW(tb, TBM_SETPAGESIZE, 0, 10);
    SendMessageW(tb, TBM_SETLINESIZE, 0, 10);
    SendMessageW(tb, TBM_SETTHUMBLENGTH, 20, 0);
    SendMessageW(tb, TBM_SETPOS, TRUE, 100 - zoom_pos(icon_px(b)));
    g_zoom_b = &b;
    ShowWindow(pop, SW_SHOWNA);   // (the keys go to the trackbar from the loop; no focus rectangle)
    MSG msg;
    while (IsWindow(pop) && GetMessageW(&msg, nullptr, 0, 0)) {
        bool ours = msg.hwnd == pop || msg.hwnd == tb;
        if (msg.message == WM_KEYDOWN && (msg.wParam == VK_ESCAPE || msg.wParam == VK_RETURN)) break;
        if (msg.message == WM_KEYDOWN && !ours) { msg.hwnd = tb; }   // the keys move the thumb
        if (!ours && (msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN || msg.message == WM_MBUTTONDOWN ||
                      msg.message == WM_NCLBUTTONDOWN || msg.message == WM_NCRBUTTONDOWN)) break;   // eaten, as a menu's
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_zoom_b = nullptr;
    if (IsWindow(pop)) DestroyWindow(pop);
    if (IsWindow(b.list)) SetFocus(b.list);
}

void zoom_step(Browser& b, int dir)
{
    int cur = icon_px(b);
    if (b.view != LVS_ICON) { set_zoom(b, cur); return; }   // first step: the Icons view at the size now
    int next = cur;
    for (int v : zoom_levels()) {
        if (dir > 0 && v > cur) { next = v; break; }
        if (dir < 0 && v < cur) next = v;
    }
    if (next != cur) set_zoom(b, next);
}

void set_grouped(Browser& b, bool on)
{
    if (on == b.grouped) return;
    b.grouped = on;
    fill_list(b);
}

std::vector<Item> selection(const Browser& b)
{
    std::vector<Item> out;
    int i = -1;
    while ((i = (int)SendMessageW(b.list, LVM_GETNEXTITEM, (WPARAM)i, LVNI_SELECTED)) >= 0)
        if (i < (int)b.items.size()) out.push_back(b.items[(size_t)i]);
    return out;
}

int list_index(const Browser& b, const std::string& path)
{
    for (size_t i = 0; i < b.items.size(); i++) if (b.items[i].path == path) return (int)i;
    return -1;
}

void drag_from_list(Browser& b, int grabbed)
{
    std::vector<Item> items;
    std::vector<int> images;
    std::vector<int> order;
    if (grabbed >= 0) order.push_back(grabbed);
    int i = -1;
    while ((i = (int)SendMessageW(b.list, LVM_GETNEXTITEM, (WPARAM)i, LVNI_SELECTED)) >= 0) if (i != grabbed) order.push_back(i);
    for (int k : order) {
        if (k >= (int)b.items.size()) continue;
        items.push_back(b.items[(size_t)k]);
        LVITEMW lvi = {}; lvi.mask = LVIF_IMAGE; lvi.iItem = k;
        SendMessageW(b.list, LVM_GETITEMW, 0, (LPARAM)&lvi);
        images.push_back(lvi.iImage);
    }
    verbs::drag_items(b.list, items, (HIMAGELIST)SendMessageW(b.list, LVM_GETIMAGELIST, LVSIL_NORMAL, 0), images);
}

int g_drop_item = -1;        // the list item shown as the drop target (LVIS_DROPHILITED)

void drop_highlight(Browser& b, int item)
{
    if (item == g_drop_item) return;
    LVITEMW st = {}; st.stateMask = LVIS_DROPHILITED;
    if (g_drop_item >= 0 && g_drop_item < (int)b.items.size()) SendMessageW(b.list, LVM_SETITEMSTATE, (WPARAM)g_drop_item, (LPARAM)&st);
    st.state = LVIS_DROPHILITED;
    if (item >= 0) SendMessageW(b.list, LVM_SETITEMSTATE, (WPARAM)item, (LPARAM)&st);
    g_drop_item = item;
}

std::string drop_dest(Browser& b, const FLXPDROP* d, int* item)
{
    *item = -1;
    HWND t = d->hwndTarget;
    if (t != b.list && !IsChild(b.list, t)) return "";
    LVHITTESTINFO ht = {}; ht.pt = { d->x, d->y }; ScreenToClient(b.list, &ht.pt);
    int i = (int)SendMessageW(b.list, LVM_HITTEST, 0, (LPARAM)&ht);
    if (i >= 0 && i < (int)b.items.size() && b.items[(size_t)i].is_folder() && verbs::is_fs(b.items[(size_t)i])) { *item = i; return b.items[(size_t)i].path; }
    return b.folder && b.folder->is_filesystem() ? b.folder->path() : "";
}

}
