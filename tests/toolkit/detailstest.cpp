// The Explorer's details model (shell/details.h): the property registry, the
// item kinds' lists, XP's Details lines for the folder, one item and several,
// its size and date formats, and 4051's band values, invitations, counts and
// picture stack; the stored properties: lists, columns, sorting, the infotip and
// the write path (setxattr / removexattr on a scratch file).
#include <stdio.h>
#include <stdlib.h>
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

int main()
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

    // the write path on a scratch file (skipped where the file system keeps no user attributes)
    {
        const char* tmp = getenv("TMPDIR");
        std::string dir = std::string(tmp && *tmp ? tmp : "/tmp") + "/detailstest.XXXXXX";
        if (!mkdtemp(&dir[0])) { perror("mkdtemp"); return 2; }
        std::string f = dir + "/scratch.txt";
        FILE* fp = fopen(f.c_str(), "w"); if (fp) { fputs("x\n", fp); fclose(fp); }
        std::string err;
        const Property* tags = property("tags");
        const Property* desc = property("description");
        if (!write(f, *desc, "probe", &err) && err.find("extended attributes") != std::string::npos) {
            printf("note: no user xattrs in %s: the write checks are skipped\n", dir.c_str());
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
            // a NUL and a newline someone else wrote are not part of the value
            setxattr(f.c_str(), "user.xdg.comment", "echoed\n", 7, 0);
            check("newline dropped", "echoed", value("user.xdg.comment"));
            check("missing file", 0, write(dir + "/gone", *desc, "x", &err));
            check("missing file says so", 1, err.find("not there") != std::string::npos);
        }
        unlink(f.c_str());
        rmdir(dir.c_str());
    }

    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
