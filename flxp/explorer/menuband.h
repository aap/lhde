// The menu band: XP's Explorer menu is a toolbar band in the rebar (browseui's
// CMenuBand), not a system menu bar. This is a compact re-interpretation:
// text buttons for the top-level items, popups tracked below them, hover and
// arrow keys moving between menus, Alt/F10 keyboard access.
#pragma once
#include "w32/w32.h"
extern "C" {
#include "commctrl.h"
}

namespace menuband {

HWND create(HWND rebar, HWND owner, HMENU menu, int band_id);   // inserts the band at the top
void set_menu(HMENU menu);                                      // rebuilds the buttons
void fonts_changed();                                           // the style's menu font again (WM_THEMECHANGED)
LRESULT on_notify(NMHDR* nm, bool* handled);                    // call from the owner's WM_NOTIFY
void on_menuselect(WPARAM wp, LPARAM lp);                       // call from the owner's WM_MENUSELECT
bool key_menu(WCHAR ch);                                        // SC_KEYMENU: 0 = Alt alone, else the mnemonic
void on_activate(bool active);                                  // call from the owner's WM_ACTIVATE
int height();                                                   // band height
bool active();                                                  // the owner is active (as last told)
// Longhorn's coloured bar: item drawing handed to fn (NM_CUSTOMDRAW of the band)
void set_custom_draw(LRESULT (*fn)(NMTBCUSTOMDRAW* cd, bool active));

}
