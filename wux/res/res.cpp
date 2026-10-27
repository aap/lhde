#include <stdio.h>
#include <string.h>
#include "res.h"

namespace res {

struct Module {
    std::vector<uint8_t> bytes;
    std::vector<Entry> entries;
};

// ---- ids and strings ----

static char16_t fold(char16_t c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

bool operator==(const Id& a, const Id& b)
{
    if (a.is_num() != b.is_num()) return false;
    if (a.is_num()) return a.num == b.num;
    if (a.name.size() != b.name.size()) return false;
    for (size_t i = 0; i < a.name.size(); i++)
        if (fold(a.name[i]) != fold(b.name[i])) return false;
    return true;
}

Id::Id(const char16_t* s)
{
    if (s) name = s;
}

Id::Id(const char* s)
{
    const uint8_t* p = (const uint8_t*)(s ? s : "");
    while (*p) {
        uint32_t c = *p++;
        int more = 0;
        if (c >= 0xf0) { c &= 0x07; more = 3; }
        else if (c >= 0xe0) { c &= 0x0f; more = 2; }
        else if (c >= 0xc0) { c &= 0x1f; more = 1; }
        else if (c >= 0x80) c = 0xfffd;
        for (; more > 0 && (*p & 0xc0) == 0x80; more--) c = c << 6 | (*p++ & 0x3f);
        if (more) c = 0xfffd;
        if (c >= 0x10000) {
            c -= 0x10000;
            name += (char16_t)(0xd800 | c >> 10);
            name += (char16_t)(0xdc00 | (c & 0x3ff));
        } else
            name += (char16_t)c;
    }
}

static void put_utf8(std::string& o, uint32_t c)
{
    if (c < 0x80) o += (char)c;
    else if (c < 0x800) { o += (char)(0xc0 | c >> 6); o += (char)(0x80 | (c & 0x3f)); }
    else if (c < 0x10000) {
        o += (char)(0xe0 | c >> 12); o += (char)(0x80 | (c >> 6 & 0x3f)); o += (char)(0x80 | (c & 0x3f));
    } else {
        o += (char)(0xf0 | c >> 18); o += (char)(0x80 | (c >> 12 & 0x3f));
        o += (char)(0x80 | (c >> 6 & 0x3f)); o += (char)(0x80 | (c & 0x3f));
    }
}

static std::string utf16_to_utf8(const char16_t* s, size_t n)
{
    std::string o;
    for (size_t i = 0; i < n; i++) {
        uint32_t c = s[i];
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < n && s[i + 1] >= 0xdc00 && s[i + 1] < 0xe000)
            c = 0x10000 + ((c - 0xd800) << 10) + (s[++i] - 0xdc00);
        else if (c >= 0xd800 && c < 0xe000)
            c = 0xfffd;
        put_utf8(o, c);
    }
    return o;
}

std::string to_utf8(const std::u16string& s) { return utf16_to_utf8(s.data(), s.size()); }

std::string to_utf8(const Id& id)
{
    if (id.is_num()) return std::to_string(id.num);
    return to_utf8(id.name);
}

const char* type_name(uint32_t rt)
{
    switch (rt) {
    case RT_CURSOR: return "RT_CURSOR";
    case RT_BITMAP: return "RT_BITMAP";
    case RT_ICON: return "RT_ICON";
    case RT_MENU: return "RT_MENU";
    case RT_DIALOG: return "RT_DIALOG";
    case RT_STRING: return "RT_STRING";
    case RT_FONTDIR: return "RT_FONTDIR";
    case RT_FONT: return "RT_FONT";
    case RT_ACCELERATOR: return "RT_ACCELERATOR";
    case RT_RCDATA: return "RT_RCDATA";
    case RT_MESSAGETABLE: return "RT_MESSAGETABLE";
    case RT_GROUP_CURSOR: return "RT_GROUP_CURSOR";
    case RT_GROUP_ICON: return "RT_GROUP_ICON";
    case RT_VERSION: return "RT_VERSION";
    case 17: return "RT_DLGINCLUDE";
    case 19: return "RT_PLUGPLAY";
    case 20: return "RT_VXD";
    case 21: return "RT_ANICURSOR";
    case 22: return "RT_ANIICON";
    case 23: return "RT_HTML";
    case RT_MANIFEST: return "RT_MANIFEST";
    case RT_DLGINIT: return "RT_DLGINIT";
    }
    return nullptr;
}

// ---- little-endian readers; callers check bounds ----

static inline uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline uint32_t rd32(const uint8_t* p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline bool fits(size_t size, uint64_t off, uint64_t len) { return off <= size && len <= size - off; }

// ---- PE ----

struct Section { uint32_t va, vsize, raw_off, raw_size; };

struct PE {
    const uint8_t* d;
    size_t size;
    std::vector<Section> sections;
    size_t rsrc_off, rsrc_end;      // file range of the resource directory's section
    std::vector<Entry>* out;
    size_t budget;                  // guards against directory cycles/fan-out bombs
};

// Map an RVA range to a file offset; the whole range must lie in one section's raw data.
static bool rva_to_off(const PE& pe, uint32_t rva, uint32_t len, size_t& off)
{
    for (const Section& s : pe.sections) {
        uint32_t span = s.vsize > s.raw_size ? s.vsize : s.raw_size;
        if (rva < s.va || rva - s.va >= span) continue;
        uint64_t rel = rva - s.va;
        if (rel + len > s.raw_size) return false;
        uint64_t o = (uint64_t)s.raw_off + rel;
        if (!fits(pe.size, o, len)) return false;
        off = (size_t)o;
        return true;
    }
    return false;
}

static bool pe_name(const PE& pe, uint32_t name, Id& id)
{
    if (!(name & 0x80000000u)) { id = Id((uint32_t)(name & 0xffff)); return true; }
    uint64_t o = (uint64_t)pe.rsrc_off + (name & 0x7fffffffu);
    if (!fits(pe.rsrc_end, o, 2)) return false;
    uint16_t n = rd16(pe.d + o);
    if (!fits(pe.rsrc_end, o + 2, (uint64_t)n * 2)) return false;
    id = Id();
    id.name.resize(n);
    for (uint16_t i = 0; i < n; i++) id.name[i] = rd16(pe.d + o + 2 + i * 2);
    if (id.name.empty()) id.num = 0;   // empty string name: treat as ordinal 0
    return true;
}

// level 0: types, 1: names, 2: languages
static void pe_dir(PE& pe, uint32_t dir_rel, int level, Entry& cur)
{
    uint64_t o = (uint64_t)pe.rsrc_off + dir_rel;
    if (!fits(pe.rsrc_end, o, 16)) return;
    uint32_t n = (uint32_t)rd16(pe.d + o + 12) + rd16(pe.d + o + 14);
    for (uint32_t i = 0; i < n; i++) {
        uint64_t e = o + 16 + (uint64_t)i * 8;
        if (!fits(pe.rsrc_end, e, 8)) return;
        if (pe.budget == 0) return;
        pe.budget--;
        uint32_t name = rd32(pe.d + e), data = rd32(pe.d + e + 4);
        bool sub = data & 0x80000000u;
        uint32_t rel = data & 0x7fffffffu;
        if (level < 2) {
            if (!sub) continue;
            Id id;
            if (!pe_name(pe, name, id)) continue;
            if (level == 0) cur.type = id; else cur.name = id;
            pe_dir(pe, rel, level + 1, cur);
        } else {
            if (sub) continue;
            uint64_t de = (uint64_t)pe.rsrc_off + rel;     // IMAGE_RESOURCE_DATA_ENTRY
            if (!fits(pe.rsrc_end, de, 16)) continue;
            uint32_t rva = rd32(pe.d + de), len = rd32(pe.d + de + 4);
            size_t off;
            if (!rva_to_off(pe, rva, len, off)) continue;
            cur.lang = (name & 0x80000000u) ? 0 : (uint16_t)name;
            cur.data = Blob{pe.d + off, len};
            pe.out->push_back(cur);
        }
    }
}

static bool parse_pe(Module* m)
{
    const uint8_t* d = m->bytes.data();
    size_t size = m->bytes.size();
    if (size < 0x40 || d[0] != 'M' || d[1] != 'Z') return false;
    uint32_t pe_off = rd32(d + 0x3c);
    if (!fits(size, pe_off, 24) || memcmp(d + pe_off, "PE\0\0", 4)) return false;
    const uint8_t* coff = d + pe_off + 4;
    uint16_t nsect = rd16(coff + 2), opt_size = rd16(coff + 16);
    uint64_t opt = (uint64_t)pe_off + 24;
    if (!fits(size, opt, opt_size) || opt_size < 2) return false;
    uint16_t magic = rd16(d + opt);
    uint32_t ndir_at, dirs_at;
    if (magic == 0x10b) { ndir_at = 92; dirs_at = 96; }
    else if (magic == 0x20b) { ndir_at = 108; dirs_at = 112; }
    else return false;
    if (opt_size < dirs_at) return false;
    uint32_t ndirs = rd32(d + opt + ndir_at);
    if (ndirs < 3 || opt_size < dirs_at + 3 * 8) return false;   // no resource directory
    uint32_t rsrc_rva = rd32(d + opt + dirs_at + 16), rsrc_size = rd32(d + opt + dirs_at + 20);
    if (!rsrc_rva || !rsrc_size) return false;

    PE pe;
    pe.d = d; pe.size = size; pe.out = &m->entries; pe.budget = 1 << 20;
    uint64_t st = opt + opt_size;
    if (!fits(size, st, (uint64_t)nsect * 40)) return false;
    for (uint16_t i = 0; i < nsect; i++) {
        const uint8_t* s = d + st + i * 40;
        Section sec{rd32(s + 12), rd32(s + 8), rd32(s + 20), rd32(s + 16)};
        // clamp raw data to the file
        if (sec.raw_off > size) sec.raw_size = 0;
        else if (sec.raw_size > size - sec.raw_off) sec.raw_size = (uint32_t)(size - sec.raw_off);
        pe.sections.push_back(sec);
    }
    // the resource directory tree lives in the section containing rsrc_rva
    size_t off;
    if (!rva_to_off(pe, rsrc_rva, 16, off)) return false;
    pe.rsrc_off = off;
    pe.rsrc_end = off;
    for (const Section& s : pe.sections)
        if (rsrc_rva >= s.va && rsrc_rva - s.va < s.raw_size)
            pe.rsrc_end = (size_t)s.raw_off + s.raw_size;
    Entry cur{};
    pe_dir(pe, 0, 0, cur);
    return true;
}

// ---- .res ----

// Reads an ordinal (0xFFFF, id) or nul-terminated UTF-16 string at d[o..end).
static bool res_id(const uint8_t* d, size_t end, size_t& o, Id& id)
{
    if (!fits(end, o, 2)) return false;
    if (rd16(d + o) == 0xffff) {
        if (!fits(end, o, 4)) return false;
        id = Id((uint32_t)rd16(d + o + 2));
        o += 4;
        return true;
    }
    id = Id();
    for (;;) {
        if (!fits(end, o, 2)) return false;
        char16_t c = rd16(d + o);
        o += 2;
        if (!c) break;
        id.name += c;
    }
    return true;
}

static bool is_res(const uint8_t* d, size_t size)
{
    return size >= 32 && rd32(d) == 0 && rd32(d + 4) == 32 && rd16(d + 8) == 0xffff && rd16(d + 12) == 0xffff;
}

static bool parse_res(Module* m)
{
    const uint8_t* d = m->bytes.data();
    size_t size = m->bytes.size();
    size_t pos = 0;
    while (fits(size, pos, 8)) {
        uint32_t data_size = rd32(d + pos), hdr_size = rd32(d + pos + 4);
        if (hdr_size < 16 + 8 || !fits(size, pos, hdr_size)) break;
        size_t hend = pos + hdr_size, o = pos + 8;
        Entry e{};
        if (!res_id(d, hend, o, e.type) || !res_id(d, hend, o, e.name)) break;
        o = pos + ((o - pos + 3) & ~(size_t)3);
        if (!fits(hend, o, 16)) break;
        e.lang = rd16(d + o + 6);
        if (!fits(size, hend, data_size)) break;
        e.data = Blob{d + hend, data_size};
        // skip the empty leading header (type 0, name 0, no data)
        if (!(data_size == 0 && e.type.is_num() && e.type.num == 0))
            m->entries.push_back(e);
        uint64_t next = (uint64_t)hend + data_size;
        next = (next + 3) & ~(uint64_t)3;
        if (next <= pos) break;
        pos = (size_t)next;
    }
    return true;
}

// ---- public ----

static Module* parse(Module* m)
{
    const uint8_t* d = m->bytes.data();
    size_t size = m->bytes.size();
    bool ok = false;
    if (size >= 2 && d[0] == 'M' && d[1] == 'Z') ok = parse_pe(m);
    else if (is_res(d, size)) ok = parse_res(m);
    if (!ok) { delete m; return nullptr; }
    return m;
}

Module* load_memory(const uint8_t* data, size_t size)
{
    Module* m = new Module;
    m->bytes.assign(data, data + size);
    return parse(m);
}

Module* load(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    Module* m = new Module;
    uint8_t tmp[65536];
    size_t n;
    while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) m->bytes.insert(m->bytes.end(), tmp, tmp + n);
    bool err = ferror(f);
    fclose(f);
    if (err) { delete m; return nullptr; }
    return parse(m);
}

void unload(Module* m) { delete m; }

bool find(const Module* m, const Id& type, const Id& name, Blob& out, uint16_t lang)
{
    if (!m) return false;
    // lang 0: any language, preferring neutral, then en-US, then any English,
    // then whatever comes first. Without this a multilingual PE (Wine's
    // aero.msstyles) would hand out Arabic strings because 0x0001 sorts first.
    const Entry* best = nullptr;
    int best_rank = 99;
    for (const Entry& e : m->entries) {
        if (lang != 0 && e.lang != lang) continue;
        if (e.type != type || e.name != name) continue;
        int rank = lang != 0 || e.lang == 0 ? 0 : e.lang == 0x409 ? 1 : (e.lang & 0x3ff) == 9 ? 2 : 3;
        if (rank < best_rank) { best = &e; best_rank = rank; }
        if (rank == 0) break;
    }
    if (!best) return false;
    out = best->data;
    return true;
}

std::vector<Entry> list(const Module* m, const Id* type)
{
    std::vector<Entry> v;
    if (!m) return v;
    for (const Entry& e : m->entries)
        if (!type || e.type == *type) v.push_back(e);
    return v;
}

bool load_string(const Module* m, uint32_t id, std::string& out, uint16_t lang)
{
    Blob b;
    if (!find(m, Id((uint32_t)RT_STRING), Id(id / 16 + 1), b, lang)) return false;
    size_t o = 0;
    for (uint32_t i = 0; i < 16; i++) {
        if (!fits(b.size, o, 2)) return false;
        uint16_t n = rd16(b.data + o);
        o += 2;
        if (!fits(b.size, o, (uint64_t)n * 2)) return false;
        if (i == id % 16) {
            if (!n) return false;
            std::u16string s(n, 0);
            for (uint16_t k = 0; k < n; k++) s[k] = rd16(b.data + o + k * 2);
            out = to_utf8(s);
            return true;
        }
        o += (size_t)n * 2;
    }
    return false;
}

} // namespace res
