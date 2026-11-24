/* -------------------------------------------------------------------------------

   Pure helpers for Quake 3 fog volumes (openspec change fog-preview,
   capability fog-volume): which brushes are fog volumes and in what order,
   which point belongs to which volume, the fog surface, the fog amount and
   the display-space composite, as the stock renderers do it with opengl1 as
   the reference for exact colours.

   Header-only and free of editor dependencies, so that renderer_sh (plan
   task C3a) can reimplement or include it; tested by libs/fogvolume_test.cpp
   through `make test-fog-volume`. The gamepack README's "Fog" section
   describes the same model in words.

   - FogShader: the raw shader data (fogparms, q3map_fogDir, the q3map2
     content classes) filled by the editor's shader parser;
   - ContentSide: q3map2's SetBrushContents choice of a brush's content side;
   - FogOrder: the order q3map2's FinishBrush and MoveBrushesToWorld leave the
     brushes in, which CreateMapFogs walks;
   - ClampColour, ClampDistance: the fogparms clamps of tr_bsp.cpp;
   - FogSurfaceSide: the fog surface (fog.cpp's q3map_fogDir rule, else
     writebsp.cpp's axial order without its visibleHull test);
   - MembershipTolerance, Contains, ChooseVolume: membership, last volume wins;
   - SurfaceT, Fraction, FogAmount: RB_CalcFogTexCoords and R_FogFactor
     (renderergl1) and CalcFog (renderergl2) in rend2's analytic form;
   - Composite: out = (1 - f) pixel + f C, in 8-bit display values;
   - SelectVolumes: the 8-volume and 32-plane limits.

   Coordinates are Z-up map coordinates. Planes are outward: a point x lies
   outside a plane by n . x - dist.

   This file is part of NetRadiant-custom, under the GNU General Public License
   version 2 or later.

   -------------------------------------------------------------------------------
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace fogvolume
{

struct V3
{
	double x = 0, y = 0, z = 0;
	V3() = default;
	V3( double x_, double y_, double z_ ) : x( x_ ), y( y_ ), z( z_ ) {
	}
};

inline V3 operator+( const V3& a, const V3& b ) {
	return V3( a.x + b.x, a.y + b.y, a.z + b.z );
}
inline V3 operator-( const V3& a, const V3& b ) {
	return V3( a.x - b.x, a.y - b.y, a.z - b.z );
}
inline V3 operator*( const V3& a, double s ) {
	return V3( a.x * s, a.y * s, a.z * s );
}
inline double Dot( const V3& a, const V3& b ) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

/// \brief An outward plane: n . x = dist on the plane, n . x - dist > 0 outside.
struct Plane
{
	V3 normal;
	double dist = 0;
};

inline double DistanceOutside( const Plane& plane, const V3& point ) {
	return Dot( plane.normal, point ) - plane.dist;
}

const int c_maxVolumes = 8; ///< volumes honoured
const int c_maxPlanes = 32; ///< planes per volume

/// \brief The raw fog data of one shader, filled by the shader parser.
struct FogShader
{
	bool hasFog = false;      ///< fogparms declared and well formed
	V3 colour;                ///< fogparms ( r g b ), unclamped
	double distance = 0;      ///< fogparms distance, unclamped
	V3 direction;             ///< q3map_fogDir, (0 0 0) when not declared
	bool liquid = false;      ///< surfaceparm water, slime or lava
	bool fog = false;         ///< surfaceparm fog
	bool translucent = false; ///< any surfaceparm q3map2 marks C_TRANSLUCENT
	bool nodraw = false;      ///< surfaceparm nodraw
};

/// \brief One brush side as the helpers see it: its outward plane, its shader's data and whether q3map2 would add it as a bevel.
struct Side
{
	Plane plane;
	FogShader shader;
	bool bevel = false;
};

/// \brief The index of a brush's content side, chosen as q3map2's SetBrushContents (map.cpp) chooses it among the fog-relevant
/// classes: the first side, then a liquid side replaces a non-liquid content side, otherwise a fog side replaces a content side
/// that is neither liquid nor fog. -1 for a brush without sides. (Playerclip and solidity only decide among non-fog shaders.)
inline int ContentSide( const std::vector<Side>& sides ) {
	if ( sides.empty() ) {
		return -1;
	}
	int content = 0;
	for ( std::size_t i = 1; i < sides.size(); ++i ) {
		const FogShader& current = sides[content].shader;
		const FogShader& side = sides[i].shader;
		if ( current.liquid ) {
			continue;
		}
		if ( side.liquid ) {
			content = int( i );
		}
		else if ( current.fog ) {
			continue;
		}
		else if ( side.fog ) {
			content = int( i );
		}
	}
	return content;
}

/// \brief A brush is translucent for q3map2's ordering when any side's shader sets C_TRANSLUCENT (map.cpp, games.cpp).
inline bool Translucent( const std::vector<Side>& sides ) {
	return std::any_of( sides.begin(), sides.end(), []( const Side& side ) { return side.shader.translucent; } );
}

enum class EntityKind
{
	Worldspawn,
	FuncGroup,
	Other, ///< any other entity holding brushes
};

/// \brief A brush's place in the map for ordering: its entity (in entity order) and its index within that entity (map order).
struct BrushKey
{
	int entity = 0;
	int brush = 0;
	bool operator==( const BrushKey& other ) const {
		return entity == other.entity && brush == other.brush;
	}
};

/// \brief One entity for FogOrder: its kind and its brushes' translucency, in map order.
struct OrderEntity
{
	EntityKind kind = EntityKind::Other;
	std::vector<bool> translucent;
};

/// \brief Every brush in the order q3map2 leaves them for CreateMapFogs:
/// 1. the opaque brushes of func_group entities, later groups first, each group's in map order;
/// 2. worldspawn's opaque brushes in reverse map order;
/// 3. worldspawn's translucent brushes in map order;
/// 4. the func_groups' translucent brushes, in group order, each group's in map order;
/// 5. every other entity in entity order, its opaque brushes in reverse map order, then its translucent ones in map order.
inline std::vector<BrushKey> FogOrder( const std::vector<OrderEntity>& entities ) {
	std::vector<BrushKey> order;
	const auto opaqueReversed = [&]( int e ) {
		for ( int b = int( entities[e].translucent.size() ); b-- > 0; ) {
			if ( !entities[e].translucent[b] ) {
				order.push_back( BrushKey{ e, b } );
			}
		}
	};
	const auto pick = [&]( int e, bool translucent ) {
		for ( int b = 0; b < int( entities[e].translucent.size() ); ++b ) {
			if ( entities[e].translucent[b] == translucent ) {
				order.push_back( BrushKey{ e, b } );
			}
		}
	};
	const int count = int( entities.size() );
	for ( int e = count; e-- > 0; ) {
		if ( entities[e].kind == EntityKind::FuncGroup ) {
			pick( e, false );
		}
	}
	for ( int e = 0; e < count; ++e ) {
		if ( entities[e].kind == EntityKind::Worldspawn ) {
			opaqueReversed( e );
			pick( e, true );
		}
	}
	for ( int e = 0; e < count; ++e ) {
		if ( entities[e].kind == EntityKind::FuncGroup ) {
			pick( e, true );
		}
	}
	for ( int e = 0; e < count; ++e ) {
		if ( entities[e].kind == EntityKind::Other ) {
			opaqueReversed( e );
			pick( e, true );
		}
	}
	return order;
}

/// \brief fogparms colour, each channel clamped to [0, 1].
inline V3 ClampColour( const V3& colour ) {
	return V3( std::clamp( colour.x, 0.0, 1.0 ), std::clamp( colour.y, 0.0, 1.0 ), std::clamp( colour.z, 0.0, 1.0 ) );
}

/// \brief fogparms distance, raised to 1 as tr_bsp.cpp does.
inline double ClampDistance( double distance ) {
	return distance < 1 ? 1 : distance;
}

namespace detail
{
/// the axial index of a side: 0..5 for -X, +X, -Y, +Y, -Z, +Z, or -1 for a non-axial side
inline int AxialIndex( const V3& n ) {
	const double e = 1e-6;
	const double ax = std::fabs( n.x ), ay = std::fabs( n.y ), az = std::fabs( n.z );
	if ( std::fabs( ax - 1 ) < e && ay < e && az < e ) {
		return n.x < 0 ? 0 : 1;
	}
	if ( std::fabs( ay - 1 ) < e && ax < e && az < e ) {
		return n.y < 0 ? 2 : 3;
	}
	if ( std::fabs( az - 1 ) < e && ax < e && ay < e ) {
		return n.z < 0 ? 4 : 5;
	}
	return -1;
}

/// side indices ordered with the axial sides in \p axialOrder first (the first side in map order for each axis), then the others in
/// map order
inline std::vector<int> SideOrder( const std::vector<Side>& sides, const int ( &axialOrder )[6] ) {
	std::vector<int> order;
	std::vector<bool> used( sides.size(), false );
	for ( int axis : axialOrder ) {
		for ( std::size_t i = 0; i < sides.size(); ++i ) {
			if ( !used[i] && AxialIndex( sides[i].plane.normal ) == axis ) {
				order.push_back( int( i ) );
				used[i] = true;
				break;
			}
		}
	}
	for ( std::size_t i = 0; i < sides.size(); ++i ) {
		if ( !used[i] ) {
			order.push_back( int( i ) );
		}
	}
	return order;
}
} // namespace detail

/// \brief The index of the volume's fog surface side, or -1 for none.
/// With a non-zero \p direction (the content shader's q3map_fogDir) and some non-bevel side whose outward normal has a negative dot
/// product with it: the first, in the order -X, +X, -Y, +Y, -Z, +Z and then the others in map order, with the most negative dot
/// product (fog.cpp). Otherwise the first non-bevel side, in the order +Z, -Z, +Y, -Y, +X, -X and then the others in map order,
/// whose shader is not nodraw (writebsp.cpp, without its visibleHull test).
inline int FogSurfaceSide( const std::vector<Side>& sides, const V3& direction ) {
	if ( direction.x != 0 || direction.y != 0 || direction.z != 0 ) {
		static const int dirOrder[6] = { 0, 1, 2, 3, 4, 5 };
		double best = 0;
		int chosen = -1;
		for ( int i : detail::SideOrder( sides, dirOrder ) ) {
			if ( sides[i].bevel ) {
				continue;
			}
			const double dot = Dot( direction, sides[i].plane.normal );
			if ( dot < best ) {
				best = dot;
				chosen = i;
			}
		}
		if ( chosen >= 0 ) {
			return chosen;
		}
	}
	static const int axialOrder[6] = { 5, 4, 3, 2, 1, 0 };
	for ( int i : detail::SideOrder( sides, axialOrder ) ) {
		if ( !sides[i].bevel && !sides[i].shader.nodraw ) {
			return i;
		}
	}
	return -1;
}

/// \brief A fog volume ready for evaluation.
struct Volume
{
	std::vector<Plane> planes; ///< outward
	V3 colour;                 ///< clamped
	double distance = 1;       ///< clamped
	bool hasSurface = false;
	Plane surface; ///< the fog surface side's outward plane
	std::string name;
};

/// \brief Builds a volume from a brush whose content side declares fogparms: the planes of every side, the clamped colour and
/// distance of the content shader, and the fog surface. \p content is ContentSide( sides ).
inline Volume MakeVolume( const std::vector<Side>& sides, int content, const std::string& name = std::string() ) {
	Volume volume;
	volume.name = name;
	for ( const Side& side : sides ) {
		volume.planes.push_back( side.plane );
	}
	const FogShader& shader = sides[content].shader;
	volume.colour = ClampColour( shader.colour );
	volume.distance = ClampDistance( shader.distance );
	const int surface = FogSurfaceSide( sides, shader.direction );
	volume.hasSurface = surface >= 0;
	if ( volume.hasSurface ) {
		volume.surface = sides[surface].plane;
	}
	return volume;
}

/// \brief The membership tolerance for a point at view-axis \p depth: ε = 1 + depth / 512 map units.
inline double MembershipTolerance( double depth ) {
	return 1 + std::max( depth, 0.0 ) / 512;
}

/// \brief Whether \p point lies within \p epsilon outside every plane of \p volume.
inline bool Contains( const Volume& volume, const V3& point, double epsilon ) {
	return std::all_of( volume.planes.begin(), volume.planes.end(), [&]( const Plane& plane ) { return DistanceOutside( plane, point ) <= epsilon; } );
}

/// \brief The last volume containing \p point with the tolerance of \p depth, or -1.
inline int ChooseVolume( const std::vector<Volume>& volumes, const V3& point, double depth ) {
	const double epsilon = MembershipTolerance( depth );
	for ( int i = int( volumes.size() ); i-- > 0; ) {
		if ( Contains( volumes[i], point, epsilon ) ) {
			return i;
		}
	}
	return -1;
}

/// \brief t(x): the distance of \p x below the fog surface's plane, positive on the volume's side.
inline double SurfaceT( const Volume& volume, const V3& x ) {
	return -DistanceOutside( volume.surface, x );
}

/// \brief The share of the view ray that is fogged, from t(e) and t(p) (fog-volume "Fog amount").
inline double Fraction( bool hasSurface, double tEye, double tPoint, double epsilon ) {
	if ( !hasSurface ) {
		return 1;
	}
	if ( tEye >= 0 ) {
		return tPoint >= -epsilon ? 1 : 0;
	}
	if ( tPoint < 1 ) {
		return 0;
	}
	return tPoint / ( tPoint - tEye );
}

/// \brief f = sqrt( clamp( depth × fraction / d, 0, 1 ) ) for \p point in \p volume, seen from \p eye along the unit \p forward.
inline double FogAmount( const Volume& volume, const V3& eye, const V3& forward, const V3& point ) {
	const double depth = Dot( point - eye, forward );
	if ( depth <= 0 ) {
		return 0;
	}
	const double fraction = volume.hasSurface
	                          ? Fraction( true, SurfaceT( volume, eye ), SurfaceT( volume, point ), MembershipTolerance( depth ) )
	                          : 1;
	return std::sqrt( std::clamp( depth * fraction / volume.distance, 0.0, 1.0 ) );
}

/// \brief The display-space composite of opengl1's fog pass: (1 − f) pixel + f × 255 × colour, per 8-bit channel (not rounded).
inline V3 Composite( const V3& pixel, double f, const V3& colour ) {
	return pixel * ( 1 - f ) + colour * ( 255 * f );
}

/// \brief One candidate volume for the limits: its plane count and name.
struct Candidate
{
	int planes = 0;
	std::string name;
};

/// \brief The candidates honoured: in order, skipping those with more than c_maxPlanes planes, at most c_maxVolumes of them.
/// \p overPlanes receives the indices skipped for their planes; \p found is the number of candidates within the plane limit.
inline std::vector<int> SelectVolumes( const std::vector<Candidate>& candidates, std::vector<int>& overPlanes, int& found ) {
	std::vector<int> honoured;
	overPlanes.clear();
	found = 0;
	for ( int i = 0; i < int( candidates.size() ); ++i ) {
		if ( candidates[i].planes > c_maxPlanes ) {
			overPlanes.push_back( i );
			continue;
		}
		++found;
		if ( int( honoured.size() ) < c_maxVolumes ) {
			honoured.push_back( i );
		}
	}
	return honoured;
}

/// \brief The volume texture of the resolve shader (design decision 8): RGBA32F, c_texWidth × c_maxVolumes texels, one row per volume.
/// Texel 0: colour, distance. Texel 1: surface plane normal, dist. Texel 2: has surface, plane count, t(e), 0. Texels 3.. : planes
/// (normal, dist). The eye term t(e) is computed here, per frame.
const int c_texWidth = 3 + c_maxPlanes;

inline std::vector<float> PackVolumes( const std::vector<Volume>& volumes, const V3& eye ) {
	std::vector<float> texels( std::size_t( c_texWidth ) * c_maxVolumes * 4, 0.f );
	for ( int v = 0; v < int( volumes.size() ) && v < c_maxVolumes; ++v ) {
		const Volume& volume = volumes[v];
		float* row = &texels[std::size_t( v ) * c_texWidth * 4];
		const auto put = [&]( int texel, double a, double b, double c, double d ) {
			row[texel * 4 + 0] = float( a );
			row[texel * 4 + 1] = float( b );
			row[texel * 4 + 2] = float( c );
			row[texel * 4 + 3] = float( d );
		};
		put( 0, volume.colour.x, volume.colour.y, volume.colour.z, volume.distance );
		put( 1, volume.surface.normal.x, volume.surface.normal.y, volume.surface.normal.z, volume.surface.dist );
		const int planes = std::min( int( volume.planes.size() ), c_maxPlanes );
		put( 2, volume.hasSurface ? 1 : 0, planes, volume.hasSurface ? SurfaceT( volume, eye ) : 1, 0 );
		for ( int p = 0; p < planes; ++p ) {
			const Plane& plane = volume.planes[p];
			put( 3 + p, plane.normal.x, plane.normal.y, plane.normal.z, plane.dist );
		}
	}
	return texels;
}

} // namespace fogvolume
