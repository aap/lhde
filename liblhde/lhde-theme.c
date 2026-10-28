/* lhde-theme: the theme files from the command line (liblhde; docs/THEME-FORMAT.md, docs/USAGE.md "lhde-theme").
     lhde-theme list                       every theme: the user's, the shipped ones, the bare styles (name, group, title)
     lhde-theme resolve NAME [--why]       the resolved record, one SECTION.KEY=VALUE per line; --why adds where each came from
     lhde-theme resolve NAME --get KEY     one value (scripts: lhde-session, lhwall.sh)
     lhde-theme resolve NAME --shell       the record as shell assignments (lhde_Glass_frames='...')
     lhde-theme current                    the selected theme (settings.ini [theme] name)
     lhde-theme set NAME                   select a theme: settings.ini [theme] name, then the root _LH_THEME when $DISPLAY has
                                           an X server (xprop); luxwm, luxshell and flxp follow the property
     lhde-theme set-key NAME KEY VALUE     a theme key changed the way Display Properties and luxshell's flyout do it: into
                                           NAME-modified.theme (or the user's own NAME.theme); VALUE "-" removes the key
     lhde-theme style NAME                 what liblhde reads from a visual style (kind, format, build, family, first scheme)
     lhde-theme setting SEC KEY [VALUE]    a per-user key of settings.ini: print it, or write it (VALUE "-" removes it)
     lhde-theme migrate [--force]          the one-time move from ~/.config/longhorn and ~/.config/lhshell (docs/SETTINGS.md)
     lhde-theme registry                   (re)write the compatibility themes.ini for flxp and print its path
     lhde-theme dirs                       the data directories, the settings file, the user's theme directory */
#define _GNU_SOURCE
#include "lhde/data.h"
#include "lhde/theme.h"
#include "lhde/settings.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(void)
{
    fprintf(stderr, "usage: lhde-theme list | resolve NAME [--why | --get KEY | --shell] | current | set NAME |\n"
                    "                  set-key NAME KEY VALUE | setting SEC KEY [VALUE] | style NAME | migrate [--force] |\n"
                    "                  registry | dirs\n");
    exit(2);
}

static void list_one(const char *name, const char *title, const char *file, int group, void *ud)
{
    (void)file; (void)ud;
    static const char *g[] = { "user", "shipped", "style" };
    printf("%-22s %-8s %s\n", name, g[group < 3 ? group : 2], title);
}

static void shq(const char *s)
{
    putchar('\'');
    for (; *s; s++) { if (*s == '\'') fputs("'\\''", stdout); else putchar(*s); }
    putchar('\'');
}

int main(int argc, char **argv)
{
    if (argc < 2) usage();
    const char *cmd = argv[1];
    char err[1024], buf[PATH_MAX];
    if (!strcmp(cmd, "list")) { lhde_theme_list(list_one, NULL); return 0; }
    if (!strcmp(cmd, "dirs")) {
        const char *d[LHDE_DATA_MAX]; int n = lhde_data_dirs(d, LHDE_DATA_MAX);
        for (int i = 0; i < n; i++) printf("data %d  %s\n", i + 1, d[i]);
        printf("user data  %s\n", lhde_data_user());
        printf("settings   %s\n", lhde_settings_path(buf, sizeof buf));
        printf("themes     %s\n", lhde_theme_user_dir(buf, sizeof buf));
        return 0;
    }
    if (!strcmp(cmd, "current")) {
        const char *v = lhde_settings_get("theme", "name", buf, sizeof buf);
        if (!v) { fprintf(stderr, "lhde-theme: no [theme] name in %s\n", lhde_settings_path(buf, sizeof buf)); return 1; }
        printf("%s\n", v); return 0;
    }
    if (!strcmp(cmd, "resolve")) {
        if (argc < 3) usage();
        int why = 0, sh = 0; const char *one = NULL;
        for (int i = 3; i < argc; i++) {
            if (!strcmp(argv[i], "--why")) why = 1;
            else if (!strcmp(argv[i], "--shell")) sh = 1;
            else if (!strcmp(argv[i], "--get") && i + 1 < argc) one = argv[++i];
            else usage();
        }
        lhde_theme *t = lhde_theme_resolve(argv[2], err, sizeof err);
        if (!t) { fprintf(stderr, "lhde-theme: %s\n", err); return 1; }
        if (one) { const char *v = lhde_theme_get(t, one); if (!v) { lhde_theme_free(t); return 1; } printf("%s\n", v); lhde_theme_free(t); return 0; }
        const char *k, *v, *w;
        for (int i = 0; lhde_theme_item(t, i, &k, &v, &w); i++) {
            if (sh) {
                printf("lhde_");
                for (const char *c = k; *c; c++) putchar(*c == '.' || *c == '-' ? '_' : *c);
                putchar('='); shq(v); putchar('\n');
            } else if (why) printf("%-24s = %-40s  # %s\n", k, v, w);
            else printf("%s=%s\n", k, v);
        }
        lhde_theme_free(t);
        return 0;
    }
    if (!strcmp(cmd, "set")) {
        if (argc < 3) usage();
        char canon[256], file[PATH_MAX];
        if (!lhde_theme_lookup(argv[2], canon, sizeof canon, file, sizeof file)) { fprintf(stderr, "lhde-theme: no theme %s\n", argv[2]); return 1; }
        if (lhde_settings_set("theme", "name", canon) < 0) { fprintf(stderr, "lhde-theme: cannot write %s\n", lhde_settings_path(buf, sizeof buf)); return 1; }
        const char *d = getenv("DISPLAY");
        if (d && *d) {
            char c[600]; snprintf(c, sizeof c, "xprop -root -f _LH_THEME 8u -set _LH_THEME '%s' 2>/dev/null", canon);
            if (system(c) != 0) fprintf(stderr, "lhde-theme: no X server on %s: settings.ini only\n", d);
        }
        printf("%s\n", canon);
        return 0;
    }
    if (!strcmp(cmd, "set-key")) {
        if (argc < 5) usage();
        char sel[300];
        int r = lhde_theme_set_key(argv[2], argv[3], strcmp(argv[4], "-") ? argv[4] : NULL, sel, sizeof sel);
        if (r < 0) { fprintf(stderr, "lhde-theme: cannot set %s for %s\n", argv[3], argv[2]); return 1; }
        printf("%s%s\n", sel, r ? "" : " (unchanged)");
        return 0;
    }
    if (!strcmp(cmd, "setting")) {
        if (argc < 4) usage();
        if (argc == 4) { const char *v = lhde_settings_get(argv[2], argv[3], buf, sizeof buf); if (!v) return 1; printf("%s\n", v); return 0; }
        int r = lhde_settings_set(argv[2], argv[3], strcmp(argv[4], "-") ? argv[4] : NULL);
        return r < 0;
    }
    if (!strcmp(cmd, "style")) {
        if (argc < 3) usage();
        lhde_style_info si;
        int ok = lhde_style_probe(argv[2], &si);
        static const char *kinds[] = { "missing", "classic", "xp", "longhorn", "dwm" };
        printf("kind=%s\nformat=%d\ncomposited=%d\ndwm=%d\nsidebar=%d\nbuild=%s\nfamily=%s\ncolor=%s\nsize=%s\npath=%s\n",
               kinds[si.kind], si.format, si.composited, si.dwm, si.sidebar, si.build, si.family, si.color, si.size, si.path);
        return ok ? 0 : 1;
    }
    if (!strcmp(cmd, "migrate")) {
        static char rep[65536];
        int r = lhde_settings_migrate(argc > 2 && !strcmp(argv[2], "--force"), rep, sizeof rep);
        fputs(rep, stdout);
        if (r == 0) printf("lhde-theme: nothing to migrate (%s exists, or no ~/.config/longhorn)\n", lhde_settings_path(buf, sizeof buf));
        return r < 0;
    }
    if (!strcmp(cmd, "registry")) {
        if (!lhde_compat_registry(buf, sizeof buf)) { fprintf(stderr, "lhde-theme: cannot write the registry\n"); return 1; }
        printf("%s\n", buf);
        return 0;
    }
    usage();
    return 2;
}
