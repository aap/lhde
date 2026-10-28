# Sourced by lhde-session, lab.sh, lhwall.sh, the theme scripts and luxwm's regress: lhde's directories the way the programs
# see them. Every lookup goes through lhde-theme (liblhde/data.c, docs/LAYOUT.md "The three trees"); nothing here knows the
# layout. Set LHDE_SELF to the sourcing script's real path first (lhde_self below does it).
#   lhde_data_dirs       the search order, one directory per line (lhde-theme dirs --list): ~/.config/lhde, the Windows tree's
#                        index ($LHDE_WINDOWS/index, or $LHDE_DATA), an old-layout ~/.local/share/lhde, the install's
#                        <prefix>/share/lhde, $XDG_DATA_DIRS/lhde, the source tree's data/
#   lhde_find REL        the first directory's DIR/REL that exists (nothing when none has it): lhde-theme find REL
#   lhde_resolve VALUE   a theme value as a path: absolute or ~/ as it is, else lhde_find (else where it would be)
#   lhde_bin NAME        a program: the build tree's build/bin or build/examples, else beside this script's install, else PATH
#   LHDE_TOP             the source tree (when the script runs from one); LHDE_SHARE the install's share/lhde and LHDE_LIB its
#                        lib/lhde (else empty); LHDE_USER_DATA the Windows tree's index (where lhde-import's files are found)
#   LHDE_SESSION_DIR     session/ of the tree or lib/lhde/session of the install
# A script in session/ of the tree: LHDE_TOP=<tree>. Installed: lhde-session in <prefix>/bin, the rest in
# <prefix>/lib/lhde/session (lhde-session sources <prefix>/lib/lhde/session/lhde-paths.sh).
lhde_self() { # SCRIPT -> LHDE_SELF_DIR (its directory, symlinks resolved)
	LHDE_SELF_DIR=$(cd "$(dirname "$(readlink -f "$1")")" && pwd)
}
lhde_setup_paths() {
	LHDE_TOP= LHDE_SHARE= LHDE_LIB= LHDE_BINDIR=
	d=$LHDE_SELF_DIR
	if [ -d "$d/../data/themes" ] && [ -f "$d/../luxwm/wm.c" ]; then LHDE_TOP=$(cd "$d/.." && pwd)
	elif [ -f "$d/../lib/lhde/session/lhde-paths.sh" ]; then LHDE_LIB=$(cd "$d/../lib/lhde" && pwd); LHDE_BINDIR=$d
	elif [ -f "$d/lhde-paths.sh" ] && [ "$(basename "$d")" = session ] && [ "$(basename "$(dirname "$d")")" = lhde ]; then
		LHDE_LIB=$(cd "$d/.." && pwd); LHDE_BINDIR=$(cd "$d/../../../bin" 2>/dev/null && pwd)
	fi
	if [ -n "$LHDE_LIB" ] && [ -d "$LHDE_LIB/../../share/lhde" ]; then LHDE_SHARE=$(cd "$LHDE_LIB/../../share/lhde" && pwd); fi
	if [ -n "$LHDE_TOP" ]; then LHDE_SESSION_DIR=$LHDE_TOP/session; else LHDE_SESSION_DIR=$LHDE_LIB/session; fi
	LHDE_USER_DATA=$(lhde_theme_tool dirs --user 2>/dev/null)
}
lhde_theme_tool() { "$(lhde_bin lhde-theme)" "$@"; }
lhde_data_dirs() { lhde_theme_tool dirs --list 2>/dev/null; }
lhde_find() { lhde_theme_tool find "$1" 2>/dev/null; }
lhde_resolve() { [ -n "$1" ] && lhde_theme_tool path "$1" 2>/dev/null; }
lhde_bin() {
	if [ -n "$LHDE_TOP" ]; then
		for x in "$LHDE_TOP/build/bin/$1" "$LHDE_TOP/build/examples/$1" "$LHDE_TOP/session/$1" "$LHDE_TOP/tools/$1"; do [ -x "$x" ] && { echo "$x"; return; }; done
	fi
	[ -n "$LHDE_BINDIR" ] && [ -x "$LHDE_BINDIR/$1" ] && { echo "$LHDE_BINDIR/$1"; return; }
	command -v "$1" 2>/dev/null
}
# a theme name through liblhde (lhde-theme: theme files, aliases, bare styles): prints the theme's name, nothing when unknown
lhde_themename() { [ -n "$1" ] && "$(lhde_bin lhde-theme)" resolve "$1" --get theme 2>/dev/null; }
# the theme names, one line each (lhde-theme list)
lhde_themes() { "$(lhde_bin lhde-theme)" list 2>/dev/null | awk '{ print $1 }' | tr '\n' ' '; }
