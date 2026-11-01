-- luxshell runtime: event bus, animation, theme pack loading, the Avalon taskbar + sidebar builders, tiles.
-- Facts (sizes, ids, triggers) come from docs/SHELL-DESIGN.md sections 2.2-2.4; comments cite them.
local lh = lh
lh._handlers = {}
function lh.on(ev, fn) lh._handlers[ev] = lh._handlers[ev] or {}; table.insert(lh._handlers[ev], fn) end
function lh.off(ev, fn) for i, f in ipairs(lh._handlers[ev] or {}) do if f == fn then table.remove(lh._handlers[ev], i) return end end end
local debug_events = (os.getenv("LHSHELL_DEBUG") or "") ~= ""
function lh._emit(ev, ...) if debug_events and ev ~= "surface-press" then io.stderr:write(string.format("luxshell: [%d] event %s %s\n", lh.now(), ev, tostring((...)))) end for _, fn in ipairs(lh._handlers[ev] or {}) do local ok, err = pcall(fn, ...); if not ok then io.stderr:write("luxshell: " .. ev .. ": " .. tostring(err) .. "\n") end end end

-- animate(node, prop, to, ms, easing): Animator2 (2.4.3): 25 ms tick; easings linear, easeout 2t-t^2, easein 2t^2 (clamped), easeinout.
-- The first frame is applied at once (not one tick later), so an animation starts in the same repaint as its trigger.
local function ease(kind, t) if kind == "easeout" then return 2*t - t*t elseif kind == "easein" then return t*t elseif kind == "easeinout" then return t < 0.5 and 2*t*t or 1 - (-2*t+2)^2/2 end return t end
function lh.animate(node, prop, to, ms, easing, done)
  node._anim = node._anim or {}
  if node._anim[prop] then lh.cancel(node._anim[prop]) end
  local from = node[prop] or (prop == "opacity" and 1) or 0; local t0 = lh.now()
  if ms <= 0 then node[prop] = to; node:set({[prop] = to}); if done then done() end return end
  local id
  local function step()
    local t = math.min(1, (lh.now() - t0) / ms); local v = from + (to - from) * ease(easing or "linear", t)
    node[prop] = v; node:set({[prop] = v})
    if t >= 1 then if id then lh.cancel(id) end node._anim[prop] = nil; if done then done() end return true end
  end
  t0 = t0 - 25                                       -- frame 1 = one Animator2 tick in
  if step() then return end
  id = lh.timer(step, 25, true)
  node._anim[prop] = id
end

-- pack images are relative to packs/ in lhde's data directories (lhde-import: packs/<build>/imageres/<id>.png, ...)
local function img(id) return "4074/imageres/" .. id .. ".png" end
local function N(t) return lh.node(t) end
local screen_w, screen_h, screen_no = lh.screen()

------------------------------------------------------------------ theme (SHELL-DESIGN 14): session theme name → resolved theme → pack
-- The session theme is the root property _LH_THEME, a theme name liblhde resolves (docs/THEME-FORMAT.md); its Shell.pack
-- names the pack. At startup: _LH_THEME if someone (luxwm, a script) set it, else settings.ini [theme] name, which is then
-- published; LHSHELL_THEME (a pack directory) only when neither names a theme. A change of _LH_THEME
-- swaps the pack live (rebuild_shell below), sets the wallpaper and persists the name.
local function lua(f) return dofile(lh.lua_dir .. "/" .. f) end   -- luxshell's own Lua (lh.lua_dir: the tree's luxshell/ or <prefix>/lib/lhde/luxshell)
-- lhde's own programs beside us first (build/bin of the tree, <prefix>/bin), then PATH: lh.prog("flxp-taskbar")
function lh.prog(n) local p = lh.bin_dir .. "/" .. n; local f = io.open(p); if f then f:close() return "'" .. (p:gsub("'", "'\\''")) .. "'" end return n end
local S = lua("session.lua"); lh.session = S
local T = {}            -- the current pack; replaced in place on a switch, so every module holding T (tiles, flyout, toast) sees it
local pack_dir
local function load_pack(dir)
  local ok, P = pcall(dofile, dir .. "/shell.lua")
  if not ok or type(P) ~= "table" then io.stderr:write("luxshell: pack " .. tostring(dir) .. ": " .. tostring(P) .. "\n") return false end
  for k in pairs(T) do T[k] = nil end
  for k, v in pairs(P) do T[k] = v end
  T.font = T.font or "Segoe UI 9"; pack_dir = dir
  lh.image_filter(T.filter or "bilinear")   -- HackImage scaling: nearest (4074 packs) | bilinear (4051), scene.c
  return true
end
-- R(id): the pack's image for an Aero imageres id: the Slate packs remap ids (resmap aero_to_slate); a pack with its own
-- `imageres` directory (4051-slate: packs/4051/imageres, the 4051 imageres.dll images that differ from 4074's) takes the
-- image from there when it has it, else from packs/4074/imageres
local own_res = {}
local function R(id)
  id = T.ids and T.ids[id] or id
  local d = T.imageres
  if d then
    local key = d .. "/" .. id
    if own_res[key] == nil then own_res[key] = lh.data_find("packs/" .. key .. ".png") ~= nil end
    if own_res[key] then return key .. ".png" end
  end
  return img(id)
end
-- the effective values of a resolved theme (S.resolve: liblhde's record, docs/THEME-FORMAT.md): the theme files, their base
-- chain, the style's defaults and the fallbacks, with settings.ini's per-user [desktop] wallpaper = none (the flyout's "Show the
-- wallpaper" off) and the current layer applied by the resolver. v.wallpaper_src says where the wallpaper came from.
local function effective(e)
  local v = {}
  for _, k in ipairs{ "msstyles", "color", "size", "frame", "frame_classic", "shell", "fonts" } do v[k] = e and e[k] or "" end
  v.wallpaper, v.mode = e and e.wallpaper or "", e and e.mode or "fill"
  v.wallpaper_src = e and e.why and e.why["Desktop.wallpaper"] or "?"
  v.icons = e and e.icons or ""
  v.composition = e and e.composition or "auto"
  v.animations = e and e.v and e.v["Glass.animations"] or ""
  return v
end
lh.effective_theme = effective
-- _LH_THEME_RESOLVED (THEME-FORMAT.md section 4): theme=NAME, serial= (_LH_SETTINGS_SERIAL), then the record's key=value lines;
-- written when it does not describe this record yet (luxwm writes the same record when it resolves first)
local function publish_resolved(e)
  if not e then return end
  local serial = lh.root_prop("_LH_SETTINGS_SERIAL"); if math.type(serial) ~= "integer" then serial = 0 end
  local mine = "theme=" .. e.name .. "\n" .. (e.record or "")
  local cur = lh.root_prop("_LH_THEME_RESOLVED")
  local body = type(cur) == "string" and cur:gsub("serial=[^\n]*\n", "", 1) or nil
  if body ~= mine then lh.set_root_prop("_LH_THEME_RESOLVED", "theme=" .. e.name .. "\nserial=" .. serial .. "\n" .. (e.record or "")) end
  if (os.getenv("LHSHELL_DEBUG") or "") ~= "" then local v = effective(e); io.stderr:write(string.format("luxshell: wallpaper %s (%s) from %s\n", v.wallpaper, v.mode, v.wallpaper_src or "?")) end
end
-- our own writes of _LH_THEME come back as PropertyNotify: counted here so the handler below ignores exactly those
local theme_echo = {}
local function set_theme_prop(v) theme_echo[v] = (theme_echo[v] or 0) + 1; lh.set_root_prop("_LH_THEME", v) end
-- startup: the root wins (SETTINGS.md "Conflicts"): _LH_THEME holds the selected name; else settings.ini [theme] name. liblhde
-- resolves it (a theme file, an alias, a bare style); an alias goes back on the root as the theme's name.
local theme_name, theme_entry
do
  local root, file = lh.root_prop("_LH_THEME"), S.get("theme", "name")
  local want = root or file
  local err
  if want then theme_entry, err = S.resolve(want) end
  if not theme_entry and root and file and file ~= root then theme_entry = S.resolve(file) end
  if theme_entry then
    theme_name = theme_entry.name
    publish_resolved(theme_entry)
    if root ~= theme_entry.name then set_theme_prop(theme_entry.name) end    -- first one up publishes; an alias: the theme's name
    S.set("theme", "name", theme_name)                                       -- the first writer brings the file in line
  elseif want then io.stderr:write("luxshell: theme " .. want .. ": " .. tostring(err) .. " (lhde-theme list)\n") end
end
-- the selected theme's name and resolved entry (settings flyout; the clock tile reads the visual style); defined before the
-- first build_tiles
function lh.theme_name() return theme_name, theme_entry end
if not (theme_entry and theme_entry.shell and load_pack(theme_entry.shell)) then load_pack(S.pack_dir(os.getenv("LHSHELL_THEME") or "4074-aero")) end
-- the theme's path.fonts: its font directories as fontconfig application fonts, so the packs' "Segoe UI 9"
-- is 4074's Segoe UI 0.7 (luxshell.c lh.fonts; the set is replaced on every theme switch). Returns true when the set changed.
local function apply_fonts(e)
  local fam, file, changed = lh.fonts(e and e.fonts or "", T.font)
  if debug_events or changed then io.stderr:write(string.format("luxshell: fonts %s: \"%s\" -> %s (%s)\n", e and e.fonts or "(none)", tostring(T.font), fam, file)) end
  return changed
end
apply_fonts(theme_entry)
------------------------------------------------------------------ shell settings (SHELL-DESIGN 14; settings.ini, SETTINGS.md; defaults written on first run)
--   [taskbar] kind = avalon|classic|none (classic = flxp-taskbar, started from [commands] taskbar; LHSHELL_TASKBAR overrides it
--   for one run), autohide = 0|1, rows = 1..3 (Taskbar\RowCount, 2.4.2; the top-edge drag), classic_start_menu = flxp|flxp-classic
--   (the menu the classic bar's Start button opens)
--   [sidebar] mode = full|minimized|pinned, width = 100..600 (Sidebar\Width, default 150; the SidebarSizer drag),
--   with_classic = 0|1 (0, default: no sidebar with the classic taskbar; 1: the 4051 layout), yield_bottom = auto|0,
--   stacking = top|bottom (top: 4074, the sidebar is above the windows (_NET_WM_STATE_ABOVE) with its strut, runs full height and
--   the taskbar yields to it; bottom: part of the desktop, under the windows (_NET_WM_STATE_BELOW) with no strut, the taskbar runs
--   the full width over it and the sidebar ends at its top; Win+S = _LHSHELL_COMMAND "sidebar-stacking toggle" flips it at once).
--   [clock] pinned = 1|0 with pinned_pack = the pack it was chosen in (other packs use their own default, T.clock_pinned), layout
--   [desktop] wallpaper = none (the picture off in every theme; else the theme's), desktop_black = 0|1 (0 = maximised mode only switches the bars' black layers; 1 = also the
--   full-screen black of the desktop layer)   [shell] max_fade_ms = 0 (the black layers switch at once) or the fade in ms
--   (LHSHELL_WALLPAPER=0 forces the wallpaper off). Runtime state (the tile order) stays in state.ini.
for _, d in ipairs{ { "desktop", "desktop_black", "0" }, { "taskbar", "autohide", "0" }, { "taskbar", "rows", "1" },
                    { "sidebar", "width", "150" }, { "sidebar", "with_classic", "0" }, { "sidebar", "stacking", "top" },
                    { "clock", "layout", "default" }, { "shell", "max_fade_ms", "0" } } do
  if not S.has(d[1], d[2]) then S.set(d[1], d[2], d[3]) end
end
local SB_MIN, SB_MAX = 100, 600         -- _ValidateSize(v, 100, 150, 600) (2.4.2, L53528)
local ROWS_MAX = 3                      -- Taskbar\RowCount 1..3 (Taskbar.Initialize, L51705)
local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end
local cfg = {
  autohide = S.get("taskbar", "autohide") == "1",
  sidebar = S.get("sidebar", "mode") or (theme_entry and theme_entry.v["Shell.sidebar"]) or "full",   -- the user's, else the theme's
  rows = clamp(math.floor(tonumber(S.get("taskbar", "rows") or "") or 1), 1, ROWS_MAX),
  sidebar_w = tonumber(S.get("sidebar", "width") or ""),
  sb_bottom = S.get("sidebar", "stacking") == "bottom",   -- [sidebar] stacking = bottom: the sidebar under the windows
  composition = "follow",                                 -- set below from the per-theme chain
  -- maximised windows do not black out the whole desktop (luxwm crossfades the frame to its dark maximised plate; only
  -- the bars' black layers fade). desktop_black = 1: the full-screen black of the desktop layer too
  desktop_black = S.get("desktop", "desktop_black") == "1",
  -- [shell] max_fade_ms: the black layers' fade in maximised mode; 0 (default) = switched in the same repaint, no fade
  max_fade_ms = math.max(0, math.floor(tonumber(S.get("shell", "max_fade_ms") or "") or 0)),
}
-- [taskbar] kind: avalon = luxshell's taskbar; classic = no taskbar surface, struts or _NET_WM_ICON_GEOMETRY of ours, the
-- classic flxp-taskbar runs instead (started and stopped by us); none = neither.
local function taskbar_kind(v)
  v = (v or ""):lower()
  if v == "classic" or v == "none" or v == "avalon" then return v end
  return nil
end
-- taskbar kind, start menu and the compositor switch are theme keys (THEME-FORMAT.md):
-- S.theme_value(theme_entry, ...) = the resolved record's key (the current layer, the theme file, its base chain, the default); they
-- are applied at startup, on every theme change and on _LH_SETTINGS_CHANGED. LHSHELL_TASKBAR / LHSHELL_COMPOSITION (on|off|follow;
-- lhde-session sets it from LHDE_PICOM=0/1) force one for this run.
local taskbar_env = taskbar_kind(os.getenv("LHSHELL_TASKBAR"))
local comp_env = os.getenv("LHSHELL_COMPOSITION"); if comp_env ~= "on" and comp_env ~= "off" and comp_env ~= "follow" then comp_env = nil end
local function theme_taskbar(e) return taskbar_env or taskbar_kind(S.theme_value(e, "taskbar")) or "avalon" end
local function theme_composition(e)
  if comp_env then return comp_env end
  local c = S.theme_value(e, "composition"); if c ~= "on" and c ~= "off" then c = "follow" end
  return c
end
cfg.taskbar = theme_taskbar(theme_entry)
cfg.composition = theme_composition(theme_entry)
local function tb_on() return cfg.taskbar == "avalon" end
-- [sidebar] with_classic = 0 (default): with the classic taskbar the sidebar is not shown at all (unmapped, no strut; flxp-taskbar
-- has the tray and the clock, _LHSHELL_CLOCK_VISIBLE = 0); its mode is kept and comes back with the Avalon taskbar. 1: the 4051
-- layout, the sidebar yielding above the classic bar.
cfg.with_classic = S.get("sidebar", "with_classic") == "1"
-- [sidebar] locked = 1: "Lock the Sidebar" in the sidebar menu (CShellSidebar::SetLockBar, 2.4.6): no width drag on the sizer
cfg.sb_locked = S.get("sidebar", "locked") == "1"
local function sb_hidden() return cfg.taskbar == "classic" and cfg.with_classic ~= true end
lh.sidebar_hidden = sb_hidden
if cfg.sidebar_w then cfg.sidebar_w = clamp(math.floor(cfg.sidebar_w), SB_MIN, SB_MAX) end
-- [clock] pinned: the user's choice (the clock's menu) holds for the pack it was made in ([clock] pinned_pack); any other pack
-- (and a state.ini from before pinned_pack existed) uses the pack's default T.clock_pinned (nil = pinned, the 4074 look)
local function pack_key() return T.clock_pack or (pack_dir or ""):match("([^/]+)/*$") or "" end   -- T.clock_pack: a pack that shares another's choice (4074-aerovision: 4074-aero)
local function clock_pinned()
  local v, at = S.get("clock", "pinned"), S.get("clock", "pinned_pack")
  if (v == "0" or v == "1") and at == pack_key() then return v == "1" end
  return T.clock_pinned ~= false
end
cfg.clock_pinned = clock_pinned()
if cfg.sidebar ~= "full" and cfg.sidebar ~= "minimized" and cfg.sidebar ~= "pinned" then cfg.sidebar = "full" end
local feh_mode = { fill = "--bg-fill", fit = "--bg-max", center = "--bg-center", tile = "--bg-tile", stretch = "--bg-scale" }
local wp_applied        -- wallpaper|mode last put on the root (a settings reload only repaints when it changed)
-- Another program owns the desktop (flxp-desktop: the session sets LHSHELL_DESKTOP=0 while it runs, see below, and
-- lhde-session already put the same wallpaper on the root once): luxshell then never runs feh/xsetroot itself. Also when a
-- _NET_WM_WINDOW_TYPE_DESKTOP window of another client exists (flxp-desktop started by hand next to a luxshell with its own layer).
local function desktop_elsewhere()
  local e = (os.getenv("LHSHELL_DESKTOP") or ""):lower()
  if e == "0" or e == "off" or e == "no" or e == "false" then return true end
  local me = ""; local f = io.open("/proc/self/stat"); if f then me = (f:read("*l") or ""):match("^(%d+)") or ""; f:close() end
  local p = io.popen("for w in $(xprop -root _NET_CLIENT_LIST 2>/dev/null | sed 's/.*# //; s/,//g'); do "
    .. "xprop -id $w _NET_WM_WINDOW_TYPE _NET_WM_PID 2>/dev/null | tr '\\n' ' '; echo; done 2>/dev/null")
  local found = false
  if p then
    for l in p:lines() do
      if l:find("_NET_WM_WINDOW_TYPE_DESKTOP", 1, true) and l:match("_NET_WM_PID.-=%s*(%d+)") ~= me then found = true end
    end
    p:close()
  end
  return found
end
local function set_wallpaper(e)
  local v = effective(e)
  wp_applied = v.wallpaper .. "|" .. v.mode
  if desktop_elsewhere() then return end
  if (os.getenv("LHSHELL_WALLPAPER") or "") == "0" or v.wallpaper == "none" then lh.spawn("xsetroot -solid '#3a6ea5' 2>/dev/null") return end   -- the 4074 [sysmetrics] BACKGROUND 58 110 165
  if v.wallpaper == "" then return end
  lh.spawn("feh --no-fehbg " .. (feh_mode[v.mode] or "--bg-fill") .. " " .. S.quote(v.wallpaper) .. " 2>/dev/null")
end
if theme_entry and not lh.root_prop("_XROOTPMAP_ID") then set_wallpaper(theme_entry) end   -- startup: only when nobody set one
local black_on = false  -- maximised mode (set_maximized below); new black layers start in its state
-- (nodes are reference counted (scene.h): a replaced pack's tree is freed by the GC once nothing (an animation, a timer, a
-- tile) holds a node of it, and a node someone holds stays valid)

------------------------------------------------------------------ desktop layer (SHELL-DESIGN 11)
-- full screen, _NET_WM_WINDOW_TYPE_DESKTOP, sticky + below, no struts, at the bottom of the stack. Unmapped normally (the
-- wallpaper is the root background); with [shell] desktop_black = 1 it is mapped black at once in maximised mode (no fade)
-- and unmapped at once on restore; the default (0) leaves it unmapped. Future home of the Aurora.
-- LHSHELL_DESKTOP=0 (or off/no/false) leaves it out entirely, for when another program owns the desktop (flxp-desktop draws
-- the wallpaper and icons and does the maximised-mode black fade itself; lhde-session sets it while flxp-desktop runs).
-- lh.desktop_layer is then nil; the taskbar and sidebar black layers still switch.
local dk = nil
local dk_env = (os.getenv("LHSHELL_DESKTOP") or ""):lower()
if not (dk_env == "0" or dk_env == "off" or dk_env == "no" or dk_env == "false") then
  dk = {}
  -- opaque (24-bit): it is only ever mapped while black, and a solid window lets picom skip the full-screen blur behind it
  -- (an ARGB one cost ~150-300 ms per composited frame during the maximise on the xrender lab). The Aurora will need ARGB again.
  dk.surface = lh.surface{ name = "luxshell-desktop", type = "desktop", x = 0, y = 0, w = screen_w, h = screen_h, opaque = true }
  dk.root = N{ layout = "fill" }
  dk.black = dk.root:add(N{ kind = "rect", fill = "#000000", opacity = 0 })                        -- the maximised-mode black
  dk.scene = dk.root:add(N{ layout = "fill" })                                                     -- Aurora (xaml_sidebar_desktopaurora) goes here
  dk.surface:root(dk.root)
end

------------------------------------------------------------------ taskbar (SHELL-DESIGN 2.2)
local sidebar_w = cfg.sidebar_w or T.sidebar.width   -- 150 (Sidebar\Width): the taskbar yields to the sidebar (CShellTaskbar::SetMixedTaskbarSidebar)
-- 30 (Aero/Jade) per row: TaskList row = 30, buttons 29 high; GetTaskbarHeight = row * Taskbar\RowCount; pct: autohide visible percent
local tb = { row_h = T.taskbar.height, rows = cfg.rows, pct = 100 }
tb.h = tb.row_h * tb.rows
local mode = cfg.sidebar                -- sidebar mode (6.4)
tb.wide = mode == "minimized"           -- the pinned tiles live in the taskbar (sidebar minimized into it)
-- the taskbar runs the full width when the sidebar is minimized, or over a desktop-level sidebar ([sidebar] stacking = bottom)
local function tb_full() return tb.wide or cfg.sb_bottom end
tb.geo = { x = 0, y = screen_h - tb.h, w = tb_full() and screen_w or (screen_w - sidebar_w), h = tb.h }
tb.surface = lh.surface{ name = "luxshell-taskbar", x = tb.geo.x, y = tb.geo.y, w = tb.geo.w, h = tb.h, dock = "bottom", strut = tb_on() and tb.h or 0, layer = "above" }
-- the sidebar toggle's 17x17 glyph: centred in its 29x29 slot on the first row (Border 6, TransformDecorator VCenter), the
-- slot itself centred on a bar of several rows; Taskbar.BarVisible=false (sidebar minimized) → 1167 [Slate 1162]
local function toggle_y() return math.floor((tb.row_h - 17) / 2) + math.floor((tb.h - tb.row_h) / 2) end
-- pack key toggle_minimized = { hot = id, pressed = id }: own hot/pressed images while the sidebar is minimized (Plex: BARDOWN,
-- the bar shown: BARUP); default: 1171/1172 in both modes
local function toggle_images()
  local tm = mode == "minimized" and T.toggle_minimized or nil
  return { normal = R(mode == "minimized" and 1167 or 1170), hot = R(tm and tm.hot or 1171), pressed = R(tm and tm.pressed or 1172) }
end
-- pack key toggle = { slot = W, x = X }: the toggle's slot width and the glyph's x in it (default 29 and 5: Border 6, 4074)
local function toggle_slot_w() return T.toggle and T.toggle.slot or 29 end
-- pack key start_compose = { flag = id, flag_w, flag_h, text, font, color, text_pad = {l, t, r, b}, hot = id, checked = id }:
-- a Start button made of parts (4042 main's System.Windows.Desktop.StartButton: StackImageResourcePresenter { Control
-- StartButtonImage Dock Left, ThemeImage true size at the top left; Text StartButtonText Dock Fill "start" }), its width the
-- flag plus the measured text; the button's own image only on hover (PointerFocused) and while the menu is open (FakePressed).
-- Sets tb.start_w.
local function start_composed(slot)
  local c = T.start_compose
  local pad = c.text_pad or {0, 0, 0, 0}
  local probe = N{ kind = "text", text = c.text, font = c.font }
  local tw = math.ceil(probe:measure())
  local w = c.flag_w + pad[1] + tw + pad[3]
  tb.start_w = w; slot:set{ w = w }
  local b = slot:add(N{ kind = "button", x = 0, y = 0, w = w, h = tb.row_h, layout = "dock",
    images = { normal = R(c.normal), hot = R(c.hot), pressed = R(c.checked), checked = R(c.checked), hotchecked = R(c.checked) },
    on_press = function(self, button) if button == 1 then lh._emit("start-press") end end,
    on_click = function(self, button) if button == 1 then lh._emit("start-click") end end })
  local fl = b:add(N{ dock = "left", w = c.flag_w, layout = "canvas" })
  fl:add(N{ kind = "image", image = R(c.flag), x = 0, y = 0, w = c.flag_w, h = c.flag_h })
  b:add(N{ kind = "text", text = c.text, font = c.font, color = c.color or "#ffffff", dock = "fill", margin = pad })
  return b
end
local function build_taskbar()          -- the node tree; the surface stays (built again on a pack switch)
  local root = N{ layout = "fill" }
  tb.root = root
  tb.black = tb.root:add(N{ kind = "rect", fill = "#000000", opacity = black_on and 1 or 0 })        -- TaskbarBlackLayer
  -- pack key taskbar_dy (default 0): background, Start button, tasks, toggle, tray all this many px lower inside the unchanged
  -- surface (clipped at the bottom; the 4074 bar's last bare row disappears). Hit-testing follows the nodes.
  -- the background stays unshifted (its highlight row keeps the bar's top; the pill then covers the bare last row),
  -- only the content (InnerTaskbar) moves down
  tb.bg = root:add(N{ kind = "ninegrid", image = R(1187), grid = T.taskbar.bg_grid or {0,7,0,0}, filter = T.taskbar.bg_filter }) -- TaskbarBackground, Rotated → 1187 grid 0,7,0,0
  local body = root
  if (T.taskbar_dy or 0) ~= 0 then
    body = root:add(N{ layout = "canvas" })
    tb.shift = body:add(N{ x = 0, y = T.taskbar_dy, w = "100%", h = "100%", layout = "fill" })
    body = tb.shift
  end
  tb.inner = body:add(N{ layout = "dock" })                                                      -- InnerTaskbar
  -- StartButton: Width 83, Height 29, Dock Left, Border(0,0,4,0) > HackBorder 1179/1180/1181
  -- StartButton.FakePressed (1181) while the start menu is open: the "checked" state
  -- (in a canvas slot: on a taller bar of several rows it stays 29 high at the top, the first row (inferred, as XP))
  tb.start_w = T.start.w
  if T.start_compose then   -- built from parts, not one image per state: see start_composed
    tb.start_slot = tb.inner:add(N{ dock = "left", w = T.start.w, layout = "canvas" })
    tb.start = start_composed(tb.start_slot)
  else
  tb.start_slot = tb.inner:add(N{ dock = "left", w = T.start.w, layout = "canvas" })
  -- pack key start_dy (default 0): the whole button this many px lower, clipped at the bar bottom (the originals leave the bar's
  -- last row below the pill; the user finds it odd). start_alpha (default 1): multiplies the three images' alpha (glass pill).
  tb.start = tb.start_slot:add(N{ kind = "button", x = 0, y = T.start_dy or 0, w = T.start.w, h = T.start.h + (T.start_extend_bottom or 0), margin = {0, 0, 4, 0},
    grid = (T.start_extend_bottom or 0) > 0 and {0, 0, 1, 0} or nil, opacity = T.start_alpha or 1,
    images = { normal = R(1179), hot = R(1180), pressed = R(1181), checked = R(1181), hotchecked = R(1181) },
    on_press = function(self, button) if button == 1 then lh._emit("start-press") end end,
    on_click = function(self, button) if button == 1 then lh._emit("start-click") end end })
  end
  -- TaskbarToggleBar (SidebarButton): Dock Right, 29x29 slot with a 17x17 glyph centred (Border 6)
  tb.toggle_slot = tb.inner:add(N{ dock = "right", w = toggle_slot_w(), layout = "canvas" })
  -- Taskbar.BarVisible=false (sidebar minimized) → 1167 [Slate 1162]
  tb.toggle = tb.toggle_slot:add(N{ kind = "button", x = T.toggle and T.toggle.x or 5, y = toggle_y(), w = 17, h = 17,   -- TransformDecorator VCenter
    images = toggle_images(),
    on_click = function() lh._emit("toggle-sidebar") end })
  -- TBContentHostCollection: the sidebar's pinned tiles while it is minimized (2.4.2 SetMinimized), at the right end
  tb.tbhost = tb.inner:add(N{ dock = "right", layout = "hbox" })
  tb.tasks = tb.inner:add(N{ dock = "fill", layout = "vbox", clip = true })                        -- TaskOverflowableCollection (rows of buttons)
  -- the height sizer: the horizontal bar's TaskbarSizer is Width 0 (Rotated); its rows change through the appbar's sizing edge
  -- (CAvalonAppBar::_PtOnResizableEdge → SetTaskbarRowCount, 2.4.2): a 3 px SizeNS strip along the top edge, invisible
  local edge = tb.root:add(N{ layout = "canvas" })
  tb.sizer = edge:add(N{ x = 0, y = 0, w = "100%", h = 3, hoverable = true, cursor = "sizens" })
  tb.sizer.on_press = function(self, button) if button == 1 then tb.drag_rows = tb.rows end end
  tb.sizer.on_drag = function(self, dx, dy) if tb.drag_rows then lh.taskbar_set_rows(math.floor((tb.drag_rows * tb.row_h - dy) / tb.row_h + 0.5)) end end
  tb.sizer.on_release = function() if tb.drag_rows then tb.drag_rows = nil; S.set("taskbar", "rows", tostring(tb.rows)) end end
  tb.surface:root(tb.root)
end
build_taskbar()

-- text on the taskbar (task buttons, the taskbar clock): pack key taskbar_text = "plain" (default: white, the 4074 look) | "glow"
-- (Aero vision's text on glass, as lhnav: black over a white halo, the GDI coverage blurred: taskbar_glow = { color, size,
-- strength }, default white, 4 px, 2.5). Returns the text node keys; nil glow = none.
function lh.taskbar_text_style()
  if T.taskbar_text == "glow" then
    local g = T.taskbar_glow or {}
    return { color = g.text or "#000000", glow = g.color or "#ffffff", glow_size = g.size or 4, glow_strength = g.strength or 2.5 }
  end
  return { color = "#ffffff" }
end
-- the task button's state images; pack key taskbutton.images = { state = id } replaces single states (4042 main: pressed and
-- hot+active have images of their own)
local function task_images()
  local o = T.taskbutton.images or {}
  local function I(st, id) return R(o[st] or id) end
  return { normal = I("normal", 1182), hot = I("hot", 1184), pressed = I("pressed", 1185), checked = I("checked", 1185), hotchecked = I("hotchecked", 1185), flash = I("flash", 1183) }
end
-- task buttons: TaskButton style = Border(0,2,2,0) > HackBorder 1182/1184/1185 grid 3 > Border(6,5,6,6) > [icon 16 + 2px | text white ellipsis]
local function task_button(win, width)
  local marg = T.taskbutton.margin or {0, 2, 2, 0}
  local b = N{ kind = "button", w = width, margin = marg, grid = T.taskbutton.grid or 3, layout = "fill", icon_geometry_for = win.id,   -- _NET_WM_ICON_GEOMETRY (luxwm's minimise target)
    images = task_images() }
  b.win = win
  local content = b:add(N{ layout = "dock", margin = T.taskbutton.content or {6, 5, 6, 6} })
  local icon = win.icon or T.default_icon   -- _NET_WM_ICON, then WM_HINTS (C side), then the pack's default (an assets path)
  if icon then content:add(N{ kind = "image", image = icon, w = 16, h = 16, dock = "left", margin = T.taskbutton.icon_margin or {0, 0, 2, 0} }) end
  local ts = lh.taskbar_text_style()
  content:add(N{ kind = "text", text = win.title, color = ts.color, glow = ts.glow, glow_size = ts.glow_size, glow_strength = ts.glow_strength, font = T.taskbutton.font or T.font, ellipsis = true, valign = "center", dock = "fill" })
  if win.active then b.checked = true; b:set{ state = "checked" } end
  -- drag to reorder (4074's taskbar): past the drag threshold the button takes the place of the one under the pointer
  b.on_press = function(self, button) if button == 1 then tb.drag = { id = self.win.id }; tb.dragged = false end end
  b.on_drag = function(self, dx, dy)
    local d = tb.drag
    if not d or d.id ~= self.win.id then return end
    if not d.moved and math.abs(dx) < 4 and math.abs(dy) < 4 then return end   -- SM_CXDRAG / SM_CYDRAG
    d.moved = true
    local px, py = lh.pointer()
    local x, y = px - tb.geo.x, py - tb.geo.y
    for j, n in ipairs(tb.task_nodes or {}) do
      local bx, by, bw, bh = n:bounds()
      if x >= bx and x < bx + bw and y >= by and y < by + bh then
        if n.win.id ~= d.id then tb.move_task(d.id, n.win.id) end
        break
      end
    end
  end
  b.on_release = function(self, button) if button == 1 and tb.drag then tb.dragged = tb.drag.moved; tb.drag = nil end end
  b.on_click = function(self, button)
    if button == 1 and tb.dragged then tb.dragged = false return end   -- the end of a drag is no click
    if button == 1 then if self.win.active and not self.win.hidden then lh.minimize(self.win.id) else lh.activate(self.win.id) end end
    -- XP's task button menu is the window's system menu: luxwm's window menu at the pointer (minimised windows too)
    if button == 3 then local x, y = lh.pointer(); lh.window_menu(self.win.id, x, y) end
  end
  return b
end
-- the buttons' order: the windows as they came (lh.windows()), rearranged by dragging; kept for the X session in the root
-- property _LHSHELL_TASK_ORDER (the window ids, decimal, space separated; every desktop's), so a restarted luxshell keeps it
tb.order = {}
for id in (lh.root_prop("_LHSHELL_TASK_ORDER") or ""):gmatch("%d+") do table.insert(tb.order, tonumber(id)) end
-- (ids of windows that are gone stay until the next drag: at startup the window list may still be empty)
local function task_order(wins)
  local seen, order = {}, {}
  for _, id in ipairs(tb.order) do if not seen[id] then seen[id] = true; table.insert(order, id) end end
  for _, w in ipairs(lh.windows()) do if not seen[w.id] then seen[w.id] = true; table.insert(order, w.id) end end
  tb.order = order
  local pos = {}
  for i, id in ipairs(order) do pos[id] = i end
  table.sort(wins, function(a, b) return (pos[a.id] or 0) < (pos[b.id] or 0) end)
  return wins
end
-- id goes where target is: after it when it was before it (dragged right), else before it
function tb.move_task(id, target)
  local i, j
  for k, v in ipairs(tb.order) do if v == id then i = k elseif v == target then j = k end end
  if not i or not j then return end
  table.remove(tb.order, i)
  if i < j then j = j - 1 end
  table.insert(tb.order, i <= j and j + 1 or j, id)
  local alive, ids = {}, {}
  for _, w in ipairs(lh.windows()) do alive[w.id] = true end
  for k = #tb.order, 1, -1 do if not alive[tb.order[k]] then table.remove(tb.order, k) end end
  for k, v in ipairs(tb.order) do ids[k] = tostring(v) end
  lh.set_root_prop("_LHSHELL_TASK_ORDER", table.concat(ids, " "))
  if not tb.rebuild then return end
  tb.rebuild()
  -- the rebuilt button of the dragged window takes the press over, so the drag goes on
  for _, n in ipairs(tb.task_nodes or {}) do if n.win.id == id then tb.surface:press(n) break end end
end
local function rebuild_tasks()
  tb.tasks:clear(); tb.task_nodes = {}
  if not tb_on() then return end           -- classic / none: no buttons, so no _NET_WM_ICON_GEOMETRY of ours either (luxshell.c drops them)
  local wins, desk = {}, lh.desktop()
  for _, w in ipairs(lh.windows()) do   -- the current desktop's windows and the sticky ones (EWMH _NET_WM_DESKTOP 0xFFFFFFFF)
    if w.desktop == desk or w.desktop == -1 or w.desktop == 0xFFFFFFFF then table.insert(wins, w) end
  end
  task_order(wins)
  local n = #wins; if n == 0 then tb.surface:damage() return end
  local hostw = tb.tbhost:desired(screen_w, tb.h)
  local avail = tb.geo.w - (tb.start_w or T.start.w) - toggle_slot_w() - hostw   -- the task list's width after this layout (bounds() lags one paint)
  -- width rule (2.4.2): rows = the bar's rows; items per row from the count; width = min(140, w / itemsPerRow)
  local per = math.max(1, math.ceil(n / tb.rows))
  local width = math.min(T.taskbutton.max_w or 140, math.floor(avail / per))
  -- pack key taskbutton.fill_w (4042 main, System.Windows.Desktop.TaskPresenter.OnUpdate): the buttons share the whole width,
  -- columns = min(items per row, max(1, floor(width / fill_w))), each width / columns (one window: one button over the bar)
  if T.taskbutton.fill_w then
    per = math.min(per, math.max(1, math.floor(avail / T.taskbutton.fill_w)))
    width = math.floor(avail / per)
  end
  local row
  for i, w in ipairs(wins) do
    if (i - 1) % per == 0 then row = tb.tasks:add(N{ layout = "hbox", h = tb.row_h }) end
    table.insert(tb.task_nodes, row:add(task_button(w, width)))
  end
  tb.surface:damage()
  tb.surface:raise()
end
tb.rebuild = rebuild_tasks
lh.on("windows", rebuild_tasks); lh.on("window", rebuild_tasks); lh.on("desktop", rebuild_tasks)

------------------------------------------------------------------ sidebar (SHELL-DESIGN 2.3)
local sb = { w = sidebar_w }
sb.geo = { x = screen_w - sb.w, y = 0, w = sb.w, h = screen_h }
-- [sidebar] stacking: top = above the windows (_NET_WM_STATE_ABOVE, like the taskbar), with its strut, full height, over the
-- (shortened) taskbar at the corner (4074); bottom = a desktop-level layer: _NET_WM_STATE_BELOW (luxwm keeps BELOW docks under the
-- clients), no strut (windows and maximised windows cover it), ending at the top of the full-width taskbar, which stays above it.
local function sb_layer() return cfg.sb_bottom and "below" or "above" end
sb.surface = lh.surface{ name = "luxshell-sidebar", x = sb.geo.x, y = 0, w = sb.w, h = screen_h, dock = "right", strut = cfg.sb_bottom and 0 or sb.w, layer = sb_layer() }
-- top: raised (luxwm keeps the docks' order among themselves, docks_raise() raises them as a group in the server's order, so a
-- plain raise puts the sidebar over the taskbar); bottom: lowered under every window (the desktop layer goes below it again)
local function sb_stack()
  if cfg.sb_bottom then sb.surface:lower() else sb.surface:raise() end
  if dk then dk.surface:lower() end
end
local function build_sidebar()          -- the node tree (tiles: build_tiles); built again on a pack switch and a mode change
  sb.root = N{ layout = "fill" }
  sb.black = sb.root:add(N{ kind = "rect", fill = "#000000", opacity = black_on and 1 or 0 })        -- SidebarBlackLayer
  sb.bg = sb.root:add(N{ kind = "ninegrid", image = R(1178), grid = T.sidebar.bg_grid or {36,0,0,0} })  -- SidebarBackground 1178 grid 36,0,0,0 (Slate: 1000, no grid)
  sb.main = sb.root:add(N{ layout = "dock" })                                                        -- sidebarMain
  if mode == "pinned" then   -- 6.4 "pinned": only the bottom block of pinned tiles, no options row, no tile collection
    sb.pinned = sb.main:add(N{ dock = "fill", layout = "vbox" })
    sb.tiles = nil; sb.clocksep = nil
    sb.add_sizer()
    sb.surface:root(sb.root)
    return
  end
  if T.sidebar.title then   -- a pack's own title band (4042-plex), no separators: see sb.plex_title
    sb.plex_title()
    sb.pinned = sb.main:add(N{ dock = "bottom", layout = "vbox" })
    sb.clocksep = nil
    sb.tiles = sb.main:add(N{ dock = "fill", layout = "vbox", clip = true })
    sb.add_sizer()
    sb.surface:root(sb.root)
    return
  end
  -- sidebarTitle: OptionsButton Height 17, glyph Border(4,7,2,2) Dock Right, 9x6 glyph 1176/1177, "Options" text Opacity 0 fades in on hover
  sb.title = sb.main:add(N{ dock = "top", h = 17, layout = "dock" })
  sb.options = sb.title:add(N{ kind = "button", dock = "right", layout = "dock", w = 70, h = 17 })
  -- the HackImage is 9x6 in its Border (4,7,2,2) on the 17 px row: 6 high at y 7 (the bottom inset 4 keeps the docked image
  -- from stretching to 8 rows), and 1176/1177's 10 columns go into 9 by dropping one, as both 4051-vm-now.png and
  -- lh4074-181533.png show (nearest: exact; bilinear 34, the old stretched 9x8: 494 per pixel)
  sb.options_glyph = sb.options:add(N{ kind = "image", image = R(1176), w = 15, h = 17, dock = "right", margin = {4, 7, 2, 4}, filter = "nearest" })
  sb.options_text = sb.options:add(N{ kind = "text", text = "Options", color = "#ffffff", font = T.font, dock = "right", valign = "center", opacity = 0 })
  sb.options.on_hover = function(self, over) sb.options_glyph:set{ image = R(over and 1177 or 1176) }; lh.animate(sb.options_text, "opacity", over and 1 or 0, 120) end
  sb.options.on_click = function(self, button) if button == 1 then lh._emit("sidebar-options") end end   -- SidebarControl._OnOptions: the sidebar menu
  sb.main:add(N{ kind = "image", image = R(1199), dock = "top", h = 2 })                             -- SidebarOptionsSeperator (1199 148x3, Height 2)
  sb.pinned = sb.main:add(N{ dock = "bottom", layout = "vbox" })                                     -- SBPinnedContentHostCollection (clock)
  sb.clocksep = sb.main:add(N{ kind = "image", image = R(1199), dock = "bottom", h = 3 })             -- SidebarClockSeperator Height 3
  sb.tiles = sb.main:add(N{ dock = "fill", layout = "vbox", clip = true })                           -- SBContentHostCollection
  sb.add_sizer()
  sb.surface:root(sb.root)
end
-- pack key sidebar.title = { h, image, hot, pressed, glyph, glyph_w, glyph_h, glyph_pad = {l, t, r, b} }: 4042 main's SidebarTitle
-- (System.Windows.Desktop.SidebarElement + Sidebarstylesheet_themed): a FillImageResourcePresenter band of Height 27 with
-- ThemeImage(Sidebar, TITLEBACK) stretched, holding the SidebarOptions button over its whole width (no image at rest,
-- FLYOUTOPTIONSBACK hot / pressed stretched), its text "Options" of Height 0 (never shown) and the glyph FLYOUTOPTIONSGLYPH
-- (state 1 in every state) Dock Right, Padding 4,0,4,0, true size, centred
function sb.plex_title()
  local t = T.sidebar.title
  sb.title = sb.main:add(N{ dock = "top", h = t.h, layout = "fill" })
  sb.title:add(N{ kind = "ninegrid", image = R(t.image) })
  sb.options = sb.title:add(N{ kind = "button", layout = "dock", images = { normal = R(t.normal), hot = R(t.hot), pressed = R(t.pressed) } })
  local gp = t.glyph_pad or {0, 0, 0, 0}
  local gw = gp[1] + t.glyph_w + gp[3]
  local slot = sb.options:add(N{ dock = "right", w = gw, layout = "canvas" })
  sb.options_glyph = slot:add(N{ kind = "image", image = R(t.glyph), x = gp[1], y = math.floor((t.h - t.glyph_h) / 2), w = t.glyph_w, h = t.glyph_h })
  sb.options.on_click = function(self, button) if button == 1 then lh._emit("sidebar-options") end end
end
-- SidebarSizer (the Appbar's second DockPanel, over everything): TileThumb Width 2 (Aero, image 1201 Hidden: the visible edge
-- is 1178's columns 0-1) or 6 (Slate, 1108 visible), Dock Left on the right-docked (Flipped) bar, Cursor SizeWE. Dragging it
-- sets the width live (_OnSizerDrag: width - HorizontalChange, message 1457; _ValidateSize clamps 100..600); the struts,
-- _NET_WORKAREA, the taskbar and the tiles follow; the width is kept in state.ini [sidebar] width on release.
function sb.add_sizer()
  local sz = T.sidebar.sizer or { w = 2 }
  local slot = sb.root:add(N{ layout = "dock" })
  sb.sizer = slot:add(N{ kind = sz.image and "image" or nil, image = sz.image and R(sz.image) or nil, dock = "left", w = sz.w, hoverable = true, cursor = "sizewe" })
  sb.sizer.on_press = function(self, button) if button == 1 and not cfg.sb_locked then sb.drag_w = sb.w end end
  sb.sizer.on_drag = function(self, dx, dy) if sb.drag_w then lh.sidebar_set_width(sb.drag_w - dx) end end
  sb.sizer.on_release = function() if sb.drag_w then sb.drag_w = nil; S.set("sidebar", "width", tostring(sb.w)) end end
end
build_sidebar()

-- tile chrome (tile.xaml / outertile.xaml): Border(1,0,0,0) > FillPanel TileUI > DockPanel { side strip 9px (arrow 1173/1174, restore 1058, close 1061 on hover), top pad 3, right pad 6, foreground } ; 4px sizer strip 1199/1200 at the bottom.
-- The right-docked sidebar is Flipped (Appbar.StuckPlace, 2.4.1), so TileSideAreaBack docks Right and TileForegroundPadder Left
-- (2.3.3): the arrow sits at the tile's top-right as in 4051-vm-now.png, and the foreground starts 7 px in (1 + 6), which is
-- where the 4074 Slate power tile's battery starts (lh4074-181415.png, x = 1010 on a sidebar at 1003). Foreground 133 x (h - 7).
local plex_tile_chrome
local function tile_chrome(tile)
  if T.tile_chrome then return plex_tile_chrome(tile) end
  local outer = N{ h = tile.height, layout = "dock" }
  local sizer = outer:add(N{ kind = "button", dock = "bottom", h = 4, margin = {0, 2, 0, 0}, images = { normal = R(1199), hot = R(1200) } })  -- OuterContentHostSizer
  local body = outer:add(N{ dock = "fill", layout = "fill", clip = true })
  if T.tile_bg then body:add(N{ kind = "ninegrid", image = R(T.tile_bg) }) end                     -- Slate TileBackground (1000) ; Aero has none
  local ui = body:add(N{ layout = "dock", margin = {1, 0, 0, 0} })                                  -- Border(1,0,0,0) > TileUI
  tile.ui = ui
  local side = ui:add(N{ dock = "right", w = 9, layout = "vbox", hoverable = true })               -- TileSideAreaBack > TileSideButton Width 9 (Flipped → Right)
  tile.arrow = side:add(N{ kind = "image", image = R(1173), w = 9, h = 10, margin = {0, 2, 1, 0} })
  tile.restore = side:add(N{ kind = "button", w = 8, h = 8, visible = false, images = { normal = R(1058), hot = R(1059) }, on_click = function() lh._emit("tile-restore", tile) end })
  tile.close = side:add(N{ kind = "button", w = 8, h = 8, visible = false, images = { normal = R(1061), hot = R(1062) }, on_click = function() lh._emit("tile-close", tile) end })
  ui:add(N{ dock = "top", h = 3 }); ui:add(N{ dock = "left", w = 6 })                              -- TileForegroundPadder2 / TileForegroundPadder (Flipped → Left)
  tile.foreground = ui:add(N{ dock = "fill", layout = "fill", hoverable = tile.flyout ~= nil })
  -- DragButton#TileContent: a click on a tile with a flyout opens it (InnerContentHost.SendShowFlyout, 2.4.4)
  if tile.flyout then tile.foreground.on_click = function(self, button) if button == 1 then lh.toggle_flyout(tile) end end end
  side.on_hover = function(self, over) tile.arrow:set{ image = R(over and 1174 or 1173) }; tile.restore:set{ visible = over }; tile.close:set{ visible = over } end
  -- the side strip is part of DragButton#TileContent: a click opens the tile's flyout like the body; without a flyout the tile
  -- menu (the arrow is the tile's only "options" glyph; right click anywhere on the tile gives the menu too)
  side.on_click = function(self, button) if button == 1 then if tile.flyout then lh.toggle_flyout(tile) else lh._emit("tile-menu", tile) end end end
  sizer.on_drag = function(self, dx, dy) end
  tile.node = outer; tile.body = body
  ui:set{ opacity = 0 }; lh.animate(ui, "opacity", 1, 800)                                       -- TileUI fade-in 800 ms (2.4)
  return outer
end

-- pack key tile_chrome = { sizer, sizer_hot, sizer_h, sizer_grid, back, side_w, side, side_hot, side_pressed, fade_in, fade_out }:
-- 4042 main's tile (Application1.Tile, System.Windows.Desktop.Tile/OuterTile, Sidebarstylesheet_themed): TileBackgroundImage =
-- ThemeImage(Sidebar, TILEBACK) stretched under everything; the content over the whole tile; TileFadeButton Width 16, Dock Right on
-- the Flipped bar, its FillAlphaPresenter opacity animated to 1 in 300 ms when the pointer is within the tile and back to 0 in
-- 500 ms (Tile._OnPropertyChanged, PointerFocusWithinWorkaround), holding TileSideButton = TILEHEADERBACK normal / hot, pressed
-- TILECHEVRONBACK pressed (the tile menu); restore / close are 0x0 except for RMA tiles (none here); OuterTile's TileSizer
-- TileThumb Height 7, Dock Bottom, TILESIZER normal (NineGridMargin 1,0,0,0) / hot. No top or side padding: a tile's own
-- content keeps clear of the strip (tile:fgw() = width - 16).
plex_tile_chrome = function(tile)
  local c = T.tile_chrome
  local outer = N{ h = tile.height, layout = "dock" }
  local sizer = outer:add(N{ kind = "button", dock = "bottom", h = c.sizer_h, grid = c.sizer_grid, images = { normal = R(c.sizer), hot = R(c.sizer_hot) } })
  local body = outer:add(N{ dock = "fill", layout = "fill", clip = true, hoverable = true })
  body:add(N{ kind = "ninegrid", image = R(c.back) })
  tile.foreground = body:add(N{ layout = "fill", hoverable = tile.flyout ~= nil })
  if tile.flyout then tile.foreground.on_click = function(self, button) if button == 1 then lh.toggle_flyout(tile) end end end
  local over = body:add(N{ layout = "dock" })
  local side = over:add(N{ kind = "button", dock = "right", w = c.side_w, opacity = 0, layout = "dock", images = { normal = R(c.side), hot = R(c.side_hot), pressed = R(c.side_pressed) } })
  local icon = c.icons and c.icons[tile.id]                       -- TileHeaderIcon 16x16 Dock Top: BaseTile.Icon (the clock: imageres 1016)
  if icon then side:add(N{ kind = "image", image = R(icon), dock = "top", w = 16, h = 16 }) end
  side.on_click = function(self, button) if button == 1 then if tile.flyout then lh.toggle_flyout(tile) else lh._emit("tile-menu", tile) end end end
  body.on_within = function(self, inside) lh.animate(side, "opacity", inside and 1 or 0, inside and (c.fade_in or 300) or (c.fade_out or 500)) end
  tile.ui = body; tile.side = side
  sizer.on_drag = function(self, dx, dy) end
  tile.node = outer; tile.body = body
  return outer
end
-- Tile API (mirrors BaseTile, SHELL-DESIGN 2.5): Tile{ name, height, pinned, initialize = function(self) ... end }
Tile = {}; Tile.__index = Tile
function Tile.new(def) local t = setmetatable(def, Tile); return t end
sb.tile_list = {}
function sb.add_tile(def)
  local t = Tile.new(def); local chrome = tile_chrome(t)
  if t.pinned or not sb.tiles then sb.pinned:add(chrome) else sb.tiles:add(chrome) end
  table.insert(sb.tile_list, t)
  if t.initialize then t:initialize() end
  sb.surface:damage(); return t
end
lh.on("tile-close", function(tile) if lh.flyout_tile == tile then lh.close_flyout() end tile.node:remove(); tile.closed = true; if tile.dispose then tile:dispose() end sb.surface:damage(); if lh.publish_clock then lh.publish_clock() end end)
-- resize to a new height: 400 ms ease-in-out, first size applied directly (2.4.4); min 30
function sb.resize_tile(tile, h, now)
  h = math.max(30, math.floor(h)); if tile.height == h and not now then return end
  tile.height = h
  if now or not tile.node then if tile.node then tile.node:set{ h = h }; tile.node.h = h end return end
  lh.animate(tile.node, "h", h, 400, "easeinout")
end
-- tile rect in root coordinates (flyout placement)
function sb.tile_rect(tile) local x, y, w, h = tile.node:bounds(); return sb.geo.x + x, sb.geo.y + y, w, h end

-- maximised-window black layers: SetMaximizedWindow animates the taskbar and sidebar layers to 1 / 0 in 400 ms (2.4.3).
-- The desktop layer switches at once both ways: on = the X window background is set to opaque
-- black before the map, so the server paints it black in the same request stream as the map (no transparent first frame,
-- no fade), off = unmapped at once. A 400 ms fade of a full-screen surface comes late: every frame re-composites and
-- re-blurs the whole screen in picom (xrender), ~230 ms per frame on the lab.
function lh.set_maximized(on)
  if on == black_on then return end
  black_on = on
  if not dk or not cfg.desktop_black then   -- default: the desktop layer stays unmapped (its future Aurora role only)
  elseif on then
    dk.black.opacity = 1; dk.black:set{ opacity = 1 }
    dk.surface:background("#ff000000"); dk.surface:lower(); dk.surface:show()   -- lower first: mapping keeps the stacking position
  else
    dk.surface:hide(); dk.surface:background(nil); dk.black.opacity = 0; dk.black:set{ opacity = 0 }
  end
  -- [shell] max_fade_ms (default 0): 0 = the layers switch in the same repaint (lh.animate with 0 ms cancels a running fade and
  -- sets the value at once); otherwise SetMaximizedWindow's fade (400 ms in 4074)
  lh.animate(tb.black, "opacity", on and 1 or 0, cfg.max_fade_ms); lh.animate(sb.black, "opacity", on and 1 or 0, cfg.max_fade_ms)
end
-- the native trigger is the foreground window being maximised (CTray::HandleFullScreenApp / _FindRudeApp): the active
-- window when there is one, otherwise any visible window on the current desktop. luxwm computes exactly this and publishes
-- it as _LHWM_MAXIMIZED_MODE (so its square-cornered frames and these layers switch together); the EWMH rule below is the
-- fallback under another WM (single monitor; _NET_WM_STATE_MAXIMIZED_VERT+HORZ while a window fills the work area)
local function update_black()
  local mm = lh.maxmode()
  if mm ~= nil then lh.set_maximized(mm) return end
  local desk, active, any = lh.desktop(), nil, false
  for _, w in ipairs(lh.windows()) do
    if w.active and not w.hidden then active = w end
    if not w.hidden and (w.desktop == desk or w.desktop == -1 or w.desktop == 0xFFFFFFFF) and (w.maximized or w.fullscreen) then any = true end
  end
  if active then lh.set_maximized(active.maximized or active.fullscreen) else lh.set_maximized(any) end
end
lh.on("windows", update_black); lh.on("window", update_black); lh.on("desktop", update_black); lh.on("maxmode", update_black)

------------------------------------------------------------------ flyouts (flyout.lua) and toasts (toast.lua), SHELL-DESIGN 2.3.3 / 2.4.5
local ctx = { R = R, T = T, sb = sb, tb = tb, screen_w = screen_w, screen_h = screen_h, img = img }
lua("flyout.lua")(ctx)
lua("toast.lua")(ctx)
ctx.apps = lua("apps.lua")          -- desktop entries, icons, MFU (start menu, 6.3)
lh.apps = ctx.apps                  -- the same for the tiles (tiles/quicklaunch.lua)
lua("tooltip.lua")(ctx)             -- lh.tooltip(node, text)
lua("notifylog.lua")(ctx)           -- lh.notifylog: the notification history (tiles/notifyhistory.lua)
lua("menu.lua")(ctx)                -- popup menus: All Programs cascade, taskbar context menu
-- The Start button and Super go to the start-menu provider, flxp-startmenu (flxp's XP start panel; started below), which owns
-- _LH_STARTMENU_S<screen>: _LHSHELL_START to its owner window, format 32, l[0] = flags (bit 0: toggle), l[1..4] = anchor x, y, w, h
-- in root coordinates (w or h 0: none). luxwm's Super broadcast has a timestamp in l[0] and no rect: "toggle, no anchor". There
-- is no start menu of our own (Longhorn's Avalon shell never had one); without a provider nothing opens.
local function our_anchor() if not lh.start_anchor then return nil end local x, y, w, h = lh.start_anchor(); if not x then return nil end return { x = x, y = y, w = w, h = h } end
local function forward_start(flags, a)
  local sel = lh.startmenu_selection or "_LH_STARTMENU_S0"
  local o = lh.selection_owner(sel)
  if not o then io.stderr:write("luxshell: start menu: nothing owns " .. sel .. " (flxp-startmenu is not running)\n") return end
  lh.send_message(o, "_LHSHELL_START", flags, a and a.x or 0, a and a.y or 0, a and a.w or 0, a and a.h or 0)
  lh._emit("start-forwarded", flags)   -- the Start button's pressed look while the provider's menu is open
end
lh.on("start-click", function() forward_start(1, our_anchor()) end)
lh.on("start-key", function(l0, x, y, w, h)
  local has = (w or 0) > 0 and (h or 0) > 0
  forward_start(has and ((l0 or 0) & 0xff) or 1, has and { x = x, y = y, w = w, h = h } or our_anchor())
end)
lua("settings.lua")(ctx)            -- settings flyout ("Control Panel"), run / search box

------------------------------------------------------------------ tiles
local tiles_dir = lh.lua_dir .. "/tiles"
local function load_tile(name)
  local ok, def = pcall(dofile, tiles_dir .. "/" .. name .. ".lua")
  if ok and type(def) == "table" then def.R = R; def.T = T; def.sb = sb; return def end
  io.stderr:write("luxshell: tile " .. name .. ": " .. tostring(def) .. "\n")
end
-- full: the pack's tiles (T.tiles); pinned: only T.pinned_tiles (clock + notifications), all in the bottom block;
-- minimized: none in the sidebar, the same pinned tiles as compact views in the taskbar's TBContentHost (tile:taskbar(host))
local function pinned_names() return T.pinned_tiles or { "notifications", "clock" } end
-- the tile list: state.ini [sidebar] tiles (written by Move Up / Move Down, Remove ... Tile and the sidebar menu's parts list;
-- "none" = empty) as long as [sidebar] tiles_base still names the pack's own list it was made from (another pack, or the pack's
-- list edited, falls back to the pack's); an old state.ini without tiles_base: when it names the same tiles as the pack's
local function tile_names()
  local own = S.state_get("sidebar", "tiles")
  local base = T.tiles or { "clock" }
  if own then
    local t, seen = {}, {}
    for n in own:gmatch("[^;%s]+") do if n ~= "none" then table.insert(t, n); seen[n] = true end end
    if S.state_get("sidebar", "tiles_base") == table.concat(base, ";") then return t end
    local same = #t == #base; for _, n in ipairs(base) do if not seen[n] then same = false end end
    if same then return t end
  end
  return base
end
local function save_tile_names(names)
  S.state_set("sidebar", "tiles", #names > 0 and table.concat(names, ";") or "none")
  S.state_set("sidebar", "tiles_base", table.concat(T.tiles or { "clock" }, ";"))
end
local function build_tiles()
  for _, t in ipairs(sb.tile_list) do if t.dispose then pcall(t.dispose, t) end end   -- timers, event handlers of the old tiles
  sb.tile_list = {}
  if mode == "minimized" then return end
  for _, name in ipairs(mode == "pinned" and pinned_names() or tile_names()) do
    local def = load_tile(name)
    if def then
      def.id = name
      if mode == "pinned" then def.pinned = true
      elseif name == "clock" then def.pinned = cfg.clock_pinned end   -- [clock] pinned: 4074 bottom host or a 4051 tile in the list
      sb.add_tile(def)
    end
  end
end
tb.tile_list = {}
-- clock coordination (_ToggleClock, COMPOSITION.md 11): root _LHSHELL_CLOCK_VISIBLE (CARDINAL) = 1 while a clock tile of ours
-- is on screen (the sidebar's, or the minimized sidebar's compact clock in the Avalon taskbar), else 0; the classic taskbar hides
-- its own clock while it is 1. Deleted on exit (luxshell.c).
local clock_vis
local function publish_clock()
  local vis = false
  if mode ~= "minimized" and not sb_hidden() then for _, t in ipairs(sb.tile_list) do if t.id == "clock" and not t.closed then vis = true end end
  elseif tb_on() then for _, t in ipairs(tb.tile_list) do if t.id == "clock" then vis = true end end end
  if vis ~= clock_vis then clock_vis = vis; lh.set_root_cardinal("_LHSHELL_CLOCK_VISIBLE", vis and 1 or 0) end
end
lh.publish_clock = publish_clock
local function build_tb_tiles()
  for _, t in ipairs(tb.tile_list) do if t.dispose then pcall(t.dispose, t) end end
  tb.tile_list = {}; tb.tbhost:clear()
  -- the sidebar stacked under the windows: its clock may be covered, so the taskbar shows the time at its right end, as 4074's
  -- taskbar does with no clock of the sidebar's to show (the clock's taskbar view alone; minimized: all the pinned tiles)
  local names = (mode == "minimized" and tb.wide) and (T.taskbar_tiles or pinned_names()) or (cfg.sb_bottom and { "clock" }) or nil
  if not names or not tb_on() then tb.surface:damage(); publish_clock() return end
  for _, name in ipairs(names) do   -- pack key taskbar_tiles: 4074 Sidebar\TaskbarTiles
    local def = load_tile(name)
    if def and def.taskbar then def.id = name; def.tb = tb; def:taskbar(tb.tbhost); table.insert(tb.tile_list, def) end
  end
  tb.surface:damage(); publish_clock()
end
local function after_tiles()   -- the clock separator only above a non-empty pinned host
  if sb.clocksep then local any = false; for _, t in ipairs(sb.tile_list) do if t.pinned and not t.hidden then any = true end end; sb.clocksep:set{ visible = any } end
  publish_clock()
end
lh.on("tile-visibility", function() after_tiles(); if mode == "pinned" and lh.place_sidebar then lh.place_sidebar() end end)
build_tiles(); after_tiles()

------------------------------------------------------------------ live theme switch (SHELL-DESIGN 14)
-- New node trees for the taskbar and the sidebar on the same windows (the tray icons stay docked in the sidebar window and
-- move to the new tray node), surfaces resized when the pack's bar sizes differ (the struts follow, luxwm refits), tiles
-- disposed and created again, flyout chrome rebuilt (event "pack"). A toast on screen keeps its look until it closes.
local place_taskbar, place_sidebar
local function rebuild_shell()
  lh.close_flyout()
  -- the user's width (state.ini) survives a pack switch; the rows keep their count at the new pack's row height
  tb.row_h = T.taskbar.height; tb.h = tb.row_h * tb.rows
  sb.w = cfg.sidebar_w or T.sidebar.width; sidebar_w = sb.w
  cfg.clock_pinned = clock_pinned()        -- the new pack's default unless the user chose in this pack
  build_taskbar(); build_sidebar(); build_tiles(); after_tiles(); build_tb_tiles()
  place_taskbar(); place_sidebar(); rebuild_tasks()
  sb.surface:damage(); tb.surface:damage()
  if lh.update_start_button then lh.update_start_button() end   -- the new Start button: pressed if a menu is open
  lh._emit("pack")
end
------------------------------------------------------------------ bar placement, taskbar autohide, sidebar modes (SHELL-DESIGN 6.4, 7.3)
-- Taskbar autohide (Avalon, 2.4.2): the window keeps its size and is shifted so 2 + (h-2)*pct/100 px stay on screen; "focus
-- within" (pointer over the bar, the start menu, a menu or flyout of ours) moves pct by 20 every 20 ms (100 ms, linear); the
-- strut is the 2 px sliver while autohide is on, so the work area grows; switching autohide on jumps straight to 0/100.
-- _NET_WM_ICON_GEOMETRY keeps reporting the buttons where they are when the bar is shown (icon_offset).
-- another dock's bottom strut over these columns (the classic flxp-taskbar runs the full width at the bottom; 4051 layout:
-- the sidebar sits above it). Our own surfaces are not counted (lh.struts skips them), so in the avalon mode this is 0 and the
-- Avalon taskbar keeps yielding to the sidebar as before. [sidebar] yield_bottom = 0 turns it off.
local function foreign_bottom(x0, x1)
  if S.get("sidebar", "yield_bottom") == "0" then return 0 end
  local b = 0
  for _, st in ipairs(lh.struts()) do
    if st.bottom > 0 and st.bottom < screen_h / 2 and st.bottom_end_x >= x0 and st.bottom_start_x <= x1 then b = math.max(b, st.bottom) end
  end
  return b
end
-- the bottom space the sidebar leaves free: a foreign dock's strut, and with stacking = bottom our own taskbar's (its strut:
-- the 2 px sliver while autohide is on, so the sidebar reflows when the bar hides)
local function sb_yield()
  local y = foreign_bottom(screen_w - sb.w, screen_w - 1)
  if cfg.sb_bottom and tb_on() then y = math.max(y, cfg.autohide and 2 or tb.h) end
  return y
end
sb.yield = sb_yield()
local function tb_shown() return 0, screen_h - tb.h, tb_full() and screen_w or (screen_w - sidebar_w), tb.h end
-- the top of the bottom bar (start menu, flyouts, toasts sit above it): ours, or in the classic / none mode another dock's
-- full-width bottom strut (flxp-taskbar), else the screen's bottom
function lh.taskbar_shown_top() if tb_on() then return screen_h - tb.h end return screen_h - foreign_bottom(0, 0) end
place_taskbar = function()
  if not tb_on() then   -- classic / none: no strut, unmapped (the node tree stays for a switch back)
    if tb.placed ~= 0 then tb.surface.strut = 0; tb.placed = 0; tb.surface:move(tb.geo.x, tb.geo.y, tb.geo.w, tb.geo.h) end
    tb.surface:hide(); return
  end
  local x, y, w, h = tb_shown()
  local vis = cfg.autohide and math.floor(2 + (tb.h - 2) * tb.pct / 100 + 0.5) or tb.h
  tb.surface.strut = cfg.autohide and 2 or tb.h
  local ny = screen_h - vis
  if tb.geo.x ~= x or tb.geo.y ~= ny or tb.geo.w ~= w or tb.geo.h ~= h or tb.placed ~= tb.surface.strut then   -- move() also writes the strut
    local resized = tb.geo.w ~= w
    tb.geo = { x = x, y = ny, w = w, h = h }; tb.placed = tb.surface.strut
    tb.surface:move(x, ny, w, h)
    if resized then rebuild_tasks() end
  end
  tb.surface:icon_offset(0, y - ny)
  if lh.yield_soon then lh.yield_soon(0) end   -- the sidebar follows the taskbar's strut
end
local function ui_open() return (lh.menu and lh.menu.is_open()) or lh.flyout_tile ~= nil end
local function pointer_in_taskbar()
  local px, py = lh.pointer(); local x, y, w, h = tb_shown()
  return px >= x and px < x + w and py >= y and py < screen_h
end
local function focus_within() return pointer_in_taskbar() or ui_open() end
local function slide_to(pct)
  if tb.anim then lh.cancel(tb.anim); tb.anim = nil end
  if tb.pct == pct then return end
  tb.anim = lh.timer(function()
    tb.pct = tb.pct < pct and math.min(pct, tb.pct + 20) or math.max(pct, tb.pct - 20)
    place_taskbar()
    if tb.pct == pct and tb.anim then lh.cancel(tb.anim); tb.anim = nil end
  end, 20, true)
end
local function autohide_check() if cfg.autohide then slide_to(focus_within() and 100 or 0) end end
lh.timer(function() if cfg.autohide and tb_on() and tb.pct > 0 then autohide_check() end end, 100, true)   -- shown: poll focus-within
tb.surface.on_enter = function() if cfg.autohide and tb_on() then autohide_check() end end            -- hidden: the sliver re-shows it
-- The Start button shows StartButton.FakePressed (1181, the "checked" state) while the provider's start menu is open
-- (CLIENT-PROTOCOL "Start menu state"): the provider sets root _LH_STARTMENU_OPEN = 1 while its
-- menu is mapped (0 or deleted when closed). A provider that does not set it (the property never changed since the forward):
-- pressed from the forward (Start button or Super) until the first toplevel mapped within 1.5 s of it (its popup) unmaps, or
-- until the next forward (a click on the button while it shows pressed closes the provider's menu), or 1.5 s without a map.
local fwd       -- { t = ms, win = the popup } while a forward to a provider without the property is assumed open
local function start_pressed()
  local p = lh.root_prop("_LH_STARTMENU_OPEN")
  return p == 1 or (fwd ~= nil and p == nil) or false
end
local function update_start()
  if not tb.start then return end
  local on = start_pressed()
  if tb.start.checked ~= on then tb.start.checked = on; tb.start:set{ checked = on, state = on and "checked" or "normal" } end
end
lh.update_start_button = update_start
lh.on("startmenu-open", function() fwd = nil; update_start() end)   -- the provider speaks the protocol: the property decides
lh.on("start-forwarded", function()
  if fwd or lh.root_prop("_LH_STARTMENU_OPEN") == 1 then fwd = nil   -- shown pressed: this forward closes the provider's menu
  elseif lh.root_prop("_LH_STARTMENU_OPEN") == nil then
    local mine = { t = lh.now() }; fwd = mine
    lh.timer(function() if fwd == mine and not mine.win then fwd = nil; update_start() end end, 1500)
  end
  update_start()
end)
lh.on("toplevel", function(win, what)
  if not fwd or not win then return end
  if what == "map" and not fwd.win and lh.now() - fwd.t < 1500 then fwd.win = win
  elseif what ~= "map" and win == fwd.win then fwd = nil; update_start() end
end)

-- Sidebar modes: full (right edge, full height, right strut = the width, 150), pinned (the bottom block of pinned tiles; a *bottom* strut
-- of the block's height over the sidebar's columns, so the work area keeps the full width above it), minimized (unmapped,
-- no strut; the pinned tiles live in the taskbar, which runs the full width). Transitions slide/shrink in 400 ms
-- (ease-in-out, like the tile animations); the struts are set to their final value when a transition starts. With
-- [sidebar] stacking = bottom the sidebar reserves no strut in any mode (it is under the windows).
local function pinned_height()
  local h = 0; for _, t in ipairs(sb.tile_list) do if not t.hidden then h = h + (t.height or 0) end end
  return math.max(30, h)
end
local function sb_target(m)
  local Y = sb.yield or 0
  local strut = not cfg.sb_bottom
  if m == "pinned" then local ph = pinned_height(); return screen_w - sb.w, screen_h - Y - ph, sb.w, ph, "bottom", strut and ph + Y or 0 end
  if m == "minimized" then return screen_w, 0, sb.w, screen_h - Y, nil, 0 end
  return screen_w - sb.w, 0, sb.w, screen_h - Y, "right", strut and sb.w or 0
end
local function sb_move(x, y, w, h)
  sb.geo = { x = x, y = y, w = w, h = h }
  sb.surface:move(math.floor(x + 0.5), math.floor(y + 0.5), math.max(1, math.floor(w + 0.5)), math.max(1, math.floor(h + 0.5)))
end
place_sidebar = function()
  local x, y, w, h, dock, strut = sb_target(mode)
  sb.surface.dock, sb.surface.strut = dock, strut
  if mode == "minimized" or sb_hidden() then sb.surface.strut = 0; sb.surface.dock = nil; sb_move(x, y, w, h); sb.surface:hide() else sb_move(x, y, w, h); sb.surface:show(); sb_stack() end
end
lh.place_sidebar = function() place_sidebar() end
local function animate_sidebar(to, done)
  if sb.anim then lh.cancel(sb.anim); sb.anim = nil end
  local from = { sb.geo.x, sb.geo.y, sb.geo.w, sb.geo.h }; local t0 = lh.now(); local ms = 400
  local function step()
    local t = math.min(1, (lh.now() - t0) / ms); local e = t < 0.5 and 2 * t * t or 1 - (-2 * t + 2) ^ 2 / 2
    sb_move(from[1] + (to[1] - from[1]) * e, from[2] + (to[2] - from[2]) * e, from[3] + (to[3] - from[3]) * e, from[4] + (to[4] - from[4]) * e)
    if t >= 1 then if sb.anim then lh.cancel(sb.anim); sb.anim = nil end if done then done() end return true end
  end
  if not step() then sb.anim = lh.timer(step, 25, true) end
end
local function update_toggle() if tb.toggle then tb.toggle:set{ images = toggle_images() } end end
local function set_mode(m, instant)
  if m == mode or (m ~= "full" and m ~= "minimized" and m ~= "pinned") then return end
  local old = mode; mode = m; cfg.sidebar = m; S.set("sidebar", "mode", m)
  if old ~= "minimized" then cfg.last_mode = old end
  update_toggle()
  if sb_hidden() then   -- classic taskbar without the sidebar: remember the mode, show it with the Avalon taskbar again
    if sb.anim then lh.cancel(sb.anim); sb.anim = nil end
    tb.wide = m == "minimized"; build_sidebar(); build_tiles(); after_tiles(); place_sidebar(); return
  end
  local t0 = lh.now()
  if m == "minimized" then
    -- slide the sidebar out to the right, then the taskbar takes the full width with the pinned tiles at its right end
    local x, y, w, h = sb_target(m); sb.surface.strut = 0; sb.surface.dock = nil; sb_move(sb.geo.x, sb.geo.y, sb.geo.w, sb.geo.h)
    local function fin() sb.surface:hide(); build_tiles(); tb.wide = true; build_tb_tiles(); place_taskbar(); rebuild_tasks()
      io.stderr:write(string.format("luxshell: sidebar %s -> %s, %d ms\n", old, m, lh.now() - t0)) end
    if instant then fin() else animate_sidebar({ screen_w, sb.geo.y, sb.geo.w, sb.geo.h }, fin) end
    return
  end
  if old == "minimized" then tb.wide = false; build_tb_tiles(); place_taskbar(); rebuild_tasks() end
  build_sidebar(); build_tiles(); after_tiles()
  local x, y, w, h, dock, strut = sb_target(m)
  if old == "minimized" then sb_move(screen_w, y, w, h) end   -- slide in from the right edge
  sb.surface.dock, sb.surface.strut = dock, strut
  sb_move(sb.geo.x, sb.geo.y, sb.geo.w, sb.geo.h)               -- the strut now (final value)
  sb.surface:show(); sb_stack()
  local function fin() sb_move(x, y, w, h); io.stderr:write(string.format("luxshell: sidebar %s -> %s, %d ms\n", old, m, lh.now() - t0)) end
  if instant then fin() else animate_sidebar({ x, y, w, h }, fin) end
end
-- live sidebar width (SidebarSizer drag): applied at most every 25 ms (one Animator2 tick); luxwm refits maximised windows to
-- the new _NET_WORKAREA on each strut change, so fewer steps are kinder to it
function lh.sidebar_set_width(w)
  w = clamp(math.floor(w + 0.5), SB_MIN, SB_MAX)
  if w == sb.w then return end
  sb.w = w; sidebar_w = w; cfg.sidebar_w = w
  if sb.resize_timer then return end
  sb.resize_timer = lh.timer(function()
    sb.resize_timer = nil
    if mode ~= "minimized" then
      local x, y, ww, h, dock, strut = sb_target(mode)
      sb.surface.dock, sb.surface.strut = dock, strut
      sb_move(x, y, ww, h)
    end
    place_taskbar()
    lh._emit("sidebar-resize", sb.w)
    sb.surface:damage()
  end, 25)
end
-- taskbar rows (the top-edge drag): GetTaskbarHeight = 30 * rows; the strut and the work area follow
function lh.taskbar_set_rows(n)
  n = clamp(n, 1, ROWS_MAX)
  if n == tb.rows then return end
  tb.rows = n; cfg.rows = n; tb.h = tb.row_h * n
  if tb.toggle then tb.toggle:set{ y = toggle_y() } end
  place_taskbar(); rebuild_tasks()
  io.stderr:write(string.format("luxshell: taskbar %d row(s), %d px\n", n, tb.h))
end
-- tile context menu (right click on a tile; 2.4.4 "Move Up/Left", "Move Down/Right", plus the tile's own entries, e.g. the
-- clock's "Clock layout"); the order of the tile list is kept in state.ini [sidebar] tiles
local function tile_at(x, y)
  for _, t in ipairs(sb.tile_list) do
    if t.node then local bx, by, bw, bh = t.node:bounds(); if x >= bx and x < bx + bw and y >= by and y < by + bh then return t end end
  end
end
local function move_tile(t, dir)
  local names = {}; for _, n in ipairs(tile_names()) do table.insert(names, n) end
  local i; for k, n in ipairs(names) do if n == t.id then i = k end end
  if not i then return end
  -- step over pinned tiles (they live in the bottom host, not in the list's visible order)
  local j = i + dir
  while names[j] and names[j] == "clock" and cfg.clock_pinned do j = j + dir end
  if not names[j] then return end
  names[i], names[j] = names[j], names[i]
  save_tile_names(names)
  build_sidebar(); build_tiles(); after_tiles(); place_sidebar()
end

------------------------------------------------------------------ sidebar and tile context menus (Avalon ContextMenu, menu.lua "avalon")
-- Sources: lh4074-091929.png (the 4074 sidebar menu: "Minimize bar", "Properties", the parts list with checks, the transient
-- parts, "Clock") and the IL (res/4074/il/desktop.il): SidebarBaseMenu2.GetBaseContextMenu L17037 ("Minimize bar" / "Restore
-- bar" = ToggleMinimized, "Properties" = message 1379, the Taskbar and Start Menu Properties dialog, lh4074-091944.png),
-- SidebarControl.GetContextMenu L37721 (base items, separator, GetAddPartsMenu: Friendly Names, " (Multi-Instance)", then the
-- transient parts with " (Transient)", separator, "Clock" checked while the clock tile is shown), SidebarControl._OnOptions
-- L38583 (the Options button opens the same menu under itself), OuterTile._GetContextMenu L44524 (the tile's own items from
-- FireContextMenuEvent first, then "Auto Size" (checked), "Move Up" (disabled on the first tile), "Move Down" (disabled on the
-- last), "<Friendly Name> Tile Properties" when the tile has a property page, "Remove <Friendly Name> Tile" unless pinned).
-- Our additions, in their own group: "Lock the Sidebar" ([sidebar] locked) and "Stacking" ([sidebar] stacking).
-- Mnemonics are ours (the 4074 Avalon menus draw none); "&" marks them (menu.lua).
-- The 4074 module list as the screenshot shows it; `id` = the tiles/<id>.lua that implements it, the others are greyed.
local PARTS = {
  { "Basket" }, { "Classic Tray", id = "notifications" }, { "Contacts" }, { "News Feed" }, { "Notification History", id = "notifyhistory" },
  { "People nearby" }, { "Quick Launch", id = "quicklaunch" }, { "Search" }, { "Slide Show" }, { "Sync" },
}
local TRANSIENT = { { "Power (Transient)", id = "power" }, { "Web Host Tile (Transient)" } }
local FRIENDLY = { notifications = "Classic Tray", power = "Power", clock = "Clock", quicklaunch = "Quick Launch", notifyhistory = "Notification History" }   -- the modules' Friendly Name
local function friendly(t) return FRIENDLY[t.id] or t.name or t.id end
local KNOWN_ORDER = { "notifyhistory", "quicklaunch", "notifications", "power", "clock" }   -- 4074 SidebarTiles order
local function tile_shown(id) for _, n in ipairs(tile_names()) do if n == id then return true end end return false end
-- add (at its place in the pack's order, else before the next known tile) or remove a tile from the list; persisted
local function set_tile_shown(id, on)
  local names = {}; for _, n in ipairs(tile_names()) do if n ~= id then table.insert(names, n) end end
  if on then
    local ref, rank = {}, {}
    for _, n in ipairs(T.tiles or {}) do table.insert(ref, n) end
    for _, n in ipairs(KNOWN_ORDER) do local dup = false; for _, m in ipairs(ref) do if m == n then dup = true end end if not dup then table.insert(ref, n) end end
    for k, n in ipairs(ref) do rank[n] = k end
    local at = #names + 1
    for k, n in ipairs(names) do if (rank[n] or 1e9) > (rank[id] or 1e9) then at = k break end end
    table.insert(names, at, id)
  end
  save_tile_names(names)
  if lh.flyout_tile and lh.flyout_tile.id == id then lh.close_flyout() end
  if mode == "full" then build_sidebar(); build_tiles(); after_tiles(); place_sidebar() end
  io.stderr:write(string.format("luxshell: sidebar: %s tile %s (%s)\n", on and "added" or "removed", id, table.concat(names, ";")))
end
function lh.sidebar_set_locked(on)
  cfg.sb_locked = on and true or false; S.set("sidebar", "locked", cfg.sb_locked and "1" or "0")
  if not on then return end
  sb.drag_w = nil
end
local function sidebar_menu_items()
  local items = {
    { text = mode == "minimized" and "&Restore bar" or "&Minimize bar", action = function()
        if mode == "minimized" then set_mode(cfg.last_mode or "full") else set_mode("minimized") end end },
    { text = "&Properties", action = function() lh.settings_flyout(math.max(0, (sb.geo and sb.geo.x or screen_w) - 480)) end },
    { separator = true },
    { text = "&Lock the Sidebar", checked = cfg.sb_locked, action = function() lh.sidebar_set_locked(not cfg.sb_locked) end },
    { text = "&Stacking", submenu = function() return {
        { text = "&Top (above the windows)", radio = true, checked = not cfg.sb_bottom, action = function() lh.set_sidebar_stacking("top") end },
        { text = "&Bottom (part of the desktop)", radio = true, checked = cfg.sb_bottom and true or false, action = function() lh.set_sidebar_stacking("bottom") end },
      } end },
    { separator = true },
  }
  local full = mode == "full"
  local function part(p)
    local on = p.id and tile_shown(p.id) or false
    table.insert(items, { text = p[1], checked = on, disabled = not (p.id and full), action = function() set_tile_shown(p.id, not on) end })
  end
  for _, p in ipairs(PARTS) do part(p) end
  table.insert(items, { separator = true })
  for _, p in ipairs(TRANSIENT) do part(p) end
  table.insert(items, { separator = true })
  part({ "Clock", id = "clock" })
  return items
end
-- x, y: the click (root coordinates); the menu flips left/up where it does not fit (TrackPopupMenu, as lh4074-091929.png)
local function sidebar_menu(x, y)
  lh.close_flyout()
  lh.menu.open(sidebar_menu_items(), x, y, { style = "avalon", grab = true, flip = true })
end
lh.sidebar_menu = sidebar_menu
-- the Options button (SidebarControl._OnOptions: PlacementTarget = the button, the default Bottom placement): under the button,
-- right-aligned with the bar's right edge
lh.on("sidebar-options", function()
  if lh.menu.is_open() then lh.menu.close() return end
  local bx, by, bw, bh = sb.options:bounds()
  sidebar_menu(sb.geo.x + bx + bw, sb.geo.y + by + bh)
end)
local function tile_index(t)   -- position among the visible tiles of its host (the list or the pinned block), and their count
  local i, n = nil, 0
  for _, x in ipairs(sb.tile_list) do
    if not x.closed and not x.hidden and (x.pinned and true or false) == (t.pinned and true or false) then n = n + 1; if x == t then i = n end end
  end
  return i or 1, n
end
local function tile_menu(t, x, y)
  local items = {}
  if t.menu then for _, it in ipairs(t:menu()) do table.insert(items, it) end end   -- FireContextMenuEvent: the tile's own first
  if #items > 0 then table.insert(items, { separator = true }) end
  local i, n = tile_index(t)
  local movable = mode == "full" and not t.pinned
  -- Auto Size: our tiles always size to their content (the sizer strip does not drag yet), so it is shown checked and greyed
  table.insert(items, { text = "&Auto Size", checked = true, disabled = true })
  table.insert(items, { text = "Move &Up", disabled = not movable or i <= 1, action = function() move_tile(t, -1) end })
  table.insert(items, { text = "Move &Down", disabled = not movable or i >= n, action = function() move_tile(t, 1) end })
  local props = t.properties and function() t:properties() end or t.flyout and function() lh.open_flyout(t) end
  table.insert(items, { text = friendly(t) .. " Tile &Properties", disabled = props == nil, action = props })
  if not t.pinned then
    table.insert(items, { text = "&Remove " .. friendly(t) .. " Tile", disabled = mode ~= "full", action = function() set_tile_shown(t.id, false) end })
  end
  lh.close_flyout()
  lh.menu.open(items, x, y, { style = "avalon", grab = true, flip = true })
end
lh.on("tile-menu", function(t)   -- a click on the tile's arrow (no flyout): under the arrow
  local a = t.arrow or t.side             -- the 4074 arrow, or the 4042-plex side strip (its top left corner)
  if not a then return end
  local ax, ay, aw, ah = a:bounds()
  if not t.arrow then aw, ah = 0, 0 end
  tile_menu(t, sb.geo.x + ax + aw, sb.geo.y + ay + ah)
end)
lh.on("surface-press", function(x, y, button, win)
  if button ~= 3 or win ~= sb.surface:id() or mode == "minimized" or sb_hidden() then return end
  local t = tile_at(x, y)
  if t then tile_menu(t, sb.geo.x + x, sb.geo.y + y) else sidebar_menu(sb.geo.x + x, sb.geo.y + y) end
end)
-- [clock] pinned: the clock between the 4074 pinned host (bottom) and the 4051 tile list
lh.on("clock-pinned", function(on)
  cfg.clock_pinned = on and true or false; S.set("clock", "pinned", on and "1" or "0"); S.set("clock", "pinned_pack", pack_key())
  if mode ~= "full" then return end
  build_sidebar(); build_tiles(); after_tiles(); place_sidebar()
end)
-- the pinned block follows its tiles' heights (the tray tile grows by rows, 400 ms)
lh.on("tray", function() if mode == "pinned" then lh.timer(function() if mode == "pinned" and not sb.anim then local x, y, w, h, dock, strut = sb_target("pinned"); sb.surface.strut = strut; animate_sidebar({ x, y, w, h }) end end, 450) end
  if mode == "minimized" then lh.timer(rebuild_tasks, 0) end end)
-- TaskbarToggleBar (_ToggleSidebar, 2.4.2): full/pinned ⇄ minimized
lh.on("toggle-sidebar", function() if mode == "minimized" then set_mode(cfg.last_mode or "full") else set_mode("minimized") end end)

function lh.shell_get(k)
  if k == "autohide" then return cfg.autohide elseif k == "sidebar" then return mode elseif k == "wallpaper" then return S.get("desktop", "wallpaper") ~= "none"
  elseif k == "taskbar" then return cfg.taskbar
  elseif k == "sidebar_with_classic" then return cfg.with_classic
  elseif k == "sidebar_stacking" then return cfg.sb_bottom and "bottom" or "top" end
end
function lh.shell_set(k, v)
  if k == "autohide" then
    cfg.autohide = v and true or false; S.set("taskbar", "autohide", cfg.autohide and "1" or "0")
    if tb.anim then lh.cancel(tb.anim); tb.anim = nil end
    tb.pct = (not cfg.autohide or focus_within()) and 100 or 0      -- switched on: jump to 0/100 at once (2.4.2)
    place_taskbar()
  elseif k == "sidebar" then set_mode(v)
  elseif k == "taskbar" then lh.set_taskbar(v)
  elseif k == "sidebar_with_classic" then lh.set_sidebar_with_classic(v)
  elseif k == "sidebar_stacking" then lh.set_sidebar_stacking(v)
  elseif k == "wallpaper" then
    if v then S.unset("desktop", "wallpaper") else S.set("desktop", "wallpaper", "none") end   -- per user; the resolver applies it
    theme_entry = S.resolve(theme_name) or theme_entry
    set_wallpaper(theme_entry); publish_resolved(theme_entry)
  end
end

-- the Avalon taskbar's context menu (right click on its empty part). ExtendedTaskbar.GetContextMenu (desktop.il L23627): the
-- sidebar's base items ("Minimize bar"/"Restore bar", "Properties"), separator, "Task Manager", separator, "Show Desktop" /
-- "Hide Desktop" (_fDesktopVisible), "Cascade", "Tile Vertically", "Tile Horizontally" (enabled with windows open). No screenshot
-- of it exists; the wording is the IL's. 4074 passes constants to the base items, so its taskbar always said "Restore bar";
-- ours follows the sidebar's state. Then luxshell's own group: autohide and the sidebar mode.
local function work_rect()   -- the area the windows get (our struts; another dock's bottom strut in the classic mode)
  local r = screen_w
  if not sb_hidden() and mode ~= "minimized" and not cfg.sb_bottom and sb.geo then r = sb.geo.x end
  local b = tb_on() and (screen_h - (cfg.autohide and 2 or tb.h)) or (screen_h - foreign_bottom(0, screen_w))
  return 0, 0, r, b
end
local function arrangeable()
  local out, desk = {}, lh.desktop()
  for _, w in ipairs(lh.windows()) do
    if not w.hidden and not w.fullscreen and (w.desktop == nil or w.desktop == desk or w.desktop == -1 or w.desktop == 0xFFFFFFFF) then table.insert(out, w) end
  end
  return out
end
-- Cascade (26 px steps, 60 % of the work area) / Tile Vertically (side by side) / Tile Horizontally (one above the other); more
-- than three windows go into a grid (Win32 TileWindows)
local function arrange(how)
  local ws = arrangeable(); local n = #ws; if n == 0 then return end
  local x0, y0, x1, y1 = work_rect(); local W, H = x1 - x0, y1 - y0
  if how == "cascade" then
    local w, h, step = math.floor(W * 0.6), math.floor(H * 0.6), 26
    for i, win in ipairs(ws) do
      local k = (i - 1) % math.max(1, math.floor((H - h) / step) + 1)
      lh.wm_moveresize(win.id, x0 + k * step, y0 + k * step, w, h); lh.activate(win.id)
    end
    return
  end
  local cols, rows
  if n <= 3 then if how == "vertical" then cols, rows = n, 1 else cols, rows = 1, n end
  else cols = math.ceil(math.sqrt(n)); rows = math.ceil(n / cols); if how == "horizontal" then cols, rows = rows, cols end end
  for i, win in ipairs(ws) do
    local c, r = (i - 1) % cols, math.floor((i - 1) / cols)
    local x, y = x0 + math.floor(c * W / cols), y0 + math.floor(r * H / rows)
    lh.wm_moveresize(win.id, x, y, x0 + math.floor((c + 1) * W / cols) - x, y0 + math.floor((r + 1) * H / rows) - y)
  end
end
local taskmgr_cmd
local function task_manager()
  if taskmgr_cmd == nil then
    taskmgr_cmd = false
    -- io.popen, not os.execute: SIGCHLD is ignored in luxshell, so system()'s status is -1 whatever the command did
    local function have(c) local f = io.popen("command -v " .. c .. " 2>/dev/null"); if not f then return false end local o = f:read("*l"); f:close(); return o ~= nil and o ~= "" end
    for _, c in ipairs{ "xfce4-taskmanager", "lxtask", "gnome-system-monitor", "mate-system-monitor", "qps" } do
      if have(c) then taskmgr_cmd = c break end
    end
    if not taskmgr_cmd and have("xterm") then taskmgr_cmd = "xterm -title 'Task Manager' -e top" end
  end
  return taskmgr_cmd or nil
end
local function taskbar_menu(px, py)
  local showing = (tonumber(lh.root_prop("_NET_SHOWING_DESKTOP") or 0) or 0) ~= 0
  local nwin = #arrangeable()
  local tm = task_manager()
  lh.close_flyout()
  lh.menu.open({
    { text = mode == "minimized" and "&Restore bar" or "&Minimize bar", disabled = sb_hidden(), action = function()
        if mode == "minimized" then set_mode(cfg.last_mode or "full") else set_mode("minimized") end end },
    { text = "&Properties", action = function() lh.settings_flyout(px) end },
    { separator = true },
    { text = "Tas&k Manager", disabled = tm == nil, action = function() lh.spawn(tm) end },
    { separator = true },
    { text = showing and "&Hide Desktop" or "&Show Desktop", action = function() lh.send_root_message("_NET_SHOWING_DESKTOP", showing and 0 or 1) end },
    { text = "&Cascade", disabled = nwin == 0, action = function() arrange("cascade") end },
    { text = "Tile &Vertically", disabled = nwin == 0, action = function() arrange("vertical") end },
    { text = "Tile Hori&zontally", disabled = nwin == 0, action = function() arrange("horizontal") end },
    { separator = true },
    { text = "A&uto-hide the taskbar", checked = cfg.autohide, action = function() lh.shell_set("autohide", not cfg.autohide) end },
    { text = "Side&bar", submenu = function() return {
        { text = "&Full", radio = true, checked = mode == "full", action = function() set_mode("full") end },
        { text = "&Minimized", radio = true, checked = mode == "minimized", action = function() set_mode("minimized") end },
        { text = "&Pinned tiles only", radio = true, checked = mode == "pinned", action = function() set_mode("pinned") end },
      } end },
  }, px, py, { style = "avalon", grab = true, up = true })
end
lh.taskbar_menu = taskbar_menu
lh.on("surface-press", function(x, y, button, win)
  if button ~= 3 or win ~= tb.surface:id() then return end
  for _, b in ipairs(tb.task_nodes or {}) do local bx, by, bw, bh = b:bounds(); if x >= bx and x < bx + bw and y >= by and y < by + bh then return end end
  taskbar_menu(tb.geo.x + x, tb.geo.y + y)
end)

------------------------------------------------------------------ the classic taskbar, tray and start-menu coordination
-- Processes of this display by name: /proc/PID/comm is NAME and its environment has our DISPLAY (nothing else is touched).
local function display_pids(name)
  local out, me = {}, os.getenv("DISPLAY") or ""
  for _, d in ipairs(lh.list_dir("/proc") or {}) do
    if d:match("^%d+$") then
      local f = io.open("/proc/" .. d .. "/comm"); local comm = f and f:read("l"); if f then f:close() end
      if comm == name:sub(1, 15) then
        local e = io.open("/proc/" .. d .. "/environ"); local env = e and e:read("a") or ""; if e then e:close() end
        for kv in env:gmatch("[^%z]+") do if kv == "DISPLAY=" .. me then table.insert(out, tonumber(d)) break end end
      end
    end
  end
  return out
end
lh.display_pids = display_pids
local function pid_is(pid, name)   -- pid still runs NAME on our display (pids are reused; SIGCHLD is ignored, so no wait)
  for _, p in ipairs(display_pids(name)) do if p == pid then return true end end
  return false
end
-- The classic taskbar: [commands] taskbar (default flxp-taskbar; $LHSHELL_TASKBAR_CMD), run with our DISPLAY and session bus
-- (inherited) and its log in $XDG_RUNTIME_DIR. It follows _LH_THEME by itself (flxp's WM_THEMECHANGED; no --theme). It may take
-- the tray over from us (FLXP_TRAY_TAKEOVER=1: our notifications tile then hides, tiles/notifications.lua). Start menu:
-- [taskbar] classic_start_menu = flxp (default; FLXP_START_MENU=panel: XP's start panel of flxp-taskbar; it owns
-- _LH_STARTMENU_S<screen>, so we forward Super to it, and its own Start button opens it directly) | flxp-classic (flxp's classic
-- popup menu, FLXP_START_MENU=classic).
local classic = {}
local function classic_command()
  local c = S.get("commands", "taskbar") or os.getenv("LHSHELL_TASKBAR_CMD")
  return (c and c ~= "") and c or lh.prog("flxp-taskbar")
end
local function classic_name() return (classic_command():gsub("'", ""):match("^%s*(%S+)") or "flxp-taskbar"):match("([^/]+)$") end
local function classic_menu() return S.get("taskbar", "classic_start_menu") == "flxp-classic" and "flxp-classic" or "flxp" end
-- flxp-taskbar processes of this display: the bar, or one run by hand as the menu-only provider (/proc/PID/cmdline has
-- --start-menu-only; the provider we start is flxp-startmenu, a name of its own)
local function menu_only(pid)
  local f = io.open("/proc/" .. pid .. "/cmdline"); local c = f and f:read("a") or ""; if f then f:close() end
  for a in c:gmatch("[^%z]+") do if a == "--start-menu-only" then return true end end
  return false
end
local function flxp_pids(want_menu_only, name)
  local out = {}
  for _, p in ipairs(display_pids(name or classic_name())) do if menu_only(p) == want_menu_only then table.insert(out, p) end end
  return out
end
-- a process that held _LH_STARTMENU_S must have let go before the next flxp-taskbar starts: one that finds the selection
-- owned leaves it alone (flxp NOTES "start-menu provider"), so a switch would end with no provider
local function when_provider_free(fn)
  local tries = 0
  local function try()
    tries = tries + 1
    if not lh.selection_owner(lh.startmenu_selection) or tries > 30 then fn() return true end
  end
  if try() then return end
  local id; id = lh.timer(function() if try() then lh.cancel(id) end end, 100, true)
end
local function classic_start()
  local name = classic_name()
  if classic.pid and pid_is(classic.pid, classic.name or name) then return end
  local running = flxp_pids(false, name)
  if #running > 0 then classic.pid, classic.name = running[1], name; io.stderr:write("luxshell: taskbar: " .. name .. " already runs (" .. running[1] .. ")\n") return end
  -- hand the tray over before it starts (a takeover races: icons answering its MANAGER while we still hand them back were lost
  -- by flxp-taskbar's ReparentNotify handling); our tile stays hidden while it starts and claims again if nobody took the tray
  if lh.tray_yield then lh.tray_yield(10000) end
  local menu = classic_menu()
  local env = { FLXP_TRAY_TAKEOVER = "1", FLXP_START_MENU = (menu == "flxp-classic" and "classic" or "panel"), FLXP_START_KEY = "1" }
  local log = (os.getenv("XDG_RUNTIME_DIR") or "/tmp") .. "/luxshell-taskbar" .. (os.getenv("DISPLAY") or ""):gsub("[^%w]", "") .. ".log"
  classic.pid = lh.spawn_pid(classic_command() .. " >" .. S.quote(log) .. " 2>&1 </dev/null", env); classic.name, classic.menu = name, menu
  io.stderr:write(string.format("luxshell: taskbar: started %s (pid %s, start menu %s, log %s)\n", classic_command(), tostring(classic.pid), classic_menu(), log))
end
local function classic_stop()
  local pid, name = classic.pid, classic.name or classic_name(); classic.pid = nil
  if lh.tray_unyield then lh.tray_unyield() end
  if pid and pid_is(pid, name) then io.stderr:write("luxshell: taskbar: stopping " .. name .. " " .. pid .. "\n"); lh.kill(pid) end
end

-- Without the classic bar (taskbar avalon or none) the start menu is flxp-startmenu (flxp-taskbar's start panel without the bar,
-- tray or struts; it owns _LH_STARTMENU_S<screen>, we forward our Start button and Super to it with the button's rect). Started,
-- adopted (one already running on this display, or a flxp-taskbar --start-menu-only run by hand), restarted when it dies (at
-- most 5 times a minute, then nothing opens until the settings change), stopped on a switch to the classic bar (the provider
-- then) and on exit; only ever its own pid, checked against /proc (name, DISPLAY).
local PROVIDER = "flxp-startmenu"
local provider = { starts = {} }
local function provider_wanted() return cfg.taskbar ~= "classic" end
local function provider_running() return provider.pid and pid_is(provider.pid, provider.name or PROVIDER) end
local function provider_stop()
  local pid, name = provider.pid, provider.name or PROVIDER; provider.pid = nil
  if pid and pid_is(pid, name) then io.stderr:write("luxshell: start menu: stopping " .. name .. " " .. pid .. "\n"); lh.kill(pid) end
end
local function provider_start()
  if provider_running() or provider.pending then return end
  local name = PROVIDER
  local running = display_pids(PROVIDER)
  if #running == 0 then running = flxp_pids(true, classic_name()); if #running > 0 then name = classic_name() end end
  if #running > 0 then provider.pid, provider.name = running[1], name; io.stderr:write("luxshell: start menu: " .. name .. " already runs (" .. running[1] .. ")\n") return end
  local now = lh.now(); local recent = {}
  for _, t in ipairs(provider.starts) do if now - t < 60000 then table.insert(recent, t) end end
  provider.starts = recent
  if #recent >= 5 then
    if not provider.gave_up then provider.gave_up = true; io.stderr:write("luxshell: start menu: " .. name .. " died 5 times in a minute; no start menu until the settings change\n") end
    return
  end
  provider.pending = true
  when_provider_free(function()
    provider.pending = false
    if not provider_wanted() or provider_running() then return end
    table.insert(provider.starts, lh.now())
    local log = (os.getenv("XDG_RUNTIME_DIR") or "/tmp") .. "/luxshell-startmenu" .. (os.getenv("DISPLAY") or ""):gsub("[^%w]", "") .. ".log"
    provider.pid = lh.spawn_pid(lh.prog(PROVIDER) .. " >" .. S.quote(log) .. " 2>&1 </dev/null", { FLXP_START_MENU = "panel", FLXP_START_KEY = "1" })
    provider.name = PROVIDER
    io.stderr:write(string.format("luxshell: start menu: started %s (pid %s, log %s)\n", lh.prog(PROVIDER), tostring(provider.pid), log))
  end)
end
function lh.start_provider_check()
  if provider_wanted() then provider_start() elseif provider.pid then provider_stop() end
  -- the classic bar was started with the other start menu ([taskbar] classic_start_menu): start it again with this one
  if cfg.taskbar == "classic" and classic.pid and classic.menu and classic.menu ~= classic_menu() and pid_is(classic.pid, classic.name or classic_name()) then
    io.stderr:write("luxshell: taskbar: start menu " .. classic.menu .. " -> " .. classic_menu() .. ", restarting " .. (classic.name or classic_name()) .. "\n")
    classic_stop()
    when_provider_free(function() if cfg.taskbar == "classic" then classic_start() end end)
  end
end
function lh.start_provider_reset() provider.starts, provider.gave_up = {}, nil end
lh.timer(function() if provider_wanted() and not provider_running() then provider_start() end end, 3000, true)   -- died: again
lh.on("exit", function() if cfg.taskbar == "classic" then classic_stop() end provider_stop() end)
-- [sidebar] with_classic live (settings flyout, _LHSHELL_COMMAND "sidebar-with-classic 0|1")
function lh.set_sidebar_with_classic(on)
  S.set("sidebar", "with_classic", on and "1" or "0"); cfg.with_classic = on and true or false
  if sb.anim then lh.cancel(sb.anim); sb.anim = nil end
  build_sidebar(); build_tiles(); after_tiles(); place_sidebar(); publish_clock()
end
-- [sidebar] stacking live (Win+S = _LHSHELL_COMMAND "sidebar-stacking toggle|top|bottom", the settings flyout, settings.ini):
-- top | bottom | toggle. Instant, no animation: the sidebar's strut, geometry, _NET_WM_STATE (ABOVE/BELOW) and place in the stack
-- and the taskbar's width change in this one callback, painted in the same pass; windows are not moved (luxwm refits maximised
-- ones to the new work area as for any strut change). A running mode animation is finished at once. The order keeps every
-- in-between state the compositor might catch invisible: to bottom the taskbar first runs the full width (under the sidebar's
-- corner), to top the sidebar first runs the full height (under the taskbar); the restack comes last. (A server grab around
-- it was tried: on the xrender lab it froze picom halfway through a repaint, which showed for ~180 ms.)
-- switch the virtual desktop through the WM (EWMH _NET_CURRENT_DESKTOP request): a 0-based index, "next" or "prev" (wrapping
-- at _NET_NUMBER_OF_DESKTOPS); for _LHSHELL_COMMAND "desktop ..." and a future sidebar tile
function lh.switch_desktop(v)
  local n, cur = lh.root_prop("_NET_NUMBER_OF_DESKTOPS") or 1, lh.desktop()
  local d = v == "next" and (cur + 1) % n or v == "prev" and (cur - 1) % n or tonumber(v)
  if d and d >= 0 and d < n then lh.set_desktop(d) end
end
function lh.set_sidebar_stacking(v)
  if v == "toggle" then v = cfg.sb_bottom and "top" or "bottom" end
  if v ~= "top" and v ~= "bottom" then return end
  local on = v == "bottom"
  S.set("sidebar", "stacking", v)
  if on == cfg.sb_bottom then return end
  local t0 = lh.now()
  cfg.sb_bottom = on
  if sb.anim then lh.cancel(sb.anim); sb.anim = nil end
  sb.yield = sb_yield()
  local function sidebar() build_sidebar(); build_tiles(); after_tiles(); place_sidebar() end
  local function taskbar() build_tb_tiles(); place_taskbar(); rebuild_tasks() end
  if on then taskbar(); sidebar() else sidebar(); taskbar() end
  sb.surface:set_layer(sb_layer())
  if mode ~= "minimized" and not sb_hidden() then sb_stack() end   -- top: raised over the taskbar at the corner; bottom: lowered
  publish_clock()
  io.stderr:write(string.format("luxshell: sidebar stacking %s (%s), %d ms\n", v, on and "under the windows, no strut" or "above the windows, strut", lh.now() - t0))
end
-- apply_taskbar(avalon | classic | none): the live switch; lh.set_taskbar(kind) (settings flyout, _LHSHELL_COMMAND "taskbar KIND")
-- also stores it for the current theme like Display Properties: Shell.taskbar in the current layer (settings.ini [current],
-- dropped when it equals the theme's own value), not in the global [taskbar] kind
local function apply_taskbar(kind)
  if kind == cfg.taskbar then if kind == "classic" then classic_start() end lh.start_provider_check() return end
  local old = cfg.taskbar; cfg.taskbar = kind
  if old == "classic" then classic_stop() end
  if kind == "classic" then provider_stop() end
  lh.close_flyout()
  tb.placed = nil; tb.wide = mode == "minimized"
  build_tb_tiles(); place_taskbar(); rebuild_tasks()
  if tb_on() then tb.surface:show(); tb.surface:raise() end
  if sb.anim then lh.cancel(sb.anim); sb.anim = nil end
  place_sidebar()               -- [sidebar] with_classic = 0: gone with the classic bar, back in its mode with the Avalon one
  if kind == "classic" then when_provider_free(function() if cfg.taskbar == "classic" then classic_start() end end) end
  lh.start_provider_check()     -- after the classic bar went: the menu-only provider (it waits for the selection to be free)
  publish_clock()
  io.stderr:write("luxshell: taskbar " .. old .. " -> " .. kind .. "\n")
end
function lh.set_taskbar(kind)
  kind = taskbar_kind(kind)
  if not kind then return end
  local sel = S.set_theme_value(theme_entry, "taskbar", kind)   -- a theme key: the current layer
  apply_taskbar(kind)
  if sel then lh.select_theme(sel, true) end
end
-- the sidebar follows other docks' bottom struts live (luxwm rewrites _NET_WORKAREA on every strut change: the classic bar
-- appearing, going away, autohiding)
local function yield_check()
  local y = sb_yield()
  if y == sb.yield then return end
  io.stderr:write(string.format("luxshell: sidebar yields %d px to a bottom dock\n", y))
  sb.yield = y
  if sb.anim then lh.cancel(sb.anim); sb.anim = nil end
  place_sidebar()
end
local yield_pending
local function yield_soon(ms) if yield_pending then return end yield_pending = lh.timer(function() yield_pending = nil; yield_check() end, ms or 0) end
lh.yield_soon = yield_soon
lh.on("workarea", function() yield_soon(0) end)
lh.on("toplevel", function() yield_soon(100) end)
lh.on("sidebar-resize", function() lh.timer(yield_check, 0) end)

-- Tray coordination: one XEmbed tray per display. While another client owns _NET_SYSTEM_TRAY_S<screen> the notifications tile is
-- hidden (event "tray-owner"(true)); when that owner goes away it is claimed again ("tray-owner"(false)). Owner changes come from
-- XFixes (lh.watch_selection) or, without XFIXES, a 2 s poll.
local tray_sel = "_NET_SYSTEM_TRAY_S" .. (screen_no or 0)
local tray_foreign_now = false
local tray_yield_until = 0     -- lh.tray_yield: we gave the tray up for a starting classic bar; no reclaiming until then
local function tray_foreign()
  local o = lh.selection_owner(tray_sel)
  if o ~= nil and o ~= lh.tray_manager() then tray_yield_until = 0 return true end   -- the new tray is up: the yield is over
  return lh.now() < tray_yield_until
end
local function tray_check()
  local f = tray_foreign()
  if f ~= tray_foreign_now then tray_foreign_now = f; io.stderr:write("luxshell: tray: " .. (f and "left to another tray: " or "ours again: ") .. tray_sel .. "\n"); lh._emit("tray-owner", f) end
end
tray_foreign_now = tray_foreign()
function lh.tray_foreign() return tray_foreign_now end
if lh.watch_selection(tray_sel) then lh.on("selection", function(name) if name == tray_sel then tray_check() end end)
else lh.timer(tray_check, 2000, true) end
lh.on("tray-lost", function() lh.timer(tray_check, 0) end)
function lh.tray_unyield() tray_yield_until = 0; tray_check() end
function lh.tray_yield(ms)
  tray_yield_until = lh.now() + ms
  lh.tray_stop()                -- icons back to the root (unmapped), selection released; the new tray's MANAGER docks them there
  tray_check()
  lh.timer(tray_check, ms + 50)
end

-- Start-menu provider (SHELL-DESIGN 13): the provider owns _LH_STARTMENU_S<screen>; we forward the
-- Start button and Super to it (above). We own _LHSHELL_S<screen> while running, so a provider knows luxshell forwards luxwm's
-- root Super message and must not answer that broadcast itself.
lh.startmenu_selection = "_LH_STARTMENU_S" .. (screen_no or 0)
lh.own_selection("_LHSHELL_S" .. (screen_no or 0))
lh.watch_selection(lh.startmenu_selection)
-- the Avalon Start button's rect in root coordinates (the anchor sent to a provider), nil without our taskbar
function lh.start_anchor()
  if not tb_on() or not tb.start then return nil end
  local x, y, w, h = tb.start:bounds()
  return math.floor(tb.geo.x + x), math.floor(screen_h - tb.h + y), math.floor(w), math.floor(h)
end

------------------------------------------------------------------ the compositor follows the theme
-- On a theme change luxshell applies the theme's `composition`: off → stop the picom of this display (only processes named
-- picom whose environment has our DISPLAY; nothing else is touched), auto → start one when no compositor owns
-- _NET_WM_CM_S<screen>. luxwm already follows the compositor's presence (glass ⇄ frame_classic, _LHWM_COMPOSITED), so this is
-- the whole chain. The switch (per theme: the resolved Glass.composition, auto = follow) is
-- follow (the theme's `composition` decides) | on | off; it is applied at startup (1.5 s in: stops the picom the session may
-- have started for a classic theme, starts one for a glass theme when none runs), on every theme change and when it changes.
-- Command: settings.ini [commands] picom, else $LHSHELL_PICOM_CMD, else picom --config session/picom-glass.conf --backend glx.
local function picom_pids()
  local out, me = {}, os.getenv("DISPLAY") or ""
  for _, d in ipairs(lh.list_dir("/proc") or {}) do
    if d:match("^%d+$") then
      local f = io.open("/proc/" .. d .. "/comm"); local comm = f and f:read("l"); if f then f:close() end
      if comm == "picom" then
        local e = io.open("/proc/" .. d .. "/environ"); local env = e and e:read("a") or ""; if e then e:close() end
        for kv in env:gmatch("[^%z]+") do if kv == "DISPLAY=" .. me then table.insert(out, tonumber(d)) break end end
      end
    end
  end
  return out
end
function lh.picom_command()
  local c = S.get("commands", "picom") or os.getenv("LHSHELL_PICOM_CMD")
  if c and c ~= "" then return c end
  return "picom --config " .. S.quote(S.session_dir .. "/picom-glass.conf") .. " --backend glx"
end
function lh.compositor(want)   -- want: true = running, false = stopped
  if want then
    if #picom_pids() > 0 or lh.cm_owner() then return end
    local log = (os.getenv("XDG_RUNTIME_DIR") or "/tmp") .. "/luxshell-picom" .. (os.getenv("DISPLAY") or ""):gsub("[^%w]", "") .. ".log"
    io.stderr:write("luxshell: compositor: starting " .. lh.picom_command() .. "\n")
    lh.spawn(lh.picom_command() .. " >" .. S.quote(log) .. " 2>&1 </dev/null")
  else
    for _, pid in ipairs(picom_pids()) do io.stderr:write("luxshell: compositor: stopping picom " .. pid .. "\n"); lh.kill(pid) end
  end
end
local function composition_wanted(entry)
  local m = cfg.composition
  if m == "on" then return true elseif m == "off" then return false end
  return ((entry and entry.composition) or "auto") ~= "off"
end
local function apply_composition(entry) lh.compositor(composition_wanted(entry)) end
function lh.composition_mode() return cfg.composition end
function lh.set_composition_mode(m)
  if m ~= "follow" and m ~= "on" and m ~= "off" then return end
  cfg.composition = m
  local sel = S.set_theme_value(theme_entry, "composition", m)   -- a theme key: the current layer
  if sel then lh.select_theme(sel, true) else apply_composition(theme_entry) end
end

-- a theme switch (SETTINGS.md order: write settings.ini, set _LH_THEME, send _LH_SETTINGS_CHANGED; the message goes out from
-- S.set's 0 ms timer, after this). e = S.resolve(name): a theme file or a bare style. _LH_THEME_RESOLVED is written before
-- _LH_THEME, so a client reacting to _LH_THEME already finds the resolved view current. _LH_THEME gets the selected name itself
-- (SETTINGS.md "The session theme on the root": every reader resolves it; only an alias is replaced by its theme's name). root_set: the value somebody else just wrote to _LH_THEME (not written again: no bounce)
local function apply_theme(e, root_set)
  local t0 = lh.now()
  theme_name, theme_entry = e.name, e
  local swapped = e.shell and e.shell ~= pack_dir and load_pack(e.shell)
  if swapped then pack_own = {} end
  local fonts_changed = apply_fonts(e)
  if swapped or fonts_changed then rebuild_shell() else lh._emit("pack") end   -- "pack": the start menu and the menus drop their cached look
  set_wallpaper(e)
  cfg.composition = theme_composition(e)                 -- the theme's own switch (Glass.composition)
  apply_composition(e)
  local k = theme_taskbar(e); if k ~= cfg.taskbar then apply_taskbar(k) end   -- and its taskbar (Shell.taskbar)
  lh.start_provider_check()                              -- and its start menu
  S.select(e.name)                                       -- [theme] name; another theme's current layer is dropped (liblhde)
  publish_resolved(e)
  if root_set ~= e.name then set_theme_prop(e.name) end
  io.stderr:write(string.format("luxshell: theme %s%s: pack %s%s, %d ms\n", e.name, e.user and (" (the user's, base " .. e.base .. ")") or "", pack_dir, swapped and " (rebuilt)" or "", lh.now() - t0))
end
-- lh.select_theme(name, force): the settings flyout, Save As, settings.ini changed by another writer; force: the selected theme
-- applied again (a key of its current layer changed: the record is published again, _LH_THEME keeps its name and is not rewritten,
-- luxwm and flxp follow the republished _LH_THEME_RESOLVED and _LH_SETTINGS_CHANGED)
function lh.select_theme(name, force)
  local e = S.resolve(name)
  if not e then io.stderr:write("luxshell: theme " .. tostring(name) .. " is no theme\n") return false end
  if e.name ~= theme_name then apply_theme(e) elseif force then apply_theme(e, e.name) end
  return true
end
lh.on("lh-theme", function()
  local want = lh.root_prop("_LH_THEME")
  if not want then return end
  if (theme_echo[want] or 0) > 0 then theme_echo[want] = theme_echo[want] - 1 return end   -- our own write
  if want == theme_name then lh.reload_pack("_LH_THEME " .. want .. " set again") return end   -- the same name again: reload the pack
  local e = S.resolve(want)
  if not e then io.stderr:write("luxshell: _LH_THEME " .. want .. " is no theme (lhde-theme list)\n") return end
  if e.name == theme_name then if want ~= e.name then set_theme_prop(e.name) end return end   -- an alias of the current one
  apply_theme(e, want)
end)
-- _LH_SETTINGS_CHANGED (SETTINGS.md): another writer (flxp's Display Properties, a script) saved settings.ini: read it again
-- and apply what differs from the running state (our own messages come back too; nothing differs then). The setters below
-- write the same values back, which S.set skips (no write, no message).
local function reload_settings()
  S.reload()
  local n = S.get("theme", "name")
  if n and n ~= theme_name then lh.select_theme(n)
  else   -- the same theme: its files, or its current layer ([current]) may have changed
    local e = theme_name and S.resolve(theme_name)
    if e and theme_entry and e.record ~= theme_entry.record then apply_theme(e, theme_name) end
  end
  -- per theme (S.theme_value): the resolved record's keys; applied, not written back
  local c = theme_composition(theme_entry)
  if c ~= cfg.composition then cfg.composition = c; apply_composition(theme_entry) end
  local k = theme_taskbar(theme_entry)
  if k ~= cfg.taskbar then apply_taskbar(k) end
  local ah = S.get("taskbar", "autohide") == "1"
  if ah ~= cfg.autohide then lh.shell_set("autohide", ah) end
  local rows = tonumber(S.get("taskbar", "rows") or "")
  if rows and math.floor(rows) ~= tb.rows then lh.taskbar_set_rows(math.floor(rows)) end
  local m = S.get("sidebar", "mode") or (theme_entry and theme_entry.v["Shell.sidebar"]) or "full"
  if m ~= mode then set_mode(m) end
  local w = tonumber(S.get("sidebar", "width") or "")
  if w and clamp(math.floor(w), SB_MIN, SB_MAX) ~= sb.w then lh.sidebar_set_width(w) end
  local lay = S.get("sidebar", "stacking") == "bottom" and "bottom" or "top"
  if lay ~= lh.shell_get("sidebar_stacking") then lh.set_sidebar_stacking(lay) end
  local wc = S.get("sidebar", "with_classic") == "1"
  if wc ~= cfg.with_classic then lh.set_sidebar_with_classic(wc) end
  cfg.sb_locked = S.get("sidebar", "locked") == "1"
  if clock_pinned() ~= cfg.clock_pinned then
    cfg.clock_pinned = clock_pinned()
    if mode == "full" then build_sidebar(); build_tiles(); after_tiles(); place_sidebar() end
  end
  lh._emit("clock-layout", S.get("clock", "layout") or "default")
  cfg.desktop_black = S.get("desktop", "desktop_black") == "1"
  cfg.max_fade_ms = math.max(0, math.floor(tonumber(S.get("shell", "max_fade_ms") or "") or 0))
  publish_resolved(theme_entry)                          -- [desktop] wallpaper / mode / icons, [theme] animations
  local v = effective(theme_entry); local key = v.wallpaper .. "|" .. v.mode
  if key ~= wp_applied then set_wallpaper(theme_entry) end
  lh.start_provider_reset(); lh.start_provider_check()   -- [start] menu
  if lh.flyout_tile and lh.flyout_tile.is_settings then lh.settings_flyout() end
end
if not wp_applied then local v = effective(theme_entry); wp_applied = v.wallpaper .. "|" .. v.mode end
local reload_timer
lh.on("settings-changed", function(serial, hint)
  S.seen_serial(serial)
  if debug_events then io.stderr:write(string.format("luxshell: settings: _LH_SETTINGS_CHANGED %d %s\n", serial, tostring(hint))) end
  if reload_timer then return end
  reload_timer = lh.timer(function() reload_timer = nil; reload_settings() end, 50)   -- one reload per burst
end)
-- session commands from outside (6.2; the start menu calls lh.session.run directly):
--   xprop -root -f _LHSHELL_COMMAND 8u -set _LHSHELL_COMMAND logoff
-- Pack reload (live editing, USAGE.md "Live-editing a theme"): _LHSHELL_COMMAND "reload", or _LH_THEME set again to the name it
-- already has (session/theme-reload.sh sends the first). The theme is resolved again (theme file edits count), the pack's
-- shell.lua is read again even when the directory is the same, the theme's fonts are
-- applied again, changed PNGs leave the image cache (lh.image_flush: mtime/size), and the bars, tiles (tiles/*.lua are read
-- again by build_tiles), flyout chrome, start menu and menus are rebuilt in place (rebuild_shell: same windows, no restart).
-- Wallpaper (Desktop.wallpaper), composition and taskbar kind follow the theme's entry as on a switch. A pack that fails to load
-- keeps the running one (the error goes to the log). Several requests within 150 ms are one reload.
local reload_pending
local function reload_pack_now(why)
  local t0 = lh.now()
  S.reload()
  local e = (theme_name and S.resolve(theme_name)) or theme_entry
  local dir = (e and e.shell) or pack_dir
  if not load_pack(dir) then io.stderr:write("luxshell: reload (" .. why .. "): pack " .. tostring(dir) .. " failed, keeping the old one\n") return end
  for k in pairs(own_res) do own_res[k] = nil end
  pack_own = {}
  local flushed = lh.image_flush()
  if e then theme_entry = e end
  apply_fonts(theme_entry)
  lh.close_flyout(); if lh.menu.is_open() then lh.menu.close() end
  rebuild_shell()
  if theme_entry then
    set_wallpaper(theme_entry)
    cfg.composition = theme_composition(theme_entry); apply_composition(theme_entry)
    local k = theme_taskbar(theme_entry); if k ~= cfg.taskbar then apply_taskbar(k) end
    lh.start_provider_check()
    publish_resolved(theme_entry)
  end
  io.stderr:write(string.format("luxshell: reload (%s): theme %s, pack %s, %d image(s) re-read, %d ms\n", why, tostring(theme_name), tostring(pack_dir), flushed, lh.now() - t0))
end
function lh.reload_pack(why)
  if reload_pending then return end
  reload_pending = lh.timer(function() reload_pending = nil; reload_pack_now(why or "reload") end, 150)
end
lh.on("command", function()
  local c = lh.root_prop("_LHSHELL_COMMAND"); lh.set_root_prop("_LHSHELL_COMMAND", nil)
  if not c or c == "" then return end
  local verb, arg = c:match("^%s*(%S+)%s+(%S+)%s*$")
  if c:match("^%s*run%s*$") then lh.run_box("run") return end          -- Super+r (luxwm "shell run"): luxshell's own Run box
  if c:match("^%s*reload%s*$") then lh.reload_pack("_LHSHELL_COMMAND reload") return end   -- x11/theme-reload.sh: re-read the pack
  if verb == "taskbar" then lh.set_taskbar(arg) return end            -- _LHSHELL_COMMAND "taskbar avalon|classic|none"
  if c:match("^%s*settings%s*$") then lh.settings_flyout() return end   -- the settings flyout (e.g. with the classic bar, which has no way to it)
  if verb == "sidebar-stacking" then lh.set_sidebar_stacking(arg) return end   -- Win+S (luxwm "shell sidebar-stacking toggle"): top | bottom | toggle
  if verb == "desktop" then lh.switch_desktop(arg) return end   -- desktop N (0-based, as _NET_CURRENT_DESKTOP) | next | prev
  if verb == "sidebar-with-classic" then lh.set_sidebar_with_classic(arg == "1") return end   -- _LHSHELL_COMMAND "sidebar-with-classic 0|1"
  S.run(c)
end)

if cfg.autohide then tb.pct = 0 end
build_tb_tiles(); place_taskbar(); place_sidebar()
if cfg.sb_bottom and mode ~= "minimized" and not sb_hidden() then sb.surface:show() end   -- bottom: the taskbar is mapped last (on top)
if tb_on() then tb.surface:show() end if mode ~= "minimized" and not sb_hidden() then sb.surface:show(); sb_stack() end if dk then dk.surface:lower() end   -- the desktop layer stays at the bottom
lh.on("windows", function() if tb_on() then tb.surface:raise() end if not cfg.sb_bottom and mode ~= "minimized" and not sb_hidden() then sb.surface:raise() end end)
if cfg.taskbar == "classic" then classic_start() end
lh.start_provider_check()
rebuild_tasks()
lh.taskbar, lh.sidebar, lh.desktop_layer, lh.theme = tb, sb, dk, T
update_black()
-- the compositor rule at startup too (follow included): a session that starts on a classic theme stops the picom the session
-- (or anyone) started on this display; a glass theme gets one when none runs
lh.timer(function()
  io.stderr:write(string.format("luxshell: compositor at startup: %s (theme %s, frame %s) -> %s\n", cfg.composition, tostring(theme_name),
    tostring(theme_entry and theme_entry.composition), composition_wanted(theme_entry) and "on" or "off"))
  apply_composition(theme_entry)
end, 1500)
io.stderr:write(string.format("luxshell: theme %s, pack %s, taskbar %s %dx%d%s, sidebar %s %dx%d%s\n", tostring(theme_name), pack_dir, cfg.taskbar, tb.geo.w, tb.h, cfg.autohide and " (autohide)" or "", mode, sb.geo.w, sb.geo.h, dk and "" or ", no desktop layer (LHSHELL_DESKTOP)"))
