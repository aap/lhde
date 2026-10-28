/* liblhde's own helpers (not installed). */
#ifndef LHDE_INTERNAL_H
#define LHDE_INTERNAL_H
#include <stddef.h>
char *lhde__trim(char *s);
int lhde__mkdirs(const char *d);
int lhde__write_file(const char *path, const char *data);   /* atomic, through symlinks, parent created; 0 on success */
int lhde__exists(const char *p);
int lhde__is_dir(const char *p);
long long lhde__mtime(const char *p);                        /* ns, -1 when missing */
const char *lhde__config_home(char *out, size_t n);          /* ${XDG_CONFIG_HOME:-~/.config} */
/* the first 4-digit run of s ("slate4051" -> "4051") into out; "" when none */
const char *lhde__build_of(const char *s, char *out, size_t n);
/* settings.ini's legacy [override:NAME] / [override] for theme NAME: the theme key it maps to ("Shell.taskbar") and the value
   in the new vocabulary; iterate i from 0, 0 when past the end */
int lhde__legacy_override(const char *name, int i, char *key, size_t nk, char *val, size_t nv, char *why, size_t nw);
const char *lhde__src_name(int src);                         /* file base settings default fallback derived */
#endif
