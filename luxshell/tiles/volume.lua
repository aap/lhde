-- Volume tile: XP's tray speaker (stobject / sndvol32) as a sidebar tile and a taskbar icon, on the default sink.
-- Read and set through the first that answers of wpctl (PipeWire), pactl (PulseAudio, pipewire-pulse) and amixer (ALSA,
-- Master): `wpctl get-volume @DEFAULT_AUDIO_SINK@` ("Volume: 0.40 [MUTED]"), `pactl get-sink-volume / get-sink-mute
-- @DEFAULT_SINK@`, `amixer get Master` ("[40%] ... [on]"). Changes by other programs: `pactl subscribe` (sink and server events)
-- when pactl is there, else a read every second. Tooltip "Volume: 40%" (XP's), "Volume: 40% (muted)"; a click opens XP's volume
-- popup (traycontrol.lua: "Volume", the trackbar, "Mute"); the wheel steps 2 %, a middle click toggles mute.
-- $LHSHELL_VOLUME = wpctl | pactl | amixer forces one backend.
local C = dofile(lh.lua_dir .. "/traycontrol.lua")
local SINK_W, SINK_P = "@DEFAULT_AUDIO_SINK@", "@DEFAULT_SINK@"
local backends = {
  wpctl = {
    read = "wpctl get-volume " .. SINK_W,
    parse = function(lines) local t = table.concat(lines, "\n"); local v = t:match("Volume:%s*([%d%.]+)")
      if v then return math.floor(tonumber(v) * 100 + 0.5), t:find("%[MUTED%]") ~= nil end end,
    set = function(v) return string.format("wpctl set-volume %s %.2f", SINK_W, v / 100) end,
    mute = function(on) return "wpctl set-mute " .. SINK_W .. " " .. (on == nil and "toggle" or on and "1" or "0") end,
  },
  pactl = {
    read = "pactl get-sink-volume " .. SINK_P .. "; pactl get-sink-mute " .. SINK_P,
    parse = function(lines) local t = table.concat(lines, "\n"); local v = t:match("Volume:.-(%d+)%%"); local m = t:match("Mute:%s*(%a+)")
      if v and m then return tonumber(v), m == "yes" end end,
    set = function(v) return string.format("pactl set-sink-volume %s %d%%", SINK_P, v) end,
    mute = function(on) return "pactl set-sink-mute " .. SINK_P .. " " .. (on == nil and "toggle" or on and "1" or "0") end,
  },
  amixer = {
    read = "amixer get Master",
    parse = function(lines) local t = table.concat(lines, "\n"); local v = t:match("%[(%d+)%%%]")
      if v then return tonumber(v), t:find("%[off%]") ~= nil end end,
    set = function(v) return string.format("amixer -q set Master %d%%", v) end,
    mute = function(on) return "amixer -q set Master " .. (on == nil and "toggle" or on and "mute" or "unmute") end,
  },
}
local ORDER = { "wpctl", "pactl", "amixer" }

local function start(self, update)
  local st = { volume = 0, muted = false }
  local forced = os.getenv("LHSHELL_VOLUME")
  local cands = {}
  for _, n in ipairs(ORDER) do if (not forced or forced == "" or forced == n) and C.have(n) then table.insert(cands, n) end end
  if #cands == 0 then
    st.error = (forced and forced ~= "") and (forced .. " not found") or "no wpctl, pactl or amixer found"
    update(st); return
  end
  local k, reading, again, gone = 1, false, false, false
  self.backend = cands[1]
  local function read()
    if gone then return end
    if reading then again = true return end
    reading = true
    local b = backends[cands[k]]
    C.run(b.read, function(lines)
      reading = false
      if gone then return end
      local v, m = b.parse(lines)
      if v then st.volume, st.muted, st.error = v, m, nil; st.backend = cands[k]; self.backend = cands[k]
      elseif k < #cands then k = k + 1; self.backend = cands[k]; again = true      -- this one does not answer: the next tool
      else st.error = cands[k] .. ": " .. ((lines[1] and lines[1] ~= "") and lines[1] or "no answer") end
      update(st)
      if again then again = false; read() end
    end)
  end
  self.read_now = read
  -- set: the commands one at a time, then a read (its answer replaces the guess shown meanwhile)
  self.run_set = C.queue(read)
  read()
  local debounce
  local function changed() if debounce then return end debounce = lh.timer(function() debounce = nil; read() end, 50) end
  local function poll() self.poll = lh.timer(read, 1000, true) end
  if C.have("pactl") and (not forced or forced == "" or forced == "pactl") then
    self.sub = lh.pipe("exec pactl subscribe 2>/dev/null", function(line)
      if line:find(" sink ", 1, true) or line:find("server", 1, true) then changed() end
    end, function() self.sub = nil; if not gone then poll() end end)
    if not self.sub then poll() end
  else poll() end
  self.st = st
  self.stop_reading = function() gone = true; if debounce then lh.cancel(debounce) end end
end
local function stop(self)
  if self.stop_reading then self.stop_reading() end
  if self.sub then lh.pipe_close(self.sub); self.sub = nil end
  if self.poll then lh.cancel(self.poll); self.poll = nil end
end
local function set_volume(self, v)
  local st = self.st; if not st or st.error or not self.backend then return end
  v = math.max(0, math.min(100, math.floor(v + 0.5)))
  st.volume = v; self:update(st)
  self.run_set("volume", backends[self.backend].set(v))
end
local function set_mute(self, on)
  local st = self.st; if not st or st.error or not self.backend then return end
  st.muted = on; self:update(st)
  self.run_set("mute", backends[self.backend].mute(on))
end

return C.tile{
  name = "Volume",
  start = start, stop = stop,
  -- XP's tray speaker has no level steps; the waves (0 %: none, under 50 %: one, else two) are ours
  icon = function(st) if st.muted then return "speaker-muted" end local v = st.volume or 0; return "speaker-" .. (v <= 0 and 0 or v < 50 and 1 or 2) end,
  label = function(st) if st.error then return "Volume: ?" end return (st.muted and "Muted: " or "Volume: ") .. (st.volume or 0) .. "%" end,
  tooltip = function(st) if st.error then return "Volume: " .. st.error end return "Volume: " .. (st.volume or 0) .. "%" .. (st.muted and " (muted)" or "") end,
  wheel = function(self, dir) local st = self.st; if not st or st.error then return end
    local v = st.volume or 0; if dir > 0 and v >= 100 then return end
    set_volume(self, v + 2 * dir) end,
  middle = function(self) local st = self.st; if st and not st.error then set_mute(self, not st.muted) end end,
  slider = { title = "Volume", step = 2, get = function(st) return st.volume end,
             set = function(self, v) set_volume(self, v) end,
             mute = { text = "Mute", get = function(st) return st.muted end, set = function(self, on) set_mute(self, on) end } },
}
