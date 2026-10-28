-- The settings flyout ("Control Panel" in the start menu, "Properties" in the taskbar and sidebar menus; SHELL-DESIGN 6.3/6.4) and the
-- run / search box, both in the generic flyout chrome (flyout.lua: 1175 frame, Options row, white body like powerflyout.xaml).
--   lh.settings_flyout([x])  two columns: the theme list (registry, then the user's presets; Save as preset...), composition
--                            (follow / on / off), taskbar (avalon / classic / none, M8) and its autohide, start menu (flxp / lux),
--                            sidebar mode, wallpaper
--   lh.save_preset_box()     name box for "Save as preset...": writes themes/NAME.theme (SETTINGS.md) and selects it
--   lh.run_box("run" | "search")  a one-line command box (Run: the line goes to sh; Search: the best matching application,
--                            else an existing path through the file manager, else the line as a command)
-- The settings themselves live in runtime.lua (lh.shell_get / lh.shell_set, lh.select_theme) and settings.ini (session.lua).
return function(ctx)
  local S, A = lh.session, ctx.apps
  local N = lh.node
  local screen_w, screen_h = ctx.screen_w, ctx.screen_h
  local F = "Segoe UI,Tahoma 12px"
  local function taskbar_top() return lh.taskbar_shown_top and lh.taskbar_shown_top() or screen_h - 30 end

  ------------------------------------------------------------------ settings
  local anchor_x
  function lh.settings_flyout(x)
    if x then anchor_x = x end
    local owner = { focusable = {} }
    local focus = 1
    local function paint() for i, f in ipairs(owner.focusable) do f.hl:set{ visible = i == focus } end if lh.flyout then lh.flyout.surface:damage() end end
    function owner:flyout()
      local CW = 230; local W = 2 * CW
      local root = N{ layout = "fill", w = W }
      root:add(N{ kind = "rect", fill = "#ffffff" })
      local cols = root:add(N{ layout = "hbox" })
      local left, right = cols:add(N{ layout = "vbox", w = CW }), cols:add(N{ layout = "vbox", w = CW })
      local col = left
      local function heading(t) col:add(N{ kind = "text", text = t, font = "Segoe UI,Tahoma Bold 14px", color = "#808080", margin = { 8, 8, 8, 4 } }) end
      local function line() col:add(N{ h = 9, layout = "fill", margin = { 8, 6, 8, 2 } }):add(N{ kind = "rect", fill = "#c0c0c0", h = 1 }) end
      -- a row with a radio dot or a check box (or a plain command: kind "action"); act() applies it, then the flyout is rebuilt
      local function option(kind, text, on, act)
        local r = col:add(N{ layout = "fill", h = 20, margin = { 6, 0, 6, 0 }, hoverable = true })
        local hl = r:add(N{ kind = "rect", fill = "#c5e7f6", radius = 2, visible = false })
        local d = r:add(N{ layout = "dock" })
        local mark = d:add(N{ dock = "left", w = 20, layout = "canvas" })
        if kind == "radio" then
          mark:add(N{ kind = "rect", x = 4, y = 4, w = 11, h = 11, radius = 5.5, fill = "#ffffff", stroke = "#808080" })
          if on then mark:add(N{ kind = "rect", x = 7, y = 7, w = 5, h = 5, radius = 2.5, fill = "#2a5bd7" }) end
        elseif kind == "check" then
          mark:add(N{ kind = "rect", x = 4, y = 4, w = 12, h = 12, fill = "#ffffff", stroke = "#808080" })
          if on then mark:add(N{ kind = "text", text = "✓", font = "Sans Bold 11px", color = "#2a5bd7", x = 5, y = 2, w = 12, h = 14 }) end
        end
        d:add(N{ kind = "text", text = text, font = F, color = kind == "action" and "#00007f" or "#000000", dock = "fill", valign = "center", ellipsis = true })
        local idx = #owner.focusable + 1
        owner.focusable[idx] = { hl = hl, act = act }
        r.on_hover = function(self, over) if over then focus = idx; paint() end end
        r.on_click = function(self, b) if b == 1 then act() end end
      end
      local function refresh() lh.timer(function() lh.settings_flyout() end, 0) end
      local function later() lh.timer(function() lh.settings_flyout() end, 300) end   -- after a switch that closed it (event "pack")
      heading("Theme")
      local cur = lh.theme_name and lh.theme_name() or nil
      for _, e in ipairs(S.themes()) do
        option("radio", e.title or e.name, e.name == cur, function() lh.select_theme(e.name); later() end)
      end
      -- user presets (SETTINGS.md themes/NAME.theme): a registry entry with overrides
      local presets = S.presets()
      if #presets > 0 then
        line(); heading("Your themes")
        for _, e in ipairs(presets) do
          option("radio", e.title .. "  (" .. e.base .. ")", e.name == cur, function() lh.select_theme(e.name); later() end)
        end
      end
      line()
      option("action", "Save as preset...", false, function() lh.close_flyout(); lh.save_preset_box() end)
      col = right
      -- the compositor rule (runtime.lua lh.compositor): follow the theme's `composition`, or always on / off. Composition, taskbar
      -- and start menu are stored for the current theme ([override:NAME] or the preset, as Display Properties does; SETTINGS.md)
      heading("Composition")
      for _, m in ipairs{ { "follow", "Follow the theme" }, { "on", "Always on" }, { "off", "Always off" } } do
        option("radio", m[2], lh.composition_mode() == m[1], function() lh.set_composition_mode(m[1]); refresh() end)
      end
      line(); heading("Taskbar")
      -- M8: which bottom bar runs (settings.ini [taskbar] kind); classic = flxp-taskbar, started and stopped by lux
      for _, m in ipairs{ { "avalon", "Longhorn (Avalon) taskbar" }, { "classic", "Classic taskbar (flxp)" }, { "none", "No taskbar" } } do
        option("radio", m[2], lh.shell_get("taskbar") == m[1], function() lh.shell_set("taskbar", m[1]); later() end)
      end
      option("check", "Auto-hide the taskbar", lh.shell_get("autohide"), function() lh.shell_set("autohide", not lh.shell_get("autohide")); refresh() end)
      line(); heading("Start menu")
      -- [start] menu: flxp's start panel (flxp-taskbar is the provider; --start-menu-only next to the Avalon taskbar) or ours
      for _, m in ipairs{ { "flxp", "Start panel (flxp)" }, { "lux", "Longhorn start menu (lux)" } } do
        option("radio", m[2], lh.shell_get("start_menu") == m[1], function() lh.shell_set("start_menu", m[1]); later() end)
      end
      line(); heading("Sidebar")
      for _, m in ipairs{ { "full", "Full" }, { "minimized", "Minimized (in the taskbar)" }, { "pinned", "Pinned tiles only" } } do
        option("radio", m[2], lh.shell_get("sidebar") == m[1], function() lh.shell_set("sidebar", m[1]); refresh() end)
      end
      -- [sidebar] stacking (Win+S toggles it): top = above the windows with its strut, full height, the taskbar yields (4074);
      -- bottom = part of the desktop: under the windows, no strut, ending at the full-width taskbar's top (M4)
      for _, m in ipairs{ { "top", "Sidebar on top of windows" }, { "bottom", "Sidebar under windows (desktop)" } } do
        option("radio", m[2], lh.shell_get("sidebar_stacking") == m[1], function() lh.shell_set("sidebar_stacking", m[1]); later() end)
      end
      -- [sidebar] with_classic: off (default) = no sidebar next to the classic taskbar (flxp-taskbar has the tray and the clock)
      option("check", "Also with the classic taskbar", lh.shell_get("sidebar_with_classic"), function()
        lh.shell_set("sidebar_with_classic", not lh.shell_get("sidebar_with_classic")); later() end)
      line(); heading("Desktop")
      option("check", "Show the wallpaper", lh.shell_get("wallpaper"), function() lh.shell_set("wallpaper", not lh.shell_get("wallpaper")); refresh() end)
      left:add(N{ h = 8 }); right:add(N{ h = 8 })
      local _, h1 = left:desired(CW, 2000); local _, h2 = right:desired(CW, 2000)
      local h = math.max(h1, h2)
      lh.timer(paint, 0)
      return root, W, h
    end
    function owner:place(fw, fh) local x = anchor_x or 8; return math.max(0, math.min(x, screen_w - fw)), math.max(0, taskbar_top() - fh - 4) end
    function owner:on_key(name)
      local n = #self.focusable
      if name == "Down" or name == "Tab" then focus = focus % n + 1; paint()
      elseif name == "Up" or name == "ISO_Left_Tab" then focus = (focus + n - 2) % n + 1; paint()
      elseif name == "Return" or name == "KP_Enter" or name == "space" then local f = self.focusable[focus]; if f then f.act() end end
    end
    owner.is_settings = true
    lh.open_flyout(owner)
  end

  ------------------------------------------------------------------ save as preset (SETTINGS.md themes/NAME.theme)
  -- a name box; Enter writes $XDG_CONFIG_HOME/longhorn/themes/<file name>.theme with the current effective values (base = the
  -- registry theme in use, or the current preset's base) and selects it. The file name is the typed name in lower case with
  -- anything but letters, digits, '-' and '_' turned into '-'; a registry name gets "-custom" appended (the registry wins).
  function lh.save_preset_box()
    local owner = { text = "" }
    local entry, msg
    local function update() entry:set{ text = owner.text .. "|" } if lh.flyout then lh.flyout.surface:damage() end end
    function owner:flyout()
      local W = 300
      local root = N{ layout = "fill", w = W }
      root:add(N{ kind = "rect", fill = "#ffffff" })
      local col = root:add(N{ layout = "vbox" })
      col:add(N{ kind = "text", text = "Save as preset", font = "Segoe UI,Tahoma Bold 14px", color = "#808080", margin = { 8, 8, 8, 2 } })
      local _, e = lh.theme_name()
      col:add(N{ kind = "text", text = "A name for the current look (" .. (e and (e.title or e.name) or "?") .. ", wallpaper, composition):", font = "Segoe UI,Tahoma 11px", color = "#606060", wrap = true, w = W, margin = { 8, 0, 8, 6 } })
      local box = col:add(N{ layout = "fill", h = 22, margin = { 8, 0, 8, 0 } })
      box:add(N{ kind = "rect", fill = "#ffffff", stroke = "#7f9db9" })
      entry = box:add(N{ kind = "text", text = owner.text .. "|", font = F, color = "#000000", valign = "center", margin = { 4, 0, 4, 0 }, ellipsis = true })
      msg = col:add(N{ kind = "text", text = "Enter saves, Esc closes", font = "Segoe UI,Tahoma 11px", color = "#808080", margin = { 8, 6, 8, 8 } })
      local _, h = root:desired(W, 600)
      return root, W, h
    end
    function owner:place(fw, fh) return math.max(0, math.min(anchor_x or 8, screen_w - fw)), math.max(0, taskbar_top() - fh - 4) end
    function owner:on_key(name, text)
      if name == "BackSpace" then owner.text = owner.text:gsub("[%z\1-\127\194-\244][\128-\191]*$", "")
      elseif name == "Return" or name == "KP_Enter" then
        local title = owner.text:gsub("^%s+", ""):gsub("%s+$", ""); if title == "" then return end
        local file = title:lower():gsub("[^%w%-_]", "-"):gsub("%-+", "-"):gsub("^%-", ""):gsub("%-$", "")
        if file == "" then file = "preset" end
        if S.lookup(file) then file = file .. "-custom" end
        local _, e = lh.theme_name(); if not e then return end
        local v = lh.effective_theme(e)
        local vals = { name = file, title = title, base = e.base, msstyles = v.msstyles, color = v.color, size = v.size,
          composition = v.composition, animations = v.animations, wallpaper = v.wallpaper, mode = v.mode, icons = v.icons,
          startpanel = v.startpanel, fonts = v.fonts }
        -- a pack of the data directories is saved by name (packs/NAME), anything else as its path
        local pn = v.shell:match("/packs/([^/]+)/*$")
        vals.shell = (pn and S.pack_dir(pn) == v.shell) and pn or v.shell
        if e.over and e.over.frame then vals.frame = v.frame end
        if e.over and e.over.frame_classic then vals.frame_classic = v.frame_classic end
        -- the theme's taskbar / start menu ([override:NAME], ...) when they differ from what the base gives (SETTINGS.md Save As)
        local b = S.resolve(e.base)
        for _, k in ipairs{ "taskbar", "start" } do local tv = S.theme_value(e, k); if tv ~= S.theme_default(b, k) then vals[k] = tv end end
        local path = S.save_preset(vals)
        lh.close_flyout()
        if not path then io.stderr:write("lux: cannot write the preset " .. file .. "\n") return end
        io.stderr:write("lux: saved preset " .. path .. "\n")
        lh.select_theme(file, true)
        return
      elseif text ~= "" then owner.text = owner.text .. text end
      update()
    end
    lh.open_flyout(owner)
  end

  ------------------------------------------------------------------ run / search box
  function lh.run_box(mode)
    local owner = { text = "" }
    local entry, hint, matches
    local function best()
      local t = owner.text:lower(); if t == "" then return {} end
      local out = {}
      for _, e in ipairs(A.list()) do
        local n = e.name:lower()
        if n:sub(1, #t) == t then table.insert(out, 1, e) elseif n:find(t, 1, true) or (e.generic or ""):lower():find(t, 1, true) then table.insert(out, e) end
        if #out >= 12 then break end
      end
      return out
    end
    local function update()
      entry:set{ text = owner.text .. "|" }
      if mode == "search" then
        local m = best(); local names = {}
        for i = 1, math.min(4, #m) do names[i] = m[i].name end
        matches:set{ text = #names > 0 and table.concat(names, "\n") or (owner.text ~= "" and "(no program: Enter runs it as a command)" or "") }
      end
      if lh.flyout then lh.flyout.surface:damage() end
    end
    function owner:flyout()
      local W = 300
      local root = N{ layout = "fill", w = W }
      root:add(N{ kind = "rect", fill = "#ffffff" })
      local col = root:add(N{ layout = "vbox" })
      col:add(N{ kind = "text", text = mode == "search" and "Search" or "Run", font = "Segoe UI,Tahoma Bold 14px", color = "#808080", margin = { 8, 8, 8, 2 } })
      col:add(N{ kind = "text", text = mode == "search" and "Type the name of a program, folder or command." or "Type the name of a program, folder, document or command.", font = "Segoe UI,Tahoma 11px", color = "#606060", wrap = true, w = W, margin = { 8, 0, 8, 6 } })
      local box = col:add(N{ layout = "fill", h = 22, margin = { 8, 0, 8, 0 } })
      box:add(N{ kind = "rect", fill = "#ffffff", stroke = "#7f9db9" })
      entry = box:add(N{ kind = "text", text = "|", font = F, color = "#000000", valign = "center", margin = { 4, 0, 4, 0 }, ellipsis = true })
      if mode == "search" then matches = col:add(N{ kind = "text", text = "", font = F, color = "#00025c", wrap = true, w = W, h = 64, margin = { 10, 6, 8, 0 } }) end
      hint = col:add(N{ kind = "text", text = "Enter runs, Esc closes", font = "Segoe UI,Tahoma 11px", color = "#808080", margin = { 8, 6, 8, 8 } })
      local _, h = root:desired(W, 600)
      return root, W, h
    end
    function owner:place(fw, fh) return 8, math.max(0, taskbar_top() - fh - 4) end
    function owner:on_key(name, text)
      if name == "BackSpace" then owner.text = owner.text:gsub("[%z\1-\127\194-\244][\128-\191]*$", "")
      elseif name == "Return" or name == "KP_Enter" then
        local t = owner.text:gsub("^%s+", ""):gsub("%s+$", ""); if t == "" then return end
        lh.close_flyout()
        local m = mode == "search" and best()[1] or nil
        if m then A.launch(m)
        elseif t:sub(1, 1) == "/" or t:sub(1, 1) == "~" then
          local p = t:gsub("^~", os.getenv("HOME") or "~")
          local fm = S.get("commands", "filemanager") or (lh.prog("flxp") ~= "flxp" and lh.prog("flxp") or A.in_path("flxp") and "flxp" or A.in_path("rox") and "rox" or "xdg-open")
          lh.spawn((lh.list_dir(p) and fm or "xdg-open") .. " " .. S.quote(p))
        else lh.spawn(t) end
        return
      elseif text ~= "" then owner.text = owner.text .. text end
      update()
    end
    lh.open_flyout(owner)
  end
end
