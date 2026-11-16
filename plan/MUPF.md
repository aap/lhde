# mupf: the document viewer on MuPDF

Status: the first version is in (apps/mupf; docs/USAGE.md "mupf", docs/KEYS.md, docs/APPLICATIONS.md): the engine on its own
thread, the cache, the four page layouts, zoom, the Contents pane, the search, links, Back and Forward, the Longhorn header,
the password prompt, Markdown files (md4c's HTML laid out by MuPDF, GitHub's look, followed on disk); tests/headless/mupf.sh,
mupfheader.sh, mupfmd.sh and tests/toolkit/mupftest.cpp, mdtest.cpp. Open: the items below, in the
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
as the crumbs, the Contents pane following the page, the search box as the Find box, the menu band and the toolbar on one
row (the Explorer's 4051 command bar), the Small header with View > Large Header to compare, the page navigation in the
status bar under both looks, the Longhorn Open dialog (flxp-filedialog). Open: which size to keep (aap: "until it's
decided"); under the header the toolbar holds the zoom and fit buttons alone, and whether it stays is aap's call; its glyphs
are mupf's XP-coloured drawings on the bar's dark gradient.

## Markdown

In (docs/USAGE.md "mupf"). Next: luxpad's preview pane, mupf's view beside the editor. For that the view has to become a
component: today it is mupf.cpp's globals (`App g`: the engine, the cache, the layout, the scroll position, the links, the
history) and its window procedure, with the chrome (toolbar, status bar, pane, header) reaching into them. A `mupf::View`
(a window class over one Engine and Cache, its own state, opened on a path or on a string of Markdown with a base
directory, notifying its owner of the title, the outline, a link to another file and the page) would let mupf be its frame
and luxpad embed it; the engine needs an open from text (luxpad's unsaved buffer) besides the path. Also open: the page
width follows the window (GitHub reflows a README to a narrow window; mupf lays it out at 660 pt and zooms), syntax colours
in code blocks, the theme's fixed font for code (MuPDF's Courier-like monospace today), pictures from the web.

## Smaller

- Show the pane at open when the PDF asks for it (/PageMode /UseOutlines), as Acrobat does.
- Printing waits for wux's (APPLICATIONS.md "No printing").
