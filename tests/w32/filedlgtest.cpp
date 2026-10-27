// GetOpenFileNameW / GetSaveFileNameW / GetOpenFileNameA through the shim
// (src/w32/comdlg.cpp -> src/filedlg). An owner window is shown first so the
// dialog is owned (transient-for, disabled owner) as in a real program.
//   build/w32test/filedlgtest [open|save|multi|ansi] [initial-dir]
// Prints the result fields; exit 0 = chosen, 1 = cancelled, 2 = error.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "w32/w32.h"
#include "w32/user.h"
extern "C" {
#include "commdlg.h"
#include "cderr.h"
}

static LRESULT CALLBACK owner_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        SetBkMode(dc, TRANSPARENT);
        TextOutW(dc, 10, 10, L"filedlgtest: owner window", 25);
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int main(int argc, char** argv)
{
    const char* what = argc > 1 ? argv[1] : "open";
    const char* dir = argc > 2 ? argv[2] : nullptr;
    w32::init();
    WNDCLASSW wc = {};
    wc.lpfnWndProc = owner_proc;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"filedlgtest";
    RegisterClassW(&wc);
    HWND owner = CreateWindowExW(0, L"filedlgtest", L"filedlgtest", WS_OVERLAPPEDWINDOW, 40, 40, 400, 200, nullptr, nullptr, nullptr, nullptr);
    ShowWindow(owner, SW_SHOW);
    UpdateWindow(owner);

    static const WCHAR filter[] = L"Text Documents (*.txt)\0*.txt\0C/C++ Sources\0*.c;*.cpp;*.h\0All Files (*.*)\0*.*\0";
    BOOL ok;
    if (!strcmp(what, "ansi")) {
        char file[1024] = "";
        char title[256] = "";
        OPENFILENAMEA a = {};
        a.lStructSize = sizeof a;
        a.hwndOwner = owner;
        a.lpstrFilter = "Markdown\0*.md\0All Files\0*.*\0";
        a.nFilterIndex = 1;
        a.lpstrFile = file; a.nMaxFile = sizeof file;
        a.lpstrFileTitle = title; a.nMaxFileTitle = sizeof title;
        a.lpstrInitialDir = dir;
        a.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
        ok = GetOpenFileNameA(&a);
        if (!ok) { printf("cancelled, CommDlgExtendedError %lu\n", (unsigned long)CommDlgExtendedError()); return CommDlgExtendedError() ? 2 : 1; }
        printf("file %s\ntitle %s\noffset %u ext %u filter %lu\n", file, title, a.nFileOffset, a.nFileExtension, (unsigned long)a.nFilterIndex);
        return 0;
    }
    WCHAR file[4096] = L"";
    OPENFILENAMEW o = {};
    o.lStructSize = sizeof o;
    o.hwndOwner = owner;
    o.lpstrFilter = filter;
    o.nFilterIndex = 3;
    o.lpstrFile = file;
    o.nMaxFile = sizeof file / sizeof file[0];
    std::u16string d16 = dir ? w32::utf16_of(dir) : std::u16string();
    o.lpstrInitialDir = dir ? (LPCWSTR)d16.c_str() : nullptr;
    o.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!strcmp(what, "save")) {
        wcscpy(file, L"Untitled.txt");
        o.lpstrDefExt = L"txt";
        o.nFilterIndex = 1;
        o.Flags |= OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        ok = GetSaveFileNameW(&o);
    } else {
        o.Flags |= OFN_FILEMUSTEXIST;
        if (!strcmp(what, "multi")) o.Flags |= OFN_ALLOWMULTISELECT;
        ok = GetOpenFileNameW(&o);
    }
    if (!ok) { printf("cancelled, CommDlgExtendedError %lu\n", (unsigned long)CommDlgExtendedError()); return CommDlgExtendedError() ? 2 : 1; }
    printf("filter %lu offset %u ext %u flags %lx\n", (unsigned long)o.nFilterIndex, o.nFileOffset, o.nFileExtension, (unsigned long)o.Flags);
    const WCHAR* p = file;
    if (o.nFileOffset && !file[o.nFileOffset - 1]) {   // multi-select list
        printf("dir %s\n", w32::utf8_of(p).c_str());
        for (p += o.nFileOffset; *p; p += w32::utf16_len(p) + 1) printf("name %s\n", w32::utf8_of(p).c_str());
    } else {
        printf("file %s\n", w32::utf8_of(file).c_str());
    }
    return 0;
}
