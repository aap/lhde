#!/bin/sh
# flxp's UI thread on a slow network mount makes no round trips once the list is up (docs/USAGE.md "How the Explorer
# lists a folder"): a folder like a photo share (150 subfolders of 30 small EXIF JPEGs each, and "pictures", a folder of
# 2000) under tests/headless/slowfs.c at 50 ms a round trip with LHDE_SLOW_REMOTE=1 (statfs says NFS), the shim's
# LHDE_SLOW_MAIN_LOG writing every call from the main thread with its stack. Twice, with a cold thumbnail cache each time:
#   - the Longhorn frame (FLXP_LHWM), Icons at 96 px with thumbnails: the folder pictures of the 150
#   - the XP frame with the Folders pane, Thumbnails: the tree's 150 subfolders under the open one
# Each time the list is scrolled to the end and back (keys and the wheel), an item hovered (its infotip) and one selected
# (the task band, the task pane or the status bar), then "pictures" opened, scrolled, hovered and selected.
# After the first paint ("list: 151 of 151") the main thread makes no stat, open, read, opendir or xattr call under the
# slow tree. Known exceptions, bounded: the opendir and readdir of a folder being opened (itself and the folder it is in,
# for the tree), and the stat of the folder opened (whether it still is one).
# The calls found are printed with their stacks (addr2line names the frames where it is installed). A HOME and XDG
# directories of its own. Needs Xvfb, xdotool and python3 with PIL; prints "skip: ..." and passes without them.
# Usage: tests/headless/slowpath-ui.sh [:DISPLAY]   (SLOWPATH_UI_KEEP=1 keeps the scratch directory and says where)
D=${1:-:73}
TOP=$(cd "$(dirname "$0")/../.." && pwd)
B=$TOP/build/bin/flxp
SHIM=$TOP/build/tests/slowfs.so
FOLDERS=${SLOWPATH_UI_FOLDERS:-150}
EACH=30
BIG=2000
ALLOWED=40          # the known exceptions' calls in a run: opendir + readdir (one per 128 entries) of the folders opened
for t in Xvfb xdotool python3; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
python3 -c 'import PIL' 2>/dev/null || { echo "skip: needs python3's PIL"; exit 0; }
[ -f "$SHIM" ] || { echo "FAIL no $SHIM (make tests)"; exit 1; }
T=$(mktemp -d)
if [ -n "$SLOWPATH_UI_KEEP" ]; then trap 'echo "kept $T"' EXIT; else trap 'rm -rf "$T"' EXIT; fi
F=$T/net/photos
mkdir -p $T/home/.config/lhde "$F"
# the pictures: 640 x 480 JPEGs with an APP1 Exif block whose IFD1 holds a 160 x 120 thumbnail, ten of them hard-linked
python3 - "$T" "$F" $FOLDERS $EACH $BIG <<'EOF' || { echo "FAIL could not make the pictures"; exit 1; }
import io, os, struct, sys
from PIL import Image
def exif_jpeg(main, thumb):
    t = io.BytesIO(); thumb.save(t, 'JPEG', quality=80); tb = t.getvalue()
    ifd0 = 8; ifd1 = ifd0 + 2 + 12 + 4; data = ifd1 + 2 + 3 * 12 + 4
    tiff = b'II*\0' + struct.pack('<I', ifd0)
    tiff += struct.pack('<H', 1) + struct.pack('<HHIHH', 0x0112, 3, 1, 1, 0) + struct.pack('<I', ifd1)
    tiff += struct.pack('<H', 3) + struct.pack('<HHIHH', 0x0103, 3, 1, 6, 0) + struct.pack('<HHII', 0x0201, 4, 1, data) \
            + struct.pack('<HHII', 0x0202, 4, 1, len(tb)) + struct.pack('<I', 0)
    tiff += tb
    app1 = b'\xff\xe1' + struct.pack('>H', 2 + 6 + len(tiff)) + b'Exif\0\0' + tiff
    m = io.BytesIO(); main.save(m, 'JPEG', quality=85); mb = m.getvalue()
    return mb[:2] + app1 + mb[2:]
top, out, folders, each, big = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
bases = []
for k in range(10):
    img = Image.linear_gradient('L').resize((640, 480)).convert('RGB')
    img.paste((40 + 20 * k, 90, 200 - 15 * k), (60, 60, 300, 260))
    th = img.copy(); th.thumbnail((160, 120))
    p = os.path.join(top, 'base%02d.jpg' % k)
    open(p, 'wb').write(exif_jpeg(img, th))
    bases.append(p)
for f in range(folders):
    d = os.path.join(out, 'album_%03d' % f)
    os.mkdir(d)
    for i in range(each): os.link(bases[(f + i) % 10], os.path.join(d, 'IMG_%04d.jpg' % i))
d = os.path.join(out, 'pictures')
os.mkdir(d)
for i in range(big): os.link(bases[i % 10], os.path.join(d, 'P_%04d.jpg' % i))
EOF
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D FLXP_TRACE_LIST=1 FLXP_TRACE_THUMBS=1 TMPDIR=$T
unset LHDE_DATA LHDE_SETTINGS LHDE_WINDOWS FLXP_LHWM
printf '[flxp]\nthumbnails = 1\nthumbnails_network = 1\ndefault_view_longhorn = icons@96\ndefault_view = thumbnails\n' \
    > $T/home/.config/lhde/flxp.ini
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1280x1024x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
fails=0
ok() { echo "ok   $*"; }
bad() { echo "FAIL $*"; fails=$((fails + 1)); }
now() { python3 -c 'import time; print(int(time.monotonic() * 1000))'; }
# the time (ms on the monotonic clock) at which a pattern first appears in flxp's output: polled every 50 ms up to SECS
seen_at() {   # SECS PATTERN
    _end=$(( $(now) + $1 * 1000 ))
    while [ $(now) -lt $_end ]; do
        grep -q "$2" $T/out 2>/dev/null && { now; return 0; }
        sleep .05
    done
    return 1
}
# until no thumbnail or listing line came for 2 s (or 40 s)
settle() {
    _i=0; _m=-1
    while [ $_i -lt 20 ] && [ "$(grep -c '^thumbs: \|^listed: ' $T/out)" != "$_m" ]; do
        _m=$(grep -c '^thumbs: \|^listed: ' $T/out); sleep 2; _i=$((_i + 1))
    done
}
# scroll to the end and back: End and Home, page by page, the wheel
scroll() {
    xdotool key End; sleep .5; xdotool key Home; sleep .5
    _i=0; while [ $_i -lt 8 ]; do xdotool key Next; sleep .2; _i=$((_i + 1)); done
    _i=0; while [ $_i -lt 8 ]; do xdotool key Prior; sleep .2; _i=$((_i + 1)); done
    xdotool mousemove --window $W $LX $LY
    _i=0; while [ $_i -lt 15 ]; do xdotool click 5; sleep .1; _i=$((_i + 1)); done
    _i=0; while [ $_i -lt 15 ]; do xdotool click 4; sleep .1; _i=$((_i + 1)); done
    xdotool key ctrl+End; sleep .5; xdotool key ctrl+Home; sleep .5
}
# hover the first item for its infotip, select the second (keys: the mouse would open a folder on a double click)
hover_select() {
    xdotool mousemove --window $W $LX $LY; sleep .2; xdotool mousemove --window $W $((LX + 2)) $((LY + 2)); sleep 2
    xdotool key Home; sleep .3; xdotool key Right; sleep 1; xdotool key Down; sleep 1
}

run() {   # NAME LIST-X LIST-Y (a point over the first item)
    name=$1; LX=$2; LY=$3
    rm -rf $T/home/.cache/thumbnails $T/main.log $T/slow.log
    T0=$(now)
    LD_PRELOAD=$SHIM LHDE_SLOW_PREFIX=$T/net LHDE_SLOW_MS=50 LHDE_SLOW_REMOTE=1 LHDE_SLOW_LOG=$T/slow.log LHDE_SLOW_MAIN_LOG=$T/main.log \
        "$B" --geometry 1100x900+0+0 "$F" > $T/out 2>&1 & P=$!
    W=$(wait_window 30 --class Flxp)
    n=$((FOLDERS + 1))
    if ! t=$(seen_at 30 "^list: $n of $n"); then bad "$name: no list of $n within 30 s"; kill $P; wait $P 2>/dev/null; return; fi
    paint=$((t - T0))
    ok "$name: the list of $n painted $paint ms after the start"
    xdotool windowfocus $W 2>/dev/null; sleep .3
    settle
    pics=$(grep -c '^thumbs: ' $T/out)
    ok "$name: $pics thumbnail lines after the list settled ($(( $(now) - T0 )) ms after the start)"
    scroll; hover_select; settle
    # open "pictures" (type-ahead selects it) and do the same there
    xdotool key Home; sleep .3; xdotool type --delay 80 pictures; sleep .5; xdotool key Return
    if seen_at 30 "^list: $BIG of $BIG" >/dev/null; then ok "$name: pictures opened, $BIG listed"
    else bad "$name: pictures not listed within 30 s"; fi
    settle; scroll; hover_select; settle
    kill $P 2>/dev/null; wait $P 2>/dev/null
    # the main thread's calls after the first paint (the shim's clock starts at its load, just after T0: a few ms early)
    awk -v after=$paint -v net=$T/net -v f="$F" '
        /^MAIN / { ms = $2 + 0; call = $3; path = $4; keep = ms > after
                   if (keep) {
                       allowed = (call == "opendir" || call == "readdir") && (path == f || path == f "/pictures" || path == net) ||
                                 call == "stat" && path == f "/pictures"
                       if (allowed) n_ok++; else { n_bad++; print "BAD " $0 }
                   }
                   next }
        keep && !allowed && /^  #/ { print }
        END { printf "COUNT %d %d\n", n_ok, n_bad }' $T/main.log > $T/main.$name
    set -- $(sed -n 's/^COUNT //p' $T/main.$name)
    allowed=$1 found=$2
    [ $allowed -le $ALLOWED ] && ok "$name: $allowed known calls from the main thread (opendir, readdir, stat of the folders opened)" \
        || bad "$name: $allowed opendir and readdir calls from the main thread (at most $ALLOWED)"
    if [ $found = 0 ]; then ok "$name: no other call under the slow tree from the main thread after the first paint"
    else
        bad "$name: $found calls under the slow tree from the main thread after the first paint:"
        # by call and stack: the count, the call, the first frames in flxp
        awk '/^BAD / { if (key != "") n[key]++; call = $4; key = call; depth = 0; next }
             /^  #/ && depth < 6 && $2 !~ /^lib(c|pthread|thr|sys)/ { key = key " | " $2 " " $4; depth++ }
             END { if (key != "") n[key]++; for (k in n) printf "%6d  %s\n", n[k], k }' $T/main.$name | sort -rn | head -n 20 |
        while read -r line; do echo "     $line"; done
        if command -v addr2line >/dev/null 2>&1; then
            # the frame's offset in a position-independent flxp, its address in one that is not
            col=2; readelf -h "$B" 2>/dev/null | grep -q 'Type:.*EXEC' && col=3
            awk -v c=$col '/^BAD / { f = 0; next } /^  #/ && $2 ~ /^flxp\+/ && f < 4 { a = $c; sub(/^flxp\+/, "", a); print a; f++ }' $T/main.$name |
                sort | uniq -c | sort -rn | head -n 12 | while read -r c a; do
                echo "     $c x $(addr2line -f -C -e "$B" $a 2>/dev/null | paste -s -d' ' -)"
            done
        fi
    fi
    echo "     through the shim: $(grep -c ' main$' $T/slow.log) calls from the main thread, $(wc -l < $T/slow.log) in all"
}

# the Longhorn frame (as under luxwm): Icons at 96 px with thumbnails
FLXP_LHWM=1; export FLXP_LHWM
run longhorn 60 120
unset FLXP_LHWM
# the XP frame with the Folders pane
FLXP_FOLDERS=1; export FLXP_FOLDERS
run xp 320 120
unset FLXP_FOLDERS
stop_xvfb
[ $fails = 0 ] && echo PASS || { echo FAIL; exit 1; }
