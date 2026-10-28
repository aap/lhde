# Sourced (not run) by lhde-session (was xinitrc.lab and second-display.sh): the startup wallpaper, resolved as lux does (lhshell/runtime.lua
# `effective`, SETTINGS.md "Resolved theme"), so the first frames of a session already show the wallpaper the session keeps.
#   lhwall_resolve [THEME]  prints "MODE PATH": PATH is an image, "none" (no image) or empty (nothing to paint).
#                           Order (THEMES.md "Wallpapers"): settings.ini [desktop] wallpaper = none wins; then settings.ini
#                           [override:NAME] wallpaper/mode; then a preset's own [Desktop] wallpaper/mode; then settings.ini
#                           [desktop] wallpaper/mode; then a `wallpaper` key in the themes.ini section (none ships); then
#                           wallpapers.ini [SECTION] (an alias's section too), then its [default] (a preset resolves to its
#                           base). THEME defaults to settings.ini [theme] name. LH_WALLPAPERS_INI: another wallpapers.ini.
#   lhwall_apply [THEME]    sets the root background once: feh --bg-MODE PATH, or xsetroot -solid '#3a6ea5' for none.
#                           LHSHELL_WALLPAPER=0 forces "none". Needs DISPLAY.
#   lhcomp_wanted [THEME]   prints yes / no: whether a compositor should run for THEME, the rule lux applies (runtime.lua
#                           apply_composition, SETTINGS.md "Per-theme settings"): the switch = settings.ini [override:THEME]
#                           composition, else the preset's [Composition] composition, else [theme] composition, else follow;
#                           on -> yes, off -> no, follow -> the theme's frame mode (the preset's [Composition] composition = off,
#                           else themes.ini `composition` of the entry or the preset's base; off -> no, anything else -> yes).
# The sourcing script has sourced lhde-paths.sh and run lhde_setup_paths (lhde: the registry and the wallpapers come from lhde's
# data directories; a relative wallpapers.ini root is looked up there, LHDE_DATA honoured); LH_SETTINGS, LH_THEMES_INI and
# XDG_CONFIG_HOME are honoured like luxshell does.
lhw_ini() { # FILE SECTION KEY: last value, section and key case-insensitive, up to a comment (; or # after a blank)
	awk -v s="$2" -v k="$3" 'BEGIN { s = tolower(s); k = tolower(k) }
		/^[ \t]*\[/ { t = $0; gsub(/^[ \t]*\[|\][ \t]*$/, "", t); sec = (tolower(t) == s); next }
		sec { i = index($0, "="); if (!i) next; n = substr($0, 1, i - 1); gsub(/[ \t]/, "", n)
			if (tolower(n) != k) next; v = substr($0, i + 1); sub(/[ \t]+[;#].*$/, "", v); gsub(/^[ \t]+|[ \t]+$/, "", v); r = v }
		END { print r }' "$1" 2>/dev/null; }
lhw_rn() { awk -v n="$1" '
	/^[ \t]*\[/ { s = $0; gsub(/^[ \t]*\[|\][ \t]*$/, "", s); if (s == n) { print s; exit } next }
	/^[ \t]*aliases[ \t]*=/ { v = $0; sub(/^[^=]*=/, "", v); sub(/[ \t][;#].*$/, "", v); k = split(v, a, /[ \t,]+/); for (i = 1; i <= k; i++) if (a[i] == n) { print s; exit } }' "$lhw_reg" 2>/dev/null; }
lhw_aliases() { lhw_ini "$lhw_reg" "$1" aliases | tr ',' ' '; }   # SECTION: its aliases
# [override:NAME] KEY for theme NAME; for a registry section also [override:ALIAS] (aero4074 is an alias of aero-patch now)
lhw_ovr() { # NAME KEY [REGISTRY-SECTION]
	lhw_o=$(lhw_ini "$lhw_st" "override:$1" "$2")
	if [ -z "$lhw_o" ] && [ -n "$3" ]; then for lhw_a in $(lhw_aliases "$3"); do lhw_o=$(lhw_ini "$lhw_st" "override:$lhw_a" "$2"); [ -n "$lhw_o" ] && break; done; fi
	echo "$lhw_o"; }
lhw_home() { case $1 in "~/"*) echo "$HOME/${1#"~/"}" ;; *) echo "$1" ;; esac; }
lhwall_resolve() {
	lhw_cfg=${XDG_CONFIG_HOME:-$HOME/.config}/longhorn; lhw_st=${LH_SETTINGS:-$lhw_cfg/settings.ini}
	lhw_reg=$(lhde_registry)
	lhw_wpi=${LH_WALLPAPERS_INI:-$(dirname "$lhw_reg")/wallpapers.ini}; [ -f "$lhw_wpi" ] || lhw_wpi=$(lhde_find wallpapers.ini)
	lhw_swp=$(lhw_ini "$lhw_st" desktop wallpaper); lhw_smode=$(lhw_ini "$lhw_st" desktop mode)
	lhw_t=${1:-$(lhw_ini "$lhw_st" theme name)}
	if [ "$lhw_swp" = none ]; then echo "${lhw_smode:-fill} none"; return; fi
	# registry entry: section name or alias; else a preset (themes/NAME.theme) whose [Theme] base is a registry name
	lhw_pw=; lhw_pm=; lhw_sel=
	lhw_name=$(lhw_rn "$(basename "${lhw_t:-x}")"); [ -n "$lhw_name" ] && lhw_sel=$lhw_name
	if [ -z "$lhw_name" ] && [ -n "$lhw_t" ]; then
		lhw_pf=$lhw_cfg/themes/$lhw_t.theme
		if [ -f "$lhw_pf" ]; then
			lhw_sel=$lhw_t
			lhw_name=$(lhw_rn "$(lhw_ini "$lhw_pf" theme base)")
			lhw_pw=$(lhw_ini "$lhw_pf" desktop wallpaper); lhw_pm=$(lhw_ini "$lhw_pf" desktop mode)
			case $lhw_pw in ""|none|/*) ;; "~/"*) lhw_pw=$HOME/${lhw_pw#"~/"} ;; *) lhw_pw=$lhw_cfg/themes/$lhw_pw ;; esac
		fi
	fi
	# the order of THEMES.md "Wallpapers" (lux: runtime.lua effective)
	lhw_w=; lhw_m=; lhw_ow=
	[ -n "$lhw_sel" ] && lhw_ow=$(lhw_ovr "$lhw_sel" wallpaper "$([ "$lhw_sel" = "$lhw_name" ] && echo "$lhw_name")")
	if [ -n "$lhw_ow" ]; then
		lhw_w=$(lhw_home "$lhw_ow"); lhw_m=$(lhw_ovr "$lhw_sel" mode "$([ "$lhw_sel" = "$lhw_name" ] && echo "$lhw_name")"); [ -n "$lhw_m" ] || lhw_m=$lhw_smode
	elif [ -n "$lhw_pw" ]; then lhw_w=$lhw_pw; lhw_m=${lhw_pm:-$lhw_smode}
	elif [ -n "$lhw_swp" ]; then lhw_w=$(lhw_home "$lhw_swp"); lhw_m=$lhw_smode
	elif [ -n "$lhw_name" ] && [ -n "$(lhw_ini "$lhw_reg" "$lhw_name" wallpaper)" ]; then   # a legacy themes.ini key still wins
		lhw_w=$(lhw_ini "$lhw_reg" "$lhw_name" wallpaper)
		lhw_w=$(lhde_resolve "$lhw_w")
		lhw_m=${lhw_pm:-$lhw_smode}
	else   # wallpapers.ini: [base] (or one of its aliases), else [default]; files relative to [default] root
		lhw_root=$(lhw_ini "$lhw_wpi" default root); lhw_root=$(lhde_resolve "${lhw_root:-wallpapers}"); lhw_root=${lhw_root%/}
		for lhw_s in $lhw_name $([ -n "$lhw_name" ] && lhw_aliases "$lhw_name") default; do
			lhw_f=$(lhw_ini "$lhw_wpi" "$lhw_s" file)
			if [ -n "$lhw_f" ]; then lhw_m=${lhw_pm:-$(lhw_ini "$lhw_wpi" "$lhw_s" mode)}; break; fi
		done
		case $lhw_f in ""|none|/*) lhw_w=$lhw_f ;; "~/"*) lhw_w=$HOME/${lhw_f#"~/"} ;; *) lhw_w=$lhw_root/$lhw_f ;; esac
		[ -n "$lhw_m" ] || lhw_m=$lhw_smode
	fi
	echo "${lhw_m:-fill} $lhw_w"
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
	lhw_cfg=${XDG_CONFIG_HOME:-$HOME/.config}/longhorn; lhw_st=${LH_SETTINGS:-$lhw_cfg/settings.ini}
	lhw_reg=$(lhde_registry)
	lhw_t=${1:-$(lhw_ini "$lhw_st" theme name)}
	# NAME as lux has it: the registry section (an alias resolved), else the preset's file name
	lhc_name=$(lhw_rn "$(basename "${lhw_t:-x}")"); lhc_sec=$lhc_name; lhc_pc=
	if [ -z "$lhc_name" ] && [ -n "$lhw_t" ] && [ -f "$lhw_cfg/themes/$lhw_t.theme" ]; then
		lhc_sec=$lhw_t; lhc_name=$(lhw_rn "$(lhw_ini "$lhw_cfg/themes/$lhw_t.theme" theme base)")
		lhc_pc=$(lhw_ini "$lhw_cfg/themes/$lhw_t.theme" composition composition)
	fi
	lhc_sw=; [ -n "$lhc_sec" ] && lhc_sw=$(lhw_ovr "$lhc_sec" composition "$([ "$lhc_sec" = "$lhc_name" ] && echo "$lhc_name")")
	[ -n "$lhc_sw" ] || case $lhc_pc in on|off|follow) lhc_sw=$lhc_pc ;; esac
	[ -n "$lhc_sw" ] || lhc_sw=$(lhw_ini "$lhw_st" theme composition)
	case $lhc_sw in
	on) echo yes ;;
	off) echo no ;;
	*)  # the frame mode as lux resolves it (session.lua load_preset): a preset's composition off/0 = off, any other value = auto
	    case $lhc_pc in off|0) lhc_fm=off ;; "") lhc_fm=; [ -n "$lhc_name" ] && lhc_fm=$(lhw_ini "$lhw_reg" "$lhc_name" composition) ;; *) lhc_fm=auto ;; esac
	    [ "$lhc_fm" = off ] && echo no || echo yes ;;
	esac
}
