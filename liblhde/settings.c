/* liblhde: the settings store and the user's two directories (lhde/settings.h). */
#define _GNU_SOURCE
#include "lhde/settings.h"
#include "internal.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

/* NAME is an executable file in a directory of $PATH */
static int on_path(const char *name)
{
    const char *p = getenv("PATH");
    char buf[8192]; snprintf(buf, sizeof buf, "%s", p && p[0] ? p : "/usr/local/bin:/usr/bin:/bin");
    for (char *sv, *d = strtok_r(buf, ":", &sv); d; d = strtok_r(NULL, ":", &sv)) {
        char f[PATH_MAX]; snprintf(f, sizeof f, "%s/%s", d[0] ? d : ".", name);
        if (access(f, X_OK) == 0) return 1;
    }
    return 0;
}

/* the default order: cmd first (a user's own console-looking terminal script, ~/bin/cmd), then the terminals luxwm's keys
   and lab.sh name */
const char *lhde_terminal_command(char *out, size_t n)
{
    if (lhde_settings_get("commands", "terminal", out, n)) return out;
    static const char *const order[] = { "cmd", "urxvt", "xterm", "x-terminal-emulator", NULL };
    for (int i = 0; order[i]; i++) if (on_path(order[i])) { snprintf(out, n, "%s", order[i]); return out; }
    snprintf(out, n, "xterm");
    return out;
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
