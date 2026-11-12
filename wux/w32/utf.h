// wux's one UTF-8 <-> UTF-16 conversion pair. "ANSI" is UTF-8 in wux
// (GetACP() = CP_UTF8), so every A function, the resource reader, GDI's text
// and the keyboard's text go through these two. Plain C++ (no Win32 headers),
// so the native toolkit (wux/res, wux/win) uses them as the shim does; w32.h
// adds the WCHAR overloads.
//
// Invalid input never stops a conversion: a bad UTF-8 sequence becomes U+FFFD
// and consumes one byte, a lone surrogate becomes U+FFFD; a code point above
// U+FFFF becomes a surrogate pair and back.
#pragma once
#include <stdint.h>
#include <string>

namespace w32 {

std::string utf8_of(const char16_t* s, int len = -1);   // len -1: up to the NUL
std::u16string utf16_of(const char* s, int len = -1);
inline std::string utf8_of(const std::u16string& s) { return utf8_of(s.data(), (int)s.size()); }
inline std::u16string utf16_of(const std::string& s) { return utf16_of(s.data(), (int)s.size()); }

} // namespace w32
