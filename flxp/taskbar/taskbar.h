// flxp-taskbar: the classic (XP-style, uxtheme-drawn) taskbar. Shared state
// of the bar (taskbar.cpp), the task band (taskband.cpp) and the start menu
// (startmenu.cpp). The X side (window list, actions, struts, tray) is xwm.h.
#pragma once
#include <string>
#include <vector>
#include "w32/w32.h"
#include "xwm.h"
extern "C" {
#include "commctrl.h"
#include "uxtheme.h"
#include "vssym32.h"
}

namespace shell { struct App; }

namespace tb {

// a bitmap of XP's explorer.exe by resource id (lhde-import's resources/xp), null when not imported
HBITMAP xp_explorer_bitmap(int id, UINT flags);

enum : UINT {
    WM_TB_XCHANGED = WM_APP + 1,    // xwm has news (posted from its thread)
    WM_TB_TRAYCTL = WM_APP + 2,     // the tray's own controls read new values (notify.cpp's thread)
};

void relayout_bar();                // the bar's parts placed again (the tray, Quick Launch changed size)
void taskbar_menu(POINT screen);    // XP's taskbar context menu at a point
void toggle_desktop();              // _NET_SHOWING_DESKTOP flipped (Show the Desktop / Show Open Windows)

enum Edge { EdgeLeft = 0, EdgeTop = 1, EdgeRight = 2, EdgeBottom = 3 };

struct Bar {
    HWND wnd = nullptr;
    HWND start = nullptr, band = nullptr, tray = nullptr, clock = nullptr;
    Edge edge = EdgeBottom;
    int thickness = 30;             // across the bar
    RECT screen = {};               // the bar on the screen
    HTHEME theme = nullptr;         // TaskBar (null: the classic look)
    HFONT font_caption = nullptr, font_caption_bold = nullptr;   // the caption font, normal and bold weight
    int button_top = 0, button_height = 30;   // task buttons across the bar (horizontal)
    uint64_t own = 0;               // our X window
    bool menu_only = false;         // --start-menu-only: no bar, just the start menu (next to luxshell's Avalon taskbar)
    bool horizontal() const { return edge == EdgeTop || edge == EdgeBottom; }
};
extern Bar g;

std::u16string W(const std::string& s);
std::string U(const WCHAR* s);
std::string setting(const char* key, const char* fallback = "");   // shell::theme_setting: FLXP_<KEY>, the theme, flxp.ini

// The pixels under a child: the bar background (WM_ERASEBKGND of the bar)
void paint_bar_background(HDC hdc, const RECT& client);

namespace band {
HWND create(HWND parent);
void theme_changed();
void place(const RECT& r);                  // in the bar's client coordinates
void update(const xwm::Snapshot& s);
LRESULT notify(NMHDR* h);                   // WM_NOTIFY from the toolbar
bool command(UINT id);
void timer(UINT_PTR id);
int button_height();
uint64_t active_window();                   // _NET_ACTIVE_WINDOW as last seen
}

namespace startmenu {
// the popup menu above/beside the start button; anchored: above (below in the screen's upper half) a Start
// button that is not ours (_LHSHELL_START's rect), left edges aligned
void show(const RECT& button_screen, bool anchored = false);
void close();                               // EndMenu if it is open
void run_dialog();
bool dialog_message(MSG* m);                // IsDialogMessage for the Run dialog
void open_folder(const std::string& path);  // a new explorer window (flxp PATH)
void find_files(const std::string& dir);     // Search: luxfind (looking in dir; "" every root), else the folder
void window_menu(uint64_t id, const xwm::Task& t, POINT pt);
// shared with the start panel
HMENU programs_popup();                     // Programs: the .desktop applications by category
HMENU settings_popup();                     // Display Properties, Theme >
bool command(UINT cmd);                     // a command of those menus (false: not one)
// the Programs menus' owner-drawn items (XP's menu band look): their owner's WM_MEASUREITEM / WM_DRAWITEM
bool measure_item(MEASUREITEMSTRUCT* mi);   // false: not one of them
bool draw_item(const DRAWITEMSTRUCT* di);
void launch(const shell::App& a);           // start it and count it for the panel's MFU list
bool run_line(const std::string& cmd);      // /bin/sh -c in $HOME
void log_off();                             // asks first
void shut_down();
}

// the notification area's chevron, our own items and the docked icons' tooltips (notify.cpp)
namespace notify {
void start(bool tray_running);              // the controls' thread, the tooltips
void stop();
void theme_changed();
int slot();                                 // one icon's cell: 16 px and XP's 2 px
int own_count();                            // our items shown (Volume, Brightness, Touchpad)
SIZE chevron_size();
bool chevron_shown();                       // two or more inactive icons ([taskbar] hide_inactive)
bool demote();                              // decide which docked icons hide; true when that changed
void place(const RECT& chevron, const RECT& own_icons);   // in the tray window
void paint(HDC hdc);
bool mouse(UINT msg, POINT p);              // a mouse message in the tray window; true when it was ours
bool wheel(POINT screen, int delta);
void controls_changed();                    // WM_TB_TRAYCTL
void x_changed(const xwm::Snapshot& s);     // the docked icon under the pointer
bool timer(UINT_PTR id);
std::string hover_text();                   // the docked icon's tooltip on screen ("" none)
bool popup_open();
}

// XP's Quick Launch toolbar (quicklaunch.cpp)
namespace quicklaunch {
HWND create(HWND parent);
void theme_changed();
void read_setting();                        // [taskbar] quicklaunch
bool shown();
int length();                               // along the bar
int count();                                // buttons, Show Desktop included
void place(const RECT& r);                  // in the bar's client coordinates; an empty rect hides it
bool refresh();                             // the folder changed: read again (true if it did)
LRESULT notify(NMHDR* h);
bool command(UINT id);
HWND window();
}

namespace startpanel {
void show(const RECT& button_screen, bool anchored = false);   // XP's two-column start panel ([taskbar] start_menu = panel, the default)
void close();                               // dismiss it if it is open (a toggling _LHSHELL_START)
void count_launch(const std::string& app_id);   // +1 in $XDG_DATA_HOME/flxp/mfu
// A menu item image (MIIM_BITMAP): a 32 bpp premultiplied DIB of a .desktop Icon= (the panel's lookup), else of
// the icon theme's `fallback_key` ("" = none); null if neither. The caller deletes it.
HBITMAP menu_bitmap(const std::string& icon_name, const char* fallback_key, int size = 16);
}

} // namespace tb
