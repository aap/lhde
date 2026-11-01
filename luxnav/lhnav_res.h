/* lhnav_res: lhnav's private reader (lhnav__res_*) of a build's shell32.dll resources (lhnav_res.c) */
#ifndef LHNAV_RES_H
#define LHNAV_RES_H
#include <stdint.h>
/* icon group `id` (RT_GROUP_ICON), the image nearest to `want` px (larger preferred, then more bits); premultiplied ARGB */
uint32_t *lhnav__res_icon(const char *file, int id, int want, int *w, int *h);
/* bitmap `id` (RT_BITMAP), e.g. the toolbar strips 214/216 (24/16 px, XP's tb_sh_def_*) */
uint32_t *lhnav__res_bitmap(const char *file, int id, int *w, int *h);
#endif
