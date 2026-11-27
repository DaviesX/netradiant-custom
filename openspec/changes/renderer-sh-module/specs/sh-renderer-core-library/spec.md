# Spec Delta

## Purpose

sh-renderer is split into a core library that an ioquake3 renderer module can embed and a glTF/viewer target that keeps the standalone viewer and the glTF path. This capability is the contract between the two halves and the engine.

## ADDED Requirements

### Requirement: Core library without process-owning dependencies
sh-renderer SHALL build a static core library holding the render passes, scene, layer compositor, shader loader, render targets and frame API. The core SHALL NOT link or include glog, gflags, GLFW, tinygltf, tinyexr, Embree or stb_image's implementation. Its only third-party dependencies SHALL be OpenGL through its bundled loader and Eigen.

#### Scenario: Core link closure
- **WHEN** the core library is built and its link interface and include closure are inspected
- **THEN** none of glog, gflags, glfw, tinygltf, tinyexr or embree appears

#### Scenario: Core builds alone
- **WHEN** the core target is configured on a machine without glog, gflags, GLFW, tinygltf or tinyexr installed, with the glTF target disabled
- **THEN** the core and its tests build

#### Scenario: Embedded without tests
- **WHEN** the project is added to another build with tests and the glTF target turned off
- **THEN** GoogleTest is not looked for and only the core library is configured

### Requirement: glTF target keeps the viewer
A separate glTF target SHALL hold the glTF loader, tinygltf, tinyexr, GLFW, gflags, the window, input and interaction code, the EXR lightmap reader and `ComputeLightRadius`, and the viewer `sh_renderer_main`. Given the same scene, flags and window size, the viewer SHALL render the same frames as before the split.

#### Scenario: Viewer output unchanged
- **WHEN** the viewer is run before and after the split on the tracked scene `data/Sponza/scene.gltf` at the same size, camera and MSAA setting, and its third presented frame is read back
- **THEN** the two readbacks are byte-identical

#### Scenario: Tests unchanged
- **WHEN** the full test suite is run after the split
- **THEN** every test that passed before passes, with core tests linked against the core and loader, window and input tests against the glTF target

### Requirement: Logging shim with fatal semantics
The core SHALL log through a shim with glog's macro syntax (`LOG(INFO|WARNING|ERROR|FATAL)`, `LOG_EVERY_N`, `CHECK`, `CHECK_op`, `DCHECK`) and a sink the embedding program installs. A failed `CHECK` or `LOG(FATAL)` SHALL call the sink's fatal handler with the message and source location, and SHALL NOT return to the caller. With no sink installed, messages SHALL go to stderr, and fatal SHALL abort.

#### Scenario: Default sink
- **WHEN** a program that installs no sink logs a warning and then fails a `CHECK`
- **THEN** both messages appear on stderr with severity and location, and the process aborts

#### Scenario: Installed sink
- **WHEN** a sink is installed and code logs at each severity
- **THEN** the sink receives each message with its severity; the fatal handler receives the `CHECK` message, and control never returns past the failed check

### Requirement: Program creation reports failure
Creating a shader program SHALL return an empty result with the driver's info log, never end the process, so the caller decides. When setup fails because programs failed, its error SHALL list each failed program with its files and the reason (the missing file or the driver's info log), and SHALL leave the host's GL state as it was. A zero-sized framebuffer passed to the frame API SHALL skip the frame.

#### Scenario: Program rejected
- **WHEN** a program's source doesn't compile
- **THEN** its create function returns an empty program and an error message naming the files and the info log, and the process continues

#### Scenario: Zero framebuffer
- **WHEN** `BeginFrame` is called with a width or height of 0
- **THEN** it reports that the frame is skipped, renders nothing and fails no check

### Requirement: Caller-supplied file reader
Every file the core reads, GLSL sources and their `#include`s included, SHALL be read through a reader the embedding program supplies, by a forward-slash path relative to the reader's root. The core SHALL provide a disk reader rooted at a directory and a reader over the GLSL table built into the library. A reader SHALL be able to chain to a fallback reader.

#### Scenario: Includes through the reader
- **WHEN** a program is built through an in-memory reader whose files include each other
- **THEN** the program source is assembled from the reader alone, with no file system access

#### Scenario: Built-in GLSL
- **WHEN** the library is built with GLSL embedding on and a program is built through the built-in reader from a working directory without `glsl/`
- **THEN** the program compiles from the built-in table

### Requirement: Inverted frame API
The core SHALL expose setup, per-frame `BeginFrame`, `RenderScene` (given the scene, camera and the host's time) and `EndFrame`, and teardown, called by the host, which owns the window, context, swap and input. Setup SHALL load GL through a host-supplied address resolver and enable sRGB output. Resources sized by a scene SHALL be made when a scene is first rendered. `EndFrame` SHALL leave the tonemapped frame in the default framebuffer unswapped.

#### Scenario: Engine-driven frame
- **WHEN** a host with a current GL 4.6 context calls setup, then `BeginFrame`, `RenderScene` with no scene and `EndFrame`
- **THEN** the default framebuffer holds the sRGB-encoded tonemapped clear colour, and no window or input call was made by the core

#### Scenario: Viewer uses the same calls
- **WHEN** the viewer renders a frame
- **THEN** it does so through setup, `BeginFrame`, `RenderScene` and `EndFrame`, then swaps its own window

#### Scenario: Readback through the facade
- **WHEN** the host asks the facade for the presented frame's pixels after `EndFrame`
- **THEN** it receives the default framebuffer's RGB bytes, with no GL header included by the host

### Requirement: Eigen-free engine facade
The core SHALL provide an engine-facing header whose declarations use only fundamental types, standard containers and opaque handles, so engine translation units built without the core's compiler flags never include Eigen or another core header.

#### Scenario: Facade header standalone
- **WHEN** a translation unit including only the facade header is compiled with no `-march` flag and no Eigen include path
- **THEN** it compiles

### Requirement: Display gamma in the tonemap
The tonemap SHALL take a display gamma g (default 1), clamped to [0.5, 3]; a non-finite or non-positive g SHALL be taken as 1, with a warning. For g ≠ 1, with e the unrounded sRGB encoding of the tonemapped colour, the output byte SHALL be 255·e^(1/g) rounded, within 1. At g = 1 the shader output SHALL be bit-identical to the tonemap before this change.

#### Scenario: Gamma one is identity
- **WHEN** the viewer renders with its default gamma
- **THEN** its frames match those before the change (see "Viewer output unchanged")

#### Scenario: Out-of-range gamma
- **WHEN** a frame of known colour is rendered with display gamma 0, -1, NaN or +inf, and again with 10 and 0.1
- **THEN** the first four give the gamma-1 bytes, 10 gives the gamma-3 bytes and 0.1 the gamma-0.5 bytes

### Requirement: Q3 coordinate build option
The build SHALL offer an option, `SH_RENDERER_Q3_COORDINATE`, default off, that defines `Q3_COORDINATE` for the core's translation units as a public definition of the core target, so every target that links the core (its tests, the engine glue) is compiled with it too. The ioquake3 module SHALL build the core with it on. The viewer and the tests SHALL build with it off unless a test configuration asks for it.

#### Scenario: Option defines the macro
- **WHEN** the core is configured with `-DSH_RENDERER_Q3_COORDINATE=ON`
- **THEN** the core's translation units and those of every target linking it are compiled with `Q3_COORDINATE` defined, and with the option off none are
