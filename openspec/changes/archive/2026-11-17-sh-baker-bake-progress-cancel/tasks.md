# Tasks

The gate for each sh-baker group is the same as in B2 and B2b:

- every target builds;
- the tests give the same results as the baseline `f4e4388` build, and the new tests pass;
- the CLI runs `--width 256 --height 256 --samples 32 --dilation 4` on `box`, `MultiLights`, `layers` and `notangent`, and its outputs are compared with the baseline's, with any difference reported to the user before continuing;
- `loader_q3map2.h` and `baker.h` compile under `-std=c++20 -fno-exceptions -fno-rtti` with only `src` and Eigen on the include path, and their `-M` closure has no tinygltf, JSON, Embree or `loader.h`;
- from group 2 on, the no-RTTI link check (task 2.2) builds and runs.

Every task's exit criterion includes the stock check (`tools/stockcheck/stockcheck.py`).

## 1. Callbacks and progress (sh-baker, branch `bake-progress-cancel` from `main`)

- [x] 1.1 Create branch `bake-progress-cancel` from sh-baker `origin/main` (`f4e4388`), not from `nee-alpha-shadows`. Build it, and record the baseline test results and CLI outputs for the four fixtures before changing any code. Verify by checking the recorded files.
- [x] 1.2 Add `progress_fn` and `cancel_fn` to `BakeConfig` and `cancelled` to `BakeResult` in `baker.h`, documented as in design decision 1. The documentation covers:
  - which threads call each callback;
  - that calls of each never overlap;
  - that there are no polls after a true;
  - that callbacks must be short, must not call back into sh-baker, run TBB work or block on the bake;
  - what `cancelled` means.

  The library tests each `std::function` for emptiness before every call. Verify that everything builds and the header check passes with `<functional>` included.
  Note 2026-11-17: `ce5419a` on sh-baker `main` added a third callback, `done_fn`, called once on the calling thread after the loop, including for empty and cancelled bakes. The CLI prints its trailing newline through it. No test covers it yet.
- [x] 1.3 Replace the in-loop printing in `baker.cpp` with the finished-point reporter (design decision 2), and remove the library's stdout output. Move the progress printing and the trailing newline to `main.cpp`. Verify with new `baker_test.cpp` tests:
  - with 1000 points, reports never overlap (checked with an atomic in-callback flag), `done` strictly increases and stays at most `total`, and the last report is `(1000, 1000)`;
  - with 4 of 10 points invalid, the last report is `(10, 10)`;
  - 100000 valid points with one sample, no bounces and no lights get at most 101 reports, and the last is `(100000, 100000)`;
  - zero points with a raster width of 0 get no reports, and the result has no pixels;
  - a bake with no callback writes nothing to stdout (`testing::internal::CaptureStdout`);
  - with no callbacks and with only a progress callback, the results are bit-identical.

## 2. Cancellation

- [x] 2.1 Add the cancel latch with serialized polls (design decision 3: the latch check, then the mutex, a re-check, the call and the store). Poll on the calling thread before `rtcNewDevice`, after `BuildBVH` and the light trees, and at the start of each point before the invalid-point check. Cancelled points keep the marker and are not counted as done. Release the Embree resources on every path, and set `result.cancelled`. Verify with new `baker_test.cpp` tests:
  - cancel on the first poll: marked cancelled, all points hold markers, and no `(total, total)` report;
  - on 4096 points, cancel once a report shows at least 25% done: marked cancelled, with 0 < baked < 4096, every baked point bit-identical to an uncancelled bake, and every other point a marker;
  - 4096 points with a cancel callback that returns true on its 100th call, counts every call after that and detects overlapping calls: zero later calls and no overlap;
  - 10 invalid points with a cancel callback that returns true on its third call (the first per-point poll): marked cancelled, and no progress report;
  - a never-true cancel callback leaves the result not cancelled;
  - with no callbacks, with only a never-true cancel callback, and with a progress callback plus a never-true cancel callback, the results are bit-identical to each other. This is where `cancel_fn` is first polled, so the case is meaningful only from here.
  Note 2026-11-17: as merged (`b6364ba`), cancel is polled only at the start of each point, before the invalid-point check, and a true `break`s out of the TBB range. There is no poll before `rtcNewDevice` or after `BuildBVH`, so there is a single release path. `Cancelled()` takes no lock when `cancel_fn` is empty.
  Note 2026-11-17: in the merged tests the first poll is the first point's. `CancelledBeforeStart` asserts exactly one call, and `CancelCheckedBeforeValidity` returns true on the first call and asserts exactly one call. The after-build test (`CancelledAfterBuildSkipsLoop`) was dropped along with the after-build poll.
- [x] 2.2 Add the no-RTTI link check: a small C++20 program, kept with the tests (for example `src/no_rtti_caller_test.cpp`, built by CMake as its own target with `-fno-exceptions -fno-rtti` and linked to `sh_baker`). It bakes 4096 points with a progress callback and a cancel callback that returns true once a quarter are done. It checks that the reports increase and the result is marked cancelled, and exits 0. Verify that it is built and run by the gate (and by `ctest`) and exits 0.
- [x] 2.3 Document progress and cancel in the README's "Library: q3map2 input" section: the callbacks, their threading, and what a cancelled result holds. Verify by reading the rendered section.

## 3. Gate and PR

- [x] 3.1 Run the gate. The CLI outputs for the four fixtures are byte-identical to the baseline, and its stdout still ends the bake with `[Baking] 100%` and a newline. The tests give the same results as the baseline, and the new tests pass. The header check and the no-RTTI link check pass. Verify with the recorded gate output.
- [x] 3.2 Run `clang-format -i` on every touched C++ file, commit, push `bake-progress-cancel`, and open the PR to `github.com/DaviesX/sh-baker` against `main`. The description gives the gate results and notes the expected rebase against #62 in `baker.cpp`. Verify that the PR URL exists and `git diff --check` is clean.
  Done 2026-11-17: sh-baker PR #63 (branch `bake-progress-cancel`, commit 9ab1845). After review: the returning poll in the no-poll-after-true test now sleeps, so a missing second latch check fails 30/30; a test asserts the pre-setup poll is the only call when it returns true; an unrelated clang-format rewrap in `main.cpp` was reverted (only changed lines are formatted, via `git clang-format`); and the README and `baker.h` note that q3map2 must be rebuilt whenever `libsh_baker.so` moves.

## 4. Landing (netradiant branch `b3-progress-cancel`, after the user merges)

- [x] 4.1 Bump `tools/sh-baker` to `ce5419a` (PR #63 merged as `b6364ba`, plus the done-callback commit) and re-run the gate on it, with `77f1b72` as the baseline for the bit-identical checks (`f4e4388` still matches for fixtures with no alpha below 1). Tick `[x] **B3**` in `CLAUDE.md`, add `\emph{Done (\texttt{sh-baker} PR \#63).}` to plan §4.2 item 3, and rebuild the PDF with `latexmk -pdf`. Verify that the PDF builds and the submodule points at `ce5419a`.
  Done 2026-11-17: `tools/sh-baker` at `ce5419a`. Gate against a fresh `f4e4388` build: 162/162 tests (none removed, 29 new across B2b and B3); `sh_baker_no_rtti_caller_test` exits 0; the four fixtures are byte-identical and the CLI still ends with `[Baking] 100%` and a newline (now printed through `done_fn`); header checks pass. B3 ticked in `CLAUDE.md`; plan §4.2 item 3 marked done; PDF rebuilt.
- [x] 4.2 Archive the change, which creates `openspec/specs/sh-baker-bake-control/spec.md`. Verify that `openspec validate --specs --strict` passes.
  Done 2026-11-17: archived as `2026-11-17-sh-baker-bake-progress-cancel`, creating `openspec/specs/sh-baker-bake-control/spec.md`; `openspec validate --specs --strict` passes (12 items, info notes only).
- [ ] 4.3 Run the stock check on a clean copy of the benchmark. Push `b3-progress-cancel` to `fork` and open the landing PR with `--repo DaviesX/netradiant-custom`. Its base is `master` if PR #3 has merged by then, otherwise `b2-land`. Verify that the stock check prints PASS for compile, opengl1 and opengl2, and that the PR URL exists.
