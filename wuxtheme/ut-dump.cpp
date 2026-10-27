// ut-dump: look into a visual style with libuxtheme (ut.h only).
//
//   ut-dump FILE [--color C] [--size S]                    format, schemes, sizes
//   ut-dump FILE --list [CLASS]                            every property as ini text
//   ut-dump FILE --get CLASS PART STATE PROP               one value (names or numbers)
//   ut-dump FILE --render CLASS PART STATE WxH out.png     DrawThemeBackground on transparent
//   ut-dump FILE --size-of CLASS PART STATE [min|true|draw]
//   ut-dump FILE --extract-streams DIR                     PNG/D3DX/BAML streams, IMAGEDATA, bitmaps
#include <errno.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include "wuxtheme/ut.h"

static void usage()
{
    fprintf(stderr,
        "usage: ut-dump FILE [--color C] [--size S] [--list [CLASS] | --get CLASS PART STATE PROP |\n"
        "               --render CLASS PART STATE WxH out.png | --size-of CLASS PART STATE [min|true|draw] |\n"
        "               --extract-streams DIR]\n");
    exit(2);
}

static std::vector<std::string> classes(ut_theme* t)
{
    int n = ut_list_classes(t, nullptr, 0);
    std::vector<const char*> v(n);
    ut_list_classes(t, v.data(), n);
    return std::vector<std::string>(v.begin(), v.end());
}

template <class F> static std::vector<int> list_ints(F f)
{
    int n = f(nullptr, 0);
    std::vector<int> v(n);
    f(v.data(), n);
    return v;
}

static std::vector<ut_prop_info> props(ut_theme* t, const char* cls, int part, int state)
{
    int n = ut_list_props(t, cls, part, state, nullptr, 0);
    std::vector<ut_prop_info> v(n);
    ut_list_props(t, cls, part, state, v.data(), n);
    return v;
}

static std::string name_or_num(const char* name, int n)
{
    return *name ? std::string(name) : std::to_string(n);
}

static std::string value_text(ut_theme* t, const char* cls, int part, int state, ut_prop_info p)
{
    char buf[4096];
    if (!ut_get_string(t, cls, part, state, p.id, buf, sizeof buf)) return "?";
    std::string v = buf;
    if (p.type == 200) {                                   // TMT_ENUM: v4 stores numbers
        int e;
        if (ut_get_enum(t, cls, part, state, p.id, &e)) {
            const char* n = ut_enum_name(t, p.id, e);
            if (*n) v = n;
        }
    }
    const void* d;
    size_t sz;
    if ((v[0] == '#' || v[0] == '*') && ut_get_stream(t, cls, part, state, p.id, &d, &sz))
        v += " (" + std::to_string(sz) + " bytes)";
    return v;
}

static void list(ut_theme* t, const char* only)
{
    for (const std::string& c : classes(t)) {
        if (only && strcasecmp(only, c.c_str())) continue;
        for (int part : list_ints([&](int* o, int m) { return ut_list_parts(t, c.c_str(), o, m); }))
            for (int state : list_ints([&](int* o, int m) { return ut_list_states(t, c.c_str(), part, o, m); })) {
                std::string sec = c;
                if (part) sec += "." + name_or_num(ut_part_name(t, c.c_str(), part), part);
                if (state) sec += "(" + name_or_num(ut_state_name(t, c.c_str(), part, state), state) + ")";
                printf("[%s]\n", sec.c_str());
                for (ut_prop_info p : props(t, c.c_str(), part, state))
                    printf("%s = %s\n", name_or_num(ut_prop_name(t, p.id), p.id).c_str(),
                           value_text(t, c.c_str(), part, state, p).c_str());
                printf("\n");
            }
    }
}

static int lookup(int v, const char* what, const char* s)
{
    if (v < 0) { fprintf(stderr, "ut-dump: unknown %s %s\n", what, s); exit(1); }
    return v;
}

static bool write_png(const char* path, const uint32_t* px, int w, int h)
{
    std::vector<uint8_t> rgba((size_t)w * h * 4);
    for (size_t i = 0; i < (size_t)w * h; i++) {         // premultiplied -> straight
        uint32_t p = px[i], a = p >> 24;
        uint8_t* o = &rgba[i * 4];
        for (int c = 0; c < 3; c++) {
            uint32_t v = p >> (16 - 8 * c) & 255;
            o[c] = a ? (uint8_t)((v * 255 + a / 2) / a > 255 ? 255 : (v * 255 + a / 2) / a) : 0;
        }
        o[3] = (uint8_t)a;
    }
    png_image img;
    memset(&img, 0, sizeof img);
    img.version = PNG_IMAGE_VERSION;
    img.width = w; img.height = h; img.format = PNG_FORMAT_RGBA;
    return png_image_write_to_file(&img, path, 0, rgba.data(), 0, nullptr) != 0;
}

static bool write_file(const std::string& path, const void* d, size_t n, const void* head = nullptr, size_t hn = 0)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = (!hn || fwrite(head, 1, hn, f) == hn) && fwrite(d, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

static std::string safe(std::string s)
{
    for (char& c : s) if (c == '/' || c == ':' || c == ' ' || c == '(' || c == ')') c = '_';
    return s;
}

static int extract(ut_theme* t, const char* dir)
{
    mkdir(dir, 0777);
    int count = 0;
    for (const std::string& c : classes(t))
        for (int part : list_ints([&](int* o, int m) { return ut_list_parts(t, c.c_str(), o, m); }))
            for (int state : list_ints([&](int* o, int m) { return ut_list_states(t, c.c_str(), part, o, m); }))
                for (ut_prop_info p : props(t, c.c_str(), part, state)) {
                    char v[512];
                    const void* d;
                    size_t n;
                    if (!ut_get_string(t, c.c_str(), part, state, p.id, v, sizeof v)) continue;
                    bool file = p.type == 206;                    // TMT_FILENAME
                    if (!file && v[0] != '#' && v[0] != '*') continue;
                    if (!ut_get_stream(t, c.c_str(), part, state, p.id, &d, &n)) continue;
                    std::string name = c;
                    if (part) name += "." + name_or_num(ut_part_name(t, c.c_str(), part), part);
                    if (state) name += "." + name_or_num(ut_state_name(t, c.c_str(), part, state), state);
                    name += "." + name_or_num(ut_prop_name(t, p.id), p.id);
                    const uint8_t* b = (const uint8_t*)d;
                    std::string ext = n > 4 && !memcmp(b, "\x89PNG", 4) ? ".png" : n > 4 && !memcmp(b, "xof ", 4) ? ".x"
                                    : file ? ".bmp" : ".bin";
                    std::string path = std::string(dir) + "/" + safe(name) + ext;
                    bool ok;
                    if (ext == ".bmp" && n >= 40 && memcmp(b, "BM", 2)) {        // RT_BITMAP: add the file header
                        uint32_t hsz, clr = 0, comp;
                        uint16_t bpp;
                        memcpy(&hsz, b, 4); memcpy(&bpp, b + 14, 2); memcpy(&comp, b + 16, 4);
                        if (hsz >= 36) memcpy(&clr, b + 32, 4);
                        uint32_t pal = (clr ? clr : bpp <= 8 ? 1u << bpp : 0) * 4 + (comp == 3 && hsz == 40 ? 12 : 0);
                        uint8_t fh[14] = {'B', 'M'};
                        uint32_t total = 14 + (uint32_t)n, off = 14 + hsz + pal;
                        memcpy(fh + 2, &total, 4); memcpy(fh + 10, &off, 4);
                        ok = write_file(path, b, n, fh, 14);
                    } else
                        ok = write_file(path, b, n);
                    if (!ok) { fprintf(stderr, "ut-dump: %s: %s\n", path.c_str(), strerror(errno)); return 1; }
                    printf("%s %zu\n", path.c_str(), n);
                    count++;
                }
    fprintf(stderr, "%d files\n", count);
    return 0;
}

int main(int argc, char** argv)
{
    const char *file = nullptr, *color = nullptr, *size = nullptr;
    std::vector<const char*> args;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--color") && i + 1 < argc) color = argv[++i];
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) size = argv[++i];
        else if (!file && argv[i][0] != '-') file = argv[i];
        else args.push_back(argv[i]);
    }
    if (!file) usage();
    if (const char* fp = getenv("UT_FONT_PATH")) ut_add_font_dir(fp);
    ut_theme* t = ut_open(file, color, size);
    if (!t) { fprintf(stderr, "ut-dump: %s: not a visual style this library reads\n", file); return 1; }
    int rc = 0;
    if (args.empty()) {
        printf("%s: PACKTHEM_VERSION %d, colour %s, size %s\n", file, ut_format(t), ut_color(t), ut_size(t));
        const char* names[64];
        int n = ut_list_colors(t, names, 64);
        printf("colours:");
        for (int i = 0; i < n && i < 64; i++) printf(" %s", names[i]);
        n = ut_list_sizes(t, names, 64);
        printf("\nsizes:");
        for (int i = 0; i < n && i < 64; i++) printf(" %s", names[i]);
        printf("\nclasses: %zu\n", classes(t).size());
    } else if (!strcmp(args[0], "--list")) {
        list(t, args.size() > 1 ? args[1] : nullptr);
    } else if (!strcmp(args[0], "--get") && args.size() == 5) {
        const char* c = args[1];
        int part = lookup(ut_part(t, c, args[2]), "part", args[2]);
        int state = lookup(ut_state(t, c, part, args[3]), "state", args[3]);
        int prop = lookup(ut_prop(t, args[4]), "property", args[4]);
        char buf[4096];
        if (!ut_get_string(t, c, part, state, prop, buf, sizeof buf)) { fprintf(stderr, "ut-dump: not set\n"); rc = 1; }
        else {
            printf("%s\n", value_text(t, c, part, state, ut_prop_info{prop, 0}).c_str());
            ut_font f;
            uint32_t rgb;
            if (strstr(ut_prop_name(t, prop), "FONT") && ut_get_font(t, c, part, state, prop, &f))
                printf("font: %s, height %d, weight %d%s%s\n", f.face, f.height, f.weight, f.italic ? ", italic" : "", f.underline ? ", underline" : "");
            else if (strstr(ut_prop_name(t, prop), "COLOR") && ut_get_color(t, c, part, state, prop, &rgb))
                printf("#%06x\n", rgb);
        }
    } else if (!strcmp(args[0], "--render") && args.size() == 6) {
        const char* c = args[1];
        int part = lookup(ut_part(t, c, args[2]), "part", args[2]);
        int state = lookup(ut_state(t, c, part, args[3]), "state", args[3]);
        int w, h;
        if (sscanf(args[4], "%dx%d", &w, &h) != 2 || w <= 0 || h <= 0) usage();
        std::vector<uint32_t> px((size_t)w * h, 0);
        ut_rect r = {0, 0, w, h};
        if (!ut_draw_background(t, c, part, state, px.data(), w, h, w * 4, &r, nullptr)) {
            fprintf(stderr, "ut-dump: cannot draw %s %d %d\n", c, part, state);
            rc = 1;
        } else if (!write_png(args[5], px.data(), w, h)) {
            fprintf(stderr, "ut-dump: cannot write %s\n", args[5]);
            rc = 1;
        }
    } else if (!strcmp(args[0], "--size-of") && (args.size() == 4 || args.size() == 5)) {
        const char* c = args[1];
        int part = lookup(ut_part(t, c, args[2]), "part", args[2]);
        int state = lookup(ut_state(t, c, part, args[3]), "state", args[3]);
        int which = args.size() == 5 ? (!strcmp(args[4], "min") ? 0 : !strcmp(args[4], "draw") ? 2 : 1) : 1;
        int cx, cy;
        if (!ut_part_size(t, c, part, state, which, nullptr, &cx, &cy)) { fprintf(stderr, "ut-dump: no size\n"); rc = 1; }
        else printf("%dx%d\n", cx, cy);
    } else if (!strcmp(args[0], "--extract-streams") && args.size() == 2) {
        rc = extract(t, args[1]);
    } else
        usage();
    ut_close(t);
    return rc;
}
