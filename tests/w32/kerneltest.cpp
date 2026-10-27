// Tests for the kernel/CRT/registry/debug/usp10 parts of the win32 shim.
// Usage: kerneltest [dir-with-test.res-and-comdlg32.res] [scratch-dir]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include "w32/w32.h"
extern "C" {
#include "winnls.h"
#include "winreg.h"
#include "usp10.h"
#include "imm.h"
#include "wine/debug.h"
#include "wine/exception.h"
}
#include "w32/kernel.h"

static int failures, passes;

static void check(bool ok, const char* what)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) passes++; else failures++;
}

typedef const flxp_wch* W;
#define WS(s) ((W)L##s)

static bool weq(const WCHAR* a, const wchar_t* b) { return a && !flxp_wcscmp((W)a, (W)b); }
static std::string u8(const WCHAR* s, int n = -1) { return w32::utf8_of(s, n); }

// ---- fake GDI for the usp10 test; real implementations override these ----

static bool fake_gdi_used;
static const int FAKE_DC = 0x5ca1ab1e;
static int fake_width(WCHAR c) { return c == 'i' ? 3 : c == 'W' ? 11 : 7; }
struct TextCall { int x, y; UINT opts; RECT rc; bool has_rc; std::u16string s; std::vector<int> dx; COLORREF fg; };
static std::vector<TextCall> text_calls;
static COLORREF fake_fg = RGB(0, 0, 0), fake_bg = RGB(255, 255, 255);
static int fake_bkmode = TRANSPARENT;

extern "C" {
__attribute__((weak)) BOOL WINAPI GetTextExtentExPointW(HDC hdc, LPCWSTR s, INT n, INT, LPINT, LPINT dx, LPSIZE sz)
{
    if (hdc != (HDC)(uintptr_t)FAKE_DC) return FALSE;
    fake_gdi_used = true;
    int x = 0;
    for (int i = 0; i < n; i++) { x += fake_width(s[i]); if (dx) dx[i] = x; }
    sz->cx = x; sz->cy = 13;
    return TRUE;
}
__attribute__((weak)) BOOL WINAPI GetCharWidthW(HDC, UINT, UINT, LPINT) { return FALSE; }
__attribute__((weak)) BOOL WINAPI GetTextMetricsW(HDC hdc, LPTEXTMETRICW tm)
{
    fake_gdi_used = true;
    if (hdc != (HDC)(uintptr_t)FAKE_DC) return FALSE;
    memset(tm, 0, sizeof *tm);
    tm->tmHeight = 13; tm->tmAscent = 11; tm->tmDescent = 2; tm->tmAveCharWidth = 7;
    return TRUE;
}
__attribute__((weak)) BOOL WINAPI ExtTextOutW(HDC, INT x, INT y, UINT opts, const RECT* rc, LPCWSTR s, UINT n, const INT* dx)
{
    TextCall c{x, y, opts, {}, rc != nullptr, std::u16string((const char16_t*)s, n), {}, fake_fg};
    if (rc) c.rc = *rc;
    if (dx) c.dx.assign(dx, dx + n);
    text_calls.push_back(c);
    return TRUE;
}
__attribute__((weak)) COLORREF WINAPI SetTextColor(HDC, COLORREF c) { COLORREF o = fake_fg; fake_fg = c; return o; }
__attribute__((weak)) COLORREF WINAPI SetBkColor(HDC, COLORREF c) { COLORREF o = fake_bg; fake_bg = c; return o; }
__attribute__((weak)) INT WINAPI SetBkMode(HDC, INT m) { int o = fake_bkmode; fake_bkmode = m; return o; }
__attribute__((weak)) DWORD WINAPI GetSysColor(INT i) { return i == COLOR_HIGHLIGHTTEXT ? RGB(255, 255, 255) : i == COLOR_HIGHLIGHT ? RGB(10, 36, 106) : 0; }
}

// ---- tests ----

static void test_crt()
{
    check(flxp_wcslen(WS("hello")) == 5, "wcslen");
    flxp_wch buf[256];
    flxp_wcscpy(buf, WS("foo"));
    flxp_wcscat(buf, WS("Bar"));
    check(weq((WCHAR*)buf, L"fooBar"), "wcscpy/wcscat");
    check(flxp_wcscmp(WS("abc"), WS("abd")) < 0 && flxp_wcscmp(WS("b"), WS("a")) > 0 && !flxp_wcscmp(WS("x"), WS("x")), "wcscmp");
    check(!flxp_wcsicmp(WS("HeLLo"), WS("hello")) && flxp_wcsicmp(WS("a"), WS("B")) < 0, "wcsicmp");
    check(!flxp_wcsnicmp(WS("ABCx"), WS("abcy"), 3) && flxp_wcsnicmp(WS("ABCx"), WS("abcy"), 4), "wcsnicmp");
    check(!flxp_wcsicmp(WS("ÄRGER"), WS("ärger")), "wcsicmp Latin-1");
    check(!flxp_wcsncmp(WS("abcX"), WS("abcY"), 3), "wcsncmp");
    W s = WS("a,b,c");
    check(flxp_wcschr(s, ',') == s + 1 && flxp_wcsrchr(s, ',') == s + 3 && !flxp_wcschr(s, 'z'), "wcschr/wcsrchr");
    check(flxp_wcsstr(WS("hello world"), WS("wor")) && flxp_wcsstr(WS("hello world"), WS("wor"))[0] == 'w' && !flxp_wcsstr(WS("abc"), WS("abd")), "wcsstr");
    flxp_wch* d = flxp_wcsdup(WS("dup"));
    check(d && weq((WCHAR*)d, L"dup"), "wcsdup");
    free(d);
    flxp_wch* end;
    check(flxp_wcstol(WS("  -42xyz"), &end, 10) == -42 && *end == 'x', "wcstol");
    check(flxp_wcstol(WS("0x1F"), nullptr, 0) == 31 && flxp_wcstol(WS("017"), nullptr, 0) == 15, "wcstol base 0");
    check(flxp_wcstoul(WS("ffffffff"), nullptr, 16) == 0xffffffffUL, "wcstoul");
    check(flxp_wtoi(WS("123abc")) == 123, "_wtoi");
    check(flxp_towupper('a') == 'A' && flxp_towupper(0xe9) == 0xc9 && flxp_towlower(0xc9) == 0xe9 && flxp_towupper(0xff) == 0x178 && flxp_towupper(0xf7) == 0xf7, "towupper/towlower");
    check(flxp_iswspace(' ') && flxp_iswspace('\t') && !flxp_iswspace('a') && flxp_iswdigit('7') && !flxp_iswdigit('a')
          && flxp_iswalpha('z') && flxp_iswalpha(0xe4) && !flxp_iswalpha('1') && flxp_iswalnum('1'), "isw*");

    // swprintf with uxtheme's formats
    int n = flxp_swprintf(buf, 256, WS("%d %d %d"), 212, 208, 200);
    check(n == 11 && weq((WCHAR*)buf, L"212 208 200"), "swprintf %d %d %d");
    int r = -1, g = -1, b = -1;
    check(flxp_swscanf(buf, WS("%d %d %d"), &r, &g, &b) == 3 && r == 212 && g == 208 && b == 200, "swscanf %d %d %d round trip");
    check(flxp_swscanf(WS("  10   20\t30 "), WS("%d %d %d"), &r, &g, &b) == 3 && r == 10 && g == 20 && b == 30, "swscanf whitespace");
    check(flxp_swscanf(WS("1 2"), WS("%d %d %d"), &r, &g, &b) == 2, "swscanf short input count");
    check(flxp_swscanf(WS(""), WS("%d"), &r) == -1, "swscanf EOF");
    unsigned u; int x;
    flxp_wch word[32]; char nword[32]; flxp_wch ch[2];
    check(flxp_swscanf(WS("ff,-7 word nar Z"), WS("%x,%d %s %hs %c"), &x, &r, word, nword, ch) == 5 && x == 255 && r == -7
          && weq((WCHAR*)word, L"word") && !strcmp(nword, "nar") && ch[0] == 'Z', "swscanf %x,%d %s %hs %c");
    check(flxp_swscanf(WS("ff,-7 word nar Z"), WS("%x,%d %s %hs %c"), &x, &r, word, nword, ch) == 5, "swscanf repeat");
    check(flxp_swscanf(WS("ab"), WS("%c%c"), ch, ch + 1) == 2 && ch[0] == 'a' && ch[1] == 'b', "swscanf %c%c no skip");
    check(flxp_swscanf(WS("key=val;x"), WS("%[^=]=%[a-z]"), word, word + 16) == 2 && weq((WCHAR*)word, L"key") && weq((WCHAR*)word + 16, L"val"), "swscanf %[ scanset");
    check(flxp_swscanf(WS("12u Q"), WS("%uu %c"), &u, ch) == 2 && u == 12 && ch[0] == 'Q', "swscanf literal + %c");
    check(flxp_swscanf(WS("abc"), WS("x%d"), &r) == 0, "swscanf literal mismatch");

    struct { const wchar_t* fmt; const wchar_t* want; } cases[] = {
        { L"[%5d]", L"[   42]" }, { L"[%-5d]", L"[42   ]" }, { L"[%05d]", L"[00042]" },
        { L"[%x]", L"[ff]" }, { L"[%X]", L"[FF]" }, { L"[%#x]", L"[0xff]" }, { L"[%+d]", L"[+42]" },
        { L"[%u]", L"[4294967295]" }, { L"[%ld]", L"[-1]" }, { L"[%lx]", L"[ffffffff]" },
    };
    flxp_swprintf(buf, 256, WS("[%5d]"), 42); check(weq((WCHAR*)buf, cases[0].want), "swprintf %5d");
    flxp_swprintf(buf, 256, WS("[%-5d]"), 42); check(weq((WCHAR*)buf, cases[1].want), "swprintf %-5d");
    flxp_swprintf(buf, 256, WS("[%05d]"), 42); check(weq((WCHAR*)buf, cases[2].want), "swprintf %05d");
    flxp_swprintf(buf, 256, WS("[%x]"), 255); check(weq((WCHAR*)buf, cases[3].want), "swprintf %x");
    flxp_swprintf(buf, 256, WS("[%X]"), 255); check(weq((WCHAR*)buf, cases[4].want), "swprintf %X");
    flxp_swprintf(buf, 256, WS("[%#x]"), 255); check(weq((WCHAR*)buf, cases[5].want), "swprintf %#x");
    flxp_swprintf(buf, 256, WS("[%+d]"), 42); check(weq((WCHAR*)buf, cases[6].want), "swprintf %+d");
    flxp_swprintf(buf, 256, WS("[%u]"), -1); check(weq((WCHAR*)buf, cases[7].want), "swprintf %u");
    flxp_swprintf(buf, 256, WS("[%ld]"), (long)-1); check(weq((WCHAR*)buf, cases[8].want), "swprintf %ld");
    flxp_swprintf(buf, 256, WS("[%lx]"), (long)-1); check(weq((WCHAR*)buf, cases[9].want), "swprintf %lx (32-bit long)");
    flxp_swprintf(buf, 256, WS("%s|%S|%hs|%c|%C|%%|%.2s|%-4s|%4s"), L"wide", "narr\xc3\xa4", "hs", L'w', 'n', L"abc", L"l", L"r");
    check(weq((WCHAR*)buf, L"wide|narrä|hs|w|n|%|ab|l   |   r"), "swprintf %s %S %hs %c %C %% precision width");
    flxp_swprintf(buf, 256, WS("%s"), (const wchar_t*)nullptr);
    check(weq((WCHAR*)buf, L"(null)"), "swprintf %s NULL");
    flxp_swprintf(buf, 256, WS("%I64d %lld %.3f %5.1f"), (long long)-1234567890123LL, 7LL, 3.14159, 2.25);
    check(weq((WCHAR*)buf, L"-1234567890123 7 3.142   2.2"), "swprintf %I64d %lld %f");
    check(flxp_swprintf(buf, 5, WS("%s"), L"toolong") == -1 && weq((WCHAR*)buf, L"tool"), "swprintf truncation");

    WCHAR wbuf[1024];
    int wn = wsprintfW(wbuf, L"%s%s\\%s.msstyles", L"/themes/", L"Luna", L"Luna");
    check(wn == 26 && weq(wbuf, L"/themes/Luna\\Luna.msstyles"), "wsprintfW uxtheme format");
    wsprintfW(wbuf, L"%d-%u-%x-%c-%S-%08lX", -5, 5u, 0xabc, L'Q', "n", 0xbeefUL);
    check(weq(wbuf, L"-5-5-abc-Q-n-0000BEEF"), "wsprintfW mixed");
    char abuf[1024];
    wsprintfA(abuf, "%s/%S/%d/%c", "narrow", L"widé", 12, 'z');
    check(!strcmp(abuf, "narrow/wid\xc3\xa9/12/z"), "wsprintfA");
}

static void test_utf()
{
    const char* s = "Gr\xc3\xb6\xc3\x9f" "e \xe2\x82\xac \xf0\x9d\x84\x9e";   // Größe € 𝄞
    std::u16string w = w32::utf16_of(s);
    check(w.size() == 10 && w[2] == 0xf6 && w[6] == 0x20ac && w[8] == 0xd834 && w[9] == 0xdd1e, "utf16_of");
    check(w32::utf8_of((const WCHAR*)w.c_str()) == s, "utf8_of round trip");
    check(w32::utf16_len((const WCHAR*)w.c_str()) == 10, "utf16_len");
    check(w32::utf8_of((const WCHAR*)u"ab\xd800" u"c") == "ab\xef\xbf\xbd" "c", "lone surrogate -> U+FFFD");

    int need = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    check(need == 11, "MultiByteToWideChar size query (-1 includes nul)");
    WCHAR wb[32];
    int got = MultiByteToWideChar(CP_UTF8, 0, s, -1, wb, 32);
    check(got == 11 && wb[10] == 0 && wb[6] == 0x20ac, "MultiByteToWideChar CP_UTF8");
    SetLastError(0);
    check(MultiByteToWideChar(CP_ACP, 0, s, -1, wb, 3) == 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER, "MultiByteToWideChar small buffer");
    check(MultiByteToWideChar(CP_THREAD_ACP, 0, "abc", 2, wb, 32) == 2 && wb[0] == 'a' && wb[1] == 'b', "MultiByteToWideChar explicit length");
    char nb[64];
    BOOL used = TRUE;
    int nn = WideCharToMultiByte(CP_OEMCP, 0, (const WCHAR*)w.c_str(), -1, nullptr, 0, nullptr, &used);
    check(nn == (int)strlen(s) + 1 && !used, "WideCharToMultiByte size query");
    nn = WideCharToMultiByte(CP_ACP, 0, (const WCHAR*)w.c_str(), -1, nb, sizeof nb, nullptr, nullptr);
    check(nn == (int)strlen(s) + 1 && !strcmp(nb, s), "WideCharToMultiByte CP_ACP is UTF-8");
}

static void test_kernel_strings()
{
    WCHAR b[64];
    check(lstrlenW(L"four") == 4 && lstrlenA("three") == 5, "lstrlen");
    lstrcpynW(b, L"abcdef", 4);
    check(weq(b, L"abc"), "lstrcpynW");
    char a[16];
    lstrcpynA(a, "abcdef", 4);
    check(!strcmp(a, "abc"), "lstrcpynA");
    lstrcpyW(b, L"x"); lstrcatW(b, L"yz");
    check(weq(b, L"xyz"), "lstrcpyW/lstrcatW");
    check(lstrcmpW(L"abc", L"abd") < 0 && lstrcmpW(L"a", L"B") < 0 && lstrcmpW(L"a", L"A") < 0 && !lstrcmpW(L"q", L"q"), "lstrcmpW");
    check(!lstrcmpiW(L"ABC", L"abc") && lstrcmpiW(L"abc", L"ABD") < 0, "lstrcmpiW");
    check(!lstrcmpiA("ABC", "abc") && lstrcmpA("a", "b") < 0, "lstrcmpiA/lstrcmpA");
    lstrcpyW(b, L"MiXed é");
    CharUpperW(b);
    check(weq(b, L"MIXED É"), "CharUpperW string");
    CharLowerBuffW(b, 3);
    check(weq(b, L"mixED É"), "CharLowerBuffW");
    check((UINT_PTR)CharUpperW((LPWSTR)(UINT_PTR)'q') == 'Q' && (UINT_PTR)CharLowerW((LPWSTR)(UINT_PTR)'Q') == 'q', "CharUpperW/CharLowerW single char");
    strcpy(a, "abC");
    check(!strcmp(CharUpperA(a), "ABC"), "CharUpperA");
    check(CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, L"Hello", -1, L"hELLO", -1) == CSTR_EQUAL, "CompareStringW ignore case");
    check(CompareStringW(LOCALE_USER_DEFAULT, 0, L"apple", -1, L"Banana", -1) == CSTR_LESS_THAN, "CompareStringW linguistic-ish");
    check(CompareStringW(LOCALE_USER_DEFAULT, 0, L"abcX", 3, L"abc", -1) == CSTR_EQUAL, "CompareStringW lengths");
    check(CompareStringOrdinal(L"B", -1, L"a", -1, FALSE) == CSTR_LESS_THAN && CompareStringOrdinal(L"B", -1, L"a", -1, TRUE) == CSTR_GREATER_THAN, "CompareStringOrdinal");
    check(MulDiv(5, 3, 2) == 8 && MulDiv(-5, 3, 2) == -8 && MulDiv(1, 1, 0) == -1 && MulDiv(7, 96, 72) == 9, "MulDiv rounding");
    check(GetVersion() == 0x0a280105, "GetVersion");
    OSVERSIONINFOW vi; vi.dwOSVersionInfoSize = sizeof vi;
    check(GetVersionExW(&vi) && vi.dwMajorVersion == 5 && vi.dwMinorVersion == 1 && vi.dwBuildNumber == 2600, "GetVersionExW");
    check(GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SDECIMAL, b, 64) == 2 && weq(b, L"."), "GetLocaleInfoW SDECIMAL");
    check(GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SDECIMAL, nullptr, 0) == 2, "GetLocaleInfoW size");
    check(GetUserDefaultLCID() == 0x409, "GetUserDefaultLCID");
    SetLastError(1234);
    check(GetLastError() == 1234, "SetLastError/GetLastError via fake TEB");
    check(GetCurrentThreadId() == 1 && GetProcessHeap() != nullptr, "GetCurrentThreadId/GetProcessHeap via fake TEB");
    setenv("FLXP_TEST_VAR", "v\xc3\xa4l", 1);
    DWORD en = ExpandEnvironmentStringsW(L"<%FLXP_TEST_VAR%|%NOPE_NOT_SET%>", b, 64);
    check(en == lstrlenW(b) + 1u && weq(b, L"<väl|%NOPE_NOT_SET%>"), "ExpandEnvironmentStringsW");
}

static void test_heap()
{
    HANDLE h = GetProcessHeap();
    unsigned char* p = (unsigned char*)HeapAlloc(h, HEAP_ZERO_MEMORY, 100);
    bool zero = true;
    for (int i = 0; i < 100; i++) zero &= !p[i];
    check(p && zero && HeapSize(h, 0, p) == 100, "HeapAlloc zero + HeapSize");
    memset(p, 0xaa, 100);
    p = (unsigned char*)HeapReAlloc(h, HEAP_ZERO_MEMORY, p, 200);
    check(p && p[99] == 0xaa && p[100] == 0 && p[199] == 0 && HeapSize(h, 0, p) == 200, "HeapReAlloc zero growth");
    check(HeapFree(h, 0, p), "HeapFree");
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 10);
    check(g && GlobalLock(g) == g && GlobalSize(g) == 10 && !((char*)g)[9], "GlobalAlloc/Lock/Size");
    GlobalUnlock(g);
    check(GlobalFree(g) == nullptr, "GlobalFree");
    HLOCAL l = LocalAlloc(LMEM_ZEROINIT, 8);
    l = LocalReAlloc(l, 32, LMEM_MOVEABLE | LMEM_ZEROINIT);
    check(l && LocalSize(l) == 32 && !((char*)l)[31], "LocalAlloc/ReAlloc/Size");
    check(LocalFree(l) == nullptr, "LocalFree");
}

extern "C" void* WINAPI Alloc(DWORD);
extern "C" void* WINAPI ReAlloc(void*, DWORD);
extern "C" BOOL WINAPI Free(void*);
extern "C" DWORD WINAPI GetSize(void*);

static void test_comctl_alloc()
{
    char* p = (char*)Alloc(16);
    bool zero = true;
    for (int i = 0; i < 16; i++) zero &= !p[i];
    memset(p, 1, 16);
    p = (char*)ReAlloc(p, 64);
    check(zero && p[15] == 1 && !p[16] && !p[63] && GetSize(p) == 64, "comctl32 Alloc/ReAlloc/GetSize");
    check(Free(p), "comctl32 Free");
    p = (char*)ReAlloc(nullptr, 4);
    check(p && !p[3], "comctl32 ReAlloc(NULL)");
    Free(p);
}

static void test_atoms()
{
    ATOM a = AddAtomW(L"FlxpTestAtom");
    check(a >= 0xc000, "AddAtomW");
    check(AddAtomW(L"flxptestatom") == a, "AddAtomW case-insensitive same atom");
    check(FindAtomW(L"FLXPTESTATOM") == a, "FindAtomW");
    WCHAR b[64];
    check(GetAtomNameW(a, b, 64) == 12 && weq(b, L"FlxpTestAtom"), "GetAtomNameW");
    check(GetAtomNameW(a, b, 5) == 4 && weq(b, L"Flxp"), "GetAtomNameW truncation");
    check(AddAtomW(L"#123") == 123 && AddAtomW((LPCWSTR)MAKEINTATOM(77)) == 77, "integer atoms");
    check(GetAtomNameW(123, b, 64) == 4 && weq(b, L"#123"), "GetAtomNameW integer");
    DeleteAtom(a);
    check(FindAtomW(L"FlxpTestAtom") == a, "DeleteAtom refcount (still present)");
    DeleteAtom(a);
    check(FindAtomW(L"FlxpTestAtom") == 0, "DeleteAtom removes");
    ATOM ga = GlobalAddAtomW(L"GlobalOne");
    check(ga >= 0xc000 && GlobalFindAtomW(L"globalone") == ga && GlobalGetAtomNameW(ga, b, 64) == 9, "GlobalAddAtomW/Find/GetName");
    GlobalDeleteAtom(ga);
    UINT m1 = RegisterWindowMessageW(L"FLXP_MSG"), m2 = RegisterWindowMessageA("flxp_msg");
    check(m1 >= 0xc000 && m1 == m2, "RegisterWindowMessageW/A");
    check(RegisterWindowMessageW(L"FLXP_OTHER") != m1, "RegisterWindowMessage distinct");
}

static void test_registry()
{
    const char* tm = "Software\\Microsoft\\Windows\\CurrentVersion\\ThemeManager";
    w32::reg_set_string(HKEY_CURRENT_USER, tm, "DllName", "/themes/Luna/Luna.msstyles");
    w32::reg_set_string(HKEY_CURRENT_USER, tm, "ColorName", "NormalColor");
    w32::reg_set_string(HKEY_CURRENT_USER, tm, "SizeName", "NormalSize");
    w32::reg_set_string(HKEY_CURRENT_USER, tm, "ThemeActive", "1");
    HKEY k;
    check(RegOpenKeyW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\ThemeManager", &k) == ERROR_SUCCESS, "RegOpenKeyW");
    WCHAR b[MAX_PATH];
    DWORD size = sizeof b, type = 0;
    check(RegQueryValueExW(k, L"DllName", nullptr, &type, (BYTE*)b, &size) == ERROR_SUCCESS && type == REG_SZ
          && weq(b, L"/themes/Luna/Luna.msstyles") && size == (lstrlenW(b) + 1) * 2u, "RegQueryValueExW REG_SZ");
    size = 4;
    check(RegQueryValueExW(k, L"colorname", nullptr, nullptr, (BYTE*)b, &size) == ERROR_MORE_DATA && size == 24, "RegQueryValueExW ERROR_MORE_DATA");
    size = 0;
    check(RegQueryValueExW(k, L"ThemeActive", nullptr, nullptr, nullptr, &size) == ERROR_SUCCESS && size == 4, "RegQueryValueExW size query");
    char ab[64]; DWORD asz = sizeof ab;
    check(RegQueryValueExA(k, "SizeName", nullptr, nullptr, (BYTE*)ab, &asz) == ERROR_SUCCESS && !strcmp(ab, "NormalSize"), "RegQueryValueExA");
    check(RegQueryValueExW(k, L"Missing", nullptr, nullptr, nullptr, &size) == ERROR_FILE_NOT_FOUND, "missing value");
    HKEY k2;
    check(RegOpenKeyExW(HKEY_CURRENT_USER, L"software\\MICROSOFT\\windows\\currentversion\\thememanager", 0, KEY_READ, &k2) == ERROR_SUCCESS && k2 == k, "RegOpenKeyExW case-insensitive");
    check(RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Nope", 0, KEY_READ, &k2) == ERROR_FILE_NOT_FOUND, "missing key ERROR_FILE_NOT_FOUND");
    HKEY nk; DWORD disp;
    check(RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Flxp\\Sub", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &nk, &disp) == ERROR_SUCCESS && disp == REG_CREATED_NEW_KEY, "RegCreateKeyExW new");
    DWORD v = 42;
    check(RegSetValueExW(nk, L"Answer", 0, REG_DWORD, (BYTE*)&v, 4) == ERROR_SUCCESS, "RegSetValueExW");
    DWORD out = 0; size = 4;
    check(RegGetValueW(HKEY_LOCAL_MACHINE, L"Software\\Flxp\\Sub", L"Answer", RRF_RT_REG_DWORD, &type, &out, &size) == ERROR_SUCCESS && out == 42 && type == REG_DWORD, "RegGetValueW DWORD");
    size = sizeof b;
    check(RegGetValueW(HKEY_LOCAL_MACHINE, L"Software\\Flxp\\Sub", L"Answer", RRF_RT_REG_SZ, nullptr, b, &size) == ERROR_UNSUPPORTED_TYPE, "RegGetValueW type mismatch");
    DWORD light = TRUE; size = sizeof light;
    check(RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size) == ERROR_FILE_NOT_FOUND && light == TRUE, "RegGetValueW missing leaves data");
    size = sizeof b;
    check(RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\ThemeManager", L"ColorName", RRF_RT_REG_SZ, nullptr, b, &size) == ERROR_SUCCESS && weq(b, L"NormalColor"), "RegGetValueW REG_SZ");
    check(RegCreateKeyW(HKEY_LOCAL_MACHINE, L"Software\\Flxp\\Sub", &k2) == ERROR_SUCCESS && k2 == nk, "RegCreateKeyW existing");
    check(RegDeleteValueW(nk, L"answer") == ERROR_SUCCESS && RegDeleteValueW(nk, L"answer") == ERROR_FILE_NOT_FOUND, "RegDeleteValueW");
    RegSetValueExW(nk, nullptr, 0, REG_SZ, (const BYTE*)L"dflt", 10);
    LONG lsz = sizeof b;
    check(RegQueryValueW(HKEY_LOCAL_MACHINE, L"Software\\Flxp\\Sub", b, &lsz) == ERROR_SUCCESS && weq(b, L"dflt"), "RegQueryValueW default value");
    check(RegCloseKey(k) == ERROR_SUCCESS && RegCloseKey(nk) == ERROR_SUCCESS, "RegCloseKey");
}

static void test_resources(const std::string& dir)
{
    std::string path = dir + "/test.res";
    std::u16string wpath = w32::utf16_of(path.c_str());
    // backslash form is accepted too
    for (auto& c : wpath) if (c == '/') c = '\\';
    HMODULE m = LoadLibraryExW((LPCWSTR)wpath.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
    check(m != nullptr, "LoadLibraryExW .res");
    if (!m) return;
    WCHAR b[64];
    check(LoadStringW(m, 1, b, 64) == 3 && weq(b, L"One"), "LoadStringW 1 (en-US preferred)");
    check(LoadStringW(m, 17, b, 64) == 9 && weq(b, L"Seventeen"), "LoadStringW 17");
    check(LoadStringW(m, 40, b, 64) == 5 && weq(b, L"Forty"), "LoadStringW 40");
    check(LoadStringW(m, 18, b, 64) == 7 && weq(b, L"Größe ✓"), "LoadStringW non-ASCII");
    check(LoadStringW(m, 17, b, 4) == 3 && weq(b, L"Sev"), "LoadStringW truncation");
    check(LoadStringW(m, 2, b, 64) == 0 && b[0] == 0, "LoadStringW missing");
    const WCHAR* p = nullptr;
    int n = LoadStringW(m, 40, (LPWSTR)&p, 0);
    check(n == 5 && p && !flxp_wcsncmp((W)p, WS("Forty"), 5), "LoadStringW pointer form");
    char a[64];
    check(LoadStringA(m, 18, a, 64) == 11 && !strcmp(a, "Gr\xc3\xb6\xc3\x9f" "e \xe2\x9c\x93"), "LoadStringA UTF-8");
    HRSRC r = FindResourceExW(m, (LPCWSTR)RT_STRING, MAKEINTRESOURCEW(1), MAKELANGID(LANG_GERMAN, SUBLANG_NEUTRAL));
    check(r != nullptr, "FindResourceExW German string table");
    if (r) {
        const WORD* d = (const WORD*)LockResource(LoadResource(m, r));
        check(d && SizeofResource(m, r) > 0 && d[1] == 4 && !flxp_wcsncmp((W)(d + 2), WS("Eins"), 4), "German table content");
    }
    r = FindResourceW(m, L"PALBMP", (LPCWSTR)RT_BITMAP);
    check(r && SizeofResource(m, r) > 40, "FindResourceW named");
    check(FindResourceW(m, L"palbmp", (LPCWSTR)RT_BITMAP) != nullptr, "FindResourceW name case-insensitive");
    check(FindResourceW(m, L"#2", (LPCWSTR)RT_BITMAP) != nullptr && FindResourceA(m, MAKEINTRESOURCEA(100), (LPCSTR)RT_DIALOG) != nullptr, "FindResource #id / ordinal / A");
    check(FindResourceW(m, L"NOPE", (LPCWSTR)RT_BITMAP) == nullptr, "FindResourceW missing");
    check(GetModuleHandleW(L"test.res") == m, "GetModuleHandleW by loaded name");
    HMODULE m2 = LoadLibraryW((LPCWSTR)wpath.c_str());
    check(m2 == m, "LoadLibraryW refcount same handle");
    check(FreeLibrary(m2) && FindResourceW(m, L"PALBMP", (LPCWSTR)RT_BITMAP) != nullptr, "FreeLibrary keeps module while referenced");
    check(FreeLibrary(m), "FreeLibrary");

    HMODULE main0 = GetModuleHandleW(nullptr);
    check(main0 && LoadStringW(nullptr, 256, b, 64) == 0, "empty main module");
    check(w32::set_main_module((dir + "/comdlg32.res").c_str()), "set_main_module comdlg32.res");
    check(LoadStringW(GetModuleHandleW(nullptr), 256, b, 64) == 7 && weq(b, L"Regular"), "LoadStringW from main module (256)");
    check(LoadStringW(nullptr, 257, b, 64) == 4 && weq(b, L"Bold"), "LoadStringW(NULL hinst)");
    check(GetModuleHandleA("user32.dll") == GetModuleHandleW(nullptr), "GetModuleHandleA unknown -> main");
    check(LoadLibraryW(L"/nonexistent/x.dll") == nullptr && GetLastError() == ERROR_MOD_NOT_FOUND, "LoadLibraryW missing");
}

static void test_files(const std::string& scratch)
{
    std::string dir = scratch + "/findtest";
    mkdir(dir.c_str(), 0755);
    mkdir((dir + "/sub").c_str(), 0755);
    for (const char* f : { "a.txt", "B.TXT", "c.msstyles", ".hidden" }) {
        FILE* fp = fopen((dir + "/" + f).c_str(), "w");
        if (fp) { fputs("hello", fp); fclose(fp); }
    }
    std::u16string pat = w32::utf16_of((dir + "\\*.*").c_str());
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((LPCWSTR)pat.c_str(), &fd);
    check(h != INVALID_HANDLE_VALUE, "FindFirstFileW *.*");
    int count = 0, dirs = 0;
    bool size_ok = false, hidden_ok = false;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            count++;
            std::string name = u8(fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) dirs++;
            if (name == "a.txt") size_ok = fd.nFileSizeLow == 5 && fd.ftLastWriteTime.dwHighDateTime > 0x1d00000;
            if (name == ".hidden") hidden_ok = fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN;
        } while (FindNextFileW(h, &fd));
        check(GetLastError() == ERROR_NO_MORE_FILES, "FindNextFileW ends with ERROR_NO_MORE_FILES");
        FindClose(h);
    }
    check(count == 7 && dirs == 3, "FindFirstFileW enumerates . .. sub and 4 files");
    check(size_ok && hidden_ok, "WIN32_FIND_DATAW size/time/attributes");
    pat = w32::utf16_of((dir + "/*.txt").c_str());
    h = FindFirstFileW((LPCWSTR)pat.c_str(), &fd);
    count = 0;
    if (h != INVALID_HANDLE_VALUE) { do count++; while (FindNextFileW(h, &fd)); FindClose(h); }
    check(count == 2, "FindFirstFileW *.txt (case-insensitive)");
    pat = w32::utf16_of((dir + "/*.none").c_str());
    check(FindFirstFileW((LPCWSTR)pat.c_str(), &fd) == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND, "FindFirstFileW no match");
    pat = w32::utf16_of((dir + "/c.msstyles").c_str());
    h = FindFirstFileW((LPCWSTR)pat.c_str(), &fd);
    check(h != INVALID_HANDLE_VALUE && weq(fd.cFileName, L"c.msstyles"), "FindFirstFileW exact name");
    if (h != INVALID_HANDLE_VALUE) FindClose(h);

    WCHAR old[MAX_PATH], b[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, old);
    std::u16string wdir = w32::utf16_of(dir.c_str());
    check(SetCurrentDirectoryW((LPCWSTR)wdir.c_str()), "SetCurrentDirectoryW");
    UINT n = GetCurrentDirectoryW(MAX_PATH, b);
    check(n == wdir.size() && u8(b) == dir, "GetCurrentDirectoryW");
    check(GetCurrentDirectoryW(2, b) == wdir.size() + 1, "GetCurrentDirectoryW size query");
    WCHAR* fp = nullptr;
    n = GetFullPathNameW(L"sub\\..\\a.txt", MAX_PATH, b, &fp);
    check(u8(b) == dir + "/a.txt" && fp && weq(fp, L"a.txt") && n == (UINT)lstrlenW(b), "GetFullPathNameW");
    SetCurrentDirectoryW(old);
    check(GetDriveTypeW(L"/") == DRIVE_FIXED && GetDriveTypeW(L"/nonexistent-root/") == DRIVE_NO_ROOT_DIR, "GetDriveTypeW");
}

static void test_debug()
{
    struct __wine_debug_channel a = { 0xff, "listbox" }, b = { 0xff, "edit" }, c = { 0xff, "combo" };
    w32::debug_set_options(nullptr);
    unsigned char f = __wine_dbg_get_channel_flags(&a);
    check(f == ((1 << __WINE_DBCL_ERR) | (1 << __WINE_DBCL_FIXME)), "default flags err+fixme");
    a.flags = b.flags = c.flags = 0xff;
    w32::debug_set_options("warn+all,-fixme,+listbox,trace-listbox,-combo");
    unsigned char fa = __wine_dbg_get_channel_flags(&a), fb = __wine_dbg_get_channel_flags(&b), fc = __wine_dbg_get_channel_flags(&c);
    check(fa == ((1 << __WINE_DBCL_ERR) | (1 << __WINE_DBCL_FIXME) | (1 << __WINE_DBCL_WARN)), "WINEDEBUG +listbox then trace-listbox");
    check(fb == ((1 << __WINE_DBCL_ERR) | (1 << __WINE_DBCL_WARN)), "WINEDEBUG warn+all,-fixme");
    check(fc == 0, "WINEDEBUG -combo");
    check(__wine_dbg_get_channel_flags(&a) == fa, "channel flags cached");
    a.flags = 0xff;
    w32::debug_set_options("+all");
    check(__wine_dbg_get_channel_flags(&a) == 0x0f, "WINEDEBUG +all");
    const char* s1 = __wine_dbg_strdup("one");
    const char* s2 = wine_dbg_sprintf("%d-%s", 5, "x");
    check(!strcmp(s1, "one") && !strcmp(s2, "5-x"), "__wine_dbg_strdup / wine_dbg_sprintf");
    check(!strcmp(wine_dbgstr_w(L"wé"), "L\"w\\00e9\""), "wine_dbgstr_w");
    fflush(stdout);
    fprintf(stderr, "(expected debug line follows)\n");
    int n = wine_dbg_log(__WINE_DBCL_FIXME, &a, "test_debug", "hello %d\n", 7);
    check(n == (int)strlen("fixme:listbox:test_debug hello 7\n"), "__wine_dbg_header + output");
    w32::debug_set_options(nullptr);
}

static int try_value;
static void test_try()
{
    int v = 0;
    __TRY
    {
        v = 1;
        try_value = 2;
    }
    __EXCEPT_PAGE_FAULT
    {
        v = -1;
    }
    __ENDTRY
    check(v == 1 && try_value == 2, "__TRY/__EXCEPT_PAGE_FAULT normal path");
}

// With the real GDI shim linked: measure through a memory DC and check the
// analysis agrees with GetTextExtentExPointW, and that drawing works.
static void test_usp10_real()
{
    printf("(usp10: real GDI linked, using a memory DC)\n");
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateBitmap(200, 30, 1, 32, nullptr);
    HGDIOBJ obmp = SelectObject(dc, bmp);
    HGDIOBJ ofont = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    const WCHAR* t = L"Wiki x";
    int ext[6]; SIZE esz;
    BOOL got = GetTextExtentExPointW(dc, t, 6, 0, nullptr, ext, &esz);
    check(dc && got && esz.cx > 0, "real GDI: GetTextExtentExPointW on memory DC");
    SCRIPT_STRING_ANALYSIS ssa = nullptr;
    HRESULT hr = ScriptStringAnalyse(dc, t, 6, 25, -1, SSA_LINK | SSA_FALLBACK | SSA_GLYPHS, -1, nullptr, nullptr, nullptr, nullptr, nullptr, &ssa);
    check(hr == S_OK && ssa, "ScriptStringAnalyse (real GDI)");
    if (!ssa) return;
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    const SIZE* sz = ScriptString_pSize(ssa);
    check(sz->cx == esz.cx && sz->cy == tm.tmHeight, "ScriptString_pSize matches GetTextExtentExPointW");
    int dx[6];
    ScriptStringGetLogicalWidths(ssa, dx);
    bool ok = true;
    for (int i = 0; i < 6; i++) ok &= dx[i] == ext[i] - (i ? ext[i - 1] : 0);
    check(ok, "ScriptStringGetLogicalWidths matches extents");
    int x = -1, cp = -1, tr = -1;
    ScriptStringCPtoX(ssa, 3, FALSE, &x);
    ok = x == ext[2];
    ScriptStringXtoCP(ssa, ext[2], &cp, &tr);
    ok &= cp == 3 && !tr;
    ScriptStringXtoCP(ssa, esz.cx + 5, &cp, &tr);
    ok &= cp == 6 && !tr;
    check(ok, "ScriptStringCPtoX/XtoCP (real GDI)");
    RECT rc = { 0, 0, 200, 30 };
    PatBlt(dc, 0, 0, 200, 30, WHITENESS);
    SetTextColor(dc, RGB(0, 0, 0));
    SetBkMode(dc, TRANSPARENT);
    check(ScriptStringOut(ssa, 2, 2, 0, &rc, 1, 3, FALSE) == S_OK, "ScriptStringOut (real GDI)");
    COLORREF sel = GetPixel(dc, 2 + ext[0] + 1, 3);
    printf("(selection pixel %06lx, highlight %06lx)\n", (unsigned long)sel, (unsigned long)GetSysColor(COLOR_HIGHLIGHT));
    check(sel == GetSysColor(COLOR_HIGHLIGHT) || sel == GetSysColor(COLOR_HIGHLIGHTTEXT), "selection drawn with highlight colours");
    ScriptStringFree(&ssa);
    SelectObject(dc, ofont);
    SelectObject(dc, obmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

static void test_usp10()
{
    SCRIPT_LOGATTR la[16];
    SCRIPT_ANALYSIS sa;
    memset(&sa, 0, sizeof sa);
    const WCHAR* s = L"ab  cd-ef g";
    check(ScriptBreak(s, 11, &sa, la) == S_OK, "ScriptBreak");
    check(!la[0].fSoftBreak && la[4].fSoftBreak && la[7].fSoftBreak && la[10].fSoftBreak && !la[1].fSoftBreak && !la[3].fSoftBreak,
          "ScriptBreak fSoftBreak after spaces/hyphen");
    check(la[2].fWhiteSpace && la[3].fWhiteSpace && !la[0].fWhiteSpace && la[0].fWordStop && la[4].fWordStop && !la[5].fWordStop, "ScriptBreak fWhiteSpace/fWordStop");
    bool stops = true;
    for (int i = 0; i < 11; i++) stops &= la[i].fCharStop;
    check(stops, "ScriptBreak fCharStop");
    check(ScriptIsComplex(s, 11, SIC_COMPLEX) == S_FALSE, "ScriptIsComplex");

    TEXTMETRICW probe;
    GetTextMetricsW(nullptr, &probe);          // only the weak fake sets fake_gdi_used
    if (!fake_gdi_used) { test_usp10_real(); return; }
    HDC dc = (HDC)(uintptr_t)FAKE_DC;
    SCRIPT_STRING_ANALYSIS ssa = nullptr;
    const WCHAR* t = L"Wiki x";
    HRESULT hr = ScriptStringAnalyse(dc, t, 6, 25, -1, SSA_LINK | SSA_FALLBACK | SSA_GLYPHS, -1, nullptr, nullptr, nullptr, nullptr, nullptr, &ssa);
    check(hr == S_OK && ssa, "ScriptStringAnalyse");
    const SIZE* sz = ScriptString_pSize(ssa);
    check(sz && sz->cx == 11 + 3 + 7 + 3 + 7 + 7 && sz->cy == 13, "ScriptString_pSize");
    check(*ScriptString_pcOutChars(ssa) == 6, "ScriptString_pcOutChars");
    int dx[6];
    ScriptStringGetLogicalWidths(ssa, dx);
    check(dx[0] == 11 && dx[1] == 3 && dx[5] == 7, "ScriptStringGetLogicalWidths");
    int x = -1;
    ScriptStringCPtoX(ssa, 2, FALSE, &x);
    bool ok = x == 14;
    ScriptStringCPtoX(ssa, 2, TRUE, &x);
    ok &= x == 21;
    ScriptStringCPtoX(ssa, 6, FALSE, &x);
    ok &= x == 38;
    check(ok, "ScriptStringCPtoX leading/trailing/end");
    int cp, tr;
    ScriptStringXtoCP(ssa, 12, &cp, &tr); ok = cp == 1 && !tr;
    ScriptStringXtoCP(ssa, 13, &cp, &tr); ok &= cp == 1 && tr;
    ScriptStringXtoCP(ssa, 100, &cp, &tr); ok &= cp == 6 && !tr;
    ScriptStringXtoCP(ssa, -3, &cp, &tr); ok &= cp == -1 && tr;
    check(ok, "ScriptStringXtoCP");
    text_calls.clear();
    RECT fr = { 0, 0, 200, 50 };
    COLORREF fg0 = fake_fg;
    ScriptStringOut(ssa, 5, 7, 0, &fr, 1, 3, FALSE);
    check(text_calls.size() == 2 && text_calls[0].x == 5 && text_calls[0].s.size() == 6 && text_calls[0].dx.size() == 6
          && text_calls[1].x == 16 && text_calls[1].s == u"ik" && (text_calls[1].opts & ETO_OPAQUE)
          && text_calls[1].rc.left == 16 && text_calls[1].rc.right == 26 && text_calls[1].rc.top == 7 && text_calls[1].rc.bottom == 20
          && text_calls[1].fg == RGB(255, 255, 255) && fake_fg == fg0, "ScriptStringOut + selection");
    ScriptStringFree(&ssa);
    check(ssa == nullptr, "ScriptStringFree");

    // Tabs (edit.c passes SSA_TAB with a tabdef) and password masking.
    SCRIPT_TABDEF td = { 0, 4, nullptr, 0 };    // default stops: 32 dialog units = 8 * iScale
    WCHAR pw = '*';
    hr = ScriptStringAnalyse(dc, L"a\tb", 3, 20, -1, SSA_GLYPHS | SSA_TAB, -1, nullptr, nullptr, nullptr, &td, nullptr, &ssa);
    ScriptStringGetLogicalWidths(ssa, dx);
    check(hr == S_OK && dx[0] == 7 && dx[1] == 32 - 7 && dx[2] == 7, "ScriptStringAnalyse SSA_TAB");
    ScriptStringFree(&ssa);
    hr = ScriptStringAnalyse(dc, &pw, 4, 22, -1, SSA_GLYPHS | SSA_PASSWORD, -1, nullptr, nullptr, nullptr, nullptr, nullptr, &ssa);
    check(hr == S_OK && ScriptString_pSize(ssa)->cx == 28, "ScriptStringAnalyse SSA_PASSWORD");
    ScriptStringFree(&ssa);
    hr = ScriptStringAnalyse(dc, L"abcdef", 6, 25, -1, SSA_GLYPHS | SSA_CLIP, 20, nullptr, nullptr, nullptr, nullptr, nullptr, &ssa);
    check(hr == S_OK && *ScriptString_pcOutChars(ssa) == 2 && ScriptString_pSize(ssa)->cx == 14, "ScriptStringAnalyse SSA_CLIP");
    ScriptStringFree(&ssa);
    check(ScriptStringAnalyse(dc, L"", 0, 16, -1, SSA_GLYPHS, -1, nullptr, nullptr, nullptr, nullptr, nullptr, &ssa) == E_INVALIDARG && !ssa, "ScriptStringAnalyse empty fails");
}

static void test_imm()
{
    HIMC c = ImmGetContext(nullptr);
    check(c == nullptr && ImmReleaseContext(nullptr, c) && ImmGetCompositionStringW(c, GCS_RESULTSTR, nullptr, 0) == 0
          && !ImmGetOpenStatus(c), "imm32 stubs");
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
    const char* tmp = getenv("TMPDIR");
    std::string scratch = argc > 2 ? argv[2] : tmp && *tmp ? tmp : "/tmp";
    std::string resdir = argc > 1 ? argv[1] : beside_exe("");   // make puts test.res and comdlg32.res there
    test_crt();
    test_utf();
    test_kernel_strings();
    test_heap();
    test_comctl_alloc();
    test_atoms();
    test_registry();
    test_resources(resdir);
    test_files(scratch);
    test_debug();
    test_try();
    test_usp10();
    test_imm();
    printf("%d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
