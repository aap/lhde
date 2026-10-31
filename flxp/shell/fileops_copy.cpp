// The copy/move/delete engine (see fileops_job.h).
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <algorithm>
#include "config.h"
#include "fileops.h"
#include "fileops_job.h"

namespace shell {
namespace fileops {

namespace {

struct Id {
    dev_t dev = 0; ino_t ino = 0;
    bool operator==(const Id& o) const { return dev == o.dev && ino == o.ino; }
};
Id id_of(const struct stat& st) { Id i; i.dev = st.st_dev; i.ino = st.st_ino; return i; }

std::string absolute(const std::string& p)
{
    if (!p.empty() && p[0] == '/') return p;
    char buf[PATH_MAX];
    if (!getcwd(buf, sizeof buf)) return p;
    return std::string(buf) + "/" + p;
}

std::string strip_slashes(std::string p)
{
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

std::string real(const std::string& p)
{
    char* r = realpath(p.c_str(), nullptr);
    if (!r) return std::string();
    std::string s = r;
    free(r);
    return s;
}

// A source as (physical parent, name): trailing slashes dropped, the parent
// resolved, "." and ".." resolved as directories. The last component itself
// is not followed (a symlink stays the link).
bool split_source(const std::string& in, std::string* parent, std::string* name)
{
    std::string p = strip_slashes(absolute(in));
    std::string base = base_of(p);
    if (base == "." || base == "..") { p = real(p); if (p.empty()) return false; base = base_of(p); }
    if (p == "/" || base.empty()) { errno = EINVAL; return false; }
    std::string dir = real(dir_of(p));
    if (dir.empty()) return false;
    *parent = dir;
    *name = base;
    return true;
}

// Is anc the directory path resolves to, or one of its physical ancestors?
// Walks ".." with O_PATH descriptors: crosses mount points the way the kernel
// does, so a bind mount of anc further down is found too.
bool chain_contains(const std::string& path, const Id& anc)
{
    int fd = open(path.c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return false;
    bool found = false;
    for (int depth = 0; depth < 4096; depth++) {
        struct stat st;
        if (fstat(fd, &st) != 0) break;
        if (id_of(st) == anc) { found = true; break; }
        int up = openat(fd, "..", O_PATH | O_DIRECTORY | O_CLOEXEC);
        if (up < 0) break;
        struct stat us;
        bool root = fstat(up, &us) != 0 || id_of(us) == id_of(st);
        close(fd);
        fd = up;
        if (root) break;
    }
    close(fd);
    return found;
}

// XP's copy.c _HandleRename with PathYetAnotherMakeUniqueName: the template
// "Copy () of x" tried as "Copy of x", then "Copy (2) of x", "Copy (3) of x";
// a name that already is "Copy (n) of x" is its own template ("Copy (n+..) of x",
// from 2), while "Copy of x" becomes "Copy of Copy of x".
std::string copy_of_name(const std::string& dir, const std::string& base)
{
    struct stat st;
    auto taken = [&](const std::string& n) { return lstat((dir + "/" + n).c_str(), &st) == 0; };
    std::string rest = base;
    bool numbered = false;
    if (base.compare(0, 6, "Copy (") == 0) {
        size_t i = 6;
        while (i < base.size() && base[i] >= '0' && base[i] <= '9') i++;
        if (i > 6 && base.compare(i, 5, ") of ") == 0) { rest = base.substr(i + 5); numbered = true; }
    }
    std::string name = "Copy of " + rest;
    if (!numbered && !taken(name)) return name;
    for (int n = 2; ; n++) {
        name = "Copy (" + std::to_string(n) + ") of " + rest;
        if (!taken(name)) return name;
    }
}

bool user_xattr(const char* name) { return !strncmp(name, "user.", 5); }
bool unsupported(int e) { return e == ENOTSUP || e == EOPNOTSUPP || e == ENOSYS; }

struct Moved { std::string path; bool dir; Id id; off_t size; struct timespec mtime; };

struct Engine {
    Op op;
    const Options& o;
    Result& r;
    Progress prog;
    enum { AskFile = 1, AskFolder = 2, AskMove = 4, AskReadOnly = 8 };
    unsigned asks = ~0u, no_all = 0;    // see ask()
    bool stop = false;                  // cancelled (flag or a Cancel answer)
    bool root = geteuid() == 0;
    // the item in progress
    std::vector<std::string> created;   // creation order
    std::vector<Moved> moved;           // sources copied by a cross-fs move, pre-order
    int skipped = 0;
    size_t nsources = 0;                // top-level items in the operation
    std::vector<Id> stack;              // directories being copied (loop guard)
    char* buf = nullptr;                // read/write fallback buffer
    enum { Chunk = 1 << 20 };

    Engine(Op op, const Options& o, Result& r) : op(op), o(o), r(r) {}
    ~Engine() { free(buf); }

    // dest: the failure is on the copy's side (XP: "Cannot create or replace x")
    void fail(const std::string& what, const std::string& path, int code, Reason reason = Reason::None)
    {
        Error e; e.what = what; e.path = path; e.code = code; e.reason = reason;
        for (const char* d : { "Cannot create", "Cannot write", "Cannot set", "Cannot open" })
            if (what.compare(0, strlen(d), d) == 0) e.dest = true;
        r.errors.push_back(e);
    }
    bool cancelled()
    {
        if (!stop && o.cancel && o.cancel->load()) stop = true;
        return stop;
    }
    void report() { if (o.progress) o.progress(prog); }
    void item_begin(const std::string& src) { prog.current = src; report(); }
    void item_end() { prog.items_done++; report(); }

    void skip(const std::string& src)   // left where it is: done as far as progress goes
    {
        skipped++;
        if (!o.progress) return;
        Totals t = count(std::vector<std::string>{ src });
        prog.items_done += t.items; prog.bytes_done += t.bytes;
        report();
    }

    // XP's confirmation state (copy.c ConfirmFileOp and SetConfirmMaskAndText):
    // what a question needs still asked, what a Yes / Yes to All takes off for
    // the rest of the operation, what No to All (Shift+No) answers from now on.
    // A Yes to replace a folder means its files are replaced without asking.
    Answer ask(const Conflict& c)
    {
        unsigned need, yes = 0, yes_all;
        if (c.kind == Conflict::MoveReadOnly) { need = AskReadOnly; yes_all = AskMove | AskReadOnly; }
        else if (c.folder) { need = AskFolder; yes = AskFile; yes_all = AskFile | AskFolder; }
        else if (c.dst_readonly) { need = AskReadOnly; yes_all = AskFile | AskReadOnly; }
        else { need = AskFile; yes_all = AskFile; }
        if (!(asks & need)) return Answer::Yes;
        if ((no_all & need) == need) return Answer::No;
        Answer a = o.confirm(c);
        switch (a) {
        case Answer::Yes: case Answer::Retry: asks &= ~yes; return Answer::Yes;
        case Answer::YesToAll: asks &= ~yes_all; return Answer::Yes;
        case Answer::NoToAll: no_all |= yes_all; return Answer::No;
        default: return a;
        }
    }

    // a fresh "name.flxp-part" next to dst, opened or made with make(); recorded
    std::string part_name(const std::string& dst, int n) { return dst + ".flxp-part" + (n > 1 ? "." + std::to_string(n) : std::string()); }

    // put part in place as dst: replace (dst existed and the user said Yes) or never clobber
    bool place(const std::string& part, const std::string& dst, bool replace)
    {
        int rc = replace ? rename(part.c_str(), dst.c_str()) : renameat2(AT_FDCWD, part.c_str(), AT_FDCWD, dst.c_str(), RENAME_NOREPLACE);
        if (rc != 0 && !replace && errno == EINVAL) {   // the filesystem has no RENAME_NOREPLACE
            struct stat st;
            if (lstat(dst.c_str(), &st) == 0) errno = EEXIST;
            else rc = rename(part.c_str(), dst.c_str());
        }
        if (rc != 0) { fail("Cannot create", dst, errno); return false; }
        // the part entry (the newest) becomes dst, unless dst existed before
        if (!created.empty() && created.back() == part) {
            if (replace) created.pop_back(); else created.back() = dst;
        }
        return true;
    }

    void copy_xattrs(int in, int out, const std::string& src, const std::string& dst, bool link, bool* ok)
    {
        ssize_t n = in >= 0 ? flistxattr(in, nullptr, 0) : llistxattr(src.c_str(), nullptr, 0);
        if (n <= 0) return;
        std::vector<char> names((size_t)n + 1);
        n = in >= 0 ? flistxattr(in, names.data(), names.size()) : llistxattr(src.c_str(), names.data(), names.size());
        if (n <= 0) return;
        std::vector<char> val;
        for (const char* nm = names.data(); nm < names.data() + n; nm += strlen(nm) + 1) {
            ssize_t vn = in >= 0 ? fgetxattr(in, nm, nullptr, 0) : lgetxattr(src.c_str(), nm, nullptr, 0);
            if (vn < 0) continue;
            val.resize((size_t)vn + 1);
            vn = in >= 0 ? fgetxattr(in, nm, val.data(), val.size()) : lgetxattr(src.c_str(), nm, val.data(), val.size());
            if (vn < 0) continue;
            int rc = out >= 0 ? fsetxattr(out, nm, val.data(), (size_t)vn, 0) : lsetxattr(dst.c_str(), nm, val.data(), (size_t)vn, 0);
            if (rc == 0 || !user_xattr(nm)) continue;   // security./trusted./system.: best effort
            // user.* on a filesystem without them, or on a symlink (EPERM): skipped
            if (unsupported(errno) || (link && errno == EPERM)) continue;
            fail("Cannot copy the extended attributes of", src, errno);
            *ok = false;
            return;
        }
    }

    // owner, xattrs, mode, times: on fd (out >= 0) or the path (symlinks)
    bool apply_meta(int in, int out, const std::string& src, const std::string& dst, const struct stat& ss)
    {
        bool ok = true, link = S_ISLNK(ss.st_mode);
        uid_t uid = root ? ss.st_uid : (uid_t)-1;
        if (out >= 0) { if (fchown(out, uid, ss.st_gid) != 0 && root) fchown(out, uid, (gid_t)-1); }
        else { if (lchown(dst.c_str(), uid, ss.st_gid) != 0 && root) lchown(dst.c_str(), uid, (gid_t)-1); }
        copy_xattrs(in, out, src, dst, link, &ok);
        if (!link) {
            mode_t m = ss.st_mode & 07777;
            if (fchmod(out, m) != 0 && (errno != EPERM || fchmod(out, m & 01777) != 0)) { fail("Cannot set the permissions of", dst, errno); ok = false; }
        }
        struct timespec ts[2] = { ss.st_atim, ss.st_mtim };
        int rc = out >= 0 ? futimens(out, ts) : utimensat(AT_FDCWD, dst.c_str(), ts, AT_SYMLINK_NOFOLLOW);
        if (rc != 0 && !(link && unsupported(errno))) { fail("Cannot set the times of", dst, errno); ok = false; }
        return ok;
    }

    // copy [from, to) of in to the same offsets of out
    bool copy_range(int in, int out, off_t from, off_t to, bool* use_cfr, const std::string& src, const std::string& dst, uint64_t* written)
    {
        off_t pos = from;
        while (pos < to) {
            if (cancelled()) return false;
            size_t len = (size_t)std::min<off_t>(to - pos, Chunk);
            ssize_t n = -1;
            if (*use_cfr) {
                loff_t ip = pos, op = pos;
                n = copy_file_range(in, &ip, out, &op, len, 0);
                if (n < 0 && (errno == EXDEV || errno == EINVAL || unsupported(errno) || errno == EBADF || errno == EPERM)) { *use_cfr = false; continue; }
                if (n < 0) { fail("Cannot copy", src, errno); return false; }
            } else {
                if (!buf && !(buf = (char*)malloc(Chunk))) { fail("Cannot copy", src, ENOMEM); return false; }
                n = pread(in, buf, len, pos);
                if (n < 0) { if (errno == EINTR) continue; fail("Cannot read", src, errno); return false; }
                for (ssize_t w = 0; w < n; ) {
                    ssize_t k = pwrite(out, buf + w, (size_t)(n - w), pos + w);
                    if (k < 0 && errno == EINTR) continue;
                    if (k <= 0) { fail("Cannot write", dst, k < 0 ? errno : EIO); return false; }
                    w += k;
                }
            }
            if (n == 0) break;   // the source got shorter: caught by the size check
            pos += n;
            *written += (uint64_t)n;
            prog.bytes_done += (uint64_t)n;
            report();
        }
        return true;
    }

    bool copy_file(const std::string& src, const std::string& dst, const struct stat& ls, bool replace)
    {
        int in = open(src.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NOCTTY);
        if (in < 0) { fail("Cannot read", src, errno); return false; }
        struct stat ss;
        if (fstat(in, &ss) != 0) { fail("Cannot read", src, errno); close(in); return false; }
        if (!(id_of(ss) == id_of(ls)) || !S_ISREG(ss.st_mode)) { fail("Changed while being copied:", src, ESTALE, Reason::Changed); close(in); return false; }
        std::string part;
        int out = -1;
        for (int n = 1; n < 100 && out < 0; n++) {
            part = part_name(dst, n);
            out = open(part.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
            if (out < 0 && errno != EEXIST) break;
        }
        if (out < 0) { fail("Cannot create", dst, errno); close(in); return false; }
        created.push_back(part);
        bool use_cfr = true, ok = true;
        uint64_t written = 0;
        off_t size = ss.st_size, off = 0;
        // data segments only (holes stay holes); SEEK_DATA unsupported: one segment
        while (ok && off < size) {
            off_t data = lseek(in, off, SEEK_DATA);
            if (data < 0) {
                if (errno == ENXIO) break;      // only a hole is left
                data = off;
            }
            off_t hole = data < size ? lseek(in, data, SEEK_HOLE) : size;
            if (hole < 0 || hole > size) hole = size;
            if (hole <= data) hole = size;
            ok = copy_range(in, out, data, hole, &use_cfr, src, dst, &written);
            off = hole;
        }
        if (ok && ftruncate(out, size) != 0) { fail("Cannot write", dst, errno); ok = false; }
        // verify: the source still has the size that was copied
        struct stat after;
        if (ok && (fstat(in, &after) != 0 || after.st_size != size)) { fail("Changed while being copied:", src, EAGAIN, Reason::Changed); ok = false; }
        prog.bytes_done += (uint64_t)size - std::min<uint64_t>((uint64_t)size, written);   // the holes
        if (ok) ok = apply_meta(in, out, src, dst, ss);
        close(in);
        if (close(out) != 0 && ok) { fail("Cannot write", dst, errno); ok = false; }
        return ok && place(part, dst, replace);
    }

    bool copy_link(const std::string& src, const std::string& dst, const struct stat& ss, bool replace)
    {
        std::vector<char> t((size_t)std::max<off_t>(ss.st_size, 255) + 2);
        ssize_t n;
        for (;;) {
            n = readlink(src.c_str(), t.data(), t.size());
            if (n < 0) { fail("Cannot read", src, errno); return false; }
            if ((size_t)n < t.size()) break;
            t.resize(t.size() * 2);
        }
        t[(size_t)n] = 0;
        std::string part;
        int rc = -1;
        for (int k = 1; k < 100 && rc != 0; k++) {
            part = part_name(dst, k);
            rc = symlink(t.data(), part.c_str());
            if (rc != 0 && errno != EEXIST) break;
        }
        if (rc != 0) { fail("Cannot create", dst, errno); return false; }
        created.push_back(part);
        return apply_meta(-1, -1, src, part, ss) && place(part, dst, replace);
    }

    bool copy_special(const std::string& src, const std::string& dst, const struct stat& ss, bool replace)
    {
        if (S_ISSOCK(ss.st_mode)) { fail("Cannot copy (a socket)", src, ENOTSUP); return false; }
        std::string part;
        int rc = -1;
        for (int k = 1; k < 100 && rc != 0; k++) {
            part = part_name(dst, k);
            rc = mknod(part.c_str(), (ss.st_mode & S_IFMT) | 0600, ss.st_rdev);
            if (rc != 0 && errno != EEXIST) break;
        }
        if (rc != 0) { fail("Cannot create", dst, errno); return false; }
        created.push_back(part);
        int fd = open(part.c_str(), O_PATH | O_NOFOLLOW | O_CLOEXEC);
        bool ok = fd >= 0;
        if (ok) {
            // O_PATH descriptors take neither fchmod nor xattrs: go through /proc/self/fd
            std::string pp = "/proc/self/fd/" + std::to_string(fd);
            uid_t uid = root ? ss.st_uid : (uid_t)-1;
            if (chown(pp.c_str(), uid, ss.st_gid) != 0 && root) { if (chown(pp.c_str(), uid, (gid_t)-1)) {} }
            if (chmod(pp.c_str(), ss.st_mode & 07777) != 0) { fail("Cannot set the permissions of", dst, errno); ok = false; }
            struct timespec ts[2] = { ss.st_atim, ss.st_mtim };
            if (ok && utimensat(AT_FDCWD, pp.c_str(), ts, 0) != 0) { fail("Cannot set the times of", dst, errno); ok = false; }
            close(fd);
        }
        return ok && place(part, dst, replace);
    }

    bool copy_dir(const std::string& src, const std::string& dst, const struct stat& ss, bool merge)
    {
        if (std::find(stack.begin(), stack.end(), id_of(ss)) != stack.end()) { fail("Cannot copy (a folder loop)", src, ELOOP); return false; }
        int sfd = open(src.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (sfd < 0) { fail("Cannot read", src, errno); return false; }
        DIR* d = fdopendir(sfd);
        if (!d) { fail("Cannot read", src, errno); close(sfd); return false; }
        std::vector<std::string> names;   // all first: the listing is not changed under us
        errno = 0;
        while (struct dirent* e = readdir(d)) {
            if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) names.push_back(e->d_name);
            errno = 0;
        }
        int rerr = errno;
        if (rerr) { fail("Cannot read", src, rerr); closedir(d); return false; }
        if (!merge) {
            if (mkdir(dst.c_str(), 0700) != 0) { fail("Cannot create", dst, errno); closedir(d); return false; }
            created.push_back(dst);
        }
        if (op == Op::Move) { Moved m; m.path = src; m.dir = true; m.id = id_of(ss); m.size = 0; m.mtime = ss.st_mtim; moved.push_back(m); }
        item_end();
        stack.push_back(id_of(ss));
        bool ok = true;
        std::sort(names.begin(), names.end());
        for (const std::string& n : names) {
            if (cancelled() || !(ok = transfer(src + "/" + n, dst, n, false))) break;
        }
        stack.pop_back();
        if (ok && !stop && !merge) {   // a merged folder keeps its own attributes
            int dfd = open(dst.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (dfd < 0) { fail("Cannot open", dst, errno); ok = false; }
            else { ok = apply_meta(dirfd(d), dfd, src, dst, ss); close(dfd); }
        }
        closedir(d);
        return ok && !stop;
    }

    bool copy_entry(const std::string& src, const std::string& dst, const struct stat& ss, bool exists)
    {
        if (S_ISDIR(ss.st_mode)) return copy_dir(src, dst, ss, exists);
        bool ok = S_ISREG(ss.st_mode) ? copy_file(src, dst, ss, exists)
                : S_ISLNK(ss.st_mode) ? copy_link(src, dst, ss, exists)
                : copy_special(src, dst, ss, exists);
        if (ok && op == Op::Move) { Moved m; m.path = src; m.dir = false; m.id = id_of(ss); m.size = ss.st_size; m.mtime = ss.st_mtim; moved.push_back(m); }
        if (ok) item_end();
        return ok;
    }

    // src (lstat ss) to dst_dir/name; false: an error (recorded) or stop
    bool transfer(const std::string& src, const std::string& dst_dir, std::string name, bool top, std::string* final_dst = nullptr)
    {
        struct stat ss, ds;
        if (lstat(src.c_str(), &ss) != 0) { fail(op == Op::Move ? "Cannot move" : "Cannot copy", src, errno); return false; }
        prog.current = src;
        prog.current_dst = dst_dir + "/" + name;
        if (top) report();
        std::string dst = dst_dir + "/" + name;
        bool exists = lstat(dst.c_str(), &ds) == 0;
        if (exists && id_of(ss) == id_of(ds)) {          // onto itself
            if (top && op == Op::Move && o.confirm) { fail("Cannot move", src, 0, Reason::SameFile); return false; }
            if (!(top && op == Op::Copy)) { skip(src); if (final_dst) *final_dst = dst; return true; }
            name = copy_of_name(dst_dir, name);
            dst = dst_dir + "/" + name;
            exists = false;
        }
        if (exists && !o.confirm) {
            if (!top) { fail("Cannot create", dst, EEXIST); return false; }
            name = copy_of_name(dst_dir, name);
            dst = dst_dir + "/" + name;
            exists = false;
        }
        if (exists) {
            bool sdir = S_ISDIR(ss.st_mode), ddir = S_ISDIR(ds.st_mode);
            if (sdir != ddir) {
                fail(ddir ? "A folder with that name exists:" : "A file with that name exists:", dst, ddir ? EISDIR : ENOTDIR,
                     ddir ? Reason::FileOntoFolder : Reason::FolderOntoFile);
                return false;
            }
            Conflict c;
            c.src = src; c.dst = dst; c.folder = sdir; c.only = top && nsources == 1;
            c.dst_readonly = !ddir && access(dst.c_str(), W_OK) != 0 && errno == EACCES;
            c.src_size = (uint64_t)ss.st_size; c.dst_size = (uint64_t)ds.st_size;
            c.src_mtime = ss.st_mtime; c.dst_mtime = ds.st_mtime;
            Answer a = ask(c);
            if (a == Answer::Cancel) { stop = true; return false; }
            if (a == Answer::No) { skip(src); return true; }
            if (sdir && chain_contains(src, id_of(ds))) { fail("Cannot merge a folder into one that contains it:", src, EINVAL); return false; }
        }
        // XP's Confirm File Move: a read-only file (no write permission at all) moved, asked once per file
        if (op == Op::Move && o.confirm && S_ISREG(ss.st_mode) && !(ss.st_mode & 0222)) {
            Conflict c;
            c.kind = Conflict::MoveReadOnly; c.only = top && nsources == 1;
            c.src = src; c.dst = dst;
            c.src_size = (uint64_t)ss.st_size; c.src_mtime = ss.st_mtime;
            Answer a = ask(c);
            if (a == Answer::Cancel) { stop = true; return false; }
            if (a == Answer::No) { skip(src); return true; }
        }
        prog.current_dst = dst;
        if (final_dst) *final_dst = dst;
        if (op == Op::Move) {
            if (exists && S_ISDIR(ss.st_mode)) {         // merge: entry by entry, then the emptied folder
                std::vector<std::string> names;
                if (DIR* d = opendir(src.c_str())) {
                    while (struct dirent* e = readdir(d)) if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) names.push_back(e->d_name);
                    closedir(d);
                } else { fail("Cannot read", src, errno); return false; }
                std::sort(names.begin(), names.end());
                int skipped_before = skipped;
                size_t moved_before = moved.size();
                Moved self; self.path = src; self.dir = true; self.id = id_of(ss); self.size = 0; self.mtime = ss.st_mtim;
                for (const std::string& n : names)
                    if (cancelled() || !transfer(src + "/" + n, dst, n, false)) return false;
                prog.items_done++;
                if (skipped == skipped_before) {
                    if (moved.size() == moved_before) {   // all renamed: the folder can go now
                        if (rmdir(src.c_str()) != 0) { fail("Cannot remove the original of", src, errno); return false; }
                    } else moved.insert(moved.begin() + (long)moved_before, self);   // after the copied ones are removed
                }
                return true;
            }
            int rc = exists ? rename(src.c_str(), dst.c_str()) : renameat2(AT_FDCWD, src.c_str(), AT_FDCWD, dst.c_str(), RENAME_NOREPLACE);
            if (rc != 0 && !exists && errno == EINVAL) {
                // RENAME_NOREPLACE unsupported here (or a folder into itself, refused before): the plain rename decides
                struct stat t;
                if (lstat(dst.c_str(), &t) == 0) errno = EEXIST; else rc = rename(src.c_str(), dst.c_str());
            }
            if (rc == 0) {
                if (o.progress) {
                    Totals t = count(std::vector<std::string>{ dst });
                    prog.items_done += t.items; prog.bytes_done += t.bytes;
                    report();
                }
                return true;
            }
            if (errno != EXDEV) { fail("Cannot move", src, errno); return false; }
        }
        return copy_entry(src, dst, ss, exists);
    }

    // remove what the failed item created, newest first; a folder we made
    // read-only gets write permission back first
    void rollback()
    {
        for (size_t i = created.size(); i-- > 0; ) {
            const std::string& p = created[i];
            struct stat st;
            if (lstat(p.c_str(), &st) != 0) continue;
            bool dir = S_ISDIR(st.st_mode);
            int rc = dir ? rmdir(p.c_str()) : unlink(p.c_str());
            if (rc != 0 && (errno == EACCES || errno == EPERM)) {
                std::string parent = dir_of(p);
                if (std::find(created.begin(), created.end(), parent) != created.end() && chmod(parent.c_str(), 0700) == 0)
                    rc = dir ? rmdir(p.c_str()) : unlink(p.c_str());
            }
            if (rc != 0) fail("Cannot remove the partial copy", p, errno);
        }
        created.clear();
    }

    // a cross-fs move: the copies are complete; remove the originals, children
    // first, each only if it is still the file that was copied
    bool remove_moved()
    {
        bool ok = true;
        for (size_t i = moved.size(); i-- > 0; ) {
            const Moved& m = moved[i];
            struct stat st;
            if (lstat(m.path.c_str(), &st) != 0) { fail("Cannot remove the original of", m.path, errno); ok = false; continue; }
            if (!(id_of(st) == m.id) || (!m.dir && (st.st_size != m.size || st.st_mtim.tv_sec != m.mtime.tv_sec || st.st_mtim.tv_nsec != m.mtime.tv_nsec))) {
                fail("Not removed (changed after it was copied):", m.path, EAGAIN); ok = false; continue;
            }
            int rc = m.dir ? rmdir(m.path.c_str()) : unlink(m.path.c_str());
            if (rc != 0 && m.dir && (errno == ENOTEMPTY || errno == EEXIST) && skipped) continue;   // kept "No" items inside
            if (rc != 0) { fail("Cannot remove the original of", m.path, errno); ok = false; }
        }
        moved.clear();
        return ok;
    }
};

// rm -r that stays on the item's filesystem and collects errors
bool remove_tree(const std::string& p, dev_t dev, bool top, Result& r)
{
    struct stat st;
    if (lstat(p.c_str(), &st) != 0) { Error e; e.what = "Cannot delete"; e.path = p; e.code = errno; r.errors.push_back(e); return false; }
    if (!top && st.st_dev != dev) { Error e; e.what = "Not deleted (another filesystem is mounted there):"; e.path = p; e.code = EXDEV; r.errors.push_back(e); return false; }
    bool ok = true;
    if (S_ISDIR(st.st_mode)) {
        std::vector<std::string> names;
        if (DIR* d = opendir(p.c_str())) {
            while (struct dirent* e = readdir(d)) if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) names.push_back(e->d_name);
            closedir(d);
        }
        for (const std::string& n : names) if (!remove_tree(p + "/" + n, st.st_dev, false, r)) ok = false;
        if (ok && rmdir(p.c_str()) != 0) { Error e; e.what = "Cannot delete"; e.path = p; e.code = errno; r.errors.push_back(e); ok = false; }
        return ok;
    }
    if (unlink(p.c_str()) != 0) { Error e; e.what = "Cannot delete"; e.path = p; e.code = errno; r.errors.push_back(e); return false; }
    return true;
}

void count_tree(const std::string& p, Totals& t, uint64_t max_items, std::vector<Id>& stack)
{
    if (t.items >= max_items) { t.complete = false; return; }
    struct stat st;
    if (lstat(p.c_str(), &st) != 0) return;
    t.items++;
    if (S_ISREG(st.st_mode)) t.bytes += (uint64_t)st.st_size;
    if (!S_ISDIR(st.st_mode) || std::find(stack.begin(), stack.end(), id_of(st)) != stack.end()) return;
    DIR* d = opendir(p.c_str());
    if (!d) return;
    stack.push_back(id_of(st));
    while (struct dirent* e = readdir(d)) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        count_tree(p + "/" + e->d_name, t, max_items, stack);
        if (!t.complete) break;
    }
    stack.pop_back();
    closedir(d);
}

} // namespace

std::string Error::text() const
{
    std::string why = code ? strerror(code) : reason == Reason::SameFile ? "the source and destination are the same" : "";
    return what + (path.empty() ? std::string() : " " + path) + (why.empty() ? std::string() : ": " + why);
}

std::string Result::summary(size_t max_lines) const
{
    std::string s;
    size_t n = 0, shown = 0;
    for (const Error& e : errors) {
        if (e.reported) continue;
        if (n++ < max_lines) { s += (s.empty() ? "" : "\n") + e.text(); shown++; }
    }
    if (n > shown) s += "\n(" + std::to_string(n - shown) + " more)";
    if (errors.empty() && cancelled) s = "Cancelled.";
    return s;
}

bool is_inside(const std::string& path, const std::string& dir)
{
    struct stat st;
    if (stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return false;
    return chain_contains(path, id_of(st));
}

bool same_file(const std::string& a, const std::string& b)
{
    struct stat sa, sb;
    return lstat(a.c_str(), &sa) == 0 && lstat(b.c_str(), &sb) == 0 && id_of(sa) == id_of(sb);
}

Totals count(const std::vector<std::string>& sources, uint64_t max_items)
{
    Totals t;
    std::vector<Id> stack;
    for (const std::string& s : sources) { count_tree(strip_slashes(s), t, max_items, stack); if (!t.complete) break; }
    return t;
}

bool worth_a_thread(Op op, const std::vector<std::string>& sources, const std::string& dest_dir)
{
    bool heavy = op == Op::Copy || op == Op::Remove;
    if (op == Op::Move) for (const std::string& s : sources) if (!same_filesystem(dir_of(strip_slashes(absolute(s))), dest_dir)) heavy = true;
    if (!heavy) return sources.size() > 64;
    Totals t = count(sources, 33);
    return !t.complete || t.bytes > (8u << 20);
}

Result run(Op op, const std::vector<std::string>& sources, const std::string& dest_dir_in, const Options& o)
{
    Result r;
    Engine e(op, o, r);
    e.nsources = sources.size();
    if (o.progress && o.count) {
        e.prog.counting = true;
        e.report();
        Totals t = count(sources);
        e.prog.bytes_total = t.bytes;
        e.prog.items_total = t.items;
        e.prog.counting = false;
    }
    if (op == Op::Trash || op == Op::Remove) {
        for (const std::string& s : sources) {
            if (e.cancelled()) break;
            std::string parent, name;
            if (!split_source(s, &parent, &name)) { e.fail("Cannot delete", s, errno); continue; }
            std::string p = parent + "/" + name, err;
            e.item_begin(p);
            Totals t = o.progress ? count(std::vector<std::string>{ p }) : Totals();
            if (op == Op::Trash) {
                if (!shell::trash(p, &err)) {   // err already names the file and the reason
                    Error x; x.code = errno; x.what = err.empty() ? "Cannot move to the Trash:" : err; x.path = err.empty() ? p : std::string();
                    if (!err.empty()) x.code = 0;
                    r.errors.push_back(x);
                    continue;
                }
            } else {
                struct stat st;
                if (lstat(p.c_str(), &st) != 0) { e.fail("Cannot delete", p, errno); continue; }
                if (!remove_tree(p, st.st_dev, true, r)) continue;
            }
            e.prog.items_done += t.items; e.prog.bytes_done += t.bytes;
            e.report();
        }
        r.cancelled = e.stop;
        r.ok = r.errors.empty() && !r.cancelled;
        return r;
    }
    std::string dest = real(dest_dir_in);
    struct stat dst_st;
    if (dest.empty() || stat(dest.c_str(), &dst_st) != 0 || !S_ISDIR(dst_st.st_mode)) {
        e.fail("Cannot open the destination folder", dest_dir_in, dest.empty() ? errno : ENOTDIR);
        r.ok = false;
        return r;
    }
    const char* verb = op == Op::Move ? "Cannot move" : "Cannot copy";
    // one top-level item; true: done, false: it failed (errors recorded, rolled back)
    auto item = [&](const std::string& s) -> bool {
        std::string parent, name;
        if (!split_source(s, &parent, &name)) { e.fail(verb, s, errno); return false; }
        std::string src = parent + "/" + name;
        struct stat ss;
        if (lstat(src.c_str(), &ss) != 0) { e.fail(verb, src, errno); return false; }
        if (S_ISDIR(ss.st_mode) && chain_contains(dest, id_of(ss))) {
            bool self = id_of(ss) == id_of(dst_st);
            e.fail(std::string(verb) + (self ? " (the destination folder is the folder itself):" : " (the destination folder is inside it):"),
                   src, EINVAL, self ? Reason::IntoItself : Reason::IntoSubfolder);
            return false;
        }
        e.created.clear(); e.moved.clear(); e.skipped = 0;
        size_t errs = r.errors.size();
        std::string final_dst;
        bool ok = e.transfer(src, dest, name, true, &final_dst);
        if (!ok || e.stop || r.errors.size() != errs) {
            if (o.rollback) e.rollback();
            e.moved.clear();
            return false;
        }
        if (op == Op::Move && !e.remove_moved()) return false;
        if (!final_dst.empty()) r.created.push_back(final_dst);
        return true;
    };
    for (const std::string& s : sources) {
        if (e.cancelled()) break;
        for (;;) {
            size_t errs = r.errors.size();
            uint64_t bytes = e.prog.bytes_done, items = e.prog.items_done;
            if (item(s) || e.stop || !o.confirm || r.errors.size() == errs) break;
            // XP's copy engine reports a failure when it happens: Retry the item, go on (No), or stop
            Conflict c;
            c.kind = Conflict::Failed;
            c.src = s;
            c.error = r.errors[errs];
            Answer a = o.confirm(c);
            for (size_t i = errs; i < r.errors.size(); i++) r.errors[i].reported = true;
            if (a == Answer::Retry) { r.errors.resize(errs); e.prog.bytes_done = bytes; e.prog.items_done = items; continue; }
            if (a != Answer::No) e.stop = true;
            break;
        }
        if (e.stop) break;
    }
    r.cancelled = e.stop;
    r.ok = r.errors.empty() && !r.cancelled;
    return r;
}

} // namespace fileops
} // namespace shell
