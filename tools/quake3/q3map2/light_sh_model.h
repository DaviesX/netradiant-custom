/* -------------------------------------------------------------------------------

   light_sh_model: the pure rules of q3map2's -shbake stage (task B5), kept free
   of sh-baker and of q3map2's globals so that `make test-q3map2-shbake-model`
   can test them on their own. light_sh.cpp holds the glue that needs either.

   This file is part of NetRadiant-custom, under the GNU General Public License
   version 2 or later.

   ------------------------------------------------------------------------------- */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "math/vector.h"
#include "skylight.h"

/*
   Every rule here is a port of the editor (or of the engine, for the grid), so
   that the bake reads a map the way the lighting preview shows it. The editor's
   source is the reference: a divergence is fixed in both (design.md, Risks).

   1. Light entities (design decision 9; plugins/entity/light.cpp, class
      PBRLight and Light::pbrUpdateDirection / pbrParams; plugins/entity/colour.h
      read_colour_normalized; setup/data/gamepacks/pbr/README.md, "Light
      entities and units"). Keys are parsed exactly as libs/stringio.h does:
      a float is one strtod literal with nothing after it, a vector is three
      literals separated by single spaces with nothing before or after.
      - Type: classname `light`, `light_spot`, `light_sun`, case-insensitive
        (eclasslib.h classname_equal). Any other classname is not a light here.
      - `intensity`: radiant flux (power, in the model's arbitrary linear unit)
        for point and spot, irradiance (that unit per square map unit) for the
        sun. Unparsable or absent gives 50000 (point,
        spot) or 3 (sun). Negative values are kept, as the editor keeps them.
      - `radius`: cutoff distance in map units. Unparsable, absent or <= 0
        gives 256. The sun has no cutoff (range 0 here means unbounded).
      - `cone`: outer half-angle in degrees. Unparsable, absent or <= 0 gives
        45. `cone_inner`: unparsable, absent or < 0 gives 30. cos outer =
        cos(min(cone, 89.9)), cos inner = cos(min(cone_inner, cone, 89.9))
        (PBRLight::cosOuter / cosInner).
      - Point radiant intensity I = intensity / (4 pi). Spot I = intensity /
        (2 pi max(1 - cos outer, 1e-4)) (pbr_fp.glsl). Irradiance at distance d
        is I / max(d, 5e-3)^2 while d <= radius, else 0, times the spot cone
        factor smoothstep(cos outer, cos inner, cos angle); when cos inner <=
        cos outer the cone is a hard edge (design decision 1), lit where
        cos angle >= cos outer.
      - Direction, for spot and sun (the direction the light travels): a
        spot's `target`, when the targeted entity exists and isn't at the
        light's origin, aims at that entity. Otherwise a non-empty `angles`
        ("pitch yaw roll", positive pitch looks down; unparsable gives 0 0 0)
        gives (cos yaw cos pitch, sin yaw cos pitch, -sin pitch). Otherwise a
        non-empty `angle` is a yaw, with -1 meaning straight up (pitch -90) and
        -2 straight down (pitch 90); unparsable gives yaw 0. With neither key the
        direction is +X (angles 0 0 0). In the editor the later of `angles` and
        `angle` in key order wins; the spec orders them `angles` first, which
        is what this module does.
      - `_color`: linear RGB, divided by its largest component; unparsable or a
        largest component of exactly 0 gives white.
      - `_shadows`: spot and sun cast unless it parses as an integer equal to
        0. Point lights never cast, whatever it says. This is the editor's
        preview rule only: it is parsed (ShLight::castsShadows) and tested, but
        no bake uses it, since sh-baker shadows every light, point lights
        included (design decisions 1 and 9).
      The sh-baker side scaling of design decision 2 (s = 1/32) and sh-baker's
      Flux() are glue in light_sh.cpp; every value here is in map units.

   2. Casters and bake materials (design decisions 7 and 8;
      radiant/renderstate.cpp Shadow_materialCasts; plugins/shaders/shaders.cpp
      Quake3Stage, Quake3Stage_parseLine and ShaderTemplate::deriveQuake3).
      - Caster rule: a shader never casts if it has skyparms or surfaceparm
        sky, fog, water, slime, lava, playerclip, botclip, areaportal,
        noshadows, trigger or hint; or surfaceparm trans without alphashadow;
        or surfaceparm nodraw together with nonsolid. Everything else casts:
        solid nodraw (caulk), opaque and blended surfaces alike.
      - Base colour: the stage paired with the first `$lightmap` stage by a
        multiplying blend (filter, GL_DST_COLOR GL_ZERO or GL_ZERO
        GL_SRC_COLOR) and having a map: the stage before the lightmap if that
        pairing holds, else the one after. Without such a pair, the first stage
        with a map that isn't `$lightmap`. `$whiteimage` gives "_white". A bare
        texture (no script) uses its own name; a scripted shader with no
        stages has none.
      - Emissive: the first stage that adds (blend dst GL_ONE with src GL_ONE
        or GL_SRC_ALPHA, i.e. `add`), has a map, and isn't `$lightmap`,
        `$whiteimage` or `tcGen environment`. Its colour is its `rgbGen const`,
        else white; its strength is `qer_pbr_emissiveStrength` (default 1).
        A shader with skyparms has no emissive (openspec change
        q3map2-sky-environment, design decision 7): its light is the sky
        environment light of section 8, so an additive cloud stage never makes
        it an area light. `surfaceparm sky` without skyparms keeps the rule.
      - Alpha test: the base stage's alphaFunc (GT0, LT128, GE128).
      - Additive (sh-baker Material::additive: emits, never occludes): every
        stage adds, and there is at least one stage. An opaque base with an
        additive glow stage is therefore not additive and occludes.
      - Stages with rend2 keywords take no part (Quake3Stage::m_rend2).
      - Stage lines are read by ShStageParseLine, a port of
        Quake3Stage_parseLine and of parseQuake3's rend2 keyword test, so the
        shader parser glue (task 5.1) doesn't re-implement them.

   3. Alpha rewrite (design decision 7): the base stage's alphaFunc rewrites a
      texel's alpha to 255 where the engine's test passes and 0 where it fails:
      GT0 a > 0, LT128 a < 128, GE128 a >= 128 (the editor's float refs 0, 0.5,
      0.5 on a / 255). Without an alphaFunc every texel becomes 255.

   4. Single-tile UV test (design decision 7): an emissive surface gets
      sh-baker's textured emission CDF only when every UV lies in one unit
      tile [k, k + 1] x [l, l + 1] (closed, with a 1e-4 tolerance: a full-
      texture quad has UVs exactly 0 and 1) and no two triangles overlap in UV
      with positive area. Triangles with (near) zero UV area are rejected too,
      because sh-baker's rasteriser gives them no texels and their power would
      be lost. sh-baker rasterises raw UVs with no wrap
      (rasterizer.cpp RasterizeGeometryUVMaps), so the caller subtracts the
      returned tile from the UVs it hands sh-baker.

   5. SH basis (design decisions 6 and 12): the SH basis and coefficient order
      are sh-baker's (tools/sh-baker/src/sh_coeffs.cpp) and the side file's:
      index 0 L0 0.282095; 1 L1m1 0.488603 y; 2 L10 0.488603 z; 3 L11 0.488603 x;
      4 L2m2 1.092548 xy; 5 L2m1 1.092548 yz; 6 L20 0.315392 (3z^2 - 1);
      7 L21 1.092548 xz; 8 L22 0.546274 (x^2 - y^2). A coefficient is the
      projection of incoming radiance, integral of L_i Y dw, with directions
      pointing towards the light. There is no SH to irradiance convolution
      here: the lightmap is not encoded from the SH (section 6), and the
      order-2 SH of a delta light rings (+6.25% at normal incidence, 0.0625 E
      on the back), which sh-baker's irradiance estimate doesn't.

   6. Lightmap encoding (design decision 5; setup/data/tools/gl/tonemap_fp.glsl;
      gamepack README "Shading"). The irradiance E of a super-luxel or vertex
      is the full bake's SHCoeffs::irradiance at that point: sh-baker's
      cosine-weighted Monte Carlo estimate at its normal, direct plus
      indirect, with no SH ringing; -super N averages it linearly before it
      gets here. Per channel d = E / pi; t = clamp(U(d) /
      U(11.2), 0, 1) with Hable's U(x) = (x (A x + C B) + D E) / (x (A x + B) +
      D F) - E / F, A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
      s = the exact sRGB encode of t; byte = round(63.75 s), so 0..64. opengl1
      at default cvars in a window shifts lightmap bytes left by 2, making
      byte 64 a multiplier of 1.0.
      - Neutral bytes (-shnofull, design decisions 1 and 5): with no full
        bake, every lightmap luxel a surface uses and every baked vertex
        colour is byte 64 per channel, so textures show at their own
        brightness (128 would be twice overbright).

   7. Grid fit (design decision 6; the engine model of ioquake3 renderergl1
      tr_light.cpp R_SetupEntityLightingGrid and R_SetupEntityLighting,
      tr_shade_calc.cpp RB_CalcDiffuseColor_scalar and tr_bsp.cpp
      R_ColorShiftLightingBytes and R_LoadLightGrid, confirmed by task 6.3
      against ../ioq3-custom/ioq3 at 0135354f). Input: one full-sphere SH of the full bake at the grid
      point (SurfacePoint::full_sphere: direct plus indirect, projected over
      the whole sphere). The fit is ambient plus one directional light.
      - Direction d (towards the light, q3map2's grid convention): the
        normalised, Rec. 709 luminance-weighted L1 vector (L11, L1m1, L10).
        When its squared length isn't finite or is at most
        c_shGridMinL1Length2, d is +Z.
      - Directed D = max(0, L1 . d) / 0.488603 per channel. A delta light of
        irradiance E-perp from d projects to E-perp Y(d), whose L1 . d is
        E-perp 0.488603, so for one light D is exactly its E-perp.
      - Ambient: the delta light's L0 share, 0.282095 D, is taken from L0 per
        channel and the rest clamped at 0; A = pi 0.282095 L0_rest, the
        irradiance of the remaining light averaged over all normals (a
        constant radiance L gives L0 = 4 pi 0.282095 L, so A = pi L).
      - Engine: a model's realised ambient is min(255, 4 a 0.6 + 32) and a face
        towards d adds 4 dir 1.0, clamped at 255 (r_ambientScale 0.6,
        r_directedScale 1, identityLight 32 floor, load-time shift 2).
        target(x) = 4 enc(x), the post-shift value of a lightmap byte.
      - Bytes, per channel: ambient = round(clamp((target(A) - 32) / 0.6, 0,
        255) / 4); realised = min(255, 4 ambient 0.6 + 32); directed =
        round(clamp(target(A + D) - realised, 0, 255) / 1.0 / 4). Each byte is
        clamped to 63, since a byte above 63 makes R_ColorShiftLightingBytes
        rescale the whole triple and shift its hue. Then an all-zero ambient
        triple becomes 1 1 1: R_SetupEntityLightingGrid skips a cell whose
        ambient sums to zero as one in a wall and lights the model from the
        other cells, but a baked point is never in a wall, so a dark one must
        still darken its neighbourhood (q3map2's TraceGrid raises it only
        when directed isn't zero; task 6.3 widened the rule). Grid points in
        solid are never fitted and stay 0. Directed comes after that rule,
        and the realised ambient uses the final quantised byte, so a face
        darker than the engine floor of 32 gets the floor, the lit face is target(A + D) measured from what the engine
        realises rather than from target(A), and ambient above 4 * 63 * 0.6 +
        32 = 183.2 post-shift (enc(A) about 46) is capped there.
      - Neutral bytes (-shnofull, design decision 6): ambient 63 63 63,
        directed 0 0 0, direction +Z, for every grid point not in solid. The
        ambient is the fit's for an ambient target of byte 64: (255 - 32) /
        0.6 is above 255, so it takes the clamp of 63, not 64, which would
        trigger the load-time rescale. The fit would also put the gap from the
        ceiling to 4 * 64 into directed light (18); the neutral has none, so
        no direction is favoured. Models then show at the engine's ambient
        ceiling, about 183/255; the ambient-1 rule doesn't apply.

   8. Sky environment light (openspec change q3map2-sky-environment; the
      derivation is libs/skylight.h, the editor's radiant/skyenvironment.cpp
      and plugins/shaders/shaders.cpp are the reference for the shader data).
      - Sky source: the shader's name; its farbox, the skyparms first token
        unless it is `-`, kept as written (so `full` is a farbox named
        "full", as in the editor); the sky stage, the first stage with a map
        (rend2 stages have none) that isn't `$lightmap`, and its `rgbGen
        const` or white; and whether `qer_pbr_emissiveStrength` was declared,
        with its value. skylight::EffectiveStrength decides whether it emits.
      - Equirect texture: sh-baker's Environment::Texture layout, W x H =
        1024 x 512 RGB floats, row 0 at the zenith, texel (x, y) at index
        (y W + x) 3. Texel (x, y) spans theta in [y, y + 1] pi / H from the
        texture's +Y and phi in [x, x + 1] 2 pi / W, its texture direction
        being (sin theta sin phi, cos theta, sin theta cos phi). The texture
        frame is the world turned by c_shSkyToEnvironment, whose rows are
        (0 1 0), (0 0 1), (1 0 0): texture (x, y, z) = world (y, z, x), so
        world +Z is up, theta is measured from world +Z and phi = atan2(y, x)
        is the map azimuth from +X. Each texel is the mean of L over a 4 x 4
        grid of (theta, phi) sub-positions at sub-cell centres, weighted by
        sin theta. H is even, so a cloud layer's horizon falls on a row
        boundary and every row of it is exactly its radiance or 0.
      - Cross-check: the texture's irradiance, from its SH in the texture
        frame (sh-baker's ProjectEnvironmentToSH), evaluated at the texture
        direction of each world axis n in +-X, +-Y, +-Z, against the
        derivation's skylight::Irradiance( ProjectRadiance, n ). The error is
        the largest channel difference over the six, divided by the largest
        channel of the derivation's six irradiances (0 when that is 0); the
        glue warns above 1%.
*/

/// Real SH coefficients, l <= 2, in sh-baker's order, one RGB triple each.
using ShCoeffs9 = std::array<Vector3, 9>;
using ShBytes3 = BasicVector3<std::uint8_t>;

// --- 1. Light entities ------------------------------------------------------

enum class ShLightType { Point, Spot, Sun };

/// \brief The light type for an entity classname, or nothing if it isn't one of the three.
std::optional<ShLightType> ShLightTypeFromClassname( const char* classname );

/// \brief An editor light, in map units.
struct ShLight
{
	ShLightType type = ShLightType::Point;
	Vector3 origin = Vector3( 0, 0, 0 );
	Vector3 direction = Vector3( 1, 0, 0 ); ///< unit; the direction the light travels (spot, sun)
	Vector3 colour = Vector3( 1, 1, 1 );    ///< `_color`, normalised by its largest component
	float intensity = 50000;                ///< the key: flux (point, spot) or irradiance (sun)
	float radiantIntensity = 0;             ///< I in W/sr for point and spot; 0 for the sun
	float range = 256;                      ///< cutoff in map units; 0 (unbounded) for the sun
	float cosInner = 1;                     ///< spot only; point and sun keep 1 and -1
	float cosOuter = -1;
	bool castsShadows = false;
};

/// Returns a key's value, or "" (or nullptr) when the entity doesn't have it.
using ShKeyLookup = std::function<const char*( const char* key )>;

/// \brief Reads a light entity's keys as the editor does.
/// \p targetOrigin is the origin of the entity a spot's `target` names, or nullptr if there is none.
ShLight ShLightFromKeys( ShLightType type, const Vector3& origin, const ShKeyLookup& key, const Vector3* targetOrigin );

/// \brief The editor's direct irradiance from \p light at \p point on a surface with unit \p normal,
/// unshadowed: the reference model the bake must reproduce.
Vector3 ShLightIrradiance( const ShLight& light, const Vector3& point, const Vector3& normal );

// --- 1b. Attached emitter lights (plan revision 7) ----------------------------
//
// A `light_area` (a one-sided rectangle on a static emissive quad) and a `light` or
// `light_spot` with `_proxy 1` stand in for an emissive surface in the editor preview
// and renderer_sh. The bake already holds that surface as an area light, so it leaves
// them out: adding them would count the surface's direct light twice in the full bake
// (spec "Attached emitter lights skipped"). The rectangle follows the light_area keys
// of openspec change emissive-area-lights (pbr-light-entities, "Area light keys").

constexpr float c_shAttachDistance = 1.0f;  ///< a light_area's plane lies within this of every surface vertex
constexpr float c_shAttachDegrees = 1.0f;   ///< and its normal within this of the surface's normal
constexpr float c_shProxyMargin = 64.0f;    ///< a _proxy light lies inside the surface's bounds grown by this
constexpr float c_shAttachMinArea = 1e-3f;  ///< the overlap of a light_area and a surface, in square units

/// \brief True for classname `light_area`, case-insensitive.
bool ShIsAreaLightClassname( const char* classname );
/// \brief True for an entity the bake leaves out (spec "Attached emitter lights skipped"): a `light_area`,
/// or a `light`, `light_spot` or `light_sun` whose `_proxy` starts with '1' (q3map2's reading of a boolean
/// key, which the placeholder -light uses for the same skip). `_proxy` on any other class is ignored.
bool ShIsAttachedEmitterLight( const char* classname, const ShKeyLookup& key );

/// \brief A light_area's rectangle, in map units.
struct ShAreaRect
{
	Vector3 centre = Vector3( 0, 0, 0 );
	Vector3 forward = Vector3( 1, 0, 0 ); ///< unit; the emitting normal
	Vector3 right = Vector3( 0, -1, 0 );  ///< unit; `width` lies along it
	Vector3 up = Vector3( 0, 0, 1 );      ///< unit; `height` lies along it
	float width = 64;
	float height = 64;
};

/// \brief Reads a light_area's `origin`, `angles` (pitch yaw roll; or `angle`, a yaw with -1 up and -2
/// down), `width` and `height` (unparsable or not positive gives 64). The basis is Quake's AngleVectors.
ShAreaRect ShAreaRectFromKeys( const Vector3& origin, const ShKeyLookup& key );

/// \brief True when \p rect is attached to the surface: its normal is within c_shAttachDegrees of the
/// surface's area-weighted normal, every vertex lies within c_shAttachDistance of its plane, and some
/// triangle overlaps the rectangle by more than c_shAttachMinArea. \p indices holds three per triangle;
/// their winding doesn't matter, since the sum of the vertex \p normals picks the side the surface faces
/// (a surface whose triangles have no area, or whose vertex normals sum to the plane, is not attached).
bool ShAreaRectAttached( const ShAreaRect& rect, const std::vector<Vector3>& positions, const std::vector<Vector3>& normals,
                         const std::vector<std::uint32_t>& indices );

/// \brief True when a _proxy light at \p origin lies inside the bounds of \p positions grown by
/// c_shProxyMargin on every axis (closed).
bool ShProxyAttached( const Vector3& origin, const std::vector<Vector3>& positions );

// Parsers with libs/stringio.h semantics, exposed for the glue and the tests.
bool ShParseFloat( const char* string, float& f );
bool ShParseInt( const char* string, int& i );
bool ShParseVector3( const char* string, Vector3& v );

// --- 2. Casters and bake materials ------------------------------------------

enum class ShBlendFactor
{
	Zero, One, SrcColour, OneMinusSrcColour, SrcAlpha, OneMinusSrcAlpha,
	DstColour, OneMinusDstColour, DstAlpha, OneMinusDstAlpha, SrcAlphaSaturate,
};

enum class ShAlphaFunc { None, GT0, LT128, GE128 };

/// \brief One shader stage, as far as the derivation needs it (shaders.cpp Quake3Stage).
struct ShStage
{
	std::string map;               ///< map, clampMap or the first animMap frame; "" when none; `$lightmap` etc. kept as written
	bool blend = false;            ///< has a recognised blendFunc
	ShBlendFactor src = ShBlendFactor::One;
	ShBlendFactor dst = ShBlendFactor::Zero;
	bool constColour = false;      ///< rgbGen const ( r g b )
	Vector3 colour = Vector3( 1, 1, 1 );
	bool tcGenEnvironment = false;
	ShAlphaFunc alphaFunc = ShAlphaFunc::None;
	bool rend2 = false;            ///< uses a rend2 stage keyword: ignored by the derivation

	bool hasMap() const;
	bool isLightmap() const;
	bool isWhiteImage() const;
	bool multiplies() const;
	bool adds() const;
	bool blends() const;
};

/// \brief Applies a stage's `blendFunc` arguments as the editor does: one word (add, filter, blend; anything
/// else is ignored) or two GL factor names (an unknown name is GL_ZERO). Returns false if nothing was set.
bool ShStageParseBlendFunc( ShStage& stage, const std::vector<std::string>& args );
/// \brief Applies a stage's `alphaFunc` argument (GT0, LT128, GE128; anything else leaves it unchanged).
void ShStageParseAlphaFunc( ShStage& stage, const char* func );
/// \brief A GL blend factor name (case-insensitive), or nothing.
std::optional<ShBlendFactor> ShBlendFactorFromName( const char* name );
/// \brief True for a rend2-only stage keyword (shaders.cpp c_rend2StageKeywords): stage, normalMap, bumpMap,
/// specularMap, specularReflectance, specularExponent, gloss, roughness, normalScale, specularScale, parallaxDepth.
bool ShStageIsRend2Keyword( const char* keyword );
/// \brief Applies one stage line as the editor does (Quake3Stage_parseLine, plus the rend2 test of
/// ShaderTemplate::parseQuake3): a rend2 keyword marks the stage rend2; map and clampMap take their first
/// argument, animMap its second (the first frame); blendFunc and alphaFunc as above; `rgbGen const` takes the
/// first three arguments that parse as floats (so the parentheses are skipped) and is ignored with fewer;
/// tcGen and texGen set or clear `tcGen environment`. Every other keyword is ignored. Map names are kept as
/// written: the caller normalises them as it does for every image path.
void ShStageParseLine( ShStage& stage, const char* keyword, const std::vector<std::string>& args );

/// \brief The literal surfaceparms (and skyparms) the caster rule tests.
struct ShSurfaceParms
{
	bool skyParms = false;   ///< the shader has a skyparms line (editor QER_SKY)
	bool sky = false;        ///< surfaceparm sky
	bool fog = false;
	bool water = false, slime = false, lava = false;
	bool playerClip = false, botClip = false;
	bool areaPortal = false;
	bool noShadows = false, trigger = false, hint = false;
	bool trans = false, alphaShadow = false;
	bool nodraw = false, nonsolid = false;
};

/// \brief The editor's caster rule (Shadow_materialCasts).
bool ShShaderCasts( const ShSurfaceParms& parms );

/// \brief A shader as the derivation sees it.
struct ShShaderDesc
{
	std::string name;              ///< used as the base image of a bare texture
	bool scripted = true;          ///< false for a texture with no shader script
	ShSurfaceParms parms;
	std::vector<ShStage> stages;
	Vector3 baseColorFactor = Vector3( 1, 1, 1 ); ///< qer_pbr_baseColorFactor
	float emissiveStrength = 1;    ///< qer_pbr_emissiveStrength
	bool emissiveStrengthDeclared = false; ///< qer_pbr_emissiveStrength was given
	std::string skyFarbox;                 ///< skyparms' first token unless it is `-`; "" otherwise
};

/// \brief What the bake takes from a shader.
struct ShMaterialRules
{
	bool casts = false;            ///< the caster rule
	int baseStage = -1;            ///< index into the stages, or -1
	std::string baseImage;         ///< the base colour image, "_white" for $whiteimage, "" for none
	ShAlphaFunc alphaFunc = ShAlphaFunc::None; ///< the base stage's alphaFunc
	int emissiveStage = -1;        ///< index into the stages, or -1
	std::string emissiveImage;
	Vector3 emissiveColour = Vector3( 0, 0, 0 ); ///< rgbGen const or white; 0 without an emissive stage
	float emissiveStrength = 0;    ///< qer_pbr_emissiveStrength; 0 without an emissive stage
	bool additive = false;         ///< every stage adds: emits and never occludes

	bool alphaTested() const {
		return alphaFunc != ShAlphaFunc::None;
	}
	/// an emissive stage with a non-zero colour and strength
	bool emits() const;
};

ShMaterialRules ShDeriveMaterial( const ShShaderDesc& shader );

// --- 3. Alpha rewrite -------------------------------------------------------

std::uint8_t ShAlphaRewrite( ShAlphaFunc func, std::uint8_t alpha );
/// \brief Rewrites the alpha of \p pixels RGBA8 texels in place.
void ShAlphaRewriteRGBA( ShAlphaFunc func, std::uint8_t* rgba, std::size_t pixels );

// --- 4. Single-tile UV test -------------------------------------------------

struct ShUVTile
{
	bool single = false;           ///< all UVs in one tile, no overlap, no degenerate triangle
	int u = 0, v = 0;              ///< the tile; subtract it from the UVs before handing them to sh-baker
};

/// \brief \p uvs holds one (u, v) per vertex; \p indices three per triangle.
ShUVTile ShSingleTileUV( const std::vector<Vector2>& uvs, const std::vector<std::uint32_t>& indices );

// --- 5. SH basis -------------------------------------------------------------

constexpr float c_shBasis0 = 0.282095f;
constexpr float c_shBasis1 = 0.488603f;
constexpr float c_shBasis2_1 = 1.092548f;
constexpr float c_shBasis2_0 = 0.315392f;
constexpr float c_shBasis2_2 = 0.546274f;

/// \brief The nine basis values at unit direction \p n, in sh-baker's order.
std::array<float, 9> ShBasis( const Vector3& n );

// --- 6. Lightmap encoding ---------------------------------------------------

constexpr float c_shHableA = 0.15f;
constexpr float c_shHableB = 0.50f;
constexpr float c_shHableC = 0.10f;
constexpr float c_shHableD = 0.20f;
constexpr float c_shHableE = 0.02f;
constexpr float c_shHableF = 0.30f;
constexpr float c_shWhitePoint = 11.2f;
constexpr float c_shByteScale = 63.75f;
constexpr int c_shNeutralLightmapByte = 64;   ///< -shnofull: lightmap and vertex bytes, a multiplier of 1.0 in opengl1

double ShHable( double x );
double ShSrgbEncode( double linear );
double ShSrgbDecode( double encoded );
/// \brief One channel of irradiance to a lightmap byte, 0..64.
int ShLightmapByte( float irradiance );
/// \brief The lightmap bytes of irradiance \p E (q3map2's ColorToBytes under -shbake).
ShBytes3 ShLightmapBytes( const Vector3& E );
/// \brief The neutral lightmap and vertex bytes under -shnofull: 64 64 64.
ShBytes3 ShNeutralLightmapBytes();

// --- 7. Grid fit ------------------------------------------------------------

constexpr int c_shGridShift = 2;              ///< r_mapOverBrightBits 2, no hardware gamma: bytes << 2
constexpr float c_shGridAmbientScale = 0.6f;  ///< r_ambientScale
constexpr float c_shGridDirectedScale = 1.0f; ///< r_directedScale
constexpr float c_shGridFloor = 32.0f;        ///< identityLight * 32, added to every model's ambient
constexpr int c_shGridMaxByte = 63;           ///< above this the load-time shift rescales the triple
constexpr float c_shGridMinL1Length2 = 1e-30f; ///< squared weighted L1 length at or below which d is +Z
constexpr int c_shGridNeutralAmbientByte = 63; ///< -shnofull: ambient 63 63 63, directed 0 0 0, d = +Z

struct ShGridFit
{
	Vector3 direction = Vector3( 0, 0, 1 ); ///< unit, towards the light
	Vector3 ambient = Vector3( 0, 0, 0 );   ///< A, irradiance
	Vector3 directed = Vector3( 0, 0, 0 );  ///< D, irradiance along the direction
	ShBytes3 ambientBytes = ShBytes3( 0, 0, 0 );
	ShBytes3 directedBytes = ShBytes3( 0, 0, 0 );
};

/// \brief Fits one grid point from the full bake's full-sphere SH there.
ShGridFit ShFitGridPoint( const ShCoeffs9& sh );
/// \brief The neutral grid point under -shnofull (design decision 6), for a grid point not in solid.
ShGridFit ShGridNeutralFit();
/// \brief opengl1's realised ambient (post-shift, 0..255) for a grid ambient byte.
float ShGridRealisedAmbient( int ambientByte );
/// \brief opengl1's colour (post-shift, 0..255) of a model face looking straight at the grid direction.
float ShGridRealisedLit( int ambientByte, int directedByte );

// --- 8. Sky environment light ----------------------------------------------

constexpr int c_shSkyWidth = 1024;
constexpr int c_shSkyHeight = 512;
constexpr int c_shSkySubsamples = 4; ///< per axis, per texel
/// rows of sh-baker's Environment::to_environment: texture = c_shSkyToEnvironment x world
constexpr float c_shSkyToEnvironment[3][3] = { { 0, 1, 0 }, { 0, 0, 1 }, { 1, 0, 0 } };

/// \brief The sky data of a shader as skylight.h takes it (rule 8).
skylight::SkySource ShSkySource( const ShShaderDesc& shader );
/// \brief The unit world direction at continuous texel coordinates ( \p x, \p y ) of the equirect texture (a texel's centre is
/// ( x + 0.5, y + 0.5 )).
skylight::Vec3 ShSkyTexelDirection( double x, double y );
/// \brief \p sky resampled into the equirect texture: c_shSkyWidth * c_shSkyHeight * 3 floats (rule 8).
std::vector<float> ShSkyEquirect( const skylight::SkyRadiance& sky );
/// \brief The cross-check error of rule 8: \p textureFrame is the texture's L0-L2 radiance SH in the texture frame,
/// \p derived the derivation's in world space.
double ShSkyCheckError( const skylight::SHCoeffs& derived, const skylight::SHCoeffs& textureFrame );
