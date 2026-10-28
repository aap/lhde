-- Start menu (SHELL-DESIGN 6.3): the XP start panel (explorer/desktop2: deskhost, sfthost, proglist, specfldr, moreprog,
-- logoff, userpane) skinned with the msstyles StartPanel class, as one override-redirect popup (_NET_WM_WINDOW_TYPE_MENU)
-- holding the pointer and keyboard while open.
--   pane layout (measured on 4074-dwm.png, Jade/Aero, and lh4074-181541.png, Slate; numbers in themes/*/shell.lua startmenu):
--   [USERPANE (Slate only: Jade's StartPanel has no USERPANE part)]
--   [PROGLIST: pinned (title bold + subtitle), separator, MFU] [PLACESLIST: places, separators]
--   [MOREPROGRAMS: "All Programs" + arrow                    ]
--   [LOGOFF: Log Off, Stand by / Shut down, right-aligned                                      ]
--   Item metrics from sfthost.cpp _ComputeTileMetrics: tile = max(icon, text lines) + 4*SM_CYEDGE; separators 4*cySep.
-- Opened by the StartButton (pressed image 1181 while open) and by Super alone (lhwm's _LHSHELL_START root message).
return function(ctx)
  local T, A, S = ctx.T, ctx.apps, lh.session
  local screen_w, screen_h = ctx.screen_w, ctx.screen_h
  local sm = { open = false }
  sm.surface = lh.surface{ name = "lhshell-startmenu", popup = true, type = "menu", x = 0, y = 0, w = 10, h = 10 }
  sm.root = lh.node{ layout = "canvas" }
  sm.surface:root(sm.root)
  local N = lh.node
  local function C() return T.startmenu or {} end
  local function bold(font) local f = font:gsub(" Bold", ""); local face, size = f:match("^(.-)%s+(%S+)$"); return face and (face .. " Bold " .. size) or (f .. " Bold") end
  local function shell32(id, size) return string.format("4074/shell32/%d/%d.png", size or 24, id) end
  local function file_manager(dir)
    local fm = S.get("commands", "filemanager")
    if not fm then fm = lh.prog("flxp") ~= "flxp" and lh.prog("flxp") or A.in_path("flxp") and "flxp" or A.in_path("rox") and "rox" or "xdg-open" end
    lh.spawn(fm .. " " .. S.quote(dir))
  end

  ------------------------------------------------------------------ content
  local function pinned_ids()
    local v = S.get("start", "pinned")
    if v then local t = {}; for id in v:gmatch("[^;]+") do id = id:gsub("^%s+", ""):gsub("%s+$", ""); if id ~= "" then table.insert(t, id) end end return t end
    local t = {}; local b, m = A.default_browser(), A.default_mail()
    if b then table.insert(t, b) end; if m and m ~= b then table.insert(t, m) end
    return t
  end
  local default_icon32 = shell32(3, 32)
  local function app_icon(e, size) return A.icon(e.icon, size) or (size == 32 and default_icon32 or shell32(3, size)) end
  local function left_items()
    local items, skip = {}, {}
    local browser, mail = A.default_browser(), A.default_mail()
    for _, id in ipairs(pinned_ids()) do
      local e = A.get(id)
      if e then
        skip[id] = true
        local title, sub = e.name, nil
        if id == browser then title, sub = "Internet", e.name elseif id == mail then title, sub = "E-mail", e.name end
        table.insert(items, { text = title, sub = sub, bold = true, icon = app_icon(e, 32), action = function() A.launch(e) end })
      end
    end
    local mfu = A.mfu(tonumber(S.get("start", "mfu_count") or "") or C().mfu or 6, skip)
    if #items > 0 and #mfu > 0 then table.insert(items, { separator = true }) end
    for _, e in ipairs(mfu) do table.insert(items, { text = e.name, icon = app_icon(e, 32), action = function() A.launch(e) end }) end
    return items
  end
  local function run_command()
    local r = S.get("commands", "run") or (A.in_path("rofi") and "rofi -show run")
    if r then lh.spawn(r) else lh.run_box("run") end
  end
  local function right_items()
    return {
      { text = "Documents", bold = true, icon = shell32(348), action = function() file_manager(A.user_dir("DOCUMENTS")) end },
      { text = "Pictures", bold = true, icon = shell32(387), action = function() file_manager(A.user_dir("PICTURES")) end },
      { text = "Music", bold = true, icon = shell32(386), action = function() file_manager(A.user_dir("MUSIC")) end },
      { text = "Computer", bold = true, icon = shell32(16), action = function() file_manager("/") end },
      { separator = true },
      { text = "Control Panel", icon = shell32(137), action = function() lh.settings_flyout() end },
      { separator = true },
      { text = "Search", icon = shell32(23), action = function() local s = S.get("commands", "search"); if s then lh.spawn(s) else lh.run_box("search") end end },
      { text = "Run...", icon = shell32(25), action = run_command },
    }
  end
  -- the bottom row: Log Off + Stand by (Jade, 4074-dwm.png) or Shut down (Slate, lh4074-181541.png); every one asks first
  -- (the three-button session flyout, 6.2) with the clicked action preselected. LOGOFFBUTTONS strip: 0 eject, 1 log off, 2 power.
  local logoff_defs = { logoff = { "Log Off", 1 }, standby = { "Stand by", 2 }, shutdown = { "Shut down", 2 }, turnoff = { "Turn Off", 2 } }

  ------------------------------------------------------------------ build
  local rows   -- { col = "left"|"right"|"bottom"|"more", node, hl, labels = {...}, item }
  local sel    -- index into rows
  local function paint_sel()
    local c = C()
    for i, r in ipairs(rows) do
      local on = i == sel
      if r.hl then r.hl:set{ visible = on and not r.custom_hot } end
      for _, l in ipairs(r.labels or {}) do l.node:set{ color = on and not r.custom_hot and (c.hot_text or "#000000") or l.color } end
      if r.on_sel then r.on_sel(on) end
    end
    sm.surface:damage()
  end
  local function set_sel(i) if sel ~= i then sel = i; paint_sel() end end
  local more_timer
  local function activate(i)
    local r = rows[i]; if not r then return end
    if r.col == "more" then sm.all_programs(true) return end
    local it = r.item
    if it.keep_open then it.action() return end
    sm.hide()
    if it.action then it.action() end
  end

  local function build()
    local P = lh.startpanel(); local c = C()
    sm.root:clear(); rows = {}; sel = nil
    local W, split = c.width or 380, c.left_w or 190
    local font = c.font or T.font; local fbold = bold(font)
    local left, right = left_items(), right_items()
    local prog_tile, place_h, sep_left, sep_right = c.prog_tile or 40, c.places_h or 36, c.sep_left or 8, c.sep_right or 4
    local pm, plm = P.margins("StartPanel.PROGLIST", "CONTENTMARGINS"), P.margins("StartPanel.PLACESLIST", "CONTENTMARGINS")
    local function list_h(items, tile, seph) local h = 0; for _, it in ipairs(items) do h = h + (it.separator and seph or tile) end return h end
    local more_h, logoff_h = c.more_h or 34, c.logoff_h or 40
    local up_h = (c.userpane ~= false and P.has("StartPanel.USERPANE")) and (c.userpane_h or 64) or 0
    local up_pref = S.get("start", "userpane")
    if up_pref == "0" then up_h = 0 elseif up_pref == "1" and up_h == 0 then up_h = c.userpane_h or 64 end
    local left_need = pm[3] + list_h(left, prog_tile, sep_left) + (c.bonus_sep or 3) + pm[4] + more_h
    local right_need = plm[3] + list_h(right, place_h, sep_right) + plm[4]
    local body = math.max(c.body_min or 0, left_need, right_need)
    local H = up_h + body + logoff_h
    sm.W, sm.H = W, H
    local root = sm.root
    local function pane(part, x, y, w, h)
      local img = P.image(part); if not img then return end
      root:add(N{ kind = "ninegrid", image = img, grid = P.grid(part), x = x, y = y, w = w, h = h })
    end
    -- user pane
    if up_h > 0 then
      if P.has("StartPanel.USERPANE") then pane("StartPanel.USERPANE", 0, 0, W, up_h) else root:add(N{ kind = "rect", fill = c.userpane_fill or "#202020", x = 0, y = 0, w = W, h = up_h }) end
      local pic = A.to_png((os.getenv("HOME") or "") .. "/.face", 48)
      -- USERPICTURE is the frame plate (55x55, drawn first); the picture sits inside its CONTENTMARGINS (8 6 8 6)
      local px, py, ps = c.pic_x or 5, c.pic_y or 5, c.pic_size or 55
      local cm = P.has("StartPanel.USERPICTURE") and P.margins("StartPanel.USERPICTURE", "CONTENTMARGINS") or { 4, 4, 4, 4 }
      if P.has("StartPanel.USERPICTURE") then root:add(N{ kind = "ninegrid", image = P.image("StartPanel.USERPICTURE"), grid = P.grid("StartPanel.USERPICTURE"), x = px, y = py, w = ps, h = ps }) end
      root:add(N{ kind = "image", image = pic or shell32(269, 48), x = px + cm[1], y = py + cm[3], w = ps - cm[1] - cm[2], h = ps - cm[3] - cm[4] })
      root:add(N{ kind = "text", text = os.getenv("USER") or "user", font = c.user_font or P.font("StartPanel.USERPANE", "FONT", "Sans 19px"),
        color = P.color("StartPanel.USERPANE", "TEXTCOLOR", "#ffffff"), shadow = { dx = 2, dy = 2, color = P.color("StartPanel.USERPANE", "TEXTSHADOWCOLOR", "#000000") },
        x = c.user_x or 66, y = 0, w = W - (c.user_x or 66) - 8, h = up_h, valign = "center", ellipsis = true })
    end
    local by = up_h
    pane("StartPanel.PROGLIST", 0, by, split, body - more_h)
    pane("StartPanel.PLACESLIST", split, by, W - split, body)
    pane("StartPanel.MOREPROGRAMS", 0, by + body - more_h, split, more_h)
    pane("StartPanel.LOGOFF", 0, by + body, W, logoff_h)
    local hot_bg = c.hot_bg or (T.menu and T.menu.hot) or "#c5e7f6"
    local function row(col, it, x, y, w, h)
      local n = root:add(N{ layout = "fill", x = x, y = y, w = w, h = h, hoverable = true })
      local hl = n:add(N{ kind = "rect", fill = hot_bg, radius = c.hot_radius or 2, visible = false })
      local r = { col = col, node = n, hl = hl, labels = {}, item = it }
      table.insert(rows, r); local idx = #rows
      n.on_hover = function(self, over) if over then sm.mouse = true; set_sel(idx); sm.mouse = false end end
      n.on_click = function(self, button) if button == 1 then activate(idx) end end
      return r, n
    end
    -- program list (left)
    local x0, y = pm[1], by + pm[3] + (c.prog_dy or 0)
    local lw = split - pm[1] - pm[2]
    local tc, sub = P.color("StartPanel.PROGLIST", "TEXTCOLOR", "#ffffff"), P.color("StartPanel.PROGLIST", "CAPTIONTEXT", "#b4b4b4")
    for _, it in ipairs(left) do
      if it.separator then
        local sp = P.image("StartPanel.PROGLISTSEPARATOR")
        if sp then root:add(N{ kind = "image", image = sp, x = c.sep_left_x or 31, y = y + math.floor((sep_left - 2) / 2) + 1 }) end
        y = y + sep_left
      else
        local r, n = row("left", it, x0, y, lw, prog_tile)
        local d = n:add(N{ layout = "canvas" })
        d:add(N{ kind = "image", image = it.icon, x = 2, y = math.floor((prog_tile - 32) / 2), w = 32, h = 32 })
        local tx = c.prog_text_x or 41
        if it.sub then
          local t1 = d:add(N{ kind = "text", text = it.text, font = fbold, color = tc, x = tx, y = math.floor(prog_tile / 2) - 16 + 1, w = lw - tx - 2, h = 16, ellipsis = true })
          local t2 = d:add(N{ kind = "text", text = it.sub, font = font, color = sub, x = tx, y = math.floor(prog_tile / 2), w = lw - tx - 2, h = 16, ellipsis = true })
          r.labels = { { node = t1, color = tc }, { node = t2, color = sub } }
        else
          local t = d:add(N{ kind = "text", text = it.text, font = it.bold and fbold or font, color = tc, wrap = true, x = tx, y = 0, w = lw - tx - 2, h = prog_tile, valign = "center" })
          r.labels = { { node = t, color = tc } }
        end
        y = y + prog_tile
      end
    end
    -- the bonus separator above More Programs (sfthost _DrawSeparators)
    local sp = P.image("StartPanel.PROGLISTSEPARATOR")
    if sp then root:add(N{ kind = "image", image = sp, x = c.sep_left_x or 31, y = by + body - more_h - (c.bonus_sep_y or 3) }) end
    -- All Programs
    do
      local my = by + body - more_h
      local mf = P.font("StartPanel.MOREPROGRAMS", "FONT", fbold)
      local mc = P.color("StartPanel.MOREPROGRAMS", "TEXTCOLOR", "#ffffff")
      local probe = N{ kind = "text", text = "All Programs", font = mf }; local holder = root:add(N{ visible = false }); holder:add(probe)
      local tw = probe:measure(); holder:clear()
      local r, n = row("more", nil, 0, my, split, more_h)
      r.custom_hot = true
      local d = n:add(N{ layout = "canvas" })
      local tx = c.more_text_x or 48
      local t = d:add(N{ kind = "text", text = "All Programs", font = mf, color = mc, x = tx, y = 0, w = tw + 2, h = more_h - (c.more_text_dy or 4), valign = "center" })
      local arrow, ahot = P.image("StartPanel.MOREPROGRAMSARROW"), P.image("StartPanel.MOREPROGRAMSARROW(HOT)")
      local a = arrow and d:add(N{ kind = "image", image = arrow, x = tx + math.ceil(tw) + (c.more_gap or 4), y = math.floor((more_h - (c.more_text_dy or 4) - 19) / 2) })
      r.labels = {}
      r.on_sel = function(on) if a then a:set{ image = on and ahot or arrow } end t:set{ color = on and P.color("StartPanel.MOREPROGRAMS", "HOTTRACKING", mc) or mc } end
    end
    -- places (right)
    x0, y = split + plm[1], by + plm[3] + (c.places_dy or 0)
    local rw = W - split - plm[1] - plm[2]
    local pc = P.color("StartPanel.PLACESLIST", "TEXTCOLOR", "#646464")
    for _, it in ipairs(right) do
      if it.separator then
        local ps = P.image("StartPanel.PLACESLISTSEPARATOR")
        if ps then root:add(N{ kind = "image", image = ps, x = split + (c.sep_right_x or 12), y = y + math.floor(sep_right / 2) - 1 }) end
        y = y + sep_right
      else
        local r, n = row("right", it, x0, y, rw, place_h)
        local d = n:add(N{ layout = "canvas" })
        d:add(N{ kind = "image", image = it.icon, x = c.place_icon_x or 5, y = math.floor((place_h - 24) / 2), w = 24, h = 24 })
        local col = it.bold and (c.places_bold_color or pc) or pc
        local t = d:add(N{ kind = "text", text = it.text, font = it.bold and fbold or font, color = col, x = c.place_text_x or 33, y = 0, w = rw - (c.place_text_x or 33), h = place_h, valign = "center", ellipsis = true })
        r.labels = { { node = t, color = col } }
        y = y + place_h
      end
    end
    -- log off row: right-aligned [glyph 24][6][text] buttons, 11 px apart, the last text ending 30 px from the edge
    do
      local ly = by + body
      local lb, lbh = P.image("StartPanel.LOGOFFBUTTONS"), P.image("StartPanel.LOGOFFBUTTONS(HOT)")
      local lc = P.color("StartPanel.LOGOFF", "TEXTCOLOR", "#ffffff")
      local defs = {}
      for _, k in ipairs(c.logoff_buttons or { "logoff", "standby" }) do
        local d = logoff_defs[k]; local t = c.logoff_text and c.logoff_text[k]
        table.insert(defs, { k, t and { t, d[2] } or d })
      end
      local holder = root:add(N{ visible = false }); local widths = {}
      for i, d in ipairs(defs) do local p = holder:add(N{ kind = "text", text = d[2][1], font = font }); widths[i] = math.ceil(p:measure()) end
      holder:clear()
      local total = 0; for i = 1, #defs do total = total + 24 + 6 + widths[i] + (i > 1 and (c.logoff_gap or 11) or 0) end
      local bx = W - (c.logoff_right or 30) - total
      for i, d in ipairs(defs) do
        local bw = 24 + 6 + widths[i]
        local r, n = row("bottom", { action = function() sm.confirm(d[1]) end, text = d[2][1] }, bx - 3, ly + (c.logoff_btn_y or 5), bw + 6, 26)
        r.hl:set{ fill = "#ffffff30" }
        local cell = n:add(N{ layout = "canvas" })
        local clipbox = cell:add(N{ layout = "canvas", x = 3, y = 1, w = 24, h = 24, clip = true })
        local g = clipbox:add(N{ kind = "image", image = lb, x = -24 * d[2][2], y = 0, w = 72, h = 24 })
        local t = cell:add(N{ kind = "text", text = d[2][1], font = font, color = lc, x = 3 + 24 + 6, y = 0, w = widths[i] + 2, h = 26, valign = "center" })
        r.labels = { { node = t, color = lc } }; r.custom_hot = true
        r.on_sel = function(on) g:set{ image = on and lbh or lb } t:set{ color = on and P.color("StartPanel.LOGOFF", "HOTTRACKING", lc) or lc } end
        bx = bx + bw + (c.logoff_gap or 11)
      end
    end
    -- outer frame (4074-dwm.png: 1 px black + 1 px #6b6b6b on every side; Slate: 1 px)
    for k, col in ipairs(c.frame or {}) do
      local o = k - 1
      root:add(N{ kind = "rect", stroke = col, x = o, y = o, w = W - 2 * o, h = H - 2 * o })
    end
  end

  ------------------------------------------------------------------ open / close
  local function taskbar_top() return lh.taskbar_shown_top and lh.taskbar_shown_top() or (screen_h - (T.taskbar and T.taskbar.height or 30)) end
  local function grab_keys()
    local ok, kb = sm.surface:grab(true)
    if not kb then   -- lhwm may still hold the keyboard for a moment after Super's release
      local tries = 0
      local function retry() tries = tries + 1; if not sm.open then return end local _, k = sm.surface:grab(true); if not k and tries < 20 then lh.timer(retry, 50) end end
      lh.timer(retry, 50)
    end
  end
  -- anchor (M8): the rect of the Start button that asked (root coordinates, from _LHSHELL_START): the menu opens above it (below
  -- it when the button is in the screen's upper half, a top-edge bar), its left edge at the button's; without one, bottom-left
  -- above our taskbar or the classic bar
  function sm.show(anchor)
    if sm.open then return end
    lh.close_flyout()
    build()
    sm.x, sm.y = 0, taskbar_top() - sm.H
    if anchor then
      sm.x = math.max(0, math.min(anchor.x, screen_w - sm.W))
      if anchor.y + anchor.h / 2 > screen_h / 2 then sm.y = anchor.y - sm.H else sm.y = anchor.y + anchor.h end
      sm.y = math.max(0, math.min(sm.y, screen_h - sm.H))
    end
    sm.surface:move(sm.x, sm.y, sm.W, sm.H); sm.surface:show(); sm.surface:raise()
    sm.open = true; sm.super_alone = false
    grab_keys()
    lh._emit("startmenu", true)
  end
  function sm.hide()
    if not sm.open then return end
    if more_timer then lh.cancel(more_timer); more_timer = nil end
    lh.menu.close()
    sm.surface:ungrab(); sm.surface:hide(); sm.open = false
    sm.root:clear(); rows = {}
    lh._emit("startmenu", false)
  end
  function sm.toggle(anchor) if sm.open then sm.hide() else sm.show(anchor) end end
  sm.surface.on_outside = function() sm.hide() end
  lh.on("surface-press", function(x, y, button, win)
    if not sm.open then return end
    if win == sm.surface:id() then   -- a press on the start menu itself closes the cascade unless it is on All Programs
      if lh.menu.is_open() then local r = rows[sel]; if not (r and r.col == "more") then lh.menu.close() end end
      return
    end
    if lh.menu.owns(win) then return end
    sm.hide()
  end)
  -- Start-menu provider protocol (SHELL-DESIGN "Start menu ownership, revised" / "M8 lhshell side"). _LHSHELL_START, format 32:
  -- l[0] = flags (bit 0: toggle), l[1..4] = anchor x, y, w, h in root coordinates (w or h 0: none). lhwm's Super broadcast has a
  -- timestamp in l[0] and no rect: it always means "toggle, no anchor". When a client owns _LH_STARTMENU_S<screen> (the provider),
  -- the Avalon Start button and every root _LHSHELL_START are forwarded to the owner window with the anchor (our button's rect
  -- when we have a taskbar); otherwise our StartPanel opens as before.
  -- start menu = lux (per theme: [override:NAME] start > the preset > themes.ini `start` > [start] menu; runtime.lua lh.start_pref):
  -- ours, even when a provider runs; flxp (default): the provider's when there is one
  local function provider()
    if (lh.start_pref and lh.start_pref() or S.get("start", "menu")) == "lux" then return nil end
    return lh.selection_owner(lh.startmenu_selection or "_LH_STARTMENU_S0")
  end
  local function forward(o, flags, a)
    lh.send_message(o, "_LHSHELL_START", flags, a and a.x or 0, a and a.y or 0, a and a.w or 0, a and a.h or 0)
    io.stderr:write(string.format("luxshell: start menu: forwarded to provider 0x%x (flags %d, anchor %s)\n", o, flags, a and string.format("%d,%d %dx%d", a.x, a.y, a.w, a.h) or "none"))
    lh._emit("start-forwarded", flags)   -- runtime.lua: the Start button's pressed look while the provider's menu is open
  end
  local function our_anchor() if not lh.start_anchor then return nil end local x, y, w, h = lh.start_anchor(); if not x then return nil end return { x = x, y = y, w = w, h = h } end
  lh.on("start-press", function() sm.suppress = sm.open end)      -- the press that closes the menu must not reopen it on release
  lh.on("start-click", function()
    if sm.suppress then sm.suppress = false return end
    local o = provider(); if o then forward(o, 1, our_anchor()) return end
    sm.toggle()
  end)
  lh.on("start-key", function(l0, x, y, w, h)
    local has = (w or 0) > 0 and (h or 0) > 0
    local flags = has and ((l0 or 0) & 0xff) or 1
    local anchor = has and { x = x, y = y, w = w, h = h } or nil
    local o = provider()
    if o then forward(o, flags, anchor or our_anchor()) return end
    -- (the root-message bridge for a classic flxp-taskbar that was no provider is gone: since flxp 6861d31 it owns
    -- _LH_STARTMENU_S<screen> with its own menu and ignores the root broadcast while _LHSHELL_S<screen> is owned)
    if flags & 1 == 1 then sm.toggle(anchor) elseif not sm.open then sm.show(anchor) end
  end)
  lh.on("pack", function() sm.hide() end)

  -- All Programs: the cascade opens to the right of the left column, bottom-aligned with the All Programs row
  function sm.all_programs(now)
    if lh.menu.is_open() then return end
    local items = {}
    local folder = shell32(4, 16)
    for _, g in ipairs(A.categories()) do
      table.insert(items, { text = g.title, icon = folder, submenu = function()
        local sub = {}
        for _, e in ipairs(g.entries) do table.insert(sub, { text = e.name, icon = A.icon(e.icon, 16) or shell32(3, 16), action = function() sm.hide(); A.launch(e) end }) end
        return sub
      end })
    end
    if #items == 0 then items = { { text = "(no applications)", disabled = true } } end
    local c = C()
    lh.menu.open(items, sm.x + (c.left_w or 190) - 2, sm.y + sm.H - (c.logoff_h or 40), { style = "start", up = true, select_first = now })
  end
  local function more_hover(on)
    if more_timer then lh.cancel(more_timer); more_timer = nil end
    if on then more_timer = lh.timer(function() more_timer = nil; if sm.open and rows[sel] and rows[sel].col == "more" then sm.all_programs() end end, 400) end
  end

  ------------------------------------------------------------------ keyboard (deskhost.cpp: arrows move inside a pane and across panes, typing jumps)
  local function col_rows(col) local t = {}; for i, r in ipairs(rows) do if r.col == col or (col == "left" and r.col == "more") then table.insert(t, i) end end return t end
  local function move(dir)
    if not sel then set_sel(col_rows("left")[1] or 1) return end
    local cur = rows[sel].col; if cur == "more" then cur = "left" end
    local list = col_rows(cur)
    local k; for j, i in ipairs(list) do if i == sel then k = j end end
    if dir == "up" or dir == "down" then
      k = k + (dir == "down" and 1 or -1)
      if cur == "bottom" then if dir == "up" then set_sel(col_rows("right")[#col_rows("right")]) end return end
      if k > #list then local b = col_rows("bottom"); if #b > 0 then set_sel(b[#b]) return end k = 1 end
      if k < 1 then k = #list end
      set_sel(list[k])
    else
      if cur == "bottom" then k = k + (dir == "right" and 1 or -1); if list[k] then set_sel(list[k]) end return end
      if cur == "left" and dir == "right" then
        if rows[sel].col == "more" then sm.all_programs(true) return end
        local r = col_rows("right"); set_sel(r[math.min(k, #r)])
      elseif cur == "right" and dir == "left" then local l = col_rows("left"); set_sel(l[math.min(k, #l)]) end
    end
  end
  sm.surface.on_key = function(_, name, text, state, pressed)
    if not sm.open then return end
    if name == "Super_L" or name == "Super_R" then   -- Super alone closes the menu again (lhwm's grab is not active while we hold the keyboard)
      if pressed then sm.super_alone = true elseif sm.super_alone then sm.hide() end
      return
    end
    if not pressed then return end
    sm.super_alone = false
    if lh.menu.is_open() then local r = lh.menu.key(name, text); if r == "close" then lh.menu.close() end return end
    if name == "Escape" then sm.hide()
    elseif name == "Up" or name == "Down" or name == "Left" or name == "Right" then move(name:lower())
    elseif name == "Tab" or name == "ISO_Left_Tab" then
      local order = { "left", "right", "bottom" }; local cur = sel and rows[sel].col or "bottom"; if cur == "more" then cur = "left" end
      local k = 1; for j, c in ipairs(order) do if c == cur then k = j end end
      k = k % 3 + 1; set_sel(col_rows(order[k])[1])
    elseif name == "Return" or name == "KP_Enter" or name == "space" then if sel then activate(sel) end
    elseif text ~= "" then
      local t = text:lower(); local n = #rows; local i = sel or 0
      for _ = 1, n do i = i % n + 1; local r = rows[i]; local label = r.col == "more" and "all programs" or (r.item and r.item.text or ""):lower()
        if label:sub(1, #t) == t then set_sel(i) break end end
    end
  end
  -- the All Programs row opens its cascade after a hover delay (XP: SPI_GETMENUSHOWDELAY 400 ms)
  local base_set_sel = set_sel
  set_sel = function(i) local was = sel; base_set_sel(i); if was ~= i then more_hover(sm.mouse and rows[i] and rows[i].col == "more") end end   -- hover only; the keyboard opens it with Right/Return

  ------------------------------------------------------------------ session confirmation flyout (6.2): Log Off / Stand by / Turn Off
  function sm.confirm(default)
    local P = lh.startpanel()
    local lb, lbh = P.image("StartPanel.LOGOFFBUTTONS"), P.image("StartPanel.LOGOFFBUTTONS(HOT)")
    local acts = { { "logoff", "Log Off", lb, lbh, 1 }, { "standby", "Stand by", lb, lbh, 2 }, { "shutdown", "Turn Off", shell32(28), shell32(28), nil } }
    local cur = 1; for i, a in ipairs(acts) do if a[1] == default or (default == "turnoff" and a[1] == "shutdown") then cur = i end end
    local owner = {}
    local cells = {}
    local function paint() for i, c in ipairs(cells) do c.hl:set{ visible = i == cur }; c.g:set{ image = i == cur and c.hot or c.img } end lh.flyout.surface:damage() end
    local function go(i) local a = acts[i]; lh.close_flyout(); S.run(a[1]) end
    function owner:flyout()
      local W = 228
      local root = N{ layout = "fill", w = W }
      root:add(N{ kind = "rect", fill = "#ffffff" })
      local col = root:add(N{ layout = "vbox" })
      col:add(N{ kind = "text", text = "End your Longhorn session?", font = "Segoe UI,Tahoma Bold 14px", color = "#808080", margin = 8 })
      local row = col:add(N{ layout = "hbox", h = 64, margin = { 6, 0, 6, 0 } })
      for i, a in ipairs(acts) do
        local cell = row:add(N{ layout = "fill", w = 72, hoverable = true })
        local hl = cell:add(N{ kind = "rect", fill = "#c5e7f6", stroke = "#7da2ce", radius = 3, visible = false })
        local cv = cell:add(N{ layout = "canvas" })
        local box = cv:add(N{ layout = "canvas", x = 24, y = 6, w = 24, h = 24, clip = true })
        local g = box:add(N{ kind = "image", image = a[3], x = a[5] and -24 * a[5] or 0, y = 0, w = a[5] and 72 or 24, h = 24 })
        cv:add(N{ kind = "text", text = a[2], font = "Segoe UI,Tahoma 12px", color = "#000000", align = "center", x = 0, y = 36, w = 72, h = 18 })
        cells[i] = { hl = hl, g = g, img = a[3], hot = a[4] }
        cell.on_hover = function(self, over) if over then cur = i; paint() end end
        cell.on_click = function(self, b) if b == 1 then go(i) end end
      end
      local foot = col:add(N{ layout = "dock", h = 26, margin = { 8, 2, 8, 6 } })
      local cancel = foot:add(N{ layout = "fill", dock = "right", w = 64, hoverable = true })
      cancel:add(N{ kind = "rect", fill = "#f4f4f4", stroke = "#929497", radius = 2 })
      cancel:add(N{ kind = "text", text = "Cancel", font = "Segoe UI,Tahoma 12px", color = "#000000", align = "center", valign = "center" })
      cancel.on_click = function() lh.close_flyout() end
      foot:add(N{ kind = "text", text = "Enter confirms, Esc cancels", font = "Segoe UI,Tahoma 11px", color = "#808080", dock = "fill", valign = "center" })
      local _, h = root:desired(W, 400)
      lh.timer(paint, 0)
      return root, W, h
    end
    function owner:place(fw, fh) local c = C(); return math.max(0, (c.width or 380) - fw), taskbar_top() - fh end
    function owner:on_key(name)
      if name == "Left" then cur = (cur + 1) % 3 + 1; paint() elseif name == "Right" or name == "Tab" then cur = cur % 3 + 1; paint()
      elseif name == "Return" or name == "KP_Enter" or name == "space" then go(cur) end
    end
    lh.open_flyout(owner)
    sm.confirm_owner = owner
  end

  lh.startmenu = sm
  return sm
end
