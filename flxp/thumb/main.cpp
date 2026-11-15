// flxp-thumb: the shell layer's thumbnails (shell::thumbs, the freedesktop cache) for programs outside flxp, such as
// lhde-cat index --thumbs. No win32 and no display.
//
//   flxp-thumb [--size 128|256] FILE...    a line per file: "made<TAB>PNG", "cached<TAB>PNG" or "fail<TAB>FILE"
//   flxp-thumb [--size 128|256] -          the same for each line of stdin (a path a line), flushed after each:
//                                          a coprocess
// Exit status: 0 every file has a thumbnail, 1 some could not be made, 2 a usage error.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include "shell/thumbs.h"

static bool current(const std::string& png)
{
    struct stat st;
    return stat(png.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// one file: its thumbnail at size made or found; the line printed
static bool one(const std::string& file, int size)
{
    std::string normal = shell::thumbs::cache_path(file, size);
    std::string large = shell::thumbs::cache_path(file, 256);
    int before = shell::thumbs::decodes();
    const shell::thumbs::Thumbnailer* by = shell::thumbs::own_picture(file) ? nullptr : shell::thumbs::thumbnailer_for(file);
    gdi::Bitmap* b = shell::thumbs::get(file, 0, size, by);
    bool made = shell::thumbs::decodes() > before;
    if (b) gdi::destroy_bitmap(b);
    // a larger current thumbnail serves a smaller size without a file of its own
    std::string png = current(normal) ? normal : size < 256 && current(large) ? large : "";
    if (!b || png.empty()) { printf("fail\t%s\n", file.c_str()); return false; }
    printf("%s\t%s\n", made ? "made" : "cached", png.c_str());
    return true;
}

int main(int argc, char** argv)
{
    int size = 128, first = 1;
    if (argc > 2 && !strcmp(argv[1], "--size")) { size = atoi(argv[2]); first = 3; }
    if (size != 128 && size != 256) { fprintf(stderr, "flxp-thumb: --size 128 or 256\n"); return 2; }
    if (first >= argc) { fprintf(stderr, "usage: flxp-thumb [--size 128|256] FILE... | -\n"); return 2; }
    bool ok = true;
    if (!strcmp(argv[first], "-")) {
        char* line = nullptr;
        size_t cap = 0;
        ssize_t n;
        while ((n = getline(&line, &cap, stdin)) > 0) {
            if (line[n - 1] == '\n') line[--n] = 0;
            if (n) ok = one(line, size) && ok;
            fflush(stdout);
        }
        free(line);
    } else
        for (int i = first; i < argc; i++) ok = one(argv[i], size) && ok;
    return ok ? 0 : 1;
}
