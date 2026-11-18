# Spec Delta

## Purpose

How a caller of sh-baker's single bake call, such as q3map2's `-shbake`, observes its progress and stops it early, and what the results hold when it stops.

## ADDED Requirements

### Requirement: Progress reports
A bake call given a progress callback SHALL report `(done, total)`. `total` is the number of points passed in, and `done` is the number of points finished, counting points skipped as invalid. Reports SHALL never overlap one another. `done` SHALL strictly increase between reports and never exceed `total`. A point SHALL be counted only after its result is written.

#### Scenario: Reports increase and never overlap
- **WHEN** 1000 points are baked with a progress callback that records each report and detects whether two calls overlap
- **THEN** no two calls overlap, each `done` is greater than the one before it, and every `done` is at most 1000

#### Scenario: Invalid points count
- **WHEN** 10 points are baked, 4 of them marked invalid
- **THEN** the last report is `(10, 10)`

### Requirement: Report frequency and completion
A bake call SHALL report at most once for each whole percent of `done / total`, which is no more than 101 reports. A bake of at least one point that finishes without being cancelled SHALL end with the report `(total, total)`. A bake of zero points SHALL make no reports.

#### Scenario: Bounded reports
- **WHEN** 100000 valid points are baked with one sample each, no bounces and no lights, with a progress callback
- **THEN** it is called no more than 101 times, and its last call is `(100000, 100000)`

#### Scenario: Zero points
- **WHEN** an empty list of points is baked, with a raster width of 0, and a progress callback
- **THEN** the callback is never called and the result has no points

### Requirement: Cancellation
A bake call given a cancel callback SHALL poll it before starting each point, until a poll returns true. It SHALL not poll it before or during the building of its ray-tracing resources. Polls SHALL never overlap, but they may come from different bake threads. Once a poll returns true, the call SHALL never poll again and SHALL start no further point. It SHALL let points already in progress finish, release its ray-tracing resources, and return with the result marked cancelled.

#### Scenario: Cancelled at the first point
- **WHEN** 500 points are baked with a progress callback and a cancel callback that returns true from its first call
- **THEN** the cancel callback is called exactly once, the call returns marked cancelled, every point keeps the not-baked marker, and no progress report says `done == total`

#### Scenario: Cancelled midway
- **WHEN** 4096 points are baked, and the cancel callback starts returning true once a progress report shows at least a quarter of them done
- **THEN** the call returns marked cancelled with fewer than 4096 points baked and at least one baked

#### Scenario: Not called again after true
- **WHEN** 4096 points are baked, and the cancel callback returns true on its 100th call while counting every call after that one, and also detects whether two calls overlap
- **THEN** that count is zero and no two calls overlap

#### Scenario: Cancel checked before validity
- **WHEN** a bake of 10 points, all marked invalid, has a progress callback and a cancel callback that returns true on its first call
- **THEN** the cancel callback is called exactly once, the result is marked cancelled, and no progress report is made, because a point is polled before its validity is checked and no point is counted as done

### Requirement: Results after a cancel
After a cancelled bake, each point SHALL either hold exactly the result a full bake of the same input gives it, or keep the not-baked marker that an invalid point keeps. This includes points that were never polled because the cancel came first. A result SHALL be marked cancelled exactly when a poll returned true. A cancelled result therefore has at least one point that was not baked. A bake whose polls all return false, or that makes no polls, SHALL not be marked cancelled.

#### Scenario: Baked points match a full bake
- **WHEN** the midway-cancelled bake is compared with an uncancelled bake of the same points
- **THEN** every point with a result has bit-identical SH coefficients and environment visibility in both, and every other point holds the not-baked marker

#### Scenario: Finished bake not marked
- **WHEN** a bake runs to completion with a cancel callback that always returns false
- **THEN** the result is not marked cancelled

### Requirement: Done callback
A bake call given a done callback SHALL call it exactly once, on the thread that made the bake call, after the last point has finished or been skipped and after every progress report, and before the call returns. It SHALL be called for every bake, including a bake of zero points and a cancelled bake.

#### Scenario: Called once after the last report
- **WHEN** 1000 points are baked with a progress callback and a done callback that records its calling thread and how many reports came before it
- **THEN** the done callback is called once, on the calling thread, after the `(1000, 1000)` report

#### Scenario: Zero points
- **WHEN** an empty list of points is baked, with a raster width of 0, and a done callback
- **THEN** the done callback is called once

#### Scenario: Cancelled bake
- **WHEN** a bake's cancel callback returns true from its first call, and a done callback is set
- **THEN** the done callback is called once and the result is marked cancelled

### Requirement: Callbacks do not change results
A bake with no callbacks, or with callbacks that never cancel, SHALL give bit-identical results to the commit before this change. The CLI's output files SHALL be byte-identical.

#### Scenario: Same results with and without callbacks
- **WHEN** the same points are baked once with no callbacks and once with a progress callback and a cancel callback that returns false
- **THEN** the two results are bit-identical

#### Scenario: CLI outputs
- **WHEN** the CLI bakes `data/box`, `data/MultiLights`, `data/layers` and `data/notangent` with fixed arguments before and after the change
- **THEN** every output file is byte-identical

### Requirement: Absent callbacks and no throwing
Any of the progress, cancel and done callbacks MAY be left empty. An empty callback SHALL be ignored, and the bake call SHALL not throw, whether callbacks are set or not.

#### Scenario: Only one callback set
- **WHEN** points are baked once with only a progress callback and once with only a cancel callback that returns false
- **THEN** both calls complete normally and give bit-identical results

### Requirement: Callbacks work from a no-RTTI caller
A caller compiled with `-std=c++20 -fno-exceptions -fno-rtti`, as q3map2 is built, SHALL be able to set the progress and cancel callbacks and bake through the shared library, which is built with RTTI. Progress reports and cancellation SHALL behave as specified.

#### Scenario: Linked no-RTTI program
- **WHEN** a program built with those flags and linked against `libsh_baker.so` bakes 4096 points with a progress callback and a cancel callback that returns true once a quarter are done
- **THEN** it receives increasing reports, the result is marked cancelled, and the program exits normally

### Requirement: Library silent on stdout
The bake call SHALL write no progress text to stdout. The CLI SHALL print its progress through the progress callback in the same `\r[Baking] N%` form, and the newline that ends it through the done callback.

#### Scenario: Library call prints nothing
- **WHEN** a test captures stdout around a bake call that has no progress callback
- **THEN** nothing was written to stdout

#### Scenario: CLI still shows progress
- **WHEN** the CLI bakes `data/box`
- **THEN** its stdout contains `[Baking] 100%` followed by a newline
