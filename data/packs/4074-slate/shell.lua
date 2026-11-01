-- Slate_*Resources pack: same styles, Slate imageres ids (resmap aero_to_slate), SHELL-DESIGN 2.2.2 (ids in brackets)
-- Selected by the session theme: a theme's `[Shell] pack = 4074-slate`, SHELL-DESIGN 6.1;
-- slate4051 has its own pack (packs/4051-slate: this table with the 4051 differences).
return {
  name = "4074-slate", family = "slate", font = "Tahoma 8",   -- family slate: Slate's opaque flyouts, toasts and borders
  -- the task buttons' icon for a window without _NET_WM_ICON or WM_HINTS icon: Explorer falls back to the application icon
  -- (IDI_APPLICATION, shell32 #3: packs/4074/shell32, lhde-import / shellpack.sh); luxwm's DWM captions use CompositedWindow DEFAULTICON instead
  default_icon = "4074/shell32/16/3.png",
  ids = { [1178]=1000, [1187]=1155, [1176]=1103, [1177]=1104, [1199]=1028, [1200]=1030, [1201]=1108, [1173]=1105, [1174]=1106, [1175]=1109,
          [1179]=1156, [1180]=1157, [1181]=1158, [1182]=1150, [1183]=1154, [1184]=1151, [1185]=1152, [1170]=1159, [1171]=1160, [1172]=1161, [1167]=1162 },
  -- 4074 look (builds after ~4060): HackImages magnify nearest-neighbour (the blocky start flag of lh4074-181533.png: 1156
  -- 74x23 drawn 79x29 nearest fits the shot with a mean squared error of 53 per pixel, bilinear 250); slate4051 is bilinear
  filter = "nearest",
  -- 30 per row as in Aero (GetTaskbarHeight = 30 x RowCount): lh4074-181533.png rows 834..863. 1155 (50x28) is stretched
  -- whole to 30 rows, nearest: the shot repeats rows 7 and 21 (fit error 1; with a 7 px top grid 80)
  taskbar = { height = 30, bg_grid = {0, 0, 0, 0} },
  start = { w = 83, h = 29 }, start_dy = 0, taskbar_dy = 0, start_alpha = 1,                             -- StartButton Width 83 Height 29, Border(0,0,4,0): 1156 74x23 drawn 79x29
  taskbutton = { max_w = 140, margin = { 0, 2, 2, 1} },
  -- 150 wide (Sidebar\Width default; 4051-vm-now.png and the lh4074-18* shots: the bar starts at 1152 - 150); 1000 149x20,
  -- no grid (stretched). SidebarSizer (Slate): TileThumb Width 6, Dock Left, 1108 (6x20, white 72/120/101/66/37/16 alpha)
  -- visible over the background: it turns 1000's dark column 0 into the grey/white edge of the references (112/161/148/...)
  sidebar = { width = 150, bg_grid = {0, 0, 0, 0}, sizer = { w = 6, image = 1108 } },
  tile_bg = 1000,                                         -- Slate repaints 1000 under each tile
  -- 4074 Administrator SidebarTiles: Search, Notification History, Quick Launch, Classic Tray (+ the transient Power, the pinned
  -- Clock): lh4074-193747.png top to bottom (no Search tile in luxshell yet)
  tiles = { "notifyhistory", "quicklaunch", "notifications", "power", "clock" },
  pinned_tiles = { "notifications", "clock" },            -- sidebar modes pinned / minimized (SHELL-DESIGN 6.4)
  taskbar_tiles = { "notifyhistory", "quicklaunch", "notifications", "clock" },   -- minimized: 4074 TaskbarTiles (all five)
  -- popup menus: the 4074 Slate [sysmetrics] FLATMENUS colours (MENU, MENUTEXT, MENUHILIGHT, GRAYTEXT)
  menu = { bg = "#ffffff", text = "#373738", hot = "#edb078", hot_text = "#000000", border = "#969696", gray = "#969696" },
}
