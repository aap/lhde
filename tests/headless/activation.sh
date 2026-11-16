#!/bin/sh
# A focus change paints each side once, in step (docs/CLIENT-PROTOCOL.md "Activation in step"): two flxp windows side by side under
# luxwm (and picom xrender with the session's picom-glass.conf when installed), each Longhorn theme in turn (ACTIVATION_THEMES,
# default jade4074 slate4074 aerovision; a theme whose style is not imported is skipped). The windows are activated in turn,
# with xdotool windowactivate and with a click, and once more each while already active, while build/tests/activewatch reads in
# a tight loop a point of each window's frame caption (luxwm's decoration window; on a CLIENT_CAPTION header the glass under
# it), of its header band (the client window; none where the header looks the same in both states, as aerovision's), and the
# same points on the screen. Per switch and window:
#   - the frame and the band change at most once, from the old state to the new (no active - inactive - active blink), and not
#     at all when the window keeps its state;
#   - luxwm paints the frame after the client's _LHWM_ACTIVE_DRAWN says the header shows the new state (the property change and
#     the frame's pixels are read on one connection, so their order is the server's; the band's paint before the property is
#     not: the client sets it right after presenting, faster than a probe round);
#   - on the screen the caption and the band of a window never disagree for 10 ms or more, and change at most once.
# Activation states are told apart by the colours each probe has in the two settled states. Needs Xvfb, xdotool, xprop,
# xwininfo, python3 and an imported Longhorn style; prints "skip: ..." and passes without them. Usage: tests/headless/activation.sh [:DISPLAY]
D=${1:-:80}
R=$(cd "$(dirname "$0")/../.." && pwd)
AW=$R/build/tests/activewatch
[ -x "$AW" ] || { echo "skip: build/tests/activewatch not built (make tests)"; exit 0; }
for t in Xvfb xdotool xprop xwininfo python3; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
styles=$("$R/build/bin/lhde-theme" find styles 2>/dev/null)
[ -n "$styles" ] || { echo "skip: no imported styles (lhde-import)"; exit 0; }
T=$(mktemp -d); [ -n "$ACTIVATION_KEEP" ] && echo "kept: $T"
trap '[ -n "$ACTIVATION_KEEP" ] || rm -rf "$T"' EXIT
mkdir -p "$T/home/.config/lhde" "$T/left" "$T/right"
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D LHDE_WINDOWS=${styles%/index/styles}
unset LHDE_DATA LHDE_SETTINGS
. "$(dirname "$0")/wait.sh"
fails=0
bad() { echo "FAIL $1"; fails=$((fails + 1)); }

start_xvfb $D -screen 0 1280x800x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
"$R/build/bin/luxwm" > "$T/luxwm.log" 2>&1 & PW=$!
wm_up() { xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'; }
wait_for 20 wm_up || echo "note: luxwm did not announce itself"
PP=
if command -v picom >/dev/null 2>&1; then
    picom --config "$R/session/picom-glass.conf" --backend xrender --blur-method gaussian --blur-size 5 --blur-deviation 1.2 \
        > "$T/picom.log" 2>&1 & PP=$!
    sleep 1
    kill -0 $PP 2>/dev/null && echo "note: picom runs" || { PP=; echo "note: picom did not start: the screen is the windows' own pixels"; }
else
    echo "note: no picom: the screen is the windows' own pixels"
fi

mark() { xprop -root -f _LHTEST_MARK 32c -set _LHTEST_MARK "$1"; }
# probes WIN: activewatch's probes of a window, named after $2 (L or R)
probes() {
    echo "${2}cap:f:$1:$PX:$CY ${2}cap_s:r:$1:$PX:$CY"
    [ -n "$BY" ] && echo "${2}band:c:$1:$PX:$BY ${2}band_s:r:$1:$PX:$BY"
}

for theme in ${ACTIVATION_THEMES:-jade4074 slate4074 aerovision}; do
    name=$("$R/build/bin/lhde-theme" resolve "$theme" 2>/dev/null | sed -n 's/^theme=//p' | head -1)
    style=$("$R/build/bin/lhde-theme" resolve "$theme" 2>/dev/null | sed -n 's/^path.style=//p' | head -1)
    if [ -z "$name" ] || { [ -n "$style" ] && [ ! -f "$style" ]; }; then echo "skip $theme: not available here"; continue; fi
    printf '[theme]\nname = %s\n' "$theme" > "$T/home/.config/lhde/settings.ini"
    xprop -root -f _LH_THEME 8u -set _LH_THEME "$theme"
    sleep 1
    "$R/build/bin/flxp" "$T/left" > "$T/left-$theme.log" 2>&1 & P1=$!
    WL=$(wait_window 20 --name '^left$')
    "$R/build/bin/flxp" "$T/right" > "$T/right-$theme.log" 2>&1 & P2=$!
    WR=$(wait_window 20 --name '^right$')
    if [ -z "$WL" ] || [ -z "$WR" ]; then bad "$theme: the flxp windows did not appear"; kill $P1 $P2 2>/dev/null; wait $P1 $P2 2>/dev/null; continue; fi
    xdotool windowsize --sync $WL 600 480 windowmove --sync $WL 20 60
    xdotool windowsize --sync $WR 600 480 windowmove --sync $WR 660 60
    xdotool mousemove 640 790
    sleep 2.5
    # the probes: the middle column; the caption's middle row above the client, or the glass 20 rows into a CLIENT_CAPTION
    # header; the band's 5th row (none on glass: the header is the same in both states there)
    PX=300
    flags=$(xprop -id $WL _LHWM_FRAME_FLAGS | sed -n 's/.*= //p')
    top=$(xprop -id $WL _NET_FRAME_EXTENTS | sed -n 's/.*= [0-9]*, [0-9]*, \([0-9]*\),.*/\1/p')
    if [ $(( ${flags:-0} & 8 )) != 0 ]; then CY=20 BY=; else CY=$(( -${top:-24} / 2 )) BY=5; fi
    P="$(probes $WL L) $(probes $WR R)"
    # the settled colours: left active, then right active
    xdotool windowactivate --sync $WL; sleep 1.5
    "$AW" 0.3 $P > "$T/$theme-ref-L.log"
    xdotool windowactivate --sync $WR; sleep 1.5
    "$AW" 0.3 $P > "$T/$theme-ref-R.log"
    # the switches (the active window after each): R activated again (R), a click on L (L) and again (L), a click on R (R),
    # L activated (L)
    "$AW" 14 $P > "$T/$theme-run.log" & PA=$!
    sleep .5
    mark 1; xdotool windowactivate $WR; sleep 2
    xdotool mousemove 320 400; sleep .5
    mark 2; xdotool click 1; sleep 2
    mark 3; xdotool click 1; sleep 2
    xdotool mousemove 960 400; sleep .5
    mark 4; xdotool click 1; sleep 2
    mark 5; xdotool windowactivate $WL; sleep 2
    xdotool mousemove 640 790
    wait $PA
    python3 -I - "$theme" "$WL" "$WR" "$T/$theme-ref-L.log" "$T/$theme-ref-R.log" "$T/$theme-run.log" "R L L R L" <<'EOF' || fails=$((fails + 1))
import sys
theme, wl, wr, refl, refr, run, after = sys.argv[1:8]
wid = {'L': int(wl), 'R': int(wr)}
after = after.split()
def lines(p):
    out = []
    for ln in open(p):
        f = ln.split()
        if len(f) >= 3: out.append((float(f[0]), f[1], f[2:]))
    return out
def settled(p):
    v = {}
    for t, k, a in lines(p):
        if k == 'probe': v[a[0]] = a[1]
    return v
ref = {'L': settled(refl), 'R': settled(refr)}   # the settled colours with L active, with R active
def cls(probe, val):
    w = probe[0]
    act, ina = ref[w].get(probe), ref['R' if w == 'L' else 'L'].get(probe)
    return 'A' if val == act else 'I' if val == ina else '?'
probes = sorted(ref['L'])
ok = True
def bad(msg):
    global ok; ok = False; print('FAIL %s: %s' % (theme, msg))
for p in probes:
    if ref['L'][p] == ref['R'][p]: bad('probe %s has the same colour (%s) in both states: no point to tell them apart' % (p, ref['L'][p]))
if not ok: sys.exit(1)
ev = lines(run)
segs, cur = [], None
for i, (t, k, a) in enumerate(ev):
    if k == 'prop' and a[1] == '_LHTEST_MARK': cur = []; segs.append(cur)
    elif cur is not None: cur.append((i, t, k, a))
if len(segs) != len(after): bad('%d switches recorded, %d made' % (len(segs), len(after))); sys.exit(1)
state = {}
for t, k, a in ev:
    if k == 'probe' and a[0] not in state: state[a[0]] = a[1]
before = 'R'   # the settled state the run starts from
for n, seg in enumerate(segs):
    now = after[n]
    name = 'switch %d (%s active -> %s active)' % (n + 1, before, now)
    for w in 'LR':
        old, new = ('A' if before == w else 'I'), ('A' if now == w else 'I')
        first = {}
        for kind in ('cap', 'band'):
            p = w + kind
            if p not in ref['L']: continue
            ch = [(i, t, cls(p, a[1])) for i, t, k, a in seg if k == 'probe' and a[0] == p]
            want = [] if old == new else [new]
            if [c for _, _, c in ch] != want:
                bad('%s, window %s: %s went %s, expected %s' % (name, w, 'luxwm\'s frame' if kind == 'cap' else 'the header band',
                    ' -> '.join([old] + [c for _, _, c in ch]), ' -> '.join([old] + want)))
            if ch: first[kind] = ch[0][0]
        if old != new:
            acks = [i for i, t, k, a in seg if k == 'prop' and a[1] == '_LHWM_ACTIVE_DRAWN' and int(a[0], 16) == wid[w]]
            if not acks: bad('%s, window %s: flxp published no _LHWM_ACTIVE_DRAWN' % (name, w))
            elif 'cap' in first and first['cap'] < acks[0]: bad('%s, window %s: luxwm painted the frame before the header said it follows' % (name, w))
        # the screen: caption and band agree (a disagreement shorter than 10 ms is a compositor caught mid-paint), one change
        cs, bs = w + 'cap_s', w + 'band_s'
        c, b = state.get(cs), state.get(bs)
        since = None
        for i, t, k, a in seg + [(None, seg[-1][1] + 1000 if seg else 0, 'end', [])]:
            if k == 'probe' and a[0] == cs: c = a[1]
            elif k == 'probe' and a[0] == bs: b = a[1]
            elif k != 'end': continue
            dis = bs in ref['L'] and cls(cs, c) != cls(bs, b)
            if dis and since is None: since = t
            if (not dis or k == 'end') and since is not None:
                if t - since >= 10: bad('%s, window %s: on the screen the caption and the band disagreed for %.0f ms' % (name, w, t - since))
                since = None
        # the caption's states on the screen, those held for less than 10 ms dropped (a compositor caught mid-paint)
        held = [old]
        cap_ev = [(t, cls(cs, a[1])) for i, t, k, a in seg if k == 'probe' and a[0] == cs]
        for j, (t, s) in enumerate(cap_ev):
            nxt = cap_ev[j + 1][0] if j + 1 < len(cap_ev) else t + 1000
            if nxt - t >= 10 and held[-1] != s: held.append(s)
        if held != ([old] if old == new else [old, new]):
            bad('%s, window %s: the caption on the screen went %s' % (name, w, ' -> '.join(held)))
        for i, t, k, a in seg:
            if k == 'probe': state[a[0]] = a[1]
    before = now
if ok: print('ok   %s: %d switches, each side painted once, in step' % (theme, len(segs)))
sys.exit(0 if ok else 1)
EOF
    kill $P1 $P2 2>/dev/null; wait $P1 $P2 2>/dev/null
    sleep .5
done
kill $PP $PW 2>/dev/null; wait $PP $PW 2>/dev/null
stop_xvfb
[ $fails = 0 ] && echo PASS || { echo "FAIL ($fails)"; exit 1; }
