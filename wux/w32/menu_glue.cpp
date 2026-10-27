// What Wine's menu.c needs from its surroundings: a handle table for menus,
// capture, and a couple of nonclient hooks.
#include <set>
#include "user.h"

extern "C" {

static std::set<struct user_object*> g_user_objects;

HANDLE alloc_user_handle(struct user_object* ptr, enum user_obj_type type)
{
    ptr->type = type;
    ptr->handle = (HANDLE)ptr;
    g_user_objects.insert(ptr);
    return ptr->handle;
}

void* get_user_handle_ptr(HANDLE handle, enum user_obj_type type)
{
    if (!w32::is_obj_ptr(handle)) return nullptr;
    struct user_object* o = (struct user_object*)handle;
    if (!g_user_objects.count(o) || o->type != type) return nullptr;
    return o;
}

void release_user_handle_ptr(void*) {}

void* free_user_handle(HANDLE handle, enum user_obj_type type)
{
    void* p = get_user_handle_ptr(handle, type);
    if (p) g_user_objects.erase((struct user_object*)p);
    return p;
}

BOOL set_capture_window(HWND hwnd, UINT flags, HWND* prev)
{
    HWND old = GetCapture();
    if (hwnd) win::set_capture(w32::window_of(hwnd), (flags & GUI_INMENUMODE) != 0); else ReleaseCapture();
    if (prev) *prev = old;
    return TRUE;
}

HMODULE user32_module = nullptr;   // no user32 resources: no stock system menu

void NC_GetSysPopupPos(HWND hwnd, RECT* rect)
{
    GetWindowRect(hwnd, rect);
    rect->right = rect->left + GetSystemMetrics(SM_CXSIZE);
    rect->bottom = rect->top + GetSystemMetrics(SM_CYSIZE);
}

void NC_DrawSysButton(HWND, HDC, BOOL) {}

LRESULT NC_HandleNCHitTest(HWND hwnd, POINT pt) { return w32::nc_hit_test(hwnd, pt); }

SHORT WINAPI NtUserGetKeyState(INT vk) { return GetKeyState(vk); }
INT WINAPI CopyAcceleratorTableW(HACCEL, LPACCEL, INT) { return 0; }
void __wine_finally_handler(void) {}

/* menu.c keeps its class descriptor static; this is the same one */
const struct builtin_class_descr POPUPMENU_builtin_class =
{
    (LPCWSTR)POPUPMENU_CLASS_ATOM, CS_SAVEBITS | CS_DBLCLKS, WINPROC_MENU, sizeof(HMENU), IDC_ARROW, (HBRUSH)(COLOR_MENU + 1)
};

} // extern "C"
