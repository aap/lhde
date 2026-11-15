/* liblhde: the icon of a window or a program (lhde/icon.h, docs/THEME-FORMAT.md section 9): the class map icons/apps.ini,
   the icon set recipes icons/NAME.theme read by key, the desktop files and the XDG icon theme; icons read from PE resources
   (RT_GROUP_ICON / RT_ICON), .ico and .png files. */
#define _GNU_SOURCE
#include "lhde/icon.h"
#include "lhde/data.h"
#include "lhde/settings.h"
#include "lhde/theme.h"
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <limits.h>
#include <png.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int exists(const char *p) { struct stat st; return p && p[0] && stat(p, &st) == 0 && S_ISREG(st.st_mode); }
static int is_dir(const char *p) { struct stat st; return p && p[0] && stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
/* a + sep + b into out, cut at n (paths: snprintf's truncation warnings say nothing here) */
static void join(char *out, size_t n, const char *a, const char *sep, const char *b)
{
    size_t la = strlen(a), ls = strlen(sep), lb = strlen(b);
    if (!n) return;
    if (la >= n) la = n - 1;
    memmove(out, a, la);
    if (ls > n - 1 - la) ls = n - 1 - la;
    memcpy(out + la, sep, ls);
    if (lb > n - 1 - la - ls) lb = n - 1 - la - ls;
    memcpy(out + la + ls, b, lb); out[la + ls + lb] = 0;
}
static void say(char *why, size_t n, const char *fmt, const char *a, const char *b, const char *c)
{
    if (why && n) snprintf(why, n, fmt, a ? a : "", b ? b : "", c ? c : "");
}

/* ------------------------------------------------------------------ images: { w, h, ARGB... } */
static unsigned long *img_new(unsigned w, unsigned h, unsigned long *n)
{
    unsigned long *o = calloc(2 + (size_t)w * h, sizeof *o);
    if (!o) return NULL;
    o[0] = w; o[1] = h; *n = 2 + (unsigned long)w * h;
    return o;
}

static unsigned long *png_decode(const void *mem, size_t len, const char *file, unsigned long *n)
{
    png_image im; memset(&im, 0, sizeof im); im.version = PNG_IMAGE_VERSION;
    if (!(file ? png_image_begin_read_from_file(&im, file) : png_image_begin_read_from_memory(&im, mem, len))) return NULL;
    im.format = PNG_FORMAT_BGRA;   /* little-endian uint32 0xAARRGGBB, straight alpha */
    if (im.width == 0 || im.height == 0 || im.width > 1024 || im.height > 1024) { png_image_free(&im); return NULL; }
    uint32_t *px = malloc(PNG_IMAGE_SIZE(im));
    if (!px || !png_image_finish_read(&im, NULL, px, 0, NULL)) { free(px); png_image_free(&im); return NULL; }
    unsigned long *o = img_new(im.width, im.height, n);
    if (o) for (size_t i = 0; i < (size_t)im.width * im.height; i++) o[2 + i] = px[i];
    free(px);
    return o;
}

static unsigned u16(const unsigned char *p) { return p[0] | p[1] << 8; }
static uint32_t u32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* one icon image: a PNG, or a DIB (BITMAPINFOHEADER, height doubled: the XOR image then the AND mask, rows bottom-up) of
   1, 4, 8, 24 or 32 bits; 32-bit images without any alpha take the mask */
static unsigned long *dib_decode(const unsigned char *p, size_t len, unsigned long *n)
{
    if (len >= 8 && !memcmp(p, "\x89PNG", 4)) return png_decode(p, len, NULL, n);
    if (len < 40) return NULL;
    uint32_t hs = u32(p); int w = (int)u32(p + 4), h = (int)u32(p + 8) / 2; unsigned bpp = u16(p + 14); uint32_t used = u32(p + 32);
    if (hs < 40 || w <= 0 || h <= 0 || w > 512 || h > 512) return NULL;
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) return NULL;
    size_t ncol = bpp <= 8 ? (used ? used : 1u << bpp) : 0;
    const unsigned char *pal = p + hs, *xor_ = pal + ncol * 4;
    size_t xs = ((size_t)w * bpp + 31) / 32 * 4, as = ((size_t)w + 31) / 32 * 4;
    const unsigned char *and_ = xor_ + xs * h;
    if ((size_t)(xor_ - p) + xs * h > len) return NULL;
    int has_mask = (size_t)(and_ - p) + as * h <= len;
    unsigned long *o = img_new(w, h, n);
    if (!o) return NULL;
    int any_alpha = 0;
    for (int y = 0; y < h; y++) {
        const unsigned char *row = xor_ + xs * (h - 1 - y);
        for (int x = 0; x < w; x++) {
            uint32_t c;
            if (bpp == 32) { c = u32(row + x * 4); if (c >> 24) any_alpha = 1; }
            else if (bpp == 24) c = 0xff000000u | row[x * 3 + 2] << 16 | row[x * 3 + 1] << 8 | row[x * 3];
            else {
                unsigned i = bpp == 8 ? row[x] : bpp == 4 ? (row[x / 2] >> (x & 1 ? 0 : 4)) & 15 : (row[x / 8] >> (7 - (x & 7))) & 1;
                c = i < ncol ? 0xff000000u | pal[i * 4 + 2] << 16 | pal[i * 4 + 1] << 8 | pal[i * 4] : 0xff000000u;
            }
            o[2 + (size_t)y * w + x] = c;
        }
    }
    if (bpp != 32 || !any_alpha)
        for (int y = 0; y < h; y++) {
            const unsigned char *row = and_ + as * (h - 1 - y);
            for (int x = 0; x < w; x++) {
                int transparent = has_mask && (row[x / 8] >> (7 - (x & 7))) & 1;
                unsigned long *c = &o[2 + (size_t)y * w + x];
                *c = transparent ? 0 : (*c | 0xff000000u);
            }
        }
    return o;
}

/* the entry of an icon directory (GRPICONDIR in a PE: 14-byte entries ending in the RT_ICON id; ICONDIR in a .ico: 16-byte
   ones ending in the file offset) nearest at or above want, else the largest; of one size the most colours (at most depth
   when depth is under 32: the Windows 2000 look takes the 16-colour images) */
static int pick_entry(const unsigned char *dir, size_t len, int esz, int want, int depth)
{
    if (len < 6) return -1;
    int count = (int)u16(dir + 4), best = -1, bs = 0, bb = 0;
    for (int i = 0; i < count && 6 + (size_t)(i + 1) * esz <= len; i++) {
        const unsigned char *e = dir + 6 + (size_t)i * esz;
        int s = e[0] ? e[0] : 256, b = u16(e + 6);
        if (!b) b = e[2] == 2 ? 1 : e[2] == 16 ? 4 : 8;
        int bscore = depth < 32 && b > depth ? -b : b;   /* over the depth: least wanted */
        int better;
        if (best < 0) better = 1;
        else if ((s >= want) != (bs >= want)) better = s >= want;
        else if (s != bs) better = s >= want ? s < bs : s > bs;
        else better = bscore > bb;
        if (better) { best = i; bs = s; bb = bscore; }
    }
    return best;
}

/* ------------------------------------------------------------------ PE resources */
struct pe { char path[PATH_MAX]; const unsigned char *d; size_t n; size_t rsrc; uint32_t rva; int nsec; const unsigned char *secs; };
static struct pe pes[16]; static int npes;

static size_t rva_off(const struct pe *pe, uint32_t rva)
{
    for (int i = 0; i < pe->nsec; i++) {
        const unsigned char *s = pe->secs + i * 40;
        uint32_t va = u32(s + 12), vs = u32(s + 8), raw = u32(s + 20), rs = u32(s + 16);
        uint32_t span = vs > rs ? vs : rs;
        if (rva >= va && rva < va + span) return raw + (rva - va);
    }
    return (size_t)-1;
}

/* a PE file mapped once per process (shell32.dll is read for most keys) */
static const struct pe *pe_open(const char *path)
{
    for (int i = 0; i < npes; i++) if (!strcmp(pes[i].path, path)) return pes[i].d ? &pes[i] : NULL;
    struct pe *pe = npes < 16 ? &pes[npes++] : NULL;
    if (!pe) return NULL;
    memset(pe, 0, sizeof *pe); snprintf(pe->path, sizeof pe->path, "%s", path);
    int fd = open(path, O_RDONLY | O_CLOEXEC); struct stat st;
    if (fd < 0) return NULL;
    if (fstat(fd, &st) || st.st_size < 64) { close(fd); return NULL; }
    void *m = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0); close(fd);
    if (m == MAP_FAILED) return NULL;
    const unsigned char *d = m; size_t n = (size_t)st.st_size;
    uint32_t lf = u32(d + 60);
    if (d[0] != 'M' || d[1] != 'Z' || (size_t)lf + 24 > n || memcmp(d + lf, "PE\0\0", 4)) { munmap(m, n); return NULL; }
    unsigned nsec = u16(d + lf + 6), opt = u16(d + lf + 20);
    const unsigned char *oh = d + lf + 24;
    if ((size_t)lf + 24 + opt + nsec * 40 > n) { munmap(m, n); return NULL; }
    int plus = u16(oh) == 0x20b;
    size_t dd = plus ? 112 : 96;   /* the data directories; [2] is the resources */
    if (opt < dd + 3 * 8) { munmap(m, n); return NULL; }
    pe->d = d; pe->n = n; pe->nsec = (int)nsec; pe->secs = oh + opt;
    pe->rva = u32(oh + dd + 16);
    pe->rsrc = pe->rva ? rva_off(pe, pe->rva) : (size_t)-1;
    if (pe->rsrc == (size_t)-1 || pe->rsrc + 16 > n) { pe->d = NULL; munmap(m, n); return NULL; }
    return pe;
}

/* the subdirectory / data entry of a resource directory for an id, or (name != NULL) a string name matched without case */
static int rdir_find(const struct pe *pe, size_t dir, unsigned id, const char *name, uint32_t *off)
{
    if (dir + 16 > pe->n) return 0;
    const unsigned char *p = pe->d + dir;
    unsigned nn = u16(p + 12), ni = u16(p + 14);
    for (unsigned i = 0; i < nn + ni; i++) {
        const unsigned char *e = p + 16 + i * 8;
        if ((size_t)(e - pe->d) + 8 > pe->n) return 0;
        uint32_t nm = u32(e), to = u32(e + 4);
        int match;
        if (nm & 0x80000000u) {
            if (!name) continue;
            size_t so = pe->rsrc + (nm & 0x7fffffffu);
            if (so + 2 > pe->n) continue;
            unsigned len = u16(pe->d + so); match = strlen(name) == len && so + 2 + len * 2 <= pe->n;
            for (unsigned k = 0; match && k < len; k++) { unsigned c = u16(pe->d + so + 2 + k * 2); match = c < 128 && tolower((int)c) == tolower((unsigned char)name[k]); }
        } else match = !name && (id == 0 || nm == id);   /* id 0: the first (the language level) */
        if (match) { *off = to; return 1; }
    }
    return 0;
}
static const unsigned char *pe_resource(const struct pe *pe, unsigned type, unsigned id, const char *name, size_t *len)
{
    uint32_t o;
    if (!rdir_find(pe, pe->rsrc, type, NULL, &o) || !(o & 0x80000000u)) return NULL;
    if (!rdir_find(pe, pe->rsrc + (o & 0x7fffffffu), id, name, &o) || !(o & 0x80000000u)) return NULL;
    if (!rdir_find(pe, pe->rsrc + (o & 0x7fffffffu), 0, NULL, &o) || (o & 0x80000000u)) return NULL;
    size_t de = pe->rsrc + o;
    if (de + 8 > pe->n) return NULL;
    size_t off = rva_off(pe, u32(pe->d + de)), sz = u32(pe->d + de + 4);
    if (off == (size_t)-1 || off + sz > pe->n) return NULL;
    *len = sz;
    return pe->d + off;
}

/* pe:PATH#ID (a number, or a resource name: cmd.exe's IDI_APPICON) */
static unsigned long *pe_icon(const char *path, const char *id, int want, int depth, unsigned long *n)
{
    const struct pe *pe = pe_open(path);
    if (!pe) return NULL;
    int num = id[0] && strspn(id, "0123456789") == strlen(id);
    size_t gl; const unsigned char *g = pe_resource(pe, 14, num ? (unsigned)atoi(id) : 0, num ? NULL : id, &gl);
    if (!g) return NULL;
    int e = pick_entry(g, gl, 14, want, depth);
    if (e < 0) return NULL;
    size_t il; const unsigned char *im = pe_resource(pe, 3, u16(g + 6 + e * 14 + 12), NULL, &il);
    return im ? dib_decode(im, il, n) : NULL;
}

static unsigned long *file_icon(const char *path, int want, int depth, unsigned long *n)
{
    size_t l = strlen(path);
    if (l > 4 && !strcasecmp(path + l - 4, ".png")) return png_decode(NULL, 0, path, n);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    unsigned char *d = malloc(1 << 20); size_t len = d ? fread(d, 1, 1 << 20, f) : 0; fclose(f);
    unsigned long *o = NULL;
    if (len >= 6 && u16(d + 2) == 1) {   /* ICONDIR */
        int e = pick_entry(d, len, 16, want, depth);
        if (e >= 0) {
            const unsigned char *en = d + 6 + e * 16; uint32_t sz = u32(en + 8), off = u32(en + 12);
            if ((size_t)off + sz <= len) o = dib_decode(d + off, sz, n);
        }
    }
    free(d);
    return o;
}

/* ------------------------------------------------------------------ the icon set recipes */
struct recipe { char name[64], path[PATH_MAX], base[PATH_MAX]; int depth, usable; lhde_ini *ini; };
static struct recipe recipes[16]; static int nrecipes;
static char chosen[PATH_MAX];      /* lhde_app_icons_set's */
static int have_current; static char current[PATH_MAX];

static const char *wine_recipe(char *out, size_t n)
{
    if (lhde_data_find("res/icons/wine.theme", out, n)) return out;
    const char *t = lhde_tree();
    if (t) { snprintf(out, n, "%s/flxp/res/icons/wine.theme", t); if (exists(out)) return out; }
    return NULL;
}

/* NAME or a path -> the recipe, loaded once */
static struct recipe *recipe_get(const char *name_or_path)
{
    char path[PATH_MAX], rel[300], nm[64];
    if (strchr(name_or_path, '/')) {
        snprintf(path, sizeof path, "%s", name_or_path);
        const char *b = strrchr(path, '/') + 1; snprintf(nm, sizeof nm, "%.*s", (int)strcspn(b, "."), b);
    } else {
        snprintf(nm, sizeof nm, "%s", name_or_path);
        snprintf(rel, sizeof rel, "icons/%s.theme", nm);
        if (!lhde_data_find(rel, path, sizeof path) && !(!strcmp(nm, "wine") && wine_recipe(path, sizeof path))) return NULL;
    }
    for (int i = 0; i < nrecipes; i++) if (!strcmp(recipes[i].path, path)) return &recipes[i];
    if (nrecipes == 16 || !exists(path)) return NULL;
    struct recipe *r = &recipes[nrecipes++]; memset(r, 0, sizeof *r);
    snprintf(r->name, sizeof r->name, "%s", nm); snprintf(r->path, sizeof r->path, "%s", path);
    r->ini = lhde_ini_load(path);
    char v[PATH_MAX];
    r->depth = lhde_ini_get(r->ini, "theme", "depth", v, sizeof v) ? atoi(v) : 32;
    if (r->depth <= 0) r->depth = 32;
    r->usable = 1;
    if (lhde_ini_get(r->ini, "theme", "resources", v, sizeof v) && v[0]) {
        if (!lhde_data_find(v, r->base, sizeof r->base)) r->usable = 0;   /* not imported */
    } else if (lhde_ini_get(r->ini, "theme", "base", v, sizeof v) && v[0]) {
        if (v[0] == '/') snprintf(r->base, sizeof r->base, "%s", v);
        else if (v[0] == '~') snprintf(r->base, sizeof r->base, "%s%s", getenv("HOME") ? getenv("HOME") : "", v + 1);
        else { snprintf(r->base, sizeof r->base, "%s", path); *strrchr(r->base, '/') = 0; size_t l = strlen(r->base); snprintf(r->base + l, sizeof r->base - l, "/%s", v); }
    } else { snprintf(r->base, sizeof r->base, "%s", path); *strrchr(r->base, '/') = 0; }   /* Wine's: the .ico beside it */
    return r;
}

/* a recipe's source for a key as a file: "pe:REL#ID" (REL: absolute, resources/BUILD/FILE of any imported build, else under
   the recipe's base), file:PACK/PATH (an imported icon pack), else a file path as REL. 1 with the file and id when it exists */
static int recipe_source(const struct recipe *r, const char *key, char *file, size_t nf, char *id, size_t nid, char *src, size_t ns)
{
    char v[PATH_MAX];
    if (!r->usable || !lhde_ini_get(r->ini, "icons", key, v, sizeof v) || !v[0]) return 0;
    snprintf(src, ns, "%s", v);
    char *s = v; id[0] = 0;
    if (!strncmp(s, "file:", 5)) { char rel[PATH_MAX + 8]; snprintf(rel, sizeof rel, "icons/%s", s + 5); return lhde_data_find(rel, file, nf) && exists(file); }
    if (!strncmp(s, "pe:", 3)) {
        s += 3; char *h = strrchr(s, '#');
        if (!h) return 0;
        *h = 0; snprintf(id, nid, "%s", h + 1);
    }
    if (s[0] == '/') snprintf(file, nf, "%s", s);
    else if (s[0] == '~') snprintf(file, nf, "%s%s", getenv("HOME") ? getenv("HOME") : "", s + 1);
    else if (!strncmp(s, "resources/", 10)) { if (!lhde_data_find(s, file, nf)) return 0; }
    else join(file, nf, r->base, "/", s);
    return exists(file);
}

/* the session's icon set: what lhde_app_icons_set gave, else the selected theme's path.icons (Desktop.icons resolved: a
   recipe, "" for Wine's), else xp */
static const char *current_set(void)
{
    if (chosen[0]) return chosen;
    if (!have_current) {
        have_current = 1; snprintf(current, sizeof current, "xp");
        char err[256]; lhde_theme *t = lhde_theme_resolve_current(err, sizeof err);
        if (t) {
            const char *p = lhde_theme_get(t, "path.icons"), *nm = lhde_theme_get(t, "Desktop.icons");
            if (p && p[0]) snprintf(current, sizeof current, "%s", p);
            else if (nm && !strcmp(nm, "wine")) snprintf(current, sizeof current, "wine");
            lhde_theme_free(t);
        }
    }
    return current;
}

static unsigned long *key_image(const char *key, int size, unsigned long *n, char *why, size_t nwhy)
{
    struct recipe *chain[24]; int nc = 0;
    struct recipe *cur = recipe_get(current_set());
    if (cur) {
        chain[nc++] = cur;
        char fb[512];
        if (lhde_ini_get(cur->ini, "theme", "fallback", fb, sizeof fb))
            for (char *sv, *t = strtok_r(fb, " ,", &sv); t && nc < 20; t = strtok_r(NULL, " ,", &sv)) { struct recipe *r = recipe_get(t); if (r) chain[nc++] = r; }
    }
    static const char *const tail[] = { "xp", "2k", "reactos", "wine", NULL };
    for (int i = 0; tail[i]; i++) { struct recipe *r = recipe_get(tail[i]); if (r) chain[nc++] = r; }
    for (int i = 0; i < nc; i++) {
        int dup = 0; for (int j = 0; j < i; j++) dup |= chain[j] == chain[i];
        char file[PATH_MAX], id[128], src[PATH_MAX];
        if (dup || !recipe_source(chain[i], key, file, sizeof file, id, sizeof id, src, sizeof src)) continue;
        unsigned long *im = id[0] ? pe_icon(file, id, size, chain[i]->depth, n) : file_icon(file, size, chain[i]->depth, n);
        if (!im) continue;
        if (why && nwhy) snprintf(why, nwhy, "%s (icons/%s.theme%s)", src, chain[i]->name, i && chain[i] != cur ? ", a fallback" : "");
        return im;
    }
    return NULL;
}

int lhde_pe_has_resource(const char *path, unsigned type, unsigned id)
{
    size_t len;
    const struct pe *pe = path && path[0] ? pe_open(path) : NULL;
    return pe && pe_resource(pe, type, id, NULL, &len) != NULL;
}

unsigned long *lhde_icon_key_image(const char *key, int size, unsigned long *n, char *why, size_t nwhy)
{
    if (!key || !key[0]) return NULL;
    return key_image(key, size, n, why, nwhy);
}

/* ------------------------------------------------------------------ the XDG icon theme */
struct xdir { char sub[160]; int size, min, max, thr, type; };   /* type 0 Fixed, 1 Scalable, 2 Threshold */
struct xtheme { char name[128], inherits[256]; struct xdir *dirs; int ndirs; };
static struct xtheme xthemes[8]; static int nxthemes;
static char xbases[10][PATH_MAX]; static int nxbases = -1;

static void xdg_bases(void)
{
    if (nxbases >= 0) return;
    nxbases = 0;
    const char *h = getenv("HOME"), *dh = getenv("XDG_DATA_HOME"), *dd = getenv("XDG_DATA_DIRS");
    if (h) snprintf(xbases[nxbases++], PATH_MAX, "%s/.icons", h);
    if (dh && dh[0]) snprintf(xbases[nxbases++], PATH_MAX, "%s/icons", dh); else if (h) snprintf(xbases[nxbases++], PATH_MAX, "%s/.local/share/icons", h);
    char buf[4096]; snprintf(buf, sizeof buf, "%s", dd && dd[0] ? dd : "/usr/local/share:/usr/share");
    for (char *sv, *t = strtok_r(buf, ":", &sv); t && nxbases < 10; t = strtok_r(NULL, ":", &sv)) snprintf(xbases[nxbases++], PATH_MAX, "%s/icons", t);
}

static struct xtheme *xtheme_get(const char *name)
{
    for (int i = 0; i < nxthemes; i++) if (!strcmp(xthemes[i].name, name)) return &xthemes[i];
    if (nxthemes == 8) return NULL;
    xdg_bases();
    char idx[PATH_MAX + 160];
    int b = 0;
    for (; b < nxbases; b++) { snprintf(idx, sizeof idx, "%s/%s/index.theme", xbases[b], name); if (exists(idx)) break; }
    if (b == nxbases) return NULL;
    struct xtheme *t = &xthemes[nxthemes++]; memset(t, 0, sizeof *t); snprintf(t->name, sizeof t->name, "%s", name);
    lhde_ini *ini = lhde_ini_load(idx);
    lhde_ini_get(ini, "Icon Theme", "Inherits", t->inherits, sizeof t->inherits);
    static char dl[16384];
    if (lhde_ini_get(ini, "Icon Theme", "Directories", dl, sizeof dl))
        for (char *sv, *d = strtok_r(dl, ",", &sv); d; d = strtok_r(NULL, ",", &sv)) {
            char v[64]; d = lhde_trim(d);
            if (!d[0]) continue;
            if (lhde_ini_get(ini, d, "Scale", v, sizeof v) && atoi(v) > 1) continue;
            struct xdir *nd = realloc(t->dirs, (size_t)(t->ndirs + 1) * sizeof *nd);
            if (!nd) break;
            t->dirs = nd; struct xdir *x = &t->dirs[t->ndirs++]; memset(x, 0, sizeof *x);
            snprintf(x->sub, sizeof x->sub, "%s", d);
            x->size = lhde_ini_get(ini, d, "Size", v, sizeof v) ? atoi(v) : 48;
            x->min = lhde_ini_get(ini, d, "MinSize", v, sizeof v) ? atoi(v) : x->size;
            x->max = lhde_ini_get(ini, d, "MaxSize", v, sizeof v) ? atoi(v) : x->size;
            x->thr = lhde_ini_get(ini, d, "Threshold", v, sizeof v) ? atoi(v) : 2;
            x->type = !lhde_ini_get(ini, d, "Type", v, sizeof v) ? 2 : !strcasecmp(v, "Fixed") ? 0 : !strcasecmp(v, "Scalable") ? 1 : 2;
        }
    lhde_ini_free(ini);
    return t;
}

/* the icon theme spec's DirectorySizeDistance */
static int xdist(const struct xdir *d, int size)
{
    int lo = d->type == 0 ? d->size : d->type == 1 ? d->min : d->size - d->thr, hi = d->type == 0 ? d->size : d->type == 1 ? d->max : d->size + d->thr;
    return size < lo ? lo - size : size > hi ? size - hi : 0;
}

/* NAME in a theme: the PNG of the nearest size (ties: the larger) in any base */
static int xtheme_find(const struct xtheme *t, const char *name, int size, char *out, size_t n)
{
    int best = -1, bd = 0, bs = 0;
    for (int i = 0; i < t->ndirs; i++) {
        int dd = xdist(&t->dirs[i], size);
        if (best >= 0 && (dd > bd || (dd == bd && t->dirs[i].size <= bs))) continue;
        for (int b = 0; b < nxbases; b++) {
            char p[PATH_MAX * 2]; snprintf(p, sizeof p, "%s/%s/%s/%s.png", xbases[b], t->name, t->dirs[i].sub, name);
            if (exists(p)) { best = i; bd = dd; bs = t->dirs[i].size; join(out, n, p, "", ""); break; }
        }
    }
    return best >= 0;
}

static const char *gtk_theme(char *out, size_t n)
{
    const char *h = getenv("HOME"), *ch = getenv("XDG_CONFIG_HOME");
    char p[PATH_MAX];
    if (ch && ch[0]) snprintf(p, sizeof p, "%s/gtk-3.0/settings.ini", ch); else snprintf(p, sizeof p, "%s/.config/gtk-3.0/settings.ini", h ? h : "");
    lhde_ini *ini = lhde_ini_load(p);
    const char *r = ini ? lhde_ini_get(ini, "Settings", "gtk-icon-theme-name", out, n) : NULL;
    lhde_ini_free(ini);
    return r && r[0] ? r : NULL;
}

static int xdg_icon(const char *name, int size, char *out, size_t n)
{
    if (!name || !name[0]) return 0;
    if (name[0] == '/') { snprintf(out, n, "%s", name); return exists(out); }
    char nm[256]; snprintf(nm, sizeof nm, "%s", name);
    size_t l = strlen(nm);
    if (l > 4 && (!strcmp(nm + l - 4, ".png") || !strcmp(nm + l - 4, ".svg") || !strcmp(nm + l - 4, ".xpm"))) nm[l - 4] = 0;
    xdg_bases();
    char queue[8][128]; int nq = 0, seen_hicolor = 0;
    char g[128];
    if (gtk_theme(g, sizeof g)) snprintf(queue[nq++], 128, "%s", g);
    for (int i = 0; i < nq || !seen_hicolor; i++) {
        if (i == nq) { snprintf(queue[nq++], 128, "hicolor"); }
        if (!strcmp(queue[i], "hicolor")) seen_hicolor = 1;
        struct xtheme *t = xtheme_get(queue[i]);
        if (!t) continue;
        if (xtheme_find(t, nm, size, out, n)) return 1;
        char inh[256]; snprintf(inh, sizeof inh, "%s", t->inherits);
        for (char *sv, *x = strtok_r(inh, ",", &sv); x && nq < 8; x = strtok_r(NULL, ",", &sv)) {
            x = lhde_trim(x); int dup = 0;
            for (int k = 0; k < nq; k++) dup |= !strcmp(queue[k], x);
            if (!dup && x[0]) snprintf(queue[nq++], 128, "%s", x);
        }
    }
    snprintf(out, n, "/usr/share/pixmaps/%s.png", nm);
    return exists(out);
}

/* ------------------------------------------------------------------ desktop files */
struct dentry { char id[160], wmclass[96], icon[256]; };
static struct dentry *dents; static int ndents = -1;

static void scan_apps(const char *dir, const char *prefix)
{
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char p[PATH_MAX]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        size_t l = strlen(e->d_name);
        if (is_dir(p)) { if (!prefix[0]) scan_apps(p, e->d_name); continue; }
        if (l < 9 || strcmp(e->d_name + l - 8, ".desktop")) continue;
        char id[160]; join(id, sizeof id, prefix, prefix[0] ? "-" : "", e->d_name);
        int dup = 0; for (int i = 0; i < ndents && !dup; i++) dup = !strcmp(dents[i].id, id);   /* the first directory wins */
        if (dup) continue;
        lhde_ini *ini = lhde_ini_load(p);
        struct dentry de; memset(&de, 0, sizeof de); snprintf(de.id, sizeof de.id, "%s", id);
        lhde_ini_get(ini, "Desktop Entry", "StartupWMClass", de.wmclass, sizeof de.wmclass);
        lhde_ini_get(ini, "Desktop Entry", "Icon", de.icon, sizeof de.icon);
        lhde_ini_free(ini);
        struct dentry *nd = realloc(dents, (size_t)(ndents + 1) * sizeof *nd);
        if (!nd) break;
        dents = nd; dents[ndents++] = de;
    }
    closedir(d);
}
static void load_apps(void)
{
    if (ndents >= 0) return;
    ndents = 0;
    const char *h = getenv("HOME"), *dh = getenv("XDG_DATA_HOME"), *dd = getenv("XDG_DATA_DIRS");
    char p[PATH_MAX];
    if (dh && dh[0]) snprintf(p, sizeof p, "%s/applications", dh); else snprintf(p, sizeof p, "%s/.local/share/applications", h ? h : "");
    scan_apps(p, "");
    char buf[4096]; snprintf(buf, sizeof buf, "%s", dd && dd[0] ? dd : "/usr/local/share:/usr/share");
    for (char *sv, *t = strtok_r(buf, ":", &sv); t; t = strtok_r(NULL, ":", &sv)) { snprintf(p, sizeof p, "%s/applications", t); scan_apps(p, ""); }
}

/* the desktop entry for an id (with or without .desktop), else by StartupWMClass, else by its name */
static const struct dentry *find_app(const char *id, const char *cls, const char *inst)
{
    load_apps();
    char want[200];
    if (id && id[0]) {
        snprintf(want, sizeof want, "%s%s", id, strstr(id, ".desktop") ? "" : ".desktop");
        for (int i = 0; i < ndents; i++) if (!strcmp(dents[i].id, want)) return &dents[i];
    }
    const char *names[2] = { cls, inst };
    for (int k = 0; k < 2; k++) if (names[k] && names[k][0])
        for (int i = 0; i < ndents; i++) if (dents[i].wmclass[0] && !strcasecmp(dents[i].wmclass, names[k])) return &dents[i];
    for (int k = 0; k < 2; k++) if (names[k] && names[k][0]) {
        snprintf(want, sizeof want, "%s.desktop", names[k]);
        for (int i = 0; i < ndents; i++) {
            const char *d = dents[i].id; size_t dl = strlen(d), wl = strlen(want);
            /* xterm -> xterm.desktop; org.gnome.Nautilus -> org.gnome.Nautilus.desktop; a reverse-DNS id ending in .Class */
            if (!strcasecmp(d, want) || (dl > wl && d[dl - wl - 1] == '.' && !strcasecmp(d + dl - wl, want))) return &dents[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ the class map */
struct classmatch { const char *cls, *inst; char *key; size_t n; int hit; };
static void class_cb(const char *k, const char *v, void *ud)
{
    struct classmatch *m = ud;
    if (m->hit) return;
    char pats[1024]; snprintf(pats, sizeof pats, "%s", k);
    for (char *sv, *p = strtok_r(pats, " \t", &sv); p; p = strtok_r(NULL, " \t", &sv))
        if ((m->cls && m->cls[0] && !fnmatch(p, m->cls, FNM_CASEFOLD)) || (m->inst && m->inst[0] && !fnmatch(p, m->inst, FNM_CASEFOLD))) {
            snprintf(m->key, m->n, "%s", v); m->hit = 1; return;
        }
}
const char *lhde_app_icon_key(const char *cls, const char *inst, char *key, size_t nkey)
{
    const char *dirs[LHDE_DATA_MAX]; int nd = lhde_data_dirs(dirs, LHDE_DATA_MAX);
    for (int i = 0; i < nd; i++) {
        char p[PATH_MAX + 32]; snprintf(p, sizeof p, "%s/icons/apps.ini", dirs[i]);
        if (!exists(p)) continue;
        struct classmatch m = { cls, inst, key, nkey, 0 };
        lhde_ini *ini = lhde_ini_load(p);
        lhde_ini_keys(ini, "classes", class_cb, &m);
        lhde_ini_free(ini);
        if (m.hit) return key;
    }
    return NULL;
}

/* ------------------------------------------------------------------ the lookup */
static unsigned long *own_best(const unsigned long *d, unsigned long n, int want, unsigned long *out_n)
{
    unsigned long i = 0, bw = 0, bh = 0; const unsigned long *best = NULL;
    while (i + 2 <= n) {
        unsigned long w = d[i], h = d[i + 1];
        if (!w || !h || w > 1024 || h > 1024 || i + 2 + w * h > n) break;
        unsigned long s = w > h ? w : h, bs = bw > bh ? bw : bh;
        if (!best || (s >= (unsigned long)want ? (bs < (unsigned long)want || s < bs) : (bs < (unsigned long)want && s > bs))) { best = d + i + 2; bw = w; bh = h; }
        i += 2 + w * h;
    }
    if (!best) return NULL;
    unsigned long *o = img_new((unsigned)bw, (unsigned)bh, out_n);
    if (o) for (unsigned long k = 0; k < bw * bh; k++) o[2 + k] = best[k] & 0xffffffffu;
    return o;
}

unsigned long *lhde_app_icon(const char *cls, const char *inst, const char *desktop_id, int size, const unsigned long *own,
                             unsigned long nown, int flags, unsigned long *n, int *step, char *why, size_t nwhy)
{
    unsigned long dummy; if (!n) n = &dummy;
    *n = 0; if (step) *step = LHDE_APPICON_NONE; if (why && nwhy) why[0] = 0;
    if (size <= 0) size = 32;
    unsigned long *im;
    if (own && nown > 2 && (im = own_best(own, nown, size, n))) { if (step) *step = LHDE_APPICON_OWN; say(why, nwhy, "the window's _NET_WM_ICON", 0, 0, 0); return im; }
    char key[256], src[PATH_MAX + 300], f[PATH_MAX];
    if (lhde_app_icon_key(cls, inst, key, sizeof key)) {
        im = key_image(key, size, n, src, sizeof src);
        if (!im && xdg_icon(key, size, f, sizeof f)) { im = file_icon(f, size, 32, n); snprintf(src, sizeof src, "%s (an XDG icon)", f); }
        if (im) {
            if (step) *step = LHDE_APPICON_CLASS;
            if (why && nwhy) snprintf(why, nwhy, "class map: %s = %s (icons/apps.ini): %s", cls && cls[0] ? cls : inst, key, src);
            return im;
        }
    }
    if ((cls && cls[0]) || (inst && inst[0]) || (desktop_id && desktop_id[0])) {
        const struct dentry *de = find_app(desktop_id, cls, inst);
        if (de && de->icon[0] && xdg_icon(de->icon, size, f, sizeof f) && (im = file_icon(f, size, 32, n))) {
            if (step) *step = LHDE_APPICON_DESKTOP;
            say(why, nwhy, "desktop file %s: Icon=%s: %s", de->id, de->icon, f);
            return im;
        }
    }
    if (flags & LHDE_APPICON_NO_GENERIC) return NULL;
    if ((im = key_image("exe", size, n, src, sizeof src))) {
        if (step) *step = LHDE_APPICON_GENERIC;
        say(why, nwhy, "the generic application icon: exe = %s", src, 0, 0);
        return im;
    }
    return NULL;
}

void lhde_app_icons_set(const char *icons)
{
    snprintf(chosen, sizeof chosen, "%s", icons ? icons : "");
    have_current = 0;
    for (int i = 0; i < nrecipes; i++) lhde_ini_free(recipes[i].ini);
    nrecipes = 0;
    for (int i = 0; i < nxthemes; i++) free(xthemes[i].dirs);
    nxthemes = 0; nxbases = -1;
    free(dents); dents = NULL; ndents = -1;
}
