// The Longhorn 4074 Explorer header: the band above the rebar with 4074's
// round Back and Forward buttons, the folder's name in a large font, the
// breadcrumb of the path and the search box. Under luxwm with a compositor it
// lies on the window manager's glass (the frame extended into the client,
// docs/CLIENT-PROTOCOL.md: transparent with glowing text). Without
// composition it is opaque: the navigation bar of the session theme's
// Longhorn msstyles (navbar.h, COMPOSITION.md 16.4: Navbar::Rebar,
// splitter, travel/favorites buttons, fonts and colours by focus and
// maximise state), else luxwm's _LHWM_HEADER_PIXMAP (the caption band
// continued, e.g. Luna) under the glass layout, else (no luxwm) the 4074
// Slate gradient. The empty parts answer HTTRANSPARENT so the frame's
// WM_NCHITTEST makes them a caption.
// Vision mode: when luxwm's frame theme has a nav.ini (header_engine = auto |
// lhnav | flxp), the contents are lhnav's (docs/NAVBAR.md): the concept art's
// orb, title in luxwm's deco, breadcrumb, search box and toolbar; the behaviour
// stays flxp's.
#pragma once
#include <string>
#include <memory>
#include <vector>
#include "shell/namespace.h"
#include "w32/w32.h"

namespace header {

struct Callbacks {
    void (*navigate)(const std::string& path);       // a breadcrumb was clicked
    void (*filter)(const std::string& text);         // the search box changed (live)
    void (*done)(bool keep);                         // Enter (keep the filter) or Escape (cleared)
    void (*command)(int id);                         // ID_BACK / ID_FORWARD (the round buttons)
    void (*history)(bool back, int x, int y);        // the buttons' drop-down: a history menu at screen (x, y)
    void (*favorites)(int x, int y);                 // the navigation bar's star: the bookmarks at screen (x, y)
    void (*tool)(int id, int x, int y) = nullptr;    // a set_tools() button; (x, y): screen, under it (its menu)
};

// Glyph buttons of the host on the breadcrumb's row, left of the magnifier
// (the file dialog's New Folder and Views, as 4051's command row has glyph
// buttons instead of 4074's "Up a Folder / New Folder / Change View" strip).
// The icons are copied (16 px); a drop-down one calls back on the press.
struct Tool { int id; HICON icon; bool dropdown; };
void set_tools(const std::vector<Tool>& tools);
void enable_tool(int id, bool on);
bool tool_rect(int id, RECT* screen);

// 4074's Toolbar Size and Toolbars (its system menu, lh4074-1952*.png), as
// settings (flxp.ini, View > Toolbars): <prefix>header_size = small | medium |
// large | xlarge, and <prefix>header_buttons / _title / _breadcrumb / _search /
// _favorites = 1 | 0. Small: one row under the frame's caption (which keeps the
// title) with the small Back/Forward buttons, the breadcrumb, the small
// magnifier and star; Medium: the 76 rows of 4074-dwm.png (title over the
// breadcrumb); Large / Extra-Large: Medium plus 32 / 49 empty rows, the
// magnifier and star (small / normal) at the bottom.
enum Size { SIZE_SMALL, SIZE_MEDIUM, SIZE_LARGE, SIZE_XLARGE };
enum Element { SHOW_BUTTONS, SHOW_TITLE, SHOW_BREADCRUMB, SHOW_SEARCH, SHOW_FAVORITES, ELEMENTS };
// before create(): the settings' prefix ("" the explorer, "filedlg_" the
// dialog, whose elements fall back to the explorer's) and the default size
void configure(const std::string& prefix, const std::string& default_size);
Size size();
bool shown(Element e);
bool draws_title();                 // the header shows the title (else the frame's caption: Small, or Title off)
void set_size(Size s);              // stored; the host refits (browser::header_refit)
void set_shown(Element e, bool on); // stored; Title also wants a refit (the caption's flags)
enum { MENU_IDS = 9 };
// View > Toolbars under this header, ids first..first+MENU_IDS-1: the elements
// (checks, 4074's Toolbars) and last the Toolbar Size submenu (radio, 4074's
// Small / Medium / Large / Extra-Large), appended to the host's Toolbars popup
void append_menu(HMENU toolbars, UINT first);
void update_menu(HMENU toolbars, UINT first);
bool menu_command(UINT id, UINT first);      // true when id was one of them (then refit)

// The header's height in client rows: on glass 4074's 76 px from the frame's
// top edge to the client contents, less the window manager's caption (at
// least the Back button's 54); opaque the theme's navigation bar (53 Slate,
// 58 Jade) or the same as on glass.
int height_for_caption(int caption_h);
// Glass with luxwm's CLIENT_CAPTION (experimental; client_caption=0 turns it
// off): the client covers the caption rows too, luxwm keeps only its buttons
// (FlxpDwmGetCaptionRect), and the header is 4074's 76 rows less the border.
bool client_caption();
int height();
// the frame extension's top (DwmExtendFrameIntoClientArea): the header, on
// glass one row more (luxwm's line pair under the header's dark row); vision:
// the header itself, whose last two rows are luxwm's line pair (lhnav_extend_top)
int extend_top();
// vision: (x, y) of the header is one of lhnav's top resize rows (the frame answers HTTOP)
bool resize_hit(int x, int y);
void caption_changed();                              // _LHWM_CAPTION_RECT changed (vision: lhnav's caption rect)
HWND create(HWND frame, const Callbacks& cb);       // child of frame, at (0,0)
void destroy();                                      // the frame goes back to the XP look (a theme switch)
void set_folder(const std::shared_ptr<shell::Folder>& f);
void clear_filter();                                 // empties the search box (no callback)
void set_title(const std::string& title);            // the title shown instead of the folder's name ("" = the folder's; the file dialog: "Open")
void relayout(int width, int height);
void composition_changed();                          // repaint: glass or opaque (the height may change: height_for_caption)
void state_changed();                               // _NET_WM_STATE: maximised or restored (title colours, button variants)
void set_active(bool active);                        // luxwm shows the frame active (_NET_ACTIVE_WINDOW): navigation bar variant, colours
void pixmap_changed();                               // WM_FLXP_DWMHEADERCHANGED: luxwm's header background
void open_search();                                  // shows (navigation bar: it hides behind the magnifier) and focuses the box
void theme_changed();                                // the session theme: reload the buttons' images, repaint
HWND search_box();
std::string buttons_source();                        // where the Back/Forward/Favorites images come from per mode
std::string framelet_source();                       // where the Back/Forward images come from (path and the setting), or "drawn ..."
std::string background_source();
std::string metrics_info();                          // the rows per size, the travel buttons' places and style, the lower edge (--info)                     // what the header is painted on now (glass, navigation bar, pixmap/gradient)
void set_history(bool can_back, bool can_forward);  // enables the Back/Forward buttons
void edit_address();                                 // Ctrl+L: the breadcrumb becomes an edit field with the path (Enter goes, Esc leaves)

}
