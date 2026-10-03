# Design

## Context

See `proposal.md` for the motivation, and `specs/sh-baker-material-layers/spec.md` and `specs/sh-baker-q3map2-loader/spec.md` for the requirements. sh-baker code paths are in the `tools/sh-baker` submodule at `86d7239`. q3map2 paths are in this repo under `tools/quake3/q3map2/`.

- **What a bake reads per sample point.** `BakeSHLightMap` (`src/baker.cpp:63`) takes a `std::vector<SurfacePoint>` plus a `RasterConfig` that gives the buffer's width and height. For each point it reads:
  - `material_id`, only as a validity flag (`:142`);
  - `position` and `normal`, for NEE (`:178`) and for the sensor;
  - `tangent`, used by the sensor only to build the sampling frame (`src/sensor.cpp:85–99`).

  The receiver's own material is never read: SH stores incoming light. Nothing in the bake depends on how the points were produced.
- **q3map2's luxel data.** `-light` builds raw lightmaps (`SetupSurfaceLightmaps`, `light.cpp:2964`). `MapRawLightmap` (`:2007`) then fills each raw lightmap with per-luxel origins, normals and clusters (`superOrigins`, `superNormals`, `superClusters`, `q3map2.h:1317–1320`), at `superSample` resolution, with cluster < 0 meaning unmapped. `IlluminateRawLightmap` (`:2031`) computes the light. `StoreSurfaceLightmaps` (`:2052`) then packs the raw lightmaps into output pages, and that packing depends on the lit values: `FindOutLightmaps` approximates a lightmap to vertex light, stamps a solid colour as 1×1, and deduplicates identical lightmaps (`lightmaps_ydnar.cpp:1314`, `:1578`, `:1783`). So the final pages exist only after lighting.
- **sh-baker's rasterizer does not fit q3map2's coordinates.** It snaps vertices to whole texels (`src/rasterizer.cpp:244`), while q3map2 puts lightmap coordinates on luxel centres (`lightmaps_ydnar.cpp:732`). Its single-pixel branch can also write one texel past a non-square page. xatlas, as sh-baker calls it, always makes one atlas (`xatlas.cpp:8289`, `maxResolution` is 0 when `texelsPerUnit` is 0).
- **Material and the layer stack.**
  - `Material` and `Texture` are defined in `src/scene.h`.
  - `Material::layers` is a `MaterialLayers` (`src/material_layers.h`) holding the raw `SH_material_layers` `tinygltf::Value` plus a map from input texture index to source path. That is why `scene.h` includes `tiny_gltf.h`.
  - The layer enums are in `src/layer_composite.h`. Its `TcMod` keeps values only for `SCALE`/`TRANSFORM`, because the compositor freezes the time-varying types at t=0.
  - The loader parses the extension twice (`src/loader.cpp:561`), once to composite and once to keep the verbatim copy. The saver rebuilds the `Value` with remapped indices (`src/saver.cpp:110`).
  - `src/material.h` includes `scene.h`.
- **The extension's schema.** Its only producer is `ioq3-map-exporter` (`src/saver.cpp:375–539` in `../ioq3-custom`). It writes:
  - `surfaceBlend`, `cullMode`, an integer `baseLayer`, and `layers[]`;
  - per layer: `texture.index`, string `blendSrc`/`blendDst`, and `rgbGen` (`type`, plus `func`, `base`, `amplitude`, `phase`, `frequency` for `WAVE`);
  - optionally `animFreq` with `animFrames[]`, and `tcMod[]`, where `ROTATE`'s `value` is a scalar and `TURB`/`STRETCH`'s `value` is `[wave, base, amp, phase, freq]` with wave possibly `"NONE"`.

  Every number is a `double` converted from a `float`. sh-renderer mirrors the enums (`sh-renderer/src/q3_layer.h`) and reads the extension from the baked glTF.
- **The glTF loader's invariants.** It is the only geometry producer today. `ProcessPrimitive` (`src/loader.cpp:148`) requires `POSITION` and `NORMAL`, plus `TEXCOORD_0` unless the primitive is an occluder, which gets (0, 0). It drops triangles that repeat an index (`:339–370`). Tangents come from MikkTSpace (`GenerateTangents`, `:92`, in an anonymous namespace) when texture UVs were read, and from a fallback basis otherwise (`:372–396`).
- **How q3map2 will build against sh-baker.** It will link `libsh_baker.so` and include `src/` headers under `-std=c++20 -fno-exceptions -fno-rtti`. Plan §4.3 puts only `src` and Eigen on its include path. Today `scene.h` also includes `<embree4/rtcore.h>` (`:4`), only for the `RTCScene`/`RTCDevice` handles in `BuildBVH` and `ReleaseBVH` (`:181–184`). The system Embree is built without `EMBREE_API_NAMESPACE`, so those handles are global typedefs (`typedef struct RTCSceneTy* RTCScene;`), which Embree itself repeats in `rtcore_device.h:12` and `rtcore_geometry.h:12`. `baker.h` reaches embree only through `scene.h`. sh-baker logs through glog to stderr, which the editor's build monitor does not show.

## Goals / Non-Goals

**Goals:**
- q3map2 can hand sh-baker valid tracing geometry and its own luxel points, and get one SH result per luxel, without sh-baker rasterizing anything.
- The material model q3map2 will fill in B5 (Quake 3 stage stacks) is sh-baker's own, and no header q3map2 includes reaches tinygltf.
- No behaviour change for any existing caller: the CLI, the tests (apart from the two rewritten pass-through tests), the visualizer and `sh_cvt`.

**Non-Goals:**
- Lightmap pages, page indices or layouts in sh-baker. Multi-page xatlas output. Changes to the rasterizer.
- q3map2 materials from shaders, lights, area lights from emissive shaders, and the sky. B5 adds them to the q3map2 loader TU.
- Shadow rays that honour alpha. NEE's visibility test treats any hit as opaque (`src/light.cpp:265`, `:339`), so fences and leaf cards block direct light completely. The user wants it fixed. It goes in its own sh-baker PR (B2b, added to the plan in task 1.4), because it deliberately changes bake output, while this PR proves its output unchanged.
- Changing the `SH_material_layers` schema or what sh-renderer reads.
- Animating the time-varying tcMods in the bake. The compositor keeps freezing them at t=0.

## Decisions

### q3map2 supplies the sample points; sh-baker does not rasterize them

B5 will collect every mapped luxel of every raw lightmap (cluster ≥ 0) as a `SurfacePoint`:
- the position comes from `superOrigins`, and the normal from `superNormals`;
- the tangent is any unit vector perpendicular to the normal, with `w = +1` (the sensor multiplies the bitangent by `w`, `src/sensor.cpp:94`);
- `material_id` is the surface's material.

B5 passes all of them to `BakeSHLightMap` in one call, as an N×1 buffer, and scatters the results back to the luxels. That gives one BVH and one light tree for the whole map. q3map2 then downsamples, filters and stores them as `-light` does today. B5 therefore runs inside `-light`, in place of `IlluminateRawLightmap`'s math, rather than as a stage on the finished BSP: the pages are final only after `StoreSurfaceLightmaps`, whose packing depends on the lit values.

The result matches stock `-light` luxel for luxel, including the borders and `superSample`, and sh-baker gets no rasterizer, page or dilation work. B2 only has to make the existing API's support for this explicit: a doc comment on `BakeSHLightMap` and a test that bakes points from different surfaces in one call.

**Supersampling** is q3map2's `-super N` (`light.cpp:2439`, ordered grid, at most 8). Raw lightmaps are mapped at N× resolution (`lightmaps_ydnar.cpp:337`), so every super-luxel has its own origin, normal and cluster, and each becomes one point in the bake. `StoreSurfaceLightmaps` averages the mapped super-luxels of each luxel (`lightmaps_ydnar.cpp:2325–2365`). SH coefficients are linear in radiance, so the side data averages the same way. The cost is N² points. Because the average also reduces noise, B5 can lower the per-point sample count. sh-baker's sensor shoots every sample from the point itself (`src/sensor.cpp:87`), so the super-luxels are what provide the spatial anti-aliasing.

What is lost is `-samples N`, the adaptive subsampling that refines only where neighbouring luxels differ (`light_ydnar.cpp:1789`, `:1911`). It lives inside `IlluminateRawLightmap`'s lighting, which B5 replaces. If `-super` turns out too costly, the equivalent is a second pass that bakes extra points only where neighbouring luxels differ, again as caller-supplied points. That is a B5 refinement and needs nothing from sh-baker.

*Alternative: sh-baker rasterizes q3map2's pages (the plan's §4.2 item 1).* That needs a page model, a sub-texel-exact rasterizer that differs from the CLI's, a fix for the edge write, and dilation that guesses at q3map2's borders. It would also bake on the finished BSP, whose pages q3map2 has already deduplicated by lit value. Rejected by the user.

*Alternative: many small calls, one per raw lightmap.* Each call rebuilds the BVH (`src/baker.cpp:95`). Rejected. B3 adds progress and cancel callbacks to the single call.

### Producers guarantee validity; the tracer does not check

Every invariant the tracer, light tree and material sampling rely on is established by the producer that builds the geometry: the glTF loader, as today, and the new q3map2 loader below. The hot paths keep their minimal branching.

### The layer stack is an sh-baker-owned model; glTF is only a serialization

The new owned types follow the maintainer's direction: the loader and saver serialize, and `Material` holds sh-baker data. `material.h` gains:
- the enums `BlendFactor`, `RgbGenType`, `WaveType` (plus `kNone`), `TcModType`, `SurfaceBlend` and `CullMode`, moved from `layer_composite.h` or new;
- `RgbGen`, whose wave function can be absent. The exporter writes `WAVE` without `func` for `noise` and unknown waves (`ioq3-map-exporter/src/saver.cpp:461–465`). Today's loader composites that as `SIN`, which the compositor keeps doing, and the saver omits `func` again so the round trip stays byte-identical. sh-renderer reads a missing `func` as `SIN` too;
- `TcMod`, with `values` for every type, kept in field order `{type, values, wave}` so `layer_composite_test`'s aggregate initializers still compile;
- `MaterialLayer`, holding an optional texture path, frame paths, `anim_freq`, blend factors, `rgbgen` and `tcmods`;
- `MaterialLayers`, holding `surface_blend`, `cull_mode`, `base_layer` and `layers`.

`material.h` also takes `Material` from `scene.h`. That breaks the include cycle, since `material.h` needs `Texture` and `scene.h` needs `Material`. `Texture`, `Texture32F` and `Texture32I` move to a new dependency-free `texture.h`. `scene.h` includes `material.h`, so every existing `#include "scene.h"` still sees all of it. `material_layers.h` is deleted, and `layer_composite.h` includes `material.h` for the enums.

- **Loader.** The extension is deserialized once into `MaterialLayers`, resolving texture indices to paths with `ResolveTexturePath`. The compositor's `CompositeLayer` list is built from that model plus loaded pixels, so compositing reads the same values as today.
- **Saver.** A serializer replaces `RemapLayerTextureIndices`. It writes exactly the exporter's keys and value types (integers for indices and `baseLayer`, `double(float)` for the rest) under the exporter's conditions. It calls `AddOrReuseTexture` in today's order: each layer's texture, then its frames. A texture or frame with no source keeps its slot as -1, as `remap` does today.

tinygltf parses a JSON integer as an integer `Value`, and the old pass-through wrote it back as an integer. The owned model writes every non-index number as a double. Byte identity therefore holds for exporter output, which always writes decimal literals, and the fixture must do the same.

tinygltf objects are `std::map`s, so key order in the JSON is fixed, and byte-identical output reduces to "same keys, same values, same types".

*Alternative: keep the verbatim `Value` behind a type-erased handle.* That would hide tinygltf from the header but keep glTF as the in-memory model, and B5's q3map2 stages could not be expressed without inventing JSON. Rejected.

*Alternative: put the owned types in `material_layers.h`, included by `scene.h`.* This avoids moving `Material` and `Texture`. The maintainer asked for `material.h`, where the BRDF code that consumes a `Material` already lives. Rejected.

*Unknown values.* The verbatim carrier passed unknown keys and names through. The owned model keeps only the schema above. An unknown name logs a warning and takes the default listed in the spec, and an unknown key is dropped. The only producer writes none.

### A new TU for q3map2: `loader_q3map2.{h,cpp}`

The glTF loader TU is built around tinygltf, and it defines the tinygltf and stb implementation macros. q3map2 needs a header that is free of both, and a contract shaped like its draw surfaces. With the owned layer model, `scene.h`'s include closure no longer reaches tinygltf, so the new header can include `scene.h`. It declares:

```cpp
struct Q3Map2Surface {
  std::vector<Eigen::Vector3f> positions;     // world space
  std::vector<Eigen::Vector3f> normals;
  std::vector<Eigen::Vector2f> texture_uvs;   // may be empty for occluders
  std::vector<uint32_t> indices;              // q3map2 winding: front is clockwise
  int material_id = -1;                       // < 0: pure occluder
};

// Appends one Geometry for tracing. Malformed input (see the spec) fails a
// CHECK; degenerate normals and triangles are repaired.
void AddQ3Map2Surface(const Q3Map2Surface& surface, Scene* scene);
```

`AddQ3Map2Surface` first `CHECK`s the spec's malformed-input cases, and each message names the field and value. It then:
1. swaps each triangle's second and third index. q3map2's front faces are clockwise (`PlaneFromPoints`, `tools/quake3/common/qmath.h:197`), and sh-baker scenes are counter-clockwise like glTF, so B5 passes q3map2's indices untouched and the scene keeps one convention;
2. normalizes the normals, replacing each shorter than 1e-6 with the normalized sum of its triangles' unnormalized face normals, or (0, 0, 1) if that sum is also degenerate;
3. fills zero texture UVs for an occluder that has none;
4. drops triangles that repeat an index, as `ProcessPrimitive` does;
5. generates tangents through the shared code;
6. appends the geometry.

The repair in step 2 uses the counter-clockwise triangles from step 1, so a repaired normal faces the same way as q3map2's front face.

q3map2 draw surfaces have no tangents, and those tangents matter for the normal maps of surfaces a bounce ray hits.

*Why CHECK and not an error return.* Every `bspDrawVert_t` carries `xyz`, `st` and `normal` together (`q3map2.h:324`), and B5 builds the surface from q3map2's own index buffers and material table. Count, index and material faults can therefore only come from a bug in B5's conversion. Non-finite values can only come from a corrupt model file. None of them carries a meaning sh-baker could act on. A NaN or infinite UV becomes NaN in the wrap `uv - floor(uv)` (`src/material.cpp:56`), and indexing a texel with it is undefined behaviour. Failing at load, naming the field, beats crashing or baking garbage at the first ray hit. Out-of-range UVs are legitimate tiling and are accepted. The checks run once per vertex at load, not on the hot path. A glog `CHECK` writes to stderr, which the editor's build monitor does not show, so such a bug is debugged from a terminal.

*Why repair zero normals.* Real content produces them. A degenerate patch vertex gets a zero normal (`mesh.cpp:239`), and q3map2 itself repairs bad normals on meta surfaces (`surface_meta.cpp:224`). Aborting a town compile over one degenerate patch would be wrong. Passing the zero through would become NaN at a hit and poison the SH.

*Alternative: extend `loader.cpp`.* That would put q3map2 types next to tinygltf in one TU. Rejected, following the maintainer's direction.

### Solid brush hulls are occluders; draw surfaces are receivers

q3map2's draw surfaces are thin: only the visible faces of brushes, often in pieces. Rays can slip through the cracks and slivers between them, and a luxel can sit on the wrong side of a thin wall. The BSP still holds every brush as planes:
- `EmitBrushes` (`writebsp.cpp`) writes each brush of every entity, structural and detail, with its sides;
- q3map2 has the code to turn planes into closed convex polygons (`BaseWindingForPlane`, `ChopWindingInPlace`, `common/polylib.h`).

Stock `-light` stays leak-free only for structural brushes, because it traces against the BSP tree's solid leaves (`light_trace.cpp:318`). Detail brushes cast only through their thin surfaces there.

B5 therefore gives sh-baker two sets:
- **Receivers:** the draw surfaces, with materials. They are what the engine draws, so they own the luxels and lightmap UVs. Bounce rays shade them at hits.
- **Blockers:** one closed hull per brush that casts under the editor's caster rule, structural and detail. Each is built from the brush's planes with every plane pushed inward by ε, a little more than the sensor's `tnear`. Each is added as a material-less surface. The inward offset means the visible face is always hit first, so shading is unchanged, and no luxel starts inside a hull. A ray that slips through a crack hits the hull behind it and is blocked. Bevel sides are skipped. Surfaces without a brush (patches, `misc_model` props, foliage) stay thin receivers and casters.

B5 also writes the hulls, un-shrunk, into the side file, and renderer_sh's shadow maps (C5) load them from there. The bake's blockers and the renderer's casters are then one set, and C5 loads hulls instead of rebuilding them from the brush lumps. B2 needs no code for any of this: material-less surfaces already block without shading (`src/tracer.cpp:34`, and NEE's visibility test). B2 adds a test that pins this down.

### Embree handles are declared in `scene.h`, not included

`scene.h` replaces `#include <embree4/rtcore.h>` with the two global declarations Embree itself uses:

```cpp
// Opaque Embree handles; translation units that call Embree include
// <embree4/rtcore.h> themselves.
typedef struct RTCDeviceTy* RTCDevice;
typedef struct RTCSceneTy* RTCScene;
```

C++ allows a typedef to be repeated with the same type, so any TU that also includes Embree compiles unchanged. Every `.cpp` that calls `rtc*` and relied on `scene.h` for the include gets its own `#include <embree4/rtcore.h>`. `occlusion.h`, `light.h` and `tracer.h` keep theirs, because they are internal and use Embree types beyond the handles. With this change and the owned layer model, `loader_q3map2.h` and `baker.h` need only `src` and Eigen, as plan §4.3 already says.

*Alternative: put Embree on q3map2's include path.* That works, but it couples q3map2's build to Embree headers for two opaque pointers. Rejected, following the maintainer's direction.

### Scene assembly order is checked

`Light::geometry` and the BVH's user data are raw pointers into `scene.geometries`, and an append can reallocate it. `AddQ3Map2Surface` therefore `CHECK`s that no light in the scene has a non-null `geometry`. That catches the realistic mistake of creating area lights before the last surface. A BVH built too early cannot be detected from the scene, so the header documents the order: all surfaces, then lights, then the BVH.

### Tangent code moves to `tangents.{h,cpp}` unchanged

The MikkTSpace callbacks, `GenerateTangents` and the fallback-basis loop move out of `loader.cpp` into `tangents.cpp` behind one function:

```cpp
// Fills geo->tangents (one per vertex) if empty: MikkTSpace when
// use_texture_uvs, else the fallback basis.
void GenerateTangents(Geometry* geo, bool use_texture_uvs);
```

`ProcessPrimitive` calls it with today's condition, `!geo.texture_uvs.empty() && uv0_data != nullptr`. `AddQ3Map2Surface` calls it with `!surface.texture_uvs.empty()`. The move is verbatim, so the glTF path stays byte-identical, and the CLI comparison checks that.

### Development and delivery

The work is on a branch in `tools/sh-baker`. The baseline is built from `86d7239` into `build/baseline/`, and the change into `build/`. Both are covered by the submodule's `build` ignore rule. FetchContent sources come from the network, or from `../ioq3-custom/sh-baker/deps` through `FETCHCONTENT_SOURCE_DIR_XATLAS` / `_MIKKTSPACE`. Comparing baseline and change shows that the CLI outputs and the existing tests are unchanged. Every sample scene carries `TANGENT`, which would leave the moved tangent code untested, so two fixtures are added under `data/`, each generated by a `make_fixture.py` beside it:
- `data/layers`, with `SH_material_layers` covering every field kind;
- `data/notangent`, the box without `TANGENT`, plus an offset material-less copy without `TANGENT` and `TEXCOORD_0`. It exercises both MikkTSpace and the fallback, and the saver writes both into the output glTF. The user is asked before the branch is pushed or the PR opened. The plan and `CLAUDE.md` are corrected first, in task 1.4. After the merge, this repo bumps the submodule and ticks B2.

## Risks / Trade-offs

- [B5 moves inside `-light` instead of running as a separate stage on the finished BSP, as plan §4.3 placed it.] → This is required by where the luxel points exist, and it matches `CLAUDE.md` B5 ("replaces `-light`'s lighting math"). Task 1.4 rewrites §4.3. `StoreSurfaceLightmaps`' lit-value packing (approximate to vertex light, solid stamps, deduplication) must carry the SH side data along or be disabled when baking SH. That is B5's concern, recorded in the plan.
- [One call with every luxel of the map is a large point list, and `BakeSHLightMap` reports progress only to stdout.] → Memory is about one `SurfacePoint` plus one `SHCoeffs` per luxel, which is fine for town-scale maps. B3 adds progress and cancel callbacks.
- [The sensor's `tnear` is 0.005 (`src/sensor.cpp:85`), sized for metres, while q3map2 works in Quake units.] → B5 converts units or scales the scene. Recorded for B5, and unchanged here.
- [xatlas or the bake may not be deterministic run to run, which would make "byte-identical" untestable.] → The baseline CLI runs twice first. If the two runs differ, stop and ask the user how to compare rather than weaken the check silently.
- [The Release build uses `-O3 -flto=auto -march=native`, so moving code between translation units can change inlining and FMA contraction, and with them float bits, without any change in meaning.] → Any byte difference between the baseline and the change stops the work. It is diagnosed (numeric size of the difference, which task introduced it, whether codegen alone explains it) and reported to the user with the evidence. It is never accepted silently.
- [The owned model drops unknown keys and normalizes unknown names, where the verbatim carrier passed them through.] → The exporter, the only producer, writes neither. The layered fixture covers every field kind, and the byte-identical CLI run on it proves the serialization is exact.
- [Moving `Material` and `Texture` touches many includes.] → `scene.h` re-exports both, so most files compile unchanged. The move is mechanical, and the pre-existing tests and the byte-identical CLI runs check it.
- [An Embree built with `EMBREE_API_NAMESPACE` would put the handles in a namespace, and they would no longer match the global declarations.] → The mismatch fails to compile in `scene.cpp`, loudly, rather than misbehaving. The supported Embree (the system package and the plan's releases) has no namespace.
- [A `CHECK` failure inside `-light` kills the compile, and its message reaches stderr, not the editor's build monitor.] → Only a B5 bug or a corrupt model can trigger one. B5 development runs q3map2 from a terminal.
- [Brushes with non-solid contents may be missing from the BSP's brush lumps, so some casters could have no hull.] → This is already in the plan's risk table. B5 checks which casters survive. Those without a hull fall back to their thin surfaces.
- [MikkTSpace writes one tangent per vertex index. A vertex that q3map2 shares across a UV seam gets whichever face wrote it last, the same caveat as the glTF loader.] → q3map2 drawverts are per surface and are already split where `st` differs. Accepted, and noted in the header comment.
- [The PR is reviewed upstream and may come back with changes that alter the API.] → The specs state behaviour, not names. If the maintainer renames things, only `design.md` and the tasks need updating before archive.

## Migration Plan

Nothing to migrate for users of the CLI or of the glTF files. The library API break is limited to `MaterialLayers`' fields (see the proposal). Rollback means reverting the PR in sh-baker and leaving this repo's submodule pointer at `86d7239`.
