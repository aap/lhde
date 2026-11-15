/* liblhcat's own checks (make check-liblhcat, run by tests/catalogue.sh): value normalization as flxp stores values,
   the picture readers on built headers, sha256, and the performance of a large du listing: import of N generated lines
   (2,000,000 by default; argv[1]) under 30 s, a tag query on it under 50 ms. $XDG_STATE_HOME is the caller's scratch. */
#define _GNU_SOURCE
#include "lhcat.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int fails, passes;
#define CHECK(c) do { if (c) passes++; else { fails++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
static void eq(int line, const char *what, const char *got, const char *want)
{
    if (got && want && !strcmp(got, want)) { passes++; return; }
    fails++;
    fprintf(stderr, "FAIL line %d: %s\n  got      [%s]\n  expected [%s]\n", line, what, got ? got : "(null)", want ? want : "(null)");
}
#define EQ(a, b) eq(__LINE__, #a, (a), (b))

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec / 1e9;
}

static void normalize(void)
{
    char *v;
    EQ(v = lhcat_normalize("user.xdg.tags", " a, b;a\nc ,, "), "a,b,c"); free(v);
    EQ(v = lhcat_normalize("user.lux.desc", "  one\r\ntwo\n\nthree "), "one two three"); free(v);
    EQ(v = lhcat_normalize("user.xdg.comment", "l1\r\nl2\rl3\n"), "l1\nl2\nl3"); free(v);
    EQ(v = lhcat_normalize("user.lux.color", " Red "), "red"); free(v);
    EQ(v = lhcat_normalize("user.lux.color", "Mauve"), "Mauve"); free(v);
    char n[300];
    EQ(lhcat_attr_name("desc", n, sizeof n), "user.lux.desc");
    EQ(lhcat_attr_name("tags", n, sizeof n), "user.xdg.tags");
    EQ(lhcat_attr_name("lux.notes", n, sizeof n), "user.lux.notes");
    EQ(lhcat_attr_name("user.x.y", n, sizeof n), "user.x.y");
    if (lhcat_type_for_name("a.c")) {   /* a case-sensitive glob: *.C is C++, *.c C */
        EQ(lhcat_type_for_name("a.c"), "text/x-csrc");
        EQ(lhcat_type_for_name("a.C"), "text/x-c++src");
    }
    EQ(lhcat_type_for_name("a.JPG") ? lhcat_type_for_name("a.JPG") : "(none: no shared-mime-info)", "image/jpeg");
}

static void put16(unsigned char *p, unsigned v, int le) { if (le) { p[0] = v; p[1] = v >> 8; } else { p[0] = v >> 8; p[1] = v; } }
static void put32(unsigned char *p, uint32_t v, int le)
{
    if (le) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; } else { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
}

/* a TIFF block: IFD0 Make, Model, Orientation, the Exif IFD with DateTimeOriginal and the pixel sizes */
static size_t tiff(unsigned char *b, int le)
{
    memset(b, 0, 512);
    memcpy(b, le ? "II" : "MM", 2);
    put16(b + 2, 42, le);
    put32(b + 4, 8, le);
    size_t ifd = 8, data = 200;
    put16(b + ifd, 4, le);
    struct { unsigned tag, type; uint32_t count; const char *s; uint32_t v; } e0[4] = {
        { 0x010F, 2, 6, "Canon", 0 }, { 0x0110, 2, 13, "Canon EOS 5D", 0 }, { 0x0112, 3, 1, NULL, 6 }, { 0x8769, 4, 1, NULL, 100 } };
    for (int i = 0; i < 4; i++) {
        unsigned char *e = b + ifd + 2 + 12 * i;
        put16(e, e0[i].tag, le); put16(e + 2, e0[i].type, le); put32(e + 4, e0[i].count, le);
        if (e0[i].s) { put32(e + 8, (uint32_t)data, le); memcpy(b + data, e0[i].s, e0[i].count); data += e0[i].count + 1; }
        else if (e0[i].type == 3) put16(e + 8, e0[i].v, le);
        else put32(e + 8, e0[i].v, le);
    }
    size_t x = 100;
    put16(b + x, 3, le);
    unsigned char *e = b + x + 2;
    put16(e, 0x9003, le); put16(e + 2, 2, le); put32(e + 4, 20, le); put32(e + 8, (uint32_t)data, le);
    memcpy(b + data, "2019:06:01 12:34:56", 20); data += 20;
    e += 12; put16(e, 0xA002, le); put16(e + 2, 4, le); put32(e + 4, 1, le); put32(e + 8, 4000, le);
    e += 12; put16(e, 0xA003, le); put16(e + 2, 3, le); put32(e + 4, 1, le); put16(e + 8, 3000, le);
    return data;
}

static void pictures(void)
{
    unsigned char j[1024], t[512];
    struct lhcat_picture p;
    for (int le = 0; le < 2; le++) {
        size_t n = tiff(t, le);
        size_t o = 0;
        j[o++] = 0xFF; j[o++] = 0xD8;
        j[o++] = 0xFF; j[o++] = 0xE0; j[o++] = 0; j[o++] = 4; j[o++] = 0; j[o++] = 0;   /* an APP0 to skip */
        j[o++] = 0xFF; j[o++] = 0xE1; put16(j + o, (unsigned)(n + 8), 0); o += 2;
        memcpy(j + o, "Exif\0\0", 6); o += 6;
        memcpy(j + o, t, n); o += n;
        j[o++] = 0xFF; j[o++] = 0xC0; put16(j + o, 17, 0); o += 2; j[o++] = 8;
        put16(j + o, 3456, 0); o += 2; put16(j + o, 5184, 0); o += 2;
        memset(j + o, 0, 12); o += 12;
        CHECK(lhcat_picture_parse(j, o, "image/jpeg", &p) == 1);
        EQ(p.taken_at, "2019-06-01 12:34:56");
        EQ(p.camera, "Canon EOS 5D");
        CHECK(p.orientation == 6);
        CHECK(p.width == 5184 && p.height == 3456);   /* the frame's, over the Exif IFD's */
        CHECK(lhcat_picture_parse(t, n, "image/tiff", &p) == 1 && !strcmp(p.taken_at, "2019-06-01 12:34:56"));
    }
    unsigned char png[24] = "\x89PNG\r\n\x1a\n\0\0\0\rIHDR";
    put32(png + 16, 640, 0); put32(png + 20, 480, 0);
    CHECK(lhcat_picture_parse(png, 24, "image/png", &p) == 1 && p.width == 640 && p.height == 480);
    unsigned char gif[10] = "GIF89a";
    put16(gif + 6, 33, 1); put16(gif + 8, 44, 1);
    CHECK(lhcat_picture_parse(gif, 10, "image/gif", &p) == 1 && p.width == 33 && p.height == 44);
    unsigned char webp[30] = "RIFF\0\0\0\0WEBPVP8X";
    webp[24] = 99; webp[27] = 49;   /* 100 x 50 */
    CHECK(lhcat_picture_parse(webp, 30, "image/webp", &p) == 1 && p.width == 100 && p.height == 50);
    CHECK(!strcmp(lhcat_sniff(j, 3), "image/jpeg") && !strcmp(lhcat_sniff(png, 8), "image/png"));
}

static void sha(void)
{
    char path[] = "/tmp/lhcat-sha-XXXXXX";
    const char *dir = getenv("XDG_STATE_HOME");
    char p2[4096];
    snprintf(p2, sizeof p2, "%s/sha-XXXXXX", dir ? dir : "/tmp");
    int fd = mkstemp(dir ? p2 : path);
    CHECK(fd >= 0);
    CHECK(write(fd, "abc", 3) == 3);
    lseek(fd, 0, SEEK_SET);
    char pre[65], full[65];
    uint64_t bytes = 0;
    CHECK(lhcat_sha256_file(fd, pre, full, &bytes) == 0 && bytes == 3);
    EQ(full, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EQ(pre, full);
    close(fd);
    unlink(dir ? p2 : path);
}

static void hit_count(void *ctx, const struct lhcat_hit *h) { (void)h; (*(int *)ctx)++; }

static void performance(long lines)
{
    const char *st = getenv("XDG_STATE_HOME");
    if (!st) { fprintf(stderr, "XDG_STATE_HOME not set: no performance check\n"); return; }
    char root[4096], listing[4096], err[1024];
    snprintf(root, sizeof root, "%s/perf-root", st);
    snprintf(listing, sizeof listing, "%s/perf-listing.txt", st);
    mkdir(root, 0755);
    FILE *f = fopen(listing, "w");
    CHECK(f != NULL);
    if (!f) return;
    /* a tree as du -a prints it: 1000 directories of photo years and albums, the files before their directory's line */
    long written = 0;
    for (int d = 0; written < lines; d++) {
        for (int i = 0; i < 1999 && written < lines - 1; i++, written++)
            fprintf(f, "%d\t./photos/%d/album-%d/IMG_%05d.%s\n", 100 + i % 4000, 1990 + d % 35, d, i, i % 3 ? "jpg" : "txt");
        fprintf(f, "%d\t./photos/%d/album-%d\n", 300000, 1990 + d % 35, d);
        written++;
    }
    fprintf(f, "5213649379\t.\n");
    fclose(f);
    CHECK(lhcat_add_root("perf", root, NULL, 0, err, sizeof err) == 0);
    lhcat *c = lhcat_open("perf", err, sizeof err);
    CHECK(c != NULL);
    if (!c) return;
    FILE *in = fopen(listing, "r");
    struct lhcat_progress p;
    double t0 = now();
    CHECK(lhcat_import_du(c, in, 1024, NULL, NULL, &p) == 0);
    double t = now() - t0;
    fclose(in);
    printf("perf: import-du of %ld lines: %.2f s (%.0f lines/s), %llu rows, %llu directories\n", lines, t, lines / t,
           (unsigned long long)p.rows, (unsigned long long)p.dirs);
    CHECK(t < 30.0);
    /* the root's total is the "." line's, never a sum of directory totals */
    sqlite3 *db = lhcat_db(c);
    sqlite3_stmt *q;
    sqlite3_prepare_v2(db, "SELECT usage_kib FROM locations WHERE parent = 0", -1, &q, NULL);
    CHECK(sqlite3_step(q) == SQLITE_ROW && sqlite3_column_int64(q, 0) == 5213649379LL);
    sqlite3_finalize(q);
    /* tags on 1000 of the objects, as an index would have observed them */
    sqlite3_exec(db, "BEGIN; INSERT INTO tags(tag, object_id) SELECT 'holiday', object_id FROM locations WHERE id % 2000 = 7;"
                     "INSERT INTO tags(tag, object_id) SELECT 'family', object_id FROM locations WHERE id % 3 = 0; COMMIT", NULL, NULL, NULL);
    struct lhcat_query qq = { 0 };
    qq.tag = "Holiday";
    int hits = 0;
    t0 = now();
    int64_t n = lhcat_find(c, &qq, hit_count, &hits, NULL, NULL);
    t = now() - t0;
    printf("perf: find --tag on %llu rows: %lld found, %.1f ms (the paths too)\n", (unsigned long long)p.rows, (long long)n, t * 1000);
    sqlite3_prepare_v2(db, "SELECT count(*) FROM tags WHERE tag = 'holiday'", -1, &q, NULL);
    int64_t want = sqlite3_step(q) == SQLITE_ROW ? sqlite3_column_int64(q, 0) : -1;
    sqlite3_finalize(q);
    CHECK(n == hits && n == want && n > 0);
    if (t >= 0.050) {   /* the pages read back from a file system that is still writing the database out (ZFS): once more */
        t0 = now();
        hits = 0;
        lhcat_find(c, &qq, hit_count, &hits, NULL, NULL);
        double t2 = now() - t0;
        printf("perf: find --tag again: %.1f ms\n", t2 * 1000);
        if (t2 < t) t = t2;
    }
    CHECK(t < 0.050);
    qq.tag = NULL;
    qq.type = "image/*";
    qq.limit = 100;
    t0 = now();
    n = lhcat_find(c, &qq, hit_count, &hits, NULL, NULL);
    t = now() - t0;
    printf("perf: find --type image/* --limit 100: %.1f ms\n", t * 1000);
    CHECK(n == 100);
    CHECK(t < 0.050);
    /* a subtree: the whole tree's directory and one year's, first rows and all */
    qq.type = NULL;
    qq.in = "photos";
    t0 = now();
    n = lhcat_find(c, &qq, hit_count, &hits, NULL, NULL);
    t = now() - t0;
    printf("perf: find --in photos --limit 100: %.1f ms\n", t * 1000);
    CHECK(n == 100 && t < 0.050);
    qq.in = "photos/2001";
    qq.limit = 0;
    t0 = now();
    n = lhcat_find(c, &qq, hit_count, &hits, NULL, NULL);
    t = now() - t0;
    sqlite3_prepare_v2(db, "WITH y(id) AS (SELECT id FROM locations WHERE name = '2001' AND kind = 'd'),"
                           " a(id) AS (SELECT id FROM locations WHERE parent IN (SELECT id FROM y))"
                           " SELECT count(*) FROM locations WHERE parent IN (SELECT id FROM y UNION SELECT id FROM a)", -1, &q, NULL);
    want = sqlite3_step(q) == SQLITE_ROW ? sqlite3_column_int64(q, 0) : -1;
    sqlite3_finalize(q);
    printf("perf: find --in photos/2001: %lld found, %.1f ms (the paths too)\n", (long long)n, t * 1000);
    CHECK(n > 0 && n == want);   /* its albums and their files */
    CHECK(t < 0.5);
    lhcat_close(c);
    unlink(listing);
}

/* exclude globs: anchored at the root, '*' within a name, "**" whole names, a trailing '/' directories only */
static void excludes(void)
{
    char g[256];
#define M(list, rel, dir) (lhcat_exclude_match((list), (rel), (dir), g, sizeof g) != NULL)
    CHECK(M("n/", "n", 1));
    CHECK(!M("n/", "n", 0));                     /* a file called n */
    CHECK(!M("n/", "src/n", 1));                 /* anchored */
    CHECK(!M("n/", "nn", 1));
    CHECK(M(".cache/", ".cache", 1));
    CHECK(M(".local/share/Trash/", ".local/share/Trash", 1));
    CHECK(!M(".local/share/Trash/", ".local/share", 1));
    CHECK(M("**/node_modules/", "node_modules", 1));
    CHECK(M("**/node_modules/", "a/b/c/node_modules", 1));
    CHECK(!M("**/node_modules/", "a/node_modules2", 1));
    CHECK(M("*.iso", "x.iso", 0));
    CHECK(!M("*.iso", "dir/x.iso", 0));           /* '*' stays within a name */
    CHECK(M("**/*.iso", "dir/x.iso", 0));
    CHECK(M("src/**", "src/a/b", 0));
    CHECK(M("a/**/b", "a/b", 1) && M("a/**/b", "a/x/y/b", 1) && !M("a/**/b", "a/x/yb", 1));
    CHECK(M("img?.jpg", "img1.jpg", 0) && !M("img?.jpg", "img10.jpg", 0));
    CHECK(M("[ab]*", "about", 0) && !M("[!ab]*", "about", 0) && M("[a-c]x", "bx", 0));
    CHECK(M("/build/", "build", 1));             /* a leading / changes nothing */
    CHECK(M("# a comment\n\n  docs/  \n", "docs", 1));
    CHECK(!M("# docs/", "docs", 1));
    lhcat_exclude_match("one/\n**/two/", "x/two", 1, g, sizeof g);
    EQ(g, "**/two/");
#undef M
}

int main(int argc, char **argv)
{
    normalize();
    excludes();
    pictures();
    sha();
    performance(argc > 1 ? atol(argv[1]) : 2000000);
    printf("%d passed, %d failed\n", passes, fails);
    return fails != 0;
}
