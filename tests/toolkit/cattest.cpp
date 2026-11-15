// The shell layer and the catalogue (liblhcat, docs/CATALOGUE.md): details::write's writes become operations in the
// journal of the catalogue holding the file (program, op, value), and details::read_stored(items, folder) reads a
// catalogued folder's attributes from the catalogue while each file is as the catalogue observed it, from the file once
// it changed (an attribute written elsewhere changes its ctime). On a scratch $XDG_STATE_HOME and root.
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <sqlite3.h>
#include "liblhde/compat.h"
#include "lhcat.h"
#include "shell/details.h"

using namespace shell;

static int fails, checks;
static void check(const char* what, const std::string& want, const std::string& got)
{
    checks++;
    if (want == got) return;
    fails++;
    printf("FAIL %s: expected [%s] got [%s]\n", what, want.c_str(), got.c_str());
}

static std::string one(sqlite3* db, const char* sql)
{
    sqlite3_stmt* st;
    std::string v = "(none)";
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_text(st, 0)) v = (const char*)sqlite3_column_text(st, 0);
        sqlite3_finalize(st);
    }
    return v;
}

static std::vector<Item> items_of(const std::string& dir, const std::vector<std::string>& names)
{
    std::vector<Item> v;
    for (const std::string& n : names) { Item it; it.name = n; it.path = dir + "/" + n; v.push_back(it); }
    return v;
}

int main()
{
    char tmpl[] = "/tmp/cattest-XXXXXX";
    const char* base = getenv("TMPDIR");
    std::string t = std::string(base && *base ? base : "/tmp") + "/cattest-XXXXXX";
    std::vector<char> buf(t.begin(), t.end());
    buf.push_back(0);
    char* dir = mkdtemp(buf.data());
    if (!dir) dir = mkdtemp(tmpl);
    if (!dir) { printf("FAIL mkdtemp\n"); return 1; }
    std::string T = dir;
    setenv("XDG_STATE_HOME", (T + "/state").c_str(), 1);
    std::string root = T + "/root", f = root + "/f";
    mkdir(root.c_str(), 0755);
    mkdir(f.c_str(), 0755);
    for (const char* n : { "a.txt", "b.txt" }) { FILE* o = fopen((f + "/" + n).c_str(), "w"); if (o) { fputs("x\n", o); fclose(o); } }
    if (lhde_xattr_set((f + "/b.txt").c_str(), "user.lux.desc", "bee", 3) != 0 && errno == ENOTSUP) {
        printf("skip: no user attributes on %s\n", T.c_str());
        return 0;
    }
    char err[1024];
    check("add-root", "0", std::to_string(lhcat_add_root("hook", root.c_str(), nullptr, 0, err, sizeof err)));
    lhcat* c = lhcat_open("hook", err, sizeof err);
    if (!c) { printf("FAIL open: %s\n", err); return 1; }
    struct lhcat_progress p;
    check("index", "0", std::to_string(lhcat_index(c, "", nullptr, nullptr, nullptr, &p)));

    // details::write -> an operation in the journal, the desired and observed values follow
    std::string e;
    check("write", "1", std::to_string(details::write(f + "/a.txt", *details::property("tags"), "x; y, x", &e)));
    sqlite3* db = lhcat_db(c);
    check("journal program", "cattest", one(db, "SELECT program FROM journal ORDER BY seq DESC LIMIT 1"));
    check("journal op", "set x,y applied", one(db, "SELECT op || ' ' || value || ' ' || status FROM journal ORDER BY seq DESC LIMIT 1"));
    check("desired", "x,y x,y none", one(db, "SELECT desired || ' ' || observed || ' ' || pending FROM attrs WHERE name = 'user.xdg.tags'"));
    check("add tag", "1", std::to_string(details::add_tags(f + "/a.txt", { "z" }, &e)));
    check("journal tags", "x,y,z", one(db, "SELECT value FROM journal ORDER BY seq DESC LIMIT 1"));

    // read_stored from the catalogue: a value only the catalogue has shows while the file is unchanged
    sqlite3_exec(db, "UPDATE attrs SET desired = 'from-catalogue' WHERE name = 'user.lux.desc'", nullptr, nullptr, nullptr);
    std::vector<Item> items = items_of(f, { "a.txt", "b.txt" });
    details::read_stored(items, f);
    check("a's tags", "x,y,z", items[0].stored("user.xdg.tags"));
    check("b from the catalogue", "from-catalogue", items[1].stored("user.lux.desc"));
    // changed elsewhere: the file's value
    usleep(20000);
    lhde_xattr_set((f + "/b.txt").c_str(), "user.lux.desc", "elsewhere", 9);
    items = items_of(f, { "a.txt", "b.txt" });
    details::read_stored(items, f);
    check("b from the file", "elsewhere", items[1].stored("user.lux.desc"));
    // a folder no catalogue holds reads the files
    std::string other = T + "/other";
    mkdir(other.c_str(), 0755);
    FILE* o = fopen((other + "/c.txt").c_str(), "w");
    if (o) fclose(o);
    lhde_xattr_set((other + "/c.txt").c_str(), "user.xdg.tags", "q", 1);
    items = items_of(other, { "c.txt" });
    details::read_stored(items, other);
    check("outside a root", "q", items[0].stored("user.xdg.tags"));
    lhcat_close(c);
    std::string rm = "rm -rf '" + T + "'";
    if (system(rm.c_str()) != 0) printf("could not remove %s\n", T.c_str());
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
