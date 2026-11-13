// wux's UTF-8 <-> UTF-16 pair (utf.h). Compiled with the shim's flags but
// needs nothing of it: the toolkit tests link it beside the native objects.
#include <string.h>
#include "w32/utf.h"

namespace {

// One code point from UTF-8; an invalid sequence yields U+FFFD and consumes one byte.
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

// One code point from UTF-16; a lone surrogate yields U+FFFD.
uint32_t utf16_next(const char16_t*& p, const char16_t* end)
{
    uint32_t c = *p++;
    if (c >= 0xd800 && c < 0xdc00) {
        if (p < end && *p >= 0xdc00 && *p < 0xe000) return 0x10000 + ((c - 0xd800) << 10) + (*p++ - 0xdc00);
        return 0xfffd;
    }
    if (c >= 0xdc00 && c < 0xe000) return 0xfffd;
    return c;
}

} // namespace

namespace w32 {

std::string utf8_of(const char16_t* s, int len)
{
    std::string out;
    if (!s) return out;
    size_t n = 0;
    if (len < 0) while (s[n]) n++;
    else n = (size_t)len;
    const char16_t* p = s, *end = s + n;
    out.reserve(n);
    while (p < end) {
        uint32_t c = utf16_next(p, end);
        if (c < 0x80) out += (char)c;
        else if (c < 0x800) { out += (char)(0xc0 | c >> 6); out += (char)(0x80 | (c & 0x3f)); }
        else if (c < 0x10000) { out += (char)(0xe0 | c >> 12); out += (char)(0x80 | ((c >> 6) & 0x3f)); out += (char)(0x80 | (c & 0x3f)); }
        else {
            out += (char)(0xf0 | c >> 18); out += (char)(0x80 | ((c >> 12) & 0x3f));
            out += (char)(0x80 | ((c >> 6) & 0x3f)); out += (char)(0x80 | (c & 0x3f));
        }
    }
    return out;
}

std::u16string utf16_of(const char* s, int len)
{
    std::u16string out;
    if (!s) return out;
    const unsigned char* p = (const unsigned char*)s;
    const unsigned char* end = p + (len < 0 ? strlen(s) : (size_t)len);
    out.reserve((size_t)(end - p));
    while (p < end) {
        uint32_t c = utf8_next(p, end);
        if (c < 0x10000) out += (char16_t)c;
        else { c -= 0x10000; out += (char16_t)(0xd800 + (c >> 10)); out += (char16_t)(0xdc00 + (c & 0x3ff)); }
    }
    return out;
}

} // namespace w32
