# docs

Current documents describe lhde as it is. They carry no dated status blocks: what changed and when is in the commit log, and
the design journals of the merge and before are in `history/` (kept unchanged, never updated; nothing current should need them).

| file | what |
| --- | --- |
| `ARCHITECTURE.md` | the map: the components, what runs in a session, how they talk, where to look first |
| `LAYOUT.md` | the files on disk: the asset policy and the three trees (program files, the Windows tree, the user's) |
| `THEME-FORMAT.md` | the theme files: components, the file, inheritance, the resolver, defaults, fallbacks, the shipped themes |
| `SETTINGS.md` | the settings store (`~/.config/lhde/settings.ini`), change notification, who writes what |
| `CLIENT-PROTOCOL.md` | luxwm's client protocol: extended glass frames, the `_LHWM_*` properties, the vision header |
| `USAGE.md` | the user guide: where a pixel comes from, importing the Windows resources, themes, the shell, the lab |
| `LUXWM.md` | the window manager: frames (glass, msstyles, Windows Classic), EWMH, animations, the regression |
| `NAVBAR.md` | luxnav, the Longhorn navigation bar: geometry, presets, art, the flxp integration |
| `SHELL-DESIGN.md` | luxshell: the three shells reproduced, fact sheets, the architecture, the milestone designs |
| `MSSTYLES-FORMAT.md` | the `.msstyles` file format as wuxtheme reads it |
| `ANNOTATIONS.md` | file annotations as extended attributes (lhde-import, lhde-pack) |
| `history/` | journals: `flxp-NOTES.md` (flxp's implementation notes), `THEMES.md` (the registry before theme files), `LAYOUT-draft.md` (the merge plan and the step reports), `THEME-FORMAT-status.md` (open points, decisions, status of the theme format), `CLASSIC-DECO-NOTES.md` |

Still to do in this spirit: `SETTINGS.md`, `CLIENT-PROTOCOL.md`, `SHELL-DESIGN.md` and `NAVBAR.md` carry current content under
dated headers ("Additions 2026-10-24", "M8 ... status"); each wants one pass that keeps the description and drops the diary.
Per-component READMEs (`luxwm/README.md`, ...) say what a directory holds and how to build it; the docs here say how it works.
