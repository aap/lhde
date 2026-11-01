// glassframe: the flxp port of lhwm's reference client examples/extendframe.c
// (docs/CLIENT-PROTOCOL.md), written as a plain win32 program against the
// shim's dwmapi: a 640x420 window whose top 60 px are the window manager's
// glass (DwmExtendFrameIntoClientArea + NO_TITLE), holding a big folder
// title, an icon and a fake breadcrumb with TextGlow, above an opaque white
// body. The header acts as a caption (WM_NCHITTEST -> HTCAPTION: drag moves,
// double-click maximises), the right edge resizes (HTRIGHT). Without
// composition the header is painted opaque, Slate style.
//
//   build/w32test/glassframe [quit_ms]      Escape or q quits
//   FLXP_GLASS_ICON=file.ico                another 32x32 icon
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include "w32/w32.h"
#include "w32/user.h"
extern "C" {
#include "w32/flxp_dwmapi.h"
}

enum { HEADER = 60, EDGE = 6 };

static HFONT g_title, g_crumb, g_arrow, g_body;
static HICON g_icon;
static const COLORREF ink = RGB(26, 31, 77);      // extendframe's (0.10, 0.12, 0.30)

static HFONT make_font(int pt, int weight)
{
    LOGFONTW lf = {};
    lf.lfHeight = -MulDiv(pt, 96, 72);
    lf.lfWeight = weight;
    lf.lfQuality = ANTIALIASED_QUALITY;       // pango antialiases the reference too
    lstrcpyW(lf.lfFaceName, L"Tahoma");       // extendframe asks for "Segoe UI, Tahoma, Sans": Tahoma here
    return CreateFontIndirectW(&lf);
}

static void text(HDC dc, HFONT f, const wchar_t* s, int x, int y, bool glow)
{
    SelectObject(dc, f);
    RECT r = { x, y, x + 1000, y + 100 };
    FlxpDrawGlowTextW(dc, s, -1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX, glow ? 3 : 0, RGB(255, 255, 255));
}

static void paint(HWND h, HDC dc)
{
    RECT rc; GetClientRect(h, &rc);
    BOOL composited = FALSE;
    DwmIsCompositionEnabled(&composited);
    RECT head = { 0, 0, rc.right, HEADER };
    if (composited) {
        // transparent black: lhwm's plate shows through wherever we draw nothing
        FlxpClearRect(dc, &head);
    } else {
        // the X server would show the transparent pixels black: paint the 4074 Slate Explorer's own header
        TRIVERTEX v[2] = { { 0, 0, 0xed00, 0xf200, 0xfa00, 0xff00 }, { rc.right, HEADER, 0xc700, 0xd600, 0xeb00, 0xff00 } };
        GRADIENT_RECT g = { 0, 1 };
        GradientFill(dc, v, 2, &g, 1, GRADIENT_FILL_RECT_V);
    }
    // the icon, drawn over the glass with its alpha
    if (g_icon) {
        DrawIconEx(dc, 18, 14, g_icon, 32, 32, 0, nullptr, DI_NORMAL);
    } else {
        // placeholder: a folder-ish rounded square, as extendframe's
        HBRUSH b = CreateSolidBrush(RGB(240, 200, 90));
        HPEN p = CreatePen(PS_SOLID, 1, RGB(115, 77, 13));
        HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
        RoundRect(dc, 14, 10, 54, 50, 12, 12);
        SelectObject(dc, ob); SelectObject(dc, op);
        DeleteObject(b); DeleteObject(p);
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ink);
    text(dc, g_title, L"Documents", 64, 4, composited);
    text(dc, g_crumb, L"Computer", 78, 37, composited);
    text(dc, g_arrow, L"\x25ba", 148, 40, composited);
    text(dc, g_crumb, L"Documents", 166, 37, composited);
    // the body: ordinary opaque content
    RECT body = { 0, HEADER, rc.right, rc.bottom };
    FillRect(dc, &body, (HBRUSH)GetStockObject(WHITE_BRUSH));
    const wchar_t* lines[] = {
        L"The header above is lhwm's frame plate, extended 60 px into this window.",
        L"Drag the header to move, double-click it to maximise or restore,",
        L"drag the right edge to resize. Escape or q quits.",
        composited ? L"Composited: the header is glass (_LHWM_COMPOSITED = 1)."
                   : L"Not composited: the header is painted opaque (_LHWM_COMPOSITED = 0).",
    };
    for (int i = 0; i < 4; i++) text(dc, g_body, lines[i], 16, HEADER + 14 + i * 20, false);
}

static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        // rule 1+2 of the protocol: ARGB toplevel (done by the extension) and the
        // properties before mapping; lhwm draws no caption text
        MARGINS m = { 0, 0, HEADER, 0 };
        HRESULT hr = DwmExtendFrameIntoClientArea(h, &m);
        DWORD flags = FLXP_DWMFF_NO_TITLE;
        DwmSetWindowAttribute(h, DWMWA_FLXP_FRAME_FLAGS, &flags, sizeof flags);
        BOOL on = FALSE; DwmIsCompositionEnabled(&on);
        fprintf(stderr, "glassframe: DwmExtendFrameIntoClientArea 0x%08lx, composited %d\n", (unsigned long)hr, on);
        return 0;
    }
    case WM_NCHITTEST: {
        // the header is a caption, the right edge a sizing border; the WM does the rest
        POINT p = { (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
        ScreenToClient(h, &p);
        RECT rc; GetClientRect(h, &rc);
        if (p.x >= rc.right - EDGE && p.y >= 0 && p.y < rc.bottom) return HTRIGHT;
        if (p.y >= 0 && p.y < HEADER) return HTCAPTION;
        return DefWindowProcW(h, msg, wp, lp);
    }
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, (LPCWSTR)(LOWORD(lp) == HTRIGHT ? IDC_SIZEWE : IDC_ARROW)));
        return TRUE;
    case WM_DWMCOMPOSITIONCHANGED:         // compositor or theme mode switched: glass or opaque header
    case WM_DWMCOLORIZATIONCOLORCHANGED:   // another frame theme
        InvalidateRect(h, nullptr, TRUE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        paint(h, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE || wp == 'Q') DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void CALLBACK quit_timer(HWND h, UINT, UINT_PTR, DWORD) { DestroyWindow(h); }

static HICON load_icon()
{
    std::string path;
    if (const char* e = getenv("FLXP_GLASS_ICON")) path = e;
    else {
        char exe[4096];
        ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
        if (n <= 0) return nullptr;
        exe[n] = 0;
        path = exe;
        path = path.substr(0, path.rfind('/')) + "/../../../flxp/res/icons/mydocs.ico";
    }
    std::u16string w = w32::utf16_of(path.c_str());
    return (HICON)LoadImageW(nullptr, (LPCWSTR)w.c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE);
}

int main(int argc, char** argv)
{
    w32::init();
    g_title = make_font(15, FW_NORMAL);
    g_crumb = make_font(9, FW_NORMAL);
    g_arrow = make_font(6, FW_NORMAL);
    g_body = make_font(9, FW_NORMAL);
    g_icon = load_icon();
    WNDCLASSW wc = {};
    wc.lpfnWndProc = proc;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"glassframe";
    RegisterClassW(&wc);
    // the title still matters (taskbar, Alt+Tab) although lhwm does not draw it
    HWND h = CreateWindowExW(0, L"glassframe", L"Documents", WS_OVERLAPPEDWINDOW, 120, 90, 640, 420,
                             nullptr, nullptr, nullptr, nullptr);
    if (!h) { fprintf(stderr, "glassframe: no window\n"); return 1; }
    ShowWindow(h, SW_SHOW);
    if (argc > 1) SetTimer(h, 1, atoi(argv[1]), quit_timer);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
