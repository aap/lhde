-- Aero_*Resources pack (imageres ids 1167-1201), SHELL-DESIGN 2.2.2 / 2.3.2
-- Selected by the session theme: a theme's `[Shell] pack = 4074-aero`, SHELL-DESIGN 5;
-- swapped live on a _LH_THEME change (runtime.lua rebuild_shell), so keep everything the pack decides in this table.
return {
  font = "Segoe UI 9",
  -- HackImage scaling in 4074: nearest-neighbour (1179 78x28 drawn 79x29: lh4074-181415.png fits nearest with error 129 per
  -- pixel, bilinear 266; 4074-dwm.png 1868 / 2047 over the glass). scene.c applies it to stretched nine-grid pieces and to
  -- magnified images; shrinking (clock layers, icons) stays smooth.
  filter = "nearest",
  -- the task buttons' icon for a window without _NET_WM_ICON or WM_HINTS icon: Explorer falls back to the application icon
  -- (IDI_APPLICATION, shell32 #3: packs/4074/shell32, lhde-import / shellpack.sh); luxwm's DWM captions use CompositedWindow DEFAULTICON instead
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
  -- 4074 Administrator SidebarTiles: Search, Notification History, Quick Launch, Classic Tray (+ the transient Power, the pinned
  -- Clock): lh4074-193747.png top to bottom (no Search tile in luxshell yet)
  tiles = { "notifyhistory", "quicklaunch", "notifications", "power", "clock" },
  pinned_tiles = { "notifications", "clock" },            -- sidebar modes pinned / minimized (SHELL-DESIGN 7.3)
  taskbar_tiles = { "notifyhistory", "quicklaunch", "notifications", "clock" },   -- minimized: 4074 TaskbarTiles (all five)
  -- popup menus: the 4074 Jade [sysmetrics] FLATMENUS colours (MENU, MENUTEXT, MENUHILIGHT, MENUBAR, GRAYTEXT)
  menu = { bg = "#f4f4f4", text = "#00025c", hot = "#c5e7f6", hot_text = "#000000", border = "#929497", gray = "#b0b1b3" },
}
