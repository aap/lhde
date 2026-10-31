// The texts of the explorer's copy/move user interface (explorer/fileopui.h)
// against XP's: shlwapi's StrFormatByteSize, copy.c's date line and CopyError's
// messages. No window is made.
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <string>
#include "explorer/fileopui.h"

using namespace shell::fileops;

static int g_fail = 0, g_pass = 0;
#define CHECK_EQ(a, b) do { std::string x_ = (a), y_ = (b); if (x_ == y_) g_pass++; else { g_fail++; fprintf(stderr, "FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, x_.c_str(), y_.c_str()); } } while (0)
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static Error err(const char* what, const char* path, int code, bool dest = false, Reason r = Reason::None)
{
    Error e;
    e.what = what; e.path = path; e.code = code; e.dest = dest; e.reason = r;
    return e;
}

int main()
{
    // StrFormatByteSize: bytes below 1024, then three significant digits, truncated
    CHECK_EQ(fileopui::byte_size(0), "0 bytes");
    CHECK_EQ(fileopui::byte_size(532), "532 bytes");
    CHECK_EQ(fileopui::byte_size(1023), "1023 bytes");
    CHECK_EQ(fileopui::byte_size(1024), "1.00 KB");
    CHECK_EQ(fileopui::byte_size(1340), "1.30 KB");
    CHECK_EQ(fileopui::byte_size(23506), "22.9 KB");
    CHECK_EQ(fileopui::byte_size(123456), "120 KB");
    CHECK_EQ(fileopui::byte_size(1024000), "0.97 MB");
    CHECK_EQ(fileopui::byte_size(104857600), "100 MB");
    CHECK_EQ(fileopui::byte_size(5ull << 30), "5.00 GB");

    // the replace dialog's lines: the size alone without a date, else "modified:" with Today / Yesterday / the weekday
    CHECK_EQ(fileopui::date_line(1340, 0), "1.30 KB");
    time_t now = time(nullptr);
    std::string today = fileopui::date_line(6, (int64_t)now);
    CHECK(today.compare(0, 23, "6 bytes\nmodified: Today") == 0);
    std::string yday = fileopui::date_line(6, (int64_t)now - 24 * 3600);
    CHECK(yday.compare(0, 27, "6 bytes\nmodified: Yesterday") == 0);
    struct tm tm = {};
    tm.tm_year = 2001 - 1900; tm.tm_mon = 9; tm.tm_mday = 25; tm.tm_hour = 15; tm.tm_min = 4; tm.tm_sec = 5; tm.tm_isdst = -1;
    CHECK_EQ(fileopui::date_line(1024, (int64_t)mktime(&tm)), "1.00 KB\nmodified: Thursday, October 25, 2001, 3:04:05 PM");

    // CopyError: "Cannot copy <name without its extension>: <reason>"
    fileopui::ErrorText t = fileopui::error_text(Op::Copy, err("Cannot read", "/nonexistent/dir/readme.txt", EACCES));
    CHECK_EQ(t.title, "Error Copying File or Folder");
    CHECK_EQ(t.text, "Cannot copy readme: Access is denied.\n\nMake sure the disk is not full or write-protected\nand that the file is not currently in use.");
    CHECK(!t.retry);
    t = fileopui::error_text(Op::Move, err("Cannot create", "/nonexistent/x/report.doc", EROFS, true));
    CHECK_EQ(t.title, "Error Moving File or Folder");
    CHECK_EQ(t.text, "Cannot create or replace report: The disk is write-protected.\n\nRemove the write-protection or use another disk.");
    t = fileopui::error_text(Op::Move, err("Cannot move", "/nonexistent/a.txt", 0, false, Reason::SameFile));
    CHECK_EQ(t.text, "Cannot move a: The source and destination file names are the same.");
    t = fileopui::error_text(Op::Copy, err("Cannot copy", "/tmp", EINVAL, false, Reason::IntoSubfolder));
    CHECK_EQ(t.text, "Cannot copy tmp: The destination folder is a subfolder of the source folder.");
    // a full disk: CopyMoveRetry's box, Retry and Cancel
    t = fileopui::error_text(Op::Copy, err("Cannot write", "/nonexistent/big.bin", ENOSPC, true));
    CHECK(t.retry);
    CHECK_EQ(t.title, "Copy");
    CHECK_EQ(t.text, "The disk in the destination drive is full.\n\nInsert a new disk to continue.");

    printf("fileopuitest: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
