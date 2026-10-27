/* Wine's wine/heap.h convenience wrappers (as of Wine 7.0). */
#ifndef __WINE_WINE_HEAP_H
#define __WINE_WINE_HEAP_H
#include <windef.h>
#include <winbase.h>
static inline void * __WINE_ALLOC_SIZE(1) heap_alloc(SIZE_T len) { return HeapAlloc(GetProcessHeap(), 0, len); }
static inline void * __WINE_ALLOC_SIZE(1) heap_alloc_zero(SIZE_T len) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len); }
static inline void * __WINE_ALLOC_SIZE(2) heap_realloc(void *mem, SIZE_T len) { if (!mem) return heap_alloc(len); return HeapReAlloc(GetProcessHeap(), 0, mem, len); }
static inline BOOL heap_free(void *mem) { return HeapFree(GetProcessHeap(), 0, mem); }
static inline void * __WINE_ALLOC_SIZE(1,2) heap_calloc(SIZE_T count, SIZE_T size) { SIZE_T len = count * size; if (size && len / size != count) return NULL; return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len); }
#endif
