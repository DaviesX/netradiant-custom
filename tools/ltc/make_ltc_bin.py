#!/usr/bin/env python3
"""Convert upstream's LTC GGX tables (tools/ltc/ltc.js) to ltc_ggx.bin.

Layout (see tools/ltc/README.md): a 16-byte header, b"LTC1" then width 64,
height 64 and table count 2 as little-endian uint32; then g_ltc_1 and g_ltc_2
as two 64 x 64 RGBA little-endian float32 tables in upstream's entry order,
each value the float32 nearest to the printed decimal (ties to even).

Usage, from anywhere:
    make_ltc_bin.py            write setup/data/tools/gl/ltc_ggx.bin
    make_ltc_bin.py --check    rebuild in memory, compare with the committed
                               file and check the invariants; write nothing
"""

import argparse
import math
import os
import re
import struct
import sys
from fractions import Fraction

SIZE = 64
TABLES = ("g_ltc_1", "g_ltc_2")
COUNT = SIZE * SIZE * 4

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SRC = os.path.join(HERE, "ltc.js")
DST = os.path.join(ROOT, "setup", "data", "tools", "gl", "ltc_ggx.bin")

NUMBER = re.compile(r"^[+-]?(\d+(\.\d*)?|\.\d+)([eE][+-]?\d+)?$")


def f32_bits(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def f32_from_bits(b):
    return struct.unpack("<f", struct.pack("<I", b))[0]


def nearest_f32(text):
    """The float32 nearest to the exact decimal `text`, ties to even.

    Rounding through a double first could in principle round twice, so the
    double's float32 and its neighbours are compared exactly against the
    decimal's rational value."""
    exact = Fraction(text)
    guess = struct.unpack("<f", struct.pack("<f", float(text)))[0]
    if math.isinf(guess):
        raise ValueError("value out of float32 range: " + text)
    bits = f32_bits(guess)
    candidates = {bits}
    for d in (-1, 1):
        nb = bits + d
        if 0 <= nb <= 0xFFFFFFFF:
            v = f32_from_bits(nb)
            if not math.isnan(v) and not math.isinf(v):
                candidates.add(nb)
    # -0.0 and +0.0 share a value; keep the sign of the printed text.
    best = min(
        candidates,
        key=lambda b: (abs(Fraction(f32_from_bits(b)) - exact), b & 1),
    )
    value = f32_from_bits(best)
    if value == 0.0:
        value = -0.0 if text.lstrip().startswith("-") else 0.0
    return value


def parse_tables(text):
    tables = []
    for name in TABLES:
        m = re.search(r"var\s+" + name + r"\s*=\s*\[(.*?)\]\s*;", text, re.S)
        if not m:
            sys.exit("make_ltc_bin: array %s not found in %s" % (name, SRC))
        tokens = [t.strip() for t in m.group(1).split(",")]
        if tokens and tokens[-1] == "":
            tokens.pop()  # trailing comma before "]"
        for t in tokens:
            if not NUMBER.match(t):
                sys.exit("make_ltc_bin: %s: bad number %r" % (name, t))
        if len(tokens) != COUNT:
            sys.exit(
                "make_ltc_bin: %s has %d values, expected %d"
                % (name, len(tokens), COUNT)
            )
        tables.append(tokens)
    return tables


def build(tables):
    out = bytearray(b"LTC1")
    out += struct.pack("<III", SIZE, SIZE, len(TABLES))
    for tokens in tables:
        out += struct.pack("<%df" % COUNT, *(nearest_f32(t) for t in tokens))
    return bytes(out)


def check(data, tables):
    errors = []
    expected_size = 16 + len(TABLES) * COUNT * 4
    if len(data) != expected_size:
        errors.append("size %d, expected %d" % (len(data), expected_size))
        return errors
    if data[:4] != b"LTC1" or struct.unpack("<III", data[4:16]) != (SIZE, SIZE, 2):
        errors.append("bad header %r" % data[:16])
    for k, tokens in enumerate(tables):
        off = 16 + k * COUNT * 4
        values = struct.unpack("<%df" % COUNT, data[off : off + COUNT * 4])
        for i, (t, v) in enumerate(zip(tokens, values)):
            if f32_bits(v) != f32_bits(nearest_f32(t)):
                errors.append("%s[%d]: %r != float32(%s)" % (TABLES[k], i, v, t))
        if k == 1:
            bad = [i for i in range(COUNT // 4) if f32_bits(values[4 * i + 2]) != 0]
            if bad:
                errors.append("g_ltc_2 third component nonzero at %s" % bad[:8])
    return errors


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    with open(SRC, "r", encoding="ascii") as f:
        tables = parse_tables(f.read())
    data = build(tables)
    errors = check(data, tables)
    if errors:
        sys.exit("make_ltc_bin: " + "; ".join(errors[:10]))

    if args.check:
        try:
            with open(DST, "rb") as f:
                committed = f.read()
        except OSError as e:
            sys.exit("make_ltc_bin: %s" % e)
        if committed != data:
            sys.exit("make_ltc_bin: %s differs from a fresh conversion" % DST)
        print("make_ltc_bin: %s OK (%d bytes)" % (DST, len(data)))
        return

    os.makedirs(os.path.dirname(DST), exist_ok=True)
    with open(DST, "wb") as f:
        f.write(data)
    print("make_ltc_bin: wrote %s (%d bytes)" % (DST, len(data)))


if __name__ == "__main__":
    main()
