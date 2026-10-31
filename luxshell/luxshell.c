/* lhshell: Longhorn taskbar + sidebar runtime. C core (X11 ARGB dock windows, EWMH window model, cairo painting,
   input, timers) scripted by Lua (runtime.lua + theme packs + tiles). See ../SHELL-DESIGN.md section 4. */
#include "luxshell.h"
#include <lhde/data.h>
#include <lhde/theme.h>
#include <lhde/settings.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xfixes.h>
#include <X11/cursorfont.h>
#include <cairo/cairo-xlib.h>
#include <lua5.4/lua.h>
#include <lua5.4/lauxlib.h>
#include <lua5.4/lualib.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fontconfig/fontconfig.h>

Display *dpy; int screen; Window root; Visual *argb; static Colormap cmap; static int depth = 32;
lua_State *L; static bool running = true; static int scr_w, scr_h;
enum { A_NET_WM_WINDOW_TYPE, A_NET_WM_WINDOW_TYPE_DOCK, A_NET_WM_STATE, A_NET_WM_STATE_STICKY, A_NET_WM_STATE_ABOVE, A_NET_WM_STATE_BELOW, A_NET_WM_STATE_HIDDEN,
       A_NET_WM_STATE_SKIP_TASKBAR, A_NET_WM_STATE_DEMANDS_ATTENTION, A_NET_WM_STATE_MAXIMIZED_VERT, A_NET_WM_STRUT, A_NET_WM_STRUT_PARTIAL, A_NET_WM_DESKTOP, A_NET_WM_NAME,
       A_NET_WM_ICON, A_NET_CLIENT_LIST, A_NET_ACTIVE_WINDOW, A_NET_CURRENT_DESKTOP, A_NET_CLOSE_WINDOW, A_WM_STATE, A_UTF8, A_LHWM_SELF_FRAMED, A_MOTIF, A_NET_WM_WINDOW_TYPE_DESKTOP,
       A_NET_WM_WINDOW_TYPE_SPLASH, A_NET_WM_WINDOW_TYPE_TOOLBAR, A_NET_WM_WINDOW_TYPE_MENU, A_NET_WM_WINDOW_TYPE_UTILITY,
       A_NET_WM_STATE_MAXIMIZED_HORZ, A_NET_WM_STATE_FULLSCREEN, A_NET_CLIENT_LIST_STACKING, A_LHWM_MAXIMIZED_MODE, A_NET_SUPPORTING_WM_CHECK, A_NET_WM_ICON_GEOMETRY, A_NET_WM_WINDOW_TYPE_NOTIFICATION, A_NET_WM_WINDOW_TYPE_POPUP_MENU,
       A_LH_THEME, A_LHSHELL_COMMAND, A_LHSHELL_START, A_NET_WORKAREA, A_LHSHELL_CLOCK_VISIBLE, A_LH_SETTINGS_CHANGED, A_LH_STARTMENU_OPEN, A_LAST };
static Atom atom[A_LAST];
static const char *atom_names[A_LAST] = { "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DOCK", "_NET_WM_STATE", "_NET_WM_STATE_STICKY", "_NET_WM_STATE_ABOVE", "_NET_WM_STATE_BELOW", "_NET_WM_STATE_HIDDEN",
       "_NET_WM_STATE_SKIP_TASKBAR", "_NET_WM_STATE_DEMANDS_ATTENTION", "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STRUT", "_NET_WM_STRUT_PARTIAL", "_NET_WM_DESKTOP", "_NET_WM_NAME",
       "_NET_WM_ICON", "_NET_CLIENT_LIST", "_NET_ACTIVE_WINDOW", "_NET_CURRENT_DESKTOP", "_NET_CLOSE_WINDOW", "WM_STATE", "UTF8_STRING", "_LHWM_SELF_FRAMED", "_MOTIF_WM_HINTS", "_NET_WM_WINDOW_TYPE_DESKTOP",
       "_NET_WM_WINDOW_TYPE_SPLASH", "_NET_WM_WINDOW_TYPE_TOOLBAR", "_NET_WM_WINDOW_TYPE_MENU", "_NET_WM_WINDOW_TYPE_UTILITY",
       "_NET_WM_STATE_MAXIMIZED_HORZ", "_NET_WM_STATE_FULLSCREEN", "_NET_CLIENT_LIST_STACKING", "_LHWM_MAXIMIZED_MODE", "_NET_SUPPORTING_WM_CHECK", "_NET_WM_ICON_GEOMETRY", "_NET_WM_WINDOW_TYPE_NOTIFICATION", "_NET_WM_WINDOW_TYPE_POPUP_MENU",
       "_LH_THEME", "_LHSHELL_COMMAND", "_LHSHELL_START", "_NET_WORKAREA", "_LHSHELL_CLOCK_VISIBLE", "_LH_SETTINGS_CHANGED", "_LH_STARTMENU_OPEN" };

/* ---------------- surfaces ---------------- */
static struct surface *surfaces;
static struct surface *surface_from_window(Window w) { for (struct surface *s = surfaces; s; s = s->next) if (s->win == w) return s; return NULL; }

static void icon_geometry_update(struct surface *s); static void icon_geometry_drop(struct surface *s);
long long now_ms(void); static bool debug;   /* LHSHELL_DEBUG: event and paint timestamps (CLOCK_MONOTONIC ms) on stderr */
static void surface_paint(struct surface *s) {
    if (!s->root || !s->mapped) { icon_geometry_drop(s); s->dirty = false; return; }   /* a hidden taskbar (classic / none) publishes no icon geometry */
    long long t0 = debug ? now_ms() : 0;
    scene_layout(s->root, 0, 0, s->w, s->h);
    tray_place(s); icon_geometry_update(s);
    /* double-buffered: the frame is built in an offscreen group (a server-side pixmap, starting transparent) and goes to the
       window in one SOURCE composite. Painting straight into the window (a clear, then some 40 layers) let the compositor show
       half-drawn frames on every repaint: a hover repaints the whole bar, so the clock face flickered while the mouse moved */
    cairo_t *cr = cairo_create(s->xs);
    cairo_push_group_with_content(cr, CAIRO_CONTENT_COLOR_ALPHA);
    scene_paint(s->root, cr);
    cairo_pop_group_to_source(cr); cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE); cairo_paint(cr);
    cairo_destroy(cr); cairo_surface_flush(s->xs); XFlush(dpy); s->dirty = false;
    if (debug) { XSync(dpy, False); fprintf(stderr, "luxshell: [%lld] painted 0x%lx %dx%d in %lld ms\n", now_ms(), s->win, s->w, s->h, now_ms() - t0); }
}
void surface_damage(struct surface *s) { s->dirty = true; }
struct surface *surface_of_node(struct node *n) { while (n->parent) n = n->parent; for (struct surface *s = surfaces; s; s = s->next) if (s->root == n) return s; return NULL; }
static void damage_owner(struct node *n) { struct surface *s = surface_of_node(n); if (s) s->dirty = true; }

/* ---------------- Lua helpers ---------------- */
static int ref_of(lua_State *L, int idx) { lua_pushvalue(L, idx); return luaL_ref(L, LUA_REGISTRYINDEX); }
static void push_ref(lua_State *L, int ref) { lua_rawgeti(L, LUA_REGISTRYINDEX, ref); }
/* Node handles. A Lua node is a table (metatable lh.node: methods, handlers like on_click as fields) whose field _h is a
   full userdata (lh.nodeh) owning one reference on the C node (scene.h), released by its __gc. The handle's user value 1
   is the set of the node's children's tables, so a parent's table keeps its children's tables (and their handlers) alive
   exactly as long as the C tree links them. C finds a node's table through a weak-valued registry table (node* -> table):
   the C side never pins Lua tables, so nothing leaks through closures, and a handle can never point at freed memory —
   a node lives while a handle, a parent or a surface holds it (clear() only detaches). */
struct nodebox { struct node *n; };
static const char nodetab_key = 0;
static void nodetab_push(lua_State *L) { lua_rawgetp(L, LUA_REGISTRYINDEX, &nodetab_key); }
static bool push_node_table(lua_State *L, struct node *n) { nodetab_push(L); lua_rawgetp(L, -1, n); lua_remove(L, -2); if (lua_istable(L, -1)) return true; lua_pop(L, 1); return false; }
static struct node *check_node(lua_State *L, int idx) {
    luaL_checktype(L, idx, LUA_TTABLE); lua_getfield(L, idx, "_h"); struct nodebox *b = luaL_testudata(L, -1, "lh.nodeh"); lua_pop(L, 1);
    if (!b) luaL_error(L, "not a node");
    if (!b->n) luaL_error(L, "dead node");   /* only reachable from a __gc ordering corner; never a freed pointer */
    return b->n;
}
static int l_nodeh_gc(lua_State *L) { struct nodebox *b = lua_touserdata(L, 1); struct node *n = b->n; b->n = NULL; if (n) node_unref(n); return 0; }
/* scene_node_freed: the node is going (its last reference released): the tray stops placing icons in it, and its (already
   unreachable) table entry goes; no Lua call, so it is safe inside a finalizer */
static void node_freed(struct node *n) { tray_forget(n); nodetab_push(L); lua_pushnil(L); lua_rawsetp(L, -2, n); lua_pop(L, 1); }
/* push the children set of the node table at idx (nil when it has none) */
static void push_kidset(lua_State *L, int idx) { lua_getfield(L, idx, "_h"); if (lua_type(L, -1) != LUA_TUSERDATA) return; lua_getiuservalue(L, -1, 1); lua_remove(L, -2); }
/* c (its table at cidx) leaves its parent: drop it from the parent's children set */
static void kid_unlink(lua_State *L, struct node *c, int cidx) {
    if (!c->parent || !push_node_table(L, c->parent)) return;
    push_kidset(L, lua_gettop(L)); if (lua_istable(L, -1)) { lua_pushvalue(L, cidx); lua_pushnil(L); lua_rawset(L, -3); } lua_pop(L, 2);
}
static struct surface *check_surface(lua_State *L, int idx) { luaL_checktype(L, idx, LUA_TTABLE); lua_getfield(L, idx, "_s"); struct surface *s = lua_touserdata(L, -1); lua_pop(L, 1); if (!s) luaL_error(L, "not a surface"); return s; }
static bool parse_color(const char *s, double *c) {
    if (!s || s[0] != '#') return false; unsigned v = 0; size_t n = strlen(s + 1); sscanf(s + 1, "%x", &v);
    if (n == 8) { c[3] = ((v >> 24) & 255) / 255.0; } else c[3] = 1.0;
    c[0] = ((v >> 16) & 255) / 255.0; c[1] = ((v >> 8) & 255) / 255.0; c[2] = (v & 255) / 255.0; return true;
}
static void parse_dim(lua_State *L, int idx, struct dim *d) {
    if (lua_type(L, idx) == LUA_TNUMBER) { d->v = lua_tonumber(L, idx); d->pct = 0; d->is_auto = 0; }
    else if (lua_type(L, idx) == LUA_TSTRING) { const char *s = lua_tostring(L, idx); if (!strcmp(s, "auto")) d->is_auto = 1; else { d->v = atof(s); d->pct = strchr(s, '%') != NULL; d->is_auto = 0; } }
}
static void parse_box(lua_State *L, int idx, double *l, double *t, double *r, double *b) {
    if (lua_type(L, idx) == LUA_TNUMBER) { *l = *t = *r = *b = lua_tonumber(L, idx); return; }
    if (lua_type(L, idx) != LUA_TTABLE) return;
    double v[4] = {0, 0, 0, 0}; int n = (int)lua_rawlen(L, idx);
    for (int i = 0; i < 4 && i < n; i++) { lua_rawgeti(L, idx, i + 1); v[i] = lua_tonumber(L, -1); lua_pop(L, 1); }
    if (n == 1) { *l = *t = *r = *b = v[0]; } else { *l = v[0]; *t = v[1]; *r = v[2]; *b = v[3]; }
}
/* a pack image: "@icon:..." (registered icons), an absolute path, or a path under packs/ in lhde's data directories
   (4074/imageres/1179.png: lhde-import's pack pixels, lhde/data.h) */
static cairo_surface_t *img_from_lua(lua_State *L, int idx) {
    const char *p = lua_tostring(L, idx); if (!p) return NULL;
    if (p[0] == '@') return image_get(p);           /* registered icons */
    if (p[0] == '/') return image_get(p);
    char rel[1100], full[4200]; snprintf(rel, sizeof rel, "packs/%s", p); lhde_data_find(rel, full, sizeof full); return image_get(full);
}
static enum bstate state_of(const char *s) {
    if (!s) return B_NORMAL; if (!strcmp(s, "hot")) return B_HOT; if (!strcmp(s, "pressed")) return B_PRESSED; if (!strcmp(s, "checked")) return B_CHECKED;
    if (!strcmp(s, "hotchecked")) return B_HOTCHECKED; if (!strcmp(s, "flash")) return B_FLASH; if (!strcmp(s, "disabled")) return B_DISABLED; return B_NORMAL;
}
/* image scaling filter names (scene.h enum sfilter): nil / "default" = the pack's (lh.image_filter) */
static int filter_of(const char *s) { return !s ? SF_DEFAULT : !strcmp(s, "nearest") ? SF_NEAREST : !strcmp(s, "bilinear") ? SF_BILINEAR : !strcmp(s, "best") ? SF_BEST : SF_DEFAULT; }
/* apply a property table to a node */
static void node_apply(lua_State *L, struct node *n, int idx) {
    lua_pushnil(L);
    while (lua_next(L, idx) != 0) {
        const char *k = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : ""; int v = lua_gettop(L);
        if (!strcmp(k, "layout")) { const char *s = lua_tostring(L, v); n->layout = !strcmp(s, "fill") ? L_FILL : !strcmp(s, "canvas") ? L_CANVAS : !strcmp(s, "hbox") ? L_HBOX : !strcmp(s, "vbox") ? L_VBOX : L_DOCK; }
        else if (!strcmp(k, "dock")) { const char *s = lua_tostring(L, v); n->dock = !strcmp(s, "left") ? D_LEFT : !strcmp(s, "top") ? D_TOP : !strcmp(s, "right") ? D_RIGHT : !strcmp(s, "bottom") ? D_BOTTOM : !strcmp(s, "fill") ? D_FILL : D_NONE; }
        else if (!strcmp(k, "x")) n->x = lua_tonumber(L, v); else if (!strcmp(k, "y")) n->y = lua_tonumber(L, v);
        else if (!strcmp(k, "w")) parse_dim(L, v, &n->w); else if (!strcmp(k, "h")) parse_dim(L, v, &n->h);
        else if (!strcmp(k, "margin")) parse_box(L, v, &n->ml, &n->mt, &n->mr, &n->mb);
        else if (!strcmp(k, "opacity")) n->opacity = lua_tonumber(L, v); else if (!strcmp(k, "visible")) n->visible = lua_toboolean(L, v);
        else if (!strcmp(k, "clip")) n->clip = lua_toboolean(L, v); else if (!strcmp(k, "flip")) n->flip_h = lua_toboolean(L, v);
        else if (!strcmp(k, "hoverable")) n->hoverable = lua_toboolean(L, v);
        else if (!strcmp(k, "filter")) n->filter = filter_of(lua_tostring(L, v));
        else if (!strcmp(k, "image")) n->img = img_from_lua(L, v);
        else if (!strcmp(k, "images") && lua_istable(L, v)) { lua_pushnil(L); while (lua_next(L, v)) { const char *sk = lua_tostring(L, -2); n->simg[state_of(sk)] = img_from_lua(L, -1); lua_pop(L, 1); } }
        else if (!strcmp(k, "grid")) { double t, r, b, l; if (lua_isnumber(L, v)) { t = r = b = l = lua_tonumber(L, v); } else { parse_box(L, v, &t, &r, &b, &l); } n->gt = t; n->gr = r; n->gb = b; n->gl = l; }
        else if (!strcmp(k, "tile")) n->tile = lua_toboolean(L, v); else if (!strcmp(k, "strips")) n->strip_count = lua_tointeger(L, v); else if (!strcmp(k, "strip")) n->strip_index = lua_tointeger(L, v);
        else if (!strcmp(k, "state")) n->state = state_of(lua_tostring(L, v));
        else if (!strcmp(k, "checked")) n->checked = lua_toboolean(L, v);   /* hover in/out returns to checked, not normal */
        else if (!strcmp(k, "text")) { free(n->text); n->text = strdup(lua_tostring(L, v) ? lua_tostring(L, v) : ""); }
        else if (!strcmp(k, "font")) { free(n->font); n->font = strdup(lua_tostring(L, v)); }
        else if (!strcmp(k, "color")) parse_color(lua_tostring(L, v), n->color); else if (!strcmp(k, "fill")) n->has_fill = parse_color(lua_tostring(L, v), n->fill);
        else if (!strcmp(k, "glow")) n->has_glow = parse_color(lua_tostring(L, v), n->glow);
        else if (!strcmp(k, "glow_size")) n->glow_size = (int)lua_tointeger(L, v); else if (!strcmp(k, "glow_strength")) n->glow_strength = lua_tonumber(L, v);
        else if (!strcmp(k, "shadow") && lua_istable(L, v)) { lua_getfield(L, v, "dx"); n->shadow_dx = lua_tonumber(L, -1); lua_getfield(L, v, "dy"); n->shadow_dy = lua_tonumber(L, -1); lua_getfield(L, v, "color"); n->has_shadow = parse_color(lua_tostring(L, -1), n->shadow); lua_pop(L, 3); }
        else if (!strcmp(k, "ellipsis")) n->ellipsis = lua_toboolean(L, v); else if (!strcmp(k, "wrap")) n->wrap = lua_toboolean(L, v); else if (!strcmp(k, "markup")) n->markup = lua_toboolean(L, v);
        else if (!strcmp(k, "align")) { const char *s = lua_tostring(L, v); n->halign = !strcmp(s, "center") ? 1 : !strcmp(s, "right") ? 2 : 0; }
        else if (!strcmp(k, "valign")) { const char *s = lua_tostring(L, v); n->valign = !strcmp(s, "center") ? 1 : !strcmp(s, "bottom") ? 2 : 0; }
        else if (!strcmp(k, "radius")) n->radius = lua_tonumber(L, v);
        else if (!strcmp(k, "stroke")) n->has_stroke = parse_color(lua_tostring(L, v), n->stroke); else if (!strcmp(k, "stroke_width")) n->stroke_w = lua_tonumber(L, v);
        else if (!strcmp(k, "gradient") && lua_istable(L, v)) {   /* { dir = "h"|"v", {offset, "#rrggbb"}, ... } */
            lua_getfield(L, v, "dir"); n->grad_h = lua_isstring(L, -1) && !strcmp(lua_tostring(L, -1), "h"); lua_pop(L, 1);
            n->ngrad = 0; int cnt = (int)lua_rawlen(L, v);
            for (int g = 1; g <= cnt && n->ngrad < 8; g++) { lua_rawgeti(L, v, g); if (lua_istable(L, -1)) { lua_rawgeti(L, -1, 1); lua_rawgeti(L, -2, 2);
                n->gpos[n->ngrad] = lua_tonumber(L, -2); if (parse_color(lua_tostring(L, -1), n->gcol[n->ngrad])) n->ngrad++; lua_pop(L, 2); } lua_pop(L, 1); } }
        else if (!strcmp(k, "icon_size")) n->icon_size = lua_tointeger(L, v);
        else if (!strcmp(k, "pitch") && lua_istable(L, v)) { lua_rawgeti(L, v, 1); n->pitch_x = lua_tointeger(L, -1); lua_rawgeti(L, v, 2); n->pitch_y = lua_tointeger(L, -1); lua_pop(L, 2); }
        else if (!strcmp(k, "icon_geometry_for")) n->wmicon = (unsigned long)lua_tointeger(L, v);
        else if (!strcmp(k, "cursor")) { const char *c = lua_tostring(L, v); n->cursor = !c ? 0 : !strcmp(c, "sizewe") ? 2 : !strcmp(c, "sizens") ? 3 : !strcmp(c, "hand") ? 4 : !strcmp(c, "arrow") ? 1 : 0; }
        else if (!strcmp(k, "angle")) n->angle = lua_tonumber(L, v);
        else if (!strcmp(k, "origin") && lua_istable(L, v)) { lua_rawgeti(L, v, 1); n->ox = lua_tonumber(L, -1); lua_rawgeti(L, v, 2); n->oy = lua_tonumber(L, -1); lua_pop(L, 2); }
        else if (!strcmp(k, "translate") && lua_istable(L, v)) { lua_rawgeti(L, v, 1); n->tx = lua_tonumber(L, -1); lua_rawgeti(L, v, 2); n->ty = lua_tonumber(L, -1); lua_pop(L, 2); }
        else if (!strcmp(k, "scale")) { if (lua_istable(L, v)) { lua_rawgeti(L, v, 1); n->sx = lua_tonumber(L, -1); lua_rawgeti(L, v, 2); n->sy = lua_tonumber(L, -1); lua_pop(L, 2); } else n->sx = n->sy = lua_tonumber(L, v); }
        lua_pop(L, 1);
    }
    if (n->kind == N_BUTTON && !n->hoverable) n->hoverable = true;
    damage_owner(n);
}
static int l_node(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE); lua_getfield(L, 1, "kind"); const char *k = lua_tostring(L, -1); lua_pop(L, 1);
    enum kind kind = N_PANEL; if (k) { if (!strcmp(k, "ninegrid")) kind = N_NINEGRID; else if (!strcmp(k, "image")) kind = N_IMAGE; else if (!strcmp(k, "text")) kind = N_TEXT; else if (!strcmp(k, "rect")) kind = N_RECT; else if (!strcmp(k, "button")) kind = N_BUTTON; else if (!strcmp(k, "transform")) kind = N_TRANSFORM; else if (!strcmp(k, "hand")) kind = N_CLOCKHAND; else if (!strcmp(k, "tray")) kind = N_TRAY; }
    struct nodebox *b = lua_newuserdatauv(L, sizeof *b, 1); b->n = NULL; luaL_setmetatable(L, "lh.nodeh");
    lua_newtable(L); lua_setiuservalue(L, -2, 1);                     /* the children set */
    struct node *n = node_new(kind); b->n = n;                        /* the handle owns the creator's reference */
    node_apply(L, n, 1); lua_setfield(L, 1, "_h"); luaL_getmetatable(L, "lh.node"); lua_setmetatable(L, 1);
    nodetab_push(L); lua_pushvalue(L, 1); lua_rawsetp(L, -2, n); lua_pop(L, 1);
    lua_pushvalue(L, 1); return 1;
}
static int l_node_add(lua_State *L) {
    struct node *p = check_node(L, 1), *c = check_node(L, 2);
    for (struct node *x = p; x; x = x->parent) if (x == c) return luaL_error(L, "node:add: a node cannot be added under itself");
    damage_owner(c); kid_unlink(L, c, 2); node_add(p, c);
    push_kidset(L, 1); lua_pushvalue(L, 2); lua_pushboolean(L, 1); lua_rawset(L, -3); lua_pop(L, 1);
    damage_owner(p); lua_pushvalue(L, 2); return 1;
}
static int l_node_remove(lua_State *L) { struct node *n = check_node(L, 1); damage_owner(n); kid_unlink(L, n, 1); node_remove(n); return 0; }
/* a surface's root / hot / pressed slot holds a reference on its node */
static void set_node_slot(struct node **slot, struct node *n) { if (n) node_ref(n); struct node *old = *slot; *slot = n; if (old) node_unref(old); }
/* a surface's hot/pressed node must not outlive a clear() of its parent (task buttons are rebuilt on every window event) */
static void forget_nodes_under(struct node *n) {
    for (struct surface *s = surfaces; s; s = s->next) {
        for (struct node *x = s->hot; x; x = x->parent) if (x->parent == n) { set_node_slot(&s->hot, NULL); break; }
        for (struct node *x = s->pressed; x; x = x->parent) if (x->parent == n) { set_node_slot(&s->pressed, NULL); break; }
    }
}
/* node:clear(): detach every child. A child (or a descendant) still held by a Lua handle stays valid, detached; the rest is freed */
static int l_node_clear(lua_State *L) {
    struct node *n = check_node(L, 1); forget_nodes_under(n); for (int i = 0; i < n->nkids; i++) tray_forget(n->kids[i]);
    lua_getfield(L, 1, "_h"); lua_newtable(L); lua_setiuservalue(L, -2, 1); lua_pop(L, 1);   /* an empty children set */
    node_clear(n); damage_owner(n); return 0;
}
static int l_node_set(lua_State *L) { struct node *n = check_node(L, 1); luaL_checktype(L, 2, LUA_TTABLE); node_apply(L, n, 2); lua_pushvalue(L, 1); return 1; }
static int l_node_bounds(lua_State *L) { struct node *n = check_node(L, 1); lua_pushnumber(L, n->cx); lua_pushnumber(L, n->cy); lua_pushnumber(L, n->cw); lua_pushnumber(L, n->ch); return 4; }
static int l_node_desired(lua_State *L) { struct node *n = check_node(L, 1); double w, h; scene_desired(n, luaL_optnumber(L, 2, scr_w), luaL_optnumber(L, 3, scr_h), &w, &h); lua_pushnumber(L, w); lua_pushnumber(L, h); return 2; }
static int l_node_measure(lua_State *L) { struct node *n = check_node(L, 1); double w = 0, h = 0; if (n->kind == N_TEXT) text_measure_w(n, luaL_optnumber(L, 2, -1), &w, &h); lua_pushnumber(L, w); lua_pushnumber(L, h); return 2; }   /* node:measure([wrap_width]) */

/* ---- surfaces: lh.surface{ x,y,w,h, dock, strut, layer, name, type } ; type = "desktop": _NET_WM_WINDOW_TYPE_DESKTOP,
   sticky + below, no struts (the desktop layer under all windows) ---- */
static void set_strut(struct surface *s, const char *dock, long strut) {
    long partial[12] = {0}; long simple[4] = {0};
    if (!dock || strut <= 0) { XDeleteProperty(dpy, s->win, atom[A_NET_WM_STRUT]); XDeleteProperty(dpy, s->win, atom[A_NET_WM_STRUT_PARTIAL]); return; }
    if (!strcmp(dock, "left")) { partial[0] = strut; partial[4] = s->y; partial[5] = s->y + s->h - 1; simple[0] = strut; }
    else if (!strcmp(dock, "right")) { partial[1] = strut; partial[6] = s->y; partial[7] = s->y + s->h - 1; simple[1] = strut; }
    else if (!strcmp(dock, "top")) { partial[2] = strut; partial[8] = s->x; partial[9] = s->x + s->w - 1; simple[2] = strut; }
    else { partial[3] = strut; partial[10] = s->x; partial[11] = s->x + s->w - 1; simple[3] = strut; }
    XChangeProperty(dpy, s->win, atom[A_NET_WM_STRUT_PARTIAL], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)partial, 12);
    XChangeProperty(dpy, s->win, atom[A_NET_WM_STRUT], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)simple, 4);
}
static void apply_round(struct surface *s);
static int l_surface(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    struct surface *s = calloc(1, sizeof *s);
    lua_getfield(L, 1, "x"); s->x = lua_tointeger(L, -1); lua_getfield(L, 1, "y"); s->y = lua_tointeger(L, -1); lua_getfield(L, 1, "w"); s->w = lua_tointeger(L, -1); lua_getfield(L, 1, "h"); s->h = lua_tointeger(L, -1); lua_pop(L, 4);
    if (s->w < 1) s->w = 1; if (s->h < 1) s->h = 1;
    lua_getfield(L, 1, "popup"); bool popup = lua_toboolean(L, -1); lua_pop(L, 1);
    /* opaque = true: a 24-bit window (the desktop layer, mapped only while black): the compositor then knows it is solid,
       skips blurring behind it and everything it covers (picom treats every ARGB window as translucent) */
    lua_getfield(L, 1, "opaque"); bool opaque = lua_toboolean(L, -1); lua_pop(L, 1);
    Visual *vis = opaque ? DefaultVisual(dpy, screen) : argb; int dep = opaque ? DefaultDepth(dpy, screen) : depth;
    XSetWindowAttributes a = { .colormap = opaque ? DefaultColormap(dpy, screen) : cmap, .border_pixel = 0, .background_pixel = 0, .override_redirect = popup,
        .event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | LeaveWindowMask | EnterWindowMask | StructureNotifyMask | KeyPressMask | KeyReleaseMask };
    s->win = XCreateWindow(dpy, root, s->x, s->y, s->w, s->h, 0, dep, InputOutput, vis, CWColormap | CWBorderPixel | CWBackPixel | CWEventMask | CWOverrideRedirect, &a);
    s->xs = cairo_xlib_surface_create(dpy, s->win, vis, s->w, s->h);
    lua_getfield(L, 1, "name"); const char *name = lua_tostring(L, -1); if (!name) name = "lhshell";
    XStoreName(dpy, s->win, name); XClassHint ch = { (char *)name, "lhshell" }; XSetClassHint(dpy, s->win, &ch); lua_pop(L, 1);
    long one = 1; XChangeProperty(dpy, s->win, atom[A_LHWM_SELF_FRAMED], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&one, 1);
    long mwm[5] = { 2, 0, 0, 0, 0 }; XChangeProperty(dpy, s->win, atom[A_MOTIF], atom[A_MOTIF], 32, PropModeReplace, (unsigned char *)mwm, 5);
    if (popup) {   /* override-redirect: toasts ("notification") and flyouts ("popup_menu") still carry their EWMH type for picom rules */
        lua_getfield(L, 1, "type"); const char *type = lua_tostring(L, -1);
        if (type) { Atom t = !strcmp(type, "notification") ? atom[A_NET_WM_WINDOW_TYPE_NOTIFICATION] : !strcmp(type, "menu") ? atom[A_NET_WM_WINDOW_TYPE_MENU] : atom[A_NET_WM_WINDOW_TYPE_POPUP_MENU]; XChangeProperty(dpy, s->win, atom[A_NET_WM_WINDOW_TYPE], XA_ATOM, 32, PropModeReplace, (unsigned char *)&t, 1); }
        lua_pop(L, 1);
    }
    if (!popup) {
        lua_getfield(L, 1, "type"); const char *type = lua_tostring(L, -1); bool desk = type && !strcmp(type, "desktop"); lua_pop(L, 1);
        XChangeProperty(dpy, s->win, atom[A_NET_WM_WINDOW_TYPE], XA_ATOM, 32, PropModeReplace, (unsigned char *)&atom[desk ? A_NET_WM_WINDOW_TYPE_DESKTOP : A_NET_WM_WINDOW_TYPE_DOCK], 1);
        Atom st[3]; int ns = 0; st[ns++] = atom[A_NET_WM_STATE_STICKY];
        lua_getfield(L, 1, "layer"); const char *layer = lua_tostring(L, -1); if (layer && !strcmp(layer, "above")) st[ns++] = atom[A_NET_WM_STATE_ABOVE]; if ((layer && !strcmp(layer, "below")) || desk) st[ns++] = atom[A_NET_WM_STATE_BELOW]; lua_pop(L, 1);
        XChangeProperty(dpy, s->win, atom[A_NET_WM_STATE], XA_ATOM, 32, PropModeReplace, (unsigned char *)st, ns);
        long all = 0xFFFFFFFF; XChangeProperty(dpy, s->win, atom[A_NET_WM_DESKTOP], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&all, 1);
        lua_getfield(L, 1, "dock"); const char *dock = lua_tostring(L, -1); lua_getfield(L, 1, "strut"); long strut = lua_tointeger(L, -1); set_strut(s, dock, strut); lua_pop(L, 2);
    }
    s->next = surfaces; surfaces = s;
    lua_pushlightuserdata(L, s); lua_setfield(L, 1, "_s"); luaL_getmetatable(L, "lh.surface"); lua_setmetatable(L, 1); s->lua_ref = ref_of(L, 1);
    lua_pushvalue(L, 1); return 1;
}
static int l_surface_root(lua_State *L) {
    struct surface *s = check_surface(L, 1); struct node *n = check_node(L, 2); set_node_slot(&s->root, n); n->owner = s;
    lua_pushvalue(L, 2); lua_setfield(L, 1, "_lh_root");          /* the (pinned) surface table keeps the root's table, and so the tree's handlers */
    surface_damage(s); return 0;
}
static int l_surface_show(lua_State *L) { struct surface *s = check_surface(L, 1); XMapWindow(dpy, s->win); s->mapped = true; surface_damage(s); return 0; }
static int l_surface_hide(lua_State *L) { struct surface *s = check_surface(L, 1); XUnmapWindow(dpy, s->win); s->mapped = false; return 0; }
static int l_surface_move(lua_State *L) { struct surface *s = check_surface(L, 1); s->x = luaL_checkinteger(L, 2); s->y = luaL_checkinteger(L, 3); s->w = luaL_checkinteger(L, 4); s->h = luaL_checkinteger(L, 5); if (s->w < 1) s->w = 1; if (s->h < 1) s->h = 1;
    XMoveResizeWindow(dpy, s->win, s->x, s->y, s->w, s->h); cairo_xlib_surface_set_size(s->xs, s->w, s->h); apply_round(s); lua_getfield(L, 1, "dock"); lua_getfield(L, 1, "strut"); set_strut(s, lua_tostring(L, -2), lua_tointeger(L, -1)); lua_pop(L, 2); surface_damage(s); return 0; }
/* round-rect bounding shape (the toast's SetWindowRgn radius 9, 2.4.5): picom then blurs and shadows only inside it */
static void apply_round(struct surface *s) {
    if (s->radius <= 0) return;
    int r = s->radius, n = 0; XRectangle *rects = calloc(2 * r + 1, sizeof *rects);
    for (int y = 0; y < r; y++) {   /* inset of row y from a circle of radius r */
        double dy = r - y - 0.5; int in = (int)ceil(r - sqrt((double)r * r - dy * dy));
        rects[n++] = (XRectangle){ in, y, s->w - 2 * in, 1 }; rects[n++] = (XRectangle){ in, s->h - 1 - y, s->w - 2 * in, 1 };
    }
    rects[n++] = (XRectangle){ 0, r, s->w, s->h - 2 * r > 0 ? s->h - 2 * r : 0 };
    XShapeCombineRectangles(dpy, s->win, ShapeBounding, 0, 0, rects, n, ShapeSet, Unsorted); free(rects);
}
static int l_surface_round(lua_State *L) { struct surface *s = check_surface(L, 1); s->radius = luaL_checkinteger(L, 2); apply_round(s); return 0; }
static int l_surface_raise(lua_State *L) { struct surface *s = check_surface(L, 1); XRaiseWindow(dpy, s->win); return 0; }
/* surface:stackpos(): index among the root's children, 0 = bottom (-1 when not found); XQueryTree lists bottom to top */
static int l_surface_stackpos(lua_State *L) {
    struct surface *s = check_surface(L, 1); Window r, p, *kids = NULL; unsigned int n = 0; int pos = -1;
    if (XQueryTree(dpy, DefaultRootWindow(dpy), &r, &p, &kids, &n)) { for (unsigned int i = 0; i < n; i++) if (kids[i] == s->win) pos = (int)i; if (kids) XFree(kids); }
    lua_pushinteger(L, pos); return 1;
}
/* surface:set_layer("above" | "below" | nil): rewrite _NET_WM_STATE of a mapped dock (sticky + ABOVE or BELOW) in place; lhwm
   re-reads the state of a tracked dock on PropertyNotify (dock_property) and restacks the docks at once */
static int l_surface_layer(lua_State *L) { struct surface *s = check_surface(L, 1); const char *layer = luaL_optstring(L, 2, "");
    Atom st[2]; int ns = 0; st[ns++] = atom[A_NET_WM_STATE_STICKY];
    if (!strcmp(layer, "above")) st[ns++] = atom[A_NET_WM_STATE_ABOVE]; else if (!strcmp(layer, "below")) st[ns++] = atom[A_NET_WM_STATE_BELOW];
    XChangeProperty(dpy, s->win, atom[A_NET_WM_STATE], XA_ATOM, 32, PropModeReplace, (unsigned char *)st, ns); return 0; }
static int l_surface_lower(lua_State *L) { struct surface *s = check_surface(L, 1); XLowerWindow(dpy, s->win); return 0; }
static int l_surface_damage(lua_State *L) { surface_damage(check_surface(L, 1)); return 0; }
static int l_surface_id(lua_State *L) { lua_pushinteger(L, (lua_Integer)check_surface(L, 1)->win); return 1; }
/* surface:background("#aarrggbb" | nil): the X window background, painted by the server itself on map/expose before our
   first cairo frame (the desktop layer switches to black atomically with its XMapWindow) */
static int l_surface_background(lua_State *L) { struct surface *s = check_surface(L, 1); double c[4] = {0, 0, 0, 0}; if (lua_isstring(L, 2)) parse_color(lua_tostring(L, 2), c);
    unsigned long px = ((unsigned long)(c[3] * 255) << 24) | ((unsigned long)(c[0] * c[3] * 255) << 16) | ((unsigned long)(c[1] * c[3] * 255) << 8) | (unsigned long)(c[2] * c[3] * 255);
    XSetWindowBackground(dpy, s->win, px); return 0; }
/* surface:grab([keyboard]) / :ungrab(): pointer grab with owner events; a press outside every lhshell surface calls
   surface.on_outside. keyboard = true also grabs the keyboard (menus: every key goes to this surface's on_key, wherever
   the focus is); returns pointer_ok, keyboard_ok. Without a keyboard grab, keys reach a surface only when it has the focus. */
static struct surface *kb_surface;   /* the surface holding the keyboard grab */
static int l_surface_grab(lua_State *L) { struct surface *s = check_surface(L, 1); bool kb = lua_toboolean(L, 2);
    int r = XGrabPointer(dpy, s->win, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask | EnterWindowMask | LeaveWindowMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    s->grabbed = r == GrabSuccess; int k = -1;
    if (kb) { k = XGrabKeyboard(dpy, s->win, False, GrabModeAsync, GrabModeAsync, CurrentTime); if (k == GrabSuccess) kb_surface = s; }
    if (debug) fprintf(stderr, "luxshell: grab 0x%lx: pointer %d keyboard %d\n", s->win, r, k);
    lua_pushboolean(L, s->grabbed); lua_pushboolean(L, kb && k == GrabSuccess); return 2; }
static int l_surface_ungrab(lua_State *L) { struct surface *s = check_surface(L, 1); if (s->grabbed) XUngrabPointer(dpy, CurrentTime); s->grabbed = false;
    if (kb_surface == s) { XUngrabKeyboard(dpy, CurrentTime); kb_surface = NULL; } XFlush(dpy); return 0; }
/* surface:icon_offset(dx, dy): _NET_WM_ICON_GEOMETRY of the nodes on this surface is published shifted by dx,dy (the
   autohidden taskbar reports where its buttons are when shown, not the off-screen slot) */
static int l_surface_icon_offset(lua_State *L) { struct surface *s = check_surface(L, 1); s->geo_dx = luaL_checkinteger(L, 2); s->geo_dy = luaL_checkinteger(L, 3); surface_damage(s); return 0; }

/* ---- timers ---- */
struct timer { int id; long long due; long long interval; int ref; bool dead; };
static struct timer timers[256]; static int ntimers = 0, next_timer = 1;
long long now_ms(void);
long long now_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000; }
static int l_timer(lua_State *L) { long long ms = luaL_checkinteger(L, 2); luaL_checktype(L, 1, LUA_TFUNCTION); bool rep = lua_toboolean(L, 3);
    if (ntimers >= 256) return luaL_error(L, "too many timers");
    struct timer *t = &timers[ntimers++]; t->id = next_timer++; t->due = now_ms() + ms; t->interval = rep ? ms : 0; t->ref = ref_of(L, 1); t->dead = false; lua_pushinteger(L, t->id); return 1; }
static int l_cancel(lua_State *L) { int id = luaL_checkinteger(L, 1); for (int i = 0; i < ntimers; i++) if (timers[i].id == id) timers[i].dead = true; return 0; }
static void run_timers(void) {
    long long t = now_ms();
    for (int i = 0; i < ntimers; i++) {
        struct timer *tm = &timers[i]; if (tm->dead || tm->due > t) continue;
        push_ref(L, tm->ref); if (lua_pcall(L, 0, 0, 0)) { fprintf(stderr, "luxshell: timer: %s\n", lua_tostring(L, -1)); lua_pop(L, 1); }
        if (tm->interval) tm->due = t + tm->interval; else tm->dead = true;
    }
    int j = 0; for (int i = 0; i < ntimers; i++) { if (timers[i].dead) luaL_unref(L, LUA_REGISTRYINDEX, timers[i].ref); else timers[j++] = timers[i]; } ntimers = j;
}
static long long next_deadline(void) { long long d = -1; for (int i = 0; i < ntimers; i++) if (!timers[i].dead && (d < 0 || timers[i].due < d)) d = timers[i].due; return d; }

/* ---- EWMH window model ---- */
/* stacking: position in _NET_CLIENT_LIST_STACKING (0 = bottom, -1 = not listed); the task list keeps _NET_CLIENT_LIST (creation) order */
struct client { Window w; char title[256]; bool active, hidden, skip, attention, maximized, fullscreen; long desktop; char cls[64]; bool has_icon; int stacking; };
static struct client clients[512]; static int nclients = 0; static Window active_win;
static struct client *client_find(Window w);
static unsigned char *get_prop(Window w, Atom a, Atom type, unsigned long *n, int *fmt) {
    Atom ty; unsigned long after; unsigned char *d = NULL; *n = 0;
    if (XGetWindowProperty(dpy, w, a, 0, 1L << 20, False, type, &ty, fmt, n, &after, &d) != Success) return NULL;
    if (!d || *n == 0) { if (d) XFree(d); return NULL; } return d;
}
/* an icon scaled into want x want (aspect kept, centred) with CAIRO_FILTER_BEST; takes s */
static cairo_surface_t *icon_fit(cairo_surface_t *s, int want) {
    int iw = cairo_image_surface_get_width(s), ih = cairo_image_surface_get_height(s);
    if (iw == want && ih == want) return s;
    cairo_surface_t *out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, want, want); cairo_t *cr = cairo_create(out);
    double k = (double)want / (iw > ih ? iw : ih); cairo_translate(cr, (want - iw * k) / 2, (want - ih * k) / 2); cairo_scale(cr, k, k);
    cairo_set_source_surface(cr, s, 0, 0); cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST); cairo_paint(cr);
    cairo_destroy(cr); cairo_surface_destroy(s); return out;
}
/* _NET_WM_ICON: the smallest image >= want (else the largest), scaled to want */
static cairo_surface_t *icon_from_netwm(const unsigned long *data, unsigned long n, int want) {
    unsigned long i = 0; const unsigned long *best = NULL; unsigned long bw = 0, bh = 0;
    while (i + 2 <= n) { unsigned long w = data[i], h = data[i + 1]; if (!w || !h || w > 1024 || h > 1024 || i + 2 + w * h > n) break;
        unsigned long sz = w > h ? w : h, bsz = bw > bh ? bw : bh;
        if (!best || (sz >= (unsigned long)want ? (bsz < (unsigned long)want || sz < bsz) : (bsz < (unsigned long)want && sz > bsz))) { best = data + i + 2; bw = w; bh = h; }
        i += 2 + w * h; }
    if (!best) return NULL;
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bw, bh); unsigned char *px = cairo_image_surface_get_data(s); int stride = cairo_image_surface_get_stride(s);
    for (unsigned long y = 0; y < bh; y++) for (unsigned long x = 0; x < bw; x++) { unsigned long v = best[y * bw + x]; unsigned a = (v >> 24) & 255, r = (v >> 16) & 255, g = (v >> 8) & 255, b = v & 255; *(unsigned *)(px + y * stride + x * 4) = (a << 24) | ((r * a / 255) << 16) | ((g * a / 255) << 8) | (b * a / 255); }
    cairo_surface_mark_dirty(s); return icon_fit(s, want);
}
/* WM_HINTS icon_pixmap (+ icon_mask) → premultiplied ARGB, scaled to want x want: the fallback for clients without _NET_WM_ICON
   (xterm, xclock, urxvt). Bitmaps (depth 1) draw black on white, like the classic X icon box. */
static int mask_shift(unsigned long m) { int s = 0; if (!m) return 0; while (!(m & 1)) { m >>= 1; s++; } return s; }
static unsigned chan(unsigned long p, unsigned long m) { if (!m) return 0; unsigned long v = (p & m) >> mask_shift(m), max = m >> mask_shift(m); return (unsigned)(v * 255 / (max ? max : 1)); }
static cairo_surface_t *icon_from_wmhints(Window w, int want) {
    XWMHints *h = XGetWMHints(dpy, w); if (!h) return NULL;
    cairo_surface_t *out = NULL;
    Window r; int gx, gy; unsigned pw = 0, ph = 0, bw, pd = 0, mw = 0, mh = 0, md = 0;
    if ((h->flags & IconPixmapHint) && h->icon_pixmap && XGetGeometry(dpy, h->icon_pixmap, &r, &gx, &gy, &pw, &ph, &bw, &pd) && pw && ph && pw <= 512 && ph <= 512) {
        XImage *img = XGetImage(dpy, h->icon_pixmap, 0, 0, pw, ph, AllPlanes, ZPixmap), *mask = NULL;
        if ((h->flags & IconMaskHint) && h->icon_mask && XGetGeometry(dpy, h->icon_mask, &r, &gx, &gy, &mw, &mh, &bw, &md) && mw >= pw && mh >= ph)
            mask = XGetImage(dpy, h->icon_mask, 0, 0, pw, ph, AllPlanes, ZPixmap);
        if (img) {
            Visual *v = DefaultVisual(dpy, screen);
            unsigned long rm = img->red_mask ? img->red_mask : v->red_mask, gm = img->green_mask ? img->green_mask : v->green_mask, bm = img->blue_mask ? img->blue_mask : v->blue_mask;
            cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph); unsigned char *px = cairo_image_surface_get_data(s); int stride = cairo_image_surface_get_stride(s);
            for (unsigned y = 0; y < ph; y++) for (unsigned x = 0; x < pw; x++) {
                unsigned long p = XGetPixel(img, x, y); unsigned rr, gg, bb;
                if (pd == 1) rr = gg = bb = p ? 0 : 255; else { rr = chan(p, rm); gg = chan(p, gm); bb = chan(p, bm); }
                unsigned a = mask ? (XGetPixel(mask, x, y) ? 255 : 0) : 255;
                *(unsigned *)(px + y * stride + x * 4) = (a << 24) | ((rr * a / 255) << 16) | ((gg * a / 255) << 8) | (bb * a / 255);
            }
            cairo_surface_mark_dirty(s); out = icon_fit(s, want);   /* to the task-button size, keeping the aspect */
            XDestroyImage(img);
        }
        if (mask) XDestroyImage(mask);
    }
    XFree(h); return out;
}
/* icons are registered in the image cache under "@icon:<window>" (task buttons) and "@notify:<id>" (image-data hints) so
   Lua can use them as image paths */
struct iconent { char key[64]; cairo_surface_t *s; struct iconent *next; }; static struct iconent *icons;
void icon_register(const char *key, cairo_surface_t *s) {
    for (struct iconent *e = icons; e; e = e->next) if (!strcmp(e->key, key)) { if (e->s) cairo_surface_destroy(e->s); e->s = s; return; }
    struct iconent *e = calloc(1, sizeof *e); snprintf(e->key, sizeof e->key, "%s", key); e->s = s; e->next = icons; icons = e;
}
static void client_update_icon(struct client *c) {
    unsigned long n; int fmt; unsigned char *d = get_prop(c->w, atom[A_NET_WM_ICON], XA_CARDINAL, &n, &fmt);
    cairo_surface_t *s = (d && fmt == 32) ? icon_from_netwm((unsigned long *)d, n, 16) : NULL; if (d) XFree(d);
    if (!s) s = icon_from_wmhints(c->w, 16);   /* then WM_HINTS; the theme's default icon is the Lua side's fallback */
    c->has_icon = s != NULL;
    char key[64]; snprintf(key, sizeof key, "@icon:%lu", c->w);
    icon_register(key, s);
}
cairo_surface_t *icon_lookup(const char *key) { for (struct iconent *e = icons; e; e = e->next) if (!strcmp(e->key, key)) return e->s; return NULL; }
static void client_update_title(struct client *c) {
    unsigned long n; int fmt; unsigned char *d = get_prop(c->w, atom[A_NET_WM_NAME], atom[A_UTF8], &n, &fmt);
    if (!d) d = get_prop(c->w, XA_WM_NAME, AnyPropertyType, &n, &fmt);
    if (d) { strncpy(c->title, (char *)d, sizeof c->title - 1); c->title[sizeof c->title - 1] = 0; XFree(d); } else c->title[0] = 0;
}
static void client_update_state(struct client *c) {
    unsigned long n; int fmt; c->hidden = c->skip = c->attention = c->maximized = c->fullscreen = false; bool mv = false, mh = false;
    unsigned char *d = get_prop(c->w, atom[A_NET_WM_STATE], XA_ATOM, &n, &fmt);
    if (d) { Atom *a = (Atom *)d; for (unsigned long i = 0; i < n; i++) { if (a[i] == atom[A_NET_WM_STATE_HIDDEN]) c->hidden = true; if (a[i] == atom[A_NET_WM_STATE_SKIP_TASKBAR]) c->skip = true; if (a[i] == atom[A_NET_WM_STATE_DEMANDS_ATTENTION]) c->attention = true;
        if (a[i] == atom[A_NET_WM_STATE_MAXIMIZED_VERT]) mv = true; if (a[i] == atom[A_NET_WM_STATE_MAXIMIZED_HORZ]) mh = true; if (a[i] == atom[A_NET_WM_STATE_FULLSCREEN]) c->fullscreen = true; } XFree(d); }
    c->maximized = mv && mh;
    d = get_prop(c->w, atom[A_WM_STATE], atom[A_WM_STATE], &n, &fmt); if (d) { if (((long *)d)[0] == IconicState) c->hidden = true; XFree(d); }
    d = get_prop(c->w, atom[A_NET_WM_WINDOW_TYPE], XA_ATOM, &n, &fmt);
    if (d) { Atom t = ((Atom *)d)[0]; if (t == atom[A_NET_WM_WINDOW_TYPE_DOCK] || t == atom[A_NET_WM_WINDOW_TYPE_DESKTOP] || t == atom[A_NET_WM_WINDOW_TYPE_SPLASH] || t == atom[A_NET_WM_WINDOW_TYPE_TOOLBAR] || t == atom[A_NET_WM_WINDOW_TYPE_MENU] || t == atom[A_NET_WM_WINDOW_TYPE_UTILITY]) c->skip = true; XFree(d); }
    d = get_prop(c->w, atom[A_NET_WM_DESKTOP], XA_CARDINAL, &n, &fmt); c->desktop = d ? ((long *)d)[0] : 0; if (d) XFree(d);
    { Window tf = None; if (XGetTransientForHint(dpy, c->w, &tf) && tf != None) c->skip = true; }   /* owned windows get no task button (3.1) */
    if (surface_from_window(c->w)) c->skip = true;
}
static struct client *client_find(Window w) { for (int i = 0; i < nclients; i++) if (clients[i].w == w) return &clients[i]; return NULL; }

/* _NET_CLIENT_LIST_STACKING (bottom to top) → client.stacking; returns true when any index changed */
static bool stacking_refresh(void) {
    unsigned long n = 0; int fmt; unsigned char *d = get_prop(root, atom[A_NET_CLIENT_LIST_STACKING], XA_WINDOW, &n, &fmt); bool changed = false;
    for (int i = 0; i < nclients; i++) { int k = -1; for (unsigned long j = 0; d && j < n; j++) if (((Window *)d)[j] == clients[i].w) { k = (int)j; break; } if (clients[i].stacking != k) { clients[i].stacking = k; changed = true; } }
    if (d) XFree(d); return changed;
}
static void clients_refresh(void) {
    unsigned long n; int fmt; unsigned char *d = get_prop(root, atom[A_NET_CLIENT_LIST], XA_WINDOW, &n, &fmt);
    struct client fresh[512]; int nf = 0;
    for (unsigned long i = 0; d && i < n && nf < 512; i++) {
        Window w = ((Window *)d)[i]; struct client *old = client_find(w);
        if (old) fresh[nf++] = *old; else {
            struct client c = { .w = w, .stacking = -1 }; XSelectInput(dpy, w, PropertyChangeMask); client_update_title(&c); client_update_icon(&c); client_update_state(&c);
            XClassHint ch; if (XGetClassHint(dpy, w, &ch)) { strncpy(c.cls, ch.res_class ? ch.res_class : "", sizeof c.cls - 1); if (ch.res_name) XFree(ch.res_name); if (ch.res_class) XFree(ch.res_class); }
            fresh[nf++] = c;
        }
    }
    if (d) XFree(d);
    memcpy(clients, fresh, nf * sizeof *clients); nclients = nf;
    unsigned char *a = get_prop(root, atom[A_NET_ACTIVE_WINDOW], XA_WINDOW, &n, &fmt); active_win = a ? ((Window *)a)[0] : None; if (a) XFree(a);
    for (int i = 0; i < nclients; i++) clients[i].active = clients[i].w == active_win;
    stacking_refresh();
    emit("windows", 0);
}
static int l_windows(lua_State *L) {
    lua_newtable(L); int k = 1;
    for (int i = 0; i < nclients; i++) { struct client *c = &clients[i]; if (c->skip) continue;
        lua_newtable(L); lua_pushinteger(L, (lua_Integer)c->w); lua_setfield(L, -2, "id"); lua_pushstring(L, c->title); lua_setfield(L, -2, "title"); lua_pushboolean(L, c->active); lua_setfield(L, -2, "active");
        lua_pushboolean(L, c->hidden); lua_setfield(L, -2, "hidden"); lua_pushboolean(L, c->attention); lua_setfield(L, -2, "attention");
        lua_pushboolean(L, c->maximized); lua_setfield(L, -2, "maximized"); lua_pushboolean(L, c->fullscreen); lua_setfield(L, -2, "fullscreen"); lua_pushinteger(L, c->stacking); lua_setfield(L, -2, "stacking"); lua_pushinteger(L, c->desktop); lua_setfield(L, -2, "desktop"); lua_pushstring(L, c->cls); lua_setfield(L, -2, "class");
        if (c->has_icon) { char key[64]; snprintf(key, sizeof key, "@icon:%lu", c->w); lua_pushstring(L, key); lua_setfield(L, -2, "icon"); }
        lua_rawseti(L, -2, k++); }
    return 1;
}
static void send_root_message(Window w, Atom type, long d0, long d1, long d2) {
    XEvent e = {0}; e.xclient.type = ClientMessage; e.xclient.window = w; e.xclient.message_type = type; e.xclient.format = 32; e.xclient.data.l[0] = d0; e.xclient.data.l[1] = d1; e.xclient.data.l[2] = d2;
    XSendEvent(dpy, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e); XFlush(dpy);
}
static int l_activate(lua_State *L) { Window w = (Window)luaL_checkinteger(L, 1); send_root_message(w, atom[A_NET_ACTIVE_WINDOW], 2, CurrentTime, 0); return 0; }
static int l_minimize(lua_State *L) { Window w = (Window)luaL_checkinteger(L, 1); XIconifyWindow(dpy, w, screen); XFlush(dpy); return 0; }
static int l_close(lua_State *L) { Window w = (Window)luaL_checkinteger(L, 1); send_root_message(w, atom[A_NET_CLOSE_WINDOW], CurrentTime, 2, 0); return 0; }
static int l_spawn(lua_State *L) { const char *cmd = luaL_checkstring(L, 1); if (fork() == 0) { setsid(); execl("/bin/sh", "sh", "-c", cmd, NULL); _exit(1); } return 0; }
/* lh.set_desktop(n): ask the WM to switch to desktop n (EWMH _NET_CURRENT_DESKTOP client message to the root) */
static int l_set_desktop(lua_State *L) { long n = (long)luaL_checkinteger(L, 1); send_root_message(root, atom[A_NET_CURRENT_DESKTOP], n, CurrentTime, 0); return 0; }
static int l_desktop(lua_State *L) { unsigned long n; int fmt; unsigned char *d = get_prop(root, atom[A_NET_CURRENT_DESKTOP], XA_CARDINAL, &n, &fmt); lua_pushinteger(L, d ? ((long *)d)[0] : 0); if (d) XFree(d); return 1; }
/* lh.maxmode([desktop]): lhwm's _LHWM_MAXIMIZED_MODE for that desktop (default: the current one) as a boolean, or nil when
   lhwm is not the running WM (no _NET_SUPPORTING_WM_CHECK window named lhwm: the caller falls back to its own EWMH rule) */
static int l_maxmode(lua_State *L) {
    unsigned long n; int fmt; unsigned char *d = get_prop(root, atom[A_NET_SUPPORTING_WM_CHECK], XA_WINDOW, &n, &fmt); bool lhwm = false;
    if (d) { Window chk = ((Window *)d)[0]; XFree(d); unsigned char *nm = get_prop(chk, atom[A_NET_WM_NAME], atom[A_UTF8], &n, &fmt);
        if (nm) { lhwm = n >= 4 && !strncmp((char *)nm, "lhwm", 4); XFree(nm); } }
    if (!lhwm) { lua_pushnil(L); return 1; }
    long desk; if (lua_isinteger(L, 1)) desk = lua_tointeger(L, 1); else { d = get_prop(root, atom[A_NET_CURRENT_DESKTOP], XA_CARDINAL, &n, &fmt); desk = d ? ((long *)d)[0] : 0; if (d) XFree(d); }
    d = get_prop(root, atom[A_LHWM_MAXIMIZED_MODE], XA_CARDINAL, &n, &fmt);
    if (!d) { lua_pushnil(L); return 1; }
    if (desk >= 0 && (unsigned long)desk < n) lua_pushboolean(L, ((long *)d)[desk] != 0); else lua_pushnil(L);
    XFree(d); return 1;
}
static int l_screen(lua_State *L) { lua_pushinteger(L, scr_w); lua_pushinteger(L, scr_h); lua_pushinteger(L, screen); return 3; }   /* width, height, X screen number */
/* lh.root_prop(name): a root property as a string (8-bit types: STRING, UTF8_STRING) or an integer (the first 32-bit item:
   CARDINAL, PIXMAP, WINDOW, ...); nil when missing. lh.set_root_prop(name, string | nil): UTF8_STRING, nil deletes it.
   The theme setting (_LH_THEME, _LH_THEME_RESOLVED) and _LHSHELL_COMMAND go through these (SHELL-DESIGN 6.1/6.2). */
static int l_root_prop(lua_State *L) {
    Atom a = XInternAtom(dpy, luaL_checkstring(L, 1), False), ty; int fmt; unsigned long n, after; unsigned char *d = NULL;
    if (XGetWindowProperty(dpy, root, a, 0, 4096, False, AnyPropertyType, &ty, &fmt, &n, &after, &d) != Success || !d || !n) { if (d) XFree(d); lua_pushnil(L); return 1; }
    if (fmt == 8) lua_pushlstring(L, (char *)d, n); else if (fmt == 32) lua_pushinteger(L, (lua_Integer)((unsigned long *)d)[0]); else lua_pushinteger(L, ((unsigned short *)d)[0]);
    XFree(d); return 1;
}
static int l_set_root_prop(lua_State *L) {
    Atom a = XInternAtom(dpy, luaL_checkstring(L, 1), False);
    if (lua_isnoneornil(L, 2)) XDeleteProperty(dpy, root, a);
    else { size_t n; const char *v = luaL_checklstring(L, 2, &n); XChangeProperty(dpy, root, a, atom[A_UTF8], 8, PropModeReplace, (const unsigned char *)v, (int)n); }
    XFlush(dpy); return 0;
}
/* lhde's data directories (liblhde, lhde/data.h): lh.data_find(rel) -> the first dir/rel that exists, or nil;
   lh.data_resolve(value) -> a registry value as a path; lh.data_resolve_dir(value, marker) -> the directory of value that has
   marker (frames/x + theme.ini, packs/x + shell.lua); lh.data_dirs() -> the list; lh.data_user() -> the user's data directory;
   lh.component_dir(name) -> luxshell/ or session/ (tree or <prefix>/lib/lhde), or nil */
static int l_data_find(lua_State *L) { char b[4200]; if (lhde_data_find(luaL_checkstring(L, 1), b, sizeof b)) lua_pushstring(L, b); else lua_pushnil(L); return 1; }
static int l_data_resolve(lua_State *L) { char b[4200]; lua_pushstring(L, lhde_data_resolve(luaL_checkstring(L, 1), b, sizeof b)); return 1; }
static int l_data_resolve_dir(lua_State *L) { char b[4200]; lua_pushstring(L, lhde_data_resolve_dir(luaL_checkstring(L, 1), luaL_checkstring(L, 2), b, sizeof b)); return 1; }
static int l_data_dirs(lua_State *L) { const char *d[LHDE_DATA_MAX]; int n = lhde_data_dirs(d, LHDE_DATA_MAX); lua_createtable(L, n, 0);
    for (int i = 0; i < n; i++) { lua_pushstring(L, d[i]); lua_rawseti(L, -2, i + 1); } return 1; }
static int l_data_user(lua_State *L) { lua_pushstring(L, lhde_data_user()); return 1; }
static int l_component_dir(lua_State *L) { char b[4200]; if (lhde_component_dir(luaL_checkstring(L, 1), b, sizeof b)) lua_pushstring(L, b); else lua_pushnil(L); return 1; }
/* liblhde's theme resolver and settings store (docs/THEME-FORMAT.md, docs/SETTINGS.md):
     lh.theme_resolve(name) -> { name =, file =, v = { ["Glass.frames"] = ..., ... }, why = { ... }, record = TEXT (no serial) },
                               or nil, error
     lh.theme_record(name, serial) -> the _LH_THEME_RESOLVED text (theme=, serial=, the record) or nil
     lh.theme_list() -> { { name =, title =, file =, group = 0 user | 1 shipped | 2 style }, ... }
     lh.theme_lookup(name) -> the theme's name (aliases resolved) and its file ("" for a bare style), or nil
     lh.theme_set_key(name, "Sec.key", value | nil) -> the theme to select now (NAME-modified or NAME), or nil
     lh.theme_user_dir() -> ~/.config/lhde/themes
     the current layer (THEME-FORMAT.md sections 3 and 14, Windows' "NAME (Modified)"; settings.ini [current]):
     lh.current_set_key("Sec.key", value | nil) -> true when settings.ini changed, false when not, nil on an error (no theme
     selected, not a theme key); lh.current_keys() -> { { key =, value = }, ... } of the selected theme; lh.current_theme() -> the
     theme the layer belongs to ("" without one); lh.theme_select(name) -> true when settings.ini changed (the [theme] name, another
     theme's layer dropped), false when not, nil on an error; lh.theme_save_as(file, title) -> true, or nil, error
     lh.settings_get(sec, key) -> value ("" when empty) or nil when absent; lh.settings_set(sec, key, value | nil) -> true when the
     file changed; lh.settings_path(); lh.config_file(NAME), lh.state_file(NAME) -> path; lh.settings_reload();
     lh.settings_migrate() -> count, report */
static int l_theme_resolve(lua_State *L) {
    char err[600]; lhde_theme *t = lhde_theme_resolve(luaL_checkstring(L, 1), err, sizeof err);
    if (!t) { lua_pushnil(L); lua_pushstring(L, err); return 2; }
    lua_newtable(L);
    lua_pushstring(L, lhde_theme_get(t, "theme")); lua_setfield(L, -2, "name");
    lua_pushstring(L, lhde_theme_get(t, "file")); lua_setfield(L, -2, "file");
    lua_newtable(L); lua_newtable(L);
    const char *k, *v, *w;
    for (int i = 0; lhde_theme_item(t, i, &k, &v, &w); i++) { lua_pushstring(L, v); lua_setfield(L, -3, k); lua_pushstring(L, w); lua_setfield(L, -2, k); }
    lua_setfield(L, -3, "why"); lua_setfield(L, -2, "v");
    char *rec = lhde_theme_serialize(t, 0);
    if (rec) { char *body = strchr(rec, '\n'); body = body ? strchr(body + 1, '\n') : NULL;   /* without theme= and serial= */
               lua_pushstring(L, body ? body + 1 : rec); lua_setfield(L, -2, "record"); free(rec); }
    lhde_theme_free(t);
    return 1;
}
static int l_theme_record(lua_State *L) {
    lhde_theme *t = lhde_theme_resolve(luaL_checkstring(L, 1), NULL, 0);
    if (!t) { lua_pushnil(L); return 1; }
    char *rec = lhde_theme_serialize(t, (unsigned long)luaL_optinteger(L, 2, 0));
    lua_pushstring(L, rec ? rec : ""); free(rec); lhde_theme_free(t); return 1;
}
static void theme_list_cb(const char *name, const char *title, const char *file, int group, void *ud) {
    lua_State *L = ud; lua_newtable(L);
    lua_pushstring(L, name); lua_setfield(L, -2, "name"); lua_pushstring(L, title); lua_setfield(L, -2, "title");
    lua_pushstring(L, file); lua_setfield(L, -2, "file"); lua_pushinteger(L, group); lua_setfield(L, -2, "group");
    lua_rawseti(L, -2, (lua_Integer)lua_rawlen(L, -2) + 1);
}
static int l_theme_list(lua_State *L) { lua_newtable(L); lhde_theme_list(theme_list_cb, L); return 1; }
static int l_theme_lookup(lua_State *L) {
    char c[256], f[4200];
    if (!lhde_theme_lookup(luaL_checkstring(L, 1), c, sizeof c, f, sizeof f)) { lua_pushnil(L); return 1; }
    lua_pushstring(L, c); lua_pushstring(L, f); return 2;
}
static int l_theme_set_key(lua_State *L) {
    char sel[300];
    int r = lhde_theme_set_key(luaL_checkstring(L, 1), luaL_checkstring(L, 2), luaL_optstring(L, 3, NULL), sel, sizeof sel);
    if (r < 0) { lua_pushnil(L); return 1; }
    lua_pushstring(L, sel); lua_pushboolean(L, r > 0); return 2;
}
static int l_current_set_key(lua_State *L) {
    int r = lhde_current_set_key(luaL_checkstring(L, 1), luaL_optstring(L, 2, NULL));
    if (r < 0) lua_pushnil(L); else lua_pushboolean(L, r > 0);
    return 1;
}
static void current_key_cb(const char *k, const char *v, void *ud) {
    lua_State *L = ud; lua_newtable(L);
    lua_pushstring(L, k); lua_setfield(L, -2, "key"); lua_pushstring(L, v ? v : ""); lua_setfield(L, -2, "value");
    lua_rawseti(L, -2, (lua_Integer)lua_rawlen(L, -2) + 1);
}
static int l_current_keys(lua_State *L) { lua_newtable(L); lhde_current_keys(current_key_cb, L); return 1; }
static int l_current_theme(lua_State *L) { char b[300]; lua_pushstring(L, lhde_current_theme(b, sizeof b)); return 1; }
static int l_theme_select(lua_State *L) {
    int r = lhde_theme_select(luaL_checkstring(L, 1));
    if (r < 0) lua_pushnil(L); else lua_pushboolean(L, r > 0);
    return 1;
}
static int l_theme_save_as(lua_State *L) {
    char err[600] = "";
    if (lhde_theme_save_as(luaL_checkstring(L, 1), luaL_checkstring(L, 2), err, sizeof err)) { lua_pushnil(L); lua_pushstring(L, err); return 2; }
    lua_pushboolean(L, 1); return 1;
}
static int l_theme_user_dir(lua_State *L) { char b[4200]; lua_pushstring(L, lhde_theme_user_dir(b, sizeof b)); return 1; }
static int l_settings_get(lua_State *L) {
    char b[4096]; char p[4200]; lhde_settings_path(p, sizeof p);
    lhde_ini *ini = lhde_ini_load(p);
    const char *v = lhde_ini_get(ini, luaL_checkstring(L, 1), luaL_checkstring(L, 2), b, sizeof b);
    if (v) lua_pushstring(L, v); else lua_pushnil(L);
    lhde_ini_free(ini); return 1;
}
static int l_settings_set(lua_State *L) {
    int r = lhde_settings_set(luaL_checkstring(L, 1), luaL_checkstring(L, 2), luaL_optstring(L, 3, NULL));
    lua_pushboolean(L, r > 0); return 1;
}
static int l_settings_path(lua_State *L) { char b[4200]; lua_pushstring(L, lhde_settings_path(b, sizeof b)); return 1; }
/* lh.config_file(NAME) / lh.state_file(NAME): the path of NAME in lhde's config directory (what the user chose) or state directory
   (what we remember on our own: luxshell-state.ini, luxshell-mfu.ini); the directory is created (docs/SETTINGS.md "Files") */
static int l_config_file(lua_State *L) { char b[4200]; lua_pushstring(L, lhde_config_file(luaL_checkstring(L, 1), b, sizeof b)); return 1; }
static int l_state_file(lua_State *L) { char b[4200]; lua_pushstring(L, lhde_state_file(luaL_checkstring(L, 1), b, sizeof b)); return 1; }
static int l_settings_reload(lua_State *L) { (void)L; lhde_settings_reload(); return 0; }
static int l_settings_migrate(lua_State *L) {
    static char rep[65536]; int n = lhde_settings_migrate(0, rep, sizeof rep);
    lua_pushinteger(L, n); lua_pushstring(L, rep); return 2;
}
static int l_realpath(lua_State *L) { char buf[4096]; if (realpath(luaL_checkstring(L, 1), buf)) lua_pushstring(L, buf); else lua_pushnil(L); return 1; }
/* lh.list_dir(path): file names in a directory (no . and ..), nil when it cannot be read; lh.mtime(path): modification time
   in seconds or nil (desktop-entry and icon caches of the start menu) */
static int l_list_dir(lua_State *L) { DIR *d = opendir(luaL_checkstring(L, 1)); if (!d) { lua_pushnil(L); return 1; }
    lua_newtable(L); int k = 1; struct dirent *e; while ((e = readdir(d))) { if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue; lua_pushstring(L, e->d_name); lua_rawseti(L, -2, k++); }
    closedir(d); return 1; }
static int l_mtime(lua_State *L) { struct stat st; if (stat(luaL_checkstring(L, 1), &st)) { lua_pushnil(L); return 1; } lua_pushinteger(L, (lua_Integer)st.st_mtime); return 1; }
static int l_quit(lua_State *L) { (void)L; running = false; return 0; }
/* lh.cm_owner(): whether a compositing manager owns _NET_WM_CM_S<screen> (picom or any other) */
static int l_cm_owner(lua_State *L) { char name[32]; snprintf(name, sizeof name, "_NET_WM_CM_S%d", screen); lua_pushboolean(L, XGetSelectionOwner(dpy, XInternAtom(dpy, name, False)) != None); return 1; }
/* lh.kill(pid): SIGTERM to one process (the compositor rule in runtime.lua picks the pid: a `picom` with our DISPLAY) */
static int l_kill(lua_State *L) { lua_pushboolean(L, kill((pid_t)luaL_checkinteger(L, 1), SIGTERM) == 0); return 1; }
/* lh.image_alpha(path, [strips, strip]): the highest alpha (0..255) in an image or in one of its vertical state strips
   (the start menu's cascade highlight: Slate's StartMenu::Toolbar.BUTTON hot strip is empty) */
/* lh.fonts("DIR[;DIR...]"): the theme's font directories (themes.ini `fonts`, docs/history/THEMES.md "Fonts") become the process's fontconfig
   application fonts, replacing the previous set (FcConfigAppFontClear; the fallback lookups of text.c), and libuxtheme's font
   directories (ut_add_font_dir, text.c: libuxtheme only adds, so a directory of an earlier theme stays known to it). Returns the
   face and file the given description (default: "Segoe UI 9") now resolves to, and whether the set changed. Called at pack load
   and on every _LH_THEME change. */
static char fonts_cur[2048] = "\x01";
static int l_fonts(lua_State *L) {
    const char *list = luaL_optstring(L, 1, ""), *desc = luaL_optstring(L, 2, "Segoe UI 9");
    int changed = strcmp(list, fonts_cur) != 0;
    if (changed) {
        snprintf(fonts_cur, sizeof fonts_cur, "%s", list);
        FcInit(); FcConfigAppFontClear(NULL);
        char buf[2048]; snprintf(buf, sizeof buf, "%s", list);
        for (char *save = NULL, *f = strtok_r(buf, ";", &save); f; f = strtok_r(NULL, ";", &save)) {
            while (*f == ' ') f++; size_t n = strlen(f); while (n && f[n - 1] == ' ') f[--n] = 0; if (!*f) continue;
            struct stat st; if (stat(f, &st)) { fprintf(stderr, "luxshell: fonts: %s does not exist\n", f); continue; }
            if (!(S_ISDIR(st.st_mode) ? FcConfigAppFontAddDir(NULL, (const FcChar8 *)f) : FcConfigAppFontAddFile(NULL, (const FcChar8 *)f)))
                fprintf(stderr, "luxshell: fonts: cannot load %s\n", f);
            text_add_font_dir(f);
        }
    }
    char fam[256], file[512]; text_resolve(desc, fam, sizeof fam, file, sizeof file);
    lua_pushstring(L, fam); lua_pushstring(L, file); lua_pushboolean(L, changed); return 3;
}
static int l_image_alpha(lua_State *L) {
    cairo_surface_t *im = img_from_lua(L, 1); if (!im || cairo_image_surface_get_format(im) != CAIRO_FORMAT_ARGB32) { lua_pushinteger(L, im ? 255 : 0); return 1; }
    int strips = (int)luaL_optinteger(L, 2, 1), strip = (int)luaL_optinteger(L, 3, 0); if (strips < 1) strips = 1;
    int w = cairo_image_surface_get_width(im), h = cairo_image_surface_get_height(im) / strips, st = cairo_image_surface_get_stride(im); cairo_surface_flush(im);
    const unsigned char *d = cairo_image_surface_get_data(im); int m = 0;
    for (int y = strip * h; y < (strip + 1) * h && y < cairo_image_surface_get_height(im); y++) for (int x = 0; x < w; x++) { int a = ((const uint32_t *)(d + y * st))[x] >> 24; if (a > m) m = a; }
    lua_pushinteger(L, m); return 1;
}
static int l_now(lua_State *L) { lua_pushinteger(L, now_ms()); return 1; }

/* ---- M8: the classic taskbar next to lhshell (SHELL-DESIGN "M8 lhshell side") ---- */
/* lh.selection_owner(name): the owner window of a selection (e.g. _NET_SYSTEM_TRAY_S0, _LH_STARTMENU_S0) or nil */
static int l_selection_owner(lua_State *L) { Window w = XGetSelectionOwner(dpy, XInternAtom(dpy, luaL_checkstring(L, 1), False)); if (w == None) lua_pushnil(L); else lua_pushinteger(L, (lua_Integer)w); return 1; }
/* lh.watch_selection(name): XFixes owner-change notification for a selection (event "selection"(name, owner or 0));
   false when the server has no XFIXES (the caller polls lh.selection_owner instead) */
static int xfixes_event = -1;
static int l_watch_selection(lua_State *L) {
    static int have = -1; int er;
    if (have < 0) { int ev; have = XFixesQueryExtension(dpy, &ev, &er) ? 1 : 0; if (have) { int ma = 5, mi = 0; XFixesQueryVersion(dpy, &ma, &mi); xfixes_event = ev; } }
    if (!have) { lua_pushboolean(L, 0); return 1; }
    XFixesSelectSelectionInput(dpy, root, XInternAtom(dpy, luaL_checkstring(L, 1), False), XFixesSetSelectionOwnerNotifyMask | XFixesSelectionWindowDestroyNotifyMask | XFixesSelectionClientCloseNotifyMask);
    XFlush(dpy); lua_pushboolean(L, 1); return 1;
}
/* lh.own_selection(name): own a selection with a hidden window of ours (manager-style, e.g. _LHSHELL_S0 = "lhshell runs here");
   returns the window or nil */
static int l_own_selection(lua_State *L) {
    Atom sel = XInternAtom(dpy, luaL_checkstring(L, 1), False), ts = XInternAtom(dpy, "_LHSHELL_TIMESTAMP", False);
    XSetWindowAttributes a = { .event_mask = PropertyChangeMask, .override_redirect = True };
    Window w = XCreateWindow(dpy, root, -1, -1, 1, 1, 0, CopyFromParent, InputOnly, CopyFromParent, CWEventMask | CWOverrideRedirect, &a);
    XStoreName(dpy, w, "lhshell-selection");
    unsigned char c = 0; XEvent e; XChangeProperty(dpy, w, ts, XA_STRING, 8, PropModeAppend, &c, 0); XWindowEvent(dpy, w, PropertyChangeMask, &e);
    XSetSelectionOwner(dpy, sel, w, e.xproperty.time);
    if (XGetSelectionOwner(dpy, sel) != w) { XDestroyWindow(dpy, w); lua_pushnil(L); return 1; }
    XFlush(dpy); lua_pushinteger(L, (lua_Integer)w); return 1;
}
/* lh.send_message(window, type, l0, l1, l2, l3, l4): a format-32 ClientMessage to that window only (no propagation) */
static int l_send_message(lua_State *L) {
    XEvent e = {0}; e.xclient.type = ClientMessage; e.xclient.window = (Window)luaL_checkinteger(L, 1); e.xclient.message_type = XInternAtom(dpy, luaL_checkstring(L, 2), False); e.xclient.format = 32;
    for (int i = 0; i < 5; i++) e.xclient.data.l[i] = (long)luaL_optinteger(L, 3 + i, 0);
    xerror_quiet++; Status ok = XSendEvent(dpy, e.xclient.window, False, NoEventMask, &e); XSync(dpy, False); xerror_quiet--;
    lua_pushboolean(L, ok != 0); return 1;
}
/* lh.send_root_message(type, l0, l1, l2, l3, l4): a format-32 ClientMessage on the root, sent with SubstructureNotifyMask
   (SETTINGS.md _LH_SETTINGS_CHANGED: every client selecting SubstructureNotify on the root gets it, ourselves included) */
static int l_send_root_message(lua_State *L) {
    XEvent e = {0}; e.xclient.type = ClientMessage; e.xclient.window = root; e.xclient.message_type = XInternAtom(dpy, luaL_checkstring(L, 1), False); e.xclient.format = 32;
    for (int i = 0; i < 5; i++) e.xclient.data.l[i] = (long)luaL_optinteger(L, 2 + i, 0);
    XSendEvent(dpy, root, False, SubstructureNotifyMask, &e); XFlush(dpy); return 0;
}
/* lh.atom(name): the atom's number (interned), e.g. a section hint for _LH_SETTINGS_CHANGED */
static int l_atom(lua_State *L) { lua_pushinteger(L, (lua_Integer)XInternAtom(dpy, luaL_checkstring(L, 1), False)); return 1; }
/* lh.set_root_cardinal(name, n | nil): a CARDINAL[1] root property (nil deletes it), e.g. _LHSHELL_CLOCK_VISIBLE */
static int l_set_root_cardinal(lua_State *L) {
    Atom a = XInternAtom(dpy, luaL_checkstring(L, 1), False);
    if (lua_isnoneornil(L, 2)) XDeleteProperty(dpy, root, a); else { long v = (long)luaL_checkinteger(L, 2); XChangeProperty(dpy, root, a, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&v, 1); }
    XFlush(dpy); return 0;
}
/* lh.struts(): the struts of every mapped top-level window that is not ours (and of the clients inside lhwm's frames), as
   { win, left, right, top, bottom, left_start_y, left_end_y, right_start_y, right_end_y, top_start_x, top_end_x,
   bottom_start_x, bottom_end_x } (_NET_WM_STRUT_PARTIAL; a plain _NET_WM_STRUT spans the whole edge) */
static void strut_of(lua_State *L, Window w, int *k) {
    unsigned long n; int fmt; long v[12] = {0}; bool got = false;
    unsigned char *d = get_prop(w, atom[A_NET_WM_STRUT_PARTIAL], XA_CARDINAL, &n, &fmt);
    if (d && fmt == 32 && n >= 12) { for (int i = 0; i < 12; i++) v[i] = ((long *)d)[i]; got = true; }
    if (d) XFree(d);
    if (!got) { d = get_prop(w, atom[A_NET_WM_STRUT], XA_CARDINAL, &n, &fmt);
        if (d && fmt == 32 && n >= 4) { for (int i = 0; i < 4; i++) v[i] = ((long *)d)[i]; v[4] = v[8] = 0; v[5] = v[7] = scr_h - 1; v[6] = 0; v[9] = v[11] = scr_w - 1; v[10] = 0; got = true; }
        if (d) XFree(d); }
    if (!got || !(v[0] || v[1] || v[2] || v[3])) return;
    static const char *names[12] = { "left", "right", "top", "bottom", "left_start_y", "left_end_y", "right_start_y", "right_end_y", "top_start_x", "top_end_x", "bottom_start_x", "bottom_end_x" };
    lua_newtable(L); lua_pushinteger(L, (lua_Integer)w); lua_setfield(L, -2, "win");
    for (int i = 0; i < 12; i++) { lua_pushinteger(L, v[i]); lua_setfield(L, -2, names[i]); }
    lua_rawseti(L, -2, ++*k);
}
static int l_struts(lua_State *L) {
    lua_newtable(L); int k = 0; Window r, p, *kids = NULL; unsigned nk = 0;
    xerror_quiet++;
    if (XQueryTree(dpy, root, &r, &p, &kids, &nk)) {
        for (unsigned i = 0; i < nk; i++) {
            if (surface_from_window(kids[i])) continue;
            XWindowAttributes wa; if (!XGetWindowAttributes(dpy, kids[i], &wa) || wa.map_state != IsViewable || wa.override_redirect) continue;
            int before = k; strut_of(L, kids[i], &k);
            if (k == before) { Window r2, p2, *sub = NULL; unsigned ns = 0;   /* a frame: look one level down */
                if (XQueryTree(dpy, kids[i], &r2, &p2, &sub, &ns)) { for (unsigned j = 0; j < ns; j++) if (!surface_from_window(sub[j])) strut_of(L, sub[j], &k); if (sub) XFree(sub); } }
        }
        if (kids) XFree(kids);
    }
    XSync(dpy, False); xerror_quiet--;
    return 1;
}
/* lh.spawn_pid(cmd, { NAME = value, ... }): like lh.spawn, with extra environment, through `sh -c 'exec cmd'` so the pid
   returned is the program's own (the classic taskbar is stopped by this pid, after checking its DISPLAY) */
static int l_spawn_pid(lua_State *L) {
    const char *cmd = luaL_checkstring(L, 1); int envi = lua_istable(L, 2) ? 2 : 0;
    char *full = malloc(strlen(cmd) + 8); sprintf(full, "exec %s", cmd);
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        if (envi) { lua_pushnil(L); while (lua_next(L, envi)) { const char *k = lua_tostring(L, -2), *v = lua_tostring(L, -1); if (k && v) setenv(k, v, 1); lua_pop(L, 1); } }
        execl("/bin/sh", "sh", "-c", full, NULL); _exit(127);
    }
    free(full);
    if (pid < 0) lua_pushnil(L); else lua_pushinteger(L, pid); return 1;
}
static int l_pointer(lua_State *L) { Window r, c; int rx, ry, wx, wy; unsigned m; XQueryPointer(dpy, root, &r, &c, &rx, &ry, &wx, &wy, &m); lua_pushinteger(L, rx); lua_pushinteger(L, ry); return 2; }

void emit(const char *ev, int nargs) {
    /* calls lh._emit(ev, args...) with args already on the stack */
    lua_getglobal(L, "lh"); lua_getfield(L, -1, "_emit"); lua_remove(L, -2);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1 + nargs); return; }
    lua_insert(L, -nargs - 1); lua_pushstring(L, ev); lua_insert(L, -nargs - 1);   /* fn, ev, args (was calling the first arg) */
    if (lua_pcall(L, 1 + nargs, 0, 0)) { fprintf(stderr, "luxshell: event %s: %s\n", ev, lua_tostring(L, -1)); lua_pop(L, 1); }
}
static void call_handler(struct node *n, const char *name, int nargs) {
    if (!push_node_table(L, n)) { lua_pop(L, nargs); return; }
    lua_getfield(L, -1, name);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 2 + nargs); return; }
    lua_insert(L, -nargs - 2); lua_insert(L, -nargs - 1);   /* fn, self, args */
    if (lua_pcall(L, 1 + nargs, 0, 0)) { fprintf(stderr, "luxshell: %s: %s\n", name, lua_tostring(L, -1)); lua_pop(L, 1); }
}
/* surface.<name>(surface, args...) when the Lua surface table has that field */
static void call_surface(struct surface *s, const char *name, int nargs) {
    push_ref(L, s->lua_ref); lua_getfield(L, -1, name);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 2 + nargs); return; }
    lua_insert(L, -nargs - 2); lua_insert(L, -nargs - 1);   /* fn, self, args */
    if (lua_pcall(L, 1 + nargs, 0, 0)) { fprintf(stderr, "luxshell: surface %s: %s\n", name, lua_tostring(L, -1)); lua_pop(L, 1); }
}
/* the pointer shape of the hot node (or its nearest ancestor with a `cursor`): the sizers' SizeWE / SizeNS (TileThumb Cursor) */
static void update_cursor(struct surface *s, struct node *n) {
    static Cursor shapes[5]; static const unsigned glyph[5] = { 0, XC_left_ptr, XC_sb_h_double_arrow, XC_sb_v_double_arrow, XC_hand2 };
    int c = 0; for (struct node *x = n; x && !c; x = x->parent) c = x->cursor;
    if (c > 0 && c < 5 && !shapes[c]) shapes[c] = XCreateFontCursor(dpy, glyph[c]);
    if (c != s->cursor) { s->cursor = c; XDefineCursor(dpy, s->win, c > 0 && c < 5 ? shapes[c] : None); }
}
static void set_hot(struct surface *s, struct node *n) {
    if (s->pressed == NULL) update_cursor(s, n);
    if (s->hot == n) return;
    if (n) { node_ref(n); node_ref(n); }   /* s->hot's reference, and ours across the handlers (they may rebuild the tree n is in) */
    struct node *old = s->hot; s->hot = n;   /* old's reference is ours now */
    if (old) { if (old->kind == N_BUTTON) { old->state = old->checked ? B_CHECKED : B_NORMAL; } lua_pushboolean(L, 0); call_handler(old, "on_hover", 1); damage_owner(old); node_unref(old); }
    if (n) { if (n->kind == N_BUTTON) n->state = n->checked ? B_HOTCHECKED : B_HOT; lua_pushboolean(L, 1); call_handler(n, "on_hover", 1); damage_owner(n); node_unref(n); }
}
static void handle_event(XEvent *e) {
    if (debug && (e->type == ButtonPress || e->type == ButtonRelease)) fprintf(stderr, "luxshell: [%lld] %s on 0x%lx at %d,%d\n", now_ms(), e->type == ButtonPress ? "press" : "release", e->xbutton.window, e->xbutton.x, e->xbutton.y);
    if (tray_handle_event(e)) return;
    if (e->type == PropertyNotify) {
        XPropertyEvent *p = &e->xproperty;
        if (p->window == root) { if (p->atom == atom[A_NET_CLIENT_LIST] || p->atom == atom[A_NET_ACTIVE_WINDOW]) clients_refresh(); else if (p->atom == atom[A_NET_CLIENT_LIST_STACKING]) { if (stacking_refresh()) emit("windows", 0); } else if (p->atom == atom[A_NET_CURRENT_DESKTOP]) emit("desktop", 0); else if (p->atom == atom[A_LHWM_MAXIMIZED_MODE] || p->atom == atom[A_NET_SUPPORTING_WM_CHECK]) emit("maxmode", 0);
            else if (p->atom == atom[A_LH_THEME] && p->state == PropertyNewValue) emit("lh-theme", 0);          /* the session theme (6.1) */
            else if (p->atom == atom[A_LHSHELL_COMMAND] && p->state == PropertyNewValue) emit("command", 0);   /* a session command (6.2) */
            else if (p->atom == atom[A_NET_WORKAREA]) emit("workarea", 0);                                       /* struts changed (M8: the sidebar yields to a foreign bottom dock) */
            else if (p->atom == atom[A_LH_STARTMENU_OPEN]) emit("startmenu-open", 0);                           /* the provider's menu state (CLIENT-PROTOCOL "Requests to the wux side") */
            return; }
        struct client *c = client_find(p->window); if (!c) return;
        if (p->atom == atom[A_NET_WM_NAME] || p->atom == XA_WM_NAME) client_update_title(c); else if (p->atom == atom[A_NET_WM_ICON] || p->atom == XA_WM_HINTS) client_update_icon(c); else if (p->atom == atom[A_NET_WM_STATE] || p->atom == atom[A_WM_STATE] || p->atom == atom[A_NET_WM_DESKTOP]) client_update_state(c); else return;
        lua_pushinteger(L, (lua_Integer)c->w); emit("window", 1); return;
    }
    /* _LHSHELL_START on the root: lhwm's Super key (l[0] = timestamp, no rect) or a classic Start button (flxp-taskbar with
       start_menu = lhshell: l[0] = flags, l[1..4] = the button's rect); the Lua side decides (M8 start-menu provider protocol) */
    if (e->type == ClientMessage && e->xclient.window == root) {
        if (e->xclient.message_type == atom[A_LHSHELL_START]) { for (int i = 0; i < 5; i++) lua_pushinteger(L, e->xclient.data.l[i]); emit("start-key", 5); }
        /* SETTINGS.md: a writer saved settings.ini (l[0] = serial, l[1] = 0 or a section hint atom); ours come back too */
        else if (e->xclient.message_type == atom[A_LH_SETTINGS_CHANGED]) {
            char *nm = e->xclient.data.l[1] ? XGetAtomName(dpy, (Atom)e->xclient.data.l[1]) : NULL;
            lua_pushinteger(L, (lua_Integer)(unsigned long)e->xclient.data.l[0]); if (nm) lua_pushstring(L, nm); else lua_pushnil(L); if (nm) XFree(nm);
            emit("settings-changed", 2);
        }
        return;
    }
    if (xfixes_event >= 0 && e->type == xfixes_event + XFixesSelectionNotify) {   /* lh.watch_selection: owner changed */
        XFixesSelectionNotifyEvent *x = (XFixesSelectionNotifyEvent *)e; char *nm = XGetAtomName(dpy, x->selection);
        lua_pushstring(L, nm ? nm : ""); lua_pushinteger(L, x->subtype == XFixesSetSelectionOwnerNotify ? (lua_Integer)x->owner : 0); if (nm) XFree(nm);
        emit("selection", 2); return;
    }
    /* top-level windows mapped / unmapped / destroyed (root SubstructureNotify): a dock with a strut may have come or gone even
       when _NET_WORKAREA keeps its value (M8: the sidebar's yield to the classic taskbar) */
    /* (the window and "map" / "unmap" / "destroy": runtime.lua's start button follows a start-menu provider's popup by them) */
    if ((e->type == MapNotify && e->xmap.event == root && !surface_from_window(e->xmap.window)) || (e->type == UnmapNotify && e->xunmap.event == root && !surface_from_window(e->xunmap.window))
        || (e->type == DestroyNotify && e->xdestroywindow.event == root)) {
        lua_pushinteger(L, (lua_Integer)(e->type == MapNotify ? e->xmap.window : e->type == UnmapNotify ? e->xunmap.window : e->xdestroywindow.window)); lua_pushstring(L, e->type == MapNotify ? "map" : e->type == UnmapNotify ? "unmap" : "destroy");
        emit("toplevel", 2); return; }
    struct surface *s = surface_from_window(e->xany.window);
    if ((e->type == KeyPress || e->type == KeyRelease) && kb_surface) s = kb_surface;   /* the grabbing menu gets every key */
    if (!s) return;
    switch (e->type) {
        case KeyPress: case KeyRelease: {   /* surface.on_key(self, keysym name, text, modifier state, pressed) */
            char buf[32] = {0}; KeySym sym = NoSymbol; int n = XLookupString(&e->xkey, buf, sizeof buf - 1, &sym, NULL); buf[n > 0 ? n : 0] = 0;
            if (n == 1 && (unsigned char)buf[0] >= 0x80) { unsigned char c = buf[0]; buf[0] = 0xC0 | (c >> 6); buf[1] = 0x80 | (c & 0x3F); buf[2] = 0; }   /* Latin-1 → UTF-8 */
            const char *name = XKeysymToString(sym);
            lua_pushstring(L, name ? name : ""); lua_pushstring(L, (n > 0 && (unsigned char)buf[0] >= 0x20 && buf[0] != 0x7f) ? buf : ""); lua_pushinteger(L, e->xkey.state); lua_pushboolean(L, e->type == KeyPress);
            call_surface(s, "on_key", 4); break; }
        case EnterNotify: lua_pushinteger(L, e->xcrossing.detail); call_surface(s, "on_enter", 1); break;
        case Expose: surface_damage(s); break;
        case ConfigureNotify: if (e->xconfigure.width != s->w || e->xconfigure.height != s->h) { s->w = e->xconfigure.width; s->h = e->xconfigure.height; cairo_xlib_surface_set_size(s->xs, s->w, s->h); surface_damage(s); } break;
        case MotionNotify: { struct node *n = s->root ? scene_hit(s->root, e->xmotion.x, e->xmotion.y) : NULL;
            if (s->pressed) { struct node *pn = s->pressed; node_ref(pn); if (s->pressed->kind == N_BUTTON) { enum bstate want = (n == s->pressed) ? B_PRESSED : (s->pressed->checked ? B_CHECKED : B_NORMAL); if (s->pressed->state != want) { s->pressed->state = want; damage_owner(s->pressed); } }
                lua_pushinteger(L, e->xmotion.x_root - s->press_x); lua_pushinteger(L, e->xmotion.y_root - s->press_y); call_handler(pn, "on_drag", 2); node_unref(pn); }
            else set_hot(s, n); break; }
        case LeaveNotify: if (!s->pressed) set_hot(s, NULL); lua_pushinteger(L, e->xcrossing.detail); call_surface(s, "on_leave", 1); break;
        case ButtonPress: {
            if (s->grabbed && (e->xbutton.x < 0 || e->xbutton.y < 0 || e->xbutton.x >= s->w || e->xbutton.y >= s->h)) {   /* outside all our surfaces */
                push_ref(L, s->lua_ref); lua_getfield(L, -1, "on_outside"); lua_remove(L, -2);
                if (lua_isfunction(L, -1)) { push_ref(L, s->lua_ref); if (lua_pcall(L, 1, 0, 0)) { fprintf(stderr, "luxshell: on_outside: %s\n", lua_tostring(L, -1)); lua_pop(L, 1); } } else lua_pop(L, 1);
                break; }
            struct node *n = s->root ? scene_hit(s->root, e->xbutton.x, e->xbutton.y) : NULL; s->press_x = e->xbutton.x_root; s->press_y = e->xbutton.y_root;
            if (n) { set_node_slot(&s->pressed, n); if (n->kind == N_BUTTON) { n->state = B_PRESSED; damage_owner(n); } lua_pushinteger(L, e->xbutton.button); call_handler(n, "on_press", 1); }
            lua_pushinteger(L, e->xbutton.x); lua_pushinteger(L, e->xbutton.y); lua_pushinteger(L, e->xbutton.button); lua_pushinteger(L, (lua_Integer)s->win); emit("surface-press", 4); break; }
        case ButtonRelease: { struct node *n = s->root ? scene_hit(s->root, e->xbutton.x, e->xbutton.y) : NULL; struct node *p = s->pressed; s->pressed = NULL;   /* the slot's reference on p is ours until the end */
            if (p) { if (p->kind == N_BUTTON) { p->state = (n == p) ? (p->checked ? B_HOTCHECKED : B_HOT) : (p->checked ? B_CHECKED : B_NORMAL); damage_owner(p); }
                lua_pushinteger(L, e->xbutton.button); call_handler(p, "on_release", 1);
                if (n == p) { lua_pushinteger(L, e->xbutton.button); call_handler(p, "on_click", 1); }
                node_unref(p); }
            set_hot(s, s->root ? scene_hit(s->root, e->xbutton.x, e->xbutton.y) : NULL); break; }   /* handlers may have rebuilt the tree */
    }
}

/* _NET_WM_ICON_GEOMETRY (EWMH): every node tagged icon_geometry_for = <client> (the task buttons) publishes its rect in root
   coordinates on that client after each layout; lhwm reads it for the minimise animation. Deleted when the node goes away. */
struct icongeo { Window w; struct surface *s; long r[4]; bool seen; };
static struct icongeo geos[512]; static int ngeos;
static void geo_walk(struct surface *s, struct node *n) {
    if (!n->visible) return;
    if (n->wmicon && n->cw > 0 && n->ch > 0) {
        Window w = (Window)n->wmicon; long r[4] = { s->x + s->geo_dx + lround(n->cx + n->ml), s->y + s->geo_dy + lround(n->cy + n->mt), lround(n->cw - n->ml - n->mr), lround(n->ch - n->mt - n->mb) };   /* the plate, inside the margin */ struct icongeo *g = NULL;
        for (int i = 0; i < ngeos; i++) if (geos[i].w == w) g = &geos[i];
        if (!g && ngeos < 512) { g = &geos[ngeos++]; g->w = w; g->s = s; g->r[0] = g->r[1] = g->r[2] = g->r[3] = -1; }
        if (g) { g->seen = true; g->s = s;
            if (memcmp(g->r, r, sizeof r)) { memcpy(g->r, r, sizeof r); if (client_find(w)) XChangeProperty(dpy, w, atom[A_NET_WM_ICON_GEOMETRY], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)r, 4); } }
    }
    for (int i = 0; i < n->nkids; i++) geo_walk(s, n->kids[i]);
}
static void icon_geometry_drop(struct surface *s) {
    for (int i = ngeos - 1; i >= 0; i--) if (geos[i].s == s) { if (client_find(geos[i].w)) XDeleteProperty(dpy, geos[i].w, atom[A_NET_WM_ICON_GEOMETRY]); geos[i] = geos[--ngeos]; }
}
static void icon_geometry_update(struct surface *s) {
    for (int i = 0; i < ngeos; i++) if (geos[i].s == s) geos[i].seen = false;
    geo_walk(s, s->root);
    for (int i = ngeos - 1; i >= 0; i--) if (geos[i].s == s && !geos[i].seen) { if (client_find(geos[i].w)) XDeleteProperty(dpy, geos[i].w, atom[A_NET_WM_ICON_GEOMETRY]); geos[i] = geos[--ngeos]; }
}

/* ---- tray and notifications (tray.c, notify.c) ---- */
static int l_tray_start(lua_State *L) { lua_pushboolean(L, tray_start(check_node(L, 1))); return 1; }
static int l_tray_count(lua_State *L) { lua_pushinteger(L, tray_count()); return 1; }
static int l_tray_manager(lua_State *L) { Window w = tray_manager(); if (w) lua_pushinteger(L, (lua_Integer)w); else lua_pushnil(L); return 1; }
static int l_tray_stop(lua_State *L) { (void)L; tray_stop(); return 0; }
static int l_notification_closed(lua_State *L) { notify_closed((unsigned)luaL_checkinteger(L, 1), (unsigned)luaL_optinteger(L, 2, 2)); return 0; }
static int l_notification_action(lua_State *L) { notify_action((unsigned)luaL_checkinteger(L, 1), luaL_checkstring(L, 2)); return 0; }

/* lh.image_filter("nearest" | "bilinear" | "best"): the pack's default image scaling filter (scene.h; the packs' shell.lua
   `filter`); the caller repaints. Returns the filter now in force. */
/* lh.image_flush(): re-read changed PNGs on their next use (scene.c image_flush; the pack reload); returns the count */
static int l_image_flush(lua_State *L) { lua_pushinteger(L, image_flush()); return 1; }
/* lh.wm_moveresize(win, x, y, w, h): EWMH _NET_MOVERESIZE_WINDOW for a client (the taskbar menu's Cascade / Tile; lhwm applies
   x,y,w,h as client_move_absolute / client_resize_absolute) */
static int l_wm_moveresize(lua_State *L) {
    XEvent e = {0}; e.xclient.type = ClientMessage; e.xclient.window = (Window)luaL_checkinteger(L, 1);
    e.xclient.message_type = XInternAtom(dpy, "_NET_MOVERESIZE_WINDOW", False); e.xclient.format = 32;
    e.xclient.data.l[0] = 1 | (0xF << 8) | (2 << 12);   /* NorthWestGravity, x y w h present, source: pager */
    for (int i = 0; i < 4; i++) e.xclient.data.l[1 + i] = (long)luaL_checkinteger(L, 2 + i);
    XSendEvent(dpy, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e); XFlush(dpy); return 0;
}
static int l_image_filter(lua_State *L) {
    if (lua_isstring(L, 1)) { int f = filter_of(lua_tostring(L, 1)); scene_filter = f ? f : SF_BILINEAR; }
    lua_pushstring(L, scene_filter == SF_NEAREST ? "nearest" : scene_filter == SF_BEST ? "best" : "bilinear"); return 1;
}
/* image_get hook for "@icon:" keys: wrap the scene loader */
cairo_surface_t *image_get_hook(const char *path);

static const luaL_Reg lhlib[] = { {"node", l_node}, {"surface", l_surface}, {"timer", l_timer}, {"cancel", l_cancel}, {"windows", l_windows}, {"activate", l_activate}, {"minimize", l_minimize}, {"close", l_close},
    {"tray_start", l_tray_start}, {"tray_count", l_tray_count}, {"tray_manager", l_tray_manager}, {"tray_stop", l_tray_stop},
    {"selection_owner", l_selection_owner}, {"watch_selection", l_watch_selection}, {"own_selection", l_own_selection}, {"send_message", l_send_message}, {"send_root_message", l_send_root_message}, {"atom", l_atom},
    {"set_root_cardinal", l_set_root_cardinal}, {"struts", l_struts}, {"spawn_pid", l_spawn_pid}, {"notification_closed", l_notification_closed}, {"notification_action", l_notification_action},
    {"spawn", l_spawn}, {"screen", l_screen}, {"root_prop", l_root_prop}, {"set_root_prop", l_set_root_prop}, {"realpath", l_realpath}, {"maxmode", l_maxmode}, {"desktop", l_desktop}, {"set_desktop", l_set_desktop}, {"quit", l_quit}, {"now", l_now}, {"pointer", l_pointer}, {"list_dir", l_list_dir}, {"mtime", l_mtime},
    {"data_find", l_data_find}, {"data_resolve", l_data_resolve}, {"data_resolve_dir", l_data_resolve_dir}, {"data_dirs", l_data_dirs}, {"data_user", l_data_user},
    {"component_dir", l_component_dir},
    {"theme_resolve", l_theme_resolve}, {"theme_record", l_theme_record}, {"theme_list", l_theme_list}, {"theme_lookup", l_theme_lookup},
    {"theme_set_key", l_theme_set_key}, {"theme_user_dir", l_theme_user_dir}, {"current_set_key", l_current_set_key},
    {"current_keys", l_current_keys}, {"current_theme", l_current_theme}, {"theme_select", l_theme_select}, {"theme_save_as", l_theme_save_as}, {"settings_get", l_settings_get}, {"settings_set", l_settings_set},
    {"settings_path", l_settings_path}, {"config_file", l_config_file}, {"state_file", l_state_file}, {"settings_reload", l_settings_reload}, {"settings_migrate", l_settings_migrate},
    {"cm_owner", l_cm_owner}, {"kill", l_kill}, {"image_alpha", l_image_alpha}, {"fonts", l_fonts}, {"image_filter", l_image_filter}, {"image_flush", l_image_flush}, {"wm_moveresize", l_wm_moveresize}, {NULL, NULL} };
static const luaL_Reg nodemeth[] = { {"add", l_node_add}, {"remove", l_node_remove}, {"clear", l_node_clear}, {"set", l_node_set}, {"bounds", l_node_bounds}, {"measure", l_node_measure}, {"desired", l_node_desired}, {NULL, NULL} };
static const luaL_Reg surfmeth[] = { {"root", l_surface_root}, {"show", l_surface_show}, {"hide", l_surface_hide}, {"move", l_surface_move}, {"raise", l_surface_raise}, {"lower", l_surface_lower}, {"set_layer", l_surface_layer}, {"stackpos", l_surface_stackpos}, {"damage", l_surface_damage}, {"id", l_surface_id},
    {"background", l_surface_background}, {"round", l_surface_round}, {"grab", l_surface_grab}, {"ungrab", l_surface_ungrab}, {"icon_offset", l_surface_icon_offset}, {NULL, NULL} };

int xerror_quiet;   /* > 0: expected errors (freeing an icon's Picture after the icon window died) are not logged */
static int xerror(Display *d, XErrorEvent *e) { if (xerror_quiet) return 0; char buf[128]; XGetErrorText(d, e->error_code, buf, sizeof buf); fprintf(stderr, "luxshell: X error %s (request %d)\n", buf, e->request_code); return 0; }

static volatile sig_atomic_t terminated;
static void on_term(int sig) { (void)sig; terminated = 1; }
int main(int argc, char **argv) {
    /* the Lua code: $LUXSHELL_LUA, else luxshell/ of the source tree (a build tree's binary) or <prefix>/lib/lhde/luxshell */
    char lua_dir[4200] = "", script_buf[4300], exe_dir[4200] = "";
    { const char *e = getenv("LUXSHELL_LUA"); if (e && *e) snprintf(lua_dir, sizeof lua_dir, "%s", e);
      else if (!lhde_component_dir("luxshell", lua_dir, sizeof lua_dir)) snprintf(lua_dir, sizeof lua_dir, "."); }
    { ssize_t k = readlink("/proc/self/exe", exe_dir, sizeof exe_dir - 1); if (k > 0) { exe_dir[k] = 0; char *sl = strrchr(exe_dir, '/'); if (sl) *sl = 0; } else exe_dir[0] = 0; }
    snprintf(script_buf, sizeof script_buf, "%s/runtime.lua", lua_dir);
    const char *script = argc > 1 ? argv[1] : script_buf; signal(SIGCHLD, SIG_IGN);
    struct sigaction sa = { .sa_handler = on_term }; sigaction(SIGTERM, &sa, NULL); sigaction(SIGINT, &sa, NULL);   /* no SA_RESTART: poll returns EINTR */
    dpy = XOpenDisplay(NULL); if (!dpy) { fprintf(stderr, "luxshell: cannot open display\n"); return 1; }
    XSetErrorHandler(xerror); screen = DefaultScreen(dpy); root = RootWindow(dpy, screen); scr_w = DisplayWidth(dpy, screen); scr_h = DisplayHeight(dpy, screen);
    XVisualInfo vi; if (!XMatchVisualInfo(dpy, screen, 32, TrueColor, &vi)) { fprintf(stderr, "luxshell: no ARGB visual\n"); return 1; } argb = vi.visual; cmap = XCreateColormap(dpy, root, argb, AllocNone);
    for (int i = 0; i < A_LAST; i++) atom[i] = XInternAtom(dpy, atom_names[i], False);
    XSelectInput(dpy, root, PropertyChangeMask | SubstructureNotifyMask);   /* Substructure: lhwm's _LHSHELL_START root message */
    debug = getenv("LHSHELL_DEBUG") && *getenv("LHSHELL_DEBUG");
    L = luaL_newstate(); luaL_openlibs(L); scene_custom_paint = tray_paint;
    luaL_newmetatable(L, "lh.node"); lua_pushvalue(L, -1); lua_setfield(L, -2, "__index"); luaL_setfuncs(L, nodemeth, 0); lua_pop(L, 1);
    luaL_newmetatable(L, "lh.nodeh"); lua_pushcfunction(L, l_nodeh_gc); lua_setfield(L, -2, "__gc"); lua_pop(L, 1);
    lua_newtable(L); lua_newtable(L); lua_pushstring(L, "v"); lua_setfield(L, -2, "__mode"); lua_setmetatable(L, -2); lua_rawsetp(L, LUA_REGISTRYINDEX, &nodetab_key);
    scene_node_freed = node_freed;
    luaL_newmetatable(L, "lh.surface"); lua_pushvalue(L, -1); lua_setfield(L, -2, "__index"); luaL_setfuncs(L, surfmeth, 0); lua_pop(L, 1);
    luaL_newlib(L, lhlib); lua_pushstring(L, lua_dir); lua_setfield(L, -2, "lua_dir"); lua_pushstring(L, exe_dir); lua_setfield(L, -2, "bin_dir");
    lua_setglobal(L, "lh");
    if (luaL_dofile(L, script)) { fprintf(stderr, "luxshell: %s\n", lua_tostring(L, -1)); return 1; }
    clients_refresh();
    int xfd = ConnectionNumber(dpy), dfd = notify_start();
    while (running && !terminated) {
        while (XPending(dpy)) { XEvent e; XNextEvent(dpy, &e); handle_event(&e); }
        notify_dispatch();
        run_timers();
        for (struct surface *s = surfaces; s; s = s->next) if (s->dirty) surface_paint(s);
        long long d = next_deadline(); int timeout = d < 0 ? 1000 : (int)(d - now_ms()); if (timeout < 0) timeout = 0;
        /* round trips made by Lua handlers, timers and cairo (XGetWindowProperty, XRender queries) can pull events into
           Xlib's queue; poll() on the socket would then sleep on events already read, up to the next timer (the clock's
           1 s), which made the maximise black layers start late (SHELL-DESIGN M5a status) */
        if (XEventsQueued(dpy, QueuedAlready) > 0 || notify_pending()) timeout = 0;
        struct pollfd p[2] = { { xfd, POLLIN, 0 }, { dfd, POLLIN, 0 } }; poll(p, dfd >= 0 ? 2 : 1, timeout);
    }
    emit("exit", 0);                                                  /* runtime.lua: stops the classic taskbar it started */
    tray_stop();
    XDeleteProperty(dpy, root, atom[A_LHSHELL_CLOCK_VISIBLE]); XSync(dpy, False);   /* M8: nobody shows a sidebar clock any more */
    return 0;
}
