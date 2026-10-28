# Sourced by lhde-session, lab.sh, lhwall.sh, the theme scripts and luxwm's regress: lhde's directories the way the programs
# see them (liblhde, lhde/data.h). Set LHDE_SELF to the sourcing script's real path first (lhde_self below does it).
#   lhde_data_dirs       the data directories in search order, one per line: $LHDE_DATA (else ${XDG_DATA_HOME:-~/.local/share}/lhde),
#                        the install's <prefix>/share/lhde, $XDG_DATA_DIRS/lhde, the source tree's data/
#   lhde_find REL        the first data directory's DIR/REL that exists (nothing when none has it)
#   lhde_resolve VALUE   a registry value: absolute or ~/ as it is, else lhde_find (else the user dir's path)
#   lhde_bin NAME        a program: the build tree's build/bin or build/examples, else beside this script's install, else PATH
#   LHDE_TOP             the source tree (when the script runs from one), LHDE_SHARE the install's share/lhde (else empty)
#   LHDE_SESSION_DIR     session/ of the tree or share/lhde/session of the install
# A script in session/ of the tree: LHDE_TOP=<tree>. Installed: lhde-session in <prefix>/bin, the rest in
# <prefix>/share/lhde/session (lhde-session sources <prefix>/share/lhde/session/lhde-paths.sh).
lhde_self() { # SCRIPT -> LHDE_SELF_DIR (its directory, symlinks resolved)
	LHDE_SELF_DIR=$(cd "$(dirname "$(readlink -f "$1")")" && pwd)
}
lhde_setup_paths() {
	LHDE_TOP= LHDE_SHARE= LHDE_BINDIR=
	d=$LHDE_SELF_DIR
	if [ -d "$d/../data/themes" ] && [ -f "$d/../luxwm/wm.c" ]; then LHDE_TOP=$(cd "$d/.." && pwd)
	elif [ -f "$d/../share/lhde/session/lhde-paths.sh" ]; then LHDE_SHARE=$(cd "$d/../share/lhde" && pwd); LHDE_BINDIR=$d
	elif [ -f "$d/lhde-paths.sh" ] && [ "$(basename "$d")" = session ] && [ "$(basename "$(dirname "$d")")" = lhde ]; then
		LHDE_SHARE=$(cd "$d/.." && pwd); LHDE_BINDIR=$(cd "$d/../../../bin" 2>/dev/null && pwd)
	fi
	if [ -n "$LHDE_TOP" ]; then LHDE_SESSION_DIR=$LHDE_TOP/session; else LHDE_SESSION_DIR=$LHDE_SHARE/session; fi
	LHDE_USER_DATA=${LHDE_DATA:-${XDG_DATA_HOME:-$HOME/.local/share}/lhde}
}
lhde_data_dirs() {
	{
		echo "$LHDE_USER_DATA"
		[ -n "$LHDE_SHARE" ] && echo "$LHDE_SHARE"
		echo "${XDG_DATA_DIRS:-/usr/local/share:/usr/share}" | tr ':' '\n' | sed 's|$|/lhde|'
		[ -n "$LHDE_TOP" ] && echo "$LHDE_TOP/data"
	} | while IFS= read -r x; do [ -d "$x" ] && echo "$x"; done
}
lhde_find() {
	lhde_data_dirs | while IFS= read -r x; do [ -e "$x/$1" ] && { echo "$x/$1"; break; }; done
}
lhde_resolve() {
	case $1 in
	"") ;;
	/*) echo "$1" ;;
	"~/"*) echo "$HOME/${1#"~/"}" ;;
	*) r=$(lhde_find "$1"); echo "${r:-$LHDE_USER_DATA/$1}" ;;
	esac
}
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
