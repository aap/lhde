// What Wine's menu.c needs from its surroundings: a handle table for menus,
// capture, and a couple of nonclient hooks.
#include <set>
#include <vector>
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

int MENU_HitsTracking(int x, int y);

BOOL set_capture_window(HWND hwnd, UINT flags, HWND* prev)
{
    HWND old = GetCapture();
    win::set_menu_hit_test(MENU_HitsTracking);
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
BOOL MENU_LastInputKeyboard(void) { return win::last_input_keyboard(); }   // menu.c: a menu opened from the keyboard

// Accelerator tables (TranslateAcceleratorW is menu.c's): an HACCEL is a heap block holding its ACCELs, tagged so a bogus
// handle reads as an empty table. LoadAcceleratorsW reads RT_ACCELERATOR (an array of {WORD fVirt, key, cmd, pad}, the
// last entry's fVirt with 0x80).
struct AccelTable { uint32_t magic; std::vector<ACCEL> keys; };
static const uint32_t ACCEL_MAGIC = 0x4c434341;   // "ACCL"
static std::set<AccelTable*> g_accels;
static AccelTable* accel_of(HACCEL h)
{
    AccelTable* t = (AccelTable*)h;
    return g_accels.count(t) && t->magic == ACCEL_MAGIC ? t : nullptr;
}

HACCEL WINAPI CreateAcceleratorTableW(LPACCEL keys, INT n)
{
    if (!keys || n <= 0) { SetLastError(ERROR_INVALID_PARAMETER); return nullptr; }
    AccelTable* t = new AccelTable{ ACCEL_MAGIC, std::vector<ACCEL>(keys, keys + n) };
    for (ACCEL& a : t->keys) a.fVirt &= 0x7f;
    g_accels.insert(t);
    return (HACCEL)t;
}
HACCEL WINAPI CreateAcceleratorTableA(LPACCEL keys, INT n) { return CreateAcceleratorTableW(keys, n); }

INT WINAPI CopyAcceleratorTableW(HACCEL h, LPACCEL out, INT n)
{
    AccelTable* t = accel_of(h);
    if (!t) return 0;
    if (!out) return (INT)t->keys.size();
    INT k = n < (INT)t->keys.size() ? n : (INT)t->keys.size();
    for (INT i = 0; i < k; i++) out[i] = t->keys[(size_t)i];
    return k;
}
INT WINAPI CopyAcceleratorTableA(HACCEL h, LPACCEL out, INT n) { return CopyAcceleratorTableW(h, out, n); }

BOOL WINAPI DestroyAcceleratorTable(HACCEL h)
{
    AccelTable* t = accel_of(h);
    if (!t) return FALSE;
    g_accels.erase(t);
    t->magic = 0;
    delete t;
    return TRUE;
}

HACCEL WINAPI LoadAcceleratorsW(HINSTANCE inst, LPCWSTR name)
{
    HRSRC r = FindResourceW(inst, name, (LPCWSTR)RT_ACCELERATOR);
    if (!r) return nullptr;
    const BYTE* p = (const BYTE*)LockResource(LoadResource(inst, r));
    DWORD size = SizeofResource(inst, r);
    std::vector<ACCEL> keys;
    for (DWORD o = 0; p && o + 8 <= size; o += 8) {
        WORD fvirt = (WORD)(p[o] | p[o + 1] << 8);
        ACCEL a;
        a.fVirt = (BYTE)(fvirt & 0x7f);
        a.key = (WORD)(p[o + 2] | p[o + 3] << 8);
        a.cmd = (WORD)(p[o + 4] | p[o + 5] << 8);
        keys.push_back(a);
        if (fvirt & 0x80) break;
    }
    return keys.empty() ? nullptr : CreateAcceleratorTableW(keys.data(), (INT)keys.size());
}
HACCEL WINAPI LoadAcceleratorsA(HINSTANCE inst, LPCSTR name)
{
    if (IS_INTRESOURCE(name)) return LoadAcceleratorsW(inst, (LPCWSTR)name);
    std::u16string w = w32::utf16_of(name);
    return LoadAcceleratorsW(inst, (LPCWSTR)w.c_str());
}
void __wine_finally_handler(void) {}

/* menu.c keeps its class descriptor static; this is the same one (with XP's CS_DROPSHADOW: CreateWindowExW gives the popup
   its shadow) */
const struct builtin_class_descr POPUPMENU_builtin_class =
{
    (LPCWSTR)POPUPMENU_CLASS_ATOM, CS_DROPSHADOW | CS_SAVEBITS | CS_DBLCLKS, WINPROC_MENU, sizeof(HMENU), IDC_ARROW, (HBRUSH)(COLOR_MENU + 1)
};

} // extern "C"
