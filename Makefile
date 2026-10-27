# lhde, the Longhorn desktop environment: the top-level build.
#
#   make                  everything: libwux.a, libwuxtheme.{a,so.1}, the flxp
#                         programs, ut-dump and the tests, into build/
#   make check            the tests that need no Microsoft files (tests/check.sh)
#   make install          PREFIX=/usr/local DESTDIR= (also LIBDIR, INCLUDEDIR, DATADIR,
#                         PORTALDIR, DBUSSERVICEDIR); make uninstall
#   make install-icons    copy the XP/2000 icon files from nt5src into DATADIR (NT5SRC=)
#   make clean
#
# Components (each with its own fragment, included below):
#   wux/wux.mk            the win32 UI layer: gdi, sys, win, res, w32, vendor/wine -> build/lib/libwux.a
#   wuxtheme/wuxtheme.mk  libwuxtheme (ut.h): build/lib/libwuxtheme.{a,so.1}, wuxtheme.pc, ut-dump
#   flxp/flxp.mk          flxp, flxp-desktop, flxp-taskbar, flxp-settings, flxp-filedialog,
#                         flxp-portal -> build/bin
#   tests/tests.mk        build/tests (toolkit tests), build/tests/w32 (shim tests), make check
#   luxnav                not here yet: LUXNAV_DIR (below)
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

# luxnav, the Longhorn navigation bar (the explorer header's vision mode,
# docs/flxp-NOTES.md "The vision header"). Until the lux side imports it into
# luxnav/, it is compiled from LUXNAV_DIR (its lhnav.c, lhnav_px.c, lhnav_res.c)
# into build/luxnav against wuxtheme/ut.h. Empty (the default): flxp is built
# without the vision header (flxp --info says so); LUXNAV_DIR=/path/to/lhnav
# builds it in, as flxp did with its LHNAV_DIR.
LUXNAV_DIR ?=

all:

include wuxtheme/wuxtheme.mk   # first: wux.mk leaves its objects out of libwux.a
include wux/wux.mk
include flxp/flxp.mk
include tests/tests.mk

all: libs programs tests
libs: $(WUXA) $(UTA) $(UTSO) $(UTPC)
programs: $(PROGRAMS)

clean:
	rm -rf build

install: install-libs install-flxp
uninstall: uninstall-libs uninstall-flxp

.PHONY: all libs programs tests clean install uninstall check
.PRECIOUS: $(O)/%.o $(P)/%.o
.DELETE_ON_ERROR:
