/* Force-included into every vendored Wine C file. Wine's code calls the
 * CRT's 16-bit wide string functions by their MSVCRT names; on this side
 * wchar_t is 16 bits only inside the vendored code (-fshort-wchar), so the
 * names are redirected to our own implementations in src/w32/crt.cpp. */
#ifndef FLXP_CRT_H
#define FLXP_CRT_H
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

/* The renames apply to the vendored C only: C++ shim code (also compiled
 * with this header force-included) uses the flxp_ names directly, and
 * renaming would clash with glibc's wchar.h declarations. */
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
#endif /* !__cplusplus */
#endif
