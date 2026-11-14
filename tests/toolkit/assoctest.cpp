// The association model from the command line (for headless tests):
//   assoctest mime <file>...           type, description, default app, Open With list
//   assoctest exec <app.desktop> <file>...   the argvs a launch would run
//   assoctest open <file>...           open as a double click would
//   assoctest default <type or file>...    the default, its layer and the file that set it
//   assoctest files                    the mimeapps.list files read, most important first
//   assoctest setdefault <mime> <app.desktop>
//   assoctest reset <mime>             the type out of the user's mimeapps.list
//   assoctest roles                    each role: ID APP TYPES-COUNT then its candidates (shell/roles.h)
//   assoctest roletext                 the role file's text as apply would write it
//   assoctest rolesapply               write the role file
//   assoctest setrole <role> <app|none|->   settings.ini [programs] ROLE, then apply
//   assoctest roleof <type>...         the role of each type, "-" for none
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
#include "shell/roles.h"

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
    } else if (cmd == "default") {
        // the default and where it comes from, one line per type or file: "TYPE APP LAYER FILE"
        for (int i = 2; i < argc; i++) {
            std::string t = strchr(argv[i], '/') && argv[i][0] != '/' && argv[i][0] != '.' ? std::string(argv[i]) : shell::mime_type(argv[i]);
            shell::AppSource s = shell::default_source(t);
            printf("%s %s %s %s\n", shell::mime_canonical(t).c_str(), s.app ? s.app->id.c_str() : "-", s.layer.empty() ? "-" : s.layer.c_str(),
                   s.file.empty() ? "-" : s.file.c_str());
        }
    } else if (cmd == "roles") {
        std::vector<shell::Role> rs = shell::roles();
        auto sets = shell::role_types(rs);
        for (const shell::Role& r : rs) {
            shell::RoleChoice c = shell::role_choice(r);
            printf("%s %s %zu\n ", r.id.c_str(), c.app.empty() ? "-" : c.app.c_str(), sets[r.id].size());
            for (const shell::App* a : shell::role_candidates(r, sets[r.id])) printf(" %s", a->id.c_str());
            printf("\n");
        }
    } else if (cmd == "roletext") {
        fputs(shell::roles_text().c_str(), stdout);
    } else if (cmd == "rolesapply") {
        if (!shell::apply_roles(&err)) { fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        printf("%s\n", shell::roles_file().c_str());
    } else if (cmd == "setrole" && argc == 4) {
        if (!shell::set_role(argv[2], strcmp(argv[3], "-") ? argv[3] : "", &err)) { fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        printf("%s\n", shell::roles_file().c_str());
    } else if (cmd == "roleof") {
        for (int i = 2; i < argc; i++) { const shell::Role* r = shell::role_of(argv[i]); printf("%s %s\n", argv[i], r ? r->id.c_str() : "-"); }
    } else if (cmd == "files") {
        for (const std::string& f : shell::mimeapps_files()) printf("%s\n", f.c_str());
    } else if (cmd == "reset" && argc == 3) {
        if (!shell::reset_default_app(argv[2], &err)) { fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        const shell::App* d = shell::default_app(argv[2]);
        printf("default for %s now %s\n", argv[2], d ? d->id.c_str() : "-");
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
