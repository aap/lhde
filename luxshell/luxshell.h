/* lhshell internals shared by lhshell.c (X11 core + Lua), tray.c (XEmbed system tray) and notify.c (D-Bus toasts) */
#ifndef LHSHELL_H
#define LHSHELL_H
#include "scene.h"
#include <X11/Xlib.h>
#include <cairo/cairo.h>
#include <lua5.4/lua.h>

struct surface { Window win; cairo_surface_t *xs; int x, y, w, h; struct node *root; bool dirty, mapped, grabbed; struct node *hot, *pressed; int press_x, press_y, radius, cursor; int geo_dx, geo_dy; int lua_ref; struct surface *next; };

extern Display *dpy; extern int screen; extern Window root; extern Visual *argb; extern lua_State *L;
void emit(const char *ev, int nargs);                       /* lh._emit(ev, <nargs values on the Lua stack>) */
void icon_register(const char *key, cairo_surface_t *s);    /* "@..." image keys usable as Lua image paths (takes ownership) */
struct surface *surface_of_node(struct node *n);
void surface_damage(struct surface *s);
extern int xerror_quiet;

/* tray.c */
bool tray_start(struct node *n);                            /* own _NET_SYSTEM_TRAY_S<screen>, host icons in n's surface */
bool tray_handle_event(XEvent *e);                          /* true when the event was the tray's */
void tray_place(struct surface *s);                         /* after layout: move icon windows to the tray node's slots */
void tray_paint(struct node *n, cairo_t *cr);
int tray_count(void);
Window tray_manager(void);                                   /* our manager window while we own the selection, else None */
void tray_stop(void);
void tray_forget(struct node *gone);                         /* gone (or its subtree) is being freed: drop the tray node if it is in there */                                       /* on SIGTERM/SIGINT: icons back to the root, unmapped */

/* menupaint.c: lh.menu_render (wuxtheme's popup menu painter for menu.lua) */
int l_menu_render(lua_State *L);

/* notify.c */
int notify_start(void);                                     /* org.freedesktop.Notifications on the session bus; fd or -1 */
void notify_dispatch(void);
bool notify_pending(void);
void notify_closed(unsigned id, unsigned reason);          /* NotificationClosed: 1 expired, 2 dismissed, 3 CloseNotification, 4 other */
void notify_action(unsigned id, const char *key);          /* ActionInvoked */
#endif
