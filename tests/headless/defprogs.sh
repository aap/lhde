#!/bin/sh
# Default Programs (flxp --default-programs) on a scratch home and data directory under luxwm on Xvfb, in an lhde session
# (XDG_CURRENT_DESKTOP=lhde): two text editors installed, zzpad (lhde's default here: the first of the editor role's
# prefer= list in a user roles.ini) and zzedit; the dialog opens at the editor role with zzpad checked; Down picks zzedit,
# Apply writes settings.ini [programs] editor = zzedit.desktop and the role file's lines for text/plain and the text type
# below it (text/x-csrc, from the scratch database's subclasses); Folder Options' File Types then says TXT opens with zzedit,
# set by "your editor role (ZZ Edit)"; a choice made there for one type (Change...) goes to ~/.config/mimeapps.list and wins
# over the role. FLXP_TRACE_DEFPROGS and FLXP_TRACE_FILETYPES say what the dialogs show. Needs Xvfb, xdotool and xprop.
# Usage: tests/headless/defprogs.sh [:DISPLAY]
D=${1:-:78}
R=$(cd "$(dirname "$0")/../.." && pwd)
for t in Xvfb xdotool xprop; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p $T/home/.config/lhde/applications $T/home/.local/share/applications $T/usr/applications $T/usr/mime
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache XDG_DATA_DIRS=$T/usr XDG_CONFIG_DIRS=$T/etc DISPLAY=$D XDG_CURRENT_DESKTOP=lhde \
       FLXP_TRACE_DEFPROGS=1 FLXP_TRACE_FILETYPES=1
unset LHDE_DATA LHDE_SETTINGS
printf '50:text/plain:*.txt\n50:text/x-csrc:*.c\n50:image/png:*.png\n' > $T/usr/mime/globs2
printf 'text/x-csrc text/plain\n' > $T/usr/mime/subclasses
for p in zzpad:'ZZ Pad' zzedit:'ZZ Edit'; do
    printf '[Desktop Entry]\nType=Application\nName=%s\nExec=/bin/true %%f\nMimeType=text/plain;\nCategories=Utility;TextEditor;\n' \
        "${p#*:}" > $T/usr/applications/${p%%:*}.desktop
done
# the user's roles.ini over the shipped one: lhde's default editor here is zzpad
printf '[editor]\nprefer = zzpad.desktop\n' > $T/home/.config/lhde/applications/roles.ini
RF=$T/home/.local/share/applications/lhde-mimeapps.list
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
"$R/build/bin/luxwm" > $T/luxwm.log 2>&1 & PW=$!
wm_up() { xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'; }
wait_for 20 wm_up || echo "note: luxwm did not announce itself"
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
has() { grep -q "$2" "$1" 2>/dev/null; }
said() { grep -q "^$1\$" $T/out; }
expect() {   # WHAT LINE: wait until the dialog's trace has LINE
    if wait_for 10 said "$2"; then ok "$1: $2"; else bad "$1: expected [$2] in [$(tail -n 3 $T/out | tr '\n' '|')]"; fi
}
"$R/build/bin/flxp" --default-programs=editor > $T/out 2>&1 & P=$!
W=$(wait_window 30 --name '^Default Programs$')
if [ -z "$W" ]; then
    bad "no Default Programs window"
else
    xdotool windowactivate --sync $W 2>/dev/null; sleep .5
    expect "opens at the editor role, lhde's default checked" "defprogs: editor zzpad.desktop zzpad.desktop zzedit.desktop"
    # Tab from the role list to the checked radio button, Down to the next program
    xdotool key Tab; sleep .3; xdotool key Down
    expect "Down picks zzedit" "defprogs: editor chosen zzedit.desktop"
    xdotool key alt+a
    expect "Apply" "defprogs: applied 1"
    has $T/home/.config/lhde/settings.ini '^editor = zzedit.desktop' && ok "settings.ini [programs] editor = zzedit.desktop" ||
        bad "settings.ini: $(cat $T/home/.config/lhde/settings.ini 2>&1)"
    has $RF '^text/plain=zzedit.desktop$' && has $RF '^text/x-csrc=zzedit.desktop$' &&
        ok "the role file has text/plain and text/x-csrc" || bad "the role file: $(cat $RF 2>&1)"
    has $RF '^# lhde.s default programs by role' && ok "the role file says it is generated" || bad "no header in $RF"
    xdotool key Escape
    wait_for 10 sh -c "! kill -0 $P 2>/dev/null" && ok "Escape closes it" || bad "flxp still runs after Escape"
fi
kill $P 2>/dev/null; wait $P 2>/dev/null
# File Types: "Set by" names the role; Change... for TXT goes to ~/.config/mimeapps.list and wins
"$R/build/bin/flxp" --folder-options=filetypes > $T/out 2>&1 & P=$!
W=$(wait_window 30 --name '^Folder Options$')
if [ -z "$W" ]; then
    bad "no Folder Options window"
else
    xdotool windowactivate --sync $W 2>/dev/null; sleep .5
    expect "File Types: TXT set by the role" "filetypes: TXT text/plain zzedit.desktop role \[your editor role (ZZ Edit)\]"
    xdotool key alt+c
    OW=$(wait_window 10 --name '^Open With$')
    if [ -n "$OW" ]; then
        # the recommended programs: zzedit (the default) first, zzpad after it
        sleep .5; xdotool key Down; xdotool key Return
        expect "Change...: a choice for the type wins over the role" "filetypes: TXT text/plain zzpad.desktop user restore"
        has $T/home/.config/mimeapps.list '^text/plain=zzpad.desktop$' && ok "~/.config/mimeapps.list has text/plain=zzpad" ||
            bad "~/.config/mimeapps.list: $(cat $T/home/.config/mimeapps.list 2>&1)"
        has $RF '^text/plain=zzedit.desktop$' && ok "the role file still has the role's line" || bad "the role file changed: $(cat $RF)"
    else
        bad "Change... opened no Open With dialog"
    fi
    focused() { [ "$(xdotool getwindowfocus 2>/dev/null)" = "$W" ]; }
    wait_for 10 focused; sleep .3
    xdotool key Escape
    wait_for 10 sh -c "! kill -0 $P 2>/dev/null" && ok "Escape closes the sheet" || bad "flxp still runs after Escape"
fi
kill $P 2>/dev/null; wait $P 2>/dev/null
kill $PW 2>/dev/null; wait $PW 2>/dev/null
stop_xvfb
echo "$fails failed"
exit $fails
