#!/usr/bin/env python3
"""Generate the benchmark's placeholder vegetation: textures and OBJ meshes.

Writes into content/benchmark/ (see content/benchmark/README.md, "Vegetation"):
  textures/bench/bark_c.tga, bark_nrm.tga, leaves.tga, grass.tga, dirt_c.tga
  models/bench/tree.obj, grass_clump.obj, grass_patch.obj (each with a .mtl)

The outputs are committed; nothing in the editor or the stock check runs this script. Re-running it with the same
Pillow version rewrites identical files (fixed seed, fixed float formatting). Last run with Pillow 12.3.0.

Conventions shared with the rest of the bench:
  - images are 128x128 RGBA TGA, uncompressed and stored bottom-up (ioquake3 ignores the top-down flag);
  - no image name ends in _n, _nh or _s (rend2 loads those by name);
  - normal maps are tangent space, +Y toward the top of the image.
Mesh conventions (both q3map2 and the editor load OBJ through assimp, rotate Y-up to Z-up and flip the winding):
  - geometry is built in Quake coordinates (Z up) and written to the OBJ as (x, z, -y), i.e. Y-up;
  - front faces are counter-clockwise seen from their front, normals are explicit;
  - texture v runs from 0 at the bottom of the image to 1 at the top;
  - each usemtl is the full shader name, so neither loader looks for an image file.
"""

import math
import os
import random

from PIL import Image, ImageDraw

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MOD = os.path.join(REPO, "content", "benchmark")
TEXTURES = os.path.join(MOD, "textures", "bench")
MODELS = os.path.join(MOD, "models", "bench")
SIZE = 128
SEED = 1996  # one seed for everything; each asset draws from its own Random(SEED + n)


# ---------------------------------------------------------------- images

def periodic_noise(rng, cells_x, cells_y, size=SIZE):
    """Tileable value noise in [0, 1]: a random lattice of cells_x by cells_y, smoothly interpolated with wrap-around."""
    lattice = [[rng.random() for _ in range(cells_x)] for _ in range(cells_y)]
    out = []
    for py in range(size):
        fy = py * cells_y / size
        y0 = int(fy)
        ty = fy - y0
        ty = ty * ty * (3 - 2 * ty)
        row = []
        for px in range(size):
            fx = px * cells_x / size
            x0 = int(fx)
            tx = fx - x0
            tx = tx * tx * (3 - 2 * tx)
            a = lattice[y0 % cells_y][x0 % cells_x]
            b = lattice[y0 % cells_y][(x0 + 1) % cells_x]
            c = lattice[(y0 + 1) % cells_y][x0 % cells_x]
            d = lattice[(y0 + 1) % cells_y][(x0 + 1) % cells_x]
            row.append((a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty)
        out.append(row)
    return out


def fbm(rng, octaves, size=SIZE):
    """Sum of periodic noise octaves (cells 4, 8, 16, ...), normalised to [0, 1]."""
    total = [[0.0] * size for _ in range(size)]
    weight = 0.0
    amp = 1.0
    for o in range(octaves):
        cells = 4 << o
        n = periodic_noise(rng, cells, cells, size)
        for y in range(size):
            for x in range(size):
                total[y][x] += amp * n[y][x]
        weight += amp
        amp *= 0.5
    return [[v / weight for v in row] for row in total]


def clamp8(v):
    return max(0, min(255, int(round(v))))


def lerp_colour(c0, c1, t):
    return tuple(clamp8(a + (b - a) * t) for a, b in zip(c0, c1))


def normal_map(height, strength):
    """Tangent-space normal map from a tileable height field (rows top to bottom), +Y toward the top of the image."""
    size = len(height)
    img = Image.new("RGBA", (size, size))
    px = img.load()
    for y in range(size):
        for x in range(size):
            dx = (height[y][(x + 1) % size] - height[y][(x - 1) % size]) * strength
            # row index grows downward, +Y points up the image
            dy = (height[(y - 1) % size][x] - height[(y + 1) % size][x]) * strength
            nx, ny, nz = -dx, -dy, 1.0
            inv = 1.0 / math.sqrt(nx * nx + ny * ny + nz * nz)
            px[x, y] = (clamp8((nx * inv * 0.5 + 0.5) * 255), clamp8((ny * inv * 0.5 + 0.5) * 255),
                        clamp8((nz * inv * 0.5 + 0.5) * 255), 255)
    return img


def bark_height(rng):
    """Vertical grooves (a few per tile, wandering slightly) plus fine noise."""
    wander = periodic_noise(rng, 1, 4)
    fine = fbm(rng, 3)
    grooves = 6
    h = []
    for y in range(SIZE):
        row = []
        for x in range(SIZE):
            u = (x / SIZE + 0.08 * wander[y][x]) * grooves
            ridge = 0.5 + 0.5 * math.cos(2 * math.pi * u)
            row.append(0.75 * ridge ** 0.6 + 0.25 * fine[y][x])
        h.append(row)
    return h


def make_bark(rng):
    h = bark_height(rng)
    tint = fbm(rng, 2)
    img = Image.new("RGBA", (SIZE, SIZE))
    px = img.load()
    for y in range(SIZE):
        for x in range(SIZE):
            c = lerp_colour((38, 28, 20), (96, 76, 58), h[y][x])
            c = lerp_colour(c, (70, 70, 58), 0.3 * tint[y][x])
            px[x, y] = c + (255,)
    return img, normal_map(h, 6.0)


def make_dirt(rng):
    n = fbm(rng, 4)
    pebbles = periodic_noise(rng, 32, 32)
    img = Image.new("RGBA", (SIZE, SIZE))
    px = img.load()
    for y in range(SIZE):
        for x in range(SIZE):
            c = lerp_colour((52, 40, 28), (104, 86, 64), n[y][x])
            if pebbles[y][x] > 0.82:
                c = lerp_colour(c, (120, 112, 100), 0.6)
            px[x, y] = c + (255,)
    return img


def cutout(draw_fn, fill_rgb, supersample=4):
    """RGBA image whose alpha is a hard 0/255 mask drawn at 4x and reduced, with the colour of cut-away texels set to
    fill_rgb so mipmapping doesn't bleed black fringes into the alpha test's edge."""
    big = SIZE * supersample
    colour = Image.new("RGB", (big, big), fill_rgb)
    mask = Image.new("L", (big, big), 0)
    draw_fn(ImageDraw.Draw(colour), ImageDraw.Draw(mask), supersample)
    colour = colour.resize((SIZE, SIZE), Image.Resampling.BOX)
    mask = mask.resize((SIZE, SIZE), Image.Resampling.BOX).point(lambda v: 255 if v >= 128 else 0)
    img = colour.convert("RGBA")
    img.putalpha(mask)
    return img


def make_leaves(rng):
    """A cluster of leaves filling the card, thinning toward its edge."""
    def draw(col, mask, s):
        for _ in range(280):
            r = 0.5 * math.sqrt(rng.random())
            a = rng.random() * 2 * math.pi
            cx = (0.5 + r * math.cos(a)) * SIZE * s
            cy = (0.5 + r * math.sin(a)) * SIZE * s
            length = rng.uniform(9, 15) * s
            width = length * rng.uniform(0.35, 0.5)
            rot = rng.random() * math.pi
            pts = []
            for k in range(12):
                t = 2 * math.pi * k / 12
                lx = 0.5 * length * math.cos(t)
                ly = 0.5 * width * math.sin(t) * (1.0 - 0.35 * math.cos(t))
                pts.append((cx + lx * math.cos(rot) - ly * math.sin(rot), cy + lx * math.sin(rot) + ly * math.cos(rot)))
            shade = rng.random()
            col.polygon(pts, fill=lerp_colour((34, 58, 26), (92, 112, 48), shade))
            mask.polygon(pts, fill=255)
    return cutout(draw, (56, 80, 36))


def make_grass(rng):
    """Blades rising from the bottom edge of the card, tapering to a point."""
    def draw(col, mask, s):
        for _ in range(48):
            base = rng.uniform(0.04, 0.96) * SIZE * s
            height = rng.uniform(0.45, 0.97) * SIZE * s
            half = rng.uniform(1.6, 3.2) * s
            lean = rng.uniform(-0.25, 0.25) * height
            bottom = SIZE * s
            pts = [(base - half, bottom), (base + half, bottom), (base + lean, bottom - height)]
            shade = rng.random()
            col.polygon(pts, fill=lerp_colour((58, 66, 30), (132, 124, 62), shade))
            mask.polygon(pts, fill=255)
    return cutout(draw, (92, 94, 46))


def save_tga(img, name):
    path = os.path.join(TEXTURES, name + ".tga")
    assert not any(name.endswith(sfx) for sfx in ("_n", "_nh", "_s")), name
    img.save(path, orientation=-1, compression=None)
    with open(path, "rb") as f:
        header = f.read(18)
    assert header[2] == 2 and header[16] == 32, "%s: expected uncompressed 32-bit TGA" % path
    assert header[17] & 0x20 == 0, "%s: stored top-down" % path
    return path


# ---------------------------------------------------------------- meshes

def norm(v):
    inv = 1.0 / math.sqrt(sum(c * c for c in v))
    return tuple(c * inv for c in v)


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def scale(v, k):
    return tuple(c * k for c in v)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


class Mesh:
    """Triangles grouped by shader; positions and normals in Quake coordinates."""

    def __init__(self):
        self.groups = {}

    def tri(self, shader, verts):
        """verts: three (position, normal, uv), counter-clockwise seen from the front."""
        self.groups.setdefault(shader, []).append(verts)

    def quad(self, shader, verts):
        a, b, c, d = verts
        self.tri(shader, (a, b, c))
        self.tri(shader, (a, c, d))

    def write(self, name):
        obj_path = os.path.join(MODELS, name + ".obj")
        mtl_path = os.path.join(MODELS, name + ".mtl")
        lines = ["# generated by tools/benchmark/gen_foliage.py; do not edit", "mtllib %s.mtl" % name, "o %s" % name]
        v_lines, vt_lines, vn_lines, f_lines = [], [], [], []
        index = {"v": {}, "vt": {}, "vn": {}}

        def ref(kind, key, text, out):
            if key not in index[kind]:
                out.append(text)
                index[kind][key] = len(out)
            return index[kind][key]

        for shader in sorted(self.groups):
            f_lines.append("usemtl %s" % shader)
            for tri in self.groups[shader]:
                refs = []
                for (x, y, z), n, (u, v) in tri:
                    # Quake (x, y, z) -> OBJ (x, z, -y); the loaders rotate it back
                    p = "%.4f %.4f %.4f" % (x, z, -y)
                    nn = norm(n)
                    q = "%.4f %.4f %.4f" % (nn[0], nn[2], -nn[1])
                    t = "%.4f %.4f" % (u, v)
                    refs.append("%d/%d/%d" % (ref("v", p, "v " + p, v_lines), ref("vt", t, "vt " + t, vt_lines),
                                              ref("vn", q, "vn " + q, vn_lines)))
                f_lines.append("f " + " ".join(refs))
        with open(obj_path, "w", newline="\n") as f:
            f.write("\n".join(lines + v_lines + vt_lines + vn_lines + f_lines) + "\n")
        with open(mtl_path, "w", newline="\n") as f:
            f.write("# generated by tools/benchmark/gen_foliage.py; do not edit\n")
            for shader in sorted(self.groups):
                f.write("newmtl %s\n" % shader)
        return obj_path


BARK = "textures/bench/bark"
LEAVES = "textures/bench/leaves"
GRASS = "textures/bench/grass"


def tapered_prism(mesh, shader, base, axis, length, r0, r1, sides, u_repeat, cap):
    """A closed-sided prism from base along axis, radius r0 to r1, with smooth radial normals.
    Adds a top cap when cap is set. The bottom is left open (it sits in the ground or inside the trunk)."""
    axis = norm(axis)
    helper = (0.0, 0.0, 1.0) if abs(axis[2]) < 0.9 else (1.0, 0.0, 0.0)
    side_a = norm(cross(axis, helper))
    side_b = cross(axis, side_a)
    top = add(base, scale(axis, length))
    v_len = length / 64.0
    ring = []
    for k in range(sides + 1):
        t = 2 * math.pi * k / sides
        radial = add(scale(side_a, math.cos(t)), scale(side_b, math.sin(t)))
        ring.append((radial, u_repeat * k / sides))
    for k in range(sides):
        (ra, ua), (rb, ub) = ring[k], ring[k + 1]
        p0 = add(base, scale(ra, r0))
        p1 = add(base, scale(rb, r0))
        p2 = add(top, scale(rb, r1))
        p3 = add(top, scale(ra, r1))
        # radial order a -> b runs counter-clockwise around the axis, so (p0, p1, p2, p3) faces outward
        mesh.quad(shader, [(p0, ra, (ua, 0.0)), (p1, rb, (ub, 0.0)), (p2, rb, (ub, v_len)), (p3, ra, (ua, v_len))])
    if cap:
        for k in range(sides):
            (ra, _), (rb, _) = ring[k], ring[k + 1]
            pa = add(top, scale(ra, r1))
            pb = add(top, scale(rb, r1))
            mesh.tri(shader, [(top, axis, (0.5, 0.5)), (pa, axis, (0.5 + 0.5 * ra[0], 0.5 + 0.5 * ra[1])),
                              (pb, axis, (0.5 + 0.5 * rb[0], 0.5 + 0.5 * rb[1]))])
    return top


def card(mesh, shader, centre, normal, up_hint, width, height, bend_centre, uv_bottom_at_base=False):
    """One quad of width x height around centre, facing normal, with per-vertex normals bent away from bend_centre.
    With uv_bottom_at_base the quad stands on centre (grass); otherwise it is centred on it (leaves)."""
    n = norm(normal)
    right = norm(cross(up_hint, n))
    up = cross(n, right)
    hw = 0.5 * width
    if uv_bottom_at_base:
        lo, hi = 0.0, height
    else:
        lo, hi = -0.5 * height, 0.5 * height
    corners = [(-hw, lo, (0.0, 0.0)), (hw, lo, (1.0, 0.0)), (hw, hi, (1.0, 1.0)), (-hw, hi, (0.0, 1.0))]
    verts = []
    for cx, cy, uv in corners:
        p = add(centre, add(scale(right, cx), scale(up, cy)))
        verts.append((p, norm(sub(p, bend_centre)), uv))
    # right x up = normal, so the corners above run counter-clockwise seen from the front
    mesh.quad(shader, verts)


def make_tree(rng):
    mesh = Mesh()
    trunk_top = tapered_prism(mesh, BARK, (0.0, 0.0, 0.0), (0.0, 0.0, 1.0), 132.0, 7.0, 4.5, 6, 2.0, cap=True)
    canopy = (0.0, 0.0, 165.0)
    for k in range(4):
        a = 2 * math.pi * (k + 0.2 * rng.random()) / 4
        start = (0.0, 0.0, 92.0 + 10.0 * k)
        direction = (math.cos(a), math.sin(a), 0.9 + 0.3 * rng.random())
        tapered_prism(mesh, BARK, start, direction, 46.0 + 8.0 * rng.random(), 2.6, 1.4, 4, 1.0, cap=True)
    assert trunk_top[2] < canopy[2]
    placed = 0
    while placed < 30:
        # rejection-sample card centres in the canopy ellipsoid (radii 56, 56, 38)
        x, y, z = (rng.uniform(-1, 1) for _ in range(3))
        if x * x + y * y + z * z > 1.0:
            continue
        centre = (canopy[0] + 56.0 * x, canopy[1] + 56.0 * y, canopy[2] + 38.0 * z)
        theta = rng.uniform(0, 2 * math.pi)
        tilt = rng.uniform(-0.6, 0.6)
        normal = (math.cos(theta), math.sin(theta), tilt)
        up_hint = (rng.uniform(-0.3, 0.3), rng.uniform(-0.3, 0.3), 1.0)
        card(mesh, LEAVES, centre, normal, up_hint, 36.0, 36.0, canopy)
        placed += 1
    return mesh


def add_clump(mesh, rng, origin, size):
    """Three crossed grass quads standing on origin, 60 degrees apart, normals bent up and out from below the clump."""
    turn = rng.uniform(0, math.pi / 3)
    bend = (origin[0], origin[1], origin[2] - 16.0 * size)
    for k in range(3):
        a = turn + k * math.pi / 3
        width = 22.0 * size
        height = rng.uniform(15.0, 22.0) * size
        card(mesh, GRASS, origin, (math.cos(a), math.sin(a), 0.0), (0.0, 0.0, 1.0), width, height, bend,
             uv_bottom_at_base=True)


def make_grass_clump(rng):
    mesh = Mesh()
    add_clump(mesh, rng, (0.0, 0.0, 0.0), 1.0)
    return mesh


def make_grass_patch(rng):
    mesh = Mesh()
    for _ in range(25):
        add_clump(mesh, rng, (rng.uniform(-64, 64), rng.uniform(-64, 64), 0.0), rng.uniform(0.8, 1.2))
    return mesh


# ---------------------------------------------------------------- main

def main():
    os.makedirs(TEXTURES, exist_ok=True)
    os.makedirs(MODELS, exist_ok=True)
    written = []
    bark_c, bark_nrm = make_bark(random.Random(SEED + 1))
    written.append(save_tga(bark_c, "bark_c"))
    written.append(save_tga(bark_nrm, "bark_nrm"))
    written.append(save_tga(make_leaves(random.Random(SEED + 2)), "leaves"))
    written.append(save_tga(make_grass(random.Random(SEED + 3)), "grass"))
    written.append(save_tga(make_dirt(random.Random(SEED + 4)), "dirt_c"))
    written.append(make_tree(random.Random(SEED + 5)).write("tree"))
    written.append(make_grass_clump(random.Random(SEED + 6)).write("grass_clump"))
    written.append(make_grass_patch(random.Random(SEED + 7)).write("grass_patch"))
    for path in written:
        print(os.path.relpath(path, REPO))


if __name__ == "__main__":
    main()
