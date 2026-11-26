# Spec Delta

## MODIFIED Requirements

### Requirement: Defaults keep existing output
With every new light field, the full-sphere flag and `confidence_threshold` at their defaults, a bake SHALL give bit-identical results to the bake before this change for the same scene, points and configuration, wherever the earlier bake's result was finite. There are two exceptions from change `sh-baker-sky-radiance` on (spec `sh-baker-environment-light`): surface points' environment visibility is cosine-weighted, while their SH, irradiance and full-sphere points' visibility stay pinned; and a scene with an environment gains sky light. The CLI SHALL set no new field, so its output changes only where its scene has an environment (the glTF loader derives one from the brightest directional light when no skybox is named).

#### Scenario: Existing tests pass
- **WHEN** sh-baker's test suite is run after this change
- **THEN** every test that existed before it passes; the changed assertions are: `IndirectProbeIsSumOfHemispheres` drops its visibility identity (probe = mean of the two hemispheres), the tracer tests adapt to the first-ray callback's new signature without changing what they assert about radiance, and the pinned default-bake references (`kDefaultsFull` and `kDefaultsIndirectOnly`) have their surface points' visibility words re-recorded with the cosine-weighted visibility (sh-baker PR #70), while their SH and irradiance words stay as recorded at `046ff2e`
