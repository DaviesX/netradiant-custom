# Proposal

## Why

An emissive surface glows on every target, but the editor cannot show it lighting anything. Revision 6 planned runtime light from emissive quads only in `renderer_sh` (C7), so the editor could neither author nor preview that light, and the parity rule of §3.4 could not cover it. Plan revision 7 (§rev7, §2.2 step 6, §Units) makes area lights editor lights, like `light`, `light_spot` and `light_sun`. The author attaches a light to an emitter in the editor, the preview shows it, and `renderer_sh` later evaluates the same model (C7). This is task A11. It depends only on A6 and A5.

## What Changes

- **`light_area` entity.** A new point entity of the `pbr` entity module. It is a one-sided rectangle:
  - `origin` is the centre;
  - the forward vector of `angles` is the emitting normal, `width` runs along the right vector and `height` along the up vector, in map units (Quake `AngleVectors`, roll included);
  - `intensity` is the radiance L, uniform over the rectangle, with `_color` as linear RGB;
  - `radius` is the cutoff from the centre (default 256), clamped up to the half-diagonal so it always reaches the corners;
  - `_frustum` is a shadow half-angle in degrees (default 0, no shadow).

  It is defined in `_pbr_lights.ent`. The editor draws it as a rectangle with a normal arrow.
- **`RendererLightParams` gains `eArea`** and the rectangle's half-extent axes. Light culling covers the area light's radius and drops what lies wholly behind its emitting side.
- **`_proxy 1`.** It marks a light that stands in for an emissive surface. The entity definitions offer it on `light` and `light_spot`, the two kinds the commands create. Like `-shbake`, q3map2's placeholder `-light` honours it on `light`, `light_spot` and `light_sun`, read as "the value starts with `1`". The preview and `renderer_sh` treat a proxy as the ordinary light it is, on every class. The editor reads the key only on `light` and `light_spot`, for Re-derive, and ignores it on `light_sun`. The key exists so that the compile stages can ignore it.
- **Three editor commands**, each one undoable step, in the Entity menu of `entities="pbr"` games:
  - "Create area light from selected face": the face must be a planar rectangle. The command creates a `light_area` on the face plane with the face's rectangle and `radius` = max(256, twice the half-diagonal rounded up to a multiple of 8). Its radiance is L̄ = `qer_pbr_emissiveStrength` × `rgbGen const` (white without one) × the mean linear texel over the face's UV footprint, written as `intensity` (the largest component) and `_color` (L̄ / max).
  - "Create proxy light from selection": creates a light with `_proxy 1` and flux kπ Σ L̄_f A_f over the selected emissive faces and patches. k = 1 for opaque emitters, and k = 2 for all-additive ones, which are two-sided billboards. Opaque emitters that face one way get a hemisphere `light_spot` (`cone 90`, `cone_inner 89`, `_shadows 0`) aimed along their mean normal; anything else gets a point `light`. On the axis the proxy is about 2× dimmer than its emitter (documented).
  - "Re-derive light from emitter": repeats the derivation after the shader changes, rewriting only `intensity` and `_color`. A `light_area` finds its emitter faces with q3map2-shbake's attachment test. A `light` or `light_spot` proxy uses the surfaces selected with it, gathered as Create gathers them; a `light_sun` is refused.
- **LTC area-light pass in the preview.** Diffuse is (1 − metallic) × base colour × the normalised form factor of the rectangle, clipped to the fragment's horizon and to the emitting side. Specular is GGX through the LTC tables, weighted by the tables' Fresnel fit. The tables are upstream's 64 × 64 GGX fit from `github.com/selfshadow/ltc_code` (`fit/results/ltc.js`, the 2017 revision of the Heitz et al. 2016 method). They ship as one binary file beside the GLSL in `setup/data/tools/gl/`, which `renderer_sh` reuses byte for byte. Without float textures, area lights fall back to diffuse only, with one warning.
- **`_frustum` shadows** on the spot shadow path: a perspective map from the rectangle's centre along its normal with field of view 2 × `_frustum`, sharing the spot atlas, with the caster rule. Area lights ignore `_shadows`.
- **Emitters the preview doesn't light are drawn in linear HDR.** This covers shaders with an emissive stage that are all-additive or `surfaceparm nolightmap`. The emissive is drawn as sRGB-decoded texel × `rgbGen const` × `qer_pbr_emissiveStrength`, as for lit emitters. A `nolightmap` base is drawn fullbright in linear light. Today they take their textured-mode state (`radiant/renderstate.cpp:3191-3229`).
- **q3map2 placeholder `-light`** skips `light_area` entities and `light`, `light_spot` and `light_sun` entities with `_proxy 1` in `CreateEntityLights`, the same set `-shbake` already leaves out. Today every `light*` classname becomes a 300-photon point light (`tools/quake3/q3map2/light.cpp:327-335`, `:439-443`).
- **Stock check:** the allowlist gains `^light_area doesn't have a spawn function$`.
- **Benchmark:** the neon sign's front face gets a `light_area` created by the new command. Its other four neon faces stay unattached, as q3map2-shbake's scenario "Area light ignored silently" expects.
- **Docs:** the gamepack README documents the area and proxy model, its units, LTC, the one-sided versus two-sided difference from sh-baker, the derivation formulas, the proxies' on-axis residual, the `_frustum` shadow and the tables' source.
- **Out of scope:**
  - the `-shbake` skip, the attachment test and the unattached-emitter warning, which landed with B5 (requirement "Attached emitter lights skipped" in `openspec/specs/q3map2-shbake/spec.md`);
  - `renderer_sh` (C7);
  - modulating the area light by the emissive image;
  - animated emission;
  - moving emissive quads;
  - proxies from `misc_model` surfaces.

## Capabilities

### New Capabilities

- `emissive-light-derivation`: the three commands, the rectangle test, the radiance and flux formulas, the proxy kind, the emitter lookup, and the pure helpers they share.

### Modified Capabilities

- `pbr-light-entities`: `light_area` and its keys, `_proxy`, the `eArea` renderer type and area culling, the entity definitions, area lights ignoring `_shadows`, and q3map2's placeholder `-light` skipping attached lights.
- `pbr-lighting-preview`: the area pass with LTC and its fallback, the Lambert form-factor exception to Disney diffuse, and linear-HDR drawing of emitters the preview doesn't light.
- `pbr-shadow-maps`: `_frustum` shadows in the spot atlas.
- `pbr-gamepack`: the README documents the area and proxy model, the LTC tables' source and the area shadow.
- `stock-check`: the allowlist starts with three spawn-function entries.
- `benchmark-map`: the neon sign carries a `light_area`.

## Impact

- `include/irender.h`: `RendererLightParams::eArea`, `right` and `up` half-extent axes.
- `include/ishaders.h`, `plugins/shaders/shaders.cpp`:
  - `IShader::hasEmissive()`, `getEmissiveName()`, `isAllAdditive()` and `getEmissiveSrcAlpha()`;
  - `loadsLightingTextures()` also covers emitters.
- `plugins/entity/entity.cpp`, `light.cpp`: the `light_area` class routing, `PBRLight` area keys and basis, params, culling, gizmo and bounds, the `_proxy` observer, and a spot gizmo drawn to its radius sphere so a hemisphere spot proxy's gizmo stays bounded.
- `radiant/brush.h`: `Face_addLight` tests the rectangle's corners for area lights.
- `radiant/renderstate.cpp`:
  - the area uniforms and type 4;
  - LTC texture load, bind and release;
  - the area shadow on the spot path;
  - the emitter draw branch.
- `radiant/camwindow.cpp`: the per-frame LTC bind.
- `setup/data/tools/gl/pbr_fp.glsl` (the area branch) and the new `setup/data/tools/gl/ltc_ggx.bin` with its licence notice `setup/data/tools/gl/ltc_ggx.LICENSE.txt`.
- New `tools/ltc/` with the vendored upstream `fit/results/ltc.js`, its licence, a README recording the upstream commit, and the converter script.
- New header-only `libs/emissivelight.h` with `libs/emissivelight_test.cpp` and `make test-emissive-light`.
- `radiant/entity.cpp` (the commands and menu), plus a public tessellation accessor on `Patch` (`radiant/patch.h`).
- `tools/quake3/q3map2/light.cpp` (`CreateEntityLights`). B5 is on `master`, and this change branches from `master` (design decision 14). The comment in `light_sh_model.cpp` that cites a stale design line number is changed to name design decision 13 (comment only).
- `setup/data/gamepacks/pbr/`: `_pbr_lights.ent`, the `games/Q3.game` comment and `README.md`.
- `tools/stockcheck/allowlist.txt` and `README.md`.
- `content/benchmark/maps/bench.map` and `README.md`.
- `CLAUDE.md` ground rules, and the plan's §2.2 status on completion.
- Unaffected:
  - textured, solid and wireframe modes;
  - Doom 3 and Quake 4 games;
  - `-shbake` (q3map2-shbake);
  - the shipped BSP's stock behaviour, apart from one more allowlisted spawn line per `light_area`.
