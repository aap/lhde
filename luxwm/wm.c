/* Copyright (c) 2018 Joshua L Ervin. All rights reserved. */
/* Licensed under the MIT License. See the LICENSE file in the project root for full license information. */

#include "config.h"

#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include <sys/stat.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/sync.h>
#include <X11/cursorfont.h>
#include <X11/Xft/Xft.h>

#include "globals.h"
#include "ipc.h"
#include "types.h"
#include "utils.h"
#include "frame.h"
#include "menu.h"
#include <lhde/data.h>
#include <lhde/theme.h>
#include <lhde/settings.h>
#include <cairo/cairo-xlib.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>

static struct client *f_client = NULL; /* focused client (the last one luxwm focused: Alt+Tab, key actions start from it) */
static bool f_active = false;          /* f_client really holds the X input focus; false while an unmanaged window (the desktop,
                                          a dock) has it: no frame is painted active, _NET_ACTIVE_WINDOW is None (focus_sync) */
#define client_active(c) ((c) != NULL && (c) == f_client && f_active)
static struct client *c_list[WORKSPACE_NUMBER]; /* 'stack' of managed clients in drawing order */
static struct client *f_list[WORKSPACE_NUMBER]; /* ordered lists for clients to be focused */
static struct monitor *m_list = NULL; /* All saved monitors */
static struct config conf; /* global config */
static int ws_m_list[WORKSPACE_NUMBER]; /* Mapping from workspaces to associated monitors */
static int curr_ws = 0;
static int m_count = 0;
static Cursor move_cursor, normal_cursor, edge_cursor[HIT_BR + 1];
static struct theme theme;
static Window adopt_pending[256]; static int adopt_count = 0;
static Window adopt_active = None, adopt_stack[256]; static int adopt_nstack = 0;   /* the previous WM's active window and stacking order */
static Visual *argb_visual; static Colormap argb_cmap; static int argb_depth = 32;
static void margins(struct client *c, int *l, int *r, int *t, int *b);
static void client_read_hints(struct client *c);
static void client_area(struct client *c, int fw, int fh, int *x, int *y, int *w, int *h);
static void frame_fit(struct client *c, int *fw, int *fh);
static void frame_button_press(struct client *c, int hit, XButtonPressedEvent *bev);
static void frame_resize_drag(struct client *c, int hit, XButtonPressedEvent *bev);
static void handle_motion_notify(XEvent *e);
static void handle_key_press(XEvent *e);
static void handle_key_release(XEvent *e);
static void keys_setup(void);
static void spawn(const char *cmd);
static bool sd_instant;
static void showing_desktop(int mode);   /* 0 off, 1 on, 2 toggle */
static void showing_forget(void);
static void showing_changed(const char *why);
static void focus_next(struct client *c);
static void handle_leave_notify(XEvent *e);
static void client_set_icon(struct client *c);
static cairo_surface_t *client_icon(struct client *c);
/* EWMH states, stacking order, struts and work area */
static void ewmh_update_state(struct client *c);
static int client_hit(struct client *c, int x, int y);
static cairo_surface_t *app_icon(int want);
static void ewmh_update_stacking(void);
static void client_minimize(struct client *c);
static void client_restore(struct client *c);
static void client_maximize(struct client *c);
static void client_unmaximize(struct client *c);
static void client_apply_max(struct client *c);
static void client_move_raw(struct client *c, int x, int y);
static void client_initial_place(struct client *c, int req_x, int req_y);
static void focus_after(struct client *c);
static void workarea_mon(int mon, int *x, int *y, int *w, int *h);
static void workarea_update(void);
static void dock_track(Window w);
static void dock_forget(Window w);
static bool dock_property(Window w, Atom a);
static void docks_raise(void);
static void handle_map_notify(XEvent *e);
static void handle_destroy_notify(XEvent *e);
/* maximised mode, compositor detection, runtime theme switching */
static void maxmode_update(void);
static bool theme_select(void);
static void theme_property(void);
static void compositor_check(void);
static char theme_req[2200], theme_cur[2200], theme_written[2200];   /* requested theme (a directory or a spec DIR|STYLE|COLOR|SIZE), the one in use, last _LHWM_THEME written */
static bool compositor = false;
static int xfixes_ev = -1;
static Atom cm_atom, lhwm_theme_atom, lhwm_maxmode_atom, lhwm_max_atom;
/* the session theme (SHELL-DESIGN 14) is the root _LH_THEME, a theme name that liblhde resolves (docs/THEME-FORMAT.md:
   theme files, bare styles, aliases); luxwm maps the record to frames / frames_classic / composition. _LHWM_THEME is the derived
   frame directory. */
static char theme_req_classic[1024];   /* the resolved frames_classic directory ("style" = the style's own decorations) */
static bool theme_comp_off = false;    /* Glass.composition = 0, or no glass frame set: always the classic frame */
static bool theme_announce = false;    /* a switch was asked for: republish _LHWM_COMPOSITED and _LHWM_FRAME_METRICS even if unchanged */
static char lh_theme_cur[256];   /* the _LH_THEME name applied last */
static Atom lh_theme_atom;
static Atom lhwm_reload_atom;          /* _LHWM_RELOAD: root ClientMessage or property, re-read the current theme (LUXWM.md "Live-editing a theme") */
static int lh_theme_selfwrite;         /* _LH_THEME writes of luxwm's own (alias -> name) whose PropertyNotify is not a reload request */
static bool theme_force;               /* theme_select: load and apply even when the spec is the one in use (reload) */
static void theme_reload(const char *why);
/* _LH_THEME_RESOLVED (docs/THEME-FORMAT.md section 4): the record of the theme in effect, "key=value" lines with theme= and
   serial=; luxwm publishes it when it resolved a theme the property does not describe yet, and re-resolves when somebody
   republishes it for the current theme (a theme file changed: luxshell's flyout, Display Properties). theme_anim: the
   theme's Glass.animations (settings_load) */
static Atom lh_resolved_atom, lh_serial_atom;
static char theme_anim[64];
static char *theme_record;             /* the record luxwm applied last (serialized without serial=), to see changes */
static void lh_resolved_property(void);
static bool lh_theme_apply(const char *arg, char *canon, size_t n);
static void lh_theme_property(void);
static void theme_recheck(const char *why);
static bool root_string(Atom a, char *buf, size_t n);
static char *root_string_alloc(Atom a);
static const char *theme_opt = NULL;   /* -t THEME */
/* extended frames (CLIENT-PROTOCOL.md): client properties, and the root properties clients read */
static Atom lhwm_ext_atom, lhwm_flags_atom, lhwm_comp_atom, lhwm_metrics_atom, lhwm_deco_atom;
/* _LHWM_ANIMATE value while a maximise/restore geometry animation runs (picom-glass.conf): ANIM_INVENTED only (LUXWM.md "Animations") */
#define ANIM_BUSY 2
/* maximise/restore crossfade of the DWM frame plate, as luxshell's black layers (SHELL-DESIGN.md 2.4.3: 400 ms, 25 ms tick) */
#define XFADE_MS (MAX_FADE_MS > 0 ? (double)MAX_FADE_MS : 400.0)   /* config.h MAX_FADE_MS; 0 = no fade at all (max_transition) */
#define XFADE_TICK 25
static Atom lhwm_anim_atom, net_icon_geom_atom;   /* _LHWM_ANIMATE (picom-glass.conf animation rules), _NET_WM_ICON_GEOMETRY */
/* the animation style in use (LUXWM.md "Animations"): the theme's Glass.animations, LHWM_ANIMATIONS, else config.h ANIMATE_STYLE;
   ANIM_NONE: no _LHWM_ANIMATE property and no animation of luxwm's own. Reloaded on the root ClientMessage _LH_SETTINGS_CHANGED. */
#define ANIM_NONE 0
static long anim_style = ANIMATE_STYLE;
static Atom lh_settings_atom;   /* _LH_SETTINGS_CHANGED */
static void settings_load(bool startup);
static bool super_other;   /* something else happened while Super was held (Super alone: the start menu on its release) */
#define ANIM_ON (ANIMATE && anim_style != ANIM_NONE)
static Atom lhwm_desklayer_atom;                  /* _LHWM_DESKTOP_LAYER: order of the desktop windows (docks_raise) */
/* a dock changed its struts or state: _NET_WORKAREA and the refit of maximised windows wait until the event queue is drained,
   so a burst of dock changes (lux's Win+S: the sidebar's state, strut, geometry and stacking plus the taskbar's width, sent in
   one server grab) is restacked first and refits the windows once, against the final struts */
static bool workarea_dirty = false;
static void anim_mark(struct client *c, long v);
static void anim_classic(struct client *c, int fx, int fy, int fw, int fh, int tx, int ty, int tw, int th);
static void anim_icon_rect(struct client *c, int *x, int *y, int *w, int *h);
static void client_forget(struct client *c);
static void handle_reparent_notify(XEvent *e);
static int anim_expire(void);
static double xfade_weight(struct client *c);
static int xfade_tick(void);
static void max_transition(struct client *c, bool to_max, int x, int y, int w, int h);
static int comp_written = -1;   /* _LHWM_COMPOSITED last written */
static Atom lhwm_hdrpix_atom, lhwm_caprect_atom;   /* _LHWM_HEADER_PIXMAP, _LHWM_CAPTION_RECT (CLIENT-PROTOCOL.md) */
static unsigned theme_gen;      /* bumped on every theme load: header pixmaps are rendered again */
static void margins_for(struct client *c, const struct theme *th, int max, int *l, int *r, int *t, int *b);
static void header_update(struct client *c, bool focused);
static void client_read_ext(struct client *c);
static void composited_publish(void);
static void drag_run(struct client *c, int hit, int px, int py, unsigned button, bool keyboard);
static void wm_menu_open(struct client *c, int x, int y, unsigned flags, unsigned button, bool from_icon);
static void wm_menu_at_caption(struct client *c, unsigned flags, unsigned button, bool from_icon);
static bool client_on_icon(struct client *c, int x, int y);
static unsigned long next_seq = 1;
static bool adopting = false;
static int prof = 0;   /* LHWM_PROF=1: per-drag timing summary on stderr */
static int sync_ev = -1;   /* XSync extension event base (-1: none) */
static double now_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6; }   /* managing windows that existed before luxwm started (restart) */
static Display *display = NULL;
static Atom net_atom[NetLast], wm_atom[WMLast], net_berry[BerryLast];
static Window root, check, nofocus;
static bool running = true;
static bool debug = false;
static int screen, display_width, display_height;
static int (*xerrorxlib)(Display *, XErrorEvent *);
static XftColor xft_focus_color, xft_unfocus_color;
static XftFont *font;
static char global_font[MAXLEN] = DEFAULT_FONT;
static XRenderColor r_color;
static GC gc;
static Atom utf8string;

/* All functions */

/* Client management functions */
static void client_cardinal_focus(struct client *c, int dir);
static void client_center(struct client *c);
static void client_center_in_rect(struct client *c, int x, int y, unsigned w, unsigned h);
static void client_close(struct client *c);
static void client_kill(struct client *c);
static void client_decorations_create(struct client *c);
static void client_decorations_destroy(struct client *c);
static void client_delete(struct client *c);
static void client_fullscreen(struct client *c, bool toggle, bool fullscreen, bool max);
static void client_hide(struct client *c);
static void client_manage_focus(struct client *c);
static void client_move_absolute(struct client *c, int x, int y);
static void client_move_relative(struct client *c, int x, int y);
static void client_move_to_front(struct client *c);
static void client_monocle(struct client *c);
static void client_place(struct client *c);
static void client_raise(struct client *c);
static void client_restack(struct client *c, Window w, Window sib, long detail, long source);
static void client_refresh(struct client *c);
static void client_resize_absolute(struct client *c, int w, int h);
static void client_resize_relative(struct client *c, int w, int h);
static void client_save(struct client *c, int ws);
static void client_send_to_ws(struct client *c, int ws);
static void client_set_color(struct client *c, unsigned long i_color, unsigned long b_color);
static void client_set_input(struct client *c);
static void client_set_title(struct client *c);
static void client_show(struct client *c);
static void client_snap_left(struct client *c);
static void client_snap_right(struct client *c);
static void client_toggle_decorations(struct client *c);
static void client_set_status(struct client *c);
static void client_set_wm_state(struct client *c, unsigned long state);
static bool client_window_is_below(struct client *c);

/* EWMH functions */
static void ewmh_set_fullscreen(struct client *c, bool fullscreen);
static void ewmh_set_viewport(void);
static void ewmh_set_focus(struct client *c);
static void ewmh_set_desktop(struct client *c, int ws);
static void ewmh_set_frame_extents(struct client *c);
static void ewmh_set_client_list(void);
static void ewmh_set_desktop_names(void);
static void ewmh_set_active_desktop(int ws);
static void ewmh_set_below(struct client *c, bool below);

/* Event handlers */
static void handle_client_message(XEvent *e);
static void handle_configure_notify(XEvent *e);
static void handle_configure_request(XEvent *e);
static void handle_focus(XEvent *e);
static bool client_has_focus(struct client *c);
static void focus_set_active(bool on);
static void handle_map_request(XEvent *e);
static void handle_unmap_notify(XEvent *e);
static void handle_button_press(XEvent *e);
static void handle_expose(XEvent *e);
static void handle_property_notify(XEvent *e);
static void handle_enter_notify(XEvent *e);

/* IPC client functions */
static void ipc_move_absolute(long *d);
static void ipc_move_relative(long *d);
static void ipc_monocle(long *d);
static void ipc_raise(long *d);
static void ipc_resize_absolute(long *d);
static void ipc_resize_relative(long *d);
static void ipc_toggle_decorations(long *d);
static void ipc_window_close(long *d);
static void ipc_window_kill(long *d);
static void ipc_window_center(long *d);
static void ipc_switch_ws(long *d);
static void ipc_send_to_ws(long *d);
static void ipc_fullscreen(long *d);
static void ipc_fullscreen_state(long *d);
static void ipc_snap_left(long *d);
static void ipc_snap_right(long *d);
static void ipc_cardinal_focus(long *d);
static void ipc_cycle_focus(long *d);
static void ipc_below(long *d);
static void ipc_pointer_focus(long *d);
static void ipc_config(long *d);
static void ipc_save_monitor(long *d);
static void ipc_set_font(long *d);
static void ipc_edge_gap(long *d);

static void monitors_free(void);
static void monitors_setup(void);

static void close_wm(void);
static void draw_text(struct client *c, bool focused);
static struct client* get_client_from_window(Window w);
static void load_color(XftColor *dest_color, unsigned long raw_color);
static void load_config(char *conf_path);
static void manage_new_window(Window w, XWindowAttributes *wa);
static int manage_xsend_icccm(struct client *c, Atom atom);
static void window_grab_buttons(Window window);
static void window_ungrab_buttons(Window window);
static void grab_buttons(void);
static void ungrab_buttons(void);
static void refresh_config(void);
static void run(void);
static bool safe_to_focus(int ws);
static void setup(void);
static void switch_ws(int ws);
static void warp_pointer(struct client *c);
static void usage(void);
static void version(void);
static int xerror(Display *display, XErrorEvent *e);

static int get_dec_width(struct client *c);
static int get_dec_height(struct client *c);

typedef void (*x11_event_handler_t)(XEvent *e);
typedef void (*ipc_event_handler_t)(long *e);

/* Native X11 Event handler */
static const x11_event_handler_t event_handler [LASTEvent] = {
    [MapRequest]       = handle_map_request,
    [UnmapNotify]      = handle_unmap_notify,
    [ReparentNotify]   = handle_reparent_notify,
    [ConfigureNotify]  = handle_configure_notify,
    [ConfigureRequest] = handle_configure_request,
    [ClientMessage]    = handle_client_message,
    [ButtonPress]      = handle_button_press,
    [PropertyNotify]   = handle_property_notify,
    [Expose]           = handle_expose,
    [MotionNotify]     = handle_motion_notify,
    [KeyPress]         = handle_key_press,
    [KeyRelease]       = handle_key_release,
    [LeaveNotify]      = handle_leave_notify,
    [FocusIn]          = handle_focus,
    [FocusOut]         = handle_focus,
    [EnterNotify]      = handle_enter_notify,
    [MapNotify]        = handle_map_notify,
    [DestroyNotify]    = handle_destroy_notify,
};

static const ipc_event_handler_t ipc_handler [IPCLast] = {
    [IPCWindowMoveRelative]       = ipc_move_relative,
    [IPCWindowMoveAbsolute]       = ipc_move_absolute,
    [IPCWindowMonocle]            = ipc_monocle,
    [IPCWindowRaise]              = ipc_raise,
    [IPCWindowResizeRelative]     = ipc_resize_relative,
    [IPCWindowResizeAbsolute]     = ipc_resize_absolute,
    [IPCWindowToggleDecorations]  = ipc_toggle_decorations,
    [IPCWindowClose]              = ipc_window_close,
    [IPCWindowKill]               = ipc_window_kill,
    [IPCWindowCenter]             = ipc_window_center,
    [IPCSwitchWorkspace]          = ipc_switch_ws,
    [IPCSendWorkspace]            = ipc_send_to_ws,
    [IPCBelow]                    = ipc_below,
    [IPCFullscreen]               = ipc_fullscreen,
    [IPCFullscreenState]          = ipc_fullscreen_state,
    [IPCSnapLeft]                 = ipc_snap_left,
    [IPCSnapRight]                = ipc_snap_right,
    [IPCCardinalFocus]            = ipc_cardinal_focus,
    [IPCCycleFocus]               = ipc_cycle_focus,
    [IPCPointerFocus]             = ipc_pointer_focus,
    [IPCSaveMonitor]              = ipc_save_monitor,
    [IPCSetFont]                  = ipc_set_font,
    [IPCEdgeGap]                  = ipc_edge_gap,
    [IPCConfig]                   = ipc_config
};

static unsigned
euclidean_distance(const struct client *a, const struct client *b)
{
    int dx = a->geom.x - b->geom.x, dy = a->geom.y - b->geom.y;
    return dx*dx + dy*dy;
}

/* Give focus to the given client in the given direction */
static void
client_cardinal_focus(struct client *c, int dir)
{
    struct client *tmp, *focus_next;
    int min;

    tmp = c_list[curr_ws];
    focus_next = NULL;
    min = INT_MAX;

    while (tmp != NULL) {
        int dist = euclidean_distance(c, tmp);
        switch (dir) {
            case EAST:
                LOGN("Focusing EAST");
                if (tmp->geom.x > c->geom.x && dist < min) {
                    min = dist;
                    focus_next = tmp;
                }
                break;
            case SOUTH:
                LOGN("Focusing SOUTH");
                if (tmp->geom.y > c->geom.y && dist < min) {
                    min = dist;
                    focus_next = tmp;
                }
                break;
            case WEST:
                LOGN("Focusing WEST");
                if (tmp->geom.x < c->geom.x && dist < min) {
                    min = dist;
                    focus_next = tmp;
                }
                break;
            case NORTH:
                LOGN("Focusing NORTH");
                if (tmp->geom.y < c->geom.y && dist < min) {
                    min = dist;
                    focus_next = tmp;
                }
                break;
        }
        tmp = tmp->next;
    }

    if (focus_next == NULL) {
        LOGN("Cannot cardinal focus, no valid windows found");
        return;
    } else {
        LOGP("Valid window found in direction %d, focusing", dir);
        client_manage_focus(focus_next);
    }
}

/* Move a client to the center of the screen, centered vertically and horizontally
 * by the middle of the Client
 */
static void
client_center(struct client *c)
{
    int x, y, w, h;
    LOGN("Centering Client");
    workarea_mon(ws_m_list[c->ws], &x, &y, &w, &h);   /* the work area already excludes struts and gaps */
    client_center_in_rect(c, x, y, w, h);
}

static int
ceil10 (int n)
{
    return (n + 9) - (n + 9) % 10;
}

static void
client_center_in_rect(struct client *c, int x, int y, unsigned w, unsigned h)
{
    LOGP("Centering at x=%d, y=%d, w=%u, h=%u", x, y, w, h);
    int new_x = ceil10(x + (int)w / 2 - c->geom.width / 2);
    int new_y = ceil10(y + (int)h / 2 - c->geom.height / 2);
    LOGP("Sending to x=%d, y=%d", new_x, new_y);
    client_move_absolute(c, new_x, new_y);

    client_refresh(c); // in case we went over the top gap
}

/* Close connection to the current display */
static void
close_wm(void)
{
    LOGN("Shutting down window manager");

    /* clean exit (luxwmc quit, SHELL-DESIGN 14): clients stay where they are and mapped; windows parked off screen on other
       workspaces come back; minimised ones stay unmapped with WM_STATE Iconic, so the next luxwm adopts them minimised again
       (berry set every client Withdrawn here, which made a restarted WM lose the minimised ones). Frames go away. */
    for (int i = 0; i < WORKSPACE_NUMBER; i++)
        for (struct client *c = c_list[i]; c != NULL; c = c->next) {
            if (c->hidden && !c->minimized)
                client_move_raw(c, c->x_hide, c->geom.y);
            if (c->decorated) {
                XDeleteProperty(display, c->window, lhwm_deco_atom);
                XDestroyWindow(display, c->dec);
            }
            XDeleteProperty(display, c->window, net_atom[NetWMFrameExtents]);
            if (c->hdr_pix) { XDeleteProperty(display, c->window, lhwm_hdrpix_atom); XFreePixmap(display, c->hdr_pix); c->hdr_pix = None; }
            if (c->cap_rect_set) XDeleteProperty(display, c->window, lhwm_caprect_atom);
        }

    XDeleteProperty(display, root, net_berry[BerryWindowStatus]);
    XDeleteProperty(display, root, net_berry[BerryFontProperty]);
    XDeleteProperty(display, root, net_atom[NetSupported]);
    XDeleteProperty(display, root, net_atom[NetShowingDesktop]);
    XDeleteProperty(display, root, lhwm_maxmode_atom);
    XDeleteProperty(display, root, lhwm_theme_atom);
    XDeleteProperty(display, root, lhwm_comp_atom);
    XDeleteProperty(display, root, lhwm_metrics_atom);
    XDeleteProperty(display, root, net_atom[NetWMCheck]);   /* luxshell then falls back to its EWMH rules (maxmode) */
    XDeleteProperty(display, root, net_atom[NetWorkarea]);
    /* kept on purpose: _LH_THEME (the session's setting, not luxwm's), and _NET_CLIENT_LIST,
       _NET_CLIENT_LIST_STACKING, _NET_ACTIVE_WINDOW, which a restarted luxwm uses to restore order and focus */
    fprintf(stderr, "luxwm: clean exit\n");

    LOGN("Closing display...");
    XCloseDisplay(display);
}

/* a window whose WM_NORMAL_HINTS fix its size (min == max: a win32 dialog without WS_THICKFRAME, flxp's property sheets): no
   resize from the frame's edges, Alt+drag or the window menu, no maximise (its button drawn disabled), as Windows treats a
   window without WS_THICKFRAME and WS_MAXIMIZEBOX */
static bool
client_fixed(const struct client *c)
{
    return c->max_w > 0 && c->max_h > 0 && c->min_w == c->max_w && c->min_h == c->max_h;
}

static void
draw_text(struct client *c, bool focused)
{
    if (!c->decorated || c->dec == None) return;
    struct frame_ext ext, *e = NULL;
    bool comp = compositor && theme.mode == THEME_DWM, cc = (c->ext_flags & FRAME_CLIENT_CAPTION) != 0;
    unsigned pf = (unsigned)c->ext_flags & ~(unsigned)FRAME_NO_HEADER_PIXMAP;   /* the painter's flags (NO_HEADER_PIXMAP is header_update's) */
    unsigned flags = pf | (cc ? FRAME_NO_TITLE | FRAME_NO_ICON : 0);   /* CLIENT_CAPTION: title and icon are the client's */
    if (client_fixed(c)) flags |= FRAME_MAX_DISABLED;
    /* extended frame: only while composited (glass); in client pixels, clamped to the client */
    if ((c->has_ext || pf) && comp) {
        int l, r, t, b; margins(c, &l, &r, &t, &b);
        int nl, nr, nt, nb; wm_theme_margins_ex(&theme, c->mono, &nl, &nr, &nt, &nb);
        int ax, ay, aw, ah; client_area(c, c->geom.width, c->geom.height, &ax, &ay, &aw, &ah);
        long cw = MAX(aw, 0), ch = MAX(ah, 0);
        memset(&ext, 0, sizeof ext); ext.flags = flags; e = &ext;
        if (c->has_ext && (c->ext[0] & c->ext[1] & c->ext[2] & c->ext[3] & 0xFFFFFFFFUL) == 0xFFFFFFFFUL) { ext.sheet = 1; ext.t = (int)ch + t - nt; }
        else if (c->has_ext) {
            ext.l = (int)MIN((long)c->ext[0], cw); ext.r = (int)MIN((long)c->ext[1], cw - ext.l);
            ext.t = (int)MIN((long)c->ext[2], ch); ext.b = (int)MIN((long)c->ext[3], ch - ext.t);
        }
        /* CLIENT_CAPTION: the client starts above the normal caption bottom (t < nt); the plate's band is measured from there */
        if (!ext.sheet) ext.t = MAX(ext.t + t - nt, 0);
    } else if (!comp && (flags & (FRAME_NO_TITLE | FRAME_NO_ICON | FRAME_MAX_DISABLED))) {   /* msstyles: NO_TITLE / NO_ICON only, a normal frame otherwise */
        memset(&ext, 0, sizeof ext); ext.flags = flags & (FRAME_NO_TITLE | FRAME_NO_ICON | FRAME_MAX_DISABLED); e = &ext;
    } else if (flags & FRAME_MAX_DISABLED) {   /* glass without an extension: only the disabled maximise */
        memset(&ext, 0, sizeof ext); ext.flags = FRAME_MAX_DISABLED; e = &ext;
    }
    /* an opaque client hides the plate under it: that part is neither painted nor uploaded (LUXWM.md "Performance") */
    /* the client's rectangle: the hinted size, which may leave part of the frame's client area free (LUXWM.md "Size hints") */
    XRectangle hole, crect, *hp = NULL;
    { int ax, ay, aw, ah; client_area(c, c->geom.width, c->geom.height, &ax, &ay, &aw, &ah);
      crect.x = (short)ax; crect.y = (short)ay; crect.width = (unsigned short)aw; crect.height = (unsigned short)ah; }
    if (!(e && (e->l || e->r || e->t || e->b || e->sheet)) && c->depth != 32 && !cc) {   /* CLIENT_CAPTION: the buttons lie under the client */
        if (crect.width >= MINIMUM_DIM && crect.height >= MINIMUM_DIM && crect.x + crect.width <= c->geom.width && crect.y + crect.height <= c->geom.height) {
            hole = crect; hp = &hole; }
    }
    wm_frame_paint_ex4(display, argb_visual, &theme, c->dec, c->geom.width, c->geom.height, c->title, client_icon(c), focused, c->hover, c->pressed, c->mono, e, hp,
                    xfade_weight(c), &crect);
    c->paint_serial = wm_frame_upload_serial; c->dirty = false;
    header_update(c, focused);
    XFlush(display);   /* XNextEvent will not flush while events keep arriving */
}

/* _LHWM_HEADER_PIXMAP and _LHWM_CAPTION_RECT on a client (CLIENT-PROTOCOL.md "Header background without composition").
   Header pixmap: while not composited, a client with _LHWM_EXTEND_FRAME (or CLIENT_CAPTION) gets the frame's caption band as a
   pixmap of its own depth and visual: client width x (top frame margin + header rows), from the frame's top edge down to the
   bottom of the client's header (the caption bitmap nine-gridded into the taller band); its last rows lie under the header.
   With CLIENT_CAPTION the caption buttons are in it, in their hover/pressed state. A new pixmap (new id) whenever any of that
   changes; the old one is freed after the property has been replaced. Composited: no property. */
static void
header_update(struct client *c, bool focused)
{
    bool comp = compositor && theme.mode == THEME_DWM, cc = (c->ext_flags & FRAME_CLIENT_CAPTION) != 0;
    int l, r, t, b; margins(c, &l, &r, &t, &b);
    int cw, ch, ax, ay; client_area(c, c->geom.width, c->geom.height, &ax, &ay, &cw, &ch);
    /* the caption-button area in client coordinates, which the client keeps free (and out of its input shape) */
    if (cc && c->decorated && cw > 0 && ch > 0) {
        int bx, by, bw, bh; wm_frame_buttons_rect(&theme, c->geom.width, c->mono, &bx, &by, &bw, &bh);
        long rc[4] = { bx - l, by - t, bw, bh };
        if (rc[0] < 0) { rc[2] += rc[0]; rc[0] = 0; }
        if (rc[1] < 0) { rc[3] += rc[1]; rc[1] = 0; }
        if (rc[0] + rc[2] > cw) rc[2] = cw - rc[0];
        if (rc[1] + rc[3] > ch) rc[3] = ch - rc[1];
        if (rc[2] < 0 || rc[3] < 0) rc[2] = rc[3] = 0;
        /* rewritten on change, and on every maximise/restore even when the rect stays (the client relayouts its header then:
           maximised the caption zone is max_caption_h - max_border_t of _LHWM_FRAME_METRICS; _NET_WM_STATE is written first) */
        if (!c->cap_rect_set || memcmp(rc, c->cap_rect, sizeof rc) || c->cap_rect_mono != c->mono) {
            XChangeProperty(display, c->window, lhwm_caprect_atom, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)rc, 4);
            memcpy(c->cap_rect, rc, sizeof rc); c->cap_rect_set = true; c->cap_rect_mono = c->mono;
        }
    } else if (c->cap_rect_set) {
        XDeleteProperty(display, c->window, lhwm_caprect_atom); c->cap_rect_set = false;
    }
    /* NO_HEADER_PIXMAP (bit 4): the client draws its header itself (flxp's navbar); set at runtime it frees the pixmap and deletes
       the property here, cleared it renders one again (both via the PropertyNotify -> draw_text) */
    bool want = c->decorated && !comp && (c->has_ext || cc) && !(c->ext_flags & FRAME_NO_HEADER_PIXMAP) && c->visual && cw > 0 && ch > 0;
    if (!want) {
        if (c->hdr_pix) { XDeleteProperty(display, c->window, lhwm_hdrpix_atom); XFreePixmap(display, c->hdr_pix); c->hdr_pix = None; c->hdr_key = 0; }
        return;
    }
    int nl, nr, nt, nb; wm_theme_margins_ex(&theme, c->mono, &nl, &nr, &nt, &nb);
    long et = 0;
    if (c->has_ext) et = (c->ext[0] & c->ext[1] & c->ext[2] & c->ext[3] & 0xFFFFFFFFUL) == 0xFFFFFFFFUL ? ch : MIN((long)c->ext[2], (long)ch);
    int band = t + (int)et;
    if (band < 1) band = 1;
    struct frame_ext pe; memset(&pe, 0, sizeof pe);
    pe.t = MAX(band - nt, 0);   /* how far the band reaches below the normal caption */
    int hov = cc ? c->hover : 0, prs = cc ? c->pressed : 0;   /* buttons only matter when they are inside the client */
    unsigned long key = 1469598103934665603UL;
    long kv[] = { c->geom.width, c->geom.height, cw, band, focused, hov, prs, c->mono, theme_gen, pe.t, l };
    for (size_t i = 0; i < sizeof kv / sizeof kv[0]; i++) key = (key ^ (unsigned long)kv[i]) * 1099511628211UL;
    if (c->hdr_pix && key == c->hdr_key) return;
    cairo_surface_t *img = wm_frame_header_image(&theme, c->geom.width, c->geom.height, l, cw, band, focused, hov, prs, c->mono, &pe);
    if (!img) return;
    Pixmap pix = XCreatePixmap(display, root, (unsigned)cw, (unsigned)band, (unsigned)c->depth);   /* root: the client may be gone already */
    cairo_surface_t *xs = cairo_xlib_surface_create(display, pix, c->visual, cw, band);
    cairo_t *cr = cairo_create(xs);
    if (c->depth != 32) { cairo_set_source_rgb(cr, 0, 0, 0); cairo_paint(cr); }   /* no alpha channel: flatten onto black (corner pixels only) */
    cairo_set_operator(cr, c->depth == 32 ? CAIRO_OPERATOR_SOURCE : CAIRO_OPERATOR_OVER);
    cairo_set_source_surface(cr, img, 0, 0); cairo_paint(cr);
    cairo_destroy(cr); cairo_surface_flush(xs); cairo_surface_destroy(xs); cairo_surface_destroy(img);
    long v = (long)pix;
    XChangeProperty(display, c->window, lhwm_hdrpix_atom, XA_PIXMAP, 32, PropModeReplace, (unsigned char *)&v, 1);
    if (c->hdr_pix) XFreePixmap(display, c->hdr_pix);
    c->hdr_pix = pix; c->hdr_key = key;
}

/* repaint the frames that got an Expose since their last paint (called when the event queue is empty) */
static void
paint_dirty(void)
{
    for (int i = 0; i < WORKSPACE_NUMBER; i++)
        for (struct client *c = c_list[i]; c; c = c->next)
            if (c->dirty) { if (prof) fprintf(stderr, "luxwm prof: expose repaint 0x%lx\n", c->dec); draw_text(c, client_active(c)); }
}

static void
margins(struct client *c, int *l, int *r, int *t, int *b)
{
    margins_for(c, &theme, c->mono, l, r, t, b);
}

/* the frame margins of c under theme th; CLIENT_CAPTION (experimental): the client grows up into the caption, only the outer
   border stays above it (_NET_FRAME_EXTENTS follows) */
static void
margins_for(struct client *c, const struct theme *th, int max, int *l, int *r, int *t, int *b)
{
    if (!c->decorated) { *l = *r = *t = *b = 0; return; }
    wm_theme_margins_ex(th, max, l, r, t, b);
    if (c->ext_flags & FRAME_CLIENT_CAPTION) *t = MIN(*t, wm_theme_caption_border(th, max));
}

/* ---- size hints (LUXWM.md "Size hints"): WM_NORMAL_HINTS, ICCCM 4.1.2.3 ----
   Read at map time and on every PropertyNotify of WM_NORMAL_HINTS. ICCCM's defaults: without PBaseSize the minimum is the base,
   without PMinSize the base is the minimum. A maximum below the minimum is raised to it. */
static void
client_read_hints(struct client *c)
{
    XSizeHints h; long sup = 0;
    c->base_w = c->base_h = c->inc_w = c->inc_h = c->min_w = c->min_h = c->max_w = c->max_h = 0;
    c->min_a = c->max_a = 0;
    memset(&h, 0, sizeof h);
    if (!XGetWMNormalHints(display, c->window, &h, &sup))
        return;
    if (h.flags & PBaseSize) { c->base_w = h.base_width; c->base_h = h.base_height; }
    else if (h.flags & PMinSize) { c->base_w = h.min_width; c->base_h = h.min_height; }
    if (h.flags & PMinSize) { c->min_w = h.min_width; c->min_h = h.min_height; }
    else if (h.flags & PBaseSize) { c->min_w = h.base_width; c->min_h = h.base_height; }
    if (h.flags & PResizeInc) { c->inc_w = h.width_inc; c->inc_h = h.height_inc; }
    if (h.flags & PMaxSize) { c->max_w = h.max_width; c->max_h = h.max_height; }
    if ((h.flags & PAspect) && h.min_aspect.x > 0 && h.min_aspect.y > 0 && h.max_aspect.x > 0 && h.max_aspect.y > 0) {
        c->min_a = (float)h.min_aspect.y / h.min_aspect.x;   /* h / w at most this (the narrowest allowed) */
        c->max_a = (float)h.max_aspect.x / h.max_aspect.y;   /* w / h at most this (the widest allowed) */
    }
    c->base_w = MAX(c->base_w, 0); c->base_h = MAX(c->base_h, 0); c->min_w = MAX(c->min_w, 0); c->min_h = MAX(c->min_h, 0);
    c->inc_w = MAX(c->inc_w, 0); c->inc_h = MAX(c->inc_h, 0); c->max_w = MAX(c->max_w, 0); c->max_h = MAX(c->max_h, 0);
    if (c->max_w && c->max_w < c->min_w) c->max_w = c->min_w;
    if (c->max_h && c->max_h < c->min_h) c->max_h = c->min_h;
    LOGP("hints 0x%lx: base %dx%d inc %dx%d min %dx%d max %dx%d aspect %.3f %.3f", c->window, c->base_w, c->base_h, c->inc_w, c->inc_h,
         c->min_w, c->min_h, c->max_w, c->max_h, c->min_a, c->max_a);
}

/* a client size made to fit the hints, never larger than asked except for the minimum: aspect (on the size minus the base when a
   base other than the minimum is given), then base + n * increment rounded down, then minimum and maximum (dwm's applysizehints) */
static void
client_hint_size(struct client *c, int *w, int *h)
{
    int cw = MAX(*w, 1), ch = MAX(*h, 1);
    bool baseismin = c->base_w == c->min_w && c->base_h == c->min_h;
    if (!baseismin) { cw -= c->base_w; ch -= c->base_h; }
    if (c->min_a > 0 && c->max_a > 0 && cw > 0 && ch > 0) {
        if (c->max_a < (float)cw / ch) cw = (int)(ch * c->max_a + 0.5f);
        else if (c->min_a < (float)ch / cw) ch = (int)(cw * c->min_a + 0.5f);
    }
    if (baseismin) { cw -= c->base_w; ch -= c->base_h; }
    cw = MAX(cw, 0); ch = MAX(ch, 0);
    if (c->inc_w > 1) cw -= cw % c->inc_w;
    if (c->inc_h > 1) ch -= ch % c->inc_h;
    cw = MAX(cw + c->base_w, c->min_w); ch = MAX(ch + c->base_h, c->min_h);
    if (c->max_w > 0) cw = MIN(cw, c->max_w);
    if (c->max_h > 0) ch = MIN(ch, c->max_h);
    *w = cw; *h = ch;
}

/* where the client window lies in a frame of fw x fh (frame-relative): inside the margins, at the hinted size. Not maximised the
   frame is fitted around that (frame_fit), so it fills the margins exactly; maximised (the frame is the work area) it may be
   smaller by the remainder, which the frame paints (draw_text: the plate, msstyles the border colour), and never larger. Fullscreen
   windows ignore the hints. */
static void
client_area(struct client *c, int fw, int fh, int *x, int *y, int *w, int *h)
{
    int l, r, t, b; margins(c, &l, &r, &t, &b);
    int cw = fw - l - r, ch = fh - t - b;
    if (!c->fullscreen) {
        int aw = cw, ah = ch;
        client_hint_size(c, &cw, &ch);
        if (c->mono) { cw = MIN(cw, MAX(aw, 1)); ch = MIN(ch, MAX(ah, 1)); }
    }
    *x = l; *y = t; *w = MAX(cw, MINIMUM_DIM); *h = MAX(ch, MINIMUM_DIM);
}

/* a frame size changed so that the client inside it gets exactly its hinted size (the frame shrinks by the remainder, or grows
   to the minimum); the caller keeps the anchored edge (drags from the left/top: the right/bottom edge) */
static void
frame_fit(struct client *c, int *fw, int *fh)
{
    if (c->fullscreen) return;
    int l, r, t, b, x, y, w, h; margins(c, &l, &r, &t, &b);
    client_area(c, *fw, *fh, &x, &y, &w, &h);
    *fw = w + l + r; *fh = h + t + b;
}

// Kill the client forcefully
// Client deletion is handled automatically with XUnmapEvent
static void
client_kill(struct client *c)
{
    XKillClient(display, c->window);
}

/* Communicate with the given Client, kindly telling it to close itself
 * and terminate any associated processes using the WM_DELETE_WINDOW protocol
 */
static void
client_close(struct client *c)
{
    XEvent ev;
    ev.type = ClientMessage;
    ev.xclient.window = c->window;
    ev.xclient.message_type = wm_atom[WMProtocols];
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = wm_atom[WMDeleteWindow];
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(display, c->window, False, NoEventMask, &ev);
    LOGN("Closing window...");
}

/* Create new "dummy" windows to be used as decorations for the given client */
static void
client_decorations_create(struct client *c)
{
    /* no background and NorthWest bit gravity: a resize neither clears the frame nor discards its pixels, so nothing but the old
       frame can show before the new one is copied in (LUXWM.md "Resize without flicker") */
    XSetWindowAttributes a = { .colormap = argb_cmap, .border_pixel = 0, .background_pixmap = None, .bit_gravity = NorthWestGravity,
        .event_mask = ExposureMask|EnterWindowMask|LeaveWindowMask|PointerMotionMask|ButtonPressMask|ButtonReleaseMask };
    c->decorated = true;
    {   /* _LHWM_DECORATED on the client (set before dec or client is mapped): picom's shadow-exclude "_LHWM_DECORATED@" then leaves the
           client without a shadow of its own; the decoration window (the ARGB plate) casts the only one */
        long one = 1;
        XChangeProperty(display, c->window, lhwm_deco_atom, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&one, 1);
    }
    c->dec = XCreateWindow(display, root, c->geom.x, c->geom.y, MAX(c->geom.width, 1), MAX(c->geom.height, 1), 0,
                           argb_depth, InputOutput, argb_visual, CWColormap|CWBorderPixel|CWEventMask
                           | CWBitGravity | CWBackPixmap, &a);
    LOGP("created decoration window 0x%lx depth %d for client 0x%lx (%dx%d at %d,%d)", c->dec, argb_depth, c->window, c->geom.width, c->geom.height, c->geom.x, c->geom.y);
    anim_mark(c, c->anim_until > 0 ? ANIM_BUSY : anim_style);   /* the new decoration window gets _LHWM_ANIMATE too */
    XGrabButton(display, 1, AnyModifier, c->dec, True, ButtonPressMask|ButtonReleaseMask|PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);
    draw_text(c, client_active(c));
    ewmh_set_frame_extents(c);
    client_set_status(c);
}

/* Destroy any "dummy" windows associated with the given Client as decorations */
static void
client_decorations_destroy(struct client *c)
{
    LOGN("Removing decorations");
    c->decorated = false;
    XDeleteProperty(display, c->window, lhwm_deco_atom);
    header_update(c, false);   /* undecorated: no header pixmap, no caption rect */
    XUnmapWindow(display, c->dec);
    XDestroyWindow(display, c->dec);
    ewmh_set_frame_extents(c);
    client_set_status(c);
}

/* Remove the given Client from the list of currently managed clients
 * Does not free the given client from memory.
 * */
static void
client_delete(struct client *c)
{
    int ws;
    ws = c->ws;

    if (ws == -1) {
        LOGN("Cannot delete client, not found");
        return;
    } else {
        LOGP("Deleting client on workspace %d", ws);
    }

    /* Delete in the stack */
    if (c_list[ws] == c) {
        c_list[ws] = c_list[ws]->next;
    } else {
        struct client *tmp = c_list[ws];
        while (tmp != NULL && tmp->next != c)
            tmp = tmp->next;

        if (tmp)
            tmp->next = tmp->next->next;
    }

    /* Delete in the focus list */
    /* I'll factor this out later */
    /* Or actually it might not be so easy... */
    if (f_list[ws] == c) {
        f_list[ws] = f_list[ws]->f_next;
    } else {
        struct client *tmp = f_list[ws];
        while (tmp != NULL && tmp->f_next != c)
            tmp = tmp->f_next;

        if (tmp)
            tmp->f_next = tmp->f_next->f_next;
    }

    if (c_list[ws] == NULL)
        f_client = NULL;

    client_set_wm_state(c, WithdrawnState);
    ewmh_set_client_list();
}

static void
monitors_free(void)
{
    free(m_list);
    m_list = NULL;
}

/* Set the given Client to be fullscreen. Moves the window to fill the dimensions
 * of the given display.
 * Updates the value of _NET_WM_STATE_FULLSCREEN to reflect fullscreen changes
 */
static void
client_fullscreen(struct client *c, bool toggle, bool fullscreen, bool max)
{
    int mon;
    bool to_fs;
    mon = ws_m_list[c->ws];
    UNUSED(max);
    // save the old geometry values so that we can toggle between fulscreen mode

    to_fs = toggle ? !c->fullscreen : fullscreen;

    /* I'm going to keep this comment here for readability
     * for the previous ternary statement
    if (toggle)
        to_fs = !c->fullscreen;
    else
        to_fs = fullscreen;
    */


    // TODO: FACTOR THIS SHIT
    if (to_fs) {
        ewmh_set_fullscreen(c, true);
        if (c->decorated && conf.fs_remove_dec) { //
            client_decorations_destroy(c);
            c->was_fs = true;
        }
        if (conf.fs_max) {
            c->prev.x = c->geom.x;
            c->prev.y = c->geom.y;
            c->prev.width = c->geom.width;
            c->prev.height = c->geom.height;
            c->fullscreen = true;   /* before the resize: fullscreen ignores the size hints (client_area) */
            client_move_absolute(c, m_list[mon].x, m_list[mon].y);
            client_resize_absolute(c, m_list[mon].width, m_list[mon].height);
        }
        c->fullscreen = true;
    } else {
        ewmh_set_fullscreen(c, false);
        if (max) {
            client_move_absolute(c, c->prev.x, c->prev.y);
            client_resize_absolute(c, c->prev.width, c->prev.height);
        }
        if (!c->decorated && conf.fs_remove_dec && c->was_fs) { //
            client_decorations_create(c);
            if (!c->unmapped) XMapWindow(display, c->dec);
            client_refresh(c);
            client_raise(c);
            client_manage_focus(c);
            ewmh_set_frame_extents(c);
        }

        c->fullscreen = false;
        c->was_fs = false;
        client_refresh(c);
    }

    docks_raise();   /* entering fullscreen with the focus: above the docks; leaving it: the docks back on top */
    ewmh_update_stacking();
    client_set_status(c);
}

/* Focus the next window in the list. Windows are sorted by the order in which they are
 * created (mapped to the window manager)
 */
static void
focus_next(struct client *c)
{
    if (c == NULL)
        return;

    int ws;
    ws = c->ws;

    if (f_list[ws] == c && f_list[ws]->f_next == NULL) {
        client_manage_focus(f_list[ws]);
        return;
    }

    struct client *tmp;
    tmp = c->f_next == NULL ? f_list[ws] : c->f_next;
    client_manage_focus(tmp);
}

/* Returns the struct client associated with the given struct Window */
static struct client*
get_client_from_window(Window w)
{
    for (int i = 0; i < WORKSPACE_NUMBER; i++) {
        for (struct client *tmp = c_list[i]; tmp != NULL; tmp = tmp->next) {
            if (tmp->window == w)
                return tmp;
            else if (tmp->decorated && tmp->dec == w)
                return tmp;
        }
    }

    return NULL;
}

/* _NET_RESTACK_WINDOW (EWMH; LUXWM.md "Restack requests"): data.l[0] source indication, l[1] sibling, l[2] detail (Above 0, Below 1,
   TopIf 2, BottomIf 3, Opposite 4). The client and its frame are restacked together (c_list order, client_raise), the focus is not
   touched: a drag source (flxp's XDND drag, CLIENT-PROTOCOL.md "Raising a window during a drag") raises the window under the pointer while
   it holds an active pointer grab, and the window being dragged from keeps the focus. Above with no sibling = raise to the top,
   Below with no sibling = lower to the bottom of the managed windows; with a managed sibling on the same desktop: directly above /
   below it. TopIf/BottomIf are taken as Above/Below (no occlusion test), Opposite as Above unless it is the top window already.
   Minimised windows and windows of other desktops are left alone, as are BELOW (always-below) windows. */
static void
client_restack(struct client *c, Window w, Window sib, long detail, long source)
{
    if (c == NULL || c->window != w || c->minimized || c->hidden || c->ws < 0) {
        LOGP("restack 0x%lx: not a visible managed client, ignored", w);
        return;
    }
    struct client *s = sib != None ? get_client_from_window(sib) : NULL;
    if (s && (s->window != sib || s->ws != c->ws || s == c)) s = NULL;
    if (sib != None && !s) { LOGP("restack 0x%lx: sibling 0x%lx is not a managed window there, ignored", w, sib); return; }
    int ws = c->ws;
    bool above = detail == Above || detail == TopIf || (detail == Opposite && c_list[ws] != c);
    if (c->below) return;
    /* take c out of the stack list, then put it back */
    if (c_list[ws] == c) c_list[ws] = c->next;
    else for (struct client *t = c_list[ws]; t; t = t->next) if (t->next == c) { t->next = c->next; break; }
    c->next = NULL;
    if (!s && above) { c->next = c_list[ws]; c_list[ws] = c; }
    else if (!s) { struct client **pp = &c_list[ws]; while (*pp) pp = &(*pp)->next; *pp = c; }
    else {
        struct client **pp = &c_list[ws];
        while (*pp && *pp != s) pp = &(*pp)->next;
        if (above) { c->next = *pp; *pp = c; }
        else if (*pp) { c->next = (*pp)->next; (*pp)->next = c; }
        else *pp = c;
    }
    /* c_list is the stacking order (top first): restack it as client_raise does, from the top window, with c in its new place */
    struct client *top = c_list[ws];
    while (top && top->below) top = top->next;
    LOGP("restack 0x%lx: %s %s0x%lx (source %ld)", w, above ? "above" : "below", s ? "" : "all, ", s ? s->window : 0UL, source);
    if (top) client_raise(top);
}

/* Redirect an XEvent from berry's client program, berryc */
static void
handle_client_message(XEvent *e)
{
    XClientMessageEvent *cme = &e->xclient;
    long cmd, *data;

    if (cme->message_type == lhwm_reload_atom && lhwm_reload_atom != None) {   /* luxwmc reload-theme (LUXWM.md "Live-editing a theme") */
        theme_reload("_LHWM_RELOAD message");
    } else if (cme->message_type == lh_settings_atom && lh_settings_atom != None) {   /* ../SETTINGS.md "Change notification" */
        LOGP("_LH_SETTINGS_CHANGED serial %ld", cme->data.l[0]);
        lhde_settings_reload();
        theme_recheck("_LH_SETTINGS_CHANGED");   /* a theme file or the current layer may have changed */
        settings_load(false);
    } else if (cme->message_type == net_berry[BerryClientEvent]) {
        LOGN("Received event from berryc");
        if (cme->format != 32) {
			LOGN("Wrong format, ignoring event");
			return;
		}
        cmd = cme->data.l[0];
        data = cme->data.l;
        ipc_handler[cmd](data);
    } else if (cme->message_type == net_atom[NetWMState]) {
        struct client* c = get_client_from_window(cme->window);
        if (c == NULL) {
            LOGN("client not found...");
            return;
        }

        /* data.l[0]: 0 remove, 1 add, 2 toggle; l[1], l[2]: the states (both MAXIMIZED_* are handled as one) */
        long act = cme->data.l[0];
        Atom p1 = (Atom)cme->data.l[1], p2 = (Atom)cme->data.l[2];
        #define WANT(cur) (act == 1 ? true : act == 0 ? false : !(cur))
        if (p1 == net_atom[NetWMStateHidden] || p2 == net_atom[NetWMStateHidden]) {
            if (WANT(c->minimized)) client_minimize(c);
            else if (c->minimized) { if (c->ws != curr_ws) switch_ws(c->ws); client_restore(c); client_manage_focus(c); }
        }
        if (p1 == net_atom[NetWMStateMaxVert] || p2 == net_atom[NetWMStateMaxVert] ||
            p1 == net_atom[NetWMStateMaxHorz] || p2 == net_atom[NetWMStateMaxHorz]) {
            if (WANT(c->mono)) client_maximize(c); else client_unmaximize(c);
        }
        if (p1 == net_atom[NetWMStateDemandsAttention] || p2 == net_atom[NetWMStateDemandsAttention]) {
            c->attention = WANT(c->attention) && !client_active(c);   /* the focused window needs no attention */
            ewmh_update_state(c);
        }
        if (p1 == net_atom[NetWMStateBelow] || p2 == net_atom[NetWMStateBelow])
            ewmh_set_below(c, WANT(c->below));
        #undef WANT
        if ((Atom)cme->data.l[1] == net_atom[NetWMStateFullscreen] ||
            (Atom)cme->data.l[2] == net_atom[NetWMStateFullscreen]) {
            LOGN("Received fullscreen request");
            if (cme->data.l[0] == 0) { // remove fullscreen
                /*ewmh_set_fullscreen(c, false);*/
                client_fullscreen(c, false, false, true);
                LOGN("type 0");
            } else if (cme->data.l[0] == 1) { // set fullscreen
                /*ewmh_set_fullscreen(c, true);*/
                client_fullscreen(c, false, true, true);
                LOGN("type 1");
            } else if (cme->data.l[0] == 2) { // toggle fullscreen
                /*ewmh_set_fullscreen(c, !c->fullscreen);*/
                client_fullscreen(c, true, true, true);
                LOGN("type 2");
            }
        }
    } else if (cme->message_type == net_atom[NetActiveWindow]) {
        /* activate: switch to its workspace, restore if minimised, raise and focus (client_manage_focus raises) */
        struct client *c = get_client_from_window(cme->window);
        if (c == NULL)
            return;
        showing_changed("activation");   /* Windows: the shown desktop is over, the other windows stay minimised */
        if (c->ws != curr_ws) switch_ws(c->ws);
        if (c->minimized) client_restore(c);
        else if (c->hidden) client_show(c);
        client_manage_focus(c);
    } else if (cme->message_type == wm_atom[WMChangeState]) {
        /* ICCCM iconify request (XIconifyWindow) */
        struct client *c = get_client_from_window(cme->window);
        if (c && cme->data.l[0] == IconicState) client_minimize(c);
    } else if (cme->message_type == net_atom[NetCloseWindow]) {
        struct client *c = get_client_from_window(cme->window);
        if (c) client_close(c);
    } else if (cme->message_type == net_atom[NetGtkShowWindowMenu]) {
        /* the window menu at a root point (data.l[1], l[2]; l[0] the device): a client's own caption, the taskbar's button of
           the window (XP's task button menu is the window's system menu; minimised windows too) */
        struct client *c = get_client_from_window(cme->window);
        if (c) wm_menu_open(c, (int)cme->data.l[1], (int)cme->data.l[2], WMMENU_FLIP, 0, false);

    } else if (cme->message_type == net_atom[NetWMDesktop]) {
        /* EWMH: a pager (luxshell, flxp) moves a window to another desktop; the current desktop stays (0xFFFFFFFF, all desktops, is
           not supported) */
        struct client *c = get_client_from_window(cme->window);
        long d = cme->data.l[0];
        if (c && d >= 0 && d < WORKSPACE_NUMBER && d != c->ws) { LOGP("_NET_WM_DESKTOP: 0x%lx to desktop %ld", c->window, d); client_send_to_ws(c, (int)d); }
    } else if (cme->message_type == net_atom[NetCurrentDesktop]) {
        if (cme->data.l[0] >= 0 && cme->data.l[0] < WORKSPACE_NUMBER)   /* a pager's or luxshell's request (lh.set_desktop) */
            switch_ws(cme->data.l[0]);
    } else if (cme->message_type == net_atom[NetRestackWindow]) {
        client_restack(get_client_from_window(cme->window), cme->window, (Window)cme->data.l[1], cme->data.l[2], cme->data.l[0]);
    } else if (cme->message_type == net_atom[NetShowingDesktop]) {
        showing_desktop(cme->data.l[0] ? 1 : 0);   /* EWMH: data.l[0] 1 = show, 0 = restore */

    } else if (cme->message_type == net_atom[NetWMMoveResize]) {
        LOGN("Handling MOVERESIZE");
        struct client *c = get_client_from_window(cme->window);
        if (c == NULL)
            return;
        data = cme->data.l;
        client_move_absolute(c, data[1], data[2]);
        client_resize_absolute(c, data[3], data[4]);
    } else if (cme->message_type == net_atom[NetWMMoveResizeDrag]) {
        /* EWMH _NET_WM_MOVERESIZE: l[0], l[1] pointer root position, l[2] direction (0..7 resize from the top-left corner clockwise,
           8 move, 9 size with the keyboard, 10 move with the keyboard, 11 cancel), l[3] button. The drag itself is luxwm's own
           caption/edge drag (drag_run), so moving a maximised window and EDGE_LOCK behave as with the frame. */
        static const int dir_hit[8] = { HIT_TL, HIT_T, HIT_TR, HIT_R, HIT_BR, HIT_B, HIT_BL, HIT_L };
        struct client *c = get_client_from_window(cme->window);
        long dir = cme->data.l[2];
        if (c == NULL || dir == 11 || c->fullscreen || c->hidden)   /* cancel outside a drag: nothing to do */
            return;
        if (c != f_client) { if (c->ws != curr_ws) switch_ws(c->ws); client_manage_focus(c); }
        if (dir == 8 || dir == 10)
            drag_run(c, HIT_CAPTION, cme->data.l[0], cme->data.l[1], (unsigned)cme->data.l[3], dir == 10);
        else if ((dir >= 0 && dir <= 7) || dir == 9) {
            if (c->mono) return;   /* no resize edges on a maximised frame */
            if (!client_fixed(c)) drag_run(c, dir == 9 ? HIT_BR : dir_hit[dir], cme->data.l[0], cme->data.l[1], (unsigned)cme->data.l[3], dir == 9);
        }
    }
}

/* one step of a window move (caption drag, Super+drag, _NET_WM_MOVERESIZE): EDGE_LOCK keeps the frame in the work area;
   client_move_absolute drops the maximised state (the frame keeps its size) */
static void
drag_move_step(struct client *c, int nx, int ny)
{
    if (conf.edge_lock)
        client_move_relative(c, nx - c->geom.x, ny - c->geom.y);
    else
        client_move_absolute(c, nx, ny);
}

static void
handle_button_press(XEvent *e)
{
    /* Much credit to the authors of dwm for
     * this function.
     */
    XButtonPressedEvent *bev = &e->xbutton;
    XEvent ev;
    struct client *c;
    int x, y, ocx, ocy, nx, ny, nw, nh, di, ocw, och;
    super_other = true;   /* a click while Super is held: no start menu on its release */
    unsigned int dui, state;
    Window dummy;
    Time current_time, last_motion;

    XQueryPointer(display, root, &dummy, &dummy, &x, &y, &di, &di, &dui);
    LOGN("Handling button press event");
    c = get_client_from_window(bev->window);
    if (c == NULL) {
        /* a click on the bare root (no desktop window over it): nothing is active any more, as on a click on the desktop
           (LUXWM.md "Focus and the desktop") */
        if (bev->window == root && f_client != NULL) {
            XSetInputFocus(display, nofocus, RevertToPointerRoot, bev->time);
            focus_set_active(false);
        }
        return;
    }
    /* compared with the real input focus: after a click on the desktop f_client is still the last focused window, but inactive */
    if (c != f_client || !client_has_focus(c)) {
        switch_ws(c->ws);
        client_manage_focus(c);
    }
    // If it's not window movement or resize then process focus on click
    state = mod_clean(bev->state);
    if (conf.focus_on_click && bev->button == (unsigned)conf.focus_button && state != (unsigned)conf.move_mask && !(conf.resize_button && state == (unsigned)conf.resize_mask) && bev->window != c->dec)
    {
        LOGN("Handling focus on click");
        // Ungrab buttons, propagate event to the window, and regrab
        window_ungrab_buttons(bev->window);
        XAllowEvents(display, ReplayPointer, CurrentTime);
        window_grab_buttons(bev->window);
        return;
    }
    if (bev->window == c->dec && c->decorated) {
        int hit = client_hit(c, bev->x, bev->y);
        /* the window menu (LUXWM.md "The window menu"): a click on the caption icon opens it under the caption (a double click there
           closes the window), a right click on the caption opens it where the button is released (WM_CONTEXTMENU on the up) */
        if (bev->button == 1 && hit == HIT_CAPTION && client_on_icon(c, bev->x, bev->y)) { wm_menu_at_caption(c, 0, 1, true); return; }
        if (bev->button == 3 && hit == HIT_CAPTION) {
            int rx = bev->x_root, ry = bev->y_root;
            if (XGrabPointer(display, c->dec, False, MOUSEMASK, GrabModeAsync, GrabModeAsync, None, None, CurrentTime) == GrabSuccess) {
                do {
                    XMaskEvent(display, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
                    if (ev.type == Expose || ev.type == MapRequest || ev.type == ConfigureRequest) event_handler[ev.type](&ev);
                    else if (ev.type == MotionNotify || ev.type == ButtonRelease) { rx = ev.xbutton.x_root; ry = ev.xbutton.y_root; }
                } while (!(ev.type == ButtonRelease && ev.xbutton.button == 3));
                XUngrabPointer(display, CurrentTime);
            }
            struct client *k = get_client_from_window(c->window);
            if (k == c) wm_menu_open(c, rx, ry, WMMENU_FLIP, 0, false);
            return;
        }
        if (bev->button != 1 && hit == HIT_CAPTION) return;
        if (hit == HIT_CLOSE || hit == HIT_MAX || hit == HIT_MIN) { frame_button_press(c, hit, bev); return; }
        if (hit >= HIT_L && hit <= HIT_BR) { frame_resize_drag(c, hit, bev); return; }
        if (hit == HIT_NONE && client_fixed(c)) return;   /* a fixed frame's border (HTBORDER) neither sizes nor moves */
        if (hit == HIT_CAPTION && bev->button == 1 && conf.last_caption_click + 350 > bev->time && conf.last_caption_win == c->dec) {
            conf.last_caption_click = 0; client_monocle(c); draw_text(c, client_active(c)); return; }
        conf.last_caption_click = bev->time; conf.last_caption_win = c->dec;
    }
    /* a move (caption, or the move modifier with its button): drag_run, which knows snapping and EDGE_LOCK */
    if ((bev->window == c->dec && bev->button == 1) || (state == (unsigned)conf.move_mask && bev->button == (unsigned)conf.move_button)) {
        drag_run(c, HIT_CAPTION, bev->x_root, bev->y_root, bev->button, false);
        return;
    }
    // Otherwise process window movement or resize
    ocx = c->geom.x;
    ocy = c->geom.y;
    ocw = c->geom.width;
    och = c->geom.height;
    last_motion = 0;
    bool moved = false;   /* the pointer left the DRAG_THRESHOLD square: a real drag (until then a click, e.g. half of a double-click) */
    if (XGrabPointer(display, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync, None, move_cursor, CurrentTime) != GrabSuccess)
        return;
    do {
        XMaskEvent(display, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
        switch (ev.type) {
            case ConfigureRequest:
            case Expose:
            case MapRequest:
                event_handler[ev.type](&ev);
                break;
            case MotionNotify:
                while (XCheckTypedEvent(display, MotionNotify, &ev)) ;   /* only the latest position (each resize step repaints the frame) */
                current_time = ev.xmotion.time;
                Time diff_time = current_time - last_motion;
                if (diff_time < (Time)conf.pointer_interval) {
                    continue;
                }
                last_motion = current_time;
                state       = mod_clean(ev.xbutton.state);
                if (!moved && abs(ev.xmotion.x - x) <= DRAG_THRESHOLD && abs(ev.xmotion.y - y) <= DRAG_THRESHOLD)
                    break;
                moved = true;
                if ((state == (unsigned)conf.move_mask && bev->button == (unsigned)conf.move_button) || ev.xbutton.state == Button1Mask) {
                    nx = ocx + (ev.xmotion.x - x);
                    ny = ocy + (ev.xmotion.y - y);
                    drag_move_step(c, nx, ny);
                } else if (conf.resize_button && state == (unsigned)conf.resize_mask && bev->button == (unsigned)conf.resize_button && !client_fixed(c)) {
                    nw = ev.xmotion.x - x;
                    nh = ev.xmotion.y - y;
                    if (conf.edge_lock)
                        client_resize_relative(c, nw - c->geom.width + ocw, nh - c->geom.height + och);
                    else
                        client_resize_absolute(c, ocw + nw, och + nh);
                }
                XFlush(display);
                break;
        }
    } while (ev.type != ButtonRelease);
    XUngrabPointer(display, CurrentTime);
}

static void
handle_expose(XEvent *e)
{
    XExposeEvent *ev = &e->xexpose;
    struct client *c;
    bool focused;

    LOGN("Handling expose event");
    c = get_client_from_window(ev->window);
    if (c == NULL) {
        LOGN("Expose event client not found, exiting");
        return;
    }
    focused = c == f_client;
    UNUSED(focused);
    /* a frame paint covers the whole window: an Expose generated before the last paint reached the server (the resize that went with
       it, typically) needs nothing; newer ones mark the frame, which is repainted once the queue is empty (paint_dirty) */
    if (ev->window == c->dec && c->decorated && (long)(ev->serial - c->paint_serial) >= 0)
        c->dirty = true;
}

/* the client whose window (client or frame) contains w, NULL for any other window; *override: w lies in an override-redirect
   top-level (a menu or tooltip) */
static struct client *
focus_owner(Window w, bool *override)
{
    *override = false;
    while (w != None && w != PointerRoot && w != root) {
        struct client *c = get_client_from_window(w);
        if (c != NULL)
            return c;
        Window r, parent, *kids = NULL; unsigned int nk;
        if (!XQueryTree(display, w, &r, &parent, &kids, &nk))
            return NULL;
        if (kids) XFree(kids);
        if (parent == root) {
            XWindowAttributes wa;
            *override = XGetWindowAttributes(display, w, &wa) && wa.override_redirect;
            return NULL;
        }
        w = parent;
    }
    return NULL;
}

/* c (or a window inside it) has the X input focus */
static bool
client_has_focus(struct client *c)
{
    Window fw; int rev; bool ovr;
    XGetInputFocus(display, &fw, &rev);
    return c != NULL && focus_owner(fw, &ovr) == c;
}

/* f_client becomes active or inactive without changing which client it is: the frame repainted, _NET_ACTIVE_WINDOW and
   _NET_WM_STATE_FOCUSED follow */
static void
focus_set_active(bool on)
{
    if (f_client == NULL) { f_active = false; return; }
    if (f_active == on)
        return;
    f_active = on;
    if (f_client->fullscreen) docks_raise();   /* above the docks only while it has the focus */
    LOGP("focus: 0x%lx %s", f_client->window, on ? "active again" : "inactive (focus outside the managed windows)");
    if (f_client->decorated) draw_text(f_client, on);
    Window a = on ? f_client->window : None;
    XChangeProperty(display, root, net_atom[NetActiveWindow], XA_WINDOW, 32, PropModeReplace, (unsigned char *)&a, 1);
    ewmh_update_state(f_client);
}

/* luxwm only sets the focus itself, but an unmanaged window can take it (flxp-desktop on a click, a dock for its menus):
   follow the real input focus (LUXWM.md "Focus and the desktop"). Focus on f_client (or inside it): active; on a window luxwm
   does not manage, the root, PointerRoot or None: inactive; on another managed client (one that focused itself) or inside
   an override-redirect window (menus, tooltips): unchanged. */
static void
focus_sync(void)
{
    Window fw; int rev; bool ovr;
    XGetInputFocus(display, &fw, &rev);
    struct client *c = focus_owner(fw, &ovr);
    if (c != NULL) {
        if (c == f_client) focus_set_active(true);
        return;
    }
    if (!ovr)
        focus_set_active(false);
}

static void
handle_focus(XEvent *e)
{
    XFocusChangeEvent *ev = &e->xfocus;
    /* keyboard grabs (menus, luxwm's own key bindings) and the PointerRoot pointer events do not move the focus */
    if (ev->mode == NotifyGrab || ev->mode == NotifyUngrab || ev->detail == NotifyPointer)
        return;
    focus_sync();
}

static void
handle_property_notify(XEvent *e)
{
    XPropertyEvent *ev = &e->xproperty;
    struct client *c;

    LOGN("Handling property notify event");
    if (ev->window == root) {
        if (ev->atom == lhwm_theme_atom && ev->state == PropertyNewValue)
            theme_property();
        else if (ev->atom == lh_theme_atom && ev->state == PropertyNewValue)
            lh_theme_property();
        else if (ev->atom == lhwm_reload_atom && ev->state == PropertyNewValue)
            theme_reload("_LHWM_RELOAD property");
        else if (ev->atom == lh_resolved_atom && lh_resolved_atom != None && ev->state == PropertyNewValue)
            lh_resolved_property();
        else if (ev->atom == lhwm_comp_atom && ev->state == PropertyDelete && running)
            { comp_written = -1; composited_publish(); }   /* someone deleted it: put it back */
        return;
    }
    if (dock_property(ev->window, ev->atom))
        return;
    c = get_client_from_window(ev->window);

    if (c == NULL)
        return;
    if (ev->atom == XA_WM_HINTS) {   /* the urgency hint sets DEMANDS_ATTENTION (cleared when the window gets focus) */
        XWMHints *h = XGetWMHints(display, c->window); Pixmap ip = None, im = None;
        if (h) { if ((h->flags & XUrgencyHint) && !client_active(c) && !c->attention) { c->attention = true; ewmh_update_state(c); }
                 if (h->flags & IconPixmapHint) ip = h->icon_pixmap;
                 if (h->flags & IconMaskHint) im = h->icon_mask;
                 XFree(h); }
        /* a new icon pixmap (the hints are rewritten for urgency too: only re-read when the pixmaps changed) */
        if (ip != c->icon_pm || im != c->icon_mask) { c->icon_pm = ip; c->icon_mask = im; client_set_icon(c); draw_text(c, client_active(c)); }
        return;
    }
    if (ev->atom == XA_WM_NORMAL_HINTS) {   /* set, changed or deleted: re-read and refit (LUXWM.md "Size hints") */
        client_read_hints(c);
        if (c->fullscreen)
            ;
        else if (c->mono && !c->hidden)
            client_apply_max(c);   /* the frame stays the work area, the client is re-rounded inside it */
        else if (!c->mono)
            client_resize_absolute(c, c->geom.width, c->geom.height);
        return;
    }
    if (ev->atom == net_atom[NetWMIcon]) {   /* set, changed or deleted: re-read (deleted: WM_HINTS, then the default icon) */
        client_set_icon(c);
        draw_text(c, client_active(c));
        return;
    }

    if (ev->atom == lhwm_ext_atom || ev->atom == lhwm_flags_atom) {   /* set or deleted: repaint the plate */
        unsigned long was = c->ext_flags;
        client_read_ext(c);
        if (((was ^ c->ext_flags) & FRAME_CLIENT_CAPTION) && c->decorated) {   /* the top margin changed: the frame stays, the client moves */
            int x, y, w, h; client_area(c, c->geom.width, c->geom.height, &x, &y, &w, &h);
            XMoveResizeWindow(display, c->window, c->geom.x + x, c->geom.y + y, (unsigned)w, (unsigned)h);
            ewmh_set_frame_extents(c);
            /* size hints: the frame is kept (a refit would round the client down at every toggle and drift); a remainder lies on
               the plate until the next resize */
        }
        draw_text(c, client_active(c));
        return;
    }

    if (ev->state == PropertyDelete)
        return;

    if (ev->atom == net_atom[NetWMName] || ev->atom == XA_WM_NAME) {
        LOGN("Updating client title");
        client_set_title(c);
        draw_text(c, client_active(c));
    }
}

static void
handle_configure_notify(XEvent *e)
{
    XConfigureEvent *ev = &e->xconfigure;

    /* SubstructureNotify delivers every child's ConfigureNotify here too; only the root's size matters */
    if (ev->window != root)
        return;
    display_width = ev->width;
    display_height = ev->height;

    LOGN("Handling configure notify event");

    monitors_free();
    monitors_setup();
    workarea_update();
}

static void
handle_configure_request(XEvent *e)
{
    struct client *c;
    XConfigureRequestEvent *ev = &e->xconfigurerequest;
    XWindowChanges wc;

    LOGN("Handling configure request event");


    wc.x = ev->x;
    wc.y = ev->y;
    wc.width = ev->width;
    wc.height = ev->height;
    wc.border_width = ev->border_width;
    wc.sibling = ev->above;
    wc.stack_mode = ev->detail;
    c = get_client_from_window(ev->window);
    LOGP("configure request 0x%lx mask 0x%lx %d,%d %dx%d", ev->window, ev->value_mask, ev->x, ev->y, ev->width, ev->height);
    if (c != NULL && c->hidden) {
        /* minimised or on another workspace: take the size, keep it off screen */
        XConfigureWindow(display, ev->window, ev->value_mask & (CWWidth|CWHeight), &wc);
        if (ev->value_mask & (CWWidth|CWHeight))
            client_resize_absolute(c, wc.width + get_dec_width(c), wc.height + get_dec_height(c));
        return;
    }
    XConfigureWindow(display, ev->window, ev->value_mask, &wc);
    if (ev->value_mask & CWStackMode) { docks_raise(); ewmh_update_stacking(); }

    if (c != NULL) {
        if (c->fullscreen)
            return;

        /* only the fields the request has: the others hold the geometry the window had when the request was made, which may be
           stale (a client asking for a size before luxwm placed it moved back to 0,0: emacs; urxvt after a font change) */
        int l, r, t, b; margins(c, &l, &r, &t, &b);
        if (ev->value_mask & (CWX|CWY))
            client_move_relative(c, (ev->value_mask & CWX) ? wc.x - l - c->geom.x : 0, (ev->value_mask & CWY) ? wc.y - t - c->geom.y : 0);
        if (ev->value_mask & (CWWidth|CWHeight))   /* the frame is fitted around the hinted size (LUXWM.md "Size hints") */
            client_resize_relative(c, (ev->value_mask & CWWidth) ? wc.width + l + r - c->geom.width : 0,
                                   (ev->value_mask & CWHeight) ? wc.height + t + b - c->geom.height : 0);
        client_refresh(c);
    } else {
        LOGN("Window for configure was not found");
    }
}

static void
handle_map_request(XEvent *e)
{
    static XWindowAttributes wa;
    XMapRequestEvent *ev = &e->xmaprequest;

    /*LOGN("Handling map request event");*/

    if (!XGetWindowAttributes(display, ev->window, &wa))
        return;
    if (wa.override_redirect)
        return;
    {   /* reparented away from the root (e.g. docked in the tray) before the map request: not ours */
        Window r, p, *kids = NULL; unsigned int nk;
        if (XQueryTree(display, ev->window, &r, &p, &kids, &nk)) {
            if (kids) XFree(kids);
            if (p != root) return;
        }
    }

    showing_changed("new window");   /* a new window ends "show desktop" (the minimised ones stay so) */
    manage_new_window(ev->window, &wa);
}

/* another client reparented a managed window away from the root (XEmbed tray icon): let go of it, leave the window alone */
static void
handle_reparent_notify(XEvent *e)
{
    XReparentEvent *ev = &e->xreparent;
    struct client *c;
    if (ev->parent == root) return;
    c = get_client_from_window(ev->window);
    if (c == NULL || c->window != ev->window) {
        dock_forget(ev->window);
        return;
    }
    LOGN("managed client reparented away from root: releasing it");
    window_ungrab_buttons(c->window);
    XAllowEvents(display, ReplayPointer, CurrentTime);
    XSelectInput(display, c->window, NoEventMask);
    client_forget(c);
}

static void
handle_unmap_notify(XEvent *e)
{
    XUnmapEvent *ev = &e->xunmap;
    struct client *c;
    c = get_client_from_window(ev->window);
    if (c != NULL && ev->window == c->dec)
        return;   /* our own decoration window */

    if (c != NULL && c->unmap_ignore > 0 && !ev->send_event) {
        /* our own unmap (minimise): StructureNotify on the client and SubstructureNotify on the root each report it, the
           root's copy comes last */
        if (ev->event == root) c->unmap_ignore--;
        return;
    }
    if (c != NULL) {
        LOGN("Client found while unmapping, focusing next client");
        client_forget(c);
    } else {
        dock_forget(ev->window);
        /* Some applications *ahem* Spotify *ahem*, don't seem to place nicely with being deleted.
         * They close slowing, causing focusing issues with unmap requests. Check to see if the current
         * workspace is empty and, if so, focus the root client so that we can pick up new key presses..
         */
        if (f_list[curr_ws] == NULL) {
            LOGN("Client not found while deleting and ws is empty, focusing root window");
            client_manage_focus(NULL);

        } else {
            LOGN("Client not found while deleting and ws is non-empty, doing nothing");
        }
    }
}

/* the client went away (unmapped by itself, withdrawn or destroyed): drop it */
static void
client_forget(struct client *c)
{
    showing_changed("a window closed");
    if (c == f_client)
        focus_after(c);
    if (c->decorated)
        client_decorations_destroy(c);
    client_delete(c);
    if (f_client == c)
        f_client = NULL;
    if (c->icon) cairo_surface_destroy((cairo_surface_t *)c->icon);
    if (c->hdr_pix) XFreePixmap(display, c->hdr_pix);
    free(c);
    client_raise(f_client);
    ewmh_update_stacking();
}

static void
handle_enter_notify(XEvent *e)
{
    XEnterWindowEvent *ev = &e->xcrossing;

    struct client *c;

    if (!conf.follow_pointer)
        return;

    c = get_client_from_window(ev->window);

    if (c != NULL && c != f_client) {
        bool warp_pointer;
        warp_pointer = conf.warp_pointer;
        conf.warp_pointer = false;
        client_manage_focus(c);
        if (c->ws != curr_ws)
            switch_ws(c->ws);
        conf.warp_pointer = warp_pointer;
    }
}

/* Hides the given Client by moving it outside of the visible display */
static void
client_hide(struct client *c)
{
    if (!c->hidden) {
        c->x_hide = c->geom.x;
        LOGN("Hiding client");
        client_move_raw(c, display_width + conf.b_width, c->geom.y);
        c->hidden = true;
    }
}

static void
ipc_move_absolute(long *d)
{
    int x, y;

    if (f_client == NULL)
        return;

    x = d[1];
    y = d[2];

    client_move_absolute(f_client, x, y);
}

static void
ipc_move_relative(long *d)
{
    int x, y;

    if (f_client == NULL)
        return;

    x = d[1];
    y = d[2];

    client_move_relative(f_client, x, y);
}

static void
ipc_monocle(long *d)
{
    UNUSED(d);
    if (f_client == NULL)
        return;

    client_monocle(f_client);
}

static void
ipc_raise(long *d)
{
    UNUSED(d);
    if (f_client == NULL)
        return;

    client_raise(f_client);
}

static void
ipc_resize_absolute(long *d)
{
    int w, h;

    if (f_client == NULL)
        return;

    w = d[1];
    h = d[2];

    client_resize_absolute(f_client, w, h);
}

static void
ipc_resize_relative(long *d)
{
    int w, h;

    if (f_client == NULL)
        return;

    w = d[1];
    h = d[2];

    client_resize_relative(f_client, w, h);
}

static void
ipc_toggle_decorations(long *d)
{
    UNUSED(d);
    if (f_client == NULL)
        return ;

    client_toggle_decorations(f_client);
}

static void
ipc_window_close(long *d)
{
    UNUSED(d);
    if (f_client == NULL)
        return;

    client_close(f_client);
}

static void
ipc_window_kill(long *d)
{
    UNUSED(d);
    if (f_client == NULL)
        return;

    client_kill(f_client);
}

static void
ipc_window_center(long *d)
{
    UNUSED(d);
    if (f_client == NULL)
        return;
    client_center(f_client);
}

static void
ipc_switch_ws(long *d)
{
    int ws = d[1];
    LOGP("IPC says switch to workspace %d", ws);
    switch_ws(ws);
}

static void
ipc_send_to_ws(long *d)
{
    if (f_client == NULL)
        return;

    int ws = d[1];
    client_send_to_ws(f_client, ws);
}

static void
ipc_fullscreen(long *d)
{
    UNUSED(d);
    if (f_client == NULL)
        return;

    client_fullscreen(f_client, true, true, true);
}

static void
ipc_fullscreen_state(long *d)
{
    UNUSED(d);

    if (f_client == NULL)
        return;

    client_fullscreen(f_client, true, true, false);
}

static void
ipc_snap_left(long *d)
{
    UNUSED(d);
    if (f_client == NULL)
        return;

    client_snap_left(f_client);
}

static void
ipc_snap_right(long *d)
{
    UNUSED(d);
    if (f_client == NULL)
        return;

    client_snap_right(f_client);
}

static void
ipc_cardinal_focus(long *d)
{
    int dir = d[1];
    client_cardinal_focus(f_client, dir);
}

static void
ipc_cycle_focus(long *d)
{
    UNUSED(d);
    if (f_client != NULL && !f_active) client_manage_focus(f_client);   /* from the desktop: the last focused window first */
    else focus_next(f_client);
}

static void
ipc_below(long *d)
{
    UNUSED(d);
    if (f_client == NULL)
        return;

    ewmh_set_below(f_client, !client_window_is_below(f_client));
}

static void
ipc_pointer_focus(long *d)
{
    UNUSED(d);
    /* Shoutout to vain for this methodology */
    int x, y, di;
    unsigned int dui;
    Window child, dummy;
    struct client *c;

    XQueryPointer(display, root, &dummy, &child, &x, &y, &di, &di, &dui);
    c = get_client_from_window(child);

    if (c != NULL)
    {
        /* Focus the client for either type of event
         * However, don't change focus if the client is already focused
         * otherwise menu's will be hidden behind the parent window
         */
        if (c != f_client) {
            switch_ws(c->ws);
            client_manage_focus(c);
        }
    }
}

static void
ipc_config(long *d)
{
    enum IPCCommand cmd = d[1];
    LOGP("Handling config command of type %ld", d[1]);
    LOGP("Data has value %ld", d[2]);

    switch (cmd) {
        case IPCFocusColor:
            conf.bf_color = d[2];
            break;
        case IPCUnfocusColor:
            conf.bu_color = d[2];
            break;
        case IPCInnerFocusColor:
            conf.if_color = d[2];
            break;
        case IPCInnerUnfocusColor:
            conf.iu_color = d[2];
            break;
        case IPCTitleFocusColor:
            load_color(&xft_focus_color, d[2]);
            break;
        case IPCTitleUnfocusColor:
            load_color(&xft_unfocus_color, d[2]);
            break;
        case IPCBorderWidth:
            conf.b_width = d[2];
            break;
        case IPCInnerBorderWidth:
            conf.i_width = d[2];
            break;
        case IPCTitleHeight:
            conf.t_height = d[2];
            break;
        case IPCEdgeGap:
            conf.top_gap = d[2];
            conf.bot_gap = d[3];
            break;
        case IPCTopGap:
            conf.top_gap = d[2];
            break;
        case IPCEdgeLock:
            conf.edge_lock = d[2];
            break;
        case IPCJSONStatus:
            conf.json_status = d[2];
            break;
        case IPCManage:
            conf.manage[(int)d[2]] = true;
            break;
        case IPCFullscreenRemoveDec:
            conf.fs_remove_dec = d[2];
            break;
        case IPCUnmanage:
            conf.manage[(int)d[2]] = false;
            break;
        case IPCQuit:
            running = false;
            break;
        case IPCDecorate:
            conf.decorate = d[2];
            break;
        case IPCDrawText:
            conf.draw_text = d[2];
            break;
        case IPCFocusButton:
            ungrab_buttons();
            conf.focus_button = (d[2] == 0) ? conf.focus_button : d[2];
            grab_buttons();
            break;
        case IPCMoveButton:
            ungrab_buttons();
            conf.move_button = (d[2] == 0) ? conf.move_button : d[2];
            grab_buttons();
            break;
        case IPCMoveMask:
            ungrab_buttons();
            conf.move_mask = (d[2] == 0) ? conf.move_mask : d[2];
            grab_buttons();
            break;
        case IPCResizeButton:
            ungrab_buttons();
            conf.resize_button = (d[2] == 0) ? conf.resize_button : d[2];
            grab_buttons();
            break;
        case IPCResizeMask:
            ungrab_buttons();
            conf.resize_mask = (d[2] == 0) ? conf.resize_mask : d[2];
            grab_buttons();
            break;
        case IPCPointerInterval:
            conf.pointer_interval = d[2];
            break;
        case IPCFocusFollowsPointer:
            conf.follow_pointer = d[2];
            break;
        case IPCFocusOnClick:
            ungrab_buttons();
            conf.focus_on_click = d[2];
            grab_buttons();
            break;
        case IPCWarpPointer:
            conf.warp_pointer = d[2];
            break;
        case IPCSmartPlace:
            conf.smart_place = d[2];
        default:
            break;
    }

    refresh_config();
}

static void
ipc_edge_gap(long *d)
{
    int top, bot, left, right;
    top = d[1];
    bot = d[2];
    left = d[3];
    right = d[4];

    conf.top_gap = top;
    conf.bot_gap = bot;
    conf.left_gap = left;
    conf.right_gap = right;

    LOGN("Changing edge gap...");

    refresh_config();
}

static void
ipc_save_monitor(long *d)
{
    int ws, mon;
    ws = d[1];
    mon = d[2];

    if (mon >= m_count) {
        LOGN("Cannot save monitor, number is too high");
        return;
    }

    LOGP("Saving ws %d to monitor %d", ws, mon);

    /* Associate the given workspace to the given monitor */
    ws_m_list[ws] = mon;
    ewmh_set_viewport();
}

static void
ipc_set_font(long *d)
{
    UNUSED(d);
    XTextProperty font_prop;
    char** font_list;
    int err, n;
    LOGN("Handling new set_font request");

    font_list = NULL;
    LOGN("Getting text property");
    XGetTextProperty(display, root, &font_prop, net_berry[BerryFontProperty]);
    LOGN("Converting to text list");
    err = XmbTextPropertyToTextList(display, &font_prop, &font_list, &n);
    strncpy(global_font, font_list[0], sizeof(global_font));
    LOGN("Opening font by name");
    font = XftFontOpenName(display, screen, global_font);
    if (font == NULL) {
        LOGN("Error, could not open font name");
        return;
    }
    refresh_config();
    if (err >= Success && n > 0 && *font_list)
        XFreeStringList(font_list);
    XFree(font_prop.value);
}

static void
load_color(XftColor *dest_color, unsigned long raw_color)
{
    XColor x_color;
    x_color.pixel = raw_color;
    XQueryColor(display, DefaultColormap(display, screen), &x_color);
    r_color.blue = x_color.blue;
    r_color.green = x_color.green;
    r_color.red = x_color.red;
    r_color.alpha = DEFAULT_ALPHA;

    XftColorFree(display, DefaultVisual(display, screen), DefaultColormap(display, screen), dest_color);
    XftColorAllocValue(display, DefaultVisual(display, screen), DefaultColormap(display, screen),
            &r_color, dest_color);
}


static void
load_config(char *conf_path)
{
    if (fork() == 0) {
        setsid();
        execl("/bin/sh", "sh", conf_path, NULL);
        LOGP("CONFIG PATH: %s", conf_path);
    }
}

static void
client_manage_focus(struct client *c)
{
    struct client *old = f_client;
    if (c != NULL && c->minimized)
        client_restore(c);   /* focusing a minimised window (Alt+Tab, activation) restores it */
    if (c != NULL && f_client != NULL) {
        client_set_color(f_client, conf.iu_color, conf.bu_color);
        draw_text(f_client, false);
        manage_xsend_icccm(c, wm_atom[WMTakeFocus]);
    }

    if (c != NULL) {
        f_active = true;
        client_move_to_front(c);
        client_set_color(c, conf.if_color, conf.bf_color);
        draw_text(c, true);
        client_raise(c);
        client_set_input(c);
        if (conf.warp_pointer)
            warp_pointer(c);
        ewmh_set_focus(c);
        if (c->fullscreen || (old != NULL && old != c && old->fullscreen)) docks_raise();   /* a focused fullscreen window is above the docks */
        c->attention = false;   /* DEMANDS_ATTENTION ends with focus */
        if (old != c) ewmh_update_state(old);
        ewmh_update_state(c);
        manage_xsend_icccm(c, wm_atom[WMTakeFocus]);
        if (c->ws != curr_ws)
            switch_ws(c->ws);
    } else { //client is null, might happen when switching to a new workspace
             // without any active clients
        LOGN("Giving focus to dummy window");
        f_client = NULL;
        f_active = false;
        if (old != NULL && old->fullscreen) docks_raise();
        XSetInputFocus(display, nofocus, RevertToPointerRoot, CurrentTime);
        if (net_atom[NetActiveWindow]) {   /* no active window: _NET_ACTIVE_WINDOW = None */
            Window none = None;
            XChangeProperty(display, root, net_atom[NetActiveWindow], XA_WINDOW, 32, PropModeReplace, (unsigned char *)&none, 1);
        }
        ewmh_update_state(old);
    }
}

static void
manage_new_window(Window w, XWindowAttributes *wa)
{
    /* Credits to vain for XGWP checking */
    Atom prop, da;
    unsigned char *prop_ret = NULL;
    int di;
    unsigned long dl;
    if (XGetWindowProperty(display, w, net_atom[NetWMWindowType], 0,
                sizeof (Atom), False, XA_ATOM, &da, &di, &dl, &dl,
                &prop_ret) == Success) {
        if (prop_ret) {
            prop = ((Atom *)prop_ret)[0];
            if ((prop == net_atom[NetWMWindowTypeDock]    && !conf.manage[Dock])    ||
                (prop == net_atom[NetWMWindowTypeToolbar] && !conf.manage[Toolbar]) ||
                (prop == net_atom[NetWMWindowTypeUtility] && !conf.manage[Utility]) ||
                (prop == net_atom[NetWMWindowTypeDialog]  && !conf.manage[Dialog])  ||
                (prop == net_atom[NetWMWindowTypeMenu]    && !conf.manage[Menu])    ||
                prop == net_atom[NetWMWindowTypeDesktop]) {   /* the shell's desktop layer: never managed, kept at the bottom */
                XMapWindow(display, w);
                LOGN("Window is of type dock, toolbar, utility, menu, or splash: not managing");
                LOGN("Mapping new window, not managed");
                XFree(prop_ret);
                dock_track(w);   /* struts (and keeping docks above) */
                return;
            }
        }
    }

    // Make sure we aren't trying to map the same window twice
    for (int i = 0; i < WORKSPACE_NUMBER; i++) {
        for (struct client *tmp = c_list[i]; tmp; tmp = tmp->next) {
            if (tmp->window == w) {
                LOGN("Error, window already mapped. Not mapping.");
                return;
            }
        }
    }

    // Get class information for the current window
    XClassHint ch;
    if (XGetClassHint(display, w, &ch) > Success) {
        LOGP("client has class %s", ch.res_class);
        LOGP("client has name %s", ch.res_name);
        if (ch.res_class)
            XFree(ch.res_class);
        if (ch.res_name)
            XFree(ch.res_name);
    } else {
        LOGN("could not retrieve client class name");
    }

    struct client *c;
    c = malloc(sizeof(struct client));
    if (c == NULL) {
        LOGN("Error, malloc could not allocated new window");
        return;
    }
    /* cooperating clients paint their own frame: _MOTIF_WM_HINTS with decorations off, or _LHWM_SELF_FRAMED */
    bool decorate = conf.decorate;
    {
        Atom mwm = XInternAtom(display, "_MOTIF_WM_HINTS", False), self = XInternAtom(display, "_LHWM_SELF_FRAMED", False), ty; int fmt; unsigned long n, after; unsigned char *d = NULL;
        if (XGetWindowProperty(display, w, mwm, 0, 5, False, mwm, &ty, &fmt, &n, &after, &d) == Success && d) {
            long *h = (long *)d; if (n >= 3 && (h[0] & 2) && h[2] == 0) decorate = false; XFree(d); d = NULL; }
        if (XGetWindowProperty(display, w, self, 0, 1, False, AnyPropertyType, &ty, &fmt, &n, &after, &d) == Success && d) { if (n) decorate = false; XFree(d); }
    }
    memset(c, 0, sizeof *c);
    c->window = w;
    c->dec = None;
    c->ws = curr_ws;
    c->decorated = decorate;
    c->hover = c->pressed = 0;
    /* property changes from here on: a client answering what luxwm publishes during the setup below (_LHWM_CAPTION_RECT ->
       a new _LHWM_EXTEND_FRAME) must not slip through before the full event mask is selected after mapping */
    XSelectInput(display, w, PropertyChangeMask);
    client_read_ext(c);
    client_read_hints(c);   /* WM_NORMAL_HINTS: the size is fitted by the client_refresh below */
    { int l, r, t, b; margins(c, &l, &r, &t, &b);
      if (!adopting && c->decorated) wm_theme_margins(&theme, &l, &r, &t, &b);   /* new: normal margins, CLIENT_CAPTION then grows the client up into the caption */
      c->geom.x = wa->x;
      c->geom.y = wa->y;
      c->geom.width = wa->width + l + r;
      c->geom.height = wa->height + t + b; }
    c->hidden = false;
    c->fullscreen = false;
    c->mono = false;
    c->was_fs = false;
    c->seq = next_seq++;
    c->depth = wa->depth;
    c->visual = wa->visual;
    bool want_max = false, want_fs = false, want_min = false;
    {   /* states the client asked for before mapping */
        Atom ty; int fmt; unsigned long n, after; unsigned char *d = NULL;
        if (XGetWindowProperty(display, w, net_atom[NetWMState], 0, 64, False, XA_ATOM, &ty, &fmt, &n, &after, &d) == Success && d) {
            Atom *a = (Atom *)d; int mv = 0, mh = 0;
            for (unsigned long i = 0; i < n; i++) {
                if (a[i] == net_atom[NetWMStateBelow]) c->below = true;
                else if (a[i] == net_atom[NetWMStateDemandsAttention]) c->attention = true;
                else if (a[i] == net_atom[NetWMStateMaxVert]) mv = 1;
                else if (a[i] == net_atom[NetWMStateMaxHorz]) mh = 1;
                else if (a[i] == net_atom[NetWMStateFullscreen]) want_fs = true;
                else if (a[i] == net_atom[NetWMStateHidden] && adopting) want_min = true;   /* minimised before a WM restart */
            }
            want_max = mv && mh;
            XFree(d);
        }
    }

    XSetWindowBorderWidth(display, c->window, 0);

    if (decorate)
        client_decorations_create(c);

    client_set_title(c);
    client_set_icon(c);
    client_refresh(c); /* using our current factoring, w/h are set incorrectly */
    client_save(c, curr_ws);
    client_initial_place(c, wa->x, wa->y);
    client_set_wm_state(c, NormalState);
    ewmh_set_desktop(c, c->ws);
    ewmh_set_client_list();

    anim_mark(c, anim_style);
    if (want_min) {   /* adopted while minimised: stays unmapped (unmapped now if the previous WM had only moved it away) */
        c->minimized = true; c->hidden = true; c->x_hide = c->geom.x; c->unmapped = true;
        if (wa->map_state == IsViewable) { c->unmap_ignore++; XUnmapWindow(display, c->window); }
        XSelectInput(display, c->window, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
    } else {
        if (decorate)
            XMapWindow(display, c->dec);
        XMapWindow(display, c->window);
        XSelectInput(display, c->window, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
    }
    window_grab_buttons(c->window);
    if (want_fs)
        client_fullscreen(c, false, true, true);
    else if (want_max) {
        long g[4]; bool have = false;
        Atom ty; int fmt; unsigned long n, after; unsigned char *d = NULL;
        if (adopting && XGetWindowProperty(display, w, XInternAtom(display, "_LHWM_RESTORE_GEOMETRY", False), 0, 4, False, XA_CARDINAL, &ty, &fmt, &n, &after, &d) == Success && d) {
            if (n == 4) { memcpy(g, d, sizeof g); have = true; }
            XFree(d);
        }
        client_maximize(c);
        if (have) {
            c->prev.x = g[0]; c->prev.y = g[1]; c->prev.width = g[2]; c->prev.height = g[3];
            XChangeProperty(display, w, XInternAtom(display, "_LHWM_RESTORE_GEOMETRY", False), XA_CARDINAL, 32, PropModeReplace, (unsigned char *)g, 4);
        }
    }
    if (want_min) {
        client_set_wm_state(c, IconicState);
        ewmh_update_state(c);
        client_set_status(c);
        ewmh_update_stacking();
        return;
    }
    client_manage_focus(c);
    ewmh_update_state(c);
    ewmh_update_stacking();
}

static int
manage_xsend_icccm(struct client *c, Atom atom)
{
    /* This is from a dwm patch by Brendan MacDonell:
     * http://lists.suckless.org/dev/1104/7548.html */

    int n;
    Atom *protocols;
    int exists = 0;
    XEvent ev;

    if (XGetWMProtocols(display, c->window, &protocols, &n)) {
        while (!exists && n--)
            exists = protocols[n] == atom;
        XFree(protocols);
    }

    if (exists) {
        ev.type = ClientMessage;
        ev.xclient.window = c->window;
        ev.xclient.message_type = wm_atom[WMProtocols];
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = atom;
        ev.xclient.data.l[1] = CurrentTime;
        XSendEvent(display, c->window, False, NoEventMask, &ev);
    }

    return exists;
}

static void
window_grab_buttons(Window window)
{
    XGrabButton(display, conf.move_button, conf.move_mask, window, True, ButtonPressMask|ButtonReleaseMask|PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);
    if (conf.resize_button)   /* the modifier resize drag is off by default (config.h RESIZE_BUTTON) */
        XGrabButton(display, conf.resize_button, conf.resize_mask, window, True, ButtonPressMask|ButtonReleaseMask|PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);
    if (conf.focus_on_click)
        XGrabButton(display, conf.focus_button, AnyModifier, window, True, ButtonPressMask, GrabModeSync, GrabModeSync, None, None);
}

static void
window_ungrab_buttons(Window window)
{
    XUngrabButton(display, conf.move_button, conf.move_mask, window);
    if (conf.resize_button)
        XUngrabButton(display, conf.resize_button, conf.resize_mask, window);
    if (conf.focus_on_click)
        XUngrabButton(display, conf.focus_button, AnyModifier, window);
}

static void
grab_buttons(void)
{
    for (int i = 0; i < WORKSPACE_NUMBER; i++)
        for (struct client *tmp = c_list[i]; tmp != NULL; tmp = tmp->next) {
            window_grab_buttons(tmp->window);
        }
}

static void
ungrab_buttons(void)
{
    for (int i = 0; i < WORKSPACE_NUMBER; i++)
        for (struct client *tmp = c_list[i]; tmp != NULL; tmp = tmp->next) {
            window_ungrab_buttons(tmp->window);
        }
}

/* a maximised window that gets moved or resized stops being maximised; its frame margins (msstyles: borders back) change */
static void
client_unmax_margins(struct client *c)
{
    if (!c->mono) return;
    c->mono = false;
    c->xfade_t0 = 0;    /* a drag ends a maximise crossfade: the normal plate at once */
    maxmode_update();   /* _LHWM_MAXIMIZED_MODE before the geometry (see max_transition) */
    ewmh_update_state(c);   /* and _NET_WM_STATE before the _LHWM_CAPTION_RECT of the next paint */
    if (!c->decorated) return;
    int x, y, w, h; client_area(c, c->geom.width, c->geom.height, &x, &y, &w, &h);
    XMoveResizeWindow(display, c->window, c->geom.x + x, c->geom.y + y, (unsigned)w, (unsigned)h);
    ewmh_set_frame_extents(c);
}

static void
client_move_absolute(struct client *c, int x, int y)
{
    int dest_x = x;
    int dest_y = y;

    client_unmax_margins(c);

    if (c->decorated) {
        int l, r, t, b; margins(c, &l, &r, &t, &b);
        dest_x = x + l;
        dest_y = y + t;
    }

    /* move relative to where decorations should go */
    XMoveWindow(display, c->window, dest_x, dest_y);
    if (c->decorated){
        XMoveWindow(display, c->dec, x, y);
    }

    c->geom.x = x;
    c->geom.y = y;

    if (c->mono)
        c->mono = false;

    client_set_status(c);
}

static void
client_move_relative(struct client *c, int x, int y)
{
    if (x == 0 && y == 0 && c->mono)
        return;   /* client_refresh: a no-op move must not drop the maximised state */
    /* Constrain the current client to the w/h of display */
    /* God this is soooo ugly */
    if (conf.edge_lock) {
        /* keep the frame inside the work area (monitor minus struts and gaps); the top-left edge wins when it does not fit */
        int dx, dy, ax, ay, aw, ah;
        workarea_mon(ws_m_list[c->ws], &ax, &ay, &aw, &ah);
        dx = c->geom.x + x;
        dy = c->geom.y + y;
        if (dx + c->geom.width > ax + aw) dx = ax + aw - c->geom.width;
        if (dx < ax) dx = ax;
        if (dy + c->geom.height > ay + ah) dy = ay + ah - c->geom.height;
        if (dy < ay) dy = ay;
        client_move_absolute(c, dx, dy);
    } else {
        client_move_absolute(c, c->geom.x + x, c->geom.y + y);
    }
}

static bool
client_window_is_below(struct client *c)
{
    /* kept as a flag (read from _NET_WM_STATE at map time, changed by IPC / client messages) */
    return c->below;
}

static void
client_move_to_front(struct client *c)
{
    int ws;
    ws = c->ws;

    /* If we didn't find the client */
    if (ws == -1)
        return;


    /* If the Client is set to be always below */
    if (client_window_is_below(c))
        return;

    /* If the Client is at the front of the list, ignore command */
    if (c_list[ws] == c || c_list[ws]->next == NULL)
        return;

    struct client *tmp;
    for (tmp = c_list[ws]; tmp->next != NULL; tmp = tmp->next)
        if (tmp->next == c)
            break;

    if (tmp && tmp->next)
        tmp->next = tmp->next->next; /* remove the Client from the list */
    c->next = c_list[ws]; /* add the client to the front of the list */
    c_list[ws] = c;
}

static void
client_monocle(struct client *c)
{
    if (c->mono)
        client_unmaximize(c);
    else
        client_maximize(c);
}

/* the start of a maximise (to_max) or restore, before any geometry changes: _LHWM_MAXIMIZED_MODE is written first, so the shell
   can show (or drop) its black layer at the moment the window starts to grow (or shrink); then the animation: none with a compositor
   (Longhorn 4074 and 5048 resize instantly), except with the animation style ANIM_INVENTED picom's geometry animation (_LHWM_ANIMATE = ANIM_BUSY on
   client and frame for 400 ms, picom-glass.conf); without a compositor XP's caption flight from the old to the new rectangle
   (minmax.c: xxxMinMaximize animates SW_MAXIMIZE/SW_RESTORE with IDANI_CAPTION too) */
static void
max_transition(struct client *c, bool to_max, int x, int y, int w, int h)
{
    bool was = c->mono;
    if (compositor && !c->hidden)
        XGrabServer(display);   /* until client_geom_max has the new geometry painted: the shell sees the mode with the geometry */
    c->mono = to_max;
    maxmode_update();
    c->mono = was;
    if (!ANIMATE || c->hidden || c->ws != curr_ws)
        return;
    if (compositor && c->decorated && theme.mode == THEME_DWM && theme.plate_m[0]) {
        /* the frame jumps to the new rectangle and crossfades its plate there (xfade_tick, 400 ms); picom's geometry animation is
           not used for it. A fade still running is reversed from where it is. */
        if (MAX_FADE_MS <= 0) { c->xfade_t0 = 0; return; }   /* config.h: no fade, the plate switches in the geometry change's own paint */
        double w0 = xfade_weight(c), now = now_ms();
        double p = w0 < 0 ? 0 : to_max ? w0 : 1 - w0;
        /* a fresh fade starts one 25 ms tick in, as lh.animate does (its first frame is applied at once at t = 25 ms), so frame and bars
           show the same weight from the first composited frame on; client_geom_max moves the start past its grab */
        if (w0 < 0) p = XFADE_TICK / XFADE_MS;
        c->xfade_to_max = to_max; c->xfade_t0 = now - p * XFADE_MS; c->xfade_next = now + XFADE_TICK;
        return;
    }
    if (compositor) {
        if (anim_style == ANIM_INVENTED) {   /* only there: picom's 250 ms geometry animation (not with ANIM_4074PLUS) */
            anim_mark(c, ANIM_BUSY);
            c->anim_until = now_ms() + 400;
        }
    } else if (anim_style != ANIM_NONE && (x != c->geom.x || y != c->geom.y || w != c->geom.width || h != c->geom.height))
        anim_classic(c, c->geom.x, c->geom.y, c->geom.width, c->geom.height, x, y, w, h);
}

/* the geometry change of a maximise or restore in one step: frame and client go straight to their final rectangles (with the final
   margins: to_max is the state they are for) in one batch, and the frame is painted once, there. With a compositor the batch and the
   paint run under a server grab, so picom cannot compose a frame in between (done as steps, a move at the old size, a resize, a
   paint, the client re-placed and a second paint, picom on the lab showed windows missing or at the old size for 60..80 ms) */
static void
client_geom_max(struct client *c, int x, int y, int w, int h, bool to_max)
{
    double t_in = now_ms();
    if (compositor) XGrabServer(display);   /* max_transition took it already (a grab does not nest: this is a no-op then) */
    c->mono = to_max;
    w = MAX(w, MINIMUM_DIM); h = MAX(h, MINIMUM_DIM);
    if (!to_max) frame_fit(c, &w, &h);   /* maximised the frame stays the work area and the client may be smaller (size hints) */
    c->geom.x = x; c->geom.y = y; c->geom.width = w; c->geom.height = h;
    ewmh_update_state(c);   /* _NET_WM_STATE before the _LHWM_CAPTION_RECT that draw_text republishes (a CLIENT_CAPTION client relayouts on it) */
    int cx, cy, cw, ch; client_area(c, w, h, &cx, &cy, &cw, &ch);
    if (c->decorated) {
        XMoveResizeWindow(display, c->dec, x, y, w, h);
        XMoveResizeWindow(display, c->window, x + cx, y + cy, (unsigned)cw, (unsigned)ch);
        ewmh_set_frame_extents(c);
        draw_text(c, client_active(c));
    } else
        XMoveResizeWindow(display, c->window, x, y, (unsigned)cw, (unsigned)ch);
    if (compositor) XUngrabServer(display);
    XFlush(display);
    if (c->xfade_t0 > 0) {   /* the crossfade's clock starts when the new geometry (and the mode) reach picom and the shell */
        double now = now_ms();
        c->xfade_t0 += now - t_in; c->xfade_next = now + XFADE_TICK;
    }
    client_set_status(c);
}

/* maximise into the work area of the client's monitor (struts excluded); c->prev keeps the restore geometry */
static void
client_apply_max(struct client *c)
{
    int x, y, w, h;
    workarea_mon(ws_m_list[c->ws], &x, &y, &w, &h);
    client_geom_max(c, x, y, w, h, true);
}

/* the restore geometry of a maximised window (c->prev); it survives a WM restart (read back when adopting) */
static void
client_set_restore(struct client *c, int x, int y, int w, int h)
{
    c->prev.x = x; c->prev.y = y; c->prev.width = w; c->prev.height = h;
    long g[4] = { x, y, w, h };
    XChangeProperty(display, c->window, XInternAtom(display, "_LHWM_RESTORE_GEOMETRY", False), XA_CARDINAL, 32, PropModeReplace, (unsigned char *)g, 4);
}

static void
client_maximize(struct client *c)
{
    if (c->mono || c->fullscreen || client_fixed(c))
        return;
    client_set_restore(c, c->hidden ? c->x_hide : c->geom.x, c->geom.y, c->geom.width, c->geom.height);
    if (c->hidden) {   /* minimised: applied by client_show */
        c->mono = true;
        ewmh_update_state(c);
        return;
    }
    {
        int x, y, w, h;
        workarea_mon(ws_m_list[c->ws], &x, &y, &w, &h);
        max_transition(c, true, x, y, w, h);
    }
    client_apply_max(c);
}

static void
client_unmaximize(struct client *c)
{
    if (!c->mono)
        return;
    c->mono = false;   /* before moving: the frame margins depend on it (msstyles themes hide the borders when maximised) */
    ewmh_update_state(c);   /* _NET_WM_STATE before the _LHWM_CAPTION_RECT that the resize below republishes */
    if (!c->hidden)
        max_transition(c, false, c->prev.x, c->prev.y, c->prev.width, c->prev.height);
    if (c->hidden) {
        c->x_hide = c->prev.x;
        c->geom.y = c->prev.y;
        client_resize_absolute(c, c->prev.width, c->prev.height);
        if (c->decorated) { ewmh_set_frame_extents(c); draw_text(c, client_active(c)); }
    } else
        client_geom_max(c, c->prev.x, c->prev.y, c->prev.width, c->prev.height, false);
}

/* berry's smart placement on a PLACE_RES grid, over the work area (struts and gaps excluded): the lowest free
   gap that fits, scanning bottom-up, centred in it; otherwise centred in the work area */
static void
client_place(struct client *c)
{
    int ax, ay, aw, ah, width, height, need_w, need_h;

    workarea_mon(ws_m_list[c->ws], &ax, &ay, &aw, &ah);
    width = aw / PLACE_RES;
    height = ah / PLACE_RES;
    need_w = (c->geom.width + PLACE_RES - 1) / PLACE_RES;
    need_h = (c->geom.height + PLACE_RES - 1) / PLACE_RES;

    // If this is the first window in the workspace, we can simply center
    // it. Also center it if the user wants to disable smart placement.
    if (f_list[curr_ws]->next == NULL || !conf.smart_place || width < 1 || height < 1) {
        client_center(c);
        return;
    }

    uint16_t opt[height][width];
    for (int i = 0; i < height; i++)
        for (int j = 0; j < width; j++)
            opt[i][j] = 1;

    /* occupied cells: every visible window on the workspace (frames, rounded outwards) */
    for (struct client *tmp = f_list[curr_ws]; tmp != NULL; tmp = tmp->f_next) {
        if (tmp == c || tmp->hidden)
            continue;
        int i0 = (tmp->geom.y - ay) / PLACE_RES, i1 = (tmp->geom.y + tmp->geom.height - ay + PLACE_RES - 1) / PLACE_RES;
        int j0 = (tmp->geom.x - ax) / PLACE_RES, j1 = (tmp->geom.x + tmp->geom.width - ax + PLACE_RES - 1) / PLACE_RES;
        for (int i = MAX(i0, 0); i < MIN(i1, height); i++)
            for (int j = MAX(j0, 0); j < MIN(j1, width); j++)
                opt[i][j] = 0;
    }

    // opt[i][j] = free cells in column j ending at row i
    for (int i = 1; i < height; i++)
        for (int j = 0; j < width; j++)
            opt[i][j] = opt[i][j] == 0 ? 0 : opt[i-1][j] + 1;

    for (int i = height - 1; i >= need_h - 1; i--) {
        int count = 0, max_height = INT_MAX;
        for (int j = 0; j <= width; j++) {
            if (j < width && opt[i][j] >= need_h) {
                max_height = MIN(max_height, opt[i][j]);
                count++;
                if (count >= need_w) {   // the window WILL fit here
                    int x0 = j - count + 1;
                    int place_x = ax + ceil10(x0 * PLACE_RES + (count * PLACE_RES - c->geom.width) / 2);
                    int place_y = ay + ceil10((i - max_height + 1) * PLACE_RES + (max_height * PLACE_RES - c->geom.height) / 2);
                    client_move_absolute(c, place_x, place_y);
                    return;
                }
            } else {
                count = 0;
                max_height = INT_MAX;
            }
        }
    }
    // Center the client if there is no space for it
    client_center(c);
}

static void
client_raise(struct client *c)
{
    if (c != NULL) {

        /* If the Client is set to be always below */
        if (client_window_is_below(c))
            return;

        if (!c->decorated) {
            XRaiseWindow(display, c->window);
            docks_raise();
            ewmh_update_stacking();
        } else {
            // how may active clients are there on our workspace
            int count, i;
            count = 0;
            for (struct client *tmp = c_list[c->ws]; tmp != NULL; tmp = tmp->next) {
                count++;
            }

            if (count == 0)
                return;

            Window wins[count*2];

            i = 0;
            for (struct client *tmp = c_list[c->ws]; tmp != NULL; tmp = tmp->next) {
                wins[i] = tmp->window;
                wins[i+1] = tmp->dec;
                i += 2;
            }
            XRestackWindows(display, wins, count*2);
            docks_raise();   /* dock windows (taskbar) stay above the clients */
            ewmh_update_stacking();
        }
    }
}

static void monitors_setup(void)
{
    XineramaScreenInfo *m_info;
    int n;

    if (!XineramaIsActive(display)) {
        LOGN("Xinerama not active, cannot read monitors");
        return;
    }

    m_info = XineramaQueryScreens(display, &n);
    if (m_info == NULL) {
        LOGN("Xinerama could not query screens");
        return;
    }
    LOGP("Found %d screens active", n);
    m_count = n;

    /* First, we need to decide which monitors are unique.
     * Non-unique monitors can become a problem when displays
     * are mirrored. They will share the same information (which something
     * like xrandr will handle for us) but will have the exact same information.
     * We want to avoid creating duplicate structs for the same monitor if we dont
     * need to
     */

    // TODO: Add support for repeated displays

    m_list = malloc(sizeof(struct monitor) * n);

    for (int i = 0; i < n; i++) {
        m_list[i].screen = m_info[i].screen_number;
        m_list[i].width = m_info[i].width;
        m_list[i].height = m_info[i].height;
        m_list[i].x = m_info[i].x_org;
        m_list[i].y = m_info[i].y_org;
        LOGP("Screen #%d with dim: x=%d y=%d w=%d h=%d",
                m_list[i].screen, m_list[i].x, m_list[i].y, m_list[i].width, m_list[i].height);
    }

    ewmh_set_viewport();
}

static void
client_refresh(struct client *c)
{
    LOGN("Refreshing client");
    for (int i = 0; i < 2; i++) {
        client_move_relative(c, 0, 0);
        client_resize_relative(c, 0, 0);
    }
}

static void
refresh_config(void)
{
    for (int i = 0; i < WORKSPACE_NUMBER; i++) {
        for (struct client *tmp = c_list[i]; tmp != NULL; tmp = tmp->next) {
            /* We run into this annoying issue when where we have to
             * re-create these windows since the border_width has changed.
             * We end up destroying and recreating this windows, but this
             * causes them to be redrawn on the wrong screen, regardless of
             * their current desktop. The easiest way around this is to move
             * them all to the current desktop and then back again */
            if (tmp->decorated && conf.decorate) {
                client_decorations_destroy(tmp);
                client_decorations_create(tmp);
                if (!tmp->unmapped) XMapWindow(display, tmp->dec);
            }

            client_refresh(tmp);
            if (!tmp->minimized)
                client_show(tmp);

            if (f_client != tmp)
                client_set_color(tmp, conf.iu_color, conf.bu_color);
            else
                client_set_color(tmp, conf.if_color, conf.bf_color);

            if (i != curr_ws || tmp->minimized) {
                client_hide(tmp);
            } else {
                client_show(tmp);
                client_raise(tmp);
            }
        }
    }
}

/* a geometry change of client and decoration window, sent by wm_frame_paint_ex3 between the offscreen paint and the copy into the
   window (LUXWM.md "Resize without flicker"); move: x, y are the frame's position, else only the sizes change */
struct geom_req { struct client *c; bool move; int x, y, cw, ch, dw, dh; };
struct drag_cl;
static struct drag_cl *geom_probe;   /* drag_bg_probe's samples to take around the next geom_apply (under its server grab) */
static void drag_bg_sample(struct client *c, struct drag_cl *d, bool after);

static void
geom_apply(void *arg)
{
    struct geom_req *g = arg; struct client *c = g->c;
    struct drag_cl *pd = geom_probe; geom_probe = NULL;
    if (pd) drag_bg_sample(c, pd, false);
    if (g->move) {
        int l, r, t, b; margins(c, &l, &r, &t, &b);
        XMoveResizeWindow(display, c->window, g->x + l, g->y + t, (unsigned)g->cw, (unsigned)g->ch);
        XMoveResizeWindow(display, c->dec, g->x, g->y, (unsigned)g->dw, (unsigned)g->dh);
    } else {
        XResizeWindow(display, c->window, (unsigned)g->cw, (unsigned)g->ch);
        XResizeWindow(display, c->dec, (unsigned)g->dw, (unsigned)g->dh);
    }
    if (pd) drag_bg_sample(c, pd, true);
}

/* c->geom already holds the new geometry: paint the frame for it, the windows follow in the same batch as the pixels */
static void
geom_paint(struct client *c, struct geom_req *g)
{
    wm_frame_pre_upload = geom_apply; wm_frame_pre_upload_arg = g;
    draw_text(c, client_active(c));
    if (wm_frame_pre_upload) { wm_frame_pre_upload = NULL; geom_apply(g); }   /* nothing was painted */
}

static void
client_resize_absolute(struct client *c, int w, int h)
{
    client_unmax_margins(c);
    /* size hints: the frame is fitted around the client's hinted size (LUXWM.md "Size hints"); width and height are the frame's */
    w = MAX(w, MINIMUM_DIM); h = MAX(h, MINIMUM_DIM);
    frame_fit(c, &w, &h);
    int cx, cy, dw, dh; client_area(c, w, h, &cx, &cy, &dw, &dh);

    /*LOGN("Resizing client main window");*/
    if (c->decorated) {
        /* the frame is painted at the new size first; the resize of both windows goes out together with its pixels (wm_frame_pre_upload) */
        struct geom_req g = { c, false, 0, 0, dw, dh, w, h };
        c->geom.width = w; c->geom.height = h;
        geom_paint(c, &g);
    } else
        XResizeWindow(display, c->window, (unsigned)dw, (unsigned)dh);

    c->geom.width = w;
    c->geom.height = h;
    if (c->mono)
        c->mono = false;
    if (c->decorated) ewmh_set_frame_extents(c);
    client_set_status(c);
}

static void
client_resize_relative(struct client *c, int w, int h)
{
    if (w == 0 && h == 0 && c->mono)
        return;
    if (conf.edge_lock) {
        /* cap the size at the right and bottom edges of the work area */
        int dw, dh, ax, ay, aw, ah;
        workarea_mon(ws_m_list[c->ws], &ax, &ay, &aw, &ah);
        dw = c->geom.width + w;
        dh = c->geom.height + h;
        if (c->geom.x + dw > ax + aw) dw = ax + aw - c->geom.x;
        if (c->geom.y + dh > ay + ah) dh = ay + ah - c->geom.y;
        client_resize_absolute(c, dw, dh);
    } else {
        client_resize_absolute(c, c->geom.width + w, c->geom.height + h);
    }
}

static void
run(void)
{
    XEvent e;
    adopting = true;
    for (int i = 0; i < adopt_count && i < 256; i++) {
        XWindowAttributes wa;
        if (XGetWindowAttributes(display, adopt_pending[i], &wa)) { LOGP("adopting existing window 0x%lx", adopt_pending[i]); manage_new_window(adopt_pending[i], &wa); }
    }
    adopting = false;
    if (adopt_count) {   /* WM restart: put back the previous stacking order (bottom to top) and the active window */
        for (int i = 0; i < adopt_nstack; i++) {
            struct client *c = get_client_from_window(adopt_stack[i]);
            if (c && c->window == adopt_stack[i] && c->ws == curr_ws) client_move_to_front(c);
        }
        struct client *a = adopt_active != None ? get_client_from_window(adopt_active) : NULL;
        if (a && a->window == adopt_active && !a->minimized) client_manage_focus(a);
        else if (c_list[curr_ws]) client_raise(c_list[curr_ws]);
    }
    maxmode_update();
    XSync(display, false);
    while (running) {
        if (!XPending(display)) {
            if (workarea_dirty) { workarea_dirty = false; workarea_update(); }
            paint_dirty();
            int to = anim_expire();   /* _LHWM_ANIMATE back to anim_style after a geometry animation */
            int tf = xfade_tick();     /* the next frame of a maximise/restore crossfade */
            if (tf >= 0 && (to < 0 || tf < to)) to = tf;
            if (to >= 0) {
                struct pollfd pfd = { .fd = ConnectionNumber(display), .events = POLLIN };
                XFlush(display);
                if (poll(&pfd, 1, to) == 0 && !XPending(display))
                    continue;
            }
        }
        XNextEvent(display, &e);
        LOGP("Received new %d event", e.type);
        if (xfixes_ev >= 0 && e.type == xfixes_ev + XFixesSelectionNotify) {
            compositor_check();
        } else if (e.type < LASTEvent && event_handler[e.type]) {
            LOGP("Handling %d event", e.type);
            event_handler[e.type](&e);
        }
        maxmode_update();   /* cheap; writes _LHWM_MAXIMIZED_MODE / _LHWM_MAXIMIZED only on change */
    }
}

static void
client_save(struct client *c, int ws)
{
    /* Save the client to the "stack" of managed clients */
    c->next = c_list[ws];
    c_list[ws] = c;

    /* Save the client o the list of focusing order */
    c->f_next = f_list[ws];
    f_list[ws] = c;

    ewmh_set_client_list();
}

/* This method will return true if it is safe to show a client on the given workspace
 * based on the currently focused workspaces on each monitor.
 */
static bool
safe_to_focus(int ws)
{
    int mon = ws_m_list[ws];

    if (m_count == 1)
        return false;

    for (int i = 0; i < WORKSPACE_NUMBER; i++)
        if (i != ws && ws_m_list[i] == mon && c_list[i] != NULL && c_list[i]->hidden == false)
            return false;

    LOGN("Workspace is safe to focus");
    return true;
}

static void
client_send_to_ws(struct client *c, int ws)
{
    int prev, mon_next, mon_prev, x_off, y_off;
    mon_next = ws_m_list[ws];
    mon_prev = ws_m_list[c->ws];
    client_delete(c);
    prev = c->ws;
    c->ws = ws;
    client_save(c, ws);
    focus_next(f_list[prev]);

    x_off = c->geom.x - m_list[mon_prev].x;
    y_off = c->geom.y - m_list[mon_prev].y;
    client_move_absolute(c, m_list[mon_next].x + x_off, m_list[mon_next].y + y_off);

    if (safe_to_focus(ws) && !c->minimized)
        client_show(c);
    else
        client_hide(c);

    ewmh_set_desktop(c, ws);
}

static void
client_set_color(struct client *c, unsigned long i_color, unsigned long b_color)
{
    UNUSED(i_color); UNUSED(b_color);
    if (c->decorated)
        draw_text(c, client_active(c));
}


static void
client_set_input(struct client *c)
{
    XSetInputFocus(display, c->window, RevertToPointerRoot, CurrentTime);
}

static void
client_set_icon(struct client *c)
{
    Atom ty; int fmt; unsigned long n, after; unsigned char *d = NULL;
    int want = theme.mode == THEME_MSSTYLES ? 16 : theme.icon_size;   /* msstyles: SM_CXSMICON */
    if (c->icon) { cairo_surface_destroy((cairo_surface_t *)c->icon); c->icon = NULL; }
    /* _NET_WM_ICON first (the whole array: 1L << 24 longs; a 256 px icon set alone is 87k), then WM_HINTS icon_pixmap/icon_mask
       (xterm, xclock, Xt/Motif/FOX programs: they have no _NET_WM_ICON). Without either the frame shows the theme's default icon */
    if (XGetWindowProperty(display, c->window, net_atom[NetWMIcon], 0, 1L << 24, False, XA_CARDINAL, &ty, &fmt, &n, &after, &d) == Success && d) {
        if (fmt == 32 && n > 2) c->icon = wm_frame_icon_from_netwm((unsigned long *)d, n, want);
        XFree(d);
    }
    XWMHints *h = XGetWMHints(display, c->window);
    c->icon_pm = c->icon_mask = None;
    if (h) {
        if (h->flags & IconPixmapHint) c->icon_pm = h->icon_pixmap;
        if (h->flags & IconMaskHint) c->icon_mask = h->icon_mask;
        if (!c->icon && c->icon_pm) c->icon = wm_frame_icon_from_pixmap(display, c->icon_pm, c->icon_mask, want);
        XFree(h);
    }
    if (getenv("LHWM_DEBUG")) fprintf(stderr, "luxwm: icon 0x%lx: %s\n", c->window, c->icon ? "client" : app_icon(want) ? "application" : wm_theme_default_icon(&theme, want) ? "theme default" : "none");
}

/* the generic application icon (shell32 #3, user32's IDI_APPLICATION art) from an imported build's pack, 16 and 32 px: what a
   window without an icon of its own shows in Windows' caption (urxvt, xterm and most X programs have none) */
static cairo_surface_t *
app_icon(int want)
{
    static cairo_surface_t *ic[2]; static bool tried;
    if (!tried) {
        tried = true;
        static const char *builds[] = { "4074", "4051", "5112", "4042" };
        for (size_t b = 0; b < sizeof builds / sizeof builds[0] && !ic[0] && !ic[1]; b++)
            for (int k = 0; k < 2; k++) {
                char rel[64], q[4200]; snprintf(rel, sizeof rel, "packs/%s/shell32/%d/3.png", builds[b], k ? 32 : 16);
                if (!lhde_data_find(rel, q, sizeof q)) continue;
                cairo_surface_t *s = cairo_image_surface_create_from_png(q);
                if (cairo_surface_status(s) == CAIRO_STATUS_SUCCESS) ic[k] = s; else cairo_surface_destroy(s);
            }
    }
    return want > 16 && ic[1] ? ic[1] : ic[0] ? ic[0] : ic[1];
}

/* what the caption shows: the client's icon, else the generic application icon, else the theme's default icon (DWM
   DEFAULTICON / the XP application icon of the classic frame set) */
static cairo_surface_t *
client_icon(struct client *c)
{
    if (c->icon) return (cairo_surface_t *)c->icon;
    int want = theme.mode == THEME_MSSTYLES ? 16 : theme.icon_size;
    cairo_surface_t *a = app_icon(want);
    return a ? a : wm_theme_default_icon(&theme, want);
}

static void
client_set_title(struct client *c)
{
    XTextProperty tp;
    char **slist = NULL;
    int count;

    c->title[0] = 0;
    if (!XGetTextProperty(display, c->window, &tp, net_atom[NetWMName]) && !XGetTextProperty(display, c->window, &tp, XA_WM_NAME)) {
        LOGN("Could not read client title, not updating");
        return;
    }

    if (tp.encoding == XA_STRING) {
        strncpy(c->title, (char *)tp.value, sizeof(c->title) - 1);
    } else {
        if (XmbTextPropertyToTextList(display, &tp, &slist, &count) >= Success && count > 0 && *slist) {
            strncpy(c->title, slist[0], sizeof(c->title) - 1);
            XFreeStringList(slist);
        }
    }

    c->title[sizeof c->title - 1] = 0;
    XFree(tp.value);
}


static void
setup(void)
{
    unsigned long data[1], data2[1];
    int mon;
    XSetWindowAttributes wa = { .override_redirect = true };
    // Setup our conf initially
    conf.b_width          = BORDER_WIDTH;
    conf.t_height         = TITLE_HEIGHT;
    conf.i_width          = INTERNAL_BORDER_WIDTH;
    conf.bf_color         = BORDER_FOCUS_COLOR;
    conf.bu_color         = BORDER_UNFOCUS_COLOR;
    conf.if_color         = INNER_FOCUS_COLOR;
    conf.iu_color         = INNER_UNFOCUS_COLOR;
    conf.m_step           = MOVE_STEP;
    conf.r_step           = RESIZE_STEP;
    conf.focus_new        = FOCUS_NEW;
    conf.edge_lock        = EDGE_LOCK;
    conf.t_center         = TITLE_CENTER;
    conf.top_gap          = TOP_GAP;
    conf.bot_gap          = BOT_GAP;
    conf.smart_place      = SMART_PLACE;
    conf.draw_text        = DRAW_TEXT;
    conf.json_status      = JSON_STATUS;
    conf.manage[Dock]     = MANAGE_DOCK;
    conf.manage[Dialog]   = MANAGE_DIALOG;
    conf.manage[Toolbar]  = MANAGE_TOOLBAR;
    conf.manage[Menu]     = MANAGE_MENU;
    conf.manage[Splash]   = MANAGE_SPLASH;
    conf.manage[Utility]  = MANAGE_UTILITY;
    conf.decorate         = DECORATE_NEW;
    conf.focus_button     = FOCUS_BUTTON;
    conf.move_button      = MOVE_BUTTON;
    conf.move_mask        = MOVE_MASK;
    conf.resize_button    = RESIZE_BUTTON;
    conf.resize_mask      = RESIZE_MASK;
    conf.fs_remove_dec    = FULLSCREEN_REMOVE_DEC;
    conf.fs_max           = FULLSCREEN_MAX;
    conf.pointer_interval = POINTER_INTERVAL;
    conf.follow_pointer   = FOLLOW_POINTER;
    conf.focus_on_click   = FOCUS_ON_CLICK;
    conf.warp_pointer     = WARP_POINTER;

    root = DefaultRootWindow(display);
    screen = DefaultScreen(display);
    display_height = DisplayHeight(display, screen); /* Display height/width still needed for hiding clients */
    display_width = DisplayWidth(display, screen);
    move_cursor = XCreateFontCursor(display, XC_fleur);
    normal_cursor = XCreateFontCursor(display, XC_left_ptr);
    edge_cursor[HIT_L] = XCreateFontCursor(display, XC_left_side);   edge_cursor[HIT_R] = XCreateFontCursor(display, XC_right_side);
    edge_cursor[HIT_T] = XCreateFontCursor(display, XC_top_side);    edge_cursor[HIT_B] = XCreateFontCursor(display, XC_bottom_side);
    edge_cursor[HIT_TL] = XCreateFontCursor(display, XC_top_left_corner); edge_cursor[HIT_TR] = XCreateFontCursor(display, XC_top_right_corner);
    edge_cursor[HIT_BL] = XCreateFontCursor(display, XC_bottom_left_corner); edge_cursor[HIT_BR] = XCreateFontCursor(display, XC_bottom_right_corner);
    {
        XVisualInfo vi;
        if (XMatchVisualInfo(display, screen, 32, TrueColor, &vi)) { argb_visual = vi.visual; argb_depth = 32; }
        else { argb_visual = DefaultVisual(display, screen); argb_depth = DefaultDepth(display, screen); LOGN("no ARGB visual, frames will be opaque"); }
        argb_cmap = XCreateColormap(display, root, argb_visual, AllocNone);
        /* -t / LHWM_THEME: a theme name, an alias or a theme directory; only used when the session has no _LH_THEME yet */
        const char *td = theme_opt ? theme_opt : getenv("LHWM_THEME"); if (!td || !*td) td = "aero-patch";
        /* LHWM_THEME_SOURCE=dir: the theme directories' PNG caches even with libuxtheme; lib: the styles only (LUXWM.md "libuxtheme
           source"); default: the style through the library when it is built in and the theme names one */
        const char *tsrc = getenv("LHWM_THEME_SOURCE");
        wm_frame_theme_source = tsrc && !strcmp(tsrc, "dir") ? FRAME_SRC_DIR : tsrc && !strcmp(tsrc, "lib") ? FRAME_SRC_LIB : FRAME_SRC_AUTO;
        fprintf(stderr, "luxwm: libuxtheme %s\n", !wm_frame_uxtheme_available() ? "not built in: theme directories only" :
                wm_frame_theme_source == FRAME_SRC_DIR ? "built in, LHWM_THEME_SOURCE=dir: theme directories only" : "built in: visual styles are read live");
        snprintf(theme_req, sizeof theme_req, "%s", td);
        /* DWM themes need a compositor (_NET_WM_CM_S<screen> owned); XFixes reports when one comes or goes */
        char cm[32]; snprintf(cm, sizeof cm, "_NET_WM_CM_S%d", screen);
        cm_atom = XInternAtom(display, cm, False);
        lhwm_theme_atom = XInternAtom(display, "_LHWM_THEME", False);
        lhwm_maxmode_atom = XInternAtom(display, "_LHWM_MAXIMIZED_MODE", False);
        lhwm_max_atom = XInternAtom(display, "_LHWM_MAXIMIZED", False);
        lhwm_ext_atom = XInternAtom(display, "_LHWM_EXTEND_FRAME", False);
        lhwm_deco_atom = XInternAtom(display, "_LHWM_DECORATED", False);
        lhwm_anim_atom = XInternAtom(display, "_LHWM_ANIMATE", False);
        lh_settings_atom = XInternAtom(display, "_LH_SETTINGS_CHANGED", False);
        settings_load(true);
        net_icon_geom_atom = XInternAtom(display, "_NET_WM_ICON_GEOMETRY", False);
        lhwm_flags_atom = XInternAtom(display, "_LHWM_FRAME_FLAGS", False);
        lhwm_hdrpix_atom = XInternAtom(display, "_LHWM_HEADER_PIXMAP", False);
        lhwm_caprect_atom = XInternAtom(display, "_LHWM_CAPTION_RECT", False);
        lhwm_comp_atom = XInternAtom(display, "_LHWM_COMPOSITED", False);
        lhwm_desklayer_atom = XInternAtom(display, "_LHWM_DESKTOP_LAYER", False);
        lhwm_metrics_atom = XInternAtom(display, "_LHWM_FRAME_METRICS", False);
        lhwm_reload_atom = XInternAtom(display, "_LHWM_RELOAD", False);
        int fx_err;
        if (XFixesQueryExtension(display, &xfixes_ev, &fx_err))
            XFixesSelectSelectionInput(display, root, cm_atom, XFixesSetSelectionOwnerNotifyMask |
                                       XFixesSelectionWindowDestroyNotifyMask | XFixesSelectionClientCloseNotifyMask);
        else { xfixes_ev = -1; fprintf(stderr, "luxwm: no XFixes, compositor changes are not followed\n"); }
        compositor = XGetSelectionOwner(display, cm_atom) != None;
        lh_theme_atom = XInternAtom(display, "_LH_THEME", False);
        lh_resolved_atom = XInternAtom(display, "_LH_THEME_RESOLVED", False);
        lh_serial_atom = XInternAtom(display, "_LH_SETTINGS_SERIAL", False);
        utf8string = XInternAtom(display, "UTF8_STRING", False);
        {   /* the session theme (_LH_THEME, published by whoever starts first) wins over -t / LHWM_THEME */
            char cur[256], canon[256];
            bool have = root_string(lh_theme_atom, cur, sizeof cur);
            if (have && lh_theme_apply(cur, canon, sizeof canon)) {
                snprintf(lh_theme_cur, sizeof lh_theme_cur, "%s", canon);
                if (strcmp(cur, canon)) XChangeProperty(display, root, lh_theme_atom, utf8string, 8, PropModeReplace, (unsigned char *)canon, strlen(canon));
            } else {
                if (have) fprintf(stderr, "luxwm: _LH_THEME %s is no theme, using %s\n", cur, td);
                if (lh_theme_apply(td, canon, sizeof canon)) {
                    snprintf(lh_theme_cur, sizeof lh_theme_cur, "%s", canon);
                    if (!have) XChangeProperty(display, root, lh_theme_atom, utf8string, 8, PropModeReplace, (unsigned char *)canon, strlen(canon));
                }
            }
        }
        theme_select();
        settings_load(false);   /* the theme's animations */
    }
    XDefineCursor(display, root, normal_cursor);

    XSelectInput(display, root,
            StructureNotifyMask|SubstructureRedirectMask|SubstructureNotifyMask|ButtonPressMask|Button1Mask|PropertyChangeMask|FocusChangeMask);
    xerrorxlib = XSetErrorHandler(xerror);

    check = XCreateSimpleWindow(display, root, 0, 0, 1, 1, 0, 0, 0);
    nofocus = XCreateSimpleWindow(display, root, -10, -10, 1, 1, 0, 0, 0);
    XChangeWindowAttributes(display, nofocus, CWOverrideRedirect, &wa);
    XMapWindow(display, nofocus);
    client_manage_focus(NULL);

    /* ewmh supported atoms */
    utf8string                       = XInternAtom(display, "UTF8_STRING", False);
    net_atom[NetSupported]           = XInternAtom(display, "_NET_SUPPORTED", False);
    net_atom[NetGtkShowWindowMenu]   = XInternAtom(display, "_GTK_SHOW_WINDOW_MENU", False);
    net_atom[NetNumberOfDesktops]    = XInternAtom(display, "_NET_NUMBER_OF_DESKTOPS", False);
    net_atom[NetActiveWindow]        = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
    net_atom[NetWMStateFullscreen]   = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);
    net_atom[NetWMStateBelow]        = XInternAtom(display, "_NET_WM_STATE_BELOW", False);
    net_atom[NetWMMoveResize]        = XInternAtom(display, "_NET_MOVERESIZE_WINDOW", False);
    net_atom[NetWMMoveResizeDrag]    = XInternAtom(display, "_NET_WM_MOVERESIZE", False);
    net_atom[NetWMSyncRequest]       = XInternAtom(display, "_NET_WM_SYNC_REQUEST", False);
    net_atom[NetWMSyncRequestCounter] = XInternAtom(display, "_NET_WM_SYNC_REQUEST_COUNTER", False);
    {   int sev, serr, smaj, smin;
        if (XSyncQueryExtension(display, &sev, &serr) && XSyncInitialize(display, &smaj, &smin)) sync_ev = sev; }
    net_atom[NetShowingDesktop]      = XInternAtom(display, "_NET_SHOWING_DESKTOP", False);
    net_atom[NetRestackWindow]       = XInternAtom(display, "_NET_RESTACK_WINDOW", False);
    net_atom[NetWMCheck]             = XInternAtom(display, "_NET_SUPPORTING_WM_CHECK", False);
    net_atom[NetCurrentDesktop]      = XInternAtom(display, "_NET_CURRENT_DESKTOP", False);
    net_atom[NetWMState]             = XInternAtom(display, "_NET_WM_STATE", False);
    net_atom[NetWMName]              = XInternAtom(display, "_NET_WM_NAME", False);
    net_atom[NetClientList]          = XInternAtom(display, "_NET_CLIENT_LIST", False);
    net_atom[NetWMWindowType]        = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    net_atom[NetWMWindowTypeDock]    = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DOCK", False);
    net_atom[NetWMWindowTypeToolbar] = XInternAtom(display, "_NET_WM_WINDOW_TYPE_TOOLBAR", False);
    net_atom[NetWMWindowTypeMenu]    = XInternAtom(display, "_NET_WM_WINDOW_TYPE_MENU", False);
    net_atom[NetWMWindowTypeSplash]  = XInternAtom(display, "_NET_WM_WINDOW_TYPE_SPLASH", False);
    net_atom[NetWMWindowTypeDialog]  = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    net_atom[NetWMWindowTypeUtility] = XInternAtom(display, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    net_atom[NetWMDesktop]           = XInternAtom(display, "_NET_WM_DESKTOP", False);
    net_atom[NetWMFrameExtents]      = XInternAtom(display, "_NET_FRAME_EXTENTS", False);
    net_atom[NetDesktopNames]        = XInternAtom(display, "_NET_DESKTOP_NAMES", False);
    net_atom[NetDesktopViewport]     = XInternAtom(display, "_NET_DESKTOP_VIEWPORT", False);
    net_atom[NetCloseWindow]         = XInternAtom(display, "_NET_CLOSE_WINDOW", False);
    net_atom[NetClientListStacking]  = XInternAtom(display, "_NET_CLIENT_LIST_STACKING", False);
    net_atom[NetWMStateHidden]       = XInternAtom(display, "_NET_WM_STATE_HIDDEN", False);
    net_atom[NetWMIcon]              = XInternAtom(display, "_NET_WM_ICON", False);
    net_atom[NetWMStateMaxVert]      = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    net_atom[NetWMStateMaxHorz]      = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    net_atom[NetWMStateDemandsAttention] = XInternAtom(display, "_NET_WM_STATE_DEMANDS_ATTENTION", False);
    net_atom[NetWMStateFocused]      = XInternAtom(display, "_NET_WM_STATE_FOCUSED", False);
    net_atom[NetWMStrut]             = XInternAtom(display, "_NET_WM_STRUT", False);
    net_atom[NetWMStrutPartial]      = XInternAtom(display, "_NET_WM_STRUT_PARTIAL", False);
    net_atom[NetWorkarea]            = XInternAtom(display, "_NET_WORKAREA", False);
    net_atom[NetWMWindowTypeDesktop] = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);

    /* Some icccm atoms */
    wm_atom[WMDeleteWindow]          = XInternAtom(display, "WM_DELETE_WINDOW", False);
    wm_atom[WMTakeFocus]             = XInternAtom(display, "WM_TAKE_FOCUS", False);
    wm_atom[WMProtocols]             = XInternAtom(display, "WM_PROTOCOLS", False);
    wm_atom[WMState]                 = XInternAtom(display, "WM_STATE", False);
    wm_atom[WMChangeState]           = XInternAtom(display, "WM_CHANGE_STATE", False);

    /* Internal berry atoms */
    net_berry[BerryWindowStatus]     = XInternAtom(display, "BERRY_WINDOW_STATUS", False);
    net_berry[BerryClientEvent]      = XInternAtom(display, "BERRY_CLIENT_EVENT", False);
    net_berry[BerryFontProperty]     = XInternAtom(display, "BERRY_FONT_PROPERTY", False);

    LOGN("Successfully assigned atoms");

    XChangeProperty(display , check , net_atom[NetWMCheck]   , XA_WINDOW  , 32 , PropModeReplace , (unsigned char *) &check              , 1);
    XChangeProperty(display , check , net_atom[NetWMName]    , utf8string , 8  , PropModeReplace , (unsigned char *) __WINDOW_MANAGER_NAME__ , 5);
    XChangeProperty(display , root  , net_atom[NetWMCheck]   , XA_WINDOW  , 32 , PropModeReplace , (unsigned char *) &check              , 1);
    XChangeProperty(display , root  , net_atom[NetSupported] , XA_ATOM    , 32 , PropModeReplace , (unsigned char *) net_atom            , NetLast);
    { long off = 0; XChangeProperty(display, root, net_atom[NetShowingDesktop], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&off, 1); }

    LOGN("Successfully set initial properties");

    /* Set the total number of desktops */
    data[0] = WORKSPACE_NUMBER;
    XChangeProperty(display, root, net_atom[NetNumberOfDesktops], XA_CARDINAL, 32, PropModeReplace, (unsigned char *) data, 1);

    /* Set the initial "current desktop" to 0 */
    data2[0] = curr_ws;
    XChangeProperty(display, root, net_atom[NetCurrentDesktop], XA_CARDINAL, 32, PropModeReplace, (unsigned char *) data2, 1);
    LOGN("Setting up monitors");
    monitors_setup();
    LOGN("Successfully setup monitors");
    workarea_update();
    mon = ws_m_list[curr_ws];
    XWarpPointer(display, None, root, 0, 0, 0, 0,
        m_list[mon].x + m_list[mon].width / 2,
        m_list[mon].y + m_list[mon].height / 2);

    gc = XCreateGC(display, root, 0, 0);
    keys_setup();

    /* adopt windows that already exist (restart, or started after clients) */
    {
        Window rr, pr, *kids = NULL; unsigned int nk = 0;
        if (XQueryTree(display, root, &rr, &pr, &kids, &nk) && kids) {
            for (unsigned int i = 0; i < nk; i++) {
                XWindowAttributes kwa;
                if (kids[i] == check || kids[i] == nofocus) continue;
                if (!XGetWindowAttributes(display, kids[i], &kwa) || kwa.override_redirect || kwa.class == InputOnly) continue;
                if (kwa.map_state != IsViewable) {   /* minimised under a previous luxwm: unmapped, WM_STATE Iconic */
                    Atom ty; int fmt; unsigned long n = 0, after; unsigned char *d = NULL; bool iconic = false;
                    if (XGetWindowProperty(display, kids[i], wm_atom[WMState], 0, 2, False, wm_atom[WMState], &ty, &fmt, &n, &after, &d) == Success && d) {
                        iconic = n >= 1 && ((unsigned long *)d)[0] == IconicState; XFree(d); }
                    if (!iconic) continue;
                }
                if (adopt_count < 256) adopt_pending[adopt_count++] = kids[i];
            }
            XFree(kids);
        }
        Atom ty; int fmt; unsigned long n = 0, after; unsigned char *d = NULL;
        if (XGetWindowProperty(display, root, net_atom[NetActiveWindow], 0, 1, False, XA_WINDOW, &ty, &fmt, &n, &after, &d) == Success && d) {
            if (n == 1) adopt_active = ((Window *)d)[0];
            XFree(d); d = NULL;
        }
        if (XGetWindowProperty(display, root, net_atom[NetClientListStacking], 0, 256, False, XA_WINDOW, &ty, &fmt, &n, &after, &d) == Success && d) {
            for (unsigned long k = 0; k < n && k < 256; k++) adopt_stack[adopt_nstack++] = ((Window *)d)[k];
            XFree(d); d = NULL;
        }
        n = 0;
        if (XGetWindowProperty(display, root, net_atom[NetClientList], 0, 1024, False, XA_WINDOW, &ty, &fmt, &n, &after, &d) == Success && d) {
            Window *old = (Window *)d;
            int rank[256];
            for (int i = 0; i < adopt_count; i++) {
                rank[i] = (int)n + i;   /* not listed before: after the listed ones, in stacking order */
                for (unsigned long k = 0; k < n; k++) if (old[k] == adopt_pending[i]) { rank[i] = (int)k; break; }
            }
            for (int i = 1; i < adopt_count; i++)
                for (int j = i; j > 0 && rank[j - 1] > rank[j]; j--) {
                    int tr = rank[j]; rank[j] = rank[j - 1]; rank[j - 1] = tr;
                    Window tw = adopt_pending[j]; adopt_pending[j] = adopt_pending[j - 1]; adopt_pending[j - 1] = tw;
                }
            XFree(d);
        }
    }

    LOGN("Allocating color values");
    XftColorAllocName(display, DefaultVisual(display, screen), DefaultColormap(display, screen),
            TEXT_FOCUS_COLOR, &xft_focus_color);
    XftColorAllocName(display, DefaultVisual(display, screen), DefaultColormap(display, screen),
            TEXT_UNFOCUS_COLOR, &xft_unfocus_color);

    font = XftFontOpenName(display, screen, global_font);
    ewmh_set_desktop_names();
}

static void
client_show(struct client *c)
{
    if (c->hidden) {
        LOGN("Showing client");
        client_move_raw(c, c->x_hide, c->geom.y);
        c->hidden = false;
        if (c->unmapped) {   /* minimised: mapped again where it was (picom's "show" animation) */
            c->unmapped = false;
            if (c->decorated && c->dec != None) XMapWindow(display, c->dec);
            XMapWindow(display, c->window);
        }
        if (c->mono)
            client_apply_max(c);   /* the work area may have changed while it was hidden */
        client_raise(c);
    }
}

/* ---- snap (Windows 7's Aero Snap, docs/LUXWM.md "Snap"): a window put on the left or right half of the work area, or stretched to
   its full height, remembering the rectangle it had before (c->snap_prev), which a drag away restores ---- */

/* the rectangle of a snap kind on c's work area (SNAP_VERT keeps c's columns) */
static void
snap_rect(struct client *c, int kind, int *x, int *y, int *w, int *h)
{
    int ax, ay, aw, ah; workarea_mon(ws_m_list[c->ws], &ax, &ay, &aw, &ah);
    *y = ay; *h = ah;
    switch (kind) {
    case SNAP_LEFT:  *x = ax; *w = aw / 2; break;
    case SNAP_RIGHT: *x = ax + aw / 2; *w = aw - aw / 2; break;
    case SNAP_VERT:  *x = c->geom.x; *w = c->geom.width; break;
    default:         *x = ax; *w = aw; break;
    }
}

/* snap c (SNAP_LEFT, SNAP_RIGHT, SNAP_VERT, or SNAP_MAX = maximise); rest: the rectangle to come back to (a window that is snapped
   already keeps its first one) */
static void
client_snap_to(struct client *c, int kind, struct client_geom rest)
{
    if (c->snap) rest = c->snap_prev;
    if (kind == SNAP_MAX) {
        c->snap = SNAP_NONE;
        client_maximize(c);
        client_set_restore(c, rest.x, rest.y, rest.width, rest.height);
        draw_text(c, client_active(c));
        return;
    }
    if (c->mono) client_unmax_margins(c);
    int x, y, w, h; snap_rect(c, kind, &x, &y, &w, &h);
    c->snap_prev = rest; c->snap = kind;
    client_move_absolute(c, x, y);
    client_resize_absolute(c, w, h);
    if (kind == SNAP_RIGHT && c->geom.width != w) client_move_absolute(c, x + w - c->geom.width, y);   /* size hints made it narrower */
    ewmh_update_state(c);
    LOGP("snap 0x%lx: %s, back to %dx%d+%d+%d", c->window, kind == SNAP_LEFT ? "left" : kind == SNAP_RIGHT ? "right" : "vertical",
         rest.width, rest.height, rest.x, rest.y);
}

static void
client_snap_left(struct client *c)
{
    client_snap_to(c, SNAP_LEFT, c->geom);
}

static void
client_snap_right(struct client *c)
{
    client_snap_to(c, SNAP_RIGHT, c->geom);
}

/* the snap preview: an outline of the rectangle a drop would give, 4 px of XP's drag-rectangle checker (luxwm's choice; 7 draws a
   glass sheet) */
static Window snap_win;
static Pixmap snap_pat;
static void
snap_preview(int x, int y, int w, int h)
{
    if (w <= 0) { if (snap_win) XUnmapWindow(display, snap_win); return; }
    if (!snap_win) {
        static const char bits[] = { 0x01, 0x02 };   /* 2x2 checker */
        snap_pat = XCreatePixmapFromBitmapData(display, root, (char *)bits, 2, 2, BlackPixel(display, screen), WhitePixel(display, screen),
                                               (unsigned)DefaultDepth(display, screen));
        XSetWindowAttributes a = { .override_redirect = True, .background_pixmap = snap_pat, .save_under = True };
        snap_win = XCreateWindow(display, root, 0, 0, 1, 1, 0, CopyFromParent, InputOutput, CopyFromParent,
                                 CWOverrideRedirect | CWBackPixmap | CWSaveUnder, &a);
        XClassHint ch = { (char *)"lhwm-snap", (char *)"lhwm" }; XSetClassHint(display, snap_win, &ch);
        XStoreName(display, snap_win, "lhwm snap preview");
    }
    const int t = 4;
    XRectangle r[4] = { { 0, 0, (unsigned short)w, t }, { 0, (short)(h - t), (unsigned short)w, t },
                        { 0, t, t, (unsigned short)MAX(h - 2 * t, 0) }, { (short)(w - t), t, t, (unsigned short)MAX(h - 2 * t, 0) } };
    XMoveResizeWindow(display, snap_win, x, y, (unsigned)w, (unsigned)h);
    XShapeCombineRectangles(display, snap_win, ShapeBounding, 0, 0, r, 4, ShapeSet, Unsorted);
    XShapeCombineRectangles(display, snap_win, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted);
    XMapRaised(display, snap_win);
}

/* the snap a drag would give with the pointer at x, y: a move (HIT_CAPTION) whose pointer reaches the left or right edge of the work
   area snaps to that half, the top edge maximises; a resize of the top or bottom border whose pointer reaches that edge stretches the
   window to the full height. The pointer, not the frame, has to be there. */
static int
snap_target(struct client *c, int hit, int x, int y)
{
    if (c->fullscreen || client_fixed(c)) return SNAP_NONE;
    int ax, ay, aw, ah; workarea_mon(ws_m_list[c->ws], &ax, &ay, &aw, &ah);
    if (hit == HIT_CAPTION) {
        if (x <= ax) return SNAP_LEFT;
        if (x >= ax + aw - 1) return SNAP_RIGHT;
        if (y <= ay) return SNAP_MAX;
        return SNAP_NONE;
    }
    if ((hit == HIT_T && y <= ay) || (hit == HIT_B && y >= ay + ah - 1)) return SNAP_VERT;
    return SNAP_NONE;
}

/* a move drag takes a maximised or snapped window away: it gets its earlier size back, under the pointer at the same fraction of its
   width (Windows 7), and the drag continues with it */
static void
snap_tear_off(struct client *c, int px, int py)
{
    struct client_geom r = c->snap ? c->snap_prev : c->prev;
    if (r.width <= 0 || r.height <= 0) { r.width = c->geom.width; r.height = c->geom.height; }
    double f = c->geom.width > 0 ? (double)(px - c->geom.x) / c->geom.width : 0.5;
    int nx = px - (int)(f * r.width + 0.5), ny = c->geom.y;
    LOGP("drag: 0x%lx leaves its %s, back to %dx%d", c->window, c->mono ? "maximised state" : "snap", r.width, r.height);
    (void)py;
    c->snap = SNAP_NONE;
    if (c->mono) {
        c->mono = false; c->xfade_t0 = 0;
        maxmode_update();
        client_geom_max(c, nx, ny, r.width, r.height, false);
    } else {
        client_move_absolute(c, nx, ny);
        client_resize_absolute(c, r.width, r.height);
        ewmh_update_state(c);
    }
}

static void
switch_ws(int ws)
{
    if (ws != curr_ws) showing_forget();   /* another desktop: whatever was minimised stays so, the mode is over */
    for (int i = 0; i < WORKSPACE_NUMBER; i++) {
        if (i != ws && ws_m_list[i] == ws_m_list[ws]) {
        /*if (i != ws) {*/
            for (struct client *tmp = c_list[i]; tmp != NULL; tmp = tmp->next) {
                client_hide(tmp);
                LOGN("Hiding client...");
            }
        } else if (i == ws) {
            int count, j;
            count = 0;

            // how many active clients are on the current workspace
            for (struct client *tmp = c_list[i]; tmp != NULL; tmp = tmp->next) {
                count++;
                if (!tmp->minimized)
                    client_show(tmp);
            }

            if (count != 0) {
                Window wins[count*2];
                j = 0;

                for (struct client *tmp = c_list[i]; tmp != NULL; tmp = tmp->next) {
                    wins[j] = tmp->window;
                    wins[j+1] = tmp->dec;
                    j += 2;
                }

                XRestackWindows(display, wins, count * 2);
                docks_raise();
            }
        }
    }
    curr_ws = ws;
    int mon = ws_m_list[ws];
    LOGP("Setting Screen #%d with active workspace %d", m_list[mon].screen, ws);
    {   /* focus the most recent window that is not minimised */
        struct client *f = c_list[curr_ws];
        while (f && f->minimized) f = f->next;
        client_manage_focus(f);
    }
    ewmh_update_stacking();
    ewmh_set_active_desktop(ws);
    /* berry discarded the whole event queue here (XSync(display, True)), which also dropped a pager's
       _NET_ACTIVE_WINDOW sent right after _NET_CURRENT_DESKTOP (xdotool windowactivate, taskbars); only the
       EnterNotify events caused by moving windows around need dropping */
    XSync(display, False);
    { XEvent ev; while (XCheckMaskEvent(display, EnterWindowMask, &ev)) ; }
}

static void
warp_pointer(struct client *c)
{
    XWarpPointer(display, None, c->dec, 0, 0, 0, 0, c->geom.width / 2, c->geom.height / 2);
}

static void
client_toggle_decorations(struct client *c)
{
    if (c->decorated) {
        client_decorations_destroy(c);
    } else {
        client_decorations_create(c);
        if (!c->unmapped) XMapWindow(display, c->dec);
    }

    client_refresh(c);
    client_raise(c);
    client_manage_focus(c);
    ewmh_set_frame_extents(c);
}

/*
 * Credit to tudurom and windowchef
 * as inspiration for this functionality
 */
static void
client_set_status(struct client *c)
{
    if (c == NULL)
        return;
    ewmh_update_state(c);
    int size = 0;
    int mon = 0;
    char *str = NULL;
    char *state, *decorated;

    LOGN("Updating client status...");

    if (c->fullscreen)
        state = "fullscreen";
    else if (c->mono)
        state = "mono";
    else if (c->hidden)
        state = "hidden";
    else
        state = "normal";

    if (c->decorated)
        decorated = "true";
    else
        decorated = "false";


    if (conf.json_status) {
        mon = ws_m_list[c->ws];
        size = wm_asprintf(&str,
                "{"
                    "\"window\":\"0x%08x\","
                    "\"geom\":{"
                        "\"x\":%d,"
                        "\"y\":%d,"
                        "\"width\":%d,"
                        "\"height\":%d"
                    "},"
                    "\"monitor\":{"
                        "\"id\": %d,"
                        "\"x\":%d,"
                        "\"y\":%d,"
                        "\"width\":%d,"
                        "\"height\":%d"
                    "},"
                    "\"state\":\"%s\","
                    "\"decorated\":\"%s\""
                "}",
                (unsigned int)c->window, c->geom.x, c->geom.y, c->geom.width, c->geom.height,
                mon, m_list[mon].x, m_list[mon].y, m_list[mon].width, m_list[mon].height,
                state, decorated);
    } else {
        size = wm_asprintf(&str,
                "0x%08x, " // window id
                "%d, " // x
                "%d, " // y
                "%d, " // width
                "%d, " // height
                "%s, " // state
                "%s, " // decorated
                "%d, " // monitor id
                "%d, " // monitor x
                "%d, " // monitor y
                "%d, " // monitor width
                "%d",  // monitor height
                (unsigned int)c->window, c->geom.x, c->geom.y, c->geom.width, c->geom.height, state, decorated,
                mon, m_list[mon].x, m_list[mon].y, m_list[mon].width, m_list[mon].height);
    }

    if (size == -1) {
        LOGN("wm_asprintf returned -1, could not report window status");
        return;
    }

    XChangeProperty(display, c->window, net_berry[BerryWindowStatus], utf8string, 8, PropModeReplace,
            (unsigned char *) str, strlen(str));
    free(str);
}

static void
client_set_wm_state(struct client *c, unsigned long state)
{
    unsigned long data[] = { state, None };
    XChangeProperty(display, c->window, wm_atom[WMState], wm_atom[WMState], 32, PropModeReplace, (unsigned char*)data, 2);
}

static void
ewmh_set_fullscreen(struct client *c, bool fullscreen)
{
    c->fullscreen = fullscreen;
    ewmh_update_state(c);
}

static void
ewmh_set_below(struct client *c, bool below)
{
    c->below = below;
    ewmh_update_state(c);
}

static void
ewmh_set_viewport(void)
{
    unsigned long data[2] = { 0, 0 };
    XChangeProperty(display, root, net_atom[NetDesktopViewport], XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&data, 2);
}

static void
ewmh_set_focus(struct client *c)
{
        XDeleteProperty(display, root, net_atom[NetActiveWindow]);
        f_client = c;
        /* Tell EWMH about our new window */
        XChangeProperty(display, root, net_atom[NetActiveWindow], XA_WINDOW, 32, PropModeReplace, (unsigned char *) &(c->window), 1);
}

static void
ewmh_set_desktop(struct client *c, int ws)
{
    unsigned long data[1];
    data[0] = ws;
    XChangeProperty(display, c->window, net_atom[NetWMDesktop],
            XA_CARDINAL, 32, PropModeReplace, (unsigned char *) data, 1);
}

static void ewmh_set_frame_extents(struct client *c)
{
    unsigned long data[4];
    int left, right, top, bottom;
    LOGN("Setting client frame extents");

    /* the frame around the client as it lies: with size hints a maximised client may leave a remainder at the right and bottom,
       which counts as frame (LUXWM.md "Size hints"); written only on change */
    int x, y, w, h;
    client_area(c, c->geom.width, c->geom.height, &x, &y, &w, &h);
    left = x; top = y; right = MAX(c->geom.width - x - w, 0); bottom = MAX(c->geom.height - y - h, 0);
    if (!c->decorated) left = right = top = bottom = 0;

    data[0] = left;
    data[1] = right;
    data[2] = top;
    data[3] = bottom;
    if (c->fext_set && (long)data[0] == c->fext[0] && (long)data[1] == c->fext[1] && (long)data[2] == c->fext[2] && (long)data[3] == c->fext[3])
        return;
    XChangeProperty(display, c->window, net_atom[NetWMFrameExtents],
            XA_CARDINAL, 32, PropModeReplace, (unsigned char *) data, 4);
    for (int i = 0; i < 4; i++) c->fext[i] = (long)data[i];
    c->fext_set = true;
}

static void ewmh_set_client_list(void)
{
    /* EWMH: initial mapping order (c_list is the per-workspace focus stack, so sort by creation sequence) */
    int n = 0;
    for (int i = 0; i < WORKSPACE_NUMBER; i++)
        for (struct client *tmp = c_list[i]; tmp != NULL; tmp = tmp->next)
            n++;
    struct client *cl[n > 0 ? n : 1];
    Window wl[n > 0 ? n : 1];
    n = 0;
    for (int i = 0; i < WORKSPACE_NUMBER; i++)
        for (struct client *tmp = c_list[i]; tmp != NULL; tmp = tmp->next)
            cl[n++] = tmp;
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && cl[j - 1]->seq > cl[j]->seq; j--) { struct client *t = cl[j]; cl[j] = cl[j - 1]; cl[j - 1] = t; }
    for (int i = 0; i < n; i++)
        wl[i] = cl[i]->window;
    XChangeProperty(display, root, net_atom[NetClientList], XA_WINDOW, 32, PropModeReplace, (unsigned char *)wl, n);
}

/*
* Create and populate the values for _NET_DESKTOP_NAMES,
* used by applications such as polybar for named workspaces.
* By default, set the name of each workspaces to simply be the
* index of that workspace.
*/
static void ewmh_set_desktop_names(void)
{
    char** list = calloc(WORKSPACE_NUMBER, sizeof(char*));
    for (int i = 0; i < WORKSPACE_NUMBER; i++)
        wm_asprintf(&list[i], "%d", i);
    XTextProperty text_prop;
    Xutf8TextListToTextProperty(display, list, WORKSPACE_NUMBER, XUTF8StringStyle, &text_prop);
    XSetTextProperty(display, root, &text_prop, XInternAtom(display, "_NET_DESKTOP_NAMES", False));
    XFree(text_prop.value);
    for (int i = 0; i < WORKSPACE_NUMBER; i++)
        free(list[i]);
    free(list);
}

static void
ewmh_set_active_desktop(int ws)
{
    unsigned long data[1];
    data[0] = ws;
    XChangeProperty(display, root, net_atom[NetCurrentDesktop], XA_CARDINAL, 32,
            PropModeReplace, (unsigned char *) data, 1);
}

static void
usage(void)
{
    printf("Usage: luxwm [-h|-v|-d|-c CONFIG_PATH|-t THEME]\n"
           "  -t THEME  a theme name, an alias, a theme directory or a spec DIR|STYLE.msstyles|COLOR|SIZE\n"
           "            (also LHWM_THEME; LHWM_THEME_SOURCE=dir|lib picks the PNG cache or libuxtheme);\n"
           "            used only when the root has no _LH_THEME yet\n");
    exit(EXIT_SUCCESS);
}

static void
version(void)
{
    printf("luxwm %s (_NET_WM_NAME %s)\n", __THIS_VERSION__, __WINDOW_MANAGER_NAME__);
    printf("Copyright (c) 2018 Joshua L Ervin\n");
    printf("Released under the MIT License\n");
    exit(EXIT_SUCCESS);
}

static int
xerror(Display *dpy, XErrorEvent *e)
{
    /* this is stolen verbatim from katriawm which stole it from dwm lol */
    if (e->error_code == BadWindow ||
            (e->request_code == X_SetInputFocus && e->error_code == BadMatch) ||
            (e->request_code == X_PolyText8 && e->error_code == BadDrawable) ||
            (e->request_code == X_PolyFillRectangle && e->error_code == BadDrawable) ||
            (e->request_code == X_PolySegment && e->error_code == BadDrawable) ||
            (e->request_code == X_ConfigureWindow && e->error_code == BadMatch) ||
            (e->request_code == X_GrabButton && e->error_code == BadAccess) ||
            (e->request_code == X_GrabKey && e->error_code == BadAccess) ||
            (e->request_code == X_CopyArea && e->error_code == BadDrawable) ||
            (e->request_code == 139 && e->error_code == BadDrawable) ||
            (e->request_code == 139 && e->error_code == 143))
        return 0;

    LOGP("Fatal request. Request code=%d, error code=%d", e->request_code, e->error_code);
    return xerrorxlib(dpy, e);
}

/* ---- EWMH window states, stacking order, struts and the work area ---- */

enum { ST_VALID = 1 << 30, ST_HIDDEN = 1, ST_MAXV = 2, ST_MAXH = 4, ST_ATTN = 8, ST_FOCUSED = 16, ST_FS = 32, ST_BELOW = 64 };

/* write _NET_WM_STATE from the client's flags; atoms luxwm does not own (SKIP_TASKBAR, ABOVE, STICKY, ...) are kept */
static void
ewmh_update_state(struct client *c)
{
    if (c == NULL)
        return;
    unsigned st = ST_VALID;
    if (c->minimized)  st |= ST_HIDDEN;
    if (c->mono)       st |= ST_MAXV | ST_MAXH;
    if (c->snap)       st |= ST_MAXV;   /* snapped: the work area's full height */
    if (c->attention)  st |= ST_ATTN;
    if (client_active(c)) st |= ST_FOCUSED;
    if (c->fullscreen) st |= ST_FS;
    if (c->below)      st |= ST_BELOW;
    if (st == c->net_state)
        return;
    c->net_state = st;

    Atom out[72]; int n = 0;
    Atom ty; int fmt; unsigned long cnt = 0, after; unsigned char *d = NULL;
    if (XGetWindowProperty(display, c->window, net_atom[NetWMState], 0, 64, False, XA_ATOM, &ty, &fmt, &cnt, &after, &d) == Success && d) {
        Atom *a = (Atom *)d;
        for (unsigned long i = 0; i < cnt && n < 64; i++)
            if (a[i] != net_atom[NetWMStateHidden] && a[i] != net_atom[NetWMStateMaxVert] && a[i] != net_atom[NetWMStateMaxHorz] &&
                a[i] != net_atom[NetWMStateDemandsAttention] && a[i] != net_atom[NetWMStateFocused] &&
                a[i] != net_atom[NetWMStateFullscreen] && a[i] != net_atom[NetWMStateBelow])
                out[n++] = a[i];
        XFree(d);
    }
    if (st & ST_HIDDEN)  out[n++] = net_atom[NetWMStateHidden];
    if (st & ST_MAXV)    out[n++] = net_atom[NetWMStateMaxVert];
    if (st & ST_MAXH)    out[n++] = net_atom[NetWMStateMaxHorz];
    if (st & ST_ATTN)    out[n++] = net_atom[NetWMStateDemandsAttention];
    if (st & ST_FOCUSED) out[n++] = net_atom[NetWMStateFocused];
    if (st & ST_FS)      out[n++] = net_atom[NetWMStateFullscreen];
    if (st & ST_BELOW)   out[n++] = net_atom[NetWMStateBelow];
    XChangeProperty(display, c->window, net_atom[NetWMState], XA_ATOM, 32, PropModeReplace, (unsigned char *)out, n);
}

/* _NET_CLIENT_LIST_STACKING, bottom to top, from the real X stacking order (XQueryTree lists the root's children bottom-up) */
static void
ewmh_update_stacking(void)
{
    static Window *last = NULL; static int nlast = -1;
    Window rr, pr, *kids = NULL; unsigned int nk = 0;
    if (!XQueryTree(display, root, &rr, &pr, &kids, &nk))
        return;
    Window *list = malloc(sizeof(Window) * (nk ? nk : 1));
    int n = 0;
    for (unsigned int i = 0; i < nk; i++) {
        struct client *c = get_client_from_window(kids[i]);
        if (c && c->window == kids[i])
            list[n++] = kids[i];
    }
    if (kids)
        XFree(kids);
    if (n == nlast && (n == 0 || !memcmp(list, last, sizeof(Window) * n))) {
        free(list);
        return;
    }
    XChangeProperty(display, root, net_atom[NetClientListStacking], XA_WINDOW, 32, PropModeReplace, (unsigned char *)list, n);
    free(last);
    last = list;
    nlast = n;
}

/* move frame and client without touching the maximised state (hide/show use this) */
static void
client_move_raw(struct client *c, int x, int y)
{
    int l, r, t, b;
    margins(c, &l, &r, &t, &b);
    XMoveWindow(display, c->window, x + l, y + t);
    if (c->decorated)
        XMoveWindow(display, c->dec, x, y);
    c->geom.x = x;
    c->geom.y = y;
}

/* focus whatever should get it when c goes away (minimised or unmapped): the most recently focused other window on the
   workspace that is not minimised, or nothing (then _NET_ACTIVE_WINDOW becomes None) */
static void
focus_after(struct client *c)
{
    struct client *n = NULL;
    if (c->ws != curr_ws)
        return;
    for (struct client *t = c_list[c->ws]; t != NULL; t = t->next)
        if (t != c && !t->minimized) { n = t; break; }
    if (!f_active && c == f_client) {   /* the desktop has the focus: n becomes the last focused, still inactive */
        f_client = n;
        return;
    }
    client_manage_focus(n);
}

static void
client_minimize(struct client *c)
{
    if (c == NULL || c->minimized)
        return;
    c->minimized = true;
    if (!c->hidden) {
        if (ANIM_ON && !compositor && !sd_instant && c->ws == curr_ws) {   /* XP: the caption flies to the taskbar button, then the window goes */
            int ix, iy, iw, ih; anim_icon_rect(c, &ix, &iy, &iw, &ih);
            anim_classic(c, c->geom.x, c->geom.y, c->geom.width, c->geom.height, ix, iy, iw, ih);
        }
        c->x_hide = c->geom.x;   /* hidden in place: unmapped, not moved (picom's "hide" animation starts where the window is) */
        c->hidden = true;
    }
    if (!c->unmapped) {
        c->unmapped = true;
        c->unmap_ignore++;
        XUnmapWindow(display, c->window);
        if (c->decorated && c->dec != None) XUnmapWindow(display, c->dec);
    }
    client_set_wm_state(c, IconicState);
    ewmh_update_state(c);
    client_set_status(c);
    if (c == f_client)
        focus_after(c);
}

/* un-minimise (shown again if on the current workspace); callers focus it */
static void
client_restore(struct client *c)
{
    if (c == NULL || !c->minimized)
        return;
    showing_changed("a window restored");   /* a taskbar click, _NET_WM_STATE, Alt+Tab: not the restore of the remembered set */
    c->minimized = false;
    client_set_wm_state(c, NormalState);
    if (ANIM_ON && !compositor && !sd_instant && c->ws == curr_ws && c->hidden) {   /* XP: from the taskbar button to the window, then it appears */
        int ix, iy, iw, ih; anim_icon_rect(c, &ix, &iy, &iw, &ih);
        anim_classic(c, ix, iy, iw, ih, c->x_hide, c->geom.y, c->geom.width, c->geom.height);
    }
    if (c->ws == curr_ws)
        client_show(c);
    ewmh_update_state(c);
    client_set_status(c);
}

static Atom
window_type(Window w)
{
    Atom ty, t = None; int fmt; unsigned long n, after; unsigned char *d = NULL;
    if (XGetWindowProperty(display, w, net_atom[NetWMWindowType], 0, 1, False, XA_ATOM, &ty, &fmt, &n, &after, &d) == Success && d) {
        if (n) t = ((Atom *)d)[0];
        XFree(d);
    }
    return t;
}

/* keep a frame inside the work area (independent of conf.edge_lock): shrink if larger, then move in */
static void
client_clamp_workarea(struct client *c)
{
    int ax, ay, aw, ah, x = c->geom.x, y = c->geom.y;
    workarea_mon(ws_m_list[c->ws], &ax, &ay, &aw, &ah);
    if (c->geom.width > aw || c->geom.height > ah)
        client_resize_absolute(c, MIN(c->geom.width, aw), MIN(c->geom.height, ah));
    if (x + c->geom.width > ax + aw) x = ax + aw - c->geom.width;
    if (x < ax) x = ax;
    if (y + c->geom.height > ay + ah) y = ay + ah - c->geom.height;
    if (y < ay) y = ay;
    if (x != c->geom.x || y != c->geom.y)
        client_move_absolute(c, x, y);
}

/* where a new window goes: dialogs (and transients, which EWMH treats as dialogs) centred on their parent, a position
   the user or program asked for (USPosition, PPosition other than 0,0) honoured, otherwise smart placement; the frame
   is then clamped into the work area */
static void
client_initial_place(struct client *c, int req_x, int req_y)
{
    if (adopting) {
        /* an existing window (WM restart): the frame goes back around the client where it is; berry's hide parked
           minimised windows and other workspaces right of the screen, those are centred */
        int l, r, t, b;
        margins(c, &l, &r, &t, &b);
        if (req_x >= display_width) client_center(c);
        else client_move_absolute(c, req_x - l, req_y - t);
        return;
    }
    Window parent = None;
    XGetTransientForHint(display, c->window, &parent);
    struct client *p = parent != None ? get_client_from_window(parent) : NULL;
    bool dialog = window_type(c->window) == net_atom[NetWMWindowTypeDialog] || parent != None;

    if (dialog && p && p != c && !p->hidden) {
        client_move_absolute(c, p->geom.x + (p->geom.width - c->geom.width) / 2, p->geom.y + (p->geom.height - c->geom.height) / 2);
        client_clamp_workarea(c);
        return;
    }
    XSizeHints h; long supplied = 0;
    memset(&h, 0, sizeof h);
    if (XGetWMNormalHints(display, c->window, &h, &supplied)) {
        if ((h.flags & USPosition) || ((h.flags & PPosition) && (req_x != 0 || req_y != 0))) {
            /* ICCCM 4.1.2.3: the window gravity says which point of the frame lands on the requested client position
               (NorthWest, the default: the frame's top-left; SouthEast: its bottom-right; Static: the client stays put) */
            int l, r, t, b, x = req_x, y = req_y, g = (h.flags & PWinGravity) ? h.win_gravity : NorthWestGravity;
            margins(c, &l, &r, &t, &b);
            if (g == StaticGravity) { x -= l; y -= t; }
            else {
                if (g == NorthGravity || g == CenterGravity || g == SouthGravity) x -= (l + r) / 2;
                else if (g == NorthEastGravity || g == EastGravity || g == SouthEastGravity) x -= l + r;
                if (g == WestGravity || g == CenterGravity || g == EastGravity) y -= (t + b) / 2;
                else if (g == SouthWestGravity || g == SouthGravity || g == SouthEastGravity) y -= t + b;
            }
            client_move_absolute(c, x, y);
            client_clamp_workarea(c);
            return;
        }
    }
    if (dialog)
        client_center(c);
    else
        client_place(c);
    client_clamp_workarea(c);
}

/* ---- docks: unmanaged windows (MANAGE_DOCK is false) whose struts define the work area ---- */
/* seq: when luxwm started tracking the window (= its map; an unmap forgets it, a new map gets a new seq); layer: the
   window's _LHWM_DESKTOP_LAYER (0 without one). Both order the desktop windows, see docks_raise() */
struct dock { Window w; long strut[12]; bool is_dock, below, is_desktop; long layer; unsigned long seq; };
static struct dock docks[64];
static int ndocks = 0;
static unsigned long dock_seq = 0;

static struct dock *
dock_find(Window w)
{
    for (int i = 0; i < ndocks; i++)
        if (docks[i].w == w) return &docks[i];
    return NULL;
}

static bool
dock_read(struct dock *k)
{
    long old[12]; memcpy(old, k->strut, sizeof old);
    memset(k->strut, 0, sizeof k->strut);
    Atom ty; int fmt; unsigned long n, after; unsigned char *d = NULL;
    if (XGetWindowProperty(display, k->w, net_atom[NetWMStrutPartial], 0, 12, False, XA_CARDINAL, &ty, &fmt, &n, &after, &d) == Success && d && n >= 12) {
        for (int i = 0; i < 12; i++) k->strut[i] = ((long *)d)[i];
    } else {
        if (d) { XFree(d); d = NULL; }
        if (XGetWindowProperty(display, k->w, net_atom[NetWMStrut], 0, 4, False, XA_CARDINAL, &ty, &fmt, &n, &after, &d) == Success && d && n >= 4) {
            long *s = (long *)d;   /* _NET_WM_STRUT: the whole edge */
            for (int i = 0; i < 4; i++) k->strut[i] = s[i];
            k->strut[4] = k->strut[6] = 0; k->strut[5] = k->strut[7] = display_height - 1;
            k->strut[8] = k->strut[10] = 0; k->strut[9] = k->strut[11] = display_width - 1;
        }
    }
    if (d) XFree(d);
    k->is_dock = window_type(k->w) == net_atom[NetWMWindowTypeDock];
    k->is_desktop = window_type(k->w) == net_atom[NetWMWindowTypeDesktop];
    k->below = false;
    k->layer = 0;
    d = NULL;
    if (XGetWindowProperty(display, k->w, lhwm_desklayer_atom, 0, 1, False, XA_CARDINAL, &ty, &fmt, &n, &after, &d) == Success && d && n >= 1)
        k->layer = (long)(int)((long *)d)[0];   /* CARDINAL, but read as signed so a client can go below the default 0 */
    if (d) { XFree(d); d = NULL; }
    if (XGetWindowProperty(display, k->w, net_atom[NetWMState], 0, 64, False, XA_ATOM, &ty, &fmt, &n, &after, &d) == Success && d) {
        for (unsigned long i = 0; i < n; i++) if (((Atom *)d)[i] == net_atom[NetWMStateBelow]) k->below = true;
        XFree(d);
    }
    return memcmp(old, k->strut, sizeof old) != 0;
}

static void
dock_track(Window w)
{
    XWindowAttributes wa;
    if (w == root || w == check || w == nofocus || get_client_from_window(w))
        return;
    if (!XGetWindowAttributes(display, w, &wa) || wa.override_redirect || wa.class == InputOnly)
        return;
    struct dock *k = dock_find(w);
    if (k == NULL) {
        if (ndocks >= 64) return;
        k = &docks[ndocks++];
        memset(k, 0, sizeof *k);
        k->w = w;
        k->seq = ++dock_seq;
        XSelectInput(display, w, PropertyChangeMask);
        LOGP("tracking unmanaged window 0x%lx", w);
    }
    dock_read(k);
    workarea_update();
    docks_raise();
}

static void
dock_forget(Window w)
{
    for (int i = 0; i < ndocks; i++)
        if (docks[i].w == w) {
            docks[i] = docks[--ndocks];
            workarea_update();
            return;
        }
}

/* PropertyNotify on a tracked dock: re-read struts / state; returns true if the window is a dock */
static bool
dock_property(Window w, Atom a)
{
    struct dock *k = dock_find(w);
    if (k == NULL)
        return false;
    if (a == net_atom[NetWMStrutPartial] || a == net_atom[NetWMStrut] || a == net_atom[NetWMState] || a == net_atom[NetWMWindowType] || a == lhwm_desklayer_atom) {
        dock_read(k);
        docks_raise();
        workarea_dirty = true;   /* the work area (and the refit of maximised windows) once the queue is drained, see run() */
    }
    return true;
}

/* desktop windows from the top down: higher _LHWM_DESKTOP_LAYER first, then the later-mapped one first */
static int
desktop_cmp(const void *a, const void *b)
{
    const struct dock *x = *(const struct dock * const *)a, *y = *(const struct dock * const *)b;
    if (x->layer != y->layer) return x->layer > y->layer ? -1 : 1;
    return x->seq > y->seq ? -1 : x->seq < y->seq ? 1 : 0;
}

/* dock windows are above the clients unless they asked to be below (luxshell's sidebar); desktop windows (flxp-desktop,
   luxshell's desktop layer) go to the very bottom in a defined order (CLIENT-PROTOCOL.md "Desktop windows"): by
   _LHWM_DESKTOP_LAYER (higher above lower, default 0), equal layers in map order with the first-mapped one lowest.
   They are lowered from the top one down, so the last lowered (lowest layer, mapped first) ends at the bottom. */
static void
docks_raise(void)
{
    struct dock *desk[64]; int nd = 0;
    /* the docks keep the order among themselves that the server has (a dock orders itself with XRaiseWindow/XLowerWindow or a
       ConfigureRequest with stack_mode, applied before we get here): raise them bottom to top in tree order */
    Window root_ret, parent_ret, *kids = NULL;
    unsigned int nk = 0;
    if (XQueryTree(display, root, &root_ret, &parent_ret, &kids, &nk) && kids) {
        for (unsigned int j = 0; j < nk; j++) {
            struct dock *k = dock_find(kids[j]);
            if (k && k->is_dock && !k->below)
                XRaiseWindow(display, k->w);
        }
        XFree(kids);
    }
    for (int i = 0; i < ndocks; i++)
        if (docks[i].is_desktop)
            desk[nd++] = &docks[i];
    qsort(desk, nd, sizeof desk[0], desktop_cmp);
    for (int i = 0; i < nd; i++)
        XLowerWindow(display, desk[i]->w);
    /* EWMH "_NET_WM_STATE_FULLSCREEN": a focused fullscreen window is above the docks; once it loses the focus or leaves
       fullscreen, the next call puts the docks back on top of it */
    struct client *f = f_client;
    if (f != NULL && f_active && f->fullscreen && f->ws == curr_ws && !f->minimized && !f->unmapped) {
        if (f->decorated) { Window w[2] = { f->window, f->dec }; XRaiseWindow(display, f->window); XRestackWindows(display, w, 2); }
        else XRaiseWindow(display, f->window);
    }
}

/* a rectangle of the root minus the struts that overlap it, minus the configured gaps */
static void
strut_area(int mx, int my, int mw, int mh, int *ox, int *oy, int *ow, int *oh)
{
    int x0 = mx, y0 = my, x1 = mx + mw, y1 = my + mh;
    /* per edge the largest strut wins (MAX/MIN), struts never add up: each is a distance from the screen edge, so one stacked
       above another (luxshell's pinned sidebar above the taskbar) already includes it. A partial strut cuts the whole rect. */
    for (int i = 0; i < ndocks; i++) {
        long *s = docks[i].strut;
        if (s[0] > 0 && mx < s[0] && s[5] >= my && s[4] < my + mh) x0 = MAX(x0, (int)s[0]);                                   /* left */
        if (s[1] > 0 && mx + mw > display_width - s[1] && s[7] >= my && s[6] < my + mh) x1 = MIN(x1, display_width - (int)s[1]); /* right */
        if (s[2] > 0 && my < s[2] && s[9] >= mx && s[8] < mx + mw) y0 = MAX(y0, (int)s[2]);                                   /* top */
        if (s[3] > 0 && my + mh > display_height - s[3] && s[11] >= mx && s[10] < mx + mw) y1 = MIN(y1, display_height - (int)s[3]); /* bottom */
    }
    x0 += conf.left_gap; y0 += conf.top_gap; x1 -= conf.right_gap; y1 -= conf.bot_gap;
    *ox = x0; *oy = y0;
    *ow = MAX(x1 - x0, MINIMUM_DIM);
    *oh = MAX(y1 - y0, MINIMUM_DIM);
}

static void
workarea_mon(int mon, int *x, int *y, int *w, int *h)
{
    if (m_list == NULL || mon < 0 || mon >= MAX(m_count, 1)) {
        strut_area(0, 0, display_width, display_height, x, y, w, h);
        return;
    }
    strut_area(m_list[mon].x, m_list[mon].y, m_list[mon].width, m_list[mon].height, x, y, w, h);
}

/* recompute _NET_WORKAREA (one rect per desktop, all equal) and refit maximised windows when it changes */
static void
workarea_update(void)
{
    static long last[4] = { -1, -1, -1, -1 };
    int x, y, w, h;
    strut_area(0, 0, display_width, display_height, &x, &y, &w, &h);
    long wa[WORKSPACE_NUMBER * 4];
    for (int i = 0; i < WORKSPACE_NUMBER; i++) { wa[i * 4] = x; wa[i * 4 + 1] = y; wa[i * 4 + 2] = w; wa[i * 4 + 3] = h; }
    if (!memcmp(last, wa, sizeof last))
        return;
    memcpy(last, wa, sizeof last);
    XChangeProperty(display, root, net_atom[NetWorkarea], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)wa, WORKSPACE_NUMBER * 4);
    LOGP("work area %d,%d %dx%d", x, y, w, h);
    for (int i = 0; i < WORKSPACE_NUMBER; i++)
        for (struct client *t = c_list[i]; t != NULL; t = t->next)
            if (t->mono && !t->hidden)
                client_apply_max(t);
}

static void
handle_map_notify(XEvent *e)
{
    XMapEvent *ev = &e->xmap;
    if (ev->event != root || ev->override_redirect)
        return;
    dock_track(ev->window);   /* ignores managed clients and their decoration windows */
}

static void
handle_destroy_notify(XEvent *e)
{
    /* a minimised client is unmapped already: its destruction comes without an UnmapNotify */
    struct client *c = get_client_from_window(e->xdestroywindow.window);
    if (c != NULL && c->window == e->xdestroywindow.window) { LOGN("minimised client destroyed"); client_forget(c); return; }
    dock_forget(e->xdestroywindow.window);
}

/* ---- maximised mode, compositor detection, runtime theme switching (SHELL-DESIGN.md 11) ---- */

/* _LHWM_MAXIMIZED_MODE (root, CARDINAL per desktop, like _NET_WORKAREA): 1 while the desktop's active window is maximised or
   fullscreen, or, with no active window there, any window on it that is not minimised is. On the current desktop the active
   window is the focused one; on the others the most recently focused one that is not minimised. luxshell fades its black
   layers and its desktop layer on it. Each decoration window also carries _LHWM_MAXIMIZED (CARDINAL 0/1) so picom leaves
   maximised frames without a shadow. Only the frame: the client's own shadow falls under the bars (docks,
   clip-shadow-above), and the frame is the window that spans the whole work area. */
static void
maxmode_update(void)
{
    static long last[WORKSPACE_NUMBER]; static bool valid = false;
    long mm[WORKSPACE_NUMBER];
    for (int i = 0; i < WORKSPACE_NUMBER; i++) {
        struct client *a = NULL; bool any = false;
        if (i == curr_ws) { if (f_client && f_client->ws == i && !f_client->minimized) a = f_client; }
        else for (struct client *t = c_list[i]; t; t = t->next) if (!t->minimized) { a = t; break; }
        for (struct client *t = c_list[i]; t; t = t->next) {
            bool want = t->mono || t->fullscreen;
            if (!t->minimized && want) any = true;
            if (t->decorated && t->dec != None && (t->max_prop != want + 1 || t->max_prop_dec != t->dec)) {
                long v = want;
                XChangeProperty(display, t->dec, lhwm_max_atom, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&v, 1);
                t->max_prop = want + 1; t->max_prop_dec = t->dec;
            }
        }
        mm[i] = a ? (a->mono || a->fullscreen) : any;
    }
    if (valid && !memcmp(last, mm, sizeof mm))
        return;
    memcpy(last, mm, sizeof mm); valid = true;
    XChangeProperty(display, root, lhwm_maxmode_atom, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)mm, WORKSPACE_NUMBER);
    LOGP("maximised mode on desktop %d: %ld", curr_ws, mm[curr_ws]);
}

/* a theme name without '/' is a sibling of the requested theme's directory (fallback_theme = jade4074-classic); base may be a theme
   spec "DIR|STYLE|COLOR|SIZE" (frame.h wm_theme_load): its directory part counts */
static void
theme_sibling(char *out, size_t n, const char *base, const char *name)
{
    const char *bar = strchr(base, '|'), *end = bar ? bar : base + strlen(base), *sl = end;
    while (sl > base && sl[-1] != '/') sl--;
    if (strchr(name, '/') || sl == base) snprintf(out, n, "%s", name);
    else snprintf(out, n, "%.*s/%s", (int)(sl - 1 - base), base, name);
}

static void
theme_publish(void)
{
    snprintf(theme_written, sizeof theme_written, "%s", theme_cur);
    XChangeProperty(display, root, lhwm_theme_atom, XA_STRING, 8, PropModeReplace, (unsigned char *)theme_cur, strlen(theme_cur));
}

/* after a theme change: keep the client's logical rectangle where it was (new margins around it), keep the restore geometry's
   client rectangle too, refit maximised frames to the work area, repaint.
   The logical rectangle is the client without the CLIENT_CAPTION extension, i.e. the frame minus the theme's normal margins
   (wm_theme_margins_ex, not margins_for): the frame is computed from it with the new theme's normal margins, and only then is the
   client window placed with margins() (CLIENT_CAPTION: up into the caption by the new theme's caption_h - border, once).
   Keeping the client window's own rectangle (caption rows included) instead would move the logical rectangle by the difference
   of the two themes' caption_h - border on every switch, and with the CLIENT_CAPTION toggle path (which keeps the frame, i.e. the
   logical rectangle) the client's height would drift (flxp: 418, 398, 416, 390, 408). */
static void
client_retheme(struct client *c, const struct theme *old)
{
    if (!c->decorated)
        return;
    int ol, or_, ot, ob, nl, nr, nt, nb;
    wm_theme_margins_ex(old, c->mono, &ol, &or_, &ot, &ob);
    wm_theme_margins_ex(&theme, c->mono, &nl, &nr, &nt, &nb);
    int x = (c->hidden ? c->x_hide : c->geom.x) + ol - nl, y = c->geom.y + ot - nt;
    int w = MAX(c->geom.width - ol - or_ + nl + nr, MINIMUM_DIM), h = MAX(c->geom.height - ot - ob + nt + nb, MINIMUM_DIM);
    /* size hints: the client's size is kept exactly, so this only fits a client that did not conform yet; not for CLIENT_CAPTION,
       whose client changes by the caption difference (rounding that down at every switch would drift: the remainder lies on the plate) */
    if (!c->mono && !(c->ext_flags & FRAME_CLIENT_CAPTION)) frame_fit(c, &w, &h);
    if (c->hidden) c->x_hide = x; else c->geom.x = x;
    c->geom.y = y; c->geom.width = w; c->geom.height = h;
    int px = c->hidden ? c->geom.x : x;
    int cx, cy, cw, ch; client_area(c, w, h, &cx, &cy, &cw, &ch);   /* the client inside the new frame (CLIENT_CAPTION: the shorter top margin) */
    XMoveResizeWindow(display, c->dec, px, y, w, h);
    XMoveResizeWindow(display, c->window, px + cx, y + cy, (unsigned)cw, (unsigned)ch);
    if (c->mono) {   /* c->prev is a frame rectangle of the unmaximised window */
        wm_theme_margins_ex(old, 0, &ol, &or_, &ot, &ob);
        wm_theme_margins_ex(&theme, 0, &nl, &nr, &nt, &nb);
        c->prev.x += ol - nl; c->prev.y += ot - nt;
        c->prev.width += nl + nr - ol - or_; c->prev.height += nt + nb - ot - ob;
        long g[4] = { c->prev.x, c->prev.y, c->prev.width, c->prev.height };
        XChangeProperty(display, c->window, XInternAtom(display, "_LHWM_RESTORE_GEOMETRY", False), XA_CARDINAL, 32, PropModeReplace, (unsigned char *)g, 4);
    }
    ewmh_set_frame_extents(c);
    client_set_icon(c);   /* sized by the theme's icon_size */
    /* theme switches follow a compositor coming or going, when the server redirects or unredirects every window: have the
       client repaint (an Expose) so it does not keep the contents it had while the compositor changed (seen with urxvt) */
    XClearArea(display, c->window, 0, 0, 0, 0, True);
    if (c->mono && !c->hidden)
        client_apply_max(c);
    else
        draw_text(c, client_active(c));
}

/* load the theme to use: theme_req, or its fallback_theme when it is a DWM theme and no compositor runs (Longhorn's
   USESCOMPOSITION fallback to the msstyles frame); switch every client over when it differs from the current one */
static bool
theme_select(void)
{
    char eff[2200]; struct theme nt;
    snprintf(eff, sizeof eff, "%s", theme_req);
    if (!wm_theme_load(&nt, eff)) {
        fprintf(stderr, "luxwm: could not load theme from %s\n", eff);
        if (theme_cur[0]) { wm_theme_free(&nt); theme_publish(); composited_publish(); return false; }   /* keep what is there */
    }
    /* the classic frame (LUXWM.md "Classic fallback"): a path.frames_classic directory; frames_classic empty or "style": the
       theme's own visual style in msstyles mode ("|STYLE|COLOR|SIZE", drawn live through libuxtheme), and if that does not load (no
       Window class, no library) the theme.ini fallback_theme; _LHWM_THEME set directly (theme_req_classic ""): fallback_theme, where
       "style" is the own style too */
    if (nt.mode == THEME_DWM && (!compositor || theme_comp_off)) {
        char cand[2][2200]; int nc = 0;
        char own[2200] = "";
        if (nt.style[0]) snprintf(own, sizeof own, "|%.1500s|%.63s|%.63s", nt.style, nt.style_color, nt.style_size);
        bool req_style = !strcmp(theme_req_classic, "style");
        if (theme_req_classic[0] && !req_style) snprintf(cand[nc++], sizeof cand[0], "%s", theme_req_classic);
        else {
            if (req_style && own[0]) snprintf(cand[nc++], sizeof cand[0], "%s", own);
            if (!strcmp(nt.fallback, "style")) { if (own[0] && !req_style) snprintf(cand[nc++], sizeof cand[0], "%s", own); }
            else if (nt.fallback[0]) theme_sibling(cand[nc++], sizeof cand[0], theme_req, nt.fallback);
        }
        for (int i = 0; i < nc; i++) {
            struct theme ft;
            if (wm_theme_load(&ft, cand[i])) {
                fprintf(stderr, "luxwm: %s: %s -> %s%s\n", compositor ? "composition = 0" : "no compositor", theme_req, cand[i],
                        !strcmp(cand[i], own) ? " (the theme's own msstyles)" : "");
                wm_theme_free(&nt); nt = ft; snprintf(eff, sizeof eff, "%s", cand[i]);
                break;
            }
            wm_theme_free(&ft); fprintf(stderr, "luxwm: classic frame %s did not load%s\n", cand[i], i + 1 < nc ? ", trying the next" : ", keeping the glass frame");
        }
    }
    if (theme_cur[0] && !strcmp(eff, theme_cur) && !theme_force) {
        wm_theme_free(&nt); theme_publish(); composited_publish(); return true;
    }
    struct theme old = theme;
    theme = nt; theme_gen++;
    for (int i = 0; i < WORKSPACE_NUMBER; i++)
        for (struct client *c = c_list[i]; c != NULL; c = c->next)
            client_retheme(c, &old);
    wm_theme_free(&old);
    snprintf(theme_cur, sizeof theme_cur, "%s", eff);
    { int ml, mr, mt, mb; wm_theme_margins(&theme, &ml, &mr, &mt, &mb);
      fprintf(stderr, "luxwm: theme %s (%s, %s) margins t=%d r=%d b=%d l=%d font=%s\n", theme_cur, theme.mode == THEME_MSSTYLES ? "msstyles" : "dwm",
              theme.ut ? theme.ut_src : "PNG cache", mt, mr, mb, ml, theme.mode == THEME_MSSTYLES ? theme.ms_font : theme.font); }
    if (getenv("LHWM_DEBUG")) wm_frame_fonts_report(&theme);
    theme_publish();
    composited_publish();
    XFlush(display);
    return true;
}

/* _LHWM_THEME changed on the root: xprop -root -f _LHWM_THEME 8s -set _LHWM_THEME <dir|name> asks for another theme
   (luxwm's own writes come back here too and are ignored) */
static void
theme_property(void)
{
    Atom ty; int fmt; unsigned long n, after; unsigned char *d = NULL;
    if (XGetWindowProperty(display, root, lhwm_theme_atom, 0, 550, False, AnyPropertyType, &ty, &fmt, &n, &after, &d) != Success || !d)
        return;
    char want[2200]; snprintf(want, sizeof want, "%.*s", (int)n, (char *)d); XFree(d);
    if (fmt != 8 || !want[0] || !strcmp(want, theme_written))
        return;
    char prev[2200], prev_c[1024]; bool prev_off = theme_comp_off;
    snprintf(prev, sizeof prev, "%s", theme_req); snprintf(prev_c, sizeof prev_c, "%s", theme_req_classic);
    theme_sibling(theme_req, sizeof theme_req, prev, want);
    theme_req_classic[0] = 0; theme_comp_off = false;   /* a directory set directly (debugging): its own fallback_theme */
    fprintf(stderr, "luxwm: _LHWM_THEME asks for %s\n", theme_req);
    theme_announce = true;
    if (!theme_select()) {
        snprintf(theme_req, sizeof theme_req, "%s", prev); snprintf(theme_req_classic, sizeof theme_req_classic, "%s", prev_c); theme_comp_off = prev_off; }
}

/* a root string property (any 8-bit type) into buf; false when missing or empty */
static bool
root_string(Atom a, char *buf, size_t n)
{
    Atom ty; int fmt; unsigned long cnt, after; unsigned char *d = NULL;
    buf[0] = 0;
    if (XGetWindowProperty(display, root, a, 0, 256, False, AnyPropertyType, &ty, &fmt, &cnt, &after, &d) != Success || !d)
        return false;
    if (fmt == 8) snprintf(buf, n, "%.*s", (int)cnt, (char *)d);
    XFree(d);
    return buf[0] != 0;
}

/* a long root string property (any 8-bit type, up to 64 KiB: _LH_THEME_RESOLVED), malloc'ed; NULL when missing or empty */
static char *
root_string_alloc(Atom a)
{
    Atom ty; int fmt; unsigned long cnt, after; unsigned char *d = NULL;
    if (a == None || XGetWindowProperty(display, root, a, 0, 16384, False, AnyPropertyType, &ty, &fmt, &cnt, &after, &d) != Success || !d)
        return NULL;
    char *out = NULL;
    if (fmt == 8 && cnt > 0 && (out = malloc(cnt + 1))) { memcpy(out, d, cnt); out[cnt] = 0; }
    XFree(d);
    return out;
}

/* _LH_SETTINGS_SERIAL, the serial a record is resolved at */
static unsigned long
settings_serial(void)
{
    Atom ty; int fmt; unsigned long n, after; unsigned char *d = NULL; unsigned long v = 0;
    if (lh_serial_atom != None && XGetWindowProperty(display, root, lh_serial_atom, 0, 1, False, XA_CARDINAL, &ty, &fmt, &n, &after, &d) == Success && d) {
        if (fmt == 32 && n == 1) v = (unsigned long)*(long *)d;
        XFree(d);
    }
    return v;
}

/* the record without its serial= line (to compare two records) */
static char *
record_body(const char *rec)
{
    if (!rec) return NULL;
    char *out = malloc(strlen(rec) + 1), *o = out;
    if (!out) return NULL;
    for (const char *p = rec; *p; ) {
        const char *e = strchr(p, '\n'); size_t l = e ? (size_t)(e - p + 1) : strlen(p);
        if (strncmp(p, "serial=", 7)) { memcpy(o, p, l); o += l; }
        p += l;
    }
    *o = 0;
    return out;
}

/* a theme name (a theme file, a bare style, an alias such as aero-patch or jade4074-classic; also a path whose last component is
   one) through liblhde: sets theme_req, theme_req_classic, theme_comp_off, the fonts and theme_anim; canon gets the theme's
   name. Nothing changes when it is unknown. publish: write _LH_THEME_RESOLVED when it does not describe this record yet. */
static bool
lh_theme_apply(const char *arg, char *canon, size_t n)
{
    char base[256]; size_t len = strlen(arg);
    while (len > 1 && arg[len - 1] == '/') len--;
    const char *b = arg + len; while (b > arg && b[-1] != '/') b--;
    snprintf(base, sizeof base, "%.*s", (int)(arg + len - b), b);
    char err[512];
    lhde_theme *t = lhde_theme_resolve(arg, err, sizeof err);
    if (!t && strcmp(base, arg)) t = lhde_theme_resolve(base, err, sizeof err);
    if (!t) { fprintf(stderr, "luxwm: theme %s: %s\n", arg, err); return false; }
#define TV(k) (lhde_theme_get(t, k) ? lhde_theme_get(t, k) : "")
    const char *frames = TV("path.frames"), *classic = TV("path.frames_classic"), *style = TV("path.style");
    const char *color = TV("VisualStyle.color"), *size = TV("VisualStyle.size"), *comp = TV("Glass.composition");
    /* the frame: Windows Classic for style = classic (and for a theme with neither a frame set nor a visual style): the classic
       colours and metrics of the record (Colors.*, Metrics.*: the scheme, the current layer) in the theme spec, "|classic|KEY=VALUE;..."
       (frame.c classic_load; LUXWM.md "Windows Classic frames"); the glass set with the theme's visual style (wm_theme_load reads the style
       live through libuxtheme and takes only the tuning keys from the directory; LUXWM.md "libuxtheme source"); without glass the
       classic set (or the style alone), always */
    if (!strcmp(TV("style.kind"), "classic") || (!frames[0] && !classic[0] && !style[0])) {
        static const char *metrics[] = { "borderwidth", "captionwidth", "captionheight", "captionfont", "menufont" };
        size_t o = (size_t)snprintf(theme_req, sizeof theme_req, "|classic|smoothing=%s", TV("Fonts.smoothing"));
        for (int i = 0; i < 31 + 5 && o < sizeof theme_req; i++) {
            char k[64]; const char *nm = i < 31 ? lhde_color_name(i) : metrics[i - 31];
            if (!nm) continue;
            snprintf(k, sizeof k, "%s.%s", i < 31 ? "Colors" : "Metrics", nm);
            const char *v = lhde_theme_get(t, k);
            if (v && *v && !strchr(v, ';')) o += (size_t)snprintf(theme_req + o, sizeof theme_req - o, ";%s=%s", nm, v);
        }
        theme_req_classic[0] = 0;   /* no other frame without a compositor: this one is it */
        theme_comp_off = true;
    } else if (frames[0]) {
        snprintf(theme_req, sizeof theme_req, "%.1000s|%.1000s|%.63s|%.63s", frames, style, color, size);
        snprintf(theme_req_classic, sizeof theme_req_classic, "%s", classic[0] ? classic : "style");
        theme_comp_off = !strcmp(comp, "0");
    } else {
        snprintf(theme_req, sizeof theme_req, "%.1000s|%.1000s|%.63s|%.63s", classic, style, color, size);
        snprintf(theme_req_classic, sizeof theme_req_classic, "%s", classic[0] ? classic : "style");
        theme_comp_off = true;
    }
    wm_menu_set_theme_smoothing(TV("Fonts.smoothing"));   /* the menus' text, with settings.ini [fonts] smoothing over it */
    wm_frame_fonts_set(TV("path.fonts"));   /* registered by the wm_theme_load that follows */
    snprintf(theme_anim, sizeof theme_anim, "%s", TV("Glass.animations"));
    if (canon) snprintf(canon, n, "%s", TV("theme"));
    if (!strncmp(theme_req, "|classic|", 9))
        fprintf(stderr, "luxwm: theme %s%s%s: Windows Classic frames (style %s, scheme %s)\n", TV("theme"), strcmp(arg, TV("theme")) ? " for " : "",
                strcmp(arg, TV("theme")) ? arg : "", TV("VisualStyle.style"), TV("VisualStyle.scheme")[0] ? TV("VisualStyle.scheme") : "none: Windows Standard");
    else
        fprintf(stderr, "luxwm: theme %s%s%s: frames %s (%s), classic %s, style %s %s %s%s\n", TV("theme"), strcmp(arg, TV("theme")) ? " for " : "",
                strcmp(arg, TV("theme")) ? arg : "", frames[0] ? frames : "none", lhde_theme_why(t, "Glass.frames"),
                classic[0] ? classic : "(the style's own)", style[0] ? style : "(none)", color, size, theme_comp_off ? ", composition off" : "");
    /* _LH_THEME_RESOLVED: ours when it describes another theme or another record (luxshell writes the same) */
    char *rec = lhde_theme_serialize(t, settings_serial());
    char *mine = record_body(rec), *pub = root_string_alloc(lh_resolved_atom), *theirs = record_body(pub);
    if (rec && (!theirs || strcmp(theirs, mine)))
        XChangeProperty(display, root, lh_resolved_atom, utf8string, 8, PropModeReplace, (unsigned char *)rec, strlen(rec));
    free(theme_record); theme_record = mine;
    free(rec); free(pub); free(theirs);
#undef TV
    lhde_theme_free(t);
    return true;
}

/* root _LH_THEME changed (luxshell's settings, lhde-session theme, lhde-theme set, xprop -root -f _LH_THEME 8u -set _LH_THEME
   slate4074): load the theme's frame and announce it to clients (WM_THEMECHANGED: _LHWM_COMPOSITED and _LHWM_FRAME_METRICS are
   rewritten even when unchanged). An alias is replaced by the theme's name. */
static void
lh_theme_property(void)
{
    char want[256], canon[256], prev[2200], prev_c[1024], prev_anim[64]; bool prev_off = theme_comp_off;
    if (!root_string(lh_theme_atom, want, sizeof want))
        return;
    if (!strcmp(want, lh_theme_cur)) {   /* the name in use set again: a reload (unless it is luxwm's own alias rewrite coming back) */
        if (lh_theme_selfwrite > 0) { lh_theme_selfwrite--; return; }
        theme_reload("_LH_THEME set to the theme in use");
        return;
    }
    snprintf(prev, sizeof prev, "%s", theme_req); snprintf(prev_c, sizeof prev_c, "%s", theme_req_classic);
    snprintf(prev_anim, sizeof prev_anim, "%s", theme_anim);
    if (!lh_theme_apply(want, canon, sizeof canon)) {
        fprintf(stderr, "luxwm: _LH_THEME %s is no theme; keeping %s\n", want, lh_theme_cur);
        return;
    }
    snprintf(lh_theme_cur, sizeof lh_theme_cur, "%s", canon);
    if (strcmp(want, canon)) {
        lh_theme_selfwrite++;
        XChangeProperty(display, root, lh_theme_atom, utf8string, 8, PropModeReplace, (unsigned char *)canon, strlen(canon));
    }
    theme_announce = true;
    if (!theme_select()) {
        snprintf(theme_req, sizeof theme_req, "%s", prev); snprintf(theme_req_classic, sizeof theme_req_classic, "%s", prev_c); theme_comp_off = prev_off;
        snprintf(theme_anim, sizeof theme_anim, "%s", prev_anim); }
    settings_load(false);   /* the theme's animations */
}

/* the current theme again from its files (a theme key changed: NAME-modified.theme written, _LH_SETTINGS_CHANGED or a
   republished _LH_THEME_RESOLVED): applied when the record differs from the one in use */
static void
theme_recheck(const char *why)
{
    if (!lh_theme_cur[0]) return;
    char *before = theme_record ? strdup(theme_record) : NULL;
    char prev[2200], prev_c[1024], prev_anim[64], canon[256]; bool prev_off = theme_comp_off;
    snprintf(prev, sizeof prev, "%s", theme_req); snprintf(prev_c, sizeof prev_c, "%s", theme_req_classic);
    snprintf(prev_anim, sizeof prev_anim, "%s", theme_anim);
    theme_record = NULL;
    if (!lh_theme_apply(lh_theme_cur, canon, sizeof canon)) { free(theme_record); theme_record = before; return; }
    bool same = before && theme_record && !strcmp(before, theme_record);
    free(before);
    if (same) return;
    if (strcmp(theme_req, prev) || strcmp(theme_req_classic, prev_c) || theme_comp_off != prev_off) {
        fprintf(stderr, "luxwm: theme %s changed (%s)\n", lh_theme_cur, why);
        theme_announce = true;
        if (!theme_select()) {
            snprintf(theme_req, sizeof theme_req, "%s", prev); snprintf(theme_req_classic, sizeof theme_req_classic, "%s", prev_c); theme_comp_off = prev_off; }
    }
    if (strcmp(prev_anim, theme_anim)) settings_load(false);
}

/* root _LH_THEME_RESOLVED changed: when it describes the current theme and differs from what luxwm applied, resolve again */
static void
lh_resolved_property(void)
{
    char *pub = root_string_alloc(lh_resolved_atom), *body = record_body(pub);
    bool differs = body && theme_record && strcmp(body, theme_record);
    char name[256] = "";
    if (pub && !strncmp(pub, "theme=", 6)) { const char *e = strchr(pub + 6, '\n'); snprintf(name, sizeof name, "%.*s", (int)(e ? e - pub - 6 : (long)strlen(pub + 6)), pub + 6); }
    free(pub); free(body);
    if (differs && !strcmp(name, lh_theme_cur)) theme_recheck("_LH_THEME_RESOLVED republished");
}

/* re-read the theme in use from disk, in place (LUXWM.md "Live-editing a theme"): _LH_THEME resolved again,
   then theme_select with the same spec forced: theme.ini and its PNGs, or the .msstyles through libuxtheme, are loaded anew; every
   frame is re-placed around its client with the new margins (client_retheme keeps the client rectangle) and repainted, the
   header pixmaps are rendered again (new theme generation), _LHWM_FRAME_METRICS and _LHWM_COMPOSITED are republished. */
static void
theme_reload(const char *why)
{
    char prev[2200], prev_c[1024]; bool prev_off = theme_comp_off;
    snprintf(prev, sizeof prev, "%s", theme_req); snprintf(prev_c, sizeof prev_c, "%s", theme_req_classic);
    if (lh_theme_cur[0]) {
        char canon[256];
        if (!lh_theme_apply(lh_theme_cur, canon, sizeof canon))
            fprintf(stderr, "luxwm: reload: %s is no theme any more; reloading %s as it was\n", lh_theme_cur, theme_req);
    }
    theme_force = true; theme_announce = true;
    bool ok = theme_select();
    theme_force = false;
    if (!ok) {
        snprintf(theme_req, sizeof theme_req, "%s", prev); snprintf(theme_req_classic, sizeof theme_req_classic, "%s", prev_c); theme_comp_off = prev_off;
        fprintf(stderr, "luxwm: theme reload failed (%s); keeping the loaded theme\n", why);
        return;
    }
    fprintf(stderr, "luxwm: theme reloaded (%s): %s\n", why, theme_cur);
}

/* _NET_WM_CM_S<screen> owner changed (XFixes): a compositor appeared or went away */
static void
compositor_check(void)
{
    bool now = XGetSelectionOwner(display, cm_atom) != None;
    if (now == compositor)
        return;
    compositor = now;
    fprintf(stderr, "luxwm: compositor %s\n", now ? "appeared" : "gone");
    theme_select();
}

/* ---- keybindings: ~/.config/lhde/keys (lines: MODS+Key action [arg]); actions: exec, close, kill, cycle, maximize, minimize, sysmenu (the window menu), workspace N, showdesktop, shell CMD (_LHSHELL_COMMAND), start (bare Super → _LHSHELL_START), none ---- */
struct keybind { unsigned mods; KeySym sym; char action[16]; char arg[240]; };
static struct keybind keys[64]; static int nkeys = 0;
static const char *default_keys[] = {
    "Mod4+Return exec urxvt", "Mod4+t exec xterm", "Mod4+e exec if command -v flxp >/dev/null; then exec flxp; else exec rox; fi",
    "Mod4+r shell run", "Mod4+d showdesktop", "Mod4+s shell sidebar-stacking toggle",
    "Mod4+Shift+f shell screenshot", "Mod4+Shift+w shell screenshot_window", "Mod4+Shift+s shell screenshot_region",
    "Mod4+q close", "Mod1+F4 close", "Mod1+space sysmenu", "Mod4+Tab cycle", "Mod1+Tab cycle", "Mod4+Shift+Tab cycle", "Mod1+Shift+Tab cycle", "Mod4+Up maximize", "Mod4+Down minimize",
    "Mod4+1 workspace 0", "Mod4+2 workspace 1", "Mod4+3 workspace 2", "Mod4+4 workspace 3", "Super_L start", "Super_R start", NULL };

static unsigned modmask(const char *m) {
    if (!strcmp(m, "Mod4") || !strcmp(m, "Super")) return Mod4Mask;
    if (!strcmp(m, "Mod1") || !strcmp(m, "Alt")) return Mod1Mask;
    if (!strcmp(m, "Control") || !strcmp(m, "Ctrl")) return ControlMask;
    if (!strcmp(m, "Shift")) return ShiftMask;
    return 0;
}
static void keys_add(const char *line) {
    char buf[300], *p, *tok, *save; strncpy(buf, line, sizeof buf - 1); buf[sizeof buf - 1] = 0;
    if (!(p = strchr(buf, ' ')) || nkeys >= 64) return;
    *p++ = 0;
    struct keybind *k = &keys[nkeys]; memset(k, 0, sizeof *k);
    for (tok = strtok_r(buf, "+", &save); tok; tok = strtok_r(NULL, "+", &save)) { unsigned m = modmask(tok); if (m) k->mods |= m; else k->sym = XStringToKeysym(tok); }
    while (*p == ' ') p++;
    char *a = strchr(p, ' '); if (a) { *a++ = 0; while (*a == ' ') a++; strncpy(k->arg, a, sizeof k->arg - 1); }
    strncpy(k->action, p, sizeof k->action - 1);
    if (k->sym != NoSymbol) nkeys++;
}
static void keys_setup(void) {
    char path[MAXLEN]; lhde_config_file("keys", path, sizeof path);
    FILE *f = fopen(path, "r");
    if (f) { char line[300]; while (fgets(line, sizeof line, f)) { line[strcspn(line, "\n")] = 0; if (line[0] && line[0] != '#') keys_add(line); } fclose(f); }
    else for (int i = 0; default_keys[i]; i++) keys_add(default_keys[i]);
    /* "start" (Super pressed and released alone → _LHSHELL_START to the shell, SHELL-DESIGN 13): added to a user's keys file
       that binds none, so the start menu key works there too; "Super_L none" in the file turns it off */
    bool has_start = false; for (int i = 0; i < nkeys; i++) if (keys[i].sym == XK_Super_L || keys[i].sym == XK_Super_R) has_start = true;
    if (!has_start) { keys_add("Super_L start"); keys_add("Super_R start"); }
    /* newer defaults (show desktop, run, the sidebar's stacking toggle, the screenshot keys) apply to a user's keys file only where
       the key is unbound there; never Print, which may be the user's compose key (a binding of their own in the file) */
    if (f) for (int i = 0; default_keys[i]; i++) {
        if (strncmp(default_keys[i], "Mod4+d ", 7) && strncmp(default_keys[i], "Mod4+r ", 7) && strncmp(default_keys[i], "Mod4+s ", 7) &&
            strncmp(default_keys[i], "Mod4+Shift+f ", 13) && strncmp(default_keys[i], "Mod4+Shift+w ", 13) && strncmp(default_keys[i], "Mod4+Shift+s ", 13) &&
            strncmp(default_keys[i], "Mod1+space ", 11) && strncmp(default_keys[i], "Mod1+Shift+Tab ", 15) &&
            strncmp(default_keys[i], "Mod4+Shift+Tab ", 15)) continue;
        bool bound = false; int before = nkeys; keys_add(default_keys[i]);
        if (nkeys == before) continue;
        for (int j = 0; j < before; j++) if (keys[j].sym == keys[before].sym && keys[j].mods == keys[before].mods) bound = true;
        if (bound) nkeys = before;
    }
    for (int i = 0; i < nkeys; i++) {
        KeyCode kc = XKeysymToKeycode(display, keys[i].sym); if (!kc) continue;
        unsigned extra[] = { 0, LockMask, Mod2Mask, LockMask | Mod2Mask };
        for (int j = 0; j < 4; j++) XGrabKey(display, kc, keys[i].mods | extra[j], root, True, GrabModeAsync, GrabModeAsync);
    }
    LOGP("%d key bindings", nkeys);
}
static void spawn(const char *cmd) {
    if (fork() == 0) { const char *h = getenv("HOME"); if (h && *h && chdir(h)) {} setsid(); execl("/bin/sh", "sh", "-c", cmd, NULL); _exit(1); }
}
/* "shell CMD": a session command for the shell, the root property _LHSHELL_COMMAND (luxshell's runtime.lua handles it) */
static void shell_command(const char *cmd) {
    Atom p = XInternAtom(display, "_LHSHELL_COMMAND", False), u = XInternAtom(display, "UTF8_STRING", False);
    XChangeProperty(display, root, p, u, 8, PropModeReplace, (const unsigned char *)cmd, (int)strlen(cmd)); XFlush(display);
}

/* ---- show desktop (EWMH _NET_SHOWING_DESKTOP), as Windows' Win+D (LUXWM.md "Show desktop"): on, the visible windows of the current
   desktop are really minimised (iconic, task buttons stay) and remembered by window id with the focused one; they stay minimised.
   Off again (Win+D, or a _NET_SHOWING_DESKTOP 0 message) restores exactly that set and the old focus, but only while nothing has
   changed in between: a window restored, activated, newly mapped or closed (showing_changed) ends the mode without restoring
   anything, and the next Win+D is a new "show desktop" that minimises whatever is visible then. _NET_SHOWING_DESKTOP is 1 exactly
   while the remembered set is minimised and unchanged. ---- */
static Window sd_wins[256]; static int sd_n; static Window sd_focus; static bool sd_on, sd_restoring;
/* inside the show-desktop batch: no per-window animation (anim_classic skipped, picom's _LHWM_ANIMATE removed) */
static void showing_set_prop(void) {
    long v = sd_on; XChangeProperty(display, root, net_atom[NetShowingDesktop], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&v, 1);
}
static void showing_forget(void) { if (!sd_on) return; sd_on = false; sd_n = 0; sd_focus = None; showing_set_prop(); LOGN("showing desktop: forgotten"); }
/* something the user did changed the desktop while it was shown (why: for the debug log); not luxwm's own restore of the set */
static void showing_changed(const char *why) {
    if (!sd_on || sd_restoring) return;
    LOGP("showing desktop: %s, the remembered windows stay minimised", why);
    showing_forget();
}
/* instant batch: no animation for any window of the set; picom's hide/show animation is suppressed by deleting _LHWM_ANIMATE on client and
   frame while the set is unmapped/mapped (one server grab), put back 400 ms later */
static void sd_batch_begin(const Window *w, int n) {
    sd_instant = true; XGrabServer(display);
    if (compositor && ANIM_ON)
        for (int i = 0; i < n; i++) { struct client *c = get_client_from_window(w[i]); if (c) anim_mark(c, ANIM_NONE); }
}
static void sd_batch_end(const Window *w, int n) {
    if (compositor && ANIM_ON)
        for (int i = 0; i < n; i++) {   /* picom reads the property lazily: back to anim_style only after 400 ms (anim_expire) */
            struct client *c = get_client_from_window(w[i]); if (c) c->anim_until = now_ms() + 400;
        }
    XUngrabServer(display); XFlush(display); sd_instant = false;
}
static void showing_desktop(int mode) {
    if (mode == 2) mode = !sd_on;
    if (mode == sd_on) return;
    if (mode) {
        sd_n = 0; sd_focus = client_active(f_client) && f_client->ws == curr_ws ? f_client->window : None;
        for (struct client *c = c_list[curr_ws]; c && sd_n < 256; c = c->next)
            if (!c->hidden && !c->minimized) sd_wins[sd_n++] = c->window;
        if (!sd_n) return;                                  /* nothing to hide: stay off */
        /* bottom first: the focus passes to nothing in between (only the active window's minimise moves it), so the stack list
           keeps its order (a focus handed down would move each next window to the front) */
        sd_batch_begin(sd_wins, sd_n);
        for (int i = sd_n - 1; i >= 0; i--) { struct client *c = get_client_from_window(sd_wins[i]); if (c) client_minimize(c); }
        sd_batch_end(sd_wins, sd_n);
        sd_on = true; showing_set_prop();                   /* after the minimises (focus_after) */
    } else {
        int n = sd_n; Window w[256], focus = sd_focus; memcpy(w, sd_wins, n * sizeof *w);
        sd_restoring = true; sd_batch_begin(w, n);
        /* bottom first, each put on top of the stack list: the set comes back in the order it had (sd_wins is top first) */
        for (int i = n - 1; i >= 0; i--) {
            struct client *c = get_client_from_window(w[i]);
            if (c && c->window == w[i] && c->minimized) { client_move_to_front(c); client_restore(c); }
        }
        struct client *f = focus ? get_client_from_window(focus) : NULL;
        if (f && !f->minimized && f->ws == curr_ws) client_manage_focus(f);
        else { struct client *t = c_list[curr_ws]; while (t && t->minimized) t = t->next; if (t) client_manage_focus(t); }
        sd_batch_end(w, n); sd_restoring = false;
        sd_on = false; sd_n = 0; sd_focus = None; showing_set_prop();
    }
    LOGP("showing desktop %d", sd_on);
}

static bool super_down;
/* ---- Alt+Tab as Windows' (LUXWM.md "Alt+Tab"): while the binding's modifier (Alt, or Super for Mod4+Tab) is held the keyboard is
   luxwm's; the candidates are the current desktop's windows in stacking order (top first), then the minimised ones. Each Tab
   (Shift+Tab: backwards, wrapping) previews the next candidate by raising it, client and frame, over the others without focusing
   it; the previous candidate goes back to where it was (the whole set is restacked to the order from before Alt+Tab, then the new
   candidate put on top of it). A minimised candidate is not shown (it is restored only if it is chosen). Releasing the modifier
   restores the original order and focuses and raises the candidate for real (client_manage_focus: restored if minimised), so
   every other window keeps its place; Esc puts the original order back and leaves the focus alone. ---- */
static unsigned keycode_modmask(KeyCode kc) {
    XModifierKeymap *mm = XGetModifierMapping(display); unsigned m = 0;
    if (!mm) return 0;
    for (int i = 0; i < 8; i++) for (int j = 0; j < mm->max_keypermod; j++) if (mm->modifiermap[i * mm->max_keypermod + j] == kc) m |= 1u << i;
    XFreeModifiermap(mm);
    return m;
}
/* the current desktop's client and frame windows in the server's stacking order, top first */
static int cycle_snapshot(Window *out, int max) {
    Window rr, pr, *kids = NULL; unsigned nk = 0; int n = 0;
    if (!XQueryTree(display, root, &rr, &pr, &kids, &nk)) return 0;
    for (int i = (int)nk - 1; i >= 0 && n < max; i--) {
        struct client *c = get_client_from_window(kids[i]);
        if (c && c->ws == curr_ws && !c->hidden) out[n++] = kids[i];
    }
    if (kids) XFree(kids);
    return n;
}
/* restack the snapshot (only windows that still belong to a client), with cand (client + frame) on top of it when given */
static void cycle_restack(const Window *orig, int no, struct client *cand) {
    Window w[1024]; int n = 0;
    if (cand && !cand->hidden && !cand->minimized) { w[n++] = cand->window; if (cand->decorated && cand->dec != None) w[n++] = cand->dec; }
    for (int i = 0; i < no && n < 1024; i++) {
        struct client *c = get_client_from_window(orig[i]);
        if (!c || c->hidden || c->minimized || c == cand) continue;
        w[n++] = orig[i];
    }
    /* the first window stays where it is and the others go under it: on a preview the candidate (raised first), on the restore
       the original top window, under which the candidate falls back into its old place */
    if (cand && n) XRaiseWindow(display, w[0]);
    if (n > 1) XRestackWindows(display, w, n);
    docks_raise();
}
static void cycle_run(XKeyEvent *kev, unsigned mods, bool reverse) {
    Window cand[256]; int n = 0;
    for (struct client *c = c_list[curr_ws]; c && n < 256; c = c->next) if (!c->minimized && !c->hidden) cand[n++] = c->window;
    for (struct client *c = c_list[curr_ws]; c && n < 256; c = c->next) if (c->minimized) cand[n++] = c->window;
    if (!n) return;
    Window orig[1024]; int no = cycle_snapshot(orig, 1024);
    /* the first Tab: the window under the active one (the active one is first); from the desktop (nothing active) the top one */
    bool from_active = client_active(f_client) && f_client->window == cand[0];
    int idx = reverse ? n - 1 : (from_active && n > 1 ? 1 : 0);
    unsigned need = mods & ~(unsigned)ShiftMask;
    bool cancel = false, done = false;
    int gr = XGrabKeyboard(display, root, True, GrabModeAsync, GrabModeAsync, kev->time);
    if (gr != GrabSuccess) { done = true; LOGP("alt+tab: keyboard grab failed (%d): one step", gr); }
    else {   /* the modifier may be up already (a fast "key alt+Tab"): then this is a single step */
        Window d1, d2; int i1, i2, i3, i4; unsigned st = 0;
        XQueryPointer(display, root, &d1, &d2, &i1, &i2, &i3, &i4, &st);
        if (need && !(st & need)) { done = true; LOGP("alt+tab: modifier 0x%x already up (state 0x%x): one step", need, st); }
    }
    struct client *pc = get_client_from_window(cand[idx]);
    if (!done) { cycle_restack(orig, no, pc && pc->window == cand[idx] ? pc : NULL); XFlush(display); }
    LOGP("alt+tab: %d candidates, first 0x%lx", n, cand[idx]);
    while (!done) {
        XEvent ev;
        if (!XPending(display)) paint_dirty();
        XNextEvent(display, &ev);
        if (ev.type == KeyPress) {
            KeySym sym = XkbKeycodeToKeysym(display, ev.xkey.keycode, 0, 0);
            if (sym == XK_Escape) { cancel = done = true; break; }
            if (sym != XK_Tab && sym != XK_ISO_Left_Tab) continue;
            bool back = (ev.xkey.state & ShiftMask) != 0;
            for (int k = 0; k < n; k++) {   /* the next candidate that still exists */
                idx = (idx + (back ? n - 1 : 1)) % n;
                if (get_client_from_window(cand[idx])) break;
            }
            struct client *c = get_client_from_window(cand[idx]);
            cycle_restack(orig, no, c && c->window == cand[idx] ? c : NULL);
            XFlush(display);
            LOGP("alt+tab: candidate 0x%lx", cand[idx]);
        } else if (ev.type == KeyRelease) {
            unsigned m = keycode_modmask(ev.xkey.keycode);
            if (need ? ((m & need) && !((ev.xkey.state & need) & ~m)) : true) {
                done = true; LOGP("alt+tab: release of keycode %u (modifier 0x%x, state 0x%x)", ev.xkey.keycode, m, ev.xkey.state); }
        } else if (xfixes_ev >= 0 && ev.type == xfixes_ev + XFixesSelectionNotify) {
            compositor_check();
        } else if (ev.type < LASTEvent && event_handler[ev.type]) {
            event_handler[ev.type](&ev);
        }
    }
    XUngrabKeyboard(display, CurrentTime);
    if (need & Mod4Mask) super_down = false;   /* the Super release was ours: no start menu, and the next Super press is not a repeat */
    cycle_restack(orig, no, NULL);
    struct client *c = cancel ? NULL : get_client_from_window(cand[idx]);
    if (c && c->window == cand[idx]) {
        if (c->ws != curr_ws) switch_ws(c->ws);
        client_manage_focus(c);   /* restores a minimised one, moves it to the front of the stack, raises and focuses it */
    }
    ewmh_update_stacking();
    LOGP("alt+tab: %s 0x%lx", cancel ? "cancelled at" : "chose", cand[idx]);
}

/* Super alone: the "start" binding grabs the bare Super key (the keyboard is then luxwm's until Super is released, so the
   Mod4 bindings still arrive here); any other key or a button press in between cancels it; the release sends the
   _LHSHELL_START client message to the root (luxshell toggles its start menu). Unbound Super+key combinations are swallowed
   while Super is held (they never reached clients through the old Mod4 grabs either when bound). */
static void handle_key_release(XEvent *e) {
    XKeyEvent *ev = &e->xkey; KeySym sym = XkbKeycodeToKeysym(display, ev->keycode, 0, 0);
    if (!(sym == XK_Super_L || sym == XK_Super_R) || !super_down) return;
    super_down = false;
    if (super_other) return;
    XEvent m = {0}; m.xclient.type = ClientMessage; m.xclient.window = root; m.xclient.format = 32;
    m.xclient.message_type = XInternAtom(display, "_LHSHELL_START", False); m.xclient.data.l[0] = (long)ev->time;
    XSendEvent(display, root, False, SubstructureNotifyMask | SubstructureRedirectMask, &m); XFlush(display);
    LOGN("Super alone: _LHSHELL_START");
}
static void handle_key_press(XEvent *e) {
    XKeyEvent *ev = &e->xkey; KeySym sym = XkbKeycodeToKeysym(display, ev->keycode, 0, 0); unsigned mods = mod_clean(ev->state);
    bool super_key = sym == XK_Super_L || sym == XK_Super_R;
    if (!super_key) super_other = true;
    else if (super_down) return;                                   /* autorepeat of a held Super */
    for (int i = 0; i < nkeys; i++) {
        if (keys[i].sym != sym || keys[i].mods != mods) continue;
        const char *a = keys[i].action;
        if (!strcmp(a, "exec")) spawn(keys[i].arg);
        /* the window actions need an active window (Alt+F4 on the desktop closes nothing); Alt+Tab from the desktop first
           reactivates the last focused window */
        else if (!strcmp(a, "close") && client_active(f_client)) client_close(f_client);
        else if (!strcmp(a, "kill") && client_active(f_client)) client_kill(f_client);
        else if (!strcmp(a, "cycle")) cycle_run(ev, keys[i].mods, (keys[i].mods & ShiftMask) != 0);   /* Alt+Tab (Shift: backwards) */
        else if (!strcmp(a, "maximize") && client_active(f_client)) { client_monocle(f_client); draw_text(f_client, true); }
        else if (!strcmp(a, "minimize") && client_active(f_client)) client_minimize(f_client);
        else if (!strcmp(a, "sysmenu") && client_active(f_client)) wm_menu_at_caption(f_client, WMMENU_KEYBOARD, 0, false);
        else if (!strcmp(a, "workspace")) switch_ws(atoi(keys[i].arg));
        else if (!strcmp(a, "showdesktop")) showing_desktop(2);
        else if (!strcmp(a, "shell")) shell_command(keys[i].arg);
        else if (!strcmp(a, "start")) { super_down = true; super_other = false; }   /* the menu opens on the release */
        return;
    }
}

int get_dec_width(struct client *c)     { int l, r, t, b; margins(c, &l, &r, &t, &b); return l + r; }
int get_dec_height(struct client *c)    { int l, r, t, b; margins(c, &l, &r, &t, &b); return t + b; }

/* ---- frame input: buttons, hover, edge resize ---- */

/* wm_frame_hit_ex for c's decoration window. CLIENT_CAPTION (not maximised): the client covers the frame from the top border down,
   so the decoration only gets events in the border rows above it; all of them resize (top, or the corner where wm_frame_hit_ex has
   its corner zones on the outer edge), not just the outer 6 DWM rows with a 3-row move strip below them. The buttons keep their
   hit rectangles (they reach into the border). The zone is not shrunk by the client: wm_frame_hit_ex itself does not know about it. */
static int
client_hit(struct client *c, int x, int y)
{
    int hit = wm_frame_hit_ex(&theme, c->geom.width, c->geom.height, x, y, c->mono);
    if (client_fixed(c) && (hit == HIT_MAX || (hit >= HIT_L && hit <= HIT_BR))) return HIT_NONE;   /* HTBORDER: a fixed frame */
    if (hit == HIT_CAPTION && (c->ext_flags & FRAME_CLIENT_CAPTION) && !c->mono) {
        int l, r, t, b; margins(c, &l, &r, &t, &b);
        if (y < t) {
            int e = wm_frame_hit_ex(&theme, c->geom.width, c->geom.height, x, 0, 0);
            return e == HIT_TL || e == HIT_TR ? e : HIT_T;
        }
    }
    return hit;
}
static void
handle_motion_notify(XEvent *e)
{
    XMotionEvent *ev = &e->xmotion;
    struct client *c = get_client_from_window(ev->window);
    if (c == NULL || !c->decorated || ev->window != c->dec) return;
    int hit = client_hit(c, ev->x, ev->y);
    int hover = (hit == HIT_CLOSE || hit == HIT_MAX || hit == HIT_MIN) ? hit : 0;
    LOGP("motion on dec 0x%lx at %d,%d (dec %dx%d) hit=%d hover=%d prev=%d", c->dec, ev->x, ev->y, c->geom.width, c->geom.height, hit, hover, c->hover);
    Cursor cur = (hit >= HIT_L && hit <= HIT_BR) ? edge_cursor[hit] : normal_cursor;
    XDefineCursor(display, c->dec, cur);
    if (hover != c->hover) { c->hover = hover; draw_text(c, client_active(c)); }
}

static void
handle_leave_notify(XEvent *e)
{
    XCrossingEvent *ev = &e->xcrossing;
    struct client *c = get_client_from_window(ev->window);
    if (c == NULL || !c->decorated || ev->window != c->dec) return;
    if (c->hover) { c->hover = 0; draw_text(c, client_active(c)); }
    XDefineCursor(display, c->dec, normal_cursor);
}

static void
frame_button_press(struct client *c, int hit, XButtonPressedEvent *bev)
{
    XEvent ev; int over = 1;
    UNUSED(bev);
    c->pressed = hit; c->hover = hit; draw_text(c, client_active(c));
    if (XGrabPointer(display, c->dec, False, MOUSEMASK, GrabModeAsync, GrabModeAsync, None, normal_cursor, CurrentTime) != GrabSuccess) return;
    do {
        XMaskEvent(display, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
        if (ev.type == Expose || ev.type == MapRequest || ev.type == ConfigureRequest) { event_handler[ev.type](&ev); continue; }
        if (ev.type == MotionNotify) {
            int h = client_hit(c, ev.xmotion.x, ev.xmotion.y);
            int now = (h == hit); if (now != over) { over = now; c->pressed = now ? hit : 0; c->hover = now ? hit : 0; draw_text(c, client_active(c)); }
        }
    } while (ev.type != ButtonRelease);
    XUngrabPointer(display, CurrentTime);
    c->pressed = 0; c->hover = 0;
    if (!over) { draw_text(c, client_active(c)); return; }
    if (hit == HIT_CLOSE) client_close(c);
    else if (hit == HIT_MAX) { client_monocle(c); draw_text(c, client_active(c)); }
    else if (hit == HIT_MIN) client_minimize(c);
}

static void
frame_resize_drag(struct client *c, int hit, XButtonPressedEvent *bev)
{
    drag_run(c, hit, bev->x_root, bev->y_root, bev->button, false);
}

/* ---- the window menu (LUXWM.md "The window menu"): XP's system menu, drawn by menu.c ---- */
enum { SC_RESTORE_ID, SC_MOVE_ID, SC_SIZE_ID, SC_MINIMIZE_ID, SC_MAXIMIZE_ID, SC_SEP_ID, SC_CLOSE_ID, SC_DESKMENU_ID, SC_DESK_ID /* + desktop */ };

/* the caption icon is drawn (client or theme default icon, no NO_ICON, a caption of the frame's own) and (x, y) lies on it */
static bool
client_on_icon(struct client *c, int x, int y)
{
    if (!c->decorated || (c->ext_flags & (FRAME_NO_ICON | FRAME_CLIENT_CAPTION)) || !client_icon(c)) return false;
    int ix, iy, iw, ih; wm_frame_icon_rect(&theme, c->geom.width, c->mono, &ix, &iy, &iw, &ih);
    return x >= ix && x < ix + iw && y >= iy && y < iy + ih;
}

static void
menu_dispatch(XEvent *ev, void *arg)
{
    (void)arg;
    if (ev->type < LASTEvent && event_handler[ev->type]) event_handler[ev->type](ev);
}

/* a window menu command for the window w (it may have gone while the menu was open) */
static void
sysmenu_command(void *arg, int id)
{
    Window w = (Window)(uintptr_t)arg;
    struct client *c = get_client_from_window(w);
    if (c == NULL || c->window != w) return;
    LOGP("window menu: command %d on 0x%lx", id, w);
    if (c->minimized && (id == SC_RESTORE_ID || id == SC_MAXIMIZE_ID)) {   /* from the taskbar: back as it was, then maximised */
        if (c->ws != curr_ws) switch_ws(c->ws);
        client_restore(c); client_manage_focus(c);
        if (id == SC_RESTORE_ID) return;
    }
    switch (id) {
    case SC_RESTORE_ID: if (c->mono) { client_monocle(c); draw_text(c, client_active(c)); } break;
    case SC_MAXIMIZE_ID: if (!c->mono) { client_monocle(c); draw_text(c, client_active(c)); } break;
    case SC_MINIMIZE_ID: client_minimize(c); break;
    case SC_CLOSE_ID: client_close(c); break;
    case SC_MOVE_ID: drag_run(c, HIT_CAPTION, 0, 0, 0, true); break;
    case SC_SIZE_ID: if (!c->mono && !client_fixed(c)) drag_run(c, HIT_BR, 0, 0, 0, true); break;
    default:   /* Move to Desktop N: the window goes there and the desktop with it, as a desktop key during a move */
        if (id >= SC_DESK_ID && id < SC_DESK_ID + WORKSPACE_NUMBER && id - SC_DESK_ID != c->ws) {
            int ws = id - SC_DESK_ID;
            client_send_to_ws(c, ws); switch_ws(ws); client_manage_focus(c);
        }
        break;
    }
}

/* open the window menu of c with its top-left corner at x, y (root); flags WMMENU_KEYBOARD / WMMENU_FLIP; button: the button still
   held (0 none); from_icon: a double click on the caption icon closes the window. Greyed as xxxSetSysMenu (mnsys.c) greys: Restore
   unless maximised; Move, Size and Maximize while maximised; Size for a window whose size hints fix its size; a minimised window
   (the taskbar's menu): Move, Size and Minimize, Restore the default item. Close is the default item otherwise (bold), with its
   accelerator. */
static void
wm_menu_open(struct client *c, int x, int y, unsigned flags, unsigned button, bool from_icon)
{
    if (c == NULL || (c->hidden && !c->minimized)) return;   /* minimised: hidden too */
    Window w = c->window; void *arg = (void *)(uintptr_t)w;
    bool fixed = c->max_w > 0 && c->max_h > 0 && c->min_w == c->max_w && c->min_h == c->max_h, mini = c->minimized;
    unsigned mx = c->mono && !mini ? WMMENU_DISABLED : 0, mn = mini ? WMMENU_DISABLED : 0;
    /* luxwm's own: the desktops by number (Desktop 1 is desktop 0, as the Mod4+1 key), the window's own one checked */
    static char desk_text[WORKSPACE_NUMBER][24];
    struct wm_menu_item desk[WORKSPACE_NUMBER];
    for (int i = 0; i < WORKSPACE_NUMBER; i++) {
        if (i < 9) snprintf(desk_text[i], sizeof desk_text[i], "Desktop &%d", i + 1);
        else snprintf(desk_text[i], sizeof desk_text[i], "Desktop %d&%d", (i + 1) / 10, (i + 1) % 10);
        desk[i] = (struct wm_menu_item){ desk_text[i], NULL, i == c->ws ? WMMENU_CHECKED : 0, WMMENU_GLYPH_NONE, SC_DESK_ID + i, sysmenu_command, arg, NULL, 0 };
    }
    struct wm_menu_item it[] = {
        { "&Restore",  NULL,     mini ? WMMENU_DEFAULT : c->mono ? 0 : WMMENU_DISABLED, WMMENU_GLYPH_RESTORE, SC_RESTORE_ID, sysmenu_command, arg, NULL, 0 },
        { "&Move",     NULL,     mx | mn,                       WMMENU_GLYPH_NONE,     SC_MOVE_ID,     sysmenu_command, arg, NULL, 0 },
        { "&Size",     NULL,     mx | mn | (fixed ? WMMENU_DISABLED : 0), WMMENU_GLYPH_NONE, SC_SIZE_ID, sysmenu_command, arg, NULL, 0 },
        { "Mi&nimize", NULL,     mn,                            WMMENU_GLYPH_MINIMIZE, SC_MINIMIZE_ID, sysmenu_command, arg, NULL, 0 },
        { "Ma&ximize", NULL,     (mini ? 0 : mx) | (fixed ? WMMENU_DISABLED : 0), WMMENU_GLYPH_MAXIMIZE, SC_MAXIMIZE_ID, sysmenu_command, arg, NULL, 0 },
        { "Move to &Desktop", NULL, 0,                          WMMENU_GLYPH_NONE,     SC_DESKMENU_ID, NULL, NULL, desk, WORKSPACE_NUMBER },
        { NULL,        NULL,     WMMENU_SEPARATOR,              WMMENU_GLYPH_NONE,     SC_SEP_ID,      NULL, NULL, NULL, 0 },
        { "&Close",    "Alt+F4", mini ? 0 : WMMENU_DEFAULT,     WMMENU_GLYPH_CLOSE,    SC_CLOSE_ID,    sysmenu_command, arg, NULL, 0 },
    };
    struct wm_menu_env e = { .dpy = display, .screen = screen, .root = root, .visual = argb_visual, .cmap = argb_cmap, .depth = argb_depth,
                            .theme = &theme, .composited = compositor && argb_depth == 32, .dispatch = menu_dispatch };
    int mon = ws_m_list[c->ws];
    if (m_list && mon >= 0 && mon < m_count) { e.bx = m_list[mon].x; e.by = m_list[mon].y; e.bw = m_list[mon].width; e.bh = m_list[mon].height; }
    else { e.bx = 0; e.by = 0; e.bw = display_width; e.bh = display_height; }
    if (from_icon) {
        int ix, iy, iw, ih; wm_frame_icon_rect(&theme, c->geom.width, c->mono, &ix, &iy, &iw, &ih);
        e.dbl_x = c->geom.x + ix; e.dbl_y = c->geom.y + iy; e.dbl_w = iw; e.dbl_h = ih; e.dbl_ms = 500;
    }
    LOGP("window menu for 0x%lx at %d,%d%s", w, x, y, flags & WMMENU_KEYBOARD ? " (keyboard)" : "");
    int r = wm_menu_popup(&e, x, y, it, (int)(sizeof it / sizeof it[0]), flags, button);
    if (r == WMMENU_DBLCLICK) sysmenu_command(arg, SC_CLOSE_ID);
}

/* the window menu at its own place: below the caption at the frame's left border (Alt+Space, the caption icon) */
static void
wm_menu_at_caption(struct client *c, unsigned flags, unsigned button, bool from_icon)
{
    int l, r, t, b; margins(c, &l, &r, &t, &b);
    if (c->ext_flags & FRAME_CLIENT_CAPTION) wm_theme_margins_ex(&theme, c->mono, &l, &r, &t, &b);
    wm_menu_open(c, c->geom.x + l, c->geom.y + t, flags, button, from_icon);
}

/* ---- the frame drag, shared by edge/corner presses on the frame and _NET_WM_MOVERESIZE ----
   hit: HIT_CAPTION moves, HIT_L..HIT_BR resize that edge/corner. px, py: where the press was (root). button: the button whose
   release ends the drag (0: any). keyboard: _NET_WM_MOVERESIZE_{MOVE,SIZE}_KEYBOARD (arrows move the pointer, Return or a click
   ends, Escape cancels). A _NET_WM_MOVERESIZE cancel (11) or Escape puts the window back where it was. */
static Bool
drag_pred(Display *d, XEvent *ev, XPointer arg)
{
    (void)d; (void)arg;
    switch (ev->type) {
    case MotionNotify: case ButtonPress: case ButtonRelease: case KeyPress: case KeyRelease:
    case Expose: case MapRequest: case ConfigureRequest:
        return True;
    case ClientMessage:
        return ev->xclient.message_type == net_atom[NetWMMoveResizeDrag];
    }
    return sync_ev >= 0 && ev->type == sync_ev + XSyncAlarmNotify;
}

/* the next drag event (drag_pred), waiting at most timeout ms (-1: no limit); false on timeout. Frames exposed meanwhile are
   repainted before blocking. */
static bool
drag_next_event(XEvent *ev, int timeout)
{
    for (;;) {
        if (XCheckIfEvent(display, ev, drag_pred, NULL))
            return true;
        paint_dirty();
        XFlush(display);
        struct pollfd pfd = { .fd = ConnectionNumber(display), .events = POLLIN };
        if (poll(&pfd, 1, timeout) == 0)
            return false;
    }
}

static void drag_bg_probe(struct client *c, struct drag_cl *d, int nx, int ny, int nw, int nh);

static void
drag_resize_step(struct client *c, struct drag_cl *d, int hit, int ox, int oy, int ow, int oh, int dx, int dy)
{
    int nx = ox, ny = oy, nw = ow, nh = oh;
    bool left = hit == HIT_L || hit == HIT_TL || hit == HIT_BL, top = hit == HIT_T || hit == HIT_TL || hit == HIT_TR;
    if (left) nw = ow - dx;
    if (hit == HIT_R || hit == HIT_TR || hit == HIT_BR) nw = ow + dx;
    if (top) nh = oh - dy;
    if (hit == HIT_B || hit == HIT_BL || hit == HIT_BR) nh = oh + dy;
    if (nw < 120) nw = 120;
    if (nh < 60) nh = 60;
    /* size hints: the client gets base + n * increment (rounded down), min/max, aspect; the frame follows, and a left/top drag
       keeps the opposite edge where it was (LUXWM.md "Size hints") */
    { bool was = c->mono; c->mono = false; frame_fit(c, &nw, &nh); c->mono = was; }
    if (left) nx = ox + ow - nw;
    if (top) ny = oy + oh - nh;
    if (d && c->decorated) drag_bg_probe(c, d, nx, ny, nw, nh);
    if ((nx == c->geom.x && ny == c->geom.y) || !c->decorated) {
        if (nx != c->geom.x || ny != c->geom.y) client_move_absolute(c, nx, ny);
        client_resize_absolute(c, nw, nh);
        geom_probe = NULL;
        return;
    }
    /* left/top edges: position and size in one request each, sent with the frame's new pixels (not a move, then a resize) */
    client_unmax_margins(c);
    int ax, ay, aw, ah; client_area(c, nw, nh, &ax, &ay, &aw, &ah);
    struct geom_req g = { c, true, nx, ny, aw, ah, MAX(nw, MINIMUM_DIM), MAX(nh, MINIMUM_DIM) };
    c->geom.x = nx; c->geom.y = ny; c->geom.width = g.dw; c->geom.height = g.dh; c->mono = false;
    geom_paint(c, &g);
    geom_probe = NULL;
    ewmh_set_frame_extents(c);
    client_set_status(c);
}

/* ---- the client's side of an interactive resize (LUXWM.md "Resize without flicker") ----
   bit gravity: NorthWest while the drag lasts, so the server keeps the client's pixels on a resize (ForgetGravity would discard
   them and show the background until the client repaints); afterwards the client's own value again and, when that was Forget,
   one synthetic full Expose, so a client that repaints only exposed areas redraws the whole window once at its final size.
   _NET_WM_SYNC_REQUEST (EWMH basic frame sync): before each step that changes the client's size the client gets the request with
   the next counter value; the next step waits until the client has set its _NET_WM_SYNC_REQUEST_COUNTER to it (an XSync alarm),
   at most SYNC_WAIT ms, so the steps follow the client's paint rate instead of queueing up behind it.
   Background: what a client shows in the strip a resize adds, until it has painted it, is its window background. Most toolkits
   (GTK 3, Qt 5) use None, which under Composite means a copy of whatever the screen showed there (the old frame border, the
   desktop): that strip is the flicker. The first growing step finds out which kind the client is (drag_bg_probe); a client with
   background None gets the colour of its own right edge (sampled when the drag starts) as background pixel until the drag ends,
   then None again. A client with a background pixel (xterm, Xt) is left alone: its strip is already its own colour, and luxwm
   cannot read the old value back to restore it. */
#define SYNC_WAIT 50
struct drag_cl {
    bool gravity_set; int gravity;
    XSyncCounter counter; XSyncAlarm alarm; int64_t value; bool waiting; double sent;
    int bg;                  /* 0: not known yet, 1: was None, now the edge colour (None again at the end), 2: leave alone */
    unsigned long edge;      /* the client's right-edge pixel when the drag started */
    unsigned long screen[2]; /* drag_bg_probe: the screen pixels where the new strip will be */
    int px[2], py[2];        /* those points on the screen */
    int sx[2], sy[2];        /* and in client coordinates */
};

static bool
pixel_at(Window w, int x, int y, unsigned long *px)
{
    XImage *im = XGetImage(display, w, x, y, 1, 1, AllPlanes, ZPixmap);
    if (!im) return false;
    *px = XGetPixel(im, 0, 0); XDestroyImage(im);
    return true;
}

/* the first step that grows the client by 4 px or more tells a client with background None (its new strip is a copy of the
   screen) from one with a background pixel (the strip is that pixel). drag_bg_probe picks two points 3 px into the strip, i.e. on
   the old frame's glass border, not on its uniform inner line; drag_bg_sample reads the screen there before the resize and the
   client's strip after it, both inside geom_apply under wm_frame_paint_ex3's server grab, so neither the client nor the compositor
   can have drawn in between. Any doubt leaves the client alone. */
static void
drag_bg_probe(struct client *c, struct drag_cl *d, int nx, int ny, int nw, int nh)
{
    if (d->bg) return;
    int l, t, cw, ch, ncw, nch;
    client_area(c, c->geom.width, c->geom.height, &l, &t, &cw, &ch);
    client_area(c, nw, nh, &l, &t, &ncw, &nch);
    int mw = MIN(cw, ncw), mh = MIN(ch, nch);
    if (ncw - cw >= 4) { d->sx[0] = d->sx[1] = cw + 3; d->sy[0] = mh / 3; d->sy[1] = mh * 2 / 3; }   /* NorthWest gravity: the new */
    else if (nch - ch >= 4) { d->sy[0] = d->sy[1] = ch + 3; d->sx[0] = mw / 3; d->sx[1] = mw * 2 / 3; } /* columns right, rows below */
    else return;   /* not yet */
    for (int i = 0; i < 2; i++) {
        d->px[i] = nx + l + d->sx[i]; d->py[i] = ny + t + d->sy[i];
        if (d->px[i] < 0 || d->py[i] < 0 || d->px[i] >= DisplayWidth(display, screen) || d->py[i] >= DisplayHeight(display, screen)) { d->bg = 2; return; }
    }
    geom_probe = d;
}

static void
drag_bg_sample(struct client *c, struct drag_cl *d, bool after)
{
    if (!after) {
        for (int i = 0; i < 2; i++) if (!pixel_at(root, d->px[i], d->py[i], &d->screen[i])) { d->bg = 2; return; }
        return;
    }
    if (d->bg) return;
    for (int i = 0; i < 2; i++) {
        unsigned long strip = 0;
        if (!pixel_at(c->window, d->sx[i], d->sy[i], &strip) || strip != d->screen[i] || strip == d->edge) {
            LOGP("drag: client 0x%lx keeps its background (strip %06lx, screen %06lx, edge %06lx)", c->window, strip, d->screen[i], d->edge);
            d->bg = 2; return; }
    }
    LOGP("drag: client 0x%lx has background None: edge colour %06lx while the drag lasts", c->window, d->edge);
    XSetWindowBackground(display, c->window, d->edge);
    d->bg = 1;
}

static void
drag_client_begin(struct client *c, struct drag_cl *d)
{
    memset(d, 0, sizeof *d);
    XWindowAttributes wa;
    bool have_wa = XGetWindowAttributes(display, c->window, &wa);
    if (have_wa && wa.bit_gravity != NorthWestGravity && wa.bit_gravity != StaticGravity) {
        XSetWindowAttributes a = { .bit_gravity = NorthWestGravity };
        XChangeWindowAttributes(display, c->window, CWBitGravity, &a);
        d->gravity_set = true; d->gravity = wa.bit_gravity;
    }
    /* the edge colour (an opaque TrueColor client in the root's pixel format only: its pixels compare with the screen's) */
    d->bg = 2;
    if (have_wa && c->depth != 32 && wa.depth == DefaultDepth(display, screen) && wa.visual->class == TrueColor
        && wa.visual->red_mask == DefaultVisual(display, screen)->red_mask && wa.visual->blue_mask == DefaultVisual(display, screen)->blue_mask) {
        int ax, ay, cw, ch; client_area(c, c->geom.width, c->geom.height, &ax, &ay, &cw, &ch);
        if (cw > 2 && ch > 2 && pixel_at(c->window, cw - 2, ch / 2, &d->edge)) d->bg = 0;
    }
    if (sync_ev < 0) return;
    Atom *prot = NULL; int n = 0; bool has = false;
    if (XGetWMProtocols(display, c->window, &prot, &n)) {
        for (int i = 0; i < n; i++) if (prot[i] == net_atom[NetWMSyncRequest]) has = true;
        XFree(prot);
    }
    if (!has) return;
    Atom type; int fmt; unsigned long cnt, after; unsigned char *data = NULL;
    if (XGetWindowProperty(display, c->window, net_atom[NetWMSyncRequestCounter], 0, 2, False, XA_CARDINAL, &type, &fmt, &cnt, &after, &data) == Success
        && data && fmt == 32 && cnt >= 1)
        d->counter = (XSyncCounter)((unsigned long *)data)[0];   /* the basic counter (a second one is the extended one) */
    if (data) XFree(data);
    XSyncValue v;
    if (!d->counter || !XSyncQueryCounter(display, d->counter, &v)) { d->counter = None; return; }
    d->value = ((int64_t)XSyncValueHigh32(v) << 32) | XSyncValueLow32(v);
    XSyncAlarmAttributes aa; memset(&aa, 0, sizeof aa);
    aa.trigger.counter = d->counter; aa.trigger.value_type = XSyncAbsolute; aa.trigger.wait_value = v;
    aa.trigger.test_type = XSyncPositiveComparison; XSyncIntToValue(&aa.delta, 0); aa.events = True;
    d->alarm = XSyncCreateAlarm(display, XSyncCACounter | XSyncCAValueType | XSyncCAValue | XSyncCATestType | XSyncCADelta | XSyncCAEvents, &aa);
}

/* before a step that changes the client's size */
static void
drag_client_sync(struct client *c, struct drag_cl *d, Time when)
{
    if (!d->alarm) return;
    d->value++;
    XEvent ev; memset(&ev, 0, sizeof ev);
    ev.xclient.type = ClientMessage; ev.xclient.window = c->window; ev.xclient.message_type = wm_atom[WMProtocols]; ev.xclient.format = 32;
    ev.xclient.data.l[0] = (long)net_atom[NetWMSyncRequest]; ev.xclient.data.l[1] = (long)when;
    ev.xclient.data.l[2] = (long)(d->value & 0xffffffff); ev.xclient.data.l[3] = (long)((d->value >> 32) & 0xffffffff);
    XSendEvent(display, c->window, False, NoEventMask, &ev);
    XSyncAlarmAttributes aa; memset(&aa, 0, sizeof aa);
    XSyncIntsToValue(&aa.trigger.wait_value, (unsigned)(d->value & 0xffffffff), (int)(d->value >> 32));
    XSyncChangeAlarm(display, d->alarm, XSyncCAValue, &aa);
    d->waiting = true; d->sent = now_ms();
}

static void
drag_client_alarm(struct drag_cl *d, XEvent *ev)
{
    XSyncAlarmNotifyEvent *ae = (XSyncAlarmNotifyEvent *)ev;
    if (!d->alarm || ae->alarm != d->alarm) return;
    int64_t v = ((int64_t)XSyncValueHigh32(ae->counter_value) << 32) | XSyncValueLow32(ae->counter_value);
    if (v >= d->value) d->waiting = false;
}

static void
drag_client_end(struct client *c, struct drag_cl *d)
{
    if (d->alarm) XSyncDestroyAlarm(display, d->alarm);
    if (d->bg == 1) XSetWindowBackgroundPixmap(display, c->window, None);
    if (d->gravity_set) {
        XSetWindowAttributes a = { .bit_gravity = d->gravity };
        XChangeWindowAttributes(display, c->window, CWBitGravity, &a);
        if (d->gravity == ForgetGravity) {
            XEvent ev; memset(&ev, 0, sizeof ev);
            ev.xexpose.type = Expose; ev.xexpose.window = c->window; ev.xexpose.display = display;
            int ax, ay, aw, ah; client_area(c, c->geom.width, c->geom.height, &ax, &ay, &aw, &ah);
            ev.xexpose.width = MAX(aw, 1); ev.xexpose.height = MAX(ah, 1);
            XSendEvent(display, c->window, False, ExposureMask, &ev);
        }
    }
}

/* the hit (edge or corner) of the keyboard size mode's chosen edges: kx, ky -1 left/top, 1 right/bottom, 0 none */
static int
kb_hit(int kx, int ky)
{
    static const int tab[3][3] = { { HIT_TL, HIT_L, HIT_BL }, { HIT_T, HIT_NONE, HIT_B }, { HIT_TR, HIT_R, HIT_BR } };
    return tab[kx + 1][ky + 1];
}

/* the other way round: the edges of a border hit (HIT_L..HIT_BR) */
static void
kb_edges(int hit, int *kx, int *ky)
{
    *kx = hit == HIT_L || hit == HIT_TL || hit == HIT_BL ? -1 : hit == HIT_R || hit == HIT_TR || hit == HIT_BR ? 1 : 0;
    *ky = hit == HIT_T || hit == HIT_TL || hit == HIT_TR ? -1 : hit == HIT_B || hit == HIT_BL || hit == HIT_BR ? 1 : 0;
}

/* the keyboard move/size step (movesize.c: max(SM_CXSIZE / 2, 1), Ctrl 1 px): half a caption button, at least one size-hint
   increment when sizing */
static int
kb_step(struct client *c, bool ctrl, bool size, bool horz)
{
    if (ctrl) return 1;
    int s = MAX((theme.mode == THEME_MSSTYLES ? theme.ms_cap_h : theme.m_t) / 2, 1);
    if (size) s = MAX(s, horz ? c->inc_w : c->inc_h);
    return s;
}

/* the desktop a "workspace N" key binding switches to for this key and modifier state, -1 when it is no such binding */
static int
key_workspace(KeySym k, unsigned state)
{
    unsigned mods = mod_clean(state);
    for (int i = 0; i < nkeys; i++)
        if (keys[i].sym == k && keys[i].mods == mods && !strcmp(keys[i].action, "workspace")) {
            int ws = atoi(keys[i].arg);
            return ws >= 0 && ws < WORKSPACE_NUMBER ? ws : -1;
        }
    return -1;
}

static void
drag_run(struct client *c, int hit, int px, int py, unsigned button, bool keyboard)
{
    XEvent ev; int ox = c->geom.x, oy = c->geom.y, ow = c->geom.width, oh = c->geom.height; bool was_max = c->mono, cancel = false;
    const int dg_x = ox, dg_y = oy, dg_w = ow, dg_h = oh;   /* where Escape / a cancel puts the window back (ox.. are rebased in size mode) */
    const int was_snap = c->snap;
    /* snap (mouse drags only): the target under the pointer (shown as an outline, applied on the release); the rectangle a snapped or
       maximised window comes back to */
    int snap = SNAP_NONE; struct client_geom rest = { dg_x, dg_y, dg_w, dg_h };
    /* keyboard size (Alt+Space, S; _NET_WM_MOVERESIZE_SIZE_KEYBOARD) starts with no edge, as Windows' WMSZ_KEYSIZE: the first arrow
       picks the edge (left/right, top/bottom; two arrows a corner) and puts the pointer on it, the next ones move it; or the mouse
       picks the edges it moves towards. Keyboard move (Alt+Space, M; ..._MOVE_KEYBOARD) moves at once; the mouse takes over too */
    bool kbsize = keyboard && hit != HIT_CAPTION;
    int kx = 0, ky = 0, rx0 = 0, ry0 = 0, vx = 0, vy = 0;   /* chosen edges; pointer before the mode (put back on Return/Escape); last pointer */
    int first_ax = 0;           /* size mode: the axis of the edge picked first (1 x, 2 y; 0 none or a corner at once): Shift toggles back to it */
    bool held[4] = { false };   /* keyboard modes: the arrows held down (Left, Right, Up, Down): two of them move diagonally */
    bool corner_armed = true;   /* size mode: an edge's pointer reaching its corner takes the corner (not right after Shift went back to the edge) */
    Bool repeat_was = False;
    if (kbsize) hit = HIT_NONE;
    Cursor cur = hit == HIT_CAPTION || hit == HIT_NONE ? move_cursor : edge_cursor[hit];
    int grabbed = GrabNotViewable;
    /* a client sending _NET_WM_MOVERESIZE should have dropped its implicit grab first; give it a moment if not */
    for (int i = 0; i < 20 && (grabbed = XGrabPointer(display, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync, None, cur, CurrentTime)) == AlreadyGrabbed; i++)
        usleep(5000);
    if (grabbed != GrabSuccess) { LOGP("drag: pointer grab failed (%d)", grabbed); return; }
    if (keyboard) {
        int kg = GrabNotViewable;
        for (int i = 0; i < 20 && (kg = XGrabKeyboard(display, root, False, GrabModeAsync, GrabModeAsync, CurrentTime)) != GrabSuccess; i++) usleep(5000);
        if (kg != GrabSuccess) { XUngrabPointer(display, CurrentTime); return; }
        repeat_was = XkbGetDetectableAutoRepeat(display, NULL);
        XkbSetDetectableAutoRepeat(display, True, NULL);   /* a held arrow repeats as presses only, its release is real */
        Window rw, cw; int wx, wy; unsigned m; XQueryPointer(display, root, &rw, &cw, &rx0, &ry0, &wx, &wy, &m);
        /* movesize.c: the pointer goes to the middle of the caption (move) or of the window (size) */
        int l, r, t, b; margins(c, &l, &r, &t, &b);
        px = c->geom.x + c->geom.width / 2;
        py = hit == HIT_CAPTION ? c->geom.y + MAX(t, 2) / 2 : c->geom.y + c->geom.height / 2;
        XWarpPointer(display, None, root, 0, 0, 0, 0, px, py);
        XSync(display, False); while (XCheckTypedEvent(display, MotionNotify, &ev)) ;
    } else {
        /* the button may already be up again (a quick click): then there is no drag */
        Window rw, cw; int rx, ry, wx, wy; unsigned mask = 0;
        XQueryPointer(display, root, &rw, &cw, &rx, &ry, &wx, &wy, &mask);
        unsigned want = button >= 1 && button <= 5 ? (Button1Mask << (button - 1)) : (Button1Mask|Button2Mask|Button3Mask|Button4Mask|Button5Mask);
        if (!(mask & want)) { XUngrabPointer(display, CurrentTime); return; }
    }
    vx = px; vy = py;
    LOGP("drag %s hit=%d from %d,%d button %u%s", hit == HIT_CAPTION ? "move" : "resize", hit, px, py, button, keyboard ? " (keyboard)" : "");
    double p_start = now_ms(), p_step = 0, p_max = 0; int p_n = 0, p_ev = 0; memset(wm_frame_prof_ms, 0, sizeof wm_frame_prof_ms);
    /* motion is coalesced (only the latest position counts) and applied at most every DRAG_INTERVAL ms: each step resizes the
       frame and client and repaints the frame, so a 1000 Hz mouse would otherwise queue far more work than the screen can show.
       A position held back is applied when the interval is over or the drag ends (LUXWM.md "Performance"). */
    bool pend = false; int pend_x = 0, pend_y = 0; double last_step = 0; Time pend_time = CurrentTime;
    bool moved = keyboard || hit != HIT_CAPTION;   /* mouse moves start past DRAG_THRESHOLD (see handle_button_press) */
    bool kb_end = false;                           /* ended (or cancelled) from the keyboard: the pointer goes back */
    struct drag_cl dc; memset(&dc, 0, sizeof dc);
    if (hit != HIT_CAPTION) drag_client_begin(c, &dc);
    /* one step: the held position; a resize that changes the client's size asks it for a sync first */
    #define DRAG_STEP() do { \
        if (hit == HIT_CAPTION) drag_move_step(c, ox + pend_x - px, oy + pend_y - py); \
        else if (hit != HIT_NONE) { int w0 = c->geom.width, h0 = c->geom.height, x0 = c->geom.x, y0 = c->geom.y; \
               drag_client_sync(c, &dc, pend_time); \
               drag_resize_step(c, &dc, hit, ox, oy, ow, oh, pend_x - px, pend_y - py); \
               if (c->geom.width == w0 && c->geom.height == h0 && c->geom.x == x0 && c->geom.y == y0) dc.waiting = false; } } while (0)
    /* size mode: edges picked (by keys or the mouse): the drag starts again from the current rectangle with the anchor at ax, ay */
    #define KB_PICK(nkx, nky, ax, ay) do { \
        if (pend) { DRAG_STEP(); pend = false; } \
        if (nkx) kx = nkx; \
        if (nky) ky = nky; \
        if (!first_ax) first_ax = kx && !ky ? 1 : ky && !kx ? 2 : 0; \
        hit = kb_hit(kx, ky); ox = c->geom.x; oy = c->geom.y; ow = c->geom.width; oh = c->geom.height; \
        px = vx = (ax); py = vy = (ay); \
        XChangeActivePointerGrab(display, MOUSEMASK, edge_cursor[hit], CurrentTime); } while (0)
    /* size mode: from one edge to the corner nkx, nky (Shift, or the pointer at the end of the edge): the pointer is put on the corner
       and both edges follow it from there */
    #define KB_CORNER(nkx, nky) do { \
        if (pend) { DRAG_STEP(); pend = false; } \
        kx = (nkx); ky = (nky); \
        hit = kb_hit(kx, ky); ox = c->geom.x; oy = c->geom.y; ow = c->geom.width; oh = c->geom.height; \
        px = vx = kx < 0 ? c->geom.x : c->geom.x + c->geom.width - 1; py = vy = ky < 0 ? c->geom.y : c->geom.y + c->geom.height - 1; \
        XChangeActivePointerGrab(display, MOUSEMASK, edge_cursor[hit], CurrentTime); \
        XWarpPointer(display, None, root, 0, 0, 0, 0, vx, vy); \
        XSync(display, False); while (XCheckTypedEvent(display, MotionNotify, &ev)) ; } while (0)
    for (bool done = false; !done; ) {
        int wait = -1;
        if (pend) {
            double ready = last_step + DRAG_INTERVAL;
            if (dc.waiting && dc.sent + SYNC_WAIT > ready) ready = dc.sent + SYNC_WAIT;
            wait = (int)(ready - now_ms() + 0.999); if (wait < 0) wait = 0;
        }
        bool timeout = !drag_next_event(&ev, wait);   /* interval (or the sync wait) over: apply the held position */
        if (timeout) { dc.waiting = false; ev.type = 0; }
        switch (ev.type) {
        case 0: break;
        case Expose: case MapRequest: case ConfigureRequest:
            event_handler[ev.type](&ev);
            break;
        case ClientMessage:   /* another _NET_WM_MOVERESIZE during the drag: 11 cancels it, anything else ends it */
            cancel = ev.xclient.data.l[2] == 11; done = true; kb_end = keyboard;
            break;
        case ButtonPress:
            if (keyboard) done = true;   /* a click accepts (the pointer stays where it is) */
            break;
        case ButtonRelease:
            if (!keyboard && (button == 0 || ev.xbutton.button == button)) done = true;
            break;
        case KeyRelease: {
            KeySym k = XkbKeycodeToKeysym(display, ev.xkey.keycode, 0, 0);
            int a = k == XK_Left || k == XK_KP_Left ? 0 : k == XK_Right || k == XK_KP_Right ? 1 : k == XK_Up || k == XK_KP_Up ? 2 : k == XK_Down || k == XK_KP_Down ? 3 : -1;
            if (a >= 0) held[a] = false;
            break;
        }
        case KeyPress: {
            KeySym k = XkbKeycodeToKeysym(display, ev.xkey.keycode, 0, 0);
            int to_ws = key_workspace(k, ev.xkey.state);
            if (to_ws >= 0 && hit == HIT_CAPTION) {   /* a desktop key while moving: the window goes along to that desktop */
                if (to_ws != c->ws) {
                    LOGP("drag: 0x%lx moved along to desktop %d", c->window, to_ws);
                    client_send_to_ws(c, to_ws); switch_ws(to_ws); client_manage_focus(c);
                }
                break;
            }
            if (k == XK_Escape) { cancel = true; done = true; kb_end = keyboard; }
            else if (keyboard && (k == XK_Return || k == XK_KP_Enter)) { done = true; kb_end = true; }
            else if (kbsize && (k == XK_Shift_L || k == XK_Shift_R)) {
                /* luxwm's own: Shift toggles between the edge and the corner next to it (the end nearer the pointer), and back to the
                   edge picked first; the pointer is put on the new edge or corner, and the drag continues from there */
                if (hit == HIT_NONE) break;
                if (kx && ky) {   /* back to the edge picked first; the pointer stays where it is */
                    if (pend) { DRAG_STEP(); pend = false; }
                    if (first_ax == 2) kx = 0; else ky = 0;
                    hit = kb_hit(kx, ky); ox = c->geom.x; oy = c->geom.y; ow = c->geom.width; oh = c->geom.height;
                    px = vx; py = vy; corner_armed = false;   /* the pointer is still on the corner: not straight back to it */
                    XChangeActivePointerGrab(display, MOUSEMASK, edge_cursor[hit], CurrentTime);
                }
                else if (kx) KB_CORNER(kx, vy < c->geom.y + c->geom.height / 2 ? -1 : 1);
                else KB_CORNER(vx < c->geom.x + c->geom.width / 2 ? -1 : 1, ky);
                LOGP("size mode: Shift -> %s", kx && ky ? "corner" : "edge");
            }
            else if (keyboard) {
                int dx = k == XK_Left || k == XK_KP_Left ? -1 : k == XK_Right || k == XK_KP_Right ? 1 : 0;
                int dy = k == XK_Up || k == XK_KP_Up ? -1 : k == XK_Down || k == XK_KP_Down ? 1 : 0;
                if (!dx && !dy) break;
                held[dx < 0 ? 0 : dx > 0 ? 1 : dy < 0 ? 2 : 3] = true;
                /* an arrow of the other axis that is still held joins in: Right and Down held together size (or move) diagonally */
                if (!dx) dx = held[1] - held[0];
                if (!dy) dy = held[3] - held[2];
                bool ctrl = (ev.xkey.state & ControlMask) != 0;
                int nkx = kbsize && !kx ? dx : 0, nky = kbsize && !ky ? dy : 0;
                if (nkx || nky) {   /* a new edge (both: a corner): the pointer onto it, nothing moves yet (movesize.c NoOffset) */
                    int ax = nkx ? (nkx < 0 ? c->geom.x : c->geom.x + c->geom.width - 1) : vx;
                    int ay = nky ? (nky < 0 ? c->geom.y : c->geom.y + c->geom.height - 1) : vy;
                    KB_PICK(nkx, nky, ax, ay);
                } else {
                    vx += dx * kb_step(c, ctrl, kbsize, true); vy += dy * kb_step(c, ctrl, kbsize, false);
                    pend = true; pend_x = vx; pend_y = vy; pend_time = ev.xkey.time;
                }
                /* the pointer follows (so the mouse continues from here); motion already queued from before is stale */
                XWarpPointer(display, None, root, 0, 0, 0, 0, vx, vy);
                XSync(display, False); while (XCheckTypedEvent(display, MotionNotify, &ev)) ;
            }
            break;
        }
        case MotionNotify:
            p_ev++; while (XCheckTypedEvent(display, MotionNotify, &ev)) p_ev++;   /* only the latest position */
            if (kbsize && hit == HIT_NONE) {
                /* the mouse picks the edges: the border or corner it is on (movesize.c: a hit test on every move in WMSZ_KEYSIZE), or
                   the ones it moves towards, from where it is (no jump): past 5 x DRAG_THRESHOLD (20 px), both edges of a corner when the
                   move is within about 23 degrees of the diagonal (the shorter axis at least 0.4 of the longer) */
                int mx = ev.xmotion.x_root - px, my = ev.xmotion.y_root - py, m = MAX(abs(mx), abs(my)), nkx = 0, nky = 0;
                int fx = ev.xmotion.x_root - c->geom.x, fy = ev.xmotion.y_root - c->geom.y;
                int on = fx >= 0 && fy >= 0 && fx < c->geom.width && fy < c->geom.height ? client_hit(c, fx, fy) : HIT_NONE;
                if (on >= HIT_L && on <= HIT_BR) kb_edges(on, &nkx, &nky);
                else if (m > 5 * DRAG_THRESHOLD) {
                    if (5 * abs(mx) >= 2 * m) nkx = mx < 0 ? -1 : 1;
                    if (5 * abs(my) >= 2 * m) nky = my < 0 ? -1 : 1;
                }
                if (nkx || nky) {   /* the pointer onto the picked edge (or corner): the edge follows it from there */
                    int ax = nkx ? (nkx < 0 ? c->geom.x : c->geom.x + c->geom.width - 1) : ev.xmotion.x_root;
                    int ay = nky ? (nky < 0 ? c->geom.y : c->geom.y + c->geom.height - 1) : ev.xmotion.y_root;
                    KB_PICK(nkx, nky, ax, ay);
                    XWarpPointer(display, None, root, 0, 0, 0, 0, vx, vy);
                    XSync(display, False); while (XCheckTypedEvent(display, MotionNotify, &ev)) ;
                }
                break;
            }
            if (!moved && abs(ev.xmotion.x_root - px) <= DRAG_THRESHOLD && abs(ev.xmotion.y_root - py) <= DRAG_THRESHOLD)
                break;
            if (!moved && !keyboard && hit == HIT_CAPTION && (c->mono || c->snap)) {   /* dragged away: the earlier size again */
                rest = c->snap ? c->snap_prev : c->prev;
                snap_tear_off(c, ev.xmotion.x_root, ev.xmotion.y_root);
                ox = c->geom.x; oy = c->geom.y; px = ev.xmotion.x_root; py = ev.xmotion.y_root;
            }
            moved = true;
            vx = ev.xmotion.x_root; vy = ev.xmotion.y_root;
            pend = true; pend_x = vx; pend_y = vy; pend_time = ev.xmotion.time;
            if (kbsize && !kx != !ky) {
                /* luxwm's own: a pointer that slides along the one edge it sizes to that edge's corner (the frame's corner hit area,
                   or past the end) gets the corner, as Shift: put on it, both edges follow; after Shift went back to the edge, only once
                   the pointer has left the corner */
                int fx = kx ? (kx < 0 ? 0 : c->geom.width - 1) : MIN(MAX(vx - c->geom.x, 0), c->geom.width - 1);
                int fy = ky ? (ky < 0 ? 0 : c->geom.height - 1) : MIN(MAX(vy - c->geom.y, 0), c->geom.height - 1);
                int on = client_hit(c, fx, fy), ex, ey;
                kb_edges(on, &ex, &ey);
                bool at_corner = on >= HIT_TL && on <= HIT_BR && (kx ? ex == kx : ey == ky);
                if (!at_corner) corner_armed = true;
                else if (corner_armed) {
                    KB_CORNER(ex, ey);
                    LOGP("size mode: the pointer reached corner %d", hit);
                    break;
                }
            }
            if (!keyboard) {
                int t = snap_target(c, hit, vx, vy);
                if (t != snap) {
                    snap = t;
                    if (t == SNAP_NONE) snap_preview(0, 0, 0, 0);
                    else { int x, y, w, h; snap_rect(c, t, &x, &y, &w, &h); snap_preview(x, y, w, h); }
                }
            }
            break;
        default:   /* XSyncAlarmNotify: the client has drawn the last step */
            drag_client_alarm(&dc, &ev);
            break;
        }
        if (pend && !done) {   /* apply the held position once the interval is over and the client has caught up */
            double t0 = now_ms();
            if (t0 - last_step < DRAG_INTERVAL || (dc.waiting && t0 - dc.sent < SYNC_WAIT)) continue;
            pend = false; last_step = t0;
            DRAG_STEP();
            XFlush(display);
            if (prof) { XSync(display, False); double d = now_ms() - t0; p_step += d; if (d > p_max) p_max = d; p_n++; }
        }
        if (done && pend && !cancel) {   /* the last position, if it was held back */
            DRAG_STEP();
            p_n += prof ? 1 : 0;
        }
    }
    #undef KB_PICK
    #undef KB_CORNER
    #undef DRAG_STEP
    if (snap != SNAP_NONE) snap_preview(0, 0, 0, 0);
    if (kbsize || hit != HIT_CAPTION) drag_client_end(c, &dc);
    if (prof && p_n)
        fprintf(stderr, "luxwm prof: drag %s %.0f ms, %d motion events, %d steps, step avg %.2f max %.2f ms (incl. XSync); "
                "%.0f frame_paints, each: paint %.2f shape %.2f upload %.2f ms\n", hit == HIT_CAPTION ? "move" : "resize",
                now_ms() - p_start, p_ev, p_n, p_step / p_n, p_max, wm_frame_prof_ms[3], wm_frame_prof_ms[0] / wm_frame_prof_ms[3], wm_frame_prof_ms[1] / wm_frame_prof_ms[3], wm_frame_prof_ms[2] / wm_frame_prof_ms[3]);
    if (kb_end) XWarpPointer(display, None, root, 0, 0, 0, 0, rx0, ry0);   /* movesize.c: ptRestore when the keyboard ended it */
    XUngrabPointer(display, CurrentTime);
    if (keyboard) { XUngrabKeyboard(display, CurrentTime); if (!repeat_was) XkbSetDetectableAutoRepeat(display, False, NULL); }
    if (cancel) {
        LOGN("drag cancelled");
        if (was_max) { if (!c->mono) client_apply_max(c); }
        else if (c->geom.x != dg_x || c->geom.y != dg_y || c->geom.width != dg_w || c->geom.height != dg_h) {
            client_move_absolute(c, dg_x, dg_y); client_resize_absolute(c, dg_w, dg_h); }
        if (was_snap && !c->snap) { c->snap = was_snap; ewmh_update_state(c); }   /* a torn-off snap: snapped again (snap_prev is kept) */
    } else if (snap != SNAP_NONE) {
        client_snap_to(c, snap, rest);
    } else if (hit != HIT_CAPTION && c->snap && c->geom.height != dg_h) {
        c->snap = SNAP_NONE; ewmh_update_state(c);   /* resized off the full height: no longer snapped */
    }
    XFlush(display);
}

/* _LHWM_EXTEND_FRAME / _LHWM_FRAME_FLAGS of a client (CLIENT-PROTOCOL.md) */
static void
client_read_ext(struct client *c)
{
    Atom ty; int fmt; unsigned long n, after; unsigned char *d = NULL;
    c->has_ext = false; c->ext_flags = 0; memset(c->ext, 0, sizeof c->ext);
    if (XGetWindowProperty(display, c->window, lhwm_ext_atom, 0, 4, False, XA_CARDINAL, &ty, &fmt, &n, &after, &d) == Success && d) {
        if (fmt == 32 && n == 4) { for (int i = 0; i < 4; i++) c->ext[i] = ((unsigned long *)d)[i] & 0xFFFFFFFFUL; c->has_ext = true; }
        XFree(d); d = NULL;
    }
    if (XGetWindowProperty(display, c->window, lhwm_flags_atom, 0, 1, False, XA_CARDINAL, &ty, &fmt, &n, &after, &d) == Success && d) {
        if (fmt == 32 && n == 1) c->ext_flags = ((unsigned long *)d)[0] & 0xFFFFUL;   /* the client's flags (the higher bits are luxwm's) */
        XFree(d);
    }
    LOGP("client 0x%lx extend %lu %lu %lu %lu flags %lu", c->window, c->ext[0], c->ext[1], c->ext[2], c->ext[3], c->ext_flags);
}

/* root _LHWM_COMPOSITED (1: DWM glass frames, extended frames honoured) and _LHWM_FRAME_METRICS (CARDINAL[19]: caption_h border_l
   border_r border_b text_rgb glow_rgb inactive_text_rgb 0 max_caption_h max_border_l max_border_r
   max_border_b border_t max_border_t max_text_rgb max_inactive_text_rgb max_glow_rgb 0 deco_h; CLIENT-PROTOCOL.md); written when they change; extended clients are repainted when
   composition comes or goes (the extension is then ignored / honoured again) */
#define METRICS_N 19
static void
composited_publish(void)
{
    static long last_m[METRICS_N]; static bool have_m = false;
    bool force = theme_announce; theme_announce = false;   /* one PropertyNotify of each per theme switch (6.1) */
    int comp = compositor && theme.mode == THEME_DWM;
    int l, r, t, b; wm_theme_margins(&theme, &l, &r, &t, &b);
    #define RGB(v) ((long)((int)((v)[0] * 255 + 0.5) << 16 | (int)((v)[1] * 255 + 0.5) << 8 | (int)((v)[2] * 255 + 0.5)))
    double glow[3] = { 0, 0, 0 };
    if (theme.mode == THEME_DWM) memcpy(glow, theme.glow, sizeof glow);
    else if (theme.ms && theme.ms[MP_CAPTION].sh_type[0]) memcpy(glow, theme.ms[MP_CAPTION].sh_rgb[0], sizeof glow);
    int ml, mr, mt, mb; wm_theme_margins_ex(&theme, 1, &ml, &mr, &mt, &mb);   /* maximised: DWM caption with no borders, msstyles MAXCAPTION */
    long m[METRICS_N] = { t, l, r, b, RGB(theme.text_a), RGB(glow), RGB(theme.text_i), 0,
                          mt, ml, mr, mb,                                     /* the maximised frame margins */
                          wm_theme_caption_border(&theme, 0), wm_theme_caption_border(&theme, 1),   /* top border kept above a CLIENT_CAPTION client */
                          RGB(theme.text_ma), RGB(theme.text_mi), 0, 0,     /* maximised caption text (text_max_*; default the normal ones) */
                          theme.mode == THEME_DWM && theme.deco_h > 0 ? theme.deco_h : t };   /* 18: deco_h (vision: 15), else the caption */
    /* 16: the maximised glow (glow_max; 0xFFFFFFFF = none); msstyles: MAXCAPTION's text shadow, else CAPTION's (field 5) */
    if (theme.mode == THEME_DWM) m[16] = theme.glow_m_none ? 0xFFFFFFFFL : RGB(theme.glow_m);
    else if (theme.ms && theme.ms[MP_MAXCAPTION].sh_type[0]) m[16] = RGB(theme.ms[MP_MAXCAPTION].sh_rgb[0]);
    else m[16] = m[5];
    #undef RGB
    if (force || !have_m || memcmp(m, last_m, sizeof m)) {
        XChangeProperty(display, root, lhwm_metrics_atom, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)m, METRICS_N);
        memcpy(last_m, m, sizeof m); have_m = true;
    }
    if (comp == comp_written && !force)
        return;
    bool repaint = comp_written != -1 && comp != comp_written;
    comp_written = comp;
    long v = comp;
    XChangeProperty(display, root, lhwm_comp_atom, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&v, 1);
    fprintf(stderr, "luxwm: _LHWM_COMPOSITED = %d%s\n", comp, force ? " (theme switch)" : "");
    if (repaint)
        for (int i = 0; i < WORKSPACE_NUMBER; i++)
            for (struct client *c = c_list[i]; c != NULL; c = c->next)
                if (c->has_ext || c->ext_flags) draw_text(c, client_active(c));
}

/* ---- animations (LUXWM.md "Animations", config.h ANIMATE) ---- */

/* _LHWM_ANIMATE on the client and its frame: anim_style (settings_load; picom-glass.conf picks the rule set by the value: 1 = 4074,
   5 = 5048, 3 = invented, 7 = 4074PLUS; ANIM_NONE deletes it), ANIM_BUSY (2, ANIM_INVENTED only) = the geometry change of a maximise/restore that is starting. Only with ANIMATE. */
static void
anim_mark(struct client *c, long v)
{
    if (!ANIMATE)
        return;
    if (v == ANIM_NONE) {   /* "none": no property at all, so no picom rule matches */
        XDeleteProperty(display, c->window, lhwm_anim_atom);
        if (c->decorated && c->dec != None) XDeleteProperty(display, c->dec, lhwm_anim_atom);
        return;
    }
    XChangeProperty(display, c->window, lhwm_anim_atom, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&v, 1);
    if (c->decorated && c->dec != None)
        XChangeProperty(display, c->dec, lhwm_anim_atom, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&v, 1);
}

/* an animation style name (../SETTINGS.md: 4074plus | 4074 | invented | 5048 | none; a number is taken as the value) -> its
   _LHWM_ANIMATE value, -1 when unknown */
static long
anim_style_parse(const char *v)
{
    static const struct { const char *name; long val; } names[] = {
        { "4074plus", ANIM_4074PLUS }, { "4074", ANIM_4074 }, { "invented", ANIM_INVENTED }, { "5048", ANIM_5048 }, { "none", ANIM_NONE } };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (!strcasecmp(v, names[i].name)) return names[i].val;
    char *end; long n = strtol(v, &end, 10);
    if (*v && !*end && (n == ANIM_NONE || n == ANIM_4074 || n == ANIM_INVENTED || n == ANIM_5048 || n == ANIM_4074PLUS)) return n;
    return -1;
}

static const char *
anim_style_name(long v)
{
    return v == ANIM_4074PLUS ? "4074plus" : v == ANIM_4074 ? "4074" : v == ANIM_INVENTED ? "invented" : v == ANIM_5048 ? "5048" : v == ANIM_NONE ? "none" : "?";
}

/* the animation style from the theme's Glass.animations, read at startup, on a theme change and on every
   _LH_SETTINGS_CHANGED; LHWM_ANIMATIONS=<name> overrides it, config.h ANIMATE_STYLE is the default. A change
   rewrites _LHWM_ANIMATE on every client and decoration window, so picom's rules switch live (a window in a running ANIM_INVENTED
   geometry animation gets the new value when that ends). */
static void
settings_load(bool startup)
{
    /* the theme's Glass.animations (docs/THEME-FORMAT.md; luxwm's own default is config.h's); LHWM_ANIMATIONS wins */
    char val[64] = "", src[1100] = "config.h";
    if (theme_anim[0]) { snprintf(val, sizeof val, "%s", theme_anim); snprintf(src, sizeof src, "theme %s", lh_theme_cur); }
    const char *env = getenv("LHWM_ANIMATIONS");
    if (env && *env) { snprintf(val, sizeof val, "%s", env); snprintf(src, sizeof src, "LHWM_ANIMATIONS"); }
    long v = ANIMATE_STYLE;
    if (val[0]) {
        long p = anim_style_parse(val);
        if (p < 0) fprintf(stderr, "luxwm: unknown animations = %s (%s), using %s\n", val, src, anim_style_name(ANIMATE_STYLE));
        else v = p;
    }
    if (!startup && v == anim_style) return;
    fprintf(stderr, "luxwm: animations %s (_LHWM_ANIMATE %ld) from %s\n", anim_style_name(v), v, val[0] && anim_style_parse(val) >= 0 ? src : "config.h");
    bool changed = v != anim_style;
    anim_style = v;
    if (!changed) return;
    for (int i = 0; i < WORKSPACE_NUMBER; i++)
        for (struct client *c = c_list[i]; c; c = c->next)
            if (c->anim_until <= 0) anim_mark(c, anim_style);
    XFlush(display);
}

/* reset _LHWM_ANIMATE = 2 marks whose time is over; returns ms until the next one is due, -1 for none */
static int
anim_expire(void)
{
    double now = now_ms(), next = -1;
    for (int i = 0; i < WORKSPACE_NUMBER; i++)
        for (struct client *c = c_list[i]; c; c = c->next) {
            if (c->anim_until <= 0) continue;
            if (c->anim_until <= now) { c->anim_until = 0; anim_mark(c, anim_style); continue; }
            if (next < 0 || c->anim_until - now < next) next = c->anim_until - now;
        }
    return next < 0 ? -1 : (int)next + 1;
}

/* the maximise crossfade (LUXWM.md "Maximised mode"): the weight of the maximised plate for wm_frame_paint_ex3, linear over XFADE_MS like luxshell's
   black layers (runtime.lua: lh.animate(..., 400), linear, 25 ms tick); -1 = by state (no fade running; a finished one is cleared) */
static double
xfade_weight(struct client *c)
{
    if (c->xfade_t0 <= 0)
        return -1;
    double p = (now_ms() - c->xfade_t0) / XFADE_MS;
    if (p >= 1) { c->xfade_t0 = 0; return -1; }
    if (p < 0) p = 0;
    return c->xfade_to_max ? p : 1 - p;
}

/* repaint the frames whose crossfade frame is due (every XFADE_TICK ms; the last one, at XFADE_MS, paints the final plate); returns
   ms until the next one, -1 for none */
static int
xfade_tick(void)
{
    double now = now_ms(), next = -1;
    for (int i = 0; i < WORKSPACE_NUMBER; i++)
        for (struct client *c = c_list[i]; c; c = c->next) {
            if (c->xfade_t0 <= 0) continue;
            if (c->hidden || !c->decorated || c->dec == None) { c->xfade_t0 = 0; continue; }
            if (now >= c->xfade_next) {
                double end = c->xfade_t0 + XFADE_MS;
                c->xfade_next = MIN(c->xfade_t0 + ((long)((now - c->xfade_t0) / XFADE_TICK) + 1) * XFADE_TICK, end);   /* on the 25 ms grid */
                draw_text(c, client_active(c));   /* clears xfade_t0 once the time is over */
                if (c->xfade_t0 <= 0) continue;
            }
            if (next < 0 || c->xfade_next - now < next) next = c->xfade_next - now;
        }
    return next < 0 ? -1 : (int)next + 1;
}

/* where a minimised window goes: its taskbar button (_NET_WM_ICON_GEOMETRY, set by luxshell), else the bottom-left of the work
   area (a 160 x 24 rectangle sitting on its bottom edge) */
static void
anim_icon_rect(struct client *c, int *x, int *y, int *w, int *h)
{
    Atom ty; int fmt; unsigned long n = 0, after; unsigned char *d = NULL;
    if (XGetWindowProperty(display, c->window, net_icon_geom_atom, 0, 4, False, XA_CARDINAL, &ty, &fmt, &n, &after, &d) == Success && d) {
        long *g = (long *)d;
        if (fmt == 32 && n == 4 && g[2] > 0 && g[3] > 0) { *x = (int)g[0]; *y = (int)g[1]; *w = (int)g[2]; *h = (int)g[3]; XFree(d); return; }
        XFree(d);
    }
    int wx, wy, ww, wh;
    workarea_mon(ws_m_list[c->ws], &wx, &wy, &ww, &wh);
    *x = wx; *y = wy + wh - 24; *w = 160; *h = 24;
}

/* Windows' MulDiv: a * b / c rounded to nearest */
static int muldiv(int a, int b, int c) { double v = (double)a * b / c; return (int)(v < 0 ? v - 0.5 : v + 0.5); }

/* Without a compositor: XP's minimise/restore/maximise animation, drawn on the root window.
   XP (windows/core/ntuser/kernel/minmax.c) only ever uses IDANI_CAPTION: xxxDrawAnimatedRects -> xxxAnimateCaption flies the
   active caption bar (CYCAPTION - 1 rows, inside the window borders, drawn once at the larger of the two widths and cut to the
   current width from the left) from the start to the end rectangle: left, top and width move linearly with the elapsed time,
   x = start + MulDiv(delta, elapsed, CMS_ANIMATION) with CMS_ANIMATION = 250 ms, a UserSleep(1) between frames (so the tick
   rate, 10..16 ms, sets the frame count: ~16..25 frames), the screen under the caption saved and restored each frame. The XOR
   wireframe (AnimateFrame / DrawWireFrame, 1 px white XOR pen) is Win95 code that XP compiles out ("DISABLE OLD ANIMATION FOR
   M7"); ANIMATE_WIREFRAME draws XOR rectangles (3 px, GXxor) with the same timing instead.

   XP locks the screen for the whole 250 ms; holding the X server grab that long freezes every other client (an xclock misses its
   tick). Only each frame's erase + draw runs under the grab (well under a millisecond); between frames the server
   is free and other clients draw, also under our caption or wireframe. So a frame is not erased blindly: each patch keeps the
   screen it covered (saved) and what we put there (drawn), and the erase reads the screen back and restores saved only where
   our pixels are still there; a pixel a client painted meanwhile is left as the client painted it (no stale save-under, no
   inverted XOR left behind). The wireframe is put as images too: XOR with the white ^ black pen is the inverted saved pixels. */
struct anim_patch { int x, y, w, h; XImage *saved, *drawn; };
#define ANIM_PATCHES 4

static unsigned long
img_px(XImage *im, int x, int y)
{
    if (im->bits_per_pixel == 32) return ((uint32_t *)(void *)(im->data + y * im->bytes_per_line))[x];
    return XGetPixel(im, x, y);
}

static void
img_set(XImage *im, int x, int y, unsigned long v)
{
    if (im->bits_per_pixel == 32) ((uint32_t *)(void *)(im->data + y * im->bytes_per_line))[x] = (uint32_t)v;
    else XPutPixel(im, x, y, v);
}

/* erase the patches of a frame (in reverse order): saved where the screen still shows drawn, the screen's own pixel elsewhere */
static void
anim_erase(GC g, struct anim_patch *p, int n, unsigned long mask)
{
    for (int i = n - 1; i >= 0; i--) {
        XImage *cur = XGetImage(display, root, p[i].x, p[i].y, (unsigned)p[i].w, (unsigned)p[i].h, AllPlanes, ZPixmap);
        if (cur) {
            for (int y = 0; y < p[i].h; y++)
                for (int x = 0; x < p[i].w; x++)
                    if (((img_px(cur, x, y) ^ img_px(p[i].drawn, x, y)) & mask) == 0)
                        img_set(cur, x, y, img_px(p[i].saved, x, y));
            XPutImage(display, root, g, cur, 0, 0, p[i].x, p[i].y, (unsigned)p[i].w, (unsigned)p[i].h);
            XDestroyImage(cur);
        }
        XDestroyImage(p[i].saved); XDestroyImage(p[i].drawn);
    }
}

/* put one patch: the screen rectangle x,y,w,h (clipped to the screen) is saved, then covered by the strip image (from its pixel
   sx,sy) or, strip == NULL, by its own inverted pixels (the XOR wireframe); false when nothing of it is on screen */
static bool
anim_put(GC g, struct anim_patch *p, int x, int y, int w, int h, XImage *strip, int sx, int sy, unsigned long mask)
{
    int scr = DefaultScreen(display), W = DisplayWidth(display, scr), H = DisplayHeight(display, scr);
    if (x < 0) { sx -= x; w += x; x = 0; }
    if (y < 0) { sy -= y; h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    if (strip) { if (w > strip->width - sx) w = strip->width - sx; if (h > strip->height - sy) h = strip->height - sy; }
    if (w <= 0 || h <= 0) return false;
    XImage *saved = XGetImage(display, root, x, y, (unsigned)w, (unsigned)h, AllPlanes, ZPixmap);
    if (!saved) return false;
    XImage *drawn = strip ? XSubImage(strip, sx, sy, (unsigned)w, (unsigned)h) : XSubImage(saved, 0, 0, (unsigned)w, (unsigned)h);
    if (!drawn) { XDestroyImage(saved); return false; }
    if (!strip)
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) img_set(drawn, i, j, img_px(saved, i, j) ^ mask);
    XPutImage(display, root, g, drawn, 0, 0, x, y, (unsigned)w, (unsigned)h);
    p->x = x; p->y = y; p->w = w; p->h = h; p->saved = saved; p->drawn = drawn;
    return true;
}

/* one frame's patches: the caption strip cut to width w, or the 3 px wireframe outline of x,y,w,h as four separate bands */
static int
anim_draw(GC g, struct anim_patch *p, bool wire, XImage *strip, int cy, int x, int y, int w, int h, unsigned long mask)
{
    int n = 0;
    if (!wire) return anim_put(g, &p[0], x, y, MIN(w, strip->width), cy, strip, 0, 0, mask) ? 1 : 0;
    int lw = MIN(3, w), rw = MIN(3, w - lw), th = MIN(3, h), bh = MIN(3, h - th), sh = h - th - bh;
    if (anim_put(g, &p[n], x, y, w, th, NULL, 0, 0, mask)) n++;
    if (bh > 0 && anim_put(g, &p[n], x, y + h - bh, w, bh, NULL, 0, 0, mask)) n++;
    if (sh > 0 && anim_put(g, &p[n], x, y + th, lw, sh, NULL, 0, 0, mask)) n++;
    if (sh > 0 && rw > 0 && anim_put(g, &p[n], x + w - rw, y + th, rw, sh, NULL, 0, 0, mask)) n++;
    return n;
}

static void
anim_classic(struct client *c, int fx, int fy, int fw, int fh, int tx, int ty, int tw, int th)
{
    if (!ANIM_ON || compositor)
        return;
    const int ms = 250, frame_us = 10000;
    int l, r, t, b; margins(c, &l, &r, &t, &b);
    int cy = t - l;   /* the caption rows inside the top border */
    bool wire = ANIMATE_WIREFRAME || !c->decorated || c->dec == None || cy <= 4;
    int scr = DefaultScreen(display), depth = DefaultDepth(display, scr);
    unsigned long mask = depth >= 32 ? 0xffffffffUL : (1UL << depth) - 1;   /* the pixel bits that count when comparing */
    XGCValues gv = { .subwindow_mode = IncludeInferiors, .graphics_exposures = False };
    GC g = XCreateGC(display, root, GCSubwindowMode | GCGraphicsExposures, &gv);
    XImage *strip = NULL;
    unsigned long inv = (WhitePixel(display, scr) ^ BlackPixel(display, scr)) & mask;   /* the XOR pen */
    if (!wire) {
        /* the caption strip: the frame painted as it is (focused), its caption rows inside the borders, over black */
        int gw = c->geom.width, gh = c->geom.height;
        int sw = gw - l - r;
        if (sw < 1) sw = 1;
        cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, gw, t);
        cairo_t *cr = cairo_create(img);
        cairo_rectangle(cr, 0, 0, gw, t); cairo_clip(cr);
        wm_frame_paint_cr(cr, &theme, gw, gh, c->title, client_icon(c), 1, 0, 0, c->mono);
        cairo_destroy(cr);
        Pixmap pm = XCreatePixmap(display, root, (unsigned)sw, (unsigned)cy, (unsigned)depth);
        cairo_surface_t *xs = cairo_xlib_surface_create(display, pm, DefaultVisual(display, scr), sw, cy);
        cr = cairo_create(xs);
        cairo_set_source_rgb(cr, 0, 0, 0); cairo_paint(cr);
        cairo_set_source_surface(cr, img, -l, -l); cairo_paint(cr);
        cairo_destroy(cr); cairo_surface_flush(xs); cairo_surface_destroy(xs); cairo_surface_destroy(img);
        strip = XGetImage(display, pm, 0, 0, (unsigned)sw, (unsigned)cy, AllPlanes, ZPixmap);   /* the server's pixel format */
        XFreePixmap(display, pm);
        if (!strip) { XFreeGC(display, g); return; }
        /* xxxAnimateCaption: a rectangle taller than the caption is inset by the window borders */
        if (fh > cy + l) { fx += l; fy += l; fw -= 2 * l; }
        if (th > cy + l) { tx += l; ty += l; tw -= 2 * l; }
    }
    if (fw < 1) fw = 1;
    if (tw < 1) tw = 1;
    bool grab = !getenv("LHWM_ANIM_NOGRAB");   /* testing only: lets a screenshot tool watch the frames */
    struct anim_patch p[ANIM_PATCHES]; int np;
    XSync(display, False);
    int x = fx, y = fy, w = fw, h = fh;
    double t0 = now_ms(), held = 0;
    if (grab) XGrabServer(display);
    np = anim_draw(g, p, wire, strip, cy, x, y, w, h, wire ? inv : mask);
    XSync(display, False);
    if (grab) XUngrabServer(display);
    held += now_ms() - t0;
    int frames = 1;
    for (int el = (int)(now_ms() - t0); el <= ms; el = (int)(now_ms() - t0)) {
        int nx = fx + muldiv(tx - fx, el, ms), ny = fy + muldiv(ty - fy, el, ms), nw = fw + muldiv(tw - fw, el, ms);
        int nh = fh + muldiv(th - fh, el, ms);
        XFlush(display);
        usleep(frame_us);   /* the server is not grabbed here: the other clients run (and may draw under the frame) */
        double tg = now_ms();
        if (grab) XGrabServer(display);
        anim_erase(g, p, np, mask);
        np = anim_draw(g, p, wire, strip, cy, nx, ny, nw, nh, wire ? inv : mask);
        XSync(display, False);
        if (grab) XUngrabServer(display);
        held += now_ms() - tg;
        x = nx; y = ny; w = nw; h = nh;
        frames++;
    }
    if (grab) XGrabServer(display);
    anim_erase(g, p, np, mask);
    XSync(display, False);
    if (grab) XUngrabServer(display);
    XFreeGC(display, g);
    if (strip) XDestroyImage(strip);
    XFlush(display);
    LOGP("animation %s %d,%d %dx%d -> %d,%d %dx%d: %d frames in %.0f ms, server grabbed %.1f ms in all", wire ? "wireframe" : "caption",
         fx, fy, fw, fh, tx, ty, tw, th, frames, now_ms() - t0, held);
}

int
main(int argc, char *argv[])
{
    int opt;
    char *conf_path = malloc(MAXLEN * sizeof(char));
    char *font_name = malloc(MAXLEN * sizeof(char));
    bool conf_found = true;
    conf_path[0] = '\0';
    font_name[0] = '\0';

    while ((opt = getopt(argc, argv, "dhf:vc:t:")) != -1) {
        switch (opt) {
            case 't':
                theme_opt = optarg;
                break;
            case 'h':
                usage();
                break;
            case 'f':
                snprintf(font_name, MAXLEN * sizeof(char), "%s", optarg);
                break;
            case 'c':
                snprintf(conf_path, MAXLEN * sizeof(char), "%s", optarg);
                break;
            case 'v':
                version();
                break;
            case 'd':
                debug = true;
                break;
        }
    }

    if (conf_path[0] == '\0') {   /* berry's autostart script, ~/.config/lhde/luxwm-autostart; run only when it exists */
        lhde_config_file(LUXWM_AUTOSTART, conf_path, MAXLEN);
        if (access(conf_path, R_OK) != 0) conf_found = false;
    }

    if (font_name[0] == '\0') { // font not loaded
        LOGN("font not specified, loading default font");
    } else {
        LOGP("font specified, loading... %s", font_name);
        strncpy(global_font, font_name, sizeof(global_font));
    }

    prof = getenv("LHWM_PROF") && atoi(getenv("LHWM_PROF"));
    display = XOpenDisplay(NULL);
    if (!display)
        exit(EXIT_FAILURE);

    LOGN("Successfully opened display");

    setup();
    if (conf_found) {
        signal(SIGCHLD, SIG_IGN);
        load_config(conf_path);
    }
    run();
    close_wm();
    free(font_name);
    free(conf_path);
}
