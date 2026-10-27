// The 16-bit wide CRT that the vendored Wine code calls by its MSVCRT names
// (see include/flxp_crt.h), the wsprintf family, and the UTF-8/UTF-16
// helpers of w32.h. printf/scanf follow MSVCRT: in the wide functions %s and
// %c are wide and %S/%hs/%hc narrow (UTF-8); %ld is 32 bits.
#include <errno.h>
#include <stdio.h>
#include <type_traits>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "w32/w32.h"
#include "w32/kernel.h"

typedef unsigned short wch;

// ---- UTF-8 / UTF-16 ----

namespace {

// Decodes one code point from UTF-8; invalid sequences yield U+FFFD and
// consume one byte.
uint32_t utf8_next(const unsigned char*& p, const unsigned char* end)
{
    uint32_t c = *p++;
    if (c < 0x80) return c;
    int n; uint32_t min;
    if ((c & 0xe0) == 0xc0) { n = 1; c &= 0x1f; min = 0x80; }
    else if ((c & 0xf0) == 0xe0) { n = 2; c &= 0x0f; min = 0x800; }
    else if ((c & 0xf8) == 0xf0) { n = 3; c &= 0x07; min = 0x10000; }
    else return 0xfffd;
    if (end - p < n) return 0xfffd;
    for (int i = 0; i < n; i++)
        if ((p[i] & 0xc0) != 0x80) return 0xfffd;
    for (int i = 0; i < n; i++) c = (c << 6) | (*p++ & 0x3f);
    if (c < min || c > 0x10ffff || (c >= 0xd800 && c < 0xe000)) return 0xfffd;
    return c;
}

// Decodes one code point from UTF-16; lone surrogates yield U+FFFD.
uint32_t utf16_next(const wch*& p, const wch* end)
{
    uint32_t c = *p++;
    if (c >= 0xd800 && c < 0xdc00) {
        if (p < end && *p >= 0xdc00 && *p < 0xe000) return 0x10000 + ((c - 0xd800) << 10) + (*p++ - 0xdc00);
        return 0xfffd;
    }
    if (c >= 0xdc00 && c < 0xe000) return 0xfffd;
    return c;
}

void put_utf8(std::string& s, uint32_t c)
{
    if (c < 0x80) s += (char)c;
    else if (c < 0x800) { s += (char)(0xc0 | c >> 6); s += (char)(0x80 | (c & 0x3f)); }
    else if (c < 0x10000) { s += (char)(0xe0 | c >> 12); s += (char)(0x80 | ((c >> 6) & 0x3f)); s += (char)(0x80 | (c & 0x3f)); }
    else { s += (char)(0xf0 | c >> 18); s += (char)(0x80 | ((c >> 12) & 0x3f)); s += (char)(0x80 | ((c >> 6) & 0x3f)); s += (char)(0x80 | (c & 0x3f)); }
}

void put_utf16(std::u16string& s, uint32_t c)
{
    if (c < 0x10000) s += (char16_t)c;
    else { c -= 0x10000; s += (char16_t)(0xd800 + (c >> 10)); s += (char16_t)(0xdc00 + (c & 0x3ff)); }
}

size_t wlen(const wch* s) { size_t n = 0; while (s[n]) n++; return n; }

} // namespace

namespace w32 {

int utf16_len(const WCHAR* s) { return s ? (int)wlen((const wch*)s) : 0; }

std::string utf8_of(const WCHAR* s, int len)
{
    std::string out;
    if (!s) return out;
    const wch* p = (const wch*)s;
    const wch* end = p + (len < 0 ? wlen(p) : (size_t)len);
    while (p < end) put_utf8(out, utf16_next(p, end));
    return out;
}

std::u16string utf16_of(const char* s, int len)
{
    std::u16string out;
    if (!s) return out;
    const unsigned char* p = (const unsigned char*)s;
    const unsigned char* end = p + (len < 0 ? strlen(s) : (size_t)len);
    while (p < end) put_utf16(out, utf8_next(p, end));
    return out;
}

// ASCII, Latin-1 and Latin Extended-A.
WCHAR wupper(WCHAR wc)
{
    wch c = (wch)wc;
    if (c < 0x80) return (c >= 'a' && c <= 'z') ? c - 32 : c;
    if (c < 0x100) {
        if (c >= 0xe0 && c <= 0xfe && c != 0xf7) return c - 32;
        if (c == 0xff) return 0x178;
        if (c == 0xb5) return 0x39c;
        return c;
    }
    if (c < 0x180) {
        if ((c >= 0x100 && c <= 0x137) || (c >= 0x14a && c <= 0x177)) return c & ~1;
        if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17e)) return (c & 1) ? c : c - 1;
        if (c == 0x131) return 'I';
        if (c == 0x17f) return 'S';
        return c;
    }
    if (c >= 0x3b1 && c <= 0x3c9 && c != 0x3c2) return c - 32;     // Greek
    if (c >= 0x430 && c <= 0x44f) return c - 32;                   // Cyrillic
    if (c >= 0x450 && c <= 0x45f) return c - 80;
    return c;
}

WCHAR wlower(WCHAR wc)
{
    wch c = (wch)wc;
    if (c < 0x80) return (c >= 'A' && c <= 'Z') ? c + 32 : c;
    if (c < 0x100) {
        if (c >= 0xc0 && c <= 0xde && c != 0xd7) return c + 32;
        return c;
    }
    if (c < 0x180) {
        if ((c >= 0x100 && c <= 0x137 && c != 0x130) || (c >= 0x14a && c <= 0x177)) return c | 1;
        if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17e)) return (c & 1) ? c + 1 : c;
        if (c == 0x130) return 'i';
        if (c == 0x178) return 0xff;
        return c;
    }
    if (c >= 0x391 && c <= 0x3a9 && c != 0x3a2) return c + 32;
    if (c >= 0x410 && c <= 0x42f) return c + 32;
    if (c >= 0x400 && c <= 0x40f) return c + 80;
    return c;
}

} // namespace w32

// ---- string functions ----

extern "C" {

size_t flxp_wcslen(const wch* s) { return wlen(s); }

wch* flxp_wcscpy(wch* d, const wch* s)
{
    wch* r = d;
    while ((*d++ = *s++)) {}
    return r;
}

wch* flxp_wcsncpy(wch* d, const wch* s, size_t n)
{
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}

wch* flxp_wcscat(wch* d, const wch* s)
{
    flxp_wcscpy(d + wlen(d), s);
    return d;
}

int flxp_wcscmp(const wch* a, const wch* b)
{
    while (*a && *a == *b) a++, b++;
    return (int)*a - (int)*b;
}

int flxp_wcsncmp(const wch* a, const wch* b, size_t n)
{
    if (!n) return 0;
    while (--n && *a && *a == *b) a++, b++;
    return (int)*a - (int)*b;
}

int flxp_wcsicmp(const wch* a, const wch* b)
{
    for (;;) {
        int ca = (wch)w32::wlower(*a), cb = (wch)w32::wlower(*b);
        if (ca != cb || !ca) return ca - cb;
        a++, b++;
    }
}

int flxp_wcsnicmp(const wch* a, const wch* b, size_t n)
{
    for (; n; n--, a++, b++) {
        int ca = (wch)w32::wlower(*a), cb = (wch)w32::wlower(*b);
        if (ca != cb || !ca) return ca - cb;
    }
    return 0;
}

wch* flxp_wcschr(const wch* s, wch c)
{
    for (;; s++) {
        if (*s == c) return (wch*)s;
        if (!*s) return nullptr;
    }
}

wch* flxp_wcsrchr(const wch* s, wch c)
{
    const wch* r = nullptr;
    for (;; s++) {
        if (*s == c) r = s;
        if (!*s) return (wch*)r;
    }
}

wch* flxp_wcsstr(const wch* s, const wch* sub)
{
    size_t n = wlen(sub);
    if (!n) return (wch*)s;
    for (; *s; s++)
        if (*s == *sub && !flxp_wcsncmp(s, sub, n)) return (wch*)s;
    return nullptr;
}

wch* flxp_wcsdup(const wch* s)
{
    if (!s) return nullptr;
    size_t n = (wlen(s) + 1) * sizeof(wch);
    wch* r = (wch*)malloc(n);
    if (r) memcpy(r, s, n);
    return r;
}

wch* flxp_wcsinc(const wch* s) { return (wch*)(s + 1); }

wch flxp_towupper(wch c) { return (wch)w32::wupper(c); }
wch flxp_towlower(wch c) { return (wch)w32::wlower(c); }

int flxp_iswspace(wch c)
{
    return (c >= 9 && c <= 13) || c == ' ' || c == 0x85 || c == 0xa0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f ||
           c == 0x205f || c == 0x3000;
}

int flxp_iswdigit(wch c) { return c >= '0' && c <= '9'; }

int flxp_iswalpha(wch c)
{
    if (c < 0x80) return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    if (c < 0x100) return c >= 0xc0 && c != 0xd7 && c != 0xf7 ? 1 : (c == 0xaa || c == 0xb5 || c == 0xba);
    // Outside Latin-1: letters unless obviously punctuation/space/symbols.
    if (flxp_iswspace(c)) return 0;
    if (c >= 0x2000 && c <= 0x2bff) return 0;
    if (c >= 0x3000 && c <= 0x303f) return 0;
    if (c >= 0xd800 && c <= 0xdfff) return 0;
    if (c >= 0xff00 && c <= 0xff20) return 0;
    return 1;
}

int flxp_iswalnum(wch c) { return flxp_iswdigit(c) || flxp_iswalpha(c); }

} // extern "C"

namespace {

int digit_val(unsigned c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 99;
}

// Shared by wcstol/wcstoul: parses sign, prefix and digits. Returns false if
// no digits were found (end then points at the start).
bool parse_ul(const wch* s, int base, const wch** end, unsigned long long& mag, bool& neg, bool& overflow)
{
    const wch* p = s;
    while (flxp_iswspace(*p)) p++;
    neg = false; overflow = false; mag = 0;
    if (*p == '-' || *p == '+') neg = *p++ == '-';
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && digit_val(p[2]) < 16) {
        p += 2; base = 16;
    } else if (base == 0) base = p[0] == '0' ? 8 : 10;
    const wch* start = p;
    while (digit_val(*p) < base) {
        unsigned long long n = mag * base + digit_val(*p);
        if (n / base != mag && !overflow) overflow = true;
        if (mag > (~0ull - digit_val(*p)) / base) overflow = true;
        mag = n;
        p++;
    }
    if (p == start) { *end = s; return false; }
    *end = p;
    return true;
}

} // namespace

extern "C" {

long flxp_wcstol(const wch* s, wch** end, int base)
{
    const wch* e; unsigned long long m; bool neg, ovf;
    if (!parse_ul(s, base, &e, m, neg, ovf)) { if (end) *end = (wch*)s; return 0; }
    if (end) *end = (wch*)e;
    // MSVCRT long is 32 bits; clamp to that range.
    if (!neg && (ovf || m > 0x7fffffffull)) { errno = ERANGE; return 0x7fffffffL; }
    if (neg && (ovf || m > 0x80000000ull)) { errno = ERANGE; return -0x7fffffffL - 1; }
    return neg ? -(long)m : (long)m;
}

unsigned long flxp_wcstoul(const wch* s, wch** end, int base)
{
    const wch* e; unsigned long long m; bool neg, ovf;
    if (!parse_ul(s, base, &e, m, neg, ovf)) { if (end) *end = (wch*)s; return 0; }
    if (end) *end = (wch*)e;
    if (ovf || m > 0xffffffffull) { errno = ERANGE; return 0xffffffffUL; }
    uint32_t v = (uint32_t)m;
    return neg ? (uint32_t)-v : v;
}

int flxp_wtoi(const wch* s) { return (int)flxp_wcstol(s, nullptr, 10); }

} // extern "C"

// ---- printf engine ----

namespace {

// Output sinks. Wide sinks take UTF-16 units; narrow sinks take UTF-8 bytes.
struct WideOut {
    wch* buf; size_t cap; size_t n = 0;
    void unit(uint32_t u) { if (n < cap) buf[n] = (wch)u; n++; }
    void wide(const wch* s, size_t len) { for (size_t i = 0; i < len; i++) unit(s[i]); }
    void narrow(const char* s, size_t len)
    {
        const unsigned char* p = (const unsigned char*)s, *e = p + len;
        while (p < e) {
            uint32_t c = utf8_next(p, e);
            if (c >= 0x10000) { c -= 0x10000; unit(0xd800 + (c >> 10)); unit(0xdc00 + (c & 0x3ff)); }
            else unit(c);
        }
    }
};

struct NarrowOut {
    char* buf; size_t cap; size_t n = 0;
    void unit(uint32_t u) { if (n < cap) buf[n] = (char)u; n++; }
    void narrow(const char* s, size_t len) { for (size_t i = 0; i < len; i++) unit((unsigned char)s[i]); }
    void wide(const wch* s, size_t len)
    {
        std::string t;
        const wch* p = s, *e = s + len;
        while (p < e) put_utf8(t, utf16_next(p, e));
        narrow(t.data(), t.size());
    }
};

// Argument fetchers over a System V va_list or an ms_abi va_list.
struct SysvArgs {
    va_list* ap;
    int i() { return va_arg(*ap, int); }
    long long ll() { return va_arg(*ap, long long); }
    double d() { return va_arg(*ap, double); }
    void* p() { return va_arg(*ap, void*); }
};
struct MsArgs {
    __builtin_ms_va_list ap;
    int i() { return __builtin_va_arg(ap, int); }
    long long ll() { return __builtin_va_arg(ap, long long); }
    double d() { return __builtin_va_arg(ap, double); }
    void* p() { return __builtin_va_arg(ap, void*); }
};

enum { F_LEFT = 1, F_PLUS = 2, F_SPACE = 4, F_ALT = 8, F_ZERO = 16 };

template <class Out> void pad(Out& o, int n, char c) { while (n-- > 0) o.unit(c); }

// Emits an ASCII body with width/flags handling. `prefix` (sign, 0x) goes
// before zero padding.
template <class Out>
void emit_padded(Out& o, const char* prefix, const char* body, int width, int flags, bool zero_ok)
{
    int lp = (int)strlen(prefix), lb = (int)strlen(body);
    int fill = width - lp - lb;
    if (flags & F_LEFT) { o.narrow(prefix, lp); o.narrow(body, lb); pad(o, fill, ' '); }
    else if ((flags & F_ZERO) && zero_ok) { o.narrow(prefix, lp); pad(o, fill, '0'); o.narrow(body, lb); }
    else { pad(o, fill, ' '); o.narrow(prefix, lp); o.narrow(body, lb); }
}

template <class F> unsigned fch(const F* f) { return (unsigned)(typename std::make_unsigned<F>::type)*f; }

// wide_default: %s/%c are wide (the wide functions) or narrow (wsprintfA).
template <class F, class Out, class A>
void format(Out& o, const F* fmt, A& a, bool wide_default)
{
    while (*fmt) {
        if (fch(fmt) != '%') { o.unit(fch(fmt)); fmt++; continue; }
        const F* start = fmt;
        fmt++;
        int flags = 0;
        for (;; fmt++) {
            unsigned c = fch(fmt);
            if (c == '-') flags |= F_LEFT;
            else if (c == '+') flags |= F_PLUS;
            else if (c == ' ') flags |= F_SPACE;
            else if (c == '#') flags |= F_ALT;
            else if (c == '0') flags |= F_ZERO;
            else break;
        }
        int width = 0, prec = -1;
        if (fch(fmt) == '*') { width = a.i(); fmt++; if (width < 0) { flags |= F_LEFT; width = -width; } }
        else while (fch(fmt) >= '0' && fch(fmt) <= '9') width = width * 10 + (fch(fmt++) - '0');
        if (fch(fmt) == '.') {
            fmt++; prec = 0;
            if (fch(fmt) == '*') { prec = a.i(); fmt++; if (prec < 0) prec = -1; }
            else while (fch(fmt) >= '0' && fch(fmt) <= '9') prec = prec * 10 + (fch(fmt++) - '0');
        }
        // size: 0 default, 'h' short/narrow, 'l' long(32)/wide, 'L' 64, 'P' pointer-sized
        int size = 0;
        bool hh = false;
        for (;;) {
            unsigned c = fch(fmt);
            if (c == 'h') { if (size == 'h') hh = true; size = 'h'; fmt++; }
            else if (c == 'l') { size = size == 'l' ? 'L' : 'l'; fmt++; }
            else if (c == 'w') { size = 'l'; fmt++; }
            else if (c == 'L' || c == 'q' || c == 'j') { size = 'L'; fmt++; }
            else if (c == 'z' || c == 't') { size = 'P'; fmt++; }
            else if (c == 'I') {
                if (fch(fmt + 1) == '6' && fch(fmt + 2) == '4') { size = 'L'; fmt += 3; }
                else if (fch(fmt + 1) == '3' && fch(fmt + 2) == '2') { size = 0; fmt += 3; }
                else { size = 'P'; fmt++; }
            }
            else break;
        }
        unsigned conv = fch(fmt);
        if (!conv) { for (const F* p = start; p < fmt; p++) o.unit(fch(p)); break; }
        fmt++;
        char body[512], prefix[4] = "";
        switch (conv) {
        case '%':
            o.unit('%');
            break;
        case 'c': case 'C': {
            bool wide = size == 'l' ? true : size == 'h' ? false : (conv == 'c' ? wide_default : !wide_default);
            int ch = a.i();
            if (!(flags & F_LEFT)) pad(o, width - 1, ' ');
            if (wide) { wch w = (wch)ch; o.wide(&w, 1); }
            else { char n = (char)ch; o.narrow(&n, 1); }
            if (flags & F_LEFT) pad(o, width - 1, ' ');
            break;
        }
        case 's': case 'S': {
            bool wide = size == 'l' ? true : size == 'h' ? false : (conv == 's' ? wide_default : !wide_default);
            const void* s = a.p();
            static const char nulls[] = "(null)";
            static const wch nullw[] = { '(', 'n', 'u', 'l', 'l', ')', 0 };
            if (!s) s = wide ? (const void*)nullw : (const void*)nulls;
            size_t len;
            if (wide) {
                const wch* w = (const wch*)s;
                for (len = 0; (prec < 0 || (int)len < prec) && w[len]; len++) {}
            } else {
                const char* n = (const char*)s;
                for (len = 0; (prec < 0 || (int)len < prec) && n[len]; len++) {}
            }
            int fill = width - (int)len;
            if (!(flags & F_LEFT)) pad(o, fill, ' ');
            if (wide) o.wide((const wch*)s, len);
            else o.narrow((const char*)s, len);
            if (flags & F_LEFT) pad(o, fill, ' ');
            break;
        }
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': case 'p': {
            unsigned long long v;
            bool neg = false;
            bool is_signed = conv == 'd' || conv == 'i';
            if (conv == 'p') { v = (uintptr_t)a.p(); if (prec < 0) prec = 2 * (int)sizeof(void*); }
            else if (size == 'L') { long long x = a.ll(); v = (unsigned long long)x; if (is_signed && x < 0) { neg = true; v = 0 - v; } }
            else if (size == 'P') { long long x = (long long)(intptr_t)a.p(); v = (unsigned long long)x; if (is_signed && x < 0) { neg = true; v = 0 - v; } }
            else {
                int x = a.i();
                if (size == 'h') x = hh ? (is_signed ? (int)(signed char)x : (int)(unsigned char)x)
                                        : (is_signed ? (int)(short)x : (int)(unsigned short)x);
                if (is_signed) { if (x < 0) { neg = true; v = 0 - (unsigned long long)(long long)x; } else v = (unsigned)x; }
                else v = (unsigned)x;
            }
            unsigned base = (conv == 'x' || conv == 'X' || conv == 'p') ? 16 : conv == 'o' ? 8 : 10;
            const char* digs = (conv == 'x') ? "0123456789abcdef" : "0123456789ABCDEF";
            char tmp[80]; int n = 0;
            while (v) { tmp[n++] = digs[v % base]; v /= base; }
            int p = prec < 0 ? 1 : prec;
            if (prec >= 0) flags &= ~F_ZERO;
            int bl = 0;
            for (int k = n; k < p && bl < 400; k++) body[bl++] = '0';
            if (conv == 'o' && (flags & F_ALT) && (bl == 0 && (n == 0 || tmp[n - 1] != '0'))) body[bl++] = '0';
            while (n) body[bl++] = tmp[--n];
            body[bl] = 0;
            if (neg) strcpy(prefix, "-");
            else if (is_signed && (flags & F_PLUS)) strcpy(prefix, "+");
            else if (is_signed && (flags & F_SPACE)) strcpy(prefix, " ");
            if ((flags & F_ALT) && base == 16 && conv != 'p' && bl && strcmp(body, "0") && strspn(body, "0") != (size_t)bl)
                strcpy(prefix, conv == 'x' ? "0x" : "0X");
            emit_padded(o, prefix, body, width, flags, true);
            break;
        }
        case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A': {
            char f[32]; int k = 0;
            f[k++] = '%';
            if (flags & F_LEFT) f[k++] = '-';
            if (flags & F_PLUS) f[k++] = '+';
            if (flags & F_SPACE) f[k++] = ' ';
            if (flags & F_ALT) f[k++] = '#';
            if (flags & F_ZERO) f[k++] = '0';
            k += snprintf(f + k, sizeof f - k, "%d", width);
            if (prec >= 0) k += snprintf(f + k, sizeof f - k, ".%d", prec > 300 ? 300 : prec);
            f[k++] = (char)conv; f[k] = 0;
            int bl = snprintf(body, sizeof body, f, a.d());
            o.narrow(body, bl < 0 ? 0 : (bl >= (int)sizeof body ? sizeof body - 1 : bl));
            break;
        }
        case 'n': {
            int* p = (int*)a.p();
            if (p) *p = (int)o.n;
            break;
        }
        default:
            for (const F* p = start; p < fmt; p++) o.unit(fch(p));
            break;
        }
    }
}

// Formats into a counted wide buffer; MSVCRT _vsnwprintf return convention
// (-1 if truncated) but the result is always terminated when count > 0.
template <class A> int wide_format(wch* buf, size_t count, const wch* fmt, A& a)
{
    WideOut o{buf, count};
    format(o, fmt, a, true);
    if (o.n < count) { buf[o.n] = 0; return (int)o.n; }
    if (count) buf[count - 1] = 0;
    return -1;
}

} // namespace

extern "C" {

int flxp_vswprintf(wch* buf, size_t count, const wch* fmt, va_list ap)
{
    va_list aq;
    va_copy(aq, ap);
    SysvArgs a{&aq};
    int r = wide_format(buf, count, fmt, a);
    va_end(aq);
    return r;
}

int flxp_swprintf(wch* buf, size_t count, const wch* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = flxp_vswprintf(buf, count, fmt, ap);
    va_end(ap);
    return r;
}

// user32's wsprintf: output buffer is assumed to hold 1024 characters.
INT WINAPI wvsprintfW(LPWSTR buf, LPCWSTR fmt, __ms_va_list ap)
{
    MsArgs a;
    __builtin_ms_va_copy(a.ap, ap);
    WideOut o{(wch*)buf, 1024};
    format(o, (const wch*)fmt, a, true);
    if (o.n < 1024) { buf[o.n] = 0; return (int)o.n; }
    buf[1023] = 0;
    return 1023;
}

INT WINAPIV wsprintfW(LPWSTR buf, LPCWSTR fmt, ...)
{
    __ms_va_list ap;
    __builtin_ms_va_start(ap, fmt);
    INT r = wvsprintfW(buf, fmt, ap);
    __builtin_ms_va_end(ap);
    return r;
}

INT WINAPI wvsprintfA(LPSTR buf, LPCSTR fmt, __ms_va_list ap)
{
    MsArgs a;
    __builtin_ms_va_copy(a.ap, ap);
    NarrowOut o{buf, 1024};
    format(o, fmt, a, false);
    if (o.n < 1024) { buf[o.n] = 0; return (int)o.n; }
    buf[1023] = 0;
    return 1023;
}

INT WINAPIV wsprintfA(LPSTR buf, LPCSTR fmt, ...)
{
    __ms_va_list ap;
    __builtin_ms_va_start(ap, fmt);
    INT r = wvsprintfA(buf, fmt, ap);
    __builtin_ms_va_end(ap);
    return r;
}

} // extern "C"

// ---- scanf ----

namespace {

bool in_scanset(const wch* set, const wch* set_end, bool negate, wch c)
{
    bool hit = false;
    for (const wch* p = set; p < set_end; p++) {
        if (p + 2 < set_end && p[1] == '-') { if (c >= p[0] && c <= p[2]) hit = true; p += 2; }
        else if (*p == c) hit = true;
    }
    return hit != negate;
}

int vscan(const wch* in, const wch* fmt, va_list ap)
{
    const wch* s = in;
    int count = 0;
    bool any_conv = false;
    while (*fmt) {
        if (flxp_iswspace(*fmt)) {
            while (flxp_iswspace(*fmt)) fmt++;
            while (flxp_iswspace(*s)) s++;
            continue;
        }
        if (*fmt != '%' || fmt[1] == '%') {
            if (*fmt == '%') { fmt++; while (flxp_iswspace(*s)) s++; }
            if (!*s) return any_conv || count ? count : -1;
            if (*s != *fmt) return count;
            s++, fmt++;
            continue;
        }
        fmt++;
        bool suppress = false;
        if (*fmt == '*') { suppress = true; fmt++; }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        int size = 0; bool hh = false;
        for (;;) {
            if (*fmt == 'h') { if (size == 'h') hh = true; size = 'h'; fmt++; }
            else if (*fmt == 'l') { size = size == 'l' ? 'L' : 'l'; fmt++; }
            else if (*fmt == 'w') { size = 'l'; fmt++; }
            else if (*fmt == 'L' || *fmt == 'q' || *fmt == 'j') { size = 'L'; fmt++; }
            else if (*fmt == 'z' || *fmt == 't') { size = 'P'; fmt++; }
            else if (*fmt == 'I') {
                if (fmt[1] == '6' && fmt[2] == '4') { size = 'L'; fmt += 3; }
                else if (fmt[1] == '3' && fmt[2] == '2') { size = 0; fmt += 3; }
                else { size = 'P'; fmt++; }
            }
            else break;
        }
        wch conv = *fmt;
        if (!conv) break;
        fmt++;
        if (conv != 'c' && conv != 'C' && conv != '[' && conv != 'n')
            while (flxp_iswspace(*s)) s++;
        if (conv != 'n' && !*s) return any_conv || count ? count : -1;
        any_conv = true;
        switch (conv) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': case 'p': {
            int base = (conv == 'd' || conv == 'u') ? 10 : (conv == 'x' || conv == 'X' || conv == 'p') ? 16 : conv == 'o' ? 8 : 0;
            int lim = width ? width : 0x7fffffff;
            const wch* p = s;
            bool neg = false;
            if (lim && (*p == '-' || *p == '+')) { neg = *p == '-'; p++; lim--; }
            if ((base == 0 || base == 16) && lim >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { p += 2; lim -= 2; base = 16; }
            else if (base == 0) base = (lim && p[0] == '0') ? 8 : 10;
            unsigned long long v = 0;
            const wch* ds = p;
            while (lim && digit_val(*p) < base) { v = v * base + digit_val(*p); p++; lim--; }
            if (p == ds) return count;
            s = p;
            if (neg) v = 0 - v;
            if (!suppress) {
                void* dst = va_arg(ap, void*);
                if (conv == 'p' || size == 'P') *(uintptr_t*)dst = (uintptr_t)v;
                else if (size == 'L') *(unsigned long long*)dst = v;
                else if (size == 'h') { if (hh) *(unsigned char*)dst = (unsigned char)v; else *(unsigned short*)dst = (unsigned short)v; }
                else *(unsigned int*)dst = (unsigned int)v;
                count++;
            }
            break;
        }
        case 'e': case 'E': case 'f': case 'g': case 'G': case 'a': {
            char tmp[128]; int n = 0;
            int lim = width ? width : 127;
            while (n < lim && n < 127 && s[n] && s[n] < 128 && !flxp_iswspace(s[n])) { tmp[n] = (char)s[n]; n++; }
            tmp[n] = 0;
            char* e;
            double d = strtod(tmp, &e);
            if (e == tmp) return count;
            s += e - tmp;
            if (!suppress) {
                void* dst = va_arg(ap, void*);
                if (size == 'l' || size == 'L') *(double*)dst = d; else *(float*)dst = (float)d;
                count++;
            }
            break;
        }
        case 's': case 'S': case 'c': case 'C': case '[': {
            bool wide = (conv == 's' || conv == 'c' || conv == '[') ? size != 'h' : size == 'l';
            const wch* set = nullptr, *set_end = nullptr; bool negate = false;
            if (conv == '[') {
                if (*fmt == '^') { negate = true; fmt++; }
                set = fmt;
                if (*fmt == ']') fmt++;
                while (*fmt && *fmt != ']') fmt++;
                set_end = fmt;
                if (*fmt) fmt++;
            }
            int lim = width ? width : ((conv == 'c' || conv == 'C') ? 1 : 0x7fffffff);
            const wch* p = s;
            while (lim && *p) {
                if ((conv == 's' || conv == 'S') && flxp_iswspace(*p)) break;
                if (conv == '[' && !in_scanset(set, set_end, negate, *p)) break;
                p++; lim--;
            }
            if (p == s) return count;
            if (!suppress) {
                void* dst = va_arg(ap, void*);
                if (wide) {
                    wch* d = (wch*)dst;
                    memcpy(d, s, (p - s) * sizeof(wch));
                    if (conv != 'c' && conv != 'C') d[p - s] = 0;
                } else {
                    std::string t;
                    const wch* q = s;
                    while (q < p) put_utf8(t, utf16_next(q, p));
                    memcpy(dst, t.data(), t.size());
                    if (conv != 'c' && conv != 'C') ((char*)dst)[t.size()] = 0;
                }
                count++;
            }
            s = p;
            break;
        }
        case 'n':
            if (!suppress) *va_arg(ap, int*) = (int)(s - in);
            break;
        default:
            return count;
        }
    }
    return count;
}

} // namespace

extern "C" int flxp_swscanf(const wch* in, const wch* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vscan(in, fmt, ap);
    va_end(ap);
    return r;
}
