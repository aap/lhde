-- 4042-plex pack: the Avalon taskbar and sidebar of Longhorn 4042 main (theme plex4042). Every value below is read from 4042 main
-- itself: Microsoft.Windows.Client 6.0.3708 (Application1.Taskbar, Application1.Tile, Application1.Sidebarstylesheet_themed,
-- System.Windows.Desktop.SidebarElement / FlyoutElement / SideBarClock), System.Windows.Explorer 6.0.3708 (Taskbar, TaskList,
-- TaskPresenter, StartButton, Tile, ImageResource), the Administrator hive and plex.msstyles' `Sidebar` class. Extracted for
-- reading in ~/fun/longhorn/wim/4042main/2 (the ISO's install.wim image 2).
-- What drew the bars: 4042 main runs the managed "new taskbar" (default since build 4015, BetaWiki 4015 main) beside the managed
-- sidebar, both System.Windows.Desktop.StartBar; the Win32 tray (Shell_TrayWnd, uxtheme TaskBar/TaskBand/Start::Button) is not
-- used by default (no StartBar\Taskbar key; StuckRects2's 30 px rect is the unused classic tray's). Both bars are skinned only by
-- ThemeImage('Sidebar', part, state) of the msstyles (4042 main's Sidebar BLOBs are bare PNGs; the margins are the stylesheet's
-- NineGridMargin values) and Franklin Gothic Medium text (framd.ttf, imported from 4042 main).
-- Images: packs/4042/imageres (data/sources/4042main.ini [packs]): plex-sidebar-<part>[-<state>].png = the Sidebar parts unmodified
-- (recipe plexbars), plex-taskbar.png = BACK transposed, plex-clear.png = nothing; NNNN.png = 4042 main's imageres (clock, tray).
-- Sizes in px: the stylesheet's BoxUnits are pixels (unit 0). Font descriptions name Franklin Gothic Medium with a fallback after a
-- comma: text.c would otherwise read "Medium" as the weight (family "Franklin Gothic", not found).
local S = function(part) return "plex-sidebar-" .. part end
local P = {
  name = "4042-plex", build = 4042, imageres = "4042/imageres",
  font = "Franklin Gothic Medium,Tahoma 12px",       -- the stylesheet's tile, task, notify and PIDL text: Franklin Gothic Medium 12, white
  filter = "bilinear",                        -- Avalon 6.0.3708 draws images through MIL scaling (not nearest); not measured
  default_icon = "4042/shell32/16/3.png",     -- IDI_APPLICATION of 4042 main's shell32 (packs/4042/shell32)
  -- the Aero imageres ids runtime.lua asks for -> the Plex Sidebar parts
  ids = {
    [1187] = "plex-taskbar",                  -- TaskbarBackground2: ThemeImage(Sidebar, 2 BACK), Rotate 1 (bottom bar), NineGridMargin 5,0,5,0
    [1178] = S("back"),                       -- SidebarBackground: ThemeImage(Sidebar, 2 BACK), NineGridMargin 5,0,5,0, no Flip on the right
    [1170] = S("barup-normal"), [1171] = S("barup-hot"), [1172] = S("barup-pressed"),   -- TaskbarToggleBar, BarVisible: 29 BARUP 1/2/3
    [1167] = S("bardown-normal"),             -- BarVisible false: 30 BARDOWN 1
  },
  toggle_minimized = { hot = S("bardown-hot"), pressed = S("bardown-pressed") },
  toggle = { slot = 17, x = 0 },              -- TaskbarToggleBar: Dock Right, SizingMode 2 (true size 17x17), centred; no padding
  -- the bar: TaskList.GetHeight = 24 x rows (Taskbar.GetTaskbarHeight on a Rotated bar: GetTaskbarRowSize x _cRows); the 30 px of
  -- StuckRects2 is the classic tray's rect. BACK transposed (column 0, (75,117,177), on the top row), 5 rows kept at each end.
  taskbar = { height = 24, bg_grid = {5, 0, 5, 0} },
  taskbar_text = "plain",
  -- StartButton (System.Windows.Desktop.StartButton .ctor): StackImageResourcePresenter { StartButtonImage Dock Left =
  -- ThemeImage(Sidebar, 9 STARTBUTTON) 28x22, SizingMode 3, top left; StartButtonText Fill "start", Franklin Gothic Medium 14,
  -- weight 400, white, Padding 0,4,0,4 }; on the Rotated bar Dock Left, no Height (the bar's 24); no image at rest, PointerFocused
  -- / Focused = TILEHEADERBACK 3 (hot), FakePressed (menu open) = TILEHEADERBACK 1
  start = { w = 70, h = 24 },                 -- w is replaced by the measured flag + text (runtime.lua start_composed)
  start_compose = { flag = S("startbutton"), flag_w = 28, flag_h = 22, text = "start", font = "Franklin Gothic Medium,Tahoma 14px",
                    color = "#ffffff", text_pad = {0, 4, 0, 4},
                    normal = "plex-clear", hot = S("tileheaderback-hot"), checked = S("tileheaderback-normal") },
  start_dy = 0, taskbar_dy = 0, start_alpha = 1,
  -- TaskItem: Height 24; no image at rest; PointerFocused TASKBACK 2; Active TASKBACK 3 (NineGridMargin 4,4,4,4); Active +
  -- PointerFocused TASKBACK 4; Flash TASKBACK 5 (4,4,4,4); Pressed TILEHEADERBACK 1. TaskItemIcon 28x24, Padding 6,4,6,4;
  -- TaskItemText Padding 0,5,4,5, Franklin Gothic Medium 12 white. Width (TaskPresenter.OnUpdate): the row's width shared by
  -- min(items per row, floor(width / 130)) columns. The hot state has no NineGridMargin in the sheet (stretched whole); one grid
  -- of 4 for all states here (the 109x24 plates differ only in their 4 px edges).
  taskbutton = { fill_w = 130, margin = {0, 0, 0, 0}, grid = 4, content = {6, 4, 4, 4}, icon_margin = {0, 0, 6, 0},
                 font = "Franklin Gothic Medium,Tahoma 12px",
                 images = { normal = "plex-clear", hot = S("taskback-hot"), pressed = S("tileheaderback-normal"), checked = S("taskback-selected"),
                            hotchecked = S("taskback-selectedhot"), flash = S("taskback-flash") } },
  -- the sidebar: Administrator StartBar\Sidebar Width = 150. No sizer element in SidebarElement (the width changes through the
  -- appbar edge); luxshell keeps its invisible 2 px drag strip.
  sidebar = { width = 150, bg_grid = {0, 5, 0, 5}, sizer = { w = 2, image = nil },
              -- SidebarTitle Height 27 (TITLEBACK) with the SidebarOptions button (FLYOUTOPTIONSBACK 2/3) and its glyph
              title = { h = 27, image = S("titleback"), normal = "plex-clear", hot = S("flyoutoptionsback-hot"), pressed = S("flyoutoptionsback-pressed"),
                        glyph = S("flyoutoptionsglyph-normal"), glyph_w = 9, glyph_h = 6, glyph_pad = {4, 0, 4, 0} } },
  -- tiles (runtime.lua plex_tile_chrome): TileBackgroundImage TILEBACK; TileFadeButton Width 16 with TileSideButton TILEHEADERBACK
  -- 1 / 3, pressed TILECHEVRONBACK 2, faded in 300 ms / out 500 ms; TileSizer Height 7, TILESIZER 1 (NineGridMargin 1,0,0,0) / 2 (none)
  tile_chrome = { back = S("tileback"), side_w = 16, side = S("tileheaderback-normal"), side_hot = S("tileheaderback-hot"),
                  side_pressed = S("tilechevronback-pressed"), sizer = S("tilesizer-normal"), sizer_hot = S("tilesizer-hot"),
                  sizer_h = 7, sizer_grid = {0, 0, 0, 1}, fade_in = 300, fade_out = 500,
                  icons = { clock = 1016 } },          -- SideBarClock.Initialize: set_Icon(imageres ClockIcon 1016); other tiles' icons not read
  -- the flyout (FlyoutElement): FLYOUTTOP 27 / FLYOUTCONTENT / FLYOUTBOTTOM 26, NineGridMargin 2,2,0,6 (l t r b)
  flyout = { top = S("flyouttop"), top_h = 27, top_grid = {2, 0, 6, 2}, content = S("flyoutcontent"),
             bottom = S("flyoutbottom"), bottom_h = 26, bottom_grid = {2, 0, 6, 2},
             opt_hot = S("flyoutoptionsback-hot"), opt_pressed = S("flyoutoptionsback-pressed"),
             glyph = S("flyoutoptionsglyph-normal"), glyph_w = 9, glyph_h = 6, glyph_pad = {4, 0, 4, 0},
             font = "Franklin Gothic Medium,Tahoma 14px" },
  -- Classic Tray (NotificationArea): NotifyButtonIcon 28x24, Padding 6,4,6,4 (the buttons' Franklin Gothic text has no XEmbed source)
  tray = { icon = 16, pitch = {28, 24}, origin = {6, 4} },
  -- the Administrator's tiles (NTUSER StartBar\Sidebar Module0000..0003): Search, Quick Launch, Clock, Classic Tray; luxshell has no
  -- Search tile. The clock is a tile in the list (SideBarClock, 4029-era 179 px set): tiles/clock.lua clock4042.
  tiles = { "quicklaunch", "clock", "notifications" },
  pinned_tiles = { "notifications", "clock" },        -- luxshell's pinned/minimized modes (no 4042 counterpart)
  taskbar_tiles = nil,
  clock_pinned = false, clock_face = "plex", clock_layers = "full",
  -- popup menus: plex.msstyles [sysmetrics] FLATMENUS colours (MENU 58,106,170, MENUTEXT white, MENUHILIGHT 131,182,234, GRAYTEXT
  -- 147,185,220); the border is not in the style (darker MENU, a guess). The managed context menus' own look is not decoded.
  menu = { bg = "#3a6aaa", text = "#ffffff", hot = "#83b6ea", hot_text = "#ffffff", border = "#2b5286", gray = "#93b9dc" },
}
return P
