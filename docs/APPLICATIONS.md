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
  objects with its win32 part flxp/shell/ui and liblhde's, the same objects flxp's programs link) whole, then `libwuxtheme.a`.
  `WUX_APP_DEPS` are those archives; `WUX_APP_FILEDIALOG` adds the Open/Save dialog (`GetOpenFileNameW`, `GetSaveFileNameW`,
  `filedlg::run`); without it those calls fail with `CDERR_INITIALIZATION`. An application that draws the Longhorn
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
speaks. Strings to the API are UTF-16 (`L"..."` is 16-bit here). `MessageBoxW` (and `MessageBoxA`, `MessageBoxIndirectW`) is
Wine's message box (`wux/vendor/wine/user32/msgbox.c`): the icon, the text and the buttons the flags ask for, laid out by the
text's size, in the message font (the style's MsgBoxFont, Tahoma 8 under XP), Escape giving Cancel, or OK in an OK box.

The look needs nothing from the application: WuxInit loads the session theme's visual style, colours, metrics and fonts, and
the program follows a theme switch by itself (`WM_THEMECHANGED`, the system colours), as every flxp program does. Controls are
themed by the style; a dialog in "MS Shell Dlg" gets the theme's dialog font.

## What the shell layer offers (shell/shell.h)

`shell::user_dir("PICTURES", "Pictures")` and the other XDG directories; `shell::spawn` and `shell::open_files` (launching, the
way a double click opens); `shell::details::write(path, *shell::details::property("description"), text, &err)` and `"tags"`,
`"comment"` (the annotations, written exactly as the Explorer writes them: docs/ANNOTATIONS.md), `read_stored`, `read_attr`,
`list_attrs` (every `user.*` name on a file), `infotip`, the Properties formats (`size_and_bytes`: "1.29 KB (1,331 bytes)",
`long_date` with seconds, `thousands`);
`shell::store::get` / `set` (settings.ini, with the change notification); `shell::session_theme()` (the theme's record);
`shell::mime_type`; `shell::trash`, the copy engine (`fileops_job.h`); `shell::thumbs`; the Explorer itself as a program:
`flxp DIR`, `flxp --select FILE`. The win32 part (`shell/ui.h`): `shell::message_box(owner, title, text, MB_OK |
MB_ICONEXCLAMATION)` (MessageBoxW with UTF-8 strings), `shell::icon_for(path, 32)` and `icon_for_key(key, px)` (the icon the
Explorer shows, as an HICON), `load_image` (an icon theme's source, an .ico or "pe:FILE#ID", with LoadImageW),
`shell::properties(owner, paths, options)` (the Properties sheet: General and Summary, as the Explorer's Properties opens it). The shell
layer's warnings on stderr start with the program's name (`shell::program_name()`, argv[0]'s basename).

## Trying it in the lab

`LAB=:9 session/lab.sh start`, `session/lab.sh shell`, then `DISPLAY=:9 build/bin/NAME`; `session/lab.sh theme luna` and
`session/lab.sh theme jade4074` switch the theme under it; `session/lab.sh shot FILE` takes a screenshot; `session/lab.sh stop`
ends everything the lab started. A headless test goes in `tests/headless/` (its own HOME and XDG directories, xdotool to drive
the window; `tests/headless/luxshot.sh` is one) and into `tests/check.sh`.

## Two worked examples

- **luxshot** (`apps/luxshot`): a dialog program with an Xlib part (`capture_native.cpp`), the Open/Save dialog for Browse...,
  the annotations written with `shell::details::write`, PNG encoding and the clipboard (`FlxpWritePng`, `FlxpEncodePng`).
- **luxattr** (`apps/luxattr`): the shell layer's Properties sheet (`shell::properties`, `shell/ui.h`) from a terminal, a main
  of fifty lines. The sheet itself (`flxp/shell/ui/properties.cpp`, its templates in `flxp/rc/shell.rc`) is the example of a
  property sheet: `PropertySheetW` of wux's comctl32 with two `PROPSHEETPAGEW` from an embedded .res, a report list view with
  an edit subclassed over a cell for in-place editing, a modal dialog of its own, cue banners (`EM_SETCUEBANNER`), the shell
  layer's `details::list_attrs` / `read_attr` / `write`, `file_info`, `type_name`, `mime_type`, `default_app`,
  `icon_for_key`, `message_box`, and a hook for its caller (`change_program`: the Explorer's Open With). luxattr is the test that an application needs no session: with `LHDE_WINDOWS=/nonexistent`
  and an empty HOME, WuxInit and the shell layer fall back by themselves (fontconfig's fonts, the classic look, the Wine icon
  recipe with a warning on stderr), and `tests/headless/luxattr.sh` runs it that way.

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
- **Shell images as `gdi::Bitmap`**: `shell/thumbs.h` (thumbnails) and `icons.h` (`load_art`: toolbar strips, Windows art by
  resource id) hand out `gdi::Bitmap`, wux's internal type; only icons have a win32 handle (`shell::icon_for`, `load_image`). The
  image lists of every icon at 16 and 32 px, one index per key, are still the Explorer's (`flxp/explorer/uiicons`).
- **The X clipboard is not INCR**: typed data larger than the server's request size (16 MB with BIG-REQUESTS) is refused.
