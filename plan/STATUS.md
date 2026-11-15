# Where lhde stands

As of 2026-11-15.

<!-- The page to read first. The session updates it every round (what moved to "What works", the step each plan is at, the
next things, new decisions) and changes the date above; agents do not write status anywhere else, docs/ included. One line
per entry: the detail is in the docs (how it works), plan/*.md (what is planned) and git log (what changed). -->

Files here: plan/BUGS.md (one line per concrete item), plan/IDEAS.md (wishes), and one design file per undertaking:
plan/CATALOGUE-PROPOSAL.md, plan/CATALOGUE-REMOTE-PHOTOS.md, plan/METADATA.md, plan/THEMING.md, plan/UTF8-API.md.

## What works
Done and covered by `make check` unless a line says otherwise.

**Explorer (flxp)**
- The Explorer with XP's chrome and 4051/4074's headers (luxnav's vision header for 5048/5112), live theme switching.
- Views: Icons, Tiles, Thumbnails, List, Details (XP's columns, per folder), the 4074 view slider; thumbnails with bounded memory,
  the freedesktop thumbnailers for documents and videos.
- XP's Details rollout and 4051's task band (none / one / many, compact and expanded, top or bottom per theme).
- Copy, move, rename and delete as XP's dialogs; Folder Options with File Types; Default Programs by role; lhde-mime.
- The Open and Save dialog (flxp-filedialog, the portal), Show in Folder (org.freedesktop.FileManager1), `flxp --select`.

**Desktop and bars**
- flxp-desktop (icons, wallpaper); flxp-taskbar: XP's bar with Quick Launch, the tray and its chevron, Volume, Brightness and
  Touchpad, the taskbar menu; XP's start panel as the one start menu (the classic popup too), Run (Super+R).
- luxshell, the Avalon shell: taskbar, sidebar and its tiles (clock, notifications and their history, power), tray items,
  toasts, context menus through wuxtheme's painter, the settings flyout, instant hide (Super+H).
- The Start button's flag per theme or per build (Shell.start_icon, the builds' own bitmaps).

**Window manager (luxwm)**
- Frames in three modes (DWM glass, msstyles, Windows Classic), EWMH, the extended-frame protocol for clients.
- XP's Alt+Tab switcher, the window menu with XP's shadow, the screenshot keys, configurable keys (docs/KEYS.md).
- The frame regression: 292 renders byte-identical.

**Themes and resources**
- Theme files with inheritance and one resolver (liblhde), `lhde-theme explain / resolve --why / dirs`.
- lhde-pack and lhde-import from ISOs, WIMs, packs and style folders; the Aero patches for 4074; icon packs.
- The free baseline: ReactOS 0.4.16 plus the free pack (version 3, Pexels photos), every theme passing `make check-free`.
- wuxtheme reads and draws v3 and v4 msstyles; Display Properties (Themes, Desktop, Appearance, icon sets); font linking.

**Applications**
- luxpad (Notepad, from ReactOS's), luxview (the Picture and Fax Viewer), luxshot (screenshots), luxattr (the Properties sheet
  without a session); the application entry point (wux/wux.h, shell/shell.h; docs/APPLICATIONS.md).

**Metadata and catalogue**
- Descriptions, comments, tags, colour and Run as as extended attributes: Details columns, infotip, the band's editable rows,
  the Properties sheet's Metadata tab, the Filter box (docs/USAGE.md "Metadata", docs/ANNOTATIONS.md).
- lhde-cat: a catalogue per root (import-du, index, touch, find with subtree and type indexes, the journal, the authority
  setting); flxp reads a catalogued folder's attributes from it and records its writes there; machines.ini (docs/CATALOGUE.md).

**Platforms**
- Linux (Void): build, install, `lhde-session` (real, nested, attached, a display manager's xsession), the lab.
- FreeBSD 15: builds with clang, the session scripts, `gmake check` with its skips (docs/BUILDING.md "FreeBSD").
- The hardware layer (battery, backlight, volume, touchpad) on both, `lhde-hw`.

**Tooling**
- `make check` (wux tests, headless Xvfb scripts, ut-dump, the resolver, luxshell's Lua, the frame regression, check-free) and
  `make check-bare`; `session/lab.sh` (start, shell, theme, shot, stop); tests/keys-doc.py keeps docs/KEYS.md honest.

## In progress
- The catalogue: plan/CATALOGUE-REMOTE-PHOTOS.md (design: plan/CATALOGUE-PROPOSAL.md), step 1 in (a network folder
  listed from readdir and filled in by workers, three-stage thumbnails, the slow-path harness; its leftovers in BUGS.md), step 2 in (luxfind and tree-wide queries
  open), the machines list done as groundwork for step 3.
- Metadata: plan/METADATA.md, steps 1, 1b, 2 and 4 in; step 3 (tasks as data) not started.
- UTF-8 API: plan/UTF8-API.md, the conversion pair in; the UTF-8 functions and the move file by file not started.
- Theming without Microsoft files: plan/THEMING.md, the ReactOS baseline in; nothing-imported points and the installer open.

## Next
1. lhde-remote over ssh (one Python file, FreeBSD 13.1-safe) and flxp's provider, tested against indra and soma (step 3).
2. luxfind, the query window over the catalogue (step 2's open part).
3. The aurora in flxp, the PDC 2003 4050 demo's last piece (plan/IDEAS.md; it gets a plan file when started).
4. Publish the free pack release (plan/BUGS.md "Repository").

## Decisions
Choices made that an item might re-open; each says where it came from.
- Tags carry no colour yet; the colour is a plain attribute (user.lux.color, a name or hex) (aap, 2026-11-13).
- Where a colour shows is provisional (the Details column's square, the band's square, "Color: Red"); the badge is an idea.
- The battery (power) tile's brightness waits for its redesign (aap; compare with lh4074-193747.png then).
- XP's CJK fonts are not imported (the XP source's East Asian faces, reverted 2026-11-14); font linking falls back to fontconfig.
- The catalogue's authority is per root, file system authoritative by default; the folder Filter box keeps its folder,
  tree-wide search is luxfind's (the proposal's review, 2026-11-15).
- The Alt+Tab switcher's geometry is ReactOS's, unmeasured until VM screenshots exist (plan/BUGS.md).
- Cancel in copy/move rolls the current top-level item back (all or nothing per item); XP keeps what was copied.
- The start panel's All Programs arrow sits about 6 px left of XP's (WinXP-193504.png), kept so the Luna and Longhorn panels stay.
- 5048 and 5112 use the `concept` luxnav preset until they diverge (presets of their own then, share/navbar/vision.ini).
- luxshell's context menus are wuxtheme's painter, not 4074's Avalon look (an option for that is an idea).
- No Notepad++ (GPL-3, against lhde's GPL-2): the bigger editor is SciTE-based (plan/IDEAS.md).
- examples/aurorademo.c's extracted colour table stays until the aurora goes into flxp (aap: not worried).
