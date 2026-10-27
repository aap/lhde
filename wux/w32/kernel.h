// Internal interface of the kernel/CRT/registry part of the win32 shim
// (crt.cpp, kernel.cpp, debug.cpp). Include after w32.h. Files that need
// res::Id must include res/res.h before w32.h (winuser.h defines RT_* macros).
#pragma once
#include <stdint.h>
#include <string>
#include "w32.h"

namespace res { struct Module; struct Id; }

namespace w32 {

// Modules. An HMODULE/HINSTANCE is a res::Module*. module_of(NULL) is the
// main module: an empty module, or the one loaded by set_main_module().
bool set_main_module(const char* path);        // .res or PE file; false on failure
res::Module* main_module();
res::Module* module_of(HINSTANCE h);
// A resource id as passed to FindResource/LoadImage etc. (MAKEINTRESOURCE or
// a string, "#123" meaning 123).
res::Id res_id_of(const WCHAR* s);
res::Id res_id_of_a(const char* s);

// Paths: a win32 path in UTF-16 (unix path, backslashes allowed) to UTF-8
// with forward slashes.
std::string unix_path(const WCHAR* s);

// Registry helpers for tests and setup code.
LONG reg_set_string(HKEY root, const char* path, const char* name, const char* value);
LONG reg_set_dword(HKEY root, const char* path, const char* name, DWORD value);
void reg_clear();                               // drop every key and value

// Debug channels: (re)parse a WINEDEBUG-style option string; channels that
// were already initialised keep their flags. Default is WINEDEBUG from the
// environment, else "err+all,fixme+all".
void debug_set_options(const char* opts);

// The fake TEB (GS base) for the calling thread, if it has none yet
// (libuxtheme's entry points; flxp's own threads do not call win32).
void ensure_teb();

// Latin-1 aware case mapping shared by crt.cpp and kernel.cpp.
WCHAR wupper(WCHAR c);
WCHAR wlower(WCHAR c);

} // namespace w32
