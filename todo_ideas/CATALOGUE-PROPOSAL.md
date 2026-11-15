# A catalogue for lhde: the proposal for review

Written for aap to comment on and to put to Codex and Astra. lhde is a Windows XP / Longhorn-era desktop for X11 (Linux and
FreeBSD) whose Explorer, flxp, annotates files with extended attributes: description (one line), comment (text), tags,
colour, run-as, and luxfs's author/source/build. The annotations show up as Details columns, in the infotip, in a task band,
in a Properties sheet, and a Filter box narrows a folder by name, tags and text. Thumbnails come from flxp's own decoders and
from the system's freedesktop thumbnailers into the shared ~/.cache/thumbnails. All of that is per folder and synchronous.

## The problem
1. aap's files live mostly on a remote host (indra, FreeBSD 13.1), reached over NFS. Browsing a photo folder there is slow:
   flxp lists a folder only after stat and attributes for every entry, each a round trip; thumbnails need the whole file read
   (5 MB per photo) and are disabled on network mounts; opening a picture in a viewer blocks until it is loaded.
2. Finding things is per folder. There is no way to ask "every photo tagged family from 2019" across a tree, let alone across
   hosts, and no way to browse or tag a remote tree while offline.
3. Scale: the two `du` listings in ~/idx describe 1.56 M entries (home) and 7.1 M entries, 26 TB (the stash), with about
   255 k pictures by extension in the stash alone. Anything per-file-per-round-trip is out; anything that needs the whole tree
   walked before it is useful is out too.
4. Authority: today the file system is the truth and attributes travel with the file. For a photo collection that is edited
   locally in bulk and lives remotely, the better model may be a local catalogue as the truth, with edits committed to the
   remote file system afterwards. Which of the two is authoritative must be decidable later without a redesign, and
   synchronisation between them is the hard part.

## The proposal in one paragraph
A catalogue per root (a local directory, a mount, or host:path): SQLite, one row per file with an identity that survives
renames (dev, inode, size, mtime, a lazy content hash for pictures and documents), every user.* attribute, the picture facts
worth querying (EXIF date, camera, dimensions), a pointer to the thumbnail, and a journal of every attribute change (what,
old, new, when, host, program). It is filled three ways, all incremental: seeded from a cheap listing (the du files, no
stat), walked with a byte budget, and kept current by every write lhde makes. Queries run on it. A small Python agent on the
remote, spoken to over ssh, answers listings with attributes in one round trip, makes thumbnails there (EXIF preview first,
a few KB), and applies attribute writes; flxp gets a folder provider for it; the catalogue of a remote root syncs down for
offline use and queued writes go back up. The Explorer shows a folder from readdir alone and fills the rest from a worker,
so a remote folder is responsive before anything else exists. The first user-visible pieces are a filter that answers in
milliseconds over a whole tree and thumbnails that appear on a remote mount.

## Core ideas
* **Truth and cache are a policy, not a format.** The journal records every change with origin and time. With "file system
  authoritative", the catalogue is rebuilt from a walk and the journal is a log. With "catalogue authoritative" (a photo
  library edited locally), the journal is the truth and the xattrs on the remote are written back from it ("commit"). The
  switch is a per-root setting; the tables are the same. Both modes keep attributes on the files in the end, so a tree
  copied with rsync -X or tar --xattrs keeps its annotations whichever mode produced them.
* **Identity beyond the path.** A moved photo keeps its tags and its history because the row is found by (dev, ino) first,
  then by content hash, then by path. The hash is computed lazily (when a row is first seen as a picture or when a
  rename is suspected), never as a precondition.
* **Seed cheap, enrich lazily.** A du listing gives paths and sizes for millions of files in seconds without touching the
  remote. stat, type, attributes, EXIF and thumbnails are added when a folder is visited or when a background walk with a
  byte budget reaches it. The catalogue is useful at every stage of completeness and says what it does not know yet.
* **Round trips, not bytes, are the enemy.** LIST over the ssh agent returns a folder's stat and attributes in one message;
  a thumbnail is the camera's embedded EXIF preview (160x120, in the first 64 KB) first and a real one later; the full
  file crosses the network only when opened.
* **The UI never waits.** Enumeration is readdir first; everything slower comes from a worker and fills in. This is
  independent of the catalogue and is the first step because it fixes the daily pain by itself; a slow-path test harness
  (a preload library delaying stat/open/read under a prefix) makes the responsiveness a test.
* **One agent script for the remote.** indra runs FreeBSD 13.1: the remote side is one Python 3 file with Pillow optional,
  runs unchanged on 13.1, 15 and Linux, needs no build of lhde, speaks a line/JSON protocol over `ssh host lhde-remote`,
  and can build the remote's own catalogue so queries run where the files are.
* **External first, then the GUI.** `lhde-cat` is a command (index, find, set, journal, sync). flxp's existing Filter box
  uses it when the shown folder lies under a root (instant, tree-wide). A small window, luxfind, is the query UI; the
  photo views (by month, by tag) and a static gallery generated from the catalogue come after.

## Synchronisation, the hard part, as proposed
* Each root has a journal; each host appends with its own origin id and a monotonic sequence. Sync exchanges journal
  tails. Per attribute per file, the latest change wins (timestamps with origin as tie-break); a conflict (two hosts changed
  the same attribute since the last sync) is recorded and shown, not silently resolved, but with one user it is rare.
* In file-system-authoritative mode, a sync also re-reads the remote's attributes for rows whose mtime/ctime changed, since
  other programs may have written them.
* In catalogue-authoritative mode, the remote's attributes are a derived view: `commit` writes pending journal entries to the
  files (through the agent) and marks them applied; a remote file changed outside lhde is detected by ctime and surfaced.
* Thumbnails are content-addressed (freedesktop's URI+mtime key, or the content hash when known) so a synced catalogue can
  bring its thumbnails along or fetch them lazily.
* Deletions and moves: a row whose (dev, ino) is seen under a new path is a move; a row not seen in a complete walk of its
  folder is marked gone, not deleted, and its journal kept.

## What is deliberately not proposed
* No daemon that must always run; the watcher is used when present, walks are resumable, and nothing breaks without it.
* No new on-disk format for the files: xattrs remain the interchange with other tools (luxfs, rsync, tar).
* No full-text indexing of file contents; names, descriptions, comments and tags only (SQLite FTS5 when available).
* No 3D stage: Phodeo's research question (browse thousands of pictures by time cheaply) is answered by date-grouped
  thumbnail views over the catalogue with the thumbnail budget already in place.

## Steps, each useful alone
1. Responsive Explorer: async enumeration, EXIF-preview thumbnails on mounts within a byte budget, luxview's three stages,
   the slow-path harness with numbers as tests.
2. `lhde-cat` locally: SQLite schema with identity and journal, import-du, index with budget, find/stats/set/journal, the
   Filter box over it, luxfind. (In progress: the catalogue tool.)
3. `lhde-remote` over ssh (Python, 13.1-safe), flxp's provider, tested against indra and soma.
4. Sync and the authority switch per root, commit to the remote.
5. Photo query views, bulk tagging, the gallery from the catalogue.

## Questions for reviewers
* Is SQLite per root the right store at 7 M rows, or should the seeded listing rows live in a leaner table until enriched?
* Journal merge: is latest-wins per attribute with surfaced conflicts enough for one user on several hosts, or is a
  vector clock worth it from the start?
* Catalogue-authoritative mode: what must the commit guarantee before an annotation is considered safe (written to the
  remote xattr? journaled on two hosts? both)?
* The ssh agent versus using the NFS mount for everything with aggressive caching: is a protocol worth it, or would a local
  catalogue plus EXIF previews over NFS already be good enough?
* Identity: content hash of the whole file, or a cheap prefix hash plus size for pictures, with the full hash on demand?
* Where should the thumbnails of a remote catalogue live when synced: freedesktop's cache keyed by the remote URI, or inside
  the catalogue directory so the catalogue is self-contained and copyable?
