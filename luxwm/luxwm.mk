# luxwm, the window manager (a berry fork; docs/LUXWM.md), and luxwmc, its command client.
# Included by the top-level Makefile. Frames come from the visual style through libwuxtheme (build/lib/libwuxtheme.so.1,
# rpath $ORIGIN/../lib) and from the frame sets in lhde's data directories, the theme from liblhde's resolver (liblhde/liblhde.mk).
#   build/bin/luxwm build/bin/luxwmc
#   build/luxwm/tests/frametest menutest desktopwin dockorder strutdock   (tests/regress.sh: make check-luxwm)

LUXWM_PKGS   = x11 xext xfixes xinerama fontconfig xft cairo cairo-xlib pangocairo
# berry's warnings; -Os as berry's configure chose
LUX_COPT    ?= -Os -g -DNDEBUG
LUXWM_CFLAGS = -std=c99 $(LUX_COPT) -Wall -Wextra -Wredundant-decls -Wshadow -Wno-deprecated-declarations -MMD -MP \
               -DHAVE_UXTHEME -Iluxwm -Iliblhde -idirafter . $(shell pkg-config --cflags $(LUXWM_PKGS))
LUXWM_LIBS   = -Lbuild/lib -lwuxtheme '-Wl,-rpath,$$ORIGIN/../lib' $(shell pkg-config --libs $(LUXWM_PKGS)) -lm
LUXWM_TLIBS  = $(subst ../lib,../../lib,$(LUXWM_LIBS))   # build/luxwm/tests: the library two levels up
LUXWM_CORE   = $(addprefix $(O)/luxwm/,frame.o frame_ut.o) $(LHDEA)
LUXWM_OBJS   = $(addprefix $(O)/luxwm/,wm.o utils.o menu.o) $(LUXWM_CORE)
LUXWM_TESTS  = $(addprefix build/luxwm/tests/,frametest menutest desktopwin dockorder strutdock)

$(O)/luxwm/%.o: luxwm/%.c
	@mkdir -p $(dir $@)
	$(CC) $(LUXWM_CFLAGS) -c $< -o $@
# the tests are plain X clients and painter drivers: gnu99 (usleep & co. without config.h's feature macros)
$(O)/luxwm/tests/%.o: luxwm/tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(LUXWM_CFLAGS) -std=gnu99 -c $< -o $@

build/bin/luxwm: $(LUXWM_OBJS) $(UTSO)
	@mkdir -p $(dir $@)
	$(CC) -o $@ $(LUXWM_OBJS) $(LUXWM_LIBS)
build/bin/luxwmc: $(O)/luxwm/client.o
	@mkdir -p $(dir $@)
	$(CC) -o $@ $< $(shell pkg-config --libs x11)

# tests: the frame and menu painters offline, and small X clients for the lab
build/luxwm/tests/frametest: $(O)/luxwm/tests/frametest.o $(LUXWM_CORE) $(UTSO)
	@mkdir -p $(dir $@)
	$(CC) -o $@ $< $(LUXWM_CORE) $(LUXWM_TLIBS)
build/luxwm/tests/menutest: $(O)/luxwm/tests/menutest.o $(O)/luxwm/menu.o $(LUXWM_CORE) $(UTSO)
	@mkdir -p $(dir $@)
	$(CC) -o $@ $< $(O)/luxwm/menu.o $(LUXWM_CORE) $(LUXWM_TLIBS)
build/luxwm/tests/%: $(O)/luxwm/tests/%.o
	@mkdir -p $(dir $@)
	$(CC) -o $@ $< $(shell pkg-config --libs x11)

LUXWM_PROGRAMS = build/bin/luxwm build/bin/luxwmc
luxwm: $(LUXWM_PROGRAMS) $(LUXWM_TESTS)

# make check-luxwm: the frame regression against a baseline of this machine (luxwm/tests/regress.sh)
check-luxwm: build/luxwm/tests/frametest
	luxwm/tests/regress.sh

install-luxwm: $(LUXWM_PROGRAMS)
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(MANDIR)/man1
	install -m 755 $(LUXWM_PROGRAMS) $(DESTDIR)$(BINDIR)/
	ln -sf luxwm $(DESTDIR)$(BINDIR)/lhwm
	ln -sf luxwmc $(DESTDIR)$(BINDIR)/lhwmc
	install -m 644 luxwm/luxwm.1 luxwm/luxwmc.1 $(DESTDIR)$(MANDIR)/man1/
uninstall-luxwm:
	rm -f $(addprefix $(DESTDIR)$(BINDIR)/,luxwm luxwmc lhwm lhwmc) $(addprefix $(DESTDIR)$(MANDIR)/man1/,luxwm.1 luxwmc.1)

-include $(patsubst %.o,%.d,$(filter %.o,$(LUXWM_OBJS))) $(O)/luxwm/client.d $(patsubst build/luxwm/tests/%,$(O)/luxwm/tests/%.d,$(LUXWM_TESTS))
.PHONY: luxwm check-luxwm install-luxwm uninstall-luxwm
.PRECIOUS: $(O)/luxwm/tests/%.o
