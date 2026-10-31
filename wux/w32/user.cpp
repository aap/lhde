// user32 shim, part 1: classes, windows, messages, painting, input, timers,
// carets, properties. See defwnd.cpp for DefWindowProc and sysparams.cpp
// for metrics and the small stuff.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <deque>
#include "user.h"
#include "sys/sys.h"
#include "wuxtheme/ut.h"   // ut_menu_shadow_alpha

using namespace w32;

namespace w32 {

static std::vector<Class*> g_classes;
static ATOM g_next_atom = 0xC001;
static Window* g_pending;             // data for the window being created
static win::Window* g_desktop;
static std::deque<win::Message> g_recent;   // for TranslateMessage
static DWORD g_msg_time;
static POINT g_msg_pos;
static HWND g_insert_after_hwnd(win::Window* w)
{
    if (w == WIN_HWND_TOP) return (HWND)0;
    if (w == WIN_HWND_BOTTOM) return (HWND)1;
    return hwnd_of(w);
}

static bool ieq(const std::u16string& a, const std::u16string& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        char16_t x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return false;
    }
    return true;
}

Class* find_class(LPCWSTR name)
{
    if (!name) return nullptr;
    if (!((ULONG_PTR)name >> 16)) {
        ATOM a = LOWORD((ULONG_PTR)name);
        for (Class* c : g_classes) if (c->atom == a) return c;
        return nullptr;
    }
    std::u16string n((const char16_t*)name);
    if (n.size() > 1 && n[0] == '#') {
        ATOM a = (ATOM)atoi(utf8_of(name + 1).c_str());
        for (Class* c : g_classes) if (c->atom == a) return c;
    }
    for (Class* c : g_classes) if (ieq(c->name, n)) return c;
    return nullptr;
}

win::Window* desktop() { return g_desktop; }

static Window* attach(win::Window* w)
{
    Window* d = (Window*)w->w32;
    if (!d) {
        d = g_pending; g_pending = nullptr;
        if (!d) { d = new Window(); d->proc = nullptr; }
        d->hwnd = w;
        w->w32 = d;
    }
    return d;
}

static LRESULT call_proc(Window* d, HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (d->proc) return d->proc(h, msg, wp, lp);
    return DefWindowProcW(h, msg, wp, lp);
}

// Every shim window uses this native class; it forwards to the win32 WNDPROC,
// translating the few messages whose lparam the win layer defines differently.
static win::Result thunk(win::Window* w, uint32_t msg, win::WParam wp, win::LParam lp)
{
    Window* d = attach(w);
    HWND h = hwnd_of(w);
    switch (msg) {
    case WM_NCCALCSIZE: {
        gdi::Rect* r = (gdi::Rect*)lp;
        RECT rc = RECT_of(*r);
        LRESULT res = call_proc(d, h, msg, 0, (LPARAM)&rc);
        *r = rect_of(rc);
        return res;
    }
    case WM_WINDOWPOSCHANGED:
    case WM_WINDOWPOSCHANGING: {
        win::WindowPos* p = (win::WindowPos*)lp;
        WINDOWPOS wp32;
        wp32.hwnd = hwnd_of(p->w);
        wp32.hwndInsertAfter = g_insert_after_hwnd(p->insert_after);
        wp32.x = p->rect.left; wp32.y = p->rect.top;
        wp32.cx = p->rect.width(); wp32.cy = p->rect.height();
        wp32.flags = p->flags;
        return call_proc(d, h, msg, wp, (LPARAM)&wp32);
    }
    case WM_NCDESTROY: {
        LRESULT res = call_proc(d, h, msg, wp, lp);
        w->w32 = nullptr;
        delete d;
        return res;
    }
    default:
        return call_proc(d, h, msg, wp, lp);
    }
}

static win::Result desktop_proc(win::Window* w, uint32_t msg, win::WParam wp, win::LParam lp)
{
    return win::def_proc(w, msg, wp, lp);
}

void init()
{
    static bool done;
    if (done) return;
    done = true;
    if (!win::init()) { fprintf(stderr, "flxp: cannot open the display\n"); exit(1); }
    extern void init_gdi() __attribute__((weak));
    extern void init_kernel() __attribute__((weak));
    if (init_gdi) init_gdi();
    if (init_kernel) init_kernel();
    init_sysparams();
    win::Class c{"w32", thunk};
    win::register_class(c);
    win::Class dc{"w32desktop", desktop_proc};
    win::register_class(dc);
    gdi::Size ss = sys::screen_size();
    g_desktop = win::create("w32desktop", "", WS_POPUP, 0, gdi::rect_xywh(0, 0, ss.cx, ss.cy));
    register_builtin_classes();
    init_theme();
}

} // namespace w32

extern "C" {

// ---- classes --------------------------------------------------------------

ATOM WINAPI RegisterClassExW(const WNDCLASSEXW* wc)
{
    if (!wc || !wc->lpszClassName) return 0;
    Class* c = find_class(wc->lpszClassName);
    if (c) { SetLastError(ERROR_CLASS_ALREADY_EXISTS); return 0; }
    c = new Class();
    if (!((ULONG_PTR)wc->lpszClassName >> 16)) {
        c->atom = LOWORD((ULONG_PTR)wc->lpszClassName);
        c->name = u"#" + std::u16string(utf16_of(std::to_string(c->atom).c_str()));
    } else {
        c->name = (const char16_t*)wc->lpszClassName;
        c->atom = g_next_atom++;
    }
    c->proc = wc->lpfnWndProc;
    c->style = wc->style;
    c->cls_extra = wc->cbClsExtra;
    c->wnd_extra = wc->cbWndExtra;
    c->inst = wc->hInstance;
    c->icon = wc->hIcon;
    c->icon_sm = wc->hIconSm;
    c->cursor = wc->hCursor;
    c->background = wc->hbrBackground;
    if (wc->lpszMenuName && ((ULONG_PTR)wc->lpszMenuName >> 16)) c->menu_name = (const char16_t*)wc->lpszMenuName;
    c->cls_bytes.assign((size_t)(c->cls_extra > 0 ? c->cls_extra : 0) + 8, 0);
    g_classes.push_back(c);
    return c->atom;
}

ATOM WINAPI RegisterClassW(const WNDCLASSW* wc)
{
    WNDCLASSEXW ex;
    ex.cbSize = sizeof ex;
    ex.style = wc->style; ex.lpfnWndProc = wc->lpfnWndProc; ex.cbClsExtra = wc->cbClsExtra;
    ex.cbWndExtra = wc->cbWndExtra; ex.hInstance = wc->hInstance; ex.hIcon = wc->hIcon;
    ex.hCursor = wc->hCursor; ex.hbrBackground = wc->hbrBackground; ex.lpszMenuName = wc->lpszMenuName;
    ex.lpszClassName = wc->lpszClassName; ex.hIconSm = nullptr;
    return RegisterClassExW(&ex);
}

ATOM WINAPI RegisterClassA(const WNDCLASSA* wc)
{
    std::u16string name = ((ULONG_PTR)wc->lpszClassName >> 16) ? utf16_of(wc->lpszClassName) : std::u16string();
    std::u16string menu = (wc->lpszMenuName && ((ULONG_PTR)wc->lpszMenuName >> 16)) ? utf16_of(wc->lpszMenuName) : std::u16string();
    WNDCLASSW w;
    w.style = wc->style; w.lpfnWndProc = wc->lpfnWndProc; w.cbClsExtra = wc->cbClsExtra;
    w.cbWndExtra = wc->cbWndExtra; w.hInstance = wc->hInstance; w.hIcon = wc->hIcon;
    w.hCursor = wc->hCursor; w.hbrBackground = wc->hbrBackground;
    w.lpszMenuName = menu.empty() ? (LPCWSTR)wc->lpszMenuName : (LPCWSTR)menu.c_str();
    w.lpszClassName = name.empty() ? (LPCWSTR)wc->lpszClassName : (LPCWSTR)name.c_str();
    return RegisterClassW(&w);
}

ATOM WINAPI RegisterClassExA(const WNDCLASSEXA* wc)
{
    WNDCLASSA w;
    w.style = wc->style; w.lpfnWndProc = wc->lpfnWndProc; w.cbClsExtra = wc->cbClsExtra;
    w.cbWndExtra = wc->cbWndExtra; w.hInstance = wc->hInstance; w.hIcon = wc->hIcon;
    w.hCursor = wc->hCursor; w.hbrBackground = wc->hbrBackground; w.lpszMenuName = wc->lpszMenuName;
    w.lpszClassName = wc->lpszClassName;
    return RegisterClassA(&w);
}

BOOL WINAPI UnregisterClassW(LPCWSTR name, HINSTANCE)
{
    Class* c = find_class(name);
    if (!c) return FALSE;
    for (size_t i = 0; i < g_classes.size(); i++) if (g_classes[i] == c) { g_classes.erase(g_classes.begin() + i); break; }
    delete c;
    return TRUE;
}

BOOL WINAPI UnregisterClassA(LPCSTR name, HINSTANCE inst)
{
    if (!((ULONG_PTR)name >> 16)) return UnregisterClassW((LPCWSTR)name, inst);
    return UnregisterClassW((LPCWSTR)utf16_of(name).c_str(), inst);
}

static void fill_classinfo(Class* c, WNDCLASSEXW* wc)
{
    wc->style = c->style; wc->lpfnWndProc = c->proc; wc->cbClsExtra = c->cls_extra;
    wc->cbWndExtra = c->wnd_extra; wc->hInstance = c->inst; wc->hIcon = c->icon; wc->hIconSm = c->icon_sm;
    wc->hCursor = c->cursor; wc->hbrBackground = c->background;
    wc->lpszMenuName = c->menu_name.empty() ? nullptr : (LPCWSTR)c->menu_name.c_str();
    wc->lpszClassName = (LPCWSTR)c->name.c_str();
}

BOOL WINAPI GetClassInfoExW(HINSTANCE, LPCWSTR name, WNDCLASSEXW* wc)
{
    Class* c = find_class(name);
    if (!c) return FALSE;
    fill_classinfo(c, wc);
    return TRUE;
}

BOOL WINAPI GetClassInfoW(HINSTANCE inst, LPCWSTR name, WNDCLASSW* wc)
{
    WNDCLASSEXW ex;
    if (!GetClassInfoExW(inst, name, &ex)) return FALSE;
    wc->style = ex.style; wc->lpfnWndProc = ex.lpfnWndProc; wc->cbClsExtra = ex.cbClsExtra;
    wc->cbWndExtra = ex.cbWndExtra; wc->hInstance = ex.hInstance; wc->hIcon = ex.hIcon;
    wc->hCursor = ex.hCursor; wc->hbrBackground = ex.hbrBackground; wc->lpszMenuName = ex.lpszMenuName;
    wc->lpszClassName = ex.lpszClassName;
    return TRUE;
}

INT WINAPI GetClassNameW(HWND h, LPWSTR buf, INT n)
{
    Window* d = wdata(h);
    if (!d || !buf || n <= 0) return 0;
    const std::u16string& name = d->cls->name;
    int len = (int)name.size() < n - 1 ? (int)name.size() : n - 1;
    memcpy(buf, name.data(), (size_t)len * 2);
    buf[len] = 0;
    return len;
}

INT WINAPI GetClassNameA(HWND h, LPSTR buf, INT n)
{
    Window* d = wdata(h);
    if (!d || !buf || n <= 0) return 0;
    std::string name = utf8_of((const WCHAR*)d->cls->name.c_str());
    int len = (int)name.size() < n - 1 ? (int)name.size() : n - 1;
    memcpy(buf, name.data(), (size_t)len);
    buf[len] = 0;
    return len;
}

ULONG_PTR WINAPI GetClassLongPtrW(HWND h, INT idx)
{
    Window* d = wdata(h);
    if (!d) return 0;
    Class* c = d->cls;
    switch (idx) {
    case GCL_STYLE: return c->style;
    case GCL_CBWNDEXTRA: return (ULONG_PTR)c->wnd_extra;
    case GCL_CBCLSEXTRA: return (ULONG_PTR)c->cls_extra;
    case GCLP_HBRBACKGROUND: return (ULONG_PTR)c->background;
    case GCLP_HCURSOR: return (ULONG_PTR)c->cursor;
    case GCLP_HICON: return (ULONG_PTR)c->icon;
    case GCLP_HICONSM: return (ULONG_PTR)c->icon_sm;
    case GCLP_WNDPROC: return (ULONG_PTR)c->proc;
    case GCLP_HMODULE: return (ULONG_PTR)c->inst;
    case GCLP_MENUNAME: return (ULONG_PTR)c->menu_name.c_str();
    case GCW_ATOM: return c->atom;
    default:
        if (idx >= 0 && idx + (int)sizeof(ULONG_PTR) <= (int)c->cls_bytes.size()) { ULONG_PTR v; memcpy(&v, &c->cls_bytes[(size_t)idx], sizeof v); return v; }
        return 0;
    }
}

DWORD WINAPI GetClassLongW(HWND h, INT idx) { return (DWORD)GetClassLongPtrW(h, idx); }
DWORD WINAPI GetClassLongA(HWND h, INT idx) { return (DWORD)GetClassLongPtrW(h, idx); }
ULONG_PTR WINAPI GetClassLongPtrA(HWND h, INT idx) { return GetClassLongPtrW(h, idx); }

ULONG_PTR WINAPI SetClassLongPtrW(HWND h, INT idx, LONG_PTR v)
{
    Window* d = wdata(h);
    if (!d) return 0;
    Class* c = d->cls;
    ULONG_PTR old = GetClassLongPtrW(h, idx);
    switch (idx) {
    case GCL_STYLE: c->style = (UINT)v; break;
    case GCLP_HBRBACKGROUND: c->background = (HBRUSH)v; break;
    case GCLP_HCURSOR: c->cursor = (HCURSOR)v; break;
    case GCLP_HICON: c->icon = (HICON)v; break;
    case GCLP_HICONSM: c->icon_sm = (HICON)v; break;
    case GCLP_WNDPROC: c->proc = (WNDPROC)v; break;
    case GCL_CBWNDEXTRA: c->wnd_extra = (int)v; break;
    default:
        if (idx >= 0 && idx + (int)sizeof(ULONG_PTR) <= (int)c->cls_bytes.size()) memcpy(&c->cls_bytes[(size_t)idx], &v, sizeof v);
    }
    return old;
}
DWORD WINAPI SetClassLongW(HWND h, INT idx, LONG v) { return (DWORD)SetClassLongPtrW(h, idx, v); }

// ---- window creation ------------------------------------------------------

HWND WINAPI CreateWindowExW(DWORD ex, LPCWSTR clsname, LPCWSTR name, DWORD style, INT x, INT y, INT cx, INT cy,
                            HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
{
    w32::init();
    Class* c = find_class(clsname);
    if (!c) { SetLastError(ERROR_CANNOT_FIND_WND_CLASS); return nullptr; }
    if (x == CW_USEDEFAULT) x = 100;
    if (y == CW_USEDEFAULT) y = 100;
    if (cx == CW_USEDEFAULT) cx = 640;
    if (cy == CW_USEDEFAULT) cy = 480;
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (parent == hwnd_of(g_desktop)) parent = nullptr;
    if ((style & WS_CHILD) && !parent) { SetLastError(ERROR_TLW_WITH_WSCHILD); return nullptr; }

    Window* d = new Window();
    d->cls = c;
    d->proc = c->proc;
    d->inst = inst;
    d->menu = (style & WS_CHILD) ? nullptr : menu;
    d->help_id = 0;
    d->extra.assign((size_t)(c->wnd_extra > 0 ? c->wnd_extra : 0) + 8, 0);
    memset(&d->wnd, 0, sizeof d->wnd);
    d->paint_dc = nullptr;
    d->in_paint = false;
    // XP: the mnemonics hidden until the keyboard is used (a child starts as its parent is; a top level as the last input)
    if (win::keyboard_cues()) d->uistate = 0;
    else if (Window* pd = (style & WS_CHILD) ? wdata(parent) : nullptr) d->uistate = pd->uistate;
    else d->uistate = win::last_input_keyboard() ? 0 : UISF_HIDEACCEL;

    CREATESTRUCTW cs;
    cs.lpCreateParams = param; cs.hInstance = inst; cs.hMenu = menu; cs.hwndParent = parent;
    cs.cy = cy; cs.cx = cx; cs.y = y; cs.x = x; cs.style = style; cs.lpszName = name;
    cs.lpszClass = clsname; cs.dwExStyle = ex;

    std::string text8 = (name && ((ULONG_PTR)name >> 16)) ? utf8_of(name) : std::string();
    win::Window* parentw = (style & WS_CHILD) ? window_of(parent) : nullptr;
    win::Window* owner = (style & WS_CHILD) ? nullptr : window_of(parent);
    if (owner) owner = win::toplevel_of(owner);
    intptr_t id = (intptr_t)menu;   // for toplevels this is the menu handle, as GWLP_ID is in win32
    g_pending = d;
    win::Window* w = win::create("w32", text8.c_str(), style & ~WS_VISIBLE, ex, gdi::rect_xywh(x, y, cx, cy),
                                 parentw, owner, id, &cs);
    g_pending = nullptr;
    if (!w) return nullptr;
    w->class_style = c->style;
    if (w->top && (c->style & CS_DROPSHADOW)) {   // XP's popup menus (and its v6 tooltips, which Wine's comctl32 does not ask for)
        BOOL on = FALSE;
        SystemParametersInfoW(SPI_GETDROPSHADOW, 0, &on, 0);
        win::set_drop_shadow(w, UT_MENU_SHADOW, on ? ut_menu_shadow_alpha : nullptr);
    }
    HWND h = hwnd_of(w);
    if (!(style & WS_CHILD) && menu) MENU_SetMenu(h, menu);   // attaches the bar to the window and recomputes the frame
    SendMessageW(h, WM_SIZE, SIZE_RESTORED, MAKELONG(w->client.width(), w->client.height()));
    SendMessageW(h, WM_MOVE, 0, MAKELONG(w->rect.left + w->client.left, w->rect.top + w->client.top));
    if (!win::is_window(w)) return nullptr;
    if (style & WS_VISIBLE) ShowWindow(h, SW_SHOW);
    return win::is_window(w) ? h : nullptr;
}

HWND WINAPI CreateWindowExA(DWORD ex, LPCSTR clsname, LPCSTR name, DWORD style, INT x, INT y, INT cx, INT cy,
                            HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
{
    std::u16string c16, n16;
    LPCWSTR cw = (LPCWSTR)clsname, nw = name ? (LPCWSTR)name : nullptr;
    if (clsname && ((ULONG_PTR)clsname >> 16)) { c16 = utf16_of(clsname); cw = (LPCWSTR)c16.c_str(); }
    if (name && ((ULONG_PTR)name >> 16)) { n16 = utf16_of(name); nw = (LPCWSTR)n16.c_str(); }
    return CreateWindowExW(ex, cw, nw, style, x, y, cx, cy, parent, menu, inst, param);
}

BOOL WINAPI DestroyWindow(HWND h)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return FALSE;
    win::destroy(w);
    return TRUE;
}

BOOL WINAPI IsWindow(HWND h) { return win::is_window(window_of(h)); }
BOOL WINAPI IsWindowUnicode(HWND) { return TRUE; }
BOOL WINAPI IsWindowVisible(HWND h) { win::Window* w = window_of(h); return win::is_window(w) && win::is_visible(w); }
// win32: the window's own WS_DISABLED only (a control under a disabled owner, as
// while a modal dialog is up, is still enabled and drawn so; input is routed by
// win::is_enabled, which follows the parents)
BOOL WINAPI IsWindowEnabled(HWND h) { win::Window* w = window_of(h); return win::is_window(w) && !(w->style & WS_DISABLED); }
BOOL WINAPI IsIconic(HWND) { return FALSE; }
// a WM-framed toplevel maximised by the window manager (_NET_WM_STATE)
BOOL WINAPI IsZoomed(HWND h)
{
    win::Window* w = window_of(h);
    return win::is_window(w) && w->top && w->wm_decorated && win::is_maximized(w);
}
BOOL WINAPI IsChild(HWND parent, HWND child)
{
    win::Window* p = window_of(parent); win::Window* c = window_of(child);
    if (!win::is_window(p) || !win::is_window(c)) return FALSE;
    for (win::Window* w = c->parent; w; w = w->parent) if (w == p) return TRUE;
    return FALSE;
}

BOOL WINAPI EnableWindow(HWND h, BOOL enable)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return FALSE;
    BOOL was_disabled = (w->style & WS_DISABLED) != 0;
    win::enable(w, enable != 0);
    return was_disabled;
}

BOOL WINAPI ShowWindow(HWND h, INT cmd)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return FALSE;
    BOOL was = (w->style & WS_VISIBLE) != 0;
    win::show_window(w, cmd == SW_HIDE ? SW_HIDE : SW_SHOW);
    return was;
}
BOOL WINAPI ShowWindowAsync(HWND h, INT cmd) { return ShowWindow(h, cmd); }

static win::Window* insert_after_of(HWND after)
{
    if (after == (HWND)0 || after == (HWND)(LONG_PTR)-1 || after == (HWND)(LONG_PTR)-2) return WIN_HWND_TOP;
    if (after == (HWND)1) return WIN_HWND_BOTTOM;
    win::Window* w = window_of(after);
    return win::is_window(w) ? w : WIN_HWND_TOP;
}

BOOL WINAPI SetWindowPos(HWND h, HWND after, INT x, INT y, INT cx, INT cy, UINT flags)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return FALSE;
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    return win::set_window_pos(w, insert_after_of(after), gdi::rect_xywh(x, y, cx, cy), flags);
}

BOOL WINAPI MoveWindow(HWND h, INT x, INT y, INT cx, INT cy, BOOL repaint)
{
    return SetWindowPos(h, nullptr, x, y, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE | (repaint ? 0 : SWP_NOREDRAW));
}

HDWP WINAPI BeginDeferWindowPos(INT) { return (HDWP)1; }
HDWP WINAPI DeferWindowPos(HDWP hdwp, HWND h, HWND after, INT x, INT y, INT cx, INT cy, UINT flags)
{
    SetWindowPos(h, after, x, y, cx, cy, flags);
    return hdwp;
}
BOOL WINAPI EndDeferWindowPos(HDWP) { return TRUE; }

BOOL WINAPI GetWindowRect(HWND h, RECT* r)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w) || !r) return FALSE;
    *r = RECT_of(win::window_rect(w));
    return TRUE;
}

BOOL WINAPI GetClientRect(HWND h, RECT* r)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w) || !r) return FALSE;
    *r = RECT_of(win::client_rect(w));
    return TRUE;
}

BOOL WINAPI ClientToScreen(HWND h, POINT* p)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w) || !p) return FALSE;
    gdi::Point s = win::client_to_screen(w, point_of(*p));
    p->x = s.x; p->y = s.y;
    return TRUE;
}

BOOL WINAPI ScreenToClient(HWND h, POINT* p)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w) || !p) return FALSE;
    gdi::Point s = win::screen_to_client(w, point_of(*p));
    p->x = s.x; p->y = s.y;
    return TRUE;
}

INT WINAPI MapWindowPoints(HWND from, HWND to, POINT* pts, UINT n)
{
    win::Window* f = window_of(from); win::Window* t = window_of(to);
    if (f == g_desktop) f = nullptr;
    if (t == g_desktop) t = nullptr;
    if (f && !win::is_window(f)) return 0;
    if (t && !win::is_window(t)) return 0;
    gdi::Point off = win::map_point(f, t, gdi::Point{0, 0});
    for (UINT i = 0; i < n; i++) { pts[i].x += off.x; pts[i].y += off.y; }
    return MAKELONG(off.x, off.y);
}

HWND WINAPI GetParent(HWND h)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return nullptr;
    if (w->parent) return hwnd_of(w->parent);
    if (w->owner) return hwnd_of(w->owner);
    return nullptr;
}

HWND WINAPI SetParent(HWND h, HWND parent)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return nullptr;
    HWND old = w->parent ? hwnd_of(w->parent) : nullptr;
    win::Window* p = parent ? window_of(parent) : nullptr;
    if (p == g_desktop) p = nullptr;
    if (p && !win::is_window(p)) return old;
    if (!old && !p) return hwnd_of(g_desktop);
    win::set_parent(w, p);
    return old ? old : hwnd_of(g_desktop);
}

HWND WINAPI GetAncestor(HWND h, UINT flags)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return nullptr;
    switch (flags) {
    case GA_PARENT: return w->parent ? hwnd_of(w->parent) : hwnd_of(g_desktop);
    case GA_ROOT: return hwnd_of(win::toplevel_of(w));
    case GA_ROOTOWNER: { w = win::toplevel_of(w); while (w->owner) w = w->owner; return hwnd_of(w); }
    }
    return nullptr;
}

HWND WINAPI GetWindow(HWND h, UINT cmd)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return nullptr;
    std::vector<win::Window*> tops;
    switch (cmd) {
    case GW_HWNDFIRST:
        if (w->parent) return hwnd_of(w->parent->child);
        win::toplevels(tops); return tops.empty() ? nullptr : hwnd_of(tops[0]);
    case GW_HWNDLAST: {
        if (w->parent) { win::Window* c = w->parent->child; while (c && c->next) c = c->next; return hwnd_of(c); }
        win::toplevels(tops); return tops.empty() ? nullptr : hwnd_of(tops.back());
    }
    case GW_HWNDNEXT:
        if (w->parent) return hwnd_of(w->next);
        win::toplevels(tops);
        for (size_t i = 0; i + 1 < tops.size(); i++) if (tops[i] == w) return hwnd_of(tops[i + 1]);
        return nullptr;
    case GW_HWNDPREV:
        if (w->parent) return hwnd_of(w->prev);
        win::toplevels(tops);
        for (size_t i = 1; i < tops.size(); i++) if (tops[i] == w) return hwnd_of(tops[i - 1]);
        return nullptr;
    case GW_OWNER: return w->owner ? hwnd_of(w->owner) : nullptr;
    case GW_CHILD: return hwnd_of(w->child);
    }
    return nullptr;
}

HWND WINAPI GetTopWindow(HWND h)
{
    win::Window* w = h ? window_of(h) : g_desktop;
    if (!win::is_window(w)) return nullptr;
    if (w == g_desktop) { std::vector<win::Window*> tops; win::toplevels(tops); return tops.empty() ? nullptr : hwnd_of(tops[0]); }
    return hwnd_of(w->child);
}

HWND WINAPI GetDesktopWindow(void) { w32::init(); return hwnd_of(g_desktop); }

static void collect_children(win::Window* w, std::vector<win::Window*>& out)
{
    for (win::Window* c = w->child; c; c = c->next) { out.push_back(c); collect_children(c, out); }
}

BOOL WINAPI EnumChildWindows(HWND h, WNDENUMPROC proc, LPARAM lp)
{
    win::Window* w = h ? window_of(h) : g_desktop;
    if (!win::is_window(w)) return FALSE;
    std::vector<win::Window*> kids;
    if (w == g_desktop) win::toplevels(kids); else collect_children(w, kids);
    for (win::Window* c : kids) {
        if (!win::is_window(c)) continue;
        if (!proc(hwnd_of(c), lp)) return FALSE;
    }
    return TRUE;
}

BOOL WINAPI EnumWindows(WNDENUMPROC proc, LPARAM lp)
{
    std::vector<win::Window*> tops;
    win::toplevels(tops);
    for (win::Window* t : tops) {
        if (!win::is_window(t) || t == g_desktop) continue;
        if (!proc(hwnd_of(t), lp)) return FALSE;
    }
    return TRUE;
}

BOOL WINAPI EnumThreadWindows(DWORD, WNDENUMPROC proc, LPARAM lp) { return EnumWindows(proc, lp); }

HWND WINAPI WindowFromPoint(POINT p)
{
    win::Window* w = win::window_from_point(point_of(p));
    return w ? hwnd_of(w) : nullptr;
}

HWND WINAPI ChildWindowFromPointEx(HWND h, POINT p, UINT flags)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return nullptr;
    gdi::Point pt = point_of(p);
    if (!win::client_rect(w).contains(pt)) return nullptr;
    for (win::Window* c = w->child; c; c = c->next) {
        if (!gdi::offset(c->rect, -w->client.left, -w->client.top).contains(pt)) continue;
        if ((flags & CWP_SKIPINVISIBLE) && !(c->style & WS_VISIBLE)) continue;
        if ((flags & CWP_SKIPDISABLED) && (c->style & WS_DISABLED)) continue;
        if ((flags & CWP_SKIPTRANSPARENT) && (c->ex_style & WS_EX_TRANSPARENT)) continue;
        return hwnd_of(c);
    }
    return h;
}

HWND WINAPI ChildWindowFromPoint(HWND h, POINT p) { return ChildWindowFromPointEx(h, p, CWP_ALL); }
HWND WINAPI RealChildWindowFromPoint(HWND h, POINT p) { return ChildWindowFromPointEx(h, p, CWP_SKIPTRANSPARENT | CWP_SKIPINVISIBLE); }

DWORD WINAPI GetWindowThreadProcessId(HWND, DWORD* pid) { if (pid) *pid = 1; return 1; }

// ---- window longs, text, props -------------------------------------------

LONG_PTR WINAPI GetWindowLongPtrW(HWND h, INT idx)
{
    win::Window* w = window_of(h);
    Window* d = wdata(h);
    if (!win::is_window(w) || !d) { SetLastError(ERROR_INVALID_WINDOW_HANDLE); return 0; }
    switch (idx) {
    case GWL_STYLE: return (LONG)w->style;
    case GWL_EXSTYLE: return (LONG)w->ex_style;
    case GWLP_WNDPROC: return (LONG_PTR)d->proc;
    case GWLP_HINSTANCE: return (LONG_PTR)d->inst;
    case GWLP_HWNDPARENT: return (LONG_PTR)GetParent(h);
    case GWLP_ID: return (LONG_PTR)w->id;
    case GWLP_USERDATA: return (LONG_PTR)w->userdata;
    default:
        if (idx >= 0 && (size_t)idx + sizeof(LONG_PTR) <= d->extra.size()) { LONG_PTR v; memcpy(&v, &d->extra[(size_t)idx], sizeof v); return v; }
        SetLastError(ERROR_INVALID_INDEX);
        return 0;
    }
}

LONG WINAPI GetWindowLongW(HWND h, INT idx)
{
    Window* d = wdata(h);
    if (idx >= 0 && d && (size_t)idx + sizeof(LONG) <= d->extra.size()) { LONG v; memcpy(&v, &d->extra[(size_t)idx], sizeof v); return v; }
    return (LONG)GetWindowLongPtrW(h, idx);
}

LONG_PTR WINAPI GetWindowLongPtrA(HWND h, INT idx) { return GetWindowLongPtrW(h, idx); }
LONG WINAPI GetWindowLongA(HWND h, INT idx) { return GetWindowLongW(h, idx); }

LONG_PTR WINAPI SetWindowLongPtrW(HWND h, INT idx, LONG_PTR v)
{
    win::Window* w = window_of(h);
    Window* d = wdata(h);
    if (!win::is_window(w) || !d) { SetLastError(ERROR_INVALID_WINDOW_HANDLE); return 0; }
    LONG_PTR old;
    switch (idx) {
    case GWL_STYLE: {
        old = (LONG)w->style;
        STYLESTRUCT ss = { (DWORD)old, (DWORD)v };
        SendMessageW(h, WM_STYLECHANGING, GWL_STYLE, (LPARAM)&ss);
        if (!win::is_window(w)) return 0;
        w->style = (ss.styleNew & ~WS_CHILD) | (w->style & WS_CHILD);
        win::style_changed(w);
        SendMessageW(h, WM_STYLECHANGED, GWL_STYLE, (LPARAM)&ss);
        return old;
    }
    case GWL_EXSTYLE: {
        old = (LONG)w->ex_style;
        STYLESTRUCT ss = { (DWORD)old, (DWORD)v };
        SendMessageW(h, WM_STYLECHANGING, GWL_EXSTYLE, (LPARAM)&ss);
        if (!win::is_window(w)) return 0;
        w->ex_style = ss.styleNew;
        SendMessageW(h, WM_STYLECHANGED, GWL_EXSTYLE, (LPARAM)&ss);
        return old;
    }
    case GWLP_WNDPROC: old = (LONG_PTR)d->proc; d->proc = (WNDPROC)v; return old;
    case GWLP_HINSTANCE: old = (LONG_PTR)d->inst; d->inst = (HINSTANCE)v; return old;
    case GWLP_HWNDPARENT: return (LONG_PTR)SetParent(h, (HWND)v);
    case GWLP_ID: old = (LONG_PTR)w->id; w->id = (intptr_t)v; return old;
    case GWLP_USERDATA: old = (LONG_PTR)w->userdata; w->userdata = (void*)v; return old;
    default:
        if (idx >= 0 && (size_t)idx + sizeof(LONG_PTR) <= d->extra.size()) {
            memcpy(&old, &d->extra[(size_t)idx], sizeof old);
            memcpy(&d->extra[(size_t)idx], &v, sizeof v);
            return old;
        }
        SetLastError(ERROR_INVALID_INDEX);
        return 0;
    }
}

LONG WINAPI SetWindowLongW(HWND h, INT idx, LONG v)
{
    Window* d = wdata(h);
    if (idx >= 0 && d && (size_t)idx + sizeof(LONG) <= d->extra.size()) {
        LONG old; memcpy(&old, &d->extra[(size_t)idx], sizeof old); memcpy(&d->extra[(size_t)idx], &v, sizeof v); return old;
    }
    return (LONG)SetWindowLongPtrW(h, idx, v);
}
LONG_PTR WINAPI SetWindowLongPtrA(HWND h, INT idx, LONG_PTR v) { return SetWindowLongPtrW(h, idx, v); }
LONG WINAPI SetWindowLongA(HWND h, INT idx, LONG v) { return SetWindowLongW(h, idx, v); }

INT WINAPI GetWindowTextW(HWND h, LPWSTR buf, INT n)
{
    if (!buf || n <= 0) return 0;
    buf[0] = 0;
    if (!IsWindow(h)) return 0;
    return (INT)SendMessageW(h, WM_GETTEXT, (WPARAM)n, (LPARAM)buf);
}

INT WINAPI GetWindowTextA(HWND h, LPSTR buf, INT n)
{
    if (!buf || n <= 0) return 0;
    buf[0] = 0;
    if (!IsWindow(h)) return 0;
    std::vector<WCHAR> tmp((size_t)n * 2 + 2);
    int len = (int)SendMessageW(h, WM_GETTEXT, tmp.size(), (LPARAM)tmp.data());
    std::string s = utf8_of(tmp.data(), len);
    int l = (int)s.size() < n - 1 ? (int)s.size() : n - 1;
    memcpy(buf, s.data(), (size_t)l); buf[l] = 0;
    return l;
}

INT WINAPI GetWindowTextLengthW(HWND h) { return IsWindow(h) ? (INT)SendMessageW(h, WM_GETTEXTLENGTH, 0, 0) : 0; }
INT WINAPI GetWindowTextLengthA(HWND h) { return GetWindowTextLengthW(h); }

INT WINAPI InternalGetWindowText(HWND h, LPWSTR buf, INT n)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w) || !buf || n <= 0) return 0;
    std::u16string s = utf16_of(w->text.c_str());
    int len = (int)s.size() < n - 1 ? (int)s.size() : n - 1;
    memcpy(buf, s.data(), (size_t)len * 2); buf[len] = 0;
    return len;
}

BOOL WINAPI SetWindowTextW(HWND h, LPCWSTR s) { return IsWindow(h) ? (BOOL)SendMessageW(h, WM_SETTEXT, 0, (LPARAM)s) : FALSE; }
BOOL WINAPI SetWindowTextA(HWND h, LPCSTR s) { std::u16string t = s ? utf16_of(s) : std::u16string(); return SetWindowTextW(h, (LPCWSTR)t.c_str()); }

static Prop* find_prop(Window* d, LPCWSTR name)
{
    bool is_atom = !((ULONG_PTR)name >> 16);
    for (Prop& p : d->props) {
        if (is_atom ? (p.atom == LOWORD((ULONG_PTR)name) && p.name.empty()) : ieq(p.name, (const char16_t*)name)) return &p;
    }
    return nullptr;
}

HANDLE WINAPI GetPropW(HWND h, LPCWSTR name)
{
    Window* d = wdata(h);
    if (!d || !IsWindow(h)) return nullptr;
    Prop* p = find_prop(d, name);
    return p ? p->value : nullptr;
}

BOOL WINAPI SetPropW(HWND h, LPCWSTR name, HANDLE v)
{
    Window* d = wdata(h);
    if (!d || !IsWindow(h)) return FALSE;
    Prop* p = find_prop(d, name);
    if (p) { p->value = v; return TRUE; }
    Prop np;
    if ((ULONG_PTR)name >> 16) { np.name = (const char16_t*)name; np.atom = 0; }
    else { np.atom = LOWORD((ULONG_PTR)name); }
    np.value = v;
    d->props.push_back(np);
    return TRUE;
}

HANDLE WINAPI RemovePropW(HWND h, LPCWSTR name)
{
    Window* d = wdata(h);
    if (!d || !IsWindow(h)) return nullptr;
    for (size_t i = 0; i < d->props.size(); i++) {
        Prop& p = d->props[i];
        bool is_atom = !((ULONG_PTR)name >> 16);
        if (is_atom ? (p.atom == LOWORD((ULONG_PTR)name) && p.name.empty()) : ieq(p.name, (const char16_t*)name)) {
            HANDLE v = p.value;
            d->props.erase(d->props.begin() + i);
            return v;
        }
    }
    return nullptr;
}

HANDLE WINAPI GetPropA(HWND h, LPCSTR name) { if (!((ULONG_PTR)name >> 16)) return GetPropW(h, (LPCWSTR)name); return GetPropW(h, (LPCWSTR)utf16_of(name).c_str()); }
BOOL WINAPI SetPropA(HWND h, LPCSTR name, HANDLE v) { if (!((ULONG_PTR)name >> 16)) return SetPropW(h, (LPCWSTR)name, v); return SetPropW(h, (LPCWSTR)utf16_of(name).c_str(), v); }
HANDLE WINAPI RemovePropA(HWND h, LPCSTR name) { if (!((ULONG_PTR)name >> 16)) return RemovePropW(h, (LPCWSTR)name); return RemovePropW(h, (LPCWSTR)utf16_of(name).c_str()); }

BOOL WINAPI SetWindowContextHelpId(HWND h, DWORD id) { Window* d = wdata(h); if (!d) return FALSE; d->help_id = id; return TRUE; }
DWORD WINAPI GetWindowContextHelpId(HWND h) { Window* d = wdata(h); return d ? d->help_id : 0; }

// ---- painting -------------------------------------------------------------

BOOL WINAPI InvalidateRect(HWND h, const RECT* r, BOOL erase)
{
    win::Window* w = h ? window_of(h) : g_desktop;
    if (!win::is_window(w)) return FALSE;
    if (r) { gdi::Rect gr = rect_of(*r); win::invalidate(w, &gr, erase != 0); }
    else win::invalidate(w, nullptr, erase != 0);
    return TRUE;
}

BOOL WINAPI InvalidateRgn(HWND h, HRGN hrgn, BOOL erase)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return FALSE;
    Region* rg = as<Region>(hrgn, T_REGION);
    if (!rg) return InvalidateRect(h, nullptr, erase);
    win::invalidate_region(w, rg->rgn, erase != 0);
    return TRUE;
}

BOOL WINAPI ValidateRect(HWND h, const RECT* r)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return FALSE;
    if (r) { gdi::Rect gr = rect_of(*r); win::validate(w, &gr); } else win::validate(w, nullptr);
    return TRUE;
}

BOOL WINAPI ValidateRgn(HWND h, HRGN hrgn)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return FALSE;
    Region* rg = as<Region>(hrgn, T_REGION);
    if (!rg) win::validate(w, nullptr); else win::validate_region(w, rg->rgn);
    return TRUE;
}

BOOL WINAPI GetUpdateRect(HWND h, RECT* r, BOOL)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return FALSE;
    gdi::Rect b = w->update.bounds();
    if (r) *r = RECT_of(b);
    return !w->update.empty();
}

INT WINAPI GetUpdateRgn(HWND h, HRGN hrgn, BOOL)
{
    win::Window* w = window_of(h);
    Region* rg = as<Region>(hrgn, T_REGION);
    if (!win::is_window(w) || !rg) return ERROR;
    rg->rgn = w->update;
    return rg->rgn.empty() ? NULLREGION : (rg->rgn.rects().size() == 1 ? SIMPLEREGION : COMPLEXREGION);
}

BOOL WINAPI UpdateWindow(HWND h)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return FALSE;
    win::update(w);
    return TRUE;
}

BOOL WINAPI RedrawWindow(HWND h, const RECT* r, HRGN hrgn, UINT flags)
{
    win::Window* w = h ? window_of(h) : g_desktop;
    if (!win::is_window(w)) return FALSE;
    Region* rg = as<Region>(hrgn, T_REGION);
    if (rg && (flags & RDW_INVALIDATE)) { win::invalidate_region(w, rg->rgn, (flags & RDW_ERASE) != 0); flags &= ~RDW_INVALIDATE; }
    gdi::Rect gr;
    if (r) gr = rect_of(*r);
    win::redraw(w, r ? &gr : nullptr, flags);
    return TRUE;
}

HDC WINAPI BeginPaint(HWND h, PAINTSTRUCT* ps)
{
    win::Window* w = window_of(h);
    Window* d = wdata(h);
    if (!win::is_window(w) || !d || !ps) return nullptr;
    win::PaintInfo pi;
    win::begin_paint(w, pi, false);
    DC* dc = create_window_dc(pi.dc, h);
    d->paint_dc = pi.dc;
    d->in_paint = true;
    memset(ps, 0, sizeof *ps);
    ps->hdc = (HDC)dc;
    ps->rcPaint = RECT_of(pi.rect);
    ps->fErase = pi.erase;
    if (pi.erase) {
        if (SendMessageW(h, WM_ERASEBKGND, (WPARAM)dc, 0)) ps->fErase = FALSE;
    }
    return (HDC)dc;
}

BOOL WINAPI EndPaint(HWND h, const PAINTSTRUCT* ps)
{
    win::Window* w = window_of(h);
    Window* d = wdata(h);
    if (!ps || !d) return FALSE;
    DC* dc = dc_of(ps->hdc);
    if (dc) {
        win::PaintInfo pi; pi.dc = dc->dc; pi.erase = false; pi.rect = gdi::empty_rect();
        destroy_window_dc(dc);
        if (win::is_window(w)) win::end_paint(w, pi); else gdi::destroy_dc(pi.dc);
    }
    d->paint_dc = nullptr;
    d->in_paint = false;
    return TRUE;
}

HDC WINAPI GetDCEx(HWND h, HRGN hrgn, DWORD flags)
{
    w32::init();
    win::Window* w = h ? window_of(h) : g_desktop;
    if (!win::is_window(w)) return nullptr;
    gdi::DC* g = (flags & DCX_WINDOW) ? win::get_window_dc(w) : win::get_dc(w);
    Region* rg = as<Region>(hrgn, T_REGION);
    if (rg && (flags & (DCX_INTERSECTRGN | DCX_EXCLUDERGN))) {
        // the region is in screen coordinates; move it into backbuffer coords
        gdi::Point so = win::window_origin(win::toplevel_of(w));
        gdi::Region r = rg->rgn;
        r.offset(-so.x, -so.y);
        gdi::Region clip = g->clip;
        if (flags & DCX_INTERSECTRGN) clip.intersect(r); else clip.subtract(r);
        gdi::set_clip(g, clip);
    }
    return (HDC)create_window_dc(g, h);
}

HDC WINAPI GetDC(HWND h) { return GetDCEx(h, nullptr, DCX_CACHE); }
HDC WINAPI GetWindowDC(HWND h) { return GetDCEx(h, nullptr, DCX_WINDOW | DCX_CACHE); }

INT WINAPI ReleaseDC(HWND h, HDC hdc)
{
    DC* dc = dc_of(hdc);
    if (!dc || dc->type != T_DC) return 0;
    win::Window* w = window_of(dc->hwnd ? dc->hwnd : h);
    gdi::DC* g = dc->dc;
    destroy_window_dc(dc);
    if (win::is_window(w)) win::release_dc(w, g); else gdi::destroy_dc(g);
    return 1;
}

INT WINAPI ScrollWindowEx(HWND h, INT dx, INT dy, const RECT* scroll, const RECT* clip, HRGN hrgn, RECT* rcUpdate, UINT flags)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return ERROR;
    gdi::Rect s, c;
    if (scroll) s = rect_of(*scroll);
    if (clip) c = rect_of(*clip);
    gdi::Region unc;
    win::scroll_window(w, dx, dy, scroll ? &s : nullptr, clip ? &c : nullptr, (flags & SW_SCROLLCHILDREN) != 0, &unc);
    if (flags & SW_INVALIDATE) win::invalidate_region(w, unc, (flags & SW_ERASE) != 0);
    Region* rg = as<Region>(hrgn, T_REGION);
    if (rg) rg->rgn = unc;
    if (rcUpdate) *rcUpdate = RECT_of(unc.bounds());
    return unc.empty() ? NULLREGION : (unc.rects().size() == 1 ? SIMPLEREGION : COMPLEXREGION);
}

BOOL WINAPI ScrollWindow(HWND h, INT dx, INT dy, const RECT* r, const RECT* clip)
{
    return ScrollWindowEx(h, dx, dy, r, clip, nullptr, nullptr, SW_INVALIDATE | SW_ERASE | (r ? 0 : SW_SCROLLCHILDREN)) != ERROR;
}

// A toplevel's region becomes its X shape (rounded panels: the start panel); the system owns the region after
// the call. Child windows: ignored.
BOOL WINAPI SetWindowRgn(HWND h, HRGN rgn, BOOL)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return FALSE;
    if (!w->parent) {
        if (!rgn) win::set_shape(w, nullptr);
        else {
            std::vector<uint8_t> buf(GetRegionData(rgn, 0, nullptr));
            RGNDATA* d = (RGNDATA*)buf.data();
            std::vector<gdi::Rect> rs;
            if (!buf.empty() && GetRegionData(rgn, (DWORD)buf.size(), d))
                for (DWORD i = 0; i < d->rdh.nCount; i++) {
                    const RECT& r = ((RECT*)d->Buffer)[i];
                    rs.push_back(gdi::Rect{ r.left, r.top, r.right, r.bottom });
                }
            win::set_shape(w, &rs);
        }
    }
    if (rgn) DeleteObject(rgn);
    return TRUE;
}
INT WINAPI GetWindowRgn(HWND, HRGN) { return ERROR; }

// ---- messages -------------------------------------------------------------

LRESULT WINAPI SendMessageW(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (h == HWND_BROADCAST) {
        std::vector<win::Window*> tops; win::toplevels(tops);
        for (win::Window* t : tops) if (win::is_window(t) && t != g_desktop) win::send(t, msg, wp, lp);
        return 1;
    }
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return 0;
    return win::send(w, msg, wp, lp);
}
LRESULT WINAPI SendMessageA(HWND h, UINT msg, WPARAM wp, LPARAM lp) { return SendMessageW(h, msg, wp, lp); }
BOOL WINAPI SendNotifyMessageW(HWND h, UINT msg, WPARAM wp, LPARAM lp) { SendMessageW(h, msg, wp, lp); return TRUE; }
LRESULT WINAPI SendMessageTimeoutW(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT, UINT, PDWORD_PTR res)
{
    LRESULT r = SendMessageW(h, msg, wp, lp);
    if (res) *res = (DWORD_PTR)r;
    return 1;
}

BOOL WINAPI PostMessageW(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (!win::is_ui_thread()) {   // worker threads: no window lookups here, the UI thread checks h
        if (h == HWND_BROADCAST) return FALSE;
        win::post(h ? window_of(h) : nullptr, msg, wp, lp);
        return TRUE;
    }
    win::Window* w = h ? window_of(h) : nullptr;
    if (h && !win::is_window(w)) return FALSE;
    if (h == HWND_BROADCAST) { SendMessageW(h, msg, wp, lp); return TRUE; }
    win::post(w, msg, wp, lp);
    return TRUE;
}
BOOL WINAPI PostMessageA(HWND h, UINT msg, WPARAM wp, LPARAM lp) { return PostMessageW(h, msg, wp, lp); }
BOOL WINAPI PostThreadMessageW(DWORD, UINT msg, WPARAM wp, LPARAM lp) { win::post(nullptr, msg, wp, lp); return TRUE; }
void WINAPI PostQuitMessage(INT code) { win::quit(code); }

LRESULT WINAPI CallWindowProcW(WNDPROC proc, HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (!proc) return DefWindowProcW(h, msg, wp, lp);
    return proc(h, msg, wp, lp);
}
LRESULT WINAPI CallWindowProcA(WNDPROC proc, HWND h, UINT msg, WPARAM wp, LPARAM lp) { return CallWindowProcW(proc, h, msg, wp, lp); }

#define SYSTIMER_TAG ((UINT_PTR)1 << 62)

static void fill_msg(MSG* out, const win::Message& m)
{
    out->hwnd = hwnd_of(m.w);
    out->message = m.msg;
    out->wParam = m.wp;
    out->lParam = m.lp;
    out->time = m.time;
    out->pt.x = m.pt.x; out->pt.y = m.pt.y;
    if (m.msg == WM_TIMER && (m.wp & SYSTIMER_TAG)) { out->message = WM_SYSTIMER; out->wParam = m.wp & ~SYSTIMER_TAG; out->lParam = 0; }
    g_msg_time = m.time;
    g_msg_pos.x = m.pt.x; g_msg_pos.y = m.pt.y;
    g_recent.push_back(m);
    if (g_recent.size() > 16) g_recent.pop_front();
}

BOOL WINAPI GetMessageW(MSG* out, HWND h, UINT min, UINT max)
{
    w32::init();
    win::Window* filter = h ? window_of(h) : nullptr;
    win::Message m;
    bool ok = win::get_message(m, filter, min, max);
    fill_msg(out, m);
    if (!ok) { out->message = WM_QUIT; return FALSE; }
    return TRUE;
}
BOOL WINAPI GetMessageA(MSG* out, HWND h, UINT min, UINT max) { return GetMessageW(out, h, min, max); }

BOOL WINAPI PeekMessageW(MSG* out, HWND h, UINT min, UINT max, UINT flags)
{
    w32::init();
    win::Window* filter = h ? window_of(h) : nullptr;
    win::Message m;
    if (!win::peek_message(m, filter, min, max, (flags & PM_REMOVE) != 0)) return FALSE;
    if (out) fill_msg(out, m);
    return TRUE;
}
BOOL WINAPI PeekMessageA(MSG* out, HWND h, UINT min, UINT max, UINT flags) { return PeekMessageW(out, h, min, max, flags); }

BOOL WINAPI TranslateMessage(const MSG* msg)
{
    if (!msg || (msg->message != WM_KEYDOWN && msg->message != WM_SYSKEYDOWN)) return FALSE;
    for (auto it = g_recent.rbegin(); it != g_recent.rend(); ++it) {
        if (hwnd_of(it->w) == msg->hwnd && it->msg == msg->message && it->wp == msg->wParam && it->time == msg->time)
            return win::translate_message(*it) ? TRUE : FALSE;
    }
    return FALSE;
}

LRESULT WINAPI DispatchMessageW(const MSG* msg)
{
    if (!msg) return 0;
    if (msg->message == WM_TIMER && msg->lParam) {
        ((TIMERPROC)msg->lParam)(msg->hwnd, WM_TIMER, (UINT_PTR)msg->wParam, GetTickCount());
        return 0;
    }
    win::Message m;
    memset(&m, 0, sizeof m);
    m.w = window_of(msg->hwnd);
    m.msg = msg->message; m.wp = msg->wParam; m.lp = msg->lParam; m.time = msg->time;
    m.pt = gdi::Point{msg->pt.x, msg->pt.y};
    if (!m.w) return 0;
    return win::dispatch_message(m);
}
LRESULT WINAPI DispatchMessageA(const MSG* msg) { return DispatchMessageW(msg); }

BOOL WINAPI WaitMessage(void) { win::wait_message(); return TRUE; }
DWORD WINAPI MsgWaitForMultipleObjects(DWORD, const HANDLE*, BOOL, DWORD, DWORD) { win::wait_message(); return WAIT_OBJECT_0; }
DWORD WINAPI GetQueueStatus(UINT) { return 0; }
BOOL WINAPI InSendMessage(void) { return FALSE; }
DWORD WINAPI InSendMessageEx(void*) { return ISMEX_NOSEND; }
BOOL WINAPI ReplyMessage(LRESULT) { return FALSE; }
LONG WINAPI GetMessageTime(void) { return (LONG)g_msg_time; }
DWORD WINAPI GetMessagePos(void) { return MAKELONG(g_msg_pos.x, g_msg_pos.y); }
LPARAM WINAPI GetMessageExtraInfo(void) { return 0; }
BOOL WINAPI CallMsgFilterW(MSG* msg, INT code) { extern BOOL flxp_call_msg_filter(MSG*, INT); return flxp_call_msg_filter(msg, code); }
BOOL WINAPI CallMsgFilterA(MSG* msg, INT code) { return CallMsgFilterW(msg, code); }

// ---- timers ---------------------------------------------------------------

static UINT_PTR g_next_timer_id = 0x10000;

UINT_PTR WINAPI SetTimer(HWND h, UINT_PTR id, UINT ms, TIMERPROC proc)
{
    win::Window* w = h ? window_of(h) : nullptr;
    if (h && !win::is_window(w)) return 0;
    if (!h) id = g_next_timer_id++;
    win::set_timer(w, id, ms, (void*)proc);
    return id;
}

BOOL WINAPI KillTimer(HWND h, UINT_PTR id)
{
    win::Window* w = h ? window_of(h) : nullptr;
    win::kill_timer(w, id);
    return TRUE;
}

UINT_PTR WINAPI SetSystemTimer(HWND h, UINT_PTR id, UINT ms, void*)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return 0;
    win::set_timer(w, id | SYSTIMER_TAG, ms, nullptr);
    return id;
}

BOOL WINAPI KillSystemTimer(HWND h, UINT_PTR id)
{
    win::Window* w = window_of(h);
    win::kill_timer(w, id | SYSTIMER_TAG);
    return TRUE;
}

// ---- capture, focus, keyboard, mouse --------------------------------------

HWND WINAPI SetCapture(HWND h)
{
    HWND old = hwnd_of(win::capture());
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return old;
    win::set_capture(w);
    return old;
}
BOOL WINAPI ReleaseCapture(void) { win::release_capture(); return TRUE; }
HWND WINAPI GetCapture(void) { return hwnd_of(win::capture()); }

HWND WINAPI SetFocus(HWND h)
{
    HWND old = hwnd_of(win::focus());
    win::Window* w = h ? window_of(h) : nullptr;
    if (h && (!win::is_window(w) || !win::is_enabled(w))) return old;
    win::set_focus(w);
    return old;
}
HWND WINAPI GetFocus(void) { return hwnd_of(win::focus()); }

HWND WINAPI GetActiveWindow(void)
{
    win::Window* a = win::active_window();
    if (!a && win::focus()) a = win::toplevel_of(win::focus());
    return hwnd_of(a);
}
HWND WINAPI SetActiveWindow(HWND h) { HWND old = GetActiveWindow(); win::Window* w = window_of(h); if (win::is_window(w)) win::set_focus(w); return old; }
HWND WINAPI GetForegroundWindow(void) { return GetActiveWindow(); }
BOOL WINAPI SetForegroundWindow(HWND h) { SetActiveWindow(h); return TRUE; }
BOOL WINAPI AllowSetForegroundWindow(DWORD) { return TRUE; }
BOOL WINAPI BringWindowToTop(HWND h) { return SetWindowPos(h, (HWND)0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); }
HWND WINAPI GetLastActivePopup(HWND h) { return h; }
BOOL WINAPI FlashWindow(HWND, BOOL) { return FALSE; }
BOOL WINAPI FlashWindowEx(PFLASHWINFO) { return FALSE; }

SHORT WINAPI GetKeyState(INT vk) { return win::key_state((uint32_t)vk); }
SHORT WINAPI GetAsyncKeyState(INT vk) { return win::key_state((uint32_t)vk); }
BOOL WINAPI GetKeyboardState(BYTE* state) { for (int i = 0; i < 256; i++) state[i] = win::key_down((uint32_t)i) ? 0x80 : 0; return TRUE; }
BOOL WINAPI SetKeyboardState(BYTE*) { return TRUE; }
UINT WINAPI MapVirtualKeyW(UINT code, UINT type)
{
    switch (type) {
    case MAPVK_VK_TO_CHAR: return (code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9') ? code : 0;
    case MAPVK_VK_TO_VSC: case MAPVK_VK_TO_VSC_EX: return win::scan_from_vk(code);
    case MAPVK_VSC_TO_VK: case MAPVK_VSC_TO_VK_EX: {
        UINT vk = win::vk_from_scan(code & 0xff, (code & 0xff00) == 0xe000);
        if (type == MAPVK_VSC_TO_VK) {       // left/right variants fold to the generic key
            if (vk == VK_LSHIFT || vk == VK_RSHIFT) vk = VK_SHIFT;
            if (vk == VK_LCONTROL || vk == VK_RCONTROL) vk = VK_CONTROL;
            if (vk == VK_LMENU || vk == VK_RMENU) vk = VK_MENU;
        }
        return vk;
    }
    }
    return 0;
}
UINT WINAPI MapVirtualKeyA(UINT code, UINT type) { return MapVirtualKeyW(code, type); }
// Key names as the US layout reports them: lparam bits 16-23 scan code, bit 24 extended.
INT WINAPI GetKeyNameTextW(LONG lparam, LPWSTR buf, INT n)
{
    if (!buf || n <= 0) return 0;
    UINT scan = (lparam >> 16) & 0xff;
    bool ext = (lparam >> 24) & 1;
    UINT vk = win::vk_from_scan(scan, ext);
    char tmp[16]; const char* name = nullptr;
    static const struct { UINT vk; const char* name; } names[] = {
        {VK_ESCAPE,"Esc"}, {VK_BACK,"Backspace"}, {VK_TAB,"Tab"}, {VK_RETURN,"Enter"}, {VK_CONTROL,"Ctrl"},
        {VK_SHIFT,"Shift"}, {VK_RSHIFT,"Right Shift"}, {VK_MENU,"Alt"}, {VK_SPACE,"Space"}, {VK_CAPITAL,"Caps Lock"},
        {VK_NUMLOCK,"Num Lock"}, {VK_SCROLL,"Scroll Lock"}, {VK_MULTIPLY,"Num *"}, {VK_SUBTRACT,"Num -"}, {VK_ADD,"Num +"},
        {VK_DECIMAL,"Num Del"}, {VK_HOME,"Home"}, {VK_UP,"Up"}, {VK_PRIOR,"Page Up"}, {VK_LEFT,"Left"}, {VK_RIGHT,"Right"},
        {VK_END,"End"}, {VK_DOWN,"Down"}, {VK_NEXT,"Page Down"}, {VK_INSERT,"Insert"}, {VK_DELETE,"Delete"},
        {VK_DIVIDE,"Num /"}, {VK_LWIN,"Left Windows"}, {VK_RWIN,"Right Windows"}, {VK_APPS,"Application"},
        {VK_OEM_MINUS,"-"}, {VK_OEM_PLUS,"="}, {VK_OEM_4,"["}, {VK_OEM_6,"]"}, {VK_OEM_1,";"}, {VK_OEM_7,"'"},
        {VK_OEM_3,"`"}, {VK_OEM_5,"\\"}, {VK_OEM_COMMA,","}, {VK_OEM_PERIOD,"."}, {VK_OEM_2,"/"},
    };
    if (vk == VK_LCONTROL || vk == VK_RCONTROL) vk = VK_CONTROL;
    if (vk == VK_LSHIFT) vk = VK_SHIFT;
    if (vk == VK_LMENU || vk == VK_RMENU) vk = VK_MENU;
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) { tmp[0] = (char)vk; tmp[1] = 0; name = tmp; }
    else if (vk >= VK_F1 && vk <= VK_F24) { snprintf(tmp, sizeof tmp, "F%u", vk - VK_F1 + 1); name = tmp; }
    else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) { snprintf(tmp, sizeof tmp, "Num %u", vk - VK_NUMPAD0); name = tmp; }
    else for (auto& e : names) if (e.vk == vk) { name = e.name; break; }
    if (!name) { buf[0] = 0; return 0; }
    std::u16string w = utf16_of(name);
    int len = (int)w.size() < n - 1 ? (int)w.size() : n - 1;
    memcpy(buf, w.data(), len * sizeof(WCHAR));
    buf[len] = 0;
    return len;
}
HKL WINAPI GetKeyboardLayout(DWORD) { return (HKL)0x04090409; }
INT WINAPI ToUnicode(UINT, UINT, const BYTE*, LPWSTR, INT, UINT) { return 0; }

BOOL WINAPI GetCursorPos(POINT* p) { gdi::Point c = win::cursor_pos(); p->x = c.x; p->y = c.y; return TRUE; }
BOOL WINAPI SetCursorPos(INT, INT) { return FALSE; }
INT WINAPI ShowCursor(BOOL) { return 0; }

static HCURSOR g_cursor;
HCURSOR WINAPI SetCursor(HCURSOR c)
{
    HCURSOR old = g_cursor;
    g_cursor = c;
    static const struct { LPCWSTR id; win::Cursor shape; } map[] = {
        { (LPCWSTR)IDC_ARROW, win::Cursor::Arrow }, { (LPCWSTR)IDC_IBEAM, win::Cursor::IBeam },
        { (LPCWSTR)IDC_WAIT, win::Cursor::Wait }, { (LPCWSTR)IDC_CROSS, win::Cursor::Cross },
        { (LPCWSTR)IDC_SIZEWE, win::Cursor::SizeWE }, { (LPCWSTR)IDC_SIZENS, win::Cursor::SizeNS },
        { (LPCWSTR)IDC_SIZENWSE, win::Cursor::SizeNWSE }, { (LPCWSTR)IDC_SIZENESW, win::Cursor::SizeNESW },
        { (LPCWSTR)IDC_SIZEALL, win::Cursor::SizeAll }, { (LPCWSTR)IDC_HAND, win::Cursor::Hand },
        { (LPCWSTR)IDC_NO, win::Cursor::No },
    };
    if (!c) { win::set_cursor(win::Cursor::None); return old; }
    for (auto& m : map) if (LoadCursorW(nullptr, m.id) == c) { win::set_cursor(m.shape); return old; }
    win::set_cursor(win::Cursor::Arrow);
    return old;
}
HCURSOR WINAPI GetCursor(void) { return g_cursor; }

// TrackMouseEvent: TME_LEAVE is the toolkit's (win::track_mouse_leave: WM_MOUSELEAVE
// when the pointer leaves the window); TME_HOVER is a timer here: WM_MOUSEHOVER once the
// pointer has rested inside the 4x4 hover rectangle for the hover time over the
// window (or it has the capture), then hover tracking ends, as in win32. TME_QUERY reports
// the window last tracked and what is still pending for it (Wine's themed button tracks
// only when the query says nothing is: without TME_HOVER ever arriving, BST_HOT was never
// set and push buttons, check and radio boxes had no hot state).
static const UINT_PTR HOVER_TIMER = 0x7f7f7f7f;
static HWND g_tme_hwnd;                 // the window last given to TrackMouseEvent
static HWND g_hover_hwnd;               // TME_HOVER pending for this window
static UINT g_hover_ms;
static POINT g_hover_pt;                // screen point the rest is measured from

static void CALLBACK hover_timer(HWND h, UINT, UINT_PTR, DWORD)
{
    KillTimer(h, HOVER_TIMER);
    if (h != g_hover_hwnd || !IsWindow(h)) return;
    POINT p; GetCursorPos(&p);
    const int cx = 2, cy = 2;           // SPI_GETMOUSEHOVERWIDTH/HEIGHT 4, centred
    if (abs(p.x - g_hover_pt.x) > cx || abs(p.y - g_hover_pt.y) > cy) {
        g_hover_pt = p;                 // still moving: measure the rest from here
        SetTimer(h, HOVER_TIMER, g_hover_ms, hover_timer);
        return;
    }
    HWND under = WindowFromPoint(p);
    if (under != h && GetCapture() != h) return;   // left without a leave (hidden, covered): drop it
    g_hover_hwnd = nullptr;
    ScreenToClient(h, &p);
    WPARAM keys = 0;
    if (GetKeyState(VK_LBUTTON) < 0) keys |= MK_LBUTTON;
    if (GetKeyState(VK_RBUTTON) < 0) keys |= MK_RBUTTON;
    if (GetKeyState(VK_MBUTTON) < 0) keys |= MK_MBUTTON;
    if (GetKeyState(VK_SHIFT) < 0) keys |= MK_SHIFT;
    if (GetKeyState(VK_CONTROL) < 0) keys |= MK_CONTROL;
    PostMessageW(h, WM_MOUSEHOVER, keys, MAKELPARAM(p.x, p.y));
}

BOOL WINAPI TrackMouseEvent(TRACKMOUSEEVENT* t)
{
    if (!t || t->cbSize != sizeof(TRACKMOUSEEVENT)) return FALSE;
    if (t->dwFlags & TME_QUERY) {
        win::Window* q = g_tme_hwnd ? window_of(g_tme_hwnd) : nullptr;
        t->dwFlags = 0; t->hwndTrack = nullptr; t->dwHoverTime = 0;
        if (q && win::is_window(q)) {
            if (q->track_leave) t->dwFlags |= TME_LEAVE;
            if (q->track_nc_leave) t->dwFlags |= TME_LEAVE | TME_NONCLIENT;
            if (g_hover_hwnd == g_tme_hwnd) { t->dwFlags |= TME_HOVER; t->dwHoverTime = g_hover_ms; }
            if (t->dwFlags) t->hwndTrack = g_tme_hwnd;
        }
        return TRUE;
    }
    win::Window* w = window_of(t->hwndTrack);
    if (!win::is_window(w)) return FALSE;
    if (t->dwFlags & TME_CANCEL) {
        if (t->dwFlags & TME_LEAVE) { if (t->dwFlags & TME_NONCLIENT) w->track_nc_leave = false; else w->track_leave = false; }
        if ((t->dwFlags & TME_HOVER) && g_hover_hwnd == t->hwndTrack) { KillTimer(g_hover_hwnd, HOVER_TIMER); g_hover_hwnd = nullptr; }
        return TRUE;
    }
    g_tme_hwnd = t->hwndTrack;
    if (t->dwFlags & TME_LEAVE) win::track_mouse_leave(w, false);
    if (t->dwFlags & TME_NONCLIENT) win::track_mouse_leave(w, true);
    if (t->dwFlags & TME_HOVER) {
        if (g_hover_hwnd && g_hover_hwnd != t->hwndTrack && IsWindow(g_hover_hwnd)) KillTimer(g_hover_hwnd, HOVER_TIMER);
        g_hover_hwnd = t->hwndTrack;
        g_hover_ms = t->dwHoverTime == HOVER_DEFAULT || !t->dwHoverTime ? 400 : t->dwHoverTime;
        GetCursorPos(&g_hover_pt);
        SetTimer(t->hwndTrack, HOVER_TIMER, g_hover_ms, hover_timer);
    }
    return TRUE;
}

BOOL WINAPI DragDetect(HWND h, POINT pt)
{
    int cx = GetSystemMetrics(SM_CXDRAG), cy = GetSystemMetrics(SM_CYDRAG);
    RECT rect = { pt.x - cx, pt.y - cy, pt.x + cx, pt.y + cy };
    SetCapture(h);
    for (;;) {
        MSG msg;
        while (PeekMessageW(&msg, 0, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE)) {
            if (msg.message == WM_LBUTTONUP) { ReleaseCapture(); return FALSE; }
            if (msg.message == WM_MOUSEMOVE) {
                POINT tmp = { (SHORT)LOWORD(msg.lParam), (SHORT)HIWORD(msg.lParam) };
                ClientToScreen(h, &tmp);
                if (!PtInRect(&rect, tmp)) { ReleaseCapture(); return TRUE; }
            }
        }
        if (GetCapture() != h) return FALSE;
        WaitMessage();
    }
}

// ---- caret ----------------------------------------------------------------

BOOL WINAPI CreateCaret(HWND h, HBITMAP, INT w, INT ht)
{
    win::Window* win = window_of(h);
    if (!win::is_window(win)) return FALSE;
    return win::create_caret(win, w ? w : GetSystemMetrics(SM_CXBORDER), ht ? ht : GetSystemMetrics(SM_CYBORDER));
}
BOOL WINAPI DestroyCaret(void) { win::destroy_caret(); return TRUE; }
BOOL WINAPI SetCaretPos(INT x, INT y) { win::set_caret_pos(gdi::Point{x, y}); return TRUE; }
BOOL WINAPI GetCaretPos(POINT* p) { gdi::Point c = win::caret_pos(); p->x = c.x; p->y = c.y; return TRUE; }
BOOL WINAPI ShowCaret(HWND h) { win::show_caret(h ? window_of(h) : nullptr); return TRUE; }
BOOL WINAPI HideCaret(HWND h) { win::hide_caret(h ? window_of(h) : nullptr); return TRUE; }
UINT WINAPI GetCaretBlinkTime(void) { return 500; }
BOOL WINAPI SetCaretBlinkTime(UINT) { return TRUE; }

// ---- Wine-internal helpers used by the vendored user32 files --------------

WND* WIN_GetPtr(HWND h)
{
    win::Window* w = window_of(h);
    Window* d = wdata(h);
    if (!win::is_window(w) || !d) return nullptr;
    d->wnd.dwStyle = w->style;
    d->wnd.dwExStyle = w->ex_style;
    d->wnd.cbWndExtra = d->cls->wnd_extra;
    d->wnd.wExtra = (DWORD_PTR*)d->extra.data();
    d->wnd.wIDmenu = (UINT_PTR)w->id;
    return &d->wnd;
}

void WIN_ReleasePtr(WND*) {}
HWND WIN_GetFullHandle(HWND h) { return h; }

BOOL WIN_GetRectangles(HWND h, enum coords_relative rel, RECT* rw, RECT* rc)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return FALSE;
    gdi::Rect wr, cr;
    switch (rel) {
    case COORDS_CLIENT:
        wr = gdi::offset(gdi::rect_xywh(0, 0, w->rect.width(), w->rect.height()), -w->client.left, -w->client.top);
        cr = win::client_rect(w);
        break;
    case COORDS_WINDOW:
        wr = gdi::rect_xywh(0, 0, w->rect.width(), w->rect.height());
        cr = w->client;
        break;
    case COORDS_PARENT:
        wr = w->rect;
        cr = gdi::offset(w->client, w->rect.left, w->rect.top);
        break;
    default: {
        wr = win::window_rect(w);
        cr = gdi::offset(w->client, wr.left, wr.top);
        break;
    }
    }
    if (rw) *rw = RECT_of(wr);
    if (rc) *rc = RECT_of(cr);
    return TRUE;
}

HWND* WIN_ListChildren(HWND h)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return nullptr;
    std::vector<win::Window*> kids;
    for (win::Window* c = w->child; c; c = c->next) kids.push_back(c);
    HWND* list = (HWND*)HeapAlloc(GetProcessHeap(), 0, (kids.size() + 1) * sizeof(HWND));
    for (size_t i = 0; i < kids.size(); i++) list[i] = hwnd_of(kids[i]);
    list[kids.size()] = nullptr;
    return list;
}

ULONG WIN_SetStyle(HWND h, ULONG set, ULONG clear)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return 0;
    ULONG old = w->style;
    w->style = (w->style | set) & ~clear;
    return old;
}

BOOL WIN_IsWindowDrawable(HWND h, BOOL)
{
    win::Window* w = window_of(h);
    return win::is_window(w) && win::is_visible(w);
}

INT_PTR WINPROC_CallDlgProcW(DLGPROC func, HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (!func) return 0;
    return func(h, msg, wp, lp);
}
INT_PTR WINPROC_CallDlgProcA(DLGPROC func, HWND h, UINT msg, WPARAM wp, LPARAM lp) { return WINPROC_CallDlgProcW(func, h, msg, wp, lp); }

void WINAPI NotifyWinEvent(DWORD, HWND, LONG, LONG) {}

void WINPOS_ActivateOtherWindow(HWND h)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return;
    if (w->owner && win::is_window(w->owner) && win::is_enabled(w->owner)) { win::set_focus(w->owner); return; }
    std::vector<win::Window*> tops; win::toplevels(tops);
    for (win::Window* t : tops)
        if (t != w && t != g_desktop && (t->style & WS_VISIBLE) && win::is_enabled(t)) { win::set_focus(t); return; }
}

const char* SPY_GetMsgName(UINT, HWND) { return ""; }

} // extern "C"
