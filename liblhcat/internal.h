/* liblhcat's own: what its files share (lhcat__ names). */
#ifndef LHCAT_INTERNAL_H
#define LHCAT_INTERNAL_H

#include "lhcat.h"
#include <limits.h>
#include <sqlite3.h>
#include <sys/stat.h>

#define LHCAT_PATH_CACHE 4096
#define LHCAT_XATTR_NAME_MAX 255

struct lhcat {
    sqlite3 *db;
    char name[LHCAT_NAME_MAX + 1];
    char root[PATH_MAX];
    size_t rootlen;
    int catalogue_authority;
    int offline;                        /* the root's path is not reachable from here (a listing only) */
    int64_t root_id;                    /* the row of the root directory itself (parent 0, name "") */
    char err[1024];
    char program[64];
    char host[256];
    sqlite3_stmt *st_child;             /* SELECT id FROM files WHERE parent = ? AND name = ? */
    sqlite3_stmt *st_node;              /* SELECT parent, name FROM files WHERE id = ? */
    struct { int64_t id; char *path; } pcache[LHCAT_PATH_CACHE];   /* directories' paths by id */
    char *pathbuf;
    size_t pathcap;
};

/* the error into c->err (printf-style); -1 */
int lhcat__fail(lhcat *c, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* sqlite's error after a failed call; -1 */
int lhcat__dbfail(lhcat *c, const char *what);
/* run SQL without results; -1 on an error */
int lhcat__exec(lhcat *c, const char *sql);
sqlite3_stmt *lhcat__prepare(lhcat *c, const char *sql);
int64_t lhcat__now_ms(void);
/* the absolute path of rel into out */
const char *lhcat__abs(lhcat *c, const char *rel, char *out, size_t n);
/* the child row of parent called name (0: none) */
int64_t lhcat__child(lhcat *c, int64_t parent, const char *name, size_t len);
/* the path cache forgets id (a row moved or went) / everything */
void lhcat__forget(lhcat *c, int64_t id);
void lhcat__forget_all(lhcat *c);

/* a new object and its location (parent, name, kind) with no facts yet; *id the location. 1 on success */
int lhcat__new_location(lhcat *c, int64_t parent, const char *name, size_t len, char kind, int64_t *id);
/* one attribute of the file (follow: through a symbolic link); 1 a value (malloc'ed into *out), 0 not set, -1 an error */
int lhcat__read_one(const char *abs, const char *name, char **out, int follow);
/* every user.* attribute of the file at abs (not following a symbolic link): n pairs, malloc'ed (lhcat__free_nv);
   0 none, -1 a read error (errno) */
int lhcat__read_xattrs(const char *abs, char ***nv, int *n);
void lhcat__free_nv(char **nv, int n);
/* the object's attributes as observed on the file now (n name/value pairs, the whole list): observed values updated,
   desired following them as the root's authority says, a pending write meeting a change a conflict. The count of
   conflicts it made, -1 on an error */
int lhcat__observe(lhcat *c, int64_t object, const char *rel, char **nv, int n);
/* the tags table and the text index of an object from its desired values */
int lhcat__derived(lhcat *c, int64_t object);
/* the locations in the temporary table lhcat_gone marked gone */
int lhcat__mark_gone(lhcat *c, uint64_t *count);

/* the shared part of index and touch: the entry name under parent (rel its path, abs its absolute path, st its lstat)
   written or refreshed; *id its row. force: read it again even when unchanged */
struct lhcat__walk;
int lhcat__entry(lhcat *c, struct lhcat__walk *w, int64_t parent, const char *name, const char *rel, const char *abs,
                 const struct stat *st, int force, int64_t *id);

#endif
