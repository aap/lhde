#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/inotify.h>
#include "dirwatch.h"

namespace shell {

DirWatch::~DirWatch()
{
    if (fd_ >= 0) close(fd_);
}

void DirWatch::clear()
{
    if (fd_ >= 0 && wd_ >= 0) inotify_rm_watch(fd_, wd_);
    wd_ = -1;
    dir_.clear();
    take();   // what the old watch still had queued
}

bool DirWatch::watch(const std::string& dir)
{
    if (dir == dir_ && wd_ >= 0) return true;
    clear();
    if (dir.empty() || dir[0] != '/') return false;
    if (fd_ < 0) {
        if (failed_) return false;
        fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (fd_ < 0) { failed_ = true; fprintf(stderr, "flxp: no inotify (%s): folders are not updated by themselves (F5 refreshes)\n", strerror(errno)); return false; }
    }
    // IN_ATTRIB: permissions, ownership and touch(1)'s times; IN_CLOSE_WRITE: a file written
    // (not every IN_MODIFY of a copy in progress); IN_ONLYDIR: not a directory (any more)
    wd_ = inotify_add_watch(fd_, dir.c_str(), IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE |
                                              IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR);
    if (wd_ < 0) {
        static bool told;
        if (errno == ENOSPC && !told) {
            told = true;
            fprintf(stderr, "flxp: the inotify watch limit is reached (fs.inotify.max_user_watches): %s is not followed (F5 refreshes)\n", dir.c_str());
        }
        return false;
    }
    dir_ = dir;
    return true;
}

int DirWatch::take()
{
    if (fd_ < 0) return 0;
    int out = 0;
    alignas(struct inotify_event) char buf[8192];
    for (;;) {
        ssize_t n = read(fd_, buf, sizeof buf);
        if (n <= 0) break;   // EAGAIN: all read
        for (char* p = buf; p < buf + n;) {
            const struct inotify_event* e = (const struct inotify_event*)p;
            p += sizeof *e + e->len;
            if (e->mask & IN_Q_OVERFLOW) { if (wd_ >= 0) out |= Changed; continue; }   // events lost: look again
            if (e->wd != wd_ || wd_ < 0) continue;   // a watch given up already
            out |= e->mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_UNMOUNT | IN_IGNORED) ? Gone : Changed;
        }
        if (n < (ssize_t)sizeof buf - 512) break;
    }
    if (out & Gone) {   // deleted: the kernel drops the watch; moved: it would follow the directory, we do not
        inotify_rm_watch(fd_, wd_);
        wd_ = -1;
        dir_.clear();
    }
    return out;
}

}
