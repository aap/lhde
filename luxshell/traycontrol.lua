-- The shared part of the system tray items (trayitems/volume.lua, brightness.lua, touchpad.lua): a 16 px icon in the tray
-- beside the XEmbed icons (the Classic Tray tile and its view in the minimized sidebar's taskbar, tiles/notifications.lua),
-- the XP volume popup's shape as the flyout, and the calls of the hardware layer they make in a child (C.hw).
--   local C = dofile(lh.lua_dir .. "/traycontrol.lua")
--   return C.item{ name, tooltip = function(st) -> text, icon = function(st) -> icon name,
--                  start = function(self, update) (reads the value, calls update(st) on every change; returns nothing),
--                  stop = function(self), click = function(self) | nil, middle = function(self) | nil,
--                  wheel = function(self, dir) (+1 up, -1 down), slider = { title, get = function(st) -> 0..100,
--                  set = function(self, v, final), mute = { text, get = function(st) -> bool, set = function(self, on) } } | nil,
--                  hidden_until_read = true (no icon before the first read) }
-- st.hidden hides the item's icons, and it takes no tray slot (no such device); st is whatever the item's start reports.
local C = {}

------------------------------------------------------------------ the hardware layer
-- C.hw(name, done, ...): lh[name](...) (lh.hw_volume, lh.hw_volume_set, ...: liblhde's hardware layer, which may run a
-- program and wait for it) in a child of luxshell (lh.fork_call), done(result) in the main loop: the table the call
-- returned, or { true } / { nil, reason } for a set ([1], [2])
function C.hw(name, done, ...)
  local t = {}
  return lh.fork_call(lh[name], function(l)
    local k, ty, v = l:match("^([^\t]*)\t(%a)\t(.*)$")
    if not k then return end
    if ty == "n" then v = tonumber(v) elseif ty == "b" then v = v == "true" end
    t[tonumber(k) or k] = v
  end, function() if done then done(t) end end, ...)
end
-- C.queue(after): runs calls of C.hw one at a time, in order; a call queued under a key while others wait replaces the
-- waiting one of that key (the wheel's steps collapse to the last), after() once the queue is empty (the item reads back)
--   local q = C.queue(read); q("volume", "hw_volume_set", 40)
function C.queue(after)
  local busy, order, pending = false, {}, {}
  local function nextc()
    local k = table.remove(order, 1)
    if not k then busy = false; if after then after() end return end
    local job = pending[k]; pending[k] = nil
    if not C.hw(job[1], function() nextc() end, table.unpack(job, 2, job.n)) then nextc() end
  end
  return function(key, name, ...)
    if pending[key] == nil then table.insert(order, key) end
    pending[key] = table.pack(name, ...)
    if not busy then busy = true; nextc() end
  end
end

------------------------------------------------------------------ icons
-- No picture of the packs fits (4074's imageres and shell32 have no speaker, sun or touchpad; XP's tray speaker is
-- sndvol32's, which lhde-import does not take), so the icons are drawn: shapes sampled 4x4 per pixel into runs of 1 px rects.
local function lerp(a, b, t) return a + (b - a) * t end
local function hex(c) return { tonumber(c:sub(2, 3), 16), tonumber(c:sub(4, 5), 16), tonumber(c:sub(6, 7), 16) } end
local function vgrad(top, bottom, y0, y1) local a, b = hex(top), hex(bottom)
  return function(_, y) local t = math.max(0, math.min(1, (y - y0) / (y1 - y0))); return { lerp(a[1], b[1], t), lerp(a[2], b[2], t), lerp(a[3], b[3], t) } end end
local function solid(c) local v = hex(c); return function() return v end end
local function poly(pts)   -- even-odd point in polygon
  return function(x, y)
    local inside, j = false, #pts
    for i = 1, #pts do
      local xi, yi, xj, yj = pts[i][1], pts[i][2], pts[j][1], pts[j][2]
      if (yi > y) ~= (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi then inside = not inside end
      j = i
    end
    return inside
  end
end
local function rrect(x0, y0, x1, y1, r) return function(x, y)
  if x < x0 or x > x1 or y < y0 or y > y1 then return false end
  local cx, cy = math.max(x0 + r, math.min(x1 - r, x)), math.max(y0 + r, math.min(y1 - r, y))
  return (x - cx) ^ 2 + (y - cy) ^ 2 <= r * r end end
local function disc(cx, cy, r) return function(x, y) return (x - cx) ^ 2 + (y - cy) ^ 2 <= r * r end end
local function ring(cx, cy, r0, r1) return function(x, y) local d = (x - cx) ^ 2 + (y - cy) ^ 2; return d >= r0 * r0 and d <= r1 * r1 end end
local function any(...) local fs = { ... }; return function(x, y) for _, f in ipairs(fs) do if f(x, y) then return true end end return false end end
local function arc(cx, cy, r, w, half)   -- a ring segment opening right, |angle| <= half degrees
  local inner, outer, lim = r - w / 2, r + w / 2, math.rad(half)
  return function(x, y) local dx, dy = x - cx, y - cy; local d = math.sqrt(dx * dx + dy * dy)
    return d >= inner and d <= outer and dx > 0 and math.abs(math.atan(dy, dx)) <= lim end end
-- the "no" sign (a red ring with a slash, XP's muted speaker and disabled overlays) over the lower right, on a white disc
local function no_sign(layers, cx, cy)
  table.insert(layers, { disc(cx, cy, 4.6), solid("#ffffff") })
  table.insert(layers, { any(ring(cx, cy, 3.0, 4.4), function(x, y) local dx, dy = x - cx, y - cy
    return dx * dx + dy * dy <= 3.4 * 3.4 and math.abs(dx - dy) <= 1.1 end), vgrad("#f05a4a", "#b81800", cy - 4, cy + 4) })
end
local shapes = {}
-- speaker: the box and the cone outlined dark, filled light to mid grey; waves 0, 1 or 2 (blue), or muted (the no sign)
function shapes.speaker(v)
  local L = {
    { any(rrect(0.5, 5, 5.5, 11, 0.5), poly{ {4.5, 5}, {9.6, 0.6}, {9.6, 15.4}, {4.5, 11} }), solid("#3a3a3a") },
    { any(rrect(1.5, 6, 5.5, 10, 0), poly{ {5, 6}, {8.6, 2.9}, {8.6, 13.1}, {5, 10} }), vgrad("#f4f4f4", "#8c8c8c", 3, 13) },
  }
  if v == "muted" then no_sign(L, 11.4, 11.4)
  else
    local n = tonumber(v) or 0
    if n >= 1 then table.insert(L, { arc(8.6, 8, 3.4, 1.5, 50), solid("#3d7fe6") }) end
    if n >= 2 then table.insert(L, { arc(8.6, 8, 6.0, 1.5, 52), solid("#3d7fe6") }) end
  end
  return L
end
-- sun: a yellow disc with an orange rim and eight rays
function shapes.sun()
  local rays = function(x, y) local dx, dy = x - 7.5, y - 7.5; local d = math.sqrt(dx * dx + dy * dy)
    if d < 5.2 or d > 7.6 then return false end
    for k = 0, 7 do local a = k * math.pi / 4; local px = math.abs(-dx * math.sin(a) + dy * math.cos(a)); local along = dx * math.cos(a) + dy * math.sin(a)
      if along > 0 and px <= 0.75 then return true end end
    return false end
  return { { rays, solid("#e08a00") }, { disc(7.5, 7.5, 4.3), solid("#b86200") }, { disc(7.5, 7.5, 3.4), vgrad("#fff4a0", "#ffb400", 4, 11) } }
end
-- touchpad: a rounded pad with the two buttons under a divider; off adds the no sign
function shapes.touchpad(v)
  local L = {
    { rrect(0.5, 1.5, 15.5, 14.5, 2.2), solid("#3a3a3a") },
    { rrect(1.5, 2.5, 14.5, 13.5, 1.4), vgrad("#f2f2f2", "#b4b4b4", 2, 14) },
    { any(rrect(1.5, 10, 14.5, 10.9, 0), rrect(7.6, 10, 8.4, 13.5, 0)), solid("#5a5a5a") },
  }
  if v == "off" then no_sign(L, 11.4, 11.4) end
  return L
end
local cache = {}
-- the pixels of an icon: { {x, y, w, "#aarrggbb"}, ... } (horizontal runs of one colour)
function C.pixels(name)
  if cache[name] then return cache[name] end
  local kind, v = name:match("^(%a+)%-?(.*)$")
  local layers = shapes[kind](v)
  local runs = {}
  for py = 0, 15 do
    local row = {}
    for px = 0, 15 do
      local r, g, b, a = 0, 0, 0, 0
      for _, l in ipairs(layers) do
        local cov, sc = 0, { 0, 0, 0 }
        for sy = 0, 3 do for sx = 0, 3 do local x, y = px + (sx + 0.5) / 4, py + (sy + 0.5) / 4
          if l[1](x, y) then cov = cov + 1; local c = l[2](x, y); sc[1] = sc[1] + c[1]; sc[2] = sc[2] + c[2]; sc[3] = sc[3] + c[3] end end end
        if cov > 0 then
          local ca = cov / 16; local cr, cg, cb = sc[1] / cov, sc[2] / cov, sc[3] / cov
          local na = ca + a * (1 - ca)                                     -- OVER, straight alpha
          r = (cr * ca + r * a * (1 - ca)) / na; g = (cg * ca + g * a * (1 - ca)) / na; b = (cb * ca + b * a * (1 - ca)) / na; a = na
        end
      end
      row[px] = a > 0.02 and string.format("#%02x%02x%02x%02x", math.floor(a * 255 + 0.5), math.floor(r + 0.5), math.floor(g + 0.5), math.floor(b + 0.5)) or nil
    end
    local px = 0
    while px <= 15 do
      if row[px] then local c, x0 = row[px], px; while px + 1 <= 15 and row[px + 1] == c do px = px + 1 end table.insert(runs, { x0, py, px - x0 + 1, c }) end
      px = px + 1
    end
  end
  cache[name] = runs
  return runs
end
-- C.icon(parent, x, y): a 16x16 canvas; :show(name) draws that icon (kept until the name changes)
function C.icon(parent, t)
  t = t or {}; t.layout = "canvas"; t.w = 16; t.h = 16
  local c = parent:add(lh.node(t))
  function c.show(_, name)
    if c.name == name then return end
    c.name = name; c:clear()
    for _, r in ipairs(C.pixels(name)) do c:add(lh.node{ kind = "rect", x = r[1], y = r[2], w = r[3], h = 1, fill = r[4] }) end
  end
  return c
end

------------------------------------------------------------------ the slider flyout
-- XP's tray volume popup (sndvol32 /t): "Volume" over a vertical trackbar (loud at the top, ticks on both sides), a "Mute"
-- check box under it, closing when it loses the focus. Its pixel layout is ours (no reference of it measured): 72 wide, the
-- caption Tahoma 8 centred 6 px from the top, a 100 px travel with 11 ticks, a 21x11 thumb on a 4 px sunken channel, the check
-- box row 8 px under the travel; inside the 4074 flyout chrome on white, as the power flyout.
local FONT = "Tahoma 8"
local W, TRAVEL, THUMB_W, THUMB_H = 72, 100, 21, 11
local function checkmark(parent, x, y)   -- the 7x7 check of a check box (Marlett "a")
  for i, row in ipairs{ "......#", ".....##", "#...###", "##.###.", "#####..", ".###...", "..#...." } do
    local s = 1
    while s <= 7 do
      if row:sub(s, s) == "#" then local e = s; while e < 7 and row:sub(e + 1, e + 1) == "#" do e = e + 1 end
        parent:add(lh.node{ kind = "rect", x = x + s - 1, y = y + i - 1, w = e - s + 1, h = 1, fill = "#21a121" }); s = e end
      s = s + 1
    end
  end
end
function C.slider_flyout(self, sl)
  local st = self.state or {}
  local root = lh.node{ layout = "fill", w = W }
  root:add(lh.node{ kind = "rect", fill = "#ffffff" })
  local col = root:add(lh.node{ layout = "vbox" })
  col:add(lh.node{ kind = "text", text = sl.title, font = FONT, color = "#000000", align = "center", w = W, h = 14, margin = { 0, 6, 0, 4 } })
  local H = TRAVEL + THUMB_H
  local track = col:add(lh.node{ layout = "canvas", w = W, h = H + 4, hoverable = true })
  local cx = math.floor(W / 2)
  track:add(lh.node{ kind = "rect", x = cx - 2, y = THUMB_H // 2, w = 4, h = TRAVEL + 1, fill = "#ffffff", stroke = "#9d9d9d" })   -- the channel
  track:add(lh.node{ kind = "rect", x = cx - 2, y = THUMB_H // 2, w = 3, h = 1, fill = "#6e6e6e" })
  track:add(lh.node{ kind = "rect", x = cx - 2, y = THUMB_H // 2, w = 1, h = TRAVEL, fill = "#6e6e6e" })
  for k = 0, 10 do   -- TBS_BOTH ticks every 10 %
    local y = THUMB_H // 2 + math.floor(k * TRAVEL / 10 + 0.5)
    track:add(lh.node{ kind = "rect", x = cx - THUMB_W // 2 - 6, y = y, w = 3, h = 1, fill = "#000000" })
    track:add(lh.node{ kind = "rect", x = cx + THUMB_W // 2 + 4, y = y, w = 3, h = 1, fill = "#000000" })
  end
  local thumb = track:add(lh.node{ kind = "rect", x = cx - THUMB_W // 2, y = 0, w = THUMB_W, h = THUMB_H, radius = 2, stroke = "#1c5180",
    gradient = { { 0, "#ffffff" }, { 0.5, "#f0f0ea" }, { 1, "#d6d2c2" } } })
  local grip = track:add(lh.node{ kind = "rect", x = cx - THUMB_W // 2 + 3, y = 5, w = THUMB_W - 6, h = 1, fill = "#9cb6d8" })
  local value = sl.get(st) or 0
  local function place(v) value = math.max(0, math.min(100, v)); local y = math.floor((100 - value) * TRAVEL / 100 + 0.5)
    thumb:set{ y = y }; grip:set{ y = y + 5 }; if lh.flyout then lh.flyout.surface:damage() end end
  place(value)
  -- pointer: on_press gives the button, the surface-press event right after it the position in the flyout; a drag moves by dy
  local press_y, last_set = nil, 0
  local function from_y(y) local _, ty = track:bounds(); return 100 - (y - (ty or 0) - THUMB_H / 2) * 100 / TRAVEL end
  local function set(v, final)
    place(v)
    local now = lh.now()
    if final or now - last_set >= 60 then last_set = now; sl.set(self, math.floor(value + 0.5), final) end
  end
  local listening
  track.on_press = function(_, b)
    if b == 4 or b == 5 then return end
    if b ~= 1 then return end
    press_y = nil
    if not listening then
      listening = function(x, y) lh.off("surface-press", listening); listening = nil; press_y = y; set(from_y(y)) end
      lh.on("surface-press", listening)
    end
  end
  track.on_drag = function(_, dx, dy) if press_y then set(from_y(press_y + dy)) end end
  track.on_release = function(_, b) if b == 1 and press_y then set(value, true); press_y = nil end end
  track.on_click = function(_, b) if b == 4 then set(value + (sl.step or 2), true) elseif b == 5 then set(value - (sl.step or 2), true) end end
  local box
  if sl.mute then
    local row = col:add(lh.node{ layout = "canvas", w = W, h = 13, margin = { 0, 4, 0, 6 }, hoverable = true, cursor = "arrow" })
    local tw = 13 + 5 + 24
    local x0 = math.floor((W - tw) / 2)
    row:add(lh.node{ kind = "rect", x = x0, y = 0, w = 13, h = 13, stroke = "#1c5180", gradient = { { 0, "#dcdcd7" }, { 1, "#ffffff" } } })
    box = row:add(lh.node{ layout = "canvas", x = 0, y = 0, w = W, h = 13 })
    row:add(lh.node{ kind = "text", text = sl.mute.text or "Mute", font = FONT, color = "#000000", x = x0 + 18, y = 0, w = 40, h = 13, valign = "center" })
    local function show(on) box:clear(); if on then checkmark(box, x0 + 3, 3) end if lh.flyout then lh.flyout.surface:damage() end end
    show(sl.mute.get(st))
    row.on_click = function(_, b) if b == 1 then local on = not sl.mute.get(self.state or {}); show(on); sl.mute.set(self, on) end end
    self._show_mute = show
  else col:add(lh.node{ h = 6 }) end
  -- an outside change (another mixer, the brightness keys) while it is open moves the thumb, unless the pointer holds it
  self.flyout_refresh = function(s) if not press_y then place(sl.get(s) or 0) end if self._show_mute and sl.mute then self._show_mute(sl.mute.get(s)) end end
  self.flyout_cleanup = function() if listening then lh.off("surface-press", listening); listening = nil end end
  return root, W, 6 + 14 + 4 + H + 4 + (sl.mute and 4 + 13 + 6 or 6)
end

------------------------------------------------------------------ the tray item
-- A tray item lives beside the XEmbed icons wherever the tray is shown (tiles/notifications.lua: the Classic Tray tile in the
-- sidebar, its view in the minimized sidebar's taskbar): one 16 px icon per view, in the tray's slots after the docked icons.
--   local cls = dofile(lh.lua_dir .. "/trayitems/volume.lua")
--   local it = cls.new{ T = pack, sb = sidebar, tb = taskbar, tile = host tile, on_visibility = function(it) end }
--   it:view(parent, { x = X, y = Y }, "sidebar" | "taskbar")   a canvas with the icon; it:start_once(); it:dispose()
-- Pointer: a left click opens the slider flyout (or does the item's click), the wheel steps, a middle click does the item's
-- middle (volume: mute); the tooltip is the item's tooltip, read when it shows. The flyout from the sidebar opens left of the
-- bar at the tile's top; from the taskbar above the bar, centred on the pointer (as the Notification History tile's).
function C.item(def)
  local cls = { name = def.name }
  function cls.new(ctx)
    ctx = ctx or {}
    local it = { name = def.name, def = def, T = ctx.T or {}, sb = ctx.sb, tb = ctx.tb, tile = ctx.tile, ctx = ctx, views = {} }
    if def.slider then it.flyout = function(self) return C.slider_flyout(self, def.slider) end end
    function it:flyout_closed() if self.flyout_cleanup then self.flyout_cleanup() end self.flyout_refresh = nil; self._show_mute = nil end
    local function handlers(view)
      view.node.on_click = function(_, b)
        if b == 4 then def.wheel(it, 1) elseif b == 5 then def.wheel(it, -1)
        elseif b == 2 then if def.middle then def.middle(it) end
        elseif b == 1 then
          it.anchor = view; it.node = view.node   -- flyout.lua: a press on the item itself is the toggle, not an outside click
          if def.click then def.click(it) elseif def.slider then lh.toggle_flyout(it) end
        end
      end
      lh.tooltip(view.node, function() return def.tooltip(it.state or {}) end)
    end
    function it:view(parent, t, where)
      t = t or {}
      local v = { where = where or "sidebar" }
      v.node = C.icon(parent, { x = t.x or 0, y = t.y or 0 })
      v.node:set{ visible = not self.hidden }
      if self.state then v.node:show(def.icon(self.state)) end
      table.insert(self.views, v)
      handlers(v)
      return v.node
    end
    function it:damage()
      if self.sb and self.sb.surface then self.sb.surface:damage() end
      if self.tb and self.tb.surface then self.tb.surface:damage() end
    end
    function it:update(st)
      self.state = st
      local name = def.icon(st)
      for _, v in ipairs(self.views) do v.node:show(name) end
      self:set_hidden(st.hidden and true or false)
      if self.flyout_refresh and not st.hidden then self.flyout_refresh(st) end
      self:damage()
    end
    function it:set_hidden(h)
      if self.hidden == h then return end
      self.hidden = h
      if h and lh.flyout_tile == self then lh.close_flyout() end
      for _, v in ipairs(self.views) do v.node:set{ visible = not h } end
      if ctx.on_visibility then ctx.on_visibility(self) end
    end
    function it:start_once()
      if self.started then return end
      self.started = true
      self:update({ hidden = def.hidden_until_read and true or nil })
      def.start(self, function(st) self:update(st) end)
    end
    function it:place(fw, fh)
      local sw, sh = lh.screen()
      if self.anchor and self.anchor.where == "taskbar" or not self.sb then
        local px = lh.pointer()
        return math.max(0, math.min(px - fw // 2, sw - fw)), math.max(0, sh - (self.tb and self.tb.h or 30) - fh)
      end
      local _, ty = self.sb.tile_rect(self.tile)
      return (self.sb.geo and self.sb.geo.x or self.sb.surface.x) - fw, math.max(0, math.min(ty or 0, sh - fh))
    end
    function it:dispose() if lh.flyout_tile == self then lh.close_flyout() end if def.stop then def.stop(self) end end
    return it
  end
  return cls
end
return C
