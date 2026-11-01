# luxnav: the Longhorn navigation bar, and the vision header

The Explorer title/navigation area as the Longhorn concept art drew it (`/u/aap/chaos/img/windows_concepts/lh_concepts/`, chiefly
`1456384117.or.45878.png`), for the experimental themes: **Aero vision** (`aerovision`) and the Vista-era themes whose
`Explorer.navbar` is `vision` (aero5048, aero5112, basic5048, basic5112). This is taste, a "vision" theme: the faithful 4051/4074
headers are flxp's own and stay as they are. luxnav is the library that lays out, paints and hit-tests it; its C prefix `lhnav_`
and header `lhnav.h` are stable.

## Who owns what

| Piece | Where | Why |
| --- | --- | --- |
| Frame: cropped header plate, 15 px top deco, vision caption buttons | luxwm (`data/frames/aerovision`, `frame.c`; CLIENT-PROTOCOL.md "Vision header") | it is decoration |
| Back/forward orb, breadcrumb, address and search box rects, toolbar, title text: **layout, painting, hit-testing, visual state** | **luxnav** (`luxnav/`), plain C, toolkit-independent | iconic Longhorn component; must look identical in every client |
| Behaviour: history, the path model, what a click does, the address and search text fields, the drop-down lists | the client (flxp's header, navdemo) | real functionality belongs to the explorer |
| Reference integration | `examples/navdemo.c` | a small file browser with the vision header |

luxnav follows wuxtheme's conventions: no window system; it draws into 32-bit premultiplied ARGB buffers (`uint32_t *pixels, w,
h, stride`, native byte order: cairo's ARGB32, a 32-bit DIB section's), compositing OVER what is there (glass: transparent;
opaque: the header pixmap). Art comes from the visual style through wuxtheme (`ut_get_stream`/`ut_get_bitmap`/
`ut_draw_background`), from shell32.dll where the style has none, else it is drawn. All text is wuxtheme's GDI text, as flxp's.

## Building

`luxnav/luxnav.mk`, included by the top-level Makefile (`make`):

- `build/lib/libluxnav.a`: linked into every flxp program (the explorer header's vision mode is always built in) and navdemo;
- `build/lib/libluxnav.so.1` (+ `.so`): for other programs; links `-lwuxtheme` (rpath `$ORIGIN`), libpng and libm;
- `build/lib/pkgconfig/luxnav.pc` (`Requires: wuxtheme`, `Requires.private: libpng`); `make install` installs
  `lib/libluxnav.so*`, `include/luxnav/lhnav.h` and the installed `luxnav.pc`.

Sources `lhnav.c` (layout, painting, state), `lhnav_px.c` (pixels), `lhnav_res.c` (shell32's resources), compiled with
`-DHAVE_UXTHEME` and `LUX_COPT`; the library carries `liblhde/data.o` (nav.ini's `icon_dir` and `art_style` are found in lhde's
data directories). No cairo, pango, fontconfig or freetype of its own (FreeType is wuxtheme's). navdemo links libluxnav.a,
liblhde and libwuxtheme, and keeps cairo, pango and fontconfig for its own window.

## API

`luxnav/lhnav.h` has the full comments. `lhnav` is the opaque handle.

```c
void   lhnav_metrics_default(lhnav_metrics *m);                    /* the geometry below */
int    lhnav_metrics_load(lhnav_metrics *m, const char *path);     /* nav.ini, its own preset */
int    lhnav_metrics_load_preset(lhnav_metrics *m, const char *path, const char *preset);
int    lhnav_presets(const char *path, char (*names)[48], int max);
void   lhnav_add_font_dir(const char *dir);                        /* ut_add_font_dir: the theme's `fonts` (Segoe UI 0.7) */
lhnav *lhnav_new(struct ut_theme *style, const lhnav_metrics *m);  void lhnav_free(lhnav *);
void   lhnav_set_metrics(lhnav *, const lhnav_metrics *);          /* border_t, caption rect, ... from luxwm; rebuilds the toolbar */
void   lhnav_set_style(lhnav *, struct ut_theme *);                /* the style stays open while lhnav uses it */
void   lhnav_set_style_name(lhnav *, const char *name);            /* for the log and lhnav_art_sources */
const char *lhnav_art_sources(lhnav *);                            /* where each piece of art came from */
void   lhnav_set_title(lhnav *, const char *utf8);                 /* NULL: no title */
void   lhnav_set_path(lhnav *, const char *const *segments, int n);
void   lhnav_set_history(lhnav *, int can_back, int can_fwd);
void   lhnav_set_frame_state(lhnav *, int focused, int maximized); /* Inactive/Max/MaxInactive travel art */
void   lhnav_set_field(lhnav *, int part, const char *utf8, int caret, int editing);  /* paints the client's edit fields */
void   lhnav_layout(lhnav *, int width);                           /* after the setters and on every resize */
int    lhnav_height(lhnav *);                                      /* client rows: header_h - border_t */
int    lhnav_extend_top(lhnav *);                                  /* + `lines`: the _LHWM_EXTEND_FRAME top */
void   lhnav_paint(lhnav *, uint32_t *px, int w, int h, int stride, int composited);
int    lhnav_hit(lhnav *, int x, int y);
int    lhnav_pointer(lhnav *, int x, int y, int buttons);         /* 1 = repaint; (-1, -1, 0) = left */
int    lhnav_hover(lhnav *); int lhnav_pressed(lhnav *); int lhnav_hidden(lhnav *);   /* hidden: segments collapsed into "«" */
int    lhnav_rect(lhnav *, int part, int *x, int *y, int *w, int *h);
int    lhnav_field_x(lhnav *, int part, const char *utf8, int index);    /* caret <-> x in a field */
int    lhnav_field_index(lhnav *, int part, const char *utf8, int x);
int    lhnav_title_baseline(lhnav *);
int    lhnav_set_tools(lhnav *, const char *spec);                 /* replaces m.tools */
int    lhnav_tool_count(lhnav *);
int    lhnav_tool(lhnav *, int i, const char **arg, const char **label);  /* kind; a bookmark's path and label */
const char *lhnav_tip(lhnav *, int part);
void   lhnav_set_search_open(lhnav *, int open);  int lhnav_search_open(lhnav *);
void   lhnav_set_open(lhnav *, int part);         int lhnav_open(lhnav *);   /* the part whose list is up: drawn pressed */
void   lhnav_set_icon(lhnav *, const char *name, const uint32_t *px, int w, int h);  /* "search" "folder" "views" "newfolder" (+"_hot") */
void   lhnav_text(lhnav *, uint32_t *px, int w, int h, int stride, int x, int y, const char *utf8, int bold, uint32_t rgb);
int    lhnav_text_size(lhnav *, const char *utf8, int bold, int *height);  /* the client's popups in luxnav's text */
static inline void lhnav_paint_cairo(lhnav *, cairo_t *, int width, int composited);  /* LHNAV_WITH_CAIRO; only it includes cairo.h */
```

Parts (`lhnav_hit`): `LHNAV_NONE`, `CAPTION` (empty header: the client sends `_NET_WM_MOVERESIZE` move, double-click maximises),
`RESIZE_TOP` (the top `resize_rows` outside the buttons: `_NET_WM_MOVERESIZE` direction 1), `BUTTONS` (luxwm's
`_LHWM_CAPTION_RECT`, kept out of the input shape), `TITLE` (behaves as caption), `BACK`, `FWD`, `ADDRESS` (the breadcrumb box
outside the segments: the client's edit field), `OVERFLOW` ("«"), `SEARCH`, `BACK_MENU`, `FWD_MENU` (big variant), `CRUMB + i`
(100..), `SEP + i` (10000..: the chevron after segment i), `TOOL + i` (20000..); test with `LHNAV_IS_CRUMB/SEP/TOOL(p)`.
`lhnav_rect` answers every part; `LHNAV_SEARCH` is 0 while no field is out (a `search` item closed). While `LHNAV_ADDRESS` is
being edited the breadcrumb is hidden; an empty search field shows the placeholder "Search" (the crumb font, italic).

Usage: `lhnav_metrics_default`, `lhnav_metrics_load` (nav.ini), fill `border_t`, `border_l` and `caption_*` from
`_LHWM_FRAME_METRICS` and `_LHWM_CAPTION_RECT`, `lhnav_new(style)` (`ut_open` of the session's msstyles, may be NULL: drawn art,
no text), the setters, `lhnav_layout`, `lhnav_paint`; pointer events to `lhnav_pointer`, clicks through `lhnav_hit`.

## Geometry

aap's measurements for the small variant, frame rows from the top of the window:

- **Top deco: 15 rows**, ending exactly below the three (vision) caption buttons; nothing below them.
- **Title**: small, baseline on row 15, inside the deco, left of the buttons, drawn by the client (`CLIENT_CAPTION`, `NO_TITLE`).
- **Orb row: 30 rows**: the back/forward orb (no drop-down arrows) directly below the deco; the breadcrumb box and the search box
  vertically centred in it.
- **2 rows** of spacing. **Header: 47 rows** (15 + 30 + 2).
- **Text on glass**: every text drawn on glass gets a liberal white blur behind it and is drawn in black on top (the caption
  treatment, generalised; luxshell's taskbar does the same under Aero vision: pack `4074-aerovision`, `taskbar_text = glow`).

Measured on the lab (aerovision, navdemo, 720 px client):

| | rows / pixels |
| --- | --- |
| frame outer edge / highlight | rows 0 / 1 |
| caption buttons | rows 2..14 (13 rows, the plate's pill), 27 + 25 + 43 px, 5 px from the right edge; `_LHWM_CAPTION_RECT` `627, 0, 93, 13` |
| client's row 0 | frame row 2 (`caption_border`; `_NET_FRAME_EXTENTS` 9 9 2 9) |
| title | Segoe UI 12 px (the style's breadcrumb font), ink rows 6..14, baseline 15, from x 16 |
| orb row | rows 15..44: back 26x30 + forward 18x30 (the 4074 art's rows 0..43 scaled by 30/44) |
| boxes | rows 18..40 (23), breadcrumb from x 61, search 180 px wide, 4 px from the right |
| spacing | rows 45..46 |
| below | the plate's light line row 47, black line 48, the body from 49 |
| `_LHWM_EXTEND_FRAME` top | 47 client rows (45 header + 2 line rows); maximised 49 (`border_t` 0) |

The frame side (the Explorer plate copied 1:1 and cropped at the header, never scaled vertically, its inner line pair redrawn at
the crop; `header_mode = crop`, `deco_h = 15`, `caption_border = 2`, `_LHWM_FRAME_METRICS` field 18) is CLIENT-PROTOCOL.md
"Vision header". Without composition aerovision's frame is `jade4074-classic`.

## Vision caption buttons

Smaller buttons fitting the 15-row deco: aero's sprites (4 frames of 14 rows, at `button_top = 2`, which would end on row 15) with
one sprite row dropped, 13 rows ending on row 14 with the pill; the widths stay (27 + 25 + 43 fill the plate's pill with
`button_right = 5`). `tools/build_aerovision.py [--drop-row N]` (default 11: the light inner line above the dark bottom line) cuts
them from the aero-patch sprites; `lhde-import` runs it into the Windows tree, and theme.ini names them with `button_files =
vision_close.png vision_max.png vision_min.png vision_restore.png` (not close.png etc., which the style's CompositedWindow streams
replace). The concept shows a ~13-row pill hanging from the top edge with buttons of ~26/25/37 px; ours keep aero's widths because
the pill is baked into the plate texture.

## Presets

nav.ini: `data/frames/aerovision/nav.ini` beside the frame set's theme.ini, and `data/navbar/vision.ini` (the same file, the
theme's `path.navbar`). Every key is commented there with its automatic value at 720 px. `preset = NAME` at the top picks a
`[preset NAME]` section; each section has all keys, so a new size is a copied section. Keys before the first section apply to
every preset (a section's keys win); no `preset` key = the first section; other `[...]` sections are skipped. Shipped presets:
`small` (aap's values), `big` (the default `preset`), `concept` (`small` with `tools = search favorites`). `m->preset` says which
was read. Keys (`lhnav_metrics` field names; colours `#RRGGBB` or `style`):

- `variant = small | big`; vertical `deco_h orb_h gap header_h box_h lines resize_rows`; horizontal `pad_l pad_r title_x box_gap
  search_w search_min crumb_pad sep_w crumb_x`; text `title_dy crumb_dy`; fonts `title_font crumb_font` (pango's form, `Segoe UI,
  14` = 14 pt = 19 px; "" = the style's breadcrumb font); colours `title_rgb crumb_rgb glow_rgb plain_rgb`; `glow_size`
  (-1 = the style's TEXTGLOWSIZE), `glow_strength` (2.5); fields `address_field` (0 only while editing, 1 on hover, 2 always),
  `search_field` (0 hover, 1 always), `field_alpha`; the toolbar ("Toolbar"); `icon_dir`, `art_dir`, `art_style` ("Where the art
  comes from").
- **Hand placement**, applied after the automatic layout, each element against its own automatic place (moving the orb does not
  move the boxes): `address_dy`/`search_dy` (the box moves, its text and segments with it; `crumb_dy` moves only the text),
  `address_dx`/`search_dx`/`orb_dx`/`orb_dy`; absolute `orb_x orb_y`, `address_x address_y address_w address_h`, `search_x search_y
  search_h` (x client pixels, negative = from the right edge; y frame rows; empty or `auto` = automatic); `title_dx` (+ title_x),
  `title_baseline` (frame row, default deco_h; + title_dy), `crumb_text_dx/dy` (the segment text inside its cell; cells and hover
  boxes stay); `header_h` (frame rows; default deco_h + orb row + gap).

**Live**: clients re-read nav.ini on every luxwm theme reload (`session/theme-watch.sh` watches it). navdemo: `p` / `P` cycle the
presets (header height and `_LHWM_EXTEND_FRAME` follow; the title shows `[NAME]`), `NAVDEMO_PRESET=NAME` starts with one, an edit
of the file's `preset =` wins over `p` again; `NAVDEMO_DEBUG=1` prints every final rect (client x, y, frame row, size) incl.
`back_menu`, `fwd_menu` and the title baseline.

## The big variant

`variant = big` (preset `big`): the full `TravelBack`/`TravelFwd::AnimationButton` art, not cropped: 38x54 + 27x54, forward at x 38
(`ANIMATIONBUTTONRECT`), unscaled (`orb_h = 0`: the art's height; another value scales it). In the aero-patch art NORMAL/DISABLED
have no drop-down (orb and well only), HOT/PRESSED show the drop-down box with the arrow below the orb (rows ~36..53, the split
`CUSTOMSPLITRECT.top` 44); `SPLITPRESSED` is used where the style has it (aero-patch: PRESSED instead). Button and arrow part are one
image: hot when either is hovered (as 4074's browseui), pressed / split-pressed by the part pressed. Measured (720 px, frame rows):

| | big |
| --- | --- |
| deco | rows 0..14, title baseline 15 (as small) |
| travel art | rows 15..68 (54), back x 0..37, forward 38..64 |
| `LHNAV_BACK_MENU` / `LHNAV_FWD_MENU` | rows 59..68 (art rows 44..53), x 3..37 / 38..62 (the hot art's opaque columns below the split; only while back / forward is possible) |
| boxes | rows 26..46 (21), centred against the orb part (rows 15..58); breadcrumb x 73, search 180 px at x 536 |
| header | 69 rows (15 + 54 + `gap = 0`): the drop-down box's last row is 68; light line row 69, black 70, body from 71 |
| `_LHWM_EXTEND_FRAME` top | 69 client rows (67 + 2 line rows); maximised 71 (MAXEXPLORER lines on 69/70 too) |

History list (navdemo): a press on an arrow part opens an override-redirect list under it (`lhnav_rect(LHNAV_BACK_MENU)`) with the
back history newest first (or forward), up to 16 folder names; pointer and keyboard grabbed; release or click on an entry goes
there, Up/Down/Enter, Escape or a click outside closes it; the arrow keeps its split-pressed look while the list is open. A right
click on back / forward opens the same list in either variant.

## Toolbar

nav.ini `tools = ITEM ITEM ...` per preset (blanks or commas between items, `"..."` for an item with blanks; default
`search_field`): the items left to right, right-aligned before `pad_r`, `tool_gap` (4) apart, centred on the search box's row
(after `search_dy`); `tools_dx`/`tools_dy` move them further (the field keeps `search_*`). `tool_size` (0 = auto: the orb part's
height - 2, at most what fits around the row: 28), `tool_icon` (16, 24 or 32).

| Kind | Look | Art (in this order) | navdemo |
| --- | --- | --- | --- |
| `search_field` | the box (`search_w`, shrinks first in narrow windows, down to `search_min`) | drawn field + magnifier | the text field (`LHNAV_SEARCH`, not a tool hit) |
| `search` | the magnifier glyph, 4/5 of `tool_size`, no box; hot: full strength + a soft white light behind; pressed: 1 px down | `lhnav_set_icon("search")`, shell32.dll icon group 23 (32 px), drawn | `lhnav_set_search_open(1)`: the field (search_w) comes out left of it, focused; closed when it loses the focus empty |
| `favorites` | the star orb at `tool_size` | `Favorites::AnimationButton` (+ `Inactive`/`Max`/`MaxInactive`, states DISABLED/NORMAL/HOT/PRESSED), `SmallFavorites::` up to 35 px, drawn | the GTK bookmarks (`$XDG_CONFIG_HOME/gtk-3.0/bookmarks`, read fallback `~/.gtk-bookmarks`) with "Add current folder" first (appended atomically, no duplicates; remote URIs greyed) |
| `bookmark:PATH[:LABEL]` | a folder icon button (`tool_icon` + 8 px, `box_h` high); tooltip LABEL or the folder's name | `"folder"`, shell32 icon group 4, drawn | goes to PATH (`~` expanded by the client) |
| `views` | XP's Views icon + an 11 px drop-down arrow part (one part) | `"views"`/`"views_hot"`, shell32's toolbar strips bitmap 216/217 (16 px; 214/215 24 px) cell 32 (XP's tb_sh_def/tb_sh_hot), drawn | List / Details; Details adds Size and Date Modified |
| `newfolder` | XP's New Folder icon button | `"newfolder"`, the same strips cell 31 (VIEW_NEWFOLDER) | "New Folder", "New Folder (2)", ... selected in the listing |
| `separator` | a 7 px cell with a vertical line (glass: white fading out at both ends, as flxp's divider; opaque: dark + light) | drawn | no hit |

Icon buttons' hot/pressed box: the breadcrumb toolbar's `SPLITBUTTON` HOT/PRESSED (as a hot segment). `icon_dir` (nav.ini,
relative to lhde's data directories, e.g. `resources/4074`; else navdemo passes the session theme's `path.resources`) is a build's
system32: `lhnav_res.c` reads shell32.dll's RT_GROUP_ICON/RT_ICON (DIB or PNG) and RT_BITMAP straight from the PE file (a small
mmap'ed resource walker).

## Breadcrumb drop-downs

luxnav provides the parts and the look; the lists are the client's. Every chevron is `LHNAV_SEP + i`, the drop-down of segment i;
the chevron after "«" is `LHNAV_SEP + hidden - 1`; the one after the last segment exists where it fits and shows with `sep_last` 1,
or 2 (default) while the breadcrumb is hovered, 0 never. `LHNAV_OVERFLOW` ("«") is a drop-down too. Hot: the style's
`SPLITBUTTONDROPDOWN` HOT over the chevron cell and the segment's `SPLITBUTTON` HOT (the split pair); pressed or open
(`lhnav_set_open(n, part)` while the client's list is up): both PRESSED and the chevron turned down (the second image of
`SPLITBUTTONHORZARROW`'s glyph strip; aero-patch: a down arrow); without the style's art a drawn box and triangle.

navdemo: a chevron lists segment i's subfolders (sorted case-insensitively, dot folders left out, at most 500) under the
breadcrumb box, segment i + 1's folder bold and selected (Vista's way); "«" lists the collapsed ancestors, the nearest first. One
list window (override-redirect, pointer and keyboard grabbed, at most 24 lines then it scrolls, wheel too): Up/Down/Home/End, a
first letter, Enter or a click picks, Escape or a click outside closes. flxp's chevron lists the crumb's *siblings* (its parent's
subfolders, the crumb bold: 4074's, lh4074-224731.png) unless `header_crumb_menu = children`.

**Tooltips**: `lhnav_tip(n, part)` gives the text (Back, Forward, Recent pages, Search, Favorites, a bookmark's label, Views, New
Folder, Folders above, "Folders in NAME" for a chevron); navdemo shows an XP-style tooltip window (infobk, black frame) after
500 ms of hover, hidden on a press, a key or leaving the part.

## Text and pixels without pango and cairo

All desktop text goes through wuxtheme's GDI-faithful text, so luxnav's text looks exactly like flxp's.

- **Text**: every string (title, segments, "«", the fields, the placeholder, and the client's tooltips and lists through
  `lhnav_text` / `lhnav_text_size`) is rendered once as an 8-bit coverage mask with `ut_draw_text_mask` and composited in luxnav's
  colour; on glass the same mask is box-blurred twice (radius = glow size), times `glow_strength`, in the glow colour, under the
  text; not composited: plain text in `plain_rgb` (the caption colour). Fonts: the breadcrumb's is the style's part font
  (`CompositedBreadcrumbBar::Toolbar` FONT, aero-patch Segoe UI -12; no such class: Segoe UI 12 px); `crumb_font` / `title_font`
  become explicit `ut_font`s (`ut_draw_text_mask(..., font)`, `ut_text_extent_font`); the placeholder is the crumb font in GDI's
  synthesized italic, bold list items the crumb font at weight 700. Widths: `ut_text_extent` / `ut_text_extent_font` (advance,
  `DT_SINGLELINE | DT_NOPREFIX`), cached per (font, text) in a 512-entry table; the cell from `ut_get_text_metrics` (ascent,
  height), centred in its box with floor; the title's baseline is `title_baseline`. Ellipsis: `DT_END_ELLIPSIS` (GDI's "...") in
  the segment / title width. Caret positions: the advance of the prefix. GDI's text is crisper than pango's: stems on whole
  pixels, integer advances.
- **Pixels** (`lhnav_px.c`): images, OVER with pixman's rounding (`x*a + 0x80`...), blits 1:1 / from a source rect / with opacity,
  scaling (box filter below 0.75, bilinear above, cairo's GOOD filter's choice), A8 masks in a colour, solid, linear and radial
  (two-circle, PAD) paints, and shapes from a signed distance: rounded rectangles, circles, polygons (nonzero), strokes (butt or
  round caps); edge pixels sampled 32 x 15 at the sample rows' tops with the top-left rule, as cairo's scan converter does. PNG
  (`art_dir` strips, shell32's PNG icons) through libpng's classic API, premultiplied as cairo's loader does.

## Where the art comes from

`LHNAV_DEBUG=1` (any client, on every art load) or `NAVDEMO_DEBUG=1` (with the layout) prints `lhnav_art_sources()`; a style
without travel art is logged once (`luxnav: STYLE has no travel art: ...; drawn orbs`). For aero5048:

```
  back         aero5048.msstyles TravelBand TRAVELBUTTONS IMAGEFILE, left half (18x24 at 4,2 on the plate), states DISABLED NORMAL HOT PRESSED
  forward      aero5048.msstyles TravelBand TRAVELBUTTONS IMAGEFILE, right half (18x24 at 22,2 on the plate), states ...
  plate        aero5048.msstyles TravelBand TRAVELBACKGROUNDS IMAGEFILE 61x28, states NORMAL HOT PRESSED DISABLED (the chevron's)
  history      aero5048.msstyles TravelBand TRAVELBACKGROUNDS: the chevron, plate columns 51..60 (one list for both directions)
  favorites    drawn
  crumb arrow  aero5048.msstyles BreadcrumbBarToolbar::Toolbar SPLITBUTTONHORZARROW GLYPHIMAGEFILE 9x9, image 1 (open: image 2)
  segment box  aero5048.msstyles BreadcrumbBarToolbar::Toolbar SPLITBUTTON IMAGEFILE, states HOT PRESSED (nine-gridded)
  arrow box    aero5048.msstyles BreadcrumbBarToolbar::Toolbar SPLITBUTTONDROPDOWN IMAGEFILE, states HOT PRESSED
  crumb text   aero5048.msstyles BreadcrumbBarToolbar::Toolbar TEXTCOLOR #00025c GLOWCOLOR #ffffff TEXTGLOWSIZE 4 (...)
  tool 0       search field: the field: drawn; its magnifier: drawn
```

| Element | 4051/4074 styles, aero patch | Vista TravelBand (5048, 5112) | nothing usable |
| --- | --- | --- | --- |
| back / forward | `TravelBack`/`TravelFwd::AnimationButton` ABBACKGROUND IMAGEFILE, DISABLED NORMAL HOT PRESSED (+SPLITPRESSED), Inactive/Max/MaxInactive variants; small: the rows above `CUSTOMSPLITRECT.top` (44), scaled to `orb_h`, HOT/PRESSED synthesized inside the orb; forward at `ANIMATIONBUTTONRECT.left` (38) | `TravelBand` TRAVELBUTTONS: one image per state, back = left half, forward = right half, each in its own state; unscaled | drawn orbs (38x44 + 27x44 at 38, arrows pointing back left, forward right), scaled like the art |
| plate behind them | (part of the button art) | `TravelBand` TRAVELBACKGROUNDS, unscaled, centred on the orb part; its state is the chevron's (DISABLED: neither direction possible) | none |
| history drop-down | big: the art's rows below `CUSTOMSPLITRECT.top` (`LHNAV_BACK_MENU`, `LHNAV_FWD_MENU`); small: none | the plate's chevron (`LHNAV_BACK_MENU`, either direction), from where the HOT plate's box starts | none |
| favorites star | `Favorites::AnimationButton` (+variants), `SmallFavorites::AnimationButton` up to 35 px | (none in 5048/5112) drawn | drawn star |
| breadcrumb arrow | `BreadcrumbBarToolbar::Toolbar` / `BB::Toolbar` / `Toolbar` SPLITBUTTONHORZARROW GLYPHIMAGEFILE (image 2: open) | the same classes | drawn triangle |
| segment / arrow boxes | the same class's SPLITBUTTON / SPLITBUTTONDROPDOWN, HOT PRESSED | the same | drawn rounded boxes |
| breadcrumb colours, glow, font | the **theme's** style: `CompositedBreadcrumbBar::Toolbar`, else `BreadcrumbBarToolbar::Toolbar`, `BB::Toolbar` (never from `art_style`) | the same | (0,2,92), white glow 4 |
| tool icons | the client's `lhnav_set_icon`, then `icon_dir`'s shell32.dll: icon groups 23 and 4, bitmaps 216/217 (214/215 at 24 px) cells 32 and 31 | the same | drawn |
| search box's magnifier, fields, separator line | drawn | drawn | drawn |

The small orbs' hot/pressed images without the menu arrow do not exist in the art: luxnav synthesizes them when the style is
loaded, the NORMAL image with the HOT/PRESSED pixels copied in where NORMAL is opaque (alpha 160..240 blended), so the drop-down
box and the well stay out. `art_dir` (nav.ini, relative to the frame set) holds optional `nav_back.png` / `nav_fwd.png` orb-only
state strips (disabled, normal, hot, pressed, stacked) that replace the style's travel art.

**TravelBand** (`ut-dump --list STYLE TravelBand`, `--render`): 5048's plate is 61x28 (one long well, the chevron at columns
54..58; HOT/PRESSED light a box around it, DISABLED drops it), its glyphs 36x24 (two 18x24 orbs; blue, HOT and PRESSED green,
DISABLED grey back and an *empty* forward: 5048 hides forward when it is impossible, as its Explorer screenshot shows); 5112's plate
70x27 (two round wells, `CONTENTMARGINS 0 13 2 0`), glyphs 54x27 (two 27x27 blue orbs, DISABLED ghosted). Composition as browseui:
the plate, then each half of the glyph strip in its button's state. Placement: with CONTENTMARGINS each button is centred in its
half of the content width (5112: back at x 1, forward at 29, the wells' centres); without (5048) the strip sits at x 4, y 2,
measured on `~/fun/longhorn/screenshots/WindowsVista-6.0.5048-Explorer.png` (plate at 8,25, back glyph at 12,27; template match,
mean error 2 of 765). The band is never scaled: small, centred in the 30-row orb row; big, centred on the 44-row orb part (header
69 rows as with the aero patch). 5112's TRAVELBUTTONS(DISABLED) bitmap comes out of wuxtheme with straight alpha (colour above
alpha in 976 pixels); luxnav premultiplies such an image and logs it.

**`art_style`** (nav.ini, global or per preset; the shipped files set `auto`): `auto` = the theme's own style; a NAME =
`styles/NAME.msstyles` in lhde's data directories (`lhde_data_find`), e.g. `aero-patch` for the patched aero art under 5048/5112;
an absolute path = that file. It replaces the travel, favorites and breadcrumb art; text (fonts, colours, glow) stays the theme
style's. A name that is not found or not a style falls back to `auto` with a log line. For 5048/5112 the small preset with `auto`
(crisp unscaled Vista orbs) is closer to the builds; the big preset is drawn for the aero patch's 54-row orbs and looks emptier
with the 27/28-row band, so there `aero-patch` fills it better (taste).

Which nav.ini a client reads: navdemo, the frame set's (`_LHWM_THEME`), else the theme's `path.navbar` when its `Explorer.navbar`
is `vision` (aero5048, aero5112 have no nav.ini of their own); flxp, the theme's `path.navbar` when `Explorer.navbar = vision`,
else the frame set's ("flxp's header").

## Against the concept art

Measured on the concept (`1456384117.or.45878.png`, frame at 122,67, 799 px wide) against navdemo at the same place and width:

| | concept | ours | knob |
| --- | --- | --- | --- |
| header to the body's dark line | 51 rows | 48 (light 47, black 48) | aap's 47; `orb_h`/`gap` |
| back orb sphere | ~25-26 px, the well ~34 rows (13..47), forward stub to x ~58 | ~21-22 px, well 30 rows (15..44), stub to x ~52 | `orb_h` (37 gives the concept's sphere) |
| title | none (the breadcrumb is the title) | "All Music", Segoe UI 12 px black, baseline 15 | `title_font`, `title_rgb`; a client may pass no title |
| breadcrumb text | ~15-16 px Segoe UI, ink (14,64,128) steel blue, from x ~100 after a 16 px Windows-flag icon | 12 px, (0,2,92) navy, from x 70, no icon | `crumb_font`, `crumb_rgb`, `crumb_x` |
| separator | a small 4x7 blue triangle, no outline | the style's 9x9 black chevron with a white rim | (art; `sep_w` only) |
| search | a large magnifier (~20 px) and a favorites star orb (~32 px) at the right, no field | `small`/`big`: a 180 px field with a 10 px magnifier; `concept`: a 22 px magnifier and a 28 px star, the field only while searching | `tools`, `tool_size` (32 gives the concept's star), `search_w` |
| caption buttons | a ~13-row pill from the top edge, 25/26/37-39 px, small grey glyphs | rows 2..14, 27/25/43 (the plate's pill), aero glyphs | `button_*` in theme.ini, `build_aerovision.py --drop-row` |
| glass | frosted, nearly opaque white-blue | aero's squared plate alpha: the sky shows through clearly | `glass_alpha`/`glass_alpha_mode` in aerovision/theme.ini |

The concept's orb (~25-26 px sphere) lies between small (~22) and big (~36).

## Travel-button sizes after 4051

The back/forward art was resized after 4051: 4074 Slate's buttons have the drop-down arrow part not extending as far down (which
goes together with the rendering bug under the 4074 title bar), while 4074 Jade's keep the 4051 size. Relevant for a Slate vision
preset and for flxp's faithful 4074 Slate header.

## flxp's header

flxp's Explorer header (`flxp/explorer/header.cpp`) draws through luxnav in its vision mode. Which header draws: flxp.ini's
`header_engine = lhnav | flxp` when set (a developer's choice), else the session theme's `Explorer.navbar` (`vision` = luxnav with
`path.navbar`, `4051 | 4074` = flxp's own header); without a session theme luxnav when luxwm's frame set has a nav.ini. The
preset: `header_preset`, else the theme's `Explorer.navbar_preset` (`auto` = none), else `small` for the Small header size, else
the file's own. One lhnav per header (the explorer; the Longhorn file dialog prepends `newfolder views` to `tools`, the small
preset for its Small size). The style is the theme's `navbar_msstyles`, else the session's msstyles; nav.ini is re-read on every
luxwm theme reload. Measured next to navdemo at 720 px: every luxnav rect identical, frame rows 0..70 (big) / 0..48 (small,
maximised 49) identical outside the text, hover/pressed orb identical.

**luxnav does** (`lhnav_paint` into a 32-bit premultiplied DIB section, then `AlphaBlend` over the background): the layout of travel
buttons, title, breadcrumb (segment fitting, "«" collapsing), search box and the right-hand items; painting the travel art with its
variants and states, the title and crumb text with the glow on glass, the segments' split-button art, the magnifier, the star and
the toolbar's New Folder and Views (icons through `lhnav_set_icon` from flxp's icon theme, or shell32 through `icon_dir`);
hit-testing and the hover/pressed state machine (WM_NCHITTEST maps `LHNAV_CAPTION`/`LHNAV_TITLE` to HTTRANSPARENT, the frame's
caption, everything else to HTCLIENT; drop-down parts `LHNAV_BACK_MENU`/`_FWD_MENU`, `LHNAV_SEP + i`, Favorites, Views open on
WM_LBUTTONDOWN, the rest act on WM_LBUTTONUP; `lhnav_set_open` while flxp's menu is tracked); the tooltips' text (`lhnav_tip`)
for flxp's comctl32 tooltip control.

**flxp does**: everything a click does (the history and its Back/Forward menus, the namespace chain for the segments with Computer
first and virtual folders, the chevron's menu: `header_crumb_menu = siblings` (4074, auto) or `children` (Vista), bookmarks, New
Folder, the views and their per-folder memory, search filtering); the address and search Win32 EDITs placed at
`lhnav_rect(LHNAV_ADDRESS / LHNAV_SEARCH)` (while one is shown luxnav paints the box only, `lhnav_set_field(part, NULL, -1, 0)`;
opening the search is `lhnav_set_search_open(1)` + relayout); the background under luxnav (glass, the theme's navigation-bar band,
luxwm's `_LHWM_HEADER_PIXMAP`, the Slate gradient); the 4051 command bar below the header; settings and menus (`header_size`, the
element toggles, which become the preset, `lhnav_set_title(NULL)` and the `tools` list); the luxwm protocol
(`DwmExtendFrameIntoClientArea`, CLIENT_CAPTION, `_LHWM_FRAME_METRICS` -> `lhnav_metrics` `border_t`, `deco_h`, caption rect) and
the theme reload (`lhnav_set_style`/`lhnav_set_metrics`).

## Missing

- flxp's 4074 Medium/Large/Extra-Large layout (title on its own row above the breadcrumb, 4074-dwm.png, the right items at the
  band's places or the bottom): luxnav has the vision rows only (title in the deco, boxes beside the orb); `data/navbar/4051.ini`
  and `4074.ini` wait for it.
- The even small travel art (`SmallTravelBack/Fwd::AnimationButton`) and flxp's uneven shrink (the drop-down rows kept 1:1):
  luxnav scales the full art's orb rows and synthesizes orb-only hot/pressed images instead.
- The Avalon framelets (`Microsoft.Windows.Client.dll` .resources) as travel art: luxnav takes `art_dir` PNG strips only (which no
  tool generates); an API to hand it pixels (like `lhnav_set_icon`) would let flxp keep its netres reader.
- The breadcrumb field's 15 px drop button (flxp: the back history) and 4074's field look (darker field, rules).
- A generic icon item for arbitrary host buttons (flxp's `set_tools` takes any `{id, icon, dropdown}`), and a disabled state for
  tool items.
- An icon before the first segment (the concept's Windows flag).
- Slate vision (`data/themes/slatevision.theme.todo`: Aero's buttons on Slate, as the WinHEC 2004 Slate concepts show, a
  non-composited reskin); a size between small and big for the concept's orb (`variant = big`, `orb_h` ~40, or `small` with `orb_h`
  37) and the concept's toolbar size (`tool_size = 32` with `orb_h = 37`); the 4074 shell32 16 px folder is the tall Longhorn
  folder, `tool_icon = 24` uses the 24 px strip and folder.
- `caption_border = 2` leaves luxwm only the 2 outer rows for top resizing, plus luxnav's 3 `resize_rows`: whether that is enough
  is to be seen.
