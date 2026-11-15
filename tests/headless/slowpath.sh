#!/bin/sh
# flxp on a slow network mount (docs/USAGE.md "How the Explorer lists a folder", "Thumbnails"): 2000 JPEGs of 1600 x 1200
# (about 1 MB each, ten pictures hard-linked 200 times, each with its own 160 x 120 EXIF thumbnail in IFD1; one has
# Orientation 6) in a scratch folder, shown in the Longhorn frame's Icons view at 96 px with a cold thumbnail cache and
# thumbnails_network_budget = 8 (MB). First as a local folder: the list complete with its stats at once (no "listed:"
# lines), the thumbnails made by full decodes, no EXIF stage; then under tests/headless/slowfs.c, which makes the folder
# as slow as an NFS mount at 50 ms a round trip (LHDE_SLOW_REMOTE: statfs says NFS, so flxp takes it for a network one):
#   - the list is painted from readdir alone (FLXP_TRACE_LIST's "list: 2000 of 2000") at most 2 s later after the
#     start than the local one, where stat'ing every entry first would take 2000 round trips and more (100 s and more)
#   - the stats come in afterwards ("listed: N of M"), all 2000 within 60 s
#   - EXIF thumbnails appear progressively: the first within 10 s, at least 20 before the first full decode
#   - the byte budget holds: the files read past their first 64 KB (full decodes) add up to at most 8 MB plus one file,
#     counted from the shim's own log of the bytes read per open; at least 20 read for their EXIF thumbnail alone
# Prints the measured times. A HOME and XDG directories of its own. Needs Xvfb, xdotool and python3 with PIL; prints
# "skip: ..." and passes without them. Usage: tests/headless/slowpath.sh [:DISPLAY]
D=${1:-:73}
TOP=$(cd "$(dirname "$0")/../.." && pwd)
B=$TOP/build/bin/flxp
SHIM=$TOP/build/tests/slowfs.so
N=${SLOWPATH_N:-2000}
BUDGET_MB=8
for t in Xvfb xdotool python3; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
python3 -c 'import PIL' 2>/dev/null || { echo "skip: needs python3's PIL"; exit 0; }
[ -f "$SHIM" ] || { echo "FAIL no $SHIM (make tests)"; exit 1; }
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
F=$T/net/photos
mkdir -p $T/home/.config/lhde "$F"
# the pictures: a JPEG with an APP1 Exif block of IFD0 (Orientation) and IFD1 (the thumbnail as JPEGInterchangeFormat)
python3 - "$F" $N <<'EOF' || { echo "FAIL could not make the pictures"; exit 1; }
import io, os, struct, sys
from PIL import Image
def exif_jpeg(main, thumb, orientation):
    t = io.BytesIO(); thumb.save(t, 'JPEG', quality=80); tb = t.getvalue()
    ifd0 = 8; ifd1 = ifd0 + 2 + 12 + 4; data = ifd1 + 2 + 3 * 12 + 4
    tiff = b'II*\0' + struct.pack('<I', ifd0)
    tiff += struct.pack('<H', 1) + struct.pack('<HHIHH', 0x0112, 3, 1, orientation, 0) + struct.pack('<I', ifd1)
    tiff += struct.pack('<H', 3) + struct.pack('<HHIHH', 0x0103, 3, 1, 6, 0) + struct.pack('<HHII', 0x0201, 4, 1, data) \
            + struct.pack('<HHII', 0x0202, 4, 1, len(tb)) + struct.pack('<I', 0)
    tiff += tb
    app1 = b'\xff\xe1' + struct.pack('>H', 2 + 6 + len(tiff)) + b'Exif\0\0' + tiff
    m = io.BytesIO(); main.save(m, 'JPEG', quality=90); mb = m.getvalue()
    return mb[:2] + app1 + mb[2:]
out, n, kinds = sys.argv[1], int(sys.argv[2]), 10
bases = []
for k in range(kinds):
    w, h = 1600, 1200
    g = Image.linear_gradient('L').resize((w, h)).convert('RGB')
    noise = Image.frombytes('RGB', (w, h), os.urandom(w * h * 3))
    img = Image.blend(g, noise, 0.35)
    img.paste((40 + 20 * k, 90, 200 - 15 * k), (100, 100, 700, 500))
    th = img.copy(); th.thumbnail((160, 120))
    p = os.path.join(os.path.dirname(out), 'base%02d.jpg' % k)
    open(p, 'wb').write(exif_jpeg(img, th, 6 if k == 0 else 1))
    bases.append(p)
for i in range(n):
    os.link(bases[i % kinds], os.path.join(out, 'IMG_%04d.jpg' % i))
EOF
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D FLXP_TRACE_LIST=1 FLXP_TRACE_THUMBS=1 FLXP_LHWM=1 TMPDIR=$T
unset LHDE_DATA LHDE_SETTINGS LHDE_WINDOWS
printf '[flxp]\nthumbnails = 1\nthumbnails_network = 1\nthumbnails_network_budget = %s\ndefault_view_longhorn = icons@96\ntaskpane = 0\n' \
    $BUDGET_MB > $T/home/.config/lhde/flxp.ini
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1280x1024x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
fails=0
ok() { echo "ok   $*"; }
bad() { echo "FAIL $*"; fails=$((fails + 1)); }
now() { python3 -c 'import time; print(int(time.monotonic() * 1000))'; }
# the time (ms after flxp was started) at which a pattern first appears in its output: polled every 50 ms up to SECS
first_at() {   # SECS PATTERN
    _end=$(( $(now) + $1 * 1000 ))
    while [ $(now) -lt $_end ]; do
        grep -q "$2" $T/out 2>/dev/null && { echo $(( $(now) - T0 )); return 0; }
        sleep .05
    done
    return 1
}
# until no thumbnail line came for 3 s (or 60 s)
settle() {
    _i=0; _m=-1
    while [ $_i -lt 20 ] && [ "$(grep -c '^thumbs: ' $T/out)" != "$_m" ]; do _m=$(grep -c '^thumbs: ' $T/out); sleep 3; _i=$((_i + 1)); done
}

# ---- the folder as a local one: listed and decoded as it always was (and the time flxp takes to start) ----
T0=$(now)
"$B" --geometry 1100x900+0+0 "$F" > $T/out 2>&1 & P=$!
if local=$(first_at 30 "^list: $N of $N"); then ok "local: the list of $N painted $local ms after the start"
else bad "local: no list"; local=0; fi
first_at 30 '^thumbs: decode ' >/dev/null && ok "local: thumbnails decoded" || bad "local: no thumbnail decoded"
settle
grep -q '^listed: ' $T/out && bad "local: stats filled in afterwards (the list should be complete at once)" || ok "local: the list complete at once"
grep -q '^thumbs: exif ' $T/out && bad "local: EXIF thumbnails used" || ok "local: no EXIF stage"
kill $P 2>/dev/null; wait $P 2>/dev/null
rm -rf $T/home/.cache/thumbnails

# ---- under the shim: a network folder at 50 ms a round trip ----
T0=$(now)
LD_PRELOAD=$SHIM LHDE_SLOW_PREFIX=$T/net LHDE_SLOW_MS=50 LHDE_SLOW_REMOTE=1 LHDE_SLOW_LOG=$T/slow.log \
    "$B" --geometry 1100x900+0+0 "$F" > $T/out 2>&1 & P=$!
if t=$(first_at ${SLOWPATH_LIST_WAIT:-30} "^list: $N of $N"); then
    # what the slow folder adds to the start: the folder's stat and statfs, readdir's round trips (16 at 128 entries
    # each), the parent folder's entries for the tree
    [ $((t - local)) -le 2000 ] && ok "the list of $N painted $t ms after the start ($((t - local)) ms more than local)" \
        || bad "the list of $N painted only $t ms after the start ($((t - local)) ms more than local)"
else bad "no list of $N within ${SLOWPATH_LIST_WAIT:-30} s"; fi
if e=$(first_at 10 '^thumbs: exif '); then ok "the first EXIF thumbnail $e ms after the start ($((e - t)) ms after the list)"
else bad "no EXIF thumbnail within 10 s"; fi
if s=$(first_at 60 "^listed: $N of $N"); then ok "every entry stat'ed $s ms after the start"
else bad "the stats did not all come in within 60 s ($(sed -n 's/^listed: //p' $T/out | tail -n 1))"; fi
settle
exif=$(grep -c '^thumbs: exif ' $T/out)
decodes=$(grep -c '^thumbs: decode ' $T/out)
before=$(sed -n '/^thumbs: decode /q; /^thumbs: exif /p' $T/out | wc -l)
[ $before -ge 20 ] && ok "$before EXIF thumbnails before the first full decode ($exif in all, $decodes full decodes)" \
    || bad "only $before EXIF thumbnails before the first full decode ($exif in all, $decodes full decodes)"
grep -q '^thumbs: exif .* turned 6' $T/out && ok "the Orientation 6 picture's EXIF thumbnail turned" || bad "no EXIF thumbnail turned by its Orientation"
kill $P 2>/dev/null; wait $P 2>/dev/null
# the bytes read per open of a picture (the shim's close lines): past 64 KB is a full read
full=$(awk '$2 == "close" && $3 ~ /\.jpg$/ && $4 > 65536 { n++; b += $4 } END { printf "%d %d", n, b }' $T/slow.log)
fulln=${full% *}; fullb=${full#* }
small=$(awk '$2 == "close" && $3 ~ /\.jpg$/ && $4 <= 65536 { n++ } END { print n + 0 }' $T/slow.log)
size=$(wc -c < "$F/IMG_0000.jpg")
limit=$((BUDGET_MB * 1048576 + size))
[ $fullb -le $limit ] && ok "$fulln full reads, $((fullb / 1024)) KB, within the budget of $BUDGET_MB MB" \
    || bad "$fulln full reads of $((fullb / 1024)) KB: past the budget of $BUDGET_MB MB"
[ $small -ge 20 ] && ok "$small pictures read for their EXIF thumbnail alone (at most 64 KB each)" || bad "only $small EXIF reads"
echo "     through the shim: $(grep -c ' stat \| lstat \| statx ' $T/slow.log) stat calls, $(grep -c ' open ' $T/slow.log) opens," \
     "$(awk '$2 == "close" && $3 ~ /\.jpg$/ { b += $4 } END { printf "%d", b / 1024 }' $T/slow.log) KB of pictures read"
stop_xvfb
[ $fails = 0 ] && echo PASS || { echo FAIL; exit 1; }
