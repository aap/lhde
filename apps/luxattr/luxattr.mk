# luxattr: the attributes editor (docs/USAGE.md "Metadata"), the shell layer's Properties sheet
# (shell::properties) from a terminal, opened on its Metadata page, with the file dialog for Run as's Browse...; it needs
# no lhde session. Installed.
LUXATTR_OBJS = $(call app_objs,luxattr)
build/bin/luxattr: $(LUXATTR_OBJS) $(WUX_APP_DEPS) $(WUX_APP_FILEDIALOG)
	$(app_link)
APPS += luxattr
apps programs: build/bin/luxattr
