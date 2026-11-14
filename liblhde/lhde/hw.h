/* liblhde: the hardware a desktop shows and sets: the battery, the screen backlight, the volume of the default output and the
   touchpad, one interface over a back end per system (docs/APPLICATIONS.md "Hardware"; lhde-hw is its command line).

     Linux     battery /sys/class/power_supply (type Battery), change events from the kernel's uevent socket;
               backlight /sys/class/backlight (written when writable, else brightnessctl);
               volume wpctl (PipeWire), pactl (PulseAudio), amixer (ALSA), the first that answers; `pactl subscribe` for
               change events
     FreeBSD   battery sysctl hw.acpi.battery.* and hw.acpi.acline, change events from devd's socket;
               backlight /dev/backlight ioctls (backlight(8) when they fail);
               volume the OSS mixer of hw.snd.default_unit (ioctls; mixer(8) when they fail), no change events
     both      touchpad through xinput: it is the X server's device, not the system's

   Reads fill a record and return whether the thing is there (1) or not (0: no battery, no backlight, no touchpad, no mixer
   answering; the record's error says why when there is something to say). Sets return 0, or -1 with lhde_hw_error().
   A call may run a program (wpctl, xinput) and wait for it, at most two seconds: call it where that may block (a worker
   thread, a child). Not thread-safe: one thread of a process uses it.

   Test hooks (environment):
     LHDE_HW_FAKE=DIR     read DIR instead of the system: DIR/sys/class/... for Linux; for FreeBSD DIR/sysctl/NAME (a sysctl's
                          value), DIR/dev/backlight/NAME ("brightness N" lines, written back on a set) and DIR/dev/mixerN
                          ("vol L:R", "mute 0|1", "devmask N" lines); no event sources
     LHDE_HW_SYSTEM=linux|freebsd   the back end to use (the fake trees of the other system on this one)
     LHDE_HW_VOLUME=NAME  only that volume back end: wpctl, pactl, amixer, oss, mixer */
#ifndef LHDE_HW_H
#define LHDE_HW_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { LHDE_HW_BATTERY = 1, LHDE_HW_BACKLIGHT = 2, LHDE_HW_VOLUME = 4, LHDE_HW_TOUCHPAD = 8, LHDE_HW_ALL = 15 };
enum { LHDE_BATTERY_UNKNOWN, LHDE_BATTERY_CHARGING, LHDE_BATTERY_DISCHARGING, LHDE_BATTERY_FULL, LHDE_BATTERY_IDLE };

typedef struct lhde_battery {
    int count;                  /* batteries (0: none, a desktop); the rest is over all of them */
    int percent;                /* 0..100, -1 unknown */
    int state;                  /* LHDE_BATTERY_*: IDLE is on AC, neither charging nor full */
    int ac;                     /* 1 on mains, 0 on battery, -1 unknown */
    int minutes;                /* to empty while discharging, to full while charging; -1 unknown */
    double now, full, rate;     /* the charge, its full value and the rate in unit (Wh and W, or Ah and A); 0 unknown */
    char unit[4];               /* "Wh", "Ah", "" */
    char model[64], technology[32];
    char source[128];           /* what was read: "/sys/class/power_supply/BAT0", "sysctl hw.acpi.battery (2 units)" */
} lhde_battery;

typedef struct lhde_backlight {
    int present;
    int percent;                /* 0..100 */
    int raw, max;               /* the device's own level and its maximum */
    char device[64];            /* intel_backlight, backlight0 */
    char method[32];            /* how a set goes: sysfs, brightnessctl, ioctl, backlight(8); "" none */
    char error[256];            /* why a set cannot work ("" when it can) */
} lhde_backlight;

typedef struct lhde_volume {
    int present;                /* a back end answered */
    int percent;                /* 0..100 (a level above 100 % is reported as it is) */
    int muted;
    char backend[16];           /* wpctl, pactl, amixer, oss, mixer */
    char device[64];            /* @DEFAULT_AUDIO_SINK@, @DEFAULT_SINK@, Master, /dev/mixer0 */
    char error[256];            /* "no wpctl, pactl or amixer found", "pactl: <its first line>" */
} lhde_volume;

typedef struct lhde_touchpad {
    int present;                /* xinput lists a slave pointer named *touchpad* or *trackpad* */
    int enabled;
    int id;                     /* its X input device id */
    char name[96];
    char prop[48];              /* "libinput Device Enabled" or "Device Enabled" */
    char error[160];
} lhde_touchpad;

int lhde_hw_battery(lhde_battery *b);
int lhde_hw_backlight(lhde_backlight *b);
int lhde_hw_backlight_set(int percent);        /* clamped to 1..100 (0 turns some panels off) */
int lhde_hw_volume(lhde_volume *v);
int lhde_hw_volume_set(int percent);           /* clamped to 0..100 */
int lhde_hw_volume_mute(int on);               /* 1 mute, 0 unmute, -1 toggle */
int lhde_hw_touchpad(lhde_touchpad *t);
int lhde_hw_touchpad_set(int enabled);
const char *lhde_hw_error(void);               /* why the last set failed */

/* Change events. A descriptor to poll for input, -1 when the system offers none (then read every LHDE_HW_*_POLL_MS);
   after it is readable, the poll call drains it and returns 1 when the thing may have changed, 0 when not, -1 when the
   source ended (its descriptor is closed: read on a timer from then on). The volume's is a `pactl subscribe` child (Linux,
   PulseAudio or PipeWire's pulse server); the battery's the kernel's uevents (Linux) or devd's socket (FreeBSD).
   lhde_hw_close() ends them (the child gets SIGTERM; it also dies with the process). */
int lhde_hw_volume_fd(void);
int lhde_hw_volume_poll(void);
int lhde_hw_battery_fd(void);
int lhde_hw_battery_poll(void);
void lhde_hw_close(void);
#define LHDE_HW_BATTERY_POLL_MS   30000
#define LHDE_HW_BACKLIGHT_POLL_MS 1000         /* the brightness keys change it behind us, without an event */
#define LHDE_HW_VOLUME_POLL_MS    1000
#define LHDE_HW_TOUCHPAD_POLL_MS  2000

/* "volume: wpctl @DEFAULT_AUDIO_SINK@ (PipeWire)" lines, one for each of what (LHDE_HW_* bits): the back end in use, what it
   reads and what a set needs; for tooltips and diagnosis */
const char *lhde_hw_describe(int what, char *buf, size_t n);

/* every field of a record (what is one LHDE_HW_* bit) as a key and a value, for bindings and printing: type 's' a string,
   'n' a number, 'b' a boolean (n 0 or 1). The battery's state is the string "charging", "discharging", "full", "idle" or
   "unknown"; its ac a boolean, absent when unknown. */
typedef void (*lhde_hw_field_fn)(const char *key, int type, const char *s, double n, void *ud);
void lhde_hw_fields(int what, const void *record, lhde_hw_field_fn fn, void *ud);
const char *lhde_hw_state_name(int state);

#ifdef __cplusplus
}
#endif
#endif
