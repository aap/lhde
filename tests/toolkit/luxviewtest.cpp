// luxview's view model (apps/luxview/model.h) with a slow fake decoder: the list's order (the Explorer's Name order, the
// command line's lists), the zoom steps, the generations (three switches made fast while a decode is under way: the last
// picture is the one shown, an older decode that finishes is thrown away, the middle one never decoded), the thumbnail
// shown first and replaced by the decode, the prefetched neighbour shown at once by Next, a zoom past the decode asking
// for a larger one, rotation, the budget, Delete's move to the next picture, and the loader ended (destroyed) while a
// decode is under way: joined, nothing leaked. Last a real JPEG decoded at the scale of its box (gdi's DCT scaling), not
// at its own size.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "apps/luxview/model.h"
#include "gdi/image.h"
#include "gdi/png.h"

using namespace luxview;

static int fails, checks;
static void check(const char* what, const std::string& want, const std::string& got)
{
    checks++;
    if (want == got) return;
    fails++;
    printf("FAIL %s: expected [%s] got [%s]\n", what, want.c_str(), got.c_str());
}
static void check(const char* what, long long want, long long got) { check(what, std::to_string(want), std::to_string(got)); }
static void check(const char* what, bool ok) { check(what, std::string("true"), std::string(ok ? "true" : "false")); }

// ---- the fake decoder: a picture's id in its pixels, its own size from its name ----------------------------------------

struct Fake {
    int delay_ms = 0;
    bool obey_stop = true;              // returns early when the loader says stopping()
    std::mutex lock;
    std::vector<std::string> decoded;   // decode calls, in order
    std::vector<std::string> previews;
    int w = 4000, h = 3000;
};
static Fake fake;

static std::string base(const std::string& p) { size_t s = p.rfind('/'); return s == std::string::npos ? p : p.substr(s + 1); }

static gdi::Bitmap* make(const std::string& path, int w, int h)
{
    gdi::Bitmap* b = gdi::create_bitmap(w, h);
    uint32_t id = (uint32_t)std::hash<std::string>()(base(path)) | 0xff000000u;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) b->row(y)[x] = id;
    return b;
}
static uint32_t id_of(const std::string& path) { return (uint32_t)std::hash<std::string>()(base(path)) | 0xff000000u; }

static gdi::Bitmap* fake_decode(const std::string& path, int bw, int bh, int* w, int* h, void*)
{
    { std::lock_guard<std::mutex> l(fake.lock); fake.decoded.push_back(base(path)); }
    for (int t = 0; t < fake.delay_ms; t += 5) {
        if (fake.obey_stop && Loader::stopping()) return nullptr;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    *w = fake.w; *h = fake.h;
    int fw, fh;
    fit_into(fake.w, fake.h, bw, bh, &fw, &fh);
    return make(path, fw, fh);
}

static gdi::Bitmap* fake_preview(const std::string& path, int* w, int* h, void*)
{
    { std::lock_guard<std::mutex> l(fake.lock); fake.previews.push_back(base(path)); }
    if (base(path).find("nothumb") != std::string::npos) return nullptr;
    *w = fake.w; *h = fake.h;
    return make(path, 256, 192);
}

static Decoder fake_decoder()
{
    Decoder d;
    d.decode = fake_decode;
    d.preview = fake_preview;
    return d;
}

static std::atomic<int> notified{0};
static void notify(void*) { notified++; }

// pump until cond() or ms pass; true when cond() held
template <class F> static bool pump_until(Viewer& v, int ms, F cond)
{
    for (int t = 0; t < ms; t += 5) {
        v.pump();
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    v.pump();
    return cond();
}

static uint32_t shown_id(const Viewer& v) { return v.bitmap() ? v.bitmap()->pixels[0] : 0; }
static std::string decoded_list()
{
    std::lock_guard<std::mutex> l(fake.lock);
    std::string s;
    for (const std::string& d : fake.decoded) s += (s.empty() ? "" : " ") + d;
    return s;
}
static int decodes_of(const char* name)
{
    std::lock_guard<std::mutex> l(fake.lock);
    int n = 0;
    for (const std::string& d : fake.decoded) n += d == name;
    return n;
}
static void reset() { std::lock_guard<std::mutex> l(fake.lock); fake.decoded.clear(); fake.previews.clear(); }

static void touch(const std::string& p) { FILE* f = fopen(p.c_str(), "w"); if (f) fclose(f); }

int main()
{
    char tmpl[] = "/tmp/luxviewtest.XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 1; }
    std::string T = tmpl;

    // ---- the list ----
    for (const char* n : { "img10.png", "img2.png", "Img1.jpg", "notes.txt", ".hidden.png", "b.JPEG", "a 3.bmp", "a 20.bmp" })
        touch(T + "/" + n);
    mkdir((T + "/sub.png").c_str(), 0755);   // a folder named like a picture is not one
    std::string order;
    for (const std::string& p : folder_pictures(T)) order += (order.empty() ? "" : " ") + base(p);
    check("folder_pictures: the Explorer's Name order, pictures only, no hidden ones", "a 3.bmp a 20.bmp b.JPEG Img1.jpg img2.png img10.png", order);
    size_t start = 99;
    std::vector<std::string> l = list_for({ T + "/img2.png" }, &start);
    check("one file: its folder's pictures", 6, (long long)l.size());
    check("one file: starting at it", 4, (long long)start);
    l = list_for({ T }, &start);
    check("a folder: from its first picture", "a 3.bmp", l.empty() ? "" : base(l[0]));
    check("a folder: start 0", 0, (long long)start);
    l = list_for({ T + "/img10.png", T + "/a 3.bmp" }, &start);
    check("several files: those, in their order", "img10.png a 3.bmp", l.size() == 2 ? base(l[0]) + " " + base(l[1]) : "?");
    l = list_for({ T + "/.hidden.png" }, &start);
    check("a hidden file: alone", 1, (long long)l.size());
    char cwd[4096];
    if (getcwd(cwd, sizeof cwd) && chdir(T.c_str()) == 0) {
        l = list_for({ "img2.png" }, &start);
        check("a relative name is made absolute", T + "/img2.png", l.size() > start ? l[start] : "");
        if (chdir(cwd) != 0) perror("chdir");
    }

    // ---- zoom steps ----
    check("zoom in from 37%", 50, (long long)(zoom_step_in(0.37) * 100 + 0.5));
    check("zoom in from 100%", 200, (long long)(zoom_step_in(1.0) * 100 + 0.5));
    check("zoom out from 37%", 25, (long long)(zoom_step_out(0.37) * 100 + 0.5));
    check("zoom out from 5%", 5, (long long)(zoom_step_out(0.05) * 100 + 0.5));
    check("zoom in from 4000%", 4000, (long long)(zoom_step_in(40) * 100 + 0.5));

    // ---- generations: three switches while a decode is under way ----
    std::vector<std::string> files;
    for (const char* n : { "a.jpg", "b.jpg", "c.jpg", "d.jpg", "e.jpg" }) files.push_back(T + "/" + n);
    fake.delay_ms = 300;
    fake.obey_stop = false;             // a decoder that cannot stop: its result must still be thrown away
    {
        Viewer v(notify, nullptr, fake_decoder());
        v.set_viewport(1000, 750);
        v.set_files(files, 0);
        check("set_files shows the first", "a.jpg", base(v.path()));
        check("the thumbnail first", pump_until(v, 1000, [&] { return v.bitmap() != nullptr; }));
        check("it is a's thumbnail", (long long)id_of("a.jpg"), (long long)shown_id(v));
        check("it is a preview", v.is_preview());
        check("the preview is drawn at the fitted size (1000 px wide)", 1000, (long long)(v.zoom() * v.picture_w() + 0.5));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));   // a's decode is under way
        uint32_t g0 = v.loader().generation();
        v.next();
        v.next();
        check("two Next: c", "c.jpg", base(v.path()));
        check("each picture a generation", (long long)g0 + 2, (long long)v.loader().generation());
        bool older = false;
        bool got = pump_until(v, 3000, [&] {
            uint32_t id = shown_id(v);
            if (id == id_of("a.jpg") || id == id_of("b.jpg")) older = true;
            return v.bitmap() && !v.is_preview();
        });
        check("c's decode arrives", got);
        check("c is shown", (long long)id_of("c.jpg"), (long long)shown_id(v));
        check("never a's or b's picture after the switch", !older);
        check("decoded: a (under way), c; b never", "a.jpg c.jpg", decoded_list().substr(0, 11));
        check("fitted: 1000 x 750", "1000x750", std::to_string(v.bitmap()->width) + "x" + std::to_string(v.bitmap()->height));
        check("its own size known", 4000, v.picture_w());

        // the neighbour is prefetched; Next shows it at once, decoded at the fitted size
        check("d prefetched", pump_until(v, 2000, [&] { return v.prefetched(T + "/d.jpg"); }));
        int d0 = decodes_of("d.jpg");
        v.next();
        check("Next: d at once", (long long)id_of("d.jpg"), (long long)shown_id(v));
        check("d is not a preview", !v.is_preview());
        check("the budget: the shown and the prefetched picture", v.bytes() <= Viewer::BUDGET);

        // a zoom past what was decoded asks for a larger decode; the shown bitmap stays until it is in
        pump_until(v, 2000, [&] { return v.prefetched(T + "/e.jpg"); });
        // counted once e's prefetch is in (a decode of d would have been queued before it); not the loader's count, which
        // e's prefetch raises as soon as the loader thread takes it up, before or after next() returns
        check("d needed no decode of its own", d0, decodes_of("d.jpg"));
        v.set_zoom(1.0);
        check("Actual Size: loading a larger decode", v.loading());
        check("the smaller one stays meanwhile", (long long)id_of("d.jpg"), (long long)shown_id(v));
        check("the larger decode arrives", pump_until(v, 3000, [&] { return !v.loading(); }));
        check("at the picture's own size", "4000x3000", std::to_string(v.bitmap()->width) + "x" + std::to_string(v.bitmap()->height));
        check("48 MB and the 3 MB prefetch: within the budget, kept", v.prefetched(T + "/e.jpg") && v.bytes() <= Viewer::BUDGET);
        int n1 = v.loader().decodes();
        v.zoom_out();
        v.pump();
        check("Zoom Out: 50%", 50, (long long)(v.zoom() * 100 + 0.5));
        check("Zoom Out: no decode", n1, v.loader().decodes());

        // rotation in the view only
        v.set_best_fit();
        v.rotate(1);
        check("rotated: 3000 x 4000", "3000x4000", std::to_string(v.picture_w()) + "x" + std::to_string(v.picture_h()));
        check("the bitmap turned with it", v.bitmap() && v.bitmap()->height > v.bitmap()->width);
        v.rotate(-1);
        check("and back", 0, v.rotation());

        // Delete: the next one; at the end the previous one
        v.go(4);
        v.remove_current();
        check("the last deleted: the previous shown", "d.jpg", base(v.path()));
        check("four left", 4, (long long)v.count());
        v.go(1);
        v.remove_current();
        check("b deleted: c shown", "c.jpg", base(v.path()));
        v.first();
        check("Home: a", "a.jpg", base(v.path()));
        v.last();
        check("End: d", "d.jpg", base(v.path()));
        v.next();
        check("Next wraps around", "a.jpg", base(v.path()));
        v.prev();
        check("Previous wraps around", "d.jpg", base(v.path()));
    }

    // ---- a zoomed-in picture past the budget drops the prefetch ----
    {
        fake.delay_ms = 0;
        fake.w = 6000; fake.h = 4000;
        Viewer v(notify, nullptr, fake_decoder());
        v.set_viewport(1000, 750);
        v.set_files(files, 0);
        pump_until(v, 2000, [&] { return v.prefetched(T + "/b.jpg"); });
        check("at the fitted size: the prefetch", v.prefetched(T + "/b.jpg"));
        v.set_zoom(1.0);
        check("Actual Size of 6000 x 4000", pump_until(v, 3000, [&] { return !v.loading(); }));
        check("96 MB: the prefetch dropped", !v.prefetched(T + "/b.jpg"));
        check("only the shown picture held", (long long)6000 * 4000 * 4, (long long)v.bytes());
        v.set_zoom(4.0);
        pump_until(v, 500, [&] { return !v.loading(); });
        check("400%: magnified, no larger decode than the picture", (long long)6000 * 4000 * 4, (long long)v.bytes());
        v.set_best_fit();
        check("Best Fit: a decode at the fitted size replaces the 96 MB one",
              pump_until(v, 2000, [&] { return v.bitmap() && v.bitmap()->width == 1000; }));
        check("only that held, and the prefetch", pump_until(v, 2000, [&] { return v.prefetched(T + "/b.jpg"); }) && v.bytes() <= Viewer::BUDGET);
        v.next();
        check("Best Fit and Next: the prefetch again", pump_until(v, 2000, [&] { return v.prefetched(T + "/c.jpg"); }));
        fake.w = 4000; fake.h = 3000;
    }

    // ---- a picture that cannot be read ----
    {
        fake.delay_ms = 0;
        struct Bad { static gdi::Bitmap* d(const std::string&, int, int, int*, int*, void*) { return nullptr; } };
        Decoder d = fake_decoder();
        d.decode = Bad::d;
        Viewer v(notify, nullptr, d);
        v.set_files({ T + "/nothumb.jpg" }, 0);
        check("unreadable: failed", pump_until(v, 1000, [&] { return v.failed(); }));
        check("unreadable: nothing shown", v.bitmap() == nullptr);
        check("unreadable: not loading", !v.loading());
    }

    // ---- closing while a decode is under way ----
    {
        fake.delay_ms = 2000;
        fake.obey_stop = true;
        reset();
        auto t0 = std::chrono::steady_clock::now();
        {
            Viewer v(notify, nullptr, fake_decoder());
            v.set_files(files, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            check("the decode is under way", decoded_list().size() > 0);
        }
        long ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        check("closed during a decode: the decoder stopped, the thread joined (under 1 s)", ms < 1000);
        fake.obey_stop = false;
        fake.delay_ms = 400;
        t0 = std::chrono::steady_clock::now();
        {
            Viewer v(notify, nullptr, fake_decoder());
            v.set_files(files, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        check("closed during a decode that cannot stop: joined after it", ms >= 400 && ms < 2000);
    }

    // ---- the real decoder: a 4000 x 3000 JPEG fitted into 400 x 300 ----
    {
        std::string jpg = T + "/big.jpg";
        std::string cmd = "IM=convert; command -v magick >/dev/null 2>&1 && IM=magick; $IM -size 4000x3000 gradient:red-blue '" + jpg + "' 2>/dev/null";
        if (system(cmd.c_str()) == 0 && access(jpg.c_str(), R_OK) == 0) {
            Decoder d = default_decoder();
            int w = 0, h = 0;
            gdi::Bitmap* b = d.decode(jpg, 400, 300, &w, &h, nullptr);
            check("a JPEG decoded", b != nullptr);
            check("its own size", "4000x3000", std::to_string(w) + "x" + std::to_string(h));
            check("fitted into the box", "400x300", b ? std::to_string(b->width) + "x" + std::to_string(b->height) : "-");
            if (b) gdi::destroy_bitmap(b);
            b = d.decode(jpg, 10000, 10000, &w, &h, nullptr);
            check("a box larger than the picture: its own size", "4000x3000", b ? std::to_string(b->width) + "x" + std::to_string(b->height) : "-");
            if (b) gdi::destroy_bitmap(b);
        } else printf("skip the real JPEG: no ImageMagick\n");
    }

    std::string rm = "rm -rf '" + T + "'";
    if (system(rm.c_str()) != 0) printf("could not remove %s\n", T.c_str());
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
