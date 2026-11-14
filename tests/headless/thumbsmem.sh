#!/bin/sh
# flxp's memory on a big folder of big pictures (docs/USAGE.md, thumbnails): 600 6000 x 4000 JPEGs (hard links of one
# picture), 4 PNGs of 4000 x 4000, 30 PDFs (copies of one page; made by lhde-pdf.thumbnailer's pdftoppm, or mutool, where
# installed: all 30 are made) and a subfolder of 40, in the XP frame's Thumbnails view at 256 px with a cold thumbnail
# cache; the list is paged through to the end and back, the subfolder opened and left. The peak RSS (sampled every
# 0.2 s) stays under 400 MB, the view's images (FLXP_TRACE_THUMBS) within their 64 MB budget plus what is on screen, and
# images were evicted on the way. A HOME and XDG directories of its own. Needs Xvfb, xdotool and ImageMagick; prints
# "skip: ..." and passes without them. Usage: tests/headless/thumbsmem.sh [:DISPLAY]
D=${1:-:75}
LIMIT_MB=400
TOP=$(cd "$(dirname "$0")/../.." && pwd)
B=$TOP/build/bin/flxp
for t in Xvfb xdotool; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
if command -v magick >/dev/null 2>&1; then IM=magick; elif command -v convert >/dev/null 2>&1; then IM=convert; else echo "skip: needs ImageMagick"; exit 0; fi
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
F=$T/pictures
mkdir -p $T/home/.config/lhde "$F/album"
$IM -size 6000x4000 gradient:navy-orange -quality 85 $T/big.jpg || { echo "FAIL ImageMagick could not write the JPEG"; exit 1; }
$IM -size 4000x4000 gradient:green-white $T/big.png || { echo "FAIL ImageMagick could not write the PNG"; exit 1; }
n=0; while [ $n -lt 600 ]; do ln $T/big.jpg "$F/$(printf %03d $n).jpg"; n=$((n + 1)); done
for n in 0 1 2 3; do ln $T/big.png "$F/png$n.png"; done
pdfs=0
if command -v pdftoppm >/dev/null 2>&1 || command -v mutool >/dev/null 2>&1; then
    $IM -size 850x1100 gradient:white-steelblue $T/page.pdf || { echo "FAIL ImageMagick could not write the PDF"; exit 1; }
    n=0; while [ $n -lt 30 ]; do cp $T/page.pdf "$F/pdf$(printf %02d $n).pdf"; n=$((n + 1)); done
    pdfs=30
fi
n=0; while [ $n -lt 40 ]; do ln $T/big.jpg "$F/album/$(printf %02d $n).jpg"; n=$((n + 1)); done
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D FLXP_TRACE_THUMBS=1 TMPDIR=$T
unset LHDE_DATA LHDE_SETTINGS LHDE_WINDOWS
printf '[flxp]\nthumbnails = 1\nthumbnail_size = 256\ndefault_view = thumbnails\n' > $T/home/.config/lhde/flxp.ini
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1280x1024x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
fails=0
"$B" --geometry 1100x900+0+0 "$F" > $T/out 2>&1 & P=$!
# the peak RSS, sampled until flxp exits; killed above 1.5 GB (the machine's sake)
( peak=0
  while r=$(ps -o rss= -p $P 2>/dev/null) && [ -n "$r" ]; do
      [ $r -gt $peak ] && peak=$r && echo $peak > $T/peak
      [ $r -gt 1500000 ] && { echo "killed at $((r / 1024)) MB" > $T/killed; kill -9 $P; }
      sleep .2
  done ) & M=$!
W=$(wait_window 30 --class Flxp)
made() { grep -c '^thumbs: decode' $T/out; }
documents() { grep -c '^thumbs: decode .* by .* at [1-9]' $T/out; }
settle() {   # until no thumbnail was made for a second (or 60 s)
    _i=0; _m=-1
    while [ $_i -lt 60 ] && [ "$(made)" != "$_m" ]; do _m=$(made); sleep 1; _i=$((_i + 1)); done
}
if [ -z "$W" ]; then
    echo "FAIL no Explorer window"; fails=1
else
    xdotool windowfocus $W; sleep .5
    xdotool mousemove --window $W 600 500 click 1; sleep .3
    settle
    i=0; while [ $i -lt 120 ]; do xdotool key Next; sleep .15; i=$((i + 1)); done
    settle
    xdotool key Home; settle
    xdotool key End; settle
    xdotool key Home; sleep .3; xdotool key Return; settle   # the subfolder (first by name), then back
    xdotool key alt+Left; settle
fi
kill $P 2>/dev/null; wait $P 2>/dev/null; wait $M 2>/dev/null
stop_xvfb
peak=$(cat $T/peak 2>/dev/null || echo 0)
[ -f $T/killed ] && { echo "FAIL $(cat $T/killed)"; fails=$((fails + 1)); }
if [ $peak -lt $((LIMIT_MB * 1024)) ]; then echo "ok   peak RSS $((peak / 1024)) MB (under $LIMIT_MB MB)"
else echo "FAIL peak RSS $((peak / 1024)) MB (limit $LIMIT_MB MB)"; fails=$((fails + 1)); fi
# the view's images: at most the budget plus what is on screen (24 at 256 px in this window, 3 pages near)
most=$(sed -n 's/^thumbs: view \([0-9]*\) images.*/\1/p' $T/out | sort -n | tail -n 1)
evicted=$(sed -n 's/.* \([0-9]*\) evicted.*/\1/p' $T/out | sort -n | tail -n 1)
decoded=$(made)
if [ -n "$most" ] && [ $most -le 300 ]; then echo "ok   at most $most images held (256 in 64 MB, plus the ones near)"
else echo "FAIL images held: ${most:-none}"; fails=$((fails + 1)); fi
if [ -n "$evicted" ] && [ $evicted -gt 0 ]; then echo "ok   $evicted evicted"; else echo "FAIL nothing evicted"; fails=$((fails + 1)); fi
if [ $decoded -gt 100 ]; then echo "ok   $decoded decoded"; else echo "FAIL only $decoded decoded"; fails=$((fails + 1)); fi
if [ $pdfs = 0 ]; then echo "skip the PDFs: no pdftoppm or mutool"
elif [ "$(documents)" -ge $pdfs ]; then echo "ok   $(documents) PDF pages made by a thumbnailer"
else echo "FAIL $(documents) of the $pdfs PDF pages made"; fails=$((fails + 1)); fi
grep -m1 '^thumbs: decode .*6000x4000' $T/out | sed 's/^/     /'
grep -m1 '^thumbs: decode .*4000x4000' $T/out | sed 's/^/     /'
[ $fails = 0 ] && echo "PASS (peak $((peak / 1024)) MB)" || { echo FAIL; exit 1; }
