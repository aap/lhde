# luxshell, the Avalon bars, sidebar, tiles, toasts and start menu (docs/SHELL-DESIGN.md). Included by
# the top-level Makefile. A C core (scene graph, X surfaces, tray, notifications, GDI text through libwuxtheme) running Lua
# (runtime.lua and its modules, tiles/): system Lua 5.4 (pkg-config lua5.4; FreeBSD's lua-5.4) and libdbus.
#   build/bin/luxshell                    finds its Lua in luxshell/ (build tree) or $(PKGLIBDIR)/luxshell (installed)
#   build/luxshell/tests/trayicon         an XEmbed tray icon for the notification area tile
# The packs' text (share/packs/NAME/shell.lua) installs with share/; their images come from lhde-import (packs/<build>/...).

LUA_PC         := $(firstword $(foreach p,lua5.4 lua-5.4,$(shell pkg-config --exists $(p) && echo $(p))) lua5.4)
LUXSHELL_PKGS   = x11 xext xfixes cairo cairo-xlib fontconfig freetype2 $(LUA_PC) dbus-1 libpng
LUXSHELL_CFLAGS = -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation -MMD -MP -DHAVE_UXTHEME \
                  -Iluxshell -Iliblhde -idirafter . $(shell pkg-config --cflags $(LUXSHELL_PKGS))
LUXSHELL_OBJS   = $(addprefix $(O)/luxshell/,luxshell.o scene.o text.o tray.o notify.o menupaint.o)
LUXSHELL_LUA    = $(wildcard luxshell/*.lua) $(wildcard luxshell/tiles/*.lua) $(wildcard luxshell/trayitems/*.lua)

$(O)/luxshell/%.o: luxshell/%.c
	@mkdir -p $(dir $@)
	$(CC) $(LUXSHELL_CFLAGS) -c $< -o $@
build/bin/luxshell: $(LUXSHELL_OBJS) $(LHDEA) $(UTSO)
	@mkdir -p $(dir $@)
	$(CC) -o $@ $(LUXSHELL_OBJS) $(LHDEA) -Lbuild/lib -lwuxtheme '-Wl,-rpath,$$ORIGIN/../lib' $(shell pkg-config --libs $(LUXSHELL_PKGS)) -lm -ldl
build/luxshell/tests/trayicon: luxshell/tests/trayicon.c
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -g -Wall -o $@ $< $(shell pkg-config --cflags --libs x11 cairo cairo-xlib) -lm

luxshell: build/bin/luxshell build/luxshell/tests/trayicon

# make check-luxshell: it builds, every Lua file at least compiles (luac -p when there is one), and the tray items pass
# luxshell/tests/tiles.sh (stub system commands, no display; the Classic Tray tile hosting them), and every tile is in the
# sidebar menu's parts list (luxshell/tests/parts.py)
check-luxshell: build/bin/luxshell
	@if command -v luac5.4 >/dev/null 2>&1; then L=luac5.4; elif command -v luac >/dev/null 2>&1; then L=luac; else L=; fi; \
	if [ -n "$$L" ]; then $$L -p $(LUXSHELL_LUA) share/packs/*/shell.lua && echo "ok    luxshell (built, Lua parses: $$L -p)"; \
	else echo "ok    luxshell (built; no luac to check the Lua)"; fi
	@luxshell/tests/tiles.sh > build/luxshell-tiles.log 2>&1 && echo "ok    luxshell tray items ($$(tail -n 1 build/luxshell-tiles.log))" || \
	{ echo "FAIL  luxshell tray items (log below)"; grep -v '^ok' build/luxshell-tiles.log | tail -n 20; exit 1; }
	@python3 -I luxshell/tests/parts.py > build/luxshell-parts.log 2>&1 && echo "ok    luxshell sidebar parts list (every tile can be added back)" || \
	{ echo "FAIL  luxshell sidebar parts list"; grep -v '^ok' build/luxshell-parts.log; exit 1; }

install-luxshell: build/bin/luxshell
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(PKGLIBDIR)/luxshell/tiles $(DESTDIR)$(PKGLIBDIR)/luxshell/trayitems
	install -m 755 build/bin/luxshell $(DESTDIR)$(BINDIR)/
	install -m 644 $(wildcard luxshell/*.lua) $(DESTDIR)$(PKGLIBDIR)/luxshell/
	install -m 644 $(wildcard luxshell/tiles/*.lua) $(DESTDIR)$(PKGLIBDIR)/luxshell/tiles/
	install -m 644 $(wildcard luxshell/trayitems/*.lua) $(DESTDIR)$(PKGLIBDIR)/luxshell/trayitems/
uninstall-luxshell:
	rm -f $(DESTDIR)$(BINDIR)/luxshell
	rm -rf $(DESTDIR)$(PKGLIBDIR)/luxshell

-include $(LUXSHELL_OBJS:.o=.d)
.PHONY: luxshell check-luxshell install-luxshell uninstall-luxshell
