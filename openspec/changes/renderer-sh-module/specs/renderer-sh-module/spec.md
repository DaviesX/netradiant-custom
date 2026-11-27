# Spec Delta

## Purpose

`renderer_sh` is the enhanced target's ioquake3 renderer module, sh-renderer behind the engine's `refexport_t`, loaded by `cl_renderer sh` beside the untouched `opengl1` and `opengl2`. This capability covers how it loads, the context it needs, how it uses the engine's services and how it fails.

## ADDED Requirements

### Requirement: Opt-in build beside the stock renderers
The ioq3 build SHALL produce `renderer_sh` only when `BUILD_RENDERER_SH` is on (default off), as a dynamically loaded module next to `renderer_opengl1` and `renderer_opengl2`. With the option off, the build's targets and outputs SHALL be those of today. Enabling it SHALL NOT change how `opengl1` or `opengl2` are compiled.

#### Scenario: Default build unchanged
- **WHEN** ioq3 is configured and built with default options
- **THEN** no `renderer_sh` target exists, and `renderer_opengl1`/`renderer_opengl2` are built from the same sources and flags as before this change

#### Scenario: Module built on request
- **WHEN** ioq3 is configured with `-DBUILD_RENDERER_SH=ON` on a machine with the sh-renderer submodule checked out
- **THEN** `renderer_sh` is written to the build's output directory next to the other renderer modules

#### Scenario: No sh-renderer tests in the engine build
- **WHEN** ioq3 is configured with `-DBUILD_RENDERER_SH=ON` on a machine without GoogleTest, glog, gflags, GLFW, tinygltf or tinyexr
- **THEN** configuration succeeds, and no sh-renderer test, viewer or glTF target is configured or built

### Requirement: Stock renderers request the same context
The shared SDL GL setup SHALL let one module request a specific context, and SHALL give modules that don't request one exactly the context attempts, attributes and fallbacks they get today.

#### Scenario: opengl2 context unchanged
- **WHEN** ioquake3 starts with `cl_renderer opengl2` on a machine where it ran before this change
- **THEN** the console's GL version, renderer and extension lines and the context profile match a run of the pre-change engine on that machine

### Requirement: Loads under cl_renderer sh
With `cl_renderer sh`, the engine SHALL load `renderer_sh` through `GetRefAPI`, and the module SHALL print a banner line starting `renderer_sh:` naming its sh-renderer commit and the GL version it got. A run that fell back to `opengl2` SHALL be distinguishable from a successful load by its console alone.

#### Scenario: Successful load
- **WHEN** ioquake3 starts with `+set cl_renderer sh` and a `renderer_sh` module present
- **THEN** `qconsole.log` contains the `renderer_sh:` banner with a GL version of at least 4.6, and no `failed:` line from the renderer loader

#### Scenario: Module missing
- **WHEN** ioquake3 starts with `+set cl_renderer sh` and no `renderer_sh` module present
- **THEN** the log has the loader's `failed:` line and no `renderer_sh:` banner (the engine continues on `opengl2`)

### Requirement: OpenGL 4.6 core context
`renderer_sh` SHALL request exactly one context: OpenGL 4.6 core profile, debug, with an sRGB-capable default framebuffer. It SHALL run only when it gets a context of at least 4.6 core.

#### Scenario: Context obtained
- **WHEN** the module initialises on a driver offering OpenGL 4.6 core
- **THEN** the context reports version 4.6 or higher, core profile, and `GL_FRAMEBUFFER_SRGB` is enabled for the default framebuffer

#### Scenario: Driver below 4.6
- **WHEN** no 4.6 core context can be created
- **THEN** the module sets `cl_renderer` to `opengl2`, writes the configuration so `q3config.cfg` holds `seta cl_renderer "opengl2"`, and ends with a fatal engine error whose message says `renderer_sh` needs OpenGL 4.6 core and that the next start uses `opengl2`; the display mode and gamma are restored and `qconsole.log` holds the message

#### Scenario: Next start after the fallback
- **WHEN** ioquake3 is started again with the same homepath and no `cl_renderer` on the command line
- **THEN** it loads `opengl2`

### Requirement: Complete renderer interface
`renderer_sh` SHALL fill every entry of `refexport_t` with a function that is safe to call at any time after `GetRefAPI`, and SHALL report `REF_API_VERSION`. Entries with no behaviour yet SHALL return neutral results (zero handles, no fragments, everything in PVS, no light at a point, no entity tokens) and SHALL NOT print per call.

#### Scenario: Main menu runs
- **WHEN** the engine runs the UI VM at the main menu under `cl_renderer sh` for 300 frames
- **THEN** no crash, no fatal error and no per-frame console output from the renderer occurs

#### Scenario: vid_restart
- **WHEN** `vid_restart` is issued under `cl_renderer sh`
- **THEN** the module shuts down, re-creates its window, context and GPU resources, and keeps rendering frames

### Requirement: Logging through the engine console
Messages from `renderer_sh` and its sh-renderer core SHALL go to the engine console (and so to `qconsole.log`), with warnings and errors marked as warnings. A failed invariant check or fatal log SHALL end the program through the engine's fatal error path, not through `abort()`.

#### Scenario: Invariant failure
- **WHEN** an invariant `CHECK` fails inside the core while the module is running
- **THEN** the engine shuts down through its fatal error path: `qconsole.log` holds the check's message and source location, the display mode and hardware gamma are restored, and the mouse is released

#### Scenario: Shutdown after a fatal error
- **WHEN** the engine's fatal path calls the renderer's shutdown after a failed `CHECK` inside the core
- **THEN** the shutdown makes no call into the core, the log has no `recursive error` line, and the display mode is restored

### Requirement: Environment failures are not invariant failures
A condition the user's environment causes SHALL be handled or reported, never treated as a broken invariant. A zero-sized drawable (minimised window) SHALL skip the frame. A GLSL program the driver rejects SHALL be reported with its info log. When the program is the module's built-in copy, the result SHALL be the same as a driver below 4.6.

#### Scenario: Minimised window
- **WHEN** the window is minimised so the drawable is 0×0
- **THEN** frames are skipped without error, and rendering resumes when the window is restored

#### Scenario: Built-in program rejected
- **WHEN** the driver rejects a built-in GLSL program at initialisation
- **THEN** the console shows the program's name and info log, `cl_renderer` is set to `opengl2` and written to `q3config.cfg`, and the engine ends with a fatal error saying the machine can't run `renderer_sh`

### Requirement: GLSL built in, overridable through the VFS
The module SHALL carry its GLSL sources built in, so it needs no file beside it to run. For each GLSL file, including `#include`d ones, it SHALL first read `glsl/sh/<name>` through the engine's file system (loose or packed). If that file exists, it SHALL be used in place of the built-in copy, and the console SHALL name it once. If building the renderer fails while any override was used, the module SHALL print a warning that gives the failure (with the driver's info log) and names every override used as dropped, and SHALL then rebuild the whole renderer, every program, from the built-in copies alone. If that rebuild fails too, the result SHALL be the same as a built-in program rejected. A module built without the GLSL built in SHALL refuse to start with a fatal engine error.

#### Scenario: No override
- **WHEN** the module starts with no `glsl/sh/` files in the search path
- **THEN** every program is built from the built-in sources and no file-not-found message is printed

#### Scenario: Override used
- **WHEN** `glsl/sh/tonemap.frag` is present as a loose file in the homepath game directory
- **THEN** the tonemap program is built from it, and the console names the override

#### Scenario: Broken override
- **WHEN** `glsl/sh/tonemap.frag` exists but doesn't compile
- **THEN** the console shows the info log and a warning naming `glsl/sh/tonemap.frag` as dropped, the renderer comes up built from the built-in copies, and the frame renders

#### Scenario: Broken override drops the others
- **WHEN** a broken `glsl/sh/tonemap.frag` and a valid `glsl/sh/ssao.frag` are both present
- **THEN** the warning names both `glsl/sh/tonemap.frag` and `glsl/sh/ssao.frag` as dropped, and every program, the SSAO one included, is built from the built-in copies

### Requirement: Engine services
The module SHALL read every file through `ri.FS_ReadFile` and never through `ri.FS_FileExists`, which searches no pk3. Its configuration SHALL come from cvars through `ri.Cvar_Get`. It SHALL NOT open files through the C or C++ standard library or the OS.

#### Scenario: Packed asset found
- **WHEN** a `glsl/sh/` override is present only inside a pk3 in the search path
- **THEN** the module uses it

### Requirement: Display gamma in the shader
`renderer_sh` SHALL report that it has no hardware gamma and SHALL never set a hardware gamma ramp. It SHALL apply `r_gamma` g in its final pass, before quantisation: with e ∈ [0, 1] the unrounded sRGB encoding of the tonemapped colour, the displayed byte SHALL be 255·e^(1/g) rounded, within 1. At g = 1 the output SHALL be unchanged. A change to `r_gamma` SHALL take effect on the next frame. `r_overBrightBits` and `r_ignorehwgamma` SHALL have no effect.

#### Scenario: Default gamma
- **WHEN** a frame is rendered with `r_gamma 1`
- **THEN** the displayed bytes equal the sRGB encoding of the tonemapped colour

#### Scenario: Raised gamma
- **WHEN** a frame of known linear colour (the Cleared frame) is rendered with `r_gamma 1.5`
- **THEN** each displayed byte is within 1 of 255·e^(1/1.5), with e the unrounded sRGB encoding of that colour's tonemapped value

### Requirement: Cleared frame
Until world rendering exists, each frame SHALL clear sh-renderer's HDR target to the linear colour in the cvar `r_shClearColor` (three floats, default `0 0 0`), resolve it through sh-renderer's tonemap to the window, and present it at `GLimp_EndFrame`.

#### Scenario: Known colour on screen
- **WHEN** the main menu is shown with `r_shClearColor "0.25 1 4"` and `r_gamma 1`
- **THEN** every pixel of a screenshot is within 1 of sRGB(Uncharted2(c)/Uncharted2(11.2)) per channel for c = 0.25, 1, 4

### Requirement: Screenshot
The module SHALL implement the `screenshot [name]` console command, writing the presented frame as an uncompressed TGA to `screenshots/<name>.tga` through the engine file system, matching the stock command's path and default naming.

#### Scenario: Named screenshot
- **WHEN** `screenshot sc_test` is run under `cl_renderer sh`
- **THEN** `screenshots/sc_test.tga` appears in the homepath game directory, with the window's size and the frame's pixels

### Requirement: Smoke check proves the module
This repo SHALL provide one command that boots ioquake3 with `cl_renderer sh` and `com_introPlayed 1` in a fresh temporary basepath and homepath, holding only the stock paks, and exits 0 only if every check passes: the engine exited 0, the `renderer_sh:` banner is present, no loader `failed:` line, no fatal error, and screenshots at `r_gamma` 1 and 1.5 match the Cleared frame and Display gamma requirements.

#### Scenario: Fallback detected
- **WHEN** the smoke check is run against an engine build without `renderer_sh`
- **THEN** it exits non-zero and reports that the module didn't load

#### Scenario: Module passes
- **WHEN** the smoke check is run against an engine built with `BUILD_RENDERER_SH=ON` on a GL 4.6 machine
- **THEN** it prints PASS for the load, gamma 1 and gamma 1.5 rows and exits 0
