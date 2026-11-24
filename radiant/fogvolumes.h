/*
   The map's fog volumes for the lighting preview (openspec change fog-preview,
   design decision 10), built through libs/fogvolume.h.

   This file is part of NetRadiant-custom, under the GNU General Public License
   version 2 or later.
 */

#pragma once

#include <vector>

#include "fogvolume.h"

/// \brief Registers the scene and shader observers. Called once at startup.
void FogVolumes_construct();
void FogVolumes_destroy();

/// \brief Marks the fog volumes for rebuilding at the next lighting-mode draw (scene change, shader reload, preference change).
void FogVolumes_setDirty();

/// \brief Rebuilds the fog volumes if they are dirty: walks the scene's brushes, picks the fog volumes in q3map2's order and applies
/// the limits, warning as fog-volume requires. Called at the start of each lighting-mode draw; cheap when nothing changed.
void FogVolumes_update();

/// \brief The honoured volumes, at most fogvolume::c_maxVolumes, in order (the last containing volume wins).
const std::vector<fogvolume::Volume>& FogVolumes_volumes();
