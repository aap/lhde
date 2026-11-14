-- Brightness: the screen backlight, a tray item (traycontrol.lua C.item; XP has none; the volume popup's shape).
-- liblhde's hardware layer (lh.hw_backlight: /sys/class/backlight on Linux, /dev/backlight on FreeBSD; docs/SHELL-DESIGN.md
-- 8.6), read every second (the brightness keys change it behind us, without an event); a set is lh.hw_backlight_set (a
-- write or an ioctl; brightnessctl or backlight(8) when that cannot be done). Hidden without a backlight (desktops). Tooltip
-- "Brightness: 60%", with the layer's reason when a set cannot work; a click opens the slider, the wheel steps 5 %; never
-- below 1 % (0 turns some panels off).
local C = dofile(lh.lua_dir .. "/traycontrol.lua")

local function start(self, update)
  local st = {}
  self.st = st
  local function read()
    local b = lh.hw_backlight()
    st.hidden = not b.present
    if b.present then st.raw, st.max, st.percent, st.device = b.raw, b.max, b.percent, b.device; st.error = b.error ~= "" and b.error or nil end
    update(st)
  end
  self.read_now = read
  read()
  self.poll = lh.timer(read, 1000, true)
end
local function stop(self) if self.poll then lh.cancel(self.poll); self.poll = nil end end
local function set_percent(self, p)
  local st = self.st; if not st or st.hidden or not st.max then return end
  p = math.max(1, math.min(100, math.floor(p + 0.5)))
  local ok, err = lh.hw_backlight_set(p)
  st.set_error = not ok and err or nil
  self.read_now()
end

return C.item{
  name = "Brightness",
  hidden_until_read = true,
  start = start, stop = stop,
  icon = function() return "sun" end,
  tooltip = function(st) local e = st.set_error or st.error; return "Brightness: " .. (st.percent or 0) .. "%" .. (e and (" (" .. e .. ")") or "") end,
  wheel = function(self, dir) local st = self.st; if st and st.percent then set_percent(self, st.percent + 5 * dir) end end,
  slider = { title = "Brightness", step = 5, get = function(st) return st.percent end, set = function(self, v) set_percent(self, v) end },
}
