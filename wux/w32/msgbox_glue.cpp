// What Wine's msgbox.c (MessageBoxW and the rest: wux/vendor/wine/user32/msgbox.c) needs from its surroundings: the
// MSGBOX template and the button names (wux/rc/user32.res, cut from Wine's user32.rc) and the owner made known to the
// window manager.
#include "res/res.h"   // before user.h: winuser.h's RT_* macros clash with res::
#include "user.h"
#include "sys/sys.h"

asm(".section .rodata\n"
    ".global wux_user32_res_start\n.global wux_user32_res_end\n"
    "wux_user32_res_start:\n.incbin \"wux/rc/user32.res\"\nwux_user32_res_end:\n"
    ".previous\n");
extern "C" const unsigned char wux_user32_res_start[], wux_user32_res_end[];

extern "C" {

HMODULE WUX_User32Resources(void)
{
    static res::Module* m;
    if (!m) m = res::load_memory(wux_user32_res_start, (size_t)(wux_user32_res_end - wux_user32_res_start));
    return (HMODULE)m;
}

// the box is a modal dialog of its owner's toplevel (WM_TRANSIENT_FOR, _NET_WM_STATE_MODAL): luxwm keeps it above
void WUX_MsgBoxOwner(HWND hwnd, HWND owner)
{
    HWND top = GetAncestor(owner, GA_ROOT);
    sys::NativeWindow* nw = sys::window_for_user((void*)hwnd);
    sys::NativeWindow* ow = top ? sys::window_for_user((void*)top) : nullptr;
    if (nw && ow) sys::set_transient_for(nw, sys::native_id(ow), true);
}

}
