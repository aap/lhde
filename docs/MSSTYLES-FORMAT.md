# `.msstyles` visual style formats (XP v3 and Longhorn v4)

> **Imported from the Longhorn X11 lab** (`~/fun/longhorn/x11` at d353b69, MSSTYLES-FORMAT.md) into lhde in step 3 of docs/LAYOUT.md. Program and path names are updated to lhde (lhwm → luxwm, lhwmc → luxwmc, lhshell → luxshell, lhnav → luxnav; x11/... and lhwm/themes, lhshell/themes, lhshell/assets → the lhde tree and data directories, see docs/LAYOUT.md and tools/lhde-import); the X atoms (`_LH_*`, `_LHWM_*`, `_LHSHELL_*`), the window manager name "lhwm", luxnav's C API (`lhnav_*`, `lhnav.h`), configuration paths (`~/.config/longhorn`, `~/.config/lhwm/keys`, `~/.config/lhshell`) and setting values (`header_engine = lhnav`, `classic_start_menu = lhshell`) keep their names. Dated sections are history: they describe the x11 tree as it was (its compare_*.png and screenshots stay in the research workspace).

Implementation-ready specification for a C loader. Everything below was checked against the real files with
`tools/peres.py`, `tools/lhstyle.py`, `tools/msstyles2theme.py` and independent scratch parsers; statements that
are inference rather than observation are tagged **[inferred]** or listed in section 9. Byte order is little-endian
everywhere. "Resource" always means a Win32 PE resource `(type, name, language)`; every resource in every file has
language 1033, so the language can be ignored (use "first language found").

Files used for verification (paths relative to `/u/aap/fun/longhorn`):

| tag | file |
| --- | --- |
| XP | `/u/aap/.wine/drive_c/windows/resources/themes/Luna/luna.msstyles` (real XP SP, 2 877 960 bytes of BITMAP) |
| 4042 | `wim/4042/2/WINDOWS/Resources/Themes/{Luna,Slate}/*.msstyles` |
| 4051 | `wim/4051/2/WINDOWS/Resources/Themes/Luna/luna.msstyles` |
| 4066 | `wim/4066/expanded/{jade,slate,luna}.msstyles` |
| 4074 | `wim/4074/3/WINDOWS/Resources/Themes/{Slate,Jade,Luna}/*.msstyles` |
| 5048 | `wim/5048/2/Windows/Resources/Themes/aero/aero.msstyles` |
| schema | `wim/sdk4051/x/Tmschema_h.A91BE3D6_...` (also `res/sdk4051/`), the XP-level `tmschema.h` shipped with the 4051 SDK |

## 1. Overview

| build | format (`PACKTHEM_VERSION`) | resource types present | what differs |
| --- | --- | --- | --- |
| XP (2600) | **3** (`03 00`) | `BITMAP` (string names, e.g. `BLUE_BUTTON_BMP`), `TEXTFILE` (ini text, UTF-16LE), `STRING`, `COLORNAMES`, `SIZENAMES`, `COMBODATA`, `FILERESNAMES`, `ORIGFILENAMES`, `MINDEPTH`, `PACKTHEM_VERSION`, `VERSION` | Text format. Bitmaps 1/4/8/24/32 bpp, straight alpha. This is what Wine's uxtheme reads. |
| 4042 (Lab06) | **4** (`04 00`) | `BITMAP` (integer ids), `BLOB` (PNG), `STRING`, `VERSION`, `PACKTHEM_VERSION`, `MINCOLORDEPTH`, `STBL` (`STBL01`, Luna also `STBL02`), `CMAP`, `BCMAP`, `RMAP`, `VMAP`, `VARIANT` | Binary format. Property type 213 is named `BLOB`; its only property is `IMAGEDATA` (id 6000); used by class `Sidebar` only (60 records per variant). No INTLIST records. |
| 4051 | 4 | same as 4042 | Luna adds class `AvalonButton` whose properties (8000..8023) are defined in `STBL02`. Luna: 657 BITMAP, 180 BLOB, 9 VARIANT. |
| 4066 | 4 | `BLOB` is renamed `STREAM`; everything else as 4074 | Type ids 213 `BAMLSTREAM`, 214 `D3DXSTREAM`, 215 `PNGSTREAM`. Jade gets classes `CompositedWindow` (stream ids 479..504; 2 lower than in 4074, section 8.2) and `PreviewPane`. |
| 4074 | 4 | `BITMAP`, `STREAM` (PNG / `.x` / BAML), `STRING`, `VERSION`, `PACKTHEM_VERSION`, `MINCOLORDEPTH`, `STBL01` (+`STBL02` in Luna), `CMAP`, `BCMAP`, `RMAP`, `VMAP`, `VARIANT` | Reference build for this document. Property 6000 is now `TRANSITIONDURATIONS` (INTLIST). Jade has `CompositedWindow` (25 stream records), Slate does not. |
| 5048 | 4 | `BITMAP`, `STRING`, `VERSION`, `PACKTHEM_VERSION`, `MINCOLORDEPTH`, `STBL01`, `STBL02` (28 bytes), `CMAP`, `BCMAP`, `RMAP`, `VMAP`, `VARIANT` | **No STREAM/BLOB resources at all.** Type ids renumbered (213 `D3DXSTREAM`, 214 `PNGSTREAM`, no BAML). `MENU`, `STARTPANEL`, `WINDOW` part tables changed. Colour name is `NormalColor`. |

Longhorn's own `uxtheme.dll` (4066) contains the wide strings `PACKTHEM_VERSION`, `VARIANT`, `VMAP`, `CMAP`, `BCMAP`,
`RMAP`, `STBL`, `STREAM`, `MINCOLORDEPTH`, `sizevariant`, `colorvariant`, `globals`, `sysmetrics`, `documentation` and
**none** of `COLORNAMES`, `SIZENAMES`, `TEXTFILE`, `THEMES_INI`, `FILERESNAMES`, `COMBODATA` (checked with a byte
search). So the two formats are separate code paths; v4 files are not readable by an XP-format loader and vice versa.

**Detection:** open resource `(type "PACKTHEM_VERSION", name integer 1)`; it is 2 bytes, `03 00` = XP text format,
`04 00` = Longhorn binary format. (Wine's `MSSTYLES_OpenThemeFile` rejects anything but 3, `#define MSSTYLES_VERSION 0x0003`
in `msstyles.c`.)

## 2. PE container (both formats)

Standard PE32 (`MZ`, `e_lfanew` at 0x3C, `PE\0\0`), optional header magic 0x10B, resource directory = data directory
entry 2. The directory is the usual three levels: type, name/id, language. Entries with the high bit set in the name word
are *named* (offset to a counted UTF-16 string); otherwise integer ids. Observed naming:

* Types: integer `2` (RT_BITMAP), `6` (RT_STRING), `16` (RT_VERSION); everything else is a **string-named custom type**
  (`"VARIANT"`, `"STBL"`, `"CMAP"`, `"TEXTFILE"` ...).
* Names inside custom types: strings for `STBL` (`"STBL01"`, `"STBL02"`), `VARIANT` (`"NORMALDEFAULT"` ...), `CMAP`/`BCMAP`/
  `RMAP`/`VMAP` (`"CMAP"` etc., single entry), `TEXTFILE` (`"NORMALBLUE_INI"`), `COMBODATA` (`"COMBO"`); integer `1` for
  `PACKTHEM_VERSION`, `MINCOLORDEPTH`, `MINDEPTH`, `COLORNAMES`, `SIZENAMES`, `FILERESNAMES`, `ORIGFILENAMES`, `VERSION`;
  integer ids for `STREAM`/`BLOB` (e.g. 482) and for RT_BITMAP in v4. RT_BITMAP in XP uses string names.
* Resource data is the raw bytes at the resource's RVA (no extra header). Use `rva -> file offset` through the section
  table (`/u/aap/fun/longhorn/themes/tools/pe_resources.py` is a short reference).

RT_STRING is the standard format: block id `b` (name), 16 entries per block, each `u16 length_in_chars` followed by
`length` UTF-16LE chars (no NUL); string id = `16*(b-1) + index`; length 0 = absent.

## 3. XP format (v3), as Wine reads it

Wine source: `/u/aap/src/flxp/vendor/wine/uxtheme/{msstyles.c,uxini.c,system.c}`.

1. `PACKTHEM_VERSION` must be 2 bytes `03 00`.
2. `COLORNAMES` (`NormalColor\0Metallic\0HomeStead\0\0`) and `SIZENAMES` (`NormalSize\0LargeFonts\0ExtraLargeFonts\0\0`):
   UTF-16LE, NUL-separated, double-NUL terminated. The first entry is the default. Match case-insensitively.
3. `FILERESNAMES`: same NUL list of `TEXTFILE` resource names, **colour-major**: `NORMALBLUE_INI, LARGEFONTSBLUE_INI,
   EXTRALARGEBLUE_INI, NORMALMETALLIC_INI, ...` (9 entries for Luna). Index = `nSizes * colorIndex + sizeIndex`
   (Wine: `dwResourceIndex = (dwSizeCount * dwColorNum) + dwSizeNum`). Example: Metallic + LargeFonts = index 4 =
   `LARGEFONTSMETALLIC_INI`.
4. The chosen `TEXTFILE` resource is the complete, already substituted ini for that colour+size (UTF-16LE, no BOM, CRLF,
   about 2330 lines / 129 KB for Luna). `THEMES_INI` (the other TEXTFILE) holds `[documentation]`, `[ColorScheme.*]`,
   `[Size.*]`, `[Subst.*]` and `[File.<variant>]` (`FileName`, `ColorSchemes`, `Sizes`) which `msstyles2theme.py` uses to find
   the variant; Wine does not parse `[File.*]` (it uses `FILERESNAMES`).
5. Ini syntax: `;` comments (also trailing), `[Section]`, `key = value`, values are comma/space separated integers
   (`SizingMargins = 28, 35, 9, 17`), colours `r g b`, bools `True/False`, fonts `Trebuchet MS, 10, bold`. Section names:
   `[class]`, `[class(state)]`, `[class.part]`, `[class.part(state)]`, each optionally prefixed `app::`. Class/part/state
   names resolve to ids through the `tmschema.h` tables (Wine: `stylemap.c`). Special sections: `[globals]` (class
   `globals`, base of every class) and `[SysMetrics]` (system colours, fonts, sizes).
6. Property lookup (Wine `MSSTYLES_FindProperty`): exact (part,state) in the class and its override chain
   (`app::class` -> `class` -> `globals`); then (part,0); then (0,0). Duplicates: the first one parsed wins.
7. Images: the ini value `ImageFile = Blue\FrameCaption.bmp` maps to the RT_BITMAP **string name** with `\ / .` replaced
   by `_`, upper-cased: `BLUE_FRAMECAPTION_BMP` (Wine `MSSTYLES_LoadBitmap`). Bitmap depths in Luna: 24 bpp x315, 8 x165,
   32 x60, 4 x12, 1 x6 (558 total). 32-bpp bitmaps carry **straight** alpha (rgb > a occurs); uxtheme only uses it if some
   alpha != 255, and premultiplies on load (Wine `prepare_alpha`). Otherwise `Transparent=True` + `TransparentColor`
   (default 255 0 255, magenta) is a colour key. Alpha 0/255-only images are treated as opaque by XP.
8. `STRING` resource ids (Wine `system.c`): `1000+i` colour display name, `2000+i` colour tooltip, `3000+i` size display
   name, `4000+i` size tooltip, `5000..5007` = DISPLAYNAME, TOOLTIP, COMPANY, AUTHOR, COPYRIGHT, URL, VERSION, DESCRIPTION.
   `6000..` are localisation overrides of the form `NormalBlue@[SysMetrics]CaptionFont=Trebuchet MS, 10, bold`; Wine
   ignores them.
9. `COMBODATA/COMBO` (22 bytes = 11 u16: `3 3 | 0 3 6 1 4 7 2 5 8`) looks like `[nColors][nSizes]` then
   `map[size*nColors+color] = file index` **[inferred; Wine ignores it]**. `MINDEPTH` = u16 per file (all 0x000F) +
   terminating 0; `ORIGFILENAMES` = NUL list of original ini paths. Neither is needed.

To adapt Wine's reader to Longhorn you do not touch this part; you add a second loader (section 4) that produces the same
`THEME_CLASS -> THEME_PARTSTATE -> THEME_PROPERTY` structures (see section 10 for how to hand over the values).

## 4. Longhorn-compiled format (v4)

### 4.1 Resource inventory (4074 Slate/Jade; deviations in section 8)

| resource `(type, name)` | content | notes |
| --- | --- | --- |
| `(PACKTHEM_VERSION, 1)` | u16 = 4 | |
| `(MINCOLORDEPTH, 1)` | u16 = 15 | = `[SysMetrics] MinColorDepth` of XP |
| `(STBL, "STBL01")` | ASCII/Latin-1 schema, CRLF lines | section 4.2. Luna also has `"STBL02"` (3 831 bytes, AvalonButton schema); 5048 has a 28-byte STBL02. **Parse every `STBL*` resource** and merge them. |
| `(CMAP, "CMAP")` | class names | section 4.3 |
| `(BCMAP, "BCMAP")` | class inheritance | section 4.4 |
| `(VMAP, "VMAP")` | size/colour variant table | section 4.5 |
| `(RMAP, "RMAP")` | documentation/display-name records | section 4.6 (same record layout as VARIANT) |
| `(VARIANT, "<UPPERCASE VARIANT NAME>")` | the actual style data | section 4.7; one per VMAP row, complete (not a delta) |
| `(2 = RT_BITMAP, <int id>)` | 32-bpp DIB | section 6 |
| `(STREAM, <int id>)` (4066+) / `(BLOB, <int id>)` (4042/4051) | PNG, DirectX `.x`, BAML | section 6.3 |
| `(6 = RT_STRING, block)` | strings referenced by id from VARIANT/RMAP records | |
| `(16 = RT_VERSION, 1)` | VS_VERSIONINFO | irrelevant |

### 4.2 `STBL` schema (the loader must parse it: ids differ between builds)

Property ids, type ids, enumerator values and **part ids are not fixed across builds** (examples: type id 213 is BLOB in
4042/4051, BAMLSTREAM in 4066/4074, D3DXSTREAM in 5048; property 6000 is IMAGEDATA in 4051 and TRANSITIONDURATIONS in 4074;
MENU part 1 is POPUPGUTTER in 4051, BAR in 4074, BARBACKGROUND in 5048). A loader should resolve names only when it needs
symbolic names; for rendering the numeric ids compared against what the caller passes (`WP_CAPTION` = 1 ...) are enough,
but see the MENU caveat in section 7.

Text, one row per line, `\r\n`, 1175 rows in 4074 Jade (43 445 bytes). Split on `,`; the **first field is ignored** (it is
`1` everywhere, except one corrupted row in 5048 `NORMALHOVERNORMALHOVER1,TOOLBAR`). Three shapes:

```
2 fields   1,<CLASSNAME>                                  class list (38 rows in Slate 4074). Informational.
5 fields   1,<ENUMNAME>,,ENUMDEF,ENUMDEF                 declares an enum / parts list / states list
6 fields   1,<NAME>,<ENUMNAME>,ENUMVAL,<VALUE>,0          enumerator: NAME = VALUE inside ENUMNAME
6 fields   1,<NAME>,,<TYPE>,<ID>,<FLAG>                   property (or type) definition; ENUMNAME is empty
```

* `<TYPE>` is one of `ENUM INT BOOL COLOR MARGINS FILENAME SIZE POSITION RECT FONT INTLIST STRING HBITMAP DIBDATA BLOB
  BAMLSTREAM D3DXSTREAM PNGSTREAM`. The type-defining rows come first (`1,STRING,,STRING,201,0` ... `1,PNGSTREAM,,PNGSTREAM,215,0`):
  they give the **type ids** (`ENUM`=200, `STRING`=201, `INT`=202, `BOOL`=203, `COLOR`=204, `MARGINS`=205, `FILENAME`=206,
  `SIZE`=207, `POSITION`=208, `RECT`=209, `FONT`=210, `INTLIST`=211, `HBITMAP`=212, then 213..215 as above; `DIBDATA`=2,
  `GLYPHDIBDATA`=8). The type ids 201..211 equal the `TMT_STRING..TMT_INTLIST` primitive ids of `tmschema.h`.
* `<FLAG>` = 1 means the property's value is stored as **text in RT_STRING** (record carries a string id, section 4.7).
  Flagged: 601..608 (documentation strings), 801..809 (fonts of SysMetrics), 2601 `GLYPHFONT`, 5002 `DEFAULTPANESIZE`
  (4074/5048; in 4042/4051 it is unflagged and no record uses it). Use the record's `res_id != 0` as the real discriminator.
* Enumerators of an ENUM property `P` are the ENUMVAL rows whose `ENUMNAME == P` (e.g. property `SIZINGTYPE` ->
  `TRUESIZE=0, STRETCH=1, TILE=2`; `IMAGELAYOUT` -> `VERTICAL=0, HORIZONTAL=1`; `BGTYPE` -> `IMAGEFILE=0, BORDERFILL=1,
  NONE=2`; `GLYPHTYPE` -> `NONE, IMAGEGLYPH, FONTGLYPH`; `TEXTSHADOWTYPE` -> `NONE, SINGLE, CONTINUOUS`;
  `IMAGESELECTTYPE` -> `NONE, SIZE, DPI`).
* Parts of class `C`: ENUMVAL rows with `ENUMNAME == "<C>PARTS"` (e.g. `WINDOWPARTS`: `CAPTION=1 ... FRAMEBOTTOM=9 ...
  CLOSEBUTTON=18 ... DIALOG=29 ... FRAMEBOTTOMSIZINGTEMPLATE=36, SMALLFRAMEBOTTOMSIZINGTEMPLATE=37`; 5048 adds `FRAME=38`).
* States of part `P`: ENUMVAL rows with `ENUMNAME == "<P>STATES"` (`CAPTIONSTATES`: ACTIVE=1, INACTIVE=2; `MAXCAPTIONSTATES`
  adds DISABLED=3; `CLOSEBUTTONSTATES`: NORMAL=1 HOT=2 PUSHED=3 DISABLED=4 STYLUSHOT=5). **The schema has no explicit
  part -> states link**; it is a naming convention. Classes whose parts share one state set use `<CLASS>STATES` or a
  generic name (Sidebar `SBCLOSEBUTTONSTATES`, `BUTTONSTATES`, `TASKBACKSTATES`...). Compare names case-insensitively
  (`BackbuttonSTATES`, `PresentationFrameworkPARTS` are mixed case).
* Some ids are defined twice under two names (`CHILD1PERCENTWIDTH` and `CHILD3PERCENTWIDTH` are both 8000 in STBL02): ids are
  authoritative, names are not unique.
* Property ids >= 5000 are Longhorn-specific; ids <= 5003 are the `tmschema.h` ids (SDK 4051 `Tmschema_h`: `TM_PROP(3601,
  TMT, SIZINGMARGINS, MARGINS)` etc.). Additions seen in STBL01 (4074 Jade) that are **not** in the 4051 SDK header:
  `NAME`=600, `HEADING1FONT..BODYFONT`=807..809, `LASTUPDATED`=1403, `ALIAS`=1404, `DRAWBORDERS`=2214, `NOETCHEDEFFECT`=2215,
  `TEXTAPPLYOVERLAY`=2216, `USESCOMPOSITION`=2217, `TEXTGLOWSIZE`=2425, `FRAMESPERSECOND`=2426, `NORMALSIZE`=3409,
  `TEXTCOLORHINT`..`BODYTEXTCOLOR`=3824..3827, `CUSTOMSPLITRECT`=5004, `ANIMATIONBUTTONRECT`=5005,
  `TRANSITIONDURATIONS`=6000. The SDK's `STOCKIMAGEFILE` (3007) is absent from the compiled schema.
  STBL02 (Luna 4042..4074): `BORDERSTYLE`/`PARENTBORDERSTYLE`/`BASEBORDERSTYLE` enums (NONE, SOLID, DOTTED, DASHED, DOTDASHED)
  and props 8000..8023 (`CHILD*PERCENTWIDTH/HEIGHT/COLOR`, `PARENTCONTENTMARGINS`, `BASECONTENTMARGINS`, `BORDERMARGINS`,
  `*BORDERMARGINS`, `*BACKGROUNDCOLOR`, `*BORDERCOLOR`, `BORDERRADIUS`, `RINGTHICKNESS`, `BASERINGBKCOLOR`) used by class
  `AvalonButton` (parts PUSHBUTTON=1, RADIOBUTTON=2, SCROLLTHUMB=3, CHECKBOX=4; 83 records per Luna variant).

### 4.3 `CMAP` class table

Not length-prefixed: a sequence of **NUL-terminated UTF-16LE names, each followed by zero padding up to the next multiple of
8 bytes** (so a name of 14 bytes incl. NUL is followed by 2 pad bytes, a 16-byte one by none). The resource ends exactly on
an 8-byte boundary. Verified on all 10 files: the 8-alignment rule consumes the resource exactly and yields the same list
as `lhstyle.py`'s "split on NUL and drop empties".

`class_id` = index in this list (0-based). Layout (Slate 4074; `globals` index differs per build):

```
 0 documentation          1.. sizevariant.<SizeName>   (3 entries)   then   colorvariant.<ColorName> (1 entry, Luna: 3)
 5 globals                6 sysmetrics                  7.. real classes: Link, Button, AnimationButton, ...
```

* Classes 0..(globals-1) are pseudo classes that only occur in `RMAP`; `VARIANT` records use ids >= index(`globals`)
  (minimum class id seen = 5 for Slate/Jade/aero, 7 for Luna). Index of `globals`: 5 (Slate, Jade, aero, 4042 Slate),
  7 (Luna: 3 sizes + 3 colours). Do not hard-code it; find the entry named `globals`.
* Names are `[App::]Class`: `TravelBack::AnimationButton`, `Explorer::ListView`, `TaskBar::Toolbar` -> application/sub-class
  `TravelBack`, base Win32/theme class `AnimationButton`. Same convention as XP `OpenThemeData(hwnd, L"Explorer::ListView")`.
  **The class used to look up the part/state schema is the part after the last `::`**, upper-cased
  (`AnimationButton` -> `ANIMATIONBUTTONPARTS`).
* Counts: Slate 4074 91, Jade 4074 122, Luna 4074 75, Luna 4051/4042 63, Slate 4042 79, aero 5048 88, Jade 4066 119.
* The class ids are **per file** (`Window` = 65 in Slate 4074, 94 in Jade 4074, 38 in Luna 4051, 64 in aero). Always resolve by name.

### 4.4 `BCMAP` base-class table

`u32 n` then `n` x `i32`. `n == len(CMAP) - index_of("globals")` (86 for Slate 4074, 117 Jade, 56 Luna 4051, 83 aero; the
resource is exactly `4 + 4n` bytes). Entry `i` describes class id `g + i` (`g` = index of `globals`); value `v >= 0` = the
base class is class id `g + v`; `-1` = none. Observed (Slate 4074): entries i=5..10 (`TravelBack::AnimationButton` ... `SmallFavorites::AnimationButton`) point at i=4 (`AnimationButton`); `TransparentRebar::Rebar` -> `Rebar`; `ITBarBase::Rebar` -> `TransparentRebar::Rebar`
(chains of length 3: `MaxNavbar::Rebar` -> `Navbar::Rebar` -> `NavbarBase::Rebar` -> `Rebar`). Whenever `v >= 0` the base
has the same suffix after `::` as the child (verified for all 10 files). It is **not** complete inheritance: `Explorer::ListView`
has `-1` although `ListView` exists, and `TaskBar::Toolbar` has `-1`. **[inferred]** Runtime semantics: property lookup
falls through child -> BCMAP parent chain -> (by-name fallback `App::Class` -> `Class`, as XP/Wine do) -> `globals`.
(XP/Wine order, Longhorn code not disassembled.)

### 4.5 `VMAP` variant table

Concatenation of **counted strings** to the end of the resource: `u32 nchars` (including the terminating NUL), then `nchars`
UTF-16LE chars (last is NUL), then zero padding so that the character data occupies a multiple of 8 bytes
(`pad = (8 - (2*nchars) % 8) % 8`; the next count follows immediately). Strings come in **triples**
`(variant name, size name, colour name)`. Verified on all 10 files (the resource is consumed exactly; the set of variant
names equals the set of `VARIANT` resource names, case-insensitively).

```
Slate/Jade 4074 : (NormalDefault, Normal, Default) (LargeFontsDefault, Large, Default) (ExtraLargeDefault, ExtraLarge, Default)
Luna 4051..4074 : (NormalBlue, NormalSize, NormalColor) (LargeFontsBlue, LargeFonts, NormalColor) (ExtraLargeBlue, ExtraLargeFonts, NormalColor)
                  (NormalMetallic, NormalSize, Metallic) ... (ExtraLargeHomeStead, ExtraLargeFonts, HomeStead)     [9 rows]
aero 5048       : (NormalDefault, Normal, NormalColor) (LargeFontsDefault, LargeFonts, NormalColor) (ExtraLargeDefault, ExtraLargeFonts, NormalColor)
```

(`msstyles2theme.py` extracts the triples with a regex over the decoded UTF-16 and works only because every name is an
alphanumeric word; the real structure is the one above.) The resource name of the data is the variant name **upper-cased**:
`NormalDefault` -> `(VARIANT, "NORMALDEFAULT")`. Default selection: first triple. The `sizevariant.<size>` /
`colorvariant.<colour>` pseudo classes in CMAP carry the same size/colour names (and the RMAP display strings).

### 4.6 `RMAP` documentation records

Same record layout as VARIANT (section 4.7), referring to the pseudo classes 0..g-1:

| class | prop | meaning |
| --- | --- | --- |
| `documentation` | 600 `NAME` (inline STRING, e.g. `Slate`), 601 `DISPLAYNAME`, 602 `TOOLTIP`, 603 `COMPANY`, 604 `AUTHOR`, 605 `COPYRIGHT`, 606 `URL`, 607 `VERSION`, 608 `DESCRIPTION` (all via RT_STRING id), 1403 `LASTUPDATED` (inline, `08-21-2003`) | 18 records Slate/Jade, 22 Luna, 19 aero |
| `sizevariant.X`, `colorvariant.Y` | 601 `DISPLAYNAME`, 602 `TOOLTIP` | via RT_STRING id |

Slate 4074: STRING 101 = "Slate", 102 = "Microsoft's Visual Style for Longhorn", 103 = "Slate Visual Style", 104 = "MSX",
105 = "Microsoft Corporation", 106 = copyright, 107 = URL, 108 = ".10", 109 = "Normal", 110 = "Normal size", 111/112 Large,
113/114 "Extra Large"/"Extra large size", 115 = "Default", 116 = "Default Color Scheme", 117/118 = "Tahoma Bold, 8" /
"Tahoma, 8" (fonts, referenced from VARIANT). Wine's `wDocToRes` (5000+) is the XP analogue.

### 4.7 `VARIANT` record stream

A VARIANT resource is a flat array of records, no header, no count, no terminator (parse to the end of the resource).
Records are **8-byte aligned** and sorted ascending by `(class_id, part_id, state_id, prop_id)` (verified, all variants of
all files; so equal-class runs are contiguous and binary search works). Record header, 32 bytes:

| off | type | field | notes |
| --- | --- | --- | --- |
| 0 | u32 | `prop_id` | property id from STBL (`SIZINGMARGINS`=3601...). For the stream types (4066+) and the generic inline `FONT` it equals the *type* id (213/214/215, 210; see 4.8) |
| 4 | u32 | `type` | type id from STBL (200..215, section 4.2) |
| 8 | u32 | `class_id` | CMAP index |
| 12 | u32 | `part_id` | 0 = class level (`[class]`) |
| 16 | u32 | `state_id` | 0 = part level (`[class.part]`); nonzero only with part != 0 |
| 20 | u32 | `res_id` | **0: value is inline** after the header. Non-zero: value lives in another resource with this id (BITMAP / STREAM / BLOB / RT_STRING depending on type) and the record has **no payload** |
| 24 | u32 | reserved | 0 in all 10 files (lhstyle calls this `res`) |
| 28 | u32 | `size` | inline: payload byte count (unpadded). Out-of-line: size of the resolved value (stream: byte length of the STREAM resource; string-backed FONT: 92; string-backed RECT: 16; FILENAME: always 8; STRING: byte length incl. NUL). Informational only |

If `res_id == 0`: payload = the next `size` bytes, followed by zero padding to a multiple of 8, so
`record_length = 32 + ((size + 7) & ~7)`; if `res_id != 0`: `record_length = 32`.

```c
struct msv4_rec { uint32_t prop, type, cls, part, state, res_id, reserved, size; };   /* 32 bytes */
static void walk(const uint8_t *d, size_t n) {
    size_t o = 0;
    while (o + 32 <= n) {
        const struct msv4_rec *r = (const void *)(d + o);          /* little-endian host */
        const uint8_t *payload = r->res_id ? NULL : d + o + 32;
        o += r->res_id ? 32 : 32 + ((size_t)(r->size + 7) & ~(size_t)7);
        /* ... */
    }
}
```

Counts (NORMAL* variant): Slate 4074 2010 records / 83 176 bytes; Jade 4074 2206 / 90 760; Luna 4051 1813 / 73 704;
aero 5048 2191 / 92 784. The 3 (or 9) variants of one file have the same number of records.

**Duplicates:** 4 `(class, part, state, prop)` keys occur twice in Jade 4074 NORMALDEFAULT (Combobox FILLCOLOR; `Wizard05Wizard::BackButton`
TRANSPARENT; Tab part 10 IMAGEFILE1; TreeView part 2 BGTYPE). Wine keeps the first it sees; real uxtheme's choice is
unknown (section 9).

### 4.8 Value payloads by type

| type id | name | inline payload (`res_id == 0`) | out-of-line (`res_id != 0`) |
| --- | --- | --- | --- |
| 200 | ENUM | `i32` enumerator value. Symbolic name = ENUMVAL row of enum `<property name>` | - |
| 201 | STRING | UTF-16LE, `size` bytes **including** the NUL (`CSSNAME` = `"cpwebvw.css"` = 24 bytes) | RT_STRING id (documentation props) |
| 202 | INT | `i32` (size 4) | - |
| 203 | BOOL | `i32` 0/1 (size 4) | - |
| 204 | COLOR | 4 bytes `R G B 00` = little-endian `COLORREF` 0x00BBGGRR. The 4th byte is 0 in all 358+ colours checked; no alpha is stored | - |
| 205 | MARGINS | 4 x `i32` in order **left, right, top, bottom** (= `MARGINS{cxLeftWidth, cxRightWidth, cyTopHeight, cyBottomHeight}`; same as XP ini order `28, 35, 9, 17`) | - |
| 206 | FILENAME | - | `res_id` = integer name of an **RT_BITMAP** resource (always found, 100% of 1230 in Jade). The only property types of this kind: IMAGEFILE 3001, IMAGEFILE1..5 3002..3006, GLYPHIMAGEFILE 3008 |
| 207 | SIZE | `i32` (size 4): CAPTIONBARHEIGHT, SCROLLBARWIDTH, WIDTH, HEIGHT ... | - |
| 208 | POSITION | 2 x `i32` (x, y) (size 8): OFFSET, TEXTSHADOWOFFSET, MINSIZE..MINSIZE5, NORMALSIZE | - |
| 209 | RECT | 4 x `i32` left, top, right, bottom (size 16) | RT_STRING id; the string is XP-style text `"0, 0, 380, 40"` (DEFAULTPANESIZE) |
| 210 | FONT | a Win32 **`LOGFONTW`** (size 92): `i32 lfHeight` (negative pixels at 96 dpi: Segoe UI 9 pt = -12), `lfWidth`, `lfEscapement`, `lfOrientation`, `lfWeight` (400/700), `u8 lfItalic, lfUnderline, lfStrikeOut, lfCharSet (1 = DEFAULT), lfOutPrecision, lfClipPrecision, lfQuality, lfPitchAndFamily`, `WCHAR lfFaceName[32]`. Used for the *generic* `FONT` property (prop id 210), e.g. `[Link] FONT` | RT_STRING id; text `"Face[ Bold], points[, bold][, italic][, Quality:ClearType-Natural]"`, e.g. `"Tahoma Bold, 8"`, `"Segoe UI, 9, Quality:ClearType-Natural"`. Used by named props CAPTIONFONT..BODYFONT (801..809) and GLYPHFONT. Height in the text is in points: `lfHeight = -MulDiv(pt, dpi, 72)` |
| 211 | INTLIST | `i32[n+1]`: `[0] = n`, followed by `n` ints (XP `INTLIST{iValueCount, iValues[]}`). `TRANSITIONDURATIONS` (6000, 4074): `n = 1 + N*N`, `v[0] = N` (state count), then `N*N` millisecond durations (values 0/75/150/225/375/1275). Orientation of the matrix **[inferred]**: row = from-state. Observed N = 6, 10, 15 | - |
| 212 | HBITMAP | not used by any record | - |
| 213/214/215 | BAMLSTREAM / D3DXSTREAM / PNGSTREAM (4066/4074) | - | `res_id` = integer name of a `STREAM` resource; record `size` = its length. `prop_id` == type id (there is no named property: the *part* names the stream, e.g. `CompositedWindow.LGFRAME`). Resource contents: PNG (straight RGBA), text DirectX `.x` (`xof 0303txt`), BAML (see `tools/baml2xaml.py`) |
| 213 (4042/4051) | BLOB | - | `res_id` = integer name of a `BLOB` resource = a PNG; `prop_id` = 6000 `IMAGEDATA` |
| 2, 8 | DIBDATA, GLYPHDIBDATA | schema only, no records anywhere | - |

Unknown types must be skipped using the record length (the header makes every record self-delimiting).

### 4.9 Class / part / state addressing

* A record belongs to the key `(class_id, part_id, state_id)`. `part_id == 0` means "class level", `state_id == 0` means "all
  states of the part". This is exactly Wine's `THEME_PARTSTATE` list, where XP's `[class]`, `[class.part]`,
  `[class.part(state)]` sections become key `(cls,0,0)`, `(cls,p,0)`, `(cls,p,s)`.
* Part and state numbers are the `tmschema.h` numbers for the XP classes (Window `CAPTION`=1 .. `CLOSEBUTTON`=18 ..
  `FRAMEBOTTOM`=9; Button `PUSHBUTTON`=1, `RADIOBUTTON`=2, `CHECKBOX`=3, `GROUPBOX`=4...). Checked every class/part/state of the
  4051 SDK header against the Jade 4074 schema: identical for all classes **except** `MENU` (renumbered), plus additions
  (`STYLUSHOT` states = 5/6/..., `REBAR` parts 6..8, `TOOLBAR` 7, `LISTVIEW` 6..8, `PROGRESS` 5..6, `EDIT` 3, `LISTITEM` state 6 `HOTSELECTED`) and one removal (`CAPTION` state `DISABLED`=3).
  Examples that matter for frames: `FRAMESTATES` ACTIVE=1 INACTIVE=2; caption buttons NORMAL=1 HOT=2 PUSHED=3 DISABLED=4
  (+5 STYLUSHOT); with 8-image strips `index = (frame state-1)*4 + state` as in `docs/history/CLASSIC-DECO-NOTES.md` sec. 2. Caption
  button strips in 4074 can have `IMAGECOUNT = 10`.
* Longhorn-only classes (parts from STBL, 4074 Jade):
  * `CompositedWindow` (`COMPOSITEDWINDOWPARTS`): 1 GENERIC3DMESH, 2 LGFRAME, 3 LGFRAMEEXPLORER, 4 LGFRAMESHADOW, 5
    LGFRAMEOVERLAY, 6 LGFRAMESIZEGRIP, 7 LGCLOSEBTN, 8 LGCLOSEBTNOVERLAY, 9 LGMAXBTN, 10 LGMINBTN, 11 LGRESTOREBTN, 12 LGBTNOVERLAY,
    13 LGFRAMEMAX, 14 LGFRAMEMAXEXPLORER, 15 LGFRAMEMIN, 16 LGFRAMEMINSHADOW, 17 SMFRAME, 18 SMFRAMESHADOW, 19 SMFRAMEOVERLAY, 20
    SMFRAMESIZEGRIP, 21 SMCLOSEBTN, 22 SMCLOSEBTNOVERLAY, 23 BORDERONLY, 24 BORDERONLYSHADOW, 25 BORDERONLYOVERLAY, 26 CAPTIONFOG, 27
    DEFAULTICON. Jade fills 25 of them (parts 6 and 20 absent): part 1 as a `D3DXSTREAM`, the rest `PNGSTREAM`. **Only streams; no
    margins or other properties** - the nine-grid insets are not in the style (they are in desksrv.dll / luxwm's theme.ini).
  * `PreviewPane` (`PREVIEWPANEPARTS` 1 AURORA = `BAMLSTREAM`), `DesktopBackground` (1 AURORA; schema only), `PresentationFramework`
    (1 DEFAULTSTYLES = `BAMLSTREAM`).
  * `Sidebar` (`SIDEBARPARTS` 1..41: TITLEBACK, BACK, OVERFLOWBACK, CHEVRON, EXPANDO_OPEN_HORIZ, EXPANDO_OPEN_VERT,
    EXPANDO_CLOSED_HORIZ, EXPANDO_CLOSED_VERT, STARTBUTTON, TASKBACK, TILEBACK, TILEHEADERBACK, TILECHEVRONBACK,
    TILECHEVRONGLYPHUP, TILECHEVRONGLYPHDOWN, TILESIZER, TILESIZERBUBBLE, FLYOUTTOP, FLYOUTCONTENT, FLYOUTBOTTOM, FLYOUTCLOSEBACK,
    FLYOUTCLOSEGLYPH, FLYOUTOPTIONSBACK, FLYOUTOPTIONSGLYPH, CLOCKBACKGROUND, IDLISTBUTTON, CLOCKGLASS, TRAYAREABACK, BARUP,
    BARDOWN, BACK_HORIZ, SBRESTOREBUTTON, SBCLOSEBUTTON, BUTTON, then font/colour parts 35 TILEHEADERTEXTFONT, 36 TILETEXTFONT,
    37 TASKBACKFONT, 38 TILEHEADERTEXTCOLOR, 39 TILETEXTCOLOR, 40 TASKBACKCOLOR, 41 TASKBACKGROUPCOLOR). 60 records per variant
    (image parts with states, as PNGSTREAM in 4074/IMAGEDATA BLOB in 4042/4051).
  * Also new/changed: `AnimationButton`, `Animation`, `Navbar`, `Overlay`, `Static`, `Wizard05Wizard`, `TextGlow`, `MenuBand`,
    `AvalonButton` (4042..4074 Luna); 5048 replaces some by `Wizard05`, `TaskDialog`, `TravelBand`, `MonthCal`, `DatePicker`, `Listbox`.

### 4.10 Property lookup (what the loader should implement)

Build per variant: `cls -> sorted array of (part,state) groups -> sorted array of props` (the file is already in that
order). Lookup `FindProperty(cls, part, state, type, prop)` as in Wine:

1. for `c` in chain(cls): find group `(part,state)`; if it contains `prop` (type equal, or caller passes type 0) return it;
2. if state != 0 retry with state 0; else if part != 0 retry with part 0; else not found.

with `chain(cls)` = `cls`, `BCMAP` parents of `cls`, the by-name fallback (`App::Class` -> `Class`), then `globals`
(**[inferred]**, matches XP; section 9). Sysmetrics (class `sysmetrics`, part/state 0) is the `[SysMetrics]` section: system
colours 1601..1631, fonts 801..806 (via RT_STRING), sizes 1201..1209, `FLATMENUS` 1001, `MINCOLORDEPTH` 1301, `CSSNAME`
1401, `XMLNAME` 1402. `globals` holds the edge colours (3804..3808).

## 5. Colour schemes and sizes

* **XP**: one ini per (colour, size) selected through `FILERESNAMES`; display names via RT_STRING 1000+/3000+;
  substitutions are pre-applied; bitmaps differ per colour (`Blue\`, `Metallic\`, `HomeStead\` file name prefixes).
* **Longhorn**: `VMAP` lists the variants; **each `VARIANT` is a complete, self-contained copy** of the style for one
  (size, colour) pair (same record count in all variants of a file; sysmetrics, fonts (`Segoe UI, 9` vs `Segoe UI, 11` in LargeFonts),
  bitmap ids all differ). Slate/Jade: sizes `Normal, Large, ExtraLarge` x colour `Default` (3 variants). Luna: sizes
  `NormalSize, LargeFonts, ExtraLargeFonts` x colours `NormalColor, Metallic, HomeStead` (9 variants; 657..717 bitmaps because every
  colour has its own images). aero 5048: `Normal, LargeFonts, ExtraLargeFonts` x `NormalColor`. Display strings: RMAP.
  Loader API: `open(path, colorName, sizeName)` -> find triple in VMAP (case-insensitive; flxp's Wine patch falls back to the first
  entry if the name is unknown, which is needed because Longhorn says `Default`/`Normal` where XP says `NormalColor`/`NormalSize`).

## 6. Images

### 6.1 Bitmaps (`FILENAME` properties)

`RT_BITMAP` resource with integer name = `res_id`. Data = `BITMAPINFOHEADER` (40 bytes) + pixels, **no** `BITMAPFILEHEADER`.
All 267/356/717/657/280 bitmaps in the Longhorn files are 32 bpp, `biCompression = BI_RGB (0)`, `biHeight > 0` (bottom-up),
pixel order B,G,R,A, stride `4*w`, no palette. (XP: mixed depths with palettes.)

Strips: a part's image holds `IMAGECOUNT` sub-images stacked per `IMAGELAYOUT` (VERTICAL = 0: stacked top to bottom, each
`h/count` tall; HORIZONTAL = 1: side by side). Sub-image `k` (0-based) = `state_id - 1` for the normal state sets.
`IMAGEFILE1..5` + `MINDPI1..5`/`MINSIZE1..5` are the alternative resolutions (`IMAGESELECTTYPE` NONE/SIZE/DPI).
`SIZINGMARGINS` (l,r,t,b) cut the nine-grid; `SIZINGTYPE` TRUESIZE/STRETCH/TILE; `CONTENTMARGINS`, `CAPTIONMARGINS`,
`TRANSPARENT`, `TRANSPARENTCOLOR` (3809, default 255 0 255), `BGTYPE`, `GLYPHTYPE`/`GLYPHIMAGEFILE`/`GLYPHTRANSPARENT`,
`UNIFORMSIZING`, `BORDERONLY` keep their XP meanings; drawing rules: `docs/history/CLASSIC-DECO-NOTES.md`.

### 6.2 Alpha conventions (measured over all Longhorn bitmaps)

* In **every** 32-bpp Longhorn bitmap, `max(r,g,b) <= a` holds for every pixel (all 4 504 RT_BITMAPs of the ten v4 files tested): the data
  is **premultiplied ARGB**. To get straight alpha divide by `a/255` (`msstyles2theme.py` does, clamping to 255).
  (XP bitmaps are straight alpha: 53 of 60 32-bpp images have `rgb > a` pixels, and uxtheme premultiplies after load.)
* Classify each image by its alpha channel (same logic as `msstyles2theme.load_dib`, which was validated visually against 4074):
  1. some alpha outside {0, 255} -> real alpha, premultiplied, **ignore** `TRANSPARENT`/colour key;
  2. alpha contains both 0 and 255 and nothing else ("binary") -> alpha is a hard **mask**; Longhorn honours it (XP ignored it).
     Evidence: 4074 glyph strips are black where alpha = 0 and contain no magenta; caption corners are alpha 0 **[derived from
     msstyles2theme / COMPOSITION; uxtheme not disassembled]**;
  3. alpha all 255 -> opaque RGB; then if the part has `TRANSPARENT = TRUE`, pixels equal to `TRANSPARENTCOLOR` become transparent.
  4. alpha all 0 -> placeholder bitmap, all pixels 0 (4 in Slate 4074: ids 274 (8x5), 275 (5x8), 281 (5x8), 290 (20x120);
     5 in Jade); never draw.
  Statistics: Slate 4074 78 partial / 83 binary / 102 opaque / 4 empty; Jade 226 / 44 / 81 / 5; Luna 4051 103 / 292 / 262 / 0;
  aero 5048 142 / 50 / 78 / 10.

### 6.3 Streams (`STREAM` / `BLOB` resources)

Raw file bytes, no wrapper: PNG (`89 50 4E 47`, 8-bit RGBA **straight** alpha; e.g. Jade LGFRAME 801x1214 with plate
colour (112,144,112) alpha 213 - see COMPOSITION 18.1), DirectX `.x` text (`xof 0303txt`, 30 869 bytes, the "ChamferBox" mesh,
byte-identical to desksrv `D3DXSTREAM 101`), BAML (2003 binary XAML; first bytes `01 00 29 00 ...` for DEFAULTSTYLES, aurora starts
`13 00 00 00`; decoder: `tools/baml2xaml.py`). The consumer API is `GetThemeStream(hTheme, part, state, propid=213/214/215,
&data, &len, hInst)`: look up the record `(CompositedWindow, part, 0, prop=type)` and return the resource bytes.
Stream ids are file-specific (Jade 4074: 481 AURORA, 482 mesh, 483 LGFRAME ... 506, Sidebar 507..566, 567 DEFAULTSTYLES; Jade 4066: 479..504; Slate 4074: 394..454).

## 7. Caveats for a loader

* Resolve part/state *numbers* through the file's own STBL when you need names; trust the numeric `tmschema.h` ids for XP-era
  classes (identical in 4051..4074) **except `MENU`**, whose part ids changed three times (SDK 4051: MENUITEM=1...; Luna 4051:
  POPUPGUTTER=1; Slate/Jade 4074: BAR=1; aero 5048: BARBACKGROUND=1, with SYSTEMCLOSE etc.).
* `STBL02` must be merged (Luna's AvalonButton, ids 8000+). `lhstyle.py` reads only `STBL01`, so those print as `prop8000`.
* Treat inline `FONT` as `LOGFONTW` and out-of-line `FONT` as text; `lhstyle.py` decodes both as text (wrong for the 29 inline fonts).
* Do not assume `res_id` is a "short value": `lhstyle.py`'s `short` variable is the resource id; INT/BOOL/COLOR/MARGINS/POSITION/SIZE/ENUM are
  always inline (`res_id == 0`) in the data.
* Strings in payloads are NUL-terminated and counted in `size`; font face names in LOGFONT are NUL-padded to 32 WCHARs.
* `lfHeight` is negative pixels at 96 dpi, whereas the text form is points: scale both by the target DPI consistently.

## 8. Build-by-build differences

### 8.1 4042 / 4051 (BLOB era)

* Stream-like data lives in RT type `BLOB` (PNG only, 60 per variant-set: Luna 180 = 60 x 3 colours, Slate 4042 60); property `IMAGEDATA`
  6000 (type 213 `BLOB`) in class `Sidebar` (`TITLEBACK` = BLOB 345 (638-byte PNG) in Luna 4051, 360 in Slate 4042); `PROPSHEET` 6001
  exists in the schema, unused. `INTLIST` and `DEFAULTPANESIZE` flag differ (5002 inline RECT). `Window` has 253 records in Luna 4051.
* Luna 4051's `[Window.CAPTION]` is numerically identical to XP's `NORMALBLUE_INI [Window.Caption]`
  (`SizingMargins 28 35 9 17`, `CaptionMargins 4 3 0 0`, `ImageCount 2`, `TextShadowType Single`) - a compiled XP-Luna.

### 8.2 4066

* `BLOB` -> `STREAM`; types 213/214/215. Jade 4066: `CompositedWindow` class id 116 (`Window`=91), streams 479..504 (`PreviewPane.AURORA`=479,
  `GENERIC3DMESH`=480, `LGFRAME`=481 ...); no `PresentationFramework.DEFAULTSTYLES` in Jade (Slate/Luna 4066 have the class).

### 8.3 4074

* Jade: 87 STREAM resources (BAML 481, mesh 482, PNG 483..506 for `CompositedWindow`, 567 DEFAULTSTYLES...); Slate 61 (60 PNG sidebar + BAML
  454 DEFAULTSTYLES); Luna 183. `CompositedWindow` only in Jade; `USESCOMPOSITION` (2217), `FRAMESPERSECOND` (2426),
  `TRANSITIONDURATIONS` (6000) are new.

### 8.4 5048 (Aero)

* No `STREAM`/`BLOB`/PNG/`.x` resources and no `CompositedWindow`, `PreviewPane`, `Sidebar`, `AnimationButton`, `Navbar`, `Overlay`,
  `PresentationFramework` classes (the compositor frame art moved into `uDWM.dll` bitmaps; see COMPOSITION 15.4).
  Types 213/214 are `D3DXSTREAM`/`PNGSTREAM` in the schema but never used; no BAML type.
* Schema deltas vs Jade 4074: only the type renumbering and `DEMOCHECK` (9900, STRING, in a 28-byte `STBL02`, unused); parts: `MENU`
  fully renumbered (BARBACKGROUND, BARITEM, POPUPBACKGROUND, POPUPBORDERS, POPUPCHECK, POPUPCHECKBACKGROUND, POPUPSEPARATOR,
  POPUPSUBMENU, SYSTEMCLOSE, SYSTEMMAXIMIZE, SYSTEMMINIMIZE, SYSTEMRESTORE ...), `STARTPANEL` parts 12..17 (MOREPROGRAMSTAB, NSCHOST,
  SOFTWAREEXPLORER, OPENBOX, SEARCHVIEW, MOREPROGRAMSARROWBACK), `WINDOW` part 38 `FRAME` (no records).
* New classes: `Listbox`, `Static`, `Wizard05`, `Wizard05::Static`, `TaskDialog`, `MonthCal`, `DatePicker`, `TravelBand`, `Travel::Toolbar`.
* VMAP colour `NormalColor`, sizes `Normal`/`LargeFonts`/`ExtraLargeFonts`; 88 classes; 2191 records; 280 bitmaps, all premultiplied;
  `STBL01` has one corrupted class row (`NORMALHOVERNORMALHOVER1,TOOLBAR`).
* Fonts: `Segoe UI, 9, Quality:ClearType-Natural` for every system font.
* Aero `Window.CAPTION` (class 64): IMAGEFILE bitmap 314 (400x58), SIZINGMARGINS `5 5 17 1`, CAPTIONMARGINS `4 4 0 0`, active text shadow
  colour `240 240 240`, TEXTSHADOWTYPE CONTINUOUS.

## 9. What is inferred / not pinned down

* **BCMAP runtime semantics** (section 4.4) and the exact chain order of lookup were not verified in the Longhorn uxtheme code.
* **Duplicate records**: first-wins (Wine) vs last-wins unknown.
* **Record field at offset 24** (always 0) and the value of `size` for out-of-line FILENAME (always 8) have no known meaning.
* **COMBODATA / MINDEPTH / ORIGFILENAMES** (XP) semantics are guesses; Wine ignores them. `MINCOLORDEPTH` = 15 is the only value seen.
* **INTLIST/TRANSITIONDURATIONS** matrix orientation.
* **Alpha mask rule** for binary-alpha bitmaps is taken from `msstyles2theme.py`/`COMPOSITION.md` observations, not from uxtheme code.
* **Part -> state-enum association** is by name only (`<PART>STATES`, falling back to `<CLASS>STATES`); `lhstyle.py` guesses the same way.
* `lhstyle.py` shortcomings (for anyone diffing against it): reads only `STBL01`; decodes inline `FONT` as UTF-16 text; treats `short` as
  "value" for INT-like types (dead code); `a=` branch for colours never fires; does not parse VMAP.

## 10. Hooking the values into Wine's uxtheme (suggestion)

Wine stores every value as raw text in `THEME_PROPERTY.lpValue` and re-parses it in `MSSTYLES_GetPropertyInt/Color/Font/...`.
Two ways:

* **Synthesise XP text** (least invasive): build the `THEME_CLASS/PARTSTATE/PROPERTY` lists directly from the records (no
  class/part/state name lookup needed), writing a text form into a per-theme arena: INT/SIZE/ENUM -> decimal; BOOL -> `True`/`False`;
  COLOR -> `"r g b"`; MARGINS -> `"l, r, t, b"`; POSITION -> `"x, y"`; RECT -> `"l, t, r, b"`; FONT (inline) -> `"Face, <pt>[, bold][, italic]"`
  with `pt = -lfHeight * 72 / 96`; FONT/RECT out-of-line -> the RT_STRING text as is; FILENAME -> a synthetic name such as `"#319"`
  that your `MSSTYLES_LoadBitmap` maps to `LoadImageW(hTheme, MAKEINTRESOURCEW(319), IMAGE_BITMAP ...)` (and keep premultiplied
  handling: skip Wine's `prepare_alpha` multiplication for v4 files); stream types -> expose via a new `GetThemeStream`-style path
  keyed by `(class, part, state)`. Enum-valued properties need the enumerator names only if the callers parse them from text; Wine's
  `GetThemePropertyEnum` can instead be made to take the integer.
* Or carry `{type, res_id, payload pointer}` in `THEME_PROPERTY` and branch on `tf->version` in the getters (cleaner for fonts,
  INTLIST and streams).

Either way: pick the VARIANT by VMAP, build classes from CMAP names (`App::Class` -> `szAppName`/`szClassName`), set `overrides`
from BCMAP, then `globals`. Take the default colour/size from the first VMAP triple. The `PACKTHEM_VERSION == 4` check goes next to
Wine's `== 3` check.

## 11. Validation checklist (expected values)

All numbers below were produced by independent scratch parsers (not by `lhstyle.py`) and cross-checked against it.

**Container**
1. `PACKTHEM_VERSION`: XP `03 00`; all ten v4 files `04 00`. `MINCOLORDEPTH` `0F 00`.
2. Resource counts (BITMAP / STREAM-or-BLOB / VARIANT): Slate 4074 267 / 61 / 3; Jade 4074 356 / 87 / 3; Luna 4074 717 / 183 / 9; Jade 4066
   354 / 87 / 3; Slate 4066 265 / 61 / 3; Luna 4066 711 / 183 / 9; Luna 4051 657 / 180 BLOB / 9; Luna 4042 657 / 180 / 9; Slate 4042 240 / 60 BLOB / 3;
   aero 5048 280 / 0 / 3; XP Luna 558 BITMAP, 10 TEXTFILE, 24 STRING blocks.
3. Every BITMAP in the ten v4 files (4 504 resources): `biSize 40, bpp 32, BI_RGB, height > 0`, and `max(r,g,b) <= a` for every pixel.

**Tables (Slate 4074)**
4. `VMAP` = 252 bytes, triples `(NormalDefault,Normal,Default) (LargeFontsDefault,Large,Default) (ExtraLargeDefault,ExtraLarge,Default)`; first bytes
   `0E 00 00 00 4E 00 6F 00 72 00 6D 00 61 00 6C 00 44 00 ... 74 00 00 00 00 00 00 00 | 07 00 00 00 4E 00 6F 00 ...`.
5. `CMAP` = 3288 bytes, 91 names; `[0..6]` = `documentation, sizevariant.Normal, sizevariant.Large, sizevariant.ExtraLarge, colorvariant.Default, globals, sysmetrics`,
   `[7]=Link`, `[65]=Window`, `[89]=Sidebar`, `[90]=PresentationFramework`; first bytes `64 00 6F 00 63 00 ...` ("documentation\0" then 4 zero pad bytes).
6. `BCMAP` = 348 bytes: `n = 86`; `i=4` (`AnimationButton`, class 9) -> -1; `i=5..10` (`TravelBack::AnimationButton` ... `SmallFavorites::AnimationButton`) -> 4;
   `i=20` (`TransparentRebar::Rebar`) -> 19; `i=21` (`ITBarBase::Rebar`) -> 20; `i=34` (`Navbar::Rebar`) -> 33; `i=37` (`MaxNavbar::Rebar`) -> 34; `i=17` (`Explorer::ListView`) -> -1.
7. `RMAP` = 616 bytes, 18 records; record 0 = `(600, 201, cls 0, 0, 0, res 0, 0, size 12)` payload `"Slate\0"`; record 1 = `(601, 201, 0,0,0, res_id 101, 0, 12)`;
   last documentation record `(1403, 201, ..., size 22)` payload `"08-21-2003\0"`. STRING 101 = "Slate", 116 = "Default Color Scheme".
8. `VARIANT/NORMALDEFAULT` = 83 176 bytes = 2010 records; all three variants have 2010 records; sorted by `(class,part,state,prop)`; the walk ends exactly at the end.
9. First record (offset 0): `DC 0E 00 00 CC 00 00 00 05 00 00 00 00*4 00*4 00*4 00*4 04 00 00 00 | C8 C9 CA 00 | 00 00 00 00` =
   `globals EDGELIGHTCOLOR COLOR (200,201,202)`. Record at offset 200: `CAPTIONFONT` (class 6) `res_id 117` -> RT_STRING 117 = "Tahoma Bold, 8";
   `SCROLLBARWIDTH` = 0x11 = 17; `CAPTIONBARHEIGHT` = 0x13 = 19; `SMCAPTIONBARHEIGHT` = 17; `MINCOLORDEPTH` = 15; `CSSNAME` = "cpwebvw.css";
   `XMLNAME` = "default.xml".
10. `Window.CAPTION` (class 65, part 1, NORMALDEFAULT): `TRANSPARENT=TRUE, IMAGECOUNT=2, IMAGEFILE=bitmap 311 (320x58 -> two 320x29 sub-images),
    SIZINGMARGINS=30 30 20 8, CONTENTMARGINS=0 0 0 0, CAPTIONMARGINS=4 3 0 0, SIZINGTYPE=STRETCH, IMAGELAYOUT=VERTICAL`; state 1 (ACTIVE):
    `TEXTSHADOWOFFSET=1 1, TEXTSHADOWCOLOR=0 0 0, ICONEFFECT=SHADOW, TEXTSHADOWTYPE=CONTINUOUS`.
    `Window.FRAMEBOTTOM` (part 9): `IMAGEFILE=bitmap 319 (49x10), IMAGECOUNT=2, SIZINGMARGINS=8 8 0 3, SIZINGTYPE=STRETCH, IMAGELAYOUT=VERTICAL`.
    `Window.CLOSEBUTTON` (part 18): `IMAGECOUNT=10, IMAGEFILE=313, IMAGEFILE1..5=314..318, MINDPI1..3=96,126,170, SIZINGMARGINS=3 3 3 3,
    IMAGESELECTTYPE=SIZE, UNIFORMSIZING=TRUE`. `Window` has 260 records.
11. Slate 4074 `Sidebar` (class 89): 60 records, all `PNGSTREAM`/`STREAM`; 61 STREAM resources = 60 PNG + BAML id 454 (38 752 bytes, `PresentationFramework.DEFAULTSTYLES`,
    class 90). No `CompositedWindow` class.

**Jade 4074**
12. 122 classes: `Window`=94, `PreviewPane`=118, `CompositedWindow`=119, `Sidebar`=120, `PresentationFramework`=121; BCMAP n = 117; NORMALDEFAULT 2206 records / 90 760 bytes;
    `CompositedWindow` has 25 records, all with `state 0`, parts `1,2,3,4,5,7,8,9,10,11,12,13,14,15,16,17,18,19,21,22,23,24,25,26,27`; part 1 = `type 214 D3DXSTREAM, res_id 482, size 30869`
    (resource starts `xof 0303txt`); part 2 `LGFRAME` = `type 215, res_id 483, size 28244` (PNG 801x1214); part 3 -> 484 (28 244); part 4 -> 485 (23 941); part 5 -> 486 (13 839);
    `PreviewPane` part 1 = `type 213, res_id 481, size 43627`; `PresentationFramework` part 1 = `213, 567, 38752`. Prop id of these records equals their type id.
    Inline `[Link] FONT` (class `Link`, part 1) = LOGFONTW `lfHeight -12, lfWeight 400, lfCharSet 1, "Segoe UI"` (92 bytes). `CAPTIONFONT` -> "Segoe UI, 9, Quality:ClearType-Natural";
    LARGEFONTSDEFAULT -> "Segoe UI, 11". `TRANSITIONDURATIONS` (prop 6000, class `Button` = 8, state 0): part 1 first ints `37, 6`, 38 ints in total (152 bytes); part 2 has 102 ints (`101, 10`), part 3 has 227 (`226, 15`).
13. Jade 4066: `CompositedWindow`=116, 25 records; `GENERIC3DMESH` res_id 480, `LGFRAME` 481; `PreviewPane.AURORA` 479.

**Luna 4051 (NORMALBLUE)**
14. 63 classes (`globals`=7, `Window`=38, `Sidebar`=62, `AvalonButton` present), 1813 records / 73 704 bytes; BCMAP n 56; `Window.CAPTION`: `TRANSPARENT=TRUE, IMAGECOUNT=2,
    IMAGEFILE=bitmap 238 (66x58), SIZINGMARGINS=28 35 9 17, CONTENTMARGINS=1 1 1 1, CAPTIONMARGINS=4 3 0 0`, ACTIVE: `TEXTSHADOWTYPE=SINGLE`;
    `Sidebar` part 1 (TITLEBACK) = `type 213 BLOB, prop 6000 IMAGEDATA, res_id 345, size 638` (a PNG). `CAPTIONFONT` = "Tahoma, 13" in this build.
    9 VMAP triples starting `(NormalBlue,NormalSize,NormalColor)`; RMAP 744 bytes / 22 records, `documentation NAME = "Luna"`, `LASTUPDATED = "09-05-2001"`.
    `AvalonButton` (parts 1..4, states 0/2/3/4/5/8/12) has 83 records per variant; part 1 state 0 includes props 8006, 8009, 8012, 8014, 8015, 8018 ... (STBL02 required).

**aero 5048**
15. Classes 88 (`globals`=5, `Window`=64), 2191 records / 92 784 bytes (NORMALDEFAULT), BCMAP n 83, VMAP triples `(NormalDefault,Normal,NormalColor)...`,
    RMAP 712 bytes / 19 records. `Window.CAPTION`: IMAGEFILE bitmap 314 (400x58), SIZINGMARGINS `5 5 17 1`, CAPTIONMARGINS `4 4 0 0`. No record has type 213 or 214. STBL01 contains
    `1,D3DXSTREAM,,D3DXSTREAM,213,0` and `1,PNGSTREAM,,PNGSTREAM,214,0`. `MENU` part 1 = `BARBACKGROUND`.

**XP (Wine)**
16. `COLORNAMES` = 64 bytes `NormalColor\0Metallic\0HomeStead\0\0`; `SIZENAMES` = 78 bytes; `FILERESNAMES` has 9 names; (Metallic, LargeFonts) -> index 4 -> `LARGEFONTSMETALLIC_INI`;
    `NORMALBLUE_INI` has 2 330 lines and `[Window.Caption]` = `ImageFile Blue\FrameCaption.bmp, SizingMargins 28,35,9,17, ContentMargins 1,1,1,1, CaptionMargins 4,3,0,0, ImageCount 2,
    Transparent True`; bitmap named `BLUE_FRAMECAPTION_BMP`. XP STRING 1000 = "Default (blue)", 5000 = "Windows XP style".

## 12. Worked example: three records of Slate 4074 `VARIANT/NORMALDEFAULT`

All three are class 65 (`Window`) part 9 (`FRAMEBOTTOM`), state 0. Offsets are inside the VARIANT resource data.

```
off 54912  (IMAGECOUNT, INT, inline, 40 bytes)
 61 09 00 00  ca 00 00 00  41 00 00 00  09 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  04 00 00 00   02 00 00 00  00 00 00 00
 prop=0x0961=2401 IMAGECOUNT | type=0xCA=202 INT | cls=0x41=65 Window | part=9 | state=0 | res_id=0 | reserved=0 | size=4 | payload=2 | 4 pad bytes
 length = 32 + ((4+7)&~7) = 40 -> next record at 54952

off 54952  (IMAGEFILE, FILENAME, out-of-line, 32 bytes)
 b9 0b 00 00  ce 00 00 00  41 00 00 00  09 00 00 00  00 00 00 00  3f 01 00 00  00 00 00 00  08 00 00 00
 prop=0x0BB9=3001 IMAGEFILE | type=0xCE=206 FILENAME | cls 65 | part 9 | state 0 | res_id=0x013F=319 | reserved 0 | size 8
 no payload (res_id != 0): the image is RT_BITMAP #319 (BITMAPINFOHEADER 40, 49 x 10, 32 bpp, 2000 bytes); length 32 -> next at 54984

off 54984  (SIZINGMARGINS, MARGINS, inline, 48 bytes)
 11 0e 00 00  cd 00 00 00  41 00 00 00  09 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  10 00 00 00
 08 00 00 00  08 00 00 00  00 00 00 00  03 00 00 00
 prop=0x0E11=3601 SIZINGMARGINS | type=0xCD=205 MARGINS | cls 65 | part 9 | state 0 | res_id 0 | size 16 | left=8 right=8 top=0 bottom=3
 length = 32 + 16 = 48 -> next record at 55032 (SIZINGTYPE ENUM = 1 STRETCH)
```

Decoded with the schema: `[Window.FRAMEBOTTOM] IMAGECOUNT = 2`, `IMAGEFILE = bitmap#319`, `SIZINGMARGINS = 8 8 0 3` (matches `lhstyle.py`'s output). The
49x10 bitmap holds 2 sub-images (`IMAGELAYOUT = VERTICAL`, enum value 0): active 49x5 on top, inactive 49x5 below; nine-grid cut at left 8, right 8, bottom 3.

A colour record (offset 0): `DC 0E 00 00 | CC 00 00 00 | 05 00 00 00 | 0 | 0 | 0 | 0 | 04 00 00 00 | C8 C9 CA 00 | 00 00 00 00` -> prop 3804 EDGELIGHTCOLOR, type 204,
class 5 (`globals`), value R=0xC8=200 G=201 B=202 (`COLORREF` 0x00CAC9C8).

## 13. Minimal loader outline (C)

```c
/* 1. PE: map file, find resource dir (see sec. 2); getres(type_str_or_int, name_str_or_int, &len) */
/* 2. version = *(uint16_t*)getres("PACKTHEM_VERSION", 1)    -> 3: XP path, 4: this document */
/* 3. schema:  for each name in {STBL01, STBL02, ...}: split lines, build
 *      prop[id] = {name,type,flag}; enumerator tables; part tables per "<CLASS>PARTS"; state tables per "<PART>STATES" */
/* 4. cmap:    o = 0; while (o < len) { s = (WCHAR*)(d+o); n = wcs16len(s)+1; add(s); o += (2*n + 7) & ~7; }   -> class[i] */
/* 5. bcmap:   n = u32 at 0; base[i] = i32 at 4+4*i; globals = index of "globals"; class (globals+i) has parent (globals+base[i]) if base[i] >= 0 */
/* 6. vmap:    o = 0; while (o < len) { n = u32(d+o); s = (WCHAR*)(d+o+4); o += 4 + ((2*n + 7) & ~7); push }  -> triples (variant,size,color) */
/* 7. pick triple (default: first); data = getres("VARIANT", upper(variant)) */
/* 8. walk(data) as in 4.7; group by (cls,part,state); keep prop arrays sorted; resolve res_id lazily:
 *      FILENAME -> getres(2 /*RT_BITMAP*/, res_id), streams -> getres("STREAM"/"BLOB", res_id), string-backed -> RT_STRING table */
```
