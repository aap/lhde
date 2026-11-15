/* liblhcat: a picture's facts from its headers (EXIF in JPEG and TIFF, the sizes of PNG, GIF, WebP, BMP), and sha256. */
#define _GNU_SOURCE
#include "lhcat.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- TIFF / EXIF ------------------------------------------------------------------------------------------------------ */

struct tiff { const unsigned char *b; size_t n; int le; };

static unsigned u16(const struct tiff *t, size_t o)
{
    if (o + 2 > t->n) return 0;
    return t->le ? (unsigned)(t->b[o] | t->b[o + 1] << 8) : (unsigned)(t->b[o] << 8 | t->b[o + 1]);
}

static uint32_t u32(const struct tiff *t, size_t o)
{
    if (o + 4 > t->n) return 0;
    const unsigned char *p = t->b + o;
    return t->le ? (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24
                 : (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

/* an ASCII entry's text (trailing NULs and blanks dropped) */
static void ascii(const struct tiff *t, size_t entry, char *out, size_t n)
{
    uint32_t count = u32(t, entry + 4);
    size_t at = count <= 4 ? entry + 8 : u32(t, entry + 8);
    out[0] = 0;
    if (count == 0 || at >= t->n || count > t->n - at) return;
    size_t len = count < n ? count : n - 1;
    memcpy(out, t->b + at, len);
    out[len] = 0;
    len = strnlen(out, len);
    while (len && (out[len - 1] == ' ' || out[len - 1] == 0)) out[--len] = 0;
}

static unsigned number(const struct tiff *t, size_t entry)
{
    unsigned type = u16(t, entry + 2);
    return type == 3 ? u16(t, entry + 8) : type == 4 ? u32(t, entry + 8) : 0;
}

/* "YYYY:MM:DD HH:MM:SS" -> "YYYY-MM-DD HH:MM:SS"; an empty or zero date gives nothing */
static void exif_date(const char *in, char *out)
{
    out[0] = 0;
    if (strlen(in) < 19) return;
    for (int i = 0; i < 19; i++) {
        int digit = i != 4 && i != 7 && i != 10 && i != 13 && i != 16;
        if (digit ? (in[i] < '0' || in[i] > '9') : 0) return;
    }
    if (!strncmp(in, "0000", 4)) return;
    memcpy(out, in, 19);
    out[4] = out[7] = '-';
    out[10] = ' ';
    out[13] = out[16] = ':';
    out[19] = 0;
}

/* the TIFF structure at b (an EXIF block after "Exif\0\0", or a TIFF file) into p; 1 when anything was found */
static int parse_tiff(const unsigned char *b, size_t n, struct lhcat_picture *p, int tiff_file)
{
    struct tiff t = { b, n, 0 };
    if (n < 8) return 0;
    if (b[0] == 'I' && b[1] == 'I') t.le = 1;
    else if (!(b[0] == 'M' && b[1] == 'M')) return 0;
    if (u16(&t, 2) != 42) return 0;
    char make[48] = "", model[64] = "", date[24] = "", original[24] = "";
    size_t exif = 0;
    int found = 0;
    for (int pass = 0; pass < 2; pass++) {
        size_t ifd = pass == 0 ? u32(&t, 4) : exif;
        if (!ifd || ifd + 2 > n) break;
        unsigned count = u16(&t, ifd);
        for (unsigned i = 0; i < count && i < 512; i++) {
            size_t e = ifd + 2 + 12 * (size_t)i;
            if (e + 12 > n) break;
            unsigned tag = u16(&t, e);
            if (pass == 0) {
                switch (tag) {
                case 0x010F: ascii(&t, e, make, sizeof make); break;
                case 0x0110: ascii(&t, e, model, sizeof model); break;
                case 0x0132: ascii(&t, e, date, sizeof date); break;
                case 0x0112: p->orientation = (int)number(&t, e); found = 1; break;
                case 0x8769: exif = u32(&t, e + 8); break;
                case 0x0100: if (tiff_file) p->width = (int)number(&t, e); break;
                case 0x0101: if (tiff_file) p->height = (int)number(&t, e); break;
                }
            } else {
                switch (tag) {
                case 0x9003: ascii(&t, e, original, sizeof original); break;
                case 0xA002: if (!p->width) p->width = (int)number(&t, e); break;
                case 0xA003: if (!p->height) p->height = (int)number(&t, e); break;
                }
            }
        }
    }
    if (p->orientation < 1 || p->orientation > 8) p->orientation = 0;
    exif_date(original[0] ? original : date, p->taken_at);
    if (model[0] && make[0] && strncasecmp(model, make, strlen(make)))
        snprintf(p->camera, sizeof p->camera, "%s %s", make, model);
    else snprintf(p->camera, sizeof p->camera, "%s", model[0] ? model : make);
    return found || p->taken_at[0] || p->camera[0] || p->width;
}

/* ---- the formats ------------------------------------------------------------------------------------------------------ */

static unsigned le16(const unsigned char *b) { return (unsigned)(b[0] | b[1] << 8); }
static uint32_t le32(const unsigned char *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
static uint32_t be32(const unsigned char *b) { return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3]; }

static int small_formats(const unsigned char *b, size_t n, struct lhcat_picture *p)
{
    if (n >= 24 && !memcmp(b, "\x89PNG\r\n\x1a\n", 8) && !memcmp(b + 12, "IHDR", 4)) {
        p->width = (int)be32(b + 16);
        p->height = (int)be32(b + 20);
        return 1;
    }
    if (n >= 10 && (!memcmp(b, "GIF87a", 6) || !memcmp(b, "GIF89a", 6))) {
        p->width = (int)le16(b + 6);
        p->height = (int)le16(b + 8);
        return 1;
    }
    if (n >= 30 && !memcmp(b, "RIFF", 4) && !memcmp(b + 8, "WEBP", 4)) {
        if (!memcmp(b + 12, "VP8 ", 4) && b[23] == 0x9d && b[24] == 0x01 && b[25] == 0x2a) {
            p->width = (int)(le16(b + 26) & 0x3fff);
            p->height = (int)(le16(b + 28) & 0x3fff);
            return 1;
        }
        if (!memcmp(b + 12, "VP8L", 4) && b[20] == 0x2f) {
            uint32_t v = le32(b + 21);
            p->width = (int)(v & 0x3fff) + 1;
            p->height = (int)((v >> 14) & 0x3fff) + 1;
            return 1;
        }
        if (!memcmp(b + 12, "VP8X", 4)) {
            p->width = (int)(b[24] | b[25] << 8 | b[26] << 16) + 1;
            p->height = (int)(b[27] | b[28] << 8 | b[29] << 16) + 1;
            return 1;
        }
        return 0;
    }
    if (n >= 26 && b[0] == 'B' && b[1] == 'M') {
        uint32_t hs = le32(b + 14);
        if (hs == 12) { p->width = (int)le16(b + 18); p->height = (int)le16(b + 20); }
        else if (hs >= 40) {
            int32_t h = (int32_t)le32(b + 22);
            p->width = (int)(int32_t)le32(b + 18);
            p->height = h < 0 ? -h : h;
        } else return 0;
        return 1;
    }
    return 0;
}

/* A JPEG's markers: the reader gives bytes at an offset (from the file or a buffer) */
struct src { int fd; const unsigned char *buf; size_t n; uint64_t *bytes; };

static ssize_t src_read(struct src *s, uint64_t off, void *out, size_t len)
{
    if (s->buf) {
        if (off >= s->n) return 0;
        if (len > s->n - off) len = (size_t)(s->n - off);
        memcpy(out, s->buf + off, len);
        return (ssize_t)len;
    }
    ssize_t k = pread(s->fd, out, len, (off_t)off);
    if (k > 0 && s->bytes) *s->bytes += (uint64_t)k;
    return k;
}

static int jpeg(struct src *s, struct lhcat_picture *p)
{
    unsigned char h[4];
    if (src_read(s, 0, h, 2) != 2 || h[0] != 0xFF || h[1] != 0xD8) return 0;
    uint64_t off = 2;
    int found = 0;
    for (int markers = 0; markers < 256; markers++) {
        ssize_t k = src_read(s, off, h, 4);
        if (k < 0) return -1;
        if (k < 4 || h[0] != 0xFF) break;
        if (h[1] == 0xFF) { off++; continue; }   /* fill bytes */
        unsigned m = h[1];
        if (m == 0xD9 || m == 0xDA) break;       /* the image data: no frame header before it */
        if (m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { off += 2; continue; }
        unsigned len = (unsigned)(h[2] << 8 | h[3]);
        if (len < 2) break;
        if (m == 0xE1 && !found) {
            unsigned char *seg = malloc(len);
            if (!seg) return -1;
            ssize_t r = src_read(s, off + 4, seg, len - 2);
            if (r < 0) { free(seg); return -1; }
            if (r >= 6 && !memcmp(seg, "Exif\0\0", 6)) found = parse_tiff(seg + 6, (size_t)r - 6, p, 0);
            free(seg);
        } else if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            unsigned char f[5];
            if (src_read(s, off + 4, f, 5) == 5) {
                p->height = f[1] << 8 | f[2];
                p->width = f[3] << 8 | f[4];
                return 1;
            }
            break;
        }
        off += 2 + len;
    }
    return found || p->width;
}

static void clear(struct lhcat_picture *p) { memset(p, 0, sizeof *p); }

int lhcat_picture_parse(const unsigned char *buf, size_t n, const char *type, struct lhcat_picture *p)
{
    clear(p);
    if (n >= 3 && buf[0] == 0xFF && buf[1] == 0xD8) {
        struct src s = { -1, buf, n, NULL };
        return jpeg(&s, p);
    }
    if (n >= 4 && (!memcmp(buf, "II*\0", 4) || !memcmp(buf, "MM\0*", 4))) return parse_tiff(buf, n, p, 1);
    (void)type;
    return small_formats(buf, n, p);
}

int lhcat_picture_read(int fd, const char *type, struct lhcat_picture *p, uint64_t *bytes)
{
    clear(p);
    unsigned char head[64];
    struct src s = { fd, NULL, 0, bytes };
    ssize_t k = src_read(&s, 0, head, sizeof head);
    if (k < 0) return -1;
    if (k >= 3 && head[0] == 0xFF && head[1] == 0xD8) return jpeg(&s, p);
    if (k >= 4 && (!memcmp(head, "II*\0", 4) || !memcmp(head, "MM\0*", 4))) {   /* IFD0 within the first 256 KiB */
        size_t n = 256 * 1024;
        unsigned char *b = malloc(n);
        if (!b) return -1;
        ssize_t r = src_read(&s, 0, b, n);
        int f = r > 0 ? parse_tiff(b, (size_t)r, p, 1) : r < 0 ? -1 : 0;
        free(b);
        return f;
    }
    (void)type;
    return small_formats(head, (size_t)k, p);
}

/* ---- sha256 (FIPS 180-4) ---------------------------------------------------------------------------------------------- */

struct sha256 { uint32_t h[8]; uint64_t len; unsigned char buf[64]; size_t n; };

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR(x, n) ((x) >> (n) | (x) << (32 - (n)))

static void sha_block(struct sha256 *s, const unsigned char *b)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = be32(b + 4 * i);
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->h[0], bb = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & bb) ^ (a & c) ^ (bb & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = bb; bb = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += bb; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

static void sha_init(struct sha256 *s)
{
    static const uint32_t H[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    memcpy(s->h, H, sizeof H);
    s->len = 0;
    s->n = 0;
}

static void sha_add(struct sha256 *s, const unsigned char *p, size_t n)
{
    s->len += n;
    while (n) {
        size_t k = 64 - s->n < n ? 64 - s->n : n;
        memcpy(s->buf + s->n, p, k);
        s->n += k; p += k; n -= k;
        if (s->n == 64) { sha_block(s, s->buf); s->n = 0; }
    }
}

static void sha_hex(const struct sha256 *in, char *out)
{
    struct sha256 s = *in;
    uint64_t bits = s.len * 8;
    unsigned char pad = 0x80, zero = 0, lenb[8];
    sha_add(&s, &pad, 1);
    while (s.n != 56) sha_add(&s, &zero, 1);
    for (int i = 0; i < 8; i++) lenb[i] = (unsigned char)(bits >> (56 - 8 * i));
    sha_add(&s, lenb, 8);
    for (int i = 0; i < 8; i++) snprintf(out + 8 * i, 9, "%08x", s.h[i]);
}

int lhcat_sha256_file(int fd, char *prefix64k, char *full, uint64_t *bytes)
{
    struct sha256 s;
    sha_init(&s);
    unsigned char buf[65536];
    uint64_t total = 0;
    int prefix_done = 0;
    for (;;) {
        ssize_t k = read(fd, buf, sizeof buf);
        if (k < 0) { if (errno == EINTR) continue; return -1; }
        if (k == 0) break;
        size_t take = (size_t)k;
        if (!prefix_done && total + take >= 65536) {   /* the first 64 KiB's digest on the way */
            size_t part = (size_t)(65536 - total);
            sha_add(&s, buf, part);
            sha_hex(&s, prefix64k);
            prefix_done = 1;
            sha_add(&s, buf + part, take - part);
        } else sha_add(&s, buf, take);
        total += take;
        if (bytes) *bytes += take;
    }
    if (!prefix_done) sha_hex(&s, prefix64k);
    sha_hex(&s, full);
    return 0;
}
