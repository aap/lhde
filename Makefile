# lhde, the Longhorn desktop environment: the top-level build.
#
#   make                  everything: libwux.a, libwuxtheme.{a,so.1}, libluxnav.{a,so.1}, the flxp
#                         programs, ut-dump, luxwm, luxwmc, luxshell, the examples and the tests, into build/
#   make check            the tests that need no Microsoft files (tests/check.sh), luxshell's build
#                         and Lua, luxwm's frame regression (skips without the user's styles), the free baseline
#                         (make check-free: only ReactOS imported; skips without its pack or ISO)
#   make install-resources-free   the free resources (ReactOS's files, GPL): the published pack downloaded and imported into
#                         the Windows tree, no ISO needed; make free-pack builds that pack (below)
#   make check-bare       make check with nothing imported or configured (empty temporary homes): what CI runs
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
#   share/ session/ tools/ installed as they are: DATADIR (themes/*.theme, navbar/*.ini, icons/*.theme, sources/*.ini,
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
include liblhde/liblhde.mk     # before luxnav.mk and flxp.mk: libluxnav and flxp carry its data.o, ini.o
include luxnav/luxnav.mk       # before flxp.mk: its programs link $(LUXNAVA)
include wux/wux.mk
include flxp/flxp.mk
include tests/tests.mk
include luxwm/luxwm.mk
include luxshell/luxshell.mk
include examples/examples.mk
include apps/app.mk            # after wux.mk and flxp.mk: the recipe an application includes (docs/APPLICATIONS.md)
include apps/skeleton/skeleton.mk
include apps/luxshot/luxshot.mk

all: libs programs tests liblhde luxwm luxshell examples
libs: $(WUXA) $(UTA) $(UTSO) $(UTPC) $(LHDEA) $(LHDESO) $(LHDEPC) $(LUXNAVA) $(LUXNAVSO) $(LUXNAVPC)
programs: $(PROGRAMS) $(LUXWM_PROGRAMS) build/bin/luxshell build/bin/lhde-theme

# make check: tests/check.sh (tests/tests.mk), then the lux side's, then the free baseline (after luxwm's regression, whose
# classic renders it compares with)
check: check-lux
check-lux: all
	@$(MAKE) --no-print-directory check-liblhde check-luxshell check-luxwm
	@$(MAKE) --no-print-directory check-free
# make check-free: every shipped theme resolves with only the reactos source imported (build/free/windows, from the free pack:
# FREE_PACK, or ReactOS's ISO: LHDE_REACTOS_ISO or ~/lib/windows_isos, or the pack make fetch-free-pack downloaded; skipped
# without any), and luxwm's classic frames render from it (tests/check-free.sh)
check-free: build/bin/lhde-theme build/luxwm/tests/frametest
	@FREE_PACK='$(FREE_PACK)' FREE_PACK_CACHE='$(FREE_PACK_CACHE)' tests/check-free.sh
# make check-bare: the whole make check with an empty Windows tree and empty config, data, state and cache homes (fresh temporary
# directories, HOME too): every test passes or skips with a message, none fails for lack of resources; what a CI job runs
check-bare: all
	@tests/check-bare.sh

# share/ (text only: the theme files, navbar layouts, icon recipes, import sources, the frame sets' theme.ini/nav.ini, the packs'
# shell.lua) into DATADIR; session/ (lhde-session's helpers) and tools/ (lhde-import's Python) into PKGLIBDIR; the commands into
# BINDIR.
SESSION_FILES = lab.sh lhwall.sh lhde-paths.sh theme-reload.sh theme-watch.sh capture.sh hovertest.sh \
                picom-glass.conf picom-glass-fvwm.conf lab.fvwm2rc
TOOL_FILES    = msstyles2theme.py build_aerovision.py build_aero5112.py lhdeimport.py lhdeschemes.py lhdetree.py wim0108.py \
                lhdebaml.py lhdeaurora.py lhdestyles.py
install-data:
	install -d $(DESTDIR)$(DATADIR)/frames $(DESTDIR)$(DATADIR)/packs $(DESTDIR)$(BINDIR) \
	           $(DESTDIR)$(DATADIR)/themes $(DESTDIR)$(DATADIR)/navbar $(DESTDIR)$(DATADIR)/icons $(DESTDIR)$(DATADIR)/sources \
	           $(DESTDIR)$(PKGLIBDIR)/session $(DESTDIR)$(PKGLIBDIR)/tools
	install -m 644 share/themes/*.theme $(DESTDIR)$(DATADIR)/themes/
	install -m 644 share/navbar/*.ini $(DESTDIR)$(DATADIR)/navbar/
	install -m 644 share/icons/*.theme $(DESTDIR)$(DATADIR)/icons/
	install -m 644 share/sources/*.ini $(DESTDIR)$(DATADIR)/sources/
	for d in share/frames/*/; do n=$$(basename $$d); install -d $(DESTDIR)$(DATADIR)/frames/$$n; install -m 644 $$d*.ini $(DESTDIR)$(DATADIR)/frames/$$n/; done
	for d in share/packs/*/; do n=$$(basename $$d); install -d $(DESTDIR)$(DATADIR)/packs/$$n; install -m 644 $$d*.lua $(DESTDIR)$(DATADIR)/packs/$$n/; done
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

# The free resources as a pack (docs/BUILDING.md "The free resources"): ReactOS 0.4.16's files, GPL and other free licences,
# made once from its boot CD by make free-pack and published; make install-resources-free downloads it, and nobody needs the ISO.
#   make free-pack                 $(FREE_PACK_NAME).zip in the tree root (gitignored; a zip as the Windows packs: lhde-import
#                                  unpacks it without tar or zstd) from REACTOS_ISO, with a LICENSE note
#                                  (share/sources/reactos-LICENSE.txt) and the licence texts of REACTOS_SRC (ReactOS's source at
#                                  tag 0.4.16: COPYING*, CREDITS, media/fonts/doc); reproducible: the same ISO gives the same
#                                  bytes, so its sha256 is FREE_PACK_SHA256's; prints the sha256 and the size
#   make fetch-free-pack           the pack downloaded from FREE_PACK_URL into FREE_PACK_CACHE and checked against FREE_PACK_SHA256
#                                  (kept there: make check-free and install-resources-free use it; a CI runs this first)
REACTOS_VERSION   = 0.4.16
REACTOS_RELEASE   = https://sourceforge.net/projects/reactos/files/ReactOS/$(REACTOS_VERSION)/
REACTOS_ISO      ?= $(HOME)/lib/windows_isos/ReactOS-$(REACTOS_VERSION)-i386.iso
REACTOS_SRC      ?= $(HOME)/lib/reactos-$(REACTOS_VERSION)
FREE_PACK_NAME    = lhde-resources-reactos-$(REACTOS_VERSION)
# FREE_PACK_URL is a placeholder until the pack is published (aap: a release asset of the repository); FREE_PACK_SHA256 is the
# sha256 of the pack make free-pack builds from the release's ISO
FREE_PACK_URL    ?= https://github.com/aap/lhde/releases/download/resources/$(FREE_PACK_NAME).zip
FREE_PACK_SHA256  = 474cac09b8840a1c222534119463f41a220174fa1e4bee6902a0c93c767b9480
FREE_PACK_CACHE  ?= $(or $(XDG_CACHE_HOME),$(HOME)/.cache)/lhde/$(FREE_PACK_NAME).zip
FREE_FONT_DOCS    = Arial ArialBlack ComicSansMS CourierNew FranklinGothicMedium Georgia LucidaConsole MicrosoftSansSerif \
                    PalatinoLinotype Tahoma TimesNewRoman TrebuchetMS Verdana
free-pack:
	@[ -f '$(REACTOS_ISO)' ] || { echo "No $(REACTOS_ISO): ReactOS $(REACTOS_VERSION)'s boot CD (ReactOS-$(REACTOS_VERSION)-i386.iso, in the release's iso .zip, $(REACTOS_RELEASE)); make $@ REACTOS_ISO=FILE"; exit 1; }
	@[ -f '$(REACTOS_SRC)/COPYING' ] && [ -d '$(REACTOS_SRC)/media/fonts/doc' ] || { echo "No ReactOS source in $(REACTOS_SRC), where the licence texts come from (its tag $(REACTOS_VERSION): https://github.com/reactos/reactos); make $@ REACTOS_SRC=DIR"; exit 1; }
	rm -rf build/free-pack-notes $(FREE_PACK_NAME) $(FREE_PACK_NAME).zip
	mkdir -p build/free-pack-notes/licenses/fonts
	cp share/sources/reactos-LICENSE.txt build/free-pack-notes/LICENSE
	cd '$(REACTOS_SRC)' && cp COPYING COPYING.LIB COPYING3 COPYING3.LIB CREDITS $(CURDIR)/build/free-pack-notes/
	cd '$(REACTOS_SRC)/media/fonts/doc' && cp -r $(FREE_FONT_DOCS) $(CURDIR)/build/free-pack-notes/licenses/fonts/
	tools/lhde-pack --profile museum --sources reactos --medium reactos='$(REACTOS_ISO)' --no-local --reproducible \
	    --no-tar --zip --notes build/free-pack-notes --name $(FREE_PACK_NAME) --out . --tmp build
	rm -rf $(FREE_PACK_NAME) build/free-pack-notes
	@h=$$(sha256sum < $(FREE_PACK_NAME).zip | cut -d' ' -f1); \
	echo "$(FREE_PACK_NAME).zip: $$(stat -c %s $(FREE_PACK_NAME).zip) bytes, sha256 $$h"; \
	if [ "$$h" = '$(FREE_PACK_SHA256)' ]; then echo "(the pack FREE_PACK_SHA256 names)"; \
	else echo "(a new pack: FREE_PACK_SHA256 names $(FREE_PACK_SHA256); publish this one and set it)"; fi
fetch_free = f='$(FREE_PACK_CACHE)'; sum() { sha256sum < "$$1" | cut -d' ' -f1; }; \
	if [ -f "$$f" ] && [ "$$(sum "$$f")" = '$(FREE_PACK_SHA256)' ]; then echo "$$f (cached, sha256 checked)"; else \
	  mkdir -p "$$(dirname "$$f")" && rm -f "$$f.part" && echo "downloading $(FREE_PACK_URL) to $$f" && \
	  if command -v curl > /dev/null; then curl -fL --retry 2 -o "$$f.part" '$(FREE_PACK_URL)'; \
	  elif command -v wget > /dev/null; then wget -O "$$f.part" '$(FREE_PACK_URL)'; \
	  else echo "neither curl nor wget: download $(FREE_PACK_URL) to $$f yourself"; false; fi || \
	    { rm -f "$$f.part"; echo "The download failed. The pack: $(FREE_PACK_URL); or FREE_PACK=FILE, or ReactOS's ISO (LHDE_REACTOS_ISO=FILE)"; exit 1; }; \
	  h=$$(sum "$$f.part"); [ "$$h" = '$(FREE_PACK_SHA256)' ] || \
	    { rm -f "$$f.part"; echo "$(FREE_PACK_URL): sha256 $$h, not $(FREE_PACK_SHA256): not imported"; exit 1; }; \
	  mv "$$f.part" "$$f"; fi
fetch-free-pack:
	@$(fetch_free)

# A clean Windows tree ($LHDE_WINDOWS, else ~/.local/share/lhde-windows; docs/USAGE.md "Getting the Windows resources"): each
# target deletes the tree first (lhde-import --clean asks on the terminal; CLEAN=1 deletes without asking), then imports.
#   make install-resources-free                          only free files, ReactOS's: FREE_PACK=FILE (a pack of the reactos
#                                                        source, make free-pack's or lhde-pack's), else LHDE_REACTOS_ISO=FILE
#                                                        or the newest ReactOS-*.iso in ~/lib/windows_isos, else the pack
#                                                        downloaded from FREE_PACK_URL (as make fetch-free-pack: sha256 checked)
#   make install-resources-windows [PACK=lhde_winthemes.zip]     the small Windows pack (lhde-pack's essentials) alone
#   make install-resources-full [PACK=lhde_wintheme_full.zip]    the free files and the whole Windows pack (lhde-pack's museum)
# The Windows packs are the user's (lhde-pack --zip, or its .tar.zst / .tar.xz / a directory); the names are only defaults.
WINDOWS_PACK ?= lhde_winthemes.zip
FULL_PACK    ?= lhde_wintheme_full.zip
free_iso      = $(or $(LHDE_REACTOS_ISO),$(lastword $(sort $(wildcard $(HOME)/lib/windows_isos/ReactOS-*.iso))))
free_medium   = $(or $(FREE_PACK),$(free_iso),$(FREE_PACK_CACHE))
need_free     = $(if $(or $(FREE_PACK),$(free_iso)),m='$(free_medium)'; [ -e "$$m" ] || { echo "No $$m"; exit 1; },$(fetch_free))
import_free   = tools/lhde-import $(if $(filter %.iso %.ISO,$(free_medium)),--iso '$(free_medium)' --source reactos,--pack '$(free_medium)')
need_pack     = p='$(1)'; if [ ! -e "$$p" ]; then \
	  echo "No pack $$p: name yours with PACK=FILE (lhde-pack makes one from your ISOs, docs/USAGE.md; lhde ships no Windows files)"; exit 1; fi
clean_tree    = tools/lhde-import --clean $(if $(CLEAN),--yes)
install-resources-free:
	@$(need_free)
	$(clean_tree)
	$(import_free)
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
	$(import_free)
	tools/lhde-import --pack '$(or $(PACK),$(FULL_PACK))'
	tools/lhde-import --list

clean:
	rm -rf build

install: install-libs install-liblhde install-flxp install-luxnav install-luxwm install-luxshell install-data
uninstall: uninstall-luxshell uninstall-luxwm uninstall-luxnav uninstall-data uninstall-liblhde uninstall-libs uninstall-flxp

.PHONY: all libs programs tests clean install uninstall check check-lux check-free check-bare install-data uninstall-data \
        install-resources-free install-resources-windows install-resources-full free-pack fetch-free-pack
.PRECIOUS: $(O)/%.o $(P)/%.o
.DELETE_ON_ERROR:
