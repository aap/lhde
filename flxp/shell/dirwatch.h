// One directory followed on disk with inotify (the views follow the folder
// shown: entries created, deleted, renamed, written or changed; the directory
// itself deleted or moved). Without inotify, or past the user's watch limit
// (fs.inotify.max_user_watches), nothing is watched and F5 is the way.
#pragma once
#include <memory>
#include <string>

namespace shell {

class DirWatch {
public:
    DirWatch();
    ~DirWatch();
    DirWatch(const DirWatch&) = delete;
    DirWatch& operator=(const DirWatch&) = delete;
    // Follow dir (instead of the previous one); false: not watched. slow: a network folder, whose watch is added on a
    // thread (the UI thread does not wait for the lookups), followed from when it is in.
    bool watch(const std::string& dir, bool slow = false);
    void clear();
    int fd() const;                            // for the event loop (sys::watch_fd); -1 without inotify
    const std::string& dir() const { return dir_; }
    enum { Changed = 1, Gone = 2 };
    // what is pending (read without blocking): Changed (an entry) and Gone (the
    // directory itself was deleted, moved or unmounted) for the directory watched now
    int take();
    struct Shared;
private:
    std::shared_ptr<Shared> s_;
    std::string dir_;
    bool failed_ = false;                      // inotify_init failed: not tried again
};

}
