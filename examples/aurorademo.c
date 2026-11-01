/* aurorademo: Longhorn's two "Aurora" effects, played from their original numbers (build/examples/aurora_data.h):
   the Explorer preview-pane aurora "APPA" (jade.msstyles STREAM 481, 4074/4082) recoloured for a folder type over the pane's
   band gradient, and the desktop aurora (desktopaurora.baml, MILDesktop=1). Spec: aurora.md in the research workspace.
   build: make examples (build/examples/aurorademo). The tables are read from the user's Windows tree: lhde-import's [aurora]
   recipes (data/sources/4074.ini) write generated/aurora/{previewpane,desktop}-4074.json, make turns them into the header
   (tools/lhdeaurora.py); a scene not imported is left out and asking for it says so.
   usage: aurorademo [pane [FOLDER] | desktop] [-s SPEED] [-w WxH] [-f] [-t SECONDS] [-j THREADS] [-b FRAMES]
     pane     1000x148 (the aurora cell of a ~1000 px Explorer pane); FOLDER document music picture video contact message game
              default (default: picture). The 1080x160 scene is stretched non-uniformly to the window, as Explorer did.
     desktop  800x600, the 800x600 scene stretched to the window
     -s speed (1 = original timing)  -w window size  -f root-sized  -t start at this scene time
     -j render threads (horizontal bands; default one per CPU, at most 16)
     -b render FRAMES offscreen, print ms/frame and exit (AURORADEMO_PNG=FILE saves the last frame)
     keys: f cycles the folder type, q / Escape quit
   Rendering: source-over alpha only, as the original. Gradients are linear in the path's bounding box (isolines
   perpendicular in bbox space for the pane, in path space for the desktop: aurora.md 3.3), every animation is evaluated from one monotonic clock, opacity groups are composited as
   layers (cairo_push_group). The desktop's watermark text is not drawn. */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/select.h>
#include <unistd.h>
#include <time.h>
#include "aurora_data.h"   /* build/examples/: made by make from the Windows tree (tools/lhdeaurora.py) */
#ifdef AURORA_NO_PANE
#define PANE_SCENE NULL
#else
#define PANE_SCENE (&pane_scene)
#endif
#ifdef AURORA_NO_DESKTOP
#define DESKTOP_SCENE NULL
#else
#define DESKTOP_SCENE (&desktop_scene)
#endif

/* Aurora.c_rgColorMap by ShellView.FolderType, and the pane band (edge, centre, edge) behind it (taskpane/managed.md) */
static const struct folder { const char *name; unsigned aurora, edge, centre; } folders[] = {
    {"document", 0x5190DA, 0x24438E, 0x5573BB}, {"music", 0xDA5151, 0x741414, 0xD84848},
    {"picture", 0x6F4970, 0x260827, 0x69466A},  {"video", 0x6F4970, 0x260827, 0x69466A},
    {"contact", 0xF2A47B, 0xD2642A, 0xEB8B58},  {"message", 0x9ECA4E, 0x6E9724, 0x9CC05A},
    {"game", 0x9ECA4E, 0x6E9724, 0x9CC05A},     {"default", 0x8599B4, 0x5A6B7D, 0x8799B1},
};
#define NFOLDERS (int)(sizeof folders / sizeof *folders)

static double now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }

/* linear From->To (or key frames) over Duration; AutoReverse = triangle wave, else sawtooth; repeat forever */
static double anim(const struct au_anim *a, double t) {
    t = t < a->begin ? 0 : t - a->begin;
    double p = fmod(t, a->autorev ? 2 * a->dur : a->dur) / a->dur;
    if (p > 1) p = 2 - p;
    if (!a->nkeys) return a->from + (a->to - a->from) * p;
    for (int i = 0; i + 1 < a->nkeys; i++)
        if (p <= a->kt[i + 1]) return a->kv[i] + (a->kv[i + 1] - a->kv[i]) * (p - a->kt[i]) / (a->kt[i + 1] - a->kt[i]);
    return a->kv[a->nkeys - 1];
}

/* the bounding-box gradient in path space: A = B.G maps unit space to the path; P0 = A(start), g = A^-T v / |v|^2,
   so the parameter at p is (p - P0).g and the isolines stay perpendicular in unit space; P1 = P0 + g / |g|^2 */
static cairo_pattern_t *paint(const struct au_paint *p, const double *bb, unsigned tint, double alpha) {
    cairo_matrix_t B = {bb[2], 0, 0, bb[3], bb[0], bb[1]}, G = {p->gt[0], p->gt[1], p->gt[2], p->gt[3], p->gt[4], p->gt[5]}, A;
    cairo_matrix_multiply(&A, &G, &B);
    double x0 = p->sx, y0 = p->sy, vx = p->ex - p->sx, vy = p->ey - p->sy, vv = vx * vx + vy * vy;
    if (p->pathspace) {   /* [gradient-space fix] desktop: start/end scaled into the bbox, isolines perpendicular in path space */
        double x1 = p->ex, y1 = p->ey;
        cairo_matrix_transform_point(&A, &x0, &y0); cairo_matrix_transform_point(&A, &x1, &y1);
        if ((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0) < 1e-12) return NULL;
        cairo_pattern_t *pat = cairo_pattern_create_linear(x0, y0, x1, y1);
        for (int i = 0; i < p->nstops; i++) { unsigned c = p->stops[i].argb;
            cairo_pattern_add_color_stop_rgba(pat, p->stops[i].off, (c >> 16 & 255) / 255., (c >> 8 & 255) / 255., (c & 255) / 255.,
                                              (c >> 24) / 255. * alpha); }
        cairo_pattern_set_extend(pat, CAIRO_EXTEND_PAD);
        return pat;
    }
    double det = A.xx * A.yy - A.xy * A.yx;
    if (fabs(det) < 1e-12 || vv < 1e-12) return NULL;
    cairo_matrix_transform_point(&A, &x0, &y0);
    double gx = (A.yy * vx - A.yx * vy) / det / vv, gy = (A.xx * vy - A.xy * vx) / det / vv, gg = gx * gx + gy * gy;
    cairo_pattern_t *pat = cairo_pattern_create_linear(x0, y0, x0 + gx / gg, y0 + gy / gg);
    for (int i = 0; i < p->nstops; i++) {
        unsigned c = p->stops[i].argb;
        if (tint && (c & 0xFFFFFF) == 0x5190DA) c = (c & 0xFF000000) | tint;   /* Aurora._AdjustAurora: hue swap, alpha kept */
        cairo_pattern_add_color_stop_rgba(pat, p->stops[i].off, (c >> 16 & 255) / 255., (c >> 8 & 255) / 255., (c & 255) / 255.,
                                          (c >> 24) / 255. * alpha);
    }
    cairo_pattern_set_extend(pat, CAIRO_EXTEND_PAD);
    return pat;
}

static void layer(cairo_t *cr, const struct au_scene *s, const struct au_layer *L, double t, unsigned tint, double alpha) {
    cairo_matrix_t M, S;
    cairo_matrix_init_identity(&M);
    for (int i = 0; i < L->nsteps; i++) {           /* application order: the first step touches the points first */
        const struct au_step *st = &L->steps[i];
        switch (st->type) {
        case AU_T: cairo_matrix_init_translate(&S, st->anim >= 0 ? anim(&s->anims[st->anim], t) : st->a, st->b); break;
        case AU_R: cairo_matrix_init_rotate(&S, st->a * M_PI / 180); break;
        case AU_S: cairo_matrix_init_scale(&S, st->a, st->b); break;
        default:   cairo_matrix_init(&S, 1, tan(st->b * M_PI / 180), tan(st->a * M_PI / 180), 1, 0, 0);
        }
        if (st->cx || st->cy) {
            cairo_matrix_t a, b; cairo_matrix_init_translate(&a, -st->cx, -st->cy); cairo_matrix_init_translate(&b, st->cx, st->cy);
            cairo_matrix_multiply(&S, &a, &S); cairo_matrix_multiply(&S, &S, &b);
        }
        cairo_matrix_multiply(&M, &M, &S);
    }
    cairo_save(cr);
    cairo_transform(cr, &M);
    const double *p = L->pts;
    for (const char *o = L->ops; *o; o++) switch (*o) {
        case 'M': cairo_move_to(cr, p[0], p[1]); p += 2; break;
        case 'L': cairo_line_to(cr, p[0], p[1]); p += 2; break;
        case 'C': cairo_curve_to(cr, p[0], p[1], p[2], p[3], p[4], p[5]); p += 6; break;
        case 'Z': cairo_close_path(cr); break;
    }
    cairo_pattern_t *f = L->fill ? paint(L->fill, L->bbox, tint, alpha) : NULL;
    cairo_pattern_t *k = L->stroke ? paint(L->stroke, L->bbox, tint, alpha) : NULL;   /* open paths fill as if closed */
    if (f) { cairo_set_source(cr, f); k ? cairo_fill_preserve(cr) : cairo_fill(cr); cairo_pattern_destroy(f); }
    if (k) { cairo_set_source(cr, k); cairo_set_line_width(cr, L->stroke_w); cairo_stroke(cr); cairo_pattern_destroy(k); }
    cairo_new_path(cr);
    cairo_restore(cr);
}

static void frame(cairo_t *cr, const struct au_scene *s, const struct folder *fo, double W, double H, double t) {
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    if (fo) {      /* the pane band: horizontal edge-centre-edge under the aurora */
        cairo_pattern_t *b = cairo_pattern_create_linear(0, 0, W, 0);
        for (int i = 0; i < 3; i++) { unsigned c = i == 1 ? fo->centre : fo->edge;
            cairo_pattern_add_color_stop_rgb(b, i / 2., (c >> 16 & 255) / 255., (c >> 8 & 255) / 255., (c & 255) / 255.); }
        cairo_set_source(cr, b); cairo_paint(cr); cairo_pattern_destroy(b);
    } else { cairo_set_source_rgb(cr, 0, 0, 0); cairo_paint(cr); }
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_save(cr);
    cairo_rectangle(cr, 0, 0, W, H); cairo_clip(cr);
    cairo_scale(cr, W / s->w, H / s->h);
    int cur = -1, pushed = 0, skip = 0; double ga = 1;
    for (int i = 0; i <= s->nlayers; i++) {
        const struct au_layer *L = i < s->nlayers ? &s->layers[i] : NULL;
        if (L && L->collapsed) continue;
        if (!L || L->group != cur) {     /* leaving / entering an opacity group: composite it as one layer */
            if (pushed) { cairo_pop_group_to_source(cr); cairo_paint_with_alpha(cr, ga); pushed = 0; }
            if (!L) break;
            cur = L->group;
            ga = cur >= 0 ? anim(&s->anims[s->groups[cur].opacity_anim], t) : 1;
            skip = ga < 1. / 512;
            if (!skip && ga < 1) { cairo_push_group(cr); pushed = 1; }
        }
        double a = L->opacity_anim >= 0 ? anim(&s->anims[L->opacity_anim], t) : 1;
        if (!skip && a >= 1. / 512) layer(cr, s, L, t, fo ? fo->aurora : 0, a);
    }
    cairo_restore(cr);
}

/* the frame in horizontal bands, one thread each: every band draws the whole scene clipped to its rows (identical pixels) */
struct band { cairo_surface_t *back; const struct au_scene *s; const struct folder *fo; int W, H, y0, y1; double t; pthread_t th; };
static void *band(void *arg) {
    struct band *b = arg; int stride = cairo_image_surface_get_stride(b->back);
    cairo_surface_t *sub = cairo_image_surface_create_for_data(cairo_image_surface_get_data(b->back) + b->y0 * stride,
                                                               CAIRO_FORMAT_RGB24, b->W, b->y1 - b->y0, stride);
    cairo_t *cr = cairo_create(sub);
    cairo_translate(cr, 0, -b->y0); frame(cr, b->s, b->fo, b->W, b->H, b->t);
    cairo_destroy(cr); cairo_surface_destroy(sub);
    return NULL;
}
static void render(cairo_surface_t *back, const struct au_scene *s, const struct folder *fo, int W, int H, double t, int nt) {
    struct band b[16]; if (nt > H) nt = H;
    cairo_surface_flush(back);
    for (int i = 0; i < nt; i++) {
        b[i] = (struct band){back, s, fo, W, H, H * i / nt, H * (i + 1) / nt, t, 0};
        if (i + 1 < nt && pthread_create(&b[i].th, NULL, band, &b[i])) nt = i + 1;
    }
    band(&b[nt - 1]);
    for (int i = 0; i + 1 < nt; i++) pthread_join(b[i].th, NULL);
    cairo_surface_mark_dirty(back);
}

int main(int argc, char **argv) {
    int pane = 1, fi = 2, W = 0, H = 0, full = 0, bench = 0, nt = sysconf(_SC_NPROCESSORS_ONLN); double speed = 1, t0 = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "pane")) pane = 1;
        else if (!strcmp(argv[i], "desktop")) pane = 0;
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) speed = atof(argv[++i]);
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) t0 = atof(argv[++i]);
        else if (!strcmp(argv[i], "-b") && i + 1 < argc) bench = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-w") && i + 1 < argc && sscanf(argv[i + 1], "%dx%d", &W, &H) == 2) i++;
        else if (!strcmp(argv[i], "-j") && i + 1 < argc) nt = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-f")) full = 1;
        else {
            int k = 0; while (k < NFOLDERS && strcmp(argv[i], folders[k].name)) k++;
            if (k == NFOLDERS) { fprintf(stderr, "usage: aurorademo [pane [FOLDER] | desktop] [-s SPEED] [-w WxH] [-f] [-t SEC] [-j N] [-b N]\n"); return 2; }
            fi = k;
        }
    }
    if (nt < 1) nt = 1;
    if (nt > 16) nt = 16;
    const struct au_scene *s = pane ? PANE_SCENE : DESKTOP_SCENE;
    if (!s) {
        fprintf(stderr, "aurorademo: this build has no %s aurora: the Windows tree held no generated/aurora/%s-4074.json when it was made.\n"
                "  lhde-import makes it from %s (data/sources/4074.ini [aurora]); import that, then make examples again.\n",
                pane ? "preview-pane" : "desktop", pane ? "previewpane" : "desktop",
                pane ? "build 4074's jade.msstyles" : "build 4074's Microsoft.Windows.WCPClient.dll");
        return 1;
    }
    if (W <= 0 || H <= 0) { W = pane ? 1000 : 800; H = pane ? 148 : 600; }
    if (bench) {           /* offscreen: the drawing cost alone */
        cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_RGB24, W, H);
        double a = now();
        for (int n = 0; n < bench; n++) render(img, s, pane ? &folders[fi] : NULL, W, H, t0 + n / 30. * speed, nt);
        printf("%s %dx%d, %d thread%s: %.2f ms/frame\n", pane ? "pane" : "desktop", W, H, nt, nt > 1 ? "s" : "", (now() - a) * 1000 / bench);
        if (getenv("AURORADEMO_PNG")) cairo_surface_write_to_png(img, getenv("AURORADEMO_PNG"));
        return 0;
    }
    Display *dpy = XOpenDisplay(NULL); if (!dpy) { fprintf(stderr, "no display\n"); return 1; }
    int scr = DefaultScreen(dpy); Visual *vis = DefaultVisual(dpy, scr);
    if (full) { W = DisplayWidth(dpy, scr); H = DisplayHeight(dpy, scr); }
    XSetWindowAttributes wa; wa.background_pixel = BlackPixel(dpy, scr); wa.event_mask = ExposureMask | KeyPressMask | StructureNotifyMask;
    Window win = XCreateWindow(dpy, RootWindow(dpy, scr), 0, 0, W, H, 0, CopyFromParent, InputOutput, CopyFromParent,
                               CWBackPixel | CWEventMask, &wa);
    char title[64]; snprintf(title, sizeof title, pane ? "aurorademo: pane %s" : "aurorademo: desktop", folders[fi].name);
    XStoreName(dpy, win, title);
    XClassHint ch = {"aurorademo", "aurorademo"}; XSetClassHint(dpy, win, &ch);
    Atom del = XInternAtom(dpy, "WM_DELETE_WINDOW", False); XSetWMProtocols(dpy, win, &del, 1);
    if (full) { Atom st = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
        XChangeProperty(dpy, win, XInternAtom(dpy, "_NET_WM_STATE", False), XA_ATOM, 32, PropModeReplace, (unsigned char *)&st, 1); }
    XMapWindow(dpy, win);
    cairo_surface_t *xs = cairo_xlib_surface_create(dpy, win, vis, W, H);
    cairo_surface_t *back = cairo_image_surface_create(CAIRO_FORMAT_RGB24, W, H);   /* the back buffer: no flicker */
    double start = now(), next = start;
    for (;;) {
        while (XPending(dpy)) {
            XEvent e; XNextEvent(dpy, &e);
            if (e.type == ConfigureNotify && (e.xconfigure.width != W || e.xconfigure.height != H)) {
                W = e.xconfigure.width; H = e.xconfigure.height; cairo_xlib_surface_set_size(xs, W, H);
                cairo_surface_destroy(back); back = cairo_image_surface_create(CAIRO_FORMAT_RGB24, W, H);
            } else if (e.type == KeyPress) {
                KeySym ks = XLookupKeysym(&e.xkey, 0);
                if (ks == XK_q || ks == XK_Escape) return 0;
                if (ks == XK_f && pane) { fi = (fi + 1) % NFOLDERS; snprintf(title, sizeof title, "aurorademo: pane %s", folders[fi].name); XStoreName(dpy, win, title); }
            } else if (e.type == ClientMessage && (Atom)e.xclient.data.l[0] == del) return 0;
        }
        double tnow = now();
        if (tnow >= next) {
            render(back, s, pane ? &folders[fi] : NULL, W, H, t0 + (tnow - start) * speed, nt);
            cairo_t *cr = cairo_create(xs); cairo_set_source_surface(cr, back, 0, 0); cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            cairo_paint(cr); cairo_destroy(cr); cairo_surface_flush(xs); XFlush(dpy);
            next += 1 / 30.; if (next < tnow) next = tnow + 1 / 30.;
            continue;
        }
        fd_set fds; FD_ZERO(&fds); FD_SET(ConnectionNumber(dpy), &fds);
        double w = next - tnow; struct timeval tv = {(long)w, (long)((w - (long)w) * 1e6)};
        select(ConnectionNumber(dpy) + 1, &fds, NULL, NULL, &tv);
    }
}
