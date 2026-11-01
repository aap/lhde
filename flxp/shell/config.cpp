#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "config.h"
#include "util.h"

namespace shell {

std::string home_dir()
{
    const char* h = getenv("HOME");
    if (h && *h) return h;
    struct passwd* pw = getpwuid(getuid());
    return pw && pw->pw_dir ? pw->pw_dir : "/";
}

static std::string env_or(const char* var, const std::string& fallback)
{
    const char* v = getenv(var);
    return v && *v == '/' ? std::string(v) : fallback;   // the spec: relative paths are invalid
}

std::string config_home() { return env_or("XDG_CONFIG_HOME", home_dir() + "/.config"); }
std::string data_home() { return env_or("XDG_DATA_HOME", home_dir() + "/.local/share"); }
std::string state_home() { return env_or("XDG_STATE_HOME", home_dir() + "/.local/state"); }

static std::string lhde_dir(const std::string& base)
{
    std::string d = base + "/lhde";
    mkdir(d.c_str(), 0755);   // the parent exists in practice (~/.config, ~/.local/state); the write is atomic and fails loudly otherwise
    return d;
}
std::string state_file(const char* name) { return lhde_dir(state_home()) + "/" + name; }

static std::vector<std::string> split_dirs(const char* var, const char* fallback)
{
    const char* v = getenv(var);
    std::string s = v && *v ? v : fallback;
    std::vector<std::string> out;
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find(':', i);
        if (j == std::string::npos) j = s.size();
        std::string d = s.substr(i, j - i);
        while (d.size() > 1 && d.back() == '/') d.pop_back();
        if (!d.empty() && d[0] == '/') out.push_back(d);
        i = j + 1;
    }
    return out;
}

std::vector<std::string> config_dirs() { return split_dirs("XDG_CONFIG_DIRS", "/etc/xdg"); }
std::vector<std::string> data_dirs() { return split_dirs("XDG_DATA_DIRS", "/usr/local/share:/usr/share"); }

std::string file_setting(const std::string& key, const std::string& fallback)
{
    std::string text;
    if (!read_file(settings_path(), text)) return fallback;
    size_t i = 0;
    while (i < text.size()) {
        size_t j = text.find('\n', i);
        if (j == std::string::npos) j = text.size();
        std::string line = trim(text.substr(i, j - i));
        i = j + 1;
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (trim(line.substr(0, eq)) == key) {
            std::string v = trim(line.substr(eq + 1));
            return v.empty() ? fallback : v;   // "key =" means automatic
        }
    }
    return fallback;
}

std::string setting(const std::string& key, const std::string& fallback)
{
    std::string var = "FLXP_";
    for (char c : key) var += (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    if (const char* v = getenv(var.c_str())) if (*v) return v;
    return file_setting(key, fallback);
}

int flag_value(const std::string& v, const std::string& where)
{
    if (v.empty()) return -1;
    if (v == "1") return 1;
    if (v == "0") return 0;
    static std::vector<std::string> told;   // once per key
    bool seen = false;
    for (const std::string& t : told) seen = seen || t == where;
    if (!seen) {
        told.push_back(where);
        fprintf(stderr, "flxp: %s = %s: a switch is 0 or 1; automatic\n", where.c_str(), v.c_str());
    }
    return -1;
}

int setting_flag_auto(const std::string& key) { return flag_value(setting(key), "flxp.ini " + key); }

bool setting_flag(const std::string& key, bool fallback)
{
    int v = setting_flag_auto(key);
    return v < 0 ? fallback : v == 1;
}

std::string settings_path() { return lhde_dir(config_home()) + "/flxp.ini"; }

bool write_setting(const std::string& key, const std::string& value)
{
    std::string path = settings_path(), text;
    if (!read_file(path, text)) text = "[flxp]\n";
    std::string line_out = value.empty() ? key + " =" : key + " = " + value;   // "key =": automatic, as the template writes it
    size_t i = 0;
    while (i < text.size()) {
        size_t j = text.find('\n', i);
        if (j == std::string::npos) j = text.size();
        std::string line = text.substr(i, j - i);
        std::string t = trim(line);
        if (!t.empty() && t[0] != '#' && t[0] != ';' && t[0] != '[') {
            size_t eq = t.find('=');
            if (eq != std::string::npos && trim(t.substr(0, eq)) == key) {
                // keep the line's indentation
                size_t ind = 0;
                while (ind < line.size() && (line[ind] == ' ' || line[ind] == '\t')) ind++;
                text.replace(i, j - i, line.substr(0, ind) + line_out);
                return write_file_atomic(path, text);
            }
        }
        i = j + 1;
    }
    if (!text.empty() && text.back() != '\n') text += '\n';
    text += line_out + "\n";
    return write_file_atomic(path, text);
}

std::string exe_dir()
{
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return "";
    exe[n] = 0;
    return dir_of(exe);
}

#ifndef FLXP_DATADIR
#define FLXP_DATADIR "/usr/local/share/lhde"
#endif

// flxp's own program data (res/icons): the installed one (lhde's
// $PREFIX/share/lhde), an installed tree moved elsewhere, the source tree's
// flxp/ for an uninstalled build. Everything else comes through liblhde (lhdata.h).
std::vector<std::string> data_search_dirs()
{
    std::vector<std::string> out;
    out.push_back(FLXP_DATADIR);
    std::string e = exe_dir();
    if (!e.empty()) {
        out.push_back(e + "/../share/lhde");     // an installed tree moved elsewhere
        out.push_back(e + "/../../flxp");        // the source tree: build/bin/flxp, build/tests/assoctest
        out.push_back(e + "/../../../flxp");     // build/tests/w32/<test>
    }
    return out;
}

std::string data_path(const std::string& rel)
{
    for (const std::string& d : data_search_dirs()) {
        std::string p = d + "/" + rel;
        struct stat st;
        if (stat(p.c_str(), &st) != 0) continue;
        char real[4096];
        return realpath(p.c_str(), real) ? std::string(real) : p;
    }
    return "";
}

std::string uri_from_path(const std::string& path)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out = "file://";
    for (unsigned char c : path) {
        // RFC 3986 unreserved plus '/', as GLib's g_filename_to_uri leaves them
        bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '-' || c == '.' || c == '_' || c == '~' || c == '/' ||
                    c == '!' || c == '$' || c == '&' || c == '\'' || c == '(' || c == ')' || c == '*' ||
                    c == '+' || c == ',' || c == ';' || c == '=' || c == ':' || c == '@';
        if (keep) out += (char)c;
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

std::string percent_decode(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() + 0 && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            char h[3] = { s[i + 1], s[i + 2], 0 };
            out += (char)strtol(h, nullptr, 16);
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string path_from_uri(const std::string& uri)
{
    if (uri.compare(0, 7, "file://") != 0) return "";
    std::string rest = uri.substr(7);
    size_t slash = rest.find('/');
    if (slash == std::string::npos) return "";
    std::string host = rest.substr(0, slash);
    if (!host.empty() && host != "localhost") return "";
    std::string p = percent_decode(rest.substr(slash));
    return p;
}

std::string shell_quote(const std::string& s)
{
    std::string out = "'";
    for (char c : s) { if (c == '\'') out += "'\\''"; else out += c; }
    return out + "'";
}

std::string dir_of(const std::string& path)
{
    size_t i = path.find_last_of('/');
    if (i == std::string::npos) return ".";
    if (i == 0) return "/";
    return path.substr(0, i);
}

std::string base_of(const std::string& path)
{
    if (path == "/") return "/";
    size_t i = path.find_last_of('/');
    return i == std::string::npos ? path : path.substr(i + 1);
}

bool make_dirs(const std::string& dir)
{
    if (dir.empty()) return false;
    struct stat st;
    if (stat(dir.c_str(), &st) == 0) return S_ISDIR(st.st_mode);
    std::string parent = dir_of(dir);
    if (parent != dir && !make_dirs(parent)) return false;
    return mkdir(dir.c_str(), 0700) == 0 || errno == EEXIST;
}

bool write_file_atomic(const std::string& path_in, const std::string& data)
{
    // write through a symlink (dotfile managers link these files) instead of replacing it
    std::string path = path_in;
    if (char* rp = realpath(path_in.c_str(), nullptr)) { path = rp; free(rp); }
    if (!make_dirs(dir_of(path))) return false;
    std::string tmp = path + ".flxp-tmp";
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    size_t off = 0;
    while (off < data.size()) {
        ssize_t w = write(fd, data.data() + off, data.size() - off);
        if (w <= 0) { close(fd); unlink(tmp.c_str()); return false; }
        off += (size_t)w;
    }
    // keep the mode of the file being replaced
    struct stat st;
    if (stat(path.c_str(), &st) == 0) fchmod(fd, st.st_mode & 07777);
    if (close(fd) != 0 || rename(tmp.c_str(), path.c_str()) != 0) { unlink(tmp.c_str()); return false; }
    return true;
}

} // namespace shell
