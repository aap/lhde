/* liblhde: line-keeping ini documents (lhde/settings.h). */
#define _GNU_SOURCE
#include "lhde/settings.h"
#include "internal.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

struct lhde_ini { char **l; int n, cap; };

lhde_ini *lhde_ini_new(void) { return calloc(1, sizeof(lhde_ini)); }

static int push(lhde_ini *ini, const char *s, int at)
{
    if (ini->n == ini->cap) {
        int c = ini->cap ? ini->cap * 2 : 32;
        char **nl = realloc(ini->l, (size_t)c * sizeof *nl);
        if (!nl) return -1;
        ini->l = nl; ini->cap = c;
    }
    char *d = strdup(s);
    if (!d) return -1;
    if (at < 0 || at > ini->n) at = ini->n;
    memmove(ini->l + at + 1, ini->l + at, (size_t)(ini->n - at) * sizeof *ini->l);
    ini->l[at] = d; ini->n++;
    return 0;
}
static void drop(lhde_ini *ini, int at)
{
    free(ini->l[at]);
    memmove(ini->l + at, ini->l + at + 1, (size_t)(ini->n - at - 1) * sizeof *ini->l);
    ini->n--;
}

lhde_ini *lhde_ini_parse(const char *text)
{
    lhde_ini *ini = lhde_ini_new();
    if (!ini) return NULL;
    const char *p = text ? text : "";
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        char *line = strndup(p, len);
        if (!line) break;
        if (len && line[len - 1] == '\r') line[len - 1] = 0;
        push(ini, line, -1); free(line);
        if (!e) break;
        p = e + 1;
    }
    return ini;
}

lhde_ini *lhde_ini_load(const char *path)
{
    FILE *f = path ? fopen(path, "r") : NULL;
    if (!f) return lhde_ini_new();
    size_t cap = 4096, len = 0; char *buf = malloc(cap);
    size_t k;
    while (buf && (k = fread(buf + len, 1, cap - len - 1, f)) > 0) {
        len += k;
        if (cap - len < 2) { char *nb = realloc(buf, cap * 2); if (!nb) { free(buf); buf = NULL; break; } buf = nb; cap *= 2; }
    }
    fclose(f);
    if (!buf) return NULL;
    buf[len] = 0;
    lhde_ini *ini = lhde_ini_parse(buf);
    free(buf);
    return ini;
}

void lhde_ini_free(lhde_ini *ini)
{
    if (!ini) return;
    for (int i = 0; i < ini->n; i++) free(ini->l[i]);
    free(ini->l); free(ini);
}

char *lhde__trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
    return s;
}

/* 0 blank or comment, 1 section (sec), 2 key (key, value without comment; *vstart = the value's offset in the line,
   *cstart = the comment's offset or -1) */
static int classify(const char *line, char *sec, size_t ns, char *key, size_t nk, char *val, size_t nv, int *cstart)
{
    const char *s = line; while (*s == ' ' || *s == '\t') s++;
    if (!*s || *s == ';' || *s == '#') return 0;
    if (*s == '[') {
        const char *e = strchr(s, ']');
        size_t len = e ? (size_t)(e - s - 1) : strlen(s + 1);
        char tmp[512]; snprintf(tmp, sizeof tmp, "%.*s", (int)(len < 511 ? len : 511), s + 1);
        snprintf(sec, ns, "%s", lhde__trim(tmp));
        return 1;
    }
    const char *eq = strchr(s, '=');
    if (!eq) return 0;
    char tk[512]; snprintf(tk, sizeof tk, "%.*s", (int)((size_t)(eq - s) < 511 ? (size_t)(eq - s) : 511), s);
    snprintf(key, nk, "%s", lhde__trim(tk));
    const char *v = eq + 1;
    if (cstart) *cstart = -1;
    size_t vl = strlen(v);
    for (size_t i = 1; i < vl; i++)
        if ((v[i] == ';' || v[i] == '#') && (v[i - 1] == ' ' || v[i - 1] == '\t')) { if (cstart) *cstart = (int)(v + i - line); vl = i; break; }
    char tv[4096]; snprintf(tv, sizeof tv, "%.*s", (int)(vl < 4095 ? vl : 4095), v);
    snprintf(val, nv, "%s", lhde__trim(tv));
    return 2;
}

const char *lhde_ini_get(const lhde_ini *ini, const char *sec, const char *key, char *buf, size_t n)
{
    if (!ini) return NULL;
    char cur[256] = "", k[256], v[4096]; int found = 0;
    for (int i = 0; i < ini->n; i++) {
        char s2[256];
        int c = classify(ini->l[i], s2, sizeof s2, k, sizeof k, v, sizeof v, NULL);
        if (c == 1) snprintf(cur, sizeof cur, "%s", s2);
        else if (c == 2 && !strcasecmp(cur, sec) && !strcasecmp(k, key)) { snprintf(buf, n, "%s", v); found = 1; }
    }
    return found ? buf : NULL;
}

int lhde_ini_has_section(const lhde_ini *ini, const char *sec)
{
    char s[256], k[8], v[8];
    for (int i = 0; ini && i < ini->n; i++)
        if (classify(ini->l[i], s, sizeof s, k, sizeof k, v, sizeof v, NULL) == 1 && !strcasecmp(s, sec)) return 1;
    return 0;
}

/* the section that line i belongs to (its header index, -1 before the first header) and whether it is named sec */
static int remove_if_empty(lhde_ini *ini, const char *sec)
{
    int changed = 0;
    for (int i = 0; i < ini->n; i++) {
        char s[256], k[256], v[4096];
        if (classify(ini->l[i], s, sizeof s, k, sizeof k, v, sizeof v, NULL) != 1 || strcasecmp(s, sec)) continue;
        int j = i + 1, keys = 0;
        for (; j < ini->n; j++) {
            int c = classify(ini->l[j], s, sizeof s, k, sizeof k, v, sizeof v, NULL);
            if (c == 1) break;
            if (c == 2) keys++;
        }
        if (keys) continue;
        /* the header and what follows it up to the next section (blank lines, comments), and one blank line before it */
        for (int x = j - 1; x >= i; x--) drop(ini, x);
        if (i > 0 && !*lhde__trim(strdupa(ini->l[i - 1])) && (i >= ini->n || !*lhde__trim(strdupa(ini->l[i])))) { drop(ini, i - 1); i--; }
        i--; changed = 1;
    }
    while (ini->n > 0 && !*lhde__trim(strdupa(ini->l[ini->n - 1]))) drop(ini, ini->n - 1);
    return changed;
}

int lhde_ini_set(lhde_ini *ini, const char *sec, const char *key, const char *value)
{
    if (!ini) return 0;
    char cur[256] = "", k[256], v[4096];
    int last_key = -1, last_line = -1, have_sec = 0, cstart = -1, changed = 0;
    for (int i = 0; i < ini->n; i++) {
        char s2[256]; int cs;
        int c = classify(ini->l[i], s2, sizeof s2, k, sizeof k, v, sizeof v, &cs);
        if (c == 1) { snprintf(cur, sizeof cur, "%s", s2); if (!strcasecmp(cur, sec)) { have_sec = 1; last_line = i; } continue; }
        if (strcasecmp(cur, sec) || !cur[0]) continue;
        if (*lhde__trim(strdupa(ini->l[i]))) last_line = i;
        if (c == 2 && !strcasecmp(k, key)) {
            if (!value) { drop(ini, i); i--; last_line = i; changed = 1; continue; }
            if (last_key >= 0) { drop(ini, last_key); i--; changed = 1; }   /* a duplicate: the later one wins, one stays */
            last_key = i; cstart = cs;
        }
    }
    if (!value) { if (changed) remove_if_empty(ini, sec); return changed; }
    if (last_key >= 0) {
        char kk[256], old[4096], s2[256];
        classify(ini->l[last_key], s2, sizeof s2, kk, sizeof kk, old, sizeof old, NULL);
        if (!strcmp(old, value)) return changed;
        char comment[1024] = "";
        if (cstart >= 0) { const char *c0 = ini->l[last_key] + cstart; while (c0 > ini->l[last_key] && (c0[-1] == ' ' || c0[-1] == '\t')) c0--; snprintf(comment, sizeof comment, "%s", c0); }
        /* the key as it was spelled, its indentation kept */
        const char *ln = ini->l[last_key]; int ind = 0; while (ln[ind] == ' ' || ln[ind] == '\t') ind++;
        size_t need = strlen(kk) + strlen(value) + strlen(comment) + (size_t)ind + 8;
        char *nl = malloc(need);
        if (!nl) return changed;
        snprintf(nl, need, "%.*s%s = %s%s", ind, ln, kk, value, comment);
        free(ini->l[last_key]); ini->l[last_key] = nl;
        return 1;
    }
    size_t need = strlen(key) + strlen(value) + 8;
    char *line = malloc(need); if (!line) return changed;
    snprintf(line, need, "%s = %s", key, value);
    if (have_sec) push(ini, line, last_line + 1);
    else {
        if (ini->n > 0 && *lhde__trim(strdupa(ini->l[ini->n - 1]))) push(ini, "", -1);
        char hdr[300]; snprintf(hdr, sizeof hdr, "[%s]", sec);
        push(ini, hdr, -1); push(ini, line, -1);
    }
    free(line);
    return 1;
}

int lhde_ini_remove_section(lhde_ini *ini, const char *sec)
{
    int changed = 0;
    for (int i = 0; ini && i < ini->n; i++) {
        char s[256], k[256], v[4096];
        if (classify(ini->l[i], s, sizeof s, k, sizeof k, v, sizeof v, NULL) != 1 || strcasecmp(s, sec)) continue;
        int j = i + 1;
        for (; j < ini->n; j++) if (classify(ini->l[j], s, sizeof s, k, sizeof k, v, sizeof v, NULL) == 1) break;
        /* keep comment lines that introduce the next section (directly before its header) */
        int end = j;
        while (end > i + 1 && j < ini->n && classify(ini->l[end - 1], s, sizeof s, k, sizeof k, v, sizeof v, NULL) == 0 && *lhde__trim(strdupa(ini->l[end - 1]))) end--;
        for (int x = end - 1; x >= i; x--) drop(ini, x);
        if (i > 0 && !*lhde__trim(strdupa(ini->l[i - 1])) && (i >= ini->n || !*lhde__trim(strdupa(ini->l[i])))) drop(ini, i - 1);
        i--; changed = 1;
    }
    while (ini && ini->n > 0 && !*lhde__trim(strdupa(ini->l[ini->n - 1]))) drop(ini, ini->n - 1);
    return changed;
}

int lhde_ini_sections(const lhde_ini *ini, void (*fn)(const char *, void *), void *ud)
{
    char seen[256][128]; int ns = 0, count = 0;
    for (int i = 0; ini && i < ini->n; i++) {
        char s[256], k[8], v[8];
        if (classify(ini->l[i], s, sizeof s, k, sizeof k, v, sizeof v, NULL) != 1) continue;
        int dup = 0;
        for (int j = 0; j < ns; j++) if (!strcasecmp(seen[j], s)) dup = 1;
        if (dup) continue;
        if (ns < 256) snprintf(seen[ns++], sizeof seen[0], "%s", s);
        count++;
        if (fn) fn(s, ud);
    }
    return count;
}

int lhde_ini_keys(const lhde_ini *ini, const char *sec, void (*fn)(const char *, const char *, void *), void *ud)
{
    char cur[256] = "", k[256], v[4096]; int count = 0;
    for (int i = 0; ini && i < ini->n; i++) {
        char s[256];
        int c = classify(ini->l[i], s, sizeof s, k, sizeof k, v, sizeof v, NULL);
        if (c == 1) snprintf(cur, sizeof cur, "%s", s);
        else if (c == 2 && !strcasecmp(cur, sec)) { count++; if (fn) fn(k, v, ud); }
    }
    return count;
}

void lhde_ini_header(lhde_ini *ini, const char *text)
{
    if (!ini || ini->n > 0 || !text) return;
    lhde_ini *h = lhde_ini_parse(text);
    for (int i = 0; h && i < h->n; i++) push(ini, h->l[i], -1);
    lhde_ini_free(h);
}

char *lhde_ini_text(const lhde_ini *ini)
{
    size_t len = 1;
    for (int i = 0; ini && i < ini->n; i++) len += strlen(ini->l[i]) + 1;
    char *t = malloc(len), *p = t;
    if (!t) return NULL;
    for (int i = 0; ini && i < ini->n; i++) { size_t l = strlen(ini->l[i]); memcpy(p, ini->l[i], l); p += l; *p++ = '\n'; }
    *p = 0;
    return t;
}

int lhde__mkdirs(const char *d)
{
    char p[PATH_MAX]; snprintf(p, sizeof p, "%s", d);
    for (char *s = p + 1; *s; s++) if (*s == '/') { *s = 0; if (mkdir(p, 0755) && errno != EEXIST) return -1; *s = '/'; }
    return mkdir(p, 0755) && errno != EEXIST ? -1 : 0;
}

int lhde__write_file(const char *path_in, const char *data)
{
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s", path_in);
    /* through a symlink: write its target (dotfile managers, longhorn/settings.ini -> ../lhde/settings.ini) */
    char tgt[PATH_MAX]; struct stat lst;
    for (int hop = 0; hop < 8 && lstat(path, &lst) == 0 && S_ISLNK(lst.st_mode); hop++) {
        ssize_t k = readlink(path, tgt, sizeof tgt - 1);
        if (k <= 0) break;
        tgt[k] = 0;
        if (tgt[0] == '/') snprintf(path, sizeof path, "%s", tgt);
        else { char d[PATH_MAX]; snprintf(d, sizeof d, "%s", path); char *sl = strrchr(d, '/'); if (sl) *sl = 0; else snprintf(d, sizeof d, "."); snprintf(path, sizeof path, "%.2000s/%.2000s", d, tgt); }
    }
    char dir[PATH_MAX]; snprintf(dir, sizeof dir, "%s", path);
    char *sl = strrchr(dir, '/');
    if (sl) { *sl = 0; if (dir[0] && lhde__mkdirs(dir)) return -1; }
    char tmp[PATH_MAX + 32]; snprintf(tmp, sizeof tmp, "%s.lhde-%d", path, (int)getpid());
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    size_t len = strlen(data);
    int ok = fwrite(data, 1, len, f) == len;
    struct stat st;
    if (stat(path, &st) == 0) fchmod(fileno(f), st.st_mode & 07777);
    if (fclose(f) || !ok || rename(tmp, path)) { unlink(tmp); return -1; }
    return 0;
}

int lhde_ini_save(const lhde_ini *ini, const char *path)
{
    char *t = lhde_ini_text(ini);
    if (!t) return -1;
    int r = lhde__write_file(path, t);
    free(t);
    return r;
}
