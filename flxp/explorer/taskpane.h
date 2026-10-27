// The horizontal task band of 4051/4074's Explorer (frame=longhorn, optional:
// taskpane=1 / --taskpane / View > Task Band): the band under the command bar
// with the folder's big faded icon at the left, its name and a line about it,
// and the folder's tasks as links in two columns ("more" opens the rest), as
// in lh4051-232843.png; with a selection the item's icon, name and its verbs
// (4051's lh4051-225221.png, the PDC 2003 demo's file tasks). Its background
// is the command bar's: the theme's per-folder Rebar.BACKGROUND strip
// (lhbar), or with taskpane_style=aurora (auto when the Longhorn demo's
// images are found, doc/demo-resources.md) the demo's aurora bitmaps by folder
// kind (orange documents, blue pictures/video and the rest, purple music).
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "shell/namespace.h"
#include "w32/w32.h"

namespace taskpane {

struct Callbacks {
    void (*command)(int id, bool on_selection);   // a task link: a verb id (ID_NEW_FOLDER, ID_SEARCH, ...)
    void (*more)(bool on_selection, int x, int y); // "more": the folder's or the selection's menu at screen (x, y)
    void (*resized)() = nullptr;                   // the bottom edge was dragged (height() and compact() changed)
};

bool wanted();                       // taskpane= (flxp.ini, FLXP_TASKPANE, --taskpane): default off
int height();                        // the band's rows (compact: one row; expanded: taskpane_height)
// 4051's Preview: expanded (the big image, name and two columns of tasks) or
// compact (one row); remembered as taskpane_compact, the expanded height as
// taskpane_height (the band's bottom edge drags it, under the minimum it snaps
// to compact)
bool compact();
void set_compact(bool on);
void set_room(int rows);             // the rows for the band and the list: expanded only while 150 stay for the list
HWND create(HWND frame, const Callbacks& cb);
void destroy();
HWND window();
void set_folder(const std::shared_ptr<shell::Folder>& f);
void set_selection(const std::vector<shell::Item>& items);
void theme_changed();
std::string source();                // what the background is (for --info)

}
