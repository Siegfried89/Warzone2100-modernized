// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Atomkraftwerk und Uranbergwerk (siehe modnuke.h)
#include "modnuke.h"

#include <algorithm>
#include <map>

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"

#include "console.h"
#include "effects.h"
#include "game_world.h"
#include "map.h"
#include "modlayer.h"
#include "modoil.h"
#include "power.h"
#include "structure.h"
#include "terrain.h"

#ifndef GLM_ENABLE_EXPERIMENTAL
	#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace modnuke
{
namespace
{
constexpr int FUEL_SECONDS = 90;        // so lange reicht eine Einheit Uran
constexpr int FIRST_LOAD = 2;           // Erstbeladung beim Bau (Einheiten)
constexpr int INCOME_1 = 3;             // Energie je Sekunde mit 1 Kuehlturm, Tag und Nacht gleich
constexpr int INCOME_2 = 5;             // mit 2 Kuehltuermen (Ausbau)
constexpr int GRID_1 = 100, GRID_2 = 150;
constexpr int HYDRO_INCOME = 2, HYDRO_GRID = 60;   // (22:42) Wasserkraftwerk
constexpr int MINE_SECONDS = 30;        // ein Bergwerk im Untergrund foerdert eine Einheit je 30 s
constexpr int STOCK_MAX = 20;           // Uranlager je Spieler
constexpr unsigned WARN_EVERY_S = 60;   // Hinweis "kein Uran" hoechstens jede Minute

// Kuehlturm-Oberkanten im Modell (nuclear.py: TOWERS (64, -62) und beim Ausbau (64, +62), Hoehe 287 * 0,36)
constexpr float TOWER_X = 64.f, TOWER_Z = 62.f, TOWER_TOP = 103.f;

unsigned s_lastSec = 0;
unsigned s_sec = 0;
int s_stock[MAX_PLAYERS] = {};
std::map<uint32_t, int> s_fuel;         // Kraftwerk -> verbleibende Sekunden der aktuellen Uran-Einheit
std::map<uint32_t, int> s_mineSec;      // Bergwerk -> Sekunden bis zur naechsten Einheit
std::map<uint32_t, bool> s_told;        // Hinweise je Bauwerk schon gegeben
unsigned s_warnAt[MAX_PLAYERS] = {};

void say(unsigned player, const char *msg)
{
	if (player == selectedPlayer) { addConsoleMessage(msg, DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
}
}

bool isPlant(const STRUCTURE_STATS *psStats)
{
	return psStats != nullptr && psStats->id.startsWith("A0NuclearPlant");
}

bool isHydro(const STRUCTURE_STATS *psStats)
{
	return psStats != nullptr && psStats->id.startsWith("A0HydroPlant");
}

bool waterNear(const WorldMapState &map, int x0, int y0, int w, int h)
{
	int water = 0;
	for (int y = y0 - 1; y <= y0 + h; ++y)
	{
		for (int x = x0 - 1; x <= x0 + w; ++x)
		{
			if (x < 0 || y < 0 || x >= map.width || y >= map.height) { continue; }
			if (terrainType(mapTile(map, x, y)) == TER_WATER) { ++water; }
		}
	}
	return water >= 2;
}

static bool twoTowers(const STRUCTURE_STATS *psStats)
{
	return psStats != nullptr && psStats->id.compare("A0NuclearPlant2") == 0;
}

int gridCapacity(const STRUCTURE *psStruct)
{
	if (psStruct != nullptr && isHydro(psStruct->pStructureType))
	{
		return (!psStruct->died && psStruct->status == SS_BUILT) ? HYDRO_GRID : 0;
	}
	return running(psStruct) ? (twoTowers(psStruct->pStructureType) ? GRID_2 : GRID_1) : 0;
}

bool isMine(const STRUCTURE_STATS *psStats)
{
	return psStats != nullptr && psStats->id.startsWith("A0UraniumMine");
}

bool running(const STRUCTURE *psStruct)
{
	if (psStruct == nullptr || psStruct->died || psStruct->status != SS_BUILT || !isPlant(psStruct->pStructureType)) { return false; }
	const auto it = s_fuel.find(psStruct->id);
	return it != s_fuel.end() && it->second > 0;
}

int uranium(int player)
{
	return (player >= 0 && player < MAX_PLAYERS) ? s_stock[player] : 0;
}

void tick()
{
	const unsigned sec = gameTime / 1000;
	if (sec == s_lastSec) { return; }
	s_lastSec = sec;
	s_sec++;
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		// Bergwerke: nur im Untergrund
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died || s->status != SS_BUILT || !isMine(s->pStructureType)) { continue; }
			if (modlayer::layerOf(s) != modlayer::UNDERGROUND)
			{
				bool &told = s_told[s->id];
				if (!told) { told = true; say(p, _("Uranium mine: uranium is only found underground - build the mine on the underground level")); }
				continue;
			}
			int &left = s_mineSec.emplace(s->id, MINE_SECONDS).first->second;
			if (--left > 0) { continue; }
			left = MINE_SECONDS;
			if (s_stock[p] < STOCK_MAX) { s_stock[p]++; }
		}
		// Kraftwerke: Uran nachladen, Energie liefern
		int gain = 0;
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (!s->died && s->status == SS_BUILT && isHydro(s->pStructureType)) { gain += HYDRO_INCOME; }   // Wasserkraft: stetig, ohne Brennstoff
		}
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died || s->status != SS_BUILT || !isPlant(s->pStructureType)) { continue; }
			auto it = s_fuel.find(s->id);
			if (it == s_fuel.end())
			{
				if (twoTowers(s->pStructureType))
				{
					it = s_fuel.emplace(s->id, FUEL_SECONDS).first;   // Ausbau: der Reaktor ist schon beladen
					say(p, _("Nuclear power plant: second cooling tower online - more power"));
				}
				else
				{
					it = s_fuel.emplace(s->id, FIRST_LOAD * FUEL_SECONDS).first;   // Erstbeladung
					say(p, _("Nuclear power plant online - steady power day and night (uranium comes from uranium mines underground)"));
				}
			}
			if (it->second <= 0 && s_stock[p] > 0)
			{
				s_stock[p]--;
				it->second = FUEL_SECONDS;
			}
			if (it->second <= 0)
			{
				if (sec >= s_warnAt[p])
				{
					s_warnAt[p] = sec + WARN_EVERY_S;
					say(p, _("Nuclear power plant: out of uranium - build uranium mines underground"));
				}
				continue;
			}
			it->second--;
			gain += twoTowers(s->pStructureType) ? INCOME_2 : INCOME_1;
		}
		if (gain > 0)
		{
			addPower(static_cast<int>(p), gain);
			modoil::noteIncome(static_cast<int>(p), modoil::INC_NUCLEAR, gain);
		}
	}
	// abgerissene / zerstoerte Bauwerke vergessen (alle 30 s)
	if (s_sec % 30 == 0)
	{
		std::map<uint32_t, bool> alive;
		for (unsigned p = 0; p < MAX_PLAYERS; ++p)
		{
			for (const STRUCTURE *s : gameWorld.objects.structures[p]) { if (!s->died) { alive[s->id] = true; } }
		}
		for (auto it = s_fuel.begin(); it != s_fuel.end();) { it = alive.count(it->first) ? std::next(it) : s_fuel.erase(it); }
		for (auto it = s_mineSec.begin(); it != s_mineSec.end();) { it = alive.count(it->first) ? std::next(it) : s_mineSec.erase(it); }
		for (auto it = s_told.begin(); it != s_told.end();) { it = alive.count(it->first) ? std::next(it) : s_told.erase(it); }
	}
}

void draw3D(const glm::mat4 &)
{
	if (s_fuel.empty()) { return; }
	static std::map<uint32_t, unsigned> s_nextPuff;
	const modlayer::Layer view = modlayer::current();
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (!running(s) || !s->visibleForLocalDisplay() || modlayer::layerOf(s) != view) { continue; }
			unsigned &next = s_nextPuff[s->id];
			if (graphicsTime < next) { continue; }
			next = graphicsTime + 140;
			// Kuehlturm-Oberkanten wie das Bauwerk gedreht (Modell: x rechts, z; Welt: y = -z)
			const glm::mat4 m = glm::rotate(glm::mat4(1.f), UNDEG(-s->rot.direction), glm::vec3(0.f, 1.f, 0.f));
			const int ground = map_Height(gameWorld.map, s->pos.x, s->pos.y);
			const int towers = twoTowers(s->pStructureType) ? 2 : 1;
			for (int k = 0; k < towers; ++k)
			{
				const glm::vec4 c = m * glm::vec4(TOWER_X, TOWER_TOP, k == 0 ? -TOWER_Z : TOWER_Z, 1.f);
				const unsigned j = (graphicsTime / 7u + static_cast<unsigned>(k) * 13u + s->id) % 41u;
				Vector3i pp(s->pos.x + static_cast<int>(c.x) + static_cast<int>(j % 9u) * 4 - 16, ground + static_cast<int>(c.y) + 4,
				            s->pos.y - static_cast<int>(c.z) + static_cast<int>(j % 7u) * 4 - 12);
				addEffect(&pp, EFFECT_SMOKE, SMOKE_TYPE_STEAM, false, nullptr, 0);
			}
		}
	}
}

void reset()
{
	s_lastSec = 0;
	s_sec = 0;
	std::fill(std::begin(s_stock), std::end(s_stock), 0);
	std::fill(std::begin(s_warnAt), std::end(s_warnAt), 0u);
	s_fuel.clear();
	s_mineSec.clear();
	s_told.clear();
}
}
