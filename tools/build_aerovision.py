#!/usr/bin/env python3
"""Build the caption buttons of the aerovision frame set (docs/NAVBAR.md "Vision caption buttons").

Aero vision's frame decoration ends 15 px below the frame's top edge, right under the caption buttons (aap's todo: "the actual
frame decoration should end exactly below the 3 buttons, no space below, i count 15 pixels from the top"). The aero sprites
(close/max/min/restore.png: 4 frames of 14 rows, normal / hot / pressed / inactive) sit at button_top = 2 and so end on frame
row 15, one row below the plate's pill (plate rows 2..14) and below the 15 px deco. The vision set drops one sprite row per
frame so the buttons are 13 rows high and end on row 14 with the pill; the widths stay (27 + 25 + 43 = 95 columns fill the
plate's pill exactly with button_right = 5, aap's tuning of aero), so the only change is the row. Which row is a knob:

    --drop-row N   sprite row removed from every frame (default 11: the light inner line above the dark bottom line; the glyphs
                   and the glossy top half are untouched; 0..13)

Output: vision_close.png, vision_max.png, vision_min.png, vision_restore.png in OUT_DIR (4 frames of 13 rows). They are not
named close.png etc. on purpose: those names are replaced by the style's CompositedWindow streams when luxwm reads the msstyles
through libuxtheme (frame_ut.c fut_png); theme.ini names them with `button_files`.

The concept art (lh_concepts/1456384117.or.45878.png) shows a pill about 13 px high hanging from the top edge with three
buttons of about 26/25/37 px; ours keep aero's 27/25/43 because the pill is baked into the plate texture at that width.

usage: python3 -I build_aerovision.py [--drop-row N] AERO_DIR OUT_DIR
       (AERO_DIR = the aero-patch frame set's sprites, pixel-identical to aero-patch.msstyles' LGCLOSEBTN/LGMAXBTN/... streams)
"""
import sys, os
from PIL import Image

args = sys.argv[1:]
drop = 11
if len(args) >= 2 and args[0] == '--drop-row':
    drop = int(args[1]); args = args[2:]
if len(args) != 2 or not 0 <= drop < 14:
    sys.exit(__doc__)
src, out = args
FRAMES, FH = 4, 14

def shrink(im):
    """every frame without sprite row `drop`"""
    if im.height != FRAMES * FH:
        sys.exit(f'{im.filename}: expected {FRAMES} frames of {FH} rows, got {im.size}')
    nh = FH - 1
    o = Image.new('RGBA', (im.width, FRAMES * nh), (0, 0, 0, 0))
    for f in range(FRAMES):
        fr = im.crop((0, f * FH, im.width, (f + 1) * FH))
        top = fr.crop((0, 0, im.width, drop))
        bot = fr.crop((0, drop + 1, im.width, FH))
        o.paste(top, (0, f * nh)); o.paste(bot, (0, f * nh + drop))
    return o

os.makedirs(out, exist_ok=True)
for name in ('close', 'max', 'min', 'restore'):
    im = Image.open(os.path.join(src, name + '.png')).convert('RGBA')
    shrink(im).save(os.path.join(out, f'vision_{name}.png'))
print(f'ok: vision_{{close,max,min,restore}}.png, 4 x {FH - 1} rows (sprite row {drop} dropped) in {out}')
