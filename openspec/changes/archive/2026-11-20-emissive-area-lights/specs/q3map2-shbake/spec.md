# Spec Delta

## MODIFIED Requirements

### Requirement: Attached emitter lights skipped
The stage SHALL NOT add `light_area` entities or lights with `_proxy 1` to either bake, and SHALL print no warning for them: the emitter's surface is already an area light in the bake, so adding them would count its direct light twice in the full bake (plan revision 7). A `light`, `light_spot` or `light_sun` has `_proxy 1` when its `_proxy` value starts with `1`, q3map2's reading of a boolean key, so the placeholder `-light` of openspec change emissive-area-lights skips the same lights. For each shader with emissive surfaces that have no attached light, the stage SHALL print one warning naming the shader and the number of such surfaces. A surface is attached when a `light_area`'s rectangle is coplanar with it (within 1 unit and 1°) and overlaps it, or when a `_proxy` light's origin lies inside the surface's bounds grown by 64 units on every axis. The warning SHALL NOT fail the stock check.

#### Scenario: Proxy not counted twice
- **WHEN** a scratch map's emissive sign is baked with and without a `light` carrying `_proxy 1` in front of it
- **THEN** the two bakes' lightmap, light-grid and draw-vertex lumps and side files are byte-identical

#### Scenario: Area light ignored silently
- **WHEN** the benchmark, whose fireplace's fire face carries a `light_area`, is baked with and without that `light_area`
- **THEN** the two bakes' lightmap, light-grid and draw-vertex lumps (every lump but the entity lump) and side files are byte-identical, and no warning names `light_area`; with the `light_area`, no warning names `textures/bench/fire`, and the warning for `textures/bench/neon` counts the sign's 5 neon faces, which have no light attached

#### Scenario: Unattached emitter warned
- **WHEN** a map has three surfaces with an emissive shader and no attached light
- **THEN** one warning names that shader and the count 3
