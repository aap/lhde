// org.freedesktop.FileManager1 (filemanager1.h): ShowFolders, ShowItems, ShowItemProperties, each (as uris, s startup_id),
// on /org/freedesktop/FileManager1.
#include "filemanager1.h"
#include <dbus/dbus.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <string>
#include <vector>

namespace fm1 {
namespace {

const char* BUS_NAME = "org.freedesktop.FileManager1";
const char* OBJ_PATH = "/org/freedesktop/FileManager1";
const char* IFACE = "org.freedesktop.FileManager1";
const int IDLE_EXIT_S = 300;

DBusConnection* g_conn;
std::string g_flxp;
bool g_lost;            // the name went to another owner
time_t g_last;          // the last request (run_service's idle exit)

bool debug() { static bool on = getenv("FLXP_FM1_DEBUG") != nullptr; return on; }
#define LOG(...) do { if (debug()) { fprintf(stderr, "flxp-desktop: FileManager1: " __VA_ARGS__); fputc('\n', stderr); } } while (0)

int hex(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

// a file: URI's path (percent escapes decoded, a host other than localhost refused), a plain absolute path as it is; "" else
std::string path_of(const std::string& uri)
{
    std::string s;
    if (uri.rfind("file://", 0) == 0) {
        size_t slash = uri.find('/', 7);
        if (slash == std::string::npos) return "";
        std::string host = uri.substr(7, slash - 7);
        if (!host.empty() && host != "localhost") return "";
        s = uri.substr(slash);
    } else if (!uri.empty() && uri[0] == '/') return uri;
    else return "";
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && hex(s[i + 1]) >= 0 && hex(s[i + 2]) >= 0) {
            out += (char)(hex(s[i + 1]) * 16 + hex(s[i + 2]));
            i += 2;
        } else out += s[i];
    }
    return out;
}

std::string dir_of(const std::string& p)
{
    std::string s = p;
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    size_t k = s.find_last_of('/');
    return k == std::string::npos ? "." : k == 0 ? "/" : s.substr(0, k);
}

// flxp ARGS... in a process of its own (setsid, in the home directory), not waited for: the grandchild is reparented,
// the child reaped here; DESKTOP_STARTUP_ID the caller's startup id
void spawn(const std::vector<std::string>& args, const std::string& startup_id)
{
    std::vector<char*> av;
    av.push_back((char*)g_flxp.c_str());
    for (const std::string& a : args) av.push_back((char*)a.c_str());
    av.push_back(nullptr);
    LOG("%s%s", g_flxp.c_str(), [&] { std::string s; for (const std::string& a : args) s += " " + a; return s; }().c_str());
    pid_t pid = fork();
    if (pid < 0) { fprintf(stderr, "flxp-desktop: FileManager1: fork: %s\n", strerror(errno)); return; }
    if (pid == 0) {
        if (fork() != 0) _exit(0);
        setsid();
        signal(SIGPIPE, SIG_DFL);
        if (const char* h = getenv("HOME")) if (chdir(h) != 0) {}
        if (!startup_id.empty()) setenv("DESKTOP_STARTUP_ID", startup_id.c_str(), 1);
        else unsetenv("DESKTOP_STARTUP_ID");
        execv(av[0], av.data());
        execvp("flxp", av.data());
        fprintf(stderr, "flxp-desktop: FileManager1: cannot run %s: %s\n", av[0], strerror(errno));
        _exit(127);
    }
    while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
}

void reply_error(DBusMessage* m, const char* name, const char* text)
{
    DBusMessage* e = dbus_message_new_error(m, name, text);
    dbus_connection_send(g_conn, e, nullptr);
    dbus_message_unref(e);
}

const char* INTROSPECT =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
    "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
    "<node>\n"
    " <interface name=\"org.freedesktop.FileManager1\">\n"
    "  <method name=\"ShowFolders\"><arg type=\"as\" name=\"URIs\" direction=\"in\"/><arg type=\"s\" name=\"StartupId\" direction=\"in\"/></method>\n"
    "  <method name=\"ShowItems\"><arg type=\"as\" name=\"URIs\" direction=\"in\"/><arg type=\"s\" name=\"StartupId\" direction=\"in\"/></method>\n"
    "  <method name=\"ShowItemProperties\"><arg type=\"as\" name=\"URIs\" direction=\"in\"/><arg type=\"s\" name=\"StartupId\" direction=\"in\"/></method>\n"
    " </interface>\n"
    " <interface name=\"org.freedesktop.DBus.Introspectable\"><method name=\"Introspect\"><arg type=\"s\" direction=\"out\"/></method></interface>\n"
    "</node>\n";

DBusHandlerResult object_handler(DBusConnection*, DBusMessage* m, void*)
{
    if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Introspectable", "Introspect")) {
        DBusMessage* r = dbus_message_new_method_return(m);
        dbus_message_append_args(r, DBUS_TYPE_STRING, &INTROSPECT, DBUS_TYPE_INVALID);
        dbus_connection_send(g_conn, r, nullptr);
        dbus_message_unref(r);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    const char* method = dbus_message_get_member(m);
    if (dbus_message_get_type(m) != DBUS_MESSAGE_TYPE_METHOD_CALL || !dbus_message_has_interface(m, IFACE) || !method)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    bool folders = !strcmp(method, "ShowFolders"), items = !strcmp(method, "ShowItems"), props = !strcmp(method, "ShowItemProperties");
    if (!folders && !items && !props) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    g_last = time(nullptr);
    if (!dbus_message_has_signature(m, "ass")) { reply_error(m, DBUS_ERROR_INVALID_ARGS, "expected (as URIs, s StartupId)"); return DBUS_HANDLER_RESULT_HANDLED; }
    std::vector<std::string> paths;
    DBusMessageIter it, arr;
    dbus_message_iter_init(m, &it);
    dbus_message_iter_recurse(&it, &arr);
    while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRING) {
        const char* u = nullptr;
        dbus_message_iter_get_basic(&arr, &u);
        std::string p = path_of(u ? u : "");
        if (p.empty()) LOG("%s: %s is not a local file, skipped", method, u ? u : "");
        else paths.push_back(p);
        dbus_message_iter_next(&arr);
    }
    dbus_message_iter_next(&it);
    const char* sid = "";
    dbus_message_iter_get_basic(&it, &sid);
    std::string startup = sid ? sid : "";
    LOG("%s from %s: %zu item(s), startup id '%s'", method, dbus_message_get_sender(m), paths.size(), startup.c_str());
    if (!paths.empty()) {
        if (folders) spawn(paths, startup);
        else if (props) { std::vector<std::string> a = { "--properties", "--" }; a.insert(a.end(), paths.begin(), paths.end()); spawn(a, startup); }
        else {   // one window per folder, its first item selected
            std::vector<std::string> seen;
            for (const std::string& p : paths) {
                std::string d = dir_of(p);
                bool dup = false;
                for (const std::string& s : seen) if (s == d) dup = true;
                if (dup) continue;
                seen.push_back(d);
                spawn({ "--select", p }, startup);
            }
        }
    }
    DBusMessage* r = dbus_message_new_method_return(m);
    dbus_connection_send(g_conn, r, nullptr);
    dbus_message_unref(r);
    return DBUS_HANDLER_RESULT_HANDLED;
}

// NameLost comes to the owner itself, without a match rule
DBusHandlerResult filter(DBusConnection*, DBusMessage* m, void*)
{
    if (dbus_message_is_signal(m, "org.freedesktop.DBus", "NameLost")) {
        const char* n = nullptr;
        if (dbus_message_get_args(m, nullptr, DBUS_TYPE_STRING, &n, DBUS_TYPE_INVALID) && n && !strcmp(n, BUS_NAME)) {
            LOG("the name went to another owner");
            g_lost = true;
        }
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

} // namespace

int start(const std::string& flxp, bool replace)
{
    g_flxp = flxp;
    DBusError err;
    dbus_error_init(&err);
    g_conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
    if (!g_conn) { LOG("no session bus: %s", err.message); dbus_error_free(&err); return -1; }
    dbus_connection_set_exit_on_disconnect(g_conn, FALSE);
    DBusObjectPathVTable vt = {};
    vt.message_function = object_handler;
    dbus_connection_register_object_path(g_conn, OBJ_PATH, &vt, nullptr);
    dbus_connection_add_filter(g_conn, filter, nullptr, nullptr);
    unsigned flags = DBUS_NAME_FLAG_DO_NOT_QUEUE | (replace ? DBUS_NAME_FLAG_REPLACE_EXISTING : DBUS_NAME_FLAG_ALLOW_REPLACEMENT);
    int rc = dbus_bus_request_name(g_conn, BUS_NAME, flags, &err);
    if (rc != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER && rc != DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER) {
        fprintf(stderr, "flxp-desktop: not serving %s: %s\n", BUS_NAME, dbus_error_is_set(&err) ? err.message : "another program owns it");
        dbus_error_free(&err);
        dbus_connection_close(g_conn);
        dbus_connection_unref(g_conn);
        g_conn = nullptr;
        return -1;
    }
    LOG("serving %s", BUS_NAME);
    g_last = time(nullptr);
    int fd = -1;
    dbus_connection_get_unix_fd(g_conn, &fd);
    return fd;
}

void handle()
{
    if (!g_conn) return;
    dbus_connection_read_write(g_conn, 0);
    while (dbus_connection_dispatch(g_conn) == DBUS_DISPATCH_DATA_REMAINS) {}
    dbus_connection_flush(g_conn);
}

int run_service(const std::string& flxp)
{
    signal(SIGPIPE, SIG_IGN);
    int fd = start(flxp, false);
    if (fd < 0) return 1;
    for (;;) {
        handle();
        if (g_lost || !dbus_connection_get_is_connected(g_conn)) break;
        if (time(nullptr) - g_last > IDLE_EXIT_S && !getenv("FLXP_FM1_PERSIST")) { LOG("idle, exiting"); break; }
        pollfd p = { fd, POLLIN, 0 };
        int n = poll(&p, 1, 10000);
        if (n < 0 && errno != EINTR) break;
        if (p.revents & (POLLHUP | POLLERR)) break;
    }
    return 0;
}

} // namespace fm1
