/* liblhde's hardware layer (lhde/hw.h) over recorded trees and stand-in commands, both back ends on this system:
   LHDE_HW_SYSTEM picks one, LHDE_HW_FAKE points it at a scratch tree (Linux: sys/class/power_supply and backlight;
   FreeBSD: sysctl/NAME files, dev/backlight, dev/mixerN), and PATH holds only shell stubs of wpctl, pactl, amixer,
   brightnessctl, mixer and xinput that log their arguments to $TR_LOG and answer from files. make check-liblhde.
   Covers: batteries combined (energy and charge, a device's own battery left out, states, AC, minutes), the backlight's
   device ranking, levels, the 1 % floor, a read-only device and the command fallback; the volume back ends in order and
   forced, their parsing and sets, OSS's mute mask and the mute by 0 remembered across calls; the touchpad's device and
   property; describe and fields. */
#define _GNU_SOURCE
#include "lhde/hw.h"
#include <ftw.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails, passes;
static char T[PATH_MAX];

#define CHECK(c) do { if (c) passes++; else { fails++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
static void ieq(int line, const char *what, long got, long want)
{
    if (got == want) { passes++; return; }
    fails++; fprintf(stderr, "FAIL line %d: %s: got %ld, expected %ld\n", line, what, got, want);
}
#define IEQ(a, b) ieq(__LINE__, #a, (long)(a), (long)(b))
static void eq(int line, const char *what, const char *got, const char *want)
{
    if (got && want && !strcmp(got, want)) { passes++; return; }
    fails++; fprintf(stderr, "FAIL line %d: %s\n  got      [%s]\n  expected [%s]\n", line, what, got ? got : "(null)", want);
}
#define EQ(a, b) eq(__LINE__, #a, (a), (b))
static void has(int line, const char *what, const char *got, const char *needle)
{
    if (got && strstr(got, needle)) { passes++; return; }
    fails++; fprintf(stderr, "FAIL line %d: %s\n  got      [%s]\n  should contain [%s]\n", line, what, got ? got : "(null)", needle);
}
#define HAS(a, b) has(__LINE__, #a, (a), (b))

static void mkdirs(const char *d)
{
    char p[PATH_MAX]; snprintf(p, sizeof p, "%s", d);
    for (char *s = p + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(p, 0755); *s = '/'; }
    mkdir(p, 0755);
}
static void put(const char *rel, const char *text)
{
    char p[PATH_MAX], d[PATH_MAX];
    snprintf(p, sizeof p, "%s/%s", T, rel);
    snprintf(d, sizeof d, "%s", p); *strrchr(d, '/') = 0; mkdirs(d);
    FILE *f = fopen(p, "w"); if (!f) { perror(p); exit(2); }
    fputs(text, f); fclose(f);
}
static void chmod_rel(const char *rel, mode_t m) { char p[PATH_MAX]; snprintf(p, sizeof p, "%s/%s", T, rel); chmod(p, m); }
static char *slurp(const char *rel)
{
    static char buf[65536];
    char p[PATH_MAX]; snprintf(p, sizeof p, "%s/%s", T, rel);
    FILE *f = fopen(p, "r"); if (!f) return NULL;
    size_t n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0; fclose(f);
    return buf;
}
static int rm_one(const char *p, const struct stat *st, int fl, struct FTW *ftw) { (void)st; (void)fl; (void)ftw; return remove(p); }
static void rmtree(const char *rel) { char p[PATH_MAX]; snprintf(p, sizeof p, "%s/%s", T, rel); nftw(p, rm_one, 16, FTW_DEPTH | FTW_PHYS); }

/* a stub command: logs "NAME ARGS" to $TR_LOG, then runs body */
static void stub(const char *name, const char *body)
{
    char rel[256], text[4096];
    snprintf(rel, sizeof rel, "bin/%s", name);
    snprintf(text, sizeof text, "#!/bin/sh\necho \"%s $*\" >> \"$TR_LOG\"\n%s\n", name, body);
    put(rel, text);
    chmod_rel(rel, 0755);
}
static void unstub(const char *name) { char rel[256]; snprintf(rel, sizeof rel, "%s/bin/%s", T, name); unlink(rel); }
static void clear_log(void) { put("log", ""); }
static const char *logged(void) { const char *s = slurp("log"); return s ? s : ""; }

static void fake(const char *system, const char *dir)
{
    char p[PATH_MAX];
    setenv("LHDE_HW_SYSTEM", system, 1);
    snprintf(p, sizeof p, "%s/%s", T, dir);
    mkdirs(p);
    setenv("LHDE_HW_FAKE", p, 1);
}

static void linux_battery(void)
{
    fake("linux", "lin1");
    put("lin1/sys/class/power_supply/AC/type", "Mains\n");
    put("lin1/sys/class/power_supply/AC/online", "0\n");
    put("lin1/sys/class/power_supply/BAT0/type", "Battery\n");
    put("lin1/sys/class/power_supply/BAT0/status", "Discharging\n");
    put("lin1/sys/class/power_supply/BAT0/energy_now", "20000000\n");
    put("lin1/sys/class/power_supply/BAT0/energy_full", "23190000\n");
    put("lin1/sys/class/power_supply/BAT0/power_now", "7500000\n");
    put("lin1/sys/class/power_supply/BAT0/capacity", "86\n");
    put("lin1/sys/class/power_supply/BAT0/model_name", "45N1111\n");
    put("lin1/sys/class/power_supply/BAT0/technology", "Li-ion\n");
    put("lin1/sys/class/power_supply/BAT1/type", "Battery\n");
    put("lin1/sys/class/power_supply/BAT1/status", "Unknown\n");
    put("lin1/sys/class/power_supply/BAT1/energy_now", "10000000\n");
    put("lin1/sys/class/power_supply/BAT1/energy_full", "21000000\n");
    put("lin1/sys/class/power_supply/BAT1/power_now", "0\n");
    put("lin1/sys/class/power_supply/hidpp_battery_0/type", "Battery\n");
    put("lin1/sys/class/power_supply/hidpp_battery_0/scope", "Device\n");
    put("lin1/sys/class/power_supply/hidpp_battery_0/capacity", "5\n");
    lhde_battery b;
    IEQ(lhde_hw_battery(&b), 1);
    IEQ(b.count, 2);                                       /* the mouse's battery is not the computer's */
    IEQ(b.percent, 68);                                    /* 30 of 44.19 Wh */
    IEQ(b.state, LHDE_BATTERY_DISCHARGING);
    IEQ(b.ac, 0);
    IEQ(b.minutes, 240);                                   /* 30 Wh at 7.5 W */
    EQ(b.unit, "Wh");
    EQ(b.model, "45N1111");
    HAS(b.source, "BAT0 BAT1");
    /* charge in µAh, charging on AC */
    fake("linux", "lin2");
    put("lin2/sys/class/power_supply/ADP1/type", "Mains\n");
    put("lin2/sys/class/power_supply/ADP1/online", "1\n");
    put("lin2/sys/class/power_supply/BAT0/type", "Battery\n");
    put("lin2/sys/class/power_supply/BAT0/status", "Charging\n");
    put("lin2/sys/class/power_supply/BAT0/charge_now", "1000000\n");
    put("lin2/sys/class/power_supply/BAT0/charge_full", "4000000\n");
    put("lin2/sys/class/power_supply/BAT0/current_now", "2000000\n");
    IEQ(lhde_hw_battery(&b), 1);
    IEQ(b.percent, 25);
    IEQ(b.state, LHDE_BATTERY_CHARGING);
    IEQ(b.ac, 1);
    IEQ(b.minutes, 90);                                    /* 3 Ah to go at 2 A */
    EQ(b.unit, "Ah");
    /* full and not charging: idle; capacity only */
    put("lin2/sys/class/power_supply/BAT0/status", "Not charging\n");
    IEQ(lhde_hw_battery(&b), 1);
    IEQ(b.state, LHDE_BATTERY_IDLE);
    IEQ(b.minutes, -1);
    put("lin2/sys/class/power_supply/BAT0/status", "Full\n");
    IEQ(lhde_hw_battery(&b), 1);
    IEQ(b.state, LHDE_BATTERY_FULL);
    fake("linux", "lin3");
    IEQ(lhde_hw_battery(&b), 0);
    IEQ(b.count, 0);
    IEQ(b.ac, -1);
}

static void linux_backlight(void)
{
    fake("linux", "lin4");
    put("lin4/sys/class/backlight/intel_backlight/type", "raw\n");
    put("lin4/sys/class/backlight/intel_backlight/max_brightness", "1515\n");
    put("lin4/sys/class/backlight/intel_backlight/brightness", "100\n");
    put("lin4/sys/class/backlight/acpi_video0/type", "firmware\n");
    put("lin4/sys/class/backlight/acpi_video0/max_brightness", "1000\n");
    put("lin4/sys/class/backlight/acpi_video0/brightness", "600\n");
    put("lin4/sys/class/backlight/empty/type", "firmware\n");
    lhde_backlight b;
    IEQ(lhde_hw_backlight(&b), 1);
    EQ(b.device, "acpi_video0");                           /* firmware before raw; one without max_brightness skipped */
    IEQ(b.percent, 60);
    IEQ(b.max, 1000);
    EQ(b.method, "sysfs");
    IEQ(lhde_hw_backlight_set(65), 0);
    EQ(slurp("lin4/sys/class/backlight/acpi_video0/brightness"), "650\n");
    IEQ(lhde_hw_backlight_set(0), 0);
    EQ(slurp("lin4/sys/class/backlight/acpi_video0/brightness"), "10\n");   /* never below 1 % */
    if (getuid() != 0) {
        chmod_rel("lin4/sys/class/backlight/acpi_video0/brightness", 0444);
        IEQ(lhde_hw_backlight(&b), 1);
        HAS(b.error, "not writable");
        IEQ(lhde_hw_backlight_set(50), -1);
        HAS(lhde_hw_error(), "not writable");
        stub("brightnessctl", "for a; do v=$a; done; echo \"$v\" > \"$LHDE_HW_FAKE/sys/class/backlight/acpi_video0/brightness.new\"");
        IEQ(lhde_hw_backlight(&b), 1);
        EQ(b.method, "brightnessctl");
        EQ(b.error, "");
        clear_log();
        IEQ(lhde_hw_backlight_set(50), 0);
        EQ(logged(), "brightnessctl -q -d acpi_video0 set 500\n");
        EQ(slurp("lin4/sys/class/backlight/acpi_video0/brightness.new"), "500\n");
        unstub("brightnessctl");
    }
    fake("linux", "lin5");
    IEQ(lhde_hw_backlight(&b), 0);
}

#define WPCTL "case \"$1\" in\n" \
    "get-volume) read v < \"$TR_STATE\"; m=0; [ -f \"$TR_STATE.mute\" ] && read m < \"$TR_STATE.mute\"; [ \"$m\" = 1 ] && v=\"$v [MUTED]\"; echo \"Volume: $v\";;\n" \
    "set-volume) echo \"$3\" > \"$TR_STATE\";;\nset-mute) echo \"$3\" > \"$TR_STATE.mute\";;\nesac"
#define PACTL "case \"$1\" in\n" \
    "get-sink-volume) read v < \"$TR_STATE\"; echo \"Volume: front-left: 36044 /  $v% / -15.58 dB,   front-right: 36044 /  $v% / -15.58 dB\"; echo \"        balance 0.00\";;\n" \
    "get-sink-mute) echo \"Mute: no\";;\nset-sink-volume) echo \"${3%\\%}\" > \"$TR_STATE\";;\nesac"
#define AMIXER "read v < \"$TR_STATE\"; case \"$*\" in\n\"get Master\") echo \"Simple mixer control 'Master',0\"; echo \"  Mono: Playback 26 [$v%] [-12.00dB] [off]\";;\nesac"

static void linux_volume(void)
{
    fake("linux", "lin6");
    lhde_volume v;
    put("state", "0.40\n");
    IEQ(lhde_hw_volume(&v), 0);
    EQ(v.error, "no wpctl, pactl or amixer found");
    stub("wpctl", WPCTL);
    stub("pactl", PACTL);
    IEQ(lhde_hw_volume(&v), 1);
    EQ(v.backend, "wpctl");
    IEQ(v.percent, 40);
    IEQ(v.muted, 0);
    clear_log();
    IEQ(lhde_hw_volume_set(42), 0);
    EQ(logged(), "wpctl set-volume @DEFAULT_AUDIO_SINK@ 0.42\n");
    IEQ(lhde_hw_volume_set(140), 0);
    HAS(logged(), "set-volume @DEFAULT_AUDIO_SINK@ 1.00");
    IEQ(lhde_hw_volume_mute(-1), 0);
    HAS(logged(), "wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle");
    IEQ(lhde_hw_volume_mute(1), 0);
    IEQ(lhde_hw_volume(&v), 1);
    IEQ(v.percent, 100);
    IEQ(v.muted, 1);
    char d[2048];
    HAS(lhde_hw_describe(LHDE_HW_VOLUME, d, sizeof d), "volume: linux wpctl @DEFAULT_AUDIO_SINK@");
    /* PipeWire without a default node: wpctl answers an error, pactl takes over */
    stub("wpctl", "echo \"Translate ID error: '-1' is not a valid ID (returned by default-nodes-api)\"; exit 1");
    put("state", "55\n");
    IEQ(lhde_hw_volume(&v), 1);
    EQ(v.backend, "pactl");
    IEQ(v.percent, 55);
    clear_log();
    IEQ(lhde_hw_volume_set(57), 0);
    EQ(logged(), "pactl set-sink-volume @DEFAULT_SINK@ 57%\n");
    unstub("wpctl"); unstub("pactl");
    stub("amixer", AMIXER);
    put("state", "30\n");
    IEQ(lhde_hw_volume(&v), 1);
    EQ(v.backend, "amixer");
    IEQ(v.percent, 30);
    IEQ(v.muted, 1);
    clear_log();
    IEQ(lhde_hw_volume_mute(0), 0);
    EQ(logged(), "amixer -q set Master unmute\n");
    stub("wpctl", WPCTL);
    setenv("LHDE_HW_VOLUME", "amixer", 1);
    IEQ(lhde_hw_volume(&v), 1);
    EQ(v.backend, "amixer");
    setenv("LHDE_HW_VOLUME", "pactl", 1);
    IEQ(lhde_hw_volume(&v), 0);
    EQ(v.error, "pactl not found");
    unsetenv("LHDE_HW_VOLUME");
    unstub("wpctl"); unstub("amixer");
    /* fields: the keys of a binding */
    put("state", "0.40\n");
    stub("wpctl", WPCTL);
    lhde_hw_volume(&v);
    unstub("wpctl");
}

#define XINPUT "case \"$1\" in\n" \
    "list) printf \"%%s\\n\" \"⎡ Virtual core pointer                    \tid=2\t[master pointer  (3)]\" \"⎜   ↳ Virtual core XTEST pointer              \tid=4\t[slave  pointer  (2)]\" \"⎜   ↳ SynPS/2 Synaptics TouchPad              \tid=12\t[slave  pointer  (2)]\" \"⎣ Virtual core keyboard                   \tid=3\t[master keyboard (2)]\";;\n" \
    "list-props) read v < \"$TR_STATE\"; printf \"Device 'SynPS/2 Synaptics TouchPad':\\n\\tDevice Enabled (185):\\t%%s\\n\\tlibinput Tapping Enabled (320):\\t1\\n\\tlibinput Device Enabled (186):\\t%%s\\n\" \"$v\" \"$v\";;\n" \
    "set-prop) echo \"$4\" > \"$TR_STATE\";;\nesac"

static void touchpad(void)
{
    lhde_touchpad t;
    IEQ(lhde_hw_touchpad(&t), 0);
    EQ(t.error, "no xinput");
    char x[4096];
    snprintf(x, sizeof x, XINPUT);
    stub("xinput", x);
    put("state", "1\n");
    IEQ(lhde_hw_touchpad(&t), 1);
    IEQ(t.id, 12);
    EQ(t.name, "SynPS/2 Synaptics TouchPad");
    EQ(t.prop, "libinput Device Enabled");
    IEQ(t.enabled, 1);
    clear_log();
    IEQ(lhde_hw_touchpad_set(0), 0);
    HAS(logged(), "xinput set-prop 12 libinput Device Enabled 0\n");
    IEQ(lhde_hw_touchpad(&t), 1);
    IEQ(t.enabled, 0);
    /* turned off, the X server detaches it from the core pointer: a floating slave, still found (soma's Xorg) */
    stub("xinput", "case \"$1\" in\nlist) printf '\\342\\210\\274 SynPS/2 Synaptics TouchPad   \\tid=10\\t[floating slave]\\n';;\n"
                   "list-props) printf '\\tDevice Enabled (186):\\t0\\n\\tlibinput Tapping Enabled (349):\\t0\\n';;\nesac");
    IEQ(lhde_hw_touchpad(&t), 1);
    IEQ(t.id, 10);
    EQ(t.name, "SynPS/2 Synaptics TouchPad");
    EQ(t.prop, "Device Enabled");
    IEQ(t.enabled, 0);
    clear_log();
    IEQ(lhde_hw_touchpad_set(1), 0);
    HAS(logged(), "xinput set-prop 10 Device Enabled 1\n");
    /* the synaptics driver: "Device Enabled" only */
    stub("xinput", "case \"$1\" in\nlist) printf '  SynPS/2 Synaptics TouchPad   \\tid=11\\t[slave  pointer  (2)]\\n';;\n"
                   "list-props) printf '\\tDevice Enabled (139):\\t1\\n\\tSynaptics Tap Time (290):\\t180\\n';;\nesac");
    IEQ(lhde_hw_touchpad(&t), 1);
    EQ(t.prop, "Device Enabled");
    IEQ(t.id, 11);
    stub("xinput", "[ \"$1\" = list ] && printf '  Virtual core XTEST pointer   \\tid=4\\t[slave  pointer  (2)]\\n'");
    IEQ(lhde_hw_touchpad(&t), 0);
    unstub("xinput");
}

static void freebsd_battery(void)
{
    fake("freebsd", "bsd1");
    /* soma, a ThinkPad T460 on AC: two batteries, 99 %, not charging */
    put("bsd1/sysctl/hw.acpi.battery.units", "2\n");
    put("bsd1/sysctl/hw.acpi.battery.life", "99\n");
    put("bsd1/sysctl/hw.acpi.battery.time", "-1\n");
    put("bsd1/sysctl/hw.acpi.battery.state", "0\n");
    put("bsd1/sysctl/hw.acpi.battery.rate", "0\n");
    put("bsd1/sysctl/hw.acpi.acline", "1\n");
    lhde_battery b;
    IEQ(lhde_hw_battery(&b), 1);
    IEQ(b.count, 2);
    IEQ(b.percent, 99);
    IEQ(b.state, LHDE_BATTERY_IDLE);
    IEQ(b.ac, 1);
    IEQ(b.minutes, -1);
    EQ(b.source, "sysctl hw.acpi.battery (2 units)");
    put("bsd1/sysctl/hw.acpi.battery.state", "1\n");
    put("bsd1/sysctl/hw.acpi.battery.time", "263\n");
    put("bsd1/sysctl/hw.acpi.battery.rate", "8125\n");
    put("bsd1/sysctl/hw.acpi.acline", "0\n");
    IEQ(lhde_hw_battery(&b), 1);
    IEQ(b.state, LHDE_BATTERY_DISCHARGING);
    IEQ(b.minutes, 263);
    IEQ(b.ac, 0);
    CHECK(b.rate > 8.12 && b.rate < 8.13);
    put("bsd1/sysctl/hw.acpi.battery.state", "2\n");
    put("bsd1/sysctl/hw.acpi.acline", "1\n");
    IEQ(lhde_hw_battery(&b), 1);
    IEQ(b.state, LHDE_BATTERY_CHARGING);
    IEQ(b.minutes, -1);
    put("bsd1/sysctl/hw.acpi.battery.state", "0\n");
    put("bsd1/sysctl/hw.acpi.battery.life", "100\n");
    IEQ(lhde_hw_battery(&b), 1);
    IEQ(b.state, LHDE_BATTERY_FULL);
    put("bsd1/sysctl/hw.acpi.battery.state", "7\n");      /* ACPI_BATT_STAT_NOT_PRESENT */
    IEQ(lhde_hw_battery(&b), 0);
    fake("freebsd", "bsd2");                               /* a desktop: no acpi_battery */
    put("bsd2/sysctl/hw.acpi.acline", "1\n");
    IEQ(lhde_hw_battery(&b), 0);
    IEQ(b.ac, 1);
    HAS(b.source, "absent");
}

static void freebsd_backlight(void)
{
    fake("freebsd", "bsd3");
    put("bsd3/dev/backlight/backlight0", "brightness 39\n");
    char a[PATH_MAX], l[PATH_MAX];
    snprintf(a, sizeof a, "backlight0");
    snprintf(l, sizeof l, "%s/bsd3/dev/backlight/intel_backlight0", T);
    if (symlink(a, l) < 0) perror(l);
    lhde_backlight b;
    IEQ(lhde_hw_backlight(&b), 1);
    EQ(b.device, "backlight0");                            /* intel_backlight0 is its symlink */
    IEQ(b.percent, 39);
    IEQ(b.max, 100);
    EQ(b.method, "ioctl");
    IEQ(lhde_hw_backlight_set(45), 0);
    EQ(slurp("bsd3/dev/backlight/backlight0"), "brightness 45\n");
    IEQ(lhde_hw_backlight_set(-5), 0);
    EQ(slurp("bsd3/dev/backlight/backlight0"), "brightness 1\n");
    if (getuid() != 0) {
        chmod_rel("bsd3/dev/backlight/backlight0", 0444);
        IEQ(lhde_hw_backlight(&b), 1);
        HAS(b.error, "not writable");
        HAS(b.error, "join its group");
        IEQ(lhde_hw_backlight_set(50), -1);
    }
    fake("freebsd", "bsd4");
    IEQ(lhde_hw_backlight(&b), 0);
}

static void freebsd_volume(void)
{
    fake("freebsd", "bsd5");
    put("bsd5/sysctl/hw.snd.default_unit", "1\n");
    put("bsd5/dev/mixer0", "vol 10:10\n");
    put("bsd5/dev/mixer1", "vol 66:66\npcm 100:100\nmute 0\n");
    lhde_volume v;
    IEQ(lhde_hw_volume(&v), 1);
    EQ(v.backend, "oss");
    HAS(v.device, "/dev/mixer1 vol");                      /* hw.snd.default_unit's */
    IEQ(v.percent, 66);
    IEQ(v.muted, 0);
    IEQ(lhde_hw_volume_set(40), 0);
    EQ(slurp("bsd5/dev/mixer1"), "vol 40:40\npcm 100:100\nmute 0\n");
    IEQ(lhde_hw_volume_mute(-1), 0);
    EQ(slurp("bsd5/dev/mixer1"), "vol 40:40\npcm 100:100\nmute 1\n");
    IEQ(lhde_hw_volume(&v), 1);
    IEQ(v.muted, 1);
    IEQ(v.percent, 40);
    IEQ(lhde_hw_volume_mute(0), 0);
    EQ(slurp("bsd5/dev/mixer1"), "vol 40:40\npcm 100:100\nmute 0\n");
    char d[2048];
    HAS(lhde_hw_describe(LHDE_HW_VOLUME, d, sizeof d), "volume: freebsd oss ");
    /* no SOUND_MIXER_MUTE: a mute sets 0 and remembers the level for the next call, in any process */
    put("bsd5/dev/mixer1", "vol 57:55\n");
    IEQ(lhde_hw_volume(&v), 1);
    IEQ(v.percent, 56);
    IEQ(lhde_hw_volume_mute(1), 0);
    EQ(slurp("bsd5/dev/mixer1"), "vol 0:0\n");
    HAS(slurp("state-home/lhde/hw-volume-muted"), "/dev/mixer1 56\n");
    IEQ(lhde_hw_volume(&v), 1);
    IEQ(v.muted, 1);
    IEQ(lhde_hw_volume_mute(-1), 0);
    EQ(slurp("bsd5/dev/mixer1"), "vol 56:56\n");
    IEQ(lhde_hw_volume(&v), 1);
    IEQ(v.muted, 0);
    IEQ(lhde_hw_volume_mute(1), 0);
    IEQ(lhde_hw_volume_set(20), 0);                        /* a level set by hand ends that mute */
    IEQ(lhde_hw_volume(&v), 1);
    IEQ(v.muted, 0);
    IEQ(v.percent, 20);
    /* mixer(8) when the device does not answer (LHDE_HW_VOLUME=mixer here: a fake device always answers) */
    stub("mixer", "case \"$*\" in\n*-o*) read v < \"$TR_STATE\"; echo \"vol.volume=$v\"; echo \"vol.mute=off\";;\n"
                  "*vol.volume=*) echo \"${3#vol.volume=}\" > \"$TR_STATE\";;\nesac");
    put("state", "0.66:0.64\n");
    setenv("LHDE_HW_VOLUME", "mixer", 1);
    IEQ(lhde_hw_volume(&v), 1);
    EQ(v.backend, "mixer");
    IEQ(v.percent, 65);
    clear_log();
    IEQ(lhde_hw_volume_set(30), 0);
    HAS(logged(), "mixer -f ");
    HAS(logged(), "/dev/mixer1 vol.volume=0.30\n");
    IEQ(lhde_hw_volume_mute(-1), 0);
    HAS(logged(), "vol.mute=toggle");
    unsetenv("LHDE_HW_VOLUME");
    unstub("mixer");
    /* no mixer device, a PulseAudio answering */
    fake("freebsd", "bsd6");
    IEQ(lhde_hw_volume(&v), 0);
    stub("pactl", PACTL);
    put("state", "48\n");
    IEQ(lhde_hw_volume(&v), 1);
    EQ(v.backend, "pactl");
    IEQ(v.percent, 48);
    clear_log();
    IEQ(lhde_hw_volume_set(50), 0);
    EQ(logged(), "pactl set-sink-volume @DEFAULT_SINK@ 50%\n");
    unstub("pactl");
}

struct fieldlog { char text[1024]; };
static void field(const char *key, int type, const char *s, double n, void *ud)
{
    struct fieldlog *f = ud;
    size_t k = strlen(f->text);
    if (type == 's') snprintf(f->text + k, sizeof f->text - k, "%s=%s;", key, s);
    else snprintf(f->text + k, sizeof f->text - k, "%s=%c%g;", key, type, n);
}

static void fields(void)
{
    lhde_battery b;
    memset(&b, 0, sizeof b);
    b.count = 1; b.percent = 50; b.state = LHDE_BATTERY_CHARGING; b.ac = 1; b.minutes = 30;
    struct fieldlog f = { "" };
    lhde_hw_fields(LHDE_HW_BATTERY, &b, field, &f);
    HAS(f.text, "count=n1;percent=n50;state=charging;ac=b1;minutes=n30;");
    b.ac = -1;
    f.text[0] = 0;
    lhde_hw_fields(LHDE_HW_BATTERY, &b, field, &f);
    CHECK(!strstr(f.text, "ac="));
    lhde_volume v;
    memset(&v, 0, sizeof v);
    v.present = 1; v.percent = 40; strcpy(v.backend, "oss");
    f.text[0] = 0;
    lhde_hw_fields(LHDE_HW_VOLUME, &v, field, &f);
    EQ(f.text, "present=b1;percent=n40;muted=b0;backend=oss;device=;error=;");
}

int main(void)
{
    snprintf(T, sizeof T, "%s/lhde-hw-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(T)) { perror("mkdtemp"); return 2; }
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/bin", T); mkdirs(p); setenv("PATH", p, 1);
    snprintf(p, sizeof p, "%s/log", T); setenv("TR_LOG", p, 1);
    snprintf(p, sizeof p, "%s/state", T); setenv("TR_STATE", p, 1);
    snprintf(p, sizeof p, "%s/state-home", T); mkdirs(p); setenv("XDG_STATE_HOME", p, 1);
    unsetenv("LHDE_HW_VOLUME");
    linux_battery();
    linux_backlight();
    linux_volume();
    touchpad();
    freebsd_battery();
    freebsd_backlight();
    freebsd_volume();
    fields();
    IEQ(lhde_hw_volume_fd(), -1);                          /* no event sources under LHDE_HW_FAKE */
    IEQ(lhde_hw_battery_fd(), -1);
    rmtree("");
    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
