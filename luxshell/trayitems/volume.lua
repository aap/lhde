-- Volume: XP's tray speaker (stobject / sndvol32), a tray item (traycontrol.lua C.item), on the default output, through
-- liblhde's hardware layer (lh.hw_volume: wpctl, pactl or amixer on Linux, the OSS mixer on FreeBSD; docs/SHELL-DESIGN.md
-- 8.6), read and set in a child (C.hw: the layer may run those programs and wait for them). Changes by other programs: the
-- layer's event source (lh.hw_watch "volume": `pactl subscribe`) where there is one, else a read every second. Tooltip
-- "Volume: 40%" (XP's), "Volume: 40% (muted)", or the layer's reason; a click opens XP's volume popup (traycontrol.lua:
-- "Volume", the trackbar, "Mute"); the wheel steps 2 %, a middle click toggles mute.
local C = dofile(lh.lua_dir .. "/traycontrol.lua")

local function start(self, update)
  local st = { volume = 0, muted = false }
  local reading, again, gone = false, false, false
  local function read()
    if gone then return end
    if reading then again = true return end
    reading = true
    if not C.hw("hw_volume", function(v)
      reading = false
      if gone then return end
      if v.present then st.volume, st.muted, st.error, st.backend = v.percent, v.muted, nil, v.backend; self.backend = v.backend
      else st.error = (v.error and v.error ~= "") and v.error or "no answer"; self.backend = nil end
      update(st)
      if again then again = false; read() end
    end) then reading = false end
  end
  self.read_now = read
  -- set: the calls one at a time, then a read (its answer replaces the guess shown meanwhile)
  self.run_set = C.queue(read)
  read()
  local debounce
  local function changed() if debounce then return end debounce = lh.timer(function() debounce = nil; read() end, 50) end
  local function poll() self.poll = lh.timer(read, 1000, true) end
  self.watch = lh.hw_watch("volume", function(ended)
    if ended then self.watch = nil; if not gone then poll() end else changed() end
  end)
  if not self.watch then poll() end
  self.st = st
  self.stop_reading = function() gone = true; if debounce then lh.cancel(debounce) end end
end
local function stop(self)
  if self.stop_reading then self.stop_reading() end
  if self.watch then lh.hw_unwatch(self.watch); self.watch = nil end
  if self.poll then lh.cancel(self.poll); self.poll = nil end
end
local function set_volume(self, v)
  local st = self.st; if not st or st.error or not self.backend then return end
  v = math.max(0, math.min(100, math.floor(v + 0.5)))
  st.volume = v; self:update(st)
  self.run_set("volume", "hw_volume_set", v)
end
local function set_mute(self, on)
  local st = self.st; if not st or st.error or not self.backend then return end
  st.muted = on; self:update(st)
  self.run_set("mute", "hw_volume_mute", on)
end

return C.item{
  name = "Volume",
  start = start, stop = stop,
  -- XP's tray speaker has no level steps; the waves (0 %: none, under 50 %: one, else two) are ours
  icon = function(st) if st.muted then return "speaker-muted" end local v = st.volume or 0; return "speaker-" .. (v <= 0 and 0 or v < 50 and 1 or 2) end,
  tooltip = function(st) if st.error then return "Volume: " .. st.error end return "Volume: " .. (st.volume or 0) .. "%" .. (st.muted and " (muted)" or "") end,
  wheel = function(self, dir) local st = self.st; if not st or st.error then return end
    local v = st.volume or 0; if dir > 0 and v >= 100 then return end
    set_volume(self, v + 2 * dir) end,
  middle = function(self) local st = self.st; if st and not st.error then set_mute(self, not st.muted) end end,
  slider = { title = "Volume", step = 2, get = function(st) return st.volume end,
             set = function(self, v) set_volume(self, v) end,
             mute = { text = "Mute", get = function(st) return st.muted end, set = function(self, on) set_mute(self, on) end } },
}
