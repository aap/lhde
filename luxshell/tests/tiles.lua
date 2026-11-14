-- The tray items (trayitems/volume.lua, brightness.lua, touchpad.lua), the Classic Tray tile hosting them
-- (tiles/notifications.lua) and the power tile against a stand-in `lh`: nodes are tables, timers and lh.pipe /
-- lh.fork_call run when the test says so; lh.hw_* are liblhde's hardware layer itself, through build/bin/lhde-hw ($TR_LHDE_HW:
-- -k WHAT for a read, its sets), over tiles.sh's fake tree (LHDE_HW_FAKE) and stub commands on PATH, which log what they are
-- asked. Usage: lua5.4 tiles.lua LUXSHELL_DIR TEST (tiles.sh sets the scene).
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
function lh.fork_call(fn, on_line, on_done, ...) tid = tid + 1; pipes[tid] = { call = table.pack(fn, ...), on_line = on_line, on_done = on_done, cmd = "" }; return tid end
-- the hardware layer: lhde-hw's records and sets
local HW = os.getenv("TR_LHDE_HW")
local function hwk(what)
  local f = io.popen(HW .. " -k " .. what .. " 2>/dev/null"); local t = {}
  for l in f:lines() do local k, v = l:match("^([^=]+)=(.*)$")
    if k then if v == "true" then v = true elseif v == "false" then v = false elseif tonumber(v) then v = tonumber(v) end t[k] = v end end
  f:close(); return t
end
local function hwset(args)
  local f = io.popen(HW .. " " .. args .. " 2>&1 >/dev/null"); local out = f:read("a") or ""
  if f:close() then return true end
  return nil, out:match("^lhde%-hw: %a+: (.-)\n?$") or out
end
function lh.hw_battery() return hwk("battery") end
function lh.hw_backlight() return hwk("backlight") end
function lh.hw_volume() return hwk("volume") end
function lh.hw_touchpad() return hwk("touchpad") end
function lh.hw_backlight_set(p) return hwset("backlight " .. math.floor(p)) end
function lh.hw_volume_set(p) return hwset("volume " .. math.floor(p)) end
function lh.hw_volume_mute(on) return hwset("volume " .. (on == nil and "toggle" or on and "mute" or "unmute")) end
function lh.hw_touchpad_set(on) return hwset("touchpad " .. (on and "on" or "off")) end
-- event sources as a Linux system with pactl has them (the layer has none under LHDE_HW_FAKE): volume's when pactl is on
-- PATH and not ruled out, never the battery's
local watches, wid = {}, 0
local function on_path(p) local f = io.popen("command -v " .. p); local o = f:read("l"); f:close(); return o ~= nil end
function lh.hw_watch(what, fn)
  if what ~= "volume" or os.getenv("LHDE_HW_SYSTEM") == "freebsd" or os.getenv("LHDE_HW_VOLUME") == "amixer" or not on_path("pactl") then return nil end
  wid = wid + 1; watches[wid] = { what = what, fn = fn }; return wid
end
function lh.hw_unwatch(id) local had = watches[id] ~= nil; watches[id] = nil; return had end
-- lh.fork_call's lines: what luxshell's child writes
local function fc_lines(r)
  local out = {}
  local function one(k, v)
    local ty = type(v) == "string" and "s" or type(v) == "number" and "n" or type(v) == "boolean" and "b"
    if ty then table.insert(out, k .. "\t" .. ty .. "\t" .. tostring(v)) end
  end
  for i = 1, r.n do if type(r[i]) == "table" then for k, v in pairs(r[i]) do if type(k) == "string" then one(k, v) end end else one(tostring(i), r[i]) end end
  return out
end
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
      if p and p.call then
        pipes[id] = nil; any = true
        for _, l in ipairs(fc_lines(table.pack(p.call[1](table.unpack(p.call, 2, p.call.n))))) do p.on_line(l) end
        if p.on_done then p.on_done() end
      elseif p and not p.cmd:find("subscribe") then
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

-- an item with one view in the sidebar's Classic Tray tile (t.foreground, t.icon_node, t.node: that view) or in the taskbar
local SB = { surface = { damage = function() end, x = 1130 }, tile_rect = function() return 1130, 500 end }
local function sidebar_tile(name)
  local t = dofile(dir .. "/trayitems/" .. name .. ".lua").new{ T = {}, sb = SB, tile = {} }
  local view = t:view(lh.node{ layout = "canvas" }, { x = 61, y = 6 }, "sidebar")
  t.foreground, t.icon_node, t.node = view, view, view
  t:start_once(); settle(); return t
end
local function taskbar_tile(name)
  local t = dofile(dir .. "/trayitems/" .. name .. ".lua").new{ T = { taskbar = { height = 30 } }, sb = SB, tb = { h = 30, surface = { damage = function() end } } }
  local view = t:view(lh.node{}, { x = 2, y = 7 }, "taskbar")
  t.tb_icon = view
  t:start_once(); settle(); return t, view
end
local function tip(node) return type(node.tooltip) == "function" and node.tooltip() or node.tooltip end

------------------------------------------------------------------ the tests
if test == "volume-wpctl" then
  local t = sidebar_tile("volume")
  local fg = t.foreground
  check("volume: wpctl answers, the tooltip is XP's", tip(fg) == "Volume: 40%", tip(fg))
  check("volume: under 50 % one wave", t.icon_node.name == "speaker-1", t.icon_node.name)
  check("volume: the layer's event source follows other programs", (function() for _, w in pairs(watches) do if w.what == "volume" then return true end end end)())
  clear_log(); fg.on_click(fg, 4); settle()
  check("volume: wheel up sets 42 %", log():find("wpctl set%-volume @DEFAULT_AUDIO_SINK@ 0.42") ~= nil, log())
  check("volume: and reads it back", tip(fg) == "Volume: 42%", tip(fg))
  clear_log(); fg.on_click(fg, 5); fg.on_click(fg, 5); settle()
  check("volume: wheel down twice: 38 %", log():find("set%-volume @DEFAULT_AUDIO_SINK@ 0.38") ~= nil and tip(fg) == "Volume: 38%", log())
  clear_log(); fg.on_click(fg, 2); settle()
  check("volume: middle click toggles mute", log():find("wpctl set%-mute @DEFAULT_AUDIO_SINK@ 1") ~= nil, log())
  check("volume: muted icon and tooltip", t.icon_node.name == "speaker-muted" and tip(fg) == "Volume: 38% (muted)", t.icon_node.name .. " " .. tip(fg))
  -- another program: the state file changes and the layer's event source reports it
  write(os.getenv("TR_STATE"), "0.75\n"); write(os.getenv("TR_STATE") .. ".mute", "0\n")
  for _, w in pairs(watches) do w.fn() end
  tick()
  check("volume: an outside change shows (an event)", tip(fg) == "Volume: 75%" and t.icon_node.name == "speaker-2", tip(fg))
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
  check("volume: dispose ends its watch and calls", next(pipes) == nil and next(watches) == nil)
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
elseif test == "volume-oss" then
  -- FreeBSD's back end over a recorded mixer (LHDE_HW_SYSTEM=freebsd): no event source, read every second
  local mixer = os.getenv("LHDE_HW_FAKE") .. "/dev/mixer0"
  local t = sidebar_tile("volume")
  local fg = t.foreground
  check("volume-oss: the OSS mixer's level", tip(fg) == "Volume: 66%" and t.backend == "oss", tip(fg))
  check("volume-oss: polled, no event source", next(watches) == nil and next(timers) ~= nil)
  fg.on_click(fg, 4); settle()
  check("volume-oss: wheel up writes 68:68", read(mixer):find("vol 68:68", 1, true) ~= nil and tip(fg) == "Volume: 68%", read(mixer))
  fg.on_click(fg, 2); settle()
  check("volume-oss: middle click sets the mute mask", read(mixer):find("mute 1", 1, true) ~= nil and tip(fg) == "Volume: 68% (muted)", tip(fg))
  write(mixer, "vol 20:20\nmute 0\n"); tick()
  check("volume-oss: another mixer's change shows within a second", tip(fg) == "Volume: 20%", tip(fg))
  t:dispose()
elseif test == "volume-none" then
  local t = sidebar_tile("volume")
  check("volume: no tool, the tooltip says so", tip(t.foreground) == "Volume: no wpctl, pactl or amixer found", tip(t.foreground))
  t:dispose()
elseif test == "brightness" then
  local bl = os.getenv("LHDE_HW_FAKE") .. "/sys/class/backlight/acpi_video0/"
  local t = sidebar_tile("brightness")
  local fg = t.foreground
  check("brightness: 600 of 1000 is 60 %", tip(fg) == "Brightness: 60%", tip(fg))
  check("brightness: shown with a device", not t.hidden)
  check("brightness: the sun", t.icon_node.name == "sun", t.icon_node.name)
  clear_log(); fg.on_click(fg, 4); settle()
  check("brightness: wheel up 5 % written to sysfs", read(bl .. "brightness"):match("%d+") == "650", read(bl .. "brightness"))
  write(bl .. "brightness", "200\n"); tick()
  check("brightness: an outside change shows", tip(fg) == "Brightness: 20%", tip(fg))
  fg.on_click(fg, 1)
  check("brightness: popup with its caption and no Mute", find_text(t.fly[1], "Brightness") ~= nil and find_text(t.fly[1], "Mute") == nil)
  lh.close_flyout(); t:dispose()
elseif test == "brightness-readonly" then
  local t = sidebar_tile("brightness")
  clear_log(); t.foreground.on_click(t.foreground, 4); settle()
  if os.getenv("TR_BRIGHTNESSCTL") then
    check("brightness: not writable: wheel up 5 % through brightnessctl", log():find("brightnessctl %-q %-d acpi_video0 set 650") ~= nil, log())
  else
    check("brightness: not writable, the tooltip says so", tip(t.foreground):find("not writable") ~= nil, tip(t.foreground))
  end
  t:dispose()
elseif test == "brightness-none" then
  local t = sidebar_tile("brightness")
  check("brightness: no backlight, hidden", t.hidden == true and t.node.visible == false)
  local tb, view = taskbar_tile("brightness")
  check("brightness: no backlight, no taskbar icon", view.visible == false)
  t:dispose(); tb:dispose()
elseif test == "touchpad" then
  local t = sidebar_tile("touchpad")
  local fg = t.foreground
  check("touchpad: found by xinput, on", tip(fg) == "Touchpad: on (click to turn it off)" and t.icon_node.name == "touchpad-on", tip(fg))
  clear_log(); fg.on_click(fg, 1); settle()
  check("touchpad: a click turns it off", log():find("xinput set%-prop 12 libinput Device Enabled 0") ~= nil, log())
  check("touchpad: off icon and tooltip", t.icon_node.name == "touchpad-off" and tip(fg) == "Touchpad: off (click to turn it on)", tip(fg))
  write(os.getenv("TR_STATE"), "1\n"); tick()
  check("touchpad: an outside change shows", t.state.enabled == true)
  t:dispose()
elseif test == "touchpad-none" then
  local t = sidebar_tile("touchpad")
  check("touchpad: none, hidden", t.hidden == true and t.node.visible == false)
  t:dispose()
elseif test == "power" or test == "power-freebsd" then
  local tile = dofile(dir .. "/tiles/power.lua")
  tile.T = {}; tile.foreground = lh.node{ layout = "fill" }
  tile:initialize()
  local function label() return tile.g.text.text end
  if test == "power" then
    check("power: two batteries discharging, 30 of 44.19 Wh at 7.5 W", label() == "4h00 left: 68%", label())
    check("power: no pulse on battery", tile.pulse == nil)
    local fly = tile:flyout()
    check("power: the flyout counts both", find_text(fly, "Power Status (2)") ~= nil)
    check("power: the flyout's rate and capacity rows", find_text(fly, "7.5 W") ~= nil and find_text(fly, "30.0 / 44.2 Wh") ~= nil)
    tile:flyout_closed()
    write(os.getenv("LHDE_HW_FAKE") .. "/sys/class/power_supply/BAT0/status", "Charging\n")
    write(os.getenv("LHDE_HW_FAKE") .. "/sys/class/power_supply/AC/online", "1\n")
    tick()
    check("power: charging on AC, read again on the timer", label() == "Charging: 68%" and tile.pulse ~= nil, label())
  else
    check("power-freebsd: soma on AC, not charging", label() == "On AC: 99%", label())
    write(os.getenv("LHDE_HW_FAKE") .. "/sysctl/hw.acpi.battery.state", "1\n")
    write(os.getenv("LHDE_HW_FAKE") .. "/sysctl/hw.acpi.battery.time", "263\n")
    write(os.getenv("LHDE_HW_FAKE") .. "/sysctl/hw.acpi.acline", "0\n")
    tick()
    check("power-freebsd: discharging, the kernel's minutes", label() == "4h23 left: 99%", label())
  end
  tile:dispose()
elseif test == "host" then
  -- the Classic Tray tile with its items after K docked XEmbed icons (5 slots a row at the stand-in's 133 px), no backlight
  local K, foreign = tonumber(os.getenv("TR_DOCKED") or "2"), false
  function lh.tray_count() return K end
  function lh.tray_start() return not foreign end
  function lh.tray_foreign() return foreign end
  local tile = dofile(dir .. "/tiles/notifications.lua")
  local heights = {}
  tile.T = {}; tile.R = function(id) return id end
  tile.sb = { surface = { damage = function() end, x = 1130 }, tile_rect = function() return 1130, 500 end, resize_tile = function(_, h) table.insert(heights, h) end }
  tile.foreground = lh.node{ layout = "fill" }; tile.node = lh.node{}
  tile:initialize(); settle()
  local by = {}; for _, it in ipairs(tile.items) do by[it.name] = it end
  check("host: the three items", by.Volume and by.Brightness and by.Touchpad and #tile.items == 3)
  local v, tp = by.Volume.views[1].node, by.Touchpad.views[1].node
  check("host: brightness hidden without a backlight, no slot", by.Brightness.hidden == true and by.Brightness.views[1].node.visible == false)
  check("host: volume after the 2 docked icons (slot 2: 61,6)", v.x == 61 and v.y == 6, v.x .. "," .. v.y)
  check("host: touchpad next (slot 3: 89,6)", tp.x == 89 and tp.y == 6 and tp.visible ~= false, tp.x .. "," .. tp.y)
  check("host: one row, 33 high", heights[#heights] == 33, heights[#heights])
  check("host: volume's tooltip in the tray", tip(v) == "Volume: 40%", tip(v))
  v.on_click(v, 1)
  check("host: a click on the volume icon opens its popup, anchored at the tile", lh.flyout_tile == by.Volume and by.Volume.anchor.where == "sidebar")
  local fx = by.Volume:place(80, 160)
  check("host: the popup left of the sidebar", fx == 1130 - 80, fx)
  lh.close_flyout()
  K = 4; lh._emit("tray", 4)
  check("host: 4 docked icons: volume in slot 4 (117,6), touchpad wraps to the second row (5,30)", v.x == 117 and v.y == 6 and tp.x == 5 and tp.y == 30, v.x .. "," .. v.y .. " " .. tp.x .. "," .. tp.y)
  check("host: two rows, 57 high", heights[#heights] == 57, heights[#heights])
  -- another client owns the tray (flxp-taskbar): the tile stays for the items, which take the first slots
  foreign = true; lh._emit("tray-owner", true)
  check("host: tray foreign: the tile shown for the items, the tray node hidden", tile.hidden == false and tile.tray.visible == false)
  check("host: tray foreign: volume in slot 0 (5,6)", v.x == 5 and v.y == 6, v.x .. "," .. v.y)
  tile:dispose()
  check("host: dispose ends the items' commands", next(pipes) == nil)
  -- the minimized sidebar's taskbar view: the tray's icons, then the items, 20 px apart
  foreign = false; K = 3
  local tb = dofile(dir .. "/tiles/notifications.lua")
  tb.T = { taskbar = { height = 30 } }; tb.R = function(id) return id end; tb.sb = tile.sb; tb.tb = { h = 30, surface = { damage = function() end } }
  local host = lh.node{}; tb:taskbar(host); settle()
  local tv = host.kids[1]
  local tvol; for _, it in ipairs(tb.items) do if it.name == "Volume" then tvol = it end end
  check("host: taskbar view: the tray 4 + 3 x 20 wide, then the items (two shown: 42 px)", tb.tray.w == 64 and tb.layer.w == 42, tb.tray.w .. " " .. tostring(tb.layer.w))
  check("host: taskbar view: volume at x 2, touchpad at 22 of the items", tvol.views[1].node.x == 2, tvol.views[1].node.x)
  check("host: taskbar view: one view node in the bar", tv == tb.view and #host.kids == 1)
  tvol.views[1].node.on_click(tvol.views[1].node, 1)
  local px, py = tvol:place(80, 160)
  check("host: the taskbar popup above the bar at the pointer", px == 1200 - 40 and py == 1024 - 30 - 160, px .. "," .. py)
  lh.close_flyout(); tb:dispose()
end
print(string.format("%d checks, %d failed", n, fails))
os.exit(fails == 0 and 0 or 1)
