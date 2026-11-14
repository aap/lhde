#!/usr/bin/env python3
# Which program opens a type, on a scratch XDG tree (a home with its config and data, a config dir, lhde's data dir with
# lhde-mimeapps.list, a distribution's data dir with a mimeinfo.cache, a small shared-mime-info database of our own): the
# verdicts of tools/lhdemime.py (lhde-mime) against the spec's order, flxp's own reader (build/tests/assoctest default) against
# lhde-mime's flxp verdict, xdg-mime query default (when installed) against lhde-mime's xdg-open verdict, gio mime (when
# installed) against its GIO verdict; lhde-mime default / reset on the user's file and assoctest setdefault / reset; lhde-mime
# check's findings; explain's type detection (glob, magic, directory). Usage: tests/mime.py
import os
import shutil
import struct
import subprocess
import sys
import tempfile

TOP = os.path.realpath(os.path.join(os.path.dirname(__file__), '..'))
sys.path.insert(0, os.path.join(TOP, 'tools'))
import lhdemime as M   # noqa: E402

ASSOC = os.path.join(TOP, 'build/tests/assoctest')
fails = 0


def check(what, want, got):
    global fails
    if want == got:
        print('ok   %s' % what)
    else:
        print('FAIL %s:\n  expected: %r\n  got:      %r' % (what, want, got))
        fails += 1


def write(path, text, mode='w'):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, mode) as f:
        f.write(text)


def desktop(path, name, prog, types, cats=''):
    write(path, '[Desktop Entry]\nType=Application\nName=%s\nExec=%s %%F\nMimeType=%s\n%s' % (
        name, prog, ''.join(t + ';' for t in types), 'Categories=%s\n' % cats if cats else ''))


T = tempfile.mkdtemp(prefix='lhde-mime.')
try:
    home = os.path.join(T, 'home')
    cfg, data = os.path.join(home, '.config'), os.path.join(home, '.local/share')
    etc, lhde, usr, bindir = os.path.join(T, 'etcxdg'), os.path.join(T, 'lhde'), os.path.join(T, 'usr'), os.path.join(T, 'bin')
    for p in ('deadbeef', 'pcmanfm', 'flxp', 'luxpad', 'gedit'):
        write(os.path.join(bindir, p), '#!/bin/sh\nexit 0\n')
        os.chmod(os.path.join(bindir, p), 0o755)
    # the distribution: a music player and a file manager claim folders, an editor claims text, its cache lists them
    # (deadbeef first, as update-desktop-database sorts), a mimeapps.list of its own, a stale entry, a legacy defaults.list
    desktop(usr + '/applications/deadbeef.desktop', 'DeaDBeeF', 'deadbeef', ['inode/directory', 'audio/flac'], 'Audio;AudioVideo;Player;')
    desktop(usr + '/applications/pcmanfm.desktop', 'PCManFM', 'pcmanfm', ['inode/directory'], 'System;FileManager;')
    desktop(usr + '/applications/gedit.desktop', 'gedit', 'gedit', ['text/plain', 'text/x-csrc'], 'Utility;TextEditor;')
    write(usr + '/applications/mimeinfo.cache', '[MIME Cache]\naudio/flac=deadbeef.desktop;\n'
          'inode/directory=deadbeef.desktop;pcmanfm.desktop;\ntext/plain=gedit.desktop;\ntext/x-csrc=gedit.desktop;ghost.desktop;\n')
    write(usr + '/applications/mimeapps.list', '[Default Applications]\ntext/plain=gedit.desktop\n')
    write(usr + '/applications/defaults.list', '[Default Applications]\naudio/flac=deadbeef.desktop\n')
    # lhde's data directory, before the distribution's: its programs (no cache, as a fresh make install without
    # update-desktop-database) and lhde-mimeapps.list as shipped
    desktop(lhde + '/applications/flxp.desktop', 'Explorer', 'flxp', ['inode/directory'], 'System;FileManager;')
    desktop(lhde + '/applications/luxpad.desktop', 'luxpad', 'luxpad', ['text/plain'], 'Utility;TextEditor;')
    shutil.copy(os.path.join(TOP, 'share/applications/lhde-mimeapps.list'), lhde + '/applications/')
    os.makedirs(etc)
    os.makedirs(cfg)
    # the mime database: two text types (one an alias), a PNG signature in magic
    write(usr + '/mime/globs2', '50:text/plain:*.txt\n50:text/x-csrc:*.c\n50:text/x-log:*.log\n50:image/png:*.png\n')
    write(usr + '/mime/subclasses', 'text/x-csrc text/plain\ntext/x-log text/plain\n')
    write(usr + '/mime/aliases', 'text/x-c text/x-csrc\n')
    write(usr + '/mime/text/x-csrc.xml', '<?xml version="1.0"?>\n<mime-type type="text/x-csrc"><comment>C source code</comment>'
          '</mime-type>\n')
    png = b'\x89PNG\r\n\x1a\n'
    write(usr + '/mime/magic', b'MIME-Magic\0\n[50:image/png]\n>0=' + struct.pack('>H', len(png)) + png + b'\n', 'wb')
    # gio's own cache files of the database (mime.cache), when the tool that writes them is there
    if shutil.which('update-mime-database'):
        subprocess.run(['update-mime-database', usr + '/mime'], capture_output=True)

    base = {'HOME': home, 'PATH': bindir + ':/usr/bin:/bin', 'XDG_CONFIG_HOME': cfg, 'XDG_DATA_HOME': data,
            'XDG_CONFIG_DIRS': etc, 'XDG_DATA_DIRS': lhde + ':' + usr, 'LANG': 'C', 'LC_ALL': 'C'}

    def env(desk=None):
        e = dict(base)
        if desk:
            e['XDG_CURRENT_DESKTOP'] = desk
        return e

    def model(e):
        return M.Db(M.Env(e))

    def verdicts(e, t):
        db = model(e)
        a, _, _, _ = db.resolve(t, 'cache')
        f, _, _, _ = db.resolve(t, 'files')
        x, _ = db.xdg_open(t)
        return a.id if a else None, x, f.id if f else None

    def assoc(e, *args):
        if not os.access(ASSOC, os.X_OK):
            return None
        r = subprocess.run([ASSOC] + list(args), env=e, capture_output=True, text=True)
        return r.stdout.split()

    def xdg(e, t):
        if not shutil.which('xdg-mime'):
            return None
        r = subprocess.run(['xdg-mime', 'query', 'default', t], env=e, capture_output=True, text=True)
        return r.stdout.strip() or None

    def gio(e, t):
        if not shutil.which('gio'):
            return None
        r = subprocess.run(['gio', 'mime', t], env=e, capture_output=True, text=True)
        line = r.stdout.split('\n')[0]
        return line.rsplit(': ', 1)[1].strip() if line.startswith('Default application') else None

    def agree(what, e, t, want):
        """want = (GIO, xdg-open, flxp): the model's verdicts, then each real reader against its own"""
        got = verdicts(e, t)
        check('%s: lhde-mime (GIO, xdg-open, flxp)' % what, want, got)
        a = assoc(e, 'default', t)
        if a is not None:
            check('%s: flxp\'s reader (assoctest)' % what, want[2], a[1] if a[1] != '-' else None)
        x = xdg(e, t)
        if shutil.which('xdg-mime'):
            check('%s: xdg-mime query default' % what, want[1], x)
        g = gio(e, t)
        if shutil.which('gio'):
            check('%s: gio mime' % what, want[0], g)

    # A. aap's case: no XDG_CURRENT_DESKTOP, so no lhde-mimeapps.list; nothing sets a folder default: GIO and xdg-open run
    #    the first program the distribution's cache lists, a music player; flxp (MimeType= of the files) its own
    agree('no desktop, inode/directory', env(), 'inode/directory', ('deadbeef.desktop', 'deadbeef.desktop', 'flxp.desktop'))
    agree('no desktop, text/plain', env(), 'text/plain', ('gedit.desktop', 'gedit.desktop', 'gedit.desktop'))
    # B. in an lhde session lhde's list beats the distribution's mimeapps.list and every claimant
    agree('lhde, inode/directory', env('lhde'), 'inode/directory', ('flxp.desktop',) * 3)
    agree('lhde, text/plain', env('lhde'), 'text/plain', ('luxpad.desktop',) * 3)
    agree('LHDE:GNOME, inode/directory', env('LHDE:GNOME'), 'inode/directory', ('flxp.desktop',) * 3)
    a = assoc(env('lhde'), 'default', 'inode/directory')
    if a is not None:
        check('flxp: inode/directory set by lhde\'s file', ['lhde', lhde + '/applications/lhde-mimeapps.list'], a[2:])
        check('flxp: the files read in an lhde session', [lhde + '/applications/lhde-mimeapps.list',
                                                           usr + '/applications/mimeapps.list',
                                                           usr + '/applications/defaults.list'], assoc(env('lhde'), 'files'))
    # C. a claimant of the type before its parent's default: gedit claims text/x-csrc, which has no default; text/x-log has
    #    neither, so GIO and flxp take text/plain's default and xdg-open, asking for the exact type, finds nothing
    agree('lhde, text/x-csrc', env('lhde'), 'text/x-csrc', ('gedit.desktop',) * 3)
    agree('lhde, text/x-log', env('lhde'), 'text/x-log', ('luxpad.desktop', None, 'luxpad.desktop'))
    check('alias text/x-c is text/x-csrc', 'text/x-csrc', model(env()).mime.canonical('text/x-c'))
    # D. the user's choice beats lhde: lhde-mime default writes ~/.config/mimeapps.list, reset takes it out again
    tool = [sys.executable, '-I', os.path.join(TOP, 'tools/lhde-mime')]
    r = subprocess.run(tool + ['default', 'text/plain', 'gedit'], env=env('lhde'), capture_output=True, text=True)
    check('lhde-mime default: exit', 0, r.returncode)
    user = cfg + '/mimeapps.list'
    check('lhde-mime default: the user\'s file', '[Default Applications]\ntext/plain=gedit.desktop\n\n[Added Associations]\n'
          'text/plain=gedit.desktop;\n', open(user).read() if os.path.isfile(user) else None)
    agree('user over lhde, text/plain', env('lhde'), 'text/plain', ('gedit.desktop',) * 3)
    r = subprocess.run(tool + ['reset', 'text/plain'], env=env('lhde'), capture_output=True, text=True)
    check('lhde-mime reset: the user\'s file', '[Default Applications]\n\n[Added Associations]\n',
          open(user).read() if os.path.isfile(user) else None)
    agree('after reset, text/plain', env('lhde'), 'text/plain', ('luxpad.desktop',) * 3)
    r = subprocess.run(tool + ['default', 'text/plain', 'nosuch'], env=env('lhde'), capture_output=True, text=True)
    check('lhde-mime default of a missing program: refused', 1, r.returncode)
    if os.access(ASSOC, os.X_OK):
        assoc(env('lhde'), 'setdefault', 'inode/directory', 'pcmanfm.desktop')
        agree('assoctest setdefault, inode/directory', env('lhde'), 'inode/directory', ('pcmanfm.desktop',) * 3)
        check('flxp: the default set by the user', 'user', assoc(env('lhde'), 'default', 'inode/directory')[2])
        assoc(env('lhde'), 'reset', 'inode/directory')
        agree('assoctest reset, inode/directory', env('lhde'), 'inode/directory', ('flxp.desktop',) * 3)
    # E. Removed Associations: the user takes the music player off folders; GIO falls to the next claimant, xdg-open
    #    (which reads no Removed Associations) does not
    write(user, '[Removed Associations]\ninode/directory=deadbeef.desktop;\n')
    agree('removed, inode/directory', env(), 'inode/directory', ('pcmanfm.desktop', 'deadbeef.desktop', 'flxp.desktop'))
    # F. a default naming a desktop file that is gone; the audit
    write(user, '[Default Applications]\naudio/flac=gone.desktop\n')
    found = M.check(model(env()))
    kinds = [k for k, _ in found]
    text = '\n'.join(t for _, t in found)
    check('check: deadbeef claims folders', True, 'deadbeef.desktop claims inode/directory but is not a file manager' in text)
    check('check: the folder verdict', True, 'inode/directory: no default; GIO and xdg-open run deadbeef.desktop' in text)
    check('check: pcmanfm is fine', False, 'pcmanfm.desktop claims' in text)
    check('check: the missing default', True, 'audio/flac=gone.desktop: no such desktop file' in text)
    check('check: lhde\'s data dir has no cache', True, any(k == 'cache' and 'lhde/applications has desktop files' in t
                                                            for k, t in found))
    check('check: the distribution\'s cache is stale', True, 'listed but gone: ghost.desktop' in text)
    check('check: defaults.list is legacy', True, any(k == 'legacy' and 'defaults.list' in t for k, t in found))
    check('check: lhde\'s list not read without XDG_CURRENT_DESKTOP', True, 'lhde' in kinds)
    check('check: in an lhde session no folder verdict', False,
          'inode/directory: no default' in '\n'.join(t for _, t in M.check(model(env('lhde')))))
    # a desktop-specific file holds defaults only: its Removed Associations are not read (by GLib, xdg-mime, flxp, lhde-mime)
    write(cfg + '/lhde-mimeapps.list', '[Removed Associations]\ninode/directory=flxp.desktop;\n')
    agree('lhde, a desktop-specific Removed', env('lhde'), 'inode/directory', ('flxp.desktop',) * 3)
    check('check: the desktop-specific Removed Associations', True,
          any(k == 'ignored' and 'lhde-mimeapps.list: [Removed Associations]' in t for k, t in M.check(model(env('lhde')))))
    os.remove(cfg + '/lhde-mimeapps.list')
    # G. explain's detection
    db = model(env())
    write(T + '/f/notes.txt', 'hello\n')
    write(T + '/f/picture', png + b'\0\0\0\rIHDR', 'wb')
    os.makedirs(T + '/f/dir')
    t, how = db.mime.detect(T + '/f/notes.txt')
    check('detect: a .txt by its glob', ('text/plain', True), (t, how.startswith('glob *.txt (weight 50')))
    t, how = db.mime.detect(T + '/f/picture')
    check('detect: a PNG without a name by magic', ('image/png', True), (t, how.startswith('magic (priority 50')))
    check('detect: a directory', ('inode/directory', 'a directory'), db.mime.detect(T + '/f/dir'))
    check('parents of text/x-csrc', ['text/plain', 'application/octet-stream'], db.mime.ancestors('text/x-csrc'))
    r = subprocess.run(tool + ['explain', T + '/f/dir'], env=env(), capture_output=True, text=True)
    check('explain a folder: the xdg-open verdict', True,
          'xdg-open would run: deadbeef.desktop because %s/applications/mimeinfo.cache lists it first and nothing sets a default'
          % usr in r.stdout)
finally:
    shutil.rmtree(T, ignore_errors=True)

print('%d failed' % fails if fails else 'all passed')
sys.exit(1 if fails else 0)
