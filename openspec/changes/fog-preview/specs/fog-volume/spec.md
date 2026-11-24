# Spec Delta

## Purpose

Defines how a Quake 3 fog volume fogs what lies inside it: which brushes are fog volumes and in what order, which point belongs to which volume, the fog surface, the fog amount and how it is composited, as the stock renderers do it, with opengl1 as the reference for exact colours. The editor preview uses it today; `renderer_sh` (C3a) implements the same model from the README.

## ADDED Requirements

### Requirement: Fog volumes
A fog volume SHALL be a brush, in any entity, hidden ones included, whose content shader declares `fogparms ( r g b ) d`. The content shader SHALL be chosen as q3map2's `SetBrushContents` chooses it:
- Start with the first side that has a shader.
- Walk the remaining sides in order. A liquid side (`surfaceparm water`, `slime` or `lava`) replaces a content shader that is not liquid. Otherwise a fog side (`surfaceparm fog`) replaces a content shader that is neither liquid nor fog.

The volume's colour SHALL be (r, g, b), each clamped to [0, 1]. Its distance SHALL be d, raised to 1 when it is less than 1, as `tr_bsp.cpp` does.

Volumes SHALL be ordered as q3map2 orders the brushes it makes fogs from. A brush is translucent when any of its sides' shaders declares one of `trans`, `trigger`, `playerclip`, `monsterclip`, `alphashadow`, `lightfilter`, `nodrop`, `botclip`, `clusterportal`, `donotenter`, `areaportal`, `origin`, `water`, `slime` or `lava` (q3map2's `C_TRANSLUCENT`), and opaque otherwise. The order is:
1. the opaque brushes of `func_group` entities, later groups first, each group's in map order;
2. worldspawn's opaque brushes, in reverse map order;
3. worldspawn's translucent brushes, in map order;
4. the translucent brushes of `func_group` entities, in group order, each group's in map order;
5. every other brush entity in entity order, its opaque brushes in reverse map order and then its translucent ones in map order.

This is the order `FinishBrush` and `MoveBrushesToWorld` (`map.cpp`) leave and `CreateMapFogs` walks.

A point SHALL belong to a volume when its signed distance outside every plane of the brush is at most ε = 1 + depth / 512 map units, where depth is the point's view-axis depth. That tolerance absorbs depth-buffer reconstruction, so a floor flush with a fog face is fogged evenly. When a point lies in several volumes, the last in this order SHALL be used, as q3map2's later fogs overwrite earlier ones.

These limits SHALL apply:
- A brush with more than 32 planes SHALL be no volume. The console SHALL warn, naming it and its plane count, when it first exceeds the limit, and again only after it has dropped back under.
- At most 8 volumes SHALL be honoured, the first 8 in the order above. The console SHALL print one warning naming the number found, again only when the number changes.

Global fog (`fog` or `_fog` on worldspawn) is not a fog volume.

#### Scenario: Town volume
- **WHEN** the town is open
- **THEN** it has one fog volume, the street-level brush from z = −8 to 264, with colour (0.200392, 0.258824, 0.231373) and distance 2048

#### Scenario: Sealed room outside the volume
- **WHEN** a point lies in one of the town's sealed rooms below z = −8
- **THEN** it belongs to no fog volume

#### Scenario: Floor flush with a fog face
- **WHEN** a floor lies in the plane of a fog brush's bottom face, as the benchmark's street does
- **THEN** its points belong to the volume

#### Scenario: Overlapping translucent volumes
- **WHEN** a point lies inside two worldspawn fog brushes whose shaders declare `surfaceparm trans`
- **THEN** it takes the volume that comes later in map order

#### Scenario: Overlapping opaque volumes
- **WHEN** a point lies inside two worldspawn fog brushes none of whose sides declares a translucent surfaceparm
- **THEN** it takes the volume that comes earlier in map order

#### Scenario: Opaque func_group fog under a worldspawn fog
- **WHEN** a point lies inside an opaque fog brush in a `func_group` and an opaque worldspawn fog brush
- **THEN** it takes the worldspawn volume, which comes later in the order

#### Scenario: Fog face over a non-solid side
- **WHEN** a brush's first side is a `nonsolid` trigger shader and another side declares `surfaceparm fog` and `fogparms`
- **THEN** the brush is a fog volume

#### Scenario: Liquid wins
- **WHEN** a brush has a `surfaceparm water` side whose shader declares no `fogparms`, and a fog side
- **THEN** it is no fog volume

#### Scenario: Fogged water
- **WHEN** a brush's content shader is a water shader that declares `fogparms`
- **THEN** it is a fog volume with the water shader's colour and distance

#### Scenario: Clamps
- **WHEN** a shader declares `fogparms ( 1.5 0.5 -0.2 ) 0.5`
- **THEN** its volume's colour is (1, 0.5, 0) and its distance 1

#### Scenario: Too many volumes
- **WHEN** a map holds 9 fog brushes
- **THEN** 8 are volumes and the console warns once that 9 were found

#### Scenario: Too many planes
- **WHEN** a fog brush has 33 planes
- **THEN** it is no volume and the console warns once, naming it and its plane count

### Requirement: The fog surface
A volume's fog surface SHALL be one of its non-bevel sides. That side's plane bounds the fogged region when the camera is outside the volume:
- When the content shader declares a non-zero `q3map_fogDir ( x y z )` and some side's outward normal has a negative dot product with it, the side SHALL be the first, in the order −X, +X, −Y, +Y, −Z, +Z and then the other sides, with the most negative dot product, as q3map2's `fog.cpp` chooses it.
- Otherwise the side SHALL be the first, in the order +Z, −Z, +Y, −Y, +X, −X and then the other sides, whose shader is not `surfaceparm nodraw`, as q3map2's `writebsp.cpp` orders them.
- With no such side, there SHALL be no fog surface.

q3map2 also skips sides with no visible part after compiling, and it orders non-axial sides as its axial-swap leaves them. The editor SHALL NOT apply the visibility test, because it needs a compiled BSP, and SHALL order non-axial sides in map order. Where q3map2 would skip the editor's chosen side as hidden, or order non-axial sides differently, the two differ. The town's volume, whose every side is hidden, has no fog surface on the stock renderers but one in the editor. Differences show only with the camera outside the volume.

#### Scenario: Box over a street
- **WHEN** a fog brush's six axial sides all carry the fog shader
- **THEN** its +Z side is the fog surface

#### Scenario: Explicit direction
- **WHEN** the fog shader declares `q3map_fogDir ( 1 0 0 )`
- **THEN** the −X side is the fog surface

#### Scenario: Tie on direction
- **WHEN** the fog shader declares `q3map_fogDir ( 1 1 0 )` on an axis-aligned box
- **THEN** the −X side is the fog surface, being first in the order among the two sides with the most negative dot product

#### Scenario: Direction with no opposed side
- **WHEN** the fog shader declares `q3map_fogDir ( 0 0 0 )`
- **THEN** the fog surface is chosen by the axial order, so a box's +Z side

### Requirement: Fog amount
For a point p in a volume of distance d, seen from the eye e looking along the unit forward vector v, the fog amount SHALL be

f = √(clamp(depth × fraction / d, 0, 1)), with depth = (p − e) · v

and the fraction defined as follows. Let t(x) be the distance of a point x below the fog surface's plane, positive on the volume's side.
- With no fog surface, the fraction SHALL be 1, wherever the eye is.
- With a fog surface and t(e) ≥ 0, the fraction SHALL be 1 when t(p) ≥ −ε (the membership tolerance) and 0 otherwise.
- With a fog surface and t(e) < 0, the fraction SHALL be 0 when t(p) < 1, and t(p) / (t(p) − t(e)) otherwise, the share of the view ray below the plane.

This is the model of `RB_CalcFogTexCoords` and `R_FogFactor` (`renderergl1`) and `CalcFog` (`renderergl2`), evaluated per pixel in rend2's analytic form. The stock renderers evaluate s and the eye-outside t per vertex and interpolate them across each triangle. opengl1 then reads a 256 × 32 fog image with linear filtering, clamped at its edge, which caps depth / d at 8 before the eye-outside fraction applies and filters the fraction over 32 rows. These are known differences, largest with the eye outside a surfaced volume.

#### Scenario: Eye inside a volume without a surface
- **WHEN** the eye and a point are inside a volume with no fog surface and distance 2048, the point 512 units ahead along the view axis
- **THEN** f = √(512 / 2048) = 0.5

#### Scenario: Opaque at the distance
- **WHEN** a point inside a volume lies at depth d or more and the fraction is 1
- **THEN** f = 1

#### Scenario: Eye outside above a surfaced volume
- **WHEN** the eye is 100 units above a volume's fog surface and a point inside lies 50 units below it
- **THEN** the fraction is 50 / 150

#### Scenario: Point behind the eye
- **WHEN** a point's depth along the view axis is 0 or less
- **THEN** f = 0

### Requirement: Fog is composited in display space after the tonemap
Fog SHALL be applied to the finished, tonemapped and sRGB-encoded image as out = (1 − f) × pixel + f × C, where C = 255 × the volume's colour, as opengl1 blends at default cvars. Each pixel SHALL use the nearest surface that writes depth. Exposure and the tonemap SHALL NOT apply to the fog colour. These pixels SHALL NOT be fogged:
- pixels where nothing writing depth was drawn;
- pixels whose nearest depth-writing surface is a sky (a shader declaring `skyparms`), including where a translucent surface was drawn over that sky.

The composite is exact for opaque surfaces and for additive stages over an opaque base stage: opengl1's fog pass over the composite gives (base + add)(1 − f) + f C. The following SHALL be documented as known differences:
- **Assignment.** The stock renderers assign fog per draw surface, as q3map2 decides:
  - brush faces are chopped at the fog brush;
  - patches are cut only at its ±Z planes;
  - world triangle and meta surfaces (models) are fogged whole by the last volume they touch;
  - brush-model surfaces are fogged whole by the volume with the largest bounding-box overlap;
  - surfaces flush with a fog side and facing out of the volume are not fogged;
  - `q3map_noFog` surfaces are never fogged.

  The editor fogs per pixel.
- **Translucent and all-additive surfaces** take the fog of whatever lies behind them; stock fogs them by their own vertices.
- **The sky.** Stock fogs a cloud layer that lies in a volume, at its cloud box's distance. That is invisible when the fog colour equals the sky colour.
- **Fog faces.** Stock draws a volume's visible faces as fog-only surfaces, which fog what lies beyond them as seen from outside. The editor draws no fog faces.
- **Interpolation.** The stock renderers interpolate per vertex, and opengl1 reads a fog image (requirement "Fog amount").
- **Malformed `fogparms`.** q3map2 and stock still make a volume from it; the editor makes none and warns.

#### Scenario: Fullbright wall
- **WHEN** an opaque surface whose tonemapped value is P lies inside a volume at fog amount f
- **THEN** its pixel is (1 − f) P + f C within 2 in each 8-bit channel

#### Scenario: Fog meets the sky
- **WHEN** a surface at the fog's distance, with the eye inside the volume, is drawn next to a sky surface whose image equals C
- **THEN** the two pixels are equal within 1 in each 8-bit channel

#### Scenario: Glass in front of the sky
- **WHEN** a blended glass pane inside a volume is seen against the sky
- **THEN** the glass still draws over the sky, and those pixels are not fogged

#### Scenario: Opaque surface in front of the sky
- **WHEN** an opaque surface inside a volume is drawn in front of a sky face, in whichever order the renderer draws them
- **THEN** its pixels are fogged

### Requirement: Fog helpers are tested
These SHALL live in a header-only library with no editor dependency, `libs/fogvolume.h`:
- the content-shader choice and the volume order;
- the `fogparms` clamps;
- the fog surface choice;
- the point-in-volume test with its tolerance, the last-volume choice and the limits;
- the fog amount and the fraction;
- the display-space composite.

`make test-fog-volume` SHALL build and run its tests, and the target SHALL stay out of the installed tree.

#### Scenario: Tests pass
- **WHEN** `make test-fog-volume` runs
- **THEN** it exits 0, with a case for each scenario of this capability that needs no editor, plus two comparisons:
  - against a transcription of rend2's `CalcFog`, within 1e-5;
  - against a transcription of opengl1's opaque path, within 0.05 in f wherever f ≥ 0.1 for volumes with no surface or with the eye inside, and with the eye-outside differences printed without a bound

### Requirement: Fog model documented in the gamepack README
The gamepack README SHALL describe:
- which brushes are fog volumes, their order, the membership rule with its tolerance, and the limits;
- the fog surface rule and its differences from q3map2;
- the fog amount and fraction formulas, citing the stock functions;
- the display-space composite after the tonemap, why, and opengl1 as the reference, with opengl2's own tonemap noted;
- the "Lighting fog" preference;
- every known difference listed in "Fog is composited in display space after the tonemap", with the opengl1 figure from the tests.

The description SHALL be complete enough for `renderer_sh` to reimplement the model without reading the editor's code.

#### Scenario: README section
- **WHEN** the gamepack README is read
- **THEN** it has a fog section with each item above, and the town's volume is worked through as an example
