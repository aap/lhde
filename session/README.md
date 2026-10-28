# session: lhde-session and the lab

- `lhde-session`: the desktop session (the x11 lab's xinitrc.lab and second-display.sh merged): `start [--nested|--attach] [THEME]`,
  `theme NAME`, `taskbar avalon|classic|classic+sidebar|none`, `shell on|off`, `picom on|off`, `desktop on|off`, `stop`
  (`lhde-session --help`). Installed to `$PREFIX/bin`; the rest of this directory to `$PREFIX/share/lhde/session`.
- `lab.sh`: the headless test bench (Xvfb, or a Xephyr window): `LAB=:N session/lab.sh start | shell | theme NAME | shot | stop`.
- `lhde-paths.sh`: sourced by the scripts: the data directories, the build tree or install, program lookup (as lhtheme/lhde_data.h).
- `lhwall.sh`: the session's first root background and whether the theme wants a compositor (sourced).
- `theme-reload.sh`, `theme-watch.sh`: live-editing a theme (docs/USAGE.md "Live-editing a theme").
- `capture.sh`, `hovertest.sh`: screen capture and a hover/press test for a lab display.
- `picom-glass.conf` (luxwm's glass and animations), `picom-glass-fvwm.conf`, `lab.fvwm2rc` (an fvwm2 session: `LHDE_WM=fvwm`).

Every script honours `LHDE_DATA` (another data directory) as the programs do.
