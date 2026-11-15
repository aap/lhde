/* liblhde: the hardware layer (lhde/hw.h): the helpers the back ends share, the choice of back end, the touchpad (xinput on
   every system), the change-event descriptors, describe and fields. The systems' parts are hw_linux.c and hw_freebsd.c,
   both built everywhere so that LHDE_HW_SYSTEM and LHDE_HW_FAKE run either over recorded files. */
#include "hw_internal.h"
#include "lhde/settings.h"
#include "liblhde/compat.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

/* ---- helpers ---------------------------------------------------------------------------------------------------------- */

void hw_strlcpy(char *dst, const char *src, size_t n)
{
    if (!n) return;
    size_t k = src ? strlen(src) : 0;
    if (k >= n) k = n - 1;
    if (k) memcpy(dst, src, k);
    dst[k] = 0;
}

const char *hw_fake(void)
{
    const char *f = getenv("LHDE_HW_FAKE");
    return f && *f ? f : NULL;
}

const char *hw_path(const char *path, char *out, size_t n)
{
    const char *f = hw_fake();
    if (f) snprintf(out, n, "%s%s", f, path);
    else hw_strlcpy(out, path, n);
    return out;
}

const char *hw_read_line(const char *path, char *buf, size_t n)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    if (!fgets(buf, (int)n, f)) buf[0] = 0;
    fclose(f);
    size_t k = strlen(buf);
    while (k && isspace((unsigned char)buf[k - 1])) buf[--k] = 0;
    return buf;
}

int hw_read_long(const char *path, long *v)
{
    char b[64], *e;
    if (!hw_read_line(path, b, sizeof b) || !b[0]) return 0;
    long x = strtol(b, &e, 10);
    if (e == b) return 0;
    *v = x;
    return 1;
}

int hw_write_text(const char *path, const char *text)
{
    int fd = open(path, O_WRONLY | O_TRUNC | O_CLOEXEC);
    if (fd < 0) return -1;
    size_t k = strlen(text);
    ssize_t w = write(fd, text, k);
    int e = errno;
    if (close(fd) < 0 && w == (ssize_t)k) return -1;
    errno = e;
    return w == (ssize_t)k ? 0 : -1;
}

int hw_have(const char *prog)
{
    const char *p = getenv("PATH");
    if (!p) p = "/usr/bin:/bin";
    while (*p) {
        const char *c = strchr(p, ':');
        size_t k = c ? (size_t)(c - p) : strlen(p);
        if (k) {
            char path[PATH_MAX];
            snprintf(path, sizeof path, "%.*s/%s", (int)k, p, prog);
            if (access(path, X_OK) == 0) return 1;
        }
        if (!c) break;
        p = c + 1;
    }
    return 0;
}

static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* posix_spawnp argv with stdin /dev/null and stdout (and stderr when both) on fd, in its own process group when group */
static int spawn(const char *const argv[], int fd, int with_stderr, int group, pid_t *pid)
{
    posix_spawn_file_actions_t fa;
    posix_spawnattr_t at;
    posix_spawn_file_actions_init(&fa);
    posix_spawnattr_init(&at);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&fa, fd, 1);
    if (with_stderr) posix_spawn_file_actions_adddup2(&fa, fd, 2);
    sigset_t def;
    sigemptyset(&def);
    sigaddset(&def, SIGPIPE);
    sigaddset(&def, SIGCHLD);
    sigaddset(&def, SIGTERM);
    short flags = POSIX_SPAWN_SETSIGDEF;
    posix_spawnattr_setsigdefault(&at, &def);
    if (group) { flags |= POSIX_SPAWN_SETPGROUP; posix_spawnattr_setpgroup(&at, 0); }
    posix_spawnattr_setflags(&at, flags);
    int r = posix_spawnp(pid, argv[0], &fa, &at, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    return r == 0 ? 0 : -1;
}

int hw_run(const char *const argv[], char *out, size_t n)
{
    if (n) out[0] = 0;
    int fds[2];
    if (pipe(fds) < 0) return -1;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    pid_t pid;
    if (spawn(argv, fds[1], 1, 0, &pid) < 0) { close(fds[0]); close(fds[1]); return -1; }
    close(fds[1]);
    size_t len = 0;
    char sink[512];
    long long end = now_ms() + 2000;
    int killed = 0;
    for (;;) {
        long long left = end - now_ms();
        if (left <= 0) { kill(pid, SIGKILL); killed = 1; break; }
        struct pollfd p = { fds[0], POLLIN, 0 };
        int r = poll(&p, 1, (int)left);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) continue;
        char *dst = len + 1 < n ? out + len : sink;
        size_t room = len + 1 < n ? n - 1 - len : sizeof sink;
        ssize_t k = read(fds[0], dst, room);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) break;
        if (dst != sink) { len += (size_t)k; out[len] = 0; }
    }
    close(fds[0]);
    int st = 0;
    pid_t w;
    while ((w = waitpid(pid, &st, 0)) < 0 && errno == EINTR) {}
    if (killed) return -1;
    if (w < 0) return 0;                              /* SIGCHLD ignored by the caller: reaped already */
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    return -1;
}

const char *hw_first_line(const char *s, char *buf, size_t n)
{
    size_t k = strcspn(s, "\n");
    if (k >= n) k = n - 1;
    memcpy(buf, s, k);
    buf[k] = 0;
    return buf;
}

static char last_error[256];
void hw_fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(last_error, sizeof last_error, fmt, ap);
    va_end(ap);
}
const char *lhde_hw_error(void) { return last_error; }

int hw_spawn_reader(const char *const argv[], int *pidp)
{
    int fds[2];
    if (pipe(fds) < 0) return -1;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    pid_t parent = getpid(), pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return -1; }
    if (pid == 0) {
        setpgid(0, 0);
        lhde_parent_death_signal(SIGTERM);
        if (getppid() != parent) _exit(1);
        signal(SIGCHLD, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        int nul = open("/dev/null", O_RDWR);
        if (nul >= 0) { dup2(nul, 0); dup2(nul, 2); close(nul); }
        dup2(fds[1], 1);
        close(fds[0]); close(fds[1]);
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    setpgid(pid, pid);
    close(fds[1]);
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    *pidp = pid;
    return fds[0];
}

void hw_kill_reader(int *fd, int *pid)
{
    if (*pid > 0) { kill(-*pid, SIGTERM); waitpid(*pid, NULL, 0); }
    if (*fd >= 0) close(*fd);
    *fd = -1;
    *pid = 0;
}

static const char *muted_file(char *out, size_t n)
{
    char d[PATH_MAX];
    snprintf(out, n, "%s/hw-volume-muted", lhde_state_dir(d, sizeof d));
    return out;
}

int hw_muted_level(const char *device)
{
    char p[PATH_MAX], line[200];
    FILE *f = fopen(muted_file(p, sizeof p), "r");
    if (!f) return -1;
    int level = -1;
    while (fgets(line, sizeof line, f)) {
        char dev[160];
        int l;
        if (sscanf(line, "%159s %d", dev, &l) == 2 && !strcmp(dev, device)) level = l;
    }
    fclose(f);
    return level;
}

void hw_set_muted_level(const char *device, int level)
{
    char p[PATH_MAX], tmp[PATH_MAX + 8], line[200], d[PATH_MAX];
    muted_file(p, sizeof p);
    lhde_state_dir(d, sizeof d);
    mkdir(d, 0755);                                   /* its parent ~/.local/state exists on any session that ran lhde */
    snprintf(tmp, sizeof tmp, "%s.%d", p, (int)getpid());
    FILE *in = fopen(p, "r"), *out = fopen(tmp, "w");
    if (!out) { if (in) fclose(in); return; }
    while (in && fgets(line, sizeof line, in)) {
        char dev[160];
        if (sscanf(line, "%159s", dev) == 1 && !strcmp(dev, device)) continue;
        fputs(line, out);
    }
    if (in) fclose(in);
    if (level >= 0) fprintf(out, "%s %d\n", device, level);
    if (fclose(out) == 0) rename(tmp, p);
    else unlink(tmp);
}

/* ---- the system ------------------------------------------------------------------------------------------------------- */

static int is_freebsd(void)
{
    const char *s = getenv("LHDE_HW_SYSTEM");
    if (s && *s) return !strcmp(s, "freebsd");
#if defined(__FreeBSD__)
    return 1;
#else
    return 0;
#endif
}

static void clear(void *p, size_t n) { memset(p, 0, n); }

int lhde_hw_battery(lhde_battery *b)
{
    clear(b, sizeof *b);
    b->percent = -1; b->ac = -1; b->minutes = -1;
    return is_freebsd() ? hw_freebsd_battery(b) : hw_linux_battery(b);
}
int lhde_hw_backlight(lhde_backlight *b)
{
    clear(b, sizeof *b);
    return is_freebsd() ? hw_freebsd_backlight(b) : hw_linux_backlight(b);
}
int lhde_hw_backlight_set(int percent)
{
    last_error[0] = 0;
    if (percent < 1) percent = 1;
    if (percent > 100) percent = 100;
    return is_freebsd() ? hw_freebsd_backlight_set(percent) : hw_linux_backlight_set(percent);
}
int lhde_hw_volume(lhde_volume *v)
{
    clear(v, sizeof *v);
    return is_freebsd() ? hw_freebsd_volume(v) : hw_linux_volume(v);
}
int lhde_hw_volume_set(int percent)
{
    last_error[0] = 0;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    return is_freebsd() ? hw_freebsd_volume_set(percent) : hw_linux_volume_set(percent);
}
int lhde_hw_volume_mute(int on)
{
    last_error[0] = 0;
    return is_freebsd() ? hw_freebsd_volume_mute(on) : hw_linux_volume_mute(on);
}

/* ---- the touchpad: xinput ----------------------------------------------------------------------------------------------- */

static int ci_contains(const char *s, const char *needle)
{
    size_t k = strlen(needle);
    for (; *s; s++)
        if (!strncasecmp(s, needle, k)) return 1;
    return 0;
}

/* only white space between p and the start of its line */
static int line_start(const char *s, const char *p)
{
    while (p > s && p[-1] != '\n') { p--; if (!isspace((unsigned char)*p)) return 0; }
    return 1;
}

int lhde_hw_touchpad(lhde_touchpad *t)
{
    clear(t, sizeof *t);
    if (!hw_have("xinput")) { hw_copy(t->error, "no xinput"); return 0; }
    static char out[16384];
    const char *list[] = { "xinput", "list", NULL };
    if (hw_run(list, out, sizeof out) != 0) { hw_copy(t->error, "xinput list failed"); return 0; }
    /* "⎜   ↳ SynPS/2 Synaptics TouchPad              	id=12	[slave  pointer  (2)]"; turned off ("Device Enabled" 0),
     * the X server detaches it from its master: "∼ SynPS/2 Synaptics TouchPad              	id=10	[floating slave]" */
    for (char *l = out; l && *l;) {
        char *nl = strchr(l, '\n');
        if (nl) *nl = 0;
        char *id = strstr(l, "id="), *sl = strstr(l, "[slave"), *fl = strstr(l, "[floating slave");
        if (id && ((sl && strstr(sl, "pointer")) || fl) && (ci_contains(l, "touchpad") || ci_contains(l, "trackpad"))) {
            t->id = atoi(id + 3);
            char *s = l;
            while (*s && !isalnum((unsigned char)*s)) s++;   /* the tree's box-drawing prefix */
            char *e = id;
            while (e > s && isspace((unsigned char)e[-1])) e--;
            snprintf(t->name, sizeof t->name, "%.*s", (int)(e - s), s);
            break;
        }
        l = nl ? nl + 1 : NULL;
    }
    if (!t->id) return 0;
    char ids[16];
    snprintf(ids, sizeof ids, "%d", t->id);
    const char *props[] = { "xinput", "list-props", ids, NULL };
    if (hw_run(props, out, sizeof out) != 0) { t->id = 0; t->name[0] = 0; return 0; }   /* unplugged meanwhile */
    for (int k = 0; k < 2; k++) {
        const char *prop = k ? "Device Enabled (" : "libinput Device Enabled (";
        char *p = strstr(out, prop);
        while (k && p && !line_start(out, p)) p = strstr(p + 1, prop);   /* not "libinput Device Enabled" */
        if (!p) continue;
        char *c = strchr(p, ':');
        if (!c) continue;
        t->enabled = atoi(c + 1) != 0;
        snprintf(t->prop, sizeof t->prop, "%.*s", (int)strlen(prop) - 2, prop);
        t->present = 1;
        return 1;
    }
    t->id = 0;
    t->name[0] = 0;
    return 0;
}

int lhde_hw_touchpad_set(int enabled)
{
    last_error[0] = 0;
    lhde_touchpad t;
    if (!lhde_hw_touchpad(&t)) { hw_fail("no touchpad%s%s", t.error[0] ? ": " : "", t.error); return -1; }
    char ids[16], out[512];
    snprintf(ids, sizeof ids, "%d", t.id);
    const char *argv[] = { "xinput", "set-prop", ids, t.prop, enabled ? "1" : "0", NULL };
    if (hw_run(argv, out, sizeof out) != 0) { char l[200]; hw_fail("xinput: %s", hw_first_line(out, l, sizeof l)); return -1; }
    return 0;
}

/* ---- change events ---------------------------------------------------------------------------------------------------- */

static int vol_fd = -1, vol_pid, vol_tried, bat_fd = -1, bat_tried;

int lhde_hw_volume_fd(void)
{
    if (vol_fd >= 0 || vol_tried) return vol_fd;
    vol_tried = 1;
    if (hw_fake() || is_freebsd()) return -1;
    vol_fd = hw_linux_volume_fd(&vol_pid);
    return vol_fd;
}

int lhde_hw_volume_poll(void)
{
    if (vol_fd < 0) return -1;
    char buf[4096];
    int changed = 0;
    static char part[256];
    static size_t plen;
    for (;;) {
        ssize_t k = read(vol_fd, buf, sizeof buf);
        if (k < 0 && errno == EINTR) continue;
        if (k < 0 && errno == EAGAIN) return changed;
        if (k <= 0) { hw_kill_reader(&vol_fd, &vol_pid); return changed ? 1 : -1; }
        /* "Event 'change' on sink #45", "Event 'change' on server #-1": lines, possibly split across reads */
        for (ssize_t i = 0; i < k; i++) {
            if (buf[i] != '\n') { if (plen < sizeof part - 1) part[plen++] = buf[i]; continue; }
            part[plen] = 0;
            if (strstr(part, " sink ") || strstr(part, "server")) changed = 1;
            plen = 0;
        }
    }
}

int lhde_hw_battery_fd(void)
{
    if (bat_fd >= 0 || bat_tried) return bat_fd;
    bat_tried = 1;
    if (hw_fake()) return -1;
    bat_fd = is_freebsd() ? hw_freebsd_battery_fd() : hw_linux_battery_fd();
    return bat_fd;
}

int lhde_hw_battery_poll(void)
{
    if (bat_fd < 0) return -1;
    int changed = 0;
    for (;;) {
        int r = is_freebsd() ? hw_freebsd_battery_event(bat_fd) : hw_linux_battery_event(bat_fd);
        if (r == 2) return changed;                   /* nothing more to read */
        if (r < 0) { close(bat_fd); bat_fd = -1; return changed ? 1 : -1; }
        if (r == 1) changed = 1;
    }
}

void lhde_hw_close(void)
{
    hw_kill_reader(&vol_fd, &vol_pid);
    if (bat_fd >= 0) close(bat_fd);
    bat_fd = -1;
    vol_tried = bat_tried = 0;
}

/* ---- describe, fields ------------------------------------------------------------------------------------------------- */

const char *lhde_hw_state_name(int state)
{
    switch (state) {
    case LHDE_BATTERY_CHARGING: return "charging";
    case LHDE_BATTERY_DISCHARGING: return "discharging";
    case LHDE_BATTERY_FULL: return "full";
    case LHDE_BATTERY_IDLE: return "idle";
    default: return "unknown";
    }
}

static void add(char *buf, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void add(char *buf, size_t n, const char *fmt, ...)
{
    size_t k = strlen(buf);
    if (k + 1 >= n) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + k, n - k, fmt, ap);
    va_end(ap);
}

const char *lhde_hw_describe(int what, char *buf, size_t n)
{
    if (!n) return buf;
    buf[0] = 0;
    const char *sys = is_freebsd() ? "freebsd" : "linux";
    const char *fake = hw_fake();
    if (what & LHDE_HW_BATTERY) {
        lhde_battery b;
        lhde_hw_battery(&b);
        const char *ev = fake ? "read every 30 s (fake)" : is_freebsd() ? "devd's socket, else read every 30 s"
                                                                          : "the kernel's uevents, else read every 30 s";
        if (b.count) add(buf, n, "battery: %s %s; changes: %s\n", sys, b.source, ev);
        else add(buf, n, "battery: %s, none found%s%s\n", sys, b.source[0] ? " in " : "", b.source);
    }
    if (what & LHDE_HW_BACKLIGHT) {
        lhde_backlight b;
        if (lhde_hw_backlight(&b))
            add(buf, n, "backlight: %s %s (raw %d of %d); set: %s%s%s; changes: read every 1 s\n", sys, b.device, b.raw, b.max,
                b.method[0] ? b.method : "none", b.error[0] ? ": " : "", b.error);
        else add(buf, n, "backlight: %s, none found%s%s\n", sys, b.error[0] ? ": " : "", b.error);
    }
    if (what & LHDE_HW_VOLUME) {
        lhde_volume v;
        if (lhde_hw_volume(&v)) {
            const char *ev = !is_freebsd() && !fake && strcmp(v.backend, "amixer") && hw_have("pactl") ? "pactl subscribe"
                                                                                                       : "read every 1 s";
            add(buf, n, "volume: %s %s %s; changes: %s\n", sys, v.backend, v.device, ev);
        } else add(buf, n, "volume: %s, none: %s\n", sys, v.error);
    }
    if (what & LHDE_HW_TOUCHPAD) {
        lhde_touchpad t;
        if (lhde_hw_touchpad(&t)) add(buf, n, "touchpad: xinput (the X server's device) id %d \"%s\", %s\n", t.id, t.name, t.prop);
        else add(buf, n, "touchpad: xinput (the X server's device), none%s%s\n", t.error[0] ? ": " : "", t.error);
    }
    return buf;
}

void lhde_hw_fields(int what, const void *rec, lhde_hw_field_fn fn, void *ud)
{
#define S(k, v) fn(k, 's', v, 0, ud)
#define N(k, v) fn(k, 'n', NULL, (double)(v), ud)
#define B(k, v) fn(k, 'b', NULL, (v) ? 1 : 0, ud)
    if (what == LHDE_HW_BATTERY) {
        const lhde_battery *b = rec;
        N("count", b->count); N("percent", b->percent); S("state", lhde_hw_state_name(b->state));
        if (b->ac >= 0) B("ac", b->ac);
        N("minutes", b->minutes); N("now", b->now); N("full", b->full); N("rate", b->rate);
        S("unit", b->unit); S("model", b->model); S("technology", b->technology); S("source", b->source);
    } else if (what == LHDE_HW_BACKLIGHT) {
        const lhde_backlight *b = rec;
        B("present", b->present); N("percent", b->percent); N("raw", b->raw); N("max", b->max);
        S("device", b->device); S("method", b->method); S("error", b->error);
    } else if (what == LHDE_HW_VOLUME) {
        const lhde_volume *v = rec;
        B("present", v->present); N("percent", v->percent); B("muted", v->muted);
        S("backend", v->backend); S("device", v->device); S("error", v->error);
    } else if (what == LHDE_HW_TOUCHPAD) {
        const lhde_touchpad *t = rec;
        B("present", t->present); B("enabled", t->enabled); N("id", t->id);
        S("name", t->name); S("prop", t->prop); S("error", t->error);
    }
#undef S
#undef N
#undef B
}
