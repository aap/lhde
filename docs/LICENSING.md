# Licensing: which licence covers which part

What the repository holds, under which licence, and where the notice is. This is a record of the facts, not legal advice.

| what | licence | notice |
| --- | --- | --- |
| lhde's own code and text: everything not named below (wux's toolkit and shim, wuxtheme, flxp, luxnav, luxshell, liblhde, session/, tools/, share/, tests/, examples/, the docs) | GNU GPL version 2 | `LICENSE` (the GPL version 2 text); the source files carry no per-file notice |
| `wux/vendor/wine/`: Wine's comctl32, user32, uxtheme files and headers, with lhde's `<file>.patch` beside the files it changed | GNU LGPL 2.1 or later (Wine's terms) | `wux/vendor/wine/LICENSE` (Wine's notice), `wux/vendor/wine/COPYING.LIB` (the LGPL 2.1 text, Wine's own file); `wux/vendor/wine/MANIFEST` names each upstream file with the Wine tag and commit it was taken from |
| `wux/rc/comctl32.res`, `wux/rc/comdlg32.res`, `wux/w32/stdres.res`: Wine's resources compiled with windres | as Wine's sources, LGPL 2.1 or later | `wux/vendor/wine/MANIFEST` (last section): the Wine sources, the tag and commit (wine-11.18, 7b3fff76fa5; each file rebuilds byte for byte from it) and the command (`wux/rc/build.sh`, `wux/w32/mkstdres.py`) |
| `flxp/res/icons/*.ico`, `*.bmp`: Wine's shell32 icons, copied unchanged | as Wine's sources, LGPL 2.1 or later | `wux/vendor/wine/MANIFEST`; `flxp/res/icons/wine.theme` says so too |
| luxwm's berry parts: `wm.c`, `client.c`, `config.h`, `globals.h`, `ipc.h`, `types.h`, `utils.c`, `utils.h`, the man pages (luxwm is a fork of berry by Joshua L Ervin) | MIT | `luxwm/LICENSE` (berry's MIT notice); `luxwm/README.md` lists the files from berry and those written for lhde |

The vendored Wine objects are linked into `libwux.a` and `libwuxtheme` (`.a` and `.so`), so the flxp programs, luxwm, luxnav,
luxshell and the examples contain LGPL code from Wine. Some of lhde's own files say in their header comments that they follow
named Wine functions (`wux/gdi/font.cpp` "lifted from Wine's dlls/win32u/freetype.c", `wux/w32/gdi_draw.cpp` "follows Wine's
dlls/win32u/dibdrv"); they are not in the MANIFEST.

## Files the user imports

None of these are in the repository (docs/LAYOUT.md "Asset policy"). `lhde-import` copies them from the user's own media into
the Windows tree (`~/.local/share/lhde-windows`), and the import is the user's act; lhde does not download or redistribute
them, but for the free pack's files (below).

- **Microsoft's files** (visual styles, DLLs, fonts, wallpapers, from the user's ISOs, WIMs or a Windows directory through
  `lhde-pack` and `lhde-import`): under Microsoft's licence terms.
- **ReactOS's files** (`make install-resources-free`, `lhde-import --iso` on ReactOS's boot CD): free, so lhde publishes them,
  outside the repository, in the free pack `lhde-resources-free-1.zip`, which `make install-resources-free` downloads
  (`make free-pack` makes it from the CD). ReactOS is under the GNU GPL version 2 or later, parts under the GNU LGPL 2.1 or
  later (the DLLs it shares with Wine) and a few under version 3; the corresponding source is ReactOS's tag 0.4.16. The fonts
  carry their own free licences (SIL OFL 1.1, Apache 2.0, the Bitstream Vera licence, the GUST Font License, GPL 3 with the
  font exception for Courier New, LGPL 2.1 for Wine's Symbol and Wingdings, a permissive notice in Marlett). ReactOS's
  wallpapers are not taken (three of them are CC BY-NC). The pack carries ReactOS's `COPYING` files and `CREDITS`, the
  fonts' licence texts and a `LICENSE` note naming each (share/sources/free-pack-LICENSE.txt).
- **The free themes' wallpapers** (the source `free`, share/sources/free.ini, in the same pack): four photos from Pexels,
  chosen by aap, under the Pexels licence (free to use and modify, also commercially, without attribution:
  https://www.pexels.com/license/); `free/wallpapers/LICENSE.txt` (share/sources/free-wallpapers-LICENSE.txt) names each
  photographer and the photo's page.
- **Icon packs** (`lhde-import --icons DIR NAME --licence FILE`): the pack's own licence, copied beside it into
  `local/icons/NAME/`. The one the shipped recipe `share/icons/xp-hires.theme` names, marchmountain's "Windows XP High
  Resolution Icon Pack", is published under CC0; the recipe (the mapping of icon keys to file names) is lhde's text.
