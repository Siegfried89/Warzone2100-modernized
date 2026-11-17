// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Raketensilo (siehe modrocket.h)
#include "modrocket.h"

#include <algorithm>
#include <map>
#include <vector>

#include "lib/framework/frame.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/pielighting.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"

#include "component.h"
#include "console.h"
#include "droid.h"
#include "effects.h"
#include "game_world.h"
#include "map.h"
#include "modgrid.h"
#include "modlayer.h"
#include "modlayermap.h"
#include "order.h"
#include "power.h"
#include "structure.h"

#ifndef GLM_ENABLE_EXPERIMENTAL
	#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace modrocket
{
namespace
{
constexpr int ROCKET_COST = 250;
constexpr unsigned BUILD_MS = 75000;
constexpr unsigned COUNTDOWN_MS = 8000;
constexpr unsigned LAUNCH_MS = 7000;      // so lange bis zur Landung im Orbit
constexpr size_t CAPACITY = 3;
constexpr int BOARD_R = DIST_TILE * 7 / 5;
constexpr float ROCKET_H = 230.f;         // Hoehe des Raketenmodells (rocketsilo.py)

enum Stage { ST_ASSEMBLE = 0, ST_READY, ST_COUNTDOWN, ST_LAUNCH };
struct Silo
{
	int stage = ST_ASSEMBLE;
	bool paid = false;
	unsigned progress = 0;
	unsigned t0 = 0;
	std::vector<uint32_t> crew;
	bool told = false;
};
std::map<uint32_t, Silo> s_silo;
std::map<uint32_t, uint32_t> s_aboard;    // Einheit -> Silo

DROID *findDroid(unsigned player, uint32_t id)
{
	for (DROID *d : gameWorld.objects.droids[player]) { if (d->id == id && !d->died) { return d; } }
	return nullptr;
}

/// Landeplatz im Orbit nahe (x, y): naechstes begehbares freies Feld der Orbit-Karte
bool pickLanding(int wx, int wy, Vector2i &out)
{
	if (!modlayermap::worldsExist()) { return false; }
	modlayermap::Scope sc(modlayer::ORBIT);
	UDWORD tx = static_cast<UDWORD>(map_coord(wx)), ty = static_cast<UDWORD>(map_coord(wy));
	if (!pickATileGen(gameWorld, &tx, &ty, LOOK_FOR_EMPTY_TILE, zonedPAT)) { return false; }
	out = Vector2i(world_coord(static_cast<int>(tx)) + TILE_UNITS / 2, world_coord(static_cast<int>(ty)) + TILE_UNITS / 2);
	return true;
}

void landCrew(Silo &S, unsigned player)
{
	const unsigned it = gameTime - deltaGameTime / 2 + 1;
	for (uint32_t id : S.crew)
	{
		s_aboard.erase(id);
		DROID *d = findDroid(player, id);
		if (d == nullptr) { continue; }
		Vector3i p(d->pos.x, d->pos.z + 20, d->pos.y);
		addEffect(&p, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING_HIGH, false, nullptr, 0, it);
		addEffect(&p, EFFECT_EXPLOSION, EXPLOSION_TYPE_SMALL, false, nullptr, 0, it);
	}
	if (player == selectedPlayer && !S.crew.empty())
	{
		addConsoleMessage(_("Rocket: the engineers have landed on an orbit island"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
	}
	S.crew.clear();
}
} // namespace

bool isSilo(const STRUCTURE_STATS *st)
{
	return st != nullptr && st->id.startsWith("A0RocketSilo");
}

bool aboard(const DROID *d)
{
	return d != nullptr && !s_aboard.empty() && s_aboard.count(d->id) != 0;
}

void tick()
{
	std::vector<uint32_t> live;
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died || s->status != SS_BUILT || !isSilo(s->pStructureType)) { continue; }
			live.push_back(s->id);
			Silo &S = s_silo[s->id];
			switch (S.stage)
			{
			case ST_ASSEMBLE:
				if (!S.paid)
				{
					if (getPower(p) < ROCKET_COST) { break; }
					usePower(static_cast<int>(p), ROCKET_COST);
					S.paid = true;
					S.progress = 0;
				}
				if (!modgrid::isPoweredDown(s)) { S.progress += deltaGameTime; }
				if (S.progress >= BUILD_MS)
				{
					S.stage = ST_READY;
					S.told = false;
					if (p == selectedPlayer) { addConsoleMessage(_("Rocket ready: send up to 3 cyborg engineers to the rocket silo"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
				}
				break;
			case ST_READY:
			case ST_COUNTDOWN:
			{
				// Einsteigen: Cyborg-Pioniere am Silo (ohne anderen Auftrag oder mit dem Silo als Ziel)
				for (DROID *d : gameWorld.objects.droids[p])
				{
					if (S.crew.size() >= CAPACITY) { break; }
					if (d->died || d->droidType != DROID_CYBORG_CONSTRUCT || aboard(d) || modlayer::layerOf(d) != modlayer::SURFACE || modlayer::inTransition(d)) { continue; }
					if (iHypot(d->pos.xy() - s->pos.xy()) > BOARD_R) { continue; }
					const bool idle = d->order.type == DORDER_NONE;
					const bool toSilo = (d->order.type == DORDER_MOVE || d->order.type == DORDER_SCOUT) && iHypot(d->order.pos - s->pos.xy()) <= BOARD_R;
					if (!idle && !toSilo) { continue; }
					Vector2i land;
					if (!pickLanding(s->pos.x + static_cast<int>(S.crew.size()) * TILE_UNITS, s->pos.y, land)) { continue; }
					orderDroid(d, DORDER_STOP, ModeImmediate);
					d->selected = false;
					d->pos.x = land.x;
					d->pos.y = land.y;
					modlayer::moveToLayer(d, modlayer::ORBIT);
					initDroidMovement(d);
					s_aboard[d->id] = s->id;
					S.crew.push_back(d->id);
					if (S.stage == ST_READY) { S.stage = ST_COUNTDOWN; S.t0 = gameTime; }
				}
				if (S.stage == ST_COUNTDOWN)
				{
					const unsigned el = gameTime - S.t0;
					if (el >= COUNTDOWN_MS || (S.crew.size() >= CAPACITY && el >= 3000))
					{
						S.stage = ST_LAUNCH;
						S.t0 = gameTime;
						audio_PlayStaticTrack(s->pos.x, s->pos.y, ID_SOUND_EXPLOSION);
						if (p == selectedPlayer) { addConsoleMessage(_("Rocket launched!"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
					}
				}
				break;
			}
			case ST_LAUNCH:
				if (gameTime - S.t0 >= LAUNCH_MS)
				{
					landCrew(S, p);
					S = Silo();   // naechste Rakete
				}
				break;
			}
		}
	}
	// zerstoerte Silos: Mannschaft landet trotzdem (sie sitzt schon in der Kapsel), Eintrag weg
	for (auto it = s_silo.begin(); it != s_silo.end();)
	{
		if (std::find(live.begin(), live.end(), it->first) != live.end()) { ++it; continue; }
		for (uint32_t id : it->second.crew) { s_aboard.erase(id); }
		it = s_silo.erase(it);
	}
}

void draw3D(const glm::mat4 &viewMatrix)
{
	if (s_silo.empty() || modlayer::current() != modlayer::SURFACE) { return; }
	static const iIMDBaseShape *s_rocket = nullptr, *s_flame = nullptr;
	static bool s_tried = false;
	if (!s_tried)
	{
		s_tried = true;
		s_rocket = modelGet(WzString::fromUtf8("modrocket.pie"));
		s_flame = modelGet(WzString::fromUtf8("fxicbm.pie"));
	}
	if (s_rocket == nullptr) { return; }
	static std::map<uint32_t, unsigned> s_nextPuff;
	const glm::mat4 bill(glm::transpose(glm::mat3(viewMatrix)));
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died || s->status != SS_BUILT || !isSilo(s->pStructureType) || !s->visibleForLocalDisplay()) { continue; }
			const auto it = s_silo.find(s->id);
			if (it == s_silo.end()) { continue; }
			const Silo &S = it->second;
			const float ground = static_cast<float>(map_Height(gameWorld.map, s->pos.x, s->pos.y));
			float lift = 0.f;
			bool flame = false, steam = false;
			if (S.stage == ST_ASSEMBLE)
			{
				if (!S.paid) { continue; }
				const float k = std::min(1.f, static_cast<float>(S.progress) / static_cast<float>(BUILD_MS));
				lift = -(1.f - k) * ROCKET_H;            // faehrt beim Bau aus dem Silo hoch
			}
			else if (S.stage == ST_COUNTDOWN) { steam = true; }
			else if (S.stage == ST_LAUNCH)
			{
				const float t = static_cast<float>(graphicsTime - S.t0) / 1000.f;
				lift = t < 0.f ? 0.f : 130.f * t * t;
				flame = true;
				steam = t < 1.5f;
				if (lift > 6000.f) { continue; }
			}
			const glm::vec3 base(static_cast<float>(s->pos.x), ground + lift, static_cast<float>(-s->pos.y));
			const glm::mat4 m = glm::translate(glm::mat4(1.f), base);
			pie_Draw3DShape(s_rocket->displayModel(), 0, getPlayerColour(p), WZCOL_WHITE, 0, 0, m, viewMatrix, 0.f);
			unsigned &next = s_nextPuff[s->id];
			if ((steam || flame) && graphicsTime >= next)
			{
				next = graphicsTime + (flame ? 70u : 220u);
				const int r = flame ? 30 : 50;
				Vector3i pp(s->pos.x + (static_cast<int>(graphicsTime % 37u) - 18) * r / 18, static_cast<int>(base.y) + (flame ? -20 : 10),
				            s->pos.y + (static_cast<int>(graphicsTime % 23u) - 11) * r / 11);
				addEffect(&pp, EFFECT_SMOKE, flame ? SMOKE_TYPE_BILLOW : SMOKE_TYPE_STEAM, false, nullptr, 0);
			}
			if (flame && s_flame != nullptr)
			{
				const float fl = 1.6f + 0.25f * static_cast<float>((graphicsTime / 40u) % 3u);
				const glm::mat4 fm = glm::translate(glm::mat4(1.f), base + glm::vec3(0.f, -10.f, 0.f)) * glm::mat4(bill) * glm::scale(glm::mat4(1.f), glm::vec3(fl));
				pie_Draw3DShape(s_flame->displayModel(), 0, 0, WZCOL_WHITE, pie_ADDITIVE, 230, fm, viewMatrix, 0.f, true);
				LIGHT l;
				l.position = Vector3i(s->pos.x, static_cast<int>(base.y), s->pos.y);
				l.range = 900;
				l.colour = pal_Colour(255, 170, 80);
				l.intensity = 2.5f;
				getCurrentLightingData().lights.push_back(l);
			}
		}
	}
}

void reset()
{
	s_silo.clear();
	s_aboard.clear();
}
}
