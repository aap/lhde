# examples: reference clients

    make            build/examples/extendframe, navdemo, aurorademo (not installed)

- extendframe.c: luxwm's extended glass frame client (docs/CLIENT-PROTOCOL.md): an Explorer-style 60 px glass header.
- navdemo.c: luxnav's reference client (docs/NAVBAR.md): a small file browser with the vision header (`aerovision` theme).
- aurorademo.c: Longhorn's preview-pane and desktop aurora, played from their original numbers. Those are read from your
  own Windows files: lhde-import's `[aurora]` recipes (share/sources/4074.ini: jade.msstyles' PreviewPane.AURORA stream and
  Microsoft.Windows.WCPClient.dll's desktopaurora.baml, decoded by tools/lhdebaml.py) write the Windows tree's
  `generated/aurora/{previewpane,desktop}-4074.json`, and make turns them into `build/examples/aurora_data.h`
  (tools/lhdeaurora.py, at every make; rewritten only when it changes). A scene the tree lacks is left out with a message
  at build time, and `aurorademo pane|desktop` says so when asked for it. A Windows tree imported before the DLL was in
  the recipe gets it with `lhde-import --iso longhorn4074.iso` (docs/USAGE.md "Getting the Windows resources").

The comparison screenshots stay in the research workspace (asset policy).
