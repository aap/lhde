#include <stdio.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include "shell/xclip.h"
#include "shell/config.h"

namespace shell {
namespace xclip {

namespace {

struct Atoms {
    Atom clipboard, primary, targets, utf8, text_plain, uri_list, gnome, kde_cut, timestamp, prop;
    void init(Display* d)
    {
        clipboard = XInternAtom(d, "CLIPBOARD", False);
        primary = XA_PRIMARY;
        targets = XInternAtom(d, "TARGETS", False);
        utf8 = XInternAtom(d, "UTF8_STRING", False);
        text_plain = XInternAtom(d, "text/plain;charset=utf-8", False);
        uri_list = XInternAtom(d, "text/uri-list", False);
        gnome = XInternAtom(d, "x-special/gnome-copied-files", False);
        kde_cut = XInternAtom(d, "application/x-kde-cutselection", False);
        timestamp = XInternAtom(d, "TIMESTAMP", False);
        prop = XInternAtom(d, "FLXP_CLIPBOARD", False);
    }
};

// the owner thread's state: what we offer on CLIPBOARD (files, or text) and
// on PRIMARY (text: the selection in an edit control)
std::mutex g_lock;
std::vector<std::string> g_paths;      // CLIPBOARD's files
bool g_cut;
bool g_clip_text;                      // CLIPBOARD holds g_text[0] instead of files
bool g_clip_data;                      // CLIPBOARD holds g_data instead of files or text
Data g_data;                           // target name, bytes
std::string g_text[2];                 // text offered: [0] CLIPBOARD, [1] PRIMARY
unsigned g_req, g_done;                // set_files' requests, and the last one the thread carried out
unsigned g_preq, g_pdone;              // the same for PRIMARY
std::atomic<bool> g_lost{false}, g_owned{false}, g_powned{false};
int g_pipe[2] = { -1, -1 };
bool g_started, g_failed;
Time g_owned_since, g_powned_since;

std::string payload(Atom target, const Atoms& a, const std::vector<std::string>& paths, bool cut)
{
    std::string out;
    if (target == a.gnome) {
        out = cut ? "cut" : "copy";
        for (const std::string& p : paths) out += "\n" + uri_from_path(p);
    } else if (target == a.uri_list) {
        for (const std::string& p : paths) out += uri_from_path(p) + "\r\n";
    } else if (target == a.kde_cut) {
        out = cut ? "1" : "0";
    } else {
        for (size_t i = 0; i < paths.size(); i++) out += (i ? "\n" : "") + paths[i];
    }
    return out;
}

void answer(Display* d, const Atoms& a, const XSelectionRequestEvent& rq)
{
    XSelectionEvent ev = {};
    ev.type = SelectionNotify;
    ev.requestor = rq.requestor;
    ev.selection = rq.selection;
    ev.target = rq.target;
    ev.time = rq.time;
    ev.property = rq.property ? rq.property : rq.target;   // obsolete clients
    std::vector<std::string> paths;
    bool cut, text_mode, data_mode;
    std::string text;
    Data data;
    bool prim = rq.selection == a.primary;
    { std::lock_guard<std::mutex> l(g_lock); paths = g_paths; cut = g_cut; text_mode = prim || g_clip_text; text = g_text[prim ? 1 : 0];
      data_mode = !prim && g_clip_data; if (data_mode) data = g_data; }
    if (data_mode && rq.selection == a.clipboard) {   // typed data: the targets by name
        if (rq.target == a.targets) {
            std::vector<Atom> t = { a.targets, a.timestamp };
            for (const auto& e : data) t.push_back(XInternAtom(d, e.first.c_str(), False));
            XChangeProperty(d, rq.requestor, ev.property, XA_ATOM, 32, PropModeReplace, (unsigned char*)t.data(), (int)t.size());
        } else if (rq.target == a.timestamp) {
            long t = (long)g_owned_since;
            XChangeProperty(d, rq.requestor, ev.property, XA_INTEGER, 32, PropModeReplace, (unsigned char*)&t, 1);
        } else {
            const std::string* bytes = nullptr;
            for (const auto& e : data) if (XInternAtom(d, e.first.c_str(), True) == rq.target) bytes = &e.second;
            long max = XExtendedMaxRequestSize(d) ? XExtendedMaxRequestSize(d) : XMaxRequestSize(d);
            if (bytes && (long)bytes->size() > max * 4 - 1024) {
                fprintf(stderr, "flxp: the clipboard's %zu bytes exceed the server's request size\n", bytes->size());
                bytes = nullptr;
            }
            if (bytes) XChangeProperty(d, rq.requestor, ev.property, rq.target, 8, PropModeReplace, (const unsigned char*)bytes->data(), (int)bytes->size());
            else ev.property = None;
        }
    }
    else if (text_mode && (rq.selection == a.clipboard || prim)) {   // text: UTF-8, plain and Latin-1 STRING (as is)
        if (rq.target == a.targets) {
            Atom t[] = { a.targets, a.timestamp, a.utf8, a.text_plain, XA_STRING };
            XChangeProperty(d, rq.requestor, ev.property, XA_ATOM, 32, PropModeReplace, (unsigned char*)t, (int)(sizeof t / sizeof t[0]));
        } else if (rq.target == a.timestamp) {
            long t = (long)(prim ? g_powned_since : g_owned_since);
            XChangeProperty(d, rq.requestor, ev.property, XA_INTEGER, 32, PropModeReplace, (unsigned char*)&t, 1);
        } else if (rq.target == a.utf8 || rq.target == a.text_plain || rq.target == XA_STRING) {
            XChangeProperty(d, rq.requestor, ev.property, rq.target, 8, PropModeReplace, (const unsigned char*)text.data(), (int)text.size());
        } else ev.property = None;
    }
    else if (rq.selection != a.clipboard || paths.empty()) ev.property = None;
    else if (rq.target == a.targets) {
        Atom t[] = { a.targets, a.timestamp, a.gnome, a.uri_list, a.kde_cut, a.utf8, a.text_plain, XA_STRING };
        XChangeProperty(d, rq.requestor, ev.property, XA_ATOM, 32, PropModeReplace, (unsigned char*)t, (int)(sizeof t / sizeof t[0]));
    } else if (rq.target == a.timestamp) {
        long t = (long)g_owned_since;
        XChangeProperty(d, rq.requestor, ev.property, XA_INTEGER, 32, PropModeReplace, (unsigned char*)&t, 1);
    } else if (rq.target == a.gnome || rq.target == a.uri_list || rq.target == a.kde_cut || rq.target == a.utf8 ||
               rq.target == a.text_plain || rq.target == XA_STRING) {
        std::string s = payload(rq.target, a, paths, cut);
        XChangeProperty(d, rq.requestor, ev.property, rq.target, 8, PropModeReplace, (const unsigned char*)s.data(), (int)s.size());
    } else {
        ev.property = None;
    }
    XSendEvent(d, rq.requestor, False, 0, (XEvent*)&ev);
    XFlush(d);
}

// a server timestamp: a zero-length append to our own window's property
Time server_time(Display* d, Window w, Atom prop)
{
    XChangeProperty(d, w, prop, XA_STRING, 8, PropModeAppend, nullptr, 0);
    XEvent ev;
    for (;;) {
        XWindowEvent(d, w, PropertyChangeMask, &ev);
        if (ev.type == PropertyNotify && ev.xproperty.atom == prop) return ev.xproperty.time;
    }
}

void owner_thread(Display* d)
{
    Atoms a; a.init(d);
    Window w = XCreateSimpleWindow(d, DefaultRootWindow(d), -10, -10, 1, 1, 0, 0, 0);
    XSelectInput(d, w, PropertyChangeMask);
    XFlush(d);
    int xfd = ConnectionNumber(d);
    for (;;) {
        while (XPending(d)) {
            XEvent ev;
            XNextEvent(d, &ev);
            if (ev.type == SelectionRequest) answer(d, a, ev.xselectionrequest);
            else if (ev.type == SelectionClear && ev.xselectionclear.selection == a.clipboard) {
                g_owned = false;
                g_lost = true;
            } else if (ev.type == SelectionClear && ev.xselectionclear.selection == a.primary)
                g_powned = false;
        }
        struct pollfd p[2] = { { xfd, POLLIN, 0 }, { g_pipe[0], POLLIN, 0 } };
        if (poll(p, 2, -1) < 0) continue;
        if (p[1].revents & POLLIN) {
            char c[16];
            (void)!read(g_pipe[0], c, sizeof c);
            bool empty, pempty;
            unsigned req, done, preq, pdone;
            { std::lock_guard<std::mutex> l(g_lock); req = g_req; done = g_done;
              empty = g_clip_data ? g_data.empty() : g_clip_text ? g_text[0].empty() : g_paths.empty();
              preq = g_preq; pdone = g_pdone; pempty = g_text[1].empty(); }
            if (preq != pdone) {
                Time t = server_time(d, w, a.prop);
                XSetSelectionOwner(d, a.primary, pempty ? None : w, t);
                g_powned_since = t;
                bool owned = !pempty && XGetSelectionOwner(d, a.primary) == w;
                XFlush(d);
                std::lock_guard<std::mutex> l(g_lock);
                g_powned = owned;
                g_pdone = preq;
            }
            if (req != done) {
                Time t = server_time(d, w, a.prop);
                XSetSelectionOwner(d, a.clipboard, empty ? None : w, t);
                g_owned_since = t;
                bool owned = !empty && XGetSelectionOwner(d, a.clipboard) == w;
                XFlush(d);
                // g_done moves together with g_owned, so ours() never sees the
                // gap; a set_files meanwhile bumped g_req and the pipe: taken again
                std::lock_guard<std::mutex> l(g_lock);
                g_owned = owned;
                g_done = req;
            }
        }
    }
}

bool start()
{
    if (g_started) return !g_failed;
    g_started = true;
    Display* d = XOpenDisplay(nullptr);
    if (!d || pipe(g_pipe) != 0) { g_failed = true; if (d) XCloseDisplay(d); return false; }
    std::thread(owner_thread, d).detach();
    return true;
}

} // namespace

bool set_files(const std::vector<std::string>& paths, bool cut)
{
    if (!start()) return false;
    { std::lock_guard<std::mutex> l(g_lock); g_paths = paths; g_cut = cut; g_clip_text = false; g_clip_data = false; g_data.clear(); g_req++; }
    g_lost = false;
    (void)!write(g_pipe[1], "o", 1);
    return true;
}

bool lost() { return g_lost; }

bool ours()
{
    if (g_failed) return !g_clip_text;
    if (!g_started) return false;
    std::lock_guard<std::mutex> l(g_lock);
    if (g_clip_text || g_clip_data) return false;   // our text or data, no files
    return g_req != g_done ? !g_paths.empty() : (bool)g_owned;
}

bool set_text(const std::string& text, bool primary)
{
    if (!start()) {   // no X: process-local
        std::lock_guard<std::mutex> l(g_lock);
        g_text[primary ? 1 : 0] = text;
        if (!primary) g_clip_text = true;
        return false;
    }
    {
        std::lock_guard<std::mutex> l(g_lock);
        g_text[primary ? 1 : 0] = text;
        if (primary) g_preq++;
        else { g_clip_text = true; g_clip_data = false; g_data.clear(); g_paths.clear(); g_req++; }
    }
    if (!primary) g_lost = true;   // cut marks of an earlier set_files are stale
    (void)!write(g_pipe[1], "o", 1);
    return true;
}

bool set_data(const Data& data)
{
    if (!start()) return false;
    {
        std::lock_guard<std::mutex> l(g_lock);
        g_data = data; g_clip_data = true; g_clip_text = false; g_paths.clear(); g_req++;
    }
    g_lost = true;   // cut marks of an earlier set_files are stale
    (void)!write(g_pipe[1], "o", 1);
    return true;
}

bool owned()
{
    if (!g_started || g_failed) return false;
    std::lock_guard<std::mutex> l(g_lock);
    return g_req != g_done || g_owned;
}

bool other_owner()
{
    if (ours()) return false;
    static Display* d = XOpenDisplay(nullptr);
    if (!d) return false;
    static Atom clip = XInternAtom(d, "CLIPBOARD", False);
    return XGetSelectionOwner(d, clip) != None;
}

static bool convert(Display* d, Window w, const Atoms& a, Atom target, std::string& out, int timeout_ms)
{
    XDeleteProperty(d, w, a.prop);
    XConvertSelection(d, a.clipboard, target, a.prop, w, CurrentTime);
    XFlush(d);
    int waited = 0;
    XEvent ev;
    for (;;) {
        if (XCheckTypedWindowEvent(d, w, SelectionNotify, &ev)) break;
        if (waited >= timeout_ms) return false;
        struct pollfd p = { ConnectionNumber(d), POLLIN, 0 };
        poll(&p, 1, 20);
        waited += 20;
    }
    if (ev.xselection.property == None) return false;
    Atom type; int format; unsigned long n, after; unsigned char* data = nullptr;
    if (XGetWindowProperty(d, w, a.prop, 0, 1 << 24, True, AnyPropertyType, &type, &format, &n, &after, &data) != Success) return false;
    if (type == XInternAtom(d, "INCR", False)) { if (data) XFree(data); return false; }   // files lists are small
    out.assign((const char*)data, data ? n * (size_t)(format / 8) : 0);
    if (data) XFree(data);
    return true;
}

bool get_text(std::string& out, bool primary, int timeout_ms)
{
    out.clear();
    if (g_started) {   // ours: no round trip
        std::lock_guard<std::mutex> l(g_lock);
        if (primary && (g_preq != g_pdone || g_powned)) { out = g_text[1]; return !out.empty(); }
        if (!primary && (g_req != g_done || g_owned || g_failed)) {
            if (g_clip_text) out = g_text[0];
            else if (g_clip_data) { for (const auto& e : g_data) if (e.first == "UTF8_STRING") out = e.second; }
            else for (size_t i = 0; i < g_paths.size(); i++) out += (i ? "\n" : "") + g_paths[i];   // files as their paths
            return !out.empty();
        }
    }
    if (g_failed) return false;
    Display* d = XOpenDisplay(nullptr);
    if (!d) return false;
    Atoms a; a.init(d);
    Atom sel = primary ? a.primary : a.clipboard;
    if (XGetSelectionOwner(d, sel) == None) { XCloseDisplay(d); return false; }
    Window w = XCreateSimpleWindow(d, DefaultRootWindow(d), -10, -10, 1, 1, 0, 0, 0);
    Atom save = a.clipboard;
    a.clipboard = sel;   // convert() asks for a.clipboard
    bool ok = convert(d, w, a, a.utf8, out, timeout_ms) || convert(d, w, a, XA_STRING, out, timeout_ms / 2);
    a.clipboard = save;
    XDestroyWindow(d, w);
    XCloseDisplay(d);
    return ok && !out.empty();
}

bool get_files(std::vector<std::string>& paths, bool& cut, int timeout_ms)
{
    paths.clear(); cut = false;
    if (ours()) {
        std::lock_guard<std::mutex> l(g_lock);
        paths = g_paths; cut = g_cut;
        return !paths.empty();
    }
    Display* d = XOpenDisplay(nullptr);
    if (!d) return false;
    Atoms a; a.init(d);
    Window w = XCreateSimpleWindow(d, DefaultRootWindow(d), -10, -10, 1, 1, 0, 0, 0);
    std::string s;
    bool gnome = convert(d, w, a, a.gnome, s, timeout_ms);
    if (!gnome && !convert(d, w, a, a.uri_list, s, timeout_ms)) { XDestroyWindow(d, w); XCloseDisplay(d); return false; }
    if (!gnome) {
        std::string k;
        if (convert(d, w, a, a.kde_cut, k, timeout_ms / 2)) cut = k == "1";
    }
    XDestroyWindow(d, w);
    XCloseDisplay(d);
    size_t at = 0;
    bool first = true;
    while (at <= s.size()) {
        size_t nl = s.find('\n', at);
        std::string line = s.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        at = nl == std::string::npos ? s.size() + 1 : nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (gnome && first) { first = false; cut = line == "cut"; continue; }
        first = false;
        if (line.empty() || line[0] == '#') continue;
        std::string p = path_from_uri(line);
        if (!p.empty()) paths.push_back(p);
    }
    return !paths.empty();
}

}
}
