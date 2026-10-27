/* Replacement for Wine's user32 private user_private.h, backed by the flxp
 * shim (src/w32/user.cpp). */
#ifndef FLXP_USER_PRIVATE_H
#define FLXP_USER_PRIVATE_H
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "controls.h"

#define USER_DEFAULT_SCREEN_DPI 96

/* unaligned reads of dialog template data (Wine user_private.h) */
static inline WORD get_word( const BYTE *ptr ) { WORD ret; memcpy( &ret, ptr, sizeof(ret) ); return ret; }
static inline DWORD get_dword( const BYTE *ptr ) { DWORD ret; memcpy( &ret, ptr, sizeof(ret) ); return ret; }
#define GET_WORD(ptr)  get_word( (const BYTE *)(ptr) )
#define GET_DWORD(ptr) get_dword( (const BYTE *)(ptr) )
#define GET_LONG(ptr)  ((LONG)GET_DWORD(ptr))

static inline BOOL is_desktop_window( HWND hwnd ) { return hwnd == GetDesktopWindow(); }
extern void WINPOS_ActivateOtherWindow( HWND hwnd );
extern const char *SPY_GetMsgName( UINT msg, HWND hwnd );
extern INT_PTR WINPROC_CallDlgProcW( DLGPROC func, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam );
extern INT_PTR WINPROC_CallDlgProcA( DLGPROC func, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam );
#define WM_SYSTIMER 0x0118

/* The slice of Wine's WND structure that the vendored files touch. */
typedef struct tagWND
{
    DWORD       dwStyle;
    DWORD       dwExStyle;
    struct tagDIALOGINFO *dlgInfo;
    void       *pScroll;      /* scroll.c's SCROLL_INFO* pair, owned by scroll.c */
    INT         cbWndExtra;
    DWORD_PTR  *wExtra;       /* points at the window's extra bytes */
    DWORD       flags;
    UINT_PTR    wIDmenu;      /* ID or hmenu (from CreateWindow) */
    HMENU       hSysMenu;     /* window's copy of System Menu */
} WND;

/* user handles, as menu.c expects them */
enum user_obj_type
{
    USER_WINDOW = 1,
    USER_MENU,
    USER_ACCEL,
    USER_ICON,
    USER_DWP
};
struct user_object
{
    HANDLE             handle;
    enum user_obj_type type;
};
#define OBJ_OTHER_PROCESS ((void *)1)
extern HANDLE alloc_user_handle( struct user_object *ptr, enum user_obj_type type );
extern void *get_user_handle_ptr( HANDLE handle, enum user_obj_type type );
extern void release_user_handle_ptr( void *ptr );
extern void *free_user_handle( HANDLE handle, enum user_obj_type type );
extern BOOL set_capture_window( HWND hwnd, UINT gui_flags, HWND *prev_ret );
extern HMODULE user32_module;

/* menu.c entry points used by the nonclient code */
extern UINT MENU_DrawMenuBar( HDC hDC, LPRECT lprect, HWND hwnd );
extern HWND MENU_IsMenuActive(void);
extern void MENU_EndMenu( HWND hwnd );
extern void MENU_TrackMouseMenuBar( HWND hWnd, INT ht, POINT pt );
extern void MENU_TrackKbdMenuBar( HWND hwnd, UINT wParam, WCHAR wChar );
extern UINT MENU_GetMenuBarHeight( HWND hwnd, UINT menubarWidth, INT orgX, INT orgY );
extern BOOL MENU_SetMenu( HWND hWnd, HMENU hMenu );
extern LRESULT WINAPI PopupMenuWndProc( HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam );
extern void NC_GetSysPopupPos( HWND hwnd, RECT* rect );
extern void NC_DrawSysButton( HWND hwnd, HDC hdc, BOOL down );
extern LRESULT NC_HandleNCHitTest( HWND hwnd, POINT pt );
extern LRESULT NC_HandleSysCommand( HWND hwnd, WPARAM wParam, LPARAM lParam );
#define HAS_MENU(hwnd,style)  ((((style) & (WS_CHILD | WS_POPUP)) != WS_CHILD) && GetMenu(hwnd))

#define WND_OTHER_PROCESS ((WND *)1)
#define WND_DESKTOP       ((WND *)2)
#define WIN_ISDIALOG      0x0004

#ifndef __WINE_WINE_SERVER_PROTOCOL_H   /* menu.c pulls in the server protocol header, which defines this */
enum coords_relative { COORDS_CLIENT, COORDS_WINDOW, COORDS_PARENT, COORDS_SCREEN };
#endif

extern WND *WIN_GetPtr( HWND hwnd );
extern void WIN_ReleasePtr( WND *ptr );
extern HWND WIN_GetFullHandle( HWND hwnd );
extern BOOL WIN_GetRectangles( HWND hwnd, enum coords_relative relative, RECT *rectWindow, RECT *rectClient );
extern HWND *WIN_ListChildren( HWND hwnd );
extern ULONG WIN_SetStyle( HWND hwnd, ULONG set_bits, ULONG clear_bits );
extern BOOL WIN_IsWindowDrawable( HWND hwnd, BOOL icon );
extern HBRUSH DEFWND_ControlColor( HDC hDC, UINT ctlType );
extern HPEN SYSCOLOR_GetPen( INT index );
extern BOOL get_icon_size( HICON handle, SIZE *size );
extern HBRUSH SYSCOLOR_Get55AABrush(void);
/* uxtheme defines its own global named user_api; ours lives under another name */
extern struct user_api_hook *flxp_user_api;
#define user_api flxp_user_api

#endif
