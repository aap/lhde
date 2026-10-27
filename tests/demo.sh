#!/bin/sh
# Run a demo on the current display (from anywhere; builds what it needs first).
#   tests/demo.sh                 classic controls window
#   tests/demo.sh theme           same under the Slate (Longhorn 4074 backport) theme
#   tests/demo.sh dialog [NAME]   a comdlg32 dialog template (CHOOSE_FONT, CHOOSE_COLOR, OPEN_FILE, PRINT32, ...)
#   tests/demo.sh tdialog [NAME]  the same, themed
#   tests/demo.sh jade            controls under the Jade theme
#   tests/demo.sh explorer [DIR]  the explorer with the flxp.ini settings (default: luna if found)
#   tests/demo.sh cexplorer [DIR] the explorer, classic
#   tests/demo.sh texplorer [DIR] the explorer under Slate
#   tests/demo.sh filedlg [ARGS]  the file dialog (flxp-filedialog --help; FLXP_THEME=classic for classic)
# Slate and Jade: $FLXP_SLATE / $FLXP_JADE (a .msstyles path), else the names
# slate / jade looked up in the theme directories (theme_path= in flxp.ini).
# The explorer takes its fonts, icons and theme from ~/.config/flxp/flxp.ini
# (written on first run; see build/bin/flxp --help and --info).
cd "$(dirname "$0")/.."
SLATE=${FLXP_SLATE:-slate}
JADE=${FLXP_JADE:-jade}
make -s w32test flxp >/dev/null || exit 1
T=build/tests/w32
case "$1" in
  theme)   FLXP_THEME=${FLXP_THEME:-$SLATE} exec $T/controls ;;
  jade)    FLXP_THEME=${FLXP_THEME:-$JADE} exec $T/controls ;;
  dialog)  exec $T/dialog wux/rc/comdlg32.res "${2:-CHOOSE_FONT}" ;;
  tdialog) FLXP_THEME=${FLXP_THEME:-$SLATE} exec $T/dialog wux/rc/comdlg32.res "${2:-CHOOSE_FONT}" ;;
  explorer)  shift; exec build/bin/flxp "$@" ;;
  cexplorer) shift; exec build/bin/flxp --classic "$@" ;;
  texplorer) shift; exec build/bin/flxp --theme "${FLXP_THEME:-$SLATE}" "$@" ;;
  filedlg)   shift; exec build/bin/flxp-filedialog "$@" ;;
  *)       exec $T/controls ;;
esac
