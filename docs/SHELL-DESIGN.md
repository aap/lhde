# luxshell: the Longhorn taskbar and sidebar

luxshell ("lux", the Avalon shell; flxp is the wux side) draws the Avalon taskbar, the sidebar with its tiles and flyouts, the
toasts and the tray for the 4042-5112 looks. The classic Win32 taskbar and the start menu are flxp's (flxp-taskbar,
flxp-startmenu); luxshell starts them and talks to them through the root properties and messages below. Sections 1-3 are the
research the shell is built from (what the builds do, with sources); sections 4-17 say how luxshell does it. Use:
docs/USAGE.md; settings: docs/SETTINGS.md; theme files: docs/THEME-FORMAT.md; the WM side: docs/LUXWM.md. luxshell's windows have the
X class `luxshell` (names `luxshell-taskbar`, `luxshell-sidebar`, ...); the atoms `_LHSHELL_*` and `_LH_*` are stable names.

Everything numeric below is taken from a file in this tree; the source is named in each table.
Rows marked **(inferred)** are our reading of the data, not a stated fact. Pixel sizes of
bitmaps come from ImageMagick `identify`; screenshot measurements come from luminance edge scans
of `screenshots/4074-dwm.png` (1280x1024) and `screenshots/4051-vm-now.png` (1152x864) with
crops in the scratch directory.

Conventions: uxtheme `SIZINGMARGINS`/`CONTENTMARGINS` are **left right top bottom** (the
`MARGINS` struct order). Avalon `BorderThickness`/`HackNineGrid` are WPF `Thickness` order
**left, top, right, bottom** (inferred from `HackNineGrid="0,7,0,0"` on the 1038x30 taskbar strip,
whose only non-uniform band is the 7 top rows; see 2.2). Font heights in the `.ini` FONT blobs are
LOGFONT `lfHeight` at 96 dpi: -11 = 8 pt, -12 = 9 pt, -19 = 14 pt.

Source shorthands:

| Short | Path |
| --- | --- |
| `slate74` | `res/4074/slate_longhorn.ini` + `res/4074/slate.msstyles/BITMAP_<id>_1033.bmp`, `STREAM_<id>_1033.png` |
| `jade74` | `res/4074/jade_longhorn.ini` + `res/4074/jade.msstyles/...` |
| `slate51` | `res/4051/slate_longhorn.ini` + `res/4051/slate.msstyles/BITMAP_<id>_1033.bmp`, `BLOB_<id>_1033.png` |
| `imageres` | `res/4074/imageres.dll/PNGFILE_<id>_1033.png` (same files as `mock/assets/imageres/<id>.png`) |
| `xaml/*` | `res/4074/xaml/xaml_sidebar_*.xaml`, `xaml_notification_notification.xaml` |
| `XP` | `~/lib/nt5src/Source/XPSP1/NT/shell/explorer/{tray,taskband,traynot,trayclok}.cpp` |
| `il` | `res/4074/il/desktop.il` (System.Windows.Desktop IL dump) |
| `sym` | `res/sym4051/explorer_syms.txt`, `res/sym4051/system_syms.txt` |
| `sdk` | `wim/sdk4051/lhsdk/LHSDK/shell/sidebar/`, `.../samples/shellcore/` |

---

## 1. The three shells we have to reproduce

| Build / theme | Taskbar | Sidebar | Where the imagery lives |
| --- | --- | --- | --- |
| 4051 Slate (screenshot `4051-vm-now.png`) | **classic Win32** `CTray`/`CTaskBand` drawn through the uxtheme classes `TaskBar`, `TaskBand`, `TrayNotify*`, but with the Avalon start button image (imageres 1156, bilinear, 2.1.2); 29 px tall (rows 835..863); tray clock hidden because the sidebar shows a clock (`CAvalonTaskBar::_ToggleClock`, COMPOSITION.md §11) | Avalon "start bar" (`startbar.cpp`, registry `Explorer\StartBar\Modules`), Slate look: flat dark grey, clock near the top | uxtheme `.msstyles` BITMAPs; the `Sidebar` class holds its 60 parts as `IMAGEDATA` BLOBs |
| 4074 Jade (default shipped look) | Avalon taskbar is available but the Jade `.msstyles` still carries the classic `TaskBar`/`TaskBand` bitmaps in Segoe UI 9 | Avalon sidebar, `Slate_*Resources` or `Aero_*Resources` dictionaries | `imageres.dll` PNGs 1000-1162 (Slate set) and 1167-1201 (Aero set), plus a few `theme://Sidebar,...` parts as `PNGSTREAM`s |
| 4074 Aero/DWM (screenshot `4074-dwm.png`) | Avalon taskbar, 30 px glass strip (imageres 1187), glass start button (1179), 140 px task buttons, round sidebar toggle | Avalon sidebar 149 px wide, 70 % black glass (1178), tiles separated by 1199 lines, clock tile pinned at the bottom | `imageres.dll` 1167-1201 |

The classic path and the Avalon path are two renderers over the same model (window list, tray,
clock, tiles): luxshell draws the Avalon one, flxp-taskbar the classic one (section 12).

---

## 2. Fact sheets

### 2.1 Classic taskbar (uxtheme classes)

#### 2.1.1 Bitmap ids per theme

The three `.ini`s define the same parts with the same margins (except where noted) and only
renumber the bitmaps. Sizes are identical between `slate74` and `slate51`.

| Class.Part | slate74 id | slate51 id | jade74 id | Size (slate / jade) | IMAGECOUNT, layout | SIZINGMARGINS l r t b | CONTENTMARGINS | Other |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `TaskBar.BACKGROUNDBOTTOM` | 266 | 241 | 345 | 50x28 / 50x30 | 1 | 0 0 15 12 (jade 1 0 1 1) | - | TILE (jade STRETCH), FILLCOLORHINT 231 232 234 |
| `TaskBar.BACKGROUNDTOP` | 269 | 244 | 348 | 50x30 / 50x30 | 1 | 0 0 13 15 (jade 1 0 16 12) | - | TILE / STRETCH |
| `TaskBar.BACKGROUNDLEFT` | 267 | 242 | 346 | 28x41 / 30x41 | 1 | 15 12 0 0 (jade 1 1 1 0) | - | TILE |
| `TaskBar.BACKGROUNDRIGHT` | 268 | 243 | 347 | 28x41 / 30x41 | 1 | 12 15 0 0 (jade 1 1 1 1) | - | TILE |
| `TaskBar.SIZINGBARBOTTOM/TOP` | 270 / 273 | 245 / 248 | 349 / 352 | 15x4 | 1 | - | - | TILE; TOP is TRANSPARENT |
| `TaskBar.SIZINGBARLEFT/RIGHT` | 271 / 272 | 246 / 247 | 350 / 351 | 4x15 | 1 | - | - | TILE / STRETCH |
| `TaskBand.FLASHBUTTON` | 264 | 239 | 343 | 26x28 / 26x29 | 1 | 12 10 12 12 | 0 0 4 4 | TRANSPARENT, STRETCH; orange |
| `TaskBand.FLASHBUTTONGROUPMENU` | 265 | 240 | 344 | 30x23 / 27x23 | 1 | 17 5 12 7 | 4 3 4 4 | TEXTCOLOR 55 55 56 (jade 0 2 92) |
| `TaskBand::Toolbar.BUTTON` (task buttons) | 379 | 349 | 466 | 26x168 / 26x174 | 6, VERTICAL → 26x28 (jade 26x29) per state | 4 4 16 12 | 0 4 4 4 | TRANSPARENT, STRETCH |
| `TaskBand::Toolbar.DROPDOWNBUTTON` (group buttons) | 379 | 349 | 466 | same | 6 | 4 4 16 12 | 0 4 4 4 | |
| `TaskBandVert::Toolbar.BUTTON` | 382 | 352 | 469 | 28x138 / 26x174 | 6 → 28x23 / 26x29 | 4 4 4 4 | 0 4 4 4 (DROPDOWN 4 4 4 4) | vertical taskbar |
| `TaskBandGroupMenu::Toolbar` (group popup bg) | 381 | 351 | 468 | 28x10 | 1 | 3 3 5 3 | 9 9 0 0 | TILE |
| `TaskBandGroupMenu::Toolbar.BUTTON` | 380 | 350 | 467 | 30x69 | 3 → 30x23 | 6 8 5 5 | 9 9 3 3 | ACCENTCOLORHINT 79 150 255 |
| `TaskBar::Rebar.GRIPPER` / `GRIPPERVERT` | 387 / 388 | 357 / 358 | 474 / 475 | 6x16, 16x6 / 6x12, 12x6 | 1 | 1 0 3 2, 2 3 1 0 (jade 0) | 1 1 1 1 | TILE |
| `TaskBar::Rebar.CHEVRON` | 383 (+glyph 384) | 353 (+354) | 470 (+471) | 13x168 (6 → 13x28), glyph 8x72 (6 → 8x12) | 6 | 4 4 2 2 | 0 0 0 0 | VALIGN TOP, GLYPHTYPE IMAGEGLYPH |
| `TaskBar::Rebar.CHEVRONVERT` | 385 (+386) | 355 (+356) | 472 (+473) | 22x90 (6 → 22x15), glyph 5x48 / jade 13x168 | 6 | 4 4 3 3 (jade 4 4 15 3) | 0 | |
| `TaskBar::Toolbar.BUTTON` (quick launch) | 383 | 353 | 470 | 13x168 | 6 → 13x28 | 4 4 16 8 | 0 0 4 4 (DROPDOWN 2 0 4 4) | TRANSPARENT |
| `TaskBarVert::Toolbar.BUTTON` | 385 | 355 | 472 | 22x90 / 13x168 | 6 | 4 4 15 3 | 1 1 1 1 | |
| `TrayNotifyHoriz::TrayNotify.BACKGROUND` | 390 | 360 | 477 | 110x28 | 1 | 13 10 14 13 (jade 34 10 12 12) | 19 7 0 0 | TRANSPARENT, TILE, FILLCOLORHINT 203 204 218 |
| `TrayNotifyVert::TrayNotify.BACKGROUND` | 392 | 362 | 479 | 28x110 | 1 | 14 13 34 10 (jade 12 12 34 10) | 0 0 19 7 | |
| `TrayNotifyHoriz::Button` / `TrayNotifyVert::Button` (chevron bubble) | 389 | 359 | 476 | 19x60 | 3 → 19x20 | - | - | TRUESIZE |
| `TrayNotifyHorizOpen::Button` / `VertOpen` | 391 | 361 | 478 | 19x60 | 3 | - | - | TRUESIZE |
| `Start::Button` | 365 | 335 | 452 | 99x99 / 99x87 | 3 → 99x33 / 99x29 | 6 13 4 2 | 7 19 2 4 | TRANSPARENT, STRETCH, TEXTSHADOWOFFSET 2 2, TEXTSHADOWCOLOR 75 75 75, TEXTCOLOR 255 255 255, FILLCOLORHINT 74 153 73 |
| `StartMenu::MenuBand.NEWAPPBUTTON` | 366 | 336 | 453 | 9x2 / 8x8 | 1 | 6 1 0 0 (jade 2 2 3 3) | 10 0 3 0 (jade 8 0 2 0) | |
| `StartMenu::MenuBand.SEPERATOR` | 367 | 337 | 454 | 185x9 | 1 | 6 1 3 0 | 10 0 3 0 | |
| `StartMenu::Toolbar` (bg) | 369 | 339 | 456 | 184x22 | 1 | 32 5 2 2 | 0 | TILE, TEXTCOLOR 55 55 56 / 0 2 92 |
| `StartMenu::Toolbar.BUTTON` | 368 | 338 | 455 | 8x6 / 21x63 | 3 | 6 1 0 0 (jade 10 10 3 3) | 8 2 2 0 (jade 8 0 2 0) | |
| `TextGlow.BACKGROUND` | 393 | 363 | 480 | 21x21 | 1 | 10 10 10 10 | - | STRETCH (used by luxwm already) |

Fonts and text colours (decoded from the FONT blobs; `slate51` = `slate74`):

| Class | Slate | Jade |
| --- | --- | --- |
| `Start::Button` | Franklin Gothic Medium, lfHeight -19 (14 pt), italic flag set | Segoe UI, -19 (14 pt) |
| `TaskBand::Toolbar`, `TaskBandVert::Toolbar`, `TaskBar::Toolbar`, `TaskBar::Rebar`, `TaskBandGroupMenu::Toolbar`, `StartMenu::Toolbar`, `TrayNotify::Clock` | Tahoma -11 (8 pt) | Segoe UI -12 (9 pt) |
| `TaskBand.GROUPCOUNT` | Tahoma Bold 8 pt, TEXTCOLOR 237 176 120 | Segoe UI Bold 9 pt, TEXTCOLOR 0 2 92 |
| `TaskBand::Toolbar` text | 255 255 255 | 0 2 92 |
| `TaskBar::Toolbar` text (quick launch) | 255 255 255 | 255 255 255 |
| `TaskBarVert::Toolbar` text | 55 55 56 | 0 2 92 |
| `TaskBar::Rebar` text | 200 200 200 | 0 2 92 |
| `TrayNotify::Clock` text | 255 255 255 | 255 255 255 |
| `StartMenu::MenuBand` text | 75 75 75 | 75 75 75 |
| `[sysmetrics]` | CAPTIONFONT Tahoma Bold 8, MENUFONT/STATUSFONT/ICONTITLEFONT Tahoma 8, CAPTIONBARHEIGHT 19, SCROLLBARWIDTH 17, FLATMENUS, HIGHLIGHT 237 176 120, BTNFACE 233 233 233 | all fonts Segoe UI 9 (Quality:ClearType-Natural), CAPTIONBARHEIGHT 19, HIGHLIGHT 197 231 246, BTNFACE 244 244 244, `USESCOMPOSITION = FALSE` in globals (true only in the longhorn.ms aero-4074 patch) |

The `Sidebar` uxtheme class has **no** FONT/COLOR properties in the decompiled `.ini`s, only
stream ids (see 2.3.5); the font names listed in COMPOSITION.md §6 come from the class map, not
from this dump.

#### 2.1.2 What the six task-button states are

`TaskBand::Toolbar.BUTTON` has IMAGECOUNT 6, IMAGELAYOUT VERTICAL. uxtheme's toolbar part
`TP_BUTTON` state order is NORMAL(1), HOT(2), PRESSED(3), DISABLED(4), CHECKED(5), HOTCHECKED(6).
Reading the strips (`slate74` 379, `slate51` 349: light grey, white/highlight, dark, dark,
dark, lighter dark; `jade74` 466 similar): normal = light grey plate, hot = white, pressed =
dark, disabled = dark, checked (active window) = dark, hotchecked = lighter dark. In the 4051
screenshot the active "Computer" button is the dark plate, matching CHECKED.
The 3-state `Start::Button` strip is NORMAL (dark slate pill), HOT (bright green), PRESSED (dark
green); in `slate51` 335 the flag logo is **not** in the bitmap: explorer draws `IDB_START`
(`tray.cpp:3916-3925`, `start.bmp`) as the button image and the string `IDS_START` = "start"
(`explorer.rc:306`); the 4051 screenshot shows only the flag, so 4051 either shipped an empty
string or a logo-only `IDB_START` **(inferred)**. But the 4051 explorer.exe (and the 4051chk one) has no
bitmap 143 (`IDB_START`), 176 or 178 at all, and the start button of `4051-vm-now.png` is not the 335 pill: it is imageres
1156 (the Avalon `StartButton` image, 74x23, flag included) drawn 79x29 **bilinear** at the bar's top-left, which reproduces
the shot to a mean squared error of 0.2 per pixel (nearest 249). Only the task band (dark 146 px plates = 349) and the bar
background are the classic parts. See "4051 vs 4074 Slate" in section 5.

#### 2.1.3 Composition and metrics confirmed in the XP SP1 source

| Fact | Source |
| --- | --- |
| Task button height = `tbm.cyPad + g_cySize` where `g_cySize = GetSystemMetrics(SM_CYSIZE)` (`initcab.cpp:754`); the band reports `ptActual.y = g_cySize + 2*g_cyEdge`, min horizontal width `3 * buttonHeight`, `ptIntegral.y = buttonHeight + cyButtonSpacing` | `taskband.cpp:537-556`, `5665-5673` |
| Themed toolbar metrics: `cxPad = 20` (8 unthemed), `cyBarPad = 0`, `cxButtonSpacing = cyButtonSpacing = 0` (3 unthemed) | `taskband.cpp:5847-5851` |
| Ideal button width: `iIdeal = winWidth / cols` (minus spacing when cols > 1), clamped to `iMax = g_cxMinimized` (`SM_CXMINIMIZED`, 160 on a default XP) for a horizontal bar; `iMin = (g_cySize + 2*g_cxEdge) * 1.8 + textSpace` horizontally, `g_cySize + 2*g_cxEdge` vertically | `taskband.cpp:1425-1466` |
| Remainder pixels are spread over the first buttons (`_SizeItems(iIdeal, iRemainder)`) | `taskband.cpp:1586-1590` |
| When forced to `iMin`: group (glom) first, else add scroll bars; ungroup when `cOpenSlots >= 2` | `taskband.cpp:1577-1650` |
| Grouping registry: `Advanced\TaskbarGlomming` (bool), `Advanced\TaskbarGroupSize` with `GLOM_OLDEST 0` (default), `GLOM_BIGGEST 1`, `GLOM_SIZE 2` (>= 2 means "group when the group has that many"); a group of one is never shown grouped; `g_rgNoGlom` exclusion list | `taskband.cpp:38-44, 63, 2905-2955, 6250-6263` |
| Group button needs 1.2x height vertically for the drop arrow | `taskband.cpp:545` |
| Start button: text `IDS_START`/`IDS_STARTCLASSIC`; `_sizeStart` from `Button_GetIdealSize` (theme CONTENTMARGINS + text extent); height = `max(ideal, TBC_BUTTONHEIGHT)` when themed; text dropped when the bar is narrower than the button | `tray.cpp:620-662`, `3904-3929` |
| Taskbar default thickness: `_sStuckWidths.cy = _sizeStart.cy + 2*(g_cyDlgFrame + g_cyBorder)`; horizontal task view starts at `_sizeStart.cx + g_cxFrame + 1`, vertical at `_sizeStart.cy + g_cyTabSpace` | `tray.cpp:492-497`, `921`, `932` |
| Stuck rect: snapped to the monitor edge; themed and sizable adds the sizing bar (`_AdjustRectForSizingBar`); unthemed inflates by `g_cxEdge, g_cyEdge` | `tray.cpp:1030-1054` |
| Autohide: `IDT_AUTOHIDE` polls every 500 ms; hides when the cursor is outside the stuck rect inflated by `4*g_cxEdge, 4*g_cyEdge`, the tray is not active, no start pane and no system menu is up, no balloon | `tray.cpp:3850-3855`, `4082-4115` |
| Unhide: mouse movement over the hidden bar beyond `SM_CXDOUBLECLK*SM_CYDOUBLECLK` restarts a 50 ms `IDT_AUTOUNHIDE`; on expiry, if the cursor is still inside → `Unhide()` | `tray.cpp:3883-3900`, `4118-4133` |
| Hidden rect leaves `g_cxFrame/2` (SM_CXFRAME is 4 → 2 px) visible at the edge | `tray.cpp:3618-3650` |
| Slide animation: `_dtSlideHide = 400 ms`, `_dtSlideShow = 200 ms` (0 on `SM_SLOWMACHINE`); linear interpolation of the rect, one step per `ONEFRAME` = 20 ms, only when `SPI_GETMENUANIMATION` and full-window drag are on | `tray.cpp:1866-1880`, `3503-3600` |
| Tray clock: format `LOCALE_STIMEFORMAT` through `GetTimeFormat(..., TIME_NOSECONDS, ...)`; min size = max(AM text, PM text) with a two-digit "slop", height `text + 4*g_cyBorder`, at least `g_cySize + g_cyEdge`; adds a day line and a short-date line when `cyMax` allows (vertical bar); tooltip shows the long date; timer re-armed for the next minute | `trayclok.cpp:85, 147-154, 446-470, 532-603` |
| Tray layout: content rect from `GetThemeBackgroundContentRect(TNP_BACKGROUND)` (i.e. the 19 7 0 0 CONTENTMARGINS above); icon toolbar 16 px buttons (`SM_CXSMICON`), height trimmed to a multiple of the button size, centred; clock to the right (bottom when vertical), chevron `_szChevron` to the left | `traynot.cpp:1982-2060`, `1648` |
| Tray timers: chevron info tip 30 s, rude-app hide 10 s, balloon 10 s | `traynot.cpp:30-34`, `tray.cpp:129` |
| Persistence: `Explorer\StuckRects2\Settings` = `TVSD {dwSize, lSignature<0, dwFlags (TVSD_AUTOHIDE, TVSD_HIDECLOCK, ...), uStuckPlace, SIZE sStuckWidths, RECT rcLastStuck}`; `Advanced\TaskbarSizeMove` | `tray.h:75-96`, `tray.cpp:514, 614, 1414` |

Measured on the 4051 screenshot (Slate, 1152x864): taskbar occupies rows 836..863 = **28 px**
(= the 50x28 `BACKGROUNDBOTTOM` bitmap, so `_sizeStart.cy + 8 = 28`, start button 20 px tall
**(inferred)**); start button x = 0..~77 (pill) with the task band starting at x = 84; the single
task button "Computer" is x = 84..230 = **146 px** (one window, so `iIdeal` was clamped by
`g_cxMinimized`-ish logic; XP's 160 would give 160, so 4051's `SM_CXMINIMIZED` or `cxPad`
differs **(inferred)**); tray at the far right shows only the 19x20 chevron bubble, no clock;
the bar stops at x ≈ 1002 where the sidebar begins.

### 2.2 Avalon taskbar (4074, `taskbar.xaml` + `*_taskbarresources.xaml`)

#### 2.2.1 Element tree (`xaml/xaml_sidebar_taskbar.xaml`)

```
FillPanel#Taskbar
├─ FillPanel#Taskbar2
│  ├─ Border#TaskbarBlackLayer      Background #000000, Opacity 0   (animated to 1 behind a maximised window)
│  ├─ HackImage#TaskbarBackground   Style TaskbarBackground
│  └─ DockPanel#InnerTaskbar
│     ├─ StartButton#StartButton         Style StartButton        (Dock Left)
│     ├─ SidebarButton#TaskbarToggleBar  Style TaskbarToggleBar   (Dock Right)
│     └─ TaskList#TaskList (Dock Fill)
│        └─ DockPanel#TaskOverflow
│           ├─ TileThumb#TaskSizer        Style TaskSizer, Cursor SizeNS (Dock Bottom)
│           ├─ TaskChevron#TaskChevron    Style TaskChevron        (Dock Right)
│           └─ TaskOverflowableCollection#TaskOverflowableCollection (Dock Fill)  ← TaskButton children
└─ DockPanel
   └─ TileThumb#TaskbarSizer  Style TaskbarSizer, Cursor SizeWE
```

`extendedtaskbar.xaml` is the same minus StartButton/SidebarButton/TaskSizer and with
`TaskbarBlackLayer` **opaque** (no `Opacity="0"`): the secondary-monitor taskbar is an opaque
black bar **(inferred)**.

`taskbutton.xaml`: `<AreaButton Style="{TaskButton}" ClipToBounds="true"/>`.
`taskitem.xaml` content: `Border(0,0,2,0){Image#TaskItemIcon 16x16}` + `Text#TaskItemText`
(luxshell's icon rule: 6.2) (Style TaskItemText = white, VerticalAlignment Center, TextWrap Wrap, TextTrimming
CharacterEllipsis). `taskgroupitem.xaml`: same plus `Text#TaskItemCount` (bold) + 2 px spacer
on the left and a Marlett "u" (down arrow) docked right.

#### 2.2.2 Styles and imageres ids (`Aero_TaskbarResources`; Slate ids in brackets)

| Style | Element size / layout | Images (Aero [Slate]) with `identify` size | Triggers |
| --- | --- | --- | --- |
| `TaskbarBackground` | fills the bar | 1178 [1000] (sidebar strip, 150x786 [149x20]) default; `Appbar.Rotated=true` → 1187 [1155] (1038x30 [50x28]) with `HackNineGrid 0,7,0,0` | `Appbar.Flipped=false` → `HackFlipHoriz` |
| `StartButton` | Width 83, Height 29, Dock Left; VisualTree `Border(0,0,4,0) > HackBorder` | 1179 (78x28) rest, 1180 hover (`StartButton.IsMouseOver`), 1181 `StartButton.FakePressed` [1156/1157/1158, 74x23] | `Appbar.Rotated=false` (vertical bar): Dock top, border 0,0,0,4, Height 29 |
| `TaskbarToggleBar` (SidebarButton) | Dock Right; `TransformDecorator(VCenter) > Border(6) 29x29 > HackBorder` → 17x17 glyph | 1170 (17x17) rest, 1171 hover, 1172 pressed, `Taskbar.BarVisible=false` → 1167 [1159/1160/1161/1162] | collapsed when `Appbar.Rotated=false` |
| `TaskChevron` | 24x29, Dock Right; `Border(0,3,0,2) > HackBorder#Background > Border(4) > HackBorder#Glyph` | glyph 1003 (16x16); background 1110 hover (20x23), 1111 pressed / `TaskChevron.Active` | vertical: Dock bottom |
| `TaskSizer` (row-count thumb) | Width 100 %, Height 4, Dock Bottom; `Border(0,2,0,0) > HackBorder` | 1028 (150x2) rest, 1200 [1030] hover | |
| `TaskbarSizer` | Width 6, Height 100 %, Dock Right | 1201 (2x20) [1108 (6x20)] | Rotated → width 0; Flipped → Dock left; Flipped=false → flip |
| `TaskButton` | `Border(0,2,2,0) > HackBorder#Background HackNineGrid 3 > Border(6,5,6,6) > content` | 1182 (127x29) rest, 1184 hover, 1185 pressed and `TaskButton.Active`, 1183 (109x24) `TaskButton.Flash` [1150/1151/1152/1152/1154, all 109x24] | |
| `TaskItemText` | | `Text Foreground #FFFFFF` | |

Alpha of the Aero assets (PIL): 1187 is white (`rgb 248`) at **alpha 45-62/255** per row (18-24 %)
with a brighter 1 px line at row 1 (alpha 118) and the bottom row (99): the taskbar is a faint
white glass strip over the DWM blur. 1182/1185 are white at mean alpha 71/106 with a solid 1 px
rim; 1179 is dark (`rgb 68`) at alpha ~200 with a soft right edge (cols 72-77 fade 164→50) that
the 4 px right `Border` leaves room for.

#### 2.2.3 Measured on `4074-dwm.png` (1280x1024, Aero)

| Metric | Value | How |
| --- | --- | --- |
| Taskbar height | rows 994/995..1023 → **30 px** (= 1187 height; StartButton 29 + 1) | column scan x=600 |
| Start button | x = 0..83 (XAML Width 83 = 78 px image + 4 px border + 1) | row scans y=1010/1018 |
| Task buttons | x = 84..221, 224..361, 364..501 → **140 px pitch**, 138 px plate + 2 px right border; three buttons left-packed, remaining width empty | row scan y=1018 |
| Sidebar toggle | round 17 px button at x ≈ 1108..1125, vertically centred | crop `z4074_tb_right.png` |
| Taskbar extent | ends at x ≈ 1130: the taskbar is shortened by the sidebar width ("mixed taskbar/sidebar", `CShellTaskbar::SetMixedTaskbarSidebar`) | row scan |
| Text | white Segoe UI-like 9 pt with 16 px icon, 2 px gap, ellipsis ("Windows Task Ma...") | crop `z4074_tb_left.png` |

### 2.3 Avalon sidebar (4074, `sidebar.xaml` + `*_sidebarresources.xaml`)

This is the 4074 layout (`[sidebar] stacking = top`): the sidebar above the windows with its strut, the taskbar shortened. The
other arrangement, the taskbar running the full width and the sidebar ending at its top, is `stacking = bottom` (7.2).

#### 2.3.1 Element tree (`xaml/xaml_sidebar_sidebar.xaml`)

```
Appbar#Sidebar
├─ Border#SidebarBlackLayer           #000000, Opacity 0  (maximised-window layer)
├─ HackImage#SidebarBackground        Style SidebarBackground
├─ DockPanel#sidebarMain
│  ├─ FillPanel#sidebarTitle (Dock Top)
│  │  └─ OptionsButton#sidebarOptions  Style SidebarOptions (Height 17)
│  │     └─ DockPanel { Border(SidebarOptionsGlyphBorder){HackImage SidebarOptionsGlyph}, Text#OptionsText "Options" Opacity 0 }
│  ├─ HackImage#SidebarOptionsSeperator  (Dock Top, 1199, Height 2)
│  ├─ SBPinnedContentHostCollection#SBPinnedContentHostCollection (Dock Bottom; Rotated → right)   ← pinned tiles (clock)
│  ├─ HackImage#SidebarClockSeperator    (Dock Bottom, 1199, Height 3; 0 when BarMinimized)
│  ├─ SBContentHostOverflow#SBContentHostOverflow (Dock Bottom)
│  │  └─ DockPanel { TransformDecorator{Button#OverflowExpando}, TransformDecorator{OverflowPanel#OverflowItems} }
│  └─ SBContentHostCollection#tileCollection (Dock Fill)                                            ← OuterTile > Tile children
└─ DockPanel
   └─ TileThumb#SidebarSizer  Style SidebarSizer, Cursor SizeWE
```

#### 2.3.2 Sidebar styles (`Aero_SidebarResources`; Slate in brackets)

| Style | Size / layout | Images | Triggers |
| --- | --- | --- | --- |
| `SidebarBackground` | fills | 1178 (150x786) `HackNineGrid 36,0,0,0` [1000 149x20, no grid]; Rotated → 1187 [1155] grid 0,7,0,0 | Flipped=false → HackFlipHoriz |
| `TileBackground` | **Slate only**: each tile repaints 1000 [Rotated 1155] under itself; Aero has no such style (tiles are transparent over the glass) | | |
| `SidebarOptions` | OptionsButton Height 17 | | |
| `SidebarOptionsGlyphBorder` | Border 4,7,2,2, Dock Right (Flipped=false → left) | | |
| `SidebarOptionsGlyph` | HackImage 9x6 | 1176 (10x6) rest, 1177 `AreaButton.IsMouseOverArea` [1103/1104] | Flipped=false → flip |
| `SidebarOptionsText` | Text white, Dock Right (Flipped=false → left); starts at Opacity 0 (fades in on hover, per mock) | | |
| `SidebarOptionsSeperator` / `SidebarClockSeperator` | Height 2 / 3 | 1199 (148x3) [1028 150x2] | BarMinimized → clock separator Height 0 |
| `SBPinnedContentHostCollection` | Dock Bottom | | Rotated → Dock right |
| `SBContentHostOverflow` | Dock Bottom; VisualTree: `Sizer` 1199 Height 2 (Top), `Spacer` Height 3, `Background` HackBorder (Fill) with content, `BottomSpacer` Height 0 | `DropTarget=true` → `theme://Sidebar,SDB_TASKBACK,SDBTB_SELECTED` | BarMinimized → Sizer 0, Spacer 0, BottomSpacer 2 |
| `OverflowExpando` | Button 24x24, Dock Top; `Border(2,2,3,3) > HackBorder` → 19x19 | 1008 rest, 1009 hover, 1004 `OverflowPanel.Expanded` (all 19x19) | Rotated → Dock left |
| `OverflowItems` | OverflowPanel | | BarMinimized → Height 24 |
| `OverflowButton` (`overflowbutton.xaml`: DragButton) | `HackBorder#Background HackNineGrid 2 > HackBorder(4) > FillPanel` | 1110 hover, 1111 pressed (20x23) | |
| `SidebarSizer` | TileThumb Width 2 [6], Height 100 %, Dock Right | 1201 hidden [1108 visible] | Rotated → width 0; Flipped → Dock left |
| `FlyoutBackground` | | 1175 (129x152) `HackNineGrid 53,3,3,3` [1109 (129x152) grid 3] | |
| `FlyoutBorder` | Border 8,4,8,8 [4,0,4,4] | | |
| `FlyoutOptions` / glyph / text | Height 17; glyph border 4,5,4,4; glyph 9x6 1176/1177; text white Opacity 0 | | |

1178 (Aero sidebar strip) is near-black (`rgb 15`) at **alpha 167-175** (≈ 68 %) across the
body, with a 2 px brighter edge (alpha 219/231) at column 0-1 and a gradient to alpha 202 on the
last 50 columns; 1175 (flyout) is the same black at alpha 178-181 with a 1 px rim (alpha 196-198).

#### 2.3.3 Tile chrome (`tile.xaml`, `outertile.xaml`, tile styles)

```
FillPanel                                   (Tile)
├─ HackImage#SidebarBackground   Style TileBackground   (Slate only, see above)
├─ HackImage#OuterTileBack       Style OuterTileBack, Opacity 0, Height 10   (style not defined in either dictionary; drag/drop insertion marker (inferred))
├─ Border(BorderThickness 1,0,0,0, Dock Fill)
│  └─ FillPanel#TileUI Opacity 0            (faded in once loaded (inferred))
│     ├─ FillPanel#TileBackground           (tile's Background content goes here)
│     └─ DragButton#TileContent Style TileContent
│        └─ DockPanel 100% x 100%
│           ├─ HackBorder#TileSideAreaBack  Style TileSideAreaBack (Dock Left; Flipped → Right)
│           │  └─ DockPanel#TileSideButton Width 9
│           │     ├─ Border(0,2,1,0){HackImage TileArrow 8x8: 1173 rest, 1174 IsMouseOverArea [1105/1106]}
│           │     ├─ Button#TileHeaderRestore 8x8  1058/1059 hover, Collapsed unless OuterTile.MWT
│           │     └─ Button#TileHeaderClose   8x8  1061/1062 hover, Collapsed unless OuterTile.MWT
│           ├─ Border TileForegroundPadder2 (Height 3, Dock Top)
│           ├─ Border TileForegroundPadder  (Width 6, Dock Right; Flipped → Left)
│           └─ FillPanel#TileForeground (Dock Fill)   (tile's Foreground content)
└─ DockPanel { TileThumb#SidebarSizer }
```

`outertile.xaml` (the host each tile sits in): `HackImage#OuterContentHostBack` 1107 (150x12),
Opacity 0, Height 10; `DockPanel { TileThumb#OuterContentHostSizer (Dock Bottom, Height 4,
Cursor SizeNS; VisualTree Border(0,2,0,0) > HackBorder 1199, hover 1200), FillPanel#OuterContentHostContent ClipToBounds }`.
So every tile has a 4 px grab strip at its bottom edge, and tile heights are user-resizable.

Tile content width **(inferred arithmetic)**: 149 (bar) - 1 (left border) - 9 (side strip) - 6
(padder) = **133 px**, with a 3 px top pad; in the screenshot the news tile text starts ~8 px in
from the bar edge and the picture tile is full width (the picture tile has no padder: its content
is the whole 150x~110 area, so tiles can opt out of the side strip (inferred)).

`bodyflyout.xaml` (a tile shown as a flyout body) uses the uxtheme class directly:
background `theme://Sidebar,SDB_TILEBACK,NULL`, side strip `theme://Sidebar,SDB_TILEHEADERBACK,SDBTHB_HOT`,
pressed → `theme://Sidebar,SDB_TILECHEVRONBACK,SDBTCB_PRESSED`, side button width 16 with
`TileHeaderIcon`, restore and close docked Right.

`flyout.xaml`: `FillPanel { HackImage FlyoutBackground; DockPanel { DockPanel#FlyoutTop (Top) {
OptionsButton#FlyoutOptions (Right) }; Border FlyoutBorder (Fill) { FillPanel#FlyoutContent } } }`.

Sizes measured on `4074-dwm.png`: sidebar x = 1130..1279 = **150 px** (column 1130 is 1178's dark column 0, not desktop; the IL
default `Sidebar\Width` is 150), full screen height (the taskbar yields to it); options row y = 0..16, separator at y = 17-18; tile separators (1199,
3 px) at y ≈ 198-199, 214/215-224, 309-310, 345 (the 2nd and 3rd tiles' header rows); clock
separator at y = 925-927; clock face 80 px (x ≈ 1170..1250, y ≈ 927..1004), centred on the bar;
time/date text "3:28 AM, Jan 9" at y ≈ 1004..1013. The tiles visible are a news-feed tile
(bulleted headlines), a quick-launch tile (two icons + "Search for..." box), a picture tile
(full-width sunset photo), then empty space down to the pinned clock.

On `4051-vm-now.png` (Slate): sidebar x = 1002..1151 = **150 px** (every Slate reference starts it at 1152 - 150); options separator again at
y = 17-18; a quick-launch tile (IE + mail icons) at y ≈ 19..45; the clock tile below with a
**~95 px** face (x ≈ 1027..1124, y ≈ 75..170) and "6:37 AM, Oct 23" at y ≈ 200; each tile shows the
TileArrow glyph at its top-right (Flipped = right edge). The 4051 sidebar background is opaque
dark grey (the Slate 1000 look: `rgb 60 69 74`, alpha 255).

#### 2.3.4 Clock, notification, expando, property sheet

| File | Facts |
| --- | --- |
| `analogclock.xaml` | `ScaleDecorator DesignSize 260,260`; layers (each `AnalogClockLayerDecorator` with `Origin`): `ClockFace` 1074 (260x260) origin 130,130; `HourHand` Rectangle 3x95 (`0x03`, `0x5f`) origin 2,76, fill #FF000000; `MinuteHand` 3x125 (`0x7d`) origin 2,106; `SecondHand` Polygon `3,1 5,1 5,88 7.5,126 0.5,126 3,88` fill #6DB8CF origin 4,98; `Anchor` 1083 12x12 origin 6,6; `Glass` 1076 (260x260) origin 130,130. Scaled to 80x80 in the sidebar (`clocksidebardefault.xaml`: `Button Style AnalogClock Width 80 Height 80`) |
| `clockresources.xaml` | DateTimeTransformers: `h:mm`, `h:mm:ss`, `tt`, `%d`, `^ddd`, `dddd`, `*MonthDay`, `MMMM d, yyyy`; `ShadowText` = black copy offset 1,1 under the text; `DigitalText` Tahoma 8 white; `DigitalTimeHourMin` #D8E4FF bold, `DigitalTimeAMPM` #9EB5BB bold (margin 2,2,0); `FlyoutDigitalTime` FontSize 18; `MiniCalDay` white bold 8 on #FFADD8E6, `MiniCalDate` #4B5681 Tahoma 11 on white, border #FFD3D3D3 |
| `clocksidebar*.xaml` | views: `default` (80 px analog + LongTime + ClockName), `digtime` (analog beside bold DigitalTime), `digtimemonthday`, `digtimeweekday`, `minical`, `cal`; the 4074 screenshot shows analog + "h:mm tt, MMM d" = the month/day variant. All use `Border Margin 2,0,2` / `2,0,4` |
| `clockflyout.xaml` | `PolarCanvas` 260x260 with the analog clock, `AlarmElement` on radius 101 (`0x65`) about 123.5,123.5, `FlyoutDigitalTime`, alarm row (icon 1129 10x12 + Tahoma 8 #E2E2E2 text), `CalendarElement` on the right with margin 0,0,4,4 |
| `notification.xaml` | `Border#ToastBackground #FF000000`; `HackImage 4002 (205x60) HackNineGrid 7` frame; `TimerControl#Timer` Width 5, Foreground #DD7D2F, image 4001 (5x11), Dock Right (a countdown bar); `ImageButton#NotificationClose` 14x15 images 4003/4004/4005; `ButtonNoStyle "Options"` white; `Image#NotificationIcon` left; `HackBorder 4006 (154x56) grid 7, BorderThickness 6` with `Text#NotificationHeader` Franklin Gothic Medium 15 and `Text#NotificationBody` Franklin Gothic Medium 12 NoWrap, then `DockPanel#NotificationButtons` |
| `expandoresources.xaml` | header icon 16x16 1159 (collapsed) / 1162 (expanded), content `Border 18,1,1,1`, `Cursor Hand` |
| `hottrackbutton.xaml` | `HackBorder HackNineGrid 2`, hover 1110, pressed 1111, inner border 4 |
| `propsheet.xaml` (tile properties dialog) | Grid rows 7 / Auto / 1* / Auto / 7, columns 6 / * / 6, `FlowPanel#ButtonList`, `DockPanel#SheetArea`, OK and Cancel buttons 84x24 (Cancel right, OK with margin 0,0,7,0), `ControlBrush` background |
| `baskettile.xaml` / `basketflyout.xaml` | tile: Canvas Margin 4, MaxHeight 100, AllowDrop, watermark image, `SquarePanel#PreviewContainer` margin 5, count overlay (`Ellipse RadialGradient #CCCCCC #888888`, Franklin Gothic Medium 24 white); flyout: DockPanel Width 72, `Canvas#TaskContainer` 72x100 "No Tasks", `BasketControl#ViewControl` |

#### 2.3.5 The `Sidebar` uxtheme class

Same 60 parts in all three themes (`slate51` as `IMAGEDATA` BLOB 364-423, `slate74` as
`PNGSTREAM` STREAM 394-453, `jade74` STREAM 507-566). Sizes (`jade74` / `slate74`; `slate51` =
`slate74`):

| Part(state) | jade74 | slate74 | Notes |
| --- | --- | --- | --- |
| TITLEBACK | 109x27 (alpha 0 except a 1 px line at row 25) | 109x27 | title strip = a separator line |
| BACK | 77x3 | 77x3 | |
| OVERFLOWBACK | 2x2 | 25x77 | |
| CHEVRON | 16x16 | 16x16 | |
| EXPANDO_{OPEN,CLOSED}_{HORIZ,VERT}(NORMAL/HOT) | 19x19 x8 | 19x19 | = imageres 1004/1008/1009 size |
| STARTBUTTON | 29x22 | 63x23 | slate ≈ imageres 1156 (74x23) |
| TASKBACK(NORMAL/HOT/SELECTED/SELECTEDHOT/FLASH) | 109x24 x5 | 109x24 | **same size as imageres 1150-1154**: the 4051 task button plates lived here |
| TILEBACK | 49x76 (opaque `rgb 40`) | 77x76 | tile body |
| TILEHEADERBACK(NORMAL/HOT/PRESSED) | 4x4, 4x4, 4x4 | 4x4, 16x19, 4x4 | |
| TILECHEVRONBACK(HOT/PRESSED), TILECHEVRONGLYPHUP/DOWN | 16x26 | 16x26 | |
| TILESIZER(NORMAL/HOT) | 150x7 | 150x2 | = imageres 1028/1030 (150x2) |
| TILESIZERBUBBLE | 16x16 | 16x16 | |
| FLYOUTTOP / FLYOUTCONTENT / FLYOUTBOTTOM | 208x27 / 2x193 / 207x26 | same | three-piece flyout frame (pre-9-grid) |
| FLYOUTCLOSEBACK(HOT/PRESSED) 18x27, FLYOUTCLOSEGLYPH 9x9, FLYOUTOPTIONSBACK 89x27, FLYOUTOPTIONSGLYPH 9x6 | | | glyph 9x6 = imageres 1176/1177 |
| CLOCKBACKGROUND / CLOCKGLASS | 142x116 | 142x116 | the 4051 clock tile plate (face ≈ 95 px in the screenshot) |
| IDLISTBUTTON(HOT) | 5x5 | 20x23 | = imageres 1110 size |
| TRAYAREABACK | 14x15 | 1x77 | |
| BARUP / BARDOWN (NORMAL/HOT/PRESSED) | 17x17 x6 | 17x17 | = the 17x17 round buttons 1159-1172 |
| BACK_HORIZ | 3x77 | 3x77 | |
| SBRESTOREBUTTON / SBCLOSEBUTTON (3 states) | 15x15 | 15x15 | |
| BUTTON(NORMAL/HOT/PRESSED) | 107x41 | 107x41 | |

Reading: in 4051 the whole bar look was theme data in the `Sidebar` class; by 4074 it moved to
`imageres.dll` and only `SDB_TASKBACK` (overflow drop target), `SDB_TILEBACK`,
`SDB_TILEHEADERBACK`, `SDB_TILECHEVRONBACK` are still fetched through `theme://` (and
`CompositedWindow`/`TextGlow` for frames). The managed side reaches them with `OpenThemeData`
+ `GetThemeStream` (COMPOSITION.md §6). Extracted copies for the mock: `mock/assets/theme/jade_<PART>[_<STATE>].png`.

### 2.4 Behaviour constants from the IL (`res/4074/il/desktop.il`) and the 4051 symbols

Line numbers are `desktop.il` lines ("L…"). All Avalon sizes are measure units = device px at 96 dpi.

#### 2.4.1 Who owns what (4074)

| Piece | Role (IL) |
| --- | --- |
| `TaskbarHwndSource` (L52774) | owns the primary taskbar HWND and **all** appbar geometry for both bars: `_rgTaskbars[]` (one per monitor; index 0 = `Taskbar`, others `ExtendedTaskbar`), `_prcSidebar`, stuck places, autohide percentages and slide timers; created from native `CShellTaskbar::Create`; spawns `_SidebarThread` → `CoCreateInstance(CLSID_ShellSidebar)` → `IShellSidebar::Create(hwndPrimaryTaskbar, sidebarStuckPlace)` (L53251-53281) |
| `SidebarHwndSource` (L48230) | owns the sidebar HWND hosting `SidebarControl`; announces itself with `WM_USER+411` (1435) to the tray and the primary taskbar |
| `Appbar2.Init` (L15462) | window: `WS_POPUP`, ex-style `WS_EX_TOOLWINDOW|WS_EX_WINDOWEDGE` + `WS_EX_TOPMOST` when `_fAlwaysOnTop` (default 1, reg `Sidebar\AlwaysOnTop`) + `WS_EX_LAYERED` when `SHShouldUseMIL`; background `ARGB(0,0,0,0)` (transparent) for taskbar, sidebar, extended taskbars and toasts |
| `Appbar.StuckPlace` (L13238-13401) | ABE 0 left, 1 top, 2 right, 3 bottom; `Flipped = (place == right || bottom)`, `Rotated = (place == top || bottom)` → **Rotated = horizontal bar, Flipped = far edge** (so the right-docked sidebar is Flipped and not Rotated; the bottom taskbar is both) |
| Defaults (`TaskbarHwndSource.Init` L52831) | taskbar StuckPlace **3 (bottom)**, AutoHide 0, Monitor -1 (all monitors); sidebar StuckPlace **2 (right)**, Minimized 0, AutoHide 0, Monitor 0 |
| Cross-bar messages | private `WM_USER+n` between the two HWNDs and `Shell_TrayWnd`: 1381 `_ToggleSidebar`, 1388 **SetMaximizedWindow**, 1395/1457 sizer drags, 1430 minimized-sidebar size, 1432/1439 show/remove minimised-window tile, 1433 `SetMinimized`, 1448/1454 stuck places, 1450-1453 autohide get/set, 1438 `AddTileInProc`, 1458 `IsTileInSidebar`; `CreateEventW("SideBarReady")` + broadcast `RegisterWindowMessageW("SideBarCreated")` once initialised (L38727-38750) |

#### 2.4.2 Sizes and geometry

| Constant | Value | Where |
| --- | --- | --- |
| Sidebar width (left/right) | reg `Sidebar\Width`, **default 150**, clamp **100..600** (`_ValidateSize(v, 100, 150, 600)`) | L53528 |
| Sidebar height (top/bottom) | reg `Sidebar\Height`, default 150, 100..600 | L53488 |
| Vertical taskbar width | reg `Taskbar\Width`, **default 100**, 100..600 | L53448 |
| Horizontal taskbar height | computed: `GetTaskbarRowSize() * _cRows`; `GetTaskbarRowSize = TaskList.GetHeight = 30` (constant); `_cRows` = reg `Taskbar\RowCount` 1..3, default 1 → **30 px**, matching the screenshot | L51815, 19870, 51705-51740 |
| Vertical taskbar height | TaskList non-autosize height (default **200**, reg `TasklistHeight`, min 33) + StartButton.Height + toggle height | L51828, 19174-19260 |
| Task button height | **29** (one row = 29 + 1); rows horizontal `clamp(int(h/29), 1, 4)` | L22347-22990 |
| Task button width | columns `max(1, int(w/130))`, items per row from count; width `min(140, w / itemsPerRow)` → **max 140** (= the measured 140 px pitch) | L22400-22493 |
| Task overflow | width < **60** → `_ChildrenWillNotFit((w/60)*rows)` (rest to the chevron); width > **70** → `_ExtraSpace` pulls back | L22846-22890 |
| Group flyout | `TaskGroupFlyout` DockPanel **MaxWidth 200**, Popup CloseMode 2 placed at the button; sets `ExtendedTaskbar.ShowingUI` (keeps an autohide bar open) | L20815-20926 |
| Grouping | `TaskbarGlomming` (default **1**) and `TaskbarGroupSize` (default **0**) from `Explorer\Advanced` (key name inferred from a mangled literal); GroupSize 0 = group only when buttons do not fit (`_OnChildrenWontFit` gloms the largest group); N = group when count == N, ungroup below; `_OnExtraSpace` ungroups the biggest group when horizontal `cxFree/150 >= count` or vertical `count*29-29 <= cyFree` | L19174-19230, 20055-20139, 20602-20675 |
| Drag-over activation | hovering a drag over a TaskGroup 250 ms, TaskItem 500 ms, StartButton 1000 ms (opens the start menu) | L21402, 22067, 52524 |
| Autohide geometry | `_GetRect`: the window keeps its full size and is shifted so `2 + (size-2)*pct/100` px stay on screen → **2 px visible when hidden** (same as XP's `g_cxFrame/2`) | L53719 |
| Autohide animation | `_iSidebarVisiblePercent` / `_rgiTaskbarVisiblePercent[i]`; focus-within change starts `SetTimer(20 ms)` (id 3 sidebar, 100+i taskbar); each tick pct ± 20 → **5 steps x 20 ms = 100 ms**, linear; when autohide is switched on the bar jumps to 100 or 0 immediately | L54873-54970, 55644-55830 |
| Focus-within (keeps an autohide bar open) | sidebar: `IsMouseOver || ShowingFlyout || any tile FocusWithin` (L38232); taskbar: `IsMouseOver || ShowingUI` (L23527) | |
| Multi-monitor | one `ExtendedTaskbar` per extra monitor when `Taskbar\Monitor = -1`; bars clipped to their monitor with `SetWindowRgn`; `SetWindowPos(... SWP_ASYNCWINDOWPOS|NOACTIVATE|NOZORDER)` | L54295, 55205 |
| Mixed taskbar/sidebar | `_fBarsOnSameSide = same place && same monitor`; `_fSidebarInTaskbar = !minimized && sameSide` → taskbar takes the **top** `GetTaskbarHeight()` of the combined strip, sidebar the rest; if the sidebar is minimized and the taskbar horizontal, the sidebar gets the right-most `_cxSidebar` px of the bar (the overflow strip); when the bars overlap on different edges the taskbar rect is shrunk away from the sidebar (this is the x = 1130 cut in the 4074 screenshot) | L54114-54290, 56421 |
| `_ToggleSidebar` (toggle button, 1381) | flips `_fSidebarMinimized`, sets `Taskbar.BarVisible` (toggle glyph 1170 vs 1167), posts 1433 and re-docks a minimized sidebar to the taskbar's edge | L56520 |
| `SidebarControl.SetMinimized` | minimized: tile collection 0x0 hidden, pinned host hidden, title row collapsed, options separator 0, overflow collapsed, the **taskbar tile collection** (`TBContentHostCollection`, reg `Sidebar\TaskbarTiles`) inserted first in `sidebarMain`, `ForceOverflow`, `BarMinimized = true`; persists `Sidebar\Minimized` | L37343 |
| Sizer drag | `_OnSizerDrag` (vertical bars only, i.e. width): clamp ≥ 0, `SendMessage 1395` (taskbar) / `1457` (sidebar) | L23559, 52177, 38525 |
| Appbar registration | `_SendAppbarUpdate(hwnd, stuckPlace, alwaysOnTop, autoHide, prcPrev, prc)` native per bar; `SHAppBarMessage(ABM_REMOVE)` when the sidebar lives inside the taskbar strip | L54714 |

#### 2.4.3 SetMaximizedWindow (the black layer)

| Item | Value | Where |
| --- | --- | --- |
| Trigger | native (`TaskInterop`) → `TaskData.SetMaximizedWindow` → `TaskList` → `Taskbar/ExtendedTaskbar.SetMaximizedWindow(bool)`; native detection is `CTray::HandleFullScreenApp` / `CTaskBand::_FindRudeApp` (4051 syms) | L51418, 19692, 23473, 52133 |
| Taskbar | sets attached `Appbar.MaximizedWindow`, `SendMessage(parent, 1388, 0, b)`, `AnimatePropertyTo(TaskbarBlackLayer, Opacity, b ? 1 : 0, **400 ms**)` | L23473-23506, 52133-52166 |
| Sidebar | 1388 forwarded to the sidebar HWND → `SidebarControl.SetMaximizedWindow` → `SidebarBlackLayer` opacity 0/1 over 400 ms | L55858-55891, 38142 |
| Task text | `TaskItem._OnMaximizedWindowInvalidated`: text brush animated to white (255,255,255) over 400 ms while a window is maximised, cleared back over 400 ms | L22294-22340 |
| Animator | `Animator2`: 25 ms tick, frames = duration/25; easing 0 linear, 1 ease-out `2t - t^2`, 2 ease-in `2t^2`, 3 ease-in-out; animations are skipped (value set at once) under `SM_REMOTESESSION` / `SM_SHUTTINGDOWN` | L12646, 9436 |

#### 2.4.4 Tiles (runtime side)

| Constant | Value | Where |
| --- | --- | --- |
| Module registry (4074) | HKLM `Explorer\Sidebar\Modules\{ModuleGUID}`: `AssemblyName`, `Type`, `Friendly Name`, `Transient`, `MultiInstance`, `ShowInMenu` (default 1), `IconOnly`, `MaxHeight` (default 0), `RunInDomain` (4051 SDK still says `Explorer\StartBar\Modules`, see 2.5) | L39774-40257 |
| Per-tile state | HKCU `Explorer\Sidebar\Tiles\{TileGUID}`: `TileInfo`, `TileData`, `TileSavedData`; order lists `Sidebar\SidebarTiles`, `SidebarPinnedTiles`, `TaskbarTiles`; `Sidebar\TileFlags\{ModuleGUID}` bitmask 1 blocked, 2 may grow large, 4 denied large | L10520-10564, 40083-40226, 46115-46611, 12161 |
| Tile defaults | `_cyTile = 30`, `_cxTile = 16` (taskbar host), `_fAutoSize = true` | L39774+ |
| Min / max height | min **30** (sizer drag clamps `max(30, h + dy)`); max = `GetMaxValueForTile` = `sidebarMain` height minus all other docked children, or `TileInfo.MaxHeight` | L41667-41772, 37577-37701 |
| Taskbar-tile width | clamp **16..150** | L41814-41860 |
| "Very large tile" | if a tile wants more than allowed and flags permit nothing yet: height forced to **400**, notification "Tile is very large… allow?" Yes → TileFlags|=2, No → |=4 | L42287-42518 |
| Grow/shrink policy | grow immediately; shrink only if ≥ **30 s** since the last grow (else a one-shot timer) | L41814-42014 |
| Resize animation | Width/Height to the new size over **400 ms**, easing 3; first size applied directly | L41500 |
| Hide / show | hide: plugin cmd 2, then size → 0 over 400 ms (easing 3, **100 ms delay**); show starts at `cyTile` with `ClipToBounds` | L41324, 41233 |
| Hover | `OuterContentHostBack` / `TileSideAreaBack` / auto-hidden foreground opacity 1 over **120 ms** on enter, 0 over **80 ms** on leave (same for the "Options" text) | L41141, 44214, 14697 |
| Fade in / remove | `TileUI` opacity → 1 over **800 ms** (the `Opacity="0"` in `tile.xaml`); remove: close flyout, opacity → 0 over 400 ms | L45165, 45177 |
| Flash | `SetFlash`: opacity alternates 0.5/1.0, period starts at 1000 ms and decays by 100 ms per toggle to 100 ms; `ShowFlashImage` | L42258-42425 |
| Overflow | expando button 24x24, 16x16 icon (tile icon or imageres **1056** default); a tile goes to the overflow when the remaining height < **40**; auto-close 1000 ms after the mouse leaves; preview of an overflow tile grows 0 → 150 over 400 ms and stays 4 s (insert) / 10 s (append) | L46899, 46234-46386, 47502, 47539-47705 |
| Insert preview (drag) | `PreviewPercent` 0..1 / `PreviewIndex`; reset timer 10 s; snap over 800 ms | L38122, 38828, 46088 |
| Sizer double-click | re-enables AutoSize | L41773 |
| Default tiles | first run (`Sidebar\Initialized` absent): pinned tiles, then modules `{e9c46203-…}`, `{A5C40017-…}`, `{e60261ae-…}`, `{9b49b965-…}`; clock module `{b6852cff-6f7c-4a39-b56b-6c9d40679adb}` pinned when `Sidebar\Clock` (default 1) and policy `SHRestricted(0x40000070)` allow; "Clock" check item in the sidebar menu toggles it | L38864-38996, 37125-37190 |
| Minimised-window tiles (MWT) | `TaskData._CheckMWTs` posts 1432/1439 per tracked hwnd (`ShouldShowInTaskbar`, `IsIconic`); the tile header Restore/Close buttons send 1403/1402 to the taskbar (`RestoreTask`/`CloseTask`) → the `OuterTile.MWT` trigger in 2.3.3 | L51544-51623, 45002-45016 |
| Flyout | `InnerContentHost.SendShowFlyout`: content = `Tile.InternalCreateFlyout()` or, when none, the tile's own Background/Foreground moved into the flyout and back on close; `Popup` CloseMode 2, `PlacementRect` = tile rect, placement 2 (Flipped) or 4; placement 2 → `FlyoutContent` width **150**; sets `SidebarControl.ShowingFlyout` | L44035-44161, 44743 |
| Tile context menu | "Auto Size", "Move Up/Left", "Move Down/Right", "Remove <name> Tile from overflow", "<name> Tile Properties", "Remove <name> Tile"; sidebar menu: "Minimize bar"/"Restore bar", "Properties", parts list ("(Multi-Instance)", "(Transient)" suffixes), "Clock" | L44524-44742, 17092-17103 |
| `OuterTile.GetSidebarWidth/Height` | returns **150** when the sidebar is minimized, else the real size | L40511-40546 |
| Theme parts used from code | `OpenThemeData(hwnd, "Sidebar")`; `theme://Class,Part,State` → `GetThemeImage`, `res://module,id` → `LoadImageData` (`HackBorder._ReCreateBrush` L14351); the class map includes fonts/colours `SDB_TILEHEADERTEXTFONT`, `SDB_TILETEXTFONT`, `SDB_TASKBACKFONT`, `SDB_TILEHEADERTEXTCOLOR`, `SDB_TILETEXTCOLOR`, `SDB_TASKBACKCOLOR`, `SDB_TASKBACKGROUPCOLOR` (values not present in the decompiled inis); `IsAeroRunning` = current `.msstyles` name contains "aero" | L58-140, 17118-17312 |

#### 2.4.5 Notification toasts (`NotificationToast`, `NotifyWindow`, L30725-31692)

| Constant | Value |
| --- | --- |
| Body text wrap | measure at width **110**; if more than one line (`fontPx * 1.2`) try **154** (2 lines), then **220** (4 lines) |
| Buttons | 73x24 text buttons or 16x16 hot-track image buttons, border 2, at most **2**; header icon ≤ 80x80 |
| Window | `WS_POPUP|WS_CLIPSIBLINGS`, `WS_EX_NOACTIVATE|TOPMOST|TOOLWINDOW`, `SW_SHOWNOACTIVATE`, own thread; round-rect region radius **9** |
| Position | `x = work.right - width - 10`, `y = work.bottom - (expandedH - collapsedH) - height - 10` on the primary monitor |
| Timeout | **10 s** (min 5 s); the `TimerControl` progress ticks every `timeout*1000/heightPx` ms; hover stops the timer and rewinds progress to 0.75 over 200 ms, expands (`ExpandedPercent` → 1, buttons/options opacity → 1) over 200 ms; leaving collapses over 200 ms and restarts |
| Glass | `IsAeroRunning` → `ToastBackground` opacity 0 |
| Transport | sender serialises ≤ 64 KB with `WM_COPYDATA` (dwData 3) to `Shell_TrayWnd` (`SendMessageTimeout` 4 s); callbacks 1043 shown / 1044 closed / 1045 clicked (wParam = button id, 0 body); one toast at a time, FIFO queue |

#### 2.4.6 Native method lists (4051 PDBs, `res/sym4051/*_syms.txt`)

`CNewTaskbar`, `CStartBar` and `CTrayClockWnd` do **not** exist; the clock is `CClockCtl`.
`CShellSidebar`/`CShellTaskbar` are the native half of `System.Windows.Explorer.dll`.

| Class | Methods (noise stripped) |
| --- | --- |
| `CShellSidebar` | Create, Destroy, AddTile, RemoveTile, SetStuckPlace, SetMinimized, SetLockBar, SetTaskbar, HandleIniChange, RefreshTheme, GetSidebarMinimizedWidth/Height, NotifyComplete, SetPermCookie, UpdateViewByteArrays/Strings/Variants |
| `CShellTaskbar` | Create, Destroy, CloseHWND, RestoreHWND, DisplayClock, GetTaskbarHeight, GetTaskbarRowSize, HandleIniChange, RefreshTheme, SetLockBar, SetMixedTaskbarSidebar, SetSidebarHWND, SetSidebarMinimized, SetStuckPlace, SetTaskbarRowCount, TrackRMA, StopTrackRMA |
| `CAvalonAppBar` (explorer) | CreateThread, MainThreadProc, _MessageLoop, SHFusionCreateWindow, _AdjustRectForSizingBar, _GetRect/_SetRect, _HandleAppBarNotify, _HandleCreate/Destroy/NCPaint/Paint/ThemeChanged/WindowPosChanged, _LoadHeight/_LoadWidth/_SaveHeight/_SaveWidth, _PtOnResizableEdge, _ResetWindowPos, _SetAutoHide, _SetFocusWithin, _SetLockBar, _SetNeverShowSizingBar, _SetSide, _SizeStartBar, _ValidateNewSize |
| `CAvalonTaskBar` | _GetBarHeights/_GetBarWidths, _HandleCreate/Destroy/IniChange/ThemeChanged/WindowPosChanged, _Load/_SaveHeight/Width, _SetFocusWithin, _SetLockBar, _ToggleClock, _ToggleSidebar, _TrackRMA, _UpdatePositions |
| `CSidebarWnd` | Init, _GetBarHeights/Widths, _HandleCreate/Destroy/IniChange/ThemeChanged/WindowPosChanged, _OnAddTile, _OnRemoveTile, _SetFocusWithin, _SetLockBar, _SetSide |
| `CTray` (appbar/autohide/start) | _AppBar* (GetAutoHideBar, GetStartBarPos, GetTaskBarPos, QueryPos, SetAutoHideBar2, SetPos, SetState, SubtractRect(s)), _AutoHideCollision, _ComputeHiddenRect, _SetAutoHideState/_SetAutoHideTimer/_SetUnhideTimer, _KickStartAutohide, _SlideStep/_SlideWindow, _Hide/_UnhideNow/Unhide/InvisibleUnhide, _GetDockedRect, _GetStartBarPlace, _MakeStuckRect, _RecalcStuckPos, _StuckAppChange/_StuckTrayChange, _HandleSizing/_HandleMoving, _RecomputeWorkArea, SizeWindows, _CreateStartButton, GetStartButtonMinHeight, StartButtonClicked, HandleFullScreenApp, _OnAddTile, TileEnumProc, _CreateClockWindow, _ToggleQL, _RaiseDesktop, _MinimizeAll, _SaveTray |
| `CTaskBand` (grouping/width) | _AutoGlomGroup, _Glom, _GetGroupAge/Index/IndexFromExeName/Items/Size/Width, _HideGroup, _MoveGroup, _GetIdealWidth, _GetItemWidth/_SetItemWidth, _GetTextSpace, _GetToolbarMetrics, _GetCurButtonHeight, _GetNumberOfRowsCols, _SizeItems, _CheckSize, _CheckNeedScrollbars, _AnimateItems and the animation helpers, _UpdateFlashingFlag, _FindRudeApp/_EnumForRudeWindow, _SwitchToItem, _CreatePopupMenu/_HandleDropDown, _DrawNumber, _HandleCustomDraw, _OpenTheme, _HandleShellHook, _OnWindowActivated |
| `CTrayNotify` | _CalcMinSize, _SizeWindows, _UpdateChevronSize/State, _ToggleDemotedMenu, _ShowBalloonTip/_HideBalloonTip, _ShowChevronInfoTip, _OnIconDemoteTimer, _OnRudeApp, _PlaceItem, _AnimateButtons, _HandleCustomDraw, _OpenTheme, theme strings `c_wzTrayNotifyTheme`, `…HorizTheme`, `…HorizOpenTheme`, `…VertTheme`, `…VertOpenTheme` |
| `CClockCtl` | _CalcMinSize, _DoPaint, _DrawText, _EnableTimer, _GetMaxTimeSize/_GetMaxDaySize/_GetMaxDateSize, _OnNeedText, _OpenClock, _RecalcCurTime, _Reset, _UpdateLastHour |
| `CStartButton` | CreateStartButton, DisplayStartMenu, BuildStartMenu, TrackMenu, IsButtonPushed, ForceButtonUp, GetRect, RecalcSize, InitBackgroundBitmap, _CreateStartFont, SetStartPaneActive, ShowStartButtonToolTip, StartButtonReset, CreateStartButtonBalloon |

### 2.5 Public tile model (SDK 4051, `wim/sdk4051/lhsdk/LHSDK/shell/sidebar/`, class reference inside `res/sdk4051/LHSDK.hxs`)

| Item | Fact (source) |
| --- | --- |
| Assembly | `System.Windows.Explorer` 6.0.3708.0, namespace `System.Windows.Desktop`; Avalon types `MSAvalon.Windows.*` (class pages) |
| `BaseTile` | `public abstract class BaseTile : MarshalByRefObject, IComponent, IDisposable`; hierarchy `BaseTile → {BaseComTile → SlideShowTile, NotificationArea, QuickLaunchTile, SideBarClock, SyncTile, TestTile}` |
| Lifecycle | `public virtual void Initialize()` — runs when the tile is created, before it appears; properties may not be set in the constructor (`Sidebar` is null there); `protected virtual void Dispose(bool)`; `raise_Disposed` fires `Disposed` when the tile is removed or the sidebar closes |
| `Foreground : FrameworkElement` | "intended to contain any interactive elements… Clicking a part of the foreground does not display the flyout" |
| `Background : FrameworkElement` | "Clicking any part of the background displays the flyout view. Therefore, reserve the background for non-interactive elements." One of the two is optional |
| `ForegroundAutoHide : bool` | foreground hidden when the tile does not have focus (flashcard sample) |
| `ForegroundCenter : bool` | centre the foreground between the header and an equal strip of background |
| `MiniMode : FrameworkElement`, `Icon : ImageData` (header icon, default icon otherwise), `TooltipText`, `HasProperties` (enables "<Friendly Name> Tile Properties"), `Flyout` (the open flyout or null), `FlyoutLinkText` (optional link at the flyout bottom, drawn by the sidebar, click → `FlyoutLinkClickEvent`), `ConnectedState`, `InitializationString` (from `AddTileToSidebar`), `Sidebar : ISidebar` | member pages |
| `CreateFlyout()` | `public virtual FrameworkElement CreateFlyout()` — "displayed when an element in the tile's Background is clicked. It is not bounded by the size of the sidebar"; samples: 370x700 (news), 260 wide (contacts) |
| `CreateProperties()` | `public virtual FrameworkElement CreateProperties()` — shown from the shortcut menu; samples 125x75, 400x55, 250x220; close with `Sidebar.CloseProperties()` |
| `ISidebar` | `ShowFlyout(elem[, target]) : bool`, `CloseFlyout()`, `ShowMyProperties(elem)`, `CloseProperties()`, `RemoveMe()`, events `ContextMenuEvent` (add `MenuItem`s to `args.ContextMenu.Items`), `FlyoutClosingEvent`, `FlyoutLinkClickEvent`, `RMAActionEvent` (`RMAAction { Maximize, Restore }`, paired with `SHSTDAPI EnableRichMinimizedApp(HWND, GUID, LPCWSTR)`) |
| Context menu | owned by the sidebar; default items "Remove" and "<Friendly Name> Tile Properties"; blank-area right click = "Add a Tile" listing Friendly Names |
| Registration | HKLM `SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\StartBar\Modules\{CLSID}`: `AssemblyName` (path without `.dll`), `Type` (full type name), `Friendly Name`; `.reg` sample `frenchflashcardtile/compiled/FlashCardRegistryEntries.reg`; loaded by path (`Assembly.LoadFrom`), no GAC/strong name; `SHSTDAPI AddTileToSidebar(GUID, LPCWSTR pszData, BOOL fTransient)` (`ShellAPI.h:987`) adds a registered module at runtime |
| Deployment manifest `sideBarTileType` | `name, description, GUID, entryPoint, smallIconFile, largeIconFile, maxWidth, maxHeight, multiInstance, transient` — the only place a max size concept appears (no values) |
| No pixel sizes in the docs | sample bodies: DockPanel Height 100 (flashcards), headlines Height 72 (news), TextBox Width 120 (contacts); "give textbox a fixed width since it will not size correctly inside the tile body" |
| Samples | `helloworldsidebartile` (body + flyout + properties + checkable menu item), `helloworldvb`, `newsfeedtile` (RSS, 35-min timer, worker thread + `BeginInvoke`, "Refresh" item), `frenchflashcardtile` (click/3 s rotation, `ForegroundAutoHide`), `contactlookuptile` (WinFS/AD search, progress flyout then 260 px result flyout), `notificationsample` (WinForms app sending a toast: `HeaderText`, `BodyText`, `IsBodyClickable`, up to two `NotificationButton`s, `HeaderIcon`, `WouldNotificationShow()`, `Send()`, `NotificationClickedEvent` with ID 0 = body) |
| Built-in clock (`Microsoft.Windows.WCPClient`) | `SideBarClock : BaseTile` with `alarm`, `CreateFlyout`, `CreateProperties`, `DigitalTextElement`; `AnalogClockPanel`, `CalendarElement`, `SidebarAlarmClock` (states Idle/WaitingForCoarseTrigger/WaitingForTrigger/Trigger, Snooze, RepeatDaily, MusicFile); `BaseSidebarClockSettings` carries `ClockSize, ClockCenter, HandOverhang, LayerIDs/LayerOrigins/LayerTypes` (the layered face), `DigitalFont`, `FlyoutWidth/Height`, calendar metrics — values live in the DLL/theme, not in the docs |
| Sidebar internals named in the reference | `Sidebar : SidebarXAML : Appbar : FillPanel`; `TileCollection : OverflowableCollection` with `PreviewIndex/PreviewPercent`, `StartDrag/Drop/Remove`, `ChildrenWontFit`/`ExtraSpace`; `TileOverflow`; `OuterTile` DPs `TargetSize`, `ShowFlashImage`, `RMA`, `RMAID`; `Tile.AutoSize`; `FlyoutXAML` fields `FlyoutTop/Content/Bottom`, `FlyoutLink : AreaButton`, `FlyoutOptions`; `Microsoft_Windows_Client.xml` documents only the older "Do not use" model (`ClockTile`, `MediaTile`, `SidebarElement`, `FlyoutElement`, `SidebarCPL`) |

Model consequence for the Lua tile API (7.5): a tile = `foreground` (interactive, no flyout
on click) + `background` (click opens the flyout), optional `flyout()`, `properties()`, context
menu items, `icon`, `tooltip`, `has_properties`, `flyout_link`; the host owns the menu, the
flyout window and the properties window, and calls `initialize()` after `sidebar` is set.

---

## 3. Behaviours

### 3.1 Window list (both shells)

| Behaviour | Longhorn/XP reference | luxshell |
| --- | --- | --- |
| Enumerate top-level windows in creation order | `CTaskBand` inserts on `HSHELL_WINDOWCREATED`, keeps order, group inserts next to siblings | root `_NET_CLIENT_LIST` (creation order; `_NET_CLIENT_LIST_STACKING` for z-order), `PropertyNotify` on the root |
| Skip taskbar-less windows | `WS_EX_TOOLWINDOW`, owned windows | skip `_NET_WM_WINDOW_TYPE` dock/toolbar/menu/splash/utility/dialog-with-transient, `_NET_WM_STATE_SKIP_TASKBAR`, windows with `WM_TRANSIENT_FOR` |
| Active (checked) button | `HSHELL_WINDOWACTIVATED`, `TB_CHECKBUTTON` | root `_NET_ACTIVE_WINDOW` |
| Minimised state (restore on click) | `IsIconic` | `_NET_WM_STATE_HIDDEN` in the client's `_NET_WM_STATE` (published by luxwm, with `WM_STATE` Iconic; minimised is separate from berry's workspace "hide") |
| Click: activate or minimise if active | `_HandleClick`: minimise when already foreground | send `_NET_ACTIVE_WINDOW` client message (source 2 = pager) or `_NET_WM_STATE` add `_NET_WM_STATE_HIDDEN` / `XIconifyWindow` |
| Title / icon | `WM_GETTEXT`, `WM_GETICON` small 16 px | `_NET_WM_NAME` (UTF8), else `WM_NAME`; icon rule in 6.2 |
| Flashing (`TaskBand.FLASHBUTTON`, Avalon `TaskButton.Flash` 1183) | `HSHELL_FLASH` | `_NET_WM_STATE_DEMANDS_ATTENTION` (read; drawing not done) |
| Grouping | `TaskbarGlomming`, `TaskbarGroupSize`, group when forced to `iMin` | not done; would be: group key = `WM_CLASS` class (XP uses the exe path), the same rules (group of ≥ 2 only, GLOM_OLDEST default, ungroup when ≥ 2 open slots), the group button with count + Marlett "u" (Avalon) and a popup list |
| Button width (classic) | `iIdeal = (W - fixed) / n` clamped to [`1.8*(cySize+2*edge)+textspace`, `cxMinimized`]; remainder to the first buttons | flxp-taskbar (section 12) |
| Button width / rows (Avalon) | height 29 per row, rows `clamp(h/29, 1, 4)`; columns `max(1, w/130)`; width `min(140, w/itemsPerRow)`; overflow to the chevron below 60 px, pull back above 70 px (2.4.2) | 6.1, 6.2; the measured 140 px pitch confirms |
| Group popup (Avalon) | `TaskGroupFlyout` MaxWidth 200, items = icon + title; drag-over opens a group after 250 ms, an item after 500 ms, the start button after 1000 ms | not done |
| Window context menu (Avalon) | End Task, Restore, Move, Size, Minimize, Maximize, Close (SC_* via WM_SYSCOMMAND, disabled by window styles); group: End Group Tasks, Cascade, Tile Vertically/Horizontally, Minimize/Restore/Close Group; taskbar: Task Manager, Show Desktop, Cascade, Tile, properties (2.4.2) | the taskbar menu (6.5): `_NET_SHOWING_DESKTOP`, cascade / tile through `_NET_MOVERESIZE_WINDOW` |
| Tooltips | toolbar tooltips with the full title | not done for task buttons (tiles have them, `tooltip.lua`) |
| Right click | window system menu (`TBC_SYSMENUCOUNT`) | luxwm's window menu at the pointer (`_GTK_SHOW_WINDOW_MENU`, 6.2) |

### 3.2 Appbar placement, struts, autohide

| Behaviour | Reference | luxshell |
| --- | --- | --- |
| Reserve the screen edge | `SHAppBarMessage(ABM_SETPOS)`; stuck rect = edge strip of `_sStuckWidths` | `_NET_WM_WINDOW_TYPE_DOCK` + `_NET_WM_STRUT_PARTIAL` (bottom = the bar's height; right = 150 over the full height); luxwm keeps `_NET_WORKAREA` and maximises and places inside it |
| Taskbar yields to the sidebar | `SetMixedTaskbarSidebar`, screenshot: taskbar ends at x=1130 | taskbar strut `bottom_start_x..bottom_end_x = 0..W-150` and window width `W-150` while the sidebar is on top (7.2) |
| Autohide (classic) | 500 ms poll, hide when pointer outside rect + 4*edge and not active; show on pointer movement > dblclk distance after 50 ms; slide 400 ms out / 200 ms in, 20 ms frames; 2 px remain visible | flxp-taskbar (section 12) |
| Autohide (Avalon) | driven by "focus within" (mouse over, flyout or popup open, tile focus); visible percent ± 20 every 20 ms = 100 ms linear slide; `2 + (size-2)*pct/100` px visible; a bar jumps to 0/100 when autohide is toggled (2.4.2) | 6.4 |
| Always on top | `ABS_ALWAYSONTOP` | `_NET_WM_STATE_ABOVE` (luxwm keeps docks above clients unless BELOW) |
| Persistence | `StuckRects2` TVSD, `Explorer\Sidebar\...` (see 2.4) | settings.ini `[taskbar]`, `[sidebar]`, `[clock]`; luxshell-state.ini: the tile order (section 14) |

### 3.3 The maximised-window black layer

`TaskbarBlackLayer` / `SidebarBlackLayer` are black `Border`s at opacity 0 directly under the glass
strips. `SetMaximizedWindow` animates them 0 → 1 over **400 ms** when a window becomes maximised
on that monitor and back when none is; the taskbar forwards the state to the sidebar (message
1388) and the task button text is animated to white over the same 400 ms (2.4.3). The native
detector is XP's "rude app" logic (`CTray::HandleFullScreenApp`, `CTaskBand::_FindRudeApp`).
luxshell's side is section 11: luxwm publishes the condition, and with alpha 1 the bar is opaque black, so picom's blur
under it does not matter.

### 3.4 Sidebar tiles

| Behaviour | Reference | luxshell |
| --- | --- | --- |
| Add / remove | `CSidebarWnd::_OnAddTile/_OnRemoveTile`, `CShellSidebar::AddTile/RemoveTile`; modules registered under `Explorer\StartBar\Modules` (4051) / `Explorer\Sidebar\Modules` (4074) with `Friendly Name`, `ShowInMenu`, `MultiInstance`, `Transient`, `IconOnly`, `MaxHeight`; instances persisted under `Sidebar\Tiles\{TileGUID}` and the order lists `SidebarTiles`/`SidebarPinnedTiles`/`TaskbarTiles`; new tile added by a third party → consent toast "New Sidebar Tile Added" Show/Hide (2.4.4) | `luxshell/tiles/*.lua`; the sidebar menu's parts list adds and removes them, the tile menu has Move Up/Down, Remove, Properties (7.5, 7.6) |
| Lifecycle animations | fade in `TileUI` 800 ms; remove 400 ms; resize to a new height 400 ms ease-in-out; hide: 100 ms delay then 400 ms to 0; hover chrome 120 ms in / 80 ms out; flash 0.5↔1.0 with period 1000 → 100 ms (2.4.4) | fade-in 800 ms and resize 400 ms ease-in-out done; hide, hover chrome and flash not |
| Sizes | tile height min 30, default 30, max = remaining `sidebarMain` height or module `MaxHeight`; "very large" dialog forces 400 and asks; shrink deferred 30 s after a grow; width = bar width (150 default, 100..600) | a tile's `height` is its own request, the host clamps to min 30 (7.5); the very-large consent not done |
| Order and drag | `DragButton#TileContent`, `OuterTileBack`/`OuterContentHostBack` insertion markers (`PreviewPercent`, 800 ms snap, 10 s reset), `SBContentHostOverflow.DropTarget` highlights the overflow strip with `SDB_TASKBACK SELECTED` | not done (Move Up / Move Down in the tile menu) |
| Resize | `OuterContentHostSizer` 4 px thumb at each tile's bottom (double-click = Auto Size again); `SidebarSizer` 2 px (Aero) / 6 px (Slate) thumb on the inner edge changes the bar width (message 1457, clamp 100..600) | the bar width: 7.1; the tile sizer drag not done |
| Overflow | a tile that does not fit (remaining height < 40) collapses to a 24x24 button (16 px icon, default imageres 1056) in `OverflowItems`; expando 1008/1009/1004; list auto-closes 1 s after the mouse leaves; hovering an overflow item previews it at 150 px for 4-10 s | not done |
| Minimise the bar | `SidebarControl.BarMinimized` (`SetMinimized`, reg `Sidebar\Minimized`): tiles and title hidden, only the taskbar-tile collection and overflow row (24 px) remain, the sidebar re-docks to the taskbar's edge as a strip of `_cxSidebar` px at the right end of the taskbar; toggle button glyph 1170 ↔ 1167 (2.4.2) | the taskbar's right end in the minimized mode (7.3) |
| Minimised-window tiles (MWT) | `TaskData._CheckMWTs`: iconic windows can become sidebar tiles (`ShowMinimizedWindowTile`), header restore/close → `RestoreTask`/`CloseTask`; the "Rich Minimized Application" (RMA) hooks `TrackRMA`, `EnableRichMinimizedApp` | not done |
| Flyout | `CreateFlyout` body (or the tile's own content moved into the flyout when there is none) in a `Popup` placed at the tile rect on the free side, `FlyoutContent` width 150 when placed on the flipped side; Aero frame 1175 grid 53,3,3,3 with the Options row (17 px) and border 8,4,8,8; the open flyout keeps an autohide bar visible (`ShowingFlyout`); optional bottom link (`FlyoutLinkText`) | `flyout.lua` (7.7) |
| Clock tile | pinned module, `Sidebar\Clock` DWORD; views in 2.3.4; alarm; calendar flyout | `tiles/clock.lua` (8.1); alarm and calendar flyout not done |
| Notifications | `notification.xaml` toast: 205x60 frame 4002 with a countdown bar (4001), close button, Options; bottom-right of the work area with a 10 px margin, round-rect radius 9, 10 s timeout (min 5), expands on hover over 200 ms, body text wrapped at 110/154/220, ≤ 2 buttons 73x24 (2.4.5) | section 9 |

---

---

## 4. luxshell: windows and code

```
luxwm ── EWMH, _LHWM_* ──▶ luxshell (one process: C core + Lua)
                            ├─ luxshell-taskbar  ARGB dock, bottom strut, _NET_WM_STATE_ABOVE
                            ├─ luxshell-sidebar  ARGB dock, right strut (pinned mode: bottom strut; stacked at the bottom: none, BELOW)
                            ├─ luxshell-desktop  24-bit _NET_WM_WINDOW_TYPE_DESKTOP, sticky + below, no strut (section 11)
                            ├─ popups            override-redirect ARGB: luxshell-flyout, luxshell-tooltip (POPUP_MENU),
                            │                    luxshell-menu (MENU), luxshell-toast (NOTIFICATION)
                            ├─ tray manager      InputOnly window owning _NET_SYSTEM_TRAY_S<screen> (section 10)
                            └─ D-Bus             org.freedesktop.Notifications on the session bus (section 9)
                            starts and stops: flxp-startmenu or flxp-taskbar (sections 12, 13), picom (section 14)
```

One process keeps the model (window list, tray, clock, tiles) in one place, so the taskbar yields to the sidebar without IPC.
Each top-level is a `surface` (X window, cairo xlib surface on the 32-bit visual, or 24-bit with `opaque = true`, a scene root).
A surface's frame is laid out and painted into an offscreen group and put on the window with one SOURCE paint (painting the
40-odd layers straight into the window let the compositor show half-drawn frames on every hover); only dirty surfaces repaint,
`XFlush` after each. Images are prescaled once on the client (server-side `FILTER_BEST` of the 263 px clock layers on every
frame cost 10 ms per sidebar repaint). The poll loop runs with timeout 0 while Xlib's queue holds events already read
(`XEventsQueued(QueuedAlready)`): round trips in Lua handlers otherwise left events waiting for the next timer.

C: `luxshell.c` (surfaces, the EWMH client model, input, timers, the Lua API `lh`), `scene.c`/`scene.h`, `text.c`, `tray.c`,
`notify.c`, `menupaint.c`. Lua (found beside the binary: `luxshell/` of the tree, `<prefix>/lib/lhde/luxshell`; `LUXSHELL_LUA`
overrides): `runtime.lua` (event bus, animation, packs, taskbar, sidebar and its modes, maximised mode, theme switching, classic
bar and start-menu coordination, compositor, root commands), `session.lua` (the theme through liblhde, settings.ini, the state
file, session commands), `settings.lua` (settings flyout, run box), `menu.lua`, `flyout.lua`, `toast.lua`, `notifylog.lua`,
`tooltip.lua`, `apps.lua` (desktop entries, icon-theme lookup, launch counts), `tiles/*.lua`.

Events (`lh.on(name, fn)`, `lh._emit`): from C `windows`, `window(id)`, `desktop`, `maxmode`, `workarea`, `toplevel(win, map |
unmap | destroy)`, `lh-theme`, `command`, `settings-changed(serial, hint)`, `start-key(l0..l4)`, `startmenu-open`,
`selection(name, owner)`, `tray(count)`, `tray-lost`, `notification`, `notification-close(id)`, `surface-press(x, y, button,
surface id)`, `exit`; from Lua `start-click`, `start-press`, `start-forwarded`, `toggle-sidebar`, `sidebar-options`,
`sidebar-resize`, `tile-menu`, `tile-close`, `tile-restore`, `tile-visibility`, `clock-layout`, `clock-pinned`, `tray-owner`,
`notifylog`, `pack`. `LHSHELL_DEBUG=1` logs every event and paint with CLOCK_MONOTONIC ms.

`lh.animate(node, prop, to, ms, easing, done)` is Animator2 (2.4.3): 25 ms tick, linear / easeout `2t - t^2` / easein /
easeinout, the first frame applied in the same repaint as the trigger; `ms = 0` cancels a running animation and sets the value.

Not done: Wayland (a `surface` back end on wlr-layer-shell, anchor + exclusive zone = strut, was the plan; nothing above the
surface layer would change).

### 4.1 Scene graph (`scene.h`)

| Kind | Models | Notes |
| --- | --- | --- |
| panel (layouts `dock`, `fill`, `canvas`, `hbox`, `vbox`) | `DockPanel` (WPF dock rules), `FillPanel`, `Canvas`, stacks | `dock` left/top/right/bottom/fill; sizes fixed, percent or auto; `margin` = a Border's thickness; `clip`, `opacity`, `visible` |
| `ninegrid` | `HackImage`/`HackBorder` with `HackNineGrid`, uxtheme `SIZINGMARGINS` (STRETCH/TILE), `HackFlipHoriz`, state strips (`IMAGECOUNT`) | `grid` is **top, right, bottom, left**; `HackNineGrid` is a XAML Thickness (left, top, right, bottom), so 1178's `36,0,0,0` is `grid = {0,0,0,36}` and 1187's `0,7,0,0` is `{7,0,0,0}`; each piece samples only its own source rectangle |
| `image` | `Image`, 16 px icons, clock layers | `filter` per node, else the pack's (`nearest` / `bilinear`); shrinking is always BEST |
| `text` | `Text` | GDI text through libuxtheme (section 15): font, colour, `wrap`, `ellipsis`, `markup` (mnemonic underlines), `shadow`, `glow` |
| `rect` | `Border Background`, black layers | fill, `radius`, `stroke`/`stroke_width`, `gradient = { dir = "h"|"v", {offset, "#rrggbb"}, ... }` (LinearGradientBrush) |
| `button` | `Button`, `AreaButton`, `DragButton`, `StartButton`, `TaskButton`, `TileThumb` | states normal/hot/pressed/checked/hotchecked/flash/disabled pick an image; press-and-release-inside; `on_drag(dx, dy)` |
| `transform`, `hand` | `TransformDecorator`, `ScaleDecorator`, `AnalogClockLayerDecorator` | rotate about an origin, translate, scale |
| `tray` | the notification area | XEmbed icons in rows (`icon_size`, `pitch`) |

Any node: `hoverable`, `cursor` (sizewe, sizens, hand, arrow: the X cursor while the node is hot or pressed), handlers
`on_click`, `on_press`, `on_release`, `on_hover`, `on_within`, `on_drag`, `icon_geometry_for = <client>` (section 5.3).
`node:desired([w, h])` and `node:measure([wrap_width])` measure a subtree as layout would. Surfaces: `lh.surface{ name, type,
popup, opaque, dock, strut, layer }`, `:move`, `:show`, `:hide`, `:raise`, `:lower`, `:set_layer`, `:background(color)`,
`:round(r)` (XShape), `:grab()`/`:ungrab()` with `on_outside`, `on_key`, `on_enter`/`on_leave`, `:icon_offset(dx, dy)`.
Nodes are reference counted; a Lua node's table lives as long as the C tree links it, so a replaced pack's tree is freed once
nothing (animation, timer, tile) holds it. Image names: pack-relative paths (`4074/imageres/1187.png`), `@icon:<window>`,
`@notify:<id>`, `@menu:<depth>`; `lh.image_flush()` drops cache entries whose file's mtime or size changed.

## 5. Packs

A pack is `share/packs/NAME/shell.lua` (text only); its images are under `packs/<build>/` of the imported Windows tree
(`~/.local/share/lhde-windows/generated/packs`, written by lhde-import; `lh.data_find`). The theme's `Shell.pack` names it.

| Pack | Build, look |
| --- | --- |
| `4074-aero` | Aero imageres 1167-1201, 30 px taskbar, glass, `filter = nearest` |
| `4074-aerovision` | `4074-aero` with `taskbar_text = "glow"` and `clock_pack = "4074-aero"` (taste: Aero vision's text on glass) |
| `4074-slate` | the Slate ids (`ids` maps the Aero ids: 1178 → 1000, 1187 → 1155, 1179 → 1156, ...), 30 px, `tile_bg = 1000`, 6 px sizer 1108, `nearest` |
| `4051-slate` | `4074-slate` with `filter = bilinear`, `imageres = "4051/imageres"`, a 29 px bar, the clock in the tile list |
| `4042-plex` | the Plex sidebar parts (`start_compose`, `sidebar.title`, `tile_chrome`, a three-piece `flyout`), 24 px bar |
| `5112-aero` | 5112's imageres, 30 px |

`R(id)` gives the pack's image for an Aero imageres id: the pack's `ids` remap, then the pack's own `imageres` directory when it
has that image (4051: the 34 PNGs of 4051's imageres.dll that differ from 4074's or are missing there, e.g. tile arrow
1105/1106, tile restore/close 1058-1062, toast parts 4003-4011), else `packs/4074/imageres`. The pack's name is its
directory's. Keys: `family` (`slate` in both Slate packs: Slate's opaque flyouts, toasts and borders), `font`, `filter`,
`default_icon`, `taskbar = { height, bg_grid, bg_filter }`, `taskbar_text`, `taskbar_glow`, `taskbar_dy`, `start = { w, h }`,
`start_dy`, `start_alpha`, `start_extend_bottom`, `start_compose`, `toggle`, `toggle_minimized`, `taskbutton = { max_w, fill_w,
margin, grid, content, icon_margin, font, images }`, `sidebar = { width, bg_grid, sizer, title }`, `tile_bg`, `tile_chrome`,
`flyout`, `tray = { icon, pitch, origin }`, `tiles`, `pinned_tiles`, `taskbar_tiles`, `clock_pinned`, `clock_pack`,
`clock_face`, `clock_layers`, `toast_place`.

**Image filter.** Stretched nine-grid pieces and magnified images use the pack's filter. Evidence, as fits of 1156 / 1179 drawn
79x29 over the bar's own background (mean squared error per pixel): 4074 Slate (`lh4074-181533.png`) nearest 54 vs bilinear
250, Aero without DWM (`lh4074-181415.png`) 129 vs 266, Aero glass (`4074-dwm.png`) 1868 vs 2047 (blur under it); 4051
(`4051-vm-now.png`) bilinear 0.2 vs nearest 249. The flag's edge profile shows the same (4074 repeats rows, 4051 ramps).

**4051 vs 4074 Slate** (builds after ~4060 look like 4074). 4074 Slate's Avalon taskbar (`lh4074-181533.png`) is 30 px, rows
834..863, with 1155 (50x28) stretched whole, nearest (rows 7 and 21 repeat; a 7 px top grid does not fit): bg grid 0; the Start
button 83x29 with the 4 px right Border (image 79x29; error 0.03 per pixel against the shot). 4051 (`4051-vm-now.png`) is 29
px, rows 835..863, 1155 drawn top 7 / bottom 2 fixed and the middle nearest (`bg_filter`, repeating row 16 as the classic bar
does: exact); 1156 bilinear (error 1.4). The 4051 `Sidebar` class blobs are pixel-identical to 4074 Slate's streams (60 of 60).
Against `4051-vm-now.png` the sidebar's width, edge columns, options row, separator rows 17/18 and first tile arrow are exact.
4051 ran the classic Win32 taskbar (task band plates 349, no sidebar toggle): that is flxp-taskbar (section 12); `4051-slate`
with the Avalon taskbar is a stand-in.

## 6. The taskbar

### 6.1 Geometry, Start button, toggle, rows

Height = row height (`taskbar.height`: 30; 4051 29; Plex 24) x `[taskbar] rows` (1..3, Taskbar\RowCount). The bar runs from x 0
to the sidebar's left edge (`SetMixedTaskbarSidebar`), the full width when the sidebar is minimized or stacked at the bottom;
strut = its height (2 with autohide). Layers bottom up: `TaskbarBlackLayer` (black rect, opacity 0), `TaskbarBackground` 1187
(grid from the pack), `InnerTaskbar` dock panel: StartButton (Dock Left, Width 83, Height 29, Border 0,0,4,0, 1179/1180/1181;
in a canvas slot, so on a bar of several rows it stays one row high at the top), TaskbarToggleBar (Dock Right, 29 px slot, the
17x17 glyph 1170, 1167 while the sidebar is minimized, hover 1171, pressed 1172), the TBContentHost (the minimized sidebar's
tiles, section 7.3), the task rows. A 3 px invisible SizeNS strip on the top edge changes the rows (the horizontal bar's
TaskbarSizer is Width 0; rows go through the appbar's sizing edge, `_PtOnResizableEdge` → `SetTaskbarRowCount`), snapping at
half rows; `[taskbar] rows` is written on release. The Start button shows StartButton.FakePressed (1181, `checked`) while the
start menu is open (section 13).

### 6.2 Task buttons

Windows from `_NET_CLIENT_LIST` on the current desktop plus sticky ones; skipped: skip-taskbar, transients, dock/toolbar/menu/
splash/utility types (luxwm publishes `_NET_WM_STATE_HIDDEN` with `WM_STATE` Iconic, `_NET_CLIENT_LIST_STACKING`,
DEMANDS_ATTENTION). Width rule (2.4.2): items per row = `ceil(n / rows)`, width `min(max_w = 140, w / per)`; Plex
(`fill_w = 130`, 4042's TaskPresenter) shares the whole width. TaskButton = Border 0,2,2,0 > 1182/1184/1185 grid 3 > Border
6,5,6,6 > icon 16 + 2 px + title (ellipsis); checked = the active window. Click: activate, or minimise when already active;
right click: luxwm's window menu at the pointer (`lh.window_menu`, `_GTK_SHOW_WINDOW_MENU`; XP's task button menu is the
system menu). Drag: past 4 px the button takes the place of the one under the pointer, live (as 4074's taskbar); the order is
kept for the X session in the root property `_LHSHELL_TASK_ORDER` (window ids, decimal, space separated, all desktops), so a
restarted luxshell keeps it; new windows go to the end.

Icon: `_NET_WM_ICON`, the smallest image >= 16 (else the largest), scaled once with `CAIRO_FILTER_BEST`; else the `WM_HINTS`
icon pixmap + mask; else the pack's `default_icon` (shell32 16/3, IDI_APPLICATION, as Explorer). Re-read on every
`_NET_WM_ICON` / `WM_HINTS` PropertyNotify.

Text: `taskbar_text = "plain"` (white, 4074) or `"glow"` (black over a white halo, the GDI coverage blurred: `taskbar_glow = {
color, text, size = 4, strength = 2.5 }`), also used by the minimized sidebar's taskbar clock.

### 6.3 `_NET_WM_ICON_GEOMETRY`

Each task button writes its plate's rect (inside the 0,2,2,0 margin) in root coordinates on its client after every layout,
rewrites it when the buttons reflow and deletes it when the button goes; a hidden taskbar surface (classic / none) drops all of
them. With autohide the geometry is where the button is when the bar is shown (`icon_offset`).

### 6.4 Autohide

`[taskbar] autohide = 1`, the Avalon rule (2.4.2): the window keeps its size and is shifted so `2 + (h - 2) * pct / 100` px stay
on screen; while "focus within" (pointer over the bar, a menu or flyout of ours open) pct moves by 20 every 20 ms (100 ms,
linear), polled every 100 ms while shown, re-shown by `EnterNotify` on the 2 px sliver. The strut is the sliver, so the work area
grows. Switching autohide on jumps straight to 0 / 100.

### 6.5 Taskbar menu

Right click on the bar outside the buttons. `ExtendedTaskbar.GetContextMenu` (L23627): *Minimize bar* / *Restore bar* (the
sidebar's real state; 4074 passed constants and always said "Restore bar"), *Properties* (the settings flyout), separator,
*Task Manager* (the first of xfce4-taskmanager, lxtask, gnome-system-monitor, mate-system-monitor, qps, else `xterm -e top`;
greyed when none; looked up with `io.popen`, since `os.execute` reports -1 under luxshell's ignored SIGCHLD), separator, *Show
Desktop* / *Hide Desktop* (`_NET_SHOWING_DESKTOP`), *Cascade* (26 px steps, 60 % of the work area), *Tile Vertically*, *Tile
Horizontally* (more than three windows: a grid, as TileWindows), through `_NET_MOVERESIZE_WINDOW` over the work area (client
sizes; luxwm adds its frame); then luxshell's own: *Auto-hide the taskbar*, *Sidebar* ▸ Full / Minimized / Pinned tiles only.
No screenshot of this menu exists; the wording is the IL's.

## 7. The sidebar

### 7.1 Geometry, edge, width

150 px (`Sidebar\Width`; the bar is x 1130..1279 on `4074-dwm.png`, column 1130 being 1178's dark column 0), full height on the
right, right strut = its width; `[sidebar] width` 100..600 (`_ValidateSize`). Tree: `SidebarBlackLayer`, `SidebarBackground`
1178 (grid left 36: the edge gradient keeps its width at any bar width; Slate 1000, no grid), `sidebarMain`: the options row
(17 px, the 9x6 glyph 1176/1177 in Border 4,7,2,2 (the 10 columns of 1176 drawn into 9 by dropping one, as both builds do:
exact with nearest), "Options" fading in over 120 ms on hover), separator 1199 (2 px), the pinned host at the bottom (clock),
the clock separator (3 px, shown only above a non-empty pinned host), the tile collection. `SidebarSizer` on the inner edge:
2 px (Aero, image hidden; the visible edge is 1178's columns 0-1) or 6 px of 1108 (Slate: without it 1000's dark column 0
showed as a black line). Dragging it (`_OnSizerDrag`, width - dx) applies the width every 25 ms: surface, strut, the taskbar,
`_NET_WORKAREA` (luxwm refits maximised windows), event `sidebar-resize` (tiles reflow); `[sidebar] width` on release;
`[sidebar] locked = 1` (*Lock the Sidebar*, the 4051 `SetLockBar` idea) stops the drag. Plex: its own title band
(`sidebar.title`: TITLEBACK, FLYOUTOPTIONSBACK hot/pressed, the glyph centred) and no separators.

### 7.2 Stacking

`[sidebar] stacking = top` (default, 4074): `_NET_WM_STATE_ABOVE` like the taskbar, raised with it on window-list changes, its
strut, full height, the taskbar shortened. `bottom`: part of the desktop, `_NET_WM_STATE_BELOW` (luxwm keeps BELOW docks under
the clients), no strut (windows and maximised windows cover it), ending at the top of the full-width taskbar, which then shows
the clock's taskbar view at its right end (the sidebar's clock may be covered). Win+S (luxwm `shell sidebar-stacking toggle`),
the sidebar menu's *Stacking* ▸ and the settings flyout switch it at once, no animation; the order of the steps keeps every
in-between state invisible (to bottom the taskbar first runs the full width under the sidebar's corner, to top the sidebar
first runs the full height under the taskbar; the restack comes last). A server grab around it froze picom (xrender) mid-paint.

### 7.3 Modes

`[sidebar] mode` (default: the theme's `Shell.sidebar`, else full), from the toggle button (`_ToggleSidebar`), the menus and the
settings flyout:

- `full`: the layout above.
- `minimized` (`SidebarControl.SetMinimized`): the sidebar slides out to the right (400 ms ease-in-out) and is unmapped, no
  strut; the taskbar runs the full width with the pack's `taskbar_tiles` (4074 `Sidebar\TaskbarTiles`; else the pinned tiles) as
  compact views (`tile:taskbar(host)`: the tray row, the digital time, the Quick Launch icons, the notification bell and count)
  at its right end; the toggle shows 1167.
- `pinned` (ours, taste): the surface shrinks to the bottom block of `pinned_tiles` (notifications + clock), no options row; a
  **bottom** strut of the block's height over the sidebar's columns (luxwm's work area is one rectangle; a right strut would
  take the full width for a small block), so everything above is free desktop. With autohide on, only the clock and the tray
  icons remain: the "minimal" mode.

Transitions slide or shrink over 400 ms ease-in-out; the struts take their final value when a transition starts.

### 7.4 Yielding to a bottom dock, the classic taskbar

`[sidebar] yield_bottom = 1` (default) | `0`: `lh.struts()` reads `_NET_WM_STRUT_PARTIAL`/`_NET_WM_STRUT` of every mapped
top-level not ours (and one level down, inside WM frames); the largest bottom strut over the sidebar's columns shortens it (the
right strut's range follows), lifts the pinned block above it (bottom strut = block + bar) and keeps the minimized one out of
the way. Re-checked on `_NET_WORKAREA` changes and on top-level map / unmap / destroy (luxwm may keep `_NET_WORKAREA` unchanged
when one bottom strut replaces another). With the classic taskbar (section 12), `[sidebar] with_classic = 0` (default) hides
the sidebar entirely (unmapped, no strut, `_LHSHELL_CLOCK_VISIBLE` 0); its mode is kept and comes back with the Avalon taskbar.
`with_classic = 1` is the 4051 layout: the sidebar yields above the full-width classic bar.

### 7.5 Tiles and their chrome

The tile list is luxshell-state.ini `[sidebar] tiles` (`;`-separated, `none` = empty) with `[sidebar] tiles_base` = the pack's
`tiles` it was made from: while the pack's list is unchanged the user's list holds, otherwise the pack's list wins. A tile added
again goes to its place in the pack's order (else 4074's SidebarTiles order: notifyhistory, quicklaunch, notifications, power,
clock). Tiles are `luxshell/tiles/NAME.lua`, rebuilt on a pack switch, a mode change and a reload.

Chrome (`tile.xaml` / `outertile.xaml`): the right-docked bar is Flipped (2.4.1), so `TileSideAreaBack` (9 px: arrow 1173/1174,
restore 1058/1059 and close 1061/1062 on hover) docks **right** and `TileForegroundPadder` (6) **left**: the foreground is x
7..140 of the bar (133 wide) and y 3.. of the tile, where the 4074 Slate power tile's battery starts (`lh4074-181415.png`) and
where the 4051 arrow sits (top-right). Bottom: the 4 px `OuterContentHostSizer` (1199/1200, Border 0,2,0,0). Slate repaints
1000 under each tile (`tile_bg`). `TileUI` fades in over 800 ms. Plex (`tile_chrome`): TILEBACK under the whole tile, a 16 px
side strip (TILEHEADERBACK, TILECHEVRONBACK pressed, the tile icon) fading in over 300 ms while the pointer is within the tile
and out over 500 ms, a 7 px TILESIZER.

Tile API (mirrors BaseTile, 2.5): a tile file returns a table `{ name, height, pinned }` with methods `initialize` (the host has
set `foreground`, `sb`, `T`, `R`), `dispose`, `flyout()` → node, w, h (a click on the foreground or the side strip toggles it),
`flyout_closed`, `properties()`, `menu()` (items in front of the host's), `taskbar(host)` (the compact view). Host calls:
`sb.resize_tile(tile, h)` (400 ms ease-in-out, min 30, 2.4.4), `sb.tile_rect`, `lh.open_flyout` / `lh.toggle_flyout` /
`lh.close_flyout`, `lh.tooltip(node, text)`.

### 7.6 Sidebar and tile menus

Sources: `lh4074-091929.png` (4074 Slate sidebar menu: *Minimize bar*, *Properties*, separator, the parts list *Basket, Classic
Tray ✔, Contacts, News Feed, Notification History ✔, People nearby, Quick Launch ✔, Search ✔, Slide Show, Sync*, separator,
*Power (Transient) ✔, Web Host Tile (Transient) ✔*, separator, *Clock ✔*; *Properties* opens "Taskbar and Start Menu
Properties", `lh4074-091944.png`) and the IL: `SidebarBaseMenu2.GetBaseContextMenu` (L17037), `SidebarControl.GetContextMenu`
(L37721), `_OnOptions` (L38583), `OuterTile._GetContextMenu` (L44524: the tile's own items first, *Auto Size* (checkable), *Move
Up*/*Move Down* (*Left*/*Right* in the taskbar), *Remove <name> Tile from overflow*, *<Friendly Name> Tile Properties* with a
property page, *Remove <Friendly Name> Tile* unless pinned; no separators).

- Sidebar menu: right click outside the tiles, or the Options glyph (under it, right-aligned). 4074's base items, then
  luxshell's group *Lock the Sidebar* and *Stacking* ▸ Top / Bottom, then the parts list in the screenshot's order: *Classic
  Tray* = `notifications`, *Notification History* = `notifyhistory`, *Quick Launch* = `quicklaunch`, *Power (Transient)* =
  `power`, *Clock* = `clock` (checked = in the tile list, a click adds or removes it; only in full mode), the other modules
  greyed. The list stays inline as in 4074.
- Tile menu: right click on a tile, or a click on its arrow when it has no flyout. The tile's `menu()`, a separator (ours),
  *Auto Size* (checked and greyed: tiles size to their content), *Move Up* / *Move Down* (greyed at the ends and for pinned
  tiles), *<Name> Tile Properties* (`properties()`, else the flyout; greyed without either), *Remove <Name> Tile* (not on pinned
  tiles). Friendly names: Classic Tray, Power, Clock, Quick Launch, Notification History.

All menus are `menu.lua` over wuxtheme's painter (section 16).

### 7.7 Flyouts (`flyout.lua`)

`flyout.xaml`: 1175 nine-grid 53,3,3,3 (Slate 1109, grid 3), the Options row (17 px, glyph 1176/1177 in Border 4,5,4,4,
"Options" fading in over 120 ms), FlyoutBorder 8,4,8,8 (Slate 4,0,4,4); Plex: the three-piece FLYOUTTOP / CONTENT / BOTTOM. One
flyout at a time, `_NET_WM_WINDOW_TYPE_POPUP_MENU`, placed left of the bar at the tile's top (placement 2), clamped to the
screen; closed by a second click on the tile, a click on another luxshell surface or any click outside (pointer grab with owner
events, `on_outside`). Other owners (the settings flyout, the run box) may add `place`, `on_key` (keyboard grab; Escape closes)
and `stay_open`. An open flyout or menu keeps an autohide taskbar shown.

## 8. Tiles

### 8.1 Clock (`tiles/clock.lua`)

`[clock] pinned = 1` (4074: the pinned host at the bottom) | `0` (4051: a tile in the list: ~96 px face at tile y 22, "6:37 AM,
Oct 23" in Tahoma 10 at tile y 142, tile 160 high). The choice holds for the pack it was made in (`[clock] pack`; a
pack's `clock_pack` shares another's: aerovision → aero); other packs use their `clock_pinned` (nil = pinned). `[clock] layout`
= the six `clocksidebar*.xaml` views: `default` (80 px analog + LongTime), `digtime`, `digtimemonthday`, `digtimeweekday`
(analog + bold DigitalTime #D8E4FF / #9EB5BB + DigitalText lines; the AnalogClockPanel has no size in these XAMLs: 56 px, 40 px
beside the weekday/date lines, inferred), `minical` (42 px clock + MiniCalendar + centred bold time), `cal` (the month grid,
gradients #838A90 → #767B7F header, #6B7278 → #5B6366 body, Tahoma 7.5 white dates, weeks from the locale's first weekday).
Chosen from the tile's menu or its Properties flyout (the six layouts and the pinned switch). XAML margins like `bin:02HHVV` are
two-value Thickness (horizontal, vertical). Times follow the environment's LC_TIME (luxshell itself runs in the C locale).

Face sets, chosen by the theme's visual style (pack key `clock_face` overrides): `slate*` and anything else the light set
(1074 face, 1076 glass, image hands 1077/1078/1079 and anchor 1083 on `SidebarClockSettings`' LayerOrigins, design size 260;
`~/fun/longhorn/sidebar/CLOCK.md` 1.1); `jade*`, `aero*` the dark-blue set of `4074-dwm.png` (face 1190 263x263, 1192 a
byte-identical copy, glass 1191 265x265, hour 1193 / shadow 1194, minute 1195 / 1196, second 1197 / 1198, anchor 1188/1189;
its hand pivots are estimates: tail ~22 design px for hour/minute, ~35 for the second hand); `plex*` the Plex builds' light
set from `packs/4042/imageres` when imported (`clock_layers = "full"`: 4042 main's 179 px clock as the tile's Background, Arial
20 white time under it, checked against `4042mainwmptile.png`). `analogclock.xaml` itself names 1074/1076 with black XAML
rectangle hands; the 4074 shots without DWM show the light set under Jade and Slate alike. Slate hands are thin dark grey
(~#3E3F3F). One 1 s timer; the second hand ticks.

### 8.2 Notification area (`tiles/notifications.lua`, *Classic Tray*)

The tray (section 10) in a tile. Geometry from the 4051 / 4074 quick-launch icon tiles (`4051-vm-now.png` y 19..51, icons at x
1015 / 1043; `lh4074-181415.png` x 1017 / 1043): first icon at 5,6 in the foreground (12,9 in the tile), **28 px pitch** (5 per
row in 133 px), height **33** for one row incl. the sizer, + 24 per extra row (inferred; no multi-row reference), animated on
`tray(count)`. Hidden while another client owns the tray selection; minimized taskbar view: one row, 20 px apart, centred.

### 8.3 Power (`tiles/power.lua`)

`/sys/class/power_supply/BAT0` (`status`, `capacity`, `energy_now/full` + `power_now` or `charge_*` + `current_now`),
`AC/online`, every **30 s**; time left = now / rate (discharging), (full - now) / rate (charging). `LHSHELL_POWER_SUPPLY` /
`LHSHELL_BATTERY` point it at a fake sysfs directory. `powergauge.xaml` uses no imageres ids, only vector brushes: the battery
(`battery_body` 35x19 ShinyWhite, `nub` 3x10, red plus/minus, `ACLogo` plug) rotated 270° and scaled 0.95, at x 7..24, y 7..41 of
the tile as measured in `lh4074-181415.png`; the ACLogo pulses 0 → 1 → 0 over 2 s while charging. Gauge `#808080` 104x19 at x
33, y 6, ShinyGreen (Yellow <= 25 %, Red <= 10 %) inset 2 px over a 40 % copy, GraySweep 15 %. Text Segoe UI 13 bold `#dddddd`
centred under it: "Charging: N%", "XhMM left: N%", "Charged: N%". Tile 49 high. Flyout (`powerflyout.xaml`): white, 194 wide,
"Power Status (1)" #808080 bold 14, the gauge, status / charge / time / rate / capacity rows, "Power Plans" with the selected
one from `/sys/firmware/acpi/platform_profile` (read-only); brightness and "keep my screen on" are not shown.

### 8.4 Quick Launch (`tiles/quicklaunch.lua`)

4074's QuickLaunchTile: the `.desktop` files of `[quicklaunch] dir` (default `~/.config/lhde/quicklaunch/`, created once with
copies of the browser, the mail client, the file manager and a terminal; an emptied folder stays empty), file-name order,
re-read on change (5 s poll). One row of 16 px icons at 28 px pitch, the first at 12,9 (the tray tile's geometry), 35 high,
wrapping at 24 px (inferred). Click launches (`apps.lua A.launch`: field codes dropped, `Terminal=true` in xterm, counted in
`~/.local/state/lhde/luxshell-mfu.ini`). Tooltips per item; menu *Open Quick Launch folder* (ours).

### 8.5 Notification History (`tiles/notifyhistory.lua`, `notifylog.lua`)

4074's NotificationHistoryTile: a time line, not a list. The newest (at most 8) notifications are 19x16 bubbles (4014 unviewed,
4013 viewed) along the bar by age (span = max(30, minutes since the oldest), f = age / span, square-rooted above 30 min; offset =
(1 - f) * width); neighbours <= 20 px apart pile into stacks of at most 4 (15 + 4 (n - 1) wide, diagonal down-right, newest on
top); a ninth drops the oldest stack; a 60 s timer re-spaces. Verified on `lh4074-193747.png`. A bubble's tooltip is the time
and header; a click shows the toast again. The flyout: 400x300 white list by day, time, bubble, header (#E96D00) and body;
closing it marks all viewed. Menu: *Go to rules and alerts* (greyed: no rules engine) and *Delete All Notifications* (ours
works; 4074's did nothing). Store: `~/.local/state/lhde/luxshell-notifications`, newest last, at most 100, one tab-separated line
each (time, viewed, app, icon, summary, body; `\\ \t \n` escaped); `transient` notifications stay out; `CloseNotification`
revokes, `replaces_id` updates in place; viewed once dismissed, clicked, re-shown or seen in the flyout.

## 9. Toasts: `org.freedesktop.Notifications` (`notify.c`, `toast.lua`)

| Item | Value |
| --- | --- |
| Server | libdbus-1 on `DBUS_SESSION_BUS_ADDRESS` only (no X11 autolaunch); `RequestName(DO_NOT_QUEUE)`; when another daemon owns the name, no toasts (logged). `GetCapabilities` (body, actions, icon-static), `GetServerInformation` (name luxshell, vendor lhde, spec 1.2), `Notify` (`susssasa{sv}i`; `replaces_id` keeps the id), `CloseNotification`, Introspect; signals `NotificationClosed` (1 expired, 2 dismissed, 3 closed by call) and `ActionInvoked` |
| To Lua | event `notification` with `id, replaces_id, app_name, app_icon, summary, body, actions = {{key, label}}, hints, urgency, image_path, image` (`image-data` → `@notify:<id>`), `expire_timeout`; `notification-close(id)`; `lh.notification_closed(id, reason)`, `lh.notification_action(id, key)` |
| Window | override-redirect ARGB, `_NET_WM_WINDOW_TYPE_NOTIFICATION` (picom: no shadow, no rounding), XShape round rect radius **9**, never focused |
| Layout (`notification.xaml`) | ToastBackground black (opacity 0 under Aero = glass); 4002 frame grid 7 (205x60 minimum); TimerControl 5 px on the right, 4001 shrinking from the top; options row: close 14x15 4003/4004/4005, "Options" white; icon left (image-data, image-path, app_icon path or icon theme; <= 48); 4006 panel grid 7, border 6: summary Franklin Gothic Medium 15 px, body 12 px wrapped at 110, else 154 (<= 2 lines), else 220 (<= 4 lines); <= 2 action buttons 73x24 (hover 1110, pressed 1111) |
| Behaviour (2.4.5) | 10 s default, minimum 5 s (`expire_timeout` 0 = until dismissed); the bar ticks every `timeout / height` ms; hover stops it, rewinds to 0.75 in 200 ms and expands downward (buttons + Options fade in, 200 ms); leaving collapses and restarts; one at a time, FIFO; a click outside the buttons = the `default` action (if offered) + dismissed; the close glyph = dismissed |
| Placement | `x = work.right - w - 10`, `y = work.bottom - (expanded - collapsed) - h - 10`: above the taskbar (ours or the classic one), left of the sidebar (full width when the sidebar is hidden); pack `toast_place = "sidebar"` puts it over the sidebar's top |

No reference screenshot of a 4074 toast exists; the look follows the XAML and the 4002/4006 images.

## 10. The tray (`tray.c`)

| Item | Value |
| --- | --- |
| Protocol | freedesktop System Tray 0.3 + XEmbed: an InputOnly manager window owns `_NET_SYSTEM_TRAY_S<screen>` (server timestamp from a property append), `MANAGER` on the root, `_NET_SYSTEM_TRAY_ORIENTATION` 0, `_NET_SYSTEM_TRAY_VISUAL` = the 32-bit visual, `_NET_SYSTEM_TRAY_ICON_SIZE` 16 |
| Docking | `REQUEST_DOCK`: select Structure/PropertyChange, `XAddToSaveSet`, reparent into the surface holding the `tray` node (sidebar or taskbar), 16x16, `XEMBED_EMBEDDED_NOTIFY`, map per `_XEMBED_INFO`; a foreign ParentRelative background is reset (BadMatch in a 32-bit parent). Unmap / destroy / reparent-away undock; an icon resizing itself is put back. Balloons (BEGIN/CANCEL_MESSAGE) are ignored |
| Alpha | an ARGB child would punch holes in the glass, so with Composite + Damage each icon is redirected manually and painted with cairo OVER in the `tray` node's paint (Damage → repaint); input still goes to the icon. libXcomposite / libXdamage are `dlopen`ed (no build dependency; without them icons draw themselves) |
| Hand-back | on exit (SIGTERM/SIGINT) and `lh.tray_stop` the icons are unredirected, unmapped and reparented to the root, the selection dropped (left mapped, the WM would manage them and a later re-dock froze the pointer on an orphaned grab) |
| One tray per display | `tray_start` never takes the selection from another owner; while another owns it the notifications tile hides (`tray-owner`, XFixes `SelectSelectionInput`, else a 2 s poll) and claims it again when the owner goes |

Tested with `luxshell/tests/trayicon.c` (an ARGB circle that re-docks on `MANAGER`, logs clicks, optional exit timer). GTK
`GtkStatusIcon` apps and anything on the XEmbed fallback dock the same way.

## 11. Maximised mode

Longhorn: a maximised window's frame becomes a dark plate and the bars go black (`SetMaximizedWindow`, 2.4.3); nothing else on
screen changes (`screenshots/aero_max.png`, a maximised 4074 Aero Explorer). luxwm draws the frame (LGFRAMEMAX 493 /
MAXEXPLORER 494, docs/LUXWM.md "Maximised mode") and publishes the condition as root `_LHWM_MAXIMIZED_MODE` (CARDINAL 0/1 per
desktop index, like `_NET_WORKAREA`), written in the same server grab as the geometry change, so both sides switch in one
composited frame. Condition: the active window is maximised or fullscreen, or, with none active, any visible window on that
desktop is. luxshell uses the property and falls back to the same rule over EWMH (`MAXIMIZED_VERT`+`HORZ`, fullscreen) under
another WM.

- **Black layers.** `TaskbarBlackLayer` / `SidebarBlackLayer` go to opacity 1 / 0. `[desktop] max_fade_ms = 0` (default): in the
  same repaint (the bar is dark ~80 ms after Super+Up on the lab, picom included); a number = SetMaximizedWindow's fade (4074:
  400 ms). luxwm's `MAX_FADE_MS` is 0 too. The task text's 400 ms brush animation to white is not done (the text is white).
- **Desktop layer.** `luxshell-desktop`: full screen, `_NET_WM_WINDOW_TYPE_DESKTOP`, sticky + below, no struts, kept at the bottom
  of the stack, normally unmapped (the wallpaper is the root background). `[desktop] black = 1` maps it black in
  maximised mode: its X background is set to opaque black and it is lowered before the map, so the server paints it black with
  the map (no transparent first frame), and it is unmapped at once on restore. It is 24-bit: it is only ever mapped while black,
  and picom blurs behind every ARGB window (a full-screen one cost ~150-300 ms per frame on the xrender lab). It is the future
  home of the Aurora (`xaml_sidebar_desktopaurora.xaml`); the Aurora will need ARGB. `LHSHELL_DESKTOP=0` (lhde-session sets it
  while flxp-desktop runs) leaves it out; flxp-desktop owns the desktop then.

## 12. The classic taskbar (flxp-taskbar)

The msstyles-themed classic taskbar is Win32 explorer code (`tray.cpp`, `taskband.cpp`, `traynot.cpp`, `trayclok.cpp`: a rebar
with `TaskBand::Toolbar`, `Start::Button`, the `TrayNotify` toolbar and clock, all uxtheme classes, 2.1), so it lives on the
flxp toolkit, which has Wine's comctl32 rebar / toolbar / tooltips and the uxtheme renderer; rebuilding it in luxshell's scene
would duplicate that toolkit with less fidelity. luxshell keeps the Avalon bars and the sidebar, as 4051 ran the Avalon start
bar next to the classic taskbar.

The bottom bar is the theme key `Shell.taskbar` = `avalon | classic | none` (THEME-FORMAT.md; from the settings flyout,
`_LHSHELL_COMMAND taskbar KIND` and `lhde-session taskbar avalon|classic|classic+sidebar|none` it goes into the current layer;
`LHSHELL_TASKBAR` forces one for a run). `classic` and `none`: the Avalon taskbar is unmapped, no strut, no task buttons, no
`_NET_WM_ICON_GEOMETRY`. `classic` runs `[commands] taskbar` (default flxp-taskbar from build/bin or the install, else PATH;
`$LHSHELL_TASKBAR_CMD`) through `lh.spawn_pid` (`sh -c 'exec ...'`, so the pid is the bar's) with luxshell's DISPLAY and session
bus, `FLXP_TRAY_TAKEOVER=1`, `FLXP_START_KEY=1`, its log in `$XDG_RUNTIME_DIR/luxshell-taskbar<display>.log`. Its Start button
opens `[taskbar] start_menu` (`panel`, default: XP's start panel, or `classic`: the classic popup menu), which flxp-taskbar reads
at every open. No `--theme`: flxp-taskbar follows `_LH_THEME` itself. A bar already running on the display is adopted. It is
stopped by its pid (after checking `/proc/PID/comm` and `DISPLAY` in `/proc/PID/environ`) on a switch away and on luxshell's exit.

| Need | Mechanism |
| --- | --- |
| Window list, activation, minimise, close | EWMH (`_NET_CLIENT_LIST`, `_NET_WM_STATE`, `_NET_ACTIVE_WINDOW`, `_NET_WM_ICON`, `WM_HINTS`, `_NET_CLOSE_WINDOW`) |
| Minimise target | flxp-taskbar sets `_NET_WM_ICON_GEOMETRY`; luxshell's hidden bar sets none |
| Screen space | `_NET_WM_STRUT_PARTIAL` + dock type, full width; the sidebar is hidden or yields above it (7.4); flyouts, toasts and menus sit above it (`lh.taskbar_shown_top`) |
| Tray | one XEmbed tray per display: before starting the bar luxshell hands its tray over (`lh.tray_yield`: icons back to the root, selection released, no reclaim for 10 s or until the new tray is up), since a takeover raced with flxp-taskbar's ReparentNotify handling |
| Clock | `_ToggleClock` (COMPOSITION.md §11): root `_LHSHELL_CLOCK_VISIBLE` (CARDINAL) = 1 while a clock tile of luxshell's is on screen (the sidebar's, or the minimized sidebar's compact clock in the Avalon taskbar), else 0, deleted on exit; flxp-taskbar hides its clock while it is 1 |
| Start menu | flxp-taskbar is the provider (section 13) |
| Theme | `_LH_THEME` / `_LH_THEME_RESOLVED`; flxp-taskbar switches live |
| Autohide, position | its own (explorer's `StuckRects2` logic, 2.1) |
| Black layers | none (Avalon only, 2.4.3) |

## 13. The start menu

Longhorn's Avalon shell has no start menu of its own; 4074's is the XP start panel (DirectUI, `sfthost.cpp`) skinned by the
msstyles `StartPanel` class, Win32 code next to the classic taskbar. So the start menu is flxp's: flxp-taskbar's start panel,
and next to the Avalon taskbar (or none) **flxp-startmenu** (flxp-taskbar by another name: the start panel without bar, tray
or struts). luxshell has no menu of its own; without a provider nothing opens (logged).

**Provider protocol.** The provider owns the selection `_LH_STARTMENU_S<screen>`. luxshell owns `_LHSHELL_S<screen>` while it
runs. `_LHSHELL_START` is a ClientMessage, format 32, `message_type` = `_LHSHELL_START`: `l[0]` = flags (bit 0 = toggle: close
the menu if open, else open it; 0 = open, nothing if already open; other bits reserved, 0), `l[1..4]` = x, y, width, height of
the anchor rectangle in root coordinates (width or height 0 = no anchor: the provider opens at its own Start button, else
bottom-left above the work area). The menu opens above the anchor with its left edge at the anchor's (below it when the
anchor is in the upper half of the screen). Senders:

1. **luxwm**, Super pressed and released alone (the `start` key action): to the root (`SubstructureNotify|SubstructureRedirect`)
   with `l[0]` = the key's timestamp and no rect; a root message without a rect always means "toggle, no anchor".
2. **luxshell**, to the selection owner window itself (`XSendEvent(owner, False, 0)`): for a click on the Avalon Start button
   (flags 1, the button's rect) and for every root `_LHSHELL_START` (with a rect: flags and rect as received; without: flags 1
   and the Avalon Start button's rect, or none without the Avalon taskbar).

A provider answers messages on its owner window and ignores root broadcasts while `_LHSHELL_S<screen>` has an owner (luxshell
forwards them; answering both would toggle twice); without luxshell it may answer luxwm's broadcast itself. Its own Start
button opens its menu directly. It sets root `_LH_STARTMENU_OPEN` (CARDINAL) = 1 while its menu is mapped, 0 or deleted when
closed (docs/CLIENT-PROTOCOL.md "Start menu state"); the Avalon Start button shows FakePressed while it is 1. For a
provider that never sets it, the button shows pressed from the forward until the first top-level mapped within 1.5 s of it
(its popup) unmaps, until the next forward (a click on the pressed button closes the provider's menu), or after 1.5 s without a
map; any change of the property ends that guess.

**flxp-startmenu's life.** Wanted whenever the bottom bar is not `classic` (then flxp-taskbar is the provider). Started with
`FLXP_START_KEY=1` (as the provider it always opens the start panel), log `$XDG_RUNTIME_DIR/luxshell-startmenu<display>.log`, only after `_LH_STARTMENU_S<screen>`
has no owner (one that finds it owned stays no provider; luxshell waits up to 3 s); adopted when one (or a `flxp-taskbar
--start-menu-only`) already runs on the display; restarted when it dies (checked every 3 s, at most 5 starts a minute, then
none until the settings change); stopped by its pid (comm + DISPLAY checked) on the switch to the classic bar and on exit.

## 14. Theme, settings, compositor, reload

**The theme.** Root `_LH_THEME` (UTF8_STRING) holds the selected theme's name: a theme file of `share/themes/` or
`~/.config/lhde/themes/`, or a bare style, resolved by liblhde (docs/THEME-FORMAT.md; an alias is replaced on the root by its
theme's name). At startup the root wins, else settings.ini `[theme] name`; the first one up publishes it and brings `[theme]
name` in line. The record's `Shell.pack` names the pack (`LHSHELL_THEME`, a pack name or directory, only when no theme names
one; default `4074-aero`). luxshell writes `_LH_THEME_RESOLVED` (`theme=NAME`, `serial=` the current `_LH_SETTINGS_SERIAL`,
then the record's `key=value` lines; docs/SETTINGS.md) before `_LH_THEME` and only when it does not describe the record yet
(luxwm writes the same record when it resolves first). Its own `_LH_THEME` writes are counted and their PropertyNotify ignored;
the current name written again by someone else is a pack reload (below).

A switch (`apply_theme`): pack loaded (nodes rebuilt on the same windows when the pack or the font set changed; tray icons move
to the new tray node; a toast on screen keeps its look until it closes), wallpaper, composition, the theme's `Shell.taskbar`,
the start-menu provider check, `[theme] name` (liblhde `lhde_theme_select`, which drops another theme's current layer),
`_LH_THEME_RESOLVED`, `_LH_THEME`. Per-theme keys changed in luxshell (taskbar kind, composition) go into the current layer
(settings.ini `[current]`, `lhde_current_set_key`) and the theme is applied again; the flyout then shows "NAME (Modified)".
*Save as...* writes `~/.config/lhde/themes/FILE.theme` from the theme and its layer (`lhde_theme_save_as`) and selects it.

**Settings.** settings.ini (`~/.config/lhde/settings.ini`, liblhde's store: unknown keys, comments and order kept, nothing
written when a value is unchanged) holds the user's choices; luxshell's keys: `[taskbar] autohide rows`,
`[sidebar] mode width stacking with_classic yield_bottom locked`, `[clock] pinned pack layout`, `[desktop] wallpaper
= none, black, max_fade_ms`, `[quicklaunch] dir`, `[commands]`, `[fonts] smoothing` (menus); missing
defaults are written on first run. After a write luxshell sends one root ClientMessage `_LH_SETTINGS_CHANGED` per handled event:
`l[0]` = serial (root CARDINAL `_LH_SETTINGS_SERIAL` + 1, written first, never below the highest serial seen or the clock seed),
`l[1]` = the section's atom when only one changed, else 0. On a received message (its own too) it re-reads the file 50 ms after
the burst and applies every key that differs from what runs (theme, current layer, composition, taskbar, autohide, rows, sidebar
mode / width / stacking / with_classic / locked, clock pinned and layout, desktop black, max_fade_ms, wallpaper, the start
provider); an open settings flyout is rebuilt. Runtime state stays in `~/.local/state/lhde/luxshell-state.ini` (`[sidebar] tiles
tiles_base`; `LHSHELL_STATE` overrides the path).

**Settings flyout** (`settings.lua`; *Properties* in the taskbar and sidebar menus, `_LHSHELL_COMMAND settings`): two columns,
the theme list (shipped themes whose visual style is imported, then the user's; *Save as...*) | composition (follow the theme / always on / always off: `Glass.composition` auto / 1 / 0 in the current layer),
taskbar (Avalon / classic / none) and autohide, sidebar mode, stacking and *Also with the classic taskbar*, *Show the
wallpaper*. Run box (`_LHSHELL_COMMAND run`, Super+R): a one-line command for sh (its search mode, the best matching application,
else an existing path through the file manager, else the line as a command, is opened by nothing).

**Fonts.** The theme's `path.fonts` directories become fontconfig application fonts and libuxtheme font directories (`lh.fonts`:
`FcConfigAppFontClear` + `AddDir`, `ut_add_font_dir`) at startup and on every switch, so the packs' "Segoe UI 9" is 4074's
Segoe UI 0.7; the bars are rebuilt when the set changed. Fallback family Tahoma (Franklin Gothic Medium → ITC Franklin Gothic
Std Medium).

**Wallpaper.** The resolved `Desktop.wallpaper` / `Desktop.mode` (`[desktop] wallpaper = none`: solid #3a6ea5, the 4074
`[sysmetrics]` BACKGROUND; also `LHSHELL_WALLPAPER=0`) through `feh --no-fehbg` (fill, fit, center, tile, stretch); at startup only
when no `_XROOTPMAP_ID` exists; never when another program owns the desktop (`LHSHELL_DESKTOP=0`, or another client's
`_NET_WM_WINDOW_TYPE_DESKTOP` window; flxp-desktop reads the wallpaper from the resolved record).

**Compositor follows the theme.** `Glass.composition` = auto (a compositor unless the theme has no glass frames) | 1 | 0
(`LHSHELL_COMPOSITION` forces one; lhde-session sets it from `LHDE_PICOM`). Off: SIGTERM to the
processes named picom whose environment has this DISPLAY (nothing else is touched). On: when nobody owns `_NET_WM_CM_S<screen>`,
start `[commands] picom`, else `$LHSHELL_PICOM_CMD`, else `picom --config session/picom-glass.conf --backend glx`. Applied 1.5 s
after startup, on every theme switch and when the switch changes. luxwm follows the compositor's presence by itself (glass ⇄
classic frames, `_LHWM_COMPOSITED`).

**Pack reload** (USAGE.md "Live-editing a theme"): `_LHSHELL_COMMAND reload`, or `_LH_THEME` written again with the current name;
at most one per 150 ms. It resolves the theme again, re-reads `shell.lua` (a pack that fails keeps the old one), applies the
fonts, flushes changed images (`lh.image_flush`), rebuilds bars, tiles (re-read from `tiles/*.lua`), flyout chrome and menus in
place, then the switch's theme steps. ~40 ms on the lab. `session/theme-reload.sh` sends it plus luxwm's reload;
`session/theme-watch.sh` calls it on file changes.

**Root commands.** Root property `_LHSHELL_COMMAND` (UTF8 string; luxshell deletes it after reading:
`xprop -root -f _LHSHELL_COMMAND 8u -set _LHSHELL_COMMAND logoff`): `run`, `reload`, `settings`, `taskbar avalon|classic|none`,
`sidebar-stacking top|bottom|toggle`, `sidebar-with-classic 0|1`, `desktop N|next|prev` (`_NET_CURRENT_DESKTOP`, 0-based), and
the session commands `logoff`, `standby`, `shutdown`, `restart`, `lock` from settings.ini `[commands]`, else: logoff =
`lhde-session stop` inside an lhde-session (its state directory exists), else `luxwmc quit` (luxwm exits cleanly, clients left
mapped); standby = `zzz` if present, else `loginctl suspend`; shutdown = `loginctl poweroff`; restart = `loginctl reboot`;
lock = `xlock` or `slock`. luxwm's keys send `shell CMD` this way (Super+R `run`, Super+S `sidebar-stacking toggle`).

## 15. Text (`text.c`)

Every `text` node is libuxtheme's GDI text, as luxnav's (docs/NAVBAR.md); no pango. Font descriptions are pango-style (`Segoe
UI 9`, `Segoe UI,Tahoma Bold 14px`, `ITC Franklin Gothic Std Medium 15px`) and become a `ut_font`: the first family that exists
(theme font directories first), weight words incl. Medium = 500, points at 96 dpi, lfHeight = -px rounded. Each line is one
`ut_draw_text_mask` (DT_SINGLELINE | DT_NOPREFIX, DT_END_ELLIPSIS for `ellipsis`) composited in the node colour at whole device
pixels; widths are GDI advances (`ut_text_extent_font`, cached); `wrap` is greedy at blanks with GDI widths (a long word breaks
between characters); `markup` keeps the menus' mnemonics (tags removed, the underline at GDI's underline rows under the
prefix's extent); `glow` = the coverage blurred (two box passes, `glow_size` 4, `glow_strength` 2.5) in the glow colour;
`shadow` = the coverage offset. The line box is the font's hhea ascent / descent at the size, rounded up: GDI's cell sits on
that baseline (equal to GDI's for Segoe UI and Tahoma; Franklin Gothic's winAscent is 2-3 px taller than its hhea ascent).
Characters the face lacks (`✓`, `●`, `▸` in Segoe UI 0.7) are drawn in fontconfig's face for them (GDI has no per-glyph
fallback). `LHSHELL_TEXT_TRACE=FILE` logs every painted text node (rect, block origin, baseline). Checked against pango's layout
of the same 60 nodes (task buttons, clock and power texts, menus, a wrapped toast): every node rect identical except one
auto-sized label (2 px, GDI's advance), every baseline within 0.5 px, so node sizes and baselines match pango's.

## 16. Context menus (`menu.lua`, `menupaint.c`)

Every menu (sidebar, tiles, the Avalon taskbar, the notification history) is drawn by wuxtheme's popup menu painter
(`lh.menu_render`: `ut_menu_layout` / `ut_menu_draw`, the pixels of flxp's menus and luxwm's window menu) in the session
theme: the style's Menu class where it has one (Aero / Jade POPUPITEM band, separators, check marks, cascade arrows), its
`[SysMetrics]` colours, FLATMENUS and MENUFONT; Windows Classic from the scheme's `Colors.*` and `Metrics.menufont`; smoothing
from `[fonts] smoothing`, else the theme's `Fonts.smoothing`; Marlett registered for the classic glyphs; XP's 4 px drop shadow
(luxwm's 4074 Slate measurement; without a compositor the screen under it, darkened, copied before the map). One image node per
level (`@menu:<depth>`), redrawn on selection, with a hoverable node over each item from the painter's layout. This is **not**
4074's Avalon ContextMenu, by choice (taste: menus look the same everywhere): `lh4074-091929.png` has a white plate, a 1 px
#919191 frame and a 1 px shadow, 19 px rows with an 18 px MENUHILIGHT bar whose top row is 30 % and bottom row 40 % lighter,
text 17 px from the frame, 7 px separators with a 1 px #BEBEBE line; the painter under Slate gives #F5F5F5, a #4E5358 frame, a
flat 19 px #EFC38C bar inset 3 px, text 22 px in, the same 19 px rows. Placement: `flip` (TrackPopupMenu: left of / above the
point where it would not fit, as the screenshot's menu), `up` (bottom-left corner at the point). Keyboard: `&` mnemonics,
underlined once the keyboard is used (opened from the keyboard: at once; 4074 drew none); a mnemonic key activates a unique
item or cycles through duplicates; other letters jump by initial; arrows, Home/End, Return, Escape, cascades.

## 17. Not done

From sections 2-3, not in luxshell: task grouping (glomming) and the group flyout, flashing task buttons
(`DEMANDS_ATTENTION` is read, not drawn), task tooltips, the task overflow chevron, tile drag-and-drop and the tile sizer drag
(*Auto Size* stays checked), the sidebar overflow row and expando, tile flash, minimised-window tiles (MWT/RMA), the white task
text animation, the "very large tile" consent, tile property sheets (`propsheet.xaml`; the clock's Properties is a flyout), the
clock flyout with alarm and calendar, the Basket, News Feed, Contacts, Search, Slide Show and Sync tiles, the secondary-monitor
taskbar, the Aurora desktop.

Validation: render the lab (`session/lab.sh`, Xvfb + picom xrender) and compare crops against the real screenshots at the same
coordinates (taskbar rows 994-1023 / 835-863, sidebar columns 1130-1279 / 1002-1151); `lab.sh shot` takes them.
