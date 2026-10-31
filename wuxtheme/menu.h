/* The popup menu painter: one implementation of a themed popup menu, at the GDI level, for user32's menus in wux (menu.c.patch)
 * and, through ut_menu_* (ut.h), for luxwm's window menu. XP's own drawing (ntuser mncomput.c / mndraw.c) with the visual style's
 * colours and font, and the Longhorn / Vista Menu class (POPUPBORDERS, POPUPITEM, SEPARATOR, CHECK, FLYOUTARROW, ...) where the
 * style has one, as Longhorn 4074's user32 drew it (lh4074-195235.png). docs/MENUS.md. */
#ifndef WUXTHEME_MENU_H
#define WUXTHEME_MENU_H
#include "windef.h"
#include "wingdi.h"
#include "uxtheme.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UXMENU_LOOK {
    HTHEME theme;                       /* the style's Menu class when it has POPUPITEM (the Longhorn / Vista look), else NULL */
    int p_borders, p_bg, p_item, p_sep, p_gutter, p_check, p_arrow, p_sys[5];   /* part ids, 0 = the style has none */
    int flat;                           /* FLATMENUS */
    COLORREF menu, text, gray, hltext, hl, hlframe, shadow, hilight, face, dkshadow, light;
    COLORREF t_norm, t_hot, t_dis, t_dishot;   /* text: normal, selected, greyed, greyed and selected */
    HFONT font, bold;                   /* the menu font and its bold (the default item); owned by the look */
    int asc, th;                        /* tmAscent, tmHeight of the menu font */
    int pad;                            /* outline + space to the items on every side (MENU_MARGIN) */
    int item_h, sep_h, sep_top, sep_ch; /* a text item's height; a separator's, where its line starts, the line's height */
    int text_x;                         /* the text's start in an item */
    int glyph_x, glyph_w;               /* the check column in an item */
    int gutter_w;                       /* Vista's gutter (0: none) */
    int gap;                            /* between the text and the accelerator */
    int right_pad;                      /* after the accelerator: the submenu arrow's column */
} UXMENU_LOOK;

/* the item flags and the system menu glyphs (XP's HBMMENU_POPUP_*) */
enum { UXMENU_DISABLED = 1, UXMENU_SEPARATOR = 2, UXMENU_DEFAULT = 4, UXMENU_CHECKED = 8, UXMENU_RADIO = 16, UXMENU_SUBMENU = 32,
       UXMENU_HOT = 64, UXMENU_CUES = 128 /* underline the mnemonic */, UXMENU_TEXTONLY = 256 /* the text over a drawn item */ };
enum { UXMENU_GLYPH_NONE, UXMENU_GLYPH_RESTORE, UXMENU_GLYPH_MINIMIZE, UXMENU_GLYPH_MAXIMIZE, UXMENU_GLYPH_CLOSE };

/* sys: the system colours in COLOR_ order (UT_SYSCOLORS of them; NULL: GetSysColor); flat: FLATMENUS; theme: the style's
 * Menu class (NULL: XP's drawing in those colours); font: the menu font (copied; NULL: Tahoma 8) */
void UXMENU_InitLook(UXMENU_LOOK *L, HTHEME theme, const COLORREF *sys, BOOL flat, const LOGFONTW *font);
void UXMENU_FreeLook(UXMENU_LOOK *L);
/* a text item's size without the frame: text (and accel) width in the menu font (bold for the default item) */
void UXMENU_MeasureItem(const UXMENU_LOOK *L, HDC hdc, const WCHAR *text, int text_len, const WCHAR *accel, unsigned flags,
                        int *text_w, int *accel_w);
/* the plate of a w x h popup: border, background, gutter */
void UXMENU_DrawFrame(const UXMENU_LOOK *L, HDC hdc, const RECT *menu);
/* one item in rect (the item's full width inside the frame): its background, highlight, separator, check mark or radio bullet,
 * system glyph, text (text_len chars of text, '&' marks the mnemonic), accelerator right-aligned (accel_x >= 0: at that x
 * instead) and the submenu arrow. text NULL: everything but the text (an owner's bitmap item). */
void UXMENU_DrawItem(const UXMENU_LOOK *L, HDC hdc, const RECT *rect, unsigned flags, int glyph, const WCHAR *text, int text_len,
                     const WCHAR *accel, int accel_x);
/* only the submenu arrow of an item (an owner-drawn one: Windows draws the arrow after the owner) */
void UXMENU_DrawArrow(const UXMENU_LOOK *L, HDC hdc, const RECT *rect, unsigned flags);

#ifdef __cplusplus
}
#endif
#endif
