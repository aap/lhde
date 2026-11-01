// dwmapi over the luxwm frame protocol (docs/CLIENT-PROTOCOL.md): the X11
// equivalent of DwmExtendFrameIntoClientArea is an ARGB toplevel plus the
// _LHWM_EXTEND_FRAME / _LHWM_FRAME_FLAGS properties; composition is the
// root's _LHWM_COMPOSITED. The flxp extensions are in flxp_dwmapi.h.
#include "w32.h"
extern "C" {
#include "flxp_dwmapi.h"
}

using namespace w32;

static win::Window* toplevel(HWND h)
{
    win::Window* w = window_of(h);
    if (!win::is_window(w)) return nullptr;
    return w->top ? w : nullptr;
}

extern "C" {

HRESULT WINAPI DwmIsCompositionEnabled(BOOL* enabled)
{
    if (!enabled) return E_INVALIDARG;
    *enabled = win::composited();
    return S_OK;
}

HRESULT WINAPI DwmEnableComposition(UINT) { return S_OK; }

// Unlike Windows this also works while composition is off: luxwm keeps the
// property and ignores it until it composites again, so the window does not
// have to extend again on WM_DWMCOMPOSITIONCHANGED.
HRESULT WINAPI DwmExtendFrameIntoClientArea(HWND h, const MARGINS* m)
{
    win::Window* w = toplevel(h);
    if (!w || !m) return E_INVALIDARG;
    if (!m->cxLeftWidth && !m->cxRightWidth && !m->cyTopHeight && !m->cyBottomHeight) {
        win::set_frame_extension(w, nullptr);     // back to a normal frame; the window stays ARGB
        return S_OK;
    }
    if (!win::set_argb(w, true)) return DWM_E_COMPOSITIONDISABLED;
    int32_t ltrb[4];
    if (m->cxLeftWidth < 0 || m->cxRightWidth < 0 || m->cyTopHeight < 0 || m->cyBottomHeight < 0) {
        for (int32_t& v : ltrb) v = (int32_t)0xffffffff;   // sheet of glass
    } else {
        ltrb[0] = m->cxLeftWidth; ltrb[1] = m->cxRightWidth; ltrb[2] = m->cyTopHeight; ltrb[3] = m->cyBottomHeight;
    }
    win::set_frame_extension(w, ltrb);
    return S_OK;
}

HRESULT WINAPI DwmSetWindowAttribute(HWND h, DWORD attr, LPCVOID data, DWORD size)
{
    win::Window* w = toplevel(h);
    if (!w) return E_INVALIDARG;
    if (attr == DWMWA_FLXP_FRAME_FLAGS) {
        if (!data || size < sizeof(DWORD)) return E_INVALIDARG;
        win::set_frame_flags(w, *(const DWORD*)data & (FLXP_DWMFF_NO_TITLE | FLXP_DWMFF_NO_ICON | FLXP_DWMFF_NO_FOG | FLXP_DWMFF_CLIENT_CAPTION));
        return S_OK;
    }
    return S_OK;    // everything else (NCRENDERING_POLICY, TRANSITIONS_FORCEDISABLED, ...) is accepted and ignored
}

HRESULT WINAPI DwmGetWindowAttribute(HWND h, DWORD attr, PVOID data, DWORD size)
{
    win::Window* w = toplevel(h);
    if (!w || !data) return E_INVALIDARG;
    switch (attr) {
    case DWMWA_NCRENDERING_ENABLED:
        if (size < sizeof(BOOL)) return E_INVALIDARG;
        *(BOOL*)data = w->wm_decorated && win::composited();
        return S_OK;
    case DWMWA_EXTENDED_FRAME_BOUNDS: {
        // the client's window rect; the WM's frame is outside it (_NET_FRAME_EXTENTS, not read yet)
        if (size < sizeof(RECT)) return E_INVALIDARG;
        *(RECT*)data = RECT_of(win::window_rect(w));
        return S_OK;
    }
    case DWMWA_FLXP_FRAME_FLAGS:
        if (size < sizeof(DWORD)) return E_INVALIDARG;
        *(DWORD*)data = win::frame_flags(w);
        return S_OK;
    default:
        return E_INVALIDARG;
    }
}

// luxwm draws the caption buttons outside the client: nothing to hit-test here.
BOOL WINAPI DwmDefWindowProc(HWND, UINT, WPARAM, LPARAM, LRESULT* result)
{
    if (result) *result = 0;
    return FALSE;
}

HRESULT WINAPI DwmFlush(void) { win::present_now(); return S_OK; }

// The frame tint is not published by luxwm; FlxpDwmGetFrameMetrics has the
// caption text and glow colours instead.
HRESULT WINAPI DwmGetColorizationColor(DWORD*, BOOL*) { return E_NOTIMPL; }

HRESULT WINAPI DwmEnableBlurBehindWindow(HWND h, const DWM_BLURBEHIND*)
{
    return toplevel(h) ? S_OK : E_INVALIDARG;     // picom decides what is blurred
}

HRESULT WINAPI FlxpDwmGetFrameMetrics(FLXP_DWM_FRAME_METRICS* m)
{
    if (!m || m->cbSize < sizeof *m) return E_INVALIDARG;
    win::FrameMetrics f;
    m->bValid = win::frame_metrics(f);
    m->cyCaption = f.caption_h; m->cxLeftBorder = f.border_l; m->cxRightBorder = f.border_r; m->cyBottomBorder = f.border_b;
    m->crText = colorref_of(f.text); m->crGlow = colorref_of(f.glow); m->crInactiveText = colorref_of(f.inactive_text);
    m->bMaxValid = m->bValid && f.have_max; m->bMaxGlowNone = f.max_glow_none;
    m->crMaxText = colorref_of(f.max_text); m->crMaxInactiveText = colorref_of(f.max_inactive_text); m->crMaxGlow = colorref_of(f.max_glow);
    return S_OK;
}

HRESULT WINAPI FlxpDwmGetFrameThemePath(LPWSTR buf, UINT cch)
{
    if (!buf || !cch) return E_INVALIDARG;
    std::u16string w = w32::utf16_of(win::frame_theme().c_str());
    size_t n = w.size() < cch - 1 ? w.size() : cch - 1;
    memcpy(buf, w.data(), n * sizeof(WCHAR));
    buf[n] = 0;
    return w.empty() ? S_FALSE : S_OK;
}

HBITMAP WINAPI FlxpDwmGetHeaderBitmap(HWND h)
{
    win::Window* w = toplevel(h);
    if (!w) return nullptr;
    gdi::Bitmap* b = win::header_pixmap(w);
    if (!b) return nullptr;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = b->width; bi.bmiHeader.biHeight = -b->height; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib && bits)
        for (int32_t y = 0; y < b->height; y++) memcpy((uint32_t*)bits + (size_t)y * b->width, b->row(y), (size_t)b->width * 4);
    gdi::destroy_bitmap(b);
    return dib;
}

HRESULT WINAPI FlxpDwmGetCaptionRect(HWND h, RECT* rc)
{
    win::Window* w = toplevel(h);
    if (!w || !rc) return E_INVALIDARG;
    win::Rect r;
    if (!win::caption_rect(w, r)) { SetRectEmpty(rc); return S_FALSE; }
    *rc = RECT_of(r);
    return S_OK;
}

HRESULT WINAPI FlxpDwmSetInputHole(HWND h, const RECT* rc)
{
    win::Window* w = toplevel(h);
    if (!w) return E_INVALIDARG;
    if (!rc || IsRectEmpty(rc)) { win::set_input_hole(w, nullptr); return S_OK; }
    win::Rect r = { rc->left, rc->top, rc->right, rc->bottom };
    win::set_input_hole(w, &r);
    return S_OK;
}

HRESULT WINAPI FlxpDwmIsWindowActive(HWND h, BOOL* active)
{
    win::Window* w = toplevel(h);
    if (!w || !active) return E_INVALIDARG;
    int a = win::wm_active(w);
    if (a < 0) { *active = GetActiveWindow() == h; return S_FALSE; }
    *active = a == 1;
    return S_OK;
}

HRESULT WINAPI FlxpDwmEnableAlpha(HWND h, BOOL enable)
{
    win::Window* w = toplevel(h);
    if (!w) return E_INVALIDARG;
    return win::set_argb(w, enable != FALSE) ? S_OK : E_FAIL;
}

} // extern "C"
