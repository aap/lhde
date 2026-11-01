/* liblhde: the settings store (docs/SETTINGS.md) and the ini files it is made of.

   One file per user, ${XDG_CONFIG_HOME:-~/.config}/lhde/settings.ini ($LHDE_SETTINGS overrides the path): the current theme's
   name ([theme] name) and the per-user keys that are not part of a theme (sidebar stacking and width, autohide, rows, the
   clock, [commands], ...). Every writer keeps the comments, the order and the keys it
   does not know (read, modify, write back the whole file, atomically, through a symlink). After a write the X clients send
   _LH_SETTINGS_CHANGED with the serial in _LH_SETTINGS_SERIAL (docs/SETTINGS.md "Change notification"); that part stays in
   the clients, liblhde has no X.

   The theme keys (docs/THEME-FORMAT.md) are not here: they live in theme files; a change of one writes NAME-modified.theme
   (lhde/theme.h lhde_theme_set_key). */
#ifndef LHDE_SETTINGS_H
#define LHDE_SETTINGS_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- ini documents: lines kept as they are; sections and keys case-insensitive; a ';' or '#' at the start of a line, or
   after whitespace, starts a comment ---- */
typedef struct lhde_ini lhde_ini;
lhde_ini *lhde_ini_new(void);
/* the file's lines (an empty document when it does not exist); NULL only when out of memory */
lhde_ini *lhde_ini_load(const char *path);
/* a document from text (tests) */
lhde_ini *lhde_ini_parse(const char *text);
void lhde_ini_free(lhde_ini *ini);
/* the last value of SEC.KEY (comment stripped, trimmed) into buf; NULL when the key is absent (an empty value is "") */
const char *lhde_ini_get(const lhde_ini *ini, const char *sec, const char *key, char *buf, size_t n);
int lhde_ini_has_section(const lhde_ini *ini, const char *sec);
/* set SEC.KEY in place (a trailing comment stays) or append it to the section (created at the end); value NULL removes the key,
   and the section when nothing but blank lines and comments is left in it. Returns 1 when the document changed. */
int lhde_ini_set(lhde_ini *ini, const char *sec, const char *key, const char *value);
/* remove a whole section; 1 when there was one */
int lhde_ini_remove_section(lhde_ini *ini, const char *sec);
/* section names in file order (duplicates once) / keys of one section in order: each calls fn; returns how many */
int lhde_ini_sections(const lhde_ini *ini, void (*fn)(const char *sec, void *ud), void *ud);
int lhde_ini_keys(const lhde_ini *ini, const char *sec, void (*fn)(const char *key, const char *value, void *ud), void *ud);
/* prepend comment/header lines when the document is empty (the text may hold several '\n'-separated lines) */
void lhde_ini_header(lhde_ini *ini, const char *text);
/* the whole text (malloc'ed) / written to path atomically (tmp + rename; a symlink is written through, its target replaced;
   the parent directory is created). 0 on success. */
char *lhde_ini_text(const lhde_ini *ini);
int lhde_ini_save(const lhde_ini *ini, const char *path);

/* ---- the store ---- */
/* the settings file (above) and the directory of the user's theme files, ${XDG_CONFIG_HOME:-~/.config}/lhde/themes */
const char *lhde_settings_path(char *out, size_t n);
const char *lhde_config_dir(char *out, size_t n);       /* ${XDG_CONFIG_HOME:-~/.config}/lhde */
/* The user's two directories (docs/SETTINGS.md "Files"). What the user chose: ${XDG_CONFIG_HOME:-~/.config}/lhde (settings.ini,
   themes/, flxp.ini, keys, any shipped file overridden under its own name). What a program remembers on its own:
   ${XDG_STATE_HOME:-~/.local/state}/lhde (luxshell-state.ini, luxshell-mfu.ini, flxp-views, flxp-desktop-positions, flxp-mfu, flxp-run-history). The two
   calls below give the path of a named file in them and create the directory when it is missing. */
const char *lhde_state_dir(char *out, size_t n);        /* ${XDG_STATE_HOME:-~/.local/state}/lhde */
const char *lhde_config_file(const char *name, char *out, size_t n);
const char *lhde_state_file(const char *name, char *out, size_t n);
/* the value of SEC.KEY (the file is read again when its mtime or size changed); NULL when absent or empty */
const char *lhde_settings_get(const char *sec, const char *key, char *buf, size_t n);
/* write SEC.KEY (value NULL removes it); 1 when the file changed, 0 when the value was already that, -1 on an error */
int lhde_settings_set(const char *sec, const char *key, const char *value);
/* forget the cached file (another writer changed it) */
void lhde_settings_reload(void);

#ifdef __cplusplus
}
#endif
#endif
