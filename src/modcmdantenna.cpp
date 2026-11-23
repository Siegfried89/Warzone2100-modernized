// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Antennen an Fahrzeugen einer Kommandeurs-Gruppe (siehe modcmdantenna.h)
#include "modcmdantenna.h"

#include "lib/framework/vector.h"   // vor jedem glm-Header (GLM_FORCE_SWIZZLE)
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/pielighting.h"
#include "lib/ivis_opengl/piepalette.h"

#include "droid.h"
#include "effects.h"
#include "group.h"
#include "miscimd.h"
#include "modglowlevel.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace modcmdantenna
{
namespace
{
const iIMDShape *antennaShape()
{
	static iIMDBaseShape *s_ant = nullptr;
	static unsigned s_tried = 0;
	if (modelCacheStale(s_tried)) { s_ant = modelGet("modcmdantenna.pie"); }
	return s_ant ? s_ant->displayModel() : nullptr;
}

// schlanke Fassung der Dach-Antenne (Original 23 x 54 x 23): etwa 7 breit, 27 hoch
constexpr float SCALE_XZ = 0.32f, SCALE_Y = 0.50f, ANT_H = 54.f;

struct Spot { float x, y, z; };
std::unordered_map<const iIMDShape *, Spot> s_spots;

/// hinten links auf dem Rumpf: Hoehe der Oberseite an dieser Stelle (hoechster Modellpunkt in der Naehe)
Spot spotOn(const iIMDShape *body)
{
	auto it = s_spots.find(body);
	if (it != s_spots.end()) { return it->second; }
	Spot s;
	s.x = static_cast<float>(body->min.x) * 0.55f;
	s.z = static_cast<float>(body->max.z) * 0.72f;   // Modelle schauen nach -z: hinten = +z
	s.y = static_cast<float>(body->max.y) * 0.8f;
	float best = -1e9f;
	const float rx = std::max(4.f, static_cast<float>(body->max.x - body->min.x) * 0.15f);
	const float rz = std::max(6.f, static_cast<float>(body->max.z - body->min.z) * 0.12f);
	for (const iIMDShape *lvl = body; lvl != nullptr; lvl = lvl->next.get())
	{
		for (const Vector3f &p : lvl->points)
		{
			if (std::fabs(p.x - s.x) < rx && std::fabs(p.z - s.z) < rz) { best = std::max(best, p.y); }
		}
	}
	if (best > -1e8f) { s.y = best - 0.5f; }
	if (s_spots.size() > 512) { s_spots.clear(); }
	s_spots[body] = s;
	return s;
}

/// Blink-Takt je Fahrzeug: 255 = heller Blitz, 40 = kurz dunkel dazwischen, sonst ruhiges Leuchten (-1); Zyklus-Nummer fuer den Funken
int blinkAlpha(uint32_t id, unsigned &cycle)
{
	const unsigned period = 3000u + (id * 7919u) % 4000u;
	const unsigned t = graphicsTime + id * 1237u;
	cycle = t / period;
	const unsigned local = t % period;
	if (local < 90u) { return 255; }
	if (local < 210u) { return 40; }
	if (local < 300u) { return 255; }
	return -1;
}

std::unordered_map<uint32_t, unsigned> s_lastSpark;
}

bool draw(const DROID *d, const iIMDShape *body, const glm::mat4 &bodyMatrix, const glm::mat4 &view, UDWORD colour, PIELIGHT brightness,
          int pieFlag, int pieData, float heightAboveMap)
{
	if (d == nullptr || body == nullptr || d->psGroup == nullptr || d->psGroup->type != GT_COMMAND || d->psGroup->psCommander == d) { return false; }
	if (d->droidType == DROID_PERSON || d->isCyborg() || d->isTransporter()) { return false; }
	const iIMDShape *ant = antennaShape();
	if (ant == nullptr) { return false; }
	const Spot s = spotOn(body);
	const glm::mat4 m = bodyMatrix * glm::translate(glm::mat4(1.f), glm::vec3(s.x, s.y, s.z)) * glm::scale(glm::mat4(1.f), glm::vec3(SCALE_XZ, SCALE_Y, SCALE_XZ));
	unsigned cycle = 0;
	const int blink = blinkAlpha(d->id, cycle);
	modglowlevel::setBlinkAlpha(blink);
	const bool drew = pie_Draw3DShape(ant, 0, colour, brightness, pieFlag, pieData, m, view, -(heightAboveMap + s.y));
	modglowlevel::setBlinkAlpha(-1);
	if (blink == 255 && !(pieFlag & pie_BUTTON))
	{
		const glm::vec3 tip = glm::vec3(bodyMatrix * glm::vec4(s.x, s.y + ANT_H * SCALE_Y, s.z, 1.f));
		// schwaches rotes Licht beim Blinken
		LIGHT l;
		l.position = Vector3i(static_cast<int>(tip.x), static_cast<int>(tip.y), static_cast<int>(-tip.z));
		l.range = 140;
		l.colour = pal_Colour(255, 40, 30);
		l.intensity = 0.9f;
		getCurrentLightingData().lights.push_back(l);
		// elektrischer Funke an der Spitze: einmal je Blink-Zyklus
		auto it = s_lastSpark.find(d->id);
		if (it == s_lastSpark.end() || it->second != cycle)
		{
			if (s_lastSpark.size() > 2048) { s_lastSpark.clear(); }
			s_lastSpark[d->id] = cycle;
			if (const iIMDShape *tesla = getDisplayImdFromIndex(MI_TESLA))
			{
				Vector3i p(static_cast<int>(tip.x), static_cast<int>(tip.y) + 1, static_cast<int>(-tip.z));
				effectSetSize(22 + rand() % 10);
				addEffect(&p, EFFECT_EXPLOSION, EXPLOSION_TYPE_SPECIFIED, true, tesla, 0, graphicsTime);
			}
		}
	}
	return drew;
}
}
