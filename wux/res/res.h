// Windows resources, read straight from a PE image (exe/dll/msstyles) or from
// a 32-bit .res file as written by rc/windres/wrc. Only the resource section
// of a PE is looked at: no relocation, no imports.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

namespace res {

struct Blob { const uint8_t* data; size_t size; };

// A resource type or name: either a 16-bit ordinal or a UTF-16 string.
// String ids compare case-insensitively (ASCII folding), like FindResource.
struct Id {
    uint32_t num = 0;             // valid if name.empty()
    std::u16string name;
    Id() = default;
    Id(uint32_t n) : num(n) {}
    Id(const char* utf8);
    Id(const char16_t* s);
    bool is_num() const { return name.empty(); }
};
bool operator==(const Id& a, const Id& b);
inline bool operator!=(const Id& a, const Id& b) { return !(a == b); }

enum : uint32_t {
    RT_CURSOR = 1, RT_BITMAP = 2, RT_ICON = 3, RT_MENU = 4, RT_DIALOG = 5, RT_STRING = 6,
    RT_FONTDIR = 7, RT_FONT = 8, RT_ACCELERATOR = 9, RT_RCDATA = 10, RT_MESSAGETABLE = 11,
    RT_GROUP_CURSOR = 12, RT_GROUP_ICON = 14, RT_VERSION = 16, RT_DLGINIT = 240, RT_MANIFEST = 24
};

struct Entry { Id type; Id name; uint16_t lang; Blob data; };

struct Module;                    // opaque; owns the file bytes

// Sniffs the format: PE (MZ header) or 32-bit .res. Null on failure. Corrupt
// entries are skipped; Blobs stay valid until unload().
Module* load(const char* path);
Module* load_memory(const uint8_t* data, size_t size);   // copies the bytes
void unload(Module*);

// lang 0 = any: neutral (0) preferred, then en-US, then any English, then the
// first in file order. Otherwise an exact LANGID match.
bool find(const Module*, const Id& type, const Id& name, Blob& out, uint16_t lang = 0);
std::vector<Entry> list(const Module*, const Id* type = nullptr);   // file order
// RT_STRING: 16 strings per block, block name = id/16 + 1. False if absent or empty.
bool load_string(const Module*, uint32_t id, std::string& out_utf8, uint16_t lang = 0);

// Helpers.
std::string to_utf8(const std::u16string& s);
std::string to_utf8(const Id& id);          // decimal number or the string
const char* type_name(uint32_t rt);         // "RT_BITMAP" etc., null if not standard

} // namespace res
