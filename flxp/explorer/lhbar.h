// The Longhorn 4051 command bar (frame=longhorn): the rebar row that holds
// the menu band and, on the same row, a small toolbar, both on a coloured
// horizontal gradient - Longhorn's per-folder-type Rebar.BACKGROUND strips
// (Default/Document/Music/... in slate.msstyles) or a drawn gradient - with
// white text and the slate's ExplorerMenu::Toolbar button frames.
#pragma once
#include "w32/w32.h"
extern "C" {
#include "commctrl.h"
}

namespace lhbar {

// Loads the background and the button frames once (settings menubar_style,
// menubar_gradient, menubar_text, menubar_msstyles; see flxp.ini).
void init();
// false with menubar_style=theme: the row keeps the visual style's own rebar
// (as 4051's bar under Luna, lh4051-232843.png) and nothing here applies
bool enabled();
// WM_THEMECHANGED: reads the settings again (they may come from the session theme)
void theme_changed();
// The bar's background over rc (the rebar's client rectangle) into dc.
void paint_background(HDC dc, const RECT& rc);
// A button frame: 0 none, 1 hot, 2 pressed, 3 checked, 4 hot and checked.
void draw_frame(HDC dc, const RECT& r, int state);
COLORREF text_color(bool active);
// Subclasses the rebar so its background (and that of the transparent
// toolbars on it, which forward WM_ERASEBKGND) is the bar's.
void attach(HWND rebar);
void detach(HWND rebar);            // back to the rebar's own background (the XP look)
// NM_CUSTOMDRAW for a toolbar on the bar: white text and the bar's frames.
// checked(id) says whether a button counts as checked (drawn, not
// TBSTATE_CHECKED, which would get the classic dither).
LRESULT custom_draw(NMTBCUSTOMDRAW* cd, bool active, bool (*checked)(int id));

}
