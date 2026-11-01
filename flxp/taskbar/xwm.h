// flxp-taskbar's X side, no win32 here: the window list from EWMH root and
// client properties, the actions a task button sends (activate, minimise,
// close, maximise), the dock's struts, and the freedesktop system tray
// (XEmbed: _NET_SYSTEM_TRAY_S<screen>). Everything runs on one thread with a
// connection of its own; the UI thread reads snapshots and queues requests,
// and is told about changes through the notify callback (called on the X
// thread: post a message from it, nothing else).
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace xwm {

struct Task {
    uint64_t id = 0;                // the client window
    std::string title;              // _NET_WM_NAME, else WM_NAME
    std::vector<uint32_t> icon;     // 16x16 ARGB (straight alpha), empty = none
    bool minimized = false;         // _NET_WM_STATE_HIDDEN
    bool attention = false;         // _NET_WM_STATE_DEMANDS_ATTENTION or the WM_HINTS urgency hint
    bool maximized = false;         // both _NET_WM_STATE_MAXIMIZED atoms
    bool shown = true;              // on the current desktop (or sticky)
    uint32_t icon_serial = 0;       // changes when the icon does
};

enum Change : uint32_t {
    ChangeList = 1, ChangeActive = 2, ChangeTitle = 4, ChangeIcon = 8, ChangeState = 16,
    ChangeDesktop = 32, ChangeTray = 64,
    ChangeStartKey = 128,           // _LHSHELL_START: luxwm's root broadcast (Super) or luxshell's to our owner window
};

// _LHSHELL_START (docs/SHELL-DESIGN.md 13): l[0] bit 0 = toggle, l[1..4] = the anchor
// rectangle in root coordinates (0 x 0: none). luxwm's root broadcast (no rect) always toggles.
struct StartRequest {
    bool toggle = false;
    int x = 0, y = 0, w = 0, h = 0;
    bool from_root = false;         // the root broadcast (else luxshell's message to our owner window)
    uint32_t serial = 0;
};

struct Snapshot {
    std::vector<Task> tasks;        // in _NET_CLIENT_LIST (creation) order, taskbar windows only
    uint64_t active = 0;            // _NET_ACTIVE_WINDOW
    uint32_t changes = 0;           // Change bits since the last snapshot
    bool have_wm = false;           // a window manager publishes _NET_CLIENT_LIST
    StartRequest start;             // the last one (ChangeStartKey says a new one came)
    bool menu_provider = false;     // we own _LH_STARTMENU_S<screen>
};

typedef void (*Notify)();

// XInitThreads: before the toolkit opens its display (two threads use Xlib).
void init_threads();

// Opens the connection and starts the thread. own = the taskbar's X window.
bool start(uint64_t own, Notify notify);
void stop();
void snapshot(Snapshot& out);

// Actions (queued, done on the X thread)
void activate(uint64_t id);
void minimize(uint64_t id);
void close(uint64_t id);
void maximize(uint64_t id, int action);     // 0 restore, 1 maximise, 2 toggle (_NET_WM_STATE)
void set_icon_geometry(uint64_t id, int x, int y, int w, int h);   // _NET_WM_ICON_GEOMETRY (minimise animation target)
void focus_own();                           // XSetInputFocus on the taskbar (its menus need the keyboard)
void set_root_utf8(const std::string& prop, const std::string& value);   // a root UTF8_STRING (_LH_THEME)
void send_root_message(const std::string& type, long l0, long l1, long l2, long l3, long l4);

// The dock: _NET_WM_STRUT(_PARTIAL) for edge 0 left, 1 top, 2 right, 3 bottom
// of the screen; thickness in pixels; [start, end] along the edge.
void set_struts(int edge, int thickness, int start, int end);

// The window manager: the name on _NET_SUPPORTING_WM_CHECK and its PID's
// executable ("" if unknown); read on the calling thread.
std::string wm_name();
std::string wm_executable();

// The start-menu provider: own _LH_STARTMENU_S<screen> (on) or give it up;
// luxshell then sends _LHSHELL_START to our owner window instead of opening
// its own menu, and root broadcasts are ignored while luxshell runs.
void provide_start_menu(bool on);
// _LH_STARTMENU_OPEN = 1/0 (CARDINAL) on the _LH_STARTMENU_S owner window (when we have one) and
// on the root: our start panel or classic menu is open (set before it shows, cleared after it closed).
void set_start_menu_open(bool open);

// System tray. tray_start: claim the selection when it is free, else wait
// for it (takeover: take it from another manager, but never from luxshell). The icons live in a child window of the taskbar at
// area (taskbar coordinates), in slots of slot x slot pixels, icon_size
// centred in each; vertical lays them out top to bottom.
bool tray_start(bool takeover, bool vertical);
int tray_count();                           // icons currently shown
void tray_layout(int x, int y, int w, int h, int slot, int icon_size, bool vertical);
// The pixels behind the icons (premultiplied BGRA rows of the area's size):
// the container's background, which the icons show through ParentRelative.
void tray_background(const uint32_t* pixels, int w, int h, int stride);

} // namespace xwm
