-- Notification area tile: the freedesktop System Tray (XEmbed) hosted in the sidebar (tray.c, SHELL-DESIGN M4b).
-- Longhorn 4074 put the tray icons in a sidebar tile ("Notifications"); geometry follows the measured 4051/4074 icon tile
-- (4051-vm-now.png y 19..51, lh4074-181415.png y 97..130): 16x16 icons, first at 5,6 in the foreground (x 12 / y 9 in
-- the tile), 28 px pitch across (16 + 12); rows 24 px apart (inferred: one row is all the references show).
-- Height 33 for one row (incl. the 4 px sizer), + 24 per extra row, animated 400 ms ease-in-out (2.4.4).
local tile = { name = "Notifications", height = 33 }
function tile:initialize()
  local cfg = self.T.tray or {}
  self.icon = cfg.icon or 16; self.pitch = cfg.pitch or {28, 24}; self.origin = cfg.origin or {5, 6}
  self.tray = self.foreground:add(lh.node{ kind = "tray", icon_size = self.icon, pitch = self.pitch, margin = {self.origin[1], self.origin[2], 0, 0} })
  self:claim()
  local function relayout(n)
    local _, _, fw = self.foreground:bounds(); if not fw or fw <= 0 then fw = 133 end
    local per = math.max(1, math.floor((fw - self.origin[1] - self.icon) / self.pitch[1]) + 1)
    local rows = math.max(1, math.ceil(n / per))
    self.sb.resize_tile(self, 33 + (rows - 1) * self.pitch[2])
  end
  lh.on("tray", relayout); self.relayout = relayout
  relayout(lh.tray_count())
end
-- tray coordination: one tray per display. While another client owns _NET_SYSTEM_TRAY_S<screen> (flxp-taskbar in the classic
-- mode) the tile is hidden and the selection left alone; when that owner goes away (runtime.lua: "tray-owner"(false)) the tile
-- claims it again (lh.tray_start announces MANAGER, the icons re-dock here) and shows itself.
function tile:set_hidden(h)
  if self.hidden == h then return end
  self.hidden = h
  if self.node then self.node:set{ visible = not h } end
  if self.view then self.view:set{ visible = not h } end
  lh._emit("tile-visibility", self)
end
function tile:claim()
  local ok = (not lh.tray_foreign or not lh.tray_foreign()) and lh.tray_start(self.tray)
  self.surface_ok = ok
  self:set_hidden(not ok)
  if not self.on_owner then
    self.on_owner = function(foreign) if foreign then self:set_hidden(true) else self:claim() end end
    lh.on("tray-owner", self.on_owner)
  end
end
-- the minimized sidebar's taskbar view (TBContentHost, SHELL-DESIGN 6.4): one row of 16 px icons, 20 px apart, centred in
-- the 30 px bar; the tray moves its icon windows into the taskbar surface (tray_start on a node of another surface)
function tile:taskbar(host)
  local pitch = 20
  self.tray = host:add(lh.node{ kind = "tray", icon_size = 16, pitch = { pitch, 24 }, margin = { 4, math.floor(((self.T.taskbar and self.T.taskbar.height or 30) - 16) / 2), 0, 0 }, w = 4, h = "100%" })
  self.view = self.tray
  self:claim()
  local function relayout(n) self.tray:set{ w = 4 + math.max(0, n) * pitch } end
  lh.on("tray", relayout); self.relayout = relayout
  relayout(lh.tray_count())
end
-- a theme switch rebuilds the tiles: the new tile takes the tray over (lh.tray_start), this one stops listening
function tile:dispose() if self.relayout then lh.off("tray", self.relayout) end if self.on_owner then lh.off("tray-owner", self.on_owner) end end
return tile
