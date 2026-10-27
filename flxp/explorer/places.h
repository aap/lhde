// The left pane when the Folders tree is off: common places and the GTK
// bookmarks, where XP shows its task pane (shell32 DUI "shellstyle").
//
// XP look (visual styles and classic, the explorer's XP frame): sections as
// XP's "Other Places" - a header with the title in Tahoma 8 bold and the
// collapse chevron (headersection.bmp and up/down.bmp of the shellstyle
// sources under nt5src= when found, drawn lookalikes otherwise; other
// .msstyles: their ExplorerBar parts), a box below with 16 px icons and
// link-coloured names that underline under the pointer, on the task pane's
// gradient; Places and Bookmarks.
//
// The groups look (Options::groups: the Longhorn hosts, explorer and file
// dialog alike): 4051's white pane with the pre-reset concepts' collapsible
// groups - +/- expanders, link-blue names, the Save File concept's 16 px
// icons (places_icons=0: none, as 4051's plain links), the current folder's
// entry in bold, the 4074 Filter box at the top; Common Places, Computer,
// Bookmarks (standard_spec) unless the host gives its own (Options::spec).
//
// The groups: Places (Desktop, Home, Documents and Pictures - XDG user dirs,
// when they exist - My Computer, Trash), Bookmarks (shell/bookmarks, re-read
// when the file changes (polled); folders dropped on the group become
// bookmarks there) and Custom groups of the host's places (the dialog's
// Common Places: Recent Places, Desktop, Home, App Location). A click opens;
// the context menu has Open, Open in New Window (Options::new_window) and,
// for bookmarks, Rename (in place) and Remove. Drops: on the Bookmarks group
// (header, entries, gaps) folders become bookmarks at the insertion mark and
// files are refused; on another group's entry the files go there.
#pragma once
#include <string>
#include <vector>
#include "w32/w32.h"

namespace places {

struct Callbacks {
    void (*navigate)(const std::string& path);
    std::string (*current_dir)();               // "" when the folder shown is not a directory
    void (*filter)(const std::string& text);    // the Longhorn "Filter by" box, as typed
    void (*filter_done)();                      // Enter in it: back to the list
    void (*group_toggled)(const std::string& title, bool collapsed) = nullptr;   // a header folded (to remember it)
    std::string (*current_path)() = nullptr;    // the folder shown, also a virtual one (the groups look's bold entry)
};

struct Place { std::string label, path, icon; };

struct GroupSpec {
    enum Kind { Places, Bookmarks, Custom } kind = Custom;
    std::string title;
    std::vector<Place> places;                  // Custom
    bool collapsed = false;
};

struct Options {
    bool groups = false;                        // the groups look (the Longhorn hosts); else XP's task pane
    bool new_window = true;                     // "Open in New Window" in the menu (spawns this program)
    std::vector<GroupSpec> spec;                // the groups look's groups; empty: standard_spec()
    std::string remember;                       // a setting keeping the folded groups' titles ("" = none)
};

HWND create(HWND parent, int id, const Callbacks& cb, const Options& o);
std::vector<GroupSpec> standard_spec();        // Common Places (Desktop, Home, Documents, Pictures, Trash), Computer, Bookmarks
GroupSpec computer_group();                     // My Computer and the drives
bool icons();                                   // the groups look's icons (places_icons)
void set_icons(bool on);                        // and remembered
void destroy();                         // the host goes (the dialog: the statics are free again)
void set_group(const std::string& title, const std::vector<Place>& places);   // a Custom group's places
void folder_changed();                  // the host shows another folder (the bold entry)
// An XDG user directory ($XDG_CONFIG_HOME/user-dirs.dirs, e.g. XDG_DOCUMENTS_DIR,
// else ~/<fallback>) when it exists and is not $HOME, else ""
std::string user_dir(const char* key, const char* fallback);
void set_longhorn(bool longhorn);       // a frame switch between the XP and the groups look
HWND window();
void bookmarks_changed();      // re-read now (the pane also notices on its own)
void clear_filter();           // the folder changed: empty the Filter box

}
