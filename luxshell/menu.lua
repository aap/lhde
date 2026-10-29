-- Popup menus (SHELL-DESIGN 6.3/6.4): the All Programs cascade of the start menu (StartMenu::Toolbar / MenuBand parts of the
-- msstyles, the XP "MenuBand" look) and plain context menus (the taskbar's, drawn with the theme's FLATMENUS sysmetrics
-- colours: MENU, MENUTEXT, MENUHILIGHT, MENUBAR).
--   lh.menu.open(items, x, y, opts)   items = { { text, icon = path, submenu = items | function() return items end,
--                                       action = fn, checked = bool, radio = bool, separator = true, disabled = bool }, ... }
--                                       text may carry a Win32-style mnemonic: "&Remove" draws R underlined and the R key
--                                       activates the item ("&&" is a literal &); without one, typing jumps by first letter
--                                       opts = { style = "context" | "avalon", grab = bool (context menus take the
--                                       pointer and keyboard themselves), up = bool (x,y is the bottom-left corner),
--                                       flip = bool (TrackPopupMenu: open left of / above x,y when it does not fit to the
--                                       right / below, instead of sliding along the edge), on_close = fn }
--   styles: "start" the All Programs cascade (StartPanel bitmaps); "context" the Win32 flat menu (FLATMENUS sysmetrics, T.menu);
--           "avalon" the 4074 Avalon ContextMenu of the sidebar, its tiles and the Avalon taskbar (measured on lh4074-091929.png,
--           1152x864 Slate: 1 px #919191 frame + 1 px drop shadow, 19 px rows (18 px highlight, top row 30 % and bottom row
--           40 % lighter than MENUHILIGHT #EDB078), text 17 px in, 7 px separators with a 1 px #BEBEBE line, no icon gutter;
--           colours from T.menu as "context")
--   lh.menu.close()                   closes every level;  lh.menu.is_open()
--   lh.menu.key(name, text)           keyboard for the deepest level (Up/Down/Left/Right/Return/Escape, letters jump);
--                                     true when handled, "close" when Left/Escape closed the root level
--   lh.menu.owns(win)                 whether an X window id is one of the menu surfaces
return function(ctx)
  local T, S = ctx.T, lh.session
  local screen_w, screen_h = ctx.screen_w, ctx.screen_h

  ------------------------------------------------------------------ levels
  local M = { levels = {} }
  local pool = {}
  local function surface_for(depth)
    if not pool[depth] then
      local s = lh.surface{ name = "lhshell-menu", popup = true, type = "menu", x = 0, y = 0, w = 10, h = 10 }
      s.on_outside = function() M.close() end
      s.on_key = function(_, name, text, state, pressed) if pressed then local r = M.key(name, text); if r == "close" then M.close() end end end
      s._root = lh.node{ layout = "fill" }; s:root(s._root)   -- one tree per surface, cleared on close (nodes are not leaked per open)
      pool[depth] = s
    end
    return pool[depth]
  end
  function M.owns(win) for _, s in pairs(pool) do if s:id() == win then return true end end return false end
  function M.is_open() return #M.levels > 0 end

  local function style(name)
    local st = { name = name }
    do
      local m = T.menu or {}
      st.fill = m.bg or "#f4f4f4"; st.border = m.border or "#929497"; st.hotfill = m.hot or "#c5e7f6"
      st.text = m.text or "#00025c"; st.hot_text = m.hot_text or "#000000"; st.gray = m.gray or "#b0b1b3"
      st.font = T.font; st.item_h = 22; st.gutter = 24; st.pad = 3
      if name == "avalon" then
        st.sepline = m.sep or "#bebebe"; st.item_h = 19; st.gutter = 17; st.pad = 1; st.sep_h = 7; st.shadow = true
        st.min_w = 100; st.right = 12; st.check = "✔"; st.check_x = 1
      end
    end
    return st
  end

  local function resolve(items) if type(items) == "function" then return items() end return items end
  local function selectable(it) return it and not it.separator and not it.disabled end
  -- "&Remove" -> plain "Remove", markup "<u>R</u>emove", key "r"; "&&" is a literal "&"
  local function esc(c) return c == "&" and "&amp;" or c == "<" and "&lt;" or c == ">" and "&gt;" or c end
  local function label(text)
    text = text or ""
    if not text:find("&", 1, true) then return text, nil, nil end
    local plain, markup, key, i = {}, {}, nil, 1
    while i <= #text do
      local c = text:sub(i, i)
      if c == "&" and text:sub(i + 1, i + 1) == "&" then table.insert(plain, "&"); table.insert(markup, "&amp;"); i = i + 2
      elseif c == "&" and i < #text and not key then
        local ch = text:sub(i + 1):match("^[%z\1-\127\194-\244][\128-\191]*")
        key = ch:lower(); table.insert(plain, ch); table.insert(markup, "<u>" .. esc(ch) .. "</u>"); i = i + 1 + #ch
      else table.insert(plain, c); table.insert(markup, esc(c)); i = i + 1 end
    end
    return table.concat(plain), table.concat(markup), key
  end
  M.label = label

  local function paint_sel(L)
    for i, r in ipairs(L.rows) do
      local on = i == L.sel
      if r.hl then r.hl:set{ visible = on } end
      if r.label then r.label:set{ color = on and L.st.hot_text or (L.items[i].disabled and (L.st.gray or L.st.text) or L.st.text) } end
      if r.arrow then r.arrow:set{ color = on and L.st.hot_text or L.st.text } end
    end
    L.surface:damage()
  end

  local function level_of_surface(s) for d, L in ipairs(M.levels) do if L.surface == s then return d, L end end end
  local open_level
  local function close_from(depth)   -- close levels depth..n
    for d = #M.levels, depth, -1 do
      local L = M.levels[d]
      if L.timer then lh.cancel(L.timer) end
      if d == 1 and L.grabbed then L.surface:ungrab() end
      L.surface:hide(); L.root:clear(); M.levels[d] = nil
    end
  end
  function M.close()
    if #M.levels == 0 then return end
    local cb = M.levels[1].opts.on_close
    close_from(1)
    if cb then cb() end
  end

  local function open_sub(depth, i)
    local L = M.levels[depth]; local it = L.items[i]
    if not (it and it.submenu) then return end
    if M.levels[depth + 1] and M.levels[depth + 1].parent_index == i then return end
    close_from(depth + 1)
    local rx, ry, rw, rh = L.rows[i].node:bounds()
    local x = L.x + L.w - 3; local y = L.y + ry - L.st.pad
    open_level(resolve(it.submenu), x, y, L.opts, depth + 1, i, L.x + 3)
  end

  local function activate(depth, i)
    local L = M.levels[depth]; local it = L and L.items[i]
    if not selectable(it) then return end
    if it.submenu then open_sub(depth, i); local C = M.levels[depth + 1]; if C then C.sel = nil; for k, x in ipairs(C.items) do if selectable(x) then C.sel = k break end end paint_sel(C) end return end
    M.close()
    if it.action then it.action(it) end
  end

  local function select(depth, i, mouse)
    local L = M.levels[depth]; if not L then return end
    if L.sel == i then return end
    L.sel = i; paint_sel(L)
    if L.timer then lh.cancel(L.timer); L.timer = nil end
    if mouse then   -- XP menu delay: open the hovered submenu / close the other one after 300 ms
      L.timer = lh.timer(function()
        L.timer = nil
        if M.levels[depth] ~= L or L.sel ~= i then return end
        local it = L.items[i]
        if it and it.submenu and not it.disabled then open_sub(depth, i) else close_from(depth + 1) end
      end, 300)
    end
  end

  -- build one level: rows in columns (a column holds as many rows as fit the screen height)
  function open_level(items, x, y, opts, depth, parent_index, left_edge)
    local st = style(opts.style or "context")
    local s = surface_for(depth)
    local root = s._root; root:clear()
    if st.bg then root:add(lh.node{ kind = "ninegrid", image = st.bg, grid = st.bg_grid })   -- stretched: the nine-grid TILE mode repeats the whole bitmap
    elseif st.shadow then   -- the Avalon menu: 1 px frame and a 1 px dark drop shadow right and below (the surface is 1 px larger)
      root:add(lh.node{ layout = "fill", margin = { 1, 1, 0, 0 } }):add(lh.node{ kind = "rect", fill = "#40000000" })
      root:add(lh.node{ layout = "fill", margin = { 0, 0, 1, 1 } }):add(lh.node{ kind = "rect", fill = st.fill, stroke = st.border })
    else root:add(lh.node{ kind = "rect", fill = st.fill, stroke = st.border }) end
    local cols = root:add(lh.node{ layout = "hbox", margin = st.shadow and { st.pad + 1, st.pad + 1, st.pad + 2, st.pad + 2 } or st.pad })
    local L = { surface = s, root = root, items = items, rows = {}, st = st, opts = opts, parent_index = parent_index }
    -- measure
    local maxw, sep_h = 0, st.sep_h or (st.sep and 9 or 7)
    local tmp = cols:add(lh.node{ visible = false })
    for _, it in ipairs(items) do
      if not it.separator then
        local t = tmp:add(lh.node{ kind = "text", text = (label(it.text)), font = st.font })
        local w = t:measure(); if w > maxw then maxw = w end
      end
    end
    cols:clear()
    local item_w = math.max(st.min_w or (st.name == "start" and 150 or 140), math.min(420, math.ceil(maxw) + st.gutter + (st.right or (8 + 22))))
    local bottom = (st.name == "start" and lh.taskbar_shown_top) and lh.taskbar_shown_top() or screen_h   -- the cascade stays above the taskbar
    local avail = bottom - 2 * st.pad - 4
    local col, colh, ncols, maxcolh = nil, 0, 0, 0
    for i, it in ipairs(items) do
      local h = it.separator and sep_h or st.item_h
      if not col or colh + h > avail then col = cols:add(lh.node{ layout = "vbox", w = item_w }); ncols = ncols + 1; colh = 0 end
      colh = colh + h; if colh > maxcolh then maxcolh = colh end
      local row
      if it.separator then
        row = col:add(lh.node{ h = h, layout = "fill" })
        if st.sepline then row:add(lh.node{ layout = "fill", margin = { 0, 2, 0, 4 } }):add(lh.node{ kind = "rect", fill = st.sepline, h = 1 })
        elseif st.sep then row:add(lh.node{ kind = "ninegrid", image = st.sep, grid = { 0, 1, 0, 6 }, margin = { st.gutter - 6, 0, 0, 0 } })
        else row:add(lh.node{ layout = "fill", margin = { st.gutter, 3, 2, 3 } }):add(lh.node{ kind = "rect", fill = st.border, h = 1 }) end
        L.rows[i] = { node = row }
      else
        row = col:add(lh.node{ h = h, layout = "fill", hoverable = true })
        local hl
        if st.hot then hl = row:add(lh.node{ kind = "ninegrid", image = st.hot, strips = 3, strip = 1, grid = st.hot_grid, visible = false })
        elseif st.name == "start" then hl = row:add(lh.node{ kind = "rect", fill = st.hotfill, stroke = st.hot_stroke, visible = false })
        elseif st.name == "avalon" then   -- 18 px plate (the 19th row is the gap), lighter top and bottom rows
          local function mix(c, f) local r, g, b = c:match("#(%x%x)(%x%x)(%x%x)"); if not r then return c end
            local function m(v) v = tonumber(v, 16); return math.floor(v + (255 - v) * f + 0.5) end
            return string.format("#%02x%02x%02x", m(r), m(g), m(b)) end
          hl = row:add(lh.node{ layout = "fill", margin = { 0, 0, 0, 1 }, visible = false })
          hl:add(lh.node{ kind = "rect", gradient = { { 0, mix(st.hotfill, 0.3) }, { 1 / 18, st.hotfill }, { 17 / 18, st.hotfill }, { 1, mix(st.hotfill, 0.4) } } })
        else hl = row:add(lh.node{ kind = "rect", fill = st.hotfill, stroke = st.border, visible = false }) end
        local d = row:add(lh.node{ layout = "dock" })
        local g = d:add(lh.node{ dock = "left", w = st.gutter, layout = "canvas" })
        local ic = it.icon
        if ic then g:add(lh.node{ kind = "image", image = ic, x = math.floor((st.gutter - 16) / 2), y = math.floor((h - 16) / 2), w = 16, h = 16 }) end
        if it.checked ~= nil and it.checked then
          g:add(lh.node{ kind = "text", text = it.radio and "●" or (st.check or "✓"), font = st.font, color = st.text, x = st.check_x or math.floor(st.gutter / 2 - 5), y = math.floor((h - 16) / 2), w = 14, h = 16 })
        end
        local arrow
        if it.submenu then arrow = d:add(lh.node{ kind = "text", text = "▸", font = st.font, dock = "right", w = 16, valign = "center", color = st.text }) end
        local plain, markup = label(it.text)
        local lbl = d:add(lh.node{ kind = "text", text = markup or plain, markup = markup ~= nil, font = st.font, color = it.disabled and (st.gray or st.text) or st.text, ellipsis = true, valign = "center", dock = "fill", margin = { st.name == "avalon" and 0 or 2, 0, 0, st.name == "avalon" and 1 or 0 } })
        row.on_hover = function(self, over) if over and selectable(it) then select(depth, i, true) end end
        row.on_click = function(self, button) activate(depth, i) end
        L.rows[i] = { node = row, hl = hl, label = lbl, arrow = arrow }
      end
    end
    local w, h = ncols * item_w + 2 * st.pad, maxcolh + 2 * st.pad
    if st.shadow then w, h = w + 3, h + 3 end   -- frame 1 + shadow 1 (+1 frame on the far side)
    -- placement: a submenu to the right of its parent (left when it does not fit), clamped to the screen
    if opts.up and depth == 1 then y = y - h end
    if opts.flip and depth == 1 then   -- TrackPopupMenu: flip to the other side of the point when it does not fit
      if x + w > screen_w and x - w >= 0 then x = x - w end
      if y + h > bottom and y - h >= 0 then y = y - h end
    end
    if x + w > screen_w then x = left_edge and (left_edge - w) or (screen_w - w) end
    if x < 0 then x = 0 end
    if y + h > bottom then y = bottom - h end
    if y < 0 then y = 0 end
    L.x, L.y, L.w, L.h = x, y, w, h
    s:move(x, y, w, h); s:show(); s:raise()
    M.levels[depth] = L
    if depth == 1 and opts.grab then local ok = s:grab(true); L.grabbed = ok end
    paint_sel(L)
    -- picom (xrender, Xvfb lab) sometimes leaves the popups under a newly mapped one unpainted until the next damage: repaint
    -- the open levels and the start menu once the new window's fade-in is over
    lh.timer(function()
      for _, l in ipairs(M.levels) do l.surface:damage() end
    end, 180)
    return L
  end

  function M.open(items, x, y, opts)
    M.close()
    opts = opts or {}
    local L = open_level(resolve(items), x, y, opts, 1, nil, nil)
    if opts.select_first then for k, it in ipairs(L.items) do if selectable(it) then L.sel = k break end end paint_sel(L) end
    return L
  end

  local function step(L, dir)
    local n = #L.items; if n == 0 then return end
    local i = L.sel or (dir > 0 and 0 or n + 1)
    for _ = 1, n do i = i + dir; if i < 1 then i = n elseif i > n then i = 1 end if selectable(L.items[i]) then break end end
    L.sel = i; paint_sel(L)
  end
  function M.key(name, text)
    local depth = #M.levels; local L = M.levels[depth]
    if not L then return false end
    if name == "Up" then step(L, -1) return true
    elseif name == "Down" then step(L, 1) return true
    elseif name == "Right" then if L.sel and L.items[L.sel].submenu then activate(depth, L.sel) end return true
    elseif name == "Left" or name == "Escape" then
      if depth > 1 then close_from(depth) return true end
      return "close"
    elseif name == "Return" or name == "KP_Enter" or name == "space" then if L.sel then activate(depth, L.sel) end return true
    elseif name == "Home" or name == "End" then L.sel = nil; step(L, name == "Home" and 1 or -1) return true
    elseif text and text ~= "" and text:byte(1) >= 32 then
      -- a mnemonic: the only item with that key is activated (a submenu opens), several are cycled through (Win32)
      local t = text:lower(); local hits = {}
      for k, it in ipairs(L.items) do if selectable(it) then local _, _, key = label(it.text); if key == t then table.insert(hits, k) end end end
      if #hits == 1 then L.sel = hits[1]; paint_sel(L); activate(depth, hits[1]) return true end
      if #hits > 1 then local nxt = hits[1]; for _, k in ipairs(hits) do if k > (L.sel or 0) then nxt = k break end end L.sel = nxt; paint_sel(L) return true end
      -- no mnemonic: typing jumps to the next item starting with that letter
      local n = #L.items; local i = L.sel or 0
      for _ = 1, n do i = i % n + 1; local it = L.items[i]; if selectable(it) and (label(it.text)):lower():sub(1, #t) == t then L.sel = i; paint_sel(L) break end end
      return true
    end
    return true
  end
  -- a press on another lhshell surface (owner events of the root level's grab) closes context menus
  lh.on("surface-press", function(x, y, button, win)
    if #M.levels > 0 and M.levels[1].grabbed and not M.owns(win) then M.close() end
  end)
  lh.menu = M
end
