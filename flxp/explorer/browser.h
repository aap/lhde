// The browsing widget shared by the explorer frame and the file dialog: a
// folder shown in a list view with its views (Thumbnails, Tiles, Icons, Small
// Icons, List, Details; optionally grouped by type), sorting, the filter-as-you-type
// of the Longhorn search/Filter boxes, Back/Forward history and the views
// remembered per folder; plus the frame plumbing every Longhorn host needs:
// which frame style the session asks for (the theme's Explorer.chrome,
// luxwm), luxwm's glass extension under the header (header.h) and the caption
// behaviour of the header's empty parts.
//
// A host derives from Browser (the explorer's Explorer, the dialog's Dlg),
// creates the list with create_list() and overrides the hooks it needs. The
// other parts of the widget are their own modules with callbacks: header.h
// (Back/Forward, breadcrumb, search), places.h (the left pane's collapsible
// groups), taskpane.h (the task band), lhbar.h (the 4051 command bar),
// thumbview.h, verbs.h (context menus, drag and drop).
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "shell/namespace.h"
#include "shell/dirwatch.h"
#include "shell/viewmodel.h"
#include "w32/w32.h"
#include "commands.h"
extern "C" {
#include "commctrl.h"
#include "w32/flxp_shell.h"
}

namespace browser {

using shell::Item;

// ---- the frame ---------------------------------------------------------------------

// The session theme's Explorer.chrome (xp | longhorn | auto; Explorer.navbar =
// none: xp; shell::theme_chrome).
// auto: longhorn whenever luxwm runs (it publishes _LHWM_COMPOSITED,
// _LHWM_THEME or _LHWM_FRAME_METRICS), glass or not, else xp. `why` says what
// decided.
bool frame_auto();
bool want_longhorn(std::string* why = nullptr);

// luxwm's glass under the header: the extension by header::height(), the
// NO_TITLE / NO_ICON (and on glass CLIENT_CAPTION) frame flags, the input hole
// over luxwm's caption buttons; unextend_frame() is the way back to an ordinary
// frame (luxwm's caption with title and icon).
void extend_frame(HWND frame);
void caption_hole(HWND frame);
void unextend_frame(HWND frame);
// The header's height follows the frame (glass: 76 rows less luxwm's caption)
// or the theme's navigation bar; then the extension, the host's layout and
// the header's look follow.
void header_refit(HWND frame, void (*layout)());
// WM_NCHITTEST of a frame with a header: the header's empty parts (it answers
// HTTRANSPARENT there) are the caption. False when the frame has no header.
bool header_hittest(HWND frame, LPARAM lp, LRESULT* result);
// Active or inactive as the window manager draws the frame (_NET_ACTIVE_WINDOW
// through luxwm), else the focus state the host tracks (WM_ACTIVATE).
bool frame_active(HWND frame, bool focus_active);
// The window's icon follows the folder (4051's CShellBrowser2::v_SetIcon): the
// icon theme's key at 16, 32 and 48 px as WM_SETICON (the shim publishes them
// as _NET_WM_ICON for the window manager and the taskbar)
void set_window_icon(HWND frame, const std::string& key);

// ---- the list --------------------------------------------------------------------


struct Browser {
    virtual ~Browser();                              // unfollow()
    HWND list = nullptr;
    HWND header = nullptr;                           // the Longhorn header, when the host has one
    HIMAGELIST small = nullptr, large = nullptr;     // the list's image lists (thumbview gives `large` back)
    std::shared_ptr<shell::Folder> folder;
    std::vector<Item> items;                         // what the list shows: all_items through visible() and the filter
    std::vector<Item> all_items;
    std::string filter;                              // the header's search box (lower case), "" = all
    std::vector<std::string> back, forward;
    int view = LVS_REPORT;
    shell::SortBy sort = shell::SortBy::Name;
    bool ascending = true;
    bool show_hidden = false;
    std::string select_after_refresh;                // path to select (and maybe rename) after a refill
    bool rename_after_refresh = false;
    bool filling = false;                            // fill_list() at work (selection notifications to ignore)
    bool remember_views = true;                      // the views file and per-window memory (the explorer)
    bool select_parent = false;                      // going up selects the folder we came from (the file dialog, XP)
    bool record_same = true;                         // navigate() to the folder shown still records history (the explorer)
    bool grouped = false;                            // group view by type in the icon views (LVM_ENABLEGROUPVIEW)
    // Thumbnails: XP's view (false: choosing another view ends it); in the
    // Longhorn hosts thumbnails show in every view unless the thumbnails setting
    // (shell/thumbs.h enabled_for) is off for the folder
    bool thumbs = false;
    // The Longhorn hosts (4074's model, shell/viewmodel.h): Details (view
    // LVS_REPORT), Tiles (LVS_ICON in LV_VIEW_TILE) and Icons (LVS_ICON), each
    // at an image size, zoom; Hide File Names in Icons. Remembered per folder.
    bool zoomable = false;
    int zoom = 0;
    bool tiles = false;                              // Tiles: the Longhorn hosts' or XP's (LV_VIEW_TILE at 48 px)
    bool hide_names = false;
    // following the folder on disk (inotify, browser.cpp follow/update); off: F5 only
    bool follow_disk = true;
    std::unique_ptr<shell::DirWatch> dirwatch;
    int follow_pending = 0;                          // DirWatch bits since the last update
    bool follow_armed = false;                       // the coalescing timer runs
    std::map<std::string, bool> collapsed;           // groups folded by the user, by title
    std::vector<std::string> group_titles;           // the list's groups as last filled (by id)
    // the Details columns, by SortBy value (Name first, always), and their widths by id;
    // choose_columns: the header's right-click menu and Choose Details (the explorer),
    // remembered in $XDG_STATE_HOME/lhde/flxp-columns
    std::vector<int> columns = { 0, 1, 2, 3 };
    std::map<int, int> column_widths;
    bool choose_columns = false;
    bool infotips = false;                           // XP's infotip over an item (details::infotip)

    // hooks
    virtual std::shared_ptr<shell::Folder> resolve(const std::string& path) { return shell::folder_for(path); }
    virtual bool visible(const Item&) { return true; }       // besides the text filter (the dialog's file types)
    virtual bool ghosted(const Item&) { return false; }      // LVIS_CUT (cut items, hidden files)
    virtual bool size_shown(const Item& it) { return !it.is_folder(); }
    virtual int icon(const std::string& key) = 0;
    virtual HIMAGELIST icons_at(int px) { return px == 16 ? small : large; }   // the zoom's image lists (same indices)
    virtual void filled() {}                                 // after fill_list (status bar, focus)
    virtual void changed_on_disk() { filled(); }             // after update() (the folder changed on disk)
    virtual void shown() {}                                  // after refresh (titles, address, header, tree)
    virtual void view_changed() {}                           // after set_view (menus, toolbar)
    virtual void filter_cleared() {}                         // navigate() emptied the filter (the boxes)
};

// The list view: style must give the view (LVS_REPORT, ...) and the rest the
// host wants; the four columns Name, Size, Type, Date Modified with these
// widths. SetWindowTheme "Explorer" (Longhorn/Vista styles: themed selection).
HWND create_list(Browser& b, HWND parent, int id, DWORD style, DWORD exstyle, const int widths[4], HFONT font);

void attach_list(Browser& b);          // a list the host made itself: the widget's own handling (Ctrl+wheel zoom, the infotip, the header's menu)

// ---- the Details columns -------------------------------------------------------------
// Name, Size, Type, Date Modified (derived) and Description, Tags, Comment (the stored
// properties of shell/details.h, read from the files only while one of them is shown or
// sorted by); ids are shell::SortBy values. XP's View > Choose Details and the header's
// right-click menu (a check per column, then More...) choose them.
struct Column { int id; const char* label; int width; int fmt; };
const std::vector<Column>& all_columns();
const Column* column(int id);
int column_index(const Browser& b, int id);   // the list's column showing id (-1: not shown)
int column_id(const Browser& b, int index);   // the id the list's column index shows (Name for none)
// Name first; the list refilled; remembered (choose_columns). take: the widths the user
// dragged the columns to are kept first (false: b.column_widths as they are)
void set_columns(Browser& b, const std::vector<int>& ids, bool take = true);
void load_columns(Browser& b);                // the remembered set and widths, applied
void save_columns(Browser& b);                // the set and the widths now
bool wants_stored(const Browser& b);          // a stored column shown or sorted by
void column_menu(Browser& b, HWND owner, int x, int y);   // the header's menu at screen (x, y)
void choose_details(Browser& b, HWND owner);  // XP's Choose Details dialog
void sort_by_column(Browser& b, int index);   // a header click: that column, again the other way round
void fill_list(Browser& b);            // the list from b.items (sorted), the selection after refresh
void sort_feedback(Browser& b);        // the header's sort arrow and the tinted column
void apply_filter(Browser& b);         // b.items from b.all_items, then fill_list
bool set_filter(Browser& b, const std::string& text);   // the boxes' text (live): true if it changed the list
void refresh(Browser& b);              // enumerate the folder again, fill, then shown(); follow()
// The folder shown is followed on disk (b.follow_disk): refresh() watches it with inotify,
// changes come as update() 150 ms after the first of a burst, a deleted or moved folder
// navigates to the nearest one above it that exists. unfollow: before the list goes.
void follow(Browser& b);
void unfollow(Browser& b);
void update(Browser& b);               // enumerate again, change the list item by item (keeps selection, scroll)
bool navigate(Browser& b, const std::string& path, bool record = true);
void go_back(Browser& b);
void go_forward(Browser& b);
void go_up(Browser& b);
void history_menu(Browser& b, HWND owner, bool back, int x, int y);   // the last ten places, nearest first

int menu_view(const Browser& b);       // LVS_*, VIEW_THUMBNAILS, VIEW_TILES (XP); LVS_REPORT, VIEW_TILES, LVS_ICON (Longhorn)
// what is remembered for the folder: menu_view, in the Longhorn hosts with
// VIEW_NONAMES_FLAG and the size above ZOOM_SHIFT
int view_code(const Browser& b);
// view: a view code (LVS_*, VIEW_THUMBNAILS, VIEW_TILES; the Longhorn hosts'
// VIEW_NONAMES_FLAG, the size above ZOOM_SHIFT, none: the one the view remembers;
// earlier codes are read as the nearest view). navigating: the folder's remembered
// view, applied before refresh() fills the list (no refill or icon restore here,
// and nothing stored).
void set_view(Browser& b, int view, bool navigating = false);
void set_grouped(Browser& b, bool on);   // group view by type (icon views)
// the zoom (zoomable hosts): 4051's slider, 0..100 -> floor(16 * 16^(pos/100)) px;
// its ticks every 10 are the levels: 16 21 27 36 48 64 84 111 147 194 256
int zoom_at(int pos);
int zoom_pos(int px);
std::vector<int> zoom_levels();
int icon_px(const Browser& b);          // the image size of the view now
void set_zoom(Browser& b, int px);      // Icons at px (4051's magnifier slider is the Icons view's)
void zoom_step(Browser& b, int dir);    // the view shown a level bigger (+1) or smaller, in its range (Ctrl+wheel, Ctrl+plus/minus)
enum { ZOOM_MENU_IDS = 13 };
// View > Zoom: Zoom In, Zoom Out, the view's levels (radio), ids first..; zoom_command: true if id was one
void zoom_menu(const Browser& b, HMENU m, int first);
void zoom_popup(Browser& b, HWND owner, int x, int y);   // the magnifier's slider under (x, y) (screen), modal
bool zoom_command(Browser& b, int id, int first);
bool group_view_applies(const Browser& b);   // the view can show groups (icon views)

// ---- the Longhorn views (zoomable hosts; shell/viewmodel.h) ----------------------------
// The controls (4074's view button and slider, the menus, the keys) choose Views here;
// what the list then is - its view, images, spacing, tile lines - is set_view's business.
shell::viewmodel::View current_view(const Browser& b);
void show_view(Browser& b, const shell::viewmodel::View& v);   // a control chose v (remembered for the folder)
shell::viewmodel::Memory& view_memory();                       // what each view remembers ($XDG_STATE_HOME/lhde/flxp-view-sizes)
enum ViewCommand {
    VC_DETAILS, VC_TILES, VC_ICONS,          // the three views as remembered (Icons: the icon stop used last)
    VC_LARGE, VC_MEDIUM, VC_SMALL,           // the icon stops (Ctrl+3..5)
    VC_HIDENAMES, VC_THUMBNAILS,             // Hide File Names (Icons), the thumbnails setting
    VC_CYCLE, VC_DEFAULT,                    // the View button; Ctrl+0
};
bool view_command(Browser& b, int vc);       // false: not a zoomable host
// Re-reads the thumbnails setting (another program changed flxp.ini): the images again
void thumbnails_changed(Browser& b);
// the change listeners of the view controls (4074's view button): called after every set_view
void add_view_listener(void (*fn)(Browser&));

std::vector<Item> selection(const Browser& b);
int list_index(const Browser& b, const std::string& path);
// LVN_BEGINDRAG: the selection, its icons as the drag image (the grabbed one first)
void drag_from_list(Browser& b, int grabbed);
// where a drop at d's point on the list goes: a folder item (*item to
// highlight) or the folder shown; "" when d is not over the list
std::string drop_dest(Browser& b, const FLXPDROP* d, int* item);
void drop_highlight(Browser& b, int item);   // LVIS_DROPHILITED on item (-1: none)
extern int g_drop_item;

// ---- views remembered per folder: $XDG_STATE_HOME/lhde/flxp-views, "path\tview" lines ----
namespace views {
int lookup(const std::string& folder, bool zoomable);   // -1: keep the current view
void leaving(const std::string& folder, int view);      // remembered for this window
void store(const std::string& folder, int view);
void forget(const std::string& folder);                 // back to the default (Ctrl+0)
void forget_all();                                      // Apply to All / Reset All Folders
int parse(const std::string& text);                     // "icons@48", "details+thumbnails": a view code, -1
std::string name_of(int view);
// a folder never shown: default_view / default_view_longhorn (zoomable hosts) in flxp.ini, else
// builtin_default: Details (XP frame), Icons at 48 px with thumbnails (Longhorn); -1 = builtin
int default_view(bool zoomable);
int builtin_default(bool zoomable);
void set_default(bool zoomable, int view);
}
// the view keys: Ctrl+0 the default view (the folder's own forgotten); the Longhorn
// hosts Ctrl+1..5 the slider's stops (Details, Tiles, Large, Medium, Small Icons, each as
// remembered); XP Ctrl+1 Details, Ctrl+2 Icons, Ctrl+3 Thumbnails; true if key was one
bool view_key(Browser& b, int key);

}
