-- Longhorn notification toasts for org.freedesktop.Notifications (notify.c), after notification.xaml and the IL facts in
-- SHELL-DESIGN 2.4.5 (NotificationToast / NotifyWindow):
--   FillPanel { Border#ToastBackground #FF000000 (Opacity 0 when Aero: glass);
--     HackImage 4002 (205x60) HackNineGrid 7 > DockPanel {
--       TimerControl#Timer Dock Right, Width 5, image 4001, Foreground #DD7D2F (the countdown bar);
--       DockPanel { DockPanel#NotificationOptions (Top) { ImageButton#NotificationClose 14x15 4003/4004/4005 (Right), "Options" #FFFFFF (Right) };
--                   DockPanel { Image#NotificationIcon (Left, Stretch None, <= 80x80);
--                               HackBorder 4006 (154x56) grid 7, BorderThickness 6 { NotificationHeader Franklin Gothic Medium 15,
--                                   NotificationBody Franklin Gothic Medium 12; DockPanel#NotificationButtons } } } } }
-- Behaviour: body wrapped at 110 px, or 154 (2 lines), or 220 (4 lines); <= 2 buttons 73x24; 10 s (min 5 s) timer whose bar
-- shrinks one pixel per timeout/height ms; hover stops it, rewinds to 0.75 in 200 ms and expands (buttons + Options fade in,
-- 200 ms), leaving collapses and restarts; one toast at a time, FIFO; x = work.right - w - 10, y = work.bottom - (expanded -
-- collapsed) - h - 10 on the primary monitor (T.toast_place = "sidebar" puts it over the sidebar's top instead).
-- Window: override-redirect ARGB with _NET_WM_WINDOW_TYPE_NOTIFICATION (picom rules match it), never takes focus.
return function(ctx)
  local R, T, sb, tb = ctx.R, ctx.T, ctx.sb, ctx.tb
  -- read from the pack per toast (T is swapped in place on a theme switch, SHELL-DESIGN 6.1)
  local glass, hfont, bfont
  local function pack_style()
    glass = T.name ~= "slate"                                         -- IsAeroRunning → ToastBackground opacity 0
    hfont = T.toast_header_font or "ITC Franklin Gothic Std Medium 15px"   -- Franklin Gothic Medium 15 (ITC Std Medium is installed here)
    bfont = T.toast_body_font or "ITC Franklin Gothic Std Medium 12px"
  end
  local queue, cur = {}, nil
  local surf = lh.surface{ name = "lhshell-toast", popup = true, type = "notification", x = 0, y = 0, w = 10, h = 10 }
  surf:round(9)                                                       -- round-rect region radius 9 (2.4.5)

  -- icon lookup: absolute/file:// PNG paths, then icon-theme names (PNG only: cairo has no SVG loader)
  local icon_cache = {}
  local function exists(p) local f = io.open(p, "rb"); if f then f:close() return true end return false end
  local function find_icon(name)
    if not name or name == "" then return nil end
    name = name:gsub("^file://", "")
    if name:sub(1, 1) == "/" then return name:match("%.png$") and exists(name) and name or nil end
    if icon_cache[name] ~= nil then return icon_cache[name] or nil end
    local found = false
    for _, theme in ipairs{ "hicolor", "Adwaita", "AdwaitaLegacy", "breeze", "Papirus", "gnome" } do
      for _, size in ipairs{ "48x48", "32x32", "64x64", "24x24", "128x128", "256x256", "22x22", "16x16" } do
        for _, cat in ipairs{ "apps", "status", "devices", "actions", "categories", "places", "mimetypes", "emblems", "legacy" } do
          local p = "/usr/share/icons/" .. theme .. "/" .. size .. "/" .. cat .. "/" .. name .. ".png"
          if exists(p) then found = p break end
        end
        if found then break end
      end
      if found then break end
    end
    if not found and exists("/usr/share/pixmaps/" .. name .. ".png") then found = "/usr/share/pixmaps/" .. name .. ".png" end
    icon_cache[name] = found; return found or nil
  end
  local function strip_markup(s) return (s or ""):gsub("<[^>]*>", ""):gsub("&lt;", "<"):gsub("&gt;", ">"):gsub("&quot;", '"'):gsub("&apos;", "'"):gsub("&amp;", "&") end

  local function close(reason)
    if not cur then return end
    local t = cur; cur = nil
    if t.timer then lh.cancel(t.timer) end
    surf:hide(); t.root:clear()
    lh.notification_closed(t.n.id, reason)
    if #queue > 0 then local n = table.remove(queue, 1); lh._show_toast(n) end
  end

  local function build(n)
    pack_style()
    local t = { n = n }
    local root = lh.node{ layout = "fill", hoverable = true }
    t.root = root
    root:add(lh.node{ kind = "rect", fill = "#000000", opacity = glass and 0 or 1, radius = 9 })      -- ToastBackground (round-rect region r 9)
    root:add(lh.node{ kind = "ninegrid", image = R(4002), grid = 7 })
    local dock = root:add(lh.node{ layout = "dock", margin = {4, 4, 3, 4} })                           -- inside the 4002 rim
    -- TimerControl: 5 px column, the 4001 bar shrinks from the top as time runs out
    local timer_col = dock:add(lh.node{ dock = "right", w = 5, layout = "canvas", margin = {0, 2, 0, 2} })
    t.bar = timer_col:add(lh.node{ kind = "ninegrid", image = R(4001), grid = {2, 0, 2, 0}, x = 0, y = 0, w = 5, h = 10 })
    local main = dock:add(lh.node{ dock = "fill", layout = "dock", margin = {0, 0, 2, 0} })
    local opts = main:add(lh.node{ dock = "top", h = 15, layout = "dock" })
    local closeb = opts:add(lh.node{ kind = "button", dock = "right", w = 14, h = 15, images = { normal = R(4003), hot = R(4004), pressed = R(4005) } })
    closeb.on_click = function() close(2) end
    t.options = opts:add(lh.node{ kind = "text", text = "Options", color = "#ffffff", font = T.font, dock = "right", valign = "center", margin = {0, 0, 6, 0}, opacity = 0 })
    local body = main:add(lh.node{ dock = "fill", layout = "dock" })
    local icon = n.image or find_icon(n.image_path) or find_icon(n.app_icon)
    if icon then
      local probe = lh.node{ kind = "image", image = icon }; local iw, ih = probe:desired()
      local k = math.min(1, 48 / math.max(iw, ih, 1))                                                   -- Stretch None, header icon <= 80; we keep <= 48
      body:add(lh.node{ dock = "left", layout = "vbox", margin = {2, 2, 6, 2} }):add(lh.node{ kind = "image", image = icon, w = math.floor(iw * k), h = math.floor(ih * k) })
    end
    local panel = body:add(lh.node{ dock = "fill", layout = "fill" })
    panel:add(lh.node{ kind = "ninegrid", image = R(4006), grid = 7 })
    local inner = panel:add(lh.node{ layout = "dock", margin = 6 })
    local content = inner:add(lh.node{ dock = "top", layout = "dock", hoverable = true })               -- ButtonNoStyle#NotificationContent
    -- body wrap: 110, else 154 if that gives <= 2 lines, else 220 (<= 4 lines, ellipsised past that)
    local summary, text = strip_markup(n.summary), strip_markup(n.body)
    local bnode = lh.node{ kind = "text", text = text, font = bfont, color = "#000000", wrap = true, dock = "top" }
    local _, line = bnode:measure(); line = math.max(line, 1)
    local w = 110
    for _, cand in ipairs{ 110, 154, 220 } do
      w = cand; local _, h = bnode:measure(cand); local lines = math.floor(h / line + 0.5)
      if (cand == 110 and lines <= 1) or (cand == 154 and lines <= 2) then break end
    end
    local hnode = lh.node{ kind = "text", text = summary, font = hfont, color = "#000000", ellipsis = true, dock = "top" }
    local hw = hnode:measure(); w = math.max(w, math.min(220, hw))
    local _, bh = bnode:measure(w); bh = math.min(bh, 4 * line)
    hnode:set{ w = w }; bnode:set{ w = w, h = text ~= "" and math.ceil(bh) or 0 }
    content:add(hnode); content:add(bnode)
    content.on_click = function()                                                                    -- body click = default action
      for _, a in ipairs(n.actions or {}) do if a.key == "default" then lh.notification_action(n.id, "default") end end
      close(2)
    end
    root.on_click = content.on_click                                                                 -- anywhere on the toast but the buttons
    -- NotificationButtons: <= 2 text buttons 73x24 (border 2), the non-default actions; shown when expanded
    t.buttons = inner:add(lh.node{ dock = "top", layout = "hbox", h = 0, clip = true })
    local nb = 0; t.button_nodes = {}
    for _, a in ipairs(n.actions or {}) do
      if a.key ~= "default" and nb < 2 then
        nb = nb + 1
        local b = t.buttons:add(lh.node{ kind = "button", w = 73, h = 24, margin = {0, 4, 4, 0}, layout = "fill", grid = 2, images = { hot = R(1110), pressed = R(1111) } })
        b:add(lh.node{ kind = "rect", stroke = "#9a9a9a", radius = 0, margin = 2 })
        b:add(lh.node{ kind = "text", text = strip_markup(a.label), font = bfont, color = "#000000", align = "center", valign = "center", ellipsis = true, margin = {4, 0, 4, 0} })
        b.on_click = function() lh.notification_action(n.id, a.key); close(2) end
        table.insert(t.button_nodes, b)
      end
    end
    t.expand_h = nb > 0 and 28 or 0
    t.w, t.h = root:desired(ctx.screen_w, ctx.screen_h)
    t.w, t.h = math.max(205, math.ceil(t.w)), math.max(60, math.ceil(t.h))                           -- 4002's own 205x60 is the minimum
    -- hover: stop + rewind to 0.75 over 200 ms + expand; leave: collapse + restart (pointer still inside = moving between children)
    local function hover(over)
      if not cur or cur ~= t then return end
      if not over then local px, py = lh.pointer(); if px >= t.x and px < t.x + t.w and py >= t.y and py < t.y + t.cur_h then return end end
      if over == t.hover then return end
      t.hover = over
      if over then t.paused = true; if t.remaining < 0.75 then lh.animate(t, "remaining", 0.75, 200, "linear") end
      else t.paused = false; t.deadline = lh.now() + t.remaining * t.ms end
      lh.animate(t, "expanded", over and 1 or 0, 200, "easeinout")
    end
    t.hover_fn = hover
    for _, node in ipairs{ root, content, closeb } do node.on_hover = function(self, over) hover(over) end end
    for _, b in ipairs(t.button_nodes) do b.on_hover = function(self, over) hover(over) end end
    t.set = function(self, p)                                                                           -- lh.animate target
      if p.expanded then
        t.cur_h = math.floor(t.h + t.expand_h * p.expanded + 0.5)
        t.buttons:set{ h = math.floor(t.expand_h * p.expanded + 0.5) }; t.options:set{ opacity = p.expanded }
        surf:move(t.x, t.y, t.w, t.cur_h)
      end
      if p.remaining then t.bar_update() end
    end
    t.bar_update = function()
      local full = t.cur_h - 4 - 3 - 4
      local h = math.max(0, math.floor(full * t.remaining + 0.5)); t.bar:set{ y = full - h, h = h }
    end
    return t
  end

  function lh._show_toast(n)
    local t = build(n); cur = t
    surf:root(t.root)
    -- placement (2.4.5): the work area = screen minus the taskbar strut (bottom) and the sidebar strut (right)
    local sbw = ((lh.sidebar_hidden and lh.sidebar_hidden()) or (lh.shell_get and lh.shell_get("sidebar") ~= "full")) and 0 or sb.w      -- minimized / pinned sidebar (6.4): the work area's right edge
    local work_r, work_b = ctx.screen_w - sbw, lh.taskbar_shown_top and lh.taskbar_shown_top() or (ctx.screen_h - tb.h)   -- M8: above the classic bar too
    if T.toast_place == "sidebar" then t.x, t.y = ctx.screen_w - t.w - 6, 20
    else t.x, t.y = work_r - t.w - 10, work_b - t.expand_h - t.h - 10 end
    t.cur_h = t.h; t.expanded = 0; t.remaining = 1
    local ms = n.expire_timeout or -1
    if ms < 0 then ms = T.toast_timeout or 10000 elseif ms > 0 then ms = math.max(ms, 5000) end      -- 10 s default, min 5 s
    t.ms = ms; t.deadline = lh.now() + ms
    surf:move(t.x, t.y, t.w, t.h); t.bar_update(); surf:show(); surf:raise()
    if ms > 0 then
      -- the bar loses one pixel per timeout/height ms; we tick at that rate (>= 25 ms)
      t.timer = lh.timer(function()
        if cur ~= t then return end
        if not t.paused then t.remaining = math.max(0, (t.deadline - lh.now()) / t.ms); t.bar_update() end
        if t.remaining <= 0 then close(1) end
      end, math.max(25, math.floor(ms / math.max(1, t.h - 11))), true)
    end
  end

  lh.on("notification", function(n)
    -- replaces_id: update in place (showing: rebuild; queued: swap)
    if n.replaces_id and n.replaces_id ~= 0 then
      if cur and cur.n.id == n.replaces_id then local old = cur; if old.timer then lh.cancel(old.timer) end cur = nil; old.root:clear(); lh._show_toast(n) return end
      for i, q in ipairs(queue) do if q.id == n.replaces_id then queue[i] = n return end end
    end
    if cur then table.insert(queue, n) else lh._show_toast(n) end
  end)
  lh.on("notification-close", function(id)
    if cur and cur.n.id == id then close(3) return end
    for i, q in ipairs(queue) do if q.id == id then table.remove(queue, i); lh.notification_closed(id, 3) return end end
  end)
  lh.toast = { close = close, queue = queue, current = function() return cur end }
end
