# luxwm client protocol: extended glass frames

> **Imported from the Longhorn X11 lab** (`~/fun/longhorn/x11` at d353b69, lhwm/CLIENT-PROTOCOL.md) into lhde in step 3 of docs/LAYOUT.md. Program and path names are updated to lhde (lhwm → luxwm, lhwmc → luxwmc, lhshell → luxshell, lhnav → luxnav; x11/... and lhwm/themes, lhshell/themes, lhshell/assets → the lhde tree and data directories, see docs/LAYOUT.md and tools/lhde-import); the X atoms (`_LH_*`, `_LHWM_*`, `_LHSHELL_*`), the window manager name "lhwm", luxnav's C API (`lhnav_*`, `lhnav.h`), configuration paths (`~/.config/longhorn`, `~/.config/lhwm/keys`, `~/.config/lhshell`) and setting values (`header_engine = lhnav`, `classic_start_menu = lhshell`) keep their names. Dated sections are history: they describe the x11 tree as it was (its compare_*.png and screenshots stay in the research workspace).

How a cooperating client (the flxp Explorer, the Longhorn-style demo) gets the tall glassy
caption that Longhorn's Explorer has in 4074: the frame plate continues down behind the
client's header band (title, breadcrumbs, search box), with DWM blur behind it, and the client's
own content is drawn on top of that glass.

Status: designed and implemented 2026-10-23 (after M5a); "Implementation notes" at the end lists where luxwm
differs from the first draft of this text. Keep this file as the reference for both sides; the demo
`examples/extendframe.c` is the executable version.

## How it works on X11

luxwm is non-reparenting: the decoration is a separate ARGB window (`dec`) that lies directly
**under** the client in the stacking order and is larger than it by the frame margins. picom
composites both. So the trick is cheap:

1. the client tells luxwm how far the frame should extend into its own area (`_LHWM_EXTEND_FRAME`);
2. luxwm paints the plate so that it also covers that area (the caption band stretched taller),
   under the client;
3. the client is a 32-bit ARGB window and paints the extended area **fully transparent**
   (premultiplied `0,0,0,0`) except for its own content (text, icons, widgets), which it draws
   normally on top;
4. picom shows the plate through the client's transparent pixels, blurred background and all.

This is the same contract as Windows' `DwmExtendFrameIntoClientArea` + "paint the area black
with alpha 0". Nothing is reparented, no shared surfaces, no new compositor: the client does not
need to know how the glass is drawn, only *where*.

## Properties (all set by the client on its toplevel unless stated)

| Property | Type | Meaning |
| --- | --- | --- |
| `_LHWM_EXTEND_FRAME` | CARDINAL[4] `left right top bottom` | Extend the frame plate this many client pixels inwards from each client edge (clamped to the client size). `top` is the common one (tall caption). `0xFFFFFFFF` in all four = sheet of glass (the whole client is frame). Set before mapping or at any time; luxwm repaints on change. Delete it to go back to a normal frame. |
| `_LHWM_FRAME_FLAGS` | CARDINAL | bit 0 `NO_TITLE`: luxwm draws no caption text (the client draws its own big title); bit 1 `NO_ICON`; bit 2 `NO_FOG`: (historical; since 2026-10-24 the fog is off unless the theme sets `fog = 1`, because 4074 draws none, COMPOSITION.md 15) skip the caption fog band (`captionfog.png`, Longhorn's CAPTIONFOG part, a soft light band that luxwm draws over the caption row and the extended top of an extended frame so text stays readable). bit 3 `CLIENT_CAPTION` (experimental, "Additions 2026-10-24"); bit 4 `NO_HEADER_PIXMAP` (16): no `_LHWM_HEADER_PIXMAP` for this client ("Second round" below). `NO_FOG` applies to DWM frames while composited; `NO_TITLE` and `NO_ICON` are honoured in every mode since 2026-10-24 (before, they were ignored while `_LHWM_COMPOSITED` was 0). Also without `_LHWM_EXTEND_FRAME`. Changes repaint at once. |
| `_LHWM_SELF_FRAMED` | CARDINAL 1 | Already supported: the client draws the entire frame itself (glassdemo). Not what Explorer wants. |
| `_NET_WM_MOVERESIZE` | client message, to root | Standard EWMH: `l[0] l[1]` = pointer root position of the press, `l[2]` = direction, `l[3]` = button, `l[4]` = source (1). The client sends it on button press in its glass area where it wants caption behaviour: `direction` 8 = move, 0..7 = resize (top-left, top, top-right, right, bottom-right, bottom, bottom-left, left), 9 = size with the keyboard, 10 = move with the keyboard, 11 = cancel. **Call `XUngrabPointer` first** (the press gave the client an implicit grab). luxwm then runs its own drag exactly as if the frame had been pressed (same code: `EDGE_LOCK`, moving a maximised window un-maximises it, no resize while maximised). |
| `_NET_WM_STATE` | client message | Standard (done in M2): toggle `_NET_WM_STATE_MAXIMIZED_VERT/HORZ` on double-click in the client's glass area; `_NET_WM_STATE_HIDDEN` to minimise; `_NET_CLOSE_WINDOW` to close. |
| `_NET_FRAME_EXTENTS` | set by luxwm | Unchanged: the real frame margins outside the client. The extension is inside the client and does not change the client's geometry. |

Root properties the client reads (set by luxwm, PropertyNotify on the root when they change):

| Property | Type | Meaning |
| --- | --- | --- |
| `_LHWM_COMPOSITED` | CARDINAL 0/1 | 1 when luxwm draws DWM glass frames (a compositor is present and the theme is a DWM theme). 0 in classic/msstyles mode or without a compositor. The client should then draw its header opaque (the 4074 Slate Explorer look) and not rely on transparency; luxwm then draws a normal frame (no glass under the client) and uses `_LHWM_EXTEND_FRAME` only to size the `_LHWM_HEADER_PIXMAP` it gives the client. This is the `DwmIsCompositionEnabled` of this protocol. |
| `_LHWM_THEME` | STRING | Path of the active frame theme (M5a). Derived from the session theme `_LH_THEME` since M6 (see "Theme changes"); read-only for clients. |
| `_LHWM_FRAME_METRICS` | CARDINAL[19] | `caption_h border_l border_r border_b text_rgb glow_rgb inactive_text_rgb reserved max_caption_h max_border_l max_border_r max_border_b border_t max_border_t max_text_rgb max_inactive_text_rgb max_glow_rgb reserved deco_h` so a client can lay out its header to match the theme (fonts come from the theme.ini: read it if needed). Fields 0..3 are the normal (not maximised) frame margins, 8..11 the maximised ones, 12/13 the top border kept above a `CLIENT_CAPTION` client (normal/maximised); colours `0xRRGGBB`; `glow_rgb` is the DWM TextGlow colour, or the msstyles caption text-shadow colour (0 without one). Field 18 `deco_h` (2026-10-27, "Vision header" below): the rows at the frame's top that are the frame's own decoration (Aero vision: 15, the caption buttons), else `caption_h`. Fields 8..13 since 2026-10-24 ("Second round"), 14..17 since 2026-10-25 (maximised caption colours, below), 18 since 2026-10-27; before, the property had 8 (then 14, then 18) items, so read up to 19 and treat missing ones as absent. |

luxwm deletes them on a clean exit (`luxwmc quit`); `_LH_THEME` stays (it is the session's, not luxwm's). A client treats a missing `_LHWM_COMPOSITED` as 0 (another WM, or luxwm not running).

## What luxwm does for an extended frame

- Painting: the plate's 9-slice top band (caption) is drawn with height `caption_h + top`, the
  side bands with the extra left/right widths, the bottom band with the extra bottom height (each
  band keeps its outer edge rows and its inner dark/light lines 1:1 and stretches the part between,
  so the inner lines move inwards to where the client's own content starts); the overlay
  (highlight) follows the outer edge as before; the caption fog is drawn as one band over the
  caption row and the extended top (`caption_h + top` rows, between the side borders) unless
  `NO_FOG`. Buttons and title stay where they are (title omitted with `NO_TITLE`). The dec window
  keeps its outer geometry; only what is painted under the client changes, so `_NET_FRAME_EXTENTS`
  and the shape region are unchanged (the shape only trims the outer corners, never the extended area).
- Hit-testing: unchanged (the dec window gets no events under the client anyway). Caption
  behaviour inside the client is the client's job via `_NET_WM_MOVERESIZE`.
- picom: clients with `_LHWM_EXTEND_FRAME` are excluded from `blur-background` in
  `picom-glass.conf` (rule `"_LHWM_EXTEND_FRAME@"`), so the only blur is the dec window's; otherwise
  the client's transparent header would blur the plate under it a second time. They are also
  excluded from `shadow`: the dec window casts the frame's shadow, and the client's own shadow showed
  as a dark seam across the glass along the client's top edge.
- Without composition (`_LHWM_COMPOSITED` = 0): ignore the extension; the client's ARGB alpha is
  shown as black by the X server, so the client must paint opaque there. The demo does this.
- Maximised: the extension still applies (Explorer keeps its tall header when maximised); the
  M5a square-corner rules apply to the plate.

## What the client does (checklist for flxp)

1. Create the toplevel with a 32-bit TrueColor visual and its own colormap (`XMatchVisualInfo(...,
   32, TrueColor, ...)`, `CWColormap|CWBorderPixel|CWBackPixel` as in `examples/glassdemo.c`).
2. Set `_LHWM_EXTEND_FRAME` (e.g. `0 0 60 0`) and `_LHWM_FRAME_FLAGS` (`NO_TITLE`) before
   mapping. Normal WM hints otherwise (`WM_NAME`, `_NET_WM_ICON`, `WM_NORMAL_HINTS`, ...).
3. Paint: clear the header band to transparent black, draw the content on top with proper alpha
   (cairo `OPERATOR_OVER`); paint the rest of the window opaque as usual. When
   `_LHWM_COMPOSITED` is 0 paint the header opaque instead: `_LHWM_HEADER_PIXMAP` under the content (below), or a
   gradient of your own when it is missing (another WM).
4. Input in the header: on ButtonPress 1 where nothing interactive is hit, send
   `_NET_WM_MOVERESIZE` (move); on double-click send `_NET_WM_STATE` toggle maximised. Right-click
   can open the window menu later (`_LHWM_WINDOW_MENU`, not yet).
5. Watch the root for `_LH_THEME`, `_LHWM_COMPOSITED` and `_LHWM_FRAME_METRICS` changes and handle them as a theme change
   (next section).

The reference example is `examples/extendframe.c` (Xlib + cairo + pangocairo, ~400 lines; `-c` CLIENT_CAPTION, `-n` NO_HEADER_PIXMAP): a
window with a 60 px glass header holding a big title, an icon placeholder and a fake breadcrumb, an
opaque white body, drag, double-click and right-edge resize handling, and the opaque fallback. Build
and run it with `make && DISPLAY=:1 build/examples/extendframe` (any display running
luxwm; the lab's). Escape or q quits.

## Theme changes

The session theme is the root property `_LH_THEME` (UTF8_STRING), a theme name; its resolved record is `_LH_THEME_RESOLVED`
(docs/THEME-FORMAT.md section 4: `theme=NAME`, `serial=`, then `section.key=value` lines), current when its `theme=` is that
name and its `serial=` the root's `_LH_SETTINGS_SERIAL`; otherwise the client resolves the name itself with liblhde
(`lhde_theme_resolve`, `<lhde/theme.h>`; `lhde_theme_parse` reads the record). A theme switch is Windows' `WM_THEMECHANGED`, and
a client treats a PropertyNotify on the root for `_LH_THEME`, `_LH_THEME_RESOLVED`, `_LHWM_COMPOSITED` or `_LHWM_FRAME_METRICS`
as one: take the record and reload the visual style (`path.style`, `VisualStyle.color`, `VisualStyle.size`), re-read `_LHWM_COMPOSITED`, and repaint
the header transparent or opaque. Keep `_LHWM_EXTEND_FRAME` and `_LHWM_FRAME_FLAGS` set in both states (since 2026-10-24: while
it is 0 they ask for `_LHWM_HEADER_PIXMAP`); deleting them while it is 0, as this section said before, still gives an ordinary
framed window with title and icon. luxwm rewrites `_LHWM_COMPOSITED` and `_LHWM_FRAME_METRICS` on every `_LH_THEME` change even when the
values stay the same, so watching those two alone is enough to notice every switch; several notifications arrive per switch,
in one burst, so handle them once the queue is empty. A theme with `composition = off` (slate4074, slate4051,
jade4074-classic) keeps `_LHWM_COMPOSITED` at 0 with a compositor running: decide glass from `_LHWM_COMPOSITED`, never from
the theme name. `examples/extendframe.c` (rule 6) does this and shows the theme and msstyles it would load; verified on the lab:
aero4074 → luna paints the header from `_LHWM_HEADER_PIXMAP` (the property appears), back to aero4074 the property is deleted
and the header is glass again.

## Longhorn notes

In 4074 with DWM the Explorer frame is one plate mesh stretched to cover the header; the
caption fog (`CAPTIONFOG`, imageres/jade part 505) is a separate quad behind the caption text,
and the window title is not drawn in the caption at all: the header shows the folder name in a
large font with the icon, the breadcrumb bar and the search box (see `screenshots/4074-dwm.png`,
Documents window). In the non-composited Slate build the same header is drawn opaquely by Explorer
inside a normal frame (`screenshots/lh4074-18*.png`). The protocol above reproduces both cases
from the same client code via `_LHWM_COMPOSITED`.

## Implementation notes (2026-10-23)

Where luxwm (`frame.c` `plate_ext`/`fog_band`, `wm.c` `drag_run`/`composited_publish`) differs from or adds to the text above:

- Which caption rows stretch is a theme key, `extend_rows = r0 r1` (default `2 caption_h-2`: everything between the outer
  highlight rows and the inner lines). aero uses `15 25`: its plate has the caption-button pill baked into rows 2..14 of the
  right-hand 100 columns, and stretching those rows grew the pill into a tall well under the buttons. So on aero the top 15
  caption rows stay 1:1 and only the lower part of the caption gradient continues down behind the header. In the Longhorn
  frame the buttons are separate meshes, so there the whole band stretches. The side and bottom bands stretch their
  interior columns/rows (2 .. margin-2) the same way.
- Since 2026-10-24 themes with a `plate_explorer` (aero, jade) paint a top extension with the Explorer plate instead (Longhorn's
  `LgFrameExplorer`, STREAM 484: a 73-row caption band, README "Explorer plate"): rows 0..14 1:1, the band stretched to the header
  height, the texture's inner lines on the header's last two rows; a 46 px extension copies the band 1:1. `extend_rows` then only
  applies to themes without one. Left/right/bottom extensions and the sheet of glass are unchanged.
- The vertical inner lines of the side bands start below the header. In the header rows the sides come from the caption
  band's corner columns, as on the reference.
- Sheet of glass: the caption gradient stretched over the whole client, and none of the four bands draws its inner lines.
  No fog.
- Fog band: x from the left border to the right border (also under the caption buttons, which are painted over it), y from
  0 to `caption_h + top`, the 99x26 `captionfog.png` 3-sliced (10 px fades kept 1:1, middle stretched; its 26-row profile
  stretched to the band height). In the lab it brightens the middle of a 60 px header by up to ~16/255. Measured on the
  reference (`screenshots/4074-dwm.png` Documents window, against the Longhorn wallpaper, which is
  `mock/assets/wallpaper/longhorn.jpg` stretched to 1280x1024 there; RMSE 0.9 % where the desktop is visible), the 76 px
  header (frame top y=75, light/dark inner lines at y=149/150, client from y=151) shows only the caption texture stretched:
  the apparent opacity rises smoothly from top to bottom, and the horizontal profile (opaque left 130 px, a step, a clear
  middle, more opaque again past x~500) is exactly that of the aero caption band. No separate fog band shows outside the
  ~3 px TextGlow halos of the title and breadcrumb. Clients that want that exact look set `NO_FOG`. `compare_extend.png`
  shows the reference, luxwm with fog, and luxwm with `NO_FOG`.
- `_NET_WM_MOVERESIZE`: 9 is EWMH's SIZE_KEYBOARD and 10 its MOVE_KEYBOARD (the first draft said 9 = keyboard move). In
  keyboard mode luxwm grabs pointer and keyboard and warps the pointer to the window centre (move) or its bottom-right corner
  (size). The arrows move it 10 px, Return/Enter/space or a click ends, Escape cancels. A mouse drag ends on the release of
  `l[3]` (any button if 0). If the button is already up when luxwm grabs (a quick click), nothing happens. A second
  `_NET_WM_MOVERESIZE` during a drag ends it, and 11 cancels it: the window goes back to where it was, and a window that
  was maximised is maximised again. If the client still holds its implicit grab, luxwm retries the grab for 100 ms. luxwm
  has no drag snapping of its own: the rules that apply are `EDGE_LOCK` and the maximised rules.
- During a mouse drag only the pointer is grabbed, so Escape still goes to the focused client (the example quits on it).
- `_LHWM_COMPOSITED` is `compositor present && theme mode = dwm`. It changes with the compositor (XFixes) and with
  runtime theme switches (`_LH_THEME`, or `_LHWM_THEME` written directly), and every client with the properties is repainted
  then. On a switch it is rewritten even when unchanged (M6). If someone deletes it, luxwm writes it again.

## Desktop windows (`_NET_WM_WINDOW_TYPE_DESKTOP`)

luxwm does not manage desktop windows (no frame, no focus handling, not in `_NET_CLIENT_LIST`); it keeps them at the
bottom of the stack, under every client, after every restack. When there are several (flxp-desktop, luxshell's desktop
layer), their order is:

| Property | Type | Meaning |
| --- | --- | --- |
| `_LHWM_DESKTOP_LAYER` | CARDINAL (read as signed 32-bit) | Set by the client on its desktop window. Higher layers lie above lower ones; default 0. Changes take effect at once. |

Within one layer the window mapped first stays lowest (map order; an unmap forgets the window, so mapping it again puts
it on top of its layer). luxshell's desktop layer is mapped only in maximised mode, so without a layer it ends up above a
flxp-desktop that has been running since login, which is what the black layer wants. A future Aurora on luxshell's layer
that should sit under flxp-desktop's icons would set `_LHWM_DESKTOP_LAYER` -1 (or flxp-desktop 1). In the lab session
the question does not arise: `lhde-session` runs luxshell with `LHSHELL_DESKTOP=0` while flxp-desktop runs.

## Additions 2026-10-24 (requests from the flxp toolkit side)

### Header background without composition: `_LHWM_HEADER_PIXMAP`
When `_LHWM_COMPOSITED` is 0, a client that set `_LHWM_EXTEND_FRAME` cannot show glass, and
reconstructing the theme's caption band from the msstyles on its own is fragile. luxwm therefore
renders the band itself: for every client carrying `_LHWM_EXTEND_FRAME` while not composited,
luxwm keeps a PIXMAP of the client's visual/depth, `client width x (caption_h + top)` (the full
header: the caption band as the frame draws it, continued down over the extension with the
caption bitmap's MAXCAPTION/normal stretching rules, inactive/active variant following focus),
and publishes its id as `_LHWM_HEADER_PIXMAP` (PIXMAP) on the client, re-rendering on focus
change, resize, theme change (new id each time, old one freed after the property is replaced).
The client copies it under its header content (XCopyArea / cairo xlib surface) and treats a
PropertyNotify of `_LHWM_HEADER_PIXMAP` like an Expose of the header. When composition is on the
property is absent. `_LHWM_FRAME_METRICS` keeps the caption colours for clients that prefer to
draw their own band.

### `NO_TITLE` / `NO_ICON` in msstyles mode
Honoured in every mode, not only when composited (an opaque header carries its own title).

### Client content over the caption rows: `CLIENT_CAPTION` (bit 3 of `_LHWM_FRAME_FLAGS`)
Longhorn's Explorer puts its header (folder icon, big title, breadcrumb) in the caption rows
too, with only the caption buttons left to the frame. With `CLIENT_CAPTION` set, luxwm places the
client so that its top edge is at the frame's top inner line: the client's window grows upward
by `caption_h` (luxwm resizes it; `_NET_FRAME_EXTENTS` top becomes the outer border only), the
frame's caption band is painted under it as with an extended frame, and luxwm publishes
`_LHWM_CAPTION_RECT` (CARDINAL[4] x y w h, client coordinates) = the caption-button area that the
client must keep fully transparent (composited) and exclude from its input shape
(`XShapeCombineRectangles(..., ShapeInput, ...)` with the rest of the window) so that clicks reach
the decoration window below. Without composition the buttons are instead drawn by luxwm into the
`_LHWM_HEADER_PIXMAP` and the client keeps the rect free of content; clicks on it still go
through the input-shape hole. Title text and icon are then always the client's (`NO_TITLE` and
`NO_ICON` implied). Exact placement of the client's framelets inside the caption is the
client's business; the flxp polish agent will state what it needs and this section is updated
then. Status: experimental until flxp has used it.

### XP visual styles as frame themes
The msstyles frame mode and `tools/msstyles2theme.py` accept XP-format styles too (Luna and its
colour schemes), so a `[luna]` registry entry gives Luna frames under luxwm like any other theme.

### Implementation notes (2026-10-24)

Where luxwm (`wm.c` `margins_for`/`draw_text`/`header_update`, `frame.c` `ms_paint`/`frame_header_image`/`frame_buttons_rect`)
differs from or adds to the text above:

- `_LHWM_HEADER_PIXMAP`, size and alignment: width = the client's width; height = the current top frame margin (=
  `_NET_FRAME_EXTENTS` top: `caption_h` of `_LHWM_FRAME_METRICS` normally, `CAPTIONBARHEIGHT + 1` when maximised, the border
  with `CLIENT_CAPTION`) + the extension's `top` (clamped to the client; sheet of glass = the whole client height). In the
  normal case that is the `caption_h + top` above. Row 0 is the frame's outer top edge, the columns are the client's, so **the
  last row lies against the bottom of the header: copy the last `top` rows** (`src_y = pixmap_height - top`). Only `top`
  shapes it; `left`/`right`/`bottom` extensions are not rendered into it.
- Contents: the caption bitmap (MAXCAPTION when maximised) nine-gridded with its own SIZINGMARGINS over `caption + top` rows,
  active or inactive by focus; never title or icon. The caption buttons are drawn where the frame has them; only with
  `CLIENT_CAPTION` do they fall into rows the client copies, and only then is the pixmap re-rendered for their hover/pressed
  states. Depth and visual are the client's; a depth-24 client gets the (few) transparent corner pixels flattened onto black.
- It is published whenever `_LHWM_COMPOSITED` is 0, which includes `composition = off` themes with picom running, and only for
  clients with `_LHWM_EXTEND_FRAME` or `CLIENT_CAPTION` (and, since the second round, without `NO_HEADER_PIXMAP`). Re-rendered on focus, resize, maximise/restore, theme switch,
  extension/flag change (new id each time, property replaced, then the old pixmap freed). Deleted when composition comes, when
  the extension and flag go, when the window loses its frame, and on a clean exit.
- The old pixmap is freed right after the property changes, so a client copying from an id it read a moment earlier can get
  BadDrawable/BadPixmap: install an error handler that ignores those (extendframe does) and repaint on the PropertyNotify.
- The decoration window itself still shows a normal frame while not composited (the extension changes nothing there), and
  `NO_TITLE`/`NO_ICON` leave its caption empty. A client that keeps `NO_TITLE` therefore has no title in the frame caption in the
  Slate/Luna look; it must draw one in its header (the 4074 Explorer does).
- `CLIENT_CAPTION`: the border kept above the client is cnBorders (3 + SIZINGBORDERWIDTH, 4 px for every shipped msstyles
  theme) for msstyles frames, the side border width (`margins` left, 9 px) for DWM frames, 0 when maximised; that is
  `_NET_FRAME_EXTENTS` top. "Grows upward by `caption_h`" is therefore by `caption_h - border`. A window mapped with the flag
  gets the frame a normal window would have, its client taller; setting or clearing it later keeps the frame and resizes the
  client; after a WM restart the frame is put around the client as it is. `_LHWM_EXTEND_FRAME` top still counts from the client's
  top, which now includes the caption rows (extendframe -c: buttons' bottom + 2 px, at least 18, plus its 60 px header).
- `_LHWM_CAPTION_RECT` is the union of the buttons' hit rectangles in client coordinates (msstyles: the button rects exactly;
  DWM: with `frame_hit_ex`'s 2/3 px slop), clipped to the client (aero: 541,0 99x10, the buttons reach above the client's top
  into the border), written when it changes, deleted when the flag goes. Title text and icon are never drawn by luxwm for such a
  client. Hit-testing is unchanged: clicks through the input-shape hole reach the decoration window, whose buttons work as
  always; clicks elsewhere in the caption rows are the client's (`_NET_WM_MOVERESIZE`, `_NET_WM_STATE`). Resizing from the top
  edge only works in the border rows above the client (all of them since the second round, below, plus a client-started top
  resize). Still experimental: nothing from flxp has used it yet.
- XP visual styles: `tools/msstyles2theme.py` reads XP (PACKTHEM_VERSION 3) and Longhorn-compiled (4) `.msstyles` directly;
  the registry has `luna`, `luna-olive`, `luna-silver` (`docs/history/THEMES.md`). The `msstyles` key points at Longhorn 4051's Luna
  (version 4); `flxp_msstyles` at a real XP Luna (version 3) for flxp's uxtheme.

### Fog default changed (2026-10-24)
The research in COMPOSITION.md 15 found no fog band in 4074's Explorer header (the fit with fog: 31 levels, without: 5.6),
so luxwm no longer draws `captionfog.png` unless the frame theme sets `fog = 1`. `NO_FOG` still works but is a no-op
with the shipped themes. The glass itself now uses the 5048 equation (`glass_alpha_mode = square`, `glass_alpha = 1.0`).

### What 4074 really does without composition (2026-10-24, COMPOSITION.md §16)
The tall Explorer header in 4074 Slate/Jade without DWM is **not** the caption continued: it is
browseui's navigation bar, a comctl32 rebar at the top of the *client* area, themed with the
msstyles classes `Navbar::Rebar`, `InactiveNavbar`, `MaxNavbar`, `MaxInactiveNavbar` (chosen in
WM_NCACTIVATE from focus and maximise state), with `Rebar.SPLITTER` alpha-blended over its last
4 rows, under an ordinary uxtheme caption whose text and icon are hidden. Band height 53 (Slate)
/ 58 (Jade) rows; the full layout, fonts and colours are in COMPOSITION.md 16.3/16.4.
Consequences for this protocol:
- A uxtheme-capable client (flxp) should draw that band itself from the theme, exactly as
  browseui does, and only ask luxwm for `NO_TITLE | NO_ICON` (the `SetWindowThemeAttribute`
  equivalent). `_LHWM_HEADER_PIXMAP` stays for styles without a `Navbar` class (Luna/XP) and
  for clients without a uxtheme renderer; it is within 2 levels of the real band's pixels but
  lacks the splitter, the band's last dark row, the real band height and Slate's maximised
  variant.
- With composition the band is *replaced*: browseui switches to `_MIL` controls with the
  `Composited` sub-app, and the DWM's Explorer plate (STREAM 484) shows through. That is what
  `_LHWM_EXTEND_FRAME` models, so the composited half of this protocol stands.

## Second round (2026-10-24, flxp requests)

### Theme switches keep the logical client rectangle (CLIENT_CAPTION height drift)
A `CLIENT_CAPTION` client has a *logical* rectangle, its window without the caption rows luxwm grew it by: the frame minus the
theme's normal margins (`_LHWM_FRAME_METRICS` 0..3). Every geometry rule now keeps that rectangle: a theme switch computes the
new frame from it with the new theme's normal margins and then places the client window `caption_h - border_t` (of the new
theme) above its top, once; toggling the flag keeps the frame and changes only the window (as before); maximise/restore and
drags work on the frame. So the window height depends only on the active theme and the logical rectangle never moves:
`extendframe -c` (logical 640x420) is 438 rows high under aero4074/jade4074, 440 under slate4074, 446 under luna, after any
number of switches, and its window bottom never moves.

Root cause of the drift flxp measured (418, 398, 416, 390, 408): `client_retheme` kept the client *window* rectangle, caption
rows included (old margins from the old theme, new from the new, each with `CLIENT_CAPTION` applied: consistent in itself, but
it kept the wrong rectangle), whereas the flag-toggle path keeps the frame, i.e. the logical rectangle. A client that clears
`CLIENT_CAPTION` in its classic styles and sets it in its glass ones mixes the two: each switch kept the window height, then the
toggle grew or shrank it by the *new* theme's `caption_h - border_t` (aero/jade 18, slate 20, luna 26), so growing by 18 and
shrinking by 20 or 26 never cancelled. Reproduced on the lab with exactly those numbers (xclock with the flag toggled by xprop
around switches aero4074 → slate4074 → jade4074 → luna → aero4074); after the fix the same sequence gives 418 / 400 / 418 /
400 / 418 and the logical rectangle stays at the same place.

A client must not resize itself on a theme switch to compensate; luxwm does it.

### Maximised metrics and the caption rect on maximise/restore
`_LHWM_FRAME_METRICS` gained six items (appended; items 0..7 unchanged):

| # | Field | Meaning |
| --- | --- | --- |
| 8 | `max_caption_h` | top frame margin of a maximised window: DWM `caption_h` (M5a: the caption stays), msstyles `CAPTIONBARHEIGHT + 1` (the MAXCAPTION band) |
| 9..11 | `max_border_l max_border_r max_border_b` | maximised side/bottom margins: 0 for every theme (DWM drops them, msstyles pushes them off-screen) |
| 12 | `border_t` | the top border kept above a `CLIENT_CAPTION` client (= its `_NET_FRAME_EXTENTS` top): msstyles cnBorders, DWM the side border width |
| 13 | `max_border_t` | the same while maximised: 0 |
| 14 | `max_text_rgb` | caption text colour of the focused maximised window (theme.ini `text_max_active`; the normal `text_rgb` when the theme sets none), `0xRRGGBB`. Since 2026-10-25 |
| 15 | `max_inactive_text_rgb` | the same unfocused (`text_max_inactive`; default `inactive_text_rgb`). Since 2026-10-25 |
| 16 | `max_glow_rgb` | the TextGlow colour while maximised (`glow_max`; default `glow_rgb`), `0xFFFFFFFF` = no glow (`glow_max = none`: aero, jade); msstyles: MAXCAPTION's text-shadow colour, else `glow_rgb`. Since 2026-10-25 |
| 17 | reserved | 0 |

Fields 14..17 (2026-10-25, flxp request: the Explorer header flips its text black/white when zoomed) come from the current frame
theme and are republished with the rest on every theme switch. Shipped values: aero4074 / jade4074 `0xffffff 0x626469 0xffffffff 0`.

The caption rows of a `CLIENT_CAPTION` client are `caption_h - border_t` normally and `max_caption_h - max_border_t` while
maximised (`_NET_WM_STATE` has both `MAXIMIZED_VERT` and `MAXIMIZED_HORZ`): aero/jade 18 / 27, slate4074 20 / 20, luna 26 / 26.
luxwm republishes `_LHWM_CAPTION_RECT` on every maximise and restore (also when a drag un-maximises), even when the rectangle
did not change, and always after `_NET_WM_STATE`; so a client relayouts its header on that PropertyNotify (reading
`_NET_WM_STATE` then) and sets its new `_LHWM_EXTEND_FRAME`. No intermediate rectangle is published during a maximise. Values
for the shipped themes: aero4074 `27 9 9 9 ... 27 0 0 0 9 0`, slate4074 `24 4 4 4 ... 20 0 0 0 4 0`, luna `30 4 4 4 ... 26 0 0 0 4 0`.

### Resizing a CLIENT_CAPTION window from the top
Decision: two ways, both kept.
- luxwm's own zone: the frame's border rows above the client, **all** of them (`border_t` rows: 9 on DWM, 4 on msstyles) resize
  from the top, with the top corners where the frame has its corner zones (DWM: the side border columns, msstyles: one caption
  height along the edge); except where the caption buttons reach into them. Before, DWM frames resized only in the outer 6 rows
  and rows 6..8 were a 3-row move strip (the normal frame's hit test, which counts everything below row 6 as caption). This is
  `client_hit()` in wm.c on top of `frame_hit_ex`; `frame_hit_ex` itself (and frametest's hit columns) is unchanged. The zone is
  not shrunk by the client: the client lies over the frame only from `border_t` down, and the decoration window gets no events
  under it anyway.
- the client's own zone: the client may start a top resize from its header with `_NET_WM_MOVERESIZE` direction 1 (TOP; 0/2 for
  the corners), exactly as with the other directions (ungrab first; ignored while maximised). `extendframe -c` does this in its top
  4 rows outside `_LHWM_CAPTION_RECT`, with the `top_side` cursor. Checked on the lab (aero4074): a press at client row 1 dragged
  up 50 gives frame y -50 / height +50; frame row 7 (formerly the move strip) and row 1 dragged up resize; a press in the caption
  zone below still moves.

### `NO_HEADER_PIXMAP` (bit 4 of `_LHWM_FRAME_FLAGS`, 16)
luxwm renders and publishes no `_LHWM_HEADER_PIXMAP` for a client with this bit, whatever `_LHWM_COMPOSITED` is. For a client that
draws its header itself (flxp: its navbar for Slate/Jade/Aero) and wants the pixmap only for Luna/XP styles. It may be set and
cleared at any time (e.g. on a theme switch): set, luxwm deletes the property and frees the pixmap at once; cleared, a new pixmap
is rendered and published (if the other conditions hold). The usual BadPixmap rule applies to a copy in flight. It changes
nothing else: the frame, the extension, `NO_TITLE`/`NO_ICON` and `_LHWM_CAPTION_RECT` are as without it.
With `CLIENT_CAPTION` and no composition the caption buttons were drawn only in the header pixmap (as far as the client's
window is concerned); without it they exist only on luxwm's decoration window under the client, so a client that does not draw
them itself cuts `_LHWM_CAPTION_RECT` out of its **bounding** shape too (not only the input shape) and luxwm's buttons, with
their hover/pressed states, show through. `extendframe -c -n` does that (`n` toggles the flag live); luxwm paints the full
decoration window under a `CLIENT_CAPTION` client for this (no "opaque client" hole).

## Vision header (2026-10-27, `NAVBAR.md`)

The "Aero vision" frame theme (`themes/aerovision`, registry `[aerovision]`) is the frame for the concept-art navigation bar that
`../lhnav` draws (reference client `../examples/navdemo.c`). Three theme keys change what a `CLIENT_CAPTION` client gets; every
other theme keeps its old behaviour (and renders byte-identically):

- **`deco_h`** (`_LHWM_FRAME_METRICS` field 18). The frame's own decoration is the top `deco_h` rows from the frame's outer edge:
  aerovision 15, ending right under the caption buttons (13 rows high there, rows 2..14, with the plate's pill). The buttons' hit
  rectangles, and with them `_LHWM_CAPTION_RECT`, are clipped to it (aero's 3 px hit slop below the buttons would reach into the
  client's navigation row): aerovision at 720 px client width `627, 0, 93, 13`. Below `deco_h` the whole width is the client's.
  Themes without the key publish `caption_h` there.
- **`caption_border`**: the rows kept above a `CLIENT_CAPTION` client (= `border_t`, field 12; `_NET_FRAME_EXTENTS` top). Default
  the side border (9 on DWM themes, cnBorders on msstyles); aerovision 2, the plate's two outer edge rows, so the client's row 0 is
  frame row 2 and it can draw its title with the baseline on frame row 15 inside the deco. Those 2 rows still resize from the top;
  a client may start a top resize from its own top rows with `_NET_WM_MOVERESIZE` direction 1 (navdemo: 3 rows). Maximised: 0, as
  before.
- **`header_mode = crop`**: the Explorer plate (and MAXEXPLORER when maximised) is never scaled vertically. For a header of H frame
  rows (H = `border_t` + `_LHWM_EXTEND_FRAME` top, i.e. the extension still counts from the client's top) the texture's rows
  `[0, H - 2)` are copied 1:1, cropped there, and its inner light/dark line pair (texture rows 78/79) is drawn on rows H - 2 and
  H - 1, exactly where the stretched band put them; the client's body starts on row H either way. A header taller than the band
  (H - 2 > 78) repeats the band's last row. The default `stretch` stretches the band rows [15, 71) as before.

What a client does for the 47-row vision header (luxnav does the arithmetic: `lhnav_height`, `lhnav_extend_top`):
- set `CLIENT_CAPTION | NO_TITLE | NO_ICON`; read `border_t` (field 12, maximised 13) and `deco_h` (field 18);
- its header is `deco_h + 30 + 2 = 47` frame rows (the deco, the orb row, 2 rows of spacing), i.e. `47 - border_t` client rows
  (45 under aerovision, 47 maximised), and it asks for 2 more rows of glass for the line pair: `_LHWM_EXTEND_FRAME` top =
  `47 - border_t + 2` (aerovision: 47; maximised 49). luxwm draws the frame's light line on frame row 47 and the black line on 48
  (measured on the lab: glass rows 0..46, light 47, black 48, the body from 49);
- keep `_LHWM_CAPTION_RECT` transparent and out of its input shape, as before; everything else below row `deco_h` is its own.
- any other header height works the same way (`header_mode = crop` copies the plate 1:1 down to the line pair, up to 80 rows):
  luxnav's big variant (nav.ini `variant = big`, `NAVBAR.md` "Presets") is `15 + 54 + 0 = 69` frame rows, `_LHWM_EXTEND_FRAME`
  top 69 (maximised 71), lines on frame rows 69/70, the body from 71. A client may change the height at any time (navdemo's `p`).

## Requests to the wux side (2026-10-27)
- **Start menu state.** The start-menu provider (flxp-taskbar) sets root `_LH_STARTMENU_OPEN`
  (CARDINAL 1 while its menu is mapped, 0 or deleted when closed). lux draws its Avalon Start
  button pressed while it is 1 (aap's todo: "lux start button does not know when the wux start menu
  is open").
- **Drag and drop raise.** While an XDND drag hovers over another toplevel for ~500 ms (Windows'
  behaviour), the drag source (flxp) asks luxwm to raise that window without moving focus:
  `_NET_RESTACK_WINDOW` (source indication 2, detail Above, no sibling) on the target's client
  window. luxwm honours it during drags (implemented on the luxwm side 2026-10-27: `_NET_RESTACK_WINDOW` is in `_NET_SUPPORTED`;
  Above/Below with or without a managed sibling, client and frame together, focus untouched, also while the source holds its
  active pointer grab; README "Restack requests").
