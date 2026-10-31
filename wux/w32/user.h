// Internal declarations for the user32 side of the shim.
#pragma once
#include <vector>
#include <string>
#include "w32.h"
extern "C" {
#include "controls.h"
#include "user_private.h"
}

namespace w32 {

struct Class {
    std::u16string name;
    ATOM atom;
    WNDPROC proc;
    UINT style;
    int cls_extra, wnd_extra;
    HINSTANCE inst;
    HICON icon, icon_sm;
    HCURSOR cursor;
    HBRUSH background;      // brush, or COLOR_xxx+1 pseudo handle, or null
    std::u16string menu_name;
    std::vector<uint8_t> cls_bytes;
};

struct Prop { std::u16string name; ATOM atom; HANDLE value; };

struct Window {              // hangs off win::Window::w32
    win::Window* hwnd;
    Class* cls;
    WNDPROC proc;
    HINSTANCE inst;
    HMENU menu;
    DWORD help_id;
    std::vector<uint8_t> extra;
    std::vector<Prop> props;
    WND wnd;                 // view handed to WIN_GetPtr
    gdi::DC* paint_dc;       // underlying gdi DC between BeginPaint/EndPaint
    bool in_paint;
};

inline Window* wdata(HWND h) { win::Window* w = window_of(h); return w ? (Window*)w->w32 : nullptr; }
Class* find_class(LPCWSTR name);
win::Window* desktop();
void init();                 // whole shim: gdi, syscolor, user, builtin classes

// defwnd.cpp
LRESULT nc_calc_size(HWND hwnd, WPARAM wparam, RECT* rect);
LRESULT nc_hit_test(HWND hwnd, POINT pt);
void nc_paint(HWND hwnd, HRGN clip);
void register_builtin_classes();

// sysparams.cpp
void init_sysparams();
// what MS Shell Dlg (and 2) stands for, as XP's FontSubstitutes: Tahoma, or a Longhorn style's message face (sysparams)
void set_shell_dlg(const char* face);
void adjust_frame_rect(RECT* rect, DWORD style, BOOL menu, DWORD ex);   // AdjustWindowRectEx counting our own frame



} // namespace w32
