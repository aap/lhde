# flxp, the XP-style explorer and shell (included by the top-level Makefile).
#   shell/      the shell model: namespace, icons, associations, settings, file ops (native)
#   explorer/   the explorer (flxp) and the browsing widget the others share
#   desktop/    flxp-desktop       taskbar/   flxp-taskbar (start menu, start panel, tray)
#   filedlg/    flxp-filedialog, flxp-portal (xdg-desktop-portal FileChooser backend)
#   settings/   flxp-settings (Display Properties)
#   rc/         flxp's own dialog templates (.rc, compiled to .res with mingw windres)
#   res/        run-time data: icon themes (Wine's shell32 icons, theme files), the
#               .desktop file, the portal files, tools/install-icons.sh

# ---- luxnav (LUXNAV_DIR, see the top-level Makefile) ----------------------------------
# lhnav.h includes <uxtheme/ut.h>, as it did against flxp's tree: build/compat
# forwards that to wuxtheme/ut.h until luxnav includes <wuxtheme/ut.h>.
LUXNAV_COMPAT = build/compat/uxtheme/ut.h
$(LUXNAV_COMPAT):
	@mkdir -p $(dir $@)
	printf '%s\n' '/* lhde: <uxtheme/ut.h> for luxnav (LUXNAV_DIR) as long as it uses the old name */' '#include "../../../wuxtheme/ut.h"' > $@
# header.o is rebuilt when LUXNAV_DIR changes (a stamp, as for the data directory)
$(shell mkdir -p $(O)/flxp/explorer; [ -f $(O)/flxp/explorer/luxnav ] && [ "`cat $(O)/flxp/explorer/luxnav`" = '$(LUXNAV_DIR)' ] || echo '$(LUXNAV_DIR)' > $(O)/flxp/explorer/luxnav)
$(O)/flxp/explorer/header.o: $(O)/flxp/explorer/luxnav
ifneq ($(LUXNAV_DIR),)
ifeq ($(wildcard $(LUXNAV_DIR)/lhnav.h),)
$(error LUXNAV_DIR=$(LUXNAV_DIR) has no lhnav.h)
endif
LUXNAVOBJS = $(addprefix build/luxnav/,lhnav.o lhnav_px.o lhnav_res.o)
LUXNAVA    = build/luxnav/liblhnav.a
build/luxnav/%.o: $(LUXNAV_DIR)/%.c | $(LUXNAV_COMPAT)
	@mkdir -p $(dir $@)
	$(CC) -g -O2 -std=gnu11 -Wall -MMD -MP -DHAVE_UXTHEME -DUT_HAVE_TEXT_EXTENT -idirafter build/compat $(shell pkg-config --cflags libpng) -c $< -o $@
$(LUXNAVA): $(LUXNAVOBJS)
	rm -f $@; ar rcs $@ $^
$(O)/flxp/explorer/header.o: CXXFLAGS += -DFLXP_HAVE_LHNAV -idirafter $(LUXNAV_DIR) -idirafter build/compat
$(O)/flxp/explorer/header.o: | $(LUXNAVA) $(LUXNAV_COMPAT)
-include $(LUXNAVOBJS:.o=.d)
endif

# ---- objects ----------------------------------------------------------------------------
SHELLOBJS = $(patsubst %.cpp,$(O)/%.o,$(wildcard flxp/shell/*.cpp))
EXPLOBJS  = $(patsubst %.cpp,$(O)/%.o,$(wildcard flxp/explorer/*.cpp))
EXPLSHARED = $(filter-out $(O)/flxp/explorer/explorer.o,$(EXPLOBJS))   # everything but its main
DESKOBJS  = $(patsubst %.cpp,$(O)/%.o,$(wildcard flxp/desktop/*.cpp))
FILEDLGLIB = $(O)/flxp/filedlg/filedlg.o
TBOBJS    = $(patsubst %.cpp,$(O)/%.o,$(wildcard flxp/taskbar/*.cpp))
SETOBJS   = $(patsubst %.cpp,$(O)/%.o,$(wildcard flxp/settings/*.cpp))
FLXP_OBJS = $(SHELLOBJS) $(EXPLOBJS) $(DESKOBJS) $(TBOBJS) $(SETOBJS) $(patsubst %.cpp,$(O)/%.o,$(wildcard flxp/filedlg/*.cpp))

# the shell model is native code; the rest is win32 code on the shim
$(O)/flxp/shell/%.o: flxp/shell/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(O)/flxp/%.o: flxp/%.cpp
	@mkdir -p $(dir $@)
	$(W32CXX) -c $< -o $@
# plain Xlib / libpng+libjpeg / libdbus parts, compiled without the Wine headers
$(O)/flxp/taskbar/xwm.o: flxp/taskbar/xwm.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(O)/flxp/desktop/wallpaper.o: flxp/desktop/wallpaper.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(shell pkg-config --cflags libpng libjpeg) -c $< -o $@
$(O)/flxp/filedlg/portal.o: flxp/filedlg/portal.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(shell pkg-config --cflags dbus-1) -c $< -o $@

# the data directory, compiled into config.o (rebuilt when it changes)
$(shell mkdir -p $(O)/flxp/shell; [ "`cat $(O)/flxp/shell/datadir 2>/dev/null`" = '$(DATADIR)' ] || echo '$(DATADIR)' > $(O)/flxp/shell/datadir)
$(O)/flxp/shell/config.o: $(O)/flxp/shell/datadir
$(O)/flxp/shell/config.o: CXXFLAGS += -DFLXP_DATADIR='"$(DATADIR)"'

# dialog templates, embedded with .incbin
flxp/rc/%.res: flxp/rc/%.rc flxp/%/resource.h
	$(WINDRES) -O res -I flxp/$* -o $@ $<
$(O)/flxp/explorer/dialogs.o: flxp/rc/explorer.res
$(O)/flxp/filedlg/filedlg.o: flxp/rc/filedlg.res
$(O)/flxp/settings/settings.o: flxp/rc/settings.res

# ---- programs (build/bin) -----------------------------------------------------------------
W32LINK = $(SHELLOBJS) $(WUX_LINK)
W32DEPS = $(SHELLOBJS) $(WUXA) $(LUXNAVA) $(UTA)
build/bin/flxp: $(EXPLOBJS) $(W32DEPS)
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $(EXPLOBJS) $(W32LINK) $(LDLIBS) -no-pie
build/bin/flxp-desktop: $(DESKOBJS) $(EXPLSHARED) $(FILEDLGLIB) $(W32DEPS)
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $(DESKOBJS) $(EXPLSHARED) $(FILEDLGLIB) $(W32LINK) $(LDLIBS) $(shell pkg-config --libs libpng libjpeg) -no-pie
# the Longhorn look is built on the explorer's browsing widget (browser, header, places, ...)
build/bin/flxp-filedialog: $(O)/flxp/filedlg/main.o $(FILEDLGLIB) $(EXPLSHARED) $(W32DEPS)
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $(O)/flxp/filedlg/main.o $(FILEDLGLIB) $(EXPLSHARED) $(W32LINK) $(LDLIBS) -no-pie
build/bin/flxp-portal: $(O)/flxp/filedlg/portal.o $(O)/flxp/shell/config.o
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $^ $(shell pkg-config --libs dbus-1)
build/bin/flxp-taskbar: $(TBOBJS) $(O)/flxp/explorer/uiicons.o $(W32DEPS)
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $(TBOBJS) $(O)/flxp/explorer/uiicons.o $(W32LINK) $(LDLIBS) -no-pie
build/bin/flxp-settings: $(SETOBJS) $(FILEDLGLIB) $(O)/flxp/desktop/wallpaper.o $(EXPLSHARED) $(W32DEPS)
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $(SETOBJS) $(FILEDLGLIB) $(O)/flxp/desktop/wallpaper.o $(EXPLSHARED) $(W32LINK) $(LDLIBS) $(shell pkg-config --libs libpng libjpeg) -no-pie

FLXP_PROGRAMS = flxp flxp-desktop flxp-taskbar flxp-settings flxp-filedialog flxp-portal
PROGRAMS = $(addprefix build/bin/,$(FLXP_PROGRAMS)) $(UTDUMP)
flxp: $(addprefix build/bin/,$(FLXP_PROGRAMS))

# ---- install ------------------------------------------------------------------------------
# bin/flxp and its helpers, the run-time data in $(DATADIR) (res/icons), the
# .desktop file, the portal backend's .portal and D-Bus activation files.
# make install-icons copies the XP/2000 icon files from nt5src
# (NT5SRC=, else nt5src= in flxp.ini) into $(DATADIR).
NT5SRC  ?= $(or $(FLXP_NT5SRC),$(shell sed -n 's/^nt5src *= *//p' $${XDG_CONFIG_HOME:-$$HOME/.config}/flxp/flxp.ini 2>/dev/null))
install-flxp: flxp
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(DATADIR)/res/icons $(DESTDIR)$(APPDIR) $(DESTDIR)$(PORTALDIR) $(DESTDIR)$(DBUSSERVICEDIR)
	install -m 755 $(addprefix build/bin/,$(FLXP_PROGRAMS)) $(DESTDIR)$(BINDIR)/
	install -m 644 flxp/res/icons/*.theme flxp/res/icons/*.ico flxp/res/icons/*.bmp $(DESTDIR)$(DATADIR)/res/icons/
	sed 's|@BINDIR@|$(BINDIR)|g; s|@DATADIR@|$(DATADIR)|g' flxp/res/flxp.desktop.in > $(DESTDIR)$(APPDIR)/flxp.desktop
	install -m 644 flxp/res/portal/flxp.portal $(DESTDIR)$(PORTALDIR)/flxp.portal
	sed 's|@BINDIR@|$(BINDIR)|g' flxp/res/portal/org.freedesktop.impl.portal.desktop.flxp.service.in > $(DESTDIR)$(DBUSSERVICEDIR)/org.freedesktop.impl.portal.desktop.flxp.service
install-icons:
	flxp/res/tools/install-icons.sh xp '$(NT5SRC)' $(DESTDIR)$(DATADIR)
	flxp/res/tools/install-icons.sh 2k '$(NT5SRC)' $(DESTDIR)$(DATADIR)
uninstall-flxp:
	rm -f $(addprefix $(DESTDIR)$(BINDIR)/,$(FLXP_PROGRAMS)) $(DESTDIR)$(APPDIR)/flxp.desktop
	rm -f $(DESTDIR)$(PORTALDIR)/flxp.portal $(DESTDIR)$(DBUSSERVICEDIR)/org.freedesktop.impl.portal.desktop.flxp.service
	rm -rf $(DESTDIR)$(DATADIR)/res $(DESTDIR)$(DATADIR)/icons
	-rmdir $(DESTDIR)$(DATADIR)

-include $(FLXP_OBJS:.o=.d)
.PHONY: flxp install-flxp install-icons uninstall-flxp
