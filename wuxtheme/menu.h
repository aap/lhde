/* The popup menu painter: one implementation of a themed popup menu, at the GDI level, for user32's menus in wux (menu.c.patch)
 * and, through ut_menu_* (ut.h), for luxwm's window menu. XP's own drawing (ntuser mncomput.c / mndraw.c) with the visual style's
 * colours and font, and the Longhorn / Vista Menu class (POPUPBORDERS, POPUPITEM, SEPARATOR, CHECK, FLYOUTARROW, ...) where the
 * style has one, as Longhorn 4074's user32 drew it (lh4074-195235.png). docs/LUXWM.md "The window menu". */
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
    int band;                           /* the shell's menu band (UXMENU_SetBand): no margin, items from edge to edge */
    HTHEME band_theme;                  /* its plate: the style's StartMenu::Toolbar (part 0) */
    COLORREF band_arrow;                /* its submenu arrow: StartMenu::MenuBand's TEXTCOLOR */
    int band_gutter, band_icon_x, band_text_x;   /* the band's icon column (the plate's left sizing margin), icon, text */
} UXMENU_LOOK;

/* a private MENUINFO style bit (MIM_STYLE): the popup is a shell menu band (the start menu's Programs cascades), not a user32
 * menu; menu.c.patch lays it out and draws its plate as UXMENU_SetBand says */
#define UXMENU_MNS_BAND 0x00008000

/* the item flags and the system menu glyphs (XP's HBMMENU_POPUP_*) */
enum { UXMENU_DISABLED = 1, UXMENU_SEPARATOR = 2, UXMENU_DEFAULT = 4, UXMENU_CHECKED = 8, UXMENU_RADIO = 16, UXMENU_SUBMENU = 32,
       UXMENU_HOT = 64, UXMENU_CUES = 128 /* underline the mnemonic */, UXMENU_TEXTONLY = 256 /* the text over a drawn item */ };
enum { UXMENU_GLYPH_NONE, UXMENU_GLYPH_RESTORE, UXMENU_GLYPH_MINIMIZE, UXMENU_GLYPH_MAXIMIZE, UXMENU_GLYPH_CLOSE };

/* sys: the system colours in COLOR_ order (UT_SYSCOLORS of them; NULL: GetSysColor); flat: FLATMENUS; theme: the style's
 * Menu class (NULL: XP's drawing in those colours); font: the menu font (copied; NULL: Tahoma 8) */
void UXMENU_InitLook(UXMENU_LOOK *L, HTHEME theme, const COLORREF *sys, BOOL flat, const LOGFONTW *font);
void UXMENU_FreeLook(UXMENU_LOOK *L);
/* the look of the shell's menu band, as 4074's start menu cascades are (lh4074-215101.png): for a style with the Longhorn Menu
 * class, toolbar = its StartMenu::Toolbar (kept by the look when used) and menuband = StartMenu::MenuBand: the toolbar's
 * background (outline, icon gutter, background) over the whole menu, items edge to edge without a margin (the hot item, the
 * Menu class's POPUPITEM, over the outline), the icon 10 px and the text gutter + 3 px in, the submenu arrow XP's 4x7 in the
 * menu band's text colour; other looks (XP's styles, Windows Classic) stay user32's popup, as XP's start menu cascades are
 * (band stays 0) */
void UXMENU_SetBand(UXMENU_LOOK *L, HTHEME toolbar, HTHEME menuband);
/* user32's look (menu.c.patch): the window's Menu class, FLATMENUS and the menu font, a band as UXMENU_SetBand; released with
 * UXMENU_CloseLook. In libwux, for an owner that measures its band items in it */
void UXMENU_OpenLook(HWND hwnd, BOOL band, UXMENU_LOOK *L);
void UXMENU_CloseLook(UXMENU_LOOK *L);
/* the text colour of an item with these flags (an owner-drawn item gets it in its DC) */
COLORREF UXMENU_TextColor(const UXMENU_LOOK *L, unsigned flags);
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
