// Desktop entries, mimeapps.list and launching. See apps.h. Specs:
// Desktop Entry 1.5 (Exec field codes, quoting), MIME Applications
// Associations 1.0.1 (file precedence, Default/Added/Removed groups).
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <map>
#include <set>
#include "apps.h"
#include "config.h"
#include "util.h"
#include "mime.h"

namespace shell {

namespace {

// ---- key files ----------------------------------------------------------------

struct KeyFile {
    // group -> ordered (key, raw value)
    std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> groups;
    const std::vector<std::pair<std::string, std::string>>* group(const std::string& name) const
    {
        for (auto& g : groups) if (g.first == name) return &g.second;
        return nullptr;
    }
};

bool parse_keyfile(const std::string& path, KeyFile& kf)
{
    std::string text;
    if (!read_file(path, text)) return false;
    size_t i = 0;
    while (i < text.size()) {
        size_t j = text.find('\n', i);
        if (j == std::string::npos) j = text.size();
        std::string line = trim(text.substr(i, j - i));
        i = j + 1;
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') {
            size_t e = line.find(']');
            kf.groups.push_back({ line.substr(1, e == std::string::npos ? std::string::npos : e - 1), {} });
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos || kf.groups.empty()) continue;
        kf.groups.back().second.push_back({ trim(line.substr(0, eq)), trim(line.substr(eq + 1)) });
    }
    return true;
}

// the "string" escapes: \s \n \t \r \\ (others kept, for Exec's own quoting)
std::string unescape(const std::string& v, bool keep_unknown)
{
    std::string out;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i] != '\\' || i + 1 >= v.size()) { out += v[i]; continue; }
        char c = v[++i];
        switch (c) {
        case 's': out += ' '; break;
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case '\\': out += keep_unknown ? "\\\\" : "\\"; break;
        default: if (keep_unknown) out += '\\'; out += c; break;
        }
    }
    return out;
}

std::vector<std::string> split_list(const std::string& v)
{
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i] == '\\' && i + 1 < v.size() && v[i + 1] == ';') { cur += ';'; i++; continue; }
        if (v[i] == ';') { cur = trim(cur); if (!cur.empty()) out.push_back(cur); cur.clear(); continue; }
        cur += v[i];
    }
    cur = trim(cur);
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool truthy(const std::string& v) { return v == "true" || v == "1"; }

bool in_path(const std::string& prog)
{
    if (prog.empty()) return false;
    if (prog.find('/') != std::string::npos) return access(prog.c_str(), X_OK) == 0;
    const char* p = getenv("PATH");
    std::string path = p ? p : "/usr/local/bin:/usr/bin:/bin";
    size_t i = 0;
    while (i <= path.size()) {
        size_t j = path.find(':', i);
        if (j == std::string::npos) j = path.size();
        std::string d = path.substr(i, j - i);
        if (d.empty()) d = ".";
        if (access((d + "/" + prog).c_str(), X_OK) == 0) return true;
        i = j + 1;
    }
    return false;
}

// ---- the application database ----------------------------------------------------

struct Mimeapps {
    std::string path;
    std::map<std::string, std::vector<std::string>> defaults, added, removed;
};

struct Db {
    bool loaded = false;
    std::vector<App> apps;                       // sorted by name
    std::map<std::string, size_t> by_id;
    std::map<std::string, std::vector<std::string>> by_mime;   // canonical type -> ids (data dir precedence, then id)
    std::vector<Mimeapps> lists;                 // precedence order
};

Db db;

void scan_dir(const std::string& root, const std::string& sub, std::set<std::string>& seen, std::vector<App>& out)
{
    DIR* d = opendir((root + sub).c_str());
    if (!d) return;
    std::vector<std::string> names;
    while (struct dirent* e = readdir(d)) if (e->d_name[0] != '.') names.push_back(e->d_name);
    closedir(d);
    std::sort(names.begin(), names.end());
    for (const std::string& n : names) {
        std::string rel = sub + "/" + n, full = root + rel;
        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { scan_dir(root, rel, seen, out); continue; }
        if (n.size() < 9 || n.compare(n.size() - 8, 8, ".desktop") != 0) continue;
        std::string id = rel.substr(1);
        for (char& c : id) if (c == '/') c = '-';
        if (!seen.insert(id).second) continue;   // a higher-precedence dir has it (or hid it)
        KeyFile kf;
        if (!parse_keyfile(full, kf)) continue;
        const auto* g = kf.group("Desktop Entry");
        if (!g) continue;
        App a;
        a.id = id; a.file = full;
        std::string type, tryexec;
        bool hidden = false;
        for (auto& kv : *g) {
            const std::string& k = kv.first;
            if (k == "Type") type = kv.second;
            else if (k == "Name") a.name = unescape(kv.second, false);
            else if (k == "GenericName") a.generic_name = unescape(kv.second, false);
            else if (k == "Comment") a.comment = unescape(kv.second, false);
            else if (k == "Icon") a.icon = unescape(kv.second, false);
            else if (k == "Exec") a.exec = unescape(kv.second, true);
            else if (k == "Path") a.path = unescape(kv.second, false);
            else if (k == "TryExec") tryexec = unescape(kv.second, false);
            else if (k == "Terminal") a.terminal = truthy(kv.second);
            else if (k == "NoDisplay") a.no_display = truthy(kv.second);
            else if (k == "Hidden") hidden = truthy(kv.second);
            else if (k == "MimeType") a.mime_types = split_list(kv.second);
            else if (k == "Categories") a.categories = split_list(kv.second);
        }
        if (hidden || type != "Application" || a.exec.empty() || a.name.empty()) continue;
        if (!tryexec.empty() && !in_path(tryexec)) continue;
        out.push_back(a);
    }
}

void parse_mimeapps(const std::string& path, bool legacy)
{
    KeyFile kf;
    if (!parse_keyfile(path, kf)) return;
    Mimeapps m;
    m.path = path;
    auto fill = [&](const char* group, std::map<std::string, std::vector<std::string>>& to) {
        if (const auto* g = kf.group(group))
            for (auto& kv : *g) {
                auto& v = to[mime_canonical(kv.first)];
                for (const std::string& id : split_list(kv.second)) v.push_back(id);
            }
    };
    fill("Default Applications", m.defaults);
    if (!legacy) { fill("Added Associations", m.added); fill("Removed Associations", m.removed); }
    db.lists.push_back(m);
}

void load()
{
    if (db.loaded) return;
    db.loaded = true;
    std::set<std::string> seen;
    std::vector<std::string> roots = { data_home() };
    for (const std::string& d : data_dirs()) roots.push_back(d);
    for (const std::string& r : roots) scan_dir(r + "/applications", "", seen, db.apps);
    // MimeType= index in precedence order before sorting by name
    for (const App& a : db.apps)
        for (const std::string& t : a.mime_types) {
            auto& v = db.by_mime[mime_canonical(t)];
            if (std::find(v.begin(), v.end(), a.id) == v.end()) v.push_back(a.id);
        }
    std::stable_sort(db.apps.begin(), db.apps.end(), [](const App& x, const App& y) { return lower(x.name) < lower(y.name); });
    for (size_t i = 0; i < db.apps.size(); i++) db.by_id[db.apps[i].id] = i;
    // mimeapps.list precedence (spec): config home, config dirs, data home, data dirs
    parse_mimeapps(config_home() + "/mimeapps.list", false);
    for (const std::string& d : config_dirs()) parse_mimeapps(d + "/mimeapps.list", false);
    parse_mimeapps(data_home() + "/applications/mimeapps.list", false);
    for (const std::string& d : data_dirs()) parse_mimeapps(d + "/applications/mimeapps.list", false);
    for (const std::string& d : data_dirs()) parse_mimeapps(d + "/applications/defaults.list", true);   // the deprecated name, lowest
}

bool removed_by(size_t upto, const std::string& type, const std::string& id)
{
    for (size_t i = 0; i <= upto && i < db.lists.size(); i++) {
        auto it = db.lists[i].removed.find(type);
        if (it != db.lists[i].removed.end() && std::find(it->second.begin(), it->second.end(), id) != it->second.end()) return true;
    }
    return false;
}

// the associations of exactly this type: Added Associations by precedence, then MimeType= matches
std::vector<const App*> associations(const std::string& type)
{
    std::vector<const App*> out;
    auto add = [&](const std::string& id) {
        const App* a = find_app(id);
        if (a && std::find(out.begin(), out.end(), a) == out.end()) out.push_back(a);
    };
    for (size_t i = 0; i < db.lists.size(); i++) {
        auto it = db.lists[i].added.find(type);
        if (it == db.lists[i].added.end()) continue;
        for (const std::string& id : it->second) if (!removed_by(i, type, id)) add(id);
    }
    auto it = db.by_mime.find(type);
    if (it != db.by_mime.end())
        for (const std::string& id : it->second) if (!removed_by(db.lists.size(), type, id)) add(id);
    return out;
}

const App* default_for_exact(const std::string& type)
{
    for (size_t i = 0; i < db.lists.size(); i++) {
        auto it = db.lists[i].defaults.find(type);
        if (it == db.lists[i].defaults.end()) continue;
        for (const std::string& id : it->second) {
            const App* a = find_app(id);
            if (a && !removed_by(i, type, id)) return a;
        }
    }
    return nullptr;
}

std::vector<std::string> type_chain(const std::string& mime)
{
    std::vector<std::string> chain = { mime_canonical(mime) };
    for (const std::string& p : mime_parents(mime)) chain.push_back(p);
    return chain;
}

// ---- mimeapps.list editing -----------------------------------------------------------

struct Lines {
    std::vector<std::string> v;
    // index of "[group]", or -1
    int group(const std::string& name) const
    {
        for (size_t i = 0; i < v.size(); i++) if (trim(v[i]) == "[" + name + "]") return (int)i;
        return -1;
    }
    int group_end(int g) const   // one past the group's last non-blank line
    {
        int end = g + 1, last = g + 1;
        for (; end < (int)v.size(); end++) {
            std::string t = trim(v[(size_t)end]);
            if (!t.empty() && t[0] == '[') break;
            if (!t.empty()) last = end + 1;
        }
        return last;
    }
    int key(int g, const std::string& k) const
    {
        for (int i = g + 1; i < (int)v.size(); i++) {
            std::string t = trim(v[(size_t)i]);
            if (!t.empty() && t[0] == '[') break;
            size_t eq = t.find('=');
            if (eq != std::string::npos && trim(t.substr(0, eq)) == k) return i;
        }
        return -1;
    }
    std::vector<std::string> get(const std::string& grp, const std::string& k) const
    {
        int g = group(grp);
        int i = g < 0 ? -1 : key(g, k);
        if (i < 0) return {};
        const std::string& line = v[(size_t)i];
        return split_list(trim(line.substr(line.find('=') + 1)));
    }
    void set(const std::string& grp, const std::string& k, const std::vector<std::string>& ids, bool list)
    {
        int g = group(grp);
        std::string value;
        for (const std::string& id : ids) value += id + (list ? ";" : "");
        if (!list && ids.size() > 1) { value.clear(); for (const std::string& id : ids) value += id + ";"; }
        if (g < 0) {
            if (ids.empty()) return;
            if (!v.empty() && !trim(v.back()).empty()) v.push_back("");
            v.push_back("[" + grp + "]");
            v.push_back(k + "=" + value);
            return;
        }
        int i = key(g, k);
        if (ids.empty()) { if (i >= 0) v.erase(v.begin() + i); return; }
        if (i >= 0) v[(size_t)i] = k + "=" + value;
        else v.insert(v.begin() + group_end(g), k + "=" + value);
    }
};

// ---- launching ------------------------------------------------------------------------

// Exec tokenizer: whitespace separates, "..." quotes with \ escaping the next char.
std::vector<std::string> tokenize(const std::string& exec)
{
    std::vector<std::string> out;
    std::string cur;
    bool have = false, quoted = false;
    for (size_t i = 0; i < exec.size(); i++) {
        char c = exec[i];
        if (quoted) {
            if (c == '\\' && i + 1 < exec.size()) { cur += exec[++i]; continue; }
            if (c == '"') { quoted = false; continue; }
            cur += c;
            continue;
        }
        if (c == '"') { quoted = true; have = true; continue; }
        if (c == ' ' || c == '\t' || c == '\n') { if (have) out.push_back(cur); cur.clear(); have = false; continue; }
        if (c == '\\' && i + 1 < exec.size()) { cur += exec[++i]; have = true; continue; }   // lenient: unquoted escapes
        cur += c; have = true;
    }
    if (have) out.push_back(cur);
    return out;
}

std::string as_uri(const std::string& f) { return f.find("://") != std::string::npos ? f : uri_from_path(f); }

} // namespace

void reload_apps() { db = Db(); }

const std::vector<App>& all_apps() { load(); return db.apps; }

const App* find_app(const std::string& id)
{
    load();
    auto it = db.by_id.find(id);
    return it == db.by_id.end() ? nullptr : &db.apps[it->second];
}

const App* default_app(const std::string& mime)
{
    load();
    for (const std::string& t : type_chain(mime)) {
        if (const App* a = default_for_exact(t)) return a;
        auto as = associations(t);
        if (!as.empty()) return as[0];
    }
    return nullptr;
}

std::vector<const App*> apps_for(const std::string& mime)
{
    load();
    std::vector<const App*> out;
    auto add = [&](const App* a) { if (a && std::find(out.begin(), out.end(), a) == out.end()) out.push_back(a); };
    add(default_app(mime));
    for (const std::string& t : type_chain(mime)) {
        add(default_for_exact(t));
        for (const App* a : associations(t)) add(a);
    }
    return out;
}

std::string mimeapps_path() { return config_home() + "/mimeapps.list"; }

bool set_default_app(const std::string& mime_in, const std::string& id, std::string* err)
{
    std::string mime = mime_canonical(mime_in);
    std::string path = mimeapps_path();
    Lines L;
    std::string text;
    if (read_file(path, text)) {
        size_t i = 0;
        while (i < text.size()) {
            size_t j = text.find('\n', i);
            if (j == std::string::npos) j = text.size();
            L.v.push_back(text.substr(i, j - i));
            i = j + 1;
        }
    }
    L.set("Default Applications", mime, { id }, false);
    // as GLib does: the default also becomes the first added association, and is no longer removed
    std::vector<std::string> added = L.get("Added Associations", mime);
    added.erase(std::remove(added.begin(), added.end(), id), added.end());
    added.insert(added.begin(), id);
    L.set("Added Associations", mime, added, true);
    std::vector<std::string> removed = L.get("Removed Associations", mime);
    if (std::find(removed.begin(), removed.end(), id) != removed.end()) {
        removed.erase(std::remove(removed.begin(), removed.end(), id), removed.end());
        L.set("Removed Associations", mime, removed, true);
    }
    std::string out;
    for (const std::string& l : L.v) out += l + "\n";
    if (!write_file_atomic(path, out)) { if (err) *err = "cannot write " + path + ": " + strerror(errno); return false; }
    reload_apps();
    return true;
}

std::vector<std::vector<std::string>> exec_argvs(const App& app, const std::vector<std::string>& files)
{
    std::vector<std::string> tokens = tokenize(app.exec);
    bool multi = false, single = false;
    for (const std::string& t : tokens) {
        if (t == "%F" || t == "%U") multi = true;
        else if (t.find("%f") != std::string::npos || t.find("%u") != std::string::npos) single = true;
    }
    auto expand = [&](const std::vector<std::string>& fs) {
        std::vector<std::string> argv;
        for (const std::string& t : tokens) {
            if (t == "%F") { for (const std::string& f : fs) argv.push_back(f); continue; }
            if (t == "%U") { for (const std::string& f : fs) argv.push_back(as_uri(f)); continue; }
            if (t == "%i") { if (!app.icon.empty()) { argv.push_back("--icon"); argv.push_back(app.icon); } continue; }
            std::string out;
            bool drop = false;
            for (size_t i = 0; i < t.size(); i++) {
                if (t[i] != '%' || i + 1 >= t.size()) { out += t[i]; continue; }
                char c = t[++i];
                switch (c) {
                case '%': out += '%'; break;
                case 'f': if (fs.empty()) drop = t == "%f"; else out += fs[0]; break;
                case 'u': if (fs.empty()) drop = t == "%u"; else out += as_uri(fs[0]); break;
                case 'c': out += app.name; break;
                case 'k': out += app.file; break;
                default: break;   // deprecated %d %D %n %N %v %m, and unknown codes: removed
                }
            }
            if (!drop && !(out.empty() && t.size() == 2 && t[0] == '%')) argv.push_back(out);
        }
        // no field code at all: hand the files over anyway (most such programs take paths)
        if (!multi && !single) for (const std::string& f : fs) argv.push_back(f);
        return argv;
    };
    std::vector<std::vector<std::string>> out;
    if (single && !multi && files.size() > 1)
        for (const std::string& f : files) out.push_back(expand({ f }));
    else
        out.push_back(expand(files));
    return out;
}

bool spawn(const std::vector<std::string>& argv, const std::string& cwd, std::string* err)
{
    if (argv.empty()) { if (err) *err = "empty command"; return false; }
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) { if (err) *err = strerror(errno); return false; }
    std::vector<char*> args;
    for (const std::string& a : argv) args.push_back((char*)a.c_str());
    args.push_back(nullptr);
    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); if (err) *err = strerror(errno); return false; }
    if (pid == 0) {
        setsid();
        pid_t p2 = fork();
        if (p2 != 0) _exit(p2 < 0 ? 1 : 0);
        // the grandchild: own session, reparented to init, no inherited X connection
        long maxfd = sysconf(_SC_OPEN_MAX);
        if (maxfd < 0 || maxfd > 4096) maxfd = 4096;
        for (int fd = 3; fd < maxfd; fd++) if (fd != fds[1]) close(fd);
        sigset_t none; sigemptyset(&none); sigprocmask(SIG_SETMASK, &none, nullptr);
        signal(SIGPIPE, SIG_DFL); signal(SIGCHLD, SIG_DFL);
        int dev = open("/dev/null", O_RDONLY);
        if (dev >= 0) { dup2(dev, 0); if (dev != 0) close(dev); }
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) { int e = -errno; (void)!write(fds[1], &e, sizeof e); _exit(127); }
        execvp(args[0], args.data());
        int e = errno;
        (void)!write(fds[1], &e, sizeof e);
        _exit(127);
    }
    close(fds[1]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    int e = 0;
    ssize_t n;
    while ((n = read(fds[0], &e, sizeof e)) < 0 && errno == EINTR) {}
    close(fds[0]);
    if (n == (ssize_t)sizeof e) {
        if (err) *err = e < 0 ? cwd + ": " + strerror(-e) : argv[0] + ": " + strerror(e);
        return false;
    }
    return true;
}

std::string terminal_command() { return setting("terminal", "urxvt"); }

static std::string with_dir(std::string cmd, const std::string& dir)
{
    for (size_t k; (k = cmd.find("%d")) != std::string::npos;) cmd.replace(k, 2, shell_quote(dir));
    return cmd;
}

bool open_terminal(const std::string& dir, std::string* err)
{
    std::string cmd = terminal_command();
    std::string prog = cmd.substr(0, cmd.find_first_of(" \t"));
    if (!in_path(prog)) { if (err) *err = "The terminal program \"" + prog + "\" was not found. Set FLXP_TERMINAL or terminal= in " + settings_path() + "."; return false; }
    return spawn({ "/bin/sh", "-c", with_dir(terminal_command(), dir) }, dir, err);
}

bool launch(const App& app, const std::vector<std::string>& files, std::string* err)
{
    std::string cwd = !app.path.empty() ? app.path : files.empty() ? home_dir() : dir_of(files[0]);
    bool ok = true;
    for (const auto& argv : exec_argvs(app, files)) {
        std::vector<std::string> run = argv;
        if (app.terminal) {
            // <terminal> -e argv...: works for urxvt, xterm and most others
            run = { "/bin/sh", "-c", with_dir(terminal_command(), cwd) + " -e \"$@\"", "sh" };
            run.insert(run.end(), argv.begin(), argv.end());
        }
        if (!spawn(run, cwd, err)) ok = false;
    }
    return ok;
}

bool is_program(const std::string& path, const std::string& mime)
{
    if (mime != "application/x-executable" && mime != "application/x-pie-executable" && mime != "application/x-sharedlib") return false;
    if (access(path.c_str(), X_OK) != 0) return false;
    // a shared object with an interpreter is a PIE executable; plain libraries have none
    if (mime == "application/x-sharedlib") {
        int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) return false;
        unsigned char h[64];
        bool interp = false;
        if (read(fd, h, sizeof h) == (ssize_t)sizeof h && h[4] == 2 && h[5] == 1) {   // ELF64 little-endian
            uint64_t phoff; memcpy(&phoff, h + 32, 8);
            uint16_t phentsize, phnum; memcpy(&phentsize, h + 54, 2); memcpy(&phnum, h + 56, 2);
            for (unsigned i = 0; i < phnum && i < 64; i++) {
                uint32_t type = 0;
                if (pread(fd, &type, 4, (off_t)(phoff + (uint64_t)i * phentsize)) == 4 && type == 3) { interp = true; break; }   // PT_INTERP
            }
        }
        close(fd);
        return interp;
    }
    return true;
}

bool open_files(const std::vector<std::string>& paths, std::string* err)
{
    std::vector<std::pair<const App*, std::vector<std::string>>> groups;
    bool ok = true;
    for (const std::string& p : paths) {
        std::string mime = mime_type(p);
        if (is_program(p, mime)) { if (!spawn({ p }, dir_of(p), err)) ok = false; continue; }
        const App* a = default_app(mime);
        if (!a) { if (err) *err = "No program is associated with " + base_of(p) + " (" + mime + ")."; ok = false; continue; }
        auto g = std::find_if(groups.begin(), groups.end(), [&](const std::pair<const App*, std::vector<std::string>>& x) { return x.first == a; });
        if (g == groups.end()) groups.push_back({ a, { p } }); else g->second.push_back(p);
    }
    for (auto& g : groups) if (!launch(*g.first, g.second, err)) ok = false;
    return ok;
}

} // namespace shell
