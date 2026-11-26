# Spec Delta

## MODIFIED Requirements

### Requirement: Bake quality options
With `-shbake`, `-shsamples N` SHALL set the exact number of rays per point (default 128), and `-shbounces N` the maximum path depth (default 5), for both bakes. q3map2 SHALL pass sh-baker a `confidence_threshold` of 0, so no point stops early. A value below 1 SHALL stop q3map2 with a message naming the option.

#### Scenario: Samples option
- **WHEN** the benchmark is baked with `-shbake -shsamples 16`, and then with `-shsamples 0`
- **THEN** the first log reports exactly 16 samples per point, and the second run exits non-zero naming `-shsamples`

#### Scenario: Exact sample count
- **WHEN** a black floor in a sky shell, partly shaded by a black wall and lit by one point light, is baked single-threaded with `-shsamples 128` and again with `-shsamples 64`
- **THEN** the two side files differ in the visibility of some partly shaded luxels, which they could not if every point stopped at 64 samples (each point's first 64 samples are the same in both bakes, since it is seeded by its index)
