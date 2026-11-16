// menuband.h: the menu band (the shell layer's win32 part, flxp/shell/ui/menuband.cpp). XP's Explorer menu is a toolbar
// band in the rebar, not a system menu bar; the Explorer has it under XP's chrome and under the Longhorn header, and an
// application under the Longhorn header (shell/lhheader.h) puts its menu there too, since luxwm's glass continues from the
// caption into the header and a menu bar would lie between them (mupf). This is a compact re-interpretation:
// text buttons for the top-level items, popups tracked below them, hover and
// arrow keys moving between menus, Alt/F10 keyboard access. One band per process.
// Include after the win32 headers and commctrl.h (wux/wux.h; w32/w32.h inside flxp); shell/shell.h does.
#pragma once

namespace menuband {

HWND create(HWND rebar, HWND owner, HMENU menu, int band_id);   // inserts the band at the top
void fonts_changed();                                           // the style's menu font again (WM_THEMECHANGED)
LRESULT on_notify(NMHDR* nm, bool* handled);                    // call from the owner's WM_NOTIFY
void on_menuselect(WPARAM wp, LPARAM lp);                       // call from the owner's WM_MENUSELECT
bool key_menu(WCHAR ch);                                        // SC_KEYMENU: 0 = Alt alone, else the mnemonic
void on_activate(bool active);                                  // call from the owner's WM_ACTIVATE
int height();                                                   // band height
bool active();                                                  // the owner is active (as last told)
// Longhorn's coloured bar: item drawing handed to fn (NM_CUSTOMDRAW of the band)
void set_custom_draw(LRESULT (*fn)(NMTBCUSTOMDRAW* cd, bool active));
// the band is gone (its rebar destroyed): the calls above do nothing until the next create
void forget();

}
