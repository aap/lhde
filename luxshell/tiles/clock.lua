-- Clock tile: analogclock.xaml + the clocksidebar*.xaml views (SHELL-DESIGN 2.3.4, "Clock tile modes" in the M6 status).
--   state.ini [clock] pinned = 1 (4074: SBPinnedContentHostCollection at the bottom of the bar) | 0 (4051: a normal tile at
--             its place in the tile list, 4051-vm-now.png: ~96 px face, "6:37 AM, Oct 23" under it, tile 160 high)
--             [clock] layout = default | digtime | digtimemonthday | digtimeweekday | minical | cal ("Clock layout" in the
--             tile's context menu; the 4074 property sheet clockfacessheet.xaml lists the same views)
-- Analog face: 1074 (260x260) face, hour hand 3x95 origin 2,76, minute 3x125 origin 2,106, second polygon #6DB8CF, anchor
-- 1083, glass 1076 (Slate / 4051 light set); the Aero/Jade pack draws the dark-blue image set 1188-1198 (see "Corrections").
-- Text styles (clockresources.xaml): DigitalText Tahoma 8 white with ShadowText (black copy at 1,1); DigitalTime = "h:mm"
-- #D8E4FF + "tt" #9EB5BB (Margin 2,0), bold where the view says FontWeight Bold; MiniCalendar: Border 1 #D3D3D3 { day
-- "^ddd" Tahoma 8 bold white on #ADD8E6 (Margin 1, text margin 5,2), date "%d" Tahoma 11 #4B5681 on white }.
-- The XAML margins are binary two-value Thickness (horizontal, vertical): "bin:020002" = 0,2 (default view), "bin:020004" =
-- 0,4 (digtime / minical views), "bin:020200" = 2,0 (the AM/PM gap), "bin:020502" = 5,2 (MiniCalDay).
local tile = { name = "Clock", height = 105, pinned = true }
local LAYOUTS = { "default", "digtime", "digtimemonthday", "digtimeweekday", "minical", "cal" }
local TITLES = { default = "Analog and time", digtime = "Analog and digital time", digtimemonthday = "Digital time and date",
                 digtimeweekday = "Digital time, weekday and date", minical = "Small clock and calendar", cal = "Calendar" }
tile.LAYOUTS, tile.TITLES = LAYOUTS, TITLES

local N = function(t) return lh.node(t) end
local SHADOW = { dx = 1, dy = 1, color = "#ff000000" }
local function nozero(s) return (s:gsub("^0", ""):gsub(" 0(%d)", " %1")) end
local FMT = {
  default = function() return nozero(os.date("%I:%M %p, %b %d")) end,      -- "*LongTime": 4074-dwm.png / 4051-vm-now.png show "6:37 AM, Oct 23"
  hm = function() return nozero(os.date("%I:%M")) end,                    -- XShortTime "h:mm"
  tt = function() return os.date("%p") end,                                -- XAMPM "tt"
  monthday = function() return os.date("%B %d") end,                       -- "*MonthDay" = en-US MonthDayPattern "MMMM dd"
  weekday = function() return os.date("%A") end,                           -- "dddd"
  mdy = function() return (os.date("%B %d, %Y"):gsub(" 0(%d),", " %1,")) end, -- "MMMM d, yyyy"
  dayupper = function() return os.date("%a"):upper() end,                  -- "^ddd"
  day = function() return tostring(tonumber(os.date("%d"))) end,           -- "%d"
}

-- analog clock (AnalogClockPanel scaled from its 260 design size) of size px at x, y in a canvas `parent`
function tile:analog(parent, size, x, y)
  local R = self.R
  local clock = parent:add(N{ layout = "canvas", x = x, y = y, w = size, h = size })
  local s, c = size / 260, size / 2
  local hands = {}
  local function layer(id, w, h, pivot_y, dx)   -- an image w x h (design px) rotating about its (w/2, pivot_y)
    local g = clock:add(N{ kind = "transform", x = 0, y = 0, w = size, h = size, origin = {c, c}, layout = "canvas" })
    g:add(N{ kind = "image", image = R(id), x = c - w * s / 2 + (dx or 0) * s, y = c - pivot_y * s, w = w * s, h = h * s })
    return g
  end
  if self.T.name ~= "slate" then
    -- Aero/Jade: the dark-blue set; hand pivots estimated from 4074-dwm.png (tail ~22 design px, second hand ~35)
    clock:add(N{ kind = "image", image = R(1190), x = c - 263 * s / 2, y = c - 263 * s / 2, w = 263 * s, h = 263 * s })
    local hs, ms, ss = layer(1194, 12, 103, 93, 3), layer(1196, 12, 133, 123, 3), layer(1198, 15, 136, 124, 3)
    hands.hour, hands.minute, hands.second = layer(1193, 11, 122, 100), layer(1195, 11, 151, 129), layer(1197, 11, 163, 128)
    hands.shadow = { hour = hs, minute = ms, second = ss }
    clock:add(N{ kind = "image", image = R(1189), x = c - 6 * s, y = c - 6 * s, w = 12 * s, h = 12 * s })
    clock:add(N{ kind = "image", image = R(1191), x = c - 265 * s / 2, y = c - 265 * s / 2, w = 265 * s, h = 265 * s })
  else
    -- Slate / 4051: the light face, XAML rectangle hands (thin dark grey as in the Slate shots), anchor 1083, glass 1076
    clock:add(N{ kind = "image", image = R(1074), x = 0, y = 0, w = size, h = size })
    local function hand(len, tail, w, fill)
      local g = clock:add(N{ kind = "transform", x = 0, y = 0, w = size, h = size, origin = {c, c}, layout = "canvas" })
      g:add(N{ kind = "hand", x = c - w / 2, y = c - len * s + tail * s, w = w, h = len * s, fill = fill })
      return g
    end
    local hw = math.max(1, 3 * s * 1.2)
    hands.hour = hand(95, 2, hw, "#ff3e3f3f"); hands.minute = hand(125, 2, hw, "#ff3e3f3f")
    hands.second = hand(88 + 38, 38, 1, "#ff6db8cf")
    clock:add(N{ kind = "image", image = R(1083), x = c - 9 * s, y = c - 9 * s, w = 18 * s, h = 18 * s })
    clock:add(N{ kind = "image", image = R(1076), x = 0, y = 0, w = size, h = size })
  end
  table.insert(self.analogs, hands)
  return clock
end

-- a ShadowText line: Text, font, colour, at x, y in a canvas (returns the node and its measured width)
local function stext(parent, text, font, color, x, y)
  local t = parent:add(N{ kind = "text", text = text, font = font, color = color, x = x, y = y, shadow = SHADOW })
  local w, h = t:measure(); return t, w, h
end
local function measure(text, font) local p = N{ kind = "text", text = text, font = font }; return p:measure() end

local F, FB = "Tahoma 8", "Tahoma Bold 8"
function tile:fgw() return math.max(60, (self.sb and self.sb.w or 150) - 16) end   -- Border 1 + side strip 9 + padder 6

-- DigitalTime (h:mm #D8E4FF + tt #9EB5BB, 2 px apart) at x, y; returns the width
function tile:digital_time(parent, x, y, bold)
  local f = bold and FB or F
  local hm, w1 = stext(parent, FMT.hm(), f, "#d8e4ff", x, y)
  local tt, w2 = stext(parent, FMT.tt(), f, "#9eb5bb", x + w1 + 2, y)
  table.insert(self.texts, { node = hm, fmt = FMT.hm }); table.insert(self.texts, { node = tt, fmt = FMT.tt })
  return w1 + 2 + w2
end

local LINE = 13   -- Tahoma 8 line height
-- the views; each fills self.fg (a canvas) and returns the tile height (chrome: 3 px top pad + 4 px sizer)
local views = {}
function views.default(self, fg, W)
  if not self.pinned then
    -- 4051-vm-now.png: tile y 54..213 (160 high), face 96 px at tile y 22, centred; "6:37 AM, Oct 23" glyphs at tile y 142..150
    local size = 96
    self:analog(fg, size, math.floor((W - size) / 2), 19)
    -- the 4051 line is larger than 4074's 8 pt DigitalText: 91 px for "6:37 AM, Oct 23" = Tahoma 10 here
    local t = fg:add(N{ kind = "text", text = FMT.default(), font = "Tahoma 10", color = "#ffffff", align = "center", x = 0, y = 134, w = W, h = 18, shadow = SHADOW })
    table.insert(self.texts, { node = t, fmt = FMT.default })
    return 160
  end
  -- clocksidebardefault.xaml: Border 0,2 { 80x80 AnalogClock centred; CenteredDigitalText "*LongTime" } (4074-dwm.png)
  self:analog(fg, 80, math.floor((W - 80) / 2), 2)
  local t = fg:add(N{ kind = "text", text = FMT.default(), font = F, color = "#ffffff", align = "center", x = 0, y = 84, w = W, h = 16, shadow = SHADOW })
  table.insert(self.texts, { node = t, fmt = FMT.default })
  return 105
end
-- digtime*: Border 0,4 { Grid [AnalogClock (Auto, centred vertically)] [5] [lines (Auto, centred vertically)] }, left-aligned.
-- The AnalogClockPanel has no size in these views; 56 px (40 next to the long weekday/date lines) is our choice (inferred).
local function digtime(lines, size)
  return function(self, fg, W)
    local n = #lines + 1
    local th = n * LINE
    local inner = math.max(size, th)
    self:analog(fg, size, 0, 4 + math.floor((inner - size) / 2))
    local x, y = size + 5, 4 + math.floor((inner - th) / 2)
    self:digital_time(fg, x, y, true)
    for i, fmt in ipairs(lines) do
      local t = stext(fg, fmt(), F, "#ffffff", x, y + i * LINE)
      table.insert(self.texts, { node = t, fmt = fmt })
    end
    return 3 + 4 + inner + 4 + 4
  end
end
views.digtime = digtime({}, 56)
views.digtimemonthday = digtime({ FMT.monthday }, 56)
views.digtimeweekday = digtime({ FMT.weekday, FMT.mdy }, 40)
-- minical: Border 0,4 { [42 px AnalogClock][5][MiniCalendar] centred; CenteredDigitalTime bold under it }
function views.minical(self, fg, W)
  local dw = math.ceil(measure(FMT.dayupper(), FB)); local nw = math.ceil(measure(FMT.day(), "Tahoma 11"))
  local cw = math.max(dw + 10, nw + 4) + 2 + 2            -- MiniCalDay margin 5,2 inside Margin 1; Border 1 around
  local daybox = LINE + 4                                  -- 8 pt line + the 2 px vertical text margin (twice)
  local datebox = 18
  local ch = 1 + 1 + daybox + 1 + datebox + 1
  local bw = 42 + 5 + cw
  local x0 = math.floor((W - bw) / 2); local top = 4
  local inner = math.max(42, ch)
  self:analog(fg, 42, x0, top + math.floor((inner - 42) / 2))
  local cx, cy = x0 + 47, top + math.floor((inner - ch) / 2)
  fg:add(N{ kind = "rect", x = cx, y = cy, w = cw, h = ch, fill = "#ffffff", stroke = "#d3d3d3" })
  fg:add(N{ kind = "rect", x = cx + 2, y = cy + 2, w = cw - 4, h = daybox, fill = "#add8e6" })
  local d = fg:add(N{ kind = "text", text = FMT.dayupper(), font = FB, color = "#ffffff", align = "center", x = cx + 2, y = cy + 4, w = cw - 4, h = LINE })
  local n = fg:add(N{ kind = "text", text = FMT.day(), font = "Tahoma 11", color = "#4b5681", align = "center", x = cx + 1, y = cy + 2 + daybox + 1, w = cw - 2, h = datebox })
  table.insert(self.texts, { node = d, fmt = FMT.dayupper }); table.insert(self.texts, { node = n, fmt = FMT.day })
  -- CenteredDigitalTime, bold: measured first, then centred
  local tw = math.ceil(measure(FMT.hm(), FB) + 2 + measure(FMT.tt(), FB))
  local ty = top + inner + 1
  self.center_time = { y = ty, W = W }
  self:digital_time(fg, math.floor((W - tw) / 2), ty, true)
  return 3 + ty + LINE + 4 + 4
end
-- cal (clocksidebarcal.xaml): Grid Margin 4, 7 columns; row 0 = month (Tahoma 7.5 #D4D5D7, right-aligned, Top/Bottom 2) on a
-- horizontal gradient #838A90 → #767B7F; the date rows on a vertical gradient #6B7278 → #5B6366, dates white, centred,
-- margins 4,3,4,3. The XAML's sample is January 2004 (the 1st in column 4 = Thursday): weeks start on Sunday.
function views.cal(self, fg, W)
  local f = "Tahoma 7.5"
  local t = os.date("*t"); local first = os.date("*t", os.time{ year = t.year, month = t.month, day = 1, hour = 12 })
  local days = os.date("*t", os.time{ year = t.year, month = t.month + 1, day = 0, hour = 12 }).day
  local col0 = first.wday - 1
  local rows = math.ceil((col0 + days) / 7)
  local gw = W - 8; local cell = gw / 7
  local hh, rh = 16, 16
  fg:add(N{ kind = "rect", x = 4, y = 4, w = gw, h = hh, gradient = { dir = "h", { 0, "#838a90" }, { 1, "#767b7f" } } })
  fg:add(N{ kind = "rect", x = 4, y = 4 + hh, w = gw, h = rows * rh, gradient = { dir = "v", { 0, "#6b7278" }, { 1, "#5b6366" } } })
  local m = fg:add(N{ kind = "text", text = os.date("%B"), font = f, color = "#d4d5d7", align = "right", valign = "center", x = 4, y = 4, w = gw - 4, h = hh })
  table.insert(self.texts, { node = m, fmt = function() return os.date("%B") end })
  for d = 1, days do
    local i = col0 + d - 1
    local c, r = i % 7, math.floor(i / 7)
    fg:add(N{ kind = "text", text = tostring(d), font = f, color = "#ffffff", align = "center", valign = "center",
      x = 4 + math.floor(c * cell), y = 4 + hh + r * rh, w = math.floor(cell), h = rh })
  end
  self.cal_month = t.month
  return 3 + 4 + hh + rows * rh + 4 + 4
end

function tile:build()
  local fg = self.foreground; fg:clear()
  self.analogs, self.texts, self.center_time = {}, {}, nil
  local W = self:fgw()
  local view = views[self.layout] or views.default
  local c = fg:add(N{ layout = "canvas" })
  local h = view(self, c, W)
  self.built_w = self.sb and self.sb.w
  self:tick()
  return h
end
function tile:tick()
  local t = os.date("*t")
  local ah, am, as = (t.hour % 12 + t.min / 60) * 30, (t.min + t.sec / 60) * 6, t.sec * 6
  for _, a in ipairs(self.analogs or {}) do
    a.hour:set{ angle = ah }; a.minute:set{ angle = am }; a.second:set{ angle = as }
    if a.shadow then a.shadow.hour:set{ angle = ah }; a.shadow.minute:set{ angle = am }; a.shadow.second:set{ angle = as } end
  end
  for _, x in ipairs(self.texts or {}) do local v = x.fmt(); if v ~= x.last then x.last = v; x.node:set{ text = v } end end
  if self.layout == "cal" and self.cal_month and self.cal_month ~= t.month then self:relayout() end
end
-- a new layout / width / pinned state: rebuild the content, animate the tile to the view's height
function tile:relayout(now)
  local h = self:build()
  if self.sb and self.sb.resize_tile then self.sb.resize_tile(self, h, now) end
end
function tile:initialize()
  local S = lh.session
  self.layout = S.get("clock", "layout") or "default"
  if not TITLES[self.layout] then self.layout = "default" end
  local h = self:build()
  if self.sb and self.sb.resize_tile then self.sb.resize_tile(self, h, true) end
  self.timer = lh.timer(function() self:tick() end, 1000, true)
  self.on_width = function(w) if self.built_w ~= w then self:relayout(true) end end
  lh.on("sidebar-resize", self.on_width)
  -- settings.ini [clock] layout changed by another writer (runtime.lua reload_settings)
  self.on_layout = function(l) if TITLES[l] and l ~= self.layout then self.layout = l; self:relayout() end end
  lh.on("clock-layout", self.on_layout)
end
-- context menu entries (runtime.lua's tile menu): "Clock layout" (the views) and pinning
function tile:menu()
  local sub = {}
  for _, l in ipairs(LAYOUTS) do
    table.insert(sub, { text = TITLES[l], radio = true, checked = self.layout == l, action = function()
      lh.session.set("clock", "layout", l); self.layout = l; self:relayout() end })
  end
  return {
    { text = "Clock &layout", submenu = sub },
    { text = "Pinned to the &bottom (4074)", checked = self.pinned and true or false, action = function() lh._emit("clock-pinned", not self.pinned) end },
  }
end
-- "Clock Tile Properties" (the tile menu; 4074's clock property page clockfacessheet.xaml lists the same views): a flyout next to
-- the tile in the generic chrome (flyout.lua) with the layouts as radio rows and the pinned switch; Up/Down/Return/space, Escape
function tile:properties()
  local me = self
  local F = "Segoe UI,Tahoma 12px"
  local owner = { node = self.node, rows = {}, focus = 1, id = self.id }
  local function paint() for i, r in ipairs(owner.rows) do r.hl:set{ visible = i == owner.focus } end if lh.flyout then lh.flyout.surface:damage() end end
  function owner:flyout()
    local W = 230
    local root = N{ layout = "fill", w = W }
    root:add(N{ kind = "rect", fill = "#ffffff" })
    local col = root:add(N{ layout = "vbox" })
    local function heading(t) col:add(N{ kind = "text", text = t, font = "Segoe UI,Tahoma Bold 14px", color = "#808080", margin = { 8, 8, 8, 4 } }) end
    local function option(kind, text, on, act)
      local r = col:add(N{ layout = "fill", h = 20, margin = { 6, 0, 6, 0 }, hoverable = true })
      local hl = r:add(N{ kind = "rect", fill = "#c5e7f6", radius = 2, visible = false })
      local d = r:add(N{ layout = "dock" })
      local mark = d:add(N{ dock = "left", w = 20, layout = "canvas" })
      if kind == "radio" then
        mark:add(N{ kind = "rect", x = 4, y = 4, w = 11, h = 11, radius = 5.5, fill = "#ffffff", stroke = "#808080" })
        if on then mark:add(N{ kind = "rect", x = 7, y = 7, w = 5, h = 5, radius = 2.5, fill = "#2a5bd7" }) end
      else
        mark:add(N{ kind = "rect", x = 4, y = 4, w = 12, h = 12, fill = "#ffffff", stroke = "#808080" })
        if on then mark:add(N{ kind = "text", text = "✓", font = "Sans Bold 11px", color = "#2a5bd7", x = 5, y = 2, w = 12, h = 14 }) end
      end
      d:add(N{ kind = "text", text = text, font = F, color = "#000000", dock = "fill", valign = "center", ellipsis = true })
      local idx = #owner.rows + 1
      owner.rows[idx] = { hl = hl, act = act }
      r.on_hover = function(_, over) if over then owner.focus = idx; paint() end end
      r.on_click = function(_, b) if b == 1 then act() end end
    end
    heading("Clock layout")
    for _, l in ipairs(LAYOUTS) do
      option("radio", TITLES[l], me.layout == l, function()
        lh.session.set("clock", "layout", l); me.layout = l; me:relayout()
        lh.timer(function() if lh.flyout_tile == owner then me:properties() end end, 0) end)
    end
    col:add(N{ h = 9, layout = "fill", margin = { 8, 6, 8, 2 } }):add(N{ kind = "rect", fill = "#c0c0c0", h = 1 })
    option("check", "Pinned to the bottom (4074)", me.pinned and true or false, function() lh.close_flyout(); lh._emit("clock-pinned", not me.pinned) end)
    col:add(N{ h = 8 })
    local _, h = col:desired(W, 2000)
    lh.timer(paint, 0)
    return root, W, h
  end
  function owner:on_key(name)
    local n = #self.rows; if n == 0 then return end
    if name == "Down" or name == "Tab" then self.focus = self.focus % n + 1; paint()
    elseif name == "Up" or name == "ISO_Left_Tab" then self.focus = (self.focus + n - 2) % n + 1; paint()
    elseif name == "Return" or name == "KP_Enter" or name == "space" then local r = self.rows[self.focus]; if r then r.act() end end
  end
  lh.open_flyout(owner)
end
-- the minimized sidebar's taskbar view (TBContentHost, SHELL-DESIGN 6.4): the digital time, white with the ShadowText 1,1
function tile:taskbar(host)
  local ts = lh.taskbar_text_style and lh.taskbar_text_style() or { color = "#ffffff" }   -- pack key taskbar_text (runtime.lua)
  self.tbtext = host:add(N{ kind = "text", text = "", color = ts.color, glow = ts.glow, glow_size = ts.glow_size, glow_strength = ts.glow_strength,
    font = self.T.font, valign = "center", margin = { 6, 0, 6, 1 }, shadow = not ts.glow and SHADOW or nil })
  local function tick() local t = os.date("%I:%M %p"):gsub("^0", ""); if t ~= self.tbtext.last then self.tbtext.last = t; self.tbtext:set{ text = t } end end
  tick(); self.timer = lh.timer(tick, 1000, true)
end
function tile:dispose() if self.timer then lh.cancel(self.timer) end if self.on_width then lh.off("sidebar-resize", self.on_width) end if self.on_layout then lh.off("clock-layout", self.on_layout) end end
return tile
