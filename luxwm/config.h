#ifndef _BERRY_CONFIG_H_
#define _BERRY_CONFIG_H_

/* luxwm's build configuration (was berry's configure output from config.h.in; lhde builds it from the top-level Makefile).
   The window manager's _NET_WM_NAME stays "lhwm", like the X atoms (_LHWM_*): luxshell and flxp's log-off recognise it by
   that name (docs/LAYOUT.md "Names and tree"). */
#define LUXWM_NAME	"lhwm"
#define LUXWM_VERSION	0x017
#define LUXWM_VERSTRING	"0.1.7"
#define LUXWM_BUGREPORT	"aap@papnet.eu"

#define VERSION "0.1.7"
#define __THIS_VERSION__ VERSION
#define __WINDOW_MANAGER_NAME__ LUXWM_NAME

#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#define _POSIX_C_SOURCE 2

/* CHANGE THIS TO USE A DIFFERENT FONT */
#define DEFAULT_FONT "Monospace 10"

/* DO NOT CHANGE ANYTHING BELOW THIS COMMENT */
#define WORKSPACE_NUMBER 10

#define BORDER_WIDTH 0
#define INTERNAL_BORDER_WIDTH 0
#define TITLE_HEIGHT 0

#define MOVE_STEP 40
#define RESIZE_STEP 40
#define PLACE_RES 10

#define TOP_GAP 0
#define BOT_GAP 0

#define BORDER_UNFOCUS_COLOR 0x20292d
#define BORDER_FOCUS_COLOR 0x20292d

#define INNER_UNFOCUS_COLOR 0x2c3539
#define INNER_FOCUS_COLOR 0xac8d6e

#define TEXT_FOCUS_COLOR "#ffffff"
#define TEXT_UNFOCUS_COLOR "#000000"

#define FOCUS_NEW true
#define FOCUS_MOTION true
#define EDGE_LOCK false
#define TITLE_CENTER false
#define SMART_PLACE true
#define DRAW_TEXT true
#define JSON_STATUS true
#define FULLSCREEN_REMOVE_DEC true
#define FULLSCREEN_MAX true

#define MANAGE_DOCK false
#define MANAGE_DIALOG true
#define MANAGE_TOOLBAR false
#define MANAGE_MENU true
#define MANAGE_SPLASH false
#define MANAGE_UTILITY true

#define DECORATE_NEW true
#define FOCUS_BUTTON 1
#define MOVE_BUTTON 1
#define MOVE_MASK Mod4Mask
#define RESIZE_BUTTON 1
#define RESIZE_MASK Mod1Mask
#define POINTER_INTERVAL 0
#define FOLLOW_POINTER false
#define FOCUS_ON_CLICK true
#define WARP_POINTER false

/* lhwm: interactive move/resize applies the pointer position at most every DRAG_INTERVAL ms (README "Performance") */
#define DRAG_INTERVAL 16
/* lhwm: a caption press becomes a move only once the pointer has left a DRAG_THRESHOLD px square around the press (Windows'
   SM_CXDRAG/SM_CYDRAG = 4): hand jitter in a double-click must not move, and so un-maximise, a maximised window */
#define DRAG_THRESHOLD 4
/* lhwm: minimise/restore/maximise animations (README "Animations"). With a compositor picom animates (picom-glass.conf);
   without one lhwm flies the caption to/from the taskbar button like XP's DrawAnimatedRects (IDANI_CAPTION, 250 ms),
   or draws XOR zoom rectangles when ANIMATE_WIREFRAME is true */
#ifndef ANIMATE
#define ANIMATE true
#endif
/* lhwm: the M5b maximise/restore plate crossfade of DWM themes with plate_max (README "Maximised mode"), in ms. 0 (default since
   2026-10-25, user decision): no fade, the plate switches in the same paint as the geometry change, no timer; e.g. 400 restores the
   fade in step with lux's black layers (which are instant now too) */
#ifndef MAX_FADE_MS
#define MAX_FADE_MS 0
#endif
#ifndef ANIMATE_WIREFRAME
#define ANIMATE_WIREFRAME false
#endif
/* lhwm: which compositor animation set picom-glass.conf applies (the value lhwm writes to _LHWM_ANIMATE; README "Animations",
   COMPOSITION.md 14): ANIM_4074 = Longhorn 4074's DWM (open and restore from minimised fade in over 350 ms with a slight scale-in
   standing in for the 3D swing; minimise, close, maximise instant), ANIM_5048 = the April 2005 uDWM curve (266 ms, sqrt progress),
   ANIM_INVENTED = lhwm's earlier 250 ms shrink to the bottom edge on minimise and picom's geometry animation on maximise (not
   Longhorn), ANIM_4074PLUS = 4074's fade + gentle centre zoom on open, the invented grow on restore from minimised and shrink on minimise, no picom
   animation on maximise/restore (default). Without a compositor the XP caption flight is used whatever this says.
   Runtime (2026-10-25): this is only the default. lhwm reads [theme] animations = 4074plus | 4074 | invented | 5048 | none from
   lhde's settings store (~/.config/lhde/settings.ini, liblhde; docs/SETTINGS.md) at startup and on the root ClientMessage _LH_SETTINGS_CHANGED, and
   LHWM_ANIMATIONS=<name> overrides the file; "none" (value 0) sets no _LHWM_ANIMATE at all and turns off the XP caption flight too. */
#define ANIM_4074 1
#define ANIM_INVENTED 3
#define ANIM_5048 5
#define ANIM_4074PLUS 7
#ifndef ANIMATE_STYLE
#define ANIMATE_STYLE ANIM_4074PLUS
#endif

#endif
