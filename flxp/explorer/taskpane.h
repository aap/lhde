// The horizontal task band of 4051's Explorer (frame=longhorn, optional:
// taskpane=1 / --taskpane / View > Task Band): the band under the command bar,
// left to right the preview, the details and the tasks, as 4051's preview pane
// (System.Windows.Explorer's PreviewTaskPresenter; taskpane/managed.md in the
// Longhorn research; lh4051-152854/152900, 152336/152329, 152415/152354,
// 152420/152358.png):
// - nothing selected: the folder's image faded at the left and "N items" /
//   "in this folder" over it (WelcomePane), the folder's tasks;
// - one item: its thumbnail (a picture, framed in white) or its icon, the
//   name, the values of shell/details (two 180 px columns, an empty editable
//   property as its invitation) and "all properties...";
// - several: a stack of up to 7 thumbnails, "N items selected", the shared
//   values.
// The picture hangs 10 px below the band (4051's overhang), over the left pane.
// Expanded (4051's Preview on) and compact (off) differ only in size: the
// preview box is the band's height + 8 wide, the details show only the title
// when compact, the count goes on one line. Tasks take what is right of the
// details (none when less than 200 px are left), 189 px columns, 26 px rows,
// the last slot "more" when they do not all fit. Its background is the command
// bar's: the theme's per-folder Rebar.BACKGROUND strip (lhbar), or with
// taskpane_style=aurora the Longhorn demo's aurora bitmaps by folder kind.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "shell/details.h"
#include "shell/namespace.h"
#include "w32/w32.h"

namespace taskpane {

struct Callbacks {
    void (*command)(int id, bool on_selection);   // a task link: a verb id (ID_NEW_FOLDER, ID_SEARCH, ID_PROPERTIES, ...)
    void (*more)(bool on_selection, int x, int y); // "more": the folder's or the selection's menu at screen (x, y)
    void (*resized)() = nullptr;                   // the bottom edge was dragged (height() and compact() changed)
};

bool wanted();                       // taskpane= (flxp.ini, FLXP_TASKPANE, --taskpane): default off
int height();                        // the band's rows (compact: 48; expanded: taskpane_height)
// 4051's Preview: expanded (126 rows below the command bar by default, dragged
// between 126 and 246) or compact (48; 4051's 150 / 270 / 72 less the command
// bar's 24); remembered as taskpane_compact, the expanded height as
// taskpane_height (the band's bottom edge drags it; under 87 it snaps to
// compact, 4051's split point)
bool compact();
void set_compact(bool on);
void set_room(int rows);             // the rows for the band and the list: expanded only while 150 stay for the list
HWND create(HWND frame, const Callbacks& cb);
void destroy();
HWND window();
void set_folder(const std::shared_ptr<shell::Folder>& f);
// What to show (shell/details), and whether thumbnails may be shown here
// (thumbnails, thumbnails_network); without them the icon
void set_details(const shell::details::Info& in, bool pictures);
void theme_changed();
std::string source();                // what the background is (for --info)

}
