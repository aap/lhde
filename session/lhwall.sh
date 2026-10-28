# Sourced (not run) by lhde-session (was xinitrc.lab and second-display.sh): the startup wallpaper and the compositor rule, from
# liblhde's resolved theme (lhde-theme resolve, docs/THEME-FORMAT.md), so the first frames of a session already show the wallpaper
# and the frames the session keeps.
#   lhwall_resolve [THEME]  prints "MODE PATH": PATH is an image, "none" (no image) or empty (nothing to paint). The theme's
#                           Desktop.wallpaper / Desktop.mode after its base chain, the build's default and the fallbacks, with
#                           settings.ini's per-user [desktop] wallpaper = none applied by the resolver. THEME defaults to
#                           settings.ini [theme] name (lhde-theme current).
#   lhwall_apply [THEME]    sets the root background once: feh --bg-MODE PATH, or xsetroot -solid '#3a6ea5' for none.
#                           LHSHELL_WALLPAPER=0 forces "none". Needs DISPLAY.
#   lhcomp_wanted [THEME]   prints yes / no: whether a compositor should run for THEME (luxshell's rule, runtime.lua
#                           apply_composition): Glass.composition on -> yes, off -> no, auto -> yes when the theme has a glass
#                           frame set (path.frames), else no.
# The sourcing script has sourced lhde-paths.sh and run lhde_setup_paths (lhde_bin finds lhde-theme in the build tree or beside
# the install); LHDE_DATA and XDG_CONFIG_HOME are honoured as by every lhde program.
lhw_theme() { lhw_lt=$(lhde_bin lhde-theme); if [ -n "$1" ]; then echo "$1"; else "$lhw_lt" current 2>/dev/null; fi; }
lhw_get() { # THEME KEY
	"$(lhde_bin lhde-theme)" resolve "$1" --get "$2" 2>/dev/null; }
lhwall_resolve() {
	lhw_t=$(lhw_theme "$1"); [ -n "$lhw_t" ] || lhw_t=aero4074
	lhw_w=$(lhw_get "$lhw_t" Desktop.wallpaper); lhw_m=$(lhw_get "$lhw_t" Desktop.mode)
	if [ "$lhw_w" = none ]; then echo "${lhw_m:-fill} none"; return; fi
	echo "${lhw_m:-fill} $(lhw_get "$lhw_t" path.wallpaper)"
}
lhwall_apply() {
	lhw_r=$(lhwall_resolve "$1"); lhw_mode=${lhw_r%% *}; lhw_file=${lhw_r#* }
	[ "$LHSHELL_WALLPAPER" = 0 ] && lhw_file=none
	if [ "$lhw_file" = none ]; then xsetroot -solid '#3a6ea5' 2>/dev/null; xprop -root -remove _XROOTPMAP_ID -remove ESETROOT_PMAP_ID 2>/dev/null; return; fi   # a stale pixmap id would keep picom painting the old image
	[ -n "$lhw_file" ] && [ -f "$lhw_file" ] || return 0
	case $lhw_mode in fit) lhw_f=--bg-max ;; center) lhw_f=--bg-center ;; tile) lhw_f=--bg-tile ;; stretch) lhw_f=--bg-scale ;; *) lhw_f=--bg-fill ;; esac
	feh --no-fehbg $lhw_f "$lhw_file" 2>/dev/null
}
lhcomp_wanted() {
	lhw_t=$(lhw_theme "$1"); [ -n "$lhw_t" ] || lhw_t=aero4074
	case $(lhw_get "$lhw_t" Glass.composition) in
	on) echo yes ;;
	off) echo no ;;
	*) [ -n "$(lhw_get "$lhw_t" path.frames)" ] && echo yes || echo no ;;
	esac
}
