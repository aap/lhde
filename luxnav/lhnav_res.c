/* lhnav_res: the few Windows resources lhnav reads from a build's system32 (shell32.dll) for its toolbar items when the client
   gives no images of its own: icon groups (RT_GROUP_ICON -> RT_ICON, DIB or PNG) and bitmaps (RT_BITMAP, the toolbar strips).
   A minimal read-only PE resource walker over an mmap of the file; the first language of each entry is taken. Results are
   premultiplied ARGB (native 0xAARRGGBB), stride 4 w, free()d by the caller. */
#include "lhnav_res.h"
#include "lhnav_px.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

struct pe { const unsigned char *d; size_t n; uint32_t rsrc_off, rsrc_rva, rsrc_size; const unsigned char *sec; int nsec; };

static uint32_t u32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t u16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* file offset of an RVA, 0 when outside every section */
static uint32_t rva_off(const struct pe *pe, uint32_t rva)
{
    for (int i = 0; i < pe->nsec; i++) {
        const unsigned char *s = pe->sec + 40 * i;
        uint32_t va = u32(s + 12), vs = u32(s + 8), raw = u32(s + 20), rs = u32(s + 16);
        if (rva >= va && rva < va + (vs > rs ? vs : rs)) return raw + (rva - va);
    }
    return 0;
}

static int pe_open(struct pe *pe, const char *path)
{
    memset(pe, 0, sizeof *pe);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    struct stat st;
    if (fstat(fd, &st) || st.st_size < 512) { close(fd); return 0; }
    void *m = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) return 0;
    pe->d = m; pe->n = (size_t)st.st_size;
    const unsigned char *d = pe->d;
    uint32_t lf = d[0] == 'M' && d[1] == 'Z' ? u32(d + 60) : 0;
    if (!lf || lf + 24 > pe->n || memcmp(d + lf, "PE\0\0", 4)) goto bad;
    int nsec = u16(d + lf + 6), optsz = u16(d + lf + 20);
    const unsigned char *opt = d + lf + 24;
    if (lf + 24 + (size_t)optsz + 40u * nsec > pe->n) goto bad;
    int magic = u16(opt), dirs = magic == 0x20b ? 112 : 96;
    if (optsz < dirs + 3 * 8) goto bad;
    pe->sec = opt + optsz; pe->nsec = nsec;
    pe->rsrc_rva = u32(opt + dirs + 2 * 8); pe->rsrc_size = u32(opt + dirs + 2 * 8 + 4);
    pe->rsrc_off = rva_off(pe, pe->rsrc_rva);
    if (!pe->rsrc_off || pe->rsrc_off >= pe->n) goto bad;
    return 1;
bad:
    munmap((void *)pe->d, pe->n); pe->d = NULL;
    return 0;
}

static void pe_close(struct pe *pe) { if (pe->d) munmap((void *)pe->d, pe->n); pe->d = NULL; }

/* the entry `id` (numeric) of the directory at offset `dir` (relative to the resource section); want_dir: its subdirectory.
   id < 0: the first entry. Returns the offset (relative) or 0 */
static uint32_t dir_find(const struct pe *pe, uint32_t dir, int id, int want_dir)
{
    const unsigned char *b = pe->d + pe->rsrc_off;
    if (pe->rsrc_off + dir + 16 > pe->n) return 0;
    int nn = u16(b + dir + 12), ni = u16(b + dir + 14);
    for (int i = 0; i < nn + ni; i++) {
        size_t e = dir + 16 + 8 * (size_t)i;
        if (pe->rsrc_off + e + 8 > pe->n) return 0;
        uint32_t name = u32(b + e), off = u32(b + e + 4);
        if (id >= 0 && ((name & 0x80000000u) || (int)name != id)) continue;
        if (!!(off & 0x80000000u) != want_dir) return 0;
        return off & 0x7fffffffu;
    }
    return 0;
}

/* the data of resource type/id (first language) */
static const unsigned char *pe_get(const struct pe *pe, int type, int id, uint32_t *size)
{
    uint32_t t = dir_find(pe, 0, type, 1); if (!t) return NULL;
    uint32_t n = dir_find(pe, t, id, 1); if (!n) return NULL;
    uint32_t l = dir_find(pe, n, -1, 0); if (!l) return NULL;
    const unsigned char *de = pe->d + pe->rsrc_off + l;
    if (pe->rsrc_off + l + 16 > pe->n) return NULL;
    uint32_t off = rva_off(pe, u32(de)), sz = u32(de + 4);
    if (!off || off + (size_t)sz > pe->n) return NULL;
    *size = sz;
    return pe->d + off;
}

static uint32_t premul(uint32_t a, uint32_t r, uint32_t g, uint32_t b)
{
    return a << 24 | (r * a + 127) / 255 << 16 | (g * a + 127) / 255 << 8 | (b * a + 127) / 255;
}

/* a DIB (BITMAPINFOHEADER + palette + bits [+ AND mask]); icon: the height is doubled and the mask follows */
static uint32_t *dib_decode(const unsigned char *p, uint32_t size, int icon, int *ow, int *oh)
{
    if (size < 40 || u32(p) < 40) return NULL;
    uint32_t hs = u32(p);
    int w = (int)u32(p + 4), h = (int)u32(p + 8), bpp = u16(p + 14), comp = (int)u32(p + 16), ncol = (int)u32(p + 32);
    int down = h < 0;   /* top-down */
    if (h < 0) h = -h;
    if (icon) h /= 2;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || (comp != 0 && comp != 3)) return NULL;
    if (bpp <= 8 && !ncol) ncol = 1 << bpp;
    if (bpp > 8) ncol = 0;
    const unsigned char *pal = p + hs, *bits = pal + 4 * ncol + (comp == 3 && hs == 40 ? 12 : 0);
    size_t stride = ((size_t)w * bpp + 31) / 32 * 4, mstride = ((size_t)w + 31) / 32 * 4;
    if ((size_t)(bits - p) + stride * h > size) return NULL;
    const unsigned char *mask = icon && (size_t)(bits - p) + stride * h + mstride * h <= size ? bits + stride * h : NULL;
    uint32_t *o = malloc((size_t)w * h * 4);
    if (!o) return NULL;
    int any_alpha = 0;
    if (bpp == 32) for (int y = 0; y < h && !any_alpha; y++) for (int x = 0; x < w; x++) if (bits[y * stride + 4 * x + 3]) { any_alpha = 1; break; }
    for (int y = 0; y < h; y++) {
        int sy = down ? y : h - 1 - y;
        const unsigned char *row = bits + sy * stride, *mrow = mask ? mask + sy * mstride : NULL;
        for (int x = 0; x < w; x++) {
            uint32_t r, g, b, a = 255;
            if (bpp == 32) { b = row[4 * x]; g = row[4 * x + 1]; r = row[4 * x + 2]; if (any_alpha) a = row[4 * x + 3]; }
            else if (bpp == 24) { b = row[3 * x]; g = row[3 * x + 1]; r = row[3 * x + 2]; }
            else if (bpp <= 8) {
                int idx = bpp == 8 ? row[x] : bpp == 4 ? (row[x / 2] >> (x & 1 ? 0 : 4)) & 15 : (row[x / 8] >> (7 - (x & 7))) & 1;
                if (idx >= ncol) idx = 0;
                b = pal[4 * idx]; g = pal[4 * idx + 1]; r = pal[4 * idx + 2];
            } else { free(o); return NULL; }
            if (!any_alpha && mrow && (mrow[x / 8] >> (7 - (x & 7)) & 1)) a = 0;
            o[(size_t)y * w + x] = premul(a, r, g, b);
        }
    }
    *ow = w; *oh = h;
    return o;
}

static uint32_t *png_decode(const unsigned char *p, uint32_t size, int *ow, int *oh) { return lhnav__px_png_decode(p, size, ow, oh); }

uint32_t *lhnav__res_icon(const char *file, int id, int want, int *w, int *h)
{
    struct pe pe;
    if (!file || !pe_open(&pe, file)) return NULL;
    uint32_t gs, best_id = 0; int best = -1, bestd = 1 << 30, bestbpp = 0;
    const unsigned char *g = pe_get(&pe, 14, id, &gs);
    if (g && gs >= 6) {
        int n = u16(g + 4);
        for (int i = 0; i < n && 6 + 14 * (size_t)(i + 1) <= gs; i++) {
            const unsigned char *e = g + 6 + 14 * i;
            int sz = e[0] ? e[0] : 256, bpp = u16(e + 6), d = sz >= want ? sz - want : 2 * (want - sz);   /* prefer a larger one */
            if (d < bestd || (d == bestd && bpp > bestbpp)) { bestd = d; best = i; bestbpp = bpp; best_id = u16(e + 12); }
        }
    }
    uint32_t *o = NULL, is;
    const unsigned char *ic = best >= 0 ? pe_get(&pe, 3, (int)best_id, &is) : NULL;
    if (ic) o = is > 8 && !memcmp(ic, "\x89PNG", 4) ? png_decode(ic, is, w, h) : dib_decode(ic, is, 1, w, h);
    pe_close(&pe);
    return o;
}

uint32_t *lhnav__res_bitmap(const char *file, int id, int *w, int *h)
{
    struct pe pe;
    if (!file || !pe_open(&pe, file)) return NULL;
    uint32_t bs;
    const unsigned char *b = pe_get(&pe, 2, id, &bs);
    uint32_t *o = b ? dib_decode(b, bs, 0, w, h) : NULL;
    pe_close(&pe);
    return o;
}
