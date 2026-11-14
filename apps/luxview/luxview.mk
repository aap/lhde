# luxview: lhde's picture viewer (docs/USAGE.md "luxview"): XP's Windows Picture and Fax Viewer, 4051's Windows Picture
# Viewer under a Longhorn theme; its view model (model.h, model_native.cpp: the list, the loader thread, the generations)
# is plain C++ that tests/toolkit/luxviewtest.cpp drives. Copy To is the Save dialog, so the file dialog's objects.
# Installed, with luxview.desktop (the pictures it decodes: Open With, the images role).
LUXVIEW_OBJS = $(call app_objs,luxview)
build/bin/luxview: $(LUXVIEW_OBJS) $(WUX_APP_DEPS) $(WUX_APP_FILEDIALOG)
	$(app_link)
APPS += luxview
apps programs: build/bin/luxview

install-apps: install-luxview
uninstall-apps: uninstall-luxview
install-luxview:
	install -d $(DESTDIR)$(APPDIR)
	sed 's|@BINDIR@|$(BINDIR)|g' apps/luxview/luxview.desktop.in > $(DESTDIR)$(APPDIR)/luxview.desktop
uninstall-luxview:
	rm -f $(DESTDIR)$(APPDIR)/luxview.desktop
.PHONY: install-luxview uninstall-luxview
