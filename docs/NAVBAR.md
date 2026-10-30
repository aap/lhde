# The Longhorn navigation bar as a component (luxnav), and the "vision" title bar

> **Imported from the Longhorn X11 lab** (`~/fun/longhorn/x11` at d353b69, NAVBAR.md) into lhde in step 3 of docs/LAYOUT.md. Program and path names are updated to lhde (lhwm → luxwm, lhwmc → luxwmc, lhshell → luxshell, lhnav → luxnav; x11/... and lhwm/themes, lhshell/themes, lhshell/assets → the lhde tree and data directories, see docs/LAYOUT.md and tools/lhde-import); the X atoms (`_LH_*`, `_LHWM_*`, `_LHSHELL_*`), the window manager name "lhwm", luxnav's C API (`lhnav_*`, `lhnav.h`), configuration paths (`~/.config/longhorn`, `~/.config/lhwm/keys`, `~/.config/lhshell`) and setting values (`header_engine = lhnav`, `classic_start_menu = lhshell`) keep their names. Dated sections are history: they describe the x11 tree as it was (its compare_*.png and screenshots stay in the research workspace).

Status: spec, 2026-10-27; steps 1 and 2 implemented and step 3's comparison done the same day, then presets, hand placement and the
big-button variant, then the right-hand toolbar and the breadcrumb drop-downs, then text through libuxtheme and no cairo/pango ("Status" at the end; "Porting luxnav into flxp" for
the next step). Goal of the milestone: the Explorer title/navigation area as the concept
art envisioned it (`/u/aap/chaos/img/windows_concepts/lh_concepts/1456384117.or.45878.png` and the
other `lh_concepts/` images), for the experimental themes **Aero vision** and **Slate vision**. The
faithful 4051/4074 themes stay as they are.

## Who owns what

| Piece | Where | Why |
| --- | --- | --- |
| Frame: cropped header plate, 15 px top deco, vision caption buttons | luxwm (`themes/aerovision/`, `themes/slatevision/`, `frame.c`) | it is decoration |
| Back/forward orb, breadcrumb, address box rect, search box rect, title text: **layout, painting, hit-testing, visual state** | **luxnav**, a small toolkit-independent C library here (`luxnav/`, later `lhde/`) | iconic Longhorn component; must look identical in every client |
| Navigation behaviour: history, the path model, what a click does, the editable address and search text fields | the client (flxp; the demo) | real functionality belongs to the explorer |
| Minimal example | `examples/navdemo.c` (successor to `extendframe.c`) | the reference integration, like extendframe was for the protocol |

luxnav follows libuxtheme's conventions so flxp can use it unchanged: plain C API, no window system,
draws into premultiplied ARGB buffers (`uint32_t *pixels, w, h, stride`), with a header-only cairo
helper for cairo clients; theme art comes from the current visual style through libuxtheme
(`ut_get_stream`/`ut_get_bitmap`/`ut_draw_background`) and from imageres where the style has none.
Since 2026-10-27 (fourth pass, "Text and pixels" below) libluxnav depends on libuxtheme and libpng only: its text is libuxtheme's
GDI text (flxp's), its pixels are its own (`lhnav_px.c`), no cairo, pango or fontconfig.

### API sketch
```c
typedef struct luxnav luxnav;
luxnav *lhnav_new(ut_theme *style, const lhnav_metrics *m);   /* metrics from the frame (below) */
void   lhnav_set_title(luxnav *, const char *utf8);
void   lhnav_set_path(luxnav *, const char *const *segments, int n); /* breadcrumb segments */
void   lhnav_set_history(luxnav *, int can_back, int can_fwd);
void   lhnav_layout(luxnav *, int width);                          /* computes all rects */
int    lhnav_height(luxnav *);                                     /* 47 for the small variant */
void   lhnav_paint(luxnav *, uint32_t *px, int w, int h, int stride, int composited);
int    lhnav_hit(luxnav *, int x, int y);   /* LHNAV_BACK, _FWD, _CRUMB+i, _ADDRESS, _SEARCH, _TITLE, _CAPTION, _NONE */
void   lhnav_pointer(luxnav *, int x, int y, int buttons);         /* hover/pressed state; returns needs-repaint */
void   lhnav_rect(luxnav *, int part, int *x, int *y, int *w, int *h); /* where the client puts its edit fields */
```
`_CAPTION` = empty header area: the client sends `_NET_WM_MOVERESIZE` there (CLIENT-PROTOCOL.md).

## Geometry: small-button variant (Aero vision first)
All numbers from aap's todo (2026-10-27), measured from the top of the window:
- **Top deco: 15 px.** The frame decoration ends exactly below the three caption buttons; nothing
  below them. Caption buttons are the vision set (smaller, see below).
- **Title**: small, baseline at y = 15, inside the deco, drawn by the client (`CLIENT_CAPTION`,
  `NO_TITLE`), left of the buttons.
- **Row 2: back/forward orb, 30 px tall** (the orb only, no drop-down arrows), directly below the
  deco; the address/breadcrumb box and the search box vertically centred in that 30 px row.
- **2 px** spacing below.
- **Total header: 47 px** (15 + 30 + 2).
- Text on glass: every text drawn on glass gets a liberal white blur behind it and is drawn in black
  on top (the caption treatment, generalised: `luxnav` does it; lux's taskbar gets the same as a pack
  option for Aero vision).

## Frame: the header plate is never scaled vertically
luxwm draws the Explorer plate (STREAM 484, 73-row band) **unscaled** and **cropped** at the requested
header height, then redraws the 1 px dark inner line (and the light line under it, as the texture
has them) at the crop row; the client area starts right below. This is what 4074 apparently tried
(the visual bug under its title bar). New theme keys (proposal): `header_mode = crop`,
`deco_h = 15`, `header_h = 47`. With `CLIENT_CAPTION` the client covers everything below the deco
except the caption-button rect; `_LHWM_FRAME_METRICS` gains `deco_h`.

## Vision caption buttons
Smaller buttons fitting the 15 px deco. The hover/pressed sprites without the menu arrow do not
exist: synthesize them from the regular buttons by copying the orb interior over (a build script
writing PNGs into the theme dir, like `tools/build_aero5112.py`; inspectable and tunable with
live reload). Slate vision gets the Aero buttons too (the WinHEC 2004 Slate concepts show Slate
with Aero buttons), as a non-composited reskin.

## Medium-button variant
As today's tall header (buttons with drop-down arrows) but with the 15 px top deco. Built as nav.ini `variant = big` (preset `big`,
"Status" below).

## Order of work
1. luxwm: `aerovision` theme dir (copy of `aero`), `header_mode = crop`, `deco_h`, metrics, the
   synthesized button set; frametest renders.
2. luxnav library + `navdemo` (real directory breadcrumb, back/forward over a local history,
   address edit as a plain text field, search field stub), integrated with `CLIENT_CAPTION`.
3. Compare against the concept art; tune with live reload.
4. Slate vision; medium-button variant; flxp wraps luxnav in a wux control.

## Status (2026-10-27)

### Built
- **luxwm** (`docs/LUXWM.md` "Aero vision", `docs/CLIENT-PROTOCOL.md` "Vision header"): theme `data/frames/aerovision` (a copy of
  `aero` with aap's values), registry `[aerovision]` (title "Aero vision", aero-patch style and fonts, `taskbar = avalon`,
  `start = flxp`, composition auto, `frame_classic = jade4074-classic`), wallpaper 4051's Bliss (`wallpapers.ini`: the concept's
  white clouds on deep blue). frame.c keys `header_mode = crop`, `deco_h = 15`, `caption_border = 2`, `button_files`;
  `_LHWM_FRAME_METRICS` field 18 = `deco_h`. `tools/build_aerovision.py` writes the 13-row caption buttons.
- **luxnav** (`luxnav/lhnav.h`, `lhnav.c`, `Makefile`: `libluxnav.a`, `libluxnav.so`): layout, painting, hit-testing, hover/pressed
  state; art from the style through libuxtheme; tuning from the frame theme's `nav.ini`.
- **navdemo** (`examples/navdemo.c`, `make`; USAGE.md): the reference integration.
- **comparison**: `examples/compare_navdemo.png` (concept above, ours below, 1:1, frames at the same place and width) and
  `examples/compare_navdemo_4x.png` (the top 50 rows, left 280 and right 170 columns, 4x).

### Geometry as measured on the lab (`:3`, aerovision, navdemo 720 px client; frame rows from the frame's top edge)
| | spec | result |
| --- | --- | --- |
| frame outer edge / highlight | | rows 0 / 1 |
| caption buttons | in the deco | rows 2..14 (13 rows, the plate's pill rows 2..14), 27 + 25 + 43 px, 5 px from the right edge |
| deco | 15 rows | rows 0..14; `_LHWM_CAPTION_RECT` `627, 0, 93, 13` (client) ends on row 15 |
| client's row 0 | | frame row 2 (`caption_border`; `_NET_FRAME_EXTENTS` 9 9 2 9) |
| title | small, baseline 15, left | Segoe UI 12 px (the style's breadcrumb font), ink rows 6..14 (baseline 15), from x 16 |
| orb row | 30 rows below the deco | rows 15..44: back 26x30 + forward 18x30 (the 4074 art's rows 0..43 scaled by 30/44) |
| boxes | centred in the orb row | rows 18..40 (23 rows), breadcrumb from x 61, search 180 px wide, 4 px from the right |
| spacing | 2 | rows 45..46 |
| header | 47 | glass rows 0..46; then the plate's light line on row 47, its black line on 48, the body from row 49 |
| `_LHWM_EXTEND_FRAME` top | | 47 client rows (45 header + 2 line rows); maximised 49 (`border_t` 0) |

### luxnav API (as implemented; lhnav.h has the full comments)
```c
void   lhnav_metrics_default(lhnav_metrics *m);                 /* the numbers above */
int    lhnav_metrics_load(lhnav_metrics *m, const char *path);  /* nav.ini: key = value */
void   lhnav_add_font_dir(const char *dir);                     /* the theme's `fonts` (Segoe UI 0.7) */
luxnav *lhnav_new(struct ut_theme *style, const lhnav_metrics *m);  void lhnav_free(luxnav *);
void   lhnav_set_metrics(luxnav *, const lhnav_metrics *);       /* border_t, caption rect, ... from luxwm */
void   lhnav_set_style(luxnav *, struct ut_theme *);
void   lhnav_set_title(luxnav *, const char *utf8);
void   lhnav_set_path(luxnav *, const char *const *segments, int n);
void   lhnav_set_history(luxnav *, int can_back, int can_fwd);
void   lhnav_set_frame_state(luxnav *, int focused, int maximized); /* Inactive/Max/MaxInactive travel art */
void   lhnav_set_field(luxnav *, int part, const char *utf8, int caret, int editing); /* paints the client's edit fields */
void   lhnav_set_placeholder(luxnav *, const char *utf8);
void   lhnav_layout(luxnav *, int width);
int    lhnav_height(luxnav *);       /* client rows: 47 - border_t */
int    lhnav_extend_top(luxnav *);   /* + the 2 line rows: the _LHWM_EXTEND_FRAME top */
void   lhnav_paint(luxnav *, uint32_t *px, int w, int h, int stride, int composited);
int    lhnav_hit(luxnav *, int x, int y);  /* LHNAV_NONE CAPTION RESIZE_TOP BUTTONS TITLE BACK FWD ADDRESS OVERFLOW SEARCH, CRUMB+i */
int    lhnav_pointer(luxnav *, int x, int y, int buttons);  /* 1 = repaint */
int    lhnav_hover(luxnav *); int lhnav_pressed(luxnav *); int lhnav_hidden(luxnav *);
int    lhnav_rect(luxnav *, int part, int *x, int *y, int *w, int *h);
int    lhnav_field_x(luxnav *, int part, const char *utf8, int index);   /* caret <-> x in a field */
int    lhnav_field_index(luxnav *, int part, const char *utf8, int x);
void   lhnav_text(luxnav *, uint32_t *px, int w, int h, int stride, int x, int y, const char *utf8, int bold, uint32_t rgb);
int    lhnav_text_size(luxnav *, const char *utf8, int bold, int *height);   /* the client's popups in luxnav's (GDI) text */
static inline void lhnav_paint_cairo(luxnav *, cairo_t *, int width, int composited);  /* LHNAV_WITH_CAIRO */
```
Differences from the sketch: `lhnav_pointer` returns int (needs-repaint), not void; the setters for frame state, fields and the
placeholder, `lhnav_extend_top`, the field caret helpers and `LHNAV_RESIZE_TOP` / `LHNAV_BUTTONS` / `LHNAV_OVERFLOW` were added.
Art: `TravelBack`/`TravelFwd::AnimationButton` (and the Inactive/Max/MaxInactive variants) through `ut_get_bitmap`, rows above
`CUSTOMSPLITRECT` (44), forward at `ANIMATIONBUTTONRECT.left` (38); the hot/pressed orbs without the menu arrow are synthesized
**at runtime** when the style is loaded (aap's todo asks for that): the NORMAL image with the HOT/PRESSED pixels copied in where
NORMAL is opaque (alpha 160..240 blended), so the drop-down box and the well stay out. Breadcrumb: the font, colour and glow of
`CompositedBreadcrumbBar::Toolbar` (aero-patch: Segoe UI -12, (0,2,92), white, TEXTGLOWSIZE 4), the segment hover/pressed box =
`BreadcrumbBarToolbar::Toolbar.SPLITBUTTON` HOT/PRESSED drawn by `ut_draw_background`, the separator = its `SPLITBUTTONHORZARROW`
glyph. Without a style: a drawn orb, triangle and box. Text on glass: the glyph coverage box-blurred twice (radius = glow size),
times `glow_strength` 2.5, in the glow colour, then the text; not composited: plain text in `plain_rgb` (the caption colour).

### Comparison with the concept (`examples/compare_navdemo*.png`) and the knobs
Measured on the concept (`1456384117.or.45878.png`, frame at 122,67, 799 px wide) against ours at the same place and width:
| | concept | ours | knob |
| --- | --- | --- | --- |
| header to the body's dark line | 51 rows | 48 (light 47, black 48) | the spec's 47; `nav.ini` `orb_h`/`gap` |
| back orb sphere | ~25-26 px, the well ~34 rows (13..47), forward stub to x ~58 | ~21-22 px, well 30 rows (15..44), stub to x ~52 | `orb_h` (37 gives the concept's sphere; tried live: works) |
| title | none (the breadcrumb is the title) | "All Music", Segoe UI 12 px black, baseline 15 | `title_font`, `title_rgb`; a client may pass no title |
| breadcrumb text | ~15-16 px Segoe UI, ink (14,64,128) steel blue, starts x ~100 after a 16 px Windows-flag icon | 12 px, (0,2,92) navy, starts x 70, no icon | `crumb_font`, `crumb_rgb`, `crumb_x`; an icon API is not there yet |
| separator | a small 4x7 blue triangle, no outline | the style's 9x9 black chevron with a white rim | (art: `sep_w` only) |
| search | a large magnifier icon (~20 px) and a favorites star orb (~32 px) at the right, no field | aap's presets: a 180 px field with a 10 px magnifier; `[preset concept]` (`tools = search favorites`): a 22 px magnifier and a 28 px star, the field only while searching | `tools`, `tool_size` (32 gives the concept's star), `search_w` |
| caption buttons | a ~13-row pill from the top edge, 25/26/37-39 px, small grey glyphs | rows 2..14, 27/25/43 (the plate's pill), aero glyphs | `button_*` in theme.ini, `tools/build_aerovision.py --drop-row` |
| glass | frosted, nearly opaque white-blue | aero's squared plate alpha: the sky shows through clearly | aero's `glass_alpha`/`glass_alpha_mode` in aerovision/theme.ini |

### Presets, hand placement, the big variant (2026-10-27, second pass)
aap: his layout was 2 px off and he wants to place things by hand; the medium/big variant as an option; more tightly designed sizes
from the concept art later (small/big x Aero/Slate vision becoming a few sizes).

**nav.ini structure** (`data/frames/aerovision/nav.ini`, every key commented there with its automatic value at 720 px): `preset =
NAME` at the top picks a `[preset NAME]` section; each section has all keys, so a new size is a copied section. Keys before the
first section apply to every preset (a section's keys win); no `preset` key = the first section; other `[...]` sections are skipped.
luxnav: `lhnav_metrics_load` (the file's preset), `lhnav_metrics_load_preset(m, path, name)`, `lhnav_presets(path, names, max)`;
`m->preset` says which was read. Keys per preset:
- `variant = small | big`; vertical `deco_h orb_h gap header_h box_h lines resize_rows`; horizontal `pad_l pad_r title_x box_gap
  search_w search_min crumb_pad sep_w crumb_x`; text `title_dy crumb_dy`; fonts, colours, glow, fields as before;
- **hand placement**, applied after the automatic layout, each element against its own automatic place (moving the orb does not
  move the boxes): `address_dy`/`search_dy` (the common case, at the top of each preset: the box moves, its text and segments
  with it; `crumb_dy` only moves the text), `address_dx`/`search_dx`/`orb_dx`/`orb_dy`; absolute `orb_x orb_y`, `address_x
  address_y address_w address_h`, `search_x search_y search_h` (x client pixels, negative = from the right edge; y frame rows; empty
  or `auto` = automatic); `title_dx` (+ title_x), `title_baseline` (frame row, default deco_h; + title_dy), `crumb_text_dx/dy` (the
  segment text inside its cell; cells and hover boxes stay); `header_h` (frame rows; default deco_h + orb row + gap).
- The `small` preset is aap's values unchanged: luxnav's offscreen render of it is byte-identical to the previous code (3 states).

**Live**: nav.ini is re-read on every luxwm theme reload (`theme-watch.sh` watches it; checked on `:3`: `address_dy = 3` moved the
box from frame row 19 to 22 without restarting). navdemo: `p` / `P` cycle the presets (header height and `_LHWM_EXTEND_FRAME`
follow; the title shows `[NAME]`), `NAVDEMO_PRESET=NAME` starts with one; editing the file's `preset =` wins over `p` again.
`NAVDEMO_DEBUG=1` prints every final rect (client x, y, frame row, size) incl. `back_menu`, `fwd_menu` and the title baseline.

**Big variant** (`variant = big`, preset `big`): the full `TravelBack`/`TravelFwd::AnimationButton` art, not cropped: 38x54 +
27x54, forward at x 38 (`ANIMATIONBUTTONRECT`), drawn unscaled (`orb_h = 0`: the art's height; another value scales it). The
aero-patch art: NORMAL/DISABLED have no drop-down (orb and well only), HOT/PRESSED show the drop-down box with the arrow below the
orb (rows ~36..53, the split `CUSTOMSPLITRECT.top` 44); `SPLITPRESSED` is used when the style has it (aero-patch: no, PRESSED
instead). The button and its arrow part are one image: hot when either is hovered (as 4074's browseui), pressed / split-pressed by
the part pressed. Measured on `:3` (720 px client, frame rows):
| | big |
| --- | --- |
| deco | rows 0..14, title baseline 15 (as small) |
| travel art | rows 15..68 (54), back x 0..37, forward 38..64 |
| `LHNAV_BACK_MENU` / `LHNAV_FWD_MENU` | rows 59..68 (art rows 44..53), x 3..37 / 38..62 (the hot art's opaque columns below the split; only while back / forward is possible) |
| boxes | rows 26..46 (21), centred against the orb part (rows 15..58); breadcrumb x 73, search 180 px at x 536 |
| header | 69 rows (15 + 54 + `gap = 0`): the drop-down box's last row is 68; plate light line row 69, black 70, body from 71 |
| `_LHWM_EXTEND_FRAME` top | 69 client rows (67 + 2 line rows); maximised 71 (border_t 0; MAXEXPLORER lines on 69/70 too) |
The frame needed nothing new: `header_mode = crop` copies the band 1:1 up to 80 rows (`luxwm/tests/regress.sh` unchanged: 169
identical + the known 11 aero5112).

**History list** (navdemo): a press on an arrow part opens an override-redirect list under it (`lhnav_rect(LHNAV_BACK_MENU)`) with
the back history newest first (or forward), up to 16, folder names; pointer and keyboard grabbed; release or click on an entry goes
there (`history_step` by its distance), Up/Down/Enter, Escape or a click outside closes it. The arrow keeps its split-pressed look
while the list is open. A right click on back / forward opens the same list in either variant.
`examples/compare_navdemo_variants.png`: the concept crop, small, big, big with the list open (frames at the same place, 799 wide).

### Open points
- Slate vision; flxp wrapping luxnav in a wux control (step 4).
- Big variant vs. the concept: the concept's orb (~25-26 px sphere) lies between small (~22) and big (~36): a preset with `variant =
  big` and `orb_h` ~ 40 (scaled art), or `variant = small`, `orb_h` 37, is the next size to try.
- Concept details not in luxnav yet: an icon before the first segment. (The favorites orb, the magnifier-only search and the
  segment drop-downs are in since the toolbar pass below.)
- Toolbar: the auto `tool_size` (28 small, 28 big: limited by the room around the boxes' row) is smaller than the concept's ~32 px
  star; `tool_size = 32` with `orb_h = 37` is the concept's size, untried as a preset. The 4074 Longhorn shell32 16 px folder
  (bookmark items) is the tall Longhorn folder, not XP's; `tool_icon = 24` uses the 24 px strip and a 24 px folder.
- lux's taskbar text treatment for Aero vision (the "text on glass" pack option) is not done.
- `caption_border = 2` leaves luxwm only the 2 outer rows for top resizing; luxnav adds 3 client rows (`resize_rows`) that ask for it
  with `_NET_WM_MOVERESIZE`. Whether 2 + 3 rows are enough is to be seen.
- The `art_dir` override (`nav_back.png`/`nav_fwd.png` strips) has no generator; luxnav synthesizes the orbs itself.

### Note (aap, 2026-10-27): travel-button sizes changed after 4051
The back/forward art was resized after 4051: 4074 Slate's buttons have the drop-down arrow part
not extending as far down (which goes together with the rendering bug under the 4074 title bar),
while 4074 Jade's still have the old 4051 size. Not acted on yet; relevant for a Slate vision
preset and for the faithful 4074 Slate explorer (flxp).

### Toolbar and breadcrumb drop-downs (2026-10-27, third pass)
**Toolbar** (luxnav): nav.ini `tools = ITEM ITEM ...` per preset (blanks or commas between items, `"..."` for an item with blanks),
the items left to right, right-aligned before `pad_r`, `tool_gap` apart, centred on the search box's row (after `search_dy`, so
aap's nudges carry them; `tools_dx`/`tools_dy` move them further, the field keeps `search_*`):
| kind | look | art (in this order) | client (navdemo) |
| --- | --- | --- | --- |
| `search_field` | today's box (`search_w`, shrinks first in narrow windows, `search_min`) | drawn field + magnifier | the text field (`LHNAV_SEARCH`, not a tool hit) |
| `search` | the magnifier glyph, 4/5 of `tool_size`, no box; hot: full strength + a soft white light behind; pressed: 1 px down | `lhnav_set_icon("search")`, else shell32.dll icon group 23 (32 px; flxp's `search_icon()`), else drawn | `lhnav_set_search_open(1)`: the field (search_w) comes out left of it; focus; closed again when it loses the focus empty |
| `favorites` | the star orb at `tool_size` (default: the orb part's height - 2, at most what fits around the row: 28) | the style's `Favorites::AnimationButton` (+ `Inactive`/`Max`/`MaxInactive`, state DISABLED/NORMAL/HOT/PRESSED), `SmallFavorites::` up to 35 px, as flxp's navbar.cpp finds them; else drawn | list of `$XDG_CONFIG_HOME/gtk-3.0/bookmarks` (read fallback `~/.gtk-bookmarks`) with "Add current folder" first (appended atomically, no duplicates; remote URIs listed greyed) |
| `bookmark:PATH[:LABEL]` | a folder icon button (`tool_icon` px + 8, box_h high); tooltip LABEL or the folder's name | `lhnav_set_icon("folder")`, else shell32 icon group 4, else drawn | goes to PATH (`~` expanded by the client) |
| `views` | XP's Views icon + an 11 px drop-down arrow part (whole drop-down: one part) | `"views"`/`"views_hot"`, else shell32's toolbar strips bitmap 216/217 (16 px; 214/215 24 px) cell 32 (XP's tb_sh_def/tb_sh_hot layout, flxp's `res/icons/*.theme` `views = 32`), else drawn | List / Details list; Details adds Size and Date Modified columns |
| `newfolder` | XP's New Folder icon button | `"newfolder"`, else the same strips, cell 31 (VIEW_NEWFOLDER) | "New Folder", "New Folder (2)", ... selected in the listing |
| `separator` | a 7 px cell with a vertical line (glass: white fading out at both ends, as flxp's divider; opaque: dark + light) | drawn | none (no hit) |
Icon buttons' hot/pressed box: the breadcrumb toolbar's `SPLITBUTTON` HOT/PRESSED (as a hot segment). `icon_dir` (nav.ini, else
navdemo passes the session theme's `path.resources`): `luxnav/lhnav_res.c` reads shell32.dll's RT_GROUP_ICON/RT_ICON (DIB or
PNG) and RT_BITMAP straight from the PE file (a small mmap'ed resource walker; no other new dependency).
`tools = search_field` is the look before the toolbar: the offscreen render (`small`/`big` x short/long path x rest, not composited,
back hot/pressed, address hot, search hot, search editing, crumb hot: 32 images) is byte-identical to the previous luxnav except the
breadcrumb-hover states, which now show the chevron after the last segment (a 6x9 glyph; `sep_last = 0` makes all 32 identical).
aap's presets got the new keys with their default values only (`tools = search_field`, `tool_size = 0`, `tool_icon = 16`,
`tool_gap = 4`, `tools_dx/dy = 0`, `sep_last = 2`, `icon_dir =`); `[preset concept]` (a copy of `small` with `tools = search
favorites`) was added at the end of nav.ini.

**Breadcrumb drop-downs** (luxnav provides the parts and the look, the lists are the client's): every chevron is `LHNAV_SEP + i`, the
drop-down of segment i (the chevron after "«" is `LHNAV_SEP + hidden - 1`; the one after the last segment exists where it fits and
shows with `sep_last` 1, or 2 = while the breadcrumb is hovered); `LHNAV_OVERFLOW` ("«") is a drop-down too. Hot: the style's
`SPLITBUTTONDROPDOWN` HOT over the chevron cell and the segment's `SPLITBUTTON` HOT (the split pair); pressed or open
(`lhnav_set_open(n, part)` while the client's list is up): both PRESSED and the chevron turned down (the second image of
`SPLITBUTTONHORZARROW`'s glyph strip, aero-patch: a down arrow); without the style's art the drawn box and triangle.
navdemo: a press on a chevron lists segment i's subfolders (sorted case-insensitively, dot folders left out, at most 500) under the
breadcrumb box, segment i + 1's folder bold and selected; "«" lists the collapsed ancestors, the nearest first. One list window
(the history list's, generalised: override-redirect, pointer and keyboard grabbed, at most 24 lines then it scrolls, wheel too):
Up/Down/Home/End, a first letter, Enter or a click picks, Escape or a click outside closes. Note the difference from flxp, whose
arrow after a crumb lists the crumb's *siblings* (its parent's subfolders, the crumb bold: 4074's lh4074-224731.png); this one is
Vista's (the folder's children, the next segment bold). luxnav does not care: the part is the same, the content is the client's.

**Tooltips**: `lhnav_tip(n, part)` gives the text (Back, Forward, Recent pages, Search, Favorites, a bookmark's label, Views, New
Folder, Folders above, "Folders in NAME" for a chevron); navdemo shows an XP-style tooltip window (infobk, black frame) after 500 ms
of hover, hidden on a press, a key or leaving the part.

**API additions** (lhnav.h):
```c
LHNAV_SEP + i, LHNAV_TOOL + i;  LHNAV_IS_CRUMB(p) LHNAV_IS_SEP(p) LHNAV_IS_TOOL(p)   /* CRUMB 100.., SEP 10000.., TOOL 20000.. */
LHNAV_TOOL_SEARCH, _SEARCH_FIELD, _FAVORITES, _BOOKMARK, _VIEWS, _NEWFOLDER, _SEPARATOR
lhnav_metrics: tools[512], tool_size, tool_icon, tool_gap, tools_dx, tools_dy, sep_last, icon_dir[512]   /* nav.ini keys */
int    lhnav_set_tools(luxnav *, const char *spec);            /* replaces m.tools (lhnav_set_metrics rebuilds from m.tools) */
int    lhnav_tool_count(luxnav *);
int    lhnav_tool(luxnav *, int i, const char **arg, const char **label);   /* kind; bookmark path and label */
const char *lhnav_tip(luxnav *, int part);
void   lhnav_set_search_open(luxnav *, int open);  int lhnav_search_open(luxnav *);
void   lhnav_set_open(luxnav *, int part);         int lhnav_open(luxnav *);  /* the part whose list is up: drawn pressed */
void   lhnav_set_icon(luxnav *, const char *name, const uint32_t *px, int w, int h);  /* "search" "folder" "views" "newfolder" (+_hot) */
```
`lhnav_rect` answers `LHNAV_SEP + i` and `LHNAV_TOOL + i`; `LHNAV_SEARCH` is 0 while no field is out (a `search` item closed).
Clients that tested `part >= LHNAV_CRUMB` must use `LHNAV_IS_CRUMB` now (the chevrons and tools lie above it).
Comparison: `examples/compare_navdemo_tools.png` (the concept crop, then on `:3`, frames 799 wide: aap's `search_field`, `[preset
concept]`, the magnifier's field out, `newfolder views separator bookmark search favorites`, an open chevron list in small and in
big). `luxwm/tests/regress.sh`: unchanged (169 identical + the known 11 aero5112); no luxwm change.

### Text and pixels without pango and cairo (2026-10-27, fourth pass)
Decision (docs/history/THEMES.md "Request for the wux side"): all desktop text goes through libuxtheme's GDI-faithful text, luxnav first, so
flxp can link libluxnav directly and its text looks exactly like flxp's.
- **Dependencies**: `libluxnav.so` needs `libuxtheme.so.1` and `libpng16` (plus libc/libm); `make -C luxnav flags` prints the client
  flags. `(gone: the top-level build)` picks libuxtheme: flxp's build tree `~/src/flxp` (`src/uxtheme/ut.h` via `-idirafter`,
  `build/uxtheme/libuxtheme.so.1` with rpath) when it is there (`UT_TREE=DIR` for another), else what luxwm's `uxtheme-flags.sh`
  finds (`~/.local` ...); it defines `UT_HAVE_TEXT_EXTENT` when that `ut.h` has the text calls. `UXTHEME=no`: drawn art, no text.
- **Text path**: every string (title, segments, "«", the fields, the placeholder, and the client's tooltips / lists through the
  new `lhnav_text` / `lhnav_text_size`) is rendered once as an 8-bit coverage mask with `ut_draw_text_mask`, composited in luxnav's
  colour (nav.ini / the style's breadcrumb colour); on glass the same mask is box-blurred twice (glow size), times
  `glow_strength`, in the glow colour, under the text. Fonts: the breadcrumb's is the style's part font
  (`CompositedBreadcrumbBar::Toolbar` FONT, aero-patch Segoe UI -12; no such class: Segoe UI 12 px); nav.ini `crumb_font` /
  `title_font` (pango's form, `Segoe UI, 14` = 14 pt = 19 px) become explicit `ut_font`s (`ut_draw_text_mask(..., font)`,
  `ut_text_extent_font`); the placeholder is the crumb font with `italic` (GDI's synthesized italic), bold list items the crumb
  font at weight 700. Widths: `ut_text_extent` / `ut_text_extent_font` (advance, `DT_SINGLELINE | DT_NOPREFIX`), cached per (font,
  text) in a 512-entry table; the cell: `ut_get_text_metrics` (ascent, height): a text cell is centred in its box with floor, as
  the pango layout was, the title's baseline is `title_base`. Ellipsis: `DT_END_ELLIPSIS` (GDI's "...") in the segment / title
  width. Caret positions: the advance of the prefix.
- **Interim fallback** (an installed libuxtheme without the text calls, i.e. `~/.local` before the wux library is installed): text
  drawn with `ut_draw_text` into a scratch ARGB buffer (its alpha is the coverage); widths by drawing `text + "H"` and `"H"` and
  taking the difference of the H's right edges (exact GDI advances, trailing blanks included); the cell from "H" drawn with
  `DT_TOP` and `DT_BOTTOM`; nav.ini fonts mapped to the closest theme part font (a log line names it; `Segoe UI, 14` -> the style's
  `Start::Button` Segoe UI -19, exact), the placeholder slanted by luxnav. One `#ifdef UT_HAVE_TEXT_EXTENT` in `lhnav.c`.
- **Pixels** (`lhnav_px.c`, plain C): images, OVER with pixman's rounding (`x*a + 0x80`...), blits 1:1 / from a source rect / with
  opacity, scaling (box filter below 0.75, bilinear above: cairo's GOOD filter makes the same choice), A8 masks in a colour, solid,
  linear and radial (two-circle, PAD) paints, and shapes from a signed distance: rounded rectangles, circles, polygons (nonzero),
  strokes (butt or round caps); edge pixels sampled 32 x 15 at the sample rows' tops with the top-left rule, as cairo's scan
  converter does. PNG (the `art_dir` strips, shell32's PNG icons) through libpng's classic API, premultiplied as cairo's loader.
  `lhnav_paint_cairo` stays the header-only helper (`LHNAV_WITH_CAIRO`; only it includes cairo.h).
- **navdemo** keeps cairo and pango for its own window (body list); its drop-down lists and the tooltip draw their text with
  `lhnav_text` (flxp's look), and it registers the theme's font directory with fontconfig itself for the body list (luxnav no longer
  does). Its Makefile links libluxnav with `make -C ../lhnav flags`.

**Equivalence** (offscreen, aerovision nav.ini presets `small`, `big`, `concept` x a short and a long path (with "«") x rest,
back/forward hot and pressed, address hot, crumb hot / pressed, chevron hot / open, "«" hot, inactive, tool hot / pressed, the big
variant's menu hot / open, placeholder, search text, search editing, address editing; composited and plain: 159 renders, old =
`aa5adfb` with pango/cairo, new = this; text switched off in both by a test-only hook for the non-text comparison):
- geometry: every rect luxnav reports (title, orb, boxes, every segment, chevron, tool, the menus), `lhnav_hidden`, the title baseline,
  `lhnav_height`, `lhnav_field_x/_index`: identical in all 159 (GDI's and pango's Segoe UI 12 px advances agree for these strings);
- non-text pixels: theme art at 1:1 (the big variant's travel art, SPLITBUTTON boxes, chevrons, field interiors and straight edges,
  the caret) identical; the scaled art (the small orb 30/44, the concept's star and magnifier icons) differs by at most 1-2 levels
  of 255 (filter rounding) on 135-600 pixels; the drawn shapes differ at antialiased edge pixels only: the field boxes' rounded
  corners (about 28 pixels per box, at most 11 levels) and the drawn magnifier of the search box (at most 21 levels): cairo
  flattens arcs to Bézier polygons, lhnav_px takes exact circles;
- text: ink boxes of every text item (title, segments, "«", search text, address text) old vs new: left, top and bottom within 1 px
  everywhere (baselines identical in most), the right edge up to 4 px (glyph shapes and GDI's "..." vs pango's "…").
On `:3` the crumb cells in navdemo can differ by a pixel where GDI's and pango's advances differ for a string (the cells follow
the text). `examples/compare_navdemo_gditext.png`: concept crop, old (pango), new (GDI), frame rows 0..49 x 0..419, 4x.
Visually the GDI text is crisper: stems on whole pixels (full-strength verticals in "m", "l", "d"), darker and narrower, with
GDI's integer advances (spacing slightly uneven, e.g. "sm all"); pango's was softer (slight hinting, fractional positions).

## Where the art comes from (2026-10-28)

aap under the 5048/5112 themes (navbar `vision`): "travel buttons with the arrows flipped horizontally, and I don't know where these
even come from". They were luxnav's drawn orbs: 5048's and 5112's `aero.msstyles` have no `TravelBack/TravelFwd::AnimationButton`,
they have Vista's `TravelBand`, which luxnav did not read; and the drawn orbs had their arrows mirrored. Now luxnav reads TravelBand,
the drawn arrows point the Windows way (back left, forward right), a style without travel art is logged once
(`lhnav: STYLE has no travel art: no TravelBack::AnimationButton, TravelFwd::AnimationButton (4051-4074, aero patch) / no TravelBand
(Vista 5048, 5112); drawn orbs ...`), and every piece's source can be listed: `LHNAV_DEBUG=1` (any client, on every art load) or
`NAVDEMO_DEBUG=1 examples/navdemo` (with the layout), through `lhnav_art_sources()`, e.g.

```
navdemo: art sources (nav.ini ~/.local/share/lhde/navbar/vision.ini)
  style: ~/.local/share/lhde/styles/aero5048.msstyles
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

| Element | 4051/4074 styles, aero patch (`aero-patch`) | Vista TravelBand (5048, 5112) | nothing usable |
| --- | --- | --- | --- |
| back / forward | `TravelBack`/`TravelFwd::AnimationButton` ABBACKGROUND IMAGEFILE, DISABLED NORMAL HOT PRESSED (+SPLITPRESSED), Inactive/Max/MaxInactive variants; small: the rows above `CUSTOMSPLITRECT.top`, scaled to `orb_h`, HOT/PRESSED synthesized inside the orb; forward at `ANIMATIONBUTTONRECT.left` | `TravelBand` TRAVELBUTTONS: one image per state, back = left half, forward = right half; each button in its own state; unscaled | drawn orbs (38x44 + 27x44 at 38), scaled like the art |
| plate behind them | (part of the button art) | `TravelBand` TRAVELBACKGROUNDS, unscaled, centred on the orb part; its state is the chevron's (DISABLED: neither direction possible) | none |
| history drop-down | big: the art's rows below `CUSTOMSPLITRECT.top` (`LHNAV_BACK_MENU`, `LHNAV_FWD_MENU`); small: none | the plate's chevron (`LHNAV_BACK_MENU`, either direction), from where the HOT plate's box starts | none |
| favorites star | `Favorites::AnimationButton` (+variants), `SmallFavorites::AnimationButton` up to 35 px | (5048/5112 have none) drawn | drawn star |
| breadcrumb arrow | `BreadcrumbBarToolbar::Toolbar` / `BB::Toolbar` / `Toolbar` SPLITBUTTONHORZARROW GLYPHIMAGEFILE (image 2: open) | the same classes (5048: `BreadcrumbBarToolbar::Toolbar`) | drawn triangle |
| segment / arrow boxes | the same class's SPLITBUTTON / SPLITBUTTONDROPDOWN, HOT PRESSED | the same | drawn rounded boxes |
| breadcrumb colours, glow, font | the **theme's** style: `CompositedBreadcrumbBar::Toolbar`, else `BreadcrumbBarToolbar::Toolbar`, `BB::Toolbar` (never from `art_style`) | the same | (0,2,92), white glow 4 |
| tool icons (search, bookmark, views, new folder) | `icon_dir`'s shell32.dll: icon groups 23 and 4, bitmaps 216/217 (214/215 at 24 px) cells 32 and 31; the client's `lhnav_set_icon` first | the same | drawn |
| search box's magnifier, fields, separator line | drawn | drawn | drawn |

**TravelBand** (read with `ut-dump --list STYLE TravelBand`, `--render`): 5048 plate 61x28 (one long well, the chevron at columns
54..58; HOT/PRESSED light a box around it, DISABLED drops it), glyphs 36x24 (two 18x24 orbs; blue, HOT and PRESSED green, DISABLED
grey back and an *empty* forward: 5048 hides forward when it is impossible, as its Explorer screenshot shows); 5112 plate 70x27 (two
round wells, `CONTENTMARGINS 0 13 2 0`), glyphs 54x27 (two 27x27 blue orbs, DISABLED ghosted). Composition as browseui: the plate,
then each half of the glyph strip in its button's state. Placement: with CONTENTMARGINS each button is centred in its half of the
content width (5112: back at x 1, forward at 29, the wells' centres); without (5048) the strip sits at x 4, y 2, measured on
`~/fun/longhorn/screenshots/WindowsVista-6.0.5048-Explorer.png` (plate at 8,25, back glyph at 12,27; template match, mean error 2 of 765). The band is never
scaled: small preset: centred in the 30-row orb row; big preset: centred on the 44-row orb part of the 54-row art the big preset is
measured on (header 69 rows as with the aero patch). 5112's TRAVELBUTTONS(DISABLED) bitmap comes out of libuxtheme with straight
alpha (colour above alpha in 976 pixels); luxnav premultiplies such an image and logs it (a libuxtheme matter).

**`art_style`** (nav.ini, global or per preset; `data/navbar/vision.ini` sets `auto`): `auto` = the theme's own style; a NAME =
`styles/NAME.msstyles` in lhde's data directories (`lhde_data_find`), e.g. `art_style = aero-patch` for "luxnav with the patched
aero resources" under 5048/5112; an absolute path = that file. It replaces the travel, favorites and breadcrumb art; text (fonts,
colours, glow) stays the theme style's. A name that is not found or not a style falls back to `auto` with a log line. Which looks
better for 5048/5112: the small preset with `auto` (crisp unscaled Vista orbs, the real 5048/5112 look) is closer to the builds;
the big preset is drawn for the aero patch's 54-row orbs and looks emptier with the 27/28-row band, so there `aero-patch` fills it
better (sheets: `aero5048-auto-vs-aeropatch.png`, `aero5112-auto-vs-aeropatch.png` of the 2026-10-28 session).

Clients: navdemo reads the frame set's `nav.ini` and, when the frame set has none and the theme's `Explorer.navbar` is `vision`
(aero5048, aero5112), the theme's `path.navbar` (`navbar/vision.ini`). flxp's header (`vision_wanted`) still reads the frame
set's `nav.ini` only, so under 5048/5112 it runs on lhnav's defaults (the small layout, `art_style` auto) until it does the same.

## Porting luxnav into flxp
flxp's header (`~/src/flxp/src/explorer/header.cpp`, with `navbar.cpp` for the theme's navigation-bar skin and `lhbar.cpp` for the
4051 command bar) would keep its Win32 child window and its behaviour, and hand the header's *contents* to luxnav.

**Delegated to luxnav** (one `luxnav` per header, `lhnav_paint` into a 32-bit premultiplied DIB section, which has cairo's ARGB32
layout, then `AlphaBlend` over the background; no cairo or pango comes with it):
- layout of the travel buttons, title, breadcrumb, search box and the right-hand items; `right_layout()`, `crumb_limit()`,
  `layout()`'s segment fitting and "..." collapsing (luxnav's "«"), the separators' places (`arrow_*_gap`);
- painting: `paint_nav` (TravelBack/TravelFwd::AnimationButton with the Inactive/Max/MaxInactive variants, disabled/normal/hot/
  pressed/split-pressed), the title and crumb text with the glow on glass (`FlxpDrawGlowTextW` use there), the segments' hot/pressed/
  open split-button art (BreadcrumbBarToolbar/BB::Toolbar SPLITBUTTON, SPLITBUTTONDROPDOWN, SPLITBUTTONHORZARROW: flxp's
  `crumb_btn/crumb_drop/crumb_down`), `paint_right`'s magnifier and star (`Favorites::AnimationButton`, `SmallFavorites`) and the
  `set_tools` glyph buttons (`paint_tools`): New Folder and Views become `newfolder`/`views` items, their icons through
  `lhnav_set_icon` from flxp's icon theme (`uiicons`), or luxnav reads them itself from `longhorn_system32` as `icon_dir`;
- hit-testing and the hover/pressed state machine: `part_at`, `enabled`, `part_state`, `g_nav_hot`/`g_nav_pressed`, the crumbs'
  hit test, `g_tool_hot`/`g_tool_pressed` -> `lhnav_hit`/`lhnav_pointer`; WM_NCHITTEST maps `LHNAV_CAPTION`/`LHNAV_TITLE` to
  HTTRANSPARENT (the frame's caption) and everything else to HTCLIENT; drop-down parts (`BACK_DROP`/`FWD_DROP` -> `LHNAV_BACK_MENU`/
  `_FWD_MENU`, a crumb's arrow -> `LHNAV_SEP + i`, Favorites, Views) open on WM_LBUTTONDOWN, the rest act on WM_LBUTTONUP;
  `lhnav_set_open` while flxp's menu is tracked replaces flxp's "open" crumb state;
- tooltips' text (`lhnav_tip`) for flxp's comctl32 tooltip control (whose GDI text is the same as `lhnav_text`'s);
- the Small/Medium header geometry where an luxnav preset matches it (the vision presets; see "missing" below).

**Stays in flxp**:
- everything a click does: the history and `history_menu` (Back/Forward lists), the namespace chain for the segments
  (`set_folder`: Computer as the first segment, virtual folders, `shell::Folder`), the crumb drop-down's menu (TrackPopupMenu,
  SetMenuDefaultItem; flxp keeps 4074's sibling list or switches to the children list, luxnav does not care), the bookmarks menu
  (`g_cb.favorites`: Add to Bookmarks, Organize Bookmarks..., `shell/bookmarks.cpp`), New Folder (`shell/fileops`), the views and
  their per-folder memory, search filtering (`g_cb.filter`/`done`);
- the edit controls: the address and search EDITs stay Win32 EDITs, placed at `lhnav_rect(LHNAV_ADDRESS / LHNAV_SEARCH)`; while an
  EDIT is shown luxnav gets `lhnav_set_field(part, NULL, -1, 0)` (box only, no text) or the part is skipped (the Win32 EDIT paints
  itself); `open_search()` -> `lhnav_set_search_open(1)` + relayout;
- the background under luxnav: glass, the theme's navigation-bar band (`navbar::Skin` bg nine-grid, gripper, splitter,
  `paint_bottom`'s last row), luxwm's `_LHWM_HEADER_PIXMAP`, the Slate gradient (`choose_mode`, `paint_pixmap`);
- the 4051 command bar (`lhbar.cpp`), a rebar band below the header: unchanged;
- settings and menus: `header_size`, the element toggles (`header_buttons/_title/_breadcrumb/_search/_favorites` become the preset
  choice, `lhnav_set_title(NULL)` and the `tools` list built from them), `header_button_images`, View > Header;
- luxwm protocol work (`DwmExtendFrameIntoClientArea`, CLIENT_CAPTION, `_LHWM_FRAME_METRICS` -> `lhnav_metrics` border_t, deco_h,
  caption rect) and the theme reload (`theme_changed` -> `lhnav_set_style`/`lhnav_set_metrics`).

**Missing in luxnav for a full switch** (to do before or while porting):
- flxp's 4074 Medium/Large/Extra-Large layout (title on its own row above the breadcrumb, 4074-dwm.png, the right items at the
  band's places or the bottom) is not an luxnav layout; luxnav has the vision rows only (title in the deco, boxes beside the orb).
- the even small travel art (`SmallTravelBack/Fwd::AnimationButton`) and flxp's uneven shrink (`navbar::shrink`, the drop-down rows
  kept 1:1): luxnav scales the full art's orb rows and synthesizes orb-only hot/pressed images instead;
- the Avalon framelets (`Microsoft.Windows.Client.dll` .resources) as travel art: luxnav takes `art_dir` PNG strips only; an API
  to hand it pixels (like `lhnav_set_icon`) would let flxp keep its netres reader;
- the breadcrumb field's 15 px drop button (flxp: the back history) and the field look of 4074 (darker field, rules);
- a generic icon item for arbitrary host buttons (flxp's `set_tools` takes any `{id, icon, dropdown}`): luxnav has the fixed kinds;
- (done 2026-10-27) fonts and linking: luxnav's text is libuxtheme's GDI text and libluxnav links only libuxtheme and libpng, both of
  which flxp links already: no cairo, no pango (flxp's GDI faces from luxwm's theme.ini `caption_font` map to `title_font` /
  `crumb_font`, now explicit `ut_font`s). flxp links `libluxnav.a` with the flags of `make -C luxnav flags`, or compiles the three
  C files itself.

## flxp integration (2026-10-27)
flxp's header (`~/src/flxp/src/explorer/header.cpp`, NOTES "The vision header") now uses luxnav: its Makefile compiles the three
C files from `LHNAV_DIR` (default this directory) against flxp's own `src/uxtheme/ut.h`; `header_engine = auto` picks luxnav when
`_LHWM_THEME` has a nav.ini (aerovision), else flxp's 4051/4074 header (byte-identical to before). One luxnav per header (explorer,
Longhorn file dialog: `newfolder views` prepended to `tools`, the small preset for the dialog's Small size); flxp keeps history,
menus (chevron = 4074's siblings list, `header_crumb_menu = children` for this file's), the address/search EDITs over
`lhnav_rect`, bookmarks, views, New Folder. Measured on a lab display next to navdemo at 720 px: every luxnav rect identical,
frame rows 0..70 (big) / 0..48 (small, maximised 49) identical outside the text, hover/pressed orb identical. Still missing in
luxnav for the faithful layouts: the list under "Missing in luxnav for a full switch" above, plus a disabled state for tool items.
