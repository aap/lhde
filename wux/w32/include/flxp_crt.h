/* Force-included into every file compiled with the shim's flags (W32FLAGS:
 * the vendored Wine C, the shim, flxp's and the applications' win32 code).
 * There wchar_t is 16 bits (-fshort-wchar) while glibc's wide functions take
 * 32-bit units, so none of glibc's may be called: Wine's C calls the CRT's
 * 16-bit wide string functions by their MSVCRT names, which are redirected
 * to our own implementations in wux/w32/crt.cpp; in C++ the names are
 * poisoned instead, so a stray wcslen or swprintf fails the build (C++ code
 * calls the flxp_ names, or better w32::utf8_of / utf16_of and std::u16string). */
#ifndef FLXP_CRT_H
#define FLXP_CRT_H
#if defined(__FreeBSD__) && !defined(__cplusplus)
/* FreeBSD's headers type wchar_t in C as int whatever -fshort-wchar says; L"..." is then
 * unsigned short[], and WCHAR (wchar_t under WINE_UNICODE_NATIVE) must be the same type */
typedef __WCHAR_TYPE__ wchar_t;
#define _WCHAR_T_DECLARED
#endif
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef unsigned short flxp_wch;
size_t     flxp_wcslen(const flxp_wch*);
flxp_wch*  flxp_wcscpy(flxp_wch*, const flxp_wch*);
flxp_wch*  flxp_wcsncpy(flxp_wch*, const flxp_wch*, size_t);
flxp_wch*  flxp_wcscat(flxp_wch*, const flxp_wch*);
int        flxp_wcscmp(const flxp_wch*, const flxp_wch*);
int        flxp_wcsncmp(const flxp_wch*, const flxp_wch*, size_t);
int        flxp_wcsicmp(const flxp_wch*, const flxp_wch*);
int        flxp_wcsnicmp(const flxp_wch*, const flxp_wch*, size_t);
flxp_wch*  flxp_wcschr(const flxp_wch*, flxp_wch);
flxp_wch*  flxp_wcsrchr(const flxp_wch*, flxp_wch);
flxp_wch*  flxp_wcsstr(const flxp_wch*, const flxp_wch*);
flxp_wch*  flxp_wcsdup(const flxp_wch*);
flxp_wch*  flxp_wcsinc(const flxp_wch*);
long       flxp_wcstol(const flxp_wch*, flxp_wch**, int);
unsigned long flxp_wcstoul(const flxp_wch*, flxp_wch**, int);
int        flxp_wtoi(const flxp_wch*);
flxp_wch   flxp_towupper(flxp_wch);
flxp_wch   flxp_towlower(flxp_wch);
int        flxp_iswspace(flxp_wch);
int        flxp_iswdigit(flxp_wch);
int        flxp_iswalpha(flxp_wch);
int        flxp_iswalnum(flxp_wch);
int        flxp_swprintf(flxp_wch*, size_t, const flxp_wch*, ...);
int        flxp_vswprintf(flxp_wch*, size_t, const flxp_wch*, va_list);
int        flxp_swscanf(const flxp_wch*, const flxp_wch*, ...);
#ifdef __cplusplus
}
#endif

#ifndef __cplusplus
#define wcslen     flxp_wcslen
#define wcscpy     flxp_wcscpy
#define wcsncpy    flxp_wcsncpy
#define wcscat     flxp_wcscat
#define wcscmp     flxp_wcscmp
#define wcsncmp    flxp_wcsncmp
#define _wcsicmp   flxp_wcsicmp
#define wcsicmp    flxp_wcsicmp
#define _wcsnicmp  flxp_wcsnicmp
#define wcsnicmp   flxp_wcsnicmp
#define wcschr     flxp_wcschr
#define wcsrchr    flxp_wcsrchr
#define wcsstr     flxp_wcsstr
#define wcsdup     flxp_wcsdup
#define _wcsdup    flxp_wcsdup
#define _wcsinc    flxp_wcsinc
#define wcstol     flxp_wcstol
#define wcstoul    flxp_wcstoul
#define _wtoi      flxp_wtoi
#define towupper   flxp_towupper
#define towlower   flxp_towlower
#define iswspace   flxp_iswspace
#define iswdigit   flxp_iswdigit
#define iswalpha   flxp_iswalpha
#define iswalnum   flxp_iswalnum
#define swprintf   flxp_swprintf
#define _snwprintf flxp_swprintf
#define vswprintf  flxp_vswprintf
#define swscanf    flxp_swscanf
#else
/* C++: glibc's declarations and libstdc++'s uses of them (cwchar, cwctype, char_traits, basic_string's
 * to_wstring and stoi) come first, then the names are poisoned for everything after this header, so the
 * standard headers a file includes later are already in and only the file's own code can hit a poisoned
 * name. A renaming macro, as in C, would clash with those declarations. std::wstring is no better (its
 * char_traits call glibc's 32-bit wcslen) but cannot be poisoned without the standard headers. */
#include <wchar.h>
#include <wctype.h>
#include <cwchar>
#include <cwctype>
#include <string>
#pragma GCC poison wcslen wcscpy wcsncpy wcscat wcsncat wcscmp wcsncmp wcsicmp _wcsicmp wcsnicmp _wcsnicmp \
    wcschr wcsrchr wcsstr wcsdup _wcsdup wcstol wcstoul wcstoll wcstoull wcstod _wtoi \
    towupper towlower iswspace iswdigit iswalpha iswalnum iswupper iswlower iswpunct iswprint \
    swprintf _snwprintf vswprintf swscanf wprintf fwprintf \
    wmemcpy wmemmove wmemset wmemcmp wmemchr
#endif
#endif
