/* liblhde: the settings store and the one-time migration from ~/.config/longhorn + ~/.config/lhshell (lhde/settings.h). */
#define _GNU_SOURCE
#include "lhde/settings.h"
#include "lhde/theme.h"
#include "lhde/data.h"
#include "internal.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

const char *lhde_config_dir(char *out, size_t n)
{
    char c[PATH_MAX]; lhde__config_home(c, sizeof c);
    snprintf(out, n, "%s/lhde", c);
    return out;
}
const char *lhde_state_dir(char *out, size_t n)
{
    const char *x = getenv("XDG_STATE_HOME"), *h = getenv("HOME");
    if (x && *x == '/') snprintf(out, n, "%s/lhde", x); else snprintf(out, n, "%s/.local/state/lhde", h ? h : ".");
    return out;
}
const char *lhde_config_file(const char *name, char *out, size_t n)
{
    char d[PATH_MAX]; lhde_config_dir(d, sizeof d); lhde__mkdirs(d);
    snprintf(out, n, "%s/%s", d, name);
    return out;
}
const char *lhde_state_file(const char *name, char *out, size_t n)
{
    char d[PATH_MAX]; lhde_state_dir(d, sizeof d); lhde__mkdirs(d);
    snprintf(out, n, "%s/%s", d, name);
    return out;
}
const char *lhde_settings_path(char *out, size_t n)
{
    const char *e = getenv("LHDE_SETTINGS");
    if (e && *e) { snprintf(out, n, "%s", e); return out; }
    char d[PATH_MAX]; lhde_config_dir(d, sizeof d);
    snprintf(out, n, "%s/settings.ini", d);
    return out;
}

static lhde_ini *cache;
static char cache_path[PATH_MAX];
static long long cache_mtime = -2; static long long cache_size = -2;
static long long fsize(const char *p) { struct stat st; return stat(p, &st) == 0 ? (long long)st.st_size : -1; }
static lhde_ini *settings(void)
{
    char p[PATH_MAX]; lhde_settings_path(p, sizeof p);
    long long mt = lhde__mtime(p), sz = fsize(p);
    if (cache && !strcmp(p, cache_path) && mt == cache_mtime && sz == cache_size) return cache;
    lhde_ini_free(cache);
    cache = lhde_ini_load(p);
    snprintf(cache_path, sizeof cache_path, "%s", p); cache_mtime = mt; cache_size = sz;
    return cache;
}
void lhde_settings_reload(void) { lhde_ini_free(cache); cache = NULL; }

const char *lhde_settings_get(const char *sec, const char *key, char *buf, size_t n)
{
    const char *v = lhde_ini_get(settings(), sec, key, buf, n);
    return v && v[0] ? v : NULL;
}

static const char HEADER[] =
    "; lhde's settings (docs/SETTINGS.md): the current theme and the per-user keys that are not part of a theme. luxwm, luxshell\n"
    "; and the flxp programs read and write it, keeping keys they do not know; after a write they send _LH_SETTINGS_CHANGED.\n"
    "; Theme keys live in theme files (~/.config/lhde/themes/NAME.theme, docs/THEME-FORMAT.md).";

int lhde_settings_set(const char *sec, const char *key, const char *value)
{
    char p[PATH_MAX]; lhde_settings_path(p, sizeof p);
    lhde_ini *ini = lhde_ini_load(p);
    if (!ini) return -1;
    lhde_ini_header(ini, HEADER);
    int changed = lhde_ini_set(ini, sec, key, value);
    int r = changed ? lhde_ini_save(ini, p) : 0;
    lhde_ini_free(ini);
    lhde_settings_reload();
    return r ? -1 : changed;
}

/* ---------------------------------------------------------------- the legacy [override:NAME] / [override] keys */
/* (the migration turns them into theme files; nothing applies them live) flxp's per-theme keys (docs/SETTINGS.md "Per-theme settings") -> theme keys, values in the new vocabulary; NULL = dropped */
static const char *map_override(const char *k, const char *v, char *key, size_t nk, char *out, size_t no)
{
    out[0] = 0;
    if (!strcasecmp(k, "taskbar")) { snprintf(key, nk, "Shell.taskbar"); snprintf(out, no, "%s", v); }
    else if (!strcasecmp(k, "icons")) { snprintf(key, nk, "Desktop.icons"); snprintf(out, no, "%s", !strcasecmp(v, "longhorn") ? "longhorn4074" : v); }
    else if (!strcasecmp(k, "frame")) { if (!strcasecmp(v, "auto") || !v[0]) return NULL; snprintf(key, nk, "Explorer.chrome"); snprintf(out, no, "%s", v); }
    else if (!strcasecmp(k, "composition")) { snprintf(key, nk, "Glass.composition"); snprintf(out, no, "%s", !strcasecmp(v, "follow") ? "auto" : v); }
    else if (!strcasecmp(k, "animations")) { snprintf(key, nk, "Glass.animations"); snprintf(out, no, "%s", v); }
    else if (!strcasecmp(k, "wallpaper")) { if (!v[0]) return NULL; snprintf(key, nk, "Desktop.wallpaper"); snprintf(out, no, "%s", v); }
    else if (!strcasecmp(k, "mode")) { snprintf(key, nk, "Desktop.mode"); snprintf(out, no, "%s", v); }
    else return NULL;
    return out[0] ? out : NULL;
}

/* ---------------------------------------------------------------- the migration */
struct rep { char *buf; size_t n, len; int count; };
static void say(struct rep *r, const char *fmt, ...)
{
    char line[2048]; va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
    if (r->buf && r->n) { size_t l = strlen(line); if (r->len + l + 2 < r->n) { memcpy(r->buf + r->len, line, l); r->len += l; r->buf[r->len++] = '\n'; r->buf[r->len] = 0; } }
}

/* an old theme name (registry section, alias, preset) -> the theme it is now; out = name when unknown */
static void new_name(const char *old, char *out, size_t n)
{
    char canon[256], file[PATH_MAX];
    if (lhde_theme_lookup(old, canon, sizeof canon, file, sizeof file)) snprintf(out, n, "%s", canon);
    else snprintf(out, n, "%s", old);
}
/* a file name for a preset title ("Aero lotus" -> aero-lotus) */
static void file_name(const char *title, char *out, size_t n)
{
    size_t o = 0; int dash = 0;
    for (const char *p = title; *p && o + 1 < n; p++) {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c) || c == '.' || c == '_') { out[o++] = (char)tolower(c); dash = 0; }
        else if (!dash && o) { out[o++] = '-'; dash = 1; }
    }
    while (o && out[o - 1] == '-') o--;
    out[o] = 0;
    if (!out[0]) snprintf(out, n, "preset");
}
/* data-directory value of a path (/x/lhde/styles/jade4074.msstyles -> jade4074 for kind "styles"); 0 when not one */
static int component_of(const char *path, const char *kind, char *out, size_t n)
{
    const char *p = strstr(path, kind);
    if (!p || (p > path && p[-1] != '/')) return 0;
    p += strlen(kind);
    if (*p != '/') return 0;
    p++;
    char v[256]; snprintf(v, sizeof v, "%s", p);
    size_t l = strlen(v); while (l && v[l - 1] == '/') v[--l] = 0;
    char *dot = strstr(v, ".msstyles"); if (dot) *dot = 0;
    if (!v[0] || strchr(v, '/')) return 0;
    snprintf(out, n, "%s", v);
    return 1;
}

/* write (or extend) USER/NAME.theme with base BASE; keys "Sec.key"; values equal to what the base gives are dropped. Returns the
   number of keys stored (0: no file written) */
static int write_modified(struct rep *r, const char *udir, const char *tname, const char *base, const char *title_hint,
                          char keys[][64], char vals[][1024], int nk, const char *origin)
{
    char path[PATH_MAX + 300]; snprintf(path, sizeof path, "%s/%s.theme", udir, tname);
    lhde_theme *bt = lhde_theme_resolve(base, NULL, 0);
    lhde_ini *ini = lhde_ini_load(path);
    int fresh = !lhde__exists(path), stored = 0;
    for (int i = 0; i < nk; i++) {
        const char *bv = bt ? lhde_theme_get(bt, keys[i]) : NULL;
        if (bv && !strcmp(bv, vals[i])) { say(r, "  %s: %s = %s is what %s gives already: dropped", origin, keys[i], vals[i], base); continue; }
        char sec[64]; const char *dot = strchr(keys[i], '.'); snprintf(sec, sizeof sec, "%.*s", (int)(dot - keys[i]), keys[i]);
        char have[1024];
        if (!fresh && lhde_ini_get(ini, sec, dot + 1, have, sizeof have)) continue;   /* the user's file already says */
        lhde_ini_set(ini, sec, dot + 1, vals[i]); stored++;
    }
    if (stored) {
        if (fresh) {
            lhde_ini *h = lhde_ini_new();
            char hdr[600]; snprintf(hdr, sizeof hdr, "; %s with the user's changes, migrated from %s (docs/SETTINGS.md \"Migration\")", base, origin);
            lhde_ini_header(h, hdr);
            const char *title = bt ? lhde_theme_get(bt, "Theme.name") : NULL;
            char tt[600]; snprintf(tt, sizeof tt, "%s (modified)", title_hint ? title_hint : title ? title : base);
            lhde_ini_set(h, "Theme", "name", tt); lhde_ini_set(h, "Theme", "base", base);
            char *body = lhde_ini_text(ini); char *head = lhde_ini_text(h);
            size_t L = strlen(head) + strlen(body) + 4; char *all = malloc(L);
            if (all) { snprintf(all, L, "%s\n%s", head, body); lhde__write_file(path, all); free(all); }
            free(body); free(head); lhde_ini_free(h);
        } else lhde_ini_save(ini, path);
        say(r, "  %s -> %s (%d key%s, base %s)", origin, path, stored, stored == 1 ? "" : "s", base);
    }
    lhde_ini_free(ini); lhde_theme_free(bt);
    return stored;
}

struct kv { char (*k)[64]; char (*v)[1024]; int n, max; const char *sec; };
static void kv_add(const char *k, const char *v, void *ud)
{
    struct kv *c = ud;
    char key[64], val[1024];
    if (!map_override(k, v, key, sizeof key, val, sizeof val) || c->n >= c->max) return;
    snprintf(c->k[c->n], 64, "%s", key); snprintf(c->v[c->n], 1024, "%s", val); c->n++;
}
struct secs { char names[64][300]; int n; };
static void sec_add(const char *s, void *ud) { struct secs *x = ud; if (x->n < 64) snprintf(x->names[x->n++], 300, "%s", s); }

/* an old preset ([Theme] [VisualStyle] [Composition] [Desktop] [Frames] [Shell] [Fonts] [Explorer]) in the new format */
static const char *pack_alias(const char *v)
{
    static const char *const m[][2] = { { "aero", "4074-aero" }, { "slate", "4074-slate" }, { "slate4051", "4051-slate" },
                                        { "aero5112", "5112-aero" }, { "aerovision", "4074-aerovision" }, { NULL, NULL } };
    for (int i = 0; m[i][0]; i++) if (!strcmp(m[i][0], v)) return m[i][1];
    return v;
}
static int migrate_preset(struct rep *r, const char *src, const char *udir, char *newname, size_t nn)
{
    lhde_ini *o = lhde_ini_load(src);
    char stem[256]; { const char *b = strrchr(src, '/'); snprintf(stem, sizeof stem, "%s", b ? b + 1 : src); char *d = strstr(stem, ".theme"); if (d) *d = 0; }
    char title[256], base[256] = "", v[1024], tn[256];
    if (!lhde_ini_get(o, "Theme", "name", title, sizeof title) || !title[0]) snprintf(title, sizeof title, "%s", stem);
    lhde_ini_get(o, "Theme", "base", base, sizeof base);
    file_name(stem, tn, sizeof tn);
    char canon[256], f[PATH_MAX];
    if (lhde_theme_lookup(tn, canon, sizeof canon, f, sizeof f) && strncmp(f, udir, strlen(udir))) { size_t l = strlen(tn); snprintf(tn + l, sizeof tn - l, "-custom"); }
    snprintf(newname, nn, "%s", tn);
    char dst[PATH_MAX + 300]; snprintf(dst, sizeof dst, "%s/%s.theme", udir, tn);
    if (lhde__exists(dst)) { say(r, "  preset %s: %s exists, kept", src, dst); lhde_ini_free(o); return 0; }
    lhde_ini *n = lhde_ini_new();
    char hdr[PATH_MAX + 200]; snprintf(hdr, sizeof hdr, "; migrated from %s (docs/SETTINGS.md \"Migration\")", src);
    lhde_ini_header(n, hdr);
    lhde_ini_set(n, "Theme", "name", title);
    char nb[256]; new_name(base[0] ? base : "aero4074", nb, sizeof nb);
    lhde_ini_set(n, "Theme", "base", nb);
    char c[256];
    if (lhde_ini_get(o, "VisualStyle", "msstyles", v, sizeof v) && v[0]) lhde_ini_set(n, "VisualStyle", "style", component_of(v, "styles", c, sizeof c) ? c : v);
    if (lhde_ini_get(o, "VisualStyle", "color", v, sizeof v) && v[0]) lhde_ini_set(n, "VisualStyle", "color", v);
    if (lhde_ini_get(o, "VisualStyle", "size", v, sizeof v) && v[0]) lhde_ini_set(n, "VisualStyle", "size", v);
    if (lhde_ini_get(o, "Frames", "frame", v, sizeof v) && v[0]) lhde_ini_set(n, "Glass", "frames", component_of(v, "frames", c, sizeof c) ? c : v);
    if (lhde_ini_get(o, "Frames", "frame_classic", v, sizeof v) && v[0]) lhde_ini_set(n, "Glass", "frames_classic", !strcmp(v, "style") ? "none" : component_of(v, "frames", c, sizeof c) ? c : v);
    if (lhde_ini_get(o, "Composition", "composition", v, sizeof v) && v[0] && strcmp(v, "follow") && strcmp(v, "auto"))
        lhde_ini_set(n, "Glass", "composition", !strcmp(v, "0") ? "off" : v);
    if (lhde_ini_get(o, "Composition", "animations", v, sizeof v) && v[0]) lhde_ini_set(n, "Glass", "animations", v);
    if (lhde_ini_get(o, "Shell", "shell", v, sizeof v) && v[0]) lhde_ini_set(n, "Shell", "pack", component_of(v, "packs", c, sizeof c) ? pack_alias(c) : pack_alias(v));
    if (lhde_ini_get(o, "Shell", "taskbar", v, sizeof v) && v[0]) lhde_ini_set(n, "Shell", "taskbar", v);
    if (lhde_ini_get(o, "Explorer", "frame", v, sizeof v) && v[0] && strcmp(v, "auto")) lhde_ini_set(n, "Explorer", "chrome", v);
    if (lhde_ini_get(o, "Desktop", "wallpaper", v, sizeof v) && v[0]) {
        char abs[PATH_MAX];
        if (v[0] != '/' && strcmp(v, "none") && !(v[0] == '~' && v[1] == '/')) {   /* relative to the old preset directory */
            char d[PATH_MAX]; snprintf(d, sizeof d, "%s", src); char *sl = strrchr(d, '/'); if (sl) *sl = 0;
            snprintf(abs, sizeof abs, "%.2000s/%.2000s", d, v);
        } else snprintf(abs, sizeof abs, "%s", v);
        lhde_ini_set(n, "Desktop", "wallpaper", abs);
    }
    if (lhde_ini_get(o, "Desktop", "mode", v, sizeof v) && v[0]) lhde_ini_set(n, "Desktop", "mode", v);
    if (lhde_ini_get(o, "Desktop", "icons", v, sizeof v) && v[0]) lhde_ini_set(n, "Desktop", "icons", !strcmp(v, "longhorn") ? "longhorn4074" : v);
    if (lhde_ini_get(o, "Fonts", "fonts", v, sizeof v) && v[0]) lhde_ini_set(n, "Fonts", "fonts", component_of(v, "fonts", c, sizeof c) ? c : v);
    int ok = lhde_ini_save(n, dst) == 0;
    say(r, "  preset %s -> %s (\"%s\", base %s)", src, dst, title, nb);
    lhde_ini_free(n); lhde_ini_free(o);
    return ok;
}

/* lux's rules for state.ini (docs/SETTINGS.md "lux's migration from ~/.config/lhshell/state.ini"): [sec] key -> settings */
static const char *const STATE_RULES[][4] = {
    { "shell", "theme", "theme", "name" }, { "shell", "composition", "", "" }, { "shell", "wallpaper", "desktop", "wallpaper" },
    { "shell", "desktop_black", "desktop", "desktop_black" }, { "taskbar", "enabled", "", "" }, { "taskbar", "autohide", "taskbar", "autohide" },
    { "taskbar", "rows", "taskbar", "rows" }, { "taskbar", "classic_start_menu", "taskbar", "classic_start_menu" },
    { "sidebar", "mode", "sidebar", "mode" }, { "sidebar", "width", "sidebar", "width" }, { "sidebar", "with_classic", "sidebar", "with_classic" },
    { "sidebar", "yield_bottom", "sidebar", "yield_bottom" }, { "sidebar", "stacking", "sidebar", "stacking" }, { "sidebar", "layer", "sidebar", "stacking" },
    { "clock", "pinned", "clock", "pinned" }, { "clock", "pinned_pack", "clock", "pinned_pack" }, { "clock", "layout", "clock", "layout" },
    { "start", "pinned", "start", "pinned" }, { "start", "mfu_count", "start", "mfu_count" }, { "start", "userpane", "start", "userpane" },
    { "commands", "*", "commands", "*" }, { NULL, NULL, NULL, NULL } };
struct stctx { lhde_ini *st, *dst; struct rep *r; const char *sec; char drop[64][2][64]; int nd; int moved; };
static void st_key(const char *k, const char *v, void *ud)
{
    struct stctx *c = ud;
    for (int i = 0; STATE_RULES[i][0]; i++) {
        if (strcasecmp(STATE_RULES[i][0], c->sec) || (strcmp(STATE_RULES[i][1], "*") && strcasecmp(STATE_RULES[i][1], k))) continue;
        if (c->nd < 64) { snprintf(c->drop[c->nd][0], 64, "%s", c->sec); snprintf(c->drop[c->nd][1], 64, "%s", k); c->nd++; }
        const char *ds = STATE_RULES[i][2], *dk = !strcmp(STATE_RULES[i][3], "*") ? k : STATE_RULES[i][3];
        if (!ds[0]) { say(c->r, "  state.ini [%s] %s = %s: per theme now, dropped", c->sec, k, v); return; }
        char val[1024]; snprintf(val, sizeof val, "%s", v);
        if (!strcmp(c->sec, "shell") && !strcmp(k, "wallpaper")) { if (strcmp(v, "0")) return; snprintf(val, sizeof val, "none"); }
        if (!strcmp(k, "layer")) { if (strcmp(v, "below")) return; snprintf(val, sizeof val, "bottom"); }
        if (!strcmp(c->sec, "shell") && !strcmp(k, "theme")) new_name(v, val, sizeof val);
        char have[1024];
        if (lhde_ini_get(c->dst, ds, dk, have, sizeof have)) return;   /* settings.ini has it */
        lhde_ini_set(c->dst, ds, dk, val); c->moved++;
        say(c->r, "  state.ini [%s] %s = %s -> settings.ini [%s] %s", c->sec, k, v, ds, dk);
    }
}

/* settings.ini's legacy per-theme layers, which nothing applies live any more: [override:NAME] -> NAME-modified.theme (and
   [theme] name follows when NAME was selected), [override] (all themes) -> the selected theme's modified copy, [desktop]
   wallpaper = PATH (+ mode; the old "Set as Desktop Background", for every theme) -> the selected theme's current layer.
   [desktop] wallpaper = none stays: the per-user switch the resolver applies. Returns the number of changes. */
static int convert_legacy(struct rep *r, lhde_ini *dst, const char *udir, char *selected, size_t nsel)
{
    int count = 0;
    char v[1024];
    struct secs ss; ss.n = 0;
    lhde_ini_sections(dst, sec_add, &ss);
    static char keys[32][64], vals[32][1024];
    for (int i = 0; i < ss.n; i++) {
        if (strncasecmp(ss.names[i], "override:", 9)) continue;
        const char *old = ss.names[i] + 9;
        char canon[256]; new_name(old, canon, sizeof canon);
        struct kv c = { keys, vals, 0, 32, ss.names[i] };
        lhde_ini_keys(dst, ss.names[i], kv_add, &c);
        char tname[300]; snprintf(tname, sizeof tname, "%s-modified", canon);
        char origin[400]; snprintf(origin, sizeof origin, "settings.ini [%s]", ss.names[i]);
        int stored = c.n ? write_modified(r, udir, tname, canon, NULL, keys, vals, c.n, origin) : 0;
        if (!stored) say(r, "  %s: nothing that differs from %s, no theme file", origin, canon);
        lhde_ini_remove_section(dst, ss.names[i]); count++;
        if (stored && !strcmp(selected, canon)) { lhde_ini_set(dst, "theme", "name", tname); say(r, "  [theme] name = %s -> %s", canon, tname); snprintf(selected, nsel, "%s", tname); }
    }
    if (lhde_ini_has_section(dst, "override")) {
        struct kv c = { keys, vals, 0, 32, "override" };
        lhde_ini_keys(dst, "override", kv_add, &c);
        if (c.n && selected[0]) {
            size_t sl = strlen(selected);
            char tname[300]; if (sl > 9 && !strcmp(selected + sl - 9, "-modified")) snprintf(tname, sizeof tname, "%s", selected); else snprintf(tname, sizeof tname, "%s-modified", selected);
            char base[256]; snprintf(base, sizeof base, "%s", selected); if (!strcmp(tname, selected)) { char *m = strstr(base, "-modified"); if (m) *m = 0; }
            if (write_modified(r, udir, tname, base, NULL, keys, vals, c.n, "settings.ini [override] (all themes; now the selected theme's)") && strcmp(selected, tname)) {
                lhde_ini_set(dst, "theme", "name", tname); snprintf(selected, nsel, "%s", tname); }
        }
        lhde_ini_remove_section(dst, "override"); count++;
        say(r, "  [override] removed (no all-themes layer any more; other themes keep their own values)");
    }
    if (lhde_ini_get(dst, "desktop", "wallpaper", v, sizeof v) && v[0] && strcmp(v, "none")) {
        char m[64] = "", owner[256] = "", have[1024], canon[256] = "", file[PATH_MAX];
        lhde_ini_get(dst, "desktop", "mode", m, sizeof m);
        if (selected[0] && lhde_theme_lookup(selected, canon, sizeof canon, file, sizeof file) <= 0) snprintf(canon, sizeof canon, "%s", selected);
        selected = canon;   /* the current layer belongs to the canonical name */
        lhde_ini_get(dst, "current", "theme", owner, sizeof owner);
        if (!selected[0]) say(r, "  [desktop] wallpaper = %s removed: no theme is selected to carry it", v);
        else if (owner[0] && strcmp(owner, selected)) say(r, "  [desktop] wallpaper = %s removed: the current layer belongs to %s, not %s", v, owner, selected);
        else if (lhde_ini_get(dst, "current", "Desktop.wallpaper", have, sizeof have)) say(r, "  [desktop] wallpaper = %s removed: the current layer has %s", v, have);
        else {
            lhde_ini_set(dst, "current", "theme", selected);
            lhde_ini_set(dst, "current", "Desktop.wallpaper", v);
            if (m[0] && !lhde_ini_get(dst, "current", "Desktop.mode", have, sizeof have)) lhde_ini_set(dst, "current", "Desktop.mode", m);
            say(r, "  [desktop] wallpaper = %s%s%s -> [current] of %s (Desktop.wallpaper%s)", v, m[0] ? ", mode = " : "", m, selected, m[0] ? ", Desktop.mode" : "");
        }
        lhde_ini_set(dst, "desktop", "wallpaper", NULL); lhde_ini_set(dst, "desktop", "mode", NULL); count++;
    }
    return count;
}

static int migrate_settings(int force, struct rep *r)
{
    char ch[PATH_MAX], newp[PATH_MAX], cdir[PATH_MAX], udir[PATH_MAX], oldp[PATH_MAX + 64], olddir[PATH_MAX + 64], state[PATH_MAX + 64];
    lhde__config_home(ch, sizeof ch);
    lhde_settings_path(newp, sizeof newp);
    lhde_config_dir(cdir, sizeof cdir);
    lhde_theme_user_dir(udir, sizeof udir);
    snprintf(oldp, sizeof oldp, "%s/longhorn/settings.ini", ch);
    snprintf(olddir, sizeof olddir, "%s/longhorn/themes", ch);
    snprintf(state, sizeof state, "%s/lhshell/state.ini", ch);
    struct stat lst;
    int old_is_link = lstat(oldp, &lst) == 0 && S_ISLNK(lst.st_mode);
    if (lhde__exists(newp) && !force) return 0;
    if (!lhde__exists(oldp) && !lhde__is_dir(olddir) && !lhde__exists(state)) return 0;   /* nothing of the old layout */
    if (lhde__mkdirs(udir)) { say(r, "cannot create %s: %s", udir, strerror(errno)); return -1; }
    say(r, "lhde: migrating %s/{longhorn,lhshell} to %s", ch, cdir);
    lhde_ini *dst = lhde__exists(newp) ? lhde_ini_load(newp) : (!old_is_link && lhde__exists(oldp) ? lhde_ini_load(oldp) : lhde_ini_new());
    if (!dst) return -1;
    int count = 0;
    char v[1024];
    /* 1. the presets: longhorn/themes/NAME.theme -> lhde/themes/<file name>.theme (old name -> new name for [theme] name) */
    char ren[64][2][256]; int nren = 0;
    DIR *d = opendir(olddir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            size_t l = strlen(e->d_name);
            if (l < 7 || strcmp(e->d_name + l - 6, ".theme")) continue;
            char src[PATH_MAX + 300], nn[256]; snprintf(src, sizeof src, "%s/%s", olddir, e->d_name);
            if (migrate_preset(r, src, udir, nn, sizeof nn)) count++;
            if (nren < 64) { snprintf(ren[nren][0], 256, "%.*s", (int)(l - 6), e->d_name); snprintf(ren[nren][1], 256, "%s", nn); nren++; }
        }
        closedir(d);
    }
    /* 2. [theme] name: an old name -> its theme (aliases), a preset -> its new file name */
    char selected[256] = "";
    if (lhde_ini_get(dst, "theme", "name", v, sizeof v) && v[0]) {
        char nv[256] = "";
        for (int i = 0; i < nren; i++) if (!strcmp(ren[i][0], v)) snprintf(nv, sizeof nv, "%s", ren[i][1]);
        if (!nv[0]) new_name(v, nv, sizeof nv);
        if (strcmp(nv, v)) { lhde_ini_set(dst, "theme", "name", nv); say(r, "  [theme] name = %s -> %s", v, nv); count++; }
        snprintf(selected, sizeof selected, "%s", nv);
    }
    /* 3. [override:NAME] -> NAME-modified.theme; [override] (all themes) -> the selected theme's; [desktop] wallpaper */
    count += convert_legacy(r, dst, udir, selected, sizeof selected);
    /* 4. the global fallbacks of per-theme keys: the theme decides now */
    static const char *const gone[][3] = { { "theme", "composition", "the theme's Glass.composition" }, { "theme", "animations", "the theme's Glass.animations" },
        { "taskbar", "kind", "the theme's Shell.taskbar" }, { "start", "menu", "there is one start menu, flxp's" }, { "desktop", "icons", "the theme's Desktop.icons" },
        { "explorer", "frame", "the theme's Explorer.chrome" }, { NULL, NULL, NULL } };
    for (int i = 0; gone[i][0]; i++) {
        if (!lhde_ini_get(dst, gone[i][0], gone[i][1], v, sizeof v)) continue;
        /* a value that differs from what the selected theme gives goes into its modified copy */
        if (selected[0] && v[0] && strcmp(v, "follow")) {
            char key[64], val[1024];
            const char *lk = !strcmp(gone[i][1], "kind") ? "taskbar" : !strcmp(gone[i][1], "menu") ? "start" : gone[i][1];
            if (map_override(lk, v, key, sizeof key, val, sizeof val)) {
                lhde_theme *st = lhde_theme_resolve(selected, NULL, 0);
                const char *have = st ? lhde_theme_get(st, key) : NULL;
                if (have && strcmp(have, val)) say(r, "  [%s] %s = %s differs from %s's %s = %s: the theme's value wins now (set it in the theme to keep it)",
                                                   gone[i][0], gone[i][1], v, selected, key, have);
                lhde_theme_free(st);
            }
        }
        lhde_ini_set(dst, gone[i][0], gone[i][1], NULL); count++;
        say(r, "  [%s] %s = %s removed: %s", gone[i][0], gone[i][1], v, gone[i][2]);
    }
    if (lhde_ini_get(dst, "desktop", "wallpaper", v, sizeof v) && !v[0]) { lhde_ini_set(dst, "desktop", "wallpaper", NULL); say(r, "  [desktop] wallpaper (empty) removed: the theme's"); }
    /* 5. state.ini: user-facing keys left there by a lux from before the settings store */
    if (lhde__exists(state)) {
        lhde_ini *st = lhde_ini_load(state);
        struct stctx c; memset(&c, 0, sizeof c); c.st = st; c.dst = dst; c.r = r;
        struct secs s2; s2.n = 0; lhde_ini_sections(st, sec_add, &s2);
        for (int i = 0; i < s2.n; i++) { c.sec = s2.names[i]; lhde_ini_keys(st, s2.names[i], st_key, &c); }
        if (c.nd) {
            char bak[PATH_MAX + 64]; snprintf(bak, sizeof bak, "%s.pre-lhde", state);
            if (!lhde__exists(bak)) { char *t = lhde_ini_text(st); if (t) { lhde__write_file(bak, t); free(t); } }
            for (int i = 0; i < c.nd; i++) lhde_ini_set(st, c.drop[i][0], c.drop[i][1], NULL);
            lhde_ini_save(st, state);
            count += c.moved;
        } else say(r, "  %s: runtime state only, unchanged", state);
        lhde_ini_free(st);
    }
    /* 6. the header, then the file; the old path becomes a symlink to it */
    {
        char *t = lhde_ini_text(dst);
        int fresh_header = t && strncmp(t, "; lhde's settings", 17);
        lhde_ini *out = lhde_ini_new();
        if (fresh_header) {
            char when[64]; time_t now = time(NULL); strftime(when, sizeof when, "%Y-%m-%d %H:%M", localtime(&now));
            char hdr[2048]; snprintf(hdr, sizeof hdr, "%s\n; Migrated from %s on %s (the original: settings.ini.pre-lhde there).", HEADER, oldp, when);
            lhde_ini_header(out, hdr);
        }
        char *h = lhde_ini_text(out);
        /* drop the x11-era header comment lines of the old file (they name x11/SETTINGS.md) */
        char *body = t ? t : strdup("");
        if (fresh_header) while (body && body[0] == ';') { char *nl = strchr(body, '\n'); if (!nl) break; if (!strstr(body, "SETTINGS.md") && !strstr(body, "_LH_SETTINGS_CHANGED")) break; body = nl + 1; }
        while (body && *body == '\n') body++;
        size_t L = strlen(h) + strlen(body ? body : "") + 4;
        char *all = malloc(L);
        if (all) {
            snprintf(all, L, "%s%s%s", h, h[0] ? "\n" : "", body ? body : "");
            if (lhde__write_file(newp, all)) { say(r, "cannot write %s: %s", newp, strerror(errno)); free(all); free(h); free(t); lhde_ini_free(out); lhde_ini_free(dst); return -1; }
            free(all);
        }
        free(h); free(t); lhde_ini_free(out);
    }
    lhde_ini_free(dst);
    say(r, "  wrote %s", newp);
    if (lhde__exists(oldp) && !old_is_link) {
        char bak[PATH_MAX + 64]; snprintf(bak, sizeof bak, "%s.pre-lhde", oldp);
        if (!lhde__exists(bak)) rename(oldp, bak); else unlink(oldp);
        say(r, "  %s kept as %s", oldp, bak);
    }
    lhde_settings_reload();
    return count ? count : 1;
}

/* ---- every run: legacy layers in lhde's own settings.ini (written there by a program from before the switch, or left by a
   settings.ini that was migrated before they stopped being applied live) ---- */
static int migrate_legacy(struct rep *r)
{
    char newp[PATH_MAX], udir[PATH_MAX], v[1024];
    lhde_settings_path(newp, sizeof newp);
    if (!lhde__exists(newp)) return 0;
    lhde_ini *ini = lhde_ini_load(newp);
    if (!ini) return 0;
    struct secs ss; ss.n = 0; lhde_ini_sections(ini, sec_add, &ss);
    int any = lhde_ini_get(ini, "desktop", "wallpaper", v, sizeof v) && v[0] && strcmp(v, "none");
    for (int i = 0; i < ss.n && !any; i++) if (!strncasecmp(ss.names[i], "override", 8) && (!ss.names[i][8] || ss.names[i][8] == ':')) any = 1;
    if (!any) { lhde_ini_free(ini); return 0; }
    lhde_theme_user_dir(udir, sizeof udir);
    if (lhde__mkdirs(udir)) { say(r, "cannot create %s: %s", udir, strerror(errno)); lhde_ini_free(ini); return -1; }
    say(r, "lhde: %s: legacy per-theme sections -> theme files", newp);
    char selected[256] = "";
    lhde_ini_get(ini, "theme", "name", selected, sizeof selected);
    int n = convert_legacy(r, ini, udir, selected, sizeof selected);
    if (lhde_ini_save(ini, newp)) { say(r, "cannot write %s: %s", newp, strerror(errno)); lhde_ini_free(ini); return -1; }
    lhde_ini_free(ini);
    lhde_settings_reload();
    return n;
}

/* ---- the files that were scattered over ~/.config/{flxp,lhshell,lhwm,longhorn}: into lhde's config and state directories ---- */
static int move_file(const char *src, const char *dst)
{
    char d[PATH_MAX]; snprintf(d, sizeof d, "%s", dst); char *sl = strrchr(d, '/'); if (sl) { *sl = 0; lhde__mkdirs(d); }
    if (rename(src, dst) == 0) return 0;
    if (errno != EXDEV) return -1;
    FILE *in = fopen(src, "rb"); if (!in) return -1;
    char tmp[PATH_MAX + 8]; snprintf(tmp, sizeof tmp, "%s.tmp", dst);
    FILE *out = fopen(tmp, "wb"); if (!out) { fclose(in); return -1; }
    char buf[65536]; size_t k; int bad = 0;
    while ((k = fread(buf, 1, sizeof buf, in)) > 0) if (fwrite(buf, 1, k, out) != k) { bad = 1; break; }
    fclose(in); if (fclose(out) || bad || rename(tmp, dst)) { unlink(tmp); return -1; }
    unlink(src); return 0;
}
/* one file: moved when the source exists and the destination does not; the source left alone when both exist (reported) */
static int move_one(struct rep *r, const char *src, const char *dst, int *count)
{
    if (!lhde__exists(src)) return 0;
    if (lhde__exists(dst)) { say(r, "  %s: left, %s exists already", src, dst); return 0; }
    if (move_file(src, dst)) { say(r, "  cannot move %s to %s: %s", src, dst, strerror(errno)); return -1; }
    say(r, "  %s -> %s", src, dst); (*count)++;
    return 1;
}
/* flxp.ini keys that are not flxp's to keep: the ones that duplicated theme keys (the theme decides; FLXP_<KEY> in the
   environment still counts for one run) and the path overrides that the resolver's imported Windows tree makes redundant */
static void prune_flxp_ini(struct rep *r, const char *path)
{
    static const char *const gone[][2] = {
        { "icons", "the theme's Desktop.icons" }, { "frame", "the theme's Explorer.chrome" }, { "taskbar_kind", "the theme's Shell.taskbar" },
        { "wallpaper", "the theme's Desktop.wallpaper" }, { "wallpaper_mode", "the theme's Desktop.mode" }, { "themes_ini", "the theme files" },
        { "nt5src", "XP's art is read by resource id from the imported resources/xp" },
        { "font_path", "the theme's fonts (imported fonts/<build>, then fontconfig)" }, { "longhorn_system32", "the theme's resources/<build>" },
        { "longhorn_wim", "the imported resources/<build>" }, { "explorer_resources", "the imported resources/<build>" },
        { "theme_path", "styles come from the Windows tree (lhde-import --style FILE NAME drops one in)" }, { NULL, NULL } };
    lhde_ini *ini = lhde_ini_load(path); if (!ini) return;
    int changed = 0; char v[1024];
    for (int i = 0; gone[i][0]; i++) {
        if (!lhde_ini_get(ini, "flxp", gone[i][0], v, sizeof v)) continue;
        if (v[0]) {
            char env[64]; snprintf(env, sizeof env, "%s", gone[i][0]); for (char *c = env; *c; c++) *c = toupper((unsigned char)*c);
            say(r, "    %s = %s removed: %s (FLXP_%s in the environment for one run)", gone[i][0], v, gone[i][1], env);
        }
        lhde_ini_set(ini, "flxp", gone[i][0], NULL); changed = 1;
    }
    if (changed) lhde_ini_save(ini, path);
    lhde_ini_free(ini);
}
static int migrate_files(struct rep *r)
{
    char ch[PATH_MAX], cdir[PATH_MAX], sdir[PATH_MAX], src[PATH_MAX + 64], dst[PATH_MAX + 64];
    lhde__config_home(ch, sizeof ch); lhde_config_dir(cdir, sizeof cdir); lhde_state_dir(sdir, sizeof sdir);
    int count = 0, rc;
    snprintf(src, sizeof src, "%s/flxp/flxp.ini", ch); snprintf(dst, sizeof dst, "%s/flxp.ini", cdir);
    if ((rc = move_one(r, src, dst, &count)) > 0) prune_flxp_ini(r, dst);
    snprintf(src, sizeof src, "%s/flxp/views", ch); snprintf(dst, sizeof dst, "%s/flxp-views", sdir); move_one(r, src, dst, &count);
    snprintf(src, sizeof src, "%s/flxp/desktop-positions", ch); snprintf(dst, sizeof dst, "%s/flxp-desktop-positions", sdir); move_one(r, src, dst, &count);
    snprintf(src, sizeof src, "%s/lhshell/state.ini", ch); snprintf(dst, sizeof dst, "%s/luxshell-state.ini", sdir); move_one(r, src, dst, &count);
    { char dh[PATH_MAX]; const char *x = getenv("XDG_DATA_HOME"), *hm = getenv("HOME");   /* flxp's start panel kept two files under ~/.local/share/flxp */
      if (x && *x == '/') snprintf(dh, sizeof dh, "%s", x); else snprintf(dh, sizeof dh, "%s/.local/share", hm ? hm : ".");
      snprintf(src, sizeof src, "%s/flxp/mfu", dh); snprintf(dst, sizeof dst, "%s/flxp-mfu", sdir); move_one(r, src, dst, &count);
      snprintf(src, sizeof src, "%s/flxp/run-history", ch); snprintf(dst, sizeof dst, "%s/flxp-run-history", sdir); move_one(r, src, dst, &count);
      snprintf(src, sizeof src, "%s/flxp", dh); if (lhde__is_dir(src) && rmdir(src) == 0) say(r, "  %s (empty) removed", src); }
    snprintf(src, sizeof src, "%s/lhshell/mfu.ini", ch); snprintf(dst, sizeof dst, "%s/luxshell-mfu.ini", sdir); move_one(r, src, dst, &count);
    snprintf(src, sizeof src, "%s/lhwm/keys", ch); snprintf(dst, sizeof dst, "%s/keys", cdir); move_one(r, src, dst, &count);
    snprintf(src, sizeof src, "%s/longhorn/settings.ini", ch);
    { struct stat st; if (lstat(src, &st) == 0 && S_ISLNK(st.st_mode) && unlink(src) == 0) { say(r, "  %s (a symlink to lhde's; nothing reads it) removed", src); count++; } }
    static const char *const olddirs[] = { "flxp", "lhshell", "lhwm", "longhorn", NULL };
    for (int i = 0; olddirs[i]; i++) {
        snprintf(src, sizeof src, "%s/%s", ch, olddirs[i]);
        if (lhde__is_dir(src) && rmdir(src) == 0) { say(r, "  %s (empty) removed", src); count++; }
        else if (lhde__is_dir(src) && count) say(r, "  %s left: it still holds files (backups of the earlier migrations, or something that is not lhde's)", src);
    }
    return count;
}

int lhde_settings_migrate(int force, char *report, size_t n)
{
    struct rep r = { report, n, 0, 0 };
    if (report && n) report[0] = 0;
    int a = migrate_settings(force, &r);
    if (a < 0) return -1;
    int l = migrate_legacy(&r);
    if (l < 0) return -1;
    a += l;
    int b = migrate_files(&r);
    if (b < 0) return -1;
    if (report && report[0]) {   /* the report is kept beside the settings, appended */
        char cdir[PATH_MAX], lp[PATH_MAX + 32]; lhde_config_dir(cdir, sizeof cdir); lhde__mkdirs(cdir);
        snprintf(lp, sizeof lp, "%s/migration.log", cdir);
        FILE *f = fopen(lp, "a"); if (f) { fputs(report, f); if (report[strlen(report) - 1] != '\n') fputc('\n', f); fclose(f); }
    }
    return a + b;
}
