# Design: Progress and cancel for sh-baker's bake call (B3)

## Context

The motivation is in proposal.md. The relevant code in sh-baker `main` (`f4e4388`), `src/baker.cpp` `BakeSHLightMap`:

- It sets up once: `rtcNewDevice`, then `BuildBVH`, then the full and area-only light trees.
- It runs one `tbb::parallel_for` over `blocked_range<size_t>(0, total_pixels)`, with the default partitioner. Each index is one point. A point whose `material_id < 0` is skipped and keeps `SHCoeffs(-1)` and visibility -1.
- Each point seeds its own RNG with `std::mt19937 rng(12345 + idx)`. A point's result therefore depends only on its index and the inputs, not on thread scheduling. This is what makes partial results after a cancel exact.
- At the top of each index, the loop does a `processed_count.fetch_add` and a CAS on `last_progress`, then writes `\r[Baking] N%` to `std::cout`. That counts points when they start, not when they finish. It is the library's only stdout output during a bake, apart from the final `std::endl`.
- The only caller in the repo is the CLI (`src/main.cpp:154`). q3map2 will be the second, in B5.
- `baker.h` must keep compiling under `-fno-exceptions -fno-rtti`, with no Embree, TBB, tinygltf or JSON headers. This is the "Headers usable from q3map2" requirement.

## Goals / Non-Goals

**Goals:**
- Let the caller show progress and stop the bake, with negligible cost and no change to the results.
- Keep the semantics simple for q3map2: its callbacks are plain functions, it does no locking of its own for progress, and it gets a clear meaning for a partial result.

**Non-Goals:**
- Cancelling inside or around the Embree BVH build. Embree's build progress monitor could support it, but the build takes a small share of a bake. Cancel is polled only before each point, so a cancel that is already requested when the call starts still builds the BVH and light trees, then stops at the first point.
- Running the progress and cancel callbacks only on the caller's thread. That would need a separate polling thread or task arena around a blocking `parallel_for`. Only `done_fn`, which runs after the loop, is always called on the caller's thread.
- Progress weighted by work per point: each point counts as 1, whatever its sample count.

## Decisions

### 1. The callbacks live in `BakeConfig`, as `std::function`

```cpp
struct BakeConfig {
  ...
  // Called with (points finished, total points). Calls never overlap; done
  // strictly increases, at most once per whole percent; a finished bake ends
  // with (total, total). Called from bake worker threads. May be empty.
  std::function<void(size_t done, size_t total)> progress_fn;
  // Polled before each point in the bake loop. Polls never overlap but may
  // come from different worker threads; after one returns true there are no
  // more. Returning true stops the bake (see BakeResult::cancelled). May be
  // empty.
  std::function<bool()> cancel_fn;
  // Called once on the calling thread after the bake loop, including for empty
  // or cancelled bakes, before BakeSHLightMap returns. May be empty.
  std::function<void()> done_fn;
  // Progress and cancel: keep them short; do not call back into sh-baker, run
  // TBB work, or block waiting on the bake.
};
struct BakeResult {
  ...
  // True when cancel_fn returned true; points not baked keep the not-baked
  // marker, and at least one point is not baked.
  bool cancelled = false;
};
```

The callbacks are used through a small `BakeObserver` class in `baker.cpp` (`Cancelled()`, `FinishOneTask()`, `Done()`, `WasCancelled()`).

- *Alternative: a fifth parameter to `BakeSHLightMap`.* That changes every call site and the B2 spec's wording. `BakeConfig` is already the per-call options struct, and adding fields keeps existing callers source-compatible.
- *Alternative: C function pointers with a `void* user`.* q3map2 is C++ and already includes C++20 headers from sh-baker. `std::function` works under `-fno-exceptions -fno-rtti` with GCC and Clang, which only drop `target_type()`. The header check in the gate confirms it.
- **Empty callbacks are tested before every call.** Calling an empty `std::function` throws `bad_function_call`, which would break the B2 rule that the q3map2-facing functions do not throw.
- *Alternative: a `const std::atomic<bool>*` for cancel.* That is simpler, but it forces the caller to own an atomic. A function covers it, since `[&flag]{ return flag.load(); }`, and also covers polling a signal or a timeout.

### 2. Progress counts finished points, through one reporter

After each point's result is written, or straight away for a skipped point, the worker calls `FinishOneTask()`. With no `progress_fn` it returns at once. Otherwise it runs `done = finished.fetch_add(1) + 1` and computes `percent = done * 100 / total`. If `percent` is above the last reported percent, the worker takes a `std::mutex` and checks again under the lock. If the check still holds, it records the percent and calls `progress_fn(done, total)` while holding the lock.

- Calls never overlap, because they happen under the mutex. `done` strictly increases between calls, because reported percents strictly increase and `done` is fixed before the lock.
- That gives at most 101 calls (0–100%; 0% can only be reported when `total > 100`). The 100% report is necessarily `done == total`, because `done * 100 / total == 100` only when `done == total`. So a finished bake ends with `(total, total)`.
- The common path, where the percent is unchanged, costs one relaxed `fetch_add` and one relaxed load. The mutex is taken at most about 101 times per bake.
- *Alternative: calling under a CAS on `last_percent` without a mutex,* as the current code does. Two winners of different CAS rounds can then run the callback at the same time, and could print out of order. The mutex is what makes calls serialized and in order.

### 3. Cancel latches, and polls are serialized

There is a `std::atomic<bool>` latch, `cancelled`, starting false, and a `std::mutex cancel_mutex`. One poll, `BakeObserver::Cancelled()`, does this:

1. If `cancel_fn` is empty, return false. No lock is taken and the latch is never set.
2. If the latch is set, return true without calling anything.
3. Lock `cancel_mutex`. If the latch is still clear, call `cancel_fn` and store the latch on true. Return the latch.

The bake polls in one place, at the start of each point, before the `material_id < 0` check. On true the worker `break`s out of its TBB range, so the rest of that range is neither polled nor baked. A range that starts later polls once at its first index, sees the latch, and breaks. A skipped point is not counted as done, invalid or not. So after a cancel, `done` rises only by the points already in progress as they finish, and no `(total, total)` report is made. There is no poll before `rtcNewDevice` or after `BuildBVH`. A cancel that is already true when the call starts still pays for the device, BVH and light trees, then stops at the first point.

When the loop ends, the bake calls `done_fn`, releases the BVH and the device, and returns `result.cancelled = cancelled`. Points already started finish normally, and their results equal a full bake's because of the per-index seeding. Skipped points keep the initial `SHCoeffs(-1)` and -1 that the result buffers are filled with. The buffers are sized from `raster_config` (`width * height * scale²`), as today, so a zero-point bake needs a raster width of 0.

- **Why serialize (review fix (a)).** If the latch were only checked before an unlocked call, thread B could start `cancel_fn` after thread A's call had returned true but before A stored the latch. The spec promises no call after a true. The mutex plus the re-check makes the store happen before any later call can begin. It also means the caller's callback is never run concurrently, so it does not need to be thread-safe.
- **Cost.** With a `cancel_fn`, one lock per point. That is tens of nanoseconds uncontended, and somewhat more under contention with many threads on very cheap points. Even a one-sample point traces at least one path and one shadow ray, so the lock is noise. With no `cancel_fn`, a poll is one empty-function test and takes no lock. No merged test runs a cheap-point bake with a `cancel_fn` set.
- **`cancelled` means a cancel was requested.** A poll comes only before a point, and a true skips that point, so a cancelled result always has at least one point with the marker. A bake of zero points makes no polls and is never cancelled. The caller can tell which points are missing from the markers.
- **Promptness.** Once the latch is set, the worker that saw it breaks out of its range, and the rest of that range is skipped without a poll. Every other worker breaks at its next point with one acquire load. The worst-case latency is one point's bake time per worker. Ranges TBB has not started yet still run, but each one stops at its first index. `tbb::task_group_context::cancel_group_execution()` could drop them too. It is an optional refinement.

### 4. The CLI owns the printing

`main.cpp` sets `config.progress_fn` to print `"\r[Baking] " << done * 100 / total << "%"` with a flush, and `config.done_fn` to print `std::endl`. That replaces the library's own `std::cout << std::endl`. The library calls it once after the loop, before `BakeSHLightMap` returns. The library's `<iostream>` include and its progress printing are removed. The CLI sets no cancel callback.

- Because progress now counts finished points, the CLI's 100% means the bake really has finished. The text and the final line stay the same.

### 5. Delivery: a branch independent of B2b

The sh-baker branch `bake-progress-cancel` was cut from `origin/main` (`f4e4388`), not from `nee-alpha-shadows` (#62). Both edit `baker.cpp`. #62 merged first as `77f1b72`, and #63 was adjusted in review and merged on top of it as `b6364ba`. The done callback came after, in a direct commit to `main`, `ce5419a`. The netradiant side is on branch `b3-progress-cancel`, cut from `b2-land`, so it has the archived B2 specs.

## Risks / Trade-offs

- [The progress callback runs on a TBB worker while holding the reporter mutex. A slow callback stalls only the workers that cross a percent boundary.] → Document it as "keep it short". A `Sys_Printf` line is microseconds.
- [A progress or cancel callback that calls back into sh-baker, runs TBB work, or blocks on the bake could deadlock. A TBB algorithm inside the progress callback can steal a bake task that then waits on the reporter mutex the same thread holds.] → The header documents that the progress and cancel callbacks must do none of these. q3map2's `Sys_Printf` callback does none of them.
- [`std::function` crosses a boundary with mixed RTTI. q3map2 builds with `-fno-rtti` and `libsh_baker.so` with RTTI, so the inline `std::function` manager and invoker are emitted in each with different bodies.] → The object is built in q3map2's translation unit and only invoked or copied in the library. libstdc++'s `_M_manager` only uses `typeid` under `__cpp_rtti`, for `target_type`/`target`, which neither side calls. The gate builds a `-fno-rtti -fno-exceptions` program (`src/no_rtti_caller_test.cpp`), links it against the `.so` and runs it with the progress and cancel callbacks. It does not set `done_fn`. That callback uses the same mechanism, but this run does not cover it.
- [Changing the CLI's stdout timing (100% now printed at the real end)] → This is cosmetic, and the output files are unchanged.

## Migration Plan

Existing callers compile unchanged, because the new fields default to empty. After the PR merges, the landing bumps `tools/sh-baker`, ticks B3 in `CLAUDE.md`, adds a "Done" note to plan §4.2 item 3 and rebuilds the PDF, archives the change, and runs the stock check. To roll back, revert the sh-baker commits `b6364ba` (#63) and `ce5419a` (`done_fn`) and the submodule bump.
