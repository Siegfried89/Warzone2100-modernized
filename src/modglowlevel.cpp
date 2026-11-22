// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 22.11. 22:54 "Glow maps"): Leucht-Ebenen in Modellen.
//  - Eine Modell-Ebene mit eigenem "TYPE 202" (additiv) und eigener Texturseite, deren Name "glowlvl" enthaelt, ist eine Leucht-Ebene
//    (Mod-Paket glowlevels.py): die leuchtenden Stellen des Modells noch einmal, hauchduenn davor, auf einer Seite, die ausser den
//    Leuchtstellen schwarz ist. Die Engine zeichnet sie additiv und unbeleuchtet - sie leuchtet auch im Schatten und nachts.
//  - "glowlvl-eye": ruhiges Leuchten mit leichtem Flackern, dazu ein kleiner Lichthof mit sehr kurzen, langsam drehenden Strahlen
//    (God-Rays wie beim Combot-Auge; effects/modeyerays.pie) an der Mitte der Ebene - die roten Kybernetik-Augen der Kollektiv-Infanterie.
//  - "glowlvl-pulse": glueht ab und zu auf (alle 6-13 Sekunden gut 2 Sekunden, je Gebaeude zu einem anderen Zeitpunkt) - die blauen
//    Scheiben des Indirect Weapons Lab und die langen roten Scheiben des Laser Optics Lab.
// Nur Anzeige.
#include "modglowlevel.h"

#include "lib/framework/frame.h"
#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/piestate.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace
{
const iIMDShape *raysShape()
{
	static const iIMDShape *shape = nullptr;
	static unsigned gen = 0;
	if (modelCacheStale(gen))
	{
		const iIMDBaseShape *b = modelGet(WzString::fromUtf8("modeyerays.pie"));
		shape = b ? b->displayModel() : nullptr;
	}
	return shape;
}

unsigned seedOf(const glm::mat4 &model)
{
	// Lage des Objekts (grob, damit sich die Phase beim Laufen nicht staendig aendert)
	const int x = static_cast<int>(std::floor(model[3].x / 256.f)), z = static_cast<int>(std::floor(model[3].z / 256.f));
	unsigned h = static_cast<unsigned>(x) * 73856093u ^ static_cast<unsigned>(z) * 19349663u;
	h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
	return h;
}

int glowHook(const iIMDShape *lvl, int pieFlag, const glm::mat4 &model, const glm::mat4 &view)
{
	const std::string &tex = lvl->tilesetTextureFiles[0].texfile;
	if (tex.find("glowlvl") == std::string::npos) { return -1; }
	if (pieFlag & (pie_BUTTON | pie_TRANSLUCENT | pie_PREMULTIPLIED | pie_SHIELD | pie_HEIGHT_SCALED | pie_RAISE)) { return 0; }
	const unsigned seed = seedOf(model);
	const float ph = static_cast<float>(seed % 1000u) * 0.0063f;
	const float t = static_cast<float>(graphicsTime % 3600000u) * 0.001f;
	if (tex.find("pulse") != std::string::npos)
	{
		const unsigned period = 6000u + seed % 7000u;
		const unsigned local = (graphicsTime + seed * 977u) % period;
		constexpr unsigned GLOW_MS = 2200;
		if (local >= GLOW_MS) { return 0; }
		const float x = static_cast<float>(local) / static_cast<float>(GLOW_MS);
		const float env = std::pow(std::sin(x * 3.14159265f), 1.4f);
		const float flick = 0.9f + 0.1f * std::sin(t * 23.f + ph);
		return std::clamp(static_cast<int>(255.f * env * flick), 0, 255);
	}
	// ruhiges Leuchten (Augen): leichtes Flackern
	const int alpha = std::clamp(static_cast<int>(205.f + 35.f * std::sin(t * 6.1f + ph) + 15.f * std::sin(t * 17.3f + ph * 1.7f)), 0, 255);
	if (tex.find("eye") != std::string::npos)
	{
		const iIMDShape *rays = raysShape();
		if (rays != nullptr)
		{
			const glm::vec3 c = (glm::vec3(lvl->min) + glm::vec3(lvl->max)) * 0.5f;
			const glm::vec3 wp = glm::vec3(model * glm::vec4(c, 1.f));
			const float k = 1.f + 0.08f * std::sin(t * 7.3f + ph) + 0.05f * std::sin(t * 17.1f + ph * 0.5f);
			const float sc = glm::length(glm::vec3(model[0])) * k;
			const glm::mat3 camRot(view);
			const glm::mat4 bb = glm::translate(glm::mat4(1.f), wp) * glm::mat4(glm::transpose(camRot))
			                     * glm::rotate(glm::mat4(1.f), t * 0.35f + ph, glm::vec3(0.f, 0.f, 1.f)) * glm::scale(glm::mat4(1.f), glm::vec3(sc));
			pie_Draw3DShape(rays, 0, 0, WZCOL_WHITE, pie_ADDITIVE, std::clamp(alpha - 40, 0, 255), bb, view, 0.f, true);
		}
	}
	return alpha;
}

const bool s_registered = (pie_SetGlowLevelHook(&glowHook), true);
}

namespace modglowlevel
{
/// sicherstellen, dass der Rueckruf eingetragen ist (wird beim Spielstart aufgerufen)
void init()
{
	(void)s_registered;
	pie_SetGlowLevelHook(&glowHook);
}
}
