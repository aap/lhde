#!/usr/bin/env python3
"""Reader for the pre-release WIM format 1.8.1 (Longhorn 4015, March 2003).

Run with python -I.  Usage:
  wim0108.py WIM info
  wim0108.py WIM list [IMAGE]            IMAGE = 1-based metadata resource number
  wim0108.py WIM extract IMAGE OUTDIR [PATTERN ...]   (fnmatch on the full path, case-insensitive)
  wim0108.py WIM meta IMAGE OUT          dump the raw decompressed metadata resource

Format as reverse-engineered (see COMPOSITION.md section 20):
  header (126 bytes): u32 size 0x7e, u32 version 0x00010801, u32 flags 0x00010003, u32 0,
    reshdr lookup table @0x10, 24 zero bytes @0x31, reshdr XML @0x49, zero pad to 0x7e.
  reshdr (33 bytes): u8 flags (2 metadata, 4 compressed), u56 stored size, u8 0,
    u64 offset, u32 sequence/id, u32 refcount, u64 original size.
  lookup entry = reshdr + 20-byte SHA-1 (53 bytes).  Metadata entries have a zero hash.
  Files reference lookup entries by the sequence id (not by hash); see parse_meta.
  A compressed resource is a complete MS-CAB (MSCF), one folder, MSZIP, one file named "0".
"""
import sys, struct, zlib, os, fnmatch


def reshdr(d, o):
    fl = d[o]
    sz = int.from_bytes(d[o + 1:o + 8], 'little')
    off = int.from_bytes(d[o + 9:o + 17], 'little')
    seq, ref = struct.unpack_from('<II', d, o + 17)
    orig = int.from_bytes(d[o + 25:o + 33], 'little')
    return dict(flags=fl, size=sz, off=off, seq=seq, ref=ref, orig=orig)


def uncab(b):
    """Decompress a single-folder MSZIP cabinet, return the folder's bytes."""
    if b[:4] != b'MSCF':
        raise ValueError('not a cabinet')
    cbCabinet, coffFiles = struct.unpack_from('<I4xI', b, 8)
    flags = struct.unpack_from('<H', b, 0x1e)[0]
    o = 0x24
    cbHdrRes = cbFolRes = cbDatRes = 0
    if flags & 4:
        cbHdrRes, cbFolRes, cbDatRes = struct.unpack_from('<HBB', b, o)
        o += 4 + cbHdrRes
    coffCabStart, cCFData, typeCompress = struct.unpack_from('<IHH', b, o)
    if typeCompress & 0xf not in (0, 1):
        raise ValueError('compression %d' % typeCompress)
    o = coffCabStart
    out = bytearray()
    for _ in range(cCFData):
        csum, cb, ucb = struct.unpack_from('<IHH', b, o)
        o += 8 + cbDatRes
        blk = b[o:o + cb]
        o += cb
        if typeCompress & 0xf == 0:
            out += blk
            continue
        if blk[:2] != b'CK':
            raise ValueError('bad MSZIP block')
        zd = bytes(out[-32768:])
        z = zlib.decompressobj(-15, zdict=zd) if zd else zlib.decompressobj(-15)
        r = z.decompress(blk[2:]) + z.flush()
        if len(r) != ucb:
            raise ValueError('block size %d != %d' % (len(r), ucb))
        out += r
    return bytes(out)


class Wim:
    def __init__(self, path):
        self.f = open(path, 'rb')
        h = self.f.read(0x7e)
        hs, ver, fl = struct.unpack_from('<III', h, 0)
        if hs != 0x7e or ver != 0x00010801:
            raise ValueError('not a 1.8.1 WIM (size %#x ver %#x)' % (hs, ver))
        self.version, self.hflags = ver, fl
        self.lt = reshdr(h, 0x10)
        self.xmlhdr = reshdr(h, 0x49)
        raw = self.read_raw(self.lt)
        self.entries = []
        for o in range(0, len(raw) - 52, 53):
            e = reshdr(raw, o)
            e['hash'] = raw[o + 33:o + 53]
            self.entries.append(e)
        self.byid = {e['seq']: e for e in self.entries if not e['flags'] & 2}
        self.metas = [e for e in self.entries if e['flags'] & 2]

    def read_raw(self, e):
        self.f.seek(e['off'])
        return self.f.read(e['size'])

    def read(self, e):
        b = self.read_raw(e)
        if e['flags'] & 4:
            b = uncab(b)
        if len(b) != e['orig']:
            raise ValueError('size %d != %d' % (len(b), e['orig']))
        return b

    def xml(self):
        return self.read_raw(self.xmlhdr).decode('utf-16')

    # ---- metadata ----
    def tree(self, n):
        m = self.read(self.metas[n - 1])
        return m, parse_meta(m)


def parse_meta(m):
    """The 1.8.1 metadata resource is UTF-16 INI text: [Image] RootDrive=N, [Security] "0xN"=<hex SD>,
    then one section [x<id>] per directory with lines
    "long name"=id,"short name",attr,ctime lo,hi,atime lo,hi,mtime lo,hi,secid,,,
    For a directory (attr & 0x10) id names its section; for a file id is the lookup entry's
    sequence number (0 = empty file).  The root directory is section [x1].
    Returns list of (path, dict(id, attr, short, sec))."""
    t = m.decode('utf-16')
    secs, cur = {}, None
    for ln in t.splitlines():
        if ln.startswith('['):
            k = ln.strip()[1:-1]
            cur = secs.setdefault(k, []) if k[:1] == 'x' else None
            continue
        if cur is None or not ln.startswith('"'):
            continue
        q = ln.index('"', 1)
        name = ln[1:q]
        f = ln[q + 2:].split(',')
        e = dict(id=int(f[0], 16), short=f[1].strip('"'), attr=int(f[2], 16),
                 sec=int(f[9], 16) if len(f) > 9 and f[9] else -1, name=name)
        cur.append(e)
    out = []

    def walk(sid, prefix, depth):
        for e in secs.get('x%X' % sid, []):
            path = prefix + '\\' + e['name']
            out.append((path, e))
            if e['attr'] & 0x10 and depth < 64:
                walk(e['id'], path, depth + 1)
    walk(1, '', 0)
    return out


def main():
    a = sys.argv[1:]
    if not a:
        sys.exit(__doc__)
    w = Wim(a[0])
    cmd = a[1] if len(a) > 1 else 'info'
    if cmd == 'info':
        print('version %#x flags %#x' % (w.version, w.hflags))
        print('lookup', w.lt, 'entries', len(w.entries))
        print('xml', w.xmlhdr)
        for i, e in enumerate(w.metas, 1):
            print('image', i, e)
        print(w.xml())
    elif cmd == 'meta':
        open(a[3], 'wb').write(w.read(w.metas[int(a[2]) - 1]))
    elif cmd == 'list':
        n = int(a[2]) if len(a) > 2 else 1
        m, t = w.tree(n)
        for p, e in t:
            r = w.byid.get(e['id']) if e['id'] else None
            sz = r['orig'] if r else 0
            print('%08x %10d %s%s' % (e['attr'], sz, p, '\\' if e['attr'] & 0x10 else ''))
    elif cmd == 'extract':
        n, od, pats = int(a[2]), a[3], [p.lower() for p in a[4:]]
        m, t = w.tree(n)
        for p, e in t:
            if e['attr'] & 0x10:
                continue
            if pats and not any(fnmatch.fnmatch(p.lower(), q) for q in pats):
                continue
            r = w.byid.get(e['id']) if e['id'] else None
            dst = os.path.join(od, str(n), *p.strip('\\').split('\\'))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            data = w.read(r) if r else b''
            with open(dst, 'wb') as fo:
                fo.write(data)
            print(len(data), dst)


if __name__ == '__main__':
    main()
