# apps/app.mk: the build recipe of an lhde application (docs/APPLICATIONS.md). Included by the top-level Makefile after
# wux/wux.mk and flxp/flxp.mk, before the applications' own apps/NAME/NAME.mk.
#
# An application is apps/NAME/: its C++ sources (*.cpp: win32 code on wux, compiled with the shim's flags; *_native.cpp:
# plain code without the win32 headers, e.g. Xlib, whose macros clash with them), an optional NAME.rc compiled to a
# checked-in NAME.res (windres, only when the .rc changes) and embedded with WUX_RESOURCES (wux/wux.h), and NAME.mk:
#
#   NAME_OBJS = $(call app_objs,NAME)
#   build/bin/NAME: $(NAME_OBJS) $(WUX_APP_DEPS)          # + $(WUX_APP_FILEDIALOG) for the Open/Save dialog
#   	$(app_link)
#   $(O)/apps/NAME/NAME.o: apps/NAME/NAME.res             # the .cpp that embeds it
#   APPS += NAME                                          # installed by make install (APPS_DEV += NAME: built only)
#   apps programs: build/bin/NAME                         # built by make (`programs` only for an installed one)
#
# The headers: "wux/wux.h" (the Win32 surface and wux's additions, WuxInit) and "shell/shell.h" (the shell facilities).

# libflxpshell.a: the shell facilities (flxp/shell, with liblhde's objects), the same objects flxp's programs link
SHELLA = build/lib/libflxpshell.a
$(SHELLA): $(SHELLOBJS)
	@mkdir -p $(dir $@)
	rm -f $@; ar rcs $@ $^

# compile: win32 code on the shim; *_native.cpp without the win32 headers (the shorter stem wins: GNU make 3.82)
APP_CXXFLAGS = $(CXXFLAGS) $(W32FLAGS) -Wno-missing-field-initializers -Iliblhde
$(O)/apps/%.o: apps/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(APP_CXXFLAGS) -c $< -o $@
$(O)/apps/%_native.o: apps/%_native.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Iliblhde -c $< -o $@
# dialog templates, menus, strings: windres from the .rc, the .res checked in
apps/%.res: apps/%.rc
	$(WINDRES) -O res -I $(dir $<) -o $@ $<

# link: wux and the shell facilities whole (their weak references and the style probe resolve as in flxp), then luxnav
# when the application asked for it ($(LUXNAVA) among its prerequisites: the Longhorn navigation bar, -Iluxnav; it comes
# after liblhde's objects in libflxpshell.a so its own copy of them is not pulled), libwuxtheme as an archive.
WUX_APP_DEPS = $(SHELLA) $(WUXA) $(UTA)
WUX_APP_LINK = $(UT_PROBE) -Wl,--whole-archive $(SHELLA) $(WUXA) -Wl,--no-whole-archive $(UTA) $(LDLIBS) -no-pie
# the Open/Save dialog (GetOpenFileNameW, GetSaveFileNameW, filedlg::run, filedlg::message_box): today it is built on the
# Explorer's browsing widget, so it brings flxp/explorer (all but its main) and luxnav along
WUX_APP_FILEDIALOG = $(FILEDLGLIB) $(EXPLSHARED) $(LUXNAVA)
app_objs = $(patsubst %.cpp,$(O)/%.o,$(wildcard apps/$(1)/*.cpp))
define app_link
@mkdir -p $(dir $@)
$(CXX) -o $@ $(filter %.o,$^) $(UT_PROBE) -Wl,--whole-archive $(SHELLA) $(WUXA) -Wl,--no-whole-archive $(filter $(LUXNAVA),$^) \
    $(UTA) $(LDLIBS) -no-pie
endef

APPS =
APPS_DEV =

# each NAME.mk adds its binary to `apps` (and to `programs` when installed); the lists are read when the recipes run
all: apps
install: install-apps
uninstall: uninstall-apps
install-apps: apps
	install -d $(DESTDIR)$(BINDIR)
	$(if $(APPS),install -m 755 $(addprefix build/bin/,$(APPS)) $(DESTDIR)$(BINDIR)/)
uninstall-apps:
	rm -f $(addprefix $(DESTDIR)$(BINDIR)/,$(APPS))
-include $(patsubst %.cpp,$(O)/%.d,$(wildcard apps/*/*.cpp))
.PHONY: apps install-apps uninstall-apps
