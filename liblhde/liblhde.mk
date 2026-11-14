# liblhde, the desktop-wide library: lhde's data directories (<lhde/data.h>), the theme
# resolver (<lhde/theme.h>), the settings store (<lhde/settings.h>) and the hardware layer (<lhde/hw.h>). Plain C, no X; style probing through libwuxtheme.
# Included by the top-level Makefile, before luxnav.mk (libluxnav carries data.o).
#   build/lib/liblhde.a, build/lib/liblhde.so.1 (+ .so), build/lib/pkgconfig/lhde.pc
#   build/bin/lhde-theme                  the command-line tool (list, resolve NAME --why, set NAME, explain, dirs,
#                                         find; explain.c is its own, not the library's)
#   build/bin/lhde-hw                     the hardware layer (lhde/hw.h) from the command line
#   build/liblhde/tests/resolve           make check-liblhde: inheritance, none, defaults per style kind, fallbacks, cycles,
#                                         the settings store, on fake files (no Microsoft files)
#   build/liblhde/tests/hw                make check-liblhde: the hardware layer's Linux and FreeBSD back ends over recorded
#                                         trees (LHDE_HW_FAKE) and stand-in commands
LHDE_SRCS    = $(addprefix liblhde/,data.c ini.c style.c theme.c current.c settings.c icon.c hw.c hw_linux.c hw_freebsd.c)
LHDE_OBJS    = $(patsubst %.c,$(O)/%.o,$(LHDE_SRCS))
LHDEA        = build/lib/liblhde.a
LHDESO       = build/lib/liblhde.so.1
LHDEPC       = build/lib/pkgconfig/lhde.pc
LHDE_VERSION = 0.1
LHDE_CFLAGS  = -std=gnu11 $(LUX_COPT) -fPIC -Wall -Wextra -MMD -MP -Iliblhde -idirafter . $(shell pkg-config --cflags libpng)
LHDE_PNG     = $(shell pkg-config --libs libpng)   # lhde/icon.h reads PNG icons: every program linking liblhde.a adds it
$(O)/liblhde/%.o: liblhde/%.c
	@mkdir -p $(dir $@)
	$(CC) $(LHDE_CFLAGS) -c $< -o $@
$(LHDEA): $(LHDE_OBJS)
	@mkdir -p $(dir $@)
	rm -f $@; ar rcs $@ $^
$(LHDESO): $(LHDE_OBJS) $(UTSO)
	@mkdir -p $(dir $@)
	$(CC) -shared -o $@ $(LHDE_OBJS) -Wl,-soname,liblhde.so.1 -Lbuild/lib -lwuxtheme $(LHDE_PNG) '-Wl,-rpath,$$ORIGIN'
	ln -sf liblhde.so.1 build/lib/liblhde.so
define lhde_pc
prefix=$(1)
libdir=$(2)
includedir=$(3)

Name: lhde
Description: lhde's data directories, theme resolver, settings store and hardware layer (lhde/data.h, lhde/theme.h, lhde/settings.h, lhde/icon.h, lhde/hw.h)
Version: $(LHDE_VERSION)
Requires.private: wuxtheme libpng
Libs: -L$${libdir} -llhde
Cflags: -I$${includedir}
endef
export LHDEPC_BUILD   = $(call lhde_pc,$(CURDIR),$${prefix}/build/lib,$${prefix}/liblhde)
export LHDEPC_INSTALL = $(call lhde_pc,$(PREFIX),$(LIBDIR),$(INCLUDEDIR))
$(LHDEPC): liblhde/liblhde.mk
	@mkdir -p $(dir $@)
	printf '%s\n' "$$LHDEPC_BUILD" > $@
build/bin/lhde-theme: $(O)/liblhde/lhde-theme.o $(O)/liblhde/explain.o $(LHDEA) $(UTSO)
	@mkdir -p $(dir $@)
	$(CC) -o $@ $(O)/liblhde/lhde-theme.o $(O)/liblhde/explain.o $(LHDEA) -Lbuild/lib -lwuxtheme $(LHDE_PNG) '-Wl,-rpath,$$ORIGIN/../lib'
build/bin/lhde-hw: $(O)/liblhde/lhde-hw.o $(LHDEA) $(UTSO)
	@mkdir -p $(dir $@)
	$(CC) -o $@ $(O)/liblhde/lhde-hw.o $(LHDEA) -Lbuild/lib -lwuxtheme $(LHDE_PNG) -lm '-Wl,-rpath,$$ORIGIN/../lib'
build/liblhde/tests/hw: $(O)/liblhde/tests/hw.o $(LHDEA) $(UTSO)
	@mkdir -p $(dir $@)
	$(CC) -o $@ $< $(LHDEA) -Lbuild/lib -lwuxtheme $(LHDE_PNG) -lm '-Wl,-rpath,$$ORIGIN/../../lib'
build/liblhde/tests/resolve: $(O)/liblhde/tests/resolve.o $(LHDEA) $(UTSO)
	@mkdir -p $(dir $@)
	$(CC) -o $@ $< $(LHDEA) -Lbuild/lib -lwuxtheme $(LHDE_PNG) '-Wl,-rpath,$$ORIGIN/../../lib'
liblhde: $(LHDEA) $(LHDESO) $(LHDEPC) build/bin/lhde-theme build/bin/lhde-hw build/liblhde/tests/resolve build/liblhde/tests/hw
check-liblhde: build/liblhde/tests/resolve build/liblhde/tests/hw build/bin/lhde-hw
	@build/liblhde/tests/resolve > build/liblhde/tests/resolve.log 2>&1 && echo "ok    liblhde resolve ($$(tail -n 1 build/liblhde/tests/resolve.log))" \
	 || { cat build/liblhde/tests/resolve.log; echo "FAIL  liblhde resolve"; exit 1; }
	@build/liblhde/tests/hw > build/liblhde/tests/hw.log 2>&1 && echo "ok    liblhde hardware layer ($$(tail -n 1 build/liblhde/tests/hw.log))" \
	 || { grep -v '^ok' build/liblhde/tests/hw.log; echo "FAIL  liblhde hardware layer"; exit 1; }
install-liblhde: $(LHDESO) $(LHDEA) build/bin/lhde-theme build/bin/lhde-hw
	install -d $(DESTDIR)$(LIBDIR) $(DESTDIR)$(INCLUDEDIR)/lhde $(DESTDIR)$(PKGCONFIGDIR) $(DESTDIR)$(BINDIR)
	install -m 755 $(LHDESO) $(DESTDIR)$(LIBDIR)/liblhde.so.1
	ln -sf liblhde.so.1 $(DESTDIR)$(LIBDIR)/liblhde.so
	install -m 644 $(LHDEA) $(DESTDIR)$(LIBDIR)/
	install -m 644 liblhde/lhde/data.h liblhde/lhde/theme.h liblhde/lhde/settings.h liblhde/lhde/icon.h liblhde/lhde/hw.h $(DESTDIR)$(INCLUDEDIR)/lhde/
	printf '%s\n' "$$LHDEPC_INSTALL" > $(DESTDIR)$(PKGCONFIGDIR)/lhde.pc
	install -m 755 build/bin/lhde-theme build/bin/lhde-hw $(DESTDIR)$(BINDIR)/
uninstall-liblhde:
	rm -f $(DESTDIR)$(LIBDIR)/liblhde.so $(DESTDIR)$(LIBDIR)/liblhde.so.1 $(DESTDIR)$(LIBDIR)/liblhde.a $(DESTDIR)$(PKGCONFIGDIR)/lhde.pc
	rm -rf $(DESTDIR)$(INCLUDEDIR)/lhde; rm -f $(DESTDIR)$(BINDIR)/lhde-theme $(DESTDIR)$(BINDIR)/lhde-hw
-include $(LHDE_OBJS:.o=.d) $(O)/liblhde/lhde-theme.d $(O)/liblhde/explain.d $(O)/liblhde/tests/resolve.d $(O)/liblhde/lhde-hw.d $(O)/liblhde/tests/hw.d
.PHONY: liblhde check-liblhde install-liblhde uninstall-liblhde
