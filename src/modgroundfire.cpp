// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Boden beschiessen (siehe modgroundfire.h)

#include "modicbm.h"
#include "modgroundfire.h"

#include "lib/framework/frame.h"
#include "lib/framework/input.h"
#include "lib/framework/vector.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"

#include "baseobject.h"
#include "droid.h"
#include "console.h"
#include "feature.h"
#include "game_world.h"
#include "map.h"
#include "modlayer.h"
#include "multiplay.h"
#include "objmem.h"
#include "order.h"
#include "ordersource.h"
#include "projectile.h"
#include "random.h"
#include "range_rings.h"
#include "stats.h"
#include "structure.h"
#include "multiplay.h"
#include "lib/netplay/sync_debug.h"
#include "statsdef.h"
#include "modlayermap.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace modgroundfire
{
namespace
{
constexpr int DIRECT_RADIUS = 96;       // direkte Waffen: knapp ein Feld
constexpr int INDIRECT_RADIUS = 320;    // Artillerie / Moerser: gut zwei Felder
constexpr int REUSE_DIST = 48;          // Befehle auf (fast) denselben Punkt teilen sich einen Zielpunkt
constexpr unsigned CLEAN_INTERVAL = 1000;

FEATURE_STATS *s_stats = nullptr;
size_t s_cachedNum = static_cast<size_t>(-1);
const FEATURE_STATS *s_cachedData = nullptr;
unsigned s_nextClean = 0;
bool s_ringOn = false;

FEATURE_STATS *markerStats()
{
	if (s_cachedNum == asFeatureStats.size() && s_cachedData == asFeatureStats.data()) { return s_stats; }
	s_cachedNum = asFeatureStats.size();
	s_cachedData = asFeatureStats.data();
	s_stats = nullptr;
	for (FEATURE_STATS &fs : asFeatureStats)
	{
		if (fs.id.toStdString() == "ModGroundTarget") { s_stats = &fs; break; }
	}
	return s_stats;
}

bool referencedByDroid(const DROID *d, const BASE_OBJECT *marker)
{
	if (d->order.psObj == marker) { return true; }
	for (unsigned i = 0; i < MAX_WEAPONS; ++i)
	{
		if (d->psActionTarget[i] == marker) { return true; }
	}
	for (const DroidOrder &o : d->asOrderList)
	{
		if (o.psObj == marker) { return true; }
	}
	return false;
}

struct StructGround
{
	uint32_t structId = 0;
	FEATURE *marker = nullptr;
	uint32_t until = 0;
};
std::vector<StructGround> s_structGround;
constexpr uint32_t STRUCT_GROUND_MS = 40000;   // so lange beschiesst ein Turm den befohlenen Punkt

bool isReferenced(const GameWorld &world, const BASE_OBJECT *marker)
{
	for (const StructGround &g : s_structGround)
	{
		if (g.marker == marker && gameTime < g.until) { return true; }
	}
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (const DROID *d : world.objects.droids[p])
		{
			if (d != nullptr && referencedByDroid(d, marker)) { return true; }
		}
	}
	return false;
}
} // namespace

bool isMarkerStats(const FEATURE_STATS *stats)
{
	return stats != nullptr && stats == markerStats();
}

bool isMarker(const BASE_OBJECT *obj)
{
	return obj != nullptr && obj->type == OBJ_FEATURE && isMarkerStats(static_cast<const FEATURE *>(obj)->psStats);
}

FEATURE *markerAt(const Vector2i &pos, unsigned player, int layer)
{
	FEATURE_STATS *st = markerStats();
	if (st == nullptr || player >= MAX_PLAYERS || !worldOnMap(gameWorld.map, pos)) { return nullptr; }
	for (FEATURE *f : gameWorld.objects.features[0])
	{
		if (f != nullptr && !f->died && f->psStats == st && static_cast<int>(modlayer::layerOf(f)) == layer
		    && std::abs(f->pos.x - pos.x) <= REUSE_DIST && std::abs(f->pos.y - pos.y) <= REUSE_DIST)
		{
			f->visible[player] = UBYTE_MAX;
			return f;
		}
	}
	FEATURE *f = buildFeature(gameWorld, st, static_cast<UDWORD>(pos.x), static_cast<UDWORD>(pos.y), false);
	if (f == nullptr) { return nullptr; }
	f->pos.z = map_Height(gameWorld.map, pos.x, pos.y);
	f->visible[player] = UBYTE_MAX;
	if (layer != modlayer::SURFACE) { modlayer::assignLayer(f, static_cast<modlayer::Layer>(layer)); }   // Zielpunkt in der Ebene der Einheit (Marker sind nicht an Kacheln gebunden)
	return f;
}

bool canFireOnGround(const DROID *psDroid)
{
	if (psDroid == nullptr || psDroid->died || psDroid->numWeaps == 0 || psDroid->asWeaps[0].nStat == 0 || psDroid->isTransporter())
	{
		return false;
	}
	const WEAPON_STATS *ws = psDroid->getWeaponStats(0);
	if (ws == nullptr || ws->weaponSubClass == WSC_ELECTRONIC) { return false; }
	return (ws->surfaceToAir & SHOOT_ON_GROUND) != 0;
}

int scatterRadius(const WEAPON_STATS &stats)
{
	return proj_Direct(&stats) ? DIRECT_RADIUS : INDIRECT_RADIUS;
}

Vector2i scatterPoint(const Vector2i &center, int radius)
{
	if (radius <= 0) { return center; }
	int dx = 0, dy = 0;
	// gleichmaessig im Kreis: Wuerfeln im Quadrat, bis es im Kreis liegt (hoechstens 4 Versuche, danach nach innen ziehen)
	for (int i = 0; i < 4; ++i)
	{
		dx = static_cast<int>(gameRand(static_cast<uint32_t>(2 * radius + 1))) - radius;
		dy = static_cast<int>(gameRand(static_cast<uint32_t>(2 * radius + 1))) - radius;
		if (dx * dx + dy * dy <= radius * radius) { break; }
		if (i == 3) { dx /= 2; dy /= 2; }
	}
	return Vector2i(center.x + dx, center.y + dy);
}

bool canStructureFireOnGround(const STRUCTURE *psStruct)
{
	if (psStruct == nullptr || psStruct->died || psStruct->status != SS_BUILT || psStruct->numWeaps == 0 || psStruct->asWeaps[0].nStat == 0) { return false; }
	const WEAPON_STATS *ws = psStruct->getWeaponStats(0);
	if (ws == nullptr || ws->weaponSubClass == WSC_ELECTRONIC || ws->weaponSubClass == WSC_LAS_SAT) { return false; }
	return (ws->surfaceToAir & SHOOT_ON_GROUND) != 0;
}

void structureFireAt(STRUCTURE *psStruct, const Vector2i &pos)
{
	if (!canStructureFireOnGround(psStruct)) { return; }
	FEATURE *m = markerAt(pos, psStruct->player, static_cast<int>(modlayer::layerOf(psStruct)));
	if (m == nullptr) { return; }
	s_structGround.erase(std::remove_if(s_structGround.begin(), s_structGround.end(), [psStruct](const StructGround &g) { return g.structId == psStruct->id; }), s_structGround.end());
	StructGround g;
	g.structId = psStruct->id;
	g.marker = m;
	g.until = gameTime + STRUCT_GROUND_MS;
	s_structGround.push_back(g);
	syncDebug("structGround s%u (%d,%d)", psStruct->id, pos.x, pos.y);
}

BASE_OBJECT *structureGroundTarget(const STRUCTURE *psStruct)
{
	if (s_structGround.empty() || psStruct == nullptr) { return nullptr; }
	for (const StructGround &g : s_structGround)
	{
		if (g.structId == psStruct->id && gameTime < g.until && g.marker != nullptr && !g.marker->died) { return g.marker; }
	}
	return nullptr;
}

bool orderSelected(unsigned player, int worldX, int worldY)
{
	if (player >= MAX_PLAYERS || markerStats() == nullptr) { return false; }
	const bool surfaceView = modlayermap::viewLayer() == modlayer::SURFACE;
	if (!worldOnMap(gameWorld.map, worldX, worldY)) { return false; }
	bool any = false;
	for (DROID *d : gameWorld.objects.droids[player])
	{
		if (d == nullptr || !d->selected || (!canFireOnGround(d) && !modicbm::isTelDroid(d))) { continue; }   // (19.11.) Atomraketen-Werfer immer
		// Ebenen: ausserhalb der Oberflaeche nur Ebenen-Sonderwaffen (Orbitalbombe, Tektonik-Torpedo); diese wirken nur in eine ANDERE Ebene
		// als die eigene (Orbitalbombe aus dem Orbit auf die Oberflaeche, Torpedo von der Oberflaeche in den Untergrund)
		const WEAPON_STATS *ws = d->getWeaponStats(0);
		const bool special = ws != nullptr && ws->crossLayer >= 0;
		if (special && static_cast<int>(modlayer::layerOf(d)) == ws->crossLayer) { continue; }
		if (!surfaceView && !special) { continue; }
		DroidOrder sOrder(DORDER_ATTACKGROUND, Vector2i(worldX, worldY));
		sendDroidInfo(d, sOrder, false, currentOrderSource());
		any = true;
	}
	// MOD: gewaehlte Tuerme/Stellungen feuern ebenfalls auf den Punkt (nur an der Oberflaeche)
	if (surfaceView)
	{
		bool tooFar = false;
		int farTiles10 = 0, rangeTiles10 = 0;
		for (STRUCTURE *s : gameWorld.objects.structures[player])
		{
			if (s == nullptr || !s->selected || !canStructureFireOnGround(s)) { continue; }
			// (18.11. 13:37 "MG-Posten dreht sich, schiesst aber nicht" - Strg + Rechtsklick auf den Boden): Tuerme koennen nicht
			// naeher heranfahren. Liegt der Punkt hinter der Reichweite, dreht der Turm nur hin und schweigt. Jetzt: er beschiesst den
			// Boden in dieser Richtung so weit er reicht, und eine Meldung sagt, dass das Ziel zu weit weg ist.
			int tx = worldX, ty = worldY;
			const int range = proj_GetLongRange(*s->getWeaponStats(0), s->player);
			const int dx = worldX - s->pos.x, dy = worldY - s->pos.y;
			const int dist = iHypot(dx, dy);
			if (range > 0 && dist > range - 8)
			{
				const int r = std::max(32, range - 24);
				tx = s->pos.x + static_cast<int>(int64_t(dx) * r / std::max(1, dist));
				ty = s->pos.y + static_cast<int>(int64_t(dy) * r / std::max(1, dist));
				tooFar = true;
				farTiles10 = std::max(farTiles10, dist * 10 / TILE_UNITS);
				rangeTiles10 = std::max(rangeTiles10, range * 10 / TILE_UNITS);
			}
			sendStructureGroundFire(s, tx, ty, currentOrderSource());
			any = true;
		}
		if (tooFar && player == selectedPlayer)
		{
			char msg[200];
			snprintf(msg, sizeof(msg), "Ziel zu weit weg (%d,%d Felder) - der Turm reicht nur %d,%d Felder weit und beschiesst den Boden so weit er kommt.",
			         farTiles10 / 10, farTiles10 % 10, rangeTiles10 / 10, rangeTiles10 % 10);
			addConsoleMessage(msg, DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		}
	}
	return any;
}

bool keysForGroundFire()
{
	const bool ctrl = keyDown(KEY_LCTRL) || keyDown(KEY_RCTRL);
	const bool shift = keyDown(KEY_LSHIFT) || keyDown(KEY_RSHIFT);
	return ctrl && !shift;
}

void updatePreview(bool wanted, const Vector2i &worldPos)
{
	int radius = 0;
	if (wanted && selectedPlayer < MAX_PLAYERS && markerStats() != nullptr)
	{
		for (const DROID *d : gameWorld.objects.droids[selectedPlayer])
		{
			if (d != nullptr && d->selected && canFireOnGround(d))
			{
				radius = std::max(radius, scatterRadius(*d->getWeaponStats(0)));
			}
		}
		for (const STRUCTURE *s : gameWorld.objects.structures[selectedPlayer])
		{
			if (s != nullptr && s->selected && canStructureFireOnGround(s))
			{
				radius = std::max(radius, scatterRadius(*s->getWeaponStats(0)));
			}
		}
	}
	if (radius > 0)
	{
		range_rings::setDebugRange(static_cast<float>(worldPos.x), static_cast<float>(worldPos.y), static_cast<float>(radius));
		s_ringOn = true;
	}
	else if (s_ringOn)
	{
		range_rings::setDebugRange(0.f, 0.f, 0.f);
		s_ringOn = false;
	}
}

const char *activeLabel()
{
	return s_ringOn ? "Area Bombardment: Attack Area" : nullptr;
}

void tick(GameWorld &world)
{
	if (gameTime < s_nextClean) { return; }
	s_nextClean = gameTime + CLEAN_INTERVAL;
	FEATURE_STATS *st = markerStats();
	if (st == nullptr) { return; }
	s_structGround.erase(std::remove_if(s_structGround.begin(), s_structGround.end(), [](const StructGround &g) { return gameTime >= g.until || g.marker == nullptr || g.marker->died; }), s_structGround.end());
	std::vector<FEATURE *> doomed;
	for (FEATURE *f : world.objects.features[0])
	{
		if (f != nullptr && !f->died && f->psStats == st && !isReferenced(world, f))
		{
			doomed.push_back(f);
		}
	}
	for (FEATURE *f : doomed)
	{
		modlayer::forget(f->id);   // Ebenen-Eintrag des Zielpunkts freigeben
		removeFeature(f, world);
	}
}

void reset()
{
	s_structGround.clear();
	s_stats = nullptr;
	s_cachedNum = static_cast<size_t>(-1);
	s_cachedData = nullptr;
	s_nextClean = 0;
	if (s_ringOn)
	{
		range_rings::setDebugRange(0.f, 0.f, 0.f);
		s_ringOn = false;
	}
}
}
