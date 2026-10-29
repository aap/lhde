-- Start panels that differ from the shell pack's own (themes.ini `startpanel = NAME`, docs/history/THEMES.md; runtime.lua apply_startpanel).
-- Each entry replaces the pack's `startpanel` (packs/<build>/startpanel-* from lhde-import / shellpack.sh), `startmenu` (metrics, startmenu.lua) and `menu`
-- (flat-menu colours of the context menus and of the All Programs cascade where the msstyles has no hot button image).
--
-- Windows XP Luna (the 4051 luna.msstyles, variants NORMALBLUE / NORMALHOMESTEAD / NORMALMETALLIC). Metrics from the XP SP1
-- source (shell/explorer/desktop2) and the style's own DEFAULTPANESIZEs: panel 380 wide; USERPANE 380x64; PROGLIST 190x306 +
-- MOREPROGRAMS 190x30 beside PLACESLIST 190x336; LOGOFF 380x40. sfthost _ComputeTileMetrics: tile = max(icon, text) +
-- 4*SM_CYEDGE → programs 40 (32 px icons, two Tahoma 8 lines), places 32 (ICONSIZE_MEDIUM 24 px); separators 4*cySep = 8
-- (PROGLISTSEPARATOR / PLACESLISTSEPARATOR are 2 px high). Selection = COLOR_HIGHLIGHT / HIGHLIGHTTEXT of the scheme;
-- context menus = FLATMENUS: MENU, MENUTEXT, MENUHILIGHT, BTNSHADOW border, GRAYTEXT ([SysMetrics] of each variant).
-- Bottom row: "Log Off" (key glyph) and "Turn Off Computer" (power glyph), right-aligned. No outer frame lines: the
-- panel images carry their own edges. No XP screenshot of the start menu was at hand: metrics are the source's, not measured.
local function xp(scheme, sel, sel_text, menu)
  return {
    startpanel = "4051/startpanel-luna-" .. scheme,
    startmenu = { font = "Tahoma 11px", width = 380, left_w = 190, body_min = 336, prog_tile = 40, places_h = 32, sep_left = 8, sep_right = 8,
                  more_h = 30, logoff_h = 40, userpane_h = 64, frame = {}, logoff_buttons = { "logoff", "turnoff" },
                  logoff_text = { turnoff = "Turn Off Computer" }, hot_bg = sel, hot_text = sel_text, hot_radius = 0,
                  user_font = "ITC Franklin Gothic Std Medium 19px", pic_x = 5, pic_y = 5, pic_size = 55, user_x = 68,
                  more_text_x = 44, more_text_dy = 0, prog_dy = 0, places_dy = 0 },
    menu = menu,
  }
end
return {
  ["luna-blue"] = xp("blue", "#316ac5", "#ffffff",
    { bg = "#ffffff", text = "#000000", hot = "#316ac5", hot_text = "#ffffff", border = "#aca899", gray = "#aca899" }),
  ["luna-olive"] = xp("olive", "#93a070", "#ffffff",
    { bg = "#ffffff", text = "#000000", hot = "#93a070", hot_text = "#ffffff", border = "#aca899", gray = "#aca899" }),
  ["luna-silver"] = xp("silver", "#b2b4bf", "#000000",
    { bg = "#ffffff", text = "#000000", hot = "#bbb7c7", hot_text = "#000000", border = "#9d9da1", gray = "#aca899" }),
}
