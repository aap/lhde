#!/bin/sh
# wux's MessageBoxW (Wine's msgbox.c) under Xvfb, driven by xdotool through build/tests/w32/msgbox: the buttons a type shows,
# in XP's order with the default marked; Enter gives the default button's id (Yes 6, No 7 with MB_DEFBUTTON2, OK 1); Escape
# gives Cancel (2) where there is a Cancel button, OK (1) in an OK box and nothing in a Yes/No box (n then answers No, 7); a
# null caption is "Error"; MessageBoxA; a box with an owner. With the user's (or LHDE_WINDOWS's) imported tree: under classic,
# luna and jade4074 every button is XP's 75x23 (50 x 14 dialog units of MS Shell Dlg 8, Tahoma 8) with 6 px (4 DLU) between,
# the text in the style's message font (Tahoma 8; Segoe UI 9 under Jade); a style not imported is skipped. Needs Xvfb and
# xdotool. Usage: tests/headless/msgbox.sh [:DISPLAY]
D=${1:-:75}
T=$(mktemp -d)
R=$(cd "$(dirname "$0")/../.." && pwd)
B=$R/build/tests/w32/msgbox
styles=$("$R/build/bin/lhde-theme" find styles 2>/dev/null)   # before the scratch home
mkdir -p $T/home/.config/lhde
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D LHDE_WINDOWS=/nonexistent
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
fails=0
check() {   # NAME EXPECTED ACTUAL
    if [ "$2" = "$3" ]; then echo "ok   $1: $3"; else echo "FAIL $1: expected [$2] got [$3]"; fails=$((fails + 1)); fi
}
has_ret() { grep -q '^ret ' $T/out; }
has_buttons() { grep -q '^buttons:' $T/out; }
# run TYPE CAPTION KEYS...: the box, its buttons once read, then each key; $ret the value it returned ("" if it stayed)
run() {
    _t=$1; _c=$2; shift 2
    "$B" "$_t" "$_c" > $T/out 2>$T/err & P=$!
    W=$(wait_window 20 --name "^${_c#-}\$")
    [ "$_c" = - ] && W=$(wait_window 20 --name '^Error$')
    wait_for 10 has_buttons
    buttons=$(sed -n 's/^buttons: //p' $T/out)
    if [ -n "$W" ]; then
        timeout 10 xdotool windowfocus --sync $W 2>/dev/null; sleep .2
        for k in "$@"; do xdotool key $k; sleep .3; done
    fi
    wait_for 5 has_ret
    ret=$(sed -n 's/^ret //p' $T/out)
    kill -0 $P 2>/dev/null && { kill $P; }
    wait $P 2>/dev/null
}

run yesno Question Return;            check "Yes/No: buttons" "&Yes* &No" "$buttons"; check "Yes/No: Enter" 6 "$ret"
run yesno+def2 Question Return;       check "Yes/No, default 2: buttons" "&Yes &No*" "$buttons"; check "Yes/No, default 2: Enter" 7 "$ret"
run yesno Question Escape n;          check "Yes/No: Escape does nothing, n is No" 7 "$ret"
run ok Note Escape;                   check "OK: buttons" "OK*" "$buttons"; check "OK: Escape is OK" 1 "$ret"
run okcancel Warning Escape;          check "OK/Cancel: buttons" "OK* Cancel" "$buttons"; check "OK/Cancel: Escape" 2 "$ret"
run yesnocancel Warning Tab Return;   check "Yes/No/Cancel: buttons" "&Yes* &No Cancel" "$buttons"; check "Yes/No/Cancel: Tab Enter" 7 "$ret"
run abortretryignore Stop Return;     check "Abort/Retry/Ignore: buttons" "&Abort* &Retry &Ignore" "$buttons"; check "Abort/Retry/Ignore: Enter" 3 "$ret"
run ok - Return;                      check "null caption: Error" 1 "$ret"
run okcancel+a Ansi Return;           check "MessageBoxA: Enter" 1 "$ret"
run yesno+owner Owned Return;         check "owned box: Enter" 6 "$ret"
# the sizes under the imported styles
if [ -n "$styles" ]; then
    export LHDE_WINDOWS=${styles%/index/styles}
    for th in classic:Tahoma luna:Tahoma jade4074:Segoe; do
        name=${th%%:*}; face=${th#*:}
        if [ $name != classic ] && [ ! -e "$styles/$name.msstyles" ]; then echo "skip $name: not imported"; continue; fi
        printf '[theme]\nname = %s\n' $name > $T/home/.config/lhde/settings.ini
        run yesnocancel Sizes Escape
        check "$name: Yes/No/Cancel 75x23, 6 apart" "75x23+6 75x23+6 75x23" "$(sed -n 's/^sizes: //p' $T/out)"
        check "$name: OK 75x23" "75x23" "$(run ok Sizes Return; sed -n 's/^sizes: //p' $T/out)"
        tf=$(sed -n 's/^textfont: //p' $T/out)
        case $face in
        Tahoma) check "$name: the text in the message font" "Tahoma 8" "$tf" ;;
        Segoe) check "$name: the text in the message font" "Segoe UI 9" "$tf" ;;
        esac
    done
else
    echo "skip sizes: nothing imported"
fi
stop_xvfb
rm -rf $T
[ $fails = 0 ] && echo PASS || { echo "FAIL ($fails)"; exit 1; }
