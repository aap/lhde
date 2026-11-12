// Text into the vendored Edit control as Win32 delivers it: WM_CHAR carries one
// UTF-16 code unit, so a character above U+FFFF arrives as two WM_CHARs (the high
// surrogate, then the low one) and the edit stores the pair.
//   edittext --self        sends the WM_CHARs itself and checks WM_GETTEXT (prints ok/FAIL lines, exit 1 on a failure)
//   edittext TITLE         a window with a multiline edit that prints its text as UTF-8 hex on every change, one line
//                          ("text 61c3a9f09f9880"), flushed; tests/headless/typing.sh types into it with xdotool
// Both need $DISPLAY.
#include <stdio.h>
#include <string.h>
#include <string>
#include "w32/w32.h"
#include "w32/user.h"

static HWND g_edit;

static std::string edit_utf8(HWND e)
{
    int n = GetWindowTextLengthW(e);
    std::u16string w((size_t)n + 1, 0);
    GetWindowTextW(e, (LPWSTR)&w[0], n + 1);
    w.resize((size_t)n);
    return w32::utf8_of(w);
}

static std::string hex(const std::string& s)
{
    std::string o;
    char b[3];
    for (unsigned char c : s) { snprintf(b, sizeof b, "%02x", c); o += b; }
    return o;
}

static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND:
        if (HIWORD(wp) == EN_CHANGE && g_edit) { printf("text %s\n", hex(edit_utf8(g_edit)).c_str()); fflush(stdout); }
        return 0;
    case WM_SETFOCUS: SetFocus(g_edit); return 0;
    case WM_SIZE: MoveWindow(g_edit, 0, 0, LOWORD(lp), HIWORD(lp), TRUE); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static int fails;
static void check(bool ok, const char* what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

static int self_test(HWND parent)
{
    HWND e = CreateWindowExW(0, L"Edit", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE, 0, 0, 200, 100, parent, nullptr, nullptr, nullptr);
    // a, é, then U+1F600 as Windows sends it: two WM_CHARs, high surrogate first
    for (WPARAM c : {(WPARAM)'a', (WPARAM)0xe9, (WPARAM)0xd83d, (WPARAM)0xde00}) SendMessageW(e, WM_CHAR, c, 1);
    check(GetWindowTextLengthW(e) == 4, "four UTF-16 units: a, e-acute, the surrogate pair");
    WCHAR buf[16] = {0};
    GetWindowTextW(e, buf, 16);
    check(buf[2] == 0xd83d && buf[3] == 0xde00, "the pair stored in order");
    check(edit_utf8(e) == "a\xc3\xa9\xf0\x9f\x98\x80", "WM_GETTEXT as UTF-8: 61 c3a9 f09f9880");
    // WM_UNICHAR takes a code point (the edit splits it itself)
    SendMessageW(e, WM_UNICHAR, 0x1d11e, 1);
    check(edit_utf8(e) == "a\xc3\xa9\xf0\x9f\x98\x80\xf0\x9d\x84\x9e", "WM_UNICHAR U+1D11E");
    // Backspace takes the whole pair (the caret never stops between its halves)
    SendMessageW(e, WM_CHAR, VK_BACK, 1);
    check(edit_utf8(e) == "a\xc3\xa9\xf0\x9f\x98\x80", "Backspace removes the pair");
    DestroyWindow(e);
    printf("%d failure(s)\n", fails);
    return fails != 0;
}

int main(int argc, char** argv)
{
    w32::init();
    WNDCLASSW wc = {};
    wc.lpfnWndProc = proc;
    wc.lpszClassName = L"edittext";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    bool self = argc > 1 && !strcmp(argv[1], "--self");
    std::u16string title = w32::utf16_of(argc > 1 && !self ? argv[1] : "edittext");
    HWND w = CreateWindowExW(0, L"edittext", (LPCWSTR)title.c_str(), WS_OVERLAPPEDWINDOW | (self ? 0 : WS_VISIBLE), 0, 0, 300, 200,
                             nullptr, nullptr, nullptr, nullptr);
    if (!w) { fprintf(stderr, "edittext: no window\n"); return 2; }
    if (self) return self_test(w);
    g_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"Edit", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL, 0, 0, 300, 200,
                             w, nullptr, nullptr, nullptr);
    SetFocus(g_edit);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    return 0;
}
