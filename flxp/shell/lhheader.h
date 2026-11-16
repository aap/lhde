// lhheader.h: the Longhorn header of a window, as the 4051 and 4074 Explorer draw it (the shell layer's win32 part,
// flxp/shell/ui/lhheader.cpp; docs/APPLICATIONS.md "The Longhorn header"). The band at the top of the client area with
// the round Back and Forward buttons, the place's name in a large font, the breadcrumb (segments with a drop-down
// each, an address edit over it: Ctrl+L or a click on its empty part) and the search box. Under luxwm with a
// compositor it lies on the window manager's glass (the frame extended into the client, docs/CLIENT-PROTOCOL.md:
// transparent with glowing text). Without composition it is opaque: the navigation bar of the session theme's
// Longhorn msstyles (Navbar::Rebar, splitter, travel/favorites buttons, fonts and colours by focus and maximise
// state), else luxwm's _LHWM_HEADER_PIXMAP (the caption band continued, e.g. Luna) under the glass layout, else (no
// luxwm) the 4074 Slate gradient. The empty parts answer HTTRANSPARENT so the frame's WM_NCHITTEST makes them a
// caption (hittest). Vision mode: when luxwm's frame theme has a nav.ini (header_engine = auto | lhnav | flxp), the
// contents are luxnav's (docs/NAVBAR.md): the concept art's orb, title in luxwm's deco, breadcrumb, search box and
// toolbar; the behaviour stays this one's.
//
// One header per process (the Explorer's window, the file dialog, an application's main window): its state is the
// module's. The host is a top-level window; the header is its child at (0, 0), the host lays its own children out
// below height(). The host gives the crumbs and is called back for what the user does; what the crumbs mean (folders,
// a document's outline) is the host's. Include after the win32 headers (wux/wux.h, or w32/w32.h inside flxp);
// shell/shell.h does.
#pragma once
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include <functional>
#include <string>
#include <vector>
#pragma pop_macro("max")
#pragma pop_macro("min")

struct FLXPDROP;

namespace shell {
namespace lhheader {

// ---- the frame: whether a window gets the header, and what luxwm must be told (docs/CLIENT-PROTOCOL.md) ----------------

// luxwm is the window manager (its _NET_SUPPORTING_WM_CHECK name "lhwm"; FLXP_LHWM=1 / 0 for tests); refresh asks again
bool lhwm_running(bool refresh = false);
std::string wm_label();              // the window manager's name for a log line ("no window manager" when none)
// the session theme's Explorer.chrome is auto (neither xp nor longhorn): the header follows luxwm's coming and going
bool frame_auto();
// The session theme wants the Longhorn header: Explorer.chrome = longhorn (the vision header only under luxwm, whose
// frame has its title and buttons), or auto with luxwm running. `why` says how that was decided.
bool want_longhorn(std::string* why = nullptr);
// luxwm's glass continued behind the header (DwmExtendFrameIntoClientArea by extend_top()), the frame's own title and
// icon off when the header shows the title (DWMWA_FLXP_FRAME_FLAGS: NO_ICON, NO_TITLE, CLIENT_CAPTION), and the input
// hole over luxwm's caption buttons (caption_hole)
void extend_frame(HWND frame);
// CLIENT_CAPTION: luxwm's caption buttons lie under the header; clicks there go through to them (FlxpDwmSetInputHole
// over FlxpDwmGetCaptionRect). Again on WM_SIZE and WM_FLXP_DWMCAPTIONRECTCHANGED.
void caption_hole(HWND frame);
void unextend_frame(HWND frame);     // luxwm's own caption with title and icon again (the header destroyed)
// The header's height follows the frame (glass: 76 rows less luxwm's caption): measured again, the frame extended
// again, layout() called for the host's children, the header repainted. On WM_DWMCOMPOSITIONCHANGED,
// WM_DWMCOLORIZATIONCOLORCHANGED and WM_THEMECHANGED.
void refit(HWND frame, const std::function<void()>& layout);
// WM_NCHITTEST of the frame: the header's empty parts (it answers HTTRANSPARENT there) are the caption, the vision
// header's top rows the top edge. False when there is no header.
bool hittest(HWND frame, LPARAM lp, LRESULT* result);
// active as luxwm draws the frame (_NET_ACTIVE_WINDOW), else focus_active (no EWMH window manager)
bool frame_active(HWND frame, bool focus_active);
// All of the above for a host that has nothing else to do with them: the frame's messages the header follows (hit
// test, composition, luxwm's caption rectangle, state, header pixmap, activation, theme). True when handled (*result
// is the answer); WM_SIZE, WM_ACTIVATE and WM_THEMECHANGED return false after their part so the host does its own
// too. layout: the host's children below height(). Call create() first.
bool frame_message(HWND frame, UINT msg, WPARAM wp, LPARAM lp, LRESULT* result, const std::function<void()>& layout);

// ---- the header -------------------------------------------------------------------------------------------------------

// A breadcrumb segment. The last one is the leaf: the place shown (the Explorer: the folder; its name is also the
// title unless set_title gave one).
struct Crumb {
    std::string text;            // as shown
    std::string key;             // the host's (the Explorer: the folder's path): given back by go()
    std::string icon;            // an icon key (shell::icon_key); the 4051/4074 breadcrumb draws none, kept for the host
    bool dropdown = true;        // its chevron opens a drop-down (entries()); false: the chevron is only a separator
    std::string tip;             // the vision chevron's tooltip ("Folders next to Documents"); "" none
};
// an entry of a crumb's drop-down
struct Entry {
    std::string text, key;       // shown; given to go() when chosen
    bool bold = false;           // the one on the path (4074: the crumb itself among its siblings)
};
// command()'s ids for the travel buttons
enum { TRAVEL_BACK = 1, TRAVEL_FORWARD = 2 };

struct Callbacks {
    // a crumb clicked, a drop-down entry or a collapsed ancestor («) chosen: its key
    std::function<void(const std::string& key)> go;
    // crumb i's drop-down: 4074's siblings of crumb i (itself bold); children = true (vision, header_crumb_menu =
    // children, Vista's) its children instead (the next crumb bold). Empty: "(Empty)", greyed.
    std::function<std::vector<Entry>(int i, bool children)> entries;
    // the address edit's Enter: the text typed, trimmed. Unset: no address edit
    std::function<void(const std::string& text)> address;
    std::function<void(const std::string& path)> bookmark;   // a vision toolbar bookmark (nav.ini `bookmark:PATH:NAME`)
    std::function<void(const std::string& text)> filter;     // the search box changed (live)
    std::function<void(bool keep)> done;                     // Enter in the search box (keep the filter) or Escape (cleared)
    std::function<void(int id)> command;                     // TRAVEL_BACK, TRAVEL_FORWARD; a vision toolbar button without tool()
    std::function<void(bool back, int x, int y)> history;    // the buttons' drop-down: a history menu at screen (x, y)
    std::function<void(int x, int y)> favorites;             // the star: the host's bookmarks at screen (x, y)
    std::function<void(int id, int x, int y)> tool;          // a set_tools() button; (x, y): screen, under it (its menu)
    // a drag over crumb i (an XDND drop, wux's WM_FLXP_DRAGOVER / WM_FLXP_DROP): the effect, or what the drop did when
    // drop is true. Unset: the crumbs are no drop targets
    std::function<LRESULT(int i, const FLXPDROP* d, bool drop)> drop;
    // the host's ids for the vision toolbar's Views and New Folder items (nav.ini `tools`); 0: the item is left out
    int views_id = 0, new_folder_id = 0;
};

// Glyph buttons of the host on the breadcrumb's row, left of the magnifier
// (the file dialog's New Folder and Views, as 4051's command row has glyph
// buttons instead of 4074's "Up a Folder / New Folder / Change View" strip).
// The icons are copied (16 px); a drop-down one calls back on the press.
struct Tool { int id; HICON icon; bool dropdown; };
void set_tools(const std::vector<Tool>& tools);
void enable_tool(int id, bool on);
bool tool_rect(int id, RECT* screen);

// 4074's Toolbar Size and Toolbars (its system menu), as
// settings (flxp.ini, View > Toolbars): <prefix>header_size = small | medium |
// large | xlarge, and <prefix>header_buttons / _title / _breadcrumb / _search /
// _favorites = 1 | 0. Small: one row under the frame's caption (which keeps the
// title) with the small Back/Forward buttons, the breadcrumb, the small
// magnifier and star; Medium: 4074's 76 rows (title over the
// breadcrumb); Large / Extra-Large: Medium plus 32 / 49 empty rows, the
// magnifier and star (small / normal) at the bottom.
enum Size { SIZE_SMALL, SIZE_MEDIUM, SIZE_LARGE, SIZE_XLARGE };
enum Element { SHOW_BUTTONS, SHOW_TITLE, SHOW_BREADCRUMB, SHOW_SEARCH, SHOW_FAVORITES, ELEMENTS };
// before create(): the settings' prefix ("" the explorer, "filedlg_" the
// dialog, whose elements fall back to the explorer's) and the default size
// ("" = the explorer's: its header_size, else the theme's Explorer.navbar_preset, else medium)
void configure(const std::string& prefix, const std::string& default_size);
// before create(): the host has no use for element e (a viewer's search box, its favorites): off whatever the
// settings say, and left out of append_menu's checks
void hide(Element e);
Size size();
bool shown(Element e);
bool draws_title();                 // the header shows the title (else the frame's caption: Small, or Title off)
void set_size(Size s);              // stored; the host refits (refit)
// luxwm runs (lhwm_running): without it the header has the Small size, whatever is chosen (the other window
// manager's caption shows the title); true if that changed (the host refits)
bool set_lhwm(bool on);
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
HWND create(HWND frame, const Callbacks& cb);        // child of frame, at (0,0)
void destroy();                                      // the frame goes back to the XP look (a theme switch)
// the breadcrumb, left to right, the leaf last; title: the large title ("" = the leaf's text) unless set_title gave one
void set_crumbs(const std::vector<Crumb>& crumbs, const std::string& title = std::string());
void set_leaf(const std::string& text);              // the leaf's text alone (a viewer's page): the crumbs stay
void clear_filter();                                 // empties the search box (no callback)
void set_title(const std::string& title);            // the title shown instead of the leaf's ("" = the leaf's; the file dialog: "Open")
void relayout(int width, int height);
void composition_changed();                          // repaint: glass or opaque (the height may change: height_for_caption)
void state_changed();                               // _NET_WM_STATE: maximised or restored (title colours, button variants)
void set_active(bool active);                        // luxwm shows the frame active (_NET_ACTIVE_WINDOW): navigation bar variant, colours
void pixmap_changed();                               // WM_FLXP_DWMHEADERCHANGED: luxwm's header background
void open_search();                                  // shows (navigation bar: it hides behind the magnifier) and focuses the box
void theme_changed();                                // the session theme: reload the buttons' images, repaint
HWND window();                                       // the header's window (null: none)
HWND search_box();
std::string buttons_source();                        // where the Back/Forward/Favorites images come from per mode
std::string framelet_source();                       // where the Back/Forward images come from (path and the setting), or "drawn ..."
std::string background_source();                     // what the header is painted on now (glass, navigation bar, pixmap/gradient)
std::string metrics_info();                          // the rows per size, the travel buttons' places and style, the lower edge (--info)
void set_history(bool can_back, bool can_forward);  // enables the Back/Forward buttons
void edit_address();                                 // Ctrl+L: the breadcrumb becomes an edit field with the address (Enter goes, Esc leaves)
// what the address edit starts with ("" = the leaf's key: the Explorer's path; a viewer's page number)
void set_address(const std::string& text);

} // namespace lhheader
} // namespace shell
