/* wux's additions to the Win32 API (flxp extensions, for the shell programs and every application: wux/wux.h includes
   this): an EWMH desktop and dock window, the window manager's root properties, file drag and drop, the X selections,
   typed clipboard data, PNG files. Include after windows.h.

   CreateWindowEx with WS_EX_FLXP_DESKTOP and WS_POPUP makes a toplevel of
   type _NET_WM_WINDOW_TYPE_DESKTOP: no decorations, sticky, kept below the
   other windows (lowered when shown), not in taskbars, focused when clicked.

   FlxpWatchRootProperty: changes of that root property are posted to every
   toplevel as WM_FLXP_ROOTPROPERTY (wParam = the returned id). _NET_WORKAREA
   is what SystemParametersInfo(SPI_GETWORKAREA) and GetMonitorInfo's rcWork
   report (the screen without the window manager's struts). */
#ifndef FLXP_SHELL_H
#define FLXP_SHELL_H

#ifdef __cplusplus
extern "C" {
#endif

#define WS_EX_FLXP_DESKTOP      0x40000000L
/* WS_EX_FLXP_DOCK with WS_POPUP: a panel (_NET_WM_WINDOW_TYPE_DOCK): managed but
   undecorated, sticky, above, not in taskbars; never focused by clicks. */
#define WS_EX_FLXP_DOCK         0x20000000L
#define WM_FLXP_ROOTPROPERTY    0x03F0
/* LoadImage(IMAGE_ICON) flag: cx/cy choose the directory image (the exact
   size, else the smallest larger, else the largest), the icon keeps that
   image's own size (GetIconInfo tells): what sizes a theme's icon has. */
#define LR_FLXP_NATIVESIZE      0x00100000

/* Drag and drop of files (XDND with every X program; win/window.h). Not OLE:
   FlxpDragFiles is DoDragDrop for a list of local paths (UTF-8): call it on
   LVN_BEGINDRAG/TVN_BEGINDRAG; it returns when the drop is done or cancelled,
   with the DROPEFFECT_* the target performed (0: none). The drag image is the
   images' icons from himl (up to four, stacked; himl NULL: no image).
   Every toplevel is a drop target: the window under the pointer gets
   WM_FLXP_DRAGOVER (return the DROPEFFECT_* a drop there would have, 0 = no),
   WM_FLXP_DROP (do it, return the effect done) and WM_FLXP_DRAGLEAVE
   (unhighlight; also after a drop), lParam = FLXPDROP*. DefWindowProc hands
   them to the parent, so a frame can answer for its controls. */
#define WM_FLXP_DRAGOVER        0x03F4
#define WM_FLXP_DRAGLEAVE       0x03F5
#define WM_FLXP_DROP            0x03F6
#ifndef DROPEFFECT_COPY
#define DROPEFFECT_NONE 0
#define DROPEFFECT_COPY 1
#define DROPEFFECT_MOVE 2
#define DROPEFFECT_LINK 4
#endif
#ifndef MK_ALT
#define MK_ALT 0x20
#endif
typedef struct FLXPDROP {
    HWND hwndTarget;            /* the deepest window under the pointer */
    INT x, y;                   /* the pointer, screen coordinates */
    DWORD dwAllowed;            /* DROPEFFECT_* the source allows */
    DWORD grfKeyState;          /* MK_SHIFT | MK_CONTROL | MK_ALT */
    DWORD dwRequested;          /* the source's request by its keys (plain drag: move) */
    BOOL fLocal;                /* the source is this process */
    UINT cFiles;
    const char *const *ppszFiles;   /* local paths, UTF-8 */
    HWND hwndSource;            /* the source toplevel if it is ours */
} FLXPDROP;
DWORD WINAPI FlxpDragFiles(HWND hwnd, const char *const *paths, UINT count, DWORD allowed,
                           struct _IMAGELIST *himl, const INT *images, UINT nimages);   /* an HIMAGELIST */

UINT WINAPI FlxpWatchRootProperty(const char *name);
/* A CARDINAL[] root property: the number of values (at most max are stored), -1 if absent. */
INT WINAPI FlxpGetRootCardinals(const char *name, long *values, INT max);
/* A STRING/UTF8_STRING root property into buf (NUL-terminated, cut at cch):
   its length in bytes, -1 if absent. */
INT WINAPI FlxpGetRootString(const char *name, char *buf, INT cch);
/* Sets a root property to a UTF8_STRING (NULL or "" deletes it). */
BOOL WINAPI FlxpSetRootString(const char *name, const char *value);

/* Text on the X selections (shell/xclip): CLIPBOARD (primary FALSE: the
   clipboard API's text formats go there on CloseClipboard) or PRIMARY (an edit
   control's selection, the middle button pastes it). Get: a GlobalAlloc'd
   NUL-terminated UTF-16 string (GlobalFree it), NULL if there is none. */
BOOL WINAPI FlxpSetSelectionText(BOOL primary, const WCHAR *text, INT len);
HGLOBAL WINAPI FlxpGetSelectionText(BOOL primary);

/* Typed data on the X CLIPBOARD: SetClipboardData with a format registered under a MIME type's name
   (RegisterClipboardFormatW(L"image/png"); L"PNG", the name Windows programs use, stands for image/png) offers its bytes
   (GlobalAlloc, GlobalSize long) under that target at CloseClipboard, with the text formats if set too. The X clipboard
   lives as long as its owner: FlxpClipboardOwned is TRUE while what this process put there last is still on CLIPBOARD,
   and a program that copied something keeps answering (its message loop or a wait on this) until it turns FALSE. */
BOOL WINAPI FlxpClipboardOwned(void);

/* PNG files of a bitmap (a DIB section or any HBITMAP; 8-bit RGBA, the bitmap's alpha as it is). FlxpWritePng writes
   path (FALSE on any error); FlxpEncodePng returns the file's bytes as a
   GlobalAlloc'd block (GlobalSize long; GlobalFree it, or hand it to SetClipboardData), NULL on error. */
BOOL WINAPI FlxpWritePng(HBITMAP bitmap, const char *path);
HGLOBAL WINAPI FlxpEncodePng(HBITMAP bitmap);

#ifdef __cplusplus
}
#endif

#endif
