# Design: area lights and emissive proxies (A11)

## Context

See `proposal.md` for the motivation and `specs/` for the required behaviour. Plan revision 7 (§rev7, §2.2 step 6, §Units "Area", "Proxy" and "Emissive surfaces", §4.3 "Gaps the stage reports", §4.5) is the source of the model. Line numbers are from `master` on the fork (`061c548c`, "Land q3map2 -shbake (B5)").

**Entity module**
- Light classes are routed to `New_Light` at `plugins/entity/entity.cpp:56-62`. `light_spot` and `light_sun` are PBR lights only under `eGameTypePBR`. Any other classname, `light_area` included, becomes a generic entity.
- `PBRLight` (`plugins/entity/light.cpp:871-1006`) holds the type, intensity, radius, cones, angles, direction and the shadow flag.
  - `anglesDirection()` (`:909-917`) returns only the forward vector and ignores roll.
  - `castsShadows()` (`:995-997`) is `m_shadows && m_type != ePoint`.
  - The cone cosines clamp both half-angles to 89.9° and `cone_inner` to `cone` (`:1001`, `:1004`).
- `Light::construct` (`light.cpp:1397-1411`):
  - it picks the type from the classname (`:1399-1401`), with `ePoint` for anything that isn't a spot or a sun;
  - it registers `_shadows` for every non-point type (`:1407-1409`).
- `pbrParams()` (`light.cpp:1877-1887`) fills `RendererLightParams`. `pbrAABB()` / `pbrTestAABB()` (`:1890-1905`) give a radius cube, and a world-sized box for the sun.
- The entity's own bounds are a fixed 12-unit box (`light.cpp:1552`, `:1577`), used for culling and selection (`:1657-1664`, `:504-518`). PBR gizmos are drawn only when selected (`light.cpp:1676-1693`), by `RenderPBRLightShape` (`:1046-1135`). Its spot basis ignores roll (`:1066-1109`).
- `RendererLightParams` (`include/irender.h:66-90`) has `eDoom3, ePoint, eSpot, eSun`, plus origin, direction, intensity, radius, the cone cosines and `castsShadows`.

**Per-face culling**
- `Face_addLight` (`radiant/brush.h:3101-3109`) adds a light to a face when the light's centre lies strictly in front of the face plane. `plane3_test_point` is `dot(p,n)+d <= 0` (`libs/math/frustum.h:577-579`).
- Patches and models test bounds only (`radiant/patch.h:1673-1675`, `plugins/model/model.cpp:401-405`, `:547-549`).

**Preview light pass**
- `GLSLPBRProgram::setLightParams` (`radiant/renderstate.cpp:675-701`) uploads the light in object space. It maps the type at `:696` (sun 3, spot 2, otherwise 1).
- Sampler units 0-4 hold the material and units 5-6 the shadow atlases (`renderstate.cpp:590-594`, `:608-609`). The state machine never sets `m_texture5..7` (`:1036-1043`, `:1064-1066`). `c_shadowTextureUnits = 7` (`:509`).
- `pbr_fp.glsl`:
  - `u_light_type` is at `:40`;
  - roughness is the perceptual roughness r, clamped to [0.05, 1] (`:200`), and GGX uses alpha = r² (`:239`);
  - point and spot share a falloff with a hard cutoff (`:215-231`); the spot falloff is `smoothstep(cosOuter, cosInner, cosAngle)` (`:224`);
  - the point and spot diffuse is (1 − F)(1 − metallic) × baseColor / π × Disney (`:247-248`);
  - the radiance line multiplies by `NdotL` (`:252`);
  - `shadowTerm` mode 1 is the spot, with penumbra 1 (`:124-148`);
  - fragments outside the shadow map read as lit (`:88-90`).
- The shader files have no `#version` line, so they compile as GLSL 1.10. The context is GL 2.0 compatibility (`libs/gtkutil/glwidget.cpp:72`). Nothing loads a texture from `setup/data/tools/gl/` today. GLSL is read through `FileInputStream` (`renderstate.cpp:161-190`).
- The atlases are bound only in `ShaderCache_updateShadows` (`renderstate.cpp:2248-2254`), which runs only with the shadow preference on (`radiant/camwindow.cpp:2103-2132`).

**Spot shadows**
- `Shadow_generate` collects casting suns and `eSpot` lights only (`renderstate.cpp:2142-2158`), sorted by distance from the camera. 16 tiles of 512 are available (`:499-509`). The over-subscription warning is at `:2182-2187`.
- `Shadow_generateSpot` (`:2057-2081`) uses only `direction`, `origin`, `cosOuter` (as the half-angle, clamped below π/2 − 0.02, `:2064`) and `radius`.

**Unlit emitters**
- `m_previewLit` excludes unlightmapped, blended and `nolightmap` shaders (`plugins/shaders/shaders.cpp:1771-1772`). They take the fixed-function branch (`renderstate.cpp:3191-3229`): the 8-bit sRGB texel times a 0.8 headlight, written as if linear into the HDR target, and opaque unless `qer_trans`.
- The emissive stage is the first additive map stage that is not a lightmap, `$whiteimage` or environment stage (`shaders.cpp:1747-1755`). `adds()` accepts `GL_ONE GL_ONE` and `GL_SRC_ALPHA GL_ONE` (`:1351-1353`). Without `rgbGen const` its colour is white (pbr-material-format, "Emissive comes from the first additive stage").
- For an all-additive shader, the base colour falls back to the additive stage itself (`:1735-1742`).
- Emissive textures load only when `loadsLightingTextures()` (`:1056-1058`) is true. That needs `m_pbr`, `m_previewLit` or an alpha test. Without one, `getEmissive()` returns `_white` (`:1148`), so no accessor says whether an emissive stage exists.
- `pbr_base_fp.glsl` already computes `srgbToLinear(emissive) × factor × strength + ambient × base × occlusion`.

**q3map2 and the stock check**
- `CreateEntityLights` (`tools/quake3/q3map2/light.cpp:319`) takes every `light*` classname (`:327-335`), reads `_light`/`light` with a default of 300 (`:439-443`), and is called only on the non-`-shbake` path (`:2020`). `-keeplights` keeps every `light*` entity (`writebsp.cpp:242`).
- q3map2 reads a boolean key as "the value starts with `1`" (`entity_t::read_keyvalue_`, `bspfile_abstract.cpp:566-574`; `boolForKey`, `q3map2.h:1047`).
- B5's `-shbake` already leaves out `light_area` and `_proxy` lights and warns about unattached emitters (q3map2-shbake, "Attached emitter lights skipped"). `ShIsAttachedEmitterLight` (`light_sh_model.cpp:261-271`) honours `_proxy` on `light`, `light_spot` and `light_sun` only. `ShAreaRectAttached` (`:336-388`) is the attachment test: the surface normal within 1° of the rectangle's forward vector, every surface vertex within 1 unit of the rectangle's plane, and some triangle overlapping the rectangle by more than 1e-3 square units (`light_sh_model.h:236-239`). `ShAreaRectFromKeys` (`light_sh_model.cpp:273-294`) already uses this change's `AngleVectors` basis and key defaults.
- Stock ioquake3 prints `<classname> doesn't have a spawn function` for each unknown class (`code/game/g_spawn.c:281`). The allowlist has the `light_spot` and `light_sun` lines (`tools/stockcheck/allowlist.txt:8-11`).
- The stock check compiles with `-light -shbake` at its defaults, 128 samples and `-super 1` (`tools/stockcheck/stockcheck.py:32`), so it needs q3map2 built with `SH_BAKER=1` against the submodule's Release `libsh_baker.so` (`tools/stockcheck/README.md:27-38`). A plain `make` builds q3map2 with `SH_BAKER=0` and, through `light_sh.stamp` (`Makefile:599`, `:632`, `:636-648`), relinks `install/q3map2`, the stock check's default `--q3map2` (`stockcheck.py:54`), against `light_sh_stub.o`. The overlay's "Town" builds are the draft (128), pre-release (512, `-super 2`) and final (2048, `-super 2`) presets; the stock check matches "Town (draft)".

**The benchmark sign and fireplace**
- Shader `textures/bench/neon` (`content/benchmark/scripts/bench.shader:94-115`): opaque, lightmapped, `qer_pbr_emissiveStrength 8`, emissive stage `neon_e` with `rgbGen const ( 1 0.35 0.25 )`.
- Brush 24 (`content/benchmark/maps/bench.map:223-231`) is the neon sign, a 192 × 8 × 32 box. Its front face lies on plane y = 248 with normal −Y (`:228`), and its centre is (0, 248, 120). Its back face at y = 256 is flush against the brick wall and culled; its two ends, top and bottom are neon too. A one-sided `light_area` flush on the front face can't light the wall the sign hangs on (y = 256), which lies behind its plane, so the sign is not this change's demo (decision 15).
- The north interior (x −496..496, y 272..752, z 0..176) holds the metal pillar, brush 23 (x −200..−168, y 400..432), and the ceiling spot. Its north wall's inner face, brush 20, is at y = 752. This change adds the fireplace there (decision 15).

**Commands**
- Selected faces are in `g_SelectedFaceInstances` (`radiant/brush.h:2561`).
- `Face::getWinding()` (`brush.h:1292`) already holds per-vertex texture coordinates in repeats (`EmitTextureCoordinates`, `brush.h:1279`; `brush_primit.cpp:274`).
- Images load on the CPU through `GlobalTexturesCache().loadImage(name)` (`include/itextures.h:67`) as RGBA8, row 0 at the top (`include/iimage.h:28-45`).
- `Entity_createFromSelection` pops the legacy light-intensity dialog for `light` (`radiant/entity.cpp:474-483`), so the commands create entities directly, as `entity.cpp:107-160` does.
- The patch tessellation is private (`radiant/patch.h:424`).
- The only test targets are q3map2's (`Makefile:661`, `:672`).

## Goals / Non-Goals

**Goals:**
- One area-light model shared by the preview, `renderer_sh` (C7) and the docs, pinned tightly enough that C7 can match the preview within §3.4's tolerance.
- The emitter and its light stay consistent: the commands derive the light from the shader, and Re-derive repeats the derivation.
- Nothing changes for stock targets apart from one allowlisted spawn line per `light_area`.

**Non-Goals:**
- The bake's handling of attached lights. It landed with B5 (q3map2-shbake, "Attached emitter lights skipped").
- Texture modulation, `animMap` and moving emitters.
- Drag handles for `width` and `height`, and re-aiming PBR lights with the rotate tool. The rotate tool is already a no-op for PBR spots (`light.cpp:1756-1758`, `:1797-1839`); this change keeps that gap.

## Decisions

### 1. A point entity with copied geometry

`light_area` is a point entity whose rectangle is copied from the face.

Alternatives considered:
- **A brush entity owning the emitting face.** Rejected (plan §rev7). q3map2 compiles it into an inline model, and stock ioquake3 frees the entity and its model with it, so the glowing quad would vanish on vanilla.
- **A reference from the light to a face.** Rejected. Quake 3 faces have no names, `target` links only entities, and brush edits renumber faces.
- **A point entity (chosen).** It survives `-keeplights`, costs stock one allowlisted line, and the face stays in worldspawn, drawn and lightmapped as before. The price is that the light and the face can drift apart after edits. Re-derive and q3map2-shbake's unattached-emitter warning cover that.

### 2. One-sided

The rectangle emits only into the half-space its normal faces.

Alternatives considered:
- **Two-sided, matching sh-baker** (`tools/sh-baker/src/light.h:220`). Rejected. Most emitters are faces of solid brushes, whose back is inside the brush or against a wall, so two-sided light would leak through walls in the preview. Two-sided billboards get a point proxy with k = 2 instead.
- **A per-light sidedness key.** Rejected for now. It would add a key that no current content needs.

The difference from the bake is documented (plan §rev7 "Sidedness").

### 3. `intensity` is radiance

`intensity` on `light_area` is the radiance L, not flux or radiant intensity.

Alternatives considered:
- **Flux, like `light`.** Rejected. Resizing the rectangle would then change its brightness per unit area, so it would no longer match the surface it copies.
- **Radiant intensity.** Rejected for the same reason.

Radiance is the quantity the derivation produces (texel × factor × strength), so the light matches its emitter at any size. The README gives the bridge to the point model: flux = πLA, and on the normal at a distance d large against the rectangle, E ≈ LA/d².

Defaults are `width 64`, `height 64` and `intensity 1`. Close to the rectangle E falls below LA/d²: at 64 units on the normal the exact form factor gives E = π × L × FF = 0.752 (FF = 0.2395), against LA/d² = 1. The default `light` gives 50000 / (4π × 64²) = 0.971 there, so the two defaults are of the same order, not equal.

### 4. Basis and `angles`

The basis follows Quake `AngleVectors` (`libs/mathlib/mathlib.c:344-373`) over `angles` = pitch yaw roll, with positive pitch looking down:
- forward = (cp·cy, cp·sy, −sp);
- right = (−sr·sp·cy + cr·sy, −sr·sp·sy − cr·cy, −sr·cp);
- up = (cr·sp·cy + sr·sy, cr·sp·sy − sr·cy, cr·cp).

So right = forward × up. `width` lies along right and `height` along up. The `angle` key works as for spots: yaw only, with −1 meaning up and −2 meaning down.

`PBRLight` gains `anglesBasis()`. Spots and suns keep using only forward, so their behaviour is unchanged. The helper library (decision 10) holds the same formulas and the inverse with roll, and its tests pin both. q3map2's attachment test already uses these formulas (`light_sh_model.cpp:290-292`); C7 copies them from the README.

`target` is ignored for area lights. `pbrUpdateDirection` already uses `target` only for `eSpot` (`light.cpp:1865-1874`).

### 5. Renderer parameters and culling

`RendererLightParams` gains `Vector3 right, up`: world-space half extents, not normalised. For `eArea`:
- `origin` is the centre and `direction` the normal;
- `radius` is the effective radius, max(`radius`, half-diagonal), where the half-diagonal is ½√(`width`² + `height`²);
- `cosOuter` is `cos(min(_frustum, π/2 − 0.02))` (the shadow path's clamp, so 180° or more does not wrap), used only by the shadow path;
- `castsShadows` is `_frustum > 0`.

**Radius.** The cutoff is measured from the centre, for culling and shading alike, so `pbrAABB()` stays the cube of the effective radius. Clamping it up to the half-diagonal means the cutoff sphere always reaches the rectangle's corners; a smaller `radius` is clamped silently, as non-positive values fall back silently. "Create area light from selected face" writes `radius` = max(256, 2 × half-diagonal rounded up to a multiple of 8), so a created light lights at least as far again as its own extent. Measuring from the closest point of the rectangle was rejected: it needs a box-to-rectangle distance in culling and a per-fragment closest-point test, and with the created radius at twice the half-diagonal the cutoff never falls next to the emitter.

**Bounds test.** `pbrTestAABB` also rejects a box that lies wholly behind the plane. The test is on the box's support point toward the normal.

**Per-face test.** For `eArea`, `Face_addLight` admits the face when any of the four corners lies strictly in front of the face plane. With the centre-only test, a face whose plane cuts the rectangle would lose the light. The emitter's own coplanar face has all four corners on its plane, so it stays unlit by its own light. Its back face, on the brush's far side, has all four corners behind it, so it is unlit too. Patches and models keep the bounds test.

### 6. Gizmo and bounds

The rectangle outline and a normal arrow (32 units, or the shorter side if that is less) are drawn whether or not the light is selected, in both the camera and the 2D views, because the rectangle is the light's shape. The effective-radius sphere and, with `_frustum > 0`, the frustum pyramid out to the effective radius (max(`radius`, half-diagonal), decision 5) are drawn only when selected, as for spots, so the gizmo shows the actual cutoff.

**Spot gizmo.** `RenderPBRLightShape` draws a spot's rim at distance `radius` along the axis with rim radius `radius` × tan(min(`cone`, 89.9°)) (`light.cpp:1072`). For a hemisphere spot proxy (decision 11, `cone 90`) that is about 573 × `radius`, a huge fan or disc. The spot gizmo instead draws the cone to the radius sphere: the rim centred at `radius` × cos θ along the axis with rim radius `radius` × sin θ, θ = min(`cone`, 89.9°). Every spot's gizmo then stays within its cutoff sphere, which is where the light actually reaches, and a hemisphere spot draws a disc of the radius through its origin. Narrow spots look almost the same as before.

For `light_area`, the entity's local bounds become the union of the 12-unit marker box and the rectangle, so the rectangle is not culled while its centre is off-screen. `m_boundsChanged()` fires when `width`, `height`, `angles` or `angle` change. Clicking still selects through the centre marker (`light_testselect`), which is unchanged. Open question 5 covers selecting by the rectangle.

### 7. LTC tables: one file, one texture unit

**Source.** The tables are upstream's GGX fit from `github.com/selfshadow/ltc_code`: the method of Heitz, Dupuy, Hill and Neubelt (2016), in the 2017 revision of that repository, which adds the magnitude and Fresnel terms (Hill and Heitz 2017) and the sphere-horizon term. The data file is `fit/results/ltc.js` (arrays `g_ltc_1` and `g_ltc_2`, 64 × 64 entries of 4 floats each), written by `writeJS` in `fit/export.h` from `packTab` in `fit/fitLTC.cpp`. The shading convention is that of `webgl/shaders/ltc/ltc_quad.fs`. Task 2.1 records the upstream commit hash in `tools/ltc/README.md`; it is not fixed here. `fit/results/ltc.js`, the `LICENSE` and the converter script are vendored under `tools/ltc/`.

**Layout.** The converter writes `setup/data/tools/gl/ltc_ggx.bin`:
- a 16-byte header: `LTC1`, then width 64, height 64 and table count 2, each a little-endian `uint32`;
- then two 64 × 64 RGBA little-endian `float32` tables, each value the `float32` nearest to upstream's printed decimal. Entry i = a + 64·t holds roughness index a (u, the fast axis) and angle index t (v), as upstream's `tab[a + t*N]`.
- Table 1 holds the inverse matrix M⁻¹, normalised by its middle element: (M⁻¹₀₀, M⁻¹₀₂, M⁻¹₂₀, M⁻¹₂₂) in glm's column-major indexing (`packTab`). In GLSL, M⁻¹ = `mat3( vec3(t1.x, 0, t1.y), vec3(0, 1, 0), vec3(t1.z, 0, t1.w) )` (`ltc_quad.fs:439-443`).
- Table 2 holds (magnitude, Fresnel, 0, sphere-horizon term). The area pass clips to the horizon and does not read the sphere term; it stays so the file is upstream's data unchanged.

**Indexing.** u is the perceptual roughness r (upstream fits at roughness a/63 with alpha = roughness², `fitLTC.cpp:197-199`, the same alpha = r² as the point pass), and v = √(1 − N·V) with N·V clamped to [0, 1] (`fitLTC.cpp:191-194`). Both are scaled by 63/64 and offset by 0.5/64, so 0 and 1 land on texel centres (`ltc_quad.fs:32-34`, `:433-434`). r is the pass's clamped roughness, so the table is read on [0.05, 1].

The gamepack README (`setup/data/gamepacks/pbr/README.md`) records the file's SHA-256, and C7 (`renderer_sh`) reads the same binary file unchanged.

**Texture.** The editor uploads both tables as one 64 × 128 `GL_RGBA32F` texture on unit 7, with linear filtering and clamp to edge: table 1 in rows 0-63 and table 2 in rows 64-127. It samples table 1 at (u, v/2) and table 2 at (u, ½ + v/2), which keeps the bilinear footprint inside each table. It is created and deleted with the PBR programs (`OpenGLShaderCache::realise`/`unrealise`, `renderstate.cpp:1585-1622`). A new `ShaderCache_bindLightingTables()` binds it once per frame from `Cam_Draw` in PBR lighting mode, whatever the shadow preference. Its sampler uniform is set before `GLSLProgram_validate`.

**Licence notice.** The licence's binary-form clause asks for the copyright notice, conditions and disclaimer to be reproduced in the documentation shipped with a binary distribution. `ltc_ggx.bin` installs through `setup/data/tools/*` (`Makefile:1460`), but `tools/ltc/LICENSE` does not install, and neither does the gamepack README (`install-gamepack.sh` copies only the pack's `games/*.game` files and `*.game` directories, and `setup/data/gamepacks/pbr/README.md:31` lists it as not installed). So the notice goes next to the binary: `setup/data/tools/gl/ltc_ggx.LICENSE.txt`, a byte-for-byte copy of `tools/ltc/LICENSE` followed by the paper citation the licence asks for, which the same `setup/data/tools/*` copy installs as `gl/ltc_ggx.LICENSE.txt` beside `gl/ltc_ggx.bin`. The gamepack README names the licence, gives the citation and points to that file; it is the developer documentation, not the shipped notice.

**Fallback.** The texture needs `GL_ARB_texture_float` (or GL 3.0) and 8 texture image units. Without them, or if the file is missing or malformed, area lights shade diffuse only, and one warning is printed. Diffuse needs no table, because the form factor is analytic.

Alternatives considered:
- **GLSL arrays of constants.** Rejected. 32768 floats exceeds GL 2.0 uniform and constant limits, and `renderer_sh` could not share the file.
- **Two textures.** Rejected. That needs nine units.
- **RGBA16F.** Rejected. It needs the same extension for no gain at 128 KiB.
- **The original 2016 tables (matrix and magnitude only).** Rejected. They lack the Fresnel term, so the specular could not be weighted by F0.

### 8. Area shading in `pbr_fp.glsl`

`u_light_type 4` gets its own branch. Its uniforms are `u_light_half_right` and `u_light_half_up` (hr = right × `width`/2 and hu = up × `height`/2), transformed to object space with `matrix4_transformed_direction` and not normalised.

For a fragment at P with normal N and view V, the outgoing radiance is zero when dot(P − c, n) ≤ 0 (behind the light) or |P − c| > the effective radius. Otherwise it is

`Lo = L × _color × ( (1 − metallic) × baseColor × FF + S × (F0 × t2.x + (1 − F0) × t2.y) ) × shadow`

written as the point and spot passes write theirs. Here:
- **F0** = mix(0.04, baseColor, metallic), as in the other passes.
- **The polygon** is the four corners in upstream's order p0 = c − hr − hu, p1 = c + hr − hu, p2 = c + hr + hu, p3 = c − hr + hu (`InitRectPoints`, `ltc_quad.fs:376-385`, with dirx = right and diry = up), which emits along −(hr × hu) = forward.
- **The frame** is upstream's: T1 = normalize(V − N(N·V)), T2 = N × T1, and each corner is taken to (T1, T2, N) coordinates relative to P (`LTC_Evaluate`, `ltc_quad.fs:274-290`).
- **FF** is the clipped, normalised form factor: the cosine-weighted solid angle of the rectangle above the fragment's horizon, divided by π, so FF ∈ [0, 1]. A receiver facing an infinite emitter gets FF = 1, and its irradiance is E = π × L × FF. It is computed with the identity matrix: ClipQuadToHorizon, then the sum over the clipped polygon's edges of upstream's `IntegrateEdge`, whose rational fit of θ/sin θ already includes the 1/(2π), then clamped at 0 (`ltc_quad.fs:143-160`, `:162-271`, `:339-352`).
- **S** is the same integral with the corners first transformed by M⁻¹ from table 1, at (r, √(1 − N·V)).
- **t2** is table 2 at the same coordinates.

The diffuse term has no 1/π, because the form factor already divides by π, and no (1 − F) weight, because there is no single half-vector over an area light. This is what upstream does (`ltc_quad.fs:445-451`), so the area pass uses Lambert diffuse instead of the point pass's Disney diffuse with (1 − F). The spec states it as an exception. It is what C7 implements.

The clip uses constant indices only. The transposed basis is built by hand, so the files stay valid GLSL 1.10. No `#version` line is added.

### 9. `_frustum` on the spot shadow path

`Shadow_generate` pushes `eArea` lights with `castsShadows` into the spot list, so they share the 16 tiles and the nearest-to-camera assignment. `Shadow_generateSpot` already derives the half-angle from `cosOuter`, so nothing else changes there:
- field of view 2 × `_frustum`, clamped below 180° − 2.3° (π/2 − 0.02 per half);
- near plane 4;
- far plane `radius` (the effective radius).

The square frustum is aligned with the rectangle's right and up axes, roll included, as the gizmo's frustum pyramid is (`matrix4_light_view( direction, origin, up )` in `radiant/cascade.cpp`); a spot keeps the world-reference roll, since its cone is round. The near plane keeps the emitter's own face out of its map. The shader samples with penumbra 1 and an NdotL toward the rectangle's centre. Receivers beside the frustum (outside the map in x or y) read as shadowed for an area light, while spots keep reading them as lit (`computeShadow` in `pbr_fp.glsl`). The first version read them as lit, as spots do; with a recessed emitter that left a lit strip between the lintel's shadow and the wall, so the shadow looked detached (2026-11-20, the user chose this cheap fix over a five-face hemicube). The cost is that a casting area light gives no light past `_frustum`: the fireplace's firebox sides, at 90° to its axis, go dark. Receivers nearer than the near plane stay lit. So that the light doesn't stop at a hard square edge, an area light's shadow term is multiplied by 1 − smoothstep(`_frustum` − 10°, `_frustum`, θ) on each axis of the frustum, θ being the receiver's angle off the axis in that plane, and the two factors multiplied (user request, 2026-11-20). The spot path's bias, 0.001 × slope clamped at 0.003 in normalised depth, grows with the square of the distance in a perspective map (0.003 is about 77 units at 320 with near 4), so the fireplace's pillar shadow started far behind the pillar. An area light's bias is paid in map units instead, as the sun's is: no depth bias, the receiver moved one tile texel at its distance (t = 2·d·tan(half-angle)/512) times (1 − N·L) along its normal and times clamp(slope, 1, 3) toward the light. Spots keep their bias. The warning text changes from "spot lights" to "spot and area lights".

`_shadows` is not registered for `light_area`, so `_frustum` is the only switch.

### 10. Pure helpers in a header-only library with tests

`libs/emissivelight.h` holds code with no editor dependency:
- the rectangle test, which removes collinear vertices and then requires 4 corners at right angles within 0.5° and opposite edges equal within 0.1 unit;
- `AngleVectors` and its inverse with roll;
- sRGB decode and the linear mean over a UV polygon of a wrapped RGBA8 image;
- polygon area, and the clip of a planar polygon to a rectangle with its overlap area;
- the attachment test of decision 11 (per triangle, as `ShAreaRectAttached`);
- the radiance split, the proxy flux and the created area light's radius.

`libs/emissivelight_test.cpp` builds with `make test-emissive-light`, like the q3map2 test targets.

**Mean over the footprint.** For an affine texture mapping, the mean over a planar region equals the mean over its UV polygon, and a clipped region's UVs follow from the face's affine map. The polygon is fan-triangulated. Each triangle is sampled on a barycentric grid at at least two samples per covered texel per axis, with nearest-texel lookup and wrap, and the samples are weighted by area. The value per sample is the sRGB-decoded RGB. For a `GL_SRC_ALPHA GL_ONE` emissive stage, it is multiplied by the texel's alpha, as the engine blends it. A fixture pins the expected result.

`clampMap` is treated as wrap; open question 3 covers it.

### 11. The commands

**Placement.** The commands are registered in `Entity_Construct` (`entity.cpp:731-752`) as `EntityAreaLightFromFace`, `EntityProxyLightFromSelection` and `EntityRederiveLight`. They appear in the Entity menu only when the game's `entities` key is `pbr`. Each runs inside one `UndoableCommand`. Each creates or edits entities directly through `GlobalEntityCreator().createEntity`, never through `Entity_createFromSelection`.

**Shader data.** The shader module gains these accessors:
- `IShader::hasEmissive()`: an emissive stage exists and its colour (`rgbGen const`, white without one) is not black;
- `getEmissiveName()`;
- `isAllAdditive()`: every map stage `adds()`, ignoring rend2 stages;
- `getEmissiveSrcAlpha()`: the stage's source factor is `GL_SRC_ALPHA`.

The commands load the emissive image on the CPU with `GlobalTexturesCache().loadImage`, so they work outside lighting mode (`getEmissive()` exists only in lighting mode, `shaders.cpp:1130-1149`).

**Emitter gathering.** Create proxy and Re-derive on a proxy gather the same set: the selected brush faces, every face of each wholly selected brush, and the selected patches, keeping those whose shader has an emissive stage.

**Create area light.**
- Precondition: exactly one selected face. It must pass the rectangle test, and its shader must have an emissive stage.
- The rectangle is chosen as follows:
  - forward is the face normal;
  - width runs along the edge more nearly perpendicular to world Z, or the longer edge on a horizontal face;
  - up = right × forward, and if up points downward both right and up are flipped;
  - origin is the rectangle's centre, exactly on the face plane.
- `angles` is solved from (forward, right) and written with `%g` at six significant digits. Re-evaluating it gives the basis back within 0.01°.
- `radius` = max(256, 8 × ⌈2 × half-diagonal / 8⌉) (decision 5). For the fireplace's 96 × 48 fire face, 2 × 53.7 rounds to 112, so `radius` is 256 (the benchmark then raises it by hand, decision 15).
- The command writes `origin`, `angles`, `width`, `height`, `radius`, `intensity` and `_color` and selects the new entity. `_frustum` is left at its default.

**Create proxy light.**
- Input: the emitter gathering above. Patches use a new public `Patch::getTesselation()` accessor. If no surface remains, the command refuses.
- Flux is Φ = π Σ k_f L̄_f A_f, with k = 2 for a surface whose shader `isAllAdditive()` and 1 otherwise.
- The area-weighted mean normal is n̄ = Σ A_f n_f. Origin is the area-weighted centroid, moved 8 units along n̄, or left at the centroid when |n̄| is less than 0.1 of the total area.
- **Kind.** When every surface has k = 1 and |n̄| is at least 0.1 of the total area, the proxy is a `light_spot` with `cone 90`, `cone_inner 89`, `_shadows 0` and `angles` = the pitch and yaw of n̄ (roll 0). Otherwise it is a `light`. The spot model clamps both half-angles to 89.9° (`light.cpp:1001`, `:1004`), so `cone 90` emits into the hemisphere with radiant intensity Φ / (2π(1 − cos 89.9°)) ≈ Φ / 2π. `cone_inner` equal to `cone` would make the falloff `smoothstep(e, e, x)`, which GLSL leaves undefined, so `cone_inner 89` keeps it flat to 89° with a 0.9° rim. `_shadows 0`, because a frustum of nearly 180° gives an unusable shadow map, and point proxies never cast either. Its gizmo is the bounded spot gizmo of decision 6.
- It writes `_proxy 1`, `intensity` = the largest component of Φ, and `_color` = Φ divided by that component. `radius` stays at its default.

Why a spot for opaque emitters: a one-sided Lambertian face of radiance L and area A emits Φ = πLA, and on its normal at a distance d large against it gives E = LA/d², as the stock lightmap does. The same flux from an isotropic point gives Φ/(4πd²) = LA/(4d²), 4× too dim in front and wrongly lighting the space behind. The hemisphere spot gives LA/(2d²), 2× too dim on the axis, and nothing behind. A two-sided billboard (k = 2) emits Φ = 2πLA into both hemispheres; a point light gives 2πLA/(4πd²) = LA/(2d²) on either side, against the face's LA/d². Both residuals are listed under Risks and in the README.

**`_proxy` by class.** q3map2's light stages honour `_proxy` on `light`, `light_spot` and `light_sun` (decision 13, `ShIsAttachedEmitterLight`). The `.ent` offers the key on `light` and `light_spot` only, the two classes Create proxy makes; a sun never stands in for an emitter, and the key on a sun is honoured only so that the placeholder `-light` and `-shbake` agree. The editor reads `_proxy` only on `light` and `light_spot`, and only for Re-derive; it ignores the key on `light_sun`, and Re-derive refuses a sun, because a proxy flux written into a sun's irradiance `intensity` would be the wrong unit. The preview, and later `renderer_sh`, render a proxy of any class, a sun included, as the same light without the key.

**Re-derive.**
- For a selected `light_area`, the emitters are found with q3map2-shbake's attachment test ("Attached emitter lights skipped", `ShAreaRectAttached`), applied to each brush face in the map: its shader has an emissive stage, its outward normal is within 1° of the light's forward vector, all its vertices lie within 1 unit of the rectangle's plane, and some triangle of its fan triangulation, projected on that plane and clipped to the rectangle, covers more than 1e-3 square units. The test is per triangle, as in `ShAreaRectAttached`, not on the whole polygon's overlap, which would attach a superset. q3map2 applies it per draw surface after `-meta` merging (`light_sh.cpp:645-646`), not per brush face, so the triangles differ; that can change the outcome only at the 1e-3 threshold, and a merged face with no overlap gets zero weight in the mean. Patches are not candidates (Create makes area lights from faces only).
- The new radiance is the overlap-weighted mean L = Σ_f O_f L̄(f ∩ R) / Σ_f O_f, where O_f is the area of face f's overlap with the rectangle R and L̄(f ∩ R) is the face's emissive radiance over that overlap region alone. For a light created from a face, the only emitter is that face and the overlap is the whole face, so Re-derive gives back what Create wrote.
- For a selected `_proxy` light (`light` or `light_spot`), the emitters are the emitter gathering above, and the flux is recomputed as in Create proxy. A `light_sun` is refused whatever its `_proxy`.
- It rewrites only `intensity` and `_color`, never the classname, `origin`, `angles`, cones, `radius` or `_shadows`.
- If no emitter is found, or the result is zero, it says so and changes nothing.

**Messages.** Refusals print one console line saying why, for example "face is not a rectangle", "shader textures/x has no emissive stage" or "emissive image textures/x_e not found". They change nothing.

### 12. Emitters drawn in linear HDR

The new branch in `OpenGLShader::construct` comes after the preview-lit branch (`renderstate.cpp:3112-3160`). It applies when the preview is active, the shader is not preview-lit, `hasEmissive()` is true, the shader is not sky or fog, and it is either `isAllAdditive()` or `nolightmap`. `loadsLightingTextures()` also returns true for these shaders.

`GLSLPBRMaterialProgram`'s `bool m_base` becomes an enum: light, base, additive emitter, opaque emitter. All four reuse `pbr_base_fp`.
- **All-additive.** One pass with ambient 0, which leaves only the emissive. It draws additively, `GL_ONE GL_ONE` (or `GL_SRC_ALPHA GL_ONE` for a `SRC_ALPHA` stage), with a depth test, no depth write and the shader's cull, at `eSortTranslucent`, inside the HDR target.
- **Opaque `nolightmap`.** One opaque pass with depth write, "ambient" 1 and no occlusion map. It writes `srgbToLinear(base) × baseColorFactor + emissive`, which is the stock fullbright result in linear light.
- **Blended shaders that are not all-additive** keep their textured-mode state.

No emitter gets light passes.

### 13. q3map2 placeholder

In `CreateEntityLights`, right after the classname test (`light.cpp:327-335`), skip the entity when its classname is `light_area`, or when it is `light`, `light_spot` or `light_sun` and `e.boolForKey( "_proxy" )` holds. That is the same set `-shbake` leaves out (`ShIsAttachedEmitterLight`, `light_sh_model.cpp:261-271`) with the same reading of the key (the value starts with `1`). Without `light_area` or `_proxy` in the map, the output is byte-identical.

B5 is on `master`, so this change branches from `master` and edits `light.cpp` on top of it. The only other q3map2 edit is comment-only: the comment in `ShIsAttachedEmitterLight` (`light_sh_model.cpp:268-269`) cites "emissive-area-lights design.md:249", a stale line number, and is changed to name this decision (decision 13).

### 14. Branching and gate

The work goes on branch `emissive-area-lights` from `master`. Every task group ends with these checks:
- the editor and q3map2 build with `make SH_BAKER=1` against the submodule's Release `libsh_baker.so` (`tools/stockcheck/README.md:27-38`), with no new warnings. Plain `make` is not used: it relinks `install/q3map2` against the stub (Context, "q3map2 and the stock check"), and the stock check then fails at the light stage with "-shbake: this q3map2 was built without SH_BAKER=1". If a plain `make` runs, `make binaries-q3map2 SH_BAKER=1` runs again after it, before each stock check;
- `make test-emissive-light` passes, from group 3 on;
- the C++ formatting policy of the root `.clang-format` (see its header comment) holds: `git clang-format HEAD -- <files>` formats only the changed lines of existing files, keeping the repo exceptions the header lists (`){` without a space, `for`/`while`/`switch`/`else` braces on the next line, `void(const char*)` in template arguments, hand-aligned columns), and new files (`libs/emissivelight.h`, `libs/emissivelight_test.cpp`) are formatted whole with `clang-format -i`; untouched lines of old files are never reformatted;
- the stock check passes on a clean `git archive` copy of the benchmark. It compiles with `-light -shbake` at the defaults (128 samples, `-super 1`, as "Town (draft)"); the pre-release and final presets are not needed for the gate.

Ask before pushing or opening the PR to `DaviesX/netradiant-custom`.

### 15. The benchmark's area light is a fireplace

The benchmark's `light_area` demo is a simple one-sided fireplace in the north interior (user decision, 2026-11-20), not the neon sign. The sign's `light_area` sat flush on its front face (y = 248) and is one-sided, so the brick wall the sign hangs on (y = 256) lies behind its plane and got nothing; the ground got 1-3 levels of 255. The sign stays as it was before this change: brush 24 is unchanged and carries no light, so its 5 neon faces stay unattached emitters and q3map2-shbake's warning counts 5.

**Geometry.** Six axis-aligned brushes on the 8-unit grid, appended after the patch so existing brush numbers stay put, against the north wall's inner face (y = 752) and centred on x = −184, the pillar's centre line:
- hearth slab (concrete): x −272..−96, y 704..752, z 0..16;
- jambs (brick): x −256..−232 and −136..−112, y 720..752, z 16..64;
- lintel (brick): x −256..−112, y 720..752, z 64..96;
- mantel shelf (concrete): x −264..−104, y 712..752, z 96..104;
- the firebox's back (brush 44): x −232..−136, y 736..752, z 16..64, whose −Y face is the only `bench/fire` face (96 × 48, texture scale 0.75 so the 128 × 64 image covers it once). Its other faces are caulk; the firebox's sides, ceiling and floor are the jambs, the lintel and the hearth, which are not emissive.

Faces against the wall, the floor or another brush are caulk.

**Material.** `textures/bench/fire` follows the neon's pattern: `$lightmap`, `fire_c` multiplied, and `fire_e` added under `rgbGen const ( 1 0.9 0.8 )`, with `qer_pbr_emissiveStrength 64`. `tools/benchmark/gen_fire.py` writes both images (fixed seed, `gen_foliage.py`'s TGA conventions); `fire_e`'s mean linear texel is (0.287, 0.101, 0.026).

**Light.** "Create area light from selected face" on the fire face makes `origin -184 736 40`, `angles 0 270 0`, `width 96`, `height 48`, `radius 256`, `intensity 18.3866` and `_color 1 0.317035 0.0730977`. Two keys are then set by hand, which Re-derive keeps because it rewrites only `intensity` and `_color`:
- `radius 768`, so the light reaches the pillar (about 300 units away), the floor and the room's walls, and its hard cutoff falls where the light is already faint;
- `_frustum 70`, so the pillar casts a firelight shadow on the floor behind it, the benchmark's case of an area-light shadow.

The fire face is attached (q3map2-shbake), so `-shbake` warns about no `textures/bench/fire` surface, and its bake output is byte-identical with and without the `light_area`.

Alternatives considered:
- **Keep the sign and move its `light_area` off the face, or make it face the wall.** Rejected: the light would no longer be attached to its emitter, and Re-derive would find no face.
- **Area lights on all five neon faces.** Rejected. Four lights for 8-unit slivers, and none of them faces the wall either.
- **A proxy for the sign.** Rejected. The benchmark needs a `light_area` with a shadow, and the fireplace gives one with an occluder in front of it.

## Risks / Trade-offs

- **[The light and the face drift apart after edits]** → Re-derive repeats the derivation. q3map2-shbake's unattached-emitter warning flags an emitter whose light no longer matches. The rectangle is drawn unselected, so drift is visible.
- **[The footprint mean is sensitive to texture alignment]** → The neon sign's front face shows only the edges of its tube band: its footprint mean is 0.073 against 0.142 over the whole image. The helper uses the editor's own texture coordinates; `TestFootprintNotWholeImage` covers a footprint smaller than the image. The fireplace's fire face covers `fire_e` exactly once, so its footprint mean is the image's mean.
- **[Float textures may be missing]** → Area lights fall back to diffuse only, with one warning. The parity check (C7) requires the full path.
- **[Lambert diffuse in the area pass and Disney in the others]** → A documented exception. The difference is small for rough dielectrics, and C7 implements the same thing.
- **[Proxies are dimmer than their emitters on the axis]** → A spot proxy of a flat opaque face gives LA/(2d²) on its axis against the face's (and the stock lightmap's) LA/d², about 2× dim; off the axis its intensity stays flat while a Lambertian face falls as cos θ, so near grazing it is brighter than the face. A point proxy of a two-sided billboard gives 2πLA/(4πd²) = LA/(2d²) on either side against LA/d², also about 2× dim. The flux is right in both cases. Documented in the README; A9 judges it. Area lights have no such gap, which is why flat rectangular emitters should get a `light_area`.
- **[A hemisphere spot's gizmo would be huge]** → The old rim radius, `radius` × tan(89.9°), is about 573 × `radius`. The spot gizmo now draws its cone to the radius sphere (decision 6), which bounds it for every spot and changes wide spots' gizmos slightly.
- **[Receivers outside `_frustum` are lit]** → Specified and documented. A one-sided light leaves nothing behind it, and the frustum can widen to 88.8°.
- **[One-sided in the preview, two-sided in the bake]** → Documented (plan §rev7). A9 judges it.
- **[Unsupported keys on stock]** → `width`, `height`, `_frustum` and `_proxy` are unknown keys, which stock ignores. Only the spawn line is printed, and it is allowlisted.
- **[The licence of the vendored tables]** → Task 2.1 confirms it before anything is committed (open question 1).
- **[The 16-tile atlas is now shared with area lights]** → Most area lights keep `_frustum 0`. Over-subscription still warns.

## Migration Plan

The change is additive. Maps without `light_area` or `_proxy` render and compile as before. Under the overlay, the `.ent` gains the `light_area` class and the `_proxy` key on `light` and `light_spot` (not `light_sun`), and reinstalling the pack (`install-gamepack.sh`) picks them up. Rollback is a revert: a map holding `light_area` then shows it as a generic entity, and stock behaviour is the same. `ltc_ggx.bin` and its notice `ltc_ggx.LICENSE.txt` install with `setup/data/tools/*` (`Makefile:1460`).

## Open Questions

1. **The LTC tables' licence.** *Resolved by task 2.1:* the licence allows vendoring with the notice and the citation; it is in `tools/ltc/LICENSE`, the commit (`31e5e96b…`) in `tools/ltc/README.md`, and the installed notice in `setup/data/tools/gl/ltc_ggx.LICENSE.txt`. Upstream's `LICENSE` reads as a two-clause BSD licence with an extra clause asking users to cite the 2016 paper, which would allow vendoring with the notice and the citation. Task 2.1 reads it again before anything is committed; if it does not allow vendoring, it stops and asks the user (fitting our own tables with the published method is the fallback).
2. **Rectangle tolerance.** The values are 0.5° for the corners, 0.1 unit for the edges and 0.1 unit for collinear removal. Brush vertices off the grid after rotation may need looser values; the fixtures decide.
3. **Mean texel for `clampMap` and tiled UVs.** `clampMap` is treated as wrap (not recorded in `Quake3Stage`, `shaders.cpp:1390`). Faces with many repeats are sampled at two samples per texel per axis, which costs time on huge faces; a cap may be needed.
4. **Whether Re-derive should find a proxy's emitters by itself,** for example with q3map2-shbake's proxy rule (the light's origin inside the surface's bounds grown by 64 units). For now the user selects them.
5. **Whether clicking the rectangle should select the light.** Today only the centre marker selects it.
6. **The sign's texture alignment.** The face shows only the edges of the tube band (`neon_e` rows 112-127 and 0-47). Shifting t would make the sign read better. The sign is no longer the area-light demo (decision 15), so this only affects its look and its baked light; it is a content decision for the user.
