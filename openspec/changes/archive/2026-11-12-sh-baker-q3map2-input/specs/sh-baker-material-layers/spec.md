# Spec Delta

## Purpose

sh-baker's own, format-independent model of a material's Quake 3 shader-stage stack, and its lossless translation to and from the `SH_material_layers` glTF extension. Loaders other than glTF, such as q3map2's, can describe the same stacks without depending on tinygltf.

## ADDED Requirements

### Requirement: Owned layer model
A material's stage stack SHALL be held in sh-baker types. They cover the surface blend (`OPAQUE`, `BLEND`, `ADD`), the cull mode (`FRONT`, `BACK`, `NONE`) and the base layer. For each layer they hold the texture's source path, the animation frames' source paths and frequency, the blend factors, the rgbGen (type, wave function and four wave parameters), and the tcMods with all their parameters and, for `TURB` and `STRETCH`, the wave function, including `NONE`.

#### Scenario: Built in code
- **WHEN** a test builds a material with an animated layer using a `TURB` tcMod and a `WAVE` rgbGen, using only sh-baker headers
- **THEN** it compiles and links without including any tinygltf header

### Requirement: Headers free of tinygltf
The include closure of the scene, material and texture headers SHALL not contain `tiny_gltf.h` or `nlohmann/json.hpp`. Only the glTF loader and saver translation units and their tests SHALL include them.

#### Scenario: Include closure
- **WHEN** `g++ -std=c++20 -x c++ -M` is run on `scene.h` with sh-baker's include paths
- **THEN** the dependency list names neither `tiny_gltf.h` nor `json.hpp`

### Requirement: glTF loader reads every extension field
The glTF loader SHALL read every field the exporter writes in `SH_material_layers` into the owned model, resolving each layer and frame texture index to its image's source path. A `WAVE` rgbGen without `func` SHALL load with no wave function, which compositing treats as `SIN`. A name it does not know SHALL log a warning and take a default:
- blend factor: `ONE`;
- rgbGen type: `IDENTITY`;
- wave: `SIN`;
- tcMod type: the tcMod is dropped;
- surface blend: `OPAQUE`;
- cull mode: `FRONT`.

#### Scenario: Every tcMod kind
- **WHEN** a layer's `tcMod` array holds one each of `SCALE [2.0,3.0]`, `SCROLL [0.5,0.0]`, `ROTATE 30.0`, `TURB ["SIN",0.0,0.125,0.0,1.0]`, `STRETCH ["NONE",1.0,0.5,0.0,2.0]` and `TRANSFORM [1.0,0.0,0.5,0.0,1.0,0.0]`, written as the exporter writes them (decimal literals)
- **THEN** the loaded layer has six tcMods with those types, parameters and wave functions, in that order

#### Scenario: Animated layer
- **WHEN** a layer has `animFreq` 5.0 and `animFrames` naming two textures
- **THEN** the loaded layer has frequency 5 and the two frames' source paths in order

#### Scenario: WAVE without func
- **WHEN** a layer's rgbGen is `WAVE` with base, amplitude, phase and frequency but no `func`
- **THEN** the loaded rgbGen's wave function is absent, compositing evaluates it as `SIN`, and saving writes no `func` key

#### Scenario: Unknown blend factor
- **WHEN** a layer's `blendSrc` is `"BOGUS"`
- **THEN** loading logs a warning and the layer's source factor is `ONE`

### Requirement: Saver writes the exporter's form
The saver SHALL write `SH_material_layers` from the owned model with the key set and value types the exporter uses. Conditional keys SHALL be written only when they apply:
- `animFreq` and `animFrames` only for an animated layer;
- `tcMod` only when the layer has tcMods;
- rgbGen's wave parameters only for `WAVE`, and its `func` only when a wave function is present.

Numbers are held at single precision. That is exact for exporter output, which writes doubles converted from floats.

#### Scenario: Round trip
- **WHEN** a glTF carrying an exporter-shaped `SH_material_layers` is loaded and saved
- **THEN** the saved extension equals the input's in every key and value except the texture indices, which name copies of the same images

### Requirement: Saver copies layer images
The saver SHALL copy each image a layer references next to the output. It SHALL assign output texture indices in layer order, each layer's texture before its frames, and reuse an index for an image already written. A texture or animation frame without a known source SHALL be written with index -1, keeping its position.

#### Scenario: Frame 0 shares the layer texture
- **WHEN** an animated layer's first frame is the same image as its texture
- **THEN** the saved `animFrames[0]` equals the saved `texture.index`

#### Scenario: Unknown source
- **WHEN** a layer's texture and its second animation frame have no source path
- **THEN** the saved layer's `texture.index` is -1, its `animFrames` keeps two entries with -1 second, and saving succeeds

### Requirement: Bake and output unchanged
Replacing the verbatim carrier SHALL not change any result. Compositing at load (albedo, coverage, additive emitters) SHALL give the same textures as before. The CLI's output files SHALL be byte-identical to those of the commit before this change, for scenes with and without `SH_material_layers`. The pre-existing tests other than the two pass-through tests SHALL pass unmodified, with the same results as before.

#### Scenario: Layered fixture
- **WHEN** the CLI bakes `data/layers/scene.gltf` with fixed arguments before and after the change
- **THEN** every output file is byte-identical

#### Scenario: Plain scenes
- **WHEN** the CLI bakes `data/box/scene.gltf` and `data/MultiLights/scene.gltf` with fixed arguments before and after the change
- **THEN** every output file is byte-identical

#### Scenario: Existing tests
- **WHEN** the unmodified pre-existing tests are built and run against the changed library
- **THEN** each one has the same result as on the commit before the change

### Requirement: Pass-through tests keep their assertions
The two pre-existing tests of the extension pass-through (`PassesMaterialLayersThrough` and `LoaderRetainsMaterialLayers`) SHALL build the owned model instead of a `tinygltf::Value`, and SHALL check the loaded model's fields instead of the raw extension. Every assertion they make on the saved glTF SHALL be kept unchanged.

#### Scenario: Saved-glTF assertions kept
- **WHEN** the diff of `src/saver_test.cpp` against the commit before the change is inspected
- **THEN** the only removed lines are the construction of `tinygltf::Value` extensions and texture-index maps, and the asserts on `MaterialLayers::extension` and `::texture_paths`
