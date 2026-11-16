// What the Explorer says about the folder shown or the selection: one model,
// two presentations. XP's task pane shows it as the "Details" section,
// 4051's band as its preview (System.Windows.Explorer's
// WelcomePane and PreviewManager). No win32 here.
//
// The model is 4051's: per item kind a property list (4051's
// PKEY_PROPLIST_PREVIEWPANE_DETAILS, XP's per-type "Details" value) naming
// properties of the registry; the result is rows of {property, label, value,
// editable, invitation}. XP and 4051 show the same rows, each in its own
// order and format:
//
//   kind      XP's Details                                                  4051's band
//   file      Type; Attributes; Date Modified; Size                         size, date, type
//   picture   Type; Dimensions; Size; Date Modified                         size, date, dimensions
//   song      Size; Type (XP's Artist, Album, Year, Track, Duration:        size, date, type
//             tags are not read)
//   folder    Type; Attributes; Date Modified (a folder has no size)        date, type
//   drive     Type; File System; Free Space; Total Size                     type, free space
//   virtual   Type ("System Folder")                                        type
//   files and folders also: Description (user.lux.desc), Tags (user.xdg.tags),
//   Comment (user.xdg.comment, its first line); XP shows them when set
//   (read-only there, as XP's Details were), 4051 always, an empty one as its
//   invitation ("Add a description", ...), and a click on one edits it
//   (write()). Color (user.lux.color): XP's line when set; the band shows it as
//   a square after the name (Info::rows has its row), not as a line.
//
// XP: the name in bold above the lines; Type is the value alone, the others
// "Label: value"; an empty value drops its line, as does
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
// mappings can be added or renamed here without touching the lists.
// Text is one line (a line break typed or pasted becomes a space) unless `multiline`; Color a name of palette();
// Command a /bin/sh command line run on the file (run_command).
enum class ValueType { Text, List, Date, Size, Number, Color, Command };
struct Property {
    std::string id;                     // "tags"
    std::string label;                  // "Tags" (XP's "Label: value")
    ValueType type = ValueType::Text;
    std::string xattr;                  // "user.xdg.tags"; "" = derived
    bool editable = false;              // the user sets it (4051: a click on its row edits it)
    std::string invitation;             // shown while it is empty ("Add tags"), 4051 only
    bool multiline = false;             // a Text of several lines (the comment): one row shows its first line and "..."
};
const std::vector<Property>& registry();                // the defaults
const Property* property(const std::string& id);        // null: unknown
// The list's column of a stored property (SortBy::Description, Tags, Comment,
// Color; null for the derived columns), and the column of a property id (-1: none)
const Property* column_property(SortBy col);
int column_of(const std::string& id);

// ---- the stored properties on disk (extended attributes) -----------------------
// Values as luxfs's `x` keeps them (docs/ANNOTATIONS.md): a text as given, no
// NUL or newline added (a trailing NUL or newline someone else wrote is dropped
// when read); a list's items separated by commas when stored ("a,b"), trimmed,
// without duplicates (case-sensitive, the first kept), in their order. Writing
// an empty value removes the attribute, as `x -tag` does with the last tag.
using Stored = std::vector<std::pair<std::string, std::string>>;
// One attribute, whatever its length (its size asked first, asked again when it
// grew in between). Missing: not set, or no user attributes on that file system
// (LHDE_ENOATTR, ENOTSUP, ENOENT); Failed: there may be a value that could not be
// read (EACCES, EIO, ...), *error the errno. A change that replaces or merges a
// value must not go ahead on Failed: it would drop what it did not see.
enum class Read { Value, Missing, Failed };
Read read_attr(const std::string& path, const std::string& xattr, std::string* value, int* error = nullptr);
// The current value of p before a change ("" when missing), or false with *err
// a sentence for the message box ("The tags of 'a' could not be read ...")
bool read_current(const std::string& path, const Property& p, std::string* value, std::string* err);
Stored read_stored(const std::string& path);                         // the registry's attributes that are set (Value only)
// The names of every user.* attribute on path (any program's, not only the registry's), in the file system's order.
// Missing: none, no user attributes on that file system (ENOTSUP) or no such file; Failed: *error the errno.
Read list_attrs(const std::string& path, std::vector<std::string>* names, int* error = nullptr);
void read_stored(Item& it);                                          // into it.xattrs (a file-system item)
// The same for the items of a folder: from the catalogue when a root's catalogue holds the folder (docs/CATALOGUE.md),
// for each item whose size, mtime and ctime are those the catalogue observed, its desired values; the others from the file
void read_stored(std::vector<Item>& items, const std::string& folder);
std::vector<std::string> split_list(const std::string& text);        // "a, b;c\n" -> a, b, c (commas, semicolons, newlines)
std::string join_list(const std::vector<std::string>& items);        // -> "a,b,c", as stored
// The value the user typed for p on path: a list is split and joined again; a
// one-line text has its line breaks as spaces, a multi-line one its CR LF as LF;
// a colour of the palette its lower-case name; every value trimmed. False with *err a sentence for a message box (the file system keeps no user
// attributes, the file is not the user's or read-only, its current value could
// not be read, ...). add_tags and remove_tag refuse the same way.
// A write that succeeded is recorded in the catalogue holding the file, if one does (lhcat_record_write: an operation in
// its journal, the file observed again); so are change_tags' writes.
bool write(const std::string& path, const Property& p, const std::string& text, std::string* err);
bool add_tags(const std::string& path, const std::vector<std::string>& tags, std::string* err);   // keeping the others
bool remove_tag(const std::string& path, const std::string& tag, std::string* err);
// One read and one write: `add` appended (each split as typed: "a, b" is two), then `remove` taken out; the file's
// other tags kept in their order. Nothing is written when nothing changes.
bool change_tags(const std::string& path, const std::vector<std::string>& add, const std::vector<std::string>& remove,
                 std::string* err);
// A route: a program that keeps some files' annotations itself (luxfind: the files of a root that cannot be reached, or
// whose catalogue is the authority, docs/CATALOGUE.md) sets one, and for a path it takes, read_attr, list_attrs, write,
// change_tags (as tag operations), read_stored and known_tags ask it instead of the file; nothing else changes. The
// values given to set are normalized as write stores them ("" removes). UI thread; none by default.
struct Route {
    virtual ~Route() = default;
    virtual bool takes(const std::string& path) = 0;    // (a directory's path with a '/' after it: its files)
    virtual bool read(const std::string& path, const std::string& xattr, std::string* value) = 0;   // false: unknown
    virtual std::vector<std::string> names(const std::string& path) = 0;   // the user.* names set
    virtual bool set(const std::string& path, const std::string& xattr, const std::string& value, std::string* err) = 0;
    virtual bool tags(const std::string& path, const std::vector<std::string>& add, const std::vector<std::string>& remove,
                      std::string* err) = 0;
};
void set_route(Route* r);
Route* route();
// The tags to offer where a tag is typed (Add Tag..., the Properties sheet): those of `paths`, then the recently used
// ones (remember_tags), then those of the other files in the paths' folders (at most 2000 entries a folder); each once.
std::vector<std::string> known_tags(const std::vector<std::string>& paths);
// Tags the user just added, kept at the front of $XDG_STATE_HOME/lhde/flxp-tags (one a line, the last 100)
void remember_tags(const std::vector<std::string>& tags);

// A value on one row (the Details column, the panes, the infotip): a list as "a, b", a colour by its label ("Red"; an
// unknown name as written), a multi-line text its first line and "..." (U+2026) when more follows.
std::string shown(const Property& p, const std::string& value);
std::string first_line(const std::string& text);

// ---- colours (user.lux.color) ------------------------------------------------------------
// A small named palette, stored as the lower-case name. VGA's sixteen where they have the colour (red, yellow, green, teal,
// blue, purple, gray) and orange, pink and brown, which VGA lacks; chosen to read on white and on the task band's blue.
struct Color { const char* name; const char* label; unsigned rgb; };   // rgb 0xRRGGBB
const std::vector<Color>& palette();
const Color* color(const std::string& value);           // by name, ASCII case ignored; null: none or unknown

// ---- run as (user.lux.run) -----------------------------------------------------------------
// What a file with `run` set opens with: a /bin/sh command line in which %f is the file, quoted ("python3 %f" ->
// python3 '/a/b c.py'), %% a percent sign; without %f the quoted file is appended.
std::string run_command(const std::string& command, const std::string& path);
std::string run_program(const std::string& command);    // its program's name ("/usr/bin/python3 -u %f" -> "python3")

// XP's infotip of a list item (defview's InfoTip lines, "Label: value" per
// line): a file's Type, Date Modified and Size, a folder's Date Modified, then
// Description, Tags, Comment (its first line), Color and "Runs with: PROGRAM"
// when set (from it.xattrs, read when empty)
std::string infotip(const Item& it);

// ---- the Filter box ------------------------------------------------------------------
// What the Longhorn hosts' "Filter by" box (and the header's search box) keep of
// the folder: the items every word of the text matches, each word anywhere in the
// name, the description, a tag or the comment, as a part of the value, ASCII case
// ignored ("red" finds "Red sunset.png" and the tag "reddish"). A word written
// field:text looks at that field only: tag: (also tags:), desc: (description:),
// comment:, name:; a field with nothing after it matches the items where that
// field is set ("tag:" keeps the tagged ones). Double quotes make one word of
// several ("a red", tag:"to do"). A word with another prefix ("http:") is a
// plain word. Words are separated by spaces; an empty text is no filter.
struct Filter {
    enum Field { Any, Name, Desc, Tags, Comment };
    struct Word { Field field = Any; std::string text; };   // text in lower case
    std::vector<Word> words;
    bool empty() const { return words.empty(); }
    bool stored() const;                // a word looks at the stored properties (they must be read)
};
Filter parse_filter(const std::string& text);
bool filter_matches(const Filter& f, const Item& it);   // the stored properties from it.xattrs (read_stored)

// What the file system says about an item beyond shell::Item (probe() reads it)
struct Facts {
    enum Kind { Other, Picture, Audio, Drive, System } kind = Other;
    int width = 0, height = 0;          // a picture's dimensions (0: unknown)
    bool thumbnail = false;             // a thumbnail can be made of it (shell::thumbs)
    std::string fs;                     // a drive's file system ("ext4")
    uint64_t free = 0, total = 0;       // a drive's free and total bytes (total 0: unknown)
    bool network = false;               // on a network mount
    Stored xattrs;                      // the registry's attributes that are set: name, value
};

struct Row {
    std::string id;                     // the property
    std::string label;                  // XP's label; "" = the value alone (the type)
    std::string xp;                     // XP's value ("" = not shown there)
    std::string lh;                     // 4051's value ("" = the invitation, or not shown)
    std::string raw;                    // a stored property's value for its editor (a list as shown: "a, b")
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
    std::string path;                   // the location's or the item's path ("" for Many; an editor's target)
    std::vector<Row> rows;
    uint64_t total = 0;                 // Many: the files' bytes (0 for more than 100 items, as XP)
    bool xp_preview = false;            // XP's mini preview (a single non-folder that has a thumbnail)
    bool folder = false;                // One: the item is a folder (4051's band shows its picture as the Icons view composes it)
    std::vector<std::string> previews;  // 4051's pictures (One: the item; Many: up to 7)
};

// The two presentations
struct Line {
    std::string label, value;
    std::string text() const { return label.empty() ? value : label + ": " + value; }
};
std::string xp_title(const Info& in);               // the bold line: the name ("" for Many)
std::vector<Line> xp_lines(const Info& in);         // the lines under it, in XP's order
struct LhLine {
    std::string text;
    bool invitation = false, editable = false;
    std::string id;                     // the property (the band's editor writes it)
    std::string raw;                    // its value as typed in the editor ("" for an invitation)
};
std::string lh_title(const Info& in);               // the name, "N items selected" ("" for Location)
std::vector<LhLine> lh_lines(const Info& in);       // the values in 4051's order

// Formats
std::string byte_size(uint64_t bytes);                  // StrFormatByteSize: "198 bytes", "1.30 KB", "2.39 MB"
std::string friendly_date(int64_t t, int64_t now);      // "Today, October 31, 2026, 12:45 PM"
std::string short_date(int64_t t);                      // "8/24/2001"
// the Properties sheet's General page (shell::properties), XP's shell32 General page
std::string thousands(uint64_t n);                      // "1,331"
std::string size_and_bytes(uint64_t bytes);             // "1.29 KB (1,331 bytes)", "198 bytes (198 bytes)"
std::string long_date(int64_t t);                       // "Thursday, October 02, 2003, 5:36:12 PM"
std::string attributes(const Item& it);                 // "Read-only, Hidden"; "" for none
std::string tag_list(const std::string& value);         // a list value as shown: "a,b; c" -> "a, b, c"
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
