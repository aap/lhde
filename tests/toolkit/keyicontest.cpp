// shell::key_icon (shell/icons.h), the icon keys' images that set_window_icon_key gives a window (WM_SETICON 16 and 32), in a
// scratch home with nothing imported: Wine's set answers computer and exe at 16 and 32 px, an unknown key nothing.
// Usage: build/tests/keyicontest
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include "shell/icons.h"
extern "C" {
#include "lhde/data.h"
}

static int g_fail = 0, g_pass = 0;
#define CHECK_EQ(a, b) do { std::string x_ = (a), y_ = (b); if (x_ == y_) g_pass++; else { g_fail++; \
    fprintf(stderr, "FAIL %s:%d: %s\n  got      [%s]\n  expected [%s]\n", __FILE__, __LINE__, #a, x_.c_str(), y_.c_str()); } } while (0)

// "WxH" and whether any pixel is drawn, "empty" for no image
static std::string shape(const std::vector<unsigned long>& im)
{
    if (im.size() < 2) return "empty";
    size_t drawn = 0;
    for (size_t i = 2; i < im.size(); i++) drawn += (im[i] >> 24) != 0;
    return std::to_string(im[0]) + "x" + std::to_string(im[1]) + (im.size() == 2 + im[0] * im[1] ? "" : " short") + (drawn ? " drawn" : "");
}

int main()
{
    char tmpl[] = "/tmp/keyicontest.XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
    std::string T = tmpl;
    setenv("HOME", T.c_str(), 1);
    setenv("XDG_CONFIG_HOME", (T + "/config").c_str(), 1);
    setenv("XDG_DATA_HOME", (T + "/data").c_str(), 1);
    setenv("XDG_DATA_DIRS", (T + "/none").c_str(), 1);
    setenv("LHDE_WINDOWS", (T + "/nonexistent").c_str(), 1);
    unsetenv("LHDE_DATA"); unsetenv("LHDE_SETTINGS"); unsetenv("LHDE_TREE"); unsetenv("DISPLAY");
    lhde_data_reset();
    CHECK_EQ(shape(shell::key_icon("computer", 32)), "32x32 drawn");
    CHECK_EQ(shape(shell::key_icon("computer", 16)), "16x16 drawn");
    CHECK_EQ(shape(shell::key_icon("exe", 32)), "32x32 drawn");
    CHECK_EQ(shape(shell::key_icon("no-such-key-anywhere", 32)), "empty");
    if (system(("rm -rf '" + T + "'").c_str()) != 0) fprintf(stderr, "could not remove %s\n", T.c_str());
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
