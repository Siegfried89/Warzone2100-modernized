// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Boden beschiessen (siehe modgroundfire.h)

#include "modgroundfire.h"

#include "lib/framework/frame.h"
#include "lib/framework/input.h"
#include "lib/framework/vector.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"

#include "baseobject.h"
#include "droid.h"
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
unsigned s_nextClean = 0;
bool s_ringOn = false;

FEATURE_STATS *markerStats()
{
	if (s_cachedNum == asFeatureStats.size()) { return s_stats; }
	s_cachedNum = asFeatureStats.size();
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

bool isReferenced(const GameWorld &world, const BASE_OBJECT *marker)
{
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

FEATURE *markerAt(const Vector2i &pos, unsigned player)
{
	FEATURE_STATS *st = markerStats();
	if (st == nullptr || player >= MAX_PLAYERS || !worldOnMap(gameWorld.map, pos)) { return nullptr; }
	for (FEATURE *f : gameWorld.objects.features[0])
	{
		if (f != nullptr && !f->died && f->psStats == st
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

bool orderSelected(unsigned player, int worldX, int worldY)
{
	if (player >= MAX_PLAYERS || markerStats() == nullptr) { return false; }
	if (modlayermap::viewLayer() != modlayer::SURFACE) { return false; }   // Ebenen: nur an der Oberflaeche
	if (!worldOnMap(gameWorld.map, worldX, worldY)) { return false; }
	bool any = false;
	for (DROID *d : gameWorld.objects.droids[player])
	{
		if (d == nullptr || !d->selected || !canFireOnGround(d)) { continue; }
		DroidOrder sOrder(DORDER_ATTACKGROUND, Vector2i(worldX, worldY));
		sendDroidInfo(d, sOrder, false, currentOrderSource());
		any = true;
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

void tick(GameWorld &world)
{
	if (gameTime < s_nextClean) { return; }
	s_nextClean = gameTime + CLEAN_INTERVAL;
	FEATURE_STATS *st = markerStats();
	if (st == nullptr) { return; }
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
		removeFeature(f, world);
	}
}

void reset()
{
	s_stats = nullptr;
	s_cachedNum = static_cast<size_t>(-1);
	s_nextClean = 0;
	if (s_ringOn)
	{
		range_rings::setDebugRange(0.f, 0.f, 0.f);
		s_ringOn = false;
	}
}
}
