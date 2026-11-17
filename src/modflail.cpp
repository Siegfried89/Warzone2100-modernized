// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Minenraeumer vor dem Bug (siehe modflail.h)
#include "modflail.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "lib/framework/frame.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/piedef.h"
#include "droid.h"
#include "effects.h"
#include "game_world.h"
#include "map.h"
#include "modmines.h"
#ifndef GLM_ENABLE_EXPERIMENTAL
	#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>

namespace modflail
{
namespace
{
constexpr float DRUM_Y = 8.f, DRUM_Z = -14.f;    // Walzenachse relativ zur Bugspitze am Boden (flail.py)
constexpr float REF_HALF_W = 20.5f;              // Modell-Masse fuer diese halbe Fahrzeugbreite (Python-Ketten)
const iIMDShape *s_still = nullptr, *s_arms = nullptr, *s_spin = nullptr;
bool s_loaded = false;
std::unordered_map<unsigned, unsigned> s_dust;   // Einheit -> letzte Staubwolke (Bildzeit)

const iIMDShape *shape(const char *name)
{
	const iIMDBaseShape *b = modelGet(WzString::fromUtf8(name));
	return b != nullptr ? b->displayModel() : nullptr;
}
} // namespace

bool draw(const DROID *d, const iIMDShape *body, const iIMDShape *prop, const glm::mat4 &hull, const glm::mat4 &view,
          int colour, PIELIGHT brightness, int pieFlag, int pieData, float hat, bool button, bool &didDraw)
{
	if (d == nullptr || !modmines::isFlail(d)) { return false; }
	if (!s_loaded)
	{
		s_loaded = true;
		s_still = shape("modflail.pie");
		s_arms = shape("modflail_arms.pie");
		s_spin = shape("modflail_spin.pie");
	}
	if (s_still == nullptr) { return false; }
	// Bugspitze und Breite des Fahrzeugs (vorn = -z)
	float front = 0.f, halfW = REF_HALF_W;
	bool have = false;
	for (const iIMDShape *s : {body, prop})
	{
		if (s == nullptr) { continue; }
		const float w = static_cast<float>(std::max(std::abs(s->min.x), std::abs(s->max.x)));
		front = have ? std::min(front, static_cast<float>(s->min.z)) : static_cast<float>(s->min.z);
		halfW = have ? std::max(halfW, w) : w;
		have = true;
	}
	const float sc = std::clamp(halfW / REF_HALF_W, 0.55f, 1.6f);
	const glm::mat4 base = hull * glm::translate(glm::vec3(0.f, 0.f, front)) * glm::scale(glm::vec3(sc, sc, sc));
	const bool spinning = !button && s_arms != nullptr && s_spin != nullptr && modmines::flailing(d);
	if (!spinning)
	{
		if (pie_Draw3DShape(s_still, 0, colour, brightness, pieFlag, pieData, base, view, -hat)) { didDraw = true; }
		return true;
	}
	// Walze dreht (2,5 Umdrehungen je Sekunde; oben nach vorn - die Ketten schlagen vor der Walze auf den Boden)
	const float ang = -static_cast<float>(graphicsTime % 400u) / 400.f * 6.2831853f;
	const glm::mat4 drum = base * glm::translate(glm::vec3(0.f, DRUM_Y, DRUM_Z)) * glm::rotate(ang, glm::vec3(1.f, 0.f, 0.f));
	if (pie_Draw3DShape(s_arms, 0, colour, brightness, pieFlag, pieData, base, view, -hat)) { didDraw = true; }
	if (pie_Draw3DShape(s_spin, 0, colour, brightness, pieFlag, pieData, drum, view, -hat)) { didDraw = true; }
	// Staub und Erdklumpen vor der Walze (nur Anzeige, gedrosselt)
	if (s_dust.size() > 300) { s_dust.clear(); }
	unsigned &last = s_dust[d->id];
	if (graphicsTime - last > 170u || last > graphicsTime)
	{
		last = graphicsTime;
		const int ahead = static_cast<int>(-front + (-DRUM_Z + 4.f) * sc);
		const int side = static_cast<int>((static_cast<float>(graphicsTime % 7u) - 3.f) * 5.f * sc);
		const int x = d->pos.x + iSinR(d->rot.direction, ahead) + iCosR(d->rot.direction, side);
		const int y = d->pos.y + iCosR(d->rot.direction, ahead) - iSinR(d->rot.direction, side);
		if (worldOnMap(gameWorld.map, x, y))
		{
			Vector3i p(x, map_Height(gameWorld.map, x, y) + 4, y);
			addEffect(&p, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING_SMALL, false, nullptr, 0, graphicsTime);
		}
	}
	return true;
}
}
