# tools

- `lhde-pack`: a resource pack of raw, unmodified Windows files, from ISOs (or WIMs, Windows directories) by the rules in
  `share/sources/<source>.ini`; `<source>/{system32,themes,fonts,wallpapers}` + `MANIFEST`, as a directory and a `.tar.zst`.
- `lhde-import`: fills the Windows tree (`$LHDE_WINDOWS`, else `~/.local/share/lhde-windows`: `<source>/` as in the pack,
  `local/`, `generated/`, `index/`; docs/LAYOUT.md "The three trees") from a pack (`--pack`), or from an ISO, WIM or Windows
  directory (`--iso`, `--wim`, `--dir`: packed into a temporary pack first, the build detected), a dropped-in style (`--style`),
  aap's workspace (`--from-workspace`); then runs the recipes (frame sets, luxshell's pack images, the aurora scenes) into `generated/` with a
  manifest each (`--regenerate` makes all of it again, `--reindex` the symlinks). docs/USAGE.md "Getting the Windows resources".
- `lhde-schemes` (`lhdeschemes.py`): the classic Appearance schemes from a Windows 2000 / XP / Server 2003 medium's
  `HIVEDEF.INF` (or the ISO) into the Windows tree's `local/schemes/` (looked up as `styles/<scheme>.scheme`); lhde-import runs
  it for the sources' `[schemes]` into `generated/schemes/`.
- `lhdeimport.py`: their code (PE resources, the source rules, 7z/WIM/setup-layout readers, the recipes); `lhdetree.py`: the
  Windows tree and its index; `wim0108.py`: build 4015's pre-release WIM format. Installed to
  `$PREFIX/lib/lhde/tools` (the commands to `$PREFIX/bin`).
- Recipe builders run by lhde-import: `msstyles2theme.py` (a classic frame set's PNG cache from a .msstyles),
  `build_aerovision.py` (the vision caption buttons), `build_aero5112.py` (the 5112 frame set from its DWMWindow streams).
- The aurora: `lhdebaml.py` decodes Longhorn's binary XAML (the 2003 dialect of jade.msstyles' PreviewPane.AURORA stream, the
  2004 one of Microsoft.Windows.WCPClient.dll's resources; `lhdebaml.py FILE.baml`, `--list DLL`, `--resource DLL NAME` print
  XAML); `lhdeaurora.py` flattens a scene into its layer list (lhde-import's `[aurora]` recipes, into
  `generated/aurora/<scene>-<source>.json`) and writes examples/aurorademo's C tables from those (`make examples`).

ut-dump stays with wuxtheme/; lhde-theme (the resolver's command line) with liblhde/.

Both `lhde-pack` and `lhde-import` write file annotations (extended attributes, docs/ANNOTATIONS.md; `--no-annotate` to skip);
`lhde-import --annotate` adds them to an existing tree.
