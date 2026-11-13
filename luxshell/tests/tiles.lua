-- The tray tiles (tiles/volume.lua, brightness.lua) against a stand-in `lh`: nodes are tables, timers and
-- lh.pipe run when the test says so (lh.pipe through io.popen, so the stub commands of tiles.sh on PATH answer), and the
-- commands the tiles run are logged by those stubs. Usage: lua5.4 tiles.lua LUXSHELL_DIR TEST (tiles.sh sets the scene).
local dir, test = arg[1], arg[2]
local fails, n = 0, 0
local function check(what, ok, got) n = n + 1; if ok then print("ok    " .. what) else fails = fails + 1; print("FAIL  " .. what .. (got ~= nil and ("  (got " .. tostring(got) .. ")") or "")) end end

------------------------------------------------------------------ the stand-in lh
local Node = {}; Node.__index = Node
function Node:add(c) self.kids = self.kids or {}; table.insert(self.kids, c); c.parent = self; return c end
function Node:set(t) for k, v in pairs(t) do self[k] = v end end
function Node:clear() self.kids = {} end
function Node:remove() end
function Node:bounds() return 0, 0, 133, 26 end
function Node:desired() return self.w or 72, 160 end
local timers, pipes, handlers, tid = {}, {}, {}, 0
lh = { lua_dir = dir }
function lh.node(t) return setmetatable(t or {}, Node) end
function lh.timer(f, ms, rep) tid = tid + 1; timers[tid] = { f = f, rep = rep }; return tid end
function lh.cancel(id) timers[id] = nil end
function lh.now() return os.time() * 1000 end
function lh.pipe(cmd, on_line, on_done) tid = tid + 1; pipes[tid] = { cmd = cmd, on_line = on_line, on_done = on_done }; return tid end
function lh.pipe_close(id) pipes[id] = nil end
function lh.on(ev, f) handlers[ev] = handlers[ev] or {}; table.insert(handlers[ev], f) end
function lh.off(ev, f) for i, g in ipairs(handlers[ev] or {}) do if g == f then table.remove(handlers[ev], i) end end end
function lh._emit(ev, ...) for _, f in ipairs(handlers[ev] or {}) do f(...) end end
function lh.tooltip(node, text) node.tooltip = text end
function lh.list_dir(d) local f = io.popen("cd " .. d .. " 2>/dev/null && for f in *; do [ -e \"$f\" ] && echo \"$f\"; done"); local t = {}; for l in f:lines() do table.insert(t, l) end f:close(); return t end
function lh.screen() return 1280, 1024 end
function lh.pointer() return 1200, 1000 end
lh.flyout = { surface = { damage = function() end } }
function lh.open_flyout(t) lh.flyout_tile = t; t.fly = { t:flyout() } end
function lh.close_flyout() local t = lh.flyout_tile; lh.flyout_tile = nil; if t and t.flyout_closed then t:flyout_closed() end end
function lh.toggle_flyout(t) if lh.flyout_tile == t then lh.close_flyout() else lh.open_flyout(t) end end
-- run what is pending: the pipes (each to its end, skipping `pactl subscribe`, which never ends) and the timers once
local function settle()
  for _ = 1, 20 do
    local any = false
    local ids = {}; for id in pairs(pipes) do table.insert(ids, id) end table.sort(ids)
    for _, id in ipairs(ids) do local p = pipes[id]
      if p and not p.cmd:find("subscribe") then
        pipes[id] = nil; any = true
        local f = io.popen(p.cmd); for l in f:lines() do p.on_line(l) end f:close()
        if p.on_done then p.on_done() end
      end
    end
    if not any then return end
  end
end
local function tick() local ids = {}; for id in pairs(timers) do table.insert(ids, id) end table.sort(ids)
  for _, id in ipairs(ids) do local t = timers[id]; if t then if not t.rep then timers[id] = nil end t.f() end end settle() end
local function log() local f = io.open(os.getenv("TR_LOG")); if not f then return "" end local s = f:read("a"); f:close(); return s end
local function clear_log() local f = io.open(os.getenv("TR_LOG"), "w"); f:close() end
local function write(path, s) local f = assert(io.open(path, "w")); f:write(s); f:close() end
local function read(path) local f = io.open(path); if not f then return nil end local s = f:read("a"); f:close(); return s end
local function find_text(node, text) if node.kind == "text" and node.text == text then return node end
  for _, k in ipairs(node.kids or {}) do local r = find_text(k, text); if r then return r end end end

-- a tile in the sidebar (its foreground, chrome node and host as runtime.lua gives them) or in the taskbar
local function sidebar_tile(name)
  local t = dofile(dir .. "/tiles/" .. name .. ".lua")
  t.T = {}; t.R = function(id) return id end; t.sb = { surface = { damage = function() end, x = 1130 }, tile_rect = function() return 1130, 500 end }
  t.foreground = lh.node{ layout = "fill" }; t.node = lh.node{}
  if t.flyout then t.foreground.on_click = function(_, b) if b == 1 then lh.toggle_flyout(t) end end end
  t:initialize(); settle(); return t
end
local function taskbar_tile(name)
  local t = dofile(dir .. "/tiles/" .. name .. ".lua")
  t.T = { taskbar = { height = 30 } }; t.R = function(id) return id end; t.tb = { h = 30, surface = { damage = function() end } }
  local host = lh.node{}; t:taskbar(host); settle(); return t, host.kids[1]
end
local function tip(node) return type(node.tooltip) == "function" and node.tooltip() or node.tooltip end

------------------------------------------------------------------ the tests
if test == "volume-wpctl" then
  local t = sidebar_tile("volume")
  local fg = t.foreground
  check("volume: wpctl answers, the tooltip is XP's", tip(fg) == "Volume: 40%", tip(fg))
  check("volume: the label", t.label_node.text == "Volume: 40%", t.label_node.text)
  check("volume: under 50 % one wave", t.icon_node.name == "speaker-1", t.icon_node.name)
  check("volume: pactl subscribe follows other programs", (function() for _, p in pairs(pipes) do if p.cmd:find("pactl subscribe") then return true end end end)())
  clear_log(); fg.on_click(fg, 4); settle()
  check("volume: wheel up sets 42 %", log():find("wpctl set%-volume @DEFAULT_AUDIO_SINK@ 0.42") ~= nil, log())
  check("volume: and reads it back", tip(fg) == "Volume: 42%", tip(fg))
  clear_log(); fg.on_click(fg, 5); fg.on_click(fg, 5); settle()
  check("volume: wheel down twice: 38 %", log():find("set%-volume @DEFAULT_AUDIO_SINK@ 0.38") ~= nil and tip(fg) == "Volume: 38%", log())
  clear_log(); fg.on_click(fg, 2); settle()
  check("volume: middle click toggles mute", log():find("wpctl set%-mute @DEFAULT_AUDIO_SINK@ 1") ~= nil, log())
  check("volume: muted icon and tooltip", t.icon_node.name == "speaker-muted" and tip(fg) == "Volume: 38% (muted)", t.icon_node.name .. " " .. tip(fg))
  -- another program: the state file changes and pactl subscribe reports a sink event
  write(os.getenv("TR_STATE"), "0.75\n"); write(os.getenv("TR_STATE") .. ".mute", "0\n")
  for _, p in pairs(pipes) do if p.cmd:find("subscribe") then p.on_line("Event 'change' on sink #45") end end
  tick()
  check("volume: an outside change shows (sink event)", tip(fg) == "Volume: 75%" and t.icon_node.name == "speaker-2", tip(fg))
  -- the popup
  fg.on_click(fg, 1)
  check("volume: a click opens the popup", lh.flyout_tile == t and t.fly[1] ~= nil)
  local root = t.fly[1]
  check("volume: popup caption Volume", find_text(root, "Volume") ~= nil)
  local mute = find_text(root, "Mute")
  check("volume: popup Mute box", mute ~= nil)
  clear_log(); mute.parent.on_click(mute.parent, 1); settle()
  check("volume: the Mute box sets mute", log():find("wpctl set%-mute @DEFAULT_AUDIO_SINK@ 1") ~= nil, log())
  local track; for _, k in ipairs(root.kids[2].kids) do if k.on_drag then track = k end end
  clear_log(); track.on_click(track, 4); settle()
  check("volume: the wheel on the trackbar", log():find("set%-volume @DEFAULT_AUDIO_SINK@ 0.77") ~= nil, log())
  lh.close_flyout()
  t:dispose()
  check("volume: dispose ends pactl subscribe", next(pipes) == nil)
  local tb, view = taskbar_tile("volume")
  check("volume: taskbar view, icon and tooltip (muted by the box)", tb.tb_icon.name == "speaker-muted" and tip(view) == "Volume: 77% (muted)", tip(view))
  tb:dispose()
elseif test == "volume-pactl" then
  -- wpctl present but not answering (PipeWire without a default node): pactl takes over
  local t = sidebar_tile("volume")
  check("volume: wpctl fails, pactl answers", tip(t.foreground) == "Volume: 55%" and t.backend == "pactl", tip(t.foreground))
  clear_log(); t.foreground.on_click(t.foreground, 4); settle()
  check("volume: pactl set-sink-volume 57%", log():find("pactl set%-sink%-volume @DEFAULT_SINK@ 57%%") ~= nil, log())
  t:dispose()
elseif test == "volume-amixer" then
  local t = sidebar_tile("volume")
  check("volume: amixer only, polled", tip(t.foreground) == "Volume: 30% (muted)" and next(timers) ~= nil, tip(t.foreground))
  clear_log(); t.foreground.on_click(t.foreground, 2); settle()
  check("volume: amixer toggles Master", log():find("amixer %-q set Master unmute") ~= nil, log())
  t:dispose()
elseif test == "volume-none" then
  local t = sidebar_tile("volume")
  check("volume: no tool, the tooltip says so", tip(t.foreground) == "Volume: no wpctl, pactl or amixer found", tip(t.foreground))
  t:dispose()
elseif test == "brightness" then
  local bl = os.getenv("LHSHELL_BACKLIGHT") .. "/acpi_video0/"
  local t = sidebar_tile("brightness")
  local fg = t.foreground
  check("brightness: 600 of 1000 is 60 %", tip(fg) == "Brightness: 60%" and t.label_node.text == "Brightness: 60%", tip(fg))
  check("brightness: shown with a device", not t.hidden)
  check("brightness: the sun", t.icon_node.name == "sun", t.icon_node.name)
  clear_log(); fg.on_click(fg, 4); settle()
  if os.getenv("TR_BRIGHTNESSCTL") then
    check("brightness: wheel up 5 % through brightnessctl", log():find("brightnessctl %-q %-d acpi_video0 set 650") ~= nil, log())
  else
    check("brightness: wheel up 5 % written to sysfs", read(bl .. "brightness"):match("%d+") == "650", read(bl .. "brightness"))
  end
  write(bl .. "brightness", "200\n"); tick()
  check("brightness: an outside change shows", tip(fg) == "Brightness: 20%", tip(fg))
  fg.on_click(fg, 1)
  check("brightness: popup with its caption and no Mute", find_text(t.fly[1], "Brightness") ~= nil and find_text(t.fly[1], "Mute") == nil)
  lh.close_flyout(); t:dispose()
elseif test == "brightness-readonly" then
  local t = sidebar_tile("brightness")
  clear_log(); t.foreground.on_click(t.foreground, 4); settle()
  check("brightness: not writable, the tooltip says so", tip(t.foreground):find("not writable") ~= nil, tip(t.foreground))
  t:dispose()
elseif test == "brightness-none" then
  local t = sidebar_tile("brightness")
  check("brightness: no backlight, hidden", t.hidden == true and t.node.visible == false)
  local tb, view = taskbar_tile("brightness")
  check("brightness: no backlight, no taskbar icon", view.visible == false)
  t:dispose(); tb:dispose()
end
print(string.format("%d checks, %d failed", n, fails))
os.exit(fails == 0 and 0 or 1)
