# mupf: the document viewer on MuPDF

Status: the first version is in (apps/mupf; docs/USAGE.md "mupf", docs/KEYS.md, docs/APPLICATIONS.md): the engine on its own
thread, the cache, the four page layouts, zoom, the Contents pane, the search, links, Back and Forward, the Longhorn header,
the password prompt; tests/headless/mupf.sh, mupfheader.sh and tests/toolkit/mupftest.cpp. Open: the items below, in the
order they would come.

aap's brief: mupdf's rendering with a wux GUI, more responsive than mupdf (it should react at once); the TOC as a pane; view
modes; a search box; perhaps bookmarks; perhaps luxnav's breadcrumbs filled from the TOC or the page numbers.

Bookmarks (the user's, in an extended attribute of the file) are dropped: aap finds attributes too intrusive for them.

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

- Show the pane at open when the PDF asks for it (/PageMode /UseOutlines), as Acrobat does.
- Printing waits for wux's (APPLICATIONS.md "No printing").
