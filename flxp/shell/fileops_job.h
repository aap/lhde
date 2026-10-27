// The copy/move/delete engine behind paste and drag and drop (shell::paste_items
// and drop_items are thin wrappers over run()). No win32.
//
// - Moves rename on the same filesystem; across filesystems (or when rename
//   says EXDEV) they copy, verify, and only then remove the originals. A source
//   that cannot be removed is an error: the move does not report success.
// - "Into itself" is decided by device+inode of the destination's physical
//   ancestors (bind mounts, symlinked paths, "..", trailing slashes included).
// - Symlinks are copied as symlinks (never followed); a directory met twice on
//   the current path (a bind-mount loop) is an error, not a recursion.
// - Every path the operation creates is recorded in creation order; when an
//   item fails or is cancelled, exactly those are removed, newest first, and
//   nothing that existed before is touched. Files are written to
//   "name.flxp-part" in the target folder and renamed into place.
// - Copies keep the mode (setuid/setgid/sticky where allowed), atime/mtime in
//   nanoseconds, the owner when run as root (else the group if allowed),
//   extended attributes (user.*; others best effort) and holes in sparse files.
//   Directories get their times after their contents are in.
// - Name clashes: without a confirm callback the copy gets Explorer's
//   "Copy of x" name (as before); with one, XP's Confirm File/Folder Replace:
//   Yes / Yes to All / No / No to All / Cancel, folders merge.
#pragma once
#include <stdint.h>
// includable after windows.h (whose min/max macros break <functional>)
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#pragma pop_macro("max")
#pragma pop_macro("min")

namespace shell {
namespace fileops {

enum class Op { Copy, Move, Trash, Remove };   // Remove: permanent delete (never crosses into other filesystems)
enum class Answer { Yes, YesToAll, No, NoToAll, Cancel };

struct Conflict {
    std::string src, dst;           // the item and the existing one it would replace
    bool folder = false;            // both folders: Yes merges (XP's Confirm Folder Replace)
    uint64_t src_size = 0, dst_size = 0;
    int64_t src_mtime = 0, dst_mtime = 0;   // seconds
};

struct Progress {
    uint64_t bytes_done = 0, bytes_total = 0;   // totals 0 while unknown (count = false)
    uint64_t items_done = 0, items_total = 0;   // every file, folder and link
    std::string current;                        // the source being worked on
};

struct Error {
    std::string path;
    int code = 0;                   // errno
    std::string what;               // "Cannot copy", "Cannot remove the original of", ...
    std::string text() const;       // "Cannot copy /a/b: Permission denied"
};

struct Result {
    bool ok = true;                 // no errors and not cancelled
    bool cancelled = false;
    std::vector<Error> errors;      // one per failure, in order
    std::vector<std::string> created;   // where each completed top-level item now is
    std::string summary(size_t max_lines = 8) const;   // the errors as one message ("" when ok)
};

struct Options {
    std::function<Answer(const Conflict&)> confirm;     // null: "Copy of x" naming
    std::function<void(const Progress&)> progress;      // called on the working thread
    const std::atomic<bool>* cancel = nullptr;          // checked between files and every 1 MiB
    bool count = true;              // walk the sources first for the totals (only with progress)
    bool rollback = true;           // a failed item removes what it created
};

// Do it on the calling thread. dest_dir is ignored for Trash/Remove.
Result run(Op op, const std::vector<std::string>& sources, const std::string& dest_dir, const Options& opts = Options());

// Physical containment by device+inode: path is dir or below it.
bool is_inside(const std::string& path, const std::string& dir);
// The two paths name the same file (device+inode; symlinks not followed for the last part).
bool same_file(const std::string& a, const std::string& b);

struct Totals { uint64_t bytes = 0, items = 0; bool complete = true; };
Totals count(const std::vector<std::string>& sources, uint64_t max_items = UINT64_MAX);
// More than a handful: a caller should use a Job (a cross-fs move or copy of
// more than 32 items or 8 MiB; trash/same-fs moves of more than 64 items).
bool worth_a_thread(Op op, const std::vector<std::string>& sources, const std::string& dest_dir);

// run() on a worker thread. notify(ctx) is called on the worker when there is
// news (progress, throttled to ~20/s; a question; completion): post a message
// to the UI thread from it (win::post / PostMessage are thread-safe) and read
// the state there.
class Job {
public:
    typedef void (*Notify)(void* ctx);
    Job(Op op, const std::vector<std::string>& sources, const std::string& dest_dir, Notify notify, void* ctx, bool ask = false);
    ~Job();                         // cancels and waits
    void start();
    void cancel();
    bool done() const;
    Progress progress() const;
    // ask = true: name clashes wait for answer() (the worker blocks; cancel()
    // answers Cancel). question() is the pending one, if any.
    bool question(Conflict* out) const;
    void answer(Answer a);
    Result result();                // waits for the worker
    struct Impl;
private:
    Impl* impl_;
};

} // namespace fileops
} // namespace shell
