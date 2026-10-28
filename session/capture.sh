#!/bin/sh
# capture.sh [SECONDS] [OUTDIR]: grab the root window of $DISPLAY as fast as `import` allows (~25-40 ms per frame) for
# SECONDS (default 2) into OUTDIR (default /tmp/lhcap), after a 3 s countdown so you can get the pointer/keys ready.
# Used to show me what really happens on the real display (glx picom): run it, then do the thing (e.g. Super+Down to
# restore a maximised window) when it says GO. Frames are OUTDIR/f-<ms>.png; `magick montage` them or tell me the dir.
S=${1:-2}; D=${2:-/tmp/lhcap}; mkdir -p "$D"; rm -f "$D"/f-*.png
for i in 3 2 1; do echo "$i..."; sleep 1; done; echo GO
t0=$(date +%s%N); end=$((t0 + S * 1000000000))
while [ "$(date +%s%N)" -lt "$end" ]; do
	ms=$(( ( $(date +%s%N) - t0 ) / 1000000 ))
	import -window root "$D/$(printf 'f-%05d.png' "$ms")"
done
echo "$(ls "$D" | wc -l) frames in $D"
