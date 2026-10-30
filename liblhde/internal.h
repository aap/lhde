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
const char *lhde__src_name(int src);                         /* file base settings default fallback derived */
/* the current layer (lhde/theme.h): key/value pairs, kv[2i] = "Section.key", kv[2i+1] = value */
struct lhde__layer { char **kv; int n, cap; };
void lhde__layer_add(struct lhde__layer *l, const char *k, const char *v);
void lhde__layer_free(struct lhde__layer *l);
/* settings.ini [current]'s keys when its theme = canon (else empty) */
void lhde__current_layer(const char *canon, struct lhde__layer *l);
/* "visualstyle.STYLE" -> "VisualStyle.style": the record's spelling; "" for a key that is no layer key (Theme.name/base/aliases) */
void lhde__layer_key(const char *k, char *out, size_t n);
#endif
