-- 5112-aero pack: a copy of the Aero pack (packs/4074-aero) with the taskbar of Windows Vista build 5112
-- (Beta 1, July 2005; COMPOSITION.md 21); a theme's `[Shell] pack = 5112-aero` (basic5112).
-- The 5112 art lives in packs/5112/imageres under the Aero imageres ids it replaces (shellpack-aero5112.sh): runtime.lua R() takes an
-- id from the pack's `imageres` directory when the file is there, else from packs/4074/imageres. Replaced: the Start button
-- (1179/1180/1181 = aero.msstyles 5112 Start::Button bitmap#502 + flag + "Start"), the bar (1187 = TaskBar.BACKGROUNDBOTTOM
-- bitmap#298, opaque dark slate), the task buttons (1182/1184/1185 = TaskBand::Toolbar.BUTTON bitmap#517 normal/hot/checked) and
-- the flash plate (1183 = TaskBand.FLASHBUTTON bitmap#296). 5112 has no sidebar, no sidebar toggle and no clock face: those stay
-- the Longhorn Aero images. The 5112 tray background (TrayNotifyHoriz bitmap#528, packs/5112/imageres/tray_bg.png) has no node in
-- runtime.lua yet.
return {
  name = "aero5112", font = "Segoe UI 9", imageres = "5112/imageres",
  -- HackImage scaling in 4074: nearest-neighbour (1179 78x28 drawn 79x29: lh4074-181415.png fits nearest with error 129 per
  -- pixel, bilinear 266; 4074-dwm.png 1868 / 2047 over the glass). scene.c applies it to stretched nine-grid pieces and to
  -- magnified images; shrinking (clock layers, icons) stays smooth.
  filter = "nearest",
  -- the task buttons' icon for a window without _NET_WM_ICON or WM_HINTS icon: Explorer falls back to the application icon
  -- (IDI_APPLICATION, shell32 #3: packs/4074/shell32, lhde-import / shellpack.sh); luxwm's DWM captions use CompositedWindow DEFAULTICON instead
  default_icon = "4074/shell32/16/3.png",
  -- grid = {top, right, bottom, left} (scene.c order); HackNineGrid is a XAML Thickness (left, top, right, bottom)
  -- 5112: TaskBar.BACKGROUNDBOTTOM SIZINGMARGINS 1 1 14 15 (l r t b) → grid {14, 1, 15, 1}; the 30 px of the bitmap
  taskbar = { height = 30, bg_grid = {14, 1, 15, 1} },

  -- 5112 Start::Button: 83x30 per state, opaque pill drawn 1:1 at the bar's top (slot 87 = image 83 + the 4 px right border)
  start = { w = 87, h = 30 }, start_dy = 0, start_alpha = 1,
  taskbutton = { max_w = 140 },                           -- 2.4.2: min(140, w/itemsPerRow), 29 high
  -- 150 = Sidebar\Width default (2.4.2; every reference starts the bar at screen_w - 150: 1178's dark column 0 is the bar's
  -- first pixel); 1178 150x786 HackNineGrid 36,0,0,0 = left 36 (the edge gradient keeps its width when the bar is resized).
  -- SidebarSizer: TileThumb Width 2, Dock Left (Flipped), its 1201 image Visibility=Hidden (the edge is 1178's columns 0-1)
  sidebar = { width = 150, bg_grid = {0, 0, 0, 36}, sizer = { w = 2, image = nil } },
  tile_bg = nil,                                          -- Aero tiles are transparent over the glass
  tiles = { "notifications", "power", "clock" },     -- tray tile, power tile (lh4074-181415.png order: tray/QL above power), pinned clock
  pinned_tiles = { "notifications", "clock" },            -- sidebar modes pinned / minimized (SHELL-DESIGN 6.4)
  -- popup menus: the 4074 Jade [sysmetrics] FLATMENUS colours (MENU, MENUTEXT, MENUHILIGHT, MENUBAR, GRAYTEXT)
  menu = { bg = "#f4f4f4", text = "#00025c", hot = "#c5e7f6", hot_text = "#000000", border = "#929497", gray = "#b0b1b3" },
}
