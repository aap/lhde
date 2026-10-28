#!/usr/bin/env python3
r"""msstyles2theme: turn a visual style into an lhwm "classic msstyles" theme directory (theme.ini + one PNG per part/state).

Offline cache: the WM reads styles live when libuxtheme is present (lhwm README "libuxtheme source": theme.ini's msstyles /
color / size, written here, name the style; frame_ut.c draws its Window parts through the library, and the PNGs below are then
only the fallback for an lhwm built without the library, FRAMETEST_SOURCE=dir, and tests/regress.sh --compare-sources). Tuning
keys (text_antialias, text_hinting, text_dy, font_files, default_icon, shape_alpha) are read from the directory either way.

usage: msstyles2theme.py STYLE.ini BITMAPDIR OUTDIR [--font-fallback FAMILY]
       msstyles2theme.py STYLE.msstyles OUTDIR [--color SCHEME] [--size SIZE] [--font-fallback FAMILY] [--font-file TTF]...

The first form reads a decompiled style (*_longhorn.ini from tools/lhstyle.py + a directory of BITMAP_<id>_1033.bmp). The
second reads the PE file itself, in either format:
  - XP (PACKTHEM_VERSION 3, e.g. XP's luna.msstyles): THEMES_INI's [File.*] sections map colour scheme + size to a
    TEXTFILE resource (NORMALBLUE_INI ...); bitmaps are BITMAP resources named after the file (Blue\FrameCaption.bmp ->
    BLUE_FRAMECAPTION_BMP); XP's alpha rule (0/255-only alpha is ignored);
  - Longhorn-compiled (PACKTHEM_VERSION 4: the 4051/4074 Luna, Slate, Jade): VMAP maps variant -> size + colour, the
    VARIANT resource is decompiled with the style's own STBL01 schema (as tools/lhstyle.py does), bitmaps by id.
--color/--size default to the first scheme / NormalSize (Luna: NormalColor, Metallic, HomeStead; NormalSize, LargeFonts,
ExtraLargeFonts). --text-antialias subpixel|gray|none (default subpixel: Longhorn's ClearType; XP's default "Standard" font
smoothing draws the Luna captions bi-level: none) and --text-hinting set those theme.ini keys. --font-file adds TTFs to theme.ini's font_files (lhwm loads them as application fonts: Luna's caption
font Trebuchet MS is not installed here); without one, Fonts/<file> next to the style's WINDOWS directory is tried for
the caption font family.

Everything numeric is copied straight from the style: [sysmetrics] CAPTIONBARHEIGHT, SMCAPTIONBARHEIGHT,
SIZINGBORDERWIDTH (system default 1 when absent), CAPTIONFONT, CAPTIONTEXT/INACTIVECAPTIONTEXT and the
Window.<PART> sections (IMAGECOUNT, IMAGELAYOUT, SIZINGMARGINS, SIZINGTYPE, CAPTIONMARGINS, CONTENTALIGNMENT,
TEXTSHADOW*), with uxtheme's lookup order state section -> part section -> [Window] -> [globals].

Bitmaps are RT_BITMAP DIBs with a BMP file header. uxtheme's rules (imagefile.cpp): a 32-bit image with any
alpha value outside {0,255} is premultiplied ARGB (un-premultiplied here, PNG is straight alpha); otherwise its
alpha is used as a 1-bit mask when it holds both 0 and 255 (Longhorn; XP ignored it) and otherwise, when the part is TRANSPARENT, TRANSPARENTCOLOR (default magenta) becomes transparent.
"""
import io
import os
import re
import struct
import sys
from pathlib import Path
from PIL import Image

PARTS = {  # lhwm name: (ini part, states)
    'caption': ('CAPTION', ['ACTIVE', 'INACTIVE']),
    'maxcaption': ('MAXCAPTION', ['ACTIVE', 'INACTIVE']),
    'smallcaption': ('SMALLCAPTION', ['ACTIVE', 'INACTIVE']),
    'frameleft': ('FRAMELEFT', ['ACTIVE', 'INACTIVE']),
    'frameright': ('FRAMERIGHT', ['ACTIVE', 'INACTIVE']),
    'framebottom': ('FRAMEBOTTOM', ['ACTIVE', 'INACTIVE']),
    'close': ('CLOSEBUTTON', None),
    'max': ('MAXBUTTON', None),
    'min': ('MINBUTTON', None),
    'restore': ('RESTOREBUTTON', None),
    'help': ('HELPBUTTON', None),
    'smallclose': ('SMALLCLOSEBUTTON', None),
}


def parse_ini(path, text=None, xp=False):
    secs, cur = {}, None
    for line in (text if text is not None else Path(path).read_bytes().decode('latin-1')).splitlines():
        line = line.strip()
        if xp:   # XP source syntax: "; comment" anywhere, comma-separated lists ("28, 35, 9, 17"; fonts keep theirs)
            line = line.split(';', 1)[0].strip()
        m = re.match(r'^\[(.+)\]$', line)
        if m:
            cur = secs.setdefault(m.group(1).upper(), {})
            continue
        if cur is not None and '=' in line:
            k, v = line.split('=', 1)
            k, v = k.strip().upper(), v.strip()
            if xp and not k.endswith('FONT'):
                v = ' '.join(v.replace(',', ' ').split())
            # a property given twice: the first one counts (XP uxtheme render.cpp GetValueIndex returns the first match of the
            # packed list, which the parser fills in source order; Wine's MSSTYLES_AddProperty keeps the first; libuxtheme too).
            # Luna's [Window.MaxCaption] says SizingType = stretch, then Tile: stretch
            cur.setdefault(k, v)
    return secs


class Style:
    def __init__(self, path=None, sections=None):
        self.s = sections if sections is not None else parse_ini(path)

    def get(self, part, key, state=None, default=None):
        chain = ([f'WINDOW.{part}({state})'] if state else []) + [f'WINDOW.{part}', 'WINDOW', 'GLOBALS']
        for sec in chain:
            if key in self.s.get(sec, {}):
                return self.s[sec][key]
        return default

    def own(self, part, key, default=None):  # part section only (image properties are not inherited usefully)
        return self.s.get(f'WINDOW.{part}', {}).get(key, default)

    def sysm(self, key, default=None):
        return self.s.get('SYSMETRICS', {}).get(key, default)


def bmp_file(d):
    """an RT_BITMAP resource (a DIB without file header) as BMP file bytes"""
    if d[:2] == b'BM':
        return d
    hsz, w, h, planes, bpp = struct.unpack_from('<IiiHH', d, 0)
    comp, clr = struct.unpack_from('<I', d, 16)[0], (struct.unpack_from('<I', d, 32)[0] if hsz >= 36 else 0)
    pal = (clr or (1 << bpp if bpp <= 8 else 0)) * (3 if hsz == 12 else 4) + (12 if comp == 3 and hsz == 40 else 0)
    return b'BM' + struct.pack('<IHHI', 14 + len(d), 0, 0, 14 + hsz + pal) + d


def load_dib(path, transparent, tcolor=(255, 0, 255), data=None, xp_alpha=False):
    d = bmp_file(data if data is not None else Path(path).read_bytes())
    path = io.BytesIO(d)
    d = d[14:]
    hsz, w, h, planes, bpp = struct.unpack_from('<IiiHH', d, 0)
    comp, = struct.unpack_from('<I', d, 16)
    if bpp != 32 or comp not in (0, 3):  # let PIL handle palette / 24-bit images
        im = Image.open(path).convert('RGBA')
    else:
        off = hsz + (12 if comp == 3 and hsz == 40 else 0)
        stride = w * 4
        raw = d[off:off + stride * abs(h)]
        rows = [raw[y * stride:(y + 1) * stride] for y in range(abs(h))]
        if h > 0:
            rows.reverse()
        buf = b''.join(rows)
        alpha = buf[3::4]
        partial = any(0 < a < 255 for a in alpha)
        # Longhorn's uxtheme also honours 0/255-only alpha (the 4074 glyph strips are black where alpha is 0 and
        # contain no magenta; the caption's rounded corners are alpha 0), unlike XP which ignored it
        binary = not partial and 0 in alpha and 255 in alpha and not xp_alpha   # XP (imagefile.cpp): such alpha is ignored
        im = Image.frombuffer('RGBA', (w, abs(h)), buf, 'raw', 'BGRA', 0, 1).copy()
        px = im.load()
        for y in range(im.height):
            for x in range(im.width):
                r, g, b, a = px[x, y]
                if partial and xp_alpha:   # XP files hold straight alpha (rgb > alpha occurs); uxtheme premultiplies on load
                    pass
                elif partial:   # premultiplied (Longhorn-compiled): undo
                    px[x, y] = (0, 0, 0, 0) if a == 0 else (min(255, (r * 255 + a // 2) // a), min(255, (g * 255 + a // 2) // a), min(255, (b * 255 + a // 2) // a), a)
                elif not binary:
                    px[x, y] = (r, g, b, 255)
        if partial or binary:
            return im
    if transparent:
        px = im.load()
        for y in range(im.height):
            for x in range(im.width):
                if px[x, y][:3] == tcolor:
                    px[x, y] = (0, 0, 0, 0)
    return im


def split(im, count, vertical):
    if vertical:
        h = im.height // count
        return [im.crop((0, i * h, im.width, (i + 1) * h)) for i in range(count)]
    w = im.width // count
    return [im.crop((i * w, 0, (i + 1) * w, im.height)) for i in range(count)]


def rgb_hex(v):
    r, g, b = (int(x) for x in v.split()[:3])
    return f'#{r:02x}{g:02x}{b:02x}'


def font_spec(v, fallback):
    # "Tahoma Bold, 8[, Quality:...]" -> pango "Tahoma,FALLBACK Bold 11px" (Windows: lfHeight = -MulDiv(pt, 96, 72))
    parts = [p.strip() for p in v.split(',')]
    name, pt = parts[0], int(parts[1])
    style = ''
    for st in ('Bold', 'Italic'):
        if name.endswith(' ' + st):
            name, style = name[:-len(st) - 1], (style + ' ' + st).strip()
    for p in parts[2:]:   # XP syntax: "Trebuchet MS, 10, bold[, italic]"
        if p.lower() in ('bold', 'italic') and p.capitalize() not in style:
            style = (style + ' ' + p.capitalize()).strip()
    px = (pt * 96 + 36) // 72
    fam = name + (',' + fallback if fallback and fallback != name else '')
    quality = next((p.split(':', 1)[1] for p in parts[2:] if p.lower().startswith('quality:')), '')
    return f'{fam} {style} {px}px'.replace('  ', ' '), quality


class PEStyle:
    """a .msstyles PE file: the chosen variant's properties as ini sections, and its bitmaps"""

    def __init__(self, path, color=None, size=None):
        sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'themes' / 'tools'))
        from pe_resources import PEResources
        self.pe = PEResources(path)
        self.path = Path(path)
        self.version = struct.unpack('<H', self.pe.get('PACKTHEM_VERSION', 1)[:2])[0]
        if self.version >= 4:
            self.v4(color, size)
        else:
            self.v3(color, size)

    def pick(self, triples, color, size):
        # triples: (variant, size, colour); default: the first colour scheme in NormalSize (Normal for Longhorn styles)
        for v, sz, co in triples:
            if (color is None or co.lower() == color.lower()) and (size is None or sz.lower() == size.lower()):
                if size is None and sz.lower() not in ('normalsize', 'normal'):
                    continue
                return v, sz, co
        raise SystemExit(f'{self.path}: no variant for color={color} size={size}; have ' +
                         ', '.join(f'{co}/{sz}' for _, sz, co in triples))

    def v3(self, color, size):   # XP: THEMES_INI [File.<name>] -> ColorSchemes/Sizes, TEXTFILE <NAME>_INI
        themes = parse_ini(None, self.pe.get('TEXTFILE', 'THEMES_INI').decode('utf-16le'), xp=True)
        triples = []
        for sec, kv in themes.items():
            if sec.startswith('FILE.'):
                for co in kv.get('COLORSCHEMES', '').split():
                    for sz in kv.get('SIZES', '').split():
                        triples.append((sec[5:], sz, co))
        name, self.size, self.color = self.pick(triples, color, size)
        self.variant = name
        self.sections = parse_ini(None, self.pe.get('TEXTFILE', name.upper() + '_INI').decode('utf-16le'), xp=True)
        self.xp_alpha = True

    def v4(self, color, size):   # Longhorn-compiled: VMAP (variant, size, colour) triples, VARIANT decompiled with STBL01
        words = re.findall(r'[A-Za-z][A-Za-z0-9]+', self.pe.get('VMAP', 'VMAP').decode('utf-16le'))
        triples = [tuple(words[i:i + 3]) for i in range(0, len(words) - 2, 3)]
        name, self.size, self.color = self.pick(triples, color, size)
        self.variant = name
        self.sections = parse_ini(None, decompile_v4(self.pe, name.upper()))
        self.xp_alpha = False

    def bitmap(self, ref):
        if self.version >= 4:   # "bitmap#<id>"
            return self.pe.get(2, int(ref.split('#')[1]))
        n = re.sub(r'[\\/.]', '_', ref.strip()).upper()   # Blue\FrameCaption.bmp -> BLUE_FRAMECAPTION_BMP
        return self.pe.get(2, n)


def decompile_v4(pe, variant):
    """the VARIANT resource as ini text, like tools/lhstyle.py (same schema walk, same value formatting)"""
    stbl = pe.get('STBL', 'STBL01').decode('latin1')
    props, enums, parts = {}, {}, {}
    for line in stbl.splitlines():
        f = line.split(',')
        if len(f) < 5:
            continue
        name, enum, typ, pid = f[1], f[2], f[3], f[4]
        if typ == 'ENUMVAL':
            enums.setdefault(enum, {})[int(pid)] = name
            if enum.endswith('PARTS'):
                parts.setdefault(enum[:-5].upper(), {})[int(pid)] = name
        elif typ != 'ENUMDEF' and enum == '':
            try:
                props[int(pid)] = (name, typ)
            except ValueError:
                pass
    cmap = [x for x in pe.get('CMAP', 'CMAP').decode('utf-16le').split('\0') if x]
    strings = {}
    for (k, blk, _), d in pe.resources.items():
        if k != 6:
            continue
        o = 0
        for i in range(16):
            n = struct.unpack_from('<H', d, o)[0]
            o += 2
            if n:
                strings[16 * (blk - 1) + i] = d[o:o + 2 * n].decode('utf-16le')
            o += 2 * n

    def states_for(cls, part):
        pn = parts.get(cls, {}).get(part, '')
        for key in (pn + 'STATES', cls + 'STATES'):
            for k in enums:
                if k.upper() == key.upper():
                    return enums[k]
        return {}

    def val(typ, short, payload, name):
        num = short if payload == b'' else (struct.unpack_from('<i', payload)[0] if len(payload) >= 4 else 0)
        if typ in ('INT', 'BOOL', 'ENUM', 'SIZE'):
            return ('TRUE' if num else 'FALSE') if typ == 'BOOL' else enums.get(name, {}).get(num, str(num)) if typ == 'ENUM' else str(num)
        if typ == 'COLOR':
            v = num & 0xFFFFFFFF
            return f'{v & 255} {(v >> 8) & 255} {(v >> 16) & 255}'
        if typ in ('MARGINS', 'RECT'):
            return ' '.join(str(x) for x in struct.unpack_from('<4i', payload))
        if typ == 'POSITION':
            return ' '.join(str(x) for x in struct.unpack_from('<2i', payload))
        if typ in ('STRING', 'FONT'):
            return payload.decode('utf-16le', 'replace').rstrip('\0') if payload else strings.get(short, f'str#{short}')
        if typ == 'FILENAME':
            return f'bitmap#{num}'
        return payload.hex()[:40]

    data = pe.get('VARIANT', variant)
    out, last, off = [], None, 0
    while off < len(data):
        name, typ, cls, part, state, short, res, size = struct.unpack_from('<8I', data, off)
        payload = data[off + 32: off + 32 + size] if short == 0 else b''
        off += 32 if short else (32 + size + 7) & ~7
        cname = cmap[cls] if cls < len(cmap) else f'cls{cls}'
        base = cname.split('::')[-1].upper()
        pname = parts.get(base, {}).get(part, str(part)) if part else ''
        sname = states_for(base, part).get(state, str(state)) if state else ''
        hdr = cname + ('.' + pname if part else '') + ('(' + sname + ')' if state else '')
        if hdr != last:
            out.append(f'[{hdr}]')
            last = hdr
        pn, pt = props.get(name, (f'prop{name}', f'type{typ}'))
        try:
            out.append(f'{pn} = {val(pt, short, payload, pn)}')
        except Exception:
            pass
    return '\n'.join(out) + '\n'


def main():
    args = sys.argv[1:]
    opts = {'--font-fallback': 'Tahoma', '--color': None, '--size': None, '--text-antialias': 'subpixel', '--text-hinting': None}
    font_files = []
    for o in list(opts) + ['--font-file']:
        while o in args:
            i = args.index(o)
            if o == '--font-file':
                font_files.append(str(Path(args[i + 1]).resolve()))
            else:
                opts[o] = args[i + 1]
            del args[i:i + 2]
    fallback = opts['--font-fallback']
    pe = None
    if len(args) == 2 and args[0].lower().endswith('.msstyles'):
        ini, out = args
        pe = PEStyle(ini, opts['--color'], opts['--size'])
        st = Style(sections=pe.sections)
        src = f'{ini} ({pe.variant}: color {pe.color}, size {pe.size}, PACKTHEM_VERSION {pe.version})'
    else:
        ini, bmpdir, out = args
        st = Style(ini)
        src = ini
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob('*.png'):
        old.unlink()

    def bmp(ref):
        if pe:
            return ref
        n = int(ref.split('#')[1])
        return Path(bmpdir) / f'BITMAP_{n}_1033.bmp'

    def exists(ref):
        if not pe:
            return bmp(ref).exists()
        try:
            pe.bitmap(ref)
            return True
        except KeyError:
            return False

    def load(ref, transparent, tcol):
        if pe:
            return load_dib(None, transparent, tcol, data=pe.bitmap(ref), xp_alpha=pe.xp_alpha)
        return load_dib(bmp(ref), transparent, tcol)

    font, quality = font_spec(st.sysm('CAPTIONFONT', 'Tahoma Bold, 8'), fallback)
    sfont, _ = font_spec(st.sysm('SMALLCAPTIONFONT', 'Tahoma, 8'), fallback)
    if pe and not font_files:   # the caption font from the style's own Windows directory, when it is not installed here
        family = st.sysm('CAPTIONFONT', '').split(',')[0].strip()
        known = {'trebuchet ms': ['trebuc.ttf', 'trebucbd.ttf'], 'franklin gothic medium': ['framd.ttf']}.get(family.lower(), [])
        for f in known:
            cand = pe.path.resolve().parents[3] / 'Fonts' / f   # WINDOWS/Resources/Themes/<name>/x.msstyles -> WINDOWS/Fonts
            if cand.exists():
                font_files.append(str(cand))
    L = [f'# lhwm classic msstyles theme, generated by tools/msstyles2theme.py from {src}',
         'mode = msstyles',
         ]
    if pe:   # the style this directory caches: lhwm reads it live through libuxtheme when it has the library
        L += [f'msstyles = {os.path.abspath(ini)}', f'color = {pe.color}', f'size = {pe.size}']
    L += [
         f'border_width = {st.sysm("SIZINGBORDERWIDTH", "1")}',
         f'caption_height = {st.sysm("CAPTIONBARHEIGHT", "18")}',
         f'small_caption_height = {st.sysm("SMCAPTIONBARHEIGHT", "15")}',
         f'caption_font = {font}',
         f'small_caption_font = {sfont}',
         f'font_quality = {quality.lower() or "default"}',
         # 4074 renders captions with ClearType; Quality:ClearType-Natural (Jade) has no grid-fitting (closest: unhinted).
         # XP's default is "Standard" smoothing, which leaves small captions bi-level: --text-antialias none for Luna
         f'text_antialias = {opts["--text-antialias"]}',
         f'text_hinting = {opts["--text-hinting"] or ("none" if "natural" in quality.lower() else "full")}',
         f'caption_text = {rgb_hex(st.sysm("CAPTIONTEXT", "255 255 255"))}',
         f'caption_text_inactive = {rgb_hex(st.sysm("INACTIVECAPTIONTEXT", "0 0 0"))}']
    if font_files:
        L.append(f'font_files = {" ".join(font_files)}')
    # the default window icon for clients without one (XP draws the application icon, user32/shell32 #3): luxshell's shell32 pack images
    L.append('default_icon = packs/4074/shell32/16/3.png packs/4074/shell32/32/3.png')   # lhde: relative to the data directories
    for name, (part, states) in PARTS.items():
        if f'WINDOW.{part}' not in st.s:
            continue
        count = int(st.own(part, 'IMAGECOUNT', '1'))
        vertical = st.own(part, 'IMAGELAYOUT', 'HORIZONTAL').upper() == 'VERTICAL'
        transparent = st.get(part, 'TRANSPARENT', default='FALSE').upper() == 'TRUE'
        tcol = tuple(int(x) for x in st.get(part, 'TRANSPARENTCOLOR', default='255 0 255').split())
        files = [('', st.own(part, 'IMAGEFILE'))] + [(f'@{k}', st.own(part, f'IMAGEFILE{k}')) for k in range(1, 6)]
        variants = []
        for suffix, ref in files:
            if not ref or not exists(ref):
                continue
            imgs = split(load(ref, transparent, tcol), count, vertical)
            for i, im in enumerate(imgs, 1):
                im.save(out / f'{name}{suffix}_{i}.png')
            variants.append(f'{suffix or "@0"}:{imgs[0].width}x{imgs[0].height}')
        L.append(f'{name}.images = {count}')
        L.append(f'{name}.variants = {" ".join(variants)}')
        sm = st.own(part, 'SIZINGMARGINS', '0 0 0 0')
        L.append(f'{name}.sizing_margins = {sm}')
        L.append(f'{name}.sizing = {st.get(part, "SIZINGTYPE", default="STRETCH").lower()}')
        L.append(f'{name}.transparent = {int(transparent)}')
        cn = st.own(part, 'CONTENTMARGINS')
        if cn:
            L.append(f'{name}.content_margins = {cn}')
        cm = st.get(part, 'CAPTIONMARGINS')
        if cm:
            L.append(f'{name}.caption_margins = {cm}')
        ca = st.get(part, 'CONTENTALIGNMENT')
        if ca:
            L.append(f'{name}.content_alignment = {ca.lower()}')
        for si, state in enumerate(states if 'caption' in name else [], 1):
            off = st.get(part, 'TEXTSHADOWOFFSET', state)
            typ = (st.get(part, 'TEXTSHADOWTYPE', state, 'NONE') if off else 'NONE').lower()
            col = rgb_hex(st.get(part, 'TEXTSHADOWCOLOR', state, '0 0 0'))
            L.append(f'{name}.shadow.{si} = {typ} {off or "0 0"} {col}')
    (out / 'theme.ini').write_text('\n'.join(L) + '\n')
    print(f'{out}: {len(list(out.glob("*.png")))} images')


if __name__ == '__main__':
    main()
