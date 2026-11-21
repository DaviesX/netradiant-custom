#!/usr/bin/env python3
"""Check a compiled map's PVS against a list of point pairs.

  tools/town/check_pvs.py content/town/maps/town1.bsp content/town/maps/town1.pvs

The pairs file has one pair per line, "#" starting a comment:
  nameA x y z  nameB x y z  hidden|visible
Each point is located in the BSP (Quake 3 IBSP version 46) and the pair passes when the cluster visibility of A's
cluster to B's is what the line says. A point whose leaf has no cluster lies in solid or outside the map, and fails.
Exits 0 only if every pair passes. This is the test behind "the PVS behaves like an indoor level" (plan 0.2): doors
and entities play no part in it, only structural brushes and hint brushes do.

A map compiled without -vis has no visibility data; every pair then reads as visible, so "hidden" pairs fail.
"""

import struct
import sys

LUMP_PLANES, LUMP_NODES, LUMP_LEAFS, LUMP_VISIBILITY = 2, 3, 4, 16


class Bsp:
    def __init__(self, path):
        with open(path, "rb") as f:
            data = f.read()
        magic, version = struct.unpack_from("<4si", data, 0)
        if magic != b"IBSP" or version != 46:
            sys.exit("%s: not a Quake 3 BSP (IBSP version 46)" % path)

        def lump(index, record):
            offset, length = struct.unpack_from("<ii", data, 8 + 8 * index)
            size = struct.calcsize(record)
            return [struct.unpack_from(record, data, offset + i * size) for i in range(length // size)]

        self.planes = lump(LUMP_PLANES, "<4f")
        self.nodes = lump(LUMP_NODES, "<9i")     # plane, front child, back child, mins, maxs
        self.leafs = lump(LUMP_LEAFS, "<12i")    # cluster, area, mins, maxs, leaf surfaces, leaf brushes
        offset, length = struct.unpack_from("<ii", data, 8 + 8 * LUMP_VISIBILITY)
        if length >= 8:
            self.clusters, self.row = struct.unpack_from("<ii", data, offset)
            self.vis = data[offset + 8:offset + length]
        else:
            self.clusters, self.row, self.vis = 0, 0, b""

    def leaf(self, point):
        index = 0
        while index >= 0:
            plane, front, back = self.nodes[index][:3]
            nx, ny, nz, dist = self.planes[plane]
            index = front if nx * point[0] + ny * point[1] + nz * point[2] - dist >= 0 else back
        return self.leafs[-(index + 1)]

    def sees(self, a, b):
        """Whether cluster a's PVS holds cluster b. Without visibility data everything sees everything."""
        if not self.vis:
            return True
        return bool(self.vis[a * self.row + (b >> 3)] & (1 << (b & 7)))


def read_pairs(path):
    pairs = []
    with open(path) as f:
        for number, line in enumerate(f, 1):
            fields = line.split("#")[0].split()
            if not fields:
                continue
            if len(fields) != 9 or fields[8] not in ("hidden", "visible"):
                sys.exit("%s:%d: expected 'nameA x y z nameB x y z hidden|visible'" % (path, number))
            try:
                a = tuple(float(v) for v in fields[1:4])
                b = tuple(float(v) for v in fields[5:8])
            except ValueError:
                sys.exit("%s:%d: bad coordinate" % (path, number))
            pairs.append((fields[0], a, fields[4], b, fields[8]))
    if not pairs:
        sys.exit("%s: no pairs" % path)
    return pairs


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: check_pvs.py <map.bsp> <pairs file>")
    bsp = Bsp(sys.argv[1])
    pairs = read_pairs(sys.argv[2])
    if not bsp.vis:
        print("%s: no visibility data (compiled without -vis); every pair reads as visible" % sys.argv[1])
    failed = 0
    for name_a, a, name_b, b, want in pairs:
        ca, cb = bsp.leaf(a)[0], bsp.leaf(b)[0]
        solid = [name for name, cluster in ((name_a, ca), (name_b, cb)) if cluster < 0]
        if solid:
            got = "in solid: " + ", ".join(solid)
            ok = False
        else:
            # q3map2 writes a symmetric PVS; a pair counts as visible if either direction is set, so a one-sided
            # entry can only fail a "hidden" pair, never pass one
            seen = bsp.sees(ca, cb) or bsp.sees(cb, ca)
            got = "visible" if seen else "hidden"
            ok = got == want
        failed += not ok
        print("%-4s %-12s (cluster %3d)  %-14s (cluster %3d)  want %-7s got %s"
              % ("ok" if ok else "FAIL", name_a, ca, name_b, cb, want, got))
    print("%d pairs, %d failed, %d clusters" % (len(pairs), failed, bsp.clusters))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
