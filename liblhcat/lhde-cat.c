/* lhde-cat: the catalogue from the command line (docs/CATALOGUE.md). */
#define _GNU_SOURCE
#include "lhcat.h"
#include <sqlite3.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char *USAGE =
    "usage: lhde-cat COMMAND ...\n"
    "  add-root NAME PATH [--authority filesystem|catalogue] [--offline]   a root over a directory (or a mount);\n"
    "                                            --offline: PATH is not reachable here (a listing of another machine's)\n"
    "  roots                                     the roots and their paths\n"
    "  import-du NAME FILE [--block BYTES]       a du -a listing (FILE - for stdin) as listed-only locations\n"
    "  index NAME [SUBPATH] [--thumbs 128|256] [--self-contained] [--sniff] [--hash] [--budget MB] [--restart] [--quiet]\n"
    "                                            scan the root (or SUBPATH); an unfinished scan resumes\n"
    "  touch PATH...                             observe these files again (after a write)\n"
    "  hash PATH...                              hash these files now\n"
    "  find NAME [--tag T] [--no-tag T [--include-unknown]] [--desc TEXT] [--text WORDS] [--type image/*]\n"
    "           [--since DATE] [--until DATE] [--color C] [--in SUBPATH] [--files|--dirs] [--limit N] [--json] [--quiet]\n"
    "  stats NAME                                rows, coverage, pictures, tags, types, years, sizes\n"
    "  show PATH                                 a location with its object, attributes, relations and journal\n"
    "  set PATH ATTR VALUE [--write|--defer] [--op-id ID] [--base-rev N]\n"
    "  unset PATH ATTR [--write|--defer] [--op-id ID] [--base-rev N]\n"
    "  tag PATH +TAG|-TAG... [--write|--defer] [--op-id ID]   tag_add / tag_remove operations\n"
    "  commit NAME [PATH...]                     write the pending values out\n"
    "  diff NAME                                 where the catalogue and the files differ\n"
    "  conflicts NAME                            the attributes and operations in conflict\n"
    "  resolve PATH ATTR file|catalogue          settle a conflict for the file's value or the catalogue's\n"
    "  journal NAME [--since DATE] [--path PATH] [--json]\n"
    "  relations NAME                            moved / replaced / duplicate objects to confirm\n"
    "ATTR: desc, tags, comment, color, run, author, source, build, notes, todo, or a user.* name.\n"
    "Writes go to the file at once on a root whose authority is the file system, are deferred to commit on one whose\n"
    "authority is the catalogue; --write and --defer choose.\n";

static int usage(void) { fputs(USAGE, stderr); return 2; }
static int fail(const char *fmt, const char *a, const char *b)
{
    fprintf(stderr, "lhde-cat: ");
    fprintf(stderr, fmt, a, b ? b : "");
    fputc('\n', stderr);
    return 1;
}

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec / 1e9;
}

static lhcat *open_name(const char *name)
{
    char err[1024];
    lhcat *c = lhcat_open(name, err, sizeof err);
    if (!c) fprintf(stderr, "lhde-cat: %s\n", err);
    return c;
}

/* the catalogue holding a path (a file that is gone too: its directory resolved) and the path relative to its root */
static lhcat *open_path(const char *path, char *rel, size_t n)
{
    char abs[PATH_MAX], name[LHCAT_NAME_MAX + 1];
    if (!realpath(path, abs)) {
        char dir[PATH_MAX], d[PATH_MAX];
        snprintf(dir, sizeof dir, "%s", path);
        char *slash = strrchr(dir, '/');
        const char *base = slash ? slash + 1 : dir;
        if (slash) *slash = 0;
        if (!realpath(slash ? (dir[0] ? dir : "/") : ".", d)) { fprintf(stderr, "lhde-cat: %s: %s\n", path, strerror(errno)); return NULL; }
        snprintf(abs, sizeof abs, "%s/%s", strcmp(d, "/") ? d : "", base);
    }
    const char *r;
    if (lhcat_root_for(abs, name, sizeof name, &r)) { fprintf(stderr, "lhde-cat: %s is under no catalogue's root\n", abs); return NULL; }
    snprintf(rel, n, "%s", r);
    return open_name(name);
}

static void json_str(FILE *f, const char *s)
{
    if (!s) { fputs("null", f); return; }
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') fprintf(f, "\\%c", *p);
        else if (*p < 0x20) fprintf(f, "\\u%04x", *p);
        else fputc(*p, f);
    }
    fputc('"', f);
}

static volatile sig_atomic_t g_sig;
static void on_signal(int s) { g_sig = s; lhcat_interrupt(); }

/* ---- add-root, roots --------------------------------------------------------------------------------------------- */

static void print_root(void *ctx, const char *name, const char *path)
{
    (void)ctx;
    printf("%s\t%s\n", name, path);
}

/* ---- import-du ---------------------------------------------------------------------------------------------------- */

static double g_t0;
static int g_quiet;

static void du_progress(void *ctx, const struct lhcat_progress *p)
{
    (void)ctx;
    if (g_quiet || !isatty(2)) return;
    double t = now_s() - g_t0;
    fprintf(stderr, "\rimport-du: %llu lines, %llu rows, %.0f lines/s   ", (unsigned long long)p->lines,
            (unsigned long long)p->rows, t > 0 ? p->lines / t : 0);
}

static int cmd_import_du(int argc, char **argv)
{
    if (argc < 2) return usage();
    uint64_t block = 1024;
    for (int i = 2; i < argc; i++)
        if (!strcmp(argv[i], "--block") && i + 1 < argc) block = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--quiet")) g_quiet = 1;
        else return usage();
    lhcat *c = open_name(argv[0]);
    if (!c) return 1;
    FILE *in = strcmp(argv[1], "-") ? fopen(argv[1], "re") : stdin;
    if (!in) { lhcat_close(c); return fail("%s: %s", argv[1], strerror(errno)); }
    struct lhcat_progress p;
    g_t0 = now_s();
    int rc = lhcat_import_du(c, in, block, du_progress, NULL, &p);
    double t = now_s() - g_t0;
    if (!g_quiet && isatty(2)) fputc('\n', stderr);
    if (in != stdin) fclose(in);
    if (rc) fprintf(stderr, "lhde-cat: %s\n", lhcat_error(c));
    else printf("%llu lines, %llu rows (%llu directories), %llu gone, %.1f s, %.0f lines/s\n", (unsigned long long)p.lines,
                (unsigned long long)p.rows, (unsigned long long)p.dirs, (unsigned long long)p.gone, t, t > 0 ? p.lines / t : 0);
    lhcat_close(c);
    return rc ? 1 : 0;
}

/* ---- index, touch, hash ------------------------------------------------------------------------------------------ */

static void index_progress(void *ctx, const struct lhcat_progress *p)
{
    (void)ctx;
    if (g_quiet || !isatty(2)) return;
    fprintf(stderr, "\rindex: %llu entries, %llu read, %llu pictures, %.1f MB read  %.60s\033[K",
            (unsigned long long)p->files, (unsigned long long)p->changed, (unsigned long long)p->pictures,
            p->bytes_read / 1e6, p->current ? p->current : "");
}

static int cmd_index(int argc, char **argv)
{
    if (argc < 1) return usage();
    struct lhcat_index_opts o = { 0 };
    const char *sub = "";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--thumbs") && i + 1 < argc) o.thumbs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--self-contained")) o.self_contained = 1;
        else if (!strcmp(argv[i], "--sniff")) o.sniff = 1;
        else if (!strcmp(argv[i], "--hash")) o.hash = 1;
        else if (!strcmp(argv[i], "--budget") && i + 1 < argc) o.budget = (uint64_t)(atof(argv[++i]) * 1e6);
        else if (!strcmp(argv[i], "--restart")) o.restart = 1;
        else if (!strcmp(argv[i], "--quiet")) g_quiet = 1;
        else if (!strcmp(argv[i], "--thumb-helper") && i + 1 < argc) o.thumb_helper = argv[++i];
        else if (argv[i][0] != '-') sub = argv[i];
        else return usage();
    }
    if (o.thumbs && o.thumbs != 128 && o.thumbs != 256) return fail("--thumbs %s: 128 or 256", argv[0], NULL);
    lhcat *c = open_name(argv[0]);
    if (!c) return 1;
    if (lhcat_offline(c)) { fprintf(stderr, "lhde-cat: %s is offline (%s is not reachable here): import-du only\n", argv[0], lhcat_root_path(c)); lhcat_close(c); return 1; }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    struct lhcat_progress p;
    double t0 = now_s();
    int rc = lhcat_index(c, sub, &o, index_progress, NULL, &p);
    if (!g_quiet && isatty(2)) fputs("\r\033[K", stderr);
    if (rc) fprintf(stderr, "lhde-cat: %s\n", lhcat_error(c));
    else {
        printf("scan %lld%s: %llu entries, %llu read again, %llu gone, %llu related, %llu attributes, %llu pictures, %llu thumbnails,"
               " %llu hashed, %llu conflicts, %.1f MB read, %.1f s\n", (long long)p.scan, p.resumed ? " (resumed)" : "",
               (unsigned long long)p.files, (unsigned long long)p.changed, (unsigned long long)p.gone,
               (unsigned long long)p.related, (unsigned long long)p.attrs, (unsigned long long)p.pictures,
               (unsigned long long)p.thumbs, (unsigned long long)p.hashed, (unsigned long long)p.conflicts,
               p.bytes_read / 1e6, now_s() - t0);
        if (p.skipped_budget) printf("the budget left %llu entries' contents for the next run\n", (unsigned long long)p.skipped_budget);
        if (p.unreadable) printf("%llu directories could not be listed (nothing under them is taken as gone)\n", (unsigned long long)p.unreadable);
        if (p.interrupted) printf("interrupted: the next index resumes\n");
    }
    lhcat_close(c);
    return rc ? 1 : p.interrupted ? 130 : 0;
}

static int cmd_each_path(int argc, char **argv, int hash)
{
    if (argc < 1) return usage();
    int bad = 0;
    for (int i = 0; i < argc; i++) {
        char rel[PATH_MAX];
        lhcat *c = open_path(argv[i], rel, sizeof rel);
        if (!c) { bad = 1; continue; }
        int r = hash ? lhcat_hash(c, rel) : lhcat_touch(c, rel);
        if (r < 0) { fprintf(stderr, "lhde-cat: %s\n", lhcat_error(c)); bad = 1; }
        else if (r == 1) printf("%s: gone\n", argv[i]);
        lhcat_close(c);
    }
    return bad;
}

/* ---- find --------------------------------------------------------------------------------------------------------- */

struct find_ctx { int json, first; };

static void print_hit(void *ctx, const struct lhcat_hit *h)
{
    struct find_ctx *f = ctx;
    if (!f->json) { puts(h->path); return; }
    printf("%s{\"path\":", f->first ? "" : ",\n");
    f->first = 0;
    json_str(stdout, h->path);
    printf(",\"kind\":\"%c\",\"object\":%lld,\"coverage\":%d", h->kind, (long long)h->object, h->coverage);
    if (h->size >= 0) printf(",\"size\":%lld", (long long)h->size);
    if (h->usage_kib >= 0) printf(",\"usage_kib\":%lld", (long long)h->usage_kib);
    if (h->mtime >= 0) printf(",\"mtime\":%lld", (long long)h->mtime);
    printf(",\"type\":");
    json_str(stdout, h->type);
    printf(",\"taken_at\":");
    json_str(stdout, h->taken_at);
    putchar('}');
}

static int cmd_find(int argc, char **argv)
{
    if (argc < 1) return usage();
    struct lhcat_query q = { 0 };
    struct find_ctx f = { 0, 1 };
    int quiet = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char **slot = !strcmp(a, "--tag") ? &q.tag : !strcmp(a, "--no-tag") ? &q.no_tag : !strcmp(a, "--desc") ? &q.desc
                          : !strcmp(a, "--text") ? &q.text : !strcmp(a, "--type") ? &q.type : !strcmp(a, "--since") ? &q.since
                          : !strcmp(a, "--until") ? &q.until : !strcmp(a, "--color") ? &q.color : !strcmp(a, "--in") ? &q.in : NULL;
        if (slot && i + 1 < argc) *slot = argv[++i];
        else if (!strcmp(a, "--limit") && i + 1 < argc) q.limit = atoi(argv[++i]);
        else if (!strcmp(a, "--include-unknown")) q.include_unknown = 1;
        else if (!strcmp(a, "--files")) q.kind = 'f';
        else if (!strcmp(a, "--dirs")) q.kind = 'd';
        else if (!strcmp(a, "--json")) f.json = 1;
        else if (!strcmp(a, "--quiet")) quiet = 1;
        else return usage();
    }
    lhcat *c = open_name(argv[0]);
    if (!c) return 1;
    if (f.json) putchar('[');
    int attr_query = q.tag || q.no_tag || q.desc || q.text || q.color;
    int64_t scope = 0, covered = 0;
    double t0 = now_s();
    int64_t n = lhcat_find(c, &q, print_hit, &f, attr_query && !quiet ? &scope : NULL, attr_query && !quiet ? &covered : NULL);
    double t = now_s() - t0;
    if (f.json) puts("]");
    if (n < 0) fprintf(stderr, "lhde-cat: %s\n", lhcat_error(c));
    else if (!quiet) {
        fprintf(stderr, "%lld found in %.1f ms", (long long)n, t * 1000);
        if (attr_query) fprintf(stderr, "; of %lld locations, %lld have their attributes read", (long long)scope, (long long)covered);
        fputc('\n', stderr);
    }
    lhcat_close(c);
    return n < 0 ? 1 : 0;
}

/* ---- stats -------------------------------------------------------------------------------------------------------- */

static int64_t one(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *st;
    int64_t v = -1;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_type(st, 0) != SQLITE_NULL) v = sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
    }
    return v;
}

static void rows(sqlite3 *db, const char *title, const char *sql)
{
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return;
    printf("%s\n", title);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *k = (const char *)sqlite3_column_text(st, 0);
        printf("  %-40s %12lld\n", k ? k : "(unknown)", (long long)sqlite3_column_int64(st, 1));
    }
    sqlite3_finalize(st);
}

static void size_line(const char *what, int64_t kib)
{
    if (kib < 0) return;
    printf("%-26s %lld KiB (%.2f GiB, %.2f TiB)\n", what, (long long)kib, kib / 1048576.0, kib / 1073741824.0);
}

static int cmd_stats(int argc, char **argv)
{
    if (argc != 1) return usage();
    lhcat *c = open_name(argv[0]);
    if (!c) return 1;
    sqlite3 *db = lhcat_db(c);
    printf("root %s: %s (authority: %s)\n", lhcat_name(c), lhcat_root_path(c), lhcat_catalogue_authority(c) ? "catalogue" : "filesystem");
    printf("%-26s %lld\n", "files", (long long)one(db, "SELECT count(*) FROM locations WHERE state = 'present' AND kind = 'f'"));
    printf("%-26s %lld\n", "directories", (long long)one(db, "SELECT count(*) FROM locations WHERE state = 'present' AND kind = 'd' AND parent != 0"));
    printf("%-26s %lld\n", "other", (long long)one(db, "SELECT count(*) FROM locations WHERE state = 'present' AND kind IN ('l', 'o')"));
    printf("%-26s %lld\n", "gone", (long long)one(db, "SELECT count(*) FROM locations WHERE state = 'gone'"));
    printf("%-26s %lld\n", "objects", (long long)one(db, "SELECT count(*) FROM objects"));
    printf("coverage (present locations)\n");
    static const char *BITS[] = { "listed", "statted", "typed", "attributes read", "exif read", "thumbnail", "hashed" };
    for (int b = 0; b < 7; b++) {
        char sql[160];
        snprintf(sql, sizeof sql, "SELECT count(*) FROM locations WHERE state = 'present' AND parent != 0 AND coverage & %d", 1 << b);
        printf("  %-40s %12lld\n", BITS[b], (long long)one(db, sql));
    }
    printf("%-26s %lld\n", "pictures (facts read)", (long long)one(db, "SELECT count(*) FROM picture"));
    printf("%-26s %lld\n", "  with a date taken", (long long)one(db, "SELECT count(*) FROM picture WHERE taken_at IS NOT NULL"));
    printf("%-26s %lld\n", "tagged objects", (long long)one(db, "SELECT count(DISTINCT object_id) FROM tags"));
    printf("%-26s %lld\n", "objects with attributes", (long long)one(db, "SELECT count(DISTINCT object_id) FROM attrs"));
    printf("%-26s %lld\n", "pending writes", (long long)one(db, "SELECT count(*) FROM attrs WHERE pending = 'write'"));
    printf("%-26s %lld\n", "conflicts", (long long)one(db, "SELECT count(*) FROM attrs WHERE pending = 'conflict'"));
    printf("%-26s %lld\n", "journal operations", (long long)one(db, "SELECT count(*) FROM journal"));
    printf("%-26s %lld\n", "relations to confirm", (long long)one(db, "SELECT count(*) FROM relations WHERE confirmed IS NULL"));
    size_line("disk usage (the root's)", one(db, "SELECT usage_kib FROM locations WHERE parent = 0"));
    size_line("disk usage (files summed)", one(db, "SELECT sum(usage_kib) FROM locations WHERE state = 'present' AND kind = 'f'"));
    int64_t bytes = one(db, "SELECT sum(size) FROM locations WHERE state = 'present' AND kind = 'f'");
    if (bytes > 0) size_line("length (stat'ed files)", (bytes + 1023) / 1024);
    rows(db, "tags", "SELECT tag, count(*) FROM tags GROUP BY tag ORDER BY 2 DESC LIMIT 15");
    rows(db, "types", "SELECT type, count(*) FROM locations WHERE state = 'present' AND kind = 'f' GROUP BY type ORDER BY 2 DESC LIMIT 15");
    rows(db, "years (date taken, else modified)",
         "SELECT substr(coalesce(p.taken_at, datetime(l.mtime, 'unixepoch', 'localtime')), 1, 4) AS y, count(*)"
         " FROM locations l LEFT JOIN picture p ON p.object_id = l.object_id"
         " WHERE l.state = 'present' AND l.kind = 'f' AND (p.taken_at IS NOT NULL OR l.mtime IS NOT NULL) GROUP BY y ORDER BY y");
    struct stat st;
    char dir[PATH_MAX], path[PATH_MAX + 80];
    lhcat_dir(dir, sizeof dir);
    snprintf(path, sizeof path, "%s/%s.db", dir, lhcat_name(c));
    if (stat(path, &st) == 0) printf("%-26s %s (%.1f MB)\n", "database", path, st.st_size / 1e6);
    lhcat_close(c);
    return 0;
}

/* ---- show, journal, diff, conflicts, relations ------------------------------------------------------------------- */

static void print_query(sqlite3 *db, const char *title, const char *sql, int64_t bind, const char *const *cols)
{
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) { fprintf(stderr, "lhde-cat: %s\n", sqlite3_errmsg(db)); return; }
    if (bind) sqlite3_bind_int64(st, 1, bind);
    int any = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (!any && title) printf("%s\n", title);
        any = 1;
        printf(" ");
        for (int i = 0; i < sqlite3_column_count(st); i++) {
            const char *v = (const char *)sqlite3_column_text(st, i);
            if (cols && cols[i]) printf(" %s=", cols[i]); else printf(i ? "\t" : " ");
            printf("%s", v ? v : "-");
        }
        putchar('\n');
    }
    sqlite3_finalize(st);
}

static int cmd_show(int argc, char **argv)
{
    if (argc != 1) return usage();
    char rel[PATH_MAX];
    lhcat *c = open_path(argv[0], rel, sizeof rel);
    if (!c) return 1;
    int64_t id = lhcat_id(c, rel);
    if (!id) { lhcat_close(c); return fail("%s: not in the catalogue", argv[0], NULL); }
    sqlite3 *db = lhcat_db(c);
    int64_t obj = lhcat_object(c, id);
    printf("%s/%s (root %s)\n", lhcat_root_path(c), rel, lhcat_name(c));
    static const char *const LOC[] = { "location", "object", "kind", "state", "size", "usage_kib", "mtime", "type", "dev", "ino",
                                       "coverage", "observed", NULL };
    print_query(db, NULL, "SELECT id, object_id, kind, state, size, usage_kib, datetime(mtime, 'unixepoch', 'localtime'), type,"
                          " dev, ino, coverage, datetime(observed_at / 1000, 'unixepoch', 'localtime') FROM locations WHERE id = ?1", id, LOC);
    static const char *const PIC[] = { "taken", "width", "height", "orientation", "camera", NULL };
    print_query(db, "picture", "SELECT taken_at, width, height, orientation, camera FROM picture WHERE object_id = ?1", obj, PIC);
    static const char *const TH[] = { "variant", "version", "path", NULL };
    print_query(db, "thumbnails", "SELECT variant, version, cache_path FROM thumbs WHERE object_id = ?1", obj, TH);
    static const char *const HS[] = { "algo", "prefix64k", "full", NULL };
    print_query(db, "hashes", "SELECT algo, prefix64k, full FROM hashes WHERE object_id = ?1", obj, HS);
    static const char *const AT[] = { "name", "observed", "desired", "rev", "pending", NULL };
    print_query(db, "attributes", "SELECT name, quote(observed), quote(desired), rev, pending FROM attrs WHERE object_id = ?1 ORDER BY name", obj, AT);
    static const char *const RL[] = { "a", "b", "kind", "confidence", NULL };
    print_query(db, "relations", "SELECT object_a, object_b, kind, confidence FROM relations WHERE object_a = ?1 OR object_b = ?1", obj, RL);
    static const char *const JR[] = { "at", "op", "attr", "value", "old", "base_rev", "status", "host", "program", "op_id", NULL };
    print_query(db, "journal", "SELECT datetime(at / 1000, 'unixepoch', 'localtime'), op, attr, quote(value), quote(old), base_rev, status,"
                               " host, program, op_id FROM journal WHERE object_id = ?1 ORDER BY seq", obj, JR);
    lhcat_close(c);
    return 0;
}

static int cmd_journal(int argc, char **argv)
{
    if (argc < 1) return usage();
    const char *since = NULL, *path = NULL;
    int json = 0;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--since") && i + 1 < argc) since = argv[++i];
        else if (!strcmp(argv[i], "--path") && i + 1 < argc) path = argv[++i];
        else if (!strcmp(argv[i], "--json")) json = 1;
        else return usage();
    lhcat *c = open_name(argv[0]);
    if (!c) return 1;
    sqlite3_stmt *st;
    const char *sql = "SELECT seq, op_id, datetime(at / 1000, 'unixepoch', 'localtime'), host, program, op, attr, path, value, old,"
                      " base_rev, status FROM journal WHERE (?1 IS NULL OR datetime(at / 1000, 'unixepoch', 'localtime') >= ?1)"
                      " AND (?2 IS NULL OR path = ?2) ORDER BY seq";
    if (sqlite3_prepare_v2(lhcat_db(c), sql, -1, &st, NULL) != SQLITE_OK) { lhcat_close(c); return 1; }
    if (since) sqlite3_bind_text(st, 1, since, -1, SQLITE_STATIC);
    if (path) sqlite3_bind_text(st, 2, path, -1, SQLITE_STATIC);
    static const char *const K[] = { "seq", "op_id", "at", "host", "program", "op", "attr", "path", "value", "old", "base_rev", "status" };
    if (json) putchar('[');
    int first = 1;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (json) {
            printf("%s{", first ? "" : ",\n");
            for (int i = 0; i < 12; i++) {
                printf("%s\"%s\":", i ? "," : "", K[i]);
                if (sqlite3_column_type(st, i) == SQLITE_INTEGER) printf("%lld", (long long)sqlite3_column_int64(st, i));
                else json_str(stdout, (const char *)sqlite3_column_text(st, i));
            }
            putchar('}');
        } else {
            const char *v = (const char *)sqlite3_column_text(st, 8), *o = (const char *)sqlite3_column_text(st, 9);
            printf("%s %s %s/%s %s %s %s: %s -> %s [%s] %s\n", sqlite3_column_text(st, 0), sqlite3_column_text(st, 2),
                   sqlite3_column_text(st, 3), sqlite3_column_text(st, 4), sqlite3_column_text(st, 5), sqlite3_column_text(st, 7),
                   sqlite3_column_text(st, 6), o ? o : "(none)", v ? v : "(none)", sqlite3_column_text(st, 11), sqlite3_column_text(st, 1));
        }
        first = 0;
    }
    if (json) puts("]");
    sqlite3_finalize(st);
    lhcat_close(c);
    return 0;
}

/* rows of attrs joined to their present location, printed with the path */
static int attr_list(const char *name, const char *where, const char *title)
{
    lhcat *c = open_name(name);
    if (!c) return 1;
    char sql[512];
    snprintf(sql, sizeof sql, "SELECT l.id, a.name, a.observed, a.desired, a.rev, a.pending FROM attrs a JOIN locations l"
                              " ON l.object_id = a.object_id AND l.state = 'present' WHERE %s ORDER BY l.id, a.name", where);
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(lhcat_db(c), sql, -1, &st, NULL) != SQLITE_OK) { lhcat_close(c); return 1; }
    int n = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *p = lhcat_path(c, sqlite3_column_int64(st, 0));
        const char *o = (const char *)sqlite3_column_text(st, 2), *d = (const char *)sqlite3_column_text(st, 3);
        printf("%s\t%s\tfile: %s\tcatalogue: %s\trev %lld\t%s\n", p ? p : "?", sqlite3_column_text(st, 1), o ? o : "(none)",
               d ? d : "(none)", (long long)sqlite3_column_int64(st, 4), sqlite3_column_text(st, 5));
        n++;
    }
    sqlite3_finalize(st);
    fprintf(stderr, "%d %s\n", n, title);
    if (!strcmp(title, "in conflict")) {   /* the operations that were rejected for their base revision */
        static const char *const JR[] = { "op", "attr", "path", "value", "base_rev", "op_id", NULL };
        print_query(lhcat_db(c), "operations in conflict", "SELECT op, attr, path, quote(value), base_rev, op_id FROM journal"
                                                           " WHERE status = 'conflict' ORDER BY seq", 0, JR);
    }
    lhcat_close(c);
    return 0;
}

static int cmd_relations(int argc, char **argv)
{
    if (argc != 1) return usage();
    lhcat *c = open_name(argv[0]);
    if (!c) return 1;
    sqlite3_stmt *st;
    const char *sql = "SELECT r.kind, r.confidence, r.object_a, r.object_b,"
                      " (SELECT id FROM locations WHERE object_id = r.object_a ORDER BY state = 'present' DESC LIMIT 1),"
                      " (SELECT id FROM locations WHERE object_id = r.object_b ORDER BY state = 'present' DESC LIMIT 1)"
                      " FROM relations r WHERE r.confirmed IS NULL ORDER BY r.noted_at";
    if (sqlite3_prepare_v2(lhcat_db(c), sql, -1, &st, NULL) != SQLITE_OK) { lhcat_close(c); return 1; }
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *a = lhcat_path(c, sqlite3_column_int64(st, 4));
        char *pa = strdup(a ? a : "?");
        const char *b = lhcat_path(c, sqlite3_column_int64(st, 5));
        printf("%s %.1f\t%s (object %lld)\t-> %s (object %lld)\n", sqlite3_column_text(st, 0), sqlite3_column_double(st, 1),
               pa, (long long)sqlite3_column_int64(st, 2), b ? b : "?", (long long)sqlite3_column_int64(st, 3));
        free(pa);
    }
    sqlite3_finalize(st);
    lhcat_close(c);
    return 0;
}

/* ---- set, unset, tag, commit, resolve -------------------------------------------------------------------------- */

static const char *STATUS[] = { "", "applied", "pending", "conflict", "duplicate" };

static void print_status(const char *path, const char *what, int s, const char *op_id)
{
    int base = s & 15, first = s >> 4;
    if (base == LHCAT_DUPLICATE) printf("%s: %s: already applied as %s (%s)\n", path, what, op_id, STATUS[first & 3 ? first : 1]);
    else printf("%s: %s: %s (%s)\n", path, what, STATUS[base], op_id);
}

static int cmd_op(int argc, char **argv, int op)
{
    int need = op == LHCAT_SET ? 3 : 2;   /* PATH ATTR VALUE, PATH ATTR, PATH +TAG */
    if (argc < need) return usage();
    int write = -1;
    const char *op_id = NULL;
    int64_t base = -1;
    const char *tags[64];
    int ntags = 0;
    for (int i = op >= LHCAT_TAG_ADD ? 1 : need; i < argc; i++) {
        if (!strcmp(argv[i], "--write")) write = 1;
        else if (!strcmp(argv[i], "--defer")) write = 0;
        else if (!strcmp(argv[i], "--op-id") && i + 1 < argc) op_id = argv[++i];
        else if (!strcmp(argv[i], "--base-rev") && i + 1 < argc) base = strtoll(argv[++i], NULL, 10);
        else if (op >= LHCAT_TAG_ADD && (argv[i][0] == '+' || argv[i][0] == '-') && argv[i][1] && argv[i][1] != '-' && ntags < 64)
            tags[ntags++] = argv[i];
        else return usage();
    }
    if (op >= LHCAT_TAG_ADD && !ntags) return usage();
    char rel[PATH_MAX];
    lhcat *c = open_path(argv[0], rel, sizeof rel);
    if (!c) return 1;
    if (write < 0) write = !lhcat_catalogue_authority(c);
    int bad = 0;
    char xattr[300];
    if (op < LHCAT_TAG_ADD && !lhcat_attr_name(argv[1], xattr, sizeof xattr)) { lhcat_close(c); return usage(); }
    for (int k = 0; k < (op >= LHCAT_TAG_ADD ? ntags : 1); k++) {
        struct lhcat_op o = { 0 };
        char id[96], out[64];
        if (op_id) {
            if (op >= LHCAT_TAG_ADD && ntags > 1) snprintf(id, sizeof id, "%s.%d", op_id, k); else snprintf(id, sizeof id, "%s", op_id);
            o.op_id = id;
        }
        o.base_rev = base;
        if (op >= LHCAT_TAG_ADD) {
            o.op = tags[k][0] == '+' ? LHCAT_TAG_ADD : LHCAT_TAG_REMOVE;
            o.attr = "user.xdg.tags";
            o.value = tags[k] + 1;
        } else {
            o.op = op;
            o.attr = xattr;
            o.value = op == LHCAT_SET ? argv[2] : NULL;
        }
        int status = 0;
        if (lhcat_apply(c, rel, &o, write, &status, out)) { fprintf(stderr, "lhde-cat: %s\n", lhcat_error(c)); bad = 1; continue; }
        char what[400];
        snprintf(what, sizeof what, "%s %s", op >= LHCAT_TAG_ADD ? (o.op == LHCAT_TAG_ADD ? "tag_add" : "tag_remove") :
                 op == LHCAT_SET ? "set" : "unset", op >= LHCAT_TAG_ADD ? o.value : xattr);
        print_status(argv[0], what, status, out);
        if ((status & 15) == LHCAT_CONFLICT) bad = 3;
        else if ((status & 15) == LHCAT_PENDING && write && lhcat_error(c)[0]) fprintf(stderr, "lhde-cat: %s\n", lhcat_error(c));
    }
    lhcat_close(c);
    return bad;
}

static int cmd_commit(int argc, char **argv)
{
    if (argc < 1) return usage();
    lhcat *c = open_name(argv[0]);
    if (!c) return 1;
    uint64_t written = 0, conflicts = 0;
    int rc = 0;
    if (argc == 1) rc = lhcat_commit(c, NULL, &written, &conflicts);
    else for (int i = 1; i < argc; i++) {
        char rel[PATH_MAX];
        lhcat *d = open_path(argv[i], rel, sizeof rel);
        if (!d) { rc = -1; continue; }
        lhcat_close(d);
        if (lhcat_commit(c, rel, &written, &conflicts)) rc = -1;
    }
    if (rc) fprintf(stderr, "lhde-cat: %s\n", lhcat_error(c));
    printf("%llu written, %llu conflicts\n", (unsigned long long)written, (unsigned long long)conflicts);
    lhcat_close(c);
    return rc ? 1 : conflicts ? 3 : 0;
}

static int cmd_resolve(int argc, char **argv)
{
    if (argc != 3 || (strcmp(argv[2], "file") && strcmp(argv[2], "catalogue"))) return usage();
    char rel[PATH_MAX], xattr[300];
    lhcat *c = open_path(argv[0], rel, sizeof rel);
    if (!c) return 1;
    int rc = lhcat_resolve(c, rel, lhcat_attr_name(argv[1], xattr, sizeof xattr), !strcmp(argv[2], "catalogue"));
    if (rc) fprintf(stderr, "lhde-cat: %s\n", lhcat_error(c));
    lhcat_close(c);
    return rc ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) return usage();
    const char *cmd = argv[1];
    int n = argc - 2;
    char **a = argv + 2;
    if (!strcmp(cmd, "add-root")) {
        if (n < 2) return usage();
        const char *authority = NULL;
        int offline = 0;
        for (int i = 2; i < n; i++)
            if (!strcmp(a[i], "--authority") && i + 1 < n) authority = a[++i];
            else if (!strcmp(a[i], "--offline")) offline = 1;
            else return usage();
        char err[1024];
        if (lhcat_add_root(a[0], a[1], authority, offline, err, sizeof err)) return fail("%s", err, NULL);
        lhcat *c = open_name(a[0]);
        if (!c) return 1;
        int r = lhcat_offline(c) ? 0 : lhcat_touch(c, "");   /* the root's own facts */
        printf("%s\t%s\t(authority: %s%s)\n", a[0], lhcat_root_path(c), lhcat_catalogue_authority(c) ? "catalogue" : "filesystem", lhcat_offline(c) ? ", offline" : "");
        lhcat_close(c);
        return r < 0;
    }
    if (!strcmp(cmd, "roots")) return lhcat_roots(print_root, NULL) < 0;
    if (!strcmp(cmd, "import-du")) return cmd_import_du(n, a);
    if (!strcmp(cmd, "index")) return cmd_index(n, a);
    if (!strcmp(cmd, "touch")) return cmd_each_path(n, a, 0);
    if (!strcmp(cmd, "hash")) return cmd_each_path(n, a, 1);
    if (!strcmp(cmd, "find")) return cmd_find(n, a);
    if (!strcmp(cmd, "stats")) return cmd_stats(n, a);
    if (!strcmp(cmd, "show")) return cmd_show(n, a);
    if (!strcmp(cmd, "set")) return cmd_op(n, a, LHCAT_SET);
    if (!strcmp(cmd, "unset")) return cmd_op(n, a, LHCAT_UNSET);
    if (!strcmp(cmd, "tag")) return cmd_op(n, a, LHCAT_TAG_ADD);
    if (!strcmp(cmd, "commit")) return cmd_commit(n, a);
    if (!strcmp(cmd, "diff")) return n == 1 ? attr_list(a[0], "a.desired IS NOT a.observed OR a.pending != 'none'", "differ") : usage();
    if (!strcmp(cmd, "conflicts")) return n == 1 ? attr_list(a[0], "a.pending = 'conflict'", "in conflict") : usage();
    if (!strcmp(cmd, "resolve")) return cmd_resolve(n, a);
    if (!strcmp(cmd, "journal")) return cmd_journal(n, a);
    if (!strcmp(cmd, "relations")) return cmd_relations(n, a);
    if (!strcmp(cmd, "--help") || !strcmp(cmd, "-h") || !strcmp(cmd, "help")) { fputs(USAGE, stdout); return 0; }
    return usage();
}
