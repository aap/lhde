-- Session state and settings (SHELL-DESIGN 14, SETTINGS.md, THEME-FORMAT.md): the theme through liblhde's resolver (the
-- Lua binding lh.theme_*: theme files in data/themes/ and ~/.config/lhde/themes/, bare styles, aliases), the shared desktop
-- settings (~/.config/lhde/settings.ini through liblhde's store: lh.settings_set; read here, cached), luxshell's private runtime
-- state (~/.local/state/lhde/luxshell-state.ini: tile order) and the session commands (log off, stand by, shut down, restart, lock).
--   local S = dofile("session.lua")
--   S.resolve(name)            the theme NAME resolved (an alias gives its theme): an entry { name, title, file, user (its file is
--                              the user's), v (the record: v["Glass.frames"], ...), why (where each key came from), record (the
--                              _LH_THEME_RESOLVED text without serial), and the fields runtime.lua uses: shell (the pack's
--                              directory), fonts, msstyles, color, size, frame, frame_classic, composition (the frame
--                              mode: auto when the theme has a glass frame set, else off), wallpaper, mode, icons, modified (the
--                              current layer has keys: Windows' "NAME (Modified)"), display (the title shown: "TITLE (Modified)")
--                              }; nil, err
--   S.lookup(name)             the theme's name when NAME is one (a theme file, an alias, a bare style), else nil
--   S.themes([keep])           the shipped theme files whose visual style is imported (and keep, the selected one, anyway):
--                              { name, title, file, group } (group 1; bare styles are not themes: the
--                              Appearance of Display Properties picks them)
--   S.presets([keep])          the user's theme files (group 0) whose style is imported (keep anyway), with .base ([Theme] base)
--   S.theme_value(e, key)      taskbar | start | icons | composition | animations | sidebar of the resolved theme e (composition:
--                              follow | on | off, the theme's auto is follow) and its source (file | base | settings | default |
--                              fallback)
--   S.set_theme_value(e, k, v) a theme key changed here (the flyout, _LHSHELL_COMMAND taskbar): into the current layer
--                              (settings.ini [current], liblhde lhde_current_set_key; THEME-FORMAT.md sections 3 and 14), no theme
--                              file is written; returns the theme to apply again (e.name), nil when nothing changed
--   S.select(name)             select a theme (liblhde lhde_theme_select: settings.ini [theme] name, another theme's layer dropped)
--   S.save_as(title, e)        Save As: the selected theme with its layer as the user's theme file (lhde_theme_save_as: base = the
--                              theme or a copy of the user's own file, the layer's keys, name = TITLE), selected, the layer cleared;
--                              returns the new theme's name
--   S.get(sec, key)            settings.ini value (nil when missing or empty); S.has(sec, key): the key exists at all;
--                              S.flag(sec, key, default): a 0/1 switch (S.flag_value: the one parser)
--   S.set(sec, key, value)     writes it (liblhde: unknown keys, comments and order kept) and sends _LH_SETTINGS_CHANGED once the
--                              current event is handled (several sets: one message); value nil removes it (S.unset)
--   S.reload()                 forget the cached file (on _LH_SETTINGS_CHANGED from another writer)
--   S.state_get(sec, key)      / S.state_set(sec, key, value): luxshell-state.ini, runtime state only (LHSHELL_STATE overrides the path)
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
-- lhde: the session's scripts and files (picom-glass.conf, lhde-session): session/ of the tree or <prefix>/lib/lhde/session
S.session_dir = lh.component_dir("session") or "."

------------------------------------------------------------------ themes (liblhde, THEME-FORMAT.md)
local touched, cache, cache_stamp   -- below (settings.ini: the message, the cached file)
function S.lookup(name)
  if not name or name == "" then return nil end
  local c = lh.theme_lookup(name)
  if not c then c = lh.theme_lookup((name:gsub("/+$", ""):match("([^/]*)$"))) end   -- a path whose last component is one
  return c
end
-- keep: a name listed even when its style is missing (the selected theme); the others whose style is not imported are left out
function S.themes(keep)
  local out = {}
  for _, t in ipairs(lh.theme_list()) do
    if t.group == 1 and not t.name:match("%-xp$") and (not t.style_missing or t.name == keep) then table.insert(out, t) end
  end
  return out
end
function S.presets(keep)
  local out = {}
  for _, t in ipairs(lh.theme_list()) do
    if t.group == 0 and (not t.style_missing or t.name == keep) then
      for _, sec in ipairs(parse_ini(t.file) or {}) do if sec._name:lower() == "theme" then t.base = sec.base end end
      t.base = t.base or "?"; table.insert(out, t)
    end
  end
  return out
end
-- a resolved theme as runtime.lua uses it (see the header)
function S.resolve(name)
  if not name or name == "" then return nil, "no name" end
  local r, err = lh.theme_resolve(name)
  if not r then
    local b = name:gsub("/+$", ""):match("([^/]*)$")
    if b ~= name then r, err = lh.theme_resolve(b) end
    if not r then return nil, err end
  end
  local v = r.v
  local function nz(x) if x == nil or x == "" then return nil end return x end
  local udir = lh.theme_user_dir()
  local e = { name = r.name, file = r.file, v = v, why = r.why, record = r.record, title = v["Theme.name"] or r.name,
    user = r.file ~= "" and r.file:sub(1, #udir + 1) == udir .. "/",
    base = nz(v["Theme.base"]) or r.name,
    shell = nz(v["path.pack"]), fonts = v["path.fonts"] or "",
    msstyles = v["path.style"] or "", color = v["VisualStyle.color"] or "", size = v["VisualStyle.size"] or "",
    frame = v["path.frames"] or "", frame_classic = v["path.frames_classic"] or "",
    composition = (nz(v["path.frames"]) and v["Glass.composition"] ~= "0") and "auto" or "0",
    wallpaper = v["Desktop.wallpaper"] == "none" and "none" or (v["path.wallpaper"] or ""), mode = v["Desktop.mode"] or "fill",
    icons = v["Desktop.icons"] or "" }
  e.modified = (tonumber(v["current"] or "") or 0) > 0
  e.display = e.modified and (e.title .. " (Modified)") or e.title
  return e
end
-- a pack: a directory, or a name looked up in the data directories (packs/NAME with its shell.lua, LHSHELL_THEME=4074-slate)
function S.pack_dir(v)
  if not v or v == "" then return v end
  if v:sub(1, 1) == "/" or v:sub(1, 2) == "~/" then return lh.data_resolve(v) end
  local n = v:match("^packs/([^/]+)/*$") or v
  if not n:find("/", 1, true) then return lh.data_resolve_dir("packs/" .. n, "shell.lua") end
  return lh.data_resolve_dir(v, "shell.lua")
end
-- the theme keys the flyout and runtime.lua use, by their old names
S.theme_keys = { taskbar = "Shell.taskbar", icons = "Desktop.icons", composition = "Glass.composition",
  animations = "Glass.animations", sidebar = "Shell.sidebar", wallpaper = "Desktop.wallpaper" }
local srcname = function(why)
  why = why or ""
  for _, k in ipairs{ "file", "base", "settings", "default", "fallback" } do if why:sub(1, #k) == k then return k end end
  return "derived"
end
function S.theme_value(e, key)
  local k = S.theme_keys[key]; if not (k and e and e.v) then return nil, nil end
  local v = e.v[k]
  if key == "composition" and v ~= "1" and v ~= "0" then v = "auto" end
  return v, srcname(e.why and e.why[k])
end
function S.theme_default(e, key) return S.theme_value(e, key) end
function S.set_theme_value(e, key, value)
  local k = S.theme_keys[key]; if not (k and e) then return nil end

  local changed = lh.current_set_key(k, value)
  io.stderr:write(string.format("luxshell: theme %s: %s = %s into the current layer%s\n", e.name, k, tostring(value),
    changed == nil and " failed" or changed and "" or " (unchanged)"))
  if not changed then return nil end
  cache = nil
  touched("current")
  return e.name
end
function S.select(name)
  local changed = lh.theme_select(name)
  if changed == nil then return S.set("theme", "name", name) end   -- not known to liblhde: the name as it is
  cache = nil
  if changed then touched("theme") end
  return changed
end
-- Save As (THEME-FORMAT.md section 3): liblhde writes ~/.config/lhde/themes/FILE.theme from the selected theme and its layer,
-- selects it and clears the layer. FILE: the title in lower case, anything but letters, digits, '-', '_', '.' turned into '-';
-- a name that is already another theme gets "-custom"
function S.save_as(title, e)
  local file = title:lower():gsub("[^%w%-_%.]", "-"):gsub("%-+", "-"):gsub("^%-", ""):gsub("%-$", "")
  if file == "" then file = "theme" end
  if S.lookup(file) and S.lookup(file) ~= e.name then file = file .. "-custom" end
  local ok, err = lh.theme_save_as(file, title)
  if not ok then io.stderr:write("luxshell: save as " .. file .. ": " .. tostring(err) .. "\n") return nil end
  cache = nil
  touched("theme")
  return file
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
  if not f then io.stderr:write("luxshell: cannot write " .. path .. "\n") return false end
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

------------------------------------------------------------------ luxshell-state.ini (luxshell's runtime state: tile order)
-- ~/.local/state/lhde/luxshell-state.ini (docs/SETTINGS.md "Files"; LHSHELL_STATE overrides the path)
local state_path = os.getenv("LHSHELL_STATE") or lh.state_file("luxshell-state.ini")
S.state_path = state_path
function S.state_get(sec, key)
  for _, s in ipairs(parse_ini(state_path) or {}) do if s._name == sec and s[key] and s[key] ~= "" then return s[key] end end
  return nil
end
function S.state_set(sec, key, value)
  local lines = read_lines(state_path)
  if #lines == 0 then lines = { "; luxshell runtime state (tile order). The settings are in " .. lh.settings_path() .. " (docs/SETTINGS.md)" } end
  lines_set(lines, sec, key, value)
  write_lines(state_path, lines)
end

------------------------------------------------------------------ settings.ini (SETTINGS.md), shared with luxwm and flxp
S.settings_path = lh.settings_path()
-- cache: { sec = { key = value } }, cache_stamp: the file's mtime + size when read (declared above)
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
function S.reload() cache = nil; lh.settings_reload() end
function S.has(sec, key) local t = settings()[sec]; return t ~= nil and t[key] ~= nil end
function S.get(sec, key) local t = settings()[sec]; local v = t and t[key]; if v == nil or v == "" then return nil end return v end
-- Switches: 1 or 0, nothing else (flxp's shell::flag_value is the same rule). S.flag_value(v, where): true / false, nil for
-- unset or anything else (reported once per WHERE); S.flag(sec, key, default): settings.ini's, else default
local flag_told = {}
function S.flag_value(v, where)
  if v == "1" then return true elseif v == "0" then return false end
  if v ~= nil and v ~= "" and not flag_told[where] then
    flag_told[where] = true
    io.stderr:write(string.format("luxshell: %s = %s: a switch is 0 or 1; the default\n", where, v))
  end
  return nil
end
function S.flag(sec, key, default)
  local v = S.flag_value(S.get(sec, key), "settings.ini [" .. sec .. "] " .. key)
  if v == nil then return default end
  return v
end
-- _LH_SETTINGS_CHANGED: data[0] a serial, data[1] the section when only one changed. The serial is the root CARDINAL
-- _LH_SETTINGS_SERIAL + 1 (flxp's shell::store does the same: read, add one, write the property, send), and never below the highest
-- serial seen in a message or our clock seed, so it keeps increasing over luxshell restarts and between writers
S.serial = os.time() % 0x40000000     -- seeded from the clock: still increasing after a luxshell restart
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
  if (os.getenv("LHSHELL_DEBUG") or "") ~= "" then io.stderr:write(string.format("luxshell: settings: sent _LH_SETTINGS_CHANGED %d (%s)\n", S.serial, table.concat(secs, " "))) end
end
touched = function(sec)   -- one message for every section changed while the current event is handled
  dirty[sec] = true
  if not flush_timer then flush_timer = lh.timer(function() flush_timer = nil; S.notify() end, 0) end
end
function S.set(sec, key, value)
  if value ~= nil then value = tostring(value) end
  local cur = settings()[sec]
  if value ~= nil and cur and cur[key] == value then return false end   -- unchanged: no write, no message
  if not lh.settings_set(sec, key, value) then return false end
  cache = nil
  touched(sec)
  return true
end
function S.unset(sec, key) if not S.has(sec, key) then return false end return S.set(sec, key, nil) end

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
  if not cmd then io.stderr:write("luxshell: no command for " .. tostring(name) .. "\n") return false end
  io.stderr:write("luxshell: session " .. name .. ": " .. cmd .. "\n"); lh.spawn(cmd); return true
end

return S
