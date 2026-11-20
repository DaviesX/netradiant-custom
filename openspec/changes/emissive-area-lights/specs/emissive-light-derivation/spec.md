# Spec Delta

## Purpose

Editor commands that attach lights to emissive surfaces and derive their radiance or flux from the shader: "Create area light from selected face", "Create proxy light from selection" and "Re-derive light from emitter", with the pure helpers they share.

## ADDED Requirements

### Requirement: Commands in the Entity menu
The editor SHALL register the commands `EntityAreaLightFromFace` ("Create area light from selected face"), `EntityProxyLightFromSelection` ("Create proxy light from selection") and `EntityRederiveLight` ("Re-derive light from emitter"). They SHALL appear in the Entity menu only when the active game sets `entities="pbr"`. Each run that changes the map SHALL be one undoable step. A command that cannot run SHALL print one console line saying why and SHALL change nothing. The commands SHALL work in every camera draw mode.

#### Scenario: Hidden in other games
- **WHEN** a Doom 3 gamepack is active
- **THEN** the Entity menu has none of the three items

#### Scenario: One undo step
- **WHEN** "Create area light from selected face" creates a light and the user presses undo once
- **THEN** the light is gone and the map is as before

#### Scenario: Works outside lighting mode
- **WHEN** the camera is in textured mode and the command runs on a valid face
- **THEN** it creates the same light as in lighting mode

### Requirement: Emissive radiance of a surface
The mean emissive radiance of a face or patch, or of a planar region of a face, SHALL be L̄ = `qer_pbr_emissiveStrength` × the emissive colour × M, where M is the mean of the linear texel over the region's UV footprint, in RGB. The emissive colour is the emissive stage's `rgbGen const`, and white (1, 1, 1) for a stage without one, as pbr-material-format's requirement "Emissive comes from the first additive stage" defines; the strength is 1 without the keyword (pbr-material-format, "The emissive strength keyword"):
- texels SHALL be read from the emissive stage's image, decoded from sRGB with the piecewise curve (c / 12.92 at or below 0.04045, else ((c + 0.055) / 1.055)^2.4);
- for a `GL_SRC_ALPHA GL_ONE` stage, each texel SHALL be multiplied by its alpha;
- the footprint SHALL be the region's texture coordinates as the editor computes them (for part of a face, interpolated from the face's affine texture mapping), with the image repeated outside 0 to 1, and the mean SHALL be weighted by surface area;
- the image SHALL be read from disk, independent of lighting mode.

A shader has an emissive stage when it has a first additive map stage that is not a lightmap, `$whiteimage` or `tcGen environment` stage, and that stage's emissive colour is not black. A surface whose shader has none has no emissive radiance.

#### Scenario: Uniform image
- **WHEN** a face's emissive image is a single sRGB value of 188, with `rgbGen const ( 1 0.5 0.25 )` and strength 4
- **THEN** L̄ is 4 × 0.5029 × (1, 0.5, 0.25) within 0.001

#### Scenario: Footprint, not the whole image
- **WHEN** a face shows only the left half of an image whose left half is black and whose right half is white
- **THEN** M is 0

#### Scenario: Tiled face
- **WHEN** a face shows a two-texel image (black, then white along s) repeated 3.5 times along s, starting at s = 0
- **THEN** M is 3/7 within 0.01, because the half repeat shows only the black texel

#### Scenario: No rgbGen const
- **WHEN** a face's emissive stage is a uniform white image with no `rgbGen const`, in a shader with no `qer_pbr_emissiveStrength`
- **THEN** L̄ is (1, 1, 1)

### Requirement: Rectangle test
A face SHALL count as a rectangle when, after removing vertices that lie within 0.1 unit of the line through their neighbours, it has exactly four corners whose angles are each within 0.5° of 90°, and opposite edges whose lengths are equal within 0.1 unit.

#### Scenario: Box face
- **WHEN** the selected face is a side of an axis-aligned box brush
- **THEN** it is a rectangle

#### Scenario: Collinear vertex
- **WHEN** a rectangular face's winding has a fifth vertex on one edge, left by a T-junction with a neighbouring brush
- **THEN** it is still a rectangle

#### Scenario: Trapezoid refused
- **WHEN** the selected face is a trapezoid
- **THEN** it is not a rectangle

### Requirement: Create area light from selected face
With exactly one selected brush face that is a rectangle and whose shader has an emissive stage, "Create area light from selected face" SHALL create a `light_area` and select it, with:
- `origin`: the rectangle's centre, on the face's plane;
- `angles`: chosen so that forward is the face's outward normal, right lies along the edge more nearly perpendicular to world Z (the longer edge on a horizontal face), and up = right × forward points up or sideways, never down; written so that evaluating them gives back the basis within 0.01°;
- `width` and `height`: the edge lengths along right and up;
- `intensity`: the largest component of the face's L̄;
- `_color`: L̄ divided by that component;
- `radius`: max(256, 8 × ⌈2h / 8⌉), where h = ½√(`width`² + `height`²) is the half-diagonal, so the cutoff from the centre reaches at least as far again as the rectangle's own extent.

`_frustum` SHALL be left unset, at its default. The command SHALL refuse when no face or several faces are selected, when the face is not a rectangle, when the shader has no emissive stage, when the emissive image cannot be loaded, or when L̄ is zero.

#### Scenario: Benchmark sign
- **WHEN** the neon sign's front face (plane y = 248, normal −Y) is selected and the command runs
- **THEN** a `light_area` is created with `origin 0 248 120`, a forward vector of −Y, `width 192`, `height 32`, up along +Z, `radius 256` (twice the half-diagonal, 194.7, rounds up to 200), `_color 1 0.35 0.25`, and `intensity` equal to 8 × the face's footprint mean of `neon_e`

#### Scenario: Long emitter radius
- **WHEN** the command runs on a 1024 by 64 emissive rectangle
- **THEN** `radius` is 1032, twice the half-diagonal (1026.0) rounded up to a multiple of 8

#### Scenario: Not a rectangle
- **WHEN** a triangular face is selected and the command runs
- **THEN** the console says the face is not a rectangle and nothing is created

#### Scenario: No emissive stage
- **WHEN** a rectangular face with a plain lightmapped shader is selected and the command runs
- **THEN** the console names the shader as having no emissive stage and nothing is created

### Requirement: Emitter gathering for proxies
"Create proxy light from selection" and "Re-derive light from emitter" on a proxy SHALL gather the same surfaces: the selected brush faces, every face of each wholly selected brush, and the selected patches (by their tessellation), keeping only those whose shader has an emissive stage.

#### Scenario: Plain faces skipped
- **WHEN** a whole brush is selected whose front face is emissive and whose other faces use a plain lightmapped shader
- **THEN** only the front face is gathered

### Requirement: Create proxy light from selection
"Create proxy light from selection" SHALL create a light with `_proxy 1` from the gathered surfaces and select it, with:
- flux Φ = π × Σ k_f × L̄_f × A_f, where A_f is the surface's area and k_f is 2 when every map stage of its shader is additive (a two-sided billboard) and 1 otherwise;
- `intensity`: the largest component of Φ;
- `_color`: Φ divided by that component;
- `origin`: the area-weighted centroid, moved 8 units along the area-weighted mean normal n̄ = Σ A_f n_f when |n̄| is at least 0.1 of the total area, and left at the centroid otherwise.

The light SHALL be a `light_spot` with `cone 90`, `cone_inner 89`, `_shadows 0` and `angles` giving a forward vector along n̄ (roll 0) when every gathered surface has k_f = 1 and |n̄| is at least 0.1 of the total area. Otherwise it SHALL be a `light`. `radius` SHALL be left at its default. The command SHALL refuse when no gathered surface remains or Φ is zero.

On the axis at a distance d large against the emitter, the spot proxy of a one-sided face of radiance L and area A gives LA/(2d²) and the point proxy of a two-sided billboard gives LA/(2d²) on either side, against the emitter's LA/d²: both are about 2× dim, with the right total flux. The gamepack README SHALL state this.

#### Scenario: Opaque emitter
- **WHEN** one opaque lightmapped face of area 1000 with L̄ = (2, 1, 0.5) and outward normal +X is selected and the command runs
- **THEN** a `light_spot` is created with `_proxy 1`, `intensity` 2000π, `_color 1 0.5 0.25`, `cone 90`, `cone_inner 89`, `_shadows 0` and a forward vector of +X, 8 units in front of the face's centre

#### Scenario: Spot proxy on its axis
- **WHEN** the proxy of the previous scenario lights a wall facing it 200 units from the proxy along its axis (208 units from the face) in the preview
- **THEN** the red channel of the irradiance E × NdotL its spot pass computes at the wall's centre, read through a debug output in place of the outgoing radiance, is within 1% of L × A / (2d²) = 2 × 1000 / (2 × 200²) = 0.025, about half of the L × A / 208² ≈ 0.046 the face itself gives there

#### Scenario: Two-sided billboard
- **WHEN** an all-additive flame quad of area 1000 with L̄ = (2, 1, 0.5) is selected and the command runs
- **THEN** a `light` is created with `_proxy 1` and `intensity` 4000π

#### Scenario: Faces pointing apart
- **WHEN** all six faces of a cube brush with an opaque emissive shader are selected and the command runs
- **THEN** a `light` is created at the cube's centre, because the mean normal is zero

#### Scenario: Patch emitter
- **WHEN** an emissive patch cylinder is selected and the command runs
- **THEN** a proxy `light` is created at the cylinder's centroid, with the flux of its tessellated surface

### Requirement: Re-derive light from emitter
"Re-derive light from emitter" SHALL recompute `intensity` and `_color` of the selected light and change no other key:
- **For a selected `light_area`:** the emitters SHALL be the brush faces in the map that pass q3map2-shbake's attachment test (requirement "Attached emitter lights skipped"), applied to each brush face: its shader has an emissive stage, its outward normal is within 1° of the light's forward vector, every vertex lies within 1 unit of the rectangle's plane, and some triangle of its fan triangulation, projected on that plane and clipped to the rectangle, covers more than 0.001 square units. The test is per triangle, as q3map2's is, not on the whole polygon's overlap. q3map2 applies it per draw surface after `-meta` merging rather than per brush face, so the two can differ only at the 0.001 threshold. The new radiance SHALL be the overlap-weighted mean Σ O_f × L̄_f / Σ O_f, where O_f is the area of face f's overlap with the rectangle and L̄_f is f's emissive radiance over that overlap alone, split into `intensity` and `_color` as "Create area light from selected face" splits it.
- **For a selected `light` or `light_spot` with `_proxy 1`:** the emitters SHALL be the surfaces of "Emitter gathering for proxies", and the new values SHALL be as "Create proxy light from selection" derives them, keeping the light's classname, origin and every other key.

It SHALL refuse when no light, or more than one, is selected, when the selected light is neither a `light_area` nor a `light` or `light_spot` with `_proxy 1` (a `light_sun` SHALL be refused whatever its `_proxy`, because a proxy flux is not a sun's irradiance), when no emitter is found, or when the result is zero.

#### Scenario: Strength changed
- **WHEN** the sign shader's `qer_pbr_emissiveStrength` is changed from 8 to 16, the shaders are reloaded, and Re-derive runs on the sign's `light_area`
- **THEN** its `intensity` doubles and its `_color`, `origin`, `angles`, `width`, `height` and `radius` are unchanged

#### Scenario: Created light round trip
- **WHEN** a `light_area` is created from a face and Re-derive runs on it at once
- **THEN** its `intensity` and `_color` are unchanged, because the face is its only emitter and overlaps the whole rectangle

#### Scenario: Two faces under one rectangle
- **WHEN** a `light_area` spans two coplanar emissive faces of equal overlap, one of radiance (2, 2, 2) and one of (4, 4, 4), and Re-derive runs
- **THEN** its `intensity` is 3 and its `_color` is 1 1 1

#### Scenario: Tilted face not attached
- **WHEN** the only emissive face under a `light_area` is turned 2° away from the light's plane and Re-derive runs
- **THEN** the console says no emitter was found and no key changes

#### Scenario: Light moved off its face
- **WHEN** a `light_area` is moved 20 units away from its face and Re-derive runs
- **THEN** the console says no emitter was found and no key changes

#### Scenario: Sun proxy refused
- **WHEN** a `light_sun` carrying `_proxy 1` is selected with emissive faces and Re-derive runs
- **THEN** the console says the light is not an area light or a point or spot proxy, and no key changes

#### Scenario: Triangle overlap decides attachment
- **WHEN** a coplanar emissive face whose polygon overlaps a `light_area`'s rectangle by more than 0.001 square units, but whose fan triangles each overlap it by at most 0.001, is the only candidate and Re-derive runs
- **THEN** the console says no emitter was found and no key changes, as q3map2-shbake would not count the face as attached

#### Scenario: Proxy needs its surfaces
- **WHEN** a `_proxy` light is selected alone and Re-derive runs
- **THEN** the console says to select the emitting surfaces with it, and no key changes

### Requirement: Derivation helpers are tested
The rectangle test, the `AngleVectors` basis and its inverse with roll, the sRGB decode and footprint mean, polygon area, the clip of a polygon to a rectangle, the attachment test, the radiance split, the created area light's radius and the proxy flux and kind SHALL live in a header-only library with no editor dependency, `libs/emissivelight.h`. `make test-emissive-light` SHALL build and run its tests, and it SHALL be kept out of the installed tree.

#### Scenario: Tests pass
- **WHEN** `make test-emissive-light` runs
- **THEN** it exits 0, with a case for each scenario of this capability that needs no editor

#### Scenario: Basis round trip
- **WHEN** the tests turn 1000 random forward and right pairs into `angles` and back
- **THEN** every basis vector comes back within 0.01°
