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
/** \file
 *  Extended render routines for 3D rendering.
 */

#include "lib/framework/frame.h"

#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/piefunc.h"
#include "lib/ivis_opengl/piestate.h"
#include "piematrix.h"
#include "lib/ivis_opengl/piemode.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/pieclip.h"
#include "lib/ivis_opengl/pielight_convert.h"
#include "lib/ivis_opengl/png_util.h"
#include <glm/gtc/type_ptr.hpp>
#ifndef GLM_ENABLE_EXPERIMENTAL
	#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/transform.hpp>
#include <array>
#include <vector>

static GFX *skyboxGfx = nullptr;
static GFX *radarViewGfx[2] = { nullptr, nullptr };

#define VW_VERTICES 5

static bool s_viewMarker = false;   // MOD (17.11.): statt des Sichtfensters den Kameramarker zeichnen

void pie_SetViewingMarker(const float *xy, const uint8_t *rgba, int count)
{
	if (!radarViewGfx[0])
	{
		radarViewGfx[0] = new GFX(GFX_COLOUR, 2);
		radarViewGfx[1] = new GFX(GFX_COLOUR, 2);
	}
	if (count < 3) { s_viewMarker = false; return; }
	std::vector<gfx_api::gfxFloat> vert(static_cast<size_t>(count) * 2);
	for (int i = 0; i < count * 2; ++i) { vert[i] = static_cast<gfx_api::gfxFloat>(xy[i]); }
	radarViewGfx[0]->buffers(count, vert.data(), rgba);
	s_viewMarker = true;
}

void pie_SetViewingWindow(Vector3i *v, PIELIGHT colour)
{
	s_viewMarker = false;
	Vector3i pieVrts_insidefill[VW_VERTICES - 1];
	Vector3i pieVrts_outline[VW_VERTICES];

	if (!radarViewGfx[0])
	{
		radarViewGfx[0] = new GFX(GFX_COLOUR, 2);
		radarViewGfx[1] = new GFX(GFX_COLOUR, 2);
	}

	static_assert(VW_VERTICES == 5, "Assumption that VW_VERTICES == 5 invalid. Update the following code.");
	pieVrts_outline[0] = v[1];
	pieVrts_outline[1] = v[0];
	pieVrts_outline[2] = v[2];
	pieVrts_outline[3] = v[3];
	pieVrts_outline[4] = v[1];
	pieVrts_insidefill[0] = v[2];
	pieVrts_insidefill[1] = v[3];
	pieVrts_insidefill[2] = v[0];
	pieVrts_insidefill[3] = v[1];

	gfx_api::gfxFloat vert[VW_VERTICES * 2];
	uint8_t cols[VW_VERTICES * 4];

	// Initialize the RGB values for both, and the alpha for the inside fill
	for (int i = 0; i < VW_VERTICES * 4; i += 4)
	{
		cols[i + 0] = colour.byte.r;
		cols[i + 1] = colour.byte.g;
		cols[i + 2] = colour.byte.b;
		cols[i + 3] = colour.byte.a >> 1;
	}

	// Inside fill
	for (int i = 0; i < (VW_VERTICES - 1); i++)
	{
		vert[i * 2 + 0] = pieVrts_insidefill[i].x;
		vert[i * 2 + 1] = pieVrts_insidefill[i].y;
	}
	radarViewGfx[0]->buffers(VW_VERTICES - 1, vert, cols);

	// Outline
	for (int i = 0; i < VW_VERTICES; i++)
	{
		vert[i * 2 + 0] = pieVrts_outline[i].x;
		vert[i * 2 + 1] = pieVrts_outline[i].y;
	}
	for (int i = 0; i < VW_VERTICES * 4; i += 4) // set the proper alpha for the outline
	{
		cols[i + 3] = colour.byte.a;
	}
	radarViewGfx[1]->buffers(VW_VERTICES, vert, cols);
}

void pie_DrawViewingWindow(const glm::mat4 &modelViewProjectionMatrix)
{
	if (!radarViewGfx[0]) { return; }
	gfx_api::RadarViewInsideFillPSO::get().bind();
	gfx_api::RadarViewInsideFillPSO::get().bind_constants({ modelViewProjectionMatrix });
	radarViewGfx[0]->draw<gfx_api::RadarViewInsideFillPSO>(modelViewProjectionMatrix);
	if (s_viewMarker) { return; }   // MOD: der Marker ist eine einzige Dreiecksfolge (kein Umriss)
	gfx_api::RadarViewOutlinePSO::get().bind();
	gfx_api::RadarViewOutlinePSO::get().bind_constants({ modelViewProjectionMatrix });
	radarViewGfx[1]->draw<gfx_api::RadarViewOutlinePSO>(modelViewProjectionMatrix);
}

void pie_ViewingWindow_Shutdown()
{
	if (radarViewGfx[0])
	{
		delete radarViewGfx[0];
		radarViewGfx[0] = nullptr;
	}
	if (radarViewGfx[1])
	{
		delete radarViewGfx[1];
		radarViewGfx[1] = nullptr;
	}
}

void pie_DebugDrawTessellationTestPatch()
{
	static const bool enabled = []() {
		const char* env = getenv("WZ_DEBUG_TESS_TEST");
		return env != nullptr && *env != '\0' && strcmp(env, "0") != 0;
	}();
	if (!enabled)
	{
		return;
	}
	if (!gfx_api::context::get().supportsTessellationShaders())
	{
		static bool loggedOnce = false;
		if (!loggedOnce)
		{
			debug(LOG_INFO, "WZ_DEBUG_TESS_TEST: tessellation shaders are not supported on this system");
			loggedOnce = true;
		}
		return;
	}
	static bool loggedLevel = false;
	if (!loggedLevel)
	{
		debug(LOG_INFO, "WZ_DEBUG_TESS_TEST: drawing test patch (MAX_TESS_GEN_LEVEL: %d)", (int)gfx_api::context::get().get_context_value(gfx_api::context::context_value::MAX_TESS_GEN_LEVEL));
		loggedLevel = true;
	}
	const float tessLevel = std::min(8.f, static_cast<float>(gfx_api::context::get().get_context_value(gfx_api::context::context_value::MAX_TESS_GEN_LEVEL)));
	// 4 control points in NDC (lower-left area of the screen), CCW winding
	static const std::array<glm::vec2, 4> patchCorners = {
		glm::vec2(-0.9f, -0.9f), glm::vec2(-0.3f, -0.9f), glm::vec2(-0.3f, -0.3f), glm::vec2(-0.9f, -0.3f)
	};
	gfx_api::DebugDrawTessQuad::get().bind();
	gfx_api::DebugDrawTessQuad::get().bind_constants({ glm::mat4(1.f), glm::vec4(0.1f, 0.9f, 0.4f, 0.9f), tessLevel });
	gfx_api::context::get().bind_streamed_vertex_buffers(patchCorners.data(), patchCorners.size() * sizeof(glm::vec2));
	gfx_api::DebugDrawTessQuad::get().draw(4, 0);
	gfx_api::context::get().disable_all_vertex_buffers();
}

void pie_TransColouredTriangle(const std::array<Vector3f, 3> &vrt, PIELIGHT c, const glm::mat4 &modelViewMatrix)
{
	glm::vec4 color(c.byte.r / 255.f, c.byte.g / 255.f, c.byte.b / 255.f, 128.f / 255.f);
	const auto& renderState = getCurrentRenderState();
	const glm::vec4 fogRange(renderState.fogBegin, renderState.fogEnd, pie_GetFogEnabled() ? 1.f : 0.f, gfx_api::context::get().linearLightingEnabled() ? 1.f : 0.f);

	gfx_api::TransColouredTrianglePSO::get().bind();
	gfx_api::TransColouredTrianglePSO::get().bind_constants({
		pie_UIPerspectiveGet() * modelViewMatrix,
		modelViewMatrix,
		color,
		pie_SceneColourToVec4(pie_GetFogColour()),
		fogRange
	});
	gfx_api::context::get().bind_streamed_vertex_buffers(vrt.data(), 3 * sizeof(Vector3f));
	gfx_api::TransColouredTrianglePSO::get().draw(3, 0);
	gfx_api::context::get().disable_all_vertex_buffers();
}

void pie_Skybox_Init()
{
	float narrowingOfTop = 0.15f;
	float top = 1;
	float middle = 0.15f;
	float baseline = -0.15f - 0.3f; // Additional lowering of baseline for where fog starts fading, to reduce certain artifacts
	float bottom = -1;

	// Skybox looks like this from the side:
	//
	//             _____      top, slightly narrower to give perspective
	//            /     .
	//           /       .
	//           +-------+    middle
	//           |       |
	//           +-------+    baseline of map
	//           |       |
	//           +-------+    bottom
	// bottom cap ---^

	// The skybox is shown from baseline to top. below the baseline, the color fades to black (if fog enabled - otherwise a color of the skybox bottom).
	// We extended the skybox downwards to be able to paint more of the screen with fog.

	float narrowCoordinate = 1 - narrowingOfTop;
	const Vector3f
		northWestBottom = Vector3f(-1, bottom, 1), // nw
		northWestMiddle = Vector3f(-1, middle, 1),
		northWestBaseline = Vector3f(-1, baseline, 1),
		northWestTop = Vector3f(-narrowCoordinate, top, narrowCoordinate),
		northEastBottom = Vector3f(1, bottom, 1), // ne
		northEastTop = Vector3f(narrowCoordinate, top, narrowCoordinate),
		northEastMiddle = Vector3f(1, middle, 1),
		northEastBaseline = Vector3f(1, baseline, 1),
		southEastBottom = Vector3f(1, bottom, -1), // se
		southEastTop = Vector3f(narrowCoordinate, top, -narrowCoordinate),
		southEastMiddle = Vector3f(1, middle, -1),
		southEastBaseline = Vector3f(1, baseline, -1),
		southWestBottom = Vector3f(-1, bottom, -1), // sw
		southWestMiddle = Vector3f(-1, middle, -1),
		southWestBaseline = Vector3f(-1, baseline, -1),
		southWestTop = Vector3f(-narrowCoordinate, top, -narrowCoordinate);

	const std::array<Vector3f, 78> vertex{
		// North quads
		northWestMiddle, northWestTop, northEastMiddle,
		northEastTop, northEastMiddle, northWestTop,
		northWestBaseline, northWestMiddle, northEastBaseline,
		northEastMiddle, northEastBaseline, northWestMiddle,
		northWestBottom, northWestBaseline, northEastBottom,
		northEastBaseline, northEastBottom, northWestBaseline,
		// East quads
		northEastMiddle, northEastTop, southEastMiddle,
		southEastTop, southEastMiddle, northEastTop,
		northEastBaseline, northEastMiddle, southEastBaseline,
		southEastMiddle, southEastBaseline, northEastMiddle,
		northEastBottom, northEastBaseline, southEastBottom,
		southEastBaseline, southEastBottom, northEastBaseline,
		// South quads
		southEastMiddle, southEastTop, southWestMiddle,
		southWestTop, southWestMiddle, southEastTop,
		southEastBaseline, southEastMiddle, southWestBaseline,
		southWestMiddle, southWestBaseline, southEastMiddle,
		southEastBottom, southEastBaseline, southWestBottom,
		southWestBaseline, southWestBottom, southEastBaseline,
		// West quads
		southWestMiddle, southWestTop, northWestMiddle,
		northWestTop, northWestMiddle, southWestTop,
		southWestBaseline, southWestMiddle, northWestBaseline,
		northWestMiddle, northWestBaseline, southWestMiddle,
		southWestBottom, southWestBaseline, northWestBottom,
		northWestBaseline, northWestBottom, southWestBaseline,
		// Bottom quads
		southWestBottom, northWestBottom, northEastBottom,
		northEastBottom, southEastBottom, southWestBottom,
	};
	const Vector2f
		uvTopLeft = Vector2f(0, 0.01), // fractions of 0 and 1 to avoid mipmap bleeding
		uvMiddleLeft = Vector2f(0, 1 - middle),
		uvBaselineLeft = Vector2f(0, 0.99),
		uvTopRight = Vector2f(2, 0.01),     // 2 = each side of the skybox contains 2 repeats of the same texture
		uvMiddleRight = Vector2f(2, 1 - middle),
		uvBaselineRight = Vector2f(2, 0.99),
		uvNull = Vector2f(0, 0.99); // = single color, blend in with sides.

	const std::array<Vector2f, 78> texc =
	{
		// North quad
		uvMiddleLeft, uvTopLeft, uvMiddleRight,
		uvTopRight, uvMiddleRight, uvTopLeft,
		uvBaselineLeft, uvMiddleLeft, uvBaselineRight,
		uvMiddleRight, uvBaselineRight, uvMiddleLeft,
		uvNull, uvNull, uvNull, // bottom part has no color.
		uvNull, uvNull, uvNull,
		// East quad
		uvMiddleLeft, uvTopLeft, uvMiddleRight,
		uvTopRight, uvMiddleRight, uvTopLeft,
		uvBaselineLeft, uvMiddleLeft, uvBaselineRight,
		uvMiddleRight, uvBaselineRight, uvMiddleLeft,
		uvNull, uvNull, uvNull, // bottom part has no color.
		uvNull, uvNull, uvNull,
		// West quad
		uvMiddleLeft, uvTopLeft, uvMiddleRight,
		uvTopRight, uvMiddleRight, uvTopLeft,
		uvBaselineLeft, uvMiddleLeft, uvBaselineRight,
		uvMiddleRight, uvBaselineRight, uvMiddleLeft,
		uvNull, uvNull, uvNull, // bottom part has no color.
		uvNull, uvNull, uvNull,
		// South quad
		uvMiddleLeft, uvTopLeft, uvMiddleRight,
		uvTopRight, uvMiddleRight, uvTopLeft,
		uvBaselineLeft, uvMiddleLeft, uvBaselineRight,
		uvMiddleRight, uvBaselineRight, uvMiddleLeft,
		uvNull, uvNull, uvNull, // bottom part has no color.
		uvNull, uvNull, uvNull,
		// Bottom quad
		uvNull, uvNull, uvNull, // bottom cap has no color
		uvNull, uvNull, uvNull,
	};

	gfx_api::context::get().debugStringMarker("Initializing skybox");
	skyboxGfx = new GFX(GFX_TEXTURE, 3);
	skyboxGfx->buffers(78, vertex.data(), texc.data());
}

void pie_Skybox_Texture(const char *filename)
{
	skyboxGfx->loadTexture(filename, gfx_api::texture_type::game_texture);
}

gfx_api::texture* pie_Skybox_GetTexture()
{
	return skyboxGfx != nullptr ? skyboxGfx->getTexture() : nullptr;
}

void pie_Skybox_Shutdown()
{
	delete skyboxGfx;
	skyboxGfx = nullptr;
	pie_OrbitGround_Shutdown();   // MOD
}

// MOD (16.11.): Orbit - Himmel mit erzwungenem Dunst (unabhaengig von der Nebel-Einstellung): unterhalb der Mitte Dunstfarbe,
// nach oben weich in die Himmelstextur. So gibt es keine schwarzen Flaechen mehr unter dem Horizont.
void pie_DrawSkyboxHaze(float scale, const glm::mat4& projectionMatrix, const glm::mat4 &viewMatrix, const glm::vec4 &hazeColour)
{
	if (skyboxGfx == nullptr) { return; }
	const auto& modelViewProjectionMatrix = projectionMatrix * viewMatrix * glm::scale(glm::vec3(scale, scale / 2.f, scale));
	gfx_api::SkyboxPSO::get().bind();
	gfx_api::SkyboxPSO::get().bind_constants({ modelViewProjectionMatrix, glm::vec4(1.f), hazeColour, 1, gfx_api::context::get().linearLightingEnabled() ? 1 : 0 });
	skyboxGfx->draw<gfx_api::SkyboxPSO>(modelViewProjectionMatrix);
}

namespace
{
GFX *orbitGroundGfx = nullptr;
std::string orbitGroundTex;
glm::vec4 orbitGroundKey(0.f);   // Mittelpunkt + Radien, bei Aenderung neu aufbauen
float orbitGroundUvScale = 0.f;
bool orbitGroundTexOk = false;
}

void pie_OrbitGround_Shutdown()
{
	delete orbitGroundGfx;
	orbitGroundGfx = nullptr;
	orbitGroundTex.clear();
	orbitGroundKey = glm::vec4(0.f);
	orbitGroundTexOk = false;
}

// Flache Scheibe tief unter den Inseln: innen klar (Modell-y = 1 -> kein Dunst), zum Rand hin weich in den Dunst (Modell-y = 0).
// Die Hoehe kommt aus der Modellmatrix (y-Skalierung 0). UV = Weltkoordinaten / uvScale, genau wie die Orbit-Leere im Gelaende-Shader.
void pie_DrawOrbitGround(const char *texFile, const glm::vec3 &hazeTint, float hazeMix, float centerX, float centerZ, float rClear, float rFull,
                         float groundY, float uvScale, const glm::mat4 &projectionMatrix, const glm::mat4 &worldViewMatrix,
                         const glm::vec4 &colourMul, const glm::vec4 &hazeColour,
                         const iV_Image *customImage, float uvPeriodX, float uvPeriodZ)
{
	if (texFile == nullptr || uvScale <= 0.f || rFull <= rClear) { return; }
	// MOD (18.11.): eigenes Bild (die verwaschene echte Planetenoberflaeche, endlos gespiegelt) statt der Bilddatei; texFile ist dann der Schluessel
	if (customImage != nullptr && orbitGroundTex != texFile)
	{
		if (orbitGroundGfx == nullptr) { orbitGroundGfx = new GFX(GFX_TEXTURE, 3); }
		orbitGroundTex = texFile;
		orbitGroundTexOk = false;
		iV_Image img;
		if (img.duplicate(*customImage) && img.width() > 0)
		{
			orbitGroundGfx->loadTexture(std::move(img), gfx_api::texture_type::game_texture, std::string("orbitground:custom"));
			orbitGroundTexOk = true;
		}
	}
	const glm::vec4 key(centerX, centerZ, rClear, rFull);
	if (orbitGroundGfx == nullptr)
	{
		orbitGroundGfx = new GFX(GFX_TEXTURE, 3);
	}
	if (orbitGroundTex != texFile)
	{
		orbitGroundTex = texFile;
		orbitGroundTexOk = false;
		iV_Image img;
		if (iV_loadImage_PNG2(texFile, img, true, true) && img.width() > 0 && img.height() > 0)
		{
			// Dunst wie im Gelaende-Shader einbacken: mix(Boden, Dunst, hazeMix)
			unsigned char *px = img.bmp_w();
			const size_t n = static_cast<size_t>(img.width()) * img.height();
			const int ch = static_cast<int>(img.channels());
			for (size_t i = 0; i < n; ++i)
			{
				for (int c = 0; c < 3 && c < ch; ++c)
				{
					const float v = px[i * ch + c] / 255.f;
					const float m = v * (1.f - hazeMix) + hazeTint[c] * hazeMix;
					px[i * ch + c] = static_cast<unsigned char>(std::min(255.f, std::max(0.f, m * 255.f + 0.5f)));
				}
			}
			orbitGroundGfx->loadTexture(std::move(img), gfx_api::texture_type::game_texture, std::string("orbitground:") + texFile);
			orbitGroundTexOk = true;
		}
		else
		{
			debug(LOG_INFO, "Orbit-Boden: Bild fehlt (%s)", texFile);
		}
	}
	if (!orbitGroundTexOk) { return; }
	const float uvKey = uvScale + uvPeriodX * 1.7f + uvPeriodZ * 3.1f;
	if (key != orbitGroundKey || uvKey != orbitGroundUvScale)
	{
		orbitGroundKey = key;
		orbitGroundUvScale = uvKey;
		constexpr int SEG = 72;
		const float radii[3] = { 0.f, rClear, rFull };
		const float flag[3] = { 1.f, 1.f, 0.f };   // 1 = klar, 0 = voller Dunst
		std::vector<Vector3f> v;
		std::vector<Vector2f> t;
		v.reserve(SEG * 3 * 2 * 2);
		t.reserve(SEG * 3 * 2 * 2);
		auto P = [&](int ring, int seg) -> std::pair<Vector3f, Vector2f> {
			const float a = static_cast<float>(seg % SEG) * 6.2831853f / SEG;
			const float x = centerX + std::cos(a) * radii[ring];
			const float z = centerZ + std::sin(a) * radii[ring];
			if (uvPeriodX > 0.f && uvPeriodZ > 0.f)
			{
				return { Vector3f(x, flag[ring], z), Vector2f(x / uvPeriodX, -z / uvPeriodZ) };
			}
			return { Vector3f(x, flag[ring], z), Vector2f(x / uvScale, -z / uvScale) };
		};
		auto tri = [&](const std::pair<Vector3f, Vector2f> &a, const std::pair<Vector3f, Vector2f> &b, const std::pair<Vector3f, Vector2f> &c) {
			// beidseitig (Reihenfolge der Ecken egal fuer das Wegschneiden der Rueckseiten)
			v.push_back(a.first); t.push_back(a.second); v.push_back(b.first); t.push_back(b.second); v.push_back(c.first); t.push_back(c.second);
			v.push_back(a.first); t.push_back(a.second); v.push_back(c.first); t.push_back(c.second); v.push_back(b.first); t.push_back(b.second);
		};
		for (int s = 0; s < SEG; ++s)
		{
			tri(P(0, s), P(1, s), P(1, s + 1));
			tri(P(1, s), P(2, s), P(2, s + 1));
			tri(P(1, s), P(2, s + 1), P(1, s + 1));
		}
		orbitGroundGfx->buffers(static_cast<int>(v.size()), v.data(), t.data());
	}
	const glm::mat4 model = glm::translate(glm::vec3(0.f, groundY, 0.f)) * glm::scale(glm::vec3(1.f, 0.f, 1.f));
	const glm::mat4 mvp = projectionMatrix * worldViewMatrix * model;
	gfx_api::SkyboxPSO::get().bind();
	gfx_api::SkyboxPSO::get().bind_constants({ mvp, colourMul, hazeColour, 1, gfx_api::context::get().linearLightingEnabled() ? 1 : 0 });
	orbitGroundGfx->draw<gfx_api::SkyboxPSO>(mvp);
}

void pie_DrawSkybox(float scale, const glm::mat4& projectionMatrix, const glm::mat4 &viewMatrix)
{
	const auto& modelViewProjectionMatrix = projectionMatrix * viewMatrix * glm::scale(glm::vec3(scale, scale / 2.f, scale));

	const auto &renderState = getCurrentRenderState();
	const glm::vec4 fogColor = pielightToRGBAVec4(renderState.fogColour);

	gfx_api::SkyboxPSO::get().bind();
	gfx_api::SkyboxPSO::get().bind_constants({ modelViewProjectionMatrix, glm::vec4(1.f), fogColor, renderState.fogEnabled, gfx_api::context::get().linearLightingEnabled() ? 1 : 0 });
	skyboxGfx->draw<gfx_api::SkyboxPSO>(modelViewProjectionMatrix);
}
