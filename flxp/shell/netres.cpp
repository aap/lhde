// The .resources layout (ResourceManager header, then the ResourceReader's):
//   int32 0xBEEFCACE, int32 header version, int32 bytes to skip (reader and
//   set type names); int32 version (2), int32 count, int32 type count, the
//   type names (7-bit length + UTF-8), padding to 8 bytes from the stream's
//   start, int32 name hashes[count], int32 name positions[count], int32 data
//   section offset; the name section: 7-bit byte length + UTF-16LE name +
//   int32 offset into the data section; a datum: 7-bit type code, for
//   ByteArray (0x20) and Stream (0x21) an int32 length and the bytes.
#include <stdint.h>
#include <string.h>
#include "netres.h"
#include "config.h"

namespace shell {

namespace {

struct Reader {
    const std::string& d;
    size_t p;
    bool ok = true;
    bool need(size_t n) { if (!ok || p > d.size() || d.size() - p < n) ok = false; return ok; }
    int32_t i32() {
        if (!need(4)) return 0;
        uint32_t v = (uint8_t)d[p] | (uint8_t)d[p + 1] << 8 | (uint8_t)d[p + 2] << 16 | (uint32_t)(uint8_t)d[p + 3] << 24;
        p += 4;
        return (int32_t)v;
    }
    uint32_t u7() {
        uint32_t v = 0;
        for (int s = 0; s < 35; s += 7) {
            if (!need(1)) return 0;
            uint8_t b = (uint8_t)d[p++];
            v |= (uint32_t)(b & 0x7f) << s;
            if (!(b & 0x80)) return v;
        }
        ok = false;
        return 0;
    }
};

std::string utf16le_of(const std::string& ascii)
{
    std::string s;
    for (char c : ascii) { s += c; s += '\0'; }
    return s;
}

bool lookup(const std::string& d, size_t base, const std::string& wname, std::string& out)
{
    Reader r{ d, base + 4 };
    r.i32();                                   // header version
    int32_t skip = r.i32();
    if (!r.ok || skip < 0) return false;
    r.p += (size_t)skip;
    int32_t version = r.i32(), count = r.i32(), ntypes = r.i32();
    if (!r.ok || version != 2 || count <= 0 || count > 100000 || ntypes < 0 || ntypes > 10000) return false;
    for (int32_t i = 0; i < ntypes && r.ok; i++) { uint32_t n = r.u7(); r.p += n; }
    while (r.ok && ((r.p - base) & 7)) r.p++;
    r.p += 4 * (size_t)count;                  // hashes
    if (!r.need(4 * (size_t)count + 4)) return false;
    size_t positions = r.p;
    r.p += 4 * (size_t)count;
    int32_t data_section = r.i32();
    size_t names = r.p;
    for (int32_t i = 0; i < count; i++) {
        Reader q{ d, positions + 4 * (size_t)i };
        int32_t pos = q.i32();
        if (pos < 0) return false;
        q.p = names + (size_t)pos;
        uint32_t len = q.u7();
        if (!q.need(len)) return false;
        bool match = len == wname.size() && !memcmp(d.data() + q.p, wname.data(), len);
        q.p += len;
        int32_t off = q.i32();
        if (!q.ok) return false;
        if (!match) continue;
        if (data_section < 0 || off < 0) return false;
        q.p = base + (size_t)data_section + (size_t)off;
        uint32_t type = q.u7();
        if (type != 0x20 && type != 0x21) return false;
        int32_t n = q.i32();
        if (n < 0 || !q.need((size_t)n)) return false;
        out.assign(d, q.p, (size_t)n);
        return true;
    }
    return false;
}

} // namespace

bool net_resource_in(const std::string& data, const std::string& name, std::string& out)
{
    static const char magic[4] = { (char)0xce, (char)0xca, (char)0xef, (char)0xbe };
    std::string wname = utf16le_of(name);
    for (size_t i = data.find(magic, 0, 4); i != std::string::npos; i = data.find(magic, i + 4, 4))
        if (lookup(data, i, wname, out)) return true;
    return false;
}

bool net_resource(const std::string& file, const std::string& name, std::string& out)
{
    std::string data;
    return read_file(file, data) && net_resource_in(data, name, out);
}

} // namespace shell
