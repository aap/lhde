# lhde, the Longhorn desktop environment: the top-level build.
#
#   make                  everything: libwux.a, libwuxtheme.{a,so.1}, libluxnav.{a,so.1}, the flxp
#                         programs, ut-dump, luxwm, luxwmc, luxshell, the examples and the tests, into build/
#   make check            the tests that need no Microsoft files (tests/check.sh), luxshell's build
#                         and Lua, luxwm's frame regression (skips without the user's styles), the free baseline
#                         (make check-free: only ReactOS imported; skips without its ISO)
#   make install          PREFIX=/usr/local DESTDIR= (also BINDIR, LIBDIR, INCLUDEDIR, DATADIR, PKGLIBDIR, MANDIR,
#                         PORTALDIR, DBUSSERVICEDIR); make uninstall. Program files only: the shipped data (text) into
#                         DATADIR = $(PREFIX)/share/lhde, the code (Lua, shell, Python) into PKGLIBDIR = $(PREFIX)/lib/lhde.
#                         The Windows files never come from here (lhde-import: ~/.local/share/lhde-windows), the user's
#                         settings neither (~/.config/lhde); docs/LAYOUT.md "The three trees"
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
#   luxshell/luxshell.mk  luxshell -> build/bin (its Lua stays in luxshell/, installed to PKGLIBDIR/luxshell)
#   examples/examples.mk  extendframe, navdemo, glassdemo -> build/examples
#   liblhde/liblhde.mk    liblhde (lhde/data.h, lhde/theme.h, lhde/settings.h): the data directories, the theme resolver,
#                         the settings store -> build/lib/liblhde.{a,so.1}, lhde.pc, build/bin/lhde-theme
#   data/ session/ tools/ installed as they are: DATADIR (themes/*.theme, navbar/*.ini, icons/*.theme, sources/*.ini,
#                         frames/*/*.ini, packs/*/shell.lua), PKGLIBDIR/session + BINDIR/lhde-session,
#                         PKGLIBDIR/tools + BINDIR/lhde-import, lhde-pack, lhde-schemes
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
# the code that is not a binary: luxshell's Lua, lhde-session's helpers, lhde-import's Python. The programs find it as
# <their bindir>/../lib/lhde (liblhde: lhde_component_dir), so it stays $(PREFIX)/lib/lhde whatever LIBDIR is.
PKGLIBDIR      ?= $(PREFIX)/lib/lhde
APPDIR         ?= $(PREFIX)/share/applications
PKGCONFIGDIR   ?= $(LIBDIR)/pkgconfig
# The file chooser portal backend: the .portal file goes where xdg-desktop-portal
# looks (its own datadir: PORTALDIR=/usr/share/xdg-desktop-portal/portals for a
# system portal), the D-Bus activation file into the session bus's service dirs.
PORTALDIR      ?= $(PREFIX)/share/xdg-desktop-portal/portals
DBUSSERVICEDIR ?= $(PREFIX)/share/dbus-1/services

MANDIR         ?= $(PREFIX)/share/man
# the lux side's C optimisation (luxwm, luxnav, liblhde; -Os as berry's configure chose)
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

# make check: tests/check.sh (tests/tests.mk), then the lux side's, then the free baseline (after luxwm's regression, whose
# classic renders it compares with)
check: check-lux
check-lux: all
	@$(MAKE) --no-print-directory check-liblhde check-luxshell check-luxwm
	@$(MAKE) --no-print-directory check-free
# make check-free: every shipped theme resolves with only the reactos source imported (build/free/windows, from ReactOS's ISO:
# LHDE_REACTOS_ISO or ~/lib/windows_isos; skipped without one), and luxwm's classic frames render from it (tests/check-free.sh)
check-free: build/bin/lhde-theme build/luxwm/tests/frametest
	@tests/check-free.sh

# data/ (text only: the theme files, navbar layouts, icon recipes, import sources, the frame sets' theme.ini/nav.ini, the packs'
# shell.lua) into DATADIR; session/ (lhde-session's helpers) and tools/ (lhde-import's Python) into PKGLIBDIR; the commands into
# BINDIR.
SESSION_FILES = lab.sh lhwall.sh lhde-paths.sh theme-reload.sh theme-watch.sh capture.sh hovertest.sh \
                picom-glass.conf picom-glass-fvwm.conf lab.fvwm2rc
TOOL_FILES    = msstyles2theme.py build_aerovision.py build_aero5112.py lhdeimport.py lhdeschemes.py lhdetree.py wim0108.py \
                lhdebaml.py lhdeaurora.py
install-data:
	install -d $(DESTDIR)$(DATADIR)/frames $(DESTDIR)$(DATADIR)/packs $(DESTDIR)$(BINDIR) \
	           $(DESTDIR)$(DATADIR)/themes $(DESTDIR)$(DATADIR)/navbar $(DESTDIR)$(DATADIR)/icons $(DESTDIR)$(DATADIR)/sources \
	           $(DESTDIR)$(PKGLIBDIR)/session $(DESTDIR)$(PKGLIBDIR)/tools
	install -m 644 data/themes/*.theme $(DESTDIR)$(DATADIR)/themes/
	install -m 644 data/navbar/*.ini $(DESTDIR)$(DATADIR)/navbar/
	install -m 644 data/icons/*.theme $(DESTDIR)$(DATADIR)/icons/
	install -m 644 data/sources/*.ini $(DESTDIR)$(DATADIR)/sources/
	for d in data/frames/*/; do n=$$(basename $$d); install -d $(DESTDIR)$(DATADIR)/frames/$$n; install -m 644 $$d*.ini $(DESTDIR)$(DATADIR)/frames/$$n/; done
	for d in data/packs/*/; do n=$$(basename $$d); install -d $(DESTDIR)$(DATADIR)/packs/$$n; install -m 644 $$d*.lua $(DESTDIR)$(DATADIR)/packs/$$n/; done
	install -m 755 session/lhde-session $(DESTDIR)$(BINDIR)/
	install -m 755 tools/lhde-import tools/lhde-pack tools/lhde-schemes $(DESTDIR)$(BINDIR)/
	cd session && install -m 644 $(SESSION_FILES) $(DESTDIR)$(PKGLIBDIR)/session/
	chmod 755 $(addprefix $(DESTDIR)$(PKGLIBDIR)/session/,lab.sh theme-reload.sh theme-watch.sh capture.sh hovertest.sh)
	ln -sf $(BINDIR)/lhde-session $(DESTDIR)$(PKGLIBDIR)/session/lhde-session
	cd tools && install -m 644 $(TOOL_FILES) $(DESTDIR)$(PKGLIBDIR)/tools/
uninstall-data:
	rm -f $(DESTDIR)$(BINDIR)/lhde-session $(DESTDIR)$(BINDIR)/lhde-import $(DESTDIR)$(BINDIR)/lhde-pack $(DESTDIR)$(BINDIR)/lhde-schemes
	rm -rf $(DESTDIR)$(DATADIR)/frames $(DESTDIR)$(DATADIR)/packs $(DESTDIR)$(DATADIR)/themes $(DESTDIR)$(DATADIR)/navbar \
	       $(DESTDIR)$(DATADIR)/icons $(DESTDIR)$(DATADIR)/sources \
	       $(DESTDIR)$(PKGLIBDIR)/session $(DESTDIR)$(PKGLIBDIR)/tools
	-rmdir $(DESTDIR)$(PKGLIBDIR)

# A clean Windows tree ($LHDE_WINDOWS, else ~/.local/share/lhde-windows; docs/USAGE.md "Getting the Windows resources"): each
# target deletes the tree first (lhde-import --clean asks on the terminal; CLEAN=1 deletes without asking), then imports.
#   make install-resources-free                          only free files: ReactOS's boot CD (LHDE_REACTOS_ISO=FILE, an ISO or a
#                                                        pack made with lhde-pack --sources reactos; else the newest
#                                                        ReactOS-*.iso in ~/lib/windows_isos; else the download is named)
#   make install-resources-windows [PACK=lhde_winthemes.zip]     the small Windows pack (lhde-pack's essentials) alone
#   make install-resources-full [PACK=lhde_wintheme_full.zip]    the free files and the whole Windows pack (lhde-pack's museum)
# The packs are the user's (lhde-pack --zip, or its .tar.zst / .tar.xz / a directory); the names are only defaults.
REACTOS_RELEASE = https://sourceforge.net/projects/reactos/files/ReactOS/0.4.16/
WINDOWS_PACK ?= lhde_winthemes.zip
FULL_PACK    ?= lhde_wintheme_full.zip
free_medium   = $(or $(LHDE_REACTOS_ISO),$(lastword $(sort $(wildcard $(HOME)/lib/windows_isos/ReactOS-*.iso))))
need_free     = m='$(free_medium)'; if [ -z "$$m" ] || [ ! -e "$$m" ]; then \
	  echo "No ReactOS medium$${m:+ ($$m not found)}. Download ReactOS 0.4.16's boot CD (ReactOS-0.4.16-i386.iso, in the release's"; \
	  echo "iso .zip) from $(REACTOS_RELEASE) and run: make $@ LHDE_REACTOS_ISO=/path/to/ReactOS-0.4.16-i386.iso"; exit 1; fi
need_pack     = p='$(1)'; if [ ! -e "$$p" ]; then \
	  echo "No pack $$p: name yours with PACK=FILE (lhde-pack makes one from your ISOs, docs/USAGE.md; lhde ships no Windows files)"; exit 1; fi
clean_tree    = tools/lhde-import --clean $(if $(CLEAN),--yes)
install-resources-free:
	@$(need_free)
	$(clean_tree)
	tools/lhde-import $(if $(filter %.iso,$(free_medium)),--iso '$(free_medium)' --source reactos,'$(free_medium)')
	tools/lhde-import --list
install-resources-windows:
	@$(call need_pack,$(or $(PACK),$(WINDOWS_PACK)))
	$(clean_tree)
	tools/lhde-import --pack '$(or $(PACK),$(WINDOWS_PACK))'
	tools/lhde-import --list
install-resources-full:
	@$(need_free)
	@$(call need_pack,$(or $(PACK),$(FULL_PACK)))
	$(clean_tree)
	tools/lhde-import $(if $(filter %.iso,$(free_medium)),--iso '$(free_medium)' --source reactos,'$(free_medium)')
	tools/lhde-import --pack '$(or $(PACK),$(FULL_PACK))'
	tools/lhde-import --list

clean:
	rm -rf build

install: install-libs install-liblhde install-flxp install-luxnav install-luxwm install-luxshell install-data
uninstall: uninstall-luxshell uninstall-luxwm uninstall-luxnav uninstall-data uninstall-liblhde uninstall-libs uninstall-flxp

.PHONY: all libs programs tests clean install uninstall check check-lux check-free install-data uninstall-data \
        install-resources-free install-resources-windows install-resources-full
.PRECIOUS: $(O)/%.o $(P)/%.o
.DELETE_ON_ERROR:
