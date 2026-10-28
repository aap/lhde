/* lhde-theme: the theme files from the command line (liblhde; docs/THEME-FORMAT.md, docs/USAGE.md "lhde-theme").
     lhde-theme list                       every theme: the user's, the shipped ones, the bare styles (name, group, title)
     lhde-theme resolve NAME [--why]       the resolved record, one SECTION.KEY=VALUE per line; --why adds where each came from
     lhde-theme resolve NAME --get KEY     one value (scripts: lhde-session, lhwall.sh)
     lhde-theme resolve NAME --shell       the record as shell assignments (lhde_Glass_frames='...')
     lhde-theme current                    the selected theme (settings.ini [theme] name)
     lhde-theme resolve NAME --files ...   the theme files only, without the current layer
     lhde-theme set NAME                   select a theme: settings.ini [theme] name (the current layer of another theme is
                                           dropped), then the root _LH_THEME when $DISPLAY has an X server (xprop); luxwm,
                                           luxshell and flxp follow the property
     lhde-theme layer                      the current layer: its theme, then its keys (settings.ini [current])
     lhde-theme layer-set KEY VALUE        one key of the selected theme's current layer (VALUE "-" removes it)
     lhde-theme layer-clear                drop the current layer
     lhde-theme save-as FILE [TITLE]       the selected theme with its layer as ~/.config/lhde/themes/FILE.theme, selected
     lhde-theme delete NAME                one of the user's theme files (the selected one: its base is selected)
     lhde-theme styles | schemes           the visual styles / classic schemes Display Properties lists
     lhde-theme set-key NAME KEY VALUE     a theme key changed the way Display Properties and luxshell's flyout do it: into
                                           NAME-modified.theme (or the user's own NAME.theme); VALUE "-" removes the key
     lhde-theme style NAME                 what liblhde reads from a visual style (kind, format, build, family, first scheme)
     lhde-theme setting SEC KEY [VALUE]    a per-user key of settings.ini: print it, or write it (VALUE "-" removes it)
     lhde-theme migrate [--force]          the one-time move from ~/.config/longhorn and ~/.config/lhshell (docs/SETTINGS.md)
     lhde-theme registry                   (re)write the compatibility themes.ini for flxp and print its path
     lhde-theme explain [THEME]            where every visible part of THEME (default: the selected one, with its current layer)
                                           comes from: one row per component, its file and tree, ok / off / missing -> fallback
     lhde-theme dirs [--list | --windows | --user]
                                           the three trees (program files, Windows files, user files) with sizes and the search
                                           order; --list: only the search order, one directory per line (scripts); --windows:
                                           the Windows tree's top ($LHDE_WINDOWS); --user: its lookup directory (index/)
     lhde-theme find REL | KIND NAME       a data file by its relative name (styles/jade4074.msstyles) or by kind (style jade4074;
                                           kinds: theme style scheme frames pack navbar icons wallpaper fonts resources source):
                                           the path, exit 1 when no tree has it
     lhde-theme path VALUE                 a theme value as a path: absolute and ~/ as they are, else find (else where it would be) */
#define _GNU_SOURCE
#include "lhde/data.h"
#include "lhde/theme.h"
#include "lhde/settings.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int lhde_explain(const char *name);   /* explain.c */
int lhde_dirs_report(void);

static void usage(void)
{
    fprintf(stderr, "usage: lhde-theme list | resolve NAME [--files] [--why | --get KEY | --shell] | current | set NAME |\n"
                    "                  layer | layer-set KEY VALUE | layer-clear | save-as FILE [TITLE] | delete NAME | styles | schemes |\n"
                    "                  set-key NAME KEY VALUE | setting SEC KEY [VALUE] | style NAME | migrate [--force] |\n"
                    "                  registry | explain [THEME] | dirs [--list | --windows | --user] | find REL | find KIND NAME | path VALUE\n");
    exit(2);
}

static void list_one(const char *name, const char *title, const char *file, int group, void *ud)
{
    (void)file; (void)ud;
    static const char *g[] = { "user", "shipped", "style" };
    printf("%-22s %-8s %s\n", name, g[group < 3 ? group : 2], title);
}

static void layer_one(const char *k, const char *v, void *ud) { (void)ud; printf("%s=%s\n", k, v); }
static void style_one(const char *n, const char *p, void *ud) { (void)ud; printf("%-22s %s\n", n, p); }
static void scheme_one(const char *n, const char *t, const char *p, void *ud) { (void)ud; (void)p; printf("%-28s %s\n", n, t); }

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
        if (argc > 2 && !strcmp(argv[2], "--list")) {
            const char *d[LHDE_DATA_MAX]; int n = lhde_data_dirs(d, LHDE_DATA_MAX);
            for (int i = 0; i < n; i++) printf("%s\n", d[i]);
            return 0;
        }
        if (argc > 2 && !strcmp(argv[2], "--windows")) { printf("%s\n", lhde_windows_dir()); return 0; }
        if (argc > 2 && !strcmp(argv[2], "--user")) { printf("%s\n", lhde_data_user()); return 0; }
        return lhde_dirs_report();
    }
    if (!strcmp(cmd, "explain")) return lhde_explain(argc > 2 ? argv[2] : NULL);
    if (!strcmp(cmd, "find")) {
        if (argc < 3) usage();
        const char *r = argc > 3 ? lhde_data_find_kind(argv[2], argv[3], buf, sizeof buf) : lhde_data_find(argv[2], buf, sizeof buf);
        if (!r) { if (argc > 3 && !lhde_data_kind_rel(argv[2], argv[3], err, sizeof err)) fprintf(stderr, "lhde-theme: no kind %s\n", argv[2]); return 1; }
        printf("%s\n", r);
        return 0;
    }
    if (!strcmp(cmd, "path")) { if (argc < 3) usage(); printf("%s\n", lhde_data_resolve(argv[2], buf, sizeof buf)); return 0; }
    if (!strcmp(cmd, "current")) {
        const char *v = lhde_settings_get("theme", "name", buf, sizeof buf);
        if (!v) { fprintf(stderr, "lhde-theme: no [theme] name in %s\n", lhde_settings_path(buf, sizeof buf)); return 1; }
        printf("%s\n", v); return 0;
    }
    if (!strcmp(cmd, "resolve")) {
        if (argc < 3) usage();
        int why = 0, sh = 0, files = 0; const char *one = NULL;
        for (int i = 3; i < argc; i++) {
            if (!strcmp(argv[i], "--why")) why = 1;
            else if (!strcmp(argv[i], "--files")) files = 1;
            else if (!strcmp(argv[i], "--shell")) sh = 1;
            else if (!strcmp(argv[i], "--get") && i + 1 < argc) one = argv[++i];
            else usage();
        }
        lhde_theme *t = lhde_theme_resolve_flags(argv[2], files ? LHDE_RESOLVE_FILES : 0, err, sizeof err);
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
        if (lhde_theme_select(canon) < 0) { fprintf(stderr, "lhde-theme: cannot write %s\n", lhde_settings_path(buf, sizeof buf)); return 1; }
        const char *d = getenv("DISPLAY");
        if (d && *d) {
            char c[600]; snprintf(c, sizeof c, "xprop -root -f _LH_THEME 8u -set _LH_THEME '%s' 2>/dev/null", canon);
            if (system(c) != 0) fprintf(stderr, "lhde-theme: no X server on %s: settings.ini only\n", d);
        }
        printf("%s\n", canon);
        return 0;
    }
    if (!strcmp(cmd, "layer")) {
        char th[256]; lhde_current_theme(th, sizeof th);
        printf("theme=%s\n", th);
        lhde_current_keys(layer_one, NULL);
        return 0;
    }
    if (!strcmp(cmd, "layer-set")) {
        if (argc < 4) usage();
        int r = lhde_current_set_key(argv[2], strcmp(argv[3], "-") ? argv[3] : NULL);
        if (r < 0) { fprintf(stderr, "lhde-theme: cannot set %s (no theme selected, or not a theme key)\n", argv[2]); return 1; }
        return 0;
    }
    if (!strcmp(cmd, "layer-clear")) return lhde_current_clear() < 0;
    if (!strcmp(cmd, "save-as")) {
        if (argc < 3) usage();
        if (lhde_theme_save_as(argv[2], argc > 3 ? argv[3] : argv[2], err, sizeof err)) { fprintf(stderr, "lhde-theme: %s\n", err); return 1; }
        printf("%s\n", argv[2]);
        return 0;
    }
    if (!strcmp(cmd, "delete")) {
        if (argc < 3) usage();
        int r = lhde_theme_delete(argv[2]);
        if (r <= 0) { fprintf(stderr, "lhde-theme: %s is not one of your theme files\n", argv[2]); return 1; }
        return 0;
    }
    if (!strcmp(cmd, "styles")) { lhde_style_list(style_one, NULL); return 0; }
    if (!strcmp(cmd, "schemes")) { lhde_scheme_list(scheme_one, NULL); return 0; }
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
