// Wine's debug channel hooks (wine/debug.h) and the bits of wine/exception.h
// that __TRY/__EXCEPT need to link. WINEDEBUG is honoured with Wine's syntax:
// comma separated items [class][+|-]channel, where class is one of err,
// warn, fixme, trace (none = all classes) and channel "all" means every
// channel. Later items win. As an extension, an item without a class whose
// channel is a class name ("-fixme", "+trace") applies that class to all
// channels. Default: err and fixme on.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "w32/w32.h"
extern "C" {
#include "wine/debug.h"
#include "wine/exception.h"
}
#include "w32/kernel.h"

namespace {

const unsigned char ALL_CLASSES = (1 << __WINE_DBCL_FIXME) | (1 << __WINE_DBCL_ERR) |
                                  (1 << __WINE_DBCL_WARN) | (1 << __WINE_DBCL_TRACE);
const unsigned char DEFAULT_FLAGS = (1 << __WINE_DBCL_FIXME) | (1 << __WINE_DBCL_ERR);

struct Option { std::string channel; unsigned char set, clear; };   // channel "" = all
std::vector<Option> options;
bool options_parsed;

const char* const class_names[] = { "fixme", "err", "warn", "trace" };

int class_of(const std::string& s)
{
    for (int i = 0; i < 4; i++) if (s == class_names[i]) return i;
    return -1;
}

void parse(const char* opts)
{
    options.clear();
    options_parsed = true;
    if (!opts) return;
    std::string all = opts;
    size_t i = 0;
    while (i <= all.size()) {
        size_t e = all.find(',', i);
        if (e == std::string::npos) e = all.size();
        std::string item = all.substr(i, e - i);
        i = e + 1;
        if (item.empty()) continue;
        size_t op = item.find_first_of("+-");
        std::string cls, chan;
        bool on = true;
        if (op == std::string::npos) chan = item;
        else { cls = item.substr(0, op); on = item[op] == '+'; chan = item.substr(op + 1); }
        unsigned char mask;
        if (cls.empty()) {
            int c = class_of(chan);
            if (c >= 0 && op != std::string::npos) { mask = 1 << c; chan = "all"; }
            else mask = ALL_CLASSES;
        } else {
            int c = class_of(cls);
            if (c < 0) { fprintf(stderr, "flxp: unknown WINEDEBUG class '%s'\n", cls.c_str()); continue; }
            mask = 1 << c;
        }
        if (chan == "all") chan.clear();
        options.push_back(Option{chan, (unsigned char)(on ? mask : 0), (unsigned char)(on ? 0 : mask)});
    }
}

unsigned char flags_for(const char* name)
{
    if (!options_parsed) parse(getenv("WINEDEBUG"));
    unsigned char f = DEFAULT_FLAGS;
    for (const Option& o : options) {
        if (!o.channel.empty() && strncmp(o.channel.c_str(), name, 15)) continue;
        f = (unsigned char)((f | o.set) & ~o.clear);
    }
    return f;
}

bool at_line_start = true;

// Ring buffer for __wine_dbg_strdup: strings stay valid for a while.
const int RING = 64;
char* ring[RING];
int ring_pos;

} // namespace

namespace w32 {

void debug_set_options(const char* opts) { parse(opts); }

} // namespace w32

extern "C" {

unsigned char __cdecl __wine_dbg_get_channel_flags(struct __wine_debug_channel* ch)
{
    if (ch->flags & (1 << __WINE_DBCL_INIT)) {
        char name[16];
        memcpy(name, ch->name, 15);
        name[15] = 0;
        ch->flags = flags_for(name);
    }
    return ch->flags;
}

const char* __cdecl __wine_dbg_strdup(const char* s)
{
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (!p) return "";
    memcpy(p, s, n);
    free(ring[ring_pos]);
    ring[ring_pos] = p;
    ring_pos = (ring_pos + 1) % RING;
    return p;
}

int __cdecl __wine_dbg_output(const char* s)
{
    size_t n = strlen(s);
    fwrite(s, 1, n, stderr);
    if (n) at_line_start = s[n - 1] == '\n';
    return (int)n;
}

int __cdecl __wine_dbg_header(enum __wine_debug_class cls, struct __wine_debug_channel* ch, const char* func)
{
    if (!(__wine_dbg_get_channel_flags(ch) & (1 << cls))) return -1;
    if (!at_line_start) return 0;      // continuation of a partial line
    char name[16];
    memcpy(name, ch->name, 15);
    name[15] = 0;
    const char* cn = (unsigned)cls < 4 ? class_names[cls] : "dbg";
    char buf[200];
    int n;
    if (func) n = snprintf(buf, sizeof buf, "%s:%s:%s ", cn, name, func);
    else n = snprintf(buf, sizeof buf, "%s:%s: ", cn, name);
    fputs(buf, stderr);
    at_line_start = false;
    return n;
}

// __TRY: the guarded block runs after __wine_setjmpex returns 0; the
// exception path (non-zero return) is never taken since nothing raises.
int __cdecl __wine_setjmpex(__wine_jmp_buf*, EXCEPTION_REGISTRATION_RECORD*) { return 0; }

void __cdecl __wine_longjmp(__wine_jmp_buf*, int)
{
    fputs("flxp: __wine_longjmp called\n", stderr);
    abort();
}

static DWORD unreachable_handler(const char* what)
{
    fprintf(stderr, "flxp: %s called\n", what);
    abort();
}

DWORD __cdecl __wine_exception_handler_page_fault(EXCEPTION_RECORD*, EXCEPTION_REGISTRATION_RECORD*, CONTEXT*,
                                                  EXCEPTION_REGISTRATION_RECORD**)
{
    return unreachable_handler("__wine_exception_handler_page_fault");
}

DWORD __cdecl __wine_exception_handler_all(EXCEPTION_RECORD*, EXCEPTION_REGISTRATION_RECORD*, CONTEXT*,
                                           EXCEPTION_REGISTRATION_RECORD**)
{
    return unreachable_handler("__wine_exception_handler_all");
}

DWORD __cdecl __wine_exception_handler(EXCEPTION_RECORD*, EXCEPTION_REGISTRATION_RECORD*, CONTEXT*,
                                       EXCEPTION_REGISTRATION_RECORD**)
{
    return unreachable_handler("__wine_exception_handler");
}

} // extern "C"
