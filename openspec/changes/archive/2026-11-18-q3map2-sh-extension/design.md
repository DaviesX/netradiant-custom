# Design: The q3map2 SH extension and solid hull mesh (B4)

## Context

The motivation is in proposal.md. The relevant state:

- **sh-baker (`ce5419a`, unchanged).** `BakeSHLightMap` (`src/baker.h`) returns one result per point:
  - `BakeResult::sh_texture.pixels[i]`, an `SHCoeffs` with nine RGB coefficients;
  - `environment_visibility_texture.pixel_data[i]`, the fraction of the point's samples whose first ray reached the sky.

  Skipped points hold the marker −1.
- **The basis.** sh-baker's basis is fixed in `sh_coeffs.cpp`: real SH with no Condon–Shortley phase (Y1,−1 = 0.488603 y), in the order L0, L1m1, L10, L11, L2m2, L2m1, L20, L21, L22. `AccumulateRadiance` uses world-frame directions without rotating them. The bake's uniform-hemisphere estimator (`baker.cpp:237-240`) makes each coefficient the projection ∫ L_i Y dω over the hemisphere, not cosine-convolved.
- **q3map2's build.** q3map2 builds with `-std=c++20 -fno-exceptions -fno-rtti` (`Makefile` `CXXFLAGS_COMMON`), from an explicit object list (`$(INSTALLDIR)/q3map2.$(EXE)`), on Linux and Windows. It has no unit-test framework. Its messages go through `Sys_Printf`, `Sys_Warning` and `[[noreturn]] Error()` (`tools/quake3/common/inout.h`). `libs/math/vector.h` provides the header-only `Vector3` (`BasicVector3<float>`) that q3map2 uses everywhere.
- **The first attempt.** sh-baker PR #64 (closed) implemented this format there, with `Eigen::half`, glog `CHECK`s and gtest. The byte layout, the value rules and the test cases carry over. The error policy, the half conversion, the `.hdr` writer and the test harness change, because q3map2 has none of those dependencies.

## Goals / Non-Goals

**Goals:**
- A file `renderer_sh` can read with a short, dependency-free reader, using the spec alone.
- A writer that B5 can call from `light_sh.cpp` with plain types it fills itself.
- A module that builds and is tested without sh-baker, Eigen or the rest of q3map2.

**Non-Goals:**
- **Building pages or hulls.** That is B5, in `light_sh.cpp`.
- **The file name and location.** B5 chooses them under `maps/<name>/`. The only naming rule here is the `.hdr` dump's `lm_` refusal.
- **SH for the light grid or for vertex-lit surfaces.** A later version can add either.
- **Compression or streaming.** The pk3 compresses, and the writer builds the whole file in memory.
- **Big-endian testing.** The writer and reader build values byte by byte, so they are correct on either byte order, but only little-endian is tested.

## Decisions

### 1. The format lives in q3map2

sh-baker returns per-point results, and q3map2 produces everything the file holds. In detail:
- the page layout, which must match `StoreSurfaceLightmaps`' pages (§4.3);
- the luxel placement;
- the hulls, built from the brush lumps.

The serializer therefore sits beside its only producer, and changing the format never needs a cross-repo PR. `renderer_sh` reads the file from the spec, as it would have either way.
- *Alternative: in sh-baker (PR #64).* That kept gtest and a reusable C++ reader, but put a non-baking format, including a mesh that has nothing to do with the bake, into the baker's API. The user chose q3map2.

### 2. A custom binary format, not OpenEXR or KTX2

There is a fixed header, then pages one after another, then the hull section, as laid out in the spec.

- *Alternative: OpenEXR.* Several pages would need multi-part EXR or one file per page, EXR has no place for a mesh, and `renderer_sh` would need an EXR decoder inside ioquake3.
- *Alternative: KTX2.* It suits the GPU, but it is a large spec, has no mesh either, and would add a dependency to both sides.
- A flat layout with counts in the header lets the reader check the total size before it allocates anything, and lets a C or C++ reader in `renderer_sh` be about a page of code.

### 3. Pages and hulls in one file

Plan §3.5 and §4.3 put the hull mesh "into the side file". Keeping them together means a map has one extra file that either exists or doesn't, which matches §4.4's fallback ("a map without the side file falls back to the BSP lightmap").

### 4. Full RGB for every band, in half precision, as planes

- **Full RGB.** Plan §4.4 needs all bands. sh-renderer's luminance packing is a GPU-side choice that `renderer_sh` can still make when it loads the file.
- **`binary16`.** Lightmap radiance needs relative precision, not range. Half gives 11 bits and a maximum of 65504, and halves the size of float32. A 128×128 page takes 0.9 MB before zip compression.
- **Planes.** Each coefficient's plane, and the visibility plane, can be uploaded as one texture or array layer without reshuffling, and similar values next to each other compress better in the pk3.
- **Visibility included.** Plan §4.2 said "L0, L1, L2 per page". The bake also produces visibility, and sh-renderer's shading uses it for its sky term. Dropping it now would force a version 2 as soon as `renderer_sh` lights the sky. It costs one half per luxel.

### 5. API in `q3map2_extension.h`

```cpp
struct SHHullRange { uint32_t first_index = 0; uint32_t index_count = 0; };
struct SHHullMesh {
  std::vector<Vector3> positions;    // caller's world space and units
  std::vector<uint32_t> indices;     // outward, clockwise from outside
  std::vector<SHHullRange> hulls;    // contiguous, covering `indices`
};
struct SHQ3Map2ExtensionPage {
  int width = 0, height = 0;
  std::vector<float> coeffs;      // width * height * 27: [(luxel * 9 + k) * 3 + c]
  std::vector<float> visibility;  // width * height
};
struct SHQ3Map2Extension { std::vector<SHQ3Map2ExtensionPage> pages; SHHullMesh hull_mesh; };

bool SaveSHQ3Map2Extension(const SHQ3Map2Extension& ext, const char* path, std::string* error);
std::optional<SHQ3Map2Extension> LoadSHQ3Map2Extension(const char* path, std::string* error);
bool SaveSHQ3Map2ExtensionPageL0Hdr(const SHQ3Map2ExtensionPage& page, const char* path, std::string* error);
```

- **Naming.** The names are the ones chosen for PR #64. `Q3Map2Extension` says what the data is for, which is q3map2's extension of the BSP. The prose keeps "side file" for the file on disk, the term plan §4.3 uses, and the magic stays `SHSF`.
- **Coefficient layout in memory.** Coefficients are luxel-major in memory, the same order as `SHCoeffs::coeffs`, so B5 copies 27 floats per luxel straight from `BakeResult`. The planar order is a file concern, handled by the writer.
- **No sh-baker or Eigen types.** The module builds and is tested without `SH_BAKER=1`, and its header stays light enough for any q3map2 TU.
- **Paths are `const char*`,** as in the rest of q3map2.

### 6. Error policy: return values, no logging

- **Writer.** It checks the whole input before creating any file. On a caller bug it returns false with a message that names the field, the value and the page or hull index. B5 passes that message to `Error()`, so a caller bug still stops the compile, as `CHECK` would. On an I/O failure it also returns false, with the path and `strerror`.
  - The checks are the ones PR #64 had: counts, non-finite values, the not-baked marker (visibility outside [0, 1], a negative L0), hull ranges, indices, and `uint32` fit.
  - Visibility is checked first for each luxel, so the marker is reported as a visibility problem.
- **Reader.** It returns `std::nullopt` and a message. It reads the whole file into memory, checks the magic and the version, and then walks the page sizes without allocating. Each page's `width * height` is compared with the remaining bytes divided by 56 before anything is multiplied, so 64-bit arithmetic can't overflow. Only once the sizes match the file does it decode, applying the writer's value rules. Messages are built only on failure.
- **Atomic write.** The writer serializes into a buffer and writes `<path>.tmp` with `std::FILE`. It then calls `std::filesystem::rename(tmp, path, error_code)`, which replaces an existing file on both POSIX and Windows, where plain `std::rename` would fail. On any failure it removes the temporary file. It doesn't `fsync`, so a system crash right after the rename can leave an empty or partial file. That is acceptable for a build artifact that the next compile rewrites.
- **Why no `Sys_Warning` or `Error()` inside the module.** Calling them would tie the module to `inout.o` and its globals, so the test program couldn't link it alone. It would also make the writer's refusals untestable without forking.

### 7. A hand-written half converter

There is no Eigen in q3map2's default build, so the module has its own `FloatToHalf` and `HalfToFloat`, written with bit operations on `std::bit_cast`.
- `FloatToHalf` clamps finite values to ±65504 first. It then rounds to nearest even, for normals by adding the rounding bias to the dropped mantissa bits, and for subnormals by shifting the full significand. It keeps the sign of zero.
- `HalfToFloat` is exact.

The tests pin it down exhaustively: every finite half bit pattern round-trips, and the spec's tie and subnormal cases hold.
- *Alternative: `Eigen::half` under `SH_BAKER=1` only.* That would make the format depend on the bake build, and the test program on Eigen.

### 8. A hand-written RGBE writer

The `.hdr` dump writes a flat, uncompressed Radiance file: the header `#?RADIANCE`, `FORMAT=32-bit_rle_rgbe`, a blank line, then `-Y <h> +X <w>`, followed by the pixels top row first. Flat scanlines are valid Radiance input. The standard `frexp` encoding truncates each component into its 8-bit mantissa; the scale is an exact power of two, so the error is below 1/128 of the pixel's largest component. Negative L0 components are written as 0, and components are clamped to 65504, the range the extension stores, which also keeps the exponent within its byte (2^127 or more would wrap it). A non-finite L0 component is refused, as the writer refuses it. The `lm_` check is a case-insensitive prefix test on the part of the path after the last `/` or `\`. The test program decodes the file with a small RGBE reader of its own.

### 9. A standalone test program

`q3map2_extension_test.cpp` has its own `main` and a small check macro. A failing check prints its file, line and values and increments a counter. `main` returns non-zero if any check failed. The test cases are PR #64's, with each death test now a check on the return value and the message. Two kinds are added: the exhaustive half round-trip, and a check that a refused write leaves no file.
- The Makefile builds `$(INSTALLDIR)/q3map2_extension_test.$(EXE)` from `q3map2_extension.o` and the test object. The phony target `test-q3map2-extension` builds and runs it.
- The test writes into a fresh directory under the system temp directory and removes it afterwards.

### 10. Branching and gate

This change is netradiant-only, on branch `b4-sh-extension`, which started from `b3-progress-cancel`. The gate:
- q3map2 builds with the module linked in;
- `make test-q3map2-extension` passes;
- the stock check passes;
- the bench BSP compiled by the new q3map2 is compared with one compiled before the change, and any difference is reported.

sh-baker is untouched.

## Risks / Trade-offs

- **[Half range clips very bright luxels]**: emissive faces seen at close range could exceed 65504. → The writer clamps rather than failing, and the header states the limit. A later version could store a per-page scale.
- **[The format is frozen before `renderer_sh` reads it]**: C6 may want something different. → Any change is version 2, and the reader rejects versions it doesn't know. The golden-bytes test keeps version 1 from drifting.
- **[Page i must be BSP lightmap i]**: `renderer_sh` can only find a surface's SH through the surface's BSP `lightmapNum`. → This is a contract on B5: side-file page i is BSP lightmap page i, at the same size and luxel placement, with `-deluxe` pages excluded or matched.
- **[No not-baked value]**: a −1 that the renderer can't tell from data would light those luxels wrongly. → The writer refuses the bake's marker, and B5 fills skipped luxels before writing.
- **[Frame and units are the caller's]**: if B5 scales the scene to metres for the bake and writes the hulls in metres, `renderer_sh` would misplace them. → B5's spec must say that hull positions are in BSP units and that the SH frame is the BSP world frame.
- **[The basis is sh-baker's]**: the spec fixes sh-baker's current basis. If sh-baker ever changed its basis or normalization, B5 would have to convert, or the version would change. → The basis is written into the spec and the header, and a change to it is a change of meaning, so it gets a new version.
- **[Hand-written converters]**: bugs in rounding or RGBE encoding. → The exhaustive half test, the tie and subnormal cases, and an RGBE decode test.
- **[File size]**: about 0.9 MB per 128² page before compression. → That is acceptable for a pk3. A version 2 could pack L1 and L2 as luminance.

## Migration Plan

Nothing to migrate: the format, the module and the test program are all new, and nothing calls them until B5. Rolling back means reverting the netradiant PR. sh-baker PR #64 was closed unmerged, so sh-baker needs nothing.
