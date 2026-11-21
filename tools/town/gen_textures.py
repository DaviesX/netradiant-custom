#!/usr/bin/env python3
"""Generate the town's placeholder textures and vegetation meshes.

Writes into content/town/ (see content/town/README.md, "Materials"):
  textures/town/*.tga   every image scripts/town.shader names
  models/town/*.obj     tree, grass_clump and grass_patch, through tools/benchmark/gen_foliage.py

The outputs are committed; nothing in the editor or the stock check runs this script. Re-running it with the same
Pillow version rewrites identical files (fixed seed). Last run with Pillow 12.3.0.

The sky image is one flat colour, FOG_RGB, and the fog volume must have exactly that colour so that a building fading
into fog meets the sky without a seam. scripts/town.shader is written by hand, so this script reads the fogparms line of
textures/town/fog and refuses to run when its colour is not FOG_RGB.

Image conventions are the benchmark's (gen_foliage.py): uncompressed 32-bit TGA stored bottom-up, no name ending in
_n, _nh or _s, tangent-space normal maps with +Y toward the top of the image. Metallic-roughness maps hold roughness in
G and metallic in B; occlusion is in R.
"""

import math
import os
import random
import re
import sys

from PIL import Image, ImageDraw

sys.dont_write_bytecode = True  # importing gen_foliage must not leave a __pycache__ beside it
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "benchmark"))
import gen_foliage  # noqa: E402
from gen_foliage import REPO, SEED, SIZE, clamp8, cutout, fbm, lerp_colour, normal_map, periodic_noise, save_tga  # noqa: E402

MOD = os.path.join(REPO, "content", "town")
TEXTURES = os.path.join(MOD, "textures", "town")
MODELS = os.path.join(MOD, "models", "town")
SHADER = os.path.join(MOD, "scripts", "town.shader")
PREFIX = "textures/town"

FOG_RGB = (51, 66, 59)  # the sky image's colour and the fog volume's; town.shader's fogparms must equal it


def rng(n):
    return random.Random(SEED + 200 + n)


def image(fn):
    """A SIZE x SIZE RGBA image from fn(x, y) -> (r, g, b)."""
    img = Image.new("RGBA", (SIZE, SIZE))
    px = img.load()
    for y in range(SIZE):
        for x in range(SIZE):
            px[x, y] = tuple(clamp8(c) for c in fn(x, y)) + (255,)
    return img


def flat(rgb, size=SIZE):
    return Image.new("RGBA", (size, size), tuple(rgb) + (255,))


# ---------------------------------------------------------------- materials

def make_asphalt(r):
    n = fbm(r, 4)
    grit = periodic_noise(r, 64, 64)
    height = [[0.6 * n[y][x] + 0.4 * grit[y][x] for x in range(SIZE)] for y in range(SIZE)]

    def colour(x, y):
        c = lerp_colour((34, 34, 36), (62, 62, 64), n[y][x])
        if grit[y][x] > 0.85:
            c = lerp_colour(c, (96, 94, 90), 0.5)
        return c
    return image(colour), normal_map(height, 3.0)


def make_sidewalk(r):
    """Concrete slabs, two by two per tile, with a groove at each slab edge."""
    n = fbm(r, 4)

    def groove(v):
        d = min(v % 64, 64 - v % 64)
        return min(1.0, d / 2.0)
    height = [[0.8 * groove(x) * groove(y) + 0.2 * n[y][x] for x in range(SIZE)] for y in range(SIZE)]

    def colour(x, y):
        c = lerp_colour((110, 108, 102), (150, 148, 140), n[y][x])
        return lerp_colour((60, 58, 54), c, groove(x) * groove(y))
    return image(colour), normal_map(height, 4.0)


def make_brick(r):
    """Running bond: bricks 32 x 16 texels with 2-texel mortar. Returns colour, normal, metallic-roughness, occlusion."""
    n = fbm(r, 3)
    tint = [[r.random() for _ in range(4)] for _ in range(8)]

    def cell(x, y):
        row = y // 16
        xo = (x + (16 if row % 2 else 0)) % SIZE
        return row, xo // 32, xo % 32, y % 16

    def mortar(x, y):
        _, _, bx, by = cell(x, y)
        return bx < 2 or by < 2
    height = [[(0.0 if mortar(x, y) else 0.8) + 0.2 * n[y][x] for x in range(SIZE)] for y in range(SIZE)]

    def colour(x, y):
        row, col, _, _ = cell(x, y)
        if mortar(x, y):
            return lerp_colour((92, 88, 82), (120, 116, 108), n[y][x])
        c = lerp_colour((96, 44, 34), (140, 70, 50), tint[row][col])
        return lerp_colour(c, (70, 40, 34), 0.5 * n[y][x])

    def mr(x, y):
        return (0, 242 if mortar(x, y) else 217, 0)  # rough dielectric: roughness 0.95 in the mortar, 0.85 on the bricks

    def ao(x, y):
        _, _, bx, by = cell(x, y)
        edge = min(bx - 2, by - 2, 31 - bx, 15 - by)
        v = 150 if mortar(x, y) else (215 if edge < 1 else 255)
        return (v, v, v)
    return image(colour), normal_map(height, 5.0), image(mr), image(ao)


def make_plaster(r):
    n = fbm(r, 4)
    return image(lambda x, y: lerp_colour((150, 142, 126), (182, 174, 158), n[y][x]))


def make_woodfloor(r):
    """Planks 16 texels wide running up the image, with grain along them."""
    grain = periodic_noise(r, 32, 2)
    tone = [r.random() for _ in range(8)]

    def gap(x):
        return x % 16 == 0
    height = [[(0.0 if gap(x) else 0.85) + 0.15 * grain[y][x] for x in range(SIZE)] for y in range(SIZE)]

    def colour(x, y):
        if gap(x):
            return (30, 22, 16)
        c = lerp_colour((84, 58, 36), (120, 86, 54), tone[x // 16])
        return lerp_colour(c, (60, 42, 28), 0.5 * grain[y][x])
    return image(colour), normal_map(height, 4.0)


def make_door(r):
    """A panelled door filling the image: stiles and rails around four panels, a knob on the right."""
    grain = periodic_noise(r, 32, 2)
    img = image(lambda x, y: lerp_colour((70, 44, 28), (96, 62, 38), grain[y][x]))
    d = ImageDraw.Draw(img)
    for x0, x1 in ((14, 58), (70, 114)):
        for y0, y1 in ((12, 58), (70, 116)):
            d.rectangle((x0, y0, x1, y1), fill=(58, 36, 24, 255), outline=(40, 26, 18, 255))
    d.ellipse((112, 60, 120, 68), fill=(150, 130, 70, 255))
    return img


def make_window(r):
    """An unlit window: a dark pane with a frame and one muntin cross. Opaque."""
    n = fbm(r, 2)
    img = image(lambda x, y: lerp_colour((14, 18, 24), (26, 32, 40), n[y][x]))
    d = ImageDraw.Draw(img)
    frame = (72, 70, 64, 255)
    d.rectangle((0, 0, SIZE - 1, SIZE - 1), outline=frame, width=8)
    d.rectangle((60, 0, 67, SIZE - 1), fill=frame)
    d.rectangle((0, 60, SIZE - 1, 67), fill=frame)
    return img


def make_metal(r):
    """Painted steel for the lamp posts. Returns colour and metallic-roughness (metal, roughness about 0.45)."""
    n = fbm(r, 3)
    colour = image(lambda x, y: lerp_colour((40, 46, 44), (62, 68, 66), n[y][x]))
    mr = image(lambda x, y: (0, 100 + 30 * n[y][x], 255))
    return colour, mr


def make_fence():
    """Chain link: two families of diagonal wires, alpha-tested."""
    def draw(col, mask, s):
        big = SIZE * s
        step = 16 * s
        for k in range(-big // step, 2 * big // step + 1):
            for line in ((k * step, 0, k * step + big, big), (k * step, 0, k * step - big, big)):
                col.line(line, fill=(130, 134, 138), width=2 * s)
                mask.line(line, fill=255, width=2 * s)
    return cutout(draw, (130, 134, 138))


def tool(rgb):
    """A 32 x 32 editor image for a nodraw tool shader: the colour with a darker border."""
    img = flat(rgb, 32)
    ImageDraw.Draw(img).rectangle((0, 0, 31, 31), outline=tuple(c // 2 for c in rgb) + (255,), width=2)
    return img


# ---------------------------------------------------------------- fog and sky

def shader_fog_colour():
    """The fogparms colour of textures/town/fog in town.shader, as 8-bit RGB."""
    with open(SHADER) as f:
        text = f.read()
    block = re.search(r"^textures/town/fog\s*\{(.*?)^\}", text, re.S | re.M)
    if not block:
        sys.exit("%s: no textures/town/fog shader" % os.path.relpath(SHADER, REPO))
    parms = re.search(r"fogparms\s*\(\s*(\S+)\s+(\S+)\s+(\S+)\s*\)", block.group(1))
    if not parms:
        sys.exit("%s: textures/town/fog has no fogparms" % os.path.relpath(SHADER, REPO))
    return tuple(int(round(float(v) * 255)) for v in parms.groups())


def check_fog():
    fog = shader_fog_colour()
    if fog != FOG_RGB:
        sys.exit("fog and sky colours differ: town.shader's fogparms is %d %d %d (8-bit), the sky image is %d %d %d. "
                 "Set fogparms to ( %.5f %.5f %.5f ) or change FOG_RGB."
                 % (fog + FOG_RGB + tuple(c / 255 for c in FOG_RGB)))


# ---------------------------------------------------------------- main

def main():
    check_fog()  # before anything is written
    gen_foliage.main(TEXTURES, MODELS, PREFIX)  # bark, leaves, grass, dirt and the meshes; sets save_tga's directory
    out = []

    def save(img, name):
        out.append(save_tga(img, name))

    for name, maker, n in (("asphalt", make_asphalt, 1), ("sidewalk", make_sidewalk, 2), ("woodfloor", make_woodfloor, 5)):
        colour, nrm = maker(rng(n))
        save(colour, name + "_c")
        save(nrm, name + "_nrm")
    colour, nrm, mr, ao = make_brick(rng(3))
    save(colour, "brick_c")
    save(nrm, "brick_nrm")
    save(mr, "brick_mr")
    save(ao, "brick_ao")
    save(make_plaster(rng(4)), "plaster_c")
    save(make_door(rng(6)), "door_c")
    save(make_window(rng(7)), "window_c")
    colour, mr = make_metal(rng(8))
    save(colour, "metal_c")
    save(mr, "metal_mr")
    save(make_fence(), "fence")
    # emitters: flat images, so an attached light's derived value does not depend on how a face maps the image
    save(flat((200, 196, 184)), "lamp_c")
    save(flat((255, 214, 150)), "lamp_e")
    save(flat((210, 212, 214)), "panel_c")
    save(flat((232, 240, 255)), "panel_e")
    save(flat(FOG_RGB, 8), "sky")
    for name, rgb in (("caulk", (220, 60, 200)), ("clip", (200, 40, 40)), ("hint", (220, 200, 40)),
                      ("skip", (40, 190, 200)), ("trigger", (230, 130, 30))):
        save(tool(rgb), name)
    for path in out:
        print(os.path.relpath(path, REPO))


if __name__ == "__main__":
    main()
