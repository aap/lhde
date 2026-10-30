// flxp's view model (shell/viewmodel.h): the three views and their sizes,
// 4074's five slider stops, names old and new, the zoom steps, what the views
// remember and the View button's cycle.
#include <stdio.h>
#include <string>
#include "shell/viewmodel.h"

using namespace shell::viewmodel;

static int fails, checks;
static void check(const char* what, const std::string& want, const std::string& got)
{
    checks++;
    if (want == got) return;
    fails++;
    printf("FAIL %s: expected [%s] got [%s]\n", what, want.c_str(), got.c_str());
}
static void check(const char* what, int want, int got) { check(what, std::to_string(want), std::to_string(got)); }

static std::string P(const std::string& text)
{
    View v;
    bool sized = false;
    if (!parse(text, v, &sized)) return "-";
    return format(v) + (sized ? "" : " (unsized)");
}

static View V(Mode m, int size, bool hide = false) { View v; v.mode = m; v.size = size; v.hide_names = hide; return v; }

int main()
{
    // names, and the earlier ones
    check("parse details", "details@20", P("details@20"));
    check("parse tiles", "tiles@48", P("tiles@48"));
    check("parse icons nonames", "icons@96+nonames", P("icons@96+nonames"));
    check("parse thumbnails", "icons@147", P("thumbnails@147"));
    check("parse old toggle", "details@16 (unsized)", P("details+thumbnails"));
    check("parse old toggle sized", "details@16 (unsized)", P("list+thumbnails@96"));
    check("parse smallicons", "icons@16", P("smallicons"));
    check("parse list", "details@16 (unsized)", P("list"));
    check("parse unsized icons", "icons@48 (unsized)", P("icons"));
    check("parse clamped", "details@32", P("details@64"));
    check("parse junk", "-", P("bogus@5"));
    check("format", "tiles@96", format(V(Mode::Tiles, 96)));

    // the stops: the first whose range holds the size
    check("stop details", STOP_DETAILS, stop_of(V(Mode::Details, 32)));
    check("stop tiles", STOP_TILES, stop_of(V(Mode::Tiles, 32)));
    check("stop large", STOP_LARGE, stop_of(V(Mode::Icons, 128)));
    check("stop medium", STOP_MEDIUM, stop_of(V(Mode::Icons, 48)));
    check("stop small", STOP_SMALL, stop_of(V(Mode::Icons, 47)));
    check("stop large 256", STOP_LARGE, stop_of(V(Mode::Icons, 256)));

    // the slider's mapping (linear; growing down in Details/Tiles, shrinking in the icon stops)
    check("details top", 16, size_at(STOP_DETAILS, 0, 100));
    check("details bottom", 32, size_at(STOP_DETAILS, 100, 100));
    check("tiles middle", 64, size_at(STOP_TILES, 50, 100));
    check("large top", 256, size_at(STOP_LARGE, 0, 100));
    check("large bottom", 128, size_at(STOP_LARGE, 100, 100));
    check("small bottom", 16, size_at(STOP_SMALL, 100, 100));
    check("pos of medium 88", 50, pos_of(STOP_MEDIUM, 88, 100));
    int back = size_at(STOP_LARGE, pos_of(STOP_LARGE, 200, 60), 60);
    check("pos round trip", 1, back >= 197 && back <= 203);

    // zoom steps inside a view's range
    check("step icons up", 64, step(V(Mode::Icons, 48), 1).size);
    check("step icons down", 36, step(V(Mode::Icons, 48), -1).size);
    check("step icons top", 256, step(V(Mode::Icons, 256), 1).size);
    check("step details up", 21, step(V(Mode::Details, 16), 1).size);
    check("step details end", 32, step(V(Mode::Details, 27), 1).size);
    check("step tiles down", 32, step(V(Mode::Tiles, 36), -1).size);
    check("step tiles from odd", 96, step(V(Mode::Tiles, 90), 1).size);

    // memory: each stop keeps its size, the icon stop used last is Icons
    Memory m;
    check("recall icons", "icons@48", format(m.recall(Mode::Icons)));
    check("cycle details", "tiles@48", format(m.next(V(Mode::Details, 16))));
    check("cycle tiles", "icons@48", format(m.next(V(Mode::Tiles, 48))));
    check("cycle icons", "details@16", format(m.next(V(Mode::Icons, 48))));
    m.shown(V(Mode::Icons, 200));
    check("icons after large", "icons@200", format(m.recall(Mode::Icons)));
    check("cycle to large", "icons@200", format(m.next(V(Mode::Tiles, 48))));
    m.shown(V(Mode::Tiles, 80));
    m.shown(V(Mode::Details, 24));
    check("tiles remembered", "tiles@80", format(m.recall(Mode::Tiles)));
    check("medium kept", "icons@48", format(m.at(STOP_MEDIUM)));
    m.shown(V(Mode::Icons, 30));
    check("small becomes icons", "icons@30", format(m.recall(Mode::Icons)));
    check("large kept", "icons@200", format(m.at(STOP_LARGE)));
    check("hide names follows the cycle", "icons@30+nonames", format(m.next(V(Mode::Tiles, 80, true))));
    std::string saved = m.save();
    check("save", "details=24 tiles=80 large=200 medium=48 small=30 icons=small", saved);
    Memory m2;
    m2.load(saved);
    check("load", saved, m2.save());
    Memory m3;
    m3.load("details=99 tiles=40 icons=details bogus");
    check("load checks ranges", "details=16 tiles=40 large=128 medium=48 small=16 icons=medium", m3.save());

    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
