# The catalogue (liblhcat, lhde-cat)

A catalogue is a SQLite database of one tree of files, a **root**: every path under it with what was last observed there
(the stat, the type, the user.* attributes, a picture's EXIF facts, the freedesktop thumbnail), and the journal of every
attribute edit made through it. It is built quickly (a `du` listing gives millions of rows in a minute), queried at once
(`lhde-cat find`, flxp's Filter box), and it is where quick local edits can be made and later written back to the files
(the authority setting). The plan it comes from is `todo_ideas/CATALOGUE-REMOTE-PHOTOS.md`.

`liblhcat/` is a C library next to liblhde (`lhcat.h`, `lhcat_*`; it uses liblhde for the state directory, compat.h for the
attribute calls), linked into flxp's shell layer and into `lhde-cat`, the command line. Python tools read the same schema
with the `sqlite3` module.

## Where it is

One database per root: `${XDG_STATE_HOME:-~/.local/state}/lhde/catalogue/NAME.db` (with SQLite's `-wal` and `-shm` beside
it while open), NAME the stable name the user gives the root (letters, digits, `-`, `_`, `.`). `NAME.thumbs/` beside it
holds the thumbnails of an index run with `--self-contained`. The databases use WAL: a query, flxp's Filter box or a second
`lhde-cat` reads while an import or an index writes.

## The model: objects, locations, observations

- **Objects** (`objects`) are the catalogue's own identities. Everything the file system says is evidence about them, never
  their identity.
- **Locations** (`locations`) are paths under the root, each the location of one object, with the facts last observed
  there: kind (`f`, `d`, `l`, `o`; `m` a mount point the walk did not enter), `size` (the length, from stat), `usage_kib`
  (the disk usage, from a du listing), mtime, ctime (nanoseconds), dev, ino, `type` (MIME), `observed_at`, `state`
  (`present`, `gone`, or `excluded`: not walked, see "What a walk enters") and `coverage`. The path is the
  chain of parent locations' names (`parent`, `name`; the root's own location has parent 0 and the name ""), which keeps a
  7-million-row listing to about 180 bytes a row.
- **Coverage** is a bit mask of what a location's facts are based on: 1 listed (a du listing), 2 statted, 4 typed, 8 its
  attributes read, 16 a picture's facts read, 32 a thumbnail made (or found impossible), 64 hashed. `find` says how many of
  the locations a query could look at had their attributes read; a negative query (`--no-tag`) leaves out the ones whose
  attributes were never read unless `--include-unknown`, since unknown is not "without".
- **Identity evidence.** A path whose inode or kind changed is a new object, with a `relations` row (`replaced`, confidence
  0.5) to the old one; a new path whose inode is that of a location whose path is gone is a new object related to the old one
  (`moved`, 0.9 when size and mtime agree, 0.6 when not); two objects with the same full hash are related as `duplicate`
  (1.0). Relations are for the user to confirm (`confirmed` stays NULL until then; `lhde-cat relations NAME`); nothing is
  merged on its own. The attributes travel with the file anyway, so a moved file's new object observes them at once.
- **Hashes** (`hashes`: sha256 of the first 64 KiB and of the whole, with the content version size:mtime they were taken of)
  are computed on demand only: `lhde-cat hash PATH`, or `index --hash`. The walk does not read whole files otherwise.
- **Absence.** An index records observations only. A location becomes `gone` when a scan that completed listed its
  directory whole and the location was not in it (and so does what was under a gone directory). An interrupted scan, a
  killed one, or a directory that could not be listed marks nothing gone. Gone locations are kept (their history with them).
- **Scans** (`scans`: subpath, started, finished, status `running`, `interrupted`, `complete`, `failed`, `abandoned`, the
  cursor): an index commits every 2 seconds with its cursor (the last entry done, in the walk's order: names sorted
  bytewise, a directory's entries right after it), and the next `index` of the same subpath goes on from there with the
  scan's start time; `--restart` abandons it.

## Attributes: observed, desired, pending

Each attribute of an object (`attrs`) has three values:

| column | what |
| --- | --- |
| `observed` | the file's value when last read (NULL: not set), with `observed_at` |
| `desired` | the catalogue's value, at revision `rev` (each change of desired is one more) |
| `pending` | `none`; `write`: desired is not on the file yet; `conflict`: the file changed under a pending write, or a write found a value it had not observed |

An index or touch updates `observed` only. What happens when the observed value changes depends on the root's
**authority** (`lhde-cat add-root NAME PATH --authority filesystem|catalogue`; filesystem by default):

- **filesystem**: the file is the truth. A changed observed value becomes the desired one (rev + 1) with a journal row
  `observed` saying what it replaced. This is the catalogue as a cache: losing it loses nothing.
- **catalogue**: the catalogue is the truth. A changed observed value leaves desired as it is and marks the attribute
  `pending = write`: `lhde-cat diff NAME` shows the difference and `lhde-cat commit NAME` writes desired out. This is the
  scenario of a photo library cached locally: tag and describe in the catalogue, commit to the (remote) files later.

Under either authority a scan never erases a pending edit: an observed change under `pending = write` makes it a
`conflict`. A write never erases a change it has not observed: before writing, the file's value is read and compared with
`observed` (expected-previous-value); when they differ nothing is written, the new value becomes `observed` and the
attribute a `conflict` (`lhde-cat conflicts NAME`). After a write the value is read back; only when it equals desired does
`pending` return to `none`. `lhde-cat resolve PATH ATTR file` takes the file's value (desired = observed; the operations in
conflict are `rejected`), `resolve PATH ATTR catalogue` keeps the catalogue's (written by the next commit, over the value now
observed).

The `tags` table (one row per tag of the desired list, case-blind) and the FTS5 index `text` (desired description, comment
and tags; rowid the object) follow the desired values, so a query sees the catalogue's view, pending edits included.

## Edits are operations: the journal

Every edit through the library is an operation in `journal`: `op_id` (unique; `host:random` unless the caller gives one),
`object_id`, `path`, `attr`, `base_rev` (the attribute's revision it was made against), `op` (`set`, `unset`, `tag_add`,
`tag_remove`; and `observed`, `resolve` from the catalogue itself), `value`, `old`, `at` (ms), `host`, `program`, `status`
(`applied`: on the file; `pending`: in desired, not written yet; `conflict`; `rejected`).

- An operation whose `op_id` is in the journal already is that operation again: a no-op returning the first one's outcome;
  if that outcome was `pending`, a retry with writing performs the write (a lost acknowledgement costs nothing).
- `set` and `unset` with a `base_rev` that is not the attribute's revision are a `conflict` (recorded, not applied).
- `tag_add` and `tag_remove` change the desired set whatever its revision, so two additions made against the same revision
  (two machines offline) both apply: the set merges.
- The journal row is committed before the file is written, so an operation is durable even when the write never happens.
- Timestamps are history; revisions and op ids decide. This is the ground for syncing catalogues later: replaying another
  catalogue's journal is applying its operations by id.

Values are normalized as flxp stores them (`lhcat_normalize`, the same rules as `shell::details::write`, docs/ANNOTATIONS.md):
tags split at commas, semicolons and line breaks, trimmed, without duplicates, joined by commas; a description, colour and
run command one line; anything else its lines; a colour of the palette its lower-case name; trimmed.

## Pictures and thumbnails

`picture` (per object): `taken_at` ("YYYY-MM-DD HH:MM:SS" from DateTimeOriginal, else DateTime), width and height (a JPEG's
frame, else the Exif IFD's pixel sizes; as stored, orientation 5 to 8 shows them turned), `orientation`, `camera` ("Make
Model"). liblhcat reads them itself from the headers: a JPEG's markers up to its frame (the APP1 segment's TIFF structure:
IFD0 and the Exif IFD), a TIFF's IFD0, PNG's IHDR, GIF's screen, WebP's VP8/VP8L/VP8X header, BMP's info header; nothing
past the headers is read. The type comes from the freedesktop globs (`*.ext` patterns of the `globs2` files) and, with
`--sniff`, from the first 64 bytes of a file whose name gives none.

`thumbs` (per object and variant 128 or 256): the content version it shows (size:mtime), the file's URI (the freedesktop
cache's key) and the PNG's path. `index --thumbs 128|256` makes them with **`flxp-thumb`**, a small native program of the
shell layer (`flxp/thumb/main.cpp`: `shell::thumbs::get`, so the same decoders, thumbnailer entries and cache as flxp)
started once per index as a coprocess (a path a line in, `made|cached PATH` or `fail` a line out). The PNGs live in the
freedesktop cache (`~/.cache/thumbnails`), or with `--self-contained` are also copied to `catalogue/NAME.thumbs/` so the
catalogue can be copied whole. A missing thumbnail invalidates nothing.

## Filling it

All three are incremental and can run again at any time.

1. **`lhde-cat import-du NAME FILE`** reads a `du -a` listing (`SIZE<TAB>./PATH` a line, SIZE in `--block` bytes, du's
   1024 by default; `-` for stdin): every line a location observed as "listed", with its disk usage in `usage_kib` and its
   type by name, directories made as the parents of other lines. A directory's own line is its total: it is kept on the
   directory's location and never summed; the root's (the `.` line) is the root's total. A location an index has stat'ed
   keeps its facts; a listed-only location the listing no longer has is marked gone. Paths are kept as the bytes they are.
   It commits every 10,000 lines, so `find` works on what is in while it runs.
2. **`lhde-cat index NAME [SUBPATH]`** walks the root, one entry at a time, on its own file system and outside the excluded
   paths (see "What a walk enters"): an entry whose size, mtime, ctime and inode are the location's is not read again (only its observed time;
   the ctime is what changes when an attribute changes). A new or changed entry gets its type, its attributes observed and,
   for a picture, its facts; `--hash` and `--thumbs` add those. `--budget MB` bounds the bytes of file contents read (EXIF,
   sniffing, hashes, thumbnails; stat and attributes are free): what the budget leaves out is done by a later run.
   `--time MIN` stops after MIN minutes of wall clock (exit status 124) and the next index resumes. A progress line on a
   terminal; Ctrl+C stops at the next entry (exit status 130), and the next index resumes.
3. **`lhde-cat touch PATH...`** observes the given files again whatever their stat says (what a program calls after its own
   writes; flxp does it through the library), and marks a path that is not there any more gone.

### What a walk enters

A root may be a home directory, with other machines' file systems mounted in it and caches nobody wants catalogued, so an
index decides for each entry before it looks at it:

- **One file system** (`--one-fs`, the default): a mount point below the root is not entered. The mount points are read
  from the system's mount table (`/proc/self/mountinfo`, FreeBSD's `getmntinfo`) before the walk, so one is recognised by
  its path and never stat'ed (a stat of an NFS mount's root already asks the server); a directory whose device is not the
  root's is one too. A mount point is recorded as a location of kind `m`, coverage 0 (not entered), and nothing under it.
  `--cross-fs` walks into them as into any directory.
- **Excludes**: globs matched against the path relative to the root, the root's list (`roots.excludes`) and the run's
  (`index --exclude GLOB`, repeatable). Like `.gitignore` but always anchored at the root: `n/` is the directory `n` at the
  top only; `*` and `?` stay within a name, `[...]` is a set, `**/` any number of whole names (`**/node_modules/` at any
  depth, `src/**` all under src), a trailing `/` only directories, a line starting with `#` a comment. An excluded entry
  is recorded as a location with state `excluded` (kind from the directory listing, no stat, no attributes) and not
  walked; what an earlier walk had found under it becomes `excluded` too. An excluded location is never marked gone, and
  `find` leaves it out like a gone one; when its glob is dropped the next index walks it again, as present, with the same
  objects.
- A new root over the user's home directory (`$HOME` or the password entry's) gets the default list: `.cache/`,
  `.local/share/Trash/`, `.thumbnails/`, `**/node_modules/`. Others get none. `lhde-cat excludes NAME` prints the list,
  `lhde-cat excludes NAME +GLOB -GLOB` adds and removes, `add-root ... --exclude GLOB` adds when the root is made.
- **`index NAME --dry-run`** writes nothing: it lists each entry directly under the root (or SUBPATH) with what the walk
  would do with it (excluded by which glob, a mount point not entered, or the counts of entries and directories it would
  walk under it with the excluded ones and mount points there), names and kinds from the directory listings, a directory
  stat'ed only to compare its device. The way to look at a first run on a home before making it.
- `stats` lists what was not walked (the mount points, the top of each excluded subtree), the root's excludes, and every
  mount point of the system's table below the root with what the catalogue knows of it: `not entered`, `under an
  excluded directory, not entered`, `excluded, not entered`, `entered (--cross-fs)` or `not reached yet`.

An **offline** root (`add-root NAME PATH --offline`) is one whose PATH is not reachable from here, such as a listing made on
another machine: it is filled by import-du only; `add-root NAME REALPATH` later gives it its real path.

## Using it

    lhde-cat add-root NAME PATH [--authority filesystem|catalogue] [--offline] [--exclude GLOB]...
    lhde-cat excludes NAME [+GLOB|-GLOB]...
    lhde-cat index NAME [SUBPATH] [--exclude GLOB]... [--cross-fs] [--time MIN] [--dry-run] [--budget MB] [--thumbs 128|256] ...
    lhde-cat roots
    lhde-cat find NAME [--tag T] [--no-tag T [--include-unknown]] [--desc TEXT] [--text WORDS] [--type image/*]
                       [--since DATE] [--until DATE] [--color C] [--in SUBPATH] [--files|--dirs] [--limit N] [--json]
    lhde-cat stats NAME
    lhde-cat show PATH
    lhde-cat set PATH ATTR VALUE | unset PATH ATTR    [--write|--defer] [--op-id ID] [--base-rev N]
    lhde-cat tag PATH +TAG -TAG ...                    [--write|--defer] [--op-id ID]
    lhde-cat commit NAME [PATH...] | diff NAME | conflicts NAME | resolve PATH ATTR file|catalogue
    lhde-cat journal NAME [--since DATE] [--path PATH] [--json]
    lhde-cat relations NAME
    lhde-cat hash PATH...

`find` prints the paths relative to the root (`--json`: an array of rows with kind, object, coverage, size, usage_kib,
mtime, type, taken_at), in the order they were catalogued, and on stderr the count, the time and the coverage of the
attributes. `--tag` is a whole tag (case-blind), `--desc` a part of the description, `--text` words that must each be in
the description, comment or tags (a word's prefix) or in the name (a part of it), `--type` a type or a group (`image/*`),
`--since` and `--until` dates `YYYY`, `YYYY-MM` or `YYYY-MM-DD` over the date taken, else the mtime (`--until 2019-06` takes
in the whole of June). ATTR is a short name (`desc`, `tags`, `comment`, `color`, `run`, `author`, `source`, `build`, `notes`,
`todo`) or a `user.*` name. `set`, `unset` and `tag` write at once on a root whose authority is the file system and defer to
`commit` on one whose authority is the catalogue; `--write` and `--defer` choose. Exit status 3 means a conflict.

## What flxp uses

When the folder shown lies under a root with a catalogue, the Explorer's attribute reads for the folder (the Filter box's
`tag:`, `desc:`, `comment:` words and plain words, and the Description, Tags, Comment and Color columns) come from the
catalogue: one query for the folder (`lhcat_list_dir`), the desired values of each item whose size, mtime and ctime are
those the catalogue observed; an item that changed since (an attribute written by another program changes its ctime) is
read from the file as before. On a network mount that is one stat per item instead of an attribute call per property.
`shell::details::write` and `change_tags` (Add Tag, the band's editor, the Properties sheet, luxattr) record each write
they made in the catalogue holding the file (`lhcat_record_write`: an applied `set` or `unset` with the program's name, the
file observed again), so the catalogue follows flxp's edits. Nothing else in flxp reads it yet.

## Schema and versions

`migrations(version, applied_at, what)` records the schema versions applied; a database is migrated on open and a newer
one than the program knows is refused (version 2 widened the state check of `locations` in place, no rows rewritten).
The tables: `roots` (name, path, host, authority, offline, excludes, added_at, listed_at,
indexed_at), `objects`, `locations`, `relations`, `hashes`, `attrs`, `tags`, `picture`, `thumbs`, `journal`, `scans`, the
FTS5 table `text`. Indices: locations by (parent, name) (unique), name, type, object and inode; attrs by (name, desired)
and pending; tags by (tag, object) and object; picture by taken_at; hashes by full hash; journal by object and time.

Measured on this machine: a generated 2,000,000-line listing imports at about 230,000 lines a second (9 s); `find --tag`
over it answers in 5 ms with the paths; aap's 7.1-million-line stash listing (650 MB) imports in 35 s (200,000 lines a second; again over
the same rows in 19 s) into a 1.26 GB database, whose root total is the listing's `.` line, 4.86 TiB. `LHCAT_COMMIT_MS` changes the time between an index's commits
and `LHCAT_TEST_STOP=N` (or `kill:N`) stops (or kills) an index after N entries, `LHCAT_TEST_NO_MOUNTTAB` leaves the mount table out (the device comparison alone), for the tests. `make check-liblhcat` (`tests/catalogue.sh`, `liblhcat/tests/cat.c`) checks all of the above on a scratch root.

## Not there yet

The remote side (`lhde-remote` over ssh, a catalogue built on the remote), syncing catalogues by their journals, the watcher
(inotify) keeping a catalogue current between index runs, confirming relations from the command line, luxfind (the query
window), flxp's queries across the whole root rather than the folder shown, the gallery, and `lhdecat.py` for the Python
tools.
