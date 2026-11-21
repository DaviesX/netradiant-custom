# LTC tables (GGX)

The area-light pass of the editor's PBR lighting preview (and `renderer_sh`,
task C7) shades `light_area` specular with linearly transformed cosines. The
tables come unchanged from upstream:

- **Repository:** <https://github.com/selfshadow/ltc_code>
- **File:** `fit/results/ltc.js` (arrays `g_ltc_1` and `g_ltc_2`), vendored here
  as `ltc.js`
- **Commit:** `31e5e96b54f98f33098f8503003119ba2231a1c6` (recorded with
  `git rev-parse HEAD` on the clone, 2026-11-20; upstream `HEAD`, committed
  2019-04-14). `ltc.js` and `LICENSE` last changed upstream in
  `5c0770b74114b5dd38e9dae1b93f8486af7eac1b` (2017-10-22).
- **Licence:** `LICENSE`, copied unchanged: a two-clause BSD licence with an
  extra clause asking users of the code to cite the paper. Redistribution in
  source and binary form is allowed if the notice is retained (source) or
  reproduced in the documentation shipped with the binary. The binary
  `ltc_ggx.bin` therefore installs with its notice,
  `setup/data/tools/gl/ltc_ggx.LICENSE.txt` (a byte-for-byte copy of `LICENSE`
  followed by the citation below), as `gl/ltc_ggx.LICENSE.txt`.

Citation the licence asks for:

> Eric Heitz, Jonathan Dupuy, Stephen Hill and David Neubelt.
> Real-Time Polygonal-Light Shading with Linearly Transformed Cosines.
> ACM Transactions on Graphics (Proceedings of ACM SIGGRAPH 2016) 35(4), 2016.
> Project page: https://eheitzresearch.wordpress.com/415-2/

This is the 2017 revision of the fit, which adds the magnitude and Fresnel
terms (Hill and Heitz 2017) and the sphere-horizon term.

## Converter

`make_ltc_bin.py` reads `ltc.js` and writes `setup/data/tools/gl/ltc_ggx.bin`,
which is committed. Re-run it from the repository root:

    python3 tools/ltc/make_ltc_bin.py

It is deterministic: re-running it reproduces the committed file byte for
byte. `python3 tools/ltc/make_ltc_bin.py --check` rebuilds in memory, compares
with the committed file and checks the invariants below without writing.

## `ltc_ggx.bin` layout

All values little-endian. Size 16 + 2 × 64 × 64 × 4 × 4 = 131088 bytes.

| Offset | Size     | Content                                               |
|--------|----------|-------------------------------------------------------|
| 0      | 4        | magic `LTC1`                                          |
| 4      | 4        | width 64 (`uint32`)                                   |
| 8      | 4        | height 64 (`uint32`)                                  |
| 12     | 4        | table count 2 (`uint32`)                              |
| 16     | 65536    | table 1: 64 × 64 RGBA `float32` (`g_ltc_1`)           |
| 65552  | 65536    | table 2: 64 × 64 RGBA `float32` (`g_ltc_2`)           |

Each value is the `float32` nearest to upstream's printed decimal, in
upstream's entry order. Entry i = a + 64·t holds roughness index a (u, the fast
axis) and angle index t (v), as upstream's `tab[a + t*N]` (`fit/fitLTC.cpp:255`).

- **Table 1** holds the inverse matrix M⁻¹ normalised by its middle element:
  (M⁻¹₀₀, M⁻¹₀₂, M⁻¹₂₀, M⁻¹₂₂) in glm's column-major indexing `[column][row]`
  (`packTab`, `fit/fitLTC.cpp:339-365`; written by `writeJS`,
  `fit/export.h:122-149`). In GLSL (`webgl/shaders/ltc/ltc_quad.fs:439-443`):

      mat3 Minv = mat3(vec3(t1.x, 0, t1.y),
                       vec3(0,    1, 0   ),
                       vec3(t1.z, 0, t1.w));

- **Table 2** holds (magnitude, Fresnel, 0, sphere-horizon term). The third
  component is always 0 (`packTab`: "unused"). Specular is scaled by
  `F0 * t2.x + (1 - F0) * t2.y` (`ltc_quad.fs:447`). The area pass clips to the
  horizon and does not read the sphere term; it is kept so the file is
  upstream's data unchanged.

## Indexing

- u is the perceptual roughness r: upstream fits entry a at roughness a/63 with
  alpha = roughness² (`fit/fitLTC.cpp:198-199`), the same alpha = r² as the
  point pass.
- v = √(1 − N·V), with N·V clamped to [0, 1]: upstream fits entry t at
  x = t/63, cos θ = 1 − x² (`fit/fitLTC.cpp:192-194`).
- Both are scaled by 63/64 and offset by 0.5/64, so 0 and 1 land on texel
  centres (`LUT_SCALE`, `LUT_BIAS`, `ltc_quad.fs:33-34`, applied at `:434`).

The editor uploads both tables as one 64 × 128 `GL_RGBA32F` texture (table 1 in
rows 0-63, table 2 in rows 64-127) and samples table 1 at (u, v/2) and table 2
at (u, ½ + v/2). Line numbers above refer to the commit recorded here.
