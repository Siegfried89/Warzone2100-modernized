// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Energiewirtschaft (siehe modlava.h)

#include "modlava.h"
#include "modweather.h"   // (20:36) Tag/Nacht

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"

#include "baseobject.h"
#include "console.h"
#include "droid.h"
#include "feature.h"
#include "game_world.h"
#include "map.h"
#include "modbuild.h"
#include "modlayer.h"
#include "modlayerdig.h"
#include "modlayermap.h"
#include "modoil.h"
#include "multiplay.h"
#include "order.h"
#include "orderdef.h"
#include "power.h"
#include "random.h"
#include "stats.h"
#include "structure.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace modlava
{
namespace
{
constexpr int LAVA_MJ = 4000;              // Vorrat je Feld
constexpr int MINE_RATE = 3;               // MJ je Sekunde je Hover-Truck
constexpr int BASE_CAP = 4000;             // Grundobergrenze (MJ) mit Fass-Wirtschaft
constexpr int REACH = 128 + 192;           // Abstand Truck - Feldmitte, ab dem geschuerft wird (halbes Feld + 1,5 Felder)
constexpr int MIN_START_DIST = 12 * 128;   // Mindestabstand zu Gebaeuden bei der Verteilung

std::map<uint32_t, int> s_left;            // Feld-Id -> Rest (MJ)
unsigned s_lastSec = 0;
unsigned s_sec = 0;                        // Sekundenzaehler ("unter Tage nur jede vierte Sekunde")
bool s_placed = false;
bool s_placedUnder = false;
int s_cap[MAX_PLAYERS] = {};

bool isLavaStats(const FEATURE_STATS *fs)
{
	return fs != nullptr && fs->id.startsWith("A0LavaField");
}

FEATURE_STATS *lavaStats()
{
	for (FEATURE_STATS &fs : asFeatureStats)
	{
		if (isLavaStats(&fs)) { return &fs; }
	}
	return nullptr;
}

bool freeSpot(const WorldMapState &m, int tx, int ty)
{
	for (int y = ty; y < ty + 2; y++)
	{
		for (int x = tx; x < tx + 2; x++)
		{
			const MAPTILE *t = mapTile(m, x, y);
			if (t == nullptr || TileIsOccupied(t)) { return false; }
			const unsigned char tt = terrainType(t);
			if (tt == TER_CLIFFFACE || tt == TER_WATER) { return false; }
		}
	}
	return true;
}

bool nearAnyStructure(GameWorld &world, int wx, int wy)
{
	const int64_t lim = static_cast<int64_t>(MIN_START_DIST) * MIN_START_DIST;
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		for (const STRUCTURE *s : world.objects.structures[p])
		{
			const int64_t dx = s->pos.x - wx, dy = s->pos.y - wy;
			if (dx * dx + dy * dy < lim) { return true; }
		}
	}
	return false;
}

void placeFields(GameWorld &world)
{
	FEATURE_STATS *fs = lavaStats();
	if (fs == nullptr) { return; }
	for (const FEATURE *f : world.objects.features[0])
	{
		if (isLavaStats(f->psStats)) { return; }          // Karte (oder Spielstand) hat schon Lavafelder
	}
	const int w = world.map.width, h = world.map.height;
	if (w < 24 || h < 24) { return; }
	const int wanted = (std::clamp(w * h / 1500, 3, 40) + 1) / 2;   // je zur Haelfte Oberflaeche / Untergrund (Untergrund-Haelfte siehe placeUnderground)
	int placed = 0;
	for (int attempt = 0; attempt < wanted * 60 && placed < wanted; attempt++)
	{
		const int tx = 6 + static_cast<int>(gameRand(static_cast<uint32_t>(w - 12)));
		const int ty = 6 + static_cast<int>(gameRand(static_cast<uint32_t>(h - 12)));
		if (!freeSpot(world.map, tx, ty)) { continue; }
		const int wx = world_coord(tx + 1), wy = world_coord(ty + 1);
		if (nearAnyStructure(world, wx, wy)) { continue; }
		if (buildFeature(world, fs, static_cast<UDWORD>(wx), static_cast<UDWORD>(wy), false) != nullptr) { placed++; }
	}
}

// Untergrund-Haelfte der Lavafelder: wird erst beim ersten fertigen Schacht angelegt (vorher aendert sich im Spiel nichts, die Ebenen bleiben unbenutzt).
// Die Felder liegen im Fels (nur dort, wo noch nichts gebohrt ist); erst ein angrenzender Gang macht sie sichtbar und erreichbar.
void placeUnderground(GameWorld &world)
{
	FEATURE_STATS *fs = lavaStats();
	if (fs == nullptr || !modlayermap::worldsExist()) { return; }
	const int w = world.map.width, h = world.map.height;
	if (w < 24 || h < 24) { return; }
	const int wanted = std::clamp(w * h / 1500, 3, 40) / 2;
	int placed = 0;
	std::vector<FEATURE *> made;
	{
		modlayermap::Scope sc(modlayer::UNDERGROUND);
		for (int attempt = 0; attempt < wanted * 80 && placed < wanted; attempt++)
		{
			const int tx = 6 + static_cast<int>(gameRand(static_cast<uint32_t>(w - 12)));
			const int ty = 6 + static_cast<int>(gameRand(static_cast<uint32_t>(h - 12)));
			bool ok = true;
			for (int y = ty; y < ty + 2 && ok; y++)
			{
				for (int x = tx; x < tx + 2 && ok; x++)
				{
					const MAPTILE *t = mapTile(world.map, x, y);
					ok = t != nullptr && !TileIsOccupied(t) && terrainType(t) == TER_CLIFFFACE;   // massiver Fels
				}
			}
			if (!ok) { continue; }
			const int wx = world_coord(tx + 1), wy = world_coord(ty + 1);
			if (nearAnyStructure(world, wx, wy)) { continue; }   // Aufzugskammern und Basen bleiben frei
			FEATURE *f = buildFeature(world, fs, static_cast<UDWORD>(wx), static_cast<UDWORD>(wy), false);
			if (f != nullptr) { made.push_back(f); placed++; }
		}
	}
	for (FEATURE *f : made) { modlayer::assignLayer(f, modlayer::UNDERGROUND); }
}

// (16.11. 19:37) Landfill-Kulissen (Mauerring mit Schrotthaufen und Autowracks, Mod-Features Landfill4/6/9) auf jeder Karte:
// gab es bisher nur auf der Stadtkarte. Beim Start (nicht in der Kampagne, nicht wenn die Karte schon welche hat) werden ein paar
// auf freien, ebenen Flaechen abseits von Basen und Oelquellen verteilt. Deterministisch (gameRand), alle Rechner gleich.
bool s_landfill = false;

bool flatFree(const WorldMapState &m, int tx, int ty, int sw, int sh)
{
	int lo = INT32_MAX, hi = INT32_MIN;
	for (int y = ty - 1; y <= ty + sh; y++)
	{
		for (int x = tx - 1; x <= tx + sw; x++)
		{
			if (!tileOnMap(m, x, y)) { return false; }
			const MAPTILE *t = mapTile(m, x, y);
			if (t == nullptr || TileIsOccupied(t)) { return false; }
			const unsigned char tt = terrainType(t);
			if (tt == TER_CLIFFFACE || tt == TER_WATER) { return false; }
			lo = std::min(lo, static_cast<int>(t->height));
			hi = std::max(hi, static_cast<int>(t->height));
		}
	}
	return hi - lo <= 48;
}

void placeLandfills(GameWorld &world)
{
	std::vector<FEATURE_STATS *> kinds;
	for (FEATURE_STATS &fs : asFeatureStats)
	{
		if (fs.id.startsWith("Landfill")) { kinds.push_back(&fs); }
	}
	if (kinds.empty()) { return; }
	for (const FEATURE *f : world.objects.features[0])
	{
		if (f->psStats != nullptr && f->psStats->id.startsWith("Landfill")) { return; }   // Karte (oder Spielstand) hat schon welche
	}
	const int w = world.map.width, h = world.map.height;
	if (w < 32 || h < 32) { return; }
	const int wanted = std::clamp(w * h / 3000, 2, 10);
	std::vector<Vector2i> done;
	int placed = 0;
	for (int attempt = 0; attempt < wanted * 80 && placed < wanted; attempt++)
	{
		FEATURE_STATS *fs = kinds[gameRand(static_cast<uint32_t>(kinds.size()))];
		const int sw = std::max<int>(1, fs->baseWidth), sh = std::max<int>(1, fs->baseBreadth);
		const int tx = 4 + static_cast<int>(gameRand(static_cast<uint32_t>(w - 8 - sw)));
		const int ty = 4 + static_cast<int>(gameRand(static_cast<uint32_t>(h - 8 - sh)));
		if (!flatFree(world.map, tx, ty, sw, sh)) { continue; }
		const int wx = world_coord(tx) + world_coord(sw) / 2, wy = world_coord(ty) + world_coord(sh) / 2;
		if (nearAnyStructure(world, wx, wy)) { continue; }
		bool clash = false;
		for (const FEATURE *f : world.objects.features[0])
		{
			if (f->psStats == nullptr || f->psStats->subType != FEAT_OIL_RESOURCE) { continue; }
			if (iHypot(f->pos.x - wx, f->pos.y - wy) < 6 * DIST_TILE) { clash = true; break; }
		}
		for (const Vector2i &o : done) { if (!clash && iHypot(o.x - wx, o.y - wy) < 14 * DIST_TILE) { clash = true; } }
		if (clash) { continue; }
		if (buildFeature(world, fs, static_cast<UDWORD>(wx), static_cast<UDWORD>(wy), false) != nullptr)
		{
			done.push_back(Vector2i(wx, wy));
			placed++;
		}
	}
	debug(LOG_INFO, "MOD: %d Landfill-Kulissen verteilt", placed);
}

/// (Wunsch 17.11. 21:53/22:42) Schlachtfeld-Deko wie im WZ-Intro: kleine Gruppen aus Strassenlaternen-Reihen bzw. schraegem Wrack mit
/// Truemmern, abseits von Basen und Oelquellen (nur Gefecht/Mehrspieler, deterministisch ueber gameRand)
void placeIntroDeco(GameWorld &world)
{
	FEATURE_STATS *lamp = nullptr, *wreck = nullptr, *rubble = nullptr;
	for (FEATURE_STATS &fs : asFeatureStats)
	{
		if (fs.id.compare("IntroDecoLamp") == 0) { lamp = &fs; }
		else if (fs.id.compare("IntroDecoWreck") == 0) { wreck = &fs; }
		else if (fs.id.compare("IntroDecoRubble") == 0) { rubble = &fs; }
	}
	if (lamp == nullptr || rubble == nullptr) { return; }
	for (const FEATURE *f : world.objects.features[0])
	{
		if (f->psStats != nullptr && f->psStats->id.startsWith("IntroDeco")) { return; }   // Spielstand hat sie schon
	}
	const int w = world.map.width, h = world.map.height;
	if (w < 32 || h < 32) { return; }
	const int groups = std::clamp(w * h / 2500, 3, 14);
	std::vector<Vector2i> done;
	int placed = 0;
	auto freeHere = [&](int tx, int ty) -> bool {
		if (tx < 4 || ty < 4 || tx >= w - 4 || ty >= h - 4 || !flatFree(world.map, tx, ty, 1, 1)) { return false; }
		const int wx = world_coord(tx) + TILE_UNITS / 2, wy = world_coord(ty) + TILE_UNITS / 2;
		if (nearAnyStructure(world, wx, wy)) { return false; }
		for (const FEATURE *f : world.objects.features[0])
		{
			if (f->psStats != nullptr && f->psStats->subType == FEAT_OIL_RESOURCE && iHypot(f->pos.x - wx, f->pos.y - wy) < 5 * DIST_TILE) { return false; }
		}
		return true;
	};
	auto put = [&](FEATURE_STATS *fs, int tx, int ty) {
		if (fs == nullptr || !freeHere(tx, ty)) { return; }
		if (buildFeature(world, fs, static_cast<UDWORD>(world_coord(tx) + TILE_UNITS / 2), static_cast<UDWORD>(world_coord(ty) + TILE_UNITS / 2), false) != nullptr) { placed++; }
	};
	for (int g = 0, attempt = 0; g < groups && attempt < groups * 60; ++attempt)
	{
		const int tx = 6 + static_cast<int>(gameRand(static_cast<uint32_t>(w - 12)));
		const int ty = 6 + static_cast<int>(gameRand(static_cast<uint32_t>(h - 12)));
		if (!freeHere(tx, ty)) { continue; }
		const int wx = world_coord(tx), wy = world_coord(ty);
		bool clash = false;
		for (const Vector2i &o : done) { if (iHypot(o.x - wx, o.y - wy) < 12 * DIST_TILE) { clash = true; break; } }
		if (clash) { continue; }
		done.push_back(Vector2i(wx, wy));
		++g;
		if (gameRand(2) == 0)
		{
			// Laternenreihe (wie am Rand der Intro-Strasse): 3-4 Laternen im Abstand von 3 Feldern
			const bool alongX = gameRand(2) == 0;
			const int n = 3 + static_cast<int>(gameRand(2));
			for (int k = 0; k < n; ++k) { put(lamp, tx + (alongX ? k * 3 : 0), ty + (alongX ? 0 : k * 3)); }
		}
		else
		{
			// schraeges Wrack mit Truemmern drumherum
			put(wreck != nullptr ? wreck : rubble, tx, ty);
			put(rubble, tx + 1, ty + 1);
			put(rubble, tx - 1, ty + 2);
			if (gameRand(2) == 0) { put(lamp, tx + 3, ty - 1); }
		}
	}
	debug(LOG_INFO, "MOD: Intro-Deko: %d Teile in %d Gruppen verteilt", placed, static_cast<int>(done.size()));
}

FEATURE *nearestLava(GameWorld &world, const DROID *d)
{
	FEATURE *best = nullptr;
	int64_t bestD = static_cast<int64_t>(REACH) * REACH;
	for (FEATURE *f : world.objects.features[0])
	{
		if (f->died || !isLavaStats(f->psStats) || modlayer::layerOf(f) != modlayer::layerOf(d)) { continue; }
		const int64_t dx = f->pos.x - d->pos.x, dy = f->pos.y - d->pos.y;
		const int64_t dd = dx * dx + dy * dy;
		if (dd <= bestD) { bestD = dd; best = f; }
	}
	return best;
}
}

bool isLava(const BASE_OBJECT *psObj)
{
	return psObj != nullptr && psObj->type == OBJ_FEATURE && isLavaStats(static_cast<const FEATURE *>(psObj)->psStats);
}

bool isMiner(const DROID *d)
{
	if (d == nullptr || d->died || !d->isConstructionDroid() || modbuild::isInfantryBuilder(d) || modlayerdig::isDrill(d)) { return false; }
	const PROPULSION_STATS *p = d->getPropulsionStats();
	return p != nullptr && p->propulsionType == PROPULSION_TYPE_HOVER;
}

bool orderSelected(unsigned player, FEATURE *psLava)
{
	if (psLava == nullptr || !isLava(psLava) || player >= MAX_PLAYERS) { return false; }
	bool any = false;
	for (DROID *d : gameWorld.objects.droids[player])
	{
		if (!d->selected || !isMiner(d) || modlayer::layerOf(d) != modlayer::layerOf(psLava)) { continue; }
		double dx = d->pos.x - psLava->pos.x, dy = d->pos.y - psLava->pos.y;
		double len = std::sqrt(dx * dx + dy * dy);
		if (len < 1.0) { dx = 1.0; dy = 0.0; len = 1.0; }
		const double dist = 128.0 + 70.0;               // dicht am Rand des 2x2-Felds
		const int px = static_cast<int>(psLava->pos.x + dx / len * dist);
		const int py = static_cast<int>(psLava->pos.y + dy / len * dist);
		orderDroidLoc(d, DORDER_MOVE, static_cast<UDWORD>(px), static_cast<UDWORD>(py), ModeQueue);
		any = true;
	}
	return any;
}

int remaining(const FEATURE *f)
{
	if (f == nullptr || !isLava(f)) { return -1; }
	const auto it = s_left.find(f->id);
	return it == s_left.end() ? LAVA_MJ : it->second;
}

int energyCap(unsigned player)
{
	return player < MAX_PLAYERS ? s_cap[player] : 0;
}

void tick(GameWorld &world)
{
	if (&world != &gameWorld) { return; }
	const unsigned sec = gameTime / 1000;
	if (sec == s_lastSec) { return; }
	s_lastSec = sec;
	s_sec++;

	// (16.11. 19:28) "Lass die Lava komplett draussen. Das passt nicht zu WZ." -> keine Lavafelder mehr (Solar/Energiebank bleiben)
	constexpr bool LAVA_ENABLED = false;
	// einmal am Anfang (nach 3 s, damit Karte und Gebaeude stehen) Lavafelder verteilen
	if (LAVA_ENABLED && !s_placed && gameTime >= 3000)
	{
		s_placed = true;
		placeFields(world);   // unabhaengig vom eigenen Spieler (alle Rechner muessen gleich rechnen)
	}

	if (!s_landfill && gameTime >= 3000)
	{
		s_landfill = true;
		if (game.type != LEVEL_TYPE::CAMPAIGN) { placeLandfills(world); placeIntroDeco(world); }
	}

	// Untergrund-Lavafelder: beim ersten fertigen Schacht
	if (LAVA_ENABLED && !s_placedUnder && s_placed)
	{
		bool shaft = false;
		for (int p = 0; p < MAX_PLAYERS && !shaft; p++)
		{
			for (const STRUCTURE *s : world.objects.structures[p])
			{
				if (!s->died && s->status == SS_BUILT && s->pStructureType->id.startsWith("A0LayerShaft")) { shaft = true; break; }
			}
		}
		if (shaft)
		{
			s_placedUnder = true;
			placeUnderground(world);
		}
	}

	// Energiebanken (Obergrenze) und Solarpaneele (Einnahme)
	const bool night = modweather::isNight(gameTime);
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		int storage = 0, income = 0, incomeUnder = 0;   // aus den Stats der fertigen Gebaeude (storageProvide / incomeProvide)
		for (const STRUCTURE *s : world.objects.structures[p])
		{
			if (s->status != SS_BUILT || s->died) { continue; }
			const STRUCTURE_STATS *st = s->pStructureType;
			storage += st->storageProvide;
			if (st->incomeProvide > 0)
			{
				// (Wunsch 17.11. 20:36) nachts liefern die Paneele an der Oberflaeche nichts - ausser auf den Orbit-Inseln (dort scheint immer Sonne)
				const modlayer::Layer ly = modlayer::layerOf(s);
				if (ly == modlayer::ORBIT) { income += st->incomeProvide; }
				else if (ly == modlayer::SURFACE) { if (!night) { income += st->incomeProvide; } }
				else { incomeUnder += st->incomeProvide; }
			}
		}
		const int solarGain = income + ((s_sec % 4 == 0) ? incomeUnder : 0);
		if (solarGain > 0) { addPower(p, solarGain); modoil::noteIncome(p, modoil::INC_SOLAR, solarGain); }
		if (modoil::manages(p))
		{
			const int cap = BASE_CAP + storage;
			if (cap != s_cap[p])
			{
				s_cap[p] = cap;
				setPowerMaxStorage(p, cap);
			}
		}
	}

	// Schuerfen
	std::vector<uint32_t> empty;
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		for (DROID *d : world.objects.droids[p])
		{
			if (!isMiner(d) || d->order.type != DORDER_NONE || d->action != DACTION_NONE) { continue; }
			FEATURE *f = nearestLava(world, d);
			if (f == nullptr) { continue; }
			if (s_cap[p] > 0 && getPower(static_cast<unsigned>(p)) >= s_cap[p]) { continue; }   // Konto voll: nichts schuerfen, nichts verschwenden
			int &left = s_left.emplace(f->id, LAVA_MJ).first->second;
			const int take = std::min(left, MINE_RATE);
			if (take <= 0) { continue; }
			left -= take;
			addPower(p, take);
			modoil::noteIncome(p, modoil::INC_LAVA, take);
			if (left <= 0) { empty.push_back(f->id); }
		}
	}
	for (uint32_t id : empty)
	{
		for (FEATURE *f : world.objects.features[0])
		{
			if (f->id == id && !f->died)
			{
				removeFeature(f, world);
				s_left.erase(id);
				addConsoleMessage("Lava field exhausted.", DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
				break;
			}
		}
	}
}

void reset()
{
	s_left.clear();
	s_lastSec = 0;
	s_sec = 0;
	s_placed = false;
	s_placedUnder = false;
	s_landfill = false;
	for (int &c : s_cap) { c = 0; }
}
}
