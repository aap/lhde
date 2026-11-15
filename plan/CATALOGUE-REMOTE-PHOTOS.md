# Annotations at scale: the catalogue, remote trees, photos and a gallery

Status: the working plan (2026-11-15; the reviewed design is plan/CATALOGUE-PROPOSAL.md). Step 2 is in (lhde-cat, flxp's Filter
box and writes over it, docs/CATALOGUE.md; luxfind and tree-wide queries open) with the machines list as groundwork for step 3;
steps 1 and 3 are next. See plan/STATUS.md.

aap: most of the stuff lives on indra; organising it means tagging and reorganising old photos over the network; a
directory listing with thumbnails should not be much data; thumbnails could be made on the remote; finding photos is too
common not to do well; a web gallery per directory (today a crude static page); Longhorn's Phodeo explored the same space.

## Principles
1. The file system is the database. Descriptions, tags, comments, colour, run-as live as extended attributes on the files,
   local or remote, and travel with the file (cp -a, rsync -X, flxp's own operations). Nothing else is authoritative.
2. The catalogue is a cache of that truth: rebuildable from a walk, kept fresh by the watcher and by lhde's own writes,
   copied for offline use. Losing it loses nothing.
3. One code path for local and remote: flxp's folder model gets a provider; the same provider serves the desktop, the
   command line and the gallery.

## 1. The catalogue (local first)
* `lhde-cat` (C on liblhde, so flxp, luxshell and the tools share it; SQLite, one database per root such as ~/Pictures or
  indra:/data/photos, under ~/.local/state/lhde/catalogue/): per file path, size, mtime, MIME type, every user.* attribute,
  the picture facts worth a query (EXIF date taken, camera, dimensions, orientation; PDF title/pages), the thumbnail's id.
* Built by a walk (`lhde-cat index ROOT`), updated by inotify/kqueue events and by every write through the shell layer
  (details::write), so it is current for anything lhde touched and at most one walk behind for the rest.
* Queries: `lhde-cat find --tag x --desc "..." --since 2019-06 --type image/* ROOT`, by date range, by colour, free text
  over desc/comment/name; results as paths. flxp's Filter box and the saved filters run on it when a root has one, else
  on the walk as today. Haiku's lesson: live queries over attributes, not a separate database of records.

## 2. Remote trees over ssh
* NFS is the wrong transport for metadata: one getxattr per file per attribute. Instead `lhde-remote`, a stdio protocol over
  `ssh HOST lhde-remote` (no new ports, ssh's auth; the remote needs lhde's tools installed, Linux or FreeBSD): LIST dir ->
  entries with stat and all user.* attributes in one round trip; THUMB path size -> the thumbnail made on the remote with the
  same thumbnailer code (freedesktop cache there too); SET/REMOVE attribute; MOVE/COPY/TRASH; QUERY (the remote's
  catalogue); READ for the file itself only when opened. Thumbnails are the only bulk data of a listing: 128 px PNGs, a few
  KB each, cached locally under the freedesktop cache keyed by the remote URI.
* flxp: a second folder provider (`ssh://host/path` places, or a configured mapping "this NFS mount is indra:/data": metadata
  and thumbnails through the protocol, bytes through the mount). The remote-fs detection that disables thumbnails today
  becomes "use the provider". luxview walks a remote folder the same way.
* Offline: `lhde-cat sync HOST ROOT` copies the remote catalogue (and its thumbnails on demand) so browsing, searching and
  even tagging (queued writes) work without the host; writes replay when it is back.

## 3. Photos in the Explorer
* Query views as 4051 meant them: "All Photos" of a root grouped by month/year (EXIF date, else mtime), the Filter box over
  tags and descriptions, saved queries in the left pane, the band's bulk editing of a multiple selection (tags on all,
  description per item), keyboard tagging (select, Add Tag, Enter), move to folders by drag with the attributes travelling.
* luxview as the inspector: Metadata one key away, the filmstrip over the query's results, not only the folder.
* Phodeo (the saved article "Phodeo in detail" in ~/fun/longhorn/web_ref) is the research reading: time-ordered browsing of
  thousands of pictures at low cost per picture; our version is the date-grouped Icons view with the thumbnail budget already
  in place, not the 3D stage.

## 4. The gallery
* `lhde-gallery ROOT` writes a static site from the catalogue: one page per folder or per query, the thumbnails it already
  has (the 256 px cache or a gallery size made once), a small client-side filter by tag/date (a few KB of JS, no framework),
  the full picture on click, the descriptions and tags shown as captions; regenerate incrementally from the catalogue's
  mtimes. Run on the remote (the pictures and thumbnails are there), served by whatever already serves files.
* The same generator fed by a query gives "the 2019 trip" or "everything tagged family" as a page.

## Order
(a) lhde-cat locally with flxp's Filter and the band reading it; (b) lhde-remote with LIST/THUMB/SET and the flxp provider,
tested between this machine, indra and soma; (c) the photo query views and bulk tagging; (d) lhde-gallery; (e) offline sync.
Each step is useful on its own; (a) and (b) are the foundation the rest stands on.

## Refinements (aap, 2026-11-15): authority may flip, sync is the hard part, responsiveness first
* Design the catalogue so the flip costs no format change: every record carries the file's identity beyond its path (dev,
  inode, size, mtime, and a lazily computed content hash for pictures and documents, so a moved or renamed photo keeps its
  record and its history), and every attribute write goes through one function that also appends to a per-root journal
  (what, old value, new value, when, by which host/program). Sync is then a merge of journals (last writer wins per
  attribute; one user, conflicts are rare and shown), the catalogue can become the truth later by declaring the journal
  authoritative and the xattrs a cache written back, and an archive keeps attributes even on a file system without them.
* Responsiveness is the first deliverable, before any index: flxp's folder enumeration is synchronous today (readdir + stat
  + attributes before the list shows), which is what makes a remote mount feel dead. Step 1 is: readdir first and the list
  shown at once, stats and attributes filled in from a worker, nothing on the UI thread ever waits on a remote path;
  thumbnails on a remote mount from the cache first (instant), then from the JPEG's own EXIF preview (160x120 in the first
  few KB of most camera files: read 64 KB, not 5 MB), the full decode last and only within a byte budget per folder; luxview
  the same three stages. Measured with a slow-path test harness (an LD_PRELOAD that delays stat/open/read under a prefix),
  so "2000 photos on a 50 ms mount: the list in under half a second, thumbnails appearing progressively" is a test, not a hope.
* indra runs FreeBSD 13.1 (no inotify, old packages): the remote side must be one Python 3 script with Pillow optional
  (`lhde-remote`), not a build of lhde; it answers LIST (stat + attributes in one go), THUMB (EXIF preview or Pillow),
  SET/REMOVE, and later QUERY over a catalogue built there by the same script. It must run unchanged on 13.1, 15 and Linux.
* GUI or external: the index starts external (`lhde-cat` CLI: index, find, journal), is used by flxp's existing Filter box
  when a root has one (instant, and across the whole tree not just the folder), and gets its own small window for the
  fun part (`luxfind`: XP's Search Companion / 4051's query views; a query box, the results in the shared list view, double
  click opens luxview, drag to tag). flxp's core stays unaware beyond the provider and the filter hook.
* Steps, each a satisfying end in itself: (1) async enumeration + EXIF thumbnails + the slow-path harness; (2) lhde-cat
  locally + the Filter box over it + luxfind (in: lhde-cat with import-du, index, touch, find, the observed/desired/pending
  attributes, the operation journal and the authority setting, flxp's Filter box and details::write on it; docs/CATALOGUE.md.
  Open: luxfind, queries across the whole root in flxp); (3) lhde-remote over ssh with the flxp provider, tested against indra (13.1)
  and soma; (4) the journal and sync; (5) photo query views and the gallery.


## "My machines" (aap, 2026-11-15) and what ZFS offers
aap: a list of the machines I have and how to reach them, with a synchronised catalogue of their file systems; something
between KDE Connect and a directory service. indra and soma are FreeBSD on ZFS; vishnu (here) is not.
* **The machine list is the catalogue's address book**: `~/.config/lhde/machines.ini` (name, ssh host, OS, notes, what it
  serves: catalogue roots, zfs pools, a tray of its hardware through lhde-hw later) and every root bound to a machine. The
  same file drives the remote helper (`ssh HOST lhde-remote`), luxshell's future "machines" tile (reachability, battery of a
  laptop through the helper = the KDE Connect-ish part), and a wux "My Network Places" window in XP's idiom (each machine a
  computer icon; open it: its roots as folders through the provider; Properties: OS, pools, last sync). A directory service
  in the small: names to addresses and capabilities, kept in a text file the user owns, no daemon.
* **ZFS makes two hard problems easy on indra and soma**:
  - `zfs diff SNAP1 SNAP2` lists exactly the files changed, created, deleted or renamed between two snapshots: a change
    feed without a watcher. A catalogue root on a ZFS dataset records the snapshot it last observed; the next index asks
    `zfs diff` instead of walking, so a 7 M-file tree re-indexes in seconds and "scans are observations only" stops being a
    limitation there. Snapshots also give a consistent catalogue version: "as of @2026-11-15".
  - `zfs send | zfs receive` moves a dataset incrementally and exactly: a catalogue plus its self-contained thumbnails on a
    small dataset replicates to another machine as a stream, no sync protocol of our own needed for that part.
  - User properties on datasets (`lhde:root = stash`, `lhde:catalogue = ...`) let the machine itself say which trees are
    catalogued; `zfs list -o` from the helper fills the machine's Properties.
  - vishnu without ZFS keeps the walk and the inotify watcher; the catalogue API is the same.
* **ZFS admin in wux** (later, aap's idea): pools, datasets, snapshots, scrub status and used/avail in XP's Disk Management
  / Computer Management idiom, local or through the helper over ssh; destructive actions behind XP-style confirmations. More
  wux front ends for OS-level work (mounts, services, users) are the same pattern: a small Python helper on the host, a
  wux window here.
