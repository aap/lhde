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
- **Ownership.** The wux side owns `wux/ wuxtheme/ flxp/`, the lux side `luxwm/ luxnav/ luxshell/ session/`; `liblhde/ share/
  docs/ tools/` are shared and change by agreement. Two sessions may be working in this tree at once: keep commits small and
  do not move files another side owns.

- **Leave nothing behind.** A script or program that starts something stops it: `lhde-session stop` and `lab.sh stop` must end
  every process their start began (the window manager, the shell, picom, the session bus and what lives on it, the X server
  they started). Check with `pgrep` after a stop. Leaked daemons once used up the user's inotify instances.
- **Search inside the repository only.** Never run a file search (Glob, find, bfs, grep -r) rooted at the home directory or
  anywhere under `/u/aap/n` (network mounts); the research workspace `~/fun/longhorn` is read by the paths a brief names, not
  crawled. A search of the whole home directory walks the NFS mounts and holds thousands of descriptors for minutes.

- **`todo_ideas/` is the issue tracker**, aap's notes by area (flxp_wux, luxwm, luxshell, other_programs, xattribs, ...). An item
  that is done leaves the file in the same commit that does it; an item that turns out wrong or decided against is removed with a
  one-line reason in the commit message. Add what you find (a bug seen twice, a leftover) as a bullet under the right area, briefly.

## Working here

- Build: `make -j` (one build at a time on this machine: parallel builds in several worktrees have frozen it); everything runs
  uninstalled from `build/bin`. Tests: `make check` (wux tests, headless Xvfb scripts, ut-dump over the imported styles, liblhde's
  resolver, luxshell's Lua, luxwm's frame regression, and `check-free`: every theme resolved against a tree holding only the free
  ReactOS import, skipped without the ReactOS ISO). Keep it green; the frame regression
  must stay byte-identical unless the change is to frames, and then say so.
- See it: `LAB=:9 session/lab.sh start` (Xvfb + luxwm + picom), `session/lab.sh shell`, then `DISPLAY=:9 build/bin/flxp-desktop`,
  `build/bin/flxp DIR`, `build/bin/flxp-settings`; `session/lab.sh theme NAME` switches live; `session/lab.sh shot FILE` takes
  a screenshot; `session/lab.sh stop` cleans up. The lab uses picom's xrender backend; aap's real session (display `:1`) uses
  glx, and some things only show there. Do not kill or restart programs on `:1`.
- Where does this pixel come from: `build/bin/lhde-theme explain THEME`; `lhde-theme resolve THEME --why`; `lhde-theme dirs`.
- Files: `~/.config/lhde` (what the user chose: settings.ini, themes/, flxp.ini, keys), `~/.local/state/lhde` (what programs
  remember on their own), `~/.local/share/lhde-windows` (imported Windows files; `lhde-import` is its only writer), the install's
  `share/lhde` (ours; the source tree's `share/` when run uninstalled). No other directory is read; liblhde gives every path.
  Never write into the Windows tree by hand.
- Names: X atoms (`_LH_*`, `_LHWM_*`, `_LHSHELL_*`), the WM name "lhwm" and the C prefixes (`ut_`, `lhnav_`, `lhde_`) are
  stable; do not rename them.
