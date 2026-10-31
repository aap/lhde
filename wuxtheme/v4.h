// The Longhorn-compiled visual style format (PACKTHEM_VERSION 4: 3683's
// plex.mst .. the 4042-4074 Luna/Slate/Jade, 5048's aero.msstyles). Read here
// and handed to Wine's msstyles.c as the same text values the XP ini holds
// (FLXP_V4_* in msstyles.h), so property lookup and draw.c are shared with XP
// styles. The generations (longhorn research themes/EARLY-MSSTYLES.md) differ
// only in padding, detected per table: G1 (3683..4005) packs VMAP, CMAP and
// the records; G2 (4015, 4029) pads VMAP and the records to 8 but still packs
// CMAP; G3+ (4039 on) pads all three. G1/G2 and the 4042 main compiles keep
// XP's straight alpha and 1..24-bpp bitmaps (Style::premultiplied).
//
// Resources (all little-endian):
//   PACKTHEM_VERSION/1  WORD 4
//   VMAP/VMAP           (variant, size, colour) name triples; each name is a
//                       DWORD character count (with the NUL) and the UTF-16
//                       string, the characters padded to 8 bytes (G2+) or not
//                       at all (G1)
//   CMAP/CMAP           class names in class-id order, UTF-16, NUL-terminated,
//                       each padded with NULs to 8 bytes (G3+) or packed
//                       (G1/G2); empty strings are not entries;
//                       "App::Class", plus documentation, sysmetrics, globals,
//                       sizevariant.<size>, colorvariant.<colour>
//   BCMAP/BCMAP         DWORD n, n ints: each class's base class id or -1
//   STBL/STBL01         the schema, text lines "1,NAME,ENUM,TYPE,ID,flag":
//                       enums (ENUMDEF/ENUMVAL; <CLASS>PARTS, <PART>STATES,
//                       <CLASS>STATES), properties (ENUM empty: id -> type
//                       name), primitive types (NAME = TYPE: id = type number;
//                       213 is BLOB in 4042 but BAMLSTREAM in 4074)
//   VARIANT/<variant>   the property records of one size+colour, and
//   RMAP/RMAP           the documentation records, each 8 DWORDs:
//                       prop id, type, class id, part, state, resid, 0, size;
//                       resid = 0: `size` bytes of value follow, padded to 8
//                       (G2+) or not padded (G1);
//                       resid != 0: no value bytes, the value is a resource:
//                       FILENAME an RT_BITMAP id, streams (PNGSTREAM,
//                       D3DXSTREAM, BAMLSTREAM; 4042's BLOB IMAGEDATA) a
//                       STREAM (BLOB) resource of `size` bytes (4015's BLOBs:
//                       "RSF1", 0, four ints, then the PNG), anything else
//                       an RT_STRING id holding the ini text (fonts
//                       "Tahoma Bold, 8", localised rects "0, 0, 380, 40")
//   inline values       INT/SIZE/BOOL/ENUM/COLOR (0x00BBGGRR) a DWORD,
//                       MARGINS/RECT 4 ints, POSITION 2 ints, STRING UTF-16,
//                       FONT a LOGFONTW (lfHeight in pixels), INTLIST a count
//                       and the ints
#pragma once
#include <stdint.h>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace res { struct Module; }

namespace v4 {

struct Prop { std::string name, type; };

// = struct flxp_v4_base
struct Base { const char16_t *app, *cls, *base_app, *base_cls; };

// = struct flxp_v4_record (msstyles.h; checked in v4.cpp)
struct Record {
    const char16_t *app, *cls;
    int part, state, prim, id;
    const char16_t* value;
    uint32_t len;
};

struct Style {
    res::Module* module = nullptr;
    std::string variant, color, size;          // the selected ones
    std::vector<std::string> colors, sizes;    // in VMAP order
    std::vector<std::string> classes;          // CMAP, by class id
    std::vector<int> base_class;               // BCMAP (may be empty)
    std::map<int, Prop> props;                 // STBL01: property/type id -> name, type name
    std::map<std::string, std::map<int, std::string>> enums;   // upper-case enum name -> value -> name
    // what msstyles.c gets (FLXP_V4_Records); the text lives in `text`
    std::vector<Record> records;
    std::vector<Base> bases;
    std::deque<std::u16string> text;
    std::u16string color_list, size_list;      // double-NUL lists for THEME_FILE
    std::vector<std::vector<uint8_t>> inline_blobs;   // "*<n>": stream values stored in the record
    bool premultiplied = true;                 // false: XP-style straight alpha (3683..4029, 4042 main)
};

// Schema lookups (the file's own STBL01; names as the schema spells them).
std::string part_name(const Style&, const std::string& cls, int part);
std::string state_name(const Style&, const std::string& cls, int part, int state);
int part_id(const Style&, const std::string& cls, const std::string& name);       // -1 if unknown
int state_id(const Style&, const std::string& cls, int part, const std::string& name);
int prop_id(const Style&, const std::string& name);
std::string enum_name(const Style&, const std::string& prop, int value);
std::string type_of(const Style&, int prop_id);         // "INT", "PNGSTREAM", ...; "" if unknown
// A stream/blob value ("#id" or "*n" as the record text gives it).
bool stream(const Style&, const std::u16string& value, const uint8_t** data, size_t* size);

} // namespace v4
