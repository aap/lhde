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
clipboard data, `FlxpWritePng` / `FlxpEncodePng`, `FlxpSetFullscreen`: the window manager's full screen), plus `WuxWide` / `WuxUtf8` / `WuxWindowText` for the UTF-8 the shell layer
speaks. Strings to the API are UTF-16 (`L"..."` is 16-bit here; a character above U+FFFF is a surrogate pair, and typed it
arrives as two `WM_CHAR`s, as on Windows); the A functions and the A text messages (`SendMessageA`'s `WM_SETTEXT`,
`WM_GETTEXT`) read "ANSI" as UTF-8. `MessageBoxW` (and `MessageBoxA`, `MessageBoxIndirectW`) is
Wine's message box (`wux/vendor/wine/user32/msgbox.c`) with XP's layout: the icon, the text and the buttons the flags ask for,
measured in dialog units of MS Shell Dlg 8 (Tahoma 8: buttons 50 x 14 DLU = 75 x 23 px, 4 DLU apart, 7 DLU margins, 10 between
the icon and the text and above the buttons; a button grows to its widest name), the text and the button names in the message
font (the style's MsgBoxFont: Tahoma 8 under XP, Segoe UI 9 under Jade, which leaves the buttons 75 x 23; XP takes its units
from the message font, which is Tahoma 8 under every XP style), Escape giving Cancel, or OK in an OK box.

Besides the windows and controls, wux has what a document program reaches for: accelerator tables (`LoadAcceleratorsW`,
`TranslateAcceleratorW`), a class's menu (`lpszMenuName`, also by number), `GetWindowPlacement` / `SetWindowPlacement` (the
normal rectangle while maximised; minimising is the window manager's), `DragAcceptFiles` with `WM_DROPFILES` and
`DragQueryFileW` (wux's X drop: a file dragged from flxp or any XDND source), `IsTextUnicode`, `FormatMessageW` (from a string,
an argument array), the common dialogs `GetOpenFileNameW` / `GetSaveFileNameW` (flxp's dialog; a custom template without stc32
and its hook: the controls go under the dialog's own, the hook gets `WM_INITDIALOG` and `CDN_FILEOK`), `ChooseFontW`,
`FindTextW`, `ReplaceTextW` (Wine's comdlg32, over `EnumFontFamiliesExW` and fontconfig) and `GetFileTitleW`. An owned window
with a caption (a modeless Find dialog) is its owner's transient, as XP keeps owned windows above their owner.

The look needs nothing from the application: WuxInit loads the session theme's visual style, colours, metrics and fonts, and
the program follows a theme switch by itself, as every flxp program does: wux reloads the style and posts `WM_THEMECHANGED`,
`WM_SYSCOLORCHANGE` and `WM_SETTINGCHANGE` to every window, children included (a program need not pass them on to its
controls; comctl32's rebar takes the new face colour and caption font, an image list's icon without alpha that replaces one
with it draws through its mask, a list view's header takes its font in every view). Controls are
themed by the style; a dialog in "MS Shell Dlg" gets the theme's dialog font.

## What the shell layer offers (shell/shell.h)

`shell::user_dir("PICTURES", "Pictures")` and the other XDG directories; `shell::spawn` and `shell::open_files` (launching, the
way a double click opens, a file's own Run as command first: `shell::run_as(path)`, `shell::run_file(path, command)`);
`shell::details::write(path, *shell::details::property("description"), text, &err)` and `"tags"`, `"color"`, `"run"`,
`"comment"` (the annotations, written exactly as the Explorer writes them: docs/ANNOTATIONS.md), `change_tags` (tags added
and removed in one write), `known_tags` (the tags to offer in a tag box) and `remember_tags`, `palette`, `read_stored`, `read_attr`,
`list_attrs` (every `user.*` name on a file), `infotip`, the Properties formats (`size_and_bytes`: "1.29 KB (1,331 bytes)",
`long_date` with seconds, `thousands`);
`shell::store::get` / `set` (settings.ini, with the change notification); `shell::session_theme()` (the theme's record);
`shell::mime_type`; `shell::trash`, the copy engine (`fileops_job.h`); `shell::thumbs` (and `thumbs::DecodeTurn`, one of the
process's two decodes at a time for a decoder of the program's own); the Explorer itself as a program:
`flxp DIR`, `flxp --select FILE`, `flxp --properties ITEM...` (the Properties sheet alone, on General), or from
another toolkit the session bus's `org.freedesktop.FileManager1` (docs/USAGE.md). The win32 part (`shell/ui.h`): `shell::message_box(owner, title, text, MB_OK |
MB_ICONEXCLAMATION)` (MessageBoxW with UTF-8 strings), `shell::icon_for(path, 32)` and `icon_for_key(key, px)` (the icon the
Explorer shows, as an HICON), `load_image` (an icon theme's source, an .ico or "pe:FILE#ID", with LoadImageW),
`shell::set_window_icon_key(hwnd, "notepad")` (the program's own window icon from an icon key: luxpad's notepad, luxshot's image,
flxp-settings' display), `shell::app_hicon(desktop_id, px)` (a program's icon: liblhde's lookup, docs/THEME-FORMAT.md section 9),
`shell::properties(owner, paths, options)` (the Properties sheet: General and Metadata, as the Explorer's Properties opens it),
`shell::one_line_edit` (an edit whose paste joins the clipboard's lines). The shell
layer's warnings on stderr start with the program's name (`shell::program_name()`, argv[0]'s basename).

## Hardware (lhde/hw.h)

liblhde's hardware layer is public: `#include "lhde/hw.h"` (an application has `-Iliblhde` and links liblhde's objects; outside
the tree, `pkg-config --cflags --libs lhde`). One interface, a back end per system, chosen at run time:

| | read | set | Linux | FreeBSD |
| --- | --- | --- | --- | --- |
| battery | `lhde_hw_battery(&b)`: `count`, `percent`, `state` (charging, discharging, full, idle), `ac`, `minutes`, `now` / `full` / `rate` in Wh / W (or Ah / A), `model` | | `/sys/class/power_supply/*` of type Battery (a device's own battery, scope Device, left out), Mains / USB `online` | sysctl `hw.acpi.battery.{units,life,time,state,rate}`, `hw.acpi.acline` |
| backlight | `lhde_hw_backlight(&b)`: `percent`, `raw` of `max`, `device`, `method`, `error` (why a set cannot work) | `lhde_hw_backlight_set(percent)` (1..100) | `/sys/class/backlight/*` (firmware, then platform, then raw), written when writable, else `brightnessctl` | `/dev/backlight/*` ioctls (`BACKLIGHTGETSTATUS`, `BACKLIGHTUPDATESTATUS`), `backlight(8)` when they fail |
| volume | `lhde_hw_volume(&v)`: `percent`, `muted`, `backend`, `device`, `error` | `lhde_hw_volume_set(percent)`, `lhde_hw_volume_mute(1 / 0 / -1 toggle)` | the first that answers of `wpctl` (PipeWire), `pactl` (PulseAudio), `amixer` (ALSA Master) | the OSS mixer of `hw.snd.default_unit` (`SOUND_MIXER_READ_VOLUME` / `WRITE_VOLUME`, `SOUND_MIXER_MUTE`), `mixer(8)` when its ioctls fail, then the Linux commands (a PulseAudio without a mixer device) |
| touchpad | `lhde_hw_touchpad(&t)`: `enabled`, `id`, `name`, `prop` | `lhde_hw_touchpad_set(on)` | `xinput`: the X server's device, the same on every system | |

A read returns 1 when the thing is there and 0 when not (a desktop has no battery or backlight); a set returns 0, or -1 with
`lhde_hw_error()`. Calls may run one of those programs and wait for it (two seconds at most), so a window program calls them
from a thread of its own (flxp-taskbar's tray controls, `flxp/taskbar/notify.cpp`, are the example: one worker thread reads,
runs the sets in order and posts a message when something changed); the layer keeps no lock, one thread per process uses it.
Changes: `lhde_hw_volume_fd()` and `lhde_hw_battery_fd()` are descriptors to poll (`pactl subscribe`'s output, the kernel's
uevents, devd's socket), `-1` where the system has none (FreeBSD's mixer: read every `LHDE_HW_VOLUME_POLL_MS`); after one is
readable, `lhde_hw_volume_poll()` / `lhde_hw_battery_poll()` say whether it changed. `lhde_hw_describe(LHDE_HW_ALL, buf, n)`
names the back end in use for each, for a tooltip or a bug report; `lhde_hw_fields` gives a record's fields as keys and values
(luxshell's `lh.hw_*` tables are made from it). `lhde-hw` is the same from a shell: `lhde-hw`, `lhde-hw describe`, `lhde-hw
volume 40`, `lhde-hw backlight +5`, `lhde-hw touchpad off`. Tests point it at recorded files: `LHDE_HW_FAKE=DIR` (DIR/sys/...
for Linux; DIR/sysctl/NAME, DIR/dev/backlight/NAME, DIR/dev/mixerN for FreeBSD), `LHDE_HW_SYSTEM=linux|freebsd`,
`LHDE_HW_VOLUME=wpctl|pactl|amixer|oss|mixer`.

## Trying it in the lab

`LAB=:9 session/lab.sh start`, `session/lab.sh shell`, then `DISPLAY=:9 build/bin/NAME`; `session/lab.sh theme luna` and
`session/lab.sh theme jade4074` switch the theme under it; `session/lab.sh shot FILE` takes a screenshot; `session/lab.sh stop`
ends everything the lab started. A headless test goes in `tests/headless/` (its own HOME and XDG directories, xdotool to drive
the window; `tests/headless/luxshot.sh` is one) and into `tests/check.sh`.

## Four worked examples

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
- **luxpad** (`apps/luxpad`): a port, ReactOS's Notepad nearly as it is (its C as C++ on the W functions, its resources and
  ReactOS's header comments kept): a main window with a class menu, accelerators, a subclassed Edit control and a status bar,
  the modeless Find and Replace dialogs (`FindTextW`, the `FINDMSGSTRING` message, `IsDialogMessageW` in the loop), `ChooseFontW`,
  a modal Go To dialog, the Save As dialog with notepad's own Encoding boxes under it (`OFN_ENABLETEMPLATE | OFN_ENABLEHOOK`),
  `WM_DROPFILES`, the window placement; what Windows had and lhde does not became POSIX (files through `open`/`mmap`/`write`
  for `CreateFile`/`CreateFileMapping`, the registry as two ini files through liblhde's `lhde_ini`, `errno` for
  `GetLastError`/`FormatMessage`). `tests/headless/luxpad.sh` drives it, the status bar read from `LUXPAD_TRACE=1`.
- **luxview** (`apps/luxview`): a window that paints and takes input while its work is elsewhere. Its view model
  (`model.h`, `model_native.cpp`: plain C++ with a thread, no win32 headers, driven by `tests/toolkit/luxviewtest.cpp` with a fake
  decoder) owns the list, the zoom and a loader thread with generation ids; the window posts itself a message from the loader's
  notify and takes the results on the UI thread. It draws into a DIB section of its own (`CreateDIBSection`, the picture's pixels
  written into the bits, the chrome with GDI on top, one `BitBlt`), its toolbar is hand-drawn with the style's TOOLBAR part for the
  hot and pressed buttons (`DrawThemeBackground`, `DrawEdge` under classic) and a tooltip control with `TTF_SUBCLASS` tools, the
  glyphs a resource strip of an imported DLL (`shell::resource_file`, `shell::load_image("pe:...#105")`, `GetDIBits`,
  `AlphaBlend`); full screen is `FlxpSetFullscreen`. From the shell layer: `shell::thumbs` (the cached thumbnail first, the
  filmstrip's loader, `DecodeTurn`), `sort_items` (the Explorer's Name order), `trash`, `apps_for` and `launch`, `properties`,
  the Save dialog for Copy To, `set_window_icon_key`, `theme_build` (which look). `tests/headless/luxview.sh` drives it, what it
  shows read from `LUXVIEW_TRACE=1`.

## Not public yet: what an application cannot reach

What the first applications needed and the public surface lacks, the list of what to move:

- **The Open/Save dialog is built on the Explorer's browsing widget** (`flxp/filedlg/filedlg.cpp` uses `flxp/explorer/browser`,
  the headers, the task band), so `WUX_APP_FILEDIALOG` links all of `flxp/explorer` but its main, and luxnav. The dialog and the
  widget it needs belong with the shell facilities.
- **No font fallback**: wux's GDI draws a character its font lacks as the font's missing glyph (Windows XP's GDI did too), so
  an emoji in luxpad's Lucida Console is a box although it is stored, saved and reloaded right; Uniscribe's and fontconfig's
  fallback to a face that has it is not there.
- **No printing**: no `PrintDlgW`, `PageSetupDlgW` or printer DC; luxpad's printing was left out.
- **wux depends on the shell layer**: the session theme (`flxp/shell/lhtheme`, `runtime`: fonts and the visual style) and the X
  clipboard (`flxp/shell/xclip`) are called from wux/w32, so libwux.a does not link without libflxpshell.a. Those three belong in
  wux (or liblhde).
- **Reading the screen**: wux has no screen DC (`GetDC(NULL)` + `BitBlt` does not read the screen), so luxshot reads the root
  window with Xlib itself, as it finds the active window (`_NET_ACTIVE_WINDOW`), luxwm's frame (`_NET_FRAME_EXTENTS`) and drags
  its rectangle over an override-redirect window.
- **Shell images as `gdi::Bitmap`**: `shell/thumbs.h` (thumbnails) and `icons.h` (`load_art`: toolbar strips, Windows art by
  resource id) hand out `gdi::Bitmap`, wux's internal type; only icons have a win32 handle (`shell::icon_for`, `load_image`). The
  image lists of every icon at 16 and 32 px, one index per key, are still the Explorer's (`flxp/explorer/uiicons`).
- **Decoding a picture**: there is no public decoder. luxview includes wux's `gdi/image.h` (`load_image` at a scale,
  `resample`, `fit_size`) and `gdi/bitmap.h` for its view model, as the thumbnails do; a public one would hand out a DIB section
  (or pixels and a size) decoded to fit a box, with the EXIF turn applied.
- **The Open With dialog and the roles** are the Explorer's (`flxp/explorer/dialogs`, `shell/roles.h`, not in shell.h): luxview's
  Edit takes the first program of `shell::apps_for` that is not luxview instead of the images role's second candidate or
  asking.
- **No `GdiFlush`**: wux draws at once, so a program that writes into a DIB section's bits after GDI calls (luxview) needs no
  flush, but one written for Windows calls it and does not link.
- **The X clipboard is not INCR**: typed data larger than the server's request size (16 MB with BIG-REQUESTS) is refused.
