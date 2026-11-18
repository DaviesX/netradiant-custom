# Tasks

The gate for each group is:

- q3map2 builds (`make binaries-q3map2`) with the module linked in;
- `make test-q3map2-extension` builds the test program from the module and its test file alone, and the run exits 0;
- the stock check passes on a clean `git archive` copy of the benchmark;
- the benchmark compiled single-threaded (`-threads 1`, the stock check's three stages) gives a `bench.bsp` byte-identical to the baseline's apart from the binary's git commit stamp (in the header banner `I LOVE MY Q3MAP2 <version> on <date>` and in worldspawn's `_q3map2_version`), compiled at the same path, with any difference reported to the user before continuing. The stock check's own threaded compile is not deterministic: the same binary gave different BSPs on two runs;
- `tools/sh-baker` stays at `ce5419a`.

Every task's exit criterion includes the stock check (`tools/stockcheck/stockcheck.py`).

## 1. Baseline

- [x] 1.1 On branch `b4-sh-extension`, rebuild q3map2 from the current tree, then run the stock check on a clean `git archive` copy of the benchmark. Compile the benchmark single-threaded twice, and keep that `bench.bsp` as the byte-identity baseline. Verify that the stock check passes and that both single-threaded compiles give the same checksum, then record it.

## 2. Module, writer and reader

- [x] 2.1 Add `tools/quake3/q3map2/q3map2_extension.h` with the types and three functions of design decision 5. The header comment gives:
  - the full byte layout and the version rule;
  - the SH basis table and the radiance-projection rule;
  - the half encoding and clamp;
  - the hull winding and frame;
  - the not-baked rule;
  - the error policy and the single-writer note.

  Add `q3map2_extension.cpp` with stub bodies. Add the object to q3map2's link list in the `Makefile`. Add the test binary rule and the phony `test-q3map2-extension` target, with a placeholder `q3map2_extension_test.cpp` whose `main` returns 0. Verify that q3map2 builds and `make test-q3map2-extension` runs.
- [x] 2.2 Implement the half converter (design decision 7). Add the tests:
  - every finite half bit pattern round-trips;
  - exact values, including −0 and both subnormal ends;
  - the tie, near-tie and subnormal rounding cases of the spec;
  - the clamp to ±65504.

  Verify that the tests pass.
- [x] 2.3 Implement `SaveSHQ3Map2Extension`: the full input check before any file is created (design decision 6), the planar serialization, and the atomic write with cleanup. Add the tests:
  - the golden-bytes test, against a hand-built 144-byte array;
  - one refusal test per listed check, each asserting false, the message text and that no file exists at the path. The cases are:
    - a wrong luxel count, with the page index and counts;
    - a wrong visibility count;
    - zero width;
    - a non-finite coefficient, visibility and position;
    - the not-baked marker;
    - a visibility of 1.5;
    - a negative L0 component;
    - a hull count not a multiple of 3;
    - a zero-count hull;
    - a gap and an overlap between hulls;
    - ranges that stop short;
    - an index not below the vertex count;
  - the I/O tests:
    - a missing directory returns false, names the path and creates nothing;
    - a target that is an existing directory returns false, leaves it unchanged and leaves no `.tmp`;
    - a successful overwrite replaces the bytes and leaves no `.tmp`.

  Verify that the tests pass.
- [x] 2.4 Implement `LoadSHQ3Map2Extension` with the size walk of design decision 6. Add the tests:
  - a round trip of pages sized 4×2, 1×1 and 3×5 with a two-hull mesh, checking the file size;
  - files with no pages, no hulls, and neither (24 bytes);
  - winding kept;
  - rejections, each checked for no result and for the message text:
    - a missing file;
    - a bad magic;
    - version 2;
    - one byte short;
    - one byte extra;
    - a page width of 0, and a page height of 0;
    - the 24-byte file with every count at 0xFFFFFFFF;
    - one page of 0xFFFFFFFF × 0xFFFFFFFF, and one of 0x7FFFFFFF × 0x7FFFFFFF, in a 32-byte file;
    - a bad hull index;
    - a hull range gap;
    - a zero-count hull;
    - a hull count not a multiple of 3;
    - ranges that stop short;
    - a `binary16` infinity and a NaN in a coefficient plane;
    - a visibility of 1.5 and of −1;
    - a negative L0 component;
    - a NaN hull position, with its value in the message.

  Verify that the tests pass, and that the 0xFFFFFFFF cases are rejected by the size walk before any page or hull storage is allocated. Check this by reading the code path and by the run finishing in milliseconds.

## 3. L0 inspection dump

- [x] 3.1 Implement `SaveSHQ3Map2ExtensionPageL0Hdr` (design decision 8). Add a small RGBE decoder to the test file, and the tests:
  - a 4×2 page read back, with each component within 1/128 of its L0 relative to the pixel's largest component;
  - a negative L0 component reads as 0;
  - `LM_0000.hdr`, `lm_x.hdr` and a path `dir\lm_y.hdr` return false and create no file, while `l0_lm_.hdr` succeeds;
  - a missing directory returns false;
  - a page with a wrong luxel count returns false;
  - an infinite L0 component returns false and creates no file, and an L0 of 2^127 is written as 65504.

  Verify that the tests pass.

## 4. Gate, docs and PR

- [x] 4.1 Run the gate. q3map2 builds, the test program exits 0, and the stock check passes. Compare the single-threaded `bench.bsp` with the 1.1 baseline, and report any difference to the user before continuing. Confirm that `tools/sh-baker` is at `ce5419a`. Verify with the recorded output.
- [x] 4.2 Update the docs:
  - plan §4.2 item 4: the format moves into q3map2 as the q3map2 extension, each luxel also carries visibility, and `\emph{Done.}` is added;
  - plan §4.3, where it mentions the side-file writer;
  - `CLAUDE.md`'s B4 line, which becomes a q3map2 task, ticked `[x]`.

  Rebuild the PDF with `latexmk -pdf`. Verify that the PDF builds.
- [x] 4.3 netradiant has no `.clang-format`, and clang-format can't reproduce q3map2's brace style (`func(){`), so the new files follow q3map2's hand style (tabs, spaces inside parentheses) instead. Run `git diff --check`. Bring the artifacts in line with the code, then archive the change, creating `openspec/specs/q3map2-sh-extension/spec.md`, and run `openspec validate --specs --strict`. Commit, then ask the user before pushing. After approval, push `b4-sh-extension` to `fork` and open the PR with `--repo DaviesX/netradiant-custom`. Its base is `master` if PRs #3 and #4 have merged by then, otherwise `b3-progress-cancel`. Verify that validation passes and that the PR URL exists.
