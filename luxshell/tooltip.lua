-- Tooltips for luxshell nodes (the tiles' ToolTip / TooltipText; QuickLaunchTile sets ItemControlToolTipEnabled, the
-- NotificationElement its arrival time and text). The look is the XP/Avalon-2004 system tooltip
-- (InfoBk #FFFFE1, 1 px black frame, Tahoma 8 black, 3 px side padding): taste, no 4074 screenshot shows a sidebar tooltip.
--   lh.tooltip(node, text)       text = a string or function() -> string (read when it shows); the node becomes hoverable.
--                                It shows 500 ms after the pointer enters (SPI_GETMOUSEHOVERTIME's 400 + XP's initial delay), 20 px
--                                under the pointer, clamped to the screen; leaving, a press or 5 s hide it.
--   lh.tooltip_hide()
return function(ctx)
  local surf = lh.surface{ name = "luxshell-tooltip", popup = true, type = "popup_menu", x = 0, y = 0, w = 10, h = 10 }
  local pending, shown_timer, owner
  local function hide()
    if pending then lh.cancel(pending); pending = nil end
    if shown_timer then lh.cancel(shown_timer); shown_timer = nil end
    if owner then owner = nil; surf:hide() end
  end
  local function show(node, text)
    if type(text) == "function" then text = text() end
    if not text or text == "" then return end
    local root = lh.node{ layout = "fill" }
    root:add(lh.node{ kind = "rect", fill = "#ffffe1", stroke = "#000000" })
    root:add(lh.node{ kind = "text", text = text, font = "Tahoma 8", color = "#000000", margin = { 4, 2, 4, 2 } })
    surf:root(root)
    local w, h = root:desired(ctx.screen_w, ctx.screen_h); w, h = math.ceil(w), math.ceil(h)
    local px, py = lh.pointer()
    local x, y = math.max(0, math.min(px, ctx.screen_w - w)), py + 20
    if y + h > ctx.screen_h then y = py - h - 4 end
    surf:move(x, y, w, h); surf:show(); surf:raise()
    owner = node
    shown_timer = lh.timer(hide, 5000)
  end
  function lh.tooltip(node, text)
    node:set{ hoverable = true }
    local hover, press = node.on_hover, node.on_press
    node.on_hover = function(self, over)
      if hover then hover(self, over) end
      hide()
      if over then pending = lh.timer(function() pending = nil; show(self, text) end, 500) end
    end
    node.on_press = function(self, b) hide(); if press then return press(self, b) end end
  end
  lh.tooltip_hide = hide
  lh.on("pack", hide)
end
