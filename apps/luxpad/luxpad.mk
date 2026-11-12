# luxpad: lhde's notepad (docs/USAGE.md "luxpad"), ReactOS's Notepad ported to wux: the Open/Save dialog (its Encoding boxes
# under Save As) and the file dialog's message box, so the file dialog's objects. Installed, with luxpad.desktop (text/plain:
# flxp's Open With and a double click reach it) and its icon.
LUXPAD_OBJS = $(call app_objs,luxpad)
build/bin/luxpad: $(LUXPAD_OBJS) $(WUX_APP_DEPS) $(WUX_APP_FILEDIALOG)
	$(app_link)
$(O)/apps/luxpad/main.o: apps/luxpad/luxpad.res
apps/luxpad/luxpad.res: apps/luxpad/luxpad.ico apps/luxpad/resource.h
APPS += luxpad
apps programs: build/bin/luxpad

install-apps: install-luxpad
uninstall-apps: uninstall-luxpad
install-luxpad:
	install -d $(DESTDIR)$(APPDIR) $(DESTDIR)$(DATADIR)/res/icons
	install -m 644 apps/luxpad/luxpad.ico $(DESTDIR)$(DATADIR)/res/icons/luxpad.ico
	sed 's|@BINDIR@|$(BINDIR)|g; s|@DATADIR@|$(DATADIR)|g' apps/luxpad/luxpad.desktop.in > $(DESTDIR)$(APPDIR)/luxpad.desktop
uninstall-luxpad:
	rm -f $(DESTDIR)$(APPDIR)/luxpad.desktop $(DESTDIR)$(DATADIR)/res/icons/luxpad.ico
.PHONY: install-luxpad uninstall-luxpad
