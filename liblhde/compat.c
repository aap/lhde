/* liblhde: the compatibility registry (lhde/data.h): an old-format themes.ini + wallpapers.ini generated from the resolved
   theme files, for flxp until it reads theme files itself (docs/THEME-FORMAT.md section 11). Keys as docs/history/THEMES.md describes
   them, every path absolute. */
#define _GNU_SOURCE
#include "lhde/data.h"
#include "lhde/theme.h"
#include "lhde/settings.h"
#include "internal.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

struct buf { char *s; size_t len, cap; };
static void add(struct buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#include <stdarg.h>
static void add(struct buf *b, const char *fmt, ...)
{
    char tmp[8192]; va_list ap; va_start(ap, fmt); int l = vsnprintf(tmp, sizeof tmp, fmt, ap); va_end(ap);
    if (l < 0) return;
    if ((size_t)l >= sizeof tmp) l = sizeof tmp - 1;
    if (b->len + (size_t)l + 1 > b->cap) { size_t c = (b->cap + (size_t)l + 1) * 2; char *n = realloc(b->s, c); if (!n) return; b->s = n; b->cap = c; }
    memcpy(b->s + b->len, tmp, (size_t)l + 1); b->len += (size_t)l;
}

static const char *val(const lhde_theme *t, const char *k) { const char *v = lhde_theme_get(t, k); return v ? v : ""; }
static int none(const char *v) { return !v[0] || !strcasecmp(v, "none"); }

struct gen { struct buf reg, wp; int n; };
static void one(const char *name, const char *title, const char *file, int group, void *ud)
{
    struct gen *g = ud;
    (void)title; (void)group;
    lhde_theme *t = lhde_theme_resolve(name, NULL, 0);
    if (!t) return;
    struct buf *b = &g->reg;
    add(b, "\n; %s%s\n[%s]\n", file[0] ? "" : "a bare style: ", file[0] ? file : val(t, "path.style"), name);
    add(b, "title = %s\n", val(t, "Theme.name"));
    const char *al = val(t, "Theme.aliases");
    if (al[0]) add(b, "aliases = %s\n", al);
    if (val(t, "path.style")[0]) add(b, "msstyles = %s\n", val(t, "path.style"));
    if (val(t, "VisualStyle.color")[0]) add(b, "color = %s\n", val(t, "VisualStyle.color"));
    if (val(t, "VisualStyle.size")[0]) add(b, "size = %s\n", val(t, "VisualStyle.size"));
    const char *fr = val(t, "path.frames"), *fc = val(t, "path.frames_classic");
    if (fr[0]) add(b, "frame = %s\n", fr);
    else if (fc[0]) add(b, "frame = %s\n", fc);
    add(b, "frame_classic = %s\n", fc[0] ? fc : "style");
    if (val(t, "path.pack")[0]) add(b, "shell = %s\n", val(t, "path.pack"));
    if (val(t, "Shell.startpanel")[0]) add(b, "startpanel = %s\n", val(t, "Shell.startpanel"));
    const char *comp = val(t, "Glass.composition");
    add(b, "composition = %s\n", !strcasecmp(comp, "off") || !fr[0] ? "off" : "auto");
    if (val(t, "path.fonts")[0]) add(b, "fonts = %s\n", val(t, "path.fonts"));
    add(b, "taskbar = %s\n", val(t, "Shell.taskbar"));
    add(b, "start = %s\n", val(t, "Shell.start"));
    if (!none(val(t, "Glass.animations"))) add(b, "animations = %s\n", val(t, "Glass.animations"));
    const char *ic = val(t, "Desktop.icons");
    add(b, "flxp_icons = %s\n", !strncmp(ic, "longhorn", 8) ? "longhorn" : !strcmp(ic, "2k") ? "2k" : !strcmp(ic, "wine") ? "wine" : "xp");
    const char *ch = val(t, "Explorer.chrome"), *nb = val(t, "Explorer.navbar");
    if (!strcmp(ch, "xp") || !strcmp(ch, "longhorn")) add(b, "flxp_frame = %s\n", ch);
    if (!strcmp(ch, "longhorn")) add(b, "flxp_menubar_style = document\n");
    if (!strcmp(nb, "vision")) add(b, "flxp_header_engine = lhnav\n");
    else if (!strcmp(nb, "4051") || !strcmp(nb, "4074")) add(b, "flxp_header_engine = flxp\n");
    const char *np = val(t, "Explorer.navbar_preset");
    if (!strcmp(nb, "vision") && np[0] && strcmp(np, "auto")) add(b, "flxp_header_preset = %s\n", np);
    const char *res = val(t, "path.resources");
    if (res[0] && !strcmp(ch, "longhorn")) add(b, "flxp_longhorn_system32 = %s\n", res);
    else if (!strcmp(ch, "longhorn")) { char o[PATH_MAX]; if (lhde_data_find("resources/4074", o, sizeof o)) add(b, "flxp_longhorn_system32 = %s\n", o); }
    /* [Compat] KEY = VALUE of a theme file: passed on verbatim (an escape hatch for flxp_* keys) */
    for (int i = 0; ; i++) {
        const char *k, *v;
        if (!lhde_theme_item(t, i, &k, &v, NULL)) break;
        if (!strncasecmp(k, "Compat.", 7)) add(b, "%s = %s\n", k + 7, v);
    }
    /* wallpapers.ini */
    const char *wp = val(t, "path.wallpaper"), *wv = val(t, "Desktop.wallpaper");
    if (wp[0] || none(wv)) add(&g->wp, "\n[%s]\nfile = %s\nmode = %s\n", name, wp[0] ? wp : "none", val(t, "Desktop.mode"));
    lhde_theme_free(t);
    g->n++;
}

static void runtime_dir(char *dir, size_t n)
{
    const char *rt = getenv("XDG_RUNTIME_DIR"), *d = getenv("DISPLAY");
    char disp[64] = ""; int j = 0;
    for (const char *c = d ? d : ""; *c && j < 60; c++) disp[j++] = (*c == '/' ? '_' : *c);
    disp[j] = 0;
    if (rt && *rt) snprintf(dir, n, "%s/lhde/%s", rt, disp[0] ? disp : "nodisplay");
    else snprintf(dir, n, "/tmp/lhde-%d/%s", (int)getuid(), disp[0] ? disp : "nodisplay");
}

/* the file's text without its first (generated ...) line, so an unchanged registry is not rewritten */
static int same_body(const char *path, const char *body)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[4096];
    if (!fgets(line, sizeof line, f)) { fclose(f); return 0; }
    size_t bl = strlen(body), off = 0; int same = 1; char c[4096]; size_t k;
    while ((k = fread(c, 1, sizeof c, f)) > 0) { if (off + k > bl || memcmp(body + off, c, k)) { same = 0; break; } off += k; }
    fclose(f);
    return same && off == bl;
}

const char *lhde_compat_registry(char *out, size_t n)
{
    char dir[PATH_MAX], reg[PATH_MAX + 32], wpi[PATH_MAX + 32];
    runtime_dir(dir, sizeof dir);
    if (lhde__mkdirs(dir)) return NULL;
    snprintf(reg, sizeof reg, "%s/themes.ini", dir); snprintf(wpi, sizeof wpi, "%s/wallpapers.ini", dir);
    struct gen g; memset(&g, 0, sizeof g);
    add(&g.reg, "; Read it, do not edit it: every section is a theme of lhde's theme files (data/themes/*.theme, ~/.config/lhde/themes/)\n"
                "; or a bare style of styles/, resolved by liblhde (lhde-theme resolve NAME --why) into the registry format of docs/history/THEMES.md\n"
                "; for the programs that do not read theme files yet (flxp). Paths are absolute. wallpapers.ini beside it.\n");
    char u[PATH_MAX];
    add(&g.wp, "; generated by liblhde with themes.ini beside it\n[default]\nroot = %s/wallpapers\nmode = fill\n", lhde_data_user());
    (void)u;
    lhde_theme_list(one, &g);
    if (!g.reg.s || !g.wp.s) { free(g.reg.s); free(g.wp.s); return NULL; }
    if (!same_body(reg, g.reg.s)) {
        size_t L = g.reg.len + 256; char *all = malloc(L);
        if (all) { snprintf(all, L, "; lhde: generated by liblhde (compat.c) from %d themes; the compatibility registry until flxp reads theme files\n%s", g.n, g.reg.s);
                   lhde__write_file(reg, all); free(all); }
    }
    if (!same_body(wpi, g.wp.s)) {
        size_t L = g.wp.len + 128; char *all = malloc(L);
        if (all) { snprintf(all, L, "; lhde: generated\n%s", g.wp.s); lhde__write_file(wpi, all); free(all); }
    }
    free(g.reg.s); free(g.wp.s);
    if (!lhde__exists(reg)) return NULL;
    snprintf(out, n, "%s", reg);
    return out;
}

const char *lhde_registry_source(char *out, size_t n)
{
    const char *e = getenv("LH_THEMES_INI");
    if (e && *e && lhde__exists(e)) { snprintf(out, n, "%s", e); return out; }
    return lhde_compat_registry(out, n);
}

const char *lhde_registry_resolved(const char *src, char *out, size_t n)
{
    if (!src || !lhde__exists(src)) return NULL;
    FILE *f = fopen(src, "r");
    char line[256] = "";
    if (f) { if (!fgets(line, sizeof line, f)) line[0] = 0; fclose(f); }
    if (!strncmp(line, "; lhde: generated", 17)) return lhde_compat_registry(out, n);
    snprintf(out, n, "%s", src);
    return out;
}
