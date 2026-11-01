/* luxshell notify: the org.freedesktop.Notifications server (Desktop Notifications spec 1.2) on the session bus, libdbus-1.
   Each Notify call becomes a Lua "notification" event; runtime.lua renders it as a Longhorn toast (notification.xaml,
   SHELL-DESIGN 2.4.5) and reports back through lh.notification_closed / lh.notification_action. */
#include "luxshell.h"
#include <dbus/dbus.h>
#include <lua5.4/lauxlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NPATH "/org/freedesktop/Notifications"
#define NIFACE "org.freedesktop.Notifications"
static DBusConnection *conn;
static unsigned next_id = 1;

static const char *introspect_xml =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" \"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
    "<node><interface name=\"org.freedesktop.DBus.Introspectable\"><method name=\"Introspect\"><arg direction=\"out\" type=\"s\"/></method></interface>"
    "<interface name=\"" NIFACE "\">"
    "<method name=\"GetCapabilities\"><arg direction=\"out\" name=\"capabilities\" type=\"as\"/></method>"
    "<method name=\"Notify\"><arg direction=\"in\" name=\"app_name\" type=\"s\"/><arg direction=\"in\" name=\"replaces_id\" type=\"u\"/>"
    "<arg direction=\"in\" name=\"app_icon\" type=\"s\"/><arg direction=\"in\" name=\"summary\" type=\"s\"/><arg direction=\"in\" name=\"body\" type=\"s\"/>"
    "<arg direction=\"in\" name=\"actions\" type=\"as\"/><arg direction=\"in\" name=\"hints\" type=\"a{sv}\"/><arg direction=\"in\" name=\"expire_timeout\" type=\"i\"/>"
    "<arg direction=\"out\" name=\"id\" type=\"u\"/></method>"
    "<method name=\"CloseNotification\"><arg direction=\"in\" name=\"id\" type=\"u\"/></method>"
    "<method name=\"GetServerInformation\"><arg direction=\"out\" name=\"name\" type=\"s\"/><arg direction=\"out\" name=\"vendor\" type=\"s\"/>"
    "<arg direction=\"out\" name=\"version\" type=\"s\"/><arg direction=\"out\" name=\"spec_version\" type=\"s\"/></method>"
    "<signal name=\"NotificationClosed\"><arg name=\"id\" type=\"u\"/><arg name=\"reason\" type=\"u\"/></signal>"
    "<signal name=\"ActionInvoked\"><arg name=\"id\" type=\"u\"/><arg name=\"action_key\" type=\"s\"/></signal>"
    "</interface></node>\n";

/* image-data hint (iiibiiay: width, height, rowstride, has_alpha, bits_per_sample, channels, data) → premultiplied ARGB32 */
static cairo_surface_t *image_from_iter(DBusMessageIter *v) {
    if (dbus_message_iter_get_arg_type(v) != DBUS_TYPE_STRUCT) return NULL;
    DBusMessageIter st; dbus_message_iter_recurse(v, &st); dbus_int32_t f[6] = {0}; dbus_bool_t alpha = 0;
    for (int i = 0; i < 6; i++) {
        int t = dbus_message_iter_get_arg_type(&st);
        if (i == 3) { if (t != DBUS_TYPE_BOOLEAN) return NULL; dbus_message_iter_get_basic(&st, &alpha); }
        else { if (t != DBUS_TYPE_INT32) return NULL; dbus_message_iter_get_basic(&st, &f[i]); }
        dbus_message_iter_next(&st);
    }
    if (dbus_message_iter_get_arg_type(&st) != DBUS_TYPE_ARRAY) return NULL;
    DBusMessageIter arr; dbus_message_iter_recurse(&st, &arr); const unsigned char *px = NULL; int len = 0; dbus_message_iter_get_fixed_array(&arr, &px, &len);
    int w = f[0], h = f[1], stride = f[2], bps = f[4], ch = f[5];
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024 || bps != 8 || (ch != 3 && ch != 4) || stride < w * ch || (long)stride * (h - 1) + (long)w * ch > len) return NULL;
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h); unsigned char *o = cairo_image_surface_get_data(s); int os = cairo_image_surface_get_stride(s);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        const unsigned char *p = px + (long)y * stride + x * ch; unsigned a = (ch == 4 && alpha) ? p[3] : 255;
        *(unsigned *)(o + y * os + x * 4) = (a << 24) | ((p[0] * a / 255) << 16) | ((p[1] * a / 255) << 8) | (p[2] * a / 255);
    }
    cairo_surface_mark_dirty(s); return s;
}

static void push_str(const char *k, const char *v) { lua_pushstring(L, v ? v : ""); lua_setfield(L, -2, k); }
static void push_int(const char *k, long long v) { lua_pushinteger(L, v); lua_setfield(L, -2, k); }

static DBusMessage *on_notify(DBusMessage *msg) {
    DBusMessageIter it; const char *app = "", *icon = "", *summary = "", *body = ""; dbus_uint32_t replaces = 0; dbus_int32_t timeout = -1;
    if (!dbus_message_iter_init(msg, &it) || strcmp(dbus_message_get_signature(msg), "susssasa{sv}i"))
        return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, "Notify takes susssasa{sv}i");
    dbus_message_iter_get_basic(&it, &app); dbus_message_iter_next(&it);
    dbus_message_iter_get_basic(&it, &replaces); dbus_message_iter_next(&it);
    dbus_message_iter_get_basic(&it, &icon); dbus_message_iter_next(&it);
    dbus_message_iter_get_basic(&it, &summary); dbus_message_iter_next(&it);
    dbus_message_iter_get_basic(&it, &body); dbus_message_iter_next(&it);
    unsigned id = replaces ? replaces : next_id++;
    lua_newtable(L);
    push_int("id", id); push_int("replaces_id", replaces); push_str("app_name", app); push_str("app_icon", icon); push_str("summary", summary); push_str("body", body);
    /* actions: flat key,label list → { {key=, label=}, ... } */
    lua_newtable(L); { DBusMessageIter a; dbus_message_iter_recurse(&it, &a); int k = 1; const char *key = NULL;
        while (dbus_message_iter_get_arg_type(&a) == DBUS_TYPE_STRING) { const char *s; dbus_message_iter_get_basic(&a, &s);
            if (!key) key = s; else { lua_newtable(L); push_str("key", key); push_str("label", s); lua_rawseti(L, -2, k++); key = NULL; }
            dbus_message_iter_next(&a); } }
    lua_setfield(L, -2, "actions");
    dbus_message_iter_next(&it);
    /* hints */
    lua_newtable(L); { DBusMessageIter a; dbus_message_iter_recurse(&it, &a);
        while (dbus_message_iter_get_arg_type(&a) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter e, v; const char *k; dbus_message_iter_recurse(&a, &e); dbus_message_iter_get_basic(&e, &k); dbus_message_iter_next(&e); dbus_message_iter_recurse(&e, &v);
            int t = dbus_message_iter_get_arg_type(&v);
            if (!strcmp(k, "image-data") || !strcmp(k, "image_data") || !strcmp(k, "icon_data")) {
                cairo_surface_t *s = image_from_iter(&v);
                if (s) { char key[64]; snprintf(key, sizeof key, "@notify:%u", id); icon_register(key, s); push_str("image", key); }
            } else if (t == DBUS_TYPE_STRING) { const char *s; dbus_message_iter_get_basic(&v, &s); push_str(k, s); }
            else if (t == DBUS_TYPE_BYTE) { unsigned char b; dbus_message_iter_get_basic(&v, &b); push_int(k, b); }
            else if (t == DBUS_TYPE_BOOLEAN) { dbus_bool_t b; dbus_message_iter_get_basic(&v, &b); lua_pushboolean(L, b); lua_setfield(L, -2, k); }
            else if (t == DBUS_TYPE_INT32 || t == DBUS_TYPE_UINT32) { dbus_int32_t i; dbus_message_iter_get_basic(&v, &i); push_int(k, t == DBUS_TYPE_UINT32 ? (long long)(dbus_uint32_t)i : i); }
            dbus_message_iter_next(&a);
        } }
    lua_pushvalue(L, -1); lua_setfield(L, -3, "hints");
    lua_getfield(L, -1, "urgency"); if (lua_isinteger(L, -1)) lua_setfield(L, -3, "urgency"); else lua_pop(L, 1);
    lua_getfield(L, -1, "image"); if (lua_isstring(L, -1)) lua_setfield(L, -3, "image"); else lua_pop(L, 1);
    lua_getfield(L, -1, "image-path"); if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_getfield(L, -1, "image_path"); }
    if (lua_isstring(L, -1)) lua_setfield(L, -3, "image_path"); else lua_pop(L, 1);
    lua_pop(L, 1);   /* hints */
    dbus_message_iter_next(&it); dbus_message_iter_get_basic(&it, &timeout);
    push_int("expire_timeout", timeout);
    emit("notification", 1);
    DBusMessage *r = dbus_message_new_method_return(msg); dbus_uint32_t rid = id; dbus_message_append_args(r, DBUS_TYPE_UINT32, &rid, DBUS_TYPE_INVALID); return r;
}

static DBusHandlerResult on_message(DBusConnection *c, DBusMessage *msg, void *ud) {
    (void)ud; DBusMessage *r = NULL;
    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Introspectable", "Introspect")) {
        r = dbus_message_new_method_return(msg); dbus_message_append_args(r, DBUS_TYPE_STRING, &introspect_xml, DBUS_TYPE_INVALID);
    } else if (dbus_message_is_method_call(msg, NIFACE, "GetCapabilities")) {
        /* body, actions, icon-static: what the toast renders (no markup, no persistence, no sounds) */
        const char *caps[] = { "body", "actions", "icon-static" }; const char **p = caps;
        r = dbus_message_new_method_return(msg); dbus_message_append_args(r, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &p, 3, DBUS_TYPE_INVALID);
    } else if (dbus_message_is_method_call(msg, NIFACE, "GetServerInformation")) {
        const char *name = "luxshell", *vendor = "lhde", *version = "0.1", *spec = "1.2";
        r = dbus_message_new_method_return(msg); dbus_message_append_args(r, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &vendor, DBUS_TYPE_STRING, &version, DBUS_TYPE_STRING, &spec, DBUS_TYPE_INVALID);
    } else if (dbus_message_is_method_call(msg, NIFACE, "Notify")) {
        r = on_notify(msg);
    } else if (dbus_message_is_method_call(msg, NIFACE, "CloseNotification")) {
        dbus_uint32_t id = 0;
        if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID)) r = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, "CloseNotification takes u");
        else { lua_pushinteger(L, id); emit("notification-close", 1); r = dbus_message_new_method_return(msg); }
    } else return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    if (r) { dbus_connection_send(c, r, NULL); dbus_message_unref(r); }
    return DBUS_HANDLER_RESULT_HANDLED;
}

int notify_start(void) {
    if (!getenv("DBUS_SESSION_BUS_ADDRESS")) { fprintf(stderr, "luxshell: no DBUS_SESSION_BUS_ADDRESS, notifications off (see USAGE.md)\n"); return -1; }   /* no X11 autolaunch */
    DBusError err; dbus_error_init(&err);
    conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
    if (!conn) { fprintf(stderr, "luxshell: session bus: %s\n", err.message); dbus_error_free(&err); return -1; }
    dbus_connection_set_exit_on_disconnect(conn, FALSE);
    int r = dbus_bus_request_name(conn, NIFACE, DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
    if (r != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        fprintf(stderr, "luxshell: %s is owned by another notification daemon on this bus, toasts off%s%s\n", NIFACE, dbus_error_is_set(&err) ? ": " : "", dbus_error_is_set(&err) ? err.message : "");
        dbus_error_free(&err); dbus_connection_close(conn); dbus_connection_unref(conn); conn = NULL; return -1;
    }
    static const DBusObjectPathVTable vt = { .message_function = on_message };
    dbus_connection_register_object_path(conn, NPATH, &vt, NULL);
    int fd = -1; dbus_connection_get_unix_fd(conn, &fd);
    fprintf(stderr, "luxshell: %s on %s\n", NIFACE, getenv("DBUS_SESSION_BUS_ADDRESS"));
    return fd;
}
void notify_dispatch(void) {
    if (!conn) return;
    if (!dbus_connection_read_write(conn, 0)) { fprintf(stderr, "luxshell: session bus closed, toasts off\n"); dbus_connection_unref(conn); conn = NULL; return; }
    while (dbus_connection_dispatch(conn) == DBUS_DISPATCH_DATA_REMAINS) ;
    dbus_connection_flush(conn);
}
bool notify_pending(void) { return conn && dbus_connection_get_dispatch_status(conn) == DBUS_DISPATCH_DATA_REMAINS; }
static void signal_send(DBusMessage *m) { if (conn) { dbus_connection_send(conn, m, NULL); dbus_connection_flush(conn); } dbus_message_unref(m); }
void notify_closed(unsigned id, unsigned reason) {
    DBusMessage *m = dbus_message_new_signal(NPATH, NIFACE, "NotificationClosed"); dbus_uint32_t a = id, b = reason;
    dbus_message_append_args(m, DBUS_TYPE_UINT32, &a, DBUS_TYPE_UINT32, &b, DBUS_TYPE_INVALID); signal_send(m);
}
void notify_action(unsigned id, const char *key) {
    DBusMessage *m = dbus_message_new_signal(NPATH, NIFACE, "ActionInvoked"); dbus_uint32_t a = id;
    dbus_message_append_args(m, DBUS_TYPE_UINT32, &a, DBUS_TYPE_STRING, &key, DBUS_TYPE_INVALID); signal_send(m);
}
