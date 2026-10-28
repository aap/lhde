-- Slate_*Resources pack: same styles, Slate imageres ids (resmap aero_to_slate), SHELL-DESIGN 2.2.2 (ids in brackets)
-- Selected by the session theme: themes.ini `shell = packs/4074-slate` (slate4074, and the Luna themes' bars), SHELL-DESIGN 6.1;
-- slate4051 has its own pack (themes/slate4051: this table with the 4051 differences).
return {
  name = "slate", font = "Tahoma 8",
  -- the task buttons' icon for a window without _NET_WM_ICON or WM_HINTS icon: Explorer falls back to the application icon
  -- (IDI_APPLICATION, shell32 #3: packs/4074/shell32, lhde-import / shellpack.sh); lhwm's DWM captions use CompositedWindow DEFAULTICON instead
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
  tiles = { "notifications", "power", "clock" },     -- M4b: tray tile, power tile (lh4074-181415.png order: tray/QL above power), pinned clock
  pinned_tiles = { "notifications", "clock" },            -- sidebar modes pinned / minimized (SHELL-DESIGN 6.4)
  -- popup menus: the 4074 Slate [sysmetrics] FLATMENUS colours (MENU, MENUTEXT, MENUHILIGHT, GRAYTEXT)
  menu = { bg = "#ffffff", text = "#373738", hot = "#edb078", hot_text = "#000000", border = "#969696", gray = "#969696" },
  -- start menu (6.3): the Slate StartPanel class (packs/4074/startpanel-slate); measured on lh4074-181541.png (1152x864):
  -- 380 px wide, y 288..835: user pane 64 (USERPANE + USERPICTURE frame, name in Franklin Gothic Medium 14 with a 2,2
  -- shadow), body 441, log off row 42; same columns and tiles as Jade with 30 px places rows (Tahoma 8).
  startpanel = "4074/startpanel-slate",
  startmenu = { font = "Tahoma 11px", width = 380, left_w = 190, body_min = 441, prog_tile = 40, places_h = 30, sep_left = 8, sep_right = 4,
                more_h = 34, logoff_h = 42, userpane_h = 64, frame = { "#5f6769" }, logoff_buttons = { "logoff", "shutdown" },
                hot_bg = "#edb078", hot_text = "#000000", user_font = "ITC Franklin Gothic Std Medium 19px" },
}
