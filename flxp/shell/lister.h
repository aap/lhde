// What a slow folder's entries cost a round trip each to know, read off the UI thread: a folder on a network mount is
// listed from readdir alone (Folder::enumerate_names: names, d_type, the type by the name) and shown at once, and a
// Lister fills in the rest on worker threads: each entry's stat (stat_entry: size, date, folder or file, read-only) and,
// when a view needs them, its stored properties (the xattrs of shell/details.h, the catalogue's where it has them).
// Results come back in batches; the notify callback (a worker thread) wakes the UI thread, which takes them and puts
// them into its list in place (browser.cpp listed()).
//
// The entries are filled in the order they were given (the view's: the top of the list first), by several threads at
// once, as a network file system answers many requests in flight; a new start() drops what the last one left.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include "namespace.h"

namespace shell {

class Lister {
public:
    typedef void (*Notify)(void* ctx);
    enum { THREADS = 8 };
    struct Filled {
        Item item;                  // the entry as given, with what was asked filled in (IsPending cleared, IsStored set)
        bool stat = false;          // its stat was read (gone: the entry no longer exists)
        bool stored = false;        // its stored properties were read
        bool gone = false;
    };
    Lister(Notify notify, void* ctx, int threads = THREADS);
    ~Lister();                      // drops what is queued, joins the threads (after the calls under way)
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
    Impl* impl_;
    Lister(const Lister&);
    Lister& operator=(const Lister&);
};

}
