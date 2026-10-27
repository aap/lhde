// Inspect how a theme bitmap comes out of LoadImageW + GetObjectW, as uxtheme's prepare_alpha sees it.
#include <stdio.h>
#include "w32/w32.h"
#include "w32/user.h"
#include "w32/kernel.h"
int main(int argc, char** argv)
{
    w32::init();
    HINSTANCE h = LoadLibraryExW((LPCWSTR)w32::utf16_of(argv[1]).c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
    for (int i = 2; i < argc; i++) {
        HBITMAP b = LoadImageW(h, (LPCWSTR)w32::utf16_of(argv[i]).c_str(), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION) ? (HBITMAP)LoadImageW(h, (LPCWSTR)w32::utf16_of(argv[i]).c_str(), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION) : nullptr;
        DIBSECTION ds; int n = GetObjectW(b, sizeof ds, &ds);
        printf("%s: hbm=%p GetObject=%d (want %zu) bpp=%d comp=%u w=%d h=%d bits=%p\n", argv[i], (void*)b, n, sizeof ds, ds.dsBm.bmBitsPixel, (unsigned)ds.dsBmih.biCompression, (int)ds.dsBmih.biWidth, (int)ds.dsBmih.biHeight, ds.dsBm.bmBits);
        if (!ds.dsBm.bmBits) continue;
        int magenta = 0, nonff = 0, total = ds.dsBmih.biWidth * (ds.dsBmih.biHeight < 0 ? -ds.dsBmih.biHeight : ds.dsBmih.biHeight);
        BYTE* p = (BYTE*)ds.dsBm.bmBits;
        for (int k = 0; k < total; k++, p += 4) { if (p[3] != 0xff) nonff++; if (p[0] == 255 && p[1] == 0 && p[2] == 255) magenta++; }
        printf("   pixels=%d alpha!=ff: %d  magenta: %d\n", total, nonff, magenta);
    }
    return 0;
}
