# UTF-8 as wux's only public API

Status: surveyed 2026-11-12; the groundwork is in (one UTF-8/UTF-16 conversion pair, wux/w32/utf.h, and the A functions reading
UTF-8); the unsuffixed UTF-8 functions and the move of flxp and the applications are not started. See plan/STATUS.md.

## Where it stands (the survey)
* 127 W functions, 72 A functions (thin converters; GetACP() = CP_UTF8, so "ANSI" already means UTF-8), 425 neutral.
* flxp and the applications call no A function: they use std::u16string and a per-file W() helper over w32::utf16_of (an
  internal header), with 323 L"" literals and (LPCWSTR) casts.
* The vendored Wine controls are UTF-16 throughout and handle both the A and the W item messages.

## The plan
* Unsuffixed UTF-8 functions in wux.h (CreateWindowEx, SetWindowText, DrawText, MessageBox, ...) as converters onto the W
  internals; the W names kept for the vendored code only; the A functions deleted.
* Helpers or UTF-8 struct types for the ~40 string-carrying messages (LVM_ / TVM_ / TB_ / TTM_ / SB_ / CB_ / EM_, WM_SETTEXT /
  WM_GETTEXT) and the GETDISPINFO notifications (a buffer rule), all converting through the one pair (wux/w32/utf.h).
* Both APIs coexist while flxp moves file by file, string-free files first (filedlg/main, portal, wallpaper, xwm, preview).
* docs/APPLICATIONS.md changes with it (the public surface an application writes against).
