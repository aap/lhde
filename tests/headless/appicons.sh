#!/bin/sh
# The window and program icons every consumer shares (liblhde lhde_app_icon, docs/THEME-FORMAT.md section 9) under Xvfb, with a
# scratch HOME whose theme (base classic) names an icon set of its own: one-colour .ico files for cmd (green), display (blue)
# and exe, the generic application icon (red), and a program with a desktop file and an hicolor icon (yellow).
#   1. flxp, no window manager: the Open With dialog (a file's menu, Open With, Choose Program...) lists the program with its
#      desktop file's icon; the Properties sheet's "Opens with" row shows the default program's icon.
#   2. luxwm and luxshell (the Avalon taskbar) with an xterm (no _NET_WM_ICON: the shipped class map's cmd) and flxp-settings
#      (its own window icon from the set's display key): Alt+Tab's switcher shows both 32 px icons and not the generic one;
#      luxshell's taskbar shows both 16 px icons and not the generic one.
# Colours are counted in screenshots (ImageMagick). Needs Xvfb, xdotool, xprop, xwininfo, xterm and ImageMagick.
# Usage: tests/headless/appicons.sh [:DISPLAY]
D=${1:-:77}
R=$(cd "$(dirname "$0")/../.." && pwd)
for t in Xvfb xdotool xprop xwininfo xterm magick import; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
for b in flxp flxp-settings luxwm luxshell; do [ -x "$R/build/bin/$b" ] || { echo "skip: $b is not built"; exit 0; }; done
T=$(mktemp -d)
trap '[ -n "$APPICONS_KEEP" ] && cp $T/*.png "$APPICONS_KEEP"/ 2>/dev/null; rm -rf "$T"' EXIT
C=$T/home/.config/lhde
mkdir -p $C/icons $C/themes $T/home/.local/share/applications $T/home/.local/share/icons/hicolor/16x16/apps $T/files
ico() { magick -size 32x32 "xc:$2" -define icon:auto-resize=32,16 "$C/icons/ictest/$1.ico"; }
mkdir -p $C/icons/ictest
ico cmd '#00c000'; ico display '#0000e0'; ico exe '#e00000'; ico folder '#808080'; ico document '#a0a0a0'
cat > $C/icons/ictest.theme <<'EOF'
[theme]
name = Icon test
base = ictest
[icons]
folder = folder.ico
document = document.ico
cmd = cmd.ico
display = display.ico
exe = exe.ico
EOF
printf '[Theme]\nname = Icon test\nbase = classic\n[Desktop]\nicons = ictest\n' > $C/themes/ictest.theme
printf '[theme]\nname = ictest\n' > $C/settings.ini
printf '[flxp]\ntaskpane = 0\n' > $C/flxp.ini
magick -size 16x16 'xc:#d0d000' $T/home/.local/share/icons/hicolor/16x16/apps/zzview.png
printf '[Icon Theme]\nName=Hicolor\nDirectories=16x16/apps\n[16x16/apps]\nSize=16\nType=Threshold\n' > $T/home/.local/share/icons/hicolor/index.theme
printf '[Desktop Entry]\nType=Application\nName=ZzView\nExec=true %%f\nIcon=zzview\nMimeType=text/plain;\n' > $T/home/.local/share/applications/zzview.desktop
printf '[Default Applications]\ntext/plain=zzview.desktop\n' > $T/home/.config/mimeapps.list
echo one > $T/files/a.txt
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D PATH="$R/build/bin:$PATH" LHSHELL_COMPOSITION=0 LHSHELL_WALLPAPER=0 LHSHELL_TASKBAR=avalon
unset LHDE_DATA LHDE_SETTINGS DBUS_SESSION_BUS_ADDRESS
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
fails=0 pids=
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
# count COLOUR FILE: the pixels of that colour (within 8 %) in a screenshot
count() { magick "$2" -alpha off -fuzz 8% -fill black +opaque "$1" -fill white -opaque "$1" -format '%[fx:int(mean*w*h+0.5)]' info:; }
# expect WHAT FILE COLOUR MIN: at least MIN pixels of COLOUR
expect() { n=$(count "$3" "$2"); [ "$n" -ge "$4" ] && ok "$1 ($n px of $3)" || bad "$1: $n px of $3, expected at least $4"; }
none() { n=$(count "$3" "$2"); [ "$n" -le "${4:-0}" ] && ok "$1 (no $3)" || bad "$1: $n px of $3 (the generic icon)"; }
focus() { timeout 10 xdotool windowfocus --sync "$1" 2>/dev/null; sleep .3; }

# 1. flxp alone: Open With and Properties' Opens with
"$R/build/bin/flxp" --geometry 800x500+0+0 "$T/files" > $T/flxp.log 2>&1 & P=$!
W=$(wait_window 30 --class Flxp)
if [ -z "$W" ]; then bad "no Explorer window"; else
    focus $W; sleep .5
    xdotool mousemove --window $W 260 101 click 3; sleep .7; xdotool key h; sleep .5; xdotool key c
    O=$(wait_window 20 --name '^Open With$')
    if [ -z "$O" ]; then bad "no Open With dialog"; else
        sleep .5; import -window $O $T/openwith.png 2>/dev/null
        expect "Open With: ZzView with its desktop file's icon" $T/openwith.png '#d0d000' 200
        xdotool key Escape; sleep .5
    fi
    focus $W; xdotool mousemove --window $W 260 101 click 3; sleep .7; xdotool key r
    S=$(wait_window 20 --name '^a\.txt Properties$')
    if [ -z "$S" ]; then bad "no Properties sheet"; else
        sleep .5; import -window $S $T/props.png 2>/dev/null
        expect "Properties: Opens with ZzView's icon" $T/props.png '#d0d000' 200
        xdotool key Escape; sleep .3
    fi
fi
kill $P 2>/dev/null; wait $P 2>/dev/null

# 2. luxwm and luxshell: the switcher and the task buttons
"$R/build/bin/luxwm" -t ictest > $T/luxwm.log 2>&1 & pids="$pids $!"
wm_up() { xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'; }
wait_for 20 wm_up || bad "luxwm did not start"
"$R/build/bin/luxshell" > $T/luxshell.log 2>&1 & pids="$pids $!"
win() { xdotool search --classname "^$1\$" 2>/dev/null | head -1; }
up() { TB=$(win luxshell-taskbar); [ -n "$TB" ] && grep -q 'luxshell: theme' $T/luxshell.log; }
wait_for 20 up || bad "luxshell did not come up ($(tail -n 3 $T/luxshell.log | tr '\n' '|'))"
xterm -geometry 40x10+100+100 > /dev/null 2>&1 & pids="$pids $!"
wait_window 20 --class XTerm > /dev/null || bad "no xterm"
"$R/build/bin/flxp-settings" > $T/settings.log 2>&1 & pids="$pids $!"
wait_window 30 --name 'Display Properties' > /dev/null || bad "no Display Properties"
sleep 2
if [ -n "$TB" ]; then
    import -window $TB $T/taskbar.png 2>/dev/null
    expect "taskbar: xterm's button has cmd's icon" $T/taskbar.png '#00c000' 200
    expect "taskbar: Display Properties' button has its own display icon" $T/taskbar.png '#0000e0' 200
    none "taskbar: no generic icon" $T/taskbar.png '#e00000'
fi
switcher() { xwininfo -root -tree | awk '/"lhwm task switcher"/ { print $1; exit }'; }
shown() { [ -n "$(switcher)" ]; }
xdotool keydown alt; xdotool key Tab
if wait_for 5 shown; then
    sleep .3; import -window "$(switcher)" $T/switcher.png 2>/dev/null
    expect "switcher: xterm at 32 px with cmd's icon" $T/switcher.png '#00c000' 900
    expect "switcher: Display Properties at 32 px with the display icon" $T/switcher.png '#0000e0' 900
    none "switcher: no generic icon" $T/switcher.png '#e00000'
else bad "no switcher on Alt+Tab"; fi
xdotool key Escape; xdotool keyup alt
for p in $pids; do kill $p 2>/dev/null; done
for p in $pids; do wait $p 2>/dev/null; done
stop_xvfb
[ $fails = 0 ] && echo "appicons: all passed" || echo "appicons: $fails failed"
exit $fails
