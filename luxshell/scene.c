#include <sys/stat.h>
#include "scene.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

const char *default_font = "Segoe UI 9";
void (*scene_custom_paint)(struct node *n, cairo_t *cr);
int scene_filter = SF_BILINEAR;
static cairo_filter_t cfilter(int f) { return f == SF_NEAREST ? CAIRO_FILTER_NEAREST : f == SF_BEST ? CAIRO_FILTER_BEST : CAIRO_FILTER_BILINEAR; }
static int node_filter(const struct node *n) { return n->filter ? n->filter : scene_filter; }

/* ---- image cache ---- */
struct imgent { char *path; cairo_surface_t *s; struct imgent *next; struct timespec mt; off_t size; };
static struct imgent *cache, *stale;
static void file_stamp(const char *path, struct timespec *mt, off_t *size) { struct stat st; if (stat(path, &st) == 0) { *mt = st.st_mtim; *size = st.st_size; } else { mt->tv_sec = mt->tv_nsec = 0; *size = -1; } }
/* image_flush(): forget every cached PNG whose file changed since it was loaded (mtime or size), so the next image_get reads it
   again (_LHSHELL_COMMAND reload, live editing a pack). Nodes borrow the surfaces (n->img), so the old ones are kept on a
   stale list and never freed: a reload leaks only the images that really changed. Returns how many were dropped. */
int image_flush(void) {
    int n = 0;
    for (struct imgent **pp = &cache; *pp; ) {
        struct imgent *e = *pp; struct timespec mt; off_t size; file_stamp(e->path, &mt, &size);
        if (mt.tv_sec != e->mt.tv_sec || mt.tv_nsec != e->mt.tv_nsec || size != e->size) { *pp = e->next; e->next = stale; stale = e; n++; }
        else pp = &e->next;
    }
    return n;
}
cairo_surface_t *icon_lookup(const char *key);
cairo_surface_t *image_get(const char *path) {
    if (!path) return NULL;
    if (path[0] == '@') return icon_lookup(path);
    for (struct imgent *e = cache; e; e = e->next) if (!strcmp(e->path, path)) return e->s;
    cairo_surface_t *s = cairo_image_surface_create_from_png(path);
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) { fprintf(stderr, "luxshell: cannot load %s\n", path); cairo_surface_destroy(s); return NULL; }
    struct imgent *e = calloc(1, sizeof *e); e->path = strdup(path); e->s = s; file_stamp(path, &e->mt, &e->size); e->next = cache; cache = e; return s;
}

/* ---- tree ---- */
static int next_id = 1;
void (*scene_node_freed)(struct node *n);
struct node *node_new(enum kind k) {
    struct node *n = calloc(1, sizeof *n); n->kind = k; n->opacity = 1; n->visible = true; n->sx = n->sy = 1;
    n->w.is_auto = n->h.is_auto = 1; n->color[3] = 1; n->id = next_id++; n->refs = 1; n->stroke_w = 1; n->icon_size = 16; n->pitch_x = 28; n->pitch_y = 24;
    n->layout = (k == N_PANEL) ? L_DOCK : L_FILL; n->dock = D_NONE;
    return n;
}
void node_ref(struct node *n) { n->refs++; }
void node_unref(struct node *n) {
    if (--n->refs > 0) return;
    if (scene_node_freed) scene_node_freed(n);
    node_clear(n); if (n->scaled) cairo_surface_destroy(n->scaled); free(n->kids); free(n->text); free(n->font); free(n);
}
void node_add(struct node *p, struct node *c) {
    node_ref(c);                                       /* the new parent's reference, before the old parent drops its own */
    if (c->parent) node_remove(c);
    if (p->nkids == p->cap) { p->cap = p->cap ? p->cap * 2 : 4; p->kids = realloc(p->kids, p->cap * sizeof *p->kids); }
    p->kids[p->nkids++] = c; c->parent = p; c->owner = p->owner;
}
void node_remove(struct node *c) {
    struct node *p = c->parent; if (!p) return;
    for (int i = 0; i < p->nkids; i++) if (p->kids[i] == c) { memmove(p->kids + i, p->kids + i + 1, (p->nkids - i - 1) * sizeof *p->kids); p->nkids--; break; }
    c->parent = NULL; node_unref(c);
}
void node_clear(struct node *n) { while (n->nkids) { struct node *k = n->kids[--n->nkids]; k->parent = NULL; node_unref(k); } }

/* ---- measuring: text.c (libuxtheme's GDI text) ---- */
static double img_w(cairo_surface_t *s) { return s ? cairo_image_surface_get_width(s) : 0; }
static double img_h(cairo_surface_t *s, int strips) { return s ? cairo_image_surface_get_height(s) / (strips > 0 ? strips : 1) : 0; }
static cairo_surface_t *cur_img(const struct node *n) {
    if (n->kind == N_BUTTON) { cairo_surface_t *s = n->simg[n->state]; if (!s && (n->state == B_HOTCHECKED)) s = n->simg[B_CHECKED]; if (!s && n->state == B_PRESSED) s = n->simg[B_HOT]; return s ? s : n->simg[B_NORMAL] ? n->simg[B_NORMAL] : n->img; }
    return n->img;
}
/* desired size along each axis (auto = content) */
static void desired(struct node *n, double availw, double availh, double *w, double *h) {
    double cw = 0, ch = 0;
    if (n->kind == N_TEXT) {
        if (n->wrap) { double ww = n->w.is_auto ? availw : n->w.pct ? availw * n->w.v / 100.0 : n->w.v; text_measure_w(n, ww - n->ml - n->mr, &cw, &ch); }
        else text_measure(n, &cw, &ch); }
    else if (n->kind == N_IMAGE || n->kind == N_NINEGRID || n->kind == N_BUTTON) { cairo_surface_t *s = cur_img(n); cw = img_w(s); ch = img_h(s, n->strip_count); }
    if (n->kind == N_PANEL || n->kind == N_BUTTON || n->kind == N_TRANSFORM) {
        /* panels: sum of docked children along the stacking axis, max across */
        double sw = 0, sh = 0, mw = 0, mh = 0;
        for (int i = 0; i < n->nkids; i++) { struct node *k = n->kids[i]; if (!k->visible) continue; double kw, kh; desired(k, availw, availh, &kw, &kh);
            enum dock d = n->layout == L_HBOX ? D_LEFT : n->layout == L_VBOX ? D_TOP : k->dock;   /* box children stack like the layout does */
            if (d == D_LEFT || d == D_RIGHT) { sw += kw; if (kh > mh) mh = kh; }
            else if (d == D_TOP || d == D_BOTTOM) { sh += kh; if (kw > mw) mw = kw; }
            else { if (kw > mw) mw = kw; if (kh > mh) mh = kh; } }
        if (n->layout == L_HBOX) { cw = sw + mw; ch = mh > sh ? mh : sh; } else if (n->layout == L_VBOX) { ch = sh + mh; cw = mw > sw ? mw : sw; }
        else { if (sw + mw > cw) cw = sw + mw; if (sh + mh > ch) ch = sh + mh; }
    }
    cw += n->ml + n->mr; ch += n->mt + n->mb;
    *w = n->w.is_auto ? cw : n->w.pct ? availw * n->w.v / 100.0 : n->w.v;
    *h = n->h.is_auto ? ch : n->h.pct ? availh * n->h.v / 100.0 : n->h.v;
}

void scene_desired(struct node *n, double aw, double ah, double *w, double *h) { desired(n, aw, ah, w, h); }

/* ---- layout: WPF DockPanel semantics, children in order; Dock=Fill (or the last undocked child) takes the rest ---- */
void scene_layout(struct node *n, double x, double y, double w, double h) {
    n->cx = x; n->cy = y; n->cw = w; n->ch = h;
    if (!n->visible) return;
    double ix = x + n->ml, iy = y + n->mt, iw = w - n->ml - n->mr, ih = h - n->mt - n->mb;
    if (n->layout == L_CANVAS) {
        for (int i = 0; i < n->nkids; i++) { struct node *k = n->kids[i]; double kw, kh; desired(k, iw, ih, &kw, &kh); scene_layout(k, ix + k->x, iy + k->y, kw, kh); }
        return;
    }
    if (n->layout == L_FILL) { for (int i = 0; i < n->nkids; i++) scene_layout(n->kids[i], ix, iy, iw, ih); return; }
    double rx = ix, ry = iy, rw = iw, rh = ih;
    for (int i = 0; i < n->nkids; i++) {
        struct node *k = n->kids[i]; if (!k->visible) { k->cw = k->ch = 0; continue; }
        double kw, kh; desired(k, rw, rh, &kw, &kh);
        enum dock d = k->dock;
        if (n->layout == L_HBOX) d = D_LEFT; else if (n->layout == L_VBOX) d = D_TOP;
        if (d == D_NONE) d = (i == n->nkids - 1) ? D_FILL : (n->layout == L_DOCK ? D_LEFT : D_FILL);
        if (kw > rw) kw = rw; if (kh > rh) kh = rh;
        switch (d) {
            case D_LEFT:   scene_layout(k, rx, ry, kw, rh); rx += kw; rw -= kw; break;
            case D_RIGHT:  scene_layout(k, rx + rw - kw, ry, kw, rh); rw -= kw; break;
            case D_TOP:    scene_layout(k, rx, ry, rw, kh); ry += kh; rh -= kh; break;
            case D_BOTTOM: scene_layout(k, rx, ry + rh - kh, rw, kh); rh -= kh; break;
            default:       scene_layout(k, rx, ry, rw, rh); break;
        }
    }
}

/* ---- painting ---- */
static void nine(cairo_t *cr, cairo_surface_t *img, int strip_count, int strip_index, double x, double y, double w, double h, int it, int ir, int ib, int il, bool tile, bool flip, int filter) {
    if (!img || w <= 0 || h <= 0) return;
    double iw = cairo_image_surface_get_width(img), ih = cairo_image_surface_get_height(img), sy0 = 0;
    if (strip_count > 1) { ih = ih / strip_count; sy0 = ih * strip_index; }
    if (it + ib > ih) { it = ib = 0; } if (il + ir > iw) { il = ir = 0; }
    cairo_save(cr);
    if (flip) { cairo_translate(cr, x + w, y); cairo_scale(cr, -1, 1); x = 0; y = 0; }
    double sx[4] = {0, il, iw - ir, iw}, sy[4] = {sy0, sy0 + it, sy0 + ih - ib, sy0 + ih};
    double dx[4] = {x, x + il, x + w - ir, x + w}, dy[4] = {y, y + it, y + h - ib, y + h};
    for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++) {
        double sw = sx[i + 1] - sx[i], sh = sy[j + 1] - sy[j], dw = dx[i + 1] - dx[i], dh = dy[j + 1] - dy[j];
        if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) continue;
        cairo_save(cr); cairo_rectangle(cr, dx[i], dy[j], dw, dh); cairo_clip(cr);
        bool stretch = !tile || (i == 1 && j == 1 && false);
        if (tile && (i == 1 || j == 1)) {
            cairo_translate(cr, dx[i], dy[j]); cairo_set_source_surface(cr, img, -sx[i], -sy[j]);
            cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_REPEAT);   /* crude tiling of the middle pieces */
        } else {
            /* each piece samples only its own source rectangle (a sub-surface, PAD at its edges): a 1 px middle row
               stretched over 400 px does not blend in the neighbouring row (the Slate StartPanel strips) */
            cairo_surface_t *piece = cairo_surface_create_for_rectangle(img, sx[i], sy[j], sw, sh);
            cairo_translate(cr, dx[i], dy[j]); cairo_scale(cr, dw / sw, dh / sh);
            /* stretched pieces: the pack's filter (nearest: pixman's 16.16 fixed-point stepping, which reproduces 4074's
               1156 start button exactly, lh4074-181533.png) */
            cairo_set_source_surface(cr, piece, 0, 0); cairo_pattern_set_filter(cairo_get_source(cr), (i == 1 || j == 1) ? cfilter(filter) : CAIRO_FILTER_NEAREST);
            cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
            cairo_surface_destroy(piece);   /* the pattern holds its own reference */
        }
        (void)stretch; cairo_paint(cr); cairo_restore(cr);
    }
    cairo_restore(cr);
}

static void paint_node(cairo_t *cr, struct node *n) {
    if (!n->visible || n->cw <= 0 || n->ch <= 0) return;
    cairo_save(cr);
    if (n->opacity <= 0) { cairo_restore(cr); return; }
    bool group = n->opacity < 1 && !(n->nkids == 0 && (n->kind == N_RECT || n->kind == N_IMAGE));   /* leaves paint with alpha directly */
    if (group) cairo_push_group(cr);
    if (n->clip) { cairo_rectangle(cr, n->cx, n->cy, n->cw, n->ch); cairo_clip(cr); }
    if (n->kind == N_TRANSFORM) {
        cairo_translate(cr, n->cx + n->ox + n->tx, n->cy + n->oy + n->ty); cairo_rotate(cr, n->angle * M_PI / 180.0); cairo_scale(cr, n->sx, n->sy); cairo_translate(cr, -(n->cx + n->ox), -(n->cy + n->oy));
    }
    double ix = n->cx + n->ml, iy = n->cy + n->mt, iw = n->cw - n->ml - n->mr, ih = n->ch - n->mt - n->mb;
    switch (n->kind) {
        case N_RECT: {
            cairo_pattern_t *pat = NULL;
            if (n->ngrad > 1) { pat = n->grad_h ? cairo_pattern_create_linear(ix, 0, ix + iw, 0) : cairo_pattern_create_linear(0, iy, 0, iy + ih);
                for (int g = 0; g < n->ngrad; g++) cairo_pattern_add_color_stop_rgba(pat, n->gpos[g], n->gcol[g][0], n->gcol[g][1], n->gcol[g][2], n->gcol[g][3]); }
            if (n->radius > 0) { double r = n->radius; cairo_new_sub_path(cr); cairo_arc(cr, ix + iw - r, iy + r, r, -M_PI/2, 0); cairo_arc(cr, ix + iw - r, iy + ih - r, r, 0, M_PI/2); cairo_arc(cr, ix + r, iy + ih - r, r, M_PI/2, M_PI); cairo_arc(cr, ix + r, iy + r, r, M_PI, 3*M_PI/2); cairo_close_path(cr); }
            else cairo_rectangle(cr, ix, iy, iw, ih);
            double la = (n->opacity < 1 && n->nkids == 0) ? n->opacity : 1;   /* leaf opacity (no group) */
            if (pat) { cairo_set_source(cr, pat); if (la < 1) { cairo_save(cr); cairo_clip_preserve(cr); cairo_paint_with_alpha(cr, la); cairo_restore(cr); } else cairo_fill_preserve(cr); cairo_pattern_destroy(pat); }
            else if (n->has_fill) { cairo_set_source_rgba(cr, n->fill[0], n->fill[1], n->fill[2], n->fill[3] * la); cairo_fill_preserve(cr); }
            if (n->has_stroke && n->stroke_w > 0) { double w = n->stroke_w; cairo_set_line_width(cr, w); cairo_set_source_rgba(cr, n->stroke[0], n->stroke[1], n->stroke[2], n->stroke[3]);
                if (la < 1) cairo_set_source_rgba(cr, n->stroke[0], n->stroke[1], n->stroke[2], n->stroke[3] * la);
                if (n->radius <= 0) { cairo_new_path(cr); cairo_rectangle(cr, ix + w / 2, iy + w / 2, iw - w, ih - w); }   /* square: stroke inside the rect */
                cairo_stroke(cr); }
            cairo_new_path(cr);
        } break;
        case N_NINEGRID: case N_BUTTON: { cairo_surface_t *s = cur_img(n); if (s) nine(cr, s, n->strip_count, n->strip_index, ix, iy, iw, ih, n->gt, n->gr, n->gb, n->gl, n->tile, n->flip_h, node_filter(n)); } break;
        case N_IMAGE: if (n->img) {
            double w0 = img_w(n->img), h0 = img_h(n->img, 1); cairo_surface_t *src = n->img;
            /* a scaled image is resampled once on the client and cached on the node: painting it with a BEST filter transform
               every frame made the X server resample the 263 px clock layers on each sidebar repaint. Filter: a node's own
               `filter`, else BEST when shrinking (the clock layers, icons) and the pack's filter when magnifying (HackImage:
               nearest in 4074, bilinear in 4051) */
            int sw = (int)lround(iw), sh = (int)lround(ih);
            if ((sw != (int)w0 || sh != (int)h0) && sw > 0 && sh > 0 && sw <= 4096 && sh <= 4096) {
                int f = n->filter ? n->filter : (sw < w0 && sh < h0) ? SF_BEST : scene_filter;
                if (n->scaled_src != n->img || n->scaled_w != sw || n->scaled_h != sh || n->scaled_filter != f) {
                    if (n->scaled) cairo_surface_destroy(n->scaled);
                    n->scaled = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh); cairo_t *c2 = cairo_create(n->scaled);
                    cairo_scale(c2, sw / w0, sh / h0); cairo_set_source_surface(c2, n->img, 0, 0); cairo_pattern_set_filter(cairo_get_source(c2), cfilter(f));
                    cairo_pattern_set_extend(cairo_get_source(c2), CAIRO_EXTEND_PAD); cairo_paint(c2); cairo_destroy(c2);
                    n->scaled_src = n->img; n->scaled_w = sw; n->scaled_h = sh; n->scaled_filter = f;
                }
                src = n->scaled; w0 = sw; h0 = sh;
            }
            cairo_save(cr); cairo_translate(cr, ix, iy); if (n->flip_h) { cairo_translate(cr, iw, 0); cairo_scale(cr, -1, 1); } cairo_scale(cr, iw / w0, ih / h0);
            /* nearest only for a 1:1 copy; a rotated layer (the clock hands under a transform node) is always filtered */
            cairo_matrix_t m; cairo_get_matrix(cr, &m); bool axis = m.xy == 0 && m.yx == 0;
            cairo_set_source_surface(cr, src, 0, 0); cairo_pattern_set_filter(cairo_get_source(cr), axis && src == n->img && iw == w0 && ih == h0 ? CAIRO_FILTER_NEAREST : CAIRO_FILTER_BILINEAR);
            if (n->opacity < 1 && n->nkids == 0) cairo_paint_with_alpha(cr, n->opacity); else cairo_paint(cr); cairo_restore(cr); } break;
        case N_TEXT: text_paint(cr, n); break;
        case N_TRAY: if (scene_custom_paint) scene_custom_paint(n, cr); break;
        case N_CLOCKHAND: cairo_set_source_rgba(cr, n->fill[0], n->fill[1], n->fill[2], n->fill[3]); cairo_rectangle(cr, ix, iy, iw, ih); cairo_fill(cr); break;
        default: break;
    }
    for (int i = 0; i < n->nkids; i++) paint_node(cr, n->kids[i]);
    if (group) { cairo_pop_group_to_source(cr); cairo_paint_with_alpha(cr, n->opacity); }
    cairo_restore(cr);
}
void scene_paint(struct node *root, cairo_t *cr) { paint_node(cr, root); }

struct node *scene_hit(struct node *n, double x, double y) {
    if (!n->visible || x < n->cx || y < n->cy || x >= n->cx + n->cw || y >= n->cy + n->ch) return NULL;
    for (int i = n->nkids - 1; i >= 0; i--) { struct node *h = scene_hit(n->kids[i], x, y); if (h) return h; }
    return (n->hoverable || n->kind == N_BUTTON) ? n : NULL;
}
