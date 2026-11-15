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
        size_t n = strlen(f->type);
        if (n >= 2 && !strcmp(f->type + n - 2, "/*")) {   /* a group: its index (locations_group) is in rowid order */
            add(q, walk_order ? " AND +substr(l.type, 1, instr(l.type, '/')) = ?" : " AND substr(l.type, 1, instr(l.type, '/')) = ?");
            bind(q, keep(o, strndup(f->type, n - 1)));
        } else { add(q, walk_order ? " AND +l.type = ?" : " AND l.type = ?"); bind(q, f->type); }
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
    if (walk_order && f->type && *f->type) {   /* a rare type: from its index too (a walk of the subtree would find little) */
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
    add(&q, "SELECT l.id, l.object_id, l.kind, l.coverage, l.size, l.usage_kib, l.mtime, l.type, p.taken_at, d.path, l.name");
    if (walk_order) add(&q, " FROM dirs d CROSS JOIN locations l ON l.parent = d.id");
    else if (inp) add(&q, " FROM locations l CROSS JOIN dirs d ON d.id = l.parent");   /* l's own index first */
    else add(&q, " FROM locations l LEFT JOIN dirs d ON d.id = l.parent");
    add(&q, " LEFT JOIN picture p ON p.object_id = l.object_id WHERE l.state = 'present' AND l.parent != 0");
    if (inp) { add(&q, " AND "); subtree(&q, "d.path", inp); }
    if (f->tag && *f->tag) { add(&q, " AND l.object_id IN (SELECT object_id FROM tags WHERE tag = ?)"); bind(&q, f->tag); }
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
    add(&q, inp ? " ORDER BY d.path, l.name" : " ORDER BY l.id");
    if (f->limit > 0) {
        char lim[32];
        snprintf(lim, sizeof lim, " LIMIT %d", f->limit);
        add(&q, lim);
    }
    int64_t count = -1;
    char *pbuf = NULL;
    size_t pcap = 0;
    sqlite3_stmt *st = prepare(c, &q);
    if (st) {
        count = 0;
        while (sqlite3_step(st) == SQLITE_ROW) {
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
            if (h.path) fn(ctx, &h);
        }
        sqlite3_finalize(st);
    }
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
