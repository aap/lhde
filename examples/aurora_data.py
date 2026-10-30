#!/usr/bin/env python3
"""Write examples/aurora_data.h (aurorademo's scene tables) from the two Longhorn aurora layer dumps.
usage: examples/aurora_data.py [AURORA_DIR] [OUT.h]
  AURORA_DIR  holds previewpane-aurora-4074.json and desktop-aurora-4074.json (aurora_layers.py in the
              research workspace, default ~/fun/longhorn/aurora; spec: aurora.md there)
  OUT.h       default examples/aurora_data.h next to this script
The header is checked in, so the build does not need Python. Per scene it holds:
  layers   draw order, path ops + points, tight geometry bbox, transform steps (application order),
           fill/stroke gradient, own opacity animation, enclosing opacity group
  paints   StartPoint/EndPoint in bounding-box space, the 2003 GradientTransform folded to a matrix
           (bbox space), stops as offset + 0xAARRGGBB (not premultiplied), and where the isolines are
           perpendicular: bbox space (pane) or path space (desktop, the JSON's "gradientIsolines": "path";
           2026-10-30 gradient-space fix, aurora.md 3.3)
  groups   the canvases with an opacity animation (none of them nest)
  anims    one entry per animation of the XAML (12 / 30): property, from, to (or key frames), duration,
           auto-reverse, begin; translate steps and opacities refer to them by index
"""
import json, math, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser('~/fun/longhorn/aurora')
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, 'aurora_data.h')
STEP = {'translate': 'AU_T', 'rotate': 'AU_R', 'scale': 'AU_S', 'skew': 'AU_K'}

def num(v): return f'{float(v):.10g}'

def parse_d(d):
    toks = re.findall(r'[MLCZ]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?', d)
    ops, pts, i, cur = '', [], 0, None
    n = {'M': 2, 'L': 2, 'C': 6, 'Z': 0}
    while i < len(toks):
        if toks[i] in n: cur = toks[i]; i += 1
        ops += cur; pts += [float(t) for t in toks[i:i + n[cur]]]; i += n[cur]
        if cur == 'M': cur = 'L'
    return ops, pts

def bbox(ops, pts):
    """tight geometry bounds: end points plus the cubic extrema (not the control points)"""
    xs, ys, k, cx, cy = [], [], 0, 0, 0
    for op in ops:
        if op in 'ML': cx, cy = pts[k], pts[k + 1]; xs.append(cx); ys.append(cy); k += 2
        elif op == 'C':
            p = [(cx, cy), (pts[k], pts[k + 1]), (pts[k + 2], pts[k + 3]), (pts[k + 4], pts[k + 5])]
            for ax, out in ((0, xs), (1, ys)):
                a, b, c, d = (q[ax] for q in p)
                A, B, C = -a + 3 * b - 3 * c + d, 2 * (a - 2 * b + c), b - a   # derivative / 3
                roots = []
                if abs(A) < 1e-12:
                    if abs(B) > 1e-12: roots = [-C / B]
                else:
                    disc = B * B - 4 * A * C
                    if disc >= 0: roots = [(-B + s * math.sqrt(disc)) / (2 * A) for s in (1, -1)]
                out.append(d)
                for t in roots:
                    if 0 < t < 1: out.append((1 - t) ** 3 * a + 3 * (1 - t) ** 2 * t * b + 3 * (1 - t) * t * t * c + t ** 3 * d)
            cx, cy = pts[k + 4], pts[k + 5]; k += 6
    return min(xs), min(ys), max(xs) - min(xs), max(ys) - min(ys)

def matrix(steps):
    """step list (application order) -> cairo matrix xx yx xy yy x0 y0"""
    m = [1, 0, 0, 1, 0, 0]
    def mul(a, b):  # a then b
        return [a[0] * b[0] + a[1] * b[2], a[0] * b[1] + a[1] * b[3], a[2] * b[0] + a[3] * b[2], a[2] * b[1] + a[3] * b[3],
                a[4] * b[0] + a[5] * b[2] + b[4], a[4] * b[1] + a[5] * b[3] + b[5]]
    for s in steps:
        if s['type'] == 'translate': st = [1, 0, 0, 1, s['x'], s['y']]
        elif s['type'] == 'scale': st = [s['sx'], 0, 0, s['sy'], 0, 0]
        elif s['type'] == 'rotate':
            r = math.radians(s['deg']); st = [math.cos(r), math.sin(r), -math.sin(r), math.cos(r), 0, 0]
        else: st = [1, math.tan(math.radians(s['ay'])), math.tan(math.radians(s['ax'])), 1, 0, 0]
        if s.get('center', [0, 0]) != [0, 0]:
            cx, cy = s['center']; st = mul(mul([1, 0, 0, 1, -cx, -cy], st), [1, 0, 0, 1, cx, cy])
        m = mul(m, st)
    return m

def scene(name, fn, design):
    data = json.load(open(os.path.join(SRC, fn)))
    gx = {G['id']: G for G in data['groups']}
    pathspace = int(data.get('gradientIsolines') == 'path')   # [gradient-space fix]
    out, anims, akey = [], [], {}
    def add_anim(a, prop, target, key=None):
        if key is not None and key in akey: return akey[key]
        kv, kt = a.get('keyValues', []), a.get('keyTimes', [])
        assert len(kv) <= 3
        anims.append(f'    {{ "{target}", AU_{prop}, {num(a.get("from", 0))}, {num(a.get("to", 0))}, {num(a["dur"])}, '
                     f'{int(a["autoReverse"])}, {num(a.get("begin", 0))}, {len(kv)}, '
                     f'{{ {", ".join(num(v) for v in kt) or "0"} }}, {{ {", ".join(num(v) for v in kv) or "0"} }} }},')
        if key is not None: akey[key] = len(anims) - 1
        return len(anims) - 1
    groups, gidx = [], {}
    for G in data['groups']:
        if 'opacityAnim' in G:
            gidx[G['id']] = len(groups); groups.append(f'    {{ "{G["id"]}", {add_anim(G["opacityAnim"], "OPACITY", G["id"])} }},')
    # a translate X animation shared by several layers belongs to their canvas or decorator: one clock
    shared = {}
    for L in data['layers']:
        for s in L.get('transform', []):
            if 'animX' in s: shared.setdefault(json.dumps(s['animX'], sort_keys=True), []).append(L)
    layers, body = [], []
    for li, L in enumerate(data['layers']):
        if L['kind'] != 'path': continue
        lname = L.get('id') or f'{L["groups"][-1]}#{li + 1}'
        ops, pts = parse_d(L['d'])
        bx = bbox(ops, pts)
        p = f'{name}_{li}'
        body.append(f'static const double {p}_pts[] = {{ {", ".join(num(v) for v in pts)} }};')
        steps = []
        for s in L['transform']:
            ai = -1
            if 'animX' in s:
                k = json.dumps(s['animX'], sort_keys=True); owners = shared[k]
                tgt = next((G for G in L['groups'] if any(t.get('animX') == s['animX'] for t in gx[G]['transform'])), None)
                tgt = tgt or (f'{L["groups"][-1]} decorator' if len(owners) > 1 else lname)
                ai = add_anim(s['animX'], 'X', tgt, k)
            c = s.get('center', [0, 0])
            a, b = {'translate': (s.get('x'), s.get('y')), 'rotate': (s.get('deg'), 0), 'scale': (s.get('sx'), s.get('sy')),
                    'skew': (s.get('ax'), s.get('ay'))}[s['type']]
            steps.append(f'{{ {STEP[s["type"]]}, {num(a)}, {num(b)}, {num(c[0])}, {num(c[1])}, {ai} }}')
        body.append(f'static const struct au_step {p}_steps[] = {{ {", ".join(steps)} }};')
        paints = {}
        for kind in ('fill', 'stroke'):
            if kind not in L: continue
            B = L[kind]
            gt = matrix(B['transform']) if B['transform'] and not B['explicitPoints'] else [1, 0, 0, 1, 0, 0]
            stops = ', '.join(f'{{ {num(o)}, 0x{c[1:]} }}' for o, c in B['stops'])
            body.append(f'static const struct au_stop {p}_{kind}_stops[] = {{ {stops} }};')
            body.append(f'static const struct au_paint {p}_{kind} = {{ {num(B["start"][0])}, {num(B["start"][1])}, '
                        f'{num(B["end"][0])}, {num(B["end"][1])}, {{ {", ".join(num(v) for v in gt)} }}, '
                        f'{len(B["stops"])}, {p}_{kind}_stops, {pathspace} }};')
            paints[kind] = f'&{p}_{kind}'
        og = [G for G in L['groups'] if G in gidx]
        assert len(og) <= 1, 'nested opacity groups'
        op = add_anim(L['opacityAnim'], 'OPACITY', lname) if 'opacityAnim' in L else -1
        if L.get('visibility') == 'Collapsed': body.append(f'/* {lname}: Visibility=Collapsed, kept for completeness */')
        layers.append(f'    {{ "{lname}", "{ops}", {p}_pts, {{ {", ".join(num(v) for v in bx)} }}, {len(steps)}, {p}_steps, '
                      f'{paints.get("fill", "NULL")}, {paints.get("stroke", "NULL")}, {num(L.get("strokeThickness", 1))}, '
                      f'{op}, {gidx[og[0]] if og else -1}, {int(L.get("visibility") == "Collapsed")} }},')
    return (f'/* ---- {name}: {data["source"]} ({len(layers)} paths, {len(anims)} animations) ---- */\n' + '\n'.join(body) +
            f'\nstatic const struct au_anim {name}_anims[] = {{\n' + '\n'.join(anims) + '\n};\n' +
            f'static const struct au_group {name}_groups[] = {{\n' + ('\n'.join(groups) or '    { "", -1 },') + '\n};\n' +
            f'static const struct au_layer {name}_layers[] = {{\n' + '\n'.join(layers) + '\n};\n' +
            f'static const struct au_scene {name}_scene = {{ {design[0]}, {design[1]}, {len(layers)}, {name}_layers, '
            f'{len(groups)}, {name}_groups, {len(anims)}, {name}_anims }};\n'), len(anims)

HDR = '''/* GENERATED by examples/aurora_data.py from %s/{previewpane,desktop}-aurora-4074.json -- do not edit.
   Longhorn's two aurora scenes as numbers (spec: aurora.md in the research workspace): the Explorer preview-pane aurora
   (jade.msstyles STREAM 481, 1080x160) and the desktop aurora (WCPClient desktopaurora.baml, 800x600).
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

pane, na = scene('pane', 'previewpane-aurora-4074.json', (1080, 160))
desk, nb = scene('desktop', 'desktop-aurora-4074.json', (800, 600))
assert (na, nb) == (12, 30), (na, nb)
src = SRC.replace(os.path.expanduser('~'), '~')
open(OUT, 'w').write(HDR % src + pane + '\n' + desk + '\n#endif\n')
