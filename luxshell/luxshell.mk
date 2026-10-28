# luxshell, the Avalon bars, sidebar, tiles, toasts and start menu (was lhshell / "lux"; docs/SHELL-DESIGN.md). Included by
# the top-level Makefile. A C core (scene graph, X surfaces, tray, notifications, GDI text through libwuxtheme) running Lua
# (runtime.lua and its modules, tiles/): system Lua 5.4 (pkg-config lua5.4) and libdbus.
#   build/bin/luxshell                    finds its Lua in luxshell/ (build tree) or $(DATADIR)/luxshell (installed)
#   build/luxshell/tests/trayicon         an XEmbed tray icon for the notification area tile
# The packs' text (data/packs/NAME/shell.lua) installs with data/; their images come from lhde-import (packs/<build>/...).

LUXSHELL_PKGS   = x11 xext xfixes cairo cairo-xlib fontconfig freetype2 lua5.4 dbus-1
LUXSHELL_CFLAGS = -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation -MMD -MP -DHAVE_UXTHEME -DUT_HAVE_TEXT_EXTENT \
                  -Iluxshell -Ilhtheme -idirafter . $(shell pkg-config --cflags $(LUXSHELL_PKGS))
LUXSHELL_OBJS   = $(addprefix $(O)/luxshell/,luxshell.o scene.o text.o tray.o notify.o) $(O)/lhtheme/lhde_data.o
LUXSHELL_LUA    = $(wildcard luxshell/*.lua) $(wildcard luxshell/tiles/*.lua)

$(O)/luxshell/%.o: luxshell/%.c
	@mkdir -p $(dir $@)
	$(CC) $(LUXSHELL_CFLAGS) -c $< -o $@
build/bin/luxshell: $(LUXSHELL_OBJS) $(UTSO)
	@mkdir -p $(dir $@)
	$(CC) -o $@ $(LUXSHELL_OBJS) -Lbuild/lib -lwuxtheme '-Wl,-rpath,$$ORIGIN/../lib' $(shell pkg-config --libs $(LUXSHELL_PKGS)) -lm -ldl
build/luxshell/tests/trayicon: luxshell/tests/trayicon.c
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -g -Wall -o $@ $< $(shell pkg-config --cflags --libs x11 cairo cairo-xlib) -lm

luxshell: build/bin/luxshell build/luxshell/tests/trayicon

# make check-luxshell: it builds, and every Lua file at least compiles (luac -p when there is one)
check-luxshell: build/bin/luxshell
	@if command -v luac5.4 >/dev/null 2>&1; then L=luac5.4; elif command -v luac >/dev/null 2>&1; then L=luac; else L=; fi; \
	if [ -n "$$L" ]; then $$L -p $(LUXSHELL_LUA) data/packs/*/shell.lua data/packs/startpanels.lua && echo "ok    luxshell (built, Lua parses: $$L -p)"; \
	else echo "ok    luxshell (built; no luac to check the Lua)"; fi

install-luxshell: build/bin/luxshell
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(DATADIR)/luxshell/tiles
	install -m 755 build/bin/luxshell $(DESTDIR)$(BINDIR)/
	ln -sf luxshell $(DESTDIR)$(BINDIR)/lhshell
	install -m 644 $(wildcard luxshell/*.lua) $(DESTDIR)$(DATADIR)/luxshell/
	install -m 644 $(wildcard luxshell/tiles/*.lua) $(DESTDIR)$(DATADIR)/luxshell/tiles/
	install -m 755 luxshell/shellpack.sh luxshell/shellpack-aero5112.sh $(DESTDIR)$(DATADIR)/luxshell/
uninstall-luxshell:
	rm -f $(DESTDIR)$(BINDIR)/luxshell $(DESTDIR)$(BINDIR)/lhshell
	rm -rf $(DESTDIR)$(DATADIR)/luxshell

-include $(LUXSHELL_OBJS:.o=.d)
.PHONY: luxshell check-luxshell install-luxshell uninstall-luxshell
