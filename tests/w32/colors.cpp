#include <stdio.h>
#include "w32/w32.h"
#include "w32/user.h"
int main() {
    w32::init();
    for (int i = 0; i <= 30; i++) {
        COLORREF c = GetSysColor(i); LOGBRUSH lb = {}; GetObjectW(GetSysColorBrush(i), sizeof lb, &lb);
        printf("%2d color=%06x brush=%06x%s\n", i, (unsigned)c, (unsigned)lb.lbColor, c != lb.lbColor ? "  MISMATCH" : "");
    }
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateCompatibleBitmap(dc, 4, 4); // mono!
    SelectObject(dc, bmp);
    printf("compatible bitmap bpp: "); BITMAP bm; GetObjectW(bmp, sizeof bm, &bm); printf("%d\n", bm.bmBitsPixel);
    return 0;
}
