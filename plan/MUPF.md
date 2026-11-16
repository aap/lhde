# mupf: the document viewer on MuPDF

Status: the first version is in (apps/mupf; docs/USAGE.md "mupf", docs/KEYS.md, docs/APPLICATIONS.md): the engine on its own
thread, the cache, the four page layouts, zoom, the Contents pane, the search, links, Back and Forward; tests/headless/mupf.sh
and tests/toolkit/mupftest.cpp. Open: the items below, in the order they would come.

aap's brief: mupdf's rendering with a wux GUI, more responsive than mupdf (it should react at once); the TOC as a pane; view
modes; a search box; perhaps bookmarks; perhaps luxnav's breadcrumbs filled from the TOC or the page numbers.

## Bookmarks

A document's own bookmarks (the user's, not its outline) in an extended attribute of the file, as the annotations are
(docs/ANNOTATIONS.md): `user.lux.bookmarks`, one line a bookmark, `PAGE Y NAME` (PAGE counted from 1, Y the top in points,
NAME the rest of the line, UTF-8), written through `shell::details` so the Explorer, the catalogue (liblhcat) and a copy of
the file carry them, read when the document opens. A read-only file or a file system without user attributes keeps them in
mupf.ini's `[bookmarks]` under the file's path instead, and says so. In the window: a Bookmarks menu (Add Bookmark Ctrl+B,
the list, Organize...), and the Contents pane gets a second tab "Bookmarks" (Acrobat 6's navigation tabs). The last page read
could be one more line (`last PAGE Y`), so a document opens where it was left.

## Text selection and copy

MuPDF has what it needs (fz_new_stext_page_from_page, fz_highlight_selection, fz_copy_selection): a Select Text tool beside
the hand (Acrobat 6's two), a drag highlighting quads the engine computes on its thread, Ctrl+C putting the text on the
clipboard (SetClipboardData CF_UNICODETEXT), Ctrl+A the page. A day's work: the engine's stext pages want a cache of their own
beside the display lists.

## luxnav: the breadcrumb filled from the outline

luxnav (luxnav/lhnav.h) is the Longhorn navigation bar: the back and forward orb, the breadcrumb box, the search box, painted
by the client into the top of its own window over luxwm's glass, only under the Longhorn themes. The pieces are reachable
from an application (libluxnav through `$(LUXNAVA)` and `-Iluxnav`, apps/app.mk; wux's DwmExtendFrameIntoClientArea,
DwmGetFrameMetrics, DwmGetCaptionRect and DwmSetInputHole), but they are put together only in the Explorer's
flxp/explorer/header.cpp (the glass header, the input holes, the address edit over the box, the drop-down lists, theme
switches; 3400 lines) and examples/navdemo.c in Xlib. For mupf: the path as segments (the outline entries above the
current page: "Part II > Chapter 4 > 4.2", or "Page 12 of 300" alone for a document without an outline), each chevron's
drop-down listing an entry's children, the address box taking a page number, the search box as the Find box, back and
forward as the history. The segments are cheap (the outline is in the window already); the header around them is a good part
of header.cpp again, more than an afternoon. Better: move the header's glue out of the Explorer into the shell layer (the
"Not public yet" list), then mupf uses it under the Longhorn themes, keeping the toolbar under XP's.

## Smaller

- A password prompt (fz_authenticate_password with what is typed) instead of the refusal.
- The Contents pane following the page (the entry covering the current page selected, as Acrobat 7 does).
- Show the pane at open when the PDF asks for it (/PageMode /UseOutlines), as Acrobat does.
- Printing waits for wux's (APPLICATIONS.md "No printing").
