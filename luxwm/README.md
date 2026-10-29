# luxwm: the window manager (was lhwm)

A berry fork that paints Longhorn frames: DWM glass frame sets (plate, overlay, buttons) under a compositor, and classic frames
drawn live from the visual style through libwuxtheme (Window.CAPTION & co.) without one. Imported from the x11 lab (d353b69);
the manual is docs/LUXWM.md, the client protocol (extended glass frames, header pixmap, caption rect) docs/CLIENT-PROTOCOL.md.

    make                          build/bin/luxwm, build/bin/luxwmc (the IPC client, was lhwmc / berryc)
    make check-luxwm              luxwm/tests/regress.sh: the frame painter against this machine's baseline

- Sources: wm.c (window management, EWMH, the theme registry, animations, keys), frame.c (frame sets and painting),
  frame_ut.c (frames from a .msstyles through libwuxtheme), menu.c (the window menu), client.c (luxwmc), config.h (build
  configuration; was berry's configure output). `luxwm.mk` is its part of the top-level build.
- Themes: the registry data/themes.ini (`frame = frames/NAME`), frame sets in data/frames/NAME (theme.ini, nav.ini) with their
  pixels in the Windows tree (`~/.local/share/lhde-windows/generated/frames`, tools/lhde-import), found through liblhde/data.c. luxwm publishes the registry's
  resolved copy as `_LH_THEMES_INI`.
- Names kept from lhwm: the X atoms (`_LHWM_*`), the window manager's `_NET_WM_NAME` "lhwm" (luxshell and flxp's log-off know
  it by that), `LHWM_*` environment variables, `~/.config/lhde/keys`. `make install` adds `lhwm` and `lhwmc` symlinks.
- tests/: frametest (renders frames offline), menutest, regress.sh, and small X clients for the lab (desktopwin, dockorder,
  strutdock); examples/: berry's autostart and sxhkdrc samples (its xephyr.sh dev script, which runs killall, is not imported).
