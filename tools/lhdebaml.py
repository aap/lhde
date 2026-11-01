#!/usr/bin/env python3
"""Longhorn's binary XAML (BAML) back to XAML text, and the .NET manifest resources that carry it.

usage: lhdebaml.py FILE.baml                      the XAML of a BAML stream (either dialect, told apart by the first record)
       lhdebaml.py --list ASSEMBLY.dll            the entries of an assembly's .resources blobs (name, type, size)
       lhdebaml.py --resource ASSEMBLY.dll NAME   the XAML of the BAML entry NAME (e.g. xaml/sidebar/desktopaurora.baml)

Two dialects, as two decoders (lhde-import's aurora recipe, tools/lhdeaurora.py, uses both):

decode_2003  the early-2003 Avalon layout (assembly System.Windows, before PresentationFramework): build 4066/4074/4082
             jade.msstyles PreviewPane.AURORA (STREAM 479 in 4066, 481 in 4074). Every record of 4074's stream decodes.
               record = u32 size (whole record, header included) | u32 0 | u16 type | payload
               0x01 DocumentStart     ff ff ff ff 00 ff ff ff ff
               0x02 DocumentEnd
               0x03 ElementStart      u16 depth | i32 back-ref | i32 back-ref | u16 0 | u16 typeId | u32 0 | i32 back-ref
               0x04 ElementEnd
               0x06 xmlns / using     u8 0 | str  ("using:System;System.Windows;...#System.Windows.Desktop")
               0x07 Property          u16 attrId | str value | u8 0
               0x0b AssemblyInfo      u16 asmId | str
               0x0c TypeInfo          u16 typeId | u16 asmId | str full type name
               0x0d AttributeInfo     u16 attrId | u16 ownerTypeId | str name
               0x0e PropComplexStart  u16 attrId                    0x0f PropComplexEnd
               0x10 ObjectStart       u16 0xffff | i32 | i32 | u16 0 | u16 typeId   (an object inside a complex property)
               0x11 ObjectEnd
               0x12 NamedProperty     str name | str value | u16 0xffff   (the properties of 0x10 objects, stored by name)
               0x15 NamedComplexStart str name (TranslateTransform.X holding a FloatAnimation, LinearGradient.GradientTransform)
               0x16 NamedComplexEnd
               0x18 PropertyCustom    u16 attrId | typed bytes: a length (below), or an enum as u32 (Stretch: 0 None, 1 Fill,
                                      2 Uniform, 3 UniformToFill)
             The type id of an ElementStart is the u16 at payload offset 12; the first u16 is the nesting depth.

decode_2004  the 2004 WCP layout (PresentationFramework / PresentationCore, MSAvalon.* in 4066, System.Windows.* in 4074 and
             4093): Microsoft.Windows.WCPClient.dll's xaml/*.baml (4093: ms/internal/...). The stream starts with u32 total
             size (unless it starts with a DocumentStart), then records of u16 id | payload, the payload of fixed size for
             the ids below or else u32 size (the size field included) | payload:
               fixed: 0x02 DocumentEnd (0), 0x03 ElementStart (i16 typeId), 0x04 ElementEnd (0), 0x07/0x09/0x0b/0x0d
                      PropertyComplex / Array / IList / IDictionary start (i16 attrId), 0x08/0x0a/0x0c/0x0e their ends (0)
               0x01 DocumentStart     u32 n | n bytes UTF-16 "PreAlpha" | three versions u16 major, u16 minor | ...
               0x05 Property          i16 attrId | str
               0x06 PropertyCustom    i16 attrId | typed bytes: an enum as i32, a bool as one byte (4093), 00 + str, a
                                      length (below), a solid colour 01 + B G R A, a point list (01|04 + bytes)
               0x10 Text              str
               0x13 xmlns             str prefix | str namespace
               0x19 DefAttribute      str value | str name  (def:Name="...")
               0x1c PIMapping         str xmlns | str CLR namespace | i16 asmId
               the declarations, by the DocumentStart's version (IDS_2004): 0.0 (4066, 4074) / 0.1 (4093)
               0x1d / 0x1e AssemblyInfo   i16 asmId | str
               0x1e / 0x1f TypeInfo       i16 typeId | i16 asmId | str full type name
               0x20 / 0x21 AttributeInfo  i16 attrId | i16 ownerTypeId | (0.1: u8 usage |) str name
             Negative type ids are the built-in types (KNOWN_2004). An attribute declared on a type other than the element
             it is first used on (and not on one of the base classes) is written owner-qualified: Canvas.Left,
             Timeline.Duration. Version 0.1 writes lengths as strings ("800", "1pt") and animations without their
             DoubleAnimationCollection.

A length (both dialects) is a tag byte and a value: bit 7 set; bits 5-6 the value's size (00 one unsigned byte, 10 an int16,
11 a float64, all little-endian); bits 0-4 the unit (2003: 6 = %; 2004: 0 Auto, 1 %, 2 pixels, 3 star). So 8164 is 100%,
c22003 is 800, c2e2ff is -30, e2555555555555f53f is 1.3333333333333333 (StrokeThickness 1 pt at 96 dpi).

str is a 7-bit-length-prefixed UTF-8 string throughout.

net_resources reads an assembly's manifest resources without the metadata tables: the CLI header's Resources directory is a
sequence of u32 length | blob, 8-byte aligned; each .resources blob (magic 0xBEEFCACE, the ResourceReader format of version 1
or 2) holds named entries, and the BAML files are ByteArray entries.
"""
import struct
import sys


def s7(b, o, enc='utf-8'):
    """a 7-bit-length-prefixed string at o -> (str, offset after it)"""
    n = sh = 0
    while True:
        c = b[o]
        o += 1
        n |= (c & 0x7f) << sh
        sh += 7
        if not c & 0x80:
            break
    return b[o:o + n].decode(enc, 'replace'), o + n


def u7(b, o):
    n = sh = 0
    while True:
        c = b[o]
        o += 1
        n |= (c & 0x7f) << sh
        sh += 7
        if not c & 0x80:
            return n, o


def esc(s):
    return s.replace('&', '&amp;').replace('"', '&quot;').replace('<', '&lt;')


def length(d, units):
    """a typed length (tag byte + value) -> its XAML text, or None when d is not one"""
    if not d or not d[0] & 0x80:
        return None
    size = {0x00: ('<B', 1), 0x40: ('<h', 2), 0x60: ('<d', 8)}.get(d[0] & 0x60)
    unit = units.get(d[0] & 0x1f)
    if size is None or unit is None or len(d) != 1 + size[1]:
        return None
    v, = struct.unpack_from(size[0], d, 1)
    return unit % (repr(v) if isinstance(v, float) else str(v))


# ---------------------------------------------------------------- the 2003 dialect
STRETCH = ['None', 'Fill', 'Uniform', 'UniformToFill']
UNITS_2003 = {6: '%s%%'}


def records_2003(b):
    o = 0
    while o < len(b):
        size, zero, t = struct.unpack_from('<IIH', b, o)
        if size < 10 or o + size > len(b):
            raise ValueError('BAML 2003: bad record at %#x' % o)
        yield o, t, b[o + 10:o + size]
        o += size


def is_2003(b):
    return len(b) >= 10 and struct.unpack_from('<IIH', b, 0)[1:] == (0, 1)


def decode_2003(b):
    types, attrs = {}, {}
    stack, root, using = [], None, ''

    def custom(name, d):
        if name == 'Stretch' and len(d) == 4:
            return STRETCH[struct.unpack('<I', d)[0]]
        v = length(d, UNITS_2003)
        if v is None:
            raise ValueError('BAML 2003: %s: unknown value %s' % (name, d.hex()))
        return v

    def open_(tag, kind='el'):
        e = [tag, [], [], kind]
        if stack:
            stack[-1][2].append(e)
        stack.append(e)
        return e
    for o, t, p in records_2003(b):
        if t in (0x01, 0x02, 0x0b):
            pass
        elif t == 0x0c:
            tid, asm = struct.unpack_from('<HH', p)
            types[tid] = s7(p, 4)[0].split('.')[-1]
        elif t == 0x0d:
            aid, own = struct.unpack_from('<HH', p)
            attrs[aid] = (own, s7(p, 4)[0])
        elif t == 0x06:
            using = s7(p, 1)[0]
        elif t in (0x03, 0x10):
            tid, = struct.unpack_from('<H', p, 12)
            e = open_(types[tid])
            if root is None:
                root = e
                e[1].append(('xmlns', 'http://schemas.microsoft.com/2003/xaml'))
        elif t in (0x04, 0x11, 0x0f, 0x16):
            stack.pop()
        elif t == 0x07:
            aid, = struct.unpack_from('<H', p)
            stack[-1][1].append((attrs[aid][1], s7(p, 2)[0]))
        elif t == 0x18:
            aid, = struct.unpack_from('<H', p)
            stack[-1][1].append((attrs[aid][1], custom(attrs[aid][1], p[2:])))
        elif t == 0x12:
            n, q = s7(p, 0)
            v, q = s7(p, q)
            stack[-1][1].append((n, v))
        elif t == 0x0e:
            aid, = struct.unpack_from('<H', p)
            open_('%s.%s' % (stack[-1][0], attrs[aid][1]), 'prop')
        elif t == 0x15:
            open_('%s.%s' % (stack[-1][0], s7(p, 0)[0]), 'prop')
        else:
            raise ValueError('BAML 2003: unknown record %#x at %#x' % (t, o))
    out = []

    def emit(e, ind):
        a = ''.join(' %s="%s"' % (k, esc(v)) for k, v in e[1])
        if not e[2]:
            out.append('%s<%s%s/>' % (ind, e[0], a))
            return
        out.append('%s<%s%s>' % (ind, e[0], a))
        for c in e[2]:
            emit(c, ind + '  ')
        out.append('%s</%s>' % (ind, e[0]))
    out.append('<!-- BAML 2003, decoded by lhde tools/lhdebaml.py; %s -->' % using.replace('--', '-'))
    emit(root, '')
    return '\n'.join(out) + '\n'


# ---------------------------------------------------------------- the 2004 dialect
# the built-in (negative) type ids of the 2004 WCP BAML
KNOWN_2004 = {
    -35: 'Style', -34: 'Rectangle', -33: 'Path', -19: 'TextBox', -18: 'Text', -17: 'RadioButton', -16: 'MenuItem',
    -15: 'Menu', -14: 'ListBox', -12: 'Image', -11: 'HyperLink', -9: 'FlowPanel', -8: 'DockPanel', -5: 'ComboBox',
    -4: 'CheckBox', -3: 'Canvas', -2: 'Button', -1: 'Border',
}
FIXED_2004 = {0x02: 0, 0x03: 2, 0x04: 0, 0x07: 2, 0x08: 0, 0x09: 2, 0x0a: 0, 0x0b: 2, 0x0c: 0, 0x0d: 2, 0x0e: 0}
ENUMS_2004 = {'Dock': ['Left', 'Top', 'Right', 'Bottom', 'Fill'], 'VerticalAlignment': ['Top', 'Center', 'Bottom', 'Stretch'],
              'HorizontalAlignment': ['Left', 'Center', 'Right', 'Stretch'], 'Visibility': ['Visible', 'Hidden', 'Collapsed'],
              'Orientation': ['Horizontal', 'Vertical'], 'TextWrap': ['NoWrap', 'Wrap', 'WrapWithOverflow'],
              'TextTrimming': ['None', 'CharacterEllipsis', 'WordEllipsis'], 'Stretch': ['None', 'Fill', 'Uniform', 'UniformToFill']}
FONTWEIGHT = {400: 'Normal', 700: 'Bold', 300: 'Light'}
UNITS_2004 = {0: 'Auto', 1: '%s%%', 2: '%s', 3: '%s*'}
COLOURS = ('Background', 'Foreground', 'Fill', 'Stroke', 'BorderBrush', 'Color')
# the ids of the declaration records by the DocumentStart's versions: 0.0 (4066, 4074), 0.1 (4093: the AttributeInfo has a
# u8 more before its name)
IDS_2004 = {(0, 0): {'asm': 0x1d, 'type': 0x1e, 'attr': 0x20, 'attr_str': 4},
            (0, 1): {'asm': 0x1e, 'type': 0x1f, 'attr': 0x21, 'attr_str': 5}}
# base classes whose attributes are written unqualified
BASES = ('FrameworkElement', 'UIElement', 'Control', 'ContentControl', 'Panel', 'Shape', 'TextElement')


def records_2004(b):
    o = 0 if b[:2] == b'\x01\x00' else 4
    while o < len(b):
        rid, = struct.unpack_from('<H', b, o)
        o += 2
        if rid in FIXED_2004:
            size = FIXED_2004[rid]
            p = b[o:o + size]
            o += size
        else:
            if len(b) - o < 4:
                raise ValueError('BAML 2004: record %#x cut short at %#x' % (rid, o))
            size, = struct.unpack_from('<I', b, o)
            if size < 4 or o + size > len(b):
                raise ValueError('BAML 2004: bad record %#x at %#x' % (rid, o))
            p = b[o + 4:o + size]
            o += size
        yield rid, p


def typed_2004(an, d):
    """a PropertyCustom payload -> XAML text"""
    if len(d) == 4:
        ev, = struct.unpack_from('<i', d)
        if an == 'FontWeight':
            return FONTWEIGHT.get(ev, str(ev))
        names = ENUMS_2004.get(an)
        if names and 0 <= ev < len(names):
            return names[ev]
        raise ValueError('BAML 2004: %s: unknown enum value %d' % (an, ev))
    if len(d) == 1 and d[0] in (0, 1):
        return ('False', 'True')[d[0]]
    if d and d[0] == 0:
        sv, q = s7(d, 1)
        if q == len(d):
            return sv
    v = length(d, UNITS_2004)
    if v is not None:
        return v
    if len(d) == 5 and d[0] == 1 and an in COLOURS:
        return '#%02X%02X%02X%02X' % (d[4], d[3], d[2], d[1])
    if d and d[0] in (1, 4) and len(d) == 1 + d[0]:
        return ','.join(str(x) for x in d[1:])
    raise ValueError('BAML 2004: %s: unknown value %s' % (an, d.hex()))


def decode_2004(b):
    types, attrs, out, stack = {}, {}, [], []
    depth = 0

    def tname(t):
        if t in types:
            return types[t].split('.')[-1]
        if t in KNOWN_2004:
            return KNOWN_2004[t]
        raise ValueError('BAML 2004: unknown built-in type %d' % t)

    def aname(a):
        if a in attrs:
            return attrs[a]
        raise ValueError('BAML 2004: undeclared attribute %d' % a)

    def close_start():
        t, i, props = stack[-1]
        if not out[i].endswith('>'):
            out[i] += (' ' + ' '.join(props) if props else '') + '>'
            props.clear()
    ids = IDS_2004[(0, 0)]
    for rid, p in records_2004(b):
        ind = '  ' * depth
        if rid == 0x01:
            n, = struct.unpack_from('<I', p)
            ver = struct.unpack_from('<HHHHHH', p, 4 + n)
            ids = IDS_2004[(0, 1) if any(ver[1::2]) else (0, 0)]
        elif rid in (0x02, ids['asm']):
            continue
        elif rid == 0x1c:
            pre, q = s7(p, 0)
            ns, q = s7(p, q)
            out.append('%s<?Mapping XmlNamespace="%s" ClrNamespace="%s" ?>' % (ind, pre, ns))
        elif rid == ids['type']:
            t, a = struct.unpack_from('<hh', p)
            types[t] = s7(p, 4)[0]
        elif rid == ids['attr']:
            a, t = struct.unpack_from('<hh', p)
            n = s7(p, ids['attr_str'])[0]
            base = tname(t)
            cur = stack[-1][0] if stack else None
            attrs[a] = n if t == cur or base in BASES else '%s.%s' % (base, n)
        elif rid == 0x03:
            t, = struct.unpack_from('<h', p)
            out.append('%s<%s' % (ind, tname(t)))
            stack.append((t, len(out) - 1, []))
            depth += 1
        elif rid == 0x04:
            depth -= 1
            t, i, props = stack.pop()
            if out[i].endswith('>'):
                out.append('%s</%s>' % ('  ' * depth, tname(t)))
            else:
                if props:
                    out[i] += ' ' + ' '.join(props)
                if i == len(out) - 1:
                    out[i] += '/>'
                else:
                    out[i] += '>'
                    out.append('%s</%s>' % ('  ' * depth, tname(t)))
        elif rid in (0x05, 0x06):
            a, = struct.unpack_from('<h', p)
            v = s7(p, 2)[0] if rid == 0x05 else typed_2004(aname(a).split('.')[-1], p[2:])
            if not stack or stack[-1][0] is None:
                raise ValueError('BAML 2004: property %s outside an element' % aname(a))
            stack[-1][2].append('%s="%s"' % (aname(a), esc(v)))
        elif rid in (0x07, 0x09, 0x0b, 0x0d):
            a, = struct.unpack_from('<h', p)
            if stack:
                close_start()
            out.append('%s<%s.%s>' % (ind, tname(stack[-1][0]) if stack and stack[-1][0] is not None else '?',
                                       aname(a).split('.')[-1]))
            stack.append((None, len(out) - 1, []))
            depth += 1
        elif rid in (0x08, 0x0a, 0x0c, 0x0e):
            depth -= 1
            t, i, props = stack.pop()
            out.append('%s</%s' % ('  ' * depth, out[i].strip()[1:]))
        elif rid == 0x19:
            val, q = s7(p, 0)
            nm, q = s7(p, q)
            if stack and stack[-1][0] is not None and not out[stack[-1][1]].endswith('>'):
                stack[-1][2].append('def:%s="%s"' % (nm, esc(val)))
            else:
                out.append('%s<!-- def:%s="%s" -->' % (ind, nm, esc(val)))
        elif rid == 0x10:
            out.append('%s%s' % (ind, s7(p, 0)[0].replace('&', '&amp;').replace('<', '&lt;')))
        elif rid == 0x13:
            pre, q = s7(p, 0)
            ns, q = s7(p, q)
            if stack:
                stack[-1][2].append('xmlns%s="%s"' % (':' + pre if pre else '', ns))
        else:
            raise ValueError('BAML 2004: unknown record %#x' % rid)
    return '\n'.join(out) + '\n'


def decode(b):
    """either dialect -> XAML text"""
    return decode_2003(b) if is_2003(b) else decode_2004(b)


# ---------------------------------------------------------------- .NET manifest resources
def _clr_resources(d):
    """the CLI header's Resources directory of a PE file: the blobs it holds"""
    if d[:2] != b'MZ':
        raise ValueError('not a PE file')
    pe, = struct.unpack_from('<I', d, 0x3c)
    if d[pe:pe + 4] != b'PE\0\0':
        raise ValueError('not a PE file')
    nsec, optsz = struct.unpack_from('<H', d, pe + 6)[0], struct.unpack_from('<H', d, pe + 20)[0]
    opt = pe + 24
    magic, = struct.unpack_from('<H', d, opt)
    dd = opt + (96 if magic == 0x10b else 112)
    secs = [struct.unpack_from('<IIII', d, opt + optsz + 40 * i + 8) for i in range(nsec)]

    def off(r):
        for vsize, va, rsize, rptr in secs:
            if va <= r < va + max(vsize, rsize):
                return r - va + rptr
        raise ValueError('RVA %#x outside the sections' % r)
    cli_rva, cli_size = struct.unpack_from('<II', d, dd + 14 * 8)
    if not cli_rva:
        raise ValueError('no CLI header (not a .NET assembly)')
    c = off(cli_rva)
    rrva, rsize = struct.unpack_from('<II', d, c + 24)
    if not rrva:
        return []
    o, end, blobs = off(rrva), off(rrva) + rsize, []
    while o + 4 <= end:
        n, = struct.unpack_from('<I', d, o)
        if n == 0 or o + 4 + n > end:
            break
        blobs.append(d[o + 4:o + 4 + n])
        o = (o + 4 + n + 7) & ~7
    return blobs


def parse_resources(b):
    """a .resources blob -> [(name, type, bytes)]"""
    magic, = struct.unpack_from('<I', b, 0)
    if magic != 0xBEEFCACE:
        return []
    hdrver, skip = struct.unpack_from('<II', b, 4)
    o = 12 + skip
    ver, n, ntypes = struct.unpack_from('<III', b, o)
    o += 12
    types = []
    for _ in range(ntypes):
        t, o = s7(b, o)
        types.append(t)
    o = (o + 7) & ~7
    o += 8 * n            # name hashes, name positions
    datasec, = struct.unpack_from('<I', b, o)
    o += 4
    names = []
    for _ in range(n):
        name, o = s7(b, o, 'utf-16le')
        off, = struct.unpack_from('<I', b, o)
        o += 4
        names.append((name, off))
    out = []
    for name, off in names:
        p = datasec + off
        tcode, p = u7(b, p)
        if ver == 2:
            tname = {1: 'String', 0x20: 'ByteArray', 0x21: 'ByteArray', 0x22: 'Stream'}.get(tcode, 'type%d' % tcode)
        else:
            tname = types[tcode] if tcode < len(types) else 'type%d' % tcode
        if 'String' in tname:
            ln, p = u7(b, p)
        elif 'Byte' in tname or 'Stream' in tname:
            ln, = struct.unpack_from('<I', b, p)
            p += 4
        else:
            ln = 0
        out.append((name, tname, b[p:p + ln]))
    return out


def net_resources(path):
    """an assembly's manifest resource entries: {name: bytes} (the first of a name wins)"""
    d = open(path, 'rb').read()
    res = {}
    for blob in _clr_resources(d):
        for name, t, data in parse_resources(blob):
            res.setdefault(name, data)
    return res


def main(argv):
    if len(argv) == 2 and argv[0] == '--list':
        d = open(argv[1], 'rb').read()
        for blob in _clr_resources(d):
            for name, t, data in parse_resources(blob):
                print('%-60s %-12s %8d' % (name, t, len(data)))
        return 0
    if len(argv) == 3 and argv[0] == '--resource':
        res = net_resources(argv[1])
        if argv[2] not in res:
            print('lhdebaml: %s has no resource %s' % (argv[1], argv[2]), file=sys.stderr)
            return 1
        sys.stdout.write(decode(res[argv[2]]))
        return 0
    if len(argv) == 1 and not argv[0].startswith('-'):
        sys.stdout.write(decode(open(argv[0], 'rb').read()))
        return 0
    print(__doc__.split('\n\n')[1], file=sys.stderr)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
