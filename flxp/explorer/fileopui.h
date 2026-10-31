// Copy and move as XP's shell does them (shell32 copy.c): the operation runs on
// a worker thread (shell::fileops::Job) while the caller's windows stay live;
// name clashes ask Confirm File Replace / Confirm Folder Replace, a read-only
// file being moved asks Confirm File Move, a failure is reported when it
// happens ("Error Copying File or Folder": "Cannot copy x: <reason>", which
// ends the operation as XP's does; a full disk offers Retry), and an operation
// still running after a second shows XP's progress dialog. Every program with
// the explorer's views (flxp, flxp-desktop, flxp-filedialog) installs it as
// shell::paste_items' user interface, so paste and drag and drop use it.
#pragma once
#include <string>
#include <vector>
#include "w32/w32.h"
#include "shell/fileops_job.h"

namespace fileopui {

// Runs it and returns when it is done; owner: the window the dialogs belong to
// (the active window when null).
shell::fileops::Result run(HWND owner, shell::fileops::Op op, const std::vector<std::string>& sources, const std::string& dest_dir);

// The texts XP shows for an error (exposed for the tests): the message box's
// title, its text, and whether it offers Retry (a full disk).
struct ErrorText { std::string title, text; bool retry = false; };
ErrorText error_text(shell::fileops::Op op, const shell::fileops::Error& e);
std::string byte_size(uint64_t n);          // shlwapi StrFormatByteSize: "532 bytes", "1.30 KB", "23.5 KB", "120 MB"
std::string date_line(uint64_t size, int64_t mtime);   // "1.30 KB\nmodified: Today, October 31, 2026, 3:04:05 PM"

} // namespace fileopui
