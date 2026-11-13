// A window's icon for the classic taskbar (shell/icons.h window_icon): liblhde's lookup with the window's WM_CLASS, its
// WM_HINTS pixmap between the class map and the generic icon, as luxwm and luxshell order them.
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "shell/icons.h"

namespace shell {

std::vector<unsigned long> window_icon(void* display, unsigned long window, int px, std::vector<unsigned long> (*hint)(unsigned long))
{
    XClassHint ch = { nullptr, nullptr };
    XGetClassHint((Display*)display, (Window)window, &ch);
    std::string cls = ch.res_class ? ch.res_class : "", inst = ch.res_name ? ch.res_name : "";
    if (ch.res_class) XFree(ch.res_class);
    if (ch.res_name) XFree(ch.res_name);
    std::vector<unsigned long> v = app_icon(cls, inst, "", px, false);
    if (v.empty() && hint) v = hint(window);
    if (v.size() < 3) v = app_icon("", "", "", px, true);
    return v;
}

} // namespace shell
