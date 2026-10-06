#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read and write the game's static weapon meshes (.msh), and convert them to and from OBJ.

    python tools/weapons/msh.py info     lsaberanakin.msh
    python tools/weapons/msh.py to-obj   lsaberanakin.msh hilt.obj
    python tools/weapons/msh.py from-obj hilt.obj lsaberanakin.msh new.msh
    python tools/weapons/msh.py selftest lsaberanakin.msh

The input .msh comes from the user's own copy of the game (see docs/modding/dumping-assets.md).
`from-obj` uses it as a template: everything this tool does not understand is copied from it.

Layout (docs/modding/file-formats.md, "MSH mesh"), worked out on meshes\\weapons\\lsaberanakin:

    0x064  bounding sphere: centre x, y, z, radius (4 floats)
    0x088  triangle count (u32)
    0x114  vertex count (u32)
    0x118  vertices, 36 bytes each:
             +0   position (3 floats)
             +12  normal, 3 unsigned bytes (b / 127.5 - 1) and a pad byte
             +16  uv (2 floats)
             +24  second uv set (8 bytes; zero on the weapons seen so far)
             +32  colour (4 bytes)
    then   16 bytes (meaning unknown, copied), the index count (u32) twice, and that many u16
           indices forming a triangle strip
    tail   the material and texture strings, with three u16 counts at +8 (vertices, triangles,
           3 x triangles) and the bounding box (6 floats) at +0xC2. Two other values in the tail
           (39 on lsaberanakin, stored twice) are not understood and are copied unchanged.

The game is left-handed: OBJ output negates z and reverses the winding, so a model looks the same in
Blender as in the game. OBJ texture v is flipped (its origin is bottom-left).
"""
import math
import os
import struct
import sys
import tempfile

VERTEX_COUNT = 0x114
VERTICES = 0x118
STRIDE = 36


class Mesh:
    """A parsed mesh. hdr, pre and tail are kept as bytes so the parts not understood survive."""

    def __init__(self):
        self.hdr = bytearray()
        self.pre = b""
        self.tail = bytearray()
        self.verts = []   # dicts: pos, nb (normal bytes), pad, uv, uv1 (bytes), col (bytes)
        self.tris = []
        self.idx = None   # the strip as read; None for a mesh built here

    def from_template(self, template):
        self.hdr = bytearray(template.hdr)
        self.pre = template.pre
        self.tail = bytearray(template.tail)
        return self


def strip_to_tris(idx):
    """Triangles of a strip. Repeated indices join separate strips; odd triangles swap winding."""
    out = []
    for i in range(len(idx) - 2):
        a, b, c = idx[i:i + 3]
        if a == b or b == c or a == c:
            continue
        out.append((a, b, c) if i % 2 == 0 else (a, c, b))
    return out


def tris_to_strip(tris):
    """One strip for a list of triangles: each is joined to the last by repeating indices, with an
    extra repeat where needed so every real triangle starts at an even position (keeps its winding)."""
    out = []
    for a, b, c in tris:
        if out:
            out += [out[-1], a]
            if len(out) % 2 == 1:
                out.append(a)
        out += [a, b, c]
    return out


def parse(data):
    m = Mesh()
    nv = struct.unpack_from("<I", data, VERTEX_COUNT)[0]
    m.hdr = bytearray(data[:VERTEX_COUNT])
    for i in range(nv):
        o = VERTICES + STRIDE * i
        m.verts.append(dict(
            pos=list(struct.unpack_from("<3f", data, o)), nb=bytes(data[o + 12:o + 15]), pad=data[o + 15],
            uv=list(struct.unpack_from("<2f", data, o + 16)), uv1=bytes(data[o + 24:o + 32]),
            col=bytes(data[o + 32:o + 36])))
    end = VERTICES + STRIDE * nv
    m.pre = bytes(data[end:end + 16])
    count, count2 = struct.unpack_from("<2I", data, end + 16)
    if count != count2:
        sys.exit("unexpected index header: the two index counts differ")
    m.idx = list(struct.unpack_from("<%dH" % count, data, end + 24))
    m.tail = bytearray(data[end + 24 + 2 * count:])
    m.tris = strip_to_tris(m.idx)
    t_verts, t_tris, t_list = struct.unpack_from("<3H", m.tail, 8)
    if (t_verts, t_tris, t_list) != (nv, len(m.tris), 3 * len(m.tris)):
        sys.exit("the tail's counts disagree with the data: not a mesh this tool understands")
    return m


def build(m, keep_indices=False):
    """Bytes of a mesh. keep_indices writes the strip as read (to reproduce a file exactly)."""
    nv, nt = len(m.verts), len(m.tris)
    idx = m.idx if keep_indices else tris_to_strip(m.tris)
    if strip_to_tris(idx) != list(m.tris):
        sys.exit("internal error: the strip does not reproduce the triangles")
    if nv > 0xFFFF or 3 * nt > 0xFFFF:
        sys.exit("mesh too large: the tail stores its counts as 16-bit values")

    def f32(x):
        return struct.unpack("<f", struct.pack("<f", x))[0]

    lo = [f32(min(v["pos"][i] for v in m.verts)) for i in range(3)]
    hi = [f32(max(v["pos"][i] for v in m.verts)) for i in range(3)]
    centre = [(lo[i] + hi[i]) / 2 for i in range(3)]
    radius = math.sqrt(sum(((hi[i] - lo[i]) / 2) ** 2 for i in range(3)))
    hdr = bytearray(m.hdr)
    struct.pack_into("<4f", hdr, 0x64, *centre, radius)
    struct.pack_into("<I", hdr, 0x88, nt)
    body = bytearray()
    for v in m.verts:
        body += (struct.pack("<3f", *v["pos"]) + v["nb"] + bytes([v["pad"]]) + struct.pack("<2f", *v["uv"])
                 + v["uv1"] + v["col"])
    tail = bytearray(m.tail)
    struct.pack_into("<3H", tail, 8, nv, nt, 3 * nt)
    struct.pack_into("<6f", tail, 0xC2, *lo, *hi)
    return (bytes(hdr) + struct.pack("<I", nv) + bytes(body) + m.pre + struct.pack("<2I", len(idx), len(idx))
            + struct.pack("<%dH" % len(idx), *idx) + bytes(tail))


def unpack_normal(nb):
    return [b / 127.5 - 1 for b in nb]


def pack_normal(n, normalize=False):
    if normalize:
        length = math.sqrt(sum(c * c for c in n)) or 1.0
        n = [c / length for c in n]
    return bytes(max(0, min(255, int(round((c + 1) * 127.5)))) for c in n)


def outward_sign(m):
    """+1 or -1: the sign of (b - a) x (c - a) that agrees with the stored normals, i.e. which winding
    the game treats as front-facing."""
    s = 0.0
    for a, b, c in m.tris:
        A, B, C = (m.verts[k]["pos"] for k in (a, b, c))
        u = [B[i] - A[i] for i in range(3)]
        w = [C[i] - A[i] for i in range(3)]
        cross = [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]]
        n = unpack_normal(m.verts[a]["nb"])
        s += sum(cross[i] * n[i] for i in range(3))
    return 1 if s >= 0 else -1


def to_obj(m, path):
    with open(path, "w") as f:
        f.write("# SWROTS weapon mesh (z negated, winding reversed, v flipped)\no mesh\n")
        for v in m.verts:
            f.write("v %.7g %.7g %.7g\n" % (v["pos"][0], v["pos"][1], -v["pos"][2]))
        for v in m.verts:
            f.write("vt %.7g %.7g\n" % (v["uv"][0], 1 - v["uv"][1]))
        for v in m.verts:
            n = unpack_normal(v["nb"])
            f.write("vn %.7g %.7g %.7g\n" % (n[0], n[1], -n[2]))
        for a, b, c in m.tris:
            f.write("f %d/%d/%d %d/%d/%d %d/%d/%d\n" % (a + 1, a + 1, a + 1, c + 1, c + 1, c + 1, b + 1, b + 1, b + 1))


def from_obj(path, template):
    """A mesh from an OBJ (polygons are fanned into triangles). Vertices without normals get smooth
    normals from the faces around them."""
    V, T, N, faces = [], [], [], []

    def index(s, count):
        if s == "":
            return None
        k = int(s)
        return k - 1 if k > 0 else count + k

    for line in open(path):
        p = line.split()
        if not p:
            continue
        if p[0] == "v":
            V.append([float(x) for x in p[1:4]])
        elif p[0] == "vt":
            T.append([float(x) for x in p[1:3]])
        elif p[0] == "vn":
            N.append([float(x) for x in p[1:4]])
        elif p[0] == "f":
            corners = []
            for tok in p[1:]:
                parts = tok.split("/")
                corners.append((index(parts[0], len(V)),
                                index(parts[1], len(T)) if len(parts) > 1 else None,
                                index(parts[2], len(N)) if len(parts) > 2 else None))
            for k in range(1, len(corners) - 1):
                faces.append((corners[0], corners[k], corners[k + 1]))

    m = Mesh().from_template(template)
    keys = sorted({c for face in faces for c in face},
                  key=lambda c: (c[0], -1 if c[1] is None else c[1], -1 if c[2] is None else c[2]))
    vertex_of = {}
    for c in keys:
        vi, ti, ni = c
        x, y, z = V[vi]
        u, v = (T[ti][0], 1 - T[ti][1]) if ti is not None else (0.0, 0.0)
        nb = pack_normal([N[ni][0], N[ni][1], -N[ni][2]]) if ni is not None else None
        vertex_of[c] = len(m.verts)
        m.verts.append(dict(pos=[x, y, -z], nb=nb, pad=0, uv=[u, v], uv1=bytes(8), col=bytes([0, 0, 0, 255])))
    for c0, c1, c2 in faces:
        m.tris.append((vertex_of[c0], vertex_of[c2], vertex_of[c1]))   # undo the export's winding reversal

    if any(v["nb"] is None for v in m.verts):
        sign = outward_sign(template)
        sums = {}
        for a, b, c in m.tris:
            A, B, C = (m.verts[k]["pos"] for k in (a, b, c))
            u = [B[i] - A[i] for i in range(3)]
            w = [C[i] - A[i] for i in range(3)]
            n = [sign * (u[1] * w[2] - u[2] * w[1]), sign * (u[2] * w[0] - u[0] * w[2]), sign * (u[0] * w[1] - u[1] * w[0])]
            for k in (a, b, c):
                acc = sums.setdefault(tuple(round(x, 5) for x in m.verts[k]["pos"]), [0.0, 0.0, 0.0])
                for i in range(3):
                    acc[i] += n[i]
        for v in m.verts:
            if v["nb"] is None:
                v["nb"] = pack_normal(sums[tuple(round(x, 5) for x in v["pos"])], normalize=True)
    return m


def describe(m):
    ys = [v["pos"][1] for v in m.verts]
    strip = m.idx if m.idx is not None else tris_to_strip(m.tris)
    print(f"{len(m.verts)} vertices, {len(m.tris)} triangles, {len(strip)} strip indices, "
          f"y {min(ys):.2f} .. {max(ys):.2f}")


def _same_vertices(a, b, tol=1e-5):
    if len(a.verts) != len(b.verts):
        return False
    for p, q in zip(a.verts, b.verts):
        if (max(abs(x - y) for x, y in zip(p["pos"], q["pos"])) > tol
                or max(abs(x - y) for x, y in zip(p["uv"], q["uv"])) > tol or p["nb"] != q["nb"]):
            return False
    return True


def selftest(path):
    """Checks the tool against a real mesh: an exact rebuild, and an OBJ round trip."""
    data = open(path, "rb").read()
    m = parse(data)
    exact = build(m, keep_indices=True) == data
    print("rebuilds the file byte for byte:", exact)
    with tempfile.TemporaryDirectory() as tmp:
        obj = os.path.join(tmp, "roundtrip.obj")
        to_obj(m, obj)
        m2 = from_obj(obj, m)
    roundtrip = _same_vertices(m, m2) and m.tris == m2.tris
    print("OBJ round trip keeps vertices, uvs, normals, triangles and winding:", roundtrip)
    m3 = parse(build(m2))
    strip = m3.tris == m2.tris and _same_vertices(m2, m3, 1e-6)
    print("a rebuilt strip decodes to the same triangles:", strip)
    return exact and roundtrip and strip


def main(args):
    if len(args) == 2 and args[0] == "info":
        describe(parse(open(args[1], "rb").read()))
    elif len(args) == 3 and args[0] == "to-obj":
        to_obj(parse(open(args[1], "rb").read()), args[2])
        print("wrote", args[2])
    elif len(args) == 4 and args[0] == "from-obj":
        m = from_obj(args[1], parse(open(args[2], "rb").read()))
        open(args[3], "wb").write(build(m))
        print("wrote", args[3])
        describe(m)
    elif len(args) == 2 and args[0] == "selftest":
        sys.exit(0 if selftest(args[1]) else 1)
    else:
        print(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
