# Proposal: The q3map2 SH extension and solid hull mesh (B4)

## Why

B5 (`q3map2 -light -shbake`) bakes every luxel through sh-baker and must hand all three SH bands to `renderer_sh`. The BSP has no room for them, so they go into a side file in `maps/<name>/`, inside the level's pk3 (plan §4.3). The same file carries the solid hull mesh that `renderer_sh` uses for shadow casters (§3.5).

No such file exists yet. B5 needs it before it can write anything, and C5 and C6 need a stable format to read.

Plan §4.2 item 4 put this format in sh-baker. A first version was built there (sh-baker PR #64, closed unmerged), and it showed that the format has nothing to do with baking. sh-baker returns one result per point, and q3map2 produces everything the file holds:
- the page order and sizes, which follow its lightmap pages;
- the luxel placement;
- the hull mesh, built from its brush lumps.

So the format moves into q3map2, as the **q3map2 extension**. sh-baker stays as it is on `main`.

## What Changes

- **A new extension format.** It is a small, versioned, little-endian binary file, called the SH side file on disk. It holds:
  - an ordered list of **pages**, each with its own width and height. Each luxel has the nine SH coefficients (L0, L1, L2) as RGB, plus the environment-visibility value the bake computes. They are stored as IEEE half floats, laid out in planes per coefficient.
  - one **hull mesh**: float32 positions, uint32 triangle indices, and a list of per-hull index ranges.

  The byte layout and the SH basis are fully specified, so `renderer_sh` can read the file from the spec alone. The layout is the one PR #64 had.
- **A self-contained module in q3map2.** `tools/quake3/q3map2/q3map2_extension.{h,cpp}` holds the types, a writer, a reader, and an L0 `.hdr` dump for inspection.
  - It depends only on the standard library and the header-only `Vector3` from `libs/math`. It needs no Eigen and no sh-baker, so it builds without `SH_BAKER=1`.
  - It doesn't log or exit. Each function returns success, or false with a message, and B5 decides what to do with the message (q3map2's `Error()`).
- **The writer refuses malformed input.** Wrong counts, non-finite values, the bake's not-baked marker, and bad hull ranges or indices make it return false with a message naming the field, the value and the page or hull. It then writes nothing. Values outside the half range are clamped to it. The file is written atomically, so an I/O failure leaves no partial file.
- **The reader rejects malformed files** with a message, before allocating storage for counts the file can't hold. It never aborts.
- **The `.hdr` dump refuses `lm_` names**, because stock rend2 auto-loads `maps/<name>/lm_*.hdr`.
- **A standalone test program**, `q3map2_extension_test`, built and run by a new Makefile target `test-q3map2-extension`. q3map2 has no test framework, so it uses a small harness of its own.
- **q3map2 output is unchanged.** The module is linked into q3map2 but nothing calls it until B5.

## Capabilities

### New Capabilities
- `q3map2-sh-extension`: the q3map2 SH extension (the SH side file). This covers:
  - what it holds: SH pages, visibility and the hull mesh;
  - the SH basis;
  - its exact byte layout and version rule;
  - value encoding;
  - how the writer refuses bad input and reports failures;
  - how the reader rejects malformed files;
  - the L0 `.hdr` dump;
  - the module's dependency limits and test program.

### Modified Capabilities
None. sh-baker's capabilities are unchanged.

## Impact

- **q3map2 code:**
  - `tools/quake3/q3map2/q3map2_extension.h` and `.cpp` (new), linked into q3map2.
  - `tools/quake3/q3map2/q3map2_extension_test.cpp` (new).
  - The `Makefile` adds the object to q3map2, plus the test binary and the `test-q3map2-extension` target.
- **B5, later:**
  - B5 copies `BakeResult` entries into pages and builds the hull mesh. It calls the writer, chooses the file name in `maps/<name>/`, and turns a false return into `Error()`.
  - B5 inherits three contracts:
    - side-file page i is BSP lightmap page i, at the same size and luxel placement, because `renderer_sh` finds a surface's SH through its `lightmapNum`;
    - luxels the bake skipped are filled (zeros or dilation) before writing, because the file has no not-baked value and the writer refuses the bake's −1 marker;
    - hull positions are in BSP units, and the SH frame is the BSP world frame.
- **`renderer_sh` (C5, C6, later):** it reads this format. Nothing changes there now.
- **sh-baker:** no change. PR #64 was closed, and `tools/sh-baker` stays at `ce5419a`.
- **Docs:** this change updates both docs to match:
  - plan §4.2 item 4, which said sh-baker defines the format, and §4.3;
  - `CLAUDE.md`'s B4 line ("sh-baker PR: …").
- **Out of scope:**
  - SH for the light grid or for vertex-lit surfaces.
  - Compression inside the file (the pk3 is already a zip).
  - Building or shrinking hulls (B5).
  - The file's name and path (B5).
  - A q3map2 flag that writes the file (B5).
- **Landing:** one netradiant PR from branch `b4-sh-extension`, pushed to `fork` and opened against `DaviesX/netradiant-custom`.
