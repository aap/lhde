# Tests (included by the top-level Makefile).
#   toolkit/   native toolkit and shell-model tests: build/tests/<name> (no win32 shim)
#   w32/       shim-level tests and demos: build/tests/w32/<name>; linked with
#              unresolved symbols ignored so a partial shim can be exercised
#   headless/  scripts driving the programs under Xvfb with xdotool
#   check.sh   make check: what runs without Microsoft files
#   demo.sh    run a demo on the current display

TOOLKIT_TESTS = $(patsubst tests/toolkit/%.cpp,build/tests/%,$(wildcard tests/toolkit/*.cpp))
W32_TESTS     = $(patsubst tests/w32/%.cpp,build/tests/w32/%,$(wildcard tests/w32/*.cpp))
NATIVE_OBJS   = $(patsubst %.cpp,$(O)/%.o,$(WUX_NATIVE)) $(SHELLOBJS)

$(O)/tests/toolkit/%.o: tests/toolkit/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
build/tests/%: $(O)/tests/toolkit/%.o $(NATIVE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $^ $(LDLIBS)

$(O)/tests/w32/%.o: tests/w32/%.cpp
	@mkdir -p $(dir $@)
	$(W32CXX) -c $< -o $@
build/tests/w32/%: $(O)/tests/w32/%.o $(W32DEPS)
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $< $(W32LINK) $(LDLIBS) -no-pie -Wl,--unresolved-symbols=ignore-all
build/tests/w32/filedlgtest: $(O)/tests/w32/filedlgtest.o $(FILEDLGLIB) $(EXPLSHARED) $(W32DEPS)
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $< $(FILEDLGLIB) $(EXPLSHARED) $(W32LINK) $(LDLIBS) -no-pie

# kerneltest's resources: test.res from test.rc, comdlg32.res beside it
build/tests/w32/test.res: tests/w32/test.rc tests/w32/pal.bmp
	@mkdir -p $(dir $@)
	$(WINDRES) -O res -I tests/w32 -o $@ $<
build/tests/w32/comdlg32.res: wux/rc/comdlg32.res
	@mkdir -p $(dir $@)
	cp $< $@
TEST_DATA = build/tests/w32/test.res build/tests/w32/comdlg32.res

tests: $(TOOLKIT_TESTS) $(W32_TESTS) $(TEST_DATA)
w32test: $(W32_TESTS) $(TEST_DATA)
check: all
	tests/check.sh

-include $(patsubst build/tests/%,$(O)/tests/toolkit/%.d,$(TOOLKIT_TESTS)) $(patsubst build/tests/w32/%,$(O)/tests/w32/%.d,$(W32_TESTS))
.PHONY: tests w32test check
.PRECIOUS: $(O)/tests/toolkit/%.o $(O)/tests/w32/%.o
