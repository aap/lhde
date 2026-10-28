-- Session state and settings (SHELL-DESIGN 6.1 / 6.2, SETTINGS.md): the theme registry (lhde: data/themes.ini in the data
-- directories, published as its resolved copy in _LH_THEMES_INI; lhtheme/lhde_data.h), user presets ($XDG_CONFIG_HOME/longhorn/themes/NAME.theme), the shared desktop settings
-- ($XDG_CONFIG_HOME/longhorn/settings.ini, read and written by lux, lhwm and flxp), lux's private runtime state
-- (~/.config/lhshell/state.ini: tile order) and the session commands (log off, stand by, shut down, restart, lock).
--   local S = dofile("session.lua")
--   S.registry_path()          absolute path of themes.ini (published on the root when nobody has)
--   S.lookup(name)             registry entry for a name or alias: { name, title, msstyles, color, size, frame, frame_classic,
--                              shell, wallpaper, composition, aliases, fonts }, paths absolute; nil when unknown
--   S.themes()                 all registry entries in file order
--   S.presets()                the user presets (themes/*.theme) resolved over their base, sorted by name
--   S.theme_wallpaper(e)       the theme's wallpaper from wallpapers.ini (or a legacy themes.ini `wallpaper`): file, mode, source
--   S.override_get(e, key)     settings.ini [override:NAME] KEY (a registry theme's aliases too)
--   S.resolve(name)            a registry entry (name/alias) or else a preset: the entry with the preset's overrides; every
--                              result has .base (the registry name) and, for a preset, .preset (its path) and .over (set keys)
--   S.get(sec, key)            settings.ini value (nil when missing or empty); S.has(sec, key): the key exists at all
--   S.set(sec, key, value)     writes it (only when it changed; unknown keys, comments and order kept) and sends
--                              _LH_SETTINGS_CHANGED once the current event is handled (several sets: one message)
--   S.theme_value(e, key)      the per-theme value of taskbar | start | icons | composition | animations for the resolved theme e
--                              ([override:NAME] > the preset's key > the registry > the global key > default), and its source;
--                              S.theme_default(e, key) the same without the override and the preset's key
--   S.set_theme_value(e, k, v) stores it for that theme (the preset or [override:NAME]; removed when it equals the default)
--   S.reload()                 forget the cached file (on _LH_SETTINGS_CHANGED from another writer)
--   S.migrate()                the one-time move of the user-facing keys from state.ini to settings.ini
--   S.state_get(sec, key)      / S.state_set(sec, key, value): state.ini, runtime state only (LHSHELL_STATE overrides the path)
--   S.command(name)            the shell command for logoff | standby | shutdown | restart | lock (settings.ini [commands] or
--                              the defaults below); S.run(name) runs it
local S = {}

-- ini: sections in order, "key = value" lines; comments start with ';' or '#' at the start of a line or after whitespace
local function trim(s) return (s:gsub("^%s+", ""):gsub("%s+$", "")) end
local function parse_ini(path)
  local f = io.open(path, "r"); if not f then return nil end
  local secs, cur = {}, nil
  for line in f:lines() do
    local l = trim(line)
    if l:sub(1, 1) == "[" then
      cur = { _name = trim(l:match("^%[([^%]]*)%]") or "") }; table.insert(secs, cur)
    elseif l ~= "" and not l:match("^[;#]") and cur then
      local k, v = l:match("^([^=]-)%s*=%s*(.*)$")
      if k then v = trim((v:gsub("%s[;#].*$", ""))); cur[trim(k)] = v end
    end
  end
  f:close(); return secs
end
S.parse_ini = parse_ini   -- sections in file order: { _name = "sec", key = value, ... }
local function dirname(p) return p:match("^(.*)/[^/]*$") or "." end
local function exists(p) local f = io.open(p, "r"); if f then f:close() return true end return false end
local function q(s) return "'" .. s:gsub("'", "'\\''") .. "'" end   -- shell quoting
S.quote = q
local function have(cmd) return os.execute("command -v " .. cmd .. " >/dev/null 2>&1") == true end
-- lhde: the session's scripts and files (picom-glass.conf, lhde-session): session/ of the tree or <prefix>/share/lhde/session
S.session_dir = lh.component_dir("session") or "."
-- luxshell's packs by their x11-era names (presets written before lhde say `shell = slate`)
S.pack_aliases = { aero = "4074-aero", slate = "4074-slate", slate4051 = "4051-slate", aero5112 = "5112-aero", aerovision = "4074-aerovision" }
-- a pack: a directory, or a name looked up in the data directories (packs/NAME with its shell.lua)
function S.pack_dir(v)
  if not v or v == "" then return v end
  if v:sub(1, 1) == "/" or v:sub(1, 2) == "~/" then return lh.data_resolve(v) end
  local n = v:match("^packs/([^/]+)/*$") or v:match("^lhshell/themes/([^/]+)/*$") or v
  if not n:find("/", 1, true) then n = S.pack_aliases[n] or n; return lh.data_resolve_dir("packs/" .. n, "shell.lua") end
  return lh.data_resolve_dir(v, "shell.lua")
end

------------------------------------------------------------------ registry
-- LH_THEMES_INI, else the published _LH_THEMES_INI, else themes.ini of the data directories; what we find unpublished is
-- published as its resolved copy (every path absolute, $XDG_RUNTIME_DIR/lhde/<display>/themes.ini; luxwm does the same); a
-- resolved copy is refreshed from its source when that changed (S.registry_path is called on every lookup)
local reg_path, reg_copy
function S.registry_path()
  if reg_path then if reg_copy then lh.registry_resolved(reg_path) end return reg_path end
  local env, pub = os.getenv("LH_THEMES_INI"), lh.root_prop("_LH_THEMES_INI")
  local function copy(p) local r = lh.registry_resolved(p); reg_copy = r ~= nil; return r or p end
  if env and env ~= "" and exists(env) then reg_path = env
  elseif pub and pub ~= "" and exists(pub) then reg_path = copy(pub)
  else local src = lh.registry_source(); if src then reg_path = copy(src) end end
  if reg_path and not (pub and pub ~= "") and not (env and env ~= "") then lh.set_root_prop("_LH_THEMES_INI", reg_path) end
  return reg_path
end
-- x11/../resources/msstyles/jade4074.msstyles -> /u/.../resources/msstyles/jade4074.msstyles: the directory made canonical, the last
-- component kept, so a resources/ symlink stays the published path (resources/README.md) instead of the file it points to
local function norm_path(p)
  local d, f = p:match("^(.*)/([^/]+)$")
  if not d then return lh.realpath(p) or p end
  local rd = lh.realpath(d ~= "" and d or "/")
  if not rd then return p end
  if f == ".." or f == "." then return lh.realpath(p) or p end
  return (rd == "/" and "" or rd) .. "/" .. f
end
function S.themes()
  local path = S.registry_path(); if not path then return {} end
  local out, base = {}, dirname(path)
  for _, sec in ipairs(parse_ini(path) or {}) do
    local e = { name = sec._name }
    for k, v in pairs(sec) do if k ~= "_name" then e[k] = v end end
    -- lhde: relative values are relative to the data directories (lhde_data.h), not to the registry's directory
    for _, k in ipairs{ "msstyles", "wallpaper" } do
      if e[k] and e[k] ~= "" and e[k]:sub(1, 1) ~= "/" then e[k] = lh.data_resolve(e[k]) end
    end
    for _, k in ipairs{ "frame", "frame_classic" } do
      if e[k] and e[k] ~= "" and e[k] ~= "style" and e[k]:sub(1, 1) ~= "/" then e[k] = lh.data_resolve_dir(e[k], "theme.ini") end
    end
    if e.shell and e.shell ~= "" then e.shell = S.pack_dir(e.shell) end
    if e.fonts then   -- fonts = DIR[;DIR...] (THEMES.md "Fonts"): each entry resolved, ';' separated again
      local t = {}
      for d in e.fonts:gmatch("[^;]+") do d = trim(d); if d ~= "" then table.insert(t, d:sub(1, 1) == "/" and d or lh.data_resolve(d)) end end
      e.fonts = table.concat(t, ";")
    end
    table.insert(out, e)
  end
  return out
end
function S.lookup(name)
  if not name or name == "" then return nil end
  local base = name:gsub("/+$", ""):match("([^/]*)$")
  for _, e in ipairs(S.themes()) do
    if e.name == name or e.name == base then return e end
    for a in (e.aliases or ""):gmatch("[^%s,]+") do if a == name or a == base then return e end end
  end
  return nil
end

------------------------------------------------------------------ wallpapers.ini (THEMES.md "Wallpapers")
-- [default] root (image directory, relative to the data directories; default wallpapers), file, mode; [THEME] file, mode.
-- LH_WALLPAPERS_INI overrides the path; default: wallpapers.ini next to the registry (the resolved copy has one), else the
-- data directories'.
function S.wallpapers_path()
  local env = os.getenv("LH_WALLPAPERS_INI"); if env and env ~= "" then return env end
  local reg = S.registry_path()
  local p = reg and (dirname(reg) .. "/wallpapers.ini")
  if p and exists(p) then return p end
  return lh.data_find("wallpapers.ini") or p or "wallpapers.ini"
end
local wp_cache, wp_stamp
local function wallpapers()
  local path = S.wallpapers_path()
  local st = tostring(lh.mtime(path)) .. path
  if wp_cache and wp_stamp == st then return wp_cache end
  wp_cache, wp_stamp = { secs = {} }, st
  for _, sec in ipairs(parse_ini(path) or {}) do wp_cache.secs[sec._name] = sec end
  local d = wp_cache.secs.default or {}
  local root = d.root or "wallpapers"
  if root:sub(1, 1) ~= "/" then root = lh.data_resolve(root) end
  wp_cache.root = lh.realpath(root) or root
  return wp_cache
end
local function wp_file(w, f)
  if f == "none" or f:sub(1, 1) == "/" then return f end
  if f:sub(1, 2) == "~/" then return (os.getenv("HOME") or "") .. f:sub(2) end
  return w.root .. "/" .. f
end
-- the theme's own wallpaper for theme e (a registry entry or a preset: its base's): file (absolute, or "none"), mode, source
-- ("themes.ini" | "wallpapers.ini [NAME]" | "wallpapers.ini [default]"); nil when nothing names one
function S.theme_wallpaper(e)
  local b = e and S.lookup(e.base or e.name)
  if b and b.wallpaper and b.wallpaper ~= "" then return b.wallpaper, nil, "themes.ini" end   -- legacy key: still wins
  local w = wallpapers()
  local names = {}
  if b then table.insert(names, b.name); for a in (b.aliases or ""):gmatch("[^%s,]+") do table.insert(names, a) end end
  table.insert(names, "default")
  for _, n in ipairs(names) do
    local sec = w.secs[n]
    if sec and sec.file and sec.file ~= "" then return wp_file(w, sec.file), sec.mode, "wallpapers.ini [" .. n .. "]" end
  end
  return nil
end

------------------------------------------------------------------ presets (SETTINGS.md "themes/NAME.theme")
-- [Theme] name (display name), base (a registry name); [VisualStyle] msstyles color size; [Composition] composition animations;
-- [Desktop] wallpaper mode icons; [Frames] frame frame_classic; [Shell] shell startpanel; [Fonts] fonts. Every key is optional
-- except base; the rest comes from the base entry. `shell` is a pack name (lhshell/themes/NAME) or a directory; relative paths
-- are relative to the preset file. composition: auto | off (on = auto: glass while a compositor runs, and lux starts one).
S.preset_keys = {
  VisualStyle = { "msstyles", "color", "size" }, Composition = { "composition", "animations" }, Desktop = { "wallpaper", "mode", "icons" },
  Frames = { "frame", "frame_classic" }, Shell = { "shell", "startpanel", "taskbar", "start" }, Fonts = { "fonts" },
}
local function preset_path(name) return S.presets_dir .. "/" .. name .. ".theme" end
local function load_preset(name)
  if not name or name == "" or name:find("/", 1, true) then return nil end
  local path = preset_path(name)
  local secs = parse_ini(path); if not secs then return nil end
  local by = {}; for _, sec in ipairs(secs) do by[sec._name:lower()] = sec end
  local th = by.theme or {}
  local base = S.lookup(th.base or "")
  if not base then io.stderr:write("lux: preset " .. path .. ": base " .. tostring(th.base) .. " is not in the registry\n") return nil end
  local e = {}; for k, v in pairs(base) do e[k] = v end
  e.name, e.base, e.preset, e.title, e.aliases = name, base.name, path, (th.name and th.name ~= "") and th.name or name, nil
  e.over = {}
  local pdir = dirname(path)
  local function abs(v) if v:sub(1, 1) == "/" then return v end if v:sub(1, 2) == "~/" then return (os.getenv("HOME") or "") .. v:sub(2) end return pdir .. "/" .. v end
  for sname, keys in pairs(S.preset_keys) do
    local sec = by[sname:lower()]
    if sec then for _, k in ipairs(keys) do
      local v = sec[k]
      if v and v ~= "" then
        if k == "shell" then v = v:find("/", 1, true) and S.pack_dir(abs(v)) or S.pack_dir(v)
        elseif k == "msstyles" or k == "frame" or k == "frame_classic" or (k == "wallpaper" and v ~= "none") then v = abs(v)
        elseif k == "fonts" then local t = {}; for d in v:gmatch("[^;]+") do d = trim(d); if d ~= "" then table.insert(t, abs(d)) end end v = table.concat(t, ";")
        elseif k == "composition" then v = (v == "off" or v == "0") and "off" or "auto" end
        e[k] = v; e.over[k] = true
      end
    end end
  end
  return e
end
function S.presets()
  local out = {}
  for _, f in ipairs(lh.list_dir(S.presets_dir) or {}) do
    local n = f:match("^(.+)%.theme$")
    if n and not S.lookup(n) then local e = load_preset(n); if e then table.insert(out, e) end end   -- a registry name wins
  end
  table.sort(out, function(a, b) return a.title:lower() < b.title:lower() end)
  return out
end
-- lookup order (SETTINGS.md): the registry (section name, alias, path), then themes/NAME.theme
function S.resolve(name)
  local e = S.lookup(name)
  if e then local c = {}; for k, v in pairs(e) do c[k] = v end c.base = e.name; c.over = {}; return c end
  return load_preset(name)
end
-- a preset file from a table of effective values: { name = file name, title, base, msstyles, color, size, composition,
-- animations, wallpaper, mode, icons, frame, frame_classic, shell, startpanel, fonts }; returns its path
function S.save_preset(v)
  local lines = { "; lux: saved " .. os.date("%Y-%m-%d %H:%M") .. " (Settings > Save as preset). Format: x11/SETTINGS.md; keys left out come from base.",
    "[Theme]", "name = " .. (v.title or v.name), "base = " .. v.base }
  local order = { "VisualStyle", "Composition", "Desktop", "Frames", "Shell", "Fonts" }
  for _, sname in ipairs(order) do
    local got = {}
    for _, k in ipairs(S.preset_keys[sname]) do if v[k] and v[k] ~= "" then table.insert(got, k .. " = " .. v[k]) end end
    if #got > 0 then table.insert(lines, ""); table.insert(lines, "[" .. sname .. "]"); for _, l in ipairs(got) do table.insert(lines, l) end end
  end
  local path = preset_path(v.name)
  os.execute("mkdir -p " .. q(S.presets_dir))
  local f = io.open(path, "w"); if not f then return nil end
  f:write(table.concat(lines, "\n"), "\n"); f:close()
  return path
end

------------------------------------------------------------------ ini files: read, edit in place
local function read_lines(path)
  local lines, f = {}, io.open(path, "r")
  if f then for l in f:lines() do table.insert(lines, l) end f:close() end
  return lines
end
local function write_lines(path, lines)
  os.execute("mkdir -p " .. q(dirname(path)))
  local tmp = path .. ".tmp"; local f = io.open(tmp, "w")
  if not f then io.stderr:write("lux: cannot write " .. path .. "\n") return false end
  f:write(table.concat(lines, "\n"), "\n"); f:close(); os.rename(tmp, path); return true
end
-- set one key in a line list in place, keeping every other line (comments, other sections, unknown keys); returns the old
-- value (nil when the key was missing)
local function lines_set(lines, sec, key, value)
  local insec, last, old = false, nil, nil
  for i, l in ipairs(lines) do
    local s = l:match("^%s*%[([^%]]*)%]")
    if s then insec = trim(s) == sec; if insec then last = i end
    elseif insec then
      if l:match("%S") then last = i end   -- new keys go after the section's last non-blank line
      local k, v = l:match("^%s*([^=;#]-)%s*=%s*(.*)$")
      if k and trim(k) == key then
        old = trim((v:gsub("%s[;#].*$", "")))
        local comment = v:match("(%s+[;#].*)$") or ""                    -- a trailing comment stays
        lines[i] = key .. " = " .. value .. comment; return old, true
      end
    end
  end
  if last then table.insert(lines, last + 1, key .. " = " .. value)
  else if #lines > 0 and lines[#lines]:match("%S") then table.insert(lines, "") end
    table.insert(lines, "[" .. sec .. "]"); table.insert(lines, key .. " = " .. value) end
  return nil, false
end
-- drop keys from a line list: drop(sec, key) -> true removes it; sections left with nothing but blank lines go too
local function lines_drop(lines, drop)
  local out, sec, hdr = {}, nil, nil
  local function flush_hdr() if hdr then for _, l in ipairs(hdr) do table.insert(out, l) end hdr = nil end end
  for _, l in ipairs(lines) do
    local s = l:match("^%s*%[([^%]]*)%]")
    if s then
      sec = trim(s); hdr = { l }                                         -- held back until the section shows a line worth keeping
    else
      local k = l:match("^%s*([^=;#]-)%s*=")
      if k and sec and drop(sec, trim(k)) then -- removed
      elseif hdr and not l:match("%S") then table.insert(hdr, l)
      else flush_hdr(); table.insert(out, l) end
    end
  end
  while #out > 0 and not out[#out]:match("%S") do table.remove(out) end
  return out
end

------------------------------------------------------------------ state.ini (lux's runtime state: tile order)
local cfg_home = os.getenv("XDG_CONFIG_HOME") or ((os.getenv("HOME") or ".") .. "/.config")
local state_path = os.getenv("LHSHELL_STATE") or (cfg_home .. "/lhshell/state.ini")
S.state_path = state_path
function S.state_get(sec, key)
  for _, s in ipairs(parse_ini(state_path) or {}) do if s._name == sec and s[key] and s[key] ~= "" then return s[key] end end
  return nil
end
function S.state_set(sec, key, value)
  local lines = read_lines(state_path)
  if #lines == 0 then lines = { "; lux runtime state (tile order). The settings are in " .. "$XDG_CONFIG_HOME/longhorn/settings.ini (x11/SETTINGS.md)" } end
  lines_set(lines, sec, key, value)
  write_lines(state_path, lines)
end

------------------------------------------------------------------ settings.ini (SETTINGS.md), shared with lhwm and flxp
S.settings_path = os.getenv("LH_SETTINGS") or (cfg_home .. "/longhorn/settings.ini")
S.presets_dir = dirname(S.settings_path) .. "/themes"
local cache, cache_stamp      -- { sec = { key = value } }, and the file's mtime + size when read
local function stamp() local f = io.open(S.settings_path, "r"); if not f then return "none" end local n = f:seek("end"); f:close(); return tostring(lh.mtime(S.settings_path)) .. ":" .. tostring(n) end
local function settings()
  local st = stamp()
  if cache and cache_stamp == st then return cache end
  cache, cache_stamp = {}, st
  for _, sec in ipairs(parse_ini(S.settings_path) or {}) do
    local t = cache[sec._name] or {}; cache[sec._name] = t
    for k, v in pairs(sec) do if k ~= "_name" then t[k] = v end end
  end
  return cache
end
function S.reload() cache = nil end
function S.has(sec, key) local t = settings()[sec]; return t ~= nil and t[key] ~= nil end
function S.get(sec, key) local t = settings()[sec]; local v = t and t[key]; if v == nil or v == "" then return nil end return v end
-- _LH_SETTINGS_CHANGED: data[0] a serial, data[1] the section when only one changed. The serial is the root CARDINAL
-- _LH_SETTINGS_SERIAL + 1 (flxp's shell::store does the same: read, add one, write the property, send), and never below the highest
-- serial seen in a message or our clock seed, so it keeps increasing over lux restarts and between writers
S.serial = os.time() % 0x40000000     -- seeded from the clock: still increasing after a lux restart
local dirty, flush_timer = {}, nil
function S.seen_serial(n) if n and n > S.serial and n < 0x7fffffff then S.serial = n end end
function S.notify()
  if flush_timer then lh.cancel(flush_timer); flush_timer = nil end
  local secs = {}; for k in pairs(dirty) do table.insert(secs, k) end
  dirty = {}
  if #secs == 0 then return end
  local root = lh.root_prop("_LH_SETTINGS_SERIAL")
  if math.type(root) == "integer" then S.seen_serial(root) end
  S.serial = (S.serial + 1) & 0x7fffffff
  lh.set_root_cardinal("_LH_SETTINGS_SERIAL", S.serial)
  lh.send_root_message("_LH_SETTINGS_CHANGED", S.serial, #secs == 1 and lh.atom(secs[1]) or 0)
  if (os.getenv("LHSHELL_DEBUG") or "") ~= "" then io.stderr:write(string.format("lux: settings: sent _LH_SETTINGS_CHANGED %d (%s)\n", S.serial, table.concat(secs, " "))) end
end
local function touched(sec)   -- one message for every section changed while the current event is handled
  dirty[sec] = true
  if not flush_timer then flush_timer = lh.timer(function() flush_timer = nil; S.notify() end, 0) end
end
local header = {
  "; Shared desktop settings: lhwm, lux (the Avalon shell) and the flxp programs read and write this file (x11/SETTINGS.md).",
  "; Every writer keeps keys it does not know; after a write it sends the root ClientMessage _LH_SETTINGS_CHANGED.",
}
function S.set(sec, key, value)
  value = tostring(value or "")
  local cur = settings()[sec]
  if cur and cur[key] == value then return false end                 -- unchanged: no write, no message
  local lines = read_lines(S.settings_path)
  if #lines == 0 then for _, l in ipairs(header) do table.insert(lines, l) end end
  lines_set(lines, sec, key, value)
  if not write_lines(S.settings_path, lines) then return false end
  cache = nil
  touched(sec)
  return true
end

------------------------------------------------------------------ per-theme settings (SETTINGS.md "Per-theme settings" and
-- "Per-theme overrides: [override:NAME]"; THEMES.md "Per-theme shell settings"). The Appearance values belong to the theme: for
-- theme e (an S.resolve result, a registry entry or a preset) the value of KEY is the first set of
--   1. settings.ini [override:NAME] KEY (what flxp's Display Properties and lux's flyout write for a registry theme),
--   2. the preset's own key (e.preset: [Shell] taskbar / start, [Desktop] icons, [Composition] composition / animations),
--   3. the registry entry of e.base (taskbar, start, flxp_icons, animations; composition has no registry default here),
--   4. the global key ([taskbar] kind, [start] menu, [desktop] icons, [theme] composition / animations),
--   5. the built-in default.
-- The same chain and table as flxp's store::theme_value (src/shell/settings.cpp theme_keys), so the dialog and lux agree.
S.theme_keys = {
  taskbar     = { reg = "taskbar",    psec = "Shell",       pkey = "taskbar",     gsec = "taskbar", gkey = "kind",        dflt = "avalon" },
  start       = { reg = "start",      psec = "Shell",       pkey = "start",       gsec = "start",   gkey = "menu",        dflt = "flxp" },
  icons       = { reg = "flxp_icons", psec = "Desktop",     pkey = "icons",       gsec = "desktop", gkey = "icons",       dflt = "xp" },
  composition = {                     psec = "Composition", pkey = "composition", gsec = "theme",   gkey = "composition", dflt = "follow" },
  animations  = { reg = "animations", psec = "Composition", pkey = "animations",  gsec = "theme",   gkey = "animations",  dflt = "4074plus" },
}
local function preset_raw(e, psec, pkey)   -- the preset file's own value, read fresh (sections and keys case-insensitive)
  if not (e and e.preset) then return nil end
  local v
  for _, sec in ipairs(parse_ini(e.preset) or {}) do
    if sec._name:lower() == psec:lower() then
      for k, x in pairs(sec) do if k ~= "_name" and k:lower() == pkey and x ~= "" then v = x end end
    end
  end
  return v
end
local function norm(key, v)
  if key == "composition" and v == "auto" then return "follow" end   -- a preset lux saved before: auto is the frame mode
  if key == "start" and v == "lhshell" then return "lux" end
  return v
end
-- value, source ("override" | "preset" | "registry" | "global" | "default"); own = false: what the theme gives without 1-2
local function chain(e, key, own)
  local tk = S.theme_keys[key]; if not tk then return nil, nil end
  if e and own then
    local v = S.override_get(e, key); if v then return norm(key, v), "override" end
    v = preset_raw(e, tk.psec, tk.pkey); if v then return norm(key, v), "preset" end
  end
  if e and tk.reg then
    local b = S.lookup(e.base or e.name); local v = b and b[tk.reg]
    if v and v ~= "" then return norm(key, v), "registry" end
  end
  local v = S.get(tk.gsec, tk.gkey); if v then return norm(key, v), "global" end
  return tk.dflt, "default"
end
function S.theme_value(e, key) return chain(e, key, true) end
-- settings.ini [override:NAME] KEY for theme e; for a registry theme also [override:ALIAS] (2026-10-25: [aero4074] became
-- [aero-patch] with aero4074 as an alias, and an [override:aero4074] written before keeps applying). The section name wins.
function S.override_get(e, key)
  if not e then return nil end
  local v = S.get("override:" .. e.name, key); if v then return v end
  if e.preset then return nil end
  local b = S.lookup(e.base or e.name)
  for a in ((b and b.aliases) or ""):gmatch("[^%s,]+") do v = S.get("override:" .. a, key); if v then return v end end
  return nil
end
function S.theme_default(e, key) return chain(e, key, false) end
-- store a per-theme value the way Display Properties does: into the preset when e is a preset, else into [override:NAME];
-- a value equal to what the theme gives without it (S.theme_default) removes the key (and an emptied [override:NAME]). The
-- global key is not written. Sends _LH_SETTINGS_CHANGED (hint "theme") like S.set. Returns true when a file changed.
local function lines_remove(lines, issec, iskey)   -- one key out; its section too when nothing but blanks/comments is left
  local out, hdr_at, keep = {}, nil, false
  local function close() if hdr_at and not keep then while #out >= hdr_at do table.remove(out) end end hdr_at = nil end
  for _, l in ipairs(lines) do
    local s = l:match("^%s*%[([^%]]*)%]")
    if s then
      close()
      if issec(trim(s)) then hdr_at, keep = #out + 1, false end
      table.insert(out, l)
    else
      local k = l:match("^%s*([^=;#]-)%s*=")
      if hdr_at and k and iskey(trim(k)) then -- dropped
      else
        if hdr_at and k then keep = true end
        table.insert(out, l)
      end
    end
  end
  close()
  while #out > 0 and not out[#out]:match("%S") do table.remove(out) end
  return out
end
-- S.unset(sec, key): remove a key from settings.ini (its section too when nothing else is left); sends _LH_SETTINGS_CHANGED like S.set
function S.unset(sec, key)
  if not S.has(sec, key) then return false end
  local lines = lines_remove(read_lines(S.settings_path), function(s) return s == sec end, function(k) return k == key end)
  if not write_lines(S.settings_path, lines) then return false end
  cache = nil
  touched(sec)
  return true
end
function S.set_theme_value(e, key, value)
  local tk = S.theme_keys[key]; if not (tk and e) then return false end
  value = tostring(value or "")
  local dflt = value == S.theme_default(e, key)
  if e.preset then
    local cur = preset_raw(e, tk.psec, tk.pkey)
    local lines = read_lines(e.preset)
    if dflt then
      if cur == nil then return false end
      lines = lines_remove(lines, function(s) return s:lower() == tk.psec:lower() end, function(k) return k:lower() == tk.pkey end)
    else
      if cur == value then return false end
      local sname = tk.psec
      for _, l in ipairs(lines) do local s = l:match("^%s*%[([^%]]*)%]"); if s and trim(s):lower() == sname:lower() then sname = trim(s) end end
      lines_set(lines, sname, tk.pkey, value)
    end
    if not write_lines(e.preset, lines) then return false end
  else
    local sec = "override:" .. e.name
    local lines = read_lines(S.settings_path)
    if dflt then
      if not S.has(sec, key) then return false end
      lines = lines_remove(lines, function(s) return s == sec end, function(k) return k == key end)
    else
      if S.get(sec, key) == value then return false end
      if #lines == 0 then for _, l in ipairs(header) do table.insert(lines, l) end end
      lines_set(lines, sec, key, value)
    end
    if not write_lines(S.settings_path, lines) then return false end
    cache = nil
  end
  do
    io.stderr:write(string.format("lux: settings: %s for theme %s: %s %s\n", key, e.name, dflt and "back to the theme's" or "=", dflt and S.theme_value(e, key) or value))
  end
  touched("theme")
  return true
end

-- the one-time migration (SETTINGS.md): the user-facing keys of state.ini move to settings.ini (a key settings.ini already has
-- is not overwritten), state.ini keeps only the runtime state, and state.ini.pre-settings is the untouched original
S.migration = {   -- state.ini [sec] key -> settings.ini [sec] key; a function maps the value (nil: not copied)
  { "shell", "theme", "theme", "name" }, { "shell", "composition", "theme", "composition" },
  { "shell", "wallpaper", "desktop", "wallpaper", function(v) return v == "0" and "none" or nil end },
  { "shell", "desktop_black", "desktop", "desktop_black" },
  { "taskbar", "enabled", "taskbar", "kind" }, { "taskbar", "autohide" }, { "taskbar", "rows" }, { "taskbar", "classic_start_menu" },
  { "sidebar", "mode" }, { "sidebar", "width" }, { "sidebar", "with_classic" }, { "sidebar", "yield_bottom" }, { "sidebar", "stacking" },
  -- the old [sidebar] layer is part of stacking now: below -> stacking = bottom, above (the default) is dropped
  { "sidebar", "layer", "sidebar", "stacking", function(v) return v == "below" and "bottom" or nil end },
  { "clock", "pinned" }, { "clock", "pinned_pack" }, { "clock", "layout" },
  { "start", "pinned" }, { "start", "mfu_count" }, { "start", "userpane" },
  { "commands", "*" },
}
local function mig_rule(sec, key)
  for _, m in ipairs(S.migration) do if m[1] == sec and (m[2] == key or m[2] == "*") then return m end end
end
function S.migrate()
  local secs = parse_ini(state_path); if not secs then return 0 end
  local todo = {}
  for _, s in ipairs(secs) do
    for k, v in pairs(s) do if k ~= "_name" and mig_rule(s._name, k) then table.insert(todo, { s._name, k, v }) end end
  end
  if #todo == 0 then return 0 end
  local bak = state_path .. ".pre-settings"
  if not exists(bak) then os.execute("cp -p " .. q(state_path) .. " " .. q(bak)) end
  -- settings.ini in SETTINGS.md's section order
  local order = { theme = 1, desktop = 2, taskbar = 3, sidebar = 4, clock = 5, start = 6, commands = 7 }
  local mapped = {}
  for _, t in ipairs(todo) do
    local m = mig_rule(t[1], t[2])
    local fn = type(m[5]) == "function" and m[5] or nil
    local ds, dk = m[3] or t[1], (m[4] and type(m[4]) == "string") and m[4] or t[2]
    local v = t[3]; if fn then v = fn(v) end
    if v ~= nil then table.insert(mapped, { ds, dk, v }) end
  end
  table.sort(mapped, function(a, b) local oa, ob = order[a[1]] or 9, order[b[1]] or 9; if oa ~= ob then return oa < ob end return a[2] < b[2] end)
  local lines = read_lines(S.settings_path)
  if #lines == 0 then for _, l in ipairs(header) do table.insert(lines, l) end end
  local have = settings(); local n = 0
  for _, m in ipairs(mapped) do
    if not (have[m[1]] and have[m[1]][m[2]] ~= nil) then lines_set(lines, m[1], m[2], m[3]); n = n + 1 end
  end
  write_lines(S.settings_path, lines); cache = nil
  -- state.ini without them
  local st = lines_drop(read_lines(state_path), function(sec, key) return mig_rule(sec, key) ~= nil end)
  if st[1] and st[1]:match("^; lhshell state") then st[1] = "; lux runtime state (tile order). The settings moved to " .. S.settings_path .. " (x11/SETTINGS.md)" end
  write_lines(state_path, st)
  io.stderr:write(string.format("lux: settings: moved %d key(s) from %s to %s (%d already there; original kept as %s)\n", n, state_path, S.settings_path, #mapped - n, bak))
  for k in pairs(order) do dirty[k] = true end
  S.notify()
  return n
end

------------------------------------------------------------------ session commands (6.2)
function S.default_command(name)
  if name == "logoff" then
    -- an lhde-session has a state dir for its display; there "stop" ends the X session. Elsewhere luxwm exits cleanly
    -- (properties deleted, clients left mapped) and whatever started it decides what follows.
    local st = (os.getenv("XDG_RUNTIME_DIR") or "/tmp") .. "/lhde-session" .. (os.getenv("DISPLAY") or "")
    local function prog(n) local p = lh.bin_dir .. "/" .. n; if exists(p) then return q(p) end return n end
    if os.execute("test -d " .. q(st)) == true then
      local ls = S.session_dir .. "/lhde-session"; return (exists(ls) and q(ls) or prog("lhde-session")) .. " stop" end
    return prog("luxwmc") .. " quit"
  elseif name == "standby" then return have("zzz") and "zzz" or "loginctl suspend"
  elseif name == "shutdown" then return "loginctl poweroff"
  elseif name == "restart" then return "loginctl reboot"
  elseif name == "lock" then return have("xlock") and "xlock" or have("slock") and "slock" or nil end
  return nil
end
function S.command(name) return S.get("commands", name) or S.default_command(name) end
function S.run(name)
  local cmd = S.command(name)
  if not cmd then io.stderr:write("lux: no command for " .. tostring(name) .. "\n") return false end
  io.stderr:write("lux: session " .. name .. ": " .. cmd .. "\n"); lh.spawn(cmd); return true
end

return S
