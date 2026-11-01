-- aerovision pack: the aero pack (packs/4074-aero) with Aero vision's text on glass (NAVBAR.md "Geometry": every text drawn on glass
-- gets a liberal white blur behind it and is drawn in black on top, as luxnav's breadcrumb and title); a theme's `[Shell] pack =
-- 4074-aerovision` (aerovision).
local here = debug.getinfo(1, "S").source:match("^@(.*)/[^/]+/[^/]+$") or "themes"
local P = dofile(lh.data_find("packs/4074-aero/shell.lua") or (here .. "/4074-aero/shell.lua"))
P.name = "aerovision"
P.taskbar_text = "glow"       -- task buttons and the taskbar clock: black text on a white halo
P.clock_pack = "aero"         -- [clock] pinned chosen in the aero pack holds here too (runtime.lua pack_key)
return P
