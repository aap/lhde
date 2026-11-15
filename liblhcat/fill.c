/* liblhcat: filling the catalogue: the type by name, a du listing, the walk, touch, the thumbnail helper. */
#define _GNU_SOURCE
#include "internal.h"
#include "liblhde/compat.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

/* ---- a string -> id table (open addressing) ---------------------------------------------------------------------------- */

struct strmap { char **keys; int64_t *vals; size_t cap, n; };

static uint64_t hash_bytes(const char *s, size_t n)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 1099511628211ull; }
    return h;
}

static int64_t map_get(const struct strmap *m, const char *k, size_t n)
{
    if (!m->cap) return 0;
    for (size_t i = hash_bytes(k, n) & (m->cap - 1);; i = (i + 1) & (m->cap - 1)) {
        if (!m->keys[i]) return 0;
        if (!strncmp(m->keys[i], k, n) && m->keys[i][n] == 0) return m->vals[i];
    }
}

static int map_put(struct strmap *m, const char *k, size_t n, int64_t v)
{
    if ((m->n + 1) * 2 > m->cap) {
        size_t cap = m->cap ? m->cap * 2 : 1024;
        char **keys = calloc(cap, sizeof *keys);
        int64_t *vals = calloc(cap, sizeof *vals);
        if (!keys || !vals) { free(keys); free(vals); return -1; }
        for (size_t i = 0; i < m->cap; i++) {
            if (!m->keys[i]) continue;
            size_t j = hash_bytes(m->keys[i], strlen(m->keys[i])) & (cap - 1);
            while (keys[j]) j = (j + 1) & (cap - 1);
            keys[j] = m->keys[i];
            vals[j] = m->vals[i];
        }
        free(m->keys); free(m->vals);
        m->keys = keys; m->vals = vals; m->cap = cap;
    }
    size_t i = hash_bytes(k, n) & (m->cap - 1);
    while (m->keys[i]) {
        if (!strncmp(m->keys[i], k, n) && m->keys[i][n] == 0) { m->vals[i] = v; return 0; }
        i = (i + 1) & (m->cap - 1);
    }
    m->keys[i] = strndup(k, n);
    if (!m->keys[i]) return -1;
    m->vals[i] = v;
    m->n++;
    return 0;
}

static void map_free(struct strmap *m)
{
    for (size_t i = 0; i < m->cap; i++) free(m->keys[i]);
    free(m->keys); free(m->vals);
    memset(m, 0, sizeof *m);
}

/* ---- the type by name: the freedesktop globs ------------------------------------------------------------------------- */
/* "*.EXT" globs only (lower-cased; the case-sensitive ones too), the highest weight per extension, ties to the earlier
   directory ($XDG_DATA_HOME first). Types are interned: lhcat_type_for_name's results live as long as the process. */

struct glob { char *ext; const char *type; int weight; };
static struct glob *g_globs;
static size_t g_nglobs, g_globcap;
static char **g_types;
static size_t g_ntypes;
static int g_globs_loaded;

static const char *intern_type(const char *t)
{
    for (size_t i = 0; i < g_ntypes; i++) if (!strcmp(g_types[i], t)) return g_types[i];
    char **m = realloc(g_types, (g_ntypes + 1) * sizeof *m);
    if (!m) return NULL;
    g_types = m;
    return g_types[g_ntypes++] = strdup(t);
}

static int glob_cmp(const void *a, const void *b) { return strcmp(((const struct glob *)a)->ext, ((const struct glob *)b)->ext); }

static void read_globs(const char *dir, struct strmap *seen)
{
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/mime/globs2", dir);
    FILE *f = fopen(path, "re");
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        line[strcspn(line, "\r\n")] = 0;
        char *w = line, *t = strchr(w, ':');
        if (!t) continue;
        *t++ = 0;
        char *g = strchr(t, ':');
        if (!g) continue;
        *g++ = 0;
        char *flags = strchr(g, ':');
        if (flags) *flags = 0;
        if (g[0] != '*' || g[1] != '.' || !g[2] || strpbrk(g + 2, "*?[")) continue;
        /* a case-sensitive glob ("*.C" C++, "*.c" C) is kept as written behind a \1, the others in lower case */
        int cs = flags && strstr(flags + 1, "cs");
        char ext[256];
        size_t n = 0;
        if (cs) ext[n++] = 1;
        for (const char *p = g + 2; *p && n < sizeof ext - 1; p++) ext[n++] = cs ? *p : (char)tolower((unsigned char)*p);
        ext[n] = 0;
        int weight = atoi(w);
        int64_t at = map_get(seen, ext, n);
        if (at) {   /* an earlier directory's or this one's: only a higher weight replaces it */
            if (weight > g_globs[at - 1].weight) { g_globs[at - 1].type = intern_type(t); g_globs[at - 1].weight = weight; }
            continue;
        }
        if (g_nglobs == g_globcap) {
            g_globcap = g_globcap ? g_globcap * 2 : 1024;
            struct glob *m = realloc(g_globs, g_globcap * sizeof *m);
            if (!m) break;
            g_globs = m;
        }
        g_globs[g_nglobs].ext = strdup(ext);
        g_globs[g_nglobs].type = intern_type(t);
        g_globs[g_nglobs].weight = weight;
        g_nglobs++;
        map_put(seen, ext, n, (int64_t)g_nglobs);
    }
    fclose(f);
}

static void load_globs(void)
{
    if (g_globs_loaded) return;
    g_globs_loaded = 1;
    struct strmap seen = { 0 };
    char home[PATH_MAX];
    const char *dh = getenv("XDG_DATA_HOME");
    if (dh && *dh) snprintf(home, sizeof home, "%s", dh);
    else snprintf(home, sizeof home, "%s/.local/share", getenv("HOME") ? getenv("HOME") : "");
    read_globs(home, &seen);
    const char *dirs = getenv("XDG_DATA_DIRS");
    if (!dirs || !*dirs) dirs = "/usr/local/share:/usr/share";
    char *copy = strdup(dirs);
    for (char *s = copy, *e; s && *s; s = e ? e + 1 : NULL) {
        e = strchr(s, ':');
        if (e) *e = 0;
        if (*s) read_globs(s, &seen);
        if (!e) break;
    }
    free(copy);
    map_free(&seen);
    if (g_nglobs) qsort(g_globs, g_nglobs, sizeof *g_globs, glob_cmp);
}

const char *lhcat_type_for_name(const char *name)
{
    load_globs();
    if (!g_nglobs || !name) return NULL;
    /* the longest extension first: "a.tar.gz" tries "tar.gz", then "gz"; a case-sensitive glob before the others */
    for (const char *p = strchr(name, '.'); p; p = strchr(p + 1, '.')) {
        if (!p[1]) break;
        char ext[256];
        size_t n = 1;
        ext[0] = 1;
        for (const char *q = p + 1; *q && n < sizeof ext - 1; q++) ext[n++] = *q;
        ext[n] = 0;
        struct glob key = { ext, NULL, 0 };
        struct glob *g = bsearch(&key, g_globs, g_nglobs, sizeof *g_globs, glob_cmp);
        if (g) return g->type;
        for (size_t i = 1; i < n; i++) ext[i] = (char)tolower((unsigned char)ext[i]);
        key.ext = ext + 1;
        g = bsearch(&key, g_globs, g_nglobs, sizeof *g_globs, glob_cmp);
        if (g) return g->type;
    }
    return NULL;
}

const char *lhcat_sniff(const unsigned char *b, size_t n)
{
    if (n >= 3 && b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF) return "image/jpeg";
    if (n >= 8 && !memcmp(b, "\x89PNG\r\n\x1a\n", 8)) return "image/png";
    if (n >= 6 && (!memcmp(b, "GIF87a", 6) || !memcmp(b, "GIF89a", 6))) return "image/gif";
    if (n >= 12 && !memcmp(b, "RIFF", 4) && !memcmp(b + 8, "WEBP", 4)) return "image/webp";
    if (n >= 4 && (!memcmp(b, "II*\0", 4) || !memcmp(b, "MM\0*", 4))) return "image/tiff";
    if (n >= 14 && b[0] == 'B' && b[1] == 'M') return "image/bmp";
    if (n >= 12 && !memcmp(b + 4, "ftyp", 4)) {
        if (!memcmp(b + 8, "heic", 4) || !memcmp(b + 8, "heix", 4) || !memcmp(b + 8, "mif1", 4)) return "image/heif";
        if (!memcmp(b + 8, "avif", 4)) return "image/avif";
    }
    return NULL;
}

static int is_picture(const char *type)
{
    static const char *const T[] = { "image/jpeg", "image/png", "image/gif", "image/webp", "image/bmp", "image/tiff" };
    if (!type) return 0;
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) if (!strcmp(type, T[i])) return 1;
    return 0;
}

/* ---- the walk's state -------------------------------------------------------------------------------------------------- */

struct lhcat__walk {
    struct lhcat_index_opts o;
    struct lhcat_progress p;
    lhcat_progress_fn fn;
    void *ctx;
    int64_t scan, started;              /* the scan and its start (ms) */
    int64_t last_commit, last_progress;
    dev_t dev;
    char *cursor;                       /* resuming: the last entry the scan committed */
    char last[PATH_MAX];                /* the last entry done (with what is under it) */
    const char *sub;
    sqlite3_stmt *sel, *seen, *facts, *newobj, *relate, *moves, *pic, *nopic, *thumb, *nothumb, *hash, *dups, *orphan;
    pid_t helper;
    FILE *to_helper, *from_helper;
    int helper_failed;
    int64_t commit_ms;                  /* between commits ($LHCAT_COMMIT_MS, default 2000) */
    uint64_t stop_after;                /* $LHCAT_TEST_STOP=N or kill:N: interrupt, or SIGKILL, after N entries (tests) */
    int kill;
};

static volatile sig_atomic_t g_stop;
void lhcat_interrupt(void) { g_stop = 1; }

static int walk_init(lhcat *c, struct lhcat__walk *w, const struct lhcat_index_opts *o)
{
    memset(w, 0, sizeof *w);
    if (o) w->o = *o;
    w->started = lhcat__now_ms();
    w->helper = -1;
    const char *e = getenv("LHCAT_COMMIT_MS");
    w->commit_ms = e && *e ? atoll(e) : 2000;
    if ((e = getenv("LHCAT_TEST_STOP")) && *e) {
        w->kill = !strncmp(e, "kill:", 5);
        w->stop_after = strtoull(w->kill ? e + 5 : e, NULL, 10);
    }
    w->sel = lhcat__prepare(c, "SELECT id, object_id, size, mtime, ctime, ino, dev, kind, coverage, state FROM locations"
                               " WHERE parent = ?1 AND name = ?2");
    w->seen = lhcat__prepare(c, "UPDATE locations SET observed_at = ?2 WHERE id = ?1");
    w->facts = lhcat__prepare(c,
        "UPDATE locations SET object_id = ?2, kind = ?3, size = ?4, mtime = ?5, ctime = ?6, dev = ?7, ino = ?8, type = ?9,"
        " observed_at = ?10, state = 'present', coverage = ?11 WHERE id = ?1");
    w->newobj = lhcat__prepare(c, "INSERT INTO objects(created_at) VALUES(?1)");
    w->relate = lhcat__prepare(c, "INSERT OR REPLACE INTO relations(object_a, object_b, kind, confidence, noted_at)"
                                  " VALUES(?1, ?2, ?3, ?4, ?5)");
    w->moves = lhcat__prepare(c, "SELECT id, object_id, size, mtime FROM locations WHERE ino = ?1 AND dev = ?2 AND kind = 'f'"
                                 " AND id != ?3 LIMIT 8");
    w->pic = lhcat__prepare(c, "INSERT OR REPLACE INTO picture(object_id, taken_at, width, height, orientation, camera)"
                               " VALUES(?1, ?2, ?3, ?4, ?5, ?6)");
    w->nopic = lhcat__prepare(c, "DELETE FROM picture WHERE object_id = ?1");
    w->thumb = lhcat__prepare(c, "INSERT OR REPLACE INTO thumbs(object_id, variant, version, uri, cache_path, made_at)"
                                 " VALUES(?1, ?2, ?3, ?4, ?5, ?6)");
    w->nothumb = lhcat__prepare(c, "DELETE FROM thumbs WHERE object_id = ?1");
    w->hash = lhcat__prepare(c, "INSERT OR REPLACE INTO hashes(object_id, algo, prefix64k, full, computed_at, version)"
                                " VALUES(?1, 'sha256', ?2, ?3, ?4, ?5)");
    w->dups = lhcat__prepare(c, "SELECT object_id FROM hashes WHERE full = ?1 AND object_id != ?2 LIMIT 16");
    /* the writes a replaced file's old object was waiting for: the user decides */
    w->orphan = lhcat__prepare(c, "UPDATE attrs SET pending = 'conflict' WHERE object_id = ?1 AND pending = 'write'");
    if (!w->sel || !w->seen || !w->facts || !w->newobj || !w->relate || !w->moves || !w->pic || !w->nopic || !w->thumb ||
        !w->nothumb || !w->hash || !w->dups || !w->orphan)
        return -1;
    return lhcat__exec(c, "CREATE TEMP TABLE IF NOT EXISTS lhcat_gone(id INTEGER PRIMARY KEY)");
}

static void helper_stop(struct lhcat__walk *w)
{
    if (w->to_helper) fclose(w->to_helper);
    if (w->from_helper) fclose(w->from_helper);
    w->to_helper = w->from_helper = NULL;
    if (w->helper > 0) { int s; waitpid(w->helper, &s, 0); }
    w->helper = -1;
}

static void walk_done(struct lhcat__walk *w)
{
    sqlite3_stmt *all[] = { w->sel, w->seen, w->facts, w->newobj, w->relate, w->moves, w->pic, w->nopic, w->thumb,
                            w->nothumb, w->hash, w->dups, w->orphan };
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) sqlite3_finalize(all[i]);
    helper_stop(w);
    free(w->cursor);
}

/* ---- the thumbnail helper (flxp-thumb, a coprocess: a path a line in, "made|cached PATH" or "fail" a line out) ----- */

static int helper_start(struct lhcat__walk *w)
{
    if (w->helper > 0) return 0;
    if (w->helper_failed) return -1;
    char prog[PATH_MAX];
    if (w->o.thumb_helper && *w->o.thumb_helper) snprintf(prog, sizeof prog, "%s", w->o.thumb_helper);
    else {
        char self[PATH_MAX];
        prog[0] = 0;
        if (lhde_self_exe(self, sizeof self) > 0) {
            char *s = strrchr(self, '/');
            if (s) { *s = 0; snprintf(prog, sizeof prog, "%s/flxp-thumb", self); }
            if (access(prog, X_OK) != 0) prog[0] = 0;
        }
        if (!prog[0]) snprintf(prog, sizeof prog, "flxp-thumb");
    }
    int in[2], out[2];
    if (pipe2(in, O_CLOEXEC)) { w->helper_failed = 1; return -1; }
    if (pipe2(out, O_CLOEXEC)) { close(in[0]); close(in[1]); w->helper_failed = 1; return -1; }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, in[0], 0);
    posix_spawn_file_actions_adddup2(&fa, out[1], 1);
    char size[16];
    snprintf(size, sizeof size, "%d", w->o.thumbs);
    char *argv[] = { prog, "--size", size, "-", NULL };
    pid_t pid;
    int rc = posix_spawnp(&pid, prog, &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(in[0]);
    close(out[1]);
    if (rc != 0) { close(in[1]); close(out[0]); w->helper_failed = 1; return -1; }
    w->helper = pid;
    w->to_helper = fdopen(in[1], "w");
    w->from_helper = fdopen(out[0], "r");
    return w->to_helper && w->from_helper ? 0 : -1;
}

/* 1 a thumbnail (its path into out; *made when it was made, not read from the cache), 0 none, -1 no helper */
static int helper_thumb(struct lhcat__walk *w, const char *abs, char *out, size_t n, int *made)
{
    if (strchr(abs, '\n') || helper_start(w)) return -1;
    signal(SIGPIPE, SIG_IGN);
    if (fprintf(w->to_helper, "%s\n", abs) < 0 || fflush(w->to_helper)) { helper_stop(w); w->helper_failed = 1; return -1; }
    char line[PATH_MAX + 16];
    if (!fgets(line, sizeof line, w->from_helper)) { helper_stop(w); w->helper_failed = 1; return -1; }
    line[strcspn(line, "\n")] = 0;
    *made = !strncmp(line, "made\t", 5);
    if (*made || !strncmp(line, "cached\t", 7)) { snprintf(out, n, "%s", strchr(line, '\t') + 1); return 1; }
    return 0;
}

/* the freedesktop cache's key: file:// and the path, bytes outside the unreserved set as %XX */
static void file_uri(const char *abs, char *out, size_t n)
{
    size_t o = (size_t)snprintf(out, n, "file://");
    for (const unsigned char *p = (const unsigned char *)abs; *p && o + 4 < n; p++) {
        if (isalnum(*p) || strchr("/-._~!$&'()*+,;=:@", *p)) out[o++] = (char)*p;
        else o += (size_t)snprintf(out + o, n - o, "%%%02X", *p);
    }
    out[o < n ? o : n - 1] = 0;
}

/* a copy of src at dst (through a temporary file); 0 on success */
static int copy_file(const char *src, const char *dst)
{
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s.tmp%d", dst, (int)getpid());
    int in = open(src, O_RDONLY | O_CLOEXEC), out = in >= 0 ? open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644) : -1;
    int rc = in >= 0 && out >= 0 ? 0 : -1;
    char buf[65536];
    ssize_t k;
    while (!rc && (k = read(in, buf, sizeof buf)) > 0) if (write(out, buf, (size_t)k) != k) rc = -1;
    if (in >= 0) close(in);
    if (out >= 0 && close(out)) rc = -1;
    if (!rc && rename(tmp, dst)) rc = -1;
    if (rc) unlink(tmp);
    return rc;
}

/* ---- one entry ------------------------------------------------------------------------------------------------------- */

static int64_t ns_of(const struct timespec *t) { return (int64_t)t->tv_sec * 1000000000 + t->tv_nsec; }

static int relate(lhcat *c, struct lhcat__walk *w, int64_t a, int64_t b, const char *kind, double confidence)
{
    sqlite3_reset(w->relate);
    sqlite3_bind_int64(w->relate, 1, a);
    sqlite3_bind_int64(w->relate, 2, b);
    sqlite3_bind_text(w->relate, 3, kind, -1, SQLITE_STATIC);
    sqlite3_bind_double(w->relate, 4, confidence);
    sqlite3_bind_int64(w->relate, 5, lhcat__now_ms() / 1000);
    int ok = sqlite3_step(w->relate) == SQLITE_DONE;
    sqlite3_reset(w->relate);
    if (!ok) return lhcat__dbfail(c, "relation");
    w->p.related++;
    return 0;
}

static int64_t new_object(lhcat *c, struct lhcat__walk *w)
{
    sqlite3_reset(w->newobj);
    sqlite3_bind_int64(w->newobj, 1, lhcat__now_ms() / 1000);
    int ok = sqlite3_step(w->newobj) == SQLITE_DONE;
    sqlite3_reset(w->newobj);
    if (!ok) { lhcat__dbfail(c, "object"); return 0; }
    return sqlite3_last_insert_rowid(c->db);
}

static int budget_left(const struct lhcat__walk *w) { return !w->o.budget || w->p.bytes_read < w->o.budget; }

int lhcat__entry(lhcat *c, struct lhcat__walk *w, int64_t parent, const char *name, const char *rel, const char *abs,
                 const struct stat *st, int force, int64_t *idp)
{
    char kind = S_ISREG(st->st_mode) ? 'f' : S_ISDIR(st->st_mode) ? 'd' : S_ISLNK(st->st_mode) ? 'l' : 'o';
    int64_t ctime_ns = ns_of(&st->st_ctim);
    const char *type = kind == 'd' ? "inode/directory" : kind == 'l' ? "inode/symlink" : kind == 'f' ? lhcat_type_for_name(name) : NULL;
    int64_t id = 0, object = 0;
    int coverage = 0, had_ino = 0, same_file = 0, present = 0;
    int pict = kind == 'f' && is_picture(type);
    int want = LHCAT_STATTED | LHCAT_ATTRS | (pict ? LHCAT_EXIF : 0) | (pict && w->o.thumbs ? LHCAT_THUMB : 0) |
               (kind == 'f' && w->o.hash ? LHCAT_HASHED : 0);
    sqlite3_reset(w->sel);
    sqlite3_bind_int64(w->sel, 1, parent);
    sqlite3_bind_text(w->sel, 2, name, -1, SQLITE_STATIC);
    if (sqlite3_step(w->sel) == SQLITE_ROW) {
        id = sqlite3_column_int64(w->sel, 0);
        object = sqlite3_column_int64(w->sel, 1);
        coverage = sqlite3_column_int(w->sel, 8);
        const char *k = (const char *)sqlite3_column_text(w->sel, 7);
        const char *state = (const char *)sqlite3_column_text(w->sel, 9);
        present = state && !strcmp(state, "present");
        had_ino = sqlite3_column_type(w->sel, 5) != SQLITE_NULL;
        same_file = (k && k[0] == kind) && (!had_ino || (sqlite3_column_int64(w->sel, 5) == (int64_t)st->st_ino &&
                                                         sqlite3_column_int64(w->sel, 6) == (int64_t)st->st_dev));
        int same = same_file && had_ino && present &&
                   sqlite3_column_int64(w->sel, 2) == (int64_t)st->st_size &&
                   sqlite3_column_int64(w->sel, 3) == (int64_t)st->st_mtime &&
                   sqlite3_column_int64(w->sel, 4) == ctime_ns;
        sqlite3_reset(w->sel);
        if (same && !force && (coverage & want) == want) {
            sqlite3_reset(w->seen);
            sqlite3_bind_int64(w->seen, 1, id);
            sqlite3_bind_int64(w->seen, 2, w->started);
            sqlite3_step(w->seen);
            sqlite3_reset(w->seen);
            *idp = id;
            return 0;
        }
        if (!same) coverage &= LHCAT_LISTED;   /* the content may have changed: read it again */
    }
    sqlite3_reset(w->sel);
    if (id && !same_file) {
        /* another file at this path (a new inode, another kind): a new object, related to the old one */
        int64_t old = object;
        if (!(object = new_object(c, w))) return -1;
        if (relate(c, w, old, object, "replaced", 0.5)) return -1;
        sqlite3_reset(w->orphan);
        sqlite3_bind_int64(w->orphan, 1, old);
        sqlite3_step(w->orphan);
        sqlite3_reset(w->orphan);
        coverage &= LHCAT_LISTED;
    }
    if (!id) {
        if (!lhcat__new_location(c, parent, name, strlen(name), kind, &id)) return -1;
        object = lhcat_object(c, id);
        if (kind == 'f') {   /* the inode at another path whose file is gone: probably moved here */
            sqlite3_reset(w->moves);
            sqlite3_bind_int64(w->moves, 1, (int64_t)st->st_ino);
            sqlite3_bind_int64(w->moves, 2, (int64_t)st->st_dev);
            sqlite3_bind_int64(w->moves, 3, id);
            struct { int64_t loc, obj; int same; } cand[8];
            int nc = 0;
            while (nc < 8 && sqlite3_step(w->moves) == SQLITE_ROW) {
                cand[nc].loc = sqlite3_column_int64(w->moves, 0);
                cand[nc].obj = sqlite3_column_int64(w->moves, 1);
                cand[nc].same = sqlite3_column_int64(w->moves, 2) == (int64_t)st->st_size &&
                                sqlite3_column_int64(w->moves, 3) == (int64_t)st->st_mtime;
                nc++;
            }
            sqlite3_reset(w->moves);
            for (int i = 0; i < nc; i++) {
                const char *old = lhcat_path(c, cand[i].loc);
                char oabs[PATH_MAX];
                struct stat os;
                if (!old) continue;
                lhcat__abs(c, old, oabs, sizeof oabs);
                if (lstat(oabs, &os) == 0 && os.st_ino == st->st_ino && os.st_dev == st->st_dev) continue;   /* a hard link */
                if (relate(c, w, cand[i].obj, object, "moved", cand[i].same ? 0.9 : 0.6)) return -1;
            }
        }
    }
    char head[64];
    if (kind == 'f' && !type && w->o.sniff && st->st_size > 0 && budget_left(w)) {
        int fd = open(abs, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
        if (fd >= 0) {
            ssize_t k = read(fd, head, sizeof head);
            if (k > 0) { w->p.bytes_read += (uint64_t)k; type = lhcat_sniff((const unsigned char *)head, (size_t)k); }
            close(fd);
        }
        pict = is_picture(type);
    }
    coverage |= LHCAT_STATTED | (type ? LHCAT_TYPED : 0);
    w->p.changed++;
    *idp = id;

    char **nv;
    int n;
    if (lhcat__read_xattrs(abs, &nv, &n) >= 0) {   /* an unreadable list: no observation (nothing is taken as removed) */
        int r = lhcat__observe(c, object, rel, nv, n);
        lhcat__free_nv(nv, n);
        if (r < 0) return -1;
        w->p.conflicts += (uint64_t)r;
        w->p.attrs += (uint64_t)n;
        coverage |= LHCAT_ATTRS;
    }

    if (kind == 'f' && !(coverage & LHCAT_EXIF) && pict) {
        sqlite3_reset(w->nopic);
        sqlite3_bind_int64(w->nopic, 1, object);
        sqlite3_step(w->nopic);
        sqlite3_reset(w->nopic);
        if (!budget_left(w)) w->p.skipped_budget++;
        else {
            struct lhcat_picture pic;
            int fd = open(abs, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
            int r = fd >= 0 ? lhcat_picture_read(fd, type, &pic, &w->p.bytes_read) : -1;
            if (fd >= 0) close(fd);
            if (r >= 0) {
                sqlite3_stmt *q = w->pic;
                sqlite3_reset(q);
                sqlite3_bind_int64(q, 1, object);
                if (pic.taken_at[0]) sqlite3_bind_text(q, 2, pic.taken_at, -1, SQLITE_TRANSIENT); else sqlite3_bind_null(q, 2);
                if (pic.width) sqlite3_bind_int(q, 3, pic.width); else sqlite3_bind_null(q, 3);
                if (pic.height) sqlite3_bind_int(q, 4, pic.height); else sqlite3_bind_null(q, 4);
                if (pic.orientation) sqlite3_bind_int(q, 5, pic.orientation); else sqlite3_bind_null(q, 5);
                if (pic.camera[0]) sqlite3_bind_text(q, 6, pic.camera, -1, SQLITE_TRANSIENT); else sqlite3_bind_null(q, 6);
                int ok = sqlite3_step(q) == SQLITE_DONE;
                sqlite3_reset(q);
                if (!ok) return lhcat__dbfail(c, "picture");
                w->p.pictures++;
                coverage |= LHCAT_EXIF;
            }
        }
    }
    if (pict && w->o.thumbs && !(coverage & LHCAT_THUMB)) {
        if (!budget_left(w)) w->p.skipped_budget++;
        else {
            char tp[PATH_MAX], uri[PATH_MAX * 3], version[64];
            int made = 0;
            int t = helper_thumb(w, abs, tp, sizeof tp, &made);
            if (t >= 0) {   /* none is kept too (path NULL): not asked again until the file changes */
                if (t > 0 && w->o.self_contained) {   /* a copy beside the database: the catalogue copies whole */
                    char dir[PATH_MAX], dst[PATH_MAX + 64];
                    lhcat_dir(dir, sizeof dir);
                    snprintf(dir + strlen(dir), sizeof dir - strlen(dir), "/%s.thumbs", c->name);
                    mkdir(dir, 0755);
                    snprintf(dst, sizeof dst, "%s/%lld-%d.png", dir, (long long)object, w->o.thumbs);
                    if (copy_file(tp, dst) == 0) snprintf(tp, sizeof tp, "%s", dst);
                }
                file_uri(abs, uri, sizeof uri);
                snprintf(version, sizeof version, "%lld:%lld", (long long)st->st_size, (long long)st->st_mtime);
                sqlite3_stmt *q = w->thumb;
                sqlite3_reset(q);
                sqlite3_bind_int64(q, 1, object);
                sqlite3_bind_int(q, 2, w->o.thumbs);
                sqlite3_bind_text(q, 3, version, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(q, 4, uri, -1, SQLITE_TRANSIENT);
                if (t > 0) sqlite3_bind_text(q, 5, tp, -1, SQLITE_TRANSIENT); else sqlite3_bind_null(q, 5);
                sqlite3_bind_int64(q, 6, lhcat__now_ms() / 1000);
                sqlite3_step(q);
                sqlite3_reset(q);
                if (t > 0) w->p.thumbs++;
                if (made) w->p.bytes_read += (uint64_t)st->st_size;
                coverage |= LHCAT_THUMB;
            }
        }
    }
    if (kind == 'f' && w->o.hash && !(coverage & LHCAT_HASHED)) {
        if (!budget_left(w)) w->p.skipped_budget++;
        else {
            char pre[65], full[65], version[64];
            int fd = open(abs, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
            if (fd >= 0 && lhcat_sha256_file(fd, pre, full, &w->p.bytes_read) == 0) {
                snprintf(version, sizeof version, "%lld:%lld", (long long)st->st_size, (long long)st->st_mtime);
                sqlite3_reset(w->hash);
                sqlite3_bind_int64(w->hash, 1, object);
                sqlite3_bind_text(w->hash, 2, pre, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(w->hash, 3, full, -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(w->hash, 4, lhcat__now_ms() / 1000);
                sqlite3_bind_text(w->hash, 5, version, -1, SQLITE_TRANSIENT);
                sqlite3_step(w->hash);
                sqlite3_reset(w->hash);
                coverage |= LHCAT_HASHED;
                w->p.hashed++;
                int64_t other[16];
                int no = 0;
                sqlite3_reset(w->dups);
                sqlite3_bind_text(w->dups, 1, full, -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(w->dups, 2, object);
                while (no < 16 && sqlite3_step(w->dups) == SQLITE_ROW) other[no++] = sqlite3_column_int64(w->dups, 0);
                sqlite3_reset(w->dups);
                for (int i = 0; i < no; i++) if (relate(c, w, other[i], object, "duplicate", 1.0)) { close(fd); return -1; }
            }
            if (fd >= 0) close(fd);
        }
    }
    if (!pict || !(coverage & LHCAT_THUMB)) {
        if (!pict) {
            sqlite3_reset(w->nothumb);
            sqlite3_bind_int64(w->nothumb, 1, object);
            sqlite3_step(w->nothumb);
            sqlite3_reset(w->nothumb);
        }
    }
    sqlite3_stmt *f = w->facts;
    sqlite3_reset(f);
    sqlite3_bind_int64(f, 1, id);
    sqlite3_bind_int64(f, 2, object);
    char ks[2] = { kind, 0 };
    sqlite3_bind_text(f, 3, ks, 1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(f, 4, (int64_t)st->st_size);
    sqlite3_bind_int64(f, 5, (int64_t)st->st_mtime);
    sqlite3_bind_int64(f, 6, ctime_ns);
    sqlite3_bind_int64(f, 7, (int64_t)st->st_dev);
    sqlite3_bind_int64(f, 8, (int64_t)st->st_ino);
    if (type) sqlite3_bind_text(f, 9, type, -1, SQLITE_STATIC); else sqlite3_bind_null(f, 9);
    sqlite3_bind_int64(f, 10, lhcat__now_ms() > w->started ? lhcat__now_ms() : w->started);
    sqlite3_bind_int(f, 11, coverage);
    int ok = sqlite3_step(f) == SQLITE_DONE;
    sqlite3_reset(f);
    return ok ? 0 : lhcat__dbfail(c, rel);
}

/* ---- the walk -------------------------------------------------------------------------------------------------------- */

/* the walk's order: names sorted bytewise in each directory, a directory's entries right after it; as a path order,
   a byte compare where '/' comes before every other byte */
static int walk_cmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        unsigned x = (unsigned char)*a, y = (unsigned char)*b;
        if (x == '/') x = 1;
        if (y == '/') y = 1;
        if (x != y || !x) return (int)x - (int)y;
    }
}

static int is_ancestor(const char *dir, const char *path)
{
    size_t n = strlen(dir);
    return !n || (!strncmp(dir, path, n) && path[n] == '/');
}

static int name_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static int scan_update(lhcat *c, struct lhcat__walk *w, const char *status, const char *cursor)
{
    sqlite3_stmt *st = lhcat__prepare(c, "UPDATE scans SET status = ?2, cursor = ?3, entries = ?4,"
                                         " finished = CASE WHEN ?2 = 'complete' THEN ?5 END WHERE id = ?1");
    if (!st) return -1;
    sqlite3_bind_int64(st, 1, w->scan);
    sqlite3_bind_text(st, 2, status, -1, SQLITE_STATIC);
    if (cursor) sqlite3_bind_text(st, 3, cursor, -1, SQLITE_TRANSIENT); else sqlite3_bind_null(st, 3);
    sqlite3_bind_int64(st, 4, (int64_t)w->p.files);
    sqlite3_bind_int64(st, 5, lhcat__now_ms());
    int ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok ? 0 : lhcat__dbfail(c, "scan");
}

static void progress(struct lhcat__walk *w, const char *rel, int now)
{
    int64_t t = lhcat__now_ms();
    if (!w->fn || (!now && t - w->last_progress < 250)) return;
    w->last_progress = t;
    w->p.current = rel;
    w->fn(w->ctx, &w->p);
    w->p.current = NULL;
}

static int maybe_commit(lhcat *c, struct lhcat__walk *w, const char *rel)
{
    int64_t t = lhcat__now_ms();
    if (t - w->last_commit < w->commit_ms) return 0;
    w->last_commit = t;
    if (scan_update(c, w, "running", rel) || lhcat__exec(c, "COMMIT") || lhcat__exec(c, "BEGIN IMMEDIATE")) return -1;
    return 0;
}

static void join(char *out, size_t n, const char *a, const char *b)
{
    if (!*a) snprintf(out, n, "%s", b);
    else snprintf(out, n, "%s/%s", a, b);
}

/* 1 stopped (interrupted), 0 done, -1 an error */
static int walk_dir(lhcat *c, struct lhcat__walk *w, int64_t dir, const char *rel, const char *abs)
{
    DIR *d = opendir(abs);
    if (!d) { w->p.unreadable++; return 0; }   /* not listed: nothing under it is taken as gone */
    char **names = NULL;
    size_t n = 0, cap = 0;
    struct dirent *e;
    int whole = 1;
    errno = 0;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 64;
            char **m = realloc(names, cap * sizeof *m);
            if (!m) { whole = 0; break; }
            names = m;
        }
        if ((names[n] = strdup(e->d_name))) n++; else whole = 0;
    }
    if (errno) whole = 0;
    closedir(d);
    if (n) qsort(names, n, sizeof *names, name_cmp);
    int rc = 0;
    for (size_t i = 0; i < n && rc == 0; i++) {
        if (g_stop) { rc = 1; w->p.interrupted = 1; break; }
        char crel[PATH_MAX], cabs[PATH_MAX];
        join(crel, sizeof crel, rel, names[i]);
        snprintf(cabs, sizeof cabs, "%s/%s", abs, names[i]);
        if (w->cursor) {
            if (is_ancestor(crel, w->cursor)) ;                           /* on the way back to where it stopped */
            else if (walk_cmp(crel, w->cursor) <= 0) continue;            /* done before the interruption */
            else { free(w->cursor); w->cursor = NULL; }                   /* from here on as a fresh walk */
        }
        struct stat st;
        if (lstat(cabs, &st) != 0) { whole = 0; continue; }
        int64_t id = 0;
        w->p.files++;
        if (lhcat__entry(c, w, dir, names[i], crel, cabs, &st, 0, &id)) { rc = -1; break; }
        progress(w, crel, 0);
        if (S_ISDIR(st.st_mode) && st.st_dev == w->dev) rc = walk_dir(c, w, id, crel, cabs);
        if (rc == 0) snprintf(w->last, sizeof w->last, "%s", crel);
        if (rc == 0 && maybe_commit(c, w, crel)) rc = -1;
        if (rc == 0 && w->stop_after && w->p.files >= w->stop_after) {
            if (w->kill) raise(SIGKILL);
            lhcat_interrupt();
        }
    }
    for (size_t i = 0; i < n; i++) free(names[i]);
    free(names);
    if (rc == 0 && whole) {   /* this directory was listed whole in this scan: what it lacks may be gone at the end */
        sqlite3_stmt *st = lhcat__prepare(c, "UPDATE locations SET listed_in = ?2 WHERE id = ?1");
        if (!st) return -1;
        sqlite3_bind_int64(st, 1, dir);
        sqlite3_bind_int64(st, 2, w->scan);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }
    return rc;
}

/* the locations of rel's parents (as they are now) and rel's own; *id rel's. -1 rel is not there (errno), -2 an error */
static int ensure_path(lhcat *c, struct lhcat__walk *w, const char *rel, int force_last, int64_t *idp, struct stat *last)
{
    int64_t id = c->root_id;
    char prefix[PATH_MAX] = "", abs[PATH_MAX];
    struct stat st;
    if (!*rel) {   /* the root itself */
        if (lstat(c->root, &st) != 0) return -1;
        if (last) *last = st;
        if (lhcat__entry(c, w, 0, "", "", c->root, &st, force_last, &id)) return -2;
        *idp = id;
        return 0;
    }
    for (const char *p = rel; *p;) {
        while (*p == '/') p++;
        const char *e = strchr(p, '/');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (!len) break;
        char name[NAME_MAX + 1];
        snprintf(name, sizeof name, "%.*s", (int)len, p);
        char next[PATH_MAX];
        join(next, sizeof next, prefix, name);
        snprintf(prefix, sizeof prefix, "%s", next);
        lhcat__abs(c, prefix, abs, sizeof abs);
        int is_last = !e || !e[strspn(e, "/")];
        if (lstat(abs, &st) != 0) return -1;
        int64_t child = lhcat__child(c, id, name, len);
        if (!child || is_last)
            if (lhcat__entry(c, w, id, name, prefix, abs, &st, is_last && force_last, &child)) return -2;
        id = child;
        if (is_last) { if (last) *last = st; break; }
        p += len;
    }
    *idp = id;
    return 0;
}

static void keep_err_rollback(lhcat *c)
{
    char keep[sizeof c->err];
    memcpy(keep, c->err, sizeof keep);
    sqlite3_exec(c->db, "ROLLBACK", NULL, NULL, NULL);
    memcpy(c->err, keep, sizeof keep);
}

int lhcat_index(lhcat *c, const char *subpath, const struct lhcat_index_opts *o, lhcat_progress_fn fn, void *ctx,
                struct lhcat_progress *out)
{
    struct lhcat__walk w;
    if (walk_init(c, &w, o)) { walk_done(&w); return -1; }
    w.fn = fn;
    w.ctx = ctx;
    char sub[PATH_MAX];
    snprintf(sub, sizeof sub, "%s", subpath ? subpath : "");
    while (strlen(sub) && sub[strlen(sub) - 1] == '/') sub[strlen(sub) - 1] = 0;
    w.sub = sub;
    struct stat rs;
    if (stat(c->root, &rs) != 0) { walk_done(&w); return lhcat__fail(c, "%s: %s", c->root, strerror(errno)); }
    w.dev = rs.st_dev;
    g_stop = 0;
    int rc = -1;
    if (lhcat__exec(c, "BEGIN IMMEDIATE")) { walk_done(&w); return -1; }
    /* an unfinished scan of the same subpath goes on where it stopped, with its start time */
    sqlite3_stmt *q = lhcat__prepare(c, "SELECT id, started, cursor FROM scans WHERE subpath = ?1"
                                        " AND status IN ('running', 'interrupted', 'failed') ORDER BY id DESC LIMIT 1");
    if (!q) goto fail;
    sqlite3_bind_text(q, 1, sub, -1, SQLITE_STATIC);
    if (sqlite3_step(q) == SQLITE_ROW) {
        if (w.o.restart) {
            char sql[128];
            snprintf(sql, sizeof sql, "UPDATE scans SET status = 'abandoned' WHERE id = %lld", (long long)sqlite3_column_int64(q, 0));
            sqlite3_finalize(q);
            q = NULL;
            if (lhcat__exec(c, sql)) goto fail;
        } else {
            w.scan = sqlite3_column_int64(q, 0);
            w.started = sqlite3_column_int64(q, 1);
            if (sqlite3_column_type(q, 2) != SQLITE_NULL) w.cursor = strdup((const char *)sqlite3_column_text(q, 2));
            w.p.resumed = 1;
        }
    }
    sqlite3_finalize(q);
    if (!w.scan) {
        sqlite3_stmt *s = lhcat__prepare(c, "INSERT INTO scans(subpath, started, status) VALUES(?1, ?2, 'running')");
        if (!s) goto fail;
        sqlite3_bind_text(s, 1, sub, -1, SQLITE_STATIC);
        sqlite3_bind_int64(s, 2, w.started);
        int ok = sqlite3_step(s) == SQLITE_DONE;
        sqlite3_finalize(s);
        if (!ok) { lhcat__dbfail(c, "scan"); goto fail; }
        w.scan = sqlite3_last_insert_rowid(c->db);
    }
    w.p.scan = w.scan;
    w.last_commit = lhcat__now_ms();
    int64_t top = 0;
    struct stat st;
    char abs[PATH_MAX];
    lhcat__abs(c, sub, abs, sizeof abs);
    int e = ensure_path(c, &w, sub, 0, &top, &st);
    if (e == -1) { lhcat__fail(c, "%s: %s", abs, strerror(errno)); goto fail; }
    if (e < 0) goto fail;
    if (!S_ISDIR(st.st_mode)) { lhcat__fail(c, "%s: not a directory", abs); goto fail; }
    w.p.files++;
    int r = walk_dir(c, &w, top, sub, abs);
    if (r < 0) goto fail;
    if (r == 0) {
        /* a whole scan: what a directory it listed whole no longer has is gone (and what was under that) */
        sqlite3_stmt *g = lhcat__prepare(c,
            "WITH RECURSIVE under(id) AS (SELECT ?1 UNION ALL"
            " SELECT l.id FROM under u CROSS JOIN locations l ON l.parent = u.id WHERE +l.kind = 'd')"
            " INSERT OR IGNORE INTO lhcat_gone SELECT l.id FROM locations l"
            " WHERE l.parent IN (SELECT id FROM under) AND l.state = 'present'"
            " AND (l.observed_at IS NULL OR l.observed_at < ?2)"
            " AND l.parent IN (SELECT id FROM locations WHERE listed_in = ?3)");
        if (!g) goto fail;
        sqlite3_bind_int64(g, 1, top);
        sqlite3_bind_int64(g, 2, w.started);
        sqlite3_bind_int64(g, 3, w.scan);
        int ok = sqlite3_step(g) == SQLITE_DONE;
        sqlite3_finalize(g);
        if (!ok) { lhcat__dbfail(c, "gone"); goto fail; }
        if (lhcat__mark_gone(c, &w.p.gone) || scan_update(c, &w, "complete", NULL)) goto fail;
        sqlite3_stmt *t = lhcat__prepare(c, "UPDATE roots SET indexed_at = ?1");
        if (t) { sqlite3_bind_int64(t, 1, lhcat__now_ms() / 1000); sqlite3_step(t); sqlite3_finalize(t); }
    } else if (scan_update(c, &w, "interrupted", w.last[0] ? w.last : w.cursor)) goto fail;
    if (lhcat__exec(c, "COMMIT")) goto fail;
    progress(&w, NULL, 1);
    rc = 0;
fail:
    if (rc) keep_err_rollback(c);
    if (out) *out = w.p;
    walk_done(&w);
    return rc;
}

int lhcat_touch(lhcat *c, const char *rel)
{
    struct lhcat__walk w;
    struct lhcat_index_opts o = { 0 };
    o.sniff = 1;
    if (walk_init(c, &w, &o)) { walk_done(&w); return -1; }
    int own = sqlite3_get_autocommit(c->db);
    if (own && lhcat__exec(c, "BEGIN IMMEDIATE")) { walk_done(&w); return -1; }
    int64_t id = 0;
    int rc = ensure_path(c, &w, rel ? rel : "", 1, &id, NULL);
    if (rc == -1 && (errno == ENOENT || errno == ENOTDIR)) {   /* not there: it and what is under it are gone */
        rc = 1;
        int64_t gone = lhcat_id(c, rel);
        if (gone && gone != c->root_id) {
            char sql[96];
            snprintf(sql, sizeof sql, "INSERT OR IGNORE INTO lhcat_gone VALUES(%lld)", (long long)gone);
            if (lhcat__exec(c, sql) || lhcat__mark_gone(c, NULL)) rc = -1;
        }
    } else if (rc == -1) {
        char abs[PATH_MAX];
        rc = lhcat__fail(c, "%s: %s", lhcat__abs(c, rel, abs, sizeof abs), strerror(errno));
    } else if (rc < 0) rc = -1;
    if (own) {
        if (rc >= 0) { if (lhcat__exec(c, "COMMIT")) rc = -1; }
        else keep_err_rollback(c);
    }
    walk_done(&w);
    return rc;
}

int lhcat_hash(lhcat *c, const char *rel)
{
    struct lhcat__walk w;
    struct lhcat_index_opts o = { 0 };
    o.hash = 1;
    if (walk_init(c, &w, &o)) { walk_done(&w); return -1; }
    int own = sqlite3_get_autocommit(c->db);
    if (own && lhcat__exec(c, "BEGIN IMMEDIATE")) { walk_done(&w); return -1; }
    int64_t id = 0;
    int rc = ensure_path(c, &w, rel ? rel : "", 1, &id, NULL);
    if (rc == -1) {
        char abs[PATH_MAX];
        lhcat__fail(c, "%s: %s", lhcat__abs(c, rel, abs, sizeof abs), strerror(errno));
    }
    rc = rc < 0 ? -1 : 0;
    if (own) {
        if (rc == 0) { if (lhcat__exec(c, "COMMIT")) rc = -1; }
        else keep_err_rollback(c);
    }
    walk_done(&w);
    return rc;
}

/* ---- a du listing ------------------------------------------------------------------------------------------------------ */

struct du {
    lhcat *c;
    struct strmap dirs;                 /* directory path -> location */
    sqlite3_stmt *obj, *loc, *upd;
    int64_t now;
    struct lhcat_progress p;
};

/* the location (parent, name) as listed: made with its object when new; usage NULL for a directory made as a parent */
static int64_t du_row(struct du *d, int64_t parent, const char *name, size_t len, char kind, int64_t usage, const char *type)
{
    int64_t id = lhcat__child(d->c, parent, name, len);
    if (!id) {
        sqlite3_reset(d->obj);
        sqlite3_bind_int64(d->obj, 1, d->now / 1000);
        if (sqlite3_step(d->obj) != SQLITE_DONE) { sqlite3_reset(d->obj); lhcat__dbfail(d->c, "import"); return 0; }
        sqlite3_reset(d->obj);
        sqlite3_stmt *u = d->loc;
        sqlite3_reset(u);
        sqlite3_bind_int64(u, 1, sqlite3_last_insert_rowid(d->c->db));
        sqlite3_bind_int64(u, 2, parent);
        sqlite3_bind_text(u, 3, name, (int)len, SQLITE_STATIC);
        sqlite3_bind_text(u, 4, kind == 'd' ? "d" : "f", 1, SQLITE_STATIC);
        if (usage >= 0) sqlite3_bind_int64(u, 5, usage); else sqlite3_bind_null(u, 5);
        if (type) sqlite3_bind_text(u, 6, type, -1, SQLITE_STATIC); else sqlite3_bind_null(u, 6);
        sqlite3_bind_int(u, 7, LHCAT_LISTED | (type ? LHCAT_TYPED : 0));
        sqlite3_bind_int64(u, 8, d->now);
        int ok = sqlite3_step(u) == SQLITE_DONE;
        sqlite3_reset(u);
        if (!ok) { lhcat__dbfail(d->c, "import"); return 0; }
        id = sqlite3_last_insert_rowid(d->c->db);
    } else {
        sqlite3_stmt *u = d->upd;
        sqlite3_reset(u);
        sqlite3_bind_int64(u, 1, id);
        if (usage >= 0) sqlite3_bind_int64(u, 2, usage); else sqlite3_bind_null(u, 2);
        sqlite3_bind_int64(u, 3, d->now);
        if (type) sqlite3_bind_text(u, 4, type, -1, SQLITE_STATIC); else sqlite3_bind_null(u, 4);
        sqlite3_step(u);
        sqlite3_reset(u);
    }
    d->p.rows++;
    return id;
}

/* the location of the directory path[0..len) and its parents' (made when missing) */
static int64_t du_dir(struct du *d, const char *path, size_t len)
{
    if (!len) return d->c->root_id;
    int64_t id = map_get(&d->dirs, path, len);
    if (id) return id;
    const char *slash = memrchr(path, '/', len);
    size_t plen = slash ? (size_t)(slash - path) : 0;
    int64_t parent = du_dir(d, path, plen);
    if (!parent) return 0;
    const char *name = slash ? slash + 1 : path;
    id = du_row(d, parent, name, len - (size_t)(name - path), 'd', -1, "inode/directory");
    if (id) { map_put(&d->dirs, path, len, id); d->p.dirs++; }
    return id;
}

int lhcat_import_du(lhcat *c, FILE *in, uint64_t block, lhcat_progress_fn fn, void *ctx, struct lhcat_progress *out)
{
    struct du d;
    memset(&d, 0, sizeof d);
    d.c = c;
    d.now = lhcat__now_ms();
    if (!block) block = 1024;
    d.obj = lhcat__prepare(c, "INSERT INTO objects(created_at) VALUES(?1)");
    d.loc = lhcat__prepare(c, "INSERT INTO locations(object_id, parent, name, kind, usage_kib, type, coverage, observed_at)"
                              " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");
    /* a location a scan observed keeps its time: the listing is older evidence than a stat */
    d.upd = lhcat__prepare(c, "UPDATE locations SET usage_kib = coalesce(?2, usage_kib), coverage = coverage | 1,"
                              " type = CASE WHEN coverage & 2 OR kind = 'd' THEN type ELSE ?4 END,"
                              " observed_at = CASE WHEN coverage & 2 THEN observed_at ELSE ?3 END,"
                              " state = CASE WHEN coverage & 2 THEN state ELSE 'present' END WHERE id = ?1");
    int rc = -1;
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    if (!d.obj || !d.loc || !d.upd || lhcat__exec(c, "CREATE TEMP TABLE IF NOT EXISTS lhcat_gone(id INTEGER PRIMARY KEY)"))
        goto out;
    /* the listing's bulk: a bigger page cache, no sync per transaction */
    lhcat__exec(c, "PRAGMA cache_size = -262144; PRAGMA synchronous = OFF");
    if (lhcat__exec(c, "BEGIN IMMEDIATE")) goto out;
    while ((len = getline(&line, &cap, in)) >= 0) {
        d.p.lines++;
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        char *tab = memchr(line, '\t', (size_t)len);
        if (!tab) continue;
        char *end;
        unsigned long long sz = strtoull(line, &end, 10);
        if (end == line) continue;
        char *path = tab + 1;
        if (path[0] == '.' && path[1] == '/') path += 2;
        else if (path[0] == '.' && path[1] == 0) path++;
        else if (path[0] == '/') {   /* an absolute listing: under the root, or not ours */
            if (strncmp(path, c->root, c->rootlen) || (path[c->rootlen] && path[c->rootlen] != '/')) continue;
            path += c->rootlen;
        }
        while (*path == '/') path++;
        size_t plen = strlen(path);
        while (plen && path[plen - 1] == '/') path[--plen] = 0;
        int64_t kib = (int64_t)((sz * block + 1023) / 1024);
        int64_t known = !plen ? c->root_id : map_get(&d.dirs, path, plen);
        if (known) {   /* a directory's total, after its contents (the root's: the "." line) */
            sqlite3_reset(d.upd);
            sqlite3_bind_int64(d.upd, 1, known);
            sqlite3_bind_int64(d.upd, 2, kib);
            sqlite3_bind_int64(d.upd, 3, d.now);
            sqlite3_bind_null(d.upd, 4);
            sqlite3_step(d.upd);
            sqlite3_reset(d.upd);
        } else {
            const char *slash = memrchr(path, '/', plen);
            size_t dlen = slash ? (size_t)(slash - path) : 0;
            int64_t parent = du_dir(&d, path, dlen);
            if (!parent) goto out;
            const char *name = slash ? slash + 1 : path;
            if (!du_row(&d, parent, name, plen - (size_t)(name - path), 'f', kib, lhcat_type_for_name(name))) goto out;
        }
        if (d.p.lines % 10000 == 0) {   /* committed: a query sees what is in so far */
            if (lhcat__exec(c, "COMMIT") || lhcat__exec(c, "BEGIN IMMEDIATE")) goto out;
            if (fn && d.p.lines % 100000 == 0) { d.p.current = path; fn(ctx, &d.p); d.p.current = NULL; }
        }
    }
    if (ferror(in)) { lhcat__fail(c, "reading the listing: %s", strerror(errno)); goto out; }
    {   /* a listed-only location this listing does not have any more is gone */
        sqlite3_stmt *g = lhcat__prepare(c, "INSERT OR IGNORE INTO lhcat_gone SELECT id FROM locations"
                                            " WHERE coverage & 2 = 0 AND coverage & 1 AND parent != 0 AND state = 'present'"
                                            " AND (observed_at IS NULL OR observed_at < ?1)");
        if (!g) goto out;
        sqlite3_bind_int64(g, 1, d.now);
        int ok = sqlite3_step(g) == SQLITE_DONE;
        sqlite3_finalize(g);
        if (!ok || lhcat__mark_gone(c, &d.p.gone)) goto out;
        sqlite3_stmt *t = lhcat__prepare(c, "UPDATE roots SET listed_at = ?1");
        if (t) { sqlite3_bind_int64(t, 1, d.now / 1000); sqlite3_step(t); sqlite3_finalize(t); }
    }
    if (lhcat__exec(c, "COMMIT")) goto out;
    if (fn) fn(ctx, &d.p);
    rc = 0;
out:
    if (rc) keep_err_rollback(c);
    lhcat__exec(c, "PRAGMA synchronous = NORMAL; PRAGMA cache_size = -65536");
    free(line);
    sqlite3_finalize(d.obj);
    sqlite3_finalize(d.loc);
    sqlite3_finalize(d.upd);
    map_free(&d.dirs);
    if (out) *out = d.p;
    return rc;
}
