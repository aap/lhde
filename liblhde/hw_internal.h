/* liblhde's hardware layer inside (lhde/hw.h): the helpers hw.c gives the system back ends, and the back ends' entry
   points. Not installed. */
#ifndef LHDE_HW_INTERNAL_H
#define LHDE_HW_INTERNAL_H
#include <stddef.h>
#include "lhde/hw.h"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wformat-truncation"   /* the records' fixed fields: a longer name or message is cut */
#endif

/* $LHDE_HW_FAKE, NULL when unset or empty */
const char *hw_fake(void);
/* path under the fake root when there is one, else path itself, into out */
const char *hw_path(const char *path, char *out, size_t n);
/* a file's first line, trailing white space removed; NULL when it cannot be read */
const char *hw_read_line(const char *path, char *buf, size_t n);
int hw_read_long(const char *path, long *v);
int hw_write_text(const char *path, const char *text);   /* 0 or -1 with errno */
/* the program is on $PATH */
int hw_have(const char *prog);
/* runs argv (no shell) with stdin on /dev/null, its stdout and stderr together into out (NUL-terminated, cut at n - 1),
   killed after two seconds; returns its exit status (0 when it cannot be known: SIGCHLD ignored), -1 when it could not run
   or was killed */
int hw_run(const char *const argv[], char *out, size_t n);
/* out's first line (into buf) */
const char *hw_first_line(const char *s, char *buf, size_t n);
/* the reason a set failed (lhde_hw_error) */
void hw_fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* a child running argv with its stdout on the returned descriptor (non-blocking, close-on-exec), in its own process group,
   killed when this process dies; -1 when it could not start */
int hw_spawn_reader(const char *const argv[], int *pid);
void hw_kill_reader(int *fd, int *pid);
/* the remembered level of a mute done by setting 0 (FreeBSD's OSS without SOUND_MIXER_MUTE, or mixer(8)), shared by every
   process of the user: ${XDG_STATE_HOME:-~/.local/state}/lhde/hw-volume-muted; -1 when nothing is remembered */
int hw_muted_level(const char *device);
void hw_set_muted_level(const char *device, int level);   /* level -1 forgets */
#define hw_copy(dst, src) hw_strlcpy((dst), (src), sizeof(dst))
void hw_strlcpy(char *dst, const char *src, size_t n);

/* the back ends: the same calls as lhde/hw.h; describe appends lines for what */
int hw_linux_battery(lhde_battery *b);
int hw_linux_backlight(lhde_backlight *b);
int hw_linux_backlight_set(int percent);
int hw_linux_volume(lhde_volume *v);
int hw_linux_volume_set(int percent);
int hw_linux_volume_mute(int on);
int hw_linux_volume_fd(int *pid);
int hw_linux_battery_fd(void);
int hw_linux_battery_event(int fd);        /* one message: 1 a battery or AC one, 0 another, 2 none waiting, -1 ended */
int hw_freebsd_battery(lhde_battery *b);
int hw_freebsd_backlight(lhde_backlight *b);
int hw_freebsd_backlight_set(int percent);
int hw_freebsd_volume(lhde_volume *v);
int hw_freebsd_volume_set(int percent);
int hw_freebsd_volume_mute(int on);
int hw_freebsd_battery_fd(void);
int hw_freebsd_battery_event(int fd);
#endif
