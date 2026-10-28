// The Longhorn-compiled visual style reader (format: v4.h). Every record of
// the selected variant becomes the text the XP ini would have held, so Wine's
// msstyles.c keeps one property model for both formats.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "res/res.h"
#include "wuxtheme/v4.h"
extern "C" {
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "uxtheme.h"
#include "vssym32.h"
#include "msstyles.h"
}

static_assert(sizeof(v4::Record) == sizeof(flxp_v4_record), "v4::Record = flxp_v4_record");
static_assert(sizeof(v4::Base) == sizeof(flxp_v4_base), "v4::Base = flxp_v4_base");
static_assert(sizeof(WCHAR) == sizeof(char16_t), "16-bit WCHAR");

namespace v4 {

namespace {

std::string upper(std::string s)
{
    for (char& c : s) if (c >= 'a' && c <= 'z') c -= 32;
    return s;
}

bool ieq(const std::string& a, const std::string& b) { return upper(a) == upper(b); }

std::u16string widen(const std::string& s)       // ASCII/Latin-1 (schema, numbers)
{
    std::u16string o;
    for (unsigned char c : s) o += (char16_t)c;
    return o;
}

std::string narrow(const std::u16string& s)      // names are ASCII
{
    std::string o;
    for (char16_t c : s) o += c < 0x80 ? (char)c : '?';
    return o;
}

std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::vector<std::string> split(const std::string& s, char sep)
{
    std::vector<std::string> o;
    size_t a = 0;
    for (;;) {
        size_t b = s.find(sep, a);
        o.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) return o;
        a = b + 1;
    }
}

bool blob(res::Module* m, const res::Id& type, const res::Id& name, res::Blob& b)
{
    return res::find(m, type, name, b) && b.data;
}

uint32_t u32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
int32_t i32(const uint8_t* p) { int32_t v; memcpy(&v, p, 4); return v; }

// RT_STRING entry `id` in UTF-16 (16 per block, block = id / 16 + 1).
bool load_string16(res::Module* m, uint32_t id, std::u16string& out)
{
    res::Blob b;
    if (!blob(m, res::Id(6u), res::Id(id / 16 + 1), b)) return false;
    size_t o = 0;
    for (uint32_t i = 0; i < 16; i++) {
        if (o + 2 > b.size) return false;
        uint16_t n; memcpy(&n, b.data + o, 2); o += 2;
        if (o + 2 * (size_t)n > b.size) return false;
        if (i == id % 16) {
            out.assign((const char16_t*)(b.data + o), n);
            return n > 0;
        }
        o += 2 * (size_t)n;
    }
    return false;
}

// VMAP: (variant, size, colour) triples.
std::vector<std::string> vmap_names(const res::Blob& b)
{
    std::vector<std::string> o;
    size_t off = 0;
    while (off + 4 <= b.size) {
        uint32_t n = u32(b.data + off);
        if (n == 0 || n > 256 || off + 4 + 2 * (size_t)n > b.size) break;
        std::u16string s((const char16_t*)(b.data + off + 4), n);
        while (!s.empty() && !s.back()) s.pop_back();
        o.push_back(narrow(s));
        off += 4 + ((2 * (size_t)n + 7) & ~(size_t)7);    // the characters padded to 8 bytes
    }
    return o;
}

void parse_schema(Style& st)
{
    res::Id stbl("STBL");
    for (const res::Entry& e : res::list(st.module, &stbl))       // STBL01, STBL02 (Luna's AvalonButton) ...
    for (std::string line : split(std::string((const char*)e.data.data, e.data.size), '\n')) {
        line = trim(line);
        std::vector<std::string> f = split(line, ',');
        if (f.size() < 5) continue;
        const std::string &name = f[1], &en = f[2], &type = f[3], &id = f[4];
        if (type == "ENUMVAL") st.enums[upper(en)][atoi(id.c_str())] = name;
        else if (type != "ENUMDEF" && en.empty() && !id.empty() && id.find_first_not_of("0123456789") == std::string::npos)
            st.props.emplace(atoi(id.c_str()), Prop{name, type});   // ids defined twice: the first wins (Luna STBL02 8020..8022)
    }
}

int wine_primitive(const std::string& type)
{
    static const struct { const char* name; int prim; } map[] = {
        {"ENUM", TMT_ENUM}, {"STRING", TMT_STRING}, {"INT", TMT_INT}, {"BOOL", TMT_BOOL},
        {"COLOR", TMT_COLOR}, {"MARGINS", TMT_MARGINS}, {"FILENAME", TMT_FILENAME},
        {"SIZE", TMT_SIZE}, {"POSITION", TMT_POSITION}, {"RECT", TMT_RECT}, {"FONT", TMT_FONT},
        {"INTLIST", TMT_INTLIST},
    };
    for (auto& m : map) if (type == m.name) return m.prim;
    return 0;
}

std::string join_ints(const uint8_t* p, size_t n)
{
    std::string o;
    for (size_t i = 0; i < n; i++) {
        if (i) o += ", ";
        o += std::to_string(i32(p + 4 * i));
    }
    return o;
}

// "Tahoma Bold, 8[, Quality:ClearType]" -> "Tahoma, 8, bold[, Quality:ClearType]"
// (msstyles.c reads the face up to the comma, then the size and attributes)
std::u16string font_text(const std::u16string& s)
{
    std::vector<std::string> f = split(narrow(s), ',');
    std::string face = trim(f[0]), attrs;
    for (bool again = true; again;) {
        again = false;
        for (const char* st : {" Bold", " Italic"}) {
            size_t n = strlen(st);
            if (face.size() > n && ieq(face.substr(face.size() - n), st)) {
                face.resize(face.size() - n);
                attrs = ", " + std::string(st[1] == 'B' ? "bold" : "italic") + attrs;
                again = true;
            }
        }
    }
    if (attrs.empty()) return s;                     // nothing to move (keeps non-ASCII faces intact)
    std::string o = face;
    if (f.size() > 1) o += ", " + trim(f[1]);
    o += attrs;
    for (size_t i = 2; i < f.size(); i++) o += ", " + trim(f[i]);
    return widen(o);
}

std::u16string logfont_text(const uint8_t* p, size_t n)
{
    if (n < sizeof(LOGFONTW)) return u"";
    LOGFONTW lf;
    memcpy(&lf, p, sizeof lf);
    std::u16string face;
    for (int i = 0; i < LF_FACESIZE && lf.lfFaceName[i]; i++) face += (char16_t)lf.lfFaceName[i];
    // A face named by its full name ("Tahoma Bold", "Segoe UI Bold" at weight 400: 4074's
    // StartPanel.MOREPROGRAMS, 5048's): Windows' font mapper matches the full name and draws
    // the bold face; ours matches families only, so the style goes into the attributes
    // (as font_text does for the text form). The XP backports wrote "Tahoma, 8, bold".
    bool bold = lf.lfWeight >= FW_SEMIBOLD, italic = lf.lfItalic;
    for (bool again = true; again;) {
        again = false;
        for (const char* st : {" Bold", " Italic"}) {
            size_t n = strlen(st);
            if (face.size() > n && ieq(narrow(face.substr(face.size() - n)), st)) {
                face.resize(face.size() - n);
                (st[1] == 'B' ? bold : italic) = true;
                again = true;
            }
        }
    }
    // a negative size: the pixel height itself (msstyles.c flxp: patch)
    std::string rest = ", " + std::to_string(lf.lfHeight > 0 ? -(int)lf.lfHeight : (int)lf.lfHeight);
    if (bold) rest += ", bold";
    if (italic) rest += ", italic";
    if (lf.lfUnderline) rest += ", underline";
    if (lf.lfStrikeOut) rest += ", strikeout";
    return face + widen(rest);
}

// Class id -> (app, class) as msstyles.c stores them.
void class_names(Style& st, int cls, const char16_t** app16, const char16_t** cls16)
{
    std::string cname = cls >= 0 && cls < (int)st.classes.size() ? st.classes[cls] : "cls" + std::to_string(cls);
    std::string app, cn = cname;
    size_t sep = cname.rfind("::");
    if (sep != std::string::npos) { app = cname.substr(0, sep); cn = cname.substr(sep + 2); }
    if (app.size() >= MAX_THEME_APP_NAME) app.resize(MAX_THEME_APP_NAME - 1);
    if (cn.size() >= MAX_THEME_CLASS_NAME) cn.resize(MAX_THEME_CLASS_NAME - 1);
    st.text.push_back(widen(app));
    *app16 = st.text.back().c_str();
    st.text.push_back(widen(cn));
    *cls16 = st.text.back().c_str();
}

void add_records(Style& st, const res::Blob& b)
{
    size_t off = 0;
    while (off + 32 <= b.size) {
        const uint8_t* r = b.data + off;
        int prop = (int)u32(r), typ = (int)u32(r + 4), cls = (int)u32(r + 8);
        int part = (int)u32(r + 12), state = (int)u32(r + 16);
        uint32_t resid = u32(r + 20), size = u32(r + 28);
        const uint8_t* pay = r + 32;
        if (!resid && off + 32 + (size_t)size > b.size) break;
        off = resid ? off + 32 : (off + 32 + size + 7) & ~(size_t)7;

        std::string type;
        auto t = st.props.find(typ);
        if (t != st.props.end()) type = t->second.type;
        else if ((t = st.props.find(prop)) != st.props.end()) type = t->second.type;

        std::u16string v, s;
        if (type == "FILENAME" || (resid && (type.find("STREAM") != std::string::npos || type == "BLOB" ||
                                              type == "DIBDATA" || type == "HBITMAP")))
            v = resid ? u"#" + widen(std::to_string(resid)) : u"";
        else if (resid) {
            if (!load_string16(st.module, resid, s)) s = widen(std::to_string(resid));
            v = type == "FONT" ? font_text(s) : s;
        } else if (type == "INT" || type == "SIZE" || type == "ENUM") {
            if (size >= 4) v = widen(std::to_string(i32(pay)));
        } else if (type == "BOOL") {
            if (size >= 4) v = i32(pay) ? u"TRUE" : u"FALSE";
        } else if (type == "COLOR") {
            if (size >= 4) {
                uint32_t c = u32(pay);
                v = widen(std::to_string(c & 255) + ", " + std::to_string(c >> 8 & 255) + ", " + std::to_string(c >> 16 & 255));
            }
        } else if (type == "MARGINS" || type == "RECT") {
            if (size >= 16) v = widen(join_ints(pay, 4));
        } else if (type == "POSITION") {
            if (size >= 8) v = widen(join_ints(pay, 2));
        } else if (type == "INTLIST") {
            if (size >= 4) {
                size_t n = (size_t)u32(pay);
                if (4 + 4 * n > size) n = (size - 4) / 4;
                v = widen(join_ints(pay + 4, n));
            }
        } else if (type == "STRING") {
            v.assign((const char16_t*)pay, size / 2);
            size_t z = v.find(u'\0');
            if (z != std::u16string::npos) v.resize(z);
        } else if (type == "FONT") {
            v = logfont_text(pay, size);
        } else {                                          // streams, blobs, unknown: raw bytes
            v = u"*" + widen(std::to_string(st.inline_blobs.size()));
            st.inline_blobs.emplace_back(pay, pay + size);
        }

        const char16_t *app16, *cls16;
        class_names(st, cls, &app16, &cls16);
        st.text.push_back(v);
        int prim = wine_primitive(type);
        st.records.push_back(Record{app16, cls16, part, state, prim ? prim : typ, prop,
                                    st.text.back().c_str(), (uint32_t)st.text.back().size()});
    }
}

} // namespace

std::string part_name(const Style& st, const std::string& cls, int part)
{
    std::string base = upper(cls.substr(cls.find("::") == std::string::npos ? 0 : cls.find("::") + 2));
    auto e = st.enums.find(base + "PARTS");
    if (e == st.enums.end()) return "";
    auto v = e->second.find(part);
    return v == e->second.end() ? "" : v->second;
}

std::string state_name(const Style& st, const std::string& cls, int part, int state)
{
    std::string base = upper(cls.substr(cls.find("::") == std::string::npos ? 0 : cls.find("::") + 2));
    std::string pn = part_name(st, cls, part);
    for (const std::string& key : {upper(pn) + "STATES", base + "STATES"}) {
        if (key == "STATES") continue;
        auto e = st.enums.find(key);
        if (e == st.enums.end()) continue;
        auto v = e->second.find(state);
        if (v != e->second.end()) return v->second;
    }
    return "";
}

int part_id(const Style& st, const std::string& cls, const std::string& name)
{
    std::string base = upper(cls.substr(cls.find("::") == std::string::npos ? 0 : cls.find("::") + 2));
    auto e = st.enums.find(base + "PARTS");
    if (e != st.enums.end())
        for (auto& v : e->second) if (ieq(v.second, name)) return v.first;
    return -1;
}

int state_id(const Style& st, const std::string& cls, int part, const std::string& name)
{
    std::string base = upper(cls.substr(cls.find("::") == std::string::npos ? 0 : cls.find("::") + 2));
    std::string pn = part_name(st, cls, part);
    for (const std::string& key : {upper(pn) + "STATES", base + "STATES"}) {
        if (key == "STATES") continue;
        auto e = st.enums.find(key);
        if (e == st.enums.end()) continue;
        for (auto& v : e->second) if (ieq(v.second, name)) return v.first;
    }
    return -1;
}

int prop_id(const Style& st, const std::string& name)
{
    for (auto& p : st.props) if (ieq(p.second.name, name) && p.second.name != p.second.type) return p.first;
    for (auto& p : st.props) if (ieq(p.second.name, name)) return p.first;
    return -1;
}

std::string enum_name(const Style& st, const std::string& prop, int value)
{
    auto e = st.enums.find(upper(prop));
    if (e == st.enums.end()) return "";
    auto v = e->second.find(value);
    return v == e->second.end() ? "" : v->second;
}

std::string type_of(const Style& st, int id)
{
    auto p = st.props.find(id);
    return p == st.props.end() ? "" : p->second.type;
}

bool stream(const Style& st, const std::u16string& value, const uint8_t** data, size_t* size)
{
    if (value.size() < 2) return false;
    uint32_t n = (uint32_t)strtoul(narrow(value.substr(1)).c_str(), nullptr, 10);
    if (value[0] == u'*') {
        if (n >= st.inline_blobs.size()) return false;
        *data = st.inline_blobs[n].data(); *size = st.inline_blobs[n].size();
        return true;
    }
    if (value[0] != u'#') return false;
    res::Blob b;
    for (const char* type : {"STREAM", "BLOB"})
        if (blob(st.module, res::Id(type), res::Id(n), b)) { *data = b.data; *size = b.size; return true; }
    if (blob(st.module, res::Id(2u), res::Id(n), b)) { *data = b.data; *size = b.size; return true; }
    return false;
}

} // namespace v4

// ---- the msstyles.c side (msstyles.h) ----

extern "C" HRESULT FLXP_V4_Open(HMODULE hTheme, LPCWSTR pszColorName, LPCWSTR pszSizeName, void** out,
                                LPWSTR* pszColors, LPWSTR* pszSelectedColor, LPWSTR* pszSizes, LPWSTR* pszSelectedSize)
{
    using namespace v4;
    *out = nullptr;
    res::Module* m = (res::Module*)hTheme;
    res::Blob b;
    if (!blob(m, res::Id("VMAP"), res::Id("VMAP"), b)) return HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
    std::vector<std::string> names = vmap_names(b);
    struct Triple { std::string variant, size, color; };
    std::vector<Triple> triples;
    for (size_t i = 0; i + 2 < names.size(); i += 3) triples.push_back(Triple{names[i], names[i + 1], names[i + 2]});
    if (triples.empty()) return HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);

    Style* st = new Style;
    st->module = m;
    for (const Triple& t : triples) {
        bool c = false, s = false;
        for (auto& x : st->colors) c |= ieq(x, t.color);
        for (auto& x : st->sizes) s |= ieq(x, t.size);
        if (!c) st->colors.push_back(t.color);
        if (!s) st->sizes.push_back(t.size);
    }
    // a colour or size the style does not have selects its first one (as for
    // XP styles, msstyles.c flxp: patch): Longhorn says Default/Normal, XP
    // NormalColor/NormalSize
    std::string want_c = pszColorName ? narrow((const char16_t*)pszColorName) : "";
    std::string want_s = pszSizeName ? narrow((const char16_t*)pszSizeName) : "";
    bool have_c = false, have_s = false;
    for (auto& x : st->colors) have_c |= ieq(x, want_c);
    for (auto& x : st->sizes) have_s |= ieq(x, want_s);
    if (!have_c) want_c = st->colors[0];
    if (!have_s) want_s = st->sizes[0];
    const Triple* pick = nullptr;
    for (const Triple& t : triples) if (ieq(t.color, want_c) && ieq(t.size, want_s)) { pick = &t; break; }
    for (const Triple& t : triples) if (!pick && ieq(t.color, want_c)) pick = &t;
    if (!pick) pick = &triples[0];
    st->variant = pick->variant; st->color = pick->color; st->size = pick->size;

    res::Blob var;
    if (!blob(m, res::Id("VARIANT"), res::Id(st->variant.c_str()), var)) {
        delete st;
        return HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
    }
    if (blob(m, res::Id("CMAP"), res::Id("CMAP"), b)) {          // NUL-terminated names, each padded to 8 bytes
        size_t o = 0;
        while (o + 2 <= b.size) {
            std::u16string name;
            size_t n = 0;
            while (o + 2 * n + 2 <= b.size && ((const char16_t*)(b.data + o))[n]) n++;
            name.assign((const char16_t*)(b.data + o), n);
            st->classes.push_back(narrow(name));
            o += (2 * (n + 1) + 7) & ~(size_t)7;
        }
    }
    if (blob(m, res::Id("BCMAP"), res::Id("BCMAP"), b) && b.size >= 4) {
        uint32_t n = u32(b.data);
        for (uint32_t i = 0; i < n && 4 + 4 * (size_t)(i + 1) <= b.size; i++) st->base_class.push_back(i32(b.data + 4 + 4 * i));
    }
    // BCMAP entry i is class globals + i; its base is globals + value
    int g = -1;
    for (size_t i = 0; i < st->classes.size(); i++) if (ieq(st->classes[i], "globals")) { g = (int)i; break; }
    for (size_t i = 0; g >= 0 && i < st->base_class.size(); i++) {
        int v = st->base_class[i];
        if (v < 0 || g + v >= (int)st->classes.size() || g + (int)i >= (int)st->classes.size()) continue;
        Base b;
        class_names(*st, g + (int)i, &b.app, &b.cls);
        class_names(*st, g + v, &b.base_app, &b.base_cls);
        st->bases.push_back(b);
    }
    parse_schema(*st);
    add_records(*st, var);
    if (blob(m, res::Id("RMAP"), res::Id("RMAP"), b)) add_records(*st, b);

    for (auto& c : st->colors) { st->color_list += widen(c); st->color_list += u'\0'; }
    for (auto& s : st->sizes) { st->size_list += widen(s); st->size_list += u'\0'; }
    st->color_list += u'\0'; st->size_list += u'\0';
    auto find_in = [](std::u16string& list, const std::string& name) {
        size_t a = 0;
        while (a < list.size() && list[a]) {
            size_t z = list.find(u'\0', a);
            if (ieq(narrow(list.substr(a, z - a)), name)) return (LPWSTR)&list[a];
            a = z + 1;
        }
        return (LPWSTR)&list[0];
    };
    *pszColors = (LPWSTR)&st->color_list[0];
    *pszSizes = (LPWSTR)&st->size_list[0];
    *pszSelectedColor = find_in(st->color_list, st->color);
    *pszSelectedSize = find_in(st->size_list, st->size);
    *out = st;
    return S_OK;
}

extern "C" int FLXP_V4_Records(void* v4, const struct flxp_v4_record** records)
{
    v4::Style* st = (v4::Style*)v4;
    *records = (const flxp_v4_record*)st->records.data();
    return (int)st->records.size();
}

extern "C" int FLXP_V4_Bases(void* v4, const struct flxp_v4_base** bases)
{
    v4::Style* st = (v4::Style*)v4;
    *bases = (const flxp_v4_base*)st->bases.data();
    return (int)st->bases.size();
}

extern "C" void FLXP_V4_Close(void* v4)
{
    delete (v4::Style*)v4;
}
