// Internal declarations for images, icons, image lists and system colours
// (image.cpp, syscolor.cpp).
#pragma once
#include "w32/w32.h"
extern "C" {
#include "commctrl.h"
}

namespace res { struct Module; }

namespace w32 {

// An HIMAGELIST. Every image is a cx*cy 32bpp ARGB bitmap, non-premultiplied;
// the mask (ILC_MASK) is folded into alpha.
inline Icon* icon_of(HICON h) { return as<Icon>((HANDLE)h, T_ICON); }

// Helpers shared by image.cpp/syscolor.cpp.
Bitmap* new_bitmap(gdi::Bitmap* b, bool dib, int bpp = 32);   // wraps; takes ownership of b
Icon* new_icon(gdi::Bitmap* argb, bool cursor, gdi::Point hotspot);  // takes ownership
gdi::Bitmap* copy_bitmap(const gdi::Bitmap* b);
// Resample (nearest) b to w*h; returns a new bitmap (a copy when sizes match).
gdi::Bitmap* scale_bitmap(const gdi::Bitmap* b, int w, int h);
// Draw an ARGB image into dc at x,y (logical), scaled to w*h, alpha-composited.
void draw_argb(gdi::DC* dc, int x, int y, int w, int h, const gdi::Bitmap* src, const gdi::Rect& sr,
               uint8_t alpha = 255);
res::Module* std_module();          // user32's OEM bitmaps/icons/cursors (HINSTANCE NULL)

void init_syscolor();               // idempotent; called lazily
void reset_syscolors();             // back to the classic scheme (before a visual style sets its own)

} // namespace w32
