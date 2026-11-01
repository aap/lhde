-- Quick Launch tile: 4074 QuickLaunchTile (WCPClient; 3683-4039 FolderViewTile): an ItemsControl over the Quick Launch folder
-- (Application Data\Microsoft\Internet Explorer\Quick Launch) in a FlowPanel, item tooltips on (ItemControlToolTipEnabled), tile
-- TooltipText = the folder's display name, tile icon imageres 1021 (the lightning). No flyout, no properties (the class has
-- Initialize and .ctor only), MultiInstance 0. Look (4051-vm-now.png, lh4074-193747.png: IE and mail icons): one row of 16 px
-- icons, 28 px pitch, the first at 12,9 in the tile (the tray tile's geometry, tiles/notifications.lua); the tile is 35 high with
-- its sizer (separators 35 rows apart: 4051 y 17/52, 4074 y 97/132); the FlowPanel wraps to further rows (24 px apart,
-- inferred) when the bar is too narrow (BetaWiki 4088: one icon per row on a narrow bar).
-- The folder: a directory of .desktop files, settings.ini [quicklaunch] dir, default ~/.config/lhde/quicklaunch/. When it does not
-- exist it is created once with copies of the browser, the mail client, the file manager (flxp) and a terminal (4074's folder held
-- IE and Outlook Express); an emptied folder stays empty. Shortcuts are shown in file-name order; the folder is re-read when it
-- changes (5 s poll). A click launches (apps.lua A.launch: field codes dropped, Terminal=true in xterm, counted in the MFU list).
local tile = { name = "Quick Launch", height = 35, tooltip = "Quick Launch" }
local ICON, PITCH, ORIGIN = 16, { 28, 24 }, { 5, 6 }

local exists = lh.exists
function tile.dir()
  local d = lh.session.get("quicklaunch", "dir")
  if d then d = d:gsub("^~", os.getenv("HOME") or "~") end
  return d or lh.config_file("quicklaunch")
end
-- first run: the folder with a few standard shortcuts (copies, so the user can edit or delete them)
local function populate(dir, A)
  os.execute("mkdir -p " .. lh.session.quote(dir))
  local function copy(e, name)
    local src = io.open(e.path, "r"); if not src then return end
    local body = src:read("a"); src:close()
    local f = io.open(dir .. "/" .. (name or e.id), "w"); if f then f:write(body); f:close() end
  end
  local done = {}
  local function add(id) local e = id and A.get(id); if e and not done[id] then done[id] = true; copy(e) return true end end
  add(A.default_browser())
  add(A.default_mail())
  if not add("flxp.desktop") then
    local p = lh.prog("flxp"):gsub("^'(.*)'$", "%1")
    if p ~= "flxp" or A.in_path("flxp") then
      local f = io.open(dir .. "/flxp.desktop", "w")
      if f then f:write("[Desktop Entry]\nType=Application\nName=Explorer\nGenericName=File Manager\nExec=" .. p .. " %F\nIcon=system-file-manager\nCategories=System;FileManager;\n"); f:close() end
    end
  end
  local term
  for _, id in ipairs{ "xterm.desktop", "org.gnome.Terminal.desktop", "org.kde.konsole.desktop", "xfce4-terminal.desktop", "Alacritty.desktop", "foot.desktop", "uxterm.desktop" } do
    if A.get(id) then term = id break end
  end
  if not term then for _, e in ipairs(A.list()) do if e.categories.TerminalEmulator then term = e.id break end end end
  add(term)
end
function tile.entries()
  local A, dir = lh.apps, tile.dir()
  if not A then return {} end
  if not lh.list_dir(dir) and not lh.session.get("quicklaunch", "dir") then populate(dir, A) end
  local names = lh.list_dir(dir) or {}
  table.sort(names, function(a, b) return a:lower() < b:lower() end)
  local out = {}
  for _, n in ipairs(names) do
    if n:match("%.desktop$") then
      local e = A.parse(dir .. "/" .. n, n)
      if e and not e.hidden and (not e.tryexec or A.in_path(e.tryexec)) then out[#out + 1] = e end
    end
  end
  return out
end

-- one icon button: the entry's icon (icon theme / path, apps.lua A.icon), else imageres 1056 (the sidebar's default icon)
local function icon_button(parent, e, x, y, R)
  local img = lh.apps.icon(e.icon, ICON) or R(1056)
  local b = parent:add(lh.node{ kind = "button", x = x, y = y, w = ICON, h = ICON, layout = "fill" })
  b:add(lh.node{ kind = "image", image = img, w = ICON, h = ICON, filter = "best" })
  b.on_click = function(_, button) if button == 1 then lh.apps.launch(e) end end
  lh.tooltip(b, e.comment and e.comment ~= "" and (e.name .. "\n" .. e.comment) or e.name)
  return b
end

function tile:fgw() return math.max(40, (self.sb and self.sb.w or 150) - 16) end
function tile:build()
  local fg = self.foreground; fg:clear()
  local c = fg:add(lh.node{ layout = "canvas" })
  local list = self.list or {}
  local W = self:fgw()
  local per = math.max(1, math.floor((W - ORIGIN[1] - ICON) / PITCH[1]) + 1)
  for i, e in ipairs(list) do
    local k = i - 1
    icon_button(c, e, ORIGIN[1] + (k % per) * PITCH[1], ORIGIN[2] + (k // per) * PITCH[2], self.R)
  end
  self.built_w = self.sb and self.sb.w
  local rows = math.max(1, math.ceil(#list / per))
  return 35 + (rows - 1) * PITCH[2]
end
function tile:relayout(now) local h = self:build(); if self.sb and self.sb.resize_tile then self.sb.resize_tile(self, h, now) end end
local function stamp(dir)
  local s = { tostring(lh.mtime(dir)) }
  for _, n in ipairs(lh.list_dir(dir) or {}) do s[#s + 1] = n .. "@" .. tostring(lh.mtime(dir .. "/" .. n)) end
  return table.concat(s, ";")
end
function tile:initialize()
  self.list = tile.entries(); self.stamp = stamp(tile.dir())
  self:relayout(true)
  self.on_width = function(w) if self.built_w ~= w then self:relayout(true) end end
  lh.on("sidebar-resize", self.on_width)
  self.timer = lh.timer(function()
    local s = stamp(tile.dir())
    if s ~= self.stamp then self.stamp = s; self.list = tile.entries(); self:relayout() end
  end, 5000, true)
end
-- tile menu: the folder (4074 had no entries of its own; "Open Quick Launch folder" is ours)
function tile:menu()
  return { { text = "&Open Quick Launch folder", action = function()
    local fm = lh.session.get("commands", "filemanager") or (lh.prog("flxp") ~= "flxp" and lh.prog("flxp") or "xdg-open")
    lh.spawn(fm .. " " .. lh.session.quote(tile.dir()))
  end } }
end
-- the minimized sidebar's taskbar view (4074 TaskbarTiles holds Quick Launch): the icons in one row, 20 px apart as the tray's,
-- centred in the bar
function tile:taskbar(host)
  local list = tile.entries()
  local bh = self.T.taskbar and self.T.taskbar.height or 30
  local w = 4 + #list * 20
  local c = host:add(lh.node{ layout = "canvas", w = w, h = "100%" })
  for i, e in ipairs(list) do icon_button(c, e, 4 + (i - 1) * 20, math.floor((bh - ICON) / 2), self.R) end
end
function tile:dispose()
  if self.timer then lh.cancel(self.timer) end
  if self.on_width then lh.off("sidebar-resize", self.on_width) end
end
return tile
