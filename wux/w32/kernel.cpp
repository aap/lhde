// kernel32 (and the kernel-ish parts of user32/advapi32/comctl32) for the
// win32 shim: heaps, code pages (the "ANSI" code page is UTF-8), strings,
// atoms, version/locale, files over POSIX, modules and resources via
// res::Module, and an in-memory registry.
//
// Wine source code is compiled with __WINESRC__, which inlines
// GetLastError/SetLastError/GetProcessHeap/GetCurrentThreadId as reads of
// the TEB through NtCurrentTeb() (%gs:0x30 on x86-64), and __TRY uses the
// TEB's exception list. So a fake TEB/PEB is installed as the GS base at
// startup.
#define WINE_NO_INLINE_STRING     // so the lstr* functions can be defined here
#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#if defined(__x86_64__)
#include <asm/prctl.h>
#endif
#include <map>
#include "res/res.h"
#include "w32/w32.h"
extern "C" {
#include "winnls.h"
#include "winreg.h"
}
#include "w32/kernel.h"
#include "sys/sys.h"

using w32::utf8_of;
using w32::utf16_of;

// ---- fake TEB ----

namespace {

alignas(64) void* teb_block[0x400];      // 8 KiB, zeroed
alignas(64) void* peb_block[0x80];
int process_heap_dummy;

void install_teb(void** teb, void** peb, uintptr_t tid)
{
    teb[0x30 / 8] = teb;                                // NT_TIB.Self
    teb[8] = (void*)(uintptr_t)getpid();                // ClientId.UniqueProcess
    teb[9] = (void*)tid;                                // ClientId.UniqueThread
    teb[12] = peb;                                      // Peb
    peb[6] = &process_heap_dummy;                       // Peb->ProcessHeap
#if defined(__x86_64__)
    if (syscall(SYS_arch_prctl, ARCH_SET_GS, (unsigned long)teb) != 0)
        perror("flxp: arch_prctl(ARCH_SET_GS)");
#endif
}

__attribute__((constructor(101))) void init_teb()
{
    install_teb(teb_block, peb_block, 1);
}

thread_local bool teb_checked;

} // namespace

namespace w32 {

// libuxtheme can be called from any thread of its host: a thread whose GS
// base is not set yet gets a TEB of its own on its first call (never freed).
void ensure_teb()
{
    if (teb_checked) return;
    teb_checked = true;
#if defined(__x86_64__)
    unsigned long gs = 0;
    if (syscall(SYS_arch_prctl, ARCH_GET_GS, &gs) == 0 && gs) return;
    static uintptr_t next_tid = 2;
    void** teb = (void**)aligned_alloc(64, sizeof teb_block);
    memset(teb, 0, sizeof teb_block);
    install_teb(teb, peb_block, __atomic_fetch_add(&next_tid, 1, __ATOMIC_RELAXED));
#endif
}

} // namespace w32

// ---- memory ----

namespace {

const size_t MEM_MAGIC = 0x666c78706d656d31ull;
struct MemHdr { size_t size; size_t magic; };

MemHdr* hdr_of(void* p)
{
    MemHdr* h = (MemHdr*)p - 1;
    return h->magic == MEM_MAGIC ? h : nullptr;
}

void* mem_alloc(size_t n, bool zero)
{
    MemHdr* h = (MemHdr*)(zero ? calloc(1, sizeof(MemHdr) + n) : malloc(sizeof(MemHdr) + n));
    if (!h) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return nullptr; }
    h->size = n;
    h->magic = MEM_MAGIC;
    return h + 1;
}

void* mem_realloc(void* p, size_t n, bool zero)
{
    if (!p) return mem_alloc(n, zero);
    MemHdr* h = hdr_of(p);
    if (!h) return realloc(p, n);
    size_t old = h->size;
    h = (MemHdr*)realloc(h, sizeof(MemHdr) + n);
    if (!h) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return nullptr; }
    h->size = n;
    if (zero && n > old) memset((char*)(h + 1) + old, 0, n - old);
    return h + 1;
}

bool mem_free(void* p)
{
    if (!p) return true;
    MemHdr* h = hdr_of(p);
    if (!h) { free(p); return true; }
    h->magic = 0;
    free(h);
    return true;
}

size_t mem_size(void* p)
{
    MemHdr* h = p ? hdr_of(p) : nullptr;
    return h ? h->size : (size_t)-1;
}

} // namespace

extern "C" {

LPVOID WINAPI HeapAlloc(HANDLE, DWORD flags, SIZE_T n) { return mem_alloc(n, flags & HEAP_ZERO_MEMORY); }

LPVOID WINAPI HeapReAlloc(HANDLE, DWORD flags, LPVOID p, SIZE_T n)
{
    if (!p) return nullptr;
    if (flags & HEAP_REALLOC_IN_PLACE_ONLY) {
        MemHdr* h = hdr_of(p);
        if (!h || n > h->size) return nullptr;
        h->size = n;
        return p;
    }
    return mem_realloc(p, n, flags & HEAP_ZERO_MEMORY);
}

BOOL WINAPI HeapFree(HANDLE, DWORD, LPVOID p) { return mem_free(p); }
SIZE_T WINAPI HeapSize(HANDLE, DWORD, LPCVOID p) { return mem_size((void*)p); }

HGLOBAL WINAPI GlobalAlloc(UINT flags, SIZE_T n) { return mem_alloc(n, flags & GMEM_ZEROINIT); }
HGLOBAL WINAPI GlobalReAlloc(HGLOBAL h, SIZE_T n, UINT flags)
{
    if (flags & GMEM_MODIFY) return h;
    return mem_realloc(h, n, flags & GMEM_ZEROINIT);
}
HGLOBAL WINAPI GlobalFree(HGLOBAL h) { mem_free(h); return nullptr; }
LPVOID WINAPI GlobalLock(HGLOBAL h) { return h; }
BOOL WINAPI GlobalUnlock(HGLOBAL) { SetLastError(NO_ERROR); return FALSE; }
SIZE_T WINAPI GlobalSize(HGLOBAL h) { size_t n = mem_size(h); return n == (size_t)-1 ? 0 : n; }

HLOCAL WINAPI LocalAlloc(UINT flags, SIZE_T n) { return mem_alloc(n, flags & LMEM_ZEROINIT); }
HLOCAL WINAPI LocalReAlloc(HLOCAL h, SIZE_T n, UINT flags)
{
    if (flags & LMEM_MODIFY) return h;
    return mem_realloc(h, n, flags & LMEM_ZEROINIT);
}
HLOCAL WINAPI LocalFree(HLOCAL h) { mem_free(h); return nullptr; }
LPVOID WINAPI LocalLock(HLOCAL h) { return h; }
BOOL WINAPI LocalUnlock(HLOCAL) { SetLastError(NO_ERROR); return FALSE; }
SIZE_T WINAPI LocalSize(HLOCAL h) { size_t n = mem_size(h); return n == (size_t)-1 ? 0 : n; }

// comctl32's allocators (comctl32.h): zeroing.

} // extern "C"

// ---- code pages ----

namespace {

bool is_latin1_cp(UINT cp) { return cp == 1252 || cp == 28591 || cp == 20127 || cp == CP_SYMBOL; }

} // namespace

extern "C" {

UINT WINAPI GetACP(void) { return CP_UTF8; }
UINT WINAPI GetOEMCP(void) { return CP_UTF8; }

INT WINAPI MultiByteToWideChar(UINT cp, DWORD, LPCSTR src, INT srclen, LPWSTR dst, INT dstlen)
{
    if (!src || !srclen || dstlen < 0 || (dst && (const void*)dst == (const void*)src)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (srclen < 0) srclen = (int)strlen(src) + 1;
    std::u16string w;
    if (is_latin1_cp(cp)) { w.resize(srclen); for (int i = 0; i < srclen; i++) w[i] = (unsigned char)src[i]; }
    else w = utf16_of(src, srclen);
    int n = (int)w.size();
    if (!dstlen) return n;
    if (n > dstlen) {
        memcpy(dst, w.data(), dstlen * sizeof(WCHAR));
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    memcpy(dst, w.data(), n * sizeof(WCHAR));
    return n;
}

INT WINAPI WideCharToMultiByte(UINT cp, DWORD, LPCWSTR src, INT srclen, LPSTR dst, INT dstlen,
                               LPCSTR defchar, LPBOOL used_def)
{
    if (used_def) *used_def = FALSE;
    if (!src || !srclen || dstlen < 0 || (dst && (const void*)dst == (const void*)src)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (srclen < 0) srclen = w32::utf16_len(src) + 1;
    std::string s;
    if (is_latin1_cp(cp)) {
        s.resize(srclen);
        for (int i = 0; i < srclen; i++) {
            WCHAR c = src[i];
            if (c > 0xff) { s[i] = defchar ? *defchar : '?'; if (used_def) *used_def = TRUE; }
            else s[i] = (char)c;
        }
    } else s = utf8_of(src, srclen);
    int n = (int)s.size();
    if (!dstlen) return n;
    if (n > dstlen) {
        memcpy(dst, s.data(), dstlen);
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    memcpy(dst, s.data(), n);
    return n;
}

} // extern "C"

// ---- strings ----

namespace {

int wcmp_ci(const WCHAR* a, int la, const WCHAR* b, int lb)
{
    int n = la < lb ? la : lb;
    for (int i = 0; i < n; i++) {
        int ca = (unsigned short)w32::wlower(a[i]), cb = (unsigned short)w32::wlower(b[i]);
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    return la == lb ? 0 : (la < lb ? -1 : 1);
}

// A cheap stand-in for linguistic comparison: case-insensitive first, then
// lower case before upper case.
int wcmp_ling(const WCHAR* a, int la, const WCHAR* b, int lb, bool ignore_case)
{
    int r = wcmp_ci(a, la, b, lb);
    if (r || ignore_case) return r;
    int n = la < lb ? la : lb;
    for (int i = 0; i < n; i++) {
        if (a[i] == b[i]) continue;
        bool ua = a[i] != w32::wlower(a[i]);
        return ua ? 1 : -1;
    }
    return 0;
}

int cmp_ci_a(const char* a, const char* b)
{
    for (;; a++, b++) {
        int ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca < cb ? -1 : 1;
        if (!ca) return 0;
    }
}

} // namespace

extern "C" {

INT WINAPI lstrlenW(LPCWSTR s) { return w32::utf16_len(s); }
INT WINAPI lstrlenA(LPCSTR s) { return s ? (int)strlen(s) : 0; }

LPWSTR WINAPI lstrcpyW(LPWSTR d, LPCWSTR s)
{
    LPWSTR r = d;
    while ((*d++ = *s++)) {}
    return r;
}
LPSTR WINAPI lstrcpyA(LPSTR d, LPCSTR s) { return strcpy(d, s); }

LPWSTR WINAPI lstrcpynW(LPWSTR d, LPCWSTR s, INT n)
{
    LPWSTR r = d;
    if (n <= 0) return r;
    while (n > 1 && *s) { *d++ = *s++; n--; }
    *d = 0;
    return r;
}

LPSTR WINAPI lstrcpynA(LPSTR d, LPCSTR s, INT n)
{
    LPSTR r = d;
    if (n <= 0) return r;
    while (n > 1 && *s) { *d++ = *s++; n--; }
    *d = 0;
    return r;
}

LPWSTR WINAPI lstrcatW(LPWSTR d, LPCWSTR s) { lstrcpyW(d + lstrlenW(d), s); return d; }
LPSTR WINAPI lstrcatA(LPSTR d, LPCSTR s) { return strcat(d, s); }

INT WINAPI lstrcmpW(LPCWSTR a, LPCWSTR b)
{
    if (!a || !b) return a ? 1 : b ? -1 : 0;
    return wcmp_ling(a, lstrlenW(a), b, lstrlenW(b), false);
}

INT WINAPI lstrcmpiW(LPCWSTR a, LPCWSTR b)
{
    if (!a || !b) return a ? 1 : b ? -1 : 0;
    return wcmp_ci(a, lstrlenW(a), b, lstrlenW(b));
}

INT WINAPI lstrcmpA(LPCSTR a, LPCSTR b)
{
    if (!a || !b) return a ? 1 : b ? -1 : 0;
    std::u16string wa = utf16_of(a), wb = utf16_of(b);
    return wcmp_ling((const WCHAR*)wa.data(), (int)wa.size(), (const WCHAR*)wb.data(), (int)wb.size(), false);
}

INT WINAPI lstrcmpiA(LPCSTR a, LPCSTR b)
{
    if (!a || !b) return a ? 1 : b ? -1 : 0;
    return cmp_ci_a(a, b);
}

LPWSTR WINAPI CharUpperW(LPWSTR s)
{
    if (IS_INTRESOURCE(s)) return (LPWSTR)(UINT_PTR)(unsigned short)w32::wupper((WCHAR)(UINT_PTR)s);
    for (LPWSTR p = s; *p; p++) *p = w32::wupper(*p);
    return s;
}

LPWSTR WINAPI CharLowerW(LPWSTR s)
{
    if (IS_INTRESOURCE(s)) return (LPWSTR)(UINT_PTR)(unsigned short)w32::wlower((WCHAR)(UINT_PTR)s);
    for (LPWSTR p = s; *p; p++) *p = w32::wlower(*p);
    return s;
}

DWORD WINAPI CharUpperBuffW(LPWSTR s, DWORD n)
{
    if (!s) return 0;
    for (DWORD i = 0; i < n; i++) s[i] = w32::wupper(s[i]);
    return n;
}

DWORD WINAPI CharLowerBuffW(LPWSTR s, DWORD n)
{
    if (!s) return 0;
    for (DWORD i = 0; i < n; i++) s[i] = w32::wlower(s[i]);
    return n;
}

// UTF-8: only ASCII changes case (the byte length must stay the same).
LPSTR WINAPI CharUpperA(LPSTR s)
{
    if (IS_INTRESOURCE(s)) { UINT_PTR c = (UINT_PTR)s & 0xff; return (LPSTR)(c >= 'a' && c <= 'z' ? c - 32 : c); }
    for (char* p = s; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
    return s;
}

LPSTR WINAPI CharLowerA(LPSTR s)
{
    if (IS_INTRESOURCE(s)) { UINT_PTR c = (UINT_PTR)s & 0xff; return (LPSTR)(c >= 'A' && c <= 'Z' ? c + 32 : c); }
    for (char* p = s; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
    return s;
}

INT WINAPI CompareStringW(LCID, DWORD flags, LPCWSTR a, INT la, LPCWSTR b, INT lb)
{
    if (!a || !b) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    if (la < 0) la = lstrlenW(a);
    if (lb < 0) lb = lstrlenW(b);
    return wcmp_ling(a, la, b, lb, flags & (NORM_IGNORECASE | LINGUISTIC_IGNORECASE)) + 2;
}

INT WINAPI CompareStringA(LCID lcid, DWORD flags, LPCSTR a, INT la, LPCSTR b, INT lb)
{
    if (!a || !b) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    std::u16string wa = utf16_of(a, la), wb = utf16_of(b, lb);
    return CompareStringW(lcid, flags, (LPCWSTR)wa.data(), (int)wa.size(), (LPCWSTR)wb.data(), (int)wb.size());
}

INT WINAPI CompareStringOrdinal(const WCHAR* a, INT la, const WCHAR* b, INT lb, BOOL ignore_case)
{
    if (!a || !b) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    if (la < 0) la = lstrlenW(a);
    if (lb < 0) lb = lstrlenW(b);
    int n = la < lb ? la : lb;
    for (int i = 0; i < n; i++) {
        unsigned ca = (unsigned short)a[i], cb = (unsigned short)b[i];
        if (ignore_case) { ca = (unsigned short)w32::wupper(ca); cb = (unsigned short)w32::wupper(cb); }
        if (ca != cb) return ca < cb ? CSTR_LESS_THAN : CSTR_GREATER_THAN;
    }
    return la == lb ? CSTR_EQUAL : la < lb ? CSTR_LESS_THAN : CSTR_GREATER_THAN;
}

BOOL WINAPI IsBadStringPtrW(LPCWSTR s, UINT_PTR) { return !s; }
BOOL WINAPI IsBadStringPtrA(LPCSTR s, UINT_PTR) { return !s; }
BOOL WINAPI IsBadWritePtr(LPVOID p, UINT_PTR n) { return !p && n; }
BOOL WINAPI IsBadReadPtr(LPCVOID p, UINT_PTR n) { return !p && n; }
BOOL WINAPI IsBadCodePtr(FARPROC) { return FALSE; }

} // extern "C"

// ---- atoms ----

namespace {

struct Atom { std::u16string name; int refs; };
std::vector<Atom> atoms;                 // atom = 0xC000 + index

bool int_atom(LPCWSTR s, ATOM& out)
{
    if (IS_INTRESOURCE(s)) { out = (ATOM)(UINT_PTR)s; return true; }
    if (s[0] != '#') return false;
    unsigned v = 0;
    const WCHAR* p = s + 1;
    if (!*p) return false;
    for (; *p; p++) {
        if (*p < '0' || *p > '9') return false;
        v = v * 10 + (*p - '0');
        if (v > 0xffff) return false;
    }
    out = (ATOM)v;
    return true;
}

int atom_find(LPCWSTR s)
{
    int len = lstrlenW(s);
    for (size_t i = 0; i < atoms.size(); i++)
        if (atoms[i].refs && !wcmp_ci((const WCHAR*)atoms[i].name.data(), (int)atoms[i].name.size(), s, len)) return (int)i;
    return -1;
}

ATOM atom_add(LPCWSTR s)
{
    ATOM a;
    if (!s) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    if (int_atom(s, a)) {
        if (!a || a >= 0xc000) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
        return a;
    }
    int len = lstrlenW(s);
    if (!len || len > 255) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    int i = atom_find(s);
    if (i >= 0) { atoms[i].refs++; return (ATOM)(0xc000 + i); }
    for (size_t k = 0; k < atoms.size(); k++)
        if (!atoms[k].refs) { atoms[k].name.assign((const char16_t*)s, len); atoms[k].refs = 1; return (ATOM)(0xc000 + k); }
    if (atoms.size() >= 0x3fff) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return 0; }
    atoms.push_back(Atom{std::u16string((const char16_t*)s, len), 1});
    return (ATOM)(0xc000 + atoms.size() - 1);
}

ATOM atom_findw(LPCWSTR s)
{
    ATOM a;
    if (!s) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    if (int_atom(s, a)) return a < 0xc000 ? a : 0;
    int i = atom_find(s);
    if (i < 0) { SetLastError(ERROR_FILE_NOT_FOUND); return 0; }
    return (ATOM)(0xc000 + i);
}

ATOM atom_delete(ATOM a)
{
    if (a < 0xc000) return 0;
    size_t i = a - 0xc000;
    if (i >= atoms.size() || !atoms[i].refs) { SetLastError(ERROR_INVALID_HANDLE); return a; }
    if (!--atoms[i].refs) atoms[i].name.clear();
    return 0;
}

UINT atom_name(ATOM a, LPWSTR buf, INT size)
{
    if (!buf || size <= 0) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
    std::u16string name;
    if (a < 0xc000) {
        if (!a) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
        char tmp[16];
        snprintf(tmp, sizeof tmp, "#%u", a);
        name = utf16_of(tmp);
    } else {
        size_t i = a - 0xc000;
        if (i >= atoms.size() || !atoms[i].refs) { SetLastError(ERROR_INVALID_HANDLE); return 0; }
        name = atoms[i].name;
    }
    int n = (int)name.size();
    if (n >= size) { n = size - 1; SetLastError(ERROR_INSUFFICIENT_BUFFER); }
    memcpy(buf, name.data(), n * sizeof(WCHAR));
    buf[n] = 0;
    return n;
}

} // namespace

extern "C" {

ATOM WINAPI AddAtomW(LPCWSTR s) { return atom_add(s); }
ATOM WINAPI GlobalAddAtomW(LPCWSTR s) { return atom_add(s); }
ATOM WINAPI AddAtomA(LPCSTR s)
{
    if (IS_INTRESOURCE(s)) return atom_add((LPCWSTR)s);
    std::u16string w = utf16_of(s);
    return atom_add((LPCWSTR)w.c_str());
}
ATOM WINAPI GlobalAddAtomA(LPCSTR s) { return AddAtomA(s); }
ATOM WINAPI DeleteAtom(ATOM a) { return atom_delete(a); }
ATOM WINAPI GlobalDeleteAtom(ATOM a) { return atom_delete(a); }
ATOM WINAPI FindAtomW(LPCWSTR s) { return atom_findw(s); }
ATOM WINAPI GlobalFindAtomW(LPCWSTR s) { return atom_findw(s); }
UINT WINAPI GetAtomNameW(ATOM a, LPWSTR buf, INT size) { return atom_name(a, buf, size); }
UINT WINAPI GlobalGetAtomNameW(ATOM a, LPWSTR buf, INT size) { return atom_name(a, buf, size); }

UINT WINAPI RegisterWindowMessageW(LPCWSTR s)
{
    if (!s || IS_INTRESOURCE(s) || !*s) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    int i = atom_find(s);
    if (i >= 0) return 0xc000 + i;
    ATOM a = atom_add(s);
    return a >= 0xc000 ? a : 0;
}

UINT WINAPI RegisterWindowMessageA(LPCSTR s)
{
    if (!s || IS_INTRESOURCE(s)) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    std::u16string w = utf16_of(s);
    return RegisterWindowMessageW((LPCWSTR)w.c_str());
}

// ---- version, locale ----

DWORD WINAPI GetVersion(void) { return 0x0a280105; }

BOOL WINAPI GetVersionExW(OSVERSIONINFOW* v)
{
    if (!v || (v->dwOSVersionInfoSize != sizeof(OSVERSIONINFOW) && v->dwOSVersionInfoSize != sizeof(OSVERSIONINFOEXW))) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    v->dwMajorVersion = 5;
    v->dwMinorVersion = 1;
    v->dwBuildNumber = 2600;
    v->dwPlatformId = VER_PLATFORM_WIN32_NT;
    static const char sp[] = "Service Pack 3";
    for (size_t i = 0; i < sizeof sp; i++) v->szCSDVersion[i] = sp[i];
    if (v->dwOSVersionInfoSize == sizeof(OSVERSIONINFOEXW)) {
        OSVERSIONINFOEXW* x = (OSVERSIONINFOEXW*)v;
        x->wServicePackMajor = 3;
        x->wServicePackMinor = 0;
        x->wSuiteMask = 0x100;           // VER_SUITE_SINGLEUSERTS
        x->wProductType = VER_NT_WORKSTATION;
        x->wReserved = 0;
    }
    return TRUE;
}

BOOL WINAPI GetVersionExA(OSVERSIONINFOA* v)
{
    if (!v || (v->dwOSVersionInfoSize != sizeof(OSVERSIONINFOA) && v->dwOSVersionInfoSize != sizeof(OSVERSIONINFOEXA))) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    v->dwMajorVersion = 5;
    v->dwMinorVersion = 1;
    v->dwBuildNumber = 2600;
    v->dwPlatformId = VER_PLATFORM_WIN32_NT;
    strcpy(v->szCSDVersion, "Service Pack 3");
    if (v->dwOSVersionInfoSize == sizeof(OSVERSIONINFOEXA)) {
        OSVERSIONINFOEXA* x = (OSVERSIONINFOEXA*)v;
        x->wServicePackMajor = 3;
        x->wServicePackMinor = 0;
        x->wSuiteMask = 0x100;
        x->wProductType = VER_NT_WORKSTATION;
        x->wReserved = 0;
    }
    return TRUE;
}

LCID WINAPI GetUserDefaultLCID(void) { return 0x409; }
LCID WINAPI GetSystemDefaultLCID(void) { return 0x409; }
LCID WINAPI GetThreadLocale(void) { return 0x409; }
LANGID WINAPI GetUserDefaultLangID(void) { return 0x409; }
LANGID WINAPI GetSystemDefaultLangID(void) { return 0x409; }
LANGID WINAPI GetUserDefaultUILanguage(void) { return 0x409; }
LANGID WINAPI GetSystemDefaultUILanguage(void) { return 0x409; }
BOOL WINAPI IsValidLocale(LCID, DWORD) { return TRUE; }

INT WINAPI GetLocaleInfoW(LCID, LCTYPE type, LPWSTR buf, INT len)
{
    const char* v = nullptr;
    static const char* days[7] = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" };
    static const char* abbr_days[7] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
    static const char* short_days[7] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };
    static const char* months[12] = { "January", "February", "March", "April", "May", "June", "July",
                                      "August", "September", "October", "November", "December" };
    static const char* abbr_months[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    LCTYPE t = type & ~(LOCALE_NOUSEROVERRIDE | LOCALE_USE_CP_ACP | LOCALE_RETURN_NUMBER);
    if (t >= LOCALE_SDAYNAME1 && t <= LOCALE_SDAYNAME7) v = days[t - LOCALE_SDAYNAME1];
    else if (t >= LOCALE_SABBREVDAYNAME1 && t <= LOCALE_SABBREVDAYNAME7) v = abbr_days[t - LOCALE_SABBREVDAYNAME1];
    else if (t >= LOCALE_SSHORTESTDAYNAME1 && t <= LOCALE_SSHORTESTDAYNAME7) v = short_days[t - LOCALE_SSHORTESTDAYNAME1];
    else if (t >= LOCALE_SMONTHNAME1 && t <= LOCALE_SMONTHNAME12) v = months[t - LOCALE_SMONTHNAME1];
    else if (t >= LOCALE_SABBREVMONTHNAME1 && t <= LOCALE_SABBREVMONTHNAME12) v = abbr_months[t - LOCALE_SABBREVMONTHNAME1];
    else if (t == LOCALE_SMONTHNAME13 || t == LOCALE_SABBREVMONTHNAME13) v = "";
    switch (t) {
    case LOCALE_SYEARMONTH: v = "MMMM yyyy"; break;
    case LOCALE_IFIRSTWEEKOFYEAR: v = "0"; break;
    case LOCALE_SDECIMAL: case LOCALE_SMONDECIMALSEP: v = "."; break;
    case LOCALE_STHOUSAND: case LOCALE_SMONTHOUSANDSEP: v = ","; break;
    case LOCALE_SGROUPING: case LOCALE_SMONGROUPING: v = "3;0"; break;
    case LOCALE_SLIST: v = ","; break;
    case LOCALE_SNEGATIVESIGN: v = "-"; break;
    case LOCALE_SPOSITIVESIGN: v = ""; break;
    case LOCALE_IDIGITS: case LOCALE_ICURRDIGITS: v = "2"; break;
    case LOCALE_ILZERO: v = "1"; break;
    case LOCALE_INEGNUMBER: v = "1"; break;
    case LOCALE_IMEASURE: v = "1"; break;
    case LOCALE_SCURRENCY: v = "$"; break;
    case LOCALE_ILANGUAGE: v = "0409"; break;
    case LOCALE_SLANGUAGE: v = "English (United States)"; break;
    case LOCALE_SENGLANGUAGE: v = "English"; break;
    case LOCALE_SNAME: v = "en-US"; break;
    case LOCALE_SISO639LANGNAME: v = "en"; break;
    case LOCALE_SISO3166CTRYNAME: v = "US"; break;
    case LOCALE_IDEFAULTANSICODEPAGE: case LOCALE_IDEFAULTCODEPAGE: v = "65001"; break;
    case LOCALE_STIME: v = ":"; break;
    case LOCALE_SDATE: v = "/"; break;
    case LOCALE_SSHORTDATE: v = "M/d/yyyy"; break;
    case LOCALE_SLONGDATE: v = "dddd, MMMM dd, yyyy"; break;
    case LOCALE_STIMEFORMAT: v = "h:mm:ss tt"; break;
    case LOCALE_S1159: v = "AM"; break;
    case LOCALE_S2359: v = "PM"; break;
    case LOCALE_IFIRSTDAYOFWEEK: v = "6"; break;
    default: break;
    }
    if (type & LOCALE_RETURN_NUMBER) {
        if (!v || !*v || !(*v >= '0' && *v <= '9')) { SetLastError(ERROR_INVALID_FLAGS); return 0; }
        if (!len) return sizeof(DWORD) / sizeof(WCHAR);
        if (len < (int)(sizeof(DWORD) / sizeof(WCHAR))) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
        DWORD n = (DWORD)strtoul(v, nullptr, 10);
        memcpy(buf, &n, sizeof n);
        return sizeof(DWORD) / sizeof(WCHAR);
    }
    if (!v) {
        if (len > 0 && buf) buf[0] = 0;
        SetLastError(ERROR_INVALID_FLAGS);
        return 0;
    }
    std::u16string w = utf16_of(v);
    int n = (int)w.size() + 1;
    if (!len) return n;
    if (!buf || len < n) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
    memcpy(buf, w.c_str(), n * sizeof(WCHAR));
    return n;
}

// ---- environment, files ----

DWORD WINAPI ExpandEnvironmentStringsW(LPCWSTR src, LPWSTR dst, DWORD size)
{
    if (!src) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    std::string in = utf8_of(src), out;
    for (size_t i = 0; i < in.size();) {
        if (in[i] == '%') {
            size_t e = in.find('%', i + 1);
            if (e != std::string::npos && e > i + 1) {
                std::string name = in.substr(i + 1, e - i - 1);
                const char* v = getenv(name.c_str());
                if (v) { out += v; i = e + 1; continue; }
            }
        }
        out += in[i++];
    }
    std::u16string w = utf16_of(out.c_str());
    DWORD n = (DWORD)w.size() + 1;
    if (dst && size >= n) memcpy(dst, w.c_str(), n * sizeof(WCHAR));
    else if (dst && size) { memcpy(dst, w.c_str(), (size - 1) * sizeof(WCHAR)); dst[size - 1] = 0; }
    return n;
}

DWORD WINAPI GetEnvironmentVariableW(LPCWSTR name, LPWSTR buf, DWORD size)
{
    if (!name) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    const char* v = getenv(utf8_of(name).c_str());
    if (!v) { SetLastError(ERROR_ENVVAR_NOT_FOUND); return 0; }
    std::u16string w = utf16_of(v);
    DWORD n = (DWORD)w.size();
    if (!buf || size <= n) return n + 1;
    memcpy(buf, w.c_str(), (n + 1) * sizeof(WCHAR));
    return n;
}

} // extern "C"

namespace w32 {

std::string unix_path(const WCHAR* s)
{
    std::string p = utf8_of(s);
    for (char& c : p) if (c == '\\') c = '/';
    return p;
}

} // namespace w32

namespace {

// Copies a UTF-8 string into a counted UTF-16 buffer with the win32
// "return length, or required size including the terminator" convention.
DWORD return_wstring(const std::string& s, LPWSTR buf, DWORD len)
{
    std::u16string w = utf16_of(s.c_str());
    DWORD n = (DWORD)w.size();
    if (!buf || len <= n) return n + 1;
    memcpy(buf, w.c_str(), (n + 1) * sizeof(WCHAR));
    return n;
}

void set_errno_error()
{
    switch (errno) {
    case ENOENT: SetLastError(ERROR_FILE_NOT_FOUND); break;
    case ENOTDIR: SetLastError(ERROR_PATH_NOT_FOUND); break;
    case EACCES: case EPERM: SetLastError(ERROR_ACCESS_DENIED); break;
    default: SetLastError(ERROR_GEN_FAILURE); break;
    }
}

std::string normalize(const std::string& path)
{
    std::string abs = path;
    if (abs.empty() || abs[0] != '/') {
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof cwd)) strcpy(cwd, "/");
        abs = std::string(cwd) + "/" + abs;
    }
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= abs.size()) {
        size_t e = abs.find('/', i);
        if (e == std::string::npos) e = abs.size();
        std::string c = abs.substr(i, e - i);
        if (c == "..") { if (!parts.empty()) parts.pop_back(); }
        else if (!c.empty() && c != ".") parts.push_back(c);
        i = e + 1;
    }
    std::string out;
    for (auto& p : parts) out += "/" + p;
    if (out.empty()) out = "/";
    if (path.size() > 1 && (path.back() == '/') && out != "/") out += "/";
    return out;
}

void filetime_of(const struct timespec& ts, FILETIME& ft)
{
    uint64_t t = ((uint64_t)ts.tv_sec + 11644473600ull) * 10000000ull + ts.tv_nsec / 100;
    ft.dwLowDateTime = (DWORD)t;
    ft.dwHighDateTime = (DWORD)(t >> 32);
}

struct FindState {
    DIR* dir;
    std::string dirpath, mask;
    std::string single;          // non-empty: one pre-matched entry, no DIR
};

bool fill_find_data(const std::string& dirpath, const char* name, WIN32_FIND_DATAW* fd)
{
    std::string full = dirpath + "/" + name;
    struct stat st;
    if (stat(full.c_str(), &st) && lstat(full.c_str(), &st)) return false;
    memset(fd, 0, sizeof *fd);
    DWORD attr = 0;
    if (S_ISDIR(st.st_mode)) attr |= FILE_ATTRIBUTE_DIRECTORY;
    if (!(st.st_mode & S_IWUSR)) attr |= FILE_ATTRIBUTE_READONLY;
    if (name[0] == '.' && strcmp(name, ".") && strcmp(name, "..")) attr |= FILE_ATTRIBUTE_HIDDEN;
    if (!attr) attr = FILE_ATTRIBUTE_NORMAL;
    fd->dwFileAttributes = attr;
    filetime_of(st.st_mtim, fd->ftCreationTime);
    filetime_of(st.st_atim, fd->ftLastAccessTime);
    filetime_of(st.st_mtim, fd->ftLastWriteTime);
    uint64_t size = S_ISDIR(st.st_mode) ? 0 : (uint64_t)st.st_size;
    fd->nFileSizeHigh = (DWORD)(size >> 32);
    fd->nFileSizeLow = (DWORD)size;
    std::u16string w = utf16_of(name);
    size_t n = w.size() < MAX_PATH - 1 ? w.size() : MAX_PATH - 1;
    memcpy(fd->cFileName, w.data(), n * sizeof(WCHAR));
    fd->cFileName[n] = 0;
    return true;
}

bool find_next(FindState* fs, WIN32_FIND_DATAW* fd)
{
    if (!fs->dir) return false;
    while (struct dirent* de = readdir(fs->dir)) {
        if (fnmatch(fs->mask.c_str(), de->d_name, FNM_CASEFOLD | FNM_PERIOD * 0) != 0) continue;
        if (fill_find_data(fs->dirpath, de->d_name, fd)) return true;
    }
    return false;
}

} // namespace

extern "C" {

UINT WINAPI GetCurrentDirectoryW(UINT len, LPWSTR buf)
{
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof cwd)) { set_errno_error(); return 0; }
    return return_wstring(cwd, buf, len);
}

BOOL WINAPI SetCurrentDirectoryW(LPCWSTR dir)
{
    if (!dir) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    if (chdir(w32::unix_path(dir).c_str())) { set_errno_error(); return FALSE; }
    return TRUE;
}

DWORD WINAPI GetFullPathNameW(LPCWSTR name, DWORD len, LPWSTR buf, LPWSTR* filepart)
{
    if (!name || !*name) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    std::string full = normalize(w32::unix_path(name));
    DWORD r = return_wstring(full, buf, len);
    if (filepart) {
        *filepart = nullptr;
        if (buf && r < len && full.back() != '/') {
            size_t slash = full.rfind('/');
            *filepart = buf + utf16_of(full.c_str(), (int)slash + 1).size();
        }
    }
    return r;
}

UINT WINAPI GetDriveTypeW(LPCWSTR root)
{
    if (!root) return DRIVE_FIXED;
    struct stat st;
    if (stat(w32::unix_path(root).c_str(), &st) || !S_ISDIR(st.st_mode)) return DRIVE_NO_ROOT_DIR;
    return DRIVE_FIXED;
}

HANDLE WINAPI FindFirstFileW(LPCWSTR pattern, LPWIN32_FIND_DATAW fd)
{
    if (!pattern || !fd) { SetLastError(ERROR_INVALID_PARAMETER); return INVALID_HANDLE_VALUE; }
    std::string p = w32::unix_path(pattern);
    size_t slash = p.rfind('/');
    std::string dir = slash == std::string::npos ? "." : (slash == 0 ? "/" : p.substr(0, slash));
    std::string mask = slash == std::string::npos ? p : p.substr(slash + 1);
    if (mask.empty()) { SetLastError(ERROR_FILE_NOT_FOUND); return INVALID_HANDLE_VALUE; }
    if (mask == "*.*") mask = "*";
    FindState* fs = new FindState{nullptr, dir, mask, {}};
    if (mask.find_first_of("*?[") == std::string::npos) {
        if (!fill_find_data(dir, mask.c_str(), fd)) { delete fs; SetLastError(ERROR_FILE_NOT_FOUND); return INVALID_HANDLE_VALUE; }
        fs->single = mask;
        return (HANDLE)fs;
    }
    fs->dir = opendir(dir.c_str());
    if (!fs->dir) { set_errno_error(); if (GetLastError() == ERROR_FILE_NOT_FOUND) SetLastError(ERROR_PATH_NOT_FOUND); delete fs; return INVALID_HANDLE_VALUE; }
    if (!find_next(fs, fd)) {
        closedir(fs->dir);
        delete fs;
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    return (HANDLE)fs;
}

BOOL WINAPI FindNextFileW(HANDLE h, LPWIN32_FIND_DATAW fd)
{
    FindState* fs = (FindState*)h;
    if (!fs || h == INVALID_HANDLE_VALUE) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    if (find_next(fs, fd)) return TRUE;
    SetLastError(ERROR_NO_MORE_FILES);
    return FALSE;
}

BOOL WINAPI FindClose(HANDLE h)
{
    FindState* fs = (FindState*)h;
    if (!fs || h == INVALID_HANDLE_VALUE) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    if (fs->dir) closedir(fs->dir);
    delete fs;
    return TRUE;
}

DWORD WINAPI GetFileAttributesW(LPCWSTR name)
{
    struct stat st;
    if (!name || stat(w32::unix_path(name).c_str(), &st)) { set_errno_error(); return INVALID_FILE_ATTRIBUTES; }
    DWORD attr = 0;
    if (S_ISDIR(st.st_mode)) attr |= FILE_ATTRIBUTE_DIRECTORY;
    if (!(st.st_mode & S_IWUSR)) attr |= FILE_ATTRIBUTE_READONLY;
    return attr ? attr : FILE_ATTRIBUTE_NORMAL;
}

// ---- misc ----

INT WINAPI MulDiv(INT a, INT b, INT c)
{
    if (!c) return -1;
    if (c < 0) { a = -a; c = -c; }
    long long r;
    if ((a < 0 && b < 0) || (a >= 0 && b >= 0)) r = ((long long)a * b + c / 2) / c;
    else r = ((long long)a * b - c / 2) / c;
    if (r > 2147483647 || r < -2147483647) return -1;
    return (INT)r;
}

DWORD WINAPI GetTickCount(void) { return sys::ticks(); }
ULONGLONG WINAPI GetTickCount64(void) { return sys::ticks(); }

BOOL WINAPI CloseHandle(HANDLE) { return TRUE; }

struct Event { uint32_t magic; BOOL signaled; };

HANDLE WINAPI CreateEventW(LPSECURITY_ATTRIBUTES, BOOL, BOOL initial, LPCWSTR)
{
    return (HANDLE)new Event{0x45564e54, initial};
}
HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES sa, BOOL manual, BOOL initial, LPCSTR) { return CreateEventW(sa, manual, initial, nullptr); }
BOOL WINAPI SetEvent(HANDLE h) { if (h) ((Event*)h)->signaled = TRUE; return TRUE; }
BOOL WINAPI ResetEvent(HANDLE h) { if (h) ((Event*)h)->signaled = FALSE; return TRUE; }
DWORD WINAPI WaitForSingleObject(HANDLE, DWORD) { return WAIT_OBJECT_0; }
void WINAPI Sleep(DWORD ms) { usleep((useconds_t)ms * 1000); }

BOOL WINAPI DisableThreadLibraryCalls(HMODULE) { return TRUE; }

void WINAPI InitializeCriticalSection(CRITICAL_SECTION* cs) { if (cs) memset(cs, 0, sizeof *cs); }
BOOL WINAPI InitializeCriticalSectionEx(CRITICAL_SECTION* cs, DWORD, DWORD) { InitializeCriticalSection(cs); return TRUE; }
BOOL WINAPI InitializeCriticalSectionAndSpinCount(CRITICAL_SECTION* cs, DWORD) { InitializeCriticalSection(cs); return TRUE; }
void WINAPI EnterCriticalSection(CRITICAL_SECTION*) {}
BOOL WINAPI TryEnterCriticalSection(CRITICAL_SECTION*) { return TRUE; }
void WINAPI LeaveCriticalSection(CRITICAL_SECTION*) {}
void WINAPI DeleteCriticalSection(CRITICAL_SECTION*) {}

} // extern "C"

// ---- modules and resources ----

namespace {

struct Lib { std::string path, base; res::Module* m; int refs; };
std::vector<Lib> libs;
res::Module* g_main;
res::Module* g_empty;

struct ResInfo { const uint8_t* data; DWORD size; };
std::map<const uint8_t*, ResInfo*> res_infos;

res::Module* empty_module()
{
    if (!g_empty) {
        static const uint8_t hdr[32] = { 0,0,0,0, 32,0,0,0, 0xff,0xff,0,0, 0xff,0xff,0,0 };
        g_empty = res::load_memory(hdr, sizeof hdr);
    }
    return g_empty;
}

std::string lower(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c += 32; return s; }

std::string base_of(const std::string& path)
{
    size_t s = path.rfind('/');
    return lower(s == std::string::npos ? path : path.substr(s + 1));
}

std::string strip_ext(const std::string& s)
{
    size_t d = s.rfind('.');
    return d == std::string::npos ? s : s.substr(0, d);
}

HMODULE module_by_name(const std::string& name)
{
    std::string b = base_of(name);
    for (auto& l : libs)
        if (l.base == b || strip_ext(l.base) == strip_ext(b)) return (HMODULE)l.m;
    return (HMODULE)w32::main_module();
}

HRSRC find_res(HMODULE h, const res::Id& type, const res::Id& name, WORD lang)
{
    res::Module* m = w32::module_of(h);
    res::Blob b;
    if (!m || !res::find(m, type, name, b, lang)) { SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND); return nullptr; }
    ResInfo*& ri = res_infos[b.data];
    if (!ri) ri = new ResInfo{b.data, (DWORD)b.size};
    ri->size = (DWORD)b.size;
    return (HRSRC)ri;
}

// Locates string `id` in the module's RT_STRING table: pointer to the
// (unterminated) UTF-16 text and its length.
bool find_string(HINSTANCE h, UINT id, const WCHAR*& s, int& len)
{
    res::Module* m = w32::module_of(h);
    res::Blob b;
    if (!m || !res::find(m, res::Id((uint32_t)6 /* RT_STRING */), res::Id((uint32_t)((id >> 4) + 1)), b)) return false;
    size_t o = 0;
    for (UINT i = 0; i < 16; i++) {
        if (o + 2 > b.size) return false;
        uint16_t n = (uint16_t)(b.data[o] | b.data[o + 1] << 8);
        o += 2;
        if (o + (size_t)n * 2 > b.size) return false;
        if (i == (id & 15)) { s = (const WCHAR*)(b.data + o); len = n; return true; }
        o += (size_t)n * 2;
    }
    return false;
}

} // namespace

namespace w32 {

bool set_main_module(const char* path)
{
    res::Module* m = res::load(path);
    if (!m) return false;
    if (g_main) res::unload(g_main);
    g_main = m;
    return true;
}

res::Module* main_module() { return g_main ? g_main : empty_module(); }
res::Module* module_of(HINSTANCE h) { return h ? (res::Module*)h : main_module(); }

res::Id res_id_of(const WCHAR* s)
{
    if (IS_INTRESOURCE(s)) return res::Id((uint32_t)(UINT_PTR)s);
    if (s[0] == '#') {
        uint32_t v = 0;
        for (const WCHAR* p = s + 1; *p >= '0' && *p <= '9'; p++) v = v * 10 + (*p - '0');
        return res::Id(v);
    }
    return res::Id((const char16_t*)s);
}

res::Id res_id_of_a(const char* s)
{
    if (IS_INTRESOURCE(s)) return res::Id((uint32_t)(UINT_PTR)s);
    if (s[0] == '#') return res::Id((uint32_t)strtoul(s + 1, nullptr, 10));
    return res::Id(s);
}

} // namespace w32

extern "C" {

HMODULE WINAPI GetModuleHandleW(LPCWSTR name)
{
    if (!name) return (HMODULE)w32::main_module();
    return module_by_name(w32::unix_path(name));
}

HMODULE WINAPI GetModuleHandleA(LPCSTR name)
{
    if (!name) return (HMODULE)w32::main_module();
    std::string n = name;
    for (char& c : n) if (c == '\\') c = '/';
    return module_by_name(n);
}

DWORD WINAPI GetModuleFileNameW(HMODULE h, LPWSTR buf, DWORD size)
{
    std::string path;
    for (auto& l : libs) if ((HMODULE)l.m == h) path = l.path;
    if (path.empty()) {
        char tmp[PATH_MAX];
        ssize_t n = readlink("/proc/self/exe", tmp, sizeof tmp - 1);
        path = n > 0 ? std::string(tmp, n) : "flxp";
    }
    std::u16string w = utf16_of(path.c_str());
    if (!buf || !size) return 0;
    DWORD n = (DWORD)w.size();
    if (n >= size) { n = size - 1; SetLastError(ERROR_INSUFFICIENT_BUFFER); }
    memcpy(buf, w.data(), n * sizeof(WCHAR));
    buf[n] = 0;
    return n;
}

HMODULE WINAPI LoadLibraryExW(LPCWSTR name, HANDLE, DWORD)
{
    if (!name) { SetLastError(ERROR_INVALID_PARAMETER); return nullptr; }
    std::string path = w32::unix_path(name);
    for (auto& l : libs) if (l.path == path) { l.refs++; return (HMODULE)l.m; }
    res::Module* m = res::load(path.c_str());
    if (!m) { SetLastError(ERROR_MOD_NOT_FOUND); return nullptr; }
    libs.push_back(Lib{path, base_of(path), m, 1});
    return (HMODULE)m;
}

HMODULE WINAPI LoadLibraryW(LPCWSTR name) { return LoadLibraryExW(name, nullptr, 0); }
HMODULE WINAPI LoadLibraryExA(LPCSTR name, HANDLE f, DWORD flags)
{
    if (!name) { SetLastError(ERROR_INVALID_PARAMETER); return nullptr; }
    std::u16string w = utf16_of(name);
    return LoadLibraryExW((LPCWSTR)w.c_str(), f, flags);
}
HMODULE WINAPI LoadLibraryA(LPCSTR name) { return LoadLibraryExA(name, nullptr, 0); }

BOOL WINAPI FreeLibrary(HMODULE h)
{
    for (size_t i = 0; i < libs.size(); i++) {
        if ((HMODULE)libs[i].m != h) continue;
        if (--libs[i].refs == 0) {
            res::Module* m = libs[i].m;
            libs.erase(libs.begin() + i);
            for (const res::Entry& e : res::list(m)) {      // drop HRSRCs into this module
                auto it = res_infos.find(e.data.data);
                if (it != res_infos.end()) { delete it->second; res_infos.erase(it); }
            }
            res::unload(m);
        }
        return TRUE;
    }
    if (h && (res::Module*)h == w32::main_module()) return TRUE;
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}

HRSRC WINAPI FindResourceExW(HMODULE h, LPCWSTR type, LPCWSTR name, WORD lang)
{
    if (!type || !name) { SetLastError(ERROR_INVALID_PARAMETER); return nullptr; }
    return find_res(h, w32::res_id_of(type), w32::res_id_of(name), lang);
}

HRSRC WINAPI FindResourceW(HMODULE h, LPCWSTR name, LPCWSTR type) { return FindResourceExW(h, type, name, 0); }

HRSRC WINAPI FindResourceExA(HMODULE h, LPCSTR type, LPCSTR name, WORD lang)
{
    if (!type || !name) { SetLastError(ERROR_INVALID_PARAMETER); return nullptr; }
    return find_res(h, w32::res_id_of_a(type), w32::res_id_of_a(name), lang);
}

HRSRC WINAPI FindResourceA(HMODULE h, LPCSTR name, LPCSTR type) { return FindResourceExA(h, type, name, 0); }

HGLOBAL WINAPI LoadResource(HMODULE, HRSRC r) { return r ? (HGLOBAL)((ResInfo*)r)->data : nullptr; }
LPVOID WINAPI LockResource(HGLOBAL h) { return h; }
DWORD WINAPI SizeofResource(HMODULE, HRSRC r) { return r ? ((ResInfo*)r)->size : 0; }
BOOL WINAPI FreeResource(HGLOBAL) { return FALSE; }

// nMax == 0: *buf receives a read-only pointer to the string in the resource
// data (not terminated) and the length is returned.
INT WINAPI LoadStringW(HINSTANCE h, UINT id, LPWSTR buf, INT max)
{
    if (!buf) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    const WCHAR* s = nullptr;
    int len = 0;
    bool found = find_string(h, id, s, len);
    if (max == 0) {
        *(const WCHAR**)buf = found ? s : nullptr;
        if (!found) SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND);
        return found ? len : 0;
    }
    if (!found) { buf[0] = 0; SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND); return 0; }
    if (len > max - 1) len = max - 1;
    memcpy(buf, s, len * sizeof(WCHAR));
    buf[len] = 0;
    return len;
}

INT WINAPI LoadStringA(HINSTANCE h, UINT id, LPSTR buf, INT max)
{
    if (!buf || max <= 0) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    const WCHAR* s = nullptr;
    int len = 0;
    if (!find_string(h, id, s, len)) { buf[0] = 0; SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND); return 0; }
    std::string u = utf8_of(s, len);
    size_t n = u.size();
    if (n > (size_t)max - 1) {
        n = max - 1;
        while (n && ((unsigned char)u[n] & 0xc0) == 0x80) n--;     // don't split a sequence
    }
    memcpy(buf, u.data(), n);
    buf[n] = 0;
    return (INT)n;
}

} // extern "C"

// ---- registry ----

namespace {

const uint32_t KEY_MAGIC = 0x52454759;

struct Value { std::string name; DWORD type; std::vector<uint8_t> data; };
struct Key {
    uint32_t magic = KEY_MAGIC;
    std::string name;
    std::vector<Key*> sub;
    std::vector<Value> values;
};

Key* roots[7];

Key* key_of(HKEY h)
{
    uintptr_t v = (uintptr_t)h;
    if ((v & ~(uintptr_t)0xffffffff) == 0 || (v & ~(uintptr_t)0xffffffff) == ~(uintptr_t)0xffffffff) {
        uint32_t lo = (uint32_t)v;
        if (lo >= 0x80000000u && lo <= 0x80000006u) {
            Key*& r = roots[lo - 0x80000000u];
            if (!r) r = new Key;
            return r;
        }
    }
    Key* k = (Key*)h;
    return k && k->magic == KEY_MAGIC ? k : nullptr;
}

void free_key(Key* k)
{
    for (Key* s : k->sub) free_key(s);
    k->magic = 0;
    delete k;
}

Key* child(Key* k, const std::string& name, bool create)
{
    for (Key* s : k->sub) if (!strcasecmp(s->name.c_str(), name.c_str())) return s;
    if (!create) return nullptr;
    Key* n = new Key;
    n->name = name;
    k->sub.push_back(n);
    return n;
}

Key* walk(Key* k, const std::string& path, bool create, bool* created = nullptr)
{
    if (created) *created = false;
    size_t i = 0;
    while (k && i < path.size()) {
        size_t e = path.find('\\', i);
        if (e == std::string::npos) e = path.size();
        std::string c = path.substr(i, e - i);
        i = e + 1;
        if (c.empty()) continue;
        Key* n = child(k, c, false);
        if (!n && create) { n = child(k, c, true); if (created) *created = true; }
        k = n;
    }
    return k;
}

Value* find_value(Key* k, const std::string& name)
{
    for (auto& v : k->values) if (!strcasecmp(v.name.c_str(), name.c_str())) return &v;
    return nullptr;
}

void set_value(Key* k, const std::string& name, DWORD type, const void* data, size_t size)
{
    Value* v = find_value(k, name);
    if (!v) { k->values.push_back(Value{name, type, {}}); v = &k->values.back(); }
    v->type = type;
    v->data.assign((const uint8_t*)data, (const uint8_t*)data + size);
}

bool is_string_type(DWORD t) { return t == REG_SZ || t == REG_EXPAND_SZ || t == REG_MULTI_SZ; }

LONG copy_out(const void* src, DWORD size, LPBYTE data, LPDWORD cb)
{
    if (data) {
        if (!cb) return ERROR_INVALID_PARAMETER;
        if (*cb < size) { *cb = size; return ERROR_MORE_DATA; }
        memcpy(data, src, size);
    }
    if (cb) *cb = size;
    return ERROR_SUCCESS;
}

} // namespace

namespace w32 {

LONG reg_set_string(HKEY root, const char* path, const char* name, const char* value)
{
    Key* k = key_of(root);
    if (!k) return ERROR_INVALID_HANDLE;
    k = walk(k, path ? path : "", true);
    std::u16string w = utf16_of(value);
    set_value(k, name ? name : "", REG_SZ, w.c_str(), (w.size() + 1) * sizeof(char16_t));
    return ERROR_SUCCESS;
}

LONG reg_set_dword(HKEY root, const char* path, const char* name, DWORD value)
{
    Key* k = key_of(root);
    if (!k) return ERROR_INVALID_HANDLE;
    k = walk(k, path ? path : "", true);
    set_value(k, name ? name : "", REG_DWORD, &value, sizeof value);
    return ERROR_SUCCESS;
}

void reg_clear()
{
    for (Key*& r : roots) if (r) { free_key(r); r = nullptr; }
}

} // namespace w32

extern "C" {

LSTATUS WINAPI RegOpenKeyExW(HKEY h, LPCWSTR sub, DWORD, REGSAM, PHKEY out)
{
    if (!out) return ERROR_INVALID_PARAMETER;
    *out = nullptr;
    Key* k = key_of(h);
    if (!k) return ERROR_INVALID_HANDLE;
    if (!sub || !*sub) { *out = h; return ERROR_SUCCESS; }
    k = walk(k, utf8_of(sub), false);
    if (!k) return ERROR_FILE_NOT_FOUND;
    *out = (HKEY)k;
    return ERROR_SUCCESS;
}

LSTATUS WINAPI RegOpenKeyW(HKEY h, LPCWSTR sub, PHKEY out) { return RegOpenKeyExW(h, sub, 0, KEY_ALL_ACCESS, out); }

LSTATUS WINAPI RegOpenKeyExA(HKEY h, LPCSTR sub, DWORD opt, REGSAM sam, PHKEY out)
{
    std::u16string w = utf16_of(sub);
    return RegOpenKeyExW(h, sub ? (LPCWSTR)w.c_str() : nullptr, opt, sam, out);
}

LSTATUS WINAPI RegCreateKeyExW(HKEY h, LPCWSTR sub, DWORD, LPWSTR, DWORD, REGSAM, LPSECURITY_ATTRIBUTES,
                               PHKEY out, LPDWORD disp)
{
    if (!out) return ERROR_INVALID_PARAMETER;
    Key* k = key_of(h);
    if (!k) return ERROR_INVALID_HANDLE;
    bool created = false;
    if (sub && *sub) k = walk(k, utf8_of(sub), true, &created);
    *out = (sub && *sub) ? (HKEY)k : h;
    if (disp) *disp = created ? REG_CREATED_NEW_KEY : REG_OPENED_EXISTING_KEY;
    return ERROR_SUCCESS;
}

LSTATUS WINAPI RegCreateKeyW(HKEY h, LPCWSTR sub, PHKEY out)
{
    return RegCreateKeyExW(h, sub, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, out, nullptr);
}

LSTATUS WINAPI RegCloseKey(HKEY h) { return key_of(h) ? ERROR_SUCCESS : ERROR_INVALID_HANDLE; }

LSTATUS WINAPI RegQueryValueExW(HKEY h, LPCWSTR name, LPDWORD, LPDWORD type, LPBYTE data, LPDWORD cb)
{
    Key* k = key_of(h);
    if (!k) return ERROR_INVALID_HANDLE;
    if (data && !cb) return ERROR_INVALID_PARAMETER;
    Value* v = find_value(k, name ? utf8_of(name) : "");
    if (!v) return ERROR_FILE_NOT_FOUND;
    if (type) *type = v->type;
    return copy_out(v->data.data(), (DWORD)v->data.size(), data, cb);
}

LSTATUS WINAPI RegQueryValueExA(HKEY h, LPCSTR name, LPDWORD, LPDWORD type, LPBYTE data, LPDWORD cb)
{
    Key* k = key_of(h);
    if (!k) return ERROR_INVALID_HANDLE;
    if (data && !cb) return ERROR_INVALID_PARAMETER;
    Value* v = find_value(k, name ? name : "");
    if (!v) return ERROR_FILE_NOT_FOUND;
    if (type) *type = v->type;
    if (!is_string_type(v->type)) return copy_out(v->data.data(), (DWORD)v->data.size(), data, cb);
    std::string s = utf8_of((const WCHAR*)v->data.data(), (int)(v->data.size() / 2));
    return copy_out(s.data(), (DWORD)s.size(), data, cb);
}

LSTATUS WINAPI RegGetValueW(HKEY h, LPCWSTR sub, LPCWSTR name, DWORD flags, LPDWORD type, PVOID data, LPDWORD cb)
{
    if (data && !cb) return ERROR_INVALID_PARAMETER;
    HKEY hk = h;
    if (sub && *sub) {
        LONG r = RegOpenKeyExW(h, sub, 0, KEY_QUERY_VALUE, &hk);
        if (r) return r;
    }
    Key* k = key_of(hk);
    if (!k) return ERROR_INVALID_HANDLE;
    Value* v = find_value(k, name ? utf8_of(name) : "");
    if (!v) {
        if ((flags & RRF_ZEROONFAILURE) && data && cb) memset(data, 0, *cb);
        return ERROR_FILE_NOT_FOUND;
    }
    DWORD t = v->type;
    std::vector<uint8_t> bytes = v->data;
    if (t == REG_EXPAND_SZ && !(flags & RRF_NOEXPAND)) {
        std::vector<WCHAR> src(bytes.size() / 2 + 1, 0);
        memcpy(src.data(), bytes.data(), bytes.size());
        DWORD n = ExpandEnvironmentStringsW(src.data(), nullptr, 0);
        std::vector<WCHAR> dst(n + 1, 0);
        ExpandEnvironmentStringsW(src.data(), dst.data(), n);
        bytes.assign((uint8_t*)dst.data(), (uint8_t*)(dst.data() + n));
        t = REG_SZ;
    }
    DWORD allowed = flags & RRF_RT_ANY;
    bool ok = t < 32 && (allowed & (1u << t));
    if (t == REG_SZ && v->type == REG_EXPAND_SZ) ok = allowed & (RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ);
    if (t == REG_BINARY && (allowed & RRF_RT_DWORD) == RRF_RT_DWORD && bytes.size() == 4) ok = true;
    if (t == REG_BINARY && (allowed & RRF_RT_QWORD) == RRF_RT_QWORD && bytes.size() == 8) ok = true;
    if (!ok) {
        if ((flags & RRF_ZEROONFAILURE) && data && cb) memset(data, 0, *cb);
        return ERROR_UNSUPPORTED_TYPE;
    }
    // Strings come back terminated.
    if (is_string_type(t) && (bytes.size() < 2 || bytes[bytes.size() - 1] || bytes[bytes.size() - 2])) {
        bytes.push_back(0); bytes.push_back(0);
    }
    if (type) *type = t;
    LONG r = copy_out(bytes.data(), (DWORD)bytes.size(), (LPBYTE)data, cb);
    if (r && (flags & RRF_ZEROONFAILURE) && data && cb) memset(data, 0, *cb < bytes.size() ? 0 : *cb);
    return r;
}

LSTATUS WINAPI RegSetValueExW(HKEY h, LPCWSTR name, DWORD, DWORD type, const BYTE* data, DWORD cb)
{
    Key* k = key_of(h);
    if (!k) return ERROR_INVALID_HANDLE;
    if (!data && cb) return ERROR_NOACCESS;
    set_value(k, name ? utf8_of(name) : "", type, data, cb);
    return ERROR_SUCCESS;
}

LSTATUS WINAPI RegDeleteValueW(HKEY h, LPCWSTR name)
{
    Key* k = key_of(h);
    if (!k) return ERROR_INVALID_HANDLE;
    std::string n = name ? utf8_of(name) : "";
    for (size_t i = 0; i < k->values.size(); i++)
        if (!strcasecmp(k->values[i].name.c_str(), n.c_str())) { k->values.erase(k->values.begin() + i); return ERROR_SUCCESS; }
    return ERROR_FILE_NOT_FOUND;
}

LSTATUS WINAPI RegQueryValueW(HKEY h, LPCWSTR sub, LPWSTR data, LPLONG cb)
{
    HKEY hk;
    LONG r = RegOpenKeyExW(h, sub, 0, KEY_QUERY_VALUE, &hk);
    if (r) return r;
    Key* k = key_of(hk);
    Value* v = find_value(k, "");
    static const WCHAR empty[1] = { 0 };
    const void* src = empty;
    DWORD size = sizeof(WCHAR);
    if (v && v->type == REG_SZ) { src = v->data.data(); size = (DWORD)v->data.size(); }
    DWORD c = cb ? (DWORD)*cb : 0;
    r = copy_out(src, size, (LPBYTE)data, cb ? &c : nullptr);
    if (cb) *cb = (LONG)c;
    return r;
}

} // extern "C"

namespace w32 {

void init_kernel() {}

} // namespace w32
