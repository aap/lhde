# wux, the win32 UI layer (included by the top-level Makefile).
#   gdi/ sys/ win/ res/   the native toolkit: surfaces and text, the X11 backend,
#                         the window tree, the PE/.res reader
#   w32/                  the win32 shim the vendored Wine code and flxp run on
#   vendor/wine/          Wine's comctl32, user32 and uxtheme sources (LGPL; MANIFEST, *.patch)
#   rc/                   Wine's comctl32/comdlg32 resources compiled with rc/build.sh
# build/lib/libwux.a holds all of it except what libwuxtheme.a already has
# (wuxtheme/wuxtheme.mk: gdi, res and the window-system-free part of the shim);
# programs link it whole (--whole-archive), as flxp linked the objects.

WUX_NATIVE = $(wildcard wux/gdi/*.cpp wux/sys/*.cpp wux/win/*.cpp wux/res/*.cpp)
WUX_SHIM   = $(wildcard wux/w32/*.cpp)
WUX_VEND   = $(wildcard $(WINE_DIR)/*/*.c)
WUX_OBJS   = $(patsubst %.cpp,$(O)/%.o,$(WUX_NATIVE) $(WUX_SHIM)) $(patsubst %.c,$(O)/%.o,$(WUX_VEND))
WUXA       = build/lib/libwux.a

$(O)/wux/%.o: wux/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(O)/wux/w32/%.o: wux/w32/%.cpp
	@mkdir -p $(dir $@)
	$(W32CXX) -c $< -o $@
$(O)/$(WINE_DIR)/comctl32/%.o: $(WINE_DIR)/comctl32/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Dset_control_clipping=comctl32_set_control_clipping -DDllMain=COMCTL32_DllMain '-DFLATSB_CLASSW=L"flatsb_class32"' -c $< -o $@
$(O)/$(WINE_DIR)/%.o: $(WINE_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# resources embedded with .incbin (the path is the assembler's, relative to the top)
$(O)/wux/w32/comctl.o: wux/rc/comctl32.res
$(O)/wux/w32/image.o: wux/w32/stdres.res

$(WUXA): $(filter-out $(UTOBJS),$(WUX_OBJS))
	@mkdir -p $(dir $@)
	rm -f $@; ar rcs $@ $^

# what a win32 program links: libwux whole, luxnav when built, libwuxtheme as an archive
WUX_LINK = -Wl,--whole-archive $(WUXA) -Wl,--no-whole-archive $(LUXNAVA) $(UTA)

-include $(WUX_OBJS:.o=.d)
.PHONY: wux
wux: $(WUXA)
