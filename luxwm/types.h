#ifndef _BERRY_TYPES_H_
#define _BERRY_TYPES_H_

#include "config.h"

#include <X11/Xlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "ipc.h"

struct client_geom {
    int x, y, width, height;
};

struct client {
    Window window, dec;
    int ws, x_hide;
    bool decorated, hidden, fullscreen, mono, was_fs;
    bool minimized, attention, below;   /* minimised (hidden by the user, not by a workspace switch), EWMH demands-attention, always below */
    unsigned long seq;                  /* creation order, for _NET_CLIENT_LIST */
    unsigned net_state;                 /* last _NET_WM_STATE bit set written (to skip redundant property changes) */
    int max_prop; Window max_prop_dec; /* _LHWM_MAXIMIZED last written (0 = never, else value + 1) and on which decoration */
    struct client_geom geom;
    struct client_geom prev;
    int snap;                           /* SNAP_LEFT / SNAP_RIGHT / SNAP_VERT: snapped by a drag to a work-area edge (docs/LUXWM.md "Snap") */
    struct client_geom snap_prev;       /* the frame rectangle before the snap: a drag away restores its size */
    struct client *next, *f_next;
    int hover, pressed;
    void *icon;   /* cairo_surface_t* */
    unsigned long icon_pm, icon_mask;   /* WM_HINTS icon_pixmap / icon_mask last read (PropertyNotify re-reads the icon when they change) */
    bool has_ext;                       /* _LHWM_EXTEND_FRAME set (CLIENT-PROTOCOL.md) */
    unsigned long ext[4];               /* its left right top bottom, client pixels (all 0xFFFFFFFF: sheet of glass) */
    unsigned long ext_flags;            /* _LHWM_FRAME_FLAGS */
    char title[512];
    int depth;                          /* the client's depth: 32 (ARGB) may be translucent, anything else covers its part of the plate */
    unsigned long paint_serial;         /* first request of the last frame paint: Expose events older than it are already covered */
    bool dirty;                         /* an Expose arrived after the last paint: repainted when the event queue is empty */
    int unmap_ignore;                   /* UnmapNotify events luxwm caused itself (minimise unmaps the client) */
    bool unmapped;                      /* minimised: client and decoration window are unmapped (ICCCM IconicState) */
    double anim_until;                  /* _LHWM_ANIMATE is 2 (picom geometry animation) until this time (ms, now_ms()), 0: not */
    double xfade_t0, xfade_next;        /* maximise/restore crossfade of the DWM plate: start (ms, now_ms(); 0 = none), next repaint */
    bool xfade_to_max;                  /* its direction: normal -> maximised plate, or back */
    Visual *visual;                     /* the client's visual (the header pixmap is made in its depth/visual) */
    Pixmap hdr_pix;                     /* _LHWM_HEADER_PIXMAP published on the client (None: none) */
    unsigned long hdr_key;              /* what it was rendered from (size, focus, hover, theme...), to skip identical renders */
    long cap_rect[4];                   /* _LHWM_CAPTION_RECT last written (CLIENT_CAPTION) */
    bool cap_rect_set;
    bool cap_rect_mono;                 /* c->mono when it was last written: republished on every maximise/restore */
    /* WM_NORMAL_HINTS (ICCCM 4.1.2.3; LUXWM.md "Size hints"), read at map and on PropertyNotify: base, increments, min, max (0: none),
       aspect as h/w minimum (min_a) and w/h maximum (max_a), 0: none */
    int base_w, base_h, inc_w, inc_h, min_w, min_h, max_w, max_h;
    float min_a, max_a;
    long fext[4];                       /* _NET_FRAME_EXTENTS last written (fext_set) */
    bool fext_set;
};

enum { SNAP_NONE, SNAP_LEFT, SNAP_RIGHT, SNAP_VERT, SNAP_MAX /* a drag target only: maximise */ };

struct config {
    int b_width, i_width, t_height, top_gap, bot_gap, left_gap, right_gap, r_step, m_step, focus_button, move_button, move_mask, resize_button, resize_mask, pointer_interval;
    unsigned long bf_color, bu_color, if_color, iu_color;
    bool focus_new, focus_motion, edge_lock, t_center, smart_place, draw_text, json_status, decorate, fs_remove_dec, fs_max;
    bool follow_pointer, focus_on_click, warp_pointer;
    bool manage[WindowLast];
    unsigned long last_caption_click, last_caption_win;
};

struct monitor {
    int x, y, width, height, screen;
};

enum atoms_net {
    NetSupported,
    NetNumberOfDesktops,
    NetActiveWindow,
    NetCurrentDesktop,
    NetClientList,
    NetWMStateFullscreen,
    NetWMStateBelow,
    NetWMCheck,
    NetWMState,
    NetWMName,
    NetWMWindowType,
    NetWMWindowTypeMenu,
    NetWMWindowTypeToolbar,
    NetWMWindowTypeDock,
    NetWMWindowTypeDialog,
    NetWMWindowTypeUtility,
    NetWMWindowTypeSplash,
    NetWMDesktop,
    NetWMFrameExtents,
    NetWMMoveResize,
    NetDesktopNames,
    NetDesktopViewport,
    NetCloseWindow,
    NetClientListStacking,
    NetWMStateHidden,
    NetWMIcon,
    NetWMStateMaxVert,
    NetWMStateMaxHorz,
    NetWMStateDemandsAttention,
    NetWMStateFocused,
    NetWMStrut,
    NetWMStrutPartial,
    NetWorkarea,
    NetWMWindowTypeDesktop,
    NetWMMoveResizeDrag,   /* _NET_WM_MOVERESIZE (NetWMMoveResize is berry's _NET_MOVERESIZE_WINDOW) */
    NetWMSyncRequest,      /* basic frame sync during interactive resizes (LUXWM.md "Resize without flicker") */
    NetWMSyncRequestCounter,
    NetShowingDesktop,     /* _NET_SHOWING_DESKTOP (root property + client message) */
    NetRestackWindow,      /* _NET_RESTACK_WINDOW (client message; LUXWM.md "Restack requests") */
    NetGtkShowWindowMenu,  /* _GTK_SHOW_WINDOW_MENU (client message: the window menu at a point; CLIENT-PROTOCOL.md) */
    NetLast
};

enum atoms_wm {
    WMDeleteWindow,
    WMProtocols,
    WMTakeFocus,
    WMState,
    WMChangeState,
    WMLast,
};

enum berry_net {
    BerryWindowStatus,
    BerryClientEvent,
    BerryFontProperty,
    BerryLast
};

enum direction {
    EAST,
    NORTH,
    WEST,
    SOUTH
};

enum dec {
    TOP,
    LEFT,
    RIGHT,
    BOT,
    TITLE,
    DECLast
};

#endif
