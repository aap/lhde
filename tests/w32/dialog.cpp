// Real dialog templates from Wine's comdlg32 resources, run
// through the vendored dialog manager and controls.
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string>
#include <string.h>
#include "res/res.h"
#include "w32/w32.h"
#include "w32/user.h"
#include "w32/kernel.h"
extern "C" {
#include "dlgs.h"
#include "cderr.h"
}

static int g_quit_ms;

static void CALLBACK quit_timer(HWND h, UINT, UINT_PTR, DWORD) { EndDialog(h, IDCANCEL); }

static INT_PTR CALLBACK dlg_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        // populate whatever combos/lists exist so the dialog looks alive
        const wchar_t* fonts[] = { L"Tahoma", L"Microsoft Sans Serif", L"Courier New", L"Arial", L"Times New Roman", L"Verdana" };
        const wchar_t* styles[] = { L"Regular", L"Italic", L"Bold", L"Bold Italic" };
        const wchar_t* sizes[] = { L"8", L"9", L"10", L"11", L"12", L"14", L"16", L"18", L"20", L"22", L"24" };
        HWND c;
        if ((c = GetDlgItem(h, cmb1))) { for (auto f : fonts) SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)f); SendMessageW(c, CB_SETCURSEL, 0, 0); }
        if ((c = GetDlgItem(h, cmb2))) { for (auto f : styles) SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)f); SendMessageW(c, CB_SETCURSEL, 0, 0); }
        if ((c = GetDlgItem(h, cmb3))) { for (auto f : sizes) SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)f); SendMessageW(c, CB_SETCURSEL, 0, 0); }
        if ((c = GetDlgItem(h, cmb4))) { SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"Black"); SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"Maroon"); SendMessageW(c, CB_SETCURSEL, 0, 0); }
        if ((c = GetDlgItem(h, cmb5))) { SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"Western"); SendMessageW(c, CB_SETCURSEL, 0, 0); }
        if ((c = GetDlgItem(h, lst1))) { for (auto f : fonts) SendMessageW(c, LB_ADDSTRING, 0, (LPARAM)f); SendMessageW(c, LB_SETCURSEL, 1, 0); }
        if ((c = GetDlgItem(h, lst2))) { for (auto f : sizes) SendMessageW(c, LB_ADDSTRING, 0, (LPARAM)f); }
        if ((c = GetDlgItem(h, edt1))) SetWindowTextW(c, L"*.txt");
        if (g_quit_ms) SetTimer(h, 77, g_quit_ms, quit_timer);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) { EndDialog(h, LOWORD(wp)); return TRUE; }
        return FALSE;
    }
    return FALSE;
}

// <the test's directory>/REL (build/tests/w32/<test> -> the source tree with "../../../")
static std::string beside_exe(const char* rel)
{
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return rel;
    exe[n] = 0;
    std::string p = exe;
    return p.substr(0, p.rfind('/') + 1) + rel;
}

int main(int argc, char** argv)
{
    std::string deflt = beside_exe("../../../wux/rc/comdlg32.res");
    const char* resfile = argc > 1 ? argv[1] : deflt.c_str();
    const char* name = argc > 2 ? argv[2] : "CHOOSE_FONT";
    g_quit_ms = argc > 3 ? atoi(argv[3]) : 0;
    w32::init();
    HINSTANCE inst = LoadLibraryExW((LPCWSTR)w32::utf16_of(resfile).c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (!inst) { fprintf(stderr, "cannot load %s\n", resfile); return 1; }
    std::u16string n16 = w32::utf16_of(name);
    LPCWSTR id = (name[0] >= '0' && name[0] <= '9') ? MAKEINTRESOURCEW(atoi(name)) : (LPCWSTR)n16.c_str();
    INT_PTR r = DialogBoxParamW(inst, id, nullptr, dlg_proc, 0);
    fprintf(stderr, "dialog returned %ld\n", (long)r);
    return r == -1 ? 1 : 0;
}
