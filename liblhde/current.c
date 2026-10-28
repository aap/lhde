/* liblhde: the current layer (lhde/theme.h, docs/THEME-FORMAT.md section 3): settings.ini [current], the unsaved changes on
   top of the selected theme (Windows' "NAME (Modified)"); selecting a theme, Save As, Delete; the lists of visual styles and
   classic schemes Display Properties offers. */
#define _GNU_SOURCE
#include "lhde/theme.h"
#include "lhde/data.h"
#include "lhde/settings.h"
#include "internal.h"
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static const char CUR[] = "current";

/* the selected theme's name after aliases ("" when none or unknown) */
static int selected_theme(char *canon, size_t n)
{
    char b[256];
    canon[0] = 0;
    if (!lhde_settings_get("theme", "name", b, sizeof b)) return 0;
    char f[PATH_MAX];
    if (!lhde_theme_lookup(b, canon, n, f, sizeof f)) { snprintf(canon, n, "%s", b); return 0; }
    return 1;
}

const char *lhde_current_theme(char *out, size_t n)
{
    if (!lhde_settings_get(CUR, "theme", out, n)) { if (n) out[0] = 0; }
    return out;
}

int lhde_current_keys(void (*fn)(const char *, const char *, void *), void *ud)
{
    char canon[256];
    if (!selected_theme(canon, sizeof canon)) return 0;
    struct lhde__layer l;
    lhde__current_layer(canon, &l);
    for (int i = 0; fn && i < l.n; i++) fn(l.kv[2 * i], l.kv[2 * i + 1], ud);
    int n = l.n;
    lhde__layer_free(&l);
    return n;
}

/* the layer without the keys the theme gives anyway: K goes when resolving with every other key gives K's value */
static void prune(const char *theme, struct lhde__layer *l)
{
    for (int i = 0; i < l->n; ) {
        const char **rest = malloc(sizeof *rest * (size_t)(2 * l->n + 2));
        if (!rest) return;
        int m = 0;
        for (int j = 0; j < l->n; j++) if (j != i) { rest[2 * m] = l->kv[2 * j]; rest[2 * m + 1] = l->kv[2 * j + 1]; m++; }
        lhde_theme *t = lhde_theme_resolve_with(theme, rest, m, NULL, 0);
        free(rest);
        const char *v = t ? lhde_theme_get(t, l->kv[2 * i]) : NULL;
        int same = v && !strcmp(v, l->kv[2 * i + 1]) && lhde_theme_source(t, l->kv[2 * i]) != LHDE_FROM_FALLBACK;
        lhde_theme_free(t);
        if (!same) { i++; continue; }
        free(l->kv[2 * i]); free(l->kv[2 * i + 1]);
        memmove(&l->kv[2 * i], &l->kv[2 * i + 2], sizeof *l->kv * (size_t)(2 * (l->n - i - 1)));
        l->n--;
    }
}

/* write [current] = theme + the layer (none: the section removed) */
static int store(const char *theme, const struct lhde__layer *l)
{
    char p[PATH_MAX]; lhde_settings_path(p, sizeof p);
    lhde_ini *ini = lhde_ini_load(p);
    if (!ini) return -1;
    char *before = lhde_ini_text(ini);
    lhde_ini_remove_section(ini, CUR);
    if (l->n) {
        lhde_ini_set(ini, CUR, "theme", theme);
        for (int i = 0; i < l->n; i++) lhde_ini_set(ini, CUR, l->kv[2 * i], l->kv[2 * i + 1]);
    }
    char *after = lhde_ini_text(ini);
    int changed = after && (!before || strcmp(after, before));
    free(after); free(before);
    int r = changed ? lhde_ini_save(ini, p) : 0;
    lhde_ini_free(ini);
    lhde_settings_reload();
    return r ? -1 : changed;
}

int lhde_current_set(const char *const *kv, int n)
{
    char canon[256];
    if (!selected_theme(canon, sizeof canon)) return -1;
    struct lhde__layer l = { NULL, 0, 0 };
    for (int i = 0; i < n; i++) {
        char k[256]; lhde__layer_key(kv[2 * i], k, sizeof k);
        if (!k[0] || !lhde_theme_is_key(k) || !kv[2 * i + 1]) continue;
        int dup = 0;
        for (int j = 0; j < l.n; j++) if (!strcasecmp(l.kv[2 * j], k)) { free(l.kv[2 * j + 1]); l.kv[2 * j + 1] = strdup(kv[2 * i + 1]); dup = 1; }
        if (!dup) lhde__layer_add(&l, k, kv[2 * i + 1]);
    }
    prune(canon, &l);
    int r = store(canon, &l);
    lhde__layer_free(&l);
    return r;
}

int lhde_current_set_key(const char *key, const char *value)
{
    char canon[256], k[256];
    if (!key || !selected_theme(canon, sizeof canon)) return -1;
    lhde__layer_key(key, k, sizeof k);
    if (!k[0] || !lhde_theme_is_key(k)) return -1;
    struct lhde__layer l;
    lhde__current_layer(canon, &l);   /* a layer of another theme reads as empty: replaced */
    int found = -1;
    for (int j = 0; j < l.n; j++) if (!strcasecmp(l.kv[2 * j], k)) found = j;
    if (found >= 0) {
        free(l.kv[2 * found]); free(l.kv[2 * found + 1]);
        memmove(&l.kv[2 * found], &l.kv[2 * found + 2], sizeof *l.kv * (size_t)(2 * (l.n - found - 1)));
        l.n--;
    }
    if (value) lhde__layer_add(&l, k, value);
    prune(canon, &l);
    int r = store(canon, &l);
    lhde__layer_free(&l);
    return r;
}

int lhde_current_clear(void)
{
    char p[PATH_MAX]; lhde_settings_path(p, sizeof p);
    lhde_ini *ini = lhde_ini_load(p);
    if (!ini) return -1;
    int had = lhde_ini_remove_section(ini, CUR);
    int r = had ? lhde_ini_save(ini, p) : 0;
    lhde_ini_free(ini);
    lhde_settings_reload();
    return r ? -1 : had;
}

int lhde_theme_select(const char *name)
{
    char canon[256], file[PATH_MAX];
    if (!name || !lhde_theme_lookup(name, canon, sizeof canon, file, sizeof file)) return -1;
    char p[PATH_MAX]; lhde_settings_path(p, sizeof p);
    lhde_ini *ini = lhde_ini_load(p);
    if (!ini) return -1;
    char owner[256] = "";
    lhde_ini_get(ini, CUR, "theme", owner, sizeof owner);
    int changed = lhde_ini_set(ini, "theme", "name", canon);
    if (lhde_ini_has_section(ini, CUR) && strcmp(owner, canon)) changed |= lhde_ini_remove_section(ini, CUR);
    int r = changed ? lhde_ini_save(ini, p) : 0;
    lhde_ini_free(ini);
    lhde_settings_reload();
    return r ? -1 : changed;
}

static int user_file(const char *name, char *path, size_t n)
{
    char canon[256], file[PATH_MAX], udir[PATH_MAX];
    path[0] = 0;
    if (lhde_theme_lookup(name, canon, sizeof canon, file, sizeof file) != 1 || strcmp(canon, name)) return 0;
    lhde_theme_user_dir(udir, sizeof udir);
    size_t ul = strlen(udir);
    if (strncmp(file, udir, ul) || file[ul] != '/') return 0;
    snprintf(path, n, "%s", file);
    return 1;
}

static int file_name_ok(const char *f)
{
    if (!f || !f[0] || f[0] == '.' || f[0] == '-' || strlen(f) > 100) return 0;
    for (const char *c = f; *c; c++)
        if (!(islower((unsigned char)*c) || isdigit((unsigned char)*c) || *c == '.' || *c == '_' || *c == '-')) return 0;
    return 1;
}

int lhde_theme_save_as(const char *file, const char *title, char *err, size_t nerr)
{
    if (err && nerr) err[0] = 0;
    char cur[256];
    if (!selected_theme(cur, sizeof cur)) { if (err) snprintf(err, nerr, "no theme selected"); return -1; }
    if (!file_name_ok(file)) { if (err) snprintf(err, nerr, "\"%s\" is no theme file name ([a-z0-9._-])", file ? file : ""); return -1; }
    char canon[256], f[PATH_MAX], target[PATH_MAX + 300], udir[PATH_MAX];
    lhde_theme_user_dir(udir, sizeof udir);
    snprintf(target, sizeof target, "%s/%s.theme", udir, file);
    int k = lhde_theme_lookup(file, canon, sizeof canon, f, sizeof f);
    if (k && strcmp(f, target)) { if (err) snprintf(err, nerr, "\"%s\" is a built-in theme", file); return -1; }
    char own[PATH_MAX];
    int mine = user_file(cur, own, sizeof own);
    lhde_ini *ini = mine ? lhde_ini_load(own) : lhde_ini_new();
    if (!ini) return -1;
    if (!mine) {
        lhde_ini_header(ini, "; a theme of your own (docs/THEME-FORMAT.md), saved by Display Properties: the keys that differ from base");
        lhde_ini_set(ini, "Theme", "base", cur);
    }
    lhde_ini_set(ini, "Theme", "name", title && title[0] ? title : file);
    lhde_ini_set(ini, "Theme", "aliases", NULL);
    struct lhde__layer l;
    lhde__current_layer(cur, &l);
    for (int i = 0; i < l.n; i++) {
        const char *dot = strchr(l.kv[2 * i], '.');
        char sec[64]; snprintf(sec, sizeof sec, "%.*s", (int)(dot - l.kv[2 * i]), l.kv[2 * i]);
        lhde_ini_set(ini, sec, dot + 1, l.kv[2 * i + 1]);
    }
    lhde__layer_free(&l);
    int r = lhde_ini_save(ini, target);
    lhde_ini_free(ini);
    if (r) { if (err) snprintf(err, nerr, "cannot write %s", target); return -1; }
    lhde_current_clear();
    if (lhde_theme_select(file) < 0) { if (err) snprintf(err, nerr, "cannot write %s", lhde_settings_path(f, sizeof f)); return -1; }
    return 0;
}

int lhde_theme_delete(const char *name)
{
    char path[PATH_MAX];
    if (!name || !user_file(name, path, sizeof path)) return 0;
    lhde_ini *ini = lhde_ini_load(path);
    char base[256] = "";
    lhde_ini_get(ini, "Theme", "base", base, sizeof base);
    lhde_ini_free(ini);
    if (unlink(path)) return -1;
    char cur[256];
    selected_theme(cur, sizeof cur);
    if (!strcmp(cur, name)) {
        lhde_current_clear();
        if (base[0]) lhde_theme_select(base);
    }
    return 1;
}

/* ---------------------------------------------------------------- lists */
struct entry { char name[200], title[256], path[PATH_MAX]; };
struct entries { struct entry *e; int n, cap; };
static struct entry *add_entry(struct entries *x, const char *name)
{
    for (int i = 0; i < x->n; i++) if (!strcmp(x->e[i].name, name)) return NULL;   /* the first data directory wins */
    if (x->n == x->cap) { int c = x->cap ? x->cap * 2 : 32; struct entry *ne = realloc(x->e, (size_t)c * sizeof *ne); if (!ne) return NULL; x->e = ne; x->cap = c; }
    struct entry *e = &x->e[x->n++]; memset(e, 0, sizeof *e);
    snprintf(e->name, sizeof e->name, "%s", name);
    return e;
}
static int by_name(const void *a, const void *b) { return strcasecmp(((const struct entry *)a)->name, ((const struct entry *)b)->name); }
static int by_title(const void *a, const void *b) { return strcasecmp(((const struct entry *)a)->title, ((const struct entry *)b)->title); }

int lhde_style_list(void (*fn)(const char *, const char *, void *), void *ud)
{
    struct entries x = { NULL, 0, 0 };
    const char *dirs[LHDE_DATA_MAX]; int nd = lhde_data_dirs(dirs, LHDE_DATA_MAX);
    for (int i = 0; i < nd; i++) {
        char p[PATH_MAX + 16]; snprintf(p, sizeof p, "%s/styles", dirs[i]);
        DIR *d = opendir(p); if (!d) continue;
        struct dirent *de;
        while ((de = readdir(d))) {
            if (de->d_name[0] == '.') continue;
            char nm[200]; snprintf(nm, sizeof nm, "%s", de->d_name);
            size_t l = strlen(nm);
            if (l > 9 && !strcasecmp(nm + l - 9, ".msstyles")) nm[l - 9] = 0;
            else { char q[PATH_MAX * 2]; snprintf(q, sizeof q, "%s/%s", p, de->d_name); if (!lhde__is_dir(q)) continue; }
            size_t nl = strlen(nm);
            if (nl > 3 && !strcmp(nm + nl - 3, "-xp")) continue;   /* the retired XP backports (decision 1) */
            char sp[PATH_MAX]; lhde_style_path(nm, sp, sizeof sp);
            if (!sp[0]) continue;
            struct entry *e = add_entry(&x, nm);
            if (e) snprintf(e->path, sizeof e->path, "%s", sp);
        }
        closedir(d);
    }
    qsort(x.e, (size_t)x.n, sizeof *x.e, by_name);
    for (int i = 0; fn && i < x.n; i++) fn(x.e[i].name, x.e[i].path, ud);
    int n = x.n;
    free(x.e);
    return n;
}

int lhde_scheme_list(void (*fn)(const char *, const char *, const char *, void *), void *ud)
{
    struct entries x = { NULL, 0, 0 };
    const char *dirs[LHDE_DATA_MAX]; int nd = lhde_data_dirs(dirs, LHDE_DATA_MAX);
    for (int i = 0; i < nd; i++) {
        char p[PATH_MAX + 16]; snprintf(p, sizeof p, "%s/styles", dirs[i]);
        DIR *d = opendir(p); if (!d) continue;
        struct dirent *de;
        while ((de = readdir(d))) {
            size_t l = strlen(de->d_name);
            if (de->d_name[0] == '.' || l < 8 || strcmp(de->d_name + l - 7, ".scheme")) continue;
            char nm[200]; snprintf(nm, sizeof nm, "%.*s", (int)(l - 7), de->d_name);
            struct entry *e = add_entry(&x, nm);
            if (!e) continue;
            snprintf(e->path, sizeof e->path, "%s/%s", p, de->d_name);
            lhde_ini *ini = lhde_ini_load(e->path);
            if (!lhde_ini_get(ini, "Scheme", "name", e->title, sizeof e->title) || !e->title[0]) snprintf(e->title, sizeof e->title, "%s", nm);
            lhde_ini_free(ini);
        }
        closedir(d);
    }
    qsort(x.e, (size_t)x.n, sizeof *x.e, by_title);
    for (int i = 0; fn && i < x.n; i++) fn(x.e[i].name, x.e[i].title, x.e[i].path, ud);
    int n = x.n;
    free(x.e);
    return n;
}
