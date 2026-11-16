# Keyboard shortcuts

Every key lhde answers, by the program that owns it. **Configurable** marks the keys a user can rebind today: only luxwm's,
in `~/.config/lhde/keys`. Everything else is **fixed** in the code, as XP's are. A key that luxwm grabs never reaches a
program, so a binding in the keys file wins over any program key below.

## The session: luxwm (configurable)

The defaults, from luxwm's table (`luxwm/wm.c` `default_keys`). The keys file (docs/LUXWM.md "Keys") holds lines
`MODS+Key action [arg]`, MODS of `Super`/`Mod4`, `Alt`/`Mod1`, `Ctrl`/`Control`, `Shift`, the key an X keysym name, `#`
comments; actions `exec CMD`, `close`, `kill`, `cycle`, `maximize`, `minimize`, `sysmenu`, `workspace N`, `showdesktop`,
`shell CMD` (a command for luxshell), `start`, `none`. Without the file these apply; with it, the file's lines, plus Super+D,
Super+R, Super+S, Super+H, Alt+Space, the Shift+Tab pair and the Super+Shift screenshot keys where the file leaves that key unbound,
and Super alone when it binds neither Super key.

| Keys | Action | luxwm action |
| --- | --- | --- |
| Super (pressed and released alone) | the start menu (again: closes it) | `Super_L start`, `Super_R start` |
| Super+R | Run: the start menu's Run dialog, as Start > Run... (settings.ini `[commands] run` runs another program instead) | `shell run` |
| Super+E | the Explorer (flxp; rox without it) | `exec ...` |
| Super+F | Search: luxfind, the query window over the catalogues, looking in every root (the Explorer's Search for files or folders looks in its folder) | `exec luxfind` |
| Super+D | show desktop: the visible windows minimised; again brings exactly those back if nothing changed meanwhile | `showdesktop` |
| Super+S | the sidebar under the windows / on top of them | `shell sidebar-stacking toggle` |
| Super+H | hide the sidebar / show it again: minimized into the taskbar and back, at once (the taskbar's sidebar toggle, 4074's) | `shell sidebar-mode toggle` |
| Super+Return | the terminal: settings.ini `[commands] terminal`, else the first of cmd, urxvt, xterm, x-terminal-emulator (docs/SETTINGS.md) | `terminal` |
| Super+T | xterm | `exec xterm` |
| Super+Q, Alt+F4 | close the active window | `close` |
| Super+Up, Super+Down | maximise, minimise the active window | `maximize`, `minimize` |
| Super+1 .. Super+4 | desktop 1 .. 4 (with a window being moved: it goes along) | `workspace 0` .. `3` |
| Alt+Tab, Super+Tab; with Shift backwards | the task switcher (below) | `cycle` |
| Alt+Space | the window menu (below) | `sysmenu` |
| Super+Shift+S, Super+Shift+F, Super+Shift+W | a screenshot of a dragged rectangle (a click instead: the whole screen; Shift+click: the window under the pointer with its frame), of the screen, of the active window (luxshot) | `shell screenshot_region`, `shell screenshot`, `shell screenshot_window` |

Print is not bound (it may be the user's compose key); `Print shell screenshot` in the keys file binds it.

**The task switcher** (Alt+Tab or Super+Tab held): Tab / Shift+Tab the next / previous window, the arrow keys move in the grid,
Enter or releasing the modifier switches, Esc cancels (docs/LUXWM.md "Alt+Tab").

**The window menu** (Alt+Space, fixed): Up/Down, the mnemonics (R Restore, M Move, S Size, N Minimize, X Maximize, C Close),
Enter, Esc. After Move or Size: the arrows move (size: the first arrow picks the edge) in steps, Ctrl+arrow by one pixel, Enter
accepts, Esc puts the window back.

**With the mouse** (fixed): Super+drag moves a window from anywhere on it.

### Which Super combinations are free

In use by default: Super alone, Super+R, E, F, D, S, H, T, Q, Return, Up, Down, 1-4, Tab, Shift+Tab, Shift+S, Shift+F, Shift+W.

| | used | free |
| --- | --- | --- |
| letters | D E F H Q R S T | A B C G I J K L M N O P U V W X Y Z |
| digits | 1 2 3 4 | 5 6 7 8 9 0 |
| others | Return, Tab, Up, Down | Space, Left, Right, Home, End, Page Up/Down, Escape, BackSpace, Delete, F1-F12, comma, period, slash, minus, equal |
| with Shift | Shift+S, Shift+F, Shift+W, Shift+Tab | every other |

What the Windows of the time gave the free ones, to keep in mind when choosing: Super+M and
Super+Shift+M minimise all and undo it, Super+L lock, Super+U Utility Manager, Super+F1 Help, Super+Break System Properties,
Ctrl+Super+F Find Computers; Vista's Sidebar Super+Space (the Sidebar to the front) and Super+G (cycle its gadgets).

## The shell: luxshell (fixed)

luxshell grabs no keys of its own: its Super keys are luxwm's above, sent as `_LHSHELL_START` and `_LHSHELL_COMMAND`. In its
own windows:

| Where | Keys |
| --- | --- |
| context menus (sidebar, tiles, taskbar, Start button) | Up/Down, Right opens a submenu, Left or Esc closes one level, Enter or Space chooses, Home/End, a mnemonic chooses (several: cycles), another letter jumps to the next item starting with it |
| the settings flyout | Tab or Down / Shift+Tab or Up move, Enter or Space chooses, Esc closes |
| the run and search box (no start menu running), Save as | typing, BackSpace, Enter runs, Esc closes |
| every flyout | Esc closes |

## The start menu: flxp-startmenu / flxp-taskbar (fixed)

XP's start panel: the arrow keys and Tab move, Enter or Space opens, Esc, Alt+F4 or a Super key close it. The Run dialog: as a
dialog (below), Enter runs, Esc closes.

## The Explorer: flxp (fixed)

| Keys | Action |
| --- | --- |
| Ctrl+0 | the folder's default view |
| Ctrl+1 .. Ctrl+5 | XP: Details, Icons, Thumbnails, List, Small Icons; the Longhorn looks: the view slider's stops (Details, Tiles, Large, Medium, Small Icons) |
| Ctrl+Plus, Ctrl+Minus | zoom in / out (the Longhorn looks) |
| Ctrl+L | the address bar (the Longhorn header) |
| F5 | refresh |
| Enter | open the selection |
| Backspace | the parent folder |
| Alt+Left, Alt+Right | back, forward |
| F2 | rename (list and tree) |
| Delete | delete (list and tree) |
| Alt+Enter | Properties |
| Shift+F10, the Menu key | the selection's context menu |
| Ctrl+A | select all |
| Ctrl+X, Ctrl+C, Ctrl+V | cut, copy, paste files |
| Esc in the Filter box | empties it (the filter goes) |
| Enter, Esc in the address bar | go, cancel |
| Alt, F10, Alt+letter | the menu bar (menus: as below) |

## The Open / Save dialog: flxp-filedialog (fixed)

As a dialog (below), and in its list: Backspace and Alt+Up the parent folder, Alt+Left back, F5 refresh, Ctrl+H hidden files
on/off, Ctrl+A select all (multiple selection), F2 rename, Delete delete. In the Longhorn look also Alt+Right forward, Ctrl+L
the address bar, Ctrl+E, Ctrl+F or F3 the search box, Ctrl+Plus / Minus zoom, Ctrl+0..5 the views as the Explorer's; Enter
and Esc in its address, search and Filter boxes stay theirs.

## luxpad (fixed, Notepad's)

| Keys | Action |
| --- | --- |
| Ctrl+N, Ctrl+Shift+N | new, new window |
| Ctrl+O, Ctrl+S | open, save |
| Ctrl+Z, Ctrl+X, Ctrl+C, Ctrl+V, Del | undo, cut, copy, paste, delete |
| Ctrl+F, F3, Shift+F3 | find, find next, find upwards |
| Ctrl+H, Ctrl+G | replace, go to line |
| Ctrl+A | select all |
| F5 | the time and date |
| Alt+F4 | exit (asks about unsaved changes) |

The text keys are the Edit control's (below).

## luxview (fixed, XP's picture viewer's)

| Keys | Action |
| --- | --- |
| Left, Right; Up, Down | the previous / next picture; a picture larger than the window moves instead |
| Page Up, Page Down; Backspace, Space; the wheel | the previous / next picture |
| Home, End | the first / last picture |
| Ctrl+B, Ctrl+A | Best Fit, Actual Size |
| +, -; Ctrl+wheel | zoom in / out (about the pointer with the wheel) |
| Ctrl+K, Ctrl+L | rotate clockwise / counterclockwise (in the view) |
| F11 | the slide show (F11 or Esc ends it) |
| Delete | to the Trash (asks) |
| Ctrl+S | Copy To |
| Ctrl+E | open for editing (the type's next program) and close |
| Alt+Enter, Ctrl+Enter | Properties on General, on Metadata |
| F1 | the keys |
| Alt+F4 | close |

A drag moves a picture larger than the window.

## mupf (fixed, Acrobat Reader 6's where it had one)

| Keys | Action |
| --- | --- |
| Ctrl+O, Ctrl+W, Ctrl+D, Ctrl+Q | open, close the document, Document Properties, exit |
| Page Up, Page Down | the previous / next page (with facing: pair) |
| Left, Right | the previous / next page; scroll sideways when the page is wider than the window |
| Up, Down; the wheel; Shift+wheel | scroll (the wheel: the system's wheel lines, three of 40 px; Shift+wheel across; Single Page: past the page's end to the next) |
| Space, Shift+Space, Backspace | a screen down / up |
| Home, End | the first / last page |
| Ctrl+G | Go To Page |
| Alt+Left, Alt+Right | back, forward over jumps |
| Ctrl++, Ctrl+-; Ctrl+wheel | zoom in / out (about the pointer with the wheel) |
| Ctrl+1, Ctrl+0, Ctrl+2 | Actual Size, Fit Page, Fit Width |
| Ctrl+], Ctrl+[ | rotate the view clockwise / counterclockwise |
| Ctrl+F | the Find box (under the Longhorn header the header's search box) |
| Return, Shift+Return in the Find box; F3, Shift+F3 | search, the next / previous hit |
| F4 | the Contents pane |
| Ctrl+L | Full Screen (Esc leaves it) |
| Return, Esc in the status bar's page number (a click on it) | go to the page typed, back to the pages |
| Esc in the header's search box | clear the search |
| Alt, F10, Alt+letter | the menu bar |

A drag moves the pages; a click on a link follows it. With `autoscroll = 1` under `[view]` in mupf.ini (off by default, as
Firefox's is on Linux) the middle button scrolls as Firefox's autoscroll: held and moved, toward the pointer until it is let
go; clicked, until the next click or key.

## Every program built on wux (fixed)

**The Edit control** (luxpad's text, the address bar, the Filter and search boxes, every dialog field):

| Keys | Action |
| --- | --- |
| Left, Right, Up, Down | a character, a line; with Shift extends the selection |
| Ctrl+Left, Ctrl+Right | a word (with Shift: extends) |
| Home, End; Ctrl+Home, Ctrl+End | the line's start / end; the text's |
| Page Up, Page Down | a page (multiline) |
| Backspace, Delete | the character before / after the caret, or the selection |
| Ctrl+Backspace, Ctrl+Delete | the word before / after the caret, to where Ctrl+Left / Ctrl+Right go (XP inserted a DEL character for Ctrl+Backspace) |
| Ctrl+A | select all |
| Ctrl+Z, Alt+Backspace | undo |
| Ctrl+X, Shift+Delete | cut |
| Ctrl+C, Ctrl+Insert | copy |
| Ctrl+V, Shift+Insert | paste |

**Menus** (a menu bar, a context menu): Alt alone or F10 the menu bar, Alt+letter a menu, the arrows move, Enter chooses, Esc
closes, a mnemonic chooses; Shift+F10 or the Menu key a context menu; Alt+Space the window menu (luxwm's).

**Dialogs and property sheets**: Tab / Shift+Tab the next / previous control, the arrows within a group of radio buttons, Space
presses the focused button or toggles a check box, Enter the default button, Esc cancels, Alt+letter the control with that
mnemonic; in a property sheet Ctrl+Tab / Ctrl+Shift+Tab and Ctrl+Page Down / Ctrl+Page Up the next / previous page. A message
box: Enter its default button, Esc its Cancel (or OK when it has only OK), its buttons' mnemonics.

## Making the fixed keys configurable

flxp's keys are tests in its message loop (`flxp/explorer/explorer.cpp` `loop_keys`, the file dialog's `msg_filter`). Making them
configurable would take a table behind them (an accelerator table, `CreateAcceleratorTableW` with `TranslateAcceleratorW`,
built from the defaults above) and a section of the keys file the table is read from (for instance `[flxp]` lines `Ctrl+L
address`); luxpad's are already such a table (`luxpad.rc` ACCELERATORS), so it would read the same section.
