#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "config.h"
#include "fileops.h"
#include "fileops_job.h"

namespace shell {

static void set_err(std::string* err, const std::string& what) { if (err) *err = what + ": " + strerror(errno); }

static std::string absolute(const std::string& p)
{
    if (!p.empty() && p[0] == '/') return p;
    char buf[4096];
    if (!getcwd(buf, sizeof buf)) return p;
    return std::string(buf) + "/" + p;
}

// the mount point holding path (walk up while the device stays the same)
static std::string topdir_of(const std::string& path, dev_t dev)
{
    std::string cur = dir_of(path);
    for (;;) {
        std::string up = dir_of(cur);
        struct stat st;
        if (cur == "/" || stat(up.c_str(), &st) != 0 || st.st_dev != dev) return cur;
        cur = up;
    }
}

std::string trash_dir_for(const std::string& path_in)
{
    std::string path = absolute(path_in);
    struct stat st, hs;
    std::string home_trash = data_home() + "/Trash";
    if (lstat(path.c_str(), &st) != 0) return home_trash;
    std::string dh = data_home();
    make_dirs(dh);
    if (stat(dh.c_str(), &hs) != 0 || hs.st_dev == st.st_dev) return home_trash;
    std::string top = topdir_of(path, st.st_dev);
    // $topdir/.Trash/$uid if .Trash is a sticky, non-symlink directory; else $topdir/.Trash-$uid
    char uid[32]; snprintf(uid, sizeof uid, "%u", (unsigned)getuid());
    struct stat ts;
    std::string shared = top + "/.Trash";
    if (lstat(shared.c_str(), &ts) == 0 && S_ISDIR(ts.st_mode) && (ts.st_mode & S_ISVTX)) return shared + "/" + uid;
    return (top == "/" ? std::string() : top) + "/.Trash-" + uid;
}

static std::string iso_now()
{
    time_t t = time(nullptr);
    struct tm tm; localtime_r(&t, &tm);
    char buf[32];
    strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", &tm);
    return buf;
}

bool trash(const std::string& path_in, std::string* err)
{
    std::string path = absolute(path_in);
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) { set_err(err, path); return false; }
    std::string dir = trash_dir_for(path);
    bool home = dir == data_home() + "/Trash";
    if (!make_dirs(dir + "/files") || !make_dirs(dir + "/info")) { set_err(err, dir); return false; }
    // the info file reserves the name (O_EXCL), then the rename moves the file in
    std::string base = base_of(path), name = base, info;
    int fd = -1;
    for (int n = 1; n < 10000; n++) {
        if (n > 1) {
            size_t dot = base.find_last_of('.');
            name = dot == std::string::npos || dot == 0 ? base + "." + std::to_string(n) : base.substr(0, dot) + "." + std::to_string(n) + base.substr(dot);
        }
        info = dir + "/info/" + name + ".trashinfo";
        struct stat fs;
        if (lstat((dir + "/files/" + name).c_str(), &fs) == 0) continue;
        fd = open(info.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd >= 0 || errno != EEXIST) break;
    }
    if (fd < 0) { set_err(err, info); return false; }
    // Path: absolute for the home trash, relative to the top directory otherwise (spec)
    std::string recorded = path;
    if (!home) {
        std::string top = dir_of(dir);
        if (dir.find("/.Trash/") != std::string::npos) top = dir_of(top);
        if (path.compare(0, top.size() + 1, top + "/") == 0) recorded = path.substr(top.size() + 1);
    }
    std::string enc = uri_from_path(recorded).substr(7);   // percent-encoded, without the scheme
    std::string text = "[Trash Info]\nPath=" + enc + "\nDeletionDate=" + iso_now() + "\n";
    bool wrote = write(fd, text.data(), text.size()) == (ssize_t)text.size();
    close(fd);
    if (!wrote || rename(path.c_str(), (dir + "/files/" + name).c_str()) != 0) {
        set_err(err, "cannot move " + base + " to " + dir);
        unlink(info.c_str());
        return false;
    }
    return true;
}

bool rename_item(const std::string& path, const std::string& new_name, std::string* new_path, std::string* err)
{
    if (new_name.empty() || new_name.find('/') != std::string::npos || new_name == "." || new_name == "..") {
        if (err) *err = "A file name cannot be empty or contain a slash.";
        return false;
    }
    std::string to = dir_of(path) + "/" + new_name;
    if (to == path) { if (new_path) *new_path = to; return true; }
    struct stat st;
    if (lstat(to.c_str(), &st) == 0) { if (err) *err = "A file named \"" + new_name + "\" already exists."; return false; }
    if (rename(path.c_str(), to.c_str()) != 0) { set_err(err, "cannot rename " + base_of(path)); return false; }
    if (new_path) *new_path = to;
    return true;
}

std::string unique_name(const std::string& dir, const std::string& base, const std::string& ext)
{
    struct stat st;
    std::string name = base + ext;
    for (int n = 2; lstat((dir + "/" + name).c_str(), &st) == 0; n++) name = base + " (" + std::to_string(n) + ")" + ext;
    return name;
}

bool new_folder(const std::string& dir, std::string* path, std::string* err)
{
    std::string p = dir + "/" + unique_name(dir, "New Folder", "");
    if (mkdir(p.c_str(), 0777) != 0) { set_err(err, p); return false; }
    if (path) *path = p;
    return true;
}

bool new_text_file(const std::string& dir, std::string* path, std::string* err)
{
    std::string p = dir + "/" + unique_name(dir, "New Text Document", ".txt");
    int fd = open(p.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (fd < 0) { set_err(err, p); return false; }
    close(fd);
    if (path) *path = p;
    return true;
}

bool paste_items(const std::vector<std::string>& sources, const std::string& dest_dir, bool move, std::string* err)
{
    fileops::Result r = fileops::run(move ? fileops::Op::Move : fileops::Op::Copy, sources, dest_dir);
    if (!r.ok && err) *err = r.summary();
    return r.ok;
}

bool same_filesystem(const std::string& a, const std::string& b)
{
    struct stat sa, sb;
    return stat(a.c_str(), &sa) == 0 && stat(b.c_str(), &sb) == 0 && sa.st_dev == sb.st_dev;
}

uint32_t drop_effect(const std::vector<std::string>& paths, const std::string& dest_dir, bool shift, bool ctrl, uint32_t allowed)
{
    if (paths.empty() || dest_dir.empty() || access(dest_dir.c_str(), W_OK) != 0) return 0;
    bool all_here = true;
    struct stat ds;
    if (stat(dest_dir.c_str(), &ds) != 0) return 0;
    for (const std::string& p : paths) {
        struct stat ls;
        if (lstat(p.c_str(), &ls) == 0 && S_ISDIR(ls.st_mode) && fileops::is_inside(dest_dir, p)) return 0;   // into itself (device+inode)
        struct stat ps;
        std::string parent = dir_of(p);
        if (stat(parent.c_str(), &ps) != 0 || ps.st_dev != ds.st_dev || ps.st_ino != ds.st_ino) all_here = false;
    }
    uint32_t e = ctrl && shift ? EffectLink : (ctrl || shift) ? EffectCopy : same_filesystem(dir_of(paths[0]), dest_dir) ? EffectMove : EffectCopy;
    if (!(e & allowed)) e = allowed & EffectCopy ? EffectCopy : allowed & EffectMove ? EffectMove : allowed & EffectLink ? (uint32_t)EffectLink : 0u;
    if ((e == EffectMove || e == EffectLink) && all_here) return 0;   // nothing to do (XP shows "no")
    return e;
}

bool link_items(const std::vector<std::string>& sources, const std::string& dest_dir, std::string* err)
{
    bool ok = true;
    for (const std::string& src : sources) {
        std::string name = base_of(src), to = dest_dir + "/" + name;
        struct stat st;
        for (int n = 1; lstat(to.c_str(), &st) == 0; n++)
            to = dest_dir + "/Shortcut" + (n > 1 ? " (" + std::to_string(n) + ")" : std::string()) + " to " + name;
        if (symlink(src.c_str(), to.c_str()) != 0) { set_err(err, "cannot link " + name); ok = false; }
    }
    return ok;
}

bool drop_items(const std::vector<std::string>& sources, const std::string& dest_dir, uint32_t effect, std::string* err)
{
    if (effect == EffectLink) return link_items(sources, dest_dir, err);
    if (effect == EffectMove || effect == EffectCopy) return paste_items(sources, dest_dir, effect == EffectMove, err);
    return false;
}

static void walk(const std::string& p, FileInfo& fi, int depth, int& budget)
{
    DIR* d = opendir(p.c_str());
    if (!d) return;
    while (struct dirent* e = readdir(d)) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (--budget < 0) break;
        std::string c = p + "/" + e->d_name;
        struct stat st;
        if (lstat(c.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { fi.folders++; if (depth < 64) walk(c, fi, depth + 1, budget); }
        else { fi.files++; fi.size += (uint64_t)st.st_size; }
    }
    closedir(d);
}

FileInfo file_info(const std::string& path, bool deep)
{
    FileInfo fi;
    struct stat ls, st;
    if (lstat(path.c_str(), &ls) != 0) return fi;
    fi.ok = true;
    fi.is_link = S_ISLNK(ls.st_mode);
    if (fi.is_link) {
        char buf[4096];
        ssize_t n = readlink(path.c_str(), buf, sizeof buf - 1);
        if (n >= 0) { buf[n] = 0; fi.link_target = buf; }
    }
    if (stat(path.c_str(), &st) != 0) st = ls;
    fi.is_dir = S_ISDIR(st.st_mode);
    fi.size = fi.is_dir ? 0 : (uint64_t)st.st_size;
    fi.mtime = st.st_mtime; fi.atime = st.st_atime; fi.ctime = st.st_ctime;
    char p[11] = "----------";
    p[0] = S_ISDIR(st.st_mode) ? 'd' : S_ISLNK(ls.st_mode) ? 'l' : S_ISCHR(st.st_mode) ? 'c' : S_ISBLK(st.st_mode) ? 'b' : S_ISFIFO(st.st_mode) ? 'p' : S_ISSOCK(st.st_mode) ? 's' : '-';
    static const mode_t bits[9] = { S_IRUSR, S_IWUSR, S_IXUSR, S_IRGRP, S_IWGRP, S_IXGRP, S_IROTH, S_IWOTH, S_IXOTH };
    for (int i = 0; i < 9; i++) if (st.st_mode & bits[i]) p[i + 1] = "rwxrwxrwx"[i];
    if (st.st_mode & S_ISUID) p[3] = (st.st_mode & S_IXUSR) ? 's' : 'S';
    if (st.st_mode & S_ISGID) p[6] = (st.st_mode & S_IXGRP) ? 's' : 'S';
    if (st.st_mode & S_ISVTX) p[9] = (st.st_mode & S_IXOTH) ? 't' : 'T';
    fi.perms = p;
    if (struct passwd* pw = getpwuid(st.st_uid)) fi.owner = pw->pw_name; else fi.owner = std::to_string(st.st_uid);
    if (struct group* gr = getgrgid(st.st_gid)) fi.group = gr->gr_name; else fi.group = std::to_string(st.st_gid);
    if (fi.is_dir && deep) { int budget = 200000; walk(path, fi, 0, budget); }
    return fi;
}

} // namespace shell
