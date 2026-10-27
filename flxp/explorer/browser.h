// The browsing widget shared by the explorer frame and the file dialog: a
// folder shown in a list view with its views (Thumbnails, Icons, Small Icons,
// List, Details; optionally grouped by type), sorting, the filter-as-you-type
// of the Longhorn search/Filter boxes, Back/Forward history and the views
// remembered per folder; plus the frame plumbing every Longhorn host needs:
// which frame style the session asks for (frame=, the theme's flxp_frame,
// lhwm), lhwm's glass extension under the header (header.h) and the caption
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
#include "w32/w32.h"
#include "commands.h"
extern "C" {
#include "commctrl.h"
#include "w32/flxp_shell.h"
}

namespace browser {

using shell::Item;

// ---- the frame ---------------------------------------------------------------------

// frame= xp | longhorn | auto (default). auto: the session theme's flxp_frame
// (themes.ini: xp for the Luna entries, longhorn for Longhorn's), else
// longhorn whenever lhwm runs (it publishes _LHWM_COMPOSITED, _LHWM_THEME or
// _LHWM_FRAME_METRICS), glass or not, else xp. `why` says what decided.
bool frame_auto();
bool want_longhorn(std::string* why = nullptr);

// lhwm's glass under the header: the extension by header::height(), the
// NO_TITLE / NO_ICON (and on glass CLIENT_CAPTION) frame flags, the input hole
// over lhwm's caption buttons; unextend_frame() is the way back to an ordinary
// frame (lhwm's caption with title and icon).
void extend_frame(HWND frame);
void caption_hole(HWND frame);
void unextend_frame(HWND frame);
// The header's height follows the frame (glass: 76 rows less lhwm's caption)
// or the theme's navigation bar; then the extension, the host's layout and
// the header's look follow.
void header_refit(HWND frame, void (*layout)());
// WM_NCHITTEST of a frame with a header: the header's empty parts (it answers
// HTTRANSPARENT there) are the caption. False when the frame has no header.
bool header_hittest(HWND frame, LPARAM lp, LRESULT* result);
// Active or inactive as the window manager draws the frame (_NET_ACTIVE_WINDOW
// through lhwm), else the focus state the host tracks (WM_ACTIVATE).
bool frame_active(HWND frame, bool focus_active);
// The window's icon follows the folder (4051's CShellBrowser2::v_SetIcon): the
// icon theme's key at 16, 32 and 48 px as WM_SETICON (the shim publishes them
// as _NET_WM_ICON for the window manager and the taskbar)
void set_window_icon(HWND frame, const std::string& key);

// ---- the list --------------------------------------------------------------------

std::string lower(const std::string& s);   // ASCII lower case (the filter's comparison)

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
    // Thumbnails: XP's view (false: choosing another view ends it) or, in the
    // Longhorn hosts (4051's View > Preview, lh4051-194700.png), a toggle that
    // shows thumbnails in the Icons view whatever view is chosen after it
    bool thumbs_toggle = false;
    bool thumbs = false;
    // The Longhorn hosts (4051, Vista): zoom levels for the icon views and the
    // thumbnails toggle in every view (Details/List: 16 px squares); zoom = the
    // Icons view's icon size, 0 = the default (the large icons; with thumbnails
    // thumbnail_size). Remembered per folder with the view.
    bool zoomable = false;
    int zoom = 0;
    // following the folder on disk (inotify, browser.cpp follow/update); off: F5 only
    bool follow_disk = true;
    std::unique_ptr<shell::DirWatch> dirwatch;
    int follow_pending = 0;                          // DirWatch bits since the last update
    bool follow_armed = false;                       // the coalescing timer runs
    std::map<std::string, bool> collapsed;           // groups folded by the user, by title
    std::vector<std::string> group_titles;           // the list's groups as last filled (by id)

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

void attach_list(Browser& b);          // a list the host made itself: the widget's own handling (Ctrl+wheel zoom)
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

int menu_view(const Browser& b);       // LVS_* or VIEW_THUMBNAILS
// what is remembered for the folder: menu_view, or LVS_* | VIEW_THUMBS_FLAG for
// another view with the toggle on
int view_code(const Browser& b);
// the toggle (thumbs_toggle hosts): on from another view goes to Icons as well
void set_thumbnails(Browser& b, bool on);
// view: LVS_* or VIEW_THUMBNAILS (Icons with thumbnails), or with
// VIEW_THUMBS_FLAG (navigating: a remembered view_code). navigating: the folder's remembered view,
// applied before refresh() fills the list (no refill or icon restore here, and
// nothing stored).
void set_view(Browser& b, int view, bool navigating = false);
void set_grouped(Browser& b, bool on);   // group view by type (icon views)
// the zoom (zoomable hosts): 4051's slider, 0..100 -> floor(16 * 16^(pos/100)) px;
// the levels (Ctrl+wheel, View > Zoom) are its ticks every 10: 16 21 27 36 48 64 84 111 147 194 256
int zoom_at(int pos);
int zoom_pos(int px);
std::vector<int> zoom_levels();
int icon_px(const Browser& b);          // the Icons view's size now
void set_zoom(Browser& b, int px);      // from List/Details: to Icons at px
void zoom_step(Browser& b, int dir);    // +1 bigger, -1 smaller (Ctrl+wheel, Ctrl+plus/minus)
enum { ZOOM_MENU_IDS = 13 };
// View > Zoom: Zoom In, Zoom Out, the levels (radio), ids first..; zoom_command: true if id was one
void zoom_menu(const Browser& b, HMENU m, int first);
void zoom_popup(Browser& b, HWND owner, int x, int y);   // the magnifier's slider under (x, y) (screen), modal
bool zoom_command(Browser& b, int id, int first);
bool group_view_applies(const Browser& b);   // the view can show groups (icon views)

std::vector<Item> selection(const Browser& b);
int list_index(const Browser& b, const std::string& path);
// LVN_BEGINDRAG: the selection, its icons as the drag image (the grabbed one first)
void drag_from_list(Browser& b, int grabbed);
// where a drop at d's point on the list goes: a folder item (*item to
// highlight) or the folder shown; "" when d is not over the list
std::string drop_dest(Browser& b, const FLXPDROP* d, int* item);
void drop_highlight(Browser& b, int item);   // LVIS_DROPHILITED on item (-1: none)
extern int g_drop_item;

// ---- views remembered per folder: $XDG_CONFIG_HOME/flxp/views, "path\tview" lines ----
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
// the view keys (both hosts): Ctrl+0 the default view (the folder's own forgotten),
// Ctrl+1 Details, Ctrl+2 Icons (48 px in the Longhorn hosts), Ctrl+3 big icons with
// thumbnails (thumbnail_size; XP: the Thumbnails view); true if key was one
bool view_key(Browser& b, int key);

}
