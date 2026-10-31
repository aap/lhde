# How to use the Longhorn X11 lab

> **Imported from the Longhorn X11 lab** (`~/fun/longhorn/x11` at d353b69, USAGE.md) into lhde in step 3 of docs/LAYOUT.md. Program and path names are updated to lhde (lhwm → luxwm, lhwmc → luxwmc, lhshell → luxshell, lhnav → luxnav; x11/... and lhwm/themes, lhshell/themes, lhshell/assets → the lhde tree and data directories, see docs/LAYOUT.md and tools/lhde-import); the X atoms (`_LH_*`, `_LHWM_*`, `_LHSHELL_*`), the window manager name "lhwm", luxnav's C API (`lhnav_*`, `lhnav.h`), configuration paths (`~/.config/longhorn`, `~/.config/lhde/keys`, `~/.config/lhshell`) and setting values (`header_engine = lhnav`, `classic_start_menu = lhshell`) keep their names. Dated sections are history: they describe the x11 tree as it was (its compare_*.png and screenshots stay in the research workspace).

Names: **luxwm** is the window manager; **lux** is the Lua/cairo Avalon-style shell (taskbar, sidebar, tiles, toasts, start menu; the directory and binary are `luxshell`, its windows' X class still `lhshell`); **flxp** is the Win32-style explorer, taskbar and desktop built on the **wux** win32 layer (`~/src/flxp`). Shared pieces: `themes.ini` (docs/history/THEMES.md), `settings.ini` (SETTINGS.md).


## When something looks wrong: where does this pixel come from?
Start here. lhde keeps its files in three trees (docs/LAYOUT.md "The three trees"), and one command says which file every
visible part of a theme comes from:
```
lhde-theme explain                  the selected theme (with its unsaved changes), one row per component
lhde-theme explain aero5112         another theme
lhde-theme dirs                     the three trees with their sizes, and the search order
lhde-theme find style jade4074      one file by kind (theme style scheme frames pack navbar icons wallpaper fonts resources
lhde-theme find frames/jade4074-classic/caption_1.png        source), or by its relative name
lhde-theme resolve aero5112 --why   every theme key and where its value came from (file, base, default, fallback + reason)
```
`explain` prints, for the visual style (+ colour scheme for Windows Classic), the glass and the classic frame set, the taskbar,
the shell pack (+ the imageres it was cut from), the start menu, the navigation bar (its layout file and art), the icon recipe
(+ the DLLs it reads), the wallpaper, the fonts and the build's resources: the setting, a status (`ok`, `off`, or
`missing -> fallback X` with the reason) and the file with its tree in brackets:

| tree | where | what | may I edit it? |
| --- | --- | --- | --- |
| `[shipped]` | `$PREFIX/share/lhde` (code: `$PREFIX/lib/lhde`) | our text: theme files, navbar layouts, icon recipes, frame-set `theme.ini`, pack `shell.lua` | no: `make install` overwrites it; copy the file into `~/.config/lhde/<same path>` and edit that |
| `[windows]` | `~/.local/share/lhde-windows` | the Windows files of each build (`<build>/system32, themes, fonts, wallpapers`), `local/` (dropped in), `generated/` (frame and pack PNGs) | no: `lhde-import` fills it; `generated/` may be deleted (`lhde-import --regenerate`) |
| `[user]` | `~/.config/lhde` | `settings.ini`, your `themes/`, and any shipped file you override under its own name | yes |
| `[source tree]` | `<tree>/data` | the shipped text when running from a build tree | yes (it is the repository) |

Typical answers: a wrong frame -> the `frames` rows (which set, its `theme.ini`, its PNGs and the recipe that made them; glass
sets of the 4042-4093 styles are read live from the `.msstyles`); a wrong taskbar or start orb -> `shell pack` (the pack's
`shell.lua`, its images in `generated/packs/<build>` and the imageres they came from); a wrong icon -> `icons` (the recipe and
the DLL). An old-layout `~/.local/share/lhde` (everything mixed) is not searched; `lhde-theme dirs` reports it as unmigrated
until `lhde-import --migrate` (which lhde-session runs at start) has moved it.

Three ways to run luxwm, pick by how much you want to see and touch:

| | Where it appears | Interactive | Command |
| --- | --- | --- | --- |
| headless | nowhere (Xvfb `:2`), screenshots only; this is Claude's test bench | no | `session/lab.sh start` then `session/lab.sh shot out.png` |
| nested | a 1280x800 window on your desktop (`:1`) | yes, click into it | `LAB=:1 session/lab.sh start xephyr` |
| real | a second X server `:1` on tty8 | yes, full speed, GPU glass | log in on tty8 (Ctrl+Alt+F8) and run `lhde-session [THEME]` |

Display `:1` is yours (Xephyr or the tty8 session); the headless lab and all scripted tests use `:2`, so
neither side restarts the other's window manager. `lab.sh` takes `LAB=:N` to pick.

All three use the same pieces: `luxwm` as the window manager (`LAB_WM=fvwm` for your Win95 fvwm
instead), picom with `picom-glass.conf` (glx on real hardware, xrender in Xvfb), the Longhorn
wallpaper, and the theme `aero4074` (`LAB_THEME=jade4074` for the shipped look). `LAB_PICOM=0` starts the lab without picom
(`start`/`ensure`/`restart-picom` just say so); luxwm then shows the theme's classic frame (aero4074, jade4074: jade4074-classic).

## Getting the Windows resources (lhde-pack, lhde-import)
lhde ships no Microsoft files. Everything Windows-made (visual styles, DLLs for icons and art, fonts, wallpapers) comes from the
user's own copies, in two stages below. The short way, from the top-level Makefile, to a clean Windows tree (`$LHDE_WINDOWS`,
else `~/.local/share/lhde-windows`; each target first deletes it through `lhde-import --clean`, which asks on the terminal, or
without asking with `CLEAN=1`):

| make target | what it imports | what then resolves |
| --- | --- | --- |
| `install-resources-free` | the free source alone: ReactOS 0.4.16's boot CD (`LHDE_REACTOS_ISO=FILE`, an ISO or a pack of the reactos source; else the newest `~/lib/windows_isos/ReactOS-*.iso`; without one it names the download and stops) | `classic`, `lunar`, `modern`, `mizu`, `lautus` complete; every other theme as classic with ReactOS's icons, fonts and art (docs/THEME-FORMAT.md section 6) |
| `install-resources-windows PACK=lhde_winthemes.zip` | the small Windows pack alone (an essentials pack, below) | the shipped Windows themes |
| `install-resources-full PACK=lhde_wintheme_full.zip` | the ReactOS CD, then the whole Windows pack (a museum pack) | everything (the Windows files win where both have one) |

`lhde_winthemes.zip` and `lhde_wintheme_full.zip` are only default names for packs lhde does not supply: `lhde-pack --zip`
writes `NAME.zip` beside the tarball; the tarball or the pack directory works the same.

1. **`lhde-pack`** (once, by whoever has the ISOs): the raw, unmodified files each source needs, by the rules in
   `data/sources/<source>.ini`, into a resource pack. Two **profiles**:
   - `--profile essentials` (the default): only what a list of themes needs, small enough to hand to others. The themes are the
     shipped ones (`data/themes/*.theme`, not `.todo`) or `--themes a,b,c`. The file set is not a list kept by hand: the pack is
     imported into a scratch tree and, for each theme, `lhde-theme explain --files NAME` names the Windows files the resolver
     reads (its visual style with Luna's `Shell/` colour schemes, the styles and DLLs its frame sets and pack images are made
     from (the recipes' manifests name them), the DLLs of the icon recipe's `pe:` references, the wallpaper, the build's fonts
     directory, the DLLs flxp, luxnav and luxshell open: shell32, browseui, explorer.exe, shellstyle, comctl32, themeui,
     imageres, logonui, and `setup/hivedef.inf` for a classic scheme); the union is packed whole (DLLs are never cut). A build no
     theme needs is left out entirely; only referenced styles are kept (one Luna, XP's, and `luna4051` for the classic Luna frames).
   - `--profile museum`: every file of every source, as before (archaeology, and the input from which essentials is made).
   ```
   tools/lhde-pack --isos /u/aap/lib/windows_isos --out ~/packs               # essentials for the shipped themes, from the ISOs
   tools/lhde-pack --themes luna,jade4074,classic --isos DIR --out ~/packs    # essentials for three themes
   tools/lhde-pack --profile museum --isos DIR --out ~/packs                  # everything: today's complete pack
   tools/lhde-pack --from-pack ~/packs/lhde-resources-2026-10-28 --out ~/packs   # re-profile a museum pack: no ISO is read (seconds)
   tools/lhde-pack --isos DIR --sources 4074,5048,xp --profile museum         # a subset; --all adds 4074r2
   tools/lhde-pack --medium 4074=~/fun/longhorn/wim/4074/3/WINDOWS --sources 4074   # an extracted tree instead of the ISO
   tools/lhde-pack --list                                                     # the sources and whether their ISO was found
   ```
   `--from-pack` takes a pack directory or its tarball and reads nothing else; from ISOs, essentials makes the museum pack
   first (in `--tmp`, about 400 MB, deleted after) and profiles it, so both ways give the same pack. `lhde-theme explain --files`
   must be built (`make build/bin/lhde-theme`); it is also what you run to see what one theme reads:
   `lhde-theme explain --files luna` (one real path per line, relative to the Windows tree).
   Output: `lhde-resources-<date>[-essentials]/` with `<source>/{system32,themes,fonts,wallpapers}/`, `<source>/source.ini`, `MANIFEST`
   (sha256, size, pack path, source, medium:in-image path; its `# profile:` and `# themes:` header lines say which profile and
   themes made it) and the tarball `.tar.zst` (`.tar.xz` without zstd); with `--zip` also `NAME.zip` (no hard links, no
   annotations: larger, and lhde-import annotates on import). `lhde-import --pack` takes either profile unchanged.
   **Identical files are stored once** (both profiles): files with the same sha256, within or across builds (Trebuchet,
   Tahoma, comctl32 repeat through the builds), are hard links in the pack directory (relative symlinks where the file system
   has no hard links), `tar` stores them once, and `lhde-import --pack` copies every path as a normal file of its own (the
   Windows tree has no links). A hard link is one inode, so one set of extended attributes: the pack's annotation names the
   first path, the MANIFEST (one line per path) is the authority, and lhde-import annotates each imported file by its own
   path. Sources: 4015 (pre-release WIM 1.8.1), 4042 (Lab06), 4042main, 4051, 4053, 4066 (I386 layout, `*.XX_` expanded), 4074,
   4074r2 (not default), 4093, 5048, 5112, xp (Windows Server 2003 R2 x64 CD1: XP's Luna with its three schemes' shellstyle.dll,
   shell32, explorer, fonts, wallpapers), 2k (Windows 2000) and reactos (not default; below). Adding a file or a build is
   editing or adding an ini.
   **reactos** is the free source (ReactOS 0.4.16's boot CD, `ReactOS-0.4.16-i386.iso`, GPL/LGPL): its shell32, browseui,
   comctl32, themeui and explorer.exe keep Windows' resource ids, so the xp and 2k icon recipes and flxp's art by id have a
   free stand-in; its fonts (Tahoma, Microsoft Sans Serif, Marlett, Trebuchet MS, Verdana, ... under the Windows file names,
   free faces); four visual styles (`styles/lunar`, `modern`, `mizu`, `lautus`); its wallpapers. The CD carries the live
   system as a plain `reactos/` directory (layout `live`), read with 7z like a WIM's Windows directory; `explorer.exe` 0.4.x
   tells it apart. `lhde-import --iso ReactOS-0.4.16-i386.iso` (or `--dir` on an installed or extracted ReactOS) imports it
   into `lhde-windows/reactos/` (about 31 MB). Not taken: ReactOS's classic schemes (its hive has Vista's `New Schemes`
   form, which lhde-schemes does not read: the built-in Windows Standard stands in), its cursors (user32 resources; lhde
   uses X cursors), and Blackshade, which ReactOS's source tree has only as the unbuilt sources of a style
   (`media/themes/Blackshade`, a placeholder its build skips): there is no blackshade.msstyles to import.
   **What the free baseline shows**, and what needs XP's files: Windows Classic and the four ReactOS styles with ReactOS's
   icons, fonts and Marlett; the classic start button with flxp-taskbar's own flag (XP's or 2000's explorer.exe give theirs);
   the Explorer's XP task pane (the web view's Places / Details sections) in the classic look, as XP itself shows it under a
   style that has no shellstyle.dll: ReactOS implements no web view (its DefView has no task pane, browseui says "We don't
   support WebView") and ships no shellstyle.dll, and its styles' ExplorerBar images are placeholders (flat fills), so flxp
   draws the classic sections in the style's system colours, with the chevrons drawn after XP's. The Luna task pane (the
   blue, olive or silver gradient, the white-to-colour section headers, the round chevrons) needs XP's Luna and its
   `Shell/<colour>/shellstyle.dll`; with Luna imported but no shellstyle.dll (a Longhorn build's copy) flxp draws that look
   itself from the colour scheme. ReactOS's Modern style loads here although XP rejects it ("Light_Button_BMP missing": its
   bitmaps carry BITMAPV5 headers, which XP's uxtheme does not read and wuxtheme does).
   Sizes with the ISOs here (2026-10-28; the tarball is `tar` + `xz -9e`, as no zstd is installed here): **museum** 418 MB
   raw (377 MB stored once), 57 MB compressed; **essentials** for the 14 shipped themes 169 MB raw (164 MB stored once), 36 MB
   compressed, in 5 builds (4051 4074 5048 5112 xp) and the 2k icon DLLs. About 20 s from the ISOs, 10 s `--from-pack` (the
   tarball's xz is most of it).
   Themes that need a file no medium has (`aero4074`, `aerovision`: `aero-patch.msstyles`) report it missing in
   `lhde-theme explain` with either profile: those styles go in with `lhde-import --style` (below).
2. **`lhde-import`** (every user): the pack into the Windows tree (`$LHDE_WINDOWS`, else `~/.local/share/lhde-windows`;
   docs/LAYOUT.md "The three trees"):
   ```
   lhde-import --pack lhde-resources-2026-10-28.tar.xz        # or the directory
   lhde-import --style ~/aero.msstyles aero-patch              # a style dropped in by hand (the 4074 Aero patch: aero4074, aerovision)
   lhde-import --regenerate                                    # delete generated/ and make it again from the imported files
   lhde-import --reindex                                       # make index/ again (after moving files by hand)
   lhde-import --migrate -n                                    # what the move from the old ~/.local/share/lhde would do; without -n: do it
   lhde-import --list
   lhde-import --clean [--yes]                                 # delete the Windows tree (asks first; --yes: does not)
   ```
   Each `<source>/` of the pack is copied as it is to `~/.local/share/lhde-windows/<source>/` (`system32/` with shell32,
   imageres, explorer, browseui, comctl32, themeui, shellstyle, uDWM; `themes/` = the build's Resources/Themes; `fonts/`,
   `wallpapers/`, `source.ini`); a style dropped in by hand goes to `local/themes/`. `index/` holds the names the programs ask
   for, as relative symlinks: `styles/<name>.msstyles` (build-qualified: jade4074, slate4051, aero5112, luna = XP's own, whose
   `styles/luna/` is XP's `themes/Luna/` with `Shell/<Scheme>/shellstyle.dll`; luna4051 / luna4074 = Longhorn's copies),
   `resources/<source>`, `fonts/<source>`, `wallpapers/<source>`, `frames` and `packs` (-> `generated/`). Then the recipes of
   each source run: frame sets (CompositedWindow streams, `msstyles2theme.py`, 5048's uDWM bitmaps, `build_aero5112.py`,
   `build_aerovision.py`) and luxshell's pack images (imageres PNGs, shell32 icons, start panels, the 5112 taskbar) into
   `generated/frames/<set>/` and `generated/packs/<build>/`, each with a manifest naming its inputs and their sha256.
   Developer paths through the same rules: `lhde-import --iso FILE`, `--wim FILE`, `--dir WINDOWS-DIR` detect the build from
   explorer.exe's version (6.5.40xx Longhorn, 6.0.5048/5112, 6.0.3790 Server 2003, 5.0 Windows 2000) and the layout, pack into a
   temporary pack and import it (`--source ID` skips the detection). `--from-workspace ~/fun/longhorn` copies aap's research
   workspace (the demo wallpapers of aero4074 and aerovision come only from there; they land in `local/`).
3. **Classic schemes** (Display Properties > Appearance > "Windows Classic style": Windows Standard, Windows Classic, Brick, ...,
   High Contrast; docs/THEME-FORMAT.md section 2a): the `xp` and `2k` sources now carry the medium's `HIVEDEF.INF` (the default
   user hive's setup data) and lhde-import turns its `Control Panel\Appearance\Schemes` values into
   `generated/schemes/<scheme>.scheme` (looked up as `styles/<scheme>.scheme`). Without a re-import, the stand-alone importer
   reads them straight from the ISO (7z) or an extracted `HIVEDEF.INF` into `local/schemes/`:
   ```
   tools/lhde-schemes "/u/aap/lib/windows_isos/Windows Server 2003 R2 Enterprise Corp x64 CD1.iso"   # -> ~/.local/share/lhde-windows/local/schemes
   tools/lhde-schemes --windows /tmp/scratch-windows --force I386/HIVEDEF.INF
   ```
   22 schemes (XP, Server 2003 and Windows 2000 have the same ones), the "(large)" / "(extra large)" variants as the font sizes of
   their scheme. Without any, "Windows Standard" is the built-in classic colours.

## lhde-theme
The resolver from the shell (docs/THEME-FORMAT.md, liblhde):
```
lhde-theme list                         every theme: the user's, the shipped ones, bare styles; "style missing" marks a theme
                                        whose visual style is not imported (the settings lists leave it out)
lhde-theme resolve aero5048 --why       the record, each key with where it came from (file, base, default, fallback + reason)
lhde-theme resolve luna --get path.style
lhde-theme set jade4074                 settings.ini [theme] name (the current layer dropped), then _LH_THEME on $DISPLAY
lhde-theme layer                        the current layer: settings.ini [current], the unsaved changes on top of the theme
lhde-theme layer-set Glass.composition off   a key of the selected theme's layer ("-" removes it)
lhde-theme save-as my-jade "My Jade"    the theme with its layer as ~/.config/lhde/themes/my-jade.theme, selected
lhde-theme delete my-jade               one of your theme files
lhde-theme styles | schemes             the visual styles and the classic schemes Display Properties lists
lhde-theme set-key aero4074 Glass.composition off    -> aero4074-modified.theme (the earlier model; luxshell's flyout)
lhde-theme setting sidebar width 200    a per-user key; without VALUE: print it
lhde-theme style jade4074               what liblhde reads from a style (kind, format, build, family, first scheme)
lhde-theme migrate                      ~/.config/{longhorn,lhshell,flxp,lhwm} -> ~/.config/lhde + ~/.local/state/lhde (once)
lhde-theme explain [THEME]              where every visible part comes from ("When something looks wrong" above)
lhde-theme dirs [--list|--windows|--user]   the three trees and the search order; --list for scripts
lhde-theme find REL | find KIND NAME    one data file (exit 1 when no tree has it); lhde-theme path VALUE: a theme value as a path
```

## Themes and switching them

One setting for everything (`SHELL-DESIGN.md` 6.1, details for client authors in `docs/history/THEMES.md`): the root property
`_LH_THEME` holds a theme *name* (a theme file, an alias or a bare style; docs/THEME-FORMAT.md), its resolved record is
`_LH_THEME_RESOLVED`. luxwm picks its frames from it, luxshell its taskbar/sidebar
pack and the wallpaper, and clients (extendframe, flxp) their visual style and glass; all of them follow a change live, nothing
restarts. lux remembers the last theme in `~/.config/lhde/settings.ini` (`[theme] name`, below) and publishes it when a session
starts without one. Your own variants of a theme (presets) are described under "Settings and presets" below.

| Name | Frames | Shell pack | Compositor |
| --- | --- | --- | --- |
| `aero-patch` (formerly `aero4074`, still an alias) | Aero glass (`data/frames/aero-patch`; the longhorn.ms patch style) | Aero | glass with picom, Jade classic frames without |
| `jade4074` | Jade glass (`data/frames/jade4074`) | Aero | glass with picom, Jade classic frames without |
| `jade4074-classic` | Jade msstyles frames | Aero | never (`composition = off`) |
| `slate4074` | Slate 4074 msstyles frames | Slate (4074 look: 30 px bar, nearest-neighbour scaling) | never |
| `slate4051` | Slate 4051 msstyles frames | Slate 4051 (29 px bar, bilinear scaling, 4051 images, clock tile in the list) | never |
| `luna` (aliases `luna-blue`, `luna4051`), `luna-olive`, `luna-silver` | Windows XP Luna msstyles frames from 4051's copy (blue, olive green, silver) | Slate bars, flxp's start panel in Luna's look | never |
| `luna-xp` | XP SP3's own Luna, drawn live from the style | Slate bars, flxp's start panel in Luna's look | never |
| `lunar`, `modern`, `mizu`, `lautus` | ReactOS's free styles (the reactos source), drawn live from the style | flxp's classic taskbar and start panel in the style's look | never |
| `royale`, `zune`, `embedded` | Microsoft's later XP styles (Media Center 2005, Zune, Embedded), drawn live from the style | flxp's classic taskbar in the style's look (Slate pack as stand-in) | never |
| `aero5048` | 5048's uDWM bitmaps | Aero | glass with picom |

The old luxwm theme directory names still work everywhere a name is taken (`aliases` in themes.ini): `aero`, `aero4074`, `jade`,
`slate4074-classic`, `slate4051-classic`.

**Resources and wallpapers (2026-10-25).** Every style, font directory and wallpaper the themes use is under
`/u/aap/fun/longhorn/resources/` (`resources/README.md`); `themes.ini` points only there. Each theme's wallpaper is chosen in
`data/wallpapers.ini` (one `[NAME]` section with `file` and `mode`, plus `[default]`), not in themes.ini: the 4074 themes get the
4074 leaves, slate4074 Stonehenge, slate4051 4051's wheat field, aero-patch the 4093 tree, aero5048 5048's glass facade, the Luna
and XP styles Bliss and XP stock pictures. Your own choice still wins: settings.ini `[desktop] wallpaper` (Display Properties'
Desktop tab and Set as Desktop Background write it into the current layer, `settings.ini [current]`), your own theme file's
`[Desktop] wallpaper`, or `[desktop] wallpaper = none` for no picture in any theme (docs/THEME-FORMAT.md section 3).

**4051 vs 4074 Slate.** Builds after ~4060 look like 4074: their Avalon bars scale images nearest-neighbour, so the start
button (imageres 1156, 74x23, drawn 79x29) has the blocky flag; 4051 scaled bilinear (the smooth flag). `slate4051` has its own
pack for that and for 4051's other differences: a 29 px bar, the images of 4051's own imageres.dll (the right-angled tile
arrows), the clock as a tile in the list with the 96 px face, no power tile. 4051 really ran the classic Win32 taskbar there; the
Avalon taskbar with the 4051 start button stands in for it (`data/packs/4051-slate/shell.lua`, `lhshell/compare_4051_4074.png`).

**Fonts.** Each theme names its font directories (`fonts =` in themes.ini, docs/history/THEMES.md "Fonts"); luxwm and luxshell load them while the
theme is active, so captions, taskbar and menus use the Segoe UI that 4074 ships (`wim/4074/3/WINDOWS/Fonts/seguibk.ttf`, version
0.7) instead of a fallback. Nothing is installed system-wide. `LHWM_DEBUG=1` (luxwm) and `LHSHELL_DEBUG=1` (luxshell) log the face the
caption / pack font resolves to.
Since 2026-10-27 lux draws all its text with libuxtheme's GDI text (the glyphs flxp and luxnav draw: whole-pixel stems instead of
pango's fractional positions); `make` needs a libuxtheme with the text calls, found as luxnav finds it
(`(gone: the top-level build)`: flxp's build tree `~/src/flxp` until it is installed). In Aero vision the task buttons' text is black on
a white halo (pack `aerovision`, docs/history/THEMES.md "luxshell packs" `taskbar_text`).

**Window icons.** Captions and task buttons show the window's `_NET_WM_ICON` (the size nearest at or above 16 px), else its
`WM_HINTS` icon pixmap (xterm, xclock, Xt/Motif/FOX programs), else the theme's default icon: on the glass themes the Windows flag
(`DEFAULTICON` of the msstyles), on the classic and Luna themes and on the taskbar the XP application icon. A window that changes its
icon is redrawn at once.

| Where | Start with a theme | Switch while running |
| --- | --- | --- |
| real `:1` (tty8) | `lhde-session slate4074` (no name: the last one) | `lhde-session theme jade4074` from any terminal, on `:0` or inside `:1` |
| nested / headless | `LAB=:1 LAB_THEME=jade4074 session/lab.sh start xephyr` | `LAB=:1 session/lab.sh theme slate4074` (or `LAB_THEME=slate4074 session/lab.sh restart-wm`) |
| anywhere | | `xprop -root -f _LH_THEME 8u -set _LH_THEME slate4074` |

`luxwm -t NAME` / `LHWM_THEME=NAME` only applies while the display has no `_LH_THEME` yet; after that the root property wins
(a restarted luxwm keeps the session's theme). `LHSHELL_THEME=themes/slate` (a pack directory) is now only luxshell's fallback
when no registry theme is known.

Session commands (luxshell, 6.2; the start menu's Log Off / Stand by / Shut down use them after a confirmation flyout): `logoff`, `standby`, `shutdown`, `restart`, `lock`, overridable
in `~/.config/lhde/settings.ini` under `[commands]` (`name = shell command`). Defaults: `logoff` = `lhde-session stop` in
the tty8 session, else `luxwmc quit` (luxwm exits cleanly: its root properties are deleted, the windows stay mapped; the
lab's `restart-wm` brings it back); `standby` = `zzz` if installed, else `loginctl suspend`; `shutdown` = `loginctl poweroff`;
`restart` = `loginctl reboot`; `lock` = `xlock` or `slock` if installed. From a terminal:
`xprop -root -f _LHSHELL_COMMAND 8u -set _LHSHELL_COMMAND logoff`.

`lhde-session` also has `shell on|off` (the Avalon taskbar+sidebar), `picom on|off`, `desktop on|off`, `taskbar
avalon|classic|classic+sidebar|none` (the bottom bar, live; see "Classic taskbar instead of the Avalon one" below), and `stop`. `SECOND_DISPLAY=:N`
points these commands at another display (e.g. `SECOND_DISPLAY=:3 lhde-session taskbar classic` on a lab).

The `:1` session starts flxp's desktop (`flxp-desktop` from PATH, else `/u/aap/src/flxp/build/desktop/flxp-desktop`):
wallpaper and icons in a `_NET_WM_WINDOW_TYPE_DESKTOP` window, started after luxwm and with luxshell, from the session
shell (so it and the explorers it opens share DISPLAY, the session D-Bus and PATH). Unless `flxp.ini` or `FLXP_WALLPAPER`/
`FLXP_ICONS` say otherwise it shows the Longhorn wallpaper (fill) and the longhorn icon theme. It survives `theme`
switches (not part of the WM loop); log in `$XDG_RUNTIME_DIR/lhwm-lab:1/flxp-desktop.log`. While it runs luxshell is
started with `LHSHELL_DESKTOP=0` (no desktop layer of its own: flxp-desktop fades to black in maximised mode itself).
`LAB_DESKTOP=0 lhde-session` starts without it; `desktop off|on` stops/starts it later and restarts luxshell with
or without its layer. `LHSHELL_DESKTOP=0` (or off/no/false) works for any luxshell start.
A theme switch restarts nothing: frames, bars and clients change in place.

Startup wallpaper: `lhde-session` sets the root background once, before anything else paints, with `lhwall.sh` (sourced here and by
`lhde-session desktop on`): the session theme's resolved `Desktop.wallpaper` and `Desktop.mode` (`lhde-theme resolve THEME
--get`: the current layer, the theme file and its base chain, the build's default; `settings.ini [desktop] wallpaper = none`
applied), mapped to `feh --bg-fill|max|center|tile|scale`; `none` is a
plain root (`xsetroot -solid '#3a6ea5'`); `LHSHELL_WALLPAPER=0` forces `none`. flxp-desktop reads settings.ini itself (the session no
longer exports an `FLXP_WALLPAPER` default, which would have overridden it) and gets the theme's wallpaper from lux through the root
property `_FLXP_WALLPAPER` (SETTINGS.md "Resolved theme": the registry has no `wallpaper` keys now), and lux never runs feh/xsetroot while another program
owns the desktop (`LHSHELL_DESKTOP=0`, i.e. flxp-desktop on, or a `_NET_WM_WINDOW_TYPE_DESKTOP` window of another client), so the
session shows one wallpaper from the first frame on.

Inside the session (default keys, override in `~/.config/lhde/keys`, one binding per line):

| Keys | Action |
| --- | --- |
| Super (pressed and released alone) | start menu (luxwm sends `_LHSHELL_START`; again closes it) |
| Super+Return | terminal (urxvt), Super+t for xterm |
| Super+e | flxp explorer (ROX-Filer when flxp is not installed) |
| Super+d | show desktop, as Windows' Win+D: minimises the visible windows (they stay minimised); Super+d again brings exactly those back, but only if nothing changed in between (no window restored, activated, opened or closed); otherwise it is a new "show desktop" |
| Super+r | Run box (lux's own, via `_LHSHELL_COMMAND run`; or `Mod4+r exec rofi -show run` in your keys file) |
| Super+s | sidebar under the windows (part of the desktop) ⇄ on top of them, at once (`_LHSHELL_COMMAND sidebar-stacking toggle`; saved as `[sidebar] stacking`) |
| Super+q or Alt+F4 | close window |
| Alt+Space | the window menu (Restore, Move, Size, Minimize, Maximize, Close; arrows, mnemonics, Enter, Esc); also a right click on the caption or a click on the caption icon (double click there: close) |
| Alt+Space, M / S | keyboard move / size: arrows move (size: the first arrow picks the edge) in half-button steps, Ctrl+arrow 1 px, the mouse takes over, Enter or a click accepts, Esc puts the window back |
| Alt+Tab, Super+Tab (Shift: backwards) | as Windows': while Alt (Super) is held each Tab previews the next window by raising it (the previous one goes back to its place; minimised windows come last and are not previewed); releasing Alt focuses and raises the chosen one, every other window keeps its stacking order; Esc cancels |
| Super+Up / Super+Down | maximise / minimise |
| Super+1..4 | workspaces (virtual desktops: the taskbars show the current one's windows; `_LHSHELL_COMMAND desktop N\|next\|prev` or any pager's `_NET_CURRENT_DESKTOP` request switches too) |
| drag caption | move; drag edges and corners | resize |
| double-click caption | maximise/restore |
| Super+drag / Alt+drag | move / resize anywhere on the window (berry's defaults) |

Extended glass frames (`docs/CLIENT-PROTOCOL.md`): `make && DISPLAY=:1 build/examples/extendframe` builds and opens the
reference client (Explorer-style 60 px glass header; drag it, double-click to maximise, drag the right edge, Escape or q quits;
needs x11, xext, cairo and pangocairo). Without glass (Slate, Luna, no picom) its header shows luxwm's `_LHWM_HEADER_PIXMAP`, the
theme's caption band continued down. With aero and jade the glass header is the theme's Explorer plate (`plate_explorer` in
`data/frames/*/theme.ini`); `glass_alpha` there sets how see-through the frame glass is (0.5 matches 4074's screenshots, 1.0 is the
texture as stored), `glass_alpha_mode = square` the alternative 2005 weighting. `examples/extendframe -c` also takes the caption rows (experimental `CLIENT_CAPTION`: it
draws its own title there, luxwm's caption buttons stay clickable through a hole in its input shape).

The navigation bar of the concept art (`NAVBAR.md`, `luxnav/`): `make` (builds `luxnav/libluxnav.a` first; needs
libuxtheme for the theme's art, else drawn stand-ins), switch to Aero vision (`session/lab.sh theme aerovision`, or `DISPLAY=:3 xprop
-root -f _LH_THEME 8u -set _LH_THEME aerovision`) and run `DISPLAY=:3 examples/navdemo [-r ROOT] [DIRECTORY]`: a small file browser
with the title in the frame's 15 px deco, the back/forward orb, the breadcrumb (click a segment; click the box's empty part or
Ctrl+L to type a path, Enter goes, Escape cancels; "«" stands for the segments that do not fit), a search box that filters the
listing (Ctrl+F, or just type), back/forward (also Alt+Left/Right, mouse buttons 8/9; Backspace goes up), double-click a folder line
to open it; drag the header to move, double-click it to maximise, the top 3 rows resize. Escape (outside a field) or Ctrl+Q quits.
`-r ROOT` starts the breadcrumb at ROOT instead of "File System" (`/`); `NAVDEMO_DEBUG=1` prints luxnav's layout. It works under
every theme (glass or luxwm's header pixmap); the vision geometry needs `aerovision`. Tuning: `data/frames/aerovision/nav.ini`
(orb size, boxes, fonts, colours, the text glow; comments there), live with `session/theme-reload.sh :3` or `session/theme-watch.sh :3`;
`examples/compare_navdemo.png` / `compare_navdemo_4x.png` compare it with the concept art.

Experimenting with the header (`NAVBAR.md` "Presets"): run `session/theme-watch.sh :3` in one terminal and
`NAVDEMO_DEBUG=1 DISPLAY=:3 examples/navdemo` in another, then edit `data/frames/aerovision/nav.ini` and save: every save reloads
the header. The file is a set of presets (`[preset small]`, `[preset big]`, each with all keys; `preset = NAME` at the top picks
the default); in navdemo `p` / `P` cycle through them live (the title shows `[NAME]`, `NAVDEMO_PRESET=NAME` starts with one) and a
new size is a copied and renamed section. To move the breadcrumb box or the search box, set `address_dy` / `search_dy` (rows, + =
down) or `address_dx` / `search_dx` at the top of the preset; `orb_*`, `address_*`, `search_*` `_x`/`_y`/`_w`/`_h`, `title_dx`,
`title_baseline` and `crumb_text_dx`/`_dy` place things absolutely (x in client pixels, y in frame rows; empty = automatic, whose
value the comment beside each key gives). The numbers to start from: NAVDEMO_DEBUG prints every part's final rect (client x, y, the
frame row, size) on each relayout. `variant = big` is the tall header with the travel buttons' drop-down arrows (69 rows); there a
press on an arrow, or a right click on back/forward in either variant, opens the history list.

The right-hand toolbar and the drop-downs (`NAVBAR.md` "Toolbar", "Breadcrumb drop-downs"): nav.ini's `tools` key lists the items
per preset (`tools = search_field`, aap's presets, is the search box alone; `[preset concept]` has `tools = search favorites`, the
concept's magnifier and star orb; all kinds are commented at the top of nav.ini: `search`, `search_field`, `favorites`,
`bookmark:PATH[:LABEL]`, `views`, `newfolder`, `separator`). `NAVDEMO_TOOLS="newfolder views separator search favorites"` overrides
the list for a run. In navdemo: the magnifier brings the search field out (it goes back when it loses the focus empty); the star
opens the GTK bookmarks (`$XDG_CONFIG_HOME/gtk-3.0/bookmarks`) with "Add current folder" at the top (point `XDG_CONFIG_HOME` at a
scratch directory to try it without touching your own); a bookmark button goes to its folder; Views switches the listing between
List and Details; New Folder creates "New Folder", "New Folder (2)", ... A hovered item shows a tooltip after half a second. Every
chevron of the breadcrumb is a drop-down: a press lists that folder's subfolders (sorted, dot folders left out, the next segment's
folder bold and selected), the chevron after the last segment appears while the breadcrumb is hovered (`sep_last`), and "«" lists
the collapsed ancestors; Up/Down/Home/End or a letter move in a list, Enter or a click goes there, Escape or a click outside closes
it. The icons come from the registry's `flxp_longhorn_system32` (`resources/system32/4074/shell32.dll`; nav.ini `icon_dir`
overrides it), the star from the visual style. `examples/compare_navdemo_tools.png`: the concept and several configurations.

Animations (`ANIMATE true` in `luxwm/config.h`, then `make`): with picom, Longhorn 4074's set by default (`ANIMATE_STYLE ANIM_4074`):
opening a window and restoring a minimised one fade it in over 350 ms with a slight scale-in (standing in for 4074's 3D swing),
while minimise, close and maximise/restore are instant, as in 4074. `ANIMATE_STYLE ANIM_5048` is the April 2005 curve (266 ms),
`ANIM_INVENTED` luxwm's earlier, non-Longhorn 250 ms shrink towards the bottom edge plus a maximise morph. Without a compositor luxwm
flies the caption bar to the taskbar button like XP on minimise, restore and maximise (`ANIMATE_WIREFRAME true` for XOR zoom
rectangles instead). `ANIMATE false` turns all of it off (luxwm then sets no `_LHWM_ANIMATE`, so the picom rules match nothing). `DRAG_INTERVAL` (ms, default 16) caps how
often an interactive move/resize is applied; `DRAG_THRESHOLD` (px, default 4, Windows' SM_CXDRAG) is how far the pointer must
move before a caption press becomes a move, so a double-click with a shaky hand restores a maximised window instead of dragging it.

Minimised windows are only hidden until the taskbar exists: `./luxwmc` has berry's IPC commands
(e.g. `luxwmc window_close`, `luxwmc switch_workspace 1`, `luxwmc quit`), and Alt+Tab reaches hidden windows.

Housekeeping: `session/lab.sh restart-wm` installs a fresh build (`make` in `luxwm/`) and restarts the WM on
`:1` without disturbing your `:0`; `session/lab.sh stop` tears the lab down. `./hovertest.sh` scripts the
button states. Nothing here touches your fvwm session except picom, which you started yourself.

## The Explorer's views (flxp)

In the Longhorn frame (and the Longhorn file dialog) the Explorer's list has build 4074's three views, each at an image size:

| view | sizes | layout |
| --- | --- | --- |
| Details | 16-32 px | the table; the size is the rows' image (17 px rows at 16) |
| Tiles | 32-96 px | the image at the left, the name and, from 48 px, two grey "Label: value" lines right of it (a folder its Type; a picture Dimensions and Type; a drive Percent Full and Free Space; another file Type and Size); 218 x 56 at 48 px |
| Icons | 16-256 px | the image above, the name below (two lines, then an ellipsis); Large 128-256, Medium 48-128, Small 16-48 |

The view button at the bottom of the left pane (the Explorer's and the file dialog's) is 4074's: its left part shows the view's picture and goes to the next view
(Details, Tiles, Icons, each at the size it had last); its arrows open the slider, a white popup with the five stops' pictures
(Details, Tiles, Large, Medium, Small Icons) beside a vertical slider. Dragging the thumb (or Up/Down) sizes the view as it moves,
growing downwards through Details and Tiles, then jumping to 256 px at Large Icons and shrinking downwards to 16 px at Small Icons;
a click on a picture shows that stop at the size it had last and closes the popup; Escape, Enter or a click elsewhere closes it. The
pictures are the 4074 pack's (`packs/4074/imageres`, imported by `lhde-import`); without it the control draws plain shapes.

Choosing a view otherwise: View > Details, Tiles, Icons (also in the list's context menu and the header's Views drop-down); Ctrl+1 Details,
Ctrl+2 Tiles, Ctrl+3, 4, 5 Large, Medium, Small Icons (4074's five slider stops, top to bottom), Ctrl+0 the default view; Ctrl+plus,
Ctrl+minus, Ctrl+wheel and View > Zoom make the view shown a step bigger or smaller within its range (4051's levels 16 21 27 36 48 64
84 111 147 194 256 and the range's ends). Each of the five stops remembers the size it was last shown at, and "Icons" is the icon
stop used last, so going back to a view brings its size back; the sizes are kept for every window in
`~/.local/state/lhde/flxp-view-sizes`. Every folder keeps its own view, size and Hide File Names in `flxp-views` beside it
(`remember_views`, Tools > Folder Options); a folder never opened gets `default_view_longhorn` (Icons at 48 px unless Folder Options
says otherwise).

The breadcrumb names your home folder "Home" (Home > Documents), as the namespace places it under the Desktop. Folder Options'
"Display the full path in the address bar" (`breadcrumb_home = 0` in `flxp.ini`) shows a folder under it by its real path instead,
My Computer > Local Disk (/) > home > you > Documents; the default is `breadcrumb_home = 1`.

View > Show in Groups (`groups = 1` in `flxp.ini`) groups Tiles and Icons by type, under the theme's group headers (4074's
GROUPHEADER band with the title and its count, the collapse button at the right end; a click on a header folds the group).

Hide File Names (View menu) shows Icons without names, in cells hugging the images; it belongs to the folder and to Icons: Details
and Tiles always show the names, and the names stay hidden when the folder comes back to Icons.

Thumbnails show in every view and are just bigger or smaller images: a picture in a photo frame that follows its aspect ratio (a soft
shadow, a grey line, a mat graded from white to light grey; no frame below 32 px), any other file its own icon at the size, a
folder 4074's photo standing in the open folder where the build has that art (shell32 #389/#390 of `longhorn_system32`; the
folder's `folder.jpg` first, else its first picture), else the folder icon with up to four pictures on it. There is no square
behind them; selection and hover are the theme's `Explorer::ListView` LISTITEM underlay (Slate tan, Jade blue; in Icons around
the icon and its label, not the whole cell), the classic highlight in styles without it. View > Show Thumbnails (and Folder Options) turn them off for every folder (`thumbnails = 0` in
`flxp.ini`); on network mounts (NFS, SMB, sshfs and other FUSE network filesystems) they are off unless `thumbnails_network = 1`,
since every picture would be read over the network. The thumbnails are cached in `~/.cache/thumbnails` (the freedesktop spec, shared
with other programs).

In the XP frame the Explorer keeps XP's views: Thumbnails (squares of `thumbnail_size`), Tiles, Icons, Small Icons, List,
Details; Ctrl+1 Details, 2 Icons, 3 Thumbnails, 4 List, 5 Small Icons. Tiles is XP's: the 48 px icon at the left of a tile and the
name right of it (wrapped to two lines when it does not fit), with up to two grey lines of values under it, as XP's TileInfo gives
them: a file its type and size, a picture its dimensions and type; a folder or drive has none of its own, so it shows the name
alone. The column the folder is arranged by (View > Arrange Icons by; not Name) comes first, so arranged by Type a folder shows
"File Folder". The tile is sized as XP's list view sizes it (the icon and twenty 'm's of text wide, 56 px high in Tahoma 8 pt);
a selected tile's text block is filled with the highlight colour and its icon blended with it. A program's version details and
a song's artist and album, which XP's tiles show, are not read: those files show type and size. The View menu, the list's
context menu, the Views drop-down, Folder Options' default view and the XP-look file dialog's View Menu (Tiles, Icons, List,
Details) list it; a folder shown in Tiles keeps it (`tiles` in `flxp-views`, which the Longhorn frame reads as its Tiles).

## The Explorer's details and task band (flxp)

What the Explorer says about the folder shown or the selection comes from one model (`flxp/shell/details.h`) shown two ways.
Per kind of item (file, picture, song, folder, drive, a virtual folder such as My Computer) it lists properties from a small
registry: the derived Type, Attributes, Date Modified, Size, Dimensions, File System, Free Space, Total Size, and the stored
Description (`user.lux.desc`), Tags (`user.xdg.tags`) and Comment (`user.xdg.comment`), the extended attributes of
`docs/ANNOTATIONS.md`, read-only for now (set them with `setfattr` or luxfs's `x`).

**XP frame: the Details section.** The task pane's last section, under Places and Bookmarks, is XP's Details rollout. With nothing
selected it describes the folder itself (its name in bold, "File Folder", Date Modified; My Computer "System Folder"; a drive "Local
Disk", File System, Free Space, Total Size); with one item its name, type and its kind's lines in XP's order and wording ("Date
Modified: Today, October 31, 2026, 12:45 PM", "Size: 23.1 KB", "Dimensions: 800 x 600", "Attributes: Read-only, Hidden"), the
extended attributes when they are set; a picture gets its thumbnail above the name (at most 150 x 100), except in the Thumbnails
view and where thumbnails are off (`thumbnails = 0`, or a network mount without `thumbnails_network = 1`). Several items: "N items
selected." and the files' "Total File Size" (for up to 100 items). Like XP the section starts folded except in My Computer; its
chevron folds and opens it, and from then on it stays as it was left (`places_details = 0|1` in `flxp.ini`).

**Longhorn frame: the task band** (View > Task Band, `taskpane = 1` or `--taskpane`). Build 4051's band under the command bar:
left to right the preview, the details and the tasks. Nothing selected: the folder's image faded at the left with "N items / in this
folder" over it, and the folder's tasks (Make a new folder, Search, Open a terminal here, Add to Bookmarks, folder Properties) in
columns, "more" opening the rest of the folder's menu. One item: its thumbnail in a white frame (a picture) or its 128 px icon,
hanging a little below the band over the left pane, then its name, its values in two columns (size, date, type; a picture's size,
date and dimensions) with "Add a description", "Add tags", "Add comments" where those attributes are empty, and "all properties..."
(the Properties dialog). Several items: a stack of up to seven thumbnails, the focused one in front, "N items selected" and each
value shared by all or "(multiple values)". The item tasks (Open, Rename, Copy, Delete, Properties) follow when 200 px are left
right of the details. View > Preview switches between the expanded band (126 rows, its bottom edge drags it up to 246) and the
compact one (48 rows: the count or the name on one line, a small icon or thumbnail, the tasks side by side); dragging the edge
above 87 rows expands it, below compacts it (`taskpane_compact`, `taskpane_height` in `flxp.ini`). Thumbnails follow the same
`thumbnails` and `thumbnails_network` settings as the views; without them the icon shows. The theme puts the band at the top
or the bottom (`Explorer.taskpane`): the 5048 and 5112 themes have it under the list and the left pane, across the window's width,
which comes close to Vista's details pane; there it is the same band mirrored, the picture hanging above it over the left pane
(whose view button moves up out of its way) and its top edge dragging it.

## Start menu, taskbar autohide, sidebar modes (luxshell M6, `SHELL-DESIGN.md` 6.3/6.4)

**Start menu.** The Start button and the Super key (luxwm sends the root client message `_LHSHELL_START` when Super is pressed
and released alone; the Mod4 bindings keep working, unbound Super+key combinations are swallowed while Super is held; `Super_L
none` in `~/.config/lhde/keys` turns it off) open flxp's XP start panel, drawn from the visual style's StartPanel parts: with the
Avalon taskbar luxshell runs `flxp-startmenu` (flxp-taskbar's panel without the bar) and forwards both to it, with the classic
bar flxp-taskbar answers itself. See "Start menu: flxp's start panel everywhere" below. Longhorn's Avalon shell never had a start
menu of its own, so luxshell draws none.

**Context menus** (2026-10-26; Longhorn 4074's Avalon menus, sources in `SHELL-DESIGN.md` "Context menus"). All three are drawn
in the 4074 Avalon menu look (white, 1 px grey frame with a drop shadow, 19 px rows, the theme's highlight: Slate's tan
`#EDB078`, Jade's light blue) and open where you click, flipped to the left / upwards where they would not fit, as Windows does.
- **Sidebar menu**: right-click an empty part of the sidebar, or click the *Options* glyph (▾) at its top right (the menu opens
  under it). *Minimize bar* (the sidebar into the taskbar; *Restore bar* when minimized), *Properties* (the settings flyout,
  next to the sidebar), then lux's own *Lock the Sidebar* (`[sidebar] locked = 1`: the width sizer no longer drags) and
  *Stacking* ▸ *Top (above the windows)* / *Bottom (part of the desktop)* (the same as Super+s); then 4074's parts list: every
  module 4074 had, checked when it is in the sidebar. Ours are *Classic Tray* (the notification area tile), *Notification
  History*, *Quick Launch*, *Power (Transient)* and *Clock*: clicking one adds or removes that tile (remembered in
  `~/.local/state/lhde/luxshell-state.ini [sidebar] tiles`); the others (Basket, Contacts, News Feed, People nearby, Search,
  Slide Show, Sync, Web Host Tile) are greyed: not implemented. The parts list works in the `full` mode (greyed in `pinned`).
- **Tile menu**: right-click a tile; a tile without a flyout (the tray tile) also opens it with a click on its arrow at the
  top right. First the tile's own items (clock: *Clock layout* ▸, *Pinned to the bottom (4074)*), then 4074's: *Auto Size*
  (checked and greyed: our tiles always size to their content), *Move Up* / *Move Down* (greyed at the ends of the list and for
  pinned tiles), *&lt;Name&gt; Tile Properties* (clock: a flyout with the layouts and the pinned switch; power: the power flyout;
  tray: greyed, it has none), *Remove &lt;Name&gt; Tile* (not on pinned tiles, as in 4074; the sidebar menu brings it back).
- **Taskbar menu** (Avalon taskbar): right-click an empty part. 4074's items: *Minimize bar* / *Restore bar* (the sidebar),
  *Properties* (the settings flyout: theme list, Save as preset..., composition, taskbar kind and autohide, start menu, sidebar
  mode and stacking, wallpaper), *Task Manager* (the first of xfce4-taskmanager, lxtask, gnome-system-monitor,
  mate-system-monitor, qps, else `xterm -e top`), *Show Desktop* / *Hide Desktop* (`_NET_SHOWING_DESKTOP`), *Cascade*, *Tile
  Vertically* (side by side), *Tile Horizontally* (one above the other; more than three windows go into a grid; greyed with no
  windows); then lux's *Auto-hide the taskbar* and *Sidebar* ▸ *Full* / *Minimized* / *Pinned tiles only*.
- **Keys** while a menu is open: Up/Down (Home/End) move, Right or Return opens a submenu, Left or Esc closes a submenu, Esc
  closes the menu, Return/space activates. The underlined letter (mnemonic) activates its item directly (opens its submenu);
  when several items share it, the key cycles through them; a letter that is nobody's mnemonic jumps to the next item starting
  with it. A click outside any of our surfaces (or on another lux surface) closes the menu. The 4074 menus drew no underlines;
  ours always do.

**Autohide** (Avalon behaviour): the bar slides out (100 ms) as soon as the pointer is off it and no start menu, menu or
flyout of the shell is open, leaving a 2 px sliver; touching the bottom edge brings it back. The strut is the sliver, so
maximised windows use the full height. **Sidebar modes:** `full` (today's sidebar), `minimized` (the sidebar slides away; the
tray icons and the clock move into the taskbar's right end next to the toggle button, the taskbar runs the full width; the
round toggle button at the right end flips full/pinned ⇄ minimized, as in 4074), `pinned` (only the bottom block with the
tray and the clock, glass behind it; it reserves a bottom strut of its height over the sidebar's columns, so maximised
windows get the full width above it). `pinned` + autohide is the minimal mode: only the clock and the tray icons stay.
Mode changes animate in 400 ms; `_NET_WORKAREA` follows (1280x800 lab: full 1130x770, minimized 1280x770, pinned 1280x662,
autohide makes the 770s 798; these are the `top` numbers). **Stacking** (`[sidebar] stacking`, Super+s, settings flyout
*Sidebar*): the sidebar takes a lot of room, so it can live **under the windows like part of the desktop** and come **on top**
with one key when you want to look at it.
- `bottom` ("Sidebar under windows (desktop)"): the sidebar window carries `_NET_WM_STATE_BELOW` (luxwm keeps BELOW docks under
  all clients) and reserves **no strut**, so windows dragged over it cover it and maximised windows take the full width
  (`_NET_WORKAREA` 1280x770 on the lab, the taskbar's strut only). The Avalon taskbar runs the full width over it, above it in the
  stack, and the sidebar ends at the taskbar's top (the 4015/M4 look; it follows autohide). Pinned: the block sits above the
  full-width taskbar, under the windows, no strut. Minimized: as always (no sidebar, the tiles in the taskbar). Since the
  sidebar's clock can be covered by windows, the taskbar shows the time at its right end as well (the clock tile's taskbar
  view, as 4074's taskbar shows it when the sidebar has no clock on screen).
- `top` (default, "Sidebar on top of windows"): the sidebar is above the windows (`_NET_WM_STATE_ABOVE`, like the taskbar),
  runs the full height and is above the taskbar at the corner, which is shortened by its width (4074); its strut keeps maximised
  windows clear (full 1099x770 with a 181 px sidebar, pinned 1280x662).
- **Super+s** toggles between the two **instantly**: no animation or fade; the sidebar's window state, strut, geometry and place
  in the stack and the taskbar's width change in one callback and one repaint. Windows are not moved; luxwm refits maximised
  windows to the new work area as for any strut change (in the same composited frame on the lab). The key is luxwm's default
  `Mod4+s shell sidebar-stacking toggle` (added to a user keys file where Mod4+s is unbound); lux takes
  `sidebar-stacking toggle|top|bottom` on `_LHSHELL_COMMAND` (e.g. `xprop -root -f _LHSHELL_COMMAND 8u -set _LHSHELL_COMMAND
  "sidebar-stacking top"`), saves `[sidebar] stacking` and applies it; a change by another writer (`_LH_SETTINGS_CHANGED`) applies
  the same way.
- With the classic taskbar and `[sidebar] with_classic = 1` the sidebar still ends above the classic bar, under or over the
  windows by the same rule (`bottom`: below, no strut; `top`: above, with its strut).
- The maximised-mode black layers are unchanged (in `bottom` the maximised window simply covers the sidebar).
- `[sidebar] layer` is gone (2026-10-25): `layer = below` meant the sidebar under the windows, which is now `stacking = bottom`.
  lux folds an old key into `stacking` (`below` → `bottom`, `above` dropped) and removes it from settings.ini, at start and when
  another writer brings it back.

**Resizing the bars.** The sidebar's left edge is its `SidebarSizer` (2 px in Aero, where it is invisible over the glass
edge; 6 px in Slate, the light edge you see): the pointer turns into a left-right arrow, drag it to make the sidebar 100 to
600 px wide (Longhorn's `Sidebar\Width` limits; default 150). The taskbar's top edge (3 px, up-down arrow) sets its rows: drag
up for 2 or 3 rows (30 px each), down for fewer. Both resize live: the struts and `_NET_WORKAREA` follow (maximised windows
refit), the taskbar yields to the sidebar, tiles reflow; the new size is saved when you let go.

**Clock tile.** Right-click the clock (or any tile) for its menu (see "Context menus" above); *Clock Tile Properties* shows the
same choices in a flyout. *Clock layout* picks one of the 4074 views: Analog and time
(the default), Analog and digital time, Digital time and date, Digital time, weekday and date, Small clock and calendar, Calendar
(a month grid). *Pinned to the bottom (4074)* switches between 4074's pinned clock at the bottom of the bar (80 px face) and
4051's clock as an ordinary tile in the list (96 px face, larger time line); unpinned, *Move Up* / *Move Down* place it (and
any other tile) in the list, which is remembered. Each pack has its default (pinned in the 4074 packs, a tile in `slate4051`);
your choice holds for the pack you made it in. Unpinned, the face follows the bar width as in 4051 (the tile's width less 32,
up to the native 260 px) and the tile grows and shrinks with it; pinned, it stays 80 px at any width. The face is 4074's
layered clock (face, hands, hub and glass images from imageres.dll on their original pivots); the second hand ticks once a
second. It follows the theme's visual style: the light grey set under Slate (and Luna, Classic and the other non-Longhorn
styles), the dark-blue set that 4074 drew with the DWM under Jade and Aero (jade4074, aero4074, aerovision, the 5048/5112
Aero and Basic themes), and under a Plex style the light set of the Plex builds (4042's art when it is imported, else the same
4074 images). A pack can override it with the key `clock_face = "light"`, `"dwm"` or `"plex"`.

**Calendar flyout.** A click on the clock opens 4074's clock flyout: the clock at full size with the digital time under it,
and the month calendar with that month's photo, today on a tan box, the days of the neighbouring months in grey and the week
starting on the locale's first weekday. Point at the calendar and the ◀ ▶ buttons fade in; they step a month (another
year shows in the title). It opens at the clock and covers the bar's bottom, as in 4074, and closes on Escape, a click outside
it, or another click on the clock; it always opens on today's month. The tile's time line and the weekday and month names
follow `LC_TIME` (24-hour time where the locale has it); the flyout's large time is 4074's fixed `h:mm AM/PM`.

**Compositor rule.** The compositor follows the theme: switching to a theme with `composition = off` (Slate, Luna,
jade4074-classic) stops the picom of this display (only a process named `picom` whose environment has the same `DISPLAY`), and
switching to an `auto` theme (aero4074, jade4074) starts one if no compositor runs. luxwm already follows the compositor (glass
frames with it, the theme's classic frames without), so the whole desktop changes look together. The settings flyout's
*Composition* group overrides this: *Always on* / *Always off*; *Follow the theme* is the default. The switch is the theme key
`Glass.composition` like the taskbar (see "Per-theme taskbar, composition" below). Since 2026-10-25 the rule is applied **at startup too**, also for *Follow*: 1.5 s after
lux starts it stops the picom of its display when the theme says off (a session starting on luna or slate4074 no longer keeps
glass) and starts one for a glass theme when none runs (log `lux: compositor at startup: ...`). `lhde-session` (and so
`lhde-session`) already decides with the same rule whether to start picom at all (`lhcomp_wanted` in `lhwall.sh`), so a
classic session shows no flash of glass; `LAB_PICOM=0` / `LAB_PICOM=1` force it off / on for the whole session, lux included
(`LHSHELL_COMPOSITION=off|on|follow` forces lux for one run; the flyout can still change it). A picom you start by hand
(`lhde-session picom on`) stays until the next theme change or lux start. The picom
command is `[commands] picom` in settings.ini, else `$LHSHELL_PICOM_CMD`, else `picom --config session/picom-glass.conf --backend
glx`; on the Xvfb lab, which needs xrender, start luxshell with
`LHSHELL_PICOM_CMD="picom --config /u/aap/fun/longhorn/session/picom-glass.conf --backend xrender --blur-method gaussian --blur-size 5 --blur-deviation 1.2"`.
picom's output goes to `$XDG_RUNTIME_DIR/lhshell-picom<display>.log`.

**Classic taskbar instead of the Avalon one** (SHELL-DESIGN "M8 luxshell side"). The taskbar kind, the theme key `Shell.taskbar` (below), picks the
bottom bar: `avalon` (default, luxshell's taskbar), `classic` (flxp's XP-style `flxp-taskbar`, which luxshell starts and stops)
or `none`. Switch live from the settings flyout (*Taskbar*: Longhorn (Avalon) taskbar / Classic taskbar (flxp) / No taskbar) or
from a terminal with `lhde-session taskbar avalon|classic|classic+sidebar|none` (the root command `_LHSHELL_COMMAND` =
`taskbar KIND`, plus `sidebar-with-classic 0|1` for `classic` / `classic+sidebar`, to the running luxshell, which saves it for the
current theme's current layer; with no lux on the display the script sets the theme key itself, `lhde-theme set-key`). The settings flyout can also be opened from a terminal (the
classic bar has no way to it): `xprop -root -f _LHSHELL_COMMAND 8u -set _LHSHELL_COMMAND settings`. In `classic`:
- luxshell has no taskbar surface, no taskbar strut and no `_NET_WM_ICON_GEOMETRY` (flxp-taskbar sets its own); it runs
  `[commands] taskbar` (default `flxp-taskbar`, from PATH; `$LHSHELL_TASKBAR_CMD`) with its own DISPLAY and D-Bus session, log in
  `$XDG_RUNTIME_DIR/lhshell-taskbar<display>.log`, and stops exactly that process (its pid, checked against the display) when you
  switch away or luxshell exits. flxp-taskbar follows the session theme by itself (`_LH_THEME`).
- No sidebar (default, `[sidebar] with_classic = 0`): the sidebar is unmapped with no strut, so windows get the full width;
  the tray and the clock are flxp-taskbar's (`_LHSHELL_CLOCK_VISIBLE` = 0), toasts sit above the classic bar at the right edge,
  Super still works. The sidebar's mode (full / minimized / pinned) is kept, can still be changed in the settings flyout, and
  comes back with the Avalon taskbar.
- `[sidebar] with_classic = 1` (settings flyout *Sidebar*: Also with the classic taskbar; `lhde-session taskbar
  classic+sidebar`): the 4051 layout, the sidebar ends above the classic bar (the bar runs the full width, the sidebar sits on
  it): it reads other docks' bottom struts and follows them live (the bar appearing, going away, changing height).
  `[sidebar] yield_bottom = 0` turns that off. In `avalon` nothing changes (the Avalon taskbar still stops at the sidebar).
- Tray: luxshell hands its tray icons to flxp-taskbar (one tray per display) and hides its notifications tile; when the
  classic bar goes away the tile takes the tray back and the icons return. Any other program that owns
  `_NET_SYSTEM_TRAY_S0` (stalonetray, a panel) is treated the same way.
- Clock: luxshell publishes the root property `_LHSHELL_CLOCK_VISIBLE` = 1 while a clock tile of its own is on screen, 0
  otherwise (sidebar minimized, clock tile closed); flxp-taskbar hides its clock while it is 1.
- Start menu: see "Start menu: flxp's start panel everywhere" below; in `classic` the classic Start button and Super open
  flxp-taskbar's XP start panel (it is the start-menu provider).
`LHSHELL_TASKBAR=avalon|classic|none` overrides the setting for one luxshell run (not saved).

## Live-editing a theme (`theme-reload.sh`, `theme-watch.sh`)

Edit a theme and see it at once, without restarting lux or luxwm:
- `session/theme-reload.sh [DISPLAY]` (default `$DISPLAY`, else `:1`) reloads both: lux gets `_LHSHELL_COMMAND reload`, luxwm
  `luxwmc reload-theme` (`build/bin/luxwmc` when luxwmc is not on `PATH`), or, when luxwmc is missing or does not know that command,
  the root ClientMessage `_LHWM_RELOAD` (sent with python3-xlib). A missing luxwm side is reported, not an error.
- `session/theme-watch.sh [DISPLAY]` watches `themes.ini`, `wallpapers.ini`, `data/frames/*/theme.ini`, `data/frames/*/nav.ini` (luxnav's
  tuning; luxnav clients such as `examples/navdemo` re-read it when luxwm announces the reload), `data/packs/*/shell.lua`
  and `luxshell/tiles/*.lua` and runs `theme-reload.sh` after every save (a burst of writes within 0.3 s counts once). It uses
  `inotifywait` (inotify-tools) when installed, watching the directories so editors that replace the file are seen; otherwise
  it polls the files' mtime and size every second. Ctrl-C stops it. Example: `session/theme-watch.sh :1 &`, then edit
  `data/packs/4074-slate/shell.lua`.

What lux's reload does (`runtime.lua` `lh.reload_pack`): it resolves the current theme again in `themes.ini` (or the preset),
reads the pack's `shell.lua` again even when the directory did not change, applies the theme's start panel
(`themes/startpanels.lua`) and fonts again, drops the cached PNGs whose file changed (mtime or size, so an edited imageres
bitmap is read again), and rebuilds the taskbar, sidebar, tiles (`tiles/*.lua` are read again), flyout chrome, start menu and
menus in place on the same windows; the wallpaper (`wallpapers.ini`), the composition rule and the taskbar kind follow the
theme entry as on a theme switch. The log says `lux: reload (...): theme T, pack P, N image(s) re-read, M ms` (about 40 ms on
the lab). A `shell.lua` with an error keeps the running pack (the error is logged). Setting `_LH_THEME` to the name it
already has (`session/lab.sh theme NAME` again, Display Properties' Apply with the same theme) is a reload too. Several requests
within 150 ms make one reload. The Lua code of lux itself (`runtime.lua`, `menu.lua`, ...) is not reloaded: restart lux for that.

What luxwm's reload does (`luxwmc reload-theme`, the root ClientMessage or property `_LHWM_RELOAD`, or `_LH_THEME` set to the name it
already has): it looks the current `_LH_THEME` up in `themes.ini` (or the preset) again and loads the frame theme anew from disk,
even though the spec did not change: the theme directory's `theme.ini` and PNGs, or the `.msstyles` through libuxtheme (so a
number edited in `data/frames/jade4074/theme.ini`, e.g. `margins = 27 9 9 9`, or a re-saved style shows at once). Every frame is
re-placed around its client with the new margins (the client stays where it is, `_NET_FRAME_EXTENTS` follows), repainted, and
`_LHWM_FRAME_METRICS` / `_LHWM_COMPOSITED` are announced again; the log says `luxwm: theme reloaded (...)`. A theme that no longer
loads (a broken `theme.ini`) keeps the running one. luxwm's C code and `config.h` are not reloaded (`session/lab.sh restart-wm`).

## Settings and presets (lux, SETTINGS.md)

**Where.** lux keeps its settings in the desktop-wide `$XDG_CONFIG_HOME/lhde/settings.ini` (default
`~/.config/lhde/settings.ini`, `LHDE_SETTINGS` for another file), which luxwm and the flxp programs share (SETTINGS.md). lux
writes every change back in place (other programs' keys, comments and order are kept) and then sends the root ClientMessage
`_LH_SETTINGS_CHANGED`; when another program (flxp's Display Properties, a script) changes the file and sends that message, lux
reads it again and applies what changed, live. A hand edit is picked up by sending it yourself, e.g.
`python3 -c 'from Xlib import display,protocol,X; d=display.Display(); r=d.screen().root; r.send_event(protocol.event.ClientMessage(window=r, client_type=d.intern_atom("_LH_SETTINGS_CHANGED"), data=(32,[1,0,0,0,0])), event_mask=X.SubstructureNotifyMask); d.flush()'`,
or by restarting lux. `~/.local/state/lhde/luxshell-state.ini` keeps only lux's runtime state (the tile order; launch counts are in
`mfu.ini`).

**Migration.** The first lux of this version moves the user-facing keys of `~/.local/state/lhde/luxshell-state.ini` to `settings.ini`
(a key `settings.ini` already has is not overwritten), removes them from state.ini and keeps the untouched original as
`state.ini.pre-settings`. Renamed on the way: `[shell] theme` -> `[theme] name`, `[shell] composition` -> `[theme] composition`,
`[taskbar] enabled` -> `[taskbar] kind`, `[shell] wallpaper = 0` -> `[desktop] wallpaper = none`, `[shell] desktop_black` ->
`[desktop] desktop_black`; everything else keeps its section and name. It happens once (afterwards state.ini has none of those
keys); an older luxshell still running elsewhere would write them back to state.ini, and the next new lux moves only what
settings.ini lacks.

**Presets** ("your themes"): `~/.config/longhorn/themes/NAME.theme` is a theme of its own, a registry theme (`base`) with some
keys replaced (format in SETTINGS.md). They are listed under "Your themes" in the settings flyout (start menu Control Panel,
taskbar or sidebar menu Properties, or `xprop -root -f _LH_THEME 8u -set _LH_THEME NAME`). *Save as preset...* in the flyout asks for a
name and writes the current look (style, composition, wallpaper and mode, shell pack, start panel, fonts) as a preset whose
`base` is the registry theme in use, then selects it. Selecting a preset: lux applies the pack, start panel, fonts, wallpaper
and compositor rule from it, writes `[theme] name = NAME`, publishes the resolved theme as the root property
`_LH_THEME_RESOLVED` (SETTINGS.md) and sets `_LH_THEME` to the preset's **name** (since 2026-10-25: luxwm and flxp resolve
preset names themselves, registry first, then `themes/NAME.theme`; luxwm's frames come from the base entry plus the preset's
`[VisualStyle]`, `[Frames]`, `[Fonts]` and `[Composition] composition` (`off` = the classic frame), its animations from
`[Composition] animations`; log line `luxwm: theme NAME: preset (base BASE) from ...`). Which value wins:
the current layer (`settings.ini [current]`), then the theme file and its base chain, then the style's defaults;
`[desktop] wallpaper = none` (the flyout's "Show the wallpaper" off) always wins. A name that is neither a theme nor a preset
is ignored by everybody (the current theme stays; luxwm and lux log it).

**Display Properties (flxp-settings).** flxp's dialog (`~/src/flxp/build/settings/flxp-settings [--tab
themes|desktop|appearance|effects]`, or `flxp --settings`; also flxp's start panel Control Panel > Display Properties...) is the
full settings UI on the same file: Themes (registry themes and presets, Save As..., Delete), Desktop (background, position, icon
set), Appearance (explorer frame, taskbar kind, start menu, composition, animations), Effects (font smoothing). Apply writes
settings.ini, sets `_LH_THEME` for a theme change and sends one `_LH_SETTINGS_CHANGED`; lux applies the theme, composition
(starts/stops picom), taskbar kind, start menu and wallpaper live, luxwm the frames and animations. Checked on the lab with luxwm +
lux + picom (`lhshell/compare_settings_sync.png`): Jade, composition off, animations invented, classic taskbar, a saved
preset `test1` (`_LH_THEME = test1`, luxwm on the base's frames, lux on the base's pack, no rewrite), and back.

**Per-theme taskbar, composition.** The taskbar kind, the compositor switch, the icon set and the animations are theme keys
(`Shell.taskbar`, `Glass.composition`, `Desktop.icons`, `Glass.animations`; docs/THEME-FORMAT.md): a theme file says them, its
base chain or the style's defaults fill them in. Display Properties' Appearance tab and lux's settings flyout (and
`_LHSHELL_COMMAND taskbar KIND`) change them for the current theme only, into its current layer (`settings.ini [current]`,
"NAME (Modified)"); a value equal to what the theme gives anyway is dropped; Save As keeps it in a theme file of your own. So
aero4074 can have the classic bar while jade4074 keeps the Avalon one. lux applies the record at startup, on every theme change
and on `_LH_SETTINGS_CHANGED` (log `luxshell: taskbar A -> B`). The `[override:NAME]` sections of an older settings.ini are
turned into `NAME-modified.theme` by the migration (docs/SETTINGS.md "Migration"); nothing reads them live.

**Start menu: flxp's start panel everywhere.** flxp-taskbar's XP start panel is the start menu in every taskbar mode. With the
Avalon taskbar or none, lux runs `flxp-startmenu` (the same program without the bar, tray or struts; log
`$XDG_RUNTIME_DIR/lux-startmenu<display>.log`): it owns `_LH_STARTMENU_S0` and lux forwards the Avalon Start button and Super to
it (the panel opens above the Start button; Super or a click outside closes it). lux adopts one that already runs on the display
(also a `flxp-taskbar --start-menu-only` run by hand), restarts it when it dies (at most 5 times a minute, then nothing opens
until the settings change), stops it when you switch to the classic bar (which is the provider then) and on exit, always by its
own pid checked against the display. flxp's panel has a Control Panel cascade whose *Display Properties...* starts
flxp-settings; lux's settings flyout is reachable from a terminal with `xprop -root -f _LHSHELL_COMMAND 8u -set _LHSHELL_COMMAND settings`.
The Avalon Start button shows its pressed image while flxp's panel is open: the provider sets the root property
`_LH_STARTMENU_OPEN` (1 open, 0 or deleted closed; `docs/CLIENT-PROTOCOL.md`); with a flxp-taskbar that does not set it yet lux
keeps the button pressed from the click (or Super) until the panel's window unmaps, or until the next click on the button.

`settings.ini` keys lux uses (it writes the defaults of the marked ones when missing):

| Section | Key | Values |
| --- | --- | --- |
| `[theme]` | `name` | the session theme: a themes.ini name or a preset (mirrors `_LH_THEME` / `_LH_THEME_RESOLVED`) |
| `[theme]` | `composition` * | `follow` (default: the theme's `composition`, at startup and on a theme change), `on`, `off` (always); the fallback for themes without `[override:NAME] composition` |
| `[theme]` | `animations` | luxwm's animation set (`4074plus`, `4074`, `invented`, `5048`, `none`; read by luxwm, published by lux in `_LH_THEME_RESOLVED`) |
| `[desktop]` | `wallpaper` | empty (default: the theme's, from `data/wallpapers.ini`), `none` (solid #3a6ea5; `LHSHELL_WALLPAPER=0` forces it), or an image path |
| `[desktop]` | `mode` * | `fill` (default), `fit`, `center`, `tile`, `stretch` (feh `--bg-fill`, `--bg-max`, `--bg-center`, `--bg-tile`, `--bg-scale`) |
| `[desktop]` | `desktop_black` * | `0` (default, M5b): a maximised window darkens only its own frame (luxwm) and the bars' black layers; `1`: the whole desktop also turns black (M5a) |
| `[shell]` | `max_fade_ms` * | `0` (default): the bars' black layers switch at once when a window is maximised or restored; a number: the old fade in ms (4074: 400) |
| `[taskbar]` | `kind` * | `avalon` (default), `classic` (flxp-taskbar instead, started/stopped by lux) or `none`; the fallback for themes with neither `[override:NAME] taskbar` nor a themes.ini `taskbar` |
| `[taskbar]` | `classic_start_menu` | whose menu the classic bar's Start button opens: `flxp` (its XP start panel, default) or `flxp-classic` (its classic popup menu). Was: from `[start] menu`: `flxp` -> `flxp`, `lux` -> `luxshell` |
| `[taskbar]` | `autohide` * | `0` (default) or `1` |
| `[taskbar]` | `rows` * | 1 (default), 2 or 3: the top-edge drag writes it |
| `[sidebar]` | `mode` * | `full` (default), `minimized`, `pinned` |
| `[sidebar]` | `width` * | 100..600 px (default 150): the SidebarSizer drag writes it |
| `[sidebar]` | `stacking` * | `top` (default: the sidebar is above the windows with its strut, runs the full height, on top at the corner, the taskbar is shortened by its width; 4074) or `bottom` (part of the desktop: under the windows, `_NET_WM_STATE_BELOW`, no strut; the Avalon taskbar spans the full width over it and the sidebar, in every mode, ends at its top; follows taskbar autohide; M4). Super+s toggles it live; settings flyout *Sidebar*. Replaces `[sidebar] layer` (`below` = `bottom`) |
| `[sidebar]` | `with_classic` * | `0` (default: no sidebar while the classic taskbar runs; its mode returns with the Avalon one) or `1` (the sidebar stays, above the classic bar) |
| `[sidebar]` | `yield_bottom` | `auto` (default: the sidebar ends above another dock's bottom strut, e.g. the classic taskbar) or `0` |
| `[clock]` | `pinned`, `pinned_pack` | `1` (pinned at the bottom) or `0` (a tile in the list), chosen in the pack `pinned_pack` (the clock's menu writes both); other packs use their own default: pinned in aero/slate, a tile in slate4051 |
| `[clock]` | `layout` * | `default`, `digtime`, `digtimemonthday`, `digtimeweekday`, `minical`, `cal` |
| `[start]` | `menu` * | `flxp` (default: flxp-taskbar's start panel, see above) or `lux`; the fallback after `[override:NAME] start` and themes.ini `start` |
| `[override:NAME]` | `taskbar`, `start`, `composition`, `icons`, `animations`, `frame` | the per-theme values for theme NAME (written by Display Properties and lux's flyout; above). lux also reads `[override:ALIAS]` for a registry theme's aliases (`[override:aero4074]` for aero-patch) |
| `[override:NAME]` | `wallpaper`, `mode` | a wallpaper for theme NAME only (before `[desktop] wallpaper`; docs/history/THEMES.md "Wallpapers"); nothing writes it yet |
| `[start]` | `pinned` | `a.desktop;b.desktop` (default: the default web browser and mail client from `xdg-settings`/`xdg-mime`, else an installed one) |
| `[start]` | `mfu_count` | number of most-used programs (default 6) |
| `[start]` | `userpane` | `1` / `0` to force the user pane (`$USER` + `~/.face`) on or off (default: when the theme has one) |
| `[commands]` | `logoff`, `standby`, `shutdown`, `restart`, `lock` | session commands (defaults above) |
| `[commands]` | `taskbar` | the classic taskbar for `[taskbar] kind = classic` and, with `--start-menu-only`, the start-menu provider (default: `$LHSHELL_TASKBAR_CMD`, else `flxp-taskbar`) |
| `[commands]` | `picom` | the compositor command for the rule above (default: `$LHSHELL_PICOM_CMD`, else picom with `picom-glass.conf` and glx) |
| `[commands]` | `filemanager`, `run`, `search` | file manager for the places (default flxp, rox, else xdg-open); Run (default `rofi -show run` if installed, else lux's own run box); Search (default: lux's box, which starts the best-matching program) |

`~/.local/state/lhde/luxshell-state.ini` (`LHSHELL_STATE`): `[sidebar] tiles` = the tile order, `notifications;clock;power` (written by
Move Up / Move Down; default: the pack's list).

## Second pair of eyes: Codex vision (`tools/eyes.sh`)

`tools/eyes.sh REF.png OURS.png [notes]` attaches both images to a non-interactive, read-only
`codex exec` run and returns a numbered list of visual differences with pixel/colour estimates and
a closeness verdict. `tools/eyes.sh --one IMG.png [question]` describes a single screenshot.
Feed it 2x-scaled crops of the same region (`magick in.png -crop WxH+X+Y +repage -scale 200% out.png`),
not whole screens, and tell it what to ignore (wallpaper, which windows are open). Codex cannot edit
anything in this mode. `EYES_MODEL=<model>` picks the Codex model. Agents doing compare-and-adjust
loops should call it once per iteration instead of relying on their own eyes alone.

## Shell extras: toasts, tray icons, power (luxshell M4b)

**Toasts need a session bus of their own on the lab.** luxshell serves `org.freedesktop.Notifications` on the bus named by
`DBUS_SESSION_BUS_ADDRESS`. Your desktop's bus already has a notification daemon (dunst on `:0`); luxshell will not take the
name from it (it logs "owned by another notification daemon ... toasts off" and runs without toasts). Give the lab its own bus
and start luxshell and the clients with it:

    eval $(dbus-launch --sh-syntax)          # or: dbus-run-session -- sh, then start everything from that shell
    echo $DBUS_SESSION_BUS_ADDRESS > /tmp/lab-bus    # so other terminals can join: export DBUS_SESSION_BUS_ADDRESS=$(cat /tmp/lab-bus)
    DISPLAY=:3 LHSHELL_THEME=4074-aero setsid build/bin/luxshell &
    DISPLAY=:3 notify-send "Hello" "from Longhorn"
    DISPLAY=:3 notify-send -A ok=OK -A no=Cancel "Question" "Pick one"   # prints the chosen action

Without `DBUS_SESSION_BUS_ADDRESS` luxshell does not autolaunch a bus; toasts are simply off. Toasts appear above the taskbar,
left of the sidebar (Longhorn's placement); `toast_place = "sidebar"` in `themes/*/shell.lua` puts them over the sidebar's top.

**Tray icons** go into the sidebar's "Notifications" tile (XEmbed system tray, `_NET_SYSTEM_TRAY_S0`). Start tray apps after
luxshell, on the same display (most also want the same session bus): any XEmbed tray app, e.g. `DISPLAY=:3 blueman-applet &`
(installed here; needs bluetoothd and the system bus), `nm-applet`, `pasystray`, `volumeicon` if installed, or the test icon `make build/luxshell/tests/trayicon &&
DISPLAY=:3 build/luxshell/tests/trayicon '#3399ff' &` (an ARGB circle; click cycles its colour; a second argument makes it exit
after that many seconds). Apps that only speak StatusNotifierItem (D-Bus, KDE/Ayatana style, no XEmbed fallback) do not show
up. Stop luxshell with a plain `kill` (SIGTERM): it hands the icons back unmapped, and the apps re-dock when luxshell returns.

**Notification History tile** (on by default in the 4074 packs, as in 4074): every notification luxshell receives (except those
with the `transient` hint) is kept, newest 100, in `~/.local/state/lhde/luxshell-notifications` and survives a restart. The tile
is 4074's time line: the newest eight as speech bubbles along the bar, the newest at the right edge, older ones further left,
close ones piled in stacks of up to four; yellow = not seen yet, grey = seen (its toast was clicked or closed by you, or the
history flyout was opened). Point at a bubble for its time and text, click it to see the toast again. A click elsewhere on the
tile opens the flyout: the full list by day, newest first (time, bubble, title in orange, text), a click on a row shows the toast
again, the wheel scrolls, and *Delete All Notifications* at the bottom (also in the tile's and the flyout's right-click menu)
empties the history. An app that withdraws its notification (`CloseNotification`) takes it out of the history too. Minimized,
the bar shows the bell with the number of unseen notifications in the taskbar; click it for the flyout.

**Quick Launch tile** (default in the 4074 and 4051 packs): the `.desktop` files of `~/.config/lhde/quicklaunch/` (or the
directory in `settings.ini [quicklaunch] dir`) as 16 px icons in rows of five, in file-name order; click to start, point for the
name. On the first start luxshell creates the folder with copies of your browser, mail client, flxp and a terminal; add, remove
or rename files there (the tile follows within seconds; *Open Quick Launch folder* in the tile menu opens it). Minimized, the
icons sit in the taskbar.

**Power tile** reads `/sys/class/power_supply/BAT0` and `AC/online` every 30 s; click it for the details flyout.
`LHSHELL_POWER_SUPPLY=/some/dir` (with `BAT0/{status,capacity,...}` and `AC/online` files) fakes a battery for screenshots.
`LHSHELL_DEBUG=1` makes luxshell log events and paint times (CLOCK_MONOTONIC ms) to stderr.
