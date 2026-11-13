// A colour swatch as an icon (shell::swatch_key, shell::load_image: the Details view's Color column): the square of
// 3/4 of the size in the colour with a dark 1 px border, transparent around it, drawn through an image list onto white
// as the list view draws a subitem's image.
//   build/tests/w32/swatchtest
#include <stdio.h>
#include <string.h>
#include "shell/icons.h"
#include "w32/w32.h"
#include "w32/user.h"
extern "C" {
#include "commctrl.h"
}
#include "shell/ui.h"

static int fails, checks;
static void check(const char* what, unsigned want, unsigned got)
{
    checks++;
    if (want == got) return;
    fails++;
    printf("FAIL %s: expected %06x got %06x\n", what, want, got);
}

int main()
{
    w32::init();
    InitCommonControls();
    check("key", 0, shell::swatch_key(0xff0000) == "swatch:ff0000" ? 0 : 1);
    check("source", 0, shell::icon_file("swatch:ff0000") == "swatch:ff0000" ? 0 : 1);
    HICON ic = (HICON)shell::load_image(shell::swatch_key(0x008080), IMAGE_ICON, 16, 16);
    check("icon", 1, ic != nullptr);
    if (!ic) { printf("%d checks, %d failed\n", checks, fails); return 1; }
    HIMAGELIST il = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 4, 4);
    int idx = ImageList_ReplaceIcon(il, -1, ic);
    check("index", 0, (unsigned)idx);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = 16;
    bi.bmiHeader.biHeight = -16;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(dc, bmp);
    RECT all = { 0, 0, 16, 16 };
    FillRect(dc, &all, (HBRUSH)GetStockObject(WHITE_BRUSH));
    ImageList_Draw(il, idx, dc, 0, 0, ILD_NORMAL);
    const unsigned* px = (const unsigned*)bits;
    // 16 px: the square from 2 to 13, its border at 2 and 13
    check("outside: white", 0xffffff, px[0 * 16 + 0] & 0xffffff);
    check("outside, beside the square", 0xffffff, px[8 * 16 + 1] & 0xffffff);
    check("the border", 0x505050, px[2 * 16 + 8] & 0xffffff);
    check("the colour", 0x008080, px[8 * 16 + 8] & 0xffffff);
    check("the colour at its edge", 0x008080, px[3 * 16 + 3] & 0xffffff);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    ImageList_Destroy(il);
    DestroyIcon(ic);
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
