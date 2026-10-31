// What the Explorer says about the folder shown or the selection: one model,
// two presentations. XP's task pane shows it as the "Details" section (shell32
// duiinfo.cpp), 4051's band as its preview (System.Windows.Explorer's
// WelcomePane and PreviewManager). No win32 here.
//
// The model is 4051's: per item kind a property list (4051's
// PKEY_PROPLIST_PREVIEWPANE_DETAILS, XP's per-type "Details" value) naming
// properties of the registry; the result is rows of {property, label, value,
// editable, invitation}. XP and 4051 show the same rows, each in its own
// order and format:
//
//   kind      XP's Details (selfreg.inx, the folders' GetDetailsEx)          4051's band
//   file      Type; Attributes; Date Modified; Size                         size, date, type
//   picture   Type; Dimensions; Size; Date Modified                         size, date, dimensions
//   song      Size; Type (XP's Artist, Album, Year, Track, Duration:        size, date, type
//             tags are not read)
//   folder    Type; Attributes; Date Modified (a folder has no size)        date, type
//   drive     Type; File System; Free Space; Total Size                     type, free space
//   virtual   Type ("System Folder")                                        type
//   files and folders also: Description (user.lux.desc), Tags (user.xdg.tags),
//   Comment (user.xdg.comment), read-only for now; XP shows them when set,
//   4051 always, an empty one as its invitation ("Add a description", ...).
//
// XP: the name in bold above the lines; Type is the value alone, the others
// "Label: value" (IDS_COLONSEPERATED); an empty value drops its line, as does
// a size of 0. Attributes lists Read-only (files only) and Hidden, joined by
// ", ". Sizes as StrFormatByteSize ("198 bytes", "1.30 KB", "2.39 MB": three
// digits, cut), Date Modified as SHFormatDateTime's long date with
// FDTF_RELATIVE ("Today, October 31, 2026, 12:45 PM"). Nothing selected: the
// folder itself as the item. Several: "N items selected." and, when the files
// among at most 100 of them add up to more than 0 bytes, an empty line and
// "Total File Size: X". A single non-folder with a thumbnail gets the mini
// preview above its name (not on a network mount: XP's slow items).
//
// 4051: nothing selected "N items" / "in this folder" (the list's count,
// " item" for one, "in this folder" only when N is not 0); one item its name
// as the title and the values without labels (size as the details column's
// KB, date and time, a picture's date alone); several "N items selected" and
// per value the shared one or "(multiple values)"; then "all properties...".
// The pictures of the stack: up to 7, the item of interest first.
#pragma once
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>
#include "namespace.h"

namespace shell {
namespace details {

// The property registry: what a property is, apart from where it is stored.
// A stored one names its extended attribute; a derived one (type, size, date,
// dimensions, a drive's space) has no attribute and is computed from the item
// and its facts. The item kinds' lists name properties by id only, so
// mappings can be added or renamed here (later from a data file: shipped
// defaults with the user's overrides) without touching the lists.
enum class ValueType { Text, List, Date, Size, Number };
struct Property {
    std::string id;                     // "tags"
    std::string label;                  // "Tags" (XP's "Label: value")
    ValueType type = ValueType::Text;
    std::string xattr;                  // "user.xdg.tags"; "" = derived
    bool editable = false;              // the user sets it (4051: a click edits it; not yet)
    std::string invitation;             // shown while it is empty ("Add tags"), 4051 only
};
const std::vector<Property>& registry();                // the defaults, in code for now
const Property* property(const std::string& id);        // null: unknown

// What the file system says about an item beyond shell::Item (probe() reads it)
struct Facts {
    enum Kind { Other, Picture, Audio, Drive, System } kind = Other;
    int width = 0, height = 0;          // a picture's dimensions (0: unknown)
    bool thumbnail = false;             // a thumbnail can be made of it (shell::thumbs)
    std::string fs;                     // a drive's file system ("ext4")
    uint64_t free = 0, total = 0;       // a drive's free and total bytes (total 0: unknown)
    bool network = false;               // on a network mount
    std::vector<std::pair<std::string, std::string>> xattrs;   // the registry's attributes that are set: name, value
};

struct Row {
    std::string id;                     // the property
    std::string label;                  // XP's label; "" = the value alone (the type)
    std::string xp;                     // XP's value ("" = not shown there)
    std::string lh;                     // 4051's value ("" = the invitation, or not shown)
    bool editable = false;
    std::string invitation;
    int xp_rank = -1, lh_rank = -1;     // the place in each presentation (-1: not there)
};

enum class Kind { Location, One, Many };

struct Info {
    Kind kind = Kind::Location;
    int count = 0;                      // Location: the items in it; One/Many: the selection's size
    std::string icon;                   // the icon key: the location's, the item's, the focused one's
    std::string name;                   // the location's or the item's name ("" for Many)
    std::vector<Row> rows;
    uint64_t total = 0;                 // Many: the files' bytes (0 for more than 100 items, as XP)
    bool xp_preview = false;            // XP's mini preview (a single non-folder that has a thumbnail)
    std::vector<std::string> previews;  // 4051's pictures (One: the item; Many: up to 7)
};

// The two presentations
struct Line {
    std::string label, value;
    std::string text() const { return label.empty() ? value : label + ": " + value; }
};
std::string xp_title(const Info& in);               // the bold line: the name ("" for Many)
std::vector<Line> xp_lines(const Info& in);         // the lines under it, in XP's order
struct LhLine { std::string text; bool invitation = false; bool editable = false; };
std::string lh_title(const Info& in);               // the name, "N items selected" ("" for Location)
std::vector<LhLine> lh_lines(const Info& in);       // the values in 4051's order

// Formats
std::string byte_size(uint64_t bytes);                  // StrFormatByteSize: "198 bytes", "1.30 KB", "2.39 MB"
std::string friendly_date(int64_t t, int64_t now);      // "Today, October 31, 2026, 12:45 PM"
std::string short_date(int64_t t);                      // "8/24/2001"
std::string attributes(const Item& it);                 // "Read-only, Hidden"; "" for none
std::string tag_list(const std::string& value);         // a list value: "a,b, c" -> "a, b, c"
std::string items_selected_xp(size_t n);                // "3 items selected."
std::string lh_items_word(int n);                       // " item" / " items" (after the number)

Facts probe(const Item& it);                            // reads the file (header, statvfs, user xattrs)
Item location_item(const Folder& f);                    // the folder shown as an item (XP: its own details)

// The model. facts[i] belongs to sel[i] (may be shorter: the rest count as
// Other); `focus` is the item of interest in sel (-1: the first).
Info describe(const std::vector<Item>& sel, const std::vector<Facts>& facts, const Item& location,
              const Facts& location_facts, int count, int64_t now, int focus = -1);
// The same with the facts read from the file system
Info describe(const std::vector<Item>& sel, const Item& location, int count, int focus = -1);

} // namespace details
} // namespace shell
