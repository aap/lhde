# How XP's uxtheme draws themed window frames (for luxwm's classic msstyles mode)

> **Imported from the Longhorn X11 lab** (`~/fun/longhorn/x11` at d353b69, lhwm/CLASSIC-DECO-NOTES.md) into lhde in step 3 of docs/LAYOUT.md. Program and path names are updated to lhde (lhwm → luxwm, lhwmc → luxwmc, lhshell → luxshell, lhnav → luxnav; x11/... and lhwm/themes, lhshell/themes, lhshell/assets → the lhde tree and data directories, see docs/LAYOUT.md and tools/lhde-import); the X atoms (`_LH_*`, `_LHWM_*`, `_LHSHELL_*`), the window manager name "lhwm", luxnav's C API (`lhnav_*`, `lhnav.h`), configuration paths (`~/.config/longhorn`, `~/.config/lhwm/keys`, `~/.config/lhshell`) and setting values (`header_engine = lhnav`, `classic_start_menu = lhshell`) keep their names. Dated sections are history: they describe the x11 tree as it was (its compare_*.png and screenshots stay in the research workspace).

Source: `/u/aap/lib/nt5src/Source/XPSP1/NT/shell/themes/uxtheme/nctheme.cpp` (all non-client logic),
`imagefile.cpp`, `ninegrid2.cpp`, `textdraw.cpp`, `loader.cpp`, `../themeldr/{parser,utils,tmutils}.cpp`,
`comctl32/v6/blur.cpp`, `public/sdk/inc/tmschema.h`. Line numbers refer to nctheme.cpp unless named.
(Research notes gathered 2026-10-23; the Longhorn 4051/4074 styles follow the same engine.)

## 1. Frame geometry
- [SysMetrics] feed the system via SPI_SETNONCLIENTMETRICS (loader.cpp:1114-1130): iBorderWidth = SIZINGBORDERWIDTH,
  iCaptionHeight = CAPTIONBARHEIGHT, iSmCaptionHeight = SMCAPTIONBARHEIGHT, iCaptionWidth = CAPTIONBARWIDTH,
  fonts = CAPTIONFONT / SMALLCAPTIONFONT (DPI scaled).
- cnBorders (`_GetWindowBorders` 471-501): +2 for WS_EX_WINDOWEDGE (+1 for STATICEDGE instead), +1 for WS_CAPTION or
  WS_EX_DLGMODALFRAME, +iBorderWidth if WS_THICKFRAME and not minimised. Overlapped sizable window: 3 + SIZINGBORDERWIDTH
  (= SM_CXSIZEFRAME), same on all four sides. The FRAMELEFT/RIGHT/BOTTOM bitmap widths are ignored for geometry
  (the parts are stretched into cnBorders-wide rects); the bitmap only shapes the window region if TRANSPARENT.
- Rects (`_GetNcFrameMetrics` 2695-2744): CAPTION spans the full window width, height = cnBorders + iCaptionHeight + 1
  (the 1 px row below the caption). FRAMEBOTTOM = full width, top = bottom - cnBorders. FRAMELEFT/RIGHT run from
  caption.bottom to framebottom.top, cnBorders wide. Client top = caption.bottom (minus menu bar and WS_EX_CLIENTEDGE).

## 2. Caption buttons (`_GetNcBtnMetrics` 3319-3437)
- Size: GetThemePartSize(WP_CLOSEBUTTON, TS_TRUE) = bitmap / IMAGECOUNT; cxBtn = MulDiv(SM_CYSIZE, size.cx, size.cy);
  per window both minus 2*cyEdge (= 4): button = (aspect-scaled width - 4) x (iCaptionHeight - 4). All buttons use the
  close button's aspect ratio. SMALLCLOSEBUTTON with SM_CYSMSIZE likewise.
- Placement from cnROffset = caption.right - cnBorders: close.right = -cxEdge (2 px gap), max.right = close.left - cxEdge,
  min.right = max.left - cxEdge. Help takes the max slot (exclusive with min/max). Tool windows: close only.
- Vertical: top = caption.top + cnBorders + 2, height = iCaptionHeight - 4 (code has small/normal swapped, both give 2).
- CONTENTMARGINS and CAPTIONMARGINS are not used for buttons.
- Icon: left = caption.left + cnBorders + cxEdge, SM_CXSMICON square, top = caption.top + cnBorders + (iCaptionHeight - SM_CYSMICON)/2.
- States: FRAME FS_ACTIVE=1/FS_INACTIVE=2; buttons NORMAL=1 HOT=2 PUSHED=3 DISABLED=4; CAPTION/MAXCAPTION/MINCAPTION
  ACTIVE=1 INACTIVE=2 DISABLED=3. Button strip index = (framestate-1)*4 + state, 1-based: active 1-4, inactive 5-8.
  Max/min DISABLED when the window lacks WS_MAXIMIZEBOX/WS_MINIMIZEBOX; help always NORMAL; RESTOREBUTTON when maximised.
  Hot/pushed are painted over the existing button (TrackFrameButton 3630-3642). 8-image strips assumed.

## 3. CImageFile drawing (imagefile.cpp)
- SIZINGMARGINS order: left, right, top, bottom (parser.cpp:728). CONTENTMARGINS defaults to SIZINGMARGINS.
- A property given twice in one section counts with its first value: the parser appends in source order (loader.cpp
  `CThemeLoader::AddData`) and `CRenderObj::GetValueIndex` (render.cpp:1088) returns the first match. Luna's
  `[Window.MaxCaption]` has `SizingType = stretch` then `SizingType = Tile`: STRETCH (Wine and libuxtheme agree; the converter
  kept the last until 2026-10-24).
- Strips: IMAGECOUNT images laid out IL_HORIZONTAL (default) or IL_VERTICAL; state i uses offset (i-1)*single size.
- IMAGESELECTTYPE: ImageFile1..5; DPI: last file with MinDpiN <= dpi; SIZE/SOURCEGROW: last file whose MinSizeN fits.
- Nine-grid (GdiDrawStream DS_NINEGRID; semantics in ninegrid2.cpp): STRETCH is nearest-neighbour with a half-step start;
  TILE tiles the middle from its origin; TRUESIZE copies 1:1 aligned by HALIGN/VALIGN (centre) with BGFILL/FILLCOLOR;
  when dest < margins, no middle: corners split the width proportionally. Margins are scaled only with SOURCESHRINK/SOURCEGROW.
- Transparency: TRANSPARENT uses TRANSPARENTCOLOR (default magenta). True alpha only if some pixel has alpha not in {0,255}
  (then premultiplied AlphaBlend); 0/255-only alpha is treated as opaque RGB unless TRANSPARENT. Alpha wins over TRANSPARENT.
- GLYPHTYPE=IMAGEGLYPH: background first (unless GLYPHONLY), glyph TRUESIZE into the content rect, centred; the glyph file
  (IMAGEFILE1..5) is selected for the content rect too (`DrawBackground`, imagefile.cpp:1529-1545). Wine's draw.c (and so
  libuxtheme) uses the whole part rect for both.

## 4. Caption text
- Text rect: CAPTIONMARGINS of the part in use (ACTIVE state) widened past the icon (text starts at icon.right +
  CAPTIONMARGINS.left) and the leftmost button. Vertically centred in (caption height - cnBorders) using
  GetThemeTextExtent(WP_CAPTION). CONTENTMARGINS unused.
- Flags: DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS (+ CONTENTALIGNMENT). Font = CAPTIONFONT/SMALLCAPTIONFONT unless the
  part defines FONT. Colour is always the system colour: [SysMetrics] CAPTIONTEXT / INACTIVECAPTIONTEXT (part TEXTCOLOR
  overridden); shadow colour/offset/type come from the part+state.
- TST_SINGLE: one shadow copy in TEXTSHADOWCOLOR at the offset (bottom rows clipped by a comma-operator bug), then text.
- TST_CONTINUOUS (blur.cpp:232-299): text rendered into a (W+10)x(H+10) buffer at (5,5), alpha = 2-pass box blur
  (horizontal sum of next 4 >>2, vertical sum of 5 rows >>2, clamped), tinted, blended at (left-4+dx, top-8+dy), text on top.

## 5. MAXCAPTION / MINCAPTION / SMALLCAPTION
- Minimised: MINCAPTION over the whole window rect, no frames.
- Full-screen maximised: MAXCAPTION, side/bottom frames not drawn; background inset by cnBorders on top/left/right,
  visible caption = iCaptionHeight + 1 px; buttons/text keep their offsets (cnBorders lies off-screen).
- Partially maximised: MAXCAPTION with normal frames. Tool windows: SMALLCAPTION/SMALLFRAMEx, iSmCaptionHeight,
  SMALLCLOSEBUTTON only, no icon. Caption margins follow the same part choice.
