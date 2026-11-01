-- Notification History tile: 4074 NotificationHistoryTile + NotificationHistoryPanel + NotificationElement (WCPClient IL; the
-- store is luxshell/notifylog.lua). Tile icon imageres 4007 (the bell).
-- The tile is a time line, not a list: NotificationHistoryPanel (Height 30, in a Canvas as the tile's Foreground) places the newest
-- (at most 8) notifications as 19x16 speech bubbles (4014 unviewed / yellow, 4013 viewed / grey) along the bar's width by age:
--   span = max(30, minutes since the oldest shown); f = age / span, square-rooted when span > 30 (Transform);
--   offset = (1 - f) * width (truncated), so the newest sits at the right edge and the oldest at the left;
--   neighbours whose offsets are <= 20 px apart join a stack of at most 4 (FixStacks / CombineStacks); a stack of n is
--   15 + 4 (n - 1) wide, pushed right of the previous one and then back left of the next (ArrangeCore's two passes), and its
--   element j is drawn at (offset + 4 (j - 1), 15 - 4 (n - 1) + 4 j): a diagonal pile going down-right, the newest on top;
--   a ninth arrival drops the whole oldest stack (IsTooCrowded: more than 8 children); a 60 s timer re-spaces them.
-- Verified on lh4074-193747.png (Slate, 1152x864, bar at x 1002, tile y 62..98 = 3 + 30 + 4): one bubble at the left edge (its
-- element 4 px left of the foreground), a stack of three at offset 29, one at the right edge (offset 119 = 134 - 15).
-- Element: tooltip = arrival time + newline + header (or body); a click shows the toast again (_IconClicked → _Show, _fReshow).
-- A click on the tile elsewhere opens the flyout. Tile menu (_OnContextMenu): "Go to rules and alerts" (greyed: luxshell has no rules
-- engine) and our "Delete All Notifications". Empty: 4074 drew nothing; we write "No notifications" dimly (ours).
local tile = { name = "Notification History", height = 37, tooltip = "Notification History" }
local MAXN, JOIN, STACK = 8, 20, 4
local F = "Segoe UI,Tahoma 8"
local SHADOW = { dx = 1, dy = 1, color = "#ff000000" }

-- short time ("t") as the clock tile's: the locale's %X without seconds, no leading zero in the 12-hour form
local function short_time(t)
  local s = os.date("%X", t):gsub("^(.-%d+[:.]%d+)[:.]%d+", "%1")
  if os.date("%p", t) ~= "" then s = s:gsub("^0(%d)", "%1") end
  return s
end
local function long_date(t) return (os.date("%A, %B %d, %Y", t):gsub(" 0(%d),", " %1,")) end   -- "D" (en-US "dddd, MMMM dd, yyyy")
local function tip(e) return os.date("%x ", e.t) .. os.date("%X", e.t) .. "\n" .. ((e.summary ~= "" and e.summary) or e.body or "") end

function tile:fgw() return math.max(40, (self.sb and self.sb.w or 150) - 16) end

-- the panel's layout (RecomputePositions, SplitAllStacks, FixStacks, ArrangeCore): shown = entries oldest first
local function arrange(shown, W, now)
  if #shown == 0 then return {} end
  local span = math.max(30, (now - shown[1].t) / 60)
  local stacks = {}
  for _, e in ipairs(shown) do
    local f = math.max(0, (now - e.t) / 60) / span
    if span > 30 then f = math.sqrt(f) end
    stacks[#stacks + 1] = { elements = { e }, offset = math.floor((1 - f) * W) }   -- conv.i4 truncates
  end
  local i = 1
  while i <= #stacks do
    local s1 = stacks[i]
    while #s1.elements < STACK and s1.offset >= 0 and stacks[i + 1] do
      local s2 = stacks[i + 1]
      if s2.offset < 0 or s2.offset - s1.offset > JOIN then break end
      while #s1.elements < STACK and #s2.elements > 0 do table.insert(s1.elements, table.remove(s2.elements, 1)) end
      if #s2.elements == 0 then table.remove(stacks, i + 1) else break end
    end
    i = i + 1
  end
  local function width(s) return 15 + 4 * (#s.elements - 1) end
  local edge = 0
  for _, s in ipairs(stacks) do s.offset = math.max(edge, s.offset); edge = s.offset + width(s) end
  local right = W
  for k = #stacks, 1, -1 do local s = stacks[k]; s.offset = math.min(s.offset, right - width(s)); right = s.offset end
  return stacks
end

function tile:build()
  local fg = self.foreground; fg:clear()
  local c = fg:add(lh.node{ layout = "canvas" })
  local log = lh.notifylog
  local W = self:fgw()
  self.built_w = self.sb and self.sb.w
  local shown = self.shown or {}
  self.stacks = arrange(shown, W, os.time())
  if #shown == 0 then
    c:add(lh.node{ kind = "text", text = "No notifications", font = F, color = "#a0a6ac", align = "center", valign = "center", x = 0, y = 0, w = W, h = 30, shadow = SHADOW })
    return
  end
  for _, s in ipairs(self.stacks) do
    local n = #s.elements
    for j, e in ipairs(s.elements) do
      local b = c:add(lh.node{ kind = "button", x = s.offset + 4 * (j - 2), y = 15 - 4 * (n - 1) + 4 * (j - 1), w = 19, h = 16, layout = "fill" })
      b:add(lh.node{ kind = "image", image = self.R(e.viewed and 4013 or 4014), w = 19, h = 16 })
      b.on_click = function(_, button) if button == 1 and log then log.reshow(e) end end
      lh.tooltip(b, function() return tip(e) end)
    end
  end
end
-- GetRecentNotifications: the newest 8 at start; then arrivals append and a crowd drops the oldest stack; revoked / deleted entries go
function tile:sync()
  local log = lh.notifylog; local all = log and log.list() or {}
  local present = {}; for _, e in ipairs(all) do present[e] = true end
  if not self.shown then
    self.shown = {}; for k = math.max(1, #all - MAXN + 1), #all do self.shown[#self.shown + 1] = all[k] end
  else
    local keep, have = {}, {}
    for _, e in ipairs(self.shown) do if present[e] then keep[#keep + 1] = e; have[e] = true end end
    local newest = keep[#keep]
    local after = newest == nil
    for _, e in ipairs(all) do
      if after and not have[e] then
        keep[#keep + 1] = e
        if #keep > MAXN then   -- IsTooCrowded → RemoveStack(Stacks[0])
          local first = self.stacks and self.stacks[1]
          local drop = {}; for _, x in ipairs(first and first.elements or {}) do drop[x] = true end
          local k2 = {}; for _, x in ipairs(keep) do if not drop[x] then k2[#k2 + 1] = x end end
          if #k2 == #keep then table.remove(k2, 1) end
          keep = k2
          self.stacks = arrange(keep, self:fgw(), os.time())
        end
      end
      if e == newest then after = true end
    end
    -- an entry updated in place (replaces_id) moves to the end of the store: keep the panel in arrival order
    table.sort(keep, function(a, b) if a.t ~= b.t then return a.t < b.t end return a.key < b.key end)
    self.shown = keep
  end
end
function tile:refresh() self:sync(); self:build(); if self.sb then self.sb.surface:damage() end if self.fly_refresh then self.fly_refresh() end self:tb_update() end
function tile:initialize()
  self:sync(); self:build()
  if self.sb and self.sb.resize_tile then self.sb.resize_tile(self, 37, true) end
  self.on_log = function() self:refresh() end
  lh.on("notifylog", self.on_log)
  self.timer = lh.timer(function() self:build() end, 60000, true)   -- UpdateTimeScale
  self.on_width = function(w) if self.built_w ~= w then self:build() end end
  lh.on("sidebar-resize", self.on_width)
end
function tile:dispose()
  if self.timer then lh.cancel(self.timer) end
  if self.tbtimer then lh.cancel(self.tbtimer) end
  if self.on_log then lh.off("notifylog", self.on_log) end
  if self.on_width then lh.off("sidebar-resize", self.on_width) end
end
local function clear_all() lh.close_flyout(); if lh.notifylog then lh.notifylog.clear() end end
function tile:menu()
  return {
    { text = "Go to rules and alerts", disabled = true },
    { text = "&Delete All Notifications", disabled = not (lh.notifylog and #lh.notifylog.list() > 0), action = clear_all },
  }
end

------------------------------------------------------------------ the flyout (NotificationHistoryTile.CreateFlyout)
-- DockPanel { ScrollViewer (Top) 400x300, vertical scroll bar when needed { Grid, white, columns Auto | Auto | Star:
--     per day a DateLineElement ("D", Segoe UI 8 black) across the three columns; per notification: time ("t"), the bubble
--     (4014 / 4013), DockPanel { header (Left, #E96D00, Segoe UI 8, margin 2,0,2,0, trimmed), body (Fill, black, Segoe UI 8,
--     trimmed) }, and a NotificationFlyoutMaskElement over the row (left click: the toast again) }
--   DockPanel (Fill) { Image 4007 (Left), "Go to Rules and Alerts" (Left, Segoe UI 8 white) } }
-- right click: ContextMenu "Delete All Notifications", "Go to Rules and Alerts".
-- Ours: newest first; a 16 px app icon before the header when the notification named one; the full text in the row tooltip;
-- the white link at the bottom deletes all (4074's linked to the rules pages, which luxshell does not have; its context menu entry
-- "Delete All Notifications" did nothing in 4074 and works here); the mouse wheel scrolls by three rows (no scroll bar drawn).
-- Closing the flyout marks everything viewed.
local FW, FH, ROW = 400, 300, 17
function tile:flyout()
  local me, log = self, lh.notifylog
  local root = lh.node{ layout = "dock", w = FW }
  local view = root:add(lh.node{ dock = "top", h = FH, layout = "fill", clip = true })
  view:add(lh.node{ kind = "rect", fill = "#ffffff" })
  local list = view:add(lh.node{ layout = "canvas" })
  local scroll, total = 0, 0
  local function scroll_by(d)
    local maxs = math.max(0, total - FH)
    local s = math.max(0, math.min(maxs, scroll + d))
    if s ~= scroll then scroll = s; list:set{ y = -scroll }; if lh.flyout then lh.flyout.surface:damage() end end
  end
  local function wheel(b) if b == 4 then scroll_by(-3 * ROW) return true elseif b == 5 then scroll_by(3 * ROW) return true end end
  local function menu()
    local px, py = lh.pointer()
    lh.close_flyout()
    lh.menu.open({ { text = "&Delete All Notifications", disabled = not (log and #log.list() > 0), action = clear_all },
                   { text = "Go to Rules and Alerts", disabled = true } }, px, py, { style = "avalon", grab = true, flip = true })
  end
  local function fill()
    list:clear()
    local entries = log and log.list() or {}
    local y, day = 2, nil
    -- column widths: Auto (the widest time), Auto (19), Star
    local tw = 0
    for _, e in ipairs(entries) do local p = lh.node{ kind = "text", text = short_time(e.t), font = F }; tw = math.max(tw, (p:measure())) end
    tw = math.ceil(tw) + 6
    for k = #entries, 1, -1 do
      local e = entries[k]
      local d = os.date("%Y-%m-%d", e.t)
      if d ~= day then
        day = d
        list:add(lh.node{ kind = "text", text = long_date(e.t), font = F, color = "#000000", x = 4, y = y + 2, w = FW - 8, h = 14 })
        list:add(lh.node{ kind = "rect", fill = "#d8d8d8", x = 4, y = y + ROW, w = FW - 8, h = 1 })
        y = y + ROW + 3
      end
      local row = list:add(lh.node{ layout = "canvas", x = 0, y = y, w = FW, h = ROW, hoverable = true })
      local hl = row:add(lh.node{ kind = "rect", fill = "#fdecd8", x = 0, y = 0, w = FW, h = ROW, visible = false })
      row:add(lh.node{ kind = "text", text = short_time(e.t), font = F, color = "#000000", x = 4, y = 1, w = tw, h = 15 })
      row:add(lh.node{ kind = "image", image = me.R(e.viewed and 4013 or 4014), x = 4 + tw, y = 0, w = 19, h = 16 })
      local x = 4 + tw + 19 + 2
      local icon = e.icon ~= "" and lh.apps and lh.apps.icon(e.icon, 16)
      if icon then row:add(lh.node{ kind = "image", image = icon, x = x, y = 0, w = 16, h = 16, filter = "best" }); x = x + 18 end
      local avail = FW - x - 4
      local h = lh.node{ kind = "text", text = e.summary, font = F, color = "#e96d00", ellipsis = true }
      local hw = math.min(math.ceil((h:measure())), e.body ~= "" and math.floor(avail * 0.6) or avail)
      h:set{ x = x, y = 1, w = hw, h = 15 }; row:add(h)
      if e.body ~= "" then
        row:add(lh.node{ kind = "text", text = (e.body:gsub("%s*\n%s*", " ")), font = F, color = "#000000", ellipsis = true, x = x + hw + 4, y = 1, w = math.max(0, avail - hw - 4), h = 15 })
      end
      row.on_hover = function(_, over) hl:set{ visible = over } end
      row.on_click = function(_, b) if wheel(b) then return end if b == 1 and log then lh.close_flyout(); log.reshow(e) elseif b == 3 then menu() end end
      lh.tooltip(row, function() return (e.app ~= "" and (e.app .. "\n") or "") .. tip(e) .. (e.summary ~= "" and e.body ~= "" and ("\n" .. e.body) or "") end)
      y = y + ROW + 1
    end
    if #entries == 0 then
      list:add(lh.node{ kind = "text", text = "No notifications", font = F, color = "#808080", x = 0, y = 8, w = FW, h = 15, align = "center" })
    end
    total = y + 2; scroll = math.max(0, math.min(scroll, total - FH)); list:set{ y = -scroll }
  end
  fill()
  view:set{ hoverable = true }
  view.on_click = function(_, b) if wheel(b) then return end if b == 3 then menu() end end
  -- the bottom row on the flyout chrome: the bell and the white link
  local foot = root:add(lh.node{ dock = "fill", layout = "dock", h = 22, hoverable = true })
  foot:add(lh.node{ kind = "image", image = self.R(4007), dock = "left", w = 18, h = 18, margin = { 0, 3, 4, 1 } })
  local link = foot:add(lh.node{ kind = "text", text = "Delete All Notifications", font = F, color = "#ffffff", dock = "left", valign = "center", cursor = "hand", hoverable = true })
  link.on_hover = function(_, over) link:set{ color = over and "#ffd28a" or "#ffffff" } end
  link.on_click = function(_, b) if b == 1 then clear_all() end end
  self.fly_refresh = function() fill(); if lh.flyout then lh.flyout.surface:damage() end end
  return root, FW, FH + 22
end
function tile:flyout_closed() self.fly_refresh = nil; if lh.notifylog then lh.notifylog.view_all() end end

-- the minimized sidebar's taskbar view (4074 TaskbarTiles holds Notification History): the bell and the unread count
function tile:taskbar(host)
  local bh = self.T.taskbar and self.T.taskbar.height or 30
  local ts = lh.taskbar_text_style and lh.taskbar_text_style() or { color = "#ffffff" }
  local box = host:add(lh.node{ layout = "hbox", margin = { 4, 0, 2, 0 } })
  local bell = box:add(lh.node{ kind = "button", w = 18, h = 18, margin = { 0, math.floor((bh - 18) / 2), 2, 0 }, layout = "fill" })
  bell:add(lh.node{ kind = "image", image = self.R(4007), w = 18, h = 18 })
  self.tbcount = box:add(lh.node{ kind = "text", text = "", font = self.T.font, color = ts.color, glow = ts.glow, glow_size = ts.glow_size, glow_strength = ts.glow_strength,
    valign = "center", h = bh, shadow = not ts.glow and SHADOW or nil })
  lh.tooltip(bell, function() local n = lh.notifylog and lh.notifylog.unread() or 0; return n == 0 and "No new notifications" or (n .. " new notification" .. (n == 1 and "" or "s")) end)
  bell.on_click = function(_, b) if b == 1 then lh.open_flyout(self) end end
  self.on_log = function() self:tb_update() end
  lh.on("notifylog", self.on_log)
  self:tb_update()
end
function tile:tb_update()
  if not self.tbcount then return end
  local n = lh.notifylog and lh.notifylog.unread() or 0
  self.tbcount:set{ text = n > 0 and tostring(n) or "" }
  if self.tb then self.tb.surface:damage() end
end
-- the flyout from the taskbar view: above the bar's right end
function tile:place(fw, fh)
  local sw, sh = lh.screen()
  if not self.tb then   -- in the bar: flyout.lua's default, left of the tile
    local _, ty = self.sb.tile_rect(self)
    return (self.sb.geo and self.sb.geo.x or self.sb.surface.x) - fw, math.max(0, math.min(ty, sh - fh))
  end
  local px = lh.pointer()
  return math.max(0, math.min(px - fw // 2, sw - fw)), math.max(0, sh - (self.tb.h or 30) - fh)
end
return tile
