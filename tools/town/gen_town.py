#!/usr/bin/env python3
"""Generate the town's first block, content/town/maps/town1.map, with its view and PVS lists.

  tools/town/gen_town.py                         write maps/town1.map, town1.cameras and town1.pvs
  tools/town/gen_town.py --pull-lights MAP       rewrite tools/town/town1_lights.json from a map saved by the editor

The committed town1.map is always this script's output (content/town/README.md, "Generated content"). Lights are the
one part tuned by hand: they live in town1_lights.json, which this script appends to the map. After tuning lights in the
editor, save, pull, regenerate and commit both. Any other editor edit is lost on regeneration.

The layout (all on the 8-unit grid, Z up):
  - two streets 320 wide along X and Y, crossing at the origin, each arm closed by a facade at 1024;
  - a sky ceiling at z = 256 and one fog volume over the whole street level;
  - NW and NE corners: solid blocks, each with a door on its facade that teleports to a sealed room below the corner;
  - SW corner: a room at street level behind an open doorway on the south arm, with no light of its own;
  - SE corner: a fenced lawn open to the sky, with trees and grass;
  - one street lamp per arm: post, arm and a reflector cover whose inside is emissive, with a bench beside its post;
  - one trash bin per arm, toward the arm's end.
"""

import argparse
import json
import math
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MAPS = os.path.join(REPO, "content", "town", "maps")
LIGHTS = os.path.join(REPO, "tools", "town", "town1_lights.json")
NAME = "town1"

# ---------------------------------------------------------------- layout table

HALF = 160        # half the street width: facades stand at +-HALF
END = 1024        # each arm ends at +-END
SHELL = 16        # thickness of walls, floors and the outer hull
CEIL = 256        # underside of the sky
WALK = 64         # sidewalk width
KERB = 8          # sidewalk height
HINTS = (HALF, 592)   # hint planes across each arm: at the crossroads and mid-arm
EYE = 24          # a standing player's origin above the floor

ROOM = 384        # interiors are ROOM x ROOM
ROOM_H = 160
DOOR_W, DOOR_H = 64, 112
DOOR_AT = 320     # distance of every doorway's centre from the map centre, along its arm

# the street-level interior (SW) and the lawn (SE), as (x0, y0, x1, y1)
SW_ROOM = (-HALF - SHELL - ROOM, -HALF - SHELL - ROOM, -HALF - SHELL, -HALF - SHELL)
YARD = (320, -HALF - ROOM, 704, -HALF)
FENCE_GAP = (480, 544)
FENCE_H = 96

# sealed rooms below their corners: interior (x0, y0, x1, y1), floor height, and the wall their door is on
SEALED_FLOOR = -320
NW_ROOM = (-752, 368, -368, 752)
NE_ROOM = (368, 368, 752, 752)

# arms: name -> outward direction. An arm's lateral axis is its direction turned 90 degrees to the left.
ARMS = {"west": (-1, 0), "north": (0, 1), "east": (1, 0), "south": (0, -1)}
LAMPS = {"west": 448, "north": 448, "east": 448, "south": DOOR_AT}   # along-arm position; the south one faces the SW doorway
LAMP_POST = HALF - WALK // 2     # lateral position of the post (the sidewalk's middle)
LAMP_HEAD = 48                   # lateral position of the cover's centre, over the roadway
LAMP_TOP = 192                   # the cover's emissive ceiling
LAMP_SKIRT = 16                  # height of the cover's emissive sides
BENCH = (32, 126)                # a bench beside each lamp post, against the wall: its extent along the arm, from the lamp
# a trash bin per arm, toward its end: (along-arm position, side: 1 = the lamp's sidewalk, -1 = the other). The
# positions and sides differ from arm to arm on purpose.
BIN_HALF, BIN_H, BIN_WALL = 20, 56, 4   # a bin is 40 x 40 x 56, oversized like the lamp covers
BINS = {"west": (800, -1), "north": (856, 1), "east": (776, -1), "south": (832, 1)}
WINDOWS = (480, 736)             # along-arm positions of the facade windows, both sides of every arm

DETAIL = 134217728  # CONTENTS_DETAIL


# ---------------------------------------------------------------- brushes

T = "town/"
CAULK = T + "caulk"

# Quake's texture axes per face: (s axis index, t axis index); t runs against the second axis
FACE_AXES = {"-x": (1, 2), "+x": (1, 2), "-y": (0, 2), "+y": (0, 2), "-z": (0, 1), "+z": (0, 1)}


def num(v):
    return ("%.6f" % v).rstrip("0").rstrip(".") if v != int(v) else "%d" % int(v)


def box(lo, hi, tex=CAULK, faces=None, detail=False):
    """An axis-aligned brush. faces maps a face name (-x +x -y +y -z +z) to a texture, or to (texture, "fit") to
    stretch a 128 x 128 image once over the face; faces not named get tex."""
    (x1, y1, z1), (x2, y2, z2) = lo, hi
    assert x1 < x2 and y1 < y2 and z1 < z2, (lo, hi)
    faces = faces or {}
    planes = [
        ("-x", (x1, y1, z1), (x1, y1 + 1, z1), (x1, y1, z1 + 1)),
        ("+x", (x2, y2, z2), (x2, y2, z2 + 1), (x2, y2 + 1, z2)),
        ("-y", (x1, y1, z1), (x1, y1, z1 + 1), (x1 + 1, y1, z1)),
        ("+y", (x2, y2, z2), (x2 + 1, y2, z2), (x2, y2, z2 + 1)),
        ("-z", (x1, y1, z1), (x1 + 1, y1, z1), (x1, y1 + 1, z1)),
        ("+z", (x2, y2, z2), (x2, y2 + 1, z2), (x2 + 1, y2, z2)),
    ]
    out = ["{"]
    for face, a, b, c in planes:
        spec = faces.get(face, tex)
        align = "0 0 0 0.5 0.5"
        if isinstance(spec, tuple):
            spec, mode = spec
            assert mode == "fit"
            si, ti = FACE_AXES[face]
            ss = (hi[si] - lo[si]) / 128.0
            ts = (hi[ti] - lo[ti]) / 128.0
            # s = (p[si] / ss + shift_s) / 128 is 0 at lo; t = (-p[ti] / ts + shift_t) / 128 is 0 at hi (the image's top)
            align = "%s %s 0 %s %s" % (num(-lo[si] / ss), num(hi[ti] / ts), num(ss), num(ts))
        pts = " ".join("( %d %d %d )" % p for p in (a, b, c))
        out.append("%s %s %s %d 0 0" % (pts, spec, align, DETAIL if detail else 0))
    out.append("}")
    return "\n".join(out)


def facing(v):
    """The face name whose outward normal is the axis vector v."""
    return {(1, 0): "+x", (-1, 0): "-x", (0, 1): "+y", (0, -1): "-y"}[tuple(v)]


def span(c, n, depth, half):
    """Bounds of a slab standing on a wall: centre c on the wall plane, normal n, reaching depth out from the wall and
    half to each side along the wall. Returns ((x0, y0), (x1, y1))."""
    side = (-n[1], n[0])
    xs = [c[0] + n[0] * d + side[0] * h for d in depth for h in (-half, half)]
    ys = [c[1] + n[1] * d + side[1] * h for d in depth for h in (-half, half)]
    return (min(xs), min(ys)), (max(xs), max(ys))


def arm_point(arm, along, lateral):
    d = ARMS[arm]
    side = (-d[1], d[0])
    return (d[0] * along + side[0] * lateral, d[1] * along + side[1] * lateral)


def arm_box(arm, along, lateral, z):
    """A box given in an arm's own coordinates: (a0, a1) along it, (b0, b1) across it, (z0, z1)."""
    corners = [arm_point(arm, a, b) for a in along for b in lateral]
    xs, ys = [p[0] for p in corners], [p[1] for p in corners]
    return (min(xs), min(ys), z[0]), (max(xs), max(ys), z[1])


def yaw(v):
    return int(round(math.degrees(math.atan2(v[1], v[0])))) % 360


class Map:
    def __init__(self):
        self.structural = []
        self.detail = []
        self.entities = []       # (keys, [brushes])
        self.hints = 0
        self.triggers = 0

    def solid(self, lo, hi, tex=CAULK, **faces):
        self.structural.append(box(lo, hi, tex, {k.replace("px", "+x").replace("nx", "-x").replace("py", "+y")
                                                 .replace("ny", "-y").replace("pz", "+z").replace("nz", "-z"): v
                                                 for k, v in faces.items()}))

    def prop(self, lo, hi, tex, faces=None):
        self.detail.append(box(lo, hi, tex, faces, detail=True))

    def entity(self, classname, brushes=None, **keys):
        self.entities.append((dict(classname=classname, **keys), brushes or []))

    def write(self, path, lights):
        out = ["// generated by tools/town/gen_town.py; do not edit (content/town/README.md, \"Generated content\")",
               "// entity 0", "{", '"classname" "worldspawn"', '"message" "Town block 1"', '"_blocksize" "0"']
        for i, b in enumerate(self.structural + self.detail):
            out += ["// brush %d" % i, b]
        out.append("}")
        n = 1
        for keys, brushes in self.entities + [(light, []) for light in lights]:
            out += ["// entity %d" % n, "{"]
            out += ['"%s" "%s"' % (k, v) for k, v in keys.items()]
            for i, b in enumerate(brushes):
                out += ["// brush %d" % i, b]
            out.append("}")
            n += 1
        with open(path, "w", newline="\n") as f:
            f.write("\n".join(out) + "\n")


# ---------------------------------------------------------------- the block

BOTTOM, TOP = -SHELL, CEIL + SHELL     # the street level's hull spans this height
BRICK, PLASTER = T + "brick", T + "plaster"


def streets(m):
    """Floors, sky, arm ends, sidewalks, hints and the fog volume."""
    strips = [((-END, -HALF), (END, HALF)), ((-HALF, HALF), (HALF, END)), ((-HALF, -END), (HALF, -HALF))]
    for (x0, y0), (x1, y1) in strips:
        m.solid((x0, y0, BOTTOM), (x1, y1, 0), pz=T + "asphalt")
        m.solid((x0, y0, CEIL), (x1, y1, TOP), T + "sky")
    for arm, d in ARMS.items():
        lo, hi = arm_box(arm, (END, END + SHELL), (-HALF, HALF), (BOTTOM, TOP))
        m.structural.append(box(lo, hi, CAULK, {facing((-d[0], -d[1])): BRICK}))
        for side in (-1, 1):
            lo, hi = arm_box(arm, (HALF, END), (side * (HALF - WALK), side * HALF), (0, KERB))
            m.prop(lo, hi, T + "sidewalk")
        for along in HINTS:
            lo, hi = arm_box(arm, (along, along + 8), (-HALF, HALF), (0, CEIL))
            m.structural.append(box(lo, hi, T + "skip", {facing((-d[0], -d[1])): T + "hint"}))
            m.hints += 1
    # one fog volume over the street level; every face lies inside the hull, and the sealed rooms are below it
    edge = END + SHELL // 2
    m.structural.append(box((-edge, -edge, -SHELL // 2), (edge, edge, CEIL + SHELL // 2), T + "fog"))


def corners(m):
    """The four building corners at street level."""
    far = END + SHELL
    # NW and NE: solid blocks; their interiors are the sealed rooms below
    m.solid((-far, HALF, BOTTOM), (-HALF, far, TOP), px=BRICK, ny=BRICK)
    m.solid((HALF, HALF, BOTTOM), (far, far, TOP), nx=BRICK, ny=BRICK)

    # SW: a room behind the south arm's west facade, entered through an open doorway
    x0, y0, x1, y1 = SW_ROOM
    wall = -HALF - SHELL
    d0, d1 = -DOOR_AT - DOOR_W // 2, -DOOR_AT + DOOR_W // 2
    m.solid((-far, -far, BOTTOM), (x0, -HALF, TOP), px=PLASTER, py=BRICK)                   # west of the room
    m.solid((x0, -far, BOTTOM), (-HALF, y0, TOP), px=BRICK, py=PLASTER)                     # south of the room
    m.solid((x0, wall, BOTTOM), (-HALF, -HALF, TOP), px=BRICK, py=BRICK, ny=PLASTER)        # north wall
    m.solid((wall, y0, BOTTOM), (-HALF, d0, TOP), px=BRICK, nx=PLASTER, py=BRICK)           # east wall, south of the doorway
    m.solid((wall, d1, BOTTOM), (-HALF, wall, TOP), px=BRICK, nx=PLASTER, ny=BRICK)         # east wall, north of it
    m.solid((wall, d0, DOOR_H), (-HALF, d1, TOP), px=BRICK, nx=PLASTER, nz=BRICK)           # lintel
    m.solid((wall, d0, BOTTOM), (-HALF, d1, 0), pz=T + "sidewalk")                          # threshold
    m.solid((x0, y0, BOTTOM), (x1, y1, 0), pz=T + "woodfloor")
    m.solid((x0, y0, ROOM_H), (x1, y1, TOP), nz=PLASTER)

    # SE: the lawn, open to the east arm along its north edge
    x0, y0, x1, y1 = YARD
    m.solid((HALF, -far, BOTTOM), (x0, -HALF, TOP), nx=BRICK, px=BRICK, py=BRICK)
    m.solid((x1, -far, BOTTOM), (far, -HALF, TOP), nx=BRICK, py=BRICK)
    m.solid((x0, -far, BOTTOM), (x1, y0, TOP), py=BRICK)
    m.solid((x0, y0, BOTTOM), (x1, y1, 0), pz=T + "dirt")
    m.solid((x0, y0, CEIL), (x1, y1, TOP), T + "sky")
    for f0, f1 in ((x0, FENCE_GAP[0]), (FENCE_GAP[1], x1)):
        m.prop((f0, y1 - 8, 0), (f1, y1 - 4, FENCE_H), CAULK, {"-y": T + "fence", "+y": T + "fence"})
    for model, x, y, angle, scale in (("tree", 420, -300, 30, None), ("tree", 600, -420, 200, "0.85"),
                                      ("grass_patch", 430, -450, 0, None), ("grass_patch", 600, -260, 90, None),
                                      ("grass_patch", 610, -470, 45, None), ("grass_clump", 500, -330, 300, None),
                                      ("grass_clump", 380, -220, 120, None), ("grass_clump", 660, -350, 20, "1.2")):
        keys = dict(origin="%d %d 0" % (x, y), angle=angle, model="models/town/%s.obj" % model)
        if scale:
            keys["modelscale"] = scale
        m.entity("misc_model", **keys)


def door(m, c, floor, n, target, name):
    """A teleporting door on a wall: centre c = (x, y) on the wall plane, standing on floor, wall normal n. A static
    slab in a frame with a trigger_teleport in front of it, and the arrival point of the door that leads back here,
    48 units out from the wall and facing away from it."""
    half = DOOR_W // 2
    (x0, y0), (x1, y1) = span(c, n, (0, 4), half)
    m.prop((x0, y0, floor), (x1, y1, floor + DOOR_H), CAULK, {facing(n): (T + "door", "fit")})
    for lo_h, hi_h in ((-half - 8, -half), (half, half + 8)):                      # jambs
        side = (-n[1], n[0])
        mid = (c[0] + side[0] * (lo_h + hi_h) / 2, c[1] + side[1] * (lo_h + hi_h) / 2)
        (jx0, jy0), (jx1, jy1) = span(mid, n, (0, 8), 4)
        m.prop((jx0, jy0, floor), (jx1, jy1, floor + DOOR_H + 8), T + "metal")
    (hx0, hy0), (hx1, hy1) = span(c, n, (0, 8), half)                              # head
    m.prop((hx0, hy0, floor + DOOR_H), (hx1, hy1, floor + DOOR_H + 8), T + "metal")
    (tx0, ty0), (tx1, ty1) = span(c, n, (4, 20), half)
    m.entity("trigger_teleport", [box((tx0, ty0, floor), (tx1, ty1, floor + DOOR_H), T + "trigger")], target=target)
    m.triggers += 1
    m.entity("target_position", origin="%d %d %d" % (c[0] + n[0] * 48, c[1] + n[1] * 48, floor + EYE),
             angle=yaw(n), targetname=name)


def sealed_room(m, room, floor_tex=T + "woodfloor"):
    x0, y0, x1, y1 = room
    z0, z1 = SEALED_FLOOR, SEALED_FLOOR + ROOM_H
    m.solid((x0, y0, z0 - SHELL), (x1, y1, z0), pz=floor_tex)
    m.solid((x0, y0, z1), (x1, y1, z1 + SHELL), nz=PLASTER)
    m.solid((x0 - SHELL, y0 - SHELL, z0 - SHELL), (x0, y1 + SHELL, z1 + SHELL), px=PLASTER)
    m.solid((x1, y0 - SHELL, z0 - SHELL), (x1 + SHELL, y1 + SHELL, z1 + SHELL), nx=PLASTER)
    m.solid((x0, y0 - SHELL, z0 - SHELL), (x1, y0, z1 + SHELL), py=PLASTER)
    m.solid((x0, y1, z0 - SHELL), (x1, y1 + SHELL, z1 + SHELL), ny=PLASTER)


def interiors(m):
    """The two sealed rooms and the four teleporting doors that join them to the street."""
    sealed_room(m, NW_ROOM)
    sealed_room(m, NE_ROOM)
    # NW: a door on the west arm's north facade, and one on the room's south wall
    street = arm_point("west", DOOR_AT, -HALF)
    inside = ((NW_ROOM[0] + NW_ROOM[2]) // 2, NW_ROOM[1])
    door(m, street, KERB, (0, -1), "nw_room", "nw_street")
    door(m, inside, SEALED_FLOOR, (0, 1), "nw_street", "nw_room")
    # NE: a door on the north arm's east facade, and one on the room's west wall
    street = arm_point("north", DOOR_AT, -HALF)
    inside = (NE_ROOM[0], (NE_ROOM[1] + NE_ROOM[3]) // 2)
    door(m, street, KERB, (-1, 0), "ne_room", "ne_street")
    door(m, inside, SEALED_FLOOR, (1, 0), "ne_street", "ne_room")
    # the NW room's ceiling light panel: its underside is the emitter of the light_area in town1_lights.json
    cx, cy = panel_centre()
    top = SEALED_FLOOR + ROOM_H
    m.prop((cx - 32, cy - 32, top - 8), (cx + 32, cy + 32, top), T + "metal", {"-z": T + "panel"})


def panel_centre():
    return ((NW_ROOM[0] + NW_ROOM[2]) // 2, (NW_ROOM[1] + NW_ROOM[3]) // 2)


def lamp_head(arm):
    return arm_point(arm, LAMPS[arm], LAMP_HEAD)


def lamps(m):
    """One street lamp per arm: a post on the sidewalk, an arm over the roadway and a reflector cover. The cover's
    inside (its ceiling and the four inner sides) is the emitter; every emissive face is wholly exposed, so the flux
    the editor derives for the lamp's proxy light counts no hidden area."""
    metal, glow = T + "metal", T + "lamp"
    for arm in ARMS:
        a = LAMPS[arm]
        z0, z1, z2 = LAMP_TOP - LAMP_SKIRT, LAMP_TOP, LAMP_TOP + 8
        lo, hi = arm_box(arm, (a - 4, a + 4), (LAMP_POST - 4, LAMP_POST + 4), (KERB, z2 + 8))
        m.prop(lo, hi, metal)
        lo, hi = arm_box(arm, (a - 4, a + 4), (LAMP_HEAD, LAMP_POST - 4), (z2, z2 + 8))
        m.prop(lo, hi, metal)
        hx, hy = lamp_head(arm)
        m.prop((hx - 20, hy - 20, z1), (hx + 20, hy + 20, z2), metal, {"-z": glow})          # emissive ceiling
        for sx, sy in ((-1, 0), (1, 0), (0, -1), (0, 1)):                                    # the four sides
            if sx:
                lo, hi = (hx + sx * 20, hy - 20), (hx + sx * 24, hy + 20)
            else:
                lo, hi = (hx - 20, hy + sy * 20), (hx + 20, hy + sy * 24)
            (bx0, bx1), (by0, by1) = sorted((lo[0], hi[0])), sorted((lo[1], hi[1]))
            m.prop((bx0, by0, z0), (bx1, by1, z1), metal, {facing((-sx, -sy)): glow})         # skirt, emissive inside
            m.prop((bx0, by0, z1), (bx1, by1, z2), metal)                                    # rim above it
        for cx in (-1, 1):
            for cy in (-1, 1):
                (bx0, bx1), (by0, by1) = sorted((hx + cx * 20, hx + cx * 24)), sorted((hy + cy * 20, hy + cy * 24))
                m.prop((bx0, by0, z0), (bx1, by1, z2), metal)                                # corner posts


def props(m):
    """Street furniture, all detail brushes: a bench against the wall beside each lamp post (wooden seat and back on
    two metal legs), and a square metal trash bin with an open top on each arm's sidewalk."""
    metal, wood = T + "metal", T + "woodfloor"
    for arm in ARMS:
        a0, a1 = LAMPS[arm] + BENCH[0], LAMPS[arm] + BENCH[1]
        # proportions the author tuned in the editor on the east arm's bench (2026-11-21), used for all four, with the
        # legs evened out: each 6 in from its end and reaching the wall
        for lo, hi, tex in ((arm_box(arm, (a0, a1), (130, 156), (KERB + 18, KERB + 22)) + (wood,)),             # seat
                            (arm_box(arm, (a0, a1), (154, 156), (KERB + 16, KERB + 48)) + (wood,)),             # back
                            (arm_box(arm, (a0 + 6, a0 + 10), (136, 160), (KERB, KERB + 20)) + (metal,)),        # legs
                            (arm_box(arm, (a1 - 10, a1 - 6), (136, 160), (KERB, KERB + 20)) + (metal,))):
            m.prop(lo, hi, tex)
        along, side = BINS[arm]
        h, w = BIN_HALF, BIN_WALL
        b0, b1 = sorted((side * (HALF - 4 - 2 * h), side * (HALF - 4)))                  # 4 units off the wall
        z0, z1 = KERB, KERB + BIN_H
        for la, lb in (((along - h, along + h), (b0, b0 + w)), ((along - h, along + h), (b1 - w, b1)),   # two walls
                       ((along - h, along - h + w), (b0 + w, b1 - w)), ((along + h - w, along + h), (b0 + w, b1 - w))):
            lo, hi = arm_box(arm, la, lb, (z0, z1))
            m.prop(lo, hi, metal)
        lo, hi = arm_box(arm, (along - h + w, along + h - w), (b0 + w, b1 - w), (z0, z0 + w))             # bottom
        m.prop(lo, hi, metal)


def windows(m):
    """Opaque panes on the facades, both sides of every arm, wherever a wall stands."""
    for arm, d in ARMS.items():
        for side in (-1, 1):
            n = (d[1] * side, -d[0] * side)                    # from the wall toward the street
            for along in WINDOWS:
                c = arm_point(arm, along, side * HALF)
                if YARD[0] <= c[0] <= YARD[2] and c[1] == YARD[3]:
                    continue                                   # the lawn's open side
                (x0, y0), (x1, y1) = span(c, n, (0, 4), 32)
                m.prop((x0, y0, 96), (x1, y1, 176), T + "metal", {facing(n): (T + "window", "fit")})


def spawns(m):
    m.entity("info_player_start", origin="0 0 %d" % EYE, angle=0)
    for arm, d in ARMS.items():
        x, y = arm_point(arm, 704, 0)
        m.entity("info_player_deathmatch", origin="%d %d %d" % (x, y, EYE), angle=yaw((-d[0], -d[1])))


def build():
    m = Map()
    streets(m)
    corners(m)
    interiors(m)
    lamps(m)
    props(m)
    windows(m)
    spawns(m)
    return m


# ---------------------------------------------------------------- lights

def default_lights():
    """The first lights file. Attached lights (the lamps' proxies and the panel's light_area) carry values estimated
    from their shaders; "Re-derive light from emitter" in the editor replaces them (README, "Lights")."""
    out = [{"classname": "light_sun", "origin": "0 0 200", "angles": "50 145 0", "intensity": "2",
            "_color": "0.296178 0.309361 0.326665"}]
    for arm in ARMS:
        hx, hy = lamp_head(arm)
        out.append({"classname": "light_spot", "origin": "%d %d %d" % (hx, hy, LAMP_TOP - 12), "angles": "90 0 0",
                    "cone": "60", "cone_inner": "45", "radius": "512", "intensity": "209000",
                    "_color": "1 0.672443 0.304987", "_proxy": "1", "_shadows": "1"})
    cx, cy = panel_centre()
    out.append({"classname": "light_area", "origin": "%d %d %d" % (cx, cy, SEALED_FLOOR + ROOM_H - 8),
                "angles": "90 0 0", "width": "64", "height": "64", "radius": "512", "intensity": "8",
                "_color": "0.806952 0.871367 1", "_frustum": "70"})
    out.append({"classname": "light", "origin": "%d %d %d" % ((NE_ROOM[0] + NE_ROOM[2]) // 2,
                                                              (NE_ROOM[1] + NE_ROOM[3]) // 2, SEALED_FLOOR + 120),
                "intensity": "150000", "radius": "400", "_color": "1 0.85 0.65"})
    return out


def ordered(entity):
    """classname first, the other keys sorted, so the file is stable whatever order the editor saved them in."""
    return {"classname": entity["classname"], **{k: entity[k] for k in sorted(entity) if k != "classname"}}


def write_lights(lights):
    with open(LIGHTS, "w", newline="\n") as f:
        json.dump([ordered(e) for e in lights], f, indent=2)
        f.write("\n")


def read_lights():
    if not os.path.exists(LIGHTS):
        write_lights(default_lights())
    with open(LIGHTS) as f:
        return json.load(f)


def parse_entities(path):
    """The key/value pairs of every entity in a .map file, in file order (brushes and patches are skipped)."""
    entities, depth, current = [], 0, None
    pair = re.compile(r'^"([^"]*)"\s+"([^"]*)"$')
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line == "{":
                depth += 1
                if depth == 1:
                    current = {}
            elif line == "}":
                depth -= 1
                if depth == 0:
                    entities.append(current)
            elif depth == 1:
                found = pair.match(line)
                if found:
                    current[found.group(1)] = found.group(2)
    if depth != 0:
        sys.exit("%s: unbalanced braces" % path)
    return entities


def pull_lights(path):
    """Rewrite the lights file from a saved map: every entity whose classname starts with "light", and every entity a
    light targets. Nothing else in the map is read."""
    entities = parse_entities(path)
    lights = [e for e in entities if e.get("classname", "").startswith("light")]
    if not lights:
        sys.exit("%s: no light entity; %s left unchanged" % (path, os.path.relpath(LIGHTS, REPO)))
    wanted = {e["target"] for e in lights if "target" in e}
    targets = [e for e in entities if e.get("targetname") in wanted and e not in lights]
    missing = wanted - {e["targetname"] for e in targets}
    if missing:
        sys.exit("%s: light target(s) not found: %s; %s left unchanged"
                 % (path, ", ".join(sorted(missing)), os.path.relpath(LIGHTS, REPO)))
    write_lights(lights + targets)
    print("%s: %d lights, %d targets" % (os.path.relpath(LIGHTS, REPO), len(lights), len(targets)))


# ---------------------------------------------------------------- views and PVS pairs

def cameras():
    """View requests for the stock check: name, player origin, yaw. setviewpos pushes the player about 120 units
    along the yaw, so each has at least 160 units of open floor ahead."""
    sx, sy = lamp_head("south")
    below = SEALED_FLOOR + EYE
    return [
        ("street", -900, 0, EYE, 0, "the whole X street from its west end: the far facade, most of the fog's opacity distance away, meeting the sky"),
        ("crossroads", -420, -60, EYE, 15, "the crossroads from the west arm, with the NW door on the left"),
        ("lamp", sx - 100, sy - 330, EYE, 80, "the south arm's lamp, its pool of light and the SW doorway"),
        ("nw-room", NW_ROOM[0] + 60, NW_ROOM[1] + 60, below, 45, "the NW room under its ceiling panel"),
        ("ne-room", NE_ROOM[0] + 60, NE_ROOM[1] + 60, below, 45, "the NE room under its point light"),
        ("sw-room", SW_ROOM[0] + 60, -DOOR_AT, EYE, 0, "the SW room looking east at its open doorway: the street outside and the light spilling onto the floor"),
        ("sw-room-back", SW_ROOM[2] - 64, -DOOR_AT, EYE, 180, "the SW room looking west from the doorway: the wall farthest from it, which no light reaches directly"),
        ("lawn", 380, 60, EYE, 300, "the lawn through its fence from the east arm: trees and grass"),
    ]


def pvs_pairs():
    z = 64
    far = 900
    ends = {arm: arm_point(arm, far, 0) + (z,) for arm in ARMS}
    below = SEALED_FLOOR + 80
    rooms = {"nw-room": panel_centre() + (below,),
             "ne-room": ((NE_ROOM[0] + NE_ROOM[2]) // 2, (NE_ROOM[1] + NE_ROOM[3]) // 2, below)}
    sw = ((SW_ROOM[0] + SW_ROOM[2]) // 2, (SW_ROOM[1] + SW_ROOM[3]) // 2, z)
    street = dict(ends)
    street["crossroads"] = (0, 0, z)
    pairs = []
    for a, b in (("west", "north"), ("west", "south"), ("east", "north"), ("east", "south")):
        pairs.append((a + "-end", ends[a], b + "-end", ends[b], "hidden"))
    for room, p in rooms.items():
        for name, q in street.items():
            pairs.append((room, p, name if name == "crossroads" else name + "-end", q, "hidden"))
        pairs.append((room, p, "sw-room", sw, "hidden"))
    pairs.append(("nw-room", rooms["nw-room"], "ne-room", rooms["ne-room"], "hidden"))
    pairs.append(("west-end", ends["west"], "east-end", ends["east"], "visible"))
    pairs.append(("north-end", ends["north"], "south-end", ends["south"], "visible"))
    pairs.append(("sw-room", sw, "south-doorway", arm_point("south", DOOR_AT, 0) + (z,), "visible"))
    return pairs


def write_lists():
    with open(os.path.join(MAPS, NAME + ".cameras"), "w", newline="\n") as f:
        f.write("# generated by tools/town/gen_town.py; do not edit\n"
                "# View requests for the stock check: name, player origin x y z, yaw (degrees).\n"
                "# setviewpos pushes the player about 120 units along the yaw; each request has at least 160 units of\n"
                "# open floor ahead. The eye position the opengl1 run records in views.txt is the authoritative one.\n")
        for name, x, y, z, angle, what in cameras():
            f.write("# %s: %s\n%s %d %d %d %d\n" % (name, what, name, x, y, z, angle))
    with open(os.path.join(MAPS, NAME + ".pvs"), "w", newline="\n") as f:
        f.write("# generated by tools/town/gen_town.py; do not edit\n"
                "# Point pairs for tools/town/check_pvs.py: nameA x y z  nameB x y z  hidden|visible\n")
        for a, p, b, q, mark in pvs_pairs():
            f.write("%s %d %d %d  %s %d %d %d  %s\n" % ((a,) + tuple(p) + (b,) + tuple(q) + (mark,)))


def main():
    parser = argparse.ArgumentParser(description="Generate the town's first block.")
    parser.add_argument("--pull-lights", metavar="MAP",
                        help="rewrite town1_lights.json from the light entities of a map saved by the editor, and stop")
    args = parser.parse_args()
    if args.pull_lights:
        pull_lights(args.pull_lights)
        return
    m = build()
    lights = read_lights()
    os.makedirs(MAPS, exist_ok=True)
    path = os.path.join(MAPS, NAME + ".map")
    m.write(path, lights)
    write_lists()
    print("%s: %d structural and %d detail brushes, %d hint brushes, %d triggers, %d entities, %d lights"
          % (os.path.relpath(path, REPO), len(m.structural), len(m.detail), m.hints, m.triggers,
             len(m.entities), len(lights)))


if __name__ == "__main__":
    main()
