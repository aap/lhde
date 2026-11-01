// Text rendering test: draws a few GDI-style fonts into a bitmap, prints their
// metrics and writes a PPM. Usage: build/text [out.ppm]
// Extra font dirs come from TEXT_FONT_DIRS (colon-separated).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "gdi/font.h"

using gdi::rgb;
using gdi::Point;

static bool write_ppm(const gdi::Bitmap* b, const char* path)
{
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", b->width, b->height);
    for (int32_t y = 0; y < b->height; y++) {
        const uint32_t* row = b->row(y);
        for (int32_t x = 0; x < b->width; x++) {
            gdi::Color c = gdi::unpack(row[x]);
            uint8_t px[3] = {c.r, c.g, c.b};
            fwrite(px, 1, 3, f);
        }
    }
    fclose(f);
    return true;
}

static void add_path_dirs(const char* path)
{
    if (!path) return;
    std::string s = path;
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find(':', i);
        if (j == std::string::npos) j = s.size();
        if (j > i) gdi::add_font_dir(s.substr(i, j - i).c_str());
        i = j + 1;
    }
}

static void print_tm(const char* name, const gdi::Font* f)
{
    gdi::TextMetrics tm = gdi::metrics(f);
    printf("%-28s height %2d ascent %2d descent %d int.lead %d ext.lead %d avg %d max %2d %s\n",
           name, tm.height, tm.ascent, tm.descent, tm.internal_leading, tm.external_leading,
           tm.ave_char_width, tm.max_char_width, tm.fixed_pitch ? "fixed" : "variable");
}

int main(int argc, char** argv)
{
    const char* out = argc > 1 ? argv[1] : "text.ppm";
    add_path_dirs(getenv("TEXT_FONT_DIRS"));

    gdi::Bitmap* bmp = gdi::create_bitmap(480, 320);
    gdi::DC* dc = gdi::create_dc(bmp);
    gdi::fill_rect(dc, bmp->rect(), gdi::white);

    const char* pangram = "The quick brown fox jumps over the lazy dog 0123456789 \xe2\x80\x94 ";
    struct { const char* family; int32_t height, weight; const char* label; } fonts[] = {
        {"Tahoma", -11, 400, "Tahoma 8pt"},
        {"Tahoma", -11, 700, "Tahoma 8pt bold"},
        {"Microsoft Sans Serif", -11, 400, "MS Sans Serif 8pt"},
        {"Tahoma", -13, 400, "Tahoma 10pt"},
        {"Tahoma", -16, 400, "Tahoma 16px"},
    };
    int32_t y = 4;
    gdi::set_font_smoothing(gdi::SMOOTH_OFF);   // mono first, the smoothing block below turns it on
    gdi::Font* tahoma = nullptr;
    for (auto& fd : fonts) {
        gdi::Font* f = gdi::create_font(fd.family, fd.height, fd.weight);
        if (!f) { printf("%s: no font\n", fd.label); continue; }
        print_tm(fd.label, f);
        std::string s = std::string(pangram) + fd.label;
        gdi::text_out(dc, Point{4, y}, f, s.c_str(), -1, gdi::black);
        gdi::Size sz = gdi::text_extent(f, s.c_str());
        printf("%-28s extent %dx%d\n", "", sz.cx, sz.cy);
        y += gdi::metrics(f).height + 4;
        if (!tahoma) tahoma = f; else gdi::destroy_font(f);
    }

    // underline + strikeout
    gdi::FontDesc ud{"Tahoma", -11, 400, false, true, false};
    gdi::Font* ul = gdi::create_font(ud);
    gdi::text_out(dc, Point{4, y}, ul, "Underlined Tahoma 8pt, File Edit View Help", -1, gdi::black);
    gdi::FontDesc sd{"Tahoma", -11, 400, false, false, true};
    gdi::Font* so = gdi::create_font(sd);
    gdi::text_out(dc, Point{260, y}, so, "Struck out text", -1, rgb(128, 0, 0));
    y += 17;

    // opaque background: cell rect, and an explicit rect
    gdi::text_out(dc, Point{4, y}, tahoma, "Selected item (cell opaque)", -1, gdi::white, rgb(49, 106, 197), nullptr);
    gdi::Rect r = gdi::rect_xywh(200, y - 2, 200, 17);
    gdi::text_out(dc, Point{204, y}, tahoma, "Explicit opaque rect", -1, gdi::black, rgb(255, 255, 225), &r);
    y += 19;

    // smoothing on, for comparison: 8pt stays mono via gasp, 16px too; 20px goes gray
    gdi::set_font_smoothing(true);
    gdi::text_out(dc, Point{4, y}, tahoma, "Smoothing on, Tahoma 8pt (gasp says mono)", -1, gdi::black);
    y += 15;
    gdi::Font* big = gdi::create_font("Tahoma", -20);
    print_tm("Tahoma 20px", big);
    gdi::text_out(dc, Point{4, y}, big, "Smoothing on, Tahoma 20px", -1, gdi::black);
    y += gdi::metrics(big).height + 2;
    gdi::Font* mss = gdi::create_font("MS Sans Serif", -11, 700);
    print_tm("MS Sans Serif 8pt fake bold", mss);
    gdi::text_out(dc, Point{4, y}, mss, "Smoothing on, MS Sans Serif fake bold (gray)", -1, gdi::black);
    gdi::set_font_smoothing(false);
    gdi::text_out(dc, Point{300, y}, mss, "same, smoothing off", -1, gdi::black);
    y += 17;

    // clipping: only [40,140) x [y+3, y+9) is visible, offset by an origin
    gdi::fill_rect(dc, gdi::rect_xywh(30, y - 2, 120, 17), rgb(230, 230, 230));
    gdi::Region clip(gdi::rect(40, y + 3, 140, y + 9));
    clip.add(gdi::rect(160, y, 230, y + 13));
    gdi::set_clip(dc, clip);
    gdi::set_origin(dc, Point{10, 0});
    gdi::text_out(dc, Point{20, y}, tahoma, "Clipped text, clipped text, clipped text", -1, gdi::black);
    gdi::set_origin(dc, Point{0, 0});
    gdi::set_clip(dc, gdi::Region(bmp->rect()));
    gdi::frame_rect(dc, gdi::rect(39, y + 2, 141, y + 10), rgb(255, 0, 0));
    gdi::frame_rect(dc, gdi::rect(159, y - 1, 231, y + 14), rgb(255, 0, 0));

    printf("Tahoma 8pt: 'W' %d 'i' %d 'e' %d, extent(\"Help\") %d\n", gdi::char_width(tahoma, 'W'),
           gdi::char_width(tahoma, 'i'), gdi::char_width(tahoma, 'e'), gdi::text_extent(tahoma, "Help").cx);

    // gray pixel census for the first (mono, Tahoma 8pt) line
    int gray = 0;
    gdi::TextMetrics tm = gdi::metrics(tahoma);
    for (int32_t yy = 4; yy < 4 + tm.height; yy++)
        for (int32_t x = 0; x < bmp->width; x++) {
            uint32_t p = bmp->row(yy)[x] & 0xffffff;
            if (p != 0 && p != 0xffffff) gray++;
        }
    printf("Tahoma 8pt line: %d non-black/white pixels\n", gray);

    gdi::destroy_font(tahoma);
    gdi::destroy_font(ul);
    gdi::destroy_font(so);
    gdi::destroy_font(big);
    gdi::destroy_font(mss);
    gdi::destroy_dc(dc);
    bool ok = write_ppm(bmp, out);
    gdi::destroy_bitmap(bmp);
    return ok ? 0 : 1;
}
