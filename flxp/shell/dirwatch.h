// One directory followed on disk with inotify (the views follow the folder
// shown: entries created, deleted, renamed, written or changed; the directory
// itself deleted or moved). Without inotify, or past the user's watch limit
// (fs.inotify.max_user_watches), nothing is watched and F5 is the way.
#pragma once
#include <string>

namespace shell {

class DirWatch {
public:
    DirWatch() = default;
    ~DirWatch();
    DirWatch(const DirWatch&) = delete;
    DirWatch& operator=(const DirWatch&) = delete;
    // Follow dir (instead of the previous one); false: not watched.
    bool watch(const std::string& dir);
    void clear();
    int fd() const { return fd_; }             // for the event loop (sys::watch_fd); -1 without inotify
    const std::string& dir() const { return dir_; }
    enum { Changed = 1, Gone = 2 };
    // what is pending (read without blocking): Changed (an entry) and Gone (the
    // directory itself was deleted, moved or unmounted) for the directory watched now
    int take();
private:
    int fd_ = -1, wd_ = -1;
    std::string dir_;
    bool failed_ = false;                      // inotify_init failed: not tried again
};

}
