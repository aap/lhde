# liblhcat: the catalogue

`lhcat.h` (`lhcat_*`): one SQLite database per root of a tree's files, their attributes (observed, desired, pending), picture
facts and thumbnails, with the journal of attribute operations. `cat.c` the roots, the schema and paths; `attrs.c` the
attribute model, operations, write-back and the journal; `fill.c` import-du, the walk, touch and the thumbnail helper;
`find.c` the queries; `exif.c` the picture headers (also the EXIF thumbnail's place, which flxp's and luxview's
thumbnails read through `lhcat_picture_parse`: the shell layer links liblhcat) and sha256; `lhde-cat.c` the command line. How it works:
docs/CATALOGUE.md.

Build: `build/lib/liblhcat.a`, `build/bin/lhde-cat` (`make liblhcat`; sqlite3 through pkg-config). Tests: `make
check-liblhcat` (`tests/catalogue.sh` over a scratch root, then `tests/cat.c`); `LHCAT_PERF_LINES=N` sizes the generated
listing, `LHCAT_COMMIT_MS` the time between an index's commits, `LHCAT_TEST_STOP=N` (or `kill:N`) interrupts (or kills)
an index after N entries.
