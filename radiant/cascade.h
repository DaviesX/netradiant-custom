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

#pragma once

/// \file
/// \brief Cascaded shadow map fitting for the sun: pure matrix and bounds arithmetic, no GL.
/// Ported from sh-renderer src/cascade.cpp so the two can be read side by side; see the pbr gamepack README
/// for which constants are the engine's and which are editor-side.

#include "math/matrix.h"
#include <cstddef>

/// Number of sun cascades. Matches sh-renderer kNumShadowMapCascades.
const std::size_t c_shadowCascadeCount = 3;
/// Texels per cascade edge. Matches sh-renderer kCascadeShadowMapSize.
const std::size_t c_shadowCascadeSize = 1024;
/// Practical split blend between the logarithmic and uniform distributions. Matches sh-renderer.
const float c_shadowCascadeLambda = 0.8f;
/// How far the cascade near plane is pulled back so casters behind the slice still occlude, in map units.
/// Matches sh-renderer's z_padding of 20.
const float c_shadowCascadeNearPadding = 20.f;
/// Editor-side cap on the cascade range, in map units. The camera's own far clip is the world diagonal
/// (~227000 units), which would leave the last cascade at hundreds of units per texel; the reference engine's
/// far plane is a game-scale distance instead. Shadows fade out beyond this, not the camera's far clip.
const float c_shadowDistance = 4096.f;

/// \brief The camera frustum the cascades are fitted to.
struct ShadowCascadeCamera
{
	Vector3 origin;
	Vector3 forward;          ///< view direction, normalised
	Vector3 right;            ///< normalised
	Vector3 up;               ///< normalised
	float halfWidthAtNear;    ///< frustum half-width at nearDistance, in map units
	float halfHeightAtNear;   ///< frustum half-height at nearDistance, in map units
	float nearDistance;
	float farDistance;        ///< already capped to c_shadowDistance by the caller
};

/// \brief The fitted cascades.
struct ShadowCascades
{
	/// View-space distance at which each cascade ends; a fragment picks the first cascade whose split it is within.
	float splits[c_shadowCascadeCount];
	/// World -> light space; shared by every cascade, they differ only in their bounds.
	Matrix4 view;
	/// Orthographic projection fitted to each cascade's slice.
	Matrix4 projection[c_shadowCascadeCount];
	/// projection[i] * view, the matrix the fragment program samples with.
	Matrix4 viewProjection[c_shadowCascadeCount];
};

/// \brief Fills \p out with the split distances and the orthographic light matrices for \p camera.
/// \p lightDirection is the direction the sun's light travels, normalised.
void ShadowCascades_compute( ShadowCascades& out, const ShadowCascadeCamera& camera, const Vector3& lightDirection );

/// \brief An orthographic projection in the OpenGL convention, the counterpart of matrix4_frustum.
Matrix4 matrix4_ortho( float left, float right, float bottom, float top, float nearval, float farval );

/// \brief World -> light view space, for a light at \p origin whose light travels along \p direction.
/// Used for the sun's cascades (with the origin at zero, since the orthographic bounds supply the position)
/// and for a spot light's perspective frustum.
Matrix4 matrix4_light_view( const Vector3& direction, const Vector3& origin );
