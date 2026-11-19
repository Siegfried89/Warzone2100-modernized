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
#include "modnuke.h"
#include "modoil.h"
#include "effects.h"
#include "fpath.h"
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
constexpr int BASE_CAP = 4000;             // Grundobergrenze (MJ) mit Fass-Wirtschaft
constexpr int REACH = 128 + 192;           // Abstand Truck - Feldmitte, ab dem geschuerft wird (halbes Feld + 1,5 Felder)
constexpr int MIN_START_DIST = 12 * 128;   // Mindestabstand zu Gebaeuden bei der Verteilung

std::map<uint32_t, int> s_left;            // Feld-Id -> Rest (MJ; seit 19.11. Uran-Einheiten)
// (19.11.) Uran: Harvester (C&C3) schuerfen an den Lavafeldern im Untergrund
constexpr int FIELD_URANIUM = 30;          // Uran-Einheiten je Lavafeld
constexpr int HARVEST_SECONDS = 20;        // ein Harvester am Feld foerdert eine Einheit je 20 s
constexpr unsigned SEEK_IDLE_MS = 4000;    // so lange untaetig, dann faehrt er von selbst zum naechsten erreichbaren Feld
std::map<uint32_t, int> s_harvestSec;      // Harvester -> Sekunden seit der letzten Einheit
std::map<uint32_t, unsigned> s_idleSince;  // Harvester -> Spielzeit, seit der er untaetig ist
std::map<uint32_t, bool> s_hint;           // Hinweis "nur im Untergrund" je Harvester gegeben
std::map<uint32_t, bool> s_shaftDone;      // Schacht -> eigenes Start-Lavafeld angelegt
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

[[maybe_unused]] void placeFields(GameWorld &world)
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
// (19.11.) Lavafelder als Uranquelle im Untergrund: jedes Feld liegt in einer kleinen gebohrten Felstasche (4 x 4 Felder, MF: nur durch
// Bohren erreichbar). Neben jedem fertigen Schacht entsteht ausserdem ein Start-Feld, mit einem gebohrten Gang an die Schachtkammer
// angeschlossen - so kann der erste Harvester gleich loslegen. Alles ueber gameRand (alle Rechner gleich), Gaenge im Bohr-Protokoll
// (Spielstand).
bool rockBlock(const WorldMapState &m, int tx, int ty, int w, int h)
{
	for (int y = ty; y < ty + h; y++)
	{
		for (int x = tx; x < tx + w; x++)
		{
			const MAPTILE *t = mapTile(m, x, y);
			if (t == nullptr || TileIsOccupied(t) || terrainType(t) != TER_CLIFFFACE) { return false; }
		}
	}
	return true;
}

FEATURE *fieldInPocket(GameWorld &world, int tx, int ty, int player, int reveal)
{
	FEATURE_STATS *fs = lavaStats();
	if (fs == nullptr) { return nullptr; }
	for (int y = ty - 1; y <= ty + 2; y++)
	{
		for (int x = tx - 1; x <= tx + 2; x++) { modlayermap::drillTile(x, y, player, reveal); }
	}
	FEATURE *f = nullptr;
	{
		modlayermap::Scope sc(modlayer::UNDERGROUND);
		f = buildFeature(world, fs, static_cast<UDWORD>(world_coord(tx + 1)), static_cast<UDWORD>(world_coord(ty + 1)), false);
	}
	if (f != nullptr) { modlayer::assignLayer(f, modlayer::UNDERGROUND); }
	return f;
}

bool fieldNear(GameWorld &world, int wx, int wy, int radTiles)
{
	for (const FEATURE *f : world.objects.features[0])
	{
		if (f->died || !isLavaStats(f->psStats) || modlayer::layerOf(f) != modlayer::UNDERGROUND) { continue; }
		const int64_t dx = f->pos.x - wx, dy = f->pos.y - wy;
		if (dx * dx + dy * dy <= static_cast<int64_t>(world_coord(radTiles)) * world_coord(radTiles)) { return true; }
	}
	return false;
}

void starterField(GameWorld &world, const STRUCTURE *shaft)
{
	const int sx = map_coord(shaft->pos.x), sy = map_coord(shaft->pos.y);
	const int w = world.map.width, h = world.map.height;
	for (int attempt = 0; attempt < 60; attempt++)
	{
		const int ang = static_cast<int>(gameRand(360));
		const int dist = 7 + static_cast<int>(gameRand(4));
		const int tx = sx + static_cast<int>(std::lround(std::cos(ang * M_PI / 180.0) * dist)) - 1;
		const int ty = sy + static_cast<int>(std::lround(std::sin(ang * M_PI / 180.0) * dist)) - 1;
		if (tx < 6 || ty < 6 || tx > w - 8 || ty > h - 8) { continue; }
		bool ok = false;
		{
			modlayermap::Scope sc(modlayer::UNDERGROUND);
			ok = rockBlock(world.map, tx - 1, ty - 1, 4, 4);
		}
		if (!ok) { continue; }
		// Gang von der Schachtkammer zur Tasche (1 Feld breit, gerade Linie)
		const int ex = tx, ey = ty;
		const int steps = std::max(std::abs(ex - sx), std::abs(ey - sy));
		for (int k = 0; k <= steps; k++)
		{
			const int x = sx + (ex - sx) * k / std::max(1, steps);
			const int y = sy + (ey - sy) * k / std::max(1, steps);
			modlayermap::drillTile(x, y, shaft->player, 1);
		}
		fieldInPocket(world, tx, ty, shaft->player, 2);
		return;
	}
}

void placeUnderground(GameWorld &world)
{
	FEATURE_STATS *fs = lavaStats();
	if (fs == nullptr || !modlayermap::worldsExist()) { return; }
	const int w = world.map.width, h = world.map.height;
	if (w < 24 || h < 24) { return; }
	const int wanted = std::clamp(w * h / 1500, 3, 40) / 2;
	int placed = 0;
	for (int attempt = 0; attempt < wanted * 80 && placed < wanted; attempt++)
	{
		const int tx = 6 + static_cast<int>(gameRand(static_cast<uint32_t>(w - 12)));
		const int ty = 6 + static_cast<int>(gameRand(static_cast<uint32_t>(h - 12)));
		bool ok = false;
		{
			modlayermap::Scope sc(modlayer::UNDERGROUND);
			ok = rockBlock(world.map, tx - 1, ty - 1, 4, 4);
		}
		if (!ok) { continue; }
		const int wx = world_coord(tx + 1), wy = world_coord(ty + 1);
		if (nearAnyStructure(world, wx, wy) || fieldNear(world, wx, wy, 6)) { continue; }   // Aufzugskammern und Basen bleiben frei
		if (fieldInPocket(world, tx, ty, -1, 0) != nullptr) { placed++; }
	}
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
	// (19.11.) nur noch der Harvester (C&C3) schuerft - Uran fuer das Atomkraftwerk
	if (d == nullptr || d->died) { return false; }
	const BODY_STATS *b = d->getBodyStats();
	return b != nullptr && b->id.startsWith("HarvesterBody");
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
	return it == s_left.end() ? FIELD_URANIUM : it->second;
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

	if (!s_landfill && gameTime >= 3000)
	{
		s_landfill = true;
		if (game.type != LEVEL_TYPE::CAMPAIGN) { placeLandfills(world); placeIntroDeco(world); }
	}

	// (16.11. 19:28) "Lass die Lava komplett draussen" (Oberflaeche) - (19.11.) "Als Uranquelle werden wir die MF Lavafelder im Untergrund
	// anlegen": nur im Untergrund, in Felstaschen. Beim ersten fertigen Schacht die verstreuten Felder, an jedem Schacht ein Start-Feld.
	if (modlayermap::worldsExist())
	{
		for (int p = 0; p < MAX_PLAYERS; p++)
		{
			for (const STRUCTURE *s : world.objects.structures[p])
			{
				if (s->died || s->status != SS_BUILT || !s->pStructureType->id.startsWith("A0LayerShaft") || s_shaftDone.count(s->id)) { continue; }
				s_shaftDone[s->id] = true;
				if (!s_placedUnder)
				{
					s_placedUnder = true;
					bool any = false;
					for (const FEATURE *f : world.objects.features[0]) { if (!f->died && isLavaStats(f->psStats)) { any = true; break; } }
					if (!any) { placeUnderground(world); }   // Spielstand hat sie schon
				}
				if (!fieldNear(world, s->pos.x, s->pos.y, 14)) { starterField(world, s); }
			}
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

	// (19.11.) Harvester schuerfen Uran: steht er untaetig am Rand eines Lavafelds (gleiche Ebene), foerdert er eine Einheit je 20 s
	// ins Uranlager (modnuke, hoechstens 20). Untaetige Harvester fahren nach kurzer Zeit von selbst zum naechsten erreichbaren Feld
	// (wie in C&C). An der Oberflaeche gibt es kein Uran: einmaliger Hinweis.
	std::vector<uint32_t> empty;
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		for (DROID *d : world.objects.droids[p])
		{
			if (!isMiner(d)) { continue; }
			const bool idle = d->order.type == DORDER_NONE && (d->action == DACTION_NONE || d->action == DACTION_ATTACK);
			if (!idle) { s_idleSince.erase(d->id); continue; }
			const unsigned since = s_idleSince.emplace(d->id, gameTime).first->second;
			FEATURE *f = nearestLava(world, d);
			if (f == nullptr)
			{
				if (modlayer::layerOf(d) != modlayer::UNDERGROUND)
				{
					bool &told = s_hint[d->id];
					if (!told && static_cast<unsigned>(p) == selectedPlayer)
					{
						told = true;
						addConsoleMessage(_("Harvester: uranium is mined at the lava fields underground - drive the harvester down a shaft"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
					}
					continue;
				}
				if (gameTime - since < SEEK_IDLE_MS) { continue; }
				// naechstes erreichbares Feld auf dieser Ebene suchen und hinfahren
				FEATURE *best = nullptr;
				int64_t bestD = INT64_MAX;
				const PROPULSION_STATS *ps = d->getPropulsionStats();
				for (FEATURE *g : world.objects.features[0])
				{
					if (g->died || !isLavaStats(g->psStats) || modlayer::layerOf(g) != modlayer::layerOf(d)) { continue; }
					const int64_t dx = g->pos.x - d->pos.x, dy = g->pos.y - d->pos.y;
					const int64_t dd = dx * dx + dy * dy;
					if (dd >= bestD) { continue; }
					bool reach = true;
					if (ps != nullptr)
					{
						modlayermap::Scope sc(modlayer::layerOf(d));
						double ux = static_cast<double>(d->pos.x - g->pos.x), uy = static_cast<double>(d->pos.y - g->pos.y);
						const double len = std::max(1.0, std::sqrt(ux * ux + uy * uy));
						const Position rim(g->pos.x + static_cast<int>(ux / len * 200.0), g->pos.y + static_cast<int>(uy / len * 200.0), 0);
						reach = fpathCheck(world.map, d->pos, rim, ps->propulsionType);
					}
					if (reach) { bestD = dd; best = g; }
				}
				s_idleSince[d->id] = gameTime;   // naechster Versuch erst nach SEEK_IDLE_MS
				if (best != nullptr)
				{
					double dx = d->pos.x - best->pos.x, dy = d->pos.y - best->pos.y;
					const double len = std::max(1.0, std::sqrt(dx * dx + dy * dy));
					orderDroidLoc(d, DORDER_MOVE, static_cast<UDWORD>(best->pos.x + dx / len * 200.0), static_cast<UDWORD>(best->pos.y + dy / len * 200.0), ModeImmediate);
				}
				continue;
			}
			if (modnuke::uraniumFull(p)) { continue; }   // Lager voll: warten
			int &hsec = s_harvestSec.emplace(d->id, 0).first->second;
			// Staub und Funken an den Krallen (nur Anzeige, wenn sichtbar und auf der angezeigten Ebene)
			if (d->visibleForLocalDisplay() && modlayer::layerOf(d) == modlayer::current() && (s_sec + d->id) % 2 == 0)
			{
				const int dir = static_cast<int>(d->rot.direction);
				const double a = dir * (2.0 * M_PI / 65536.0);
				Vector3i pp(d->pos.x + static_cast<int>(std::sin(a) * 48.0), d->pos.z + 10, d->pos.y + static_cast<int>(std::cos(a) * 48.0));
				addEffect(&pp, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING_SMALL, false, nullptr, 0);
			}
			if (++hsec < HARVEST_SECONDS) { continue; }
			hsec = 0;
			int &left = s_left.emplace(f->id, FIELD_URANIUM).first->second;
			if (left <= 0) { continue; }
			if (modnuke::addUranium(p, 1))
			{
				left--;
				if (left <= 0) { empty.push_back(f->id); }
			}
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
				addConsoleMessage(_("Lava field exhausted - harvesters move on to the next field."), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
				break;
			}
		}
	}
	// abgerissene Harvester vergessen (alle 30 s)
	if (s_sec % 30 == 0)
	{
		std::map<uint32_t, bool> alive;
		for (int p = 0; p < MAX_PLAYERS; p++) { for (const DROID *d : world.objects.droids[p]) { if (!d->died) { alive[d->id] = true; } } }
		for (auto it = s_harvestSec.begin(); it != s_harvestSec.end();) { it = alive.count(it->first) ? std::next(it) : s_harvestSec.erase(it); }
		for (auto it = s_idleSince.begin(); it != s_idleSince.end();) { it = alive.count(it->first) ? std::next(it) : s_idleSince.erase(it); }
		for (auto it = s_hint.begin(); it != s_hint.end();) { it = alive.count(it->first) ? std::next(it) : s_hint.erase(it); }
	}
}

void reset()
{
	s_left.clear();
	s_harvestSec.clear();
	s_idleSince.clear();
	s_hint.clear();
	s_shaftDone.clear();
	s_lastSec = 0;
	s_sec = 0;
	s_placed = false;
	s_placedUnder = false;
	s_landfill = false;
	for (int &c : s_cap) { c = 0; }
}
}
