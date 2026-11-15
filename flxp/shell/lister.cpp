#include <stdio.h>
#include <atomic>
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
    std::condition_variable cv;
    std::deque<Job> jobs;
    std::set<std::pair<std::string, int>> queued;    // (path, what) queued and not taken yet
    std::vector<Filled> done;
    std::vector<std::thread> threads;
    std::string folder;
    uint32_t gen = 1;
    int working = 0;                                 // entries taken by a thread and not done
    bool quit = false;
    int want_threads;
    void run();
    void queue(const std::vector<Item>& items, bool stat, bool stored);   // (under lock)
};

static int what(bool stat, bool stored) { return (stat ? 1 : 0) | (stored ? 2 : 0); }

Lister::Lister(Notify notify, void* ctx, int threads) : impl_(new Impl)
{
    impl_->notify = notify;
    impl_->ctx = ctx;
    impl_->want_threads = threads > 0 ? threads : 1;
}

Lister::~Lister()
{
    {
        std::lock_guard<std::mutex> l(impl_->lock);
        impl_->quit = true;
        impl_->jobs.clear();
    }
    impl_->cv.notify_all();
    for (std::thread& t : impl_->threads) t.join();
    delete impl_;
}

void Lister::Impl::queue(const std::vector<Item>& items, bool stat, bool stored)
{
    for (const Item& it : items) {
        if (it.flags & (IsVirtual | IsDrive)) continue;
        bool s = stat && (it.flags & IsPending), x = stored;
        if (!s && !x) continue;
        if (!queued.insert(std::make_pair(it.path, what(s, x))).second) continue;
        jobs.push_back(Job{ it, s, x });
    }
    // the threads, the first time there is work (a Lister that is never started costs none)
    while ((int)threads.size() < want_threads && !jobs.empty()) threads.emplace_back([this] { run(); });
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
        impl_->queue(items, stat, stored);
    }
    impl_->cv.notify_all();
}

void Lister::add(const std::vector<Item>& items, bool stat, bool stored)
{
    {
        std::lock_guard<std::mutex> l(impl_->lock);
        impl_->queue(items, stat, stored);
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

void Lister::Impl::run()
{
    for (;;) {
        std::vector<Job> mine;
        uint32_t g;
        std::string dir;
        {
            std::unique_lock<std::mutex> l(lock);
            cv.wait(l, [this] { return quit || !jobs.empty(); });
            if (quit) return;
            while (!jobs.empty() && mine.size() < CHUNK) {
                Job j = jobs.front();
                jobs.pop_front();
                queued.erase(std::make_pair(j.item.path, what(j.stat, j.stored)));
                mine.push_back(j);
            }
            g = gen;
            dir = folder;
            working += (int)mine.size();
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
        bool wake;
        {
            std::lock_guard<std::mutex> l(lock);
            working -= (int)mine.size();
            if (g != gen || quit) continue;
            wake = done.empty();
            for (Filled& f : out) done.push_back(std::move(f));
        }
        if (wake && notify) notify(ctx);   // one wake-up per batch: the UI takes everything there is
    }
}

}
