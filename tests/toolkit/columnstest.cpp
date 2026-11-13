// The Details columns' layout (shell/columns.h): hiding and showing keep every
// column's place and width, a header drag reorders the shown ones around the hidden
// ones, an older file (the shown columns only) reads as before; and the per-folder
// records of flxp-views (shell::folderviews) on a scratch XDG_STATE_HOME: the view and
// the columns of one folder kept apart from another's, a line of the older format read.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include "shell/columns.h"
#include "shell/config.h"

using namespace shell;
using namespace shell::columns;

static int fails, checks;
static void check(const char* what, const std::string& want, const std::string& got)
{
    checks++;
    if (want == got) return;
    fails++;
    printf("FAIL %s: expected [%s] got [%s]\n", what, want.c_str(), got.c_str());
}

static std::string ids(const std::vector<int>& v)
{
    std::string s;
    for (int id : v) s += (s.empty() ? "" : " ") + std::string(name_of(id));
    return s;
}

int main()
{
    enum { NAME, SIZE, TYPE, DATE, DESC, TAGS, COMMENT };
    Layout l = builtin();
    check("builtin", "name:180 size:96 type:120 modified:120 -description:180 -tags:120 -comment:150 -color:100", format(l));

    // aap's case: show Tags, hide Size, show Size again: Size at its own place, not last
    set_shown(l, TAGS, true);
    set_shown(l, SIZE, false);
    check("size hidden", "name type modified tags", ids(shown(l)));
    set_width(l, SIZE, 77);   // (a width given while hidden is kept too)
    set_shown(l, SIZE, true);
    check("size back at its place", "name size type modified tags", ids(shown(l)));
    check("size keeps its width", "name:180 size:77 type:120 modified:120 -description:180 tags:120 -comment:150 -color:100", format(l));
    set_shown(l, NAME, false);
    check("name stays shown", "name size type modified tags", ids(shown(l)));

    // a header drag: Date Modified before Size; the hidden Description keeps its place
    // between Date Modified and Tags' old neighbours
    reorder_shown(l, { NAME, DATE, SIZE, TYPE, TAGS });
    check("dragged", "name:180 modified:120 size:77 type:120 -description:180 tags:120 -comment:150 -color:100", format(l));
    reorder_shown(l, { NAME, TAGS, DATE, SIZE, TYPE });
    check("tags dragged before a hidden one", "name:180 tags:120 modified:120 size:77 -description:180 type:120 -comment:150 -color:100", format(l));
    // a hidden column shown again comes back at its slot
    set_shown(l, DESC, true);
    check("description back between size and type", "name tags modified size description type", ids(shown(l)));
    // Name dragged elsewhere stays there
    reorder_shown(l, { TAGS, NAME, DATE, SIZE, DESC, TYPE });
    check("name dragged", "tags name modified size description type", ids(shown(l)));

    // parse: the format read back, an older file (the shown ones only), junk
    Layout r = parse(format(l), builtin());
    check("read back", format(l), format(r));
    check("same", "1", r == l ? "1" : "0");
    Layout old = parse("name:200 type:120 modified:100 tags:90\n", builtin());
    check("older file", "name:200 type:120 modified:100 tags:90 -size:96 -description:180 -comment:150 -color:100", format(old));
    check("empty file", format(builtin()), format(parse("", builtin())));
    check("junk dropped, name shown, the unnamed hidden", "name:180 -size:96 type:120 -modified:120 -description:180 -tags:120 -comment:150 -color:100",
          format(parse("-name -size bogus:3 name:9999 type", builtin())));

    // per folder: flxp-views on a scratch state directory
    char tmpl[] = "/tmp/columnstestXXXXXX";
    const char* dir = mkdtemp(tmpl);
    if (!dir) { printf("FAIL mkdtemp\n"); return 1; }
    setenv("XDG_STATE_HOME", dir, 1);
    std::string f = state_file("flxp-views");
    // an older line (path, view) is read
    write_file_atomic(f, "/a\ticons@48\n");
    check("older line", "icons@48|", folderviews::get("/a").view + "|" + folderviews::get("/a").columns);
    Layout a = builtin(); set_shown(a, TAGS, true); set_shown(a, SIZE, false);
    Layout b = builtin(); reorder_shown(b, { NAME, TYPE, SIZE, DATE }); set_width(b, DATE, 140);
    folderviews::set_columns("/a", format(a));
    folderviews::set_columns("/b", format(b));
    folderviews::set_view("/b", "details");
    check("a's columns", format(a), folderviews::get("/a").columns);
    check("a's view kept", "icons@48", folderviews::get("/a").view);
    check("b's columns", format(b), folderviews::get("/b").columns);
    // b changes again: a's do not
    set_shown(b, COMMENT, true);
    folderviews::set_columns("/b", format(b));
    check("a untouched by b", format(a), folderviews::get("/a").columns);
    std::string text;
    read_file(f, text);
    check("the file", "/a\ticons@48\tcolumns=" + format(a) + "\n/b\tdetails\tcolumns=" + format(b) + "\n", text);
    // forgetting
    folderviews::set_columns("/a", "");
    check("a's columns forgotten, view kept", "icons@48|", folderviews::get("/a").view + "|" + folderviews::get("/a").columns);
    folderviews::forget_views();
    check("views forgotten, columns kept", "|" + format(b), folderviews::get("/b").view + "|" + folderviews::get("/b").columns);
    check("a gone", "|", folderviews::get("/a").view + "|" + folderviews::get("/a").columns);
    folderviews::forget_columns();
    read_file(f, text);
    check("all forgotten", "", text);
    // another writer: the file re-read when it changed
    write_file_atomic(f, "/c\t\tcolumns=name:100\n");
    check("re-read", "name:100", folderviews::get("/c").columns);

    unlink(f.c_str());
    std::string lhde = std::string(dir) + "/lhde";
    rmdir(lhde.c_str());
    rmdir(dir);
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
