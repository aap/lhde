#!/bin/sh
# Drag and drop (XDND) between two explorer processes and with a GTK program:
# a plain drag moves (same filesystem), Shift copies, Ctrl+Shift links, Escape
# cancels; a file from flxp dropped on a GTK3 drop target, a file dragged from
# GTK3 into flxp. The drags are xdotool's (XTest), each starting with a small
# step inside the list (the list sees the drag threshold only while the pointer
# is over it). Needs Xvfb, xdotool; python3-gi (GTK 3) for the GTK part.
# Usage: tests/headless/dnd.sh [:DISPLAY]; FLXP_DEBUG_DND=1 traces the protocol.
D=${1:-:72}
T=$(mktemp -d)
B=$(cd "$(dirname "$0")/../.." && pwd)/build/bin/flxp
mkdir -p $T/home/.config $T/src $T/dst
for f in alpha beta gamma delta; do echo $f >$T/src/$f.txt; done
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D   # the scratch home only: nothing exported from outside leaks in
Xvfb $D -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 & XP=$!
sleep 1
$B --geometry 600x500+0+0 $T/src 2>$T/e1.log & P1=$!
$B --geometry 600x500+650+0 $T/dst 2>$T/e2.log & P2=$!
sleep 3
# details view: row n of the left list (sorted: alpha beta delta gamma, then without alpha)
row() { echo $((104 + 17 * $1)); }
drag() { # x1 y1 x2 y2 [keys held]
    xdotool mousemove $1 $2; sleep .2; xdotool mousedown 1; sleep .1
    xdotool mousemove $(($1 + 8)) $(($2 + 3)); sleep .1
    [ -n "$5" ] && xdotool keydown $5
    for f in 1 2 3 4 5 6 7 8; do xdotool mousemove $(($1 + ($3 - $1) * f / 8)) $(($2 + ($4 - $2) * f / 8)); sleep .08; done
    sleep .3; xdotool mouseup 1; [ -n "$5" ] && xdotool keyup $5; sleep .8
}
ok=1
check() { if eval "$2"; then echo "ok   $1"; else echo "FAIL $1"; ok=0; fi; }
drag 265 $(row 0) 1000 300;            check "move to the other window" "[ -e $T/dst/alpha.txt ] && [ ! -e $T/src/alpha.txt ]"
drag 265 $(row 0) 1000 300 shift;      check "Shift copies" "[ -e $T/dst/beta.txt ] && [ -e $T/src/beta.txt ]"
drag 265 $(row 1) 1000 300 ctrl+shift; check "Ctrl+Shift links" "[ -L $T/dst/delta.txt ]"
xdotool mousemove 265 $(row 2) mousedown 1; sleep .1; xdotool mousemove 273 $(($(row 2) + 3)); sleep .1
for f in 1 2 3 4; do xdotool mousemove $((265 + f * 150)) 300; sleep .08; done
xdotool key Escape; sleep .2; xdotool mouseup 1; sleep .8
check "Escape cancels" "[ ! -e $T/dst/gamma.txt ] && [ -e $T/src/gamma.txt ]"
if python3 -c 'import gi; gi.require_version("Gtk", "3.0")' 2>/dev/null; then
    cat >$T/gtkdnd.py <<'PY'
import sys, gi
gi.require_version("Gtk", "3.0"); gi.require_version("Gdk", "3.0")
from gi.repository import Gtk, Gdk, GLib
w = Gtk.Window(title="gtkdnd"); w.set_default_size(400, 200); w.move(0, 560)
box = Gtk.Box(); w.add(box)
t = [Gtk.TargetEntry.new("text/uri-list", 0, 0)]
acts = Gdk.DragAction.COPY | Gdk.DragAction.MOVE | Gdk.DragAction.LINK
drop = Gtk.Label(label="drop"); drop.set_size_request(200, 200); box.pack_start(drop, True, True, 0)
drop.drag_dest_set(Gtk.DestDefaults.ALL, t, acts)
drop.connect("drag-data-received", lambda w, c, x, y, d, i, tm: print("DROPPED", " ".join(d.get_uris()), flush=True))
src = Gtk.EventBox(); src.add(Gtk.Label(label="drag")); src.set_size_request(200, 200); box.pack_start(src, True, True, 0)
src.drag_source_set(Gdk.ModifierType.BUTTON1_MASK, t, acts)
src.connect("drag-data-get", lambda w, c, d, i, tm: d.set_uris([GLib.filename_to_uri(sys.argv[1], None)]))
w.connect("destroy", Gtk.main_quit); w.show_all(); Gtk.main()
PY
    echo g >$T/gfile.txt
    python3 -I $T/gtkdnd.py $T/gfile.txt >$T/gtk.log 2>/dev/null & G=$!
    sleep 2
    drag 915 $(row 0) 100 660
    check "flxp -> GTK (text/uri-list)" "grep -q 'DROPPED file://.*/dst/alpha.txt' $T/gtk.log"
    drag 300 660 1000 300
    check "GTK -> flxp" "[ -e $T/dst/gfile.txt ]"
    kill $G
fi
kill $P1 $P2 $XP
rm -rf $T
[ $ok = 1 ] && echo PASS || { echo FAIL; exit 1; }
