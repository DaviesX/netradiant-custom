# Proposal

## Why

Track C grows sh-renderer into `renderer_sh`, an ioquake3 renderer module loaded by `cl_renderer sh`. Today sh-renderer owns its process. It has a GLFW window and frame loop, glog and gflags, reads GLSL from `glsl/` relative to the working directory, and builds as one SHARED library with `-march=native` that also contains tinygltf, tinyexr and the viewer's input code. Nothing in C2–C8 can be ported until the module exists. If features are ported before the module's edges are fixed, each one gets ported twice. Plan §3.1 therefore makes C1 a refactor done first: the only part of Track C that isn't additive.

## What Changes

The work spans two repos outside this one, delivered as PRs the user reviews and merges. Pushing a branch or opening a PR is asked first, every time.

**sh-renderer** (`github.com/DaviesX/sh-renderer`, working copy `../ioq3-custom/sh-renderer`):
- **CMake split.** A static core library, `sh_renderer_core`, holds the passes, scene, compositor, shader loader, render targets and a new frame API. A separate glTF target, `sh_renderer_gltf`, holds the glTF loader, tinygltf, tinyexr, stb_image's implementation, GLFW, gflags, `window`/`input`/`interaction` and the viewer `sh_renderer_main`. The core links no glog, gflags, GLFW, tinygltf or tinyexr.
- **Code moves.** `scene.cpp`'s tinyexr include, `LoadLightmaps` and `ComputeLightRadius` go to the glTF target, and so does `implementations.cpp`. `scene.h` drops the unused `<embree4/rtcore.h>`.
- **Q3_COORDINATE option.** The CMake option `SH_RENDERER_Q3_COORDINATE` (default OFF) defines `Q3_COORDINATE` on the core as a public definition, so its consumers see it too. Nothing reads it until C2.
- **Logging shim.** `log.h` provides glog-compatible `LOG(severity)`, `LOG_EVERY_N`, `CHECK*` and `DCHECK` over a replaceable sink. The viewer's sink writes to stderr and aborts on fatal, as glog does. The module's sink goes to `ri.Printf`, and a failed `CHECK` or `LOG(FATAL)` goes to `ri.Error(ERR_FATAL)`.
- **Environment failures are handled.** A 0×0 framebuffer skips the frame, where it used to fail `main.cpp:126-127`. The four `LOG(FATAL)` program-creation sites (`draw_radiance.cpp:56`, `draw_sky.cpp:19`, `draw_tonemap.cpp:20`, `compute_light_tile.cpp:19`) return an empty program to the caller.
- **File reader.** The shader loader, including its `#include` resolution, reads through a caller-supplied `FileReader`, not `std::ifstream`. A build-time step turns `glsl/*` into an embedded table (`SH_RENDERER_EMBED_GLSL`).
- **Frame API.** The frame loop is inverted onto `BeginFrame`/`RenderScene`/`EndFrame` in the core's `frame.{h,cpp}`. The viewer drives the same calls. An Eigen-free facade, `engine_api.h`, is all that engine TUs include.
- **Display gamma.** The tonemap gains a display-gamma uniform, applied to the unrounded sRGB value. At 1.0 it changes nothing.
- **Viewer and docs.** The viewer renders the same frames as before. `GEMINI.md` describes the split.

**ioq3** (`github.com/DaviesX/ioq3`, `../ioq3-custom/ioq3`):
- **Context request.** `sdl_glimp.c` gains `GLimp_InitWithRequest(const glimpContextRequest_t *)`, which asks for exactly one context: 4.6 core, debug, sRGB-capable. `GLimp_Init(fixedFunction)` keeps its signature and behaviour, so opengl1/opengl2 request exactly what they do today and their sources are unchanged.
- **Build.** `cmake/renderer_sh.cmake` adds `BUILD_RENDERER_SH` (default OFF), builds the `code/tools/sh-renderer` submodule's core with `SH_RENDERER_Q3_COORDINATE=ON` and `SH_RENDERER_EMBED_GLSL=ON`, and links `renderer_sh` against it.
- **Engine glue.** `code/renderersh/` contains the engine glue: `GetRefAPI` and a complete `refexport_t` whose entry points are safe no-ops apart from init, frame, screenshot and shutdown. glad is loaded inside sh-renderer's core through `SDL_GL_GetProcAddress`, passed in as a resolver, so no engine TU includes `glad.h`. After a fatal error the glue makes no further call into the core. The glue sets the log sink and the GLSL reader (`ri.FS_ReadFile("glsl/sh/<name>")` first, then the embedded copy from the facade's `ShEmbeddedReader()`); if the renderer fails to build while an override was used, it is rebuilt entirely from the embedded GLSL with a warning naming the dropped overrides. `r_gamma` is applied in the tonemap; no hardware ramp is ever set.
- **Context failures.** If no 4.6 core context can be created, the module sets `cl_renderer` to `opengl2`, saves it with `writeconfig` (the fatal path never writes `q3config.cfg`), and calls `ri.Error(ERR_FATAL)` with a message naming the fix. It does the same when an embedded GLSL program is rejected.
- **The frame.** The HDR target is cleared to `r_shClearColor` and tonemapped to the window, and a `screenshot` command reads it back.
- **Submodule bump.** The submodule pointer moves to the merged sh-renderer.

**This repo:**
- **Smoke check.** `tools/stockcheck/renderer_sh_smoke.py` boots ioquake3 with `cl_renderer sh` in an isolated basepath. It fails unless the console proves `renderer_sh` loaded (no `failed:` fallback, the module's banner present) and the screenshot matches the expected tonemapped clear colour at `r_gamma` 1 and 1.5.
- **Docs.** The plan (§3.1, §Verification) and `CLAUDE.md` (C1) record the decisions made here: embedded GLSL with a VFS override, shader gamma with no hardware ramp, and fatal-with-fallback for a machine that can't run `renderer_sh`. `../ioq3-custom/CLAUDE.md` loses its stale C0 text.

Nothing is **BREAKING** for players or content: `opengl1`/`opengl2` are untouched, and `BUILD_RENDERER_SH` defaults OFF. sh-renderer's library target name and layout change (SHARED `sh_renderer` becomes static `sh_renderer_core` plus `sh_renderer_gltf`). Nothing outside sh-renderer links it.

## Capabilities

### New Capabilities

- `renderer-sh-module`: the `renderer_sh` ioquake3 module. It covers loading through `cl_renderer sh`, the context it requests and its failure behaviour, the complete `refexport_t`, engine services (logging, files, cvars, memory), GLSL sourcing, display gamma, the frame and screenshot, and the smoke check that proves it.
- `sh-renderer-core-library`: sh-renderer's split into an engine-embeddable core and a glTF/viewer target. It covers what the core may link, the logging shim and its fatal semantics, handled environment failures, the caller-supplied file reader and embedded GLSL, the frame API and Eigen-free facade, the `Q3_COORDINATE` option, and the viewer's unchanged output.

### Modified Capabilities

None. `stock-check` gains its `cl_renderer sh` run in C2, not here. The smoke check is a separate script.

## Impact

- **sh-renderer:** `CMakeLists.txt`, the new `src/log.h`, `src/file_reader.{h,cpp}`, `src/frame.{h,cpp}`, `src/engine_api.{h,cpp}` and `cmake/embed_glsl.cmake`, plus `src/shader.{h,cpp}`, `src/scene.{h,cpp}`, the four `draw_*.cpp`/`compute_light_tile.cpp` program sites, every glog includer, `src/main.cpp`, `glsl/tonemap.frag`, `GEMINI.md`, and new tests (`log_test`, `file_reader_test`, `frame_test`).
- **ioq3:** `code/sdl/sdl_glimp.c`, `code/renderercommon/tr_common.h`, the new `code/renderersh/*`, the new `cmake/renderer_sh.cmake`, `CMakeLists.txt`, and the `code/tools/sh-renderer` pointer.
- **This repo:** `tools/stockcheck/renderer_sh_smoke.py`, `docs/pbr-plan/pbr-plan.tex` and its PDF, `CLAUDE.md`, and the new specs.
- **Dependencies:** the module adds none at runtime. The core needs only OpenGL (through glad) and Eigen. glog leaves sh-renderer entirely; the viewer logs through the shim's stderr sink and keeps gflags, GLFW, tinygltf, tinyexr and stb_image.
- **Portability:** the core still builds with `-march=native` in Release, so `renderer_sh.so` isn't portable across machines. That is acceptable for the dev target and noted for later.
