-- Generic tile flyout (flyout.xaml, SHELL-DESIGN 2.3.3 / 2.4.4 / 3.4):
--   FillPanel { HackImage FlyoutBackground (1175, HackNineGrid 53,3,3,3 [Slate 1109 grid 3]);
--               DockPanel { DockPanel#FlyoutTop (Top) { OptionsButton#FlyoutOptions (Right, Height 17: glyph Border 4,5,4,4 1176/1177,
--                           "Options" white, Opacity 0, fades in on hover 120 ms) }; Border FlyoutBorder 8,4,8,8 [4,0,4,4] { FlyoutContent } } }
-- A Popup at the tile rect on the free side (placement 2 = left of a Flipped bar), CloseMode 2: closes on a click outside,
-- a second click on the tile, or Escape-less here (no keyboard). One flyout at a time; it sets SidebarControl.ShowingFlyout.
-- A tile opts in with `flyout = function(self) return content_node, width, height end` (width/height optional: measured).
-- Any other owner (the settings, run and session flyouts) works the same and may add
-- `place = function(self, fw, fh) return x, y end` (default: left of the tile) and `on_key = function(self, name, text)`
-- (the flyout then grabs the keyboard; Escape always closes) and `stay_open = true` (no grab at all: an outside click leaves it
-- open and only the tile closes it).
return function(ctx)
  local R, T, sb = ctx.R, ctx.T, ctx.sb
  local fl = { surface = lh.surface{ name = "luxshell-flyout", popup = true, type = "popup_menu", x = 0, y = 0, w = 10, h = 10 } }
  local border, content
  local chrome_h = 17          -- the chrome's height above and below the content (17: the 4074 options row)
  -- pack key flyout = { top, top_h, top_grid, content, bottom, bottom_h, bottom_grid, opt_hot, opt_pressed, glyph, glyph_w,
  -- glyph_h, glyph_pad, font }: 4042 main's FlyoutElement (Microsoft.Windows.Client) under Sidebarstylesheet_themed: FlyoutTop
  -- Dock Top Height 27 = FLYOUTTOP (NineGridMargin 2,2,0,6) { FlyoutOptions Dock Right (FLYOUTOPTIONSBACK hot / pressed) {
  -- glyph FLYOUTOPTIONSGLYPH Dock Right Padding 4,0,4,0; "Options" Franklin Gothic Medium 14 white Padding 0,6,4,6 }; FlyoutTitle
  -- Fill (Padding 6,6,4,6; left empty here) }; FlyoutBottom Dock Bottom Height 26 = FLYOUTBOTTOM (2,2,0,6) { FlyoutLink };
  -- FlyoutContent Fill = FLYOUTCONTENT stretched, no padding
  local function plex_chrome(f)
    border = {0, 0, 0, 0}; chrome_h = f.top_h + f.bottom_h
    local root = lh.node{ layout = "dock", hoverable = true }
    local top = root:add(lh.node{ dock = "top", h = f.top_h, layout = "fill" })
    top:add(lh.node{ kind = "ninegrid", image = R(f.top), grid = f.top_grid })
    local row = top:add(lh.node{ layout = "dock" })
    local opts = row:add(lh.node{ kind = "button", dock = "right", layout = "dock", h = f.top_h, images = { hot = R(f.opt_hot), pressed = R(f.opt_pressed) } })
    local gp = f.glyph_pad
    local slot = opts:add(lh.node{ dock = "right", w = gp[1] + f.glyph_w + gp[3], layout = "canvas" })
    slot:add(lh.node{ kind = "image", image = R(f.glyph), x = gp[1], y = math.floor((f.top_h - f.glyph_h) / 2), w = f.glyph_w, h = f.glyph_h })
    opts:add(lh.node{ kind = "text", text = "Options", color = "#ffffff", font = f.font, dock = "right", margin = {0, 6, 4, 6} })
    opts.on_click = function() if fl.tile and fl.tile.flyout_options then fl.tile:flyout_options() end end
    local bottom = root:add(lh.node{ dock = "bottom", h = f.bottom_h, layout = "fill" })
    bottom:add(lh.node{ kind = "ninegrid", image = R(f.bottom), grid = f.bottom_grid })
    local body = root:add(lh.node{ dock = "fill", layout = "fill" })
    body:add(lh.node{ kind = "ninegrid", image = R(f.content) })
    content = body:add(lh.node{ layout = "fill" })
    fl.surface:root(root)
  end
  -- the chrome follows the pack: built at load and again after a theme switch (event "pack", SHELL-DESIGN 14); the old tree is
  -- left to the garbage of the switch (it is never shown again)
  local function chrome()
    if T.flyout then return plex_chrome(T.flyout) end
    chrome_h = 17
    local slate = T.family == "slate"
    border = slate and {4, 0, 4, 4} or {8, 4, 8, 8}
    local root = lh.node{ layout = "fill", hoverable = true }
    -- grid order of our nine-grid nodes is top,right,bottom,left (HackNineGrid is left,top,right,bottom)
    root:add(lh.node{ kind = "ninegrid", image = R(1175), grid = slate and 3 or {3, 3, 3, 53} })
    local dock = root:add(lh.node{ layout = "dock" })
    local top = dock:add(lh.node{ dock = "top", h = 17, layout = "dock" })
    local opts = top:add(lh.node{ kind = "button", dock = "right", layout = "dock", h = 17 })
    local glyph = opts:add(lh.node{ kind = "image", image = R(1176), w = 17, h = 17, dock = "right", margin = {4, 5, 4, 6}, filter = "nearest" })   -- 9x6 as the sidebar's (runtime.lua)
    local text = opts:add(lh.node{ kind = "text", text = "Options", color = "#ffffff", font = T.font, dock = "right", valign = "center", opacity = 0 })
    opts.on_hover = function(self, over) glyph:set{ image = R(over and 1177 or 1176) }; lh.animate(text, "opacity", over and 1 or 0, over and 120 or 80) end
    opts.on_click = function() if fl.tile and fl.tile.flyout_options then fl.tile:flyout_options() end end
    content = dock:add(lh.node{ dock = "fill", layout = "fill", margin = border })
    fl.surface:root(root)
  end
  chrome()
  lh.on("pack", function() lh.close_flyout(); chrome() end)
  fl.surface.on_outside = function() if not (fl.tile and fl.tile.stay_open) then lh.close_flyout() end end
  fl.surface.on_key = function(_, name, text, state, pressed)
    if not pressed or not fl.tile then return end
    if name == "Escape" then lh.close_flyout() return end
    if fl.tile.on_key then fl.tile:on_key(name, text, state) end
  end

  function lh.close_flyout()
    if not fl.tile then return end
    local t = fl.tile; fl.tile = nil; lh.flyout_tile = nil
    fl.surface:ungrab(); fl.surface:hide(); content:clear()
    if t.flyout_closed then t:flyout_closed() end
  end
  function lh.open_flyout(tile)
    lh.close_flyout()
    local node, w, h = tile:flyout()
    if not node then return end
    content:add(node)
    if not w or not h then local dw, dh = node:desired(ctx.screen_w, ctx.screen_h); w = w or dw; h = h or dh end
    local fw, fh = math.ceil(w + border[1] + border[3]), math.ceil(h + chrome_h + border[2] + border[4])
    local x, y
    if tile.place then x, y = tile:place(fw, fh)
    else
      local tx, ty = sb.tile_rect(tile)
      x = (sb.geo and sb.geo.x or sb.surface.x) - fw              -- placement 2: left of the Flipped (right-docked) bar
      y = math.max(0, math.min(ty, ctx.screen_h - fh))
    end
    fl.surface:move(x, y, fw, fh); fl.surface:show(); fl.surface:raise()
    local ok, kb = true, true
    if not tile.stay_open then ok, kb = fl.surface:grab(tile.on_key ~= nil) end
    if tile.on_key and not kb then   -- the keyboard may still be held by whoever opened us (luxwm's Super grab, a menu): retry
      local tries = 0
      local function retry() tries = tries + 1; if fl.tile ~= tile then return end local _, k = fl.surface:grab(true); if not k and tries < 20 then lh.timer(retry, 50) end end
      lh.timer(retry, 50)
    end
    fl.tile = tile; lh.flyout_tile = tile
  end
  function lh.toggle_flyout(tile) if fl.tile == tile then lh.close_flyout() else lh.open_flyout(tile) end end
  -- a press on another luxshell surface (owner events of the grab go there) also closes it, except on the tile itself
  lh.on("surface-press", function(x, y, button, win)
    if fl.tile and not fl.tile.stay_open and win ~= fl.surface:id() and not (win == sb.surface:id() and fl.tile.node and (function()
        local nx, ny, nw, nh = fl.tile.node:bounds(); return x >= nx and x < nx + nw and y >= ny and y < ny + nh end)()) then lh.close_flyout() end
  end)
  lh.flyout = fl
end
