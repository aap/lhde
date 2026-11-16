#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <thread>
#include "lister.h"
#include "details.h"

namespace shell {

// a worker takes this many entries at a time (one catalogue lookup for their stored properties)
enum { CHUNK = 16 };

struct Lister::Impl {
    struct Job { Item item; bool stat, stored; };
    Notify notify;
    void* ctx;
    mutable std::mutex lock;
    std::condition_variable cv, idle;
    std::deque<Job> jobs;
    std::set<std::pair<std::string, int>> queued;    // (path, what) queued and not taken yet
    std::vector<Filled> done;
    std::vector<std::thread> threads;
    std::string folder;
    uint32_t gen = 1;
    int working = 0;                                 // entries taken by a thread and not done
    bool quit = false;
    int want_threads;
    static void run(std::shared_ptr<Impl> self);
    void queue(const std::vector<Item>& items, bool stat, bool stored, const std::shared_ptr<Impl>& self);   // (under lock)
};

static int what(bool stat, bool stored) { return (stat ? 1 : 0) | (stored ? 2 : 0); }

Lister::Lister(Notify notify, void* ctx, int threads) : impl_(std::make_shared<Impl>())
{
    impl_->notify = notify;
    impl_->ctx = ctx;
    impl_->want_threads = threads > 0 ? threads : 1;
}

Lister::~Lister()
{
    Impl& m = *impl_;
    bool stuck;
    {
        std::unique_lock<std::mutex> l(m.lock);
        m.quit = true;
        m.notify = nullptr;
        m.jobs.clear();
        m.cv.notify_all();
        // the calls under way a moment to return (a local disk's, a server's that answers)
        stuck = !m.idle.wait_for(l, std::chrono::milliseconds(500), [&m] { return m.working == 0; });
    }
    for (std::thread& t : m.threads) { if (stuck) t.detach(); else t.join(); }
    m.threads.clear();
}

void Lister::Impl::queue(const std::vector<Item>& items, bool stat, bool stored, const std::shared_ptr<Impl>& self)
{
    for (const Item& it : items) {
        if (it.flags & (IsVirtual | IsDrive)) continue;
        bool s = stat && (it.flags & IsPending), x = stored;
        if (!s && !x) continue;
        if (!queued.insert(std::make_pair(it.path, what(s, x))).second) continue;
        jobs.push_back(Job{ it, s, x });
    }
    // the threads, the first time there is work (a Lister that is never started costs none)
    while ((int)threads.size() < want_threads && !jobs.empty()) threads.emplace_back(run, self);
}

void Lister::start(const std::string& folder, const std::vector<Item>& items, bool stat, bool stored)
{
    {
        std::lock_guard<std::mutex> l(impl_->lock);
        impl_->gen++;
        impl_->jobs.clear();
        impl_->queued.clear();
        impl_->done.clear();
        impl_->folder = folder;
        impl_->queue(items, stat, stored, impl_);
    }
    impl_->cv.notify_all();
}

void Lister::add(const std::vector<Item>& items, bool stat, bool stored)
{
    {
        std::lock_guard<std::mutex> l(impl_->lock);
        impl_->queue(items, stat, stored, impl_);
    }
    impl_->cv.notify_all();
}

void Lister::cancel()
{
    std::lock_guard<std::mutex> l(impl_->lock);
    impl_->gen++;
    impl_->jobs.clear();
    impl_->queued.clear();
    impl_->done.clear();
}

bool Lister::take(std::vector<Filled>& out)
{
    std::lock_guard<std::mutex> l(impl_->lock);
    out.swap(impl_->done);
    impl_->done.clear();
    return !out.empty();
}

bool Lister::busy() const
{
    std::lock_guard<std::mutex> l(impl_->lock);
    return !impl_->jobs.empty() || impl_->working > 0;
}

size_t Lister::pending() const
{
    std::lock_guard<std::mutex> l(impl_->lock);
    return impl_->jobs.size() + (size_t)impl_->working;
}

void Lister::Impl::run(std::shared_ptr<Impl> self)
{
    Impl& m = *self;
    for (;;) {
        std::vector<Job> mine;
        uint32_t g;
        std::string dir;
        {
            std::unique_lock<std::mutex> l(m.lock);
            m.cv.wait(l, [&m] { return m.quit || !m.jobs.empty(); });
            if (m.quit) return;
            while (!m.jobs.empty() && mine.size() < CHUNK) {
                Job j = m.jobs.front();
                m.jobs.pop_front();
                m.queued.erase(std::make_pair(j.item.path, what(j.stat, j.stored)));
                mine.push_back(j);
            }
            g = m.gen;
            dir = m.folder;
            m.working += (int)mine.size();
        }
        std::vector<Filled> out(mine.size());
        std::vector<Item> want_stored;
        for (size_t i = 0; i < mine.size(); i++) {
            out[i].item = mine[i].item;
            if (mine[i].stat) {
                out[i].stat = true;
                out[i].gone = !stat_entry(out[i].item);
            }
            if (mine[i].stored && !out[i].gone) want_stored.push_back(out[i].item);
        }
        if (!want_stored.empty()) {
            details::read_stored(want_stored, dir);   // the catalogue's values where it has them, else the xattrs
            for (size_t i = 0, k = 0; i < mine.size(); i++) {
                if (!mine[i].stored || out[i].gone) continue;
                out[i].item.xattrs = want_stored[k++].xattrs;
                out[i].item.flags |= IsStored;
                out[i].stored = true;
            }
        }
        std::lock_guard<std::mutex> l(m.lock);
        m.working -= (int)mine.size();
        if (m.working == 0) m.idle.notify_all();
        if (g != m.gen || m.quit) continue;
        bool wake = m.done.empty();
        for (Filled& f : out) m.done.push_back(std::move(f));
        if (wake && m.notify) m.notify(m.ctx);   // one wake-up per batch: the UI takes everything there is
    }
}

// ---- Reader ---------------------------------------------------------------------------------------------------------

struct Reader::State {
    struct Request { uint32_t ticket; std::string path; bool hidden; };
    mutable std::mutex lock;
    Notify notify;
    void* ctx;
    std::deque<Request> queue;
    std::set<uint32_t> live;             // asked for, not cancelled, not delivered
    std::vector<Result> done;
    uint32_t next = 0;
    size_t running = 0;                  // threads (each in a call, or taking the next request)
    bool trace = getenv("FLXP_TRACE_LIST") != nullptr;
    static void run(std::shared_ptr<State> self);
};

Reader::Reader(Notify notify, void* ctx) : st_(std::make_shared<State>())
{
    st_->notify = notify;
    st_->ctx = ctx;
}

Reader::~Reader()
{
    std::lock_guard<std::mutex> l(st_->lock);
    st_->notify = nullptr;
    st_->queue.clear();
    st_->live.clear();
    st_->done.clear();
}

uint32_t Reader::read(const std::string& path, bool include_hidden)
{
    std::lock_guard<std::mutex> l(st_->lock);
    uint32_t t = ++st_->next;
    if (!t) t = ++st_->next;
    st_->live.insert(t);
    st_->queue.push_back(State::Request{ t, path, include_hidden });
    if (st_->running < READERS) {
        st_->running++;
        std::thread(State::run, st_).detach();
    }
    return t;
}

void Reader::cancel(uint32_t ticket)
{
    std::lock_guard<std::mutex> l(st_->lock);
    State& s = *st_;
    if (!ticket) { s.live.clear(); s.queue.clear(); s.done.clear(); return; }
    s.live.erase(ticket);
    for (auto i = s.queue.begin(); i != s.queue.end();) { if (i->ticket == ticket) i = s.queue.erase(i); else ++i; }
    for (auto i = s.done.begin(); i != s.done.end();) { if (i->ticket == ticket) i = s.done.erase(i); else ++i; }
}

bool Reader::take(std::vector<Result>& out)
{
    {
        std::lock_guard<std::mutex> l(st_->lock);
        out.swap(st_->done);
        st_->done.clear();
    }
    for (Result& r : out) name_types(r.items);   // (here: the MIME database is the UI thread's)
    return !out.empty();
}

size_t Reader::running() const
{
    std::lock_guard<std::mutex> l(st_->lock);
    return st_->running;
}

void Reader::State::run(std::shared_ptr<State> self)
{
    State& s = *self;
    for (;;) {
        Request q;
        {
            std::lock_guard<std::mutex> l(s.lock);
            if (s.queue.empty()) { s.running--; return; }
            q = s.queue.front();
            s.queue.pop_front();
        }
        Result r;
        r.ticket = q.ticket;
        r.path = q.path;
        r.ok = read_names(q.path, r.items, q.hidden, &r.error);
        std::lock_guard<std::mutex> l(s.lock);
        if (!s.live.erase(q.ticket)) {   // cancelled while it was read (the user went elsewhere): dropped
            if (s.trace) { fprintf(stderr, "read: %s dropped (cancelled)\n", q.path.c_str()); fflush(stderr); }
            continue;
        }
        bool wake = s.done.empty();
        s.done.push_back(std::move(r));
        if (wake && s.notify) s.notify(s.ctx);
    }
}

}
