// flxp-desktop: the desktop of the flxp shell. As in XP, where Explorer owns
// the desktop as a list view in icon mode (shell32 DefView over the Desktop
// folder) whose background is the wallpaper: one toplevel of EWMH type DESKTOP
// covering the screen paints the wallpaper, and a transparent LVS_ICON list
// view over it shows ~/Desktop merged with My Documents, My Computer and the
// Recycle Bin, with white labels and XP's drop shadow. Icons are placed
// freely (no auto-arrange); positions are kept per name in
// $XDG_CONFIG_HOME/flxp/desktop-positions. Folders open in an explorer window
// (a new flxp process, as the explorer itself does for every further folder),
// files with their default application, and the context menus are the
// explorer's (src/explorer/verbs). The desktop never fades: lhwm's maximised
// mode (M5b, x11/SHELL-DESIGN.md) switches only the window's plate and the bars.
//
//   flxp-desktop [--wallpaper FILE] [--wallpaper-mode center|stretch|tile|fill] [--quit-after MS]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include "shell/namespace.h"
#include "shell/apps.h"
#include "shell/config.h"
#include "shell/settings.h"
#include "shell/lhtheme.h"
#include "shell/wallpapers.h"
#include "shell/fileops.h"
#include "shell/runtime.h"
#include "shell/dirwatch.h"
#include "sys/sys.h"
#include "explorer/commands.h"
#include "explorer/dialogs.h"
#include "explorer/uiicons.h"
#include "explorer/verbs.h"
#include "wallpaper.h"
#include "w32/w32.h"
#include "w32/user.h"
extern "C" {
#include "commctrl.h"
#include "commdlg.h"
#include "w32/flxp_shell.h"
}

using shell::Item;
using verbs::g_ctx;
using verbs::Ctx;

namespace {

const uint32_t DEFAULT_BACKGROUND = 0x3a6ea5;    // XP's desktop blue (58,110,165), as the classic screenshots
enum { TIMER_WATCH = 2, TIMER_QUIT = 3, TIMER_FOLLOW = 4 };

struct Desktop {
    HWND wnd = nullptr, list = nullptr;
    int width = 0, height = 0;
    HDC wall_dc = nullptr;              // the composed wallpaper (screen size)
    HBITMAP wall_bmp = nullptr;
    std::string wall_file, wall_mode;   // what wall_bmp shows
    std::vector<Item> items;            // list index = items index
    std::map<std::string, POINT> pos;   // saved icon positions by name
    shell::SortBy sort = shell::SortBy::Name;
    bool auto_arrange = false, align_grid = false;
    // dragging icons (LVN_BEGINDRAG): files are dragged (XDND, FlxpDragFiles) and a drop
    // back on the desktop repositions them; only system icons follow the pointer
    bool dragging = false;
    POINT drag_last = {};
    bool drag_own = false;              // our FlxpDragFiles is running
    POINT drag_start = {};              // where it was grabbed (screen)
    int drop_item = -1;                 // the icon shown as the drop target
    UINT w_workarea = 0, w_wallpaper = 0, w_settings = 0, w_resolved = 0;
    int64_t desk_mtime = 0;             // ~/Desktop, polled for changes (without inotify)
    shell::DirWatch watch;              // ~/Desktop followed with inotify: TIMER_FOLLOW after a change
    bool follow_armed = false;
    std::string select_after;           // after a refill: select (and maybe rename) this path
    bool rename_after = false;
};
Desktop g;

std::u16string W(const std::string& s) { return w32::utf16_of(s.c_str()); }

std::string desktop_dir()
{
    std::string d = shell::home_dir() + "/Desktop";
    struct stat st;
    return stat(d.c_str(), &st) == 0 && S_ISDIR(st.st_mode) ? d : "";
}

std::string trash_dir() { return shell::data_home() + "/Trash/files"; }

std::string positions_path() { return shell::config_home() + "/flxp/desktop-positions"; }

// ---- the explorer -------------------------------------------------------------------

// Folders open in a new explorer process: the explorer keeps one window per
// process (its state is per process, and it starts a new process for every
// further folder itself), and the desktop then survives an explorer crash.
std::string explorer_exe()
{
    std::string dir = shell::exe_dir();
    const char* cands[] = { "/flxp" };   // installed or build/bin, next to us
    for (const char* c : cands) {
        std::string p = dir + c;
        if (access(p.c_str(), X_OK) == 0) return p;
    }
    return "flxp";
}

void open_in_explorer(const std::string& path)
{
    std::string err;
    if (!path.empty() && path[0] == '/') shell::make_dirs(path);   // the Trash may not exist yet
    if (!shell::spawn({ explorer_exe(), path }, shell::home_dir(), &err)) verbs::error_box(err);
}

// ---- wallpaper --------------------------------------------------------------------

// The wallpaper, first match:
// 1. the root property _FLXP_WALLPAPER = "MODE PATH" (Set as Desktop Background,
//    Desktop Background..., or any other tool: an explicit choice for this X
//    session; also beats a theme switch);
// 2. wallpaper= (flxp.ini, or FLXP_WALLPAPER at start-up: a session script's
//    default; after a theme switch only flxp.ini's counts);
// 3. the session theme's (x11/THEMES.md "Wallpapers": [override:NAME], the preset, a themes.ini key, wallpapers.ini;
//    shell/wallpapers.h; fill as lhshell's feh --bg-fill);
// 4. after a switch to a theme without one: the start-up wallpaper.
// XP's plain blue when none of them names a file, or when the first that is
// set says "none" (settings.ini [desktop] wallpaper = none, _FLXP_WALLPAPER
// "none", x11/SETTINGS.md): no image, the theme's wallpaper is not used.
bool root_wallpaper(std::string& file, std::string& mode)
{
    char buf[4096];
    int n = FlxpGetRootString("_FLXP_WALLPAPER", buf, sizeof buf);
    if (n <= 0 || n >= (int)sizeof buf) return false;
    std::string v(buf, (size_t)n);
    size_t sp = v.find(' ');
    if (sp == std::string::npos) { file = v; mode.clear(); }
    else { mode = v.substr(0, sp); file = v.substr(sp + 1); }
    if (mode != "stretch" && mode != "center" && mode != "tile" && mode != "fill" && mode != "fit") {
        if (sp != std::string::npos && !mode.empty() && mode[0] == '/') file = v;   // a bare path with a space
        mode.clear();
    }
    return !file.empty();
}

void make_wallpaper(bool switched = false)
{
    std::string file, mode;
    if (!root_wallpaper(file, mode)) {
        file = switched ? shell::file_setting("wallpaper") : shell::setting("wallpaper");
        mode = switched ? shell::file_setting("wallpaper_mode") : shell::setting("wallpaper_mode");
        const shell::SessionTheme& st = shell::session_theme();
        std::string lc = file;
        for (char& c : lc) c = (char)tolower((unsigned char)c);
        // x11/THEMES.md "Wallpapers": none, the theme's [override:NAME] / preset, the user's file (settings.ini
        // [desktop]; flxp.ini and the environment stay ahead of all), the theme's (themes.ini key, wallpapers.ini)
        bool explicit_file = !file.empty() && file != shell::store::value_for("wallpaper");
        shell::Wallpaper w;
        if (lc != "none" && !explicit_file && shell::own_wallpaper(st, w)) {
            file = w.file;
            mode = w.mode.empty() ? shell::file_setting("wallpaper_mode", "fill") : w.mode;
        } else if (file.empty() && shell::theme_wallpaper(st, w)) {
            file = w.file;
            mode = w.mode.empty() ? "fill" : w.mode;
        } else if (switched && file.empty()) {
            file = shell::setting("wallpaper");          // no theme wallpaper: keep the start-up one
            mode = shell::setting("wallpaper_mode");
        }
    }
    if (file == g.wall_file && mode == g.wall_mode && g.wall_bmp) return;
    g.wall_file = file; g.wall_mode = mode;
    wallpaper::Image img;
    std::string err;
    std::string lc = file;
    for (char& c : lc) c = (char)tolower((unsigned char)c);
    if (!file.empty() && lc != "none") {
        if (file[0] == '~') file = shell::home_dir() + file.substr(1);
        if (!wallpaper::load(file, img, &err)) fprintf(stderr, "flxp-desktop: %s\n", err.c_str());
    }
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = g.width; bi.bmiHeader.biHeight = -g.height;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    if (!g.wall_dc) g.wall_dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateDIBSection(g.wall_dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) return;
    // the whole screen (XP stretches over the monitor, not the work area)
    wallpaper::compose(img, wallpaper::mode_from(mode.empty() ? "stretch" : mode), DEFAULT_BACKGROUND,
                       (uint32_t*)bits, g.width, g.height, 0, 0, g.width, g.height);
    HGDIOBJ old = SelectObject(g.wall_dc, bmp);
    if (g.wall_bmp) DeleteObject(g.wall_bmp); else (void)old;
    g.wall_bmp = bmp;
}

// The wallpaper into dc, whose origin is the screen's.
void paint_background(HDC dc, const RECT& r)
{
    if (g.wall_dc) BitBlt(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, g.wall_dc, r.left, r.top, SRCCOPY);
}

// ---- icon positions --------------------------------------------------------------------

void load_positions()
{
    std::string data;
    g.pos.clear();
    if (!shell::read_file(positions_path(), data)) return;
    size_t i = 0;
    while (i < data.size()) {
        size_t e = data.find('\n', i);
        if (e == std::string::npos) e = data.size();
        std::string line = data.substr(i, e - i);
        i = e + 1;
        if (line.empty() || line[0] == '#') continue;
        int x, y, n = 0;
        if (sscanf(line.c_str(), "%d %d %n", &x, &y, &n) == 2 && n > 0 && (size_t)n < line.size()) g.pos[line.substr((size_t)n)] = POINT{ x, y };
    }
}

void save_positions()
{
    std::string out = "# flxp-desktop icon positions: x y name (screen pixels, the icon's top-left)\n";
    char buf[64];
    for (auto& p : g.pos) { snprintf(buf, sizeof buf, "%ld %ld ", (long)p.second.x, (long)p.second.y); out += buf + p.first + "\n"; }
    shell::write_file_atomic(positions_path(), out);
}

RECT work_rect()
{
    RECT r = { 0, 0, g.width, g.height };
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &r, 0);   // _NET_WORKAREA: without the taskbar/sidebar struts
    return r;
}

SIZE spacing()
{
    DWORD s = (DWORD)SendMessageW(g.list, LVM_GETITEMSPACING, FALSE, 0);
    SIZE sz = { (LONG)LOWORD(s), (LONG)HIWORD(s) };
    if (sz.cx <= 0) sz.cx = 75;
    if (sz.cy <= 0) sz.cy = 75;
    return sz;
}

// Grid cell (column-major from the work area's top-left, as XP arranges the
// desktop) -> the icon's top-left in list coordinates (= screen).
POINT cell_pos(int col, int row)
{
    RECT wa = work_rect();
    SIZE sp = spacing();
    return POINT{ wa.left + col * sp.cx + (sp.cx - uiicons::large_size()) / 2, wa.top + row * sp.cy + 6 };
}

int rows_per_column()
{
    RECT wa = work_rect();
    int n = (wa.bottom - wa.top - 6) / spacing().cy;
    return n > 0 ? n : 1;
}

POINT snap(POINT p)
{
    RECT wa = work_rect();
    SIZE sp = spacing();
    int col = (p.x - wa.left + sp.cx / 2 - (sp.cx - uiicons::large_size()) / 2) / sp.cx, row = (p.y - wa.top - 6 + sp.cy / 2) / sp.cy;
    if (col < 0) col = 0;
    if (row < 0) row = 0;
    if (row >= rows_per_column()) row = rows_per_column() - 1;
    return cell_pos(col, row);
}

// keep an icon inside the work area
POINT clamp_to_work(POINT p)
{
    RECT wa = work_rect();
    SIZE sp = spacing();
    if (p.x > wa.right - sp.cx + (sp.cx - uiicons::large_size()) / 2) p.x = wa.right - sp.cx + (sp.cx - uiicons::large_size()) / 2;
    if (p.y > wa.bottom - sp.cy) p.y = wa.bottom - sp.cy;
    if (p.x < wa.left) p.x = wa.left;
    if (p.y < wa.top) p.y = wa.top;
    return p;
}

void set_item_pos(int i, POINT p) { SendMessageW(g.list, LVM_SETITEMPOSITION32, (WPARAM)i, (LPARAM)&p); }

POINT item_pos(int i)
{
    POINT p = {};
    SendMessageW(g.list, LVM_GETITEMPOSITION, (WPARAM)i, (LPARAM)&p);
    return p;
}

void remember_all()
{
    for (size_t i = 0; i < g.items.size(); i++) g.pos[g.items[i].name] = item_pos((int)i);
    save_positions();
}

// Arrange: every icon into the grid in list order (Arrange Icons By, Auto Arrange).
void arrange()
{
    int rows = rows_per_column();
    for (size_t i = 0; i < g.items.size(); i++) set_item_pos((int)i, cell_pos((int)i / rows, (int)i % rows));
    remember_all();
}

// Items without a saved position go to the first free grid cells.
void place_new(const std::vector<int>& fresh)
{
    if (fresh.empty()) return;
    int rows = rows_per_column();
    SIZE sp = spacing();
    std::vector<POINT> taken;
    for (size_t i = 0; i < g.items.size(); i++)
        if (std::find(fresh.begin(), fresh.end(), (int)i) == fresh.end()) taken.push_back(item_pos((int)i));
    int cell = 0;
    for (int i : fresh) {
        for (;; cell++) {
            POINT c = cell_pos(cell / rows, cell % rows);
            bool used = false;
            for (const POINT& t : taken) if (abs(t.x - c.x) < sp.cx / 2 && abs(t.y - c.y) < sp.cy / 2) used = true;
            if (!used) break;
        }
        POINT c = cell_pos(cell / rows, cell % rows);
        set_item_pos(i, c);
        taken.push_back(c);
        g.pos[g.items[(size_t)i].name] = c;
    }
    save_positions();
}

// ---- items ---------------------------------------------------------------------

Item virtual_item(const std::string& name, const std::string& path, const std::string& icon)
{
    Item it;
    it.name = name; it.path = path; it.icon = icon;
    it.flags = shell::IsFolder | shell::IsVirtual;
    it.type = "System Folder";
    return it;
}

void fill()
{
    load_positions();   // the file is the truth (every change is saved at once; another instance may have moved icons)
    std::vector<Item> items;
    // XP's order: My Documents, My Computer, (My Network Places), Recycle Bin, then the Desktop folder
    items.push_back(virtual_item("My Documents", shell::home_dir(), "home"));
    items.push_back(virtual_item("My Computer", "::computer", "computer"));
    items.push_back(virtual_item("Recycle Bin", trash_dir(), "trash"));
    std::string dd = desktop_dir();
    if (!dd.empty()) {
        auto f = shell::folder_for(dd);
        std::vector<Item> files;
        if (f) f->enumerate(files, false);
        shell::sort_items(files, g.sort, true);
        items.insert(items.end(), files.begin(), files.end());
        struct stat st;
        g.desk_mtime = stat(dd.c_str(), &st) == 0 ? (int64_t)st.st_mtime : 0;
    }
    g.items = items;
    SendMessageW(g.list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g.list, LVM_DELETEALLITEMS, 0, 0);
    std::vector<int> fresh;
    for (size_t i = 0; i < g.items.size(); i++) {
        const Item& it = g.items[i];
        LVITEMW lvi = {};
        lvi.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
        lvi.iItem = (int)i;
        std::u16string name = W(it.name);
        lvi.pszText = (LPWSTR)name.c_str();
        lvi.iImage = uiicons::index(it.icon);
        lvi.lParam = (LPARAM)i;
        if (verbs::is_cut(it.path)) { lvi.mask |= LVIF_STATE; lvi.stateMask = LVIS_CUT; lvi.state = LVIS_CUT; }
        SendMessageW(g.list, LVM_INSERTITEMW, 0, (LPARAM)&lvi);
        auto p = g.pos.find(it.name);
        if (p != g.pos.end() && !g.auto_arrange) set_item_pos((int)i, clamp_to_work(p->second));
        else fresh.push_back((int)i);
    }
    if (g.auto_arrange) arrange();
    else place_new(fresh);
    SendMessageW(g.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g.list, nullptr, TRUE);
    if (!g.select_after.empty()) {
        for (size_t i = 0; i < g.items.size(); i++) {
            if (g.items[i].path != g.select_after) continue;
            LVITEMW st = {}; st.stateMask = LVIS_SELECTED | LVIS_FOCUSED; st.state = st.stateMask;
            SendMessageW(g.list, LVM_SETITEMSTATE, (WPARAM)i, (LPARAM)&st);
            if (g.rename_after) { SetFocus(g.list); SendMessageW(g.list, LVM_EDITLABELW, (WPARAM)i, 0); }
        }
        g.select_after.clear();
        g.rename_after = false;
    }
}

// ~/Desktop changed (inotify): the icons again, the selection kept (the
// positions are kept by fill() itself, by name)
void refill_keeping_selection()
{
    std::vector<std::string> sel;
    int i = -1;
    while ((i = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)i, LVNI_SELECTED)) >= 0)
        if (i < (int)g.items.size()) sel.push_back(g.items[(size_t)i].path);
    i = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_FOCUSED);
    std::string focus = i >= 0 && i < (int)g.items.size() ? g.items[(size_t)i].path : "";
    fill();
    for (size_t k = 0; k < g.items.size(); k++) {
        bool s = std::find(sel.begin(), sel.end(), g.items[k].path) != sel.end(), f = g.items[k].path == focus;
        if (!s && !f) continue;
        LVITEMW st = {}; st.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
        st.state = (s ? LVIS_SELECTED : 0) | (f ? LVIS_FOCUSED : 0);
        SendMessageW(g.list, LVM_SETITEMSTATE, (WPARAM)k, (LPARAM)&st);
    }
}

static void desktop_ready(int, void*)
{
    if (!g.watch.take() || g.follow_armed) return;
    g.follow_armed = true;
    SetTimer(g.wnd, TIMER_FOLLOW, 150, nullptr);   // a burst is one refill
}

// follow ~/Desktop with inotify (the 2 s mtime poll stays for when that is not possible)
void follow_desktop()
{
    std::string dd = desktop_dir();
    if (dd.empty() || !g.watch.watch(dd)) return;
    sys::watch_fd(g.watch.fd(), desktop_ready, nullptr);
}

std::vector<Item> selection()
{
    std::vector<Item> out;
    int i = -1;
    while ((i = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)i, LVNI_SELECTED)) >= 0)
        if (i < (int)g.items.size()) out.push_back(g.items[(size_t)i]);
    return out;
}

int list_index(const std::string& path)
{
    for (size_t i = 0; i < g.items.size(); i++) if (g.items[i].path == path) return (int)i;
    return -1;
}

// ---- wallpaper changes -----------------------------------------------------------------

void wallpaper_changed(bool switched)
{
    if (!g.wall_bmp) return;
    HBITMAP was = g.wall_bmp;
    make_wallpaper(switched);
    if (g.wall_bmp != was) {
        RedrawWindow(g.wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
}

// Paste Shortcut (XP): a symbolic link on the desktop to each copied or cut
// item, "Shortcut to NAME" when the name is taken
void paste_shortcuts()
{
    std::string dd = desktop_dir();
    if (dd.empty()) return;
    std::string last;
    for (const std::string& p : verbs::clipboard_files()) {
        std::string name = shell::base_of(p), to = dd + "/" + name;
        struct stat st;
        for (int n = 1; lstat(to.c_str(), &st) == 0; n++)
            to = dd + "/Shortcut" + (n > 1 ? " (" + std::to_string(n) + ")" : std::string()) + " to " + name;
        if (symlink(p.c_str(), to.c_str()) != 0) { verbs::error_box("Cannot create the shortcut " + to + "."); break; }
        last = to;
    }
    if (!last.empty()) fill();
}

// ---- the verbs' host -------------------------------------------------------------------

struct DesktopHost : verbs::Host {
    HWND window() override { return g.wnd; }
    std::string current_dir() override { return desktop_dir(); }
    Item folder_item() override
    {
        Item it;
        it.path = desktop_dir(); it.name = "Desktop"; it.flags = shell::IsFolder; it.type = "File Folder"; it.icon = "desktop";
        return it;
    }
    void open_folder(const Item& it, bool) override { open_in_explorer(it.path); }
    void changed(const std::vector<std::string>&, const std::string& select, bool rename) override
    {
        g.select_after = select;
        g.rename_after = rename;
        fill();
    }
    void begin_rename() override
    {
        if (g_ctx.items.size() != 1) return;
        int i = list_index(g_ctx.items[0].path);
        if (i < 0) return;
        SetFocus(g.list);
        SendMessageW(g.list, LVM_EDITLABELW, (WPARAM)i, 0);
    }
    void clip_changed() override
    {
        for (size_t i = 0; i < g.items.size(); i++) {
            LVITEMW st = {}; st.stateMask = LVIS_CUT; st.state = verbs::is_cut(g.items[i].path) ? LVIS_CUT : 0;
            SendMessageW(g.list, LVM_SETITEMSTATE, (WPARAM)i, (LPARAM)&st);
        }
    }
    void background_properties() override
    {
        // XP: Display Properties (desk.cpl): flxp-settings, on its first tab (Themes) as XP's
        if (!shell::store::launch_settings())
            dialogs::message(g.wnd, "Display Properties", "Cannot start " + shell::store::settings_program() + ".", false);
    }
};
DesktopHost g_host;

// ---- menus ---------------------------------------------------------------------

void command(int cmd)
{
    switch (cmd) {
    case ID_SORT_NAME: case ID_SORT_SIZE: case ID_SORT_TYPE: case ID_SORT_DATE:
        // XP: Arrange Icons By sorts once and lays the icons out in the grid
        g.sort = (shell::SortBy)(cmd - ID_SORT_NAME);
        fill();
        arrange();
        break;
    case ID_VIEW_REFRESH: fill(); break;
    case ID_DESK_PASTELINK: paste_shortcuts(); break;
    case ID_DESK_AUTOARRANGE:
        g.auto_arrange = !g.auto_arrange;
        if (g.auto_arrange) arrange();
        break;
    case ID_DESK_ALIGN:
        g.align_grid = !g.align_grid;
        if (g.align_grid) {
            for (size_t i = 0; i < g.items.size(); i++) set_item_pos((int)i, snap(item_pos((int)i)));
            remember_all();
        }
        break;
    default:
        verbs::run(cmd);
    }
}

void context_menu(POINT pt)
{
    bool keyboard = pt.x == -1 && pt.y == -1;
    g_ctx = Ctx();
    HMENU m = CreatePopupMenu();
    bool on_item;
    if (keyboard) {
        int f = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED | LVNI_FOCUSED);
        if (f < 0) f = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
        on_item = f >= 0;
        RECT r = {}; r.left = LVIR_ICON;
        if (on_item && SendMessageW(g.list, LVM_GETITEMRECT, (WPARAM)f, (LPARAM)&r)) { pt.x = (r.left + r.right) / 2; pt.y = (r.top + r.bottom) / 2; }
        else { RECT wa = work_rect(); pt.x = wa.left + 4; pt.y = wa.top + 4; }
    } else {
        LVHITTESTINFO hti = {}; hti.pt = pt;
        on_item = SendMessageW(g.list, LVM_HITTEST, 0, (LPARAM)&hti) >= 0 && (hti.flags & LVHT_ONITEM);
    }
    if (on_item) {
        g_ctx.items = selection();
        if (g_ctx.items.empty()) { DestroyMenu(m); return; }
        verbs::build_item_menu(m);
    } else {
        verbs::build_background_menu(m, verbs::BG_ARRANGE, LVS_ICON, (int)g.sort);
        // XP's desktop: Paste Shortcut after Paste; no Add to Bookmarks (Properties = Display Properties)
        int paste = -1;
        for (int i = 0; i < GetMenuItemCount(m); i++) if (GetMenuItemID(m, i) == ID_PASTE) paste = i;
        if (paste >= 0)
            InsertMenuW(m, (UINT)paste + 1, MF_BYPOSITION | MF_STRING | (!verbs::can_paste() ? MF_GRAYED : 0), ID_DESK_PASTELINK, L"Paste &Shortcut");
        DeleteMenu(m, ID_ADD_BOOKMARK, MF_BYCOMMAND);
        // Properties = Display Properties: needs no ~/Desktop (the menu greys the folder items without one)
        EnableMenuItem(m, ID_PROPERTIES, MF_BYCOMMAND | MF_ENABLED);
        HMENU arr = GetSubMenu(m, 0);   // Arrange Icons By: XP's desktop adds these
        AppendMenuW(arr, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(arr, MF_STRING | (g.auto_arrange ? MF_CHECKED : 0), ID_DESK_AUTOARRANGE, L"&Auto Arrange");
        AppendMenuW(arr, MF_STRING | (g.align_grid ? MF_CHECKED : 0), ID_DESK_ALIGN, L"Align to &Grid");
    }
    int cmd = TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, g.wnd, nullptr);
    DestroyMenu(m);
    if (cmd > 0) command(cmd);
}

// ---- notifications --------------------------------------------------------------------

LRESULT on_notify(NMHDR* nm)
{
    if (nm->hwndFrom != g.list) return 0;
    switch (nm->code) {
    case NM_DBLCLK: {
        NMITEMACTIVATE* ia = (NMITEMACTIVATE*)nm;
        if (ia->iItem >= 0 && ia->iItem < (int)g.items.size()) verbs::open_items({ g.items[(size_t)ia->iItem] });
        return 0;
    }
    case LVN_KEYDOWN: {
        NMLVKEYDOWN* kd = (NMLVKEYDOWN*)nm;
        g_ctx = Ctx(); g_ctx.items = selection();
        if (kd->wVKey == VK_RETURN) { if (!g_ctx.items.empty()) verbs::run(ID_OPEN); }
        else if (kd->wVKey == VK_DELETE) verbs::run(ID_DELETE);
        else if (kd->wVKey == VK_F2) verbs::run(ID_RENAME);
        return 0;
    }
    case LVN_BEGINDRAG: {
        NMLISTVIEW* lv = (NMLISTVIEW*)nm;
        std::vector<Item> items, sel = selection();
        std::vector<int> images;
        for (const Item& it : sel) if (verbs::is_fs(it)) { items.push_back(it); images.push_back(uiicons::index(it.icon)); }
        if (items.empty()) {
            // only system icons (My Computer, ...): they follow the pointer
            g.dragging = true;
            GetCursorPos(&g.drag_last);
            SetCapture(g.wnd);
            return 0;
        }
        g.drag_start = lv->ptAction;
        ClientToScreen(g.list, &g.drag_start);
        g.drag_own = true;
        verbs::drag_items(g.list, items, uiicons::large(), images);
        g.drag_own = false;
        return 0;
    }
    case LVN_BEGINLABELEDITW: {
        NMLVDISPINFOW* di = (NMLVDISPINFOW*)nm;
        int i = di->item.iItem;
        return i < 0 || i >= (int)g.items.size() || !verbs::is_fs(g.items[(size_t)i]);   // TRUE cancels
    }
    case LVN_ENDLABELEDITW: {
        NMLVDISPINFOW* di = (NMLVDISPINFOW*)nm;
        int i = di->item.iItem;
        if (!di->item.pszText || i < 0 || i >= (int)g.items.size()) return FALSE;
        std::string from = g.items[(size_t)i].path, to, err;
        std::string name = w32::utf8_of((const WCHAR*)di->item.pszText);
        if (name == g.items[(size_t)i].name) return FALSE;
        if (!shell::rename_item(from, name, &to, &err)) { verbs::error_box(err); return FALSE; }
        g.pos[shell::base_of(to)] = item_pos(i);   // the icon stays where it is
        g.pos.erase(g.items[(size_t)i].name);
        save_positions();
        verbs::clip_renamed(from, to);
        g.select_after = to;
        PostMessageW(g.wnd, WM_APP, 0, 0);   // refill after the list view is done with its edit control
        return TRUE;
    }
    }
    return 0;
}

// ---- drops (WM_FLXP_DRAGOVER / WM_FLXP_DROP) ----------------------------------------

void drop_highlight(int i)
{
    if (i == g.drop_item) return;
    LVITEMW st = {}; st.stateMask = LVIS_DROPHILITED;
    if (g.drop_item >= 0 && g.drop_item < (int)g.items.size()) SendMessageW(g.list, LVM_SETITEMSTATE, (WPARAM)g.drop_item, (LPARAM)&st);
    st.state = LVIS_DROPHILITED;
    if (i >= 0) SendMessageW(g.list, LVM_SETITEMSTATE, (WPARAM)i, (LPARAM)&st);
    g.drop_item = i;
}

// A folder icon takes the files (not the ones being dragged); elsewhere our own
// icons are repositioned and other files go into ~/Desktop, their icons where
// they were dropped.
LRESULT drop_message(UINT msg, const FLXPDROP* d)
{
    if (msg == WM_FLXP_DRAGLEAVE) { drop_highlight(-1); return 0; }
    bool drop = msg == WM_FLXP_DROP;
    LVHITTESTINFO ht = {};
    ht.pt = { d->x, d->y };
    ScreenToClient(g.list, &ht.pt);
    int i = (int)SendMessageW(g.list, LVM_HITTEST, 0, (LPARAM)&ht);
    bool own = g.drag_own && d->fLocal;
    if (i >= 0 && i < (int)g.items.size()) {
        const Item& it = g.items[(size_t)i];
        bool dragged = own && (SendMessageW(g.list, LVM_GETITEMSTATE, (WPARAM)i, LVIS_SELECTED) & LVIS_SELECTED);
        if (it.is_folder() && !it.path.empty() && it.path[0] == '/' && it.path != trash_dir() && !dragged) {
            DWORD e = verbs::drop_effect(d, it.path);
            drop_highlight(e && !drop ? i : -1);
            return drop && e ? verbs::drop(d, it.path) : e;
        }
    }
    drop_highlight(-1);
    if (own) {
        if (drop) {
            int dx = d->x - g.drag_start.x, dy = d->y - g.drag_start.y;
            int k = -1;
            while ((k = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)k, LVNI_SELECTED)) >= 0) {
                POINT q = item_pos(k);
                q.x += dx; q.y += dy;
                q = clamp_to_work(q);
                if (g.align_grid || g.auto_arrange) q = snap(q);
                set_item_pos(k, q);
            }
            if (g.auto_arrange) arrange(); else remember_all();
        }
        return DROPEFFECT_MOVE;
    }
    std::string dd = desktop_dir();
    DWORD e = verbs::drop_effect(d, dd);
    if (!drop || !e) return e;
    // the new icons where they were dropped (one under the other), unless the name has a place already
    POINT at = ht.pt;
    SIZE sp = spacing();
    at.x -= sp.cx / 2; at.y -= 16;
    for (UINT k = 0; k < d->cFiles; k++) {
        std::string name = shell::base_of(d->ppszFiles[k]);
        if (g.pos.find(name) == g.pos.end()) g.pos[name] = clamp_to_work(at);
        at.y += sp.cy;
    }
    save_positions();
    return verbs::drop(d, dd);
}

void drag_move(bool done)
{
    POINT p; GetCursorPos(&p);
    int dx = p.x - g.drag_last.x, dy = p.y - g.drag_last.y;
    g.drag_last = p;
    int i = -1;
    while ((i = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)i, LVNI_SELECTED)) >= 0) {
        POINT q = item_pos(i);
        q.x += dx; q.y += dy;
        if (done) { q = clamp_to_work(q); if (g.align_grid || g.auto_arrange) q = snap(q); }
        if (dx || dy || done) set_item_pos(i, q);
    }
    if (done) {
        if (g.auto_arrange) arrange();
        else remember_all();
    }
}

LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        g.wnd = h;
        g.list = CreateWindowExW(0, WC_LISTVIEWW, nullptr,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | LVS_ICON | LVS_SHAREIMAGELISTS | LVS_EDITLABELS,
            0, 0, g.width, g.height, h, (HMENU)IDC_LIST, nullptr, nullptr);
        LOGFONTW lf = {};
        SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof lf, &lf, 0);   // XP: the icon title font
        SendMessageW(g.list, WM_SETFONT, (WPARAM)CreateFontIndirectW(&lf), FALSE);
        SendMessageW(g.list, LVM_SETIMAGELIST, LVSIL_NORMAL, (LPARAM)uiicons::large());
        SendMessageW(g.list, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)uiicons::small());
        // transparent: the list asks us (WM_ERASEBKGND) for its background; white labels with XP's shadow
        SendMessageW(g.list, LVM_SETBKCOLOR, 0, (LPARAM)CLR_NONE);
        SendMessageW(g.list, LVM_SETTEXTBKCOLOR, 0, (LPARAM)CLR_NONE);
        SendMessageW(g.list, LVM_SETTEXTCOLOR, 0, (LPARAM)RGB(255, 255, 255));
        SendMessageW(g.list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_TRANSPARENTSHADOWTEXT, LVS_EX_TRANSPARENTSHADOWTEXT);
        return 0;
    }
    case WM_ERASEBKGND: {
        // also the list view's (it forwards its WM_ERASEBKGND with its own DC: same origin, it covers us)
        HDC dc = (HDC)wp;
        RECT r;
        if (GetClipBox(dc, &r) == NULLREGION) return 1;
        paint_background(dc, r);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (ps.fErase) paint_background(dc, ps.rcPaint);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_NOTIFY: return on_notify((NMHDR*)lp);
    case WM_FLXP_DRAGOVER:
    case WM_FLXP_DROP:
    case WM_FLXP_DRAGLEAVE:
        return drop_message(msg, (const FLXPDROP*)lp);
    case WM_CONTEXTMENU: {
        POINT pt = { (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
        context_menu(pt);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g.dragging) drag_move(false);
        return 0;
    case WM_LBUTTONUP:
        if (g.dragging) { g.dragging = false; ReleaseCapture(); drag_move(true); }
        return 0;
    case WM_CAPTURECHANGED:
        if (g.dragging) { g.dragging = false; drag_move(true); }
        return 0;
    case WM_SETFOCUS:
        if (IsWindowVisible(g.list)) SetFocus(g.list);
        return 0;
    case WM_APP:
        fill();
        return 0;
    case WM_THEMECHANGED:
        // the session theme's wallpaper (unless _FLXP_WALLPAPER or flxp.ini names one)
        wallpaper_changed(true);
        if (uiicons::sync()) InvalidateRect(g.list, nullptr, TRUE);   // the theme's icon set
        break;
    case WM_FLXP_ROOTPROPERTY:
        // _LH_SETTINGS_CHANGED: settings.ini's [desktop] wallpaper/mode (shell/settings.h), the icon set (per theme)
        if ((UINT)wp == g.w_wallpaper || (UINT)wp == g.w_settings || (UINT)wp == g.w_resolved) wallpaper_changed(true);
        if ((UINT)wp == g.w_settings && uiicons::sync()) InvalidateRect(g.list, nullptr, TRUE);
        if ((UINT)wp == g.w_workarea) {
            // the struts moved: keep every icon inside the new work area
            for (size_t i = 0; i < g.items.size(); i++) set_item_pos((int)i, clamp_to_work(item_pos((int)i)));
            if (g.auto_arrange) arrange();
        }
        return 0;
    case WM_TIMER:
        if (wp == TIMER_FOLLOW) {
            KillTimer(h, TIMER_FOLLOW);
            if (SendMessageW(g.list, LVM_GETEDITCONTROL, 0, 0)) { SetTimer(h, TIMER_FOLLOW, 300, nullptr); return 0; }   // renaming: later
            g.follow_armed = false;
            refill_keeping_selection();
            follow_desktop();   // (again: the folder may have been made anew)
        } else if (wp == TIMER_WATCH) {
            std::string dd = desktop_dir();
            struct stat st;
            int64_t m = !dd.empty() && stat(dd.c_str(), &st) == 0 ? (int64_t)st.st_mtime : 0;
            if (m != g.desk_mtime && !SendMessageW(g.list, LVM_GETEDITCONTROL, 0, 0)) fill();
        } else if (wp == TIMER_QUIT) DestroyWindow(h);
        return 0;
    case WM_CLOSE:
        return 0;   // Alt+F4 on the desktop does not close it (XP shows Shut Down instead)
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void usage(FILE* f)
{
    fprintf(f,
        "usage: flxp-desktop [options]\n"
        "The flxp desktop: wallpaper and icons (~/Desktop, My Documents, My Computer,\n"
        "Recycle Bin) in a window of type _NET_WM_WINDOW_TYPE_DESKTOP covering the screen.\n"
        "  --wallpaper FILE          BMP, PNG or JPEG (setting wallpaper=)\n"
        "  --wallpaper-mode MODE     stretch, center, tile or fill (setting wallpaper_mode=)\n"
        "  --theme NAME|PATH, --classic, --icons NAME|PATH   as for flxp\n"
        "  --quit-after MS           close after MS milliseconds (testing)\n");
}

} // namespace

int main(int argc, char** argv)
{
    int quit_ms = 0;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto value = [&]() -> const char* {
            if (i + 1 >= argc) { fprintf(stderr, "flxp-desktop: %s needs a value\n", a.c_str()); exit(2); }
            return argv[++i];
        };
        if (a == "--help" || a == "-h") { usage(stdout); return 0; }
        else if (a == "--wallpaper") setenv("FLXP_WALLPAPER", value(), 1);
        else if (a == "--wallpaper-mode") setenv("FLXP_WALLPAPER_MODE", value(), 1);
        else if (a == "--theme") setenv("FLXP_THEME", value(), 1), unsetenv("FLXP_THEME_FOLLOWED");
        else if (a == "--classic") setenv("FLXP_THEME", "classic", 1), unsetenv("FLXP_THEME_FOLLOWED");
        else if (a == "--icons") setenv("FLXP_ICONS", value(), 1);
        else if (a == "--quit-after") quit_ms = atoi(value());
        else { fprintf(stderr, "flxp-desktop: unknown option %s\n", a.c_str()); usage(stderr); return 2; }
    }
    shell::write_settings_template();
    shell::setup_fonts();
    shell::setup_theme();
    w32::init();
    // XP's Display Properties > Appearance > Effects > "Use large icons": 48 (desktop_icon_size = 32|48)
    uiicons::init(atoi(shell::setting("desktop_icon_size", "32").c_str()));
    verbs::set_host(&g_host);
    load_positions();
    g.width = GetSystemMetrics(SM_CXSCREEN);
    g.height = GetSystemMetrics(SM_CYSCREEN);
    make_wallpaper();
    // desktop_fade_ms (the fade to black in lhwm's maximised mode) is gone with M5b: ignored
    g.w_workarea = FlxpWatchRootProperty("_NET_WORKAREA");
    g.w_wallpaper = FlxpWatchRootProperty("_FLXP_WALLPAPER");
    g.w_settings = FlxpWatchRootProperty("_LH_SETTINGS_CHANGED");
    g.w_resolved = FlxpWatchRootProperty("_LH_THEME_RESOLVED");   // lux's wallpaper=/mode= for the theme in force

    WNDCLASSW wc = {};
    wc.lpfnWndProc = proc;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"flxp_desktop";
    RegisterClassW(&wc);
    // XP's desktop window is "Program Manager" (Progman)
    HWND h = CreateWindowExW(WS_EX_FLXP_DESKTOP, L"flxp_desktop", L"Program Manager", WS_POPUP | WS_CLIPCHILDREN,
                             0, 0, g.width, g.height, nullptr, nullptr, nullptr, nullptr);
    if (!h) { fprintf(stderr, "flxp-desktop: no window\n"); return 1; }
    fill();
    follow_desktop();
    ShowWindow(h, SW_SHOW);
    SetFocus(g.list);
    SetTimer(h, TIMER_WATCH, 2000, nullptr);
    if (quit_ms) SetTimer(h, TIMER_QUIT, (UINT)quit_ms, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        bool in_list = GetFocus() == g.list;
        if (in_list && ((msg.message == WM_KEYDOWN && msg.wParam == VK_F10 && (GetKeyState(VK_SHIFT) & 0x8000)) ||
                        (msg.message == WM_KEYUP && msg.wParam == VK_APPS))) { context_menu(POINT{ -1, -1 }); continue; }
        if (in_list && msg.message == WM_KEYDOWN && msg.wParam == VK_APPS) continue;
        if (in_list && msg.message == WM_KEYUP && msg.wParam == VK_F10 && (GetKeyState(VK_SHIFT) & 0x8000)) continue;
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_F5) { fill(); continue; }
        if (in_list && msg.message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (msg.wParam == 'A') {
                LVITEMW lvi = {}; lvi.stateMask = LVIS_SELECTED; lvi.state = LVIS_SELECTED;
                SendMessageW(g.list, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&lvi);
                continue;
            }
            if (msg.wParam == 'X' || msg.wParam == 'C') { g_ctx = Ctx(); g_ctx.items = selection(); verbs::run(msg.wParam == 'X' ? ID_CUT : ID_COPY); continue; }
            if (msg.wParam == 'V') { g_ctx = Ctx(); verbs::run(ID_PASTE); continue; }
        }
        if (in_list && msg.message == WM_SYSKEYDOWN && msg.wParam == VK_RETURN) { g_ctx = Ctx(); g_ctx.items = selection(); verbs::run(ID_PROPERTIES); continue; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
