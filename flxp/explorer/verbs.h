// The shell verbs behind the item and background context menus, shared by the
// explorer and the desktop. XP's defview builds the item menu from the folder's
// IContextMenu (Open bold, Explore for folders, Open With, Send To, Cut/Copy,
// Create Shortcut, Delete, Rename, Properties) and the background menu from its
// own (View, Arrange Icons By, Refresh, Paste, New, Properties). Ours is the
// same shape with the unix additions: Open Terminal Here and Add to Bookmarks.
// What differs between the views (how a folder opens, how they refresh, how a
// rename starts) goes through a Host.
#pragma once
#include <string>
#include <vector>
#include "shell/namespace.h"
#include "w32/w32.h"
#include "commands.h"
extern "C" {
#include "commctrl.h"
#include "w32/flxp_shell.h"
}

namespace verbs {

using shell::Item;

struct Ctx {
    std::vector<Item> items;      // the items a verb applies to (empty: the folder background)
    void* node = nullptr;         // the tree item, when the menu came from the Folders tree
    bool node_expanded = false;   // (its state: Expand or Collapse)
};
extern Ctx g_ctx;                 // set by whoever opens a menu or runs a verb

struct Host {
    virtual ~Host() = default;
    virtual HWND window() = 0;                              // owner of dialogs and menus
    virtual std::string current_dir() = 0;                  // the background's folder ("" if virtual)
    virtual Item folder_item() = 0;                         // the background as an item (Properties)
    virtual void open_folder(const Item& it, bool explore) = 0;
    // after a change in these folders: refill, then select (and maybe rename) `select`
    virtual void changed(const std::vector<std::string>& dirs, const std::string& select = "", bool rename = false) = 0;
    virtual void begin_rename() = 0;                        // label edit of g_ctx's item
    virtual void clip_changed() {}                          // ghost the cut items
    virtual void trashed(const Item&) {}                    // before the refill (the explorer leaves a deleted folder)
    virtual void updated() {}                               // a dialog may have changed what the status shows
    virtual void background_properties() { show_properties_default(); }
    void show_properties_default();
};

void set_host(Host* h);
Host* host();                 // the current one (a nested host puts it back)
bool is_fs(const Item& it);
void error_box(const std::string& text);

// menus for g_ctx
void build_item_menu(HMENU m);
enum { BG_VIEW = 1, BG_ARRANGE = 2, BG_THUMBNAILS = 4 };   // View submenu; Arrange Icons By, Refresh, Paste; View has Thumbnails
void build_background_menu(HMENU m, unsigned flags, int view, int sort);
HMENU view_submenu(int view, bool thumbnails = false);   // view: LVS_* or VIEW_THUMBNAILS
void check_view_items(HMENU m, int view);                 // the radio mark among Thumbnails/Icons/List/Details
// The Longhorn hosts' Thumbnails toggle (browser.h thumbs_toggle): -1 XP's
// Thumbnails view (a radio item), else 0/1 the toggle's state: Thumbnails is a
// check item after Icons/List/Details and the radio mark goes by `view` & ~thumbnails
extern int thumbs_toggle;
// runs a verb on g_ctx; false if id is not one (view commands, ID_EXPAND)
bool run(int id);

// Drag and drop (flxp_shell.h, FlxpDragFiles / WM_FLXP_DROP). drop_effect: what
// dropping d's files into dest would do (shell::drop_effect with d's keys and
// allowed effects, 0 = no); drop: does it (errors in a message box), then the
// host's changed() for dest and the sources' folders. drag_items: starts a drag
// of these items (their icons from il) from a list or tree; the host's changed()
// for their folders after another process moved them.
DWORD drop_effect(const FLXPDROP* d, const std::string& dest);
DWORD drop(const FLXPDROP* d, const std::string& dest);
DWORD drag_items(HWND from, const std::vector<Item>& items, HIMAGELIST il, const std::vector<int>& images);
std::vector<std::string> drop_paths(const FLXPDROP* d);

void open_items(const std::vector<Item>& items);
void show_properties(const Item& it);
void add_bookmark(const std::string& dir);

// The wallpaper (flxp-desktop paints it). is_wallpaper_image: a local BMP, PNG or
// JPEG file (by its signature: what the desktop decodes; GIF is not). set_wallpaper
// writes wallpaper= and wallpaper_mode= (stretch|center|tile|fill) into flxp.ini
// and sets the root property _FLXP_WALLPAPER = "MODE PATH", which a running
// flxp-desktop watches and applies at once. False (and a message) on failure.
bool is_wallpaper_image(const Item& it);
bool set_wallpaper(const std::string& path, const std::string& mode);

// Cut/Copy's files, also on the X CLIPBOARD (shell/xclip). clip() is our own list;
// clipboard_files() is what Paste takes: ours while we own the CLIPBOARD, else
// read from its owner now (and kept in clip()).
const std::vector<std::string>& clip();
const std::vector<std::string>& clipboard_files();
bool clip_cut();
bool can_paste();                                    // our files, or another client owns the X CLIPBOARD
bool is_cut(const std::string& path);
void clip_renamed(const std::string& from, const std::string& to);

}
