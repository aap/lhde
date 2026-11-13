// luxattr: the attributes editor (docs/USAGE.md "Metadata"), an application on the public entry point
// (wux/wux.h, shell/shell.h, apps/app.mk; docs/APPLICATIONS.md) that needs no lhde session.
//
//   luxattr [--general] FILE...
//
// The shell layer's Properties sheet (shell::properties, shell/ui.h: the one the Explorer's Properties opens) for the files
// given, a directory being a file too, on its Metadata page (--general: on General). Everything else is the sheet's: the
// pages, several files at once, the writing (shell::details::write, as the Explorer writes) and the error boxes.
// Exit status: 0 something was written, 1 nothing, 2 bad usage or a file that is not there.
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <string>
#include <vector>
#include "wux/wux.h"
#include "shell/shell.h"

static void usage(FILE* f)
{
    fputs("usage: luxattr [--general] FILE...\n"
          "  The Properties sheet of the files, on its Metadata page: the description (user.lux.desc), tags (user.xdg.tags),\n"
          "  colour (user.lux.color), comment (user.xdg.comment) and run as (user.lux.run), Advanced >> every user.* extended\n"
          "  attribute. --general opens it on the General page.\n"
          "Exit status: 0 something was written, 1 nothing, 2 usage error or a file that is not there.\n", f);
}

int main(int argc, char** argv)
{
    std::vector<std::string> args, files;
    shell::PropertiesOptions o;
    o.window_icon = "settings";   // the sheet is luxattr's window
    o.page = 1;
    bool opts = true;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (opts && a == "--") { opts = false; continue; }
        if (opts && (a == "--help" || a == "-h")) { usage(stdout); return 0; }
        if (opts && a == "--general") { o.page = 0; continue; }
        if (opts && a.size() > 1 && a[0] == '-') { fprintf(stderr, "luxattr: unknown option %s\n", a.c_str()); usage(stderr); return 2; }
        args.push_back(a);
    }
    if (args.empty()) { usage(stderr); return 2; }
    std::string cwd;
    { char buf[PATH_MAX]; if (getcwd(buf, sizeof buf)) cwd = buf; }
    for (std::string a : args) {
        struct stat st;
        if (lstat(a.c_str(), &st) != 0) { fprintf(stderr, "luxattr: %s: %s\n", a.c_str(), strerror(errno)); return 2; }
        if (a[0] != '/') a = cwd + "/" + a;
        while (a.size() > 1 && a.back() == '/') a.pop_back();
        if (std::find(files.begin(), files.end(), a) == files.end()) files.push_back(a);
    }
    WuxInit();
    return shell::properties(nullptr, files, o) ? 0 : 1;
}
