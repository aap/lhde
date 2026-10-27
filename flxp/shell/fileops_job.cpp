// fileops::Job: run() on a worker thread (see fileops_job.h).
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "fileops_job.h"

namespace shell {
namespace fileops {

struct Job::Impl {
    Op op;
    std::vector<std::string> sources;
    std::string dest;
    Notify notify;
    void* ctx;
    bool ask;
    std::thread th;
    mutable std::mutex m;
    std::condition_variable cv;
    std::atomic<bool> stop{false}, finished{false};
    Progress prog;
    bool has_q = false, has_a = false;
    Conflict q;
    Answer a = Answer::Cancel;
    Result res;
    std::chrono::steady_clock::time_point last;

    void poke() { if (notify) notify(ctx); }

    void work()
    {
        Options o;
        o.cancel = &stop;
        o.progress = [this](const Progress& p) {
            bool tell;
            {
                std::lock_guard<std::mutex> l(m);
                prog = p;
                auto now = std::chrono::steady_clock::now();
                tell = now - last >= std::chrono::milliseconds(50);
                if (tell) last = now;
            }
            if (tell) poke();
        };
        if (ask) o.confirm = [this](const Conflict& c) {
            std::unique_lock<std::mutex> l(m);
            q = c; has_q = true; has_a = false;
            l.unlock();
            poke();
            l.lock();
            cv.wait(l, [this] { return has_a || stop.load(); });
            has_q = false;
            return has_a ? a : Answer::Cancel;
        };
        Result r = run(op, sources, dest, o);
        {
            std::lock_guard<std::mutex> l(m);
            res = r;
        }
        finished = true;
        poke();
    }
};

Job::Job(Op op, const std::vector<std::string>& sources, const std::string& dest_dir, Notify notify, void* ctx, bool ask)
    : impl_(new Impl)
{
    impl_->op = op; impl_->sources = sources; impl_->dest = dest_dir;
    impl_->notify = notify; impl_->ctx = ctx; impl_->ask = ask;
}

Job::~Job()
{
    cancel();
    if (impl_->th.joinable()) impl_->th.join();
    delete impl_;
}

void Job::start()
{
    if (!impl_->th.joinable() && !impl_->finished) impl_->th = std::thread([this] { impl_->work(); });
}

void Job::cancel()
{
    std::lock_guard<std::mutex> l(impl_->m);
    impl_->stop = true;
    impl_->cv.notify_all();
}

bool Job::done() const { return impl_->finished; }

Progress Job::progress() const
{
    std::lock_guard<std::mutex> l(impl_->m);
    return impl_->prog;
}

bool Job::question(Conflict* out) const
{
    std::lock_guard<std::mutex> l(impl_->m);
    if (impl_->has_q && !impl_->has_a && out) *out = impl_->q;
    return impl_->has_q && !impl_->has_a;
}

void Job::answer(Answer a)
{
    std::lock_guard<std::mutex> l(impl_->m);
    if (!impl_->has_q) return;
    impl_->a = a;
    impl_->has_a = true;
    impl_->cv.notify_all();
}

Result Job::result()
{
    if (impl_->th.joinable()) impl_->th.join();
    std::lock_guard<std::mutex> l(impl_->m);
    return impl_->res;
}

} // namespace fileops
} // namespace shell
