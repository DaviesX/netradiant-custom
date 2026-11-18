# Proposal: Progress and cancel for sh-baker's bake call (B3)

## Why

In B5, q3map2 makes one `BakeSHLightMap` call for every luxel in a map, and that call can run for minutes. Today the call gives the caller no say while it runs. It prints its own `\r[Baking] N%` line to stdout from inside the library (`src/baker.cpp:130-138`, `:199`), and it cannot be stopped short of killing the process. q3map2 needs to show progress its own way, through its own printing, and needs a clean way to stop. Plan §4.2 item 3 asks for this: the single call already builds the BVH and light trees once, and it gains progress and cancel callbacks.

## What Changes

- `BakeConfig` (`baker.h`) gains three optional callbacks:
  - **Progress** reports `(done, total)` points. Calls never overlap, `done` only increases, there is at most one call per whole percent, and a bake that finishes ends with `(total, total)`.
  - **Cancel** is polled before each point in the bake loop, not before or during the ray-tracing setup. Its calls never overlap, and once one returns true it is never called again. After that, no new point starts, points already in progress finish, and the call returns.
  - **Done** is called once on the calling thread after the bake loop ends and before the call returns. It is called for every bake, including a bake of zero points and a cancelled bake.
  - An empty callback is ignored, and the call never throws.
- `BakeResult` gains `cancelled`, meaning a cancel was requested. A cancel is only seen before a point starts, so a cancelled result always has at least one point that was not baked, and a bake of zero points is never cancelled. After a cancel, a point that was baked holds the same result a full bake would give it. A point that was not baked keeps the not-baked marker, the same marker an invalid point has today.
- **The library stops writing progress to stdout.** The CLI (`main.cpp`) installs a progress callback that prints the same `\r[Baking] N%` text, and a done callback that prints the trailing newline, so the CLI looks the same as before.
- The progress count is moved to after each point finishes. Today it counts a point when it starts, so 100% is printed before the last points are done.
- **Bake results are unchanged.** A bake with no callbacks, or with callbacks that never cancel, gives byte-identical results and CLI output files.
- The work was proposed as one sh-baker PR (#63) from a branch off sh-baker `main` (`f4e4388`), separate from the B2b PR (#62). #62 merged first (`77f1b72`), and #63 was adjusted in review and merged on top of it as `b6364ba`. A direct commit on `main`, `ce5419a`, then added the done callback. The netradiant planning and landing work is on its own branch, `b3-progress-cancel`.

## Capabilities

### New Capabilities
- `sh-baker-bake-control`: how a caller observes and stops a running `BakeSHLightMap` call. This covers progress reports, cancellation, the state of results after a cancel, and the library staying silent on stdout.

### Modified Capabilities
None. "Baking caller-supplied points" in `sh-baker-q3map2-loader` still holds unchanged: the not-baked marker it defines is the one a cancelled bake leaves.

## Impact

- **sh-baker code:**
  - `src/baker.h`: the `BakeConfig` callbacks (`progress_fn`, `cancel_fn`, `done_fn`) and `BakeResult::cancelled`. It adds `<functional>` and `<cstddef>`, and stays free of TBB and Embree headers.
  - `src/baker.cpp`: the bake loop, with the callbacks handled by a `BakeObserver` class.
  - `src/main.cpp`: the CLI's progress printer and the done callback that ends its line.
- **sh-baker tests:** new tests in `src/baker_test.cpp`. Existing tests are unchanged. The gate also builds a small program with `-fno-rtti -fno-exceptions`, as q3map2 is built, links it against `libsh_baker.so`, which is built with RTTI, and runs it with the progress and cancel callbacks set.
- **q3map2 (B5, later):** it will pass a progress callback that prints through `Sys_Printf`, and a cancel callback if it wants one. Nothing changes in q3map2 now.
- **Unchanged:** the bake results, the CLI flags, the output files, the `BakeSHLightMap` signature, and the header constraints that q3map2 relies on.
- **Out of scope:**
  - Cancelling inside the Embree BVH build.
  - A Ctrl-C handler for the CLI.
  - Progress for the rasterization, dilation and saving stages.
  - Time estimates.
- **netradiant (landing, after merge):** bump the `tools/sh-baker` submodule, tick `[x] **B3**` in `CLAUDE.md`, add a "Done" note to plan §4.2 item 3 and rebuild the PDF, archive the change, and run the stock check.
