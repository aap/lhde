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

/* the conditions on the locations' own facts (shared with the coverage count) */
static void facts(struct sql *q, struct owned *o, const struct lhcat_query *f, int64_t in_id)
{
    if (f->type && *f->type) {
        size_t n = strlen(f->type);
        if (n >= 2 && !strcmp(f->type + n - 2, "/*")) {   /* a group: "image/" <= type < "image0" */
            char *lo = strndup(f->type, n - 1), *hi = strndup(f->type, n - 1);
            if (lo && hi) {
                hi[n - 2] = '0';
                add(q, " AND l.type >= ? AND l.type < ?");
                bind(q, keep(o, lo));
                bind(q, keep(o, hi));
            } else { free(lo); free(hi); }
        } else { add(q, " AND l.type = ?"); bind(q, f->type); }
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
    if (in_id) {
        char buf[256];
        snprintf(buf, sizeof buf, " AND l.parent IN (WITH RECURSIVE under(id) AS (SELECT %lld UNION ALL"
                                  " SELECT x.id FROM under u CROSS JOIN locations x ON x.parent = u.id WHERE +x.kind = 'd')"
                                  " SELECT id FROM under)", (long long)in_id);
        add(q, buf);
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
    int64_t in_id = 0;
    if (f->in && *f->in) {
        in_id = lhcat_id(c, f->in);
        if (!in_id) return lhcat__fail(c, "%s: not in the catalogue", f->in);
    }
    add(&q, "SELECT l.id, l.object_id, l.kind, l.coverage, l.size, l.usage_kib, l.mtime, l.type, p.taken_at"
            " FROM locations l LEFT JOIN picture p ON p.object_id = l.object_id"
            " WHERE l.state = 'present' AND l.parent != 0");
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
    facts(&q, &o, f, in_id);
    add(&q, " ORDER BY l.id");
    if (f->limit > 0) {
        char lim[32];
        snprintf(lim, sizeof lim, " LIMIT %d", f->limit);
        add(&q, lim);
    }
    int64_t count = -1;
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
            h.path = lhcat_path(c, h.id);
            if (h.path) fn(ctx, &h);
        }
        sqlite3_finalize(st);
    }
    if (count >= 0 && (scope || covered)) {   /* what the attribute conditions could see at all */
        struct sql s = { 0 };
        struct owned o2 = { { 0 }, 0 };
        add(&s, "SELECT count(*), coalesce(sum(l.coverage & 8 != 0), 0) FROM locations l"
                " LEFT JOIN picture p ON p.object_id = l.object_id WHERE l.state = 'present' AND l.parent != 0");
        facts(&s, &o2, f, in_id);
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
