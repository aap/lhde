# skeleton: the smallest lhde application, the copy-and-rename start of a new one (docs/APPLICATIONS.md). Built into
# build/bin, not installed.
SKELETON_OBJS = $(call app_objs,skeleton)
build/bin/skeleton: $(SKELETON_OBJS) $(WUX_APP_DEPS) $(WUX_APP_FILEDIALOG)
	$(app_link)
$(O)/apps/skeleton/skeleton.o: apps/skeleton/skeleton.res
APPS_DEV += skeleton
apps: build/bin/skeleton
