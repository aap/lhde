"""Classic Appearance schemes from the user's own Windows install media (docs/THEME-FORMAT.md section 2a).

Windows 2000, XP and Server 2003 set up the default user's "Control Panel\\Appearance\\Schemes" from HIVEDEF.INF on the
install medium (I386/ or AMD64/): one REG_BINARY value per scheme, Windows' SCHEMEDATA (version 2): a SHORT version, two
bytes of padding, NONCLIENTMETRICSW (500 bytes, without iPaddedBorderWidth; its cbSize is often garbage), the icon title LOGFONTW, then the COLORREFs
in COLOR_ index order. The names are [Strings] entries (%WINDOWS_STANDARD% = "Windows Standard"). Each scheme becomes
styles/NAME.scheme in lhde's data directory; "NAME (large)" and "NAME (extra large)" become the [Metrics:Large Fonts] and
[Metrics:Extra Large Fonts] sections of NAME's file (XP's "Font size"), or a scheme of their own when there is no NAME.
Nothing of Microsoft's is in the repository: this reads the user's medium."""
import os
import re
import struct

COLORS = ['scrollbar', 'background', 'activecaption', 'inactivecaption', 'menu', 'window', 'windowframe', 'menutext',
          'windowtext', 'captiontext', 'activeborder', 'inactiveborder', 'appworkspace', 'highlight', 'highlighttext', 'btnface',
          'btnshadow', 'graytext', 'btntext', 'inactivecaptiontext', 'btnhighlight', '3ddkshadow', '3dlight', 'infotext', 'infobk',
          'alternatebtnface', 'hotlight', 'gradientactivecaption', 'gradientinactivecaption', 'menuhilight', 'menubar']
SIZES = [(' (extra large)', 'Extra Large Fonts'), (' (large)', 'Large Fonts')]


def read_text(path):
    d = open(path, 'rb').read()
    if d[:2] in (b'\xff\xfe', b'\xfe\xff'):
        return d.decode('utf-16')
    try:
        return d.decode('utf-8')
    except UnicodeDecodeError:
        return d.decode('latin-1')


def inf_values(text):
    """the Appearance\\Schemes values of a hive INF: [(name, bytes)] with the %STRINGS% resolved"""
    lines, cur = [], ''
    for raw in text.splitlines():
        s = raw.rstrip()
        if s.endswith('\\'):
            cur += s[:-1]
            continue
        lines.append(cur + s)
        cur = ''
    strings, sec = {}, ''
    for l in lines:
        m = re.match(r'\s*\[([^\]]+)\]', l)
        if m:
            sec = m.group(1).strip().lower()
            continue
        if sec == 'strings':
            m = re.match(r'\s*([^=;\s]+)\s*=\s*"?([^"]*)"?', l)
            if m:
                strings[m.group(1).lower()] = m.group(2)
    out = []
    for l in lines:
        m = re.match(r'\s*HKCU\s*,\s*"Control Panel\\Appearance\\Schemes"\s*,\s*"([^"]+)"\s*,\s*(0x[0-9a-fA-F]+|\d+)\s*,(.*)$', l)
        if not m:
            continue
        name = m.group(1)
        if name.startswith('%') and name.endswith('%'):
            name = strings.get(name[1:-1].lower(), name[1:-1])
        hexes = re.findall(r'[0-9a-fA-F]{2}', m.group(3))
        out.append((name, bytes(int(h, 16) for h in hexes)))
    return out


def logfont(d, o):
    h, w, esc, ori, weight = struct.unpack_from('<iiiii', d, o)
    italic = d[o + 20]
    face = d[o + 28:o + 92].decode('utf-16le', 'replace').split('\0')[0]
    s = '%s, %d' % (face, h)   # LOGFONT's lfHeight: negative = pixels (the form ut-dump prints), positive = points
    if weight >= 600:
        s += ', bold'
    if italic:
        s += ', italic'
    return s


def decode(data):
    """SCHEMEDATA -> (colors {name: 'r g b'}, metrics {name: value}); None when it is no version 2 scheme"""
    if len(data) < 596:
        return None
    ver, = struct.unpack_from('<h', data, 0)
    if ver != 2:   # NONCLIENTMETRICS' cbSize is not reliable here (most of the shipped values hold 3 or 0x46)
        return None
    n = 8
    border, sw, sh, cw, ch = struct.unpack_from('<iiiii', data, n)
    m = {'borderwidth': border, 'scrollwidth': sw, 'scrollheight': sh, 'captionwidth': cw, 'captionheight': ch}
    n += 20
    m['captionfont'] = logfont(data, n); n += 92
    m['smcaptionwidth'], m['smcaptionheight'] = struct.unpack_from('<ii', data, n); n += 8
    m['smcaptionfont'] = logfont(data, n); n += 92
    m['menuwidth'], m['menuheight'] = struct.unpack_from('<ii', data, n); n += 8
    m['menufont'] = logfont(data, n); n += 92
    m['statusfont'] = logfont(data, n); n += 92
    m['messagefont'] = logfont(data, n); n += 92
    m['icontitlefont'] = logfont(data, n); n += 92
    c = {}
    i = 0
    while n + 4 <= len(data) and i < len(COLORS):
        v, = struct.unpack_from('<I', data, n)
        c[COLORS[i]] = '%d %d %d' % (v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff)
        n += 4
        i += 1
    return c, m


def file_name(title):
    s = re.sub(r'[^a-z0-9]+', '-', title.lower()).strip('-')
    return s or 'scheme'


METRIC_ORDER = ['borderwidth', 'scrollwidth', 'scrollheight', 'captionwidth', 'captionheight', 'smcaptionwidth',
                'smcaptionheight', 'menuwidth', 'menuheight', 'captionfont', 'smcaptionfont', 'menufont', 'statusfont',
                'messagefont', 'icontitlefont']


def schemes(values):
    """group the values: {title: {'Normal': (c, m), 'Large Fonts': ..., ...}}"""
    dec = [(n, decode(d)) for n, d in values]
    dec = [(n, x) for n, x in dec if x]
    names = {n for n, _ in dec}
    out = {}
    for n, x in dec:
        base, size = n, 'Normal'
        for suf, sz in SIZES:
            if n.lower().endswith(suf) and n[:-len(suf)] in names:
                base, size = n[:-len(suf)], sz
                break
        out.setdefault(base, {})[size] = x
    return out


def scheme_text(title, sizes, origin):
    order = [s for s in ('Normal', 'Large Fonts', 'Extra Large Fonts') if s in sizes]
    c0, m0 = sizes[order[0]]
    L = ['; a classic Appearance scheme (docs/THEME-FORMAT.md section 2a), imported by lhde-schemes from %s' % origin,
         # lhde's ini starts a comment at '#' after whitespace: "High Contrast #1" keeps its look with a no-break space
         '[Scheme]', 'name = %s' % re.sub(r'\s#', '\u00a0#', title), 'sizes = %s' % ', '.join(order), '', '[Colors]']
    L += ['%s = %s' % (k, c0[k]) for k in COLORS if k in c0]
    L += ['', '[Metrics]']
    L += ['%s = %s' % (k, m0[k]) for k in METRIC_ORDER]
    for s in order[1:]:
        c, m = sizes[s]
        L += ['', '[Metrics:%s]' % s]
        L += ['%s = %s' % (k, m[k]) for k in METRIC_ORDER]
        diff = [k for k in COLORS if c.get(k) != c0.get(k)]
        if diff:
            L += ['', '[Colors:%s]' % s]
            L += ['%s = %s' % (k, c[k]) for k in diff]
    return '\n'.join(L) + '\n'


def import_inf(inf, styles_dir, force=False, origin=None, log=print):
    """every scheme of a hive INF into styles_dir/NAME.scheme; returns (written, kept)"""
    vals = inf_values(read_text(inf))
    groups = schemes(vals)
    os.makedirs(styles_dir, exist_ok=True)
    written = kept = 0
    for title in sorted(groups):
        p = os.path.join(styles_dir, file_name(title) + '.scheme')
        if os.path.exists(p) and not force:
            kept += 1
            continue
        with open(p + '.tmp', 'w') as f:
            f.write(scheme_text(title, groups[title], origin or os.path.basename(inf)))
        os.replace(p + '.tmp', p)
        written += 1
    log('  schemes: %d written, %d kept (%s)' % (written, kept, styles_dir))
    return written, kept
