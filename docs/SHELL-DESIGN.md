> **Imported from the Longhorn X11 lab** (`~/fun/longhorn/x11` at d353b69, SHELL-DESIGN.md) into lhde in step 3 of docs/LAYOUT.md. Program and path names are updated to lhde (lhwm → luxwm, lhwmc → luxwmc, lhshell → luxshell, lhnav → luxnav; x11/... and lhwm/themes, lhshell/themes, lhshell/assets → the lhde tree and data directories, see docs/LAYOUT.md and tools/lhde-import); the X atoms (`_LH_*`, `_LHWM_*`, `_LHSHELL_*`), the window manager name "lhwm", luxnav's C API (`lhnav_*`, `lhnav.h`), configuration paths (`~/.config/longhorn`, `~/.config/lhwm/keys`, `~/.config/lhshell`) and setting values (`header_engine = lhnav`, `classic_start_menu = lhshell`) keep their names. Dated sections are history: they describe the x11 tree as it was (its compare_*.png and screenshots stay in the research workspace).

> **Naming (2026-10-25):** the Lua/cairo Avalon-style shell designed here is called **lux** (directory/binary still `luxshell`); the win32 layer under flxp is **wux**. See USAGE.md.

# Longhorn shell on Linux: taskbar + sidebar runtime design

Research document for the second phase of the Longhorn/X11 recreation: after `luxwm` (the window
manager, `luxwm/`) comes the *shell*: taskbar, start button, tray/clock, sidebar with tiles and
flyouts, for both the 4051 (Slate, classic taskbar + Avalon "start bar") and 4074 (Aero/Jade,
Avalon taskbar + sidebar) looks.

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
| `XP` | `/u/aap/lib/nt5src/Source/XPSP1/NT/shell/explorer/{tray,taskband,traynot,trayclok}.cpp` |
| `il` | `res/4074/il/desktop.il` (System.Windows.Desktop IL dump) |
| `sym` | `res/sym4051/explorer_syms.txt`, `res/sym4051/system_syms.txt` |
| `sdk` | `wim/sdk4051/lhsdk/LHSDK/shell/sidebar/`, `.../samples/shellcore/` |

---

## 1. The three shells we have to reproduce

| Build / theme | Taskbar | Sidebar | Where the imagery lives |
| --- | --- | --- | --- |
| 4051 Slate (screenshot `4051-vm-now.png`) | **classic Win32** `CTray`/`CTaskBand` drawn through the uxtheme classes `TaskBar`, `TaskBand`, `TrayNotify*`, but with the Avalon start button image (imageres 1156, bilinear; correction 2026-10-24, 2.1.2); 29 px tall (rows 835..863); tray clock hidden because the sidebar shows a clock (`CAvalonTaskBar::_ToggleClock`, COMPOSITION.md §11) | Avalon "start bar" (`startbar.cpp`, registry `Explorer\StartBar\Modules`), Slate look: flat dark grey, clock near the top | uxtheme `.msstyles` BITMAPs; the `Sidebar` class holds its 60 parts as `IMAGEDATA` BLOBs |
| 4074 Jade (default shipped look) | Avalon taskbar is available but the Jade `.msstyles` still carries the classic `TaskBar`/`TaskBand` bitmaps in Segoe UI 9 | Avalon sidebar, `Slate_*Resources` or `Aero_*Resources` dictionaries | `imageres.dll` PNGs 1000-1162 (Slate set) and 1167-1201 (Aero set), plus a few `theme://Sidebar,...` parts as `PNGSTREAM`s |
| 4074 Aero/DWM (screenshot `4074-dwm.png`) | Avalon taskbar, 30 px glass strip (imageres 1187), glass start button (1179), 140 px task buttons, round sidebar toggle | Avalon sidebar 149 px wide, 70 % black glass (1178), tiles separated by 1199 lines, clock tile pinned at the bottom | `imageres.dll` 1167-1201 |

The classic path and the Avalon path are two renderers over the same model (window list, tray,
clock, tiles). Section 6 proposes one runtime with two theme packs.

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
string or a logo-only `IDB_START` **(inferred)**. **Correction 2026-10-24:** the 4051 explorer.exe (and the 4051chk one) has no
bitmap 143 (`IDB_START`), 176 or 178 at all, and the start button of `4051-vm-now.png` is not the 335 pill: it is imageres
1156 (the Avalon `StartButton` image, 74x23, flag included) drawn 79x29 **bilinear** at the bar's top-left, which reproduces
the shot to a mean squared error of 0.2 per pixel (nearest 249). Only the task band (dark 146 px plates = 349) and the bar
background are the classic parts. See "4051 vs 4074 Slate" in the M6 status.

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
(luxshell's icon rule, 2026-10-24: `_NET_WM_ICON`, smallest image >= 16 scaled with cairo BEST, else the `WM_HINTS` pixmap + mask,
else the pack's `default_icon`, the shell32 application icon, as Explorer falls back to IDI_APPLICATION; re-read on every
`_NET_WM_ICON` / `WM_HINTS` PropertyNotify)
(Style TaskItemText = white, VerticalAlignment Center, TextWrap Wrap, TextTrimming
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

Note: early M4 builds had both bars with the taskbar running the full width and the sidebar ending at its top; `[sidebar] stacking = bottom` reproduces that (the sidebar yields to lux's own taskbar strut like to a foreign bottom dock) and, since 2026-10-25, also puts the sidebar under the windows like part of the desktop (`_NET_WM_STATE_BELOW`, no strut; Win+S brings it on top at once, USAGE.md "Stacking"); default `top` is the 4074 layout described here, above the windows with its strut.

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

Sizes measured on `4074-dwm.png` (**correction 2026-10-24:** the bar is **150 px**, x = 1130..1279; column 1130 is 1178's dark
column 0, not desktop, and every Slate reference starts it at 1152 - 150 = 1002, matching the IL default `Sidebar\Width` 150): sidebar x = 1131..1279 = 149 px, full screen height (the
taskbar yields to it); options row y = 0..16, separator at y = 17-18; tile separators (1199,
3 px) at y ≈ 198-199, 214/215-224, 309-310, 345 (the 2nd and 3rd tiles' header rows); clock
separator at y = 925-927; clock face 80 px (x ≈ 1170..1250, y ≈ 927..1004), centred on the bar;
time/date text "3:28 AM, Jan 9" at y ≈ 1004..1013. The tiles visible are a news-feed tile
(bulleted headlines), a quick-launch tile (two icons + "Search for..." box), a picture tile
(full-width sunset photo), then empty space down to the pinned clock.

On `4051-vm-now.png` (Slate): sidebar x ≈ 1003..1151 = **149 px**; options separator again at
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

Model consequence for the Lua tile API (4.3): a tile = `foreground` (interactive, no flyout
on click) + `background` (click opens the flyout), optional `flyout()`, `properties()`, context
menu items, `icon`, `tooltip`, `has_properties`, `flyout_link`; the host owns the menu, the
flyout window and the properties window, and calls `initialize()` after `sidebar` is set.

---

## 3. Behaviours to implement

### 3.1 Window list (both shells)

| Behaviour | Longhorn/XP reference | Linux mechanism |
| --- | --- | --- |
| Enumerate top-level windows in creation order | `CTaskBand` inserts on `HSHELL_WINDOWCREATED`, keeps order, group inserts next to siblings | root `_NET_CLIENT_LIST` (creation order; `_NET_CLIENT_LIST_STACKING` for z-order), `PropertyNotify` on the root |
| Skip taskbar-less windows | `WS_EX_TOOLWINDOW`, owned windows | skip `_NET_WM_WINDOW_TYPE` dock/toolbar/menu/splash/utility/dialog-with-transient, `_NET_WM_STATE_SKIP_TASKBAR`, windows with `WM_TRANSIENT_FOR` |
| Active (checked) button | `HSHELL_WINDOWACTIVATED`, `TB_CHECKBUTTON` | root `_NET_ACTIVE_WINDOW` |
| Minimised state (restore on click) | `IsIconic` | `_NET_WM_STATE_HIDDEN` in the client's `_NET_WM_STATE` (published by luxwm since M2, with `WM_STATE` Iconic; minimised is separate from berry's workspace "hide") |
| Click: activate or minimise if active | `_HandleClick`: minimise when already foreground | send `_NET_ACTIVE_WINDOW` client message (source 2 = pager) or `_NET_WM_STATE` add `_NET_WM_STATE_HIDDEN` / `XIconifyWindow` |
| Title / icon | `WM_GETTEXT`, `WM_GETICON` small 16 px | `_NET_WM_NAME` (UTF8) fallback `WM_NAME`; `_NET_WM_ICON` pick the 16x16 (luxwm's `frame_icon_from_netwm()` already does this) |
| Flashing (`TaskBand.FLASHBUTTON`, Avalon `TaskButton.Flash` 1183) | `HSHELL_FLASH` | `_NET_WM_STATE_DEMANDS_ATTENTION` |
| Grouping | `TaskbarGlomming`, `TaskbarGroupSize`, group when forced to `iMin` | group key = `WM_CLASS` class (XP uses the exe path); same rules: group of ≥ 2 only, GLOM_OLDEST default, ungroup when ≥ 2 open slots; the group button shows count + Marlett "u" (Avalon) or the DROPDOWNBUTTON part (classic) and opens a popup list (`TaskBandGroupMenu` parts) |
| Button width (classic) | `iIdeal = (W - fixed) / n` clamped to [`1.8*(cySize+2*edge)+textspace`, `cxMinimized`]; remainder to the first buttons | same; 160 max for the classic pack |
| Button width / rows (Avalon) | height 29 per row, rows `clamp(h/29, 1, 4)`; columns `max(1, w/130)`; width `min(140, w/itemsPerRow)`; overflow to the chevron below 60 px, pull back above 70 px (2.4.2) | the Avalon pack's list layout; measured 140 px pitch confirms |
| Group popup (Avalon) | `TaskGroupFlyout` MaxWidth 200, items = icon + title; drag-over opens a group after 250 ms, an item after 500 ms, the start button after 1000 ms | override-redirect popup above the button |
| Window context menu (Avalon) | End Task, Restore, Move, Size, Minimize, Maximize, Close (SC_* via WM_SYSCOMMAND, disabled by window styles); group: End Group Tasks, Cascade, Tile Vertically/Horizontally, Minimize/Restore/Close Group; taskbar: Task Manager, Show Desktop, Cascade, Tile, properties (2.4.2) | `_NET_WM_STATE` / `_NET_CLOSE_WINDOW` / `_NET_WM_MOVERESIZE`; cascade/tile done by luxwm on request (`luxwmc`) |
| Tooltips | toolbar tooltips with the full title | own tooltip window (ARGB) |
| Right click | window system menu (`TBC_SYSMENUCOUNT`) | our own menu: restore/minimise/maximise/close via `_NET_WM_STATE`/`_NET_CLOSE_WINDOW` |

### 3.2 Appbar placement, struts, autohide

| Behaviour | Reference | Implementation |
| --- | --- | --- |
| Reserve the screen edge | `SHAppBarMessage(ABM_SETPOS)`; stuck rect = edge strip of `_sStuckWidths` | `_NET_WM_WINDOW_TYPE_DOCK` + `_NET_WM_STRUT_PARTIAL` (bottom = 30 or 28; right = 149 with the full height); luxwm honours struts (`_NET_WORKAREA`) when maximising and placing since M2 |
| Taskbar yields to the sidebar | `SetMixedTaskbarSidebar`, screenshot: taskbar ends at x=1130 | taskbar strut `bottom_start_x..bottom_end_x = 0..W-149` and window width `W-149` when the sidebar is docked on the same screen |
| Autohide (classic) | 500 ms poll, hide when pointer outside rect + 4*edge and not active; show on pointer movement > dblclk distance after 50 ms; slide 400 ms out / 200 ms in, 20 ms frames; 2 px remain visible | timer + `XQueryPointer` (or `MotionNotify` on a 2 px ARGB strip left at the edge); animate `XMoveWindow`; update the strut to 2 px while hidden (XP keeps the hidden bar out of the work area too: `tray.cpp:2607-2612` "don't subtract the tray if it is autohide") |
| Autohide (Avalon) | driven by "focus within" (mouse over, flyout or popup open, tile focus); visible percent ± 20 every 20 ms = 100 ms linear slide; `2 + (size-2)*pct/100` px visible; a bar jumps to 0/100 when autohide is toggled (2.4.2) | same machinery with the theme pack choosing 100 ms linear vs 400/200 ms; focus-within = `EnterNotify`/`LeaveNotify` on the bar plus "a popup of ours is open" |
| Always on top | `ABS_ALWAYSONTOP` | `_NET_WM_STATE_ABOVE` (dock windows are above normal windows in luxwm anyway) |
| Persistence | `StuckRects2` TVSD, `Explorer\Sidebar\...` (see 2.4) | `~/.config/lhshell/state.ini`: edge, thickness, autohide, hideclock, tile list and heights |

### 3.3 The maximised-window black layer

`TaskbarBlackLayer` / `SidebarBlackLayer` are black `Border`s at opacity 0 directly under the glass
strips. `SetMaximizedWindow` animates them 0 → 1 over **400 ms** when a window becomes maximised
on that monitor and back when none is; the taskbar forwards the state to the sidebar (message
1388) and the task button text is animated to white over the same 400 ms (2.4.3). The native
detector is XP's "rude app" logic (`CTray::HandleFullScreenApp`, `CTaskBand::_FindRudeApp`).
Under luxwm:
subscribe to `_NET_WM_STATE` changes of all clients plus `_NET_ACTIVE_WINDOW`; "maximised" =
`_NET_WM_STATE_MAXIMIZED_VERT`+`HORZ` (or fullscreen) and not hidden, on the bar's monitor.
Render: paint the black rectangle with alpha `a(t)` under the 9-grid strip; with alpha 1 the
bar becomes opaque black and picom's blur is irrelevant (so the compositor cost drops too, as in
the DWM).

### 3.4 Sidebar tiles

| Behaviour | Reference | Notes |
| --- | --- | --- |
| Add / remove | `CSidebarWnd::_OnAddTile/_OnRemoveTile`, `CShellSidebar::AddTile/RemoveTile`; modules registered under `Explorer\StartBar\Modules` (4051) / `Explorer\Sidebar\Modules` (4074) with `Friendly Name`, `ShowInMenu`, `MultiInstance`, `Transient`, `IconOnly`, `MaxHeight`; instances persisted under `Sidebar\Tiles\{TileGUID}` and the order lists `SidebarTiles`/`SidebarPinnedTiles`/`TaskbarTiles`; new tile added by a third party → consent toast "New Sidebar Tile Added" Show/Hide (2.4.4) | runtime: Lua tiles in `~/.config/luxshell/tiles/*.lua` with the same metadata keys; "Options"/right-click menu lists Friendly Names ("Add a Tile"), tile menu = Auto Size, Move Up/Down, Remove, Properties |
| Lifecycle animations | fade in `TileUI` 800 ms; remove 400 ms; resize to a new height 400 ms ease-in-out; hide: 100 ms delay then 400 ms to 0; hover chrome 120 ms in / 80 ms out; flash 0.5↔1.0 with period 1000 → 100 ms (2.4.4) | `anim` nodes with easing 3 (ease-in-out) |
| Sizes | tile height min 30, default 30, max = remaining `sidebarMain` height or module `MaxHeight`; "very large" dialog forces 400 and asks; shrink deferred 30 s after a grow; width = bar width (150 default, 100..600) | tile `height` is a Lua-side request, the host clamps |
| Order and drag | `DragButton#TileContent`, `OuterTileBack`/`OuterContentHostBack` insertion markers (`PreviewPercent`, 800 ms snap, 10 s reset), `SBContentHostOverflow.DropTarget` highlights the overflow strip with `SDB_TASKBACK SELECTED` | pointer drag within the sidebar window; drop between tiles or on the overflow strip |
| Resize | `OuterContentHostSizer` 4 px thumb at each tile's bottom (double-click = Auto Size again); `SidebarSizer` 2 px (Aero) / 6 px (Slate) thumb on the inner edge changes the bar width (message 1457, clamp 100..600) | same |
| Overflow | a tile that does not fit (remaining height < 40) collapses to a 24x24 button (16 px icon, default imageres 1056) in `OverflowItems`; expando 1008/1009/1004; list auto-closes 1 s after the mouse leaves; hovering an overflow item previews it at 150 px for 4-10 s | |
| Minimise the bar | `SidebarControl.BarMinimized` (`SetMinimized`, reg `Sidebar\Minimized`): tiles and title hidden, only the taskbar-tile collection and overflow row (24 px) remain, the sidebar re-docks to the taskbar's edge as a strip of `_cxSidebar` px at the right end of the taskbar; toggle button glyph 1170 ↔ 1167 (2.4.2) | the taskbar's right-end strip in minimised mode |
| Minimised-window tiles (MWT) | `TaskData._CheckMWTs`: iconic windows can become sidebar tiles (`ShowMinimizedWindowTile`), header restore/close → `RestoreTask`/`CloseTask`; the "Rich Minimized Application" (RMA) hooks `TrackRMA`, `EnableRichMinimizedApp` | optional later: a minimised client's `_NET_WM_ICON` + title as a tile with restore/close |
| Flyout | `CreateFlyout` body (or the tile's own content moved into the flyout when there is none) in a `Popup` placed at the tile rect on the free side, `FlyoutContent` width 150 when placed on the flipped side; Aero frame 1175 grid 53,3,3,3 with the Options row (17 px) and border 8,4,8,8; the open flyout keeps an autohide bar visible (`ShowingFlyout`); optional bottom link (`FlyoutLinkText`) | separate ARGB override-redirect window positioned at the tile's y, to the left of a right-docked bar; closes on focus loss / Esc / click outside |
| Clock tile | pinned module, `Sidebar\Clock` DWORD; views in 2.3.4; alarm; calendar flyout | Lua tile shipped with the runtime |
| Notifications | `notification.xaml` toast: 205x60 frame 4002 with a countdown bar (4001), close button, Options; bottom-right of the work area with a 10 px margin, round-rect radius 9, 10 s timeout (min 5), expands on hover over 200 ms, body text wrapped at 110/154/220, ≤ 2 buttons 73x24 (2.4.5) | toast window at the work-area corner; wire to `org.freedesktop.Notifications` later |

---

## 4. Proposed architecture: `luxshell` (C + cairo + Lua)

### 4.1 Processes and windows

```
luxwm (WM)  ── EWMH root properties ──▶  luxshell (one process)
                                         ├─ taskbar window   (ARGB, _NET_WM_WINDOW_TYPE_DOCK, strut bottom/top/left/right)
                                         ├─ sidebar window   (ARGB dock, strut right/left, full height)
                                         ├─ popup windows    (override-redirect ARGB: start menu, group menu, flyouts, tooltips, toasts)
                                         └─ Lua VM           (theme pack + tiles + behaviour scripts)
picom: blur-background for the ARGB docks (already configured in session/picom-glass.conf)
```

One process keeps the model (window list, tray, clock, tiles) in one place and lets the taskbar
yield to the sidebar without IPC. Each top-level is a `struct surface` with an X window, a
cairo xlib surface (32-bit visual) and a scene root. Later, the same `surface` type gets a
`wl_surface` + `zwlr_layer_surface_v1` back end (anchor edge, exclusive zone = strut,
`keyboard_interactivity` none for bars, `on_demand` for the start menu); nothing above the
surface layer needs to know.

### 4.2 Scene graph node types

The XAML in 2.2-2.3 reduces to a small set of node kinds. Every node has `x,y,w,h`, `opacity`,
`visible`, `clip`, `dock` (left/top/right/bottom/fill), `size` (fixed, percent, auto),
optional `margin` (Border thickness), children.

| Node | Models | Paint |
| --- | --- | --- |
| `panel` | `DockPanel`, `FillPanel`, `FlowPanel`, `Canvas` | layout only; `dock` layout = WPF DockPanel rules; `fill` = all children get the full rect; `flow` wraps |
| `ninegrid` | `HackImage`/`HackBorder` with `HackNineGrid`, uxtheme `IMAGEFILE` + `SIZINGMARGINS` (`STRETCH`/`TILE`), `HackFlipHoriz`, `TRUESIZE` | cairo: 9 `cairo_set_source_surface` + `cairo_pattern_set_extend(REPEAT)` for TILE; strips with `IMAGECOUNT n` are sliced by state index |
| `image` | `Image`, `ResImage` 16 px icons, clock faces | scaled surface |
| `text` | `Text` with font, colour, wrap, `CharacterEllipsis`, shadow (`TEXTSHADOWOFFSET`/`ShadowText` 1,1), glow (`TextGlow.BACKGROUND` 21x21 grid 10 as in luxwm's caption) | libuxtheme's GDI text (`text.c`, since 2026-10-27; pangocairo before); glow = the coverage blurred under the text, shadow = offset copy |
| `rect` | `Border Background`, black layers, clock hands, polygon second hand | cairo fill / path |
| `button` | `Button`, `AreaButton`, `DragButton`, `StartButton`, `TaskButton`, `TileThumb` | state machine: normal/hot/pressed/checked/hotchecked/flash/disabled → picks a child or a strip index; press-and-release-inside semantics (luxwm already has these for caption buttons); `drag` emits move deltas (thumbs, tile drag) |
| `transform` | `TransformDecorator`, `ScaleDecorator DesignSize`, `AnalogClockLayerDecorator Origin` (rotate about origin) | `cairo_save/translate/rotate/scale` |
| `list` | `TaskOverflowableCollection`, `SBContentHostCollection`, `OverflowPanel` | data-bound: item template function + width/height rules (3.1) |
| `timer` / `anim` | `IDT_*` timers, `AnimatePropertyTo` (opacity, width, height, text brush), slide 400/200 ms (classic) or 20 ms steps (Avalon), black layer 400 ms, tile fades 800/400/120/80 ms, clock tick | `timerfd`/poll-based scheduler; `anim(node, prop, to, ms, easing, delay)` with the four `Animator2` easings (linear, ease-out `2t-t^2`, ease-in `2t^2`, ease-in-out) at a 20-25 ms tick, marking the surface dirty each frame |
| `popup` | `Popup` (group flyout MaxWidth 200, tile flyout, context menus, tooltips, toast) | override-redirect ARGB top-level with placement rect + side, close-on-click-outside (`CloseMode 2`), keeps the owning bar's "focus within" |

Dirty tracking per surface, repaint only the union rect, `XFlush` after paint (lesson from luxwm).

### 4.3 Lua API sketch

```lua
-- theme pack: themes/aero/shell.lua
local T = theme "aero"
T.font        = "Segoe UI 9"
T.taskbar     = { height = 30, bg = ninegrid("imageres/1187.png", {0,7,0,0}),
                  black_layer = true }
T.start       = { w = 83, h = 29, margin = {0,0,4,0},
                  img = states("imageres/1179.png", "imageres/1180.png", "imageres/1181.png") }
T.taskbutton  = { max_w = 140, border = {0,2,2,0}, pad = {6,5,6,6}, grid = 3,
                  img = { normal="imageres/1182.png", hot="imageres/1184.png",
                          pressed="imageres/1185.png", checked="imageres/1185.png",
                          flash="imageres/1183.png" },
                  text = { color = "#ffffff", ellipsis = true }, icon = 16, icon_gap = 2 }
T.sidebar     = { width = 149, bg = ninegrid("imageres/1178.png", {36,0,0,0}),
                  separator = image("imageres/1199.png", 3), options_h = 17 }
T.flyout      = { bg = ninegrid("imageres/1175.png", {53,3,3,3}), border = {8,4,8,8} }

-- classic pack: themes/slate4051/shell.lua, same keys, uxtheme-style strips
T.taskbar    = { height = 28, bg = strip("slate51/266.bmp", margins={0,0,15,12}, mode="tile") }
T.taskbutton = { img = strip6("slate51/349.bmp", margins={4,4,16,12}, content={0,4,4,4}),
                 max_w = 160, min_w = function(h) return math.floor(h*1.8) end,
                 font = "Tahoma 8", text = { color="#ffffff" } }
T.start      = { img = strip3("slate51/335.bmp", margins={6,13,4,2}, content={7,19,2,4}),
                 font = "Franklin Gothic Medium Italic 14", text="start",
                 shadow = { dx=2, dy=2, color="#4b4b4b" }, logo = "start.png" }
T.tray       = { bg = strip("slate51/360.bmp", margins={13,10,14,13}, content={19,7,0,0}),
                 chevron = strip3("slate51/359.bmp"), clock = { font="Tahoma 8", color="#ffffff" } }

-- a tile: ~/.config/luxshell/tiles/clock.lua   (mirrors BaseTile, 2.5)
local tile = Tile { friendly_name = "Clock", guid = "b6852cff-6f7c-4a39-b56b-6c9d40679adb",
                    height = 105, pinned = true, has_properties = true, tooltip = "Clock",
                    icon = "imageres/1056.png", multi_instance = false }
function tile:initialize()                       -- BaseTile.Initialize: sidebar is set, tile not yet shown
  self.face = self.background:add(AnalogClock { size = 80, face="imageres/1074.png",
                                                glass="imageres/1076.png", anchor="imageres/1083.png",
                                                second_color = "#6db8cf" })     -- background: click opens the flyout
  self.label = self.foreground:add(Text { style = "digital", format = "%-I:%M %p, %b %-d" })  -- foreground: interactive
  shell.every(1000, function() self.face:set(os.time()); self.label:set(os.date(self.label.format)) end)
  self.sidebar.on_context_menu = function(menu) menu:add("Clock faces...", function() ... end) end
end
function tile:create_flyout() return ClockFlyout(self) end        -- CreateFlyout
function tile:create_properties() return PropSheet { ... } end    -- CreateProperties (OK/Cancel 84x24)
function tile:dispose() shell.cancel(self.timer) end               -- Dispose
-- host-provided: self.sidebar:show_flyout(elem), :close_flyout(), :close_properties(), :remove_me()

-- behaviour hooks exposed by the runtime
shell.windows             -- array of { id, title, icon, active, hidden, demands_attention, class }
shell.on("window-added" | "window-removed" | "window-changed" | "active-changed", fn)
shell.on("maximized-changed", function(any_maximized) bar.black:animate("opacity", any_maximized and 1 or 0, 400) end)
shell.activate(id); shell.minimize(id); shell.close(id)
shell.timer(ms, fn); node:animate(prop, to, ms)
```

The C core owns: X11/Wayland surfaces, EWMH model, cairo painting, input routing, timers,
image cache (BMP with alpha and PNG via cairo/`stb_image`; the `.bmp` strips are 32-bit with
alpha, `identify` reports `srgba`). Lua owns: which nodes exist, their styles, tile logic, menus.
Theme packs are directories of the extracted PNG/BMP files plus `shell.lua`, mirroring
`data/frames/<name>/theme.ini`; the `id → file` tables from `mock/assets/resmap.json`
(`aero_to_slate`) give the Slate pack for free.

### 4.4 Theme switching matrix

| Pack | Taskbar renderer | Sidebar | Frames (luxwm) |
| --- | --- | --- | --- |
| `slate4051` | classic strips from `slate51` (28 px, Tahoma, pill start button, tray chevron, clock hidden when the sidebar has a clock) | Avalon with `Slate_*Resources` ids (1000/1155...) and the 4051 Sidebar class PNGs for the clock plate (BLOB 404/405) | Slate frame parts (not yet in luxwm) |
| `jade4074` | Avalon with `Slate_*Resources` (Jade shipped the Slate dictionaries plus `theme://Sidebar,...` streams 507-566) or the classic Jade strips (Segoe UI 9) | Avalon Slate look | `themes/jade` |
| `aero4074` | Avalon with `Aero_*Resources` (1167-1201), glass | Avalon Aero (1178, 1175, 1199) | `themes/aero` |

Both renderers are Lua over the same node kinds; the classic pack sets `renderer = "classic"`
which selects the tray/clock-in-bar layout and the uxtheme strip semantics (`IMAGECOUNT`
slicing, `SIZINGTYPE`), the Avalon pack selects dock-panel layouts with `HackNineGrid`.

---

## 5. Implementation plan

| Milestone | Deliverable | Depends on |
| --- | --- | --- |
| **M0 asset pack** | `tools/shellpack.py`: copies the ids in 2.1-2.3 into `data/packs/{slate4051,jade4074,aero4074}/`, converting `.bmp` strips to per-state PNGs (6/3 slices) and writing `assets.lua` with sizes and margins straight from the `.ini` tables above | nothing |
| **M1 core** | `luxshell` binary: ARGB dock window with strut, cairo 9-grid/strip painter (reuse `luxwm/frame.c` loader and TextGlow), Lua VM, scene nodes `panel/ninegrid/image/text/rect/button`, timers, hot/pressed states; renders the Aero taskbar background + start button + sidebar background statically | M0 |
| **M2 window list** (done) | EWMH model (`_NET_CLIENT_LIST`, `_NET_ACTIVE_WINDOW`, `_NET_WM_STATE`, names, icons); task buttons with the width rule of 3.1, click to activate/minimise; luxwm publishes `_NET_WM_STATE_HIDDEN`, `_NET_CLIENT_LIST_STACKING`, honours `_NET_WM_STRUT_PARTIAL`/`_NET_WORKAREA` when maximising and treats dock windows as above | M1 |
| **M3 classic renderer** | `slate4051` pack: 28 px bar, `Start::Button` strip + "start" text/logo, task strips with CHECKED state, `TrayNotify` background, chevron bubble, clock (`%H:%M` per locale, no seconds, minute timer), group menu parts; compare against `4051-vm-now.png` crops | M2 |
| **M4 sidebar + tiles** | sidebar dock (149 px), options row, separators, `SBContentHostCollection` with `OuterTile` chrome (side strip 9 px, arrow, restore/close on hover, 4 px sizer), pinned bottom host, Lua `Tile` API (foreground/background/flyout/properties/menu), clock tile with the 80 px analog face and digital line, taskbar yielding to the sidebar | M1, M2 |
| **M5 behaviours** | black layer 400 ms on maximise (+ white task text), autohide (classic 500/50 ms timers and 400/200 ms slides; Avalon focus-within and 100 ms slide; 2 px sliver, strut update), grouping with the XP rules (classic) and the 29/130/140/60/70 rules (Avalon) plus the group popup, flashing (`DEMANDS_ATTENTION`), tooltips, right-click window menu, state persistence (`state.ini` mirroring `Taskbar\{StuckPlace,AutoHide,RowCount,Width}` and `Sidebar\{StuckPlace,AutoHide,Minimized,Width,Clock}`) | M2-M4 |
| **M6 flyouts, overflow, toasts** | flyout windows (1175 frame, Options row, 150 px content), tile drag/reorder with the insertion markers (800 ms snap), overflow row with expando (1 s auto-close, previews), `BarMinimized` strip at the taskbar's end, tile fade/resize/flash animations, notification toast (4002/4006 frame, countdown bar, 10 s, hover expand), `org.freedesktop.Notifications` bridge | M4, M5 |
| **M7 start menu** | classic `StartPanel` parts (USERPANE, PROGLIST, PLACESLIST, LOGOFF) in the Slate pack; Avalon start menu is out of scope until its XAML (`StartMenu` class) is decoded | M3 |
| **M8 Wayland** | `surface` back end on `wlr-layer-shell` (anchor + exclusive zone = strut, margins), input via `wl_pointer`; same Lua packs | M1-M6 |

**M2 status.** luxwm (`wm.c`, details in `docs/LUXWM.md` "EWMH and the shell"): `_NET_CLIENT_LIST` in creation
order, `_NET_CLIENT_LIST_STACKING`, `_NET_WM_STATE` HIDDEN / MAXIMIZED_VERT+HORZ / DEMANDS_ATTENTION (+ urgency hint) /
FOCUSED with add/remove/toggle client messages and `WM_CHANGE_STATE`, minimised as a state of its own, struts of
unmanaged docks → `_NET_WORKAREA` (1131x770 on the 1280x800 lab with the Aero taskbar and sidebar), maximise / snap /
centring / smart placement inside the work area, docks above clients (unless `_NET_WM_STATE_BELOW`), dialogs and
transients centred on the parent, `USPosition`/`PPosition` with window gravity, `_NET_ACTIVE_WINDOW` switching the
desktop (berry's `XSync(dpy, True)` in `switch_ws` dropped such messages). luxshell: windows carry `maximized`,
`fullscreen`, `stacking` (index in `_NET_CLIENT_LIST_STACKING`; task order stays `_NET_CLIENT_LIST`), transients get no
button, icons fall back from `_NET_WM_ICON` to `WM_HINTS` icon_pixmap/icon_mask (xterm, xclock) and then to the pack's
`default_icon` (none defined yet), `lh.desktop()`; the black layers follow 2.4.3: on iff the active window (or, with
none active, any visible window on the current desktop) is maximised or fullscreen, 400 ms fade. Struts verified:
taskbar bottom 30 over x 0..1130, sidebar right 149 over the full height. Fixed on the way: `emit()` called its first
argument instead of `lh._emit` for events with arguments (`window`, `surface-press` never reached Lua), and a
rebuilt task list left the surface's hot/pressed node dangling (crash on the next click). The 400 ms white task text
(2.4.3) stays in M5.

Validation at each milestone: render the lab (`session/lab.sh`, Xvfb + picom xrender) and diff the
crops in the scratch directory against the real screenshots at the same coordinates (taskbar
rows 994-1023 / 836-863, sidebar columns 1131-1279 / 1003-1151).

## Corrections from the luxshell rendering pass

- **Two clock face sets.** `analogclock.xaml` names 1074/1076 (light grey face, black XAML rectangle hands), but the Aero/Jade 4074 screenshot shows the **dark-blue set**: face 1190 (263x263; 1192 is a byte-identical copy), glass 1191 (265x265), hands as images: hour 1193 (11x122, shadow 1194 12x103), minute 1195 (11x151, shadow 1196 12x133), second 1197 (11x163, shadow 1198 15x136), anchor 1188/1189 (11/12 px). The light set is what Slate shows (4074 Slate screenshots). Hand pivots are not in the XAML; luxshell uses estimates (tail ~22 design px for hour/minute, ~35 for the second hand).
- **Sizes of `HackImage` slots include the margins of the wrapping Border.** E.g. options glyph: image 9x6 plus Border 4,7,2,2 is a 15x15 slot; the TileArrow 8x8 with Border 0,2,1,0 is 9x10.
- Slate clock hands are thin dark grey (#3E3F3F-ish, ~1 px at 80 px), not thick black; anchor is small.
- `picom-glass.conf`: the dock window type was not in `blur-background-exclude` (it is in the shadow and rounded-corner excludes), so docks already blur.

## M5a design: the maximised look, the desktop layer, and running without a compositor

Status 2026-10-23: implemented (luxwm `_LHWM_MAXIMIZED_MODE`, square DWM maximised frames, `fallback_theme` + `_LHWM_THEME`; luxshell desktop layer; `LAB_PICOM=0`), verified on the lab; details and deviations in `docs/LUXWM.md` "Maximised mode".

Status update 2026-10-23 (M4b pass, black layer timing). aap: on maximise the black "pops in eventually", on restore it
"blends out". New behaviour: the **desktop layer switches instantly both ways**: when `_LHWM_MAXIMIZED_MODE` turns on, luxshell
sets the desktop window's X background to opaque black and maps it (lowered first), so the server paints it black as part of
the map, no transparent first frame and no fade; on restore it is unmapped at once. It is now a **24-bit (opaque) window**:
it is only ever mapped while black, and picom treats every ARGB window as translucent (full-screen blur behind it on every
frame); the Aurora will need ARGB again. The **taskbar and sidebar black layers keep the 400 ms Animator2 fade** both ways
(2.4.3), now at Animator2's 25 ms tick with the first frame applied in the same repaint as the trigger. Why it was late,
measured with a 10 ms pixel sampler and a 50 ms full-screen series against `LHSHELL_DEBUG=1` timestamps (both CLOCK_MONOTONIC):
(1) luxshell's poll loop could sleep on X events already read into Xlib's queue by round trips in Lua handlers and cairo (up
to the next timer, the clock's 1 s); it now polls with timeout 0 while `XEventsQueued(QueuedAlready)` is non-zero. After the fix
the `maxmode` event reaches Lua 1-20 ms after the property change and the first frame is painted 2-10 ms later. (2) Every fade
frame repainted the full-screen desktop layer and the sidebar with server-side `FILTER_BEST` resampling of the 263 px clock
layers; images are now prescaled once on the client (sidebar repaint 10 ms → 2 ms idle) and leaf rect/image opacity no longer
pushes a group. (3) What remains is the compositor: on the Xvfb lab picom (xrender, gaussian blur) presents no frame for
~300-450 ms around luxwm's geometry change, so the series shows the maximised window, the black desktop and the (already
finished) black bars appearing in the same composited frame (t = 651 ms after the request, maxmode event at 203 ms), and on
restore the desktop black disappearing with the first frame picom shows (t = 601 ms, event at 126 ms) while the bars fade.

Written 2026-10-23 after M2. aap's description of the original: when a window is maximised,
Longhorn puts a **black background behind the window**, the frame's **rounded corners disappear**,
and the shell's black layers (taskbar/sidebar, 2.4.3) come on; this also holds for the non-DWM
Jade/Aero (msstyles) frames. For the XP-era classic frames the corners are rounded or square
depending on maximisation and window type (MAXCAPTION / SMALLCAPTION parts), and no compositor is
needed or wanted.

### Shared condition
"Maximised mode" on a desktop = the active window is maximised or fullscreen, or, with no active
window, any visible window on that desktop is. luxshell already computes this for the black layers
(runtime.lua, `lh.set_maximized`); luxwm knows it from its own state. Both must switch at the same
moment, so luxwm publishes it too: root property `_LHWM_MAXIMIZED_MODE` (CARDINAL 0/1 per desktop
index, like `_NET_WORKAREA`), updated whenever maximise/restore/focus/desktop changes. luxshell
reads that property when present and only falls back to its own EWMH-derived condition when luxwm
is not running (another WM).

### The desktop layer (luxshell)
A third luxshell surface, `desktop`: full screen, 32-bit ARGB, `_NET_WM_WINDOW_TYPE_DESKTOP`,
STICKY + BELOW, no struts, mapped first so it is at the bottom of the stack. Normally fully
transparent (the wallpaper stays the root background, set by the user or lab.sh). In maximised
mode it fades to opaque black in 400 ms with the same Animator2 ease as the black layers and fades
back on restore. It is also the future home of the Aurora desktop (`xaml_sidebar_desktopaurora.xaml`,
`mock/aurora.svg`), so give it a scene root like the other surfaces. picom: exclude
`window_type = 'desktop'` from blur, shadow and fading in `picom-glass.conf`. luxwm must leave
DESKTOP windows unmanaged and never raise anything below them (berry already ignores them; verify
`XRestackWindows` keeps them at the bottom after `restart-wm`).

### luxwm frames when maximised
- **DWM themes (aero, jade):** square corners. Draw the plate's four corner cells from the
  adjacent straight-edge cells instead of the rounded corner pixels (i.e. the 9-slice with the
  corner sources replaced by the edge sources), draw the overlay likewise, and set the shape
  region to the full rectangle. The frame keeps its caption height and side margins but the
  side and bottom borders lie outside the work area, as the classic MAXCAPTION path already does
  (README "Classic msstyles mode"), so the client fills the work area exactly. Check for any
  maximised window in `screenshots/` (4074 Slate shots `lh4074-18*.png`, `4051-vm-now.png`) to
  confirm whether the caption's top corners are square or clipped by the screen edge, and note
  what you find in the README.
- **msstyles themes:** already MAXCAPTION with the borders off-screen; verify the shape is a
  full rectangle in that state and that restore brings the rounded corners back. SMALLCAPTION
  (tool windows, `_NET_WM_WINDOW_TYPE_UTILITY` / MWM tool hint) rounded/square rules are a
  later item.
- Shadow (`shadow.png`, LGFRAMESHADOW) is not drawn while maximised; picom shadows for the
  maximised client are moot (fills the work area) but should be excluded via a `_LHWM_MAXIMIZED`
  window property + picom rule to avoid a shadow strip over the taskbar.

### Running without a compositor
Classic frames need none. DWM frames do: without a compositor an ARGB decoration window over the
root shows its translucent pixels as black. Rules:
- `lab.sh`: `LAB_PICOM=0` starts the lab without picom (`start`, `ensure`, `restart-picom` print
  a note instead); `docs/USAGE.md` documents it next to `LAB_THEME`.
- luxwm: on startup and on `_NET_WM_CM_S<screen>` selection owner changes, if the current theme
  is a DWM theme (has `plate_active.png`, `mode = dwm`) and no compositor owns the selection,
  switch to the theme's `fallback_theme` (theme.ini key; aero → `jade4074-classic`, jade →
  `jade4074-classic`) and back when a compositor appears. This mirrors Longhorn's own
  `USESCOMPOSITION` fallback to the msstyles frame. Log the switch to stderr. Theme switching at
  runtime = reload theme, recompute margins for every client, re-place clients so the client
  rectangle stays put, repaint.
- `luxwmc` or a keybinding to switch theme at runtime is a natural by-product; expose
  `_LHWM_THEME` root property (string) that luxwm sets and honours when changed by
  `xprop -root -f _LHWM_THEME 8s -set _LHWM_THEME <path>`.

### Order of work
1. luxwm: `_LHWM_MAXIMIZED_MODE`, square-corner DWM maximised frames, no shadow when maximised.
2. luxshell: desktop surface + black fade; read `_LHWM_MAXIMIZED_MODE`.
3. lab.sh `LAB_PICOM=0`; luxwm compositor detection + fallback theme + `_LHWM_THEME`.
Verify on the lab: maximise (Super+Up) → desktop fades to black with the bars, corners square,
client fills 0,0..workarea; restore → everything back. With `LAB_PICOM=0 LAB_THEME=aero`, frames
come up in jade4074-classic and `xprop -root _LHWM_THEME` says so.

## M4b: notification area, toasts, power tile

Written 2026-10-23. Three sidebar-side pieces that make the bar "alive", plus `_NET_WM_ICON_GEOMETRY` for luxwm.

### Tile chrome correction
The right-docked sidebar is Flipped (2.4.1), so `TileSideAreaBack` docks **Right** and `TileForegroundPadder` (6) **Left**
(2.3.3). runtime.lua had them the other way round. With the fix the foreground is x 7..140 of the bar (133 wide) and y 3.. of
the tile, which is exactly where the 4074 Slate power tile's battery starts (`lh4074-181415.png`, x 1010 on a bar at 1003) and
where the 4051 arrow sits (top-right, `4051-vm-now.png`). The clock moved 3 px left (centre 75 on a 149 px bar). Tiles may
define `flyout(self) → node, w, h`; a click on the foreground then toggles the generic flyout (below). `sb.resize_tile(tile, h)`
animates a tile to a new height in 400 ms ease-in-out (2.4.4), min 30.

### `_NET_WM_ICON_GEOMETRY`
Task buttons carry `icon_geometry_for = <client>`; after every layout of a surface, luxshell writes the button plate's rect
(inside its 0,2,2,0 margin: e.g. `83, 772, 138, 28`) in root coordinates on the client, rewrites it when the buttons reflow and
deletes it when the button goes away (skip-taskbar, window gone). Verified with `xprop` on the lab.

### Notification area (system tray) tile, `tiles/notifications.lua` + `tray.c`
| Item | Value |
| --- | --- |
| Protocol | freedesktop System Tray 0.3 + XEmbed: luxshell owns `_NET_SYSTEM_TRAY_S<screen>` with an InputOnly manager window (server timestamp from a property append), announces `MANAGER` on the root, sets `_NET_SYSTEM_TRAY_ORIENTATION` = 0 (horizontal), `_NET_SYSTEM_TRAY_VISUAL` = our 32-bit visual, `_NET_SYSTEM_TRAY_ICON_SIZE` = 16 |
| Docking | `REQUEST_DOCK`: select Structure/PropertyChange, `XAddToSaveSet`, reparent into the sidebar surface, size 16x16, send `XEMBED_EMBEDDED_NOTIFY` (embedder = sidebar, version 0), map per `_XEMBED_INFO` flags (no property: map). Unmap/destroy/reparent-away undock; `_XEMBED_INFO` changes map/unmap; an icon resizing itself is put back to the slot size. BEGIN/CANCEL_MESSAGE balloons are ignored |
| Alpha | an ARGB child would replace the glass under it (transparent icon pixels = holes). With Composite + Damage present, each icon is **redirected manually** and painted by luxshell with cairo OVER from the redirected window in the `tray` node's paint (Damage → repaint); input still goes to the icon window. The libXcomposite/libXdamage headers are not installed here, so the five entry points are `dlopen`ed (without them icons draw themselves) |
| Exit | on SIGTERM/SIGINT the icons are unmapped and reparented to the root before exit and the selection is dropped. Otherwise the save-set maps them on the root, luxwm manages them and puts a passive button grab on them; after the next luxshell re-docks them a click froze the pointer (an orphaned sync grab, observed on the lab) |
| Geometry (measured) | the only references are the 4051/4074 quick-launch icon tiles (`4051-vm-now.png` y 19..51, icons 16x16 at x 1015 / 1043, y 28; `lh4074-181415.png` x 1017 / 1043, y 108): first icon at 5,6 in the foreground (12,9 in the tile), **28 px pitch** across (5 per row in 133 px), tile height **33** for one row incl. the 4 px sizer; rows 24 px apart (inferred, no multi-row reference) |
| Lua | node `kind = "tray"` with `icon_size`, `pitch = {x, y}`; `lh.tray_start(node)`, `lh.tray_count()`, event `tray(count)` → the tile resizes (`33 + 24 * (rows - 1)`) |

Tested with `tests/trayicon.c` (ARGB circle, re-docks on `MANAGER`, logs clicks, optional exit timer): 7 icons → two rows,
the 5 timed ones exiting → one row again, clicks reach the icon, icons survive luxshell restarts. `blueman-tray` is installed but
needs a running blueman-applet/bluetooth; GTK2/3 `GtkStatusIcon` apps and anything using the XEmbed fallback dock the same way.

### Toasts: `org.freedesktop.Notifications` (`notify.c`) + `toast.lua`
| Item | Value |
| --- | --- |
| Server | libdbus-1, session bus from `DBUS_SESSION_BUS_ADDRESS` only (no X11 autolaunch); `RequestName(DO_NOT_QUEUE)`, if another daemon owns the name toasts are off (logged). Fd in luxshell's poll, dispatched every loop. Methods `GetCapabilities` (body, actions, icon-static), `GetServerInformation` (luxshell, Longhorn X11 lab, 0.1, 1.2), `Notify` (`susssasa{sv}i` → id; `replaces_id` keeps the id), `CloseNotification`, Introspect; signals `NotificationClosed` (1 expired, 2 dismissed, 3 closed by call) and `ActionInvoked` |
| To Lua | event `notification` with `id, replaces_id, app_name, app_icon, summary, body, actions = {{key, label}}, hints, urgency, image_path, image` (`image-data` hint → `@notify:<id>` image), `expire_timeout`; `notification-close(id)`; `lh.notification_closed(id, reason)`, `lh.notification_action(id, key)` |
| Window | override-redirect ARGB, `_NET_WM_WINDOW_TYPE_NOTIFICATION`, round-rect bounding shape radius **9** (XShape), never focused |
| Layout (notification.xaml) | ToastBackground black (opacity 0 under Aero = glass, 1 in Slate); 4002 frame grid 7 (205x60 is the minimum size); TimerControl column 5 px on the right, 4001 bar shrinking from the top as time runs out; options row 15: close 14x15 4003/4004/4005, "Options" white; icon left (image-data, image-path, app_icon path or icon-theme PNG; scaled to <= 48); 4006 panel grid 7, border 6: summary ITC Franklin Gothic Std Medium 15 px (Franklin Gothic Medium 15), body 12 px, wrapped at 110, else 154 if <= 2 lines, else 220 (<= 4 lines); action buttons 73x24 (<= 2, hover 1110 / pressed 1111) |
| Behaviour (2.4.5) | 10 s default, minimum 5 s (`expire_timeout` 0 = until dismissed); the bar ticks every `timeout / height` ms; hover stops the timer, rewinds to 0.75 in 200 ms and expands (buttons row + Options fade in, 200 ms, the window grows downward into the room reserved by the placement); leaving collapses and restarts; one toast at a time, FIFO; click anywhere but the buttons = `default` action (if offered) + dismissed; close glyph = dismissed |
| Placement | IL: `x = work.right - w - 10`, `y = work.bottom - (expanded - collapsed) - h - 10` (work area = screen minus the taskbar and sidebar struts): above the taskbar, left of the sidebar. `T.toast_place = "sidebar"` puts it over the sidebar's top instead |

Verified with `notify-send "Hello" "from Longhorn"`, `notify-send -A ok=OK -A no=Cancel ...` (button → `ActionInvoked(2, 'ok')`,
`NotificationClosed(2, 2)`, notify-send prints `ok`), `-t 5000` (closed with reason 1 after 5 s), `CloseNotification` (reason 3),
`replaces_id` (rebuilt in place), a queue of three, and a body click with a `default` action. No reference screenshot of a 4074
toast exists; the look follows the XAML and the 4002/4006 images only.

### Power tile `tiles/power.lua` and the generic flyout `flyout.lua`
| Item | Value |
| --- | --- |
| Data | `/sys/class/power_supply/BAT0` (`status`, `capacity`, `energy_now/full` + `power_now`, or `charge_*` + `current_now`), `AC/online`; polled every **30 s**; time left = now / rate (discharging) or (full - now) / rate (charging). `LHSHELL_POWER_SUPPLY` / `LHSHELL_BATTERY` point it elsewhere (a fake sysfs dir is how the "Charging: 95%" comparison was made) |
| Drawing (powergauge.xaml) | the XAML uses **no imageres ids**: it is vector brushes. Battery (`battery_body` 35x19 ShinyWhite, `nub` 3x10 ShinyGray, red `plus_h/plus_v/minus`, `ACLogo` black plug) rotated 270° and scaled 0.95, as measured in `lh4074-181415.png`: body x 7..24, y 7..41 of the tile, nub 9x3 on top; the ACLogo pulses 0 → 1 → 0 over 2 s while charging on AC. Gauge: `#808080` box 104x19 at x 33, y 6 (gauge panel Height 21), ShinyGreen (ShinyYellow <= 25 %, ShinyRed <= 10 %) bar inset 2 px over a 40 % copy of itself, GraySweep at 15 %. Text Segoe UI 13 bold `#dddddd` centred under the gauge (Helvetica Bold 13 px here: 89 px for "Charging: 95%" vs 86 in the reference); "Charging: N%", "XhMM left: N%" (the XAML sample text is "4h23"), "Charged: N%" |
| Tile | 49 high (45 body + 4 sizer), 2nd in the default list after the tray, as in the 4074 Slate shot |
| Flyout (flyout.xaml, 2.3.2) | override-redirect ARGB `_NET_WM_WINDOW_TYPE_POPUP_MENU`: 1175 nine-grid 53,3,3,3 (Slate 1109 grid 3), options row 17 (glyph 1176/1177 in a 4,5,4,4 border, "Options" fading in 120 ms), FlyoutBorder 8,4,8,8 (Slate 4,0,4,4); placed left of the bar at the tile's top (placement 2), clamped to the screen; closed by a second tile click, a click on another luxshell surface, or any click outside (pointer grab with owner events, `surface.on_outside`) |
| Power flyout (powerflyout.xaml) | white panel 194 wide; "Power Status (1)" HeadingText #808080 bold 14 margin 8; the gauge again (wider); status / charge / time / rate / capacity / battery rows; #808080 rule; "Power Plans" with the three plans and descriptions, the selected one from `/sys/firmware/acpi/platform_profile` (read-only). The brightness and "keep my screen on" parts are not shown |

Comparison crops (reference vs ours, power and tray tiles, toast collapsed/expanded): `x11/lhshell/compare_tiles.png`.

### Scene additions
`rect` takes `gradient = { dir = "h"|"v", {offset, "#rrggbb"}, ... }` (LinearGradientBrush), `stroke`, `stroke_width`;
`node:desired([w, h])` returns a subtree's auto size (vbox/hbox children now stack in the measurement too, as in layout);
`node:measure([wrap_width])`; text with `wrap = true` measures wrapped; `surface:background(color)`, `surface:round(r)`,
`surface:grab()/ungrab()` with `on_outside`, `lh.surface{ opaque = true }` (24-bit), popups carry `type` ("notification",
otherwise popup_menu); `surface-press` passes the surface id as a 4th argument. `LHSHELL_DEBUG=1` logs events and paint times.

### picom rules wanted (not applied; `picom-glass.conf` belongs to the WM work)
- `shadow-exclude += "window_type = 'notification'"`: the toast has its own 4002 rim and a round shape.
- Fading on map/unmap of notifications is welcome (picom default); the toast does no slide of its own (the IL has none).

## M6 design: one theme setting for everything, session commands, start menu, minimal mode

Status 2026-10-23: step 1 (registry, `_LH_THEME`, consumers, `docs/history/THEMES.md`, session commands, `luxwmc quit`) implemented and
verified on the lab (`:3`); details in `docs/LUXWM.md` ("EWMH and the shell", "Maximised mode"), `docs/CLIENT-PROTOCOL.md`
"Theme changes", `docs/history/THEMES.md` and `USAGE.md` "Themes and switching them". Deviations: `color`/`size` are `Default`/`Normal`
(the 4074/4051 `.theme` files), not `NormalColor`/`NormalSize`; registry paths are relative to the file (`frame =
data/frames/jade4074`, `shell = data/packs/4074-aero`), not bare names; old luxwm theme names are `aliases`; a fifth name
`jade4074-classic` (`composition = off`) covers the old `jade4074-classic` frame theme; the session-command confirmation
flyout is left to step 2 with the start menu (commands run through `lh.session.run(name)` or the root property
`_LHSHELL_COMMAND`).

Status 2026-10-24: step 2 (start menu, Super key, autohide, sidebar modes, settings flyout, session confirmation) implemented
and verified on the lab (`:3`); user docs in `USAGE.md` "Start menu, taskbar autohide, sidebar modes". Files: `luxshell/startmenu.lua`,
`menu.lua` (cascade + context menus), `apps.lua` (desktop entries, icon theme lookup, MFU, xdg defaults), `settings.lua`
(settings flyout, run/search box), `runtime.lua` (placement, autohide, modes, taskbar menu), `flyout.lua` (any owner, keyboard),
`lhshell.c` (keyboard grab + `on_key`, `on_enter`/`on_leave`, `_LHSHELL_START`, `_NET_WM_WINDOW_TYPE_MENU`, `icon_offset`,
`list_dir`/`mtime`), `tray.c` (the tray moves between surfaces), `scene.c` (nine-grid pieces sample only their own source
rectangle), `luxwm/wm.c` (`start` key action: Super alone → `_LHSHELL_START`). Assets: `shellpack.sh` extracts the `StartPanel`,
`StartMenu::Toolbar` and `StartMenu::MenuBand` parts of the 4074 Jade and Slate msstyles into `assets/startpanel-{jade,slate}/`
(PNG per part/state, un-premultiplied; `startpanel.ini` = the decompiled sections with decoded fonts) and 16/24/32/48 px
shell32 icons (4074) into `assets/shell32/`. Comparison: `lhshell/compare_startmenu.png`, `lhshell/compare_modes.png`.
- **Start menu measurements** (4074-dwm.png, Jade/Aero, 1280x1024): menu x 0..379, y 428..993 (380x566), directly on the
  taskbar; 1 px black + 1 px #6b6b6b outer frame; no user pane (Jade's StartPanel has no USERPANE part). Left column PROGLIST
  x 0..189 (bitmap 327 224x192, SIZINGMARGINS 50 1 100 91, CONTENTMARGINS 6 4 9 5), places PLACESLIST x 190..379 (324 190x200,
  25 25 100 91; white rounded panel from x 192), body height 526, LOGOFF row 40 (316 211x40) under both. Program tiles 40 px
  (sfthost: max(32 px icon, 2 text lines) + 4*SM_CYEDGE): first tile top ~435, 32 px icon at x 10, text at x 47, title bold
  white, subtitle CAPTIONTEXT 180; pinned/MFU separator 2 px (328, 134 px wide at x 31, y 520) in an 8 px slot (4*cySep);
  bonus separator at y 917 above "All Programs" (row 34 px, y 919..953, Segoe UI Bold white, text x 48, 19 px arrow 321/322
  4 px after it). Places rows 36 px from y ~437: 24 px icons at x 200, text at x 228, separators 1 px (325) in 4 px slots;
  first group bold (darker), the rest TEXTCOLOR 100. Log Off / Stand by: 24 px glyph cells from LOGOFFBUTTONS 317/318 (72x24 =
  eject, log off, power) at x 196 and 274, y 960, text 6 px after the glyph, 11 px between buttons, last text ending 32 px
  from the right edge. Slate (lh4074-181541.png, 1152x864): same 380 px / 190 split, y 288..835: USERPANE 64 px (248, picture
  frame 249 55x55 at ~7,7, name Franklin Gothic Medium 14 white with a 2,2 black shadow at x ~66), body 441 (PROGLIST/PLACESLIST
  are thin strips: 156x4 / 180x6, a 2 px orange accent line on top, white resp. #fff8e5 body), log off row 42 (234 97x39),
  places 30 px (Tahoma 8), Log Off + Shut down. Our rendering matches these within 1-2 px (fonts: Segoe UI is not installed,
  Tahoma stands in).
- **Behaviour** from desktop2: tile metrics (`_ComputeTileMetrics`), separators (`_DrawSeparators`, 4*cySep slots + the bonus
  one), log off toolbar right-aligned (`_RightAlign`), MFU = own launch counts (count, then recency; XP decays UEM counts),
  All Programs opens after the menu show delay (400 ms hover) or Right/Return, keyboard across panes (deskhost). Super alone
  toggles; inside the menu (our keyboard grab, so luxwm's passive grab is not active) Super's release closes it.
- **Deviations:** the 4074 bottom row has two buttons (Jade: Log Off + Stand by, Slate: Log Off + Shut down); both open the
  three-button flyout (Log Off / Stand by / Turn Off) with the clicked action preselected, so all three commands are one
  click away but nothing runs without confirmation. Jade has no user pane (msstyles has no USERPANE); `[start] userpane = 1`
  forces one. Places use the 4074 shell32 icons (Documents 348, Pictures 387, Music 386, Computer 16, Control Panel 137,
  Search 23, Run 25); no Recent Items/Contacts/Games/Help (nothing to point them at). The pinned block of the `pinned`
  sidebar mode reserves a **bottom** strut (luxwm's work area is one rectangle; a right strut would have taken the full
  149 px width for a 138 px block); minimized-mode taskbar tiles are compact views (`tile:taskbar(host)`: tray row, digital
  time). The All Programs cascade uses `StartMenu::Toolbar` (bg, hot 3-state button) and `MenuBand.SEPERATOR`, stretched
  (our nine-grid TILE repeats the whole bitmap). picom (xrender) sometimes left popups under a newly mapped submenu unpainted:
  the open levels are repainted 180 ms after a level opens.

Status 2026-10-24 (step 3, aap's review: sidebar edge, resize drags, clock modes, menu polish, Luna start menu, compositor
rule); user docs in `USAGE.md` ("Resizing the bars", "Clock tile", "Compositor rule", "Luna start menu", state.ini table).
- **Sidebar edge bug.** Root cause (not scene.c's nine-grid pieces: Aero rendered pixel-identical to `4074-dwm.png` at the
  edge): (1) the bar was 149 px instead of 150, (2) the `SidebarSizer` was not drawn. In Slate the sizer is 6 px of 1108
  (white at alpha 72/120/101/66/37/16) over the background; without it 1000's dark column 0 (rgb 56) showed as a 1 px black
  line and the light edge was missing (the user saw it in Luna, which uses the Slate pack). Now 150 px, sizer drawn: Slate's
  first columns are 112/161/148/124/104/89, the same values as `4051-vm-now.png` and the `lh4074-18*` shots. Also fixed:
  `HackNineGrid` is a XAML Thickness (left, top, right, bottom), scene.c's `grid` is top, right, bottom, left: 1178's
  `36,0,0,0` is now left 36 (the edge gradient keeps its width at any bar width) and 1187/1155's `0,7,0,0` top 7 (the row-1
  highlight stays on taller bars). `lhshell/compare_sidebar_edge.png` (reference / before / after, Aero and Slate, 4x).
- **Resize drags.** `SidebarSizer`: a `cursor = "sizewe"` node (scene nodes take `cursor`: sizewe, sizens, hand, arrow;
  lhshell.c sets the X cursor of the hot/pressed node) docked left, 2 px (Aero, image hidden) / 6 px (Slate, 1108); drag =
  `_OnSizerDrag` (width - dx), clamp 100..600 (`_ValidateSize`), applied every 25 ms: surface, right strut, taskbar width,
  `_NET_WORKAREA` (luxwm refits), event `sidebar-resize` (clock and tray tiles reflow); `[sidebar] width` on release. Taskbar:
  the horizontal bar's `TaskbarSizer` is Width 0 (Rotated), rows go through the appbar's sizing edge (`_PtOnResizableEdge` →
  `SetTaskbarRowCount`, RowCount 1..3), so luxshell has a 3 px SizeNS strip on the bar's top edge, snapping at half rows;
  height = rows x 30 (Slate 28), task buttons fill rows (`ceil(n / rows)` per row, width rule as before), start button at the
  top (inferred), toggle centred; `[taskbar] rows`. Verified with xdotool on `:3`: workarea 1130 → 1030 (250 px), clamp to
  100 px (1180), 3 rows → 710 high, back to 1 row → 770.
- **Clock tile modes** (`tiles/clock.lua`). `[clock] pinned = 1` (4074, bottom host) | `0` (a tile in the list: the 4051 look,
  96 px face at tile y 22, "6:37 AM, Oct 23" in Tahoma 10 at tile y 142, tile 160 high; measured against `4051-vm-now.png`:
  face rows 75 vs 75, text glyph top 194 vs 196 with our tray tile 2 px shorter than its quick-launch tile, text 90 vs 94 px
  wide). `[clock] layout` = the six `clocksidebar*.xaml` views, from the tile's right-click menu ("Clock layout", plus "Pinned
  to the bottom (4074)", Move Up / Move Down for tiles in the list, order in `[sidebar] tiles`). The XAML margins
  `bin:02HHVV` are two-value Thickness (horizontal, vertical). digtime views: analog + bold DigitalTime (#D8E4FF / #9EB5BB) +
  DigitalText lines; their AnalogClockPanel has no size in the XAML: 56 px, 40 px beside the weekday/date lines (inferred).
  minical: 42 px clock + MiniCalendar (#D3D3D3 border, "^ddd" on #ADD8E6, date Tahoma 11 #4B5681 on white) + centred bold
  time. The big calendar (`cal`) was cheap and is done: `clocksidebarcal.xaml`'s grid (gradients #838A90→#767B7F header,
  #6B7278→#5B6366 body, Tahoma 7.5 white dates, Sunday first as the January 2004 sample shows) for the current month.
  `lhshell/compare_clock_modes.png`.
- **All Programs cascade** (`menu.lua`). It fell back to DejaVu Sans ("Segoe UI" is not installed; the panel says
  "Segoe UI,Tahoma"): msstyles fonts now get Tahoma as fallback family (Franklin Gothic Medium → ITC Franklin Gothic Std
  Medium). Hover: Slate's (and Luna's) `StartMenu::Toolbar.BUTTON` is an empty 8x6 bitmap in all three states, so nothing was
  drawn; when the HOT strip has no alpha (`lh.image_alpha`) the cascade uses the flat-menu MENUHILIGHT with HIGHLIGHTTEXT
  (`StartMenu::Toolbar(HOT)` TEXTCOLOR when set), as XP's MenuBand does; Jade keeps its light-blue plate.
- **Luna start menu.** `tools/lhstyle.py --variant NAME`; `shellpack.sh` extracts the 4051 `luna.msstyles` StartPanel /
  StartMenu parts for NORMALBLUE / NORMALHOMESTEAD / NORMALMETALLIC into `assets/startpanel-luna-{blue,olive,silver}/`
  (non-alpha TRANSPARENT parts keyed on TRANSPARENTCOLOR; the Jade/Slate output is byte-identical to before). `themes.ini`
  gains `startpanel = luna-*` (Luna keeps the Slate bars); `data/packs/startpanels.lua` holds the XP metrics (380 wide,
  user pane 64, PROGLIST 306 + MOREPROGRAMS 30 | PLACESLIST 336, log off 40; tiles 40 / 32 and separators 8 per
  `_ComputeTileMetrics`; selection COLOR_HIGHLIGHT; "Log Off" + "Turn Off Computer") and each scheme's FLATMENUS colours.
  No XP start-menu screenshot exists in flxp's screenshots/doc (only flat menus), so the metrics are the source's, not
  measured: `lhshell/compare_startmenu_luna.png` (XP flat menu reference / ours in blue, olive, silver).
- **Compositor follows the theme** (runtime.lua `lh.compositor`): on a `_LH_THEME` change `composition = off` stops this
  display's picom (processes named picom with our DISPLAY in /proc/PID/environ, SIGTERM), `auto` starts `[commands] picom` /
  `$LHSHELL_PICOM_CMD` / picom-glass.conf+glx when `_NET_WM_CM_S0` has no owner; settings flyout "Composition: Follow the
  theme / Always on / Always off" (`[shell] composition`, on/off also applied at startup). Verified on `:3`: aero4074 → luna:
  picom stopped, `_LHWM_COMPOSITED` 0, `_LHWM_THEME` luna-blue (classic frames); → aero4074: picom started, composited 1,
  glass frames; slate4051 likewise; the flyout's Always off / Always on stop and start it.

Status 2026-10-24 (step 4, aap's review: Slate start button, 4051 vs 4074, layering, toggle glyph); user docs in `USAGE.md`
("4051 vs 4074 Slate", "Stacking", state.ini `[sidebar] layer`, `[clock] pinned_pack`) and `docs/history/THEMES.md` ("luxshell packs").
- **Slate start button "not full height".** Root cause: the rows work put the StartButton in a canvas slot (so it stays one row
  high at the top of a taller bar) with the pack's `start = { w = 78, h = 23 }`, i.e. 1156's own size; before, as a Dock=Left
  child of the dock panel, it had been stretched to the full bar height regardless of `h`. Now Width 83 / Height 29 with the 4 px
  right Border as in Aero (image 79x29), and the Slate bar is 30 px, not 28: `lh4074-181533.png` (4074 Slate Avalon taskbar) has
  the bar at rows 834..863 and 1155 (50x28) stretched whole to 30 rows nearest-neighbour (rows 7 and 21 repeat; a 7 px top grid
  does not fit), so the pack's bg grid is 0. Ours against the shot: start button mean squared error 0.03 per pixel, bar column
  1.6 (one row: pixman's 16.16 stepping repeats row 6 where the shot repeats row 7).
- **Image filter per pack** (`filter = nearest | bilinear` in shell.lua, docs/history/THEMES.md "luxshell packs"; scene.c `scene_filter`,
  node `filter`, `lh.image_filter`): stretched nine-grid pieces and magnified images use it, shrinking stays BEST. Evidence, all
  as fits of 1156 / 1179 drawn 79x29 over the bar's own background column: 4074 Slate (`lh4074-181533.png`) nearest 54 vs
  bilinear 250, Aero without DWM (`lh4074-181415.png`) 129 vs 266, Aero glass (`4074-dwm.png`) 1868 vs 2047 (blur under it);
  4051 (`4051-vm-now.png`) bilinear 0.2 vs nearest 249. Edge profile of the flag (column 36 of the button, luminance):
  4074 `86 144 144 196 203 178 182 182 ... 98 98 144 166 134 114 114 95` (repeated rows = blocky), 4051 `86 126 169 198 202
  181 178 175 172 177 170 126 119 152 173 155 143 125 98` (ramps). aero and slate are `nearest`, slate4051 `bilinear`.
  The options glyph (1176/1103, 10x6 in a 9x6 HackImage) is drawn 9x6 with a column dropped in both builds (exact with nearest;
  ours had stretched it to 9x8 because the docked image took the row's height): fixed in the sidebar and the flyout.
- **4051 vs 4074 Slate** (rule of thumb: builds after ~4060 look like 4074). `themes.ini [slate4051] shell =
  data/packs/4051-slate`, a pack that loads `themes/slate` and changes: `filter = bilinear`; taskbar 29 px (`4051-vm-now.png`
  rows 835..863, not 28) with 1155 drawn top 7 / bottom 2 fixed and the middle nearest (`bg_filter`), which repeats row 16 as
  the classic bar does (exact); start button 83x29 (1156 bilinear, error 1.4 against the shot); `imageres = imageres-4051`:
  `shellpack.sh` writes the 34 PNGs of 4051's imageres.dll that differ from 4074's or are missing there (tile arrow 1105/1106 =
  the right-angled corner of the shot, tile restore/close 1058-1062, toast parts 4003-4011, 6000-6007, ...); clock tile in the
  list by default (`clock_pinned = false`; `[clock] pinned` now applies only to the pack named in `[clock] pinned_pack`, which
  the clock's menu writes); tiles tray + clock (no power tile in the 4051 shots). The 4051 `Sidebar` class blobs
  (`assets/sidebar-slate51`) are pixel-identical to 4074 Slate's streams (60 of 60), so they change nothing. Sidebar against
  `4051-vm-now.png`: width 150, edge columns, options row, separator rows 17/18 and the first tile arrow are exact (rows 0..27:
  error 0); the first tile is our tray tile, 2 px shorter than 4051's quick launch tile, so the clock and its text sit 2 px
  higher (text 194 vs 196). **Stand-in:** 4051 ran the classic Win32 taskbar (task band plates 349, no sidebar toggle); we keep
  the Avalon taskbar with the 4051 start button, height and filter (the classic one is M8, flxp-tray).
  `lhshell/compare_4051_4074.png`: reference 4051 / ours / reference 4074 Slate / ours, start button area at 4x.
- **Layering.** The sidebar is `_NET_WM_STATE_ABOVE` like the taskbar (it was BELOW, which looked wrong where it runs past the
  taskbar's end); it is raised with the taskbar on window-list changes instead of lowered on every placement. `[sidebar] layer =
  below` keeps the old stacking. Checked on `:4`: a window moved over the sidebar stays under it, a maximised one gets 1130x770
  (`_NET_WORKAREA` unchanged), the aero black layers fade with `_LHWM_MAXIMIZED_MODE` 1 (xrender picom); with `layer = below`
  the window has `_NET_WM_STATE_BELOW` again.
- **Sidebar toggle glyph sometimes missing.** Root cause: in `build_taskbar` the toggle's `images = {...}` had slipped into the
  `-- TransformDecorator VCenter` comment at the end of the line above (rows work), so the button had no image; it appeared only
  after a sidebar mode change (update_toggle set the images) and vanished again on every pack switch or restart. Now built with
  its images (`toggle_images()`, also used by update_toggle) and centred by `toggle_y()`. Checked on `:4`: visible at start, full
  → minimized → full, full → pinned → full, slate4051 → slate4074 → aero4074.

Status 2026-10-24 (icons, fonts, desktop layer; with luxwm): user report "icons only very occasionally". luxshell itself read
`_NET_WM_ICON` and `WM_HINTS` and followed PropertyNotify, and showed icons for every lab client that has one; what was missing was a
default (no pack set `T.default_icon`; zenity, extendframe, xmessage and every flxp window, which sets no icon at all, got none) and
the size pick (nearest to 16, scaled with FILTER_GOOD). Now: `icon_from_netwm` takes the smallest image >= 16 (else the largest), both
paths scale once with `CAIRO_FILTER_BEST`, both packs have `default_icon = "shell32/16/3.png"` (task_button used `R()`, the imageres
mapper, for it; now a plain assets path). The `@icon:<window>` entry is replaced on each icon PropertyNotify and the task list rebuilt
(`window` event), so a changed icon shows at once (checked with xprop set/delete). Fonts: themes.ini `fonts =` (docs/history/THEMES.md "Fonts")
is applied by `lh.fonts()` (lhshell.c: FcConfigAppFontClear + AddDir, new pango font map) at startup and on every `_LH_THEME` change
(the bars are rebuilt when the set changed), so the packs' "Segoe UI 9" is 4074's Segoe UI 0.7 instead of DejaVu Sans;
`LHSHELL_DEBUG=1` logs the resolved face. `[shell] desktop_black` (M5b). Comparisons: `lhshell/compare_icons.png`,
`lhwm/compare_fonts.png`.

Written 2026-10-23 night. aap's questions: who owns the theme when luxwm, luxshell and the flxp
toolkit (`~/src/flxp`, Wine-derived uxtheme reading real .msstyles) all need to change together,
how clients learn about it (and re-check the glass extension), how to quit the session, and a
"minimal" mode: taskbar hidden, only time and tray icons visible.

### 6.1 Theme registry and the `_LH_THEME` root property
Windows has one setting (the active visual style + colour + size) and one broadcast
(`WM_THEMECHANGED`). We do the same with a root property and a shared registry file.

- **Registry:** `data/themes.ini`, one section per theme *name*:
  ```
  [jade4074]
  msstyles = /u/aap/fun/longhorn/wim/4074/.../jade.msstyles   ; what uxtheme clients load
  color    = NormalColor
  size     = NormalSize
  frame    = jade              ; luxwm theme dir when composited
  frame_classic = jade4074-classic   ; luxwm theme dir without a compositor (or when composition is off)
  shell    = aero              ; luxshell pack (Avalon bars imagery)
  wallpaper = mock/assets/wallpaper/longhorn.jpg
  composition = auto           ; auto | off  (off = always classic frames, like USESCOMPOSITION=FALSE)
  ```
  Names to ship: `aero4074`, `jade4074`, `slate4074`, `slate4051`. Paths are absolute or relative
  to the file. flxp reads the same file (document the format in `docs/history/THEMES.md`; it is plain ini
  and the msstyles/color/size triple maps straight onto `FLXP_THEME`, `FLXP_THEME_COLOR`,
  `FLXP_THEME_SIZE`). The file's location is published too, so nobody hardcodes it.
- **Setting:** root property `_LH_THEME` (UTF8_STRING): the theme *name*. Root property
  `_LH_THEMES_INI` (UTF8_STRING): absolute path of the registry. Whoever starts first publishes the
  persisted value: luxshell owns persistence (`~/.config/lhshell/state.ini`, key `theme`), luxwm
  publishes `LHWM_THEME`/`-t` only if `_LH_THEME` is absent at startup (so `lhde-session THEME`
  keeps working: it writes the name into the state dir, luxwm maps it through the registry).
- **Consumers:**
  - luxwm watches `_LH_THEME`; on change it loads `frame` (composited) or `frame_classic`,
    exactly the existing `_LHWM_THEME` switch; `_LHWM_THEME` stays as the *derived* frame-theme
    path (read-only for others from now on; setting it directly still works for debugging).
    It republishes `_LHWM_COMPOSITED` and `_LHWM_FRAME_METRICS` on every switch even if unchanged,
    so clients get one PropertyNotify per theme change.
  - luxshell watches it, swaps the pack (bars imagery, clock face, tile chrome) live, sets the
    wallpaper (`feh --bg-fill` or an internal root pixmap later), and its theming settings UI
    writes it.
  - Clients (flxp, extendframe): treat PropertyNotify on the root for `_LH_THEME`,
    `_LHWM_COMPOSITED` or `_LHWM_FRAME_METRICS` as `WM_THEMECHANGED`: reload the msstyles named
    by the registry, re-read `_LHWM_COMPOSITED`, and re-decide the glass extension (set or
    delete `_LHWM_EXTEND_FRAME`, repaint the header opaque or transparent). This is already how
    `examples/extendframe.c` handles composition changes; extend it to `_LH_THEME`.
- **Semantics for the deco:** aero/jade names use glass frames when a compositor is present
  and fall back to `frame_classic` otherwise (as M5a does); slate names are classic always.
  `composition = off` forces classic for a glass-capable theme (the 4074 "no DWM" look).

### 6.2 Session commands
luxshell gets configurable commands in `state.ini` `[commands]` with defaults: `logoff` =
`lhde-session stop` when its state dir exists, else `luxwmc quit` (luxwm exits cleanly:
properties deleted, clients left mapped); `standby` = `zzz` if present else `loginctl suspend`;
`shutdown` = `loginctl poweroff`; `restart` = `loginctl reboot`; `lock` = `xlock`/`slock` if
present. luxwm gains a `quit` IPC command (berry's `luxwmc`) and a clean-exit path. The start
menu's "Log Off" / "Stand by" / "Turn Off" use these. A confirmation dialog is not Longhorn's
(XP asks via the shutdown dialog; we do a small luxshell flyout with the three buttons).

### 6.3 Start menu (M7 brought forward)
4074's start menu is the XP start panel (DirectUI, `sfthost.cpp`/`userpane.cpp`) skinned via the
msstyles `StartPanel` class: parts USERPANE, MOREPROGRAMS, OPENBOX, SEARCHVIEW, PROGLIST,
PLACESLIST, LOGOFF, LOGOFFBUTTONS, USERPICTURE, PREVIEW (Longhorn adds `STARTPANEL.PREVIEW`),
with the `StartPanel::Toolbar` text colours. Reference: `screenshots/4074-dwm.png` (Aero start
menu open: dark pane, white places column, "All Programs ▸", "Log Off" / "Stand by" row) and the
XP source (`shell/explorer/desktop2/*.cpp`, `/u/aap/lib/nt5src/...`) for metrics and behaviour.
Build it in luxshell as a popup surface (`_NET_WM_WINDOW_TYPE_MENU`, override-redirect, grabbed
pointer/keyboard like a menu), opened by the StartButton (and Super alone, via luxwm sending a
`_LHSHELL_START` root message when the Super key is pressed and released without another key):
- left column: pinned items (ini list of `.desktop` files; default web browser + mail client
  from `xdg-settings`), separator, recent/frequent apps (own launch history), "All Programs"
  (cascading submenu built from `/usr/share/applications` + `~/.local/share/applications` by
  category, with icons from the icon theme, 16 px);
- right column: Documents, Pictures, Music, Computer (open with the configured file manager:
  `T.commands.filemanager`, default `xdg-open`/rox/flxp when present), Control Panel
  (= lhshell settings flyout: theme list from the registry, taskbar autohide, sidebar mode,
  wallpaper), Search, Run (`T.commands.run`, default `rofi -show run` else a small own run box);
- bottom: Log Off / Stand by / Turn Off from 6.2; user pane: `$USER` + picture
  (`~/.face` if present else imageres default icon).
Use the Slate/Jade StartPanel bitmaps from the msstyles (extract with `tools/lhstyle.py` into
`the data directory's packs/ (lhde-import; was lhshell/assets/) startpanel-<theme>/`, same as the sidebar packs) and the StartPanel fonts and
colours from the ini. Keyboard: Esc closes, arrows navigate, typing jumps.

### 6.4 Taskbar autohide and sidebar modes (the "minimal" mode)
- **Taskbar autohide** per 2.1/3.x (Avalon: hides when focus leaves the bar, 100 ms slide,
  2 px sliver at the edge, re-shows on pointer at the edge; the strut becomes the sliver so the
  work area grows). Setting in `state.ini` `[taskbar] autohide`.
- **Sidebar modes** `[sidebar] mode = full | minimized | pinned`:
  - `full`: today;
  - `minimized`: Longhorn's `SidebarControl.SetMinimized` (research row L401): tile
    collection hidden, the pinned clock and the notifications tile move into the **taskbar**
    (`TBContentHost`), sidebar width 0, the taskbar runs the full screen width; the taskbar
    toggle button (1170/1167) flips it, as in 4074;
  - `pinned` (our addition for aap's wish): the sidebar surface shrinks to the bottom block
    holding only the pinned tiles (clock + notifications), with glass background, no options
    row, strut covering only that block; everything above is free desktop.
  With `autohide` on and `mode = pinned`, only the clock and the tray icons are visible, which
  is the requested minimal mode. Transitions animate like the tile fades (400 ms), the WM's
  work area follows the struts (M2), maximised windows refit (M2).

### Order of work
1. Registry + `_LH_THEME` + lhwm/lhshell/extendframe consumers + `docs/history/THEMES.md` + session
   commands + `luxwmc quit` (one agent, luxwm + luxshell).
2. Start menu + autohide + sidebar modes + settings flyout (one agent, luxshell; flxp is told the
   registry format via docs/history/THEMES.md).

## M5b design: maximised windows the Longhorn way (2026-10-24)

Status 2026-10-24 (luxwm part): implemented. Maximised DWM frames draw `plate_max` (aero/jade LGFRAMEMAX 493) or `plate_max_explorer`
(MAXEXPLORER 494, extended clients); aero's are opaque dark greys with no pill (desksrv 9-grid margins (0,27)(0,0) and (0,71)(0,0)), Jade
4074's are its normal plate again. The frame jumps to the new rectangle and crossfades the two plates there, 400 ms linear (the curve
luxshell's black layers use), 25 ms ticks from luxwm's main loop, replacing picom's geometry animation for these themes; `_LHWM_MAXIMIZED_MODE`
is still written before the geometry. Verified on the lab (mode-before-ConfigureNotify trace, 50 ms series: on restore frame and bars fade
in step) and against `aero_max.png` (`lhwm/compare_max_plate.png`). Classic themes unchanged. luxshell part (2026-10-24, later pass): `[shell]
desktop_black = 0` is the default, so the desktop layer stays unmapped in maximised mode (its future Aurora role only) and a maximise
shows only luxwm's frame crossfade and the bars' black layers; `desktop_black = 1` restores the M5a full-screen black (checked on `:3`:
the `lhshell-desktop` window stays IsUnMapped with `_LHWM_MAXIMIZED_MODE` 1, IsViewable with 1). The maximised caption title is now
white without glow, fading with the plate (`docs/LUXWM.md` "Icons, maximised caption text, fonts"). Details, measurements and deviations: `docs/LUXWM.md` "Maximised mode" (M5b).
Status 2026-10-25 (luxwm): the maximise/restore geometry change is one step (frame and client to their final rectangles in one batch,
one paint, under a server grab together with the `_LHWM_MAXIMIZED_MODE` write), so the bars and the plate crossfade start in the same
composited frame and end together; the default animation style no longer runs picom's geometry animation (`_LHWM_ANIMATE = 8` is gone).
luxshell unchanged. `docs/LUXWM.md` "Maximised mode", "One step".

aap's correction, with `screenshots/aero_max.png` (a maximised 4074 Aero Explorer from a video):
the black does not fade over the whole screen. The maximised window's **own frame is a dark plate**
and the bars go black; nothing else exists on screen. 4074's CompositedWindow has the textures:
`LGFRAMEMAX` (493) and `MAXEXPLORER` (494) are the maximised plates (COMPOSITION.md §3 table), 5048
has a dark maximised plate too (uDWM bitmap 1002). So:

- luxwm, DWM themes: a maximised frame draws `plate_max` (LGFRAMEMAX; `plate_max_explorer` =
  MAXEXPLORER for extended/Explorer-style clients) instead of the normal plate with squared
  corners; the plates are extracted into `themes/aero|jade` with their own insets (measure; the
  maximised plate has no rounded corners and probably no side borders). The glass alpha rule
  applies to them as to the others.
- The transition is a **400 ms crossfade of the frame itself** between the normal and the
  maximised plate, drawn by luxwm over the window's *new* rectangle (on maximise the frame first
  jumps to the maximised geometry and fades dark; on restore it jumps to the restored geometry and
  fades back to glass), in step with the bars' black layers (same 400 ms, same curve). No
  full-screen desktop black: luxshell's desktop layer stays transparent in this mode and keeps
  only its future Aurora role (`[shell] desktop_black = 0` default; 1 restores the old behaviour).
- Classic (msstyles) themes: unchanged (MAXCAPTION, instant), the bars' black layers still fade
  (the Avalon shell does that regardless of DWM, 2.4.3).
- Verification: compare against `aero_max.png` (1920x1080: header dark glass with the sheen,
  taskbar and sidebar black, clock face dark) and a 50 ms series of maximise/restore.

## M8 design: the classic (Win32-style) taskbar as a separate program (2026-10-24)

aap asked whether the msstyles-themed classic taskbar should be a mode of luxshell's Avalon taskbar
or a distinct program, possibly in flxp land. Recommendation: **a distinct program built on the
flxp toolkit** (working name `flxp-tray`), for the same reason Longhorn had it that way: the
classic taskbar *is* Win32 explorer code (`tray.cpp`, `taskband.cpp`, `traynot.cpp`,
`trayclok.cpp`: a rebar with a `TaskBand::Toolbar`, the `Start::Button`, the `TrayNotify` toolbar
and clock, all drawn through uxtheme classes, research in 2.1), and flxp already has the Wine
comctl32 rebar/toolbar/tooltips and the uxtheme renderer those classes need. Rebuilding it in
luxshell's Lua scene would duplicate that toolkit with less fidelity (grouping menus, chevrons,
tooltips, balloon tips, the autohide timers of 2.1 are all comctl32/explorer behaviour). luxshell
keeps the Avalon bars and the sidebar, exactly as 4051 ran the Avalon "start bar"/sidebar next to
the classic taskbar.

### Interface between flxp-tray and lhshell/lhwm (all standard or already existing)
| Need | Mechanism |
| --- | --- |
| Window list, titles, icons, active, minimised, attention, desktops | EWMH on the root (`_NET_CLIENT_LIST`, `_NET_WM_STATE`, `_NET_ACTIVE_WINDOW`, `_NET_WM_ICON`, `WM_HINTS`), as luxshell reads them today (lhshell.c is the reference). |
| Activate / minimise / close from a button | `_NET_ACTIVE_WINDOW`, `_NET_WM_STATE` HIDDEN, `_NET_CLOSE_WINDOW` client messages (luxwm M2). |
| Minimise animation target | the task button owner sets `_NET_WM_ICON_GEOMETRY` on each client; luxshell stops setting it when its taskbar is off (`[taskbar] enabled = 0`). |
| Reserve screen space | `_NET_WM_STRUT_PARTIAL` + `_NET_WM_WINDOW_TYPE_DOCK` (luxwm M2). With the sidebar present, the classic taskbar spans the full width and the sidebar yields to it (4051 screenshot: taskbar 28 px full width, sidebar above it) — so luxshell's sidebar must honour another dock's bottom strut (`[sidebar] yield_bottom = auto`: read `_NET_WORKAREA`). |
| Start menu | the classic Start button sends the root client message `_LHSHELL_START` (already used by the Super key); luxshell shows its StartPanel menu anchored above the button rect passed in the message's data (x, y, w, h) instead of its own button. |
| Tray icons | one XEmbed tray manager per display: when flxp-tray owns `_NET_SYSTEM_TRAY_S0`, luxshell's notifications tile hides itself (it polls the selection owner), and vice versa. |
| Clock | `_ToggleClock` semantics (COMPOSITION.md §11): if the sidebar shows a clock, the tray clock is hidden; luxshell publishes `_LHSHELL_CLOCK_VISIBLE` (CARDINAL) on the root; flxp-tray reads it. |
| Theme | `_LH_THEME` + `themes.ini` (docs/history/THEMES.md): flxp-tray loads the msstyles it names (`flxp_msstyles` when set) and switches live; its `[sysmetrics]`/`TaskBar` classes give sizes. |
| Autohide / position | its own (explorer's `StuckRects2` logic, 2.1); persisted in its own ini. luxshell's taskbar autohide is independent. |
| Black layers when maximised | the classic taskbar has none (that is Avalon-only, 2.4.3); nothing to do. |
| Composition | none needed; it is an opaque dock. Shadows: picom rule `window_type = 'dock'` already disables them. |

### What luxshell needs for this
`[taskbar] enabled = 0|1` (0: no Avalon taskbar surface, no struts, no `_NET_WM_ICON_GEOMETRY`,
Super key still opens the start menu anchored to the `_LHSHELL_START` rect or bottom-left), the
sidebar yielding to a foreign bottom strut, the tray-owner check, `_LHSHELL_CLOCK_VISIBLE`, and
the `_LHSHELL_START` message carrying an anchor rect. Fallback if flxp-tray never happens: the
same switch could later select an luxshell classic renderer over the 2.1 fact sheet, since the
model is shared ("two renderers over the same model", §3).

### Start menu ownership (2026-10-24)
Question from aap: does the start menu belong here or in flxp land? In Longhorn the start panel is
Win32 DirectUI inside explorer, next to the classic taskbar, so by the M8 logic it would be flxp's.
Decision for now: **keep luxshell's StartPanel menu** (it works, is themed from the msstyles parts,
and already serves both a classic Start button and the Super key through `_LHSHELL_START`, which
carries an anchor rect). If flxp-tray later wants its own, it registers as the start-menu provider
by owning the selection `_LH_STARTMENU_S<screen>`; luxshell then forwards `_LHSHELL_START` (and the
Super key) to the owner instead of opening its own. Either way there is exactly one start menu per
display and the themed look comes from the same msstyles through libuxtheme.

### Start menu ownership, revised (2026-10-25)
flxp-taskbar exists (`~/src/flxp/src/taskbar`, NOTES.md "The taskbar") and already carries a
start menu; flxp is building the accurate start panel. Decision: **responsibility moves to flxp.**
luxshell keeps its StartPanel menu only as the fallback when no provider is present. Protocol
(implemented in luxshell now, to be mirrored by flxp-taskbar): the provider owns the selection
`_LH_STARTMENU_S<screen>`; luxshell checks the owner when the Avalon Start button is clicked or the
Super key arrives and, if someone owns it, sends `_LHSHELL_START` (data: anchor x, y, w, h, and
flags bit 0 = toggle) to the owner window instead of opening its own; the owner also receives
the message when flxp-taskbar is the one with the Start button, so one menu serves both bars.
Switching bars: `[taskbar] enabled = avalon | classic | none` in luxshell's state.ini; `classic`
starts flxp-taskbar (`[commands] taskbar`, default `flxp-taskbar`) and stops luxshell's own bar,
with the sidebar yielding to its strut (M8 table). `lhde-session taskbar avalon|classic`
and the settings flyout flip it.

#### `_LHSHELL_START`: the message layout (for start-menu providers)
`_LHSHELL_START` is a ClientMessage, format 32, `message_type` = the atom `_LHSHELL_START`. Data: `l[0]` = flags (bit 0 =
toggle: close the menu if it is open, else open it; 0 = open, nothing if already open; other bits 0, reserved), `l[1]`, `l[2]`,
`l[3]`, `l[4]` = x, y, width, height of the anchor rectangle in root coordinates (the Start button that asked; width or height 0 =
no anchor: the provider opens at its own Start button, else bottom-left above the work area). The menu opens above the anchor
with its left edge at the anchor's (below it when the anchor is in the upper half of the screen). Three senders:
(1) **luxwm**, Super pressed and released alone: sent to the root (`SubstructureNotify|SubstructureRedirect`) with `l[0]` = the
key's timestamp and no rect; a root message without a rect therefore always means "toggle, no anchor", whatever `l[0]` holds.
(2) **a classic Start button that leaves the menu to luxshell** (flxp-taskbar `start_menu = lhshell`): sent to the root with
flags and its button's rect. (3) **luxshell forwarding to a provider**: a client that owns the selection
`_LH_STARTMENU_S<screen>` is the provider; luxshell sends the message to the selection owner window itself (`XSendEvent(owner,
propagate False, mask 0)`) whenever the Avalon Start button is clicked (flags 1, the button's rect) or a root `_LHSHELL_START`
arrives (flags as received, 1 for luxwm's; the rect as received, else the Avalon Start button's rect, else none), and opens no menu
of its own. A provider must answer messages on its owner window, and must ignore root `_LHSHELL_START` broadcasts while the
selection `_LHSHELL_S<screen>` has an owner (luxshell runs and forwards them; answering both would toggle twice); without luxshell
it may answer luxwm's broadcast itself. Its own Start button opens its menu directly. With no provider, luxshell opens its
StartPanel menu for all of these, anchored as above (the old behaviour).

### M8 luxshell side (status 2026-10-25)
Implemented and verified on the lab (`:3`, aero4074, luxwm + picom xrender, flxp-taskbar from PATH); user docs in `USAGE.md`
("Classic taskbar instead of the Avalon one", state.ini table, `lhde-session taskbar`). `lhshell/compare_taskbars.png`:
avalon / classic in one session (top: half size; bottom: the bottom-right corner 1:1).
- **Switch** `[taskbar] enabled = avalon | classic | none` (1/0 of the first draft = avalon/none; `LHSHELL_TASKBAR` overrides it
  for one run). `classic`/`none`: the taskbar surface is unmapped, its strut deleted, no task buttons, and lhshell.c deletes the
  `_NET_WM_ICON_GEOMETRY` it had written (a hidden surface drops its icon geometries). `classic` runs `[commands] taskbar`
  (default `flxp-taskbar`; `$LHSHELL_TASKBAR_CMD`) through `lh.spawn_pid` (`sh -c 'exec ...'`, so the pid is the bar's), with
  luxshell's DISPLAY and session bus, `FLXP_TRAY_TAKEOVER=1`, and `FLXP_START_MENU=luxshell FLXP_START_KEY=0` (`[taskbar]
  classic_start_menu = lhshell`, then the default; now `flxp` is, see the update below) or `own`/`1` (`= flxp`; luxshell then leaves luxwm's Super message to the bar while it runs
  and nobody owns `_LH_STARTMENU_S`); no `--theme` (flxp-taskbar follows `_LH_THEME` itself). A bar already running on the display
  is adopted, not started twice. Stopped by that pid after checking `/proc/PID/comm` and `DISPLAY` in `/proc/PID/environ`, on a
  switch away and on luxshell's exit. Live from the settings flyout (*Taskbar* group) and from `lhde-session taskbar
  avalon|classic|none` (root `_LHSHELL_COMMAND` = `taskbar KIND`; state.ini written directly when no luxshell runs there;
  `SECOND_DISPLAY=:N` for other displays). Start menu, flyouts and toasts sit above the classic bar (`lh.taskbar_shown_top`
  = the top of another dock's full-width bottom strut when our taskbar is off).
- **Sidebar yield** (`[sidebar] yield_bottom = auto | 0`): `lh.struts()` (lhshell.c) reads `_NET_WM_STRUT_PARTIAL`/`_NET_WM_STRUT`
  of every mapped top-level that is not ours (and one level down, inside WM frames); the largest bottom strut over the sidebar's
  columns shortens the full sidebar (`h = screen - strut`, the right strut's partial range follows), puts the pinned block
  above it (bottom strut = block + bar) and the minimized one out of the way. Re-checked on every `_NET_WORKAREA` change and on
  top-level map/unmap/destroy (luxwm keeps `_NET_WORKAREA` unchanged when our Avalon bar's strut replaces the classic one's).
  Measured on 1280x800: classic: sidebar 1099,0 181x770 with right strut range 0..769, flxp-taskbar 1280x30 at y 770,
  `_NET_WORKAREA` 0,0 1099x770; back to avalon: sidebar 181x800, taskbar 1099x30, work area 1099x770; none + minimized: 1280x800.
- **Tray**: `tray_start` no longer takes `_NET_SYSTEM_TRAY_S<screen>` from another owner (it did before: "taking over");
  the notifications tile hides itself (tile chrome invisible, pinned block and clock separator follow) while another client owns
  it and claims it again when the owner goes away (`lh.watch_selection` = XFixes `SelectSelectionInput`, else a 2 s poll; event
  `tray-owner`). Before starting the classic bar luxshell hands the tray over itself (`lh.tray_yield`: icons unredirected,
  unmapped, reparented to the root, selection released; no reclaim for 10 s or until the new tray is up): letting flxp-taskbar
  take it over raced (see below). Handing icons back now also unredirects them (Composite), otherwise a tray without compositing
  shows nothing for them, and docking resets a foreign ParentRelative background (BadMatch into our 32-bit window otherwise).
- **Clock**: root `_LHSHELL_CLOCK_VISIBLE` CARDINAL = 1 while a clock tile of ours is on screen (sidebar; the minimized
  sidebar's compact clock in the Avalon taskbar), else 0; deleted on exit. Checked: 1 in full mode (flxp hides its clock), 0 with
  the sidebar minimized in classic mode (flxp shows "12:34 AM").
- **Start menu**: per the paragraph above; luxshell also owns `_LHSHELL_S<screen>` while it runs. Checked with a test provider
  owning `_LH_STARTMENU_S0`: Super → `flags 1, rect 0,0 0x0` (classic) / `flags 1, rect 0,770 83x29` (avalon, the Start button),
  flxp's Start button → `flags 0, rect 0,770 88x30`, Avalon Start button → `flags 1, rect 0,770 83x29`, our menu stayed closed;
  provider gone → our menu again. Without a provider, Super and flxp's Start button open the StartPanel above the classic bar.
- **flxp-taskbar against the M8 table** (read from `~/src/flxp/src/taskbar`, run unchanged). Matches: window list, activation,
  `_NET_WM_ICON_GEOMETRY` on every client, `_NET_WM_STRUT(_PARTIAL)` full width (30 px), dock type / above / sticky, theme
  from `_LH_THEME` live, `_LHSHELL_CLOCK_VISIBLE` (hides its clock while 1), tray ownership with `tray_takeover`, `start_menu =
  luxshell` sending the button rect in `l[1..4]` with `l[0]` = 0 (fits the layout: flags 0 = open). For the flxp team:
  (1) it does not own `_LH_STARTMENU_S<screen>` with its own menu, so luxshell cannot forward to it; (2) its X thread handles
  `_LHSHELL_START` only on the root, not on an owner window; (3) with `start_key = 1` it answers luxwm's root broadcast itself, so
  next to luxshell two menus open unless `start_key = 0`: once it is a provider it should ignore the broadcast while
  `_LHSHELL_S<screen>` is owned; (4) tray takeover race: when it takes the selection from luxshell, luxshell's hand-back
  (reparent to the root) can reach the server after its own `XSelectInput` on the icon but before its reparent, and the
  `ReparentNotify` (parent root) makes `tray_forget` drop an icon that then sits in its container unmanaged (check the current
  parent before forgetting); luxshell now avoids the takeover; (5) on exit it reparents icons to the root still mapped, so the WM
  manages them as windows until the next tray docks them (seen as a "trayicon" task button): unmap first, as luxshell's
  `tray_stop` does; it also leaves its ParentRelative background on them; (6) icons created under luxshell's ARGB tray visual stay
  32-bit and show black where transparent in its tray (no compositing there).
- **Deviations**: luxshell does not own `_LH_STARTMENU_S` itself (it is the fallback, not a provider); the classic bar gets the
  tray handed over before it starts rather than by takeover; `lhde-session taskbar` instead of the `taskbar on|off` restart
  that flxp's NOTES suggested.

#### Update (2026-10-25, later): classic mode without the sidebar, flxp's start panel as the classic menu
Two items of aap's todo ("classic taskbar should disable sidebar", "switch to flxp start menu, always saw the luxshell one").
Verified on the lab `:4` (aero4074, luxwm + picom xrender, flxp-taskbar of commit 41985d4); `lhshell/compare_classic_startpanel.png`:
classic + Super, classic + flxp's Start button (both flxp's XP start panel, a toast above the bar, no sidebar), avalon + Super
(luxshell's StartPanel, the sidebar back in its mode).
- **`[sidebar] with_classic = 0|1`** (default 0, written to state.ini). 0: in `enabled = classic` the sidebar surface is unmapped
  with no strut (`_NET_WORKAREA` 0,0 1280x770 on 1280x800), `_LHSHELL_CLOCK_VISIBLE` = 0 (flxp-taskbar shows its clock; it has
  the tray anyway), toasts take the full width above the bar (toast.lua: no sidebar column), Super works. The mode stays in
  `[sidebar] mode`; changing it while hidden only records it; switching back to avalon restores it (checked: pinned → classic →
  avalon = pinned; full chosen while hidden → avalon = full, work area 1099x770). 1: the 4051 layout as before (yield above the
  bar, work area 1099x770, clock visible 1). runtime.lua `sb_hidden()` (= `lh.sidebar_hidden`) gates `place_sidebar`, `set_mode`,
  `publish_clock`, the startup show and the raise on `windows`; `lh.set_taskbar` re-places the sidebar. Live: settings flyout
  *Sidebar* > "Also with the classic taskbar", root `_LHSHELL_COMMAND` `sidebar-with-classic 0|1`, `lhde-session taskbar
  classic` (sets 0) / `classic+sidebar` (sets 1). New `_LHSHELL_COMMAND` `settings` opens the settings flyout (the classic bar
  and flxp's panel have no way to it).
- **`[taskbar] classic_start_menu = flxp`** is the default now (`flxp-classic`, `luxshell` the others; the old `own` = `flxp`).
  What flxp-taskbar reads (src/taskbar/taskbar.cpp, shell/config.cpp: `setting(key)` = env `FLXP_<KEY>`, else flxp.ini, else the
  theme's `flxp_<key>`): `start_menu = panel` (default; XP's start panel) | `classic` (or `own`; the popup menu) | `luxshell`
  (sends root `_LHSHELL_START` with flags 0 and its button rect); `start_key = 1` (default) opens its menu on **any** root
  `_LHSHELL_START` (xwm.cpp: the message type on the root, data ignored), unless `start_menu = lhshell`. It does **not** own
  `_LH_STARTMENU_S<screen>`, handles no `_LHSHELL_START` on a window of its own, and does not look at `_LHSHELL_S<screen>`. So
  luxshell starts it with `FLXP_START_MENU=panel FLXP_START_KEY=1` (`classic`/`1` for flxp-classic) and the bridge is the root
  itself: luxwm's broadcast reaches flxp-taskbar directly, and luxshell (startmenu.lua "start-key") ignores every root
  `_LHSHELL_START`, with or without a rect, and closes its own menu, while the classic bar it started or adopted runs with its
  own menu and no provider owns `_LH_STARTMENU_S`. A forward to the dock window, as first planned, would reach nobody (the
  message would go to its Wine-side connection, which does not handle it). Result: one menu per Super press; a second Super
  closes flxp's panel (its own toggle). luxshell's menu is the fallback only when the bar is gone. luxshell has no other start
  request in classic mode (the Avalon Start button is unmapped). Checked: Super and the Start button open flxp's panel, the
  luxshell log shows no start-menu activity, `classic_start_menu = lhshell` still gives luxshell's menu on flxp's button, avalon
  Super opens luxshell's menu.
- **For the flxp team** (what their side still needs for the provider protocol above, none of it blocking now):
  (1) own `_LH_STARTMENU_S<screen>` while its own menu is in use (`start_menu = panel|classic`) and answer `_LHSHELL_START` on
  that owner window (flags bit 0 toggle, `l[1..4]` the anchor rect; luxshell forwards the Avalon Start button and Super there,
  which makes flxp's panel usable with the Avalon taskbar too); (2) once it owns it, ignore the root broadcast while
  `_LHSHELL_S<screen>` has an owner (luxshell forwards it; answering both would toggle twice, since with `start_key = 1` today
  it answers every root message); (3) read the root message's data: a rect in `l[1..4]` should anchor the panel and `l[0]`
  bit 0 decide toggle vs open (today data is ignored); (4) a way from its panel's Control Panel to luxshell's settings when
  luxshell runs (root `_LHSHELL_COMMAND` = `settings`), so the sidebar/taskbar choice is reachable in classic mode; (5) the
  tray items (4)-(6) of the list above are unchanged.

#### Status (2026-10-25, evening): lux on the shared settings store, presets, flxp's start panel everywhere
lux is the shell's name from here on (directory and binary stay `luxshell`). Verified on the lab `:4` (luxwm + picom xrender,
flxp-taskbar of flxp 6861d31 from PATH, a scratch `XDG_CONFIG_HOME`). User docs: USAGE.md "Settings and presets"; protocol:
SETTINGS.md ("Resolved theme", "lux's migration").
- **Settings store.** session.lua: `S.get/S.has/S.set` on `$XDG_CONFIG_HOME/longhorn/settings.ini` (cached by mtime+size; `S.set`
  edits the one line in place, keeps unknown keys, comments and order, does nothing when the value is unchanged, and queues one
  `_LH_SETTINGS_CHANGED` per handled event: serial = highest seen + 1, data[1] the section atom when only one changed).
  lhshell.c: `lh.send_root_message`, `lh.atom`, event `settings-changed`(serial, hint name) for the root message. runtime.lua
  `reload_settings` (50 ms debounce) re-reads the file and applies every key that differs from what runs: theme, composition,
  taskbar kind (unless `LHSHELL_TASKBAR`), autohide, rows, sidebar mode/width/with_classic, clock pinned and layout (event
  `clock-layout`), wallpaper/mode, desktop_black, max_fade_ms, start menu; the open settings flyout is rebuilt. `state.ini`
  keeps `[sidebar] tiles` only. Migration (`S.migrate`, once, before anything else): checked with a copy of the real state.ini:
  13 keys moved, `state.ini.pre-settings` written, state.ini left with the header comment and `[sidebar] tiles`.
- **Presets.** `S.resolve(name)`: registry (name, alias, path), else `themes/NAME.theme` over its base (`.over` = the keys it
  sets; `shell` may be a pack name). The settings flyout has two columns now: themes, "Your themes", *Save as preset...* | the
  quick switches plus *Start menu*. Theme changes go through `apply_theme`: pack/start panel/fonts/wallpaper/compositor, write
  `[theme] name`, `_LH_THEME_RESOLVED`, `_LH_THEME` = base, then the message. Own `_LH_THEME` writes are counted and their
  PropertyNotify ignored (so a preset is not undone by its base coming back). Checked: `_LH_THEME = slatejade` (Jade base, Slate
  pack, luna-olive panel, ascent wallpaper, animations 4074) -> `_LH_THEME` jade4074, luxwm `_LHWM_THEME` jade, lux the Slate
  pack, resolved name slatejade; Save as preset "My Luna" -> `themes/my-luna.theme` (base luna) selected; lux restarted (with and
  without `_LH_THEME_RESOLVED` on the root) keeps my-luna; `_LH_THEME = luna` then switches to the registry luna, `jade` to
  jade4074.
- **`[start] menu = flxp | lux`** (default flxp). flxp in avalon/none: `flxp-taskbar --start-menu-only` started (or adopted,
  /proc cmdline), restarted on death (3 s check, at most 5 per minute), stopped by pid (comm + DISPLAY checked) on the switch
  to classic, to lux, and on exit; a new flxp-taskbar is started only after `_LH_STARTMENU_S0` has no owner (one that finds it
  owned stays no provider). classic: flxp-taskbar as before, `classic_start_menu` defaulting from `[start] menu`. lux: no
  forwarding at all. The root-message bridge for a non-provider classic bar (`lh.classic_owns_start_key`) is gone.
  Verified with real Super (luxwm) and clicks: avalon: Super opens flxp's panel above the Avalon Start button ("forwarded ...
  anchor 0,770 83x29"), Super again closes it, the Start button opens it, a second click closes it (the panel's grab takes
  that click); classic: Super open/close, flxp's Start button open, Escape close; none: Super open/close; `menu = lux` (set by
  editing settings.ini + the message, as flxp's dialog will): avalon Super/Start button open lux's menu, classic Super and
  flxp's Start button open lux's menu (`FLXP_START_MENU=luxshell`). Always exactly one of the two menus mapped. Theme switch
  aero -> luna while flxp's panel was open: the panel closed, reopened in Luna; no errors. `_LHSHELL_COMMAND settings` opens the
  flyout.
- **Maximised mode** `[shell] max_fade_ms` (default 0): the taskbar/sidebar black layers switch in the same repaint (checked:
  the bar is dark 80 ms after Super+Up); a number restores the fade. The `desktop_black` desktop layer was already instant.
- **For luxwm (next task):** read `_LH_THEME_RESOLVED` (frames from its `msstyles`/`color`/`size`/`frame`/`frame_classic`,
  `animations`); today a preset gives the base's frames.
- **For the flxp team:** (1) Control Panel > *Display Properties* in the start panel is a message box (startmenu.cpp
  `CMD_DISPLAY`); until the dialog exists it could set the root `_LHSHELL_COMMAND` = `settings` when `_LHSHELL_S<screen>` is
  owned (lux's flyout opens). (2) The dialog: write settings.ini as SETTINGS.md says, then `_LH_THEME`, then
  `_LH_SETTINGS_CHANGED`; lux applies the rest. (3) Its Theme cascade lists only the registry: add `themes/*.theme`. (4) Read
  `_LH_THEME_RESOLVED` instead of looking up `_LH_THEME` (a preset's `msstyles`/`color`/`size`/`icons`); today flxp briefly sees
  a preset name on `_LH_THEME` when somebody writes one and logs "not in themes.ini; keeping ..." before lux replaces it with
  the base. (5) flxp-desktop: `[desktop] wallpaper = none` means no image.

#### Status (2026-10-25, late): in sync with flxp's Display Properties (flxp bf79f78)
Decision (SETTINGS.md "Preset names on `_LH_THEME`"): `_LH_THEME` carries the selected name, registry or preset; every reader
resolves it (registry section/alias first, then `themes/NAME.theme` = base entry + overrides); nobody writes a preset's base
back. Verified on the lab `:3` with the real dialog (`~/src/flxp/build/settings/flxp-settings`) next to luxwm + lux + picom
(xrender), a scratch `XDG_CONFIG_HOME` (empty `longhorn/`, a copy of the user's `flxp/flxp.ini`), driven by xdotool; contact sheet
`lhshell/compare_settings_sync.png`.
- **luxwm** resolves preset names itself (frame.c `theme_preset_lookup`, wm.c `lh_theme_apply`): `_LH_THEME_RESOLVED` when its
  `name` is the preset, else the preset file; `[VisualStyle]`, `[Frames]`, `[Fonts]`, `[Composition] composition` (off = the
  classic frame) over the base entry, applied like a registry entry; the preset's `[Composition] animations` wins over
  `[theme] animations`; `_LHWM_THEME` unchanged (the spec); `_LH_THEME_RESOLVED` changes re-apply; an unknown name keeps the
  theme. luxwm README "EWMH and the shell". `tests/regress.sh`: 144 identical.
- **lux** writes the selected name (preset too) to `_LH_THEME`, `_LH_THEME_RESOLVED` first as before; a preset name written by
  someone else is applied and left alone (checked with `xprop -spy`: flxp's Apply of `test1` -> no write by lux or luxwm). The
  startup compat rule (root = base, resolved + `[theme] name` = the preset) only serves sessions from before the decision.
  `_LH_SETTINGS_CHANGED` serials now go through the root `_LH_SETTINGS_SERIAL` like flxp's (flxp's next message continued at
  lux's 719187625 + 1). `lh.select_theme(name, force)`: Save as preset over the selected preset republishes.
- **Dialog steps** (all live, nothing restarted): Jade (4074) -> luxwm jade frames, dialog restyled; Composition Off -> lux stops
  picom, luxwm `jade4074-classic`; Animations Invented -> `_LHWM_ANIMATE` 7 -> 3 on clients; Taskbar Classic -> lux stops its
  bar, sidebar and the `--start-menu-only` provider, starts flxp-taskbar; Save As `test1` (icons xp) + Apply -> `_LH_THEME =
  test1`, luxwm "theme test1: preset (base jade4074)", lux "theme test1 (preset over jade4074)", resolved `icons=xp`; back to
  Aero/Avalon/Follow/4074plus in one Apply (hint 0) -> picom started by lux, glass frames, `_LHWM_ANIMATE` 7, the provider back;
  `test1` from Aero -> jade classic frames, picom stopped, animations 3 from the preset; lux and luxwm restarted with `test1`
  on the root keep it.
- **For the flxp team:** (1) `resolved_for()` (lhtheme.cpp) treats `_LH_THEME_RESOLVED` as a bare section name; it is
  `key=value;...` (SETTINGS.md "Resolved theme"): take its `base=` (unescaped), and only when its `name=` equals `_LH_THEME`.
  Harmless today (the lookup fails and flxp reads the preset file), but the "preset file we cannot read" fallback never works.
  (2) A preset's `[Composition] animations` (Save As always writes it) wins over `[theme] animations` in lux and luxwm, so the
  Appearance tab's Animations has no visible effect while such a preset is selected: write it into the preset (or leave it out
  of Save As unless it differs). Same for `[Desktop] icons`. (3) Each Apply with a theme change writes `_LH_THEME` twice (same
  value; two PropertyNotify seen with `xprop -spy`, none of them from lux or luxwm): harmless, but a single write is enough.
  (4) Choosing a theme writes `[desktop] wallpaper =` (empty) even when the key was absent, so the theme change's hint is 0;
  fine for us, just noting why the hint is rarely `theme`.

### M5b, note for flxp-desktop (2026-10-25)
The "full-screen black fade on maximise/restore, even without composition" that aap still sees
comes from **flxp-desktop**: `src/desktop/desktop.cpp` fades the whole desktop to black over
`desktop_fade_ms` (400 ms, smoothstep) while `_LHWM_MAXIMIZED_MODE` is 1, copied from lux's old
M5a desktop layer. M5b replaced that: the desktop does not fade at all; only the maximised
window's frame plate and the bars' black layers switch (and since 2026-10-25 instantly, no fade).
flxp-desktop should default `desktop_fade_ms` to 0 (or drop the behaviour) to match. Our side
has nothing left that fades: lux `desktop_black = 0`, `max_fade_ms = 0`; luxwm `MAX_FADE_MS 0`.

## Context menus and pack reload (status 2026-10-26)

**Sources.** The only real screenshot of a sidebar menu is `screenshots/lh4074-091929.png` (4074, Slate; described with the
rest in `screenshots/README-sidebar-shots.md`): *Minimize bar*, *Properties* (hot), separator, the parts list *Basket, Classic
Tray ✔, Contacts, News Feed, Notification History ✔, People nearby, Quick Launch ✔, Search ✔, Slide Show, Sync*, separator,
*Power (Transient) ✔, Web Host Tile (Transient) ✔*, separator, *Clock ✔*; no mnemonics drawn. *Properties* opens
"Taskbar and Start Menu Properties" with the *Sidebar & Taskbar* tab (`lh4074-091944.png`). No tile menu and no Avalon
taskbar menu was captured; their items come from the IL (`res/4074/il/desktop.il`):
- `SidebarBaseMenu2.GetBaseContextMenu` (L17037): "Minimize bar" / "Restore bar" (command ToggleMinimized → message 1381
  `_ToggleSidebar`), "Properties" (message 1379).
- `SidebarControl.GetContextMenu` (L37721): the base items, a separator (`MenuItem.Mode` 2), `GetAddPartsMenu` (Friendly
  Names, checked when the module has a tile, " (Multi-Instance)"; then the transient modules with " (Transient)"), a
  separator, "Clock" (ToggleClock, checked while the clock tile is shown). `_OnOptions` (L38583): the Options button opens
  the same menu with itself as PlacementTarget.
- `OuterTile._GetContextMenu` (L44524): `FireContextMenuEvent` first (the tile's own `MenuItem`s, `ISidebar.ContextMenuEvent`),
  then "Auto Size" (checkable, `_fAutoSize`), "Move Up" (enabled unless `_iIndex` = 0; "Move Left" in the taskbar), "Move
  Down" (enabled while `_iIndex + 1 < _cTiles`; "Move Right"), in the overflow "Remove <name> Tile from overflow", "<Friendly
  Name> Tile Properties" when the tile has a `TypeOfPropertyPage`, "Remove <Friendly Name> Tile" unless pinned. No separators.
- `ExtendedTaskbar.GetContextMenu` (L23627): the base items (4074 passes constants, so the taskbar always said "Restore bar"),
  separator, "Task Manager", separator, "Show Desktop" / "Hide Desktop" (`_fDesktopVisible`), "Cascade", "Tile Vertically",
  "Tile Horizontally" (enabled when there are windows).

**lux** (`runtime.lua` "sidebar and tile context menus", `menu.lua`):
- `menu.lua` style `"avalon"` measured on `lh4074-091929.png`: 1 px frame (`T.menu.border`) plus a 1 px drop shadow, 1 px
  inner padding, 19 px rows with an 18 px highlight (`T.menu.hot`, top row 30 % and bottom row 40 % lighter, as #F2C49A /
  #EDB078 / #F4CEAB there), text 17 px from the frame, 7 px separators with a 1 px `#BEBEBE` line, ✔ check marks, width text +
  29 px (152 px for that list in Tahoma 8: equal to the reference). `opts.flip`: TrackPopupMenu placement (the menu opens to
  the left of / above the point where it would not fit; the screenshot's menu is flipped that way). Mnemonics: `&` in item
  text, drawn underlined through Pango markup (text node `markup = true`, `scene.c`); the key activates a unique mnemonic or
  cycles through duplicates; other letters jump by initial; Home/End added. 4074 drew no underlines (a deviation asked for).
- Sidebar menu: right click on the sidebar outside the tiles, or a click on the Options glyph (under it, right-aligned). Items:
  4074's two base items; lux's group *Lock the Sidebar* (`[sidebar] locked`, the 4051 `CShellSidebar::SetLockBar` idea: the
  sizer stops dragging) and *Stacking* ▸ Top / Bottom; the 4074 parts list in the screenshot's order, where *Classic Tray* =
  `tiles/notifications.lua`, *Power (Transient)* = `tiles/power.lua`, *Clock* = `tiles/clock.lua` (checked = in the tile list;
  a click adds or removes it), the other modules greyed. Instead of the requested "Add Tile ▸" submenu the list stays inline as
  in 4074, and *Minimize bar* is 4074's wording for "Minimize Sidebar".
- The tile list is state.ini `[sidebar] tiles` (`none` = empty) with `[sidebar] tiles_base` = the pack's own list it was made
  from: while the pack's list is unchanged the user's list holds (added and removed tiles), otherwise the pack's list wins (an
  old state.ini without `tiles_base` keeps the old rule: used when it names the same tiles). A tile comes back at its place in
  the pack's order (else notifications, power, clock).
- Tile menu: right click on a tile; a click on the tile's arrow opens the flyout when the tile has one, else this menu. The
  tile's `tile:menu()` items, a separator (ours), then *Auto Size* (checked, greyed: our tiles size to their content), *Move
  Up* / *Move Down* (greyed at the ends and for pinned tiles), *<Name> Tile Properties* (`tile:properties()`, else the tile's
  flyout; greyed without either: the tray tile), *Remove <Name> Tile* (not on pinned tiles). Friendly names: Classic Tray,
  Power, Clock. `tiles/clock.lua` got `tile:properties()`: a flyout with the six layouts and the pinned switch.
- Taskbar menu: the IL's items with the sidebar's real state, *Properties* = the settings flyout, *Task Manager* (first of
  xfce4-taskmanager, lxtask, gnome-system-monitor, mate-system-monitor, qps, else `xterm -e top`; greyed when none; found
  with `io.popen`, since `os.execute` reports -1 under lux's ignored SIGCHLD), *Show/Hide Desktop* = `_NET_SHOWING_DESKTOP`
  (luxwm implements it), *Cascade* / *Tile Vertically* / *Tile Horizontally* through `_NET_MOVERESIZE_WINDOW`
  (`lh.wm_moveresize`, new in `lhshell.c`) over the work area (sizes are client sizes, luxwm adds its frame); then lux's
  *Auto-hide the taskbar* and *Sidebar* ▸ (replacing the old "Sidebar: Full/Minimized/Pinned" rows and "Settings...").
- Checked on `:4` (aero-patch, slate4074): both stackings, classic taskbar + `with_classic`, minimized (Restore bar from the
  taskbar menu), keyboard (arrows, mnemonics, Esc, submenus), outside clicks; `x11/lhshell/compare_menus.png`.

**Pack reload** (`lh.reload_pack`, USAGE.md "Live-editing a theme"): `_LHSHELL_COMMAND reload`, or `_LH_THEME` written again
with the current name (only writes that are not our own echo), debounced to one reload per 150 ms. It re-resolves the theme
(themes.ini / preset), re-reads `shell.lua` (a failing pack keeps the old one), re-applies start panel and fonts, calls
`lh.image_flush()` (new in `scene.c`: cache entries whose file's mtime or size changed are dropped; the old surfaces stay
allocated on a stale list because nodes borrow them), then `rebuild_shell()` (tiles re-read from `tiles/*.lua`, "pack" event:
flyout chrome, start menu, menu caches) and the theme-entry steps of a switch (wallpaper, composition, taskbar kind, start
provider, `_LH_THEME_RESOLVED`). ~40 ms on the lab. `session/theme-reload.sh` sends it plus luxwm's reload (`luxwmc reload-theme`,
else the `_LHWM_RELOAD` root ClientMessage); `session/theme-watch.sh` calls it on file changes (inotifywait, else a 1 s poll).

## Text through libuxtheme, provider start state, text on glass (status 2026-10-27)
aap's items: "lux start button does not know when the wux start menu is open", blurry taskbar text, taskbar text on glass hard
to read in Aero vision.
- **Start button and a provider's menu** (`docs/CLIENT-PROTOCOL.md` "Requests to the wux side"): lhshell.c emits
  `startmenu-open` on root `_LH_STARTMENU_OPEN` changes; runtime.lua shows StartButton.FakePressed (the pack's 1181, the
  `checked` state) while our menu is open or the property is 1. A provider that never set it (the property absent since the
  forward): pressed from the forward (`start-forwarded`, emitted by startmenu.lua for the button and Super) until the first
  top-level mapped within 1.5 s (its popup; the `toplevel` event now carries the window and map / unmap / destroy) unmaps, until
  the next forward (a click on the pressed button closes the provider's menu), or after 1.5 s without a map. Any change of the
  property drops that guess (the provider speaks the protocol). Checked on `:4` (aerovision, flxp-taskbar --start-menu-only
  without the property: pressed while its panel was open, released on Esc; `xprop` 1 / 0 / deleted by hand).
- **Text** (`luxshell/text.c`, replacing pango in `scene.c` and `lhshell.c`; lux links libuxtheme as luxnav does,
  `../(gone: the top-level build)`, plus fontconfig and freetype2; no pango): every `text` node is libuxtheme's GDI text, as luxnav's
  (NAVBAR.md "Text and pixels without pango and cairo"): the pango-style descriptions stay (`Segoe UI 9`, `Segoe UI,Tahoma Bold
  14px`, `ITC Franklin Gothic Std Medium 15px`, ...) and become a `ut_font` (first family that exists, theme font directories
  first as libuxtheme looks them up; weight words incl. Medium = 500; points at 96 dpi; lfHeight = -px rounded); each line is one
  `ut_draw_text_mask` (DT_SINGLELINE | DT_NOPREFIX, DT_END_ELLIPSIS for `ellipsis`) composited with cairo in the node colour at
  whole device pixels; widths are GDI advances (`ut_text_extent_font`, cached); `wrap` is greedy at blanks with GDI widths (a
  long word between characters, pango's WORD_CHAR); `markup` keeps the menus' mnemonics: tags removed, the underline drawn at
  GDI's underline rows (those `lfUnderline` draws) under the prefix's extent; `glow` = the coverage blurred (two box passes,
  node keys `glow_size` 4, `glow_strength` 2.5) in the glow colour, `shadow` = the coverage offset. The line box is the font's
  hhea ascent / descent at the size, rounded up (what pango laid out with): GDI's cell sits on that baseline, so nodes keep their
  sizes and baselines; for Segoe UI and Tahoma it equals GDI's, Franklin Gothic's winAscent is 2-3 px taller than its hhea
  ascent. A string with characters the face lacks (`✓`, `●`, `▸` in Segoe UI 0.7) is drawn in fontconfig's face for them (GDI
  has no per-glyph fallback). `lh.fonts` registers the theme directories with libuxtheme too (`ut_add_font_dir`; it only adds).
  `LHSHELL_TEXT_TRACE=FILE` logs every painted text node (rect, block origin, baseline).
  Equivalence (`:4`, aero-patch and slate4074, old = `7c9413a` with pango, same scenes: task buttons incl. an ellipsized title,
  the sidebar clock and power texts, the start menu (Jade; Slate with the Franklin user pane), the taskbar menu with mnemonics, a
  wrapped toast; 60 nodes): every node rect identical except Slate's auto-sized "Log Off" (2 px wider, GDI's advance), every
  baseline within 0.5 px (pango's centred blocks sat on half pixels), block x identical; text widths +-2 px. Ellipsis is GDI's
  ("A very long window ..." keeps the blank). `lhshell/compare_text_gdi.png`: task buttons, clock, menu, toast at 4x, pango
  above GDI.
- **Text on glass** (pack key `taskbar_text = plain | glow`, docs/history/THEMES.md "luxshell packs"): `glow` draws the task buttons' titles
  and the minimized sidebar's taskbar clock black over a white halo (luxnav's treatment; `taskbar_glow = { color, text, size,
  strength }`). New pack `themes/aerovision` (`aero` + `taskbar_text = "glow"`, `clock_pack = "aero"` so the clock's pinned
  choice made in aero holds), used by themes.ini `[aerovision]`; `aero` keeps `plain` (white).

