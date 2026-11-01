/* luxwm: Longhorn-style frame plates painted into ARGB decoration windows */
#ifndef LHWM_FRAME_H
#define LHWM_FRAME_H
#include <X11/Xlib.h>
#include <cairo/cairo.h>

enum hit { HIT_NONE, HIT_CAPTION, HIT_CLOSE, HIT_MAX, HIT_MIN,
           HIT_L, HIT_R, HIT_T, HIT_B, HIT_TL, HIT_TR, HIT_BL, HIT_BR };
enum btn { BTN_CLOSE, BTN_MAX, BTN_MIN, BTN_RESTORE, BTN_LAST };
enum { THEME_DWM, THEME_MSSTYLES };
/* classic msstyles parts (uxtheme WP_*), see LUXWM.md "classic msstyles mode" */
enum mspart_id { MP_CAPTION, MP_MAXCAPTION, MP_SMALLCAPTION, MP_FRAMELEFT, MP_FRAMERIGHT, MP_FRAMEBOTTOM,
              MP_CLOSE, MP_MAX, MP_MIN, MP_RESTORE, MP_HELP, MP_SMALLCLOSE, MP_LAST };
struct mspart {
    int count;                          /* IMAGECOUNT (states in the strip) */
    int nvar, vw[6], vh[6];             /* image variants: 0 = IMAGEFILE, 1..5 = IMAGEFILE1..5 (glyphs for buttons) */
    cairo_surface_t *img[6][10];        /* [variant][state-1] */
    int sm[4], cm[4];                   /* SIZINGMARGINS, CONTENTMARGINS: left right top bottom */
    int capm[4], has_capm;              /* CAPTIONMARGINS */
    int tile;                           /* SIZINGTYPE = TILE (else STRETCH) */
    int sh_type[2], sh_dx[2], sh_dy[2]; /* text shadow per frame state (active, inactive): 0 none, 1 single, 2 continuous */
    double sh_rgb[2][3];
    int ut_part;                        /* libuxtheme: the Window part id (0 = none; LUXWM.md "libuxtheme source") */
    int ut_live;                        /* drawn by ut_draw_background; 0: from img[][] (the library's bitmaps for IMAGEGLYPH buttons) */
};

struct theme {
    cairo_surface_t *plate[2];          /* active, inactive */
    cairo_surface_t *overlay, *fog, *glow_btn, *glow_close;
    cairo_surface_t *btn[BTN_LAST];     /* sprite sheets, 4 frames of btn_h: normal, hot, pressed, inactive */
    int pi_t, pi_r, pi_b, pi_l;         /* plate 9-slice insets (texture regions kept unstretched) */
    int m_t, m_r, m_b, m_l;             /* frame margins: where the client sits inside the plate */
    int oi_t, oi_r, oi_b, oi_l, oo_t, oo_r, oo_b, oo_l;  /* overlay insets; overlay outset per side (how far the overlay texture reaches past the plate) */
    int btn_h, btn_top, btn_right, btn_gap, title_x, icon_x, icon_size;
    int order[3], norder;               /* button order from the right edge, as enum btn */
    char font[64];
    double text_a[3], text_i[3], glow[3];
    int mode;                           /* THEME_DWM (glass plates) or THEME_MSSTYLES (classic uxtheme frames) */
    int shape_alpha;                    /* decoration pixels with alpha below this are cut out of the window shape */
    int ms_border, ms_cap_h;            /* [sysmetrics] SIZINGBORDERWIDTH, CAPTIONBARHEIGHT */
    int ms_text_dy;                     /* caption text baseline correction (font metric differences), px */
    char ms_font[96];
    int ms_font_aa;                     /* cairo_antialias_t for the caption text */
    int ms_hint;                        /* cairo_hint_style_t (0 = default: full) */
    struct mspart *ms;                  /* [MP_LAST] */
    char fallback[256];                 /* fallback_theme: used instead of a DWM theme while no compositor runs */
    int ext_r0, ext_r1;                 /* extend_rows: caption-band source rows [r0, r1) stretched by an extended top (default 2, m_t - 2) */
    /* Explorer plate (LgFrameExplorer, msstyles STREAM 484): used instead of plate[] for an extended frame whose top reaches into the
       client (LUXWM.md "Explorer plate"). NULL = none (the normal plate's caption rows are stretched as before) */
    cairo_surface_t *plate_x[2];        /* active, inactive */
    int xi_t, xi_r, xi_b, xi_l;         /* explorer_insets: its 9-grid (desksrv: top 73, the rest like the normal plate) */
    int x_r0, x_r1, x_line;             /* explorer_rows: band rows [r0, r1) stretched to fit the header, inner light/dark lines at
                                           rows x_line, x_line + 1 (they end up on the header's last two rows) */
    double glass_alpha;
                                        /* glass_alpha: plate alpha factor (plates only; 1 = as the texture says) */
    int fog_on;                         /* theme key fog = 1: draw captionfog.png over extended headers (COMPOSITION.md 15: 4074 has no fog band; default 0) */
    int glass_square;                   /* glass_alpha_mode = square: plate alpha a -> a * a (5048's double application), then * glass_alpha */
    /* maximised plates (LUXWM.md "Maximised mode"): LGFRAMEMAX / MAXEXPLORER (msstyles STREAM 493 / 494) drawn instead of the
       squared normal plate while maximised. NULL = none (the squared normal plate) */
    cairo_surface_t *plate_m[2], *plate_mx[2];   /* active, inactive */
    int mi_t, mi_r, mi_b, mi_l;         /* max_insets (default: plate_insets) */
    int mxi_t, mxi_r, mxi_b, mxi_l;     /* max_explorer_insets (default: max_insets) */
    int mx_r0, mx_r1, mx_line;          /* max_explorer_rows: as explorer_rows, for the MAXEXPLORER texture */
    int mbtn_right, mbtn_top;           /* max_button_right / max_button_top: button offsets while maximised (default: button_right/top) */
    int overlay_on;                     /* overlay = 0: no LGFRAMEOVERLAY edge highlight (Jade 4066: none visible; default 1) */
    /* vision header (LUXWM.md "Aero vision", docs/NAVBAR.md): header_mode = crop (x_crop) draws the Explorer / MAXEXPLORER plate's rows
       1:1 from the top down to the header and puts the texture's inner lines on its last two rows (never scaled vertically; rows past
       the band repeat its last row); deco_h > 0: the frame's own decoration is the top deco_h rows (the caption buttons' hit
       rectangles and _LHWM_CAPTION_RECT are clipped to it; _LHWM_FRAME_METRICS field 18); cap_border >= 0: the rows kept above a
       CLIENT_CAPTION client instead of the side border (theme_caption_border). Defaults 0, 0, -1: the old behaviour */
    int x_crop, deco_h, cap_border;
    unsigned gen;                       /* load count (frame.c: identifies the theme in the crossfade cache) */
    /* maximised caption text (LUXWM.md "Maximised caption text"): text_max_active / text_max_inactive / glow_max (default: the
       normal colours; glow_max = none: no TextGlow while maximised). DWM themes fade them with the plate crossfade */
    double text_ma[3], text_mi[3], glow_m[3];
    int glow_m_none;
    /* default window icon for clients without one (default_icon = FILE..., or default_icon.png): DWM CompositedWindow DEFAULTICON,
       classic/Luna the XP application icon (shell32 #3) */
    cairo_surface_t *defic[4]; int ndefic;
    /* libuxtheme source (LUXWM.md "libuxtheme source"): the open style (ut_theme *), NULL = the theme directory's PNG cache. msstyles
       mode then draws the Window parts live (frame_ut.c), DWM mode took its textures from the CompositedWindow streams */
    void *ut;
    int ut_fmt;                         /* 3 XP, 4 Longhorn-compiled */
    char ut_src[1200];                  /* "PATH (colour, size, vN)" for the logs */
    /* the visual style the theme names (the spec's, else theme.ini's msstyles/color/size; "" = none): a DWM theme's own classic frame
       is this style in msstyles mode, "|style|color|size" (path.frames_classic empty or "style", LUXWM.md "Classic fallback") */
    char style[1024], style_color[64], style_size[64];
    /* Windows Classic (LUXWM.md "Windows Classic frames"; theme spec "|classic|KEY=VALUE;..."): no visual style, the frame drawn
       like user32's non-client painting from the classic system colours and metrics. mode is THEME_MSSTYLES (its geometry:
       cnBorders = 3 + ms_border, caption ms_cap_h + 1), t->ut is NULL; the caption buttons are cl_capw - 2 wide */
    int classic;
    unsigned cl[31];                    /* COLOR_SCROLLBAR .. COLOR_MENUBAR as 0xRRGGBB (Windows Standard where the theme has none) */
    int cl_capw, cl_gradient;           /* Metrics.captionwidth (SM_CXSIZE); gradient captions (activecaption != gradientactivecaption) */
    char cl_mface[64]; int cl_mpx, cl_mbold, cl_mitalic;   /* Metrics.menufont as a LOGFONT (the window menu, drawn by wuxtheme) */
    char cl_face[64]; int cl_px, cl_bold, cl_italic;   /* Metrics.captionfont as a LOGFONT: face, pixel height, bold, italic (wuxtheme draws it) */
    int cl_smooth;                      /* Fonts.smoothing for the caption text: 0 off, 1 standard (gray where the font's gasp allows), 2 cleartype */
};

/* extended frame (CLIENT-PROTOCOL.md): the plate reaches l/r/t/b client pixels into the client (_LHWM_EXTEND_FRAME), flags from
   _LHWM_FRAME_FLAGS. sheet: the whole client is glass (all four 0xFFFFFFFF). NULL or all zero = the normal frame. */
/* FRAME_NO_HEADER_PIXMAP is wm.c's only (no _LHWM_HEADER_PIXMAP for the client); the painter never sees it */
enum { FRAME_NO_TITLE = 1, FRAME_NO_ICON = 2, FRAME_NO_FOG = 4, FRAME_CLIENT_CAPTION = 8, FRAME_NO_HEADER_PIXMAP = 16,
       FRAME_MAX_DISABLED = 1 << 16 };   /* luxwm's own (not a client flag): a fixed-size window's maximise button drawn disabled */
struct frame_ext { int l, r, t, b, sheet; unsigned flags; };

/* spec: a theme directory (theme.ini + PNG cache), optionally followed by a visual style: "DIR|STYLE.msstyles|COLOR|SIZE"; or the
   style alone, "|STYLE.msstyles|COLOR|SIZE" (msstyles mode, no directory: every tuning key at its default). The style is the spec's,
   else theme.ini's msstyles / color / size (relative to the directory). With libuxtheme (frame_uxtheme_available) a style is read
   live (LUXWM.md "libuxtheme source"); without it, or with frame_theme_source = FRAME_SRC_DIR, the directory's PNGs are used. */
int  theme_load(struct theme *t, const char *spec);
enum { FRAME_SRC_AUTO, FRAME_SRC_DIR, FRAME_SRC_LIB };
extern int frame_theme_source;          /* AUTO: the library when present and a style is named; DIR: the PNG cache; LIB: the library or fail */
int  frame_uxtheme_available(void);
void theme_free(struct theme *t);
void theme_margins(const struct theme *t, int *l, int *r, int *top, int *b);
void theme_margins_ex(const struct theme *t, int maximized, int *l, int *r, int *top, int *b);
/* hole (dec-relative, may be NULL): the area an opaque client covers; it is not painted or uploaded.
   maxfade (the maximise crossfade): weight of the maximised plate, 0 = the normal plate, 1 = the maximised one, in between a cross-dissolve of
   the two; < 0 = by state (maximized). Painted over the geometry given by maximized. */
void frame_paint_ex3(Display *dpy, Visual *vis, const struct theme *t, Window dec, int w, int h, const char *title, cairo_surface_t *icon,
                     int focused, int hover, int pressed, int maximized, const struct frame_ext *ext, const XRectangle *hole, double maxfade);
/* client (dec-relative, may be NULL): where the client window lies; with size hints it may leave part of the frame's client area free
   (a maximised terminal), which an msstyles frame then fills with its border colour (DWM plates lie under the client anyway) */
void frame_paint_ex4(Display *dpy, Visual *vis, const struct theme *t, Window dec, int w, int h, const char *title, cairo_surface_t *icon,
                     int focused, int hover, int pressed, int maximized, const struct frame_ext *ext, const XRectangle *hole, double maxfade,
                     const XRectangle *client);
void frame_paint_cr3(cairo_t *cr, const struct theme *t, int w, int h, const char *title, cairo_surface_t *icon, int focused, int hover,
                     int pressed, int maximized, const struct frame_ext *ext, double maxfade);
/* client icons, both scaled with CAIRO_FILTER_BEST to want x want (aspect kept, centred): _NET_WM_ICON picks the smallest image
   >= want (else the largest); WM_HINTS icon_pixmap (+ icon_mask): depth 1 = black on white, other depths through the default
   visual (32: ARGB). NULL when there is nothing usable. */
cairo_surface_t *frame_icon_from_netwm(const unsigned long *data, unsigned long n, int want);
cairo_surface_t *frame_icon_from_pixmap(Display *dpy, Pixmap icon, Pixmap mask, int want);
/* the theme's default icon nearest >= want (not a new reference; NULL when the theme has none) */
cairo_surface_t *theme_default_icon(const struct theme *t, int want);
/* fonts: the theme's path.fonts, font directories/files (';' separated, absolute), registered as fontconfig application
   fonts together with the next theme_load's font_files (the set is replaced, so a theme without them does not see them) */
void frame_fonts_set(const char *list);
/* LHWM_DEBUG: log the face fontconfig/pango resolve the theme's caption font to */
void frame_fonts_report(const struct theme *t);
void frame_paint_cr(cairo_t *cr, const struct theme *t, int w, int h, const char *title, cairo_surface_t *icon, int focused, int hover, int pressed, int maximized);
/* the caption icon's cell in frame coordinates (whether an icon is drawn there is the caller's: FRAME_NO_ICON) */
void frame_icon_rect(const struct theme *t, int w, int maximized, int *x, int *y, int *iw, int *ih);
int  frame_hit(const struct theme *t, int w, int h, int x, int y);
int  frame_hit_ex(const struct theme *t, int w, int h, int x, int y, int maximized);
/* window shape from the painted alpha (corners): fills up to max rects, returns the count */
int  frame_shape_rects(const struct theme *t, cairo_surface_t *img, int maximized, XRectangle *r, int max);
/* CLIENT_CAPTION (experimental, CLIENT-PROTOCOL.md "Client content over the caption rows"): the frame rows left above a client that grows up into
   the caption (msstyles: cnBorders; DWM: the side border width; maximised: 0) */
int  theme_caption_border(const struct theme *t, int maximized);
/* the union of the caption buttons' hit rectangles, frame coordinates (_LHWM_CAPTION_RECT before the client offset) */
void frame_buttons_rect(const struct theme *t, int w, int maximized, int *x, int *y, int *bw, int *bh);
/* the header band for _LHWM_HEADER_PIXMAP: the frame (w x h) painted with ext (its top extension continues the caption band; title
   and icon left out), cropped to columns [x0, x0 + cw) and rows [0, band_h). Caller destroys it. */
cairo_surface_t *frame_header_image(const struct theme *t, int w, int h, int x0, int cw, int band_h, int focused, int hover, int pressed,
                                    int maximized, const struct frame_ext *ext);
extern unsigned long frame_upload_serial;   /* request serial of the last frame_paint_ex4 upload (first request of it) */
/* when set, the next frame_paint_ex3 uploads to an offscreen pixmap, calls frame_pre_upload(frame_pre_upload_arg) (the caller's
   window resize), then sets the shape and copies the pixels into the window; reset by that paint (LUXWM.md "Resize without flicker") */
extern void (*frame_pre_upload)(void *); extern void *frame_pre_upload_arg;
extern double frame_prof_ms[4];   /* LHWM_PROF: paint, shape, upload ms (accumulated), paint count */
#endif
