# Theming without Microsoft files, and a theme installer

Status: the free baseline is in (the ReactOS import and the free pack with the Pexels photos, every theme resolved against it by
`make check-free`; docs/THEME-FORMAT.md section 6, docs/USAGE.md "Getting the Windows resources"); section 1's nothing-imported
points and the installer (section 3) are open, not started. The smaller theme gaps are in plan/BUGS.md. See plan/STATUS.md.

aap (2026-10-31): a fully working desktop without any Microsoft resources, with free Wine/ReactOS fallbacks for everything
(except maybe the Longhorn-specific parts; we'll see): that probably means classic mode, but there may be free msstyles around.
Then a graphical theme installer: something that provides the base shell files/resources of the OS and a way to add new themes.
A test for wux; inspiration: 3683's (broken) Avalon settings dialog, and what Vista made of it.

## 1. The desktop with nothing imported (the baseline)
aap: Wine's icons have no resource ids, ReactOS's do: an all-ReactOS-compatible mode. The ReactOS source and the free fallback
chain are in; a desktop with only ReactOS imported is the free baseline. Left:
* With nothing imported at all (not even ReactOS): Marlett's glyphs come out as letters unless the system has Wine's Marlett;
  ship or fetch it, or draw the glyphs (luxwm had bitmaps that were bit-exact at 16x14); a fontconfig alias set for Tahoma /
  Microsoft Sans Serif without an import.
* Cursors: an X cursor theme in XP's shapes (free ones exist); ReactOS's are user32.dll resources. (wux already takes the X
  cursor theme's shapes.)
* Sounds, wallpaper: a generated classic wallpaper (the teal), none for sounds.

## 2. Free visual styles
* Community XP styles: licences vary; the installer can import any .msstyles the user has (`lhde-import --style`,
  `lhde-import --styles DIR` exist).

## 3. The theme installer (a wux program)
* Provides the base resources: pick an ISO / WIM / resource pack / Windows directory, see what builds it holds, import with
  progress (lhde-import does the work; the dialog is the face), then what themes became complete (lhde-theme explain per
  theme, "missing -> fallback" shown as what the user would notice).
* Adds themes: a .msstyles dropped in (+ shellstyle), a theme file, a frame set recipe; Appearance's "Windows and buttons"
  lists only the styles a theme file names, so adding a style means adding a theme for it.
* Look: 3683's Avalon settings dialog and Vista's Personalization as inspiration; XP's Display Properties stays the classic
  path. Where it lives: Display Properties' Themes tab ("Install theme...") and a standalone `lhde-setup`.
* Also the place for lhde-theme dirs' view of the three trees.
