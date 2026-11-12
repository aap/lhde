# luxattr: the attributes editor (docs/USAGE.md "Descriptions and tags"), the shell layer's Properties sheet
# (shell::properties) from a terminal, opened on its Summary page; it needs no lhde session. Installed.
LUXATTR_OBJS = $(call app_objs,luxattr)
build/bin/luxattr: $(LUXATTR_OBJS) $(WUX_APP_DEPS)
	$(app_link)
APPS += luxattr
apps programs: build/bin/luxattr
