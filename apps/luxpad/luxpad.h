/*
 * PROJECT:    luxpad, lhde's notepad (from ReactOS Notepad, base/applications/notepad/notepad.h)
 * LICENSE:    LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:    Providing a Windows-compatible simple text editor
 * COPYRIGHT:  Copyright 1997,98 Marcel Baur <mbaur@g26.ethz.ch>
 *             Copyright 2002 Sylvain Petreolle <spetreolle@yahoo.fr>
 *             Copyright 2002 Andriy Palamarchuk
 *             Copyright 2000 Mike McCormack <Mike_McCormack@looksmart.com.au>
 *             Copyright 2020-2023 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 *
 * Ported to wux for lhde (C++ on the Win32 API, POSIX files, an ini for the registry; printing dropped): see main.cpp.
 */
#pragma once

#include <string>
#include "wux/wux.h"
#include "resource.h"

#define EDIT_STYLE_WRAP (WS_CHILD | WS_VSCROLL | ES_AUTOVSCROLL | ES_MULTILINE | ES_NOHIDESEL)
#define EDIT_STYLE      (EDIT_STYLE_WRAP | WS_HSCROLL | ES_AUTOHSCROLL)
#define EDIT_CLASS      L"EDIT"

#define MAX_STRING_LEN  255
#define LUXPAD_PATH_MAX 4096

/* Values are indexes of the items in the Encoding combobox. ANSI is the code page's, and the code page is UTF-8 here
   (GetACP() = CP_UTF8): ANSI and UTF-8 write the same bytes. A file read as ANSI is one that is not valid UTF-8; its
   bytes that are not UTF-8 are kept as they were (text.cpp), so it saves back unchanged. */
typedef enum
{
    ENCODING_AUTO    = -1,
    ENCODING_ANSI    =  0,
    ENCODING_UTF16LE =  1,
    ENCODING_UTF16BE =  2,
    ENCODING_UTF8    =  3,
    ENCODING_UTF8BOM =  4
} ENCODING;

#define ENCODING_DEFAULT    ENCODING_UTF8

typedef enum
{
    EOLN_CRLF = 0, /* "\r\n" */
    EOLN_LF   = 1, /* "\n" */
    EOLN_CR   = 2  /* "\r" */
} EOLN; /* End of line (NewLine) type */

typedef struct
{
    HINSTANCE hInstance;
    HWND hMainWnd;
    HWND hFindReplaceDlg;
    HWND hEdit;
    HWND hStatusBar;
    HFONT hFont; /* Font used by the edit control */
    HMENU hMenu;
    LOGFONTW lfFont;
    BOOL bWrapLongLines;
    BOOL bShowStatusBar;
    WCHAR szFindText[MAX_PATH];
    WCHAR szReplaceText[MAX_PATH];
    WCHAR szFileName[LUXPAD_PATH_MAX];
    WCHAR szFileTitle[MAX_PATH];
    WCHAR szFilter[512];
    WCHAR szStatusBarLineCol[MAX_PATH];

    ENCODING encFile;
    EOLN iEoln;

    FINDREPLACEW find;
    WNDPROC EditProc;
    BOOL bWasModified;
} NOTEPAD_GLOBALS;

extern NOTEPAD_GLOBALS Globals;

/* text.cpp: a file's bytes to the edit control's text (CR LF line ends) and back */
BOOL ReadText(int fd, std::u16string& text, ENCODING *pencFile, EOLN *piEoln);
BOOL WriteText(int fd, const WCHAR *pszText, DWORD dwTextLen, ENCODING encFile, EOLN iEoln);

/* settings.cpp: luxpad.ini (the user's choices) and luxpad-state.ini (where the window was, the last search) */
void NOTEPAD_LoadSettingsFromRegistry(WINDOWPLACEMENT *pWP);
void NOTEPAD_SaveSettingsToRegistry(void);

BOOL NOTEPAD_FindNext(FINDREPLACEW *pFindReplace, BOOL bReplace, BOOL bShowAlert);
VOID NOTEPAD_EnableSearchMenu(VOID);
VOID SetFileName(const WCHAR *szFileName);

/* dialog.cpp */
VOID DIALOG_FileNew(VOID);
VOID DIALOG_FileNewWindow(VOID);
VOID DIALOG_FileOpen(VOID);
BOOL DIALOG_FileSave(VOID);
BOOL DIALOG_FileSaveAs(VOID);
VOID DIALOG_FileExit(VOID);

VOID DIALOG_EditUndo(VOID);
VOID DIALOG_EditCut(VOID);
VOID DIALOG_EditCopy(VOID);
VOID DIALOG_EditPaste(VOID);
VOID DIALOG_EditDelete(VOID);
VOID DIALOG_EditSelectAll(VOID);
VOID DIALOG_EditTimeDate(VOID);
VOID DIALOG_EditWrap(VOID);

VOID DIALOG_Search(VOID);
VOID DIALOG_SearchNext(BOOL bDown);
VOID DIALOG_Replace(VOID);
VOID DIALOG_GoTo(VOID);

VOID DIALOG_SelectFont(VOID);

VOID DIALOG_ViewStatusBar(VOID);
VOID DIALOG_StatusBarAlignParts(VOID);
VOID DIALOG_StatusBarUpdateCaretPos(VOID);

VOID DIALOG_HelpAboutNotepad(VOID);

int DIALOG_StringMsgBox(HWND hParent, int formatId, const WCHAR *szString, DWORD dwFlags);

/* utility functions */
VOID ShowLastError(VOID);
BOOL FileExists(const WCHAR *szFilename);
BOOL HasFileExtension(const WCHAR *szFilename);
BOOL DoCloseFile(VOID);
VOID DoOpenFile(const WCHAR *szFileName);
VOID DoShowHideStatusBar(VOID);
VOID DoCreateEditWindow(VOID);
void UpdateWindowCaption(BOOL clearModifyAlert);
VOID WaitCursor(BOOL bBegin);

/* a UTF-16 path for the API from the file system's bytes, and back */
std::string PathBytes(const WCHAR *path);
std::u16string PathWide(const std::string &path);
