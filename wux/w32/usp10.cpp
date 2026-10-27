// A left-to-right Uniscribe subset for edit.c and text.c: one item per
// string, one glyph per UTF-16 unit, widths from the GDI shim's text extent
// functions on the analysed DC, tabs expanded per SCRIPT_TABDEF (SSA_TAB),
// password masking (SSA_PASSWORD) and clipping (SSA_CLIP).
#include <string.h>
#include <vector>
#include "w32/w32.h"
extern "C" {
#include "usp10.h"
}
#include "w32/kernel.h"

namespace {

struct Analysis {
    HDC hdc;
    DWORD flags;
    std::vector<WCHAR> text;          // expanded (password) text
    std::vector<int> dx;              // logical width per character
    std::vector<SCRIPT_LOGATTR> la;   // filled on demand
    int out_chars;
    SIZE size;
};

bool is_space(WCHAR c) { return c == ' ' || c == '\t' || c == 0xa0 || c == 0x3000 || (c >= 0x2000 && c <= 0x200b); }
bool is_newline(WCHAR c) { return c == '\n' || c == '\r' || c == 0x2028 || c == 0x2029; }

int tab_width(const SCRIPT_TABDEF* td, int ave, int x)
{
    int def = 8 * ave;
    int stops = 0, org = 0;
    const int* pos = nullptr;
    if (td) {
        stops = td->cTabStops;
        pos = td->pTabStops;
        if (td->iTabOrigin) org = td->iScale ? td->iTabOrigin * td->iScale / 4 : td->iTabOrigin * ave;
        if (stops == 1) {
            def = td->iScale ? pos[0] * td->iScale / 4 : pos[0] * ave;
            stops = 0;
        } else if (td->iScale) def = 32 * td->iScale / 4;
    }
    for (int i = 0; i < stops; i++) {
        int p = (td->iScale ? pos[i] * td->iScale / 4 : pos[i] * ave) - org;
        if (p > x) return p - x;
    }
    if (def <= 0) def = 1;
    return ((x + org) / def + 1) * def - (x + org);
}

// Advances of text[start, start+n) on hdc, without tabs.
void measure(HDC hdc, const WCHAR* s, int n, int* adv)
{
    if (n <= 0) return;
    std::vector<int> ext(n);
    SIZE sz;
    if (GetTextExtentExPointW(hdc, s, n, 0, nullptr, ext.data(), &sz)) {
        for (int i = 0; i < n; i++) adv[i] = ext[i] - (i ? ext[i - 1] : 0);
        return;
    }
    for (int i = 0; i < n; i++) {
        INT w = 0;
        if (!GetCharWidthW(hdc, s[i], s[i], &w)) w = 0;
        adv[i] = w;
    }
}

Analysis* as_analysis(SCRIPT_STRING_ANALYSIS ssa) { return (Analysis*)ssa; }

} // namespace

extern "C" {

HRESULT WINAPI ScriptBreak(const WCHAR* s, int n, const SCRIPT_ANALYSIS*, SCRIPT_LOGATTR* la)
{
    if (!s || n <= 0 || !la) return E_INVALIDARG;
    for (int i = 0; i < n; i++) {
        SCRIPT_LOGATTR& a = la[i];
        memset(&a, 0, sizeof a);
        WCHAR c = s[i], p = i ? s[i - 1] : 0;
        bool sp = is_space(c), psp = i && is_space(p);
        a.fCharStop = !(c >= 0xdc00 && c < 0xe000 && i && p >= 0xd800 && p < 0xdc00);
        a.fWhiteSpace = sp;
        // Break opportunity before a non-space that follows spaces, a hyphen
        // or a line break.
        if (i && !sp && !is_newline(c) && (psp || p == '-' || is_newline(p))) a.fSoftBreak = 1;
        a.fWordStop = !sp && !is_newline(c) && (i == 0 || psp || is_newline(p));
    }
    return S_OK;
}

HRESULT WINAPI ScriptIsComplex(const WCHAR* s, int n, DWORD)
{
    if (!s || n < 0) return E_INVALIDARG;
    return S_FALSE;
}

HRESULT WINAPI ScriptStringAnalyse(HDC hdc, const void* str, int n, int, int, DWORD flags, int req_width,
                                   SCRIPT_CONTROL*, SCRIPT_STATE*, const int* in_dx, SCRIPT_TABDEF* tabdef,
                                   const BYTE*, SCRIPT_STRING_ANALYSIS* pssa)
{
    if (pssa) *pssa = nullptr;
    if (!str || n < 1 || !pssa) return E_INVALIDARG;
    if (!hdc) return E_PENDING;
    Analysis* a = new Analysis;
    a->hdc = hdc;
    a->flags = flags;
    const WCHAR* s = (const WCHAR*)str;
    if (flags & SSA_PASSWORD) a->text.assign(n, s[0]);
    else a->text.assign(s, s + n);
    a->dx.assign(n, 0);
    TEXTMETRICW tm;
    memset(&tm, 0, sizeof tm);
    GetTextMetricsW(hdc, &tm);
    if (in_dx) {
        for (int i = 0; i < n; i++) a->dx[i] = in_dx[i];
    } else {
        const WCHAR* t = a->text.data();
        int x = 0;
        int i = 0;
        while (i < n) {
            if ((flags & SSA_TAB) && t[i] == '\t') {
                a->dx[i] = tab_width(tabdef, tm.tmAveCharWidth, x);
                x += a->dx[i++];
                continue;
            }
            int j = i;
            while (j < n && !((flags & SSA_TAB) && t[j] == '\t')) j++;
            measure(hdc, t + i, j - i, &a->dx[i]);
            for (int k = i; k < j; k++) x += a->dx[k];
            i = j;
        }
    }
    a->out_chars = n;
    if ((flags & SSA_CLIP) && req_width > 0) {
        int x = 0, k = 0;
        while (k < n && x + a->dx[k] <= req_width) x += a->dx[k++];
        a->out_chars = k;
    }
    int w = 0;
    for (int i = 0; i < a->out_chars; i++) w += a->dx[i];
    a->size.cx = w;
    a->size.cy = tm.tmHeight;
    *pssa = (SCRIPT_STRING_ANALYSIS)a;
    return S_OK;
}

HRESULT WINAPI ScriptStringFree(SCRIPT_STRING_ANALYSIS* pssa)
{
    if (!pssa) return E_INVALIDARG;
    delete as_analysis(*pssa);
    *pssa = nullptr;
    return S_OK;
}

HRESULT WINAPI ScriptStringValidate(SCRIPT_STRING_ANALYSIS ssa) { return ssa ? S_OK : E_INVALIDARG; }

const SIZE* WINAPI ScriptString_pSize(SCRIPT_STRING_ANALYSIS ssa)
{
    Analysis* a = as_analysis(ssa);
    return a ? &a->size : nullptr;
}

const int* WINAPI ScriptString_pcOutChars(SCRIPT_STRING_ANALYSIS ssa)
{
    Analysis* a = as_analysis(ssa);
    return a ? &a->out_chars : nullptr;
}

const SCRIPT_LOGATTR* WINAPI ScriptString_pLogAttr(SCRIPT_STRING_ANALYSIS ssa)
{
    Analysis* a = as_analysis(ssa);
    if (!a) return nullptr;
    if (a->la.empty()) {
        a->la.resize(a->text.size());
        ScriptBreak(a->text.data(), (int)a->text.size(), nullptr, a->la.data());
    }
    return a->la.data();
}

HRESULT WINAPI ScriptStringGetLogicalWidths(SCRIPT_STRING_ANALYSIS ssa, int* dx)
{
    Analysis* a = as_analysis(ssa);
    if (!a || !dx) return S_FALSE;
    for (int i = 0; i < a->out_chars; i++) dx[i] = a->dx[i];
    return S_OK;
}

HRESULT WINAPI ScriptStringGetOrder(SCRIPT_STRING_ANALYSIS ssa, UINT* order)
{
    Analysis* a = as_analysis(ssa);
    if (!a || !order) return E_INVALIDARG;
    for (int i = 0; i < a->out_chars; i++) order[i] = i;
    return S_OK;
}

HRESULT WINAPI ScriptStringCPtoX(SCRIPT_STRING_ANALYSIS ssa, int cp, BOOL trailing, int* px)
{
    Analysis* a = as_analysis(ssa);
    if (!px) return E_INVALIDARG;
    if (!a) { *px = 0; return E_INVALIDARG; }    // edit.c can pass NULL; leave a defined result
    int n = a->out_chars, x = 0;
    if (cp < 0) { *px = 0; return S_OK; }
    for (int i = 0; i < cp && i < n; i++) x += a->dx[i];
    if (trailing && cp < n) x += a->dx[cp];
    *px = x;
    return S_OK;
}

HRESULT WINAPI ScriptStringXtoCP(SCRIPT_STRING_ANALYSIS ssa, int ix, int* pch, int* ptrailing)
{
    Analysis* a = as_analysis(ssa);
    if (!pch || !ptrailing) return E_INVALIDARG;
    if (!a) { *pch = 0; *ptrailing = FALSE; return E_INVALIDARG; }   // EDIT_CharFromPos may pass NULL
    if (ix < 0) { *pch = -1; *ptrailing = TRUE; return S_OK; }
    int x = 0;
    for (int i = 0; i < a->out_chars; i++) {
        int w = a->dx[i];
        if (ix < x + w) {
            *pch = i;
            *ptrailing = (ix - x) * 2 >= w;
            return S_OK;
        }
        x += w;
    }
    *pch = a->out_chars;
    *ptrailing = FALSE;
    return S_OK;
}

// Draws the string at (x, y) with the DC's current colours and uOptions/prc
// passed to ExtTextOutW, then repaints [min_sel, max_sel) with the highlight
// colours on an opaque background (top = y, height = text height, clipped to
// prc horizontally and vertically).
HRESULT WINAPI ScriptStringOut(SCRIPT_STRING_ANALYSIS ssa, int x, int y, UINT options, const RECT* prc,
                               int min_sel, int max_sel, BOOL)
{
    Analysis* a = as_analysis(ssa);
    if (!a) return E_INVALIDARG;
    int n = a->out_chars;
    if (n <= 0) return S_OK;
    HDC hdc = a->hdc;
    std::vector<WCHAR> t(a->text.begin(), a->text.begin() + n);
    for (WCHAR& c : t) if (c == '\t') c = ' ';
    UINT opts = options & (ETO_CLIPPED | ETO_OPAQUE);
    ExtTextOutW(hdc, x, y, opts, (opts & (ETO_CLIPPED | ETO_OPAQUE)) ? prc : nullptr, t.data(), n, a->dx.data());

    if (min_sel < 0) min_sel = 0;
    if (max_sel > n) max_sel = n;
    if (min_sel >= max_sel) return S_OK;
    int sx = x, ex;
    for (int i = 0; i < min_sel; i++) sx += a->dx[i];
    ex = sx;
    for (int i = min_sel; i < max_sel; i++) ex += a->dx[i];
    RECT rc = { sx, y, ex, y + a->size.cy };
    if (prc) {
        if (rc.left < prc->left) rc.left = prc->left;
        if (rc.right > prc->right) rc.right = prc->right;
        if (rc.top < prc->top) rc.top = prc->top;
        if (rc.bottom > prc->bottom) rc.bottom = prc->bottom;
    }
    if (rc.left >= rc.right || rc.top >= rc.bottom) return S_OK;
    COLORREF old_fg = SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
    COLORREF old_bg = SetBkColor(hdc, GetSysColor(COLOR_HIGHLIGHT));
    int old_mode = SetBkMode(hdc, OPAQUE);
    ExtTextOutW(hdc, sx, y, ETO_OPAQUE | ETO_CLIPPED, &rc, t.data() + min_sel, max_sel - min_sel, a->dx.data() + min_sel);
    SetBkMode(hdc, old_mode);
    SetBkColor(hdc, old_bg);
    SetTextColor(hdc, old_fg);
    return S_OK;
}

} // extern "C"
