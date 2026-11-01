"""lhde's three trees on disk (docs/LAYOUT.md "The three trees"): where lhde-import puts the Windows files, the index the
programs look things up through (liblhde/data.c reads $LHDE_WINDOWS/index).

The Windows tree, $LHDE_WINDOWS (default ${XDG_DATA_HOME:-~/.local/share}/lhde-windows):
  <source>/      the raw files of one build exactly as the resource pack has them: system32/ themes/ fonts/ wallpapers/ setup/,
                 source.ini (the rules they were taken with and what lhde-import derives from them)
  local/         files that come from no source: themes/NAME.msstyles (+ NAME/Shell) dropped in with lhde-import --style,
                 schemes/ (lhde-schemes), resources/NAME/, wallpapers/NAME/, fonts/NAME/ copied by hand or from aap's workspace
  generated/     everything derived: frames/<set>/ (PNGs, manifest.ini, theme.ini of a set we do not ship),
                 packs/<build>/ (PNGs, manifest-*.ini), schemes/*.scheme (from a source's HIVEDEF.INF).
                 Deleting it is always safe: lhde-import --regenerate makes it again from the sources' recipes.
  index/         relative symlinks under the names programs ask for (made by reindex, never edited):
                 styles/NAME.msstyles, styles/NAME (the style's directory with Shell/), styles/NAME.scheme,
                 resources/<source>, fonts/<source>, wallpapers/<source>, frames -> ../generated/frames, packs -> ../generated/packs
  import.log
Run with python3 -I."""
import configparser
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


def windows_dir(arg=None):
    """the Windows tree: --windows DIR, else $LHDE_WINDOWS, else ${XDG_DATA_HOME:-~/.local/share}/lhde-windows"""
    return os.path.abspath(arg or os.environ.get('LHDE_WINDOWS') or os.path.join(xdg_data_home(), 'lhde-windows'))


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


# ---------------------------------------------------------------- sizes
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
            sub = 'shell32' if rec == 'shell32' else 'imageres'
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
