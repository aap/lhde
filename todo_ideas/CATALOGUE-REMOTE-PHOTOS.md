# Annotations at scale: the catalogue, remote trees, photos and a gallery (a plan, 2026-11-15)

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
