// The thumbnails' memory bounds (shell/thumbs.h, gdi/image.h) on pictures the test writes itself into a scratch
// directory (also its $XDG_CACHE_HOME): a 6000 x 4000 JPEG and a 4000 x 4000 PNG made into a 128 px thumbnail without
// their full image in memory (the peak RSS, /proc/self/status VmHWM after resetting it through /proc/self/clear_refs,
// grows by less than 16 MB), the PNG's reduction against the full decode, an interlaced PNG, the size ceilings; the
// view's images bounded by bytes (thumbs::Slots: 300 inserts, the pinned ones kept, least recently used first); a
// cancelled generation decodes nothing more, and three loaders decode at most two pictures at once (thumbs::Loader);
// gdi::resample of a 6000 x 4000 bitmap without a buffer of every source row; a JPEG's EXIF thumbnail (thumbs::exif_preview:
// as stored, turned by Orientation 6, fitted and enlarged, none past the first 64 KB or without one, never cached).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <set>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <jpeglib.h>
#include <png.h>
#include "gdi/image.h"
#include "gdi/png.h"
#include "shell/config.h"
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

// ---- thumbnailer entries ------------------------------------------------------------------

static bool put(const std::string& path, const std::string& text, mode_t mode = 0644)
{
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return false;
    bool ok = fputs(text.c_str(), f) >= 0;
    ok = fclose(f) == 0 && ok;
    return ok && chmod(path.c_str(), mode) == 0;
}

static std::string slurp(const std::string& path)
{
    std::string s;
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

static std::vector<std::string> lines(const std::string& s)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        size_t j = s.find('\n', i);
        if (j == std::string::npos) j = s.size();
        out.push_back(s.substr(i, j - i));
        i = j + 1;
    }
    return out;
}

// a process that is gone (or a zombie nobody waits for)
static bool gone(int pid)
{
    std::string stat = slurp("/proc/" + std::to_string(pid) + "/stat");
    if (stat.empty()) return true;
    size_t p = stat.rfind(')');
    return p != std::string::npos && p + 2 < stat.size() && stat[p + 2] == 'Z';
}

static double now_s()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

// Stand-in entries in a scratch XDG_DATA_HOME: a script that writes the requested size's page (its arguments and
// environment into files), one that sleeps past the timeout with a child of its own, one whose TryExec is missing and
// one for text/plain (a parent type); the Exec line's splitting and field codes; the cache write and its reuse; the
// failure marker; the output cap; lhde's own entries after the others
static void thumbnailer_checks()
{
    using namespace shell::thumbs;
    struct stat st;
    if (stat("/usr/share/mime/globs2", &st) != 0) { printf("note: no /usr/share/mime: the thumbnailer entries are not checked\n"); return; }
    std::string data = dir + "/data/thumbnailers", sys = dir + "/sys";
    std::string cmd = "mkdir -p '" + data + "' '" + sys + "' && ln -s /usr/share/mime '" + sys + "/mime'";
    if (system(cmd.c_str()) != 0) { check("scratch data directories", false); return; }
    setenv("DISPLAY", ":thumbstest", 1);   // not passed on

    // the pages the stand-in hands out: 90 x 128 and 181 x 256, a navy block on white
    for (int s : { 128, 256 }) {
        gdi::Bitmap* p = gdi::create_bitmap(s == 128 ? 90 : 181, s);
        for (int y = 0; y < p->height; y++)
            for (int x = 0; x < p->width; x++)
                p->pixels[(size_t)y * p->stride + x] = y < p->height / 3 ? 0xff000080 : 0xffffffff;
        check("write a page", gdi::write_png(p, dir + "/page-" + std::to_string(s) + ".png", {}));
        gdi::destroy_bitmap(p);
    }
    check("write the stand-ins",
          put(dir + "/standin.sh", "#!/bin/sh\nprintf '%s\\n' \"$@\" > '" + dir + "/args'\nenv > '" + dir + "/env'\n"
              "cp '" + dir + "/page-'\"$3\".png \"$2\"\n", 0755) &&
          put(dir + "/sleeper.sh", "#!/bin/sh\nsleep 30 &\necho $! > '" + dir + "/sleeper.pid'\necho $$ >> '" + dir + "/sleeper.pid'\nwait\n", 0755) &&
          put(data + "/a-missing.thumbnailer", "[Thumbnailer Entry]\nTryExec=/nonexistent/thumbnailer\nExec=/nonexistent/thumbnailer %i %o\n"
              "MimeType=application/pdf;\n") &&
          put(data + "/b-page.thumbnailer", "# a comment\n[Thumbnailer Entry]\nTryExec=" + dir + "/standin.sh\n"
              "Exec=" + dir + "/standin.sh %i %o %s %u\nMimeType=application/x-pdf;\n") &&
          put(data + "/c-sleeper.thumbnailer", "[Thumbnailer Entry]\nExec=" + dir + "/sleeper.sh %i %o\nMimeType=application/postscript;\n") &&
          put(data + "/d-text.thumbnailer", "[Thumbnailer Entry]\nExec=" + dir + "/standin.sh %i %o %s\nMimeType=text/plain;\n") &&
          put(data + "/e-broken.thumbnailer", "[Other Group]\nExec=true\nMimeType=application/pdf;\n"));
    reload_thumbnailers();

    // the order: this directory's by name, lhde's own after them; an entry without its group is none
    std::vector<const Thumbnailer*> all = thumbnailers();
    std::string names;
    size_t first_lhde = all.size(), last_other = 0;
    for (size_t i = 0; i < all.size(); i++) {
        names += all[i]->name + " ";
        if (all[i]->name.compare(0, 5, "lhde-") == 0) { if (first_lhde == all.size()) first_lhde = i; }
        else last_other = i;
    }
    check("entries: the four, by name", all.size() >= 4 && all[0]->name == "a-missing.thumbnailer" && all[1]->name == "b-page.thumbnailer" &&
          all[2]->name == "c-sleeper.thumbnailer" && all[3]->name == "d-text.thumbnailer", names);
    check("entries: no e-broken", names.find("e-broken") == std::string::npos, names);
    check("entries: lhde's own last", first_lhde == all.size() || first_lhde > last_other, names);
    check("entries: lhde's PDF entry read", names.find("lhde-pdf.thumbnailer") != std::string::npos, names);
    check("entries: MimeType canonical (application/x-pdf is application/pdf)", all.size() >= 2 && all[1]->types.size() == 1 &&
          all[1]->types[0] == "application/pdf", all.size() >= 2 && !all[1]->types.empty() ? all[1]->types[0] : "");

    // the Exec line: quotes, a backslash, %% and the field codes, each a whole argument
    Thumbnailer t;
    t.exec = "run \"a b\" 'c d' e\\ f \"g\\\"h\" 100%% -s%s %i %o %u %x";
    std::vector<std::string> argv = thumbnailer_argv(t, "/x/it's a $page.pdf", "/out dir/o.png", 128);
    std::vector<std::string> want = { "run", "a b", "c d", "e f", "g\"h", "100%", "-s128", "/x/it's a $page.pdf", "/out dir/o.png",
                                      "file:///x/it's%20a%20$page.pdf" };
    std::string got;
    for (auto& a : argv) got += "[" + a + "]";
    check("Exec split and expanded", argv == want, got);

    // which entry: TryExec missing skips a-missing; the exact type before a parent; a parent's entry; own pictures none
    std::string pdf = dir + "/it's a page.pdf", ps = dir + "/sleep.ps", c = dir + "/code.c";
    check("write the documents", put(pdf, "%PDF-1.4 stand-in\n") && put(ps, "%!PS\n") && put(c, "int main;\n"));
    const Thumbnailer* by = thumbnailer_for(pdf);
    check("PDF: b-page (a-missing's TryExec is not there)", by && by->name == "b-page.thumbnailer", by ? by->name : "none");
    const Thumbnailer* byps = thumbnailer_for(ps);
    check("PostScript: its own type's entry before text/plain's", byps && byps->name == "c-sleeper.thumbnailer", byps ? byps->name : "none");
    const Thumbnailer* byc = thumbnailer_for(c);
    check("C source: text/plain's entry (a parent type)", byc && byc->name == "d-text.thumbnailer", byc ? byc->name : "none");
    check("a JPEG: flxp's own", !thumbnailer_for(dir + "/big.jpg") && can_thumbnail(dir + "/big.jpg") && own_picture(dir + "/big.jpg"));
    check("no type: none", !thumbnailer_for(dir + "/noextension") && !can_thumbnail(dir + "/noextension"));
    check("a PDF can be thumbnailed", can_thumbnail(pdf) && !own_picture(pdf));

    // a run: the page at 128, the arguments as Exec says, a minimal environment, the cache written and read back
    int runs = thumbnailer_runs();
    gdi::Bitmap* page = get(pdf, 0, 128, by);
    check("PDF thumbnail made", page && page->width == 90 && page->height == 128,
          page ? std::to_string(page->width) + "x" + std::to_string(page->height) : "none");
    if (page) {
        check("the page's pixels", page->pixels[0] == 0xff000080 && page->pixels[(size_t)100 * page->stride + 45] == 0xffffffff);
        gdi::destroy_bitmap(page);
    }
    check("one child", thumbnailer_runs() == runs + 1, std::to_string(thumbnailer_runs() - runs));
    std::vector<std::string> args = lines(slurp(dir + "/args"));
    std::string cache = dir + "/cache/thumbnails";
    check("%i: the path", args.size() == 4 && args[0] == pdf, args.empty() ? "" : args[0]);
    check("%o: a .png in the thumbnail directory", args.size() == 4 && args[1].compare(0, cache.size() + 1, cache + "/") == 0 &&
          args[1].size() > 4 && args[1].compare(args[1].size() - 4, 4, ".png") == 0 && stat(args[1].c_str(), &st) != 0,
          args.size() > 1 ? args[1] : "");
    check("%s: 128", args.size() == 4 && args[2] == "128");
    check("%u: the URI", args.size() == 4 && args[3] == shell::uri_from_path(pdf), args.size() == 4 ? args[3] : "");
    std::string env = slurp(dir + "/env");
    check("environment: no DISPLAY, no XDG_*", env.find("DISPLAY=") == std::string::npos && env.find("XDG_") == std::string::npos &&
          env.find("PATH=") != std::string::npos);
    std::string cached = cache_path(pdf, 128);
    std::string png = slurp(cached);
    gdi::PngText text = gdi::read_png_text((const uint8_t*)png.data(), png.size());
    std::string uri;
    bool sized = false;
    for (auto& kv : text) { if (kv.first == "Thumb::URI") uri = kv.second; if (kv.first == "Thumb::Image::Width") sized = true; }
    check("cached in normal/ with Thumb::URI", uri == shell::uri_from_path(pdf), cached);
    check("cached without Thumb::Image::Width", !sized);
    page = get(pdf, 0, 128, by);
    check("read back, no second child", page && thumbnailer_runs() == runs + 1);
    if (page) gdi::destroy_bitmap(page);

    // through a loader at 256 (its request picks the entry): the large page
    {
        shell::thumbs::Loader loader(nullptr, nullptr, 1);
        loader.request(loader.generation(), 1, pdf, 0, 256);
        for (int i = 0; i < 500 && loader.busy(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        shell::thumbs::Loader::Result r;
        bool ok = loader.take(r);
        check("loader: the 256 px page", ok && r.bmp && r.bmp->width == 181 && r.bmp->height == 256);
        if (ok) shell::thumbs::free_result(r);
        check("loader: a second child", thumbnailer_runs() == runs + 2);
    }

    // the output cap: an output above it is not read, the file gets the failure marker
    std::string pdf2 = dir + "/second.pdf";
    check("write the second PDF", put(pdf2, "%PDF-1.4 stand-in\n"));
    set_thumbnailer_limits(10000, 64);
    page = get(pdf2, 0, 128, by);
    check("output above the cap: none", !page);
    if (page) gdi::destroy_bitmap(page);
    set_thumbnailer_limits(10000, 16u << 20);
    std::string fail = cache + "/fail/flxp-1.0/" + md5_hex(shell::uri_from_path(pdf2)) + ".png";
    check("output above the cap: the failure marker", stat(fail.c_str(), &st) == 0);
    runs = thumbnailer_runs();
    page = get(pdf2, 0, 128, by);
    check("a failed file is not tried again", !page && thumbnailer_runs() == runs);
    if (page) gdi::destroy_bitmap(page);

    // the timeout: the child and what it started killed, the leftover output removed
    set_thumbnailer_limits(500, 16u << 20);
    double t0 = now_s();
    page = get(ps, 0, 128, byps);
    double took = now_s() - t0;
    set_thumbnailer_limits(10000, 16u << 20);
    check("timeout: none", !page);
    if (page) gdi::destroy_bitmap(page);
    check("timeout: after about half a second", took >= 0.4 && took < 3, std::to_string(took) + " s");
    std::vector<std::string> pids = lines(slurp(dir + "/sleeper.pid"));
    bool all_gone = pids.size() == 2;
    for (int i = 0; i < 50 && all_gone && !(gone(atoi(pids[0].c_str())) && gone(atoi(pids[1].c_str()))); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    for (auto& p : pids) all_gone = all_gone && gone(atoi(p.c_str()));
    check("timeout: the script and its sleep killed", all_gone, slurp(dir + "/sleeper.pid"));
    int left = 0;
    if (DIR* d = opendir(cache.c_str())) {
        while (struct dirent* e = readdir(d)) left += strncmp(e->d_name, ".flxp-", 6) == 0;
        closedir(d);
    }
    check("no temporary output left", left == 0, std::to_string(left));
}

// ---- a JPEG's EXIF thumbnail (thumbs::exif_preview) -------------------------------------------------------------------

static std::string bytes_of(const std::string& path)
{
    std::string s;
    shell::read_file(path, s);
    return s;
}

// main with an APP1 Exif block right after SOI: IFD0 with Orientation, IFD1 with the thumbnail as JPEGInterchangeFormat;
// pad: an APP2 of that many bytes before it (an EXIF block past the first 64 KB)
static std::string with_exif(const std::string& main, const std::string& thumb, int orientation, size_t pad)
{
    auto le16 = [](std::string& s, unsigned v) { s += (char)(v & 255); s += (char)(v >> 8); };
    auto le32 = [&](std::string& s, uint32_t v) { le16(s, v & 0xffff); le16(s, v >> 16); };
    std::string t = "II*";
    t += '\0';
    le32(t, 8);
    uint32_t ifd1 = 8 + 2 + 12 + 4, data = ifd1 + 2 + 2 * 12 + 4;
    le16(t, 1); le16(t, 0x0112); le16(t, 3); le32(t, 1); le16(t, (unsigned)orientation); le16(t, 0); le32(t, ifd1);
    le16(t, 2); le16(t, 0x0201); le16(t, 4); le32(t, 1); le32(t, data); le16(t, 0x0202); le16(t, 4); le32(t, 1); le32(t, (uint32_t)thumb.size());
    le32(t, 0);
    t += thumb;
    std::string app1 = "\xff\xe1";
    size_t len = 2 + 6 + t.size();
    app1 += (char)(len >> 8); app1 += (char)(len & 255);
    app1 += std::string("Exif\0\0", 6) + t;
    std::string out = main.substr(0, 2);
    while (pad > 0) {   // APP2 segments of filler
        size_t n = pad > 60000 ? 60000 : pad;
        out += "\xff\xe2"; out += (char)((n + 2) >> 8); out += (char)((n + 2) & 255); out += std::string(n, 'x');
        pad -= n;
    }
    return out + app1 + main.substr(2);
}

static void exif_checks()
{
    std::string th = dir + "/exif-thumb.jpg", main = dir + "/exif-main.jpg";
    check("write the EXIF thumbnail", write_jpeg(th, 160, 120, false));
    check("write the EXIF picture", write_jpeg(main, 1600, 1200, false));
    std::string tb = bytes_of(th), mb = bytes_of(main);
    std::string turned = dir + "/turned.jpg", plain = dir + "/plain.jpg", far = dir + "/far.jpg";
    shell::write_file_atomic(turned, with_exif(mb, tb, 6, 0));
    shell::write_file_atomic(plain, with_exif(mb, tb, 1, 0));
    shell::write_file_atomic(far, with_exif(mb, tb, 1, 70000));
    int w = 0, h = 0;
    int before = shell::thumbs::previews();
    gdi::Bitmap* b = shell::thumbs::exif_preview(plain, 0, &w, &h);
    check("EXIF thumbnail found", b != nullptr);
    check("EXIF thumbnail as stored", b && b->width == 160 && b->height == 120, b ? std::to_string(b->width) + "x" + std::to_string(b->height) : "");
    check("the picture's own size", w == 1600 && h == 1200, std::to_string(w) + "x" + std::to_string(h));
    if (b) gdi::destroy_bitmap(b);
    b = shell::thumbs::exif_preview(turned, 0, &w, &h);
    check("Orientation 6: turned", b && b->width == 120 && b->height == 160, b ? std::to_string(b->width) + "x" + std::to_string(b->height) : "");
    check("Orientation 6: the picture's size turned", w == 1200 && h == 1600, std::to_string(w) + "x" + std::to_string(h));
    if (b) gdi::destroy_bitmap(b);
    b = shell::thumbs::exif_preview(plain, 96);
    check("fitted into 96", b && b->width == 96 && b->height == 72, b ? std::to_string(b->width) + "x" + std::to_string(b->height) : "");
    if (b) gdi::destroy_bitmap(b);
    b = shell::thumbs::exif_preview(plain, 256);
    check("enlarged to 256", b && b->width == 256 && b->height == 192, b ? std::to_string(b->width) + "x" + std::to_string(b->height) : "");
    if (b) gdi::destroy_bitmap(b);
    check("previews counted", shell::thumbs::previews() - before == 4, std::to_string(shell::thumbs::previews() - before));
    b = shell::thumbs::exif_preview(far, 0);
    check("an EXIF block past the first 64 KB: none", b == nullptr);
    if (b) gdi::destroy_bitmap(b);
    b = shell::thumbs::exif_preview(main, 0);
    check("no EXIF block: none", b == nullptr);
    if (b) gdi::destroy_bitmap(b);
    // never in the cache: a full thumbnail is still to be made
    check("nothing cached", shell::thumbs::cached_thumbnail(plain, 128) == nullptr);
}

int main()
{
    const char* t = getenv("TMPDIR");
    std::string base = std::string(t && *t ? t : "/tmp") + "/thumbstest.XXXXXX";
    std::vector<char> b(base.begin(), base.end());
    b.push_back(0);
    if (!mkdtemp(b.data())) { printf("FAIL no scratch directory\n"); return 1; }
    dir = b.data();
    setenv("XDG_CACHE_HOME", (dir + "/cache").c_str(), 1);
    // the thumbnailer entries: the test's own (XDG_DATA_HOME), the system's MIME database without its entries, lhde's
    setenv("XDG_DATA_HOME", (dir + "/data").c_str(), 1);
    setenv("XDG_DATA_DIRS", (dir + "/sys").c_str(), 1);
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

    // resampling holds the horizontal pass of the rows one output row reads, not of every source row: a 6000 x 4000
    // bitmap into 800 x 533 grows the peak by the result (1.6 MB) and a few rows, not by 4000 rows of 800 floats (51 MB);
    // the result is the area average (a left half red and a right half blue stay so, the middle column between)
    {
        gdi::Bitmap* src = gdi::create_bitmap(6000, 4000);
        for (int y = 0; y < 4000; y++)
            for (int x = 0; x < 6000; x++) src->row(y)[x] = x < 3000 ? 0xffff0000u : 0xff0000ffu;
        gdi::Bitmap* out = nullptr;
        g = peak_growth([&] { out = gdi::resample(src, 800, 533); });
        check("resample 6000 x 4000 to 800 x 533", out && out->width == 800 && out->height == 533);
        check("resample: peak RSS grows by less than 8 MB", g < 8 * 1024, kb(g));
        check("resample: the left half red", out && out->row(300)[100] == 0xffff0000u);
        check("resample: the right half blue", out && out->row(532)[700] == 0xff0000ffu);
        if (out) gdi::destroy_bitmap(out);
        gdi::destroy_bitmap(src);
    }

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

    thumbnailer_checks();
    exif_checks();

    std::string cmd = "rm -rf '" + dir + "'";
    if (system(cmd.c_str()) != 0) printf("note: %s failed\n", cmd.c_str());
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
