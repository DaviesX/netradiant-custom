/* -------------------------------------------------------------------------------

   Pure helpers for the editor's emissive-light commands (openspec change
   emissive-area-lights, design decision 10): "Create area light from selected
   face", "Create proxy light from selection" and "Re-derive light from
   emitter".

   Header-only and free of editor dependencies; tested by
   libs/emissivelight_test.cpp through `make test-emissive-light`.

   - AngleVectors (pitch yaw roll, Quake's libs/mathlib/mathlib.c) and its
   inverse from a forward and right pair;
   - the rectangle test and the created area light's frame;
   - the sRGB decode and the area-weighted footprint mean over a wrapped RGBA8
   image;
   - polygon area, and the clip of a planar polygon to a rectangle;
   - the attachment test of q3map2 -shbake (ShAreaRectAttached in
     tools/quake3/q3map2/light_sh_model.cpp), applied to one brush face;
   - the radiance split into `intensity` and `_color`, the created area light's
     `radius`, and the proxy flux and kind.

   This file is part of NetRadiant-custom, under the GNU General Public License
   version 2 or later.

   -------------------------------------------------------------------------------
 */

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "math/vector.h"

namespace emissivelight
{

constexpr double c_pi = 3.14159265358979323846;
constexpr double c_deg2rad = c_pi / 180.0;
constexpr double c_rad2deg = 180.0 / c_pi;

using Vec2 = BasicVector2<double>;
using Vec3 = DoubleVector3;

// --- Basis ----------------------------------------------------------------

/// forward is the emitting normal; right = forward x up; up = right x forward
struct Basis
{
	Vec3 forward = Vec3( 1, 0, 0 );
	Vec3 right = Vec3( 0, -1, 0 );
	Vec3 up = Vec3( 0, 0, 1 );
};

/// \brief Quake's AngleVectors over \p angles = pitch yaw roll in degrees, positive pitch looking down.
inline Basis AngleVectors( const Vec3& angles ) {
	const double p = angles[0] * c_deg2rad, y = angles[1] * c_deg2rad, r = angles[2] * c_deg2rad;
	const double sp = std::sin( p ), cp = std::cos( p ), sy = std::sin( y ), cy = std::cos( y ), sr = std::sin( r ), cr = std::cos( r );
	Basis b;
	b.forward = Vec3( cp * cy, cp * sy, -sp );
	b.right = Vec3( -sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp );
	b.up = Vec3( cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp );
	return b;
}

namespace detail
{
/// \p degrees wrapped into [0, 360), with -0 and 360 folded to 0
inline double WrapDegrees( double degrees ) {
	double d = std::fmod( degrees, 360.0 );
	if ( d < 0 ) { d += 360.0; }
	if ( d >= 360.0 || d == 0 ) { d = 0; }
	return d;
}
} // namespace detail

/// \brief The inverse of AngleVectors: pitch yaw roll in degrees for the unit, orthogonal \p forward and \p right (up follows as right x
/// forward). Pitch is in [-90, 90], yaw and roll in [0, 360). Straight up or down, where yaw and roll coincide, roll is 0.
inline Vec3 AnglesFromBasis( const Vec3& forward, const Vec3& right ) {
	const double horizontal = std::sqrt( forward[0] * forward[0] + forward[1] * forward[1] );
	const double pitch = std::atan2( -forward[2], horizontal ) * c_rad2deg;
	double yaw, roll;
	if ( horizontal > 1e-9 ) {
		yaw = std::atan2( forward[1], forward[0] ) * c_rad2deg;
		// right.z = -sr * cp, up.z = cr * cp, with cp > 0
		const Vec3 up = vector3_cross( right, forward );
		roll = std::atan2( -right[2], up[2] ) * c_rad2deg;
	}
	else {
		// sp = +-1: right = ( sin( y -+ r ), -cos( y -+ r ), 0 ); take r = 0
		yaw = std::atan2( right[0], -right[1] ) * c_rad2deg;
		roll = 0;
	}
	return Vec3( pitch == 0 ? 0 : pitch, detail::WrapDegrees( yaw ), detail::WrapDegrees( roll ) );
}

/// \brief \p angles as the `angles` key value: "%g" at six significant digits, values within 1e-9 of an integer snapped to it (so no
/// "1e-14" or "-0").
inline std::string FormatAngles( const Vec3& angles ) {
	double v[3];
	for ( int i = 0; i < 3; ++i ) {
		v[i] = angles[i];
		const double r = std::round( v[i] );
		if ( std::fabs( v[i] - r ) < 1e-9 ) { v[i] = r; }
		if ( v[i] == 0 ) { v[i] = 0; }
	}
	char buffer[96];
	std::snprintf( buffer, sizeof( buffer ), "%g %g %g", v[0], v[1], v[2] );
	return buffer;
}

// --- Polygons -------------------------------------------------------------

/// \brief The area of the planar polygon \p poly (any winding).
inline double PolygonArea( const std::vector<Vec3>& poly ) {
	Vec3 n( 0, 0, 0 );
	for ( size_t i = 1; i + 1 < poly.size(); ++i ) n += vector3_cross( poly[i] - poly[0], poly[i + 1] - poly[0] );
	return vector3_length( n ) * 0.5;
}

/// \brief The signed area of the 2D polygon \p poly, positive counter-clockwise.
inline double PolygonArea2D( const std::vector<Vec2>& poly ) {
	double area = 0;
	for ( size_t i = 0; i < poly.size(); ++i ) {
		const Vec2& a = poly[i];
		const Vec2& b = poly[( i + 1 ) % poly.size()];
		area += a[0] * b[1] - b[0] * a[1];
	}
	return area * 0.5;
}

/// \brief The area-weighted centroid of the planar polygon \p poly (its vertex mean if it has no area).
inline Vec3 PolygonCentroid( const std::vector<Vec3>& poly ) {
	Vec3 sum( 0, 0, 0 );
	double total = 0;
	for ( size_t i = 1; i + 1 < poly.size(); ++i ) {
		const double a = vector3_length( vector3_cross( poly[i] - poly[0], poly[i + 1] - poly[0] ) ) * 0.5;
		sum += ( poly[0] + poly[i] + poly[i + 1] ) * ( a / 3.0 );
		total += a;
	}
	if ( total > 0 ) { return sum / total; }
	Vec3 mean( 0, 0, 0 );
	for ( const Vec3& v : poly ) mean += v;
	return poly.empty() ? mean : mean / double( poly.size() );
}

/// a rectangle in space: centre, unit right and up axes, and half extents along them
struct Rect
{
	Vec3 centre = Vec3( 0, 0, 0 );
	Vec3 forward = Vec3( 1, 0, 0 );
	Vec3 right = Vec3( 0, -1, 0 );
	Vec3 up = Vec3( 0, 0, 1 );
	double width = 64;
	double height = 64;

	Vec3 point( const Vec2& p ) const { return centre + right * p[0] + up * p[1]; }
	Vec2 project( const Vec3& v ) const {
		const Vec3 d = v - centre;
		return Vec2( vector3_dot( d, right ), vector3_dot( d, up ) );
	}
};

/// \brief Sutherland-Hodgman clip of the 2D polygon \p poly to [-hw, hw] x [-hh, hh].
inline std::vector<Vec2> ClipToRect2D( std::vector<Vec2> poly, double hw, double hh ) {
	for ( int edge = 0; edge < 4 && !poly.empty(); ++edge ) {
		const int axis = edge / 2;
		const double sign = edge % 2 == 0 ? 1.0 : -1.0;
		const double limit = axis == 0 ? hw : hh;
		std::vector<Vec2> out;
		for ( size_t i = 0; i < poly.size(); ++i ) {
			const Vec2& a = poly[i];
			const Vec2& b = poly[( i + 1 ) % poly.size()];
			const double da = sign * a[axis] - limit, db = sign * b[axis] - limit;
			if ( da <= 0 ) { out.push_back( a ); }
			if ( ( da < 0 && db > 0 ) || ( da > 0 && db < 0 ) ) {
				const double t = da / ( da - db );
				out.push_back( Vec2( a[0] + t * ( b[0] - a[0] ), a[1] + t * ( b[1] - a[1] ) ) );
			}
		}
		poly = std::move( out );
	}
	return poly;
}

struct ClippedPolygon
{
	std::vector<Vec3> points; ///< on the rectangle's plane, in the polygon's winding; empty if no overlap
	double area = 0;          ///< the overlap area
};

/// \brief The part of the planar polygon \p poly that, projected on \p rect's plane, lies inside the rectangle, and its area. The points
/// are returned on the rectangle's plane.
inline ClippedPolygon ClipPolygonToRect( const std::vector<Vec3>& poly, const Rect& rect ) {
	std::vector<Vec2> flat;
	flat.reserve( poly.size() );
	for ( const Vec3& v : poly ) flat.push_back( rect.project( v ) );
	const std::vector<Vec2> clipped = ClipToRect2D( std::move( flat ), rect.width * 0.5, rect.height * 0.5 );
	ClippedPolygon result;
	result.area = std::fabs( PolygonArea2D( clipped ) );
	if ( clipped.size() >= 3 && result.area > 0 ) {
		for ( const Vec2& p : clipped ) result.points.push_back( rect.point( p ) );
	}
	else { result.area = 0; }
	return result;
}

// --- Rectangle test -------------------------------------------------------

constexpr double c_rectCollinear = 0.1;    ///< a vertex this close to the line through its neighbours is removed
constexpr double c_rectAngleDegrees = 0.5; ///< each corner within this of 90 degrees
constexpr double c_rectEdge = 0.1;         ///< opposite edges equal within this

/// \brief \p winding with every vertex that lies within c_rectCollinear of the line through its neighbours removed, repeatedly, so a
/// T-junction vertex on an edge disappears. Coincident vertices are removed too.
inline std::vector<Vec3> RemoveCollinear( std::vector<Vec3> winding ) {
	bool removed = true;
	while ( removed && winding.size() > 3 ) {
		removed = false;
		for ( size_t i = 0; i < winding.size(); ++i ) {
			const Vec3& prev = winding[( i + winding.size() - 1 ) % winding.size()];
			const Vec3& next = winding[( i + 1 ) % winding.size()];
			const Vec3 line = next - prev;
			const double length = vector3_length( line );
			double distance;
			if ( length > 0 ) { distance = vector3_length( vector3_cross( winding[i] - prev, line ) ) / length; }
			else { distance = vector3_length( winding[i] - prev ); }
			if ( distance <= c_rectCollinear ) {
				winding.erase( winding.begin() + i );
				removed = true;
				break;
			}
		}
	}
	return winding;
}

/// \brief The four corners of \p winding, in its order, when it is a rectangle: after RemoveCollinear, exactly four corners, each within
/// c_rectAngleDegrees of 90 degrees, with opposite edges equal within c_rectEdge.
inline std::optional<std::array<Vec3, 4>> RectangleCorners( const std::vector<Vec3>& winding ) {
	const std::vector<Vec3> w = RemoveCollinear( winding );
	if ( w.size() != 4 ) { return std::nullopt; }
	double lengths[4];
	for ( int i = 0; i < 4; ++i ) {
		const Vec3 a = w[( i + 1 ) % 4] - w[i];
		const Vec3 b = w[( i + 3 ) % 4] - w[i];
		const double la = vector3_length( a ), lb = vector3_length( b );
		if ( !( la > 0 ) || !( lb > 0 ) ) { return std::nullopt; }
		const double cosAngle = std::clamp( vector3_dot( a, b ) / ( la * lb ), -1.0, 1.0 );
		if ( std::fabs( std::acos( cosAngle ) * c_rad2deg - 90.0 ) > c_rectAngleDegrees ) { return std::nullopt; }
		lengths[i] = la;
	}
	if ( std::fabs( lengths[0] - lengths[2] ) > c_rectEdge || std::fabs( lengths[1] - lengths[3] ) > c_rectEdge ) { return std::nullopt; }
	return std::array<Vec3, 4>{ w[0], w[1], w[2], w[3] };
}

/// \brief The created area light's rectangle for the rectangular face with corners \p corners and unit outward normal \p normal: forward
/// the normal, right along the edge more nearly perpendicular to world Z (the longer edge when both are equally so, as on a horizontal
/// face), up = right x forward, both flipped if up points down; the centre is the corner mean, put on the plane through the corners.
inline Rect AreaRectFromCorners( const std::array<Vec3, 4>& corners, const Vec3& normal ) {
	Rect rect;
	rect.forward = vector3_normalised( normal );
	Vec3 centre( 0, 0, 0 );
	for ( const Vec3& c : corners ) centre += c;
	centre /= 4.0;
	double planeOffset = 0;
	for ( const Vec3& c : corners ) planeOffset += vector3_dot( c - centre, rect.forward );
	rect.centre = centre + rect.forward * ( planeOffset / 4.0 );

	// the two edge directions, each the mean of its two opposite edges
	const Vec3 e0 = ( ( corners[1] - corners[0] ) + ( corners[2] - corners[3] ) ) * 0.5;
	const Vec3 e1 = ( ( corners[3] - corners[0] ) + ( corners[2] - corners[1] ) ) * 0.5;
	const double z0 = std::fabs( vector3_dot( vector3_normalised( e0 ), Vec3( 0, 0, 1 ) ) );
	const double z1 = std::fabs( vector3_dot( vector3_normalised( e1 ), Vec3( 0, 0, 1 ) ) );
	bool useE0;
	if ( std::fabs( z0 - z1 ) <= 1e-6 ) { useE0 = vector3_length( e0 ) >= vector3_length( e1 ); }
	else { useE0 = z0 < z1; }
	const Vec3& widthEdge = useE0 ? e0 : e1;
	const Vec3& heightEdge = useE0 ? e1 : e0;
	rect.width = vector3_length( widthEdge );
	rect.height = vector3_length( heightEdge );

	// right made exactly perpendicular to forward
	Vec3 right = widthEdge - rect.forward * vector3_dot( widthEdge, rect.forward );
	right = vector3_normalised( right );
	Vec3 up = vector3_cross( right, rect.forward );
	if ( up[2] < 0 ) {
		right = -right;
		up = -up;
	}
	rect.right = right;
	rect.up = vector3_normalised( up );
	return rect;
}

// --- Footprint mean -------------------------------------------------------

/// \brief The piecewise sRGB decode of \p c in [0, 1].
inline double SrgbToLinear( double c ) { return c <= 0.04045 ? c / 12.92 : std::pow( ( c + 0.055 ) / 1.055, 2.4 ); }

/// an RGBA8 image, rows top to bottom, read with wrap in both directions
struct ImageRGBA8
{
	int width = 0;
	int height = 0;
	const std::uint8_t* pixels = nullptr; ///< width * height * 4 bytes

	const std::uint8_t* texel( double s, double t ) const {
		const auto wrap = []( double v, int size ) {
			long i = static_cast<long>( std::floor( v * size ) ) % size;
			return i < 0 ? i + size : i;
		};
		return pixels + ( wrap( t, height ) * width + wrap( s, width ) ) * 4;
	}
};

/// \brief The linear value of a texel: sRGB-decoded RGB, times its alpha when \p alphaWeight (a `GL_SRC_ALPHA GL_ONE` stage).
inline Vec3 TexelLinear( const std::uint8_t* rgba, bool alphaWeight ) {
	const double a = alphaWeight ? rgba[3] / 255.0 : 1.0;
	return Vec3( SrgbToLinear( rgba[0] / 255.0 ) * a, SrgbToLinear( rgba[1] / 255.0 ) * a, SrgbToLinear( rgba[2] / 255.0 ) * a );
}

/// \brief The mean linear texel M over the UV polygon \p uvs of \p image, repeated outside 0 to 1. The polygon is fan-triangulated; each
/// triangle is split on a barycentric grid of N x N cells, N at least two per covered texel along each axis, and each cell is sampled at
/// its centroid with nearest-texel lookup. Triangles are weighted by UV area, which for an affine texture mapping is proportional to
/// surface area. A polygon with no UV area gives the texel at its first vertex. Needs a non-empty image.
inline Vec3 FootprintMean( const ImageRGBA8& image, const std::vector<Vec2>& uvs, bool alphaWeight ) {
	if ( image.width <= 0 || image.height <= 0 || image.pixels == nullptr || uvs.empty() ) { return Vec3( 0, 0, 0 ); }
	Vec3 sum( 0, 0, 0 );
	double total = 0;
	for ( size_t i = 1; i + 1 < uvs.size(); ++i ) {
		const Vec2 a = uvs[0], b = uvs[i], c = uvs[i + 1];
		const double area = std::fabs( ( b[0] - a[0] ) * ( c[1] - a[1] ) - ( c[0] - a[0] ) * ( b[1] - a[1] ) ) * 0.5;
		if ( !( area > 0 ) ) { continue; }
		const double extentS = ( std::max( { a[0], b[0], c[0] } ) - std::min( { a[0], b[0], c[0] } ) ) * image.width;
		const double extentT = ( std::max( { a[1], b[1], c[1] } ) - std::min( { a[1], b[1], c[1] } ) ) * image.height;
		const long n = std::max( 1L, static_cast<long>( std::ceil( 2.0 * std::max( extentS, extentT ) - 1e-9 ) ) );
		const Vec2 du = ( b - a ) / double( n ), dv = ( c - a ) / double( n );
		Vec3 triSum( 0, 0, 0 );
		// cells (i, j) with i + j < n: an upright cell at ( i + 1/3, j + 1/3 ), and for i + j < n - 1 an inverted one at ( i + 2/3, j + 2/3
		// ); n * n cells of equal area
		for ( long j = 0; j < n; ++j ) {
			for ( long k = 0; k + j < n; ++k ) {
				const Vec2 p1 = a + du * ( k + 1.0 / 3.0 ) + dv * ( j + 1.0 / 3.0 );
				triSum += TexelLinear( image.texel( p1[0], p1[1] ), alphaWeight );
				if ( k + j < n - 1 ) {
					const Vec2 p2 = a + du * ( k + 2.0 / 3.0 ) + dv * ( j + 2.0 / 3.0 );
					triSum += TexelLinear( image.texel( p2[0], p2[1] ), alphaWeight );
				}
			}
		}
		sum += triSum * ( area / double( n * n ) );
		total += area;
	}
	if ( !( total > 0 ) ) { return TexelLinear( image.texel( uvs[0][0], uvs[0][1] ), alphaWeight ); }
	return sum / total;
}

/// an affine texture mapping on a plane: uv( p ) = uv0 + ( gs . ( p - p0 ), gt . ( p - p0 ) ), with the gradients in the plane, so a point
/// off the plane maps as its projection on it
struct AffineUV
{
	Vec3 p0 = Vec3( 0, 0, 0 );
	Vec2 uv0 = Vec2( 0, 0 );
	Vec3 gs = Vec3( 0, 0, 0 );
	Vec3 gt = Vec3( 0, 0, 0 );

	Vec2 operator()( const Vec3& p ) const {
		const Vec3 d = p - p0;
		return Vec2( uv0[0] + vector3_dot( gs, d ), uv0[1] + vector3_dot( gt, d ) );
	}
};

/// \brief The affine mapping through three points and their UVs; std::nullopt when they are collinear.
inline std::optional<AffineUV> AffineUVFromTriangle( const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec2& uv0, const Vec2& uv1,
                                                     const Vec2& uv2 ) {
	const Vec3 e1 = p1 - p0, e2 = p2 - p0;
	const double a = vector3_dot( e1, e1 ), b = vector3_dot( e1, e2 ), c = vector3_dot( e2, e2 );
	const double det = a * c - b * b;
	if ( !( std::fabs( det ) > 1e-12 * a * c ) ) { return std::nullopt; }
	// dual basis of ( e1, e2 ) in their plane
	const Vec3 d1 = ( e1 * c - e2 * b ) / det;
	const Vec3 d2 = ( e2 * a - e1 * b ) / det;
	AffineUV map;
	map.p0 = p0;
	map.uv0 = uv0;
	map.gs = d1 * ( uv1[0] - uv0[0] ) + d2 * ( uv2[0] - uv0[0] );
	map.gt = d1 * ( uv1[1] - uv0[1] ) + d2 * ( uv2[1] - uv0[1] );
	return map;
}

/// \brief The emissive radiance L = strength x emissive colour x M.
inline Vec3 EmissiveRadiance( double strength, const Vec3& colour, const Vec3& mean ) {
	return Vec3( strength * colour[0] * mean[0], strength * colour[1] * mean[1], strength * colour[2] * mean[2] );
}

// --- Attachment (q3map2 -shbake) ------------------------------------------

constexpr float c_attachDistance = 1.0f; ///< as c_shAttachDistance (light_sh_model.h)
constexpr float c_attachDegrees = 1.0f;  ///< as c_shAttachDegrees
constexpr float c_attachMinArea = 1e-3f; ///< as c_shAttachMinArea

/// a light_area's rectangle in q3map2's single precision, as ShAreaRect
struct AttachRect
{
	Vector3 centre = Vector3( 0, 0, 0 );
	Vector3 forward = Vector3( 1, 0, 0 );
	Vector3 right = Vector3( 0, -1, 0 );
	Vector3 up = Vector3( 0, 0, 1 );
	float width = 64;
	float height = 64;
};

/// \brief The rectangle of a light_area at \p origin with \p angles, \p width and \p height, rounded to single precision as
/// ShAreaRectFromKeys rounds it.
inline AttachRect AttachRectFromAngles( const Vec3& origin, const Vec3& angles, double width, double height ) {
	const Basis b = AngleVectors( angles );
	const auto f = []( const Vec3& v ) {
		return Vector3( static_cast<float>( v[0] ), static_cast<float>( v[1] ), static_cast<float>( v[2] ) );
	};
	AttachRect rect;
	rect.centre = f( origin );
	rect.forward = f( b.forward );
	rect.right = f( b.right );
	rect.up = f( b.up );
	rect.width = static_cast<float>( width );
	rect.height = static_cast<float>( height );
	return rect;
}

namespace detail
{
/// as ClippedTriangleArea in light_sh_model.cpp
inline double AttachClippedTriangleArea( const std::array<Vector2, 3>& tri, double hw, double hh ) {
	std::vector<Vec2> poly = { Vec2( tri[0][0], tri[0][1] ), Vec2( tri[1][0], tri[1][1] ), Vec2( tri[2][0], tri[2][1] ) };
	return std::fabs( PolygonArea2D( ClipToRect2D( std::move( poly ), hw, hh ) ) );
}
} // namespace detail

/// \brief ShAreaRectAttached (tools/quake3/q3map2/light_sh_model.cpp), operation for operation: the surface's area-weighted normal,
/// oriented by the sum of the vertex \p normals, within c_attachDegrees of forward; every vertex within c_attachDistance of the plane; and
/// some triangle, projected on the plane and clipped to the rectangle, covering more than c_attachMinArea. \p indices holds three per
/// triangle.
inline bool AreaRectAttached( const AttachRect& rect, const std::vector<Vector3>& positions, const std::vector<Vector3>& normals,
                              const std::vector<std::uint32_t>& indices ) {
	double n[3] = { 0, 0, 0 };
	for ( size_t t = 0; t + 2 < indices.size(); t += 3 ) {
		if ( indices[t] >= positions.size() || indices[t + 1] >= positions.size() || indices[t + 2] >= positions.size() ) { return false; }
		const Vector3& a = positions[indices[t]];
		const Vector3 e1 = positions[indices[t + 1]] - a, e2 = positions[indices[t + 2]] - a;
		n[0] += double( e1[1] ) * e2[2] - double( e1[2] ) * e2[1];
		n[1] += double( e1[2] ) * e2[0] - double( e1[0] ) * e2[2];
		n[2] += double( e1[0] ) * e2[1] - double( e1[1] ) * e2[0];
	}
	double side = 0;
	for ( const Vector3& vn : normals ) side += n[0] * vn[0] + n[1] * vn[1] + n[2] * vn[2];
	if ( side < 0 ) { n[0] = -n[0], n[1] = -n[1], n[2] = -n[2]; }
	const double length = std::sqrt( n[0] * n[0] + n[1] * n[1] + n[2] * n[2] );
	if ( !( length > 0 ) || !( side != 0 ) ) { return false; }
	const double cosAngle = ( n[0] * rect.forward[0] + n[1] * rect.forward[1] + n[2] * rect.forward[2] ) / length;
	if ( cosAngle < std::cos( c_attachDegrees * c_deg2rad ) ) { return false; }

	for ( const Vector3& v : positions ) {
		if ( std::fabs( vector3_dot( v - rect.centre, rect.forward ) ) > c_attachDistance ) { return false; }
	}

	const double hw = rect.width * 0.5, hh = rect.height * 0.5;
	const auto project = [&rect]( const Vector3& v ) {
		const Vector3 d = v - rect.centre;
		return Vector2( static_cast<float>( vector3_dot( d, rect.right ) ), static_cast<float>( vector3_dot( d, rect.up ) ) );
	};
	for ( size_t t = 0; t + 2 < indices.size(); t += 3 ) {
		const std::array<Vector2, 3> tri = { project( positions[indices[t]] ), project( positions[indices[t + 1]] ),
			                                 project( positions[indices[t + 2]] ) };
		if ( detail::AttachClippedTriangleArea( tri, hw, hh ) > c_attachMinArea ) { return true; }
	}
	return false;
}

/// \brief The attachment test applied to one brush face: its \p winding (in order, rounded to single precision as q3map2's draw vertices
/// are) fan-triangulated from the first vertex, every vertex normal the face's outward \p normal. The caller checks that the face's shader
/// has an emissive stage.
inline bool FaceAttached( const AttachRect& rect, const std::vector<Vec3>& winding, const Vec3& normal ) {
	std::vector<Vector3> positions, normals;
	const Vector3 n( static_cast<float>( normal[0] ), static_cast<float>( normal[1] ), static_cast<float>( normal[2] ) );
	for ( const Vec3& v : winding ) {
		positions.push_back( Vector3( static_cast<float>( v[0] ), static_cast<float>( v[1] ), static_cast<float>( v[2] ) ) );
		normals.push_back( n );
	}
	std::vector<std::uint32_t> indices;
	for ( std::uint32_t i = 1; i + 1 < winding.size(); ++i ) {
		indices.push_back( 0 );
		indices.push_back( i );
		indices.push_back( i + 1 );
	}
	return AreaRectAttached( rect, positions, normals, indices );
}

// --- Derived keys ---------------------------------------------------------

/// `intensity` and `_color` for a radiance or flux
struct Split
{
	double intensity = 0;          ///< the largest component
	Vec3 colour = Vec3( 1, 1, 1 ); ///< the value divided by it
};

/// \brief \p value split into its largest component and the value divided by it; std::nullopt when the largest component is not positive
/// (nothing to derive).
inline std::optional<Split> SplitRadiance( const Vec3& value ) {
	const double m = std::max( { value[0], value[1], value[2] } );
	if ( !( m > 0 ) || !std::isfinite( m ) ) { return std::nullopt; }
	Split s;
	s.intensity = m;
	s.colour = value / m;
	return s;
}

/// \brief The created area light's `radius`: max( 256, 8 x ceil( 2h / 8 ) ), h the half-diagonal.
inline double AreaLightRadius( double width, double height ) {
	const double h = 0.5 * std::sqrt( width * width + height * height );
	return std::max( 256.0, 8.0 * std::ceil( 2.0 * h / 8.0 ) );
}

/// one overlap of a light_area's Re-derive: area O_f and radiance over the overlap
struct Overlap
{
	double area = 0;
	Vec3 radiance = Vec3( 0, 0, 0 );
};

/// \brief The overlap-weighted mean radiance sum( O_f L_f ) / sum( O_f ); zero when no overlap has area.
inline Vec3 OverlapWeightedRadiance( const std::vector<Overlap>& overlaps ) {
	Vec3 sum( 0, 0, 0 );
	double total = 0;
	for ( const Overlap& o : overlaps ) {
		if ( o.area > 0 ) {
			sum += o.radiance * o.area;
			total += o.area;
		}
	}
	return total > 0 ? sum / total : Vec3( 0, 0, 0 );
}

// --- Proxies --------------------------------------------------------------

/// one gathered emitter surface
struct ProxySurface
{
	Vec3 radiance = Vec3( 0, 0, 0 ); ///< its mean emissive radiance L_f
	double area = 0;                 ///< A_f
	/// The area-weighted mean outward normal sum( A_t n_t ) / A_f over the surface's triangles t, NOT normalised: a unit normal for a
	/// planar face, shorter for a curved patch (about zero for a closed tube). Never pass a patch's normalised normal: DeriveProxy sums
	/// normal * area, which must be sum( A_t n_t ). ProxySurfaceFromTriangles builds it from a tessellation.
	Vec3 normal = Vec3( 0, 0, 1 );
	Vec3 centroid = Vec3( 0, 0, 0 ); ///< area-weighted
	bool allAdditive = false;        ///< every map stage additive: a two-sided billboard, k = 2
};

/// \brief A ProxySurface from a triangle list (a face's fan or a patch's tessellation, wound so that the cross product of ( b - a ) and ( c
/// - a ) points outward): area sum( A_t ), normal sum( A_t n_t ) / A_f and the area-weighted centroid.
inline ProxySurface ProxySurfaceFromTriangles( const Vec3& radiance, bool allAdditive, const std::vector<std::array<Vec3, 3>>& triangles ) {
	ProxySurface s;
	s.radiance = radiance;
	s.allAdditive = allAdditive;
	Vec3 weighted( 0, 0, 0 ), centroid( 0, 0, 0 );
	double area = 0;
	for ( const std::array<Vec3, 3>& t : triangles ) {
		const Vec3 cross = vector3_cross( t[1] - t[0], t[2] - t[0] );
		const double a = 0.5 * vector3_length( cross );
		weighted += cross * 0.5; // A_t n_t
		centroid += ( t[0] + t[1] + t[2] ) * ( a / 3.0 );
		area += a;
	}
	s.area = area;
	if ( area > 0 ) {
		s.normal = weighted / area;
		s.centroid = centroid / area;
	}
	return s;
}

enum class ProxyKind
{
	Spot,  ///< light_spot, cone 90, cone_inner 89, _shadows 0, forward along the mean normal
	Point, ///< light
};

struct Proxy
{
	ProxyKind kind = ProxyKind::Point;
	Vec3 flux = Vec3( 0, 0, 0 ); ///< pi sum( k_f L_f A_f )
	Vec3 origin = Vec3( 0, 0, 0 );
	Vec3 angles = Vec3( 0, 0, 0 ); ///< for a spot: pitch and yaw of the mean normal, roll 0
};

constexpr double c_proxyOffset = 8.0;      ///< the origin moves this far along the mean normal
constexpr double c_proxyNormalRatio = 0.1; ///< |mean normal| at least this of the total area

/// \brief The proxy light of \p surfaces: flux pi sum( k_f L_f A_f ), k 2 for all-additive surfaces and 1 otherwise; origin the
/// area-weighted centroid, moved c_proxyOffset along n = sum( A_f n_f ) when |n| is at least c_proxyNormalRatio of the total area; a spot
/// along n when, in addition, every k is 1, else a point.
inline Proxy DeriveProxy( const std::vector<ProxySurface>& surfaces ) {
	Proxy proxy;
	Vec3 n( 0, 0, 0 ), centroid( 0, 0, 0 );
	double total = 0;
	bool oneSided = true;
	for ( const ProxySurface& s : surfaces ) {
		const double k = s.allAdditive ? 2.0 : 1.0;
		oneSided = oneSided && !s.allAdditive;
		proxy.flux += s.radiance * ( c_pi * k * s.area );
		n += s.normal * s.area;
		centroid += s.centroid * s.area;
		total += s.area;
	}
	if ( !( total > 0 ) ) { return proxy; }
	centroid /= total;
	const double nLength = vector3_length( n );
	const bool facing = nLength >= c_proxyNormalRatio * total && nLength > 0;
	proxy.origin = facing ? centroid + n * ( c_proxyOffset / nLength ) : centroid;
	if ( facing && oneSided ) {
		proxy.kind = ProxyKind::Spot;
		const Vec3 f = n / nLength;
		const double horizontal = std::sqrt( f[0] * f[0] + f[1] * f[1] );
		const double pitch = std::atan2( -f[2], horizontal ) * c_rad2deg;
		const double yaw = horizontal > 1e-9 ? detail::WrapDegrees( std::atan2( f[1], f[0] ) * c_rad2deg ) : 0.0;
		proxy.angles = Vec3( pitch == 0 ? 0 : pitch, yaw, 0 );
	}
	return proxy;
}

} // namespace emissivelight
