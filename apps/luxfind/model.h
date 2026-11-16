// luxfind's model: the catalogues' roots, a search over them on worker threads, whether a root's files can be reached,
// and the commits. Plain C++ without the win32 headers (<thread> and win32's macros do not mix); the window
// (luxfind.cpp) posts itself a message from the notify callback and takes the results on the UI thread.
//
// A search runs on two threads, each with its own catalogue handles: the lister streams the rows of every root asked
// (lhcat_find, the roots one after the other), page rows at a time, and waits when it has given a page until more() asks
// for the next; the counter counts the same query to the end, so the count of matches is known while the first page
// shows. A new start() stops both at their next row (lhcat_query.stop) and begins a new generation; what an older one
// delivered is dropped by take(). The lister brings each catalogue's name index up to date the first time it opens it
// (lhcat_names_update), saying so in the status.
#pragma once
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

namespace luxfind {

struct Root {
    std::string name, path;
    bool catalogue = false;               // the catalogue is the authority (edits wait for a commit)
    bool offline = false;                 // added offline: its path is not reachable from here
};
std::vector<Root> roots();                // every catalogue's root, by name
// the root holding the absolute path (the longest root path that is a prefix of it), null none; *rel the path in it
const Root* root_for(const std::vector<Root>& rs, const std::string& path, std::string* rel);

struct Query {
    std::string name;                     // a part of the name
    std::string type;                     // "" everything, "folders", a type or group, or a comma list of them
    std::string tags;                     // tags separated by commas: each must hold
    std::string root;                     // "" every root
    std::string in;                       // a directory under root ("" the whole root)
    bool empty() const { return name.empty() && type.empty() && tags.empty() && in.empty(); }
};

struct Row {
    std::string root, rel, path;          // the root's name, the path in it, the absolute path under the root's path
    int kind = 'f';
    int64_t size = -1, mtime = -1;
    std::string type;                     // MIME type ("" unknown)
    std::vector<std::pair<std::string, std::string>> attrs;   // the desired values set (names on disk)
    int pending = 0, conflicts = 0;
};

struct Status {
    uint32_t gen = 0;
    int64_t delivered = 0;                // rows given so far
    int64_t count = -1;                   // matches in all (-1 still counting)
    bool listing = false;                 // the lister is at work (not waiting for more(), not done)
    bool done = false;                    // the lister gave every row
    std::string note;                     // "Indexing the names of vishnu..." while it does
    std::string error;
};

class Search {
public:
    typedef void (*Notify)(void* ctx);
    Search(Notify notify, void* ctx, int page = 2000);
    ~Search();
    void start(const Query& q);           // a new generation; the old one stops
    void more();                          // the next page
    void stop();                          // nothing more of this generation
    // UI thread: the rows delivered since the last take and the status now
    void take(std::vector<Row>& rows, Status& st);
    struct Impl;
private:
    Impl* impl_;
};

// Whether a root's directory can be reached: a stat of it on a thread of its own, answered within timeout_ms or taken as
// "no answer"; a thread that hangs (a dead NFS server) is left to itself and no other check of that root starts while it
// runs. Kept for keep_ms. The notify callback runs on that thread.
class Reach {
public:
    enum State { Unknown, Checking, Reachable, Unreachable };
    Reach(void (*notify)(void*), void* ctx, int timeout_ms = 3000, int keep_ms = 30000);
    ~Reach();
    // the state now; starts a check when it is unknown or older than keep_ms (force: now). *why for Unreachable
    State state(const std::string& root, const std::string& path, bool force = false, std::string* why = nullptr);
    void forget(const std::string& root); // check again at the next state()
    struct Impl;
private:
    Impl* impl_;
};

// A commit of a root's pending values (lhcat_commit) on a thread of its own; the result comes to the callback (on that
// thread) as one sentence
void commit(const std::string& root, void (*done)(void* ctx, const std::string& root, bool ok, const std::string& text), void* ctx);

// The catalogue's view of one file for the edits (UI thread, a handle per root of its own): its desired values, and an
// edit recorded as an operation that waits for a commit (lhcat_apply without writing). False with *err a sentence.
bool desired(const std::string& root, const std::string& rel, std::vector<std::pair<std::string, std::string>>* attrs,
             int* pending, int* conflicts);
bool defer_set(const std::string& root, const std::string& rel, const std::string& xattr, const std::string& value,
               std::string* err);
bool defer_tags(const std::string& root, const std::string& rel, const std::vector<std::string>& add,
                const std::vector<std::string>& remove, std::string* err);
int64_t pending_count(const std::string& root);   // attributes with a pending write or a conflict, -1 on an error
std::vector<std::string> catalogue_tags(const std::vector<Root>& rs, int max);   // the catalogues' tags, the most used first
void close_handles();

} // namespace luxfind
