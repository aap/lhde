/*
 * PROJECT:    luxpad, lhde's notepad (from ReactOS Notepad, base/applications/notepad/dialog.c)
 * LICENSE:    LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:    Providing a Windows-compatible simple text editor
 * COPYRIGHT:  Copyright 1998,99 Marcel Baur <mbaur@g26.ethz.ch>
 *             Copyright 2002 Sylvain Petreolle <spetreolle@yahoo.fr>
 *             Copyright 2002 Andriy Palamarchuk
 *             Copyright 2023 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 *
 * For lhde: files through POSIX (open, write in place, so the file keeps its inode, mode and extended attributes),
 * errors from errno, New Window runs /proc/self/exe, no printing, About a
 * message box, XP's status bar (hidden while Word Wrap is on, as XP's), XP's Time/Date.
 */
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include "luxpad.h"
#include "shell/shell.h"

extern char **environ;

LRESULT CALLBACK EDIT_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static const WCHAR szDefaultExt[] = L"txt";
static const WCHAR txt_files[] = L"*.txt";

/* Status bar parts index: XP's empty part, then Ln/Col; the line ends and the encoding after them (Windows 10's) */
#define SBPART_EMPTY    0
#define SBPART_CURPOS   1
#define SBPART_EOLN     2
#define SBPART_ENCODING 3

/* Line endings - string resource ID mapping table */
static UINT EolnToStrId[] = {
    STRING_CRLF,
    STRING_LF,
    STRING_CR
};

/* Encoding - string resource ID mapping table */
static UINT EncToStrId[] = {
    STRING_ANSI,
    STRING_UNICODE,
    STRING_UNICODE_BE,
    STRING_UTF8,
    STRING_UTF8_BOM
};

static int g_last_errno;

/* LUXPAD_TRACE=1: every status bar text on stdout as "status PART TEXT", flushed (tests/headless/luxpad.sh reads it) */
static void trace_status(int part, const WCHAR *text)
{
    static int on = -1;
    if (on < 0) on = getenv("LUXPAD_TRACE") && *getenv("LUXPAD_TRACE") == '1';
    if (!on) return;
    static const char *names[] = { "empty", "pos", "eoln", "encoding" };
    printf("status %s %s\n", names[part], WuxUtf8(text).c_str());
    fflush(stdout);
}

VOID ShowLastError(VOID)
{
    int error = g_last_errno ? g_last_errno : errno;
    if (error != 0)
    {
        WCHAR szTitle[MAX_STRING_LEN];
        LoadStringW(Globals.hInstance, STRING_ERROR, szTitle, ARRAYSIZE(szTitle));
        std::u16string msg = WuxWide(strerror(error));
        MessageBoxW(Globals.hMainWnd, (const WCHAR *)msg.c_str(), szTitle, MB_OK | MB_ICONERROR);
    }
    g_last_errno = 0;
}

/**
 * Sets the caption of the main window according to Globals.szFileTitle:
 *    Untitled - luxpad      if no file is open
 *    [filename] - luxpad    if a file is given
 */
void UpdateWindowCaption(BOOL clearModifyAlert)
{
    WCHAR szCaption[MAX_STRING_LEN + MAX_PATH];
    WCHAR szNotepad[MAX_STRING_LEN];
    WCHAR szFilename[MAX_PATH];
    BOOL isModified;

    if (clearModifyAlert)
    {
        /* When a file is being opened or created, there is no need to have
         * the edited flag shown when the file has not been edited yet. */
        isModified = FALSE;
    }
    else
    {
        /* Check whether the user has modified the file or not. If we are
         * in the same state as before, don't change the caption. */
        isModified = !!SendMessageW(Globals.hEdit, EM_GETMODIFY, 0, 0);
        if (isModified == Globals.bWasModified)
            return;
    }

    /* Remember the state for later calls */
    Globals.bWasModified = isModified;

    /* Load the name of the application */
    LoadStringW(Globals.hInstance, STRING_NOTEPAD, szNotepad, ARRAYSIZE(szNotepad));

    /* Determine if the file has been saved or if this is a new file */
    if (Globals.szFileTitle[0] != 0)
        lstrcpynW(szFilename, Globals.szFileTitle, ARRAYSIZE(szFilename));
    else
        LoadStringW(Globals.hInstance, STRING_UNTITLED, szFilename, ARRAYSIZE(szFilename));

    /* XP's caption ("Untitled - Notepad") has no modified mark: ReactOS's "*" is left out */
    wsprintfW(szCaption, L"%s - %s", szFilename, szNotepad);

    SetWindowTextW(Globals.hMainWnd, szCaption);
}

VOID WaitCursor(BOOL bBegin)
{
    static HCURSOR s_hWaitCursor = NULL;
    static HCURSOR s_hOldCursor = NULL;
    static INT s_nLock = 0;

    if (bBegin)
    {
        if (s_nLock++ == 0)
        {
            if (s_hWaitCursor == NULL)
                s_hWaitCursor = LoadCursorW(NULL, (LPCWSTR)IDC_WAIT);
            s_hOldCursor = SetCursor(s_hWaitCursor);
        }
        else
        {
            SetCursor(s_hWaitCursor);
        }
    }
    else
    {
        if (--s_nLock == 0)
            SetCursor(s_hOldCursor);
    }
}


VOID DIALOG_StatusBarAlignParts(VOID)
{
    static const int defaultWidths[] = {120, 150, 120, 120};
    RECT rcStatusBar;
    int parts[4];

    GetClientRect(Globals.hStatusBar, &rcStatusBar);

    parts[0] = rcStatusBar.right - (defaultWidths[1] + defaultWidths[2] + defaultWidths[3]);
    parts[1] = rcStatusBar.right - (defaultWidths[2] + defaultWidths[3]);
    parts[2] = rcStatusBar.right - defaultWidths[3];
    parts[3] = -1; // the right edge of the status bar

    parts[0] = std::max(parts[0], defaultWidths[0]);
    parts[1] = std::max(parts[1], parts[0] + defaultWidths[1]);
    parts[2] = std::max(parts[2], parts[1] + defaultWidths[2]);

    SendMessageW(Globals.hStatusBar, SB_SETPARTS, ARRAYSIZE(parts), (LPARAM)parts);
}

static VOID DIALOG_StatusBarUpdateLineEndings(VOID)
{
    WCHAR szText[128];

    LoadStringW(Globals.hInstance, EolnToStrId[Globals.iEoln], szText, ARRAYSIZE(szText));

    SendMessageW(Globals.hStatusBar, SB_SETTEXTW, SBPART_EOLN, (LPARAM)szText);
    trace_status(SBPART_EOLN, szText);
}

static VOID DIALOG_StatusBarUpdateEncoding(VOID)
{
    WCHAR szText[128] = L"";

    if (Globals.encFile != ENCODING_AUTO)
    {
        LoadStringW(Globals.hInstance, EncToStrId[Globals.encFile], szText, ARRAYSIZE(szText));
    }

    SendMessageW(Globals.hStatusBar, SB_SETTEXTW, SBPART_ENCODING, (LPARAM)szText);
    trace_status(SBPART_ENCODING, szText);
}

static VOID DIALOG_StatusBarUpdateAll(VOID)
{
    if (!Globals.hStatusBar)
        return;
    DIALOG_StatusBarUpdateCaretPos();
    DIALOG_StatusBarUpdateLineEndings();
    DIALOG_StatusBarUpdateEncoding();
}

int DIALOG_StringMsgBox(HWND hParent, int formatId, const WCHAR *szString, DWORD dwFlags)
{
    WCHAR szMessage[MAX_STRING_LEN + LUXPAD_PATH_MAX];
    WCHAR szResource[MAX_STRING_LEN];

    /* Load and format szMessage */
    LoadStringW(Globals.hInstance, formatId, szResource, ARRAYSIZE(szResource));
    std::u16string msg((const char16_t *)szResource);
    size_t at = msg.find(u"%s");
    if (at != std::u16string::npos)
        msg.replace(at, 2, (const char16_t *)szString);
    lstrcpynW(szMessage, (const WCHAR *)msg.c_str(), ARRAYSIZE(szMessage));

    /* Load szCaption: XP's notepad boxes all say Notepad */
    LoadStringW(Globals.hInstance, STRING_NOTEPAD, szResource, ARRAYSIZE(szResource));

    /* Display Modal Dialog */
    return MessageBoxW(hParent, szMessage, szResource, dwFlags);
}

static void AlertFileNotFound(const WCHAR *szFileName)
{
    DIALOG_StringMsgBox(Globals.hMainWnd, STRING_NOTFOUND, szFileName, MB_ICONEXCLAMATION | MB_OK);
}

static int AlertFileNotSaved(const WCHAR *szFileName)
{
    WCHAR szUntitled[MAX_STRING_LEN];

    LoadStringW(Globals.hInstance, STRING_UNTITLED, szUntitled, ARRAYSIZE(szUntitled));

    return DIALOG_StringMsgBox(Globals.hMainWnd, STRING_NOTSAVED,
                               szFileName[0] ? szFileName : szUntitled,
                               MB_ICONEXCLAMATION | MB_YESNOCANCEL);
}

/**
 * Returns:
 *   TRUE  - if file exists
 *   FALSE - if file does not exist
 */
BOOL FileExists(const WCHAR *szFilename)
{
    struct stat st;
    return stat(PathBytes(szFilename).c_str(), &st) == 0;
}

BOOL HasFileExtension(const WCHAR *szFilename)
{
    std::u16string s((const char16_t *)szFilename);
    size_t slash = s.find_last_of(u'/');
    std::u16string name = slash == std::u16string::npos ? s : s.substr(slash + 1);
    return name.find(u'.') != std::u16string::npos;
}

static BOOL DoSaveFile(VOID)
{
    BOOL bRet = FALSE;
    DWORD cchText;

    WaitCursor(TRUE);

    /* In place (O_TRUNC, not a new file renamed over it): the file keeps its inode, its mode, its owner, its extended
     * attributes (the annotations) and its hard links, as notepad's CreateFile(OPEN_ALWAYS) + SetEndOfFile keep them */
    std::string path = PathBytes(Globals.szFileName);
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0)
    {
        g_last_errno = errno;
        ShowLastError();
        WaitCursor(FALSE);
        return FALSE;
    }

    cchText = GetWindowTextLengthW(Globals.hEdit);
    if (cchText <= 0)
    {
        bRet = TRUE;
    }
    else
    {
        std::u16string text((size_t)cchText + 1, 0);
        GetWindowTextW(Globals.hEdit, (WCHAR *)&text[0], cchText + 1);
        bRet = WriteText(fd, (const WCHAR *)text.c_str(), cchText, Globals.encFile, Globals.iEoln);
        if (!bRet)
            g_last_errno = errno;
    }

    if (close(fd) != 0 && bRet)
    {
        g_last_errno = errno;
        bRet = FALSE;
    }
    if (!bRet)
        ShowLastError();

    if (bRet)
    {
        SendMessageW(Globals.hEdit, EM_SETMODIFY, FALSE, 0);
        SetFileName(Globals.szFileName);
    }

    WaitCursor(FALSE);
    return bRet;
}

/**
 * Returns:
 *   TRUE  - User agreed to close (both save/don't save)
 *   FALSE - User cancelled close by selecting "Cancel"
 */
BOOL DoCloseFile(VOID)
{
    int nResult;

    if (SendMessageW(Globals.hEdit, EM_GETMODIFY, 0, 0))
    {
        /* prompt user to save changes */
        nResult = AlertFileNotSaved(Globals.szFileName);
        switch (nResult)
        {
            case IDYES:
                if (!DIALOG_FileSave())
                    return FALSE;
                break;

            case IDNO:
                break;

            case IDCANCEL:
            default:
                return FALSE;
        }
    }

    SetFileName(L"");
    UpdateWindowCaption(TRUE);

    return TRUE;
}

VOID DoOpenFile(const WCHAR *szFileName)
{
    WCHAR log[5];
    std::u16string text;
    std::string path;
    int fd;
    std::u16string name((const char16_t *)szFileName);   /* szFileName may be Globals.szFileName, which closing empties */

    /* Close any files and prompt to save changes */
    if (!DoCloseFile())
        return;

    WaitCursor(TRUE);
    SetWindowTextW(Globals.hEdit, NULL);

    path = PathBytes((const WCHAR *)name.c_str());
    fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
    {
        g_last_errno = errno;
        ShowLastError();
        goto done;
    }

    if (!ReadText(fd, text, &Globals.encFile, &Globals.iEoln))
    {
        g_last_errno = errno ? errno : EIO;
        ShowLastError();
        goto done;
    }
    SetWindowTextW(Globals.hEdit, (const WCHAR *)text.c_str());
    SendMessageW(Globals.hEdit, EM_SETMODIFY, FALSE, 0);
    SendMessageW(Globals.hEdit, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(Globals.hEdit, EM_SETSEL, 0, 0);

    SetFocus(Globals.hEdit);

    /*  If the file starts with .LOG, add a time/date at the end and set cursor after
     *  See http://web.archive.org/web/20090627165105/http://support.microsoft.com/kb/260563
     */
    if (GetWindowTextW(Globals.hEdit, log, ARRAYSIZE(log)) && !lstrcmpW(log, L".LOG"))
    {
        static const WCHAR lf[] = L"\r\n";
        SendMessageW(Globals.hEdit, EM_SETSEL, GetWindowTextLengthW(Globals.hEdit), -1);
        SendMessageW(Globals.hEdit, EM_REPLACESEL, TRUE, (LPARAM)lf);
        DIALOG_EditTimeDate();
        SendMessageW(Globals.hEdit, EM_REPLACESEL, TRUE, (LPARAM)lf);
    }

    SetFileName((const WCHAR *)name.c_str());
    UpdateWindowCaption(TRUE);
    NOTEPAD_EnableSearchMenu();
    DIALOG_StatusBarUpdateAll();

done:
    if (fd >= 0)
        close(fd);
    WaitCursor(FALSE);
}

VOID DIALOG_FileNew(VOID)
{
    /* Close any files and prompt to save changes */
    if (!DoCloseFile())
        return;

    WaitCursor(TRUE);

    SetWindowTextW(Globals.hEdit, NULL);
    SendMessageW(Globals.hEdit, EM_EMPTYUNDOBUFFER, 0, 0);
    Globals.iEoln = EOLN_CRLF;
    Globals.encFile = ENCODING_DEFAULT;

    NOTEPAD_EnableSearchMenu();
    DIALOG_StatusBarUpdateAll();

    WaitCursor(FALSE);
}

VOID DIALOG_FileNewWindow(VOID)
{
    WaitCursor(TRUE);

    char self[4096];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n > 0)
    {
        self[n] = 0;
        std::string err;
        shell::spawn({ self }, "", &err);
    }

    WaitCursor(FALSE);
}

VOID DIALOG_FileOpen(VOID)
{
    OPENFILENAMEW openfilename;
    WCHAR szPath[LUXPAD_PATH_MAX];

    ZeroMemory(&openfilename, sizeof(openfilename));

    if (Globals.szFileName[0] == 0)
        lstrcpyW(szPath, txt_files);
    else
        lstrcpyW(szPath, Globals.szFileName);

    openfilename.lStructSize = sizeof(openfilename);
    openfilename.hwndOwner = Globals.hMainWnd;
    openfilename.hInstance = Globals.hInstance;
    openfilename.lpstrFilter = Globals.szFilter;
    openfilename.lpstrFile = szPath;
    openfilename.nMaxFile = ARRAYSIZE(szPath);
    openfilename.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    openfilename.lpstrDefExt = szDefaultExt;

    if (GetOpenFileNameW(&openfilename)) {
        if (FileExists(openfilename.lpstrFile))
            DoOpenFile(openfilename.lpstrFile);
        else
            AlertFileNotFound(openfilename.lpstrFile);
    }
}

BOOL DIALOG_FileSave(VOID)
{
    if (Globals.szFileName[0] == 0)
    {
        return DIALOG_FileSaveAs();
    }
    else if (DoSaveFile())
    {
        UpdateWindowCaption(TRUE);
        return TRUE;
    }
    return FALSE;
}

static UINT_PTR
CALLBACK
DIALOG_FileSaveAs_Hook(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WCHAR szText[128];
    HWND hCombo;

    (void)wParam;

    switch(msg)
    {
        case WM_INITDIALOG:
            hCombo = GetDlgItem(hDlg, ID_ENCODING);

            LoadStringW(Globals.hInstance, STRING_ANSI, szText, ARRAYSIZE(szText));
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM) szText);

            LoadStringW(Globals.hInstance, STRING_UNICODE, szText, ARRAYSIZE(szText));
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM) szText);

            LoadStringW(Globals.hInstance, STRING_UNICODE_BE, szText, ARRAYSIZE(szText));
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM) szText);

            LoadStringW(Globals.hInstance, STRING_UTF8, szText, ARRAYSIZE(szText));
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM) szText);

            LoadStringW(Globals.hInstance, STRING_UTF8_BOM, szText, ARRAYSIZE(szText));
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM) szText);

            SendMessageW(hCombo, CB_SETCURSEL, Globals.encFile, 0);

            hCombo = GetDlgItem(hDlg, ID_EOLN);

            LoadStringW(Globals.hInstance, STRING_CRLF, szText, ARRAYSIZE(szText));
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM) szText);

            LoadStringW(Globals.hInstance, STRING_LF, szText, ARRAYSIZE(szText));
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM) szText);

            LoadStringW(Globals.hInstance, STRING_CR, szText, ARRAYSIZE(szText));
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM) szText);

            SendMessageW(hCombo, CB_SETCURSEL, Globals.iEoln, 0);
            break;

        case WM_NOTIFY:
            if (((NMHDR *) lParam)->code == CDN_FILEOK)
            {
                hCombo = GetDlgItem(hDlg, ID_ENCODING);
                if (hCombo)
                    Globals.encFile = (ENCODING) SendMessageW(hCombo, CB_GETCURSEL, 0, 0);

                hCombo = GetDlgItem(hDlg, ID_EOLN);
                if (hCombo)
                    Globals.iEoln = (EOLN)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
            }
            break;
    }
    return 0;
}

BOOL DIALOG_FileSaveAs(VOID)
{
    OPENFILENAMEW saveas;
    WCHAR szPath[LUXPAD_PATH_MAX];
    ENCODING encOld = Globals.encFile;
    EOLN eolnOld = Globals.iEoln;

    ZeroMemory(&saveas, sizeof(saveas));

    if (Globals.szFileName[0] == 0)
        lstrcpyW(szPath, txt_files);
    else
        lstrcpyW(szPath, Globals.szFileName);

    saveas.lStructSize = sizeof(OPENFILENAMEW);
    saveas.hwndOwner = Globals.hMainWnd;
    saveas.hInstance = Globals.hInstance;
    saveas.lpstrFilter = Globals.szFilter;
    saveas.lpstrFile = szPath;
    saveas.nMaxFile = ARRAYSIZE(szPath);
    saveas.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY |
                   OFN_EXPLORER | OFN_ENABLETEMPLATE | OFN_ENABLEHOOK | OFN_NOCHANGEDIR;
    saveas.lpstrDefExt = szDefaultExt;
    saveas.lpTemplateName = MAKEINTRESOURCEW(DIALOG_ENCODING);
    saveas.lpfnHook = DIALOG_FileSaveAs_Hook;

    if (GetSaveFileNameW(&saveas))
    {
        WCHAR szOldName[LUXPAD_PATH_MAX];
        lstrcpyW(szOldName, Globals.szFileName);
        SetFileName(szPath);
        if (DoSaveFile())
        {
            UpdateWindowCaption(TRUE);
            DIALOG_StatusBarUpdateAll();
            return TRUE;
        }
        else
        {
            /* the file stays what it was (ReactOS forgets its name) */
            SetFileName(szOldName);
            Globals.encFile = encOld;
            Globals.iEoln = eolnOld;
            return FALSE;
        }
    }
    else
    {
        return FALSE;
    }
}

VOID DIALOG_FileExit(VOID)
{
    PostMessageW(Globals.hMainWnd, WM_CLOSE, 0, 0);
}

VOID DIALOG_EditUndo(VOID)
{
    SendMessageW(Globals.hEdit, EM_UNDO, 0, 0);
}

VOID DIALOG_EditCut(VOID)
{
    SendMessageW(Globals.hEdit, WM_CUT, 0, 0);
}

VOID DIALOG_EditCopy(VOID)
{
    SendMessageW(Globals.hEdit, WM_COPY, 0, 0);
}

VOID DIALOG_EditPaste(VOID)
{
    SendMessageW(Globals.hEdit, WM_PASTE, 0, 0);
}

VOID DIALOG_EditDelete(VOID)
{
    SendMessageW(Globals.hEdit, WM_CLEAR, 0, 0);
}

VOID DIALOG_EditSelectAll(VOID)
{
    SendMessageW(Globals.hEdit, EM_SETSEL, 0, -1);
    DIALOG_StatusBarUpdateCaretPos();
}

/* XP's notepad: the short time without seconds, a space, the short date ("11:41 PM 11/12/2026"); ReactOS's has the
   seconds and the long date */
VOID DIALOG_EditTimeDate(VOID)
{
    SYSTEMTIME st;
    WCHAR szDate[MAX_STRING_LEN];
    WCHAR szText[MAX_STRING_LEN * 2 + 2];

    GetLocalTime(&st);

    GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, NULL, szDate, MAX_STRING_LEN);
    lstrcpyW(szText, szDate);
    lstrcatW(szText, L" ");
    GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, szDate, MAX_STRING_LEN);
    lstrcatW(szText, szDate);
    SendMessageW(Globals.hEdit, EM_REPLACESEL, TRUE, (LPARAM)szText);
}

VOID DoShowHideStatusBar(VOID)
{
    /* XP: no status bar while Word Wrap is on */
    BOOL bShow = Globals.bShowStatusBar && !Globals.bWrapLongLines;

    /* Check if status bar object already exists. */
    if (bShow && Globals.hStatusBar == NULL)
    {
        /* Try to create the status bar */
        Globals.hStatusBar = CreateStatusWindowW(WS_CHILD | CCS_BOTTOM | SBARS_SIZEGRIP,
                                                 NULL,
                                                 Globals.hMainWnd,
                                                 CMD_STATUSBAR_WND_ID);

        if (Globals.hStatusBar == NULL)
        {
            ShowLastError();
            return;
        }

        /* Load the string for formatting column/row text output */
        LoadStringW(Globals.hInstance, STRING_LINE_COLUMN, Globals.szStatusBarLineCol, MAX_PATH - 1);
    }

    if (Globals.hStatusBar != NULL)
    {
        /* Update visibility of status bar */
        ShowWindow(Globals.hStatusBar, (bShow ? SW_SHOWNOACTIVATE : SW_HIDE));
        if (!bShow)
        {
            DestroyWindow(Globals.hStatusBar);
            Globals.hStatusBar = NULL;
        }
    }

    /* Update layout of controls */
    SendMessageW(Globals.hMainWnd, WM_SIZE, 0, 0);

    if (Globals.hStatusBar == NULL)
        return;

    /* Update status bar contents */
    DIALOG_StatusBarUpdateAll();
}

VOID DoCreateEditWindow(VOID)
{
    DWORD dwStyle;
    int iSize;
    std::u16string temp;
    BOOL bModified = FALSE;
    DWORD dwStart = 0, dwEnd = 0;

    iSize = 0;

    /* If the edit control already exists, try to save its content */
    if (Globals.hEdit != NULL)
    {
        /* number of chars currently written into the editor. */
        iSize = GetWindowTextLengthW(Globals.hEdit);
        if (iSize)
        {
            temp.assign((size_t)iSize + 1, 0);

            /* Recover the text into the control. */
            GetWindowTextW(Globals.hEdit, (WCHAR *)&temp[0], iSize + 1);

            if (SendMessageW(Globals.hEdit, EM_GETMODIFY, 0, 0))
                bModified = TRUE;
            SendMessageW(Globals.hEdit, EM_GETSEL, (WPARAM)&dwStart, (LPARAM)&dwEnd);
        }

        /* Restore original window procedure */
        SetWindowLongPtrW(Globals.hEdit, GWLP_WNDPROC, (LONG_PTR)Globals.EditProc);

        /* Destroy the edit control */
        DestroyWindow(Globals.hEdit);
    }

    /* Update wrap status into the main menu and recover style flags */
    dwStyle = (Globals.bWrapLongLines ? EDIT_STYLE_WRAP : EDIT_STYLE);

    /* Create the new edit control */
    Globals.hEdit = CreateWindowExW(WS_EX_CLIENTEDGE,
                                    EDIT_CLASS,
                                    NULL,
                                    dwStyle,
                                    CW_USEDEFAULT,
                                    CW_USEDEFAULT,
                                    CW_USEDEFAULT,
                                    CW_USEDEFAULT,
                                    Globals.hMainWnd,
                                    NULL,
                                    Globals.hInstance,
                                    NULL);
    if (Globals.hEdit == NULL)
    {
        ShowLastError();
        return;
    }

    SendMessageW(Globals.hEdit, WM_SETFONT, (WPARAM)Globals.hFont, FALSE);
    SendMessageW(Globals.hEdit, EM_LIMITTEXT, 0, 0);

    /* If some text was previously saved, restore it. */
    if (iSize != 0)
    {
        SetWindowTextW(Globals.hEdit, (const WCHAR *)temp.c_str());
        SendMessageW(Globals.hEdit, EM_SETSEL, dwStart, dwEnd);

        if (bModified)
            SendMessageW(Globals.hEdit, EM_SETMODIFY, TRUE, 0);
    }

    /* Sub-class a new window callback for row/column detection. */
    Globals.EditProc = (WNDPROC)SetWindowLongPtrW(Globals.hEdit,
                                                  GWLP_WNDPROC,
                                                  (LONG_PTR)EDIT_WndProc);

    /* Finally shows new edit control and set focus into it. */
    ShowWindow(Globals.hEdit, SW_SHOW);
    SetFocus(Globals.hEdit);

    /* Re-arrange controls */
    PostMessageW(Globals.hMainWnd, WM_SIZE, 0, 0);
}

VOID DIALOG_EditWrap(VOID)
{
    Globals.bWrapLongLines = !Globals.bWrapLongLines;

    EnableMenuItem(Globals.hMenu, CMD_GOTO, (Globals.bWrapLongLines ? MF_GRAYED : MF_ENABLED));

    DoCreateEditWindow();
    DoShowHideStatusBar();
}

VOID DIALOG_SelectFont(VOID)
{
    CHOOSEFONTW cf;
    LOGFONTW lf = Globals.lfFont;

    ZeroMemory( &cf, sizeof(cf) );
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = Globals.hMainWnd;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS;

    if (ChooseFontW(&cf))
    {
        HFONT currfont = Globals.hFont;

        Globals.hFont = CreateFontIndirectW(&lf);
        Globals.lfFont = lf;
        SendMessageW(Globals.hEdit, WM_SETFONT, (WPARAM)Globals.hFont, TRUE);
        if (currfont != NULL)
            DeleteObject(currfont);
    }
}

typedef HWND (WINAPI *FINDPROC)(LPFINDREPLACEW lpfr);

static VOID DIALOG_SearchDialog(FINDPROC pfnProc)
{
    if (Globals.hFindReplaceDlg != NULL)
    {
        SetFocus(Globals.hFindReplaceDlg);
        return;
    }

    if (!Globals.find.lpstrFindWhat)
    {
        DWORD flags = Globals.find.Flags ? Globals.find.Flags : FR_DOWN;   /* settings.cpp may have remembered them */
        ZeroMemory(&Globals.find, sizeof(Globals.find));
        Globals.find.lStructSize = sizeof(Globals.find);
        Globals.find.hwndOwner = Globals.hMainWnd;
        Globals.find.lpstrFindWhat = Globals.szFindText;
        Globals.find.wFindWhatLen = ARRAYSIZE(Globals.szFindText);
        Globals.find.lpstrReplaceWith = Globals.szReplaceText;
        Globals.find.wReplaceWithLen = ARRAYSIZE(Globals.szReplaceText);
        Globals.find.Flags = flags & (FR_DOWN | FR_MATCHCASE | FR_WHOLEWORD);
    }

    /* XP's Find selects the text under the caret's selection when there is one on a single line */
    DWORD dwStart = 0, dwEnd = 0;
    SendMessageW(Globals.hEdit, EM_GETSEL, (WPARAM)&dwStart, (LPARAM)&dwEnd);
    if (dwEnd > dwStart && dwEnd - dwStart < ARRAYSIZE(Globals.szFindText))
    {
        int n = GetWindowTextLengthW(Globals.hEdit);
        std::u16string text((size_t)n + 1, 0);
        GetWindowTextW(Globals.hEdit, (WCHAR *)&text[0], n + 1);
        std::u16string sel = text.substr(dwStart, dwEnd - dwStart);
        if (sel.find_first_of(u"\r\n") == std::u16string::npos)
            lstrcpynW(Globals.szFindText, (const WCHAR *)sel.c_str(), ARRAYSIZE(Globals.szFindText));
    }

    /* We only need to create the modal FindReplace dialog which will */
    /* notify us of incoming events using hMainWnd Window Messages    */

    Globals.hFindReplaceDlg = pfnProc(&Globals.find);
}

VOID DIALOG_Search(VOID)
{
    DIALOG_SearchDialog(FindTextW);
}

VOID DIALOG_SearchNext(BOOL bDown)
{
    if (bDown)
        Globals.find.Flags |= FR_DOWN;
    else
        Globals.find.Flags &= ~FR_DOWN;

    if (Globals.find.lpstrFindWhat != NULL && *Globals.find.lpstrFindWhat)
        NOTEPAD_FindNext(&Globals.find, FALSE, TRUE);
    else
        DIALOG_Search();
}

VOID DIALOG_Replace(VOID)
{
    DIALOG_SearchDialog(ReplaceTextW);
}

typedef struct tagGOTO_DATA
{
    UINT iLine;
    UINT cLines;
} GOTO_DATA, *PGOTO_DATA;

static INT_PTR
CALLBACK
DIALOG_GoTo_DialogProc(HWND hwndDialog, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static PGOTO_DATA s_pGotoData;

    switch (uMsg)
    {
        case WM_INITDIALOG:
            s_pGotoData = (PGOTO_DATA)lParam;
            SetDlgItemInt(hwndDialog, ID_LINENUMBER, s_pGotoData->iLine, FALSE);
            SendDlgItemMessageW(hwndDialog, ID_LINENUMBER, EM_SETSEL, 0, -1);
            return TRUE; /* Set focus */

        case WM_COMMAND:
        {
            if (LOWORD(wParam) == IDOK)
            {
                UINT iLine = GetDlgItemInt(hwndDialog, ID_LINENUMBER, NULL, FALSE);
                if (iLine <= 0 || s_pGotoData->cLines < iLine) /* Out of range */
                {
                    /* Show error message */
                    WCHAR title[128], text[256];
                    LoadStringW(Globals.hInstance, STRING_NOTEPAD, title, ARRAYSIZE(title));
                    LoadStringW(Globals.hInstance, STRING_LINE_NUMBER_OUT_OF_RANGE, text, ARRAYSIZE(text));
                    MessageBoxW(hwndDialog, text, title, MB_OK);

                    SendDlgItemMessageW(hwndDialog, ID_LINENUMBER, EM_SETSEL, 0, -1);
                    SetFocus(GetDlgItem(hwndDialog, ID_LINENUMBER));
                    break;
                }
                s_pGotoData->iLine = iLine;
                EndDialog(hwndDialog, IDOK);
            }
            else if (LOWORD(wParam) == IDCANCEL)
            {
                EndDialog(hwndDialog, IDCANCEL);
            }
            break;
        }
    }

    return 0;
}

VOID DIALOG_GoTo(VOID)
{
    GOTO_DATA GotoData;
    DWORD dwStart = 0, dwEnd = 0;
    INT ich, cch = GetWindowTextLengthW(Globals.hEdit);

    if (Globals.bWrapLongLines)   /* greyed with Word Wrap (as XP), the accelerator too */
        return;

    /* Get the current line number and the total line number */
    SendMessageW(Globals.hEdit, EM_GETSEL, (WPARAM) &dwStart, (LPARAM) &dwEnd);
    GotoData.iLine = (UINT)SendMessageW(Globals.hEdit, EM_LINEFROMCHAR, dwStart, 0) + 1;
    GotoData.cLines = (UINT)SendMessageW(Globals.hEdit, EM_GETLINECOUNT, 0, 0);

    /* Ask the user for line number */
    if (DialogBoxParamW(Globals.hInstance,
                        MAKEINTRESOURCEW(DIALOG_GOTO),
                        Globals.hMainWnd,
                        DIALOG_GoTo_DialogProc,
                        (LPARAM)&GotoData) != IDOK)
    {
        return; /* Canceled */
    }

    --GotoData.iLine; /* Make it zero-based */

    /* Get ich (the target character index) from line number */
    if (GotoData.iLine <= 0)
        ich = 0;
    else if (GotoData.iLine >= GotoData.cLines)
        ich = cch;
    else
        ich = (INT)SendMessageW(Globals.hEdit, EM_LINEINDEX, GotoData.iLine, 0);

    /* EM_LINEINDEX can return -1 on failure */
    if (ich < 0)
        ich = 0;

    /* Move the caret */
    SendMessageW(Globals.hEdit, EM_SETSEL, ich, ich);
    SendMessageW(Globals.hEdit, EM_SCROLLCARET, 0, 0);
    DIALOG_StatusBarUpdateCaretPos();
}

/* Ln and Col as XP counts them: lines of the text (a wrapped line is one), the column in UTF-16 units from the line's
   start, both from 1; the caret is the selection's moving end */
VOID DIALOG_StatusBarUpdateCaretPos(VOID)
{
    int line, ich, col;
    WCHAR buff[MAX_PATH];
    DWORD dwStart, dwSize;

    if (!Globals.hStatusBar)
        return;

    SendMessageW(Globals.hEdit, EM_GETSEL, (WPARAM)&dwStart, (LPARAM)&dwSize);
    line = (int)SendMessageW(Globals.hEdit, EM_LINEFROMCHAR, (WPARAM)dwSize, 0);
    ich = (int)SendMessageW(Globals.hEdit, EM_LINEINDEX, (WPARAM)line, 0);

    /* EM_LINEINDEX can return -1 on failure */
    col = ((ich < 0) ? 0 : (int)(dwSize - ich));

    wsprintfW(buff, Globals.szStatusBarLineCol, line + 1, col + 1);
    WCHAR old[MAX_PATH];
    old[0] = 0;
    SendMessageW(Globals.hStatusBar, SB_GETTEXTW, SBPART_CURPOS, (LPARAM)old);
    SendMessageW(Globals.hStatusBar, SB_SETTEXTW, SBPART_CURPOS, (LPARAM)buff);
    if (lstrcmpW(old, buff))
        trace_status(SBPART_CURPOS, buff);
}

VOID DIALOG_ViewStatusBar(VOID)
{
    Globals.bShowStatusBar = !Globals.bShowStatusBar;
    DoShowHideStatusBar();
}

VOID DIALOG_HelpAboutNotepad(VOID)
{
    WCHAR szNotepad[MAX_STRING_LEN];
    WCHAR szNotepadAuthors[MAX_STRING_LEN * 2];
    WCHAR szFormat[MAX_STRING_LEN * 2];
    WCHAR szText[MAX_STRING_LEN * 4];

    LoadStringW(Globals.hInstance, STRING_NOTEPAD, szNotepad, ARRAYSIZE(szNotepad));
    LoadStringW(Globals.hInstance, STRING_NOTEPAD_AUTHORS, szNotepadAuthors, ARRAYSIZE(szNotepadAuthors));
    LoadStringW(Globals.hInstance, STRING_ABOUT, szFormat, ARRAYSIZE(szFormat));
    wsprintfW(szText, szFormat, szNotepadAuthors);

    std::u16string caption = u"About " + std::u16string((const char16_t *)szNotepad);
    MessageBoxW(Globals.hMainWnd, szText, (const WCHAR *)caption.c_str(), MB_OK | MB_ICONINFORMATION);
}
