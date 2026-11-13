// The thumbnails' memory bounds (shell/thumbs.h, gdi/image.h) on pictures the test writes itself into a scratch
// directory (also its $XDG_CACHE_HOME): a 6000 x 4000 JPEG and a 4000 x 4000 PNG made into a 128 px thumbnail without
// their full image in memory (the peak RSS, /proc/self/status VmHWM after resetting it through /proc/self/clear_refs,
// grows by less than 16 MB), the PNG's reduction against the full decode, an interlaced PNG, the size ceilings; the
// view's images bounded by bytes (thumbs::Slots: 300 inserts, the pinned ones kept, least recently used first); a
// cancelled generation decodes nothing more, and three loaders decode at most two pictures at once (thumbs::Loader).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <set>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <jpeglib.h>
#include <png.h>
#include "gdi/image.h"
#include "gdi/png.h"
#include "shell/thumbs.h"

static int fails, checks;
static void check(const char* what, bool ok, const std::string& detail = "")
{
    checks++;
    if (ok) return;
    fails++;
    printf("FAIL %s%s%s\n", what, detail.empty() ? "" : ": ", detail.c_str());
}

static std::string dir;

// ---- pictures, written row by row ------------------------------------------------------

static bool write_jpeg(const std::string& path, int w, int h, bool progressive)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    jpeg_compress_struct c;
    jpeg_error_mgr e;
    c.err = jpeg_std_error(&e);
    jpeg_create_compress(&c);
    jpeg_stdio_dest(&c, f);
    c.image_width = (JDIMENSION)w; c.image_height = (JDIMENSION)h;
    c.input_components = 3; c.in_color_space = JCS_RGB;
    jpeg_set_defaults(&c);
    jpeg_set_quality(&c, 85, TRUE);
    if (progressive) jpeg_simple_progression(&c);
    jpeg_start_compress(&c, TRUE);
    std::vector<unsigned char> row((size_t)w * 3);
    while (c.next_scanline < c.image_height) {
        int y = (int)c.next_scanline;
        for (int x = 0; x < w; x++) {
            row[(size_t)x * 3] = (unsigned char)(x * 255 / w);
            row[(size_t)x * 3 + 1] = (unsigned char)(y * 255 / h);
            row[(size_t)x * 3 + 2] = (unsigned char)((x ^ y) & 255);
        }
        JSAMPROW r = row.data();
        jpeg_write_scanlines(&c, &r, 1);
    }
    jpeg_finish_compress(&c);
    jpeg_destroy_compress(&c);
    fclose(f);
    return true;
}

// RGBA: the left half opaque red, the right half blue, its top-right quarter fully transparent green
static bool write_png(const std::string& path, int w, int h, bool interlaced)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png_create_info_struct(png);
    if (setjmp(png_jmpbuf(png))) { png_destroy_write_struct(&png, &info); fclose(f); return false; }
    png_init_io(png, f);
    png_set_compression_level(png, 1);
    png_set_IHDR(png, info, (png_uint_32)w, (png_uint_32)h, 8, PNG_COLOR_TYPE_RGB_ALPHA,
                 interlaced ? PNG_INTERLACE_ADAM7 : PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    int passes = interlaced ? png_set_interlace_handling(png) : 1;
    std::vector<png_byte> row((size_t)w * 4);
    for (int p = 0; p < passes; p++)
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                png_byte* q = &row[(size_t)x * 4];
                bool left = x < w / 2, clear = !left && y < h / 2;
                q[0] = left ? 255 : 0; q[1] = clear ? 255 : 0; q[2] = left || clear ? 0 : 255; q[3] = clear ? 0 : 255;
            }
            png_write_row(png, row.data());
        }
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    fclose(f);
    return true;
}

// ---- the peak RSS ------------------------------------------------------------------------

static long status_kb(const char* key)
{
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    long v = -1;
    size_t n = strlen(key);
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, key, n) && line[n] == ':') { v = atol(line + n + 1); break; }
    fclose(f);
    return v;
}

static bool reset_peak()
{
    FILE* f = fopen("/proc/self/clear_refs", "w");
    if (!f) return false;
    bool ok = fputs("5", f) >= 0;
    return fclose(f) == 0 && ok;
}

static bool can_measure;

// the peak RSS's growth over the call, in KB (0 when the kernel cannot reset the peak)
template <class F> static long peak_growth(F f)
{
    if (!can_measure) { f(); return 0; }
    reset_peak();
    long before = status_kb("VmHWM");
    f();
    return status_kb("VmHWM") - before;
}

static std::string kb(long v) { return std::to_string(v / 1024) + " MB"; }

int main()
{
    const char* t = getenv("TMPDIR");
    std::string base = std::string(t && *t ? t : "/tmp") + "/thumbstest.XXXXXX";
    std::vector<char> b(base.begin(), base.end());
    b.push_back(0);
    if (!mkdtemp(b.data())) { printf("FAIL no scratch directory\n"); return 1; }
    dir = b.data();
    setenv("XDG_CACHE_HOME", (dir + "/cache").c_str(), 1);
    can_measure = reset_peak() && status_kb("VmHWM") > 0;
    if (!can_measure) printf("note: no /proc/self/clear_refs: the peak RSS is not checked\n");

    std::string big = dir + "/big.jpg", prog = dir + "/progressive.jpg", bigpng = dir + "/big.png";
    std::string halves = dir + "/halves.png", inter = dir + "/interlaced.png";
    check("write the JPEG", write_jpeg(big, 6000, 4000, false));
    check("write the progressive JPEG", write_jpeg(prog, 3000, 2000, true));
    check("write the PNG", write_png(bigpng, 4000, 4000, false));
    check("write the small PNG", write_png(halves, 512, 256, false));
    check("write the interlaced PNG", write_png(inter, 512, 256, true));

    // a 6000 x 4000 JPEG (96 MB decoded at full size) to 128 px: DCT scaling at 1/8, read from the file as it decodes
    gdi::Bitmap* img = nullptr;
    int ow = 0, oh = 0;
    long g = peak_growth([&] { img = gdi::load_image(big, 128, &ow, &oh); });
    check("JPEG decoded", img != nullptr);
    check("JPEG original size", ow == 6000 && oh == 4000, std::to_string(ow) + "x" + std::to_string(oh));
    check("JPEG at 1/8", img && img->width == 750 && img->height == 500, img ? std::to_string(img->width) + "x" + std::to_string(img->height) : "");
    check("JPEG: peak RSS grows by less than 16 MB", g < 16 * 1024, kb(g));
    if (img) gdi::destroy_bitmap(img);

    // the same through the thumbnail cache: made, saved at 128 px, read back
    gdi::Bitmap* th = nullptr;
    g = peak_growth([&] { th = shell::thumbs::get(big, 0, 128); });
    check("JPEG thumbnail", th && th->width == 128 && th->height == 85, th ? std::to_string(th->width) + "x" + std::to_string(th->height) : "");
    check("JPEG thumbnail: peak RSS grows by less than 16 MB", g < 16 * 1024, kb(g));
    struct stat st;
    check("JPEG thumbnail saved", stat(shell::thumbs::cache_path(big, 128).c_str(), &st) == 0);
    if (th) gdi::destroy_bitmap(th);

    // a 4000 x 4000 PNG (64 MB decoded) reduced row by row, by 16 to 250 x 250
    g = peak_growth([&] { img = gdi::load_image(bigpng, 128, &ow, &oh); });
    check("PNG decoded", img != nullptr);
    check("PNG original size", ow == 4000 && oh == 4000);
    check("PNG reduced by 16", img && img->width == 250 && img->height == 250, img ? std::to_string(img->width) + "x" + std::to_string(img->height) : "");
    check("PNG: peak RSS grows by less than 16 MB", g < 16 * 1024, kb(g));
    if (img) gdi::destroy_bitmap(img);
    g = peak_growth([&] { th = shell::thumbs::get(bigpng, 0, 256); });
    check("PNG thumbnail", th && th->width == 256 && th->height == 256);
    check("PNG thumbnail: peak RSS grows by less than 16 MB", g < 16 * 1024, kb(g));
    if (th) gdi::destroy_bitmap(th);

    // the reduction: 512 x 256 by 2 (both sides stay at least 100) to 256 x 128, the halves kept, the transparent
    // quarter transparent, its colour not bleeding into the blue (premultiplied)
    img = gdi::load_image(halves, 100, &ow, &oh);
    check("reduced size", img && img->width == 256 && img->height == 128);
    if (img) {
        check("reduced: red", img->row(100)[10] == 0xffff0000u);
        check("reduced: blue", img->row(100)[200] == 0xff0000ffu);
        check("reduced: transparent", (img->row(10)[200] >> 24) == 0);
        gdi::destroy_bitmap(img);
    }
    // without max_size (and from memory) the full image
    std::string data;
    {
        FILE* f = fopen(halves.c_str(), "rb");
        char buf[65536];
        size_t n;
        while (f && (n = fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, n);
        if (f) fclose(f);
    }
    img = gdi::decode_image((const uint8_t*)data.data(), data.size(), 0);
    check("full size from memory", img && img->width == 512 && img->height == 256);
    if (img) gdi::destroy_bitmap(img);
    img = gdi::decode_image((const uint8_t*)data.data(), data.size(), 100);
    check("reduced from memory", img && img->width == 256 && img->height == 128);
    if (img) gdi::destroy_bitmap(img);
    // an interlaced PNG cannot be reduced row by row: decoded at full size (it is under the ceiling)
    img = gdi::load_image(inter, 100, &ow, &oh);
    check("interlaced: full size", img && img->width == 512 && img->height == 256 && img->row(100)[10] == 0xffff0000u);
    if (img) gdi::destroy_bitmap(img);
    // a progressive JPEG keeps its coefficients (3000 x 2000: about 18 MB), within the decoder's 160 MB
    img = gdi::load_image(prog, 128, &ow, &oh);
    check("progressive JPEG", img && img->width == 375 && img->height == 250);
    if (img) gdi::destroy_bitmap(img);

    // the view's images: 300 thumbnails of 256 px (256 KB each) into the 64 MB budget
    {
        shell::thumbs::Slots slots;
        slots.reset(256 * 256 * 4);
        std::set<std::string> pinned = { "item 0", "item 1" };
        slots.pin(pinned);
        std::vector<std::string> evicted;
        size_t most = 0;
        int max_slot = -1;
        for (int i = 0; i < 300; i++) {
            int slot = slots.insert("item " + std::to_string(i), &evicted);
            if (slot > max_slot) max_slot = slot;
            if (slots.bytes() > most) most = slots.bytes();
            if (i == 200) slots.find("item 10");   // used again: kept over the older ones
        }
        check("bytes within the budget after 300 inserts", most <= shell::thumbs::Slots::DEFAULT_BUDGET,
              std::to_string(most / 1024) + " KB");
        check("256 kept", slots.count() == 256, std::to_string(slots.count()));
        check("44 evicted", evicted.size() == 44, std::to_string(evicted.size()));
        check("slots reused", max_slot == 255, std::to_string(max_slot));
        check("pinned kept", slots.find("item 0") >= 0 && slots.find("item 1") >= 0);
        check("the least recently used went first", evicted.size() > 1 && evicted[0] == "item 2" && evicted[1] == "item 3",
              evicted.empty() ? "" : evicted[0]);
        check("used again: kept", slots.find("item 10") >= 0);
        check("the newest kept", slots.find("item 299") >= 0);
        // everything pinned: the view still gets its images (budget plus what it shows)
        std::set<std::string> all;
        for (int i = 0; i < 300; i++) all.insert("p" + std::to_string(i));
        slots.reset(256 * 256 * 4);
        slots.pin(all);
        evicted.clear();
        for (int i = 0; i < 300; i++) slots.insert("p" + std::to_string(i), &evicted);
        check("pinned beyond the budget", slots.count() == 300 && evicted.empty());
        slots.erase("p5");
        check("an erased slot is handed out again", slots.insert("q", nullptr) >= 0 && slots.slots() == 300);
    }

    // the loader: a cancelled generation decodes nothing more (one thread; 40 pictures asked for, cancelled at once)
    {
        std::string one = dir + "/one.jpg";
        check("write the small JPEG", write_jpeg(one, 2000, 1500, false));
        std::vector<std::string> files;
        for (int i = 0; i < 40; i++) {
            std::string f = dir + "/q" + std::to_string(i) + ".jpg";
            if (link(one.c_str(), f.c_str()) == 0) files.push_back(f);
        }
        auto idle = [](shell::thumbs::Loader& l) {
            for (int i = 0; i < 1000 && l.busy(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        };
        shell::thumbs::Loader loader(nullptr, nullptr, 1);
        int before = shell::thumbs::decodes();
        uint32_t gen = loader.generation();
        for (size_t i = 0; i < 20; i++) loader.request(gen, (int)i, files[i], 0, 128);
        uint32_t next = loader.cancel();
        idle(loader);
        int made = shell::thumbs::decodes() - before;
        check("cancelled: at most the one being made", made <= 1, std::to_string(made) + " decodes");
        shell::thumbs::Loader::Result r;
        check("cancelled: nothing to take", !loader.take(r));
        check("a new generation", next != gen);
        before = shell::thumbs::decodes();
        for (size_t i = 20; i < 23; i++) loader.request(next, (int)i, files[i], 0, 128);
        idle(loader);
        int got = 0;
        while (loader.take(r)) { got += r.bmp != nullptr; shell::thumbs::free_result(r); }
        check("the new generation's three", got == 3 && shell::thumbs::decodes() - before == 3, std::to_string(got));
        // asked for in an old generation: dropped unmade
        before = shell::thumbs::decodes();
        for (size_t i = 23; i < 26; i++) loader.request(gen, (int)i, files[i], 0, 128);
        idle(loader);
        check("an old generation's requests: not made", shell::thumbs::decodes() == before && !loader.take(r));
        // three loaders of two threads each (the view's and the panes'): still at most two decodes at a time
        shell::thumbs::Loader a(nullptr, nullptr, 2), b2(nullptr, nullptr, 2), c(nullptr, nullptr, 2);
        before = shell::thumbs::decodes();
        for (size_t i = 26; i < 38; i++) {
            shell::thumbs::Loader& l = i % 3 == 0 ? a : i % 3 == 1 ? b2 : c;
            l.request(l.generation(), (int)i, files[i], 0, 256);
        }
        idle(a); idle(b2); idle(c);
        check("three loaders: all twelve made", shell::thumbs::decodes() - before == 12, std::to_string(shell::thumbs::decodes() - before));
        check("three loaders: at most two decodes at once", shell::thumbs::decodes_at_once_most() <= shell::thumbs::DECODES_AT_ONCE,
              std::to_string(shell::thumbs::decodes_at_once_most()));
        check("three loaders: two at once seen", shell::thumbs::decodes_at_once_most() == shell::thumbs::DECODES_AT_ONCE);
    }

    std::string cmd = "rm -rf '" + dir + "'";
    if (system(cmd.c_str()) != 0) printf("note: %s failed\n", cmd.c_str());
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
