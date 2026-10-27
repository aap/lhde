// shared-mime-info reader. See mime.h; the spec is
// https://specifications.freedesktop.org/shared-mime-info-spec/
#include <fcntl.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <map>
#include <set>
#include "config.h"
#include "mime.h"

namespace shell {

namespace {

struct Glob { int weight; std::string type, pattern; bool cs; };

struct Db {
    bool loaded = false;
    std::map<std::string, std::vector<Glob>> literal;   // lower-cased name -> globs
    std::map<std::string, std::vector<Glob>> ext;       // lower-cased "tar.gz" (from "*.tar.gz") -> globs
    std::vector<Glob> general;                          // anything else, fnmatch
    std::map<std::string, std::string> aliases;         // alias -> canonical
    std::multimap<std::string, std::string> parents;    // type -> parent
    std::map<std::string, std::string> generic_icon;    // type -> freedesktop icon name
    std::map<std::string, std::string> comments;        // cache
    std::vector<std::string> mime_dirs;                 // user first
};

Db db;

std::string lower(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c += 32; return s; }

bool has_wild(const std::string& s) { return s.find_first_of("*?[") != std::string::npos; }

template <class F> void each_line(const std::string& text, F f)
{
    size_t i = 0;
    while (i < text.size()) {
        size_t j = text.find('\n', i);
        if (j == std::string::npos) j = text.size();
        if (j > i && text[i] != '#') f(text.substr(i, j - i));
        i = j + 1;
    }
}

void load()
{
    if (db.loaded) return;
    db.loaded = true;
    db.mime_dirs.push_back(data_home() + "/mime");
    for (const std::string& d : data_dirs()) db.mime_dirs.push_back(d + "/mime");
    std::set<std::string> noglobs;   // "__NOGLOBS__" in a higher-precedence dir drops the type's globs below it
    for (const std::string& dir : db.mime_dirs) {
        std::string text;
        std::set<std::string> blocked_here;
        if (read_file(dir + "/globs2", text)) {
            each_line(text, [&](const std::string& line) {
                // weight:type:glob[:flags]
                size_t a = line.find(':'), b = a == std::string::npos ? a : line.find(':', a + 1);
                if (b == std::string::npos) return;
                Glob g;
                g.weight = atoi(line.substr(0, a).c_str());
                g.type = line.substr(a + 1, b - a - 1);
                std::string rest = line.substr(b + 1);
                size_t c = rest.find(':');
                g.pattern = c == std::string::npos ? rest : rest.substr(0, c);
                g.cs = c != std::string::npos && rest.find("cs", c) != std::string::npos;
                if (noglobs.count(g.type)) return;
                if (g.pattern == "__NOGLOBS__") { blocked_here.insert(g.type); return; }
                if (!has_wild(g.pattern)) db.literal[lower(g.pattern)].push_back(g);
                else if (g.pattern.size() > 2 && g.pattern[0] == '*' && g.pattern[1] == '.' && !has_wild(g.pattern.substr(2)))
                    db.ext[lower(g.pattern.substr(2))].push_back(g);
                else db.general.push_back(g);
            });
        }
        noglobs.insert(blocked_here.begin(), blocked_here.end());
        if (read_file(dir + "/aliases", text))
            each_line(text, [&](const std::string& line) {
                size_t sp = line.find(' ');
                if (sp != std::string::npos && !db.aliases.count(line.substr(0, sp))) db.aliases[line.substr(0, sp)] = line.substr(sp + 1);
            });
        if (read_file(dir + "/subclasses", text))
            each_line(text, [&](const std::string& line) {
                size_t sp = line.find(' ');
                if (sp != std::string::npos) db.parents.emplace(line.substr(0, sp), line.substr(sp + 1));
            });
        if (read_file(dir + "/generic-icons", text))
            each_line(text, [&](const std::string& line) {
                size_t c = line.find(':');
                if (c != std::string::npos && !db.generic_icon.count(line.substr(0, c))) db.generic_icon[line.substr(0, c)] = line.substr(c + 1);
            });
    }
}

// Best of a candidate list: highest weight, then longest pattern (the spec's
// order); case-sensitive matches are kept apart because they take precedence.
struct Best { const Glob* cs = nullptr; const Glob* ci = nullptr; };

void better(const Glob*& best, const Glob& g)
{
    if (!best || g.weight > best->weight || (g.weight == best->weight && g.pattern.size() > best->pattern.size())) best = &g;
}

void consider(const Glob& g, const std::string& name, const std::string& lname, Best& best)
{
    if (g.cs) {
        if (has_wild(g.pattern) ? fnmatch(g.pattern.c_str(), name.c_str(), 0) == 0 : name == g.pattern) better(best.cs, g);
    } else {
        if (has_wild(g.pattern) ? fnmatch(lower(g.pattern).c_str(), lname.c_str(), 0) == 0 : lname == lower(g.pattern)) better(best.ci, g);
    }
}

// A few signatures from shared-mime-info's magic, for files whose name says nothing.
std::string sniff(const std::string& path)
{
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return "";
    unsigned char b[512];
    ssize_t n = read(fd, b, sizeof b);
    close(fd);
    if (n < 0) return "";
    if (n == 0) return "text/plain";   // shared-mime-info says application/x-zerosize; an empty file opens best as text
    auto starts = [&](const char* sig, size_t len, size_t off = 0) { return (size_t)n >= off + len && memcmp(b + off, sig, len) == 0; };
    if (starts("\x89PNG\r\n\x1a\n", 8)) return "image/png";
    if (starts("\xff\xd8\xff", 3)) return "image/jpeg";
    if (starts("GIF87a", 6) || starts("GIF89a", 6)) return "image/gif";
    if (starts("BM", 2) && n > 14) return "image/bmp";
    if (starts("RIFF", 4) && starts("WEBP", 4, 8)) return "image/webp";
    if (starts("%PDF-", 5)) return "application/pdf";
    if (starts("%!PS", 4)) return "application/postscript";
    if (starts("PK\x03\x04", 4)) return "application/zip";
    if (starts("\x1f\x8b", 2)) return "application/gzip";
    if (starts("BZh", 3)) return "application/x-bzip2";
    if (starts("\xfd" "7zXZ", 5)) return "application/x-xz";
    if (starts("7z\xbc\xaf\x27\x1c", 6)) return "application/x-7z-compressed";
    if (starts("ustar", 5, 257)) return "application/x-tar";
    if (starts("OggS", 4)) return "audio/ogg";
    if (starts("fLaC", 4)) return "audio/flac";
    if (starts("ID3", 3)) return "audio/mpeg";
    if (starts("RIFF", 4) && starts("WAVE", 4, 8)) return "audio/x-wav";
    if (starts("RIFF", 4) && starts("AVI ", 4, 8)) return "video/x-msvideo";
    if (starts("\x1a\x45\xdf\xa3", 4)) return "video/x-matroska";
    if (starts("ftyp", 4, 4)) return "video/mp4";
    if (starts("MZ", 2)) return "application/x-ms-dos-executable";
    if (starts("\x7f" "ELF", 4) && n >= 18) {
        int type = b[5] == 2 ? (b[16] << 8 | b[17]) : (b[17] << 8 | b[16]);
        if (type == 2) return "application/x-executable";
        if (type == 3) return "application/x-sharedlib";   // PIE executables too; see apps.cpp
        if (type == 1) return "application/x-object";
        if (type == 4) return "application/x-core";
        return "application/x-executable";
    }
    if (starts("#!", 2)) {
        std::string line((const char*)b, (size_t)n);
        line = line.substr(0, line.find('\n'));
        if (line.find("python") != std::string::npos) return "text/x-python";
        if (line.find("perl") != std::string::npos) return "application/x-perl";
        return "application/x-shellscript";
    }
    if (starts("<?xml", 5)) return "application/xml";
    if (starts("<!DOCTYPE html", 14) || starts("<html", 5)) return "text/html";
    // text: no NULs and mostly printable (UTF-8 bytes count as text)
    int bad = 0;
    for (ssize_t i = 0; i < n; i++) {
        unsigned char c = b[i];
        if (c == 0) return "application/octet-stream";
        if (c < 32 && c != '\n' && c != '\r' && c != '\t' && c != '\f' && c != 27 && c != 8) bad++;
    }
    return bad * 20 < n ? "text/plain" : "application/octet-stream";
}

} // namespace

std::string mime_canonical(const std::string& type)
{
    load();
    auto it = db.aliases.find(type);
    return it == db.aliases.end() ? type : it->second;
}

std::string mime_type_for_name(const std::string& filename)
{
    load();
    std::string lname = lower(filename);
    Best best;
    auto lit = db.literal.find(lname);
    if (lit != db.literal.end()) for (const Glob& g : lit->second) consider(g, filename, lname, best);
    if (best.cs || best.ci) return (best.cs ? best.cs : best.ci)->type;   // literal names win outright (spec)
    for (size_t i = 0; i < lname.size(); i++) {
        if (lname[i] != '.') continue;
        auto e = db.ext.find(lname.substr(i + 1));
        if (e != db.ext.end()) for (const Glob& g : e->second) consider(g, filename, lname, best);
    }
    for (const Glob& g : db.general) consider(g, filename, lname, best);
    const Glob* b = best.cs ? best.cs : best.ci;
    return b ? b->type : "";
}

std::string mime_type(const std::string& path, bool do_sniff)
{
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) return "inode/directory";
        if (S_ISCHR(st.st_mode)) return "inode/chardevice";
        if (S_ISBLK(st.st_mode)) return "inode/blockdevice";
        if (S_ISFIFO(st.st_mode)) return "inode/fifo";
        if (S_ISSOCK(st.st_mode)) return "inode/socket";
    }
    std::string t = mime_type_for_name(base_of(path));
    if (!t.empty()) return t;
    if (do_sniff) { t = sniff(path); if (!t.empty()) return t; }
    return "application/octet-stream";
}

std::vector<std::string> mime_parents(const std::string& type_in)
{
    load();
    std::vector<std::string> out;
    std::set<std::string> seen;
    std::vector<std::string> queue = { mime_canonical(type_in) };
    seen.insert(queue[0]);
    for (size_t i = 0; i < queue.size(); i++) {
        auto range = db.parents.equal_range(queue[i]);
        for (auto it = range.first; it != range.second; ++it) {
            std::string p = mime_canonical(it->second);
            if (seen.insert(p).second) { queue.push_back(p); out.push_back(p); }
        }
    }
    // implicit parents (spec): text/* is a text/plain, every stream an application/octet-stream
    std::string t = queue[0];
    if (t.compare(0, 5, "text/") == 0 && seen.insert("text/plain").second) out.push_back("text/plain");
    if (t.compare(0, 6, "inode/") != 0 && seen.insert("application/octet-stream").second) out.push_back("application/octet-stream");
    return out;
}

bool mime_is_a(const std::string& type, const std::string& ancestor)
{
    std::string a = mime_canonical(ancestor);
    if (mime_canonical(type) == a) return true;
    for (const std::string& p : mime_parents(type)) if (p == a) return true;
    return false;
}

std::string mime_comment(const std::string& type_in)
{
    load();
    std::string type = mime_canonical(type_in);
    auto c = db.comments.find(type);
    if (c != db.comments.end()) return c->second;
    std::string result;
    for (const std::string& dir : db.mime_dirs) {
        std::string text;
        if (!read_file(dir + "/" + type + ".xml", text)) continue;
        // the first <comment> without xml:lang
        size_t i = 0;
        while ((i = text.find("<comment", i)) != std::string::npos) {
            size_t close = text.find('>', i);
            if (close == std::string::npos) break;
            if (text.substr(i, close - i).find("xml:lang") == std::string::npos) {
                size_t end = text.find("</comment>", close);
                if (end != std::string::npos) result = text.substr(close + 1, end - close - 1);
                break;
            }
            i = close;
        }
        // undo the XML escapes that occur in comments
        static const char* ents[][2] = { {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"} };
        for (auto& e : ents) for (size_t k; (k = result.find(e[0])) != std::string::npos;) result.replace(k, strlen(e[0]), e[1]);
        if (!result.empty()) break;
    }
    db.comments[type] = result;
    return result;
}

std::string mime_icon_key(const std::string& type_in)
{
    load();
    std::string type = mime_canonical(type_in);
    if (type == "inode/directory") return "folder";
    if (type == "application/pdf") return "pdf";
    if (type == "text/html" || type == "application/xhtml+xml") return "html";
    std::string media = type.substr(0, type.find('/'));
    if (media == "image") return "image";
    if (media == "audio") return "audio";
    if (media == "video") return "video";
    auto g = db.generic_icon.find(type);
    std::string icon = g != db.generic_icon.end() ? g->second : "";
    if (icon == "package-x-generic") return "zip";
    if (icon == "application-x-executable") return "exe";
    if (icon == "media-optical") return "disc";
    if (icon == "text-html") return "html";
    if (icon == "image-x-generic") return "image";
    if (icon == "video-x-generic") return "video";
    if (icon == "audio-x-generic") return "audio";
    if (media == "text" || icon.compare(0, 5, "text-") == 0) return "text";
    return "";
}

} // namespace shell
