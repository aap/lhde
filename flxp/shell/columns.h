// The Details columns' layout and the records the Explorer keeps per folder. No
// win32 here (the list's columns are browser.cpp's).
//
// A layout holds every column the Explorer knows, in the user's order, each shown
// or hidden and with its width, as XP's defview keeps its column state: a column
// shown again comes back at its own place in the order with the width it had; Choose
// Details' Move Up / Move Down and a drag of the header change the order. Name is
// always shown (the item's text); where it stands is the user's.
//
// Written as the columns in their order, a hidden one with a '-', each with its
// width: "name:180 size:96 type:120 modified:120 -description:180 tags:120
// -comment:150". A column the text does not name (an older file listed only the shown
// ones; a column added later) follows the named ones, hidden, at its default width.
#pragma once
#include <string>
#include <vector>

namespace shell {
namespace columns {

struct Column {
    int id;                 // a shell::SortBy value (Name, Size, Type, Date, Description, Tags, Comment)
    bool shown;
    int width;
    bool operator==(const Column& o) const { return id == o.id && shown == o.shown && width == o.width; }
};
using Layout = std::vector<Column>;

const char* name_of(int id);                                // "size"; null for an unknown id
// XP's Details defaults: Name 180, Size 96, Type 120, Date Modified 120 shown; the stored
// ones hidden (Description 180, Tags 120, Comment 150, as XP's Comments)
Layout builtin();
Layout parse(const std::string& text, const Layout& base);  // base: the widths of the columns not named
std::string format(const Layout& l);
std::vector<int> shown(const Layout& l);                    // the shown ids in their order
bool is_shown(const Layout& l, int id);
int width(const Layout& l, int id);                         // 0: unknown
void set_shown(Layout& l, int id, bool on);                 // at its own place; Name stays shown
void set_width(Layout& l, int id, int px);
// The shown columns in this order (a header drag), the hidden ones keeping their
// places between them: the shown ones' slots are filled again in the new order
void reorder_shown(Layout& l, const std::vector<int>& order);

} // namespace columns

// ---- what the Explorer remembers per folder: $XDG_STATE_HOME/lhde/flxp-views ----------
// One line per folder: "PATH\tVIEW", or "PATH\tVIEW\tcolumns=LAYOUT" when the folder's
// Details columns differ from the default (flxp-columns); VIEW may be empty when only the
// columns are kept. Re-read when another window wrote the file.
namespace folderviews {

struct Record { std::string view, columns; };
Record get(const std::string& folder);
void set_view(const std::string& folder, const std::string& view);      // "" forgets the folder's view
void set_columns(const std::string& folder, const std::string& layout); // "" forgets its columns
void forget_views();                                                     // every folder's view
void forget_columns();                                                   // every folder's columns

} // namespace folderviews
} // namespace shell
