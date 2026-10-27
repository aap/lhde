#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include "thumbs.h"
#include "config.h"
#include "gdi/image.h"
#include "gdi/png.h"

namespace shell {
namespace thumbs {

// ---- MD5 (RFC 1321) ---------------------------------------------------------------

std::string md5_hex(const std::string& data)
{
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };
    static const int R[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                               5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
                               4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                               6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
    uint32_t h[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };
    std::string m = data;
    uint64_t bits = (uint64_t)data.size() * 8;
    m += (char)0x80;
    while (m.size() % 64 != 56) m += (char)0;
    for (int i = 0; i < 8; i++) m += (char)(bits >> (8 * i));
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[16];
        for (int i = 0; i < 16; i++) {
            const unsigned char* p = (const unsigned char*)m.data() + off + i * 4;
            w[i] = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 64; i++) {
            uint32_t f; int g;
            if (i < 16) { f = (b & c) | (~b & d); g = i; }
            else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
            else { f = c ^ (b | ~d); g = (7 * i) % 16; }
            uint32_t t = d; d = c; c = b;
            uint32_t x = a + f + K[i] + w[g];
            b = b + (x << R[i] | x >> (32 - R[i]));
            a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    }
    static const char hex[] = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 4; k++) { uint8_t v = (uint8_t)(h[i] >> (8 * k)); out += hex[v >> 4]; out += hex[v & 15]; }
    return out;
}

// ---- the cache ----------------------------------------------------------------------

std::string cache_dir()
{
    const char* c = getenv("XDG_CACHE_HOME");
    if (c && c[0] == '/') return std::string(c) + "/thumbnails";
    return home_dir() + "/.cache/thumbnails";
}

int cache_size_for(int view_size) { return view_size <= 128 ? 128 : 256; }

static const char* size_dir(int cache_size) { return cache_size <= 128 ? "normal" : "large"; }

std::string cache_path(const std::string& file, int cache_size)
{
    return cache_dir() + "/" + size_dir(cache_size) + "/" + md5_hex(uri_from_path(file)) + ".png";
}

bool can_thumbnail(const std::string& path)
{
    size_t dot = path.rfind('.');
    if (dot == std::string::npos || path.find('/', dot) != std::string::npos) return false;
    std::string ext = path.substr(dot + 1);
    for (char& c : ext) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    static const char* known[] = { "png", "jpg", "jpeg", "jpe", "jfif", "bmp", "dib", "ico", "cur" };
    for (const char* k : known) if (ext == k) return true;
    return ext == "webp" && gdi::webp_supported();
}

static bool mkdir_private(const std::string& dir)
{
    // the spec wants the thumbnail directories 0700
    std::string d;
    size_t i = 0;
    while (i < dir.size()) {
        size_t j = dir.find('/', i + 1);
        if (j == std::string::npos) j = dir.size();
        d = dir.substr(0, j);
        i = j;
        if (mkdir(d.c_str(), 0700) != 0 && errno != EEXIST) return false;
    }
    return true;
}

// Thumb::URI and Thumb::MTime of a cached file match; bmp (if wanted) decoded.
static bool cached(const std::string& file, const std::string& uri, int64_t mtime, gdi::Bitmap** bmp)
{
    std::string data;
    if (!read_file(file, data) || data.size() < 8) return false;
    gdi::PngText text = gdi::read_png_text((const uint8_t*)data.data(), data.size());
    bool uri_ok = false, mtime_ok = false;
    for (auto& t : text) {
        if (t.first == "Thumb::URI") uri_ok = t.second == uri;
        else if (t.first == "Thumb::MTime") mtime_ok = strtoll(t.second.c_str(), nullptr, 10) == mtime;
    }
    if (!uri_ok || !mtime_ok) return false;
    if (bmp) *bmp = gdi::decode_png((const uint8_t*)data.data(), data.size());
    return !bmp || *bmp;
}

// tmp file in the same directory, 0600, then rename (readers never see half a file)
static void save(const gdi::Bitmap* b, const std::string& path, const gdi::PngText& text)
{
    std::string dir = dir_of(path);
    if (!mkdir_private(dir)) return;
    char tmp[64];
    snprintf(tmp, sizeof tmp, ".flxp-%d-%lx.tmp", (int)getpid(), (unsigned long)(uintptr_t)&tmp);
    std::string t = path + tmp;
    if (!gdi::write_png(b, t, text)) { unlink(t.c_str()); return; }
    chmod(t.c_str(), 0600);
    if (rename(t.c_str(), path.c_str()) != 0) unlink(t.c_str());
}

gdi::Bitmap* get(const std::string& path, int64_t mtime, int cache_size)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return nullptr;
    mtime = (int64_t)st.st_mtime;
    std::string uri = uri_from_path(path);
    std::string hash = md5_hex(uri);
    std::string dir = cache_dir();
    std::string file = dir + "/" + size_dir(cache_size) + "/" + hash + ".png";
    gdi::Bitmap* b = nullptr;
    if (cached(file, uri, mtime, &b)) return b;
    // a larger current thumbnail serves a smaller size too
    if (cache_size <= 128 && cached(dir + "/large/" + hash + ".png", uri, mtime, &b)) {
        int w, h;
        gdi::fit_size(b->width, b->height, cache_size, cache_size, false, &w, &h);
        if (w == b->width && h == b->height) return b;
        gdi::Bitmap* s = gdi::resample(b, w, h);
        gdi::destroy_bitmap(b);
        return s;
    }
    std::string fail = dir + "/fail/flxp-1.0/" + hash + ".png";
    if (cached(fail, uri, mtime, nullptr)) return nullptr;
    bool in_cache = path.compare(0, dir.size() + 1, dir + "/") == 0;   // never thumbnail thumbnails
    char mt[32], sz[32];
    snprintf(mt, sizeof mt, "%lld", (long long)mtime);
    snprintf(sz, sizeof sz, "%lld", (long long)st.st_size);
    int ow = 0, oh = 0;
    gdi::Bitmap* img = gdi::load_image(path, cache_size, &ow, &oh);
    if (!img) {
        if (!in_cache) {
            gdi::Bitmap* dot = gdi::create_bitmap(1, 1);
            if (dot) {
                dot->pixels[0] = 0;
                save(dot, fail, { { "Thumb::URI", uri }, { "Thumb::MTime", mt }, { "Software", "flxp" } });
                gdi::destroy_bitmap(dot);
            }
        }
        return nullptr;
    }
    int w, h;
    gdi::fit_size(img->width, img->height, cache_size, cache_size, false, &w, &h);
    if (w != img->width || h != img->height) {
        gdi::Bitmap* s = gdi::resample(img, w, h);
        gdi::destroy_bitmap(img);
        img = s;
        if (!img) return nullptr;
    }
    if (!in_cache) {
        char iw[16], ih[16];
        snprintf(iw, sizeof iw, "%d", ow);
        snprintf(ih, sizeof ih, "%d", oh);
        save(img, file, { { "Thumb::URI", uri }, { "Thumb::MTime", mt }, { "Thumb::Size", sz },
                          { "Thumb::Image::Width", iw }, { "Thumb::Image::Height", ih }, { "Software", "flxp" } });
    }
    return img;
}

// ---- the loader ---------------------------------------------------------------------

void free_result(Loader::Result& r)
{
    if (r.bmp) gdi::destroy_bitmap(r.bmp);
    for (gdi::Bitmap* m : r.minis) if (m) gdi::destroy_bitmap(m);
    r.bmp = nullptr;
    r.minis.clear();
}

static gdi::Bitmap* fitted(gdi::Bitmap* b, int size)
{
    if (!b || (b->width <= size && b->height <= size)) return b;
    int w, h;
    gdi::fit_size(b->width, b->height, size, size, false, &w, &h);
    gdi::Bitmap* s = gdi::resample(b, w, h);
    gdi::destroy_bitmap(b);
    return s;
}

// XP folderthumb.cpp: the first pictures of a folder (it collects 8 candidates
// for 4 previews, in case some fail)
static std::vector<gdi::Bitmap*> folder_minis(const std::string& dir, int mini)
{
    std::vector<gdi::Bitmap*> out;
    std::vector<std::string> names;
    if (DIR* d = opendir(dir.c_str())) {
        while (struct dirent* e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            if (can_thumbnail(e->d_name)) names.push_back(e->d_name);
        }
        closedir(d);
    }
    std::sort(names.begin(), names.end());
    for (size_t i = 0; i < names.size() && i < 8 && out.size() < 4; i++) {
        std::string p = dir + "/" + names[i];
        if (gdi::Bitmap* b = fitted(get(p, 0, 128), mini)) out.push_back(b);
    }
    return out;
}

struct Loader::Impl {
    struct Job { uint32_t gen; int id; std::string path; int64_t mtime; int size; bool folder; int mini; };
    Notify notify;
    void* ctx;
    std::mutex lock;
    std::condition_variable cv;
    std::deque<Job> jobs;
    std::deque<Result> done;
    std::vector<std::thread> threads;
    std::atomic<uint32_t> gen{1};
    int working = 0;                // jobs taken by a thread and not done (under lock)
    bool quit = false;
    void run();
};

Loader::Loader(Notify notify, void* ctx, int threads) : impl_(new Impl)
{
    impl_->notify = notify;
    impl_->ctx = ctx;
    if (threads <= 0) {
        long n = sysconf(_SC_NPROCESSORS_ONLN);
        threads = n < 2 ? 1 : n > 2 ? 2 : (int)n;   // two is plenty; each decode may hold a full-size image
    }
    for (int i = 0; i < threads; i++) impl_->threads.emplace_back([this] { impl_->run(); });
}

Loader::~Loader()
{
    {
        std::lock_guard<std::mutex> l(impl_->lock);
        impl_->quit = true;
        impl_->jobs.clear();
    }
    impl_->cv.notify_all();
    for (std::thread& t : impl_->threads) t.join();
    for (Result& r : impl_->done) free_result(r);
    delete impl_;
}

uint32_t Loader::generation() const { return impl_->gen; }

bool Loader::busy() const
{
    std::lock_guard<std::mutex> l(impl_->lock);
    return !impl_->jobs.empty() || impl_->working > 0;
}

void Loader::request(uint32_t gen, int id, const std::string& path, int64_t mtime, int view_size, bool folder, int mini_size)
{
    {
        std::lock_guard<std::mutex> l(impl_->lock);
        impl_->jobs.push_back(Impl::Job{ gen, id, path, mtime, view_size, folder, mini_size });
    }
    impl_->cv.notify_one();
}

uint32_t Loader::cancel()
{
    std::lock_guard<std::mutex> l(impl_->lock);
    impl_->jobs.clear();
    for (Result& r : impl_->done) free_result(r);
    impl_->done.clear();
    return ++impl_->gen;
}

bool Loader::take(Result& out)
{
    std::lock_guard<std::mutex> l(impl_->lock);
    while (!impl_->done.empty()) {
        out = impl_->done.front();
        impl_->done.pop_front();
        if (out.gen == impl_->gen) return true;
        free_result(out);   // finished after a cancel
    }
    return false;
}

void Loader::Impl::run()
{
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> l(lock);
            cv.wait(l, [this] { return quit || !jobs.empty(); });
            if (quit) return;
            job = jobs.front();
            jobs.pop_front();
            if (job.gen != gen) continue;
            working++;
        }
        Result r;
        r.gen = job.gen; r.id = job.id; r.path = job.path;
        if (job.folder) r.minis = folder_minis(job.path, job.mini);
        else r.bmp = fitted(get(job.path, job.mtime, cache_size_for(job.size)), job.size);
        bool wake;
        {
            std::lock_guard<std::mutex> l(lock);
            working--;
            if (job.gen != gen || quit) { free_result(r); continue; }
            wake = done.empty();
            done.push_back(r);
        }
        if (wake && notify) notify(ctx);   // one wake-up per batch: the UI takes everything there is
    }
}

} // namespace thumbs
} // namespace shell
