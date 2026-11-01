// The window tree, messages and painting: the heart of the toolkit.
// One native window per toplevel; every child is virtual and lives in the
// toplevel's backbuffer. The model is win32's: a window has a window rect
// (in its parent's client coordinates) and a client rect (in window
// coordinates, set through WM_NCCALCSIZE); input is posted to a queue and
// retrieved with get_message so loops can filter it; WM_PAINT is generated
// when the queue is idle.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include "gdi/geom.h"
#include "gdi/color.h"
#include "gdi/region.h"
#include "gdi/dc.h"
#include "msg.h"

namespace win {

using gdi::Rect;
using gdi::Point;

struct Window;
typedef Result (*Proc)(Window* w, uint32_t msg, WParam wp, LParam lp);

struct Class {
    const char* name;
    Proc proc;
    uint32_t style = 0;
    gdi::Color background = gdi::rgb(0, 0, 0);
    bool paint_background = false;   // def_proc erases with background if true
};

struct Toplevel;

struct Window {
    const Class* cls;
    Proc proc;
    Window* parent;
    Window* owner;          // for toplevels: owning toplevel, or null
    Window* child;          // topmost child
    Window* next;           // sibling below
    Window* prev;           // sibling above
    Rect rect;              // window rect: parent client coords; screen coords for a toplevel
    Rect client;            // client rect in window coords
    uint32_t style, ex_style;
    uint32_t class_style;   // CS_* flags (the shim sets them from the win32 class)
    intptr_t id;
    std::string text;       // UTF-8
    void* userdata;         // the application's
    void* w32;              // the win32 shim's
    gdi::Region update;     // client coords
    bool erase;             // background needs erasing
    bool nc_invalid;        // nonclient area needs painting
    bool track_leave, track_nc_leave;
    bool destroying;
    bool wm_decorated;      // toplevel whose frame is drawn by the window manager
    Toplevel* top;          // non-null for toplevels only
};

struct Message {
    Window* w;
    uint32_t msg;
    WParam wp;
    LParam lp;
    uint32_t time;
    Point pt;               // screen coords of the mouse at post time
    char text[8];           // for WM_KEYDOWN: the character(s) the key produces
};

struct PaintInfo {
    gdi::DC* dc;
    Rect rect;              // client coords, bounds of what needs painting
    bool erase;
};

struct WindowPos {          // lparam of WM_WINDOWPOSCHANGED
    Window* w;
    Window* insert_after;
    Rect rect;              // new window rect, parent coords
    uint32_t flags;
};

enum class Cursor { Arrow, IBeam, Wait, Cross, SizeWE, SizeNS, SizeNWSE, SizeNESW, SizeAll, Hand, No, None };

bool init();
void shutdown();

void register_class(const Class& c);
const Class* find_class(const char* name);

Window* create(const char* cls, const char* text, uint32_t style, uint32_t ex_style, const Rect& r,
               Window* parent = nullptr, Window* owner = nullptr, intptr_t id = 0, void* param = nullptr);
void destroy(Window* w);
bool is_window(Window* w);

void show_window(Window* w, int cmd);  // SW_*
bool is_visible(Window* w);            // this window and all ancestors
void enable(Window* w, bool enable);
bool is_enabled(Window* w);
bool set_window_pos(Window* w, Window* insert_after, const Rect& r, uint32_t flags);
void set_parent(Window* w, Window* parent);
void recalc_client(Window* w);         // re-run WM_NCCALCSIZE
Rect client_rect(Window* w);           // (0,0,w,h)
Rect window_rect(Window* w);           // screen coords
Point window_origin(Window* w);        // screen coords of the window rect's top-left
Point client_to_screen(Window* w, Point p);
Point screen_to_client(Window* w, Point p);
Point map_point(Window* from, Window* to, Point p);   // null = screen
Window* toplevel_of(Window* w);
Window* window_from_point(Point screen);               // deepest window

void set_text(Window* w, const char* text);
const std::string& text(Window* w);

void invalidate(Window* w, const Rect* r = nullptr, bool erase = true);
void invalidate_region(Window* w, const gdi::Region& r, bool erase = true);   // client coords
void invalidate_frame(Window* w);
void validate(Window* w, const Rect* r = nullptr);
void validate_region(Window* w, const gdi::Region& r);
void redraw(Window* w, const Rect* r, uint32_t rdw_flags);
void update(Window* w);                // paint this window now (frame, then client, then children)
gdi::DC* begin_paint(Window* w, PaintInfo& pi, bool send_erase = true);
void end_paint(Window* w, PaintInfo& pi);
gdi::DC* get_dc(Window* w);            // client area, origin at client top-left
gdi::DC* get_window_dc(Window* w);     // whole window, origin at window top-left
void release_dc(Window* w, gdi::DC* dc);
Point device_origin(Window* w);                 // client origin in backbuffer coords
// Scroll the backbuffer contents of w's client area; returns the area that needs repainting.
void scroll_window(Window* w, int32_t dx, int32_t dy, const Rect* scroll, const Rect* clip,
                   bool move_children, gdi::Region* uncovered);

Result send(Window* w, uint32_t msg, WParam wp = 0, LParam lp = 0);
void post(Window* w, uint32_t msg, WParam wp = 0, LParam lp = 0);   // any thread (others: queued + sys::wake)
// A file descriptor the message loop watches (sys::watch_fd): callback(fd, user) on the
// main thread, between messages, whenever it is readable (it must read what is pending)
void watch_fd(int fd, void (*callback)(int fd, void* user), void* user);
void unwatch_fd(int fd);
bool is_ui_thread();
Result def_proc(Window* w, uint32_t msg, WParam wp, LParam lp);

// Message loop. get_message blocks and returns false on WM_QUIT. Filters:
// w = only this window (null = any), min/max = message range (0,0 = all).
bool get_message(Message& m, Window* filter = nullptr, uint32_t min = 0, uint32_t max = 0);
bool peek_message(Message& m, Window* filter, uint32_t min, uint32_t max, bool remove);
bool translate_message(const Message& m);       // posts WM_CHAR for WM_KEYDOWN with text
Result dispatch_message(const Message& m);
void quit(int code = 0);
int run();
void present_now();                             // flush dirty backbuffer areas to the screen
void wait_message();                            // block until some input, timer or paint is pending
void toplevels(std::vector<Window*>& out);      // in z-order (creation order for now)

void set_capture(Window* w, bool screen_coords = false);   // screen_coords: mouse lparams in screen space, as in win32 menu mode
void release_capture();
Window* capture();
void set_focus(Window* w);
Window* focus();
Window* active_window();
void track_mouse_leave(Window* w, bool nonclient = false);
int16_t key_state(uint32_t vk);                 // like GetKeyState: 0x8000 if down
bool key_down(uint32_t vk);
uint32_t scan_from_vk(uint32_t vk, bool* extended = nullptr);   // PC/AT set-1 scan code, 0 if none
uint32_t vk_from_scan(uint32_t scan, bool extended = false);
Point cursor_pos();
void set_cursor(Cursor c);
gdi::Size screen_size();

void set_timer(Window* w, uintptr_t id, uint32_t ms, void* callback = nullptr);
void kill_timer(Window* w, uintptr_t id);

// Window manager frames (the luxwm protocol, X11's DwmExtendFrameIntoClientArea).
// A toplevel can be ARGB: its backbuffer then tracks alpha (gdi::Bitmap::
// has_alpha), cleared areas are transparent and show the frame plate when the
// frame is extended under them. Switching recreates the native window.
bool set_argb(Window* w, bool on);              // false if not a toplevel or no 32-bit visual
void style_changed(Window* w);                  // GWL_STYLE set: a toplevel's sizing border (the WM's fixed size) follows
bool is_argb(Window* w);
void set_frame_extension(Window* w, const int32_t* ltrb);   // left right top bottom, null = none
enum : uint32_t { FRAME_NO_TITLE = 1, FRAME_NO_ICON = 2, FRAME_NO_FOG = 4, FRAME_CLIENT_CAPTION = 8 };
void set_frame_flags(Window* w, uint32_t flags);
uint32_t frame_flags(Window* w);
bool composited();                              // the WM draws glass and honours extensions
struct FrameMetrics {
    int32_t caption_h, border_l, border_r, border_b; gdi::Color text, glow, inactive_text;
    bool have_max; gdi::Color max_text, max_inactive_text, max_glow; bool max_glow_none;   // maximised caption text (luxwm items 14..16)
};
bool frame_metrics(FrameMetrics& m);            // false if the WM publishes none
std::string frame_theme();                      // the WM's _LHWM_THEME (luxwm's theme directory), "" if none
gdi::Bitmap* header_pixmap(Window* w);          // luxwm's _LHWM_HEADER_PIXMAP copied (caller destroys), null if none
bool caption_rect(Window* w, Rect& r);          // _LHWM_CAPTION_RECT: luxwm's caption buttons, client coordinates
void set_input_hole(Window* w, const Rect* r);  // clicks in r go through to the window manager (null: none)
void set_shape(Window* w, const std::vector<Rect>* rects);   // a toplevel's shape (SetWindowRgn; null: rectangular)
void set_drop_shadow(Window* w, int size, int (*alpha)(int, int, int, int));   // a popup's drop shadow (sys::set_drop_shadow)
// SPI_GETDROPSHADOW / SPI_SETDROPSHADOW: XP's "Show shadows under menus" (on by default), for the popups created after it
bool drop_shadows();
void set_drop_shadows(bool on);
// SPI_GETKEYBOARDCUES / SPI_SETKEYBOARDCUES: mnemonics underlined always (off by default, XP's "Hide underlined letters for
// keyboard navigation until I press the Alt key"), and whether the last input was a key press rather than a click (XP's
// LINP_KEYBOARD: a menu opened then underlines its mnemonics)
bool keyboard_cues();
void set_keyboard_cues(bool on);
bool last_input_keyboard();
// Caption behaviour for a WM-framed toplevel: let the window manager run a
// move (HTCAPTION), a resize (HTLEFT..HTBOTTOMRIGHT, HTSIZE) or a keyboard
// move/size (MOVE_KEYBOARD/SIZE_KEYBOARD) for the last button press.
enum : intptr_t { MOVE_KEYBOARD = -100, SIZE_KEYBOARD = -101 };
bool begin_move_resize(Window* w, intptr_t ht);
bool is_maximized(Window* w);
void set_maximized(Window* w, int action);      // 0 restore, 1 maximise, 2 toggle

// Shells: root window properties (CARDINAL arrays). A watched property's
// changes are posted to every toplevel as WM_FLXP_ROOTPROPERTY (wp = the id).
// work_area is the window manager's _NET_WORKAREA (else the screen).
uint32_t watch_root_property(const char* prop);
int root_cardinals(const char* prop, long* out, int max);   // -1 if absent
std::string root_string(const char* prop);                  // STRING/UTF8_STRING, "" if absent
void set_root_string(const char* prop, const std::string& value);   // UTF8_STRING, "" deletes
// The window manager's active window (root _NET_ACTIVE_WINDOW, what luxwm draws
// with the active frame): 1 it is w's toplevel, 0 another or none, -1 the WM
// publishes no _NET_ACTIVE_WINDOW (no EWMH WM: use the focus instead).
int wm_active(Window* w);
// fn runs once after changes of the watched root property, when the pending X
// events have been handled (a burst of changes is one call)
void on_root_property_change(const char* prop, void (*fn)());
Rect work_area();

// Drag and drop of files, between our windows and with other X programs (XDND 5,
// sys.h). Source: drag_files starts it from a button press (after the drag
// threshold, e.g. on LVN_BEGINDRAG) and runs a modal loop until the drop is
// done or cancelled (win32's DoDragDrop); it returns the effect the target
// performed (0: none). image (premultiplied ARGB, may be null) is shown under the
// pointer, the pointer at hotspot. Target: every toplevel; the window under the
// pointer gets WM_FLXP_DRAGOVER / WM_FLXP_DROP / WM_FLXP_DRAGLEAVE with a DropInfo.
enum : uint32_t { DROP_COPY = 1, DROP_MOVE = 2, DROP_LINK = 4 };      // = DROPEFFECT_*
struct DropInfo {               // the same layout as flxp_shell.h's FLXPDROP
    Window* target;             // the deepest window under the pointer
    int32_t x, y;               // the pointer, screen coordinates
    uint32_t allowed;           // the effects the source allows
    uint32_t keys;              // MK_SHIFT | MK_CONTROL | MK_ALT now
    uint32_t effect;            // what the source asks for (by its keys; plain = move)
    int32_t local;              // the source is this process
    uint32_t count;             // the files (local paths, UTF-8)
    const char* const* paths;
    Window* source;             // the source window if it is ours, else null
};
enum : uint32_t { MK_ALT = 0x20 };
uint32_t drag_files(Window* source, const std::vector<std::string>& paths, uint32_t allowed,
                    const gdi::Bitmap* image = nullptr, Point hotspot = Point{0, 0});

bool create_caret(Window* w, int32_t width, int32_t height);
void destroy_caret();
void set_caret_pos(Point p);
Point caret_pos();
void show_caret(Window* w);
void hide_caret(Window* w);
Window* caret_window();

} // namespace win
