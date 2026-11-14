// Default programs by role. See roles.h; tools/lhdemime.py's roles section is the same model and writes the same text.
#include <errno.h>
#include <fnmatch.h>
#include <string.h>
#include <algorithm>
#include <set>
#include "roles.h"
#include "config.h"
#include "mime.h"
#include "settings.h"
#include "util.h"
#include <lhde/data.h>
#include <lhde/settings.h>

namespace shell {

namespace {

const char* const k_header =
    "# lhde's default programs by role, written by lhde-mime roles apply (lhde-session at start, Default Programs' Apply)\n"
    "# from settings.ini [programs] and roles.ini (docs/SETTINGS.md \"Default programs\"). Generated: rewritten whenever a\n"
    "# role changes, so not edited by hand. A choice for one type (Open With's Always use, File Types' Change...,\n"
    "# lhde-mime default) goes into ~/.config/mimeapps.list, which is read before this file and wins.\n";

std::vector<std::string> words(const std::string& v)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (i < v.size()) {
        while (i < v.size() && (v[i] == ' ' || v[i] == '\t')) i++;
        size_t j = i;
        while (j < v.size() && v[j] != ' ' && v[j] != '\t') j++;
        if (j > i) out.push_back(v.substr(i, j - i));
        i = j;
    }
    return out;
}

std::string desktop_id(std::string v)
{
    v = trim(v);
    if (v.empty() || v == "none" || (v.size() > 8 && v.compare(v.size() - 8, 8, ".desktop") == 0)) return v;
    return v + ".desktop";
}

// a value without its comment: ; or # after whitespace
std::string uncomment(const std::string& v)
{
    for (size_t i = 1; i < v.size(); i++)
        if ((v[i] == ';' || v[i] == '#') && (v[i - 1] == ' ' || v[i - 1] == '\t')) return trim(v.substr(0, i));
    return trim(v);
}

// lhde's ini with roles.ini's long lists (an indented line without = continues the value before it): section -> key ->
// value, sections in file order
typedef std::vector<std::pair<std::string, std::map<std::string, std::string>>> Sections;

bool read_roles_ini(const std::string& path, Sections& out)
{
    std::string text;
    if (!read_file(path, text)) return false;
    std::map<std::string, std::string>* cur = nullptr;
    std::string last;
    size_t i = 0;
    while (i <= text.size()) {
        size_t j = text.find('\n', i);
        if (j == std::string::npos) j = text.size();
        std::string raw = text.substr(i, j - i), line = trim(raw);
        i = j + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if ((raw[0] == ' ' || raw[0] == '\t') && line.find('=') == std::string::npos && cur && !last.empty()) {
            (*cur)[last] = trim((*cur)[last] + " " + uncomment(line));
            continue;
        }
        last.clear();
        if (line[0] == '[') {
            size_t e = line.find(']');
            std::string name = lower(trim(e == std::string::npos ? line.substr(1) : line.substr(1, e - 1)));
            cur = nullptr;
            for (auto& s : out) if (s.first == name) cur = &s.second;
            if (!cur) { out.push_back({ name, {} }); cur = &out.back().second; }
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos || !cur) continue;
        last = lower(trim(line.substr(0, eq)));
        (*cur)[last] = uncomment(line.substr(eq + 1));
    }
    return true;
}

bool match_any(const std::string& t, const std::vector<std::string>& pats)
{
    for (const std::string& p : pats) if (fnmatch(p.c_str(), t.c_str(), 0) == 0) return true;
    return false;
}

bool has_wild(const std::string& s) { return s.find_first_of("*?[") != std::string::npos; }

// every ancestor of t through mime_direct_parents
std::set<std::string> ancestors(const std::string& t)
{
    std::set<std::string> seen;
    std::vector<std::string> queue = { mime_canonical(t) };
    for (size_t i = 0; i < queue.size(); i++)
        for (const std::string& p : mime_direct_parents(queue[i]))
            if (p != queue[0] && seen.insert(p).second) queue.push_back(p);
    return seen;
}

struct Cache {
    bool loaded = false;
    std::vector<Role> roles;
    std::map<std::string, std::vector<std::string>> types;
};
Cache cache;

void load_cache()
{
    if (cache.loaded) return;
    cache.loaded = true;
    cache.roles = roles();
    cache.types = role_types(cache.roles);
}

} // namespace

std::vector<std::string> roles_ini_files()
{
    std::vector<std::string> out;
    std::string user = store::dir() + "/applications/roles.ini";
    if (is_file(user)) out.push_back(user);
    const char* d[LHDE_DATA_MAX];
    int n = lhde_data_dirs(d, LHDE_DATA_MAX);
    for (int i = 0; i < n; i++) {
        int k = lhde_data_dir_kind(i);
        if (k != LHDE_TREE_SHIPPED && k != LHDE_TREE_SOURCE) continue;
        std::string p = std::string(d[i]) + "/applications/roles.ini";
        if (is_file(p)) { out.push_back(p); break; }
    }
    return out;
}

std::vector<Role> roles()
{
    std::vector<std::string> files = roles_ini_files();
    std::vector<Role> out;
    for (auto f = files.rbegin(); f != files.rend(); ++f) {      // the shipped file first, the user's keys over it
        Sections secs;
        if (!read_roles_ini(*f, secs)) continue;
        for (auto& s : secs) {
            Role* r = nullptr;
            for (Role& x : out) if (x.id == s.first) r = &x;
            if (!r) { out.push_back(Role()); r = &out.back(); r->id = s.first; }
            for (auto& kv : s.second) {
                const std::string& k = kv.first;
                if (k == "label") r->label = kv.second;
                else if (k == "question") r->question = kv.second;
                else if (k == "current") r->current = kv.second;
                else if (k == "types") r->types = words(kv.second);
                else if (k == "descendants") r->descendants = words(kv.second);
                else if (k == "exclude") r->exclude = words(kv.second);
                else if (k == "prefer") r->prefer = words(kv.second);
                else if (k == "categories") r->categories = words(kv.second);
            }
        }
    }
    for (Role& r : out) {
        if (r.label.empty()) r.label = r.id;
        if (r.question.empty()) r.question = "Choose a default " + lower(r.label) + ":";
        if (r.current.empty()) r.current = "Use my current " + lower(r.label);
        for (std::string& p : r.prefer) p = desktop_id(p);
    }
    return out;
}

std::map<std::string, std::vector<std::string>> role_types(const std::vector<Role>& rs)
{
    std::vector<std::string> known = mime_all_types();
    std::map<std::string, std::string> strong, weak;     // type -> role
    for (const Role& r : rs)
        for (const std::string& p : r.types) {
            std::vector<std::string> hits;
            if (has_wild(p)) { for (const std::string& t : known) if (fnmatch(p.c_str(), t.c_str(), 0) == 0) hits.push_back(t); }
            else hits.push_back(mime_canonical(p));
            for (const std::string& t : hits) if (!match_any(t, r.exclude) && !strong.count(t)) strong[t] = r.id;
        }
    if (std::any_of(rs.begin(), rs.end(), [](const Role& r) { return !r.descendants.empty(); })) {
        std::map<std::string, std::set<std::string>> anc;
        for (const std::string& t : known) anc[t] = ancestors(t);
        for (const Role& r : rs)
            for (const std::string& b : r.descendants) {
                std::string base = mime_canonical(b);
                for (const std::string& t : known)
                    if (t != base && anc[t].count(base) && !match_any(t, r.exclude) && !weak.count(t)) weak[t] = r.id;
            }
    }
    std::map<std::string, std::vector<std::string>> out;
    for (const Role& r : rs) out[r.id];
    for (auto& kv : strong) out[kv.second].push_back(kv.first);
    for (auto& kv : weak) if (!strong.count(kv.first)) out[kv.second].push_back(kv.first);
    for (auto& kv : out) std::sort(kv.second.begin(), kv.second.end());
    return out;
}

const Role* role_of(const std::string& mime)
{
    load_cache();
    std::string t = mime_canonical(mime);
    for (const Role& r : cache.roles) {
        const std::vector<std::string>& v = cache.types[r.id];
        if (std::binary_search(v.begin(), v.end(), t)) return &r;
    }
    return nullptr;
}

std::string role_setting(const Role& r) { return desktop_id(store::get("programs", r.id)); }

std::string role_default(const Role& r)
{
    for (const std::string& p : r.prefer) if (find_app(p)) return p;
    return "";
}

RoleChoice role_choice(const Role& r)
{
    std::string v = role_setting(r), d = role_default(r);
    RoleChoice c;
    if (v == "none") { c.how = "none: your current programs (settings.ini [programs] " + r.id + " = none)"; c.user = true; return c; }
    if (!v.empty()) {
        if (find_app(v)) { c.app = v; c.how = "your choice (settings.ini [programs] " + r.id + ")"; c.user = true; return c; }
        if (!d.empty()) { c.app = d; c.how = v + " is not installed; lhde's default"; return c; }
        c.how = v + " is not installed and lhde has no default installed";
        return c;
    }
    if (!d.empty()) { c.app = d; c.how = "lhde's default"; return c; }
    std::string list;
    for (const std::string& p : r.prefer) list += (list.empty() ? "" : " ") + p;
    c.how = "lhde's default: none of " + (list.empty() ? std::string("no program") : list) + " installed";
    return c;
}

std::vector<const App*> role_candidates(const Role& r, const std::vector<std::string>& types)
{
    std::set<std::string> want(types.begin(), types.end());
    struct K { size_t pref; int n; std::string name, id; const App* a; };
    std::vector<K> ks;
    for (const App& a : all_apps()) {
        bool preferred = std::find(r.prefer.begin(), r.prefer.end(), a.id) != r.prefer.end();
        if (a.no_display && !preferred) continue;
        if (!r.categories.empty() && !preferred) {
            bool any = false;
            for (const std::string& c : a.categories) if (std::find(r.categories.begin(), r.categories.end(), c) != r.categories.end()) any = true;
            if (!any) continue;
        }
        std::set<std::string> claimed;
        for (const std::string& m : a.mime_types) { std::string t = mime_canonical(m); if (want.count(t)) claimed.insert(t); }
        if (claimed.empty()) continue;
        size_t pi = preferred ? (size_t)(std::find(r.prefer.begin(), r.prefer.end(), a.id) - r.prefer.begin()) : r.prefer.size();
        ks.push_back({ pi, -(int)claimed.size(), lower(a.name), a.id, &a });
    }
    std::sort(ks.begin(), ks.end(), [](const K& x, const K& y) {
        if (x.pref != y.pref) return x.pref < y.pref;
        if (x.n != y.n) return x.n < y.n;
        if (x.name != y.name) return x.name < y.name;
        return x.id < y.id;
    });
    std::vector<const App*> out;
    for (const K& k : ks) out.push_back(k.a);
    return out;
}

std::string roles_file() { return data_home() + "/applications/lhde-mimeapps.list"; }

std::string roles_text()
{
    std::vector<Role> rs = roles();
    auto sets = role_types(rs);
    std::string out = std::string(k_header) + "[Default Applications]\n";
    for (const Role& r : rs) {
        RoleChoice c = role_choice(r);
        const std::vector<std::string>& types = sets[r.id];
        if (c.app.empty() || types.empty()) { out += "# " + r.id + ": nothing (" + (c.app.empty() ? c.how : std::string("no types")) + ")\n"; continue; }
        out += "# " + r.id + ": " + c.app + ", " + c.how + ", " + std::to_string(types.size()) + " type" + (types.size() == 1 ? "" : "s") + "\n";
        for (const std::string& t : types) out += t + "=" + c.app + "\n";
    }
    return out;
}

bool apply_roles(std::string* err)
{
    std::string text = roles_text(), have, path = roles_file();
    cache = Cache();
    if (read_file(path, have) && have == text) return true;
    if (!write_file_atomic(path, text)) { if (err) *err = "cannot write " + path + ": " + strerror(errno); return false; }
    reload_apps();
    return true;
}

bool set_role(const std::string& role, const std::string& value, std::string* err)
{
    if (!store::set("programs", role, desktop_id(value))) { if (err) *err = "cannot write " + store::path(); return false; }
    return apply_roles(err);
}

} // namespace shell
