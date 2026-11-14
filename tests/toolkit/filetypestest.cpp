// Folder Options' File Types model (shell/mime.h, shell/apps.h) on a scratch XDG tree: the registered extensions from a
// globs2 of our own (one row per extension, the best glob's type, wildcards left out), where a type's default comes from
// (the user's file, lhde-mimeapps.list in an lhde session, the distribution's, the first claimant), Change and Restore on the
// user's mimeapps.list, a program's actions (Exec, then Actions=) and the copy Edit File Type writes in
// $XDG_DATA_HOME/applications (a user's own file there refused, Restore removing ours), and the user's own extensions (New,
// Delete: lhde-extensions.xml compiled by update-mime-database; skipped without it).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include "shell/apps.h"
#include "shell/config.h"
#include "shell/mime.h"

using namespace shell;

static int fails, checks;
static void check(const char* what, const std::string& want, const std::string& got)
{
    checks++;
    if (want == got) return;
    fails++;
    printf("FAIL %s: expected [%s] got [%s]\n", what, want.c_str(), got.c_str());
}

static void put(const std::string& path, const std::string& text)
{
    make_dirs(dir_of(path));
    FILE* f = fopen(path.c_str(), "w");
    if (f) { fputs(text.c_str(), f); fclose(f); }
}

static std::string read(const std::string& path)
{
    std::string t;
    read_file(path, t);
    return t;
}

static std::string source(const std::string& type)
{
    AppSource s = default_source(type);
    return (s.app ? s.app->id : "-") + " " + s.layer;
}

int main()
{
    char tmpl[] = "/tmp/filetypestest.XXXXXX";
    std::string T = mkdtemp(tmpl);
    setenv("HOME", (T + "/home").c_str(), 1);
    setenv("XDG_CONFIG_HOME", (T + "/home/.config").c_str(), 1);
    setenv("XDG_DATA_HOME", (T + "/home/.local/share").c_str(), 1);
    setenv("XDG_CONFIG_DIRS", (T + "/etc").c_str(), 1);
    setenv("XDG_DATA_DIRS", (T + "/lhde:" + T + "/usr").c_str(), 1);
    unsetenv("XDG_CURRENT_DESKTOP");
    put(T + "/usr/mime/globs2", "50:text/plain:*.txt\n50:text/x-csrc:*.c\n80:text/x-chdr:*.h\n50:application/x-ext-h:*.h\n"
                                "50:application/x-compressed-tar:*.tar.gz\n50:text/x-readme:README*\n50:image/png:*.png\n");
    put(T + "/usr/mime/subclasses", "text/x-csrc text/plain\n");
    auto desktop = [&](const std::string& path, const std::string& name, const std::string& types, const std::string& extra = "") {
        put(path, "[Desktop Entry]\nType=Application\nName=" + name + "\nExec=/bin/true %f\nMimeType=" + types + "\n" + extra);
    };
    desktop(T + "/usr/applications/gedit.desktop", "gedit", "text/plain;text/x-csrc;",
            "Actions=new-window;\n\n[Desktop Action new-window]\nName=New Window\nExec=/bin/true --new-window\n");
    desktop(T + "/lhde/applications/luxpad.desktop", "luxpad", "text/plain;");
    desktop(T + "/usr/applications/viewer.desktop", "Viewer", "image/png;");
    put(T + "/usr/applications/mimeapps.list", "[Default Applications]\ntext/plain=gedit.desktop\n");
    put(T + "/lhde/applications/lhde-mimeapps.list", "[Default Applications]\ntext/plain=luxpad.desktop\n");

    // the rows: one per simple extension, sorted; README* (no extension) and the losing *.h glob left out
    std::string rows;
    for (const Extension& e : mime_extensions()) rows += e.ext + "=" + e.type + " ";
    check("the extensions", "c=text/x-csrc h=text/x-chdr png=image/png tar.gz=application/x-compressed-tar txt=text/plain ", rows);

    // where the default comes from
    check("text/plain without a desktop: the distribution", "gedit.desktop system", source("text/plain"));
    check("image/png: the first claimant", "viewer.desktop claimant", source("image/png"));
    check("text/x-csrc: its claimant before text/plain's default", "gedit.desktop claimant", source("text/x-csrc"));
    setenv("XDG_CURRENT_DESKTOP", "lhde", 1);
    reload_apps();
    check("text/plain in an lhde session: lhde", "luxpad.desktop lhde", source("text/plain"));
    // Change (the Open With dialog's choice) and Restore
    std::string err;
    check("Change", "1", set_default_app("text/plain", "gedit.desktop", &err) ? "1" : err);
    check("Change: the user's choice", "gedit.desktop user", source("text/plain"));
    check("Change: user_default_set", "1", user_default_set("text/plain") ? "1" : "0");
    check("Change: the user's file", "[Default Applications]\ntext/plain=gedit.desktop\n\n[Added Associations]\ntext/plain=gedit.desktop;\n",
          read(mimeapps_path()));
    check("Restore", "1", reset_default_app("text/plain", &err) ? "1" : err);
    check("Restore: lhde's again", "luxpad.desktop lhde", source("text/plain"));
    check("Restore: user_default_set", "0", user_default_set("text/plain") ? "1" : "0");

    // a program's actions, and the copy Edit File Type writes
    const App* g = find_app("gedit.desktop");
    std::string acts;
    if (g) for (const AppAction& a : app_actions(*g)) acts += a.id + "|" + a.name + "|" + a.exec + " ";
    check("gedit's actions", "|&Open|/bin/true %f new-window|New Window|/bin/true --new-window ", acts);
    std::vector<AppAction> mine = { { "", "&Open", "/bin/true --edit %f" }, { "print", "Print", "/bin/true --print %f" } };
    check("New / Set Default: the copy", "1", g && write_app_override(*g, mine, &err) ? "1" : err);
    g = find_app("gedit.desktop");
    check("the copy hides the distribution's", T + "/home/.local/share/applications/gedit.desktop", g ? g->file : "-");
    check("the copy is ours", "1", g && app_overridden(*g) ? "1" : "0");
    acts.clear();
    if (g) for (const AppAction& a : app_actions(*g)) acts += a.id + "|" + a.name + "|" + a.exec + " ";
    check("the copy's actions", "|&Open|/bin/true --edit %f print|Print|/bin/true --print %f ", acts);
    std::string copy = read(T + "/home/.local/share/applications/gedit.desktop");
    check("the copy names its original", "1", copy.find("X-Lhde-Original=" + T + "/usr/applications/gedit.desktop\n") != std::string::npos ? "1" : copy);
    check("the copy keeps the MimeType", "1", copy.find("MimeType=text/plain;text/x-csrc;\n") != std::string::npos ? "1" : copy);
    check("Restore: the copy removed", "1", g && remove_app_override(*g, &err) ? "1" : err);
    g = find_app("gedit.desktop");
    check("Restore: the distribution's file again", T + "/usr/applications/gedit.desktop", g ? g->file : "-");
    // a desktop file of the user's own is not touched
    desktop(T + "/home/.local/share/applications/viewer.desktop", "My Viewer", "image/png;");
    reload_apps();
    const App* v = find_app("viewer.desktop");
    check("the user's own file: refused", "0", v && write_app_override(*v, mine, &err) ? "1" : "0");
    check("the user's own file: Restore refused", "0", v && remove_app_override(*v, &err) ? "1" : "0");

    // New / Delete: the user's own extension
    if (system("command -v update-mime-database >/dev/null 2>&1") == 0) {
        check("New .foo", "1", add_user_extension(".FOO", "text/plain", &err) ? "1" : err);
        check("New .foo: the type of a.foo", "text/plain", mime_type_for_name("a.foo"));
        rows.clear();
        for (const Extension& e : user_extensions()) rows += e.ext + "=" + e.type + " ";
        check("New .foo: the user's extensions", "foo=text/plain ", rows);
        check("Delete .txt (not the user's): refused", "0", remove_user_extension("txt", &err) ? "1" : "0");
        check("Delete .foo", "1", remove_user_extension("foo", &err) ? "1" : err);
        check("Delete .foo: unknown again", "", mime_type_for_name("a.foo"));
        check("a bad extension", "0", add_user_extension("a b", "text/plain", &err) ? "1" : "0");
    } else {
        printf("skip: the user's extensions (no update-mime-database)\n");
    }
    std::string rm = "rm -rf '" + T + "'";
    if (system(rm.c_str()) != 0) {}
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
