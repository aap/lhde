# luxwm: the window manager

A berry fork that paints Longhorn frames: DWM glass frame sets (plate, overlay, buttons) under a compositor, and classic frames
drawn live from the visual style through libwuxtheme (Window.CAPTION & co.) without one. The manual is docs/LUXWM.md, the client protocol (extended glass frames, header pixmap, caption rect) docs/CLIENT-PROTOCOL.md.

    make                          build/bin/luxwm, build/bin/luxwmc (the IPC client)
    make check-luxwm              luxwm/tests/regress.sh: the frame painter against this machine's baseline

- Sources: wm.c (window management, EWMH, the session theme, animations, keys), frame.c (frame sets and painting),
  frame_ut.c (frames from a .msstyles through libwuxtheme), menu.c (the window menu), client.c (luxwmc), config.h (build
  configuration). `luxwm.mk` is its part of the top-level build.
- Themes: the theme files share/themes/NAME.theme, resolved by liblhde (`[Glass] frames = NAME`), frame sets in
  share/frames/NAME (theme.ini, nav.ini) with their pixels in the Windows tree (`~/.local/share/lhde-windows/generated/frames`,
  tools/lhde-import), found through liblhde/data.c.
- From berry (MIT, Joshua L Ervin; its notice is luxwm/LICENSE, docs/LICENSING.md): wm.c and client.c (they carry berry's
  copyright line), config.h, globals.h, ipc.h, types.h, utils.c, utils.h, the man pages luxwm.1 and luxwmc.1 (berry's
  berry.1 and berryc.1) and examples/. Written for lhde: frame.c, frame.h, frame_ut.c, frame_ut.h, menu.c, menu.h, luxwm.mk
  and tests/.
- Names kept from lhwm: the X atoms (`_LHWM_*`), the window manager's `_NET_WM_NAME` "lhwm" (luxshell and flxp's log-off know
  it by that), `LHWM_*` environment variables, `~/.config/lhde/keys`. `make install` adds `lhwm` and `lhwmc` symlinks.
- tests/: frametest (renders frames offline), menutest, regress.sh, and small X clients for the lab (desktopwin, dockorder,
  strutdock); examples/: berry's autostart and sxhkdrc samples (its xephyr.sh dev script, which runs killall, is not imported).
