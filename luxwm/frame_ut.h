/* luxwm: the libuxtheme theme source (frame_ut.c; README "libuxtheme source"). Internal to frame.c / frame_ut.c.
   Without HAVE_UXTHEME every function is a stub that reports "no library", so frame.c falls back to the PNG cache. */
#ifndef LHWM_FRAME_UT_H
#define LHWM_FRAME_UT_H
#include "frame.h"

int  fut_available(void);                      /* 1 when built with libuxtheme */
/* open the visual style (color/size: scheme and size names, NULL/"" = the style's first) into t->ut; 1 on success */
int  fut_open(struct theme *t, const char *path, const char *color, const char *size);
void fut_close(struct theme *t);
/* msstyles mode: [SysMetrics] (SIZINGBORDERWIDTH, CAPTIONBARHEIGHT, CAPTIONFONT, CAPTIONTEXT, INACTIVECAPTIONTEXT) and the Window
   parts' metadata (IMAGECOUNT, true size, SIZING/CONTENT/CAPTIONMARGINS, SIZINGTYPE, TEXTSHADOW*) into t; aa_set / hint_set: the
   theme.ini set text_antialias / text_hinting (otherwise defaults are derived from the style). 1 when the style has a CAPTION */
int  fut_ms_load(struct theme *t, int aa_set, int hint_set);
/* DrawThemeBackground of Window part p (enum mspart_id) in state (uxtheme state id, 1-based) into (x, y, w, h): rendered into a
   w x h image and painted OVER at x, y (as frame.c's ms_draw paints its nine-grid image) */
void fut_ms_draw(cairo_t *cr, const struct theme *t, int p, int state, int x, int y, int w, int h);
/* a theme-directory PNG name (plate_active.png, close.png, default_icon.png, ...) from the style's CompositedWindow PNGSTREAMs;
   NULL when the name is not one of them or the style does not have it (the caller then reads the file) */
cairo_surface_t *fut_png(const struct theme *t, const char *name);
void fut_add_font_dir(const char *dir);
#endif
