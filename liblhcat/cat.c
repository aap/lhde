/* liblhcat: the roots, opening a catalogue and its schema, rows by path, attributes and the journal. */
#define _GNU_SOURCE
#include "internal.h"
#include "liblhde/compat.h"
#include "lhde/settings.h"
#include <ctype.h>
#include <pwd.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* ---- the schema ----------------------------------------------------------------------------------------------------- */
static int widen_state(lhcat *c);

/* Each migration takes a database from the version before it to its own; a new database runs them all. fn (when set)
   runs after the SQL, in the same transaction. */
static const struct { int version; const char *what; const char *sql; int (*fn)(lhcat *); } MIGRATIONS[] = {
    { 1, "roots, objects, locations, relations, hashes, attrs, tags, picture, thumbs, journal, scans",
      "CREATE TABLE roots(name TEXT PRIMARY KEY, path TEXT NOT NULL, host TEXT,"
      "  authority TEXT NOT NULL DEFAULT 'filesystem' CHECK(authority IN ('filesystem', 'catalogue')),"
      "  added_at INTEGER, listed_at INTEGER, indexed_at INTEGER, offline INTEGER NOT NULL DEFAULT 0);"
      /* the catalogue's own identities; everything the file system says is evidence about them */
      "CREATE TABLE objects(id INTEGER PRIMARY KEY, created_at INTEGER);"
      /* a path under the root (the chain of its parents' names; the root's own location: parent 0, name "") with the
         facts last observed there. kind f d l o; size the length (stat), usage_kib the disk usage (a du listing);
         ctime in nanoseconds (the freshness check); coverage LHCAT_LISTED...; listed_in the scan that listed this
         directory whole */
      "CREATE TABLE locations(id INTEGER PRIMARY KEY, object_id INTEGER NOT NULL, parent INTEGER NOT NULL,"
      "  name TEXT NOT NULL, kind TEXT NOT NULL, dev INTEGER, ino INTEGER, size INTEGER, usage_kib INTEGER,"
      "  mtime INTEGER, ctime INTEGER, type TEXT, observed_at INTEGER,"
      "  state TEXT NOT NULL DEFAULT 'present' CHECK(state IN ('present', 'gone')),"
      "  coverage INTEGER NOT NULL DEFAULT 0, listed_in INTEGER, UNIQUE(parent, name));"
      "CREATE INDEX locations_object ON locations(object_id);"
      "CREATE INDEX locations_name ON locations(name);"
      "CREATE INDEX locations_type ON locations(type);"
      "CREATE INDEX locations_ino ON locations(ino) WHERE ino IS NOT NULL;"
      /* what the evidence suggests about two objects (moved, replaced, duplicate), for the user to confirm */
      "CREATE TABLE relations(object_a INTEGER NOT NULL, object_b INTEGER NOT NULL, kind TEXT NOT NULL,"
      "  confidence REAL, noted_at INTEGER, confirmed INTEGER, PRIMARY KEY(object_a, object_b, kind)) WITHOUT ROWID;"
      "CREATE TABLE hashes(object_id INTEGER NOT NULL, algo TEXT NOT NULL, prefix64k TEXT, full TEXT,"
      "  computed_at INTEGER, version TEXT, PRIMARY KEY(object_id, algo)) WITHOUT ROWID;"
      "CREATE INDEX hashes_full ON hashes(full);"
      /* observed: the file's value when last read (NULL not set); desired: the catalogue's, at revision rev;
         pending: none, write (desired not written yet), conflict */
      "CREATE TABLE attrs(object_id INTEGER NOT NULL, name TEXT NOT NULL, observed TEXT, observed_at INTEGER,"
      "  desired TEXT, rev INTEGER NOT NULL DEFAULT 0,"
      "  pending TEXT NOT NULL DEFAULT 'none' CHECK(pending IN ('none', 'write', 'conflict')), changed_at INTEGER,"
      "  PRIMARY KEY(object_id, name)) WITHOUT ROWID;"
      "CREATE INDEX attrs_name_desired ON attrs(name, desired);"
      "CREATE INDEX attrs_pending ON attrs(pending) WHERE pending != 'none';"
      "CREATE TABLE tags(tag TEXT NOT NULL COLLATE NOCASE, object_id INTEGER NOT NULL, PRIMARY KEY(tag, object_id)) WITHOUT ROWID;"
      "CREATE INDEX tags_object ON tags(object_id);"
      "CREATE TABLE picture(object_id INTEGER PRIMARY KEY, taken_at TEXT, width INTEGER, height INTEGER,"
      "  orientation INTEGER, camera TEXT);"
      "CREATE INDEX picture_taken ON picture(taken_at);"
      /* version: the content the thumbnail shows (size:mtime); uri the freedesktop cache's key */
      "CREATE TABLE thumbs(object_id INTEGER NOT NULL, variant INTEGER NOT NULL, version TEXT, uri TEXT,"
      "  cache_path TEXT, made_at INTEGER, PRIMARY KEY(object_id, variant)) WITHOUT ROWID;"
      /* operations: op set unset tag_add tag_remove (edits), observed (a scan saw the file change), resolve;
         status applied pending conflict rejected */
      "CREATE TABLE journal(seq INTEGER PRIMARY KEY, op_id TEXT NOT NULL UNIQUE, object_id INTEGER, path TEXT,"
      "  attr TEXT NOT NULL, base_rev INTEGER, op TEXT NOT NULL, value TEXT, old TEXT, at INTEGER NOT NULL,"
      "  host TEXT, program TEXT, status TEXT NOT NULL);"
      "CREATE INDEX journal_object ON journal(object_id, attr);"
      "CREATE INDEX journal_at ON journal(at);"
      "CREATE TABLE scans(id INTEGER PRIMARY KEY, subpath TEXT NOT NULL, started INTEGER NOT NULL, finished INTEGER,"
      "  status TEXT NOT NULL, cursor TEXT, entries INTEGER);", NULL },
    /* a location's state 'excluded': not walked, by the root's or the run's exclude list (and what was under it); kind
       'm': a mount point the walk did not enter. excludes: the root's exclude globs, one a line */
    { 2, "excluded locations, mount points, the root's exclude list",
      "ALTER TABLE roots ADD COLUMN excludes TEXT;", widen_state },
    /* dirs: every directory location's path ("" the root's), kept by lhcat__dir_set, so that a subtree is a range of the path
       index; locations_group: the type's group ("image/") with the rowid, so a group query in catalogue order stops at
       its limit */
    { 3, "directory paths (subtree queries), the type group index",
      "CREATE TABLE dirs(id INTEGER PRIMARY KEY, path TEXT NOT NULL);"
      "WITH RECURSIVE t(id, path) AS (SELECT id, '' FROM locations WHERE parent = 0 UNION ALL"
      "  SELECT l.id, CASE WHEN t.path = '' THEN l.name ELSE t.path || '/' || l.name END"
      "  FROM t JOIN locations l ON l.parent = t.id WHERE l.kind = 'd')"
      " INSERT INTO dirs(id, path) SELECT id, path FROM t;"
      "CREATE INDEX dirs_path ON dirs(path);"
      "CREATE INDEX locations_group ON locations(substr(type, 1, instr(type, '/')));", NULL },
};
#define NMIGRATIONS (int)(sizeof MIGRATIONS / sizeof MIGRATIONS[0])

/* The state CHECK of locations as migration 1 made it, widened by 'excluded': sqlite_schema's text is changed in place
   (every row satisfies the wider check, so nothing is rebuilt) and the schema version moved on so that every connection
   reads it again. The new value is an OR beside the two-value IN list: SQLite evaluates that per row as cheaply as the
   original, a three-value IN list in a CHECK costs a third more per insert. */
static int widen_state(lhcat *c)
{
    sqlite3_db_config(c->db, SQLITE_DBCONFIG_DEFENSIVE, 0, (int *)NULL);
    sqlite3_stmt *st = lhcat__prepare(c, "PRAGMA schema_version");
    if (!st) return -1;
    int v = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int(st, 0) : 0;
    sqlite3_finalize(st);
    char sql[512];
    snprintf(sql, sizeof sql, "PRAGMA writable_schema = ON;"
             "UPDATE sqlite_schema SET sql = replace(sql, 'CHECK(state IN (''present'', ''gone''))',"
             " 'CHECK(state IN (''present'', ''gone'') OR state = ''excluded'')') WHERE type = 'table' AND name = 'locations';"
             "PRAGMA schema_version = %d; PRAGMA writable_schema = OFF", v + 1);
    if (lhcat__exec(c, sql)) return -1;
    st = lhcat__prepare(c, "SELECT instr(sql, '''excluded''') FROM sqlite_schema WHERE type = 'table' AND name = 'locations'");
    int ok = st && sqlite3_step(st) == SQLITE_ROW && sqlite3_column_int(st, 0) > 0;
    sqlite3_finalize(st);
    return ok ? 0 : lhcat__fail(c, "the locations table's state check could not be widened");
}

/* the text index over a row's description, comment and tags (rowid = files.id): FTS5 when SQLite has it */
static const char *FTS_SQL = "CREATE VIRTUAL TABLE IF NOT EXISTS text USING fts5(desc, comment, tags, tokenize = 'unicode61')";

/* ---- small things ------------------------------------------------------------------------------------------------------ */

int lhcat__fail(lhcat *c, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->err, sizeof c->err, fmt, ap);
    va_end(ap);
    return -1;
}

int lhcat__dbfail(lhcat *c, const char *what) { return lhcat__fail(c, "%s: %s", what, sqlite3_errmsg(c->db)); }

int lhcat__exec(lhcat *c, const char *sql)
{
    char *msg = NULL;
    if (sqlite3_exec(c->db, sql, NULL, NULL, &msg) == SQLITE_OK) return 0;
    lhcat__fail(c, "%s", msg ? msg : sqlite3_errmsg(c->db));
    sqlite3_free(msg);
    return -1;
}

sqlite3_stmt *lhcat__prepare(lhcat *c, const char *sql)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v3(c->db, sql, -1, SQLITE_PREPARE_PERSISTENT, &st, NULL) != SQLITE_OK) {
        lhcat__dbfail(c, sql);
        return NULL;
    }
    return st;
}

int64_t lhcat__now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

const char *lhcat_error(lhcat *c) { return c ? c->err : ""; }
const char *lhcat_name(lhcat *c) { return c->name; }
const char *lhcat_root_path(lhcat *c) { return c->root; }
sqlite3 *lhcat_db(lhcat *c) { return c->db; }
int lhcat_catalogue_authority(lhcat *c) { return c->catalogue_authority; }
int lhcat_offline(lhcat *c) { return c->offline; }

int64_t lhcat_object(lhcat *c, int64_t location)
{
    sqlite3_stmt *st = lhcat__prepare(c, "SELECT object_id FROM locations WHERE id = ?1");
    if (!st) return 0;
    sqlite3_bind_int64(st, 1, location);
    int64_t o = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int64(st, 0) : 0;
    sqlite3_finalize(st);
    return o;
}

int lhcat__new_location(lhcat *c, int64_t parent, const char *name, size_t len, char kind, int64_t *id)
{
    sqlite3_stmt *o = lhcat__prepare(c, "INSERT INTO objects(created_at) VALUES(?1)");
    sqlite3_stmt *l = lhcat__prepare(c, "INSERT INTO locations(object_id, parent, name, kind, type) VALUES(?1, ?2, ?3, ?4, ?5)");
    int ok = o && l;
    if (ok) {
        sqlite3_bind_int64(o, 1, lhcat__now_ms() / 1000);
        ok = sqlite3_step(o) == SQLITE_DONE;
    }
    if (ok) {
        char k[2] = { kind, 0 };
        sqlite3_bind_int64(l, 1, sqlite3_last_insert_rowid(c->db));
        sqlite3_bind_int64(l, 2, parent);
        sqlite3_bind_text(l, 3, name, (int)len, SQLITE_STATIC);
        sqlite3_bind_text(l, 4, k, 1, SQLITE_TRANSIENT);
        if (kind == 'd') sqlite3_bind_text(l, 5, "inode/directory", -1, SQLITE_STATIC); else sqlite3_bind_null(l, 5);
        ok = sqlite3_step(l) == SQLITE_DONE;
    }
    if (!ok) lhcat__dbfail(c, "a new location");
    else *id = sqlite3_last_insert_rowid(c->db);
    sqlite3_finalize(o);
    sqlite3_finalize(l);
    if (ok && kind == 'd' && lhcat__dir_set(c, *id, parent, name, len, 1)) ok = 0;
    return ok;
}

int lhcat__dir_set(lhcat *c, int64_t id, int64_t parent, const char *name, size_t len, int is_dir)
{
    if (!is_dir) {
        if (!c->st_dir_del && !(c->st_dir_del = lhcat__prepare(c, "DELETE FROM dirs WHERE id = ?1"))) return -1;
        sqlite3_reset(c->st_dir_del);
        sqlite3_bind_int64(c->st_dir_del, 1, id);
        int ok = sqlite3_step(c->st_dir_del) == SQLITE_DONE;
        sqlite3_reset(c->st_dir_del);
        return ok ? 0 : lhcat__dbfail(c, "dirs");
    }
    /* the parent's path and the name ("" the root's own); a parent without a path (not a directory) gives the name */
    if (!c->st_dir_add && !(c->st_dir_add = lhcat__prepare(c,
            "INSERT OR REPLACE INTO dirs(id, path) VALUES(?1, CASE WHEN ?2 = 0 THEN '' ELSE coalesce((SELECT"
            " CASE WHEN path = '' THEN ?3 ELSE path || '/' || ?3 END FROM dirs WHERE id = ?2), ?3) END)")))
        return -1;
    sqlite3_reset(c->st_dir_add);
    sqlite3_bind_int64(c->st_dir_add, 1, id);
    sqlite3_bind_int64(c->st_dir_add, 2, parent);
    sqlite3_bind_text(c->st_dir_add, 3, name, (int)len, SQLITE_STATIC);
    int ok = sqlite3_step(c->st_dir_add) == SQLITE_DONE;
    sqlite3_reset(c->st_dir_add);
    return ok ? 0 : lhcat__dbfail(c, "dirs");
}

void lhcat_set_program(lhcat *c, const char *program)
{
    snprintf(c->program, sizeof c->program, "%s", program && *program ? program : lhde_progname());
}

const char *lhcat__abs(lhcat *c, const char *rel, char *out, size_t n)
{
    if (!rel || !*rel) snprintf(out, n, "%s", c->root);
    else if (!strcmp(c->root, "/")) snprintf(out, n, "/%s", rel);
    else snprintf(out, n, "%s/%s", c->root, rel);
    return out;
}

/* ---- roots ------------------------------------------------------------------------------------------------------------ */

const char *lhcat_dir(char *out, size_t n)
{
    char st[PATH_MAX];
    lhde_state_dir(st, sizeof st);
    snprintf(out, n, "%s/catalogue", st);
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s", out);
    for (char *s = p + 1; *s; s++)   /* each missing directory on the way */
        if (*s == '/') { *s = 0; mkdir(p, 0755); *s = '/'; }
    mkdir(p, 0755);
    return out;
}

static int valid_name(const char *name)
{
    size_t n = strlen(name);
    if (n == 0 || n > LHCAT_NAME_MAX || name[0] == '.') return 0;
    for (const char *p = name; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_' && *p != '.') return 0;
    return 1;
}

static const char *db_path(const char *name, char *out, size_t n)
{
    char dir[PATH_MAX];
    lhcat_dir(dir, sizeof dir);
    snprintf(out, n, "%s/%s.db", dir, name);
    return out;
}

static int migrate(lhcat *c)
{
    if (lhcat__exec(c, "CREATE TABLE IF NOT EXISTS migrations(version INTEGER PRIMARY KEY, applied_at INTEGER, what TEXT)"))
        return -1;
    sqlite3_stmt *st = lhcat__prepare(c, "SELECT coalesce(max(version), 0) FROM migrations");
    if (!st) return -1;
    int have = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int(st, 0) : 0;
    sqlite3_finalize(st);
    if (have > LHCAT_SCHEMA)
        return lhcat__fail(c, "the catalogue's schema is version %d, newer than this program's %d", have, LHCAT_SCHEMA);
    for (int i = 0; i < NMIGRATIONS; i++) {
        if (MIGRATIONS[i].version <= have) continue;
        if (lhcat__exec(c, "BEGIN IMMEDIATE")) return -1;
        char rec[512];
        sqlite3_snprintf(sizeof rec, rec, "INSERT INTO migrations VALUES(%d, %lld, %Q)", MIGRATIONS[i].version,
                 (long long)(lhcat__now_ms() / 1000), MIGRATIONS[i].what);
        if (lhcat__exec(c, MIGRATIONS[i].sql) || (MIGRATIONS[i].fn && MIGRATIONS[i].fn(c)) || lhcat__exec(c, rec)) {
            sqlite3_exec(c->db, "ROLLBACK", 0, 0, 0);
            return -1;
        }
        if (lhcat__exec(c, "COMMIT")) return -1;
    }
    return lhcat__exec(c, FTS_SQL);
}

static lhcat *open_db(const char *name, int create, char *err, size_t errn)
{
    if (!valid_name(name)) {
        snprintf(err, errn, "'%s' is not a root's name (letters, digits, '-', '_', '.')", name);
        return NULL;
    }
    char path[PATH_MAX];
    db_path(name, path, sizeof path);
    struct stat st;
    if (!create && stat(path, &st) != 0) { snprintf(err, errn, "no catalogue named '%s' (%s)", name, path); return NULL; }
    lhcat *c = calloc(1, sizeof *c);
    if (!c) { snprintf(err, errn, "out of memory"); return NULL; }
    snprintf(c->name, sizeof c->name, "%s", name);
    lhcat_set_program(c, NULL);
    if (gethostname(c->host, sizeof c->host - 1) != 0) snprintf(c->host, sizeof c->host, "localhost");
    if (sqlite3_open_v2(path, &c->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
        snprintf(err, errn, "%s: %s", path, c->db ? sqlite3_errmsg(c->db) : "cannot open");
        lhcat_close(c);
        return NULL;
    }
    sqlite3_busy_timeout(c->db, 5000);
    if (lhcat__exec(c, "PRAGMA journal_mode = WAL; PRAGMA synchronous = NORMAL; PRAGMA temp_store = MEMORY;"
                       "PRAGMA cache_size = -65536") || migrate(c)) {
        snprintf(err, errn, "%s: %s", path, c->err);
        lhcat_close(c);
        return NULL;
    }
    c->st_child = lhcat__prepare(c, "SELECT id FROM locations WHERE parent = ?1 AND name = ?2");
    c->st_node = lhcat__prepare(c, "SELECT l.parent, l.name, d.path FROM locations l LEFT JOIN dirs d ON d.id = l.parent WHERE l.id = ?1");
    if (!c->st_child || !c->st_node) { snprintf(err, errn, "%s", c->err); lhcat_close(c); return NULL; }
    sqlite3_stmt *q = lhcat__prepare(c, "SELECT path, authority, offline, excludes FROM roots WHERE name = ?1");
    if (q) {
        sqlite3_bind_text(q, 1, name, -1, SQLITE_STATIC);
        if (sqlite3_step(q) == SQLITE_ROW) {
            snprintf(c->root, sizeof c->root, "%s", (const char *)sqlite3_column_text(q, 0));
            c->catalogue_authority = sqlite3_column_text(q, 1) && !strcmp((const char *)sqlite3_column_text(q, 1), "catalogue");
            c->offline = sqlite3_column_int(q, 2);
            if (sqlite3_column_text(q, 3) && *sqlite3_column_text(q, 3)) c->excludes = strdup((const char *)sqlite3_column_text(q, 3));
        }
        sqlite3_finalize(q);
    }
    c->rootlen = strlen(c->root);
    c->root_id = lhcat__child(c, 0, "", 0);
    return c;
}

lhcat *lhcat_open(const char *name, char *err, size_t errn)
{
    char e[256];
    lhcat *c = open_db(name, 0, err ? err : e, err ? errn : sizeof e);
    if (c && (!c->root[0] || !c->root_id)) {
        snprintf(err ? err : e, err ? errn : sizeof e, "the catalogue '%s' has no root", name);
        lhcat_close(c);
        return NULL;
    }
    return c;
}

void lhcat_close(lhcat *c)
{
    if (!c) return;
    sqlite3_finalize(c->st_child);
    sqlite3_finalize(c->st_node);
    sqlite3_finalize(c->st_dir_add);
    sqlite3_finalize(c->st_dir_del);
    lhcat__forget_all(c);
    free(c->pathbuf);
    free(c->excludes);
    if (c->db) sqlite3_close_v2(c->db);
    free(c);
}

static int g_roots_serial;

/* the path (resolved) is the user's home directory ($HOME or the password entry's) */
static int is_home(const char *real)
{
    const char *cand[2] = { getenv("HOME"), NULL };
    struct passwd *pw = getpwuid(getuid());
    if (pw) cand[1] = pw->pw_dir;
    for (int i = 0; i < 2; i++) {
        char r[PATH_MAX];
        if (cand[i] && *cand[i] && realpath(cand[i], r) && !strcmp(r, real)) return 1;
    }
    return 0;
}

const char *lhcat_root_excludes(lhcat *c) { return c->excludes ? c->excludes : ""; }

int lhcat_set_excludes(lhcat *c, const char *list)
{
    /* one glob a line, blank lines and surrounding blanks dropped */
    size_t n = list ? strlen(list) : 0;
    char *norm = malloc(n + 2), *o = norm;
    if (!norm) return lhcat__fail(c, "out of memory");
    for (const char *p = list ? list : ""; *p;) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        const char *a = p, *b = p + len;
        while (a < b && isspace((unsigned char)*a)) a++;
        while (b > a && isspace((unsigned char)b[-1])) b--;
        if (b > a) { memcpy(o, a, (size_t)(b - a)); o += b - a; *o++ = '\n'; }
        p += len + (e ? 1 : 0);
    }
    if (o > norm) o--;   /* no newline after the last */
    *o = 0;
    sqlite3_stmt *st = lhcat__prepare(c, "UPDATE roots SET excludes = ?2 WHERE name = ?1");
    int ok = st != NULL;
    if (ok) {
        sqlite3_bind_text(st, 1, c->name, -1, SQLITE_STATIC);
        if (*norm) sqlite3_bind_text(st, 2, norm, -1, SQLITE_STATIC); else sqlite3_bind_null(st, 2);
        ok = sqlite3_step(st) == SQLITE_DONE;
        if (!ok) lhcat__dbfail(c, "excludes");
    }
    sqlite3_finalize(st);
    if (!ok) { free(norm); return -1; }
    free(c->excludes);
    c->excludes = *norm ? norm : NULL;
    if (!*norm) free(norm);
    return 0;
}

int lhcat_add_root(const char *name, const char *path, const char *authority, int offline, char *err, size_t errn)
{
    if (authority && strcmp(authority, "filesystem") && strcmp(authority, "catalogue")) {
        snprintf(err, errn, "authority '%s': filesystem or catalogue", authority);
        return -1;
    }
    char real[PATH_MAX];
    struct stat st;
    char given[PATH_MAX];
    snprintf(given, sizeof given, "%s", path);
    while (strlen(given) > 1 && given[strlen(given) - 1] == '/') given[strlen(given) - 1] = 0;
    char known[PATH_MAX] = "";
    if (valid_name(name)) {   /* a root's own path as stored is taken as it is: nothing on it is looked at again */
        lhcat *k = lhcat_open(name, NULL, 0);
        if (k) snprintf(known, sizeof known, "%s", k->root);
        lhcat_close(k);
    }
    if (known[0] && !strcmp(known, given)) snprintf(real, sizeof real, "%s", known);
    else if (offline) {
        if (path[0] != '/') { snprintf(err, errn, "%s: an offline root's path is absolute", path); return -1; }
        snprintf(real, sizeof real, "%s", path);
        while (strlen(real) > 1 && real[strlen(real) - 1] == '/') real[strlen(real) - 1] = 0;
    } else {
        if (!realpath(path, real)) { snprintf(err, errn, "%s: %s", path, strerror(errno)); return -1; }
        if (stat(real, &st) != 0 || !S_ISDIR(st.st_mode)) { snprintf(err, errn, "%s: not a directory", path); return -1; }
    }
    lhcat *c = open_db(name, 1, err, errn);
    if (!c) return -1;
    int rc = 0;
    if (c->root[0] && strcmp(c->root, real) && !c->offline) {
        snprintf(err, errn, "the root '%s' is %s already", name, c->root);
        rc = -1;
    } else {
        if (lhcat__exec(c, "BEGIN IMMEDIATE")) rc = -1;
        sqlite3_stmt *q = lhcat__prepare(c, "INSERT INTO roots(name, path, host, authority, added_at, offline, excludes)"
                                            " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7)"
                                            " ON CONFLICT(name) DO UPDATE SET authority = excluded.authority,"
                                            " path = CASE WHEN roots.offline THEN excluded.path ELSE roots.path END,"
                                            " offline = CASE WHEN roots.offline THEN excluded.offline ELSE roots.offline END");
        if (!rc && q) {
            sqlite3_bind_text(q, 1, name, -1, SQLITE_STATIC);
            sqlite3_bind_text(q, 2, real, -1, SQLITE_STATIC);
            sqlite3_bind_text(q, 3, c->host, -1, SQLITE_STATIC);
            sqlite3_bind_text(q, 4, authority ? authority : c->root[0] ? (c->catalogue_authority ? "catalogue" : "filesystem") : "filesystem", -1, SQLITE_STATIC);
            sqlite3_bind_int64(q, 5, lhcat__now_ms() / 1000);
            sqlite3_bind_int(q, 6, offline);
            if (!offline && is_home(real)) sqlite3_bind_text(q, 7, LHCAT_HOME_EXCLUDES, -1, SQLITE_STATIC);
            else sqlite3_bind_null(q, 7);
            if (sqlite3_step(q) != SQLITE_DONE) rc = lhcat__dbfail(c, "add-root");
        } else rc = -1;
        sqlite3_finalize(q);
        if (!rc && !c->root_id && !lhcat__new_location(c, 0, "", 0, 'd', &c->root_id)) rc = -1;
        if (!rc) rc = lhcat__exec(c, "COMMIT");
        else sqlite3_exec(c->db, "ROLLBACK", 0, 0, 0);
        if (rc) snprintf(err, errn, "%s", c->err);
    }
    lhcat_close(c);
    g_roots_serial++;
    return rc;
}

int lhcat_rename_root(const char *old, const char *new_name, char *err, size_t errn)
{
    if (!valid_name(new_name)) { snprintf(err, errn, "'%s' is not a root's name (letters, digits, '-', '_', '.')", new_name); return -1; }
    char from[PATH_MAX], to[PATH_MAX], dir[PATH_MAX];
    db_path(old, from, sizeof from);
    db_path(new_name, to, sizeof to);
    lhcat_dir(dir, sizeof dir);
    struct stat st;
    if (stat(to, &st) == 0) { snprintf(err, errn, "a catalogue named '%s' exists already (%s)", new_name, to); return -1; }
    lhcat *c = lhcat_open(old, err, errn);
    if (!c) return -1;
    /* the only connection: an exclusive lock taken before anything is read (another program with it open refuses it) */
    int rc = lhcat__exec(c, "PRAGMA locking_mode = EXCLUSIVE");
    if (!rc) {
        sqlite3_busy_timeout(c->db, 0);
        if (lhcat__exec(c, "BEGIN EXCLUSIVE")) {
            snprintf(err, errn, "%s is in use by another program (%s)", from, c->err);
            lhcat_close(c);
            return -1;
        }
    }
    char oldthumbs[PATH_MAX + 16], newthumbs[PATH_MAX + 16];
    snprintf(oldthumbs, sizeof oldthumbs, "%s/%s.thumbs/", dir, old);
    snprintf(newthumbs, sizeof newthumbs, "%s/%s.thumbs/", dir, new_name);
    sqlite3_stmt *u = rc ? NULL : lhcat__prepare(c, "UPDATE roots SET name = ?2 WHERE name = ?1");
    sqlite3_stmt *t = rc ? NULL : lhcat__prepare(c, "UPDATE thumbs SET cache_path = ?2 || substr(cache_path, length(?1) + 1)"
                                                    " WHERE substr(cache_path, 1, length(?1)) = ?1");
    if (u && t) {
        sqlite3_bind_text(u, 1, old, -1, SQLITE_STATIC);
        sqlite3_bind_text(u, 2, new_name, -1, SQLITE_STATIC);
        sqlite3_bind_text(t, 1, oldthumbs, -1, SQLITE_STATIC);
        sqlite3_bind_text(t, 2, newthumbs, -1, SQLITE_STATIC);
        if (sqlite3_step(u) != SQLITE_DONE || sqlite3_step(t) != SQLITE_DONE) rc = lhcat__dbfail(c, "rename-root");
    } else rc = -1;
    sqlite3_finalize(u);
    sqlite3_finalize(t);
    if (!rc) rc = lhcat__exec(c, "COMMIT");
    else sqlite3_exec(c->db, "ROLLBACK", NULL, NULL, NULL);
    if (!rc) rc = lhcat__exec(c, "PRAGMA wal_checkpoint(TRUNCATE)");
    if (rc) { snprintf(err, errn, "%s", c->err); lhcat_close(c); return -1; }
    lhcat_close(c);
    /* the database whole in its main file now: it and the thumbnails' directory take the new name */
    char wal[PATH_MAX + 8], shm[PATH_MAX + 8];
    snprintf(wal, sizeof wal, "%s-wal", from);
    snprintf(shm, sizeof shm, "%s-shm", from);
    if (stat(wal, &st) == 0 && st.st_size > 0) { snprintf(err, errn, "%s still has a write-ahead log: not renamed", from); return -1; }
    unlink(wal);
    unlink(shm);
    if (rename(from, to) != 0) { snprintf(err, errn, "%s: %s", to, strerror(errno)); return -1; }
    oldthumbs[strlen(oldthumbs) - 1] = 0;
    newthumbs[strlen(newthumbs) - 1] = 0;
    if (stat(oldthumbs, &st) == 0 && rename(oldthumbs, newthumbs) != 0) {
        snprintf(err, errn, "%s: %s", newthumbs, strerror(errno));
        return -1;
    }
    g_roots_serial++;
    return 0;
}

struct root { char name[LHCAT_NAME_MAX + 1]; char *path; };
static struct root *g_roots;
static int g_nroots = -1, g_loaded_serial = -1;
static struct timespec g_dir_mtime;

static int by_name(const void *a, const void *b) { return strcmp(((const struct root *)a)->name, ((const struct root *)b)->name); }

/* every *.db in the directory with its root's path (read-only: a catalogue being written is read as it was) */
static void load_roots(void)
{
    char dir[PATH_MAX];
    lhcat_dir(dir, sizeof dir);
    struct stat st;
    if (stat(dir, &st) != 0) memset(&st, 0, sizeof st);
    if (g_nroots >= 0 && g_loaded_serial == g_roots_serial && st.st_mtim.tv_sec == g_dir_mtime.tv_sec &&
        st.st_mtim.tv_nsec == g_dir_mtime.tv_nsec)
        return;
    for (int i = 0; i < g_nroots; i++) free(g_roots[i].path);
    free(g_roots);
    g_roots = NULL;
    g_nroots = 0;
    g_dir_mtime = st.st_mtim;
    g_loaded_serial = g_roots_serial;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    int cap = 0;
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (n < 4 || n - 3 > LHCAT_NAME_MAX || strcmp(e->d_name + n - 3, ".db")) continue;
        char name[LHCAT_NAME_MAX + 1], path[PATH_MAX];
        snprintf(name, sizeof name, "%.*s", (int)(n - 3), e->d_name);
        if (!valid_name(name)) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        sqlite3 *db = NULL;
        if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK) {
            sqlite3_busy_timeout(db, 2000);
            sqlite3_stmt *q = NULL;
            if (sqlite3_prepare_v2(db, "SELECT path FROM roots WHERE name = ?1", -1, &q, NULL) == SQLITE_OK) {
                sqlite3_bind_text(q, 1, name, -1, SQLITE_STATIC);
                if (sqlite3_step(q) == SQLITE_ROW && sqlite3_column_text(q, 0)) {
                    if (g_nroots == cap) {
                        cap = cap ? cap * 2 : 8;
                        struct root *r = realloc(g_roots, (size_t)cap * sizeof *r);
                        if (!r) { sqlite3_finalize(q); sqlite3_close(db); break; }
                        g_roots = r;
                    }
                    snprintf(g_roots[g_nroots].name, sizeof g_roots[g_nroots].name, "%s", name);
                    g_roots[g_nroots].path = strdup((const char *)sqlite3_column_text(q, 0));
                    if (g_roots[g_nroots].path) g_nroots++;
                }
                sqlite3_finalize(q);
            }
        }
        sqlite3_close(db);
    }
    closedir(d);
    if (g_nroots > 1) qsort(g_roots, (size_t)g_nroots, sizeof *g_roots, by_name);
}

int lhcat_roots(lhcat_root_fn fn, void *ctx)
{
    load_roots();
    for (int i = 0; i < g_nroots; i++) if (fn) fn(ctx, g_roots[i].name, g_roots[i].path);
    return g_nroots;
}

int lhcat_root_for(const char *path, char *name, size_t n, const char **rel)
{
    if (!path || path[0] != '/') return -1;
    load_roots();
    int best = -1;
    size_t bestlen = 0;
    for (int i = 0; i < g_nroots; i++) {
        const char *r = g_roots[i].path;
        size_t len = strlen(r);
        int under = !strcmp(r, "/") ? 1 : !strncmp(path, r, len) && (path[len] == 0 || path[len] == '/');
        if (under && (best < 0 || len > bestlen)) { best = i; bestlen = len; }
    }
    if (best < 0) return -1;
    if (name) snprintf(name, n, "%s", g_roots[best].name);
    if (rel) {
        const char *p = !strcmp(g_roots[best].path, "/") ? path + 1 : path + bestlen;
        while (*p == '/') p++;
        *rel = p;
    }
    return 0;
}

/* ---- rows by path ---------------------------------------------------------------------------------------------------- */

int64_t lhcat__child(lhcat *c, int64_t parent, const char *name, size_t len)
{
    sqlite3_stmt *st = c->st_child;
    if (!st) {
        sqlite3_stmt *q = NULL;
        if (sqlite3_prepare_v2(c->db, "SELECT id FROM locations WHERE parent = ?1 AND name = ?2", -1, &q, NULL) != SQLITE_OK) return 0;
        sqlite3_bind_int64(q, 1, parent);
        sqlite3_bind_text(q, 2, name, (int)len, SQLITE_STATIC);
        int64_t id = sqlite3_step(q) == SQLITE_ROW ? sqlite3_column_int64(q, 0) : 0;
        sqlite3_finalize(q);
        return id;
    }
    sqlite3_reset(st);
    sqlite3_bind_int64(st, 1, parent);
    sqlite3_bind_text(st, 2, name, (int)len, SQLITE_STATIC);
    int64_t id = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int64(st, 0) : 0;
    sqlite3_reset(st);
    return id;
}

int64_t lhcat_id(lhcat *c, const char *rel)
{
    int64_t id = c->root_id;
    const char *p = rel ? rel : "";
    while (*p && id) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *e = strchr(p, '/');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len == 1 && p[0] == '.') { p += len; continue; }
        id = lhcat__child(c, id, p, len);
        p += len;
    }
    return id;
}

void lhcat__forget(lhcat *c, int64_t id)
{
    unsigned h = (unsigned)((uint64_t)id * 2654435761u) % LHCAT_PATH_CACHE;
    if (c->pcache[h].id == id) { free(c->pcache[h].path); c->pcache[h].path = NULL; c->pcache[h].id = 0; }
}

void lhcat__forget_all(lhcat *c)
{
    for (int i = 0; i < LHCAT_PATH_CACHE; i++) { free(c->pcache[i].path); c->pcache[i].path = NULL; c->pcache[i].id = 0; }
}

/* a directory's path, cached */
static const char *dir_path(lhcat *c, int64_t id, int depth)
{
    if (id == c->root_id) return "";
    unsigned h = (unsigned)((uint64_t)id * 2654435761u) % LHCAT_PATH_CACHE;
    if (c->pcache[h].id == id && c->pcache[h].path) return c->pcache[h].path;
    if (depth > 4096) return NULL;
    sqlite3_stmt *st = c->st_node;
    sqlite3_reset(st);
    sqlite3_bind_int64(st, 1, id);
    if (sqlite3_step(st) != SQLITE_ROW) { sqlite3_reset(st); return NULL; }
    int64_t parent = sqlite3_column_int64(st, 0);
    char *name = strdup((const char *)sqlite3_column_text(st, 1));
    sqlite3_reset(st);
    if (!name) return NULL;
    const char *pp = parent == 0 ? "" : dir_path(c, parent, depth + 1);
    if (!pp) { free(name); return NULL; }
    size_t n = strlen(pp) + strlen(name) + 2;
    char *full = malloc(n);
    if (!full) { free(name); return NULL; }
    if (*pp) snprintf(full, n, "%s/%s", pp, name); else snprintf(full, n, "%s", name);
    free(name);
    free(c->pcache[h].path);
    c->pcache[h].id = id;
    c->pcache[h].path = full;
    return full;
}

const char *lhcat_path(lhcat *c, int64_t id)
{
    if (id == c->root_id) return "";
    sqlite3_stmt *st = c->st_node;
    sqlite3_reset(st);
    sqlite3_bind_int64(st, 1, id);
    if (sqlite3_step(st) != SQLITE_ROW) { sqlite3_reset(st); return NULL; }
    int64_t parent = sqlite3_column_int64(st, 0);
    const char *name = (const char *)sqlite3_column_text(st, 1);
    int nlen = sqlite3_column_bytes(st, 1);
    const char *dp = (const char *)sqlite3_column_text(st, 2);
    if (dp && parent) {   /* the parent's path from dirs */
        size_t dlen = (size_t)sqlite3_column_bytes(st, 2), need = dlen + (size_t)nlen + 2;
        if (need > c->pathcap) {
            char *b = realloc(c->pathbuf, need * 2);
            if (!b) { sqlite3_reset(st); return NULL; }
            c->pathbuf = b;
            c->pathcap = need * 2;
        }
        size_t o = 0;
        if (dlen) { memcpy(c->pathbuf, dp, dlen); c->pathbuf[dlen] = '/'; o = dlen + 1; }
        memcpy(c->pathbuf + o, name ? name : "", (size_t)nlen);
        c->pathbuf[o + (size_t)nlen] = 0;
        sqlite3_reset(st);
        return c->pathbuf;
    }
    char nm[NAME_MAX * 4 + 1];
    snprintf(nm, sizeof nm, "%.*s", nlen, name ? name : "");
    sqlite3_reset(st);
    const char *pp = parent == 0 || parent == c->root_id ? "" : dir_path(c, parent, 0);
    if (!pp) return NULL;
    size_t need = strlen(pp) + strlen(nm) + 2;
    if (need > c->pathcap) {
        char *b = realloc(c->pathbuf, need * 2);
        if (!b) return NULL;
        c->pathbuf = b;
        c->pathcap = need * 2;
    }
    if (*pp) snprintf(c->pathbuf, c->pathcap, "%s/%s", pp, nm); else snprintf(c->pathbuf, c->pathcap, "%s", nm);
    return c->pathbuf;
}

/* ---- attributes on disk ------------------------------------------------------------------------------------------------ */

void lhcat__free_nv(char **nv, int n)
{
    if (!nv) return;
    for (int i = 0; i < 2 * n; i++) free(nv[i]);
    free(nv);
}

/* one attribute's value whatever its length (its size first, again when it grew in between); a trailing NUL or newline
   dropped as flxp drops them. 1 a value, 0 not set, -1 an error */
int lhcat__read_one(const char *abs, const char *name, char **out, int follow)
{
    *out = NULL;
    for (int tries = 0; tries < 8; tries++) {
        ssize_t n = follow ? lhde_xattr_get(abs, name, NULL, 0) : lhde_lxattr_get(abs, name, NULL, 0);
        if (n < 0) return errno == LHDE_ENOATTR || errno == ENOTSUP || errno == ENOENT ? 0 : -1;
        char *v = malloc((size_t)n + 1);
        if (!v) return -1;
        ssize_t k = !n ? 0 : follow ? lhde_xattr_get(abs, name, v, (size_t)n) : lhde_lxattr_get(abs, name, v, (size_t)n);
        if (k < 0) {
            free(v);
            if (errno == ERANGE) continue;
            return errno == LHDE_ENOATTR || errno == ENOTSUP || errno == ENOENT ? 0 : -1;
        }
        while (k > 0 && (v[k - 1] == 0 || v[k - 1] == '\n')) k--;
        v[k] = 0;
        if (k == 0) { free(v); return 0; }
        *out = v;
        return 1;
    }
    errno = ERANGE;
    return -1;
}

int lhcat__read_xattrs(const char *abs, char ***nvp, int *np)
{
    *nvp = NULL;
    *np = 0;
    char *buf = NULL;
    ssize_t n = -1;
    for (int tries = 0; tries < 8; tries++) {
        n = lhde_lxattr_list(abs, NULL, 0);
        if (n <= 0) break;
        free(buf);
        buf = malloc((size_t)n);
        if (!buf) return -1;
        n = lhde_lxattr_list(abs, buf, (size_t)n);
        if (n >= 0 || errno != ERANGE) break;
    }
    if (n < 0) {
        free(buf);
        return errno == ENOTSUP || errno == ENOENT ? 0 : -1;
    }
    char **nv = NULL;
    int count = 0, cap = 0;
    for (ssize_t a = 0; a < n;) {
        const char *nm = buf + a;
        size_t len = strnlen(nm, (size_t)(n - a));
        a += (ssize_t)len + 1;
        if (strncmp(nm, "user.", 5)) continue;
        char key[LHCAT_XATTR_NAME_MAX + 1];
        snprintf(key, sizeof key, "%.*s", (int)len, nm);
        char *v = NULL;
        int r = lhcat__read_one(abs, key, &v, 0);
        if (r < 0) { int e = errno; lhcat__free_nv(nv, count); free(buf); errno = e; return -1; }
        if (r == 0) continue;
        if (count == cap) {
            cap = cap ? cap * 2 : 8;
            char **m = realloc(nv, (size_t)cap * 2 * sizeof *m);
            if (!m) { free(v); lhcat__free_nv(nv, count); free(buf); return -1; }
            nv = m;
        }
        nv[2 * count] = strdup(key);
        nv[2 * count + 1] = v;
        count++;
    }
    free(buf);
    *nvp = nv;
    *np = count;
    return count;
}

/* ---- names and values as flxp keeps them ------------------------------------------------------------------------------- */

const char *lhcat_attr_name(const char *name, char *out, size_t n)
{
    static const struct { const char *s, *x; } SHORT[] = {
        { "desc", "user.lux.desc" }, { "description", "user.lux.desc" }, { "tags", "user.xdg.tags" }, { "tag", "user.xdg.tags" },
        { "comment", "user.xdg.comment" }, { "color", "user.lux.color" }, { "colour", "user.lux.color" },
        { "run", "user.lux.run" }, { "author", "user.lux.author" }, { "source", "user.lux.source" },
        { "build", "user.lux.build" }, { "notes", "user.lux.notes" }, { "todo", "user.lux.todo" },
    };
    if (!name || !*name) return NULL;
    for (size_t i = 0; i < sizeof SHORT / sizeof SHORT[0]; i++)
        if (!strcmp(name, SHORT[i].s)) { snprintf(out, n, "%s", SHORT[i].x); return out; }
    if (!strncmp(name, "user.", 5)) snprintf(out, n, "%s", name);
    else snprintf(out, n, "user.%s", name);
    return out;
}

static const char *const PALETTE[] = { "red", "orange", "yellow", "green", "teal", "blue", "purple", "pink", "brown", "gray" };

static char *trimmed(const char *s, size_t len, const char *blanks)
{
    size_t a = 0, b = len;
    while (a < b && strchr(blanks, s[a])) a++;
    while (b > a && strchr(blanks, s[b - 1])) b--;
    char *out = malloc(b - a + 1);
    if (!out) return NULL;
    memcpy(out, s + a, b - a);
    out[b - a] = 0;
    return out;
}

char *lhcat_normalize(const char *xattr, const char *text)
{
    if (!text) text = "";
    size_t n = strlen(text);
    if (xattr && !strcmp(xattr, "user.xdg.tags")) {
        char *out = malloc(n + 1);
        if (!out) return NULL;
        size_t o = 0;
        out[0] = 0;
        for (size_t a = 0; a <= n;) {
            size_t b = a;
            while (b < n && text[b] != ',' && text[b] != ';' && text[b] != '\n') b++;
            char *t = trimmed(text + a, b - a, " \t\r");
            if (t && *t) {
                int dup = 0;   /* an item already there (case-sensitive) */
                for (size_t p = 0; p < o && !dup;) {
                    size_t q = p;
                    while (q < o && out[q] != ',') q++;
                    if (q - p == strlen(t) && !memcmp(out + p, t, q - p)) dup = 1;
                    p = q + 1;
                }
                if (!dup) {
                    if (o) out[o++] = ',';
                    memcpy(out + o, t, strlen(t));
                    o += strlen(t);
                    out[o] = 0;
                }
            }
            free(t);
            a = b + 1;
        }
        return out;
    }
    int one_line = xattr && (!strcmp(xattr, "user.lux.desc") || !strcmp(xattr, "user.lux.color") || !strcmp(xattr, "user.lux.run"));
    char *v = calloc(1, n + 1);
    if (!v) return NULL;
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        char ch = text[i];
        if (one_line) {   /* each run of line breaks a space */
            if (ch != '\r' && ch != '\n') v[o++] = ch;
            else if (o && v[o - 1] != ' ') v[o++] = ' ';
        } else if (ch == '\r') {   /* CR LF and a lone CR as LF */
            v[o++] = '\n';
            if (i + 1 < n && text[i + 1] == '\n') i++;
        } else v[o++] = ch;
    }
    char *out = trimmed(v, o, " \t\r\n");
    free(v);
    if (out && xattr && !strcmp(xattr, "user.lux.color"))
        for (size_t i = 0; i < sizeof PALETTE / sizeof PALETTE[0]; i++)
            if (!strcasecmp(out, PALETTE[i])) { strcpy(out, PALETTE[i]); break; }
    return out;
}

