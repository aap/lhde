-- Notification area tile ("Classic Tray"): the freedesktop System Tray (XEmbed) hosted in the sidebar (tray.c), and luxshell's
-- own tray items after the docked icons (trayitems/: Volume, Brightness, Touchpad; traycontrol.lua C.item), as XP's
-- notification area holds stobject's speaker after the programs' icons.
-- Longhorn 4074 puts the tray icons in a sidebar tile ("Notifications"); the geometry is the 4051/4074 icon tile's: 16x16
-- icons, first at 5,6 in the foreground (x 12 / y 9 in the tile), 28 px pitch across (16 + 12); rows 24 px apart (our
-- choice: 4051 and 4074 show one row). The items take the slots after the XEmbed icons in the same rows; an item hidden (no
-- backlight, no touchpad) takes none.
-- Height 33 for one row (incl. the 4 px sizer), + 24 per extra row, animated 400 ms ease-in-out (2.4.4).
local tile = { name = "Notifications", height = 33 }
local ITEMS = { "volume", "brightness", "touchpad" }   -- trayitems/NAME.lua, in this order after the XEmbed icons

-- the items, one instance per tile (their system commands start with the tile and stop with it)
local function load_items(self)
  self.items = {}
  for _, n in ipairs(ITEMS) do
    local ok, cls = pcall(dofile, lh.lua_dir .. "/trayitems/" .. n .. ".lua")
    if ok and type(cls) == "table" and cls.new then
      table.insert(self.items, cls.new{ T = self.T, sb = self.sb, tb = self.tb, tile = self,
        on_visibility = function() if self.relayout then self.relayout(self:docked()) end end })
    else io.stderr:write("luxshell: tray item " .. n .. ": " .. tostring(cls) .. "\n") end
  end
end
local function shown_items(self) local k = 0; for _, it in ipairs(self.items or {}) do if not it.hidden then k = k + 1 end end return k end
-- the XEmbed icons docked here (none while another client owns the tray)
function tile:docked() return self.surface_ok and lh.tray_count() or 0 end

function tile:initialize()
  local cfg = self.T.tray or {}
  self.icon = cfg.icon or 16; self.pitch = cfg.pitch or {28, 24}; self.origin = cfg.origin or {5, 6}
  self.tray = self.foreground:add(lh.node{ kind = "tray", icon_size = self.icon, pitch = self.pitch, margin = {self.origin[1], self.origin[2], 0, 0} })
  self.layer = self.foreground:add(lh.node{ layout = "canvas" })
  load_items(self)
  for _, it in ipairs(self.items) do it:view(self.layer, { x = -100, y = -100 }, "sidebar") end
  local function relayout(n)
    local _, _, fw = self.foreground:bounds(); if not fw or fw <= 0 then fw = 133 end
    local per = math.max(1, math.floor((fw - self.origin[1] - self.icon) / self.pitch[1]) + 1)
    local slot = n
    for _, it in ipairs(self.items) do
      if not it.hidden then
        it.views[1].node:set{ x = self.origin[1] + (slot % per) * self.pitch[1], y = self.origin[2] + (slot // per) * self.pitch[2] }
        slot = slot + 1
      end
    end
    local rows = math.max(1, math.ceil(slot / per))
    self:refresh_hidden()
    self.sb.resize_tile(self, 33 + (rows - 1) * self.pitch[2])
    if self.sb.surface then self.sb.surface:damage() end
  end
  self.relayout = relayout
  self.on_tray = function() if self.relayout then self.relayout(self:docked()) end end
  lh.on("tray", self.on_tray)
  self:claim()
  for _, it in ipairs(self.items) do it:start_once() end
  relayout(self:docked())
end
-- tray coordination: one tray per display. While another client owns _NET_SYSTEM_TRAY_S<screen> (flxp-taskbar in the classic
-- mode) the XEmbed icons are that client's and the selection is left alone: the tile shows only the items (hidden when none is
-- shown); when that owner goes away (runtime.lua: "tray-owner"(false)) the tile claims it again (lh.tray_start announces
-- MANAGER, the icons re-dock here).
function tile:set_hidden(h)
  if self.hidden == h then return end
  self.hidden = h
  if self.node then self.node:set{ visible = not h } end
  if self.view then self.view:set{ visible = not h } end
  lh._emit("tile-visibility", self)
end
function tile:refresh_hidden()
  if self.tray then self.tray:set{ visible = self.surface_ok and true or false } end
  self:set_hidden(not self.surface_ok and shown_items(self) == 0)
end
function tile:claim()
  local ok = (not lh.tray_foreign or not lh.tray_foreign()) and lh.tray_start(self.tray)
  self.surface_ok = ok
  self:refresh_hidden()
  if not self.on_owner then
    self.on_owner = function(foreign) if foreign then self.surface_ok = false; self:refresh_hidden() else self:claim() end
      if self.relayout then self.relayout(self:docked()) end end
    lh.on("tray-owner", self.on_owner)
  end
end
-- the minimized sidebar's taskbar view (TBContentHost, SHELL-DESIGN 7.3): one row of 16 px icons, 20 px apart, centred in
-- the 30 px bar; the tray moves its icon windows into the taskbar surface (tray_start on a node of another surface); the items
-- follow in the same row
function tile:taskbar(host)
  local pitch, bh = 20, self.T.taskbar and self.T.taskbar.height or 30
  self.view = host:add(lh.node{ layout = "hbox", h = "100%" })
  self.tray = self.view:add(lh.node{ kind = "tray", icon_size = 16, pitch = { pitch, 24 }, margin = { 4, math.floor((bh - 16) / 2), 0, 0 }, w = 4, h = "100%" })
  self.layer = self.view:add(lh.node{ layout = "canvas", w = 0, h = bh })
  load_items(self)
  for _, it in ipairs(self.items) do it:view(self.layer, { x = -100, y = math.floor((bh - 16) / 2) }, "taskbar") end
  local function relayout(n)
    self.tray:set{ w = 4 + math.max(0, n) * pitch }
    local k = 0
    for _, it in ipairs(self.items) do if not it.hidden then it.views[1].node:set{ x = 2 + k * pitch }; k = k + 1 end end
    self.layer:set{ w = k > 0 and 2 + k * pitch or 0, visible = k > 0 }
    if self.tray then self.tray:set{ visible = self.surface_ok and true or false } end
    if self.tb and self.tb.surface then self.tb.surface:damage() end
  end
  self.relayout = relayout
  self.on_tray = function() if self.relayout then self.relayout(self:docked()) end end
  lh.on("tray", self.on_tray)
  self:claim()
  for _, it in ipairs(self.items) do it:start_once() end
  relayout(self:docked())
end
-- a theme switch rebuilds the tiles: the new tile takes the tray over (lh.tray_start), this one stops listening and its items
-- stop their commands
function tile:dispose()
  self.relayout = nil
  if self.on_tray then lh.off("tray", self.on_tray) end
  if self.on_owner then lh.off("tray-owner", self.on_owner) end
  for _, it in ipairs(self.items or {}) do it:dispose() end
end
return tile
