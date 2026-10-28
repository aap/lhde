#!/bin/sh
# luxwm's frame painter regression (make check-luxwm): render the frametest set of every frame set in data/frames (text from the
# tree, pixels and styles from lhde's data directories: the Windows tree ~/.local/share/lhde-windows or LHDE_WINDOWS, LHDE_DATA) and compare the SHA-256 of every
# render with a baseline made on this machine. No reference images are in the repository (docs/LAYOUT.md "Asset policy"): the
# first run writes the baseline, later runs compare against it.
#   luxwm/tests/regress.sh [--source dir|lib] [OUTDIR]     render into OUTDIR (default build/luxwm/regress/out) and compare;
#                                                          no baseline yet: write it (exit 0)
#   luxwm/tests/regress.sh --rebaseline [OUTDIR]           render and replace the baseline (after an intended change)
#   luxwm/tests/regress.sh --baseline-from-x11 X11DIR      the baseline from the x11 lab's own build: runs X11DIR/lhwm/tests/regress.sh
#                                                          (it compiles its frametest from X11DIR's frame.c into a scratch dir and
#                                                          renders X11DIR/lhwm/themes; X11DIR is not written) and takes the
#                                                          checksums of its renders under the lhde names (aero -> aero-patch, ...),
#                                                          so the next run proves equality with the old tree
# The baseline is build/luxwm/regress/baseline.sha256 (LUXWM_BASELINE= another file). Skips (exit 0) when the data directory
# has no styles/ (nothing imported: tools/lhde-import). Sources (docs/LUXWM.md "libuxtheme source"): lib = the visual style named
# in theme.ini read live through libwuxtheme (default), dir = the frame set's PNG cache.
# Renders per frame set (640x480, title "Frame test window"):
#   strip.png      the 4-state strip (focused, hover close, pressed max, inactive)
#   normal.png     focused                    unfocused.png   inactive
#   max.png        focused, maximised
#   ext60.png      FRAMETEST_EXT="0 0 60 0"   (extendframe's header; caption band continued by 60 rows)
#   ext46.png      FRAMETEST_EXT="0 0 46 1"   (the 4074 Explorer header: 27 + 46 = 73 rows, NO_TITLE), focused
#   ext46u.png     the same, inactive         ext46max.png    the same, maximised
#   vision49.png, vision49max.png  only for frame sets with header_mode = crop (aerovision): FRAMETEST_EXT="0 0 22 0 9"
#   classic/ classic.xl/ classic.hc/  Windows Classic frames (style = classic): the built-in Windows Standard scheme, its Extra
#                  Large Fonts metrics, High Contrast colours with a 6 px border (specs "|classic|KEY=VALUE;...")
# Each render also keeps frametest's stdout (margins, shape) as NAME.txt, compared too.
# Classic fallbacks (docs/LUXWM.md "Classic fallback"): for every glass frame set whose style is a DWM-era one (5048, 5112: their
# themes' frames_classic is none, the style's own decorations), <set>.classic/ holds the same renders of the style in msstyles mode.
set -e
here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../.." && pwd)
LHDE_SELF_DIR=$top/session; . "$top/session/lhde-paths.sh"; lhde_setup_paths
mode=compare source=lib x11=
while :; do
	case "$1" in
	--rebaseline) mode=rebaseline; shift ;;
	--baseline-from-x11) mode=x11; x11=$2; shift 2 ;;
	--source) source=$2; shift 2 ;;
	*) break ;;
	esac
done
case "$source" in dir|lib) ;; *) echo "--source dir|lib" >&2; exit 2 ;; esac
R=$top/build/luxwm/regress
out=${1:-$R/out}
base=${LUXWM_BASELINE:-$R/baseline.sha256}
ft=$top/build/luxwm/tests/frametest
mkdir -p "$R"

# the lhde names of the x11 lab's frame directories (tools/lhde-import uses the same table)
x11name() {
	case $1 in
	aero) echo aero-patch ;; jade) echo jade4074 ;; luna-blue) echo luna4051-blue ;; luna-olive) echo luna4051-olive ;;
	luna-silver) echo luna4051-silver ;; aero.classic) echo aero-patch.classic ;; jade.classic) echo jade4074.classic ;;
	*) echo "$1" ;;
	esac
}
checksums() { # DIR -> "sha256  set/file" lines, sorted
	(cd "$1" && find . -mindepth 2 -maxdepth 2 -type f ! -name '.*' | sed 's|^\./||' | sort | while IFS= read -r f; do
		printf '%s  %s\n' "$(sha256sum < "$f" | cut -d' ' -f1)" "$f"; done)
}

if [ $mode = x11 ]; then
	[ -x "$x11/lhwm/tests/regress.sh" ] || { echo "regress: no $x11/lhwm/tests/regress.sh" >&2; exit 2; }
	xo=$R/x11-renders; rm -rf "$xo"; mkdir -p "$xo"
	echo "regress: rendering with the x11 build ($x11, its own frame.c and libuxtheme) into $xo"
	"$x11/lhwm/tests/regress.sh" "$xo" > "$R/x11-regress.log" 2>&1 || true
	tail -n 1 "$R/x11-regress.log"
	checksums "$xo" | while read -r sum f; do echo "$sum  $(x11name "${f%%/*}")/${f#*/}"; done | sort -k2 > "$base"
	echo "regress: baseline from the x11 build: $(wc -l < "$base") renders -> $base"
	exit 0
fi

styles=$(lhde_find styles)
if [ -z "$styles" ] || [ -z "$(ls "$styles" 2>/dev/null)" ]; then
	echo "skip  luxwm regress: no visual styles in $LHDE_USER_DATA/styles (tools/lhde-import)"; exit 0
fi
[ -x "$ft" ] || { echo "regress: build $ft first (make)" >&2; exit 2; }
# the session's fonts (themes.ini `fonts =`, THEMES.md): the 4074 Fonts directory, so "Segoe UI 9" is Segoe UI 0.7 as in luxwm
export FRAMETEST_FONTS=${FRAMETEST_FONTS-$(lhde_find fonts/4074)}
rm -rf "$out"; mkdir -p "$out"

render_one() { # SPEC OUTDIR [vision]
	"$ft" "$1" "$2/strip.png" > "$2/strip.txt"
	"$ft" "$1" "$2/normal.png"    640 480 "Frame test window" 1   > "$2/normal.txt"
	"$ft" "$1" "$2/unfocused.png" 640 480 "Frame test window" 0   > "$2/unfocused.txt"
	"$ft" "$1" "$2/max.png"       640 480 "Frame test window" 1 1 > "$2/max.txt"
	FRAMETEST_EXT="0 0 60 0" "$ft" "$1" "$2/ext60.png"    640 480 "Frame test window" 1   > "$2/ext60.txt"
	FRAMETEST_EXT="0 0 46 0 1" "$ft" "$1" "$2/ext46.png"  640 480 "Frame test window" 1   > "$2/ext46.txt"
	FRAMETEST_EXT="0 0 46 0 1" "$ft" "$1" "$2/ext46u.png" 640 480 "Frame test window" 0   > "$2/ext46u.txt"
	FRAMETEST_EXT="0 0 46 0 1" "$ft" "$1" "$2/ext46max.png" 640 480 "Frame test window" 1 1 > "$2/ext46max.txt"
	# vision frame sets (header_mode = crop): the navdemo header, 15 + 30 + 2 = 47 glass rows and the line pair (H = 27 + 22 = 49),
	# CLIENT_CAPTION | NO_TITLE (9), focused and maximised
	if [ "$3" = vision ]; then
		FRAMETEST_EXT="0 0 22 0 9" "$ft" "$1" "$2/vision49.png" 640 480 "Frame test window" 1   > "$2/vision49.txt"
		FRAMETEST_EXT="0 0 22 0 9" "$ft" "$1" "$2/vision49max.png" 640 480 "Frame test window" 1 1 > "$2/vision49max.txt"
	fi
}
# the glass frame sets whose style has a non-composited look of its own: 5048's and 5112's Window class (the basic5048/basic5112
# themes, frames_classic = none) -- the style is v4 without CompositedWindow (lhde-theme style: kind = dwm)
classic_own() {
	for td in "$top"/data/frames/*/; do
		grep -q '^mode *= *dwm' "$td/theme.ini" 2>/dev/null || continue
		st=$(ini "$td/theme.ini" msstyles); st=${st#styles/}; st=${st%.msstyles}
		[ -n "$st" ] && "$top/build/bin/lhde-theme" style "$st" 2>/dev/null | grep -qx 'kind=dwm' && basename "$td"
	done | sort -u
}
ini() { sed -n "s/^$2 *= *//p" "$1" | head -1; }

export FRAMETEST_SOURCE=$source
n=0
for td in "$top"/data/frames/*/; do
	set_=$(basename "$td"); d=$out/$set_
	if [ "$source" = dir ] && [ -z "$(ls "$(lhde_find "frames/$set_")"/*.png 2>/dev/null)" ]; then continue; fi
	mkdir -p "$d"
	v=; grep -q '^header_mode *= *crop' "$td/theme.ini" 2>/dev/null && v=vision
	render_one "${td%/}" "$d" $v; n=$((n + 1))
done
if [ "$source" = lib ]; then
	for set_ in $(classic_own); do
		td=$top/data/frames/$set_; d=$out/$set_.classic
		st=$(ini "$td/theme.ini" msstyles); [ -n "$st" ] || continue
		spec="|$(lhde_resolve "$st")|$(ini "$td/theme.ini" color)|$(ini "$td/theme.ini" size)"; mkdir -p "$d"
		render_one "$spec" "$d"; n=$((n + 1))
	done
fi
# Windows Classic (docs/LUXWM.md "Windows Classic frames"; no visual style, no data: the built-in Windows Standard scheme): classic/,
# and the same frame at Extra Large Fonts metrics (classic.xl/) and with a 6 px sizing border in High Contrast Black's colours
# (classic.hc/)
hc='activecaption=128 0 128;gradientactivecaption=128 0 128;inactivecaption=0 128 0;gradientinactivecaption=0 128 0'
hc="$hc;captiontext=255 255 255;inactivecaptiontext=255 255 255;activeborder=255 255 0;inactiveborder=0 128 0;btnface=0 0 0"
hc="$hc;btntext=255 255 255;btnhighlight=255 255 255;btnshadow=128 128 128;3dlight=192 192 192;3ddkshadow=255 255 255"
for c in "classic|" "classic.xl|captionwidth=24;captionheight=24;captionfont=Tahoma, -19, bold" \
	"classic.hc|$hc;borderwidth=6;captionwidth=31;captionheight=31;captionfont=Tahoma, -24, bold"; do
	d=$out/${c%%|*}; mkdir -p "$d"; render_one "|classic|${c#*|}" "$d"; n=$((n + 1))
done
checksums "$out" > "$R/out.sha256"
if [ $mode = rebaseline ] || [ ! -f "$base" ]; then
	cp "$R/out.sha256" "$base"
	echo "ok    luxwm regress: baseline written, $(wc -l < "$base") renders of $n frame sets (source $source, data $LHDE_USER_DATA) -> $base"
	exit 0
fi
same=0 diff=0 miss=0 new=0
while read -r sum f; do
	got=$(awk -v f="$f" '$2 == f { print $1 }' "$R/out.sha256")
	if [ -z "$got" ]; then echo "MISSING $f"; miss=$((miss + 1))
	elif [ "$got" = "$sum" ]; then same=$((same + 1))
	else echo "CHANGED $f"; diff=$((diff + 1)); fi
done < "$base"
while read -r sum f; do grep -q "  $f\$" "$base" || { echo "NEW     $f"; new=$((new + 1)); }; done < "$R/out.sha256"
if [ $diff -eq 0 ] && [ $miss -eq 0 ]; then
	echo "ok    luxwm regress: $same renders identical to the baseline$( [ $new -gt 0 ] && echo ", $new new") (source $source, out $out)"
else
	echo "FAIL  luxwm regress: $same identical, $diff changed, $miss missing, $new new (out $out, baseline $base)"; exit 1
fi
