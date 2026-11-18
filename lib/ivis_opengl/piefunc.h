/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2020  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/
/***************************************************************************/
/*
 * piefunc.h
 *
 * type defines for extended image library functions.
 *
 */
/***************************************************************************/

#ifndef _piefunc_h
#define _piefunc_h

#include "lib/framework/frame.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/pieclip.h"
#include <array>

namespace gfx_api
{
	struct texture;
}

void pie_TransColouredTriangle(const std::array<Vector3f, 3> &vrt, PIELIGHT c, const glm::mat4 &modelViewMatrix);
/// MOD (18.11.): wie pie_TransColouredTriangle, aber ein ganzer Dreiecksstreifen in einem Aufruf (additiv, mit Tiefentest);
/// c.byte.a = Deckkraft
void pie_TransColouredTriangleStrip(const Vector3f *vrt, size_t count, PIELIGHT c, const glm::mat4 &modelViewMatrix);

void pie_SetViewingWindow(Vector3i *v, PIELIGHT colour);
/// MOD (17.11.): Kameramarker der Minikarte als freie Dreiecksfolge (xy = 2 Werte je Punkt, rgba = 4 Bytes je Punkt).
/// Ersetzt bis zum naechsten pie_SetViewingWindow das alte Sichtfenster.
void pie_SetViewingMarker(const float *xy, const uint8_t *rgba, int count);
void pie_DrawViewingWindow(const glm::mat4 &modelViewProjectionMatrix);
void pie_ViewingWindow_Shutdown();

void pie_DrawSkybox(float scale, const glm::mat4& projectionMatrix, const glm::mat4 &viewMatrix);
// MOD (16.11.): Orbit-Ansicht - Himmel mit Dunst statt schwarzer Raender und tief unten der Planetenboden ueber die Karte hinaus
void pie_DrawSkyboxHaze(float scale, const glm::mat4& projectionMatrix, const glm::mat4 &viewMatrix, const glm::vec4 &hazeColour);
/// texFile: Bodenbild (wie Orbit-Leere), hazeMix wie im Gelaende-Shader eingebacken; Mittelpunkt/Radien in Modellkoordinaten (x, z)
void pie_DrawOrbitGround(const char *texFile, const glm::vec3 &hazeTint, float hazeMix, float centerX, float centerZ, float rClear, float rFull,
                         float groundY, float uvScale, const glm::mat4 &projectionMatrix, const glm::mat4 &worldViewMatrix,
                         const glm::vec4 &colourMul, const glm::vec4 &hazeColour,
                         const class iV_Image *customImage = nullptr, float uvPeriodX = 0.f, float uvPeriodZ = 0.f);
void pie_OrbitGround_Shutdown();
void pie_Skybox_Init();
void pie_Skybox_Shutdown();
void pie_Skybox_Texture(const char *filename);
gfx_api::texture* pie_Skybox_GetTexture();

// Dev-only tessellation smoke test (set WZ_DEBUG_TESS_TEST=1): draws a tessellated patch overlay
void pie_DebugDrawTessellationTestPatch();

#endif // _piedef_h
