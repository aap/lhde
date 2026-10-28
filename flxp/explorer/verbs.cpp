#include <stdio.h>
#include <string.h>
#include <algorithm>
#include "verbs.h"
#include "uiicons.h"
#include "dialogs.h"
#include "shell/apps.h"
#include "shell/bookmarks.h"
#include "shell/config.h"
#include "shell/settings.h"
#include "shell/wallpapers.h"
#include "shell/fileops.h"
#include "shell/mime.h"
#include "shell/xclip.h"
#include "w32/user.h"
extern "C" {
#include "commctrl.h"
#include "w32/flxp_shell.h"
}

namespace verbs {

Ctx g_ctx;
static Host* g_host;
static std::vector<const shell::App*> g_openwith;    // as last shown in an Open With submenu
static std::vector<std::string> g_clip;              // Cut/Copy, also offered on the X CLIPBOARD (shell/xclip)
static bool g_clip_cut;

static std::u16string W(const std::string& s) { return w32::utf16_of(s.c_str()); }
#define WS(s) ((LPCWSTR)W(s).c_str())

void set_host(Host* h) { g_host = h; }
Host* host() { return g_host; }
const std::vector<std::string>& clip() { return g_clip; }
// the X CLIPBOARD's files unless our own Cut/Copy is what it holds: a list read
// from another owner on an earlier Paste is never reused (it was: a process that
// never owned the clipboard had nothing marking that list stale)
const std::vector<std::string>& clipboard_files()
{
    if (!shell::xclip::ours()) {
        std::vector<std::string> paths;
        bool cut = false;
        g_clip.clear(); g_clip_cut = false;
        if (shell::xclip::get_files(paths, cut)) { g_clip = paths; g_clip_cut = cut; }
    }
    return g_clip;
}
bool clip_cut() { return g_clip_cut; }
bool is_cut(const std::string& path)
{
    if (!g_clip_cut || !shell::xclip::ours()) return false;   // another client took the clipboard
    for (const std::string& c : g_clip) if (c == path) return true;
    return false;
}
// Paste is offered for our own files or when another client owns the X CLIPBOARD
bool can_paste() { return (!g_clip.empty() && shell::xclip::ours()) || shell::xclip::other_owner(); }
void clip_renamed(const std::string& from, const std::string& to) { for (std::string& c : g_clip) if (c == from) c = to; }

std::vector<std::string> drop_paths(const FLXPDROP* d)
{
    std::vector<std::string> v;
    for (UINT i = 0; d && i < d->cFiles; i++) v.push_back(d->ppszFiles[i]);
    return v;
}

DWORD drop_effect(const FLXPDROP* d, const std::string& dest)
{
    if (!d || dest.empty() || dest[0] != '/') return 0;
    return shell::drop_effect(drop_paths(d), dest, (d->grfKeyState & MK_SHIFT) != 0, (d->grfKeyState & MK_CONTROL) != 0, d->dwAllowed);
}

DWORD drop(const FLXPDROP* d, const std::string& dest)
{
    DWORD e = drop_effect(d, dest);
    if (!e) return 0;
    std::vector<std::string> paths = drop_paths(d), dirs = { dest };
    for (const std::string& p : paths) if (std::find(dirs.begin(), dirs.end(), shell::dir_of(p)) == dirs.end()) dirs.push_back(shell::dir_of(p));
    std::string err;
    bool ok = shell::drop_items(paths, dest, e, &err);
    if (!ok && !err.empty()) error_box(err);
    if (g_host) g_host->changed(dirs);
    return ok ? e : 0;
}

DWORD drag_items(HWND from, const std::vector<Item>& items, HIMAGELIST il, const std::vector<int>& images)
{
    std::vector<std::string> paths;
    std::vector<const char*> ptrs;
    for (const Item& it : items) if (is_fs(it)) paths.push_back(it.path);
    if (paths.empty()) return 0;
    for (const std::string& p : paths) ptrs.push_back(p.c_str());
    DWORD e = FlxpDragFiles(from, ptrs.data(), (UINT)ptrs.size(), DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK,
                            il, images.empty() ? nullptr : images.data(), (UINT)images.size());
    if (e == DROPEFFECT_MOVE && g_host) {   // moved by another program (ours refresh as targets already)
        std::vector<std::string> dirs;
        for (const std::string& p : paths) if (std::find(dirs.begin(), dirs.end(), shell::dir_of(p)) == dirs.end()) dirs.push_back(shell::dir_of(p));
        g_host->changed(dirs);
    }
    return e;
}

bool is_fs(const Item& it) { return !(it.flags & shell::IsVirtual) && !it.path.empty() && it.path[0] == '/'; }

void error_box(const std::string& text) { dialogs::message(g_host->window(), "flxp", text, true); }

bool is_wallpaper_image(const Item& it)
{
    if (!is_fs(it) || it.is_folder()) return false;
    FILE* f = fopen(it.path.c_str(), "rb");
    if (!f) return false;
    unsigned char h[8] = {};
    size_t n = fread(h, 1, sizeof h, f);
    fclose(f);
    if (n < 8) return false;
    if (!memcmp(h, "\x89PNG\r\n\x1a\n", 8)) return true;
    if (h[0] == 0xff && h[1] == 0xd8 && h[2] == 0xff) return true;     // JPEG
    return h[0] == 'B' && h[1] == 'M';                                   // BMP
}

bool set_wallpaper(const std::string& path, const std::string& mode)
{
    // Desktop.wallpaper / Desktop.mode into the session theme's modified copy (NAME-modified.theme, selected), then
    // _LH_THEME / _LH_SETTINGS_CHANGED: flxp-desktop, luxshell and luxwm follow the record (shell/wallpapers.h)
    std::string err;
    if (!shell::set_desktop_background(path, mode, &err)) {
        error_box("Cannot set the desktop background: " + err + ".");
        return false;
    }
    return true;
}

static std::string ctx_dir()
{
    if (g_ctx.items.empty()) return g_host->current_dir();
    const Item& it = g_ctx.items[0];
    if (!is_fs(it)) return "";
    return it.is_folder() ? it.path : shell::dir_of(it.path);
}

void open_items(const std::vector<Item>& items)
{
    std::vector<std::string> files;
    const Item* folder = nullptr;
    for (const Item& it : items) {
        if (it.is_folder()) { if (!folder) folder = &it; }
        else if (is_fs(it)) files.push_back(it.path);
    }
    std::string err;
    if (!files.empty() && !shell::open_files(files, &err)) error_box(err);
    if (folder) g_host->open_folder(*folder, false);
}

// the files of the first item's type: what Open With applies to
static std::vector<std::string> same_type(const std::vector<Item>& items, std::string* mime)
{
    std::vector<std::string> out;
    for (const Item& it : items) {
        if (it.is_folder() || !is_fs(it)) continue;
        std::string m = shell::mime_type(it.path);
        if (out.empty()) *mime = m;
        if (m == *mime) out.push_back(it.path);
    }
    return out;
}

static void open_with(const shell::App* app, const std::vector<Item>& items)
{
    std::string mime, err;
    std::vector<std::string> files = same_type(items, &mime);
    if (!app || files.empty()) return;
    if (!shell::launch(*app, files, &err)) error_box(err);
}

static void choose_program(const std::vector<Item>& items)
{
    std::string mime;
    std::vector<std::string> files = same_type(items, &mime);
    if (files.empty()) return;
    const Item* first = nullptr;
    for (const Item& it : items) if (it.path == files[0]) first = &it;
    HICON icon = first ? uiicons::large_icon(first->icon) : nullptr;
    dialogs::OpenWith ow;
    bool ok = dialogs::choose_program(g_host->window(), shell::base_of(files[0]), mime, icon, uiicons::small(), uiicons::index("exe"), false, &ow);
    if (icon) DestroyIcon(icon);
    if (!ok) return;
    std::string err;
    if (ow.always && !shell::set_default_app(mime, ow.app_id, &err)) error_box(err);
    const shell::App* app = shell::find_app(ow.app_id);
    if (app && !shell::launch(*app, files, &err)) error_box(err);
    g_host->updated();
}

static void delete_items(const std::vector<Item>& items_in)
{
    std::vector<Item> items;
    for (const Item& it : items_in) if (is_fs(it)) items.push_back(it);
    if (items.empty()) { MessageBeep(0); return; }
    std::vector<std::string> names;
    for (const Item& it : items) names.push_back(it.name);
    HICON icon = nullptr;
    int idx = uiicons::index("trash");
    if (idx >= 0) icon = ImageList_GetIcon(uiicons::large(), idx, ILD_NORMAL);
    bool yes = dialogs::confirm_delete(g_host->window(), names, icon);
    if (icon) DestroyIcon(icon);
    if (!yes) return;
    std::string err;
    std::vector<std::string> dirs;
    for (const Item& it : items) {
        std::string e;
        if (!shell::trash(it.path, &e)) { err += (err.empty() ? "" : "\n") + e; continue; }
        std::string d = shell::dir_of(it.path);
        if (std::find(dirs.begin(), dirs.end(), d) == dirs.end()) dirs.push_back(d);
        g_host->trashed(it);
    }
    g_host->changed(dirs);
    if (!err.empty()) error_box(err);
}

void show_properties(const Item& it)
{
    if (!is_fs(it)) { MessageBeep(0); return; }
    HICON icon = uiicons::large_icon(it.icon);
    dialogs::properties(g_host->window(), it.path, it.name, it.type, icon, uiicons::small(), uiicons::index("exe"));
    if (icon) DestroyIcon(icon);
    g_host->updated();
}

void Host::show_properties_default() { show_properties(folder_item()); }

void add_bookmark(const std::string& dir)
{
    if (dir.empty()) { MessageBeep(0); return; }
    if (!shell::add_bookmark(dir)) error_box("Cannot write " + shell::bookmarks_path() + ".");
}

bool run(int id)
{
    std::vector<Item>& items = g_ctx.items;
    std::string err;
    if (id >= ID_OPENWITH_FIRST && id <= ID_OPENWITH_LAST) {
        size_t k = (size_t)(id - ID_OPENWITH_FIRST);
        if (k < g_openwith.size()) open_with(g_openwith[k], items);
        return true;
    }
    switch (id) {
    case ID_OPEN:
        if (g_ctx.node) { if (!items.empty()) g_host->open_folder(items[0], false); }
        else open_items(items);
        return true;
    case ID_EXPLORE:
        if (!items.empty()) g_host->open_folder(items[0], true);
        return true;
    case ID_OPENWITH_CHOOSE: choose_program(items); return true;
    case ID_TERMINAL: {
        std::string d = ctx_dir();
        if (d.empty()) { MessageBeep(0); return true; }
        if (!shell::open_terminal(d, &err)) error_box(err);
        return true;
    }
    case ID_ADD_BOOKMARK:
        add_bookmark(items.empty() ? g_host->current_dir() : (items[0].is_folder() && is_fs(items[0]) ? items[0].path : ""));
        return true;
    case ID_CUT: case ID_COPY:
        g_clip.clear();
        for (const Item& it : items) if (is_fs(it)) g_clip.push_back(it.path);
        g_clip_cut = id == ID_CUT;
        shell::xclip::set_files(g_clip, g_clip_cut);
        g_host->clip_changed();
        return true;
    case ID_COPY_PATH: {   // the paths as text, one per line (our files offer goes: the clipboard holds text now)
        std::string text;
        for (const Item& it : items) if (is_fs(it)) text += (text.empty() ? "" : "\n") + it.path;
        if (text.empty()) { MessageBeep(0); return true; }
        g_clip.clear(); g_clip_cut = false;
        shell::xclip::set_text(text, false);
        g_host->clip_changed();
        return true;
    }
    case ID_PASTE: {
        std::string dest = (items.size() == 1 && items[0].is_folder() && is_fs(items[0])) ? items[0].path : g_host->current_dir();
        clipboard_files();
        if (dest.empty() || g_clip.empty()) { MessageBeep(0); return true; }
        std::vector<std::string> dirs = { dest };
        for (const std::string& c : g_clip) dirs.push_back(shell::dir_of(c));
        if (!shell::paste_items(g_clip, dest, g_clip_cut, &err)) error_box(err);
        if (g_clip_cut) { g_clip.clear(); shell::xclip::set_files(g_clip, false); }   // moved: the clipboard is emptied (as GNOME)
        g_host->changed(dirs);
        return true;
    }
    case ID_WALL_STRETCH: case ID_WALL_CENTER: case ID_WALL_TILE: case ID_WALL_FILL: {
        static const char* modes[] = { "stretch", "center", "tile", "fill" };
        if (items.size() == 1) set_wallpaper(items[0].path, modes[id - ID_WALL_STRETCH]);
        return true;
    }
    case ID_DELETE: delete_items(items); return true;
    case ID_RENAME: g_host->begin_rename(); return true;
    case ID_PROPERTIES:
        if (items.empty()) g_host->background_properties();
        else show_properties(items[0]);
        return true;
    case ID_NEW_FOLDER: case ID_NEW_TEXT: {
        std::string dir = g_host->current_dir(), path;
        if (dir.empty()) { MessageBeep(0); return true; }
        bool ok = id == ID_NEW_FOLDER ? shell::new_folder(dir, &path, &err) : shell::new_text_file(dir, &path, &err);
        if (!ok) { error_box(err); return true; }
        g_host->changed({ dir }, path, true);
        return true;
    }
    }
    return false;
}

// ---- menus ----

int thumbs_toggle = -1;

// The view items are adjacent in every menu that has them, but Thumbnails'
// id is not next to the others: the radio group goes by position. With the
// toggle Thumbnails is a check item outside the group.
void check_view_items(HMENU m, int view)
{
    bool toggle = thumbs_toggle >= 0;
    if (toggle && view == VIEW_THUMBNAILS) view = LVS_ICON;
    view &= ~VIEW_THUMBS_FLAG;
    UINT want = view == VIEW_THUMBNAILS ? ID_VIEW_THUMBNAILS : view == LVS_ICON ? ID_VIEW_ICONS : view == LVS_SMALLICON ? ID_VIEW_SMALLICONS : view == LVS_LIST ? ID_VIEW_LIST : ID_VIEW_DETAILS;
    int first = -1, last = -1, at = -1, n = GetMenuItemCount(m);
    for (int i = 0; i < n; i++) {
        UINT id = GetMenuItemID(m, i);
        if (id == ID_VIEW_THUMBNAILS) {
            // a plain check item with the toggle (no radio bullet), XP's radio item without
            MENUITEMINFOW mi = {}; mi.cbSize = sizeof mi; mi.fMask = MIIM_FTYPE | MIIM_STATE;
            GetMenuItemInfoW(m, (UINT)i, TRUE, &mi);
            if (toggle) {
                mi.fType &= ~MFT_RADIOCHECK;
                mi.fState = (mi.fState & ~MFS_CHECKED) | (thumbs_toggle > 0 ? MFS_CHECKED : 0);
                SetMenuItemInfoW(m, (UINT)i, TRUE, &mi);
                continue;
            }
        }
        if (id != ID_VIEW_THUMBNAILS && id != ID_VIEW_ICONS && id != ID_VIEW_SMALLICONS && id != ID_VIEW_LIST && id != ID_VIEW_DETAILS) continue;
        if (first < 0) first = i;
        last = i;
        if (id == want) at = i;
    }
    if (at >= 0) CheckMenuRadioItem(m, (UINT)first, (UINT)last, (UINT)at, MF_BYPOSITION);
}

HMENU view_submenu(int view, bool thumbnails)
{
    HMENU m = CreatePopupMenu();
    bool toggle = thumbnails && thumbs_toggle >= 0;
    if (thumbnails && !toggle) AppendMenuW(m, MF_STRING, ID_VIEW_THUMBNAILS, L"T&humbnails");
    AppendMenuW(m, MF_STRING, ID_VIEW_ICONS, L"Ico&ns");
    AppendMenuW(m, MF_STRING, ID_VIEW_LIST, L"&List");
    AppendMenuW(m, MF_STRING, ID_VIEW_DETAILS, L"&Details");
    if (toggle) {     // 4051's Views drop-down: Preview, a check after the views (lh4051-194532.png)
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, ID_VIEW_THUMBNAILS, L"T&humbnails");
    }
    check_view_items(m, view);
    return m;
}

static void append_open_with(HMENU m, const std::vector<Item>& items)
{
    std::string mime;
    std::vector<std::string> files = same_type(items, &mime);
    if (files.empty()) return;
    HMENU sub = CreatePopupMenu();
    g_openwith = shell::apps_for(mime);
    if (g_openwith.size() > (size_t)(ID_OPENWITH_LAST - ID_OPENWITH_FIRST)) g_openwith.resize((size_t)(ID_OPENWITH_LAST - ID_OPENWITH_FIRST));
    for (size_t i = 0; i < g_openwith.size(); i++) {
        std::string label = g_openwith[i]->name;
        for (size_t k = 0; (k = label.find('&', k)) != std::string::npos; k += 2) label.insert(k, "&");
        AppendMenuW(sub, MF_STRING, ID_OPENWITH_FIRST + (UINT)i, WS(label));
    }
    if (!g_openwith.empty()) AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sub, MF_STRING, ID_OPENWITH_CHOOSE, L"&Choose Program...");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)sub, L"Open Wit&h");
}

// The item menu for g_ctx (list items, or a tree folder).
void build_item_menu(HMENU m)
{
    const std::vector<Item>& items = g_ctx.items;
    if (items.empty()) return;
    bool fs = true, any_file = false, folder0 = items[0].is_folder();
    for (const Item& it : items) { if (!is_fs(it)) fs = false; if (!it.is_folder()) any_file = true; }
    UINT single = items.size() == 1 ? 0 : MF_GRAYED;
    if (g_ctx.node) {
        AppendMenuW(m, MF_STRING, ID_EXPAND, g_ctx.node_expanded ? L"Collapse" : L"Expand");
        AppendMenuW(m, MF_STRING, ID_EXPLORE, L"E&xplore");
        AppendMenuW(m, MF_STRING, ID_OPEN, L"&Open");
        SetMenuDefaultItem(m, ID_EXPAND, FALSE);
    } else {
        AppendMenuW(m, MF_STRING, ID_OPEN, L"&Open");
        if (folder0) AppendMenuW(m, MF_STRING, ID_EXPLORE, L"E&xplore");
        SetMenuDefaultItem(m, ID_OPEN, FALSE);
        if (any_file && !folder0 && fs) append_open_with(m, items);
        if (items.size() == 1 && is_wallpaper_image(items[0])) {
            // XP's Set as Desktop Background (shimgvw), with the Display Properties' positions
            HMENU wall = CreatePopupMenu();
            AppendMenuW(wall, MF_STRING, ID_WALL_STRETCH, L"&Stretch");
            AppendMenuW(wall, MF_STRING, ID_WALL_CENTER, L"&Center");
            AppendMenuW(wall, MF_STRING, ID_WALL_TILE, L"&Tile");
            AppendMenuW(wall, MF_STRING, ID_WALL_FILL, L"&Fill");
            AppendMenuW(m, MF_POPUP, (UINT_PTR)wall, L"Set as Desktop &Background");
        }
    }
    if (!fs) return;   // My Computer's drives etc.: nothing else applies
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_TERMINAL, L"Open &Terminal Here");
    if (folder0) AppendMenuW(m, MF_STRING | single, ID_ADD_BOOKMARK, L"Add to &Bookmarks");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_CUT, L"Cu&t");
    AppendMenuW(m, MF_STRING, ID_COPY, L"&Copy");
    AppendMenuW(m, MF_STRING, ID_COPY_PATH, L"Copy as P&ath");
    if (folder0 && items.size() == 1) AppendMenuW(m, MF_STRING | (!can_paste() ? MF_GRAYED : 0), ID_PASTE, L"&Paste");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_DELETE, L"&Delete");
    AppendMenuW(m, MF_STRING | single, ID_RENAME, L"Rena&me");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | single, ID_PROPERTIES, L"P&roperties");
}

void build_background_menu(HMENU m, unsigned flags, int view, int sort)
{
    bool fs = !g_host->current_dir().empty();
    UINT gray = fs ? 0 : MF_GRAYED;
    if (flags & BG_VIEW) AppendMenuW(m, MF_POPUP, (UINT_PTR)view_submenu(view, (flags & BG_THUMBNAILS) != 0), L"&View");
    if (flags & BG_ARRANGE) {
        HMENU arrange = CreatePopupMenu();
        AppendMenuW(arrange, MF_STRING, ID_SORT_NAME, L"&Name");
        AppendMenuW(arrange, MF_STRING, ID_SORT_SIZE, L"&Size");
        AppendMenuW(arrange, MF_STRING, ID_SORT_TYPE, L"&Type");
        AppendMenuW(arrange, MF_STRING, ID_SORT_DATE, L"&Modified");
        CheckMenuRadioItem(arrange, ID_SORT_NAME, ID_SORT_DATE, ID_SORT_NAME + sort, MF_BYCOMMAND);
        AppendMenuW(m, MF_POPUP, (UINT_PTR)arrange, L"Arrange &Icons By");
        AppendMenuW(m, MF_STRING, ID_VIEW_REFRESH, L"R&efresh");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | ((!can_paste() || !fs) ? MF_GRAYED : 0), ID_PASTE, L"&Paste");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    }
    HMENU nm = CreatePopupMenu();
    AppendMenuW(nm, MF_STRING, ID_NEW_FOLDER, L"&Folder");
    AppendMenuW(nm, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(nm, MF_STRING, ID_NEW_TEXT, L"Text Document");
    AppendMenuW(m, MF_POPUP | gray, (UINT_PTR)nm, L"Ne&w");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | gray, ID_TERMINAL, L"Open &Terminal Here");
    AppendMenuW(m, MF_STRING | gray, ID_ADD_BOOKMARK, L"Add to &Bookmarks");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | gray, ID_PROPERTIES, L"P&roperties");
}

} // namespace verbs
