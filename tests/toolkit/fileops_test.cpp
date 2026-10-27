// Tests for shell::fileops (copy/move engine, Job) and the paste/drop wrappers.
// Usage: build/fileops_test [scratch-dir [other-filesystem-dir]]
// Works only inside two fresh directories it makes (mkdtemp) under those
// (defaults $TMPDIR or /tmp, and /dev/shm) and removes them at the end.
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <string>
#include <vector>
#include "shell/fileops.h"
#include "shell/fileops_job.h"

using namespace shell;
using fileops::Op;
using fileops::Answer;

static int g_fail = 0, g_pass = 0;
static std::string A, B;   // scratch dir (fs 1), other filesystem dir (fs 2)

#define CHECK(c) do { if (c) g_pass++; else { g_fail++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void put(const std::string& p, const std::string& data, mode_t mode = 0644)
{
    int fd = open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) { perror(p.c_str()); exit(2); }
    if (write(fd, data.data(), data.size()) != (ssize_t)data.size()) exit(2);
    close(fd);
    chmod(p.c_str(), mode);
}
static std::string get(const std::string& p)
{
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return "<missing>";
    std::string s; char b[4096]; size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
    fclose(f);
    return s;
}
static bool exists(const std::string& p) { struct stat st; return lstat(p.c_str(), &st) == 0; }
static bool is_dir(const std::string& p) { struct stat st; return lstat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
static bool is_link(const std::string& p) { struct stat st; return lstat(p.c_str(), &st) == 0 && S_ISLNK(st.st_mode); }
static std::string link_of(const std::string& p) { char b[4096]; ssize_t n = readlink(p.c_str(), b, sizeof b - 1); if (n < 0) return ""; b[n] = 0; return b; }
static void mk(const std::string& p, mode_t m = 0755) { if (mkdir(p.c_str(), m) != 0) { perror(p.c_str()); exit(2); } chmod(p.c_str(), m); }

// anything named *.flxp-part* below p?
static bool has_parts(const std::string& p)
{
    DIR* d = opendir(p.c_str());
    if (!d) return false;
    bool found = false;
    while (struct dirent* e = readdir(d)) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (strstr(e->d_name, ".flxp-part")) found = true;
        std::string c = p + "/" + e->d_name;
        if (is_dir(c) && has_parts(c)) found = true;
    }
    closedir(d);
    return found;
}
static int entries(const std::string& p)
{
    DIR* d = opendir(p.c_str());
    if (!d) return -1;
    int n = 0;
    while (struct dirent* e = readdir(d)) if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) n++;
    closedir(d);
    return n;
}

// removes a test tree (only ours: under A or B); makes folders writable first
static void nuke(const std::string& p)
{
    if (p.compare(0, A.size(), A) != 0 && p.compare(0, B.size(), B) != 0) { fprintf(stderr, "refusing to remove %s\n", p.c_str()); exit(3); }
    struct stat st;
    if (lstat(p.c_str(), &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        chmod(p.c_str(), 0700);
        DIR* d = opendir(p.c_str());
        if (d) {
            std::vector<std::string> n;
            while (struct dirent* e = readdir(d)) if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) n.push_back(e->d_name);
            closedir(d);
            for (auto& x : n) nuke(p + "/" + x);
        }
        rmdir(p.c_str());
    } else unlink(p.c_str());
}
static std::string fresh(const std::string& base, const char* name) { std::string p = base + "/" + name; nuke(p); mk(p); return p; }

static void test_same_fs_move()
{
    std::string t = fresh(A, "samemove"), s = t + "/src", d = t + "/dst";
    mk(s); mk(d); mk(s + "/dir"); put(s + "/dir/f", "hello"); put(s + "/g", "g");
    struct stat before; lstat((s + "/dir/f").c_str(), &before);
    fileops::Result r = fileops::run(Op::Move, { s + "/dir", s + "/g/" }, d);
    CHECK(r.ok);
    CHECK(get(d + "/dir/f") == "hello" && get(d + "/g") == "g");
    CHECK(!exists(s + "/dir") && !exists(s + "/g"));
    struct stat after; lstat((d + "/dir/f").c_str(), &after);
    CHECK(after.st_ino == before.st_ino);   // renamed, not copied
    CHECK(r.created.size() == 2);
    // moving onto itself: nothing happens, no error
    r = fileops::run(Op::Move, { d + "/g" }, d);
    CHECK(r.ok && get(d + "/g") == "g" && entries(d) == 2);
    // the old wrapper, copy onto itself: "Copy of g"
    std::string err;
    CHECK(paste_items({ d + "/g" }, d, false, &err));
    CHECK(get(d + "/Copy of g") == "g");
    CHECK(paste_items({ d + "/g" }, d, false, &err) && exists(d + "/Copy (2) of g"));
    nuke(t);
}

static void test_cross_fs_move()
{
    std::string t = fresh(A, "xmove"), u = fresh(B, "xmove");
    mk(t + "/tree"); mk(t + "/tree/sub"); put(t + "/tree/a", "aaa"); put(t + "/tree/sub/b", std::string(3 << 20, 'b'));
    symlink("a", (t + "/tree/la").c_str());
    fileops::Result r = fileops::run(Op::Move, { t + "/tree" }, u);
    CHECK(r.ok);
    CHECK(!exists(t + "/tree"));
    CHECK(get(u + "/tree/a") == "aaa" && get(u + "/tree/sub/b").size() == (3u << 20) && link_of(u + "/tree/la") == "a");
    CHECK(!has_parts(u));

    // a source that cannot be removed: its parent is read-only
    mk(t + "/ro"); put(t + "/ro/f", "fff"); mk(t + "/ro/d"); put(t + "/ro/d/x", "xx");
    chmod((t + "/ro").c_str(), 0555);
    r = fileops::run(Op::Move, { t + "/ro/f", t + "/ro/d" }, u);
    CHECK(!r.ok);
    CHECK(r.errors.size() == 2);
    bool named_f = false, named_d = false;
    for (auto& e : r.errors) { if (e.path == t + "/ro/f" && e.code == EACCES) named_f = true; if (e.path == t + "/ro/d" && e.code == EACCES) named_d = true; }
    CHECK(named_f && named_d);
    CHECK(exists(t + "/ro/f") && get(u + "/f") == "fff");   // both kept: the copy is complete, the original could not go
    CHECK(get(u + "/d/x") == "xx" && is_dir(t + "/ro/d"));
    CHECK(!r.summary().empty());
    std::string err;
    chmod((t + "/ro").c_str(), 0755);

    // a source with an unreadable file: nothing is removed, nothing is left at the destination
    mk(t + "/bad"); put(t + "/bad/1", "one"); put(t + "/bad/2", "two", 0000); put(t + "/bad/3", "three");
    if (geteuid() != 0) {
        r = fileops::run(Op::Move, { t + "/bad" }, u);
        CHECK(!r.ok && r.errors.size() == 1 && r.errors[0].path == t + "/bad/2");
        CHECK(!exists(u + "/bad") && get(t + "/bad/1") == "one" && get(t + "/bad/3") == "three");
        CHECK(paste_items({ t + "/bad" }, u, true, &err) == false && err.find("/bad/2") != std::string::npos);
    }
    nuke(t); nuke(u);
}

static void test_into_itself()
{
    std::string t = fresh(A, "self");
    mk(t + "/x"); mk(t + "/x/sub"); put(t + "/x/f", "f");
    symlink("x", (t + "/lnk").c_str());
    for (Op op : { Op::Move, Op::Copy }) {
        fileops::Result r = fileops::run(op, { t + "/x" }, t + "/x/sub");
        CHECK(!r.ok && r.errors.size() == 1);
        r = fileops::run(op, { t + "/x" }, t + "/lnk/sub");           // through a symlink
        CHECK(!r.ok && r.errors.size() == 1);
        r = fileops::run(op, { t + "/x/sub/.." }, t + "/x/sub/");      // "..", trailing slash
        CHECK(!r.ok && r.errors.size() == 1);
        r = fileops::run(op, { t + "/x/" }, t + "/x");                 // into itself
        CHECK(!r.ok && r.errors.size() == 1);
    }
    CHECK(get(t + "/x/f") == "f" && entries(t + "/x/sub") == 0 && entries(t) == 2);
    CHECK(fileops::is_inside(t + "/lnk/sub", t + "/x") && !fileops::is_inside(t, t + "/x"));
    CHECK(drop_effect({ t + "/x" }, t + "/lnk/sub", false, false, EffectCopy | EffectMove) == 0);
    CHECK(drop_effect({ t + "/x/f" }, t + "/lnk", false, false, EffectCopy | EffectMove) == 0);   // already there
    CHECK(drop_effect({ t + "/x/f" }, t + "/lnk/sub", false, false, EffectCopy | EffectMove) == EffectMove);
    // a symlink to a folder moved into that folder is just the link moving
    fileops::Result r = fileops::run(Op::Move, { t + "/lnk" }, t + "/x/sub");
    CHECK(r.ok && is_link(t + "/x/sub/lnk"));
    nuke(t);
}

static void test_symlinks()
{
    std::string t = fresh(A, "links"), u = fresh(B, "links");
    mk(t + "/tree"); put(t + "/tree/f", "f");
    symlink("f", (t + "/tree/lf").c_str());
    symlink(".", (t + "/tree/self").c_str());                 // a loop if followed
    symlink("..", (t + "/tree/up").c_str());
    symlink("loop2", (t + "/tree/loop1").c_str());
    symlink("loop1", (t + "/tree/loop2").c_str());
    symlink((t + "/tree").c_str(), (t + "/tree/abs").c_str());
    symlink("missing", (t + "/tree/dangling").c_str());
    mkfifo((t + "/tree/fifo").c_str(), 0640);
    fileops::Result r = fileops::run(Op::Copy, { t + "/tree" }, u);     // across filesystems
    CHECK(r.ok);
    r = fileops::run(Op::Copy, { t + "/tree" }, t + "/tree/self");       // into itself through its own link: refused
    CHECK(!r.ok && entries(t + "/tree") == 9);
    r = fileops::run(Op::Copy, { t + "/tree" }, t);                      // next to itself
    CHECK(r.ok && r.created.size() == 1 && r.created[0] == t + "/Copy of tree");
    for (const std::string& c : { u + "/tree", t + "/Copy of tree" }) {
        CHECK(is_link(c + "/self") && link_of(c + "/self") == ".");
        CHECK(link_of(c + "/up") == ".." && link_of(c + "/loop1") == "loop2" && link_of(c + "/abs") == t + "/tree");
        CHECK(link_of(c + "/dangling") == "missing" && get(c + "/lf") == "f" && is_link(c + "/lf"));
        CHECK(entries(c) == 9);
        struct stat fs;
        CHECK(lstat((c + "/fifo").c_str(), &fs) == 0 && S_ISFIFO(fs.st_mode) && (fs.st_mode & 0777) == 0640);
    }
    nuke(t); nuke(u);
}

static void test_failed_copy_cleanup()
{
    if (geteuid() == 0) { fprintf(stderr, "skipping the unreadable-file tests as root\n"); return; }
    std::string t = fresh(A, "fail"), u = fresh(B, "fail");
    mk(t + "/u"); put(t + "/u/a", "a"); mk(t + "/u/b"); put(t + "/u/b/c", "c"); put(t + "/u/b/d", "d", 0000); put(t + "/u/e", "e");
    mk(t + "/u/f", 0555);   // read-only folder made by the copy before the failure: rollback must still remove it
    put(t + "/u/0", "zero"); chmod((t + "/u/f").c_str(), 0755); put(t + "/u/f/in", "in"); chmod((t + "/u/f").c_str(), 0555);
    fileops::Result r = fileops::run(Op::Copy, { t + "/u" }, u);
    CHECK(!r.ok && r.errors.size() == 1 && r.errors[0].path == t + "/u/b/d" && r.errors[0].code == EACCES);
    CHECK(!exists(u + "/u") && entries(u) == 0);

    // merging into a folder that existed: only what the copy made goes
    mk(u + "/u"); put(u + "/u/keep", "keep"); mk(u + "/u/b"); put(u + "/u/b/old", "old"); put(u + "/u/a", "old a");
    fileops::Options o;
    int asked = 0;
    o.confirm = [&](const fileops::Conflict&) { asked++; return Answer::Yes; };
    r = fileops::run(Op::Copy, { t + "/u" }, u, o);
    CHECK(!r.ok && r.errors.size() == 1);
    CHECK(asked >= 2);   // the folder u, then b or a
    CHECK(get(u + "/u/keep") == "keep" && get(u + "/u/b/old") == "old" && is_dir(u + "/u/b"));
    CHECK(!exists(u + "/u/b/c") && !exists(u + "/u/e") && !exists(u + "/u/0") && !exists(u + "/u/f"));
    CHECK(!has_parts(u));

    // the next item still goes after a failed one
    put(t + "/ok", "ok");
    r = fileops::run(Op::Copy, { t + "/u", t + "/ok" }, u);
    CHECK(!r.ok && r.errors.size() == 1 && get(u + "/ok") == "ok");

    // cancelled from the progress callback in the middle of a big file: no part, no file
    chmod((t + "/u/b/d").c_str(), 0644);
    put(t + "/u/big", std::string(5 << 20, 'z'));
    std::atomic<bool> cancel{false};
    fileops::Options oc;
    oc.cancel = &cancel;
    oc.progress = [&](const fileops::Progress& p) { if (p.bytes_done > (2u << 20)) cancel = true; };
    std::string v = fresh(B, "cancel");
    r = fileops::run(Op::Copy, { t + "/u" }, v, oc);
    CHECK(r.cancelled && !r.ok && r.errors.empty());
    CHECK(entries(v) == 0);
    // and a cancelled cross-fs move leaves the source alone
    cancel = false;
    r = fileops::run(Op::Move, { t + "/u" }, v, oc);
    CHECK(r.cancelled && entries(v) == 0 && get(t + "/u/big").size() == (5u << 20) && get(t + "/u/b/c") == "c");
    nuke(t); nuke(u); nuke(v);
}

static bool ts_eq(const struct timespec& a, const struct timespec& b) { return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec; }

static void test_metadata()
{
    std::string t = fresh(A, "meta"), u = fresh(B, "meta");
    mk(t + "/m"); put(t + "/m/suid", "#!/bin/sh\n", 0755); chmod((t + "/m/suid").c_str(), 04755);
    put(t + "/m/sgid", "x", 0644); chmod((t + "/m/sgid").c_str(), 02750);
    put(t + "/m/ro", "r", 0400);
    mk(t + "/m/sticky"); chmod((t + "/m/sticky").c_str(), 01777);
    mk(t + "/m/rodir"); put(t + "/m/rodir/in", "in"); chmod((t + "/m/rodir").c_str(), 0555);
    bool xa = setxattr((t + "/m/suid").c_str(), "user.flxp.test", "value", 5, 0) == 0 &&
              setxattr((t + "/m").c_str(), "user.flxp.dir", "dv", 2, 0) == 0;
    if (!xa) fprintf(stderr, "note: no user xattrs on %s (%s): xattr checks skipped\n", t.c_str(), strerror(errno));
    struct timespec ft[2] = { { 1000000000, 123456789 }, { 1200000000, 987654321 } };
    utimensat(AT_FDCWD, (t + "/m/suid").c_str(), ft, 0);
    utimensat(AT_FDCWD, (t + "/m/ro").c_str(), ft, 0);
    struct timespec dt[2] = { { 1100000000, 111 }, { 1300000000, 222333444 } };
    utimensat(AT_FDCWD, (t + "/m/sticky").c_str(), dt, 0);
    utimensat(AT_FDCWD, (t + "/m/rodir").c_str(), dt, 0);
    struct timespec lt[2] = { { 1000, 5 }, { 2000, 6 } };
    symlink("suid", (t + "/m/link").c_str());
    utimensat(AT_FDCWD, (t + "/m/link").c_str(), lt, AT_SYMLINK_NOFOLLOW);
    utimensat(AT_FDCWD, (t + "/m").c_str(), dt, 0);
    for (const std::string& dest : { u, fresh(A, "meta2") }) {
        struct stat s0;
        lstat((t + "/m/suid").c_str(), &s0);   // the atime before this copy reads it
        fileops::Result r = fileops::run(Op::Copy, { t + "/m" }, dest);
        CHECK(r.ok);
        if (!r.ok) fprintf(stderr, "%s\n", r.summary().c_str());
        std::string c = dest + "/m";
        struct stat s2;
        lstat((c + "/suid").c_str(), &s2);
        CHECK((s2.st_mode & 07777) == 04755);
        CHECK(ts_eq(s2.st_mtim, ft[1]) && ts_eq(s2.st_atim, s0.st_atim));
        lstat((c + "/sgid").c_str(), &s2); CHECK((s2.st_mode & 07777) == 02750);
        lstat((c + "/ro").c_str(), &s2); CHECK((s2.st_mode & 07777) == 0400 && ts_eq(s2.st_mtim, ft[1]) && get(c + "/ro") == "r");
        lstat((c + "/sticky").c_str(), &s2); CHECK((s2.st_mode & 07777) == 01777 && ts_eq(s2.st_mtim, dt[1]));
        lstat((c + "/rodir").c_str(), &s2); CHECK((s2.st_mode & 07777) == 0555 && ts_eq(s2.st_mtim, dt[1]) && get(c + "/rodir/in") == "in");
        lstat(c.c_str(), &s2); CHECK(ts_eq(s2.st_mtim, dt[1]));   // after its contents went in
        lstat((c + "/link").c_str(), &s2); CHECK(S_ISLNK(s2.st_mode) && ts_eq(s2.st_mtim, lt[1]));
        if (xa) {
            char v[16] = {};
            CHECK(getxattr((c + "/suid").c_str(), "user.flxp.test", v, sizeof v) == 5 && !memcmp(v, "value", 5));
            CHECK(getxattr(c.c_str(), "user.flxp.dir", v, sizeof v) == 2 && !memcmp(v, "dv", 2));
        }
        CHECK(!has_parts(dest));
        nuke(dest);
    }
    nuke(t);
}

static void test_sparse()
{
    std::string t = fresh(A, "sparse"), u = fresh(B, "sparse");
    std::string f = t + "/s";
    int fd = open(f.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    const off_t size = 64 << 20;
    pwrite(fd, "head", 4, 0);
    pwrite(fd, "middle", 6, 32 << 20);
    ftruncate(fd, size);   // a hole at the end too
    close(fd);
    struct stat s1; lstat(f.c_str(), &s1);
    bool sparse_src = (off_t)s1.st_blocks * 512 < size / 4;
    for (const std::string& dest : { u, fresh(A, "sparse2") }) {
        fileops::Result r = fileops::run(Op::Copy, { f }, dest);
        CHECK(r.ok);
        struct stat s2; lstat((dest + "/s").c_str(), &s2);
        CHECK(s2.st_size == size);
        if (sparse_src) CHECK((off_t)s2.st_blocks * 512 < (1 << 20));
        int in = open((dest + "/s").c_str(), O_RDONLY);
        char b[8] = {};
        CHECK(pread(in, b, 4, 0) == 4 && !memcmp(b, "head", 4));
        CHECK(pread(in, b, 6, 32 << 20) == 6 && !memcmp(b, "middle", 6));
        CHECK(pread(in, b, 4, 16 << 20) == 4 && !memcmp(b, "\0\0\0\0", 4));
        close(in);
        nuke(dest);
    }
    nuke(t);
}

static void test_overwrite()
{
    std::string t = fresh(A, "over"), u = fresh(A, "overdst");
    auto setup = [&]() {
        nuke(t); nuke(u); mk(t); mk(u);
        put(t + "/f", "new f"); mk(t + "/d"); put(t + "/d/x", "new x"); put(t + "/d/y", "new y"); put(t + "/g", "new g");
        put(u + "/f", "old f"); mk(u + "/d"); put(u + "/d/x", "old x"); put(u + "/d/keep", "keep"); put(u + "/g", "old g");
    };
    // No for f, Yes for d (merge) and for d/x, Yes for g
    setup();
    std::vector<std::string> asked;
    fileops::Options o;
    o.confirm = [&](const fileops::Conflict& c) {
        asked.push_back(c.dst);
        if (c.dst == u + "/f") return Answer::No;
        if (c.dst == u + "/d") { CHECK(c.folder); return Answer::Yes; }
        CHECK(!c.folder);
        return Answer::Yes;
    };
    fileops::Result r = fileops::run(Op::Copy, { t + "/f", t + "/d", t + "/g" }, u, o);
    CHECK(r.ok && asked.size() == 4);
    CHECK(get(u + "/f") == "old f" && get(u + "/d/x") == "new x" && get(u + "/d/y") == "new y" && get(u + "/d/keep") == "keep" && get(u + "/g") == "new g");
    CHECK(get(t + "/f") == "new f" && entries(u) == 3 && !has_parts(u));
    // Yes to All: asked once
    setup(); asked.clear();
    o.confirm = [&](const fileops::Conflict& c) { asked.push_back(c.dst); return Answer::YesToAll; };
    r = fileops::run(Op::Copy, { t + "/f", t + "/d", t + "/g" }, u, o);
    CHECK(r.ok && asked.size() == 1 && get(u + "/f") == "new f" && get(u + "/d/x") == "new x" && get(u + "/g") == "new g");
    // No to All: nothing replaced, y not copied either (d itself is a No)
    setup(); asked.clear();
    o.confirm = [&](const fileops::Conflict& c) { asked.push_back(c.dst); return Answer::NoToAll; };
    r = fileops::run(Op::Copy, { t + "/f", t + "/d", t + "/g" }, u, o);
    CHECK(r.ok && asked.size() == 1 && get(u + "/f") == "old f" && !exists(u + "/d/y") && get(u + "/g") == "old g");
    // Cancel at d/x: f was replaced already (kept), d's new entries go, g untouched; cancelled
    setup();
    o.confirm = [&](const fileops::Conflict& c) { return c.dst == u + "/d/x" ? Answer::Cancel : Answer::Yes; };
    r = fileops::run(Op::Copy, { t + "/f", t + "/d", t + "/g" }, u, o);
    CHECK(r.cancelled && !r.ok && r.errors.empty());
    CHECK(get(u + "/f") == "new f" && get(u + "/d/x") == "old x" && get(u + "/d/keep") == "keep" && get(u + "/g") == "old g");
    CHECK(!exists(u + "/d/y") || get(u + "/d/y") == "<missing>");
    // move with Yes (same filesystem): merged, the emptied source folder gone, the No'd item stays
    setup();
    o.confirm = [&](const fileops::Conflict& c) { return c.dst == u + "/g" ? Answer::No : Answer::Yes; };
    r = fileops::run(Op::Move, { t + "/f", t + "/d", t + "/g" }, u, o);
    CHECK(r.ok && get(u + "/f") == "new f" && get(u + "/d/x") == "new x" && get(u + "/d/y") == "new y" && get(u + "/d/keep") == "keep");
    CHECK(!exists(t + "/f") && !exists(t + "/d") && get(t + "/g") == "new g" && get(u + "/g") == "old g");
    // move with merge across filesystems, a No inside: that file and its folder stay
    std::string w = fresh(B, "overx");
    setup();
    mk(w + "/d"); put(w + "/d/x", "old x"); put(w + "/f", "old f");
    o.confirm = [&](const fileops::Conflict& c) { return c.dst == w + "/d/x" ? Answer::No : Answer::Yes; };
    r = fileops::run(Op::Move, { t + "/f", t + "/d" }, w, o);
    CHECK(r.ok);
    if (!r.ok) fprintf(stderr, "%s\n", r.summary().c_str());
    CHECK(get(w + "/f") == "new f" && !exists(t + "/f") && get(w + "/d/x") == "old x" && get(w + "/d/y") == "new y");
    CHECK(get(t + "/d/x") == "new x" && !exists(t + "/d/y"));
    // a folder onto a file: an error, nothing replaced
    setup(); unlink((u + "/g").c_str()); mk(u + "/g");
    o.confirm = [&](const fileops::Conflict&) { return Answer::Yes; };
    r = fileops::run(Op::Copy, { t + "/g" }, u, o);
    CHECK(!r.ok && r.errors.size() == 1 && is_dir(u + "/g"));
    // no callback: "Copy of f" (as before), for a move too
    setup();
    r = fileops::run(Op::Move, { t + "/f" }, u);
    CHECK(r.ok && get(u + "/f") == "old f" && get(u + "/Copy of f") == "new f" && !exists(t + "/f"));
    nuke(t); nuke(u); nuke(w);
}

static void test_trash_remove()
{
    std::string t = fresh(A, "rm");
    mk(t + "/d"); put(t + "/d/a", "a"); mk(t + "/d/e"); put(t + "/d/e/b", "b");
    mk(t + "/ro"); put(t + "/ro/x", "x"); chmod((t + "/ro").c_str(), 0555);
    fileops::Result r = fileops::run(Op::Remove, { t + "/d", t + "/ro/x" }, "");
    CHECK(!exists(t + "/d"));
    if (geteuid() != 0) CHECK(!r.ok && r.errors.size() == 1 && r.errors[0].path == t + "/ro/x" && exists(t + "/ro/x"));
    nuke(t);
}

struct Waiter { std::mutex m; std::condition_variable cv; int n = 0; };
static void on_notify(void* ctx) { Waiter* w = (Waiter*)ctx; std::lock_guard<std::mutex> l(w->m); w->n++; w->cv.notify_all(); }

static void test_job()
{
    std::string t = fresh(A, "job"), u = fresh(B, "job");
    mk(t + "/j");
    for (int i = 0; i < 50; i++) put(t + "/j/f" + std::to_string(i), std::string(100000, 'a' + i % 26));
    put(u + "/clash", "old"); put(t + "/clash", "new");
    std::vector<std::string> src = { t + "/j", t + "/clash" };
    CHECK(fileops::worth_a_thread(Op::Copy, src, u));
    CHECK(!fileops::worth_a_thread(Op::Move, { t + "/clash" }, t + "/j"));
    Waiter w;
    fileops::Job job(Op::Move, src, u, on_notify, &w, true);
    job.start();
    int answered = 0;
    for (;;) {
        std::unique_lock<std::mutex> l(w.m);
        w.cv.wait_for(l, std::chrono::milliseconds(200), [&] { return w.n > 0; });
        w.n = 0;
        l.unlock();
        fileops::Conflict c;
        if (job.question(&c)) { CHECK(c.dst == u + "/clash"); job.answer(Answer::Yes); answered++; }
        if (job.done()) break;
    }
    fileops::Result r = job.result();
    fileops::Progress p = job.progress();
    CHECK(r.ok && answered == 1);
    CHECK(p.items_total == 52 && p.items_done == p.items_total && p.bytes_done == p.bytes_total && p.bytes_total == 5000003);
    CHECK(get(u + "/clash") == "new" && entries(u + "/j") == 50 && !exists(t + "/j"));
    // cancelled while waiting for an answer
    put(t + "/clash", "newer");
    fileops::Job j2(Op::Copy, { t + "/clash" }, u, on_notify, &w, true);
    j2.start();
    while (!j2.question(nullptr)) usleep(1000);
    j2.cancel();
    r = j2.result();
    CHECK(r.cancelled && get(u + "/clash") == "new");
    nuke(t); nuke(u);
}

int main(int argc, char** argv)
{
    const char* tmp = getenv("TMPDIR");
    std::string base1 = argc > 1 ? argv[1] : tmp ? tmp : "/tmp", base2 = argc > 2 ? argv[2] : "/dev/shm";
    std::string t1 = base1 + "/fileops_test.XXXXXX", t2 = base2 + "/fileops_test.XXXXXX";
    if (!mkdtemp(&t1[0]) || !mkdtemp(&t2[0])) { perror("mkdtemp"); return 2; }
    A = t1; B = t2;
    struct statfs f1, f2;
    statfs(A.c_str(), &f1); statfs(B.c_str(), &f2);
    struct stat s1, s2;
    stat(A.c_str(), &s1); stat(B.c_str(), &s2);
    if (s1.st_dev == s2.st_dev) fprintf(stderr, "warning: %s and %s are on the same filesystem: the cross-filesystem cases test renames\n", A.c_str(), B.c_str());
    umask(022);
    test_same_fs_move();
    test_cross_fs_move();
    test_into_itself();
    test_symlinks();
    test_failed_copy_cleanup();
    test_metadata();
    test_sparse();
    test_overwrite();
    test_trash_remove();
    test_job();
    nuke(A); nuke(B);
    printf("fileops_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
