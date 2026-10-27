// Dialog manager basics against win32 behaviour, driven from outside with
// xdotool (NOTES.md, Status: "win32 dialog behaviour"):
// - child z-order = creation order (GW_CHILD/GW_HWNDNEXT), so Tab runs in
//   template order;
// - Alt+letter mnemonics (WM_SYSCHAR -> IsDialogMessage);
// - AdjustWindowRectEx gives exactly the requested client size for a
//   WM-framed window, and the dialog's client is the template's DLU size;
// - a modal dialog keeps the focus when its disabled owner is activated.
// Everything is logged to stderr; build/w32test/dlgkeys [quit_ms].
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "w32/w32.h"
#include "w32/user.h"
#include "w32/kernel.h"

enum { ID_GROUP = 100, ID_LABEL, ID_EDIT, ID_CHECK, ID_APPLY };
static const int DLG_CX = 200, DLG_CY = 100;

static int g_quit_ms, g_applied;
static HWND g_owner, g_dlg;

// ---- an in-memory DLGTEMPLATE ----

struct Tmpl {
    std::vector<WORD> w;
    void word(WORD v) { w.push_back(v); }
    void dword(DWORD v) { word(LOWORD(v)); word(HIWORD(v)); }
    void str(const wchar_t* s) { for (; *s; s++) word((WORD)*s); word(0); }
    void align() { if (w.size() & 1) word(0); }
    void item(DWORD style, short x, short y, short cx, short cy, WORD id, WORD cls, const wchar_t* text)
    {
        align();
        dword(style | WS_CHILD | WS_VISIBLE); dword(0);
        word(x); word(y); word(cx); word(cy); word(id);
        word(0xffff); word(cls);
        str(text);
        word(0);
    }
};

static std::vector<WORD> make_template()
{
    Tmpl t;
    t.dword(DS_MODALFRAME | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU); t.dword(0);
    t.word(7); t.word(0); t.word(0); t.word(DLG_CX); t.word(DLG_CY);
    t.word(0); t.word(0); t.str(L"Dialog keys");
    t.word(8); t.str(L"MS Shell Dlg");
    t.item(BS_GROUPBOX, 7, 4, 186, 62, ID_GROUP, 0x80, L"Options");
    t.item(SS_LEFT, 14, 18, 30, 8, ID_LABEL, 0x82, L"&Name:");
    t.item(WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 48, 16, 136, 12, ID_EDIT, 0x81, L"");
    t.item(WS_TABSTOP | BS_AUTOCHECKBOX, 14, 34, 80, 10, ID_CHECK, 0x80, L"&Check me");
    t.item(WS_TABSTOP | BS_PUSHBUTTON, 134, 46, 50, 14, ID_APPLY, 0x80, L"&Apply");
    t.item(WS_TABSTOP | BS_DEFPUSHBUTTON, 88, 78, 50, 14, IDOK, 0x80, L"OK");
    t.item(WS_TABSTOP | BS_PUSHBUTTON, 143, 78, 50, 14, IDCANCEL, 0x80, L"Cancel");
    return t.w;
}

// ---- checks ----

static const char* who(HWND h)
{
    static char buf[32];
    if (!h) return "none";
    if (h == g_dlg) return "dialog";
    if (h == g_owner) return "owner";
    snprintf(buf, sizeof buf, "%d", GetDlgCtrlID(h));
    return buf;
}

static void check_zorder(HWND dlg)
{
    fprintf(stderr, "zorder:");
    for (HWND c = GetWindow(dlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) fprintf(stderr, " %d", GetDlgCtrlID(c));
    fprintf(stderr, "\n");
}

static void check_client(HWND dlg)
{
    RECT want = { 0, 0, DLG_CX, DLG_CY }, cr;
    MapDialogRect(dlg, &want);
    GetClientRect(dlg, &cr);
    fprintf(stderr, "dialog client %ldx%ld want %ldx%ld %s\n", cr.right, cr.bottom, want.right, want.bottom,
            cr.right == want.right && cr.bottom == want.bottom ? "ok" : "WRONG");
}

static void check_adjust()
{
    struct { DWORD style, ex; bool menu; } cases[] = {
        { WS_OVERLAPPEDWINDOW, 0, false },
        { WS_OVERLAPPEDWINDOW, WS_EX_CLIENTEDGE, false },
        { WS_OVERLAPPEDWINDOW, 0, true },
        { WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, WS_EX_DLGMODALFRAME, false },
    };
    for (auto& c : cases) {
        RECT r = { 0, 0, 300, 200 };
        AdjustWindowRectEx(&r, c.style, c.menu, c.ex);
        HMENU m = nullptr;
        if (c.menu) { m = CreateMenu(); AppendMenuW(m, MF_STRING, 1, L"&File"); }
        HWND h = CreateWindowExW(c.ex, L"dlgkeys_owner", L"adjust", c.style, 10, 10, r.right - r.left, r.bottom - r.top, nullptr, m, nullptr, nullptr);
        RECT cr; GetClientRect(h, &cr);
        fprintf(stderr, "adjust style %08lx ex %08lx menu %d: client %ldx%ld %s\n", c.style, c.ex, c.menu, cr.right, cr.bottom,
                cr.right == 300 && cr.bottom == 200 ? "ok" : "WRONG");
        DestroyWindow(h);
    }
}

// ---- the dialog ----

static void CALLBACK poll(HWND, UINT, UINT_PTR, DWORD)
{
    static HWND last_focus = (HWND)-1, last_active = (HWND)-1;
    HWND f = GetFocus(), a = GetActiveWindow();
    if (f == last_focus && a == last_active) return;
    last_focus = f; last_active = a;
    fprintf(stderr, "focus %s", who(f));
    fprintf(stderr, " active %s\n", who(a));
}

static void CALLBACK quit_timer(HWND h, UINT, UINT_PTR, DWORD) { EndDialog(h, IDCANCEL); }

static INT_PTR CALLBACK dlg_proc(HWND h, UINT msg, WPARAM wp, LPARAM)
{
    switch (msg) {
    case WM_INITDIALOG:
        g_dlg = h;
        check_zorder(h);
        check_client(h);
        SetTimer(h, 1, 50, poll);
        if (g_quit_ms) SetTimer(h, 2, g_quit_ms, quit_timer);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_APPLY: fprintf(stderr, "apply %d\n", ++g_applied); return TRUE;
        case ID_CHECK: fprintf(stderr, "check %d\n", (int)IsDlgButtonChecked(h, ID_CHECK)); return TRUE;
        case IDOK: case IDCANCEL: {
            WCHAR text[64]; GetDlgItemTextW(h, ID_EDIT, text, 64);
            fprintf(stderr, "name \"%s\"\n", w32::utf8_of(text).c_str());
            EndDialog(h, LOWORD(wp));
            return TRUE;
        }
        }
        return FALSE;
    }
    return FALSE;
}

static LRESULT CALLBACK owner_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_SETFOCUS && h == g_owner) fprintf(stderr, "owner WM_SETFOCUS%s\n", IsWindowEnabled(h) ? "" : " while disabled");
    return DefWindowProcW(h, msg, wp, lp);
}

int main(int argc, char** argv)
{
    g_quit_ms = argc > 1 ? atoi(argv[1]) : 0;
    w32::init();
    WNDCLASSW wc = {};
    wc.lpfnWndProc = owner_proc;
    wc.hbrBackground = (HBRUSH)(COLOR_APPWORKSPACE + 1);
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"dlgkeys_owner";
    RegisterClassW(&wc);
    check_adjust();
    g_owner = CreateWindowExW(0, L"dlgkeys_owner", L"dlgkeys owner", WS_OVERLAPPEDWINDOW, 20, 20, 600, 400, nullptr, nullptr, nullptr, nullptr);
    ShowWindow(g_owner, SW_SHOW);
    UpdateWindow(g_owner);
    std::vector<WORD> t = make_template();
    INT_PTR r = DialogBoxIndirectParamW(nullptr, (LPCDLGTEMPLATEW)t.data(), g_owner, dlg_proc, 0);
    fprintf(stderr, "dialog returned %ld, applied %d\n", (long)r, g_applied);
    DestroyWindow(g_owner);
    return 0;
}
