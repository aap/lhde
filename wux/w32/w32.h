// Internal header for the win32 shim: the objects behind win32 handles.
// Shim modules are C++ and include this; the vendored Wine C code never
// sees it. Every handle is a pointer to an object whose first field is a
// type tag, so GetObjectType is a load.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include "gdi/geom.h"
#include "gdi/color.h"
#include "gdi/region.h"
#include "gdi/bitmap.h"
#include "gdi/dc.h"
#include "gdi/font.h"
#include "win/window.h"

// The win32 headers, as seen by the shim. Keep this include set small and
// identical across shim modules so the ABI (WCHAR = 16-bit) is consistent.
extern "C" {
#define WINE_UNICODE_NATIVE 0
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
}

namespace w32 {

// Object type tags; values match GDI's OBJ_* so GetObjectType is trivial.
enum ObjType : uint32_t {
    T_PEN = 1, T_BRUSH = 2, T_DC = 3, T_FONT = 6, T_BITMAP = 7, T_REGION = 8, T_MEMDC = 10,
    T_ICON = 100, T_MENU = 101, T_MODULE = 102, T_IMAGELIST = 103,
};

// Every Obj registers itself so a bogus handle (Wine code passes COLORREF+1
// as a brush in places) is rejected like an invalid entry in GDI's handle table.
bool obj_live(const void* h);
struct Obj {
    uint32_t type; bool stock;
    Obj();
    Obj(uint32_t t, bool s);
    Obj(const Obj& o);
    Obj& operator=(const Obj& o) = default;
    ~Obj();
};

struct Pen : Obj { LOGPEN lp; };
struct Brush : Obj { LOGBRUSH lb; gdi::Bitmap* pattern; /* for BS_PATTERN; 8x8 */
    bool mono;                // pattern came from a 1bpp bitmap: black = text colour, white = bk colour
};
struct Font : Obj { LOGFONTW lf; gdi::Font* font; };
struct Bitmap : Obj {
    gdi::Bitmap* bmp;         // always 32bpp internally
    int32_t req_bpp;          // bpp the creator asked for (CreateDIBSection); informational
    bool dib;                 // created by CreateDIBSection
};
struct Region : Obj { gdi::Region rgn; };
struct Icon : Obj { gdi::Bitmap* image; /* ARGB, premultiplied no */ gdi::Point hotspot; bool is_cursor; };

// A device context. `dc` is the underlying gdi::DC (always valid; for a
// memory DC it targets the selected bitmap, initially a 1x1 stock bitmap).
struct DC : Obj {
    gdi::DC* dc;
    Bitmap* bitmap;           // selected bitmap (memory DC) or null for a window DC
    HWND hwnd;                // window the DC was obtained for, or null
    Pen* pen;
    Brush* brush;
    Font* font;
    COLORREF text_color, bk_color;
    int bk_mode;              // OPAQUE / TRANSPARENT
    UINT text_align;          // TA_*
    int rop2;
    int stretch_mode;
    POINT cur_pos;
    POINT brush_org;
    POINT viewport_org;       // SetViewportOrgEx; added to logical coords
    POINT window_org;         // SetWindowOrgEx; subtracted from logical coords
    gdi::Region base_clip;    // device region given by the window system (visible area ∩ update)
    Region* clip;             // app clip region selected with SelectClipRgn (device coords), or null
    DWORD layout;
    // Added by the GDI shim (gdi.cpp):
    gdi::Point base_origin;   // where device (0,0) sits in the target (window client origin in the backbuffer)
    int poly_fill_mode;       // ALTERNATE / WINDING
    int map_mode;             // only MM_TEXT is honoured
};

// Conversions
inline gdi::Color color_of(COLORREF c) { return gdi::rgb(GetRValue(c), GetGValue(c), GetBValue(c)); }
inline COLORREF colorref_of(gdi::Color c) { return RGB(c.r, c.g, c.b); }
inline gdi::Rect rect_of(const RECT& r) { return gdi::Rect{r.left, r.top, r.right, r.bottom}; }
inline RECT RECT_of(const gdi::Rect& r) { RECT o = {r.left, r.top, r.right, r.bottom}; return o; }
inline gdi::Point point_of(POINT p) { return gdi::Point{p.x, p.y}; }
inline win::Window* window_of(HWND h) { return (win::Window*)h; }
inline HWND hwnd_of(win::Window* w) { return (HWND)w; }

// Handle checks
// win32 passes small integers as pseudo-handles in places (HRGN 1 for
// "whole window", COLOR_xxx+1 brushes), so anything below 64K is not an object.
inline bool is_obj_ptr(const void* h) { return (uintptr_t)h >= 0x10000; }
template <class T> inline T* as(HANDLE h, uint32_t type) {
    if (!is_obj_ptr(h) || !obj_live(h)) return nullptr;
    Obj* o = (Obj*)h; return o->type == type ? (T*)o : nullptr;
}
inline DC* dc_of(HDC h) { if (!is_obj_ptr(h) || !obj_live(h)) return nullptr; Obj* o = (Obj*)h; return (o->type == T_DC || o->type == T_MEMDC) ? (DC*)o : nullptr; }

// Strings: WCHAR here is 16-bit (the shim is compiled with -fshort-wchar).
std::string utf8_of(const WCHAR* s, int len = -1);
std::u16string utf16_of(const char* s, int len = -1);
int utf16_len(const WCHAR* s);

// GDI shim entry points used by the user shim (implemented in gdi.cpp)
DC* create_window_dc(gdi::DC* base, HWND hwnd);       // wraps a gdi::DC obtained from win::get_dc/begin_paint
void destroy_window_dc(DC* dc);                        // does not free the underlying gdi::DC
void apply_clip(DC* dc);                               // recompute dc->dc clip from base_clip ∩ clip

// User shim (user.cpp)
struct Class;
struct Window;                                         // per-window shim data, hangs off win::Window::w32
Window* wdata(HWND h);
HMODULE comctl32_module();                             // comctl.cpp: embedded comctl32 resources
void init_theme();                                     // theme.cpp
bool set_theme(const char* msstyles_path, const char* color, const char* size);
// the session theme again from the root properties (_LH_THEME, _LH_THEME_RESOLVED, _LH_SETTINGS_SERIAL): a new record
// becomes shell::session_theme(), a new style is loaded, WM_THEMECHANGED goes out when the look changed. Cheap when
// nothing changed; called by the root hooks and by programs that must see the record before they act (flxp-desktop).
void session_theme_refresh();
void init_gdi();
void init_kernel();

} // namespace w32
