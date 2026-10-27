/* Replacement for Wine's user32 private controls.h: just what the vendored
 * dialog.c / defdlg.c / scroll.c / text.c / uitools.c need. */
#ifndef FLXP_CONTROLS_H
#define FLXP_CONTROLS_H
#include "windef.h"
#include "winuser.h"

/* Dialog info structure */
typedef struct tagDIALOGINFO
{
    HWND      hwndFocus;   /* Current control with focus */
    HFONT     hUserFont;   /* Dialog font */
    HMENU     hMenu;       /* Dialog menu */
    UINT      xBaseUnit;   /* Dialog units (depends on the font) */
    UINT      yBaseUnit;
    INT       idResult;    /* EndDialog() result / default pushbutton ID */
    UINT      flags;       /* EndDialog() called for this dialog */
} DIALOGINFO;

#define DF_END  0x0001
#define DF_OWNERENABLED 0x0002

extern DIALOGINFO *DIALOG_get_info( HWND hwnd, BOOL create );
extern INT DIALOG_DoDialogBox( HWND hwnd, HWND owner );
extern void DIALOG_EnableOwner( HWND hOwner );
extern BOOL DIALOG_DisableOwner( HWND hOwner );
extern LRESULT WINAPI DefDlgProcA(HWND,UINT,WPARAM,LPARAM);
extern LRESULT WINAPI DefDlgProcW(HWND,UINT,WPARAM,LPARAM);
extern LRESULT WINAPI DialogWndProcA(HWND,UINT,WPARAM,LPARAM);
extern LRESULT WINAPI DialogWndProcW(HWND,UINT,WPARAM,LPARAM);
extern LRESULT WINAPI USER_ScrollBarProc(HWND, UINT, WPARAM, LPARAM, BOOL);
extern void WINAPI USER_ScrollBarDraw(HWND, HDC, INT, enum SCROLL_HITTEST,
                                      const struct SCROLL_TRACKING_INFO *, BOOL, BOOL, RECT *, UINT, INT, INT,
                                      INT, BOOL);
extern void SCROLL_DrawScrollBar( HWND hwnd, HDC hdc, INT nBar, enum SCROLL_HITTEST hit_test,
                                  const struct SCROLL_TRACKING_INFO *tracking_info, BOOL arrows,
                                  BOOL interior );
extern void SCROLL_HandleScrollEvent( HWND hwnd, INT nBar, UINT msg, POINT pt );
extern void SCROLL_TrackScrollBar( HWND hwnd, INT scrollbar, POINT pt );
extern LRESULT ScrollBarWndProc_common(HWND,UINT,WPARAM,LPARAM,BOOL);

/* builtin class descriptor, as the vendored files define them */
enum builtin_winprocs
{
    WINPROC_BUTTON = 0, WINPROC_COMBO, WINPROC_DEFWND, WINPROC_DIALOG, WINPROC_EDIT,
    WINPROC_LISTBOX, WINPROC_MDICLIENT, WINPROC_SCROLLBAR, WINPROC_STATIC, WINPROC_IME, WINPROC_MENU,
    NB_BUILTIN_WINPROCS
};
struct builtin_class_descr
{
    LPCWSTR name;    /* class name */
    UINT    style;   /* class style */
    enum builtin_winprocs proc; /* window procedure id */
    INT     extra;   /* window extra bytes */
    ULONG_PTR cursor; /* cursor id */
    HBRUSH  brush;   /* brush or system color */
};
extern const struct builtin_class_descr DIALOG_builtin_class;
extern const struct builtin_class_descr SCROLL_builtin_class;
#define DIALOG_CLASS_ATOM MAKEINTATOM(32770)
#define POPUPMENU_CLASS_ATOM MAKEINTATOM(32768)
extern const struct builtin_class_descr POPUPMENU_builtin_class;

#endif
