-- Applications for the start menu (SHELL-DESIGN 6.3): freedesktop desktop entries, a small icon-theme lookup, the launch
-- counts behind the frequently-used list (XP's MFU), the default browser / mail client, the XDG user directories.
--   local A = dofile("apps.lua")
--   A.entries()            { id = entry }, entry = { id, name, generic, comment, exec, icon, terminal, categories = {..}, path }
--   A.list()               visible entries sorted by name
--   A.get(id)              one entry (id = "xterm.desktop") or nil
--   A.launch(e)            run it (field codes dropped, Terminal=true → xterm -e), count the launch in mfu.ini
--   A.mfu(n, skip)         the n most launched entries (skip = { id = true } for the pinned ones)
--   A.icon(name, size)     a PNG path for an icon name or path at about size px, or nil
--   A.default_browser(), A.default_mail()   desktop ids (xdg-settings / xdg-mime, else the first installed one)
--   A.user_dir("DOCUMENTS" | "PICTURES" | "MUSIC" | ...)
--   A.categories()         { {name, title, icon, entries = {...}}, ... } for the All Programs cascade
local S = lh.session
local A = {}
local HOME = os.getenv("HOME") or "/"
local XDG_DATA_HOME = os.getenv("XDG_DATA_HOME") or (HOME .. "/.local/share")
local XDG_CONFIG_HOME = os.getenv("XDG_CONFIG_HOME") or (HOME .. "/.config")
local CACHE = (os.getenv("XDG_CACHE_HOME") or (HOME .. "/.cache")) .. "/lhshell"
local q = S.quote
local function exists(p) local f = io.open(p, "r"); if f then f:close() return true end return false end
local function trim(s) return (s:gsub("^%s+", ""):gsub("%s+$", "")) end
local function readcmd(cmd) local p = io.popen(cmd .. " 2>/dev/null"); if not p then return nil end local v = p:read("l"); p:close(); return v and trim(v) ~= "" and trim(v) or nil end

------------------------------------------------------------------ desktop entries
A.dirs = { XDG_DATA_HOME .. "/applications" }
for d in (os.getenv("XDG_DATA_DIRS") or "/usr/local/share:/usr/share"):gmatch("[^:]+") do table.insert(A.dirs, d .. "/applications") end
local path_dirs = {}; for d in (os.getenv("PATH") or "/usr/bin:/bin"):gmatch("[^:]+") do table.insert(path_dirs, d) end
local function in_path(cmd)
  if not cmd or cmd == "" then return false end
  if cmd:sub(1, 1) == "/" then return exists(cmd) end
  for _, d in ipairs(path_dirs) do if exists(d .. "/" .. cmd) then return true end end
  return false
end
A.in_path = in_path
local function parse_desktop(path, id)
  local f = io.open(path, "r"); if not f then return nil end
  local e, insec = { id = id, path = path, categories = {} }, false
  for line in f:lines() do
    local sec = line:match("^%[(.-)%]")
    if sec then insec = sec == "Desktop Entry"
    elseif insec then
      local k, v = line:match("^([%w%-]+)%s*=%s*(.-)%s*$")
      if k then
        if k == "Name" then e.name = v elseif k == "GenericName" then e.generic = v elseif k == "Comment" then e.comment = v
        elseif k == "Exec" then e.exec = v elseif k == "TryExec" then e.tryexec = v elseif k == "Icon" then e.icon = v
        elseif k == "Type" then e.type = v elseif k == "Terminal" then e.terminal = v == "true"
        elseif k == "NoDisplay" then e.nodisplay = v == "true" elseif k == "Hidden" then e.hidden = v == "true"
        elseif k == "OnlyShowIn" then e.onlyshowin = v
        elseif k == "Categories" then for c in v:gmatch("[^;]+") do e.categories[c] = true end end
      end
    end
  end
  f:close()
  if e.type ~= "Application" or not e.name or not e.exec then return nil end
  return e
end
local cache, cache_key
local function scan_dir(dir, prefix, out)
  for _, name in ipairs(lh.list_dir(dir) or {}) do
    local p = dir .. "/" .. name
    if name:match("%.desktop$") then
      local id = prefix .. name
      if out[id] == nil then out[id] = parse_desktop(p, id) or false end   -- the first directory wins (~/.local overrides)
    elseif lh.list_dir(p) then scan_dir(p, prefix .. name .. "-", out) end   -- subdirectories: id "sub-name.desktop"
  end
end
function A.entries()
  local key = {}; for _, d in ipairs(A.dirs) do table.insert(key, tostring(lh.mtime(d))) end; key = table.concat(key, ",")
  if cache and key == cache_key then return cache end
  local raw = {}; for _, d in ipairs(A.dirs) do scan_dir(d, "", raw) end
  cache = {}
  for id, e in pairs(raw) do
    if e and not e.hidden then
      local ok = not e.tryexec or in_path(e.tryexec)
      if ok then cache[id] = e end
    end
  end
  cache_key = key; return cache
end
function A.get(id) return id and A.entries()[id] or nil end
local function visible(e) return not e.nodisplay and (not e.onlyshowin or e.onlyshowin:match("X%-Longhorn")) end
function A.list()
  local out = {}; for _, e in pairs(A.entries()) do if visible(e) then table.insert(out, e) end end
  table.sort(out, function(a, b) return a.name:lower() < b.name:lower() end); return out
end

------------------------------------------------------------------ launching and the MFU list (~/.config/lhshell/mfu.ini)
-- one line per entry: "<id> = <count> <last launch, epoch s>". XP keeps a decaying usage count per shortcut (proglist.cpp
-- UEM data); here the list is ordered by count, ties by the most recent launch.
local mfu_path = os.getenv("LHSHELL_MFU") or (XDG_CONFIG_HOME .. "/lhshell/mfu.ini")   -- LHSHELL_MFU: another file (tests)
A.mfu_path = mfu_path
local function mfu_read()
  local t = {}; local f = io.open(mfu_path, "r"); if not f then return t end
  for line in f:lines() do local id, n, ts = line:match("^%s*([^;#=%s][^=]-)%s*=%s*(%d+)%s*(%d*)") if id then t[id] = { n = tonumber(n), t = tonumber(ts) or 0 } end end
  f:close(); return t
end
local function mfu_write(t)
  os.execute("mkdir -p " .. q(mfu_path:match("^(.*)/") or "."))
  local f = io.open(mfu_path .. ".tmp", "w"); if not f then return end
  f:write("; lhshell start menu launch counts (SHELL-DESIGN 6.3): desktop id = count last-launch-epoch\n[mfu]\n")
  local ids = {}; for id in pairs(t) do table.insert(ids, id) end; table.sort(ids)
  for _, id in ipairs(ids) do f:write(string.format("%s = %d %d\n", id, t[id].n, t[id].t)) end
  f:close(); os.rename(mfu_path .. ".tmp", mfu_path)
end
function A.command(e)
  local cmd = e.exec:gsub("%%%%", "\1"):gsub("%%[fFuUdDnNvmk]", ""):gsub("%%i", e.icon and ("--icon " .. q(e.icon)) or ""):gsub("%%c", q(e.name)):gsub("\1", "%%")
  cmd = trim(cmd)
  if e.terminal then cmd = "xterm -e " .. cmd end
  return cmd
end
function A.launch(e)
  if type(e) == "string" then e = A.get(e) end
  if not e then return false end
  local cmd = A.command(e)
  io.stderr:write("luxshell: launch " .. e.id .. ": " .. cmd .. "\n")
  lh.spawn(cmd)
  local t = mfu_read(); local m = t[e.id] or { n = 0, t = 0 }; m.n = m.n + 1; m.t = os.time(); t[e.id] = m; mfu_write(t)
  return true
end
function A.mfu(n, skip)
  local t, out = mfu_read(), {}
  for id, m in pairs(t) do local e = A.get(id); if e and not (skip and skip[id]) then table.insert(out, { e = e, n = m.n, t = m.t }) end end
  table.sort(out, function(a, b) if a.n ~= b.n then return a.n > b.n end return a.t > b.t end)
  local r = {}; for i = 1, math.min(n, #out) do r[i] = out[i].e end; return r
end

------------------------------------------------------------------ defaults
local function first_with(cat, prefer)
  for _, id in ipairs(prefer) do if A.get(id) then return id end end
  for _, e in ipairs(A.list()) do if e.categories[cat] then return e.id end end
end
function A.default_browser()
  local id = readcmd("xdg-settings get default-web-browser")
  if id and A.get(id) then return id end
  return first_with("WebBrowser", { "firefox.desktop", "chromium.desktop", "google-chrome.desktop", "org.qutebrowser.qutebrowser.desktop", "falkon.desktop" })
end
function A.default_mail()
  local id = readcmd("xdg-mime query default x-scheme-handler/mailto")
  if id and A.get(id) then return id end
  return first_with("Email", { "thunderbird.desktop", "org.mozilla.Thunderbird.desktop", "claws-mail.desktop", "evolution.desktop", "sylpheed.desktop" })
end

------------------------------------------------------------------ XDG user directories
function A.user_dir(kind)
  local v = readcmd("xdg-user-dir " .. kind)
  if not v then
    local f = io.open(XDG_CONFIG_HOME .. "/user-dirs.dirs", "r")
    if f then for line in f:lines() do local k, p = line:match('^XDG_(%u+)_DIR="(.-)"') if k == kind then v = p:gsub("^%$HOME", HOME) end end f:close() end
  end
  if not v then
    local def = { DOCUMENTS = "Documents", PICTURES = "Pictures", MUSIC = "Music", VIDEOS = "Videos", DOWNLOAD = "Downloads", DESKTOP = "Desktop" }
    v = HOME .. "/" .. (def[kind] or "")
  end
  if v ~= HOME and not lh.list_dir(v) then v = HOME end   -- missing directory: the home folder
  return v
end

------------------------------------------------------------------ icon lookup (freedesktop Icon Theme Specification, simplified)
-- themes: the GTK icon theme (gtk-3.0 / gtk-2.0 settings), its Inherits chain, then hicolor; base dirs ~/.icons,
-- $XDG_DATA_HOME/icons, $XDG_DATA_DIRS/icons; per theme the index.theme Directories with Size/MinSize/MaxSize/Type/Scale,
-- the closest size wins; last /usr/share/pixmaps. Files that are not PNG (svg, xpm) or not square PNGs are converted once
-- with ImageMagick into ~/.cache/lhshell/icons/<size>/.
local bases = { HOME .. "/.icons", XDG_DATA_HOME .. "/icons" }
for d in (os.getenv("XDG_DATA_DIRS") or "/usr/local/share:/usr/share"):gmatch("[^:]+") do table.insert(bases, d .. "/icons") end
local function gtk_theme()
  for _, p in ipairs{ XDG_CONFIG_HOME .. "/gtk-3.0/settings.ini", XDG_CONFIG_HOME .. "/gtk-4.0/settings.ini" } do
    local f = io.open(p, "r"); if f then for l in f:lines() do local v = l:match("^%s*gtk%-icon%-theme%-name%s*=%s*(.-)%s*$") if v then f:close() return v end end f:close() end
  end
  local f = io.open(HOME .. "/.gtkrc-2.0", "r")
  if f then for l in f:lines() do local v = l:match('^%s*gtk%-icon%-theme%-name%s*=%s*"?(.-)"?%s*$') if v then f:close() return v end end f:close() end
  return nil
end
local themes_cache = {}
local function load_theme(name)
  if themes_cache[name] ~= nil then return themes_cache[name] end
  local th = { name = name, roots = {}, dirs = {}, inherits = {} }
  for _, b in ipairs(bases) do
    local root = b .. "/" .. name
    if lh.list_dir(root) then
      table.insert(th.roots, root)
      local f = io.open(root .. "/index.theme", "r")
      if f and #th.dirs == 0 then
        local sec, cur, list = nil, nil, nil; local info = {}
        for l in f:lines() do
          local s = l:match("^%[(.-)%]")
          if s then sec = s; cur = {}; info[s] = cur
          elseif cur then local k, v = l:match("^%s*([%w]+)%s*=%s*(.-)%s*$") if k then cur[k] = v end end
        end
        f:close()
        local it = info["Icon Theme"] or {}
        for d in (it.Directories or ""):gmatch("[^,]+") do
          local i = info[d] or {}; local size = tonumber(i.Size) or 0
          table.insert(th.dirs, { path = d, size = size, scale = tonumber(i.Scale) or 1, type = i.Type or "Threshold",
            min = tonumber(i.MinSize) or size, max = tonumber(i.MaxSize) or size, threshold = tonumber(i.Threshold) or 2 })
        end
        for p in (it.Inherits or ""):gmatch("[^,]+") do table.insert(th.inherits, p) end
      elseif f then f:close() end
    end
  end
  if #th.roots == 0 then th = false end
  themes_cache[name] = th; return th
end
local function dir_distance(d, size)
  if d.scale ~= 1 then return 1e6 end
  if d.type == "Fixed" then return math.abs(d.size - size) end
  if d.type == "Scalable" then if size < d.min then return d.min - size elseif size > d.max then return size - d.max end return 0 end
  if size < d.size - d.threshold then return d.size - d.threshold - size elseif size > d.size + d.threshold then return size - d.size - d.threshold end return 0
end
local exts = { ".png", ".svg", ".xpm" }
local function find_in_theme(th, name, size, seen)
  if not th or seen[th.name] then return nil end
  seen[th.name] = true
  local best, bestd
  for _, d in ipairs(th.dirs) do
    local dist = dir_distance(d, size)
    if not bestd or dist < bestd then
      for _, root in ipairs(th.roots) do
        for _, x in ipairs(exts) do local p = root .. "/" .. d.path .. "/" .. name .. x; if exists(p) then best, bestd = p, dist break end end
        if best and bestd == dist then break end
      end
    end
  end
  if best then return best end
  for _, p in ipairs(th.inherits) do local r = find_in_theme(load_theme(p), name, size, seen); if r then return r end end
  return nil
end
local function png_size(p)   -- width, height from the IHDR chunk
  local f = io.open(p, "rb"); if not f then return nil end
  local h = f:read(24); f:close()
  if not h or #h < 24 or h:sub(2, 4) ~= "PNG" then return nil end
  local function be(i) local a, b, c, d = h:byte(i, i + 3); return ((a * 256 + b) * 256 + c) * 256 + d end
  return be(17), be(21)
end
local icon_cache = {}
local function to_png(src, size)
  local w, h = nil, nil
  if src:match("%.png$") then w, h = png_size(src); if w and w == h then return src end end
  local out = string.format("%s/icons/%d/%s.png", CACHE, size, (src:gsub("^/", ""):gsub("/", "_")))
  local sm, om = lh.mtime(src), lh.mtime(out)
  if om and sm and om >= sm then return out end
  os.execute(string.format("mkdir -p %s && magick -background none %s -resize %dx%d -gravity center -extent %dx%d %s 2>/dev/null",
    q(CACHE .. "/icons/" .. size), q(src), size, size, size, size, q(out)))
  return exists(out) and out or nil
end
A.to_png = to_png
function A.icon(name, size)
  if not name or name == "" then return nil end
  local key = name .. "@" .. size
  if icon_cache[key] ~= nil then return icon_cache[key] or nil end
  local found
  if name:sub(1, 1) == "/" then found = exists(name) and name or nil
  else
    name = name:gsub("%.png$", ""):gsub("%.svg$", ""):gsub("%.xpm$", "")
    local seen = {}
    local gt = gtk_theme()
    if gt then found = find_in_theme(load_theme(gt), name, size, seen) end
    if not found then found = find_in_theme(load_theme("hicolor"), name, size, seen) end
    if not found then for _, x in ipairs(exts) do local p = "/usr/share/pixmaps/" .. name .. x; if exists(p) then found = p break end end end
  end
  local png = found and to_png(found, size) or nil
  icon_cache[key] = png or false
  return png
end

------------------------------------------------------------------ All Programs: categories (freedesktop main categories)
A.category_titles = {
  { "AudioVideo", "Sound & Video" }, { "Development", "Programming" }, { "Education", "Education" }, { "Game", "Games" },
  { "Graphics", "Graphics" }, { "Network", "Internet" }, { "Office", "Office" }, { "Science", "Science" },
  { "Settings", "Settings" }, { "System", "System Tools" }, { "Utility", "Accessories" },
}
function A.categories()
  local groups, order = {}, {}
  for _, ct in ipairs(A.category_titles) do groups[ct[1]] = { name = ct[1], title = ct[2], entries = {} }; table.insert(order, groups[ct[1]]) end
  local other = { name = "Other", title = "Other", entries = {} }
  for _, e in ipairs(A.list()) do
    local g
    for _, ct in ipairs(A.category_titles) do if e.categories[ct[1]] then g = groups[ct[1]] break end end
    if not g and (e.categories.Audio or e.categories.Video) then g = groups.AudioVideo end
    table.insert((g or other).entries, e)
  end
  local out = {}
  for _, g in ipairs(order) do if #g.entries > 0 then table.insert(out, g) end end
  table.sort(out, function(a, b) return a.title < b.title end)
  if #other.entries > 0 then table.insert(out, other) end
  return out
end

return A
