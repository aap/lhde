# flxp, the XP-style explorer and shell (included by the top-level Makefile).
#   shell/      the shell model: namespace, icons, associations, settings, file ops (native)
#   explorer/   the explorer (flxp) and the browsing widget the others share
#   desktop/    flxp-desktop       taskbar/   flxp-taskbar (start menu, start panel, tray; as flxp-startmenu the panel alone)
#   filedlg/    flxp-filedialog, flxp-portal (xdg-desktop-portal FileChooser backend)
#   settings/   flxp-settings (Display Properties)
#   rc/         flxp's own dialog templates (.rc, compiled to .res with mingw windres)
#   res/        run-time data: the wine icon theme (Wine's shell32 icons), the
#               .desktop file, the portal files

# ---- luxnav (luxnav/luxnav.mk: build/lib/libluxnav.a, included before this file) ---------------------
# The explorer header's vision mode: header.cpp includes luxnav's lhnav.h, every program links libluxnav.a through
# WUX_LINK / W32DEPS ($(LUXNAVA)).
$(O)/flxp/explorer/header.o: CXXFLAGS += -Iluxnav
$(O)/flxp/explorer/header.o: luxnav/lhnav.h | $(LUXNAVA)

# ---- objects ----------------------------------------------------------------------------
SHELLOBJS = $(patsubst %.cpp,$(O)/%.o,$(wildcard flxp/shell/*.cpp)) $(LHDE_OBJS)
# liblhde (<lhde/data.h>, <lhde/theme.h>, <lhde/settings.h>): the data directories, the theme resolver and the settings
# store; its objects are linked into every flxp program and test with the shell model
$(O)/flxp/shell/lhdata.o $(O)/flxp/shell/lhtheme.o $(O)/flxp/shell/settings.o $(O)/flxp/settings/settings.o: CXXFLAGS += -Iliblhde
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
# liblhde probes a style through weak ut_* references (liblhde/style.c): a program that does not
# call them itself (flxp-taskbar) would link without the probe and see every style as "classic"
UT_PROBE = -Wl,-u,ut_open,-u,ut_close,-u,ut_format,-u,ut_list_classes,-u,ut_color,-u,ut_size,-u,ut_get_resource,-u,ut_prop,-u,ut_get_string,-u,ut_get_bitmap,-u,ut_free
W32LINK = $(UT_PROBE) $(SHELLOBJS) $(WUX_LINK)
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
PROGRAMS = $(addprefix build/bin/,$(FLXP_PROGRAMS)) build/bin/flxp-startmenu $(UTDUMP)
flxp: $(addprefix build/bin/,$(FLXP_PROGRAMS)) build/bin/flxp-startmenu
# flxp-startmenu: flxp-taskbar's start panel without the bar (luxshell's start-menu provider), the same binary by another name
build/bin/flxp-startmenu: build/bin/flxp-taskbar
	ln -sfn flxp-taskbar $@

# ---- install ------------------------------------------------------------------------------
# bin/flxp and its helpers, the run-time data in $(DATADIR) (res/icons), the
# .desktop file, the portal backend's .portal and D-Bus activation files.
# The xp/2k/longhorn4074 icon recipes are data/icons (installed with data/); their
# art is read by resource id from lhde-import's resources/<build>.
install-flxp: flxp
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(DATADIR)/res/icons $(DESTDIR)$(APPDIR) $(DESTDIR)$(PORTALDIR) $(DESTDIR)$(DBUSSERVICEDIR)
	install -m 755 $(addprefix build/bin/,$(FLXP_PROGRAMS)) $(DESTDIR)$(BINDIR)/
	ln -sfn flxp-taskbar $(DESTDIR)$(BINDIR)/flxp-startmenu
	install -m 644 flxp/res/icons/*.theme flxp/res/icons/*.ico flxp/res/icons/*.bmp $(DESTDIR)$(DATADIR)/res/icons/
	sed 's|@BINDIR@|$(BINDIR)|g; s|@DATADIR@|$(DATADIR)|g' flxp/res/flxp.desktop.in > $(DESTDIR)$(APPDIR)/flxp.desktop
	install -m 644 flxp/res/portal/flxp.portal $(DESTDIR)$(PORTALDIR)/flxp.portal
	sed 's|@BINDIR@|$(BINDIR)|g' flxp/res/portal/org.freedesktop.impl.portal.desktop.flxp.service.in > $(DESTDIR)$(DBUSSERVICEDIR)/org.freedesktop.impl.portal.desktop.flxp.service
uninstall-flxp:
	rm -f $(addprefix $(DESTDIR)$(BINDIR)/,$(FLXP_PROGRAMS)) $(DESTDIR)$(BINDIR)/flxp-startmenu $(DESTDIR)$(APPDIR)/flxp.desktop
	rm -f $(DESTDIR)$(PORTALDIR)/flxp.portal $(DESTDIR)$(DBUSSERVICEDIR)/org.freedesktop.impl.portal.desktop.flxp.service
	rm -rf $(DESTDIR)$(DATADIR)/res
	-rmdir $(DESTDIR)$(DATADIR)

-include $(FLXP_OBJS:.o=.d)
.PHONY: flxp install-flxp uninstall-flxp
