#!/bin/sh
# aero5112 pack assets (themes/aero5112, COMPOSITION.md 21): the Vista 5112 taskbar art of aero.msstyles 5112, as imageres ids so
# the pack's `imageres = "aero5112"` directory overrides the Aero images one by one (runtime.lua R()); everything else (sidebar,
# toggle, clock, tiles) stays Longhorn's. Run by shellpack.sh, or alone. Needs res/5112/aero.msstyles (tools/peres.py dump),
# res/5112/explorer.exe and resources/fonts/5112.
#   1179/1180/1181  Start::Button bitmap#502 (83x90, 3 states: normal black, hot, pressed green; 32-bit premultiplied), with the
#                   Windows flag (explorer.exe icon 7103, 48 px, scaled) and "Start" (Segoe UI 5112, 16 px (the style: lfHeight -19), white, CONTINUOUS
#                   shadow (75,75,75) at +2,+2: lhstyle/ut-dump) drawn into the CONTENTMARGINS 7 19 0 2 as uxtheme would
#   1187            TaskBar.BACKGROUNDBOTTOM bitmap#298 (40x30, opaque dark slate, SIZINGMARGINS 1 1 14 15) → pack bg_grid
#   1182/1184/1185  TaskBand::Toolbar.BUTTON bitmap#517 (26x29 x 6: normal, hot, pressed, disabled, checked, hotchecked; SIZINGMARGINS
#                   9 9 12 16), widened to 127 px with its own margins (runtime.lua slices task buttons with a fixed 3 px grid)
#   1183            TaskBand.FLASHBUTTON bitmap#296 (26x29, SIZINGMARGINS 12 10 12 12), widened the same way
#   tray_bg.png     TrayNotifyHoriz::TrayNotify.BACKGROUND bitmap#528 (5x30), not used by runtime.lua yet (no classic tray)
# lhde: a research tool. LHDE_WORKSPACE = the Longhorn workspace (default ~/fun/longhorn). Run alone it writes the Windows
# tree's generated/packs/5112/imageres ($LHDE_WINDOWS, else ~/.local/share/lhde-windows, as lhde-import lays it out); from shellpack.sh
# (SHELLPACK_OUT set) the x11 layout's aero5112/ there.
L=${LHDE_WORKSPACE:-$HOME/fun/longhorn}
if [ -n "$SHELLPACK_OUT" ]; then A=$SHELLPACK_OUT/aero5112; else A=${LHDE_WINDOWS:-${XDG_DATA_HOME:-$HOME/.local/share}/lhde-windows}/generated/packs/5112/imageres; fi
mkdir -p "$A"
python3 -I - "$L" "$A" <<'PY'
import sys, os, struct, io
from PIL import Image, ImageDraw, ImageFont, ImageFilter
L, A = sys.argv[1], sys.argv[2]; R = L + '/res/5112/aero.msstyles'
def bmp(n):   # 32-bit DIB → RGBA, un-premultiplied when the alpha channel is used
    b = open('%s/BITMAP_%d_1033.bmp' % (R, n), 'rb').read(); o = 14 if b[:2] == b'BM' else 0
    hs, w, h, _, bpp = struct.unpack_from('<IiiHH', b, o)
    if bpp != 32: return Image.open(io.BytesIO(b)).convert('RGBA')
    off = struct.unpack_from('<I', b, 10)[0] if o else hs
    raw = b[off:off + w * abs(h) * 4]; im = Image.frombuffer('RGBA', (w, abs(h)), raw, 'raw', 'BGRA', 0, 1)
    if h > 0: im = im.transpose(Image.FLIP_TOP_BOTTOM)
    px = list(im.getdata())
    if max(p[3] for p in px) == 0: return Image.frombuffer('RGBA', im.size, bytes(x for p in px for x in (p[0], p[1], p[2], 255)), 'raw', 'RGBA', 0, 1)
    im.putdata([(min(255, r * 255 // a), min(255, g * 255 // a), min(255, b_ * 255 // a), a) if a else (0, 0, 0, 0) for r, g, b_, a in px])
    return im
def widen(im, w, l, r):
    o = Image.new('RGBA', (w, im.height)); o.paste(im.crop((0, 0, l, im.height)), (0, 0))
    o.paste(im.crop((im.width - r, 0, im.width, im.height)), (w - r, 0))
    o.paste(im.crop((l, 0, im.width - r, im.height)).resize((w - l - r, im.height), Image.NEAREST), (l, 0)); return o
def states(im, n): h = im.height // n; return [im.crop((0, i * h, im.width, (i + 1) * h)) for i in range(n)]
# start button
pill = states(bmp(502), 3)
flag = Image.open(L + '/res/5112/explorer.exe/icons/7103_48.png').convert('RGBA').resize((20, 20), Image.LANCZOS)
# the style says lfHeight -19 (em 19 px); drawn at 16 px, which matches the 5112-era concept shot
# (lh_concepts/1456384117.or.45878.png: cap height 11 px, ours at 19 px was 14 px). START_PX=19 for the literal size
font = ImageFont.truetype(L + '/resources/fonts/5112/segoeui.ttf', int(os.environ.get('START_PX', '16')))
for st, iid in zip(pill, (1179, 1180, 1181)):
    im = st.copy(); cl, cr, ct, cb = 7, 19, 0, 2; ch = im.height - ct - cb
    im.alpha_composite(flag, (cl, ct + (ch - 20) // 2))
    tx = cl + 20 + 3; asc, desc = font.getmetrics(); ty = ct + (ch - asc - desc) // 2 + 1
    sh = Image.new('RGBA', im.size); ImageDraw.Draw(sh).text((tx + 2, ty + 2), 'Start', font=font, fill=(75, 75, 75, 255))
    im.alpha_composite(sh.filter(ImageFilter.GaussianBlur(0.6)))   # TEXTSHADOWTYPE CONTINUOUS: a soft offset copy
    ImageDraw.Draw(im).text((tx, ty), 'Start', font=font, fill=(255, 255, 255, 255))
    im.save('%s/%d.png' % (A, iid))
bmp(298).save(A + '/1187.png')
tb = states(bmp(517), 6)
for idx, iid in ((0, 1182), (1, 1184), (4, 1185)): widen(tb[idx], 127, 9, 9).save('%s/%d.png' % (A, iid))
widen(tb[2], 127, 9, 9).save(A + '/taskbutton_pressed.png'); widen(tb[5], 127, 9, 9).save(A + '/taskbutton_hotchecked.png')
widen(bmp(296), 127, 12, 10).save(A + '/1183.png')
bmp(528).save(A + '/tray_bg.png')
print('aero5112: %d files' % len(os.listdir(A)))
PY
