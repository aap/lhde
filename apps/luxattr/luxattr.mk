# luxattr: the attributes editor (docs/USAGE.md "Descriptions and tags"), a property sheet on wux's comctl32 that needs no
# lhde session; flxp's item menu runs it (Attributes...). Installed.
LUXATTR_OBJS = $(call app_objs,luxattr)
build/bin/luxattr: $(LUXATTR_OBJS) $(WUX_APP_DEPS)
	$(app_link)
$(O)/apps/luxattr/luxattr.o: apps/luxattr/luxattr.res
APPS += luxattr
apps programs: build/bin/luxattr
