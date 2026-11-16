# Tests (included by the top-level Makefile).
#   toolkit/   native toolkit and shell-model tests: build/tests/<name> (no win32 shim)
#   w32/       shim-level tests and demos: build/tests/w32/<name>; linked with
#              unresolved symbols ignored so a partial shim can be exercised
#   headless/  scripts driving the programs under Xvfb with xdotool
#   check.sh   make check: what runs without Microsoft files
#   demo.sh    run a demo on the current display

TOOLKIT_TESTS = $(patsubst tests/toolkit/%.cpp,build/tests/%,$(wildcard tests/toolkit/*.cpp))
W32_TESTS     = $(patsubst tests/w32/%.cpp,build/tests/w32/%,$(wildcard tests/w32/*.cpp))
NATIVE_OBJS   = $(patsubst %.cpp,$(O)/%.o,$(WUX_NATIVE) wux/w32/utf.cpp) $(SHELLOBJS)

$(O)/tests/toolkit/lhdatatest.o $(O)/tests/toolkit/keyicontest.o $(O)/tests/toolkit/starticontest.o: CXXFLAGS += -Iliblhde
$(O)/tests/toolkit/cattest.o: CXXFLAGS += -Iliblhcat $(SQLITE_CFLAGS)
$(O)/tests/toolkit/%.o: tests/toolkit/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
build/tests/%: $(O)/tests/toolkit/%.o $(NATIVE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $^ $(LDLIBS)
build/tests/luxviewtest: $(O)/apps/luxview/model_native.o   # luxview's view model, no win32
build/tests/starticontest: LDLIBS += -Lbuild/lib -Wl,--no-as-needed -lwuxtheme '-Wl,-rpath,$$ORIGIN/../lib'   # the style flags
build/tests/starticontest: | $(UTSO)

$(O)/tests/w32/%.o: tests/w32/%.cpp
	@mkdir -p $(dir $@)
	$(W32CXX) -c $< -o $@
build/tests/w32/%: $(O)/tests/w32/%.o $(W32DEPS)
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $< $(W32LINK) $(LDLIBS) -no-pie -Wl,--unresolved-symbols=ignore-all
build/tests/w32/filedlgtest: $(O)/tests/w32/filedlgtest.o $(FILEDLGLIB) $(EXPLSHARED) $(W32DEPS)
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $< $(FILEDLGLIB) $(EXPLSHARED) $(W32LINK) $(LDLIBS) -no-pie
build/tests/w32/fileopuitest: $(O)/tests/w32/fileopuitest.o $(FILEDLGLIB) $(EXPLSHARED) $(W32DEPS)
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $< $(FILEDLGLIB) $(EXPLSHARED) $(W32LINK) $(LDLIBS) -no-pie
build/tests/w32/folderpictest: $(O)/tests/w32/folderpictest.o $(FILEDLGLIB) $(EXPLSHARED) $(W32DEPS)
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $< $(FILEDLGLIB) $(EXPLSHARED) $(W32LINK) $(LDLIBS) -no-pie

# kerneltest's resources beside it: test.res and comdlg32.res. tests/w32/test.res is checked in like every .res in the tree
# (windres only when test.rc or pal.bmp changes)
tests/w32/test.res: tests/w32/test.rc tests/w32/pal.bmp
	$(call run_windres,-I tests/w32)
build/tests/w32/test.res: tests/w32/test.res
	@mkdir -p $(dir $@)
	cp $< $@
build/tests/w32/comdlg32.res: wux/rc/comdlg32.res
	@mkdir -p $(dir $@)
	cp $< $@
TEST_DATA = build/tests/w32/test.res build/tests/w32/comdlg32.res

# slowfs.so: the LD_PRELOAD shim that makes a tree as slow as a network mount (tests/headless/slowfs.c, slowpath.sh)
SLOWFS = build/tests/slowfs.so
$(SLOWFS): tests/headless/slowfs.c
	@mkdir -p $(dir $@)
	$(CC) -g -O1 -Wall -Wno-nonnull-compare -fPIC -shared -o $@ $< -pthread $(if $(filter Linux,$(shell uname -s)),-ldl,-lexecinfo)

tests: $(TOOLKIT_TESTS) $(W32_TESTS) $(TEST_DATA) $(SLOWFS)
w32test: $(W32_TESTS) $(TEST_DATA)
check: all check-poison
	tests/check.sh
# win32 C++ cannot call glibc's wide functions (wchar_t is 16 bits there; flxp_crt.h poisons the names): a probe calling
# wcslen must fail to compile where the same probe calling flxp_wcslen compiles
POISON_PROBE = printf '\043include <string>\nunsigned long f(const wchar_t* p) { return %s((const unsigned short*)p); }\n'
check-poison:
	@$(POISON_PROBE) flxp_wcslen | $(CXX) -std=c++17 $(W32FLAGS) -x c++ -fsyntax-only - || { echo "FAIL  check-poison: the probe does not compile with flxp_wcslen"; exit 1; }
	@if $(POISON_PROBE) wcslen | $(CXX) -std=c++17 $(W32FLAGS) -x c++ -fsyntax-only - 2>/dev/null; then \
	    echo "FAIL  check-poison: glibc's wcslen compiles in win32 C++"; exit 1; \
	else echo "ok    check-poison: glibc's wide functions do not compile in win32 C++"; fi

-include $(patsubst build/tests/%,$(O)/tests/toolkit/%.d,$(TOOLKIT_TESTS)) $(patsubst build/tests/w32/%,$(O)/tests/w32/%.d,$(W32_TESTS))
.PHONY: tests w32test check check-poison
.PRECIOUS: $(O)/tests/toolkit/%.o $(O)/tests/w32/%.o
