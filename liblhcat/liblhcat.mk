# liblhcat, the catalogue (lhcat.h, docs/CATALOGUE.md): SQLite databases of a root's files, their attributes, picture
# facts and thumbnails, with the journal of attribute operations. Plain C on liblhde (its state directory) and sqlite3.
# Included by the top-level Makefile after liblhde.mk, before flxp.mk (flxp links it into the shell layer).
#   build/lib/liblhcat.a                 the library
#   build/bin/lhde-cat                   the command line (add-root, import-du, index, find, set, tag, commit, ...)
#   build/liblhcat/tests/cat             make check-liblhcat: a scratch root and generated listings (tests/catalogue.sh
#                                        drives it with the generated pictures and the performance checks)
SQLITE_CFLAGS := $(shell pkg-config --cflags sqlite3)
SQLITE_LIBS   := $(shell pkg-config --libs sqlite3)
LHCAT_SRCS    = $(addprefix liblhcat/,cat.c attrs.c fill.c find.c exif.c)
LHCAT_OBJS    = $(patsubst %.c,$(O)/%.o,$(LHCAT_SRCS))
LHCATA        = build/lib/liblhcat.a
LHCAT_CFLAGS  = -std=gnu11 -O2 -g -fPIC -Wall -Wextra -Wno-format-truncation -MMD -MP -Iliblhcat -Iliblhde -idirafter . $(SQLITE_CFLAGS)
$(O)/liblhcat/%.o: liblhcat/%.c
	@mkdir -p $(dir $@)
	$(CC) $(LHCAT_CFLAGS) -c $< -o $@
$(LHCATA): $(LHCAT_OBJS)
	@mkdir -p $(dir $@)
	rm -f $@; ar rcs $@ $^
build/bin/lhde-cat: $(O)/liblhcat/lhde-cat.o $(LHCATA) $(LHDEA) $(UTSO)
	@mkdir -p $(dir $@)
	$(CC) -o $@ $(O)/liblhcat/lhde-cat.o $(LHCATA) $(LHDEA) -Lbuild/lib -lwuxtheme $(LHDE_PNG) $(SQLITE_LIBS) '-Wl,-rpath,$$ORIGIN/../lib'
build/liblhcat/tests/cat: $(O)/liblhcat/tests/cat.o $(LHCATA) $(LHDEA) $(UTSO)
	@mkdir -p $(dir $@)
	$(CC) -o $@ $< $(LHCATA) $(LHDEA) -Lbuild/lib -lwuxtheme $(LHDE_PNG) $(SQLITE_LIBS) '-Wl,-rpath,$$ORIGIN/../../lib'
liblhcat: $(LHCATA) build/bin/lhde-cat build/liblhcat/tests/cat
check-liblhcat: build/liblhcat/tests/cat build/bin/lhde-cat build/bin/flxp-thumb
	@tests/catalogue.sh > build/liblhcat/tests/cat.log 2>&1 && echo "ok    liblhcat catalogue ($$(tail -n 1 build/liblhcat/tests/cat.log))" \
	 || { cat build/liblhcat/tests/cat.log; echo "FAIL  liblhcat catalogue"; exit 1; }
install-liblhcat: build/bin/lhde-cat
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 build/bin/lhde-cat $(DESTDIR)$(BINDIR)/
uninstall-liblhcat:
	rm -f $(DESTDIR)$(BINDIR)/lhde-cat
-include $(LHCAT_OBJS:.o=.d) $(O)/liblhcat/lhde-cat.d $(O)/liblhcat/tests/cat.d
.PHONY: liblhcat check-liblhcat install-liblhcat uninstall-liblhcat
