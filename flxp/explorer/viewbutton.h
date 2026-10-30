// Build 4074's view control (System.Windows.Explorer's ViewButton and
// ViewModeSlider; the viewbutton, splitbutton and viewpopup XAML), at the
// bottom left of the Explorer's left pane: a split button "View" (imageres
// 7410-7412 and 7407-7409, the arrows 7413) whose left part goes to the next
// view (Details -> Tiles -> Icons, each as it was last) and shows the view's
// picture, and whose arrows open the slider: a white popup with a vertical
// slider beside the five stops' pictures (imageres 7400-7404, hot 7405,
// pressed 7406; Details, Tiles, Large, Medium, Small Icons). Dragging the
// thumb sizes the view live, linearly within each stop (the size jumps from
// 96 to 256 between Tiles and Large); a click on a picture shows that stop as
// it was last and closes the popup. The art comes from the imported 4074
// pack (packs/4074/imageres); without it the control draws plain shapes.
//
// It only chooses Views (browser::show_view, view_command); another control
// (4051's two-mode slider) could sit on the same model.
#pragma once
#include "w32/w32.h"
#include "browser.h"

namespace viewbutton {

enum { STRIP_H = 25 };                        // the strip it sits in: 2 + the 22 px button + 1
// the strip (the pane's white, the button at 2, 2) for a browsing widget; the host lays it
// out under its left pane (the explorer, the file dialog)
HWND create(HWND parent, browser::Browser& b);
void destroy(HWND strip);
void changed(browser::Browser& b);            // the view changed: the button's picture (a view listener does it)

}
