// Load a toolbar strip from a file, build a masked image list, draw it.
#include <stdio.h>
#include "res/res.h"
#include "w32/w32.h"
#include "w32/user.h"
#include "gdi/bmp.h"
extern "C" {
#include "commctrl.h"
}
int main(int argc, char** argv)
{
    w32::init();
    const char* file = argv[1]; int size = atoi(argv[2]); const char* out = argv[3];
    gdi::Bitmap* target = gdi::create_bitmap(1000, 120);
    for (int y = 0; y < 120; y++) for (int x = 0; x < 1000; x++) target->row(y)[x] = 0xffd4d0c8;
    w32::DC dcobj = {}; dcobj.type = w32::T_MEMDC; dcobj.dc = gdi::create_dc(target);
    HDC hdc = (HDC)&dcobj;
    for (int variant = 0; variant < 2; variant++) {
        UINT fl = LR_LOADFROMFILE | (variant ? LR_CREATEDIBSECTION : 0);
        HBITMAP b = (HBITMAP)LoadImageW(nullptr, (LPCWSTR)w32::utf16_of(file).c_str(), IMAGE_BITMAP, 0, 0, fl);
        BITMAP bm = {}; GetObjectW(b, sizeof bm, &bm);
        HDC mdc = CreateCompatibleDC(nullptr); HGDIOBJ old = SelectObject(mdc, b);
        COLORREF c00 = GetPixel(mdc, 0, 0); SelectObject(mdc, old); DeleteDC(mdc);
        HIMAGELIST il = ImageList_Create(size, size, ILC_COLOR24 | ILC_MASK, 48, 8);
        int n = ImageList_AddMasked(il, b, CLR_DEFAULT);
        printf("variant %d: bpp=%d %ldx%ld pixel00=%06x added=%d count=%d\n", variant, bm.bmBitsPixel, (long)bm.bmWidth, (long)bm.bmHeight, (unsigned)c00, n, ImageList_GetImageCount(il));
        for (int i = 0; i < 10; i++) ImageList_Draw(il, i, hdc, 10 + i * (size + 8), 10 + variant * 50, ILD_TRANSPARENT);
    }
    gdi::write_ppm(target, out);
    return 0;
}
