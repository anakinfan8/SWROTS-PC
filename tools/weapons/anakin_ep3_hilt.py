#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build an Episode III hilt for Anakin's lightsaber (the game gives him his Episode II hilt).

    python tools/weapons/anakin_ep3_hilt.py <lsaberanakin.msh> <vosasaber.stx> <mods folder> [--obj] [--atlas]

The two input files come from the user's own copy of the game (dump them as in
docs/modding/dumping-assets.md, from meshes\\weapons\\lsaberanakin\\). They are used as templates only:
the model and the texture are generated here, and the parts of the file formats not understood are
copied from the inputs (tools/weapons/msh.py, stx.py).

Writes the hilt for both of Anakin's sabers (docs/modding/replacing-weapons.md):
    <mods>\\meshes\\weapons\\lsaberanakin\\lsaberanakin.msh, vosasaber.stx          story, hooded versus skin
    <mods>\\meshes\\weapons\\lsaberanakinduel\\lsaberanakinduel.msh, vosasaber.stx  default versus skin
--obj also writes anakin_ep3_hilt.obj (for Blender), --atlas the texture as anakin_ep3_hilt_atlas.png.

Proportions and the placement of the parts were measured from royce_the_giraffe_'s 1:1 printable model
("Anakin Skywalker Lightsaber - Episode 3", Printables, CC BY-NC-ND 4.0) and checked against photos of
the hero prop. Only measurements were taken: no geometry from that model is used.

Coordinates are the game's: the hilt's axis is +y, the blade leaves the top. The blade starts at the
saber's ATT_saber_base attachment point in the unchanged .gat (y = 4.27), so the emitter keeps the
original's height. Angles go round the axis: 0 = +z, 90 = +x, 180 = -z, 270 = -x.
"""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import msh  # noqa: E402
import stx  # noqa: E402

# --- where the parts sit round the hilt ----------------------------------------------------------------
PLATE_DEG = 180                      # etched plate on its chrome block
HOOK_SIDE = -1                       # gold hook: on the clamp box's side face, -1 = the side towards the button
PORT_DEG = PLATE_DEG - 28            # two-pin port
PORT2_DEG = PLATE_DEG - 152          # the second two-pin port, across the button from the first
BUTTON_DEG = PLATE_DEG - 90          # copper button in an oval chrome bezel
KNOB_DEG = PLATE_DEG + 90            # knurled knob with a copper end
CLIP_DEG = PLATE_DEG + 90            # the two-pronged clip in front of the emitter
HOOD_DEG = PLATE_DEG + 90            # the side where the emitter shell rises highest
BELT_KNOB_DEG = PLATE_DEG + 180      # black belt-clip knob between the grips, opposite the plate

# --- scale: the prop is 273 mm long; the game's hilt runs from y = -7.15 to about 4.65 (11.8 units) ------
MM = 11.8 / 273.1


def along(mm):
    """Height on the hilt's axis of a point `mm` millimetres from the pommel end."""
    return -7.15 + mm * MM


def size(mm):
    return mm * MM


N = 12                               # sides of the round parts
SHELL_N = 48                         # sides of the emitter shell, whose cut climbs steeply round the sides

# --- texture atlas: regions of the 128x128 texture, (x, y, w, h), multiples of 4 so DXT1 blocks never
#     straddle two regions ----------------------------------------------------------------------------------
REGIONS = {"smooth": (0, 0, 96, 32), "knurl": (0, 32, 96, 32), "black": (0, 64, 48, 32), "cap": (48, 64, 32, 32),
           "core": (0, 96, 48, 32), "ring": (96, 0, 16, 16), "port": (96, 16, 16, 16), "btn": (96, 32, 32, 32),
           "emit": (96, 64, 32, 32), "plate": (64, 96, 64, 32), "knob": (80, 64, 16, 32)}
INSET = 1.5                          # texels kept clear of a region's edge, against filtering bleed


def rect(name, fu, fv):
    """uv of the point (fu, fv) in 0..1 of a region."""
    x, y, w, h = REGIONS[name]
    return [(x + INSET + fu * (w - 2 * INSET)) / 128.0, (y + INSET + fv * (h - 2 * INSET)) / 128.0]


def disc(name, sx, sy):
    """uv of the point (sx, sy) in -1..1 of the circle inscribed in a region."""
    x, y, w, h = REGIONS[name]
    rho = min(w, h) / 2 - INSET
    return [(x + w / 2 + sx * rho) / 128.0, (y + h / 2 + sy * rho) / 128.0]


class Builder:
    """Collects vertices and triangles. Features are built facing +z and turned round the axis by `rot`.
    Triangles are wound so the game draws their outside: `sign` is the winding the original mesh uses
    (msh.outward_sign)."""

    def __init__(self, sign):
        self.V, self.normals, self.T, self.sign = [], [], [], sign
        self.rot = 0.0

    def vert(self, pos, uv, n):
        c, s = math.cos(self.rot), math.sin(self.rot)
        p = [float(v) for v in pos]
        q = [float(v) for v in n]
        p = [p[0] * c + p[2] * s, p[1], -p[0] * s + p[2] * c]
        q = [q[0] * c + q[2] * s, q[1], -q[0] * s + q[2] * c]
        length = math.sqrt(sum(v * v for v in q)) or 1.0
        q = [v / length for v in q]
        self.V.append(dict(pos=p, nb=msh.pack_normal(q), pad=0, uv=[float(v) for v in uv],
                           uv1=bytes(8), col=bytes([0, 0, 0, 255])))
        self.normals.append(q)
        return len(self.V) - 1

    def tri(self, a, b, c):
        A, B, C = (self.V[k]["pos"] for k in (a, b, c))
        u = [B[i] - A[i] for i in range(3)]
        w = [C[i] - A[i] for i in range(3)]
        cross = [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]]
        n = [sum(self.normals[k][i] for k in (a, b, c)) for i in range(3)]
        d = self.sign * sum(cross[i] * n[i] for i in range(3))
        if abs(d) < 1e-12:
            return
        self.T.append((a, b, c) if d > 0 else (a, c, b))

    def quad(self, a, b, c, d):
        self.tri(a, b, c)
        self.tri(a, c, d)


# --- shapes ------------------------------------------------------------------------------------------------
def lathe(b, y0, y1, r0, r1, region, ytop=None, n=N):
    """A ring of the hilt's body from (y0, r0) to (y1, r1); ytop(angle) can slant its top edge."""
    length = math.hypot(r1 - r0, y1 - y0)
    nr, ny = (y1 - y0) / length, -(r1 - r0) / length
    rings = []
    for end, (y, r, fv) in enumerate(((y0, r0, 1.0), (y1, r1, 0.0))):
        ring = []
        for k in range(n + 1):
            th = 2 * math.pi * k / n
            s, c = math.sin(th), math.cos(th)
            yy = ytop(th) if (end == 1 and ytop) else y
            ring.append(b.vert((r * s, yy, r * c), rect(region, k / n, fv), (nr * s, ny, nr * c)))
        rings.append(ring)
    for k in range(n):
        b.quad(rings[0][k], rings[0][k + 1], rings[1][k + 1], rings[1][k])


def knurl_band(b, y0, y1, r, repeats=2):
    """The knurled band: a cylinder whose texture wraps `repeats` times around, so its studs come out
    square (the texture region is three times wider than it is tall). Each segment has its own vertices,
    so the texture can restart where a repeat ends."""
    per = N // repeats
    for k in range(N):
        t0, t1 = 2 * math.pi * k / N, 2 * math.pi * (k + 1) / N
        u0, u1 = (k % per) / per, (k % per + 1) / per
        quad = []
        for th, u, y, fv in ((t0, u0, y0, 1.0), (t1, u1, y0, 1.0), (t1, u1, y1, 0.0), (t0, u0, y1, 0.0)):
            s_, c_ = math.sin(th), math.cos(th)
            quad.append(b.vert((r * s_, y, r * c_), rect("knurl", u, fv), (s_, 0, c_)))
        b.quad(*quad)


def lathe_inner(b, y0, r, ytop, region, n=N):
    """The inside wall of the emitter shell (normals pointing in)."""
    rings = []
    for end in (0, 1):
        ring = []
        for k in range(n + 1):
            th = 2 * math.pi * k / n
            s, c = math.sin(th), math.cos(th)
            yy = y0 if end == 0 else ytop(th)
            ring.append(b.vert((r * s, yy, r * c), rect(region, k / n, 1.0 - end), (-s, 0, -c)))
        rings.append(ring)
    for k in range(n):
        b.quad(rings[0][k], rings[0][k + 1], rings[1][k + 1], rings[1][k])


def rim(b, r_out, r_in, ytop, region, n=N):
    """The top edge of the emitter shell, between its outside and inside walls. Where the edge climbs
    steeply the face tilts with it, so its normal follows the edge's slope."""
    outer, inner = [], []
    for k in range(n + 1):
        th = 2 * math.pi * k / n
        s, c = math.sin(th), math.cos(th)
        e = 1e-3
        r_mid = (r_out + r_in) / 2
        slope = (ytop(th + e) - ytop(th - e)) / (2 * e * r_mid)          # rise per unit of arc length
        nrm = (-slope * c, 1.0, slope * s)                                # up, tipped against the climb
        outer.append(b.vert((r_out * s, ytop(th), r_out * c), rect(region, k / n, 0), nrm))
        inner.append(b.vert((r_in * s, ytop(th), r_in * c), rect(region, k / n, .1), nrm))
    for k in range(n):
        b.quad(outer[k], outer[k + 1], inner[k + 1], inner[k])


def cap(b, r, ycap, normal, region, n=N):
    """A flat disc across the axis; ycap(x, z) gives its height (a slanted disc for a cut)."""
    centre = b.vert((0, ycap(0, 0), 0), disc(region, 0, 0), normal)
    ring = []
    for k in range(n + 1):
        s, c = math.sin(2 * math.pi * k / n), math.cos(2 * math.pi * k / n)
        ring.append(b.vert((r * s, ycap(r * s, r * c), r * c), disc(region, s, c), normal))
    for k in range(n):
        b.tri(centre, ring[k], ring[k + 1])


def cylinder(b, origin, axis, up, r, h0, h1, side, end, n=12, stretch=1.0):
    """A cylinder along any axis, closed at h1 (buttons, knobs, ports); `stretch` makes it oval along `up`."""
    a = np.array(axis, float)
    u = np.array(up, float)
    v = np.cross(a, u)
    o = np.array(origin, float)

    def ring_points(h):
        pts = []
        for k in range(n + 1):
            ph = 2 * math.pi * k / n
            pos = o + a * h + (math.cos(ph) * u * stretch + math.sin(ph) * v) * r
            nrm = math.cos(ph) * u / stretch + math.sin(ph) * v
            pts.append((pos, nrm, k))
        return pts

    rings = []
    for h, fv in ((h0, 1.0), (h1, 0.0)):
        rings.append([b.vert(p, rect(side, k / n, fv), nr) for p, nr, k in ring_points(h)])
    for k in range(n):
        b.quad(rings[0][k], rings[0][k + 1], rings[1][k + 1], rings[1][k])
    centre = b.vert(o + a * h1, disc(end, 0, 0), a)
    top = [b.vert(p, disc(end, math.cos(2 * math.pi * k / n), math.sin(2 * math.pi * k / n)), a)
           for p, nr, k in ring_points(h1)]
    for k in range(n):
        b.tri(centre, top[k], top[k + 1])


def spool(b, origin, axis, up, runs, region, n=24):
    """A body of revolution round `axis` from `origin`: `runs` is a list of profiles, each a list of
    (height, radius) points shaded smoothly along itself; corners between runs stay sharp."""
    a = np.array(axis, float)
    u = np.array(up, float)
    v = np.cross(a, u)
    o = np.array(origin, float)
    for run in runs:
        normals = []                                                # (radial, axial) of each profile point
        for i, (h, r) in enumerate(run):
            h0, r0 = run[max(i - 1, 0)]
            h1, r1 = run[min(i + 1, len(run) - 1)]
            dh, dr = h1 - h0, r1 - r0
            length = math.hypot(dh, dr) or 1.0
            normals.append((dh / length, -dr / length))
        rings = []
        for i, ((h, r), (nr, na)) in enumerate(zip(run, normals)):
            ring = []
            for k in range(n + 1):
                ph = 2 * math.pi * k / n
                radial = math.cos(ph) * u + math.sin(ph) * v
                ring.append(b.vert(o + a * h + radial * r, rect(region, k / n, i / max(len(run) - 1, 1)),
                                   radial * nr + a * na))
            rings.append(ring)
        for r0, r1 in zip(rings, rings[1:]):
            for k in range(n):
                b.quad(r0[k], r0[k + 1], r1[k + 1], r1[k])


def ribbed(b, origin, axis, up, r, h0, h1, region, n=24):
    """A cylinder whose texture repeats once per side, for fine straight ribs along its axis."""
    a = np.array(axis, float)
    u = np.array(up, float)
    v = np.cross(a, u)
    o = np.array(origin, float)
    for k in range(n):
        quad = []
        for kk, fu, h, fv in ((k, 0.0, h0, 1.0), (k + 1, 1.0, h0, 1.0), (k + 1, 1.0, h1, 0.0), (k, 0.0, h1, 0.0)):
            ph = 2 * math.pi * kk / n
            radial = math.cos(ph) * u + math.sin(ph) * v
            quad.append(b.vert(o + a * h + radial * r, rect(region, fu, fv), radial))
        b.quad(*quad)


def face(b, corners, uvs, n):
    b.quad(*[b.vert(c, uv, n) for c, uv in zip(corners, uvs)])


def box(b, x0, x1, y0, y1, z0, z1, regions):
    """A box; regions maps '+z', '-z', '+x', '-x', '+y', '-y' to a texture region (missing: no face)."""
    def R(name, fu, fv):
        return rect(regions[name], fu, fv)
    if "+z" in regions:
        face(b, [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)],
             [R("+z", 0, 1), R("+z", 1, 1), R("+z", 1, 0), R("+z", 0, 0)], (0, 0, 1))
    if "-z" in regions:
        face(b, [(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)],
             [R("-z", 0, 1), R("-z", 1, 1), R("-z", 1, 0), R("-z", 0, 0)], (0, 0, -1))
    if "+x" in regions:
        face(b, [(x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)],
             [R("+x", 0, 1), R("+x", 1, 1), R("+x", 1, 0), R("+x", 0, 0)], (1, 0, 0))
    if "-x" in regions:
        face(b, [(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)],
             [R("-x", 0, 1), R("-x", 1, 1), R("-x", 1, 0), R("-x", 0, 0)], (-1, 0, 0))
    if "+y" in regions:
        face(b, [(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)],
             [R("+y", 0, 1), R("+y", 1, 1), R("+y", 1, 0), R("+y", 0, 0)], (0, 1, 0))
    if "-y" in regions:
        face(b, [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)],
             [R("-y", 0, 1), R("-y", 1, 1), R("-y", 1, 0), R("-y", 0, 0)], (0, -1, 0))


def fins(b, count, y0, y1, ri, ro, half):
    """The grip's rubber fins, evenly spaced, each `half` either side of its centre angle."""
    for f in range(count):
        phi = 2 * math.pi * f / count + math.pi / count
        a0, a1 = phi - half, phi + half

        def P(a, r, y):
            return (r * math.sin(a), y, r * math.cos(a))
        out = (math.sin(phi), 0, math.cos(phi))
        b.quad(b.vert(P(a0, ro, y0), rect("black", 0, 1), out), b.vert(P(a1, ro, y0), rect("black", 1, 1), out),
               b.vert(P(a1, ro, y1), rect("black", 1, 0), out), b.vert(P(a0, ro, y1), rect("black", 0, 0), out))
        for a, sg in ((a0, -1), (a1, 1)):
            nt = (sg * math.cos(a), 0, -sg * math.sin(a))
            b.quad(b.vert(P(a, ri, y0), rect("black", 0, 1), nt), b.vert(P(a, ro, y0), rect("black", .2, 1), nt),
                   b.vert(P(a, ro, y1), rect("black", .2, 0), nt), b.vert(P(a, ri, y1), rect("black", 0, 0), nt))
        for y, ny in ((y1, 1), (y0, -1)):
            nn = (0, ny, 0)
            b.quad(b.vert(P(a0, ri, y), rect("black", 0, 0), nn), b.vert(P(a0, ro, y), rect("black", 0, .2), nn),
                   b.vert(P(a1, ro, y), rect("black", 1, .2), nn), b.vert(P(a1, ri, y), rect("black", 1, 0), nn))


def prism(b, profile, half_w, region="smooth", body_r=None, u_band=None, x_offset=0.0):
    """A convex outline in the (y, r) plane, extruded sideways by +-half_w (built facing +z). Edges
    inside the body (both ends at r <= body_r) get no face. u_band squeezes the texture's u into a
    narrow band, so small parts get an even finish instead of the chrome's stripes."""
    n = len(profile)
    if u_band is None:
        uv = rect
    else:
        def uv(reg, u, v):
            return rect(reg, u_band[0] + (u_band[1] - u_band[0]) * u, v)
    cy = sum(p[0] for p in profile) / n
    cr = sum(p[1] for p in profile) / n
    ys = [p[0] for p in profile]
    rs = [p[1] for p in profile]

    def fy(y):
        return (y - min(ys)) / ((max(ys) - min(ys)) or 1)

    def fr(r):
        return (r - min(rs)) / ((max(rs) - min(rs)) or 1)

    for sx in (-1, 1):
        nrm = (sx, 0, 0)
        centre = b.vert((x_offset + sx * half_w, cy, cr), uv(region, fy(cy), fr(cr)), nrm)
        ids = [b.vert((x_offset + sx * half_w, py, pr), uv(region, fy(py), fr(pr)), nrm) for py, pr in profile]
        for k in range(n):
            b.tri(centre, ids[k], ids[(k + 1) % n])
    for k in range(n):
        (ya, ra), (yb, rb) = profile[k], profile[(k + 1) % n]
        if body_r is not None and ra <= body_r + 1e-6 and rb <= body_r + 1e-6:
            continue
        ny, nr = (rb - ra), -(yb - ya)
        if ny * ((ya + yb) / 2 - cy) + nr * ((ra + rb) / 2 - cr) < 0:
            ny, nr = -ny, -nr
        nrm = (0, ny, nr)
        b.quad(b.vert((x_offset - half_w, ya, ra), uv(region, 0, k / n), nrm),
               b.vert((x_offset + half_w, ya, ra), uv(region, 1, k / n), nrm),
               b.vert((x_offset + half_w, yb, rb), uv(region, 1, (k + 1) / n), nrm),
               b.vert((x_offset - half_w, yb, rb), uv(region, 0, (k + 1) / n), nrm))


def arc(cy, cr, radius, a0, a1, seg):
    return [(cy + radius * math.cos(math.radians(a0 + (a1 - a0) * k / seg)),
             cr + radius * math.sin(math.radians(a0 + (a1 - a0) * k / seg))) for k in range(seg + 1)]


def rounded_rect(y0, y1, r0, r1, r_in, r_out, seg_in=3, seg_out=6):
    """Outline (in mm, converted) of a tab with rounded corners: r_in at the body, r_out at its tip."""
    pts = arc(y0 + r_in, r0 + r_in, r_in, 180, 270, seg_in)
    pts += arc(y1 - r_in, r0 + r_in, r_in, 270, 360, seg_in)
    pts += arc(y1 - r_out, r1 - r_out, r_out, 0, 90, seg_out)
    pts += arc(y0 + r_out, r1 - r_out, r_out, 90, 180, seg_out)
    return [(along(y), size(r)) for y, r in pts]


def curved_hook(b, side, y0=106.0, y1=139.0, z_mid=23.3, half_w=1.9, low=3.2, high=5.4, rise=(0.26, 0.79),
                r0=2.6, r1=3.6, n=64, around=10):
    """The gold hook on the clamp box's side face (x = side * 7.9 mm in the plate's frame), along the knurled
    band. Seen from above it is a straight bar of even width with round ends. Seen from the side its top is one
    even curve, as on the prop's lever: level at the grip end, rising smoothly to its highest point near the
    emitter end, with both ends rounded down to the box (radius r0 at the grip end, r1 at the emitter end).
    The profile follows the printable model's switch (credited at the top), smoothed and scaled to the game;
    `low` and `high` are its heights above the box at the grip end and at the top, in mm."""
    face = side * 7.9
    length = y1 - y0

    def height(y):
        u = (y - y0) / length
        s = min(max((u - rise[0]) / (rise[1] - rise[0]), 0.0), 1.0)
        h = low + (high - low) * s * s * (3 - 2 * s)                # level, an even rise, level
        for d, r in ((y - y0, r0), (y1 - y, r1)):                   # round ends
            if d < r:
                h *= math.sqrt(max(0.0, 1 - (1 - d / r) ** 2))
        return h

    def width(y):
        d = min(y - y0, y1 - y)
        return half_w * math.sqrt(max(0.0, 1 - (1 - d / half_w) ** 2)) if d < half_w else half_w

    def point(y, a):                                                # the surface, in mm
        return (face + side * height(y) * math.sin(a), y, z_mid - width(y) * math.cos(a))

    def normal(y, a):
        e = 1e-3
        ya, yb = max(y - e, y0), min(y + e, y1)
        p, q = point(ya, a), point(yb, a)
        dy = [(q[k] - p[k]) for k in range(3)]
        p, q = point(y, a - e), point(y, a + e)
        da = [(q[k] - p[k]) for k in range(3)]
        nrm = [dy[1] * da[2] - dy[2] * da[1], dy[2] * da[0] - dy[0] * da[2], dy[0] * da[1] - dy[1] * da[0]]
        if sum(v * v for v in nrm) < 1e-12:                         # the very tips: along the axis
            return (0.0, -1.0 if y < (y0 + y1) / 2 else 1.0, 0.0)
        out = side * math.sin(a)                                    # make it point away from the box
        if nrm[0] * out + nrm[2] * -math.cos(a) < 0:
            nrm = [-v for v in nrm]
        return nrm

    rings = []
    for i in range(n + 1):                                          # closer together towards the ends
        y = y0 + length * (1 - math.cos(math.pi * i / n)) / 2
        t = (y - y0) / length
        ring = []
        for j in range(around + 1):
            a = math.pi * j / around
            px, py, pz = point(y, a)
            ring.append(b.vert((size(px), along(py), size(pz)), rect("ring", t, j / around), normal(y, a)))
        rings.append(ring)
    for ra, rb in zip(rings, rings[1:]):
        for j in range(around):
            b.quad(ra[j], ra[j + 1], rb[j + 1], rb[j])


# --- the hilt ------------------------------------------------------------------------------------------------
def model(template):
    b = Builder(msh.outward_sign(template))
    rad = math.radians

    # body of revolution, pommel to emitter (mm from the pommel end, radius in mm)
    cap(b, size(18.3), lambda x, z: along(0), (0, -1, 0), "cap")          # pommel end
    for y0, y1, r0, r1, region in [
            (0, 93, 18.3, 18.3, "core"),                                     # chrome core under the grips
            (93, 96, 18.3, 20.4, "smooth"), (96, 107, 20.4, 20.4, "smooth"), (107, 110, 20.4, 19.6, "smooth"),
            (135, 138, 19.6, 20.4, "smooth"), (138, 148, 20.4, 20.4, "smooth"), (148, 151, 20.4, 18.3, "smooth"),
            (151, 233.4, 18.3, 18.3, "smooth")]:                             # plain tube
        lathe(b, along(y0), along(y1), size(r0), size(r1), region)
    knurl_band(b, along(110), along(135), size(19.6))                 # pyramid knurl
    hood = rad(HOOD_DEG)

    def shell_top(th):
        # the shell's cut: level across the hood (273 mm) for about 45 degrees either side, falling steeply
        # round the sides and level again across the open front (235 mm), as on the printable model
        d = abs((math.degrees(th - hood) + 180) % 360 - 180)
        return along(234.95 + 38.1 * 0.5 * (1 - math.tanh((d - 91) / 13.5)))
    lathe(b, along(233.4), along(254), size(18.3), size(18.3), "smooth", ytop=shell_top, n=SHELL_N)
    lathe_inner(b, along(233.4), size(15.0), shell_top, "smooth", n=SHELL_N)   # a thick wall, 3.3 mm
    rim(b, size(18.3), size(15.0), shell_top, "smooth", n=SHELL_N)
    cap(b, size(15.0), lambda x, z: along(233.4), (0, 1, 0), "black", n=SHELL_N)   # floor of the shell
    # the emitter: a narrower black mount (the dark half-ring seen through the opening), then a chrome cup
    # flared at both ends with a waist between, and the nozzle inside it
    lathe(b, along(233.4), along(239.71), size(11.9), size(11.9), "black", n=32)
    cup = [[(239.71, 11.9), (239.71, 13.5)], [(239.71, 13.5), (242.55, 11.5)],
           [(242.55, 11.5), (255.93, 11.5)], [(255.93, 11.5), (258.76, 13.5)],
           [(258.76, 13.5), (258.76, 10.3)],
           [(258.76, 10.3), (255.59, 8.0), (252.41, 7.1), (249.24, 6.4)]]
    spool(b, (0, 0, 0), (0, 1, 0), (0, 0, 1),
          [[(along(y), size(r)) for y, r in run] for run in cup], "smooth", n=32)
    cap(b, size(6.4), lambda x, z: along(249.24), (0, 1, 0), "ring", n=32)      # brass floor of the nozzle
    cylinder(b, (0, along(249.24), 0), (0, 1, 0), (0, 0, 1), size(2.3), 0.0, size(4.5), "ring", "ring")  # brass pin

    # six grips, centred 30, 90 and 150 degrees either side of the plate
    b.rot = rad(PLATE_DEG)
    fins(b, 6, along(5.6), along(91.3), size(13.0), size(24.7), rad(14.9))

    # chrome block with the etched plate on top
    box(b, -size(7.9), size(7.9), along(96.8), along(147.6), size(16), size(27.8),
        {"+z": "smooth", "+x": "smooth", "-x": "smooth", "+y": "smooth", "-y": "smooth"})
    x0, x1, y0, y1, top = -size(4.8), size(4.8), along(96.8), along(147.6), size(28.2)
    face(b, [(x0, y0, top), (x1, y0, top), (x1, y1, top), (x0, y1, top)],
         [rect("plate", 1, 0), rect("plate", 1, 1), rect("plate", 0, 1), rect("plate", 0, 0)], (0, 0, 1))
    box(b, x0, x1, y0, y1, size(27.8), top, {"+x": "smooth", "-x": "smooth", "+y": "smooth", "-y": "smooth"})

    # gold hook: a curved, rounded piece on the side face of the clamp box, level with the box
    b.rot = rad(PLATE_DEG)
    curved_hook(b, HOOK_SIDE)

    # two-pin ports either side of the button, the button, the knurled knob
    for angle in (PORT_DEG, PORT2_DEG):
        b.rot = rad(angle)
        cylinder(b, (0, along(225.5), size(15.5)), (0, 0, 1), (0, 1, 0), size(5.2), 0.0, size(3.0), "smooth", "port", n=10)
    b.rot = rad(BUTTON_DEG)
    cylinder(b, (0, along(214.6), size(16.0)), (0, 0, 1), (0, 1, 0), size(9.5), 0.0, size(3.9), "smooth", "btn",
             n=14, stretch=1.2)
    b.rot = rad(KNOB_DEG)
    # the knob: a ribbed band against the hilt, then a smooth chrome collar with a bevelled rim, and the
    # copper face set in it (heights from the axis; the body's surface is at 18.3 mm)
    knob = (0, along(220.7), 0)
    ribbed(b, knob, (0, 0, 1), (0, 1, 0), size(8.0), size(16.0), size(22.6), "knob")
    collar = [[(22.6, 8.0), (22.6, 8.4)], [(22.6, 8.4), (25.0, 8.4)], [(25.0, 8.4), (25.6, 7.8)],
              [(25.6, 7.8), (25.6, 5.9)]]
    spool(b, knob, (0, 0, 1), (0, 1, 0), [[(size(h), size(r)) for h, r in run] for run in collar], "smooth",
          n=24)
    cylinder(b, (0, along(220.7), size(25.3)), (0, 0, 1), (0, 1, 0), size(6.2), 0.0, size(0.6), "smooth", "btn",
             n=16)

    # clip in front of the emitter: two ears side by side with a 3 mm gap, each outlined like the prop's
    # clip (the printable model's, held to the shorter reach chosen in game): a straight top edge with a
    # raised shoulder at the body, a fully rounded outer end, and underneath a step and a sloping brace
    # back down to the body. Each ear is two convex pieces: the rounded tab, and the base with the brace.
    b.rot = rad(CLIP_DEG)
    w = size(7.9)
    gap = size(3.0)
    prong = (w - gap / 2) / 2
    top, bottom, reach = 261.25, 245.4, 37.0
    end_r = (top - bottom) / 2
    tab = [(bottom, 20.7), (top, 20.7)] + arc((top + bottom) / 2, reach - end_r, end_r, 0, 180, 12)
    base = [(240.0, 17.0), (263.5, 17.0), (263.5, 19.9), (top, 22.2), (bottom, 25.6), (243.0, 25.6),
            (240.0, 18.3)]
    for side in (-1, 1):
        for outline in (tab, base):
            prism(b, [(along(y), size(r)) for y, r in outline], prong, body_r=size(17.0), u_band=(.20, .26),
                  x_offset=side * (gap / 2 + prong))

    # black belt-clip knob between the grips
    b.rot = rad(BELT_KNOB_DEG)
    # a black spool, 19 mm across, standing a few mm proud of the grips: a base flange, a concave waist, an
    # outer flange with a chamfered rim, and a dished face with a screw in the middle (heights from the axis)
    waist = [(24.0 + 2.4 * s, 7.2 - 1.1 * math.sin(math.pi * s)) for s in (i / 8 for i in range(9))]
    dish = [(29.2 - 0.8 * (1 - (r - 1.8) / 3.2) ** 2, r) for r in (5.0 - 3.2 * i / 6 for i in range(7))]
    runs = [[(17.5, 9.5), (24.0, 9.5)],                                   # base flange
            [(24.0, 9.5), (24.0, 7.2)], waist, [(26.4, 7.2), (26.4, 9.5)],   # waist
            [(26.4, 9.5), (28.5, 9.5)], [(28.5, 9.5), (29.2, 8.8)],          # outer flange and its rim
            [(29.2, 8.8), (29.2, 5.0)], dish,                                 # face, dish
            [(28.4, 1.8), (28.8, 1.2)], [(28.8, 1.2), (28.8, 0.0)]]           # screw head
    spool(b, (0, along(38.1), 0), (0, 0, 1), (0, 1, 0),
          [[(size(h), size(r)) for h, r in run] for run in runs], "black")

    # drop vertices no triangle uses (the zero-width tips of the hook's rounded ends)
    used = sorted({i for t in b.T for i in t})
    new_index = {old: new for new, old in enumerate(used)}
    m = msh.Mesh().from_template(template)
    m.verts = [b.V[i] for i in used]
    m.tris = [tuple(new_index[i] for i in t) for t in b.T]
    return m


# --- the texture -----------------------------------------------------------------------------------------------
def chrome(X, w, base=132, amp=62):
    """Brightness across a chrome strip: soft reflections, one hot highlight and one dark band."""
    f = X / w
    lum = base + amp * np.sin(2 * np.pi * f * 2 + .5) + 18 * np.sin(2 * np.pi * f * 5 + 1.2)
    lum += 70 * np.exp(-((f - .28) / .035) ** 2) - 55 * np.exp(-((f - .72) / .06) ** 2)
    return lum


def paint():
    """The 128x128 atlas. Painted at 4x and box-filtered down; the pommel end and the emitter cup are
    drawn at full size afterwards."""
    S = 4
    rng = np.random.default_rng(9)
    A = np.zeros((512, 512, 3))
    A[:] = (140, 140, 144)

    def grid(name):
        x, y, w, h = [v * S for v in REGIONS[name]]
        X, Y = np.meshgrid(np.arange(w), np.arange(h))
        return x, y, w, h, X, Y

    def put(name, rgb):
        x, y, w, h, X, Y = grid(name)
        A[y:y + h, x:x + w] = np.clip(rgb, 0, 255)

    x, y, w, h, X, Y = grid("smooth")                                        # chrome tube
    lum = np.clip(chrome(X, w) + 8 * (.5 - Y / h) + rng.normal(0, 1.2, (h, w)), 55, 232)
    put("smooth", np.stack([lum, lum, lum + 2], 2))

    x, y, w, h, X, Y = grid("knurl")                                         # square-pyramid knurl
    # straight rows and columns of studs, one stud per 4x4 texel block of the final texture, so DXT1
    # compression keeps them crisp; each stud has four faces lit differently (light from the top left)
    u = (X % 16) / 16.0
    v = (Y % 16) / 16.0
    du, dv = u - .5, v - .5
    vertical = np.abs(dv) >= np.abs(du)
    face = np.where(vertical, np.where(dv < 0, 222.0, 92.0), np.where(du < 0, 186.0, 128.0))
    tip = np.maximum(np.abs(du), np.abs(dv)) < .08
    face = np.where(tip, 245.0, face)
    groove = np.maximum(np.abs(du), np.abs(dv)) > .46
    face = np.where(groove, 70.0, face)
    edge = np.clip(np.minimum(Y, h - 1 - Y) / 8., 0, 1)
    lum = np.clip(face * edge + (150 + 18 * np.sin(2 * np.pi * X / w * 2 + .9)) * (1 - edge)
                  + 10 * np.sin(2 * np.pi * X / w * 2 + .9) + rng.normal(0, 1.5, (h, w)), 40, 248)
    put("knurl", np.stack([lum, lum, lum + 2], 2))

    x, y, w, h, X, Y = grid("knob")                                          # the knob's straight ribs
    phase = ((X + .5) / (w / 2.0)) % 1.0                                     # two ribs across the region
    crest = np.sin(np.pi * phase) ** 1.5
    lum = np.clip(85 + 145 * crest + 12 * np.cos(2 * np.pi * Y / h) + rng.normal(0, 2, (h, w)), 45, 238)
    put("knob", np.stack([lum, lum, lum + 2], 2))

    x, y, w, h, X, Y = grid("black")                                         # rubber grips
    lum = 22 + 5 * np.sin(2 * np.pi * X / 24.) + 3 * np.sin(2 * np.pi * Y / 90.) + rng.normal(0, 1.2, (h, w))
    put("black", np.stack([lum, lum, lum + 3], 2))

    x, y, w, h, X, Y = grid("core")                                          # chrome between the grips
    lum = np.clip(chrome(X, w, base=150, amp=48) + rng.normal(0, 1.5, (h, w)), 70, 232)
    put("core", np.stack([lum, lum, lum + 2], 2))

    x, y, w, h, X, Y = grid("plate")                                         # bronze plate, black etching
    t = Y / h
    bronze = np.stack([158 - 26 * t, 130 - 24 * t, 78 - 18 * t], 2)
    bronze += rng.normal(0, 1.5, (h, 1, 1)) + rng.normal(0, 1.2, (h, w, 1))
    img = Image.fromarray(np.clip(bronze, 0, 255).astype(np.uint8))
    d = ImageDraw.Draw(img)
    ink = (14, 11, 8)
    d.rectangle([0, 0, w - 1, h - 1], outline=(214, 216, 222), width=10)
    d.rectangle([10, 10, w - 11, h - 11], outline=(110, 112, 118), width=2)
    for x0, s in ((26, 1), (w - 26, -1)):                                     # the E-shaped end marks
        d.line([x0, 30, x0, 98], fill=ink, width=4)
        for yy in (32, 64, 96):
            d.line([x0, yy, x0 + s * 26, yy], fill=ink, width=4)
    for i in range(6):                                                        # tick marks
        d.line([62 + i * 8, 40, 62 + i * 8, 88], fill=ink, width=3)
        d.line([w - 62 - i * 8, 40, w - 62 - i * 8, 88], fill=ink, width=3)
    cx = w // 2                                                               # the grid in the middle
    d.rectangle([cx - 20, 40, cx + 20, 88], outline=ink, width=4)
    d.line([cx, 40, cx, 88], fill=ink, width=3)
    d.line([cx - 20, 64, cx + 20, 64], fill=ink, width=3)
    A[y:y + h, x:x + w] = np.asarray(img)

    x, y, w, h, X, Y = grid("ring")                                          # polished gold
    shade = 1 - .5 * np.abs(Y / h - .5) * 2
    gold = (np.stack([240 * shade + 14, 198 * shade + 10, 84 * shade], 2)
            + 40 * np.exp(-((Y / h - .3) / .08) ** 2)[..., None] * np.array([1, .9, .5]))
    put("ring", gold)

    x, y, w, h, X, Y = grid("btn")                                           # copper in a chrome bezel
    rr = np.hypot(X - w / 2, Y - h / 2) / (w / 2)
    blot = rng.normal(0, 14, (h, w))
    blot = np.asarray(Image.fromarray(np.clip(blot + 128, 0, 255).astype(np.uint8))
                      .resize((w // 6, h // 6)).resize((w, h), Image.BICUBIC)).astype(float) - 128
    copper = np.stack([176 + blot, 98 + blot * .6, 52 + blot * .35], 2)
    bezel = 205 - 90 * np.clip((rr - .72) / .28, 0, 1)
    put("btn", np.where((rr > .72)[..., None], np.stack([bezel, bezel, bezel + 3], 2), copper))

    x, y, w, h, X, Y = grid("port")                                          # black port, two gold pins
    rr = np.hypot(X - w / 2, Y - h / 2) / (w / 2)
    col = np.zeros((h, w, 3))
    col[:] = (170, 172, 178)
    col[rr < .78] = (14, 14, 16)
    for dx in (-.28, .28):
        col[np.hypot(X - w / 2 - dx * w / 2, Y - h / 2) / (w / 2) < .16] = (226, 178, 70)
    put("port", col)

    img = np.asarray(Image.fromarray(np.clip(A, 0, 255).astype(np.uint8)).resize((128, 128), Image.BOX)).astype(float).copy()

    x, y, w, h = REGIONS["emit"]                                              # emitter cup, seen from above
    Yy, Xx = np.mgrid[0:h, 0:w]
    rr = np.hypot(Xx + .5 - w / 2, Yy + .5 - h / 2) / (w / 2)
    col = np.zeros((h, w, 3))
    col[:] = (186, 188, 194)
    lip = (rr > .62) & (rr <= .86)
    col[lip] = np.where(((rr[lip] * 20).astype(int) % 2 == 0)[:, None], (214, 216, 222), (150, 152, 158))
    nozzle = (rr > .30) & (rr <= .62)
    col[nozzle] = np.array([206, 164, 72]) * (1.05 - .35 * (rr[nozzle, None] - .3) / .32)
    col[rr <= .30] = (22, 20, 18)
    col[rr <= .12] = (226, 186, 86)
    img[y:y + h, x:x + w] = col

    x, y, w, h = REGIONS["cap"]                                               # pommel end: silver, machined rings
    Yy, Xx = np.mgrid[0:h, 0:w]
    rr = np.hypot(Xx + .5 - w / 2, Yy + .5 - h / 2) / (w / 2)
    lum = 168 + 30 * np.cos(rr * 2 * np.pi * 2.2) + 22 * (Xx - Yy) / w
    for groove in (.30, .55, .80):
        lum -= 75 * np.exp(-((rr - groove) / .035) ** 2)
    lum = np.where(rr > .9, 215, lum)
    img[y:y + h, x:x + w] = np.stack([lum, lum, lum + 3], 2)
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))


def main(args):
    flags = {a for a in args if a.startswith("--")}
    paths = [a for a in args if not a.startswith("--")]
    if len(paths) != 3 or flags - {"--obj", "--atlas"}:
        sys.exit(__doc__)
    msh_in, stx_in, mods = paths
    template = msh.parse(open(msh_in, "rb").read())
    hilt = model(template)
    mesh_bytes = msh.build(hilt)
    atlas = paint()
    texture_bytes = stx.encode(open(stx_in, "rb").read(), atlas)
    for name in ("lsaberanakin", "lsaberanakinduel"):
        folder = os.path.join(mods, "meshes", "weapons", name)
        os.makedirs(folder, exist_ok=True)
        open(os.path.join(folder, name + ".msh"), "wb").write(mesh_bytes)
        open(os.path.join(folder, "vosasaber.stx"), "wb").write(texture_bytes)
        print("wrote", folder)
    if "--obj" in flags:
        msh.to_obj(hilt, "anakin_ep3_hilt.obj")
    if "--atlas" in flags:
        atlas.save("anakin_ep3_hilt_atlas.png")
    print(f"{len(hilt.verts)} vertices, {len(hilt.tris)} triangles; texture name {stx.texture_name(texture_bytes)}")


if __name__ == "__main__":
    main(sys.argv[1:])
