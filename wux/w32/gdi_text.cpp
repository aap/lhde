// GDI shim, text: DC text attributes, ExtTextOut and friends, extents and
// metrics, all through gdi/font.h. ExtTextOut's alignment and opaquing follow
// Wine's NtGdiExtTextOutW (dlls/win32u/font.c). "ANSI" is UTF-8 here, so the
// A functions convert from UTF-8.
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "gdi_priv.h"
extern "C" {
#include "flxp_dwmapi.h"
}

namespace w32 {

static gdi::Font* font_of(DC* d) { return d && d->font ? d->font->font : nullptr; }

static gdi::TextMetrics tm_of(gdi::Font* f)
{
    if (f) return gdi::metrics(f);
    gdi::TextMetrics tm;
    memset(&tm, 0, sizeof tm);
    return tm;
}

// UTF-16 string -> code points, with the index of the first WCHAR of each.
struct Cps {
    std::vector<uint32_t> cp;
    std::vector<int> at;     // WCHAR index of each code point
};

static void decode16(const WCHAR* s, int n, Cps& out)
{
    out.cp.clear(); out.at.clear();
    for (int i = 0; i < n; i++) {
        uint32_t c = s[i];
        int at = i;
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < n && s[i + 1] >= 0xdc00 && s[i + 1] < 0xe000)
            c = 0x10000 + ((c - 0xd800) << 10) + (s[++i] - 0xdc00);
        out.cp.push_back(c);
        out.at.push_back(at);
    }
}

static std::string utf8_cp(uint32_t c)
{
    WCHAR w[2];
    int n = 1;
    if (c >= 0x10000) { c -= 0x10000; w[0] = (WCHAR)(0xd800 + (c >> 10)); w[1] = (WCHAR)(0xdc00 + (c & 0x3ff)); n = 2; }
    else w[0] = (WCHAR)c;
    return to_utf8(w, n);
}

static gdi::Color color_px(uint32_t v) { return gdi::unpack(v); }

// Advance of each WCHAR (the second half of a surrogate pair gets 0).
static void advances(gdi::Font* f, const WCHAR* s, int n, std::vector<int>& out)
{
    out.assign(n, 0);
    if (!f) return;
    Cps c;
    decode16(s, n, c);
    for (size_t i = 0; i < c.cp.size(); i++) out[c.at[i]] = gdi::char_width(f, c.cp[i]);
}

} // namespace w32

using namespace w32;

extern "C" {

// ---------------------------------------------------------------------------
// Attributes

COLORREF WINAPI SetTextColor(HDC hdc, COLORREF c)
{
    DC* d = dc_of(hdc);
    if (!d) return CLR_INVALID;
    COLORREF old = d->text_color;
    d->text_color = c & 0xffffff;
    return old;
}
COLORREF WINAPI GetTextColor(HDC hdc) { DC* d = dc_of(hdc); return d ? d->text_color : CLR_INVALID; }

COLORREF WINAPI SetBkColor(HDC hdc, COLORREF c)
{
    DC* d = dc_of(hdc);
    if (!d) return CLR_INVALID;
    COLORREF old = d->bk_color;
    d->bk_color = c & 0xffffff;
    return old;
}
COLORREF WINAPI GetBkColor(HDC hdc) { DC* d = dc_of(hdc); return d ? d->bk_color : CLR_INVALID; }

INT WINAPI SetBkMode(HDC hdc, INT mode)
{
    DC* d = dc_of(hdc);
    if (!d || (mode != OPAQUE && mode != TRANSPARENT)) return 0;
    int old = d->bk_mode;
    d->bk_mode = mode;
    return old;
}
INT WINAPI GetBkMode(HDC hdc) { DC* d = dc_of(hdc); return d ? d->bk_mode : 0; }

UINT WINAPI SetTextAlign(HDC hdc, UINT align)
{
    DC* d = dc_of(hdc);
    if (!d) return GDI_ERROR;
    UINT old = d->text_align;
    d->text_align = align;
    return old;
}
UINT WINAPI GetTextAlign(HDC hdc) { DC* d = dc_of(hdc); return d ? d->text_align : GDI_ERROR; }

INT WINAPI SetTextCharacterExtra(HDC hdc, INT extra)
{
    if (!dc_of(hdc)) return (INT)0x80000000;
    if (extra) GDI_FIXME("SetTextCharacterExtra(%d) ignored\n", extra);
    return 0;
}
INT WINAPI GetTextCharacterExtra(HDC hdc) { return dc_of(hdc) ? 0 : (INT)0x80000000; }

BOOL WINAPI SetTextJustification(HDC hdc, INT extra, INT breaks)
{
    if (extra) GDI_FIXME("SetTextJustification(%d, %d) ignored\n", extra, breaks);
    return dc_of(hdc) != nullptr;
}

// ---------------------------------------------------------------------------
// Output

BOOL WINAPI ExtTextOutW(HDC hdc, INT x, INT y, UINT flags, const RECT* lprect, LPCWSTR str, UINT count, const INT* lpDx)
{
    DC* d = dc_of(hdc);
    if (!d) return FALSE;
    if (count > 0x7fffffff) return FALSE;
    UINT align = d->text_align;
    if (align & TA_UPDATECP) { x = d->cur_pos.x; y = d->cur_pos.y; }
    gdi::Font* f = font_of(d);
    gdi::TextMetrics tm = tm_of(f);
    gdi::DC* g = d->dc;

    gdi::Rect rc = gdi::empty_rect();
    if (lprect && (flags & (ETO_OPAQUE | ETO_CLIPPED))) {
        rc = gdi::Rect{min(lprect->left, lprect->right), min(lprect->top, lprect->bottom),
                       max(lprect->left, lprect->right), max(lprect->top, lprect->bottom)};
        if (flags & ETO_OPAQUE) gdi::fill_rect(g, rc, color_px(dev_pixel(d, d->bk_color)));
    } else flags &= ~ETO_CLIPPED;

    if (!count) return TRUE;
    if (!str) return FALSE;
    if (flags & ETO_GLYPH_INDEX) GDI_FIXME("ExtTextOutW: ETO_GLYPH_INDEX treated as code points\n");

    Cps cps;
    decode16(str, (int)count, cps);
    // per code point advance
    std::vector<int> adv(cps.cp.size());
    for (size_t i = 0; i < cps.cp.size(); i++) {
        if (lpDx) {
            int at = cps.at[i];
            int next = i + 1 < cps.cp.size() ? cps.at[i + 1] : (int)count;
            int sum = 0;
            for (int k = at; k < next; k++) sum += (flags & ETO_PDY) ? lpDx[2 * k] : lpDx[k];
            adv[i] = sum;
        } else adv[i] = f ? gdi::char_width(f, cps.cp[i]) : 0;
    }
    int width = 0;
    for (int a : adv) width += a;

    switch (align & (TA_LEFT | TA_RIGHT | TA_CENTER)) {
    case TA_LEFT:
        if (align & TA_UPDATECP) d->cur_pos = POINT{x + width, y};
        break;
    case TA_CENTER:
        x -= width / 2;
        break;
    case TA_RIGHT:
        x -= width;
        if (align & TA_UPDATECP) d->cur_pos = POINT{x, y};
        break;
    }
    switch (align & (TA_TOP | TA_BOTTOM | TA_BASELINE)) {
    case TA_TOP: y += tm.ascent; break;
    case TA_BOTTOM: y -= tm.descent; break;
    case TA_BASELINE: break;
    }

    if (d->bk_mode != TRANSPARENT && !((flags & ETO_CLIPPED) && (flags & ETO_OPAQUE))) {
        if (!(flags & ETO_OPAQUE) || !lprect || x < rc.left || x + width >= rc.right ||
            y - tm.ascent < rc.top || y + tm.descent >= rc.bottom) {
            gdi::Rect box{x, y - tm.ascent, x + width, y + tm.descent};
            if (flags & ETO_CLIPPED) box = gdi::intersect(box, rc);
            if (!box.empty()) gdi::fill_rect(g, box, color_px(dev_pixel(d, d->bk_color)));
        }
    }

    if (!f) return TRUE;
    gdi::Region saved;
    bool clipped = flags & ETO_CLIPPED;
    if (clipped) {
        saved = g->clip;
        gdi::Region c = g->clip;
        c.intersect(gdi::offset(rc, g->origin.x, g->origin.y));
        gdi::set_clip(g, c);
    }
    gdi::Color fg = color_px(dev_pixel(d, d->text_color));
    if (!lpDx) {
        std::string s = to_utf8(str, (int)count);
        gdi::text_out(g, gdi::Point{x, y - tm.ascent}, f, s.data(), (int32_t)s.size(), fg);
    } else {
        int px = x;
        for (size_t i = 0; i < cps.cp.size(); i++) {
            std::string s = utf8_cp(cps.cp[i]);
            gdi::text_out(g, gdi::Point{px, y - tm.ascent}, f, s.data(), (int32_t)s.size(), fg);
            px += adv[i];
        }
    }
    if (clipped) gdi::set_clip(g, saved);
    return TRUE;
}

BOOL WINAPI TextOutW(HDC hdc, INT x, INT y, LPCWSTR str, INT count)
{
    if (count < 0) return FALSE;
    return ExtTextOutW(hdc, x, y, 0, nullptr, str, (UINT)count, nullptr);
}

BOOL WINAPI ExtTextOutA(HDC hdc, INT x, INT y, UINT flags, const RECT* lprect, LPCSTR str, UINT count, const INT* lpDx)
{
    if (!str || !count) return ExtTextOutW(hdc, x, y, flags, lprect, nullptr, 0, nullptr);
    std::u16string w = to_utf16(str, (int)count);
    std::vector<INT> dx;
    if (lpDx) {
        // one lpDx entry per byte; give each UTF-16 unit the sum for its bytes
        int step = (flags & ETO_PDY) ? 2 : 1;
        const uint8_t* p = (const uint8_t*)str;
        UINT i = 0;
        while (i < count) {
            uint8_t c = p[i];
            UINT n = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
            if (i + n > count) n = count - i;
            int sum = 0, sumy = 0;
            for (UINT k = 0; k < n; k++) { sum += lpDx[(i + k) * step]; if (step == 2) sumy += lpDx[(i + k) * 2 + 1]; }
            dx.push_back(sum);
            if (step == 2) dx.push_back(sumy);
            if (n == 4) { dx.push_back(0); if (step == 2) dx.push_back(0); }
            i += n;
        }
        while (dx.size() < w.size() * step) dx.push_back(0);
    }
    return ExtTextOutW(hdc, x, y, flags, lprect, (const WCHAR*)w.data(), (UINT)w.size(), lpDx ? dx.data() : nullptr);
}

BOOL WINAPI TextOutA(HDC hdc, INT x, INT y, LPCSTR str, INT count)
{
    if (count < 0) return FALSE;
    return ExtTextOutA(hdc, x, y, 0, nullptr, str, (UINT)count, nullptr);
}

// ---------------------------------------------------------------------------
// Extents

BOOL WINAPI GetTextExtentExPointW(HDC hdc, LPCWSTR str, INT count, INT max_ext, LPINT fit, LPINT dxs, LPSIZE size)
{
    DC* d = dc_of(hdc);
    if (!d || count < 0 || (!str && count)) return FALSE;
    gdi::Font* f = font_of(d);
    std::vector<int> adv;
    advances(f, str, count, adv);
    int total = 0, nfit = 0;
    for (int i = 0; i < count; i++) {
        total += adv[i];
        if (dxs) dxs[i] = total;
        if (total <= max_ext) nfit = i + 1;
    }
    if (fit) *fit = nfit;
    if (size) { size->cx = total; size->cy = tm_of(f).height; }
    return TRUE;
}

BOOL WINAPI GetTextExtentPoint32W(HDC hdc, LPCWSTR str, INT count, LPSIZE size)
{
    return GetTextExtentExPointW(hdc, str, count, 0, nullptr, nullptr, size);
}

BOOL WINAPI GetTextExtentPointW(HDC hdc, LPCWSTR str, INT count, LPSIZE size)
{
    return GetTextExtentPoint32W(hdc, str, count, size);
}

BOOL WINAPI GetTextExtentExPointA(HDC hdc, LPCSTR str, INT count, INT max_ext, LPINT fit, LPINT dxs, LPSIZE size)
{
    if (count < 0) return FALSE;
    std::u16string w = to_utf16(str, count);
    std::vector<INT> wdx(w.size());
    INT wfit = 0;
    if (!GetTextExtentExPointW(hdc, (const WCHAR*)w.data(), (INT)w.size(), max_ext, fit ? &wfit : nullptr,
                               dxs ? wdx.data() : nullptr, size))
        return FALSE;
    // map back to bytes
    const uint8_t* p = (const uint8_t*)str;
    int wi = 0, bfit = 0;
    for (int i = 0; i < count;) {
        uint8_t c = p[i];
        int n = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
        if (i + n > count) n = count - i;
        int units = n == 4 ? 2 : 1;
        int last = min(wi + units - 1, (int)w.size() - 1);
        for (int k = 0; k < n; k++) if (dxs) dxs[i + k] = last >= 0 ? wdx[last] : 0;
        if (fit && wi + units <= wfit) bfit = i + n;
        wi += units;
        i += n;
    }
    if (fit) *fit = bfit;
    return TRUE;
}

BOOL WINAPI GetTextExtentPoint32A(HDC hdc, LPCSTR str, INT count, LPSIZE size)
{
    if (count < 0) return FALSE;
    std::u16string w = to_utf16(str, count);
    return GetTextExtentPoint32W(hdc, (const WCHAR*)w.data(), (INT)w.size(), size);
}

BOOL WINAPI GetTextExtentPointA(HDC hdc, LPCSTR str, INT count, LPSIZE size)
{
    return GetTextExtentPoint32A(hdc, str, count, size);
}

// ---------------------------------------------------------------------------
// Metrics

BOOL WINAPI GetTextMetricsW(HDC hdc, LPTEXTMETRICW out)
{
    DC* d = dc_of(hdc);
    if (!d || !out) return FALSE;
    gdi::Font* f = font_of(d);
    if (!f) return FALSE;
    gdi::TextMetrics tm = gdi::metrics(f);
    const LOGFONTW& lf = d->font->lf;
    memset(out, 0, sizeof *out);
    out->tmHeight = tm.height;
    out->tmAscent = tm.ascent;
    out->tmDescent = tm.descent;
    out->tmInternalLeading = tm.internal_leading;
    out->tmExternalLeading = tm.external_leading;
    out->tmAveCharWidth = tm.ave_char_width;
    out->tmMaxCharWidth = tm.max_char_width;
    out->tmWeight = lf.lfWeight ? lf.lfWeight : FW_NORMAL;
    out->tmOverhang = 0;
    out->tmDigitizedAspectX = 96;
    out->tmDigitizedAspectY = 96;
    out->tmFirstChar = 0x20;
    out->tmLastChar = 0xfffc;
    out->tmDefaultChar = 0x1f;
    out->tmBreakChar = 0x20;
    out->tmItalic = lf.lfItalic ? 255 : 0;
    out->tmUnderlined = lf.lfUnderline ? 255 : 0;
    out->tmStruckOut = lf.lfStrikeOut ? 255 : 0;
    // TMPF_FIXED_PITCH set means *variable* pitch
    out->tmPitchAndFamily = (tm.fixed_pitch ? FF_MODERN : TMPF_FIXED_PITCH | FF_SWISS) | TMPF_VECTOR | TMPF_TRUETYPE;
    out->tmCharSet = ANSI_CHARSET;
    return TRUE;
}

BOOL WINAPI GetTextMetricsA(HDC hdc, LPTEXTMETRICA out)
{
    TEXTMETRICW w;
    if (!out || !GetTextMetricsW(hdc, &w)) return FALSE;
    out->tmHeight = w.tmHeight; out->tmAscent = w.tmAscent; out->tmDescent = w.tmDescent;
    out->tmInternalLeading = w.tmInternalLeading; out->tmExternalLeading = w.tmExternalLeading;
    out->tmAveCharWidth = w.tmAveCharWidth; out->tmMaxCharWidth = w.tmMaxCharWidth;
    out->tmWeight = w.tmWeight; out->tmOverhang = w.tmOverhang;
    out->tmDigitizedAspectX = w.tmDigitizedAspectX; out->tmDigitizedAspectY = w.tmDigitizedAspectY;
    out->tmFirstChar = 0x1e; out->tmLastChar = 0xff; out->tmDefaultChar = 0x1f; out->tmBreakChar = 0x20;
    out->tmItalic = w.tmItalic; out->tmUnderlined = w.tmUnderlined; out->tmStruckOut = w.tmStruckOut;
    out->tmPitchAndFamily = w.tmPitchAndFamily; out->tmCharSet = w.tmCharSet;
    return TRUE;
}

BOOL WINAPI GetCharWidth32W(HDC hdc, UINT first, UINT last, LPINT buf)
{
    DC* d = dc_of(hdc);
    if (!d || !buf || last < first) return FALSE;
    gdi::Font* f = font_of(d);
    for (UINT c = first; c <= last; c++) buf[c - first] = f ? gdi::char_width(f, c) : 0;
    return TRUE;
}

BOOL WINAPI GetCharWidthW(HDC hdc, UINT first, UINT last, LPINT buf) { return GetCharWidth32W(hdc, first, last, buf); }

BOOL WINAPI GetCharABCWidthsW(HDC hdc, UINT first, UINT last, LPABC abc)
{
    DC* d = dc_of(hdc);
    if (!d || !abc || last < first) return FALSE;
    gdi::Font* f = font_of(d);
    if (!f) return FALSE;
    for (UINT c = first; c <= last; c++) {
        abc[c - first].abcA = 0;
        abc[c - first].abcB = gdi::char_width(f, c);
        abc[c - first].abcC = 0;
    }
    return TRUE;
}

// Undocumented gdi32 export used by comctl32's edit control (Wine's struct
// char_width_info { INT lsb, rsb, unk; }).
struct char_width_info;
BOOL WINAPI GetCharWidthInfo(HDC hdc, struct char_width_info* info)
{
    (void)hdc; (void)info;
    return FALSE;
}

LONG WINAPI GdiGetCharDimensions(HDC hdc, LPTEXTMETRICW tm, LONG* height)
{
    if (tm && !GetTextMetricsW(hdc, tm)) return 0;
    static const WCHAR alphabet[] = {
        'a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z',
        'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z' };
    SIZE sz;
    if (!GetTextExtentPointW(hdc, alphabet, 52, &sz)) return 0;
    if (height) *height = sz.cy;
    return (sz.cx / 26 + 1) / 2;
}

DWORD WINAPI GdiGetCodePage(HDC hdc) { (void)hdc; return 65001; /* CP_UTF8 */ }

UINT WINAPI GetTextCharsetInfo(HDC hdc, LPFONTSIGNATURE fs, DWORD flags)
{
    (void)flags;
    if (fs) { memset(fs, 0, sizeof *fs); fs->fsCsb[0] = FS_LATIN1; }
    return dc_of(hdc) ? ANSI_CHARSET : DEFAULT_CHARSET;
}

UINT WINAPI GetTextCharset(HDC hdc) { return GetTextCharsetInfo(hdc, nullptr, 0); }

INT WINAPI GetTextFaceW(HDC hdc, INT count, LPWSTR name)
{
    DC* d = dc_of(hdc);
    if (!d || !d->font) return 0;
    const WCHAR* face = d->font->lf.lfFaceName;
    int len = 0;
    while (len < LF_FACESIZE && face[len]) len++;
    if (!name) return len + 1;
    if (count <= 0) return 0;
    int n = min(len, count - 1);
    memcpy(name, face, n * sizeof(WCHAR));
    name[n] = 0;
    return n + 1;
}

INT WINAPI GetTextFaceA(HDC hdc, INT count, LPSTR name)
{
    DC* d = dc_of(hdc);
    if (!d || !d->font) return 0;
    std::string face = to_utf8(d->font->lf.lfFaceName, -1);
    if (!name) return (INT)face.size() + 1;
    if (count <= 0) return 0;
    int n = min((int)face.size(), count - 1);
    memcpy(name, face.data(), n);
    name[n] = 0;
    return n + 1;
}

// ---------------------------------------------------------------------------
// flxp_dwmapi.h: drawing on an ARGB window's glass

BOOL WINAPI FlxpClearRect(HDC hdc, const RECT* rc)
{
    DC* d = dc_of(hdc);
    if (!d || !rc) return FALSE;
    gdi::clear_rect(d->dc, gdi::Rect{rc->left, rc->top, rc->right, rc->bottom});
    return TRUE;
}

// The halo follows whatever DrawTextW lays out (wrapping, alignment,
// prefixes): the text is drawn white on black into a scratch DIB first and
// its coverage blurred onto the target.
INT WINAPI FlxpDrawGlowTextW(HDC hdc, LPCWSTR text, INT len, RECT* rc, UINT format, INT glow_size, COLORREF glow)
{
    DC* d = dc_of(hdc);
    if (!d || !rc) return 0;
    if (glow_size > 0 && !(format & DT_CALCRECT)) {
        int w = rc->right - rc->left, h = rc->bottom - rc->top, pad = glow_size * 2;
        if (w > 0 && h > 0 && w < 8192 && h < 8192) {
            int bw = w + 2 * pad, bh = h + 2 * pad;
            BITMAPINFO bi;
            memset(&bi, 0, sizeof bi);
            bi.bmiHeader.biSize = sizeof bi.bmiHeader;
            bi.bmiHeader.biWidth = bw; bi.bmiHeader.biHeight = -bh;
            bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (mem && bmp && bits) {
                HGDIOBJ obmp = SelectObject(mem, bmp);
                HGDIOBJ ofont = SelectObject(mem, GetCurrentObject(hdc, OBJ_FONT));
                SetTextColor(mem, RGB(255, 255, 255));
                SetBkMode(mem, TRANSPARENT);
                RECT lr = { pad, pad, pad + w, pad + h };
                DrawTextW(mem, text, len, &lr, format);
                std::vector<uint8_t> cov((size_t)bw * bh);
                const uint32_t* px = (const uint32_t*)bits;
                for (size_t i = 0; i < cov.size(); i++) cov[i] = (uint8_t)(px[i] >> 8);   // green
                gdi::glow_mask(d->dc, gdi::Point{rc->left - pad, rc->top - pad}, cov.data(), bw, bh, bw,
                               gdi::rgb(GetRValue(glow), GetGValue(glow), GetBValue(glow)), glow_size);
                SelectObject(mem, ofont);
                SelectObject(mem, obmp);
            }
            if (bmp) DeleteObject(bmp);
            if (mem) DeleteDC(mem);
        }
    }
    return DrawTextW(hdc, text, len, rc, format);
}

} // extern "C"
