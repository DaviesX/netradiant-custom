# Spec Delta

## Purpose

The contract by which q3map2 hands its scene to the sh-baker library and has its own luxels baked. The library builds tracing geometry from q3map2's draw surfaces, repairs what real content can produce, and treats malformed input as a caller bug. It then bakes the sample points q3map2 supplies, with no rasterization of its own. Later stages extend the contract with materials and lights.

## ADDED Requirements

### Requirement: Surface input
The q3map2 loader SHALL take one draw surface with these fields:
- world-space positions;
- normals;
- texture UVs;
- triangle indices, in q3map2's winding (front faces clockwise);
- a material index, negative for a pure occluder.

Given a scene that already holds the materials, it SHALL append one geometry carrying those fields, with no lightmap UVs and an identity transform. Nothing else in the scene SHALL change.

#### Scenario: Valid lit surface
- **WHEN** a quad with four vertices, two triangles and material 0 is added to a scene with one material
- **THEN** the scene has one more geometry, with those positions and texture UVs, material 0, no lightmap UVs and an identity transform, and the scene's lights and materials are unchanged

#### Scenario: Occluder without UVs
- **WHEN** a material-less surface has positions, normals and indices but no texture UVs
- **THEN** it is added, and its texture UVs are (0, 0) for each vertex, as the glTF loader gives them

### Requirement: Winding converted
Surfaces arrive in q3map2's winding, where front faces are clockwise. The added geometry SHALL store its triangles counter-clockwise, as every sh-baker scene does, by swapping each triangle's second and third index.

#### Scenario: Flipped triangles
- **WHEN** a surface with indices `0 1 2  2 1 3` is added
- **THEN** the added geometry's indices are `0 2 1  2 3 1`

### Requirement: Malformed input aborts
Input only a caller bug or corrupt data can produce SHALL fail a check that stops the process and names the field and value:
- no positions;
- normals not one per vertex;
- texture UVs not one per vertex, when the surface has a material or gives any;
- an index count that is not a multiple of 3, or an index not below the vertex count;
- a material index not below the scene's material count;
- a non-finite position, normal or texture UV.

Texture UVs outside [0, 1] tile and are accepted.

#### Scenario: Index out of range
- **WHEN** a four-vertex surface has an index of 4
- **THEN** the process dies, and its message names the index and the value 4

#### Scenario: Unknown material
- **WHEN** a surface has material 3 and the scene has two materials
- **THEN** the process dies, and its message names the material

#### Scenario: Lit surface without texture UVs
- **WHEN** a surface has material 0 and no texture UVs
- **THEN** the process dies

#### Scenario: Non-finite texture UV
- **WHEN** one texture UV of a lit surface is (NaN, 0)
- **THEN** the process dies, and its message names the vertex

#### Scenario: Tiling UVs
- **WHEN** a lit surface's texture UVs range over [-3, 40]
- **THEN** it is added with those UVs unchanged

### Requirement: Occluders block without shading
A material-less surface added through the loader SHALL block both direct-light and indirect rays, and SHALL contribute no radiance when hit. This is how q3map2's solid brush hulls enter the bake behind the thin draw surfaces.

#### Scenario: Hull behind a crack
- **WHEN** a lit receiver point faces a directional light through a gap between two thin lit quads, and a material-less closed box is added just behind the gap
- **THEN** the point's baked L0 drops to below 5% of its value without the box

### Requirement: Scene assembly order
All surfaces SHALL be added before any light that points at scene geometry (an area light) is created, and before the BVH is built. Adding a surface can move the scene's geometry storage, which would leave those pointers dangling. Adding a surface to a scene that already holds a light with a geometry pointer SHALL fail a check.

#### Scenario: Surface after an area light
- **WHEN** a surface is added to a scene that already holds an area light pointing at one of its geometries
- **THEN** the process dies, and its message names the assembly order

### Requirement: Normals normalized and repaired
Every stored normal SHALL be unit length. A normal of length 1e-6 or more SHALL be normalized. A shorter one, which degenerate patches and models produce, SHALL be replaced by the normalized sum of the unnormalized front-face normals of the surface's triangles that use the vertex. If that sum is also shorter than 1e-6, the vertex lies only on zero-area triangles, which rays never hit, and it SHALL get (0, 0, 1).

#### Scenario: Normals normalized
- **WHEN** a surface's normals are all (0, 0, 2)
- **THEN** the added geometry's normals are all (0, 0, 1)

#### Scenario: Zero normal repaired
- **WHEN** one vertex of a quad in the XY plane, wound clockwise as seen from +Z (q3map2's front), has normal (0, 0, 0)
- **THEN** the added geometry's normal at that vertex is (0, 0, 1)

### Requirement: Degenerate triangles
Triangles that repeat a vertex index SHALL be dropped from the added geometry, as the glTF loader drops them. A surface left with no triangles SHALL still be added.

#### Scenario: Repeated index
- **WHEN** a surface's indices are `0 1 2  2 1 3  0 0 1`
- **THEN** the added geometry's indices are `0 2 1  2 3 1`: the degenerate triangle is dropped and the others are flipped to counter-clockwise

### Requirement: Tangents
Each added geometry SHALL get one tangent per vertex, generated the way the glTF loader generates them for a primitive without tangents. That means MikkTSpace from positions, normals and texture UVs when texture UVs were given, and the loader's fallback basis otherwise.

#### Scenario: Tangents follow the texture U axis
- **WHEN** a quad in the XY plane with normals +Z and texture U increasing along +X is added
- **THEN** every tangent's xyz is within 1e-4 of (1, 0, 0), and its w is +1 or -1

#### Scenario: Fallback tangents for an occluder
- **WHEN** a material-less quad without texture UVs is added
- **THEN** every tangent is unit length, is perpendicular to its normal within 1e-4, and has w = 1

#### Scenario: glTF tangents unchanged by the move
- **WHEN** the CLI bakes `data/notangent/scene.gltf`, whose lit primitive has no `TANGENT` and whose material-less primitive has neither `TANGENT` nor `TEXCOORD_0`, with fixed arguments before and after the change
- **THEN** every output file, including the saved glTF's `TANGENT` data, is byte-identical

### Requirement: Baking caller-supplied points
The library SHALL bake a list of N sample points, given as an N×1 buffer with supersample scale 1, in one call that needs no lightmap UVs and no rasterization. Each point's SH result SHALL be at the point's own index. A point marked invalid SHALL be skipped and keep the not-baked marker. Each point SHALL supply a position, a unit normal, and a unit tangent perpendicular to it with handedness w of +1 or -1. The tangent only orients the sampling hemisphere, and w = 0 would flatten it.

#### Scenario: Points from different surfaces in one call
- **WHEN** a scene has a quad facing a directional light and a quad facing away from it, and three points are baked in one call: one on each quad, then one marked invalid
- **THEN** result 0's L0 is greater than result 1's, and result 2 keeps the not-baked marker

### Requirement: Headers usable from q3map2
The loader's header and the baker's header SHALL compile in a translation unit built with `-std=c++20 -fno-exceptions -fno-rtti`, with only sh-baker's `src` and Eigen added to the include path. Their include closure SHALL contain no tinygltf, nlohmann JSON or Embree header, and not the glTF loader's header. The scene header SHALL declare Embree's opaque handle types itself. Their functions SHALL not throw.

#### Scenario: Strict compile
- **WHEN** a file that only includes the loader's header and the baker's header is compiled with `g++ -std=c++20 -fno-exceptions -fno-rtti -fsyntax-only -Isrc -I/usr/include/eigen3`
- **THEN** it compiles without errors, and `g++ -std=c++20 -x c++ -M` on each header lists none of `tiny_gltf.h`, `json.hpp`, `embree4/` or `loader.h`
