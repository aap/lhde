-- Power tile: powergauge.xaml (4074) with the battery drawn as in lh4074-181415.png ("Charging: 95%", Slate sidebar at x 1003):
-- tile body y 168..212 + 4 px sizer = 49; the XAML's rotated battery (RotateTransform 270 about 23,17, ScaleX .95) lands
-- at x 7..24 / y 7..41 of the tile with the nub on top; gauge box #808080 104x19 at x 33, y 6, the ShinyGreen gauge inset
-- 2 px (100x15) over a 40 % copy of itself and a 15 % GraySweep; text Segoe UI 13 bold #dddddd under the gauge, centred.
-- Data: /sys/class/power_supply/BAT0 (capacity, status, energy_* / charge_*, power_now / current_now) and AC/online,
-- polled every 30 s. Click = flyout (powerflyout.xaml, 194 px wide on white) with the details.
local tile = { name = "Power", height = 49 }
local brushes = {   -- powergauge.xaml LinearGradientBrushes (vertical; the battery's ShinyWhite runs across after the 270° turn)
  ShinyWhite  = { {0, "#cdc7af"}, {0.10, "#cdc7af"}, {0.25, "#dddddd"}, {0.40, "#cdc7af"}, {0.8, "#cdc7af"}, {1, "#ada76f"} },
  ShinyGreen  = { {0, "#4fdf26"}, {0.3, "#a1fe86"}, {1, "#4bde21"} },
  ShinyYellow = { {0, "#fff200"}, {0.5, "#fff799"}, {1, "#fff200"} },
  ShinyRed    = { {0, "#d71b0c"}, {0.5, "#f0c3bf"}, {1, "#d71b0c"} },
  ShinyGray   = { {0, "#333333"}, {0.5, "#babaa3"}, {1, "#333333"} },
  GraySweep   = { dir = "h", {0, "#000000"}, {0.8, "#777777"}, {1, "#888888"} },
}
local function brush(name, dir) local b = { dir = dir or brushes[name].dir }; for i, s in ipairs(brushes[name]) do b[i] = s end return b end
local PS = os.getenv("LHSHELL_POWER_SUPPLY") or "/sys/class/power_supply"

local function rd(path) local f = io.open(path); if not f then return nil end local v = f:read("l"); f:close(); return v end
local function num(path) local v = rd(path); return v and tonumber(v) end
function tile.read()
  local b = PS .. "/" .. (os.getenv("LHSHELL_BATTERY") or "BAT0") .. "/"
  local s = { present = rd(b .. "status") ~= nil, status = rd(b .. "status") or "Unknown", capacity = num(b .. "capacity"), ac = num(PS .. "/AC/online") == 1,
              model = rd(b .. "model_name"), tech = rd(b .. "technology") }
  local now, full, rate = num(b .. "energy_now"), num(b .. "energy_full"), num(b .. "power_now")          -- µWh, µW
  if not now then now, full, rate = num(b .. "charge_now"), num(b .. "charge_full"), num(b .. "current_now") end   -- µAh, µA
  s.now, s.full, s.rate, s.unit = now, full, rate, num(b .. "energy_now") and "Wh" or "Ah"
  if not s.capacity and now and full and full > 0 then s.capacity = math.floor(now * 100 / full + 0.5) end
  if rate and rate > 0 and now then
    if s.status == "Discharging" then s.hours = now / rate elseif s.status == "Charging" and full then s.hours = (full - now) / rate end
  end
  return s
end
local function hm(h) if not h then return nil end local m = math.floor(h * 60 + 0.5); return string.format("%dh%02d", m // 60, m % 60) end
function tile.label(s)
  if not s.present then return "No battery" end
  local p = (s.capacity or 0) .. "%"
  if s.status == "Charging" then return "Charging: " .. p end
  if s.status == "Discharging" then return (hm(s.hours) or "?") .. " left: " .. p end            -- the XAML's sample text is "4h23"
  if s.status == "Full" then return "Charged: " .. p end
  return (s.ac and "On AC: " or "") .. p
end
local function gauge_brush(c) return (c or 0) <= 10 and "ShinyRed" or (c or 0) <= 25 and "ShinyYellow" or "ShinyGreen" end

-- battery + gauge + text in a canvas; k scales the whole drawing (1 in the tile, 1.3 in the flyout)
local function build_gauge(parent, x0, y0, gw, k, font, color)
  local g = {}
  local c = parent:add(lh.node{ layout = "canvas" })
  local function R(x, y, w, h, t) t = t or {}; t.kind = "rect"; t.x = x0 + x * k; t.y = y0 + y * k; t.w = w * k; t.h = h * k; return c:add(lh.node(t)) end
  R(4, 0, 9, 3, { gradient = brush("ShinyGray", "h") })                          -- nub (ShinyGray, turned)
  R(0, 3, 18, 35, { gradient = brush("ShinyWhite", "h") })                       -- battery_body: coppertop / copper, ShinyWhite
  R(8, 7, 2, 6, { fill = "#ff0000" }); R(6, 9, 6, 2, { fill = "#ff0000" })        -- plus_v / plus_h
  R(6, 31, 6, 2, { fill = "#ff0000" })                                           -- minus
  g.plug = c:add(lh.node{ layout = "canvas", opacity = 0 })                      -- ACLogo: opacity 0 → 1, 2 s, AutoReverse, forever
  local function P(x, y, w, h) g.plug:add(lh.node{ kind = "rect", fill = "#000000", x = x0 + x * k, y = y0 + y * k, w = w * k, h = h * k }) end
  P(8, 16, 4, 3); P(5, 19, 9, 7); P(6, 26, 2, 3); P(10, 26, 2, 3)              -- cord, body, prongs
  local gx = 26
  R(gx, 2, gw, 19, { fill = "#808080" })                                         -- DockPanel Background #FF808080 Height 21 (19 visible)
  g.inner = gw - 4
  g.bkg = R(gx + 2, 4, g.inner, 15, { gradient = brush("ShinyGreen"), opacity = 0.4 })   -- gaugebkg
  g.bar = R(gx + 2, 4, g.inner * 0.75, 15, { gradient = brush("ShinyGreen") })           -- gauge
  g.sweep = R(gx + 2, 4, g.inner * 0.75, 15, { gradient = brush("GraySweep"), opacity = 0.15 })   -- gaugesweep
  g.text = c:add(lh.node{ kind = "text", text = "", font = font, color = color, align = "center", x = x0 + gx * k, y = y0 + 25 * k, w = gw * k, h = 16 * k })
  g.k, g.x0, g.gx = k, x0, gx
  function g.update(s)
    local cap = math.max(0, math.min(100, s.capacity or 0)); local br = brush(gauge_brush(cap))
    g.bkg:set{ gradient = br }; g.bar:set{ gradient = br, w = g.inner * cap / 100 * k }; g.sweep:set{ w = g.inner * cap / 100 * k }
    g.text:set{ text = tile.label(s) }
  end
  return g
end
tile.build_gauge = build_gauge

function tile:initialize()
  local font = self.T.power_font or "Segoe UI,Helvetica,Liberation Sans Bold 13px"   -- Segoe UI 13 bold; Helvetica matches its 86 px "Charging: 95%" here
  self.g = build_gauge(self.foreground, 0, 1, 104, 1, font, "#dddddd")
  local phase = 0
  local function refresh()
    local s = self.read(); self.state = s; self.g.update(s)
    if s.ac and s.status == "Charging" then                                     -- ACLogo pulse only while charging on AC
      if not self.pulse then self.pulse = lh.timer(function() phase = (phase + 125) % 4000; local t = phase / 2000; self.g.plug:set{ opacity = t <= 1 and t or 2 - t } end, 125, true) end
    else if self.pulse then lh.cancel(self.pulse); self.pulse = nil end self.g.plug:set{ opacity = 0 } end
    if self.flyout_refresh then self.flyout_refresh(s) end
  end
  refresh(); self.timer = lh.timer(refresh, 30000, true)
end
function tile:dispose() if self.timer then lh.cancel(self.timer) end if self.pulse then lh.cancel(self.pulse) end end

-- powerflyout.xaml: white DockPanel Width 194; HeadingText #808080 bold 14 margin 8; GaugeParent margin 8; 1 px #808080 rules
-- (margin 8,12,8,0); "Power Plans" with three radio rows (read-only here: /sys/firmware/acpi/platform_profile)
function tile:flyout()
  local s = self.state or self.read()
  local W = 194
  local root = lh.node{ layout = "fill", w = W }
  root:add(lh.node{ kind = "rect", fill = "#ffffff" })
  local col = root:add(lh.node{ layout = "vbox" })
  local function heading(t) return col:add(lh.node{ kind = "text", text = t, font = "Segoe UI,Tahoma Bold 14px", color = "#808080", margin = 8 }) end
  local function line() col:add(lh.node{ h = 13, layout = "fill", margin = {8, 12, 8, 0} }):add(lh.node{ kind = "rect", fill = "#808080", h = 1 }) end
  local function row(k, v) local r = col:add(lh.node{ layout = "dock", margin = {8, 1, 8, 1} })
    r:add(lh.node{ kind = "text", text = k, font = "Segoe UI,Tahoma 12px", color = "#808080", dock = "left", w = 82 })
    r:add(lh.node{ kind = "text", text = v, font = "Segoe UI,Tahoma 12px", color = "#000000", dock = "fill", ellipsis = true }) end
  heading(string.format("Power Status (%d)", s.present and 1 or 0))
  local gp = col:add(lh.node{ h = 40, layout = "fill", margin = {8, 0, 8, 6} })
  local g = build_gauge(gp, 0, 0, W - 16 - 26, 1, "Segoe UI,Tahoma Bold 12px", "#404040"); g.update(s)
  row("Status", s.status .. (s.ac and " (on AC)" or ""))
  row("Charge", (s.capacity or 0) .. "%")
  if s.hours then row(s.status == "Charging" and "Until full" or "Remaining", hm(s.hours)) end
  if s.rate and s.rate > 0 then row("Rate", string.format("%.1f %s", s.rate / 1e6, s.unit == "Wh" and "W" or "A")) end
  if s.now and s.full then row("Capacity", string.format("%.1f / %.1f %s", s.now / 1e6, s.full / 1e6, s.unit)) end
  if s.model then row("Battery", s.model .. (s.tech and (" " .. s.tech) or "")) end
  line()
  heading("Power Plans")
  local prof = rd("/sys/firmware/acpi/platform_profile")
  for _, p in ipairs{ { "low-power", "Longest Battery Life", "Battery power is more important than computer speed." },
                      { "balanced", "Balanced (Recommended)", "Battery power and computer speed are balanced." },
                      { "performance", "Highest Performance", "Computer speed is more important than battery power." } } do
    local r = col:add(lh.node{ layout = "dock", margin = {8, 0, 8, 4} })
    local dot = r:add(lh.node{ dock = "left", w = 16, layout = "canvas" })
    dot:add(lh.node{ kind = "rect", x = 1, y = 3, w = 11, h = 11, radius = 5.5, fill = "#ffffff", stroke = "#808080" })
    if prof == p[1] or (not prof and p[1] == "balanced") then dot:add(lh.node{ kind = "rect", x = 4, y = 6, w = 5, h = 5, radius = 2.5, fill = "#2a5bd7" }) end
    local t = r:add(lh.node{ dock = "fill", layout = "vbox" })
    t:add(lh.node{ kind = "text", text = p[2], font = "Segoe UI,Tahoma 12px", color = "#000000" })
    t:add(lh.node{ kind = "text", text = p[3], font = "Segoe UI,Tahoma 11px", color = "#606060", wrap = true, w = W - 32 })
  end
  col:add(lh.node{ h = 6 })
  self.flyout_refresh = function(st) g.update(st) end
  local _, h = root:desired(W, 2000)
  return root, W, h
end
function tile:flyout_closed() self.flyout_refresh = nil end
return tile
