-- Touchpad: a tray item (traycontrol.lua C.item) that turns the touchpad off and on (XP has none; the notification-area
-- icon of the laptop vendors' tools). liblhde's hardware layer (lh.hw_touchpad: xinput's first slave pointer, floating while
-- off, named *touchpad* or *trackpad*, its "libinput Device Enabled" or "Device Enabled"; the X server's device on every system), read
-- in a child (C.hw) every 2 s, every 10 s while there is none (a USB one plugged in), not again without xinput; a click
-- sets it (lh.hw_touchpad_set). Hidden while no touchpad is found. Tooltip "Touchpad: on (click to turn it off)"; no flyout.
local C = dofile(lh.lua_dir .. "/traycontrol.lua")

local function start(self, update)
  local st = { hidden = true }
  self.st = st
  local gone, reading, ticks = false, false, 0
  local function read()
    if gone or reading then return end
    reading = true
    if not C.hw("hw_touchpad", function(t)
      reading = false
      if gone then return end
      st.hidden, st.enabled = not t.present, t.enabled
      st.error = (t.error and t.error ~= "") and t.error or nil
      update(st)
    end) then reading = false end
  end
  self.read_now = read
  self.run_set = C.queue(read)
  read()
  self.poll = lh.timer(function()
    ticks = ticks + 1
    if st.error == "no xinput" then return end
    if not st.hidden or ticks % 5 == 0 then read() end
  end, 2000, true)
  self.stop_reading = function() gone = true end
end
local function stop(self)
  if self.stop_reading then self.stop_reading() end
  if self.poll then lh.cancel(self.poll); self.poll = nil end
end

return C.item{
  name = "Touchpad",
  hidden_until_read = true,
  start = start, stop = stop,
  icon = function(st) return st.enabled == false and "touchpad-off" or "touchpad-on" end,
  tooltip = function(st) if st.enabled == false then return "Touchpad: off (click to turn it on)" end return "Touchpad: on (click to turn it off)" end,
  wheel = function() end,
  click = function(self)
    local st = self.st; if not st or st.hidden then return end
    st.enabled = not st.enabled; self:update(st)
    self.run_set("set", "hw_touchpad_set", st.enabled)
  end,
}
