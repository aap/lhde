/* navdemo: the reference client for the Longhorn navigation bar (docs/NAVBAR.md, luxnav/lhnav.h) on luxwm's extended frames
   (docs/CLIENT-PROTOCOL.md). The successor of extendframe.c: everything that file shows about the protocol is here too,
   with the header drawn by luxnav instead of by hand.

   A tiny file browser: a header with the window title in the frame's deco row, the back/forward orb, a breadcrumb of the
   current directory and a search box, on luxwm's glass; below it the directory's entries as plain text lines.
     - click a breadcrumb segment: go there; "«" (the collapsed leftmost segments): go to the last hidden one;
     - click the empty part of the breadcrumb box (or Ctrl+L): the box becomes a plain text field with the path; Enter goes
       there, Escape (or a click elsewhere) cancels;
     - the search box: a stub field; typing filters the listing by substring, Escape clears it;
     - back / forward (also Alt+Left / Alt+Right, Backspace): over a local history;
     - double-click a directory line (or select it and press Enter): go into it;
     - the header outside the controls is the caption: drag moves, double-click maximises/restores, the top rows resize.
     - the big variant (nav.ini `variant = big`): the drop-down arrow parts under the orbs (LHNAV_BACK_MENU / LHNAV_FWD_MENU)
       open the back / forward history as a list (an override-redirect window; click, or Up/Down + Enter, goes there; Escape or a
       click outside closes it); in every variant a right click on back / forward opens the same list;
     - the breadcrumb's chevrons (LHNAV_SEP + i) are drop-downs: a press lists segment i's subfolders (sorted, dot folders left
       out, segment i + 1 bold and selected); "«" (LHNAV_OVERFLOW) lists the collapsed ancestors. The lists are the history
       list's window (one override-redirect list for all drop-downs): Up/Down/Home/End, a letter, Enter, a click; Escape or a
       click outside closes; lhnav_set_open keeps the opening part pressed meanwhile;
     - the right-hand toolbar (nav.ini `tools`, NAVDEMO_TOOLS="..." overrides it): `search` opens the search field
       (lhnav_set_search_open) and focuses it, `favorites` lists the GTK bookmarks ($XDG_CONFIG_HOME/gtk-3.0/bookmarks) with
       "Add current folder" first, `bookmark:PATH` goes there, `views` switches List / Details, `newfolder` creates "New Folder",
       "New Folder (2)", ...; hovering a part with lhnav_tip text shows a tooltip window after 500 ms;
     - p / P (no field edited): the next / previous preset of nav.ini ("[preset NAME]" sections), live; the title shows it.
       NAVDEMO_PRESET=NAME starts with that one. Editing nav.ini's `preset =` follows the file again;
   Escape (when no field is being edited) or Ctrl+Q quits.

   The protocol steps, in the order the code does them:
     1. a 32-bit ARGB toplevel (the header must be transparent over the glass);
     2. _LHWM_FRAME_FLAGS = CLIENT_CAPTION | NO_TITLE | NO_ICON and _LHWM_EXTEND_FRAME = 0 0 TOP 0 before mapping, where
        TOP = lhnav_extend_top(): the header in client rows plus the 2 rows on which luxwm draws the plate's inner line pair.
        With CLIENT_CAPTION luxwm grows the window up to the frame's top border (border_t, _LHWM_FRAME_METRICS 12: 2 under Aero
        vision, whose frame keeps only the plate's outer edge rows), so client row 0 is frame row border_t;
     3. _LHWM_CAPTION_RECT (on our window): luxwm's caption buttons; we leave it empty and cut it out of our input shape so clicks
        reach the buttons on luxwm's decoration window below;
     4. paint: everything transparent, then the header with luxnav (over glass: luxnav's text halo; not composited: luxwm's
        _LHWM_HEADER_PIXMAP under it and plain text), then the body opaque;
     5. caption behaviour through _NET_WM_MOVERESIZE (move 8, top resize 1) and _NET_WM_STATE (maximise toggle);
     6. relayout and repaint on PropertyNotify: root _LH_THEME / _LH_THEME_RESOLVED / _LHWM_COMPOSITED / _LHWM_FRAME_METRICS / _LHWM_THEME (theme
        change or reload: reopen the visual style, re-read nav.ini), our _LHWM_CAPTION_RECT and _NET_WM_STATE (maximise), our
        _LHWM_HEADER_PIXMAP (a new opaque header background).

   Build: make navdemo (builds luxnav first). Run: DISPLAY=:3 ./navdemo [-r ROOT] [DIRECTORY]; -r: the breadcrumb starts at
   ROOT (named by its last component) instead of "/" ("File System"). The toolbar's icons come from the session theme's
   path.resources (shell32.dll) unless nav.ini sets icon_dir. NAVDEMO_DEBUG=1 prints luxnav's final layout on every relayout:
   every part's rect in client coordinates and the frame rows (y + border_t) that nav.ini's *_y keys use. */
#define LHNAV_WITH_CAIRO
#include "lhnav.h"
#include "lhde/data.h"
#include "lhde/theme.h"
#ifdef HAVE_UXTHEME
#include <wuxtheme/ut.h>
#endif
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/extensions/shape.h>
#include <cairo/cairo-xlib.h>
#include <pango/pangocairo.h>
#include <fontconfig/fontconfig.h>
#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <sys/time.h>
#include <time.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define NO_TITLE 1          /* _LHWM_FRAME_FLAGS bits (CLIENT-PROTOCOL.md) */
#define NO_ICON 2
#define CLIENT_CAPTION 8
#define LINE_H 18           /* body line height */
#define BODY_FONT "Segoe UI, Tahoma, Sans 9"

static Display *dpy;
static Window root, win;
static Visual *vis;
static Atom a_composited, a_theme, a_lh_theme, a_lh_resolved, a_lh_serial, a_metrics, a_extend, a_flags, a_header_pixmap, a_caption_rect;
static Atom a_moveresize, a_state, a_max_v, a_max_h, a_delete, a_name, a_utf8;
static int W = 720, H = 480, composited, maximized, focused = 1, x_error;
static long cap_rect[4];
static lhnav *nav;
static lhnav_metrics base_metrics;   /* nav.ini + defaults, before luxwm's numbers are filled in */
static void *style;                  /* ut_theme * of the session's msstyles (NULL without libuxtheme) */
static unsigned long text_rgb = 0x1e1e1e;   /* _LHWM_FRAME_METRICS 4: the caption text colour (opaque header) */
static char nav_path[1300];          /* the frame theme's nav.ini */
static char preset_sel[48];          /* the preset chosen with p ("" = nav.ini's own `preset`) */
static char file_preset[48] = "\1"; /* nav.ini's `preset` when last read: when it changes, the file wins again */
static void update_title(void);

/* ---- the model: current directory, its entries, the history ---- */
static char cwd[PATH_MAX];
static char **hist; static int nhist, hpos;          /* hist[hpos] is cwd */
static char **ent; static int nent, *isdir;          /* sorted: directories first */
static long long *ent_size; static time_t *ent_mtime;  /* for the Details view */
static int view_mode;                                /* the `views` tool: 0 List, 1 Details */
static char icon_dir_reg[512];                       /* the theme's path.resources (luxnav's icon_dir unless nav.ini sets one) */
static int *vis_idx, nvis, sel = -1, scroll;         /* the entries matching the search text */
static char address[PATH_MAX], search[256];          /* the two edit fields' text */
static int editing;                                  /* 0 none, 1 address, 2 search */
static int caret;                                    /* byte offset in the edited field */

/* ---- X helpers ---- */
static long *read_cardinals(Window w, Atom a, int max, int *n) {
    Atom type; int format; unsigned long cnt, after; unsigned char *data = NULL; long *v = NULL; *n = 0;
    if (XGetWindowProperty(dpy, w, a, 0, max, False, AnyPropertyType, &type, &format, &cnt, &after, &data) == Success && data) {
        if (format == 32 && cnt) { v = calloc(cnt, sizeof *v); memcpy(v, data, cnt * sizeof *v); *n = (int)cnt; }
        XFree(data);
    }
    return v;
}
static void read_string(Window w, Atom a, char *buf, size_t n) {
    Atom type; int format; unsigned long cnt, after; unsigned char *data = NULL; buf[0] = 0;
    if (XGetWindowProperty(dpy, w, a, 0, 1024, False, AnyPropertyType, &type, &format, &cnt, &after, &data) == Success && data) {
        if (format == 8) snprintf(buf, n, "%.*s", (int)cnt, (char *)data);
        XFree(data);
    }
}
static void send_root_message(Atom type, long l0, long l1, long l2, long l3, long l4) {
    XEvent e; memset(&e, 0, sizeof e);
    e.xclient.type = ClientMessage; e.xclient.window = win; e.xclient.message_type = type; e.xclient.format = 32;
    e.xclient.data.l[0] = l0; e.xclient.data.l[1] = l1; e.xclient.data.l[2] = l2; e.xclient.data.l[3] = l3; e.xclient.data.l[4] = l4;
    XSendEvent(dpy, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e); XFlush(dpy);
}
/* the press gave us an implicit grab: release it, then luxwm runs the drag (8 move, 1 resize from the top) */
static void start_moveresize(XButtonEvent *b, int dir) {
    XUngrabPointer(dpy, b->time);
    send_root_message(a_moveresize, b->x_root, b->y_root, dir, b->button, 1);
}
static int on_x_error(Display *d, XErrorEvent *e) { (void)d; (void)e; x_error = 1; return 0; }

/* ---- theme (rule 6): the session theme's record (msstyles, fonts, resources, navbar) and the frame set of _LHWM_THEME (nav.ini) ---- */
/* _LH_THEME names the theme; _LH_THEME_RESOLVED is its record (docs/THEME-FORMAT.md section 4), current when its theme= is that
   name and its serial= the root's _LH_SETTINGS_SERIAL; else liblhde resolves the name itself */
static lhde_theme *session_theme(const char *name) {
    Atom type; int format; unsigned long cnt, after; unsigned char *data = NULL;
    char *rec = NULL; unsigned long serial = 0, rs = 0;
    if (XGetWindowProperty(dpy, root, a_lh_serial, 0, 1, False, XA_CARDINAL, &type, &format, &cnt, &after, &data) == Success && data) {
        if (format == 32 && cnt == 1) serial = (unsigned long)*(long *)data;
        XFree(data); data = NULL;
    }
    if (XGetWindowProperty(dpy, root, a_lh_resolved, 0, 1 << 20, False, AnyPropertyType, &type, &format, &cnt, &after, &data) == Success && data) {
        if (format == 8 && (rec = malloc(cnt + 1))) { memcpy(rec, data, cnt); rec[cnt] = 0; }
        XFree(data);
    }
    lhde_theme *t = NULL;
    size_t nl = strlen(name);
    if (rec && !strncmp(rec, "theme=", 6) && !strncmp(rec + 6, name, nl) && rec[6 + nl] == '\n') {
        t = lhde_theme_parse(rec, &rs);
        if (t && rs != serial) { lhde_theme_free(t); t = NULL; }   /* stale */
    }
    free(rec);
    return t ? t : lhde_theme_resolve(name, NULL, 0);
}
static const char *theme_value(const lhde_theme *t, const char *key) { const char *v = t ? lhde_theme_get(t, key) : NULL; return v ? v : ""; }
static void load_theme(void) {
    char name[256], ms[1200] = "", v[1200], frame[1200], navbar[1200] = "";
    int vision = 0;
    read_string(root, a_lh_theme, name, sizeof name);
    icon_dir_reg[0] = 0;
    lhde_theme *t = name[0] ? session_theme(name) : NULL;
    if (t) {
        snprintf(ms, sizeof ms, "%s", theme_value(t, "path.style"));
        vision = !strcmp(theme_value(t, "Explorer.navbar"), "vision");   /* the theme's navbar is luxnav's */
        snprintf(navbar, sizeof navbar, "%s", theme_value(t, "path.navbar"));
        snprintf(icon_dir_reg, sizeof icon_dir_reg, "%s", theme_value(t, "path.resources"));   /* shell32.dll for the toolbar's icons */
        snprintf(v, sizeof v, "%s", theme_value(t, "path.fonts"));   /* the build's Fonts directory: Segoe UI 0.7 */
        for (char *save = NULL, *f = strtok_r(v, ";", &save); f; f = strtok_r(NULL, ";", &save)) {
            while (*f == ' ') f++;
            if (!*f) continue;
            lhnav_add_font_dir(f);
            FcConfigAppFontAddDir(NULL, (const FcChar8 *)f); pango_cairo_font_map_set_default(NULL);   /* the body list's pango text */
        }
        lhde_theme_free(t);
    }
#ifdef HAVE_UXTHEME
    void *old = style; style = ms[0] ? ut_open(ms, NULL, NULL) : NULL;
#endif
    /* nav.ini of the frame theme (_LHWM_THEME = "DIR|STYLE|COLOR|SIZE"): luxnav's tuning, re-read on every theme reload */
    lhnav_metrics_default(&base_metrics);
    read_string(root, a_theme, frame, sizeof frame);
    char *bar = strchr(frame, '|'); if (bar) *bar = 0;
    nav_path[0] = 0;
    if (frame[0] && !lhde_data_find_in(frame, "nav.ini", nav_path, sizeof nav_path)) nav_path[0] = 0;   /* beside theme.ini, or the
                                                                       frame set's copy in a data directory */
    /* a frame set without one (aero5048, aero5112: their themes' navbar = vision): the theme's navbar layout file (path.navbar) */
    if (!nav_path[0] && vision && navbar[0]) snprintf(nav_path, sizeof nav_path, "%s", navbar);
    if (nav_path[0]) {
        /* nav.ini's own choice; p overrides it until the file's `preset =` is edited */
        lhnav_metrics tmp; lhnav_metrics_default(&tmp); lhnav_metrics_load(&tmp, nav_path);
        if (strcmp(tmp.preset, file_preset)) { if (file_preset[0] != 1) preset_sel[0] = 0; snprintf(file_preset, sizeof file_preset, "%s", tmp.preset); }
        lhnav_metrics_load_preset(&base_metrics, nav_path, preset_sel);
    }
    if (!nav) nav = lhnav_new(style, &base_metrics);
    lhnav_set_style_name(nav, ms[0] ? ms : NULL);   /* for luxnav's log and art sources */
    lhnav_set_style(nav, style);
#ifdef HAVE_UXTHEME
    if (old) ut_close(old);   /* after luxnav let go of it */
#endif
    fprintf(stderr, "navdemo: theme %s, style %s, frame %s, preset %s\n", name, ms[0] ? ms : "(none)", frame[0] ? frame : "(none)",
            base_metrics.preset[0] ? base_metrics.preset : "(none)");
    update_title();
}

/* luxwm's numbers into luxnav's metrics: where our row 0 is in the frame (border_t, field 12/13), where the deco ends (deco_h,
   field 18; no such field or a theme without a vision deco: the caption), the caption buttons (_LHWM_CAPTION_RECT); then the
   extension and the input shape follow */
static void apply_metrics(void) {
    int n; long *m = read_cardinals(root, a_metrics, 32, &n);
    lhnav_metrics lm = base_metrics;
    if (!lm.icon_dir[0]) snprintf(lm.icon_dir, sizeof lm.icon_dir, "%s", icon_dir_reg);
    if (getenv("NAVDEMO_TOOLS")) snprintf(lm.tools, sizeof lm.tools, "%s", getenv("NAVDEMO_TOOLS"));   /* instead of nav.ini's `tools` */
    long cap = n > 0 ? m[0] : 27, bt = n > 13 ? m[maximized ? 13 : 12] : 9, bl = n > 1 ? m[1] : 9;
    lm.border_t = (int)bt; lm.border_l = maximized ? 0 : (int)bl;
    if (n > 18 && m[18] > 0) lm.deco_h = (int)m[18];
    else lm.deco_h = (int)(maximized && n > 8 ? m[8] : cap);
    if (maximized) lm.resize_rows = 0;
    text_rgb = n > 4 ? (unsigned long)(maximized && n > 14 ? m[14] : m[4]) : 0x1e1e1e;
    lm.plain_rgb = (uint32_t)text_rgb;
    /* maximised: luxwm's maximised caption look (fields 14 and 16: aero/aero vision white text, no glow, on the dark MAXEXPLORER
       plate) for all header text */
    if (maximized && n > 16 && (uint32_t)m[16] == 0xFFFFFFFFu) { lm.title_rgb = lm.crumb_rgb = (uint32_t)m[14]; lm.glow_strength = 0; }
    free(m);
    int k; long *r = read_cardinals(win, a_caption_rect, 4, &k);
    memset(cap_rect, 0, sizeof cap_rect);
    if (r && k == 4) memcpy(cap_rect, r, sizeof cap_rect);
    free(r);
    lm.caption_x = (int)cap_rect[0]; lm.caption_y = (int)cap_rect[1]; lm.caption_w = (int)cap_rect[2]; lm.caption_h = (int)cap_rect[3];
    lhnav_set_metrics(nav, &lm);
    lhnav_set_frame_state(nav, focused, maximized);
    lhnav_layout(nav, W);
    if (getenv("NAVDEMO_DEBUG")) {   /* the final layout: client x, client y and frame row (nav.ini's *_y keys), size */
        static const char *const names[] = { "title", "back", "fwd", "back_menu", "fwd_menu", "address", "overflow", "search" };
        static const int parts[] = { LHNAV_TITLE, LHNAV_BACK, LHNAV_FWD, LHNAV_BACK_MENU, LHNAV_FWD_MENU, LHNAV_ADDRESS, LHNAV_OVERFLOW, LHNAV_SEARCH };
        int bt = lm.border_t;
        fprintf(stderr, "navdemo: preset %s (variant %s) %dx%d border_t %d border_l %d deco_h %d header %d frame rows (%d client) extend %d"
                " caption %ld,%ld %ldx%ld\n", lm.preset[0] ? lm.preset : "-", lm.variant == LHNAV_BIG ? "big" : "small", W, H, bt, lm.border_l,
                lm.deco_h, lhnav_height(nav) + bt, lhnav_height(nav), lhnav_extend_top(nav), cap_rect[0], cap_rect[1], cap_rect[2], cap_rect[3]);
        fprintf(stderr, "  %-9s  client x,y   frame row  w x h\n", "part");
        for (int i = 0; i < 8; i++) { int x, y, w, h; if (lhnav_rect(nav, parts[i], &x, &y, &w, &h))
            fprintf(stderr, "  %-9s  %4d,%-4d   %4d      %dx%d\n", names[i], x, y, y + bt, w, h); }
        fprintf(stderr, "  %-9s  %4s %-4d   %4d\n", "title_bl", "", lhnav_title_baseline(nav), lhnav_title_baseline(nav) + bt);
        for (int i = 0; ; i++) { int x, y, w, h; if (!lhnav_rect(nav, LHNAV_CRUMB + i, &x, &y, &w, &h)) { if (i > 64) break; continue; }
                                 fprintf(stderr, "  crumb%-4d  %4d,%-4d   %4d      %dx%d\n", i, x, y, y + bt, w, h); }
        for (int i = 0; i < 64; i++) { int x, y, w, h; if (lhnav_rect(nav, LHNAV_SEP + i, &x, &y, &w, &h))
                                 fprintf(stderr, "  sep%-6d  %4d,%-4d   %4d      %dx%d\n", i, x, y, y + bt, w, h); }
        for (int i = 0; i < lhnav_tool_count(nav); i++) { int x, y, w, h; const char *a, *l; int k = lhnav_tool(nav, i, &a, &l);
            if (lhnav_rect(nav, LHNAV_TOOL + i, &x, &y, &w, &h))
                fprintf(stderr, "  tool%-5d  %4d,%-4d   %4d      %dx%d  kind %d %s\n", i, x, y, y + bt, w, h, k, a ? a : ""); }
        fprintf(stderr, "navdemo: art sources (nav.ini %s)\n%s", nav_path[0] ? nav_path : "(none)", lhnav_art_sources(nav));
    }
    long ext[4] = { 0, 0, lhnav_extend_top(nav), 0 }, flags = CLIENT_CAPTION | NO_TITLE | NO_ICON;
    long old[4] = { -1, -1, -1, -1 }; int on; long *o = read_cardinals(win, a_extend, 4, &on);
    if (o && on == 4) memcpy(old, o, sizeof old);
    free(o);
    if (memcmp(old, ext, sizeof ext)) XChangeProperty(dpy, win, a_extend, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)ext, 4);
    XChangeProperty(dpy, win, a_flags, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&flags, 1);
    /* the caption buttons' hole in the input shape (rule 3) */
    XRectangle all = { 0, 0, (unsigned short)W, (unsigned short)H };
    XShapeCombineRectangles(dpy, win, ShapeInput, 0, 0, &all, 1, ShapeSet, Unsorted);
    if (cap_rect[2] > 0 && cap_rect[3] > 0) {
        XRectangle hole = { (short)cap_rect[0], (short)cap_rect[1], (unsigned short)cap_rect[2], (unsigned short)cap_rect[3] };
        XShapeCombineRectangles(dpy, win, ShapeInput, 0, 0, &hole, 1, ShapeSubtract, Unsorted);
    }
}

/* root _LHWM_COMPOSITED: 1 = glass (luxwm paints the plate under our transparent header); missing = 0 */
static int read_composited(void) {
    int n; long *v = read_cardinals(root, a_composited, 1, &n); int c = v && n == 1 && v[0]; free(v);
    return c;
}

static int read_maximized(void) {
    Atom type; int format; unsigned long n, after; unsigned char *data = NULL; int v = 0, h = 0;
    if (XGetWindowProperty(dpy, win, a_state, 0, 64, False, XA_ATOM, &type, &format, &n, &after, &data) == Success && data) {
        for (unsigned long i = 0; i < n; i++) { Atom a = ((Atom *)data)[i]; v |= a == a_max_v; h |= a == a_max_h; }
        XFree(data);
    }
    return v && h;
}

static void field_sync(void);

/* ---- the model ---- */
static int cmp_ent(const void *a, const void *b) {
    int ia = *(const int *)a, ib = *(const int *)b;
    if (isdir[ia] != isdir[ib]) return isdir[ib] - isdir[ia];
    return strcasecmp(ent[ia], ent[ib]);
}
static void filter(void) {
    free(vis_idx); vis_idx = calloc((size_t)nent + 1, sizeof *vis_idx); nvis = 0;
    for (int i = 0; i < nent; i++) if (!search[0] || strcasestr(ent[i], search)) vis_idx[nvis++] = i;
    qsort(vis_idx, (size_t)nvis, sizeof *vis_idx, cmp_ent);
    sel = -1; scroll = 0;
}
/* the breadcrumb's root (-r ROOT; default "/"): its first segment, named "File System" for "/" and by its last component otherwise;
   a directory outside it gets the full path from "/" */
static char rootdir[PATH_MAX] = "/";
/* the part of cwd below the root ("" at the root itself), or NULL when cwd is outside it; *base: the root to use */
static const char *below_root(const char **base) {
    size_t n = strlen(rootdir);
    *base = rootdir;
    if (!strcmp(rootdir, "/")) return cwd + 1;
    if (!strncmp(cwd, rootdir, n) && (cwd[n] == 0 || cwd[n] == '/')) return cwd[n] ? cwd + n + 1 : cwd + n;
    *base = "/";
    return cwd + 1;
}
static void set_path_segments(void) {
    const char *segs[256], *base, *rest = below_root(&base), *slash = strrchr(base, '/');
    char buf[PATH_MAX]; int n = 0;
    segs[n++] = !strcmp(base, "/") ? "File System" : slash && slash[1] ? slash + 1 : base;
    snprintf(buf, sizeof buf, "%s", rest);
    for (char *save = NULL, *t = strtok_r(buf, "/", &save); t && n < 256; t = strtok_r(NULL, "/", &save)) segs[n++] = t;
    lhnav_set_path(nav, segs, n);   /* copied */
}
/* go to path (already absolute); push: a new history entry (else back/forward moved hpos) */
static int go(const char *path, int push) {
    char real[PATH_MAX]; DIR *d;
    if (!realpath(path, real) || !(d = opendir(real))) { XBell(dpy, 0); return 0; }
    for (int i = 0; i < nent; i++) free(ent[i]);
    free(ent); free(isdir); free(ent_size); free(ent_mtime); ent = NULL; isdir = NULL; ent_size = NULL; ent_mtime = NULL; nent = 0;
    int cap = 0; struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (nent == cap) { cap = cap ? cap * 2 : 64; ent = realloc(ent, cap * sizeof *ent); isdir = realloc(isdir, cap * sizeof *isdir);
                           ent_size = realloc(ent_size, cap * sizeof *ent_size); ent_mtime = realloc(ent_mtime, cap * sizeof *ent_mtime); }
        char p[PATH_MAX + 256]; struct stat st;
        snprintf(p, sizeof p, "%s/%s", real, e->d_name);
        int ok = stat(p, &st) == 0;
        isdir[nent] = ok && S_ISDIR(st.st_mode);
        ent_size[nent] = ok ? (long long)st.st_size : -1; ent_mtime[nent] = ok ? st.st_mtime : 0;
        ent[nent++] = strdup(e->d_name);
    }
    closedir(d);
    snprintf(cwd, sizeof cwd, "%s", real);
    if (push) {   /* drop the forward part, append */
        for (int i = hpos + 1; i < nhist; i++) free(hist[i]);
        nhist = nhist ? hpos + 1 : 0;
        hist = realloc(hist, (nhist + 1) * sizeof *hist); hist[nhist++] = strdup(real); hpos = nhist - 1;
    }
    search[0] = 0; editing = 0; field_sync();
    if (lhnav_search_open(nav)) lhnav_set_search_open(nav, 0);   /* the magnifier's field goes back behind it */
    filter(); set_path_segments();
    lhnav_set_history(nav, hpos > 0, hpos < nhist - 1);
    update_title();
    lhnav_layout(nav, W);
    return 1;
}
/* the title: the directory's name, and the nav.ini preset in use (for experimenting with p) */
static void update_title(void) {
    if (!nav || !win || !cwd[0]) return;
    const char *base = strrchr(cwd, '/'); base = base && base[1] ? base + 1 : cwd;
    char t[PATH_MAX + 64];
    if (base_metrics.preset[0]) snprintf(t, sizeof t, "%s  [%s]", base, base_metrics.preset); else snprintf(t, sizeof t, "%s", base);
    lhnav_set_title(nav, t);
    XStoreName(dpy, win, t);
    XChangeProperty(dpy, win, a_name, a_utf8, 8, PropModeReplace, (const unsigned char *)t, (int)strlen(t));
}
/* p / P: the next / previous preset of nav.ini */
static void cycle_preset(int d) {
    char names[64][48]; int n = nav_path[0] ? lhnav_presets(nav_path, names, 64) : 0, cur = 0;
    if (n <= 0) { XBell(dpy, 0); return; }
    for (int i = 0; i < n; i++) if (!strcmp(names[i], base_metrics.preset)) cur = i;
    snprintf(preset_sel, sizeof preset_sel, "%s", names[(cur + d + n) % n]);
    lhnav_metrics_default(&base_metrics);
    lhnav_metrics_load_preset(&base_metrics, nav_path, preset_sel);
    fprintf(stderr, "navdemo: preset %s\n", base_metrics.preset);
    update_title();
}
static void history_step(int d) {
    if (hpos + d < 0 || hpos + d >= nhist) return;
    hpos += d;
    if (!go(hist[hpos], 0)) hpos -= d;
}
/* segment i of the breadcrumb as a path: the root + the first i components below it */
static void crumb_path(int i, char *out, size_t n) {
    const char *base, *rest = below_root(&base);
    char buf[PATH_MAX]; snprintf(buf, sizeof buf, "%s", rest);
    size_t len = (size_t)snprintf(out, n, "%s", strcmp(base, "/") ? base : "");
    int k = 0;
    for (char *save = NULL, *t = strtok_r(buf, "/", &save); t && k < i && len < n; t = strtok_r(NULL, "/", &save), k++)
        len += (size_t)snprintf(out + len, n - len, "/%s", t);
    if (!out[0]) snprintf(out, n, "/");
}

/* ---- edit fields (the text is ours, luxnav paints it) ---- */
static char *field_buf(void) { return editing == 1 ? address : search; }
static size_t field_cap(void) { return editing == 1 ? sizeof address : sizeof search; }
static void field_sync(void) {
    lhnav_set_field(nav, LHNAV_ADDRESS, editing == 1 ? address : NULL, editing == 1 ? caret : -1, editing == 1);
    lhnav_set_field(nav, LHNAV_SEARCH, search, editing == 2 ? caret : -1, editing == 2);
}
static void edit_begin(int which, int x) {
    if (which == 2 && !lhnav_rect(nav, LHNAV_SEARCH, NULL, NULL, NULL, NULL)) {   /* a `search` item: its field comes out */
        lhnav_set_search_open(nav, 1); lhnav_layout(nav, W); x = -1;
    }
    editing = which;
    if (which == 1) snprintf(address, sizeof address, "%s", cwd);
    char *b = field_buf(); caret = (int)strlen(b);
    field_sync();
    if (x >= 0) caret = lhnav_field_index(nav, which == 1 ? LHNAV_ADDRESS : LHNAV_SEARCH, b, x);
    field_sync();
    XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
}
static void edit_end(void) {
    editing = 0; field_sync();
    if (!search[0] && lhnav_search_open(nav)) { lhnav_set_search_open(nav, 0); lhnav_layout(nav, W); }   /* back behind the magnifier */
}
static int utf8_prev(const char *s, int i) { do i--; while (i > 0 && (s[i] & 0xC0) == 0x80); return i < 0 ? 0 : i; }
static int utf8_next(const char *s, int i) { if (!s[i]) return i; do i++; while (s[i] && (s[i] & 0xC0) == 0x80); return i; }
/* a key in a field; returns 1 when it was used */
static int edit_key(KeySym k, const char *txt, int len) {
    char *b = field_buf(); size_t cap = field_cap(), n = strlen(b);
    switch (k) {
    case XK_Escape: if (editing == 2) { search[0] = 0; filter(); } edit_end(); return 1;
    case XK_Return: case XK_KP_Enter:
        if (editing == 1) { char p[2 * PATH_MAX + 2]; if (address[0] == '/') snprintf(p, sizeof p, "%s", address); else snprintf(p, sizeof p, "%s/%s", cwd, address);
                            edit_end(); go(p, 1); }
        else edit_end();
        return 1;
    case XK_Left: caret = utf8_prev(b, caret); break;
    case XK_Right: caret = utf8_next(b, caret); break;
    case XK_Home: caret = 0; break;
    case XK_End: caret = (int)n; break;
    case XK_BackSpace: if (caret > 0) { int p = utf8_prev(b, caret); memmove(b + p, b + caret, n - caret + 1); caret = p; } break;
    case XK_Delete: if (caret < (int)n) { int q = utf8_next(b, caret); memmove(b + caret, b + q, n - q + 1); } break;
    default:
        if (len <= 0 || (unsigned char)txt[0] < 32 || n + len + 1 > cap) return 0;
        memmove(b + caret + len, b + caret, n - caret + 1); memcpy(b + caret, txt, len); caret += len;
    }
    if (editing == 2) { filter(); }
    field_sync();
    return 1;
}

/* ---- the drop-down lists: an override-redirect list window for every drop-down (the history under back / forward, a breadcrumb
   chevron's subfolders, the "«" ancestors, the favorites, the views); pointer and keyboard grabbed while it is open ---- */
#define POP_LINE 20
#define POP_SEP_H 7
enum { POP_HISTORY, POP_FOLDERS, POP_FAVORITES, POP_VIEWS };
enum { PF_FOLDER = 1, PF_BOLD = 2, PF_CHECK = 4, PF_SEP = 8, PF_DISABLED = 16 };
struct pop_item { char label[256]; char *path; int flags, val; };
static Window popup;
static struct pop_item *pop_items;
static int pop_n, pop_cap, pop_hot = -1, pop_w, pop_h, pop_kind, pop_part, pop_top, pop_rows;   /* pop_top: first item shown */
static void pop_add(const char *label, const char *path, int flags, int val) {
    if (pop_n == pop_cap) { pop_cap = pop_cap ? 2 * pop_cap : 32; pop_items = realloc(pop_items, (size_t)pop_cap * sizeof *pop_items); }
    struct pop_item *it = &pop_items[pop_n++];
    snprintf(it->label, sizeof it->label, "%s", label); it->path = path ? strdup(path) : NULL; it->flags = flags; it->val = val;
}
static void pop_clear(void) { for (int i = 0; i < pop_n; i++) free(pop_items[i].path); pop_n = 0; pop_hot = -1; pop_top = 0; }
static int pop_line_h(int i) { return pop_items[i].flags & PF_SEP ? POP_SEP_H : POP_LINE; }
static int pop_pickable(int i) { return i >= 0 && i < pop_n && !(pop_items[i].flags & (PF_SEP | PF_DISABLED)); }
static void path_label(const char *s, char *out, size_t n) {
    const char *b = strrchr(s, '/');
    snprintf(out, n, "%s", !strcmp(s, "/") ? "File System" : b && b[1] ? b + 1 : s);
}
/* the client's text drawn as luxnav draws its own (libuxtheme's GDI text: the drop-down lists and the tooltip look like flxp's) */
static void ui_text(cairo_surface_t *img, int x, int y, const char *t, int bold, uint32_t rgb) {
    cairo_surface_flush(img);
    lhnav_text(nav, (uint32_t *)cairo_image_surface_get_data(img), cairo_image_surface_get_width(img), cairo_image_surface_get_height(img),
               cairo_image_surface_get_stride(img), x, y, t, bold, rgb);
    cairo_surface_mark_dirty(img);
}
static void popup_paint(void) {
    cairo_surface_t *xs = cairo_xlib_surface_create(dpy, popup, DefaultVisual(dpy, DefaultScreen(dpy)), pop_w, pop_h);
    cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pop_w, pop_h);
    cairo_t *cr = cairo_create(img);
    cairo_set_source_rgb(cr, 0.98, 0.98, 0.99); cairo_paint(cr);
    cairo_set_source_rgb(cr, 0.45, 0.50, 0.58); cairo_set_line_width(cr, 1);
    cairo_rectangle(cr, 0.5, 0.5, pop_w - 1, pop_h - 1); cairo_stroke(cr);
    int y = 2;
    for (int i = pop_top; i < pop_n && i < pop_top + pop_rows; i++) {
        struct pop_item *it = &pop_items[i];
        if (it->flags & PF_SEP) { cairo_set_source_rgb(cr, 0.80, 0.82, 0.86); cairo_rectangle(cr, 28, y + 3, pop_w - 32, 1); cairo_fill(cr); y += POP_SEP_H; continue; }
        if (i == pop_hot && pop_pickable(i)) { cairo_set_source_rgb(cr, 0.80, 0.88, 0.98); cairo_rectangle(cr, 2, y, pop_w - 4, POP_LINE); cairo_fill(cr); }
        if (it->flags & PF_FOLDER) { cairo_set_source_rgb(cr, 0.93, 0.78, 0.35); cairo_rectangle(cr, 8, y + 6, 14, 9); cairo_fill(cr); cairo_rectangle(cr, 8, y + 4, 6, 3); cairo_fill(cr); }
        if (it->flags & PF_CHECK) {   /* the current view: a bullet, as XP's radio items */
            cairo_set_source_rgb(cr, 0.1, 0.1, 0.1); cairo_arc(cr, 15, y + POP_LINE / 2.0, 3, 0, 6.3); cairo_fill(cr); }
        ui_text(img, 30, y + 2, it->label, it->flags & PF_BOLD, it->flags & PF_DISABLED ? 0x8c8c8c : 0x1a1a1a);
        y += POP_LINE;
    }
    if (pop_top > 0 || pop_top + pop_rows < pop_n) {   /* scroll marks */
        cairo_set_source_rgb(cr, 0.4, 0.45, 0.55);
        if (pop_top > 0) { cairo_move_to(cr, pop_w - 12, 7); cairo_line_to(cr, pop_w - 6, 7); cairo_line_to(cr, pop_w - 9, 3); cairo_fill(cr); }
        if (pop_top + pop_rows < pop_n) { cairo_move_to(cr, pop_w - 12, pop_h - 7); cairo_line_to(cr, pop_w - 6, pop_h - 7); cairo_line_to(cr, pop_w - 9, pop_h - 3); cairo_fill(cr); }
    }
    cairo_destroy(cr);
    cr = cairo_create(xs); cairo_set_source_surface(cr, img, 0, 0); cairo_paint(cr); cairo_destroy(cr);
    cairo_surface_destroy(img); cairo_surface_destroy(xs); XFlush(dpy);
}
static void popup_close(void) {
    if (!popup) return;
    XUngrabPointer(dpy, CurrentTime); XUngrabKeyboard(dpy, CurrentTime);
    XDestroyWindow(dpy, popup); popup = 0;
    pop_clear();
    lhnav_set_open(nav, LHNAV_NONE);
    lhnav_pointer(nav, -1, -1, 0);   /* the pressed / open look ends with the list */
    XFlush(dpy);
}
/* show the items collected with pop_add under luxnav's part `part` (anchor: that part's rect, or `alt` when it is not shown);
   right: align the list's right edge with the anchor's (the toolbar); hot: the item selected first (-1 none) */
static void popup_show(int kind, int part, int alt, int right, int hot) {
    if (pop_n <= 0) { XBell(dpy, 0); return; }
    pop_kind = kind; pop_part = part;
    pop_w = 160;
    for (int i = 0; i < pop_n; i++) { int w = lhnav_text_size(nav, pop_items[i].label, 1, NULL);   /* measured bold: the widest */
        if (w + 48 > pop_w) pop_w = w + 48; }
    if (pop_w > 420) pop_w = 420;
    int x, y, w, h, rx, ry; Window ch;
    if (!lhnav_rect(nav, part, &x, &y, &w, &h)) lhnav_rect(nav, alt, &x, &y, &w, &h);
    if (kind == POP_FOLDERS) { int ax, ay, aw, ah; if (lhnav_rect(nav, LHNAV_ADDRESS, &ax, &ay, &aw, &ah)) { y = ay; h = ah; } }   /* flush under the box */
    XTranslateCoordinates(dpy, win, root, right ? x + w - pop_w : x, y + h, &rx, &ry, &ch);
    int sw = DisplayWidth(dpy, DefaultScreen(dpy)), sh = DisplayHeight(dpy, DefaultScreen(dpy));
    if (rx + pop_w > sw) rx = sw - pop_w;
    if (rx < 0) rx = 0;
    /* as many lines as fit below (at most 24): the rest scrolls */
    int avail = sh - ry - 8, hh = 4;
    pop_rows = 0;
    for (int i = 0; i < pop_n && pop_rows < 24 && hh + pop_line_h(i) <= avail; i++) { hh += pop_line_h(i); pop_rows++; }
    if (pop_rows < 1) pop_rows = 1;
    pop_h = hh;
    pop_hot = pop_pickable(hot) ? hot : -1;
    pop_top = pop_hot >= pop_rows ? pop_hot - pop_rows / 2 : 0;
    if (pop_top > pop_n - pop_rows) pop_top = pop_n - pop_rows > 0 ? pop_n - pop_rows : 0;
    XSetWindowAttributes a; memset(&a, 0, sizeof a);
    a.override_redirect = True; a.background_pixel = WhitePixel(dpy, DefaultScreen(dpy)); a.border_pixel = 0;
    a.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | KeyPressMask;
    popup = XCreateWindow(dpy, root, rx, ry, (unsigned)pop_w, (unsigned)pop_h, 0, CopyFromParent, InputOutput, CopyFromParent,
                          CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask, &a);
    XMapRaised(dpy, popup);
    XGrabPointer(dpy, popup, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    XGrabKeyboard(dpy, popup, False, GrabModeAsync, GrabModeAsync, CurrentTime);
    lhnav_set_open(nav, part);   /* the part keeps its pressed / open look while the list is up */
}
/* dir -1: back history (newest first), +1: forward; under the part's drop-down (or the button) */
static void popup_history(int dir, int part) {
    popup_close();
    int n = dir < 0 ? hpos : nhist - 1 - hpos;
    if (n > 16) n = 16;
    for (int i = 0; i < n; i++) { char t[PATH_MAX]; path_label(hist[hpos + dir * (i + 1)], t, sizeof t); pop_add(t, NULL, PF_FOLDER, dir * (i + 1)); }
    popup_show(POP_HISTORY, part, dir < 0 ? LHNAV_BACK : LHNAV_FWD, 0, -1);
}
static int cmp_pop(const void *a, const void *b) { return strcasecmp(((const struct pop_item *)a)->label, ((const struct pop_item *)b)->label); }
static void crumb_path(int i, char *out, size_t n);
/* LHNAV_SEP + i: the subfolders of segment i (sorted, hidden ones left out), segment i + 1's folder bold and selected */
static void popup_folders(int i) {
    popup_close();
    char dir[PATH_MAX], next[PATH_MAX] = "";
    crumb_path(i, dir, sizeof dir);
    { char p[PATH_MAX]; crumb_path(i + 1, p, sizeof p); if (strcmp(p, dir)) path_label(p, next, sizeof next); }   /* the last: none */
    DIR *d = opendir(dir);
    if (!d) { XBell(dpy, 0); return; }
    struct dirent *e;
    while ((e = readdir(d)) && pop_n < 500) {
        if (e->d_name[0] == '.') continue;   /* hidden (and . ..) */
        char p[PATH_MAX + 256]; struct stat st;
        snprintf(p, sizeof p, "%s/%s", strcmp(dir, "/") ? dir : "", e->d_name);
        if (stat(p, &st) || !S_ISDIR(st.st_mode)) continue;
        pop_add(e->d_name, p, PF_FOLDER, 0);
    }
    closedir(d);
    qsort(pop_items, (size_t)pop_n, sizeof *pop_items, cmp_pop);
    int hot = -1;
    for (int k = 0; k < pop_n; k++) if (next[0] && !strcmp(pop_items[k].label, next)) { pop_items[k].flags |= PF_BOLD; hot = k; }
    if (!pop_n) pop_add("(no folders)", NULL, PF_DISABLED, 0);
    popup_show(POP_FOLDERS, LHNAV_SEP + i, LHNAV_ADDRESS, 0, hot);
}
/* LHNAV_OVERFLOW: the collapsed ancestors, the nearest first */
static void popup_overflow(void) {
    popup_close();
    for (int j = lhnav_hidden(nav) - 1; j >= 0; j--) {
        char p[PATH_MAX], t[PATH_MAX]; crumb_path(j, p, sizeof p); path_label(p, t, sizeof t);
        pop_add(t, p, PF_FOLDER, 0);
    }
    popup_show(POP_FOLDERS, LHNAV_OVERFLOW, LHNAV_ADDRESS, 0, 0);
}

/* the GTK bookmarks ($XDG_CONFIG_HOME/gtk-3.0/bookmarks, read fallback ~/.gtk-bookmarks): "URI [label]" lines, as flxp's
   src/shell/bookmarks.cpp */
static void bookmarks_file(char *out, size_t n) {
    const char *x = getenv("XDG_CONFIG_HOME"), *h = getenv("HOME");
    if (x && *x) snprintf(out, n, "%s/gtk-3.0/bookmarks", x); else snprintf(out, n, "%s/.config/gtk-3.0/bookmarks", h ? h : "");
}
static int hexval(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static int uri_to_path(const char *uri, char *out, size_t n) {
    if (strncmp(uri, "file://", 7)) return 0;
    const char *s = uri + 7; size_t k = 0;
    if (*s != '/') { s = strchr(s, '/'); if (!s) return 0; }   /* file://host/path */
    for (; *s && k + 1 < n; s++) {
        if (*s == '%' && hexval(s[1]) >= 0 && hexval(s[2]) >= 0) { out[k++] = (char)(hexval(s[1]) * 16 + hexval(s[2])); s += 2; }
        else out[k++] = *s;
    }
    out[k] = 0;
    return 1;
}
static void path_to_uri(const char *p, char *out, size_t n) {
    size_t k = (size_t)snprintf(out, n, "file://");
    for (; *p && k + 4 < n; p++) {
        unsigned char c = (unsigned char)*p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("/-_.~!$&'()*+,;=:@", c)) out[k++] = (char)c;
        else k += (size_t)snprintf(out + k, n - k, "%%%02X", c);
    }
    out[k] = 0;
}
static void popup_favorites(int part) {
    popup_close();
    pop_add("Add current folder", NULL, 0, 1);
    pop_add("", NULL, PF_SEP, 0);
    char f[PATH_MAX]; bookmarks_file(f, sizeof f);
    FILE *fp = fopen(f, "r");
    if (!fp) { const char *h = getenv("HOME"); char g[PATH_MAX]; snprintf(g, sizeof g, "%s/.gtk-bookmarks", h ? h : ""); fp = fopen(g, "r"); }
    int n = 0;
    if (fp) {
        char line[PATH_MAX * 3 + 512];
        while (fgets(line, sizeof line, fp)) {
            line[strcspn(line, "\r\n")] = 0;
            if (!line[0]) continue;
            char *sp = strchr(line, ' '), *label = sp ? sp + 1 : NULL, path[PATH_MAX], t[PATH_MAX];
            if (sp) *sp = 0;
            if (uri_to_path(line, path, sizeof path)) { if (!label || !*label) { path_label(path, t, sizeof t); label = t; } pop_add(label, path, PF_FOLDER, 0); }
            else pop_add(label && *label ? label : line, NULL, PF_FOLDER | PF_DISABLED, 0);   /* remote URIs: listed, not opened here */
            n++;
        }
        fclose(fp);
    }
    if (!n) pop_add("(no bookmarks)", NULL, PF_DISABLED, 0);
    popup_show(POP_FAVORITES, part, part, 1, -1);
}
/* append cwd to the bookmarks (atomically: a new file renamed over the old one; through a symlink if the file is one) */
static void bookmark_add(void) {
    char f[PATH_MAX], real[PATH_MAX], uri[PATH_MAX * 3 + 16], tmp[PATH_MAX + 16];
    bookmarks_file(f, sizeof f);
    if (realpath(f, real)) snprintf(f, sizeof f, "%s", real);
    else { char d[PATH_MAX]; snprintf(d, sizeof d, "%s", f); char *sl = strrchr(d, '/'); if (sl) { *sl = 0; mkdir(d, 0755); } }
    path_to_uri(cwd, uri, sizeof uri);
    FILE *in = fopen(f, "r");
    snprintf(tmp, sizeof tmp, "%s.navdemo-tmp", f);
    FILE *out = fopen(tmp, "w");
    if (!out) { if (in) fclose(in); XBell(dpy, 0); return; }
    int dup = 0;
    if (in) {
        char line[PATH_MAX * 3 + 512];
        while (fgets(line, sizeof line, in)) {
            size_t l = strcspn(line, " \r\n");
            if (l == strlen(uri) && !strncmp(line, uri, l)) dup = 1;
            fputs(line, out);
            if (line[strlen(line) - 1] != '\n') fputc('\n', out);
        }
        fclose(in);
    }
    if (!dup) fprintf(out, "%s\n", uri);
    if (fclose(out) || rename(tmp, f)) { unlink(tmp); XBell(dpy, 0); return; }
    if (dup) XBell(dpy, 0);
    fprintf(stderr, "navdemo: %s %s in %s\n", dup ? "already bookmarked:" : "bookmarked", cwd, f);
}
static void popup_views(int part) {
    popup_close();
    pop_add("List", NULL, view_mode == 0 ? PF_CHECK : 0, 0);
    pop_add("Details", NULL, view_mode == 1 ? PF_CHECK : 0, 1);
    popup_show(POP_VIEWS, part, part, 0, view_mode);
}
static void filter(void);
static int pop_y_item(int y) {   /* the item at list row y */
    int yy = 2;
    for (int i = pop_top; i < pop_n && i < pop_top + pop_rows; i++) { int h = pop_line_h(i); if (y >= yy && y < yy + h) return i; yy += h; }
    return -1;
}
static int popup_item(int x, int y) { return x >= 0 && x < pop_w ? pop_y_item(y) : -1; }
static void popup_pick(int i) {
    if (!pop_pickable(i)) return;
    struct pop_item it = pop_items[i]; char path[PATH_MAX] = "";
    if (it.path) snprintf(path, sizeof path, "%s", it.path);
    int kind = pop_kind;
    popup_close();
    switch (kind) {
    case POP_HISTORY: history_step(it.val); break;
    case POP_FOLDERS: go(path, 1); break;
    case POP_FAVORITES: if (it.val == 1) bookmark_add(); else if (path[0]) go(path, 1); break;
    case POP_VIEWS: view_mode = it.val; break;
    }
}
static void pop_scroll_to(int i) {
    if (i < pop_top) pop_top = i;
    if (i >= pop_top + pop_rows) pop_top = i - pop_rows + 1;
}
static void pop_move(int d) {   /* Up / Down over the pickable items */
    int i = pop_hot;
    for (int k = 0; k < pop_n; k++) {
        i = i < 0 ? (d > 0 ? 0 : pop_n - 1) : i + d;
        if (i < 0 || i >= pop_n) return;
        if (pop_pickable(i)) { pop_hot = i; pop_scroll_to(i); popup_paint(); return; }
    }
}
/* the list's events (the pointer and keyboard are grabbed while it is open); 1 = the main window needs a repaint */
static int popup_event(XEvent *e) {
    switch (e->type) {
    case Expose: if (e->xexpose.count == 0) popup_paint(); return 0;
    case MotionNotify: { int i = popup_item(e->xmotion.x, e->xmotion.y); if (i != pop_hot) { pop_hot = i; popup_paint(); } return 0; }
    case ButtonPress:   /* wheel: scroll; outside: close; inside: the release picks */
        if (e->xbutton.button == 4 || e->xbutton.button == 5) {
            pop_top += e->xbutton.button == 4 ? -3 : 3;
            if (pop_top > pop_n - pop_rows) pop_top = pop_n - pop_rows;
            if (pop_top < 0) pop_top = 0;
            popup_paint(); return 0; }
        if (popup_item(e->xbutton.x, e->xbutton.y) < 0) { popup_close(); return 1; }
        return 0;
    case ButtonRelease: { int i = popup_item(e->xbutton.x, e->xbutton.y);   /* a release outside (the opening press's) keeps it open */
        if (i >= 0 && e->xbutton.button == 1 && pop_pickable(i)) { popup_pick(i); return 1; }
        return 0; }
    case KeyPress: {
        KeySym k = XLookupKeysym(&e->xkey, 0);
        if (k == XK_Escape) { popup_close(); return 1; }
        if (k == XK_Down) pop_move(1);
        if (k == XK_Up) pop_move(-1);
        if (k == XK_Home) { pop_hot = -1; pop_move(1); }
        if (k == XK_End) { pop_hot = -1; pop_move(-1); }
        if ((k == XK_Return || k == XK_KP_Enter) && pop_hot >= 0) { popup_pick(pop_hot); return 1; }
        if (k >= XK_a && k <= XK_z) {   /* type-ahead: the next item starting with that letter */
            for (int s = 1; s <= pop_n; s++) { int i = ((pop_hot < 0 ? -1 : pop_hot) + s) % pop_n;
                if (pop_pickable(i) && (pop_items[i].label[0] | 32) == (int)k) { pop_hot = i; pop_scroll_to(i); popup_paint(); break; } }
        }
        return 0; }
    }
    return 0;
}

/* ---- tooltips: a small window under the pointer after a short hover over a part with lhnav_tip text ---- */
#define TIP_DELAY_MS 500
static Window tipwin;
static int tip_part = LHNAV_NONE, tip_x, tip_y;   /* the part waited for, the pointer's root position */
static long long tip_due;                          /* ms; 0 = nothing pending */
static long long now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000; }
static void tip_hide(void) { if (tipwin) { XDestroyWindow(dpy, tipwin); tipwin = 0; XFlush(dpy); } tip_due = 0; }
static void tip_track(int part, int rx, int ry) {   /* on motion: a new part restarts the wait */
    if (part == tip_part) return;
    tip_hide(); tip_part = part; tip_x = rx; tip_y = ry;
    const char *t = part == LHNAV_SEARCH || part == LHNAV_ADDRESS ? NULL : lhnav_tip(nav, part);
    if (t && *t && !popup) tip_due = now_ms() + TIP_DELAY_MS;
}
static int tip_w, tip_h;
static void tip_draw(void) {
    const char *t = lhnav_tip(nav, tip_part);
    if (!tipwin || !t) return;
    cairo_surface_t *xs = cairo_xlib_surface_create(dpy, tipwin, DefaultVisual(dpy, DefaultScreen(dpy)), tip_w, tip_h);
    cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tip_w, tip_h);
    cairo_t *cr = cairo_create(img);
    cairo_set_source_rgb(cr, 1, 1, 0.882); cairo_paint(cr);   /* XP's tooltip: infobk (255,255,225), black frame and text */
    cairo_set_source_rgb(cr, 0, 0, 0); cairo_set_line_width(cr, 1); cairo_rectangle(cr, 0.5, 0.5, tip_w - 1, tip_h - 1); cairo_stroke(cr);
    cairo_destroy(cr);
    ui_text(img, 5, 3, t, 0, 0x000000);
    cr = cairo_create(xs); cairo_set_source_surface(cr, img, 0, 0); cairo_paint(cr); cairo_destroy(cr);
    cairo_surface_destroy(img); cairo_surface_destroy(xs); XFlush(dpy);
}
static void tip_show(void) {
    tip_due = 0;
    const char *t = lhnav_tip(nav, tip_part);
    if (!t || !*t || popup) return;
    int h, w = lhnav_text_size(nav, t, 0, &h);
    w += 10; h += 6;
    int sw = DisplayWidth(dpy, DefaultScreen(dpy)), x = tip_x, y = tip_y + 20;
    if (x + w > sw) x = sw - w;
    XSetWindowAttributes a; memset(&a, 0, sizeof a);
    a.override_redirect = True; a.background_pixel = WhitePixel(dpy, DefaultScreen(dpy)); a.event_mask = ExposureMask;
    tipwin = XCreateWindow(dpy, root, x, y, (unsigned)w, (unsigned)h, 0, CopyFromParent, InputOutput, CopyFromParent,
                           CWOverrideRedirect | CWBackPixel | CWEventMask, &a);
    tip_w = w; tip_h = h;
    XMapRaised(dpy, tipwin);   /* drawn on its Expose */
    XFlush(dpy);
}
static void tip_expose(void) { tip_draw(); }

/* ---- the toolbar's actions ---- */
/* New Folder: "New Folder", "New Folder (2)", ... in cwd; the listing refreshed with it selected */
static void new_folder(void) {
    char p[PATH_MAX + 64], name[64];
    for (int k = 1; k < 1000; k++) {
        if (k == 1) snprintf(name, sizeof name, "New Folder"); else snprintf(name, sizeof name, "New Folder (%d)", k);
        snprintf(p, sizeof p, "%s/%s", strcmp(cwd, "/") ? cwd : "", name);
        if (mkdir(p, 0777) == 0) {
            char here[PATH_MAX]; snprintf(here, sizeof here, "%s", cwd);
            go(here, 0);
            for (int i = 0; i < nvis; i++) if (!strcmp(ent[vis_idx[i]], name)) sel = i;
            fprintf(stderr, "navdemo: created %s\n", p);
            return;
        }
        if (errno != EEXIST) break;
    }
    XBell(dpy, 0);
}
static void expand_home(const char *in, char *out, size_t n) {
    const char *h = getenv("HOME");
    if (in[0] == '~' && (in[1] == '/' || !in[1]) && h) snprintf(out, n, "%s%s", h, in + 1); else snprintf(out, n, "%s", in);
}

/* ---- painting ---- */
/* without composition: the bottom `top` rows of luxwm's header pixmap (the frame's caption band continued down) */
static int paint_header_pixmap(cairo_t *cr, int top) {
    int n; long *v = read_cardinals(win, a_header_pixmap, 1, &n); Pixmap pix = v && n == 1 ? (Pixmap)v[0] : None; free(v);
    if (pix == None) return 0;
    Window r; int px, py; unsigned pw, ph, bw, pd;
    XSync(dpy, False); x_error = 0;
    XErrorHandler old = XSetErrorHandler(on_x_error);
    Status ok = XGetGeometry(dpy, pix, &r, &px, &py, &pw, &ph, &bw, &pd);
    XSync(dpy, False);
    if (ok && !x_error) {
        cairo_surface_t *ps = cairo_xlib_surface_create(dpy, pix, vis, (int)pw, (int)ph);
        cairo_save(cr); cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_rectangle(cr, 0, 0, W, top); cairo_clip(cr);
        cairo_set_source_surface(cr, ps, 0, top - (int)ph); cairo_paint(cr); cairo_restore(cr);
        cairo_surface_destroy(ps);
        XSync(dpy, False);
    }
    XSetErrorHandler(old);
    return ok && !x_error;
}

static void paint(cairo_surface_t *xs) {
    int top = lhnav_extend_top(nav);
    cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);   /* offscreen: no flicker */
    cairo_t *cr = cairo_create(img);
    /* 1. transparent (premultiplied 0,0,0,0): luxwm's glass shows through wherever we draw nothing */
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE); cairo_set_source_rgba(cr, 0, 0, 0, 0); cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_surface_flush(img);
    /* 2. the header: glass, or opaque from luxwm's pixmap (a gradient of our own without one), then lhnav */
    if (!composited && !paint_header_pixmap(cr, top)) {
        cairo_pattern_t *g = cairo_pattern_create_linear(0, 0, 0, top);
        cairo_pattern_add_color_stop_rgb(g, 0, 0.93, 0.95, 0.98); cairo_pattern_add_color_stop_rgb(g, 1, 0.78, 0.84, 0.92);
        cairo_set_source(cr, g); cairo_rectangle(cr, 0, 0, W, top); cairo_fill(cr); cairo_pattern_destroy(g);
    }
    cairo_surface_flush(img);
    lhnav_paint(nav, (uint32_t *)cairo_image_surface_get_data(img), W, H, cairo_image_surface_get_stride(img), composited);
    cairo_surface_mark_dirty(img);
    /* the caption buttons' rect stays empty (transparent: luxwm's buttons are under it) */
    if (cap_rect[2] > 0) { cairo_save(cr); cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_rectangle(cr, cap_rect[0], cap_rect[1], cap_rect[2], cap_rect[3]); cairo_fill(cr); cairo_restore(cr); }
    /* 3. the body: opaque white, one line per entry */
    cairo_set_source_rgb(cr, 1, 1, 1); cairo_rectangle(cr, 0, top, W, H - top); cairo_fill(cr);
    PangoLayout *lay = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_from_string(BODY_FONT);
    pango_layout_set_font_description(lay, fd);
    int rows = (H - top - 6) / LINE_H;
    int body0 = top + 4, xs_ = W - 230, xd = W - 150;   /* Details: Size and Date Modified columns at the right */
    if (view_mode == 1) {   /* the column header */
        cairo_set_source_rgb(cr, 0.95, 0.95, 0.96); cairo_rectangle(cr, 0, top, W, LINE_H + 2); cairo_fill(cr);
        cairo_set_source_rgb(cr, 0.78, 0.80, 0.84); cairo_rectangle(cr, 0, top + LINE_H + 2, W, 1); cairo_fill(cr);
        static const char *const col[3] = { "Name", "Size", "Date Modified" };
        int cx[3] = { 30, xs_, xd };
        for (int c = 0; c < 3; c++) { pango_layout_set_text(lay, col[c], -1); cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
            cairo_move_to(cr, cx[c], top + 2); pango_cairo_show_layout(cr, lay); }
        body0 = top + LINE_H + 6; rows = (H - body0 - 2) / LINE_H;
    }
    for (int i = 0; i < rows && scroll + i < nvis; i++) {
        int e = vis_idx[scroll + i], y = body0 + i * LINE_H;
        if (scroll + i == sel) { cairo_set_source_rgb(cr, 0.80, 0.88, 0.98); cairo_rectangle(cr, 4, y, W - 8, LINE_H); cairo_fill(cr); }
        if (isdir[e]) {   /* a small folder shape */
            cairo_set_source_rgb(cr, 0.93, 0.78, 0.35); cairo_rectangle(cr, 10, y + 5, 14, 9); cairo_fill(cr);
            cairo_rectangle(cr, 10, y + 3, 6, 3); cairo_fill(cr);
        } else { cairo_set_source_rgb(cr, 0.75, 0.78, 0.82); cairo_rectangle(cr, 12, y + 3, 10, 12); cairo_stroke(cr); }
        char line[600]; snprintf(line, sizeof line, "%s%s", ent[e], isdir[e] ? "/" : "");
        pango_layout_set_text(lay, line, -1);
        if (view_mode == 1) { cairo_save(cr); cairo_rectangle(cr, 0, y, xs_ - 10, LINE_H); cairo_clip(cr); }
        cairo_set_source_rgb(cr, 0.1, 0.1, 0.1); cairo_move_to(cr, 30, y + 1);
        pango_cairo_show_layout(cr, lay);
        if (view_mode == 1) {
            cairo_restore(cr);
            char sz[32] = "", dt[32] = "";
            long long b = ent_size[e];
            if (!isdir[e] && b >= 0) { if (b < 1024) snprintf(sz, sizeof sz, "%lld bytes", b); else snprintf(sz, sizeof sz, "%lld KB", (b + 1023) / 1024); }
            struct tm tm; if (ent_mtime[e] && localtime_r(&ent_mtime[e], &tm)) strftime(dt, sizeof dt, "%Y-%m-%d %H:%M", &tm);
            pango_layout_set_text(lay, sz, -1); cairo_move_to(cr, xs_, y + 1); pango_cairo_show_layout(cr, lay);
            pango_layout_set_text(lay, dt, -1); cairo_move_to(cr, xd, y + 1); pango_cairo_show_layout(cr, lay);
        }
    }
    if (!nvis) { pango_layout_set_text(lay, search[0] ? "No items match the search." : "This folder is empty.", -1);
                 cairo_set_source_rgb(cr, 0.4, 0.4, 0.4); cairo_move_to(cr, 30, top + 6); pango_cairo_show_layout(cr, lay); }
    pango_font_description_free(fd); g_object_unref(lay);
    cairo_destroy(cr);
    cr = cairo_create(xs); cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE); cairo_set_source_surface(cr, img, 0, 0); cairo_paint(cr);
    cairo_destroy(cr); cairo_surface_destroy(img); cairo_surface_flush(xs); XFlush(dpy);
}

/* the body line under y, -1 for none */
static int line_at(int y) {
    int top = lhnav_extend_top(nav) + (view_mode == 1 ? LINE_H + 2 : 0), i = (y - top - 4) / LINE_H;
    if (y < top + 4 || i < 0 || scroll + i >= nvis) return -1;
    return scroll + i;
}
static void open_line(int i) {
    if (i < 0 || i >= nvis || !isdir[vis_idx[i]]) return;
    char p[PATH_MAX + 256]; snprintf(p, sizeof p, "%s/%s", strcmp(cwd, "/") ? cwd : "", ent[vis_idx[i]]);
    go(p, 1);
}

int main(int argc, char **argv) {
    const char *arg = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-r") && i + 1 < argc) { if (!realpath(argv[++i], rootdir)) { perror(argv[i]); return 2; } }
        else if (argv[i][0] != '-' && !arg) arg = argv[i];
        else { fprintf(stderr, "usage: navdemo [-r ROOT] [DIRECTORY]\n"); return 2; }
    }
    if (!(dpy = XOpenDisplay(NULL))) { fprintf(stderr, "navdemo: cannot open display\n"); return 1; }
    int scr = DefaultScreen(dpy); root = RootWindow(dpy, scr);
    a_composited = XInternAtom(dpy, "_LHWM_COMPOSITED", False); a_theme = XInternAtom(dpy, "_LHWM_THEME", False);
    a_lh_theme = XInternAtom(dpy, "_LH_THEME", False); a_lh_resolved = XInternAtom(dpy, "_LH_THEME_RESOLVED", False);
    a_lh_serial = XInternAtom(dpy, "_LH_SETTINGS_SERIAL", False);
    a_metrics = XInternAtom(dpy, "_LHWM_FRAME_METRICS", False); a_extend = XInternAtom(dpy, "_LHWM_EXTEND_FRAME", False);
    a_flags = XInternAtom(dpy, "_LHWM_FRAME_FLAGS", False); a_header_pixmap = XInternAtom(dpy, "_LHWM_HEADER_PIXMAP", False);
    a_caption_rect = XInternAtom(dpy, "_LHWM_CAPTION_RECT", False); a_moveresize = XInternAtom(dpy, "_NET_WM_MOVERESIZE", False);
    a_state = XInternAtom(dpy, "_NET_WM_STATE", False); a_max_v = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    a_max_h = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False); a_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    a_name = XInternAtom(dpy, "_NET_WM_NAME", False); a_utf8 = XInternAtom(dpy, "UTF8_STRING", False);

    /* rule 1: 32-bit ARGB visual, own colormap, border/background pixel 0 */
    XVisualInfo vi; int argb = XMatchVisualInfo(dpy, scr, 32, TrueColor, &vi);
    vis = argb ? vi.visual : DefaultVisual(dpy, scr); int depth = argb ? 32 : DefaultDepth(dpy, scr);
    XSetWindowAttributes a; memset(&a, 0, sizeof a);
    a.colormap = XCreateColormap(dpy, root, vis, AllocNone); a.border_pixel = 0; a.background_pixel = 0;
    a.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | LeaveWindowMask
                 | KeyPressMask | PropertyChangeMask | FocusChangeMask;
    win = XCreateWindow(dpy, root, 140, 90, W, H, 0, depth, InputOutput, vis, CWColormap | CWBorderPixel | CWBackPixel | CWEventMask, &a);
    XClassHint ch = { "navdemo", "NavDemo" }; XSetClassHint(dpy, win, &ch);
    XSetWMProtocols(dpy, win, &a_delete, 1);

    /* rule 6: root PropertyNotify selected before anything is read */
    XSelectInput(dpy, root, PropertyChangeMask);
    composited = argb && read_composited();
    if (getenv("NAVDEMO_PRESET")) snprintf(preset_sel, sizeof preset_sel, "%s", getenv("NAVDEMO_PRESET"));
    load_theme();
    char start[PATH_MAX]; if (arg) snprintf(start, sizeof start, "%s", arg); else if (!getcwd(start, sizeof start)) strcpy(start, "/");
    if (!go(start, 1) && !go("/", 1)) return 1;
    apply_metrics();   /* rule 2: _LHWM_EXTEND_FRAME and _LHWM_FRAME_FLAGS before mapping */

    Cursor arrow = XCreateFontCursor(dpy, XC_left_ptr), north = XCreateFontCursor(dpy, XC_top_side), ibeam = XCreateFontCursor(dpy, XC_xterm);
    Cursor cur = arrow;
    cairo_surface_t *xs = cairo_xlib_surface_create(dpy, win, vis, W, H);
    XMapWindow(dpy, win);
    Time last_click = 0; int last_part = -1, last_line = -1, dirty = 0, relayout = 0;
    for (;;) {
        /* coalesce: handle the queue, repaint once it is empty (a theme switch sends a burst of PropertyNotify) */
        if (!XPending(dpy) && (dirty || relayout)) {
            if (relayout) { apply_metrics(); relayout = 0; }
            paint(xs); dirty = 0;
        }
        if (!XPending(dpy) && tip_due) {   /* a tooltip is due: wait for an event at most until then */
            long long wait = tip_due - now_ms();
            struct pollfd pf = { ConnectionNumber(dpy), POLLIN, 0 };
            if (wait <= 0 || poll(&pf, 1, (int)wait) == 0) { tip_show(); continue; }
        }
        XEvent e; XNextEvent(dpy, &e);
        if (tipwin && e.xany.window == tipwin) { if (e.type == Expose && e.xexpose.count == 0) tip_expose(); continue; }
        if (popup && e.xany.window == popup) { if (popup_event(&e)) dirty = 1; continue; }
        switch (e.type) {
        case Expose: if (e.xexpose.count == 0) dirty = 1; break;
        case ConfigureNotify:
            if (e.xconfigure.width != W || e.xconfigure.height != H) {
                W = e.xconfigure.width; H = e.xconfigure.height; cairo_xlib_surface_set_size(xs, W, H); relayout = 1; }
            break;
        case FocusIn: case FocusOut:
            if (e.xfocus.detail != NotifyPointer) { focused = e.type == FocusIn; lhnav_set_frame_state(nav, focused, maximized); dirty = 1; }
            break;
        case PropertyNotify: {
            Atom at = e.xproperty.atom;
            if (e.xproperty.window == root && (at == a_composited || at == a_theme || at == a_lh_theme || at == a_lh_resolved || at == a_metrics)) {
                /* WM_THEMECHANGED (and luxwm's theme reload): style, fonts, nav.ini, composition, metrics */
                composited = argb && read_composited();
                if (at == a_lh_theme || at == a_lh_resolved || at == a_theme || at == a_metrics) load_theme();
                relayout = 1;
            } else if (e.xproperty.window == win && (at == a_caption_rect || at == a_state)) {
                maximized = read_maximized(); relayout = 1;   /* republished on every maximise/restore */
            } else if (e.xproperty.window == win && at == a_header_pixmap && !composited) dirty = 1;
            break; }
        case LeaveNotify:   /* not the grab of our own lists: they keep the pressed / open look */
            tip_track(LHNAV_NONE, 0, 0);
            if (!popup && lhnav_pointer(nav, -1, -1, 0)) dirty = 1;
            break;
        case MotionNotify: {
            if (lhnav_pointer(nav, e.xmotion.x, e.xmotion.y, (e.xmotion.state & Button1Mask) ? 1 : 0)) dirty = 1;
            int p = lhnav_hit(nav, e.xmotion.x, e.xmotion.y);
            if (!(e.xmotion.state & Button1Mask)) tip_track(p, e.xmotion.x_root, e.xmotion.y_root);
            Cursor want = p == LHNAV_RESIZE_TOP ? north : (p == LHNAV_SEARCH || (p == LHNAV_ADDRESS && editing == 1)) ? ibeam : arrow;
            if (want != cur) { XDefineCursor(dpy, win, want); cur = want; }
            break; }
        case ButtonPress: {
            XButtonEvent *b = &e.xbutton;
            if (b->button == 4 || b->button == 5) {   /* wheel: scroll the listing */
                scroll += b->button == 4 ? -3 : 3;
                int rows = (H - lhnav_extend_top(nav) - 6) / LINE_H;
                if (scroll > nvis - rows) scroll = nvis - rows;
                if (scroll < 0) scroll = 0;
                dirty = 1; break; }
            if (b->button == 8 || b->button == 9) { history_step(b->button == 8 ? -1 : 1); dirty = 1; break; }   /* mouse back/forward */
            int p = lhnav_hit(nav, b->x, b->y);
            tip_hide();
            if (b->button == 3 && (p == LHNAV_BACK || p == LHNAV_FWD)) { popup_history(p == LHNAV_BACK ? -1 : 1, p == LHNAV_BACK ? LHNAV_BACK_MENU : LHNAV_FWD_MENU); dirty = 1; break; }
            if (b->button != 1) break;
            int dbl = last_click && b->time - last_click < 400 && p == last_part;
            last_click = b->time; last_part = p;
            if (editing && !(editing == 1 && p == LHNAV_ADDRESS) && !(editing == 2 && p == LHNAV_SEARCH)) { edit_end(); dirty = 1; }   /* a click elsewhere cancels */
            if (lhnav_pointer(nav, b->x, b->y, 1)) dirty = 1;
            switch (p) {
            case LHNAV_RESIZE_TOP: start_moveresize(b, 1); break;
            case LHNAV_BACK_MENU: case LHNAV_FWD_MENU:   /* the drop-down opens on the press, as 4074's split buttons (pressed look while open) */
                XUngrabPointer(dpy, b->time);
                popup_history(p == LHNAV_BACK_MENU ? -1 : 1, p);
                dirty = 1; break;
            case LHNAV_OVERFLOW:   /* "«": the collapsed ancestors */
                XUngrabPointer(dpy, b->time); popup_overflow(); dirty = 1; break;
            case LHNAV_CAPTION: case LHNAV_TITLE:   /* rule 5 */
                if (dbl) { last_click = 0; send_root_message(a_state, 2, (long)a_max_v, (long)a_max_h, 1, 0); }
                else start_moveresize(b, 8);
                lhnav_pointer(nav, -1, -1, 0);
                break;
            case LHNAV_ADDRESS: if (editing != 1) edit_begin(1, -1); else { caret = lhnav_field_index(nav, LHNAV_ADDRESS, address, b->x); field_sync(); } dirty = 1; break;
            case LHNAV_SEARCH: if (editing != 2) edit_begin(2, b->x); else { caret = lhnav_field_index(nav, LHNAV_SEARCH, search, b->x); field_sync(); } dirty = 1; break;
            case LHNAV_NONE: {   /* the body */
                int i = line_at(b->y);
                if (i >= 0 && i == last_line && dbl) { last_click = 0; open_line(i); }
                else { sel = i; last_line = i; }
                dirty = 1; break; }
            default:
                if (LHNAV_IS_SEP(p)) { XUngrabPointer(dpy, b->time); popup_folders(p - LHNAV_SEP); dirty = 1; break; }   /* on the press, as flxp */
                if (LHNAV_IS_TOOL(p)) {   /* the drop-down items open on the press; the others act on release */
                    int k = lhnav_tool(nav, p - LHNAV_TOOL, NULL, NULL);
                    if (k == LHNAV_TOOL_FAVORITES) { XUngrabPointer(dpy, b->time); popup_favorites(p); dirty = 1; }
                    else if (k == LHNAV_TOOL_VIEWS) { XUngrabPointer(dpy, b->time); popup_views(p); dirty = 1; }
                }
                break;   /* back, forward, segments, the other tools: on release (a press that leaves the part does nothing) */
            }
            break; }
        case ButtonRelease: {
            if (e.xbutton.button != 1) break;
            int pr = lhnav_pressed(nav), p = lhnav_hit(nav, e.xbutton.x, e.xbutton.y);
            if (lhnav_pointer(nav, e.xbutton.x, e.xbutton.y, 0)) dirty = 1;
            if (pr == LHNAV_NONE || pr != p) break;
            char path[PATH_MAX];
            if (p == LHNAV_BACK) history_step(-1);
            else if (p == LHNAV_FWD) history_step(1);
            else if (LHNAV_IS_CRUMB(p)) { crumb_path(p - LHNAV_CRUMB, path, sizeof path); if (strcmp(path, cwd)) go(path, 1); }
            else if (LHNAV_IS_TOOL(p)) {
                const char *arg, *label; int k = lhnav_tool(nav, p - LHNAV_TOOL, &arg, &label);
                if (k == LHNAV_TOOL_SEARCH) edit_begin(2, -1);   /* reveals the field (lhnav_set_search_open) and focuses it */
                else if (k == LHNAV_TOOL_NEWFOLDER) new_folder();
                else if (k == LHNAV_TOOL_BOOKMARK && arg) { expand_home(arg, path, sizeof path); go(path, 1); }
            }
            lhnav_pointer(nav, e.xbutton.x, e.xbutton.y, 0);
            dirty = 1;
            break; }
        case KeyPress: {
            tip_hide();
            char txt[16]; KeySym k; int len = XLookupString(&e.xkey, txt, sizeof txt - 1, &k, NULL); txt[len > 0 ? len : 0] = 0;
            int ctrl = e.xkey.state & ControlMask, alt = e.xkey.state & Mod1Mask;
            if (ctrl && (k == XK_q || k == XK_Q)) goto out;
            if (ctrl && (k == XK_l || k == XK_L)) { edit_begin(1, -1); dirty = 1; break; }
            if (ctrl && (k == XK_f || k == XK_F)) { edit_begin(2, -1); dirty = 1; break; }
            if (editing) { if (edit_key(k, txt, ctrl ? 0 : len)) dirty = 1; break; }
            if (alt && k == XK_Left) history_step(-1);
            else if (alt && k == XK_Right) history_step(1);
            else if (k == XK_BackSpace) { char p[PATH_MAX + 4]; snprintf(p, sizeof p, "%s/..", cwd); go(p, 1); }
            else if (k == XK_Escape) goto out;
            else if (k == XK_p || k == XK_P) { cycle_preset(k == XK_P ? -1 : 1); relayout = 1; }
            else if (k == XK_Down && sel + 1 < nvis) sel++;
            else if (k == XK_Up && sel > 0) sel--;
            else if (k == XK_Return) open_line(sel);
            else if (len == 1 && (unsigned char)txt[0] >= 32) { edit_begin(2, -1); edit_key(k, txt, len); }   /* type to search */
            else break;
            int rows = (H - lhnav_extend_top(nav) - 6) / LINE_H;   /* keep the selection in view */
            if (sel >= 0 && sel < scroll) scroll = sel;
            if (sel >= scroll + rows) scroll = sel - rows + 1;
            dirty = 1;
            break; }
        case ClientMessage:
            if ((Atom)e.xclient.data.l[0] == a_delete) goto out;
            break;
        }
    }
out:
    cairo_surface_destroy(xs);
    lhnav_free(nav);
#ifdef HAVE_UXTHEME
    if (style) ut_close(style);
#endif
    XDestroyWindow(dpy, win); XCloseDisplay(dpy);
    return 0;
}
