# Tasks

Paths in groups 1–5 are relative to `../ioq3-custom/sh-renderer` (the PR working copy). Paths in groups 6–7 are relative to `../ioq3-custom/ioq3`. Paths in group 8 are relative to this repo.

**Delivery** (design decision 1):
- sh-renderer gets four stacked PRs to `DaviesX/sh-renderer`: `c1/1-log-shim`, `c1/2-file-reader`, `c1/3-split`, `c1/4-frame-api`. Each branch is cut from the previous one, and PR 1's from `$BASE` = `cdfa844`.
- ioq3 gets two PRs to `DaviesX/ioq3`: `c1/a-context-request` from `685cc1a1`, and `c1/b-renderer-sh` after the sh-renderer stack is merged.
- This repo gets the plan PR (group 0, with these artifacts) and the landing PR (group 8) to `DaviesX/netradiant-custom` through the remote `fork`. Never push to `origin` in this repo.
- **Ask the user before every push and every PR** (CLAUDE.md). The user reviews and merges. Never merge, and never push to `main`/`master`. Run `clang-format -i` on whole files for every C++ file a sh-renderer PR adds or changes. ioq3 C files keep their existing style; `git clang-format` runs on changed lines only.
- Stack moves (rebases after review or merge) follow the procedure in the archived `sh-baker-q3map2-input` tasks ("Moving the stack", "After a merge", "If `main` moves"), with `DaviesX/sh-renderer` and `build/stack-tips.txt` in sh-renderer.

**sh-renderer gate.** Every sh-renderer PR passes this on its committed branch before push:
- every target builds in `build/` (Release);
- `./build/sh_renderer_test > build/tests.txt 2>&1` gives every test in `build/baseline/tests.txt` the same result, and every new test passes;
- the viewer readback of the third frame of `data/Sponza/scene.gltf` (1.2) is byte-identical to `build/baseline/frame.ppm`;
- `git diff -U0 $BASE -- 'src/*_test.cpp' | grep '^-[^-]'` shows only include-line changes, so no assertion was changed or deleted;
- `git status` is clean apart from `build/`.

Any readback difference stops the work. Find the PR that caused it, measure it, and report to the user before continuing. Never loosen the comparison.

## 0. Plan PR (this repo)

- [x] 0.1 Update `docs/pbr-plan/pbr-plan.tex` §3.1 and `CLAUDE.md` C1 with this change's decisions:
  - GLSL embedded in the module, with a `glsl/sh/` VFS override;
  - shader `r_gamma` and no hardware ramp;
  - environment failures at init set `cl_renderer opengl2`, write it with `writeconfig`, and end through `ri.Error(ERR_FATAL)`, with `ERR_DROP` kept for per-level failures;
  - `GLimp_Init` keeps its signature, and a new `GLimp_InitWithRequest` carries the context request (the plan says `GLimp_Init` "gains a parameter");
  - glad is loaded inside sh-renderer's core through a host-supplied resolver, so no engine TU includes `glad.h`;
  - the four-plus-two PR split;
  - the smoke check as C1's exit.

  Rebuild the PDF with `latexmk -pdf` in `docs/pbr-plan/`. Verify: the build succeeds with no undefined references, and `grep -n "ERR\\\\_DROP" docs/pbr-plan/pbr-plan.tex` shows the new wording in §3.1.
- [ ] 0.2 Run `openspec validate renderer-sh-module --strict` and verify that it passes. Then, after asking the user, branch `renderer-sh-module-plan` from `master`, commit the plan, `CLAUDE.md` and `openspec/changes/renderer-sh-module/`, push to `fork`, and open the PR. Record the PR number here.

## 1. sh-renderer baseline (local, no PR)

- [x] 1.1 `git fetch origin` and check that `origin/main` is still `cdfa844`. If it isn't, stop and ask the user. Build the baseline in a worktree at `cdfa844`: `cmake -DCMAKE_BUILD_TYPE=Release -B build/baseline -S .` and build it. Run `./build/baseline/sh_renderer_test > build/baseline/tests.txt 2>&1`. Verify that the file lists every test with its result.
- [x] 1.2 In the baseline worktree only, apply a local, uncommitted patch adding `--readback_frame N --readback_path P` to `main.cpp`: `glReadPixels` of framebuffer 0 before the swap of frame N, a binary PPM written to P, then exit. The flags and output format must be the same as the ones PR 4 commits. Record `build/baseline/frame.ppm` with `--input data/Sponza/scene.gltf --width 640 --height 360 --readback_frame 3` (a tracked scene; the third presented frame, as the spec says). Verify: two runs give byte-identical files (`cmp`). Keep the patch as `build/baseline/readback.patch`.

## 2. sh-renderer PR 1: logging shim

- [x] 2.1 Add `src/log.h`/`src/log.cpp` (design decision 2): `LogSink {info/warning/error, fatal}`, `SetLogSink`, `LOG(INFO|WARNING|ERROR|FATAL)`, `LOG_EVERY_N`, `CHECK`, `CHECK_EQ/NE/LT/LE/GT/GE`, and `DCHECK`, compiled out under `NDEBUG`. The default sink writes to stderr and its fatal handler aborts. Add `src/log_test.cpp`, covering: messages reach an installed sink with severity, file and line; `CHECK` failure calls the fatal handler and never returns (death test); the default sink aborts (death test); `LOG_EVERY_N` fires on the 1st, N+1th, ... call; `DCHECK` doesn't evaluate under `NDEBUG`. Verify: `log_test` passes.
- [x] 2.2 Replace `#include <glog/logging.h>` with `#include "log.h"` in every source. Remove `google::InitGoogleLogging` and `FLAGS_logtostderr` from `main.cpp`, and drop glog from `CMakeLists.txt`. Verify: `grep -rn glog src CMakeLists.txt` is empty, and the gate passes.
- [ ] 2.3 Commit "C1 (1/4): replace glog with a logging shim", then run the gate. Ask the user, then push and open the PR, citing the `sh-renderer-core-library` "Logging shim" requirement and the gate evidence. Record the PR number and tip.

## 3. sh-renderer PR 2: file reader, embedded GLSL, recoverable program creation

- [x] 3.1 Add `src/file_reader.{h,cpp}`: `FileReader`, `MakeDiskReader(root)`, `ChainReaders`, a process-wide default reader (initially a disk reader at the working directory), and a served-path record (design decision 4). Port `shader.cpp`'s `ReadFile` and `ExpandIncludes` onto it with forward-slash string paths. Keep the public `ResolveShaderIncludes(source, base_dir)` signature as a wrapper over a disk reader at `base_dir`, and add a reader overload beside it. `ShaderProgram::Create*` take an optional reader and default to the process-wide one. Add `src/file_reader_test.cpp`, covering: the disk reader reads, and returns empty on a missing file; chain fallback; an in-memory reader resolving nested and relative `#include`s with no file system access; the depth limit. Keep the existing `shader_test` cases passing unchanged. Verify: tests pass.
- [x] 3.2 Add `cmake/embed_glsl.cmake` and the `SH_RENDERER_EMBED_GLSL` option (default OFF), generating `embedded_glsl.cpp` from `glsl/*.{vert,frag,comp,glsl}` (not `.import`), plus `MakeEmbeddedReader()`. Add a test, built only with the option ON: every `glsl/` file is in the table byte-for-byte, and a program source assembled through the embedded reader from a temporary working directory equals the disk-assembled source. Verify: configure with `-DSH_RENDERER_EMBED_GLSL=ON` in `build/embed` and the test passes, and the default build still passes the gate.
- [x] 3.3 Make the four `LOG(FATAL)` program sites (`draw_radiance.cpp:56`, `draw_sky.cpp:19`, `draw_tonemap.cpp:20`, `compute_light_tile.cpp:19`) return an empty program after `LOG(ERROR)` with the file names and the info log. The viewer's existing all-programs check (`main.cpp`) then returns with its error. Add `src/shader_gl_test.cpp`, which creates a hidden GLFW 4.6 context, builds a deliberately broken program through an in-memory reader, and checks that it gets an empty optional with the info log captured by a test sink. It needs a GL context, so it is linked into the test binary that has GLFW (today's `sh_renderer_test`; after PR 3, the glTF-side binary, 4.2). Verify: the gate passes and the new test passes.
- [ ] 3.4 Commit "C1 (2/4): file reader, embedded GLSL, recoverable program creation", run the gate, then ask, push and open the PR, stacked on PR 1. Record it.

## 4. sh-renderer PR 3: CMake split

- [x] 4.1 Move `LoadLightmaps` and `ComputeLightRadius` out of `scene.cpp` into a new `src/gltf_scene_io.{h,cpp}` in the glTF target, together with the `tinyexr.h` include. Update `loader.cpp` and the tests' includes. Remove `#include <embree4/rtcore.h>` from `scene.h`. Verify: `grep -n "tinyexr\|embree" src/scene.* ` is empty, and the gate passes.
- [x] 4.2 Split `CMakeLists.txt`:
  - `sh_renderer_core`: STATIC, `POSITION_INDEPENDENT_CODE ON`. It holds camera, cascade, compute_light_tile, culling, the draw passes, glad, render_target, scene, shader, ssbo, log and file_reader, and links only OpenGL and Eigen.
  - `sh_renderer_gltf`: holds loader, gltf_scene_io, implementations, window, input and interaction, and links the core plus tinygltf, tinyexr, stb_image, nlohmann_json, GLFW, gflags, ZLIB and Threads.
  - `sh_renderer_main` links `sh_renderer_gltf`.
  - Tests are split as `sh_renderer_core_test` (camera, cascade, culling, scene, shader, log, file_reader: no GL context) and `sh_renderer_test` (loader, loader_layers, window, input, interaction, shader_gl, and frame from PR 4: everything that needs GLFW or a context).
  - `SH_RENDERER_BUILD_TESTS` (default ON) gates `find_package(GTest)` and both test binaries.
  - `SH_RENDERER_BUILD_GLTF` (default ON) gates the second half and its `find_package` calls.
  - `SH_RENDERER_Q3_COORDINATE` (default OFF) adds `Q3_COORDINATE` to the core's compile definitions.

  Update the gate to run both test binaries. Verify:
  - `cmake -B build/embedded -DSH_RENDERER_BUILD_GLTF=OFF -DSH_RENDERER_BUILD_TESTS=OFF` configures with no `GTest_DIR` in its cache, and `cmake --build build/embedded --target help` lists only the core library (and the GLSL generator when embedding is on);
  - `cmake -B build/coreonly -DSH_RENDERER_BUILD_GLTF=OFF` configures without looking for glog, gflags, glfw3, tinygltf or tinyexr (`grep` its `CMakeCache.txt` for their `_DIR`/`_INCLUDE_DIR` entries finds none), and builds the core and core tests;
  - `cmake --build build -- VERBOSE=1` with `-DSH_RENDERER_Q3_COORDINATE=ON` shows `-DQ3_COORDINATE` on core TUs only;
  - the gate passes.
- [x] 4.3 Update `GEMINI.md`: Phase 0 build describes the two targets, their options and what each may link; the logging shim replaces glog in the technology stack; GLFW is the viewer's only. Verify: `grep -n "glog\|SHARED" GEMINI.md` has no stale claims.
- [ ] 4.4 Commit "C1 (3/4): split core library from the glTF target", run the gate, then ask, push and open the PR, stacked on PR 2. Record it.

## 5. sh-renderer PR 4: frame API, facade, display gamma

- [x] 5.1 Add `src/frame.{h,cpp}` (design decision 5): `Renderer`, `CreateRenderer` (also enables `GL_FRAMEBUFFER_SRGB`), `BeginFrame` (resize, `false` on 0×0), `RenderScene(renderer, scene*, camera, time_seconds)` (clears the HDR target to `clear_color`, then today's pass sequence when a scene is given; makes the spot shadow atlas from `scene->shadow_atlas.resolution` on the first scene and remakes it when the resolution changes), `EndFrame` (tonemap into framebuffer 0, no swap), `DestroyRenderer`. Move the size-dependent targets and programs out of `main.cpp`; the viewer passes `glfwGetTime()` as the time. The viewer loop becomes input, `BeginFrame`, `RenderScene`, `EndFrame`, swap. The two `CHECK_GT`s at `main.cpp:126-127` go, because `BeginFrame` handles 0×0. Commit the `--readback_frame/--readback_path` flags exactly as in 1.2's patch. Verify: the gate's readback is byte-identical.
- [x] 5.2 Add `u_display_gamma` to `glsl/tonemap.frag` (design decision 8) and `Renderer::display_gamma` (default 1). Add `--display_gamma` to the viewer. Verify: the gate's readback at the default is byte-identical; and in `frame_test` (5.3), for clear colours spanning the curve (0, 0.002, 0.05, 0.25, 1, 4, 20) at gamma 1.5 and 2.2, every byte is within 1 of round(255·e^(1/g)), with e the float sRGB encoding of the float tonemap of the clear colour. The test computes the expectation in C++, not from a gamma-1 byte.
- [x] 5.3 Add `src/engine_api.{h,cpp}`: the opaque `ShRenderer`, `ShHostServices` (reader, log sink, GL address resolver, `debug_gl`), `ShCreate` (loads glad through the resolver, checks GL ≥ 4.6, installs the debug callback when asked), `ShBeginFrame`, `ShEndFrame`, `ShSetClearColor`, `ShSetDisplayGamma`, `ShReadPixels`, `ShDestroy`, and a last-error string for create failures. The header includes only standard headers. Add `src/frame_test.cpp` in the glTF test binary, because it needs GLFW for a hidden 4.6 context. It checks that `ShCreate` (with `glfwGetProcAddress` as resolver) → `ShBeginFrame(64, 64)` → `ShEndFrame` with clear colour (0.25, 1, 4), read back through `ShReadPixels`, is within 1 of the expected tonemapped sRGB bytes; that `ShBeginFrame(0, 0)` returns false; that create through a reader missing `tonemap.frag` fails with the error naming it; and the gamma cases of 5.2. Verify:
  - the tests pass;
  - `g++ -std=c++20 -fsyntax-only -x c++ src/engine_api.h` with no Eigen include path succeeds.
- [ ] 5.4 Commit "C1 (4/4): frame API, engine facade and display gamma", run the gate, then ask, push and open the PR, stacked on PR 3. Record it. Wait for the stack to merge before group 7. Group 6 may proceed meanwhile.

## 6. ioq3 PR A: context request

- [x] 6.1 In `code/sdl/sdl_glimp.c` and `code/renderercommon/tr_common.h`, add `glimpContextRequest_t`, `GLimp_InitWithRequest` (returns `qboolean`) and `GLimp_GetDrawableSize(int *w, int *h)` (design decision 6). `GLimp_Init(fixedFunction)` calls the shared static function with no request, through code paths unchanged from today. Verify: `git diff` shows no change inside the no-request branch of the context list; renderergl1/renderergl2 sources are untouched (`git diff --stat code/renderergl1 code/renderergl2` is empty); a default ioq3 build succeeds.
- [x] 6.2 Verify that opengl1 and opengl2 still get the same context: run `tools/stockcheck/stockcheck.py content/benchmark bench --engine <new build>` from this repo. It passes, and both runs' GL version, renderer and profile lines in the qconsole logs match a run with the pre-change engine binary (`diff` of those lines is empty).
- [ ] 6.3 Ask, then push `c1/a-context-request` and open the PR to `DaviesX/ioq3` with the stockcheck evidence. Record it.

## 7. ioq3 PR B: renderer_sh

- [ ] 7.1 Bump `code/tools/sh-renderer` to the merged sh-renderer `main` that holds PRs 1–4. Rebase onto PR A, or wait for it to merge. Verify: `git submodule status` shows the merged commit.
- [x] 7.2 Add `cmake/renderer_sh.cmake` and `option(BUILD_RENDERER_SH ... OFF)` in `CMakeLists.txt`. With the option on (and `USE_RENDERER_DLOPEN` required), it should:
  - `add_subdirectory(code/tools/sh-renderer ... EXCLUDE_FROM_ALL)` with `SH_RENDERER_BUILD_GLTF=OFF`, `SH_RENDERER_BUILD_TESTS=OFF`, `SH_RENDERER_EMBED_GLSL=ON` and `SH_RENDERER_Q3_COORDINATE=ON`;
  - build `renderer_sh` from `code/renderersh/*`, `${SDL_RENDERER_SOURCES}`, `${DYNAMIC_RENDERER_SOURCES}` and `sh_renderer_core`;
  - bake the submodule's `git describe --always --dirty` into a definition.

  Verify: a default configure has no `renderer_sh` target and `sh-renderer` isn't configured; with the option on, the cache has no `GTest_DIR` and `cmake --build build --target help` lists no sh-renderer test or viewer target; `-DBUILD_RENDERER_SH=ON` builds `build/Release/renderer_sh.so`; `nm -D` on it shows `GetRefAPI` and no glog, glfw or tinygltf symbols.
- [x] 7.3 Add `code/renderersh/` (design decision 7; no glue TU includes `glad.h`):
  - `sh_main.cpp`: cvars, `glConfig`, `GetRefAPI`, `BeginRegistration`, `BeginFrame`, `EndFrame` and `Shutdown`, with init through `GLimp_InitWithRequest` (4.6 core, debug, sRGB), then `ShCreate` with `SDL_GL_GetProcAddress` as resolver (glad and the 4.6 check live in the core). Init failure follows design decision 3: `ri.Cvar_Set("cl_renderer", "opengl2")`, `ri.Cmd_ExecuteText(EXEC_NOW, "writeconfig q3config.cfg\n")`, then `ri.Error(ERR_FATAL, ...)`. The `g_shFatal` guard (design decision 2): the sink's fatal handler sets it before `ri.Error`; with it set, `Shutdown` only calls `GLimp_Shutdown`, and every other entry point returns at once.
  - `sh_host.cpp`: the log sink, and the VFS reader mapping `glsl/` to `glsl/sh/` over `ri.FS_ReadFile`, chained to the facade's `ShEmbeddedReader()`, recording the overrides it served for the whole-renderer override fallback (design decision 4).
  - `sh_stubs.cpp`: every other `refexport_t` entry, returning neutral results.
  - `sh_screenshot.cpp`.

  `r_gamma` is read each frame into `ShSetDisplayGamma`; `r_shClearColor` (default `0 0 0`) feeds `ShSetClearColor`; `r_shDebugGL` (latched, default 0) is passed to `ShCreate` as `debug_gl`; `glConfig.deviceSupportsGamma = qfalse`. Verify: the build succeeds with `-Wall` and no new warnings in `code/renderersh`; `grep -rn "Eigen\|glad\|FS_FileExists\|fopen\|ifstream" code/renderersh` is empty.
- [ ] 7.4 Manual checks on the dev machine, recorded in the PR description:
  - main menu for 300+ frames with no renderer console spam;
  - `vid_restart` twice, with `SDL_VIDEODRIVER=wayland`: under SDL's default x11 driver on XWayland/NVIDIA every renderer, stock ones included, crashes with a Present BadValue on restart (pre-existing, design.md Risks); record that in the PR description;
  - minimise and restore;
  - a loose `glsl/sh/tonemap.frag` override is named in the console, and a broken one rebuilds the whole renderer from the built-in GLSL with a warning naming every dropped override;
  - `MESA_GL_VERSION_OVERRIDE=4.5` (or an equivalent forced-low context) produces the fatal message, leaves `seta cl_renderer "opengl2"` in `q3config.cfg`, and leaves `qconsole.log` flushed with the message; a second start with the same homepath and no `cl_renderer` argument loads opengl2;
  - a deliberately broken embedded program (a temporary local build) gives the same fatal path;
  - a forced `CHECK(false)` in `ShBeginFrame` (a temporary local build) ends through the fatal path with the check's location in `qconsole.log`, no `recursive error` line, and the desktop mode restored when run fullscreen.
- [ ] 7.5 Ask, then push `c1/b-renderer-sh` and open the PR to `DaviesX/ioq3`, citing the `renderer-sh-module` requirements, the 7.4 results, and the smoke check output from 8.1. Record it.

## 8. Smoke check, docs and landing (this repo)

- [x] 8.1 Add `tools/stockcheck/renderer_sh_smoke.py` (design decision 9) and a section in `tools/stockcheck/README.md` describing it, its options (`--engine`, `--q3base`, `--out`) and its rows. Every run passes `+set com_introPlayed 1`, and the expected bytes are computed in floating point from the clear colour (Uncharted2, exact sRGB encode, 255·e^(1/g)), never from the gamma-1 screenshot. Verify:
  - against the PR B build it prints PASS for load, gamma 1 and gamma 1.5, and exits 0;
  - against a build without `BUILD_RENDERER_SH` it exits non-zero with "renderer_sh did not load";
  - with `r_shClearColor` forced to another value through `--extra "+set r_shClearColor 0 0 0"`, its pixel row fails;
  - the README's command runs as written.
- [ ] 8.2 Update `../ioq3-custom/CLAUDE.md` (a workspace file, not in any repo): drop the C0 deletion text; describe the two sh-renderer targets, `BUILD_RENDERER_SH`, the smoke check, embedded GLSL with the `glsl/sh/` override, and shader gamma. Mark C1 done in this repo's `CLAUDE.md`, and give the smoke command. Verify: `grep -n "C0" ../ioq3-custom/CLAUDE.md` shows no claim that the glTF path is deleted.
- [ ] 8.3 Run `tools/stockcheck/stockcheck.py content/town town1` against the PR B engine build. Verify that it passes, so opengl1/opengl2 are unaffected with the module present. Then sync the specs, archive the change with `openspec archive renderer-sh-module`, and, after asking, open the landing PR to `DaviesX/netradiant-custom` through `fork` with the smoke script, docs and archive. Record it.
