# tools

- `lhde-import`: fills the data directory (`$LHDE_DATA`, else `~/.local/share/lhde`) with the user's Windows art under
  build-qualified names: styles/, frames/ (pixels), packs/<build>/, resources/<build>, fonts/<build>, wallpapers/<build>
  (docs/LAYOUT.md "Asset policy"). First version: `--from-workspace DIR` copies what the Longhorn research workspace already
  has; importing from an ISO, a WIM or a Windows directory is step 4. `lhde-import --help`, `--list`.
- Recipe builders (they need extracted builds; research tools until lhde-import runs them): `msstyles2theme.py` (a classic
  frame set from a .msstyles: theme.ini tuning keys + PNG cache), `build_aerovision.py` (the vision caption buttons),
  `build_aero5112.py` (the 5112 frame set from its DWMWindow streams). msstyles2theme.py writes default_icon as a data-directory path and msstyles as the absolute path it was given (edit it to
  styles/NAME for data/frames); the two build_*.py still name the x11 layout in their defaults (OUT, AERO_DIR).

ut-dump stays with wuxtheme/.
