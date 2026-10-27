# wuxtheme, the uxtheme library (included by the top-level Makefile; before
# wux/wux.mk, which leaves these objects out of libwux.a).
# Vendored uxtheme's reader/renderer with the window-system-free part of the
# shim, wux/gdi and wux/res. libwuxtheme.a is linked by every flxp program
# (the same objects as flxp's libuxtheme.a); libwuxtheme.so.1 is the same
# sources compiled -fPIC plus nowin.cpp, exporting only ut_* (ut.h, installed
# as <wuxtheme/ut.h>); ut-dump is its command-line tool.

UTVEND   = $(addprefix $(WINE_DIR)/uxtheme/,msstyles.c draw.c property.c stylemap.c uxini.c) \
           $(addprefix $(WINE_DIR)/user32/,text.c uitools.c)
UTSHIM   = $(addprefix wux/w32/,gdi.cpp gdi_draw.cpp gdi_text.cpp image.cpp crt.cpp debug.cpp syscolor.cpp kernel.cpp usp10.cpp)
UTOWN    = wuxtheme/ut.cpp wuxtheme/v4.cpp
UTGDI    = $(wildcard wux/gdi/*.cpp wux/res/*.cpp)
UTOBJS   = $(patsubst %.c,$(O)/%.o,$(UTVEND)) $(patsubst %.cpp,$(O)/%.o,$(UTSHIM) $(UTOWN) $(UTGDI))
UTPIC    = $(patsubst $(O)/%,$(P)/%,$(UTOBJS)) $(P)/wuxtheme/nowin.o
UTLIBS   = $(shell pkg-config --libs freetype2 fontconfig libpng libjpeg $(WEBP)) -pthread
UTA      = build/lib/libwuxtheme.a
UTSO     = build/lib/libwuxtheme.so.1
UTPC     = build/lib/pkgconfig/wuxtheme.pc
UTDUMP   = build/bin/ut-dump
UTVERSION = 0.1

$(O)/wuxtheme/%.o: wuxtheme/%.cpp
	@mkdir -p $(dir $@)
	$(W32CXX) -c $< -o $@
$(UTA): $(UTOBJS)
	@mkdir -p $(dir $@)
	rm -f $@; ar rcs $@ $^

$(P)/$(WINE_DIR)/%.o: $(WINE_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@
$(P)/wux/w32/%.o: wux/w32/%.cpp
	@mkdir -p $(dir $@)
	$(W32CXX) -fPIC -c $< -o $@
$(P)/wuxtheme/%.o: wuxtheme/%.cpp
	@mkdir -p $(dir $@)
	$(W32CXX) -fPIC -c $< -o $@
$(P)/wux/%.o: wux/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -fPIC -c $< -o $@
$(P)/wux/w32/image.o: wux/w32/stdres.res
$(UTSO): $(UTPIC) wuxtheme/exports.map
	@mkdir -p $(dir $@)
	$(CXX) -shared -o $@ $(UTPIC) -Wl,--version-script=wuxtheme/exports.map -Wl,-soname,libwuxtheme.so.1 -Wl,--no-undefined $(UTLIBS)
	ln -sf libwuxtheme.so.1 build/lib/libwuxtheme.so

$(UTDUMP): $(O)/wuxtheme/ut-dump.o $(UTSO)
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $< -Lbuild/lib -lwuxtheme '-Wl,-rpath,$$ORIGIN/../lib' $(shell pkg-config --libs libpng)

# pkg-config: build/lib/pkgconfig/wuxtheme.pc describes the build tree (for
# components built against an uninstalled lhde: PKG_CONFIG_PATH=<lhde>/build/lib/pkgconfig),
# make install writes the installed one.
define wuxtheme_pc
prefix=$(1)
libdir=$(2)
includedir=$(3)

Name: wuxtheme
Description: lhde's uxtheme library: Windows visual styles (.msstyles) read and drawn without Windows (ut.h, ut_* API)
Version: $(UTVERSION)
Requires.private: freetype2 fontconfig libpng libjpeg $(WEBP)
Libs: -L$${libdir} -lwuxtheme
Cflags: -I$${includedir}
endef
export UTPC_BUILD   = $(call wuxtheme_pc,$(CURDIR),$${prefix}/build/lib,$${prefix})
export UTPC_INSTALL = $(call wuxtheme_pc,$(PREFIX),$(LIBDIR),$(INCLUDEDIR))
$(UTPC): wuxtheme/wuxtheme.mk
	@mkdir -p $(dir $@)
	printf '%s\n' "$$UTPC_BUILD" > $@

install-libs: $(UTSO) $(UTDUMP)
	install -d $(DESTDIR)$(LIBDIR) $(DESTDIR)$(INCLUDEDIR)/wuxtheme $(DESTDIR)$(PKGCONFIGDIR) $(DESTDIR)$(BINDIR)
	install -m 755 $(UTSO) $(DESTDIR)$(LIBDIR)/libwuxtheme.so.1
	ln -sf libwuxtheme.so.1 $(DESTDIR)$(LIBDIR)/libwuxtheme.so
	install -m 644 wuxtheme/ut.h $(DESTDIR)$(INCLUDEDIR)/wuxtheme/ut.h
	printf '%s\n' "$$UTPC_INSTALL" > $(DESTDIR)$(PKGCONFIGDIR)/wuxtheme.pc
	install -m 755 $(UTDUMP) $(DESTDIR)$(BINDIR)/ut-dump
uninstall-libs:
	rm -f $(DESTDIR)$(LIBDIR)/libwuxtheme.so $(DESTDIR)$(LIBDIR)/libwuxtheme.so.1 $(DESTDIR)$(BINDIR)/ut-dump
	rm -f $(DESTDIR)$(PKGCONFIGDIR)/wuxtheme.pc
	rm -rf $(DESTDIR)$(INCLUDEDIR)/wuxtheme

-include $(UTPIC:.o=.d) $(O)/wuxtheme/ut-dump.d
.PHONY: wuxtheme install-libs uninstall-libs
wuxtheme: $(UTA) $(UTSO) $(UTPC) $(UTDUMP)
