"""lhde's three trees on disk (docs/LAYOUT.md "The three trees"): where lhde-import puts the Windows files, the index the
programs look things up through (liblhde/data.c reads $LHDE_WINDOWS/index), and the migration from the old layout, where
everything lived in ~/.local/share/lhde.

The Windows tree, $LHDE_WINDOWS (default ${XDG_DATA_HOME:-~/.local/share}/lhde-windows):
  <source>/      the raw files of one build exactly as the resource pack has them: system32/ themes/ fonts/ wallpapers/ setup/,
                 source.ini (the rules they were taken with and what lhde-import derives from them)
  local/         files that come from no source: themes/NAME.msstyles (+ NAME/Shell) dropped in with lhde-import --style,
                 schemes/ (lhde-schemes), resources/NAME/, wallpapers/NAME/, fonts/NAME/ copied by hand or from aap's workspace
  generated/     everything derived: frames/<set>/ (PNGs, manifest.ini, theme.ini of a set we do not ship),
                 packs/<build>/ (PNGs, startpanel.ini, manifest-*.ini), schemes/*.scheme (from a source's HIVEDEF.INF).
                 Deleting it is always safe: lhde-import --regenerate makes it again from the sources' recipes.
  index/         relative symlinks under the names programs ask for (made by reindex, never edited):
                 styles/NAME.msstyles, styles/NAME (the style's directory with Shell/), styles/NAME.scheme,
                 resources/<source>, fonts/<source>, wallpapers/<source>, frames -> ../generated/frames, packs -> ../generated/packs
  import.log
Run with python3 -I."""
import configparser
import datetime
import glob
import os
import shutil
import sys

HERE = os.path.dirname(os.path.realpath(__file__))
SPECIAL = ('index', 'generated', 'local')
# a <source>'s directory -> the kind (index/<kind>/<source>) it serves
BUILD_KINDS = (('system32', 'resources'), ('fonts', 'fonts'), ('wallpapers', 'wallpapers'))


def log(*a):
    print('lhde:', *a, flush=True)


def xdg_data_home():
    return os.environ.get('XDG_DATA_HOME') or os.path.expanduser('~/.local/share')


def config_dir():
    return os.path.join(os.environ.get('XDG_CONFIG_HOME') or os.path.expanduser('~/.config'), 'lhde')


def windows_dir(arg=None):
    """the Windows tree: --windows DIR, else $LHDE_WINDOWS, else ${XDG_DATA_HOME:-~/.local/share}/lhde-windows"""
    return os.path.abspath(arg or os.environ.get('LHDE_WINDOWS') or os.path.join(xdg_data_home(), 'lhde-windows'))


def old_dir():
    """the old layout's data directory (everything in one): ${XDG_DATA_HOME:-~/.local/share}/lhde"""
    return os.path.join(xdg_data_home(), 'lhde')


def shipped_dirs():
    """the shipped data, text only: the source tree's data/ (tools/ runs from the tree), the install's <prefix>/share/lhde
    (tools/ installed as <prefix>/lib/lhde/tools)"""
    out = []
    for d in (os.path.join(HERE, '..', 'data'), os.path.join(HERE, '..', '..', '..', 'share', 'lhde')):
        if os.path.isdir(os.path.join(d, 'themes')):
            out.append(os.path.realpath(d))
    return out


def read_ini(path):
    c = configparser.ConfigParser(delimiters=('=',), comment_prefixes=(';', '#'), inline_comment_prefixes=(';',),
                                  interpolation=None, strict=False)
    c.optionxform = str
    c.read(path, encoding='utf-8')
    return c


def ci_path(root, rel):
    """root/rel with every component matched case-insensitively (the images spell Themes/aero or themes/Aero)"""
    p = root
    for c in rel.split('/'):
        q = os.path.join(p, c)
        if not os.path.exists(q) and os.path.isdir(p):
            q = next((os.path.join(p, x) for x in os.listdir(p) if x.lower() == c.lower()), q)
        p = q
    return p


def sources_in(W):
    """the <source> directories of the Windows tree (everything but index/ generated/ local/ and dot files)"""
    if not os.path.isdir(W):
        return []
    return sorted(d for d in os.listdir(W) if d not in SPECIAL and not d.startswith('.') and os.path.isdir(os.path.join(W, d)))


def shipped_source_ini(sid):
    for d in shipped_dirs():
        p = os.path.join(d, 'sources', sid + '.ini')
        if os.path.isfile(p):
            return p
    return None


def source_ini(W, sid):
    """a source's rules: the copy imported with it (W/<sid>/source.ini), else the shipped sources/<sid>.ini"""
    p = os.path.join(W, sid, 'source.ini')
    return p if os.path.isfile(p) else shipped_source_ini(sid)


def section(ini, name):
    return list(ini.items(name)) if ini and ini.has_section(name) else []


def style_dir_has_shell(d):
    return os.path.isdir(d) and any(x.lower() == 'shell' for x in os.listdir(d))


def _link(target, link):
    os.makedirs(os.path.dirname(link), exist_ok=True)
    if os.path.lexists(link):
        return False
    os.symlink(os.path.relpath(target, os.path.dirname(link)), link)
    return True


def reindex(W, quiet=False):
    """(re)make W/index from what the tree holds: relative symlinks only, so W can be moved; index/ is rebuilt whole"""
    for g in ('frames', 'packs', 'schemes'):
        os.makedirs(os.path.join(W, 'generated', g), exist_ok=True)
    new = os.path.join(W, '.index.new')
    shutil.rmtree(new, ignore_errors=True)
    os.makedirs(os.path.join(new, 'styles'))
    n = 0
    for g in ('frames', 'packs'):
        os.symlink(os.path.join('..', 'generated', g), os.path.join(new, g))
    for sid in sources_in(W):
        root = os.path.join(W, sid)
        for sub, kind in BUILD_KINDS:
            if os.path.isdir(os.path.join(root, sub)):
                n += _link(os.path.join(root, sub), os.path.join(new, kind, sid))
        ini = read_ini(source_ini(W, sid)) if source_ini(W, sid) else None
        for name, rel in section(ini, 'styles'):
            p = ci_path(root, rel)
            if not os.path.isfile(p):
                continue
            n += _link(p, os.path.join(new, 'styles', name + '.msstyles'))
            if style_dir_has_shell(os.path.dirname(p)):
                n += _link(os.path.dirname(p), os.path.join(new, 'styles', name))
    L = os.path.join(W, 'local')
    lt = os.path.join(L, 'themes')
    if os.path.isdir(lt):
        for f in sorted(os.listdir(lt)):
            p = os.path.join(lt, f)
            if f.lower().endswith('.msstyles') and os.path.isfile(p):
                n += _link(p, os.path.join(new, 'styles', f[:-9] + '.msstyles'))
            elif os.path.isdir(p):
                n += _link(p, os.path.join(new, 'styles', f))
    for sub, kind in (('resources', 'resources'), ('fonts', 'fonts'), ('wallpapers', 'wallpapers'), ('icons', 'icons')):
        d = os.path.join(L, sub)
        if os.path.isdir(d):
            for x in sorted(os.listdir(d)):
                if os.path.isdir(os.path.join(d, x)):
                    n += _link(os.path.join(d, x), os.path.join(new, kind, x))
    for p in sorted(glob.glob(os.path.join(W, 'local', 'schemes', '*.scheme')) + glob.glob(os.path.join(W, 'generated', 'schemes', '*.scheme'))):
        n += _link(p, os.path.join(new, 'styles', os.path.basename(p)))
    old = os.path.join(W, 'index')
    if os.path.lexists(old):
        shutil.rmtree(old) if os.path.isdir(old) and not os.path.islink(old) else os.unlink(old)
    os.rename(new, old)
    if not quiet:
        log('index %s: %d links' % (old, n))
    return n


# ---------------------------------------------------------------- the migration from the old layout
def human(b):
    for u, f in (('GB', 1 << 30), ('MB', 1 << 20), ('KB', 1 << 10)):
        if b >= f:
            return '%.1f %s' % (b / f, u) if u == 'GB' else '%d %s' % (b // f, u)
    return '%d B' % b


def du(p):
    if os.path.islink(p):
        return 0, 1
    if os.path.isfile(p):
        return os.path.getsize(p), 1
    b = n = 0
    for r, ds, fs in os.walk(p):
        for f in fs:
            q = os.path.join(r, f)
            if not os.path.islink(q):
                b += os.path.getsize(q)
            n += 1
    return b, n


class Plan:
    """the migration's steps: listed (-n) or done; idempotent because every step moves or deletes its source"""
    def __init__(self, old, W, cfg):
        self.old, self.W, self.cfg = old, W, cfg
        self.steps = []    # (verb move|delete, src, dst, note)
        self.kept = []     # (path, why)
        self.rmdirs = []   # emptied directories of the old layout
        self.logs = []     # (src, dst): import.log appended

    def short(self, p):
        for top, name in ((self.W, '$W'), (self.old, '$OLD'), (self.cfg, '$CONFIG')):
            if p == top or p.startswith(top + '/'):
                return name + p[len(top):]
        return p

    def move(self, src, dst, note=''):
        self.steps.append(('move', src, dst, note))

    def delete(self, src, note=''):
        self.steps.append(('delete', src, None, note))

    def keep(self, src, why):
        self.kept.append((src, why))

    def pending(self):
        return [s for s in self.steps if os.path.lexists(s[1])] + [l for l in self.logs if os.path.isfile(l[0])]

    def run(self):
        moved = deleted = 0
        for verb, src, dst, note in self.steps:
            if not os.path.lexists(src):
                continue
            if verb == 'move':
                _move(src, dst)
                moved += 1
            else:
                _remove(src)
                deleted += 1
        for src, dst in self.logs:
            if os.path.isfile(src):
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                with open(dst, 'a') as o, open(src) as i:
                    o.write(i.read())
                os.unlink(src)
        for d in self.rmdirs:
            _rmdir_empty(d)
        return moved, deleted

    def report(self):
        """one line per step; a run of more than 4 steps with the same note as one line"""
        groups = []
        for verb, src, dst, note in self.steps:
            if not os.path.lexists(src):
                continue
            if groups and groups[-1][0] == (verb, note):
                groups[-1][1].append((src, dst))
            else:
                groups.append(((verb, note), [(src, dst)]))
        lines = []
        for (verb, note), items in groups:
            if len(items) <= 4:
                for src, dst in items:
                    b, n = du(src)
                    size = ' (symlink to %s)' % os.readlink(src) if os.path.islink(src) else ' (%s%s)' % ('%d files, ' % n if n > 1 else '', human(b))
                    if verb == 'move':
                        lines.append('  move   %s -> %s%s  %s' % (self.short(src), self.short(dst), size, note))
                    else:
                        lines.append('  delete %s%s  %s' % (self.short(src), size, note))
            else:
                b = sum(du(s)[0] for s, d in items)
                s0, d0 = items[0]
                if verb == 'move':
                    lines.append('  move   %d files (%s), %s -> %s, ...  %s' % (len(items), human(b), self.short(s0), self.short(d0), note))
                else:
                    lines.append('  delete %d files (%s), %s, ...  %s' % (len(items), human(b), self.short(s0), note))
        for src, dst in self.logs:
            if os.path.isfile(src):
                lines.append('  append %s -> %s  the import history' % (self.short(src), self.short(dst)))
        return lines


def _move(src, dst):
    """move src to dst; when dst exists (an earlier run, or frames/X/a.png and frames/X/cache/a.png both), merge directories
    and drop the duplicate file"""
    if os.path.lexists(dst):
        if os.path.isdir(src) and not os.path.islink(src) and os.path.isdir(dst) and not os.path.islink(dst):
            for x in os.listdir(src):
                _move(os.path.join(src, x), os.path.join(dst, x))
            _rmdir_empty(src)
        else:
            _remove(src)
        return
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.move(src, dst)


def _remove(p):
    if os.path.isdir(p) and not os.path.islink(p):
        shutil.rmtree(p)
    elif os.path.lexists(p):
        os.unlink(p)


def _rmdir_empty(top):
    """remove top and its subdirectories when they hold no files"""
    if not os.path.isdir(top) or os.path.islink(top):
        return
    for r, ds, fs in os.walk(top, topdown=False):
        try:
            os.rmdir(r)
        except OSError:
            pass


INSTALL_LIST = '.lhde-install'   # make install's list of the files it put into $PREFIX/share/lhde


def install_list(d):
    try:
        with open(os.path.join(d, INSTALL_LIST)) as f:
            return {l.strip() for l in f if l.strip() and not l.startswith('#')}
    except OSError:
        return None


def _flat(rel):
    """frames/X/cache/a.png -> a.png below X: the generated tree has no cache/ level"""
    return '/'.join(c for c in rel.split('/') if c != 'cache')


def plan_migration(old, W, cfg):
    """every step that turns the old ~/.local/share/lhde into the three trees, without reading any ISO"""
    P = Plan(old, W, cfg)
    if not os.path.isdir(old):
        return P
    shipped = shipped_dirs()
    # make install PREFIX=~/.local: the old directory is the install's share/lhde too. Its shipped text stays (make install
    # keeps it current); what is shipped is known from make install's list, else from the other shipped directories
    same = install_list(old) is not None or any(os.path.realpath(d) == os.path.realpath(old) for d in shipped) or \
        os.path.exists(os.path.join(old, '..', '..', 'bin', 'lhde-theme'))
    others = [d for d in shipped if os.path.realpath(d) != os.path.realpath(old)]
    listed = set()
    for d in shipped + [old]:
        listed |= install_list(d) or set()

    def _shipped_has(rel, _=None):
        return rel in listed or any(os.path.lexists(os.path.join(d, rel)) for d in others)
    known = set()
    for d in shipped:
        known |= {os.path.basename(p)[:-4] for p in glob.glob(os.path.join(d, 'sources', '*.ini'))}

    def stale_or_user(path, rel, kind):
        """a text file of a kind we ship: ours (stale when it is not the install's own copy), else the user's own"""
        if same:
            return   # shipped: this is also the install's share/lhde (make install PREFIX=~/.local), where it belongs
        if _shipped_has(rel, shipped):
            P.delete(path, 'stale copy: make install puts %s in its share/lhde' % rel)
        else:
            P.move(path, os.path.join(cfg, rel), 'not shipped: your own %s' % kind)

    # the build directories: resources/<b>, fonts/<b>, wallpapers/<b>
    for kind, sub in (('resources', 'system32'), ('fonts', 'fonts'), ('wallpapers', 'wallpapers')):
        d = os.path.join(old, kind)
        if not os.path.isdir(d):
            continue
        for b in sorted(os.listdir(d)):
            src = os.path.join(d, b)
            if b in known:
                P.move(src, os.path.join(W, b, sub), 'the %s of source %s' % (kind, b))
            else:
                P.move(src, os.path.join(W, 'local', kind, b), 'no source %s: local' % b)
        P.rmdirs.append(d)
    # the styles: each into its source's place ([styles] of sources/<sid>.ini), else local/themes; schemes are generated
    d = os.path.join(old, 'styles')
    if os.path.isdir(d):
        claim = {}
        present = set(sources_in(W)) | {b for b in known if any(os.path.isdir(os.path.join(old, k, b)) for k in ('resources', 'fonts', 'wallpapers'))}
        for sid in sorted(present):
            p = source_ini(W, sid) or shipped_source_ini(sid)
            for name, rel in section(read_ini(p) if p else None, 'styles'):
                claim.setdefault(name, (sid, rel))
        # classic schemes: generated/ only holds what --regenerate can make again; the old layout kept no HIVEDEF.INF, so
        # they go to local/schemes (lhde-schemes' place) unless a source of the tree has its INF
        regen = any(os.path.isfile(ci_path(os.path.join(W, sid), rel)) for sid in sources_in(W)
                    for _, rel in section(read_ini(source_ini(W, sid)) if source_ini(W, sid) else None, 'schemes'))
        for f in sorted(x for x in os.listdir(d) if x.endswith('.scheme')):
            P.move(os.path.join(d, f), os.path.join(W, 'generated' if regen else 'local', 'schemes', f),
                   'classic schemes' + (': generated' if regen else ' (no HIVEDEF.INF imported to remake them): local'))
        for f in sorted(x for x in os.listdir(d) if not x.endswith('.scheme')):
            src = os.path.join(d, f)
            name = f[:-9] if f.lower().endswith('.msstyles') else f
            if name in claim:
                sid, rel = claim[name]
                if os.path.isdir(src):   # styles/NAME/ (Shell/, or the style itself): beside the style file
                    for x in sorted(os.listdir(src)):
                        P.move(os.path.join(src, x), os.path.join(W, sid, os.path.dirname(rel), x), 'style %s of source %s' % (name, sid))
                    P.rmdirs.append(src)
                else:
                    P.move(src, os.path.join(W, sid, rel), 'style %s of source %s' % (name, sid))
            else:
                P.move(src, os.path.join(W, 'local', 'themes', f), 'no source has style %s: local' % name)
        P.rmdirs.append(d)
    # frame sets: the PNGs and manifests are generated, the theme.ini / nav.ini are ours (shipped) or generated (a set we do not ship)
    d = os.path.join(old, 'frames')
    if os.path.isdir(d):
        for s in sorted(os.listdir(d)):
            seen, dups = set(), []   # a PNG both in frames/X/ and frames/X/cache/: the one beside theme.ini is the one luxwm drew
            for r, ds, fs in os.walk(os.path.join(d, s)):
                ds.sort(key=lambda x: x == 'cache')
                for f in sorted(fs):
                    src = os.path.join(r, f)
                    rel = os.path.relpath(src, d)
                    srel = 'frames/' + _flat(rel)
                    if f.endswith('.ini') and f != 'manifest.ini' and _shipped_has(srel, shipped):
                        if not same:
                            P.delete(src, 'stale copy: make install ships %s' % srel)
                    elif _flat(rel) in seen:
                        dups.append((src, os.path.join(d, _flat(rel))))
                    else:
                        seen.add(_flat(rel))
                        P.move(src, os.path.join(W, 'generated', 'frames', _flat(rel)), 'frame set %s: generated' % s)
            if dups:
                import filecmp
                nd = sum(1 for a, b in dups if os.path.isfile(b) and not filecmp.cmp(a, b, shallow=False))
                for a, b in dups:
                    P.delete(a, 'frame set %s: second copies in cache/ (%s; the PNG beside theme.ini was in use and is kept)'
                             % (s, '%d of %d differ' % (nd, len(dups)) if nd else 'identical'))
            P.rmdirs.append(os.path.join(d, s))
    # packs: shell.lua and startpanels.lua are ours; images, startpanel.ini and manifests generated
    d = os.path.join(old, 'packs')
    if os.path.isdir(d):
        seen, dups = set(), []
        for r, ds, fs in os.walk(d):
            ds.sort(key=lambda x: x == 'cache')   # beside first: what luxshell found first
            for f in sorted(fs):
                src = os.path.join(r, f)
                rel = os.path.relpath(src, d)
                if f.endswith('.lua'):
                    stale_or_user(src, 'packs/' + rel, 'pack')
                elif _flat(rel) in seen:
                    dups.append((src, os.path.join(d, _flat(rel))))
                else:
                    seen.add(_flat(rel))
                    P.move(src, os.path.join(W, 'generated', 'packs', _flat(rel)), 'pack images: generated')
        if dups:
            import filecmp
            nd = sum(1 for a, b in dups if os.path.isfile(b) and not filecmp.cmp(a, b, shallow=False))
            for a, b in dups:
                P.delete(a, 'pack images: second copies in cache/ (%s; the one outside cache/ was in use and is kept)'
                         % ('%d of %d differ' % (nd, len(dups)) if nd else 'identical'))
        P.rmdirs.append(d)
    # the shipped text kinds
    for kind in ('themes', 'navbar', 'icons', 'sources'):
        d = os.path.join(old, kind)
        if os.path.isdir(d):
            for f in sorted(os.listdir(d)):
                stale_or_user(os.path.join(d, f), kind + '/' + f, kind[:-1] if kind.endswith('s') else kind)
    # program files that live in $PREFIX/lib/lhde now
    for kind in ('luxshell', 'session', 'tools'):
        p = os.path.join(old, kind)
        if os.path.lexists(p):
            P.delete(p, 'program files: make install puts %s/ in $PREFIX/lib/lhde' % kind)
    # flxp's res/icons: shipped by flxp's install into share/lhde
    p = os.path.join(old, 'res')
    if os.path.isdir(p):
        if same:
            pass
        elif any(x.startswith('res/') for x in listed) or any(os.path.isdir(os.path.join(d, 'res')) for d in others):
            P.delete(p, 'stale copy of flxp\'s res/ (make install ships it)')
        else:
            P.keep(p, 'flxp\'s res/: no install has it; left in place')
    p = os.path.join(old, 'import.log')
    if os.path.isfile(p):
        P.logs.append((p, os.path.join(W, 'import.log')))
    # anything else stays
    handled = {'resources', 'fonts', 'wallpapers', 'styles', 'frames', 'packs', 'themes', 'navbar', 'icons', 'sources',
               'luxshell', 'session', 'tools', 'res', 'import.log'}
    for x in sorted(os.listdir(old)):
        if x not in handled and x != INSTALL_LIST:
            P.keep(os.path.join(old, x), 'unknown: left in place')
    return P


def migrate(old, W, cfg, dry, auto=False):
    """lhde-import --migrate: 0 nothing to do, 1 done (or listed, with -n)"""
    P = plan_migration(old, W, cfg)
    if not P.pending():
        if not auto:
            log('nothing to migrate: %s holds no old-layout files%s' % (old, '' if os.path.isdir(old) else ' (it does not exist)'))
        return 0
    shipped = shipped_dirs()
    print('lhde-import --migrate%s: the old layout -> the three trees (docs/LAYOUT.md)' % (' -n' if dry else ''))
    print('  $OLD     = %s  (the old layout)' % old)
    print('  $W       = %s  (Windows files, $LHDE_WINDOWS)' % W)
    print('  $CONFIG  = %s  (your files)' % cfg)
    print('  shipped  = %s' % (', '.join(shipped) or '(no install found: make install first)'))
    for l in P.report():
        print(l)
    for src, why in P.kept:
        print('  keep   %s  (%s)' % (P.short(src), why))
    srcs = sorted({s[2][len(W) + 1:].split('/')[0] for s in P.steps if s[0] == 'move' and os.path.lexists(s[1]) and s[2].startswith(W + '/')} - set(SPECIAL))
    print('  then   $W/<source>/source.ini for %s (copies of the shipped sources/<id>.ini), $W/index (symlinks)' % (' '.join(srcs) or '-'))
    if dry:
        print('(dry run: nothing changed; run without -n to do it)')
        return 1
    moved, deleted = P.run()
    for sid in srcs:
        dst = os.path.join(W, sid, 'source.ini')
        p = shipped_source_ini(sid)
        if p and not os.path.exists(dst):
            with open(p) as i, open(dst, 'w') as o:
                o.write('; copied by lhde-import --migrate from %s (this build was imported before the three trees)\n' % p)
                o.write(i.read())
    for x in os.listdir(old):
        _rmdir_empty(os.path.join(old, x))   # the directories the moves emptied
    try:
        os.rmdir(old)   # empty unless it is also the install's share/lhde
    except OSError:
        pass
    reindex(W)
    with open(os.path.join(W, 'import.log'), 'a') as f:
        f.write('# lhde-import --migrate %s, %s: %d moved, %d deleted\n' % (old, datetime.datetime.now().strftime('%Y-%m-%d %H:%M'), moved, deleted))
    log('migrated %s: %d moved, %d deleted; the Windows files are in %s' % (old, moved, deleted, W))
    return 1


# ---------------------------------------------------------------- file annotations (docs/ANNOTATIONS.md; ~/src/luxfs x / lsx)
# Extended attributes, best effort: a filesystem without user xattrs, or a failing call, warns once and the import goes on.
# Existing attributes are never replaced (a hand-written desc stays), so annotating is idempotent.
import errno
import re

ANNOTATE = {'on': True, 'warned': False}
A_AUTHOR, A_BUILD, A_SOURCE, A_DESC, A_TAGS = 'user.lux.author', 'user.lux.build', 'user.lux.source', 'user.lux.desc', 'user.xdg.tags'


def set_annotate(on):
    ANNOTATE['on'] = bool(on)


def _warn(path, e):
    if not ANNOTATE['warned']:
        ANNOTATE['warned'] = True
        log('warning: cannot write file annotations (extended attributes) on %s: %s; going on without them (--no-annotate silences this)'
            % (path, os.strerror(e.errno) if e.errno else e))


def xput(path, attrs, replace=False):
    """set the given {attribute: text} on path (not on symlinks); an attribute that is there is kept unless replace"""
    if not ANNOTATE['on'] or os.path.islink(path) or not os.path.exists(path):
        return
    for k, v in attrs.items():
        if not v:
            continue
        try:
            if not replace:
                try:
                    os.getxattr(path, k)
                    continue
                except OSError as e:
                    if e.errno != errno.ENODATA:
                        raise
            os.setxattr(path, k, v.encode('utf-8'))
        except (OSError, AttributeError) as e:
            _warn(path, e)
            if getattr(e, 'errno', None) in (errno.ENOTSUP, errno.EOPNOTSUPP):
                ANNOTATE['on'] = False   # no user xattrs here: stop trying
            return


def copy_xattrs(src, dst):
    """the user.* attributes of src onto dst, for those dst lacks (a pack's annotations into the Windows tree)"""
    if not ANNOTATE['on'] or os.path.islink(src) or not os.path.exists(dst):
        return
    try:
        names = [n for n in os.listxattr(src) if n.startswith('user.')]
        vals = {n: os.getxattr(src, n).decode('utf-8', 'replace') for n in names}
    except OSError:
        return
    xput(dst, vals)


def source_label(title, sid, build):
    """'Longhorn build 4074 (WinHEC 2004; ...)' -> 'Longhorn 4074'; the XP source's '(from Windows Server 2003 R2 x64 CD1: ...)'
    -> 'Windows Server 2003 R2 x64'; a second dump of a build keeps its id: 'Longhorn 4042 (4042main)'"""
    m = re.search(r'\(from (.*?)(?: CD\d)?\s*[:)]', title)
    if m:
        return m.group(1).strip()
    t = re.sub(r'\s*\(.*', '', title).replace(' build ', ' ').strip() or sid
    return t if sid == build or sid in t.split() else '%s (%s)' % (t, sid)


def short_name(title, sid, build):
    """what a description calls the build: 'Longhorn 4074', 'Windows XP'"""
    return 'Windows XP' if title.startswith('Windows XP') else source_label(title, sid, build)


SYS32_NOTES = {'imageres.dll': 'icons, Avalon bar art', 'shell32.dll': 'shell icons and bitmaps', 'explorer.exe': 'the Explorer shell',
               'browseui.dll': 'shell browser UI', 'comctl32.dll': 'common controls', 'themeui.dll': 'theme and Appearance UI',
               'shellstyle.dll': 'shell styles (task pane, start panel)', 'udwm.dll': 'the DWM compositor'}
FONT_FAMILIES = (('segoeui', 'Segoe UI', {'': '', 'b': ' Bold', 'i': ' Italic', 'z': ' Bold Italic'}),
                 ('tahoma', 'Tahoma', {'': '', 'bd': ' Bold'}),
                 ('trebuc', 'Trebuchet MS', {'': '', 'bd': ' Bold', 'bi': ' Bold Italic', 'it': ' Italic'}))


def font_desc(stem):
    s = stem.lower()
    if s in ('seguibk', 'seguibd'):
        return 'font Segoe UI (%s)' % s
    for pre, fam, styles in FONT_FAMILIES:
        if s.startswith(pre) and s[len(pre):] in styles:
            return 'font ' + fam + styles[s[len(pre):]]
    return None


def kind_of(rel, name, short):
    """(description, tags) of a file below a source's directory (rel with / separators)"""
    parts = rel.split('/')
    top, base = parts[0].lower(), parts[-1]
    stem = base.rsplit('.', 1)[0]
    if top == 'system32':
        note = SYS32_NOTES.get(base.lower())
        return '%s %s%s' % (short, base, ' (%s)' % note if note else ''), 'system32'
    if top == 'fonts':
        return font_desc(stem) or base, 'font'
    if top == 'wallpapers':
        return 'wallpaper ' + (stem.replace('_', ' ').replace('-', ' ').title() if stem == stem.lower() else stem), 'wallpaper'
    if top == 'themes':
        low = base.lower()
        if low.endswith('.msstyles'):
            d = parts[-2] if len(parts) > 2 else stem
            return '%s visual style' % d, 'style'
        if low.endswith('.theme'):
            return '%s theme file' % stem, 'style'
        if low == 'shellstyle.dll' and len(parts) > 2:
            return '%s shell style' % parts[-2], 'style'
        return base, 'style'
    if top == 'setup':
        return ('default user hive setup data (classic Appearance schemes)' if base.lower() == 'hivedef.inf' else base), 'setup'
    return base, ''


def dir_kind(rel, short):
    parts = rel.split('/') if rel else []
    if not parts:
        return '%s files' % short, ''
    top = parts[0].lower()
    if len(parts) == 1:
        return {'system32': '%s system32 files' % short, 'fonts': '%s fonts' % short, 'wallpapers': '%s wallpapers' % short,
                'themes': '%s themes and visual styles' % short, 'setup': '%s setup data' % short}.get(top, parts[0]), \
            {'fonts': 'font', 'wallpapers': 'wallpaper', 'themes': 'style'}.get(top, top)
    if top == 'themes':
        return ('%s visual style files' % parts[1] if len(parts) == 2 else '%s %s' % (parts[1], parts[-1])), 'style'
    return parts[-1], top


def annotate_original(path, sid, title, build, rel, inimage):
    """a raw Windows file: made by Microsoft, of this build, from this medium's path"""
    short = short_name(title, sid, build)
    desc, tags = kind_of(rel, os.path.basename(rel), short)
    xput(path, {A_AUTHOR: 'microsoft', A_BUILD: build,
                A_SOURCE: '%s, %s' % (source_label(title, sid, build), inimage) if inimage else source_label(title, sid, build),
                A_DESC: desc, A_TAGS: tags})


def annotate_source_dirs(root, sid, title, build):
    """root = <pack or tree>/<sid>: the directory itself and its subdirectories"""
    short, label = short_name(title, sid, build), source_label(title, sid, build)
    for r, ds, fs in os.walk(root):
        rel = os.path.relpath(r, root).replace(os.sep, '/')
        desc, tags = dir_kind('' if rel == '.' else rel, short)
        xput(r, {A_AUTHOR: 'microsoft', A_BUILD: build, A_SOURCE: label, A_DESC: desc, A_TAGS: tags})


def _inimage_rules(ini):
    return [(k, [p.strip() for p in v.split('|') if p.strip()]) for k, v in section(ini, 'files')]


def guess_inimage(rules, rel):
    """the in-image path of a pack file, from the source's [files] rules (what the MANIFEST would say, without the real case
    of a setup file's name)"""
    for dest, pats in rules:
        if dest.endswith('/'):
            if rel.lower().startswith(dest.lower()) and pats:
                m = re.search(r'[*?{]', pats[0])
                head = pats[0] if not m else pats[0][:m.start()]
                return (head[:head.rfind('/') + 1] if '/' in head else '') + rel[len(dest):]
        elif dest.lower() == rel.lower() and pats:
            return pats[0]
    return None


def _real_rel(W, p):
    """p (an index/ name) as the Windows tree's real relative path"""
    return os.path.relpath(os.path.realpath(p), os.path.realpath(W)).replace(os.sep, '/')


RECIPE_DESC = {'composited': 'glass frame plates read live from the style', 'classic': 'classic window frames drawn from the style',
               'vision': 'Vision caption buttons', 'udwm': 'frames from uDWM.dll', 'dwmwindow': 'frames from the DWMWindow streams'}


def _annotate_generated_dir(W, d, kind, mpath, build_of):
    """every file below d, from the manifest.ini at mpath"""
    ini = read_ini(mpath)
    recipe = ini.get('recipe', 'kind', fallback='?')
    args = ini.get('recipe', 'args', fallback='').split()
    ins = [_real_rel(W, os.path.join(W, 'index', k)) for k, _ in section(ini, 'inputs')]
    ins = [i for i in ins if not i.startswith('..')]
    setname = os.path.basename(d)
    tags = 'generated,frames' if kind == 'frames' else 'generated,packs'
    if kind == 'frames':
        desc = 'frame set %s: %s' % (setname, RECIPE_DESC.get(recipe, recipe))
    elif recipe == 'shell32':
        desc = 'shell32.dll icons for luxshell (%s)' % (args[0] if args else setname)
    elif recipe == 'startpanel':
        desc = 'start panel parts of style %s (%s)' % (args[1] if len(args) > 1 else '?', args[0] if args else setname)
        tags += ',startpanel'
    else:
        desc = 'imageres.dll PNG resources for luxshell (%s)' % (' over '.join(args[:2]) if args else setname)
    attrs = {A_AUTHOR: 'lhde-import', A_DESC: desc, A_TAGS: tags,
             A_SOURCE: 'generated by recipe %s from %s' % (recipe, ', '.join(ins) or '(the Windows tree)')}
    b = next((build_of(i.split('/')[0]) for i in ins if i.split('/')[0] in build_of.known), None)
    if b:
        attrs[A_BUILD] = b
    xput(d, attrs)
    for r, ds, fs in os.walk(d):
        xput(r, attrs)
        for f in fs:
            xput(os.path.join(r, f), attrs)


def annotate_tree(W):
    """annotate whatever the Windows tree holds that lacks annotations (lhde-import --annotate, and the end of an import)"""
    if not ANNOTATE['on'] or not os.path.isdir(W):
        return
    info = {}
    for sid in sources_in(W):
        root = os.path.join(W, sid)
        p = source_ini(W, sid)
        ini = read_ini(p) if p else None
        title = ini.get('source', 'title', fallback=sid) if ini and ini.has_section('source') else sid
        build = ini.get('source', 'build', fallback=sid) if ini and ini.has_section('source') else sid
        info[sid] = (title, build)
        rules = _inimage_rules(ini)
        annotate_source_dirs(root, sid, title, build)
        for r, ds, fs in os.walk(root):
            for f in fs:
                q = os.path.join(r, f)
                rel = os.path.relpath(q, root).replace(os.sep, '/')
                if rel == 'source.ini' or os.path.islink(q):
                    continue
                annotate_original(q, sid, title, build, rel, guess_inimage(rules, rel))

    class BuildOf:
        known = set(info)

        def __call__(self, sid):
            return info[sid][1]
    build_of = BuildOf()
    G = os.path.join(W, 'generated')
    if os.path.isdir(G):
        xput(G, {A_AUTHOR: 'lhde-import', A_DESC: 'everything lhde-import derives from the imported files (lhde-import --regenerate)',
                 A_TAGS: 'generated'})
        for k, d in (('frames', 'frame sets'), ('packs', 'luxshell pack images'), ('schemes', 'classic Appearance schemes')):
            xput(os.path.join(G, k), {A_AUTHOR: 'lhde-import', A_DESC: 'generated ' + d, A_TAGS: 'generated'})
        for s in sorted(glob.glob(os.path.join(G, 'frames', '*', 'manifest.ini'))):
            _annotate_generated_dir(W, os.path.dirname(s), 'frames', s, build_of)
        for s in sorted(glob.glob(os.path.join(G, 'packs', '*', 'manifest-*.ini'))):
            pd = os.path.dirname(s)
            ini = read_ini(s)
            rec = ini.get('recipe', 'kind', fallback='')
            args = ini.get('recipe', 'args', fallback='').split()
            sub = 'startpanel-' + args[0] if rec == 'startpanel' and args else 'shell32' if rec == 'shell32' else 'imageres'
            if os.path.isdir(os.path.join(pd, sub)):
                _annotate_generated_dir(W, os.path.join(pd, sub), 'packs', s, build_of)
            xput(s, {A_AUTHOR: 'lhde-import', A_DESC: 'manifest of the %s recipe' % rec, A_TAGS: 'generated'})
        for s in sorted(glob.glob(os.path.join(G, 'schemes', '*.scheme'))):
            origin = None
            try:
                with open(s, errors='replace') as f:
                    m = re.search(r'from (\S+)', f.read(600))
                    origin = m.group(1) if m else None
            except OSError:
                pass
            xput(s, {A_AUTHOR: 'lhde-import', A_DESC: 'classic Appearance scheme %s' % os.path.basename(s)[:-7], A_TAGS: 'generated,scheme',
                     A_SOURCE: 'generated by recipe schemes (lhdeschemes) from %s' % (origin or 'a hive INF')})
    L = os.path.join(W, 'local')
    for f in sorted(glob.glob(os.path.join(L, 'themes', '*.msstyles'))):
        xput(f, {A_DESC: '%s visual style (dropped in with lhde-import --style)' % os.path.basename(f)[:-9], A_TAGS: 'style'})
    for f in sorted(glob.glob(os.path.join(L, 'schemes', '*.scheme'))):
        xput(f, {A_DESC: 'classic Appearance scheme %s' % os.path.basename(f)[:-7], A_TAGS: 'scheme'})
    for k in ('resources', 'fonts', 'wallpapers'):
        for d in sorted(glob.glob(os.path.join(L, k, '*'))):
            if os.path.isdir(d) and not os.path.islink(d):
                xput(d, {A_DESC: 'local %s %s' % (k, os.path.basename(d)), A_TAGS: {'resources': 'system32'}.get(k, k.rstrip('s'))})
    xput(L, {A_DESC: 'files that come from no source (lhde-import --style, lhde-schemes, copied by hand)'})
    xput(W, {A_DESC: "lhde's Windows tree: imported Windows files, generated/ and local/ (docs/LAYOUT.md)"})


if __name__ == '__main__':
    sys.exit('lhdetree.py is a module of lhde-import')
