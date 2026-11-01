#!/usr/bin/env python3
# lhde-import's Aero patches (tools/lhdetree.py AERO_PATCHES; docs/USAGE.md "The Aero patches for 4074"), on stand-in files of our own (no
# Microsoft or third-party style: the known patches are looked up by their sha256 passed in, the unknown one is recognised by
# its bytes) in a scratch Windows tree. Usage: tests/aero-patches.py
import os
import shutil
import subprocess
import sys
import tempfile

TOP = os.path.realpath(os.path.join(os.path.dirname(__file__), '..'))
sys.path.insert(0, os.path.join(TOP, 'tools'))
import lhdetree as T     # noqa: E402

fails = 0


def check(what, want, got):
    global fails
    if want == got:
        print('ok   %s' % what)
    else:
        print('FAIL %s:\n  expected: %r\n  got:      %r' % (what, want, got))
        fails += 1


t = tempfile.mkdtemp(prefix='lhde-aero-patches.')
try:
    u16 = lambda s: s.encode('utf-16-le')   # noqa: E731
    stand = os.path.join(t, 'stand-in.msstyles')
    open(stand, 'wb').write(b'MZ' + u16('nothing of the kind'))
    patch = os.path.join(t, 'aero_demo.msstyles')     # what a reconstruction of the private Aero carries
    open(patch, 'wb').write(b'MZ' + T.DEMOCHECK + u16('CompositedWindow'))
    jadeish = os.path.join(t, 'glassy.msstyles')      # a Jade-based patch: CompositedWindow, no DemoCheck
    open(jadeish, 'wb').write(b'MZ' + u16('CompositedWindow'))

    # ---- the table: every known file by its hash
    rows = {sha: T.style_release(stand, sha) for sha in T.AERO_PATCHES}
    titles = sorted(r[0] for r in rows.values())
    check('known patches', ['RealAero 0.2 by Lucas Brooks', 'RealAero 0.3 by Lucas Brooks', 'RealAero pre-0.1 by Lucas Brooks',
                            'TWIWMTB Aero by BOFH'], titles)
    bofh = rows['75d2a14b37863cd415aeba4da0f81139fcb4439370fdfbb2837ff19d3ac948b7']
    check('TWIWMTB: author', 'BOFH', bofh[1][T.A_AUTHOR])
    check('TWIWMTB: build', '4074', bofh[1][T.A_BUILD])
    check('TWIWMTB: source names the archive', True, 'dwm-aero-theme-for-4074.rar' in bofh[1][T.A_SOURCE])
    check('TWIWMTB: description', True, bofh[1][T.A_DESC].startswith('TWIWMTB Aero by BOFH: ') and 'Jade' in bofh[1][T.A_DESC])
    check('TWIWMTB: not RealAero', False, 'RealAero' in bofh[1][T.A_DESC] or 'Lucas' in bofh[1][T.A_SOURCE])
    check('TWIWMTB: its style name', 'twiwmtb-aero', T.known_patch(stand, '75d2a14b37863cd415aeba4da0f81139fcb4439370fdfbb2837ff19d3ac948b7')[2])
    ra = rows['fa153e6b15ad7682bd06c15982864b5bf710138f3aa54a476e93835911325c3e']
    check('RealAero: author', 'Lucas Brooks', ra[1][T.A_AUTHOR])
    check('RealAero: its style name', 'realaero', T.known_patch(stand, 'fa153e6b15ad7682bd06c15982864b5bf710138f3aa54a476e93835911325c3e')[2])
    # ---- unknown files
    unk = T.style_release(patch)
    check('unknown patch: title', 'an Aero patch for 4074 (unknown)', unk and unk[0])
    check('unknown patch: no author named', 'unknown', unk and unk[1][T.A_AUTHOR])
    check('unknown patch: description', "an Aero patch for 4074 (unknown): the private Aero's DemoCheck with 4074's CompositedWindow parts",
          unk and unk[1][T.A_DESC])
    check('unknown patch: not called RealAero', False, 'RealAero' in repr(unk))
    check('not a patch', None, T.style_release(stand))
    check('CompositedWindow without DemoCheck: not recognised', None, T.style_release(jadeish))
    check('has_composited', [True, True, False], [T.has_composited(patch), T.has_composited(jadeish), T.has_composited(stand)])

    # ---- lhde-import --style on a scratch tree: names, annotations
    W = os.path.join(t, 'w')
    env = dict(os.environ, LHDE_WINDOWS=W)

    def imp(*a):
        r = subprocess.run([os.path.join(TOP, 'tools', 'lhde-import'), '--windows', W, '--no-recipes', '--style'] + list(a),
                           env=env, capture_output=True, text=True)
        return r.stdout + r.stderr
    out = imp(patch)
    check('--style: an unknown patch is announced', True, 'is an Aero patch for 4074 (unknown)' in out)
    check('--style: an unknown patch is named by its file', True, os.path.isfile(os.path.join(W, 'local', 'themes', 'aero_demo.msstyles')))
    imp(jadeish, 'zzglass')
    imp(stand, 'zzplain')
    try:
        os.setxattr(W, 'user.lhde-test', b'1')
        xattrs = True
    except OSError:
        xattrs = False
    if xattrs:
        d = os.getxattr(os.path.join(W, 'local', 'themes', 'aero_demo.msstyles'), T.A_DESC).decode()
        check('--style: the unknown patch annotated', unk[1][T.A_DESC], d)
        d = os.getxattr(os.path.join(W, 'local', 'themes', 'zzplain.msstyles'), T.A_DESC).decode()
        check('--style: another style annotated by name', 'zzplain visual style (dropped in with lhde-import --style)', d)
    else:
        print('skip annotations: no user extended attributes in %s' % t)
finally:
    shutil.rmtree(t, ignore_errors=True)
print('%d failed' % fails)
sys.exit(1 if fails else 0)
