/* luxshell popup menus: wuxtheme's popup menu painter (ut_menu_layout / ut_menu_draw, the code that draws flxp's user32 menus and
   luxwm's window menu) for menu.lua. luxwm/menu.c is the model: the session theme's visual style (its Menu class, [SysMetrics]
   colours, FLATMENUS, MENUFONT), or Windows Classic from the record's Colors.* and Metrics.menufont; the text smoothed as the
   system's ([fonts] smoothing, else the theme's Fonts.smoothing). The pixels go into an image registered as an "@..." key (an
   image node's source), with XP's menu drop shadow added right and below; menu.lua keeps the input and the placement.

   lh.menu_render(items, sel, opts) -> { image = KEY, w =, h = (the menu, without the shadow), shadow = 4, pad =,
                                          rows = { { y =, h = }, ... } (each item's top and height in the menu) }, or nil
     items: { { text = "&Move", accel = "Alt+F4", separator =, disabled =, checked =, radio =, default = (bold), submenu = (any
              value: the cascade arrow) }, ... } (at most 64)
     sel:   the selected item (1-based), nil for none
     opts:  { key = "@menu:1" (the image key; required), cues = bool (underline the mnemonics), theme = the resolved record's
              values (lh.theme_resolve(...).v: path.style, VisualStyle.color / size, style.kind, Colors.*, Metrics.menufont,
              Fonts.smoothing), under = { x, y } | true (no compositor: the shadow is the screen under it darkened, as XP's
              and luxwm's: {x, y} copies the root at the menu's place now, before it is mapped, and keeps the copy under the
              key; true uses the kept copy (a repaint); nil: a translucent shadow for the compositor) } */
#define _GNU_SOURCE   /* strcasestr */
#include "luxshell.h"
#include <wuxtheme/ut.h>
#include <lhde/theme.h>
#include <lhde/settings.h>
#include <lua5.4/lauxlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <cairo/cairo-xlib.h>
#include <dirent.h>
#include <lhde/data.h>

#define MAXITEMS 64
#define SHADOW 4   /* XP's menu drop shadow, right and bottom (luxwm/menu.c SHADOW: measured on the 4074 Slate window menu) */

/* the drop shadow right of and below the W x H menu, alpha per pixel: luxwm/menu.c shadow_alpha, the same 4074 Slate measurement
   (0.44 0.33 0.17 0.055 outwards, starting 5 px in from the far corner with a 4 px ramp) */
static double shadow_alpha(int x, int y, int W, int H) {
    static const double p[5] = { 0, 0.44, 0.33, 0.17, 0.055 }, q[5] = { 0.12, 0.33, 0.65, 0.88, 1 };
    int dx = x - W + 1, dy = y - H + 1;
    if (dx >= 1 && dy >= 1) return dx <= SHADOW && dy <= SHADOW ? p[dx] * p[dy] / p[1] : 0;
    if (dx >= 1 && dx <= SHADOW) { int k = y - 5; return k < 0 ? 0 : p[dx] * (k < 5 ? q[k] : 1); }
    if (dy >= 1 && dy <= SHADOW) { int k = x - 5; return k < 0 ? 0 : p[dy] * (k < 5 ? q[k] : 1); }
    return 0;
}

/* the check marks, radio dots and arrows the painter draws from Marlett where the style has no part for them (Windows Classic,
   XP's styles): the first imported fonts/<build>/marlett.ttf, registered as a single file with libwuxtheme's text; ReactOS's free
   one (fonts/reactos) only when no Windows build has one (luxwm/frame.c cl_marlett, the same rule) */
static void marlett(void) {
    static int done; if (done) return; done = 1;
    const char *dirs[16]; int n = lhde_data_dirs(dirs, 16);
    char free_[4700] = "";
    for (int i = 0; i < n; i++) {
        char fd[4096]; snprintf(fd, sizeof fd, "%s/fonts", dirs[i]);
        DIR *d = opendir(fd); if (!d) continue;
        struct dirent *e; char found[4700] = "";
        while (!*found && (e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            int reactos = !strcmp(e->d_name, "reactos");
            if (reactos && *free_) continue;
            char sub[4400]; snprintf(sub, sizeof sub, "%s/%s", fd, e->d_name);
            DIR *b = opendir(sub); if (!b) continue;
            struct dirent *f;
            while ((f = readdir(b))) if (!strcasecmp(f->d_name, "marlett.ttf")) { snprintf(reactos ? free_ : found, sizeof found, "%s/%s", sub, f->d_name); break; }
            closedir(b);
        }
        closedir(d);
        if (*found) { ut_add_font_dir(found); return; }
    }
    if (*free_) ut_add_font_dir(free_);
}

/* the screen under each key's menu (opts.under) */
static struct under { char key[64]; cairo_surface_t *s; } unders[8];
static cairo_surface_t *under_get(const char *key, int capture, int x, int y, int W, int H) {
    struct under *u = NULL;
    for (int i = 0; i < 8 && !u; i++) if (!strcmp(unders[i].key, key)) u = &unders[i];
    for (int i = 0; i < 8 && !u; i++) if (!unders[i].key[0]) { u = &unders[i]; snprintf(u->key, sizeof u->key, "%s", key); }
    if (!u) return NULL;
    if (capture) {
        if (u->s) cairo_surface_destroy(u->s);
        cairo_surface_t *rs = cairo_xlib_surface_create(dpy, root, DefaultVisual(dpy, screen), DisplayWidth(dpy, screen), DisplayHeight(dpy, screen));
        u->s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H); cairo_t *cr = cairo_create(u->s);
        cairo_set_source_rgb(cr, 0, 0, 0); cairo_paint(cr);
        cairo_set_source_surface(cr, rs, -x, -y); cairo_paint(cr); cairo_destroy(cr); cairo_surface_destroy(rs);
        cairo_surface_flush(u->s);
    }
    if (u->s && (cairo_image_surface_get_width(u->s) != W || cairo_image_surface_get_height(u->s) != H)) return NULL;
    return u->s;
}

/* the open style, kept while the theme's style / colour / size stay the same */
static ut_theme *style; static char style_key[4400];

static const char *tget(lua_State *L, int t, const char *k) {
    if (!t) return "";
    lua_getfield(L, t, k); const char *v = lua_tostring(L, -1); lua_pop(L, 1);   /* the record table keeps the string alive */
    return v ? v : "";
}
static int smoothing_of(const char *v) {
    return !strcasecmp(v, "cleartype") ? 2 : !strcasecmp(v, "off") || !strcasecmp(v, "none") || !strcmp(v, "0") ? 0 : 1;
}

int l_menu_render(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    marlett();
    int sel = lua_isinteger(L, 2) ? (int)lua_tointeger(L, 2) - 1 : -1;
    luaL_checktype(L, 3, LUA_TTABLE);
    lua_getfield(L, 3, "key"); const char *key = luaL_checkstring(L, -1); char keyb[64]; snprintf(keyb, sizeof keyb, "%s", key); lua_pop(L, 1);
    lua_getfield(L, 3, "cues"); int cues = lua_toboolean(L, -1); lua_pop(L, 1);
    lua_getfield(L, 3, "theme"); int th = lua_istable(L, -1) ? lua_gettop(L) : 0;

    ut_menu_spec spec; memset(&spec, 0, sizeof spec);
    ut_font font; memset(&font, 0, sizeof font);
    uint32_t colors[UT_SYSCOLORS];
    const char *path = tget(L, th, "path.style");
    int classic = !strcmp(tget(L, th, "style.kind"), "classic") || !*path;
    if (classic) {   /* Windows Classic: the scheme's colours (Colors.* "R G B") over Windows Standard, Metrics.menufont */
        static const uint32_t standard[UT_SYSCOLORS] = {
            0xd4d0c8, 0x3a6ea5, 0x0a246a, 0x808080, 0xd4d0c8, 0xffffff, 0x000000, 0x000000, 0x000000, 0xffffff, 0xd4d0c8, 0xd4d0c8,
            0x808080, 0x0a246a, 0xffffff, 0xd4d0c8, 0x808080, 0x808080, 0x000000, 0xd4d0c8, 0xffffff, 0x404040, 0xd4d0c8, 0x000000,
            0xffffe1, 0xb5b5b5, 0x000080, 0xa6caf0, 0xc0c0c0, 0x0a246a, 0xd4d0c8 };
        memcpy(colors, standard, sizeof colors);
        for (int i = 0; i < UT_SYSCOLORS; i++) {
            const char *nm = lhde_color_name(i); char k[64]; int r, g, b;
            if (!nm) continue;
            snprintf(k, sizeof k, "Colors.%s", nm);
            if (sscanf(tget(L, th, k), "%d %d %d", &r, &g, &b) == 3) colors[i] = (uint32_t)((r & 255) << 16 | (g & 255) << 8 | (b & 255));
        }
        spec.colors = colors; spec.flat = 0;
        /* "Face, size[, bold][, italic]" (size: points, negative: pixels; luxwm/frame.c cl_font_lf) */
        const char *mf = tget(L, th, "Metrics.menufont"), *c = strchr(mf, ',');
        int size = 0;
        if (c && sscanf(c + 1, "%d", &size) == 1 && size) {
            int n = (int)(c - mf); while (n && mf[n - 1] == ' ') n--;
            const char *f = mf; while (*f == ' ' && n) { f++; n--; }
            snprintf(font.face, sizeof font.face, "%.*s", n, f);
            font.height = -(size < 0 ? -size : (size * 96 + 36) / 72);
            font.weight = strcasestr(c, "bold") ? 700 : 400; font.italic = strcasestr(c, "italic") != NULL;
        } else { snprintf(font.face, sizeof font.face, "Tahoma"); font.height = -11; font.weight = 400; }
        spec.font = &font; spec.smoothing = 1;
        if (style) { ut_close(style); style = NULL; style_key[0] = 0; }
    } else {
        char k[sizeof style_key];
        const char *color = tget(L, th, "VisualStyle.color"), *sz = tget(L, th, "VisualStyle.size");
        snprintf(k, sizeof k, "%.4000s|%.150s|%.150s", path, color, sz);
        if (strcmp(k, style_key)) {
            if (style) ut_close(style);
            style = ut_open(path, *color ? color : NULL, *sz ? sz : NULL);
            snprintf(style_key, sizeof style_key, "%s", k);
            if (!style) fprintf(stderr, "luxshell: menu: cannot open the visual style %s (Windows Standard menus)\n", path);
        }
        spec.theme = style; spec.flat = -1;
        spec.smoothing = style && ut_format(style) == 4 ? 2 : 1;   /* luxwm's default for a Longhorn style's text: ClearType */
    }
    { const char *ts = tget(L, th, "Fonts.smoothing"); if (*ts) spec.smoothing = smoothing_of(ts); }
    { char b[32]; const char *v = lhde_settings_get("fonts", "smoothing", b, sizeof b); if (v && *v) spec.smoothing = smoothing_of(v); }

    /* the items; their strings stay referenced by the items table on the stack */
    ut_menu_item it[MAXITEMS]; memset(it, 0, sizeof it);
    int n = (int)lua_rawlen(L, 1); if (n > MAXITEMS) n = MAXITEMS;
    for (int i = 0; i < n; i++) {
        lua_rawgeti(L, 1, i + 1);
        if (lua_istable(L, -1)) {
            int t = lua_gettop(L);
            #define FLAG(name, f) do { lua_getfield(L, t, name); if (lua_toboolean(L, -1)) it[i].flags |= (f); lua_pop(L, 1); } while (0)
            FLAG("separator", UT_MENU_SEPARATOR); FLAG("disabled", UT_MENU_DISABLED); FLAG("checked", UT_MENU_CHECKED);
            FLAG("default", UT_MENU_DEFAULT); FLAG("submenu", UT_MENU_SUBMENU);
            #undef FLAG
            lua_getfield(L, t, "radio"); if (lua_toboolean(L, -1) && (it[i].flags & UT_MENU_CHECKED)) it[i].flags |= UT_MENU_RADIO; lua_pop(L, 1);
            lua_getfield(L, t, "text"); it[i].text = lua_tostring(L, -1); lua_pop(L, 1);
            lua_getfield(L, t, "accel"); it[i].accel = lua_tostring(L, -1); lua_pop(L, 1);
            if (!it[i].text) it[i].text = "";
        }
        lua_pop(L, 1);
    }

    int w = 0, h = 0, pad = 0, iy[MAXITEMS], ih[MAXITEMS];
    if (!ut_menu_layout(&spec, it, n, &w, &h, &pad, iy, ih) || w <= 0 || h <= 0) { lua_pushnil(L); return 1; }
    int W = w + SHADOW, H = h + SHADOW;
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) { cairo_surface_destroy(s); lua_pushnil(L); return 1; }
    cairo_surface_flush(s);
    cairo_surface_t *under = NULL;
    lua_getfield(L, 3, "under");
    if (lua_istable(L, -1)) {
        lua_rawgeti(L, -1, 1); lua_rawgeti(L, -2, 2);
        under = under_get(keyb, 1, (int)lua_tointeger(L, -2), (int)lua_tointeger(L, -1), W, H); lua_pop(L, 2);
    } else if (lua_toboolean(L, -1)) under = under_get(keyb, 0, 0, 0, W, H);
    lua_pop(L, 1);
    const unsigned char *ud = under ? cairo_image_surface_get_data(under) : NULL; int ust = under ? cairo_image_surface_get_stride(under) : 0;
    unsigned char *d = cairo_image_surface_get_data(s); int st = cairo_image_surface_get_stride(s);
    ut_menu_draw(&spec, it, n, sel >= 0 && sel < n ? sel : -1, cues, (uint32_t *)d, w, h, st);
    for (int y = 0; y < H; y++) {
        uint32_t *row = (uint32_t *)(d + y * st);
        for (int x = 0; x < W; x++) {
            if (x < w && y < h) { row[x] |= 0xff000000u; continue; }   /* the painter draws an opaque menu */
            double a = shadow_alpha(x, y, w, h);
            if (ud) {   /* the screen under it, darkened: opaque */
                uint32_t p = ((const uint32_t *)(ud + y * ust))[x], o = 0xff000000u;
                for (int sh = 0; sh < 24; sh += 8) o |= (uint32_t)(((p >> sh) & 255) * (1 - a) + 0.5) << sh;
                row[x] = o;
            } else row[x] = (uint32_t)(a * 255 + 0.5) << 24;   /* black, premultiplied, for the compositor */
        }
    }
    cairo_surface_mark_dirty(s);
    icon_register(keyb, s);   /* replaces (and frees) the previous image under the key: the caller sets its node's image at once */

    lua_createtable(L, 0, 6);
    lua_pushstring(L, keyb); lua_setfield(L, -2, "image");
    lua_pushinteger(L, w); lua_setfield(L, -2, "w"); lua_pushinteger(L, h); lua_setfield(L, -2, "h");
    lua_pushinteger(L, SHADOW); lua_setfield(L, -2, "shadow"); lua_pushinteger(L, pad); lua_setfield(L, -2, "pad");
    lua_createtable(L, n, 0);
    for (int i = 0; i < n; i++) {
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, iy[i]); lua_setfield(L, -2, "y"); lua_pushinteger(L, ih[i]); lua_setfield(L, -2, "h");
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "rows");
    return 1;
}
