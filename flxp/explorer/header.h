// The Explorer's Longhorn header: the shell layer's header (shell/lhheader.h, flxp/shell/ui/lhheader.cpp: the 4051 and
// 4074 band, the vision header through luxnav, the frame's glass and input holes) with the Explorer's meaning of its
// crumbs. A crumb is a folder of the path from the namespace root (4074 starts at Computer), its key the folder's
// path; a crumb's drop-down lists its sibling folders (vision with header_crumb_menu = children: its subfolders); the
// address edit and a vision bookmark go to a path (~ the home folder); a crumb is a drop target (verbs::drop). Every
// other call is the shell layer's, reached as header::NAME.
#pragma once
#include <memory>
#include <string>
#include "shell/namespace.h"
#include "w32/w32.h"
#include "shell/lhheader.h"

namespace header {

using namespace shell::lhheader;

struct Callbacks {
    void (*navigate)(const std::string& path);       // a breadcrumb or a drop-down entry was clicked, an address typed
    void (*filter)(const std::string& text);         // the search box changed (live)
    void (*done)(bool keep);                         // Enter (keep the filter) or Escape (cleared)
    void (*command)(int id);                         // ID_BACK / ID_FORWARD (the round buttons), the vision toolbar's ID_NEW_FOLDER
    void (*history)(bool back, int x, int y);        // the buttons' drop-down: a history menu at screen (x, y)
    void (*favorites)(int x, int y);                 // the navigation bar's star: the bookmarks at screen (x, y)
    void (*tool)(int id, int x, int y) = nullptr;    // a set_tools() button; (x, y): screen, under it (its menu)
};

HWND create(HWND frame, const Callbacks& cb);        // child of frame, at (0,0)
void set_folder(const std::shared_ptr<shell::Folder>& f);

}
