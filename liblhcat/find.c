/* liblhcat: queries over the catalogue. */
#define _GNU_SOURCE
#include "internal.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct sql { char *s; size_t n, cap; const char *bind[32]; int nb; };

static void add(struct sql *q, const char *text)
{
    size_t k = strlen(text);
    if (q->n + k + 1 > q->cap) {
        size_t cap = (q->n + k + 1) * 2;
        char *m = realloc(q->s, cap);
        if (!m) return;
        q->s = m;
        q->cap = cap;
    }
    memcpy(q->s + q->n, text, k + 1);
    q->n += k;
}

/* a text bound as the next ?: the caller's strings live through the query, made ones are kept in owned[] */
struct owned { char *s[32]; int n; };
static const char *keep(struct owned *o, char *s) { if (o->n < 32) o->s[o->n++] = s; return s; }
static void bind(struct sql *q, const char *v) { if (q->nb < 32) q->bind[q->nb++] = v; }

/* %part% for LIKE with its own escape */
static char *like_part(const char *v)
{
    size_t n = strlen(v);
    char *o = malloc(2 * n + 3), *p = o;
    if (!o) return NULL;
    *p++ = '%';
    for (const char *s = v; *s; s++) { if (*s == '%' || *s == '_' || *s == '\\') *p++ = '\\'; *p++ = *s; }
    *p++ = '%';
    *p = 0;
    return o;
}

/* a word for FTS5: a quoted prefix ("re"*) */
static char *fts_word(const char *w, size_t n)
{
    char *o = malloc(2 * n + 4), *p = o;
    if (!o) return NULL;
    *p++ = '"';
    for (size_t i = 0; i < n; i++) { if (w[i] == '"') *p++ = '"'; *p++ = w[i]; }
    *p++ = '"';
    *p++ = '*';
    *p = 0;
    return o;
}

/* a part of a name for the trigram index: its trigrams at every third character and the last one, ANDed ("readme":
   "rea" AND "dme"), each quoted; NULL when it is shorter than three characters (UTF-8: a character is a lead byte and
   its continuation bytes, as the trigram tokenizer counts them) */
static char *trigram_match(const char *v)
{
    size_t n = strlen(v), starts[1024], k = 0;
    for (size_t i = 0; i < n && k < 1024; i++) if (((unsigned char)v[i] & 0xc0) != 0x80) starts[k++] = i;
    if (k < 3) return NULL;
    char *o = malloc(n * 6 + 64), *p = o;
    if (!o) return NULL;
    for (size_t t = 0;; t = t + 3 > k - 3 ? k - 3 : t + 3) {
        size_t a = starts[t], b = t + 3 < k ? starts[t + 3] : n;
        if (p != o) p += sprintf(p, " AND ");
        *p++ = '"';
        for (size_t i = a; i < b; i++) { if (v[i] == '"') *p++ = '"'; *p++ = v[i]; }
        *p++ = '"';
        if (t == k - 3) break;
    }
    *p = 0;
    return o;
}

/* a comma-separated list's items, trimmed, into out (at most max, each malloc'ed); the count */
static int split_commas(const char *v, char **out, int max)
{
    int n = 0;
    for (const char *p = v; *p && n < max;) {
        while (*p == ',' || isspace((unsigned char)*p)) p++;
        size_t k = strcspn(p, ",");
        size_t e = k;
        while (e > 0 && isspace((unsigned char)p[e - 1])) e--;
        if (e) out[n++] = strndup(p, e);
        p += k;
    }
    return n;
}

static const char *DATE = "coalesce(p.taken_at, datetime(l.mtime, 'unixepoch', 'localtime'))";

/* the subtree below a directory as a range of dirs' path index: path >= P AND path < P "0" AND (path = P OR
   path >= P "/") ('0' is the byte after '/') */
struct subtree { const char *p, *p0, *pslash; };
static const char *SUBTREE = "%s >= ? AND %s < ? AND (%s = ? OR %s >= ?)";

static void subtree(struct sql *q, const char *col, const struct subtree *t)
{
    char buf[256];
    snprintf(buf, sizeof buf, SUBTREE, col, col, col, col);
    add(q, buf);
    bind(q, t->p);
    bind(q, t->p0);
    bind(q, t->p);
    bind(q, t->pslash);
}

/* the conditions on the locations' own facts (shared with the coverage count); in: below that directory; walk_order:
   the query goes directory by directory, the type's indexes left out of it */
static void facts(struct sql *q, struct owned *o, const struct lhcat_query *f, const struct subtree *in, int walk_order)
{
    if (f->type && *f->type) {
        char *types[8];
        int nt = split_commas(f->type, types, 8);
        add(q, nt > 1 ? " AND (" : " AND ");
        for (int i = 0; i < nt; i++) {
            size_t n = strlen(types[i]);
            if (i) add(q, " OR ");
            if (n >= 2 && !strcmp(types[i] + n - 2, "/*")) {   /* a group: its index (locations_group) is in rowid order */
                add(q, walk_order ? "+substr(l.type, 1, instr(l.type, '/')) = ?" : "substr(l.type, 1, instr(l.type, '/')) = ?");
                types[i][n - 1] = 0;
            } else add(q, walk_order ? "+l.type = ?" : "l.type = ?");
            bind(q, keep(o, types[i]));
        }
        if (nt > 1) add(q, ")");
        if (!nt) add(q, "1");
    }
    if (f->since && *f->since) { add(q, " AND "); add(q, DATE); add(q, " >= ?"); bind(q, f->since); }
    if (f->until && *f->until) {   /* the whole of the year, month or day named */
        char *u = malloc(strlen(f->until) + 2);
        if (u) {
            sprintf(u, "%s\x7f", f->until);
            add(q, " AND ");
            add(q, DATE);
            add(q, " < ?");
            bind(q, keep(o, u));
        }
    }
    if (f->kind) add(q, f->kind == 'd' ? " AND l.kind = 'd'" : f->kind == 'f' ? " AND l.kind = 'f'" : "");
    if (in) {
        add(q, " AND l.parent IN (SELECT id FROM dirs WHERE ");
        subtree(q, "path", in);
        add(q, ")");
    }
}

static sqlite3_stmt *prepare(lhcat *c, struct sql *q)
{
    sqlite3_stmt *st = NULL;
    if (!q->s || sqlite3_prepare_v2(c->db, q->s, -1, &st, NULL) != SQLITE_OK) { lhcat__dbfail(c, "query"); return NULL; }
    for (int i = 0; i < q->nb; i++) sqlite3_bind_text(st, i + 1, q->bind[i], -1, SQLITE_STATIC);
    return st;
}

int64_t lhcat_find(lhcat *c, const struct lhcat_query *f, lhcat_hit_fn fn, void *ctx, int64_t *scope, int64_t *covered)
{
    struct sql q = { 0 };
    struct owned o = { { 0 }, 0 };
    /* below a directory: its subtree is a range of dirs' path index */
    struct subtree in = { 0 }, *inp = NULL;
    char *in_path = NULL;
    if (f->in && *f->in) {
        int64_t in_id = lhcat_id(c, f->in);
        if (!in_id) return lhcat__fail(c, "%s: not in the catalogue", f->in);
        sqlite3_stmt *d = lhcat__prepare(c, "SELECT path FROM dirs WHERE id = ?1");
        if (!d) return -1;
        sqlite3_bind_int64(d, 1, in_id);
        in_path = sqlite3_step(d) == SQLITE_ROW ? strdup((const char *)sqlite3_column_text(d, 0)) : NULL;
        sqlite3_finalize(d);
        if (!in_path) return lhcat__fail(c, "%s: not a directory in the catalogue", f->in);
        if (*in_path) {
            size_t n = strlen(in_path);
            char *p0 = malloc(n + 2), *ps = malloc(n + 2);
            if (p0 && ps) {
                memcpy(p0, in_path, n); p0[n] = '0'; p0[n + 1] = 0;
                memcpy(ps, in_path, n); ps[n] = '/'; ps[n + 1] = 0;
                in.p = keep(&o, in_path);
                in.p0 = keep(&o, p0);
                in.pslash = keep(&o, ps);
                inp = &in;
            } else { free(p0); free(ps); free(in_path); return lhcat__fail(c, "out of memory"); }
        } else free(in_path);   /* the root: everything */
    }
    /* With a subtree the rows come directory by directory in path order, names sorted: without an attribute condition
       or a rare type by walking the subtree's range of dirs (a limit stops it early), with one from that index, sorted
       after. */
    int attr_cond = (f->tag && *f->tag) || (f->no_tag && *f->no_tag) || (f->desc && *f->desc) || (f->color && *f->color) ||
                    (f->text && *f->text);
    int walk_order = inp && !attr_cond;
    if (walk_order && f->name && strlen(f->name) >= 3) walk_order = 0;   /* the name index finds few */
    if (walk_order && f->type && *f->type && !strchr(f->type, ',')) {   /* a rare type: from its index too (a walk of the subtree would find little) */
        size_t n = strlen(f->type);
        int group = n >= 2 && !strcmp(f->type + n - 2, "/*");
        sqlite3_stmt *r = lhcat__prepare(c, group ? "SELECT count(*) FROM (SELECT 1 FROM locations WHERE"
                                                    " substr(type, 1, instr(type, '/')) = ?1 LIMIT 50001)"
                                                  : "SELECT count(*) FROM (SELECT 1 FROM locations WHERE type = ?1 LIMIT 50001)");
        if (r) {
            sqlite3_bind_text(r, 1, f->type, group ? (int)n - 1 : -1, SQLITE_TRANSIENT);
            if (sqlite3_step(r) == SQLITE_ROW && sqlite3_column_int64(r, 0) <= 50000) walk_order = 0;
            sqlite3_finalize(r);
        }
    }
    /* a part of the name of 3 characters or more: the rows come from the trigram index's candidates (in rowid order,
       lazily: a limit stops it early) and the rows not in it yet, each checked by LIKE */
    char *name_match = f->name && *f->name ? trigram_match(f->name) : NULL;
    add(&q, "SELECT l.id, l.object_id, l.kind, l.coverage, l.size, l.usage_kib, l.mtime, l.type, p.taken_at, d.path, l.name");
    if (name_match) {
        add(&q, " FROM (SELECT rowid AS id FROM names WHERE names MATCH ? UNION ALL"
                " SELECT id FROM locations WHERE id > (SELECT upto FROM names_upto)) m CROSS JOIN locations l ON l.id = m.id");
        bind(&q, keep(&o, name_match));
        add(&q, inp ? " CROSS JOIN dirs d ON d.id = l.parent" : " LEFT JOIN dirs d ON d.id = l.parent");
    } else if (walk_order) add(&q, " FROM dirs d CROSS JOIN locations l ON l.parent = d.id");
    else if (inp) add(&q, " FROM locations l CROSS JOIN dirs d ON d.id = l.parent");   /* l's own index first */
    else add(&q, " FROM locations l LEFT JOIN dirs d ON d.id = l.parent");
    add(&q, " LEFT JOIN picture p ON p.object_id = l.object_id WHERE l.state = 'present' AND l.parent != 0");
    if (inp) { add(&q, " AND "); subtree(&q, "d.path", inp); }
    if (f->tag && *f->tag) {
        char *tags[8];
        int nt = split_commas(f->tag, tags, 8);
        for (int i = 0; i < nt; i++) {
            add(&q, " AND l.object_id IN (SELECT object_id FROM tags WHERE tag = ?)");
            bind(&q, keep(&o, tags[i]));
        }
    }
    if (f->name && *f->name) {   /* (a short part: every row looked at) */
        add(&q, " AND l.name LIKE ? ESCAPE '\\'");
        bind(&q, keep(&o, like_part(f->name)));
    }
    if (f->no_tag && *f->no_tag) {
        add(&q, " AND l.object_id NOT IN (SELECT object_id FROM tags WHERE tag = ?)");
        bind(&q, f->no_tag);
        if (!f->include_unknown) add(&q, " AND l.coverage & 8");   /* unknown is not "without the tag" */
    }
    if (f->desc && *f->desc) {
        add(&q, " AND l.object_id IN (SELECT object_id FROM attrs WHERE name = 'user.lux.desc' AND desired LIKE ? ESCAPE '\\')");
        bind(&q, keep(&o, like_part(f->desc)));
    }
    if (f->color && *f->color) {
        add(&q, " AND l.object_id IN (SELECT object_id FROM attrs WHERE name = 'user.lux.color' AND desired = lower(?))");
        bind(&q, f->color);
    }
    if (f->text && *f->text) {
        for (const char *p = f->text; *p;) {
            while (*p && isspace((unsigned char)*p)) p++;
            size_t n = 0;
            while (p[n] && !isspace((unsigned char)p[n])) n++;
            if (!n) break;
            char *word = strndup(p, n);
            add(&q, " AND (l.object_id IN (SELECT rowid FROM text WHERE text MATCH ?) OR l.name LIKE ? ESCAPE '\\')");
            bind(&q, keep(&o, fts_word(p, n)));
            bind(&q, keep(&o, like_part(word)));
            free(word);
            p += n;
        }
    }
    facts(&q, &o, f, NULL, walk_order);
    /* (from the name index the rows come in catalogue order already: the index's in rowid order, then those past it) */
    add(&q, inp ? " ORDER BY d.path, l.name" : name_match ? "" : " ORDER BY l.id");
    if (f->limit > 0) {
        char lim[32];
        snprintf(lim, sizeof lim, " LIMIT %d", f->limit);
        add(&q, lim);
    }
    int64_t count = -1;
    char *pbuf = NULL;
    size_t pcap = 0;
    sqlite3_stmt *st = prepare(c, &q);
    sqlite3_stmt *at = f->with_attrs && fn ? lhcat__prepare(c, "SELECT name, desired, pending FROM attrs WHERE object_id = ?1") : NULL;
    char **nv = NULL;
    int nvn = 0, nvcap = 0;
    if (st) {
        count = 0;
        while (!(f->stop && *f->stop) && sqlite3_step(st) == SQLITE_ROW) {
            count++;
            if (!fn) continue;
            struct lhcat_hit h;
            h.id = sqlite3_column_int64(st, 0);
            h.object = sqlite3_column_int64(st, 1);
            const char *k = (const char *)sqlite3_column_text(st, 2);
            h.kind = k ? k[0] : 'o';
            h.coverage = sqlite3_column_int(st, 3);
            h.size = sqlite3_column_type(st, 4) == SQLITE_NULL ? -1 : sqlite3_column_int64(st, 4);
            h.usage_kib = sqlite3_column_type(st, 5) == SQLITE_NULL ? -1 : sqlite3_column_int64(st, 5);
            h.mtime = sqlite3_column_type(st, 6) == SQLITE_NULL ? -1 : sqlite3_column_int64(st, 6);
            h.type = (const char *)sqlite3_column_text(st, 7);
            h.taken_at = (const char *)sqlite3_column_text(st, 8);
            if (sqlite3_column_type(st, 9) != SQLITE_NULL) {   /* the parent's path and the name */
                const char *dp = (const char *)sqlite3_column_text(st, 9), *nm = (const char *)sqlite3_column_text(st, 10);
                size_t dl = (size_t)sqlite3_column_bytes(st, 9), nl = (size_t)sqlite3_column_bytes(st, 10);
                if (dl + nl + 2 > pcap) {
                    char *b = realloc(pbuf, (dl + nl + 2) * 2);
                    if (!b) continue;
                    pbuf = b;
                    pcap = (dl + nl + 2) * 2;
                }
                size_t k = 0;
                if (dl) { memcpy(pbuf, dp, dl); pbuf[dl] = '/'; k = dl + 1; }
                memcpy(pbuf + k, nm, nl);
                pbuf[k + nl] = 0;
                h.path = pbuf;
            } else h.path = lhcat_path(c, h.id);
            h.nattrs = h.pending = h.conflicts = 0;
            h.attrs = NULL;
            if (at) {   /* the object's desired values and what is pending */
                for (int i = 0; i < nvn; i++) free(nv[i]);
                nvn = 0;
                sqlite3_reset(at);
                sqlite3_bind_int64(at, 1, h.object);
                while (sqlite3_step(at) == SQLITE_ROW) {
                    const char *pend = (const char *)sqlite3_column_text(at, 2);
                    if (pend && !strcmp(pend, "write")) h.pending++;
                    else if (pend && !strcmp(pend, "conflict")) h.conflicts++;
                    if (sqlite3_column_type(at, 1) == SQLITE_NULL) continue;
                    if (nvn + 2 > nvcap) {
                        char **m = realloc(nv, (size_t)(nvcap * 2 + 16) * sizeof *nv);
                        if (!m) break;
                        nv = m;
                        nvcap = nvcap * 2 + 16;
                    }
                    nv[nvn++] = strdup((const char *)sqlite3_column_text(at, 0));
                    nv[nvn++] = strdup((const char *)sqlite3_column_text(at, 1));
                }
                h.nattrs = nvn / 2;
                h.attrs = (const char *const *)nv;
            }
            if (h.path) fn(ctx, &h);
        }
        sqlite3_finalize(st);
    }
    for (int i = 0; i < nvn; i++) free(nv[i]);
    free(nv);
    sqlite3_finalize(at);
    free(pbuf);
    if (count >= 0 && (scope || covered)) {   /* what the attribute conditions could see at all */
        struct sql s = { 0 };
        struct owned o2 = { { 0 }, 0 };
        add(&s, "SELECT count(*), coalesce(sum(l.coverage & 8 != 0), 0) FROM locations l"
                " LEFT JOIN picture p ON p.object_id = l.object_id WHERE l.state = 'present' AND l.parent != 0");
        facts(&s, &o2, f, inp, 0);
        sqlite3_stmt *cs = prepare(c, &s);
        if (cs && sqlite3_step(cs) == SQLITE_ROW) {
            if (scope) *scope = sqlite3_column_int64(cs, 0);
            if (covered) *covered = sqlite3_column_int64(cs, 1);
        }
        sqlite3_finalize(cs);
        for (int i = 0; i < o2.n; i++) free(o2.s[i]);
        free(s.s);
    }
    for (int i = 0; i < o.n; i++) free(o.s[i]);
    free(q.s);
    return count;
}

int64_t lhcat_names_update(lhcat *c, int64_t max, int64_t *left)
{
    int64_t done = 0;
    while (max <= 0 || done < max) {
        int64_t chunk = max > 0 && max - done < 50000 ? max - done : 50000;
        if (lhcat__exec(c, "BEGIN IMMEDIATE")) return -1;
        /* the last id of the chunk, then its names (the root's own "" indexes nothing) */
        sqlite3_stmt *last = lhcat__prepare(c, "SELECT max(id), count(*) FROM (SELECT id FROM locations"
                                               " WHERE id > (SELECT upto FROM names_upto) ORDER BY id LIMIT ?1)");
        sqlite3_stmt *ins = lhcat__prepare(c, "INSERT INTO names(rowid, name) SELECT id, name FROM locations"
                                              " WHERE id > (SELECT upto FROM names_upto) AND id <= ?1");
        sqlite3_stmt *up = lhcat__prepare(c, "UPDATE names_upto SET upto = ?1");
        int ok = last && ins && up;
        int64_t m = 0, n = 0;
        if (ok) sqlite3_bind_int64(last, 1, chunk);
        if (ok && sqlite3_step(last) == SQLITE_ROW) { m = sqlite3_column_int64(last, 0); n = sqlite3_column_int64(last, 1); }
        if (ok && n > 0) {
            sqlite3_bind_int64(ins, 1, m);
            sqlite3_bind_int64(up, 1, m);
            ok = sqlite3_step(ins) == SQLITE_DONE && sqlite3_step(up) == SQLITE_DONE;
        }
        sqlite3_finalize(last);
        sqlite3_finalize(ins);
        sqlite3_finalize(up);
        if (!ok) { lhcat__dbfail(c, "the name index"); sqlite3_exec(c->db, "ROLLBACK", 0, 0, 0); return -1; }
        if (lhcat__exec(c, "COMMIT")) return -1;
        done += n;
        if (n < chunk) break;
    }
    if (left) {
        sqlite3_stmt *q = lhcat__prepare(c, "SELECT count(*) FROM locations WHERE id > (SELECT upto FROM names_upto)");
        *left = q && sqlite3_step(q) == SQLITE_ROW ? sqlite3_column_int64(q, 0) : 0;
        sqlite3_finalize(q);
    }
    return done;
}
