/* liblhcat: the catalogue of a tree of files and their metadata (docs/CATALOGUE.md). One SQLite database per root, a
 * directory the user names (`lhde-cat add-root NAME PATH`), under ${XDG_STATE_HOME:-~/.local/state}/lhde/catalogue/NAME.db.
 *
 * The catalogue keeps objects (its own ids) and their locations (a path under the root with the facts last observed
 * there: stat, type, coverage); a file-system fact is evidence, never identity: a path or inode seen again with other
 * facts is a new object, with a relation to the old one for the user to confirm. Each attribute has three values: what
 * was last observed on the file, what the catalogue wants (desired, with a revision) and whether a write is pending or in
 * conflict. Edits are operations in a journal (an op id, a base revision, set / unset / tag_add / tag_remove); the root's
 * authority (filesystem or catalogue) decides what a scan does when the file and the catalogue differ.
 *
 * Paths given to and returned by these calls are relative to the root ("a/b.jpg"; "" the root itself), bytes as the file
 * system has them (not checked for UTF-8). Calls return 0 on success and -1 on an error with the reason in lhcat_error().
 * A handle is used by one thread at a time. Plain C; C++ includes it as it is. */
#ifndef LHCAT_H
#define LHCAT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lhcat lhcat;
typedef struct sqlite3 sqlite3;

#define LHCAT_SCHEMA 4          /* the schema version a new database gets; older ones are migrated on open */
#define LHCAT_NAME_MAX 64       /* a root's name: letters, digits, '-', '_', '.', not starting with '.' */

/* what a location's facts cover (locations.coverage) */
enum {
    LHCAT_LISTED = 1,           /* in a du listing */
    LHCAT_STATTED = 2,          /* stat'ed */
    LHCAT_TYPED = 4,            /* a type known (by name, or by content with sniff) */
    LHCAT_ATTRS = 8,            /* its user.* attributes read */
    LHCAT_EXIF = 16,            /* a picture's facts read */
    LHCAT_THUMB = 32,           /* a thumbnail made (or found impossible) */
    LHCAT_HASHED = 64,          /* hashed (prefix and full) */
};

/* ---- roots ---------------------------------------------------------------------------------------------------------- */
const char *lhcat_dir(char *out, size_t n);     /* the databases' directory (created when missing) */
/* A new root NAME over the directory PATH (made absolute, symlinks resolved). authority "filesystem" (NULL: the default)
   or "catalogue". A root of that name over another path is an error, over the same path only its authority changes.
   offline: PATH (absolute) is not reachable from here, as for a listing of another machine's tree: it is kept as given,
   only import-du fills the catalogue, and an offline root can be given another path later. */
int lhcat_add_root(const char *name, const char *path, const char *authority, int offline, char *err, size_t errn);
int lhcat_offline(lhcat *c);                   /* the root was added offline */

/* Exclude globs, one a line, matched against a path relative to the root (gitignore-like, but always anchored at the
   root): '*' and '?' within one name, "[...]" a set, "**" any number of whole names ("**" + "/node_modules/" at any
   depth), a trailing '/' only directories; what one matches is not walked (an excluded location). A new root over the
   user's home directory gets LHCAT_HOME_EXCLUDES. */
#define LHCAT_HOME_EXCLUDES ".cache/\n.local/share/Trash/\n.thumbnails/\n**/node_modules/"
const char *lhcat_root_excludes(lhcat *c);             /* the root's list ("" none) */
const char *lhcat_root_machine(lhcat *c);              /* the machine the root is on ("" not said) */
int lhcat_set_machine(lhcat *c, const char *machine);  /* NULL or "": not said */
int lhcat_set_excludes(lhcat *c, const char *list);    /* replace it (blank lines dropped) */
/* the first glob of list that matches rel (is_dir: rel is a directory), NULL none; the glob is a copy in out */
const char *lhcat_exclude_match(const char *list, const char *rel, int is_dir, char *out, size_t n);
/* the mount points strictly below the absolute directory dir, from the system's mount table (nothing under them is
   looked at): the count, -1 when the table cannot be read */
typedef void (*lhcat_mount_fn)(void *ctx, const char *path, const char *fstype);
int lhcat_mounts_below(const char *dir, lhcat_mount_fn fn, void *ctx);
/* every root: its name, path and machine ("" not said), by name; the count, -1 on an error */
typedef void (*lhcat_root_fn)(void *ctx, const char *name, const char *path, const char *machine);
int lhcat_roots(lhcat_root_fn fn, void *ctx);
/* the root whose path holds the absolute path PATH (the longest such): its name; *rel (when not null) points into PATH
   after the root's path. 0 when found, -1 none. The roots are read again when the directory changed. */
int lhcat_root_for(const char *path, char *name, size_t n, const char **rel);
/* The root OLD named NEW: its row, its database file (checkpointed whole into it first) and its thumbnails' directory
   with the paths recorded to it. Refused when NEW exists or another program has the catalogue open. */
int lhcat_rename_root(const char *old, const char *new_name, char *err, size_t errn);

/* ---- machines ---------------------------------------------------------------------------------------------------------- */
/* The user's machines: ${XDG_CONFIG_HOME:-~/.config}/lhde/machines.ini, a section each (docs/SETTINGS.md "machines.ini"):
   host (how ssh reaches it; NULL: this machine), os (freebsd, linux), note. Each machine in file order to fn; the count
   (0 without the file), -1 when it cannot be read. A root may be bound to one by its section's name. */
struct lhcat_machine { const char *name, *host, *os, *note; };
typedef void (*lhcat_machine_fn)(void *ctx, const struct lhcat_machine *m);
int lhcat_machines(lhcat_machine_fn fn, void *ctx);
const char *lhcat_machines_file(char *out, size_t n);

/* ---- a catalogue ----------------------------------------------------------------------------------------------------- */
lhcat *lhcat_open(const char *name, char *err, size_t errn);
void lhcat_close(lhcat *c);
const char *lhcat_error(lhcat *c);
const char *lhcat_name(lhcat *c);
const char *lhcat_root_path(lhcat *c);
int lhcat_catalogue_authority(lhcat *c);       /* 1: the catalogue is authoritative, 0: the file system */
sqlite3 *lhcat_db(lhcat *c);
void lhcat_set_program(lhcat *c, const char *program);   /* the journal's program (default: the process's name) */

/* a location's id by its path (0: none; gone ones too), its path by its id (valid until the next lhcat_path) */
int64_t lhcat_id(lhcat *c, const char *rel);
const char *lhcat_path(lhcat *c, int64_t id);
int64_t lhcat_object(lhcat *c, int64_t location);       /* the object at a location, 0 none */

/* ---- filling it -------------------------------------------------------------------------------------------------------- */
struct lhcat_progress {
    uint64_t lines, rows, dirs;         /* import-du: lines read, rows written, directories among them */
    uint64_t files, changed, gone, related, pictures, thumbs, attrs, hashed, conflicts;   /* index */
    uint64_t bytes_read;                /* index: bytes read from file contents (EXIF, sniffing, hashes, thumbnails) */
    uint64_t skipped_budget;            /* index: entries whose content reading the budget left for a later run */
    uint64_t unreadable;                /* index: directories that could not be listed (nothing under them is gone) */
    uint64_t excluded;                  /* index: entries an exclude glob kept out (not walked) */
    uint64_t mounts;                    /* index: mount points not entered (another file system) */
    int out_of_time;                    /* index: stopped by its time limit (it resumes) */
    int64_t scan;                       /* index: the scan's id */
    int resumed, interrupted;           /* index: went on from an interrupted scan / stopped early (it resumes) */
    const char *current;                /* the path being looked at (during the callback) */
};
typedef void (*lhcat_progress_fn)(void *ctx, const struct lhcat_progress *p);

/* A du-style listing ("SIZE<TAB>./PATH" a line, du -a's output; SIZE in units of block bytes, du's 1024 by default) into
   the catalogue as observations "listed": usage_kib (disk usage, not the file's length; a directory's line is its total,
   kept on its row but never summed), the type by name; directories are the parents of other lines. A listed-only location
   the listing no longer has is marked gone. One transaction per 10000 lines: what is in so far can be queried at once. */
int lhcat_import_du(lhcat *c, FILE *in, uint64_t block, lhcat_progress_fn fn, void *ctx, struct lhcat_progress *out);

struct lhcat_index_opts {
    int thumbs;                         /* 0, or 128 / 256: the freedesktop thumbnail of every picture (flxp-thumb) */
    int self_contained;                 /* thumbnails copied under the catalogue's own directory */
    int sniff;                          /* a file whose name gives no type: its first bytes for a picture's */
    int hash;                           /* hash the files (sha256 of the first 64 KiB and of the whole) */
    uint64_t budget;                    /* bytes of file contents to read at most (0: no limit); stat and attributes are free */
    int restart;                        /* start a new scan instead of resuming an unfinished one */
    const char *thumb_helper;           /* the flxp-thumb program (NULL: beside this program, else on PATH) */
    int cross_fs;                       /* enter other file systems' mount points (by default one is recorded as a
                                           location of kind 'm', not entered: from the mount table before it is
                                           stat'ed, else by its device) */
    const char *exclude;                /* more exclude globs for this run (one a line), besides the root's */
    int64_t time_limit_ms;              /* stop after this long (0: no limit); the next index resumes */
};
/* Scan SUBPATH ("" the whole root), one entry at a time, recording observations: an entry whose size, mtime, ctime and
   inode are the location's is not read again; a new or changed one gets its type, its attributes observed (see
   lhcat_apply for what a difference does) and a picture its facts. A path whose inode changed is a new object; a new path
   with the inode of a location whose path is gone is a new object related to the old one ("moved"). A location is marked
   gone only when its directory was listed whole in a scan that completed; an interrupted or failing scan marks nothing,
   commits every few seconds and is resumed by the next index of the same SUBPATH. */
int lhcat_index(lhcat *c, const char *subpath, const struct lhcat_index_opts *o, lhcat_progress_fn fn, void *ctx,
                struct lhcat_progress *out);
void lhcat_interrupt(void);             /* stop a running lhcat_index at the next entry (async-signal-safe) */
/* What an index of SUBPATH with these options would walk, without writing anything: each entry directly under SUBPATH
   with the counts under it (the same excludes and mount rules; names and kinds from the directory listings, a directory
   stat'ed only to compare its device). In name order. */
struct lhcat_plan {
    const char *name;
    int kind;                           /* 'f', 'd', 'l', 'o' */
    int what;                           /* 0 walked, 1 excluded (by glob), 2 a mount point not entered */
    const char *glob;
    uint64_t entries, dirs;             /* under it, itself included */
    uint64_t excluded, mounts, unreadable;
};
typedef void (*lhcat_plan_fn)(void *ctx, const struct lhcat_plan *e);
int lhcat_index_plan(lhcat *c, const char *subpath, const struct lhcat_index_opts *o, lhcat_plan_fn fn, void *ctx);
/* The entry rel observed again whatever its stat says (what a program calls after writing to it); its parents get
   locations when they have none; a path that is not there any more is marked gone (and what is under it). 1 gone,
   0 observed, -1 an error. */
int lhcat_touch(lhcat *c, const char *rel);
/* hash the entry now (on demand; the walk only with opts.hash) */
int lhcat_hash(lhcat *c, const char *rel);

/* ---- attributes ---------------------------------------------------------------------------------------------------------- */
/* The name on disk for a short name: desc, tags, comment, color, run (docs/ANNOTATIONS.md), author, source, build,
   notes, todo; another name gets "user." unless it has it. NULL for an empty name. */
const char *lhcat_attr_name(const char *name, char *out, size_t n);
/* The value as flxp stores it (shell::details::write): tags split at commas, semicolons and line breaks, trimmed, without
   duplicates, joined by commas; a description, colour and run command one line; any other its lines (CR LF as LF); a
   colour of the palette its lower-case name; trimmed. malloc'ed. */
char *lhcat_normalize(const char *xattr, const char *text);

enum { LHCAT_SET = 1, LHCAT_UNSET, LHCAT_TAG_ADD, LHCAT_TAG_REMOVE };
struct lhcat_op {
    const char *op_id;                  /* NULL: a new one (host:random); the same id twice is the same operation */
    int op;                             /* LHCAT_SET ... */
    const char *attr;                   /* the name on disk (tag ops: user.xdg.tags) */
    const char *value;                  /* the value, or the tag */
    int64_t base_rev;                   /* the attribute's revision the op was made against; -1: the current one */
};
/* What an apply did */
enum { LHCAT_APPLIED = 1,               /* desired and the file agree (written, or nothing to write) */
       LHCAT_PENDING,                   /* desired changed, the write is pending (deferred, or it failed) */
       LHCAT_CONFLICT,                  /* rejected: its base revision is not the attribute's (set/unset), or the file
                                           changed under a pending write */
       LHCAT_DUPLICATE };               /* its op id was seen before; *status the first one's outcome is ORed in << 4 */
/* Apply an operation to rel's attribute: the journal row is committed first (durable), then, with write, the value is
   written to the file when the file still holds what was last observed (else conflict, nothing written) and read back.
   Tag ops change the desired set whatever its revision (two additions merge); set and unset with a base revision other
   than the attribute's are a conflict. *status one of the above; *op_id_out (64 bytes) the op's id. */
int lhcat_apply(lhcat *c, const char *rel, const struct lhcat_op *op, int write, int *status, char *op_id_out);
/* write the pending values out (rel NULL: every one in the catalogue): each as lhcat_apply's write; counts into *written
   and *conflicts */
int lhcat_commit(lhcat *c, const char *rel, uint64_t *written, uint64_t *conflicts);
/* a conflict settled: take the file's value (desired = observed) or the catalogue's (written at the next commit, over the
   value now observed) */
int lhcat_resolve(lhcat *c, const char *rel, const char *xattr, int take_catalogue);
/* What another writer did (flxp's details::write): it wrote xattr = value (NULL or "" removed) on rel, replacing old: a
   set/unset op, applied, in the journal; the location observed again. */
int lhcat_record_write(lhcat *c, const char *rel, const char *xattr, const char *old, const char *value);

/* The present locations directly under the directory rel, for a folder's listing: their facts and desired attribute
   values. -1 when rel has no location. */
struct lhcat_entry {
    int64_t id;
    const char *name;
    int kind;                           /* 'f' file, 'd' directory, 'l' symbolic link, 'o' other */
    int64_t size, mtime, ctime_ns;      /* -1: not observed (a listed-only location) */
    int coverage;
    int nattrs;
    const char *const *attrs;           /* name0, value0, name1, value1, ... */
};
typedef void (*lhcat_entry_fn)(void *ctx, const struct lhcat_entry *e);
int lhcat_list_dir(lhcat *c, const char *rel, lhcat_entry_fn fn, void *ctx);

/* ---- queries ------------------------------------------------------------------------------------------------------------- */
struct lhcat_query {
    const char *tag;                    /* a tag, whole (case ignored) */
    const char *no_tag;                 /* not this tag: only locations whose attributes were read, unless include_unknown */
    int include_unknown;
    const char *desc;                   /* a part of the description (case ignored) */
    const char *text;                   /* words over the description, comment and tags (prefix match) and the name (part) */
    const char *type;                   /* a MIME type, "image/" followed by a star: a whole group */
    const char *since, *until;          /* "YYYY", "YYYY-MM", "YYYY-MM-DD" (a time may follow): the picture's date taken,
                                           else the mtime; until takes in its whole year, month or day */
    const char *color;                  /* a colour name */
    const char *in;                     /* below this directory */
    int kind;                           /* 0 any, 'f', 'd' */
    int limit;                          /* 0: no limit */
};
struct lhcat_hit {
    int64_t id, object;
    const char *path;
    int kind, coverage;
    int64_t size, usage_kib, mtime;     /* -1 when not known */
    const char *type, *taken_at;        /* NULL when not known */
};
typedef void (*lhcat_hit_fn)(void *ctx, const struct lhcat_hit *h);
/* the present locations matching every condition, in the order they were catalogued; the count (-1 on an error).
   *covered (when not null): of the locations the query's other conditions allow, how many had their attributes read */
int64_t lhcat_find(lhcat *c, const struct lhcat_query *q, lhcat_hit_fn fn, void *ctx, int64_t *scope, int64_t *covered);

/* ---- what the catalogue reads from files -------------------------------------------------------------------------------- */
/* the MIME type of a file name from the freedesktop globs ("*.ext" patterns of globs2 in $XDG_DATA_HOME and
   $XDG_DATA_DIRS), NULL when none */
const char *lhcat_type_for_name(const char *name);
/* a picture type by its first bytes (JPEG, PNG, GIF, WebP, TIFF, BMP, HEIF/AVIF), NULL when none of those */
const char *lhcat_sniff(const unsigned char *buf, size_t n);
struct lhcat_picture {
    int width, height;                  /* as stored (orientation 5-8 shows them turned); 0 unknown */
    int orientation;                    /* EXIF Orientation 1..8, 0 not given */
    char taken_at[20];                  /* "YYYY-MM-DD HH:MM:SS" from DateTimeOriginal (else DateTime); "" none */
    char camera[96];                    /* "Make Model" (Model alone when it starts with Make); "" none */
};
/* The facts of the picture in the open file fd of that type: a JPEG's EXIF APP1 (IFD0 and the Exif IFD) and its frame's
   size, PNG's IHDR, GIF's screen, WebP's VP8/VP8L/VP8X header, BMP's info header, TIFF's IFD0. Headers only (a JPEG's
   markers up to its frame); the bytes read are added to *bytes. 1 something found, 0 nothing, -1 a read error. */
int lhcat_picture_read(int fd, const char *type, struct lhcat_picture *p, uint64_t *bytes);
int lhcat_picture_parse(const unsigned char *buf, size_t n, const char *type, struct lhcat_picture *p);
/* sha256 of the open file: of its first 64 KiB and of the whole (hex, 65 bytes each); bytes read added to *bytes */
int lhcat_sha256_file(int fd, char *prefix64k, char *full, uint64_t *bytes);

#ifdef __cplusplus
}
#endif
#endif
