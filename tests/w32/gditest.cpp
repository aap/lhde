// GDI shim test: draws a classic-looking mock into a 32bpp DIB section via
// a memory DC, checks a handful of win32 semantics, and writes a PPM.
//   build/w32test/gditest [out.ppm]
#include <stdio.h>
#include <string.h>
#include "w32/w32.h"
#include "gdi/bmp.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void textw(HDC dc, int x, int y, const char* s)
{
    WCHAR buf[256];
    int n = 0;
    while (s[n] && n < 255) { buf[n] = (unsigned char)s[n]; n++; }
    TextOutW(dc, x, y, buf, n);
}

static void bevel_sunken(HDC dc, RECT r)
{
    HPEN gray = CreatePen(PS_SOLID, 1, RGB(128, 128, 128));
    HPEN white = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    HPEN dark = CreatePen(PS_SOLID, 1, RGB(64, 64, 64));
    HPEN light = CreatePen(PS_SOLID, 1, RGB(212, 208, 200));
    HGDIOBJ old = SelectObject(dc, gray);
    MoveToEx(dc, r.left, r.bottom - 1, NULL); LineTo(dc, r.left, r.top); LineTo(dc, r.right, r.top);
    SelectObject(dc, white);
    MoveToEx(dc, r.left, r.bottom - 1, NULL); LineTo(dc, r.right - 1, r.bottom - 1); LineTo(dc, r.right - 1, r.top - 1);
    SelectObject(dc, dark);
    MoveToEx(dc, r.left + 1, r.bottom - 2, NULL); LineTo(dc, r.left + 1, r.top + 1); LineTo(dc, r.right - 1, r.top + 1);
    SelectObject(dc, light);
    MoveToEx(dc, r.left + 1, r.bottom - 2, NULL); LineTo(dc, r.right - 2, r.bottom - 2); LineTo(dc, r.right - 2, r.top);
    SelectObject(dc, old);
    DeleteObject(gray); DeleteObject(white); DeleteObject(dark); DeleteObject(light);
}

static void focus_rect(HDC dc, HBRUSH pat, RECT r)
{
    HGDIOBJ old = SelectObject(dc, pat);
    COLORREF ot = SetTextColor(dc, RGB(0, 0, 0)), ob = SetBkColor(dc, RGB(255, 255, 255));
    int w = r.right - r.left, h = r.bottom - r.top;
    PatBlt(dc, r.left, r.top, w, 1, PATINVERT);
    PatBlt(dc, r.left, r.bottom - 1, w, 1, PATINVERT);
    PatBlt(dc, r.left, r.top + 1, 1, h - 2, PATINVERT);
    PatBlt(dc, r.right - 1, r.top + 1, 1, h - 2, PATINVERT);
    SetTextColor(dc, ot); SetBkColor(dc, ob);
    SelectObject(dc, old);
}

int main(int argc, char** argv)
{
    const char* out = argc > 1 ? argv[1] : "gditest.ppm";
    const int W = 360, H = 260;

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof bmi);
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = W;
    bmi.bmiHeader.biHeight = -H;       // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    void* bits = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP dib = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    CHECK(dib && bits);
    HGDIOBJ old_bmp = SelectObject(dc, dib);
    CHECK(GetObjectType(old_bmp) == OBJ_BITMAP);

    DIBSECTION ds;
    CHECK(GetObjectW(dib, sizeof ds, &ds) == sizeof ds);
    CHECK(ds.dsBm.bmWidth == W && ds.dsBm.bmHeight == H && ds.dsBm.bmBitsPixel == 32);
    CHECK(ds.dsBm.bmBits == bits && ds.dsBmih.biHeight == -H);

    // background: button face
    HBRUSH face = CreateSolidBrush(RGB(212, 208, 200));
    HGDIOBJ old_brush = SelectObject(dc, face);
    PatBlt(dc, 0, 0, W, H, PATCOPY);
    CHECK(((uint32_t*)bits)[0] == 0xffd4d0c8u);
    CHECK(GetPixel(dc, 5, 5) == RGB(212, 208, 200));

    // text with the GUI font
    HGDIOBJ old_font = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc, TRANSPARENT);
    textw(dc, 8, 6, "DEFAULT_GUI_FONT: The quick brown fox jumps");
    TEXTMETRICW tm;
    CHECK(GetTextMetricsW(dc, &tm));
    printf("tm: height %d ascent %d descent %d avew %d pitch %#x charset %d\n",
           (int)tm.tmHeight, (int)tm.tmAscent, (int)tm.tmDescent, (int)tm.tmAveCharWidth,
           tm.tmPitchAndFamily, tm.tmCharSet);
    CHECK(tm.tmPitchAndFamily & TMPF_FIXED_PITCH);
    LONG ch = 0;
    LONG cw = GdiGetCharDimensions(dc, NULL, &ch);
    printf("GdiGetCharDimensions: %ld x %ld\n", (long)cw, (long)ch);
    SIZE sz;
    static const WCHAR hello[] = {'H','e','l','l','o'};
    CHECK(GetTextExtentPoint32W(dc, hello, 5, &sz));
    INT fit = 0, dx[5];
    CHECK(GetTextExtentExPointW(dc, hello, 5, sz.cx - 1, &fit, dx, NULL));
    CHECK(fit == 4 && dx[4] == sz.cx);
    printf("extent(Hello) = %dx%d\n", (int)sz.cx, (int)sz.cy);

    // Rectangle: black pen, light gray brush
    SelectObject(dc, GetStockObject(BLACK_PEN));
    SelectObject(dc, GetStockObject(LTGRAY_BRUSH));
    Rectangle(dc, 8, 24, 58, 54);
    CHECK(GetPixel(dc, 8, 24) == RGB(0, 0, 0));
    CHECK(GetPixel(dc, 57, 53) == RGB(0, 0, 0));
    CHECK(GetPixel(dc, 58, 54) == RGB(212, 208, 200));
    CHECK(GetPixel(dc, 9, 25) == RGB(192, 192, 192));
    // null pen rectangle is one smaller at right/bottom
    SelectObject(dc, GetStockObject(NULL_PEN));
    SelectObject(dc, GetStockObject(WHITE_BRUSH));
    Rectangle(dc, 64, 24, 74, 34);
    CHECK(GetPixel(dc, 64, 24) == RGB(255, 255, 255));
    CHECK(GetPixel(dc, 72, 32) == RGB(255, 255, 255));
    CHECK(GetPixel(dc, 73, 33) == RGB(212, 208, 200));

    // sunken bevel (an edit box)
    SelectObject(dc, GetStockObject(WHITE_BRUSH));
    PatBlt(dc, 80, 24, 100, 21, PATCOPY);
    RECT er = {80, 24, 180, 45};
    bevel_sunken(dc, er);
    SetBkMode(dc, TRANSPARENT);
    textw(dc, 84, 28, "Edit text");

    // 55AA pattern brush focus rectangle around a label
    WORD pat[8];
    for (int i = 0; i < 8; i++) pat[i] = (i & 1) ? 0xaa : 0x55;
    HBITMAP pbm = CreateBitmap(8, 8, 1, 1, pat);
    HBRUSH b55 = CreatePatternBrush(pbm);
    DeleteObject(pbm);
    textw(dc, 194, 28, "Focused");
    GetTextExtentPoint32W(dc, L"Focused", 7, &sz);
    RECT fr = {192, 27, 192 + sz.cx + 4, 27 + sz.cy + 2};
    focus_rect(dc, b55, fr);

    // radio buttons: 13x13 ellipse (white fill, gray/black outline) and a 12x12
    SelectObject(dc, GetStockObject(BLACK_PEN));
    SelectObject(dc, GetStockObject(WHITE_BRUSH));
    Ellipse(dc, 8, 62, 21, 75);
    SelectObject(dc, GetStockObject(BLACK_BRUSH));
    Ellipse(dc, 12, 66, 17, 71);
    SelectObject(dc, GetStockObject(WHITE_BRUSH));
    Ellipse(dc, 26, 62, 38, 74);
    Ellipse(dc, 44, 60, 61, 77);
    CHECK(GetPixel(dc, 14, 62) == RGB(0, 0, 0));      // top centre of the 13px circle
    CHECK(GetPixel(dc, 8, 62) == RGB(212, 208, 200));  // corner untouched
    textw(dc, 66, 62, "Radio");

    // pie and polygon, dashed pen
    HBRUSH red = CreateSolidBrush(RGB(200, 40, 40));
    SelectObject(dc, red);
    Pie(dc, 110, 58, 140, 88, 140, 73, 125, 58);
    POINT star[5] = {{165, 58}, {173, 86}, {150, 68}, {180, 68}, {157, 86}};
    HBRUSH blue = CreateSolidBrush(RGB(40, 40, 200));
    SelectObject(dc, blue);
    Polygon(dc, star, 5);
    SetPolyFillMode(dc, WINDING);
    POINT star2[5] = {{205, 58}, {213, 86}, {190, 68}, {220, 68}, {197, 86}};
    Polygon(dc, star2, 5);
    SetPolyFillMode(dc, ALTERNATE);
    HPEN dash = CreatePen(PS_DOT, 1, RGB(0, 0, 0));
    SelectObject(dc, dash);
    SetBkMode(dc, TRANSPARENT);
    MoveToEx(dc, 230, 60, NULL); LineTo(dc, 350, 60);
    SetBkMode(dc, OPAQUE);
    SetBkColor(dc, RGB(255, 255, 0));
    MoveToEx(dc, 230, 64, NULL); LineTo(dc, 350, 64);
    SetBkColor(dc, RGB(255, 255, 255));
    SelectObject(dc, GetStockObject(BLACK_PEN));
    DeleteObject(dash);    // not selected anymore
    // diagonal lines in all octants from a centre
    for (int a = 0; a < 16; a++) {
        static const int ox[16] = {12, 12, 12, 6, 0, -6, -12, -12, -12, -12, -12, -6, 0, 6, 12, 12};
        static const int oy[16] = {0, 6, 12, 12, 12, 12, 12, 6, 0, -6, -12, -12, -12, -12, -12, -6};
        MoveToEx(dc, 290, 80, NULL);
        LineTo(dc, 290 + ox[a], 80 + oy[a]);
    }
    // XOR pen
    SetROP2(dc, R2_NOT);
    MoveToEx(dc, 230, 70, NULL); LineTo(dc, 270, 90);
    SetROP2(dc, R2_COPYPEN);

    // gradient caption bar
    TRIVERTEX v[2] = { {8, 100, 0x0a00, 0x2400, 0x6a00, 0}, {352, 118, 0xa600, 0xca00, 0xf000, 0} };
    GRADIENT_RECT gr = {0, 1};
    CHECK(GradientFill(dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_H));
    HFONT bold = CreateFontW(-11, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Tahoma");
    SelectObject(dc, bold);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    textw(dc, 12, 103, "Gradient caption - bold Tahoma");
    SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    DeleteObject(bold);
    SetTextColor(dc, RGB(0, 0, 0));
    TRIVERTEX tv[3] = { {300, 124, 0xff00, 0, 0, 0}, {352, 124, 0, 0xff00, 0, 0}, {326, 160, 0, 0, 0xff00, 0} };
    GRADIENT_TRIANGLE gt = {0, 1, 2};
    GradientFill(dc, tv, 3, &gt, 1, GRADIENT_FILL_TRIANGLE);
    TRIVERTEX vv[2] = { {280, 124, 0xff00, 0xff00, 0xff00, 0}, {296, 160, 0x4000, 0x4000, 0x4000, 0} };
    GradientFill(dc, vv, 2, &gr, 1, GRADIENT_FILL_RECT_V);

    // alpha blend: checkerboard, then a premultiplied semi-transparent bitmap
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 6; x++) {
            SelectObject(dc, GetStockObject(((x + y) & 1) ? GRAY_BRUSH : WHITE_BRUSH));
            PatBlt(dc, 8 + x * 8, 124 + y * 8, 8, 8, PATCOPY);
        }
    BITMAPINFO abi = bmi;
    abi.bmiHeader.biWidth = 32; abi.bmiHeader.biHeight = 32;   // bottom-up
    void* abits;
    HBITMAP abm = CreateDIBSection(dc, &abi, DIB_RGB_COLORS, &abits, NULL, 0);
    uint32_t* ap = (uint32_t*)abits;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int dx2 = x - 16, dy2 = y - 16;
            uint32_t a = dx2 * dx2 + dy2 * dy2 < 200 ? (uint32_t)(255 * x / 31) : 0;
            uint32_t r = 255 * a / 255, g = 64 * a / 255;
            ap[y * 32 + x] = a << 24 | r << 16 | g << 8;
        }
    HDC adc = CreateCompatibleDC(dc);
    HGDIOBJ aold = SelectObject(adc, abm);
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    CHECK(AlphaBlend(dc, 12, 124, 32, 32, adc, 0, 0, 32, 32, bf));
    BLENDFUNCTION bf2 = {AC_SRC_OVER, 0, 128, 0};
    CHECK(AlphaBlend(dc, 60, 124, 32, 32, adc, 0, 0, 32, 32, bf2));
    CHECK(!AlphaBlend(dc, 60, 124, 32, 32, adc, 10, 0, 32, 32, bf2));   // source out of bounds
    CHECK(AlphaBlend(dc, 100, 124, 48, 48, adc, 0, 0, 32, 32, bf));       // stretched
    // TransparentBlt and StretchBlt from the same source
    BitBlt(dc, 152, 124, 32, 32, adc, 0, 0, SRCCOPY);
    TransparentBlt(dc, 188, 124, 32, 32, adc, 0, 0, 32, 32, RGB(0, 0, 0));
    StretchBlt(dc, 224, 124, 64, 32, adc, 0, 0, 32, 32, SRCCOPY);
    SelectObject(adc, aold);

    // mono -> colour blit uses text/bk colours
    HBITMAP mono = CreateCompatibleBitmap(adc, 16, 16);
    BITMAP mbm;
    CHECK(GetObjectW(mono, sizeof mbm, &mbm) == sizeof mbm && mbm.bmBitsPixel == 1);
    SelectObject(adc, mono);
    PatBlt(adc, 0, 0, 16, 16, WHITENESS);
    PatBlt(adc, 4, 4, 8, 8, BLACKNESS);
    SetTextColor(dc, RGB(0, 128, 0));
    SetBkColor(dc, RGB(255, 255, 200));
    BitBlt(dc, 152, 162, 16, 16, adc, 0, 0, SRCCOPY);
    CHECK(GetPixel(dc, 160, 170) == RGB(0, 128, 0));
    CHECK(GetPixel(dc, 153, 163) == RGB(255, 255, 200));
    SetTextColor(dc, RGB(0, 0, 0));
    SetBkColor(dc, RGB(255, 255, 255));
    SelectObject(adc, aold);
    DeleteObject(mono);
    DeleteDC(adc);
    DeleteObject(abm);

    // clip region test: two rects OR'ed, then fill everything
    HRGN r1 = CreateRectRgn(8, 180, 40, 200);
    HRGN r2 = CreateRectRgn(30, 190, 70, 215);
    HRGN rx = CreateRectRgn(0, 0, 0, 0);
    CHECK(CombineRgn(rx, r1, r2, RGN_XOR) == COMPLEXREGION);
    CHECK(CombineRgn(rx, r1, r2, RGN_AND) == SIMPLEREGION);
    HRGN r3 = CreateRectRgn(100, 100, 100, 100);
    CHECK(CombineRgn(rx, r1, r3, RGN_AND) == NULLREGION);
    CombineRgn(r1, r1, r2, RGN_OR);
    CHECK(SelectClipRgn(dc, r1) == COMPLEXREGION);
    RECT cb;
    CHECK(GetClipBox(dc, &cb) == COMPLEXREGION && cb.left == 8 && cb.top == 180 && cb.right == 70 && cb.bottom == 215);
    HRGN got = CreateRectRgn(0, 0, 0, 0);
    CHECK(GetClipRgn(dc, got) == 1);
    SelectObject(dc, blue);
    PatBlt(dc, 0, 0, W, H, PATCOPY);
    CHECK(GetPixel(dc, 35, 195) == RGB(40, 40, 200));
    CHECK(GetPixel(dc, 50, 185) == CLR_INVALID);       // outside the clip
    // RGN_DIFF on top: punch a hole
    HRGN hole = CreateRectRgn(36, 194, 44, 202);
    ExtSelectClipRgn(dc, hole, RGN_DIFF);
    SelectObject(dc, GetStockObject(WHITE_BRUSH));
    PatBlt(dc, 0, 0, W, H, PATCOPY);
    SelectClipRgn(dc, NULL);
    CHECK(GetClipRgn(dc, got) == 0);
    // IntersectClipRect with a viewport origin (logical coordinates)
    SetViewportOrgEx(dc, 80, 180, NULL);
    IntersectClipRect(dc, 0, 0, 20, 20);
    CHECK(GetClipBox(dc, &cb) == SIMPLEREGION && cb.left == 0 && cb.top == 0 && cb.right == 20 && cb.bottom == 20);
    SelectObject(dc, red);
    PatBlt(dc, -10, -10, 100, 100, PATCOPY);
    SelectClipRgn(dc, NULL);
    SetViewportOrgEx(dc, 0, 0, NULL);
    CHECK(GetPixel(dc, 80, 180) == RGB(200, 40, 40) && GetPixel(dc, 100, 200) != RGB(200, 40, 40));
    DeleteObject(r1); DeleteObject(r2); DeleteObject(r3); DeleteObject(rx); DeleteObject(got); DeleteObject(hole);

    // text alignment around a guide at x = 200
    SelectObject(dc, GetStockObject(BLACK_PEN));
    SetBkMode(dc, TRANSPARENT);
    MoveToEx(dc, 200, 178, NULL); LineTo(dc, 200, 256);
    MoveToEx(dc, 110, 230, NULL); LineTo(dc, 290, 230);
    SetTextAlign(dc, TA_LEFT | TA_TOP);   textw(dc, 200, 180, "left/top");
    SetTextAlign(dc, TA_CENTER | TA_TOP); textw(dc, 200, 194, "center");
    SetTextAlign(dc, TA_RIGHT | TA_TOP);  textw(dc, 200, 208, "right");
    SetTextAlign(dc, TA_LEFT | TA_BASELINE); textw(dc, 120, 230, "baseline");
    SetTextAlign(dc, TA_RIGHT | TA_BOTTOM);  textw(dc, 280, 230, "bottom");
    SetTextAlign(dc, TA_LEFT | TA_TOP);
    // ETO_OPAQUE with a yellow background, and ETO_CLIPPED
    SetBkColor(dc, RGB(255, 255, 160));
    RECT orc = {210, 236, 352, 254};
    ExtTextOutW(dc, 214, 239, ETO_OPAQUE | ETO_CLIPPED, &orc, L"ETO_OPAQUE clipped to this rect", 31, NULL);
    CHECK(GetPixel(dc, 211, 237) == RGB(255, 255, 160));
    RECT orc2 = {120, 236, 150, 254};
    ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &orc2, NULL, 0, NULL);   // fill only
    CHECK(GetPixel(dc, 121, 237) == RGB(255, 255, 160));
    SetBkMode(dc, OPAQUE);
    SetBkColor(dc, RGB(160, 255, 160));
    INT spaced[6] = {9, 9, 9, 9, 9, 9};
    ExtTextOutW(dc, 300, 180, 0, NULL, L"lpDx!!", 6, spaced);
    SetTextAlign(dc, TA_UPDATECP);
    MoveToEx(dc, 300, 196, NULL);
    textw(dc, 0, 0, "CP");
    textw(dc, 0, 0, "+cp");
    POINT cp;
    GetCurrentPositionEx(dc, &cp);
    CHECK(cp.x > 300 && cp.y == 196);
    SetTextAlign(dc, TA_LEFT | TA_TOP);
    SetBkColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    TextOutA(dc, 300, 212, "UTF-8 \xc3\xa9\xc3\xa8", 10);

    // stock objects are not deletable; deleting a selected object is deferred
    CHECK(DeleteObject(GetStockObject(BLACK_PEN)));
    HPEN p = CreatePen(PS_SOLID, 1, RGB(1, 2, 3));
    SelectObject(dc, p);
    DeleteObject(p);
    MoveToEx(dc, 0, 259, NULL); LineTo(dc, 4, 259);      // still usable
    CHECK(GetPixel(dc, 1, 259) == RGB(1, 2, 3));
    SelectObject(dc, GetStockObject(BLACK_PEN));          // now it's really freed

    // GetDIBits round trip
    BITMAPINFO gi;
    memset(&gi, 0, sizeof gi);
    gi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    CHECK(GetDIBits(dc, dib, 0, 0, NULL, &gi, DIB_RGB_COLORS));
    CHECK(gi.bmiHeader.biWidth == W && gi.bmiHeader.biBitCount == 32);
    gi.bmiHeader.biHeight = H;      // bottom-up request
    static uint32_t rows[W * H];
    CHECK(GetDIBits(dc, dib, 0, H, rows, &gi, DIB_RGB_COLORS) == H);
    CHECK(rows[(H - 1) * W + 0] == ((uint32_t*)bits)[0]);

    printf("device caps: %d dpi, %d bpp, %dx%d\n", GetDeviceCaps(dc, LOGPIXELSX), GetDeviceCaps(dc, BITSPIXEL),
           GetDeviceCaps(dc, HORZRES), GetDeviceCaps(dc, VERTRES));

    w32::Bitmap* b = w32::as<w32::Bitmap>(dib, w32::T_BITMAP);
    gdi::write_ppm(b->bmp, out);
    printf("wrote %s\n", out);

    SelectObject(dc, old_font);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_bmp);
    DeleteObject(face); DeleteObject(red); DeleteObject(blue); DeleteObject(b55);
    DeleteObject(dib);
    DeleteDC(dc);
    printf("%s: %d failure(s)\n", failures ? "FAIL" : "ok", failures);
    return failures != 0;
}
