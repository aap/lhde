/* flxp extensions to dwmapi for the lhwm window manager's glass frames
   (docs/CLIENT-PROTOCOL.md). Include after windows.h; pulls in dwmapi.h.

   DwmExtendFrameIntoClientArea turns the toplevel into a 32-bit ARGB window
   (recreating its X window if needed: call it in WM_CREATE, before the window
   is shown) and sets _LHWM_EXTEND_FRAME. Paint the extended area transparent
   (e.g. FillRect with GetStockObject(BLACK_BRUSH) does NOT do that: GDI fills
   are opaque here; use FlxpClearRect), draw text with FlxpDrawGlowTextW,
   icons with DrawIconEx/AlphaBlend. While DwmIsCompositionEnabled is FALSE
   the area must be painted opaque. WM_DWMCOMPOSITIONCHANGED is posted to
   every toplevel when that changes, WM_DWMCOLORIZATIONCOLORCHANGED when the
   frame theme or its metrics change. */
#ifndef FLXP_DWMAPI_H
#define FLXP_DWMAPI_H

#include "dwmapi.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef WM_DWMCOMPOSITIONCHANGED
#define WM_DWMCOMPOSITIONCHANGED        0x031E
#endif
#ifndef WM_DWMCOLORIZATIONCOLORCHANGED
#define WM_DWMCOLORIZATIONCOLORCHANGED  0x0320
#endif

/* DwmSetWindowAttribute(hwnd, DWMWA_FLXP_FRAME_FLAGS, &flags, sizeof(DWORD)):
   _LHWM_FRAME_FLAGS, what the window manager leaves out of the caption.
   Honoured while composited; DwmGetWindowAttribute reads it back. */
#define DWMWA_FLXP_FRAME_FLAGS  0x4c480001
#define FLXP_DWMFF_NO_TITLE     0x0001  /* no caption text: the client draws its own title */
#define FLXP_DWMFF_NO_ICON      0x0002  /* no caption icon */
#define FLXP_DWMFF_NO_FOG       0x0004  /* no caption fog band behind the header */
#define FLXP_DWMFF_CLIENT_CAPTION 0x0008 /* experimental: the client covers the caption rows too; lhwm keeps only its
                                           buttons (FlxpDwmGetCaptionRect), the window grows upwards by them */

/* Posted to the toplevel when lhwm replaces its _LHWM_HEADER_PIXMAP (focus,
   size, theme) or its _LHWM_CAPTION_RECT (CLIENT_CAPTION). */
#define WM_FLXP_DWMHEADERCHANGED    0x03F1
#define WM_FLXP_DWMCAPTIONRECTCHANGED 0x03F2
/* Posted to the toplevel when its _NET_WM_STATE changes (maximised, restored). */
#define WM_FLXP_DWMSTATECHANGED     0x03F3

/* Root _LHWM_FRAME_METRICS: the frame's normal (not maximised) margins and
   caption colours. FALSE in bValid when the window manager publishes none. */
typedef struct FLXP_DWM_FRAME_METRICS {
    UINT cbSize;
    BOOL bValid;
    INT cyCaption, cxLeftBorder, cxRightBorder, cyBottomBorder;
    COLORREF crText, crGlow, crInactiveText;    /* crGlow: DWM TextGlow colour, or 0 */
    /* the maximised caption text (lhwm's items 14..16, theme.ini text_max_*, glow_max);
       FALSE in bMaxValid from an lhwm that publishes fewer items */
    BOOL bMaxValid, bMaxGlowNone;
    COLORREF crMaxText, crMaxInactiveText, crMaxGlow;
} FLXP_DWM_FRAME_METRICS;
HRESULT WINAPI FlxpDwmGetFrameMetrics(FLXP_DWM_FRAME_METRICS *metrics);

/* Root _LHWM_THEME: the path of lhwm's active frame theme (a directory with
   theme.ini and its images), UTF-16 into buf. S_FALSE with an empty string
   when the window manager publishes none (no lhwm). */
HRESULT WINAPI FlxpDwmGetFrameThemePath(LPWSTR buf, UINT cch);

/* lhwm's _LHWM_HEADER_PIXMAP, published while not composited for windows
   that extend their frame: the theme's caption band continued down over the
   extension (active or inactive by focus), client width x (top frame margin
   + extension top); its last row lies against the bottom of the extension,
   so copy the last cyTopHeight rows. A new top-down 32bpp DIB section (the
   caller deletes it), NULL when there is none (composited, another WM). */
HBITMAP WINAPI FlxpDwmGetHeaderBitmap(HWND hwnd);

/* CLIENT_CAPTION: lhwm's caption buttons in client coordinates (S_FALSE and
   an empty rect when there are none). Keep them free of content and let
   clicks through with FlxpDwmSetInputHole. */
HRESULT WINAPI FlxpDwmGetCaptionRect(HWND hwnd, RECT *rc);
/* The toplevel's input shape: everything but rc (NULL/empty: everything). */
HRESULT WINAPI FlxpDwmSetInputHole(HWND hwnd, const RECT *rc);

/* Whether the window manager shows hwnd (a toplevel) as the active window:
   the root _NET_ACTIVE_WINDOW, which lhwm sets when it draws the active
   frame. S_OK with *active; S_FALSE when the window manager publishes no
   _NET_ACTIVE_WINDOW (*active is then hwnd == GetActiveWindow()). Watch the
   root property with FlxpWatchRootProperty("_NET_ACTIVE_WINDOW") to follow it. */
HRESULT WINAPI FlxpDwmIsWindowActive(HWND hwnd, BOOL *active);

/* Make a toplevel ARGB (or opaque again) without touching the frame
   extension. E_FAIL if the X server has no 32-bit visual. */
HRESULT WINAPI FlxpDwmEnableAlpha(HWND hwnd, BOOL enable);

/* Set rc (logical coords) of hdc to transparent black (0,0,0,0). Only
   meaningful on an ARGB window's DC; on an opaque one it paints black. */
BOOL WINAPI FlxpClearRect(HDC hdc, const RECT *rc);

/* DrawTextW with Longhorn's TextGlow: first a soft halo of crGlow about
   glow_size px around the glyphs (0 = no halo), then the text in the DC's
   text colour, mode and font. Returns what DrawTextW returns. */
INT WINAPI FlxpDrawGlowTextW(HDC hdc, LPCWSTR text, INT len, RECT *rc, UINT format, INT glow_size, COLORREF crGlow);

#ifdef __cplusplus
}
#endif

#endif
