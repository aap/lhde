// flxp-portal: an xdg-desktop-portal backend for org.freedesktop.impl.portal.FileChooser
// (OpenFile, SaveFile, SaveFiles) on the well-known name
// org.freedesktop.impl.portal.desktop.flxp, object /org/freedesktop/portal/desktop.
// Every request runs flxp-filedialog in a child process (a crash or a hung X
// connection takes one dialog down, not the service; requests run side by
// side), with the request's handle exported as org.freedesktop.impl.portal.Request
// while the dialog is up so Close can end it. libdbus-1 (no sd-bus on this
// system), a plain poll loop. Exits after a few idle minutes; D-Bus activation
// starts it again. No win32 here.
#include <dbus/dbus.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include <stdint.h>
#include <string>
#include <vector>
#include "shell/config.h"

namespace {   // the program's own, up to main

static const char* BUS_NAME = "org.freedesktop.impl.portal.desktop.flxp";
static const char* OBJ_PATH = "/org/freedesktop/portal/desktop";
static const char* IFACE = "org.freedesktop.impl.portal.FileChooser";
static const char* REQUEST_IFACE = "org.freedesktop.impl.portal.Request";
static const int IDLE_EXIT_S = 300;

static bool g_debug;
#define LOG(...) do { if (g_debug) { fprintf(stderr, "flxp-portal: " __VA_ARGS__); fputc('\n', stderr); } } while (0)

static DBusConnection* g_conn;

// ---- options ---------------------------------------------------------------------

struct PFilter {
    std::string name;
    std::vector<std::pair<uint32_t, std::string>> items;   // 0 = glob, 1 = mime type
};

struct Request {
    std::string method;          // OpenFile / SaveFile / SaveFiles
    std::string handle, app_id, parent, title;
    std::string accept_label, current_name, current_folder, current_file;
    bool modal = true, multiple = false, directory = false, has_choices = false;
    std::vector<PFilter> filters;
    PFilter current_filter;
    bool has_current_filter = false;
    std::vector<std::string> files;   // SaveFiles
    DBusMessage* call = nullptr;      // the pending method call (ref held)
    pid_t pid = -1;
    int out_fd = -1;
    std::string output;
    bool closed = false;              // Request.Close was called
};

static std::vector<Request*> g_requests;
static time_t g_last_activity;

static std::string read_bytes(DBusMessageIter* v)   // "ay", trailing NUL dropped
{
    std::string s;
    if (dbus_message_iter_get_arg_type(v) != DBUS_TYPE_ARRAY) return s;
    DBusMessageIter a;
    dbus_message_iter_recurse(v, &a);
    if (dbus_message_iter_get_arg_type(&a) == DBUS_TYPE_BYTE) {
        const char* data = nullptr; int n = 0;
        dbus_message_iter_get_fixed_array(&a, &data, &n);
        if (data && n > 0) s.assign(data, (size_t)n);
    }
    while (!s.empty() && s.back() == 0) s.pop_back();
    return s;
}

static bool read_filter(DBusMessageIter* st, PFilter& f)   // (sa(us))
{
    if (dbus_message_iter_get_arg_type(st) != DBUS_TYPE_STRUCT) return false;
    DBusMessageIter s;
    dbus_message_iter_recurse(st, &s);
    if (dbus_message_iter_get_arg_type(&s) != DBUS_TYPE_STRING) return false;
    const char* name = nullptr;
    dbus_message_iter_get_basic(&s, &name);
    f.name = name ? name : "";
    if (!dbus_message_iter_next(&s) || dbus_message_iter_get_arg_type(&s) != DBUS_TYPE_ARRAY) return true;
    DBusMessageIter a;
    dbus_message_iter_recurse(&s, &a);
    while (dbus_message_iter_get_arg_type(&a) == DBUS_TYPE_STRUCT) {
        DBusMessageIter it;
        dbus_message_iter_recurse(&a, &it);
        uint32_t kind = 0; const char* pat = nullptr;
        if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_UINT32) dbus_message_iter_get_basic(&it, &kind);
        if (dbus_message_iter_next(&it) && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_STRING) dbus_message_iter_get_basic(&it, &pat);
        if (pat) f.items.push_back({ kind, pat });
        dbus_message_iter_next(&a);
    }
    return true;
}

static void read_options(DBusMessageIter* dict, Request& r)
{
    if (dbus_message_iter_get_arg_type(dict) != DBUS_TYPE_ARRAY) return;
    DBusMessageIter a;
    dbus_message_iter_recurse(dict, &a);
    while (dbus_message_iter_get_arg_type(&a) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter e, v;
        dbus_message_iter_recurse(&a, &e);
        const char* key = nullptr;
        dbus_message_iter_get_basic(&e, &key);
        dbus_message_iter_next(&e);
        dbus_message_iter_recurse(&e, &v);
        std::string k = key ? key : "";
        int t = dbus_message_iter_get_arg_type(&v);
        if (t == DBUS_TYPE_STRING) {
            const char* s = nullptr;
            dbus_message_iter_get_basic(&v, &s);
            if (k == "accept_label") r.accept_label = s ? s : "";
            else if (k == "current_name") r.current_name = s ? s : "";
        } else if (t == DBUS_TYPE_BOOLEAN) {
            dbus_bool_t b = 0;
            dbus_message_iter_get_basic(&v, &b);
            if (k == "modal") r.modal = b;
            else if (k == "multiple") r.multiple = b;
            else if (k == "directory") r.directory = b;
        } else if (k == "current_folder") r.current_folder = read_bytes(&v);
        else if (k == "current_file") r.current_file = read_bytes(&v);
        else if (k == "filters" && t == DBUS_TYPE_ARRAY) {
            DBusMessageIter fa;
            dbus_message_iter_recurse(&v, &fa);
            while (dbus_message_iter_get_arg_type(&fa) == DBUS_TYPE_STRUCT) {
                PFilter f;
                if (read_filter(&fa, f)) r.filters.push_back(f);
                dbus_message_iter_next(&fa);
            }
        } else if (k == "current_filter") r.has_current_filter = read_filter(&v, r.current_filter);
        else if (k == "choices") r.has_choices = true;   // extra combo boxes / check boxes: not shown
        else if (k == "files" && t == DBUS_TYPE_ARRAY) {
            DBusMessageIter fa;
            dbus_message_iter_recurse(&v, &fa);
            while (dbus_message_iter_get_arg_type(&fa) == DBUS_TYPE_ARRAY) {
                r.files.push_back(read_bytes(&fa));
                dbus_message_iter_next(&fa);
            }
        }
        dbus_message_iter_next(&a);
    }
}

// ---- replies -----------------------------------------------------------------------

static void append_filter(DBusMessageIter* it, const PFilter& f)
{
    DBusMessageIter st, arr;
    dbus_message_iter_open_container(it, DBUS_TYPE_STRUCT, nullptr, &st);
    const char* n = f.name.c_str();
    dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &n);
    dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "(us)", &arr);
    for (auto& item : f.items) {
        DBusMessageIter s2;
        dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, nullptr, &s2);
        uint32_t k = item.first;
        const char* p = item.second.c_str();
        dbus_message_iter_append_basic(&s2, DBUS_TYPE_UINT32, &k);
        dbus_message_iter_append_basic(&s2, DBUS_TYPE_STRING, &p);
        dbus_message_iter_close_container(&arr, &s2);
    }
    dbus_message_iter_close_container(&st, &arr);
    dbus_message_iter_close_container(it, &st);
}

static void reply(Request& r, uint32_t response, const std::vector<std::string>& uris, int filter)
{
    if (!r.call) return;
    DBusMessage* m = dbus_message_new_method_return(r.call);
    DBusMessageIter it, dict;
    dbus_message_iter_init_append(m, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &response);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &dict);
    if (response == 0) {
        DBusMessageIter e, v, arr;
        const char* key = "uris";
        dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &e);
        dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
        dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "as", &v);
        dbus_message_iter_open_container(&v, DBUS_TYPE_ARRAY, "s", &arr);
        for (const std::string& u : uris) { const char* s = u.c_str(); dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &s); }
        dbus_message_iter_close_container(&v, &arr);
        dbus_message_iter_close_container(&e, &v);
        dbus_message_iter_close_container(&dict, &e);
        if (filter >= 0 && filter < (int)r.filters.size()) {
            key = "current_filter";
            dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &e);
            dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
            dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "(sa(us))", &v);
            append_filter(&v, r.filters[(size_t)filter]);
            dbus_message_iter_close_container(&e, &v);
            dbus_message_iter_close_container(&dict, &e);
        }
        if (r.has_choices) {   // none were shown: an empty answer
            key = "choices";
            dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &e);
            dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
            dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "a(ss)", &v);
            dbus_message_iter_open_container(&v, DBUS_TYPE_ARRAY, "(ss)", &arr);
            dbus_message_iter_close_container(&v, &arr);
            dbus_message_iter_close_container(&e, &v);
            dbus_message_iter_close_container(&dict, &e);
        }
        if (r.method == "OpenFile") {
            key = "writable";
            dbus_bool_t no = FALSE;
            dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &e);
            dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
            dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "b", &v);
            dbus_message_iter_append_basic(&v, DBUS_TYPE_BOOLEAN, &no);
            dbus_message_iter_close_container(&e, &v);
            dbus_message_iter_close_container(&dict, &e);
        }
    }
    dbus_message_iter_close_container(&it, &dict);
    dbus_connection_send(g_conn, m, nullptr);
    dbus_connection_flush(g_conn);
    dbus_message_unref(m);
    dbus_message_unref(r.call);
    r.call = nullptr;
    LOG("%s %s: response %u, %zu uris", r.method.c_str(), r.handle.c_str(), response, uris.size());
}

// ---- the Request object ---------------------------------------------------------------

static DBusHandlerResult request_handler(DBusConnection*, DBusMessage* m, void* data)
{
    Request* r = (Request*)data;
    if (dbus_message_is_method_call(m, REQUEST_IFACE, "Close")) {
        LOG("Close %s", r->handle.c_str());
        r->closed = true;
        if (r->pid > 0) kill(r->pid, SIGTERM);
        DBusMessage* ret = dbus_message_new_method_return(m);
        dbus_connection_send(g_conn, ret, nullptr);
        dbus_message_unref(ret);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Introspectable", "Introspect")) {
        static const char* xml =
            "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
            "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
            "<node><interface name=\"org.freedesktop.impl.portal.Request\"><method name=\"Close\"/></interface></node>\n";
        DBusMessage* ret = dbus_message_new_method_return(m);
        dbus_message_append_args(ret, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
        dbus_connection_send(g_conn, ret, nullptr);
        dbus_message_unref(ret);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

// ---- running the dialog -----------------------------------------------------------------

static std::string exe_dir()
{
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return "";
    buf[n] = 0;
    std::string s = buf;
    return s.substr(0, s.find_last_of('/'));
}

static std::string dialog_program()
{
    if (const char* e = getenv("FLXP_FILEDIALOG")) if (*e) return e;
    std::string here = exe_dir() + "/flxp-filedialog";
    if (access(here.c_str(), X_OK) == 0) return here;
    return "flxp-filedialog";   // $PATH
}

static std::string filter_spec(const PFilter& f)
{
    // flxp-filedialog --filter "Name|*.png image/webp": the name ends at the last '|'
    std::string s = f.name + "|";
    for (auto& item : f.items) {
        std::string p = item.second;
        if (item.first == 0 && p.find('/') != std::string::npos) continue;   // a glob with a slash would read as a MIME type
        s += p + " ";
    }
    return s;
}

static bool same_filter(const PFilter& a, const PFilter& b)
{
    return a.name == b.name && a.items == b.items;
}

static bool start(Request& r)
{
    std::vector<std::string> args = { dialog_program(), "--null", "--print-filter" };
    if (r.method == "SaveFile") args.push_back("--save");
    else if (r.method == "SaveFiles" || r.directory) args.push_back("--directory");
    if (r.method == "OpenFile" && r.multiple && !r.directory) args.push_back("--multiple");
    if (!r.title.empty()) { args.push_back("--title"); args.push_back(r.title); }
    std::string label = r.accept_label;
    if (label.empty() && r.method == "SaveFiles") label = "_Save";
    if (!label.empty()) { args.push_back("--accept-label"); args.push_back(label); }
    // the filter in effect: one of the list, or an extra one
    int current = -1;
    if (r.has_current_filter) {
        for (size_t i = 0; i < r.filters.size(); i++) if (same_filter(r.filters[i], r.current_filter)) current = (int)i;
        if (current < 0) { r.filters.push_back(r.current_filter); current = (int)r.filters.size() - 1; }
    }
    for (const PFilter& f : r.filters) { args.push_back("--filter"); args.push_back(filter_spec(f)); }
    if (current >= 0) { args.push_back("--current-filter"); args.push_back(std::to_string(current)); }
    std::string folder = r.current_folder;
    if (!r.current_file.empty()) {
        args.push_back("--filename"); args.push_back(r.current_file);
        if (folder.empty()) folder = shell::dir_of(r.current_file);
    } else if (!r.current_name.empty()) {
        args.push_back("--filename"); args.push_back(r.current_name);
    }
    if (folder.empty()) folder = shell::home_dir();
    args.push_back("--directory-start"); args.push_back(folder);
    if (!r.parent.empty()) { args.push_back("--parent"); args.push_back(r.parent); }
    if (!r.modal) args.push_back("--no-modal");
    if (g_debug) { std::string l; for (auto& a : args) l += "'" + a + "' "; LOG("run %s", l.c_str()); }

    int p[2];
    if (pipe2(p, O_CLOEXEC) != 0) return false;
    pid_t pid = fork();
    if (pid < 0) { close(p[0]); close(p[1]); return false; }
    if (pid == 0) {
        dup2(p[1], 1);
        int null = open("/dev/null", O_RDONLY);
        if (null >= 0) dup2(null, 0);
        std::vector<char*> av;
        for (auto& a : args) av.push_back((char*)a.c_str());
        av.push_back(nullptr);
        execvp(av[0], av.data());
        fprintf(stderr, "flxp-portal: cannot run %s: %s\n", av[0], strerror(errno));
        _exit(127);
    }
    close(p[1]);
    r.pid = pid;
    r.out_fd = p[0];
    return true;
}

// The child finished: parse "filter=N\0path\0path\0" and answer.
static void finish(Request& r, int status)
{
    std::vector<std::string> recs;
    size_t i = 0;
    while (i < r.output.size()) {
        size_t j = r.output.find('\0', i);
        if (j == std::string::npos) j = r.output.size();
        recs.push_back(r.output.substr(i, j - i));
        i = j + 1;
    }
    int filter = -1;
    std::vector<std::string> uris;
    for (const std::string& s : recs) {
        if (s.rfind("filter=", 0) == 0) { filter = atoi(s.c_str() + 7); continue; }
        if (s.empty()) continue;
        if (r.method == "SaveFiles") {
            for (const std::string& f : r.files) uris.push_back(shell::uri_from_path(s + (s == "/" ? "" : "/") + shell::base_of(f)));
            break;
        }
        uris.push_back(shell::uri_from_path(s));
    }
    uint32_t response = 2;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0 && !uris.empty()) response = 0;
    else if (r.closed || (WIFEXITED(status) && WEXITSTATUS(status) == 1)) response = 1;
    reply(r, response, uris, filter);
}

static DBusHandlerResult portal_handler(DBusConnection*, DBusMessage* m, void*)
{
    g_last_activity = time(nullptr);
    if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Introspectable", "Introspect")) {
        static const char* xml =
            "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
            "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
            "<node>\n"
            " <interface name=\"org.freedesktop.impl.portal.FileChooser\">\n"
            "  <method name=\"OpenFile\"><arg type=\"o\" name=\"handle\" direction=\"in\"/><arg type=\"s\" name=\"app_id\" direction=\"in\"/>"
            "<arg type=\"s\" name=\"parent_window\" direction=\"in\"/><arg type=\"s\" name=\"title\" direction=\"in\"/>"
            "<arg type=\"a{sv}\" name=\"options\" direction=\"in\"/><arg type=\"u\" name=\"response\" direction=\"out\"/>"
            "<arg type=\"a{sv}\" name=\"results\" direction=\"out\"/></method>\n"
            "  <method name=\"SaveFile\"><arg type=\"o\" name=\"handle\" direction=\"in\"/><arg type=\"s\" name=\"app_id\" direction=\"in\"/>"
            "<arg type=\"s\" name=\"parent_window\" direction=\"in\"/><arg type=\"s\" name=\"title\" direction=\"in\"/>"
            "<arg type=\"a{sv}\" name=\"options\" direction=\"in\"/><arg type=\"u\" name=\"response\" direction=\"out\"/>"
            "<arg type=\"a{sv}\" name=\"results\" direction=\"out\"/></method>\n"
            "  <method name=\"SaveFiles\"><arg type=\"o\" name=\"handle\" direction=\"in\"/><arg type=\"s\" name=\"app_id\" direction=\"in\"/>"
            "<arg type=\"s\" name=\"parent_window\" direction=\"in\"/><arg type=\"s\" name=\"title\" direction=\"in\"/>"
            "<arg type=\"a{sv}\" name=\"options\" direction=\"in\"/><arg type=\"u\" name=\"response\" direction=\"out\"/>"
            "<arg type=\"a{sv}\" name=\"results\" direction=\"out\"/></method>\n"
            " </interface>\n"
            " <interface name=\"org.freedesktop.DBus.Introspectable\"><method name=\"Introspect\"><arg type=\"s\" direction=\"out\"/></method></interface>\n"
            "</node>\n";
        DBusMessage* ret = dbus_message_new_method_return(m);
        dbus_message_append_args(ret, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
        dbus_connection_send(g_conn, ret, nullptr);
        dbus_message_unref(ret);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    const char* method = dbus_message_get_member(m);
    if (dbus_message_get_type(m) != DBUS_MESSAGE_TYPE_METHOD_CALL || !dbus_message_has_interface(m, IFACE) || !method ||
        (strcmp(method, "OpenFile") && strcmp(method, "SaveFile") && strcmp(method, "SaveFiles")))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    if (!dbus_message_has_signature(m, "osssa{sv}")) {
        DBusMessage* err = dbus_message_new_error(m, DBUS_ERROR_INVALID_ARGS, "expected (osssa{sv})");
        dbus_connection_send(g_conn, err, nullptr);
        dbus_message_unref(err);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    Request* r = new Request;
    r->method = method;
    DBusMessageIter it;
    dbus_message_iter_init(m, &it);
    const char* s = nullptr;
    dbus_message_iter_get_basic(&it, &s); r->handle = s ? s : ""; dbus_message_iter_next(&it);
    dbus_message_iter_get_basic(&it, &s); r->app_id = s ? s : ""; dbus_message_iter_next(&it);
    dbus_message_iter_get_basic(&it, &s); r->parent = s ? s : ""; dbus_message_iter_next(&it);
    dbus_message_iter_get_basic(&it, &s); r->title = s ? s : ""; dbus_message_iter_next(&it);
    read_options(&it, *r);
    LOG("%s from %s (app '%s', parent '%s', title '%s')", method, dbus_message_get_sender(m), r->app_id.c_str(), r->parent.c_str(), r->title.c_str());
    r->call = dbus_message_ref(m);
    DBusObjectPathVTable vt = {};
    vt.message_function = request_handler;
    if (!r->handle.empty()) dbus_connection_register_object_path(g_conn, r->handle.c_str(), &vt, r);
    if (!start(*r)) {
        reply(*r, 2, {}, -1);
        if (!r->handle.empty()) dbus_connection_unregister_object_path(g_conn, r->handle.c_str());
        delete r;
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    g_requests.push_back(r);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static void dispatch()
{
    while (dbus_connection_dispatch(g_conn) == DBUS_DISPATCH_DATA_REMAINS) {}
    dbus_connection_flush(g_conn);
}

} // namespace

int main(int argc, char** argv)
{
    bool replace = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose")) g_debug = true;
        else if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "--replace")) replace = true;
        else { fprintf(stderr, "usage: flxp-portal [--verbose] [--replace]\n"); return 2; }
    }
    if (getenv("FLXP_PORTAL_DEBUG")) g_debug = true;
    signal(SIGPIPE, SIG_IGN);
    DBusError err;
    dbus_error_init(&err);
    g_conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!g_conn) { fprintf(stderr, "flxp-portal: no session bus: %s\n", err.message); return 1; }
    dbus_connection_set_exit_on_disconnect(g_conn, TRUE);
    DBusObjectPathVTable vt = {};
    vt.message_function = portal_handler;
    if (!dbus_connection_register_object_path(g_conn, OBJ_PATH, &vt, nullptr)) { fprintf(stderr, "flxp-portal: cannot export %s\n", OBJ_PATH); return 1; }
    unsigned flags = DBUS_NAME_FLAG_DO_NOT_QUEUE | DBUS_NAME_FLAG_ALLOW_REPLACEMENT | (replace ? DBUS_NAME_FLAG_REPLACE_EXISTING : 0);
    int rc = dbus_bus_request_name(g_conn, BUS_NAME, flags, &err);
    if (rc != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        fprintf(stderr, "flxp-portal: cannot own %s%s%s\n", BUS_NAME, dbus_error_is_set(&err) ? ": " : " (already running?)", dbus_error_is_set(&err) ? err.message : "");
        return 1;
    }
    LOG("serving %s on %s", IFACE, BUS_NAME);
    int fd = -1;
    dbus_connection_get_unix_fd(g_conn, &fd);
    g_last_activity = time(nullptr);
    for (;;) {
        dispatch();
        std::vector<pollfd> fds;
        fds.push_back({ fd, POLLIN, 0 });
        for (Request* r : g_requests) fds.push_back({ r->out_fd, POLLIN, 0 });
        int timeout = g_requests.empty() ? 10000 : -1;
        int n = poll(fds.data(), fds.size(), timeout);
        if (n < 0 && errno != EINTR) break;
        if (fds[0].revents & (POLLHUP | POLLERR)) break;
        if (fds[0].revents & POLLIN) dbus_connection_read_write(g_conn, 0);
        for (size_t i = 1; i < fds.size(); i++) {
            Request* r = g_requests[i - 1];
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            char buf[4096];
            ssize_t k = read(r->out_fd, buf, sizeof buf);
            if (k > 0) { r->output.append(buf, (size_t)k); continue; }
            if (k < 0 && errno == EINTR) continue;
            // EOF: the dialog is gone
            close(r->out_fd);
            r->out_fd = -1;
        }
        for (size_t i = 0; i < g_requests.size();) {
            Request* r = g_requests[i];
            if (r->out_fd >= 0) { i++; continue; }
            int status = 0;
            waitpid(r->pid, &status, 0);
            finish(*r, status);
            if (!r->handle.empty()) dbus_connection_unregister_object_path(g_conn, r->handle.c_str());
            delete r;
            g_requests.erase(g_requests.begin() + (long)i);
            g_last_activity = time(nullptr);
        }
        if (g_requests.empty() && time(nullptr) - g_last_activity > IDLE_EXIT_S && !getenv("FLXP_PORTAL_PERSIST")) {
            LOG("idle, exiting");
            break;
        }
    }
    return 0;
}
