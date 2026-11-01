/* liblhde: what a visual style is (docs/THEME-FORMAT.md section 5), read through wuxtheme: ut_format, the classes
   CompositedWindow / DWMWindow / Sidebar, the first scheme and size, the family from the VERSIONINFO's OriginalFilename
   (jade.mst, slate.mst, aero.mst, luna.mst; the build number is not there: Longhorn styles say 1.0.0.1), the build from the
   build-qualified name lhde-import gives (jade4074, aero5048). */
#define _GNU_SOURCE
#include "lhde/theme.h"
#include "lhde/data.h"
#include "internal.h"
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#ifndef LHDE_NO_WUXTHEME
#include <wuxtheme/ut.h>
/* weak: a program that links liblhde without libwuxtheme (flxp's toolkit tests) gets no style probe, nothing else changes */
#pragma weak ut_open
#pragma weak ut_close
#pragma weak ut_format
#pragma weak ut_list_classes
#pragma weak ut_color
#pragma weak ut_size
#pragma weak ut_get_resource
#pragma weak ut_prop
#pragma weak ut_get_string
#pragma weak ut_get_bitmap
#pragma weak ut_free
#endif

int lhde__exists(const char *p) { struct stat st; return p && p[0] && stat(p, &st) == 0; }
int lhde__is_dir(const char *p) { struct stat st; return p && p[0] && stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
long long lhde__mtime(const char *p) { struct stat st; return p && stat(p, &st) == 0 ? (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec : -1; }

const char *lhde__build_of(const char *s, char *out, size_t n)
{
    if (n) out[0] = 0;
    for (const char *p = s; p && *p; ) {
        if (!isdigit((unsigned char)*p)) { p++; continue; }
        const char *q = p; while (isdigit((unsigned char)*q)) q++;
        if (q - p == 4) { snprintf(out, n, "%.4s", p); return out; }
        p = q;
    }
    return out;
}

static int ends_with_class(const char *c, const char *want)
{
    if (!strcasecmp(c, want)) return 1;
    const char *d = strstr(c, "::");
    return d && !strcasecmp(d + 2, want);
}

#ifndef LHDE_NO_WUXTHEME
/* VS_VERSIONINFO's OriginalFilename (UTF-16) -> "jade" */
static void family_from_version(ut_theme *t, char *out, size_t n)
{
    const void *data; size_t size;
    if (!ut_get_resource(t, "#16", "#1", &data, &size) || !data) return;
    const unsigned char *d = data;
    static const char key[] = "OriginalFilename";
    size_t kl = sizeof key - 1;
    for (size_t i = 0; i + kl * 2 + 4 < size; i += 2) {
        size_t j = 0;
        while (j < kl && d[i + j * 2] == (unsigned char)key[j] && d[i + j * 2 + 1] == 0) j++;
        if (j < kl) continue;
        size_t p = i + kl * 2;
        while (p + 1 < size && d[p] == 0 && d[p + 1] == 0) p += 2;   /* the terminator and the padding */
        char v[64]; size_t o = 0;
        while (p + 1 < size && (d[p] || d[p + 1]) && o < sizeof v - 1) { v[o++] = (char)tolower(d[p]); p += 2; }
        v[o] = 0;
        char *dot = strchr(v, '.'); if (dot) *dot = 0;
        if (v[0]) snprintf(out, n, "%s", v);
        return;
    }
}

static int probe_ut(const char *path, lhde_style_info *o)
{
    if (!ut_open) return 0;
    ut_theme *t = ut_open(path, NULL, NULL);
    if (!t) return 0;
    o->format = ut_format(t);
    static const char *names[8192];
    int nc = ut_list_classes(t, names, 8192);
    if (nc > 8192) nc = 8192;
    for (int i = 0; i < nc; i++) {
        if (ends_with_class(names[i], "CompositedWindow")) o->composited = 1;
        if (ends_with_class(names[i], "DWMWindow")) o->dwm = 1;
        if (!strcasecmp(names[i], "Sidebar")) o->sidebar = 1;
    }
    const char *c = ut_color(t), *s = ut_size(t);
    snprintf(o->color, sizeof o->color, "%s", c ? c : "");
    snprintf(o->size, sizeof o->size, "%s", s ? s : "");
    family_from_version(t, o->family, sizeof o->family);
    /* broken as packaged: the push button names an image that does not load (not in the file, or undecodable). XP refuses
       such a style ("NAME_BMP missing"); wuxtheme would draw nothing there. (ReactOS's modern.msstyles draws that message on
       XP only because XP's loader cannot read its BITMAPV5 headers; wuxtheme reads them, so it passes here.) */
    if (ut_prop && ut_get_string && ut_get_bitmap && ut_free) {
        int img = ut_prop(t, "IMAGEFILE");
        char name[256];
        if (img && ut_get_string(t, "Button", 1, 0, img, name, sizeof name)) {
            int w = 0, h = 0;
            uint32_t *px = ut_get_bitmap(t, "Button", 1, 0, img, &w, &h);
            if (px) ut_free(px);
            else { o->broken = 1; snprintf(o->broken_why, sizeof o->broken_why, "Button's image %s is not in the file", name); }
        }
    }
    ut_close(t);
    return 1;
}
static int (*probe_fn)(const char *, lhde_style_info *) = probe_ut;
#else
static int (*probe_fn)(const char *, lhde_style_info *) = NULL;
#endif

static int nmemo;
void lhde_style_set_probe(int (*fn)(const char *, lhde_style_info *))
{
    nmemo = 0;
#ifndef LHDE_NO_WUXTHEME
    probe_fn = fn ? fn : probe_ut;
#else
    probe_fn = fn;
#endif
}

/* styles/NAME.msstyles, else the first *.msstyles in styles/NAME/ (NAME.msstyles there first), in data directory order */
const char *lhde_style_path(const char *name, char *out, size_t n)
{
    if (n) out[0] = 0;
    if (!name || !name[0] || !strcasecmp(name, "classic") || !strcasecmp(name, "none")) return out;
    if (name[0] == '/' || (name[0] == '~' && name[1] == '/')) {
        char p[PATH_MAX]; lhde_data_resolve(name, p, sizeof p);
        if (lhde__exists(p)) snprintf(out, n, "%s", p);
        return out;
    }
    if (strchr(name, '/')) return out;
    char rel[512];
    snprintf(rel, sizeof rel, "styles/%s.msstyles", name);
    const char *dirs[LHDE_DATA_MAX]; int nd = lhde_data_dirs(dirs, LHDE_DATA_MAX);
    for (int i = 0; i < nd; i++) {
        char p[PATH_MAX + 600];
        snprintf(p, sizeof p, "%s/%s", dirs[i], rel);
        if (lhde__exists(p) && !lhde__is_dir(p)) { snprintf(out, n, "%s", p); return out; }
        snprintf(p, sizeof p, "%s/styles/%s", dirs[i], name);
        DIR *d = opendir(p);
        if (!d) continue;
        char best[256] = ""; struct dirent *e;
        while ((e = readdir(d))) {
            size_t l = strlen(e->d_name);
            if (l < 10 || strcasecmp(e->d_name + l - 9, ".msstyles")) continue;
            int own = !strncasecmp(e->d_name, name, l - 9) && strlen(name) == l - 9;
            if (!best[0] || own || (strcmp(e->d_name, best) < 0 && strncasecmp(best, name, strlen(name)))) snprintf(best, sizeof best, "%s", e->d_name);
            if (own) break;
        }
        closedir(d);
        if (best[0]) { snprintf(out, n, "%s/%s", p, best); return out; }
    }
    return out;
}

static struct { char path[1024]; long long mtime; lhde_style_info info; } memo[64];

int lhde_style_probe(const char *name, lhde_style_info *o)
{
    memset(o, 0, sizeof *o);
    if (!name || !name[0]) { o->kind = LHDE_STYLE_MISSING; return 0; }
    if (!strcasecmp(name, "classic") || !strcasecmp(name, "none")) { o->kind = LHDE_STYLE_CLASSIC; snprintf(o->build, sizeof o->build, "xp"); return 1; }
    lhde_style_path(name, o->path, sizeof o->path);
    if (!o->path[0]) { o->kind = LHDE_STYLE_MISSING; return 0; }
    long long mt = lhde__mtime(o->path);
    for (int i = 0; i < nmemo; i++)
        if (!strcmp(memo[i].path, o->path) && memo[i].mtime == mt) { *o = memo[i].info; return o->kind != LHDE_STYLE_MISSING; }
    char path[1024]; snprintf(path, sizeof path, "%s", o->path);
    int ok = probe_fn && probe_fn(path, o);
    snprintf(o->path, sizeof o->path, "%s", path);
    /* the name the style goes by: the theme value, else the file (styles/jade4074.msstyles, styles/jade4074-xp/jade.msstyles) */
    char base[256];
    { const char *b = strchr(name, '/') ? strrchr(name, '/') + 1 : name; snprintf(base, sizeof base, "%s", b);
      char *dot = strstr(base, ".msstyles"); if (dot) *dot = 0; }
    if (!o->family[0]) {
        size_t k = 0; while (base[k] && isalpha((unsigned char)base[k]) && k < sizeof o->family - 1) { o->family[k] = (char)tolower((unsigned char)base[k]); k++; }
        o->family[k] = 0;
    }
    if (!ok) o->kind = LHDE_STYLE_MISSING;
    else if (o->format == 3) { o->kind = LHDE_STYLE_XP; snprintf(o->build, sizeof o->build, "xp"); }
    else {
        lhde__build_of(base, o->build, sizeof o->build);
        if (!o->build[0] && strchr(name, '/')) lhde__build_of(path, o->build, sizeof o->build);
        /* a style named without a build (an Aero patch dropped in as twiwmtb-aero) with CompositedWindow: 4074's, the one build
           whose styles have that class (4051's and 5048's have none) */
        if (!o->build[0] && o->composited) snprintf(o->build, sizeof o->build, "4074");
        int b = atoi(o->build);
        if (o->dwm || (!o->composited && b >= 5000)) o->kind = LHDE_STYLE_DWM;
        else o->kind = LHDE_STYLE_LONGHORN;
    }
    if (nmemo < 64) { snprintf(memo[nmemo].path, sizeof memo[nmemo].path, "%s", path); memo[nmemo].mtime = mt; memo[nmemo].info = *o; nmemo++; }
    return o->kind != LHDE_STYLE_MISSING;
}
