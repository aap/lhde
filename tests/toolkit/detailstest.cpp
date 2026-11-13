// The Explorer's details model (shell/details.h): the property registry, the
// item kinds' lists, XP's Details lines for the folder, one item and several,
// its size and date formats, and 4051's band values, invitations, counts and
// picture stack; the stored properties: lists, columns, sorting, the infotip and
// the write path (setxattr / removexattr on a scratch file).
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <time.h>
#include <string>
#include "shell/details.h"

using namespace shell;
using namespace shell::details;

static int fails, checks;
static void check(const char* what, const std::string& want, const std::string& got)
{
    checks++;
    if (want == got) return;
    fails++;
    printf("FAIL %s: expected [%s] got [%s]\n", what, want.c_str(), got.c_str());
}
static void check(const char* what, int want, int got) { check(what, std::to_string(want), std::to_string(got)); }

static std::string xp(const Info& in)
{
    std::string s;
    for (const Line& l : xp_lines(in)) s += (s.empty() ? "" : "|") + l.text();
    return s;
}
static std::string lh(const Info& in)
{
    std::string s;
    for (const LhLine& l : lh_lines(in)) s += (s.empty() ? "" : "|") + (l.invitation ? "<" + l.text + ">" : l.text);
    return s;
}
static std::string join(const std::vector<std::string>& v)
{
    std::string s;
    for (const std::string& l : v) s += (s.empty() ? "" : "|") + l;
    return s;
}

static int64_t at(int y, int mo, int d, int h, int mi)
{
    struct tm tm = {};
    tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d; tm.tm_hour = h; tm.tm_min = mi; tm.tm_isdst = -1;
    return (int64_t)mktime(&tm);
}

static Item file(const std::string& name, uint64_t size, int64_t mtime, const std::string& type, uint32_t flags = 0)
{
    Item it; it.name = name; it.path = "/x/" + name; it.size = size; it.mtime = mtime; it.type = type; it.flags = flags;
    it.icon = "document";
    return it;
}

int main(int, char** argv)
{
    setenv("TZ", "UTC", 1);
    tzset();

    // the registry: stored properties name their attribute, derived ones none
    check("tags xattr", "user.xdg.tags", property("tags")->xattr);
    check("desc xattr", "user.lux.desc", property("description")->xattr);
    check("comment xattr", "user.xdg.comment", property("comment")->xattr);
    check("tags list", 1, property("tags")->type == ValueType::List);
    check("tags editable", 1, property("tags")->editable);
    check("size derived", "", property("size")->xattr);
    check("size not editable", 0, property("size")->editable);
    check("modified label", "Date Modified", property("modified")->label);
    check("unknown", 1, property("rating") == nullptr);
    check("tag list", "a, b, c", tag_list("a,b, c"));
    check("tag list blanks", "x", tag_list(" x ,,"));
    // the metadata's other properties: colour and run as; the description one line, the comment several
    check("color xattr", "user.lux.color", property("color")->xattr);
    check("color type", 1, property("color")->type == ValueType::Color);
    check("run xattr", "user.lux.run", property("run")->xattr);
    check("run type", 1, property("run")->type == ValueType::Command);
    check("run label", "Run as", property("run")->label);
    check("desc one line", 0, property("description")->multiline);
    check("comment multi-line", 1, property("comment")->multiline);
    check("column color", "user.lux.color", column_property(SortBy::Color)->xattr);
    check("column of color", (int)SortBy::Color, column_of("color"));
    check("column of run: none", -1, column_of("run"));
    // the palette: lower-case names, found whatever the case; an unknown value shows as written
    check("palette size", 10, (int)palette().size());
    check("palette first", "red", palette()[0].name);
    check("color by name", "Red", color("RED") ? color("RED")->label : "<none>");
    check("color rgb", (int)0x008080, color("teal") ? (int)color("teal")->rgb : -1);
    check("color unknown", 1, color("chartreuse") == nullptr);
    check("shown color", "Purple", shown(*property("color"), "purple"));
    check("shown unknown color", "chartreuse", shown(*property("color"), "chartreuse"));
    // a multi-line value's row: its first line and an ellipsis when more follows
    check("first line", "first\xe2\x80\xa6", first_line("first\nsecond"));
    check("first line alone", "only", first_line("only"));
    check("first line, blank after", "only", first_line("only\n\n"));
    check("first line CR LF", "a\xe2\x80\xa6", first_line("a\r\nb"));
    check("shown comment", "one\xe2\x80\xa6", shown(*property("comment"), "one\ntwo"));
    check("shown desc", "as is", shown(*property("description"), "as is"));
    // run as: %f the quoted file, %% a percent, appended without %f; the program's name
    check("run %f", "python3 '/a/b c.py'", run_command("python3 %f", "/a/b c.py"));
    check("run appended", "wine '/x/s.exe'", run_command("wine", "/x/s.exe"));
    check("run twice and %%", "cp '/f' '/f'.bak 100%", run_command("cp %f %f.bak 100%%", "/f"));
    check("run quote in name", "sh 'it'\\''s'", run_command("sh %f", "it's"));
    check("run program", "python3", run_program("/usr/bin/python3 -u %f"));
    check("run program quoted", "my tool", run_program("\"/opt/my tool\""));
    check("run program plain", "xdg-open", run_program("  xdg-open %f"));

    // StrFormatByteSize: three digits, the fraction cut
    check("bytes", "198 bytes", byte_size(198));
    check("bytes 1023", "1023 bytes", byte_size(1023));
    check("1 KB", "1.00 KB", byte_size(1024));
    check("1340", "1.30 KB", byte_size(1340));
    check("23506", "22.9 KB", byte_size(23506));
    check("hundreds", "999 KB", byte_size(999 * 1024 + 1000));
    check("1000 KB is 0.97 MB", "0.97 MB", byte_size(1000 * 1024));
    check("2.4 MB, the shift cuts", "2.39 MB", byte_size(2516582));
    check("GB", "9.99 GB", byte_size(10737418240ULL - 1024 * 1024 * 10));

    // SHFormatDateTime(FDTF_LONGDATE | FDTF_RELATIVE | short time)
    int64_t now = at(2026, 10, 31, 15, 0);
    check("today", "Today, October 31, 2026, 12:45 PM", friendly_date(at(2026, 10, 31, 12, 45), now));
    check("yesterday", "Yesterday, October 30, 2026, 9:05 AM", friendly_date(at(2026, 10, 30, 9, 5), now));
    check("long date", "Friday, August 24, 2001, 12:00 AM", friendly_date(at(2001, 8, 24, 0, 0), now));
    check("day with zero", "Thursday, October 02, 2003, 5:36 PM", friendly_date(at(2003, 10, 2, 17, 36), now));
    check("short date", "8/24/2001", short_date(at(2001, 8, 24, 10, 0)));
    // the Properties sheet's General page: StrFormatByteSize and the exact count, the long date with seconds
    check("thousands", "1,331", thousands(1331));
    check("thousands small", "999", thousands(999));
    check("thousands big", "10,737,418,240", thousands(10737418240ULL));
    check("size and bytes", "1.29 KB (1,331 bytes)", size_and_bytes(1331));
    check("size and bytes small", "198 bytes (198 bytes)", size_and_bytes(198));
    check("properties date", "Thursday, October 02, 2003, 5:36:09 PM", long_date(at(2003, 10, 2, 17, 36) + 9));
    check("properties midnight", "Friday, August 24, 2001, 12:00:00 AM", long_date(at(2001, 8, 24, 0, 0)));

    // attributes: read-only only for files, hidden for both, never archive
    check("attr none", "", attributes(file("a", 1, 0, "")));
    check("attr ro hidden", "Read-only, Hidden", attributes(file(".a", 1, 0, "", IsReadOnly | IsHidden)));
    check("attr folder ro", "Hidden", attributes(file(".d", 0, 0, "", IsFolder | IsReadOnly | IsHidden)));

    int64_t old = at(2026, 10, 23, 14, 24);
    // nothing selected: the folder itself (XP); the count alone (4051)
    Item loc; loc.name = "WINDOWS"; loc.path = "/x"; loc.type = "File Folder"; loc.flags = IsFolder; loc.mtime = old; loc.icon = "folder";
    Info in = describe({}, {}, loc, Facts(), 107, now);
    check("loc kind", 0, (int)in.kind);
    check("loc count", 107, in.count);
    check("loc xp title", "WINDOWS", xp_title(in));
    check("loc xp lines", "File Folder|Date Modified: Friday, October 23, 2026, 2:24 PM", xp(in));
    check("loc lh title", "", lh_title(in));
    check("loc lh lines", "", lh(in));
    check("loc no preview", 0, in.xp_preview);
    check("lh items", " items", lh_items_word(107));
    check("lh item", " item", lh_items_word(1));
    // a folder with a description: XP shows the set ones
    Facts tagged; tagged.xattrs = { { "user.lux.desc", "Reference screenshots" }, { "user.xdg.tags", "reference,4051" } };
    in = describe({}, {}, loc, tagged, 3, now);
    check("loc xattrs", "File Folder|Date Modified: Friday, October 23, 2026, 2:24 PM|Description: Reference screenshots|Tags: reference, 4051", xp(in));

    // My Computer
    Item comp; comp.name = "My Computer"; comp.path = "::computer"; comp.flags = IsFolder | IsVirtual; comp.type = "System Folder";
    Facts sys; sys.kind = Facts::System;
    in = describe({}, {}, comp, sys, 3, now);
    check("computer", "System Folder", xp(in));

    // a drive: XP's drive list
    Item drv; drv.name = "Local Disk (/)"; drv.path = "/"; drv.flags = IsFolder | IsDrive; drv.type = "File Folder";
    Facts df; df.kind = Facts::Drive; df.fs = "ext4"; df.free = 2516582; df.total = 10737418240ULL;
    in = describe({}, {}, drv, df, 20, now);
    check("drive xp", "Local Disk|File System: ext4|Free Space: 2.39 MB|Total Size: 10.0 GB", xp(in));
    in = describe({ drv }, { df }, comp, sys, 3, now);
    check("drive lh", "Local Disk|2.39 MB free of 10.0 GB", lh(in));

    // one file: the default list (Type, Attributes, Date Modified, Size);
    // 4051: size, date, type, then the stored ones as invitations
    Item bak = file("Boot.BAK", 198, old, "BAK File");
    in = describe({ bak }, { Facts() }, loc, Facts(), 107, now);
    check("one kind", 1, (int)in.kind);
    check("one xp title", "Boot.BAK", xp_title(in));
    check("one xp", "BAK File|Date Modified: Friday, October 23, 2026, 2:24 PM|Size: 198 bytes", xp(in));
    check("one lh title", "Boot.BAK", lh_title(in));
    check("one lh", "1 KB|10/23/2026 2:24 PM|BAK File|<Add a description>|<Add tags>|<Add comments>", lh(in));
    check("one no preview", 0, in.xp_preview);
    check("one no pictures", 0, (int)in.previews.size());
    check("one path", "/x/Boot.BAK", in.path);
    // with its tags set: a value in both
    Facts bf; bf.xattrs = { { "user.xdg.tags", "boot, backup" } };
    in = describe({ bak }, { bf }, loc, Facts(), 107, now);
    check("one tags xp", "BAK File|Date Modified: Friday, October 23, 2026, 2:24 PM|Size: 198 bytes|Tags: boot, backup", xp(in));
    check("one tags lh", "1 KB|10/23/2026 2:24 PM|BAK File|<Add a description>|boot, backup|<Add comments>", lh(in));
    // a comment of two lines shows its first; a colour is XP's line (the band draws it by the name); run as in neither pane;
    // the editor gets the whole stored value
    Facts cf; cf.xattrs = { { "user.xdg.comment", "line one\nline two" }, { "user.lux.color", "red" }, { "user.lux.run", "python3 %f" } };
    in = describe({ bak }, { cf }, loc, Facts(), 107, now);
    check("comment and color xp", "BAK File|Date Modified: Friday, October 23, 2026, 2:24 PM|Size: 198 bytes|Comment: line one\xe2\x80\xa6|Color: Red", xp(in));
    check("comment and color lh", "1 KB|10/23/2026 2:24 PM|BAK File|<Add a description>|<Add tags>|line one\xe2\x80\xa6", lh(in));
    {
        std::string raws;
        for (const Row& r : in.rows) if (r.id == "comment" || r.id == "color") raws += (raws.empty() ? "" : "|") + r.raw;
        check("editor raw values", "line one\nline two|red", raws);
    }

    // an empty file has no Size line; a read-only one says so
    in = describe({ file("empty", 0, old, "File", IsReadOnly) }, {}, loc, Facts(), 1, now);
    check("empty ro", "File|Attributes: Read-only|Date Modified: Friday, October 23, 2026, 2:24 PM", xp(in));

    // one picture: Type, Dimensions, Size, Date Modified; the mini preview
    Item sun = file("Sunset.jpg", 73 * 1024 + 300, at(2001, 8, 24, 12, 0), "JPEG Image");
    Facts pf; pf.kind = Facts::Picture; pf.width = 800; pf.height = 600; pf.thumbnail = true;
    in = describe({ sun }, { pf }, loc, Facts(), 4, now);
    check("pic xp", "JPEG Image|Dimensions: 800 x 600|Size: 73.2 KB|Date Modified: Friday, August 24, 2001, 12:00 PM", xp(in));
    check("pic preview", 1, in.xp_preview);
    check("pic lh", "74 KB|8/24/2001|800 x 600|<Add a description>|<Add tags>|<Add comments>", lh(in));
    check("pic previews", "/x/Sunset.jpg", join(in.previews));
    // on a network mount: XP's slow item, no mini preview
    Facts nf = pf; nf.network = true;
    check("pic network", 0, describe({ sun }, { nf }, loc, Facts(), 4, now).xp_preview);

    // a song: Size before Type (selfreg.inx's audio list)
    Facts af; af.kind = Facts::Audio;
    in = describe({ file("a.mp3", 4000000, old, "MP3 Audio") }, { af }, loc, Facts(), 1, now);
    check("audio xp", "Size: 3.81 MB|MP3 Audio", xp(in));

    // one folder: no size
    Item sub = file("Fonts", 4096, old, "File Folder", IsFolder);
    in = describe({ sub }, { Facts() }, loc, Facts(), 107, now);
    check("folder xp", "File Folder|Date Modified: Friday, October 23, 2026, 2:24 PM", xp(in));
    check("folder lh", "10/23/2026 2:24 PM|File Folder|<Add a description>|<Add tags>|<Add comments>", lh(in));

    // several: the count, a blank line and the files' total
    Item a = file("a", 1000, old, "File"), b = file("b", 2000, old, "File");
    in = describe({ a, b, sub }, {}, loc, Facts(), 107, now);
    check("many kind", 2, (int)in.kind);
    check("many xp", "3 items selected.||Total File Size: 2.92 KB", xp(in));
    check("many xp title", "", xp_title(in));
    check("many lh title", "3 items selected", lh_title(in));
    check("many lh", "(multiple values)|10/23/2026", lh(in));
    // folders only: no total
    check("many folders", "2 items selected.", xp(describe({ sub, sub }, {}, loc, Facts(), 9, now)));
    // more than 100: no total (XP does not add them up)
    std::vector<Item> many(101, a);
    check("many 101", "101 items selected.", xp(describe(many, {}, loc, Facts(), 101, now)));

    // the picture stack: up to 7, the focused one first, pictures only
    std::vector<Item> pics;
    std::vector<Facts> pfs;
    for (int i = 0; i < 9; i++) { pics.push_back(file("p" + std::to_string(i) + ".jpg", 5000, at(2001, 8, 24, 1, 0), "JPEG Image")); pfs.push_back(pf); }
    pics.push_back(file("notes.txt", 10, old, "Text Document"));
    pfs.push_back(Facts());
    in = describe(pics, pfs, loc, Facts(), 10, now, 3);
    check("stack", "/x/p3.jpg|/x/p4.jpg|/x/p5.jpg|/x/p6.jpg|/x/p7.jpg|/x/p8.jpg|/x/p0.jpg", join(in.previews));
    check("stack lh", "(multiple values)|(multiple values)", lh(in));
    std::vector<Item> four(pics.begin(), pics.begin() + 4);
    in = describe(four, pfs, loc, Facts(), 4, now);
    check("four same", "5 KB|8/24/2001", lh(in));
    check("four title", "4 items selected", lh_title(in));
    check("four icon", "document", in.icon);

    // 4051's editable rows: one item's stored ones carry their id and raw value
    in = describe({ bak }, { bf }, loc, Facts(), 107, now);
    std::string ed;
    for (const LhLine& l : lh_lines(in)) if (l.editable) ed += (ed.empty() ? "" : "|") + l.id + "=" + l.raw;
    check("editable rows", "description=|tags=boot, backup|comment=", ed);
    ed.clear();
    for (const LhLine& l : lh_lines(describe({ a, b }, {}, loc, Facts(), 107, now))) if (l.editable) ed += l.id;
    check("several: none editable", "", ed);
    check("drive not editable", 0, (int)describe({ drv }, { df }, comp, sys, 3, now).rows[0].editable);

    // the lists: commas, semicolons and newlines separate; trimmed; no duplicates; stored "a,b"
    check("split", "a|b c|d", join(split_list(" a ;b c,, d\n")));
    check("split dup", "x|X", join(split_list("x, X, x")));
    check("join", "a,b c", join_list({ "a", "b c" }));
    check("tag list semicolons", "a, b", tag_list("a;b"));

    // the columns of the stored properties
    check("column desc", "user.lux.desc", column_property(SortBy::Description)->xattr);
    check("column comment", "user.xdg.comment", column_property(SortBy::Comment)->xattr);
    check("column derived", 1, column_property(SortBy::Size) == nullptr);
    check("column of tags", (int)SortBy::Tags, column_of("tags"));
    check("column of size", (int)SortBy::Size, column_of("size"));
    check("column of unknown", -1, column_of("rating"));

    // sorting by a stored column: empty first, then by value, folders first always
    {
        Item p1 = file("p1", 1, old, "File"), p2 = file("p2", 1, old, "File"), p3 = file("p3", 1, old, "File");
        p1.xattrs = { { "user.xdg.tags", "zeta" } }; p3.xattrs = { { "user.xdg.tags", "alpha" } };
        std::vector<Item> v = { p1, p2, p3, sub };
        sort_items(v, SortBy::Tags, true);
        check("sort tags", "Fonts|p2|p3|p1", v[0].name + "|" + v[1].name + "|" + v[2].name + "|" + v[3].name);
        sort_items(v, SortBy::Tags, false);
        check("sort tags down", "Fonts|p1|p3|p2", v[0].name + "|" + v[1].name + "|" + v[2].name + "|" + v[3].name);
    }

    // the infotip: XP's Type, Date Modified, Size, then the stored ones that are set
    Item tip = bak;
    tip.xattrs = { { "user.xdg.tags", "boot,backup" }, { "user.lux.desc", "The old boot file" } };
    check("infotip", "Type: BAK File\nDate Modified: 10/23/2026 2:24 PM\nSize: 198 bytes\nDescription: The old boot file\nTags: boot, backup", infotip(tip));
    Item ftip = sub; ftip.xattrs = { { "user.xdg.comment", "fonts" } };
    check("infotip folder", "Date Modified: 10/23/2026 2:24 PM\nComment: fonts", infotip(ftip));
    Item rtip = bak;
    rtip.xattrs = { { "user.lux.run", "/usr/bin/python3 %f" }, { "user.lux.color", "teal" }, { "user.xdg.comment", "a\nb" } };
    check("infotip run and color", "Type: BAK File\nDate Modified: 10/23/2026 2:24 PM\nSize: 198 bytes\nComment: a\xe2\x80\xa6\nColor: Teal\nRuns with: python3", infotip(rtip));

    // the Filter box: words, fields, quotes; every word matches somewhere, case ignored
    {
        auto words = [](const std::string& text) {
            std::string s;
            for (const Filter::Word& w : parse_filter(text).words) s += (s.empty() ? "" : "|") + std::to_string((int)w.field) + w.text;
            return s;
        };
        check("filter words", "0red|0sun", words("  Red   sun "));
        check("filter fields", "3red|1x|2to do|4", words("TAG:red name:x desc:\"to do\" comment:"));
        check("filter other prefix", "0http://a", words("http://a"));
        check("filter quotes", "0a red", words("\"a red\" \"\""));
        check("filter empty", 1, parse_filter("   ").empty());
        check("filter name only reads nothing", 0, parse_filter("name:a").stored());
        check("filter word reads the attributes", 1, parse_filter("a").stored());
        Item a = file("alpha.txt", 1, 0, "Text"), g = file("gamma.png", 1, 0, "PNG");
        a.xattrs = { { "user.xdg.tags", "red,work" } };
        g.xattrs = { { "user.lux.desc", "A Red sunset" }, { "user.xdg.comment", "tagged later" } };
        auto hits = [&](const std::string& text) {
            Filter f = parse_filter(text);
            return std::string(filter_matches(f, a) ? "a" : "") + (filter_matches(f, g) ? "g" : "");
        };
        check("filter any field", "ag", hits("red"));
        check("filter every word", "a", hits("red work"));
        check("filter tag: tags only", "a", hits("tag:red"));
        check("filter tag: within one tag", "", hits("tag:red,work"));
        check("filter tag: set", "a", hits("tag:"));
        check("filter desc:", "g", hits("desc:SUN"));
        check("filter comment: not the tags", "g", hits("comment:tag"));
        check("filter name:", "a", hits("name:alp"));
        check("filter none", "ag", hits(""));
        check("filter no match", "", hits("blue"));
    }

    // the write path on a scratch file (skipped where the file system keeps no user attributes)
    {
        // $TMPDIR or /tmp (tmpfs keeps user attributes on current kernels), else beside this program (the build tree)
        const char* tmp = getenv("TMPDIR");
        char* self = realpath(argv[0], nullptr);   // absolute: read_stored takes file-system paths only
        std::string here = self ? self : "/tmp";
        free(self);
        here = here.substr(0, here.find_last_of('/'));
        std::string dir, f, err;
        const Property* tags = property("tags");
        const Property* desc = property("description");
        bool xattrs = false;
        for (const std::string& base : { std::string(tmp && *tmp ? tmp : "/tmp"), here }) {
            if (!dir.empty()) { unlink(f.c_str()); rmdir(dir.c_str()); }
            dir = base + "/detailstest.XXXXXX";
            if (!mkdtemp(&dir[0])) { printf("note: cannot make %s: %s\n", dir.c_str(), strerror(errno)); dir.clear(); continue; }
            f = dir + "/scratch.txt";
            FILE* fp = fopen(f.c_str(), "w"); if (fp) { fputs("x\n", fp); fclose(fp); }
            if (write(f, *desc, "probe", &err) || err.find("extended attributes") == std::string::npos) { xattrs = true; break; }
            printf("note: no user xattrs in %s\n", base.c_str());
        }
        if (!xattrs) {
            printf("note: no user xattrs in a scratch directory: the write checks are skipped\n");
        } else {
            auto value = [&](const std::string& x) { for (const auto& kv : read_stored(f)) if (kv.first == x) return kv.second; return std::string("<none>"); };
            check("write desc", 1, write(f, *desc, "  A scratch file \n", &err));
            check("desc trimmed", "A scratch file", value("user.lux.desc"));
            check("write tags", 1, write(f, *tags, "one; two , one,,three", &err));
            check("tags stored", "one,two,three", value("user.xdg.tags"));
            check("add tags", 1, add_tags(f, { "two", "four, five" }, &err));
            check("tags added", "one,two,three,four,five", value("user.xdg.tags"));
            check("remove tag", 1, remove_tag(f, "two", &err));
            check("remove missing tag", 1, remove_tag(f, "zzz", &err));
            check("tag removed", "one,three,four,five", value("user.xdg.tags"));
            Item it; it.path = f; it.name = "scratch.txt";
            read_stored(it);
            check("item xattrs", "one,three,four,five", it.stored("user.xdg.tags"));
            check("facts", 2, (int)probe(it).xattrs.size());
            for (const char* t : { "one", "three", "four", "five" }) remove_tag(f, t, &err);
            check("last tag removes it", "<none>", value("user.xdg.tags"));
            check("empty removes", 1, write(f, *desc, "", &err));
            check("desc removed", "<none>", value("user.lux.desc"));
            check("remove twice", 1, write(f, *desc, " ", &err));
            check("derived refused", 0, write(f, *property("size"), "1", &err));
            // the description is one line: a pasted line break becomes a space; the comment keeps its lines (CR LF as LF)
            check("write desc of lines", 1, write(f, *desc, "first\r\nsecond\n\nthird\n", &err));
            check("desc one line", "first second third", value("user.lux.desc"));
            write(f, *desc, "", &err);
            check("write comment", 1, write(f, *property("comment"), "  one\r\ntwo\r\n\r\nthree\r\n", &err));
            check("comment lines", "one\ntwo\n\nthree", value("user.xdg.comment"));
            // a colour of the palette is stored by its lower-case name, another as typed
            check("write color", 1, write(f, *property("color"), " Red ", &err));
            check("color stored", "red", value("user.lux.color"));
            check("write odd color", 1, write(f, *property("color"), "Chartreuse", &err));
            check("odd color stored", "Chartreuse", value("user.lux.color"));
            write(f, *property("color"), "", &err);
            // tags added and removed in one go: the others kept in their order, an added one not twice
            write(f, *tags, "a,b,c", &err);
            check("change tags", 1, change_tags(f, { "d", "b", "e; f" }, { "a", "zzz" }, &err));
            check("tags changed", "b,c,d,e,f", value("user.xdg.tags"));
            check("change nothing", 1, change_tags(f, {}, {}, &err));
            check("remove all", 1, change_tags(f, {}, { "b", "c", "d", "e", "f" }, &err));
            check("all removed", "<none>", value("user.xdg.tags"));
            // run as: the attribute is what a double click runs (shell::open_files reads it with run_as)
            check("write run", 1, write(f, *property("run"), "python3 %f\n", &err));
            check("run stored", "python3 %f", value("user.lux.run"));
            write(f, *property("run"), "", &err);
            // the known tags: the files' own, the recently used, the folder's other files'
            {
                std::string other = dir + "/other.txt", state = dir + "/state";
                FILE* op = fopen(other.c_str(), "w"); if (op) fclose(op);
                setxattr(other.c_str(), "user.xdg.tags", "folder1,shared", 14, 0);
                write(f, *tags, "own,shared", &err);
                const char* old_state = getenv("XDG_STATE_HOME");
                std::string keep = old_state ? old_state : "";
                setenv("XDG_STATE_HOME", state.c_str(), 1);
                check("known: own and folder", "own|shared|folder1", join(known_tags({ f })));
                remember_tags({ "recent1, recent2" });
                remember_tags({ "recent3", "recent1" });
                check("known: recent after own", "own|shared|recent3|recent1|recent2|folder1", join(known_tags({ f })));
                if (old_state) setenv("XDG_STATE_HOME", keep.c_str(), 1); else unsetenv("XDG_STATE_HOME");
                unlink((state + "/lhde/flxp-tags").c_str());
                rmdir((state + "/lhde").c_str());
                rmdir(state.c_str());
                unlink(other.c_str());
                write(f, *tags, "", &err);
            }
            // a NUL and a newline someone else wrote are not part of the value
            setxattr(f.c_str(), "user.xdg.comment", "echoed\n", 7, 0);
            check("newline dropped", "echoed", value("user.xdg.comment"));
            check("missing file", 0, write(dir + "/gone", *desc, "x", &err));
            check("missing file says so", 1, err.find("not there") != std::string::npos);
            // a value longer than any fixed buffer (the reviewer's 5000-byte tag list): read whole, and Add Tag keeps every old tag
            std::string many;
            for (int i = 0; i < 625; i++) many += (i ? "," : "") + std::string("tag") + std::to_string(1000 + i);   // 4999 bytes
            if (setxattr(f.c_str(), "user.xdg.tags", many.data(), many.size(), 0) != 0) {
                printf("note: %s keeps no %zu-byte attribute (%s; ext4 keeps one block): the long-value checks are skipped\n",
                       dir.c_str(), many.size(), strerror(errno));
            } else {
                check("long tags read", many, value("user.xdg.tags"));
                check("add to long tags", 1, add_tags(f, { "new" }, &err));
                check("long tags kept", many + ",new", value("user.xdg.tags"));
                check("remove from long tags", 1, remove_tag(f, "tag1000", &err));
                check("long tags less one", many.substr(8) + ",new", value("user.xdg.tags"));
            }
            // a value that cannot be read (a write-only file: user attributes need read permission to read, write to set)
            // is not "no value": the changes refuse and say so instead of replacing what they did not see
            std::string before = value("user.xdg.tags"), got;
            chmod(f.c_str(), 0200);
            int e = 0;
            if (getuid() == 0 || read_attr(f, "user.xdg.tags", &got, &e) != Read::Failed) {
                printf("note: a write-only file's attributes can be read here (root?): the unreadable checks are skipped\n");
            } else {
                check("unreadable errno", EACCES, e);
                err.clear();
                check("add refused", 0, add_tags(f, { "lost" }, &err));
                check("add says so", 1, err.find("could not be read") != std::string::npos);
                check("remove refused", 0, remove_tag(f, "tag1001", &err));
                check("write refused", 0, write(f, *tags, "only", &err));
                check("desc write refused", 0, write(f, *desc, "only", &err));
            }
            chmod(f.c_str(), 0644);
            check("unreadable kept", before, value("user.xdg.tags"));
            check("missing is not failed", (int)Read::Missing, (int)read_attr(f, "user.lux.desc", &got, &e));
        }
        unlink(f.c_str());
        rmdir(dir.c_str());
    }

    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
