"""lhde-mime's model: the freedesktop shared-mime-info database (globs2, magic, subclasses, aliases, the type descriptions)
and the MIME Applications Associations files (the mimeapps.list layers, mimeinfo.cache, the desktop entries), read the way
the programs that open files read them:

  GIO       g_app_info_get_default_for_type (Firefox, GTK and GLib programs, the portal): the type's default (the files in
            order), else its first claimant (Added Associations, then the directories' mimeinfo.cache); else the same for
            its nearest parent type, and so on
  xdg-open  xdg-utils' generic path (an unknown or no XDG_CURRENT_DESKTOP; "lhde" is one): xdg-mime query default, the exact
            type only: the first [Default Applications] line in the files' order whose program exists, else the first line of
            defaults.list or mimeinfo.cache in $XDG_DATA_HOME, $XDG_DATA_DIRS
  flxp      flxp's shell layer (flxp/shell/apps.cpp): GIO's order, the claimants read from the desktop files' MimeType= in
            each directory (no cache)

The files, most important first (the spec's order; $desktop each entry of XDG_CURRENT_DESKTOP, lower-cased):
  $XDG_CONFIG_HOME/$desktop-mimeapps.list, $XDG_CONFIG_HOME/mimeapps.list, the same in each of $XDG_CONFIG_DIRS,
  $XDG_DATA_HOME/applications/{$desktop-mimeapps.list,mimeapps.list,defaults.list}, the same in each of $XDG_DATA_DIRS.
The flxp C++ reader and this module implement the same order; tests/mime.py checks both against xdg-mime and gio.

Python, not C on liblhde: the association code is flxp's C++ shell layer, which has no C interface; reading the same text
files twice is less than wrapping it."""
import os
import re
import struct
import sys

USER, LHDE, DESKTOP, SYSTEM = 'your choice', 'lhde', 'desktop', 'the distribution'


def _env_dirs(var, default):
    v = os.environ.get(var, '')
    return [d.rstrip('/') or '/' for d in (v or default).split(':') if d]


class Env:
    """The XDG directories as the programs see them (the environment's, or given)."""

    def __init__(self, environ=None):
        e = os.environ if environ is None else environ
        home = e.get('HOME', os.path.expanduser('~'))
        self.home = home
        self.config_home = (e.get('XDG_CONFIG_HOME') or os.path.join(home, '.config')).rstrip('/')
        self.data_home = (e.get('XDG_DATA_HOME') or os.path.join(home, '.local/share')).rstrip('/')
        self.config_dirs = [d.rstrip('/') for d in (e.get('XDG_CONFIG_DIRS') or '/etc/xdg').split(':') if d]
        self.data_dirs = [d.rstrip('/') for d in (e.get('XDG_DATA_DIRS') or '/usr/local/share:/usr/share').split(':') if d]
        self.desktops = [d.lower() for d in e.get('XDG_CURRENT_DESKTOP', '').split(':') if d]
        self.menu_prefix = e.get('XDG_MENU_PREFIX', '')
        self.path = e.get('PATH', '/usr/local/bin:/usr/bin:/bin')

    def short(self, p):
        return '~' + p[len(self.home):] if self.home and self.home != '/' and p.startswith(self.home + '/') else p

    def app_dirs(self):
        return [os.path.join(d, 'applications') for d in [self.data_home] + self.data_dirs]

    def which(self, prog):
        if not prog:
            return None
        if '/' in prog:
            return prog if os.access(prog, os.X_OK) else None
        for d in self.path.split(':'):
            f = os.path.join(d or '.', prog)
            if os.path.isfile(f) and os.access(f, os.X_OK):
                return f
        return None


# ---- key files ---------------------------------------------------------------------------------------------------------

def read_keyfile(path):
    """[(group, [(key, value, line number)])] in file order; None when unreadable."""
    try:
        with open(path, encoding='utf-8', errors='replace') as f:
            lines = f.read().split('\n')
    except OSError:
        return None
    groups = []
    for n, raw in enumerate(lines, 1):
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        if line.startswith('['):
            groups.append((line[1:line.index(']')] if ']' in line else line[1:], []))
        elif '=' in line and groups:
            k, v = line.split('=', 1)
            groups[-1][1].append((k.strip(), v.strip(), n))
    return groups


def split_list(v):
    out, cur, i = [], '', 0
    while i < len(v):
        if v[i] == '\\' and i + 1 < len(v) and v[i + 1] == ';':
            cur += ';'
            i += 2
            continue
        if v[i] == ';':
            if cur.strip():
                out.append(cur.strip())
            cur = ''
        else:
            cur += v[i]
        i += 1
    if cur.strip():
        out.append(cur.strip())
    return out


# ---- shared-mime-info --------------------------------------------------------------------------------------------------

class Glob:
    __slots__ = ('weight', 'type', 'pattern', 'cs', 'file')

    def __init__(self, weight, type_, pattern, cs, file):
        self.weight, self.type, self.pattern, self.cs, self.file = weight, type_, pattern, cs, file


class MagicRule:
    __slots__ = ('indent', 'offset', 'value', 'mask', 'word', 'range', 'children')

    def __init__(self):
        self.children = []

    def matches(self, data):
        n = len(self.value)
        value, mask = self.value, self.mask
        if self.word > 1 and sys.byteorder == 'little' and n % self.word == 0:
            value = b''.join(value[i:i + self.word][::-1] for i in range(0, n, self.word))
            if mask:
                mask = b''.join(mask[i:i + self.word][::-1] for i in range(0, n, self.word))
        for off in range(self.offset, self.offset + self.range):
            chunk = data[off:off + n]
            if len(chunk) < n:
                return False
            if mask:
                if all((a & m) == (b & m) for a, b, m in zip(chunk, value, mask)):
                    return True
            elif chunk == value:
                return True
        return False

    def tree_matches(self, data):
        return self.matches(data) and (not self.children or any(c.tree_matches(data) for c in self.children))


def parse_magic(path):
    """[(priority, type, [top-level MagicRule])] of a shared-mime-info magic file."""
    try:
        data = open(path, 'rb').read()
    except OSError:
        return []
    if not data.startswith(b'MIME-Magic\0\n'):
        return []
    i, out, stack = 12, [], []
    while i < len(data):
        if data[i:i + 1] == b'[':
            j = data.index(b']\n', i)
            prio, t = data[i + 1:j].decode('ascii', 'replace').split(':', 1)
            out.append((int(prio), t, []))
            stack = []
            i = j + 2
            continue
        r = MagicRule()
        j = data.index(b'>', i)
        r.indent = int(data[i:j]) if j > i else 0
        i = j + 1
        j = data.index(b'=', i)
        r.offset = int(data[i:j])
        i = j + 1
        n = struct.unpack('>H', data[i:i + 2])[0]
        i += 2
        r.value = data[i:i + n]
        i += n
        r.mask, r.word, r.range = None, 1, 1
        while data[i:i + 1] != b'\n':
            c = data[i:i + 1]
            if c == b'&':
                r.mask = data[i + 1:i + 1 + n]
                i += 1 + n
            elif c in (b'~', b'+'):
                j = i + 1
                while data[j:j + 1].isdigit():
                    j += 1
                v = int(data[i + 1:j])
                if c == b'~':
                    r.word = v
                else:
                    r.range = v
                i = j
            else:   # an extension this reader does not know: skip the line
                i = data.index(b'\n', i)
        i += 1
        if not out:
            continue
        while stack and stack[-1].indent >= r.indent:
            stack.pop()
        (stack[-1].children if stack else out[-1][2]).append(r)
        stack.append(r)
    return out


class MimeDb:
    def __init__(self, env):
        self.env = env
        self.mime_dirs = [os.path.join(d, 'mime') for d in [env.data_home] + env.data_dirs]
        self.globs = []
        self.aliases = {}
        self.parents = {}
        self.magic = []
        self.icons = {}
        self._comments = {}
        noglobs = set()
        for d in self.mime_dirs:
            blocked = set()
            try:
                text = open(os.path.join(d, 'globs2'), encoding='utf-8', errors='replace').read()
            except OSError:
                text = ''
            for line in text.split('\n'):
                if not line or line.startswith('#'):
                    continue
                parts = line.split(':')
                if len(parts) < 3:
                    continue
                w, t, pat = parts[0], parts[1], parts[2]
                if t in noglobs:
                    continue
                if pat == '__NOGLOBS__':
                    blocked.add(t)
                    continue
                cs = len(parts) > 3 and 'cs' in parts[3].split(',')
                self.globs.append(Glob(int(w) if w.isdigit() else 50, t, pat, cs, os.path.join(d, 'globs2')))
            noglobs |= blocked
            for name, into in (('aliases', self.aliases),):
                for line in _lines(os.path.join(d, name)):
                    a, _, b = line.partition(' ')
                    into.setdefault(a, b)
            for line in _lines(os.path.join(d, 'subclasses')):
                a, _, b = line.partition(' ')
                self.parents.setdefault(a, [])
                if b not in self.parents[a]:
                    self.parents[a].append(b)
            for line in _lines(os.path.join(d, 'generic-icons')):
                a, _, b = line.partition(':')
                self.icons.setdefault(a, b)
            for prio, t, rules in parse_magic(os.path.join(d, 'magic')):
                self.magic.append((prio, t, rules, os.path.join(d, 'magic')))
        self.magic.sort(key=lambda m: -m[0])

    def canonical(self, t):
        return self.aliases.get(t, t)

    def direct_parents(self, t):
        t = self.canonical(t)
        ps = list(self.parents.get(t, []))
        media = t.split('/')[0]
        if media == 'text' and t != 'text/plain' and 'text/plain' not in ps:
            ps.append('text/plain')
        if media != 'inode' and t != 'application/octet-stream' and not ps:
            ps.append('application/octet-stream')
        return ps

    def ancestors(self, t):
        """every ancestor, nearest first (breadth first, as GLib lists them)"""
        out, queue = [], [self.canonical(t)]
        while queue:
            for p in self.direct_parents(queue.pop(0)):
                p = self.canonical(p)
                if p not in out and p != self.canonical(t):
                    out.append(p)
                    queue.append(p)
        if 'application/octet-stream' in out:   # the root of everything goes last
            out.remove('application/octet-stream')
            out.append('application/octet-stream')
        return out

    def comment(self, t):
        t = self.canonical(t)
        if t in self._comments:
            return self._comments[t]
        c = ''
        for d in self.mime_dirs:
            try:
                text = open(os.path.join(d, t + '.xml'), encoding='utf-8', errors='replace').read()
            except OSError:
                continue
            m = re.search(r'<comment>([^<]*)</comment>', text)
            if m:
                c = m.group(1).replace('&amp;', '&').replace('&lt;', '<').replace('&gt;', '>').replace('&apos;', "'")
                break
        self._comments[t] = c
        return c

    def globs_for_name(self, name):
        """the matching globs, best first: case-sensitive ones, then by weight, then by pattern length"""
        import fnmatch
        lname = name.lower()
        hits = []
        for g in self.globs:
            if g.cs:
                ok = fnmatch.fnmatchcase(name, g.pattern)
            else:
                ok = fnmatch.fnmatchcase(lname, g.pattern.lower())
            if ok:
                hits.append(g)
        hits.sort(key=lambda g: (not g.cs, -g.weight, -len(g.pattern)))
        return hits

    def type_of_name(self, name):
        hits = self.globs_for_name(name)
        return hits[0].type if hits else ''

    def detect(self, path):
        """(type, how): how is a sentence (glob with its weight, magic with its priority, a directory, ...)"""
        if os.path.isdir(path):
            return 'inode/directory', 'a directory'
        if not os.path.exists(path):
            hits = self.globs_for_name(os.path.basename(path))
            if hits:
                return hits[0].type, 'no such file; by name: glob %s (weight %d, %s)' % (hits[0].pattern, hits[0].weight,
                                                                                          self.env.short(hits[0].file))
            return '', 'no such file'
        hits = self.globs_for_name(os.path.basename(path))
        if hits:
            best = [g for g in hits if g.weight == hits[0].weight and g.cs == hits[0].cs and len(g.pattern) == len(hits[0].pattern)]
            if len({g.type for g in best}) == 1:
                g = hits[0]
                return g.type, 'glob %s (weight %d, %s)' % (g.pattern, g.weight, self.env.short(g.file))
        try:
            data = open(path, 'rb').read(4096)
        except OSError:
            data = b''
        if not data:
            return 'application/x-zerosize', 'an empty file'
        for prio, t, rules, f in self.magic:
            if any(r.tree_matches(data) for r in rules):
                if hits and t not in {g.type for g in hits}:
                    continue
                return t, 'magic (priority %d, %s)' % (prio, self.env.short(f))
        if hits:
            return hits[0].type, 'glob %s (weight %d, several types; %s)' % (hits[0].pattern, hits[0].weight,
                                                                              self.env.short(hits[0].file))
        try:
            data[:512].decode('utf-8')
            text = b'\0' not in data[:512]
        except UnicodeDecodeError:
            text = False
        return ('text/plain', 'no glob or magic matches; looks like text') if text else \
            ('application/octet-stream', 'no glob or magic matches; binary')


def _lines(path):
    try:
        return [l for l in open(path, encoding='utf-8', errors='replace').read().split('\n') if l and not l.startswith('#')]
    except OSError:
        return []


# ---- desktop entries, mimeinfo.cache, mimeapps.list --------------------------------------------------------------------

class App:
    def __init__(self, id_, path):
        self.id, self.path = id_, path
        self.name = self.exec = self.tryexec = self.type = ''
        self.hidden = self.no_display = False
        self.mime_types, self.categories, self.actions = [], [], []
        self.action_names = {}
        self.shadowed = []          # the same id in less important directories

    def program(self):
        return self.exec.split()[0].strip('"') if self.exec.strip() else ''

    def usable(self, env):
        """installed as the spec means it: an Application, not Hidden, TryExec found"""
        if self.hidden or self.type != 'Application' or not self.exec:
            return False
        return not self.tryexec or env.which(self.tryexec) is not None


def read_app(id_, path):
    kf = read_keyfile(path)
    if kf is None:
        return None
    a = App(id_, path)
    for g, kv in kf:
        if g == 'Desktop Entry':
            for k, v, _ in kv:
                if k == 'Name':
                    a.name = v
                elif k == 'Exec':
                    a.exec = v
                elif k == 'TryExec':
                    a.tryexec = v
                elif k == 'Type':
                    a.type = v
                elif k == 'Hidden':
                    a.hidden = v in ('true', '1')
                elif k == 'NoDisplay':
                    a.no_display = v in ('true', '1')
                elif k == 'MimeType':
                    a.mime_types = split_list(v)
                elif k == 'Categories':
                    a.categories = split_list(v)
                elif k == 'Actions':
                    a.actions = split_list(v)
        elif g.startswith('Desktop Action '):
            for k, v, _ in kv:
                if k == 'Name':
                    a.action_names[g[len('Desktop Action '):]] = v
    return a


class ListFile:
    """one mimeapps.list (or defaults.list): the groups by type, the layer it belongs to"""

    def __init__(self, path, layer, kind):
        self.path, self.layer, self.kind = path, layer, kind      # kind: 'mimeapps', 'desktop', 'legacy'
        self.exists = os.path.isfile(path)
        self.default, self.added, self.removed = {}, {}, {}
        self.lines = {}             # (group, type) -> line number
        self.duplicates = []        # (group, type, line)
        self.ignored = []           # groups a desktop-specific file may not have (the spec: defaults only)
        kf = read_keyfile(path) if self.exists else None
        for g, kv in kf or []:
            into = {'Default Applications': self.default, 'Added Associations': self.added,
                    'Removed Associations': self.removed}.get(g)
            if into is not None and kind == 'desktop' and into is not self.default:
                if kv:
                    self.ignored.append(g)
                continue
            if into is None or (kind == 'legacy' and into is not self.default):
                continue
            for k, v, n in kv:
                if k in into:
                    self.duplicates.append((g, k, n))
                into[k] = split_list(v)      # a key twice in a group: the last one, as GLib's key files read it
                self.lines[(g, k)] = n


class Db:
    def __init__(self, env=None):
        self.env = env or Env()
        self.mime = MimeDb(self.env)
        self.apps = {}              # id -> App (the most important directory's)
        self.dir_apps = {}          # applications dir -> [ids] whose files lie there (in id order)
        self.caches = {}            # applications dir -> {type: [ids]} of its mimeinfo.cache, None without one
        for d in self.env.app_dirs():
            ids = []
            for root, dirs, files in os.walk(d, followlinks=True):
                dirs.sort()
                for f in sorted(files):
                    if not f.endswith('.desktop'):
                        continue
                    full = os.path.join(root, f)
                    id_ = os.path.relpath(full, d).replace('/', '-')
                    a = read_app(id_, full)
                    if a is None:
                        continue
                    ids.append(id_)
                    if id_ in self.apps:
                        self.apps[id_].shadowed.append(full)
                    else:
                        self.apps[id_] = a
            self.dir_apps[d] = sorted(ids)
            cache = os.path.join(d, 'mimeinfo.cache')
            if os.path.isfile(cache):
                c = {}
                for g, kv in read_keyfile(cache) or []:
                    if g == 'MIME Cache':
                        for k, v, _ in kv:
                            c[k] = split_list(v)
                self.caches[d] = c
            else:
                self.caches[d] = None
        self.lists = self._lists()

    def _layer(self, path, desktop):
        e = self.env
        if path.startswith(e.config_home + '/') or path.startswith(e.data_home + '/'):
            return USER
        if desktop == 'lhde':
            return LHDE
        if desktop:
            return DESKTOP + ' ' + desktop
        return SYSTEM

    def _lists(self):
        e, out = self.env, []
        for d in [e.config_home] + e.config_dirs:
            for desk in e.desktops:
                p = os.path.join(d, desk + '-mimeapps.list')
                out.append(ListFile(p, self._layer(p, desk), 'desktop'))
            p = os.path.join(d, 'mimeapps.list')
            out.append(ListFile(p, self._layer(p, ''), 'mimeapps'))
        for d in e.app_dirs():
            for desk in e.desktops:
                p = os.path.join(d, desk + '-mimeapps.list')
                out.append(ListFile(p, self._layer(p, desk), 'desktop'))
            p = os.path.join(d, 'mimeapps.list')
            out.append(ListFile(p, self._layer(p, ''), 'mimeapps'))
            p = os.path.join(d, 'defaults.list')
            out.append(ListFile(p, self._layer(p, ''), 'legacy'))
        return out

    def user_file(self):
        return os.path.join(self.env.config_home, 'mimeapps.list')

    def app(self, id_):
        a = self.apps.get(id_)
        return a if a is not None and a.usable(self.env) else None

    def removed_upto(self, i, t, id_):
        return any(id_ in self.lists[j].removed.get(t, []) for j in range(min(i + 1, len(self.lists))))

    def chain(self, t):
        t = self.mime.canonical(t)
        return [t] + self.mime.ancestors(t)

    # what each list sets for exactly this type, in order: [(ListFile, group, [ids])]
    def entries(self, t):
        out = []
        for lf in self.lists:
            for g, d in (('Default Applications', lf.default), ('Added Associations', lf.added),
                         ('Removed Associations', lf.removed)):
                for k, ids in d.items():
                    if self.mime.canonical(k) == t:
                        out.append((lf, g, ids))
        return out

    def default_exact(self, t):
        """(App, ListFile) of the first installed, not removed default of exactly this type"""
        for i, lf in enumerate(self.lists):
            for k, ids in lf.default.items():
                if self.mime.canonical(k) != t:
                    continue
                for id_ in ids:
                    a = self.app(id_)
                    if a and not self.removed_upto(i, t, id_):
                        return a, lf
        return None, None

    def claimants(self, t, source='cache'):
        """[(App or None, id, how, layer)] of exactly this type in order: each applications directory's Added Associations
        (of its lists, the config directories' first), then its own claims: mimeinfo.cache (source 'cache', GIO and
        xdg-open) or the desktop files' MimeType= (source 'files', flxp); removed ones left out"""
        out, seen = [], set()

        def add(id_, how, layer, i):
            if id_ in seen or self.removed_upto(i, t, id_):
                return
            seen.add(id_)
            out.append((self.app(id_), id_, how, layer))
        for i, lf in enumerate(self.lists):
            for k, ids in lf.added.items():
                if self.mime.canonical(k) == t:
                    for id_ in ids:
                        add(id_, 'Added Associations in ' + self.env.short(lf.path), lf.layer, i)
        last = len(self.lists) - 1
        for d in self.env.app_dirs():
            layer = 'user dir' if d.startswith(self.env.data_home + '/') else 'system dir'
            if source == 'cache':
                c = self.caches.get(d) or {}
                for k, ids in c.items():
                    if self.mime.canonical(k) == t:
                        for id_ in ids:
                            add(id_, 'listed in ' + self.env.short(os.path.join(d, 'mimeinfo.cache')), layer, last)
            else:
                for id_ in self.dir_apps.get(d, []):
                    a = self.apps.get(id_)
                    if a and a.path.startswith(d + '/') and any(self.mime.canonical(m) == t for m in a.mime_types):
                        add(id_, 'MimeType= of ' + self.env.short(a.path), layer, last)
        return out

    def resolve(self, t, source='cache'):
        """GIO's (source 'cache') or flxp's (source 'files') default: (App, reason, type it came from, layer)"""
        chain = self.chain(t)
        for c in chain:
            a, lf = self.default_exact(c)
            if a:
                why = '%s sets it as the default%s' % (self.env.short(lf.path), '' if c == chain[0] else
                                                       ' of the parent type ' + c + ' (nothing sets or claims ' + t + ')')
                return a, why, c, lf.layer
            for a, id_, how, layer in self.claimants(c, source):
                if a:
                    return a, '%s (%s) and nothing sets a default%s' % (
                        'it is the first claimant' if not how.startswith('Added') else 'it is the first added association',
                        how, '' if c == chain[0] else '; claimant of the parent type ' + c), c, 'nothing (first claimant)'
        return None, 'no default and no program claims it or a parent type', None, None

    def xdg_open(self, t):
        """xdg-mime query default TYPE in xdg-utils' generic mode: (id, reason)"""
        e = self.env
        dirs = [e.config_home] + e.config_dirs + e.app_dirs()
        for d in dirs:
            for desk in e.desktops + ['']:
                p = os.path.join(d, (desk + '-' if desk else '') + 'mimeapps.list')
                lf = next((l for l in self.lists if l.path == p), None) or ListFile(p, '', 'mimeapps')
                if t in lf.default:
                    for id_ in lf.default[t]:
                        a = self.apps.get(id_)
                        if a and e.which(a.program()):
                            return id_, '%s sets it as the default' % e.short(p)
        for d in e.app_dirs():
            for prefix in ([e.menu_prefix] if e.menu_prefix else []) + ['']:
                for name in ('defaults.list', 'mimeinfo.cache'):
                    p = os.path.join(d, prefix + name)
                    for line in _lines(p):
                        if line.startswith(t + '='):
                            ids = split_list(line.split('=', 1)[1])
                            if ids:
                                return ids[0], '%s lists it first and nothing sets a default' % e.short(p)
                            break
        return None, 'nothing sets a default and no defaults.list or mimeinfo.cache names the type'


# ---- the user's file: the writer flxp's set_default_app has ------------------------------------------------------------

def _edit(path, fn):
    try:
        lines = open(path, encoding='utf-8').read().split('\n')
        if lines and lines[-1] == '':
            lines.pop()
    except FileNotFoundError:
        lines = []
    fn(lines)
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    tmp = path + '.tmp%d' % os.getpid()
    with open(tmp, 'w', encoding='utf-8') as f:
        f.write(''.join(l + '\n' for l in lines))
    os.replace(tmp, path)


def _group(lines, name):
    for i, l in enumerate(lines):
        if l.strip() == '[' + name + ']':
            return i
    return -1


def _key(lines, g, k):
    for i in range(g + 1, len(lines)):
        t = lines[i].strip()
        if t.startswith('['):
            break
        if '=' in t and t.split('=', 1)[0].strip() == k:
            return i
    return -1


def _get(lines, grp, k):
    g = _group(lines, grp)
    i = _key(lines, g, k) if g >= 0 else -1
    return split_list(lines[i].split('=', 1)[1].strip()) if i >= 0 else []


def _set(lines, grp, k, ids, as_list):
    g = _group(lines, grp)
    value = ''.join(x + ';' for x in ids) if (as_list or len(ids) > 1) else ''.join(ids)
    if g < 0:
        if not ids:
            return
        if lines and lines[-1].strip():
            lines.append('')
        lines.extend(['[' + grp + ']', k + '=' + value])
        return
    i = _key(lines, g, k)
    if not ids:
        if i >= 0:
            del lines[i]
        return
    if i >= 0:
        lines[i] = k + '=' + value
        return
    end = last = g + 1
    while end < len(lines) and not lines[end].strip().startswith('['):
        if lines[end].strip():
            last = end + 1
        end += 1
    lines.insert(last, k + '=' + value)


def set_default(path, t, id_):
    """[Default Applications] TYPE=APP; APP first of the Added Associations, off the Removed ones (GLib's writer)"""
    def fn(lines):
        _set(lines, 'Default Applications', t, [id_], False)
        added = [x for x in _get(lines, 'Added Associations', t) if x != id_]
        _set(lines, 'Added Associations', t, [id_] + added, True)
        removed = _get(lines, 'Removed Associations', t)
        if id_ in removed:
            _set(lines, 'Removed Associations', t, [x for x in removed if x != id_], True)
    _edit(path, fn)


def reset(path, t):
    """the type's lines out of all three groups (GLib's g_app_info_reset_type_associations); True if there were any"""
    found = []

    def fn(lines):
        for grp in ('Default Applications', 'Added Associations', 'Removed Associations'):
            if _get(lines, grp, t):
                found.append(grp)
            g = _group(lines, grp)
            while g >= 0 and _key(lines, g, t) >= 0:
                del lines[_key(lines, g, t)]
    if os.path.isfile(path):
        _edit(path, fn)
    return bool(found)


# ---- the audit ---------------------------------------------------------------------------------------------------------

# types a program should claim only when it is that kind of program (its Categories)
PLAUSIBLE = {
    'inode/directory': ('FileManager', 'a file manager'),
    'x-scheme-handler/http': ('WebBrowser', 'a web browser'),
    'x-scheme-handler/https': ('WebBrowser', 'a web browser'),
    'text/html': ('WebBrowser', 'a web browser'),
}
CATCH_ALL = {'application/octet-stream', 'all/all', 'all/allfiles', '*/*', '*'}


def check(db):
    """[(kind, text)] findings, the most useful first"""
    e, out = db.env, []
    # 1. programs claiming what they unlikely handle
    for a in sorted(db.apps.values(), key=lambda a: a.id):
        if not a.usable(e):
            continue
        for m in a.mime_types:
            if m in PLAUSIBLE and PLAUSIBLE[m][0] not in a.categories and not (m == 'text/html' and 'TextEditor' in a.categories):
                out.append(('claim', '%s claims %s but is not %s (Categories=%s): %s' % (
                    a.id, m, PLAUSIBLE[m][1], ';'.join(a.categories) or 'none', e.short(a.path))))
            elif m in CATCH_ALL or m.endswith('/*'):
                out.append(('claim', '%s claims %s, every file of %s: %s' % (
                    a.id, m, 'that kind' if m.endswith('/*') and m != '*/*' else 'any type', e.short(a.path))))
    # 2. the verdict for the types where the first claimant is one of those
    for t in sorted(PLAUSIBLE):
        a, why, _, layer = db.resolve(t)
        if a and layer == 'nothing (first claimant)' and PLAUSIBLE[t][0] not in a.categories:
            out.append(('verdict', '%s: no default; GIO and xdg-open run %s because %s. Set one: lhde-mime default %s APP' % (
                t, a.id, why, t)))
    # 3. defaults naming programs that are not installed
    for lf in db.lists:
        for k, ids in lf.default.items():
            for id_ in ids:
                if id_ not in db.apps:
                    out.append(('missing', '%s:%d: [Default Applications] %s=%s: no such desktop file' % (
                        e.short(lf.path), lf.lines.get(('Default Applications', k), 0), k, id_)))
                elif not db.apps[id_].usable(e):
                    out.append(('missing', '%s:%d: [Default Applications] %s=%s: %s is hidden or its program is missing' % (
                        e.short(lf.path), lf.lines.get(('Default Applications', k), 0), k, id_, e.short(db.apps[id_].path))))
        for g, k, n in lf.duplicates:
            out.append(('duplicate', '%s:%d: %s twice in [%s]; the last line counts' % (e.short(lf.path), n, k, g)))
        for g in lf.ignored:
            out.append(('ignored', '%s: [%s] in a desktop-specific file is not read (only [Default Applications] is; put it in '
                        'mimeapps.list)' % (e.short(lf.path), g)))
    # 4. caches that do not match their directory's desktop files
    for d in e.app_dirs():
        claims = {}
        for id_ in db.dir_apps.get(d, []):
            a = db.apps.get(id_)
            if a and a.path.startswith(d + '/'):
                for m in a.mime_types:
                    claims.setdefault(m, []).append(id_)
        c = db.caches.get(d)
        if c is None:
            if claims:
                out.append(('cache', '%s has desktop files that claim types (%s) but no mimeinfo.cache: GIO and xdg-open do '
                            'not see those claims (flxp does). Run: update-desktop-database %s' % (
                                e.short(d), ', '.join(sorted({i for v in claims.values() for i in v})), e.short(d))))
            continue
        missing = sorted({i for m, ids in claims.items() for i in ids if i not in c.get(m, [])})
        stale = sorted({i for ids in c.values() for i in ids if i not in db.dir_apps.get(d, [])})
        if missing or stale:
            out.append(('cache', '%s/mimeinfo.cache is out of date (%s%s%s). Run: update-desktop-database %s' % (
                e.short(d), 'not listed: ' + ', '.join(missing) if missing else '', '; ' if missing and stale else '',
                'listed but gone: ' + ', '.join(stale) if stale else '', e.short(d))))
    # 5. legacy and shadowing files
    for lf in db.lists:
        if lf.exists and lf.kind == 'legacy':
            out.append(('legacy', '%s: defaults.list is the deprecated name of mimeapps.list' % e.short(lf.path)))
        if lf.exists and lf.kind == 'mimeapps' and lf.path == os.path.join(e.data_home, 'applications', 'mimeapps.list'):
            out.append(('legacy', '%s: the old place of the user\'s file (now %s); its lines still count, after it' % (
                e.short(lf.path), e.short(db.user_file()))))
    user = next((l for l in db.lists if l.path == db.user_file()), None)
    for lf in db.lists:
        if lf.exists and lf.kind == 'desktop' and lf.layer == USER and user:
            for k in sorted(set(lf.default) & set(user.default)):
                if lf.default[k] != user.default[k]:
                    out.append(('shadow', '%s sets %s=%s over %s\'s %s, which flxp, lhde-mime default and GLib write' % (
                        e.short(lf.path), k, ';'.join(lf.default[k]), e.short(user.path), ';'.join(user.default[k]))))
    for id_, a in sorted(db.apps.items()):
        for s in a.shadowed:
            out.append(('shadowed', '%s: %s hides %s' % (id_, e.short(a.path), e.short(s))))
    if not any(os.path.isfile(os.path.join(d, 'lhde-mimeapps.list')) for d in e.app_dirs() + [e.config_home] + e.config_dirs):
        out.append(('lhde', 'no lhde-mimeapps.list in the data directories: lhde\'s defaults are not installed (make install)'))
    elif 'lhde' not in e.desktops:
        out.append(('lhde', 'XDG_CURRENT_DESKTOP is %r: lhde-mimeapps.list is read only when it names lhde (lhde-session '
                    'sets XDG_CURRENT_DESKTOP=lhde)' % ':'.join(e.desktops) if e.desktops else
                    'XDG_CURRENT_DESKTOP is not set: lhde-mimeapps.list is read only when it names lhde (lhde-session sets '
                    'XDG_CURRENT_DESKTOP=lhde)'))
    return out
