-- Aero_*Resources pack (imageres ids 1167-1201), SHELL-DESIGN 2.2.2 / 2.3.2
-- Selected by the session theme: themes.ini `shell = packs/4074-aero` (aero-patch, jade4074, jade4074-classic, aero5048), SHELL-DESIGN 6.1;
-- swapped live on a _LH_THEME change (runtime.lua rebuild_shell), so keep everything the pack decides in this table.
return {
  name = "aero", font = "Segoe UI 9",
  -- HackImage scaling in 4074: nearest-neighbour (1179 78x28 drawn 79x29: lh4074-181415.png fits nearest with error 129 per
  -- pixel, bilinear 266; 4074-dwm.png 1868 / 2047 over the glass). scene.c applies it to stretched nine-grid pieces and to
  -- magnified images; shrinking (clock layers, icons) stays smooth.
  filter = "nearest",
  -- the task buttons' icon for a window without _NET_WM_ICON or WM_HINTS icon: Explorer falls back to the application icon
  -- (IDI_APPLICATION, shell32 #3: packs/4074/shell32, lhde-import / shellpack.sh); lhwm's DWM captions use CompositedWindow DEFAULTICON instead
  default_icon = "4074/shell32/16/3.png",
  -- grid = {top, right, bottom, left} (scene.c order); HackNineGrid is a XAML Thickness (left, top, right, bottom)
  taskbar = { height = 30, bg_grid = {7, 0, 0, 0} },      -- 1187 1038x30, HackNineGrid 0,7,0,0 = top 7 (row 1 highlight kept on taller bars); height per row

  -- h = 29 more accurate but 30 looks better
  taskbar_text = "plain",                                  -- white task button / taskbar clock text (4074); "glow": themes/aerovision
  start = { w = 83, h = 30 }, start_dy = 0, taskbar_dy = 0, start_alpha = 0.75,                             -- StartButton Width 83 Height 29 (78 px image + 4 px border + 1)
  taskbutton = { max_w = 140, margin = { 0, 2, 2, 1} },                           -- 2.4.2: min(140, w/itemsPerRow), 29 high
  -- 150 = Sidebar\Width default (2.4.2; every reference starts the bar at screen_w - 150: 1178's dark column 0 is the bar's
  -- first pixel); 1178 150x786 HackNineGrid 36,0,0,0 = left 36 (the edge gradient keeps its width when the bar is resized).
  -- SidebarSizer: TileThumb Width 2, Dock Left (Flipped), its 1201 image Visibility=Hidden (the edge is 1178's columns 0-1)
  sidebar = { width = 150, bg_grid = {0, 0, 0, 36}, sizer = { w = 2, image = nil } },
  tile_bg = nil,                                          -- Aero tiles are transparent over the glass
  tiles = { "notifications", "power", "clock" },     -- M4b: tray tile, power tile (lh4074-181415.png order: tray/QL above power), pinned clock
  pinned_tiles = { "notifications", "clock" },            -- sidebar modes pinned / minimized (SHELL-DESIGN 6.4)
  -- popup menus: the 4074 Jade [sysmetrics] FLATMENUS colours (MENU, MENUTEXT, MENUHILIGHT, MENUBAR, GRAYTEXT)
  menu = { bg = "#f4f4f4", text = "#00025c", hot = "#c5e7f6", hot_text = "#000000", border = "#929497", gray = "#b0b1b3" },
  -- start menu (6.3): the Jade StartPanel class (packs/4074/startpanel-jade, shellpack.sh); metrics measured on 4074-dwm.png:
  -- 380 px wide, menu y 428..993 on 1024 (body 526 + log off row 40), left column 190 (PROGLIST), places from x 190 (190 wide),
  -- program tiles 40 px (32 px icon at +2, text at +41), places 36 px (24 px icon at +5, text at +33), separators 8 / 4 px,
  -- All Programs row 34 px (text at x 48), outer frame 1 px black + 1 px #6b6b6b; no USERPANE part in Jade (no user pane).
  startpanel = "4074/startpanel-jade",
  startmenu = { font = "Segoe UI,Tahoma 12px", width = 380, left_w = 190, body_min = 526, prog_tile = 40, places_h = 36, sep_left = 8, sep_right = 4,
                more_h = 34, logoff_h = 40, frame = { "#000000", "#6b6b6b" }, logoff_buttons = { "logoff", "standby" },
                hot_bg = "#c5e7f6", hot_text = "#000000", places_bold_color = "#323232",
                prog_dy = -2, places_dy = -1, more_text_dy = 0 },
}
