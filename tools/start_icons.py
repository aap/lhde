#!/usr/bin/env python3
"""lhde's own Start button icons (Shell.start_icon = lhde | linux | freebsd: docs/THEME-FORMAT.md section 2), drawn here in
code and written as PNGs into share/icons/flags/NAME-SIZE.png at 16, 20, 24 and 32 px.

usage: start_icons.py [OUTDIR]        (default: share/icons/flags beside this script's tools/)

These are original drawings for lhde, made of circles, ellipses, rounded boxes and polygons placed below; nothing is traced
from or modelled on anyone's logo or mascot. They are flat, in the manner of XP's 16-colour-era icons: a few solid colours, a
1 px dark outline around each figure, anti-aliased edges, straight alpha. The output is deterministic (no timestamps), so
running the script again gives the same bytes.

  lhde     two overlapping application windows, a green one behind and up to the right, a blue one in front, each with a
           caption band of its own colour over a pale body: the thing a desktop is made of. At 16 px it is two outlined
           boxes with coloured tops, which reads as "windows" without being anyone's four-pane flag.
  linux    a penguin seen from the front: a dark round body and head, a white belly, white eyes, an orange beak and two
           orange feet. A generic penguin, not Tux's drawing: no flippers, no shading, no particular expression.
  freebsd  a red daemon's face: a round red head with two horns curving outwards, white eyes with dark pupils and a grin.
           Eyes and a mouth make it a face (FreeBSD's own logo is a faceless glossy ball, Beastie a whole figure with a
           trident and shoes; neither is drawn or copied here); at 16 px the horns and the eyes still read.
"""
import math
import os
import struct
import sys
import zlib

SIZES = (16, 20, 24, 32)
SS = 5                                  # supersamples per axis


# ---- shapes: signed distance in design units (a 32 x 32 grid), negative inside

def circle(cx, cy, r):
    return lambda x, y: math.hypot(x - cx, y - cy) - r


def ellipse(cx, cy, rx, ry):
    # the scaled-circle approximation, good enough for the mild ellipses here
    return lambda x, y: (math.hypot((x - cx) / rx, (y - cy) / ry) - 1) * min(rx, ry)


def rbox(x0, y0, x1, y1, r):
    cx, cy, hx, hy = (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2 - r, (y1 - y0) / 2 - r

    def d(x, y):
        qx, qy = abs(x - cx) - hx, abs(y - cy) - hy
        return math.hypot(max(qx, 0), max(qy, 0)) + min(max(qx, qy), 0) - r
    return d


def polygon(pts):
    def d(x, y):
        best, inside = 1e9, False
        n = len(pts)
        for i in range(n):
            ax, ay = pts[i]
            bx, by = pts[(i + 1) % n]
            ex, ey = bx - ax, by - ay
            t = max(0, min(1, ((x - ax) * ex + (y - ay) * ey) / (ex * ex + ey * ey)))
            best = min(best, math.hypot(x - ax - t * ex, y - ay - t * ey))
            if (ay > y) != (by > y) and x < ax + (y - ay) * ex / (ey or 1e-12):
                inside = not inside
        return -best if inside else best
    return d


def arc(cx, cy, r, w, y_min):
    """a stroke of half-width w along a circle of radius r, the part below y_min"""
    def d(x, y):
        ring = abs(math.hypot(x - cx, y - cy) - r) - w
        return max(ring, y_min - y)
    return d


def union(*fs):
    return lambda x, y: min(f(x, y) for f in fs)


def clip(f, g):
    """f where g is too"""
    return lambda x, y: max(f(x, y), g(x, y))


def grow(f, px, k):
    """f grown by px screen pixels (k: pixels per design unit): the outline's shape"""
    return lambda x, y: f(x, y) - px / k


# ---- the drawings: a list of (shape, colour), painted in order; "outline" entries are the figure grown by 1 px

def rgb(s):
    return tuple(int(s[i:i + 2], 16) for i in (1, 3, 5))


def lhde(k):
    ink = rgb("#10295a")
    back = rbox(11.0, 1.8, 30.4, 19.5, 2.2)
    back_cap = clip(back, lambda x, y: y - 7.0)
    front = rbox(1.6, 11.0, 23.0, 30.4, 2.2)
    front_cap = clip(front, lambda x, y: y - 16.4)
    return [
        (grow(back, 1, k), ink), (back, rgb("#e2f3d4")), (back_cap, rgb("#3f9e2e")),
        (grow(front, 1, k), ink), (front, rgb("#f4f7ff")), (front_cap, rgb("#2163d6")),
    ]


def linux(k):
    ink = rgb("#000000")
    head = circle(16, 9.8, 7.2)
    body = ellipse(16, 19.2, 9.6, 10.6)
    feet = union(ellipse(10.2, 28.4, 5.0, 2.1), ellipse(21.8, 28.4, 5.0, 2.1))
    beak = ellipse(16, 12.9, 3.6, 1.9)
    figure = union(head, body, feet, beak)
    eye_r = 2.2 if k >= 0.7 else 2.6           # bigger at 16 and 20 px, where a 2.2 unit eye would be a grey speck
    eyes = union(circle(13.0, 8.6, eye_r), circle(19.0, 8.6, eye_r))
    layers = [(grow(figure, 1, k), ink), (union(head, body), rgb("#24272b")),
              (ellipse(16, 21.5, 6.6, 8.2), rgb("#f7f4ea")), (eyes, rgb("#ffffff"))]
    if k >= 0.7:                                 # pupils from 24 px up
        layers.append((union(circle(13.5, 9.0, 1.1), circle(18.5, 9.0, 1.1)), rgb("#000000")))
    layers += [(feet, rgb("#f2a612")), (beak, rgb("#f7b21a"))]
    return layers


def freebsd(k):
    ink = rgb("#4a0805")
    head = circle(16, 18.6, 11.2)
    horn = [(7.6, 13.0), (12.9, 9.0), (10.2, 6.6), (7.4, 4.0), (3.4, 1.2), (4.4, 5.2), (5.6, 8.8)]   # base on the head, tip out
    left = polygon(horn)
    right = polygon([(32 - x, y) for (x, y) in horn])
    figure = union(head, left, right)
    small = k < 0.7
    eyes = union(ellipse(11.9, 16.6, 2.9 if small else 2.6, 3.4 if small else 3.1),
                 ellipse(20.1, 16.6, 2.9 if small else 2.6, 3.4 if small else 3.1))
    pupils = union(circle(12.6, 17.4, 1.5 if small else 1.3), circle(20.8, 17.4, 1.5 if small else 1.3))
    grin = arc(16, 19.4, 6.2, 1.0 if small else 0.8, 22.4)
    return [(grow(figure, 1, k), ink), (figure, rgb("#d6271b")), (eyes, rgb("#ffffff")), (pupils, rgb("#1a0402")),
            (grin, rgb("#4a0805"))]


DRAWINGS = {"lhde": lhde, "linux": linux, "freebsd": freebsd}


# ---- rendering and PNG output

def render(draw, size):
    k = size / 32.0
    layers = draw(k)
    out = []
    for py in range(size):
        row = []
        for px in range(size):
            # straight-alpha "over" of every layer, per pixel, from coverage by supersampling
            cr = cg = cb = ca = 0.0                     # premultiplied accumulators
            for shape, col in layers:
                cov = 0
                for sy in range(SS):
                    for sx in range(SS):
                        x = (px + (sx + 0.5) / SS) / k
                        y = (py + (sy + 0.5) / SS) / k
                        if shape(x, y) < 0:
                            cov += 1
                a = cov / (SS * SS)
                if a:
                    cr = col[0] * a + cr * (1 - a)
                    cg = col[1] * a + cg * (1 - a)
                    cb = col[2] * a + cb * (1 - a)
                    ca = a + ca * (1 - a)
            if ca > 0:
                row.append((round(cr / ca), round(cg / ca), round(cb / ca), round(ca * 255)))
            else:
                row.append((0, 0, 0, 0))
        out.append(row)
    return out


def png(rows):
    h, w = len(rows), len(rows[0])
    raw = b"".join(b"\0" + bytes(c for p in r for c in p) for r in rows)

    def chunk(t, data):
        return struct.pack(">I", len(data)) + t + data + struct.pack(">I", zlib.crc32(t + data) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main(argv):
    out = argv[1] if len(argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "share", "icons", "flags")
    os.makedirs(out, exist_ok=True)
    for name, draw in DRAWINGS.items():
        for s in SIZES:
            path = os.path.join(out, "%s-%d.png" % (name, s))
            with open(path, "wb") as f:
                f.write(png(render(draw, s)))
            print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
