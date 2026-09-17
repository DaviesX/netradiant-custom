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

void ShaderCache_setBumpEnabled( bool enabled );
void ShaderCache_extensionsInitialised();

/// True when the active game uses the pbr material language: lighting mode renders with the PBR programs and HDR resolve.
bool ShaderCache_pbrGame();

/// Called once by the render loop, after the scene passes and before the overlay passes
/// (or at the end if there are no overlays), so the camera can resolve the HDR target.
class RenderResolveHook
{
public:
	virtual void resolve() = 0;
};
void ShaderCache_setResolveHook( RenderResolveHook* hook );

/// Draws a full-screen quad through the tonemap program, sampling \p hdrTexture. GL state is preserved.
void ShaderCache_drawTonemap( unsigned int hdrTexture, float exposure );
