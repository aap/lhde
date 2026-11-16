// The Explorer's crumbs on the shell layer's Longhorn header (header.h): the folder's path as segments, each
// segment's drop-down its sibling folders, drops on a segment into that folder.
#include <string.h>
#include <stdlib.h>
#include <algorithm>
#include <vector>
#include "header.h"
#include "shell/config.h"
#include "commands.h"
#include "verbs.h"

namespace header {

namespace {

Callbacks g_cb;
std::vector<std::string> g_paths;    // the crumbs' folders, the leaf last

std::string expand_home(const std::string& p) { return !p.empty() && p[0] == '~' ? shell::home_dir() + p.substr(1) : p; }

// segment i's drop-down: its parent's folders, itself bold (4074); children: its own, the next segment bold (Vista)
std::vector<Entry> entries(int i, bool children)
{
    std::vector<Entry> out;
    if (i < 0 || (size_t)i >= g_paths.size()) return out;
    std::string here = g_paths[(size_t)i];
    if (children) here = (size_t)i + 1 < g_paths.size() ? g_paths[(size_t)i + 1] : "";
    auto f = shell::folder_for(g_paths[(size_t)i]);
    auto parent = children ? f : f ? f->parent() : nullptr;
    std::vector<shell::Item> kids;
    if (parent) parent->enumerate_names(kids, false);   // (folders by d_type on a network mount: no stat per entry)
    shell::sort_items(kids, shell::SortBy::Name, true);
    for (const shell::Item& it : kids) {
        if (!it.is_folder()) continue;
        Entry e;
        e.text = it.name;
        e.key = it.path;
        e.bold = it.path == here;
        out.push_back(e);
        if (out.size() >= 250) break;
    }
    return out;
}

LRESULT drop(int i, const FLXPDROP* d, bool commit)
{
    if (i < 0 || (size_t)i >= g_paths.size()) return 0;
    const std::string path = g_paths[(size_t)i];
    return commit ? verbs::drop(d, path) : verbs::drop_effect(d, path);
}

void go(const std::string& path) { if (g_cb.navigate) g_cb.navigate(path); }

} // namespace

HWND create(HWND frame, const Callbacks& cb)
{
    g_cb = cb;
    g_paths.clear();
    shell::lhheader::Callbacks c;
    c.go = go;
    c.entries = entries;
    c.address = [](const std::string& text) { go(expand_home(text)); };
    c.bookmark = [](const std::string& path) { go(expand_home(path)); };
    c.filter = cb.filter;
    c.done = cb.done;
    c.command = [](int id) {
        if (g_cb.command) g_cb.command(id == TRAVEL_BACK ? ID_BACK : id == TRAVEL_FORWARD ? ID_FORWARD : id);
    };
    c.history = cb.history;
    c.favorites = cb.favorites;
    c.tool = cb.tool;
    c.drop = drop;
    c.views_id = ID_VIEWS;
    c.new_folder_id = ID_NEW_FOLDER;
    return shell::lhheader::create(frame, c);
}

void set_folder(const std::shared_ptr<shell::Folder>& f)
{
    if (!window() || !f) return;
    // the path from the namespace root, without the Desktop itself (4074 starts at Computer)
    std::vector<Crumb> chain;
    std::vector<std::string> paths;
    auto add = [&](const std::string& name, const std::string& path) {
        Crumb c;
        c.text = name.empty() ? path : name;
        c.key = path;
        chain.insert(chain.begin(), c);
        paths.insert(paths.begin(), path);
    };
    // flxp.ini breadcrumb_home = 0 (Folder Options' "Display the full path"): a folder under the home folder as the real path,
    // Computer > Local Disk (/) > home > user > ..., instead of the namespace's Home > ...
    const char* home = getenv("HOME");
    std::string fp = f->path();
    bool full = home && *home && fp[0] == '/' && !shell::setting_flag("breadcrumb_home", true) &&
                (fp == home || (fp.size() > strlen(home) && !fp.compare(0, strlen(home), home) && fp[strlen(home)] == '/'));
    if (full) {
        for (std::string p = fp; p.size() > 1; p = p.substr(0, std::max<size_t>(p.rfind('/'), 1)))
            add(p.substr(p.rfind('/') + 1), p);
        if (auto r = shell::folder_for("/")) add(r->name(), "/");
        if (auto c = shell::folder_for("::computer")) add(c->name(), "::computer");
    } else {
        for (auto p = f; p; p = p->parent()) {
            if (p->path() == "::desktop" && !chain.empty()) break;
            add(p->name(), p->path());
        }
    }
    // the vision chevron's tooltip (its list is the segment's siblings; the root's has none)
    for (size_t i = 1; i < chain.size(); i++) chain[i].tip = "Folders next to " + chain[i].text;
    g_paths = paths;
    set_crumbs(chain, f->name());
}

}
