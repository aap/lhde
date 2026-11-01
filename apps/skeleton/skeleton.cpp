// skeleton: the smallest lhde application, the one to copy and rename (docs/APPLICATIONS.md). A window with a menu, a
// themed button, a status bar, the Open dialog and an About box; built with apps/app.mk's recipe against the two public
// headers only. Everything a session gives a program (the visual style, the fonts, following a theme switch) comes with
// WuxInit.
#include <string>
#include "wux/wux.h"
#include "shell/shell.h"
#include "resource.h"

WUX_RESOURCES(skeleton_res, "apps/skeleton/skeleton.res");

enum { ID_OPEN = 1, ID_EXIT, ID_ABOUT, ID_BUTTON, ID_STATUS };

static HINSTANCE g_res;
static HWND g_button, g_status;

static INT_PTR CALLBACK about_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM)
{
    switch (msg) {
    case WM_INITDIALOG:
        SendDlgItemMessageW(dlg, IDC_ABOUT_ICON, STM_SETICON, (WPARAM)LoadIconW(nullptr, (LPCWSTR)IDI_INFORMATION), 0);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) EndDialog(dlg, LOWORD(wp));
        return TRUE;
    }
    return FALSE;
}

static void open_file(HWND hwnd)
{
    WCHAR file[1024] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"All Files\0*.*\0Text Files\0*.txt\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof file / sizeof file[0];
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return;
    SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)file);   // the shell layer would take it from here (shell::open_files, ...)
}

static LRESULT CALLBACK frame_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        HMENU bar = CreateMenu(), file = CreatePopupMenu(), help = CreatePopupMenu();
        AppendMenuW(file, MF_STRING, ID_OPEN, L"&Open...");
        AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(file, MF_STRING, ID_EXIT, L"E&xit");
        AppendMenuW(help, MF_STRING, ID_ABOUT, L"&About Skeleton");
        AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
        AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Help");
        SetMenu(hwnd, bar);
        g_button = CreateWindowExW(0, L"BUTTON", L"&Open...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                   11, 11, 75, 23, hwnd, (HMENU)ID_BUTTON, nullptr, nullptr);
        SendMessageW(g_button, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), FALSE);
        g_status = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, hwnd,
                                   (HMENU)ID_STATUS, nullptr, nullptr);
        std::u16string home = WuxWide("Home: " + shell::home_dir());   // UTF-8 from the shell layer, UTF-16 for the API
        SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)home.c_str());
        return 0;
    }
    case WM_SIZE:
        SendMessageW(g_status, WM_SIZE, 0, 0);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_OPEN: case ID_BUTTON: open_file(hwnd); break;
        case ID_ABOUT: DialogBoxParamW(g_res, MAKEINTRESOURCEW(IDD_ABOUT), hwnd, about_proc, 0); break;
        case ID_EXIT: DestroyWindow(hwnd); break;
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int main()
{
    WuxInit();
    g_res = WuxLoadResources(skeleton_res_start, (size_t)(skeleton_res_end - skeleton_res_start));
    WNDCLASSW wc = {};
    wc.lpfnWndProc = frame_proc;
    wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"lhde_skeleton";
    RegisterClassW(&wc);
    HWND frame = CreateWindowExW(0, L"lhde_skeleton", L"Skeleton", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 400, 260,
                                 nullptr, nullptr, nullptr, nullptr);
    ShowWindow(frame, SW_SHOW);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (IsDialogMessageW(frame, &msg)) continue;   // Tab and the mnemonics between the frame's controls
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
