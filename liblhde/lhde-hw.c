/* lhde-hw: the hardware layer (lhde/hw.h) from the command line.
     lhde-hw                              the battery, backlight, volume and touchpad, one line each
     lhde-hw battery|backlight|volume|touchpad   one of them
     lhde-hw describe                     the back end in use for each, what it reads and how a set is done
     lhde-hw -k WHAT                      WHAT's record as key=value lines (lhde_hw_fields: the keys luxshell's lh.hw_* have)
     lhde-hw volume N|+N|-N|mute|unmute|toggle
     lhde-hw backlight N|+N|-N            percent, absolute or a step from the current level
     lhde-hw touchpad on|off|toggle
     lhde-hw watch                        a line "volume" or "battery" per change event, until interrupted
   A set prints the new state; exit 1 with the reason on stderr when it fails, 2 on a usage error. */
#include "lhde/hw.h"
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int usage(void)
{
    fprintf(stderr, "usage: lhde-hw [battery|backlight|volume|touchpad|describe] | -k WHAT | volume N|+N|-N|mute|unmute|toggle\n"
                    "       | backlight N|+N|-N | touchpad on|off|toggle | watch\n");
    return 2;
}

static int what_of(const char *s)
{
    return !strcmp(s, "battery") ? LHDE_HW_BATTERY : !strcmp(s, "backlight") ? LHDE_HW_BACKLIGHT
         : !strcmp(s, "volume") ? LHDE_HW_VOLUME : !strcmp(s, "touchpad") ? LHDE_HW_TOUCHPAD : 0;
}

static void field(const char *key, int type, const char *s, double n, void *ud)
{
    (void)ud;
    if (type == 's') printf("%s=%s\n", key, s);
    else if (type == 'b') printf("%s=%s\n", key, n ? "true" : "false");
    else printf("%s=%g\n", key, n);
}

static void show(int what)
{
    if (what == LHDE_HW_BATTERY) {
        lhde_battery b;
        if (!lhde_hw_battery(&b)) { printf("battery: none (%s)\n", b.source); return; }
        printf("battery: %d%% %s", b.percent, lhde_hw_state_name(b.state));
        if (b.ac >= 0) printf(", %s", b.ac ? "on AC" : "on battery");
        if (b.minutes >= 0) printf(", %dh%02d %s", b.minutes / 60, b.minutes % 60, b.state == LHDE_BATTERY_CHARGING ? "to full" : "left");
        if (b.full > 0) printf(", %.1f of %.1f %s", b.now, b.full, b.unit);
        if (b.rate > 0) printf(", %.1f %s", b.rate, b.unit[0] == 'A' ? "A" : "W");
        printf(" (%d batter%s: %s)\n", b.count, b.count == 1 ? "y" : "ies", b.source);
    } else if (what == LHDE_HW_BACKLIGHT) {
        lhde_backlight b;
        if (!lhde_hw_backlight(&b)) { printf("backlight: none%s%s%s\n", b.error[0] ? " (" : "", b.error, b.error[0] ? ")" : ""); return; }
        printf("backlight: %d%% (%s, %d of %d; set: %s)%s%s\n", b.percent, b.device, b.raw, b.max, b.method[0] ? b.method : "none",
               b.error[0] ? " " : "", b.error);
    } else if (what == LHDE_HW_VOLUME) {
        lhde_volume v;
        if (!lhde_hw_volume(&v)) { printf("volume: none (%s)\n", v.error); return; }
        printf("volume: %d%%%s (%s %s)\n", v.percent, v.muted ? " muted" : "", v.backend, v.device);
    } else {
        lhde_touchpad t;
        if (!lhde_hw_touchpad(&t)) { printf("touchpad: none%s%s%s\n", t.error[0] ? " (" : "", t.error, t.error[0] ? ")" : ""); return; }
        printf("touchpad: %s (xinput id %d \"%s\", %s)\n", t.enabled ? "on" : "off", t.id, t.name, t.prop);
    }
}

/* N, +N or -N against the current value */
static int level(const char *arg, int cur, int *out)
{
    char *e;
    long v = strtol(arg, &e, 10);
    if (e == arg || *e) return 0;
    *out = (int)(arg[0] == '+' || arg[0] == '-' ? cur + v : v);
    return 1;
}

static int fail(const char *what)
{
    fprintf(stderr, "lhde-hw: %s: %s\n", what, lhde_hw_error());
    return 1;
}

int main(int argc, char **argv)
{
    if (argc == 1) {
        for (int w = LHDE_HW_BATTERY; w <= LHDE_HW_TOUCHPAD; w <<= 1) show(w);
        return 0;
    }
    if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) { usage(); return 0; }
    if (!strcmp(argv[1], "describe")) {
        char buf[4096];
        fputs(lhde_hw_describe(LHDE_HW_ALL, buf, sizeof buf), stdout);
        return 0;
    }
    if (!strcmp(argv[1], "-k")) {
        int w = argc == 3 ? what_of(argv[2]) : 0;
        if (!w) return usage();
        union { lhde_battery b; lhde_backlight l; lhde_volume v; lhde_touchpad t; } r;
        if (w == LHDE_HW_BATTERY) lhde_hw_battery(&r.b);
        else if (w == LHDE_HW_BACKLIGHT) lhde_hw_backlight(&r.l);
        else if (w == LHDE_HW_VOLUME) lhde_hw_volume(&r.v);
        else lhde_hw_touchpad(&r.t);
        lhde_hw_fields(w, &r, field, NULL);
        return 0;
    }
    if (!strcmp(argv[1], "watch")) {
        int vf = lhde_hw_volume_fd(), bf = lhde_hw_battery_fd();
        if (vf < 0 && bf < 0) { fprintf(stderr, "lhde-hw: no change events here (read on a timer)\n"); return 1; }
        printf("watching:%s%s\n", vf >= 0 ? " volume" : "", bf >= 0 ? " battery" : "");
        fflush(stdout);
        for (;;) {
            struct pollfd p[2] = { { vf, POLLIN, 0 }, { bf, POLLIN, 0 } };
            if (poll(p, 2, -1) < 0) { if (errno == EINTR) continue; break; }
            if (p[0].revents) { int r = lhde_hw_volume_poll(); if (r > 0) puts("volume"); if (r < 0) vf = -1; }
            if (p[1].revents) { int r = lhde_hw_battery_poll(); if (r > 0) puts("battery"); if (r < 0) bf = -1; }
            fflush(stdout);
            if (vf < 0 && bf < 0) break;
        }
        lhde_hw_close();
        return 0;
    }
    int w = what_of(argv[1]);
    if (!w || argc > 3) return usage();
    if (argc == 2) { show(w); return 0; }
    const char *a = argv[2];
    int v;
    if (w == LHDE_HW_VOLUME) {
        lhde_volume cur;
        lhde_hw_volume(&cur);
        int r;
        if (!strcmp(a, "mute")) r = lhde_hw_volume_mute(1);
        else if (!strcmp(a, "unmute")) r = lhde_hw_volume_mute(0);
        else if (!strcmp(a, "toggle")) r = lhde_hw_volume_mute(-1);
        else if (level(a, cur.percent, &v)) r = lhde_hw_volume_set(v);
        else return usage();
        if (r < 0) return fail("volume");
    } else if (w == LHDE_HW_BACKLIGHT) {
        lhde_backlight cur;
        lhde_hw_backlight(&cur);
        if (!level(a, cur.percent, &v)) return usage();
        if (lhde_hw_backlight_set(v) < 0) return fail("backlight");
    } else if (w == LHDE_HW_TOUCHPAD) {
        int on;
        if (!strcmp(a, "on")) on = 1;
        else if (!strcmp(a, "off")) on = 0;
        else if (!strcmp(a, "toggle")) { lhde_touchpad t; lhde_hw_touchpad(&t); on = !t.enabled; }
        else return usage();
        if (lhde_hw_touchpad_set(on) < 0) return fail("touchpad");
    } else return usage();
    show(w);
    return 0;
}
