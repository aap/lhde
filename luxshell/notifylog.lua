-- The notification history behind the Notification History tile (tiles/notifyhistory.lua), 4074's NotificationData store
-- (GetAllNotifications; RulesEngine NotificationArrived / Revoked / Viewed, WCPClient IL). Every notification the
-- org.freedesktop.Notifications server (notify.c) receives lands here, whether or not the tile is shown, except those with the
-- `transient` hint (the spec's "bypass the history"). An app's CloseNotification revokes its entry (4074 NotificationRevoked);
-- replaces_id updates an entry in place. An entry is *viewed* (4074 _fViewed: the grey bubble 4013 instead of the yellow 4014)
-- once the user dismissed or clicked its toast, re-showed it from the history, or saw it in the history flyout; a toast that just
-- timed out stays unviewed.
-- Kept in ~/.local/state/lhde/luxshell-notifications, newest last, at most 100
-- entries: one line per entry, tab-separated: time (epoch s), viewed (0/1), app name, app icon (name or path), summary, body;
-- backslash, tab and newline escaped as \\ \t \n.
--   lh.notifylog.list()            entries oldest first: { key, id (this session's server id or nil), t, viewed, app, icon, summary, body }
--   lh.notifylog.unread()          the number not viewed
--   lh.notifylog.view(e) / view_all()   mark viewed;  lh.notifylog.clear()   "Delete All Notifications"
--   lh.notifylog.reshow(e)         the toast again (4074 Notification._Show with _fReshow), without its actions; marks it viewed
-- Every change emits "notifylog".
return function(ctx)
  local MAX = 100
  local path = lh.state_file("luxshell-notifications")
  local L = { path = path }
  local entries, serial = {}, 0
  local function esc(s) return (tostring(s or ""):gsub("\\", "\\\\"):gsub("\t", "\\t"):gsub("\n", "\\n"):gsub("\r", "")) end
  local function unesc(s) return (s:gsub("\\(.)", function(c) return c == "t" and "\t" or c == "n" and "\n" or c end)) end
  local function strip(s) return (s or ""):gsub("<[^>]*>", ""):gsub("&lt;", "<"):gsub("&gt;", ">"):gsub("&quot;", '"'):gsub("&apos;", "'"):gsub("&amp;", "&") end
  local function load()
    local f = io.open(path, "r"); if not f then return end
    for line in f:lines() do
      if not line:match("^%s*[;#]") then
        local p = {}; for field in (line .. "\t"):gmatch("([^\t]*)\t") do p[#p + 1] = unesc(field) end
        local t = tonumber(p[1])
        if t and #p >= 6 then serial = serial + 1; entries[#entries + 1] = { key = serial, t = t, viewed = p[2] == "1", app = p[3], icon = p[4], summary = p[5], body = p[6] } end
      end
    end
    f:close()
    while #entries > MAX do table.remove(entries, 1) end
  end
  local function save()
    os.execute("mkdir -p " .. lh.session.quote(path:match("^(.*)/") or "."))
    local f = io.open(path .. ".tmp", "w"); if not f then return end
    f:write("; luxshell notification history (tiles/notifyhistory.lua): time viewed app icon summary body, tab-separated\n")
    for _, e in ipairs(entries) do f:write(table.concat({ tostring(e.t), e.viewed and "1" or "0", esc(e.app), esc(e.icon), esc(e.summary), esc(e.body) }, "\t"), "\n") end
    f:close(); os.rename(path .. ".tmp", path)
  end
  local function changed() save(); lh._emit("notifylog") end
  load()

  function L.list() return entries end
  function L.unread() local n = 0; for _, e in ipairs(entries) do if not e.viewed then n = n + 1 end end return n end
  function L.view(e) if e and not e.viewed then e.viewed = true; changed() end end
  function L.view_all() local any = false; for _, e in ipairs(entries) do if not e.viewed then e.viewed = true; any = true end end if any then changed() end end
  function L.clear() if #entries > 0 then entries = {}; changed() end end
  function L.reshow(e)
    local n = { id = 0, replaces_id = 0, app_name = e.app, app_icon = e.icon, summary = e.summary, body = e.body, actions = {}, hints = {}, expire_timeout = -1 }
    L.view(e)
    if not lh._show_toast then return end
    local cur = lh.toast and lh.toast.current()
    if cur then table.insert(lh.toast.queue, n) else lh._show_toast(n) end
  end

  lh.on("notification", function(n)
    local tr = n.hints and n.hints.transient
    if tr == true or (type(tr) == "number" and tr ~= 0) then return end
    local e
    if n.replaces_id and n.replaces_id ~= 0 then for _, x in ipairs(entries) do if x.id == n.replaces_id then e = x end end end
    if e then for i, x in ipairs(entries) do if x == e then table.remove(entries, i) break end end
    else serial = serial + 1; e = { key = serial } end
    e.id, e.t, e.viewed = n.id, os.time(), false
    e.app, e.summary, e.body = n.app_name or "", strip(n.summary), strip(n.body)
    e.icon = (n.image_path and n.image_path ~= "" and n.image_path:gsub("^file://", "")) or n.app_icon or ""
    entries[#entries + 1] = e
    while #entries > MAX do table.remove(entries, 1) end
    changed()
  end)
  lh.on("notification-close", function(id)          -- CloseNotification from the app: 4074 NotificationRevoked
    for i, x in ipairs(entries) do if x.id == id then table.remove(entries, i); changed() return end end
  end)
  -- the toast's close reasons (toast.lua → notify.c): 2 = dismissed by the user (close button, a click, an action) → viewed.
  -- A re-shown toast has id 0 and sends no NotificationClosed (no client asked for it).
  local closed = lh.notification_closed
  lh.notification_closed = function(id, reason)
    if id == 0 then return end
    if reason == 2 then for _, x in ipairs(entries) do if x.id == id then L.view(x) end end end
    return closed(id, reason)
  end
  local action = lh.notification_action
  lh.notification_action = function(id, key) if id == 0 then return end return action(id, key) end
  lh.notifylog = L
end
