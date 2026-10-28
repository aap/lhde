# session: lhde-session and the lab

- `lhde-session`: the desktop session (the x11 lab's xinitrc.lab and second-display.sh merged): `start [--nested|--attach] [THEME]`,
  `theme NAME`, `taskbar avalon|classic|classic+sidebar|none`, `shell on|off`, `picom on|off`, `desktop on|off`, `stop`
  (`lhde-session --help`). Installed to `$PREFIX/bin`; the rest of this directory to `$PREFIX/lib/lhde/session` (program
  files: docs/LAYOUT.md "The three trees"). At start it also moves an old-layout `~/.local/share/lhde` (`lhde-import --migrate`).
- `lab.sh`: the headless test bench (Xvfb, or a Xephyr window): `LAB=:N session/lab.sh start | shell | theme NAME | shot | stop`.
- `lhde-paths.sh`: sourced by the scripts: the build tree or install, program lookup; data lookups go through `lhde-theme find`
  and `lhde-theme dirs --list` (liblhde's one implementation).
- `lhwall.sh`: the session's first root background and whether the theme wants a compositor (sourced).
- `theme-reload.sh`, `theme-watch.sh`: live-editing a theme (docs/USAGE.md "Live-editing a theme").
- `capture.sh`, `hovertest.sh`: screen capture and a hover/press test for a lab display.
- `picom-glass.conf` (luxwm's glass and animations), `picom-glass-fvwm.conf`, `lab.fvwm2rc` (an fvwm2 session: `LHDE_WM=fvwm`).

Every script honours `LHDE_WINDOWS` (another Windows tree) and `LHDE_DATA` (a flat data directory) as the programs do.
