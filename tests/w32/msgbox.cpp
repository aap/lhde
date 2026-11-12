// MessageBoxW (Wine's msgbox.c in wux): shows one box and prints what it returned, for tests/headless/msgbox.sh to drive
// with xdotool. Usage: build/tests/w32/msgbox TYPE [CAPTION [TEXT]]
//   TYPE: ok | okcancel | yesno | yesnocancel | retrycancel | abortretryignore, with +def2 for MB_DEFBUTTON2, +owner for a
//   box owned by a window of the program, +a for MessageBoxA. CAPTION "-" gives a null caption ("Error").
// Prints "ret N" (IDOK 1, IDCANCEL 2, IDABORT 3, IDRETRY 4, IDIGNORE 5, IDYES 6, IDNO 7) and, read before it closed, the
// box's buttons left to right with their text and which one is the default ("buttons: &Yes* &No").
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>
#include "w32/w32.h"
#include "w32/user.h"

static std::string g_buttons;

static BOOL CALLBACK child(HWND h, LPARAM lp)
{
    WCHAR cls[32] = {}, text[64] = {};
    GetClassNameW(h, cls, 32);
    if (w32::utf8_of(cls) != "Button" || !IsWindowVisible(h)) return TRUE;
    GetWindowTextW(h, text, 64);
    RECT r;
    GetWindowRect(h, &r);
    bool def = (GetWindowLongW(h, GWL_STYLE) & 0x0f) == BS_DEFPUSHBUTTON;
    ((std::vector<std::pair<int, std::string>>*)lp)->push_back({ (int)r.left, w32::utf8_of(text) + (def ? "*" : "") });
    return TRUE;
}

// the box's buttons, read from a timer once it is up
static void CALLBACK look(HWND, UINT, UINT_PTR id, DWORD)
{
    HWND box = GetActiveWindow();
    if (!box) return;
    KillTimer(nullptr, id);
    std::vector<std::pair<int, std::string>> b;
    EnumChildWindows(box, child, (LPARAM)&b);
    std::sort(b.begin(), b.end());
    g_buttons = "buttons:";
    for (auto& x : b) g_buttons += " " + x.second;
    WCHAR cap[128] = {};
    GetWindowTextW(box, cap, 128);
    RECT wr, cr;
    GetWindowRect(box, &wr);
    GetClientRect(box, &cr);
    printf("%s\ncaption: %s\nclient: %dx%d\n", g_buttons.c_str(), w32::utf8_of(cap).c_str(), (int)cr.right, (int)cr.bottom);
    fflush(stdout);
}

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: msgbox TYPE [CAPTION [TEXT]]\n"); return 2; }
    std::string t = argv[1];
    UINT type = 0;
    std::string base = t.substr(0, t.find('+'));
    if (base == "ok") type = MB_OK | MB_ICONINFORMATION;
    else if (base == "okcancel") type = MB_OKCANCEL | MB_ICONEXCLAMATION;
    else if (base == "yesno") type = MB_YESNO | MB_ICONQUESTION;
    else if (base == "yesnocancel") type = MB_YESNOCANCEL | MB_ICONEXCLAMATION;
    else if (base == "retrycancel") type = MB_RETRYCANCEL | MB_ICONHAND;
    else if (base == "abortretryignore") type = MB_ABORTRETRYIGNORE | MB_ICONHAND;
    else { fprintf(stderr, "msgbox: unknown type %s\n", argv[1]); return 2; }
    if (t.find("+def2") != std::string::npos) type |= MB_DEFBUTTON2;
    const char* caption = argc > 2 ? argv[2] : "Message";
    if (!strcmp(caption, "-")) caption = nullptr;
    const char* text = argc > 3 ? argv[3] : "The quick brown fox jumps over the lazy dog.";
    w32::init();
    HWND owner = nullptr;
    if (t.find("+owner") != std::string::npos) {
        owner = CreateWindowExW(0, L"Static", L"msgbox owner", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 400, 300, nullptr, nullptr, nullptr, nullptr);
    }
    SetTimer(nullptr, 1, 100, look);
    int r;
    if (t.find("+a") != std::string::npos) r = MessageBoxA(owner, text, caption, type);
    else {
        std::u16string wt = w32::utf16_of(text), wc = caption ? w32::utf16_of(caption) : u"";
        r = MessageBoxW(owner, (LPCWSTR)wt.c_str(), caption ? (LPCWSTR)wc.c_str() : nullptr, type);
    }
    printf("ret %d\n", r);
    if (owner) DestroyWindow(owner);
    return 0;
}
