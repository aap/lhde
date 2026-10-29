# lhde

The Longhorn desktop environment: a Windows XP / Longhorn-era desktop for X11. Read docs/ARCHITECTURE.md first (the map),
then README.md (build, install, run). docs/README.md lists every document.

## Rules

- **No Microsoft files in the tree, ever** (docs/LAYOUT.md "Asset policy"): no `.msstyles`, DLLs, fonts, wallpapers, extracted
  or rendered bitmaps, no screenshots of them, not in tests either. Art is read at run time from the user's imported Windows
  tree. `lhde-resources-*` packs built in the tree are gitignored and stay that way.
- **Docs describe, they do not journal.** A current document says how things are now, with no dated status blocks; what
  changed goes in the commit message. Design journals live in `docs/history/` and are never updated. When you change a shared
  surface (liblhde's API, a theme-file key, an X property in docs/CLIENT-PROTOCOL.md or docs/SETTINGS.md), update the doc in the
  same commit.
- **Fidelity is the point.** Measurements against the real builds beat taste; when a choice is taste (the "vision" themes), say
  so where it is made. Reference material (VM screenshots, extracted builds, COMPOSITION.md) is in `~/fun/longhorn`, outside the
  repo.
- **Ownership.** The wux side owns `wux/ wuxtheme/ flxp/`, the lux side `luxwm/ luxnav/ luxshell/ session/`; `liblhde/ data/
  docs/ tools/` are shared and change by agreement. Two sessions may be working in this tree at once: keep commits small and
  do not move files another side owns.

## Working here

- Build: `make -j`; everything runs uninstalled from `build/bin`. Tests: `make check` (wux tests, headless Xvfb scripts, ut-dump
  over the imported styles, liblhde's resolver, luxshell's Lua, luxwm's frame regression). Keep it green; the frame regression
  must stay byte-identical unless the change is to frames, and then say so.
- See it: `LAB=:9 session/lab.sh start` (Xvfb + luxwm + picom), `session/lab.sh shell`, then `DISPLAY=:9 build/bin/flxp-desktop`,
  `build/bin/flxp DIR`, `build/bin/flxp-settings`; `session/lab.sh theme NAME` switches live; `session/lab.sh shot FILE` takes
  a screenshot; `session/lab.sh stop` cleans up. The lab uses picom's xrender backend; aap's real session (display `:1`) uses
  glx, and some things only show there. Do not kill or restart programs on `:1`.
- Where does this pixel come from: `build/bin/lhde-theme explain THEME`; `lhde-theme resolve THEME --why`; `lhde-theme dirs`.
- Settings and files: `~/.config/lhde` (the user's), `~/.local/share/lhde-windows` (imported Windows files; `lhde-import` is its
  only writer), the install's `share/lhde` (ours). Never write into the Windows tree by hand.
- Names: X atoms (`_LH_*`, `_LHWM_*`, `_LHSHELL_*`), the WM name "lhwm" and the C prefixes (`ut_`, `lhnav_`, `lhde_`) are
  stable; do not rename them.
