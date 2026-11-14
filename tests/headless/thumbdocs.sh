#!/bin/sh
# Thumbnails of documents through the thumbnailer entries (docs/USAGE.md, thumbnails): a folder with a PDF, and a PostScript
# file and a DjVu file where Ghostscript and djvulibre are installed, each page a green block on white, shown by flxp in the
# XP frame's Thumbnails view at 128 px with a cold thumbnail cache. Each document is made by an entry's program (the
# FLXP_TRACE_THUMBS line), its thumbnail lands in the freedesktop cache, and the window shows the green of the pages.
# A HOME and XDG directories of its own (the system's thumbnailer entries are read, then lhde's from share/thumbnailers).
# Needs Xvfb, xdotool, ImageMagick and pdftoppm (or mutool); prints "skip: ..." and passes without them.
# Usage: tests/headless/thumbdocs.sh [:DISPLAY]
D=${1:-:76}
TOP=$(cd "$(dirname "$0")/../.." && pwd)
B=$TOP/build/bin/flxp
for t in Xvfb xdotool; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
if command -v magick >/dev/null 2>&1; then IM=magick; elif command -v convert >/dev/null 2>&1; then IM=convert; else echo "skip: needs ImageMagick"; exit 0; fi
command -v pdftoppm >/dev/null 2>&1 || command -v mutool >/dev/null 2>&1 || { echo "skip: needs pdftoppm or mutool"; exit 0; }
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
F=$T/documents
mkdir -p $T/home/.config/lhde "$F"
$IM -size 425x550 xc:white -fill 'rgb(0,200,0)' -draw 'rectangle 40,40 385,400' $T/page.png || { echo "FAIL ImageMagick could not draw the page"; exit 1; }
$IM $T/page.png "$F/a page.pdf" || { echo "FAIL ImageMagick could not write the PDF"; exit 1; }
docs="a page.pdf"
if command -v gs >/dev/null 2>&1; then $IM $T/page.png "$F/b page.ps" && docs="$docs|b page.ps"; fi
if command -v ddjvu >/dev/null 2>&1 && command -v c44 >/dev/null 2>&1 && command -v pdftoppm >/dev/null 2>&1; then
    $IM $T/page.png $T/page.ppm && c44 $T/page.ppm "$F/c page.djvu" >/dev/null 2>&1 && docs="$docs|c page.djvu"
fi
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D FLXP_TRACE_THUMBS=1 TMPDIR=$T
unset LHDE_DATA LHDE_SETTINGS LHDE_WINDOWS
printf '[flxp]\nthumbnails = 1\nthumbnail_size = 128\ndefault_view = thumbnails\n' > $T/home/.config/lhde/flxp.ini
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
fails=0
"$B" --geometry 800x600+0+0 "$F" > $T/out 2>&1 & P=$!
W=$(wait_window 30 --class Flxp)
n=$(echo "$docs" | tr '|' '\n' | wc -l)
made() { [ "$(grep -c '^thumbs: decode .* by .* at [1-9]' $T/out)" -ge $n ]; }
if [ -z "$W" ]; then
    echo "FAIL no Explorer window"; fails=1
else
    wait_for 60 made
    sleep 1   # the view draws them
    import -display $D -window root $T/shot.png 2>/dev/null || xwd -root -silent | $IM xwd:- $T/shot.png
fi
kill $P 2>/dev/null; wait $P 2>/dev/null
stop_xvfb
echo "$docs" | tr '|' '\n' > $T/docs
while read -r doc; do
    line=$(grep "^thumbs: decode $F/$doc by " $T/out | head -n 1)
    if echo "$line" | grep -q ' at [1-9][0-9]*x[1-9]'; then echo "ok   $doc: ${line#thumbs: decode $F/}"
    else echo "FAIL $doc: ${line:-no thumbnailer ran}"; fails=$((fails + 1)); fi
done < $T/docs
cached=$(ls $T/home/.cache/thumbnails/normal 2>/dev/null | grep -c '\.png$')
if [ "$cached" -ge "$n" ]; then echo "ok   $cached thumbnails in the cache's normal/"
else echo "FAIL $cached thumbnails in the cache's normal/ (want $n)"; fails=$((fails + 1)); fi
# the green of the pages: at least each thumbnail's block (about 60 x 70 at 128 px) mostly
green=0
[ -f $T/shot.png ] && green=$($IM $T/shot.png -fuzz 8% -fill black +opaque 'rgb(0,200,0)' -fill white -opaque 'rgb(0,200,0)' \
                                -format '%[fx:round(mean*w*h)]' info: 2>/dev/null)
if [ "${green:-0}" -ge $((n * 2500)) ]; then echo "ok   $green green pixels in the window ($n pages)"
else echo "FAIL ${green:-0} green pixels in the window (want $((n * 2500)) for $n pages)"; fails=$((fails + 1)); fi
[ $fails = 0 ] && echo "PASS ($n documents)" || { echo FAIL; exit 1; }
