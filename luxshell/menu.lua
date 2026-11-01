-- Popup menus (SHELL-DESIGN 6.3/6.4): the context menus of the sidebar, its tiles, the Avalon taskbar and its Start button,
-- drawn by wuxtheme's popup menu painter (lh.menu_render, menupaint.c: ut_menu_layout / ut_menu_draw, the pixels of flxp's
-- user32 menus and luxwm's window menu) in the session theme's visual style: its Menu class where it has one (the Aero / Jade
-- POPUPITEM band, separators, check marks, cascade arrows), [SysMetrics] colours, FLATMENUS and MENUFONT; Windows Classic from
-- the scheme's colours and Metrics.menufont; XP's 4 px drop shadow (luxwm's 4074 Slate measurement). Item geometry is the
-- painter's layout; this file keeps the input, the keyboard, the mnemonics, the cascades and the placement.
-- 4074's own Avalon ContextMenu (lh4074-091929.png, 1152x864 Slate: white, 1 px #919191 frame and a 1 px shadow, 19 px rows
-- with an 18 px MENUHILIGHT bar, text 17 px in) did not take the style's look; the painter's is used on purpose (aap: menus
-- should look the same everywhere; todo_ideas/luxshell.txt keeps the old look as a possible option).
--   lh.menu.open(items, x, y, opts)   items = { { text, accel = "Ctrl+X", submenu = items | function() return items end,
--                                       action = fn, checked = bool, radio = bool, default = bool (bold), separator = true,
--                                       disabled = bool }, ... }
--                                       text may carry a Win32-style mnemonic: "&Remove" underlines R (once the keyboard is used,
--                                       XP's keyboard cues) and the R key activates the item ("&&" is a literal &); without one,
--                                       typing jumps by first letter
--                                       opts = { grab = bool (context menus take the pointer and keyboard themselves), up = bool
--                                       (x,y is the bottom-left corner), flip = bool (TrackPopupMenu: open left of / above x,y
--                                       when it does not fit to the right / below, instead of sliding along the edge),
--                                       select_first = bool (opened from the keyboard: the first item selected, cues shown),
--                                       on_close = fn; style = "avalon" | "context" is accepted, every menu draws the same }
--   lh.menu.close()                   closes every level;  lh.menu.is_open()
--   lh.menu.key(name, text)           keyboard for the deepest level (Up/Down/Left/Right/Return/Escape, letters jump);
--                                     true when handled, "close" when Left/Escape closed the root level
--   lh.menu.owns(win)                 whether an X window id is one of the menu surfaces
return function(ctx)
  local screen_w, screen_h = ctx.screen_w, ctx.screen_h

  ------------------------------------------------------------------ levels
  local M = { levels = {} }
  local pool = {}
  local function surface_for(depth)
    if not pool[depth] then
      local s = lh.surface{ name = "luxshell-menu", popup = true, type = "menu", x = 0, y = 0, w = 10, h = 10 }
      s.on_outside = function() M.close() end
      s.on_key = function(_, name, text, state, pressed) if pressed then local r = M.key(name, text); if r == "close" then M.close() end end end
      s._root = lh.node{ layout = "canvas" }; s:root(s._root)   -- one tree per surface, cleared on close (nodes are not leaked per open)
      pool[depth] = s
    end
    return pool[depth]
  end
  function M.owns(win) for _, s in pairs(pool) do if s:id() == win then return true end end return false end
  function M.is_open() return #M.levels > 0 end

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

  -- the painter's view of the items: submenu only as "has an arrow" (a function is not called to draw)
  local function painter_items(items)
    local out = {}
    for i, it in ipairs(items) do
      out[i] = { text = it.text or "", accel = it.accel, separator = it.separator, disabled = it.disabled, checked = it.checked,
                 radio = it.radio, default = it.default, submenu = it.submenu ~= nil }
    end
    return out
  end
  -- capture: {x, y} copies the screen under the menu for its shadow (no compositor; menupaint.c), later renders reuse it
  local function render(L, depth, capture)
    local e; if lh.theme_name then local _; _, e = lh.theme_name() end
    return lh.menu_render(L.pitems, L.sel, { key = "@menu:" .. depth, cues = L.cues, theme = e and e.v, under = capture or L.opaque })
  end
  local function paint_sel(L)
    local r = render(L, L.depth)
    if r then L.img:set{ image = r.image } end   -- set at once: the key's previous image is freed by the render
    L.surface:damage()
  end

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
    -- XP: the cascade overlaps its parent by 3 px, its first item level with the parent item (luxwm/menu.c open_sub)
    local x = L.x + L.w - 3; local y = L.y + L.geo.rows[i].y - L.geo.pad
    open_level(resolve(it.submenu), x, y, L.opts, depth + 1, i, L.x + 3, L.cues)
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

  -- build one level: the painted menu as one image, a hoverable node over each item's rectangle (the painter's layout)
  function open_level(items, x, y, opts, depth, parent_index, left_edge, cues)
    local s = surface_for(depth)
    local root = s._root; root:clear()
    local L = { surface = s, root = root, items = items, pitems = painter_items(items), opts = opts, parent_index = parent_index,
                depth = depth, cues = cues and true or false }
    local g = render(L, depth)
    if not g then return nil end
    L.geo = g
    local w, h = g.w, g.h                     -- the menu; the surface is larger by the shadow right and below
    L.img = root:add(lh.node{ kind = "image", image = g.image, x = 0, y = 0, w = w + g.shadow, h = h + g.shadow })
    for i, it in ipairs(items) do
      local r = g.rows[i]
      if r and not it.separator then
        local row = root:add(lh.node{ x = g.pad, y = r.y, w = w - 2 * g.pad, h = r.h, hoverable = true })
        row.on_hover = function(self, over) if over and selectable(it) then select(depth, i, true) end end
        row.on_click = function(self, button) activate(depth, i) end
      end
    end
    -- placement: a submenu to the right of its parent (left when it does not fit), clamped to the screen
    if opts.up and depth == 1 then y = y - h end
    if opts.flip and depth == 1 then   -- TrackPopupMenu: flip to the other side of the point when it does not fit
      if x + w > screen_w and x - w >= 0 then x = x - w end
      if y + h > screen_h and y - h >= 0 then y = y - h end
    end
    if x + w > screen_w then x = left_edge and (left_edge - w) or (screen_w - w) end
    if x < 0 then x = 0 end
    if y + h > screen_h then y = screen_h - h end
    if y < 0 then y = 0 end
    L.x, L.y, L.w, L.h = x, y, w, h
    if not lh.cm_owner() then   -- no compositor: the shadow is the screen under it, darkened (XP; luxwm/menu.c), copied before the map
      s:hide()
      local r = render(L, depth, { x, y })
      if r then L.img:set{ image = r.image }; L.opaque = true end
    end
    s:move(x, y, w + g.shadow, h + g.shadow); s:show(); s:raise()
    M.levels[depth] = L
    if depth == 1 and opts.grab then local ok = s:grab(true); L.grabbed = ok end
    s:damage()
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
    local L = open_level(resolve(items), x, y, opts, 1, nil, nil, opts.select_first)
    if L and opts.select_first then for k, it in ipairs(L.items) do if selectable(it) then L.sel = k break end end paint_sel(L) end
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
    if not L.cues then for _, l in ipairs(M.levels) do l.cues = true; if l ~= L then paint_sel(l) end end end   -- a key shows the cues from now on (XP)
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
      for _ = 1, n do i = i % n + 1; local it = L.items[i]; if selectable(it) and (label(it.text)):lower():sub(1, #t) == t then L.sel = i break end end
      paint_sel(L)
      return true
    end
    paint_sel(L)
    return true
  end
  -- a press on another luxshell surface (owner events of the root level's grab) closes context menus
  lh.on("surface-press", function(x, y, button, win)
    if #M.levels > 0 and M.levels[1].grabbed and not M.owns(win) then M.close() end
  end)
  lh.menu = M
end
