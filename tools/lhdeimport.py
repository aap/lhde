"""lhde's import of Windows resources (docs/USAGE.md "Getting the Windows resources", docs/LAYOUT.md "Asset policy").

Two stages, one code path:
  lhde-pack    ISO / WIM / Windows directory  --(data/sources/<source>.ini: which files, from where)-->  a resource pack:
               the raw, unmodified Windows files as <source>/{system32,themes,fonts,wallpapers,...}, source.ini, and MANIFEST
               (sha256, size, pack path, source, in-image path); a directory and lhde-resources-<date>.tar.zst (or .tar.xz)
  lhde-import  a pack (or an ISO / WIM / directory: packed into a temporary pack first)  -->  the Windows tree
               ($LHDE_WINDOWS, default ~/.local/share/lhde-windows; lhdetree.py): each <source>/ of the pack copied as it is,
               the index/ of symlinks the programs look things up through, then the recipes of [frames] and [packs] (frame
               sets, luxshell's pack images) run on those files into generated/ with a manifest each: derived pixels are
               always regenerated (lhde-import --regenerate), never packed.

Run with python3 -I. Needs 7z (ISO, WIM, the compressed *.XX_ files of an I386/AMD64 layout; cabextract also works for those),
Pillow, and lhde's ut-dump (build/bin or PATH) for the CompositedWindow and StartPanel recipes. Build 4015's pre-release WIM
(format 1.8.1) is read by wim0108.py beside this file.
"""
import configparser
import datetime
import fnmatch
import glob
import hashlib
import io
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.realpath(__file__))


def log(*a):
    print('lhde:', *a, flush=True)


def die(msg):
    print('lhde: ' + msg, file=sys.stderr)
    sys.exit(2)


# ---------------------------------------------------------------- the sources (data/sources/*.ini)
def source_dirs():
    out = []
    e = os.environ.get('LHDE_SOURCES')
    if e:
        out.append(e)
    for d in (os.path.join(HERE, '..', 'data', 'sources'), os.path.join(HERE, '..', '..', '..', 'share', 'lhde', 'sources')):   # the tree / installed: <prefix>/lib/lhde/tools
        if os.path.isdir(d):
            out.append(os.path.realpath(d))
    return out


def read_ini(path):
    c = configparser.ConfigParser(delimiters=('=',), comment_prefixes=(';', '#'), inline_comment_prefixes=(';',),
                                  interpolation=None, strict=False)
    c.optionxform = str
    c.read(path, encoding='utf-8')
    return c


class Source:
    def __init__(self, path):
        self.path = path
        self.ini = read_ini(path)
        s = self.ini['source']
        self.id = s.get('id', os.path.basename(path)[:-4])
        self.title = s.get('title', self.id)
        self.build = s.get('build', self.id)
        self.layout = s.get('layout', 'wim')
        self.isos = [x.strip() for x in s.get('iso', '').split('|') if x.strip()]
        self.wim = s.get('wim', 'sources/install.wim')
        self.image = s.get('image', '')
        self.windows = s.get('windows', 'WINDOWS')
        self.detect = s.get('detect', '')
        self.detect_file = s.get('detect_file', '')
        self.version_file = s.get('version_file', 'explorer.exe')
        self.default = s.getboolean('default', True)

    def rules(self):
        """(pack path, [patterns]) in file order"""
        if not self.ini.has_section('files'):
            return []
        return [(k, [p.strip() for p in v.split('|') if p.strip()]) for k, v in self.ini.items('files')]

    def section(self, name):
        return list(self.ini.items(name)) if self.ini.has_section(name) else []


def load_sources():
    out = {}
    for d in source_dirs():
        for p in sorted(glob.glob(os.path.join(d, '*.ini'))):
            s = Source(p)
            out.setdefault(s.id, s)
    return out


# ---------------------------------------------------------------- PE resources (styles, DLLs)
class PE:
    """the resources of a PE file: (type, name, lang) -> bytes; type/name an int or an upper-case string"""
    def __init__(self, path):
        d = open(path, 'rb').read()
        self.res = {}
        self.resources = self.res   # the name the research workspace's pe_resources.py uses (msstyles2theme.py)
        if d[:2] != b'MZ':
            return
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        if d[pe:pe + 4] != b'PE\0\0':
            return
        nsec, = struct.unpack_from('<H', d, pe + 6)
        optsz, = struct.unpack_from('<H', d, pe + 20)
        opt = pe + 24
        magic, = struct.unpack_from('<H', d, opt)
        dd = opt + (96 if magic == 0x10b else 112)
        rva, size = struct.unpack_from('<II', d, dd + 2 * 8)
        secs = []
        so = opt + optsz
        for i in range(nsec):
            vsize, va, rsize, rptr = struct.unpack_from('<IIII', d, so + 40 * i + 8)
            secs.append((va, max(vsize, rsize), rptr))

        def off(r):
            for va, sz, ptr in secs:
                if va <= r < va + sz:
                    return r - va + ptr
            return None
        base = off(rva)
        if base is None or not rva:
            return

        def name(v):
            if v & 0x80000000:
                o = base + (v & 0x7fffffff)
                n, = struct.unpack_from('<H', d, o)
                return d[o + 2:o + 2 + 2 * n].decode('utf-16le', 'replace').upper()
            return v

        def walk(o, depth, path):
            nn, ni = struct.unpack_from('<HH', d, o + 12)
            for i in range(nn + ni):
                nv, ov = struct.unpack_from('<II', d, o + 16 + 8 * i)
                p = path + (name(nv),)
                if ov & 0x80000000:
                    if depth < 2:
                        walk(base + (ov & 0x7fffffff), depth + 1, p)
                else:
                    drva, dsz = struct.unpack_from('<II', d, base + ov)
                    fo = off(drva)
                    if fo is not None and len(p) == 3:
                        self.res[p] = d[fo:fo + dsz]
        walk(base, 0, ())

    def get(self, typ, name, lang=None):
        if isinstance(typ, str):
            typ = typ.upper()
        if isinstance(name, str):
            name = name.upper()
        for (t, n, l), v in self.res.items():
            if t == typ and n == name and (lang is None or l == lang):
                return v
        return None

    def items(self, typ):
        if isinstance(typ, str):
            typ = typ.upper()
        return [(n, v) for (t, n, l), v in sorted(self.res.items(), key=lambda kv: str(kv[0])) if t == typ]

    def version(self):
        """VS_FIXEDFILEINFO's file version 'a.b.c.d', '' when none"""
        v = self.get(16, 1)
        if not v:
            for (t, n, l), x in self.res.items():
                if t == 16:
                    v = x
                    break
        if not v:
            return ''
        i = v.find(struct.pack('<I', 0xFEEF04BD))
        if i < 0:
            return ''
        ms, ls = struct.unpack_from('<II', v, i + 8)
        return '%d.%d.%d.%d' % (ms >> 16, ms & 0xffff, ls >> 16, ls & 0xffff)


def dib_image(d, premultiplied=None, tcolor=None, rounded=False):
    """an RT_BITMAP DIB -> PIL RGBA. 32 bpp: premultiplied (uxtheme's rule: any alpha outside {0,255}) is un-premultiplied; an
    all-zero alpha is opaque. Other depths: TRANSPARENTCOLOR (tcolor) becomes transparent."""
    from PIL import Image
    hs, w, h, planes, bpp = struct.unpack_from('<IiiHH', d, 0)
    if bpp != 32:
        ncol = struct.unpack_from('<I', d, 32)[0] if hs >= 36 else 0
        pal = (ncol or (1 << bpp)) * 4 if bpp <= 8 else (12 if struct.unpack_from('<I', d, 16)[0] == 3 else 0)
        im = Image.open(io.BytesIO(b'BM' + struct.pack('<IHHI', 14 + len(d), 0, 0, 14 + hs + pal) + d)).convert('RGBA')
        if tcolor:
            px = im.load()
            for y in range(im.height):
                for x in range(im.width):
                    if px[x, y][:3] == tcolor:
                        px[x, y] = (0, 0, 0, 0)
        return im
    raw = d[hs:hs + w * abs(h) * 4]
    im = Image.frombuffer('RGBA', (w, abs(h)), raw, 'raw', 'BGRA', 0, 1).copy()
    if h > 0:
        im = im.transpose(Image.FLIP_TOP_BOTTOM)
    alphas = set(im.getdata(3))
    if alphas == {0}:
        im.putalpha(255)
        return im
    pre = premultiplied if premultiplied is not None else bool(alphas - {0, 255})
    if pre:
        h_ = (lambda a: a // 2) if rounded else (lambda a: 0)   # luxshell's start panels were made with floor, 5048's frames rounded
        im.putdata([(min(255, (r * 255 + h_(a)) // a), min(255, (g * 255 + h_(a)) // a), min(255, (b * 255 + h_(a)) // a), a) if a else (0, 0, 0, 0)
                    for r, g, b, a in im.getdata()])
    return im


def icon_image(pe, group, size):
    """RT_GROUP_ICON `group` at `size` px (the exact size with the most colours, else the next larger, scaled)"""
    from PIL import Image
    d = pe.get(14, group)
    if not d:
        return None
    icons = {n: v for n, v in pe.items(3)}
    cnt, = struct.unpack_from('<H', d, 4)
    ents = []
    for i in range(cnt):
        w, h, c, r, planes, bpp, sz, iid = struct.unpack_from('<BBBBHHIH', d, 6 + 14 * i)
        if iid in icons:
            ents.append((w or 256, bpp, iid))
    if not ents:
        return None
    exact = [e for e in ents if e[0] == size]
    pick = max(exact, key=lambda e: e[1]) if exact else max([e for e in ents if e[0] >= size] or ents, key=lambda e: (-e[0], e[1]))
    data = icons[pick[2]]
    w = pick[0]
    if data[:8] == b'\x89PNG\r\n\x1a\n':
        im = Image.open(io.BytesIO(data)).convert('RGBA')
    else:
        im = Image.open(io.BytesIO(struct.pack('<HHH', 0, 1, 1) + struct.pack('<BBBBHHII', w % 256, w % 256, 0, 0, 1, pick[1], len(data), 22) + data)).convert('RGBA')
    if im.size[0] != size:
        im = im.resize((size, size), Image.LANCZOS)
    return im


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0 and not kw.get('check_ok'):
        raise RuntimeError('%s: %s' % (' '.join(cmd[:3]), (r.stderr or r.stdout).strip()[-400:]))
    return r.stdout


# ---------------------------------------------------------------- images: where files come from
def glob_re(pat):
    """a case-insensitive regex for a path glob: * within a component, ** any depth, ? one character, {a,b} alternatives"""
    out, i = '', 0
    while i < len(pat):
        c = pat[i]
        if pat.startswith('**', i):
            out += '.*'
            i += 2
            if i < len(pat) and pat[i] == '/':
                i += 1
            continue
        if c == '*':
            out += '[^/]*'
        elif c == '?':
            out += '[^/]'
        elif c == '{':
            j = pat.index('}', i)
            out += '(?:' + '|'.join(re.escape(x) for x in pat[i + 1:j].split(',')) + ')'
            i = j
        else:
            out += re.escape(c)
        i += 1
    return re.compile('^' + out + '$', re.I)


def literal_prefix(pat):
    """the directory part of a pattern before its first glob character ('Resources/Themes/**' -> 'Resources/Themes/')"""
    m = re.search(r'[*?{]', pat)
    head = pat if not m else pat[:m.start()]
    return head[:head.rfind('/') + 1] if '/' in head else ''


class Seven:
    """an archive 7z reads (ISO, WIM): its member paths and extraction"""
    def __init__(self, path):
        self.path = path
        self._members = None

    def members(self):
        if self._members is None:
            out = subprocess.run(['7z', 'l', '-slt', self.path], capture_output=True, text=True).stdout
            ms, cur, isdir = [], None, False
            for line in out.splitlines():
                if line.startswith('Path = '):
                    cur = line[7:].replace('\\', '/')
                    isdir = False
                elif line.startswith('Folder = +') or line.startswith('Attributes = D'):
                    isdir = True
                elif line == '' and cur is not None:
                    if not isdir and cur != self.path:
                        ms.append(cur)
                    cur = None
            if cur is not None and not isdir and cur != self.path:
                ms.append(cur)
            self._members = ms
        return self._members

    def extract(self, paths, dest):
        """extract the members `paths` under dest (their archive paths kept); returns {path: local file}"""
        os.makedirs(dest, exist_ok=True)
        if not paths:
            return {}
        lf = os.path.join(dest, '.list')
        with open(lf, 'w', encoding='utf-8') as f:
            f.write('\n'.join(paths) + '\n')
        subprocess.run(['7z', 'x', '-y', '-scsUTF-8', '-o' + dest, self.path, '@' + lf], capture_output=True, text=True)
        os.unlink(lf)
        out = {}
        for p in paths:
            q = os.path.join(dest, p)
            if os.path.isfile(q):
                out[p] = q
        return out


class Image:
    """what a source's [files] rules match against: member paths relative to the Windows directory (wim, wim0108, tree) or to
    the medium's root (cab), and a way to get them as local files"""
    def members(self):
        raise NotImplementedError

    def fetch(self, members, tmp):
        raise NotImplementedError


class WimImage(Image):
    def __init__(self, wim, index, windows):
        self.a = Seven(wim)
        allm = self.a.members()
        pre = ''
        if index and any(m.startswith(index + '/') for m in allm):
            pre = index + '/'
        win = windows.strip('/')
        self.root = pre
        if win:
            for m in allm:
                if m.startswith(pre) and m[len(pre):].lower().startswith(win.lower() + '/'):
                    self.root = pre + m[len(pre):len(pre) + len(win) + 1]
                    break
        self.map = {m[len(self.root):]: m for m in allm if m.startswith(self.root)}

    def members(self):
        return list(self.map)

    def fetch(self, members, tmp):
        got = self.a.extract([self.map[m] for m in members], tmp)
        return {m: got[self.map[m]] for m in members if self.map[m] in got}


class Wim0108Image(Image):
    """build 4015's WIM 1.8.1 (tools/wim0108.py)"""
    def __init__(self, wim, index, windows):
        sys.path.insert(0, HERE)
        import wim0108
        self.w = wim0108.Wim(wim)
        self.index = int(index or 2)
        m, t = self.w.tree(self.index)
        win = windows.strip('/').lower()
        self.map = {}
        for p, e in t:
            if e['attr'] & 0x10:
                continue
            q = p.strip('\\').replace('\\', '/')
            if win and not q.lower().startswith(win + '/'):
                continue
            self.map[q[len(win) + 1:] if win else q] = e

    def members(self):
        return list(self.map)

    def fetch(self, members, tmp):
        out = {}
        for m in members:
            e = self.map[m]
            r = self.w.byid.get(e['id']) if e['id'] else None
            dst = os.path.join(tmp, m)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, 'wb') as f:
                f.write(self.w.read(r) if r else b'')
            out[m] = dst
        return out


def expand(path, dest):
    """a compressed setup file (*.DL_, MSCF cabinet) -> its one file in dest (real name); other files as they are"""
    with open(path, 'rb') as f:
        magic = f.read(4)
    os.makedirs(dest, exist_ok=True)
    if magic == b'MSCF':
        before = set(os.listdir(dest))
        r = subprocess.run(['7z', 'x', '-y', '-o' + dest, path], capture_output=True, text=True)
        new = sorted(set(os.listdir(dest)) - before)
        if r.returncode != 0 or not new:
            if shutil.which('cabextract'):
                subprocess.run(['cabextract', '-q', '-d', dest, path], capture_output=True)
                new = sorted(set(os.listdir(dest)) - before)
        if new:
            return os.path.join(dest, new[0])
        raise RuntimeError('cannot expand ' + path)
    q = os.path.join(dest, os.path.basename(path))
    shutil.copyfile(path, q)
    return q


class CabImage(Image):
    """an I386 / AMD64 setup layout (Windows 2000, Server 2003, Longhorn 4066): members are the medium's paths; *.XX_ files are
    expanded when fetched (their real name comes from the cabinet)"""
    def __init__(self, medium):
        self.medium = medium
        if os.path.isdir(medium):
            self.map = {}
            for r, ds, fs in os.walk(medium):
                for f in fs:
                    p = os.path.relpath(os.path.join(r, f), medium).replace(os.sep, '/')
                    self.map[p] = p
        else:
            self.a = Seven(medium)
            self.map = {m: m for m in self.a.members()}

    def members(self):
        return list(self.map)

    def fetch(self, members, tmp):
        raw = os.path.join(tmp, '.raw')
        if os.path.isdir(self.medium):
            got = {m: os.path.join(self.medium, m) for m in members}
        else:
            got = self.a.extract(members, raw)
        out = {}
        for m in members:
            if m not in got:
                continue
            out[m] = expand(got[m], os.path.join(tmp, os.path.dirname(m), '.x', os.path.basename(m))) if m.endswith('_') else got[m]
        return out


class TreeImage(Image):
    """a Windows directory (an installed system, an extracted image): paths relative to it, matched case-insensitively"""
    def __init__(self, root):
        self.root = root
        self.map = {}
        for r, ds, fs in os.walk(root):
            for f in fs:
                p = os.path.relpath(os.path.join(r, f), root).replace(os.sep, '/')
                self.map[p] = os.path.join(r, f)

    def members(self):
        return list(self.map)

    def fetch(self, members, tmp):
        return {m: self.map[m] for m in members if os.path.isfile(self.map[m])}


# ---------------------------------------------------------------- packing (lhde-pack)
MANIFEST_HEAD = '# lhde resource pack (docs/LAYOUT.md "Asset policy"): sha256, size, pack path, source, in-image path; raw Windows files only\n'


def match_rules(src, members):
    """[(pack path, member)] for the source's rules: a pack path ending in / is a directory (the member's path below the
    pattern's literal prefix, or for an expanded setup file its real name, goes there)"""
    out = []
    for dest, pats in src.rules():
        hit = False
        for pat in pats:
            rx = glob_re(pat)
            for m in members:
                if rx.match(m):
                    hit = True
                    if dest.endswith('/'):
                        rel = m[len(literal_prefix(pat)):] if m.lower().startswith(literal_prefix(pat).lower()) else os.path.basename(m)
                        out.append((dest + rel, m, dest))
                    else:
                        out.append((dest, m, None))
        if not hit:
            log('  %s: no file for %s (%s)' % (src.id, dest, ' | '.join(pats)))
    return out


def pack_image(src, image, pack, tmp, origin):
    """the source's files out of image into pack/<id>/; returns MANIFEST lines"""
    import lhdetree
    pairs = match_rules(src, image.members())
    if not pairs:
        raise RuntimeError('%s: nothing matched (not this source?)' % src.id)
    import lhdetree
    got = image.fetch(sorted(set(m for _, m, _ in pairs)), os.path.join(tmp, 'fetch'))
    lines, n = [], 0
    root = os.path.join(pack, src.id)
    for dest, m, ddir in pairs:
        if m not in got:
            log('  %s: %s could not be read' % (src.id, m))
            continue
        local = got[m]
        if ddir is not None and m.endswith('_') and isinstance(image, CabImage):
            dest = ddir + os.path.basename(local)    # an expanded setup file: its real name
        q = os.path.join(root, dest)
        os.makedirs(os.path.dirname(q), exist_ok=True)
        shutil.copyfile(local, q)
        lhdetree.annotate_original(q, src.id, src.title, src.build, dest, m)
        lines.append('%s\t%d\t%s/%s\t%s\t%s' % (sha256(q), os.path.getsize(q), src.id, dest, src.id, origin + ':' + m))
        n += 1
    shutil.copyfile(src.path, os.path.join(root, 'source.ini'))
    lhdetree.annotate_source_dirs(root, src.id, src.title, src.build)
    size = sum(os.path.getsize(os.path.join(r, f)) for r, ds, fs in os.walk(root) for f in fs)
    log('  %s: %d files, %.1f MB -> %s' % (src.id, n, size / 1e6, root))
    shutil.rmtree(os.path.join(tmp, 'fetch'), ignore_errors=True)
    return lines


def find_member(members, name):
    for m in members:
        if m.lower() == name.lower() or m.lower().endswith('/' + name.lower()):
            return m
    return None


def is_wim0108(path):
    with open(path, 'rb') as f:
        h = f.read(16)
    return len(h) >= 8 and struct.unpack_from('<II', h, 0) == (0x7e, 0x00010801)   # header size 0x7e, version 1.8.1 (no MSWIM magic)


def image_for(src, medium, tmp, wim_override=None):
    """the Image of src on medium (an ISO, a WIM, a directory)"""
    if src.layout == 'cab':
        return CabImage(medium), medium
    if src.layout == 'tree' or os.path.isdir(medium):
        return TreeImage(medium), medium
    wim = wim_override
    if not wim:
        if medium.lower().endswith('.wim'):
            wim = medium
        else:
            a = Seven(medium)
            m = find_member(a.members(), src.wim)
            if not m:
                raise RuntimeError('%s: no %s on %s' % (src.id, src.wim, medium))
            log('  %s: extracting %s from %s' % (src.id, m, os.path.basename(medium)))
            wim = a.extract([m], os.path.join(tmp, 'wim'))[m]
    if src.layout == 'wim0108' or is_wim0108(wim):
        return Wim0108Image(wim, src.image, src.windows), wim
    return WimImage(wim, src.image, src.windows), wim


def detect(medium, sources, tmp):
    """the source a medium is (an ISO, a WIM, a Windows directory): by the version of its explorer.exe (or version_file) and
    its detect_file; returns (source, image) or (None, reason)"""
    cands = [s for s in sources.values() if s.detect]
    base = os.path.basename(medium).lower()
    named = [s for s in cands if any(i.lower() == base for i in s.isos)]
    order = named + [s for s in cands if s not in named and s.default] + [s for s in cands if s not in named and not s.default]
    tried, wims = [], {}
    for s in order:
        if os.path.isdir(medium) and s.layout == 'cab':
            pass
        try:
            key = (s.layout == 'cab' or os.path.isdir(medium), s.wim.lower(), s.image, s.windows)
            if not os.path.isdir(medium) and s.layout != 'cab' and not medium.lower().endswith('.wim'):
                a = Seven(medium)
                m = find_member(a.members(), s.wim)
                if not m:
                    continue
                if m not in wims:
                    log('  extracting %s to look at it' % m)
                    wims[m] = a.extract([m], os.path.join(tmp, 'wim%d' % len(wims)))[m]
                img, _ = image_for(s, medium, tmp, wims[m])
            elif s.layout == 'cab' and os.path.isdir(medium):
                img = CabImage(medium)
            else:
                img, _ = image_for(s, medium, tmp)
            ms = img.members()
            if s.detect_file and not any(glob_re(s.detect_file).match(m) for m in ms):
                tried.append('%s (no %s)' % (s.id, s.detect_file))
                continue
            vf = s.version_file
            m = next((x for x in ms if glob_re(vf).match(x)), None)
            if not m:
                tried.append('%s (no %s)' % (s.id, vf))
                continue
            got = img.fetch([m], os.path.join(tmp, 'ver'))
            v = PE(got[m]).version() if m in got else ''
            shutil.rmtree(os.path.join(tmp, 'ver'), ignore_errors=True)
            if v.startswith(s.detect):
                log('  %s: %s %s -> source %s (%s)' % (os.path.basename(medium), m, v, s.id, s.title))
                return s, img
            tried.append('%s (%s %s)' % (s.id, m, v or '?'))
        except Exception as e:
            tried.append('%s (%s)' % (s.id, e))
    return None, '; '.join(tried)


def _tar_xattrs():
    """GNU tar keeps the user.* attributes (the annotations of docs/ANNOTATIONS.md) in a pax archive; --no-annotate: none"""
    import lhdetree
    return ['--xattrs', '--xattrs-include=user.*'] if lhdetree.ANNOTATE['on'] else []


def write_pack(pack, lines, tarball):
    with open(os.path.join(pack, 'MANIFEST'), 'a') as f:
        if f.tell() == 0:
            f.write(MANIFEST_HEAD)
        for l in lines:
            f.write(l + '\n')
    if tarball:
        if shutil.which('zstd'):
            out = pack.rstrip('/') + '.tar.zst'
            subprocess.run(['tar', '-C', os.path.dirname(pack.rstrip('/')), *_tar_xattrs(), '-I', 'zstd -19 -T0', '-cf', out, os.path.basename(pack.rstrip('/'))], check=True)
        else:
            out = pack.rstrip('/') + '.tar.xz'
            subprocess.run(['tar', '-C', os.path.dirname(pack.rstrip('/')), *_tar_xattrs(), '-cJf', out, os.path.basename(pack.rstrip('/'))], check=True)
        log('pack archive %s (%.1f MB)' % (out, os.path.getsize(out) / 1e6))
        return out


# ---------------------------------------------------------------- importing a pack (lhde-import --pack)
def data_dir(arg=None):
    """the Windows tree (lhdetree.windows_dir)"""
    import lhdetree
    return lhdetree.windows_dir(arg)


def unpack(path, tmp):
    """a pack directory, or a .tar.zst / .tar.xz / .tar of one -> the directory"""
    if os.path.isdir(path):
        return path
    d = os.path.join(tmp, 'pack')
    os.makedirs(d, exist_ok=True)
    comp = ['-I', 'zstd -d'] if path.endswith('.zst') else []
    r = subprocess.run(['tar', '-C', d, *_tar_xattrs(), *comp, '-xf', path])   # restores the user.* annotations
    if r.returncode:
        log('tar could not restore the annotations (extended attributes); extracting without them')
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d, exist_ok=True)
        subprocess.run(['tar', '-C', d, *comp, '-xf', path], check=True)
    subs = [os.path.join(d, x) for x in os.listdir(d)]
    return subs[0] if len(subs) == 1 and os.path.isdir(subs[0]) and not os.path.exists(os.path.join(subs[0], 'source.ini')) else d


def ci_path(root, rel):
    """root/rel with every component matched case-insensitively (the images spell Themes/aero or themes/Aero)"""
    p = root
    for c in rel.split('/'):
        q = os.path.join(p, c)
        if not os.path.exists(q) and os.path.isdir(p):
            q = next((os.path.join(p, x) for x in os.listdir(p) if x.lower() == c.lower()), q)
        p = q
    return p


def copy_file(src, dst, force, rep):
    import lhdetree
    if os.path.exists(dst) and not force and os.path.getsize(dst) == os.path.getsize(src):
        rep['kept'] += 1
        lhdetree.copy_xattrs(src, dst)
        return
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(src, dst)
    lhdetree.copy_xattrs(src, dst)   # a pack's annotations (docs/ANNOTATIONS.md)
    rep['copied'] += 1


def copy_tree(src, dst, force, rep):
    import lhdetree
    for r, ds, fs in os.walk(src):
        if os.path.isdir(os.path.join(dst, os.path.relpath(r, src))) or not fs:
            os.makedirs(os.path.join(dst, os.path.relpath(r, src)), exist_ok=True)
            lhdetree.copy_xattrs(r, os.path.join(dst, os.path.relpath(r, src)))
        for f in fs:
            s = os.path.join(r, f)
            copy_file(s, os.path.join(dst, os.path.relpath(s, src)), force, rep)


def import_pack(pack, W, force=False, recipes=True):
    """every source of the pack into the Windows tree W: W/<source>/ = the pack's <source>/ (raw files, source.ini), the
    classic schemes into W/generated/schemes, the index, then the recipes into W/generated"""
    import lhdetree
    srcs = sorted(d for d in os.listdir(pack) if os.path.isfile(os.path.join(pack, d, 'source.ini')))
    if not srcs:
        die('%s is no resource pack (no <source>/source.ini)' % pack)
    todo = []
    for sid in srcs:
        root = os.path.join(pack, sid)
        s = Source(os.path.join(root, 'source.ini'))
        rep = {'copied': 0, 'kept': 0}
        os.makedirs(os.path.join(W, sid), exist_ok=True)
        lhdetree.copy_xattrs(root, os.path.join(W, sid))
        for f in sorted(os.listdir(root)):
            p = os.path.join(root, f)
            if os.path.isdir(p):
                copy_tree(p, os.path.join(W, sid, f), force, rep)
            elif f.lower().endswith(('.exe', '.dll')):   # a loose binary of an older pack: with the others
                copy_file(p, os.path.join(W, sid, 'system32', f), force, rep)
            else:
                copy_file(p, os.path.join(W, sid, f), True, rep)   # source.ini: always the pack's
        log('%s (%s): %d files copied, %d kept -> %s' % (sid, s.title, rep['copied'], rep['kept'], os.path.join(W, sid)))
        import_schemes(W, sid, s.ini, force)
        todo.append(s)
    lhdetree.reindex(W)
    if recipes:
        for s in todo:
            run_recipes(W, s.id, s.ini, force)
    lhdetree.annotate_tree(W)
    with open(os.path.join(W, 'import.log'), 'a') as f:
        f.write('# lhde-import --pack %s, %s: %s\n' % (pack, datetime.datetime.now().strftime('%Y-%m-%d %H:%M'), ' '.join(srcs)))


def import_schemes(W, sid, ini, force):
    """[schemes] NAME = path of a hive INF (HIVEDEF.INF) in W/<sid> -> W/generated/schemes/<scheme>.scheme (tools/lhdeschemes.py)"""
    import lhdetree
    for name, rel in (list(ini.items('schemes')) if ini.has_section('schemes') else []):
        p = lhdetree.ci_path(os.path.join(W, sid), rel)
        if not os.path.isfile(p):
            log('  %s: schemes %s: %s is not imported' % (sid, name, rel))
            continue
        import lhdeschemes
        lhdeschemes.import_inf(p, os.path.join(W, 'generated', 'schemes'), force, '%s/%s' % (sid, rel), log)


def run_recipes(W, sid, ini, force):
    """a source's [frames] and [packs]: from the shipped sources/<sid>.ini when there is one (the recipes of this version),
    else from the copy imported with it"""
    import lhdetree
    p = lhdetree.shipped_source_ini(sid)
    if p:
        ini = read_ini(p)
    for kind in ('frames', 'packs'):
        for target, spec in (list(ini.items(kind)) if ini.has_section(kind) else []):
            run_recipe(os.path.join(W, 'index'), kind, target, spec, sid, force)


# ---------------------------------------------------------------- recipes (derived pixels: generated/ + manifest.ini)
# Every recipe gets `data` = the Windows tree's index/: it reads its inputs by their index names (styles/NAME.msstyles,
# resources/<build>/imageres.dll) and writes below data/frames/<set>/ or data/packs/<build>/, i.e. into generated/.
def tool(name):
    """lhde's programs: the build tree's build/bin, beside an installed tools/ (<prefix>/bin), PATH"""
    for d in (os.path.join(HERE, '..', 'build', 'bin'), os.path.join(HERE, '..', '..', '..', 'bin')):
        p = os.path.join(d, name)
        if os.access(p, os.X_OK):
            return os.path.realpath(p)
    return shutil.which(name)


def write_manifest(dirpath, recipe, args, inputs, outputs):
    os.makedirs(dirpath, exist_ok=True)
    with open(os.path.join(dirpath, 'manifest.ini'), 'w') as f:
        f.write('; lhde-import recipe: the files here are derived from the imported files below and may be deleted;\n'
                '; lhde-import --regenerate makes them again (tools/lhdeimport.py)\n')
        f.write('[recipe]\nkind = %s\nargs = %s\nwhen = %s\noutputs = %d\n\n[inputs]\n' % (recipe, args, datetime.date.today(), outputs))
        for p, h in inputs:
            f.write('%s = %s\n' % (p, h))


def recipe_inputs(data, rels):
    out = []
    for r in rels:
        p = os.path.join(data, r)
        if not os.path.isfile(p):
            raise RuntimeError('needs %s (not imported)' % r)
        out.append((r, sha256(p)))
    return out


def style_rel(data, name):
    if os.path.isfile(os.path.join(data, 'styles', name + '.msstyles')):
        return 'styles/%s.msstyles' % name
    raise RuntimeError('needs styles/%s.msstyles (not imported)' % name)


# luxwm's names for the CompositedWindow streams (luxwm/frame_ut.c pngmap): file, part, half (0 top, 1 bottom, -1 whole), grey
PNGMAP = [('plate_active.png', 'LGFRAME', 0, 0), ('plate_inactive.png', 'LGFRAME', 1, 0),
          ('plate_explorer_active.png', 'LGFRAMEEXPLORER', 0, 0), ('plate_explorer_inactive.png', 'LGFRAMEEXPLORER', 1, 0),
          ('plate_max_active.png', 'LGFRAMEMAX', 0, 0), ('plate_max_inactive.png', 'LGFRAMEMAX', 1, 0),
          ('plate_max_explorer_active.png', 'LGFRAMEMAXEXPLORER', 0, 0), ('plate_max_explorer_inactive.png', 'LGFRAMEMAXEXPLORER', 1, 0),
          ('overlay.png', 'LGFRAMEOVERLAY', -1, 1), ('shadow.png', 'LGFRAMESHADOW', -1, 0), ('close.png', 'LGCLOSEBTN', -1, 0),
          ('glow_close.png', 'LGCLOSEBTNOVERLAY', -1, 0), ('max.png', 'LGMAXBTN', -1, 0), ('min.png', 'LGMINBTN', -1, 0),
          ('restore.png', 'LGRESTOREBTN', -1, 0), ('glow_btn.png', 'LGBTNOVERLAY', -1, 0), ('captionfog.png', 'CAPTIONFOG', -1, 0),
          ('default_icon.png', 'DEFAULTICON', -1, 0)]


def composited_pngs(data, style, out, only=None):
    """the CompositedWindow PNG streams of a style under luxwm's names (ut-dump --extract-streams)"""
    from PIL import Image
    ut = tool('ut-dump')
    if not ut:
        raise RuntimeError('needs ut-dump (make)')
    with tempfile.TemporaryDirectory() as t:
        run([ut, os.path.join(data, style_rel(data, style)), '--extract-streams', t])
        n = 0
        os.makedirs(out, exist_ok=True)
        for fn, part, half, grey in PNGMAP:
            if only and fn not in only:
                continue
            p = os.path.join(t, 'CompositedWindow.%s.PNGSTREAM.png' % part)
            if not os.path.isfile(p):
                continue
            im = Image.open(p)
            rgb = im.mode in ('RGB', 'P', 'L')
            im = im.convert('RGBA')
            if half >= 0:
                h = im.height // 2
                im = im.crop((0, half * h, im.width, (half + 1) * h))
            if grey and rgb:   # a grey RGB overlay is an additive highlight: white at alpha = grey (frame_ut.c)
                im.putdata([(255, 255, 255, r) for r, g, b, a in im.getdata()])
            im.save(os.path.join(out, fn))
            n += 1
        return n


def r_frames_composited(data, target, args):
    style = args[0] if args else target
    out = os.path.join(data, 'frames', target)
    n = composited_pngs(data, style, out)
    return [style_rel(data, style)], n


def r_frames_vision(data, target, args):
    """Aero vision: the CompositedWindow sprites of the base style (aero-patch), cut by tools/build_aerovision.py"""
    style = args[0]
    out = os.path.join(data, 'frames', target)
    with tempfile.TemporaryDirectory() as t:
        composited_pngs(data, style, t, only=('close.png', 'max.png', 'min.png', 'restore.png'))
        os.makedirs(out, exist_ok=True)
        run([sys.executable, '-I', os.path.join(HERE, 'build_aerovision.py'), t, out])
    return [style_rel(data, style)], len(glob.glob(os.path.join(out, 'vision_*.png')))


def r_frames_classic(data, target, args):
    """a classic (msstyles-mode) frame set's PNGs from its style (tools/msstyles2theme.py); the tuning keys ship in
    data/frames/<target>/theme.ini, written into generated/ only when the set is not shipped"""
    style = args[0]
    color = args[1] if len(args) > 1 else None
    out = os.path.join(data, 'frames', target)
    with tempfile.TemporaryDirectory() as t:
        cmd = [sys.executable, '-I', os.path.join(HERE, 'msstyles2theme.py'), os.path.join(data, style_rel(data, style)), t]
        if color:
            cmd += ['--color', color]
        fonts = glob.glob(os.path.join(data, 'fonts', '*', 'trebuc*.ttf'))
        run(cmd)
        os.makedirs(out, exist_ok=True)
        n = 0
        for f in glob.glob(os.path.join(t, '*.png')):
            shutil.copyfile(f, os.path.join(out, os.path.basename(f)))
            n += 1
        shipped = any(os.path.isfile(os.path.join(d, 'frames', target, 'theme.ini')) for d in shipped_data_dirs())
        if not shipped and os.path.isfile(os.path.join(t, 'theme.ini')):
            txt = open(os.path.join(t, 'theme.ini')).read()
            txt = re.sub(r'(?m)^msstyles = .*$', 'msstyles = styles/%s.msstyles' % style, txt)
            open(os.path.join(out, 'theme.ini'), 'w').write(txt)
    return [style_rel(data, style)], n


def r_frames_udwm(data, target, args):
    """5048's glass: uDWM.dll's own BITMAP resources (COMPOSITION.md 14.4/15; 32-bit premultiplied): 1000 the plate (active top
    half, inactive bottom), 1002 the maximised plate, 1003 min, 1005 max, 1007 restore, 1009 close, 1011 / 1013 the glows"""
    build = args[0] if args else '5048'
    rel = 'resources/%s/uDWM.dll' % build
    p = os.path.join(data, rel)
    if not os.path.isfile(p):
        alt = [x for x in os.listdir(os.path.join(data, 'resources', build)) if x.lower() == 'udwm.dll'] if os.path.isdir(os.path.join(data, 'resources', build)) else []
        if not alt:
            raise RuntimeError('needs %s (not imported)' % rel)
        rel = 'resources/%s/%s' % (build, alt[0])
        p = os.path.join(data, rel)
    pe = PE(p)
    out = os.path.join(data, 'frames', target)
    os.makedirs(out, exist_ok=True)
    n = 0

    def bm(i):
        d = pe.get(2, i)
        if not d:
            raise RuntimeError('%s has no BITMAP %d' % (rel, i))
        return dib_image(d, premultiplied=True, rounded=True)
    for bid, names in ((1000, ('plate_active.png', 'plate_inactive.png')), (1002, ('plate_max_active.png', 'plate_max_inactive.png'))):
        im = bm(bid)
        h = im.height // 2
        im.crop((0, 0, im.width, h)).save(os.path.join(out, names[0]))
        im.crop((0, h, im.width, 2 * h)).save(os.path.join(out, names[1]))
        n += 2
    for bid, name in ((1003, 'min.png'), (1005, 'max.png'), (1007, 'restore.png'), (1009, 'close.png'), (1011, 'glow_btn.png'), (1013, 'glow_close.png')):
        bm(bid).save(os.path.join(out, name))
        n += 1
    return [rel], n


def r_frames_dwmwindow(data, target, args):
    """5112's glass: aero.msstyles 5112's DWMWindow streams (RT STREAM PNGs) composed by tools/build_aero5112.py"""
    style = args[0] if args else target
    rel = style_rel(data, style)
    pe = PE(os.path.join(data, rel))
    out = os.path.join(data, 'frames', target)
    with tempfile.TemporaryDirectory() as t:
        for n_, d in pe.items('STREAM'):
            open(os.path.join(t, 'STREAM_%s_1033.png' % n_), 'wb').write(d)
        os.makedirs(out, exist_ok=True)
        run([sys.executable, '-I', os.path.join(HERE, 'build_aero5112.py'), t, out])
    return [rel], len(glob.glob(os.path.join(out, '*.png')))


def r_packs_imageres(data, target, args):
    """packs/<build>/imageres/<id>.png: every PNGFILE of the build's imageres.dll (base given: only those that differ from
    base's, the 4051 overrides of the 4051-slate pack)"""
    build = args[0]
    base = args[1] if len(args) > 1 else None
    rel = 'resources/%s/imageres.dll' % build
    ins = recipe_inputs(data, [rel])
    pe = PE(os.path.join(data, rel))
    out = os.path.join(data, 'packs', target, 'imageres')
    os.makedirs(out, exist_ok=True)
    ref = PE(os.path.join(data, 'resources/%s/imageres.dll' % base)) if base else None
    n = 0
    for name, d in pe.items('PNGFILE'):
        if ref is not None and ref.get('PNGFILE', name) == d:
            continue
        open(os.path.join(out, '%s.png' % name), 'wb').write(d)
        n += 1
    if base:
        ins += recipe_inputs(data, ['resources/%s/imageres.dll' % base])
    return [r for r, _ in ins], n


SHELL32_IDS = (1, 3, 4, 16, 21, 23, 24, 25, 28, 134, 137, 235, 236, 237, 269, 348, 386, 387, 357, 346, 221, 329, 45)


def r_packs_shell32(data, target, args):
    """packs/<build>/shell32/<size>/<id>.png: the shell32.dll icons luxshell's start menu and places use"""
    build = args[0]
    rel = 'resources/%s/shell32.dll' % build
    recipe_inputs(data, [rel])
    pe = PE(os.path.join(data, rel))
    n = 0
    for size in (16, 24, 32, 48):
        out = os.path.join(data, 'packs', target, 'shell32', str(size))
        os.makedirs(out, exist_ok=True)
        for i in SHELL32_IDS:
            im = icon_image(pe, i, size)
            if im is not None:
                im.save(os.path.join(out, '%d.png' % i))
                n += 1
    return [rel], n


def ut_sections(ut, style, color, prefixes):
    """the ut-dump --list sections whose class starts with one of prefixes: [(name, [(key, value)])]"""
    cmd = [ut, style, '--list'] + (['--color', color] if color else [])
    txt = subprocess.run(cmd, capture_output=True, text=True).stdout
    secs, cur = [], None
    for line in txt.splitlines():
        m = re.match(r'^\[(.*)\]$', line)
        if m:
            cur = (m.group(1), []) if m.group(1).startswith(prefixes) else None
            if cur:
                secs.append(cur)
            continue
        if cur is not None and ' = ' in line:
            k, v = line.split(' = ', 1)
            cur[1].append((k.strip(), v.strip()))
    return secs


def r_packs_startpanel(data, target, args):
    """packs/<build>/startpanel-<name>/: the StartPanel / StartMenu:: classes of a style (one PNG per part/state, the
    bitmaps un-premultiplied; startpanel.ini with the decoded values), as luxshell's menu.lua reads them"""
    name, style = args[0], args[1]
    color = args[2] if len(args) > 2 else None
    ut = tool('ut-dump')
    if not ut:
        raise RuntimeError('needs ut-dump (make)')
    rel = style_rel(data, style)
    path = os.path.join(data, rel)
    pe = PE(path)
    out = os.path.join(data, 'packs', target, 'startpanel-' + name)
    os.makedirs(out, exist_ok=True)
    ini = ['; StartPanel / StartMenu classes of styles/%s.msstyles%s (lhde-import, ut-dump; FONT = face, lfHeight, weight, italic)' % (style, ' colour ' + color if color else '')]
    n = 0
    for sec, kvs in ut_sections(ut, path, color, ('StartPanel', 'StartMenu::')):
        ini.append('\n[%s]' % sec)
        kv = dict(kvs)
        tcol = None
        if kv.get('TRANSPARENT') == 'TRUE':
            tcol = tuple(int(x) for x in re.split(r'[ ,]+', kv.get('TRANSPARENTCOLOR', '255, 0, 255'))[:3])
        for k, v in kvs:
            if k == 'FONT':
                f = [x.strip() for x in v.split(',')]
                weight = 700 if any(x.lower() == 'bold' for x in f[2:]) else 400
                v = '%s, %s, %d, %d' % (f[0], f[1] if len(f) > 1 else '-12', weight, int(any(x.lower() == 'italic' for x in f[2:])))
            elif k.startswith('IMAGEFILE') and v.startswith('#'):
                bid = int(v[1:].split()[0])
                part = sec.replace('StartPanel.', '').replace('StartPanel', 'STARTPANEL').replace('StartMenu::', 'MENU_').replace('.', '_')
                part = re.sub(r'\((\w+)\)', r'_\1', part)
                d = pe.get(2, bid)
                if d:
                    dib_image(d, tcolor=tcol).save(os.path.join(out, part + '.png'))
                    n += 1
                v = '%s.png ; bitmap#%d' % (part, bid)
            elif re.match(r'^-?\d+(, -?\d+)+$', v):
                v = v.replace(', ', ' ')
            ini.append('  %s = %s' % (k, v))
    open(os.path.join(out, 'startpanel.ini'), 'w').write('\n'.join(ini) + '\n')
    return [rel], n


def r_packs_aero5112(data, target, args):
    """packs/5112/imageres: the Vista 5112 taskbar art of aero.msstyles 5112 under the Aero imageres ids it replaces (was
    luxshell/shellpack-aero5112.sh): the Start pill (bitmap#502, 3 states) with the flag (explorer.exe icon 7103) and "Start"
    (Segoe UI 5112), the bar (#298), task buttons (#517) widened, the flash button (#296), the tray background (#528)"""
    from PIL import Image, ImageDraw, ImageFont, ImageFilter
    style, build = args[0], args[1] if len(args) > 1 else '5112'
    rel = style_rel(data, style)
    exe = 'resources/%s/explorer.exe' % build
    fontf = 'fonts/%s/segoeui.ttf' % build
    recipe_inputs(data, [exe, fontf])
    pe = PE(os.path.join(data, rel))
    out = os.path.join(data, 'packs', target, 'imageres')
    os.makedirs(out, exist_ok=True)

    def bmp(i):
        return dib_image(pe.get(2, i))

    def widen(im, w, l, r):
        o = Image.new('RGBA', (w, im.height))
        o.paste(im.crop((0, 0, l, im.height)), (0, 0))
        o.paste(im.crop((im.width - r, 0, im.width, im.height)), (w - r, 0))
        o.paste(im.crop((l, 0, im.width - r, im.height)).resize((w - l - r, im.height), Image.NEAREST), (l, 0))
        return o

    def states(im, n):
        h = im.height // n
        return [im.crop((0, i * h, im.width, (i + 1) * h)) for i in range(n)]
    pill = states(bmp(502), 3)
    flag = icon_image(PE(os.path.join(data, exe)), 7103, 48).resize((20, 20), Image.LANCZOS)
    font = ImageFont.truetype(os.path.join(data, fontf), int(os.environ.get('START_PX', '16')))
    for st, iid in zip(pill, (1179, 1180, 1181)):
        im = st.copy()
        cl, ct, cb = 7, 0, 2
        ch = im.height - ct - cb
        im.alpha_composite(flag, (cl, ct + (ch - 20) // 2))
        tx = cl + 20 + 3
        asc, desc = font.getmetrics()
        ty = ct + (ch - asc - desc) // 2 + 1
        sh = Image.new('RGBA', im.size)
        ImageDraw.Draw(sh).text((tx + 2, ty + 2), 'Start', font=font, fill=(75, 75, 75, 255))
        im.alpha_composite(sh.filter(ImageFilter.GaussianBlur(0.6)))
        ImageDraw.Draw(im).text((tx, ty), 'Start', font=font, fill=(255, 255, 255, 255))
        im.save(os.path.join(out, '%d.png' % iid))
    bmp(298).save(os.path.join(out, '1187.png'))
    tb = states(bmp(517), 6)
    for idx, iid in ((0, 1182), (1, 1184), (4, 1185)):
        widen(tb[idx], 127, 9, 9).save(os.path.join(out, '%d.png' % iid))
    widen(tb[2], 127, 9, 9).save(os.path.join(out, 'taskbutton_pressed.png'))
    widen(tb[5], 127, 9, 9).save(os.path.join(out, 'taskbutton_hotchecked.png'))
    widen(bmp(296), 127, 12, 10).save(os.path.join(out, '1183.png'))
    bmp(528).save(os.path.join(out, 'tray_bg.png'))
    return [rel, exe, fontf], len(os.listdir(out))


RECIPES = {
    ('frames', 'composited'): r_frames_composited, ('frames', 'vision'): r_frames_vision, ('frames', 'classic'): r_frames_classic,
    ('frames', 'udwm'): r_frames_udwm, ('frames', 'dwmwindow'): r_frames_dwmwindow,
    ('packs', 'imageres'): r_packs_imageres, ('packs', 'shell32'): r_packs_shell32, ('packs', 'startpanel'): r_packs_startpanel,
    ('packs', 'aero5112'): r_packs_aero5112,
}


def shipped_data_dirs():
    import lhdetree
    return lhdetree.shipped_dirs()


def run_recipe(data, kind, target, spec, origin, force=False):
    """[frames] / [packs] TARGET = RECIPE ARG... of a source; the manifest goes beside the cache (frames/TARGET/manifest.ini,
    packs/TARGET/manifest-RECIPE[-ARG].ini)"""
    parts = spec.split()
    recipe, args = parts[0], parts[1:]
    fn = RECIPES.get((kind, recipe))
    if not fn:
        log('  %s %s: unknown recipe %s' % (kind, target, recipe))
        return False
    tdir = target if kind == 'frames' else target.split('/')[0]   # packs: packs/<build>/..., the key's /suffix only names it
    mdir = os.path.join(data, kind, tdir)
    mname = 'manifest.ini' if kind == 'frames' else 'manifest-%s.ini' % target.replace('/', '-')
    try:
        ins, n = fn(data, tdir, args)
        os.makedirs(mdir, exist_ok=True)
        write_manifest(mdir, recipe, ' '.join(args), [(r, sha256(os.path.join(data, r))) for r in ins], n)
        if mname != 'manifest.ini':
            os.replace(os.path.join(mdir, 'manifest.ini'), os.path.join(mdir, mname))
        log('  %s/%s: %s %s -> %d file%s' % (kind, target, recipe, ' '.join(args), n, '' if n == 1 else 's'))
        return True
    except Exception as e:
        log('  %s/%s: %s %s skipped: %s' % (kind, target, recipe, ' '.join(args), e))
        return False


def regenerate(W, force=True):
    """generated/ deleted and made again: every source's [schemes], [frames] and [packs], and the recipes of the styles in
    local/ (lhde-import --style); then the index. Nothing is read but the Windows tree itself."""
    import lhdetree
    shutil.rmtree(os.path.join(W, 'generated'), ignore_errors=True)
    for sid in lhdetree.sources_in(W):
        p = lhdetree.source_ini(W, sid)
        if p:
            import_schemes(W, sid, read_ini(p), True)
    lhdetree.reindex(W)
    n = 0
    for sid in lhdetree.sources_in(W):
        p = lhdetree.source_ini(W, sid)
        if p:
            run_recipes(W, sid, read_ini(p), True)
            n += 1
    for name, recipes in STYLE_RECIPES.items():
        if os.path.isfile(os.path.join(W, 'local', 'themes', name + '.msstyles')):
            for kind, target, spec in recipes:
                run_recipe(os.path.join(W, 'index'), kind, target, spec, 'style', True)
    lhdetree.reindex(W)
    lhdetree.annotate_tree(W)
    log('regenerated %s from %d sources' % (os.path.join(W, 'generated'), n))


# the recipes a style dropped in by hand feeds (lhde-import --style NAME; regenerate): kind, target, spec
STYLE_RECIPES = {'aero-patch': [('frames', 'aero-patch', 'composited aero-patch'), ('frames', 'aerovision', 'vision aero-patch')]}
