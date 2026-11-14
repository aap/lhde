// Font linking in the GDI shim (wux/gdi/font.h load_font_links): a character the font's face lacks is drawn and measured
// with a face that has it, everything else exactly as without linking.
//   build/tests/w32/fontlink [--fonts DIR]... [--ppm FILE]
// The fonts are lhde's (shell::setup_fonts: the imported Windows fonts of $LHDE_WINDOWS, share/fonts/fontlink.ini), then
// the system's; --fonts adds a font directory first. A check whose character no installed face has is skipped. Prints
// ok/FAIL/skip lines; exit 1 on a failure.
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "w32/w32.h"
#include "gdi/font.h"

namespace shell { void setup_fonts(); }

static int fails, skips;
static void check(bool ok, const std::string& what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) fails++;
}
static void skip(const std::string& what) { printf("skip %s\n", what.c_str()); skips++; }

static const int W = 480, H = 48, BASE = 32;
static HDC g_dc;
static uint32_t* g_bits;

static std::u16string u16(const char32_t* s)
{
    std::u16string o;
    for (; *s; s++) {
        char32_t c = *s;
        if (c >= 0x10000) { c -= 0x10000; o += (char16_t)(0xd800 + (c >> 10)); o += (char16_t)(0xdc00 + (c & 0x3ff)); }
        else o += (char16_t)c;
    }
    return o;
}

static HFONT make_font(const char* face, int height, int weight = FW_NORMAL, BYTE quality = DEFAULT_QUALITY)
{
    std::u16string f = w32::utf16_of(face);
    return CreateFontW(height, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, quality, 0, (LPCWSTR)f.c_str());
}
static gdi::Font* gdi_font(HFONT f) { return ((w32::Font*)f)->font; }

// the text drawn black on white at (10, BASE) on its baseline; the pixels
static std::vector<uint32_t> render(HFONT font, const std::u16string& s)
{
    RECT all = {0, 0, W, H};
    FillRect(g_dc, &all, (HBRUSH)GetStockObject(WHITE_BRUSH));
    HGDIOBJ old = SelectObject(g_dc, font);
    SetTextAlign(g_dc, TA_BASELINE | TA_LEFT);
    SetBkMode(g_dc, TRANSPARENT);
    SetTextColor(g_dc, RGB(0, 0, 0));
    ExtTextOutW(g_dc, 10, BASE, 0, nullptr, (LPCWSTR)s.data(), (UINT)s.size(), nullptr);
    SelectObject(g_dc, old);
    return std::vector<uint32_t>(g_bits, g_bits + W * H);
}

static int extent(HFONT font, const std::u16string& s)
{
    HGDIOBJ old = SelectObject(g_dc, font);
    SIZE sz = {0, 0};
    GetTextExtentPoint32W(g_dc, (LPCWSTR)s.data(), (int)s.size(), &sz);
    SelectObject(g_dc, old);
    return sz.cx;
}

// the columns that have ink (non-white) in rows [y0, y1)
static void ink(const std::vector<uint32_t>& px, int y0, int y1, int& x0, int& x1)
{
    x0 = W; x1 = -1;
    for (int y = y0; y < y1; y++)
        for (int x = 0; x < W; x++)
            if ((px[y * W + x] & 0xffffff) != 0xffffff) { if (x < x0) x0 = x; if (x > x1) x1 = x; }
}

static bool same_rows(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b, int y0, int y1)
{
    for (int y = y0 < 0 ? 0 : y0; y < y1 && y < H; y++)
        if (memcmp(&a[y * W], &b[y * W], W * 4)) return false;
    return true;
}

static std::string hexcp(char32_t c) { char b[16]; snprintf(b, sizeof b, "U+%04X", (unsigned)c); return b; }

int main(int argc, char** argv)
{
    const char* ppm = nullptr;
    shell::setup_fonts();
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--fonts") && i + 1 < argc) gdi::add_font_dir(argv[++i]);
        else if (!strcmp(argv[i], "--ppm") && i + 1 < argc) ppm = argv[++i];
    }
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof bmi);
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = W;
    bmi.bmiHeader.biHeight = -H;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    g_dc = CreateCompatibleDC(nullptr);
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(g_dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    SelectObject(g_dc, dib);
    g_bits = (uint32_t*)bits;

    struct Face { const char* name; int height; } faces[] = { {"Tahoma", -11}, {"Lucida Console", -13} };

    // 1. Latin: what the face has draws exactly as without linking (every smoothing and weight), and so does a Latin
    //    character beside one the face lacks
    const std::u16string latin = u16(U"aé The quick brown fox jumps over the lazy dog 0123456789");
    for (const Face& fc : faces)
        for (int weight : {FW_NORMAL, FW_BOLD})
            for (BYTE q : {(BYTE)DEFAULT_QUALITY, (BYTE)ANTIALIASED_QUALITY, (BYTE)NONANTIALIASED_QUALITY, (BYTE)CLEARTYPE_QUALITY}) {
                gdi::set_font_fallback(false);
                HFONT a = make_font(fc.name, fc.height, weight, q);
                std::vector<uint32_t> off = render(a, latin);
                int w_off = extent(a, latin);
                gdi::set_font_fallback(true);
                HFONT b = make_font(fc.name, fc.height, weight, q);
                std::vector<uint32_t> on = render(b, latin);
                char what[160];
                snprintf(what, sizeof what, "%s %d weight %d quality %d: Latin the same with and without linking", fc.name, fc.height, weight, q);
                check(off == on && w_off == extent(b, latin), what);
                DeleteObject(a); DeleteObject(b);
            }
    {
        HFONT f = make_font("Tahoma", -11);
        std::u16string a = u16(U"aé"), mixed = u16(U"aéж\U0001F600");
        std::vector<uint32_t> pa = render(f, a), pm = render(f, mixed);
        int wa = extent(f, a);
        bool same = true;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < 10 + wa - 1; x++) same = same && pa[y * W + x] == pm[y * W + x];
        check(same, "Tahoma: \"aé\" drawn the same before a linked ж and \U0001F600");
        DeleteObject(f);
    }

    // 2. each character the face lacks: drawn by a face that has it, not the missing glyph; the same pixels as that face
    //    drawn on its own (within the base font's ascent and descent); its advance is what GetTextExtentPoint32W counts
    const char32_t chars[] = { 0x436 /* ж */, 0x65e5 /* 日 */, 0x672c /* 本 */, 0xd55c /* 한 */, 0x1F600 /* 😀 */ };
    for (const Face& fc : faces) {
        HFONT base = make_font(fc.name, fc.height);
        TEXTMETRICW tm;
        HGDIOBJ old = SelectObject(g_dc, base);
        GetTextMetricsW(g_dc, &tm);
        SelectObject(g_dc, old);
        std::string base_family = gdi::glyph_family(gdi_font(base), 'a');
        for (char32_t c : chars) {
            std::string what = std::string(fc.name) + " " + std::to_string(fc.height) + " " + hexcp(c);
            std::string fam = gdi::glyph_family(gdi_font(base), c);
            if (fam.empty()) { skip(what + ": no installed face has it"); continue; }
            char32_t one[2] = { c, 0 };
            std::u16string s = u16(one);
            std::vector<uint32_t> drawn = render(base, s);
            gdi::set_font_fallback(false);
            HFONT nolink = make_font(fc.name, fc.height);
            std::vector<uint32_t> box = render(nolink, s);
            gdi::set_font_fallback(true);
            DeleteObject(nolink);
            if (fam == base_family) { printf("info %s: the face has it\n", what.c_str()); continue; }
            check(drawn != box, what + ": drawn by " + fam + ", not the missing glyph");
            HFONT direct = make_font(fam.c_str(), fc.height);
            std::vector<uint32_t> own = render(direct, s);
            check(same_rows(drawn, own, BASE - (int)tm.tmAscent, BASE + (int)tm.tmDescent), what + ": the pixels of " + fam + " itself on the same baseline");
            int adv = extent(base, s);
            check(adv == extent(direct, s), what + ": advance " + std::to_string(adv) + " = " + fam + "'s");
            int x0, x1;
            ink(drawn, 0, H, x0, x1);
            check(x1 >= x0, what + ": has ink");
            // the next character starts after the advance: "x" after the linked one lands where the extent says
            std::u16string sx = s + u"x";
            std::vector<uint32_t> px = render(base, sx), pxx = render(base, u"x");
            int ax0, ax1;
            ink(pxx, 0, H, ax0, ax1);
            // the x alone, moved by the advance, is the right part of the pair
            bool moved = true;
            for (int y = 0; y < H; y++)
                for (int x = ax0; x <= ax1; x++)
                    if (x + adv < W && pxx[y * W + x] != px[y * W + x + adv] && (pxx[y * W + x] & 0xffffff) != 0xffffff) moved = false;
            check(moved && extent(base, sx) == adv + extent(base, u"x"), what + ": a following x sits at the advance");
            DeleteObject(direct);
        }
        DeleteObject(base);
    }

    // 3. a default-ignorable character the face lacks (a variation selector) draws nothing and has no width
    {
        HFONT f = make_font("Tahoma", -11);
        if (gdi::glyph_family(gdi_font(f), 0xfe0f).empty()) {
            check(extent(f, u16(U"a️b")) == extent(f, u"ab") && render(f, u16(U"a️b")) == render(f, u"ab"),
                  "Tahoma: U+FE0F after a has no width and no ink");
        } else skip("Tahoma has U+FE0F");
        DeleteObject(f);
    }

    // 4. DrawText's word wrap measures as ExtTextOut draws: a mixed line in a box as wide as its first two words wraps
    //    and the space after them wraps after them, and the first words measure what GetTextExtentPoint32W says
    {
        HFONT f = make_font("Tahoma", -11);
        std::u16string first = u16(U"日本語 жжж"), all = first + u16(U" \U0001F600\U0001F600 abc");
        if (gdi::glyph_family(gdi_font(f), 0x65e5).empty()) skip("DrawText wrap: no face has U+65E5");
        else {
            HGDIOBJ old = SelectObject(g_dc, f);
            TEXTMETRICW tm;
            GetTextMetricsW(g_dc, &tm);
            int wfirst = extent(f, first), box = extent(f, first + u" ") + 1;   // the first words and the space after them
            RECT r = {0, 0, box, 0};
            DrawTextW(g_dc, (LPCWSTR)all.data(), (int)all.size(), &r, DT_WORDBREAK | DT_CALCRECT);
            check(r.bottom == 2 * tm.tmHeight, "DrawText wraps the mixed line once in a box " + std::to_string(box) + " wide (" +
                  std::to_string(r.bottom) + " = 2 x " + std::to_string(tm.tmHeight) + ")");
            RECT one = {0, 0, 1000, 0};
            DrawTextW(g_dc, (LPCWSTR)first.data(), (int)first.size(), &one, DT_SINGLELINE | DT_CALCRECT);
            check(one.right == wfirst, "DrawText's width of the first words = GetTextExtentPoint32W's (" + std::to_string(wfirst) + ")");
            SelectObject(g_dc, old);
        }
        DeleteObject(f);
    }

    // a picture of the mixed text for a look (--ppm)
    if (ppm) {
        HFONT f = make_font("Tahoma", -11);
        render(f, u16(U"Tahoma: aé жизнь 日本語 한국어 中文 \U0001F600\U0001F44D ☃"));
        FILE* fp = fopen(ppm, "wb");
        if (fp) {
            fprintf(fp, "P6\n%d %d\n255\n", W, H);
            for (int i = 0; i < W * H; i++) { uint32_t p = g_bits[i]; fputc(p >> 16 & 255, fp); fputc(p >> 8 & 255, fp); fputc(p & 255, fp); }
            fclose(fp);
        }
        DeleteObject(f);
    }
    printf("%d failure(s), %d skipped\n", fails, skips);
    return fails != 0;
}
