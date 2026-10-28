/* liblhde: lhde's data directories (docs/LAYOUT.md "Asset policy", docs/THEME-FORMAT.md section 1): where every lhde program
   finds the art the user imported (lhde-import: styles/ frames/ packs/ resources/ fonts/ wallpapers/) and the text the build
   ships (data/: themes/<name>.theme, navbar/<name>.ini, icons/<name>.theme, frames/NAME/theme.ini, packs/NAME/shell.lua, ...). Plain C, no X.

   The search order (lhde_data_dirs), first match wins for every relative lookup:
     1. the user's data directory: $LHDE_DATA when set (tests, scratch imports), else ${XDG_DATA_HOME:-~/.local/share}/lhde
     2. the installed one, <exe>/../share/lhde (the program's own prefix: make install PREFIX=...)
     3. $XDG_DATA_DIRS (default /usr/local/share:/usr/share), each + /lhde
     4. the source tree's data/, when the program runs from a build tree (<top>/build/...; <top> has data/themes/ and
        luxwm/wm.c): the text files of an uninstalled build (LHDE_TREE=0 leaves it out: tests)
   A frame set or pack is usually split: its text (theme.ini, shell.lua) in 2 or 4, its pixels in 1; lhde_data_find_in finds a
   file of the same relative directory in any of them. Pixels an importer generated from a manifest (manifest.ini) live in a
   cache/ subdirectory that may be deleted and rebuilt (lhde-import --rebuild-cache): a lookup of DIR/NAME that misses also
   tries DIR/cache/NAME, so readers need not know. */
#ifndef LHDE_DATA_H
#define LHDE_DATA_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LHDE_DATA_MAX 12

/* the data directories in search order (absolute, existing, without duplicates); returns how many */
int lhde_data_dirs(const char **dirs, int max);
/* the user's data directory (1. above; it need not exist yet): where lhde-import writes */
const char *lhde_data_user(void);
/* the first data directory that has REL (a file or a directory; also REL's cache/ form) -> out (dir/REL); NULL (out = the
   user dir's dir/REL) when none */
const char *lhde_data_find(const char *rel, char *out, size_t n);
/* a value of a theme or registry file: absolute stays, ~/... is the home, anything else is looked up with lhde_data_find
   (not found: the user dir's path, so error messages show where it was expected) */
const char *lhde_data_resolve(const char *v, char *out, size_t n);
/* a directory named by a value (frames/jade4074, packs/4074-aero): the first data directory whose copy has MARKER (theme.ini,
   shell.lua), i.e. the one with the text; its pixels are found with lhde_data_find_in. Absolute and ~/ values as they are. */
const char *lhde_data_resolve_dir(const char *v, const char *marker, char *out, size_t n);
/* NAME next to DIR: DIR/NAME (or DIR/cache/NAME) when it exists; else, when DIR lies in one of the data directories, the same
   relative path (data dir/<DIR relative>/NAME) in the others, in search order. NULL (out = DIR/NAME) when none has it. */
const char *lhde_data_find_in(const char *dir, const char *name, char *out, size_t n);
/* a component's run-time directory (luxshell's Lua, session's scripts): <top>/NAME in a build tree, else
   <exe>/../share/lhde/NAME; NULL when neither exists */
const char *lhde_component_dir(const char *name, char *out, size_t n);
/* the source tree's top directory when the program runs from a build tree, else NULL */
const char *lhde_tree(void);
/* forget the cached directory list (after $LHDE_DATA or $XDG_DATA_HOME changed: tests) */
void lhde_data_reset(void);

/* ---- the compatibility registry (docs/THEME-FORMAT.md section 11; liblhde/compat.c) ----
   Until flxp reads theme files itself, liblhde writes an old-format themes.ini (one section per theme: shipped theme files,
   the user's, bare styles; every path absolute) and wallpapers.ini from the resolved themes into
   $XDG_RUNTIME_DIR/lhde/<display>/ (else /tmp/lhde-<uid>/<display>/); luxwm and luxshell publish its path as _LH_THEMES_INI.
   Its first line is "; lhde: generated ..."; it is rewritten when a theme file, the styles or settings.ini changed. */
/* the generated registry's path (written or refreshed now); NULL when it cannot be written */
const char *lhde_compat_registry(char *out, size_t n);
/* the registry a program should read: $LH_THEMES_INI, else the generated one (lhde_compat_registry); NULL when none */
const char *lhde_registry_source(char *out, size_t n);
/* a registry path someone published: the generated registry is refreshed in place (and returned), any other existing file is
   returned as it is. NULL when src does not exist. (Before liblhde: the resolved copy of data/themes.ini.) */
const char *lhde_registry_resolved(const char *src, char *out, size_t n);

#ifdef __cplusplus
}
#endif
#endif
