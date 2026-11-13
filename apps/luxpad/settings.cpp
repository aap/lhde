/*
 * PROJECT:    luxpad, lhde's notepad (from ReactOS Notepad, base/applications/notepad/settings.c)
 * LICENSE:    LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:    Providing a Windows-compatible simple text editor
 * COPYRIGHT:  Copyright 1998,99 Marcel Baur <mbaur@g26.ethz.ch>
 *             Copyright 2002 Sylvain Petreolle <spetreolle@yahoo.fr>
 *             Copyright 2002 Andriy Palamarchuk
 *
 * notepad's registry key (HKCU\Software\Microsoft\Notepad) as two ini files through liblhde (docs/SETTINGS.md "Files"):
 * what the user chose, ${XDG_CONFIG_HOME:-~/.config}/lhde/luxpad.ini
 *     [luxpad] font = FACE, font_size = POINTS (tenths allowed: 10.5), font_weight = 400, font_italic = 0|1,
 *              wrap = 0|1, statusbar = 0|1
 * and what it remembers on its own, ${XDG_STATE_HOME:-~/.local/state}/lhde/luxpad.ini
 *     [window] x, y, width, height, maximized = 0|1     [search] find, replace, match_case, whole_word, down
 * Both are read at start and written at exit; a missing key is notepad's default (Lucida Console 10, no wrap, the status
 * bar shown, the window manager's placement). The page setup values went with printing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "luxpad.h"
#include "lhde/settings.h"

static LONG HeightFromPointSize(DWORD dwPointSize)
{
    LONG lHeight;
    HDC hDC;

    hDC = GetDC(NULL);
    lHeight = -MulDiv(dwPointSize, GetDeviceCaps(hDC, LOGPIXELSY), 720);
    ReleaseDC(NULL, hDC);

    return lHeight;
}

static DWORD PointSizeFromHeight(LONG lHeight)
{
    DWORD dwPointSize;
    HDC hDC;

    hDC = GetDC(NULL);
    dwPointSize = -MulDiv(lHeight, 720, GetDeviceCaps(hDC, LOGPIXELSY));
    ReleaseDC(NULL, hDC);

    /* round to nearest multiple of 10 */
    dwPointSize += 5;
    dwPointSize -= dwPointSize % 10;

    return dwPointSize;
}

static std::string config_path(void)
{
    char buf[4096];
    return lhde_config_file("luxpad.ini", buf, sizeof buf) ? buf : "";
}

static std::string state_path(void)
{
    char buf[4096];
    return lhde_state_file("luxpad.ini", buf, sizeof buf) ? buf : "";
}

static bool get(const lhde_ini *ini, const char *sec, const char *key, std::string &out)
{
    char buf[1024];
    if (!ini || !lhde_ini_get(ini, sec, key, buf, sizeof buf))
        return false;
    out = buf;
    return true;
}

static void get_int(const lhde_ini *ini, const char *sec, const char *key, LONG *v)
{
    std::string s;
    if (get(ini, sec, key, s) && !s.empty())
    {
        char *end;
        long n = strtol(s.c_str(), &end, 10);
        if (!*end) *v = (LONG)n;
    }
}

static void get_bool(const lhde_ini *ini, const char *sec, const char *key, BOOL *v)
{
    LONG n = *v;
    get_int(ini, sec, key, &n);
    *v = n != 0;
}

static void get_text(const lhde_ini *ini, const char *sec, const char *key, WCHAR *out, int cch)
{
    std::string s;
    if (get(ini, sec, key, s))
    {
        std::u16string w = WuxWide(s);
        lstrcpynW(out, (const WCHAR *)w.c_str(), cch);
    }
}

/***********************************************************************
 *           NOTEPAD_LoadSettingsFromRegistry
 *
 *  Load settings from luxpad.ini (config and state).
 */
void NOTEPAD_LoadSettingsFromRegistry(WINDOWPLACEMENT *pWP)
{
    HFONT hFont;
    DWORD dwPointSize;
    LONG x = CW_USEDEFAULT, y = CW_USEDEFAULT, cx = 0, cy = 0, maximized = 0;
    lhde_ini *cfg = lhde_ini_load(config_path().c_str());
    lhde_ini *state = lhde_ini_load(state_path().c_str());

    /* Set the default values */
    Globals.bShowStatusBar = FALSE;   /* XP's Notepad starts without the status bar until View > Status Bar is chosen */
    Globals.bWrapLongLines = FALSE;
    ZeroMemory(&Globals.lfFont, sizeof(Globals.lfFont));
    Globals.lfFont.lfCharSet = DEFAULT_CHARSET;
    dwPointSize = 100;
    Globals.lfFont.lfWeight = FW_NORMAL;
    Globals.lfFont.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;

    LoadStringW(Globals.hInstance, STRING_DEFAULTFONT, Globals.lfFont.lfFaceName, ARRAYSIZE(Globals.lfFont.lfFaceName));
    get_text(cfg, "luxpad", "font", Globals.lfFont.lfFaceName, ARRAYSIZE(Globals.lfFont.lfFaceName));
    std::string size;
    if (get(cfg, "luxpad", "font_size", size) && atof(size.c_str()) > 0)
        dwPointSize = (DWORD)(atof(size.c_str()) * 10 + 0.5);
    LONG weight = Globals.lfFont.lfWeight;
    get_int(cfg, "luxpad", "font_weight", &weight);
    Globals.lfFont.lfWeight = weight;
    BOOL italic = FALSE;
    get_bool(cfg, "luxpad", "font_italic", &italic);
    Globals.lfFont.lfItalic = (BYTE)italic;
    get_bool(cfg, "luxpad", "wrap", &Globals.bWrapLongLines);
    get_bool(cfg, "luxpad", "statusbar", &Globals.bShowStatusBar);

    get_int(state, "window", "x", &x);
    get_int(state, "window", "y", &y);
    get_int(state, "window", "width", &cx);
    get_int(state, "window", "height", &cy);
    get_int(state, "window", "maximized", &maximized);
    get_text(state, "search", "find", Globals.szFindText, ARRAYSIZE(Globals.szFindText));
    get_text(state, "search", "replace", Globals.szReplaceText, ARRAYSIZE(Globals.szReplaceText));
    BOOL match_case = FALSE, whole_word = FALSE, down = TRUE;
    get_bool(state, "search", "match_case", &match_case);
    get_bool(state, "search", "whole_word", &whole_word);
    get_bool(state, "search", "down", &down);
    Globals.find.Flags = (match_case ? FR_MATCHCASE : 0) | (whole_word ? FR_WHOLEWORD : 0) | (down ? FR_DOWN : 0);

    pWP->length = sizeof(*pWP);
    pWP->flags = 0;
    pWP->showCmd = SW_SHOWNORMAL;
    if (maximized)
    {
        pWP->flags |= WPF_RESTORETOMAXIMIZED;
        pWP->showCmd = SW_SHOWMAXIMIZED;
    }
    if (cx <= 0 || cy <= 0 || x == CW_USEDEFAULT || y == CW_USEDEFAULT)
        x = y = cx = cy = 0;   /* the window manager places it */
    pWP->rcNormalPosition.left = x;
    pWP->rcNormalPosition.right = x + cx;
    pWP->rcNormalPosition.top = y;
    pWP->rcNormalPosition.bottom = y + cy;
    pWP->ptMaxPosition.x = x;
    pWP->ptMaxPosition.y = y;

    Globals.lfFont.lfHeight = HeightFromPointSize(dwPointSize);

    if (cfg) lhde_ini_free(cfg);
    if (state) lhde_ini_free(state);

    hFont = CreateFontIndirectW(&Globals.lfFont);
    SendMessageW(Globals.hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    if (hFont)
    {
        if (Globals.hFont)
            DeleteObject(Globals.hFont);
        Globals.hFont = hFont;
    }
}

/***********************************************************************
 *           NOTEPAD_SaveSettingsToRegistry
 *
 *  Save settings to luxpad.ini (config and state).
 */
void NOTEPAD_SaveSettingsToRegistry(void)
{
    WINDOWPLACEMENT wp;
    char num[64];

    wp.length = sizeof(wp);
    GetWindowPlacement(Globals.hMainWnd, &wp);

    std::string cpath = config_path(), spath = state_path();
    lhde_ini *cfg = lhde_ini_load(cpath.c_str());
    if (!cfg) cfg = lhde_ini_new();
    if (cfg)
    {
        lhde_ini_header(cfg, "luxpad's preferences (docs/USAGE.md \"luxpad\"), written when it exits");
        lhde_ini_set(cfg, "luxpad", "font", WuxUtf8(Globals.lfFont.lfFaceName).c_str());
        DWORD pt = PointSizeFromHeight(Globals.lfFont.lfHeight);
        snprintf(num, sizeof num, pt % 10 ? "%lu.%lu" : "%lu", (unsigned long)pt / 10, (unsigned long)pt % 10);
        lhde_ini_set(cfg, "luxpad", "font_size", num);
        snprintf(num, sizeof num, "%ld", (long)Globals.lfFont.lfWeight);
        lhde_ini_set(cfg, "luxpad", "font_weight", num);
        lhde_ini_set(cfg, "luxpad", "font_italic", Globals.lfFont.lfItalic ? "1" : "0");
        lhde_ini_set(cfg, "luxpad", "wrap", Globals.bWrapLongLines ? "1" : "0");
        lhde_ini_set(cfg, "luxpad", "statusbar", Globals.bShowStatusBar ? "1" : "0");
        lhde_ini_save(cfg, cpath.c_str());
        lhde_ini_free(cfg);
    }

    lhde_ini *state = lhde_ini_load(spath.c_str());
    if (!state) state = lhde_ini_new();
    if (state)
    {
        lhde_ini_header(state, "what luxpad remembers: where its window was, the last search");
        snprintf(num, sizeof num, "%ld", (long)wp.rcNormalPosition.left);
        lhde_ini_set(state, "window", "x", num);
        snprintf(num, sizeof num, "%ld", (long)wp.rcNormalPosition.top);
        lhde_ini_set(state, "window", "y", num);
        snprintf(num, sizeof num, "%ld", (long)(wp.rcNormalPosition.right - wp.rcNormalPosition.left));
        lhde_ini_set(state, "window", "width", num);
        snprintf(num, sizeof num, "%ld", (long)(wp.rcNormalPosition.bottom - wp.rcNormalPosition.top));
        lhde_ini_set(state, "window", "height", num);
        lhde_ini_set(state, "window", "maximized", (wp.flags & WPF_RESTORETOMAXIMIZED) || wp.showCmd == SW_SHOWMAXIMIZED ? "1" : "0");
        lhde_ini_set(state, "search", "find", WuxUtf8(Globals.szFindText).c_str());
        lhde_ini_set(state, "search", "replace", WuxUtf8(Globals.szReplaceText).c_str());
        lhde_ini_set(state, "search", "match_case", (Globals.find.Flags & FR_MATCHCASE) ? "1" : "0");
        lhde_ini_set(state, "search", "whole_word", (Globals.find.Flags & FR_WHOLEWORD) ? "1" : "0");
        lhde_ini_set(state, "search", "down", (Globals.find.Flags & FR_DOWN) ? "1" : "0");
        lhde_ini_save(state, spath.c_str());
        lhde_ini_free(state);
    }
}
