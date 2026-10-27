// Glue for the vendored comctl32: COM GUIDs, the few shlwapi/winmm/kernel32
// and gdi32 functions the extra controls need.
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <vector>
#include <algorithm>
#include <string>
#include "res/res.h"   // before w32.h: winuser.h's RT_* macros clash with res::
#include "w32.h"
extern "C" {
#define INITGUID
#include "initguid.h"
#include "commctrl.h"
#include "commoncontrols.h"
#include "winreg.h"
#include "mmsystem.h"
LONG WINAPI RegOpenKeyExA(HKEY, LPCSTR, DWORD, REGSAM, HKEY*);
}

// comctl32's own resources (toolbar bitmaps, cursors, property sheet dialogs),
// compiled from Wine's comctl32.rc by rc/build.sh and embedded here.
asm(".section .rodata\n"
    ".global flxp_comctl32_res_start\n.global flxp_comctl32_res_end\n"
    "flxp_comctl32_res_start:\n.incbin \"wux/rc/comctl32.res\"\nflxp_comctl32_res_end:\n"
    ".previous\n");
extern "C" const unsigned char flxp_comctl32_res_start[], flxp_comctl32_res_end[];

namespace w32 {
HMODULE comctl32_module()
{
    static res::Module* m;
    if (!m) m = res::load_memory(flxp_comctl32_res_start, (size_t)(flxp_comctl32_res_end - flxp_comctl32_res_start));
    return (HMODULE)m;
}
}

extern "C" {

// unknwn.h was already included (without INITGUID) through w32.h, so define this one by hand.
const GUID IID_IUnknown = { 0x00000000, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

// ---- shlwapi strings -------------------------------------------------------
static int wcmp(const WCHAR* a, const WCHAR* b, int n, bool icase)
{
    for (int i = 0; n < 0 || i < n; i++) {
        WCHAR x = a[i], y = b[i];
        if (icase) { if (x >= 'a' && x <= 'z') x -= 32; if (y >= 'a' && y <= 'z') y -= 32; }
        if (x != y) return x < y ? -1 : 1;
        if (!x) return 0;
    }
    return 0;
}
INT WINAPI StrCmpIW(LPCWSTR a, LPCWSTR b) { return wcmp(a, b, -1, true); }
INT WINAPI StrCmpNIW(LPCWSTR a, LPCWSTR b, INT n) { return wcmp(a, b, n, true); }
INT WINAPI StrCmpNW(LPCWSTR a, LPCWSTR b, INT n) { return wcmp(a, b, n, false); }
INT WINAPI StrCmpW(LPCWSTR a, LPCWSTR b) { return wcmp(a, b, -1, false); }
LPWSTR WINAPI StrChrW(LPCWSTR s, WCHAR c) { for (; *s; s++) if (*s == c) return (LPWSTR)s; return nullptr; }
LPWSTR WINAPI StrRChrW(LPCWSTR s, LPCWSTR end, WORD c) { LPWSTR r = nullptr; for (; *s && (!end || s < end); s++) if (*s == c) r = (LPWSTR)s; return r; }
LPWSTR WINAPI StrStrIW(LPCWSTR s, LPCWSTR sub)
{
    size_t n = 0; while (sub[n]) n++;
    for (; *s; s++) if (!wcmp(s, sub, (int)n, true)) return (LPWSTR)s;
    return nullptr;
}
LPWSTR WINAPI StrStrW(LPCWSTR s, LPCWSTR sub)
{
    size_t n = 0; while (sub[n]) n++;
    for (; *s; s++) if (!wcmp(s, sub, (int)n, false)) return (LPWSTR)s;
    return nullptr;
}
INT WINAPI StrCSpnW(LPCWSTR s, LPCWSTR set) { int i = 0; for (; s[i]; i++) if (StrChrW(set, s[i])) break; return i; }
LPWSTR WINAPI StrCpyNW(LPWSTR d, LPCWSTR s, INT n) { int i = 0; for (; i < n - 1 && s[i]; i++) d[i] = s[i]; if (n > 0) d[i] = 0; return d; }
INT WINAPI StrToIntW(LPCWSTR s) { return (int)flxp_wtoi((const flxp_wch*)s); }
BOOL WINAPI StrTrimW(LPWSTR s, LPCWSTR trim)
{
    size_t len = 0; while (s[len]) len++;
    size_t b = 0; while (b < len && StrChrW(trim, s[b])) b++;
    size_t e = len; while (e > b && StrChrW(trim, s[e - 1])) e--;
    if (b == 0 && e == len) return FALSE;
    memmove(s, s + b, (e - b) * 2); s[e - b] = 0;
    return TRUE;
}

// ---- winmm (animate control's AVI reader): not supported --------------------
HMMIO WINAPI mmioOpenW(LPWSTR, MMIOINFO*, DWORD) { return nullptr; }
MMRESULT WINAPI mmioClose(HMMIO, UINT) { return 0; }
LONG WINAPI mmioRead(HMMIO, HPSTR, LONG) { return -1; }
LONG WINAPI mmioSeek(HMMIO, LONG, INT) { return -1; }
MMRESULT WINAPI mmioDescend(HMMIO, MMCKINFO*, const MMCKINFO*, UINT) { return MMSYSERR_ERROR; }
MMRESULT WINAPI mmioAscend(HMMIO, MMCKINFO*, UINT) { return MMSYSERR_ERROR; }

// ---- kernel32 odds and ends --------------------------------------------------
FARPROC WINAPI GetProcAddress(HMODULE, LPCSTR) { return nullptr; }
HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD) { return nullptr; }
LONG WINAPI RegOpenKeyA(HKEY root, LPCSTR path, HKEY* out) { return RegOpenKeyExA(root, path, 0, KEY_READ, out); }

static const ULONGLONG TICKS_1601_TO_1970 = 116444736000000000ULL;

static void st_from_tm(SYSTEMTIME* st, const struct tm* t, int ms)
{
    st->wYear = (WORD)(t->tm_year + 1900); st->wMonth = (WORD)(t->tm_mon + 1); st->wDay = (WORD)t->tm_mday;
    st->wDayOfWeek = (WORD)t->tm_wday; st->wHour = (WORD)t->tm_hour; st->wMinute = (WORD)t->tm_min;
    st->wSecond = (WORD)t->tm_sec; st->wMilliseconds = (WORD)ms;
}

void WINAPI GetLocalTime(SYSTEMTIME* st) { struct timeval tv; gettimeofday(&tv, nullptr); struct tm t; localtime_r(&tv.tv_sec, &t); st_from_tm(st, &t, (int)(tv.tv_usec / 1000)); }
void WINAPI GetSystemTime(SYSTEMTIME* st) { struct timeval tv; gettimeofday(&tv, nullptr); struct tm t; gmtime_r(&tv.tv_sec, &t); st_from_tm(st, &t, (int)(tv.tv_usec / 1000)); }

BOOL WINAPI SystemTimeToFileTime(const SYSTEMTIME* st, FILETIME* ft)
{
    struct tm t; memset(&t, 0, sizeof t);
    t.tm_year = st->wYear - 1900; t.tm_mon = st->wMonth - 1; t.tm_mday = st->wDay;
    t.tm_hour = st->wHour; t.tm_min = st->wMinute; t.tm_sec = st->wSecond;
    time_t secs = timegm(&t);
    if (secs == (time_t)-1) return FALSE;
    ULONGLONG v = (ULONGLONG)secs * 10000000ULL + (ULONGLONG)st->wMilliseconds * 10000ULL + TICKS_1601_TO_1970;
    ft->dwLowDateTime = (DWORD)v; ft->dwHighDateTime = (DWORD)(v >> 32);
    return TRUE;
}

BOOL WINAPI FileTimeToSystemTime(const FILETIME* ft, SYSTEMTIME* st)
{
    ULONGLONG v = ((ULONGLONG)ft->dwHighDateTime << 32) | ft->dwLowDateTime;
    if (v < TICKS_1601_TO_1970) return FALSE;
    v -= TICKS_1601_TO_1970;
    time_t secs = (time_t)(v / 10000000ULL);
    struct tm t; gmtime_r(&secs, &t);
    st_from_tm(st, &t, (int)((v / 10000ULL) % 1000));
    return TRUE;
}

LONG WINAPI CompareFileTime(const FILETIME* a, const FILETIME* b)
{
    if (a->dwHighDateTime != b->dwHighDateTime) return a->dwHighDateTime > b->dwHighDateTime ? 1 : -1;
    if (a->dwLowDateTime != b->dwLowDateTime) return a->dwLowDateTime > b->dwLowDateTime ? 1 : -1;
    return 0;
}

BOOL WINAPI FileTimeToLocalFileTime(const FILETIME* in, FILETIME* out) { *out = *in; return TRUE; }
BOOL WINAPI LocalFileTimeToFileTime(const FILETIME* in, FILETIME* out) { *out = *in; return TRUE; }

// Minimal GetDateFormat/GetTimeFormat: picture strings with d, dd, ddd, dddd, M, MM, MMM, MMMM, y, yy, yyyy, h, hh, H, HH, m, mm, s, ss, tt.
static const char* day_names[] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
static const char* month_names[] = {"January","February","March","April","May","June","July","August","September","October","November","December"};

static int format_picture(const SYSTEMTIME* st, const char* pic, std::string& out)
{
    for (size_t i = 0; pic[i];) {
        char c = pic[i]; size_t n = 1; while (pic[i + n] == c) n++;
        char buf[32]; buf[0] = 0;
        switch (c) {
        case 'd': if (n == 1) snprintf(buf, sizeof buf, "%d", st->wDay); else if (n == 2) snprintf(buf, sizeof buf, "%02d", st->wDay);
                  else if (n == 3) snprintf(buf, sizeof buf, "%.3s", day_names[st->wDayOfWeek % 7]); else snprintf(buf, sizeof buf, "%s", day_names[st->wDayOfWeek % 7]); break;
        case 'M': if (n == 1) snprintf(buf, sizeof buf, "%d", st->wMonth); else if (n == 2) snprintf(buf, sizeof buf, "%02d", st->wMonth);
                  else if (n == 3) snprintf(buf, sizeof buf, "%.3s", month_names[(st->wMonth - 1) % 12]); else snprintf(buf, sizeof buf, "%s", month_names[(st->wMonth - 1) % 12]); break;
        case 'y': if (n <= 2) snprintf(buf, sizeof buf, "%02d", st->wYear % 100); else snprintf(buf, sizeof buf, "%d", st->wYear); break;
        case 'h': snprintf(buf, sizeof buf, n == 1 ? "%d" : "%02d", st->wHour % 12 == 0 ? 12 : st->wHour % 12); break;
        case 'H': snprintf(buf, sizeof buf, n == 1 ? "%d" : "%02d", st->wHour); break;
        case 'm': snprintf(buf, sizeof buf, n == 1 ? "%d" : "%02d", st->wMinute); break;
        case 's': snprintf(buf, sizeof buf, n == 1 ? "%d" : "%02d", st->wSecond); break;
        case 't': snprintf(buf, sizeof buf, n == 1 ? "%c" : "%s", st->wHour < 12 ? 'A' : 'P'); if (n > 1) snprintf(buf, sizeof buf, "%s", st->wHour < 12 ? "AM" : "PM"); break;
        case '\'': { size_t j = i + 1; while (pic[j] && pic[j] != '\'') out += pic[j++]; i = j + (pic[j] ? 1 : 0); continue; }
        default: out.append(n, c); break;
        }
        out += buf;
        i += n;
    }
    return (int)out.size();
}

static int return_w(const std::string& s, LPWSTR buf, INT n)
{
    std::u16string w = w32::utf16_of(s.c_str());
    if (n == 0) return (int)w.size() + 1;
    if ((int)w.size() + 1 > n) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
    memcpy(buf, w.c_str(), (w.size() + 1) * 2);
    return (int)w.size() + 1;
}

INT WINAPI GetDateFormatW(LCID, DWORD flags, const SYSTEMTIME* st, LPCWSTR fmt, LPWSTR buf, INT n)
{
    SYSTEMTIME now; if (!st) { GetLocalTime(&now); st = &now; }
    std::string pic = fmt ? w32::utf8_of(fmt) : ((flags & DATE_LONGDATE) ? "dddd, MMMM dd, yyyy" : (flags & DATE_YEARMONTH) ? "MMMM yyyy" : "M/d/yyyy");
    std::string out; format_picture(st, pic.c_str(), out);
    return return_w(out, buf, n);
}
INT WINAPI GetDateFormatA(LCID l, DWORD f, const SYSTEMTIME* st, LPCSTR fmt, LPSTR buf, INT n)
{
    SYSTEMTIME now; if (!st) { GetLocalTime(&now); st = &now; }
    std::string out; format_picture(st, fmt ? fmt : ((f & DATE_LONGDATE) ? "dddd, MMMM dd, yyyy" : (f & DATE_YEARMONTH) ? "MMMM yyyy" : "M/d/yyyy"), out);
    if (n == 0) return (int)out.size() + 1;
    if ((int)out.size() + 1 > n) return 0;
    memcpy(buf, out.c_str(), out.size() + 1); return (int)out.size() + 1;
}
INT WINAPI GetTimeFormatW(LCID, DWORD flags, const SYSTEMTIME* st, LPCWSTR fmt, LPWSTR buf, INT n)
{
    SYSTEMTIME now; if (!st) { GetLocalTime(&now); st = &now; }
    std::string pic = fmt ? w32::utf8_of(fmt) : ((flags & TIME_NOSECONDS) ? "h:mm tt" : "h:mm:ss tt");
    std::string out; format_picture(st, pic.c_str(), out);
    return return_w(out, buf, n);
}

// ---- gdi32 extras ------------------------------------------------------------
BOOL WINAPI PolyPolyline(HDC hdc, const POINT* pts, const DWORD* counts, DWORD n)
{
    for (DWORD i = 0; i < n; i++) { if (!Polyline(hdc, pts, (INT)counts[i])) return FALSE; pts += counts[i]; }
    return TRUE;
}
COLORREF WINAPI GetNearestColor(HDC, COLORREF c) { return c & 0xffffff; }
BOOL WINAPI FrameRgn(HDC hdc, HRGN hrgn, HBRUSH brush, INT w, INT h)
{
    // frame = region minus the region shrunk by (w,h): approximate with the bounding rect's edges
    w32::Region* rg = w32::as<w32::Region>(hrgn, w32::T_REGION);
    if (!rg) return FALSE;
    for (const gdi::Rect& r : rg->rgn.rects()) {
        RECT rc = w32::RECT_of(r);
        RECT e;
        e = rc; e.bottom = e.top + h; FillRect(hdc, &e, brush);
        e = rc; e.top = e.bottom - h; FillRect(hdc, &e, brush);
        e = rc; e.right = e.left + w; FillRect(hdc, &e, brush);
        e = rc; e.left = e.right - w; FillRect(hdc, &e, brush);
    }
    return TRUE;
}
HRGN WINAPI CreatePolygonRgn(const POINT* pts, INT n, INT mode)
{
    // scanline fill of the polygon into a region
    HRGN out = CreateRectRgn(0, 0, 0, 0);
    w32::Region* rg = w32::as<w32::Region>(out, w32::T_REGION);
    if (!rg || n < 3) return out;
    LONG miny = pts[0].y, maxy = pts[0].y;
    for (int i = 1; i < n; i++) { if (pts[i].y < miny) miny = pts[i].y; if (pts[i].y > maxy) maxy = pts[i].y; }
    std::vector<LONG> xs;
    for (LONG y = miny; y < maxy; y++) {
        xs.clear();
        for (int i = 0; i < n; i++) {
            const POINT& a = pts[i]; const POINT& b = pts[(i + 1) % n];
            if (a.y == b.y) continue;
            LONG y0 = a.y < b.y ? a.y : b.y, y1 = a.y < b.y ? b.y : a.y;
            if (y < y0 || y >= y1) continue;
            // x at scanline centre
            double t = (y + 0.5 - a.y) / (double)(b.y - a.y);
            xs.push_back((LONG)(a.x + t * (b.x - a.x) + 0.5));
        }
        std::sort(xs.begin(), xs.end());
        if (mode == WINDING) {
            // approximate winding with alternate for now
        }
        for (size_t i = 0; i + 1 < xs.size(); i += 2)
            if (xs[i + 1] > xs[i]) rg->rgn.add(gdi::Rect{xs[i], y, xs[i + 1], y + 1});
    }
    return out;
}

// ---- icons -------------------------------------------------------------------
HICON WINAPI CopyIcon(HICON h) { return (HICON)CopyImage(h, IMAGE_ICON, 0, 0, 0); }

} // extern "C"
