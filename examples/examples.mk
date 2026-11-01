# The reference clients (included by the top-level Makefile), into build/examples:
#   extendframe  luxwm's extended glass frame client (docs/CLIENT-PROTOCOL.md): an Explorer-style 60 px glass header
#   navdemo      luxnav's reference client (docs/NAVBAR.md): a small file browser with the vision header
#   aurorademo   Longhorn's preview-pane and desktop aurora, played from build/examples/aurora_data.h
# Not installed: run them from build/examples.
#
# aurora_data.h is made at every make from the Windows tree's generated/aurora/{previewpane,desktop}-4074.json (lhde-import's
# [aurora] recipes of share/sources/4074.ini; found through liblhde's lookup, lhde-theme find) by tools/lhdeaurora.py, which
# rewrites it only when it changes. A scene that is not imported is left out of the header with a message, and aurorademo
# says so when asked for it.

EXAMPLES_PKGS = x11 xext cairo cairo-xlib pangocairo
EXAMPLES      = $(addprefix build/examples/,extendframe navdemo aurorademo)

build/examples/extendframe: examples/extendframe.c $(LHDEA) $(UTSO)
	@mkdir -p $(dir $@)
	$(CC) -std=c99 -O2 -g -Wall -Wextra -Iliblhde $(shell pkg-config --cflags $(EXAMPLES_PKGS)) -o $@ $< $(LHDEA) \
	    -Lbuild/lib -lwuxtheme '-Wl,-rpath,$$ORIGIN/../lib' $(shell pkg-config --libs $(EXAMPLES_PKGS))
build/examples/navdemo: examples/navdemo.c luxnav/lhnav.h $(LUXNAVA) $(LHDEA) $(UTSO)
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -g -Wall -Wextra -DHAVE_UXTHEME -Iluxnav -Iliblhde -idirafter . $(shell pkg-config --cflags $(EXAMPLES_PKGS) fontconfig) \
	    -o $@ $< $(LUXNAVA) $(LHDEA) -Lbuild/lib -lwuxtheme '-Wl,-rpath,$$ORIGIN/../lib' $(shell pkg-config --libs $(EXAMPLES_PKGS) fontconfig libpng) -lm
build/examples/aurora_data.h: tools/lhdeaurora.py build/bin/lhde-theme aurora-data-always
	@mkdir -p $(dir $@)
	@python3 -I tools/lhdeaurora.py header $@ "$$(build/bin/lhde-theme find aurora/previewpane-4074.json)" \
	    "$$(build/bin/lhde-theme find aurora/desktop-4074.json)"
build/examples/aurorademo: examples/aurorademo.c build/examples/aurora_data.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -g -Wall -Wextra -pthread -Ibuild/examples $(shell pkg-config --cflags cairo-xlib x11) -o $@ $< $(shell pkg-config --libs cairo-xlib x11) -lm

aurora-data-always:
examples: $(EXAMPLES)
.PHONY: examples aurora-data-always
