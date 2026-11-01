# Writing an lhde application

An lhde application is a Win32 program built from source against wux for X11, like the Explorer, with the desktop's shell
facilities beside it. Three layers, and an application touches only the first two:

| layer | where | what it gives | how an application reaches it |
| --- | --- | --- | --- |
| wux | `wux/` | windows, controls (Wine's comctl32), drawing, input, menus, dialogs, the clipboard, message dispatch, the visual style | `#include "wux/wux.h"` |
| the shell facilities | `flxp/shell/`, `flxp/filedlg/` | XDG directories and settings, the settings store, the session theme's record, MIME types, launching, the property registry and the annotations, file operations, thumbnails, icon themes, the Open/Save dialog | `#include "shell/shell.h"` |
| the Explorer | `flxp/explorer/` | the browser, the views, the task panes, the headers | not at all |

Everything an application may use is named in those two headers; a declaration it needs that is elsewhere belongs in one of
them (and the list at the end says what still is not).

## A new application in ten lines

1. `cp -r apps/skeleton apps/NAME`, rename `skeleton` in the file names and in `NAME.mk`'s variables.
2. `apps/NAME/NAME.mk`:

       NAME_OBJS = $(call app_objs,NAME)
       build/bin/NAME: $(NAME_OBJS) $(WUX_APP_DEPS) $(WUX_APP_FILEDIALOG)
       	$(app_link)
       $(O)/apps/NAME/NAME.o: apps/NAME/NAME.res
       APPS += NAME
       apps programs: build/bin/NAME

3. `include apps/NAME/NAME.mk` in the top-level Makefile, after `include apps/app.mk`.
4. `make -j4 build/bin/NAME`; `make install` installs it with the rest (`APPS_DEV += NAME` and `apps:` alone: built, not
   installed, as the skeleton).

## The recipe (apps/app.mk)

- Sources: every `apps/NAME/*.cpp` (`$(call app_objs,NAME)`) is Win32 code compiled with the shim's flags (`APP_CXXFLAGS`:
  16-bit `WCHAR`, Wine's headers, the CRT redirections); a `*_native.cpp` is plain C++ without the Win32 headers, for code that
  talks to Xlib or anything else whose macros clash with them (luxshot's `capture_native.cpp`).
- Resources: `NAME.rc` (dialog templates in dialog units for "MS Shell Dlg" 8, flxp's metrics: `flxp/rc/explorer.rc`) is
  compiled to `NAME.res` by mingw's windres when the .rc changes; the .res is checked in, so a build needs no windres.
- Linking: `$(app_link)` links the objects with `libwux.a` and `build/lib/libflxpshell.a` (the shell facilities: flxp/shell's
  objects and liblhde's, the same objects flxp's programs link) whole, then `libwuxtheme.a`. `WUX_APP_DEPS` are those archives;
  `WUX_APP_FILEDIALOG` adds the Open/Save dialog (`GetOpenFileNameW`, `GetSaveFileNameW`, `filedlg::run`,
  `filedlg::message_box`); without it those calls fail with `CDERR_INITIALIZATION`. An application that draws the Longhorn
  navigation bar adds `$(LUXNAVA)` to its prerequisites and `-Iluxnav` to its flags. `WUX_APP_LINK` is the same link line for a
  rule written by hand.

## The entry point

```cpp
#include "wux/wux.h"
#include "shell/shell.h"
WUX_RESOURCES(name_res, "apps/NAME/NAME.res");

int main(int argc, char** argv)
{
    WuxInit();                       // fonts, the session's visual style, the shim; before any other call
    HINSTANCE res = WuxLoadResources(name_res_start, name_res_end - name_res_start);
    // RegisterClassW + CreateWindowExW + a GetMessageW loop, or DialogBoxParamW(res, ...) for a dialog program
}
```

`wux/wux.h` is `<windows.h>`, `<commctrl.h>`, `<commdlg.h>`, `<uxtheme.h>` with `<vsstyle.h>`, `<shellapi.h>` and wux's own
additions (`wux/w32/flxp_shell.h`: desktop and dock windows, root properties, file drag and drop, the X selections, typed
clipboard data, `FlxpWritePng` / `FlxpEncodePng`), plus `WuxWide` / `WuxUtf8` / `WuxWindowText` for the UTF-8 the shell layer
speaks. Strings to the API are UTF-16 (`L"..."` is 16-bit here). `MessageBoxW` is a stub: `filedlg::message_box` is the XP
message box.

The look needs nothing from the application: WuxInit loads the session theme's visual style, colours, metrics and fonts, and
the program follows a theme switch by itself (`WM_THEMECHANGED`, the system colours), as every flxp program does. Controls are
themed by the style; a dialog in "MS Shell Dlg" gets the theme's dialog font.

## What the shell layer offers (shell/shell.h)

`shell::user_dir("PICTURES", "Pictures")` and the other XDG directories; `shell::spawn` and `shell::open_files` (launching, the
way a double click opens); `shell::details::write(path, *shell::details::property("description"), text, &err)` and `"tags"`,
`"comment"` (the annotations, written exactly as the Explorer writes them: docs/ANNOTATIONS.md), `read_stored`, `infotip`;
`shell::store::get` / `set` (settings.ini, with the change notification); `shell::session_theme()` (the theme's record);
`shell::mime_type`; `shell::trash`, the copy engine (`fileops_job.h`); `shell::thumbs`; the Explorer itself as a program:
`flxp DIR`, `flxp --select FILE`.

## Trying it in the lab

`LAB=:9 session/lab.sh start`, `session/lab.sh shell`, then `DISPLAY=:9 build/bin/NAME`; `session/lab.sh theme luna` and
`session/lab.sh theme jade4074` switch the theme under it; `session/lab.sh shot FILE` takes a screenshot; `session/lab.sh stop`
ends everything the lab started. A headless test goes in `tests/headless/` (its own HOME and XDG directories, xdotool to drive
the window; `tests/headless/luxshot.sh` is one) and into `tests/check.sh`.

## Not public yet: what an application cannot reach

What the first applications needed and the public surface lacks, the list of what to move:

- **The Open/Save dialog is built on the Explorer's browsing widget** (`flxp/filedlg/filedlg.cpp` uses `flxp/explorer/browser`,
  the headers, the task band), so `WUX_APP_FILEDIALOG` links all of `flxp/explorer` but its main, and luxnav. The dialog and the
  widget it needs belong with the shell facilities.
- **wux depends on the shell layer**: the session theme (`flxp/shell/lhtheme`, `runtime`: fonts and the visual style) and the X
  clipboard (`flxp/shell/xclip`) are called from wux/w32, so libwux.a does not link without libflxpshell.a. Those three belong in
  wux (or liblhde).
- **Reading the screen**: wux has no screen DC (`GetDC(NULL)` + `BitBlt` does not read the screen), so luxshot reads the root
  window with Xlib itself, as it finds the active window (`_NET_ACTIVE_WINDOW`), luxwm's frame (`_NET_FRAME_EXTENTS`) and drags
  its rectangle over an override-redirect window.
- **Shell icons as HICON**: `shell/icons.h` names the file or resource of an icon key; loading it into an image list is the
  Explorer's (`flxp/explorer/uiicons`). `shell/thumbs.h` and `icons.h` hand out `gdi::Bitmap`, wux's internal type.
- **The X clipboard is not INCR**: typed data larger than the server's request size (16 MB with BIG-REQUESTS) is refused.
