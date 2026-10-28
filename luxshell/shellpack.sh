#!/bin/sh
# M0 asset pack: copy the extracted Longhorn images the shell packs reference (ids per SHELL-DESIGN.md)
# lhde: a research tool (it reads the workspace's extractions: res/, resources/, themes/tools, tools/lhstyle.py). It writes
# the x11 lab's asset layout into $SHELLPACK_OUT (default ./shellpack-out): imageres/, imageres-4051/, shell32/, startpanel-*/,
# aero5112/; tools/lhde-import --from-workspace maps that layout into the data directory's packs/<build>/. Before the next
# import, point it at the workspace's x11/lhshell/assets or copy the output there. LHDE_WORKSPACE (default ~/fun/longhorn).
WS=${LHDE_WORKSPACE:-$HOME/fun/longhorn}
R=$WS/res; A=${SHELLPACK_OUT:-$PWD/shellpack-out}
mkdir -p "$A/imageres" "$A/sidebar-jade74" "$A/sidebar-slate51" "$A/wallpaper"
for f in $R/4074/imageres.dll/PNGFILE_*_1033.png; do id=$(basename $f | cut -d_ -f2); cp "$f" "$A/imageres/$id.png"; done
# 4074 Jade Sidebar theme class streams, named by part/state (from the decompiled style)
awk '/^\[Sidebar\./{sec=$0; gsub(/[\[\]]/,"",sec); gsub(/Sidebar\./,"",sec); gsub(/[()]/,"_",sec); sub(/_$/,"",sec)} /PNGSTREAM = STREAM#/{id=$3; sub(/STREAM#/,"",id); if(sec!="") print sec, id}' $R/4074/jade_longhorn.ini | while read part id; do cp "$R/4074/jade.msstyles/STREAM_${id}_1033.png" "$A/sidebar-jade74/$part.png"; done
# 4051 Slate Sidebar class (BLOB resources; the ini only records IMAGEDATA, so map by order: BLOB 364.. follow the same part order as 4074 slate STREAM 394..)
i=0; awk '/^\[Sidebar\./{sec=$0; gsub(/[\[\]]/,"",sec); gsub(/Sidebar\./,"",sec); gsub(/[()]/,"_",sec); sub(/_$/,"",sec)} /PNGSTREAM = STREAM#/{id=$3; sub(/STREAM#/,"",id); if(sec!="") print sec, id}' $R/4074/slate_longhorn.ini | while read part id; do bid=$((id - 394 + 364)); [ -f "$R/4051/slate.msstyles/BLOB_${bid}_1033.png" ] && cp "$R/4051/slate.msstyles/BLOB_${bid}_1033.png" "$A/sidebar-slate51/$part.png"; done
cp "$WS"/resources/wallpapers/mock/*.jpg "$A/wallpaper/"
echo "imageres: $(ls $A/imageres | wc -l)  jade74 sidebar parts: $(ls $A/sidebar-jade74 | wc -l)  slate51 sidebar parts: $(ls $A/sidebar-slate51 | wc -l)"
# M6 start menu (SHELL-DESIGN 6.3): the StartPanel class (+ StartMenu::Toolbar / MenuBand for the All Programs cascade) of the
# 4074 Jade and Slate msstyles → assets/startpanel-<theme>/: one PNG per part/state (the 32-bit BMPs are premultiplied,
# un-premultiplied here), and startpanel.ini = the decompiled sections with the FONT blobs decoded as "face, lfHeight, weight, italic".
# Plus the Longhorn shell32 icons the places column uses, at 24 and 32 px → assets/shell32/<size>/<id>.png.
X=$WS/x11
python3 -I - "$X" "$A" <<'PY'
import sys, os, re, struct, subprocess, io
X, A = sys.argv[1], sys.argv[2]; L = os.path.dirname(X)
sys.path.insert(0, L + '/themes/tools')
from pe_resources import PEResources
from PIL import Image
def reg(name):
    sec = None
    for line in open(X + '/themes.ini'):
        m = re.match(r'\s*\[(.*)\]', line)
        if m: sec = m.group(1); continue
        m = re.match(r'\s*msstyles\s*=\s*(\S+)', line)
        if m and sec == name: p = m.group(1); return p if p.startswith('/') else os.path.join(X, p)
def font(s):
    b = s.encode('utf-16le', 'surrogatepass')
    h, = struct.unpack_from('<i', b, 0); w, = struct.unpack_from('<i', b, 16); it = b[20]
    face = b[28:].decode('utf-16le', 'replace').split('\0')[0].strip()
    return '%s, %d, %d, %d' % (face, h, w, it)
def bmp(styles, bid, tcolor=None):   # RT_BITMAP → PIL RGBA (32-bit BI_RGB with premultiplied alpha → straight alpha)
    d = PEResources(styles).get(2, bid)
    hs, = struct.unpack_from('<I', d, 0); bpp, = struct.unpack_from('<H', d, 14)
    im = Image.open(io.BytesIO(b'BM' + struct.pack('<IHHI', 14 + len(d), 0, 0, 14 + hs + (4 * (1 << bpp) if bpp <= 8 else 0)) + d))
    if bpp != 32:   # TRANSPARENT = TRUE parts without alpha: TRANSPARENTCOLOR (default magenta) is the hole
        im = im.convert('RGBA')
        if tcolor:
            px = im.load()
            for y in range(im.height):
                for x in range(im.width):
                    if px[x, y][:3] == tcolor: px[x, y] = (0, 0, 0, 0)
        return im
    w, h = im.size; raw = d[hs:]; stride = w * 4; out = Image.new('RGBA', (w, h)); px = out.load()
    for y in range(h):
        row = raw[(h - 1 - y) * stride:(h - y) * stride]
        for x in range(w):
            b_, g, r, a = row[4 * x:4 * x + 4]
            px[x, y] = (min(255, r * 255 // a), min(255, g * 255 // a), min(255, b_ * 255 // a), a) if a else (0, 0, 0, 0)
    return out
# Jade and Slate (4074, variant NORMALDEFAULT) and the three Luna colour schemes of the 4051 luna.msstyles (an XP visual
# style compiled in the Longhorn format; variants NORMALBLUE / NORMALHOMESTEAD / NORMALMETALLIC) → startpanel-luna-<scheme>
for theme, name, variant in (('jade', 'jade4074', None), ('slate', 'slate4074', None),
                             ('luna-blue', 'luna', 'NORMALBLUE'), ('luna-olive', 'luna-olive', 'NORMALHOMESTEAD'), ('luna-silver', 'luna-silver', 'NORMALMETALLIC')):
    styles = reg(name); out = '%s/startpanel-%s' % (A, theme); os.makedirs(out, exist_ok=True)
    txt = subprocess.run(['python3', '-I', L + '/tools/lhstyle.py'] + (['--variant', variant] if variant else []) + [styles], capture_output=True, text=True).stdout
    keep, ini, sec, secs = False, [], None, []
    for line in txt.splitlines():   # sections first, so an image knows its section's TRANSPARENT / TRANSPARENTCOLOR
        m = re.match(r'\[(.*)\]', line)
        if m:
            sec = m.group(1); keep = sec.startswith('StartPanel') or sec.startswith('StartMenu::')
            if keep: secs.append((sec, []))
            continue
        if keep and '=' in line: secs[-1][1].append(line)
    for sec, lines in secs:
        ini.append('\n[%s]' % sec)
        kv = dict((t.split('=', 1)[0].strip(), t.split('=', 1)[1].strip()) for t in lines)
        tcol = None
        if kv.get('TRANSPARENT') == 'TRUE':
            tc = kv.get('TRANSPARENTCOLOR', '255 0 255').split(); tcol = tuple(int(x) for x in tc[:3])
        for line in lines:
            k, v = [t.strip() for t in line.split('=', 1)]
            if k == 'FONT': v = font(line.split('= ', 1)[1])
            if k == 'IMAGEFILE':
                bid = int(v.split('#')[1]); part = sec.replace('StartPanel.', '').replace('StartPanel', 'STARTPANEL').replace('StartMenu::', 'MENU_').replace('.', '_')
                part = re.sub(r'\((\w+)\)', r'_\1', part)
                bmp(styles, bid, tcol).save('%s/%s.png' % (out, part)); v = '%s.png ; bitmap#%d' % (part, bid)
            ini.append('  %s = %s' % (k, v))
    open(out + '/startpanel.ini', 'w').write('; StartPanel / StartMenu classes of %s (tools/lhstyle.py; FONT = face, lfHeight, weight, italic)\n' % styles + '\n'.join(ini).lstrip() + '\n')
    print('startpanel-%s: %d files' % (theme, len(os.listdir(out))))
# shell32 icons (4074): places column and fallbacks
dll = L + '/resources/system32/4074/shell32.dll'; st = PEResources(dll)
icons = {n: d for (k, n, l), d in st.resources.items() if k == 3}
for size in (16, 24, 32, 48):
    os.makedirs('%s/shell32/%d' % (A, size), exist_ok=True)
    for (k, n, l), d in st.resources.items():
        if k != 14 or n not in (1, 3, 4, 16, 21, 23, 24, 25, 28, 134, 137, 235, 236, 237, 269, 348, 386, 387, 357, 346, 221, 329, 45): continue
        cnt, = struct.unpack_from('<H', d, 4); ents = []
        for i in range(cnt):
            w, h, c, r, planes, bpp, sz, iid = struct.unpack_from('<BBBBHHIH', d, 6 + 14 * i)
            if iid in icons: ents.append((w or 256, bpp, iid))
        exact = [e for e in ents if e[0] == size]; pick = max(exact, key=lambda e: e[1]) if exact else max([e for e in ents if e[0] >= size] or ents, key=lambda e: (-e[0], e[1]))
        data = icons[pick[2]]; w = pick[0]
        im = Image.open(io.BytesIO(struct.pack('<HHH', 0, 1, 1) + struct.pack('<BBBBHHII', w % 256, w % 256, 0, 0, 1, pick[1], len(data), 22) + data)).convert('RGBA')
        if im.size[0] != size: im = im.resize((size, size), Image.LANCZOS)
        im.save('%s/shell32/%d/%d.png' % (A, size, n))
print('shell32 icons: %d' % len(os.listdir(A + '/shell32/24')))
PY
# slate4051 pack (themes/slate4051): the 4051 imageres.dll PNGs that differ from 4074's or that 4074 lacks →
# assets/imageres-4051/<id>.png (the pack's R() takes them from there, everything else from assets/imageres). Among them the
# Slate tile arrow 1105/1106 (4051's is a right-angled corner triangle), tile restore/close 1058-1062, toast parts 4003-4011.
python3 -I - "$X" "$A" <<'PY'
import sys, os
X, A = sys.argv[1], sys.argv[2]; L = os.path.dirname(X)
sys.path.insert(0, L + '/themes/tools')
from pe_resources import PEResources
r = PEResources(L + '/resources/system32/4051/imageres.dll'); out = A + '/imageres-4051'; os.makedirs(out, exist_ok=True)
n = 0
for (kind, name, lang), d in r.resources.items():
    if kind != 'PNGFILE': continue
    ref = '%s/imageres/%s.png' % (A, name)
    if os.path.exists(ref) and open(ref, 'rb').read() == d: continue
    open('%s/%s.png' % (out, name), 'wb').write(d); n += 1
print('imageres-4051: %d images differ from or are missing in 4074' % n)
PY
# aero5112 pack (themes/aero5112): the Vista 5112 taskbar art → assets/aero5112 (its own script, runnable alone)
SHELLPACK_OUT=$A "$(dirname "$0")/shellpack-aero5112.sh"
