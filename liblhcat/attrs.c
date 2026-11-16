/* liblhcat: attributes as three values (observed, desired, pending), operations and the journal, writing back. */
#define _GNU_SOURCE
#include "internal.h"
#include "liblhde/compat.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <unistd.h>

static int same(const char *a, const char *b) { return !strcmp(a ? a : "", b ? b : ""); }
static const char *nz(const char *s) { return s && *s ? s : NULL; }
static char *dup_col(sqlite3_stmt *st, int i)
{
    const char *v = (const char *)sqlite3_column_text(st, i);
    return v ? strdup(v) : NULL;
}

static void bind_text_or_null(sqlite3_stmt *st, int i, const char *v)
{
    if (nz(v)) sqlite3_bind_text(st, i, v, -1, SQLITE_TRANSIENT); else sqlite3_bind_null(st, i);
}

/* ---- the journal ------------------------------------------------------------------------------------------------------ */

static void new_op_id(lhcat *c, char *out)
{
    unsigned char r[8];
    if (getrandom(r, sizeof r, 0) != (ssize_t)sizeof r) {
        uint64_t t = (uint64_t)lhcat__now_ms() ^ ((uint64_t)getpid() << 40);
        memcpy(r, &t, sizeof r);
    }
    snprintf(out, 64, "%.40s:%02x%02x%02x%02x%02x%02x%02x%02x", c->host, r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
}

static const char *OPS[] = { "", "set", "unset", "tag_add", "tag_remove" };

static int journal(lhcat *c, const char *op_id, int64_t object, const char *rel, const char *attr, int64_t base_rev,
                   const char *op, const char *value, const char *old, const char *status)
{
    char id[64];
    if (!op_id) { new_op_id(c, id); op_id = id; }
    sqlite3_stmt *st = lhcat__prepare(c, "INSERT INTO journal(op_id, object_id, path, attr, base_rev, op, value, old, at, host,"
                                         " program, status) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)");
    if (!st) return -1;
    sqlite3_bind_text(st, 1, op_id, -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 2, object);
    sqlite3_bind_text(st, 3, rel ? rel : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 4, attr, -1, SQLITE_STATIC);
    if (base_rev >= 0) sqlite3_bind_int64(st, 5, base_rev); else sqlite3_bind_null(st, 5);
    sqlite3_bind_text(st, 6, op, -1, SQLITE_STATIC);
    bind_text_or_null(st, 7, value);
    bind_text_or_null(st, 8, old);
    sqlite3_bind_int64(st, 9, lhcat__now_ms());
    sqlite3_bind_text(st, 10, c->host, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 11, c->program, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 12, status, -1, SQLITE_STATIC);
    int ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok ? 0 : lhcat__dbfail(c, "journal");
}

/* the pending operations on an attribute settled: status 'pending' (and 'conflict' too when also) -> to */
static int settle(lhcat *c, int64_t object, const char *attr, const char *to, int also_conflicts)
{
    sqlite3_stmt *st = lhcat__prepare(c, also_conflicts
        ? "UPDATE journal SET status = ?3 WHERE object_id = ?1 AND attr = ?2 AND status IN ('pending', 'conflict')"
        : "UPDATE journal SET status = ?3 WHERE object_id = ?1 AND attr = ?2 AND status = 'pending'");
    if (!st) return -1;
    sqlite3_bind_int64(st, 1, object);
    sqlite3_bind_text(st, 2, attr, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, to, -1, SQLITE_STATIC);
    int ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok ? 0 : lhcat__dbfail(c, "journal");
}

/* ---- an attribute row ---------------------------------------------------------------------------------------------- */

struct arow { int found; char *observed, *desired; int64_t rev; char pending[12]; };

static void arow_free(struct arow *r) { free(r->observed); free(r->desired); memset(r, 0, sizeof *r); }

static int arow_get(lhcat *c, int64_t object, const char *attr, struct arow *r)
{
    memset(r, 0, sizeof *r);
    sqlite3_stmt *st = lhcat__prepare(c, "SELECT observed, desired, rev, pending FROM attrs WHERE object_id = ?1 AND name = ?2");
    if (!st) return -1;
    sqlite3_bind_int64(st, 1, object);
    sqlite3_bind_text(st, 2, attr, -1, SQLITE_STATIC);
    if (sqlite3_step(st) == SQLITE_ROW) {
        r->found = 1;
        r->observed = dup_col(st, 0);
        r->desired = dup_col(st, 1);
        r->rev = sqlite3_column_int64(st, 2);
        snprintf(r->pending, sizeof r->pending, "%s", (const char *)sqlite3_column_text(st, 3));
    } else snprintf(r->pending, sizeof r->pending, "none");
    sqlite3_finalize(st);
    return 0;
}

/* the row written as r says (observed_at when observed is set to now); a row with nothing in it goes */
static int arow_put(lhcat *c, int64_t object, const char *attr, const struct arow *r, int observed_now)
{
    sqlite3_stmt *st;
    if (!nz(r->observed) && !nz(r->desired) && !strcmp(r->pending, "none")) {
        st = lhcat__prepare(c, "DELETE FROM attrs WHERE object_id = ?1 AND name = ?2");
        if (!st) return -1;
        sqlite3_bind_int64(st, 1, object);
        sqlite3_bind_text(st, 2, attr, -1, SQLITE_STATIC);
    } else {
        st = lhcat__prepare(c,
            "INSERT INTO attrs(object_id, name, observed, observed_at, desired, rev, pending, changed_at)"
            " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8) ON CONFLICT(object_id, name) DO UPDATE SET"
            " observed = excluded.observed, observed_at = coalesce(excluded.observed_at, attrs.observed_at),"
            " desired = excluded.desired, rev = excluded.rev, pending = excluded.pending, changed_at = excluded.changed_at");
        if (!st) return -1;
        int64_t now = lhcat__now_ms();
        sqlite3_bind_int64(st, 1, object);
        sqlite3_bind_text(st, 2, attr, -1, SQLITE_STATIC);
        bind_text_or_null(st, 3, r->observed);
        if (observed_now) sqlite3_bind_int64(st, 4, now); else sqlite3_bind_null(st, 4);
        bind_text_or_null(st, 5, r->desired);
        sqlite3_bind_int64(st, 6, r->rev);
        sqlite3_bind_text(st, 7, r->pending, -1, SQLITE_STATIC);
        sqlite3_bind_int64(st, 8, now);
    }
    int ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok ? 0 : lhcat__dbfail(c, "attribute");
}

static void set_str(char **dst, const char *v) { free(*dst); *dst = nz(v) ? strdup(v) : NULL; }

/* ---- the tags table and the text index ---------------------------------------------------------------------------- */

int lhcat__derived(lhcat *c, int64_t object)
{
    sqlite3_stmt *del = lhcat__prepare(c, "DELETE FROM tags WHERE object_id = ?1");
    sqlite3_stmt *get = lhcat__prepare(c, "SELECT name, desired FROM attrs WHERE object_id = ?1 AND name IN "
                                          "('user.lux.desc', 'user.xdg.comment', 'user.xdg.tags')");
    sqlite3_stmt *tag = lhcat__prepare(c, "INSERT OR IGNORE INTO tags(tag, object_id) VALUES(?1, ?2)");
    sqlite3_stmt *fdel = lhcat__prepare(c, "DELETE FROM text WHERE rowid = ?1");
    sqlite3_stmt *fins = lhcat__prepare(c, "INSERT INTO text(rowid, desc, comment, tags) VALUES(?1, ?2, ?3, ?4)");
    char *desc = NULL, *comment = NULL, *tags = NULL;
    int rc = -1;
    if (!del || !get || !tag || !fdel || !fins) goto out;
    sqlite3_bind_int64(del, 1, object);
    sqlite3_step(del);
    sqlite3_bind_int64(fdel, 1, object);
    sqlite3_step(fdel);
    sqlite3_bind_int64(get, 1, object);
    while (sqlite3_step(get) == SQLITE_ROW) {
        const char *n = (const char *)sqlite3_column_text(get, 0);
        char *v = dup_col(get, 1);
        if (!v) continue;
        if (!strcmp(n, "user.lux.desc")) desc = v;
        else if (!strcmp(n, "user.xdg.comment")) comment = v;
        else tags = v;
    }
    if (tags) {   /* the stored list's items, split as flxp splits them */
        char *t = lhcat_normalize("user.xdg.tags", tags);
        for (char *p = t, *e; p && *p; p = e + 1) {
            e = strchr(p, ',');
            if (e) *e = 0;
            sqlite3_reset(tag);
            sqlite3_bind_text(tag, 1, p, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(tag, 2, object);
            sqlite3_step(tag);
            if (!e) break;
        }
        free(t);
    }
    if (desc || comment || tags) {
        sqlite3_bind_int64(fins, 1, object);
        sqlite3_bind_text(fins, 2, desc ? desc : "", -1, SQLITE_STATIC);
        sqlite3_bind_text(fins, 3, comment ? comment : "", -1, SQLITE_STATIC);
        sqlite3_bind_text(fins, 4, tags ? tags : "", -1, SQLITE_STATIC);
        if (sqlite3_step(fins) != SQLITE_DONE) { lhcat__dbfail(c, "text index"); goto out; }
    }
    rc = 0;
out:
    free(desc); free(comment); free(tags);
    sqlite3_finalize(del); sqlite3_finalize(get); sqlite3_finalize(tag); sqlite3_finalize(fdel); sqlite3_finalize(fins);
    return rc;
}

/* ---- observing ---------------------------------------------------------------------------------------------------------- */

/* attribute `attr` of object seen on the file as v (NULL: not set). 1 when it made a conflict, 0 not, -1 an error;
   *derived set when the desired value changed */
static int observe_one(lhcat *c, int64_t object, const char *rel, const char *attr, const char *v, int *derived)
{
    struct arow r;
    if (arow_get(c, object, attr, &r)) return -1;
    int conflict = 0, rc = 0;
    if (!r.found) {
        if (nz(v)) {
            set_str(&r.observed, v);
            set_str(&r.desired, v);
            rc = arow_put(c, object, attr, &r, 1);
            *derived = 1;
        }
    } else if (!same(r.observed, v)) {
        char *before = r.observed ? strdup(r.observed) : NULL;
        set_str(&r.observed, v);
        if (!strcmp(r.pending, "write")) {          /* changed under a write not done yet: the user decides */
            snprintf(r.pending, sizeof r.pending, "conflict");
            conflict = 1;
            rc = settle(c, object, attr, "conflict", 0);
        } else if (!strcmp(r.pending, "none") && c->catalogue_authority && !same(r.desired, v)) {
            /* the catalogue is the authority: its value is still the one wanted, a write to make (diff, commit) */
            snprintf(r.pending, sizeof r.pending, "write");
        } else if (!strcmp(r.pending, "none") && !c->catalogue_authority && !same(r.desired, v)) {
            /* the file system is the authority: its value is the desired one now, noted in the journal */
            rc = journal(c, NULL, object, rel, attr, r.rev, "observed", v, r.desired, "applied");
            set_str(&r.desired, v);
            r.rev++;
            *derived = 1;
        }
        free(before);
        if (!rc) rc = arow_put(c, object, attr, &r, 1);
    }
    arow_free(&r);
    return rc < 0 ? -1 : conflict;
}

int lhcat__observe(lhcat *c, int64_t object, const char *rel, char **nv, int n)
{
    int derived = 0, conflicts = 0;
    for (int i = 0; i < n; i++) {
        int r = observe_one(c, object, rel, nv[2 * i], nv[2 * i + 1], &derived);
        if (r < 0) return -1;
        conflicts += r;
    }
    /* what the catalogue had observed and the file no longer has */
    sqlite3_stmt *st = lhcat__prepare(c, "SELECT name FROM attrs WHERE object_id = ?1 AND observed IS NOT NULL");
    if (!st) return -1;
    sqlite3_bind_int64(st, 1, object);
    char **gone = NULL;
    int ng = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(st, 0);
        int seen = 0;
        for (int i = 0; i < n && !seen; i++) seen = !strcmp(nv[2 * i], name);
        if (seen) continue;
        char **m = realloc(gone, (size_t)(ng + 1) * sizeof *m);
        if (!m) break;
        gone = m;
        gone[ng++] = strdup(name);
    }
    sqlite3_finalize(st);
    int rc = 0;
    for (int i = 0; i < ng; i++) {
        int r = rc ? 0 : observe_one(c, object, rel, gone[i], NULL, &derived);
        if (r < 0) rc = -1; else conflicts += r;
        free(gone[i]);
    }
    free(gone);
    if (rc) return -1;
    if (derived && lhcat__derived(c, object)) return -1;
    return conflicts;
}

/* ---- operations ------------------------------------------------------------------------------------------------------ */

static int64_t present_object(lhcat *c, const char *rel)
{
    sqlite3_stmt *st = lhcat__prepare(c, "SELECT object_id FROM locations WHERE id = ?1 AND state = 'present'");
    if (!st) return 0;
    sqlite3_bind_int64(st, 1, lhcat_id(c, rel));
    int64_t o = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int64(st, 0) : 0;
    sqlite3_finalize(st);
    return o;
}

static int status_of(const char *s)
{
    return !strcmp(s, "applied") ? LHCAT_APPLIED : !strcmp(s, "pending") ? LHCAT_PENDING : LHCAT_CONFLICT;
}

/* the pending value of one attribute written to the file at rel, if the file still has what was last observed.
   Its own transaction. LHCAT_APPLIED, LHCAT_PENDING (the write failed: c->err), LHCAT_CONFLICT; -1 a database error */
static int write_back(lhcat *c, int64_t object, const char *rel, const char *attr)
{
    if (lhcat__exec(c, "BEGIN IMMEDIATE")) return -1;
    struct arow r;
    int result = LHCAT_APPLIED;
    if (arow_get(c, object, attr, &r)) goto dberr;
    if (strcmp(r.pending, "write")) { result = !strcmp(r.pending, "conflict") ? LHCAT_CONFLICT : LHCAT_APPLIED; goto done; }
    char abs[PATH_MAX];
    lhcat__abs(c, rel, abs, sizeof abs);
    char *now = NULL;
    if (lhcat__read_one(abs, attr, &now, 1) < 0) {
        lhcat__fail(c, "%s: %s could not be read (%s): not written", abs, attr, strerror(errno));
        result = LHCAT_PENDING;
        goto done;
    }
    if (!same(now, r.observed)) {   /* an external change the catalogue has not seen: never written over */
        set_str(&r.observed, now);
        free(now);
        snprintf(r.pending, sizeof r.pending, "conflict");
        if (arow_put(c, object, attr, &r, 1) || settle(c, object, attr, "conflict", 0)) goto dberr;
        result = LHCAT_CONFLICT;
        goto done;
    }
    free(now);
    int w = nz(r.desired) ? lhde_xattr_set(abs, attr, r.desired, strlen(r.desired)) : lhde_xattr_remove(abs, attr);
    if (w != 0 && !(!nz(r.desired) && errno == LHDE_ENOATTR)) {
        lhcat__fail(c, "%s: %s", abs, errno == ENOTSUP ? "the file system keeps no user attributes" : strerror(errno));
        result = LHCAT_PENDING;
        goto done;
    }
    char *back = NULL;
    lhcat__read_one(abs, attr, &back, 1);
    if (!same(back, r.desired)) {
        lhcat__fail(c, "%s: %s reads back as something else", abs, attr);
        free(back);
        result = LHCAT_PENDING;
        goto done;
    }
    free(back);
    set_str(&r.observed, r.desired);
    snprintf(r.pending, sizeof r.pending, "none");
    if (arow_put(c, object, attr, &r, 1) || settle(c, object, attr, "applied", 0)) goto dberr;
done:
    arow_free(&r);
    {
        char keep[sizeof c->err];
        memcpy(keep, c->err, sizeof keep);
        if (lhcat__exec(c, "COMMIT")) return -1;
        memcpy(c->err, keep, sizeof keep);
    }
    return result;
dberr:
    arow_free(&r);
    sqlite3_exec(c->db, "ROLLBACK", NULL, NULL, NULL);
    return -1;
}

/* a tag list with tag added or taken out */
static char *tag_change(const char *list, const char *tag, int add)
{
    char *norm = lhcat_normalize("user.xdg.tags", list ? list : "");
    char *t = lhcat_normalize("user.xdg.tags", tag);
    if (!norm || !t) { free(norm); free(t); return NULL; }
    size_t cap = strlen(norm) + strlen(t) + 2;
    char *out = malloc(cap);
    if (!out) { free(norm); free(t); return NULL; }
    out[0] = 0;
    for (char *p = norm, *e; *p; p = e + 1) {   /* the others in their order */
        e = strchr(p, ',');
        if (e) *e = 0;
        int drop = 0;
        for (char *q = t, *f; *q && !drop; q = f + 1) {   /* "a,b" given: each */
            f = strchr(q, ',');
            size_t len = f ? (size_t)(f - q) : strlen(q);
            drop = strlen(p) == len && !strncmp(p, q, len);
            if (!f) break;
        }
        if (!drop) { if (*out) strcat(out, ","); strcat(out, p); }
        if (!e) break;
    }
    if (add && *t) { if (*out) strcat(out, ","); strcat(out, t); }
    free(norm);
    free(t);
    return out;
}

int lhcat_apply(lhcat *c, const char *rel, const struct lhcat_op *op, int write, int *status, char *op_id_out)
{
    char id[64];
    if (op->op_id && *op->op_id) snprintf(id, sizeof id, "%s", op->op_id); else new_op_id(c, id);
    if (op_id_out) snprintf(op_id_out, 64, "%s", id);
    if (op->op < LHCAT_SET || op->op > LHCAT_TAG_REMOVE || (op->op < LHCAT_TAG_ADD && (!op->attr || !*op->attr)))
        return lhcat__fail(c, "not an operation");
    const char *attr = op->op >= LHCAT_TAG_ADD ? "user.xdg.tags" : op->attr;
    c->err[0] = 0;
    if (lhcat__exec(c, "BEGIN IMMEDIATE")) return -1;
    struct arow r = { 0 };
    char *value = NULL;
    int result;
    /* the same operation twice: the first one's outcome; a pending write is tried again (a lost acknowledgement) */
    sqlite3_stmt *seen = lhcat__prepare(c, "SELECT status, object_id, path FROM journal WHERE op_id = ?1");
    if (!seen) goto dberr;
    sqlite3_bind_text(seen, 1, id, -1, SQLITE_STATIC);
    if (sqlite3_step(seen) == SQLITE_ROW) {
        result = status_of((const char *)sqlite3_column_text(seen, 0));
        int64_t obj = sqlite3_column_int64(seen, 1);
        char *path = dup_col(seen, 2);
        sqlite3_finalize(seen);
        if (lhcat__exec(c, "COMMIT")) { free(path); return -1; }
        if (result == LHCAT_PENDING && write) {
            int w = write_back(c, obj, path ? path : "", attr);
            if (w < 0) { free(path); return -1; }
            result = w;
        }
        free(path);
        *status = LHCAT_DUPLICATE | (result << 4);
        return 0;
    }
    sqlite3_finalize(seen);
    /* the file observed now, so the value a write replaces is the one on disk; a deferred operation does not look at the
       file (it may not be reachable: an offline root, a dead mount; the commit compares with what is on disk then) */
    if (write && lhcat_touch(c, rel) != 0) {
        if (!c->err[0]) lhcat__fail(c, "%s: not there", rel);
        goto fail;
    }
    int64_t object = present_object(c, rel);
    if (!object) { lhcat__fail(c, "%s: not in the catalogue", rel); goto fail; }
    if (arow_get(c, object, attr, &r)) goto dberr;
    int64_t base = op->base_rev >= 0 ? op->base_rev : r.rev;
    const char *opname = OPS[op->op];
    if ((op->op == LHCAT_SET || op->op == LHCAT_UNSET) && op->base_rev >= 0 && op->base_rev != r.rev) {
        if (journal(c, id, object, rel, attr, op->base_rev, opname, op->value, r.desired, "conflict")) goto dberr;
        result = LHCAT_CONFLICT;
    } else {
        if (op->op == LHCAT_SET) value = lhcat_normalize(attr, op->value);
        else if (op->op == LHCAT_TAG_ADD || op->op == LHCAT_TAG_REMOVE) value = tag_change(r.desired, op->value, op->op == LHCAT_TAG_ADD);
        if (value && !*value) { free(value); value = NULL; }
        char *old = r.desired ? strdup(r.desired) : NULL;
        if (!same(value, r.desired)) {
            set_str(&r.desired, value);
            r.rev++;
            if (!strcmp(r.pending, "none") || !strcmp(r.pending, "write"))
                snprintf(r.pending, sizeof r.pending, "%s", same(r.desired, r.observed) ? "none" : "write");
        }
        result = !strcmp(r.pending, "write") ? LHCAT_PENDING : !strcmp(r.pending, "conflict") ? LHCAT_CONFLICT : LHCAT_APPLIED;
        int jr = journal(c, id, object, rel, attr, base,
                         opname, op->op == LHCAT_TAG_ADD || op->op == LHCAT_TAG_REMOVE ? op->value : value, old,
                         result == LHCAT_PENDING ? "pending" : result == LHCAT_CONFLICT ? "conflict" : "applied");
        free(old);
        if (jr || arow_put(c, object, attr, &r, 0) || lhcat__derived(c, object)) goto dberr;
    }
    if (lhcat__exec(c, "COMMIT")) goto fail;   /* the operation is durable before anything is written */
    if (result == LHCAT_PENDING && write) {
        int w = write_back(c, object, rel, attr);
        if (w < 0) { arow_free(&r); free(value); return -1; }
        result = w;
    }
    arow_free(&r);
    free(value);
    *status = result;
    return 0;
dberr:
    lhcat__dbfail(c, "apply");
fail:
    {
        char keep[sizeof c->err];
        memcpy(keep, c->err, sizeof keep);
        sqlite3_exec(c->db, "ROLLBACK", NULL, NULL, NULL);
        memcpy(c->err, keep, sizeof keep);
    }
    arow_free(&r);
    free(value);
    return -1;
}

int lhcat_commit(lhcat *c, const char *rel, uint64_t *written, uint64_t *conflicts)
{
    sqlite3_stmt *st = rel
        ? lhcat__prepare(c, "SELECT a.object_id, a.name, l.id FROM attrs a JOIN locations l ON l.object_id = a.object_id"
                            " AND l.state = 'present' WHERE a.pending = 'write' AND l.id = ?1")
        : lhcat__prepare(c, "SELECT a.object_id, a.name, l.id FROM attrs a JOIN locations l ON l.object_id = a.object_id"
                            " AND l.state = 'present' WHERE a.pending = 'write' ORDER BY l.id");
    if (!st) return -1;
    if (rel) sqlite3_bind_int64(st, 1, lhcat_id(c, rel));
    struct item { int64_t object, loc; char *attr; } *items = NULL;
    size_t n = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        struct item *m = realloc(items, (n + 1) * sizeof *m);
        if (!m) break;
        items = m;
        items[n].object = sqlite3_column_int64(st, 0);
        items[n].attr = dup_col(st, 1);
        items[n].loc = sqlite3_column_int64(st, 2);
        n++;
    }
    sqlite3_finalize(st);
    int rc = 0;
    for (size_t i = 0; i < n; i++) {
        const char *p = lhcat_path(c, items[i].loc);
        char *path = p ? strdup(p) : NULL;
        int w = path ? write_back(c, items[i].object, path, items[i].attr) : -1;
        free(path);
        if (w == LHCAT_APPLIED && written) (*written)++;
        else if (w == LHCAT_CONFLICT && conflicts) (*conflicts)++;
        else if (w != LHCAT_APPLIED && w != LHCAT_CONFLICT) rc = -1;   /* c->err says which; the others go on */
        free(items[i].attr);
    }
    free(items);
    return rc;
}

int lhcat_resolve(lhcat *c, const char *rel, const char *attr, int take_catalogue)
{
    if (lhcat__exec(c, "BEGIN IMMEDIATE")) return -1;
    struct arow r = { 0 };
    int64_t object = present_object(c, rel);
    if (!object) { lhcat__fail(c, "%s: not in the catalogue", rel); goto fail; }
    if (arow_get(c, object, attr, &r)) goto fail;
    if (!r.found) { lhcat__fail(c, "%s: no %s in the catalogue", rel, attr); goto fail; }
    char abs[PATH_MAX];
    char *now = NULL;
    if (lhcat__read_one(lhcat__abs(c, rel, abs, sizeof abs), attr, &now, 1) < 0) {
        lhcat__fail(c, "%s: %s could not be read (%s)", abs, attr, strerror(errno));
        goto fail;
    }
    set_str(&r.observed, now);   /* settled against the value on the file now */
    free(now);
    if (take_catalogue) {
        snprintf(r.pending, sizeof r.pending, "%s", same(r.desired, r.observed) ? "none" : "write");
        /* the operations in conflict go out with the next commit (or are done, when the file agrees already) */
        const char *to = same(r.desired, r.observed) ? "applied" : "pending";
        if (settle(c, object, attr, to, 1) || journal(c, NULL, object, rel, attr, r.rev, "resolve", r.desired, r.observed, to))
            goto fail;
    } else {
        if (settle(c, object, attr, "rejected", 1) || journal(c, NULL, object, rel, attr, r.rev, "resolve", r.observed, r.desired, "applied"))
            goto fail;
        if (!same(r.desired, r.observed)) { set_str(&r.desired, r.observed); r.rev++; }
        snprintf(r.pending, sizeof r.pending, "none");
    }
    if (arow_put(c, object, attr, &r, 1) || lhcat__derived(c, object)) goto fail;
    arow_free(&r);
    return lhcat__exec(c, "COMMIT");
fail:
    {
        char keep[sizeof c->err];
        memcpy(keep, c->err, sizeof keep);
        sqlite3_exec(c->db, "ROLLBACK", NULL, NULL, NULL);
        memcpy(c->err, keep, sizeof keep);
    }
    arow_free(&r);
    return -1;
}

int lhcat_record_write(lhcat *c, const char *rel, const char *xattr, const char *old, const char *value)
{
    if (lhcat__exec(c, "BEGIN IMMEDIATE")) return -1;
    struct arow r = { 0 };
    int64_t object = present_object(c, rel);
    if (object) {   /* the write as an operation; the observation that follows agrees with it */
        if (arow_get(c, object, xattr, &r)) goto fail;
        if (!same(r.desired, value)) { set_str(&r.desired, value); r.rev++; }
        set_str(&r.observed, value);
        snprintf(r.pending, sizeof r.pending, "none");
        if (settle(c, object, xattr, "rejected", 1) ||
            journal(c, NULL, object, rel, xattr, r.rev, nz(value) ? "set" : "unset", value, old, "applied") ||
            arow_put(c, object, xattr, &r, 1) || lhcat__derived(c, object))
            goto fail;
    }
    if (lhcat_touch(c, rel) < 0) goto fail;
    if (!object && (object = present_object(c, rel)))
        if (journal(c, NULL, object, rel, xattr, 0, nz(value) ? "set" : "unset", value, old, "applied")) goto fail;
    arow_free(&r);
    return lhcat__exec(c, "COMMIT");
fail:
    {
        char keep[sizeof c->err];
        memcpy(keep, c->err, sizeof keep);
        sqlite3_exec(c->db, "ROLLBACK", NULL, NULL, NULL);
        memcpy(c->err, keep, sizeof keep);
    }
    arow_free(&r);
    return -1;
}

/* ---- gone ---------------------------------------------------------------------------------------------------------- */

int lhcat__mark_gone(lhcat *c, uint64_t *count)
{
    /* and everything under a directory that is gone */
    if (lhcat__exec(c, "WITH RECURSIVE under(id) AS (SELECT id FROM lhcat_gone UNION"
                       " SELECT l.id FROM under u CROSS JOIN locations l ON l.parent = u.id)"
                       " INSERT OR IGNORE INTO lhcat_gone SELECT id FROM under"))
        return -1;
    if (lhcat__exec(c, "UPDATE locations SET state = 'gone' WHERE state = 'present' AND id IN (SELECT id FROM lhcat_gone)"))
        return -1;
    if (count) *count += (uint64_t)sqlite3_changes(c->db);
    lhcat__forget_all(c);
    return lhcat__exec(c, "DELETE FROM lhcat_gone");
}

/* ---- a folder's rows ---------------------------------------------------------------------------------------------- */

int lhcat_list_dir(lhcat *c, const char *rel, lhcat_entry_fn fn, void *ctx)
{
    int64_t dir = lhcat_id(c, rel);
    if (!dir) return lhcat__fail(c, "%s: not in the catalogue", rel && *rel ? rel : "(the root)");
    sqlite3_stmt *st = lhcat__prepare(c,
        "SELECT l.id, l.name, l.kind, l.size, l.mtime, l.ctime, l.coverage, a.name, a.desired FROM locations l"
        " LEFT JOIN attrs a ON a.object_id = l.object_id AND a.desired IS NOT NULL"
        " WHERE l.parent = ?1 AND l.state = 'present' ORDER BY l.id");
    if (!st) return -1;
    sqlite3_bind_int64(st, 1, dir);
    struct lhcat_entry e;
    memset(&e, 0, sizeof e);
    char *name = NULL;
    char **nv = NULL;
    int n = 0, cap = 0, r;
    for (;;) {
        r = sqlite3_step(st);
        int64_t id = r == SQLITE_ROW ? sqlite3_column_int64(st, 0) : 0;
        if (e.id && id != e.id) {   /* the previous location is complete */
            e.name = name;
            e.nattrs = n;
            e.attrs = (const char *const *)nv;
            if (fn) fn(ctx, &e);
            free(name);
            name = NULL;
            for (int i = 0; i < 2 * n; i++) free(nv[i]);
            n = 0;
            e.id = 0;
        }
        if (r != SQLITE_ROW) break;
        if (!e.id) {
            e.id = id;
            name = dup_col(st, 1);
            const char *k = (const char *)sqlite3_column_text(st, 2);
            e.kind = k ? k[0] : 'o';
            e.size = sqlite3_column_type(st, 3) == SQLITE_NULL ? -1 : sqlite3_column_int64(st, 3);
            e.mtime = sqlite3_column_type(st, 4) == SQLITE_NULL ? -1 : sqlite3_column_int64(st, 4);
            e.ctime_ns = sqlite3_column_type(st, 5) == SQLITE_NULL ? -1 : sqlite3_column_int64(st, 5);
            e.coverage = sqlite3_column_int(st, 6);
        }
        if (sqlite3_column_type(st, 7) != SQLITE_NULL) {
            if (n == cap) {
                cap = cap ? cap * 2 : 8;
                char **m = realloc(nv, (size_t)cap * 2 * sizeof *m);
                if (!m) break;
                nv = m;
            }
            nv[2 * n] = dup_col(st, 7);
            nv[2 * n + 1] = dup_col(st, 8);
            n++;
        }
    }
    free(name);
    for (int i = 0; i < 2 * n; i++) free(nv[i]);
    free(nv);
    sqlite3_finalize(st);
    return 0;
}
