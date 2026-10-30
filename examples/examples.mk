# The reference clients (included by the top-level Makefile), into build/examples:
#   extendframe  luxwm's extended glass frame client (docs/CLIENT-PROTOCOL.md): an Explorer-style 60 px glass header
#   navdemo      luxnav's reference client (docs/NAVBAR.md): a small file browser with the vision header
#   glassdemo    an ARGB client that is its own glass frame (the frame set aero-patch's plate from the data directories)
# Not installed: run them from build/examples.

EXAMPLES_PKGS = x11 xext cairo cairo-xlib pangocairo
EXAMPLES      = $(addprefix build/examples/,extendframe navdemo glassdemo)

build/examples/extendframe: examples/extendframe.c $(LHDEA) $(UTSO)
	@mkdir -p $(dir $@)
	$(CC) -std=c99 -O2 -g -Wall -Wextra -Iliblhde $(shell pkg-config --cflags $(EXAMPLES_PKGS)) -o $@ $< $(LHDEA) \
	    -Lbuild/lib -lwuxtheme '-Wl,-rpath,$$ORIGIN/../lib' $(shell pkg-config --libs $(EXAMPLES_PKGS))
build/examples/navdemo: examples/navdemo.c luxnav/lhnav.h $(LUXNAVA) $(LHDEA) $(UTSO)
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -g -Wall -Wextra -DHAVE_UXTHEME -Iluxnav -Iliblhde -idirafter . $(shell pkg-config --cflags $(EXAMPLES_PKGS) fontconfig) \
	    -o $@ $< $(LUXNAVA) $(LHDEA) -Lbuild/lib -lwuxtheme '-Wl,-rpath,$$ORIGIN/../lib' $(shell pkg-config --libs $(EXAMPLES_PKGS) fontconfig libpng) -lm
build/examples/glassdemo: examples/glassdemo.c $(O)/liblhde/data.o
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -g -Wall -Iliblhde $(shell pkg-config --cflags cairo-xlib x11) -o $@ $< $(O)/liblhde/data.o $(shell pkg-config --libs cairo-xlib x11) -lm

examples: $(EXAMPLES)
.PHONY: examples
