// Files on the X CLIPBOARD, as GNOME/KDE file managers exchange them: the
// owner offers x-special/gnome-copied-files ("copy|cut\nURI\n..."),
// text/uri-list, application/x-kde-cutselection and the paths as text. A
// thread of its own with its own X connection owns the selection and answers
// requests, so the toolkit's event loop is not involved; reading another
// owner's files is a synchronous conversion on a second connection. No win32.
#pragma once
#include <string>
#include <vector>

namespace shell {
namespace xclip {

// Takes the CLIPBOARD with these files (empty: gives it up). False without X.
bool set_files(const std::vector<std::string>& paths, bool cut);
// Another client took the clipboard since set_files (our cut marks are stale).
bool lost();
// Our last set_files is what the CLIPBOARD holds (owned, or the take-over is
// still pending in the owner thread); true without X (process-local only).
// When false, Paste must read the selection: a list cached from an earlier
// get_files is another owner's old offer.
bool ours();
// Somebody else owns the CLIPBOARD (Paste may find files there).
bool other_owner();
// The files on the CLIPBOARD (another owner's; waits up to timeout_ms).
// False if there are none (or no X).
bool get_files(std::vector<std::string>& paths, bool& cut, int timeout_ms = 1000);
// Text (UTF-8) on CLIPBOARD (edit controls' Copy, Copy as Path) or PRIMARY
// (an edit control's selection, pasted with the middle button elsewhere).
// set_text with "" gives the selection up; get_text reads ours directly, else
// asks the owner (UTF8_STRING, then STRING). CLIPBOARD's files read as text
// are their paths, one per line.
bool set_text(const std::string& text, bool primary);
bool get_text(std::string& out, bool primary, int timeout_ms = 500);

}
}
