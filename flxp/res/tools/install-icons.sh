#!/bin/sh
# install-icons.sh THEME NT5SRC DATADIR
# Copies the icon and toolbar files flxp/res/icons/THEME.theme reads from the XP
# source tree into DATADIR/icons/THEME and installs a THEME.theme pointing at
# them (base = ../../icons/THEME, relative to the theme file), so the
# installed flxp no longer needs nt5src. Used by 'make install-icons'.
set -e
theme=$1 nt5=$2 datadir=$3
src=$(dirname "$0")/../icons/$theme.theme
[ -f "$src" ] || { echo "install-icons: no $src" >&2; exit 1; }
[ -d "$nt5" ] || { echo "install-icons: nt5src '$nt5' is not a directory (make install-icons NT5SRC=/path/to/nt5src)" >&2; exit 1; }
base=$(sed -n 's/^base *= *//p' "$src" | sed "s|\$FLXP_NT5SRC|$nt5|")
dest=$datadir/icons/$theme
mkdir -p "$dest" "$datadir/res/icons"
n=0
# the file-valued keys of [icons] and [toolbar]
for f in $(awk -F' *= *' '/^\[/{s=$0;next} /^[;#]/{next} (s=="[icons]"||s=="[toolbar]") && $2 ~ /\./ {sub(/ *;.*/,"",$2); print $2}' "$src" | sort -u); do
    case $f in /*|*'$'*|pe:*) echo "install-icons: skipping $f (not relative to base)" >&2; continue;; esac
    mkdir -p "$dest/$(dirname "$f")"
    cp "$base/$f" "$dest/$f"
    n=$((n + 1))
done
sed "s|^base *=.*|base = ../../icons/$theme|; s|^; Windows XP icons, read straight.*|; Installed copy: the files were copied from nt5src by make install-icons.|" "$src" > "$datadir/res/icons/$theme.theme"
echo "install-icons: $n files -> $dest"
