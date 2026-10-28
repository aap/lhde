# lhde, the Longhorn desktop environment: the top-level build.
#
#   make                  everything: libwux.a, libwuxtheme.{a,so.1}, libluxnav.{a,so.1}, the flxp
#                         programs, ut-dump, luxwm, luxwmc, luxshell, the examples and the tests, into build/
#   make check            the tests that need no Microsoft files (tests/check.sh), luxshell's build
#                         and Lua, luxwm's frame regression (skips without the user's styles)
#   make install          PREFIX=/usr/local DESTDIR= (also BINDIR, LIBDIR, INCLUDEDIR, DATADIR, MANDIR,
#                         PORTALDIR, DBUSSERVICEDIR); make uninstall
#   make clean
#
# Components (each with its own fragment, included below):
#   wux/wux.mk            the win32 UI layer: gdi, sys, win, res, w32, vendor/wine -> build/lib/libwux.a
#   wuxtheme/wuxtheme.mk  libwuxtheme (ut.h): build/lib/libwuxtheme.{a,so.1}, wuxtheme.pc, ut-dump
#   flxp/flxp.mk          flxp, flxp-desktop, flxp-taskbar, flxp-settings, flxp-filedialog,
#                         flxp-portal -> build/bin
#   tests/tests.mk        build/tests (toolkit tests), build/tests/w32 (shim tests), make check
#   luxnav/luxnav.mk      libluxnav (lhnav.h): build/lib/libluxnav.{a,so.1}, luxnav.pc; flxp's vision header
#   luxwm/luxwm.mk        luxwm, luxwmc -> build/bin; its tests -> build/luxwm/tests
#   luxshell/luxshell.mk  luxshell -> build/bin (its Lua stays in luxshell/, installed to DATADIR/luxshell)
#   examples/examples.mk  extendframe, navdemo, glassdemo -> build/examples
#   liblhde/liblhde.mk    liblhde (lhde/data.h, lhde/theme.h, lhde/settings.h): the data directories, the theme resolver,
#                         the settings store -> build/lib/liblhde.{a,so.1}, lhde.pc, build/bin/lhde-theme
#   data/ session/ tools/ installed as they are: DATADIR (themes/*.theme, navbar/*.ini, icons/*.theme,
#                         frames/*/*.ini, packs/*), DATADIR/session + BINDIR/lhde-session, BINDIR/lhde-import
#
# Every object is build/obj/<source path>.o (build/pic/... for the shared
# library); every program goes to build/bin, so programs that look for each
# other next to themselves (flxp-desktop -> flxp, flxp-portal -> flxp-filedialog)
# work uninstalled as installed.

CXX      ?= g++
# WebP (wux/gdi/image.cpp) when libwebp is installed: FLXP_WEBP, libwebpdemux for animations' first frame
WEBP     := $(shell pkg-config --exists libwebp libwebpdemux && echo libwebp libwebpdemux)
PKGS      = x11 xext freetype2 fontconfig libpng libjpeg $(WEBP)
CXXFLAGS ?= -g -O1
CXXFLAGS += -pthread -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -MMD -MP -Iwux -Iflxp -I. $(shell pkg-config --cflags $(PKGS))
CXXFLAGS += $(if $(WEBP),-DFLXP_WEBP)
LDLIBS    = $(shell pkg-config --libs $(PKGS)) -pthread

# the win32 headers (Wine's, vendored) and the shim's CRT redirections
WINE_DIR  = wux/vendor/wine
W32FLAGS  = -fshort-wchar -D__WINESRC__ -DWINE_UNICODE_NATIVE -DDECLSPEC_IMPORT= -DDECLSPEC_HIDDEN= -include wux/w32/include/flxp_crt.h \
            -Iwux/w32/include -I$(WINE_DIR)/include -I$(WINE_DIR)/comctl32 -I$(WINE_DIR)/user32 -I$(WINE_DIR)/uxtheme
CFLAGS    = -g -O1 -std=gnu11 -Wall -Wno-unused -Wno-missing-braces -Wno-incompatible-pointer-types -Werror=implicit-function-declaration -MMD -MP $(W32FLAGS)
W32CXX    = $(CXX) $(CXXFLAGS) $(W32FLAGS) -Wno-missing-field-initializers
WINDRES  ?= x86_64-w64-mingw32-windres

O = build/obj
P = build/pic

# Installation directories. DATADIR is compiled into flxp/shell/config.o
# (FLXP_DATADIR); an uninstalled build finds flxp/ in the source tree instead.
PREFIX         ?= /usr/local
BINDIR         ?= $(PREFIX)/bin
LIBDIR         ?= $(PREFIX)/lib
INCLUDEDIR     ?= $(PREFIX)/include
DATADIR        ?= $(PREFIX)/share/lhde
APPDIR         ?= $(PREFIX)/share/applications
PKGCONFIGDIR   ?= $(LIBDIR)/pkgconfig
# The file chooser portal backend: the .portal file goes where xdg-desktop-portal
# looks (its own datadir: PORTALDIR=/usr/share/xdg-desktop-portal/portals for a
# system portal), the D-Bus activation file into the session bus's service dirs.
PORTALDIR      ?= $(PREFIX)/share/xdg-desktop-portal/portals
DBUSSERVICEDIR ?= $(PREFIX)/share/dbus-1/services

MANDIR         ?= $(PREFIX)/share/man
# the lux side's C optimisation (luxwm, luxnav, lhde_data; -Os as lhwm's configure chose)
LUX_COPT       ?= -Os -g -DNDEBUG

all:

include wuxtheme/wuxtheme.mk   # first: wux.mk leaves its objects out of libwux.a
include liblhde/liblhde.mk     # before luxnav.mk and flxp.mk: libluxnav and flxp carry its data.o
include luxnav/luxnav.mk       # before flxp.mk: its programs link $(LUXNAVA)
include wux/wux.mk
include flxp/flxp.mk
include tests/tests.mk
include luxwm/luxwm.mk
include luxshell/luxshell.mk
include examples/examples.mk

all: libs programs tests liblhde luxwm luxshell examples
libs: $(WUXA) $(UTA) $(UTSO) $(UTPC) $(LHDEA) $(LHDESO) $(LHDEPC) $(LUXNAVA) $(LUXNAVSO) $(LUXNAVPC)
programs: $(PROGRAMS) $(LUXWM_PROGRAMS) build/bin/luxshell build/bin/lhde-theme

# make check: tests/check.sh (tests/tests.mk), then the lux side's
check: check-lux
check-lux: all
	@$(MAKE) --no-print-directory check-liblhde check-luxshell check-luxwm

# data/ (text only: the theme files, navbar layouts, icon recipes, import sources, the frame sets' theme.ini/nav.ini, the packs'
# shell.lua), session/ (lhde-session and its files), tools/lhde-import, lhde-pack and the recipe builders
SESSION_FILES = lab.sh lhwall.sh lhde-paths.sh theme-reload.sh theme-watch.sh capture.sh hovertest.sh \
                picom-glass.conf picom-glass-fvwm.conf lab.fvwm2rc
TOOL_FILES    = msstyles2theme.py build_aerovision.py build_aero5112.py lhdeimport.py wim0108.py
install-data:
	install -d $(DESTDIR)$(DATADIR)/frames $(DESTDIR)$(DATADIR)/packs $(DESTDIR)$(DATADIR)/session $(DESTDIR)$(DATADIR)/tools $(DESTDIR)$(BINDIR) \
	           $(DESTDIR)$(DATADIR)/themes $(DESTDIR)$(DATADIR)/navbar $(DESTDIR)$(DATADIR)/icons $(DESTDIR)$(DATADIR)/sources
	install -m 644 data/themes/*.theme $(DESTDIR)$(DATADIR)/themes/
	install -m 644 data/navbar/*.ini $(DESTDIR)$(DATADIR)/navbar/
	install -m 644 data/icons/*.theme $(DESTDIR)$(DATADIR)/icons/
	install -m 644 data/sources/*.ini $(DESTDIR)$(DATADIR)/sources/
	for d in data/frames/*/; do n=$$(basename $$d); install -d $(DESTDIR)$(DATADIR)/frames/$$n; install -m 644 $$d*.ini $(DESTDIR)$(DATADIR)/frames/$$n/; done
	for d in data/packs/*/; do n=$$(basename $$d); install -d $(DESTDIR)$(DATADIR)/packs/$$n; install -m 644 $$d*.lua $(DESTDIR)$(DATADIR)/packs/$$n/; done
	install -m 644 data/packs/startpanels.lua $(DESTDIR)$(DATADIR)/packs/
	install -m 755 session/lhde-session $(DESTDIR)$(BINDIR)/
	install -m 755 tools/lhde-import tools/lhde-pack $(DESTDIR)$(BINDIR)/
	cd session && install -m 644 $(SESSION_FILES) $(DESTDIR)$(DATADIR)/session/
	chmod 755 $(addprefix $(DESTDIR)$(DATADIR)/session/,lab.sh theme-reload.sh theme-watch.sh capture.sh hovertest.sh)
	ln -sf $(BINDIR)/lhde-session $(DESTDIR)$(DATADIR)/session/lhde-session
	cd tools && install -m 755 $(TOOL_FILES) $(DESTDIR)$(DATADIR)/tools/
uninstall-data:
	rm -f $(DESTDIR)$(BINDIR)/lhde-session $(DESTDIR)$(BINDIR)/lhde-import $(DESTDIR)$(BINDIR)/lhde-pack
	rm -rf $(DESTDIR)$(DATADIR)/frames $(DESTDIR)$(DATADIR)/packs $(DESTDIR)$(DATADIR)/session $(DESTDIR)$(DATADIR)/tools \
	       $(DESTDIR)$(DATADIR)/themes $(DESTDIR)$(DATADIR)/navbar $(DESTDIR)$(DATADIR)/icons $(DESTDIR)$(DATADIR)/sources

clean:
	rm -rf build

install: install-libs install-liblhde install-flxp install-luxnav install-luxwm install-luxshell install-data
uninstall: uninstall-luxshell uninstall-luxwm uninstall-luxnav uninstall-data uninstall-liblhde uninstall-libs uninstall-flxp

.PHONY: all libs programs tests clean install uninstall check check-lux install-data uninstall-data
.PRECIOUS: $(O)/%.o $(P)/%.o
.DELETE_ON_ERROR:
