#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <mutex>
#include <thread>
#include "dirwatch.h"
#include "util.h"

namespace shell {

// The inotify descriptor and the watch now, shared with a thread adding a slow folder's watch: the descriptor is closed
// when the last of them lets go (a thread still in inotify_add_watch keeps it, so its number is not reused under it)
struct DirWatch::Shared {
    std::mutex lock;
    int fd = -1;
    int wd = -1;
    unsigned gen = 0;               // the watch asked for last: a thread's result for an older one is removed again
    ~Shared() { if (fd >= 0) close(fd); }
};

// IN_ATTRIB: permissions, ownership and touch(1)'s times; IN_CLOSE_WRITE: a file written
// (not every IN_MODIFY of a copy in progress); IN_ONLYDIR: not a directory (any more)
static const uint32_t MASK = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ATTRIB |
                             IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR;

static void told_limit(const std::string& dir)
{
    static bool told;
    if (errno == ENOSPC && !told) {
        told = true;
        fprintf(stderr, "%s: the inotify watch limit is reached (fs.inotify.max_user_watches): %s is not followed (F5 refreshes)\n", shell::program_name(), dir.c_str());
    }
}

DirWatch::DirWatch() : s_(std::make_shared<Shared>()) {}

DirWatch::~DirWatch() { clear(); }

int DirWatch::fd() const { return s_->fd; }

void DirWatch::clear()
{
    {
        std::lock_guard<std::mutex> l(s_->lock);
        s_->gen++;
        if (s_->fd >= 0 && s_->wd >= 0) inotify_rm_watch(s_->fd, s_->wd);
        s_->wd = -1;
    }
    dir_.clear();
    take();   // what the old watch still had queued
}

bool DirWatch::watch(const std::string& dir, bool slow)
{
    if (dir == dir_ && (s_->wd >= 0 || slow)) return true;
    clear();
    if (dir.empty() || dir[0] != '/') return false;
    if (s_->fd < 0) {
        if (failed_) return false;
        s_->fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (s_->fd < 0) { failed_ = true; fprintf(stderr, "%s: no inotify (%s): folders are not updated by themselves (F5 refreshes)\n", shell::program_name(), strerror(errno)); return false; }
    }
    dir_ = dir;
    if (slow) {
        // a network folder: the watch is added on a thread of its own (inotify_add_watch looks the folder up, FreeBSD's
        // every entry in it: a round trip each), followed from when it is in
        std::shared_ptr<Shared> s = s_;
        unsigned gen = s->gen;
        std::thread([s, gen, dir] {
            int wd = inotify_add_watch(s->fd, dir.c_str(), MASK);
            if (wd < 0) { told_limit(dir); return; }
            std::lock_guard<std::mutex> l(s->lock);
            if (s->gen == gen) s->wd = wd;
            else inotify_rm_watch(s->fd, wd);   // another folder (or none) is followed by now
        }).detach();
        return true;
    }
    int wd = inotify_add_watch(s_->fd, dir.c_str(), MASK);
    if (wd < 0) { told_limit(dir); dir_.clear(); return false; }
    std::lock_guard<std::mutex> l(s_->lock);
    s_->wd = wd;
    return true;
}

int DirWatch::take()
{
    if (s_->fd < 0) return 0;
    int out = 0;
    alignas(struct inotify_event) char buf[8192];
    std::lock_guard<std::mutex> l(s_->lock);
    for (;;) {
        ssize_t n = read(s_->fd, buf, sizeof buf);
        if (n <= 0) break;   // EAGAIN: all read
        for (char* p = buf; p < buf + n;) {
            const struct inotify_event* e = (const struct inotify_event*)p;
            p += sizeof *e + e->len;
            if (e->mask & IN_Q_OVERFLOW) { if (s_->wd >= 0) out |= Changed; continue; }   // events lost: look again
            if (e->wd != s_->wd || s_->wd < 0) continue;   // a watch given up already
            out |= e->mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_UNMOUNT | IN_IGNORED) ? Gone : Changed;
        }
        if (n < (ssize_t)sizeof buf - 512) break;
    }
    if (out & Gone) {   // deleted: the kernel drops the watch; moved: it would follow the directory, we do not
        inotify_rm_watch(s_->fd, s_->wd);
        s_->wd = -1;
        s_->gen++;
        dir_.clear();
    }
    return out;
}

}
