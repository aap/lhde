// The association model from the command line (for headless tests):
//   assoctest mime <file>...           type, description, default app, Open With list
//   assoctest exec <app.desktop> <file>...   the argvs a launch would run
//   assoctest open <file>...           open as a double click would
//   assoctest setdefault <mime> <app.desktop>
//   assoctest terminal <dir>
//   assoctest trash <file>...
//   assoctest bookmarks [add <path> [label]]
#include <stdio.h>
#include <string.h>
#include "shell/apps.h"
#include "shell/bookmarks.h"
#include "shell/config.h"
#include "shell/fileops.h"
#include "shell/mime.h"

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: assoctest mime|exec|open|setdefault|terminal|trash|bookmarks ...\n"); return 2; }
    std::string cmd = argv[1];
    std::string err;
    if (cmd == "mime") {
        for (int i = 2; i < argc; i++) {
            std::string t = shell::mime_type(argv[i]);
            const shell::App* d = shell::default_app(t);
            printf("%s: %s (%s) icon=%s default=%s\n", argv[i], t.c_str(), shell::mime_comment(t).c_str(), shell::mime_icon_key(t).c_str(), d ? d->id.c_str() : "-");
            printf("  parents:");
            for (auto& p : shell::mime_parents(t)) printf(" %s", p.c_str());
            printf("\n  open with:");
            for (auto* a : shell::apps_for(t)) printf(" %s[%s]", a->id.c_str(), a->name.c_str());
            printf("\n");
        }
    } else if (cmd == "exec" && argc >= 3) {
        const shell::App* a = shell::find_app(argv[2]);
        if (!a) { fprintf(stderr, "no app %s\n", argv[2]); return 1; }
        std::vector<std::string> files(argv + 3, argv + argc);
        for (auto& av : shell::exec_argvs(*a, files)) {
            for (auto& s : av) printf("[%s] ", s.c_str());
            printf("\n");
        }
    } else if (cmd == "open") {
        std::vector<std::string> files(argv + 2, argv + argc);
        if (!shell::open_files(files, &err)) { fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    } else if (cmd == "setdefault" && argc == 4) {
        if (!shell::set_default_app(argv[2], argv[3], &err)) { fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        const shell::App* d = shell::default_app(argv[2]);
        printf("default for %s now %s (%s)\n", argv[2], d ? d->id.c_str() : "-", shell::mimeapps_path().c_str());
    } else if (cmd == "terminal" && argc == 3) {
        printf("terminal: %s\n", shell::terminal_command().c_str());
        if (!shell::open_terminal(argv[2], &err)) { fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    } else if (cmd == "trash") {
        for (int i = 2; i < argc; i++) {
            printf("%s -> %s\n", argv[i], shell::trash_dir_for(argv[i]).c_str());
            if (!shell::trash(argv[i], &err)) { fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        }
    } else if (cmd == "bookmarks") {
        if (argc >= 4 && !strcmp(argv[2], "add") && !shell::add_bookmark(argv[3], argc > 4 ? argv[4] : "")) { fprintf(stderr, "cannot save\n"); return 1; }
        printf("%s\n", shell::bookmarks_path().c_str());
        for (auto& b : shell::load_bookmarks()) printf("  %-40s path=%s name=%s\n", b.uri.c_str(), b.path().c_str(), b.display_name().c_str());
    } else {
        fprintf(stderr, "bad command\n");
        return 2;
    }
    return 0;
}
