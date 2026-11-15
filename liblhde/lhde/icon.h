/* liblhde: the icon of a window or a program, one answer for every consumer (docs/THEME-FORMAT.md section 9: the window
   manager's caption and Alt+Tab switcher, luxshell's and flxp-taskbar's task buttons, the Explorer's Open With list).

   The order, first answer wins:
     1. own       the window's _NET_WM_ICON, which the caller read (liblhde has no display) and passes in
     2. class     the class map: [classes] of icons/apps.ini in every data directory (the user's first, then the shipped
                  share/icons/apps.ini): "PATTERN... = KEY", WM_CLASS class or instance names, case-insensitive globs
                  (fnmatch); KEY an icon key of the icon set recipes (cmd, display, notepad, ...), else an XDG icon name or
                  an absolute path
     3. desktop   the program's .desktop file (applications/ of $XDG_DATA_HOME and of each $XDG_DATA_DIRS entry): the one
                  given, else the one whose StartupWMClass is the class or instance, else the one named like them (without
                  case); its Icon= through the XDG icon theme (the GTK theme, its Inherits, hicolor; /usr/share/pixmaps; PNG
                  files) or as an absolute path
     -            (the caller's WM_HINTS icon_pixmap goes here: LHDE_APPICON_NO_GENERIC stops before 4, the caller tries its
                  hint, then asks again with no names for 4)
     4. generic   the generic application icon: the icon set's `exe` key (shell32.dll's IDI_APPLICATION art)
   A key is looked up in the session's icon set (the resolved theme's path.icons, Desktop.icons), then that recipe's
   [theme] fallback recipes, then the free chain xp, 2k, reactos, then Wine's set (flxp's res/icons/wine.theme): the first
   whose source file exists. */
#ifndef LHDE_ICON_H
#define LHDE_ICON_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum lhde_appicon_step { LHDE_APPICON_NONE, LHDE_APPICON_OWN, LHDE_APPICON_CLASS, LHDE_APPICON_DESKTOP, LHDE_APPICON_GENERIC };
#define LHDE_APPICON_NO_GENERIC 1   /* flags: stop after step 3 (NULL when nothing answered) */

/* the icon for a window or program at about size px: one image in _NET_WM_ICON's layout, { width, height, then width*height
   ARGB pixels (straight alpha, 0xAARRGGBB) } as unsigned longs, malloc'ed (free it); *n = the number of longs. The image is
   the source's nearest size at or above size (else its largest); the caller scales it. wm_class, instance, desktop_id may be
   NULL; own/nown the window's _NET_WM_ICON (NULL, 0 for none). step (may be NULL) gets which step answered, why (may be NULL)
   a line saying what was used ("class map: urxvt = cmd (apps.ini): pe:/.../cmd.exe#IDI_APPICON"). NULL when nothing did. */
unsigned long *lhde_app_icon(const char *wm_class, const char *instance, const char *desktop_id, int size,
                             const unsigned long *own, unsigned long nown, int flags, unsigned long *n, int *step,
                             char *why, size_t nwhy);
/* the class map's answer alone: the KEY for a class or instance (NULL when no pattern matches) */
const char *lhde_app_icon_key(const char *wm_class, const char *instance, char *key, size_t nkey);
/* an icon key of the icon sets (the order above, without the class map and the desktop files): the image as lhde_app_icon
   gives it; NULL when no set has a source for it */
unsigned long *lhde_icon_key_image(const char *key, int size, unsigned long *n, char *why, size_t nwhy);
/* the icon set the keys are looked up in: an icons/NAME.theme name or a recipe path (a client that has the resolved theme's
   path.icons); NULL goes back to the session theme's, resolved on the next lookup. Also forgets the cached recipes, desktop
   files and icon themes: call it after a theme switch */
void lhde_app_icons_set(const char *icons);
/* a PE file (an .exe or .dll of the imported tree) has the resource type / id (RT_BITMAP 2, RT_GROUP_ICON 14, ...): 1 or 0 */
int lhde_pe_has_resource(const char *path, unsigned type, unsigned id);

#ifdef __cplusplus
}
#endif
#endif
