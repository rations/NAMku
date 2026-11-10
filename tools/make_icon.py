#!/usr/bin/env python3
"""Generate NAMku's icon as HVIF: the letters NAM in steel, in 3D, on an angle.

HVIF is Haiku's vector icon format (what Icon-O-Matic exports and what
BIconUtils renders). The encoder and the look (soft shadow, dark outline, a
body lit from the top left, gloss) are those of Kasen's tools/make_icons.py.
Writes:

  standalone/icons/namku.hvif       open in Icon-O-Matic to edit
  standalone/icons/namku_icon.rdef  the vector_icon resource NAMku.rdef includes

Run from the repository root:  python3 tools/make_icon.py
"""

import math
import os
import struct
import sys

# ---- format constants (FlatIconFormat.h) -----------------------------------

STYLE_TYPE_SOLID_COLOR = 1
STYLE_TYPE_GRADIENT = 2
SHAPE_TYPE_PATH_SOURCE = 10
TRANSFORMER_TYPE_STROKE = 23

GRADIENT_LINEAR = 0
GRADIENT_CIRCULAR = 1
GRADIENT_FLAG_TRANSFORM = 1 << 1

PATH_FLAG_CLOSED = 1 << 1
PATH_FLAG_NO_CURVES = 1 << 3

SHAPE_FLAG_HAS_TRANSFORMERS = 1 << 4
SHAPE_FLAG_TRANSLATION = 1 << 5

# agg line joins / caps, as the stroke transformer stores them
JOIN_MITER, JOIN_ROUND = 0, 2
CAP_BUTT, CAP_ROUND = 0, 2

MAGIC = 0x6669636E  # 'ficn', written little-endian


def write_coord(out, c):
    """FlatIconFormat.cpp write_coord: 7 bits for small whole numbers,
    else 15 bits of (c + 128) * 102 with the high bit set."""
    c = max(-128.0, min(192.0, c))
    if int(c * 100.0) == int(c) * 100 and -32.0 <= c <= 95.0:
        out.append(int(c + 32.0))
    else:
        v = int((c + 128.0) * 102.0) | 0x8000
        out.append(v >> 8)
        out.append(v & 0xFF)


def write_float24(out, value):
    """FlatIconFormat.cpp write_float_24: 1 sign, 6 exponent, 17 mantissa."""
    bits = struct.unpack("<I", struct.pack("<f", value))[0]
    sign = (bits & 0x80000000) >> 31
    exponent = ((bits & 0x7F800000) >> 23) - 127
    mantissa = bits & 0x007FFFFF
    if exponent >= 32 or exponent < -32:
        out += b"\0\0\0"
        return
    short = (sign << 23) | ((exponent + 32) << 17) | (mantissa >> 6)
    out += bytes(((short >> 16) & 0xFF, (short >> 8) & 0xFF, short & 0xFF))


class Icon:
    def __init__(self):
        self.styles = []
        self.paths = []
        self.shapes = []

    # -- styles --
    def solid(self, rgba):
        b = bytearray([STYLE_TYPE_SOLID_COLOR])
        b += bytes(rgba)
        self.styles.append(bytes(b))
        return len(self.styles) - 1

    def _gradient(self, gtype, matrix, stops):
        b = bytearray([STYLE_TYPE_GRADIENT, gtype, GRADIENT_FLAG_TRANSFORM, len(stops)])
        for v in matrix:
            write_float24(b, v)
        for offset, rgba in stops:
            b.append(int(round(offset * 255)))
            b += bytes(rgba)
        self.styles.append(bytes(b))
        return len(self.styles) - 1

    def linear(self, p0, p1, stops):
        """Gradient along p0 -> p1. The renderer runs a linear gradient over
        x = -64..64 in gradient space (IconRenderer.cpp); the matrix maps
        that span onto p0..p1. Matrix order is agg's: sx shy shx sy tx ty."""
        dx, dy = p1[0] - p0[0], p1[1] - p0[1]
        cx, cy = (p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2
        m = (dx / 128.0, dy / 128.0, -dy / 128.0, dx / 128.0, cx, cy)
        return self._gradient(GRADIENT_LINEAR, m, stops)

    def radial(self, center, radius, stops):
        """Circular gradient: radius 0..64 in gradient space."""
        s = radius / 64.0
        return self._gradient(GRADIENT_CIRCULAR, (s, 0.0, 0.0, s, center[0], center[1]), stops)

    # -- paths --
    def polygon(self, pts):
        b = bytearray([PATH_FLAG_CLOSED | PATH_FLAG_NO_CURVES, len(pts)])
        for x, y in pts:
            write_coord(b, x)
            write_coord(b, y)
        self.paths.append(bytes(b))
        return len(self.paths) - 1

    def curve_path(self, pts, closed):
        """pts: (point, control_in, control_out) triples."""
        b = bytearray([PATH_FLAG_CLOSED if closed else 0, len(pts)])
        for p, cin, cout in pts:
            for x, y in (p, cin, cout):
                write_coord(b, x)
                write_coord(b, y)
        self.paths.append(bytes(b))
        return len(self.paths) - 1

    def circle(self, c, r):
        return self.ellipse(c, r, r)

    def ellipse(self, c, rx, ry):
        kx, ky = 0.5523 * rx, 0.5523 * ry
        x, y = c
        return self.curve_path(
            [
                ((x, y - ry), (x - kx, y - ry), (x + kx, y - ry)),
                ((x + rx, y), (x + rx, y - ky), (x + rx, y + ky)),
                ((x, y + ry), (x + kx, y + ry), (x - kx, y + ry)),
                ((x - rx, y), (x - rx, y + ky), (x - rx, y - ky)),
            ],
            True,
        )

    def open_path(self, pts):
        """An open path from (point, in, out) triples (for strokes)."""
        return self.curve_path(pts, False)

    # -- shapes --
    def shape(self, style, paths, stroke=None, translate=None):
        flags = 0
        if stroke:
            flags |= SHAPE_FLAG_HAS_TRANSFORMERS
        if translate:
            flags |= SHAPE_FLAG_TRANSLATION
        b = bytearray([SHAPE_TYPE_PATH_SOURCE, style, len(paths)])
        b += bytes(paths)
        b.append(flags)
        if translate:
            write_coord(b, translate[0])
            write_coord(b, translate[1])
        if stroke:
            width, join, cap = stroke
            b.append(1)  # one transformer
            b += bytes([TRANSFORMER_TYPE_STROKE, int(width + 128), join | (cap << 4), 4])
        self.shapes.append(bytes(b))

    def encode(self):
        for name, items in (("styles", self.styles), ("paths", self.paths), ("shapes", self.shapes)):
            if len(items) > 255:
                raise ValueError("too many " + name)
        out = bytearray(struct.pack("<I", MAGIC))
        for items in (self.styles, self.paths, self.shapes):
            out.append(len(items))
            for it in items:
                out += it
        return bytes(out)


# ---- the look ----------------------------------------------------------------

SHADOW = (0, 0, 0, 70)
GLOSS_TOP = (255, 255, 255, 165)
GLOSS_END = (255, 255, 255, 0)

# (light, dark, outline) per colour family
PALETTES = {
    "green": ((196, 248, 160, 255), (34, 132, 40, 255), (8, 40, 10, 255)),
    "red": ((255, 150, 128, 255), (172, 12, 10, 255), (60, 0, 0, 255)),
    "blue": ((200, 218, 246, 255), (46, 78, 152, 255), (8, 18, 46, 255)),
    "amber": ((255, 226, 140, 255), (214, 118, 8, 255), (64, 30, 0, 255)),
    "steel": ((240, 240, 244, 255), (104, 106, 118, 255), (36, 36, 42, 255)),
    "graphite": ((150, 152, 164, 255), (38, 40, 48, 255), (10, 10, 14, 255)),
    "black": ((112, 114, 124, 255), (14, 14, 18, 255), (0, 0, 0, 255)),
    "paper": ((255, 255, 255, 255), (206, 208, 216, 255), (70, 72, 80, 255)),
}


def solid_glyph(icon, paths, family, bounds):
    """Filled glyph: shadow, outline, lit body, gloss on the upper half."""
    light, dark, outline = PALETTES[family]
    x0, y0, x1, y1 = bounds
    icon.shape(icon.solid(SHADOW), paths, translate=(2, 3))
    icon.shape(icon.solid(outline), paths, stroke=(4, JOIN_ROUND, CAP_ROUND))
    body = icon.linear((x0, y0), (x1, y1), [(0.0, light), (1.0, dark)])
    icon.shape(body, paths)
    mid = y0 + (y1 - y0) * 0.55
    gloss = icon.linear((x0, y0), (x0, mid), [(0.0, GLOSS_TOP), (1.0, GLOSS_END)])
    icon.shape(gloss, paths)


def stroked_glyph(icon, paths, family, bounds, width, heads=None):
    """A thick line glyph (rings, loop arrows): the outline is a wider stroke
    under the coloured stroke; optional filled arrowheads ride on top."""
    light, dark, outline = PALETTES[family]
    x0, y0, x1, y1 = bounds
    shadow = icon.solid(SHADOW)
    icon.shape(shadow, paths, stroke=(width, JOIN_ROUND, CAP_ROUND), translate=(2, 3))
    if heads:
        icon.shape(shadow, heads, translate=(2, 3))
    out = icon.solid(outline)
    icon.shape(out, paths, stroke=(width + 4, JOIN_ROUND, CAP_ROUND))
    if heads:
        icon.shape(out, heads, stroke=(4, JOIN_ROUND, CAP_ROUND))
    body = icon.linear((x0, y0), (x1, y1), [(0.0, light), (1.0, dark)])
    icon.shape(body, paths, stroke=(width, JOIN_ROUND, CAP_ROUND))
    if heads:
        icon.shape(body, heads)
    mid = y0 + (y1 - y0) * 0.5
    gloss = icon.linear((x0, y0), (x0, mid), [(0.0, GLOSS_TOP), (1.0, GLOSS_END)])
    icon.shape(gloss, paths, stroke=(width, JOIN_ROUND, CAP_ROUND))




def area(pts):
    return 0.5 * sum(pts[i][0] * pts[(i + 1) % len(pts)][1] - pts[(i + 1) % len(pts)][0] * pts[i][1]
                     for i in range(len(pts)))


def oriented(pts, positive):
    return pts if (area(pts) > 0) == positive else pts[::-1]


# ---- NAMku: "NAM" in 3D --------------------------------------------------------

# Block letters, y down, 20 high. (outline, holes, advance)
LETTERS = [
    ([(0, 0), (5, 0), (11, 11), (11, 0), (16, 0), (16, 20), (11, 20), (5, 9), (5, 20), (0, 20)],
     [], 16),
    ([(6, 0), (12, 0), (18, 20), (13, 20), (11.8, 16), (6.2, 16), (5, 20), (0, 20)],
     [[(9, 6.7), (10.6, 12), (7.4, 12)]], 18),
    ([(0, 0), (5, 0), (10, 8), (15, 0), (20, 0), (20, 20), (15, 20), (15, 9), (10, 16), (5, 9),
      (5, 20), (0, 20)], [], 20),
]
GAP = 2.5


def icon_namku(tilt=-16, persp=0.28, depth=(2.6, 3.6)):
    total = sum(adv for _, _, adv in LETTERS) + GAP * (len(LETTERS) - 1)
    a = math.radians(tilt)

    def P(x, y):
        u = x / total
        h = 1 - persp * u            # the far end is smaller
        X = x * (1 - persp * 0.5 * u)
        Y = (y - 10) * 1.45 * h
        return (X * math.cos(a) - Y * math.sin(a), X * math.sin(a) + Y * math.cos(a))

    contours = []   # (points, is_hole) in raw transformed space
    x0 = 0
    for outline, holes, adv in LETTERS:
        contours.append(([P(x0 + x, y) for x, y in outline], False))
        for hole in holes:
            contours.append(([P(x0 + x, y) for x, y in hole], True))
        x0 += adv + GAP

    # Fit into the icon: scale and centre (the extrusion included).
    xs = [p[0] for c, _ in contours for p in c]
    ys = [p[1] for c, _ in contours for p in c]
    w, hgt = max(xs) - min(xs), max(ys) - min(ys)
    s = min((60 - depth[0]) / w, (52 - depth[1]) / hgt)
    ox = (64 - (w * s + depth[0])) / 2 - min(xs) * s
    oy = (62 - (hgt * s + depth[1])) / 2 - min(ys) * s
    contours = [([(ox + px * s, oy + py * s) for px, py in c], hole) for c, hole in contours]
    dx, dy = depth

    i = Icon()
    cx = 32
    by = max(p[1] for c, _ in contours for p in c) + dy

    # Front faces: outer positive, holes negative (non-zero fill leaves holes).
    faces = []
    for c, hole in contours:
        faces.append(i.polygon(oriented(c, not hole)))

    # Side walls: one quad per edge facing the extrusion direction.
    buckets = {0: [], 1: [], 2: []}
    allquads = []
    for c, hole in contours:
        sgn = 1 if area(c) > 0 else -1
        if hole:
            sgn = -sgn
        for k in range(len(c)):
            p, q = c[k], c[(k + 1) % len(c)]
            ex, ey = q[0] - p[0], q[1] - p[1]
            n = (ey * sgn, -ex * sgn)
            ln = math.hypot(*n) or 1
            n = (n[0] / ln, n[1] / ln)
            if n[0] * dx + n[1] * dy <= 0:
                continue
            quad = i.polygon(oriented([p, q, (q[0] + dx, q[1] + dy), (p[0] + dx, p[1] + dy)], True))
            allquads.append(quad)
            ang = math.degrees(math.atan2(n[1], n[0]))   # 0 = facing right, 90 = down
            buckets[0 if ang < 30 else 1 if ang < 60 else 2].append(quad)

    everything = faces + allquads
    i.shape(i.solid(SHADOW), everything, translate=(2, 3))
    i.shape(i.solid((0, 0, 0, 255)), everything, stroke=(3, JOIN_ROUND, CAP_ROUND))
    side = {0: (96, 98, 110, 255), 1: (62, 64, 74, 255), 2: (34, 35, 42, 255)}
    for b in (2, 1, 0):
        if buckets[b]:
            i.shape(i.solid(side[b]), buckets[b])

    fx0 = min(p[0] for c, _ in contours for p in c)
    fy0 = min(p[1] for c, _ in contours for p in c)
    fx1 = max(p[0] for c, _ in contours for p in c)
    fy1 = max(p[1] for c, _ in contours for p in c)
    body = i.linear((fx0, fy0), (fx0 + (fx1 - fx0) * 0.6, fy1),
                    [(0.0, (252, 252, 255, 255)), (0.55, (186, 188, 198, 255)), (1.0, (122, 124, 136, 255))])
    i.shape(body, faces)
    mid = fy0 + (fy1 - fy0) * 0.5
    gloss = i.linear((fx0, fy0), (fx0, mid), [(0.0, GLOSS_TOP), (1.0, GLOSS_END)])
    i.shape(gloss, faces)
    i.shape(i.solid((20, 20, 26, 200)), faces, stroke=(1, JOIN_MITER, CAP_BUTT))
    return i



def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    outdir = os.path.join(root, "standalone", "icons")
    os.makedirs(outdir, exist_ok=True)
    data = icon_namku().encode()
    with open(os.path.join(outdir, "namku.hvif"), "wb") as f:
        f.write(data)
    rdef = [
        "// Generated by tools/make_icon.py; edit the script (or namku.hvif in",
        "// Icon-O-Matic and convert it back) instead.",
        "resource vector_icon {",
    ]
    for k in range(0, len(data), 32):
        rdef.append('\t$"%s"' % data[k:k + 32].hex().upper())
    rdef.append("};")
    rdef.append("")
    with open(os.path.join(outdir, "namku_icon.rdef"), "w") as f:
        f.write("\n".join(rdef))
    print("namku icon: %d bytes" % len(data))
    return 0


if __name__ == "__main__":
    sys.exit(main())
