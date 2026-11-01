# luxwm's client protocol

The X properties and messages between luxwm and the programs that cooperate with it: the tall glassy Explorer header of 4074
(the frame plate continued down behind the client's title, breadcrumb and search box, DWM blur and all), its opaque counterpart
without composition, the vision header of luxnav (docs/NAVBAR.md), and a few desktop-wide states. This is the contract between
the wux side (flxp, wux's `dwmapi` over it: `wux/sys/x11.cpp`, `wux/w32/dwmapi.cpp`, `flxp_dwmapi.h`) and the lux side (luxwm,
luxshell). The executable references are `examples/extendframe.c` (the extended frame, `-c` CLIENT_CAPTION, `-n`
NO_HEADER_PIXMAP) and `examples/navdemo.c` (the vision header). The atom names and the WM name "lhwm" are stable.

## How it works

luxwm is non-reparenting: the decoration is a separate ARGB window (`dec`) directly **under** the client in the stacking order,
larger than it by the frame margins; picom composites both. So:

1. the client tells luxwm how far the frame should extend into its own area (`_LHWM_EXTEND_FRAME`);
2. luxwm paints the plate so that it also covers that area, under the client;
3. the client is a 32-bit ARGB window and paints the extended area **fully transparent** (premultiplied `0,0,0,0`) except for
   its own content (text, icons, widgets), drawn normally on top;
4. picom shows the plate through the client's transparent pixels.

This is Windows' `DwmExtendFrameIntoClientArea` + "paint the area black with alpha 0": nothing is reparented, no shared surfaces;
the client needs to know only *where* the glass is, not how it is drawn.

## Properties at a glance

| Property | On | Written by | Type | Section |
| --- | --- | --- | --- | --- |
| `_LHWM_EXTEND_FRAME` | client toplevel | client | CARDINAL[4] | "Extended frames" |
| `_LHWM_FRAME_FLAGS` | client toplevel | client | CARDINAL | "Frame flags" |
| `_LHWM_SELF_FRAMED` | client toplevel | client | CARDINAL 1 | the client draws its whole frame itself (glassdemo); luxwm adds none |
| `_LHWM_HEADER_PIXMAP` | client toplevel | luxwm | PIXMAP | "Header background without composition" |
| `_LHWM_CAPTION_RECT` | client toplevel | luxwm | CARDINAL[4] | "Client content over the caption rows" |
| `_LHWM_DESKTOP_LAYER` | desktop window | client | CARDINAL | "Desktop windows" |
| `_LH_DROP_SHADOW` | override-redirect window | client | CARDINAL | "Drop shadows" |
| `_NET_FRAME_EXTENTS` | client toplevel | luxwm | CARDINAL[4] | the real frame margins outside the client; an extension (inside the client) does not change them |
| `_LHWM_COMPOSITED` | root | luxwm | CARDINAL 0/1 | "Root properties" |
| `_LHWM_THEME` | root | luxwm | STRING | "Root properties" |
| `_LHWM_FRAME_METRICS` | root | luxwm | CARDINAL[19] | "Root properties" |
| `_LH_THEME`, `_LH_THEME_RESOLVED` | root | the session (docs/SETTINGS.md) | UTF8_STRING | "Theme changes" |
| `_LH_STARTMENU_OPEN` | root (and the provider's selection window) | the start-menu provider | CARDINAL | "Start menu state" |

Client messages: `_NET_WM_MOVERESIZE`, `_NET_WM_STATE`, `_NET_CLOSE_WINDOW` ("Caption behaviour in the client"),
`_GTK_SHOW_WINDOW_MENU` ("The window menu at a point"), `_NET_RESTACK_WINDOW` ("Raising a window during a drag").

## Root properties

Set by luxwm; a client watches the root for PropertyNotify. luxwm deletes them on a clean exit (`luxwmc quit`); `_LH_THEME`
stays (it is the session's). A client treats a missing `_LHWM_COMPOSITED` as 0 (another WM, or luxwm not running).

| Property | Type | Meaning |
| --- | --- | --- |
| `_LHWM_COMPOSITED` | CARDINAL 0/1 | 1 = luxwm draws DWM glass frames: a compositor is present (XFixes watch of `_NET_WM_CM_Sn`) and the frame mode is dwm. 0 in msstyles/classic mode, without a compositor, and for a theme with `composition = off` even with picom running. The `DwmIsCompositionEnabled` of this protocol: decide glass from it, never from the theme name. Rewritten on every theme switch even when unchanged; written again if someone deletes it. When it changes, every client with an extension or flags is repainted. |
| `_LHWM_THEME` | STRING | The active frame set (a directory, or a spec `DIR\|STYLE\|COLOR\|SIZE`), derived from the session theme `_LH_THEME`; read-only for clients (writing it directly asks luxwm for that frame set). Clients read the frame set's `nav.ini` beside its theme.ini. |
| `_LHWM_FRAME_METRICS` | CARDINAL[19] | The frame now in use, so a client can lay out its header to match (fields below). Written when it changes and on every theme switch. Fonts are not in it (the theme's). Readers take up to 19 items and treat missing ones as absent. |

`_LHWM_FRAME_METRICS` fields (colours `0xRRGGBB`):

| # | Field | Meaning |
| --- | --- | --- |
| 0 | `caption_h` | top frame margin of a normal window (= `_NET_FRAME_EXTENTS` top without CLIENT_CAPTION) |
| 1..3 | `border_l border_r border_b` | normal side and bottom margins |
| 4 | `text_rgb` | caption text, focused |
| 5 | `glow_rgb` | DWM: the TextGlow colour; msstyles: CAPTION's text-shadow colour, 0 without one |
| 6 | `inactive_text_rgb` | caption text, unfocused |
| 7 | reserved | 0 |
| 8 | `max_caption_h` | top margin while maximised: DWM `caption_h` (the caption stays), msstyles `CAPTIONBARHEIGHT + 1` (the MAXCAPTION band) |
| 9..11 | `max_border_l max_border_r max_border_b` | maximised margins: 0 for every theme (DWM drops them, msstyles pushes them off-screen) |
| 12 | `border_t` | rows kept above a CLIENT_CAPTION client (its `_NET_FRAME_EXTENTS` top): theme key `caption_border`, default msstyles cnBorders (3 + SIZINGBORDERWIDTH, 4 for the shipped styles), DWM the side border width (9) |
| 13 | `max_border_t` | the same while maximised: 0 |
| 14 | `max_text_rgb` | caption text of a focused maximised window (theme.ini `text_max_active`, default `text_rgb`) |
| 15 | `max_inactive_text_rgb` | unfocused maximised (`text_max_inactive`, default `inactive_text_rgb`) |
| 16 | `max_glow_rgb` | DWM: `glow_max` (default `glow_rgb`), `0xFFFFFFFF` = no glow (`glow_max = none`); msstyles: MAXCAPTION's text-shadow colour, else field 5 |
| 17 | reserved | 0 |
| 18 | `deco_h` | DWM themes with the key `deco_h`: the rows at the frame's top that are the frame's own decoration (aerovision: 15, the caption buttons); else `caption_h` |

Examples: aero4074 (aero-patch frames) `27 9 9 9 ... 27 0 0 0 9 0` (fields 0..3, 8..13), slate4074 `24 4 4 4 ... 20 0 0 0 4 0`,
luna `30 4 4 4 ... 26 0 0 0 4 0`; fields 14..17 of aero-patch / jade4074: `0xffffff 0x626469 0xffffffff 0` (the Explorer header
flips its text when zoomed).

## Extended frames: `_LHWM_EXTEND_FRAME`

CARDINAL[4] `left right top bottom` on the client toplevel: extend the frame plate this many client pixels inwards from each
client edge (clamped to the client size). `top` is the common one (the tall caption). `0xFFFFFFFF` in all four = sheet of glass
(the whole client is frame). Set before mapping or at any time; luxwm repaints on change. Deleted = a normal frame. It does not
change the client's geometry or `_NET_FRAME_EXTENTS`; the dec window keeps its outer geometry and shape (the shape trims only the
outer corners, never the extended area). Hit-testing is unchanged: the dec window gets no events under the client, and caption
behaviour inside the client is the client's ("Caption behaviour in the client").

Painting while composited (`frame.c` `plate_ext`, `fog_band`):

- Top, frame sets with a `plate_explorer` (aero-patch, aerovision, jade4074: Longhorn's `LgFrameExplorer`, msstyles STREAM 484,
  a 73-row caption band): that plate instead of the normal one. `explorer_rows = r0 r1 line` (15 71 78): rows 0..14 stay 1:1, the
  band [r0, r1) is stretched to the header's height minus 2, the texture's inner light/dark lines (rows 78/79) go on the header's
  last two rows; a 46 px extension (27 + 46 = 73) copies the band 1:1. With `header_mode = crop` the band is never stretched
  ("Vision header"). Maximised: `plate_max_explorer` (MAXEXPLORER, STREAM 494) with `max_explorer_rows` the same way.
- Top, frame sets without one: the normal plate's caption band drawn `caption_h + top` high; only the rows `extend_rows = r0 r1`
  stretch (default `2 caption_h-2`: everything between the outer highlight rows and the inner lines). The aero-style sets use
  `15 25`: their plate has the caption-button pill baked into rows 2..14 of its right 100 columns, which must stay 1:1.
- Left, right, bottom: the side and bottom bands widened / heightened, stretching their interior columns/rows (2 .. margin-2), so
  the inner dark/light lines move inwards to where the client's own content starts. The sides' vertical inner lines start below
  the header; in the header rows the sides come from the caption band's corner columns, as on the reference.
- Sheet of glass: the caption gradient stretched over the whole client; no band draws its inner lines; no fog.
- The overlay (highlight) follows the outer edge. Buttons stay where they are; the title is omitted with `NO_TITLE`.
- Caption fog (`captionfog.png`, Longhorn's CAPTIONFOG part, imageres/jade part 505): only with the frame theme key `fog = 1`
  (every shipped set has `fog = 0`: 4074 draws none) and without `NO_FOG`, on a top extension: x from the left to the right border
  (under the caption buttons too, which are painted over it), y from 0 to `caption_h + top`, the 99x26 image 3-sliced (10 px fades
  1:1, middle stretched, its 26-row profile stretched to the band height).
- Maximised: the extension still applies (Explorer keeps its tall header); the maximised plates and square corners apply.

picom (`session/picom-glass.conf`, rule `_LHWM_EXTEND_FRAME@`): such clients get no `blur-background` (the dec window under them
is blurred already; blurring the client would blur the plate twice) and no shadow (the dec window casts the frame's; the client's
showed as a dark seam across the glass along its top edge).

Without composition (`_LHWM_COMPOSITED` = 0) the dec window shows a normal frame and the extension is not painted; the X server
shows the client's ARGB alpha as black, so the client paints its header opaque, from `_LHWM_HEADER_PIXMAP`. The client keeps
`_LHWM_EXTEND_FRAME` and `_LHWM_FRAME_FLAGS` set in both states: that is what asks for the pixmap (deleting them gives an ordinary
framed window with title and icon).

## Frame flags: `_LHWM_FRAME_FLAGS`

CARDINAL on the client toplevel, also without `_LHWM_EXTEND_FRAME`; changes repaint at once. Bits 16 and up are luxwm's and are
masked off when a client sets them.

| Bit | Name | Meaning |
| --- | --- | --- |
| 0 (1) | `NO_TITLE` | luxwm draws no caption text (the client draws its own title), in every mode. While not composited the frame caption is then empty, so the client must draw a title in its header (the 4074 Explorer does). |
| 1 (2) | `NO_ICON` | no caption icon, in every mode |
| 2 (4) | `NO_FOG` | no caption fog on a DWM extended frame; a no-op with the shipped sets (`fog = 0`) |
| 3 (8) | `CLIENT_CAPTION` | the client covers the caption rows too ("Client content over the caption rows"); implies `NO_TITLE` and `NO_ICON` |
| 4 (16) | `NO_HEADER_PIXMAP` | no `_LHWM_HEADER_PIXMAP` for this client ("Header background without composition") |

## Caption behaviour in the client

- **Move / resize**: `_NET_WM_MOVERESIZE` (EWMH, to the root): `l[0] l[1]` the pointer's root position of the press, `l[2]`
  direction, `l[3]` button, `l[4]` source (1). Directions: 8 move; 0..7 resize (top-left, top, top-right, right, bottom-right,
  bottom, bottom-left, left); 9 size with the keyboard (EWMH SIZE_KEYBOARD); 10 move with the keyboard (MOVE_KEYBOARD); 11 cancel.
  The client sends it on a press in its header where nothing interactive is hit, **after `XUngrabPointer`** (the press gave it an
  implicit grab; if the client still holds it luxwm retries its grab for 100 ms). luxwm then runs its own drag exactly as if the
  frame had been pressed (`drag_run`: `EDGE_LOCK`, moving a maximised window un-maximises it, no resize while maximised; no drag
  snapping of its own). A mouse drag grabs only the pointer (Escape still goes to the focused client) and ends on the release of
  `l[3]` (any button if 0); if the button is already up when luxwm grabs (a quick click) nothing happens. Keyboard mode grabs
  pointer and keyboard and warps the pointer to the window's centre (move) or bottom-right corner (size); the arrows move 10 px,
  Return/Enter/space or a click ends, Escape cancels. A second `_NET_WM_MOVERESIZE` during a drag ends it; 11 cancels it: the
  window goes back, and a window that was maximised is maximised again.
- **Maximise, minimise, close**: on a double-click in the header, `_NET_WM_STATE` toggle `_NET_WM_STATE_MAXIMIZED_VERT/HORZ`;
  `_NET_WM_STATE_HIDDEN` minimises; `_NET_CLOSE_WINDOW` closes.
- **Window menu**: a right-click sends `_GTK_SHOW_WINDOW_MENU` on the release (next section).

## The window menu at a point: `_GTK_SHOW_WINDOW_MENU`

A ClientMessage to the root (SubstructureRedirect | SubstructureNotify), `window` = the client's toplevel, format 32, `data.l[0]`
the pointer device (ignored), `l[1]`, `l[2]` the root x, y: luxwm opens the window menu (LUXWM.md "The window menu") of that
window with its top-left corner there, flipped left/up where it does not fit, and runs it (it grabs pointer and keyboard: send it
after the button that asked for it is released). GTK's own name, in `_NET_SUPPORTED`, so GTK's client-side headers use it too. A
minimised window gets its menu as well (XP's task button menu): Restore (the default item) and Maximize bring it back, Move, Size
and Minimize are greyed. luxshell's task buttons send it on a right click.

## Header background without composition: `_LHWM_HEADER_PIXMAP`

While `_LHWM_COMPOSITED` is 0 a client with `_LHWM_EXTEND_FRAME` or `CLIENT_CAPTION` (and without `NO_HEADER_PIXMAP`) cannot show
glass, and rebuilding the theme's caption band itself is fragile, so luxwm renders it: a PIXMAP of the client's visual and depth,
published as `_LHWM_HEADER_PIXMAP` (PIXMAP) on the client (`wm.c` `header_update`, `frame.c` `frame_header_image`).

- Size: width = the client's; height = the current top frame margin (`_NET_FRAME_EXTENTS` top: `caption_h` normally,
  `CAPTIONBARHEIGHT + 1` maximised, `border_t` with CLIENT_CAPTION) + the extension's `top` (clamped to the client; sheet of glass
  = the whole client height). Row 0 is the frame's outer top edge, the columns are the client's, so the last row lies against the
  bottom of the header: **copy the last `top` rows** (`src_y = pixmap_height - top`). Only `top` shapes it; left/right/bottom
  extensions are not rendered into it.
- Contents: the caption bitmap (MAXCAPTION when maximised) nine-gridded with its own SIZINGMARGINS over `caption + top` rows,
  active or inactive by focus; never title or icon. The caption buttons are drawn where the frame has them; only with
  CLIENT_CAPTION do they fall into rows the client copies, and only then is the pixmap re-rendered for their hover/pressed states.
  A depth-24 client gets the (few) transparent corner pixels flattened onto black.
- Life: re-rendered on focus, resize, maximise/restore, theme switch and extension/flag change, with a new id each time: the
  property is replaced, then the old pixmap freed. Deleted when composition comes, when the extension and flag go, when
  `NO_HEADER_PIXMAP` is set, when the window loses its frame, and on a clean exit. A client copying from an id it read a moment
  earlier can get BadDrawable/BadPixmap: ignore those errors and repaint on the PropertyNotify, which it treats like an Expose of
  the header.
- `NO_HEADER_PIXMAP` may be set and cleared at any time (e.g. on a theme switch): set, luxwm deletes the property and frees the
  pixmap at once; cleared, a new one is rendered and published if the other conditions hold. It changes nothing else (frame,
  extension, `NO_TITLE`/`NO_ICON`, `_LHWM_CAPTION_RECT`). For a client that draws its header itself (flxp's navbar for
  Slate/Jade) and wants the pixmap only for styles without a `Navbar` class (Luna/XP).
- `_LHWM_FRAME_METRICS` keeps the caption colours for clients that draw their own band.

## Client content over the caption rows: `CLIENT_CAPTION`

Bit 3 of `_LHWM_FRAME_FLAGS`. Longhorn's Explorer puts its header (folder icon, big title, breadcrumb) in the caption rows too,
leaving only the caption buttons to the frame. With the bit set:

- **Placement**: the client's top edge is at the frame's border row `border_t` (field 12; maximised field 13 = 0), so
  `_NET_FRAME_EXTENTS` top is `border_t`: the client window grows upward by `caption_h - border_t` (aero-patch/jade4074 18,
  slate4074 20, luna 26; maximised `max_caption_h - max_border_t`: 27, 20, 26). luxwm resizes it. A window mapped with the flag gets
  the frame a normal window would have, its client taller; setting or clearing the flag later keeps the frame and resizes the
  client; after a WM restart the frame is put around the client as it is. `_LHWM_EXTEND_FRAME` top still counts from the client's
  top, which now includes the caption rows (extendframe -c: the buttons' bottom + 2 px, at least 18, plus its 60 px header).
- **The logical rectangle**: the client's window without the rows luxwm grew it by, i.e. the frame minus the theme's normal
  margins (fields 0..3). Every geometry rule keeps it: a theme switch computes the new frame from it with the new theme's margins
  and places the window `caption_h - border_t` (of the new theme) above its top, once; toggling the flag keeps the frame;
  maximise/restore and drags work on the frame. The window height depends only on the active theme (`extendframe -c`, logical
  640x420: 438 rows under aero4074/jade4074, 440 under slate4074, 446 under luna), and its bottom never moves. A client must not
  resize itself on a theme switch to compensate.
- **`_LHWM_CAPTION_RECT`** (CARDINAL[4] `x y w h`, client coordinates, on the client): the union of the caption buttons' hit
  rectangles (msstyles: the button rects exactly; DWM: with `frame_hit_ex`'s 2/3 px slop, clipped to `deco_h` where the theme has
  one), clipped to the client (aero-patch at 640 px: `541, 0, 99, 10`, the buttons reach above the client into the border).
  Written when it changes and on every maximise and restore (also when a drag un-maximises), even unchanged, always after
  `_NET_WM_STATE`, with no intermediate rectangle during a maximise; deleted when the flag goes. A client relayouts its header on
  that PropertyNotify (reading `_NET_WM_STATE` then) and sets its new `_LHWM_EXTEND_FRAME`.
- **The client keeps the rect free**: composited, fully transparent and cut out of its input shape (`XShapeCombineRectangles(...,
  ShapeInput, ...)`), so clicks reach the decoration window below, whose buttons work as always. Not composited the buttons are
  drawn into `_LHWM_HEADER_PIXMAP` and the client keeps the rect free of content (the input-shape hole still passes clicks); with
  `NO_HEADER_PIXMAP` they exist only on the dec window, so a client that does not draw them cuts the rect out of its **bounding**
  shape too and luxwm's buttons, with their hover/pressed states, show through (`extendframe -c -n`; `n` toggles the flag live).
  luxwm paints the full decoration window under such a client (no "opaque client" hole).
- Title text and icon are always the client's. Clicks elsewhere in the caption rows are the client's (`_NET_WM_MOVERESIZE`,
  `_NET_WM_STATE`).
- **Resizing from the top**: (1) luxwm's zone, all `border_t` rows above the client (9 on DWM, 4 on msstyles, 2 on aerovision),
  with the top corners where the frame has its corner zones (DWM: the side border columns; msstyles: one caption height along the
  edge), except where the caption buttons reach into them (`client_hit()` in wm.c on top of `frame_hit_ex`); (2) the client's
  zone: it may start a top resize from its own top rows with `_NET_WM_MOVERESIZE` direction 1 (0/2 for the corners), ignored while
  maximised (`extendframe -c`: its top 4 rows outside the rect, `top_side` cursor; luxnav: `resize_rows`, 3).

## Vision header

The "Aero vision" frame set (`data/frames/aerovision`, theme `aerovision`) frames the concept-art navigation bar that luxnav
draws (docs/NAVBAR.md; reference client `examples/navdemo.c`). Three frame-set keys change what a CLIENT_CAPTION client gets;
sets without them behave (and render) as above:

- **`deco_h`** (`_LHWM_FRAME_METRICS` field 18): the frame's own decoration is the top `deco_h` rows from the frame's outer edge:
  aerovision 15, ending right under its 13-row caption buttons (rows 2..14, with the plate's pill). The buttons' hit rectangles,
  and with them `_LHWM_CAPTION_RECT`, are clipped to it (aero's 3 px slop below the buttons would reach into the client's
  navigation row): aerovision at 720 px `627, 0, 93, 13`. Below `deco_h` the whole width is the client's.
- **`caption_border`**: `border_t` (field 12). aerovision 2, the plate's two outer edge rows, so the client's row 0 is frame row 2
  and it can draw its title with the baseline on frame row 15 inside the deco. Those 2 rows still resize from the top. Maximised 0.
- **`header_mode = crop`**: the Explorer plate (MAXEXPLORER maximised) is never scaled vertically. For a header of H frame rows
  (H = `border_t` + `_LHWM_EXTEND_FRAME` top) the texture's rows `[0, H - 2)` are copied 1:1, its inner light/dark line pair
  (texture rows 78/79) is drawn on rows H - 2 and H - 1, and the client's body starts on row H. A header taller than the band
  (H - 2 > 78) repeats the band's last row. The default `stretch` stretches the band rows [15, 71).

For the 47-row header (luxnav does the arithmetic: `lhnav_height`, `lhnav_extend_top`) the client sets `CLIENT_CAPTION | NO_TITLE
| NO_ICON`, reads `border_t` (field 12, maximised 13) and `deco_h`; its header is `deco_h + 30 + 2 = 47` frame rows, `47 - border_t`
client rows (45, maximised 47), and it asks for 2 more rows of glass for the line pair: `_LHWM_EXTEND_FRAME` top = `47 - border_t +
2` (47; maximised 49). Measured: glass frame rows 0..46, light line 47, black 48, the body from 49. Any other height works the
same way (crop copies the plate 1:1 up to 80 rows): luxnav's big variant is `15 + 54 + 0 = 69` frame rows, top 69 (maximised 71),
lines on 69/70, the body from 71. A client may change the height at any time (navdemo's `p`).

## Theme changes

The session theme is the root property `_LH_THEME` (UTF8_STRING), a theme name; its resolved record is `_LH_THEME_RESOLVED`
(docs/THEME-FORMAT.md section 4: `theme=NAME`, `serial=`, then `section.key=value` lines), current when its `theme=` is that name
and its `serial=` the root's `_LH_SETTINGS_SERIAL`; otherwise the client resolves the name itself with liblhde
(`lhde_theme_resolve`, `<lhde/theme.h>`; `lhde_theme_parse` reads the record). luxwm maps the record to its frame set
(`frames`, `frames_classic`, `composition`) and publishes `_LHWM_THEME`.

A switch is Windows' `WM_THEMECHANGED`: a client treats a PropertyNotify on the root for `_LH_THEME`, `_LH_THEME_RESOLVED`,
`_LHWM_COMPOSITED` or `_LHWM_FRAME_METRICS` as one: take the record and reload the visual style (`path.style`,
`VisualStyle.color`, `VisualStyle.size`), re-read `_LHWM_COMPOSITED` and the metrics, and repaint the header transparent or
opaque, keeping `_LHWM_EXTEND_FRAME` and `_LHWM_FRAME_FLAGS` set. luxwm rewrites `_LHWM_COMPOSITED` and `_LHWM_FRAME_METRICS` on
every `_LH_THEME` change even when the values stay the same, so watching those two alone notices every switch; several
notifications arrive per switch in one burst, so handle them once the queue is empty. Themes with `composition = off` (slate4051,
slate4074, jade4074, the luna and basic* themes, classic) keep `_LHWM_COMPOSITED` at 0 with a compositor running.

## What the client does

1. Create the toplevel with a 32-bit TrueColor visual and its own colormap (`XMatchVisualInfo(..., 32, TrueColor, ...)`,
   `CWColormap|CWBorderPixel|CWBackPixel` as in `examples/glassdemo.c`).
2. Set `_LHWM_EXTEND_FRAME` (e.g. `0 0 60 0`) and `_LHWM_FRAME_FLAGS` (`NO_TITLE`) before mapping; normal WM hints otherwise
   (`WM_NAME`, `_NET_WM_ICON`, `WM_NORMAL_HINTS`, ...).
3. Paint: clear the header to transparent black and draw the content on top with proper alpha (OVER); the rest opaque. When
   `_LHWM_COMPOSITED` is 0, paint the header opaque: `_LHWM_HEADER_PIXMAP` under the content, or a gradient of its own when there is
   none (another WM).
4. Input in the header: "Caption behaviour in the client".
5. Watch the root as in "Theme changes".

`examples/extendframe.c` (Xlib, Xext, cairo, pangocairo, liblhde) does all of it: a 60 px glass header with a big title, an icon
placeholder and a fake breadcrumb over an opaque white body, drag, double-click, right-edge resize, the opaque fallback and the
theme handling. `make`, then `DISPLAY=:9 build/examples/extendframe [-c] [-n]` on any display running luxwm; Escape or q quits.

## Longhorn reference

- With DWM, 4074's Explorer frame is one plate mesh stretched to cover the header; the caption fog is a separate quad behind the
  caption text, and the window title is not drawn in the caption at all: the header shows the folder name in a large font with the
  icon, the breadcrumb bar and the search box (`~/fun/longhorn/screenshots/4074-dwm.png`, Documents window). Measured there (76 px
  header: frame top y=75, light/dark inner lines y=149/150, client from y=151) only the caption texture shows, stretched: the
  apparent opacity rises smoothly from top to bottom, the horizontal profile is that of the aero caption band, and no fog band
  shows outside the ~3 px TextGlow halos (COMPOSITION.md 15: fit with fog 31 levels, without 5.6). Hence `fog = 0`.
- Without DWM (4074 Slate/Jade) the tall header is **not** the caption continued: it is browseui's navigation bar, a comctl32
  rebar at the top of the *client* area, themed with the msstyles classes `Navbar::Rebar`, `InactiveNavbar`, `MaxNavbar`,
  `MaxInactiveNavbar` (chosen in WM_NCACTIVATE from focus and maximise state), `Rebar.SPLITTER` alpha-blended over its last 4 rows,
  under an ordinary uxtheme caption with text and icon hidden; 53 rows (Slate) / 58 (Jade) (COMPOSITION.md 16.3/16.4). So a
  uxtheme client (flxp) draws that band itself from the theme and asks luxwm only for `NO_TITLE | NO_ICON` (the
  `SetWindowThemeAttribute` equivalent) and `NO_HEADER_PIXMAP`; `_LHWM_HEADER_PIXMAP` serves styles without a `Navbar` class
  (Luna/XP) and clients without a uxtheme renderer: within 2 levels of the real band's pixels, but without the splitter, the
  band's last dark row, the real band height and Slate's maximised variant.
- With composition browseui *replaces* the band: `_MIL` controls with the `Composited` sub-app, and the DWM's Explorer plate
  (STREAM 484) shows through. That is what `_LHWM_EXTEND_FRAME` models.

## Desktop windows

luxwm does not manage `_NET_WM_WINDOW_TYPE_DESKTOP` windows (no frame, no focus handling, not in `_NET_CLIENT_LIST`); it keeps
them at the bottom of the stack, under every client, after every restack. Their order among themselves:

| Property | Type | Meaning |
| --- | --- | --- |
| `_LHWM_DESKTOP_LAYER` | CARDINAL (read as signed 32-bit) | Set by the client on its desktop window. Higher layers lie above lower ones; default 0. Changes take effect at once. |

Within one layer the window mapped first stays lowest (map order; an unmap forgets the window, so mapping it again puts it on top
of its layer). luxshell's desktop layer is mapped only in maximised mode, so without a layer it lies above a flxp-desktop running
since login, which is what the black layer wants; an Aurora on luxshell's layer that should sit under flxp-desktop's icons would
set -1 (or flxp-desktop 1). `lhde-session` runs luxshell with `LHSHELL_DESKTOP=0` while flxp-desktop runs.

## Drop shadows

XP's drop shadow under a popup menu (CS_DROPSHADOW; Display Properties > Appearance > Effects..., "Show shadows under menus",
`[effects] menu_shadows`) is drawn by the client, not by the compositor: wux gives a popup menu a window of its own, 5 px wider
and higher than the menu at the menu's top left, stacked right under it, holding only the shadow (`ut_menu_shadow_alpha`, the
same shadow as luxwm's window menu). With a compositor (the `_NET_WM_CM_Sn` selection has an owner) it is ARGB, black with the
shadow's alpha; without one it is a copy of the screen under it, taken before the map and darkened. It takes no input.

| Property | Type | Meaning |
| --- | --- | --- |
| `_LH_DROP_SHADOW` | CARDINAL | on an override-redirect window: 1 = a popup whose drop shadow is its client's own (or none, when the setting is off), 2 = such a shadow window. The compositor adds no shadow of its own and does not blur behind either (`picom-glass.conf`, `picom-glass-fvwm.conf`). |

## Start menu state: `_LH_STARTMENU_OPEN`

CARDINAL, set by the start-menu provider (flxp-taskbar, `xwm::set_start_menu_open`) on the root and on its `_LH_STARTMENU_S<n>`
owner window: 1 while its start panel or classic menu is open (set before it shows), 0 after it closed (0 or deleted = closed).
luxshell draws its Start button pressed (StartButton.FakePressed, the "checked" state) while it is 1, and a forward
(`_LHSHELL_START`) while it is 1 closes the provider's menu. A provider that never sets it: luxshell assumes its menu open from the
forward until the first toplevel mapped within 1.5 s of it unmaps, the next forward, or 1.5 s without a map.

## Raising a window during a drag: `_NET_RESTACK_WINDOW`

EWMH, in `_NET_SUPPORTED`: a client message on a managed client window, `l[0]` source indication, `l[1]` sibling, `l[2]` detail
(Above 0, Below 1, TopIf 2, BottomIf 3, Opposite 4). luxwm restacks client and frame together without touching the focus or
`_NET_ACTIVE_WINDOW`: Above with no sibling raises to the top, Below with none lowers to the bottom of the managed windows; with a
managed sibling on the same desktop directly above / below it; TopIf/BottomIf as Above/Below, Opposite as Above unless the window
is on top already. Minimised windows, windows of other desktops, always-below windows and unknown siblings are ignored. Client
messages are processed whatever pointer grab another client holds (LUXWM.md "Restack requests"). It is meant for an XDND drag
source: while a drag hovers over another toplevel, the source raises it (source 2, Above, no sibling) and the window being
dragged from keeps the focus.

wux's XDND source (`wux/sys/x11.cpp` `hover_raise`) raises the toplevel the pointer rested over (within 8 px) for 1 s, skipping
override-redirect windows and the topmost one, with `_NET_ACTIVE_WINDOW` (source 2) on its client window instead, which also
moves the focus; without a window manager it calls `XRaiseWindow`.

## Not implemented

- wux's drag raise through `_NET_RESTACK_WINDOW` (focus stays with the drag source) instead of `_NET_ACTIVE_WINDOW`, after Windows'
  ~500 ms rather than 1 s.
