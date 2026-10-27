// libuxtheme's C API (ut.h) over the vendored Wine uxtheme: a theme is a
// THEME_FILE of its own (not the process's active theme), a class a
// THEME_CLASS opened from it, and drawing goes through draw.c into a shim
// DC that wraps the caller's pixels.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include "res/res.h"
#include "wuxtheme/v4.h"
#include "w32/w32.h"
#include "w32/kernel.h"
#include "gdi/png.h"
#include "gdi/font.h"
extern "C" {
#include "uxtheme.h"
#include "vssym32.h"
#include "msstyles.h"
}
#include "wuxtheme/ut.h"

struct ut_theme {
    PTHEME_FILE tf;
    std::map<std::string, PTHEME_CLASS> classes;     // opened, by the name asked for
    std::deque<std::string> strings;                 // names handed out
    std::vector<std::string> colors, sizes;
    std::string color, size;
};

namespace {

std::recursive_mutex g_lock;

struct Lock {
    std::lock_guard<std::recursive_mutex> g{g_lock};
    Lock() { w32::ensure_teb(); w32::init_gdi(); }
};

std::u16string u16(const char* s) { return w32::utf16_of(s ? s : ""); }
const WCHAR* W(const std::u16string& s) { return (const WCHAR*)s.c_str(); }
std::string u8(const WCHAR* s, int len = -1) { return w32::utf8_of(s, len); }

v4::Style* style(ut_theme* t) { return (v4::Style*)t->tf->v4; }

const char* keep(ut_theme* t, const std::string& s)
{
    t->strings.push_back(s);
    return t->strings.back().c_str();
}

std::vector<std::string> names_of(const WCHAR* list)
{
    std::vector<std::string> o;
    while (list && *list) {
        int n = 0;
        while (list[n]) n++;
        o.push_back(u8(list, n));
        list += n + 1;
    }
    return o;
}

std::string base_class(const char* cls)
{
    const char* p = strstr(cls, "::");
    return p ? p + 2 : cls;
}

bool is_number(const char* s, int* v)
{
    if (!s || !*s) return false;
    char* e;
    long n = strtol(s, &e, 10);
    if (*e) return false;
    *v = (int)n;
    return true;
}

PTHEME_CLASS class_of(ut_theme* t, const char* cls)
{
    auto it = t->classes.find(cls);
    if (it != t->classes.end()) return it->second;
    std::u16string w = u16(cls);
    PTHEME_CLASS tc = MSSTYLES_OpenThemeFileClass(t->tf, nullptr, W(w), 96);
    t->classes[cls] = tc;
    return tc;
}

bool is_sysmetrics(const char* cls) { return cls && !strcasecmp(cls, "sysmetrics"); }

PTHEME_PROPERTY find_prop(ut_theme* t, const char* cls, int part, int state, int prop)
{
    if (!t || !cls) return nullptr;
    if (is_sysmetrics(cls)) {
        for (PTHEME_PROPERTY p = t->tf->metrics; p; p = p->next)
            if (p->iPropertyId == prop) return p;
        return nullptr;
    }
    PTHEME_CLASS tc = class_of(t, cls);
    if (!tc) return nullptr;
    PTHEME_PROPERTY p = MSSTYLES_FindProperty(tc, part, state, 0, prop);
    // 5048 aero (and Vista) parts that choose their image by DPI or size have
    // IMAGEFILE1..7 and no IMAGEFILE: answer IMAGEFILE with the one uxtheme
    // draws at 96 dpi (the highest MINDPIn <= 96, else the first).
    if (!p && prop == TMT_IMAGEFILE) {
        auto num = [](int i, int base, int base6) { return i <= 5 ? base + i - 1 : base6 + i - 6; };
        for (int i = 7; i >= 1 && !p; i--) {
            int dpi = 0;
            PTHEME_PROPERTY d = MSSTYLES_FindProperty(tc, part, state, 0, num(i, TMT_MINDPI1, TMT_MINDPI6));
            if (d && SUCCEEDED(GetThemeInt(tc, part, state, num(i, TMT_MINDPI1, TMT_MINDPI6), &dpi)) && dpi > 0 && dpi <= 96)
                p = MSSTYLES_FindProperty(tc, part, state, 0, num(i, TMT_IMAGEFILE1, TMT_IMAGEFILE6));
        }
        if (!p) p = MSSTYLES_FindProperty(tc, part, state, 0, TMT_IMAGEFILE1);
    }
    return p;
}

// The class as it is stored (exact names, app split).
PTHEME_CLASS stored_class(ut_theme* t, const char* cls)
{
    std::string c = cls, app;
    size_t sep = c.find("::");
    if (sep != std::string::npos) { app = c.substr(0, sep); c = c.substr(sep + 2); }
    for (PTHEME_CLASS p = t->tf->classes; p; p = p->next)
        if (!strcasecmp(u8(p->szAppName).c_str(), app.c_str()) && !strcasecmp(u8(p->szClassName).c_str(), c.c_str()))
            return p;
    return nullptr;
}

// A DC drawing into the caller's premultiplied ARGB pixels.
struct TargetDC {
    gdi::Bitmap* bmp;
    gdi::DC* g;
    w32::DC* d;
    TargetDC(uint32_t* pixels, int w, int h, int stride)
    {
        bmp = gdi::wrap_bitmap(w, h, stride / 4, pixels);
        bmp->has_alpha = true;
        g = gdi::create_dc(bmp);
        d = w32::create_window_dc(g, nullptr);
    }
    ~TargetDC() { w32::destroy_window_dc(d); gdi::destroy_dc(g); gdi::destroy_bitmap(bmp); }
    HDC hdc() const { return (HDC)d; }
};

// A memory DC for measuring (DPI, fonts).
struct MemDC {
    HDC h = CreateCompatibleDC(nullptr);
    ~MemDC() { DeleteDC(h); }
};

RECT rect_of(const ut_rect* r) { RECT o = {r->left, r->top, r->right, r->bottom}; return o; }

} // namespace

extern "C" {

ut_theme* ut_open(const char* path, const char* color, const char* size)
{
    Lock l;
    if (!path) return nullptr;
    std::u16string p = u16(path), c = u16(color), s = u16(size);
    PTHEME_FILE tf = nullptr;
    if (FAILED(MSSTYLES_OpenThemeFile(W(p), color ? W(c) : nullptr, size ? W(s) : nullptr, &tf)) || !tf)
        return nullptr;
    MSSTYLES_ParseThemeFile(tf);
    ut_theme* t = new ut_theme;
    t->tf = tf;
    t->colors = names_of(tf->pszAvailColors);
    t->sizes = names_of(tf->pszAvailSizes);
    t->color = u8(tf->pszSelectedColor);
    t->size = u8(tf->pszSelectedSize);
    return t;
}

void ut_close(ut_theme* t)
{
    Lock l;
    if (!t) return;
    for (auto& c : t->classes) if (c.second) MSSTYLES_CloseThemeClass(c.second);
    MSSTYLES_CloseThemeFile(t->tf);
    delete t;
}

int ut_format(ut_theme* t) { return t && t->tf->v4 ? 4 : 3; }
const char* ut_color(ut_theme* t) { return t ? t->color.c_str() : ""; }
const char* ut_size(ut_theme* t) { return t ? t->size.c_str() : ""; }

static int list_names(const std::vector<std::string>& v, const char** names, int max)
{
    for (int i = 0; i < (int)v.size() && i < max; i++) names[i] = v[i].c_str();
    return (int)v.size();
}
int ut_list_colors(ut_theme* t, const char** names, int max) { return t ? list_names(t->colors, names, max) : 0; }
int ut_list_sizes(ut_theme* t, const char** names, int max) { return t ? list_names(t->sizes, names, max) : 0; }

int ut_list_classes(ut_theme* t, const char** names, int max)
{
    Lock l;
    if (!t) return 0;
    std::vector<PTHEME_CLASS> all;
    for (PTHEME_CLASS p = t->tf->classes; p; p = p->next) all.push_back(p);
    int n = 0;
    if (t->tf->metrics) {
        if (n < max) names[n] = "sysmetrics";
        n++;
    }
    for (auto it = all.rbegin(); it != all.rend(); ++it) {         // file order
        std::string s = (*it)->szAppName[0] ? u8((*it)->szAppName) + "::" + u8((*it)->szClassName) : u8((*it)->szClassName);
        if (n < max) names[n] = keep(t, s);
        n++;
    }
    return n;
}

int ut_list_parts(ut_theme* t, const char* cls, int* parts, int max)
{
    Lock l;
    if (!t || !cls) return 0;
    std::set<int> s;
    if (is_sysmetrics(cls)) s.insert(0);
    else if (PTHEME_CLASS c = stored_class(t, cls))
        for (PTHEME_PARTSTATE ps = c->partstate; ps; ps = ps->next) s.insert(ps->iPartId);
    int n = 0;
    for (int p : s) { if (n < max) parts[n] = p; n++; }
    return n;
}

int ut_list_states(ut_theme* t, const char* cls, int part, int* states, int max)
{
    Lock l;
    if (!t || !cls) return 0;
    std::set<int> s;
    if (is_sysmetrics(cls)) { if (!part) s.insert(0); }
    else if (PTHEME_CLASS c = stored_class(t, cls))
        for (PTHEME_PARTSTATE ps = c->partstate; ps; ps = ps->next) if (ps->iPartId == part) s.insert(ps->iStateId);
    int n = 0;
    for (int v : s) { if (n < max) states[n] = v; n++; }
    return n;
}

int ut_list_props(ut_theme* t, const char* cls, int part, int state, ut_prop_info* props, int max)
{
    Lock l;
    if (!t || !cls) return 0;
    PTHEME_PROPERTY list = nullptr;
    if (is_sysmetrics(cls)) list = part || state ? nullptr : t->tf->metrics;
    else if (PTHEME_CLASS c = stored_class(t, cls))
        for (PTHEME_PARTSTATE ps = c->partstate; ps; ps = ps->next)
            if (ps->iPartId == part && ps->iStateId == state) list = ps->properties;
    std::vector<ut_prop_info> v;
    for (PTHEME_PROPERTY p = list; p; p = p->next) v.push_back(ut_prop_info{p->iPropertyId, p->iPrimitiveType});
    int n = 0;
    for (auto it = v.rbegin(); it != v.rend(); ++it) { if (n < max) props[n] = *it; n++; }   // file order
    return n;
}

int ut_part(ut_theme* t, const char* cls, const char* part)
{
    Lock l;
    int v;
    if (is_number(part, &v)) return v;
    if (!t || !cls || !part) return -1;
    if (style(t) && (v = v4::part_id(*style(t), cls, part)) >= 0) return v;
    int p, s;
    std::u16string c = u16(base_class(cls).c_str()), pn = u16(part);
    if (MSSTYLES_LookupPartState(W(c), W(pn), nullptr, &p, &s)) return p;
    return -1;
}

int ut_state(ut_theme* t, const char* cls, int part, const char* state)
{
    Lock l;
    int v;
    if (is_number(state, &v)) return v;
    if (!t || !cls || !state) return -1;
    if (style(t) && (v = v4::state_id(*style(t), cls, part, state)) >= 0) return v;
    std::u16string c = u16(base_class(cls).c_str()), sn = u16(state);
    LPCWSTR pn = nullptr;
    if (part) MSSTYLES_LookupPartStateName(W(c), part, 0, &pn, nullptr);
    if (part && !pn) return -1;
    int p, s;
    if (MSSTYLES_LookupPartState(W(c), pn, W(sn), &p, &s)) return s;
    return -1;
}

int ut_prop(ut_theme* t, const char* prop)
{
    Lock l;
    int v;
    if (is_number(prop, &v)) return v;
    if (!prop) return -1;
    if (t && style(t) && (v = v4::prop_id(*style(t), prop)) >= 0) return v;
    std::u16string w = u16(prop);
    for (char16_t& ch : w) if (ch >= 'a' && ch <= 'z') ch -= 32;
    int prim, id;
    if (MSSTYLES_LookupProperty(W(w), &prim, &id)) return id;
    return -1;
}

const char* ut_part_name(ut_theme* t, const char* cls, int part)
{
    Lock l;
    if (!t || !cls || !part) return "";
    if (style(t)) {
        std::string n = v4::part_name(*style(t), cls, part);
        if (!n.empty()) return keep(t, n);
    }
    std::u16string c = u16(base_class(cls).c_str());
    LPCWSTR pn = nullptr;
    MSSTYLES_LookupPartStateName(W(c), part, 0, &pn, nullptr);
    return pn ? keep(t, u8(pn)) : "";
}

const char* ut_state_name(ut_theme* t, const char* cls, int part, int state)
{
    Lock l;
    if (!t || !cls || !state) return "";
    if (style(t)) {
        std::string n = v4::state_name(*style(t), cls, part, state);
        if (!n.empty()) return keep(t, n);
    }
    std::u16string c = u16(base_class(cls).c_str());
    LPCWSTR sn = nullptr;
    MSSTYLES_LookupPartStateName(W(c), part, state, nullptr, &sn);
    return sn ? keep(t, u8(sn)) : "";
}

const char* ut_prop_name(ut_theme* t, int prop)
{
    Lock l;
    if (t && style(t)) {
        auto p = style(t)->props.find(prop);
        if (p != style(t)->props.end()) return keep(t, p->second.name);
    }
    LPCWSTR n = MSSTYLES_LookupPropertyName(prop, nullptr);
    if (!n) return "";
    if (!t) { static thread_local std::string s; s = u8(n); return s.c_str(); }
    return keep(t, u8(n));
}

const char* ut_enum_name(ut_theme* t, int prop, int value)
{
    Lock l;
    if (!t) return "";
    if (style(t)) {
        auto p = style(t)->props.find(prop);
        if (p != style(t)->props.end()) {
            std::string n = v4::enum_name(*style(t), p->second.name, value);
            if (!n.empty()) return keep(t, n);
        }
    }
    LPCWSTR n = MSSTYLES_LookupEnumName(prop, value);
    return n ? keep(t, u8(n)) : "";
}

int ut_get_int(ut_theme* t, const char* cls, int part, int state, int prop, int* v)
{
    Lock l;
    PTHEME_PROPERTY p = find_prop(t, cls, part, state, prop);
    return p && SUCCEEDED(MSSTYLES_GetPropertyInt(p, v));
}

int ut_get_bool(ut_theme* t, const char* cls, int part, int state, int prop, int* v)
{
    Lock l;
    PTHEME_PROPERTY p = find_prop(t, cls, part, state, prop);
    BOOL b;
    if (!p || FAILED(MSSTYLES_GetPropertyBool(p, &b))) return 0;
    *v = b ? 1 : 0;
    return 1;
}

int ut_get_enum(ut_theme* t, const char* cls, int part, int state, int prop, int* v)
{
    Lock l;
    PTHEME_PROPERTY p = find_prop(t, cls, part, state, prop);
    if (!p) return 0;
    WCHAR buf[64];
    MSSTYLES_GetPropertyString(p, buf, 64);
    return MSSTYLES_LookupEnum(buf, prop, v) ? 1 : 0;
}

int ut_get_color(ut_theme* t, const char* cls, int part, int state, int prop, uint32_t* rgb)
{
    Lock l;
    PTHEME_PROPERTY p = find_prop(t, cls, part, state, prop);
    COLORREF c;
    if (!p || FAILED(MSSTYLES_GetPropertyColor(p, &c))) return 0;
    *rgb = (uint32_t)GetRValue(c) << 16 | (uint32_t)GetGValue(c) << 8 | GetBValue(c);
    return 1;
}

int ut_get_margins(ut_theme* t, const char* cls, int part, int state, int prop, ut_margins* m)
{
    Lock l;
    PTHEME_PROPERTY p = find_prop(t, cls, part, state, prop);
    MARGINS mg = {};
    if (!p || FAILED(MSSTYLES_GetPropertyMargins(p, nullptr, &mg))) return 0;
    m->left = mg.cxLeftWidth; m->right = mg.cxRightWidth; m->top = mg.cyTopHeight; m->bottom = mg.cyBottomHeight;
    return 1;
}

int ut_get_rect(ut_theme* t, const char* cls, int part, int state, int prop, ut_rect* r)
{
    Lock l;
    PTHEME_PROPERTY p = find_prop(t, cls, part, state, prop);
    RECT rc = {};
    if (!p || FAILED(MSSTYLES_GetPropertyRect(p, &rc))) return 0;
    r->left = rc.left; r->top = rc.top; r->right = rc.right; r->bottom = rc.bottom;
    return 1;
}

int ut_get_position(ut_theme* t, const char* cls, int part, int state, int prop, int* x, int* y)
{
    Lock l;
    PTHEME_PROPERTY p = find_prop(t, cls, part, state, prop);
    POINT pt;
    if (!p || FAILED(MSSTYLES_GetPropertyPosition(p, &pt))) return 0;
    *x = pt.x; *y = pt.y;
    return 1;
}

int ut_get_font(ut_theme* t, const char* cls, int part, int state, int prop, ut_font* f)
{
    Lock l;
    PTHEME_PROPERTY p = find_prop(t, cls, part, state, prop);
    if (!p) return 0;
    MemDC dc;
    LOGFONTW lf;
    if (FAILED(MSSTYLES_GetPropertyFont(p, dc.h, &lf))) return 0;
    memset(f, 0, sizeof *f);
    snprintf(f->face, sizeof f->face, "%s", u8(lf.lfFaceName).c_str());
    f->height = lf.lfHeight; f->weight = lf.lfWeight;
    f->italic = lf.lfItalic; f->underline = lf.lfUnderline; f->strikeout = lf.lfStrikeOut;
    return 1;
}

int ut_get_intlist(ut_theme* t, const char* cls, int part, int state, int prop, int* values, int max, int* count)
{
    Lock l;
    PTHEME_PROPERTY p = find_prop(t, cls, part, state, prop);
    if (!p) return 0;
    INTLIST* il = (INTLIST*)calloc(1, sizeof(INTLIST));
    MSSTYLES_GetPropertyIntList(p, il);
    for (int i = 0; i < il->iValueCount && i < max; i++) values[i] = il->iValues[i];
    *count = il->iValueCount;
    free(il);
    return 1;
}

int ut_get_string(ut_theme* t, const char* cls, int part, int state, int prop, char* buf, size_t size)
{
    Lock l;
    PTHEME_PROPERTY p = find_prop(t, cls, part, state, prop);
    if (!p || !size) return 0;
    snprintf(buf, size, "%s", u8(p->lpValue, (int)p->dwValueLen).c_str());
    return 1;
}

// The raw bytes behind a resource-valued property.
static bool prop_bytes(ut_theme* t, PTHEME_PROPERTY p, const uint8_t** data, size_t* size)
{
    std::u16string v((const char16_t*)p->lpValue, p->dwValueLen);
    if (style(t) && v4::stream(*style(t), v, data, size)) return true;
    // an XP image file name: Blue\FrameCaption.bmp -> BLUE_FRAMECAPTION_BMP (as MSSTYLES_LoadBitmap)
    for (char16_t& c : v) if (c == '\\' || c == '/' || c == '.') c = '_';
    res::Blob b;
    if (res::find((res::Module*)t->tf->hTheme, res::Id(2u), res::Id(v.c_str()), b) && b.data) {
        *data = b.data; *size = b.size;
        return true;
    }
    return false;
}

int ut_get_stream(ut_theme* t, const char* cls, int part, int state, int prop, const void** data, size_t* size)
{
    Lock l;
    PTHEME_PROPERTY p = find_prop(t, cls, part, state, prop);
    const uint8_t* d;
    if (!p || !prop_bytes(t, p, &d, size)) return 0;
    *data = d;
    return 1;
}

int ut_get_resource(ut_theme* t, const char* type, const char* name, const void** data, size_t* size)
{
    Lock l;
    if (!t || !type || !name) return 0;
    res::Blob b;
    if (!res::find((res::Module*)t->tf->hTheme, w32::res_id_of_a(type), w32::res_id_of_a(name), b) || !b.data) return 0;
    *data = b.data; *size = b.size;
    return 1;
}

uint32_t* ut_get_bitmap(ut_theme* t, const char* cls, int part, int state, int prop, int* w, int* h)
{
    Lock l;
    PTHEME_PROPERTY p = find_prop(t, cls, part, state, prop);
    if (!p) return nullptr;
    const uint8_t* d;
    size_t n;
    if (prop_bytes(t, p, &d, &n) && n > 8 && !memcmp(d, "\x89PNG", 4)) {
        gdi::Bitmap* b = gdi::decode_png(d, n);                // straight alpha
        if (!b) return nullptr;
        uint32_t* out = (uint32_t*)malloc((size_t)b->width * b->height * 4);
        for (int y = 0; y < b->height; y++)
            for (int x = 0; x < b->width; x++) {
                uint32_t px = b->row(y)[x], a = px >> 24;
                uint32_t r = ((px >> 16 & 255) * a + 127) / 255, g = ((px >> 8 & 255) * a + 127) / 255, bl = ((px & 255) * a + 127) / 255;
                out[(size_t)y * b->width + x] = a << 24 | r << 16 | g << 8 | bl;
            }
        *w = b->width; *h = b->height;
        gdi::destroy_bitmap(b);
        return out;
    }
    if (is_sysmetrics(cls)) return nullptr;
    PTHEME_CLASS tc = class_of(t, cls);
    if (!tc) return nullptr;
    WCHAR name[MAX_PATH];
    MSSTYLES_GetPropertyString(p, name, MAX_PATH);
    BOOL alpha = FALSE, deftrans = FALSE;
    HBITMAP hb = MSSTYLES_LoadBitmap(tc, name, &alpha, &deftrans);       // uxtheme's alpha rules
    w32::Bitmap* bo = w32::as<w32::Bitmap>((HANDLE)hb, w32::T_BITMAP);
    if (!bo) return nullptr;
    const gdi::Bitmap* b = bo->bmp;
    uint32_t* out = (uint32_t*)malloc((size_t)b->width * b->height * 4);
    for (int y = 0; y < b->height; y++)
        for (int x = 0; x < b->width; x++) {
            uint32_t px = b->row(y)[x];
            out[(size_t)y * b->width + x] = alpha ? px : px | 0xff000000;
        }
    *w = b->width; *h = b->height;
    return out;
}

void ut_free(void* p) { free(p); }

int ut_part_size(ut_theme* t, const char* cls, int part, int state, int which, const ut_rect* bounds, int* cx, int* cy)
{
    Lock l;
    PTHEME_CLASS tc = t && cls ? class_of(t, cls) : nullptr;
    if (!tc) return 0;
    MemDC dc;
    RECT r = bounds ? rect_of(bounds) : RECT{};
    SIZE s = {};
    if (FAILED(GetThemePartSize((HTHEME)tc, dc.h, part, state, bounds ? &r : nullptr, (THEMESIZE)which, &s))) return 0;
    *cx = s.cx; *cy = s.cy;
    return 1;
}

int ut_content_rect(ut_theme* t, const char* cls, int part, int state, const ut_rect* bounds, ut_rect* content)
{
    Lock l;
    PTHEME_CLASS tc = t && cls ? class_of(t, cls) : nullptr;
    if (!tc || !bounds) return 0;
    MemDC dc;
    RECT r = rect_of(bounds), o;
    if (FAILED(GetThemeBackgroundContentRect((HTHEME)tc, dc.h, part, state, &r, &o))) return 0;
    content->left = o.left; content->top = o.top; content->right = o.right; content->bottom = o.bottom;
    return 1;
}

int ut_draw_background(ut_theme* t, const char* cls, int part, int state,
                       uint32_t* pixels, int width, int height, int stride,
                       const ut_rect* rect, const ut_rect* clip)
{
    Lock l;
    PTHEME_CLASS tc = t && cls ? class_of(t, cls) : nullptr;
    if (!tc || !pixels || width <= 0 || height <= 0 || stride < width * 4 || (stride & 3) || !rect) return 0;
    TargetDC dc(pixels, width, height, stride);
    RECT r = rect_of(rect), c = clip ? rect_of(clip) : RECT{};
    return SUCCEEDED(DrawThemeBackground((HTHEME)tc, dc.hdc(), part, state, &r, clip ? &c : nullptr));
}

int ut_draw_text(ut_theme* t, const char* cls, int part, int state,
                 uint32_t* pixels, int width, int height, int stride,
                 const char* text, unsigned flags, const ut_rect* rect)
{
    Lock l;
    PTHEME_CLASS tc = t && cls ? class_of(t, cls) : nullptr;
    if (!tc || !pixels || width <= 0 || height <= 0 || stride < width * 4 || (stride & 3) || !rect || !text) return 0;
    TargetDC dc(pixels, width, height, stride);
    RECT r = rect_of(rect);
    std::u16string w = u16(text);
    return SUCCEEDED(DrawThemeText((HTHEME)tc, dc.hdc(), part, state, W(w), -1, flags, 0, &r));
}

void ut_add_font_dir(const char* dir)
{
    Lock l;
    if (dir) gdi::add_font_dir(dir);
}

} // extern "C"

namespace {

// LOGFONTW from the API's font description.
HFONT font_of(const ut_font* f)
{
    LOGFONTW lf;
    memset(&lf, 0, sizeof lf);
    lf.lfHeight = f->height;
    lf.lfWeight = f->weight ? f->weight : FW_NORMAL;
    lf.lfItalic = (BYTE)(f->italic != 0);
    lf.lfUnderline = (BYTE)(f->underline != 0);
    lf.lfStrikeOut = (BYTE)(f->strikeout != 0);
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = DEFAULT_QUALITY;
    std::u16string face = u16(f->face);
    size_t n = face.size() < LF_FACESIZE - 1 ? face.size() : LF_FACESIZE - 1;
    memcpy(lf.lfFaceName, face.data(), n * sizeof(WCHAR));
    return CreateFontIndirectW(&lf);
}

// The theme part's TMT_FONT as an HFONT, or nullptr (the DC's default font).
HFONT part_font(PTHEME_CLASS tc, HDC dc, int part, int state)
{
    LOGFONTW lf;
    if (!tc || FAILED(GetThemeFont((HTHEME)tc, dc, part, state, TMT_FONT, &lf))) return nullptr;
    return CreateFontIndirectW(&lf);
}

bool valid_target(const void* p, int w, int h, int stride, int bpp)
{
    return p && w > 0 && h > 0 && stride >= w * bpp && (bpp != 4 || !(stride & 3));
}

void out_rect(const RECT& r, ut_rect* o) { o->left = r.left; o->top = r.top; o->right = r.right; o->bottom = r.bottom; }

// DT_CALCRECT with a font selected (nullptr = the DC's default).
void calc_rect(HDC dc, HFONT font, const std::u16string& text, unsigned flags, const ut_rect* bounds, ut_rect* extent)
{
    RECT r = bounds ? rect_of(bounds) : RECT{0, 0, 0xffff, 0xffff};
    HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
    DrawTextW(dc, W(text), -1, &r, flags | DT_CALCRECT);
    if (font) SelectObject(dc, old);
    out_rect(r, extent);
}

} // namespace

extern "C" {

int ut_text_extent(ut_theme* t, const char* cls, int part, int state,
                   const char* text, unsigned flags, const ut_rect* bounds, ut_rect* extent)
{
    Lock l;
    PTHEME_CLASS tc = t && cls ? class_of(t, cls) : nullptr;
    if (!tc || !text || !extent) return 0;
    MemDC dc;
    HFONT font = part_font(tc, dc.h, part, state);
    calc_rect(dc.h, font, u16(text), flags, bounds, extent);
    if (font) DeleteObject(font);
    return 1;
}

int ut_text_extent_font(const ut_font* f, const char* text, unsigned flags,
                        const ut_rect* bounds, ut_rect* extent)
{
    Lock l;
    if (!f || !text || !extent) return 0;
    MemDC dc;
    HFONT font = font_of(f);
    if (!font) return 0;
    calc_rect(dc.h, font, u16(text), flags, bounds, extent);
    DeleteObject(font);
    return 1;
}

int ut_draw_text_font(const ut_font* f, uint32_t rgb,
                      uint32_t* pixels, int width, int height, int stride,
                      const char* text, unsigned flags, const ut_rect* rect)
{
    Lock l;
    if (!f || !text || !rect || !valid_target(pixels, width, height, stride, 4)) return 0;
    HFONT font = font_of(f);
    if (!font) return 0;
    TargetDC dc(pixels, width, height, stride);
    HGDIOBJ old = SelectObject(dc.hdc(), font);
    SetBkMode(dc.hdc(), TRANSPARENT);
    SetTextColor(dc.hdc(), RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff));
    RECT r = rect_of(rect);
    std::u16string w = u16(text);
    DrawTextW(dc.hdc(), W(w), -1, &r, flags);
    SelectObject(dc.hdc(), old);
    DeleteObject(font);
    return 1;
}

int ut_get_text_metrics(ut_theme* t, const char* cls, int part, int state,
                        const ut_font* f, ut_text_metrics* m)
{
    Lock l;
    if (!m) return 0;
    MemDC dc;
    HFONT font = nullptr;
    if (f) font = font_of(f);
    else {
        PTHEME_CLASS tc = t && cls ? class_of(t, cls) : nullptr;
        if (!tc) return 0;
        font = part_font(tc, dc.h, part, state);
    }
    HGDIOBJ old = font ? SelectObject(dc.h, font) : nullptr;
    TEXTMETRICW tm;
    BOOL ok = GetTextMetricsW(dc.h, &tm);
    if (font) { SelectObject(dc.h, old); DeleteObject(font); }
    if (!ok) return 0;
    m->height = tm.tmHeight; m->ascent = tm.tmAscent; m->descent = tm.tmDescent;
    m->internal_leading = tm.tmInternalLeading; m->external_leading = tm.tmExternalLeading;
    m->ave_width = tm.tmAveCharWidth; m->max_width = tm.tmMaxCharWidth;
    return 1;
}

int ut_draw_text_mask(ut_theme* t, const char* cls, int part, int state, const ut_font* f,
                      uint8_t* mask, int width, int height, int stride,
                      const char* text, unsigned flags, const ut_rect* rect)
{
    Lock l;
    if (!text || !rect || !valid_target(mask, width, height, stride, 1)) return 0;
    PTHEME_CLASS tc = nullptr;
    if (!f) {
        tc = t && cls ? class_of(t, cls) : nullptr;
        if (!tc) return 0;
    }
    // Render white text into a transparent scratch surface: with has_alpha the
    // glyph blending writes coverage into the alpha byte.
    std::vector<uint32_t> scratch((size_t)width * height, 0);
    {
        TargetDC dc(scratch.data(), width, height, width * 4);
        HFONT font = f ? font_of(f) : part_font(tc, dc.hdc(), part, state);
        HGDIOBJ old = font ? SelectObject(dc.hdc(), font) : nullptr;
        SetBkMode(dc.hdc(), TRANSPARENT);
        SetTextColor(dc.hdc(), RGB(255, 255, 255));
        RECT r = rect_of(rect);
        std::u16string w = u16(text);
        DrawTextW(dc.hdc(), W(w), -1, &r, flags);
        if (font) { SelectObject(dc.hdc(), old); DeleteObject(font); }
    }
    for (int y = 0; y < height; y++) {
        uint8_t* row = mask + (size_t)y * stride;
        const uint32_t* s = scratch.data() + (size_t)y * width;
        for (int x = 0; x < width; x++) {
            uint8_t a = (uint8_t)(s[x] >> 24);
            if (a > row[x]) row[x] = a;
        }
    }
    return 1;
}

} // extern "C"
