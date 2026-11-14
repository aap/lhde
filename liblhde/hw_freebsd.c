/* liblhde's hardware layer, FreeBSD (lhde/hw.h): the battery from the hw.acpi.battery sysctls and hw.acpi.acline, the
   backlight through /dev/backlight's ioctls (backlight(8) when they fail), the volume through the OSS mixer of
   hw.snd.default_unit (mixer(8) when its ioctls fail; the command back ends, pactl and the rest, when there is no mixer
   or LHDE_HW_VOLUME names one), battery and AC events from devd's socket.
   Built on every system: under LHDE_HW_FAKE a sysctl is the file DIR/sysctl/NAME, a backlight device a file of
   "brightness N" lines and a mixer a file of "vol L:R", "pcm L:R", "mute MASK" lines (no mute line: the device has no
   SOUND_MIXER_MUTE), so the parsing runs on Linux too. */
#include "hw_internal.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#if defined(__FreeBSD__)
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <sys/backlight.h>
#include <sys/soundcard.h>
#endif

static int sysctl_long(const char *name, long *v)
{
    const char *f = hw_fake();
    if (f) {
        char p[PATH_MAX];
        snprintf(p, sizeof p, "%s/sysctl/%s", f, name);
        return hw_read_long(p, v);
    }
#if defined(__FreeBSD__)
    int i;
    size_t len = sizeof i;
    if (sysctlbyname(name, &i, &len, NULL, 0) < 0 || len != sizeof i) return 0;
    *v = i;
    return 1;
#else
    return 0;
#endif
}

/* ---- battery ---------------------------------------------------------------------------------------------------------- */

/* hw.acpi.battery.state: ACPI_BATT_STAT_* bits */
enum { BATT_DISCHARG = 1, BATT_CHARGING = 2, BATT_CRITICAL = 4, BATT_NOT_PRESENT = 7 };

int hw_freebsd_battery(lhde_battery *b)
{
    long units = 0, life = -1, time = -1, state = 0, rate = -1, acline = -1;
    if (!sysctl_long("hw.acpi.battery.units", &units)) {
        hw_copy(b->source, "sysctl hw.acpi.battery (absent: no acpi_battery)");
        if (sysctl_long("hw.acpi.acline", &acline)) b->ac = acline != 0;
        return 0;
    }
    sysctl_long("hw.acpi.battery.life", &life);
    sysctl_long("hw.acpi.battery.time", &time);
    sysctl_long("hw.acpi.battery.state", &state);
    sysctl_long("hw.acpi.battery.rate", &rate);
    if (sysctl_long("hw.acpi.acline", &acline)) b->ac = acline != 0;
    snprintf(b->source, sizeof b->source, "sysctl hw.acpi.battery (%ld unit%s)", units, units == 1 ? "" : "s");
    if (units <= 0 || state == BATT_NOT_PRESENT) return 0;
    b->count = (int)units;
    b->percent = life >= 0 && life <= 100 ? (int)life : -1;
    if (state & BATT_CHARGING) b->state = LHDE_BATTERY_CHARGING;
    else if (state & BATT_DISCHARG) b->state = LHDE_BATTERY_DISCHARGING;
    else if (b->ac == 1) b->state = b->percent >= 100 ? LHDE_BATTERY_FULL : LHDE_BATTERY_IDLE;
    if (b->ac < 0) b->ac = b->state == LHDE_BATTERY_DISCHARGING ? 0 : b->state == LHDE_BATTERY_CHARGING ? 1 : -1;
    if (b->state == LHDE_BATTERY_DISCHARGING && time >= 0) b->minutes = (int)time;   /* the kernel's estimate, minutes */
    if (rate > 0) { b->rate = rate / 1000.0; hw_copy(b->unit, "Wh"); }                 /* mW */
    return 1;
}

int hw_freebsd_battery_fd(void)
{
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) return -1;
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    hw_copy(a.sun_path, "/var/run/devd.seqpacket.pipe");
    if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0) { close(fd); return -1; }
    fcntl(fd, F_SETFL, O_NONBLOCK);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    return fd;
}

int hw_freebsd_battery_event(int fd)
{
    char buf[2048];
    ssize_t k = recv(fd, buf, sizeof buf - 1, 0);
    if (k < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 2 : -1;
    if (k == 0) return -1;                                /* devd went away */
    buf[k] = 0;
    /* "!system=ACPI subsystem=ACAD type=\_SB_.AC__ notify=0x00", "!system=ACPI subsystem=CMBAT ... notify=0x80" */
    return strstr(buf, "system=ACPI") && (strstr(buf, "subsystem=ACAD") || strstr(buf, "subsystem=CMBAT")) ? 1 : 0;
}

/* ---- backlight -------------------------------------------------------------------------------------------------------- */

/* the first device in /dev/backlight that is not a symlink (intel_backlight0 links to backlight0) */
static int backlight_device(char *path, size_t n, char *name, size_t nn)
{
    char root[PATH_MAX], best[64] = "";
    hw_path("/dev/backlight", root, sizeof root);
    DIR *d = opendir(root);
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        char p[PATH_MAX + 64];
        struct stat st;
        if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "%s/%s", root, e->d_name);
        if (lstat(p, &st) < 0 || S_ISLNK(st.st_mode) || S_ISDIR(st.st_mode)) continue;
        if (!best[0] || strcmp(e->d_name, best) < 0) hw_copy(best, e->d_name);
    }
    closedir(d);
    if (!best[0]) return 0;
    snprintf(path, n, "%s/%s", root, best);
    hw_strlcpy(name, best, nn);
    return 1;
}

/* who may write it: "root:video 0660" */
static void owner_text(const char *path, char *out, size_t n)
{
    struct stat st;
    if (stat(path, &st) < 0) { snprintf(out, n, "%s", strerror(errno)); return; }
    struct group *g = getgrgid(st.st_gid);
    snprintf(out, n, "group %s, mode %04o", g ? g->gr_name : "?", (unsigned)(st.st_mode & 07777));
}

/* the level, 0..100, by the ioctl (or the fake file); -1 with errno */
static int bl_get(const char *path)
{
    if (hw_fake()) {
        FILE *f = fopen(path, "r");
        if (!f) return -1;
        char line[128];
        int v = -1;
        while (fgets(line, sizeof line, f)) sscanf(line, "brightness %d", &v);
        fclose(f);
        if (v < 0) errno = EINVAL;
        return v;
    }
#if defined(__FreeBSD__)
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    struct backlight_props p;
    memset(&p, 0, sizeof p);
    int r = ioctl(fd, BACKLIGHTGETSTATUS, &p);
    int e = errno;
    close(fd);
    errno = e;
    return r < 0 ? -1 : (int)p.brightness;
#else
    errno = ENOTSUP;
    return -1;
#endif
}

static int bl_set(const char *path, int level)
{
    if (hw_fake()) {
        char t[32];
        snprintf(t, sizeof t, "brightness %d\n", level);
        return hw_write_text(path, t);
    }
#if defined(__FreeBSD__)
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) return -1;
    struct backlight_props p;
    memset(&p, 0, sizeof p);
    int r = ioctl(fd, BACKLIGHTGETSTATUS, &p);
    if (r == 0) { p.brightness = (uint32_t)level; r = ioctl(fd, BACKLIGHTUPDATESTATUS, &p); }
    int e = errno;
    close(fd);
    errno = e;
    return r;
#else
    errno = ENOTSUP;
    return -1;
#endif
}

int hw_freebsd_backlight(lhde_backlight *b)
{
    char path[PATH_MAX], who[96];
    if (!backlight_device(path, sizeof path, b->device, sizeof b->device)) return 0;
    int level = bl_get(path);
    int e = errno;
    const char *how = "ioctl";
    if (level < 0 && hw_have("backlight")) {
        /* `backlight -q -f DEV`: "39" */
        char out[256];
        const char *argv[] = { "backlight", "-q", "-f", path, NULL };
        if (hw_run(argv, out, sizeof out) == 0 && out[0] >= '0' && out[0] <= '9') { level = atoi(out); how = "backlight(8)"; }
    }
    if (level < 0) {
        owner_text(path, who, sizeof who);
        snprintf(b->error, sizeof b->error, "%s: %s (%s)", path, strerror(e), who);
        return 0;
    }
    b->present = 1;
    b->raw = b->percent = level;
    b->max = 100;
    if (access(path, W_OK) == 0) hw_copy(b->method, how);
    else {
        owner_text(path, who, sizeof who);
        snprintf(b->error, sizeof b->error, "not writable (%s is %s: join its group)", path, who);
    }
    return 1;
}

int hw_freebsd_backlight_set(int percent)
{
    char path[PATH_MAX], name[64], who[96];
    if (!backlight_device(path, sizeof path, name, sizeof name)) { hw_fail("no backlight"); return -1; }
    /* a driver may keep a raw level that reads back one lower (i915 on a T460: 39 -> 38, 40 -> 39): the next one up
       until the level reads back as asked */
    int r = -1;
    for (int ask = percent; ask <= percent + 2 && ask <= 100; ask++) {
        if ((r = bl_set(path, ask)) < 0) break;
        if (bl_get(path) >= percent) break;
    }
    if (r == 0) return 0;
    int e = errno;
    if (e != EACCES && e != EPERM && hw_have("backlight")) {
        char v[16], out[512], line[200];
        snprintf(v, sizeof v, "%d", percent);
        const char *argv[] = { "backlight", "-f", path, v, NULL };
        if (hw_run(argv, out, sizeof out) == 0) return 0;
        hw_fail("backlight: %s", hw_first_line(out, line, sizeof line));
        return -1;
    }
    owner_text(path, who, sizeof who);
    hw_fail("%s: %s (%s)", path, strerror(e), who);
    return -1;
}

/* ---- volume ----------------------------------------------------------------------------------------------------------- */

enum { MIX_VOL = 0, MIX_PCM = 4, MIX_NDEV = 25 };
struct mixer {
    int devmask;
    int level[MIX_NDEV];                                  /* left | right << 8, 0..100 each */
    int has_mute, mute;                                   /* SOUND_MIXER_MUTE: a mask of the muted devices */
};

/* the default unit's mixer device: /dev/mixerN by hw.snd.default_unit, else /dev/mixer, else /dev/mixer0 */
static const char *mixer_path(char *out, size_t n)
{
    long unit = -1;
    char p[64], q[PATH_MAX];
    if (sysctl_long("hw.snd.default_unit", &unit) && unit >= 0) {
        snprintf(p, sizeof p, "/dev/mixer%ld", unit);
        if (access(hw_path(p, q, sizeof q), F_OK) == 0) return hw_path(p, out, n);
    }
    if (access(hw_path("/dev/mixer", q, sizeof q), F_OK) != 0 && access(hw_path("/dev/mixer0", q, sizeof q), F_OK) == 0)
        return hw_path("/dev/mixer0", out, n);
    return hw_path("/dev/mixer", out, n);
}

static int mixer_load(const char *path, struct mixer *m)
{
    memset(m, 0, sizeof *m);
    if (hw_fake()) {
        FILE *f = fopen(path, "r");
        if (!f) return -1;
        char line[128], name[16];
        int l, r;
        while (fgets(line, sizeof line, f)) {
            if (sscanf(line, "mute %d", &l) == 1) { m->has_mute = 1; m->mute = l; continue; }
            if (sscanf(line, "%15s %d:%d", name, &l, &r) == 3) {
                int d = !strcmp(name, "vol") ? MIX_VOL : !strcmp(name, "pcm") ? MIX_PCM : -1;
                if (d >= 0) { m->devmask |= 1 << d; m->level[d] = l | r << 8; }
            }
        }
        fclose(f);
        return 0;
    }
#if defined(__FreeBSD__)
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    if (ioctl(fd, SOUND_MIXER_READ_DEVMASK, &m->devmask) < 0) { int e = errno; close(fd); errno = e; return -1; }
    for (int d = 0; d < MIX_NDEV; d++)
        if (m->devmask & (1 << d)) ioctl(fd, MIXER_READ(d), &m->level[d]);
    m->has_mute = ioctl(fd, SOUND_MIXER_READ_MUTE, &m->mute) == 0;
    close(fd);
    return 0;
#else
    errno = ENOTSUP;
    return -1;
#endif
}

/* writes the level of dev and the mute mask */
static int mixer_store(const char *path, const struct mixer *m, int dev)
{
    if (hw_fake()) {
        char t[256] = "";
        for (int d = 0; d < MIX_NDEV; d++)
            if (m->devmask & (1 << d))
                snprintf(t + strlen(t), sizeof t - strlen(t), "%s %d:%d\n", d == MIX_VOL ? "vol" : "pcm", m->level[d] & 0x7f, (m->level[d] >> 8) & 0x7f);
        if (m->has_mute) snprintf(t + strlen(t), sizeof t - strlen(t), "mute %d\n", m->mute);
        return hw_write_text(path, t);
    }
#if defined(__FreeBSD__)
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) return -1;
    int v = m->level[dev], r = ioctl(fd, MIXER_WRITE(dev), &v);
    if (r == 0 && m->has_mute) { int mu = m->mute; r = ioctl(fd, SOUND_MIXER_WRITE_MUTE, &mu); }
    int e = errno;
    close(fd);
    errno = e;
    return r;
#else
    (void)dev;
    errno = ENOTSUP;
    return -1;
#endif
}

static int mixer_dev(const struct mixer *m) { return m->devmask & (1 << MIX_VOL) ? MIX_VOL : m->devmask & (1 << MIX_PCM) ? MIX_PCM : -1; }
static int level_percent(int lv) { return ((lv & 0x7f) + ((lv >> 8) & 0x7f) + 1) / 2; }

enum { B_OSS, B_MIXER, B_COMMANDS };
static int cur_back = -1;

static int forced(void)
{
    const char *f = getenv("LHDE_HW_VOLUME");
    if (!f || !*f) return -1;
    return !strcmp(f, "oss") ? B_OSS : !strcmp(f, "mixer") ? B_MIXER : B_COMMANDS;
}

static int oss_read(lhde_volume *v, char *err, size_t en)
{
    char path[PATH_MAX];
    struct mixer m;
    mixer_path(path, sizeof path);
    if (mixer_load(path, &m) < 0) { snprintf(err, en, "%s: %s", path, strerror(errno)); return 0; }
    int d = mixer_dev(&m);
    if (d < 0) { snprintf(err, en, "%s: no vol or pcm control", path); return 0; }
    v->percent = level_percent(m.level[d]);
    v->muted = m.has_mute ? (m.mute >> d) & 1 : v->percent == 0 && hw_muted_level(path) >= 0;
    hw_copy(v->backend, "oss");
    snprintf(v->device, sizeof v->device, "%s %s", path, d == MIX_VOL ? "vol" : "pcm");
    v->present = 1;
    return 1;
}

/* mixer(8): "vol.volume=0.66:0.66" and "vol.mute=off" */
static int cmd_read(lhde_volume *v, char *err, size_t en)
{
    char path[PATH_MAX], out[1024], line[200];
    mixer_path(path, sizeof path);
    if (!hw_have("mixer")) { snprintf(err, en, "no mixer(8)"); return 0; }
    const char *argv[] = { "mixer", "-f", path, "-o", "vol", NULL };
    char *p;
    if (hw_run(argv, out, sizeof out) != 0 || !(p = strstr(out, "vol.volume="))) {
        snprintf(err, en, "mixer: %s", hw_first_line(out[0] ? out : "no answer", line, sizeof line));
        return 0;
    }
    double l = atof(p + 11), r = l;
    char *c = strchr(p, ':'), *nl = strchr(p, '\n');
    if (c && (!nl || c < nl)) r = atof(c + 1);
    v->percent = (int)floor((l + r) * 50 + 0.5);
    char *mu = strstr(out, "vol.mute=");
    v->muted = mu && (!strncmp(mu + 9, "on", 2) || mu[9] == '1');
    hw_copy(v->backend, "mixer");
    snprintf(v->device, sizeof v->device, "%s vol", path);
    v->present = 1;
    return 1;
}

int hw_freebsd_volume(lhde_volume *v)
{
    int f = forced();
    char err[200] = "", e2[200] = "";
    if (f == B_COMMANDS) { int r = hw_linux_volume(v); cur_back = r ? B_COMMANDS : -1; return r; }
    if ((f < 0 || f == B_OSS) && oss_read(v, err, sizeof err)) { cur_back = B_OSS; return 1; }
    if ((f < 0 || f == B_MIXER) && cmd_read(v, e2, sizeof e2)) { cur_back = B_MIXER; return 1; }
    if (f < 0) {
        lhde_volume c;
        memset(&c, 0, sizeof c);
        if (hw_linux_volume(&c)) { *v = c; cur_back = B_COMMANDS; return 1; }   /* a PulseAudio without an OSS mixer */
    }
    cur_back = -1;
    snprintf(v->error, sizeof v->error, "%s%s%s", f == B_MIXER ? "" : err, f < 0 ? "; " : "", f == B_OSS ? "" : e2);
    return 0;
}

static int back_for_set(void)
{
    if (cur_back < 0) { lhde_volume v; memset(&v, 0, sizeof v); if (!hw_freebsd_volume(&v)) { hw_fail("%s", v.error); return -1; } }
    return cur_back;
}

static int oss_change(int percent, int mute)          /* percent -1 keeps it; mute -2 keeps it, -1 toggles */
{
    char path[PATH_MAX];
    struct mixer m;
    mixer_path(path, sizeof path);
    if (mixer_load(path, &m) < 0) { hw_fail("%s: %s", path, strerror(errno)); return -1; }
    int d = mixer_dev(&m);
    if (d < 0) { hw_fail("%s: no vol or pcm control", path); return -1; }
    if (percent >= 0) {
        m.level[d] = percent | percent << 8;
        if (!m.has_mute) hw_set_muted_level(path, -1);     /* a level set by hand ends a mute done by 0 */
    }
    if (mute != -2) {
        if (m.has_mute) {
            int bit = 1 << d;
            m.mute = mute == -1 ? m.mute ^ bit : mute ? m.mute | bit : m.mute & ~bit;
        } else {
            int remembered = hw_muted_level(path), cur = level_percent(m.level[d]);
            int muted = remembered >= 0 && cur == 0;
            int want = mute == -1 ? !muted : mute;
            if (want && !muted) { hw_set_muted_level(path, cur); m.level[d] = 0; }
            else if (!want && muted) { m.level[d] = remembered | remembered << 8; hw_set_muted_level(path, -1); }
        }
    }
    if (mixer_store(path, &m, d) < 0) { hw_fail("%s: %s", path, strerror(errno)); return -1; }
    return 0;
}

static int cmd_change(const char *control)
{
    char path[PATH_MAX], out[512], line[200];
    mixer_path(path, sizeof path);
    const char *argv[] = { "mixer", "-f", path, control, NULL };
    if (hw_run(argv, out, sizeof out) == 0) return 0;
    hw_fail("mixer: %s", hw_first_line(out[0] ? out : "failed", line, sizeof line));
    return -1;
}

int hw_freebsd_volume_set(int percent)
{
    int k = back_for_set();
    if (k == B_OSS) return oss_change(percent, -2);
    if (k == B_MIXER) { char c[32]; snprintf(c, sizeof c, "vol.volume=%.2f", percent / 100.0); return cmd_change(c); }
    if (k == B_COMMANDS) return hw_linux_volume_set(percent);
    return -1;
}

int hw_freebsd_volume_mute(int on)
{
    int k = back_for_set();
    if (k == B_OSS) return oss_change(-1, on);
    if (k == B_MIXER) return cmd_change(on < 0 ? "vol.mute=toggle" : on ? "vol.mute=on" : "vol.mute=off");
    if (k == B_COMMANDS) return hw_linux_volume_mute(on);
    return -1;
}
