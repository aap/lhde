# mupf: the document viewer on MuPDF

Status: the first version is in (apps/mupf; docs/USAGE.md "mupf", docs/KEYS.md, docs/APPLICATIONS.md): the engine on its own
thread, the cache, the four page layouts, zoom, the Contents pane, the search, links, Back and Forward, the Longhorn header;
tests/headless/mupf.sh, mupfheader.sh and tests/toolkit/mupftest.cpp. Open: the items below, in the order they would come.

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

## The Longhorn header

In: under the Longhorn themes mupf has the Explorer's header (shell/lhheader.h; docs/USAGE.md "mupf"), the outline's path
as the crumbs, the Contents pane following the page. Open: the header's search box as the Find box (hidden now, the Find box
is the toolbar's); the toolbar's own Previous / Next beside the header's Back / Forward is mupf's taste, not measured.

## Smaller

- A password prompt (fz_authenticate_password with what is typed) instead of the refusal.
- Show the pane at open when the PDF asks for it (/PageMode /UseOutlines), as Acrobat does.
- Printing waits for wux's (APPLICATIONS.md "No printing").
