#ifndef _BERRY_UTILS_H_
#define _BERRY_UTILS_H_

#include "types.h"
#include <stdarg.h>

#define MAX(a, b) ((a > b) ? (a) : (b))
#define MIN(a, b) ((a < b) ? (a) : (b))
#define UNUSED(x) (void)(x)
#define LOGN(msg)      do { if (debug) fprintf(stderr, "luxwm: " msg "\n"); } while (0)
#define LOGP(msg, ...) do { if (debug) fprintf(stderr, "luxwm: " msg "\n", __VA_ARGS__); } while (0)

#include <stdio.h>
/* asprintf / vasprintf under luxwm's own names (utils.c), so the C99 build needs no feature macros */
int wm_asprintf(char **buf, const char *fmt, ...);
int wm_vasprintf(char **buf, const char *fmt, va_list args);

#endif
