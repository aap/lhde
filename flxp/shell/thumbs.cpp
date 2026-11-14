#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <time.h>
#include <sys/wait.h>
#include <algorithm>
#include <map>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <list>
#include <set>
#include <unordered_map>
#include <mutex>
#include <thread>
#include "liblhde/compat.h"
#include "thumbs.h"
#include "config.h"
#include "mime.h"
#include "gdi/image.h"
#include "gdi/png.h"

namespace shell {
namespace thumbs {

// ---- where thumbnails are made -----------------------------------------------------

bool on_network(const std::string& path)
{
    return lhde_fs_is_remote(path.c_str());
}

bool picture_size(const std::string& path, int* w, int* h)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    unsigned char b[32];
    size_t n = fread(b, 1, sizeof b, f);
    bool ok = false;
    auto be16 = [](const unsigned char* p) { return p[0] << 8 | p[1]; };
    auto be32 = [](const unsigned char* p) { return (int)((unsigned)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]); };
    auto le16 = [](const unsigned char* p) { return p[0] | p[1] << 8; };
    auto le32 = [](const unsigned char* p) { return (int)((unsigned)p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24); };
    if (n >= 24 && !memcmp(b, "\x89PNG", 4)) { *w = be32(b + 16); *h = be32(b + 20); ok = true; }
    else if (n >= 10 && (!memcmp(b, "GIF87a", 6) || !memcmp(b, "GIF89a", 6))) { *w = le16(b + 6); *h = le16(b + 8); ok = true; }
    else if (n >= 26 && b[0] == 'B' && b[1] == 'M') { *w = le32(b + 18); int y = le32(b + 22); *h = y < 0 ? -y : y; ok = true; }
    else if (n >= 30 && !memcmp(b, "RIFF", 4) && !memcmp(b + 8, "WEBPVP8", 7)) {
        if (b[15] == ' ') { *w = le16(b + 26) & 0x3fff; *h = le16(b + 28) & 0x3fff; ok = true; }
        else if (b[15] == 'L') { unsigned v = (unsigned)le32(b + 21); *w = (int)(v & 0x3fff) + 1; *h = (int)((v >> 14) & 0x3fff) + 1; ok = true; }
        else if (b[15] == 'X') { *w = (b[24] | b[25] << 8 | b[26] << 16) + 1; *h = (b[27] | b[28] << 8 | b[29] << 16) + 1; ok = true; }
    } else if (n >= 4 && b[0] == 0xff && b[1] == 0xd8) {   // JPEG: the frame header (SOFn)
        long at = 2;
        unsigned char m[9];
        while (fseek(f, at, SEEK_SET) == 0 && fread(m, 1, 4, f) == 4 && m[0] == 0xff) {
            int marker = m[1], len = be16(m + 2);
            if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
                if (fread(m, 1, 5, f) == 5) { *h = be16(m + 1); *w = be16(m + 3); ok = true; }
                break;
            }
            if (len < 2) break;
            at += 2 + len;
        }
    }
    fclose(f);
    return ok && *w > 0 && *h > 0;
}

bool enabled_for(const std::string& folder)
{
    if (!setting_flag("thumbnails", true)) return false;
    if (setting_flag("thumbnails_network", false)) return true;
    return !on_network(folder);
}

// ---- MD5 (RFC 1321) ---------------------------------------------------------------

std::string md5_hex(const std::string& data)
{
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };
    static const int R[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                               5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
                               4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                               6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
    uint32_t h[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };
    std::string m = data;
    uint64_t bits = (uint64_t)data.size() * 8;
    m += (char)0x80;
    while (m.size() % 64 != 56) m += (char)0;
    for (int i = 0; i < 8; i++) m += (char)(bits >> (8 * i));
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[16];
        for (int i = 0; i < 16; i++) {
            const unsigned char* p = (const unsigned char*)m.data() + off + i * 4;
            w[i] = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 64; i++) {
            uint32_t f; int g;
            if (i < 16) { f = (b & c) | (~b & d); g = i; }
            else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
            else { f = c ^ (b | ~d); g = (7 * i) % 16; }
            uint32_t t = d; d = c; c = b;
            uint32_t x = a + f + K[i] + w[g];
            b = b + (x << R[i] | x >> (32 - R[i]));
            a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    }
    static const char hex[] = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 4; k++) { uint8_t v = (uint8_t)(h[i] >> (8 * k)); out += hex[v >> 4]; out += hex[v & 15]; }
    return out;
}

// ---- the cache ----------------------------------------------------------------------

std::string cache_dir()
{
    const char* c = getenv("XDG_CACHE_HOME");
    if (c && c[0] == '/') return std::string(c) + "/thumbnails";
    return home_dir() + "/.cache/thumbnails";
}

int cache_size_for(int view_size) { return view_size <= 128 ? 128 : 256; }

static const char* size_dir(int cache_size) { return cache_size <= 128 ? "normal" : "large"; }

std::string cache_path(const std::string& file, int cache_size)
{
    return cache_dir() + "/" + size_dir(cache_size) + "/" + md5_hex(uri_from_path(file)) + ".png";
}

bool own_picture(const std::string& path)
{
    size_t dot = path.rfind('.');
    if (dot == std::string::npos || path.find('/', dot) != std::string::npos) return false;
    std::string ext = path.substr(dot + 1);
    for (char& c : ext) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    static const char* known[] = { "png", "jpg", "jpeg", "jpe", "jfif", "bmp", "dib", "ico", "cur" };
    for (const char* k : known) if (ext == k) return true;
    return ext == "webp" && gdi::webp_supported();
}

static bool mkdir_private(const std::string& dir)
{
    // the spec wants the thumbnail directories 0700
    std::string d;
    size_t i = 0;
    while (i < dir.size()) {
        size_t j = dir.find('/', i + 1);
        if (j == std::string::npos) j = dir.size();
        d = dir.substr(0, j);
        i = j;
        if (mkdir(d.c_str(), 0700) != 0 && errno != EEXIST) return false;
    }
    return true;
}

// Thumb::URI and Thumb::MTime of a cached file match; bmp (if wanted) decoded.
static bool cached(const std::string& file, const std::string& uri, int64_t mtime, gdi::Bitmap** bmp)
{
    std::string data;
    if (!read_file(file, data) || data.size() < 8) return false;
    gdi::PngText text = gdi::read_png_text((const uint8_t*)data.data(), data.size());
    bool uri_ok = false, mtime_ok = false;
    for (auto& t : text) {
        if (t.first == "Thumb::URI") uri_ok = t.second == uri;
        else if (t.first == "Thumb::MTime") mtime_ok = strtoll(t.second.c_str(), nullptr, 10) == mtime;
    }
    if (!uri_ok || !mtime_ok) return false;
    if (bmp) *bmp = gdi::decode_png((const uint8_t*)data.data(), data.size());
    return !bmp || *bmp;
}

// tmp file in the same directory, 0600, then rename (readers never see half a file)
static void save(const gdi::Bitmap* b, const std::string& path, const gdi::PngText& text)
{
    std::string dir = dir_of(path);
    if (!mkdir_private(dir)) return;
    char tmp[64];
    snprintf(tmp, sizeof tmp, ".flxp-%d-%lx.tmp", (int)getpid(), (unsigned long)(uintptr_t)&tmp);
    std::string t = path + tmp;
    if (!gdi::write_png(b, t, text)) { unlink(t.c_str()); return; }
    chmod(t.c_str(), 0600);
    if (rename(t.c_str(), path.c_str()) != 0) unlink(t.c_str());
}

static std::atomic<int> g_decodes{0};
int decodes() { return g_decodes; }

// At most DECODES_AT_ONCE pictures decode at a time in the process, whichever loader (the view's two threads, the panes'
// own) asks: each decode may hold a few MB (a JPEG at 1/8, a PNG's rows) up to libjpeg's 160 MB for a huge progressive one.
static std::mutex g_turn_lock;
static std::condition_variable g_turn_cv;
static int g_decoding, g_decoding_most;
struct DecodeTurn {
    DecodeTurn()
    {
        std::unique_lock<std::mutex> l(g_turn_lock);
        g_turn_cv.wait(l, [] { return g_decoding < DECODES_AT_ONCE; });
        if (++g_decoding > g_decoding_most) g_decoding_most = g_decoding;
    }
    ~DecodeTurn()
    {
        { std::lock_guard<std::mutex> l(g_turn_lock); g_decoding--; }
        g_turn_cv.notify_one();
    }
};
int decodes_at_once_most() { std::lock_guard<std::mutex> l(g_turn_lock); return g_decoding_most; }

static bool trace()
{
    static int on = -1;
    if (on < 0) { const char* t = getenv("FLXP_TRACE_THUMBS"); on = t && atoi(t) > 0; }
    return on > 0;
}

// ---- thumbnailer entries ----------------------------------------------------------------

namespace {
struct Entries {
    std::vector<Thumbnailer*> list;                       // the order they are tried
    std::map<const Thumbnailer*, bool> present;           // its program is there (looked up once)
    std::map<std::string, const Thumbnailer*> by_type;    // canonical type -> the entry, null: none
    bool loaded = false;
};
Entries g_entries;
std::mutex g_entries_lock;
std::atomic<int> g_timeout_ms{10000};
std::atomic<size_t> g_max_output{16u << 20};
std::atomic<int> g_runs{0};
std::atomic<unsigned> g_out_serial{0};
}

std::vector<std::string> thumbnailer_dirs()
{
    std::vector<std::string> out = { data_home() + "/thumbnailers" };
    for (const std::string& d : data_dirs()) out.push_back(d + "/thumbnailers");
    // lhde's: beside its data directory (<prefix>/share/lhde) or the source tree's share/ (flxp/ found from build/)
    for (const std::string& d : data_search_dirs()) {
        std::string base = base_of(d), up = dir_of(d);
        if (base == "lhde") out.push_back(up + "/thumbnailers");
        else if (base == "flxp") out.push_back(up + "/share/thumbnailers");
    }
    std::vector<std::string> unique;
    std::set<std::string> seen;
    for (const std::string& d : out) {
        char real[PATH_MAX];
        std::string r = realpath(d.c_str(), real) ? std::string(real) : d;
        if (seen.insert(r).second) unique.push_back(d);
    }
    return unique;
}

// a key file value's escapes: \s \n \t \r \\ (the desktop entry spec)
static std::string unescape_value(const std::string& v)
{
    std::string out;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i] != '\\' || i + 1 == v.size()) { out += v[i]; continue; }
        char c = v[++i];
        out += c == 's' ? ' ' : c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c;
    }
    return out;
}

static Thumbnailer* read_entry(const std::string& file)
{
    std::string text;
    if (!read_file(file, text)) return nullptr;
    Thumbnailer t;
    t.file = file;
    t.name = base_of(file);
    bool in = false;
    size_t i = 0;
    while (i < text.size()) {
        size_t j = text.find('\n', i);
        if (j == std::string::npos) j = text.size();
        std::string line = trim(text.substr(i, j - i));
        i = j + 1;
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') { in = line == "[Thumbnailer Entry]"; continue; }
        size_t eq = line.find('=');
        if (!in || eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq)), value = unescape_value(trim(line.substr(eq + 1)));
        if (key == "Exec") t.exec = value;
        else if (key == "TryExec") t.try_exec = value;
        else if (key == "MimeType") {
            size_t a = 0;
            while (a < value.size()) {
                size_t b = value.find(';', a);
                if (b == std::string::npos) b = value.size();
                std::string type = trim(value.substr(a, b - a));
                if (!type.empty()) t.types.push_back(mime_canonical(type));
                a = b + 1;
            }
        }
    }
    if (t.exec.empty() || t.types.empty()) return nullptr;
    return new Thumbnailer(t);
}

static void load_entries()
{
    if (g_entries.loaded) return;
    g_entries.loaded = true;
    std::set<std::string> names;
    std::vector<Thumbnailer*> others, ours;
    for (const std::string& dir : thumbnailer_dirs()) {
        std::vector<std::string> files;
        if (DIR* d = opendir(dir.c_str())) {
            while (struct dirent* e = readdir(d)) {
                std::string n = e->d_name;
                if (n.size() > 12 && n.compare(n.size() - 12, 12, ".thumbnailer") == 0) files.push_back(n);
            }
            closedir(d);
        }
        std::sort(files.begin(), files.end());
        for (const std::string& n : files) {
            if (!names.insert(n).second) continue;   // hidden by the same name in an earlier directory
            Thumbnailer* t = read_entry(dir + "/" + n);
            if (!t) continue;
            (n.compare(0, 5, "lhde-") == 0 ? ours : others).push_back(t);
        }
    }
    g_entries.list = others;
    g_entries.list.insert(g_entries.list.end(), ours.begin(), ours.end());
}

std::vector<const Thumbnailer*> thumbnailers()
{
    std::lock_guard<std::mutex> l(g_entries_lock);
    load_entries();
    return std::vector<const Thumbnailer*>(g_entries.list.begin(), g_entries.list.end());
}

void reload_thumbnailers()
{
    std::lock_guard<std::mutex> l(g_entries_lock);
    g_entries.list.clear();   // the entries themselves stay: a loader may hold one
    g_entries.present.clear();
    g_entries.by_type.clear();
    g_entries.loaded = false;
}

// the line split as a shell splits it, without expansion
static std::vector<std::string> split_shell(const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    bool have = false;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\n') {
            if (have) out.push_back(cur);
            cur.clear(); have = false;
        } else if (c == '\'') {
            have = true;
            while (++i < s.size() && s[i] != '\'') cur += s[i];
        } else if (c == '"') {
            have = true;
            while (++i < s.size() && s[i] != '"') {
                if (s[i] == '\\' && i + 1 < s.size() && strchr("$`\"\\\n", s[i + 1])) {
                    if (s[++i] != '\n') cur += s[i];
                } else cur += s[i];
            }
        } else if (c == '\\' && i + 1 < s.size()) {
            have = true;
            if (s[++i] != '\n') cur += s[i];
        } else { cur += c; have = true; }
    }
    if (have) out.push_back(cur);
    return out;
}

std::vector<std::string> thumbnailer_argv(const Thumbnailer& t, const std::string& path, const std::string& out, int size)
{
    std::string line;
    for (size_t i = 0; i < t.exec.size(); i++) {
        if (t.exec[i] != '%' || i + 1 == t.exec.size()) { line += t.exec[i]; continue; }
        switch (t.exec[++i]) {
        case 'i': line += shell_quote(path); break;
        case 'o': line += shell_quote(out); break;
        case 'u': line += shell_quote(uri_from_path(path)); break;
        case 's': line += std::to_string(size); break;
        case '%': line += '%'; break;
        default: break;   // an unknown field code is dropped
        }
    }
    return split_shell(line);
}

static bool runnable(const std::string& cmd)
{
    if (cmd.empty()) return false;
    if (cmd.find('/') != std::string::npos) return access(cmd.c_str(), X_OK) == 0;
    const char* p = getenv("PATH");
    std::string path = p ? p : "/usr/local/bin:/usr/bin:/bin";
    size_t a = 0;
    while (a <= path.size()) {
        size_t b = path.find(':', a);
        if (b == std::string::npos) b = path.size();
        std::string dir = b > a ? path.substr(a, b - a) : ".";
        std::string f = dir + "/" + cmd;
        struct stat st;
        if (stat(f.c_str(), &st) == 0 && S_ISREG(st.st_mode) && access(f.c_str(), X_OK) == 0) return true;
        a = b + 1;
    }
    return false;
}

static bool present(const Thumbnailer* t)
{
    auto it = g_entries.present.find(t);
    if (it != g_entries.present.end()) return it->second;
    std::string cmd = t->try_exec;
    if (cmd.empty()) {
        std::vector<std::string> argv = split_shell(t->exec);
        if (!argv.empty()) cmd = argv[0];
    }
    bool ok = runnable(cmd);
    g_entries.present[t] = ok;
    return ok;
}

const Thumbnailer* thumbnailer_for(const std::string& path)
{
    if (own_picture(path)) return nullptr;
    std::string type = mime_type_for_name(base_of(path));
    if (type.empty()) return nullptr;
    type = mime_canonical(type);
    std::lock_guard<std::mutex> l(g_entries_lock);
    load_entries();
    auto cached = g_entries.by_type.find(type);
    if (cached != g_entries.by_type.end()) return cached->second;
    std::vector<std::string> types = { type };
    for (const std::string& p : mime_parents(type)) if (p != "application/octet-stream") types.push_back(p);
    const Thumbnailer* found = nullptr;
    for (size_t k = 0; k < types.size() && !found; k++)
        for (const Thumbnailer* t : g_entries.list)
            if (std::find(t->types.begin(), t->types.end(), types[k]) != t->types.end() && present(t)) { found = t; break; }
    g_entries.by_type[type] = found;
    return found;
}

bool can_thumbnail(const std::string& path)
{
    return own_picture(path) || thumbnailer_for(path) != nullptr;
}

void set_thumbnailer_limits(int timeout_ms, size_t max_bytes)
{
    g_timeout_ms = timeout_ms;
    g_max_output = max_bytes;
}

int thumbnailer_runs() { return g_runs; }

// a loader asks its running thumbnailer to stop when it is destroyed
static thread_local const std::atomic<bool>* t_stop;

// The entry's program on path, its PNG into out; "" when it wrote one, else why not.
static std::string run_thumbnailer(const Thumbnailer& t, const std::string& path, const std::string& out, int size)
{
    std::vector<std::string> args = thumbnailer_argv(t, path, out, size);
    if (args.empty()) return "an empty Exec";
    std::vector<char*> argv;
    for (std::string& a : args) argv.push_back(&a[0]);
    argv.push_back(nullptr);
    std::vector<std::string> env;
    for (const char* k : { "PATH", "HOME", "LANG", "LC_ALL", "TMPDIR" })
        if (const char* v = getenv(k)) env.push_back(std::string(k) + "=" + v);
    if (!getenv("PATH")) env.push_back("PATH=/usr/local/bin:/usr/bin:/bin");
    std::vector<char*> envp;
    for (std::string& e : env) envp.push_back(&e[0]);
    envp.push_back(nullptr);

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    if (!trace()) posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
    posix_spawn_file_actions_addclosefrom_np(&fa, 3);   // not the X connection or any other descriptor of flxp's
#endif
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    sigset_t none, all;
    sigemptyset(&none);
    sigfillset(&all);
    posix_spawnattr_setsigmask(&attr, &none);
    posix_spawnattr_setsigdefault(&attr, &all);
    posix_spawnattr_setpgroup(&attr, 0);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    pid_t pid = -1;
    int err = posix_spawnp(&pid, argv[0], &fa, &attr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);
    if (err != 0) return std::string("cannot run ") + args[0] + ": " + strerror(err);
    g_runs++;
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int status = 0;
    std::string why;
    for (int wait_ms = 5;;) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid || (r < 0 && errno != EINTR)) break;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long ms = (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;
        bool stop = t_stop && t_stop->load();
        if (ms >= g_timeout_ms || stop) {
            kill(-pid, SIGKILL);
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            why = stop ? "stopped" : "timed out after " + std::to_string(ms) + " ms";
            break;
        }
        struct timespec nap = { 0, wait_ms * 1000000L };
        nanosleep(&nap, nullptr);
        if (wait_ms < 50) wait_ms += 5;
    }
    kill(-pid, SIGKILL);   // what it left running in its group
    if (!why.empty()) return why;
    if (!WIFEXITED(status)) return "killed by signal " + std::to_string(WTERMSIG(status));
    if (WEXITSTATUS(status) != 0) return "exit " + std::to_string(WEXITSTATUS(status));
    struct stat st;
    if (stat(out.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size == 0) return "no output";
    if ((size_t)st.st_size > g_max_output) return "output of " + std::to_string((long long)st.st_size) + " bytes";
    return "";
}

gdi::Bitmap* get(const std::string& path, int64_t mtime, int cache_size, const Thumbnailer* by)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return nullptr;
    mtime = (int64_t)st.st_mtime;
    std::string uri = uri_from_path(path);
    std::string hash = md5_hex(uri);
    std::string dir = cache_dir();
    std::string file = dir + "/" + size_dir(cache_size) + "/" + hash + ".png";
    gdi::Bitmap* b = nullptr;
    if (cached(file, uri, mtime, &b)) return b;
    // a larger current thumbnail serves a smaller size too
    if (cache_size <= 128 && cached(dir + "/large/" + hash + ".png", uri, mtime, &b)) {
        int w, h;
        gdi::fit_size(b->width, b->height, cache_size, cache_size, false, &w, &h);
        if (w == b->width && h == b->height) return b;
        gdi::Bitmap* s = gdi::resample(b, w, h);
        gdi::destroy_bitmap(b);
        return s;
    }
    std::string fail = dir + "/fail/flxp-1.0/" + hash + ".png";
    if (cached(fail, uri, mtime, nullptr)) return nullptr;
    bool in_cache = path.compare(0, dir.size() + 1, dir + "/") == 0;   // never thumbnail thumbnails
    char mt[32], sz[32];
    snprintf(mt, sizeof mt, "%lld", (long long)mtime);
    snprintf(sz, sizeof sz, "%lld", (long long)st.st_size);
    int ow = 0, oh = 0;
    g_decodes++;
    gdi::Bitmap* img = nullptr;
    std::string why;
    if (by) {
        // the entry's program writes a PNG into the thumbnail directory, read like any other
        if (!mkdir_private(dir)) return nullptr;
        char tmp[96];
        snprintf(tmp, sizeof tmp, "/.flxp-%d-%u.png", (int)getpid(), g_out_serial++);
        std::string out = dir + tmp;
        {
            DecodeTurn turn;
            why = run_thumbnailer(*by, path, out, cache_size);
            if (why.empty()) {
                img = gdi::load_image(out, cache_size);
                if (!img) why = "not a picture";
            }
        }
        unlink(out.c_str());
        if (why == "stopped") { if (img) gdi::destroy_bitmap(img); return nullptr; }   // not a failure of the file
    } else {
        DecodeTurn turn;
        img = gdi::load_image(path, cache_size, &ow, &oh);
    }
    if (trace() && by)
        fprintf(stderr, "thumbs: decode %s by %s at %dx%d%s%s\n", path.c_str(), by->name.c_str(), img ? img->width : 0,
                img ? img->height : 0, why.empty() ? "" : ": ", why.c_str());
    else if (trace())
        fprintf(stderr, "thumbs: decode %s %dx%d at %dx%d\n", path.c_str(), ow, oh, img ? img->width : 0, img ? img->height : 0);
    if (!img) {
        if (!in_cache) {
            gdi::Bitmap* dot = gdi::create_bitmap(1, 1);
            if (dot) {
                dot->pixels[0] = 0;
                save(dot, fail, { { "Thumb::URI", uri }, { "Thumb::MTime", mt }, { "Software", "flxp" } });
                gdi::destroy_bitmap(dot);
            }
        }
        return nullptr;
    }
    int w, h;
    gdi::fit_size(img->width, img->height, cache_size, cache_size, false, &w, &h);
    if (w != img->width || h != img->height) {
        gdi::Bitmap* s = gdi::resample(img, w, h);
        gdi::destroy_bitmap(img);
        img = s;
        if (!img) return nullptr;
    }
    if (!in_cache) {
        gdi::PngText text = { { "Thumb::URI", uri }, { "Thumb::MTime", mt }, { "Thumb::Size", sz } };
        if (ow > 0 && oh > 0) {   // a picture's own size (a thumbnailer's page has none to give)
            text.push_back({ "Thumb::Image::Width", std::to_string(ow) });
            text.push_back({ "Thumb::Image::Height", std::to_string(oh) });
        }
        text.push_back({ "Software", "flxp" });
        save(img, file, text);
    }
    return img;
}

// ---- the loader ---------------------------------------------------------------------

void free_result(Loader::Result& r)
{
    if (r.bmp) gdi::destroy_bitmap(r.bmp);
    for (gdi::Bitmap* m : r.minis) if (m) gdi::destroy_bitmap(m);
    r.bmp = nullptr;
    r.minis.clear();
}

static gdi::Bitmap* fitted(gdi::Bitmap* b, int size)
{
    if (!b || (b->width <= size && b->height <= size)) return b;
    int w, h;
    gdi::fit_size(b->width, b->height, size, size, false, &w, &h);
    gdi::Bitmap* s = gdi::resample(b, w, h);
    gdi::destroy_bitmap(b);
    return s;
}

// XP's folder thumbnail: the first pictures of a folder (it collects 8 candidates
// for 4 previews, in case some fail)
static std::vector<gdi::Bitmap*> folder_minis(const std::string& dir, int mini)
{
    std::vector<gdi::Bitmap*> out;
    std::vector<std::string> names;
    if (DIR* d = opendir(dir.c_str())) {
        while (struct dirent* e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            if (own_picture(e->d_name)) names.push_back(e->d_name);
        }
        closedir(d);
    }
    std::sort(names.begin(), names.end());
    // the folder's own picture first (XP and 4074: folder.jpg, folder.gif; flxp: also .png)
    auto own = [](const std::string& n) {
        std::string l = n;
        for (char& c : l) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        return l == "folder.jpg" || l == "folder.gif" || l == "folder.png";
    };
    std::stable_partition(names.begin(), names.end(), own);
    for (size_t i = 0; i < names.size() && i < 8 && out.size() < 4; i++) {
        std::string p = dir + "/" + names[i];
        if (gdi::Bitmap* b = fitted(get(p, 0, 128), mini)) out.push_back(b);
    }
    return out;
}

struct Loader::Impl {
    struct Job { uint32_t gen; int id; std::string path; int64_t mtime; int size; bool folder; int mini;
                 bool picture; const Thumbnailer* by; };   // by: the thumbnailer of a file that is not a picture
    Notify notify;
    void* ctx;
    std::mutex lock;
    std::condition_variable cv;
    std::deque<Job> jobs;
    std::deque<Result> done;
    std::vector<std::thread> threads;
    std::atomic<uint32_t> gen{1};
    int working = 0;                // jobs taken by a thread and not done (under lock)
    bool quit = false;
    std::atomic<bool> stop{false};  // the destructor: a running thumbnailer is killed
    void run();
};

Loader::Loader(Notify notify, void* ctx, int threads) : impl_(new Impl)
{
    impl_->notify = notify;
    impl_->ctx = ctx;
    if (threads <= 0) {
        long n = sysconf(_SC_NPROCESSORS_ONLN);
        threads = n < 2 ? 1 : n > 2 ? 2 : (int)n;   // two is plenty (and at most DECODES_AT_ONCE decode at a time)
    }
    for (int i = 0; i < threads; i++) impl_->threads.emplace_back([this] { impl_->run(); });
}

Loader::~Loader()
{
    {
        std::lock_guard<std::mutex> l(impl_->lock);
        impl_->quit = true;
        impl_->jobs.clear();
    }
    impl_->stop = true;
    impl_->cv.notify_all();
    for (std::thread& t : impl_->threads) t.join();
    for (Result& r : impl_->done) free_result(r);
    delete impl_;
}

uint32_t Loader::generation() const { return impl_->gen; }

bool Loader::busy() const
{
    std::lock_guard<std::mutex> l(impl_->lock);
    return !impl_->jobs.empty() || impl_->working > 0;
}

void Loader::request(uint32_t gen, int id, const std::string& path, int64_t mtime, int view_size, bool folder, int mini_size)
{
    bool picture = folder || own_picture(path);
    const Thumbnailer* by = nullptr;
    if (!picture && (setting_flag("thumbnails_network", false) || !on_network(path))) by = thumbnailer_for(path);
    {
        std::lock_guard<std::mutex> l(impl_->lock);
        impl_->jobs.push_back(Impl::Job{ gen, id, path, mtime, view_size, folder, mini_size, picture, by });
    }
    impl_->cv.notify_one();
}

uint32_t Loader::cancel()
{
    std::lock_guard<std::mutex> l(impl_->lock);
    impl_->jobs.clear();
    for (Result& r : impl_->done) free_result(r);
    impl_->done.clear();
    return ++impl_->gen;
}

bool Loader::take(Result& out)
{
    std::lock_guard<std::mutex> l(impl_->lock);
    while (!impl_->done.empty()) {
        out = impl_->done.front();
        impl_->done.pop_front();
        if (out.gen == impl_->gen) return true;
        free_result(out);   // finished after a cancel
    }
    return false;
}

void Loader::Impl::run()
{
    t_stop = &stop;
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> l(lock);
            cv.wait(l, [this] { return quit || !jobs.empty(); });
            if (quit) return;
            job = jobs.front();
            jobs.pop_front();
            if (job.gen != gen) continue;
            working++;
        }
        Result r;
        r.gen = job.gen; r.id = job.id; r.path = job.path;
        if (job.folder) r.minis = folder_minis(job.path, job.mini);
        else if (job.picture || job.by) r.bmp = fitted(get(job.path, job.mtime, cache_size_for(job.size), job.by), job.size);
        bool wake;
        {
            std::lock_guard<std::mutex> l(lock);
            working--;
            if (job.gen != gen || quit) { free_result(r); continue; }
            wake = done.empty();
            done.push_back(r);
        }
        if (wake && notify) notify(ctx);   // one wake-up per batch: the UI takes everything there is
    }
}

// ---- the view's images ----------------------------------------------------------------

struct Slots::Impl {
    struct Entry { int slot; std::list<std::string>::iterator at; };
    std::list<std::string> lru;               // the most recently used first
    std::unordered_map<std::string, Entry> map;
    std::set<std::string> pinned;
    std::vector<int> free;
    size_t budget, item = 1;
    int next = 0;
};

Slots::Slots(size_t budget) : impl_(new Impl) { impl_->budget = budget; }
Slots::~Slots() { delete impl_; }
size_t Slots::bytes() const { return impl_->map.size() * impl_->item; }
size_t Slots::count() const { return impl_->map.size(); }
size_t Slots::budget() const { return impl_->budget; }
size_t Slots::item_bytes() const { return impl_->item; }
int Slots::slots() const { return impl_->next; }
void Slots::pin(const std::set<std::string>& keys) { impl_->pinned = keys; }

void Slots::reset(size_t item_bytes)
{
    impl_->lru.clear();
    impl_->map.clear();
    impl_->pinned.clear();
    impl_->free.clear();
    impl_->next = 0;
    impl_->item = item_bytes ? item_bytes : 1;
}

int Slots::find(const std::string& key)
{
    auto it = impl_->map.find(key);
    if (it == impl_->map.end()) return -1;
    impl_->lru.splice(impl_->lru.begin(), impl_->lru, it->second.at);
    return it->second.slot;
}

void Slots::erase(const std::string& key)
{
    auto it = impl_->map.find(key);
    if (it == impl_->map.end()) return;
    impl_->free.push_back(it->second.slot);
    impl_->lru.erase(it->second.at);
    impl_->map.erase(it);
}

int Slots::insert(const std::string& key, std::vector<std::string>* evicted)
{
    int have = find(key);
    if (have >= 0) return have;
    Impl* p = impl_;
    auto victim = p->lru.end();
    while (bytes() + p->item > p->budget && victim != p->lru.begin()) {
        --victim;
        if (p->pinned.count(*victim)) continue;
        std::string k = *victim;
        ++victim;   // stays valid: the erased element is the one before it
        if (evicted) evicted->push_back(k);
        erase(k);
    }
    int slot;
    if (!p->free.empty()) { slot = p->free.back(); p->free.pop_back(); }
    else slot = p->next++;
    p->lru.push_front(key);
    p->map[key] = Impl::Entry{ slot, p->lru.begin() };
    return slot;
}

} // namespace thumbs
} // namespace shell
