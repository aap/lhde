#!/usr/bin/env python3
# Builds wux/w32/stdres.res: the user32 resources the shim serves for
# HINSTANCE NULL (OEM bitmaps, standard icons and cursors), taken from Wine's
# dlls/user32/resources. Icons are trimmed to their 16x16 and 32x32 32bpp
# entries and cursors to 32x32 32bpp to keep the blob small.
# Usage: python3 -I wux/w32/mkstdres.py WINE-SRC-DIR   (needs windres)
import os, struct, subprocess, sys, tempfile

if len(sys.argv) < 2:
    sys.exit('usage: mkstdres.py WINE-SRC-DIR')
wine = sys.argv[1]
rdir = os.path.join(wine, 'dlls/user32/resources')
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'stdres.res')

obm = {
    'trtype': 32732, 'lfarrowi': 32734, 'rgarrowi': 32735, 'dnarrowi': 32736,
    'uparrowi': 32737, 'combo': 32738, 'mnarrow': 32739, 'lfarrowd': 32740,
    'rgarrowd': 32741, 'dnarrowd': 32742, 'uparrowd': 32743, 'restored': 32744,
    'zoomd': 32745, 'reduced': 32746, 'restore': 32747, 'zoom': 32748,
    'reduce': 32749, 'lfarrow': 32750, 'rgarrow': 32751, 'dnarrow': 32752,
    'uparrow': 32753, 'close': 32754, 'old_restore': 32755, 'old_zoom': 32756,
    'old_reduce': 32757, 'btncorners': 32758, 'checkboxes': 32759, 'check': 32760,
    'btsize': 32761, 'old_lfarrow': 32762, 'old_rgarrow': 32763,
    'old_dnarrow': 32764, 'old_uparrow': 32765, 'size': 32766, 'old_close': 32767,
}
ocr = {
    'normal': 32512, 'ibeam': 32513, 'wait': 32514, 'cross': 32515, 'up': 32516,
    'size': 32640, 'sizenwse': 32642, 'sizenesw': 32643, 'sizewe': 32644,
    'sizens': 32645, 'sizeall': 32646, 'no': 32648, 'hand': 32649,
    'appstarting': 32650, 'help': 32651,
}
oic = {'sample': 32512, 'hand': 32513, 'ques': 32514, 'bang': 32515,
       'note': 32516, 'winlogo': 32517, 'shield': 32518}

def trim(path, sizes, dst):
    d = open(path, 'rb').read()
    _, typ, n = struct.unpack('<HHH', d[:6])
    keep, seen = [], set()
    for i in range(n):
        e = d[6 + 16 * i:22 + 16 * i]
        w, h, cc, r, p, b, sz, off = struct.unpack('<BBBBHHII', e)
        img = d[off:off + sz]
        if img[:4] == b'\x89PNG': continue
        bpp = struct.unpack('<H', img[14:16])[0]
        w = w or 256
        if bpp != 32 or w not in sizes or w in seen: continue
        seen.add(w)
        keep.append((e, img))
    hdr = struct.pack('<HHH', 0, typ, len(keep))
    off = 6 + 16 * len(keep)
    body = b''
    for e, img in keep:
        hdr += e[:8] + struct.pack('<II', len(img), off + len(body))
        body += img
    open(dst, 'wb').write(hdr + body)

with tempfile.TemporaryDirectory() as tmp:
    rc = []
    for name, id in obm.items():
        rc.append('%d BITMAP "%s"' % (id, os.path.join(rdir, 'obm_%s.bmp' % name)))
    for name, id in ocr.items():
        f = os.path.join(tmp, 'ocr_%s.cur' % name)
        trim(os.path.join(rdir, 'ocr_%s.cur' % name), {32}, f)
        rc.append('%d CURSOR "%s"' % (id, f))
    for name, id in oic.items():
        f = os.path.join(tmp, 'oic_%s.ico' % name)
        trim(os.path.join(rdir, 'oic_%s.ico' % name), {16, 32}, f)
        rc.append('%d ICON "%s"' % (id, f))
    rcf = os.path.join(tmp, 'stdres.rc')
    open(rcf, 'w').write('\n'.join(rc) + '\n')
    subprocess.check_call(['x86_64-w64-mingw32-windres', '-O', 'res', '-i', rcf, '-o', out])
print('wrote', out, os.path.getsize(out))
