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
// While run() waits its loop dispatches the program's messages; a program whose
// own loop handles keys before TranslateMessage (the explorer's Ctrl+C/V, F5)
// gives that here so they work meanwhile (true: handled).
void set_message_filter(bool (*filter)(MSG& msg));

// Renaming as XP's shell does it (shapi.cpp SHRenameFileEx, its errors in
// CopyError's words): blanks around the name dropped; "/" (the one character
// a name cannot have here), an empty or too long name refused in a "Rename"
// box; a changed extension of a known type asks "If you change a file name
// extension, the file may become unusable." (Yes/No); a name that is taken is
// "Cannot rename x: A file with the name you specified already exists." True:
// renamed (or nothing to do), new_path is where it is now. False: the user was
// told why or said No, and XP's view then edits the label again with what was
// typed (edit_again).
bool rename(HWND owner, const std::string& path, const std::string& typed, std::string* new_path);
// A list view's label edit of item again, with text in it, all of it selected,
// once the current edit has ended (defview: LVN_ENDLABELEDIT -> LVM_EDITLABEL).
void edit_again(HWND list, int item, const std::string& text);

// The texts XP shows for an error (exposed for the tests): the message box's
// title, its text, and whether it offers Retry (a full disk).
struct ErrorText { std::string title, text; bool retry = false; };
ErrorText error_text(shell::fileops::Op op, const shell::fileops::Error& e);
std::string byte_size(uint64_t n);          // shlwapi StrFormatByteSize: "532 bytes", "1.30 KB", "23.5 KB", "120 MB"
std::string date_line(uint64_t size, int64_t mtime);   // "1.30 KB\nmodified: Today, October 31, 2026, 3:04:05 PM"

} // namespace fileopui
