# luxshell: the Avalon bars, sidebar, tiles, toasts and start menu (was lhshell, "lux")

A C core (luxshell.c: X surfaces and the Lua API `lh`; scene.c: the scene graph; text.c: GDI text through libwuxtheme; tray.c:
the XEmbed tray; notify.c: org.freedesktop.Notifications) running Lua: runtime.lua (bars, sidebar, theme switching), session.lua
(registry, settings.ini, presets, session commands), menu.lua, flyout.lua, toast.lua, notifylog.lua (the notification history), tooltip.lua, settings.lua, apps.lua and
tiles/*.lua. Design: docs/SHELL-DESIGN.md; use: docs/USAGE.md.

    make                    build/bin/luxshell (system Lua 5.4 and libdbus via pkg-config), build/luxshell/tests/trayicon
    make check-luxshell     it builds and every Lua file parses (luac -p)

- The Lua code is found beside the binary: luxshell/ in a build tree, `$PREFIX/lib/lhde/luxshell` installed (`LUXSHELL_LUA`
  overrides). It runs from any directory.
- Packs: the text in data/packs/NAME/shell.lua (4074-aero, 4074-slate, 4051-slate, 5112-aero, 4074-aerovision;
  ), the images under packs/<build>/ of the Windows tree (`~/.local/share/lhde-windows/generated/packs`; lhde-import; `lh.data_find`).
- shellpack.sh, shellpack-aero5112.sh: the research tools that extracted the pack images from the extracted builds (they need
  the workspace's res/ and wim/; lhde-import copies their output). The X class of luxshell's windows stays `lhshell`, its atoms
  `_LHSHELL_*`; `make install` adds an `lhshell` symlink.
