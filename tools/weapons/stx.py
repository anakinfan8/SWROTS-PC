#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Convert the game's DXT1 textures (.stx) to and from PNG.

    python tools/weapons/stx.py decode vosasaber.stx vosasaber.png
    python tools/weapons/stx.py encode vosasaber.stx painted.png new.stx

`encode` takes an existing .stx from the user's own copy of the game as a template. It keeps the
template's header, including the texture name at 0x40 (the engine registers a texture under that name,
not its file name: docs/modding/replacing-textures.md), and replaces only the pixels. The PNG must have
the template's size.

Only format 0x40 (DXT1) is handled. On the weapon textures seen so far (meshes\\weapons\\lsaberanakin\\
vosasaber.stx, 128x128) its 4x4 blocks are stored in row order, not swizzled, and the pixel data starts
0x80 into the 'BIR' block at 0x104. Other formats and sizes are refused rather than guessed at.
"""
import struct
import sys

import numpy as np
from PIL import Image

BIR = 0x104
PIXELS = BIR + 0x80
DXT1 = 0x40


def read_header(data):
    if data[:4] != b"STX\0":
        sys.exit("not an STX file")
    w, h = struct.unpack_from("<II", data, 0x0C)
    if data[0x3C] != DXT1:
        sys.exit(f"format 0x{data[0x3C]:02x} is not supported (only DXT1, 0x40)")
    if len(data) != PIXELS + w * h // 2:
        sys.exit("unexpected file size for a DXT1 texture of this size")
    return w, h


def texture_name(data):
    return bytes(data[0x40:0x60]).split(b"\0")[0].decode("ascii", "replace")


def rgb565_to_rgb(c):
    return np.array([(c >> 11 & 31) * 255 // 31, (c >> 5 & 63) * 255 // 63, (c & 31) * 255 // 31], np.int32)


def rgb_to_rgb565(rgb):
    r, g, b = [int(v) for v in rgb]
    return (r * 31 + 127) // 255 << 11 | (g * 63 + 127) // 255 << 5 | (b * 31 + 127) // 255


def decode_block(block):
    c0, c1, bits = struct.unpack("<HHI", block)
    a, b = rgb565_to_rgb(c0), rgb565_to_rgb(c1)
    if c0 > c1:
        palette = [a, b, (2 * a + b) // 3, (a + 2 * b) // 3]
    else:
        palette = [a, b, (a + b) // 2, np.zeros(3, np.int32)]
    out = np.zeros((4, 4, 3), np.uint8)
    for i in range(16):
        out[i // 4, i % 4] = palette[bits >> (2 * i) & 3]
    return out


def encode_block(px):
    """A 4x4 block in DXT1's four-colour mode, endpoints on the colours' principal axis."""
    p = px.reshape(16, 3).astype(np.float64)
    if np.all(p == p[0]):
        c = rgb_to_rgb565(p[0])
        return struct.pack("<HHI", c, c, 0) if c else struct.pack("<HHI", 1, 0, 0)
    mean = p.mean(0)
    axis = np.linalg.eigh(np.cov((p - mean).T))[1][:, -1]
    t = (p - mean) @ axis
    c0 = rgb_to_rgb565(np.clip(mean + axis * t.min(), 0, 255))
    c1 = rgb_to_rgb565(np.clip(mean + axis * t.max(), 0, 255))
    if c0 < c1:
        c0, c1 = c1, c0
    if c0 == c1:
        c0 = min(c0 + 1, 0xFFFF)                     # c0 > c1 selects the four-colour mode
    a, b = rgb565_to_rgb(c0), rgb565_to_rgb(c1)
    palette = np.array([a, b, (2 * a + b) // 3, (a + 2 * b) // 3], np.float64)
    bits = 0
    for i in range(16):
        bits |= int(np.argmin(((palette - p[i]) ** 2).sum(1))) << (2 * i)
    return struct.pack("<HHI", c0, c1, bits)


def decode(data):
    """An RGB image of a DXT1 .stx."""
    w, h = read_header(data)
    img = np.zeros((h, w, 3), np.uint8)
    per_row = w // 4
    for i in range(w * h // 16):
        y, x = i // per_row * 4, i % per_row * 4
        img[y:y + 4, x:x + 4] = decode_block(data[PIXELS + 8 * i:PIXELS + 8 * i + 8])
    return Image.fromarray(img)


def encode(template, image):
    """The template .stx with its pixels replaced by `image` (a PIL image of the same size)."""
    data = bytearray(template)
    w, h = read_header(data)
    image = image.convert("RGB")
    if image.size != (w, h):
        sys.exit(f"the image must be {w}x{h}, not {image.size[0]}x{image.size[1]}")
    px = np.asarray(image)
    per_row = w // 4
    for i in range(w * h // 16):
        y, x = i // per_row * 4, i % per_row * 4
        data[PIXELS + 8 * i:PIXELS + 8 * i + 8] = encode_block(px[y:y + 4, x:x + 4])
    return bytes(data)


def main(args):
    if len(args) == 3 and args[0] == "decode":
        decode(open(args[1], "rb").read()).save(args[2])
        print("wrote", args[2])
    elif len(args) == 4 and args[0] == "encode":
        out = encode(open(args[1], "rb").read(), Image.open(args[2]))
        open(args[3], "wb").write(out)
        print(f"wrote {args[3]} (texture name kept: {texture_name(out)})")
    else:
        print(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
