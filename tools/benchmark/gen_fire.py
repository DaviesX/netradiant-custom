#!/usr/bin/env python3
"""Generate the benchmark's fireplace textures.

Writes into content/benchmark/textures/bench/ (see content/benchmark/README.md, "Materials" and "Lights"):
  fire_c.tga  base colour of the firebox's back face: soot with a bed of dark embers along the bottom
  fire_e.tga  emission of the same face: flame tongues rising from a glowing ember bed, black above them

Both are 128x64 and cover the 96x48 fire face once (texture scale 0.75). The shader textures/bench/fire adds
fire_e under rgbGen const and qer_pbr_emissiveStrength, like textures/bench/neon. The light_area on the face takes its
radiance from the mean of fire_e over the face ("Re-derive light from emitter"), so after re-running this script,
re-derive the fireplace's light.

The outputs are committed; nothing in the editor or the stock check runs this script. Re-running it with the same
Pillow version rewrites identical files (fixed seed). Last run with Pillow 12.3.0. It shares the image conventions and
the TGA writer of gen_foliage.py (uncompressed 32-bit, bottom-up, no _n/_nh/_s names).
"""

import os
import random
import sys

from PIL import Image

sys.dont_write_bytecode = True  # importing gen_foliage must not leave a __pycache__ beside it
from gen_foliage import REPO, SEED, lerp_colour, save_tga  # noqa: E402

WIDTH = 128
HEIGHT = 64


def noise_rows(rng, cells_x, cells_y):
    """Value noise in [0, 1], WIDTH x HEIGHT, wrapping horizontally (rows top to bottom)."""
    lattice = [[rng.random() for _ in range(cells_x)] for _ in range(cells_y + 1)]
    out = []
    for py in range(HEIGHT):
        fy = py * cells_y / HEIGHT
        y0 = int(fy)
        ty = fy - y0
        ty = ty * ty * (3 - 2 * ty)
        row = []
        for px in range(WIDTH):
            fx = px * cells_x / WIDTH
            x0 = int(fx)
            tx = fx - x0
            tx = tx * tx * (3 - 2 * tx)
            a = lattice[y0][x0 % cells_x]
            b = lattice[y0][(x0 + 1) % cells_x]
            c = lattice[y0 + 1][x0 % cells_x]
            d = lattice[y0 + 1][(x0 + 1) % cells_x]
            top = a + (b - a) * tx
            row.append(top + ((c + (d - c) * tx) - top) * ty)
        out.append(row)
    return out


def flame_field(rng):
    """Flame density in [0, 1]: tongues whose height varies across the face, broken up by noise, dense at the bottom."""
    tongues = noise_rows(rng, 6, 1)  # per-column flame height
    detail = noise_rows(rng, 16, 8)
    fine = noise_rows(rng, 32, 16)
    field = []
    for y in range(HEIGHT):
        h = 1.0 - (y + 0.5) / HEIGHT  # 0 at the bottom row, 1 at the top
        row = []
        for x in range(WIDTH):
            reach = 0.35 + 0.55 * tongues[y][x]  # how far up this column burns
            wobble = 0.12 * (detail[y][x] - 0.5) + 0.05 * (fine[y][x] - 0.5)
            t = (h + wobble) / reach
            v = max(0.0, 1.0 - t) ** 1.4
            row.append(min(1.0, v * (0.8 + 0.4 * fine[y][x])))
        field.append(row)
    return field


def make_fire(rng):
    field = flame_field(rng)
    soot = noise_rows(rng, 8, 4)
    emissive = Image.new("RGBA", (WIDTH, HEIGHT))
    base = Image.new("RGBA", (WIDTH, HEIGHT))
    pe = emissive.load()
    pb = base.load()
    stops = [(0.0, (0, 0, 0)), (0.25, (100, 20, 0)), (0.55, (235, 100, 16)), (0.8, (255, 185, 70)), (1.0, (255, 244, 200))]
    for y in range(HEIGHT):
        h = 1.0 - (y + 0.5) / HEIGHT
        for x in range(WIDTH):
            v = field[y][x]
            for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
                if v <= t1:
                    c = lerp_colour(c0, c1, (v - t0) / (t1 - t0))
                    break
            pe[x, y] = c + (255,)
            # base colour: dark soot, warmer and slightly lighter in the ember bed at the bottom
            ember = max(0.0, 1.0 - h / 0.2)
            b = lerp_colour((22, 18, 16), (40, 34, 30), soot[y][x])
            pb[x, y] = lerp_colour(b, (70, 30, 14), ember * 0.7) + (255,)
    return base, emissive


def mean_linear(img):
    """Mean sRGB-decoded RGB of an image, the editor's footprint mean over the whole face."""
    def lin(c):
        c /= 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    px = img.load()
    texels = [px[x, y] for y in range(img.height) for x in range(img.width)]
    return tuple(sum(lin(t[i]) for t in texels) / len(texels) for i in range(3))


def main():
    base, emissive = make_fire(random.Random(SEED + 100))
    written = [save_tga(base, "fire_c"), save_tga(emissive, "fire_e")]
    for path in written:
        print(os.path.relpath(path, REPO))
    print("fire_e linear mean: %.6f %.6f %.6f" % mean_linear(emissive))


if __name__ == "__main__":
    main()
