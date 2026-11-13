-- Touchpad: a tray item (traycontrol.lua C.item) that turns the touchpad off and on (XP has none; the notification-area
-- icon of the laptop vendors' tools).
-- `xinput list`: the first slave pointer whose name has TouchPad, Touchpad or Trackpad; its state is `xinput list-props ID`'s
-- "libinput Device Enabled" (the evdev / synaptics drivers': "Device Enabled"), read every 2 s; a click sets it with `xinput
-- set-prop ID 'libinput Device Enabled' 0|1`. Hidden while no touchpad is found (looked for again every 10 s: a USB one
-- plugged in). Tooltip "Touchpad: on (click to turn it off)"; no flyout.
local C = dofile(lh.lua_dir .. "/traycontrol.lua")

local function start(self, update)
  local st = { hidden = true }
  self.st = st
  if not C.have("xinput") then st.error = "no xinput"; update(st) return end
  local gone = false
  local find
  local function props()
    if gone or not st.id then return end
    C.run("xinput list-props " .. st.id, function(lines)
      if gone then return end
      local val, prop
      for _, l in ipairs(lines) do
        local v = l:match("libinput Device Enabled %(%d+%):%s*(%d)")
        if v then val, prop = v, "libinput Device Enabled" break end
        v = l:match("^%s*Device Enabled %(%d+%):%s*(%d)")
        if v then val, prop = v, "Device Enabled" end
      end
      if not val then st.id = nil; st.hidden = true; update(st); find() return end   -- unplugged
      st.enabled, st.prop, st.hidden = val == "1", prop, false
      update(st)
    end)
  end
  find = function()
    if gone then return end
    C.run("xinput list", function(lines)
      if gone then return end
      for _, l in ipairs(lines) do
        local name, id = l:match("^[^%w]*(.-)%s+id=(%d+)%s+%[slave%s+pointer")
        if name and (name:lower():find("touchpad", 1, true) or name:lower():find("trackpad", 1, true)) then st.id, st.name = id, name; break end
      end
      if st.id then props() else st.hidden = true; update(st) end
    end)
  end
  self.read_now = props
  self.run_set = C.queue(props)
  find()
  self.poll = lh.timer(function() if st.id then props() else find() end end, 2000, true)
  self.refind = lh.timer(function() if not st.id then find() end end, 10000, true)
  self.stop_reading = function() gone = true end
end
local function stop(self)
  if self.stop_reading then self.stop_reading() end
  if self.poll then lh.cancel(self.poll) end
  if self.refind then lh.cancel(self.refind) end
end

return C.item{
  name = "Touchpad",
  hidden_until_read = true,
  start = start, stop = stop,
  icon = function(st) return st.enabled == false and "touchpad-off" or "touchpad-on" end,
  tooltip = function(st) if st.enabled == false then return "Touchpad: off (click to turn it on)" end return "Touchpad: on (click to turn it off)" end,
  wheel = function() end,
  click = function(self)
    local st = self.st; if not st or not st.id then return end
    st.enabled = not st.enabled; self:update(st)
    self.run_set("set", "xinput set-prop " .. st.id .. " " .. C.quote(st.prop or "libinput Device Enabled") .. " " .. (st.enabled and "1" or "0"))
  end,
}
