#!/usr/bin/env python3
"""Longhorn's two aurora scenes: from their BAML to layer lists (lhde-import's aurora recipe), from those to aurorademo's
C tables (make examples).

usage: lhdeaurora.py layers FILE.baml|FILE.xaml OUT.json [SOURCE]   flatten one scene (the dialect tells pane from desktop)
       lhdeaurora.py header OUT.h [PANE.json] [DESKTOP.json]          examples/aurorademo's tables (an empty or missing path:
                                                                       that scene is left out, and the header says so)

The scenes (the files lhde-import reads them from: share/sources/<build>.ini [aurora]; the JSON lands in the Windows tree's
generated/aurora/):
  preview pane  jade.msstyles PreviewPane.AURORA (STREAM 481 in 4074, 479 in 4066), 2003 BAML: 50 paths in 9 canvases,
                12 animations, design space 1080x160 stretched non-uniformly to the pane; one hue (#5190DA) recoloured per
                folder type at run time; gradient isolines perpendicular in bounding-box space
  desktop       Microsoft.Windows.WCPClient.dll resource xaml/sidebar/desktopaurora.baml (4066, 4074; 4093:
                ms/internal/desktop/desktopaurora.baml), 2004 BAML: 52 paths (one collapsed) in 13 groups, 30 animations,
                design space 800x600; gradient isolines perpendicular in the path's own space (start and end scaled into the
                bbox). The three builds give the same layer list.

Semantics (both scenes):
  * Transform lists apply in document order: the first entry is applied to the path's points first (Avalon
    TransformCollection order; the designer's -pivot / rotate / +pivot triples prove it). A path's full list is its own
    (2004: Canvas.Left/Top first), then each enclosing TransformDecorator (2004) or Canvas.TransformEffect (2003), inside out.
  * Gradients are linear, in bounding-box units of the path geometry (its tight bbox: end points plus the cubic extrema);
    stops are #AARRGGBB, not premultiplied. The 2003 LinearGradients store no StartPoint/EndPoint: the default is the
    horizontal (0,0.5)-(1,0.5). A 2004 brush with StartPoint/EndPoint stored also has a .Transform, which is the same
    mapping baked in (designer metadata): it is ignored.
  * Animations are linear From->To or key values over Duration, repeated forever, all starting at t = 0; AutoReverse is a
    triangle wave, else a sawtooth. A translate whose X is animated keeps its static X only until the first tick.
  * Opacity multiplies down the tree: group opacity animation x path opacity animation x stop alpha.

The JSON (one scene): {"source", "gradientIsolines": "bbox" | "path", "groups": [every Canvas in document order: id,
transform, opacityAnim, viewBox, stretch, size, background, presenter], "layers": [in draw order: kind "path" (id, groups,
d, bbox, fill/stroke {start, end, explicitPoints, stops [[offset, "#AARRGGBB"]], transform}, strokeThickness, visibility,
opacity, opacityAnim, transform [steps], groupOpacityAnims) or "emptyCanvas"]}. A step is {"type": translate x y [animX] |
rotate deg center | scale sx sy center | skew ax ay}; an animation {dur, autoReverse, repeat, from/to or keyValues/keyTimes}.

The header holds per scene: layers (draw order, path ops + points, tight bbox, transform steps in application order,
fill/stroke paint, own opacity animation, enclosing opacity group), paints (StartPoint/EndPoint in bbox space, the 2003
GradientTransform folded to a matrix in bbox space, stops as offset + 0xAARRGGBB, and whether the isolines are in path
space), groups (the canvases with an opacity animation; none nest), anims (one per animation of the scene: property, from,
to or key frames, duration, auto-reverse, begin; translate steps and opacities refer to them by index).
"""
import json
import math
import os
import re
import sys
import xml.etree.ElementTree as ET

SCENES = {'pane': (1080, 160, 12), 'desktop': (800, 600, 30)}   # design size, the number of animations


# ---------------------------------------------------------------- XAML -> layer list
def parse_xaml(text):
    text = re.sub(r'<\?Mapping[^>]*\?>', '', text)
    text = re.sub(r' xmlns(:\w+)?="[^"]*"', '', text)
    return ET.fromstring(text)


def num(s):
    """a XAML number; a length in points (4093: StrokeThickness="1pt") in pixels at 96 dpi"""
    if s is None:
        return None
    s = s.strip()
    if s.endswith('pt'):
        return float(s[:-2]) * 96 / 72
    return float(s.rstrip('.'))


def pt(s):
    return [num(v) for v in s.replace(' ', '').split(',')]


def anim(el):
    """FloatAnimation (2003) / DoubleAnimation (2004) -> dict"""
    a = {k.split('.')[-1]: v for k, v in el.attrib.items()}
    d = {'dur': num(a['Duration']), 'autoReverse': a.get('AutoReverse', 'False').lower() == 'true',
         'repeat': a.get('RepeatDuration', '')}
    if 'KeyValues' in a:
        d['keyValues'] = [num(v) for v in a['KeyValues'].split()]
        d['keyTimes'] = [num(v) for v in a['KeyTimes'].split()]
    else:
        d['from'] = num(a['From'])
        d['to'] = num(a['To'])
    for k in ('BeginTime', 'Begin', 'Acceleration', 'Deceleration', 'KeySplines', 'InterpolationMethod'):
        if k in a:
            d[k] = a[k]
    return d


def find_anims(el):
    return [anim(x) for x in el.iter() if x.tag in ('FloatAnimation', 'DoubleAnimation')]


def transforms(el):
    """Transform / TransformCollection / a single transform element -> list of steps"""
    out = []
    for t in ([el] if el.tag.endswith('Transform') and '.' not in el.tag and el.tag != 'Transform' else list(el)):
        if t.tag in ('Transform', 'TransformCollection'):
            out += transforms(t)
            continue
        a = t.attrib
        if t.tag == 'TranslateTransform':
            s = {'type': 'translate', 'x': num(a.get('X', '0')), 'y': num(a.get('Y', '0'))}
            for c in t:
                if c.tag.endswith('.X') or c.tag.endswith('.XAnimations'):
                    s['animX'] = find_anims(c)[0]
                if c.tag.endswith('.Y') or c.tag.endswith('.YAnimations'):
                    s['animY'] = find_anims(c)[0]
        elif t.tag == 'RotateTransform':
            s = {'type': 'rotate', 'deg': num(a.get('Angle', '0')), 'center': pt(a.get('Center', '0,0'))}
        elif t.tag == 'ScaleTransform':
            s = {'type': 'scale', 'sx': num(a.get('ScaleX', '1')), 'sy': num(a.get('ScaleY', '1')),
                 'center': pt(a.get('Center', '0,0'))}
        elif t.tag == 'SkewTransform':
            s = {'type': 'skew', 'ax': num(a.get('AngleX', '0')), 'ay': num(a.get('AngleY', '0'))}
        else:
            raise ValueError('unknown transform %s' % t.tag)
        out.append(s)
    return out


def brush(el):
    """LinearGradient (2003) / LinearGradientBrush (2004) -> dict"""
    b = {'start': pt(el.get('StartPoint', '0,0.5')), 'end': pt(el.get('EndPoint', '1,0.5')),
         'explicitPoints': 'StartPoint' in el.attrib, 'stops': [], 'transform': []}
    for st in el.iter('GradientStop'):
        b['stops'].append([num(st.get('Offset')), st.get('Color').upper()])
    for c in el:
        if c.tag.endswith('.GradientTransform') or c.tag.endswith('.Transform'):
            b['transform'] = transforms(c)
    return b


def parse_d(d):
    """path data (M L C Z, absolute) -> (ops, points)"""
    toks = re.findall(r'[MLCZ]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?', d)
    ops, pts, i, cur = '', [], 0, None
    n = {'M': 2, 'L': 2, 'C': 6, 'Z': 0}
    while i < len(toks):
        if toks[i] in n:
            cur = toks[i]
            i += 1
        ops += cur
        pts += [float(t) for t in toks[i:i + n[cur]]]
        i += n[cur]
        if cur == 'M':
            cur = 'L'
    return ops, pts


def tight_bbox(ops, pts):
    """the tight geometry bounds: end points plus the cubic extrema (not the control points) -> x, y, w, h"""
    xs, ys, k, cx, cy = [], [], 0, 0, 0
    for op in ops:
        if op in 'ML':
            cx, cy = pts[k], pts[k + 1]
            xs.append(cx)
            ys.append(cy)
            k += 2
        elif op == 'C':
            p = [(cx, cy), (pts[k], pts[k + 1]), (pts[k + 2], pts[k + 3]), (pts[k + 4], pts[k + 5])]
            for ax, out in ((0, xs), (1, ys)):
                a, b, c, d = (q[ax] for q in p)
                A, B, C = -a + 3 * b - 3 * c + d, 2 * (a - 2 * b + c), b - a   # the derivative / 3
                roots = []
                if abs(A) < 1e-12:
                    if abs(B) > 1e-12:
                        roots = [-C / B]
                else:
                    disc = B * B - 4 * A * C
                    if disc >= 0:
                        roots = [(-B + s * math.sqrt(disc)) / (2 * A) for s in (1, -1)]
                out.append(d)
                for t in roots:
                    if 0 < t < 1:
                        out.append((1 - t) ** 3 * a + 3 * (1 - t) ** 2 * t * b + 3 * (1 - t) * t * t * c + t ** 3 * d)
            cx, cy = pts[k + 4], pts[k + 5]
            k += 6
    return [min(xs), min(ys), max(xs) - min(xs), max(ys) - min(ys)]


def walk(el, groups, outer, layers, opac_chain, allgroups):
    tag = el.tag
    if tag == 'DockPanel':        # the desktop's watermark text
        return
    if tag == 'Canvas':
        own, gop = [], None
        for c in el:
            if c.tag == 'Canvas.TransformEffect':
                own = transforms(c)
            if c.tag == 'Canvas.Opacity':
                gop = find_anims(c)[0]
        g = {'id': el.get('ID', '?'), 'transform': own}
        allgroups.append(g)
        if gop:
            g['opacityAnim'] = gop
        if el.get('ViewBox'):
            g['viewBox'] = [num(v) for v in el.get('ViewBox').split(',')]
            g['stretch'] = el.get('Stretch')
        if el.get('Width'):
            g['size'] = [el.get('Width'), el.get('Height')]
        if el.get('Background'):
            g['background'] = el.get('Background')
        if el.get('Presenter'):
            g['presenter'] = el.get('Presenter')
        for c in el:
            walk(c, groups + [g], own + outer, layers, opac_chain + ([gop] if gop else []), allgroups)
        if not [c for c in el if not c.tag.startswith('Canvas.')]:
            layers.append({'kind': 'emptyCanvas', 'groups': [x['id'] for x in groups + [g]], 'canvas': g})
    elif tag == 'TransformDecorator':
        own = []
        for c in el:
            if c.tag == 'TransformDecorator.Transform':
                own = transforms(c)
        for c in el:
            if not c.tag.startswith('TransformDecorator.'):
                walk(c, groups, own + outer, layers, opac_chain, allgroups)
    elif tag == 'Path':
        L = {'kind': 'path', 'id': el.get('ID'), 'groups': [g['id'] for g in groups], 'd': el.get('Data')}
        L['bbox'] = tight_bbox(*parse_d(L['d']))
        own = []
        if el.get('Canvas.Left') or el.get('Canvas.Top'):
            own.append({'type': 'translate', 'x': num(el.get('Canvas.Left', '0')), 'y': num(el.get('Canvas.Top', '0')),
                        'note': 'Canvas.Left/Top'})
        for c in el:
            if c.tag == 'Path.TransformEffect':
                own += transforms(c)
            elif c.tag == 'Path.Fill':
                L['fill'] = brush(c[0])
            elif c.tag == 'Path.Stroke':
                L['stroke'] = brush(c[0])
            elif c.tag == 'Path.Opacity':
                L['opacityAnim'] = find_anims(c)[0]
        if el.get('StrokeThickness'):
            L['strokeThickness'] = num(el.get('StrokeThickness'))
        if 'stroke' in L and 'strokeThickness' not in L:
            L['strokeThickness'] = 1.0
        if el.get('Visibility'):
            L['visibility'] = el.get('Visibility')
        if el.get('Opacity'):
            L['opacity'] = num(el.get('Opacity'))
        L['transform'] = own + outer
        L['groupOpacityAnims'] = list(opac_chain)
        layers.append(L)
    else:
        for c in el:
            if not c.tag.startswith(tag + '.'):
                walk(c, groups, outer, layers, opac_chain, allgroups)


def scene_layers(xaml, source, isolines):
    """a scene's XAML -> its layer list (the JSON above)"""
    layers, groups = [], []
    walk(parse_xaml(xaml), [], [], layers, [], groups)
    return {'source': source, 'gradientIsolines': isolines, 'groups': groups, 'layers': layers}


def baml_layers(baml, source):
    """a scene's BAML -> its layer list: the 2003 dialect is the preview pane (isolines in bbox space), the 2004 one the
    desktop (in path space)"""
    import lhdebaml
    pane = lhdebaml.is_2003(baml)
    d = scene_layers(lhdebaml.decode(baml), source, 'bbox' if pane else 'path')
    paths = sum(1 for L in d['layers'] if L['kind'] == 'path')
    if not paths:
        raise ValueError('%s: no paths (not an aurora scene?)' % source)
    return d


def write_json(d, path):
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    with open(path + '.new', 'w') as f:
        json.dump(d, f, indent=1)
    os.replace(path + '.new', path)


# ---------------------------------------------------------------- layer lists -> aurorademo's C tables
STEP = {'translate': 'AU_T', 'rotate': 'AU_R', 'scale': 'AU_S', 'skew': 'AU_K'}


def cnum(v):
    return '%.10g' % float(v)


def matrix(steps):
    """a step list (application order) -> cairo matrix xx yx xy yy x0 y0"""
    def mul(a, b):  # a, then b
        return [a[0] * b[0] + a[1] * b[2], a[0] * b[1] + a[1] * b[3], a[2] * b[0] + a[3] * b[2], a[2] * b[1] + a[3] * b[3],
                a[4] * b[0] + a[5] * b[2] + b[4], a[4] * b[1] + a[5] * b[3] + b[5]]
    m = [1, 0, 0, 1, 0, 0]
    for s in steps:
        if s['type'] == 'translate':
            st = [1, 0, 0, 1, s['x'], s['y']]
        elif s['type'] == 'scale':
            st = [s['sx'], 0, 0, s['sy'], 0, 0]
        elif s['type'] == 'rotate':
            r = math.radians(s['deg'])
            st = [math.cos(r), math.sin(r), -math.sin(r), math.cos(r), 0, 0]
        else:
            st = [1, math.tan(math.radians(s['ay'])), math.tan(math.radians(s['ax'])), 1, 0, 0]
        if s.get('center', [0, 0]) != [0, 0]:
            cx, cy = s['center']
            st = mul(mul([1, 0, 0, 1, -cx, -cy], st), [1, 0, 0, 1, cx, cy])
        m = mul(m, st)
    return m


def c_scene(name, data, design):
    """one scene's tables -> (C text, number of animations)"""
    gx = {G['id']: G for G in data['groups']}
    pathspace = int(data.get('gradientIsolines') == 'path')
    anims, akey = [], {}

    def add_anim(a, prop, target, key=None):
        if key is not None and key in akey:
            return akey[key]
        kv, kt = a.get('keyValues', []), a.get('keyTimes', [])
        assert len(kv) <= 3
        anims.append('    { "%s", AU_%s, %s, %s, %s, %d, %s, %d, { %s }, { %s } },' % (
            target, prop, cnum(a.get('from', 0)), cnum(a.get('to', 0)), cnum(a['dur']), int(a['autoReverse']),
            cnum(a.get('begin', 0)), len(kv), ', '.join(cnum(v) for v in kt) or '0', ', '.join(cnum(v) for v in kv) or '0'))
        if key is not None:
            akey[key] = len(anims) - 1
        return len(anims) - 1
    groups, gidx = [], {}
    for G in data['groups']:
        if 'opacityAnim' in G:
            gidx[G['id']] = len(groups)
            groups.append('    { "%s", %d },' % (G['id'], add_anim(G['opacityAnim'], 'OPACITY', G['id'])))
    # a translate X animation shared by several layers belongs to their canvas or decorator: one clock
    shared = {}
    for L in data['layers']:
        for s in L.get('transform', []):
            if 'animX' in s:
                shared.setdefault(json.dumps(s['animX'], sort_keys=True), []).append(L)
    layers, body = [], []
    for li, L in enumerate(data['layers']):
        if L['kind'] != 'path':
            continue
        lname = L.get('id') or '%s#%d' % (L['groups'][-1], li + 1)
        ops, pts = parse_d(L['d'])
        bx = tight_bbox(ops, pts)
        p = '%s_%d' % (name, li)
        body.append('static const double %s_pts[] = { %s };' % (p, ', '.join(cnum(v) for v in pts)))
        steps = []
        for s in L['transform']:
            ai = -1
            if 'animX' in s:
                k = json.dumps(s['animX'], sort_keys=True)
                owners = shared[k]
                tgt = next((G for G in L['groups'] if any(t.get('animX') == s['animX'] for t in gx[G]['transform'])), None)
                tgt = tgt or ('%s decorator' % L['groups'][-1] if len(owners) > 1 else lname)
                ai = add_anim(s['animX'], 'X', tgt, k)
            c = s.get('center', [0, 0])
            a, b = {'translate': (s.get('x'), s.get('y')), 'rotate': (s.get('deg'), 0), 'scale': (s.get('sx'), s.get('sy')),
                    'skew': (s.get('ax'), s.get('ay'))}[s['type']]
            steps.append('{ %s, %s, %s, %s, %s, %d }' % (STEP[s['type']], cnum(a), cnum(b), cnum(c[0]), cnum(c[1]), ai))
        body.append('static const struct au_step %s_steps[] = { %s };' % (p, ', '.join(steps)))
        paints = {}
        for kind in ('fill', 'stroke'):
            if kind not in L:
                continue
            B = L[kind]
            gt = matrix(B['transform']) if B['transform'] and not B['explicitPoints'] else [1, 0, 0, 1, 0, 0]
            stops = ', '.join('{ %s, 0x%s }' % (cnum(o), c[1:]) for o, c in B['stops'])
            body.append('static const struct au_stop %s_%s_stops[] = { %s };' % (p, kind, stops))
            body.append('static const struct au_paint %s_%s = { %s, %s, %s, %s, { %s }, %d, %s_%s_stops, %d };' % (
                p, kind, cnum(B['start'][0]), cnum(B['start'][1]), cnum(B['end'][0]), cnum(B['end'][1]),
                ', '.join(cnum(v) for v in gt), len(B['stops']), p, kind, pathspace))
            paints[kind] = '&%s_%s' % (p, kind)
        og = [G for G in L['groups'] if G in gidx]
        assert len(og) <= 1, 'nested opacity groups'
        op = add_anim(L['opacityAnim'], 'OPACITY', lname) if 'opacityAnim' in L else -1
        if L.get('visibility') == 'Collapsed':
            body.append('/* %s: Visibility=Collapsed, kept for completeness */' % lname)
        layers.append('    { "%s", "%s", %s_pts, { %s }, %d, %s_steps, %s, %s, %s, %d, %d, %d },' % (
            lname, ops, p, ', '.join(cnum(v) for v in bx), len(steps), p, paints.get('fill', 'NULL'),
            paints.get('stroke', 'NULL'), cnum(L.get('strokeThickness', 1)), op, gidx[og[0]] if og else -1,
            int(L.get('visibility') == 'Collapsed')))
    text = ('/* ---- %s: %s (%d paths, %d animations) ---- */\n' % (name, data['source'], len(layers), len(anims)) +
            '\n'.join(body) +
            '\nstatic const struct au_anim %s_anims[] = {\n' % name + '\n'.join(anims) + '\n};\n' +
            'static const struct au_group %s_groups[] = {\n' % name + ('\n'.join(groups) or '    { "", -1 },') + '\n};\n' +
            'static const struct au_layer %s_layers[] = {\n' % name + '\n'.join(layers) + '\n};\n' +
            'static const struct au_scene %s_scene = { %s, %s, %d, %s_layers, %d, %s_groups, %d, %s_anims };\n' % (
                name, design[0], design[1], len(layers), name, len(groups), name, len(anims), name))
    return text, len(anims)


HEAD = '''/* GENERATED by tools/lhdeaurora.py (make examples) from the Windows tree's generated/aurora/ -- do not edit.
   Longhorn's two aurora scenes as numbers, read from the user's imported files by lhde-import: the Explorer preview-pane aurora
   (jade.msstyles PreviewPane.AURORA, 1080x160) and the desktop aurora (WCPClient desktopaurora.baml, 800x600).
   Steps are in application order (the first is applied to the path's points first); centres are pivots. */
#ifndef AURORA_DATA_H
#define AURORA_DATA_H
#include <stddef.h>
enum { AU_T, AU_R, AU_S, AU_K };               /* translate x,y / rotate deg / scale sx,sy / skew ax,ay (deg, WPF SkewTransform) */
enum { AU_X, AU_OPACITY };                     /* animated property: a translate step's X, or an opacity */
struct au_anim  { const char *target; int prop; double from, to, dur; int autorev; double begin;
                  int nkeys; double kt[3], kv[3]; };  /* nkeys > 0: linear key frames over one Duration instead of from/to */
struct au_step  { int type; double a, b, cx, cy; int anim; };   /* anim >= 0: a translate whose X is that animation */
struct au_stop  { double off; unsigned argb; };
struct au_paint { double sx, sy, ex, ey;       /* StartPoint, EndPoint in bounding-box units */
                  double gt[6];                /* GradientTransform in bbox space, cairo order xx yx xy yy x0 y0 */
                  int nstops; const struct au_stop *stops;
                  int pathspace; };            /* 1: isolines perpendicular in path space, not bbox space (desktop) */
struct au_layer { const char *name, *ops; const double *pts; double bbox[4];   /* ops: M L C Z; bbox x y w h (tight) */
                  int nsteps; const struct au_step *steps; const struct au_paint *fill, *stroke; double stroke_w;
                  int opacity_anim, group, collapsed; };
struct au_group { const char *name; int opacity_anim; };
struct au_scene { double w, h; int nlayers; const struct au_layer *layers; int ngroups; const struct au_group *groups;
                  int nanims; const struct au_anim *anims; };

'''


def header(pane, desktop):
    """the header text from the two layer lists (either may be None: AURORA_NO_PANE / AURORA_NO_DESKTOP)"""
    out = [HEAD]
    for name, data in (('pane', pane), ('desktop', desktop)):
        if data is None:
            out.append('#define AURORA_NO_%s 1   /* not in the Windows tree: lhde-import did not find its file */\n\n' % name.upper())
            continue
        w, h, nanim = SCENES[name]
        text, n = c_scene(name, data, (w, h))
        if n != nanim:
            raise ValueError('%s: %d animations, not %d (not the 4074 scene?)' % (name, n, nanim))
        out.append(text + '\n')
    return ''.join(out) + '#endif\n'


HINT = {'pane': 'jade.msstyles of build 4074 (lhde-import of the 4074 source)',
        'desktop': 'Microsoft.Windows.WCPClient.dll of build 4074 (lhde-import --iso longhorn4074.iso, or a pack made since '
                   'share/sources/4074.ini names it)'}


def main(argv):
    if len(argv) in (3, 4) and argv[0] == 'layers':
        b = open(argv[1], 'rb').read()
        src = argv[3] if len(argv) > 3 else os.path.basename(argv[1])
        if argv[1].endswith('.xaml'):
            d = scene_layers(b.decode('utf-8'), src, 'path' if 'TransformDecorator' in b.decode('utf-8') else 'bbox')
        else:
            d = baml_layers(b, src)
        write_json(d, argv[2])
        return 0
    if 2 <= len(argv) <= 4 and argv[0] == 'header':
        out = argv[1]
        scenes = {}
        for name, p in zip(('pane', 'desktop'), argv[2:] + [''] * (4 - len(argv))):
            scenes[name] = json.load(open(p)) if p and os.path.isfile(p) else None
            if scenes[name] is None:
                print('lhdeaurora: %s: no %s aurora in the Windows tree (generated/aurora/%s-4074.json); aurorademo is built '
                      'without it. It comes from %s.' % (out, name, 'previewpane' if name == 'pane' else name, HINT[name]),
                      file=sys.stderr)
        text = header(scenes['pane'], scenes['desktop'])
        try:
            same = open(out).read() == text
        except OSError:
            same = False
        if not same:              # unchanged: the file is not touched, so aurorademo is not rebuilt
            os.makedirs(os.path.dirname(out) or '.', exist_ok=True)
            with open(out + '.new', 'w') as f:
                f.write(text)
            os.replace(out + '.new', out)
        return 0
    print(__doc__.split('\n\n')[1], file=sys.stderr)
    return 2


if __name__ == '__main__':
    sys.path.insert(0, os.path.dirname(os.path.realpath(__file__)))
    sys.exit(main(sys.argv[1:]))
