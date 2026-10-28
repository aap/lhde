# tools

- `lhde-pack`: a resource pack of raw, unmodified Windows files, from ISOs (or WIMs, Windows directories) by the rules in
  `data/sources/<source>.ini`; `<source>/{system32,themes,fonts,wallpapers}` + `MANIFEST`, as a directory and a `.tar.zst`.
- `lhde-import`: fills the data directory (`$LHDE_DATA`, else `~/.local/share/lhde`) from a pack (`--pack`), or from an ISO,
  WIM or Windows directory (`--iso`, `--wim`, `--dir`: packed into a temporary pack first, the build detected), a dropped-in
  style (`--style`), aap's workspace (`--from-workspace`); then runs the recipes (frame sets, luxshell's pack images) into
  `cache/` directories with a manifest each (`--rebuild-cache`). docs/USAGE.md "Getting the Windows resources".
- `lhde-schemes` (`lhdeschemes.py`): the classic Appearance schemes from a Windows 2000 / XP / Server 2003 medium's
  `HIVEDEF.INF` (or the ISO) into `styles/<scheme>.scheme`; lhde-import runs it for the sources' `[schemes]`.
- `lhdeimport.py`: their code (PE resources, the source rules, 7z/WIM/setup-layout readers, the recipes); `wim0108.py`: build
  4015's pre-release WIM format.
- Recipe builders run by lhde-import: `msstyles2theme.py` (a classic frame set's PNG cache from a .msstyles),
  `build_aerovision.py` (the vision caption buttons), `build_aero5112.py` (the 5112 frame set from its DWMWindow streams).

ut-dump stays with wuxtheme/; lhde-theme (the resolver's command line) with liblhde/.
