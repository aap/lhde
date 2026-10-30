-- Clock tile: analogclock.xaml + the clocksidebar*.xaml views (SHELL-DESIGN 2.3.4, "Clock tile modes" in the M6 status).
--   state.ini [clock] pinned = 1 (4074: SBPinnedContentHostCollection at the bottom of the bar) | 0 (4051: a normal tile at
--             its place in the tile list, 4051-vm-now.png: ~96 px face, "6:37 AM, Oct 23" under it, tile 160 high)
--             [clock] layout = default | digtime | digtimemonthday | digtimeweekday | minical | cal ("Clock layout" in the
--             tile's context menu; the 4074 property sheet clockfacessheet.xaml lists the same views)
-- Analog face: 4074's AnalogClockPanel (WCPClient SidebarClockSettings, ~/fun/longhorn/sidebar/CLOCK.md 1.1): seven image layers
-- at design size 260, each rotated about its LayerOrigin: face 1074 (129.5,129.5), hour 1077 4x95 (1.5,75.5), minute 1078 4x125
-- (1.5,105.5), anchor 1083 12x12 (5.5,5.5), second 1079 8x128 (3.5,98.5), glass 1076 (129.5,129.5); one 1 s timer, angles from
-- whole seconds (the second hand ticks). The face set follows the theme's visual style (tile:face_set): slate* the light grey
-- set, jade* and aero* the dark-blue set 1188-1198 of 4074-dwm.png (its pivots are estimates, see "Corrections"), plex* the
-- Plex-era light set, anything else the light set; pack key clock_face = "dwm" | "light" | "plex" overrides.
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
-- The culture-driven "*" formats (ClockDateTime) from the C library's LC_TIME: luxshell itself leaves the locale at "C", so
-- the time category is taken from the environment here (strftime only). *ShortTime = the locale's long time (%X) without the
-- seconds, the 12-hour form without its leading zero (en-US "h:mm tt"); *LongTime = that + ", " + the month-day pattern with
-- MMMM -> MMM and dd -> d, its order (day or month first) read off the locale's %x.
pcall(os.setlocale, "", "time")
local function short_time(t)
  local s = os.date("%X", t):gsub("^(.-%d+[:.]%d+)[:.]%d+", "%1")
  if os.date("%p", t) ~= "" then s = s:gsub("^0(%d)", "%1") end
  return s
end
local day_first = (function() local s = os.date("%x", os.time{ year = 2003, month = 11, day = 25, hour = 12 })
  local d, m = s:find("25", 1, true), s:find("11", 1, true); return d and m and d < m end)()
local function month_day(t)
  local d = tostring(tonumber(os.date("%d", t)))
  return day_first and (d .. " " .. os.date("%b", t)) or (os.date("%b", t) .. " " .. d)
end
-- the first day of the week (DateTimeFormatInfo.FirstDayOfWeek): glibc's LC_TIME first_weekday (1 = the day of week-1stday,
-- 19971130 = a Sunday); 0 = Sunday ... 6 = Saturday; Sunday when the C library cannot tell
local FIRST_DAY = (function()
  local p = io.popen("locale first_weekday week-1stday 2>/dev/null"); if not p then return 0 end
  local fw, base = tonumber(p:read("l") or ""), p:read("l"); p:close()
  local y, m, d = (base or ""):match("^(%d%d%d%d)(%d%d)(%d%d)$")
  if not fw or not y then return 0 end
  local w0 = os.date("*t", os.time{ year = tonumber(y), month = tonumber(m), day = tonumber(d), hour = 12 }).wday - 1
  return (w0 + fw - 1) % 7
end)()
local FMT = {
  default = function() return short_time() .. ", " .. month_day() end,  -- "*LongTime": 4074 "5:57 AM, Oct 23" (lh4074-181541.png)
  hm = function() return nozero(os.date("%I:%M")) end,                    -- XShortTime "h:mm"
  tt = function() return os.date("%p") end,                                -- XAMPM "tt"
  monthday = function() return os.date("%B %d") end,                       -- "*MonthDay" = en-US MonthDayPattern "MMMM dd"
  weekday = function() return os.date("%A") end,                           -- "dddd"
  mdy = function() return (os.date("%B %d, %Y"):gsub(" 0(%d),", " %1,")) end, -- "MMMM d, yyyy"
  dayupper = function() return os.date("%a"):upper() end,                  -- "^ddd"
  day = function() return tostring(tonumber(os.date("%d"))) end,           -- "%d"
}

-- the face set for the theme: "light" | "dwm" | "plex", from the resolved theme's VisualStyle.style (the pack key clock_face
-- overrides):  slate* -> light;  jade*, aero* (aero4074, aerovision, aero5048, aero5112, basic5112) -> dwm;  plex* -> plex;
-- anything else (luna, classic, ...) -> light
function tile:face_set()
  local o = self.T.clock_face
  if o == "dwm" or o == "light" or o == "plex" then return o end
  local e; if lh.theme_name then e = select(2, lh.theme_name()) end
  local st = (e and e.v and e.v["VisualStyle.style"] or ""):lower()
  if st:match("^jade") or st:match("^aero") then return "dwm" end
  if st:match("^plex") then return "plex" end
  return "light"
end
-- Plex: the Plex builds' clock is the light layered clock of 4042-4053 (CLOCK.md 3: the 260 px set 1074 face, 1076 glass,
-- 1077/1078/1079 hands, 1083 hub on the same LayerOrigins, byte-identical in 4051; 4029/4039 had a 179 px set with full-size
-- hand layers and the blue hover face 1075 instead, which this layer code does not draw). A Plex theme takes those ids from
-- packs/4042/imageres (imageres.dll of build 4042, an lhde-import imageres recipe for 4042) when they are imported, else from
-- the 4074 light set, which is the same art.
local PLEX_DIR = "4042/imageres"
local function plex_image(R, id)
  local f = PLEX_DIR .. "/" .. id .. ".png"
  if lh.data_find("packs/" .. f) then return f end
  return R(id)
end
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
  local set = self:face_set()
  local plex = set == "plex"
  if set == "dwm" then
    -- the DWM's dark-blue set (4074-dwm.png); hand pivots estimated from that shot (tail ~22 design px, second hand ~35)
    clock:add(N{ kind = "image", image = R(1190), x = c - 263 * s / 2, y = c - 263 * s / 2, w = 263 * s, h = 263 * s })
    local hs, ms, ss = layer(1194, 12, 103, 93, 3), layer(1196, 12, 133, 123, 3), layer(1198, 15, 136, 124, 3)
    hands.hour, hands.minute, hands.second = layer(1193, 11, 122, 100), layer(1195, 11, 151, 129), layer(1197, 11, 163, 128)
    hands.shadow = { hour = hs, minute = ms, second = ss }
    clock:add(N{ kind = "image", image = R(1189), x = c - 6 * s, y = c - 6 * s, w = 12 * s, h = 12 * s })
    clock:add(N{ kind = "image", image = R(1191), x = c - 265 * s / 2, y = c - 265 * s / 2, w = 265 * s, h = 265 * s })
  else
    -- AnalogClockPanel: each layer w x h with its LayerOrigin (ox, oy) on the panel centre. The origins count pixel centres
    -- (129.5 of a 260 face is its middle), so the image's edge-coordinate pivot is origin + 0.5.
    local function img(id, w, h, ox, oy, rot)
      local parent = clock
      if rot then parent = clock:add(N{ kind = "transform", x = 0, y = 0, w = size, h = size, origin = {c, c}, layout = "canvas" }) end
      parent:add(N{ kind = "image", image = plex and plex_image(R, id) or R(id), x = c - (ox + 0.5) * s, y = c - (oy + 0.5) * s, w = w * s, h = h * s })
      return parent
    end
    img(1074, 260, 260, 129.5, 129.5)
    hands.hour = img(1077, 4, 95, 1.5, 75.5, true)
    hands.minute = img(1078, 4, 125, 1.5, 105.5, true)
    img(1083, 12, 12, 5.5, 5.5)
    hands.second = img(1079, 8, 128, 3.5, 98.5, true)
    img(1076, 260, 260, 129.5, 129.5)
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
    -- 4051, the tile sized by its content (WCPClient 4051, res-clock/il/wcpclient-4051-clock.il.txt):
    --  * SideBarClock.SidebarPanel: DockPanel Margin 0,6,0,6 { DigitalClockTime Dock Bottom, Height = DigitalFont (Tahoma 10)
    --    measured on "A..Z0..9"; AnalogClockPanel Fill, PanelMargin = ClockSidebarMargin 16,0, Bias 0,0 };
    --  * AnalogClockPanel.MeasureCore: with no height limit (the tile measures its content at infinite height) the panel is
    --    square by the width, MaxHeight = ClockSize 260 (BuildClock): panel height = min(W, 260);
    --  * RotatedPanel.XForm (every layer): scale = min((w - 2 x 16) / 260, h / 260, 1), centred in the panel:
    --    face = min(W - 32, 260), so it grows with the bar width up to the native 260.
    -- Checked on the shots: 150 px bar (W 134): face 102 (grey edge 98 px, 4051-vm-now.png, lh4051-194203.png); 410 px bar
    -- (W 393): face 260 (grey 252, x 821..1071 centred on the foreground, lh4051-163648.png). Rows on both: the panel starts
    -- one row under the tile's top (the 6 px top margin does not show), the time glyphs 6 rows under it, the tile ends 3 rows
    -- under the time line + the sizer: tile height = panel + 24 (158 and 284 rows, as measured).
    local ph = math.min(W, 260)
    local size = math.max(32, math.min(W - 32, 260))   -- MinHeight 32
    local py = -2
    self:analog(fg, size, math.floor((W - size) / 2), py + math.floor((ph - size) / 2))
    -- the 4051 line is larger than 4074's 8 pt DigitalText: 91 px for "6:37 AM, Oct 23" = Tahoma 10 (13 px) here
    local t = fg:add(N{ kind = "text", text = FMT.default(), font = "Tahoma 10", color = "#ffffff", align = "center", x = 0, y = py + ph + 1, w = W, h = 18, shadow = SHADOW })
    table.insert(self.texts, { node = t, fmt = FMT.default })
    return ph + 24
  end
  -- clocksidebardefault.xaml: Border 0,2 { 80x80 AnalogClock centred; CenteredDigitalText "*LongTime" }. lh4074-181541.png:
  -- separator 1199 at y 759..761, face y 765..844, text glyphs 847..854, screen bottom 864: the pinned host is 102 high and
  -- has the face 3 rows under its top (one row above our tile chrome's foreground); face x 1041..1120 on the 150 px bar at 1002:
  -- the host has no 9 px side strip, so face and text are centred over the foreground and that strip
  local HW = W + 9
  self:analog(fg, 80, math.ceil((HW - 80) / 2), -1)
  local t = fg:add(N{ kind = "text", text = FMT.default(), font = F, color = "#ffffff", align = "center", x = 0, y = 79, w = HW, h = 16, shadow = SHADOW })
  table.insert(self.texts, { node = t, fmt = FMT.default })
  return 102
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
  self.built_face = self:face_set()
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
  if self.fly then self:fly_tick(ah, am, as) end
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
  -- a theme switch within the same pack (jade4074 <-> aero4074) only emits "pack": the face set may change with the style
  self.on_pack = function() if self.built_face ~= self:face_set() then self:relayout(true) end end
  lh.on("pack", self.on_pack)
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

------------------------------------------------------------------ the calendar flyout (4074 clockflyout.xaml, CLOCK.md 2)
-- A click on the tile (or its arrow) opens it, a second click on the tile, Escape or a click outside closes it (4074's Popup
-- CloseMode 2: it went away with the focus). tile.stay_open = true keeps it open until a click on the tile (flyout.lua: no grab, so no Escape either).
tile.stay_open = false
-- Generic chrome (flyout.lua: 1175 nine-grid 53,3,3,3 at 70 % black on the ARGB popup, Slate 1109; the 17 px Options row; border
-- 8,4,8,8 / Slate 4,0,4,4) around MainPanel: TimePanel 270 wide + the CalendarElement with margin 0,0,4,4. Content 493 x 303
-- gives Slate's 501x324 (lh4074-181636.png); Jade/Aero measures 510x336 (lh4074-181714.png x 634..1143, y 523..858) with the
-- calendar panel at the same content offset, so the Aero content is 4 rows taller here (one column short, kept at 493).
local FLY = { w = 493, h = 303, h_aero = 307, col = 270, face = 260 }
-- CalendarElement (basecalendarelement.xaml, IL74 ShowMonth), measured on lh4074-181714.png in panel coordinates: panel 1080
-- 219x294 nine-grid 11; photo 1115 + month - 1 UniformToFill, 114 high, clipped to the frame's inside (rows 1..113, columns
-- 1..217); 5 px; CalPanel 25 high (month title centred, Prev/Next 25x25 12 px in); the grid (margin 15,0,16,18, CellSpacing 5
-- around the cells too: first column at 20) with every cell as wide as the widest entry + CalendarWideDataDelta 5. Glyph tops:
-- title 127, weekday header 152, the six week rows 172 + 17.75 k (172 189 207 225 243 [260]); the today box (1082 nine-grid 4)
-- covers the cell from 2 px above the glyphs, 12 high.
local CAL = { w = 219, h = 294, photo = 114, pad = 5, bar = 25, arrow = 12, left = 15, gap = 5, delta = 5,
              title_top = 127, head_top = 152, row_top = 172, pitch = 17.75 }
local CF = { month = "Tahoma Bold 9", day = "Tahoma 8", date = "Tahoma 8", today = "Tahoma Bold 8", time = "Tahoma 18" }
local CC = { month = "#ff929bb3", day = "#ff929bb3", date = "#ff3e3e3e", other = "#ffa6a6a6", today = "#ff000000" }
local ASC = { [8] = 3, [9] = 4, [18] = 7 }   -- GDI Tahoma: rows from the text box top to the cap top at 8, 9 and 18 pt

local function days_in(y, m) return os.date("*t", os.time{ year = y, month = m + 1, day = 0, hour = 12 }).day end

-- the month grid, photo and title for (year, month) into the flyout's calendar parts
function tile:show_month(y, m)
  local f = self.fly; f.y, f.m = y, m
  local now = os.date("*t")
  local photo = self.R(1115 + m - 1)
  local pw, ph = (m == 12) and 323 or 329, 129                      -- 1115-1125 329x129, 1126 323x129
  local sc = math.max(CAL.w / pw, CAL.photo / ph)                    -- Stretch UniformToFill over the 219 x 114 rectangle
  f.photo:clear()
  f.photo:add(N{ kind = "image", image = photo, x = (CAL.w - pw * sc) / 2 - 1, y = -1, w = pw * sc, h = ph * sc })
  local t1 = os.time{ year = y, month = m, day = 1, hour = 12 }
  local title = (y == now.year) and os.date("%B", t1) or os.date("%B, %Y", t1)   -- "MMMM", else the YearMonthPattern (en-US)
  f.title:set{ text = title:upper() }
  local g = f.grid; g:clear()
  -- 7 + 42 entries: the weekday names from FirstDayOfWeek, then the days from the first weekday on or before the 1st
  local names, cells = {}, {}
  for i = 0, 6 do names[i + 1] = os.date("%a", os.time{ year = 2003, month = 11, day = 30 + (FIRST_DAY + i), hour = 12 }) end
  local first = os.date("*t", t1).wday - 1
  local back = (first - FIRST_DAY) % 7
  local n = days_in(y, m)
  for i = 0, 41 do
    local t = os.date("*t", os.time{ year = y, month = m, day = 1 - back + i, hour = 12 })
    local inm = t.month == m
    local today = inm and t.day == now.day and m == now.month and y == now.year
    -- a sixth week made only of next-month days (index > 35 outside the month) is left blank
    cells[i + 1] = { text = (i >= 35 and not inm) and "" or tostring(t.day), inm = inm, today = today }
  end
  -- JustifyGrid: every cell as wide as the widest entry, the widest date measured again in the bold today font, + 5
  -- (CalendarWideDataDelta) when the weekday names are the narrower
  local hw, dw = 0, 0
  for _, s in ipairs(names) do hw = math.max(hw, (measure(s, CF.day))) end
  for _, c in ipairs(cells) do if c.text ~= "" then dw = math.max(dw, (measure(c.text, CF.today))) end end
  -- Avalon measured 8 pt as 10.67 px with the font's unhinted advances ("Wed" 21.12 = the 26.12 column pitch of the shot); GDI's
  -- hinted 11 px widths scaled by 32/33 come within a pixel of that over the seven columns
  hw, dw = hw * 32 / 33, dw * 32 / 33
  local cw = math.max(hw, dw) + (hw < dw and CAL.delta or 0)
  local x0 = CAL.left + CAL.gap
  local function colx(k) return x0 + k * (cw + CAL.gap) end
  for k = 0, 6 do
    g:add(N{ kind = "text", text = names[k + 1], font = CF.day, color = CC.day, align = "center",
      x = math.floor(colx(k) + 0.5), y = CAL.head_top - ASC[8], w = math.floor(cw + 0.5), h = 13 })
  end
  for i, c in ipairs(cells) do
    local k, r = (i - 1) % 7, (i - 1) // 7
    local top = math.floor(CAL.row_top + r * CAL.pitch)
    local x = colx(k)
    if c.today then
      g:add(N{ kind = "ninegrid", image = self.R(1082), grid = 4, x = math.floor(x + 0.5), y = top - 2, w = math.floor(cw + 0.5), h = 12 })
    end
    if c.text ~= "" then
      g:add(N{ kind = "text", text = c.text, font = c.today and CF.today or CF.date,
        color = c.today and CC.today or (c.inm and CC.date or CC.other), align = "center",
        x = math.floor(x + 0.5), y = top - ASC[8], w = math.floor(cw + 0.5), h = 13 })
    end
  end
  if lh.flyout then lh.flyout.surface:damage() end
end

function tile:flyout()
  local me, R = self, self.R
  local slate = self.T.name == "slate"
  local H = slate and FLY.h or FLY.h_aero
  local root = N{ layout = "canvas", w = FLY.w, h = H }
  -- TimePanel: the 260 PolarCanvas with the AnalogClock at its full 260 (lh4074-181714.png: the face's grey 646..898 x 548..800,
  -- centred on content 130,130, as the 260 px render of the layer stack)
  self.fly = { analogs = {}, texts = {} }
  local save_a, save_t = self.analogs, self.texts
  self.analogs, self.texts = self.fly.analogs, self.fly.texts
  local fs = FLY.face
  self:analog(root, fs, 131 - fs / 2, 131 - fs / 2)   -- the shot has the face's centre one pixel right of and below 130,130
  -- FlyoutDigitalTime: CenteredDigitalTime at Tahoma 18 pt, normal weight: "h:mm" #D8E4FF + "tt" #9EB5BB 2 px apart, centred in the
  -- 270 column, glyph tops at 269 (lh4074-181714.png: "5:59 AM" x 94..172, y 269..285)
  local tw = math.ceil(measure(FMT.hm(), CF.time) + 2 + measure(FMT.tt(), CF.time))
  local tx, ty = math.floor((FLY.col - tw) / 2), 269 - ASC[18]
  local hm, w1 = stext(root, FMT.hm(), CF.time, "#ffd8e4ff", tx, ty)
  local tt = stext(root, FMT.tt(), CF.time, "#ff9eb5bb", tx + w1 + 2, ty)
  self.fly.hm, self.fly.tt, self.fly.tx = hm, tt, { y = ty }
  self.analogs, self.texts = save_a, save_t
  -- CalendarElement
  local cal = root:add(N{ layout = "canvas", x = FLY.col, y = 0, w = CAL.w, h = CAL.h })
  cal:add(N{ kind = "ninegrid", image = R(1080), grid = 11, x = 0, y = 0, w = CAL.w, h = CAL.h })
  self.fly.photo = cal:add(N{ layout = "canvas", x = 1, y = 1, w = CAL.w - 2, h = CAL.photo - 1, clip = true })
  -- CalendarHoverArea (AreaButton): the title row and the grid; Prev/Next fade 0 -> 1 in 250 ms while the pointer is over it
  local area = cal:add(N{ layout = "canvas", x = 0, y = CAL.photo, w = CAL.w, h = CAL.h - CAL.photo, hoverable = true })
  local bar_y = CAL.pad
  self.fly.title = area:add(N{ kind = "text", text = "", font = CF.month, color = CC.month, align = "center",
    x = 0, y = CAL.title_top - CAL.photo - ASC[9], w = CAL.w, h = 15 })
  self.fly.grid = area:add(N{ layout = "canvas", x = 0, y = -CAL.photo, w = CAL.w, h = CAL.h })
  local over = {}
  local shown = false
  local function fade()
    lh.timer(function()
      local on = over.area or over.prev or over.next or false
      if on == shown or not me.fly then return end
      shown = on
      lh.animate(me.fly.prev, "opacity", on and 1 or 0, 250); lh.animate(me.fly.next, "opacity", on and 1 or 0, 250)
    end, 0)
  end
  local function arrow(key, x, ids, step)
    local b = area:add(N{ kind = "button", x = x, y = bar_y, w = 25, h = 25, opacity = 0,
      images = { normal = R(ids[1]), hot = R(ids[2]), pressed = R(ids[3]) } })
    b.on_hover = function(_, o) over[key] = o; fade() end
    b.on_click = function(_, button)
      if button ~= 1 then return end
      local y, m = me.fly.y, me.fly.m + step
      if m < 1 then m, y = 12, y - 1 elseif m > 12 then m, y = 1, y + 1 end
      me:show_month(y, m)
    end
    return b
  end
  self.fly.prev = arrow("prev", CAL.arrow, { 1084, 1085, 1086 }, -1)
  self.fly.next = arrow("next", CAL.w - CAL.arrow - 25, { 1087, 1088, 1089 }, 1)
  area.on_hover = function(_, o) over.area = o; fade() end
  local now = os.date("*t")
  self:show_month(now.year, now.month)
  self:fly_tick()
  return root, FLY.w, H
end
-- 4074 places it at the tile's top, clamped to the screen bottom with 5 rows free, its right edge 9 px short of the screen
-- edge: over the bar and the pinned clock itself (lh4074-181636.png x 642..1142, y 535..858 on 1152x864)
function tile:place(fw, fh)
  local sw, sh = lh.screen()
  local _, ty = self.sb.tile_rect(self)
  return sw - 9 - fw, math.max(0, math.min(ty, sh - 5 - fh))
end
function tile:on_key() end          -- the flyout takes the keyboard: Escape closes it (flyout.lua)
function tile:flyout_closed() self.fly = nil end
function tile:fly_tick(ah, am, as)
  local f = self.fly; if not f then return end
  if not ah then local t = os.date("*t"); ah, am, as = (t.hour % 12 + t.min / 60) * 30, (t.min + t.sec / 60) * 6, t.sec * 6 end
  for _, a in ipairs(f.analogs) do
    a.hour:set{ angle = ah }; a.minute:set{ angle = am }; a.second:set{ angle = as }
    if a.shadow then a.shadow.hour:set{ angle = ah }; a.shadow.minute:set{ angle = am }; a.shadow.second:set{ angle = as } end
  end
  local hm, tt = FMT.hm(), FMT.tt()
  if hm ~= f.last_hm or tt ~= f.last_tt then
    f.last_hm, f.last_tt = hm, tt
    local w1, w2 = measure(hm, CF.time), measure(tt, CF.time)
    local x = math.floor((FLY.col - math.ceil(w1 + 2 + w2)) / 2)
    f.hm:set{ text = hm, x = x }; f.tt:set{ text = tt, x = x + w1 + 2 }
  end
end

function tile:dispose() if self.fly and lh.flyout_tile == self then lh.close_flyout() end if self.timer then lh.cancel(self.timer) end if self.on_width then lh.off("sidebar-resize", self.on_width) end if self.on_layout then lh.off("clock-layout", self.on_layout) end if self.on_pack then lh.off("pack", self.on_pack) end end
return tile
