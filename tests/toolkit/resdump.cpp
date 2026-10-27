// resdump <file>                          list resources
// resdump <file> <type> <name> <out>      extract one resource
// resdump <file> --bmp <name> <out.ppm>   decode an RT_BITMAP to PPM (or 32bpp BMP if out ends in .bmp)
// resdump <file> --string <id>            print a string table entry
// Types/names: decimal numbers, RT_xxx names for types, anything else is a string id.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "res/res.h"
#include "gdi/bmp.h"

static res::Id parse_id(const char* s, bool is_type)
{
    if (*s && strspn(s, "0123456789") == strlen(s)) return res::Id((uint32_t)strtoul(s, nullptr, 10));
    if (*s == '#' && s[1]) return res::Id((uint32_t)strtoul(s + 1, nullptr, 10));
    if (is_type && !strncmp(s, "RT_", 3))
        for (uint32_t t = 1; t < 256; t++) {
            const char* n = res::type_name(t);
            if (n && !strcmp(n, s)) return res::Id(t);
        }
    return res::Id(s);
}

static void print_id(const res::Id& id, bool is_type)
{
    const char* n = is_type && id.is_num() ? res::type_name(id.num) : nullptr;
    if (n) printf("%-16s", n);
    else if (id.is_num()) printf("%-16u", id.num);
    else printf("%-16s", ("\"" + res::to_utf8(id) + "\"").c_str());
}

int main(int argc, char** argv)
{
    if (argc != 2 && argc != 4 && argc != 5) {
        fprintf(stderr, "usage: resdump <file> [<type> <name> <out> | --bmp <name> <out.ppm> | --string <id>]\n");
        return 2;
    }
    res::Module* m = res::load(argv[1]);
    if (!m) { fprintf(stderr, "%s: not a PE or .res file, or no resources\n", argv[1]); return 1; }
    int ret = 0;
    if (argc == 2) {
        std::vector<res::Entry> v = res::list(m);
        for (const res::Entry& e : v) {
            print_id(e.type, true);
            printf(" ");
            print_id(e.name, false);
            printf(" lang %04x  %8zu bytes\n", e.lang, e.data.size);
        }
        printf("%zu resources\n", v.size());
    } else if (argc == 4 && !strcmp(argv[2], "--string")) {
        std::string s;
        if (res::load_string(m, (uint32_t)strtoul(argv[3], nullptr, 10), s)) printf("%s\n", s.c_str());
        else { fprintf(stderr, "string %s not found\n", argv[3]); ret = 1; }
    } else if (argc == 5 && !strcmp(argv[2], "--bmp")) {
        res::Blob b;
        gdi::Bitmap* bm = nullptr;
        if (!res::find(m, res::Id((uint32_t)res::RT_BITMAP), parse_id(argv[3], false), b))
            { fprintf(stderr, "bitmap %s not found\n", argv[3]); ret = 1; }
        else if (!(bm = gdi::decode_bmp(b.data, b.size)))
            { fprintf(stderr, "bitmap %s: cannot decode\n", argv[3]); ret = 1; }
        else {
            printf("%dx%d\n", bm->width, bm->height);
            size_t n = strlen(argv[4]);
            bool bmp = n > 4 && !strcmp(argv[4] + n - 4, ".bmp");
            if (!(bmp ? gdi::write_bmp(bm, argv[4]) : gdi::write_ppm(bm, argv[4]))) { perror(argv[4]); ret = 1; }
        }
        gdi::destroy_bitmap(bm);
    } else if (argc == 5) {
        res::Blob b;
        if (!res::find(m, parse_id(argv[2], true), parse_id(argv[3], false), b)) {
            fprintf(stderr, "resource %s/%s not found\n", argv[2], argv[3]);
            ret = 1;
        } else {
            FILE* f = fopen(argv[4], "wb");
            if (!f || fwrite(b.data, 1, b.size, f) != b.size || fclose(f)) { perror(argv[4]); ret = 1; }
        }
    } else {
        fprintf(stderr, "bad arguments\n");
        ret = 2;
    }
    res::unload(m);
    return ret;
}
