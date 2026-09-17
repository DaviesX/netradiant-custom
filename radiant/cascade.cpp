/*
   Copyright (C) 2001-2006, William Joseph.
   All Rights Reserved.

   This file is part of GtkRadiant.

   GtkRadiant is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   GtkRadiant is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GtkRadiant; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "cascade.h"

#include <algorithm>
#include <cmath>

Matrix4 matrix4_ortho( float left, float right, float bottom, float top, float nearval, float farval ){
	return Matrix4(
	           2 / ( right - left ),
	           0,
	           0,
	           0,
	           0,
	           2 / ( top - bottom ),
	           0,
	           0,
	           0,
	           0,
	           -2 / ( farval - nearval ),
	           0,
	           -( right + left ) / ( right - left ),
	           -( top + bottom ) / ( top - bottom ),
	           -( farval + nearval ) / ( farval - nearval ),
	           1
	       );
}

Matrix4 matrix4_light_view( const Vector3& direction, const Vector3& origin ){
	const Vector3 f = vector3_normalised( direction );
	// any reference up that is not parallel to the light
	const Vector3 reference = ( std::fabs( f.z() ) > 0.99f ) ? Vector3( 0, 1, 0 ) : Vector3( 0, 0, 1 );
	const Vector3 s = vector3_normalised( vector3_cross( f, reference ) ); // right
	const Vector3 u = vector3_cross( s, f );                               // up
	// rows are the light space axes; view space looks along -z, so the third row is -f
	return Matrix4(
	           s.x(), u.x(), -f.x(), 0,
	           s.y(), u.y(), -f.y(), 0,
	           s.z(), u.z(), -f.z(), 0,
	           -vector3_dot( s, origin ), -vector3_dot( u, origin ), vector3_dot( f, origin ), 1
	       );
}

namespace
{

/// \brief The eight world space corners of the camera frustum between \p nearDist and \p farDist.
void sliceCorners( Vector3 ( &corners )[8], const ShadowCascadeCamera& camera, float nearDist, float farDist ){
	const float tanX = camera.halfWidthAtNear / camera.nearDistance;
	const float tanY = camera.halfHeightAtNear / camera.nearDistance;
	const float dists[2] = { nearDist, farDist };
	std::size_t n = 0;
	for ( float dist : dists )
	{
		const Vector3 centre = camera.origin + camera.forward * dist;
		const Vector3 x = camera.right * ( tanX * dist );
		const Vector3 y = camera.up * ( tanY * dist );
		corners[n++] = centre - x - y;
		corners[n++] = centre + x - y;
		corners[n++] = centre - x + y;
		corners[n++] = centre + x + y;
	}
}

} // namespace

void ShadowCascades_compute( ShadowCascades& out, const ShadowCascadeCamera& camera, const Vector3& lightDirection ){
	const float nearDist = std::max( camera.nearDistance, 0.01f );
	const float farDist = std::max( camera.farDistance, nearDist + 1.f );

	// practical split scheme: blend the logarithmic and uniform distributions
	for ( std::size_t i = 0; i < c_shadowCascadeCount; ++i )
	{
		const float p = float( i + 1 ) / float( c_shadowCascadeCount );
		const float logSplit = nearDist * std::pow( farDist / nearDist, p );
		const float uniformSplit = nearDist + ( farDist - nearDist ) * p;
		out.splits[i] = c_shadowCascadeLambda * logSplit + ( 1.f - c_shadowCascadeLambda ) * uniformSplit;
	}

	// the cascade bounds supply the light's position, so the view is a pure rotation here
	const Matrix4 toLight = matrix4_light_view( lightDirection, Vector3( 0, 0, 0 ) );
	out.view = toLight;

	float sliceNear = nearDist;
	for ( std::size_t i = 0; i < c_shadowCascadeCount; ++i )
	{
		const float sliceFar = out.splits[i];

		Vector3 corners[8];
		sliceCorners( corners, camera, sliceNear, sliceFar );

		// axis-aligned bounds of the slice in light space
		Vector3 lightMin( matrix4_transformed_point( toLight, corners[0] ) );
		Vector3 lightMax( lightMin );
		for ( std::size_t c = 1; c < 8; ++c )
		{
			const Vector3 p = matrix4_transformed_point( toLight, corners[c] );
			for ( std::size_t axis = 0; axis < 3; ++axis )
			{
				lightMin[axis] = std::min( lightMin[axis], p[axis] );
				lightMax[axis] = std::max( lightMax[axis], p[axis] );
			}
		}

		// snap the centre to the cascade's texel grid so edges do not crawl as the camera moves
		const float unitsPerTexelX = std::max( ( lightMax.x() - lightMin.x() ) / float( c_shadowCascadeSize ), 1e-6f );
		const float unitsPerTexelY = std::max( ( lightMax.y() - lightMin.y() ) / float( c_shadowCascadeSize ), 1e-6f );
		const float centreX = std::floor( ( ( lightMin.x() + lightMax.x() ) * 0.5f ) / unitsPerTexelX ) * unitsPerTexelX;
		const float centreY = std::floor( ( ( lightMin.y() + lightMax.y() ) * 0.5f ) / unitsPerTexelY ) * unitsPerTexelY;
		// snapping moves the centre by up to one texel while the extents stay put, which would push the far
		// corners of the slice a fraction of a texel outside the map, where they would sample as lit; give the
		// extents that texel back
		const float extentX = ( lightMax.x() - lightMin.x() ) * 0.5f + unitsPerTexelX;
		const float extentY = ( lightMax.y() - lightMin.y() ) * 0.5f + unitsPerTexelY;

		// light space looks along -z, so the near plane is the least negative depth; pad it so casters
		// between the light and the slice are still rendered
		const float nearPlane = -( lightMax.z() + c_shadowCascadeNearPadding );
		const float farPlane = -lightMin.z();

		const Matrix4 projection = matrix4_ortho( centreX - extentX, centreX + extentX,
		                                          centreY - extentY, centreY + extentY,
		                                          nearPlane, farPlane );

		out.projection[i] = projection;
		out.viewProjection[i] = matrix4_multiplied_by_matrix4( projection, toLight );

		sliceNear = sliceFar;
	}
}
