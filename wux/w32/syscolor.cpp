// System colours: GetSysColor & co, the cached brushes and pens behind them,
// and comctl32's copy of the colour table. Values are the XP "Windows
// Classic" scheme (they match Wine's win32u/sysparams.c defaults except
// COLOR_HOTLIGHT, which XP classic has at 0,0,128).
#include "w32/image.h"

namespace w32 {

static const int NUM_SYS_COLORS = COLOR_MENUBAR + 1;

static const COLORREF default_colors[NUM_SYS_COLORS] = {
    RGB(212, 208, 200),   // COLOR_SCROLLBAR
    RGB( 58, 110, 165),   // COLOR_BACKGROUND
    RGB( 10,  36, 106),   // COLOR_ACTIVECAPTION
    RGB(128, 128, 128),   // COLOR_INACTIVECAPTION
    RGB(212, 208, 200),   // COLOR_MENU
    RGB(255, 255, 255),   // COLOR_WINDOW
    RGB(  0,   0,   0),   // COLOR_WINDOWFRAME
    RGB(  0,   0,   0),   // COLOR_MENUTEXT
    RGB(  0,   0,   0),   // COLOR_WINDOWTEXT
    RGB(255, 255, 255),   // COLOR_CAPTIONTEXT
    RGB(212, 208, 200),   // COLOR_ACTIVEBORDER
    RGB(212, 208, 200),   // COLOR_INACTIVEBORDER
    RGB(128, 128, 128),   // COLOR_APPWORKSPACE
    RGB( 10,  36, 106),   // COLOR_HIGHLIGHT
    RGB(255, 255, 255),   // COLOR_HIGHLIGHTTEXT
    RGB(212, 208, 200),   // COLOR_BTNFACE (3DFACE)
    RGB(128, 128, 128),   // COLOR_BTNSHADOW (3DSHADOW)
    RGB(128, 128, 128),   // COLOR_GRAYTEXT
    RGB(  0,   0,   0),   // COLOR_BTNTEXT
    RGB(212, 208, 200),   // COLOR_INACTIVECAPTIONTEXT
    RGB(255, 255, 255),   // COLOR_BTNHIGHLIGHT (3DHIGHLIGHT)
    RGB( 64,  64,  64),   // COLOR_3DDKSHADOW
    RGB(212, 208, 200),   // COLOR_3DLIGHT
    RGB(  0,   0,   0),   // COLOR_INFOTEXT
    RGB(255, 255, 225),   // COLOR_INFOBK
    RGB(181, 181, 181),   // COLOR_ALTERNATEBTNFACE (25, undocumented)
    RGB(  0,   0, 128),   // COLOR_HOTLIGHT
    RGB(166, 202, 240),   // COLOR_GRADIENTACTIVECAPTION
    RGB(192, 192, 192),   // COLOR_GRADIENTINACTIVECAPTION
    RGB( 10,  36, 106),   // COLOR_MENUHILIGHT
    RGB(212, 208, 200),   // COLOR_MENUBAR
};

static COLORREF colors[NUM_SYS_COLORS];
static Brush* brushes[NUM_SYS_COLORS];
static Pen* pens[NUM_SYS_COLORS];
static Brush* brush_55aa;
static bool inited;

static void fill_55aa()
{
    gdi::Bitmap* b = brush_55aa->pattern;
    uint32_t face = pack(color_of(colors[COLOR_3DFACE]));
    uint32_t hl = pack(color_of(colors[COLOR_3DHIGHLIGHT]));
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            b->row(y)[x] = ((x + y) & 1) ? hl : face;
}

static void refresh_objects()
{
    for (int i = 0; i < NUM_SYS_COLORS; i++) {
        brushes[i]->lb.lbColor = colors[i];
        pens[i]->lp.lopnColor = colors[i];
    }
    fill_55aa();
}

void init_syscolor()
{
    if (inited) return;
    inited = true;
    for (int i = 0; i < NUM_SYS_COLORS; i++) {
        colors[i] = default_colors[i];
        Brush* b = new Brush();
        b->type = T_BRUSH;
        b->stock = true;
        b->lb.lbStyle = BS_SOLID;
        b->lb.lbHatch = 0;
        b->pattern = nullptr;
        brushes[i] = b;
        Pen* p = new Pen();
        p->type = T_PEN;
        p->stock = true;
        p->lp.lopnStyle = PS_SOLID;
        p->lp.lopnWidth.x = 1;
        p->lp.lopnWidth.y = 0;
        pens[i] = p;
    }
    brush_55aa = new Brush();
    brush_55aa->type = T_BRUSH;
    brush_55aa->stock = true;
    brush_55aa->lb.lbStyle = BS_PATTERN;
    brush_55aa->lb.lbColor = 0;
    brush_55aa->lb.lbHatch = 0;
    brush_55aa->pattern = gdi::create_bitmap(8, 8);
    refresh_objects();
}

void reset_syscolors()
{
    init_syscolor();
    for (int i = 0; i < NUM_SYS_COLORS; i++) colors[i] = default_colors[i];
    refresh_objects();
}

// Static initialiser so the table is ready before main(); everything also
// calls init_syscolor() lazily in case it runs from another initialiser.
static struct SysColorInit { SysColorInit() { init_syscolor(); } } syscolor_init;

} // namespace w32

using namespace w32;

// Layout of COMCTL32_SysColor in vendor/wine/comctl32/comctl32.h (that header
// is C-only, so the struct is mirrored here; the symbol is all that links).
struct ComctlSysColor {
    COLORREF clrBtnHighlight, clrBtnShadow, clrBtnText, clrBtnFace, clrHighlight,
             clrHighlightText, clrHotTrackingColor, clr3dHilight, clr3dShadow,
             clr3dDkShadow, clr3dFace, clrWindow, clrWindowText, clrGrayText,
             clrActiveCaption, clrInfoBk, clrInfoText;
};

extern "C" {



DWORD WINAPI GetSysColor(INT index)
{
    init_syscolor();
    if (index < 0 || index >= NUM_SYS_COLORS) return 0;
    return colors[index];
}

HBRUSH WINAPI GetSysColorBrush(INT index)
{
    init_syscolor();
    if (index < 0 || index >= NUM_SYS_COLORS) return nullptr;
    return (HBRUSH)brushes[index];
}

extern "C" void COMCTL32_RefreshSysColors(void) __attribute__((weak));

BOOL WINAPI SetSysColors(INT count, const INT* indices, const COLORREF* values)
{
    init_syscolor();
    if (count < 0 || (count && (!indices || !values))) return FALSE;
    for (int i = 0; i < count; i++)
        if (indices[i] >= 0 && indices[i] < NUM_SYS_COLORS)
            colors[indices[i]] = values[i] & 0x00ffffff;
    // Brushes and pens are updated in place, so handles already handed out
    // follow the new colours (Windows behaves the same for GetSysColorBrush).
    refresh_objects();
    // FIXME: broadcast WM_SYSCOLORCHANGE once the user shim can. Until then
    // refresh comctl32's colour cache directly (its controls do that on
    // WM_SYSCOLORCHANGE); the theme sets colours after comctl32 initialised.
    if (COMCTL32_RefreshSysColors) COMCTL32_RefreshSysColors();
    return TRUE;
}

HPEN SYSCOLOR_GetPen(INT index)
{
    init_syscolor();
    if (index < 0 || index >= NUM_SYS_COLORS) return nullptr;
    return (HPEN)pens[index];
}

HBRUSH SYSCOLOR_Get55AABrush(void)
{
    init_syscolor();
    return (HBRUSH)brush_55aa;
}

} // extern "C"

