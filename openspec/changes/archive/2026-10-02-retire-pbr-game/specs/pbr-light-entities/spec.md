## MODIFIED Requirements

### Requirement: Entity definitions ship with the gamepack
The `pbr` gamepack source SHALL ship entity definitions for `light`, `light_spot`, and `light_sun` as `Q3.game/baseq3/_pbr_lights.ent`, declaring each key above with its default and a one-line description, so the entity inspector lists them. The definitions for `light_spot` and `light_sun` SHALL include `_shadows`. The file SHALL be named so it loads before the stock `entities.ent` and its `light` replaces the stock definition. It SHALL be the only copy of these definitions in the repository. The stock definitions of all other Quake 3 entities SHALL remain available.

#### Scenario: Inspector shows keys
- **WHEN** a `light_spot` is selected under `Q3.game` with the overlay installed and the entity inspector is open
- **THEN** `cone`, `cone_inner`, `intensity`, `radius`, `_color`, and `_shadows` appear with their defaults

#### Scenario: Quake 3 light definition
- **WHEN** a `light` is selected under `Q3.game` with the overlay installed
- **THEN** the entity inspector shows `intensity` and `radius` with the PBR defaults, and `info_player_deathmatch` and the weapon entities are still listed in the entity menu

### Requirement: Quake 3 game uses the PBR entity module
The repository SHALL provide, in the `pbr` gamepack source (`setup/data/gamepacks/pbr/`), a `games/Q3.game` identical to the downloaded Quake III Arena game file except that it sets `entities="pbr"`. Installing that pack over the downloaded gamepacks SHALL make `light`, `light_spot` and `light_sun` PBR lights under `Q3.game`, with the keys, light model and shadow behaviour this capability specifies for the `pbr` entity module. Every other setting of `Q3.game`, and every other game, SHALL be unaffected.

#### Scenario: Spot light under Quake 3
- **WHEN** the overlay is installed, the editor runs under `Q3.game` with the PBR lighting preview on, and a `light_spot` targets an `info_null` above a lightmapped floor
- **THEN** lighting mode shows the spot's cone of light on the floor, with shadows

#### Scenario: Other Quake 3 based games unchanged
- **WHEN** a Quake 3 based game other than `Q3.game` (for example OpenArena) is active
- **THEN** its `light` entities behave exactly as before this change
