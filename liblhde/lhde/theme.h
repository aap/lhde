/* liblhde: theme files and their resolver (docs/THEME-FORMAT.md sections 2-6).

   A theme is a name: a theme file (the user's ${XDG_CONFIG_HOME:-~/.config}/lhde/themes/NAME.theme first, then themes/NAME.theme
   of the data directories, i.e. the shipped data/themes/), a bare visual style (styles/NAME.msstyles or styles/NAME/<name>.msstyles:
   a theme of its own with every key from its defaults), or an alias a theme file lists ([Theme] aliases, an lhde addition for
   the x11-era names: aero-patch -> aero4074).

   lhde_theme_resolve(NAME) returns one flat record: every key as SECTION.KEY = VALUE (Glass.frames = jade4074) with base
   chains, defaults from the style and fallbacks applied, and for every key where it came from:
     file      the theme's own file                      base      a theme in its base chain (which, and its file)
     settings  settings.ini's legacy [override:NAME] / [override] (flxp's Display Properties before the switch)
     default   section 5: derived from the visual style (ut_format, CompositedWindow / DWMWindow, the build)
     fallback  section 6: what was asked for is not imported (or not usable); the reason is part of the source text
   Absent keys inherit, then take the default; the value `none` switches a component off and is never replaced by a default.
   Besides the theme keys the record carries derived ones: theme (the name resolved), file (its file, "" for a bare style),
   style.kind (xp | longhorn | dwm | classic | missing), style.format, style.family, and path.* (absolute paths of the components:
   path.style, path.shellstyle, path.frames, path.frames_classic, path.pack, path.navbar, path.icons, path.wallpaper,
   path.fonts, path.resources, path.scheme; "" when the component is none or missing). */
#ifndef LHDE_THEME_H
#define LHDE_THEME_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lhde_theme lhde_theme;

enum lhde_source { LHDE_FROM_FILE, LHDE_FROM_BASE, LHDE_FROM_SETTINGS, LHDE_FROM_DEFAULT, LHDE_FROM_FALLBACK, LHDE_FROM_DERIVED };

/* resolve NAME; NULL when it is no theme (err: why: unknown name, a base cycle, a chain deeper than 8). A base that is not
   found ends the chain (reported as the source of the keys it would have given: fallback). */
lhde_theme *lhde_theme_resolve(const char *name, char *err, size_t nerr);
void lhde_theme_free(lhde_theme *t);
/* a value ("Glass.frames"; sections case-insensitive); NULL when the record has no such key */
const char *lhde_theme_get(const lhde_theme *t, const char *key);
/* where it came from, as text: "file /path/x.theme", "base jade4074 (/path/jade4074.theme)", "default: v4 style with
   CompositedWindow, build 4074", "fallback: frames aero5048 not imported (none)", "settings: [override:aero5048]"; NULL like get */
const char *lhde_theme_why(const lhde_theme *t, const char *key);
enum lhde_source lhde_theme_source(const lhde_theme *t, const char *key);
/* iterate: i from 0; 0 when past the end */
int lhde_theme_item(const lhde_theme *t, int i, const char **key, const char **value, const char **why);
/* the record as text, one "key=value" per line: theme=NAME, serial=SERIAL, then SECTION.KEY=VALUE and the derived keys
   (_LH_THEME_RESOLVED, docs/THEME-FORMAT.md section 4); malloc'ed */
char *lhde_theme_serialize(const lhde_theme *t, unsigned long serial);
/* a record from that text (a client that reads _LH_THEME_RESOLVED); sources are "published"; NULL on empty text */
lhde_theme *lhde_theme_parse(const char *text, unsigned long *serial);

/* every theme: the user's files, the shipped files, the bare styles no file claims (by name or alias); sorted by title within
   each group. fn gets the name, its title, its file ("" for a bare style) and the group (0 user, 1 shipped, 2 style). Returns
   how many. A theme file that does not parse still counts (title = name). */
int lhde_theme_list(void (*fn)(const char *name, const char *title, const char *file, int group, void *ud), void *ud);
/* the file a name selects ("" for a bare style) and the name after aliases (aero-patch -> aero4074); 0 when unknown */
int lhde_theme_lookup(const char *name, char *canon, size_t ncanon, char *file, size_t nfile);

/* a theme key changed by the user (Display Properties, luxshell's flyout; docs/THEME-FORMAT.md section 3): written into the
   user's own theme file when NAME is one (~/.config/lhde/themes/NAME.theme), else into NAME-modified.theme there (created
   with base = NAME, name = TITLE (modified)). value NULL removes the key from that file. selected gets the theme to select now
   (NAME-modified, or NAME). Returns 1 when a file changed, 0 when nothing did, -1 on an error. Shipped files are never
   written. */
int lhde_theme_set_key(const char *name, const char *key, const char *value, char *selected, size_t nsel);
/* the user's theme directory: ${XDG_CONFIG_HOME:-~/.config}/lhde/themes */
const char *lhde_theme_user_dir(char *out, size_t n);
/* 1 when KEY ("Glass.frames") is a theme key of the format (section 2), 0 for derived and unknown keys */
int lhde_theme_is_key(const char *key);

/* ---- visual styles ---- */
enum lhde_style_kind { LHDE_STYLE_MISSING, LHDE_STYLE_CLASSIC, LHDE_STYLE_XP, LHDE_STYLE_LONGHORN, LHDE_STYLE_DWM };
typedef struct {
    int kind;              /* enum lhde_style_kind */
    int format;            /* 3 XP, 4 Longhorn-compiled, 0 unknown */
    int composited;        /* has CompositedWindow (4042-4093 glass) */
    int dwm;               /* has DWMWindow (5112's, also DWMGlass::DWMWindow) */
    int sidebar;           /* has Sidebar (Longhorn's Avalon bars) */
    char build[16];        /* "4074", "5048", "xp", "" */
    char family[32];       /* slate, jade, aero, luna, plex, royale, ... (OriginalFilename, else the name) */
    char color[64], size[64];   /* the style's first scheme and size */
    char path[1024];
} lhde_style_info;
/* a style by name (styles/NAME.msstyles, styles/NAME/<name>.msstyles) or path; "classic" gives kind CLASSIC; 0 when missing */
int lhde_style_probe(const char *name, lhde_style_info *out);
/* the style file for a name ("" when none); path in out */
const char *lhde_style_path(const char *name, char *out, size_t n);
/* replace the probe of a style file (tests without Microsoft files): fn fills out->format/composited/dwm/sidebar/family/
   color/size for path and returns 1; NULL restores wuxtheme's */
void lhde_style_set_probe(int (*fn)(const char *path, lhde_style_info *out));

#ifdef __cplusplus
}
#endif
#endif
