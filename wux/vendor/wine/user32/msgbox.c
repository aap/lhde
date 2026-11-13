/*
 * Message boxes
 *
 * Copyright 1995 Bernd Schmidt
 * Copyright 2004 Ivan Leo Puoti, Juan Lang
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>
#include <string.h>

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "winnls.h"
#include "dlgs.h"
#include "user_private.h"
#include "wine/debug.h"

/* lhde: wux has no win32u, so the NtUser calls are the user32 ones; the template and the strings are dlls/user32/user32.rc's
 * MSGBOX and STRINGTABLE, compiled into wux/rc/user32.res (wux/vendor/wine/MANIFEST), which wux/w32/msgbox_glue.cpp loads */
#define NtUserDestroyWindow DestroyWindow
#define NtUserSetWindowPos SetWindowPos
#define NtUserGetDC GetDC
#define NtUserReleaseDC ReleaseDC
#define NtUserSetFocus SetFocus
#define NtUserEnableWindow EnableWindow
#define NtUserSetWindowContextHelpId SetWindowContextHelpId
#define NtUserGetWindowContextHelpId GetWindowContextHelpId
#define NtUserModifyUserStartupInfoFlags(a, b) ((void)0)
#define MSGBOX_IDICON stc1
#define MSGBOX_IDTEXT 0xffff
#define IDS_ERROR 2
extern HMODULE WUX_User32Resources(void);
extern void WUX_MsgBoxOwner(HWND hwnd, HWND owner);
#undef user32_module
#define user32_module WUX_User32Resources()

WINE_DEFAULT_DEBUG_CHANNEL(dialog);
WINE_DECLARE_DEBUG_CHANNEL(msgbox);

struct ThreadWindows
{
    UINT numHandles;
    UINT numAllocs;
    HWND *handles;
};

static BOOL CALLBACK MSGBOX_EnumProc(HWND hwnd, LPARAM lParam)
{
    struct ThreadWindows *threadWindows = (struct ThreadWindows *)lParam;

    if (!NtUserEnableWindow(hwnd, FALSE))
    {
        if(threadWindows->numHandles >= threadWindows->numAllocs)
        {
            threadWindows->handles = HeapReAlloc(GetProcessHeap(), 0, threadWindows->handles,
                                                 (threadWindows->numAllocs*2)*sizeof(HWND));
            threadWindows->numAllocs *= 2;
        }
        threadWindows->handles[threadWindows->numHandles++]=hwnd;
    }
   return TRUE;
}

static void MSGBOX_OnInit(HWND hwnd, LPMSGBOXPARAMSW lpmb)
{
    HFONT hPrevFont;
    HWND hItem;
    HDC hdc;
    int i, buttons;
    int bw, bh, wwidth, wheight, wleft, wtop, bpos;
    int borheight, borwidth, tiheight;
    NONCLIENTMETRICSW nclm;
    HMONITOR monitor;
    MONITORINFO mon_info;
    LPCWSTR lpszText;
    WCHAR *buffer = NULL;
    const WCHAR *ptr;

    /* Index the order the buttons need to appear to an ID* constant */
    static const int buttonOrder[10] = { IDYES, IDNO, IDOK, IDABORT, IDRETRY,
                                         IDCANCEL, IDIGNORE, IDTRYAGAIN,
                                         IDCONTINUE, IDHELP };

    nclm.cbSize = sizeof(nclm);
    SystemParametersInfoW (SPI_GETNONCLIENTMETRICS, 0, &nclm, 0);

    /* lhde: the text and the buttons in the message font (NONCLIENTMETRICS' lfMessageFont, the style's MsgBoxFont: Tahoma 8
     * under XP), as XP sets it; the box keeps it until it is destroyed (MSGBOX_DlgProc) */
    {
        HFONT hFont = CreateFontIndirectW(&nclm.lfMessageFont);
        if (hFont)
        {
            for (i = IDOK; i <= IDCONTINUE; i++) SendDlgItemMessageW(hwnd, i, WM_SETFONT, (WPARAM)hFont, 0);
            SendDlgItemMessageW(hwnd, MSGBOX_IDTEXT, WM_SETFONT, (WPARAM)hFont, 0);
            SetPropA(hwnd, "WUX_MSGBOX_FONT", hFont);
        }
    }

    if (!IS_INTRESOURCE(lpmb->lpszCaption)) {
       SetWindowTextW(hwnd, lpmb->lpszCaption);
    } else {
        UINT len = LoadStringW( lpmb->hInstance, LOWORD(lpmb->lpszCaption), (LPWSTR)&ptr, 0 );
        if (!len) len = LoadStringW( user32_module, IDS_ERROR, (LPWSTR)&ptr, 0 );
        buffer = HeapAlloc( GetProcessHeap(), 0, (len + 1) * sizeof(WCHAR) );
        if (buffer)
        {
            memcpy( buffer, ptr, len * sizeof(WCHAR) );
            buffer[len] = 0;
            SetWindowTextW( hwnd, buffer );
            HeapFree( GetProcessHeap(), 0, buffer );
            buffer = NULL;
        }
    }
    if (IS_INTRESOURCE(lpmb->lpszText)) {
        UINT len = LoadStringW( lpmb->hInstance, LOWORD(lpmb->lpszText), (LPWSTR)&ptr, 0 );
        lpszText = buffer = HeapAlloc( GetProcessHeap(), 0, (len + 1) * sizeof(WCHAR) );
        if (buffer)
        {
            memcpy( buffer, ptr, len * sizeof(WCHAR) );
            buffer[len] = 0;
        }
    } else {
       lpszText = lpmb->lpszText;
    }

    /* handle modal message boxes */
    if (lpmb->dwStyle & MB_TASKMODAL && lpmb->hwndOwner == NULL)
        NtUserSetWindowPos( hwnd, lpmb->dwStyle & MB_TOPMOST ? HWND_TOPMOST : HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE );
    else if (lpmb->dwStyle & MB_SYSTEMMODAL)
        NtUserSetWindowPos( hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED );
    else if (lpmb->dwStyle & MB_TOPMOST)
        NtUserSetWindowPos( hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE );

    TRACE_(msgbox)("%s\n", debugstr_w(lpszText));
    SetWindowTextW(GetDlgItem(hwnd, MSGBOX_IDTEXT), lpszText);

    /* Remove not selected buttons and assign the WS_GROUP style to the first button */
    hItem = 0;
    switch(lpmb->dwStyle & MB_TYPEMASK) {
    case MB_OK:
	NtUserDestroyWindow(GetDlgItem(hwnd, IDCANCEL));
	/* fall through */
    case MB_OKCANCEL:
	hItem = GetDlgItem(hwnd, IDOK);
	NtUserDestroyWindow(GetDlgItem(hwnd, IDABORT));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDRETRY));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDIGNORE));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDYES));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDNO));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDTRYAGAIN));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDCONTINUE));
	break;
    case MB_ABORTRETRYIGNORE:
	hItem = GetDlgItem(hwnd, IDABORT);
	NtUserDestroyWindow(GetDlgItem(hwnd, IDOK));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDCANCEL));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDYES));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDNO));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDCONTINUE));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDTRYAGAIN));
	break;
    case MB_YESNO:
	NtUserDestroyWindow(GetDlgItem(hwnd, IDCANCEL));
	/* fall through */
    case MB_YESNOCANCEL:
	hItem = GetDlgItem(hwnd, IDYES);
	NtUserDestroyWindow(GetDlgItem(hwnd, IDOK));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDABORT));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDRETRY));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDIGNORE));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDCONTINUE));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDTRYAGAIN));
	break;
    case MB_RETRYCANCEL:
	hItem = GetDlgItem(hwnd, IDRETRY);
	NtUserDestroyWindow(GetDlgItem(hwnd, IDOK));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDABORT));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDIGNORE));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDYES));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDNO));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDCONTINUE));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDTRYAGAIN));
	break;
    case MB_CANCELTRYCONTINUE:
	hItem = GetDlgItem(hwnd, IDCANCEL);
	NtUserDestroyWindow(GetDlgItem(hwnd, IDOK));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDABORT));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDIGNORE));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDYES));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDNO));
	NtUserDestroyWindow(GetDlgItem(hwnd, IDRETRY));
    }

    if (hItem) SetWindowLongW(hItem, GWL_STYLE, GetWindowLongW(hItem, GWL_STYLE) | WS_GROUP);

    /* Set the icon */
    switch(lpmb->dwStyle & MB_ICONMASK) {
    case MB_ICONEXCLAMATION:
	SendDlgItemMessageW(hwnd, MSGBOX_IDICON, STM_SETICON,
			    (WPARAM)LoadIconW(0, (LPWSTR)IDI_EXCLAMATION), 0);
	break;
    case MB_ICONQUESTION:
	SendDlgItemMessageW(hwnd, MSGBOX_IDICON, STM_SETICON,
			    (WPARAM)LoadIconW(0, (LPWSTR)IDI_QUESTION), 0);
	break;
    case MB_ICONASTERISK:
	SendDlgItemMessageW(hwnd, MSGBOX_IDICON, STM_SETICON,
			    (WPARAM)LoadIconW(0, (LPWSTR)IDI_ASTERISK), 0);
	break;
    case MB_ICONHAND:
      SendDlgItemMessageW(hwnd, MSGBOX_IDICON, STM_SETICON,
			    (WPARAM)LoadIconW(0, (LPWSTR)IDI_HAND), 0);
      break;
    case MB_USERICON:
      SendDlgItemMessageW(hwnd, MSGBOX_IDICON, STM_SETICON,
			  (WPARAM)LoadIconW(lpmb->hInstance, lpmb->lpszIcon), 0);
      break;
    default:
	/* By default, Windows 95/98/NT do not associate an icon to message boxes.
         * So wine should do the same.
         */
	break;
    }

    /* Remove Help button unless MB_HELP supplied */
    if (!(lpmb->dwStyle & MB_HELP)) {
        NtUserDestroyWindow(GetDlgItem(hwnd, IDHELP));
    }

    /* lhde: XP's layout in place of Wine's: every length in dialog units of the box's own font, the template's MS Shell Dlg 8
     * (Tahoma 8: 50 x 14 DLU buttons are 75 x 23 px), the text and the buttons' names in the message font. Margins 7 DLU
     * around, 10 between the icon and the text and above the buttons, 4 between buttons; every button as wide as the widest
     * button name in the message font plus two digits, at least 50 DLU; the text wraps at 5/8 of the work area (6/8, 7/8, then
     * the whole screen while the box is taller than the work area); the box at least as wide as its caption and two caption
     * buttons; the buttons centred, the text centred on the icon when it is shorter. XP takes the units from the message font,
     * which is Tahoma 8 under every XP style; here a Longhorn style's larger message font (Segoe UI 9) changes the text only */
    {
        static const WCHAR *names[] = { L"OK", L"Cancel", L"&Abort", L"&Retry", L"&Ignore", L"&Yes", L"&No", L"&Try Again",
                                        L"&Continue", L"Help" };
        RECT du = { 0, 0, 4, 8 }, wr, cr, tr;
        int cxu, cyu, outx, outy, inx, gap, cxicon, cyicon, cxbuttons, cxfoo, cxcaption, cxmax, cxbox, cytext, cxtext;
        int step, maxw;
        HFONT capfont;
        SIZE sz, zero;
        WCHAR caption[256];

        MapDialogRect(hwnd, &du);
        cxu = du.right; cyu = du.bottom;
        outx = MulDiv(7, cxu, 4); outy = MulDiv(7, cyu, 8);
        inx = MulDiv(10, cxu, 4);
        gap = MulDiv(4, cxu, 4);
        bh = MulDiv(14, cyu, 8);

        GetWindowRect(hwnd, &wr);
        GetClientRect(hwnd, &cr);
        borwidth = (wr.right - wr.left) - cr.right;
        borheight = (wr.bottom - wr.top) - cr.bottom;

        hdc = NtUserGetDC(hwnd);
        hPrevFont = SelectObject(hdc, (HFONT)SendDlgItemMessageW(hwnd, MSGBOX_IDTEXT, WM_GETFONT, 0, 0));
        maxw = 0;
        for (i = 0; i < ARRAY_SIZE(names); i++)
        {
            GetTextExtentPoint32W(hdc, names[i], lstrlenW(names[i]), &sz);
            if (sz.cx > maxw) maxw = sz.cx;
        }
        GetTextExtentPoint32W(hdc, L"0", 1, &zero);
        bw = max(MulDiv(50, cxu, 4), maxw + 2 * zero.cx);

        for (buttons = 0, i = IDOK; i <= IDCONTINUE; i++)
        {
            if (i == IDCLOSE) continue; /* No CLOSE button */
            hItem = GetDlgItem(hwnd, i);
            if (GetWindowLongW(hItem, GWL_STYLE) & WS_VISIBLE) buttons++;
        }
        cxbuttons = buttons * bw + (buttons - 1) * gap;
        if (lpmb->dwStyle & MB_ICONMASK) { cxicon = GetSystemMetrics(SM_CXICON) + inx; cyicon = GetSystemMetrics(SM_CYICON); }
        else cxicon = cyicon = 0;
        cxfoo = 2 * outx;

        capfont = CreateFontIndirectW(&nclm.lfCaptionFont);
        i = GetWindowTextW(hwnd, caption, ARRAY_SIZE(caption));
        if (capfont) SelectObject(hdc, capfont);
        GetTextExtentPoint32W(hdc, caption, i, &sz);
        SelectObject(hdc, (HFONT)SendDlgItemMessageW(hwnd, MSGBOX_IDTEXT, WM_GETFONT, 0, 0));
        if (capfont) DeleteObject(capfont);
        cxcaption = sz.cx + 2 * GetSystemMetrics(SM_CXSIZE);

        monitor = MonitorFromWindow(lpmb->hwndOwner ? lpmb->hwndOwner : GetActiveWindow(), MONITOR_DEFAULTTOPRIMARY);
        mon_info.cbSize = sizeof(mon_info);
        GetMonitorInfoW(monitor, &mon_info);
        for (step = 5;; step++)
        {
            cxmax = step <= 7 ? MulDiv(mon_info.rcWork.right - mon_info.rcWork.left, step, 8)
                              : mon_info.rcMonitor.right - mon_info.rcMonitor.left;
            cxbox = cxmax - borwidth;
            tr.left = tr.top = 0;
            tr.right = cxbox - cxfoo - cxicon;
            tr.bottom = mon_info.rcWork.bottom - mon_info.rcWork.top;
            DrawTextW(hdc, lpszText, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_EXPANDTABS | DT_NOPREFIX | DT_EXTERNALLEADING |
                      DT_EDITCONTROL);
            cxtext = tr.right - tr.left + cxicon + cxfoo;
            cxbox = min(cxbox, max(cxtext, cxcaption));
            cxbox = max(cxbox, cxbuttons + cxfoo);
            cxtext = cxbox - cxfoo - cxicon;
            tr.left = tr.top = 0;
            tr.right = cxtext;
            tr.bottom = mon_info.rcWork.bottom - mon_info.rcWork.top;
            cytext = DrawTextW(hdc, lpszText, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_EXPANDTABS | DT_NOPREFIX |
                               DT_EXTERNALLEADING | DT_EDITCONTROL);
            wheight = MulDiv(2 * 7 + 10 + 14, cyu, 8) + max(cyicon, cytext) + borheight;
            if (wheight <= mon_info.rcWork.bottom - mon_info.rcWork.top || step > 7) break;
        }
        SelectObject(hdc, hPrevFont);
        NtUserReleaseDC(hwnd, hdc);
        wwidth = cxbox + borwidth;

        wleft = (mon_info.rcWork.left + mon_info.rcWork.right - wwidth) / 2;
        wtop = (mon_info.rcWork.top + mon_info.rcWork.bottom - wheight) / 2;

        /* the box is the owner's transient (WM_TRANSIENT_FOR, a modal dialog to the window manager) */
        if (lpmb->hwndOwner) WUX_MsgBoxOwner(hwnd, lpmb->hwndOwner);

        NtUserSetWindowPos( hwnd, 0, wleft, wtop, wwidth, wheight, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW );
        NtUserSetWindowPos( GetDlgItem(hwnd, MSGBOX_IDICON), 0, outx, outy, 0, 0,
                            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW );
        NtUserSetWindowPos( GetDlgItem(hwnd, MSGBOX_IDTEXT), 0, cxicon + inx,
                            outy + (cytext >= cyicon ? 0 : (cyicon - cytext) / 2), cxtext, cytext,
                            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW );

        /* the buttons in XP's order, centred, outy above the bottom */
        bpos = (cxbox - cxbuttons) / 2;
        tiheight = (wheight - borheight) - outy - bh;
        for (buttons = i = 0; i < ARRAY_SIZE(buttonOrder); i++) {
            hItem = GetDlgItem(hwnd, buttonOrder[i]);
            if (GetWindowLongW(hItem, GWL_STYLE) & WS_VISIBLE) {
                if (buttons++ == ((lpmb->dwStyle & MB_DEFMASK) >> 8)) {
                    NtUserSetFocus(hItem);
                    SendMessageW( hItem, BM_SETSTYLE, BS_DEFPUSHBUTTON, TRUE );
                }
                NtUserSetWindowPos( hItem, 0, bpos, tiheight, bw, bh, SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW );
                bpos += bw + gap;
            }
        }
    }

    HeapFree( GetProcessHeap(), 0, buffer );
}


/**************************************************************************
 *           MSGBOX_DlgProc
 *
 * Dialog procedure for message boxes.
 */
static INT_PTR CALLBACK MSGBOX_DlgProc( HWND hwnd, UINT message,
                                        WPARAM wParam, LPARAM lParam )
{
  switch(message) {
   case WM_INITDIALOG:
   {
       LPMSGBOXPARAMSW mbp = (LPMSGBOXPARAMSW)lParam;
       NtUserSetWindowContextHelpId(hwnd, mbp->dwContextHelpId);
       SetPropA(hwnd, "WUX_MSGBOX_STYLE", (HANDLE)(ULONG_PTR)mbp->dwStyle);
       MSGBOX_OnInit(hwnd, mbp);
       SetPropA(hwnd, "WINE_MSGBOX_HELPCALLBACK", mbp->lpfnMsgBoxCallback);
       break;
   }

   case WM_COMMAND:
    switch (LOWORD(wParam))
    {
     case IDCANCEL:
      /* lhde: Escape (or the close box) without a Cancel button: OK where there is one, else nothing (MessageBox's
       * documented behaviour; Wine's own ends a Yes/No box with IDCANCEL) */
      if (!GetDlgItem(hwnd, IDCANCEL))
      {
          if (GetDlgItem(hwnd, IDOK)) EndDialog(hwnd, IDOK);
          break;
      }
      EndDialog(hwnd, wParam);
      break;
     case IDOK:
     case IDABORT:
     case IDRETRY:
     case IDIGNORE:
     case IDYES:
     case IDNO:
     case IDTRYAGAIN:
     case IDCONTINUE:
      EndDialog(hwnd, wParam);
      break;
     case IDHELP:
      FIXME("Help button not supported yet\n");
      break;
    }
    break;

    case WM_HELP:
    {
        MSGBOXCALLBACK callback = (MSGBOXCALLBACK)GetPropA(hwnd, "WINE_MSGBOX_HELPCALLBACK");
        HELPINFO hi;

        memcpy(&hi, (void *)lParam, sizeof(hi));
        hi.dwContextId = NtUserGetWindowContextHelpId(hwnd);

        if (callback)
            callback(&hi);
        else
            SendMessageW(GetWindow(hwnd, GW_OWNER), WM_HELP, 0, (LPARAM)&hi);
	break;
   }

   case WM_DESTROY:
   {
       HFONT hFont = GetPropA(hwnd, "WUX_MSGBOX_FONT");   /* lhde: the message font made in MSGBOX_OnInit */
       if (hFont) { RemovePropA(hwnd, "WUX_MSGBOX_FONT"); DeleteObject(hFont); }
       RemovePropA(hwnd, "WUX_MSGBOX_STYLE");
       RemovePropA(hwnd, "WINE_MSGBOX_HELPCALLBACK");
       break;
   }

   default:
     /* Ok. Ignore all the other messages */
     TRACE("Message number 0x%04x is being ignored.\n", message);
    break;
  }
  return 0;
}


/**************************************************************************
 *		MessageBoxA (USER32.@)
 */
INT WINAPI MessageBoxA(HWND hWnd, LPCSTR text, LPCSTR title, UINT type)
{
    return MessageBoxExA(hWnd, text, title, type, LANG_NEUTRAL);
}


/**************************************************************************
 *		MessageBoxW (USER32.@)
 */
INT WINAPI MessageBoxW( HWND hwnd, LPCWSTR text, LPCWSTR title, UINT type )
{
    return MessageBoxExW(hwnd, text, title, type, LANG_NEUTRAL);
}


/**************************************************************************
 *		MessageBoxExA (USER32.@)
 */
INT WINAPI MessageBoxExA( HWND hWnd, LPCSTR text, LPCSTR title,
                              UINT type, WORD langid )
{
    MSGBOXPARAMSA msgbox;

    msgbox.cbSize = sizeof(msgbox);
    msgbox.hwndOwner = hWnd;
    msgbox.hInstance = 0;
    msgbox.lpszText = text;
    msgbox.lpszCaption = title;
    msgbox.dwStyle = type;
    msgbox.lpszIcon = NULL;
    msgbox.dwContextHelpId = 0;
    msgbox.lpfnMsgBoxCallback = NULL;
    msgbox.dwLanguageId = langid;

    return MessageBoxIndirectA(&msgbox);
}

/**************************************************************************
 *		MessageBoxExW (USER32.@)
 */
INT WINAPI MessageBoxExW( HWND hWnd, LPCWSTR text, LPCWSTR title,
                              UINT type, WORD langid )
{
    MSGBOXPARAMSW msgbox;

    msgbox.cbSize = sizeof(msgbox);
    msgbox.hwndOwner = hWnd;
    msgbox.hInstance = 0;
    msgbox.lpszText = text;
    msgbox.lpszCaption = title;
    msgbox.dwStyle = type;
    msgbox.lpszIcon = NULL;
    msgbox.dwContextHelpId = 0;
    msgbox.lpfnMsgBoxCallback = NULL;
    msgbox.dwLanguageId = langid;

    return MessageBoxIndirectW(&msgbox);
}

/**************************************************************************
 *      MessageBoxTimeoutA (USER32.@)
 */
INT WINAPI MessageBoxTimeoutA( HWND hWnd, LPCSTR text, LPCSTR title,
                               UINT type, WORD langid, DWORD timeout )
{
    FIXME("timeout not supported (%lu)\n", timeout);
    return MessageBoxExA( hWnd, text, title, type, langid );
}

/**************************************************************************
 *      MessageBoxTimeoutW (USER32.@)
 */
INT WINAPI MessageBoxTimeoutW( HWND hWnd, LPCWSTR text, LPCWSTR title,
                               UINT type, WORD langid, DWORD timeout )
{
    FIXME("timeout not supported (%lu)\n", timeout);
    return MessageBoxExW( hWnd, text, title, type, langid );
}

/**************************************************************************
 *		MessageBoxIndirectA (USER32.@)
 */
/* lhde: wux has no ntdll strings; an ANSI string to a heap copy in UTF-16 (CP_ACP is UTF-8 here) */
typedef struct { LPWSTR Buffer; } UNICODE_STRING;
static void RtlCreateUnicodeStringFromAsciiz( UNICODE_STRING *s, LPCSTR a )
{
    int n = MultiByteToWideChar( CP_ACP, 0, a, -1, NULL, 0 );
    s->Buffer = HeapAlloc( GetProcessHeap(), 0, (n > 0 ? n : 1) * sizeof(WCHAR) );
    if (!s->Buffer) return;
    s->Buffer[0] = 0;
    if (n > 0) MultiByteToWideChar( CP_ACP, 0, a, -1, s->Buffer, n );
}
static void RtlFreeUnicodeString( UNICODE_STRING *s ) { HeapFree( GetProcessHeap(), 0, s->Buffer ); }

INT WINAPI MessageBoxIndirectA( LPMSGBOXPARAMSA msgbox )
{
    MSGBOXPARAMSW msgboxW;
    UNICODE_STRING textW, captionW, iconW;
    int ret;

    if (IS_INTRESOURCE(msgbox->lpszText))
        textW.Buffer = (LPWSTR)msgbox->lpszText;
    else
        RtlCreateUnicodeStringFromAsciiz(&textW, msgbox->lpszText);
    if (IS_INTRESOURCE(msgbox->lpszCaption))
        captionW.Buffer = (LPWSTR)msgbox->lpszCaption;
    else
        RtlCreateUnicodeStringFromAsciiz(&captionW, msgbox->lpszCaption);

    if (msgbox->dwStyle & MB_USERICON)
    {
        if (IS_INTRESOURCE(msgbox->lpszIcon))
            iconW.Buffer = (LPWSTR)msgbox->lpszIcon;
        else
            RtlCreateUnicodeStringFromAsciiz(&iconW, msgbox->lpszIcon);
    }
    else
        iconW.Buffer = NULL;

    msgboxW.cbSize = sizeof(msgboxW);
    msgboxW.hwndOwner = msgbox->hwndOwner;
    msgboxW.hInstance = msgbox->hInstance;
    msgboxW.lpszText = textW.Buffer;
    msgboxW.lpszCaption = captionW.Buffer;
    msgboxW.dwStyle = msgbox->dwStyle;
    msgboxW.lpszIcon = iconW.Buffer;
    msgboxW.dwContextHelpId = msgbox->dwContextHelpId;
    msgboxW.lpfnMsgBoxCallback = msgbox->lpfnMsgBoxCallback;
    msgboxW.dwLanguageId = msgbox->dwLanguageId;

    ret = MessageBoxIndirectW(&msgboxW);

    if (!IS_INTRESOURCE(textW.Buffer)) RtlFreeUnicodeString(&textW);
    if (!IS_INTRESOURCE(captionW.Buffer)) RtlFreeUnicodeString(&captionW);
    if (!IS_INTRESOURCE(iconW.Buffer)) RtlFreeUnicodeString(&iconW);
    return ret;
}

/**************************************************************************
 *		MessageBoxIndirectW (USER32.@)
 */
INT WINAPI MessageBoxIndirectW( LPMSGBOXPARAMSW msgbox )
{
    LPVOID tmplate;
    HRSRC hRes;
    int ret;
    UINT i;
    struct ThreadWindows threadWindows;

    if (!(hRes = FindResourceExW(user32_module, (LPWSTR)RT_DIALOG, L"MSGBOX", msgbox->dwLanguageId)))
    {
        if (!msgbox->dwLanguageId ||
            !(hRes = FindResourceExW(user32_module, (LPWSTR)RT_DIALOG, L"MSGBOX", LANG_NEUTRAL)))
            return 0;
    }
    if (!(tmplate = LoadResource(user32_module, hRes)))
        return 0;

    if ((msgbox->dwStyle & MB_TASKMODAL) && (msgbox->hwndOwner==NULL))
    {
        threadWindows.numHandles = 0;
        threadWindows.numAllocs = 10;
        threadWindows.handles = HeapAlloc(GetProcessHeap(), 0, 10*sizeof(HWND));
        EnumThreadWindows(GetCurrentThreadId(), MSGBOX_EnumProc, (LPARAM)&threadWindows);
    }

    NtUserModifyUserStartupInfoFlags( STARTF_USESHOWWINDOW, 0 );
    ret=DialogBoxIndirectParamW(msgbox->hInstance, tmplate,
                                msgbox->hwndOwner, MSGBOX_DlgProc, (LPARAM)msgbox);

    if ((msgbox->dwStyle & MB_TASKMODAL) && (msgbox->hwndOwner==NULL))
    {
        for (i = 0; i < threadWindows.numHandles; i++)
            NtUserEnableWindow(threadWindows.handles[i], TRUE);
        HeapFree(GetProcessHeap(), 0, threadWindows.handles);
    }
    return ret;
}
