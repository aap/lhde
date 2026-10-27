// Exercises image.cpp/syscolor.cpp: icons, bitmaps and image lists from a
// .res module and from user32's built-in resources, drawn through DrawIconEx
// and ImageList_Draw into a hand-made memory DC; writes a PPM.
//   build/w32test/imagetest <comdlg32.res> <out.ppm>
#define OEMRESOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include "res/res.h"
#include "w32/image.h"
#include "gdi/bmp.h"
extern "C" {
#include "commoncontrols.h"
HPEN SYSCOLOR_GetPen(INT);
HBRUSH SYSCOLOR_Get55AABrush(void);
BOOL get_icon_size(HICON, SIZE*);
extern HBRUSH COMCTL32_hPattern55AABrush;
}

using namespace w32;

static int failures;
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); failures++; } } while (0)

static void checker(gdi::DC* dc, gdi::Rect r)
{
    for (int y = r.top; y < r.bottom; y += 4)
        for (int x = r.left; x < r.right; x += 4)
            gdi::fill_rect(dc, gdi::rect_xywh(x, y, 4, 4), ((x + y) / 4) & 1 ? gdi::rgb(200, 200, 200) : gdi::rgb(150, 150, 150));
}

int main(int argc, char** argv)
{
    const char* resf = argc > 1 ? argv[1] : "comdlg32.res";
    const char* out = argc > 2 ? argv[2] : "imagetest.ppm";
    res::Module* mod = res::load(resf);
    if (!mod) { printf("cannot load %s\n", resf); return 1; }
    HINSTANCE hinst = (HINSTANCE)mod;

    gdi::Bitmap* target = gdi::create_bitmap(480, 360);
    DC* dc = new DC();
    dc->type = T_MEMDC;
    dc->dc = gdi::create_dc(target);
    HDC hdc = (HDC)dc;
    gdi::fill_rect(dc->dc, target->rect(), gdi::rgb(GetRValue(GetSysColor(COLOR_3DFACE)),
                   GetGValue(GetSysColor(COLOR_3DFACE)), GetBValue(GetSysColor(COLOR_3DFACE))));
    checker(dc->dc, gdi::rect(0, 0, 480, 100));

    // Row 1: comdlg32 group icons, native and 16x16.
    static const char16_t* names[] = { u"FOLDER", u"FOLDER2", u"FLOPPY", u"HDISK", u"CDROM", u"NETWORK",
                                       u"PD32_COLLATE", u"PD32_NOCOLLATE", u"PD32_PORTRAIT", u"PD32_LANDSCAPE" };
    int x = 4;
    for (const char16_t* n : names) {
        HICON ic = (HICON)LoadImageW(hinst, (LPCWSTR)n, IMAGE_ICON, 0, 0, 0);
        HICON sm = (HICON)LoadImageW(hinst, (LPCWSTR)n, IMAGE_ICON, 16, 16, 0);
        CHECK(ic && sm);
        SIZE s = {0, 0};
        if (ic && get_icon_size(ic, &s)) {
            printf("%-16s native %ldx%ld\n", res::to_utf8(std::u16string(n)).c_str(), (long)s.cx, (long)s.cy);
            DrawIconEx(hdc, x, 4, ic, 0, 0, 0, nullptr, DI_NORMAL);
        }
        if (sm) {
            CHECK(get_icon_size(sm, &s) && s.cx == 16 && s.cy == 16);
            DrawIconEx(hdc, x, 4 + 50, sm, 0, 0, 0, nullptr, DI_NORMAL);
        }
        DestroyIcon(ic);
        DestroyIcon(sm);
        x += s.cx > 32 ? 52 : 40;
    }

    // Row 2: standard icons (HINSTANCE NULL) at 32 and 16, plus cursors.
    // IDI_* (winuser.h's macros are not usable as LPCWSTR in C++ without UNICODE)
    static const int std_icons[] = { OIC_SAMPLE, OIC_HAND, OIC_QUES, OIC_BANG, OIC_NOTE, OIC_WINLOGO, OIC_SHIELD };
    x = 4;
    checker(dc->dc, gdi::rect(0, 100, 480, 150));
    for (int id : std_icons) {
        LPCWSTR n = MAKEINTRESOURCEW(id);
        HICON ic = LoadIconW(nullptr, n);
        CHECK(ic != nullptr);
        CHECK(LoadIconW(nullptr, n) == ic);          // shared
        DrawIcon(hdc, x, 104, ic);
        HICON sm = (HICON)LoadImageW(nullptr, n, IMAGE_ICON, 16, 16, LR_SHARED);
        DrawIconEx(hdc, x + 34, 104, sm, 0, 0, 0, nullptr, DI_NORMAL);
        CHECK(DestroyIcon(ic));                       // shared: no-op
        x += 52;
    }
    static const int std_cursors[] = { OCR_NORMAL, OCR_IBEAM, OCR_WAIT, OCR_CROSS, OCR_SIZEWE, OCR_SIZENS, OCR_SIZEALL,
                                        OCR_HAND, OCR_NO, OCR_UP, OCR_APPSTARTING, OCR_HELP };
    x = 4;
    HCURSOR prev = nullptr;
    for (int id : std_cursors) {
        LPCWSTR n = MAKEINTRESOURCEW(id);
        HCURSOR c = LoadCursorW(nullptr, n);
        CHECK(c != nullptr && c != prev);
        prev = c;
        ICONINFO ii;
        if (c && GetIconInfo((HICON)c, &ii)) {
            CHECK(!ii.fIcon);
            printf("cursor %5u hotspot %lu,%lu\n", (unsigned)(uintptr_t)n, ii.xHotspot, ii.yHotspot);
            // Round-trip through CreateIconIndirect and draw the copy.
            HICON copy = CreateIconIndirect(&ii);
            CHECK(copy != nullptr);
            DrawIconEx(hdc, x, 150, copy, 0, 0, 0, nullptr, DI_NORMAL);
            DestroyIcon(copy);
        }
        x += 36;
    }

    // Row 3: bitmap 38 raw, scaled, image list from it, OEM bitmaps.
    HBITMAP b38 = (HBITMAP)LoadImageW(hinst, MAKEINTRESOURCEW(38), IMAGE_BITMAP, 0, 0, 0);
    CHECK(b38 != nullptr);
    if (b38) {
        Bitmap* b = (Bitmap*)b38;
        printf("bitmap 38: %dx%d, %d bpp\n", b->bmp->width, b->bmp->height, b->req_bpp);
        gdi::blit(dc->dc, gdi::Point{4, 190}, b->bmp, b->bmp->rect());
        HBITMAP big = (HBITMAP)CopyImage(b38, IMAGE_BITMAP, 200, 24, 0);
        gdi::blit(dc->dc, gdi::Point{110, 190}, ((Bitmap*)big)->bmp, ((Bitmap*)big)->bmp->rect());
    }
    // LR_LOADTRANSPARENT | LR_LOADMAP3DCOLORS: the background becomes COLOR_3DFACE.
    HBITMAP bt = (HBITMAP)LoadImageW(hinst, MAKEINTRESOURCEW(38), IMAGE_BITMAP, 0, 0, LR_LOADTRANSPARENT | LR_LOADMAP3DCOLORS);
    if (bt) {
        gdi::Bitmap* b = ((Bitmap*)bt)->bmp;
        CHECK((b->row(b->height - 1)[0] & 0xffffff) == (pack(color_of(GetSysColor(COLOR_3DFACE))) & 0xffffff));
        gdi::blit(dc->dc, gdi::Point{4, 204}, b, b->rect());
    }
    // Files: an .ico with a PNG 256 entry (must pick a DIB one), a .cur, a .bmp.
    // Wine's source tree ($WINE_SRC; without it the three files are not found)
    const char* ws = getenv("WINE_SRC");
    std::string wres = std::string(ws ? ws : "/nonexistent") + "/dlls/user32/resources/";
    std::string f1 = wres + "oic_ques.ico", f2 = wres + "ocr_hand.cur",
                f3 = wres + "obm_close.bmp";
    HICON fi = (HICON)LoadImageA(nullptr, f1.c_str(), IMAGE_ICON, 48, 48, LR_LOADFROMFILE);
    HCURSOR fc = (HCURSOR)LoadImageA(nullptr, f2.c_str(), IMAGE_CURSOR, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    HBITMAP fb = (HBITMAP)LoadImageA(nullptr, f3.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
    printf("from files: ico %p cur %p bmp %p\n", (void*)fi, (void*)fc, (void*)fb);
    if (fi) DrawIconEx(hdc, 330, 168, fi, 0, 0, 0, nullptr, DI_NORMAL);
    if (fc) { ICONINFO ii; GetIconInfo((HICON)fc, &ii); CHECK(ii.xHotspot == 7 && ii.yHotspot == 0);
              DrawIconEx(hdc, 380, 168, fc, 0, 0, 0, nullptr, DI_NORMAL); }
    if (fb) gdi::blit(dc->dc, gdi::Point{420, 190}, ((Bitmap*)fb)->bmp, ((Bitmap*)fb)->bmp->rect());

    HIMAGELIST il = ImageList_LoadImageW(hinst, MAKEINTRESOURCEW(38), 20, 0, CLR_DEFAULT, IMAGE_BITMAP, 0);
    CHECK(il != nullptr);
    CHECK(ImageList_GetImageCount(il) == 5);
    int cx = 0, cy = 0;
    CHECK(ImageList_GetIconSize(il, &cx, &cy) && cx == 20 && cy == 12);
    checker(dc->dc, gdi::rect(0, 220, 480, 260));
    for (int i = 0; i < ImageList_GetImageCount(il); i++) {
        ImageList_Draw(il, i, hdc, 4 + i * 24, 224, ILD_NORMAL);
        ImageList_Draw(il, i, hdc, 140 + i * 24, 224, ILD_SELECTED);
        ImageList_DrawEx(il, i, hdc, 276 + i * 24, 224, 0, 0, RGB(255, 255, 0), CLR_DEFAULT, ILD_NORMAL);
    }
    // An icon put into a list and taken back out.
    HIMAGELIST il2 = ImageList_Create(32, 32, ILC_COLOR32 | ILC_MASK, 4, 4);
    HICON folder = (HICON)LoadImageW(hinst, (LPCWSTR)u"FOLDER", IMAGE_ICON, 32, 32, 0);
    CHECK(ImageList_AddIcon(il2, folder) == 0);
    CHECK(ImageList_AddIcon(il2, LoadIconW(nullptr, MAKEINTRESOURCEW(OIC_BANG))) == 1);
    HICON back = ImageList_GetIcon(il2, 1, ILD_NORMAL);
    DrawIconEx(hdc, 404, 224, back, 16, 16, 0, nullptr, DI_NORMAL);
    IMAGEINFO info;
    CHECK(ImageList_GetImageInfo(il2, 1, &info) && info.rcImage.left == 32);
    IMAGELISTDRAWPARAMS p;
    memset(&p, 0, sizeof p);
    p.cbSize = sizeof p;
    p.i = 0; p.hdcDst = hdc; p.x = 424; p.y = 224; p.rgbBk = CLR_NONE; p.rgbFg = CLR_NONE;
    p.fStyle = ILD_TRANSPARENT; p.fState = ILS_ALPHA; p.Frame = 128;
    // DI_MASK alone draws the mask; DI_IMAGE alone still alpha-blends (as Wine does for alpha icons).
    DrawIconEx(hdc, 404, 262, folder, 0, 0, 0, nullptr, DI_MASK);
    DrawIconEx(hdc, 440, 262, folder, 0, 0, 0, nullptr, DI_IMAGE);
    CHECK(ImageList_Remove(il2, 0) && ImageList_GetImageCount(il2) == 1);
    ImageList_Destroy(il2);
    ImageList_Destroy(il);

    static const int obm[] = { OBM_CHECKBOXES, OBM_CHECK, OBM_CLOSE, OBM_COMBO, OBM_UPARROW, OBM_DNARROW,
                               OBM_LFARROW, OBM_RGARROW, OBM_UPARROWD, OBM_REDUCE, OBM_ZOOM, OBM_RESTORE,
                               OBM_MNARROW, OBM_SIZE, OBM_BTSIZE };
    x = 4;
    for (int id : obm) {
        HBITMAP h = LoadBitmapW(nullptr, MAKEINTRESOURCEW(id));
        CHECK(h != nullptr);
        if (!h) continue;
        Bitmap* b = (Bitmap*)h;
        gdi::blit(dc->dc, gdi::Point{x, 262}, b->bmp, b->bmp->rect());
        x += b->bmp->width + 4;
    }

    // System colours as a swatch strip, plus the 55AA brush.
    for (int i = 0; i <= COLOR_MENUBAR; i++) {
        COLORREF c = GetSysColor(i);
        gdi::fill_rect(dc->dc, gdi::rect_xywh(4 + i * 14, 320, 13, 20), color_of(c));
        Brush* br = (Brush*)GetSysColorBrush(i);
        CHECK(br && br->stock && br->lb.lbColor == c);
        Pen* pen = (Pen*)SYSCOLOR_GetPen(i);
        CHECK(pen && pen->lp.lopnColor == c);
    }
    Brush* pat = (Brush*)SYSCOLOR_Get55AABrush();
    CHECK(pat && pat->pattern && (HBRUSH)pat == COMCTL32_hPattern55AABrush);
    for (int i = 0; i < 4; i++) gdi::blit(dc->dc, gdi::Point{440 + (i & 1) * 8, 320 + (i >> 1) * 8}, pat->pattern, pat->pattern->rect());
    CHECK(GetSysColor(COLOR_3DFACE) == RGB(212, 208, 200));
    CHECK(GetSysColor(COLOR_HIGHLIGHT) == RGB(10, 36, 106));
    INT idx = COLOR_INFOBK;
    COLORREF nc = RGB(1, 2, 3);
    SetSysColors(1, &idx, &nc);
    CHECK(((Brush*)GetSysColorBrush(COLOR_INFOBK))->lb.lbColor == nc);
    nc = RGB(255, 255, 225);
    SetSysColors(1, &idx, &nc);

    gdi::write_ppm(target, out);
    printf("%s: %d failures\n", out, failures);
    return failures != 0;
}
