/*
   The map's sky environment light for the lighting preview (openspec change
   sky-environment-light): which sky shader the map uses and its L0-L2
   irradiance coefficients, through libs/skylight.h.

   This file is part of NetRadiant-custom, under the GNU General Public License
   version 2 or later.
 */

#pragma once

/// \brief Registers the scene and shader observers. Called once at startup.
void SkyEnvironment_construct();
void SkyEnvironment_destroy();

/// \brief Marks the sky light for recomputation at the next lighting-mode draw (scene change, shader reload, preference change).
void SkyEnvironment_setDirty();

/// \brief Recomputes the sky light if it is dirty: walks the scene for sky shaders, chooses one and projects it. Called at the start of
/// each lighting-mode draw; cheap when nothing changed.
void SkyEnvironment_update();

/// \brief The base pass's sky coefficients: nine RGB values c_i = A_l L_lm / pi in sh-baker's order, so that E( n ) / pi =
/// max( sum c_i Y_i( n ), 0 ). All zero when the map has no sky light or the "Lighting sky" preference is off.
const float* SkyEnvironment_baseCoefficients();

/// \brief Nine zero RGB values, for passes that receive no sky light.
const float* SkyEnvironment_zeroCoefficients();
