/*
 * PROJECT:    luxpad, lhde's notepad (from ReactOS Notepad, base/applications/notepad/main.c)
 * LICENSE:    LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:    Providing a Windows-compatible simple text editor
 * COPYRIGHT:  Copyright 2000 Mike McCormack <Mike_McCormack@looksmart.com.au>
 *             Copyright 1997,98 Marcel Baur <mbaur@g26.ethz.ch>
 *             Copyright 2002 Sylvain Petreolle <spetreolle@yahoo.fr>
 *             Copyright 2002 Andriy Palamarchuk
 *             Copyright 2020-2023 Katayama Hirofumi MZ
 *
 * luxpad [FILE]: XP's notepad on wux, ported from ReactOS's. What changed for lhde: C++ on the W functions (TCHAR gone);
 * an ordinary main() with WuxInit; files through POSIX (text.cpp: read and mmap for CreateFileMapping/MapViewOfFile,
 * write in place); the registry became luxpad.ini and luxpad-state.ini through liblhde (settings.cpp); printing,
 * Page Setup, Help Topics (WinHelp) and the recent-documents list are gone; About is a message box; New Window runs this
 * binary again; the message boxes are the file dialog's XP box (filedlg::message_box: wux's MessageBoxW is a stub).
 * The dialogs it uses are wux's: GetOpenFileNameW / GetSaveFileNameW (flxp's dialog, the Encoding boxes through
 * OFN_ENABLETEMPLATE), ChooseFontW, FindTextW, ReplaceTextW (Wine's, vendored), and a file dropped on the window opens
 * (DragAcceptFiles, WM_DROPFILES over wux's X drop).
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "luxpad.h"
#include "shell/shell.h"

WUX_RESOURCES(luxpad_res, "apps/luxpad/luxpad.res");

NOTEPAD_GLOBALS Globals;
static ATOM aFINDMSGSTRING;

VOID NOTEPAD_EnableSearchMenu()
{
    BOOL bEmpty = (GetWindowTextLengthW(Globals.hEdit) == 0);
    UINT uEnable = MF_BYCOMMAND | (bEmpty ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(Globals.hMenu, CMD_SEARCH, uEnable);
    EnableMenuItem(Globals.hMenu, CMD_SEARCH_NEXT, uEnable);
    EnableMenuItem(Globals.hMenu, CMD_SEARCH_PREV, uEnable);
}

/***********************************************************************
 *           SetFileName
 *
 *  Sets Global File Name.
 */
VOID SetFileName(const WCHAR *szFileName)
{
    lstrcpynW(Globals.szFileName, szFileName ? szFileName : L"", LUXPAD_PATH_MAX);
    Globals.szFileTitle[0] = 0;
    if (Globals.szFileName[0])
        GetFileTitleW(Globals.szFileName, Globals.szFileTitle, MAX_PATH);
}

/***********************************************************************
 *           NOTEPAD_MenuCommand
 *
 *  All handling of main menu events
 */
static int NOTEPAD_MenuCommand(WPARAM wParam)
{
    switch (wParam)
    {
    case CMD_NEW:        DIALOG_FileNew(); break;
    case CMD_NEW_WINDOW: DIALOG_FileNewWindow(); break;
    case CMD_OPEN:       DIALOG_FileOpen(); break;
    case CMD_SAVE:       DIALOG_FileSave(); break;
    case CMD_SAVE_AS:    DIALOG_FileSaveAs(); break;
    case CMD_EXIT:       DIALOG_FileExit(); break;

    case CMD_UNDO:       DIALOG_EditUndo(); break;
    case CMD_CUT:        DIALOG_EditCut(); break;
    case CMD_COPY:       DIALOG_EditCopy(); break;
    case CMD_PASTE:      DIALOG_EditPaste(); break;
    case CMD_DELETE:     DIALOG_EditDelete(); break;
    case CMD_SELECT_ALL: DIALOG_EditSelectAll(); break;
    case CMD_TIME_DATE:  DIALOG_EditTimeDate(); break;

    case CMD_SEARCH:      DIALOG_Search(); break;
    case CMD_SEARCH_NEXT: DIALOG_SearchNext(TRUE); break;
    case CMD_REPLACE:     DIALOG_Replace(); break;
    case CMD_GOTO:        DIALOG_GoTo(); break;
    case CMD_SEARCH_PREV: DIALOG_SearchNext(FALSE); break;

    case CMD_WRAP: DIALOG_EditWrap(); break;
    case CMD_FONT: DIALOG_SelectFont(); break;

    case CMD_STATUSBAR: DIALOG_ViewStatusBar(); break;

    case CMD_HELP_ABOUT_NOTEPAD: DIALOG_HelpAboutNotepad(); break;

    default:
        break;
    }
    return 0;
}

/* Case-insensitive comparison of one UTF-16 unit as CharUpperW does it (the shim's: ASCII, Latin-1, Latin Extended-A,
   Greek, Cyrillic) */
static WCHAR upper_unit(WCHAR c)
{
    WCHAR s[2] = { c, 0 };
    CharUpperW(s);
    return s[0];
}

static BOOL is_word_unit(WCHAR c)
{
    return IsCharAlphaNumericW(c) || c == L'_';
}

/***********************************************************************
 *           NOTEPAD_FindTextAt
 */
static BOOL
NOTEPAD_FindTextAt(FINDREPLACEW *pFindReplace, const WCHAR *pszText, INT iTextLength, DWORD dwPosition)
{
    BOOL bMatches;
    size_t iTargetLength;
    const WCHAR *pchPosition;

    if (!pFindReplace || !pszText)
        return FALSE;

    iTargetLength = (size_t)lstrlenW(pFindReplace->lpstrFindWhat);
    if ((INT)(dwPosition + iTargetLength) > iTextLength)
        return FALSE;
    pchPosition = &pszText[dwPosition];

    /* Make proper comparison */
    bMatches = TRUE;
    for (size_t i = 0; i < iTargetLength && bMatches; i++)
    {
        WCHAR a = pchPosition[i], b = pFindReplace->lpstrFindWhat[i];
        if (pFindReplace->Flags & FR_MATCHCASE)
            bMatches = a == b;
        else
            bMatches = upper_unit(a) == upper_unit(b);
    }

    if (bMatches && (pFindReplace->Flags & FR_WHOLEWORD))
    {
        if (dwPosition > 0)
        {
            if (is_word_unit(*(pchPosition - 1)))
                bMatches = FALSE;
        }
        if ((INT)(dwPosition + iTargetLength) < iTextLength)
        {
            if (is_word_unit(pchPosition[iTargetLength]))
                bMatches = FALSE;
        }
    }

    return bMatches;
}

/***********************************************************************
 *           NOTEPAD_FindNext
 */
BOOL NOTEPAD_FindNext(FINDREPLACEW *pFindReplace, BOOL bReplace, BOOL bShowAlert)
{
    int iTextLength, iTargetLength;
    size_t iAdjustment = 0;
    std::u16string text;
    const WCHAR *pszText = NULL;
    DWORD dwPosition, dwBegin, dwEnd;
    BOOL bMatches = FALSE;
    WCHAR szResource[128], szText[MAX_PATH + 128];
    BOOL bSuccess;

    iTargetLength = lstrlenW(pFindReplace->lpstrFindWhat);

    /* Retrieve the window text */
    iTextLength = GetWindowTextLengthW(Globals.hEdit);
    if (iTextLength > 0)
    {
        text.assign((size_t)iTextLength + 1, 0);
        GetWindowTextW(Globals.hEdit, (WCHAR *)&text[0], iTextLength + 1);
        pszText = (const WCHAR *)text.c_str();
    }

    SendMessageW(Globals.hEdit, EM_GETSEL, (WPARAM) &dwBegin, (LPARAM) &dwEnd);
    if (bReplace && ((dwEnd - dwBegin) == (DWORD) iTargetLength))
    {
        if (NOTEPAD_FindTextAt(pFindReplace, pszText, iTextLength, dwBegin))
        {
            SendMessageW(Globals.hEdit, EM_REPLACESEL, TRUE, (LPARAM) pFindReplace->lpstrReplaceWith);
            iAdjustment = lstrlenW(pFindReplace->lpstrReplaceWith) - (dwEnd - dwBegin);
        }
    }

    if (pFindReplace->Flags & FR_DOWN)
    {
        /* Find Down */
        dwPosition = dwEnd;
        while (dwPosition < (DWORD) iTextLength)
        {
            bMatches = NOTEPAD_FindTextAt(pFindReplace, pszText, iTextLength, dwPosition);
            if (bMatches)
                break;
            dwPosition++;
        }
    }
    else
    {
        /* Find Up */
        dwPosition = dwBegin;
        while (dwPosition > 0)
        {
            dwPosition--;
            bMatches = NOTEPAD_FindTextAt(pFindReplace, pszText, iTextLength, dwPosition);
            if (bMatches)
                break;
        }
    }

    if (bMatches)
    {
        /* Found target */
        if (dwPosition > dwBegin)
            dwPosition += (DWORD) iAdjustment;
        SendMessageW(Globals.hEdit, EM_SETSEL, dwPosition, dwPosition + iTargetLength);
        SendMessageW(Globals.hEdit, EM_SCROLLCARET, 0, 0);
        DIALOG_StatusBarUpdateCaretPos();
        bSuccess = TRUE;
    }
    else
    {
        /* Can't find target */
        if (bShowAlert)
        {
            LoadStringW(Globals.hInstance, STRING_CANNOTFIND, szResource, ARRAYSIZE(szResource));
            wsprintfW(szText, szResource, pFindReplace->lpstrFindWhat);
            LoadStringW(Globals.hInstance, STRING_NOTEPAD, szResource, ARRAYSIZE(szResource));
            NOTEPAD_MessageBox(Globals.hFindReplaceDlg ? Globals.hFindReplaceDlg : Globals.hMainWnd, szText, szResource,
                               MB_OK | MB_ICONINFORMATION);
        }
        bSuccess = FALSE;
    }

    return bSuccess;
}

/***********************************************************************
 *           NOTEPAD_ReplaceAll
 */
static VOID NOTEPAD_ReplaceAll(FINDREPLACEW *pFindReplace)
{
    BOOL bShowAlert = TRUE;

    SendMessageW(Globals.hEdit, EM_SETSEL, 0, 0);

    while (NOTEPAD_FindNext(pFindReplace, TRUE, bShowAlert))
    {
        bShowAlert = FALSE;
    }
}

/***********************************************************************
 *           NOTEPAD_FindTerm
 */
static VOID NOTEPAD_FindTerm(VOID)
{
    Globals.hFindReplaceDlg = NULL;
}

/***********************************************************************
 * Data Initialization
 */
static VOID NOTEPAD_InitData(HINSTANCE hInstance)
{
    WCHAR *p;
    static const WCHAR txt_files[] = L"*.txt";
    static const WCHAR all_files[] = L"*.*";

    ZeroMemory(&Globals, sizeof(Globals));
    Globals.hInstance = hInstance;
    Globals.encFile = ENCODING_DEFAULT;

    p = Globals.szFilter;
    p += LoadStringW(Globals.hInstance, STRING_TEXT_FILES_TXT, p, MAX_STRING_LEN) + 1;
    lstrcpyW(p, txt_files);
    p += ARRAYSIZE(txt_files);

    p += LoadStringW(Globals.hInstance, STRING_ALL_FILES, p, MAX_STRING_LEN) + 1;
    lstrcpyW(p, all_files);
    p += ARRAYSIZE(all_files);
    *p = '\0';
    Globals.find.lpstrFindWhat = NULL;
}

/***********************************************************************
 * Enable/disable items on the menu based on control state
 */
static VOID NOTEPAD_InitMenuPopup(HMENU menu, LPARAM index)
{
    DWORD dwStart, dwEnd;
    int enable;

    (void)index;

    CheckMenuItem(menu, CMD_WRAP, (Globals.bWrapLongLines ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, CMD_STATUSBAR, (Globals.bShowStatusBar ? MF_CHECKED : MF_UNCHECKED));
    /* XP: Status Bar is unavailable while Word Wrap is on, as Go To is */
    EnableMenuItem(menu, CMD_STATUSBAR, Globals.bWrapLongLines ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(menu, CMD_GOTO, Globals.bWrapLongLines ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(menu, CMD_UNDO,
        SendMessageW(Globals.hEdit, EM_CANUNDO, 0, 0) ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(menu, CMD_PASTE,
        IsClipboardFormatAvailable(CF_UNICODETEXT) || IsClipboardFormatAvailable(CF_TEXT) ? MF_ENABLED : MF_GRAYED);
    SendMessageW(Globals.hEdit, EM_GETSEL, (WPARAM)&dwStart, (LPARAM)&dwEnd);
    enable = ((dwStart == dwEnd) ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(menu, CMD_CUT, enable);
    EnableMenuItem(menu, CMD_COPY, enable);
    EnableMenuItem(menu, CMD_DELETE, enable);

    EnableMenuItem(menu, CMD_SELECT_ALL,
        GetWindowTextLengthW(Globals.hEdit) ? MF_ENABLED : MF_GRAYED);
}

LRESULT CALLBACK EDIT_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT ret = CallWindowProcW(Globals.EditProc, hWnd, msg, wParam, lParam);
    /* after the edit control moved the caret (ReactOS asks before; the position is then the old one) */
    switch (msg)
    {
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_CHAR:
            DIALOG_StatusBarUpdateCaretPos();
            UpdateWindowCaption(FALSE);
            break;
        case WM_LBUTTONUP:
        case WM_LBUTTONDOWN:
        case WM_MOUSEMOVE:
            if (msg != WM_MOUSEMOVE || (wParam & MK_LBUTTON))
                DIALOG_StatusBarUpdateCaretPos();
            break;
    }
    return ret;
}

/***********************************************************************
 *           NOTEPAD_WndProc
 */
static LRESULT
WINAPI
NOTEPAD_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {

    case WM_CREATE:
        Globals.hMainWnd = hWnd;
        Globals.hMenu = GetMenu(hWnd);

        DragAcceptFiles(hWnd, TRUE); /* Accept Drag & Drop */

        /* Create controls */
        DoCreateEditWindow();
        DoShowHideStatusBar();

        DIALOG_FileNew(); /* Initialize file info */
        break;

    case WM_COMMAND:
        if (HIWORD(wParam) == EN_CHANGE || HIWORD(wParam) == EN_HSCROLL || HIWORD(wParam) == EN_VSCROLL)
            DIALOG_StatusBarUpdateCaretPos();
        if ((HIWORD(wParam) == EN_CHANGE))
        {
            NOTEPAD_EnableSearchMenu();
            UpdateWindowCaption(FALSE);
        }
        if ((HWND)lParam == Globals.hEdit)   /* the edit control's notifications are not commands */
            break;
        NOTEPAD_MenuCommand(LOWORD(wParam));
        break;

    case WM_CLOSE:
        if (DoCloseFile())
            DestroyWindow(hWnd);
        break;

    case WM_QUERYENDSESSION:
        if (DoCloseFile()) {
            return 1;
        }
        break;

    case WM_DESTROY:
        NOTEPAD_SaveSettingsToRegistry();
        if (Globals.hFont)
            DeleteObject(Globals.hFont);
        SetWindowLongPtrW(Globals.hEdit, GWLP_WNDPROC, (LONG_PTR)Globals.EditProc);
        PostQuitMessage(0);
        break;

    case WM_SIZE:
    {
        RECT rc;
        GetClientRect(hWnd, &rc);

        if (Globals.bShowStatusBar && Globals.hStatusBar)
        {
            RECT rcStatus;
            SendMessageW(Globals.hStatusBar, WM_SIZE, 0, 0);
            GetWindowRect(Globals.hStatusBar, &rcStatus);
            rc.bottom -= rcStatus.bottom - rcStatus.top;
        }

        MoveWindow(Globals.hEdit, 0, 0, rc.right, rc.bottom, TRUE);

        if (Globals.bShowStatusBar && Globals.hStatusBar)
        {
            /* Align status bar parts, only if the status bar resize operation succeeds */
            DIALOG_StatusBarAlignParts();
        }
        break;
    }

    /* The entire client area is covered by edit control and by
     * the status bar. So there is no need to erase main background.
     * This resolves the horrible flicker effect during windows resizes. */
    case WM_ERASEBKGND:
        return 1;

    case WM_SETFOCUS:
        SetFocus(Globals.hEdit);
        break;

    case WM_DROPFILES:
    {
        WCHAR szFileName[LUXPAD_PATH_MAX];
        HDROP hDrop = (HDROP) wParam;

        DragQueryFileW(hDrop, 0, szFileName, ARRAYSIZE(szFileName));
        DragFinish(hDrop);
        DoOpenFile(szFileName);
        break;
    }

    case WM_INITMENUPOPUP:
        NOTEPAD_InitMenuPopup((HMENU)wParam, lParam);
        break;

    default:
        if (msg == aFINDMSGSTRING)
        {
            FINDREPLACEW *pFindReplace = (FINDREPLACEW *) lParam;
            Globals.find = *(FINDREPLACEW *) lParam;

            WaitCursor(TRUE);

            if (pFindReplace->Flags & FR_FINDNEXT)
                NOTEPAD_FindNext(pFindReplace, FALSE, TRUE);
            else if (pFindReplace->Flags & FR_REPLACE)
                NOTEPAD_FindNext(pFindReplace, TRUE, TRUE);
            else if (pFindReplace->Flags & FR_REPLACEALL)
                NOTEPAD_ReplaceAll(pFindReplace);
            else if (pFindReplace->Flags & FR_DIALOGTERM)
                NOTEPAD_FindTerm();

            WaitCursor(FALSE);
            break;
        }

        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

static int AlertFileDoesNotExist(const WCHAR *szFileName)
{
    return DIALOG_StringMsgBox(Globals.hMainWnd, STRING_DOESNOTEXIST,
                               szFileName,
                               MB_ICONEXCLAMATION | MB_YESNOCANCEL);
}

/* luxpad [FILE]: the file opens; a name without an extension that does not exist is tried with .txt (notepad's rule);
   a file that does not exist is offered to be created */
static BOOL HandleCommandLine(const char *arg)
{
    if (!arg || !*arg)
        return TRUE;

    std::string path = arg;
    if (path[0] != '/')
    {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof cwd))
            path = std::string(cwd) + "/" + path;
    }
    std::u16string wpath = PathWide(path);
    BOOL file_exists = FileExists((const WCHAR *)wpath.c_str());
    if (!file_exists && !HasFileExtension((const WCHAR *)wpath.c_str()))
    {
        std::u16string txt = wpath + u".txt";
        if (FileExists((const WCHAR *)txt.c_str()))
        {
            wpath = txt;
            file_exists = TRUE;
        }
    }

    if (file_exists)
    {
        DoOpenFile((const WCHAR *)wpath.c_str());
        InvalidateRect(Globals.hMainWnd, NULL, FALSE);
    }
    else
    {
        switch (AlertFileDoesNotExist((const WCHAR *)wpath.c_str()))
        {
        case IDYES:
        {
            /* XP creates it empty and opens it */
            std::string bytes = PathBytes((const WCHAR *)wpath.c_str());
            FILE *f = fopen(bytes.c_str(), "ab");
            if (f) fclose(f);
            DoOpenFile((const WCHAR *)wpath.c_str());
            break;
        }

        case IDCANCEL:
            return FALSE;

        case IDNO:
            break;
        }
    }

    return TRUE;
}

static int usage(void)
{
    fprintf(stderr, "usage: luxpad [FILE]\n");
    return 2;
}

int main(int argc, char **argv)
{
    MSG msg;
    HACCEL hAccel;
    WNDCLASSEXW wndclass;
    WINDOWPLACEMENT wp;
    static const WCHAR className[] = L"Notepad";
    static const WCHAR winName[] = L"luxpad";
    const char *file = NULL;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))
        {
            usage();
            return 0;
        }
        if (!strcmp(argv[i], "--") && i + 1 < argc) { file = argv[i + 1]; break; }
        if (argv[i][0] == '-' && argv[i][1])
            return usage();
        if (file)
            return usage();
        file = argv[i];
    }

    WuxInit();
    HINSTANCE hInstance = WuxLoadResources(luxpad_res_start, (size_t)(luxpad_res_end - luxpad_res_start));

    aFINDMSGSTRING = (ATOM)RegisterWindowMessageW(FINDMSGSTRINGW);

    NOTEPAD_InitData(hInstance);
    NOTEPAD_LoadSettingsFromRegistry(&wp);

    ZeroMemory(&wndclass, sizeof(wndclass));
    wndclass.cbSize = sizeof(wndclass);
    wndclass.lpfnWndProc = NOTEPAD_WndProc;
    wndclass.hInstance = Globals.hInstance;
    wndclass.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_NPICON));
    wndclass.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wndclass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wndclass.lpszMenuName = MAKEINTRESOURCEW(MAIN_MENU);
    wndclass.lpszClassName = className;
    wndclass.hIconSm = (HICON)LoadImageW(hInstance,
                                         MAKEINTRESOURCEW(IDI_NPICON),
                                         IMAGE_ICON,
                                         GetSystemMetrics(SM_CXSMICON),
                                         GetSystemMetrics(SM_CYSMICON),
                                         0);
    if (!RegisterClassExW(&wndclass))
    {
        ShowLastError();
        return 1;
    }

    /* Globals.hMainWnd will be set in WM_CREATE handling */
    CreateWindowExW(0,
                    className,
                    winName,
                    WS_OVERLAPPEDWINDOW,
                    CW_USEDEFAULT,
                    CW_USEDEFAULT,
                    CW_USEDEFAULT,
                    CW_USEDEFAULT,
                    NULL,
                    NULL,
                    Globals.hInstance,
                    NULL);
    if (!Globals.hMainWnd)
    {
        ShowLastError();
        return 1;
    }

    /* Use the result of CW_USEDEFAULT if the remembered placement is not valid */
    if (wp.rcNormalPosition.right == wp.rcNormalPosition.left)
    {
        GetWindowPlacement(Globals.hMainWnd, &wp);
        wp.showCmd = SW_SHOWNORMAL;
    }
    SetWindowPlacement(Globals.hMainWnd, &wp);
    UpdateWindow(Globals.hMainWnd);

    if (!HandleCommandLine(file))
    {
        DestroyWindow(Globals.hMainWnd);
        return 0;
    }

    hAccel = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(ID_ACCEL));

    while (GetMessageW(&msg, NULL, 0, 0))
    {
        if ((!Globals.hFindReplaceDlg || !IsDialogMessageW(Globals.hFindReplaceDlg, &msg)) &&
            !TranslateAcceleratorW(Globals.hMainWnd, hAccel, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    DestroyAcceleratorTable(hAccel);

    return (int) msg.wParam;
}
