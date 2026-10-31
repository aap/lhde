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
#include "wuxtheme/menu.h"

struct ut_theme {
    PTHEME_FILE tf;
    std::map<std::string, PTHEME_CLASS> classes;     // opened, by the name asked for
    std::deque<std::string> strings;                 // names handed out
    std::vector<std::string> colors, sizes;
    std::string color, size;
};

extern "C" void SYSCOLOR_SetOverride(const COLORREF* tab);   // wux/w32/syscolor.cpp: a caller-given system colour table

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

// ---- Windows Classic drawing with a caller-given system colour table (ut.h) ----------------------------------------------
namespace {

// the table installed in the shim for the duration of one call (COLORREF is 0x00BBGGRR; the API speaks 0xRRGGBB)
struct ColorOverride {
    COLORREF tab[UT_SYSCOLORS];
    bool on;
    explicit ColorOverride(const uint32_t* c) : on(c != nullptr)
    {
        if (!on) return;
        for (int i = 0; i < UT_SYSCOLORS; i++) tab[i] = RGB(c[i] >> 16 & 255, c[i] >> 8 & 255, c[i] & 255);
        SYSCOLOR_SetOverride(tab);
    }
    ~ColorOverride() { if (on) SYSCOLOR_SetOverride(nullptr); }
};

bool buffer_ok(const uint32_t* pixels, int width, int height, int stride)
{
    return pixels && width > 0 && height > 0 && stride >= width * 4 && !(stride & 3);
}

// every pixel of rect (clipped to the buffer) opaque: GDI's fills leave the alpha byte as the colour's (0)
void opaque_rect(uint32_t* pixels, int width, int height, int stride, const RECT& r)
{
    int l = r.left < 0 ? 0 : r.left, t = r.top < 0 ? 0 : r.top, rr = r.right > width ? width : r.right, b = r.bottom > height ? height : r.bottom;
    for (int y = t; y < b; y++) {
        uint32_t* row = pixels + (size_t)y * (stride / 4);
        for (int x = l; x < rr; x++) row[x] |= 0xff000000u;
    }
}

void fill_rect(HDC dc, const RECT& r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

void gradient_h(HDC dc, const RECT& r, COLORREF c0, COLORREF c1)
{
    TRIVERTEX v[2] = { { r.left, r.top, (COLOR16)(GetRValue(c0) << 8), (COLOR16)(GetGValue(c0) << 8), (COLOR16)(GetBValue(c0) << 8), 0 },
                       { r.right, r.bottom, (COLOR16)(GetRValue(c1) << 8), (COLOR16)(GetGValue(c1) << 8), (COLOR16)(GetBValue(c1) << 8), 0 } };
    GRADIENT_RECT g = { 0, 1 };
    GradientFill(dc, v, 2, &g, 1, GRADIENT_FILL_RECT_H);
}

} // namespace

extern "C" {

int ut_draw_edge(uint32_t* pixels, int width, int height, int stride, ut_rect* rect, unsigned edge, unsigned flags, const uint32_t* colors)
{
    Lock l;
    if (!buffer_ok(pixels, width, height, stride) || !rect) return 0;
    ColorOverride co(colors);
    TargetDC dc(pixels, width, height, stride);
    RECT r = rect_of(rect), whole = r;
    BOOL ok = DrawEdge(dc.hdc(), &r, edge, flags);
    // the bands DrawEdge painted are opaque: the outer and inner lines (BF_MIDDLE: the inside too)
    if (flags & UT_BF_MIDDLE) opaque_rect(pixels, width, height, stride, whole);
    else {
        int w = ((edge & (UT_BDR_RAISEDOUTER | UT_BDR_SUNKENOUTER)) ? 1 : 0) + ((edge & (UT_BDR_RAISEDINNER | UT_BDR_SUNKENINNER)) ? 1 : 0);
        if (flags & UT_BF_TOP) opaque_rect(pixels, width, height, stride, RECT{ whole.left, whole.top, whole.right, whole.top + w });
        if (flags & UT_BF_BOTTOM) opaque_rect(pixels, width, height, stride, RECT{ whole.left, whole.bottom - w, whole.right, whole.bottom });
        if (flags & UT_BF_LEFT) opaque_rect(pixels, width, height, stride, RECT{ whole.left, whole.top, whole.left + w, whole.bottom });
        if (flags & UT_BF_RIGHT) opaque_rect(pixels, width, height, stride, RECT{ whole.right - w, whole.top, whole.right, whole.bottom });
    }
    if (flags & UT_BF_ADJUST) { rect->left = r.left; rect->top = r.top; rect->right = r.right; rect->bottom = r.bottom; }
    return ok ? 1 : 0;
}

int ut_draw_frame_control(uint32_t* pixels, int width, int height, int stride, const ut_rect* rect, unsigned type, unsigned state, const uint32_t* colors)
{
    Lock l;
    if (!buffer_ok(pixels, width, height, stride) || !rect) return 0;
    ColorOverride co(colors);
    TargetDC dc(pixels, width, height, stride);
    RECT r = rect_of(rect);
    BOOL ok = DrawFrameControl(dc.hdc(), &r, type, state);
    if (ok && (type == UT_DFC_CAPTION || type == UT_DFC_BUTTON || type == UT_DFC_SCROLL)) opaque_rect(pixels, width, height, stride, rect_of(rect));
    return ok ? 1 : 0;
}

int ut_gradient_fill_h(uint32_t* pixels, int width, int height, int stride, const ut_rect* rect, uint32_t c0, uint32_t c1)
{
    Lock l;
    if (!buffer_ok(pixels, width, height, stride) || !rect) return 0;
    TargetDC dc(pixels, width, height, stride);
    RECT r = rect_of(rect);
    if (r.right <= r.left || r.bottom <= r.top) return 1;
    gradient_h(dc.hdc(), r, RGB(c0 >> 16 & 255, c0 >> 8 & 255, c0 & 255), RGB(c1 >> 16 & 255, c1 >> 8 & 255, c1 & 255));
    opaque_rect(pixels, width, height, stride, r);
    return 1;
}

int ut_draw_caption(uint32_t* pixels, int width, int height, int stride, const ut_rect* rect, unsigned flags, const char* title,
                    const uint32_t* icon, int icon_w, int icon_h, const ut_font* font, const uint32_t* colors)
{
    Lock l;
    if (!buffer_ok(pixels, width, height, stride) || !rect) return 0;
    ColorOverride co(colors);
    TargetDC dc(pixels, width, height, stride);
    RECT r = rect_of(rect);
    if (r.right <= r.left || r.bottom <= r.top) return 1;
    bool active = (flags & UT_DC_ACTIVE) != 0;
    COLORREF cap = GetSysColor(active ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION);
    COLORREF grad = GetSysColor(active ? COLOR_GRADIENTACTIVECAPTION : COLOR_GRADIENTINACTIVECAPTION);
    int L = r.left, ch = r.bottom - r.top;
    if (flags & UT_DC_ICON) {   // xxxDrawCaptionTemp: the icon slot is the caption's height wide, filled with the solid colour
        int slot = ch < r.right - L ? ch : r.right - L;
        RECT s = { L, r.top, L + slot, r.bottom };
        fill_rect(dc.hdc(), s, cap);
        opaque_rect(pixels, width, height, stride, s);
        if (icon && icon_w > 0 && icon_h > 0) {   // premultiplied over, 1:1, one pixel right of the slot's centre (SM_CXSMICON's place)
            int ix = L + 1 + (slot - icon_w) / 2, iy = r.top + (slot - icon_h) / 2;
            for (int y = 0; y < icon_h; y++) {
                int dy = iy + y; if (dy < s.top || dy >= s.bottom || dy < 0 || dy >= height) continue;
                uint32_t* row = pixels + (size_t)dy * (stride / 4);
                for (int x = 0; x < icon_w; x++) {
                    int dx = ix + x; if (dx < s.left || dx >= s.right || dx < 0 || dx >= width) continue;
                    uint32_t sp = icon[(size_t)y * icon_w + x], a = sp >> 24; if (!a) continue;
                    if (a == 255) { row[dx] = sp; continue; }
                    uint32_t d = row[dx], ia = 255 - a, o = 0;
                    for (int sh = 0; sh < 32; sh += 8) o |= ((((sp >> sh) & 255) + (((d >> sh) & 255) * ia + 127) / 255) & 255) << sh;
                    row[dx] = o | 0xff000000u;
                }
            }
        }
        L += slot;
    }
    if (L < r.right) {
        RECT t = { L, r.top, r.right, r.bottom };
        if ((flags & UT_DC_GRADIENT) && cap != grad) gradient_h(dc.hdc(), t, cap, grad); else fill_rect(dc.hdc(), t, cap);
        opaque_rect(pixels, width, height, stride, t);
    }
    if ((flags & UT_DC_TEXT) && title && *title && font && L + 2 < r.right) {
        HFONT f = font_of(font);
        HGDIOBJ old = SelectObject(dc.hdc(), f);
        SetBkMode(dc.hdc(), TRANSPARENT);
        SetTextColor(dc.hdc(), GetSysColor(active ? COLOR_CAPTIONTEXT : COLOR_INACTIVECAPTIONTEXT));
        RECT tr = { L + 2, r.top, r.right, r.bottom };   // ExtTextOut at left + CXEDGE; DrawText's DT_END_ELLIPSIS when too long
        std::u16string w = u16(title);
        DrawTextW(dc.hdc(), W(w), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | DT_LEFT);
        SelectObject(dc.hdc(), old);
        DeleteObject(f);
    }
    return 1;
}

} // extern "C"

// ---- popup menus (ut_menu_*: wuxtheme/menu.h's painter) ----------------------------------------------------------------------
namespace {

struct MenuRun {
    UXMENU_LOOK L;
    std::vector<std::u16string> text, accel;
    std::vector<int> y, h;
    int w = 0, hh = 0;
    MenuRun(const ut_menu_spec* s, const ut_menu_item* items, int n)
    {
        COLORREF tab[UT_SYSCOLORS];
        for (int i = 0; i < UT_SYSCOLORS; i++) {
            uint32_t c = s->colors ? s->colors[i] : 0;
            tab[i] = s->colors ? RGB(c >> 16 & 255, c >> 8 & 255, c & 255) : GetSysColor(i);
        }
        int flat = s->flat > 0 ? 1 : 0;
        LOGFONTW lf = {};
        bool have_font = false;
        PTHEME_CLASS menu = nullptr;
        if (ut_theme* t = s->theme) {
            // the style's [SysMetrics] colours over the table (TMT_SCROLLBAR.. are in COLOR_ order), FLATMENUS, MENUFONT
            for (PTHEME_PROPERTY p = t->tf->metrics; p; p = p->next) {
                int i = p->iPropertyId - TMT_FIRSTCOLOR;
                COLORREF c;
                if (i >= 0 && i < UT_SYSCOLORS && SUCCEEDED(MSSTYLES_GetPropertyColor(p, &c))) tab[i] = c;
                BOOL b;
                if (p->iPropertyId == TMT_FLATMENUS && s->flat < 0 && SUCCEEDED(MSSTYLES_GetPropertyBool(p, &b))) flat = b ? 1 : 0;
                if (p->iPropertyId == TMT_MENUFONT && !s->font) {
                    MemDC dc;
                    if (SUCCEEDED(MSSTYLES_GetPropertyFont(p, dc.h, &lf)) && lf.lfFaceName[0]) have_font = true;
                }
            }
            menu = class_of(t, "Menu");
        }
        if (s->font) {
            memset(&lf, 0, sizeof lf);
            lf.lfHeight = s->font->height; lf.lfWeight = s->font->weight ? s->font->weight : FW_NORMAL;
            lf.lfItalic = (BYTE)(s->font->italic != 0); lf.lfCharSet = DEFAULT_CHARSET;
            std::u16string f = u16(s->font->face);
            memcpy(lf.lfFaceName, f.data(), (f.size() < LF_FACESIZE - 1 ? f.size() : LF_FACESIZE - 1) * sizeof(WCHAR));
            have_font = true;
        }
        if (have_font && lf.lfHeight > 0) lf.lfHeight = -MulDiv(lf.lfHeight, 96, 72);   // points (the style's "Tahoma, 8")
        UXMENU_InitLook(&L, (HTHEME)menu, tab, flat, have_font ? &lf : nullptr);
        MemDC dc;
        int tw = 0, aw = 0, yy = L.pad;
        for (int i = 0; i < n; i++) {
            text.push_back(u16(items[i].text)); accel.push_back(u16(items[i].accel));
            y.push_back(yy);
            if (items[i].flags & UT_MENU_SEPARATOR) { h.push_back(L.sep_h); yy += L.sep_h; continue; }
            h.push_back(L.item_h); yy += L.item_h;
            int a, b;
            UXMENU_MeasureItem(&L, dc.h, W(text.back()), (int)text.back().size(), items[i].accel ? W(accel.back()) : nullptr,
                               (items[i].flags & UT_MENU_DEFAULT) ? UXMENU_DEFAULT : 0, &a, &b);
            if (a > tw) tw = a;
            if (b > aw) aw = b;
        }
        w = 2 * L.pad + L.text_x + tw + (aw ? L.gap + aw : 0) + L.right_pad;
        hh = yy + L.pad;
    }
    ~MenuRun() { UXMENU_FreeLook(&L); }
};

unsigned ux_flags(unsigned f)
{
    return ((f & UT_MENU_DISABLED) ? UXMENU_DISABLED : 0) | ((f & UT_MENU_SEPARATOR) ? UXMENU_SEPARATOR : 0) |
           ((f & UT_MENU_DEFAULT) ? UXMENU_DEFAULT : 0) | ((f & UT_MENU_CHECKED) ? UXMENU_CHECKED : 0) |
           ((f & UT_MENU_RADIO) ? UXMENU_RADIO : 0) | ((f & UT_MENU_SUBMENU) ? UXMENU_SUBMENU : 0);
}

} // namespace

extern "C" {

int ut_menu_layout(const ut_menu_spec* spec, const ut_menu_item* items, int n, int* w, int* h, int* pad, int* item_y, int* item_h)
{
    Lock l;
    if (!spec || n < 0 || (n && !items)) return 0;
    MenuRun m(spec, items, n);
    if (w) *w = m.w;
    if (h) *h = m.hh;
    if (pad) *pad = m.L.pad;
    for (int i = 0; i < n; i++) { if (item_y) item_y[i] = m.y[i]; if (item_h) item_h[i] = m.h[i]; }
    return 1;
}

int ut_menu_draw(const ut_menu_spec* spec, const ut_menu_item* items, int n, int sel, int cues,
                 uint32_t* pixels, int width, int height, int stride)
{
    Lock l;
    if (!spec || n < 0 || (n && !items) || !buffer_ok(pixels, width, height, stride)) return 0;
    int smooth = gdi::font_smoothing();
    gdi::set_font_smoothing(spec->smoothing < 0 ? 0 : spec->smoothing > 2 ? 2 : spec->smoothing);
    {
        MenuRun m(spec, items, n);
        TargetDC dc(pixels, width, height, stride);
        dc.bmp->has_alpha = false;   // a menu is opaque: ClearType text, the parts blended over the plate
        RECT whole = { 0, 0, m.w, m.hh };
        UXMENU_DrawFrame(&m.L, dc.hdc(), &whole);
        for (int i = 0; i < n; i++) {
            RECT r = { m.L.pad, m.y[i], m.w - m.L.pad, m.y[i] + m.h[i] };
            unsigned f = ux_flags(items[i].flags) | (i == sel ? UXMENU_HOT : 0) | (cues ? UXMENU_CUES : 0);
            UXMENU_DrawItem(&m.L, dc.hdc(), &r, f, items[i].glyph, (items[i].flags & UT_MENU_SEPARATOR) ? nullptr : W(m.text[i]),
                            (int)m.text[i].size(), items[i].accel ? W(m.accel[i]) : nullptr, -1);
        }
        opaque_rect(pixels, width, height, stride, whole);
    }
    gdi::set_font_smoothing(smooth);
    return 1;
}

void ut_set_font_smoothing(int mode)
{
    Lock l;
    gdi::set_font_smoothing(mode < 0 ? 0 : mode > 2 ? 2 : mode);
}

} // extern "C"
