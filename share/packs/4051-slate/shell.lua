-- 4051-slate pack: the Slate pack (packs/4074-slate) as build 4051 drew it; a theme's `[Shell] pack = 4051-slate` (slate4051).
-- Builds after ~4060 look like 4074 (packs/4074-slate); 4051 (at 1152x864) differs in:
--  * image scaling is bilinear: the start button is imageres 1156 (74x23) drawn 79x29 bilinear, where 4074 draws it
--    nearest-neighbour (4074's flag column repeats rows: 144 144 ... 98 98 ... 114 114, the 4051 one ramps 169 198 202 181);
--  * the bar is 29 px (rows 835..863), 1155 with one row (16) repeated, as uxtheme's TILE stretch does: drawn with a fixed
--    7 px top and 2 px bottom and the middle stretched nearest;
--  * the images come from 4051's own imageres.dll where it differs from 4074's (packs/4051/imageres, lhde-import): the
--    right-angled tile arrow 1105/1106, tile restore/close 1058-1062, toast parts 4003-4011;
--  * the clock is a tile in the list (96 px face, "6:37 AM, Oct 23" under it), not pinned to the bottom; tiles: the quick
--    launch / tray tile and the clock (4051 has no power tile).
-- Stand-in: 4051 ran the classic Win32 taskbar (CTray/CTaskBand through the uxtheme TaskBar/TaskBand classes, SHELL-DESIGN
-- 2.1; 4051's dark task button plates are TaskBand::Toolbar.BUTTON 349, 146 px wide, and there is no sidebar toggle
-- button). luxshell keeps its Avalon taskbar here, with the 4051 start button look, height and filter; a classic taskbar is the
-- separate program flxp-taskbar. The Sidebar class blobs of the 4051 msstyles (not imported: nothing reads them) are
-- pixel-identical to 4074 Slate's streams, so nothing changes there.
local here = debug.getinfo(1, "S").source:match("^@(.*)/[^/]+/[^/]+$") or "themes"
local P = dofile(lh.data_find("packs/4074-slate/shell.lua") or (here .. "/4074-slate/shell.lua"))
P.build = 4051
P.filter = "bilinear"
P.imageres = "4051/imageres"
P.taskbar = { height = 29, bg_grid = {7, 0, 2, 0}, bg_filter = "nearest" }
P.start_dy = 0; P.taskbar_dy = 0                 -- 4051: the pill sits on the bar's top row (835)
P.start = { w = 83, h = 29 }                -- StartButton 83x29, Border(0,0,4,0): 1156 drawn 79x29 (bilinear), at the bar's top row
P.tiles = { "quicklaunch", "notifications", "clock" }   -- 4051 Administrator: Quick Launch, Clock (+ our tray tile)
P.taskbar_tiles = nil                      -- 4051: no TaskbarTiles list (the tray showed one icon per tile)
P.clock_pinned = false                     -- [clock] pinned default for this pack: the 4051 tile-in-the-list clock
return P
