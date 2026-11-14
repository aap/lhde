// luxview's view model (model.h): plain C++ with a thread, no win32 headers (their min/max clash with <thread>).
#include <dirent.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include "model.h"
#include "gdi/image.h"
#include "shell/namespace.h"
#include "shell/thumbs.h"
#include "shell/util.h"

namespace luxview {

// ---- the list ---------------------------------------------------------------------------------------------------------

bool is_picture(const std::string& path) { return shell::thumbs::own_picture(path); }

static std::string absolute(const std::string& p)
{
    if (p.empty() || p[0] == '/') return p;
    char buf[PATH_MAX];
    if (!getcwd(buf, sizeof buf)) return p;
    std::string a = std::string(buf) + "/" + p;
    // ./x and a trailing slash
    for (size_t i; (i = a.find("/./")) != std::string::npos;) a.erase(i, 2);
    while (a.size() > 1 && a.back() == '/') a.pop_back();
    return a;
}

static std::string base_name(const std::string& p)
{
    size_t s = p.rfind('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}

std::vector<std::string> folder_pictures(const std::string& dir)
{
    std::vector<shell::Item> items;
    if (DIR* d = opendir(dir.c_str())) {
        while (struct dirent* e = readdir(d)) {
            if (e->d_name[0] == '.' || !is_picture(e->d_name)) continue;
            shell::Item it;
            it.name = e->d_name;
            it.path = (dir == "/" ? "" : dir) + "/" + e->d_name;
            if (!shell::is_file(it.path)) continue;
            items.push_back(it);
        }
        closedir(d);
    }
    shell::sort_items(items, shell::SortBy::Name, true);
    std::vector<std::string> out;
    for (const shell::Item& it : items) out.push_back(it.path);
    return out;
}

std::vector<std::string> list_for(const std::vector<std::string>& args, size_t* start)
{
    std::vector<std::string> out;
    *start = 0;
    if (args.size() == 1) {
        std::string a = absolute(args[0]);
        if (shell::is_dir(a)) return folder_pictures(a);
        std::string name = base_name(a);
        if (name.empty() || name[0] == '.' || !is_picture(a)) { out.push_back(a); return out; }
        size_t s = a.rfind('/');
        out = folder_pictures(s == 0 ? "/" : a.substr(0, s));
        auto it = std::find(out.begin(), out.end(), a);
        if (it == out.end()) { out.clear(); out.push_back(a); return out; }   // not there (yet): the file alone
        *start = (size_t)(it - out.begin());
        return out;
    }
    for (const std::string& arg : args) {
        std::string a = absolute(arg);
        if (shell::is_dir(a)) for (const std::string& f : folder_pictures(a)) out.push_back(f);
        else out.push_back(a);
    }
    return out;
}

// ---- zoom --------------------------------------------------------------------------------------------------------------

const int ZOOM_STEPS[] = { 5, 10, 25, 50, 100, 200, 300, 500, 1000, 2000, 4000 };
const int ZOOM_STEP_COUNT = (int)(sizeof ZOOM_STEPS / sizeof ZOOM_STEPS[0]);

double zoom_step_in(double z)
{
    for (int i = 0; i < ZOOM_STEP_COUNT; i++)
        if (ZOOM_STEPS[i] > z * 100 + 0.5) return ZOOM_STEPS[i] / 100.0;
    return ZOOM_STEPS[ZOOM_STEP_COUNT - 1] / 100.0;
}

double zoom_step_out(double z)
{
    for (int i = ZOOM_STEP_COUNT - 1; i >= 0; i--)
        if (ZOOM_STEPS[i] < z * 100 - 0.5) return ZOOM_STEPS[i] / 100.0;
    return ZOOM_STEPS[0] / 100.0;
}

void fit_into(int w, int h, int bw, int bh, int* ow, int* oh)
{
    gdi::fit_size(w > 0 ? w : 1, h > 0 ? h : 1, bw > 0 ? bw : 1, bh > 0 ? bh : 1, false, ow, oh);
}

// ---- the default decoder ---------------------------------------------------------------------------------------------

static gdi::Bitmap* decode_fit(const std::string& path, int bw, int bh, int* ow, int* oh, void*)
{
    // the decoders' max_size keeps both sides of the result at least that large: the shorter side of the fitted size,
    // for either orientation (the header's size is before the EXIF rotation)
    int pw = 0, ph = 0, max_size = 0;
    if (shell::thumbs::picture_size(path, &pw, &ph) && pw > 0 && ph > 0) {
        double s = std::max(std::min((double)bw / pw, (double)bh / ph), std::min((double)bw / ph, (double)bh / pw));
        if (s < 1) max_size = (int)ceil(s * std::min(pw, ph));
    }
    gdi::Bitmap* b;
    {
        shell::thumbs::DecodeTurn turn;
        if (Loader::stopping()) return nullptr;
        b = gdi::load_image(path, max_size, ow, oh);
    }
    if (!b) return nullptr;
    if (*ow <= 0 || *oh <= 0) { *ow = b->width; *oh = b->height; }
    int fw, fh;
    fit_into(*ow, *oh, bw, bh, &fw, &fh);
    if (fw < b->width && fh < b->height && !Loader::stopping()) {
        gdi::Bitmap* s = gdi::resample(b, fw, fh);
        if (s) { gdi::destroy_bitmap(b); b = s; }
    }
    return b;
}

static gdi::Bitmap* cached_thumbnail(const std::string& path, int* ow, int* oh, void*)
{
    *ow = *oh = 0;
    // only one that is there: making one is a decode like the picture's own
    if (!shell::exists(shell::thumbs::cache_path(path, 256)) && !shell::exists(shell::thumbs::cache_path(path, 128)))
        return nullptr;
    gdi::Bitmap* b = shell::thumbs::get(path, 0, 256);
    if (b) shell::thumbs::picture_size(path, ow, oh);
    return b;
}

Decoder default_decoder()
{
    Decoder d;
    d.decode = decode_fit;
    d.preview = cached_thumbnail;
    return d;
}

// ---- the loader --------------------------------------------------------------------------------------------------------

struct Job {
    uint32_t gen = 0;
    Loader::Kind kind = Loader::CURRENT;
    std::string path;
    int box_w = 0, box_h = 0;
};

struct Loader::Impl {
    Notify notify;
    void* ctx;
    Decoder dec;
    mutable std::mutex lock;
    std::condition_variable cv;
    std::deque<Job> jobs;
    std::deque<Result> done;
    Job running;                        // the job the thread runs (its gen and kind may be changed by show)
    bool working = false;
    uint32_t gen = 1;
    bool quit = false;
    int decodes = 0;
    std::thread thread;
    void run();
};

static thread_local Loader::Impl* t_loader;

void free_result(Loader::Result& r)
{
    if (r.bmp) gdi::destroy_bitmap(r.bmp);
    r.bmp = nullptr;
}

Loader::Loader(Notify notify, void* ctx, const Decoder& d) : impl_(new Impl)
{
    impl_->notify = notify;
    impl_->ctx = ctx;
    impl_->dec = d;
    impl_->thread = std::thread([this] { impl_->run(); });
}

Loader::~Loader()
{
    {
        std::lock_guard<std::mutex> l(impl_->lock);
        impl_->quit = true;
        impl_->jobs.clear();
    }
    impl_->cv.notify_all();
    impl_->thread.join();
    for (Result& r : impl_->done) free_result(r);
    delete impl_;
}

bool Loader::stopping()
{
    Impl* p = t_loader;
    if (!p) return false;
    std::lock_guard<std::mutex> l(p->lock);
    return p->quit || (p->running.kind != PREFETCH && p->running.gen != p->gen);
}

uint32_t Loader::show(const std::string& path, int box_w, int box_h, bool preview, bool decode)
{
    uint32_t g;
    {
        std::lock_guard<std::mutex> l(impl_->lock);
        g = ++impl_->gen;
        // a prefetch of this picture, under way or queued, at least as large, is this picture's decode
        bool have = false;
        if (decode && impl_->working && impl_->running.path == path && impl_->running.kind != PREVIEW &&
            impl_->running.box_w >= box_w && impl_->running.box_h >= box_h) {
            impl_->running.gen = g;
            impl_->running.kind = CURRENT;
            have = true;
        }
        std::deque<Job> keep;
        for (const Job& j : impl_->jobs)
            if (!have && decode && j.kind == PREFETCH && j.path == path && j.box_w >= box_w && j.box_h >= box_h) {
                Job c = j; c.gen = g; c.kind = CURRENT; keep.push_back(c); have = true;
            }
        impl_->jobs.swap(keep);
        for (Result& r : impl_->done) free_result(r);
        impl_->done.clear();
        if (preview) impl_->jobs.push_front(Job{ g, PREVIEW, path, box_w, box_h });
        if (decode && !have) impl_->jobs.push_back(Job{ g, CURRENT, path, box_w, box_h });
    }
    impl_->cv.notify_one();
    return g;
}

void Loader::request(Kind kind, const std::string& path, int box_w, int box_h)
{
    {
        std::lock_guard<std::mutex> l(impl_->lock);
        Job j{ impl_->gen, kind, path, box_w, box_h };
        // a larger decode of the shown picture goes before a prefetch
        if (kind == CURRENT) {
            auto at = impl_->jobs.begin();
            while (at != impl_->jobs.end() && at->kind != PREFETCH) ++at;
            impl_->jobs.insert(at, j);
        } else impl_->jobs.push_back(j);
    }
    impl_->cv.notify_one();
}

void Loader::cancel_prefetch()
{
    std::lock_guard<std::mutex> l(impl_->lock);
    std::deque<Job> keep;
    for (const Job& j : impl_->jobs) if (j.kind != PREFETCH) keep.push_back(j);
    impl_->jobs.swap(keep);
    std::deque<Result> kept;
    for (Result& r : impl_->done) { if (r.kind == PREFETCH) free_result(r); else kept.push_back(r); }
    impl_->done.swap(kept);
}

bool Loader::take(Result& out)
{
    std::lock_guard<std::mutex> l(impl_->lock);
    while (!impl_->done.empty()) {
        out = impl_->done.front();
        impl_->done.pop_front();
        if (out.kind == PREFETCH || out.gen == impl_->gen) return true;
        free_result(out);   // an older picture's
    }
    return false;
}

uint32_t Loader::generation() const
{
    std::lock_guard<std::mutex> l(impl_->lock);
    return impl_->gen;
}

bool Loader::busy() const
{
    std::lock_guard<std::mutex> l(impl_->lock);
    return impl_->working || !impl_->jobs.empty();
}

int Loader::decodes() const
{
    std::lock_guard<std::mutex> l(impl_->lock);
    return impl_->decodes;
}

void Loader::Impl::run()
{
    t_loader = this;
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> l(lock);
            cv.wait(l, [this] { return quit || !jobs.empty(); });
            if (quit) return;
            job = jobs.front();
            jobs.pop_front();
            if (job.kind != PREFETCH && job.gen != gen) continue;
            running = job;
            working = true;
            if (job.kind != PREVIEW) decodes++;
        }
        Result r;
        r.path = job.path;
        r.box_w = job.box_w; r.box_h = job.box_h;
        if (job.kind == PREVIEW) { if (dec.preview) r.bmp = dec.preview(job.path, &r.w, &r.h, dec.ctx); }
        else if (dec.decode) r.bmp = dec.decode(job.path, job.box_w, job.box_h, &r.w, &r.h, dec.ctx);
        bool wake;
        {
            std::lock_guard<std::mutex> l(lock);
            working = false;
            r.gen = running.gen;        // show() may have made a prefetch this picture's decode
            r.kind = running.kind;
            if (quit || (r.kind != PREFETCH && r.gen != gen)) { free_result(r); continue; }
            wake = done.empty();
            done.push_back(r);
        }
        if (wake && notify) notify(ctx);
    }
}

// ---- the viewer --------------------------------------------------------------------------------------------------------

static gdi::Bitmap* rotated(gdi::Bitmap* s, int quarters)
{
    quarters &= 3;
    if (!s || !quarters) return s;
    int w = s->width, h = s->height;
    gdi::Bitmap* d = quarters == 2 ? gdi::create_bitmap(w, h) : gdi::create_bitmap(h, w);
    if (!d) return s;
    for (int y = 0; y < h; y++) {
        const uint32_t* row = s->row(y);
        for (int x = 0; x < w; x++) {
            int dx, dy;
            if (quarters == 1) { dx = h - 1 - y; dy = x; }          // clockwise
            else if (quarters == 2) { dx = w - 1 - x; dy = h - 1 - y; }
            else { dx = y; dy = w - 1 - x; }                        // counterclockwise
            d->row(dy)[dx] = row[x];
        }
    }
    gdi::destroy_bitmap(s);
    return d;
}

static size_t bitmap_bytes(const gdi::Bitmap* b) { return b ? (size_t)b->width * b->height * 4 : 0; }

Viewer::Viewer(Loader::Notify notify, void* ctx, const Decoder& d) : loader_(notify, ctx, d) {}

Viewer::~Viewer()
{
    drop(cur_);
    drop(pre_);
}

void Viewer::drop(Pic& p)
{
    if (p.bmp) gdi::destroy_bitmap(p.bmp);
    p = Pic();
}

void Viewer::set_files(const std::vector<std::string>& files, size_t start)
{
    files_ = files;
    drop(cur_);
    drop(pre_);
    pre_asked_.clear();
    index_ = (size_t)-1;   // nothing shown: go() shows the first
    if (files_.empty()) index_ = 0;
    else go(std::min(start, files_.size() - 1));
}

int Viewer::picture_w() const { return (rot_ & 1) ? cur_.h : cur_.w; }
int Viewer::picture_h() const { return (rot_ & 1) ? cur_.w : cur_.h; }

double Viewer::zoom() const
{
    if (!fit_) return zoom_;
    int pw = picture_w(), ph = picture_h();
    if (pw <= 0 || ph <= 0) {
        if (!cur_.bmp) return 1;
        pw = cur_.bmp->width; ph = cur_.bmp->height;   // a preview of unknown size: as large as it fits
        return std::min((double)vw_ / pw, (double)vh_ / ph);
    }
    int fw, fh;
    fit_into(pw, ph, vw_, vh_, &fw, &fh);
    return (double)fw / pw;
}

// the decode the view needs, in the picture's own orientation (the loader's)
void Viewer::wanted(int* bw, int* bh) const
{
    int w = vw_, h = vh_;
    if (!fit_ && cur_.w > 0 && cur_.h > 0) {
        double z = std::min(zoom_, 1.0);
        int pw = picture_w(), ph = picture_h();
        w = (int)ceil(pw * z); h = (int)ceil(ph * z);
        long long px = (long long)w * h;
        if (px > MAX_PIXELS) {
            double s = sqrt((double)MAX_PIXELS / px);
            w = (int)(w * s); h = (int)(h * s);
        }
        w = std::max(w, 1); h = std::max(h, 1);
    }
    if (rot_ & 1) std::swap(w, h);
    *bw = w; *bh = h;
}

// the shown bitmap is as large as the view needs (or the picture itself)
bool Viewer::enough() const
{
    if (!cur_.bmp || cur_.preview) return false;
    int bw, bh;
    wanted(&bw, &bh);
    if (cur_.w <= 0 || cur_.h <= 0) return true;
    int fw, fh;
    fit_into(cur_.w, cur_.h, bw, bh, &fw, &fh);
    int have = (rot_ & 1) ? cur_.bmp->height : cur_.bmp->width;   // the bitmap is rotated, cur_.w is not
    return have >= fw || have >= cur_.w;
}

bool Viewer::loading() const { return !failed_ && !enough(); }

std::string Viewer::neighbour() const
{
    if (files_.size() < 2) return std::string();
    size_t n = dir_ >= 0 ? (index_ + 1) % files_.size() : (index_ + files_.size() - 1) % files_.size();
    return files_[n];
}

void Viewer::ensure()
{
    if (files_.empty()) return;
    int bw, bh;
    wanted(&bw, &bh);
    if (!enough() && !failed_) {
        // asked already at least that large: wait for it
        if (asked_w_ < bw || asked_h_ < bh) {
            loader_.request(Loader::CURRENT, path(), bw, bh);
            asked_w_ = bw; asked_h_ = bh;
        }
        return;
    }
    // a decode far larger than the view needs now (Actual Size, then Best Fit) and past the budget: a smaller one replaces it
    if (cur_.bmp && bitmap_bytes(cur_.bmp) > BUDGET && cur_.w > 0 && cur_.h > 0 && !asked_w_) {
        int fw, fh;
        fit_into(cur_.w, cur_.h, bw, bh, &fw, &fh);
        if ((size_t)fw * fh * 4 * 4 <= bitmap_bytes(cur_.bmp)) {
            loader_.request(Loader::CURRENT, path(), bw, bh);
            asked_w_ = bw; asked_h_ = bh;
        }
    }
    // complete: the neighbour, when both fit the budget at the fitted size
    std::string n = neighbour();
    size_t need = bitmap_bytes(cur_.bmp) + (size_t)vw_ * vh_ * 4;
    if (n.empty() || need > BUDGET) {
        if (pre_.bmp || !pre_asked_.empty()) { drop(pre_); pre_asked_.clear(); loader_.cancel_prefetch(); }
        return;
    }
    if (pre_.bmp && pre_.path == n && pre_.box_w >= vw_ && pre_.box_h >= vh_) return;
    if (pre_asked_ == n) return;
    if (pre_.bmp && pre_.path != n) drop(pre_);
    loader_.cancel_prefetch();
    loader_.request(Loader::PREFETCH, n, vw_, vh_);
    pre_asked_ = n;
}

void Viewer::install(Pic& from)
{
    gdi::Bitmap* b = rotated(from.bmp, rot_);
    from.bmp = nullptr;
    drop(cur_);
    cur_ = from;
    cur_.bmp = b;
    from = Pic();
}

bool Viewer::go(size_t i)
{
    if (i >= files_.size() || i == index_) return false;
    if (index_ < files_.size()) {
        size_t n = files_.size();
        dir_ = (i == (index_ + 1) % n) ? 1 : (i == (index_ + n - 1) % n) ? -1 : (i > index_ ? 1 : -1);
    }
    index_ = i;
    rot_ = 0;
    failed_ = false;
    drop(cur_);
    asked_w_ = asked_h_ = 0;
    const std::string p = files_[i];
    bool have = false;
    if (pre_.bmp && pre_.path == p) {   // the prefetched one: shown at once
        install(pre_);
        have = enough();
    }
    int bw, bh;
    wanted(&bw, &bh);
    // (a prefetch of p under way becomes its decode: Loader::show)
    loader_.show(p, bw, bh, !have && !cur_.bmp, !have);
    if (!have) { asked_w_ = bw; asked_h_ = bh; }
    pre_asked_.clear();
    ensure();
    return true;
}

bool Viewer::next() { return files_.size() > 1 && go((index_ + 1) % files_.size()); }
bool Viewer::prev() { return files_.size() > 1 && go((index_ + files_.size() - 1) % files_.size()); }
bool Viewer::first() { return go(0); }
bool Viewer::last() { return !files_.empty() && go(files_.size() - 1); }

void Viewer::remove_current()
{
    if (files_.empty()) return;
    std::string gone = files_[index_];
    files_.erase(files_.begin() + (long)index_);
    if (pre_.path == gone) drop(pre_);
    drop(cur_);
    if (files_.empty()) { index_ = 0; loader_.show(std::string(), 0, 0, false, false); return; }
    size_t i = index_ < files_.size() ? index_ : files_.size() - 1;
    index_ = (size_t)-1;
    go(i);
}

void Viewer::set_viewport(int w, int h)
{
    w = std::max(w, 1); h = std::max(h, 1);
    if (w == vw_ && h == vh_) return;
    vw_ = w; vh_ = h;
    ensure();
}

void Viewer::set_best_fit()
{
    fit_ = true;
    ensure();
}

void Viewer::set_zoom(double z)
{
    double lo = ZOOM_STEPS[0] / 100.0, hi = ZOOM_STEPS[ZOOM_STEP_COUNT - 1] / 100.0;
    zoom_ = std::max(lo, std::min(hi, z));
    fit_ = false;
    ensure();
}

bool Viewer::zoom_in()
{
    double z = zoom(), n = zoom_step_in(z);
    if (n <= z + 1e-9) return false;
    set_zoom(n);
    return true;
}

bool Viewer::zoom_out()
{
    double z = zoom(), n = zoom_step_out(z);
    if (n >= z - 1e-9) return false;
    set_zoom(n);
    return true;
}

void Viewer::rotate(int quarters)
{
    quarters = ((quarters % 4) + 4) % 4;
    if (!quarters) return;
    rot_ = (rot_ + quarters) & 3;
    cur_.bmp = rotated(cur_.bmp, quarters);
    ensure();
}

size_t Viewer::bytes() const { return bitmap_bytes(cur_.bmp) + bitmap_bytes(pre_.bmp); }

bool Viewer::pump()
{
    bool changed = false;
    Loader::Result r;
    while (loader_.take(r)) {
        Pic p;
        p.path = r.path; p.bmp = r.bmp; p.w = r.w; p.h = r.h; p.box_w = r.box_w; p.box_h = r.box_h;
        r.bmp = nullptr;
        bool here = !files_.empty() && r.path == files_[index_];
        if (r.kind == Loader::PREVIEW) {
            if (here && p.bmp && !cur_.bmp) {
                p.preview = true;
                install(p);
                changed = true;
            }
        } else if (r.kind == Loader::CURRENT) {
            if (here) {
                // the decode asked last is in: another may be asked (a larger window, a zoom); a larger one that failed
                // is not asked again
                bool last = r.box_w >= asked_w_ && r.box_h >= asked_h_;
                if (p.bmp) { install(p); changed = true; if (last) asked_w_ = asked_h_ = 0; }
                else if (!cur_.bmp || cur_.preview) { failed_ = true; changed = true; }
            }
        } else {   // a prefetch
            if (pre_asked_ == r.path) pre_asked_.clear();
            if (here && p.bmp && (!cur_.bmp || cur_.preview)) { install(p); changed = true; }
            else if (p.bmp && r.path == neighbour()) { drop(pre_); pre_ = p; p.bmp = nullptr; }
        }
        drop(p);
    }
    ensure();
    return changed;
}

} // namespace luxview
