# Using lhde

Names: **luxwm** is the window manager; **luxshell** is the Lua/cairo Avalon-style shell (taskbar, sidebar, tiles, toasts,
context menus, settings flyout; its windows have the X class `luxshell`, its log lines start with `luxshell:`); **flxp** is the
Win32-style Explorer, classic taskbar with XP's start panel, desktop and Display Properties, built on the **wux** win32 layer.
Shared pieces: the theme files (docs/THEME-FORMAT.md), `settings.ini` (docs/SETTINGS.md), liblhde, which finds every file
through the three trees (docs/LAYOUT.md). Programs run uninstalled from `build/bin` (and `build/examples`), or from `$PREFIX/bin`.

## When something looks wrong: where does this pixel come from?
Start here. lhde keeps its files in three trees (docs/LAYOUT.md "The three trees"), and one command says which file every
visible part of a theme comes from:
```
lhde-theme explain                  the selected theme (with its unsaved changes), one row per component
lhde-theme explain aero5112         another theme
lhde-theme dirs                     the three trees with their sizes, and the search order
lhde-theme find style jade4074      one file by kind (theme style scheme frames pack navbar icons wallpaper fonts resources
lhde-theme find frames/jade4074-classic/caption_1.png        source), or by its relative name
lhde-theme resolve aero5112 --why   every theme key and where its value came from (file, base, current, default, fallback + reason)
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
the DLL). Logs: a session's are in `$XDG_RUNTIME_DIR/lhde-session<DISPLAY>/` (below), the lab's in `${TMPDIR:-/tmp}/lhdelab<DISPLAY>/`.

## Three ways to run it

| | Where it appears | Interactive | Command |
| --- | --- | --- | --- |
| headless | nowhere (Xvfb, `LAB=:2` by default), screenshots only: the test bench | no | `session/lab.sh start`, `session/lab.sh shell`, `session/lab.sh shot out.png` |
| nested | a 1280x800 Xephyr window on your desktop | yes, click into it | `lhde-session start --nested [THEME]` (the first free display from `:2`), or `LAB=:3 session/lab.sh start xephyr` |
| real | an X server of its own on `:1`, virtual terminal 8 | yes, full speed, GPU glass | log in on a console (Ctrl+Alt+F8) and run `lhde-session start [THEME]` |

Each display belongs to one of them: the real session to `:1`, the lab and the scripted tests to `:2` (`LAB=:N` picks another),
so neither restarts the other's window manager.

**The lab** (`session/lab.sh start [xephyr] | ensure | shell [on|off] | demo | shot [FILE] | theme NAME | restart-wm |
restart-picom | stop`): luxwm (`LAB_WM=fvwm` for an fvwm2 session with `lab.fvwm2rc`), picom with `session/picom-glass.conf`
(xrender backend with gaussian blur; `LAB_BACKEND=glx`), the mock wallpaper, and the theme `aero4074` (`LAB_THEME=NAME` another;
`lhde-theme list` names them). `LAB_PICOM=0` starts it without picom (`start`/`ensure`/`restart-picom` just say so); luxwm then
shows the theme's classic frame set. `LAB_WMARGS=""` starts luxwm without `-d` (its debug log). `shell` starts luxshell
(`LAB_SHELL_DESKTOP=0`: without its desktop layer), `demo` two terminals (`$LAB_TERM`, default xterm) and the glassdemo window.
`restart-wm` restarts luxwm from the build tree (with `LAB_THEME` set, that theme becomes the session's); `stop` ends every
process whose `DISPLAY` is the lab's, then its X server. `session/hovertest.sh [DIR]` scripts the caption buttons' hover and press
states on the lab and saves zooms.

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
   - `--profile museum`: every file of every source (archaeology, and the input from which essentials is made).
   ```
   tools/lhde-pack --isos DIR --out ~/packs                                   # essentials for the shipped themes, from the ISOs
   tools/lhde-pack --themes luna,jade4074,classic --isos DIR --out ~/packs    # essentials for three themes
   tools/lhde-pack --profile museum --isos DIR --out ~/packs                  # everything
   tools/lhde-pack --from-pack ~/packs/lhde-resources-DATE --out ~/packs      # re-profile a museum pack: no ISO is read (seconds)
   tools/lhde-pack --from-tree --out ~/packs                                  # the Windows tree you run on, local/ included
   tools/lhde-pack --from-tree --profile museum --out ~/packs                 # all of it
   tools/lhde-pack --isos DIR --sources 4074,5048,xp --profile museum         # a subset; --all adds 4074r2
   tools/lhde-pack --medium 4074=DIR/WINDOWS --sources 4074                   # an extracted Windows directory instead of the ISO
   tools/lhde-pack --list                                                     # the sources and whether their ISO was found
   ```
   `--from-pack` takes a pack directory or its tarball and reads nothing else; from ISOs, essentials makes the museum pack
   first (in `--tmp`, about 400 MB, deleted after) and profiles it, so both ways give the same pack. `lhde-theme explain --files`
   must be built (`make build/bin/lhde-theme`); it is also what you run to see what one theme reads:
   `lhde-theme explain --files luna` (one real path per line, relative to the Windows tree).
   **`local/`** (the files dropped in by hand: the Aero reconstruction `aero-patch.msstyles` that `aero4074` and `aerovision`
   use, other styles from `lhde-import --style`, hand-copied schemes, resources, fonts, wallpapers, icon packs) is a source of
   its own in both profiles: the pack's `local/` mirrors the Windows tree's `local/` (default the tree's own, `$LHDE_WINDOWS`;
   `--local DIR` another, `--no-local` none; with `--from-pack` the tree's only when the pack has no `local/`), its MANIFEST lines
   have the source `local` and the in-image path `local:<path>`. museum takes all of it; essentials what a theme's
   `explain --files` names (a style of `local/` is named as `local/themes/NAME.msstyles`). `--from-tree [DIR]` takes the files
   from a Windows tree instead of the media: its `<source>/` directories are packed as they are (the MANIFEST's in-image path is
   `tree:<path>`, the tree does not record the image's), so a pack made from the tree a desktop runs on reproduces its themes.
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
   Sizes (the tarball as `tar` + `xz -9e`): **museum** 418 MB raw (377 MB stored once), 57 MB compressed; **essentials** for the
   14 shipped Windows themes 169 MB raw (164 MB stored once), 36 MB compressed, in 5 builds (4051 4074 5048 5112 xp) and the 2k
   icon DLLs. About 20 s from the ISOs, 10 s `--from-pack` (the tarball's xz is most of it).
   Themes that need a file no medium has (`aero4074`, `aerovision`: `aero-patch.msstyles`) are complete in a pack made where
   that file is in the Windows tree's `local/` (above); otherwise they report it missing in `lhde-theme explain`, and the style
   goes in with `lhde-import --style` (below).
2. **`lhde-import`** (every user): the pack into the Windows tree (`$LHDE_WINDOWS`, else `~/.local/share/lhde-windows`;
   docs/LAYOUT.md "The three trees"):
   ```
   lhde-import --pack lhde-resources-DATE.tar.xz               # or the directory
   lhde-import --style ~/aero.msstyles aero-patch              # a style dropped in by hand (the 4074 Aero patch: aero4074, aerovision);
                                                               # NAME is the style's name for the themes; without it the file's stem
                                                               # is used and printed, and a stem that is a source's style is refused
   lhde-import --style ~/styles/Royale/Royale.msstyles royale  # an XP style with its Shell/ directory beside the file: both go in
   lhde-import --icons DIR NAME [--licence FILE]               # a free icon pack ("Icon packs" below)
   lhde-import --regenerate                                    # delete generated/ and make it again from the imported files
   lhde-import --reindex                                       # make index/ again (after moving files by hand)
   lhde-import --list
   lhde-import --clean [--yes]                                 # delete the Windows tree (asks first; --yes: does not)
   ```
   Each `<source>/` of the pack is copied as it is to `~/.local/share/lhde-windows/<source>/` (`system32/` with shell32,
   imageres, explorer, browseui, comctl32, themeui, shellstyle, uDWM; `themes/` = the build's Resources/Themes; `fonts/`,
   `wallpapers/`, `source.ini`); a style dropped in by hand goes to `local/themes/` (its `Shell/` directory, the task pane's
   shellstyle.dll per colour scheme, with it when it lies beside the file) and then shows on Display Properties' Appearance tab
   ("Windows and buttons") beside the styles the shipped themes use; choosing it there gives "THEME (Modified)", and Save As
   on the Themes tab makes it a theme of its own. The pack's `local/` goes to the tree's `local/` (indexed as if dropped in by
   hand, and the recipes of its styles run: aero-patch's frame sets). `index/` holds the names the programs ask
   for, as relative symlinks: `styles/<name>.msstyles` (build-qualified: jade4074, slate4051, aero5112, luna = XP's own, whose
   `styles/luna/` is XP's `themes/Luna/` with `Shell/<Scheme>/shellstyle.dll`; luna4051 / luna4074 = Longhorn's copies),
   `resources/<source>`, `fonts/<source>`, `wallpapers/<source>`, `icons/<pack>`, `frames` and `packs` (-> `generated/`). Then the recipes of
   each source run: frame sets (CompositedWindow streams, `msstyles2theme.py`, 5048's uDWM bitmaps, `build_aero5112.py`,
   `build_aerovision.py`) and luxshell's pack images (imageres PNGs, shell32 icons, start panels, the 5112 taskbar) into
   `generated/frames/<set>/` and `generated/packs/<build>/`, and the aurora scenes as layer lists (`[aurora]`: Jade's
   PreviewPane.AURORA stream and Microsoft.Windows.WCPClient.dll's desktopaurora.baml, decoded by `tools/lhdebaml.py`) into
   `generated/aurora/<scene>-<source>.json` (indexed as `aurora/`), each with a manifest naming its inputs and their sha256.
   `make examples` builds examples/aurorademo from `aurora/previewpane-4074.json` and `aurora/desktop-4074.json`. The DLL
   is in the 4066, 4074, 4074r2 and 4093 recipes (a museum pack has it; an essentials pack does not, as no theme reads it); a
   tree imported before it was there gets it, and the desktop aurora, with `lhde-import --iso longhorn4074.iso` (files that
   are there are kept, the recipes run again).
   Developer paths through the same rules: `lhde-import --iso FILE`, `--wim FILE`, `--dir WINDOWS-DIR` detect the build from
   explorer.exe's version (6.5.40xx Longhorn, 6.0.5048/5112, 6.0.3790 Server 2003, 5.0 Windows 2000) and the layout, pack into a
   temporary pack and import it (`--source ID` skips the detection). `--from-workspace DIR` copies aap's research workspace
   (`~/fun/longhorn`; the demo wallpapers of aero4074 and aerovision come only from there; they land in `local/`).
3. **Classic schemes** (Display Properties > Appearance > "Windows Classic style": Windows Standard, Windows Classic, Brick, ...,
   High Contrast; docs/THEME-FORMAT.md section 2a): the `xp` and `2k` sources carry the medium's `HIVEDEF.INF` (the default
   user hive's setup data) and lhde-import turns its `Control Panel\Appearance\Schemes` values into
   `generated/schemes/<scheme>.scheme` (looked up as `styles/<scheme>.scheme`). The stand-alone importer reads them straight
   from an ISO (7z) or an extracted `HIVEDEF.INF` into `local/schemes/`:
   ```
   tools/lhde-schemes "Windows Server 2003 R2 Enterprise Corp x64 CD1.iso"   # -> ~/.local/share/lhde-windows/local/schemes
   tools/lhde-schemes --windows /tmp/scratch-windows --force I386/HIVEDEF.INF
   ```
   22 schemes (XP, Server 2003 and Windows 2000 have the same ones), the "(large)" / "(extra large)" variants as the font sizes of
   their scheme. Without any, "Windows Standard" is the built-in classic colours.

### Icon packs
An icon recipe (`data/icons/NAME.theme`, chosen by a theme's `Desktop.icons`) maps flxp's icon keys to sources: `pe:FILE#ID`
(an icon in an imported DLL) or `file:PACK/PATH` (a file of an icon pack imported with `lhde-import --icons`). A pack is any
directory of PNGs and `.ico` files under a free licence; `lhde-import --icons DIR NAME [--licence FILE]` writes it into the
Windows tree's `local/icons/NAME/` (relative paths kept; every PNG becomes a `.ico` with 16, 24, 32, 48, 64, 128 and 256 px
images, downscaled once there instead of at every start; `.ico` files are copied; the licence and readme beside it and
`source.ini` go with it) and indexes it as `icons/NAME`. A pack recipe says `requires = icons/NAME` (without the pack the
resolver falls back like a recipe whose resources are missing) and `fallback = RECIPE...`: the first of those whose files are
there gives every key the pack has no picture for, any file not imported, and the toolbar strips.
The one shipped: `xp-hires`, the CC0 "Windows XP High Resolution Icon Pack" by marchmountain (1024 px PNGs redrawn after
XP's icons; release 02 from github.com/marchmountain/-Windows-XP-High-Resolution-Icon-Pack, 159 MB zip; its folder
`Windows XP Icons`, 500 pictures named by what they show), mapped key by key to xp.theme's ids (pdf and settings have no
picture: XP's, 2000's or ReactOS's stand in):
```
lhde-import --icons "Windows XP High Resolution Icon Pack/Windows XP Icons" xp-hires --licence LICENSE   # 15 s, 36 MB
lhde-theme layer-set Desktop.icons xp-hires                                                              # or a theme file's [Desktop] icons
```
The 16 px images are the 1024 px pictures scaled down: softer than XP's hand-drawn 16 px icons; at 32 px and up they are
sharper.

## lhde-theme
The resolver from the shell (docs/THEME-FORMAT.md, liblhde):
```
lhde-theme list                         every theme: the user's, the shipped ones, bare styles; "style missing" marks a theme
                                        whose visual style is not imported (the settings lists leave it out)
lhde-theme resolve aero5048 --why       the record, each key with where it came from (file, base, current, default, fallback + reason)
lhde-theme resolve luna --get path.style     one key; --files: the theme files only (no current layer); --shell: as shell variables
lhde-theme current                      the selected theme's name (settings.ini [theme] name)
lhde-theme set jade4074                 settings.ini [theme] name (the current layer dropped), then _LH_THEME on $DISPLAY
lhde-theme layer                        the current layer: settings.ini [current], the unsaved changes on top of the theme
lhde-theme layer-set Glass.composition off   a key of the selected theme's layer ("-" removes it); layer-clear drops the layer
lhde-theme save-as my-jade "My Jade"    the theme with its layer as ~/.config/lhde/themes/my-jade.theme, selected
lhde-theme delete my-jade               one of your theme files
lhde-theme styles | schemes             the visual styles and the classic schemes Display Properties lists
lhde-theme set-key aero4074 Glass.composition off    -> aero4074-modified.theme (the flyout writes the current layer instead)
lhde-theme setting sidebar width 200    a per-user key; without VALUE: print it
lhde-theme style jade4074               what liblhde reads from a style (kind, format, build, family, first scheme)
lhde-theme explain [THEME]              where every visible part comes from ("When something looks wrong" above); --files THEME
lhde-theme dirs [--list|--windows|--user]   the three trees and the search order; --list for scripts
lhde-theme find REL | find KIND NAME    one data file (exit 1 when no tree has it); lhde-theme path VALUE: a theme value as a path
```

## Themes and switching them

One setting for everything (docs/SHELL-DESIGN.md 14): the root property `_LH_THEME` holds a theme *name* (a theme file, an
alias or a bare style; docs/THEME-FORMAT.md), its resolved record is `_LH_THEME_RESOLVED`. luxwm picks its frames from it,
luxshell its taskbar/sidebar pack, flxp-desktop the wallpaper, and clients (extendframe, flxp) their visual style and glass; all
of them follow a change live, nothing restarts. The choice is kept in `~/.config/lhde/settings.ini` (`[theme] name`) and
published when a session starts without one. The theme files are `data/themes/NAME.theme` (shipped, installed as
`$PREFIX/share/lhde/themes/`) and `~/.config/lhde/themes/NAME.theme` (yours: "Settings and your themes" below); any visual style
in the Windows tree is also a bare theme (an XP style imported with `lhde-import --style`, e.g. Royale, Zune, Embedded).

| Name (aliases, from the file's `[Theme] aliases`) | Frames: glass / classic | Taskbar, pack | Compositor |
| --- | --- | --- | --- |
| `aero4074` (`aero-patch`, `aero`) | Aero glass (`aero-patch`, the 4074 Aero patch style) / `jade4074-classic` | Avalon, `4074-aero` | auto: glass with picom, Jade classic frames without |
| `aerovision` | Aero vision glass (`aerovision`) / `jade4074-classic`; the luxnav "vision" header | Avalon, `4074-aerovision` (glowing taskbar text) | auto |
| `jade4074` (`jade`, `jade4074-classic`) | - / Jade msstyles frames (`jade4074-classic`) | Avalon, `4074-aero` | off |
| `slate4074` (`slate4074-classic`) | - / Slate 4074 msstyles frames | Avalon, `4074-slate` (30 px bar, nearest-neighbour scaling) | off |
| `slate4051` (`slate4051-classic`) | - / Slate 4051 msstyles frames | Avalon, `4051-slate` (29 px bar, bilinear scaling, 4051 images, clock tile in the list) | off |
| `plex4042` | - / the Plex style's own | Avalon, `4042-plex`; XP Explorer chrome | off |
| `basic5048`, `aero5048` | 5048's uDWM bitmaps (`aero5048`) / the style's own | classic | off (basic) / on (aero, `base = basic5048`) |
| `basic5112`, `aero5112` | 5112's DWMWindow streams (`aero5112`) / the style's own | classic (pack `5112-aero` for `Shell.taskbar = avalon`) | off / on |
| `luna` (`luna-blue`, `luna4051`, `luna-xp`), `luna-olive`, `luna-silver` | - / Luna msstyles frames from 4051's copy (`luna4051-blue`, `-olive`, `-silver`); style XP's own Luna | classic (flxp-taskbar in Luna's look) | off |
| `lunar`, `modern`, `mizu`, `lautus` | ReactOS's free styles, drawn live from the style | classic, in the style's look | off |
| `classic` (`windows-classic-standard`) | Windows Classic from the scheme (`windows-standard`) | classic | off |

**Wallpapers.** The wallpaper is the theme key `Desktop.wallpaper` (with `Desktop.mode`, `Desktop.color`), a relative name
`wallpapers/GROUP/NAME` resolved through the Windows tree (`index/wallpapers/<source>`, `local/wallpapers/`): the 4074 themes
get 4074's Bliss (the green leaves), slate4051 4051's (the wheat field), plex4042 4042 main's, aero4074 and aerovision a demo
image of the workspace (`lhde-import --from-workspace`; without it the build's Bliss), basic/aero5048 5048's glass facade,
basic/aero5112 5112's grass blades, luna XP's Bliss, luna-olive and luna-silver XP stock pictures, the ReactOS themes ReactOS's.
Your own choice wins: Display Properties' Desktop tab and Set as Desktop Background write it into the current layer
(`settings.ini [current]`), your own theme file's `[Desktop] wallpaper`, or `settings.ini [desktop] wallpaper = none` for no
picture in any theme (docs/THEME-FORMAT.md section 3).

**4051 vs 4074 Slate.** Builds after ~4060 look like 4074: their Avalon bars scale images nearest-neighbour, so the start
button (imageres 1156, 74x23, drawn 79x29) has the blocky flag; 4051 scaled bilinear (the smooth flag). `slate4051` has its own
pack for that and for 4051's other differences: a 29 px bar, the images of 4051's own imageres.dll (the right-angled tile
arrows), the clock as a tile in the list with the 96 px face, no power tile. 4051 really ran the classic Win32 taskbar there; the
Avalon taskbar with the 4051 start button stands in for it (`data/packs/4051-slate/shell.lua`).

**Fonts.** Each theme names its font directory (`Fonts.fonts`, default the theme's build: `fonts/<build>` in the Windows tree,
then fontconfig; docs/THEME-FORMAT.md); luxwm, luxshell and flxp load it while the theme is active, so captions, taskbar and menus
use the Segoe UI that 4074 ships (`fonts/4074/seguibk.ttf`, version 0.7) instead of a fallback. Nothing is installed system-wide.
`LHWM_DEBUG=1` (luxwm) and `LHSHELL_DEBUG=1` (luxshell) log the face the caption / pack font resolves to. luxshell draws its text
with wuxtheme's GDI text, the glyphs flxp and luxnav draw (whole-pixel stems instead of pango's fractional positions). In Aero
vision the task buttons' text is black on a white halo (pack `4074-aerovision`, `taskbar_text`).

**Dialog fonts (wux).** "MS Shell Dlg" and "MS Shell Dlg 2", the face of dialog templates and of `DEFAULT_GUI_FONT`, stand for
Tahoma as in XP's FontSubstitutes, under XP's styles and Windows Classic. Under a style whose `[SysMetrics] MSGBOXFONT` is Segoe
UI (the 4074 and later Longhorn styles) they stand for Segoe UI at the size asked for, as 4074's dialogs show it (its About box:
Segoe UI 8, from a template of 8 pt); without a Segoe UI among the font directories they stay Tahoma. The Explorer's Filter box is
in the pane's own font (the icon title font: Segoe UI 9 under Jade, as 4074's).

**Keyboard cues (wux).** As in XP, mnemonics are not underlined until the keyboard is used (Display Properties > Appearance >
Effects..., "Hide underlined letters for keyboard navigation until I press the Alt key", `[effects] keyboard_cues`): a popup menu
underlines them when it was opened from the keyboard (the last input was a key press: Alt+V, Shift+F10, the menu key), the
Explorer's menu band while Alt is held and while it is in keyboard mode or has a menu open from it, and pressing Alt shows them
in the window for good (XP's UI state: WM_CHANGEUISTATE / WM_UPDATEUISTATE / WM_QUERYUISTATE; the rebar's "Address" label
follows it).

**Window icons.** Captions and task buttons show the window's `_NET_WM_ICON` (the size nearest at or above 16 px), else its
`WM_HINTS` icon pixmap (xterm, xclock, Xt/Motif/FOX programs), else the theme's default icon: on the glass themes the Windows flag
(`DEFAULTICON` of the msstyles), on the classic and Luna themes and on the taskbar the XP application icon. A window that changes its
icon is redrawn at once.

**Switching.**

| Where | Start with a theme | Switch while running |
| --- | --- | --- |
| a session | `lhde-session start slate4074` (no name: the last one, else aero4074) | `lhde-session theme jade4074` from any terminal |
| the lab | `LAB_THEME=jade4074 session/lab.sh start` | `session/lab.sh theme slate4074` (or `LAB_THEME=slate4074 session/lab.sh restart-wm`) |
| anywhere | | `lhde-theme set slate4074`, or `xprop -root -f _LH_THEME 8u -set _LH_THEME slate4074` |

`luxwm -t NAME` / `LHWM_THEME=NAME` only applies while the display has no `_LH_THEME` yet; after that the root property wins
(a restarted luxwm keeps the session's theme). `LHSHELL_THEME=PACK` (a pack name or directory) is only luxshell's fallback when
no theme is known. A name that is not a theme is ignored (the current theme stays; luxwm and luxshell log it).

## The session (lhde-session)

`lhde-session start [--nested | --attach] [THEME]` starts, in this order: the root wallpaper once, luxwm in a restart loop,
picom when the theme wants a compositor, a terminal, flxp-desktop and luxshell (docs/ARCHITECTURE.md "A running session").
Without a flag it is a real X server on `$LHDE_DISPLAY` (default `:1`) and `$LHDE_VT` (default vt8), started with startx from a
console login; `--nested` a Xephyr window (`LHDE_GEOMETRY=WxH`, default 1280x800) on the current display, the session inside it on
`$LHDE_DISPLAY` or the first free display from `:2`; `--attach` the session's programs on an X server that already runs on
`$LHDE_DISPLAY` (else `$DISPLAY`). The control commands act on `$LHDE_DISPLAY`, else the one running session, else `$DISPLAY`:

```
lhde-session theme NAME                              switch the theme (root _LH_THEME; nothing restarts)
lhde-session taskbar avalon|classic|classic+sidebar|none   the bottom bar, live ("Classic taskbar instead of the Avalon one")
lhde-session shell on|off                            start / stop luxshell (the Avalon taskbar and sidebar)
lhde-session picom on|off                            start / stop picom (glx; LHDE_PICOM_BACKEND=xrender for Xvfb)
lhde-session desktop on|off                          start / stop flxp-desktop; restarts luxshell without / with its desktop layer
lhde-session stop                                    end the session: luxshell, flxp-desktop, luxwm, picom, the session bus and
                                                     every process on it, and the X server of start and --nested
```
Environment: `LHDE_PICOM=0|1` forces the compositor off / on for the session, luxshell included; `LHDE_SHELL=0` without luxshell;
`LHDE_DESKTOP=0` without flxp-desktop (luxshell then shows its own desktop layer); `LHDE_WM=fvwm` an fvwm2 session (`lab.fvwm2rc`);
`LHDE_TERMINAL=COMMAND` (default urxvt, else xterm; `none` for no terminal); `LHDE_WINDOWS` another Windows tree. State and logs:
`$XDG_RUNTIME_DIR/lhde-session<DISPLAY>/` (`theme`, `shell`, `desktop`, `luxwm.log`, `luxshell.log`, `flxp-desktop.log`,
`picom.log`, `session.log`). A real session without a D-Bus session bus gets one of its own, stopped with the session.

**flxp-desktop** shows the wallpaper and the icons in a `_NET_WM_WINDOW_TYPE_DESKTOP` window, started after luxwm and with
luxshell, from the session shell (so it and the explorers it opens share DISPLAY, the session D-Bus and PATH). It takes the
wallpaper and the icon set from the session theme's record (`Desktop.wallpaper`, `Desktop.mode`, `Desktop.icons`) like everyone
else; `--wallpaper FILE` (`FLXP_WALLPAPER`) overrides it for one run (as an exported `FLXP_<KEY>` would a flxp.ini key, which
is why the session exports none). It survives theme switches (it is not part of the WM loop). While it runs luxshell is started with
`LHSHELL_DESKTOP=0` (no desktop layer of its own: flxp-desktop fades to black in maximised mode itself); `LHSHELL_DESKTOP=0` works
for any luxshell start.

**Startup wallpaper.** `lhde-session` sets the root background once, before anything else paints, with `lhwall.sh` (sourced here
and by `lhde-session desktop on|off`): the session theme's resolved `Desktop.wallpaper` and `Desktop.mode` (the current layer, the
theme file and its base chain, the build's default; `settings.ini [desktop] wallpaper = none` applied), mapped to `feh
--bg-fill|max|center|tile|scale`; `none` is a plain root (`xsetroot -solid '#3a6ea5'`); `LHSHELL_WALLPAPER=0` forces `none`.
luxshell never runs feh/xsetroot while another program owns the desktop (`LHSHELL_DESKTOP=0`, i.e. flxp-desktop on, or a
`_NET_WM_WINDOW_TYPE_DESKTOP` window of another client), so the session shows one wallpaper from the first frame on.

**Session commands** (luxshell, docs/SHELL-DESIGN.md 14): `logoff`, `standby`, `shutdown`, `restart`, `lock`, overridable in
`~/.config/lhde/settings.ini` under `[commands]` (`name = shell command`). Defaults: `logoff` = `lhde-session stop` when an
lhde-session runs on the display, else `luxwmc quit` (luxwm exits cleanly: its root properties are deleted, the windows stay
mapped; the lab's `restart-wm` brings it back); `standby` = `zzz` if installed, else `loginctl suspend`; `shutdown` = `loginctl
poweroff`; `restart` = `loginctl reboot`; `lock` = `xlock` or `slock` if installed. From a terminal:
`xprop -root -f _LHSHELL_COMMAND 8u -set _LHSHELL_COMMAND logoff`. flxp's start panel asks first, then runs the same
`[commands] logoff` (else it ends luxwm with `luxwmc quit`) and `[commands] shutdown` (default `loginctl poweroff`).

## Keys in the session

Default keys, override in `~/.config/lhde/keys` (one binding per line; docs/LUXWM.md):

| Keys | Action |
| --- | --- |
| Super (pressed and released alone) | start menu (luxwm sends `_LHSHELL_START`; again closes it; `Super_L none` in the keys file turns it off) |
| Super+Return | terminal (urxvt), Super+t for xterm |
| Super+e | flxp Explorer (ROX-Filer when flxp is not installed) |
| Super+d | show desktop, as Windows' Win+D: minimises the visible windows (they stay minimised); Super+d again brings exactly those back, but only if nothing changed in between (no window restored, activated, opened or closed); otherwise it is a new "show desktop" |
| Super+r | Run box (luxshell's own, via `_LHSHELL_COMMAND run`; or `Mod4+r exec rofi -show run` in your keys file) |
| Super+s | sidebar under the windows (part of the desktop) ⇄ on top of them, at once (`_LHSHELL_COMMAND sidebar-stacking toggle`; saved as `[sidebar] stacking`) |
| Super+q or Alt+F4 | close window |
| Alt+Space | the window menu (Restore, Move, Size, Minimize, Maximize, Close; arrows, mnemonics, Enter, Esc); also a right click on the caption or a click on the caption icon (double click there: close) |
| Alt+Space, M / S | keyboard move / size: arrows move (size: the first arrow picks the edge) in half-button steps, Ctrl+arrow 1 px, the mouse takes over, Enter or a click accepts, Esc puts the window back |
| Alt+Tab, Super+Tab (Shift: backwards) | as Windows': while Alt (Super) is held each Tab previews the next window by raising it (the previous one goes back to its place; minimised windows come last and are not previewed); releasing Alt focuses and raises the chosen one, every other window keeps its stacking order; Esc cancels |
| Super+Up / Super+Down | maximise / minimise |
| Super+1..4 | workspaces (virtual desktops: the taskbars show the current one's windows; `_LHSHELL_COMMAND desktop N\|next\|prev` or any pager's `_NET_CURRENT_DESKTOP` request switches too; pressed while moving a window, the window goes along; the window menu's Move to Desktop moves one too) |
| drag caption | move; drag edges and corners: resize |
| double-click caption | maximise/restore |
| Super+drag | move the window from anywhere on it (berry's default; berry's Alt+drag resize is off, Alt+clicks go to the program: Alt+Space, S sizes with the mouse) |

`luxwmc` (in `build/bin`) has berry's IPC commands (e.g. `luxwmc window_close`, `luxwmc switch_workspace 1`, `luxwmc quit`,
`luxwmc reload-theme`). Minimised windows are only hidden until the taskbar exists, and Alt+Tab reaches hidden windows.

## luxwm: animations and dragging

The animation set is the theme key `Glass.animations` (`4074plus`, `4074`, `5048`, `invented`, `none`; Display Properties'
Appearance tab), `LHWM_ANIMATIONS=NAME` overrides it for one luxwm run, and `ANIMATE_STYLE` in `luxwm/config.h` is the default
(`ANIM_4074PLUS`). With picom: `4074` fades a window in over 350 ms with a slight scale-in when it opens or is restored (standing
in for 4074's 3D swing), while minimise, close and maximise/restore are instant, as in 4074; `4074plus` adds a gentle centre zoom
on open, a grow on restore and a shrink on minimise; `5048` is the April 2005 uDWM curve (266 ms); `invented` luxwm's own,
non-Longhorn 250 ms shrink towards the bottom edge plus a maximise morph. Without a compositor luxwm flies the caption bar to the
taskbar button like XP on minimise, restore and maximise (`ANIMATE_WIREFRAME true` for XOR zoom rectangles instead). `none`, or
`ANIMATE false` in `config.h` and `make`, turns all of it off (luxwm then sets no `_LHWM_ANIMATE`, so the picom rules match
nothing). `DRAG_INTERVAL` (ms, default 16) caps how often an interactive move/resize is applied; `DRAG_THRESHOLD` (px, default 4,
Windows' SM_CXDRAG) is how far the pointer must move before a caption press becomes a move, so a double-click with a shaky hand
restores a maximised window instead of dragging it.

## Extended glass frames and the navigation bar (examples)

**Extended frames** (docs/CLIENT-PROTOCOL.md): `make && DISPLAY=:2 build/examples/extendframe` opens the reference client
(an Explorer-style 60 px glass header; drag it, double-click to maximise, drag the right edge, Escape or q quits). Without glass
(Slate, Luna, Jade, no picom) its header shows luxwm's `_LHWM_HEADER_PIXMAP`, the theme's caption band continued down. Under a
glass frame set (aero4074, aerovision, aero5048, aero5112) the glass header is the set's Explorer plate (`plate_explorer` in
`data/frames/*/theme.ini`); `glass_alpha` there sets how see-through the frame glass is (0.5 matches 4074's screenshots, 1.0 is
the texture as stored), `glass_alpha_mode = square` the alternative 2005 weighting. `extendframe -c` also takes the caption rows
(experimental `CLIENT_CAPTION`: it draws its own title there, luxwm's caption buttons stay clickable through a hole in its input
shape).

**The navigation bar of the concept art** (docs/NAVBAR.md, `luxnav/`): switch to Aero vision (`session/lab.sh theme
aerovision`, or `xprop -root -f _LH_THEME 8u -set _LH_THEME aerovision`) and run `build/examples/navdemo [-r ROOT] [DIRECTORY]`:
a small file browser with the title in the frame's 15 px deco, the back/forward orb, the breadcrumb (click a segment; click the
box's empty part or Ctrl+L to type a path, Enter goes, Escape cancels; "«" stands for the segments that do not fit), a search box
that filters the listing (Ctrl+F, or just type), back/forward (also Alt+Left/Right, mouse buttons 8/9; Backspace goes up),
double-click a folder line to open it; drag the header to move, double-click it to maximise, the top 3 rows resize. Escape
(outside a field) or Ctrl+Q quits. `-r ROOT` starts the breadcrumb at ROOT instead of "File System" (`/`); `NAVDEMO_DEBUG=1` prints
luxnav's layout. It works under every theme (glass or luxwm's header pixmap); the vision geometry needs `aerovision`. Tuning:
navdemo reads the frame set's `nav.ini` (`data/frames/aerovision/nav.ini`), else the theme's navbar layout
(`data/navbar/vision.ini`, which flxp's Explorer reads); orb size, boxes, fonts, colours, the text glow (comments there); live
with `session/theme-reload.sh` or `session/theme-watch.sh`. Installed, copy the file to `~/.config/lhde/<same path>` to edit it.

Experimenting with the header (docs/NAVBAR.md "Presets"): run `session/theme-watch.sh :2` in one terminal and
`NAVDEMO_DEBUG=1 DISPLAY=:2 build/examples/navdemo` in another, then edit the nav.ini and save: every save reloads the header.
The file is a set of presets (`[preset small]`, `[preset big]`, `[preset concept]`, each with all keys; `preset = NAME` at the top
picks the default); in navdemo `p` / `P` cycle through them live (the title shows `[NAME]`, `NAVDEMO_PRESET=NAME` starts with one)
and a new size is a copied and renamed section. To move the breadcrumb box or the search box, set `address_dy` / `search_dy`
(rows, + = down) or `address_dx` / `search_dx` at the top of the preset; `orb_*`, `address_*`, `search_*` `_x`/`_y`/`_w`/`_h`,
`title_dx`, `title_baseline` and `crumb_text_dx`/`_dy` place things absolutely (x in client pixels, y in frame rows; empty =
automatic, whose value the comment beside each key gives). The numbers to start from: NAVDEMO_DEBUG prints every part's final
rect (client x, y, the frame row, size) on each relayout. `variant = big` is the tall header with the travel buttons' drop-down
arrows (69 rows); there a press on an arrow, or a right click on back/forward in either variant, opens the history list.

The right-hand toolbar and the drop-downs (docs/NAVBAR.md "Toolbar", "Breadcrumb drop-downs"): nav.ini's `tools` key lists the
items per preset (`tools = search_field`, the small and big presets, is the search box alone; `[preset concept]` has `tools =
search favorites`, the concept's magnifier and star orb; all kinds are commented at the top of nav.ini: `search`, `search_field`,
`favorites`, `bookmark:PATH[:LABEL]`, `views`, `newfolder`, `separator`). `NAVDEMO_TOOLS="newfolder views separator search
favorites"` overrides the list for a run. In navdemo: the magnifier brings the search field out (it goes back when it loses the
focus empty); the star opens the GTK bookmarks (`$XDG_CONFIG_HOME/gtk-3.0/bookmarks`) with "Add current folder" at the top (point
`XDG_CONFIG_HOME` at a scratch directory to try it without touching your own); a bookmark button goes to its folder; Views switches
the listing between List and Details; New Folder creates "New Folder", "New Folder (2)", ... A hovered item shows a tooltip after
half a second. Every chevron of the breadcrumb is a drop-down: a press lists that folder's subfolders (sorted, dot folders left
out, the next segment's folder bold and selected), the chevron after the last segment appears while the breadcrumb is hovered
(`sep_last`), and "«" lists the collapsed ancestors; Up/Down/Home/End or a letter move in a list, Enter or a click goes there,
Escape or a click outside closes it. The icons come from the theme's `path.resources` (its build's shell32.dll; nav.ini
`icon_dir` overrides it), the star from the visual style.

## The Explorer's views (flxp)

In the Longhorn frame (and the Longhorn file dialog) the Explorer's list has build 4074's three views, each at an image size:

| view | sizes | layout |
| --- | --- | --- |
| Details | 16-32 px | the table; the size is the rows' image (17 px rows at 16) |
| Tiles | 32-96 px | the image at the left, the name and, from 48 px, two grey "Label: value" lines right of it (a folder its Type; a picture Dimensions and Type; a drive Percent Full and Free Space; another file Type and Size); 218 x 56 at 48 px |
| Icons | 16-256 px | the image above, the name below (two lines, then an ellipsis); Large 128-256, Medium 48-128, Small 16-48 |

The view button at the bottom of the left pane (the Explorer's and the file dialog's) is 4074's: its left part shows the view's
picture and goes to the next view (Details, Tiles, Icons, each at the size it had last); its arrows open the slider, a white popup
with the five stops' pictures (Details, Tiles, Large, Medium, Small Icons) beside a vertical slider. Dragging the thumb (or
Up/Down) sizes the view as it moves, growing downwards through Details and Tiles, then jumping to 256 px at Large Icons and
shrinking downwards to 16 px at Small Icons; a click on a picture shows that stop at the size it had last and closes the popup;
Escape, Enter or a click elsewhere closes it. The pictures are the 4074 pack's (`packs/4074/imageres`, imported by `lhde-import`);
without it the control draws plain shapes.

Choosing a view otherwise: View > Details, Tiles, Icons (also in the list's context menu and the header's Views drop-down); Ctrl+1
Details, Ctrl+2 Tiles, Ctrl+3, 4, 5 Large, Medium, Small Icons (4074's five slider stops, top to bottom), Ctrl+0 the default view;
Ctrl+plus, Ctrl+minus, Ctrl+wheel and View > Zoom make the view shown a step bigger or smaller within its range (4051's levels 16
21 27 36 48 64 84 111 147 194 256 and the range's ends). Each of the five stops remembers the size it was last shown at, and
"Icons" is the icon stop used last, so going back to a view brings its size back; the sizes are kept for every window in
`~/.local/state/lhde/flxp-view-sizes`. Every folder keeps its own view, size and Hide File Names in `flxp-views` beside it
(`remember_views`, Tools > Folder Options); a folder never opened gets `default_view_longhorn` (Icons at 48 px unless Folder
Options says otherwise).

The breadcrumb names your home folder "Home" (Home > Documents), as the namespace places it under the Desktop. Folder Options'
"Display the full path in the address bar" (`breadcrumb_home = 0` in `flxp.ini`) shows a folder under it by its real path instead,
My Computer > Local Disk (/) > home > you > Documents; the default is `breadcrumb_home = 1`.

View > Show in Groups (`groups = 1` in `flxp.ini`) groups Tiles and Icons by type, under the theme's group headers (4074's
GROUPHEADER band with the title and its count, the collapse button at the right end; a click on a header folds the group).

Hide File Names (View menu) shows Icons without names, in cells hugging the images; it belongs to the folder and to Icons: Details
and Tiles always show the names, and the names stay hidden when the folder comes back to Icons.

Thumbnails show in every view and are just bigger or smaller images: a picture in a photo frame that follows its aspect ratio (a
soft shadow, a grey line, a mat graded from white to light grey; no frame below 32 px), any other file its own icon at the size, a
folder 4074's photo standing in the open folder where the build has that art (shell32 #389/#390 of the 4074 resources; the
folder's `folder.jpg` first, else its first picture), else the folder icon with up to four pictures on it. There is no square
behind them; selection and hover are the theme's `Explorer::ListView` LISTITEM underlay (Slate tan, Jade blue; in Icons around
the icon and its label, not the whole cell), the classic highlight in styles without it. View > Show Thumbnails (and Folder
Options) turn them off for every folder (`thumbnails = 0` in `flxp.ini`); on network mounts (NFS, SMB, sshfs and other FUSE
network filesystems) they are off unless `thumbnails_network = 1`, since every picture would be read over the network. The
thumbnails are cached in `~/.cache/thumbnails` (the freedesktop spec, shared with other programs).

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

**Toolbar size.** The size of what sits above the folder is in one place, the last entry of View > Toolbars, and that entry shows
only what the window's chrome has, in the words of its era:

- the XP frame: **Icon Options** > Large Icons / Small Icons, the "Icon options" of XP's Customize Toolbar dialog without the
  dialog. The Standard Buttons toolbar changes at once between the 24 px and the 16 px strip, the labels staying on the right of
  Back, Search and Folders as XP's default "Selective text on right" (`toolbar_icons = large|small` in `flxp.ini`; other open
  windows take it when they start).
- the Longhorn frame: the header's elements as checks (Back/Forward Buttons, Title, Address (Breadcrumb), Search, Favorites: 4074's
  system-menu Toolbars), then **Toolbar Size** > Small / Medium / Large / Extra-Large (4074's system-menu Toolbar Size). The header
  refits at once (`header_size`, `header_buttons`, `header_title`, ... in `flxp.ini`; without a `header_size` of its own the
  theme's `Explorer.navbar_preset`, which Display Properties' Shell tab sets). The Longhorn frame's toolbar has one icon size, so
  Icon Options is not shown there.
- the Longhorn file dialog: View > Toolbars, the same elements and Toolbar Size, kept apart from the Explorer's
  (`filedlg_header_size`, small unless set; the elements fall back to the Explorer's).

## The Explorer's details and task band (flxp)

What the Explorer says about the folder shown or the selection comes from one model (`flxp/shell/details.h`) shown two ways.
Per kind of item (file, picture, song, folder, drive, a virtual folder such as My Computer) it lists properties from a small
registry: the derived Type, Attributes, Date Modified, Size, Dimensions, File System, Free Space, Total Size, and the stored
Description (`user.lux.desc`), Tags (`user.xdg.tags`) and Comment (`user.xdg.comment`), the extended attributes of
docs/ANNOTATIONS.md (how to edit them: "Descriptions and tags" below).

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
(the Properties dialog). The description, tags and comment, or their invitations, are editable: a click opens an edit box in the
row's place. Several items: a stack of up to seven thumbnails, the focused one in front, "N items selected" and each
value shared by all or "(multiple values)". The item tasks (Open, Rename, Copy, Delete, Properties) follow when 200 px are left
right of the details. View > Preview switches between the expanded band (126 rows, its bottom edge drags it up to 246) and the
compact one (48 rows: the count or the name on one line, a small icon or thumbnail, the tasks side by side); dragging the edge
above 87 rows expands it, below compacts it (`taskpane_compact`, `taskpane_height` in `flxp.ini`). Thumbnails follow the same
`thumbnails` and `thumbnails_network` settings as the views; without them the icon shows. The theme puts the band at the top
or the bottom (`Explorer.taskpane`): the 5048 and 5112 themes have it under the list and the left pane, across the window's width,
which comes close to Vista's details pane; there it is the same band mirrored, the picture hanging above it over the left pane
(whose view button moves up out of its way) and its top edge dragging it.

The task band's keys: the theme says where it goes (`Explorer.taskpane = top | bottom`); `flxp.ini` holds the user's choices,
all spelled `taskpane*`: `taskpane` (shown, `0 | 1`), `taskpane_compact` (`0 | 1`), `taskpane_height` (the expanded rows,
87..246, default 126) and `taskpane_background` (`menubar`, default: the command bar's strip; `aurora`: the Longhorn demo's
aurora bitmaps from `resources/demo`, a taste choice).

## Descriptions and tags (flxp)

A file or folder can carry a one-line **description**, **tags** and a **comment**. They are extended attributes on the file
itself (`user.lux.desc`, `user.xdg.tags`, `user.xdg.comment`; the names luxfs's `x` and `lsx` use, docs/ANNOTATIONS.md), so
there is no database: they need a file system that keeps user attributes (ext4, btrfs, xfs, zfs, tmpfs; not FAT or many network
mounts, where editing says so). Tags are stored comma-separated (`reference,4051`); typed, they may be separated by commas or
semicolons, spaces around them are dropped, a tag given twice is kept once. Emptying a value removes the attribute.

Where they show:
- **Details view columns.** Right-click the column header for a check per column (Name, Size, Type, Date Modified, Description,
  Tags, Comment) and More..., or View > Choose Details... (XP's dialog: check, Move Up / Move Down, Show / Hide, the selected
  column's width). A click on a column header sorts by it (files without a value first). The columns, their order and widths
  are remembered in `~/.local/state/lhde/flxp-columns`; the attributes are only read while one of their columns is shown or
  sorted by.
- **The infotip.** Resting the pointer on an item shows XP's tip: Type, Date Modified and Size, then Description, Tags and
  Comment when they are set.
- **The panes.** XP's Details rollout lists them when set (read-only, as XP's did); 4051's task band shows them for one item,
  with "Add a description", "Add tags", "Add comments" when empty.

How to edit:
- **The context menu** (Explorer and desktop): Edit Description... (one item), Add Tag... (one or several items: each keeps its
  tags and gets the new ones; the drop-down offers the tags used in the folder), and Remove Tag (a submenu of the selection's
  tags, removed from every selected item that has it).
- **The task band:** click a value or its invitation, type, Enter (or click elsewhere) keeps it, Escape drops it.
- From a terminal: `x desc FILE "text"`, `x +tag FILE a b`, `lsx -t DIR` (luxfs), or `setfattr` / `getfattr`. The Explorer
  follows a change made elsewhere (inotify reports attribute changes).

They travel with the file where its attributes travel: a move on the same file system, flxp's own copy and move (also across
file systems), `cp -a` (or `--preserve=xattr`), `rsync -X`, `tar --xattrs`. They do not travel with `cp` without `-a`, most
archive and sync tools by default, or git.

## Start menu, taskbar autohide, sidebar modes (luxshell, docs/SHELL-DESIGN.md 6.4, 7.3, 13)

**Start menu.** The Start button and the Super key (luxwm sends the root client message `_LHSHELL_START` when Super is pressed
and released alone; the Mod4 bindings keep working, unbound Super+key combinations are swallowed while Super is held; `Super_L
none` in `~/.config/lhde/keys` turns it off) open flxp's XP start panel, drawn from the visual style's StartPanel parts (and
at their sizes: ReactOS's styles make it 357 px wide); under Windows Classic it is XP's unthemed panel in the system colours
(the user name on the active caption's gradient, the menu colour, etched separators, a dialog frame, the All Programs menus
as XP's classic menu bands), and a part a style leaves out is drawn that way too. With the Avalon taskbar luxshell runs
`flxp-startmenu` (flxp-taskbar's panel without the bar) and forwards both to it; with the classic bar flxp-taskbar answers itself.
See "Start menu: flxp's start panel everywhere" below. Longhorn's Avalon shell never had a start menu of its own, so luxshell
draws none.

**Context menus** (Longhorn 4074's Avalon menus, sources in docs/SHELL-DESIGN.md 16). All three are drawn in the 4074
Avalon menu look (white, 1 px grey frame with a drop shadow, 19 px rows, the theme's highlight: Slate's tan `#EDB078`, Jade's
light blue) and open where you click, flipped to the left / upwards where they would not fit, as Windows does.
- **Sidebar menu**: right-click an empty part of the sidebar, or click the *Options* glyph (▾) at its top right (the menu opens
  under it). *Minimize bar* (the sidebar into the taskbar; *Restore bar* when minimized), *Properties* (the settings flyout,
  next to the sidebar), then luxshell's own *Lock the Sidebar* (`[sidebar] locked = 1`: the width sizer no longer drags) and
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
  *Properties* (the settings flyout: theme list, Save as..., composition, taskbar kind and autohide, start menu, sidebar
  mode and stacking, wallpaper), *Task Manager* (the first of xfce4-taskmanager, lxtask, gnome-system-monitor,
  mate-system-monitor, qps, else `xterm -e top`), *Show Desktop* / *Hide Desktop* (`_NET_SHOWING_DESKTOP`), *Cascade*, *Tile
  Vertically* (side by side), *Tile Horizontally* (one above the other; more than three windows go into a grid; greyed with no
  windows); then luxshell's *Auto-hide the taskbar* and *Sidebar* ▸ *Full* / *Minimized* / *Pinned tiles only*.
- **Keys** while a menu is open: Up/Down (Home/End) move, Right or Return opens a submenu, Left or Esc closes a submenu, Esc
  closes the menu, Return/space activates. The underlined letter (mnemonic) activates its item directly (opens its submenu);
  when several items share it, the key cycles through them; a letter that is nobody's mnemonic jumps to the next item starting
  with it. A click outside any of luxshell's surfaces (or on another of its surfaces) closes the menu. The 4074 menus drew no
  underlines; ours always do.

**Autohide** (Avalon behaviour): the bar slides out (100 ms) as soon as the pointer is off it and no start menu, menu or
flyout of the shell is open, leaving a 2 px sliver; touching the bottom edge brings it back. The strut is the sliver, so
maximised windows use the full height. **Sidebar modes:** `full` (the whole sidebar), `minimized` (the sidebar slides away; the
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
  stack, and the sidebar ends at the taskbar's top (the 4015 look; it follows autohide). Pinned: the block sits above the
  full-width taskbar, under the windows, no strut. Minimized: as always (no sidebar, the tiles in the taskbar). Since the
  sidebar's clock can be covered by windows, the taskbar shows the time at its right end as well (the clock tile's taskbar
  view, as 4074's taskbar shows it when the sidebar has no clock on screen).
- `top` (default, "Sidebar on top of windows"): the sidebar is above the windows (`_NET_WM_STATE_ABOVE`, like the taskbar),
  runs the full height and is above the taskbar at the corner, which is shortened by its width (4074); its strut keeps maximised
  windows clear (full 1099x770 with a 181 px sidebar, pinned 1280x662).
- **Super+s** toggles between the two **instantly**: no animation or fade; the sidebar's window state, strut, geometry and place
  in the stack and the taskbar's width change in one callback and one repaint. Windows are not moved; luxwm refits maximised
  windows to the new work area as for any strut change (in the same composited frame on the lab). The key is luxwm's default
  `Mod4+s shell sidebar-stacking toggle` (added to a user keys file where Mod4+s is unbound); luxshell takes
  `sidebar-stacking toggle|top|bottom` on `_LHSHELL_COMMAND` (e.g. `xprop -root -f _LHSHELL_COMMAND 8u -set _LHSHELL_COMMAND
  "sidebar-stacking top"`), saves `[sidebar] stacking` and applies it; a change by another writer (`_LH_SETTINGS_CHANGED`) applies
  the same way.
- With the classic taskbar and `[sidebar] with_classic = 1` the sidebar still ends above the classic bar, under or over the
  windows by the same rule (`bottom`: below, no strut; `top`: above, with its strut).
- The maximised-mode black layers are unchanged (in `bottom` the maximised window simply covers the sidebar).

**Resizing the bars.** The sidebar's left edge is its `SidebarSizer` (2 px in Aero, where it is invisible over the glass
edge; 6 px in Slate, the light edge you see): the pointer turns into a left-right arrow, drag it to make the sidebar 100 to
600 px wide (Longhorn's `Sidebar\Width` limits; default 150). The taskbar's top edge (3 px, up-down arrow) sets its rows: drag
up for 2 or 3 rows (30 px each), down for fewer. Both resize live: the struts and `_NET_WORKAREA` follow (maximised windows
refit), the taskbar yields to the sidebar, tiles reflow; the new size is saved when you let go.

**Clock tile.** Right-click the clock (or any tile) for its menu (see "Context menus" above); *Clock Tile Properties* shows the
same choices in a flyout. *Clock layout* picks one of the 4074 views: Analog and time (the default), Analog and digital time,
Digital time and date, Digital time, weekday and date, Small clock and calendar, Calendar (a month grid). *Pinned to the bottom
(4074)* switches between 4074's pinned clock at the bottom of the bar (80 px face) and 4051's clock as an ordinary tile in the list
(96 px face, larger time line); unpinned, *Move Up* / *Move Down* place it (and any other tile) in the list, which is remembered.
Each pack has its default (pinned in `4074-aero`, `4074-slate` and `5112-aero`, a tile in `4051-slate`); your choice holds for the pack you made it in.
Unpinned, the face follows the bar width as in 4051 (the tile's width less 32, up to the native 260 px) and the tile grows and
shrinks with it; pinned, it stays 80 px at any width. The face is 4074's layered clock (face, hands, hub and glass images from
imageres.dll on their original pivots); the second hand ticks once a second. It follows the theme's visual style: the light grey
set under Slate (and Luna, Classic and the other non-Longhorn styles), the dark-blue set that 4074 drew with the DWM under Jade and
Aero (jade4074, aero4074, aerovision, the 5048/5112 Aero and Basic themes), and under a Plex style the light set of the Plex
builds (4042's art when it is imported, else the same 4074 images). A pack can override it with the key `clock_face = "light"`,
`"dwm"` or `"plex"`.

**Calendar flyout.** A click on the clock opens 4074's clock flyout: the clock at full size with the digital time under it,
and the month calendar with that month's photo, today on a tan box, the days of the neighbouring months in grey and the week
starting on the locale's first weekday. Point at the calendar and the ◀ ▶ buttons fade in; they step a month (another
year shows in the title). It opens at the clock and covers the bar's bottom, as in 4074, and closes on Escape, a click outside
it, or another click on the clock; it always opens on today's month. The tile's time line and the weekday and month names
follow `LC_TIME` (24-hour time where the locale has it); the flyout's large time is 4074's fixed `h:mm AM/PM`.

**Compositor rule.** The compositor follows the theme: switching to a theme with `composition = off` (Slate, Jade, Luna, the
classic and ReactOS themes, basic5048/5112) stops the picom of this display (only a process named `picom` whose environment has
the same `DISPLAY`), and switching to an `auto` or `on` theme (aero4074, aerovision, aero5048, aero5112) starts one if no
compositor runs. luxwm follows the compositor (glass frames with it, the theme's classic frames without), so the whole desktop
changes look together. The settings flyout's *Composition* group overrides this: *Always on* / *Always off*; *Follow the theme*
is the default. The switch is the theme key `Glass.composition` like the taskbar (see "Per-theme taskbar, composition" below).
The rule is applied at startup too: 1.5 s after luxshell starts it stops the picom of its display when the theme says off and
starts one for a glass theme when none runs (log `luxshell: compositor at startup: ...`). `lhde-session` decides with the same
rule whether to start picom at all (`lhcomp_wanted` in `lhwall.sh`), so a classic session shows no flash of glass; `LHDE_PICOM=0`
/ `LHDE_PICOM=1` force it off / on for the whole session, luxshell included (`LHSHELL_COMPOSITION=off|on|follow` forces luxshell
for one run; the flyout can still change it). A picom you start by hand (`lhde-session picom on`) stays until the next theme change
or luxshell start. The picom command is `[commands] picom` in settings.ini, else `$LHSHELL_PICOM_CMD`, else `picom --config
<session dir>/picom-glass.conf --backend glx`; on an Xvfb display, which needs xrender, start luxshell with
`LHSHELL_PICOM_CMD="picom --config session/picom-glass.conf --backend xrender --blur-method gaussian --blur-size 5 --blur-deviation 1.2"`.
picom's output goes to `$XDG_RUNTIME_DIR/luxshell-picom<N>.log` (`<N>`: the display without its colon).

**Classic taskbar instead of the Avalon one** (docs/SHELL-DESIGN.md). The taskbar kind, the theme key `Shell.taskbar` (below),
picks the bottom bar: `avalon` (luxshell's taskbar), `classic` (flxp's XP-style `flxp-taskbar`, which luxshell starts and stops)
or `none`. Switch live from the settings flyout (*Taskbar*: Longhorn (Avalon) taskbar / Classic taskbar (flxp) / No taskbar) or
from a terminal with `lhde-session taskbar avalon|classic|classic+sidebar|none` (the root command `_LHSHELL_COMMAND` =
`taskbar KIND`, plus `sidebar-with-classic 0|1` for `classic` / `classic+sidebar`, to the running luxshell, which saves it in the
current theme's current layer; with no luxshell on the display the script sets the theme key itself, `lhde-theme set-key`). The
settings flyout can also be opened from a terminal (the classic bar has no way to it):
`xprop -root -f _LHSHELL_COMMAND 8u -set _LHSHELL_COMMAND settings`. In `classic`:
- luxshell has no taskbar surface, no taskbar strut and no `_NET_WM_ICON_GEOMETRY` (flxp-taskbar sets its own); it runs
  `[commands] taskbar` (default `$LHSHELL_TASKBAR_CMD`, else `flxp-taskbar`) with its own DISPLAY and D-Bus session, log in
  `$XDG_RUNTIME_DIR/luxshell-taskbar<N>.log`, and stops exactly that process (its pid, checked against the display) when you
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
- Task buttons: a right click opens the window's window menu (luxwm's, through `_GTK_SHOW_WINDOW_MENU`: XP's task button menu;
  for a minimised window Restore is the default item); dragging a button past 4 px moves it to the place of the button under the
  pointer (4074's taskbar). The order holds for the X session (root property `_LHSHELL_TASK_ORDER`, the window ids in order);
  new windows go to the end.
- Clock: luxshell publishes the root property `_LHSHELL_CLOCK_VISIBLE` = 1 while a clock tile of its own is on screen, 0
  otherwise (sidebar minimized, clock tile closed); flxp-taskbar hides its clock while it is 1.
- Start menu: see "Start menu: flxp's start panel everywhere" below; in `classic` the classic Start button and Super open
  flxp-taskbar's XP start panel (it is the start-menu provider).
`LHSHELL_TASKBAR=avalon|classic|none` overrides the setting for one luxshell run (not saved).

## Live-editing a theme (`theme-reload.sh`, `theme-watch.sh`)

Edit a theme and see it at once, without restarting luxshell or luxwm:
- `session/theme-reload.sh [DISPLAY]` (default `$DISPLAY`, else `:1`) reloads both: luxshell gets `_LHSHELL_COMMAND reload`, luxwm
  `luxwmc reload-theme` (`build/bin/luxwmc` when luxwmc is not on `PATH`), or, when luxwmc is missing or does not know that command,
  the root ClientMessage `_LHWM_RELOAD` (sent with python3-xlib). A missing luxwm side is reported, not an error.
- `session/theme-watch.sh [DISPLAY]` watches, in every data directory (`lhde-theme dirs --list`: `~/.config/lhde`, the install's
  `share/lhde` or the source tree's `data/`, the Windows tree's index), `themes/*.theme` (with `~/.config/lhde/themes/`),
  `navbar/*.ini`, `frames/*/theme.ini` and `frames/*/nav.ini` (luxnav's tuning; luxnav clients such as navdemo re-read it when
  luxwm announces the reload), `packs/*/shell.lua`, and luxshell's `tiles/*.lua`, and runs `theme-reload.sh` after every save (a
  burst of writes within 0.3 s counts once). It uses `inotifywait` (inotify-tools) when installed, watching the directories so
  editors that replace the file are seen; otherwise it polls the files' mtime and size every second. Ctrl-C stops it. Example:
  `session/theme-watch.sh :2 &`, then edit `data/packs/4074-slate/shell.lua`.

What luxshell's reload does (`runtime.lua`): it resolves the current theme again (liblhde; theme file edits count), reads the
pack's `shell.lua` again even when the directory did not change, applies the theme's fonts again, drops the cached PNGs whose file
changed (mtime or size, so an edited imageres bitmap is read again), and rebuilds the taskbar, sidebar, tiles (`tiles/*.lua` are
read again), flyout chrome, start menu and menus in place on the same windows; the wallpaper, the composition rule and the taskbar
kind follow the theme's record as on a theme switch. The log says `luxshell: reload (...): theme T, pack P, N image(s) re-read, M
ms` (about 40 ms on the lab). A `shell.lua` with an error keeps the running pack (the error is logged). Setting `_LH_THEME` to the
name it already has (`session/lab.sh theme NAME` again, Display Properties' Apply with the same theme) is a reload too. Several
requests within 150 ms make one reload. luxshell's own Lua (`runtime.lua`, `menu.lua`, ...) is not reloaded: restart it for that.

What luxwm's reload does (`luxwmc reload-theme`, the root ClientMessage or property `_LHWM_RELOAD`, or `_LH_THEME` set to the name it
already has): it resolves the current `_LH_THEME` again and loads the frame theme anew from disk, even though the spec did not
change: the frame set's `theme.ini` and PNGs, or the `.msstyles` through wuxtheme (so a number edited in
`data/frames/jade4074/theme.ini`, e.g. `margins = 27 9 9 9`, or a re-saved style shows at once). Every frame is re-placed around
its client with the new margins (the client stays where it is, `_NET_FRAME_EXTENTS` follows), repainted, and
`_LHWM_FRAME_METRICS` / `_LHWM_COMPOSITED` are announced again; the log says `luxwm: theme reloaded (...)`. A theme that no longer
loads (a broken `theme.ini`) keeps the running one. luxwm's C code and `config.h` are not reloaded (`session/lab.sh restart-wm`).

## Settings and your themes (luxshell, SETTINGS.md)

**Where.** luxshell keeps its settings in the desktop-wide `$XDG_CONFIG_HOME/lhde/settings.ini` (default
`~/.config/lhde/settings.ini`, `LHDE_SETTINGS` for another file), which luxwm and the flxp programs share (docs/SETTINGS.md).
luxshell writes every change back in place (other programs' keys, comments and order are kept) and then sends the root
ClientMessage `_LH_SETTINGS_CHANGED`; when another program (flxp's Display Properties, a script) changes the file and sends that
message, luxshell reads it again and applies what changed, live. A hand edit is picked up by sending it yourself, e.g.
`python3 -c 'from Xlib import display,protocol,X; d=display.Display(); r=d.screen().root; r.send_event(protocol.event.ClientMessage(window=r, client_type=d.intern_atom("_LH_SETTINGS_CHANGED"), data=(32,[1,0,0,0,0])), event_mask=X.SubstructureNotifyMask); d.flush()'`,
or by restarting luxshell. `~/.local/state/lhde/luxshell-state.ini` keeps only luxshell's runtime state (the tile order; launch
counts are in `luxshell-mfu.ini`).

**Your themes.** `~/.config/lhde/themes/NAME.theme` are your theme files (docs/THEME-FORMAT.md): a `base` theme with some keys
of its own. They are listed with the shipped themes in the settings flyout (taskbar or sidebar menu Properties) and Display
Properties; `lhde-theme set NAME` or `xprop -root -f _LH_THEME 8u -set _LH_THEME NAME` selects one from a terminal. Save As (the
flyout's *Save as...*, Display Properties, `lhde-theme save-as`) writes the selected theme with its current layer as one.
Which value wins: the current layer (`settings.ini [current]`), then the theme file and its base chain, then the style's
defaults; `[desktop] wallpaper = none` (the flyout's "Show the wallpaper" off) always wins. A name that is not a theme is
ignored by everybody (the current theme stays; luxwm and luxshell log it).

**Display Properties (flxp-settings).** flxp's dialog (`flxp-settings [--tab themes|desktop|appearance|effects]`, or `flxp
--settings`; also flxp's start panel Control Panel > Display Properties...) is the full settings UI on the same file: Themes (the
shipped themes and yours, Save As..., Delete), Desktop (background, position, icon set), Appearance (explorer frame, taskbar kind,
start menu, composition, animations), Effects (font smoothing, shadows under menus, hiding underlined letters until Alt). Apply
writes settings.ini, sets `_LH_THEME` for a theme change and sends one `_LH_SETTINGS_CHANGED`; luxshell applies the theme,
composition (starts/stops picom), taskbar kind, start menu and wallpaper live, luxwm the frames and animations.

**Per-theme taskbar, composition.** The taskbar kind, the compositor switch, the icon set and the animations are theme keys
(`Shell.taskbar`, `Glass.composition`, `Desktop.icons`, `Glass.animations`; docs/THEME-FORMAT.md): a theme file says them, its
base chain or the style's defaults fill them in. Display Properties' Appearance tab and luxshell's settings flyout (and
`_LHSHELL_COMMAND taskbar KIND`) change them for the current theme only, into its current layer (`settings.ini [current]`,
"NAME (Modified)"); a value equal to what the theme gives anyway is dropped; Save As keeps it in a theme file of your own. So
aero4074 can have the classic bar while jade4074 keeps the Avalon one. luxshell applies the record at startup, on every theme
change and on `_LH_SETTINGS_CHANGED` (log `luxshell: taskbar A -> B`).

**Start menu: flxp's start panel everywhere.** flxp-taskbar's XP start panel is the start menu in every taskbar mode. With the
Avalon taskbar or none, luxshell runs `flxp-startmenu` (the same program without the bar, tray or struts; log
`$XDG_RUNTIME_DIR/luxshell-startmenu<N>.log`): it owns `_LH_STARTMENU_S0` and luxshell forwards the Avalon Start button and
Super to it (the panel opens above the Start button; Super or a click outside closes it). luxshell adopts one that already runs on
the display (also a `flxp-taskbar --start-menu-only` run by hand), restarts it when it dies (at most 5 times a minute, then nothing
opens until the settings change), stops it when you switch to the classic bar (which is the provider then) and on exit, always by
its own pid checked against the display. flxp's panel has a Control Panel cascade whose *Display Properties...* starts
flxp-settings; luxshell's settings flyout is reachable from a terminal with `xprop -root -f _LHSHELL_COMMAND 8u -set
_LHSHELL_COMMAND settings`. The Avalon Start button shows its pressed image while flxp's panel is open: the provider sets the root
property `_LH_STARTMENU_OPEN` (1 open, 0 or deleted closed; docs/CLIENT-PROTOCOL.md); with a provider that does not set it
luxshell keeps the button pressed from the click (or Super) until the panel's window unmaps, or until the next click on the button.

`settings.ini` keys of the shell (luxshell's, and the start panel's `[start] pinned`; luxshell writes the defaults of the marked ones when missing):

| Section | Key | Values |
| --- | --- | --- |
| `[theme]` | `name` | the session theme: a theme file's name, an alias or a bare style (mirrors `_LH_THEME`) |
| `[desktop]` | `wallpaper` | absent (default: the theme's `Desktop.wallpaper`) or `none` (no picture in any theme, solid #3a6ea5; `LHSHELL_WALLPAPER=0` forces it) |
| `[desktop]` | `desktop_black` * | `0` (default): a maximised window darkens only its own frame (luxwm) and the bars' black layers; `1`: the whole desktop also turns black |
| `[shell]` | `max_fade_ms` * | `0` (default): the bars' black layers switch at once when a window is maximised or restored; a number: a fade in ms (4074: 400) |
| `[taskbar]` | `start_menu` | the menu the classic bar's Start button opens: `panel` (XP's start panel, default) or `classic` (the classic popup menu); flxp-taskbar reads it at every open. Next to the Avalon bar the start menu is the panel. |
| `[taskbar]` | `autohide` * | `0` (default) or `1` |
| `[taskbar]` | `rows` * | 1 (default), 2 or 3: the top-edge drag writes it |
| `[sidebar]` | `mode` * | `full` (default), `minimized`, `pinned` |
| `[sidebar]` | `width` * | 100..600 px (default 150): the SidebarSizer drag writes it |
| `[sidebar]` | `stacking` * | `top` (default: the sidebar is above the windows with its strut, runs the full height, on top at the corner, the taskbar is shortened by its width; 4074) or `bottom` (part of the desktop: under the windows, `_NET_WM_STATE_BELOW`, no strut; the Avalon taskbar spans the full width over it and the sidebar, in every mode, ends at its top; follows taskbar autohide). Super+s toggles it live; settings flyout *Sidebar* |
| `[sidebar]` | `with_classic` * | `0` (default: no sidebar while the classic taskbar runs; its mode returns with the Avalon one) or `1` (the sidebar stays, above the classic bar) |
| `[sidebar]` | `yield_bottom` | `1` (default: the sidebar ends above another dock's bottom strut, e.g. the classic taskbar) or `0` |
| `[sidebar]` | `locked` | `1`: the width sizer does not drag (the sidebar menu's *Lock the Sidebar*) |
| `[clock]` | `pinned`, `pinned_pack` | `1` (pinned at the bottom) or `0` (a tile in the list), chosen in the pack `pinned_pack` (the clock's menu writes both); other packs use their own default: pinned in 4074-aero, 4074-slate and 5112-aero, a tile in 4051-slate |
| `[clock]` | `layout` * | `default`, `digtime`, `digtimemonthday`, `digtimeweekday`, `minical`, `cal` |
| `[start]` | `pinned` | flxp's start panel's pinned programs, `a.desktop;b.desktop` (desktop ids, `terminal`, `explorer` or a command line; default `terminal;explorer`) |
| `[quicklaunch]` | `dir` | the Quick Launch tile's folder (default `~/.config/lhde/quicklaunch/`) |
| `[commands]` | `logoff`, `standby`, `shutdown`, `restart`, `lock` | session commands ("The session" above) |
| `[commands]` | `taskbar` | the classic taskbar for `Shell.taskbar = classic` and, with `--start-menu-only`, the start-menu provider (default: `$LHSHELL_TASKBAR_CMD`, else `flxp-taskbar`) |
| `[commands]` | `picom` | the compositor command for the rule above (default: `$LHSHELL_PICOM_CMD`, else picom with `picom-glass.conf` and glx) |
| `[commands]` | `filemanager`, `run`, `search` | file manager for the places (default flxp, rox, else xdg-open); Run (default `rofi -show run` if installed, else luxshell's own run box); Search (default: luxshell's box, which starts the best-matching program) |

`~/.local/state/lhde/luxshell-state.ini` (`LHSHELL_STATE` another file): `[sidebar] tiles` = the tile order,
`notifications;clock;power` (written by Move Up / Move Down; default: the pack's list). Beside it: `luxshell-mfu.ini` (the start
menu's launch counts), `luxshell-notifications` (the notification history), `luxshell-icons/<size>/` (icons converted to PNG;
delete freely).

## Shell extras: toasts, tray icons, power (luxshell)

**Toasts need a session bus.** luxshell serves `org.freedesktop.Notifications` on the bus named by `DBUS_SESSION_BUS_ADDRESS`
(a real `lhde-session` starts one of its own when the login has none). On a desktop whose bus already has a notification daemon
(dunst), luxshell does not take the name from it (it logs "owned by another notification daemon ... toasts off" and runs without
toasts). Give the lab its own bus and start luxshell and the clients with it:

    eval $(dbus-launch --sh-syntax)          # or: dbus-run-session -- sh, then start everything from that shell
    echo $DBUS_SESSION_BUS_ADDRESS > /tmp/lab-bus    # so other terminals can join: export DBUS_SESSION_BUS_ADDRESS=$(cat /tmp/lab-bus)
    DISPLAY=:2 setsid build/bin/luxshell &
    DISPLAY=:2 notify-send "Hello" "from Longhorn"
    DISPLAY=:2 notify-send -A ok=OK -A no=Cancel "Question" "Pick one"   # prints the chosen action

(A bus started by hand is yours to stop: `kill $DBUS_SESSION_BUS_PID`.) Without `DBUS_SESSION_BUS_ADDRESS` luxshell does not
autolaunch a bus; toasts are simply off. Toasts appear above the taskbar, left of the sidebar (Longhorn's placement);
`toast_place = "sidebar"` in a pack's `shell.lua` (`data/packs/*/shell.lua`) puts them over the sidebar's top.

**Tray icons** go into the sidebar's "Notifications" tile (XEmbed system tray, `_NET_SYSTEM_TRAY_S0`). Start tray apps after
luxshell, on the same display (most also want the same session bus): any XEmbed tray app, e.g. `blueman-applet` (needs bluetoothd
and the system bus), `nm-applet`, `pasystray`, `volumeicon`, or the test icon `make build/luxshell/tests/trayicon &&
DISPLAY=:2 build/luxshell/tests/trayicon '#3399ff' &` (an ARGB circle; click cycles its colour; a second argument makes it exit
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
