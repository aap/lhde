/* liblhde's hardware layer, Linux (lhde/hw.h): the battery and the AC adapter from /sys/class/power_supply, the backlight
   from /sys/class/backlight, the volume through wpctl, pactl or amixer, battery events from the kernel's uevent socket.
   Built on every system: under LHDE_HW_FAKE the sysfs paths are a recorded tree's. */
#include "hw_internal.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#if defined(__linux__)
#include <linux/netlink.h>
#endif

/* the entries of a directory, sorted; returns how many (at most max) */
static int entries(const char *dir, char names[][64], int max)
{
    DIR *d = opendir(dir);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < max) {
        if (e->d_name[0] == '.') continue;
        hw_strlcpy(names[n++], e->d_name, 64);
    }
    closedir(d);
    qsort(names, (size_t)n, 64, (int (*)(const void *, const void *))strcmp);
    return n;
}

static int sub_long(const char *dir, const char *name, long *v)
{
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    return hw_read_long(p, v);
}
static const char *sub_line(const char *dir, const char *name, char *buf, size_t n)
{
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    return hw_read_line(p, buf, n);
}

/* ---- battery ---------------------------------------------------------------------------------------------------------- */

int hw_linux_battery(lhde_battery *b)
{
    char root[PATH_MAX], names[32][64];
    hw_path("/sys/class/power_supply", root, sizeof root);
    int n = entries(root, names, 32);
    int mains = 0, online = 0, charging = 0, discharging = 0, full = 0, notcharging = 0;
    int caps = 0, capsum = 0, energy = 0, charge = 0;
    double now = 0, fullv = 0, rate = 0;
    int have_all = 1;
    snprintf(b->source, sizeof b->source, "%s:", root);
    for (int i = 0; i < n; i++) {
        char dir[PATH_MAX], type[32], scope[32], status[32];
        snprintf(dir, sizeof dir, "%s/%s", root, names[i]);
        if (!sub_line(dir, "type", type, sizeof type)) continue;
        if (!strcmp(type, "Mains") || !strncmp(type, "USB", 3)) {
            long o;
            if (sub_long(dir, "online", &o)) { mains = 1; if (o) online = 1; }
            continue;
        }
        if (strcmp(type, "Battery")) continue;
        if (sub_line(dir, "scope", scope, sizeof scope) && !strcmp(scope, "Device")) continue;   /* a mouse's, a pen's */
        long present = 1;
        sub_long(dir, "present", &present);
        if (!present) continue;
        b->count++;
        if (strlen(b->source) + strlen(names[i]) + 3 < sizeof b->source)
            snprintf(b->source + strlen(b->source), sizeof b->source - strlen(b->source), " %s", names[i]);
        if (!sub_line(dir, "status", status, sizeof status)) status[0] = 0;
        if (!strcmp(status, "Charging")) charging++;
        else if (!strcmp(status, "Discharging")) discharging++;
        else if (!strcmp(status, "Full")) full++;
        else if (!strcmp(status, "Not charging")) notcharging++;
        long cap, nw, fl, rt = 0;
        if (sub_long(dir, "capacity", &cap)) { caps++; capsum += (int)cap; }
        if (sub_long(dir, "energy_now", &nw) && sub_long(dir, "energy_full", &fl)) {      /* µWh, µW */
            energy = 1;
            sub_long(dir, "power_now", &rt);
        } else if (sub_long(dir, "charge_now", &nw) && sub_long(dir, "charge_full", &fl)) {   /* µAh, µA */
            charge = 1;
            sub_long(dir, "current_now", &rt);
        } else { have_all = 0; continue; }
        now += nw / 1e6; fullv += fl / 1e6; rate += labs(rt) / 1e6;
        if (!b->model[0]) sub_line(dir, "model_name", b->model, sizeof b->model);
        if (!b->technology[0]) sub_line(dir, "technology", b->technology, sizeof b->technology);
    }
    if (!b->count) return 0;
    if (energy && charge) have_all = 0;                   /* Wh and Ah do not add up */
    if (have_all && fullv > 0) {
        b->now = now; b->full = fullv; b->rate = rate;
        hw_copy(b->unit, energy ? "Wh" : "Ah");
        b->percent = (int)floor(now * 100 / fullv + 0.5);
    } else if (caps) b->percent = (capsum + caps / 2) / caps;
    if (b->percent > 100) b->percent = 100;
    b->state = charging ? LHDE_BATTERY_CHARGING : discharging ? LHDE_BATTERY_DISCHARGING
             : full == b->count ? LHDE_BATTERY_FULL : (full || notcharging) ? LHDE_BATTERY_IDLE : LHDE_BATTERY_UNKNOWN;
    b->ac = mains ? online : b->state == LHDE_BATTERY_DISCHARGING ? 0 : b->state == LHDE_BATTERY_CHARGING ? 1 : -1;
    if (b->rate > 0 && b->full > 0) {
        if (b->state == LHDE_BATTERY_DISCHARGING) b->minutes = (int)floor(b->now / b->rate * 60 + 0.5);
        else if (b->state == LHDE_BATTERY_CHARGING) b->minutes = (int)floor((b->full - b->now) / b->rate * 60 + 0.5);
    }
    return 1;
}

int hw_linux_battery_fd(void)
{
#if defined(__linux__)
    int fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_KOBJECT_UEVENT);
    if (fd < 0) return -1;
    struct sockaddr_nl a;
    memset(&a, 0, sizeof a);
    a.nl_family = AF_NETLINK;
    a.nl_groups = 1;                                      /* the kernel's broadcast */
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) { close(fd); return -1; }
    return fd;
#else
    return -1;
#endif
}

int hw_linux_battery_event(int fd)
{
    char buf[8192];
    ssize_t k = recv(fd, buf, sizeof buf - 1, 0);
    if (k < 0 && errno == ENOBUFS) return 1;             /* messages were lost: one of them may have been ours */
    if (k < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 2 : -1;
    if (k == 0) return 2;
    buf[k] = 0;
    /* "change@/devices/.../power_supply/BAT0\0ACTION=change\0...\0SUBSYSTEM=power_supply\0..." */
    for (ssize_t i = 0; i < k; i += (ssize_t)strlen(buf + i) + 1)
        if (!strcmp(buf + i, "SUBSYSTEM=power_supply")) return 1;
    return 0;
}

/* ---- backlight -------------------------------------------------------------------------------------------------------- */

/* the device the kernel ranks first: type firmware, else platform, else raw (then by name) */
static int backlight_device(char *dir, size_t n, char *name, size_t nn)
{
    char root[PATH_MAX], names[16][64];
    hw_path("/sys/class/backlight", root, sizeof root);
    int k = entries(root, names, 16), best = -1, rank = 9;
    for (int i = 0; i < k; i++) {
        char d[PATH_MAX], type[32];
        long max;
        snprintf(d, sizeof d, "%s/%s", root, names[i]);
        if (!sub_long(d, "max_brightness", &max) || max <= 0) continue;
        if (!sub_line(d, "type", type, sizeof type)) type[0] = 0;
        int r = !strcmp(type, "firmware") ? 0 : !strcmp(type, "platform") ? 1 : !strcmp(type, "raw") ? 2 : 3;
        if (r < rank) { rank = r; best = i; }
    }
    if (best < 0) return 0;
    snprintf(dir, n, "%s/%s", root, names[best]);
    hw_strlcpy(name, names[best], nn);
    return 1;
}

int hw_linux_backlight(lhde_backlight *b)
{
    char dir[PATH_MAX], p[PATH_MAX + 16];
    long cur, max;
    if (!backlight_device(dir, sizeof dir, b->device, sizeof b->device)) { b->device[0] = 0; return 0; }
    if (!sub_long(dir, "brightness", &cur) || !sub_long(dir, "max_brightness", &max) || max <= 0) return 0;
    b->present = 1;
    b->raw = (int)cur; b->max = (int)max;
    b->percent = (int)((cur * 100 + max / 2) / max);
    snprintf(p, sizeof p, "%s/brightness", dir);
    if (access(p, W_OK) == 0) hw_copy(b->method, "sysfs");
    else if (hw_have("brightnessctl")) hw_copy(b->method, "brightnessctl");
    else snprintf(b->error, sizeof b->error, "not writable (no brightnessctl, and %s is not ours to write)", p);
    return 1;
}

int hw_linux_backlight_set(int percent)
{
    char dir[PATH_MAX], name[64], p[PATH_MAX + 16], v[32];
    long max;
    if (!backlight_device(dir, sizeof dir, name, sizeof name) || !sub_long(dir, "max_brightness", &max) || max <= 0) {
        hw_fail("no backlight");
        return -1;
    }
    long raw = (percent * max + 50) / 100;
    if (raw < 1) raw = 1;
    snprintf(p, sizeof p, "%s/brightness", dir);
    snprintf(v, sizeof v, "%ld\n", raw);
    if (access(p, W_OK) == 0) {
        if (hw_write_text(p, v) == 0) return 0;
        hw_fail("%s: %s", p, strerror(errno));
        return -1;
    }
    if (hw_have("brightnessctl")) {
        char out[512], line[200];
        v[strlen(v) - 1] = 0;
        const char *argv[] = { "brightnessctl", "-q", "-d", name, "set", v, NULL };
        if (hw_run(argv, out, sizeof out) == 0) return 0;
        hw_fail("brightnessctl: %s", hw_first_line(out, line, sizeof line));
        return -1;
    }
    hw_fail("not writable (no brightnessctl, and %s is not ours to write)", p);
    return -1;
}

/* ---- volume ----------------------------------------------------------------------------------------------------------- */

enum { WPCTL, PACTL, AMIXER, NBACK };
static const char *const back_name[NBACK] = { "wpctl", "pactl", "amixer" };
static const char *const back_dev[NBACK] = { "@DEFAULT_AUDIO_SINK@", "@DEFAULT_SINK@", "Master" };
static int cur_back = -1;                                 /* the one that answered last */

static int forced(void)
{
    const char *f = getenv("LHDE_HW_VOLUME");
    if (!f || !*f) return -1;
    for (int i = 0; i < NBACK; i++)
        if (!strcmp(f, back_name[i])) return i;
    return -2;                                            /* another system's */
}

/* one back end's read: 1 when it answered, else 0 with its first line in err */
static int read_one(int k, lhde_volume *v, char *err, size_t en)
{
    static char out[8192];
    int st;
    if (k == WPCTL) {
        /* "Volume: 0.40 [MUTED]" */
        const char *argv[] = { "wpctl", "get-volume", back_dev[k], NULL };
        st = hw_run(argv, out, sizeof out);
        char *p = strstr(out, "Volume:");
        if (st != 0 || !p) goto fail;
        v->percent = (int)floor(atof(p + 7) * 100 + 0.5);
        v->muted = strstr(out, "[MUTED]") != NULL;
    } else if (k == PACTL) {
        /* "Volume: front-left: 36044 /  55% / -15.58 dB, ..." and "Mute: no" */
        const char *a1[] = { "pactl", "get-sink-volume", back_dev[k], NULL }, *a2[] = { "pactl", "get-sink-mute", back_dev[k], NULL };
        st = hw_run(a1, out, sizeof out);
        char *pc = strchr(out, '%');
        if (st != 0 || !strstr(out, "Volume:") || !pc) goto fail;
        char *s = pc;
        while (s > out && s[-1] >= '0' && s[-1] <= '9') s--;
        v->percent = atoi(s);
        st = hw_run(a2, out, sizeof out);
        char *m = strstr(out, "Mute:");
        if (st != 0 || !m) goto fail;
        v->muted = strstr(m, "yes") != NULL;
    } else {
        /* "Mono: Playback 26 [40%] [-12.00dB] [on]" */
        const char *argv[] = { "amixer", "get", "Master", NULL };
        st = hw_run(argv, out, sizeof out);
        char *p = out;
        while ((p = strchr(p, '[')) && !(p[1] >= '0' && p[1] <= '9')) p++;
        if (st != 0 || !p) goto fail;
        v->percent = atoi(p + 1);
        v->muted = strstr(out, "[off]") != NULL;
    }
    hw_copy(v->backend, back_name[k]);
    hw_copy(v->device, back_dev[k]);
    v->present = 1;
    return 1;
fail:
    hw_first_line(out[0] ? out : "no answer", err, en);
    return 0;
}

int hw_linux_volume(lhde_volume *v)
{
    int f = forced(), order[NBACK + 1], n = 0;
    if (f == -2) { snprintf(v->error, sizeof v->error, "%s is not a volume back end here", getenv("LHDE_HW_VOLUME")); return 0; }
    if (f >= 0) order[n++] = f;
    else {
        if (cur_back >= 0) order[n++] = cur_back;
        for (int i = 0; i < NBACK; i++) if (i != cur_back) order[n++] = i;
    }
    int tried = 0;
    char err[200] = "";
    for (int i = 0; i < n; i++) {
        if (!hw_have(back_name[order[i]])) continue;
        tried = 1;
        if (read_one(order[i], v, err, sizeof err)) { cur_back = order[i]; return 1; }
        snprintf(v->error, sizeof v->error, "%s: %s", back_name[order[i]], err);
    }
    if (!tried) {
        if (f >= 0) snprintf(v->error, sizeof v->error, "%s not found", back_name[f]);
        else hw_copy(v->error, "no wpctl, pactl or amixer found");
    }
    cur_back = -1;
    return 0;
}

static int back_for_set(void)
{
    if (cur_back < 0) { lhde_volume v; if (!hw_linux_volume(&v)) { hw_fail("%s", v.error); return -1; } }
    return cur_back;
}

static int run_set(const char *const argv[])
{
    char out[1024], line[200];
    if (hw_run(argv, out, sizeof out) == 0) return 0;
    hw_fail("%s: %s", argv[0], hw_first_line(out[0] ? out : "failed", line, sizeof line));
    return -1;
}

int hw_linux_volume_set(int percent)
{
    int k = back_for_set();
    char v[32];
    if (k == WPCTL) {
        snprintf(v, sizeof v, "%.2f", percent / 100.0);
        const char *argv[] = { "wpctl", "set-volume", back_dev[k], v, NULL };
        return run_set(argv);
    }
    if (k == PACTL) {
        snprintf(v, sizeof v, "%d%%", percent);
        const char *argv[] = { "pactl", "set-sink-volume", back_dev[k], v, NULL };
        return run_set(argv);
    }
    if (k == AMIXER) {
        snprintf(v, sizeof v, "%d%%", percent);
        const char *argv[] = { "amixer", "-q", "set", "Master", v, NULL };
        return run_set(argv);
    }
    return -1;
}

int hw_linux_volume_mute(int on)
{
    int k = back_for_set();
    if (k == WPCTL || k == PACTL) {
        const char *argv[] = { back_name[k], k == WPCTL ? "set-mute" : "set-sink-mute", back_dev[k],
                               on < 0 ? "toggle" : on ? "1" : "0", NULL };
        return run_set(argv);
    }
    if (k == AMIXER) {
        const char *argv[] = { "amixer", "-q", "set", "Master", on < 0 ? "toggle" : on ? "mute" : "unmute", NULL };
        return run_set(argv);
    }
    return -1;
}

int hw_linux_volume_fd(int *pid)
{
    int f = forced();
    if (f == AMIXER || f == -2 || !hw_have("pactl")) return -1;
    const char *argv[] = { "pactl", "subscribe", NULL };
    return hw_spawn_reader(argv, pid);
}
