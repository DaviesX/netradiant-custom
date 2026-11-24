# Spec Delta

## ADDED Requirements

### Requirement: Fog parameters are read from Quake 3 shaders
The Quake 3 shader parser SHALL read `fogparms ( r g b ) d` into the shader's fog colour (r, g, b) and fog distance d, and `q3map_fogDir ( x y z )` into its fog direction, which fog-volume's "The fog surface" uses when the shader is a brush's content shader. Both SHALL be read whether or not lighting mode is enabled. A shader without `fogparms` SHALL have no fog parameters. A `fogparms` line that doesn't give three colour numbers and a distance SHALL print one console warning naming the shader, and the shader SHALL have no fog parameters. The parser SHALL also report the q3map2 content classes fog-volume needs: liquid (`surfaceparm water`, `slime` or `lava`), fog (`surfaceparm fog`), and translucent (any of the surfaceparms q3map2 marks `C_TRANSLUCENT`: `trans`, `trigger`, `playerclip`, `monsterclip`, `alphashadow`, `lightfilter`, `nodrop`, `botclip`, `clusterportal`, `donotenter`, `areaportal`, `origin` and the liquids). Stock keeps treating these lines as before: the editor only reads them.

#### Scenario: Town fog
- **WHEN** `textures/town/fog` is parsed
- **THEN** its fog colour is (0.200392, 0.258824, 0.231373) and its fog distance 2048, and it has no fog direction

#### Scenario: Content classes
- **WHEN** a shader declares `surfaceparm trigger` and `surfaceparm nonsolid`
- **THEN** it is translucent, and neither liquid nor fog

#### Scenario: Malformed fogparms
- **WHEN** a shader declares `fogparms ( 0.5 0.5 ) 1024`
- **THEN** the console warns once naming the shader, and it has no fog parameters
