// Platform layer: native toplevel windows, input events, presenting a bitmap.
// Everything inside a toplevel is the win layer's business.
#pragma once
#include <vector>
#include <stdint.h>
#include <string>
#include "gdi/geom.h"
#include "gdi/bitmap.h"

namespace sys {

struct NativeWindow;

enum class EventKind {
    Nothing, Exposed, Configured, Closed,
    MouseMove, ButtonDown, ButtonUp, Wheel, PointerLeft,
    GrabbedElsewhere,       // another client (the window manager) grabbed the pointer or keyboard
    KeyDown, KeyUp, FocusGained, FocusLost,
    // window = null: root state of the frame protocol changed
    CompositionChanged,     // _LHWM_COMPOSITED changed value
    FrameThemeChanged,      // _LHWM_THEME or _LHWM_FRAME_METRICS changed
    RootPropertyChanged,    // a watch_root_property() property changed; keysym = its id
    WindowPropertyChanged,  // lhwm changed a property of the window; keysym = WindowHeaderPixmap / WindowCaptionRect
    // XDND target side (window = our toplevel under the pointer; pos/root, mods = the
    // keyboard now; drag_allowed/drag_action = the source's actions and its request).
    // DragOver must be answered with drop_status, DragDrop with drop_finish.
    DragOver, DragLeave, DragDrop,
};
enum : uint32_t { WindowHeaderPixmap = 1, WindowCaptionRect = 2, WindowState = 3 };   // WindowState: _NET_WM_STATE

enum Mod : uint32_t {
    ModShift = 1, ModControl = 2, ModAlt = 4,
    ModLButton = 8, ModMButton = 16, ModRButton = 32,
};

struct Event {
    EventKind kind;
    NativeWindow* window;
    gdi::Point pos;      // relative to the window's client area
    gdi::Point root;     // screen coords
    int button;          // 1 left, 2 middle, 3 right
    int wheel;           // +1 away from user, -1 towards
    uint32_t mods;       // Mod flags
    uint32_t keysym;     // X keysym for now
    char text[8];        // utf-8 text of a key press, or ""
    gdi::Rect rect;      // Expose: damaged area; Configure: new screen rect
    uint32_t time;
    uint32_t drag_allowed, drag_action;   // DragOver/DragDrop: Drop* bits
};

bool init();
void shutdown();

enum class PointerShape { Arrow, IBeam, Wait, Cross, SizeWE, SizeNS, SizeNWSE, SizeNESW, SizeAll, Hand, No, Hidden };

// popup = undecorated, not managed by the window manager (menus, dropdowns).
// argb = 32-bit TrueColor visual with its own colormap: present() then pushes
// the backbuffer as premultiplied ARGB (ignored if the server has no such visual).
NativeWindow* create_window(const gdi::Rect& screen_rect, const char* title, void* user, bool popup = false,
                            bool argb = false);
bool argb_supported();
bool is_argb(NativeWindow* w);
// Switch an existing window to/from ARGB (recreates the X window; a mapped
// window is mapped again). False if there is no 32-bit visual.
bool set_argb(NativeWindow* w, bool on);
void set_cursor(NativeWindow* w, PointerShape shape);
void raise_window(NativeWindow* w);
// Ask the window manager to activate w (_NET_ACTIVE_WINDOW) and give it the
// input focus if it is viewable: a disabled owner hands activation to its dialog.
void activate_window(NativeWindow* w);
void destroy_window(NativeWindow* w);
void* window_user(NativeWindow* w);
void show_window(NativeWindow* w, bool show);
void set_title(NativeWindow* w, const char* title);
void set_window_rect(NativeWindow* w, const gdi::Rect& screen_rect);
gdi::Rect window_rect(NativeWindow* w);

// Frame protocol of the lhwm window manager (lhwm/CLIENT-PROTOCOL.md): the
// frame plate extends into the client (left, right, top, bottom; 0xFFFFFFFF
// in all four = sheet of glass; null removes it), flags as _LHWM_FRAME_FLAGS.
enum : uint32_t { FrameNoTitle = 1, FrameNoIcon = 2, FrameNoFog = 4, FrameClientCaption = 8 };
void set_frame_extension(NativeWindow* w, const int32_t* ltrb);
void set_frame_flags(NativeWindow* w, uint32_t flags);
uint32_t frame_flags(NativeWindow* w);
struct FrameInfo {
    bool composited = false;        // root _LHWM_COMPOSITED == 1 (missing = 0)
    bool have_metrics = false;      // _LHWM_FRAME_METRICS present
    int32_t caption_h = 0, border_l = 0, border_r = 0, border_b = 0;
    uint32_t text_rgb = 0, glow_rgb = 0, inactive_text_rgb = 0;   // 0xRRGGBB
    // items 14..16 (lhwm since 2026-10-25): the maximised caption text
    bool have_max = false;
    uint32_t max_text_rgb = 0, max_inactive_text_rgb = 0, max_glow_rgb = 0;   // max_glow 0xFFFFFFFF = none
    std::string theme;              // _LHWM_THEME
};
const FrameInfo& frame_info();      // cached, refreshed when the root properties change
// _LHWM_HEADER_PIXMAP (lhwm, not composited): the theme's caption band
// continued over the extension, client width x (top frame margin + extension
// top), its last row against the extension's bottom; copied into a new
// bitmap (premultiplied ARGB), null when there is none or it went away
gdi::Bitmap* header_pixmap(NativeWindow* w);
// _LHWM_CAPTION_RECT (CLIENT_CAPTION): lhwm's caption buttons in client coordinates
bool caption_rect(NativeWindow* w, gdi::Rect& r);
// The input shape: the whole window minus hole (null = the whole window)
void set_input_hole(NativeWindow* w, const gdi::Rect* hole);
// The window's shape (XShape bounding, SetWindowRgn): these rectangles, in window coordinates (null = rectangular)
void set_shape(NativeWindow* w, const std::vector<gdi::Rect>* rects);
// Hand a button press over to the window manager (_NET_WM_MOVERESIZE):
// direction 0..7 resize (top-left, top, top-right, right, bottom-right,
// bottom, bottom-left, left), 8 move, 9 size and 10 move with the keyboard.
void begin_move_resize(NativeWindow* w, gdi::Point root_pos, int direction, int button, uint32_t time);
// An active pointer grab for a popup that must see clicks anywhere (the taskbar's start panel):
// owner_events, so our own windows get their events as usual and clicks into other clients come
// to w (outside its rectangle) instead of to them. False if another client holds a grab.
bool grab_pointer(NativeWindow* w, bool on);
bool is_maximized(NativeWindow* w);         // _NET_WM_STATE has both MAXIMIZED atoms
void set_maximized(NativeWindow* w, int action);   // 0 restore, 1 maximise, 2 toggle

// Copy the given area of bmp to the window. bmp must be at least window-sized.
void present(NativeWindow* w, const gdi::Bitmap* bmp, const gdi::Rect& area);

// Returns false on timeout. timeout_ms < 0 waits forever.
bool wait_event(Event& ev, int timeout_ms);
// Callable from any thread: makes a blocked wait_event return (false) soon.
void wake();
// File descriptors the event loop watches (an inotify fd, a pipe): a blocked
// wait_event returns (false) when one is readable, and dispatch_fd_watches
// (the toolkit's message loop calls it, on the main thread) runs the callback
// of each readable one; the callback must read what is pending.
void watch_fd(int fd, void (*callback)(int fd, void* user), void* user);
void unwatch_fd(int fd);
void dispatch_fd_watches();

gdi::Point pointer_pos();
gdi::Size screen_size();
uint32_t ticks();    // monotonic milliseconds

// Window-manager hints for dialogs (src/filedlg, the portal backend). The
// native id is the X window id; owners may belong to other processes.
NativeWindow* window_for_user(void* user);          // the native window created with this user pointer
uint64_t native_id(NativeWindow* w);
void set_transient_for(NativeWindow* w, uint64_t owner_id, bool modal);   // + _NET_WM_WINDOW_TYPE_DIALOG
bool native_window_rect(uint64_t id, gdi::Rect& screen_rect);            // a foreign window's rect, frame excluded
void set_min_size(NativeWindow* w, int cx, int cy);
// a window the user cannot resize or maximize (win32: a caption without WS_THICKFRAME): WM_NORMAL_HINTS min == max == its
// size, following set_window_rect, and _MOTIF_WM_HINTS functions without resize and maximize
void set_fixed_size(NativeWindow* w, bool fixed);
// _NET_WM_ICON: width, height, then width*height ARGB pixels, for each size (empty: removed)
void set_icon(NativeWindow* w, const std::vector<unsigned long>& data);

// EWMH window type, before the window is first shown. Desktop: the desktop
// window of a shell (_NET_WM_WINDOW_TYPE_DESKTOP, no decorations, sticky,
// below everything, not in taskbars; lowered whenever it is mapped; it takes
// the input focus itself when clicked, since window managers do not manage it).
// Dock: a panel (_NET_WM_WINDOW_TYPE_DOCK, no decorations, sticky, above,
// not in taskbars; the program sets its struts itself).
enum class WindowType { Normal, Desktop, Dock };
void set_window_type(NativeWindow* w, WindowType t);

// Root window properties. root_cardinals reads a CARDINAL array (-1: absent);
// a watched property's changes arrive as EventKind::RootPropertyChanged with
// Event::keysym = the id watch_root_property returned (the same id for the
// same name). work_area is _NET_WORKAREA for _NET_CURRENT_DESKTOP.
int root_cardinals(const char* prop, long* out, int max);
std::string root_string(const char* prop);      // an 8-bit (STRING, UTF8_STRING) property, "" if absent
uint32_t watch_root_property(const char* prop);
bool root_window(const char* prop, uint64_t& id);   // a WINDOW property (_NET_ACTIVE_WINDOW); false if absent
void set_root_string(const char* prop, const std::string& value);   // UTF8_STRING; "" deletes it
bool work_area(gdi::Rect& r);       // false if the window manager publishes none

// ---- drag and drop: XDND version 5 (freedesktop.org/wiki/Specifications/XDND) ----
// Files travel as text/uri-list (also x-special/gnome-copied-files and the paths
// as text); the paths here are local paths, file: URIs on the wire.
enum : uint32_t { DropCopy = 1, DropMove = 2, DropLink = 4 };   // = win32's DROPEFFECT_*

// Source: start dragging these files from w while a button is down (the
// pointer and keyboard are grabbed until it is released or Escape is
// pressed). image: premultiplied ARGB shown under the pointer with the
// pointer at hotspot (null: none). The drag then runs inside wait_event:
// the caller pumps events while drag_active(); drag_result() is the effect
// the target reported (0: cancelled, refused or no target). Our own windows
// are targets like any other (the XDND messages go through the server).
bool drag_begin(NativeWindow* w, const std::vector<std::string>& paths, uint32_t allowed,
                const gdi::Bitmap* image, gdi::Point hotspot);
bool drag_active();
uint32_t drag_result();
uint32_t query_mods();      // the keyboard modifiers and buttons now (Mod flags)

// Target: every toplevel is XdndAware. During DragOver/DragDrop the files
// offered (fetched from the source once per drag, false if it offers none):
bool drop_paths(std::vector<std::string>& paths);
NativeWindow* drop_source_window();   // the source's window when it is ours, else null
void drop_status(uint32_t effect);    // the answer to the last DragOver (0 = refuse)
void drop_finish(uint32_t effect);    // the answer to DragDrop (0 = failed/refused)

} // namespace sys
