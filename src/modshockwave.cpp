// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Druckwelle grosser Explosionen (siehe modshockwave.h)
#include "modshockwave.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/piepalette.h"

#include "display.h"
#include "effects.h"
#include "game_world.h"
#include "map.h"
#include "modlayer.h"
#include "modlayermap.h"

namespace modshockwave
{
namespace
{
struct Wave
{
	int x = 0, y = 0, z = 0;    // Mittelpunkt (Welt), z = Bodenhoehe
	unsigned t0 = 0;            // Start (Grafikzeit)
	unsigned dur = 900;         // Dauer ms
	float maxR = 400.f;         // Endradius (Welt)
	int alpha = 160;
	int dustDone = 0;           // schon ausgeloeste Staubringe
	float yaw = 0.f;
	modlayer::Layer layer = modlayer::SURFACE;
};
std::vector<Wave> s_waves;
constexpr float DUST_AT[3] = { 0.30f, 0.58f, 0.86f };   // Staubringe bei diesen Anteilen des Endradius

/// Staub am Boden rund um den Kreis (Vanilla-Rauch, laeuft mit der Welle nach aussen)
void dustRing(const Wave &w, float r, int k)
{
	const int n = std::clamp(static_cast<int>(r / 55.f), 8, 26) - k * 2;
	const float off = static_cast<float>(rand() % 628) / 100.f;
	for (int i = 0; i < n; ++i)
	{
		const float a = off + 6.2831853f * static_cast<float>(i) / static_cast<float>(n);
		const int px = w.x + static_cast<int>(std::cos(a) * r), py = w.y + static_cast<int>(std::sin(a) * r);
		if (!worldOnMap(gameWorld.map, px, py)) { continue; }
		Vector3i p(px, map_Height(gameWorld.map, px, py) + 10 + rand() % 14, py);
		addEffect(&p, EFFECT_SMOKE, (k == 0 && i % 3 == 0) ? SMOKE_TYPE_BILLOW : SMOKE_TYPE_DRIFTING, false, nullptr, 0);
	}
}
}

void spawn(int worldX, int worldY, int worldZ, int strength)
{
	if (!worldOnMap(gameWorld.map, worldX, worldY)) { return; }
	MAPTILE *t = mapTile(gameWorld.map, map_coord(worldX), map_coord(worldY));
	if (!TEST_TILE_VISIBLE_TO_SELECTEDPLAYER(t)) { return; }
	if (s_waves.size() >= 12) { return; }   // Kettenreaktion: nicht unendlich viele gleichzeitig
	const int m = std::clamp(strength, 1, 10);
	Wave w;
	w.x = worldX; w.y = worldY;
	w.z = std::min(worldZ, map_Height(gameWorld.map, worldX, worldY));
	w.t0 = graphicsTime;
	w.dur = 760u + 45u * static_cast<unsigned>(m);
	w.maxR = 320.f + 62.f * static_cast<float>(m);   // 3 .. 7 Felder
	w.alpha = 120 + 9 * m;
	w.yaw = static_cast<float>(rand() % 360);
	w.layer = modlayermap::worldsExist() ? modlayermap::resident() : modlayer::SURFACE;
	s_waves.push_back(w);
	shakeStart(350u + 60u * static_cast<unsigned>(m));
}

void draw3D(const glm::mat4 &viewMatrix)
{
	if (s_waves.empty()) { return; }
	static iIMDBaseShape *dome = nullptr, *ring = nullptr;
	static unsigned gen = 0;
	if (modelCacheStale(gen))
	{
		dome = modelGet("modshockdome.pie");
		ring = modelGet("modshockring.pie");
	}
	const unsigned now = graphicsTime;
	const modlayer::Layer view = modlayer::current();
	const PIELIGHT tint = pal_RGBA(255, 242, 222, 255);
	for (auto it = s_waves.begin(); it != s_waves.end();)
	{
		Wave &w = *it;
		const float t = static_cast<float>(now - w.t0) / static_cast<float>(w.dur);
		if (t >= 1.f) { it = s_waves.erase(it); continue; }
		const float e = 1.f - std::pow(1.f - t, 2.6f);   // blitzschnell, dann langsamer
		const float r = std::max(10.f, w.maxR * e);
		while (w.dustDone < 3 && e >= DUST_AT[w.dustDone])
		{
			if (view == w.layer) { dustRing(w, w.maxR * DUST_AT[w.dustDone], w.dustDone); }
			++w.dustDone;
		}
		if (view != w.layer || dome == nullptr) { ++it; continue; }
		const float fade = std::pow(1.f - t, 1.5f);
		const float flash = t < 0.12f ? (1.f - t / 0.12f) : 0.f;   // erster Augenblick heller
		const int a = std::clamp(static_cast<int>(static_cast<float>(w.alpha) * fade + 90.f * flash), 0, 255);
		const glm::vec3 c(static_cast<float>(w.x), static_cast<float>(w.z) - 4.f, -static_cast<float>(w.y));
		const float s = r / 100.f;   // Modelle haben Radius 100
		// aeussere Haut (etwas abgeflacht wie eine Bodenwelle) und eine innere, schwaechere Lage
		glm::mat4 m1 = glm::translate(glm::mat4(1.f), c) * glm::rotate(glm::mat4(1.f), glm::radians(w.yaw), glm::vec3(0.f, 1.f, 0.f))
		               * glm::scale(glm::mat4(1.f), glm::vec3(s, s * 0.62f, s));
		pie_Draw3DShape(dome->displayModel(), 0, 0, tint, pie_ADDITIVE, a, m1, viewMatrix, 0.f, true);
		glm::mat4 m2 = glm::translate(glm::mat4(1.f), c) * glm::rotate(glm::mat4(1.f), glm::radians(-w.yaw * 1.3f + 40.f), glm::vec3(0.f, 1.f, 0.f))
		               * glm::scale(glm::mat4(1.f), glm::vec3(s * 0.9f, s * 0.5f, s * 0.9f));
		pie_Draw3DShape(dome->displayModel(), 0, 0, tint, pie_ADDITIVE, a / 2, m2, viewMatrix, 0.f, true);
		// heller Bodenring am Fuss der Welle
		if (ring != nullptr)
		{
			glm::mat4 mr = glm::translate(glm::mat4(1.f), c + glm::vec3(0.f, 7.f, 0.f)) * glm::rotate(glm::mat4(1.f), glm::radians(w.yaw * 0.5f), glm::vec3(0.f, 1.f, 0.f))
			               * glm::scale(glm::mat4(1.f), glm::vec3(s * 1.04f, 1.f, s * 1.04f));
			pie_Draw3DShape(ring->displayModel(), 0, 0, tint, pie_ADDITIVE, std::clamp(a * 5 / 4 + 20, 0, 255), mr, viewMatrix, 0.f, true);
		}
		++it;
	}
}

void reset()
{
	s_waves.clear();
}
}
