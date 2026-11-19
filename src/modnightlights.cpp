// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Nachtlichter an Gebaeuden (siehe modnightlights.h)
#include "modnightlights.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/pielighting.h"
#include "lib/ivis_opengl/piepalette.h"

#include "display3d.h"
#include "game_world.h"
#include "map.h"
#include "modlayer.h"
#include "modweather.h"
#include "structure.h"

#include <algorithm>
#include <string>
#include <vector>

#ifndef GLM_ENABLE_EXPERIMENTAL
	#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace modnightlights
{
namespace
{
constexpr size_t MAX_LIGHTS = 36;   // so viele werfen echtes Licht (die naechsten zur Kamera); Lichtpunkte gibt es an allen

enum Kind { K_NONE, K_TOP, K_TOP_BIG, K_CORNERS };

Kind kindOf(const STRUCTURE *s)
{
	const STRUCTURE_STATS *st = s->pStructureType;
	const std::string id = st->id.toUtf8();
	auto has = [&id](const char *k) { return id.find(k) != std::string::npos; };
	if (has("PowerPole") || has("Crane") || has("Gantry") || has("OilStore") || has("Outpost") || has("Substation") || has("Beacon")) { return K_TOP; }
	if (has("ATC") || has("Relay") || has("Radar") || has("Uplink")) { return K_TOP_BIG; }
	switch (st->type)
	{
	case REF_RESOURCE_EXTRACTOR: return K_TOP;
	case REF_HQ: case REF_POWER_GEN: case REF_FACTORY: case REF_CYBORG_FACTORY: case REF_VTOL_FACTORY: case REF_RESEARCH:
	case REF_REPAIR_FACILITY: case REF_COMMAND_CONTROL: case REF_SAT_UPLINK: return K_CORNERS;
	case REF_DEFENSE: return (has("Tower") || has("tow") || has("Tow")) ? K_TOP : K_NONE;
	default: break;
	}
	if (has("Refinery") || has("Cryo") || has("LayerShaft") || has("Storage") || has("NuclearPlant") || has("RocketSilo") || has("HydroPlant") || has("Sat-link")) { return K_CORNERS; }
	if (has("Tower")) { return K_TOP; }
	return K_NONE;
}

struct Spot { glm::vec3 pos; int64_t d2; };
} // namespace

void draw3D(const glm::mat4 &viewMatrix)
{
	if (modlayer::current() != modlayer::SURFACE) { return; }
	const float k = modweather::visualNight();
	if (k < 0.05f) { return; }
	static const iIMDBaseShape *s_shape = nullptr;
	static unsigned s_tried = 0;   // MOD (18.11.): Modell-Generation (Neuladen zwischen Spielen)
	if (modelCacheStale(s_tried))
	{
		s_shape = modelGet(WzString::fromUtf8("modnightlight.pie"));
	}
	if (s_shape == nullptr) { return; }
	const iIMDShape *shape = s_shape->displayModel();
	const glm::mat4 bill(glm::transpose(glm::mat3(viewMatrix)));
	const int alpha = std::clamp(static_cast<int>(235.f * k), 0, 255);
	std::vector<Spot> spots;
	auto light = [&](int wx, int wy, int h, float sc)
	{
		const glm::vec3 pos(static_cast<float>(wx), static_cast<float>(map_Height(gameWorld.map, wx, wy) + h), static_cast<float>(-wy));
		const glm::mat4 bb = glm::translate(glm::mat4(1.f), pos) * glm::mat4(bill) * glm::scale(glm::mat4(1.f), glm::vec3(sc));
		pie_Draw3DShape(shape, 0, 0, WZCOL_WHITE, pie_ADDITIVE, alpha, bb, viewMatrix, 0.f, true);
		const int64_t dx = wx - playerPos.p.x, dy = wy - playerPos.p.z;
		spots.push_back({glm::vec3(static_cast<float>(wx), static_cast<float>(wy), pos.y), dx * dx + dy * dy});
	};
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died || s->status != SS_BUILT || !s->visibleForLocalDisplay() || modlayer::layerOf(s) != modlayer::SURFACE || s->isMobileCore()) { continue; }
			const Kind kd = kindOf(s);
			if (kd == K_NONE) { continue; }
			const iIMDShape *imd = (s->sDisplay.imd != nullptr) ? s->sDisplay.imd->displayModel() : nullptr;
			const int top = imd != nullptr ? std::max(24, imd->max.y) : 80;
			if (kd == K_TOP) { light(s->pos.x, s->pos.y, top + 6, 1.0f); }
			else if (kd == K_TOP_BIG) { light(s->pos.x, s->pos.y, top + 8, 1.35f); }
			else
			{
				// zwei Lichter an diagonal gegenueberliegenden Dachecken
				const int hw = static_cast<int>(s->pStructureType->baseWidth) * TILE_UNITS / 2 - 24;
				const int hb = static_cast<int>(s->pStructureType->baseBreadth) * TILE_UNITS / 2 - 24;
				const int h = top * 4 / 5;
				light(s->pos.x - hw, s->pos.y - hb, h, 0.9f);
				light(s->pos.x + hw, s->pos.y + hb, h, 0.9f);
			}
		}
	}
	if (spots.empty()) { return; }
	// die naechsten zur Kamera beleuchten ihre Umgebung warmweiss
	const size_t nLit = std::min(MAX_LIGHTS, spots.size());
	std::partial_sort(spots.begin(), spots.begin() + static_cast<std::ptrdiff_t>(nLit), spots.end(), [](const Spot &a, const Spot &b) { return a.d2 < b.d2; });
	for (size_t i = 0; i < nLit; ++i)
	{
		LIGHT l;
		l.position = Vector3i(static_cast<int>(spots[i].pos.x), static_cast<int>(spots[i].pos.z), static_cast<int>(spots[i].pos.y));
		l.range = 300;
		l.colour = pal_Colour(255, 206, 140);
		l.intensity = 1.25f * k;
		getCurrentLightingData().lights.push_back(l);
	}
}
}
