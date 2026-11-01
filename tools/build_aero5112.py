#!/usr/bin/env python3
"""Build the aero5112 frame set from the DWMWindow class of the Vista 5112 aero.msstyles (COMPOSITION.md 21).
5112 has no frame bitmaps in uDWM.dll: uDWM reads the frame through GetThemeStream from DWMWindow parts whose DISKSTREAM
property names an RT 'STREAM' PNG (straight alpha, IMAGECOUNT states stacked vertically). The frame is piecewise (top / left /
right / bottom, DWMGlass:: or DWMMetal:: variant); luxwm wants one nine-sliced plate per state, so the pieces are composed into an
801x607 plate (5048's size). Buttons: a sprite of 4 frames (normal, hot, pressed, inactive) = the button plate state + its glyph.
usage: build_aero5112.py RES_DIR OUT_DIR   (RES_DIR = res/5112/aero.msstyles, the tools/peres.py dump)"""
import sys, os
from PIL import Image
src, out = sys.argv[1], sys.argv[2]
def S(n): return Image.open(f'{src}/STREAM_{n}_1033.png').convert('RGBA')
def state(im, i, n):
    h = im.height // n; return im.crop((0, i * h, im.width, (i + 1) * h))
# DISKSTREAM ids (tools/lhstyle.py on the 5112 aero.msstyles)
ID = dict(BUTTONCAPTION=453, BUTTONCAPTIONEND=454, BUTTONCLOSE=455, BUTTONCLOSEALONE=456, BUTTONCLOSEGLOW=457,
          BUTTONCLOSEGLYPH=458, BUTTONGLOW=459, BUTTONHELPGLYPH=460, BUTTONMAXGLYPH=461, BUTTONMINGLYPH=462,
          BUTTONRESTOREGLYPH=463, MAXBOTTOMFRAME=466, MAXLEFTFRAME=467, MAXRIGHTFRAME=469, MAXTOPFRAME=472,
          GLASS_BOTTOM=478, GLASS_LEFT=479, GLASS_RIGHT=480, GLASS_TOP=481)
W, H = 801, 607
def hstretch(piece, w, l, r):
    """3-slice horizontally: l / r columns kept, the middle stretched (nearest: the middle is uniform)"""
    o = Image.new('RGBA', (w, piece.height))
    o.paste(piece.crop((0, 0, l, piece.height)), (0, 0))
    o.paste(piece.crop((piece.width - r, 0, piece.width, piece.height)), (w - r, 0))
    mid = piece.crop((l, 0, piece.width - r, piece.height)).resize((w - l - r, piece.height), Image.NEAREST)
    o.paste(mid, (l, 0)); return o
def plate(top, left, right, bottom, w, h, cl, cr, fill_row):
    """top band (top.height rows, corner columns cl/cr kept), sides tiled, bottom band; the inside filled with the glass of the
    top band's row fill_row (what an extended frame shows; a client normally covers it)"""
    o = Image.new('RGBA', (w, h))
    t = hstretch(top, w, cl, cr); b = hstretch(bottom, w, cl, cr)
    mid_h = h - top.height - bottom.height
    inner = t.crop((cl, fill_row, w - cr, fill_row + 1)).resize((w - cl - cr, mid_h), Image.NEAREST)
    o.paste(inner, (cl, top.height))
    o.paste(left.resize((left.width, mid_h), Image.NEAREST) if left.height == 1 else tile_v(left, mid_h), (0, top.height))
    o.paste(tile_v(right, mid_h), (w - right.width, top.height))
    o.paste(t, (0, 0)); o.paste(b, (0, h - bottom.height)); return o
def tile_v(p, h):
    o = Image.new('RGBA', (p.width, h))
    for y in range(0, h, p.height): o.paste(p, (0, y))
    return o
os.makedirs(out, exist_ok=True)
for i, name in enumerate(('active', 'inactive')):
    top, left, right, bot = (state(S(ID[k]), i, 2) for k in ('GLASS_TOP', 'GLASS_LEFT', 'GLASS_RIGHT', 'GLASS_BOTTOM'))
    plate(top, left, right, bot, W, H, 9, 9, 12).save(f'{out}/plate_{name}.png')
    # maximised: MAXTOPFRAME (201x26 per state, opaque, SIZINGMARGINS 0 0 1 1: the whole width stretches) over a 787x599 plate
    mt, ml, mr, mb = (state(S(ID[k]), i, 2) for k in ('MAXTOPFRAME', 'MAXLEFTFRAME', 'MAXRIGHTFRAME', 'MAXBOTTOMFRAME'))
    mtop = mt.resize((787, mt.height), Image.BILINEAR); mbot = mb.resize((787, mb.height), Image.BILINEAR)
    plate(mtop, ml, mr, mbot, 787, 599, 9, 9, mt.height - 2).save(f'{out}/plate_max_{name}.png')
# buttons: 4 frames of 15 rows (normal, hot, pressed, inactive = the parts' 4th state), glyph centred in the part's content rect
def sprite(plate_id, glyph_id, cm):
    pl = S(plate_id); gl = S(glyph_id); n = 4; ph = pl.height // n; gh = gl.height // n
    o = Image.new('RGBA', (pl.width, ph * n))
    for s in range(n):
        p = state(pl, s, n); g = state(gl, s, n); bb = g.getbbox() or (0, 0, g.width, g.height); g = g.crop(bb)
        L, R, T, B = cm   # CONTENTMARGINS left right top bottom
        cx = L + (p.width - L - R - g.width) // 2; cy = T + (ph - T - B - g.height + 1) // 2
        p.alpha_composite(g, (cx, cy)); o.paste(p, (0, s * ph))
    return o
sprite(ID['BUTTONCAPTIONEND'], ID['BUTTONMINGLYPH'], (1, 0, 1, 4)).save(f'{out}/min.png')
sprite(ID['BUTTONCAPTION'], ID['BUTTONMAXGLYPH'], (0, 0, 1, 4)).save(f'{out}/max.png')
sprite(ID['BUTTONCAPTION'], ID['BUTTONRESTOREGLYPH'], (0, 0, 1, 4)).save(f'{out}/restore.png')
sprite(ID['BUTTONCLOSE'], ID['BUTTONCLOSEGLYPH'], (0, 2, 1, 4)).save(f'{out}/close.png')
S(ID['BUTTONGLOW']).save(f'{out}/glow_btn.png'); S(ID['BUTTONCLOSEGLOW']).save(f'{out}/glow_close.png')
print('ok', sorted(os.listdir(out)))
