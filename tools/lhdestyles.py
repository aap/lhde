"""lhde-import --styles DIR: a directory of downloaded visual styles into the Windows tree's local/themes (docs/USAGE.md "Adding a
directory of styles").

DIR is read only and nothing in it is ever run: every *.msstyles below it is taken, also those inside .zip, .rar, .7z and .exe
files (self-extracting theme installers: 7-Zip, RAR, zip and NSIS ones; 7z unpacks each into a fresh scratch directory, unrar
the RAR ones when it is installed, an archive inside an archive too, up to three deep). Each style is imported as `lhde-import --style FILE NAME` does it:
local/themes/NAME.msstyles, its Shell/ (the task pane's shellstyle.dll per colour scheme) when one lies beside it, the recipes
that read NAME. The plan before the copy:
  - a file that is no visual style (no TEXTFILE resource), or that ut-dump cannot open, is listed and left out;
  - the same bytes (sha256) are taken once: a second copy in DIR, or a style the tree has already (a source's, or one
    imported before), is a duplicate and skipped;
  - NAME is the file's stem made a name (lower case, every run of other characters one '-'); two different styles with the
    same stem, or a stem the tree already has for another style, each get the name of the folder they came from (the nearest
    one that says more than the stem or "Themes", "Resources", "Visual Style"), then -2, -3. A name ending in -xp (which the
    style list keeps for retired backports) gets -vs;
  - a .theme file beside a style (in its folder or the one above) that names it: the wallpaper it names, found in the
    download, goes to local/wallpapers/NAME/ (the Desktop tab lists it); everything else a .theme says (colour scheme, icons,
    cursors, sounds, screen saver) has no place in the Windows tree and is ignored.
The summary at the end: imported, duplicates, not styles, archives that would not open, directories without a style.
"""
import os
import re
import shutil
import subprocess
import tempfile

import lhdeimport as L
import lhdetree as T

ARCHIVES = ('.zip', '.rar', '.7z', '.exe', '.cab')
PROGRAMS = ('PE', 'ELF', 'MachO', 'TE', 'COFF')   # what 7z opens an .exe as when it is no archive: a program, never run
GENERIC = {'', 'windir', 'resources', 'resource', 'themes', 'theme', 'visual-style', 'visual-styles', 'visualstyle', 'styles',
           'style', 'shell', 'тема', 'темы', 'msstyles', 'files', 'install', 'setup'}
DEPTH = 3


def out(*a):
    print(*a, flush=True)   # in order with lhde-import's log lines


def slug(s):
    """a style name from a file or folder name: lower case, other characters as '-' (letters of any script are kept)"""
    s = re.sub(r'[^\w]+|_+', '-', s.lower()).strip('-')
    return s


def name_ok(n):
    return n if not n.endswith('-xp') else n + '-vs'


class Node:
    """a directory or an archive of DIR (archives also inside archives): what was found below it"""
    def __init__(self, rel, kind, parent=None):
        self.rel, self.kind, self.parent = rel, kind, parent
        self.children, self.direct, self.styles = [], 0, 0
        self.error = None
        if parent:
            parent.children.append(self)


def seven_type(path):
    """7z's view of an archive: (type, error); no type when it does not open"""
    r = subprocess.run(['7z', 'l', '-slt', '-pnone', '--', path], capture_output=True, stdin=subprocess.DEVNULL, timeout=300)
    txt = r.stdout.decode('utf-8', 'replace')
    head = txt.split('\n----------', 1)[0]
    t = next((l[7:].strip() for l in head.splitlines() if l.startswith('Type = ')), None)
    err = None
    if r.returncode:
        msg = r.stderr.decode('utf-8', 'replace') + txt
        m = re.search(r'(Cannot open the file as archive|Headers Error|Unsupported Method|Wrong password|Data Error|CRC Failed'
                      r'|Can not open encrypted archive|Unexpected end of archive)', msg)
        err = m.group(1) if m else '7z exit %d' % r.returncode
    return t, err


def extract(path, dest, typ):
    """7z x into the empty directory dest (the archive's own paths, nothing written outside it); the error 7z reports. A RAR
    archive goes through unrar when it is installed (7-Zip builds without the RAR codec, Void's among them, open RAR files but
    cannot unpack RAR 2.9's method: "Unsupported Method")"""
    os.makedirs(dest)
    if typ.lower().startswith('rar') and shutil.which('unrar'):
        r = subprocess.run(['unrar', 'x', '-y', '-p-', '-idq', '--', path, dest + os.sep], capture_output=True,
                           stdin=subprocess.DEVNULL, timeout=600)
        if r.returncode:
            msg = r.stderr.decode('utf-8', 'replace') + r.stdout.decode('utf-8', 'replace')
            m = re.search(r'(checksum error|Corrupt header is found|password is incorrect|Unexpected end of archive)', msg)
            return 'unrar: ' + (m.group(1) if m else 'exit %d' % r.returncode)
        return None
    r = subprocess.run(['7z', 'x', '-y', '-bd', '-pnone', '-o' + dest, '--', path], capture_output=True, stdin=subprocess.DEVNULL,
                       timeout=600)
    if r.returncode:
        msg = r.stderr.decode('utf-8', 'replace') + r.stdout.decode('utf-8', 'replace')
        m = re.search(r'(Headers Error|Unsupported Method|Wrong password|Data Error|CRC Failed|Unexpected end of archive)', msg)
        e = m.group(1) if m else '7z exit %d' % r.returncode
        return e + (' (a RAR method this 7z lacks: install unrar)' if e == 'Unsupported Method' and typ.lower().startswith('rar') else '')
    return None


def inside(p, root):
    rp, rr = os.path.realpath(p), os.path.realpath(root)
    return rp == rr or rp.startswith(rr + os.sep)


def style_colors(path):
    """the colour scheme names of a style's themes.ini ([ColorScheme.NormalColor] -> NormalColor)"""
    try:
        pe = L.PE(path)
    except Exception:
        return []
    names = []
    for n, v in pe.items('TEXTFILE'):
        if str(n) != 'THEMES_INI':
            continue
        txt = v.decode('utf-16le', 'replace') if v[:2] in (b'\xff\xfe',) or v[1:2] == b'\0' else v.decode('latin-1')
        names += re.findall(r'^\s*\[ColorScheme\.([^\]\s]+)\]', txt, re.M | re.I)
    return names


def is_style(path):
    try:
        return bool(L.PE(path).items('TEXTFILE'))
    except Exception:
        return False


def ut_dump_ok(path):
    """libwuxtheme opens it (ut-dump --list exits 0): None, else what went wrong"""
    ut = L.tool('ut-dump')
    if not ut:
        return None
    try:
        r = subprocess.run([ut, path, '--list'], capture_output=True, stdin=subprocess.DEVNULL, timeout=120)
    except subprocess.TimeoutExpired:
        return 'ut-dump timed out'
    if r.returncode == 0:
        return None
    if r.returncode < 0:
        return 'ut-dump crashed (signal %d)' % -r.returncode
    return 'ut-dump: %s' % (r.stderr.decode('utf-8', 'replace').strip().splitlines() or ['exit %d' % r.returncode])[-1][:200]


def read_theme(path):
    """a Windows .theme file: {(section lower, key lower): value}"""
    d, sec = {}, ''
    try:
        raw = open(path, 'rb').read()
    except OSError:
        return d
    txt = raw.decode('utf-16') if raw[:2] in (b'\xff\xfe', b'\xfe\xff') else raw.decode('cp1252', 'replace')
    for line in txt.splitlines():
        line = line.strip()
        if line.startswith('[') and line.endswith(']'):
            sec = line[1:-1].strip().lower()
        elif '=' in line and not line.startswith(';'):
            k, v = line.split('=', 1)
            d[(sec, k.strip().lower())] = v.strip()
    return d


def winparts(p):
    return [x for x in re.split(r'[\\/]+', p) if x and x not in ('.',)]


class Scan:
    def __init__(self, top, tmp):
        self.top, self.tmp = top, tmp
        self.found = []        # dicts: path, origin (display), node, root (the item root on disk), dir
        self.themes = {}       # directory on disk -> [.theme files]
        self.failed = []       # (origin, why): archives that would not open
        self.programs = []     # origins of .exe files that are programs, not archives
        self.n = 0
        self.root = Node('', 'dir')
        self.seven = bool(shutil.which('7z'))

    def disp(self, chain):
        return ' > '.join(chain)

    def walk(self, d, chain, node, item_root, depth):
        """d: a directory on disk; chain: the display path parts (DIR-relative, archives as their own part)"""
        try:
            names = sorted(os.listdir(d))
        except OSError:
            return
        for f in names:
            p = os.path.join(d, f)
            if os.path.islink(p) or not inside(p, item_root if depth else self.top):
                continue
            if os.path.isdir(p):
                sub = Node(self.disp(chain + [f]), 'dir', node)
                self.walk(p, chain + [f], sub, item_root if (depth or chain) else p, depth)
                continue
            if not os.path.isfile(p):
                continue
            lo = f.lower()
            if lo.endswith('.msstyles'):
                self.found.append({'path': p, 'origin': self.disp(chain + [f]), 'node': node, 'root': item_root if (depth or chain) else d,
                                   'chain': chain + [f]})
                node.direct += 1
            elif lo.endswith('.theme'):
                self.themes.setdefault(d, []).append(p)
            elif lo.endswith(ARCHIVES) and depth < DEPTH:
                if self.seven:
                    self.archive(p, chain + [f], node, depth)
                elif depth == 0:
                    self.failed.append((self.disp(chain + [f]), 'no 7z installed'))

    def archive(self, p, chain, parent, depth):
        origin = self.disp(chain)
        typ, err = seven_type(p)
        if not typ or typ in PROGRAMS:
            if depth == 0:
                if typ in PROGRAMS:
                    self.programs.append(origin)
                    self.failed.append((origin, 'a program (%s) that 7z cannot unpack; not run' % typ))
                else:
                    self.failed.append((origin, err or 'not an archive 7z opens'))
            return
        self.n += 1
        dest = os.path.join(self.tmp, 'a%04d' % self.n)
        xerr = extract(p, dest, typ)
        node = Node(origin, 'archive', parent)
        node.error = xerr or err
        self.walk(dest, chain, node, dest, depth + 1)

    def run(self):
        self.walk(self.top, [], self.root, self.top, 0)
        def count(n):
            n.styles = n.direct + sum(count(c) for c in n.children)
            return n.styles
        count(self.root)


def units(root):
    """the items of DIR whose outcome the summary reports: DIR's entries, and the entries of a collection (a directory or
    archive with no style of its own and two or more entries holding styles), recursively"""
    res = []
    def go(n):
        for c in n.children:
            if c.direct == 0 and sum(1 for x in c.children if x.styles) >= 2:
                go(c)
            else:
                res.append(c)
    go(root)
    return res


def existing(W):
    """sha256 -> name of every style the tree has (index/styles), and the names taken"""
    shas, taken = {}, {'classic', 'none', 'auto'}
    d = os.path.join(W, 'index', 'styles')
    if os.path.isdir(d):
        for f in sorted(os.listdir(d)):
            p = os.path.join(d, f)
            if f.lower().endswith('.msstyles'):
                taken.add(f[:-9])
                if os.path.isfile(p):
                    shas.setdefault(L.sha256(p), f[:-9])
            elif not f.endswith('.scheme'):
                taken.add(f)
    lt = os.path.join(W, 'local', 'themes')
    if os.path.isdir(lt):
        for f in os.listdir(lt):
            taken.add(f[:-9] if f.lower().endswith('.msstyles') else f)
    return shas, taken


def folder_name(chain, stem):
    """the nearest folder (or archive) of a style's path that names more than the stem does"""
    for part in reversed(chain[:-1]):
        b = part
        if b.lower().endswith(ARCHIVES):
            b = b.rsplit('.', 1)[0]
        s = slug(b)
        if s and s != stem and s not in GENERIC and not s.startswith('windir'):
            return s
    return None


def plan(scan, W):
    shas, taken = existing(W)
    seen = {}
    rows = {'import': [], 'dup': [], 'have': [], 'bad': []}
    cand = []
    for x in sorted(scan.found, key=lambda x: x['origin'].lower()):
        if not is_style(x['path']):
            n, err = x['node'], None
            while n and not err:
                err, n = n.error, n.parent
            rows['bad'].append((x['origin'], 'not unpacked: %s' % err if err and os.path.getsize(x['path']) == 0 else
                                'no visual style (no TEXTFILE resource)'))
            continue
        h = L.sha256(x['path'])
        x['sha'] = h
        if h in seen:
            rows['dup'].append((x['origin'], seen[h]))
            continue
        seen[h] = x
        if h in shas:
            mine = os.path.join(W, 'local', 'themes', shas[h] + '.msstyles')
            local = os.path.isfile(mine) and L.sha256(mine) == h
            rows['have'].append((x['origin'], shas[h], local))
            x['name'] = shas[h]
            if local:
                x['again'] = True
                cand.append(x)
            continue
        why = ut_dump_ok(x['path'])
        if why:
            rows['bad'].append((x['origin'], why))
            continue
        x['stem'] = slug(x['chain'][-1].rsplit('.', 1)[0]) or 'style'
        cand.append(x)
    by = {}
    for x in cand:
        if not x.get('again'):
            by.setdefault(x['stem'], []).append(x)
    used = set(taken)
    for stem, xs in sorted(by.items()):
        clash = len(xs) > 1 or name_ok(stem) in taken
        for x in xs:
            n = stem
            if clash:
                f = folder_name(x['chain'], stem)
                if f:
                    n = f if f.startswith(stem) else '%s-%s' % (stem, f)
            n = name_ok(n)
            base, i = n, 2
            while n in used:
                n = '%s-%d' % (base, i)
                i += 1
            used.add(n)
            x['name'] = n
    rows['dup'] = [(o, y.get('name', '?')) for o, y in rows['dup']]
    rows['import'] = [x for x in cand if not x.get('again')]
    rows['again'] = [x for x in cand if x.get('again')]
    for x in cand:
        x['shell'] = next((os.path.join(os.path.dirname(x['path']), f) for f in sorted(os.listdir(os.path.dirname(x['path'])))
                           if f.lower() == 'shell' and os.path.isdir(os.path.join(os.path.dirname(x['path']), f))), None)
        x['theme'], x['wallpaper'] = theme_for(scan, x)
    return rows


def theme_for(scan, x):
    """a .theme beside the style (its folder or the one above, within the item) that names it, and the wallpaper it names
    found in the download: (theme file, wallpaper file or a 'named, not found' note)"""
    d = os.path.dirname(x['path'])
    fn = os.path.basename(x['path']).lower()
    dirs = [d] + ([os.path.dirname(d)] if inside(os.path.dirname(d), x['root']) else [])
    for dd in dirs:
        ths = scan.themes.get(dd, [])
        mine = [t for t in ths if (winparts(read_theme(t).get(('visualstyles', 'path'), '')) or [''])[-1].lower() == fn]
        if not mine and dd == d and len(ths) == 1 and sum(1 for y in os.listdir(d) if y.lower().endswith('.msstyles')) == 1:
            mine = ths
        if not mine:
            continue
        th = mine[0]
        t = read_theme(th)
        wp = t.get(('control panel\\desktop', 'wallpaper'), '')
        if not wp:
            return th, None
        parts = winparts(wp)
        want = parts[-1].lower()
        best, score = None, 0
        for r, ds, fs in os.walk(x['root']):
            for f in fs:
                if f.lower() != want or os.path.islink(os.path.join(r, f)):
                    continue
                rel = os.path.relpath(os.path.join(r, f), x['root']).split(os.sep)
                s = 0
                while s < min(len(rel), len(parts)) and rel[-1 - s].lower() == parts[-1 - s].lower():
                    s += 1
                if s > score or (s == score and best and os.path.join(r, f) < best):
                    best, score = os.path.join(r, f), s
        return th, best or ('named, not found: %s' % parts[-1])
    return None, None


def norm_shell(src, dst, colors, force, rep):
    """Shell/<colour>/shellstyle.dll under the names the resolver asks for: the colour as the style spells it, the file lower case"""
    cmap = {c.lower(): c for c in colors}
    for r, ds, fs in os.walk(src):
        for f in fs:
            s = os.path.join(r, f)
            if os.path.islink(s):
                continue
            rel = os.path.relpath(s, src).split(os.sep)
            if len(rel) >= 2:
                rel[0] = cmap.get(rel[0].lower(), rel[0])
            if rel[-1].lower() == 'shellstyle.dll':
                rel[-1] = 'shellstyle.dll'
            L.copy_file(s, os.path.join(dst, *rel), force, rep)


def import_styles(top, W, force=False, recipes=True, dry=False, tmpdir=None):
    top = os.path.realpath(top)
    if not os.path.isdir(top):
        L.die('--styles %s is no directory' % top)
    if not shutil.which('7z'):
        L.log('warning: no 7z: archives (.zip .rar .7z .exe) are not opened')
    tmp = tempfile.mkdtemp(prefix='.lhde-styles-', dir=tmpdir or (W if os.path.isdir(W) else None))
    try:
        s = Scan(top, tmp)
        s.run()
        rows = plan(s, W)
        return report(s, rows, W, top, force, recipes, dry)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def report(s, rows, W, top, force, recipes, dry):
    act = 'would import' if dry else 'imported'
    themes_read = sum(1 for x in rows['import'] + rows['again'] if x['theme'])
    walls = []
    rep = {'copied': 0, 'kept': 0}
    for x in rows['import'] + rows['again']:
        extra = []
        if x['shell']:
            extra.append('Shell/')
        wp = x['wallpaper']
        if wp and not wp.startswith('named, not found'):
            extra.append('wallpaper ' + os.path.basename(wp))
            walls.append(x)
        elif wp:
            extra.append('.theme wallpaper ' + wp)
        if x in rows['import']:
            out('  %s %-28s <- %s%s' % (act, x['name'], x['origin'], ' (+ %s)' % ', '.join(extra) if extra else ''))
        if dry or (x.get('again') and not force):
            continue
        L.copy_file(x['path'], os.path.join(W, 'local', 'themes', x['name'] + '.msstyles'), force, rep)
        if x['shell']:
            norm_shell(x['shell'], os.path.join(W, 'local', 'themes', x['name'], 'Shell'), style_colors(x['path']), force, rep)
        if wp and not wp.startswith('named, not found'):
            L.copy_file(wp, os.path.join(W, 'local', 'wallpapers', x['name'], os.path.basename(wp)), force, rep)
        T.xput(os.path.join(W, 'local', 'themes', x['name'] + '.msstyles'),
               {T.A_DESC: '%s visual style (lhde-import --styles)' % x['name'], T.A_SOURCE: 'downloaded: %s' % x['origin'], T.A_TAGS: 'style'})
    for o, n, local in rows['have']:
        out('  %s %-28s <- %s' % ('already there' if local else 'duplicate   ', n, o))
    for o, n in rows['dup']:
        out('  duplicate    %-28s <- %s' % (n, o))
    for o, why in rows['bad']:
        out('  not taken    %-28s <- %s: %s' % ('', o, why))
    for o, why in s.failed:
        out('  not opened   %-28s <- %s: %s' % ('', o, why))
    us = units(s.root)
    dup_nodes = set(id(x['node']) for x in s.found)
    def any_style(n):
        return id(n) in dup_nodes or any(any_style(c) for c in n.children)
    nostyle_dirs = [u.rel for u in us if u.kind == 'dir' and not any_style(u)]
    nostyle_arch = [u.rel + (' (%s)' % u.error if u.error else '') for u in us if u.kind == 'archive' and not any_style(u)]
    loose = sum(1 for x in s.found if x['node'] is s.root)
    for d in nostyle_dirs:
        out('  no style in  %s' % d)
    for d in nostyle_arch:
        out('  no style in  %s' % d)
    if not dry:
        if rows['import'] or force:
            T.reindex(W)
            if recipes:
                for x in rows['import'] + rows['again']:
                    for kind, target, spec in L.style_recipes(W, x['name']):
                        L.run_recipe(os.path.join(W, 'index'), kind, target, spec, 'style', force)
            T.annotate_tree(W)
        with open(os.path.join(W, 'import.log'), 'a') as f:
            import datetime
            f.write('# lhde-import --styles %s, %s: %d styles (%s)\n' % (top, datetime.datetime.now().strftime('%Y-%m-%d %H:%M'),
                    len(rows['import']), ' '.join(x['name'] for x in rows['import'])))
    nwall = len(walls)
    out('lhde: --styles %s%s:' % (top, ' (dry run: nothing written)' if dry else ''))
    out('  %3d styles %s into local/themes (%d with Shell/, %d wallpapers into local/wallpapers/NAME)%s'
          % (len(rows['import']), act, sum(1 for x in rows['import'] if x['shell']),
             sum(1 for x in walls if x in rows['import']), '' if dry else ': %d files copied, %d kept' % (rep['copied'], rep['kept'])))
    nl = sum(1 for o, n, l in rows['have'] if l)
    out('  %3d already in the Windows tree (the same sha256): %d imported before, %d a source\'s style%s'
          % (len(rows['have']), nl, len(rows['have']) - nl, ' (%s)' % ', '.join(sorted(set(n for o, n, l in rows['have'] if not l))) if nl < len(rows['have']) else ''))
    out('  %3d duplicates skipped (a second copy of a style above)' % len(rows['dup']))
    out('  %3d files not taken (no visual style, or ut-dump cannot open them)' % len(rows['bad']))
    out('  %3d archives that would not open%s' % (len(s.failed), ': ' + '; '.join(o for o, w in s.failed) if s.failed else ''))
    out('  %3d directories and %d archives without a style%s' % (len(nostyle_dirs), len(nostyle_arch),
          ': ' + '; '.join(nostyle_dirs + nostyle_arch) if nostyle_dirs or nostyle_arch else ''))
    out('  %3d .theme files read for their wallpaper; their colour scheme, icons, cursors, sounds and screen saver are ignored'
          ' (the Windows tree has no place for them: choose the colour on the Appearance tab)' % themes_read)
    out('  %d archives opened (%d styles found in all, %d loose in the directory)' % (s.n, len(s.found), loose))
    return 0
