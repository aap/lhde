# luxshot: the screenshot tool (docs/USAGE.md "Screenshots"), the first application on apps/app.mk's recipe; the file
# dialog for Browse... and its message boxes. Installed.
LUXSHOT_OBJS = $(call app_objs,luxshot)
build/bin/luxshot: $(LUXSHOT_OBJS) $(WUX_APP_DEPS) $(WUX_APP_FILEDIALOG)
	$(app_link)
$(O)/apps/luxshot/luxshot.o: apps/luxshot/luxshot.res
APPS += luxshot
apps programs: build/bin/luxshot
