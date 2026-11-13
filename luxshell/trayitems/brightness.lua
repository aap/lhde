-- Brightness: the screen backlight, a tray item (traycontrol.lua C.item; XP has none; the volume popup's shape).
-- /sys/class/backlight/<device>/brightness and max_brightness (the device whose `type` is firmware, else platform, else raw,
-- as the kernel ranks them), read every second (the brightness keys change it behind us); set with `brightnessctl -q -d DEV
-- set N` when installed, else written to `brightness` when that is writable, else the tooltip says neither works. Hidden
-- without a backlight (desktops). Tooltip "Brightness: 60%", a click opens the slider, the wheel steps 5 %; never
-- below 1 % (0 turns some panels off). $LHSHELL_BACKLIGHT points it at another directory.
local C = dofile(lh.lua_dir .. "/traycontrol.lua")
local BL = os.getenv("LHSHELL_BACKLIGHT") or "/sys/class/backlight"
local RANK = { firmware = 1, platform = 2, raw = 3 }
local function rd(path) local f = io.open(path); if not f then return nil end local v = f:read("l"); f:close(); return v end
local function device()
  local best, br
  local names = lh.list_dir(BL) or {}
  table.sort(names)
  for _, n in ipairs(names) do
    if tonumber(rd(BL .. "/" .. n .. "/max_brightness") or "") then
      local r = RANK[rd(BL .. "/" .. n .. "/type") or ""] or 4
      if not best or r < br then best, br = n, r end
    end
  end
  return best
end

local function start(self, update)
  local st = {}
  self.st = st
  self.have_ctl = C.have("brightnessctl")
  local function read()
    local dev = st.device and tonumber(rd(BL .. "/" .. st.device .. "/max_brightness") or "") and st.device or device()
    st.device = dev
    if not dev then st.hidden = true; update(st) return end
    local cur, max = tonumber(rd(BL .. "/" .. dev .. "/brightness") or ""), tonumber(rd(BL .. "/" .. dev .. "/max_brightness") or "")
    st.hidden = not (cur and max and max > 0)
    if not st.hidden then st.raw, st.max = cur, max; st.percent = math.floor(cur * 100 / max + 0.5) end
    update(st)
  end
  self.read_now = read
  self.run_set = C.queue(read)
  read()
  self.poll = lh.timer(read, 1000, true)
end
local function stop(self) if self.poll then lh.cancel(self.poll); self.poll = nil end end
local function set_percent(self, p)
  local st = self.st; if not st or st.hidden or not st.max then return end
  p = math.max(1, math.min(100, p))
  local raw = math.max(1, math.floor(p * st.max / 100 + 0.5))
  st.percent = math.floor(raw * 100 / st.max + 0.5); st.raw = raw
  if self.have_ctl then st.error = nil; self:update(st); self.run_set("set", "brightnessctl -q -d " .. C.quote(st.device) .. " set " .. raw) return end
  local f = io.open(BL .. "/" .. st.device .. "/brightness", "w")
  if f then f:write(tostring(raw), "\n"); f:close(); st.error = nil; self.read_now()
  else st.error = "not writable (no brightnessctl, and " .. BL .. "/" .. st.device .. "/brightness is not ours to write)"; self.read_now() end
end

return C.item{
  name = "Brightness",
  hidden_until_read = true,
  start = start, stop = stop,
  icon = function() return "sun" end,
  tooltip = function(st) return "Brightness: " .. (st.percent or 0) .. "%" .. (st.error and (" (" .. st.error .. ")") or "") end,
  wheel = function(self, dir) local st = self.st; if st and st.percent then set_percent(self, st.percent + 5 * dir) end end,
  slider = { title = "Brightness", step = 5, get = function(st) return st.percent end, set = function(self, v) set_percent(self, v) end },
}
