// What a slow folder's entries cost a round trip each to know, read off the UI thread: a folder on a network mount is
// listed from readdir alone (Folder::enumerate_names: names, d_type, the type by the name) and shown at once, and a
// Lister fills in the rest on worker threads: each entry's stat (stat_entry: size, date, folder or file, read-only) and,
// when a view needs them, its stored properties (the xattrs of shell/details.h, the catalogue's where it has them).
// Results come back in batches; the notify callback (a worker thread) wakes the UI thread, which takes them and puts
// them into its list in place (browser.cpp listed()).
//
// The entries are filled in the order they were given (the view's: the top of the list first), by several threads at
// once, as a network file system answers many requests in flight; a new start() drops what the last one left.
//
// A Reader reads a slow folder's names themselves off the UI thread (opening it, the folder tree's look inside one, a
// change seen on disk): the opendir, which also says it still is a folder, and the readdir, one round trip per batch of
// entries. XP's Explorer does not block its message loop on a share either: the window shows the folder at once and
// its list when the names are in.
#pragma once
#include <stdint.h>
#include <memory>
#include <string>
#include <vector>
#include "namespace.h"

namespace shell {

typedef void (*Notify)(void* ctx);

class Lister {
public:
    typedef shell::Notify Notify;
    enum { THREADS = 8 };
    struct Filled {
        Item item;                  // the entry as given, with what was asked filled in (IsPending cleared, IsStored set)
        bool stat = false;          // its stat was read (gone: the entry no longer exists)
        bool stored = false;        // its stored properties were read
        bool gone = false;
    };
    Lister(Notify notify, void* ctx, int threads = THREADS);
    // drops what is queued and joins the threads once the calls under way return; a thread still in a call after a
    // moment (a server that does not answer) is left to end on its own
    ~Lister();
    // A folder's entries to fill in: a new generation, everything queued before dropped. stat: the IsPending ones get
    // their stat; stored: every one its stored properties (folder: where the catalogue is looked up).
    void start(const std::string& folder, const std::vector<Item>& items, bool stat, bool stored);
    // More for the same folder (the stored properties a column now needs, the selection's): queued after what is
    // queued, an entry already queued for the same is not queued again
    void add(const std::vector<Item>& items, bool stat, bool stored);
    void cancel();
    bool take(std::vector<Filled>& out);   // UI thread: everything filled since the last take (this generation's)
    bool busy() const;              // entries queued or being filled
    size_t pending() const;         // entries queued or being filled
    struct Impl;                    // (<thread> kept out of here: win32's min/max)
private:
    std::shared_ptr<Impl> impl_;    // (shared with the threads: one left in a call outlives the Lister)
    Lister(const Lister&);
    Lister& operator=(const Lister&);
};

// A request is read on a thread of its own (READERS at most at once: one more waits for one of them to return) and its
// result taken by the UI thread, which wakes on notify (called from a reader thread). A request cancelled before its
// answer is in is never delivered; a thread stuck in a call to a server that does not answer is abandoned, never
// joined: it ends when the call returns, its answer dropped, and the Reader may be gone by then. So a hung folder holds
// one thread and no window, and the next folder is read by another.
class Reader {
public:
    enum { READERS = 4 };
    struct Result {
        uint32_t ticket = 0;
        std::string path;
        bool ok = false;            // read: a folder (else error, an errno: ENOENT, ENOTDIR, EACCES, ...)
        int error = 0;
        std::vector<Item> items;    // read_names' entries, their type and icon by the name (name_types)
    };
    Reader(Notify notify, void* ctx);
    ~Reader();                      // nothing more is delivered; threads still in a call are left to end on their own
    uint32_t read(const std::string& path, bool include_hidden);   // a ticket, never 0
    void cancel(uint32_t ticket);   // 0: every request
    bool take(std::vector<Result>& out);   // UI thread: the results in since the last take
    size_t running() const;         // threads in a call (stuck ones included)
    struct State;
private:
    std::shared_ptr<State> st_;
    Reader(const Reader&);
    Reader& operator=(const Reader&);
};

}
