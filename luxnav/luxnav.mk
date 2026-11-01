# luxnav, the Longhorn navigation bar library (docs/NAVBAR.md). Included by the top-level Makefile.
# Plain C over libwuxtheme (art and all text) and libpng; no cairo, no pango. The C API keeps its lhnav_ prefix and its header
# lhnav.h (like wuxtheme's ut_), so flxp's header.cpp and navdemo use it unchanged.
#   build/lib/libluxnav.a                 linked into flxp (the explorer header's vision mode) and navdemo
#   build/lib/libluxnav.so.1 (+ .so)      for other programs
#   build/lib/pkgconfig/luxnav.pc         pkg-config for the build tree; make install writes the installed one
# libluxnav carries liblhde/data.o (nav.ini's icon_dir is relative to lhde's data directories).

LUXNAV_SRCS   = $(addprefix luxnav/,lhnav.c lhnav_px.c lhnav_res.c)
LUXNAV_OBJS   = $(patsubst %.c,$(O)/%.o,$(LUXNAV_SRCS)) $(O)/liblhde/data.o
LUXNAVA       = build/lib/libluxnav.a
LUXNAVSO      = build/lib/libluxnav.so.1
LUXNAVPC      = build/lib/pkgconfig/luxnav.pc
LUXNAV_CFLAGS = -std=gnu11 $(LUX_COPT) -fPIC -Wall -Wextra -MMD -MP -DHAVE_UXTHEME -Iluxnav -Iliblhde -idirafter . \
                $(shell pkg-config --cflags libpng)
LUXNAV_VERSION = 0.1

$(O)/luxnav/%.o: luxnav/%.c
	@mkdir -p $(dir $@)
	$(CC) $(LUXNAV_CFLAGS) -c $< -o $@
$(LUXNAVA): $(LUXNAV_OBJS)
	@mkdir -p $(dir $@)
	rm -f $@; ar rcs $@ $^
$(LUXNAVSO): $(LUXNAV_OBJS) $(UTSO)
	@mkdir -p $(dir $@)
	$(CC) -shared -o $@ $(LUXNAV_OBJS) -Wl,-soname,libluxnav.so.1 -Lbuild/lib -lwuxtheme '-Wl,-rpath,$$ORIGIN' $(shell pkg-config --libs libpng) -lm
	ln -sf libluxnav.so.1 build/lib/libluxnav.so

define luxnav_pc
prefix=$(1)
libdir=$(2)
includedir=$(3)

Name: luxnav
Description: lhde's Longhorn navigation bar (lhnav.h, lhnav_* API) over wuxtheme
Version: $(LUXNAV_VERSION)
Requires: wuxtheme
Requires.private: libpng
Libs: -L$${libdir} -lluxnav
Cflags: -I$${includedir}/luxnav
endef
export LUXNAVPC_BUILD   = $(call luxnav_pc,$(CURDIR),$${prefix}/build/lib,$${prefix})
export LUXNAVPC_INSTALL = $(call luxnav_pc,$(PREFIX),$(LIBDIR),$(INCLUDEDIR))
# the build tree's .pc: the header is luxnav/lhnav.h under the top, so Cflags points at <top>/luxnav via includedir/luxnav
$(LUXNAVPC): luxnav/luxnav.mk
	@mkdir -p $(dir $@)
	printf '%s\n' "$$LUXNAVPC_BUILD" > $@

luxnav: $(LUXNAVA) $(LUXNAVSO) $(LUXNAVPC)

install-luxnav: $(LUXNAVSO)
	install -d $(DESTDIR)$(LIBDIR) $(DESTDIR)$(INCLUDEDIR)/luxnav $(DESTDIR)$(PKGCONFIGDIR)
	install -m 755 $(LUXNAVSO) $(DESTDIR)$(LIBDIR)/libluxnav.so.1
	ln -sf libluxnav.so.1 $(DESTDIR)$(LIBDIR)/libluxnav.so
	install -m 644 luxnav/lhnav.h $(DESTDIR)$(INCLUDEDIR)/luxnav/lhnav.h
	printf '%s\n' "$$LUXNAVPC_INSTALL" > $(DESTDIR)$(PKGCONFIGDIR)/luxnav.pc
uninstall-luxnav:
	rm -f $(DESTDIR)$(LIBDIR)/libluxnav.so $(DESTDIR)$(LIBDIR)/libluxnav.so.1 $(DESTDIR)$(PKGCONFIGDIR)/luxnav.pc
	rm -rf $(DESTDIR)$(INCLUDEDIR)/luxnav

-include $(LUXNAV_OBJS:.o=.d)
.PHONY: luxnav install-luxnav uninstall-luxnav
