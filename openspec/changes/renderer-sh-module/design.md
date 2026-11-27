# Design

## Context

See proposal.md for the motivation. The current state, checked against sh-renderer `cdfa844` (main, the ioq3 submodule pin too) and ioq3 `685cc1a1`:

- **sh-renderer is one SHARED library** (`CMakeLists.txt:79-115`) compiled with `-O3 -march=native` (`:9-13`, directory-scoped `add_compile_options`). It links glog, gflags, GLFW, tinygltf, tinyexr, stb_image, nlohmann_json, ZLIB and Threads publicly. `implementations.cpp` defines the tinygltf, stb_image and tinyexr implementations. `scene.cpp:4` includes `tinyexr.h` for `LoadLightmaps` (`:875`). `ComputeLightRadius` (`:585`) serves only the glTF loader's light conversion. `scene.h:3` includes `<embree4/rtcore.h>` and uses nothing from it.
- **glog use is small**: 42 `LOG(ERROR)`, 17 `LOG(INFO)`, 8 `LOG(WARNING)`, 4 `LOG(FATAL)` (the program-creation sites), 1 `LOG_EVERY_N`, 4 `CHECK_GT` (two at `main.cpp:126-127`), 4 `DCHECK`. No test includes glog. gflags appears only in `main.cpp`.
- **Files**: `shader.cpp:15` reads with `std::ifstream`, and `ExpandIncludes` (`:67`, behind the public `ResolveShaderIncludes(source, base_dir)` at `:114`, which `shader_test` calls with a directory) resolves `#include` against `std::filesystem` paths. Program paths are literals like `"glsl/radiance.vert"`, resolved against the working directory.
- **Context and loop**: `window.cpp` creates a GLFW 4.6 core debug sRGB context and calls `gladLoaderLoadGL()` (`:98`). It enables `GL_FRAMEBUFFER_SRGB` (`:114`). `main.cpp` owns the loop, the targets and the per-frame resize; the spot shadow atlas is sized from the scene (`main.cpp:98-99`) and the radiance pass takes `glfwGetTime()` (`:201`). `DrawTonemap` binds framebuffer 0, clears, and writes Uncharted2(x)/Uncharted2(11.2) in linear, leaving the sRGB encode to the hardware.
- **ioq3**: every renderer module compiles `sdl_glimp.c` and `sdl_gamma.c` (`cmake/renderer_common.cmake:15-18`). `GLimp_Init(qboolean fixedFunction)` (`sdl_glimp.c:1024`) builds a context list (`:518-570`): GLES 2/3 if preferred, then core 3.2, then compat 2.0 for opengl2, or compat 1.1 for opengl1. It loads its own `qgl*` pointers (`:253`) and fills `glConfig`. `sdl_glimp.c` uses the renderer's `ri`, `glConfig` and `r_*` cvar globals declared in `tr_common.h`. `cl_main.c:3184-3200` falls back to `cl_renderer`'s reset value (`opengl2`) with `failed:` when the module doesn't load. `Com_Error(ERR_FATAL)` shuts down the client and exits without `longjmp`, while `ERR_DROP` longjmps to `abortframe` (`common.c:322-336`). A second `Com_Error` during that shutdown becomes `Sys_Error("recursive error ...")` (`:274`), which exits at once. `q3config.cfg` is written only at the top of `Com_Frame` (`:3064`), never on the fatal path; the renderer initialises from `Com_Init` before any frame runs.

## Goals / Non-Goals

**Goals:**
- After C1 no sh-renderer feature has to be touched again for engine reasons. C2+ only add.
- The viewer stays a working A/B tool for the glTF path, byte-identical where it was.
- opengl1/opengl2 are untouched in source and behaviour.

**Non-Goals:**
- No world, 2D, model or image loading. `RegisterShader`/`RegisterModel`/`DrawStretchPic`/`RenderScene` are neutral stubs. C2 fills them (2D included, §3.3).
- No `renderercommon` image decoders linked yet. They come in C2, which needs them.
- No `Q3_COORDINATE` behaviour. Only the option is added.
- No portable (non-`-march=native`) Release build.
- No `stock-check` integration. C2 adds the `cl_renderer sh` run.

## Decisions

1. **A stack of four sh-renderer PRs, then two ioq3 PRs.**
   - The sh-renderer PRs, each keeping the viewer's output byte-identical:
     1. the logging shim, replacing glog everywhere;
     2. the `FileReader`, embedded GLSL, and program creation returning failure;
     3. the CMake split and code moves, plus the `Q3_COORDINATE` option;
     4. the frame API, `engine_api.h`, display gamma, and the viewer moved onto the frame API.
   - The ioq3 PRs:
     - A: `GLimp_InitWithRequest`, which builds and runs on its own with gl1/gl2 unchanged;
     - B: `code/renderersh/`, `cmake/renderer_sh.cmake`, and the submodule bump to merged sh-renderer `main`.
   - Smaller PRs let the user review each while the next one is written, as with B2's stack. The split comes after the shim and reader because moving files is easier to review once their contents have stopped changing.
   - Alternative: one sh-renderer PR. It would be roughly 2k lines across every file, and too much to review.

2. **Logging shim (`log.h`, header plus a small `log.cpp`).**
   - The macros keep glog's syntax, so call sites change only their include line. `LOG(sev)` builds a `LogMessage` stream that hands `(severity, file, line, text)` to a global `LogSink` when it is destroyed.
   - `CHECK(c)` expands to `while (!(c)) LogMessageFatal(__FILE__, __LINE__, "Check failed: " #c " ").stream()`, and `CHECK_op(a, b)` to `while (std::unique_ptr<std::string> r = Check<op>Impl((a), (b), "a op b")) LogMessageFatal(__FILE__, __LINE__, *r).stream()`, where `Check<op>Impl` evaluates each operand once and returns null on success or the message with both values (`Check failed: a == b (1 vs. 3)`). The `while` form is a single statement with no dangling `if`, so it is safe in an unbraced `if`/`else`, and a streamed `<< x` is evaluated only on failure. `LOG_EVERY_N` is an `if (...) {} else LOG(sev)` over a per-site static counter, safe the same way. `LogMessageFatal`'s destructor is `[[noreturn]]`: it calls `sink.fatal(...)`, then `std::abort()` if the sink returns, so the loop never repeats. `log_test` checks the single evaluation (passing and failing) and the dangling-`else` safety. `DCHECK` compiles out under `NDEBUG` (`while (false) CHECK(c)`, compiled but not evaluated).
   - The default sink writes `E file:line] msg` to stderr, and its fatal handler aborts.
   - In the module, the sink prints INFO through `ri.Printf(PRINT_ALL)`, WARNING and ERROR through `PRINT_WARNING`, and FATAL through `ri.Error(ERR_FATAL, "%s:%d: %s")`.
   - `ERR_FATAL` doesn't longjmp, so no C++ frame is skipped. The client shutdown runs, restoring the mode and gamma and releasing input, and `Sys_Error` exits.
   - **No re-entry after fatal.** The fatal path calls `CL_ShutdownRef` → `re.Shutdown` while the failed core call is still on the stack. The sink's fatal handler sets the glue's `g_shFatal` flag before calling `ri.Error`. With the flag set, `Shutdown` makes no core call (GPU objects are left to process exit) and only runs `GLimp_Shutdown`, and every other entry point returns at once. Without the guard, a second check firing in `ShDestroy` would turn into `recursive error`, skipping the mode restore.
   - The core itself never raises `ERR_DROP`. Errors it can recover from come back as values, and only the engine glue calls `ri.Error`, from a frame with no live core objects. A longjmp across C++ frames with destructors is undefined.
   - Alternative: keep glog and install a custom `LogSink`. glog would still be a link dependency of the module, and its failure handler would still abort.

3. **Environment failures in the module are fatal with fallback, not `ERR_DROP`.**
   - The plan lists a rejected GLSL program among the "handled or `ERR_DROP`" cases. Programs are built at renderer init, though. An `ERR_DROP` there longjmps out of `CL_InitRenderer`, and the client then calls into a half-initialised renderer every frame. Since `cl_renderer` is archived, the next start fails the same way.
   - So, when there is no 4.6 context or a built-in program is rejected, the glue calls `ri.Cvar_Set("cl_renderer", "opengl2")`, then `ri.Cmd_ExecuteText(EXEC_NOW, "writeconfig q3config.cfg\n")`, then `ri.Error(ERR_FATAL, ...)`. The explicit `writeconfig` is needed because neither the fatal path nor `quit` writes the configuration, and no frame has run yet. The message says the next start uses opengl2. This is still the engine's orderly fatal path, not `abort()`, which is what the plan's change was about.
   - `ERR_DROP` is kept for per-level failures (C2's map load). The plan's §3.1 wording and `CLAUDE.md` C1 are updated to say this.
   - A 0×0 drawable is checked in the glue before `BeginFrame`, which also skips one on its own. The glue reads the size with `SDL_GL_GetDrawableSize` on the engine's window, through a small accessor `GLimp_GetDrawableSize` added in ioq3 PR A.

4. **`FileReader` and embedded GLSL.**
   - `using FileReader = std::function<std::optional<std::string>(std::string_view path)>`, with `MakeDiskReader(root)`, `MakeEmbeddedReader()` and `ChainReaders(primary, fallback)`.
   - `ShaderProgram::CreateGraphics/CreateCompute` gain a reader argument. A process-wide default reader keeps call sites unchanged: the viewer sets the disk reader at its working directory, as today, and the module installs its own chain (the VFS override, then the built-in copy) through `ShHostServices::reader`. The facade exposes the built-in copy as `ShEmbeddedReader()` (with `ShHasEmbeddedGlsl()`), so the glue builds that chain without including `file_reader.h` or `embedded_glsl.h`.
   - `ExpandIncludes` resolves names with forward-slash string paths relative to the including file, through the reader, so the logic is the same and only the I/O changes. The public `ResolveShaderIncludes(source, base_dir)` keeps its signature as a wrapper over a disk reader at `base_dir`, so `shader_test` is unchanged; a reader overload is added beside it.
   - `cmake/embed_glsl.cmake` turns `glsl/*.{vert,frag,comp,glsl}` into one generated `embedded_glsl.cpp`, a sorted `{path, contents}` table using raw string literals split under the compiler's limit. `SH_RENDERER_EMBED_GLSL` is ON for the module and OFF by default.
   - Alternatives:
     - A renderer pk3, or loose files: rejected by the user. A pk3 is one more install, sv_pure refuses it, and loose files are fragile.
     - ioq3's `stringify_shader.cmake`: it emits one C symbol per file, and `#include` needs a lookup by path.
   - **Override fallback (whole renderer).** The glue's VFS reader maps `glsl/x` to `glsl/sh/x`, so it can't shadow opengl2's `glsl/` overrides. The reader records each override it served, once, in the order first served. When `ShCreate` through the chained reader fails and any override was served, the glue warns, naming the failure (`ShLastError()`, with the driver's info log) and every served override as dropped, then calls `ShCreate` again with the embedded reader alone, so **every** program is rebuilt from the built-in GLSL, not only the one that failed (chosen by the user: the core builds its programs as one unit in `CreateRenderer`, and a mix of overridden and built-in programs that share `#include`s could disagree). If that second create fails too, or the first failed with no override served, it is decision 3's fatal-with-fallback. The glue also refuses to start (`ERR_FATAL`) when `ShHasEmbeddedGlsl()` is false, since the module would then depend on loose files.

5. **Frame API (`frame.{h,cpp}`) and the facade (`engine_api.{h,cpp}`).**
   - `Renderer` (in the core) owns the size-dependent targets now created in `main.cpp` (depth/normal, HDR, SSAO, tile lists), the sun cascades, the programs, `clear_color` and `display_gamma`. `CreateRenderer(std::string* error)` enables `GL_FRAMEBUFFER_SRGB` once every program and resource is built, so every host gets the sRGB encode, not just the viewer's `window.cpp:114` (which keeps its own call, harmlessly), and a failed create leaves the host's GL state alone. On failure `*error` lists one line per failed program, `<name> (<files>): <reason>`, the reason being the missing file or the driver's info log; the 11 program factories take an optional `std::string* error` for this and still `LOG(ERROR)` the same text.
   - Scene-sized resources, today only the spot shadow atlas (`scene->shadow_atlas.resolution`), are made by `RenderScene` the first time it gets a scene, and remade when the resolution changes. With no scene, none exist.
   - `BeginFrame(renderer, width, height)` resizes the targets and returns `false` for a zero size. `RenderScene(renderer, scene*, camera, time_seconds)` clears the HDR target to `clear_color` and, with a scene, runs today's passes in today's order; the host supplies the time (`glfwGetTime()` in the viewer, `ri.Milliseconds()/1000` in the module, unused until C2). `EndFrame` runs the tonemap into framebuffer 0, then resets the cheap state a host would trip over: no program, no vertex array, no texture on unit 0, `GL_DEPTH_TEST` off. It leaves framebuffer 0 bound, the frame's viewport and `GL_FRAMEBUFFER_SRGB` on; blend, cull, depth function and mask, colour mask and other bindings stay as the last pass set them, so a host that draws afterwards (C2's 2D and console) sets those itself. `RenderScene` sets the colour mask to all-true before clearing the HDR target. `SetDisplayGamma(renderer, g)` stores `SanitizeDisplayGamma(g)`: clamped to [`kMinDisplayGamma` 0.5, `kMaxDisplayGamma` 3], with a non-finite or non-positive g taken as 1 and one warning per process; `EndFrame` sanitises again in case code writes `display_gamma` directly.
   - The viewer's loop becomes input → `BeginFrame` → `RenderScene` → `EndFrame` → `glfwSwapBuffers`. That is how PR 4 keeps it byte-identical.
   - `engine_api.h` includes only `<cstdint>`, `<functional>`, `<optional>`, `<string>` and `<string_view>`, and declares:
     - `struct ShRenderer;` (opaque) and `enum class ShLogSeverity { kInfo, kWarning, kError }`;
     - `using ShFileReader = std::function<std::optional<std::string>(std::string_view path)>` (the core's `FileReader` shape, restated with standard types);
     - `bool ShHasEmbeddedGlsl()` and `ShFileReader ShEmbeddedReader()`: the built-in GLSL by `glsl/<name>`, or a reader that always misses in a build without `SH_RENDERER_EMBED_GLSL`;
     - `struct ShHostServices { ShFileReader reader; std::function<void(ShLogSeverity, const char* file, int line, const std::string&)> log; std::function<void(const char* file, int line, const std::string&)> fatal; void* (*get_proc_address)(const char*) = nullptr; bool debug_gl = false; }`: an empty reader keeps the core's default reader, an empty `log` goes to stderr, an empty `fatal` prints and aborts, and `fatal` should not return (the core aborts if it does); the resolver is required;
     - `ShRenderer* ShCreate(const ShHostServices&)` (null on failure) and `const std::string& ShLastError()`: why the last create failed (a version below 4.6, or `CreateRenderer`'s per-program lines with the info logs), empty after a success. A failed create restores the previous log sink and reader and leaves `GL_FRAMEBUFFER_SRGB` as it was;
     - `bool ShBeginFrame(ShRenderer*, int width, int height)` (false for a 0×0 drawable, and the frame is skipped) and `void ShEndFrame(ShRenderer*, double time_seconds = 0.0)`, which renders and tonemaps into framebuffer 0 without swapping and leaves the GL state described above;
     - `void ShSetClearColor(ShRenderer*, const float rgb[3])` and `void ShSetDisplayGamma(ShRenderer*, float gamma)`, the latter through `SetDisplayGamma`'s clamp;
     - `void ShReadPixels(int width, int height, uint8_t* rgb)`: framebuffer 0 (the back buffer, after `ShEndFrame` and before the swap) as packed RGB, bottom row first;
     - `void ShDestroy(ShRenderer*)`, with the context current; it restores the sink and reader `ShCreate` replaced, and null is allowed.
   - **glad lives in the core only.** `ShCreate` calls `gladLoadGL` with the host's resolver (`SDL_GL_GetProcAddress` in the module), checks the version is at least 4.6 (returning the error string otherwise), and installs the GL debug callback through the log sink when `debug_gl` is set. So no engine TU includes `glad.h`, and the conflict between `glad.h` and `qgl.h`/`SDL_opengl.h` can't arise. The screenshot reads pixels through `ShReadPixels` for the same reason.
   - Engine TUs include only `engine_api.h` from sh-renderer (the glue's `sh_local.h` includes it; no `file_reader.h`, `embedded_glsl.h`, `frame.h` or `glad.h`). Eigen types and `-march=native` stay inside the core's TUs. Eigen's `EIGEN_MAX_ALIGN_BYTES` differs between AVX and non-AVX builds, so sharing a struct across that boundary would break the ABI.
   - The core is a CMake static library with `POSITION_INDEPENDENT_CODE ON`, so it links into a `.so`.
   - `SH_RENDERER_Q3_COORDINATE` adds `Q3_COORDINATE` to the core as a **PUBLIC** compile definition, so the core's own TUs, its tests and every consumer (the engine glue) see the same layout and constants. With the option off, no TU gets it.

6. **Context request in `sdl_glimp.c`.**
   - `typedef struct { int major, minor; qboolean core, debug, srgb; } glimpContextRequest_t;`
   - `GLimp_InitWithRequest(const glimpContextRequest_t *req)` shares `GLimp_Init`'s body. `GLimp_Init(fixedFunction)` calls the same static function with `req = NULL`, and that path is byte-for-byte today's code.
   - With a request, the context list has exactly one entry (after `SDL_GL_ResetAttributes`). Before each attempt it sets `SDL_GL_CONTEXT_FLAGS` to `SDL_GL_CONTEXT_DEBUG_FLAG` and `SDL_GL_FRAMEBUFFER_SRGB_CAPABLE` to 1 (from `req->debug` and `req->srgb`; `renderer_sh` always sets both). The `r_mode` fallback attempts are kept, so a bad mode still falls back to a safe mode.
   - On total failure with a request, the function returns `qfalse` instead of calling `ri.Error`. The glue then does the fallback from decision 3.
   - `GLimp_GetProcAddresses(qfalse)` still fills `qgl*`, which `sdl_glimp.c` itself uses for `glConfig` strings. glad is loaded afterwards inside the core by `ShCreate` (decision 5), with `SDL_GL_GetProcAddress` passed in as the resolver.
   - **No brightness probe with a request.** The stock path tests hardware gamma by calling `SDL_SetWindowBrightness(SDL_window, 1.0f)`, which itself sets an (identity) ramp. With a request the probe is skipped (`deviceSupportsGamma = !req && !r_ignorehwgamma->integer && SDL_SetWindowBrightness(...) >= 0`), so `deviceSupportsGamma` is `qfalse` and no ramp is ever set, not even an identity one. The glue also sets `glConfig.deviceSupportsGamma = qfalse` after init (decision 8). The stock path keeps its probe unchanged.

7. **Engine glue layout (`code/renderersh/`).**
   - `sh_main.cpp`: `GetRefAPI`, the `refexport_t` table, the `r_*` cvar globals `sdl_glimp.c` needs, `glConfig`, and `ri`. Most of these are copied from renderergl1's `tr_init.cpp` registration.
   - `sh_stubs.cpp`: the neutral entries. `RegisterShader*` returns 0, `MarkFragments` returns 0, `inPVS` returns `qtrue`, `LightForPoint` returns `qfalse`, `GetEntityToken` returns `qfalse`, `LerpTag` returns an identity orientation, `ModelBounds` returns zero, `RegisterFont` returns a zeroed `fontInfo_t`, and the draw calls do nothing.
   - `sh_host.cpp`: `SH_MakeHostServices(useOverrides, debugGL)`, which fills `ShHostServices` with the log sink, the fatal handler (sets `g_shFatal`, then `ri.Error(ERR_FATAL)`), and the reader: with overrides, a lambda reading `glsl/sh/<name>` over `ri.FS_ReadFile`/`ri.FS_FreeFile` first and `ShEmbeddedReader()` second, naming each override in the console once and recording it (`SH_OverridesServed`, `SH_ServedOverrideList`); without, `ShEmbeddedReader()` alone. `sh_main.cpp` runs decision 4's whole-renderer fallback with these.
   - `sh_screenshot.cpp`: `screenshot [name]`, registered with `ri.Cmd_AddCommand`. It reads the back buffer through `ShReadPixels` after `ShEndFrame` and before `GLimp_EndFrame`, and writes it as a 24-bit bottom-up TGA via `ri.FS_WriteFile`. Like stock, it runs at the end of the frame the command was issued in.
   - The glue's own TUs are compiled with the engine's flags. They include `engine_api.h`, `tr_public.h`, `tr_common.h` and SDL, and never Eigen or `glad.h`.
   - `cmake/renderer_sh.cmake` adds the submodule with `add_subdirectory(... EXCLUDE_FROM_ALL)` and sets `SH_RENDERER_BUILD_TESTS=OFF` and `SH_RENDERER_BUILD_GLTF=OFF` before it, so no GoogleTest lookup, test or viewer target enters the engine build.
   - The banner is `renderer_sh: sh-renderer <git describe> GL <version> <renderer>`. The sh-renderer commit is baked in at configure time from the submodule.

8. **Display gamma in the tonemap, never a hardware ramp.**
   - Chosen by the user. `tonemap.frag` gains `uniform float u_display_gamma`. When it is ≠ 1, the shader encodes the tonemapped linear colour to sRGB exactly (e, unrounded), raises it to 1/g, and decodes back to linear. The framebuffer's sRGB encode then quantises 255·e^(1/g). Gamma acts on the unrounded e, not on the g = 1 byte, so checks compare with 255·e^(1/g) computed from a known colour (smoke check) or from the float tonemap of a known input; comparing with the g = 1 byte s would miss by more than 1 near black (s = 1 → 4…8 at g = 1.5).
   - At g = 1 a uniform branch skips all of this, so the output is bit-identical to today's.
   - The glue reads `r_gamma` each frame, so a change takes effect at once, as with `R_SetColorMappings`. It clamps g to [0.5, 3] itself, as renderergl1 does (anything below 0.5, including 0 and negative values, becomes 0.5), before `ShSetDisplayGamma`. The facade's own sanitising treats 0 and negative values differently (as 1, with a warning), so the glue's clamp stays and the facade only ever catches NaN from the module. `glConfig.deviceSupportsGamma = qfalse`, so neither the client nor `sdl_gamma.c` ever sets a ramp.
   - Alternatives:
     - A hardware ramp: no effect windowed on many SDL2 and Wayland setups, and left behind after a crash.
     - Ignoring `r_gamma`: users lose the control.

9. **The smoke check is its own script and reuses stockcheck's helpers.**
   - `tools/stockcheck/renderer_sh_smoke.py` imports `make_root`, the colour stripping and the `qconsole.log` handling from `stockcheck.py`. It makes a basepath with only `pak0–8`, and runs two engine processes (`r_gamma 1` and `1.5`), each with `+set cl_renderer sh +set com_introPlayed 1 +set r_shClearColor "0.25 1 4" ... +exec smoke.cfg` (as stockcheck does, so the intro cinematic never plays through the stubs) (`wait 100`, `screenshot sc_smoke`, `wait 20`, `quit`).
   - It checks the exit code, the banner line, that there is no `failed:` and no `ERROR:`, and every screenshot pixel against the expected bytes within ±1. It computes the expected bytes in floating point with the same Uncharted2 constants, the exact sRGB encode and 255·e^(1/g).
   - No map is loaded, since the main menu is enough. Alternative: a `stockcheck.py --renderer sh` mode. That needs a map and passes nothing a map would test until C2, which adds it there.

## Risks / Trade-offs

- **[Fatal-with-fallback rewrites a user cvar]** The module sets `cl_renderer` to opengl2 on a fatal environment failure. → The message says so, and the value is written with an explicit `writeconfig`. (The engine's own loader fallback persists only because frames run afterwards; the module can't rely on that.) The user sets `sh` again once the machine is fixed.
- **[`r_shClearColor` is a throwaway cvar]** → It is named for its purpose, documented as the C1 frame, and removed or repurposed (sky-less background) in C2. The smoke check moves to a map then.
- **[Byte-identity of the viewer depends on readback]** The viewer has no readback today. → PR 4 adds `--readback_frame N --readback_path out.ppm` to the viewer. The baseline gets the same flag as a local, uncommitted patch on `cdfa844`, used only to record `build/baseline/frame.ppm` from the third presented frame of the tracked `data/Sponza/scene.gltf`, a scene with no animated layers, so time doesn't enter the frame. The third frame lets the per-frame resize and shadow allocation settle; both the spec and the tasks use it.
- **[glad and qgl in one process]** Both load the same driver functions. → glad is confined to the core's TUs and `qgl*` to `sdl_glimp.c`; the symbol names differ (`glad_gl*` vs `qgl*`), and the glue's TUs include neither loader.
- **[`-march=native` in the module]** `renderer_sh.so` is tied to the build machine. → Accepted for the dev target, and recorded in the plan's Risks. A portable flag set (e.g. `-march=x86-64-v3`) is a later CMake option.
- **[Debug context cost]** A debug context can be slower. → The callback is installed only when `r_shDebugGL 1` (default 0, latched, passed to `ShCreate` as `debug_gl`). The context always carries the debug flag, as the plan specifies.
- **[`vid_restart` crashes under SDL's x11 driver on XWayland/NVIDIA]** On the dev desktop (XWayland, NVIDIA 580.178.04, RTX 4060 Ti) `vid_restart` dies at random with the X error `BadValue, Major opcode 145, Minor opcode 5`: opcode 145 is the Present extension, a request on the destroyed window's stale XID, sent by the driver's swap path and reported asynchronously inside SDL's window creation for the new one. It is pre-existing and not caused by `renderer_sh`: opengl1 and opengl2 in the engine before PR A die the same way, usually on the first restart (0 of 5 runs each survived it). Over 34 runs, removing the debug flag, the sRGB flag, both, or the core altogether (`ShCreate` skipped) never got `renderer_sh` through two restarts, so no context flag and no GL object lifetime is the cause; the glue's order (`ShDestroy` with the context current, then `GLimp_Shutdown`) was already right. Under `SDL_VIDEODRIVER=wayland` all three renderers survive two restarts. → No spec change: the context keeps 4.6 core, debug and sRGB always on. The manual `vid_restart` check (task 7.4) is run with `SDL_VIDEODRIVER=wayland`, and the PR description records the x11 crash as a platform issue.
- **[Context below 4.6 in VMs or CI]** → The smoke check reports the fallback and is run on the dev machine. CI integration waits for C2.

## Migration Plan

Nothing migrates. `BUILD_RENDERER_SH` defaults off. Each sh-renderer PR keeps the viewer byte-identical, so a revert of any PR is independent. ioq3 PR A is safe alone. PR B is reverted by turning the option off or reverting it.

## Open Questions

- Which sh-renderer `git describe` form the banner uses (`--always --dirty`) can be settled in PR B.
