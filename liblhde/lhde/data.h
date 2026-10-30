/* liblhde: lhde's three trees and every lookup in them (docs/LAYOUT.md "The three trees"). Plain C, no X.

   Program files (make install; never edited by the user):
     $PREFIX/share/lhde/   shipped data, text only: themes/NAME.theme, navbar/NAME.ini, icons/NAME.theme, sources/ID.ini,
                           frames/<set>/theme.ini (+ nav.ini), packs/<pack>/shell.lua, res/icons (flxp)
     $PREFIX/lib/lhde/     code: luxshell/ (the Lua), session/ (lhde-session's helpers), tools/ (lhde-import's Python)
   Windows files (lhde-import; $LHDE_WINDOWS, default ${XDG_DATA_HOME:-~/.local/share}/lhde-windows):
     <source>/             the raw files of one build, as in the resource pack: system32/ themes/ fonts/ wallpapers/ source.ini
     local/                files dropped in by hand (lhde-import --style): themes/, wallpapers/, resources/
     generated/            everything derived (frames/, packs/, schemes/): delete freely, lhde-import --regenerate
     index/                symlinks under the names programs ask for: styles/NAME.msstyles, resources/<build>, fonts/<build>,
                           wallpapers/<build>, frames -> ../generated/frames, packs -> ../generated/packs (lhde-import keeps it)
   User files: ${XDG_CONFIG_HOME:-~/.config}/lhde/ (settings.ini, themes/; any data file under its data-directory name).

   Every lookup is a relative name of a kind (styles/jade4074.msstyles, frames/jade4074/close.png, resources/4074,
   wallpapers/4074/Bliss.jpg; lhde_data_kind_rel) searched in these directories, first match wins (lhde_data_dirs):
     1. user        ~/.config/lhde
     2. windows     $LHDE_WINDOWS/index; with $LHDE_DATA set (tests, scratch): that one flat directory instead
     3. shipped     <exe>/../share/lhde (the program's own prefix), then $XDG_DATA_DIRS/lhde (default /usr/local/share:/usr/share)
     4. source tree <top>/data when the program runs from a build tree (<top>/build/...; LHDE_TREE=0 leaves it out: tests)
   A frame set or pack is split: its text (theme.ini, shell.lua) is shipped, its pixels are generated; lhde_data_find_in finds a
   file of the same relative directory in any tree. A lookup of DIR/NAME that misses also tries DIR/cache/NAME (the caches of
   a $LHDE_DATA directory). Paths come back as found (through index/); lhde_data_origin tells which tree
   a file really lies in. */
#ifndef LHDE_DATA_H
#define LHDE_DATA_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LHDE_DATA_MAX 12

/* the data directories in search order (absolute, existing, without duplicates); returns how many */
int lhde_data_dirs(const char **dirs, int max);
/* the trees */
enum lhde_tree_kind { LHDE_TREE_USER, LHDE_TREE_WINDOWS, LHDE_TREE_FLAT, LHDE_TREE_SHIPPED, LHDE_TREE_SOURCE,
                      LHDE_TREE_CODE };
/* the tree of lhde_data_dirs' i-th directory (-1 past the end), and a kind's name: user, windows, data, shipped,
   source tree, program ($PREFIX/lib/lhde), "elsewhere" for -1 */
int lhde_data_dir_kind(int i);
const char *lhde_tree_name(int kind);
/* the Windows files' lookup directory (2. above: $LHDE_WINDOWS/index, or $LHDE_DATA; it need not exist yet) */
const char *lhde_data_user(void);
/* the Windows tree itself: $LHDE_WINDOWS (default ${XDG_DATA_HOME:-~/.local/share}/lhde-windows), or $LHDE_DATA */
const char *lhde_windows_dir(void);
/* an unmigrated old-layout directory (${XDG_DATA_HOME:-~/.local/share}/lhde still holding Windows files, before
   lhde-import --migrate): never searched, only reported; NULL when there is none or $LHDE_WINDOWS / $LHDE_DATA is set */
const char *lhde_old_dir(void);
/* which tree PATH really lies in (symlinks resolved; enum lhde_tree_kind, -1 elsewhere); rel = the path below that tree's
   top (for the Windows tree: below $LHDE_WINDOWS, e.g. 4074/system32/imageres.dll), else the resolved path */
int lhde_data_origin(const char *path, char *rel, size_t n);
/* lookup by kind: theme, style, scheme, frames, pack, navbar, icons, wallpaper, fonts, resources, source -> the relative name
   (style jade4074 -> styles/jade4074.msstyles); NULL for an unknown kind. lhde_data_kind_name(i): the kinds, NULL past the end */
const char *lhde_data_kind_rel(const char *kind, const char *name, char *out, size_t n);
const char *lhde_data_kind_name(int i);
/* lhde_data_find of a kind's relative name */
const char *lhde_data_find_kind(const char *kind, const char *name, char *out, size_t n);
/* the bytes (and files) below DIR, symlinks not followed */
long long lhde_du(const char *dir, long long *files);
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
/* a component's run-time code (luxshell's Lua, session's scripts, tools' Python): <top>/NAME in a build tree, else
   <exe>/../lib/lhde/NAME; NULL when neither exists */
const char *lhde_component_dir(const char *name, char *out, size_t n);
/* the code tree: the source tree's top, else <exe>/../lib/lhde; NULL when neither */
const char *lhde_code_dir(char *out, size_t n);
/* the source tree's top directory when the program runs from a build tree, else NULL */
const char *lhde_tree(void);
/* forget the cached directory list (after $LHDE_DATA, $LHDE_WINDOWS, $XDG_DATA_HOME or $XDG_CONFIG_HOME changed: tests) */
void lhde_data_reset(void);

#ifdef __cplusplus
}
#endif
#endif
