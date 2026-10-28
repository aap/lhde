/* lhde's data directories (docs/LAYOUT.md "Asset policy"): where luxwm, luxnav, luxshell and the examples find the art the
   user imported (lhde-import: styles/ frames/ packs/ resources/ fonts/ wallpapers/) and the text the build ships (data/:
   themes.ini, wallpapers.ini, frames/NAME/theme.ini, packs/NAME/shell.lua, ...). The seed of step 4's lhtheme library: plain C,
   no X, compiled into each program that needs it.

   The search order (lhde_data_dirs), first match wins for every relative lookup:
     1. the user's data directory: $LHDE_DATA when set (tests, scratch imports), else ${XDG_DATA_HOME:-~/.local/share}/lhde
     2. the installed one, <exe>/../share/lhde (the program's own prefix: make install PREFIX=...)
     3. $XDG_DATA_DIRS (default /usr/local/share:/usr/share), each + /lhde
     4. the source tree's data/, when the program runs from a build tree (<top>/build/...; <top> has data/themes.ini and
        luxwm/wm.c): the text files of an uninstalled build
   A frame set or pack is usually split: its text (theme.ini, shell.lua) in 2 or 4, its pixels in 1; lhde_data_find_in finds a
   file of the same relative directory in any of them. */
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
/* the first data directory that has REL (a file or a directory) -> out (dir/REL); NULL (out = the user dir's dir/REL) when none */
const char *lhde_data_find(const char *rel, char *out, size_t n);
/* a value of a registry or theme file: absolute stays, ~/... is the home, anything else is looked up with lhde_data_find
   (not found: the user dir's path, so error messages show where it was expected) */
const char *lhde_data_resolve(const char *v, char *out, size_t n);
/* a directory named by a value (frames/jade4074, packs/4074-aero): the first data directory whose copy has MARKER (theme.ini,
   shell.lua), i.e. the one with the text; its pixels are found with lhde_data_find_in. Absolute and ~/ values as they are. */
const char *lhde_data_resolve_dir(const char *v, const char *marker, char *out, size_t n);
/* NAME next to DIR: DIR/NAME when it exists; else, when DIR lies in one of the data directories, the same relative path
   (data dir/<DIR relative>/NAME) in the others, in search order. NULL (out = DIR/NAME) when none has it. */
const char *lhde_data_find_in(const char *dir, const char *name, char *out, size_t n);
/* a component's run-time directory (luxshell's Lua, session's scripts): <top>/NAME in a build tree, else
   <exe>/../share/lhde/NAME; NULL when neither exists */
const char *lhde_component_dir(const char *name, char *out, size_t n);
/* the source tree's top directory when the program runs from a build tree, else NULL */
const char *lhde_tree(void);

/* The registry (data/themes.ini) names its components relative to the data directories (styles/jade4074.msstyles,
   frames/jade4074, packs/4074-aero, fonts/4074, resources/4074). Readers that resolve relative values against the registry's
   own directory (flxp, older scripts) get a resolved copy instead: every path-valued key made absolute, the rest verbatim,
   wallpapers.ini beside it with an absolute root. The copy lives in $XDG_RUNTIME_DIR/lhde/<display>/ (else /tmp/lhde-<uid>/...)
   and starts with "; lhde: resolved from SRC"; lhde_registry_resolved rewrites it when SRC (or its wallpapers.ini) is newer.
     src: a source registry (absolute) or an existing resolved copy (then its source is refreshed into it)
   Returns the copy's path in out, NULL when src cannot be read or the copy not written. */
const char *lhde_registry_resolved(const char *src, char *out, size_t n);
/* the source registry: $LH_THEMES_INI, else lhde_data_find("themes.ini"); NULL when none */
const char *lhde_registry_source(char *out, size_t n);

#ifdef __cplusplus
}
#endif
#endif
