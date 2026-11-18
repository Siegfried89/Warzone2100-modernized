// MOD (18.11.): fahrende Fabrik (Fatboy) - siehe modmobilefactory.h
#include "modmobilefactory.h"

#include "lib/framework/frame.h"
#include "lib/framework/vector.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/netplay/sync_debug.h"
#include "droid.h"
#include "game_world.h"
#include "hci.h"
#include "map.h"
#include "modlayer.h"
#include "multiplay.h"
#include "objmem.h"
#include "stats.h"
#include "structure.h"

#include <algorithm>
#include <map>
#include <unordered_map>
#include <vector>

namespace modmobilefactory
{
namespace
{
/// Sammelpunkt folgt dem Fahrzeug, solange der Spieler ihn nicht selbst versetzt hat (Kern-ID -> zuletzt gesetzte Stelle)
std::map<uint32_t, Vector2i> s_autoFlag;

STRUCTURE_STATS *coreStats(const DROID *d)
{
	const BODY_STATS *b = d != nullptr ? d->getBodyStats() : nullptr;
	if (b == nullptr || b->mobileFactory.isEmpty())
	{
		return nullptr;
	}
	static std::unordered_map<const BODY_STATS *, int32_t> cache;
	auto it = cache.find(b);
	if (it == cache.end())
	{
		int32_t idx = getStructStatFromName(b->mobileFactory);
		if (idx >= 0 && !asStructureStats[idx].mobileCore)
		{
			debug(LOG_ERROR, "mobileFactory %s ist kein Fabrik-Kern (\"mobileFactoryCore\": 1 fehlt)", b->mobileFactory.toUtf8().c_str());
			idx = -1;
		}
		it = cache.emplace(b, idx).first;
	}
	return it->second >= 0 ? &asStructureStats[it->second] : nullptr;
}

/// Heck des Fahrzeugs (Modell: vorn = -z, hinten = +z); Abstaende in Welteinheiten (DIST_TILE = 128, auch in der Viertel-Fassung)
int rearDist(const DROID *d)
{
	const BODY_STATS *b = d->getBodyStats();
	const int len = (b != nullptr && b->pIMD != nullptr) ? b->pIMD->max.z : 0;
	return std::max(DIST_TILE / 2, len);
}

Vector2i corePos(const DROID *d)
{
	Vector2i p = d->pos.xy() - iSinCosR(d->rot.direction, rearDist(d) * 3 / 4);
	p.x = std::clamp(p.x, world_coord(1), world_coord(std::max(2, gameWorld.map.width - 1)) - 1);
	p.y = std::clamp(p.y, world_coord(1), world_coord(std::max(2, gameWorld.map.height - 1)) - 1);
	return p;
}

Vector2i autoFlagPos(const DROID *d)
{
	Vector2i p = d->pos.xy() - iSinCosR(d->rot.direction, rearDist(d) + DIST_TILE * 3 / 2);
	p.x = std::clamp(p.x, world_coord(2), world_coord(std::max(3, gameWorld.map.width - 2)) - 1);
	p.y = std::clamp(p.y, world_coord(2), world_coord(std::max(3, gameWorld.map.height - 2)) - 1);
	return p;
}

void followHost(STRUCTURE *core, const DROID *d)
{
	const Vector2i p = corePos(d);
	core->pos.x = p.x;
	core->pos.y = p.y;
	core->pos.z = map_Height(gameWorld.map, p.x, p.y);
	core->rot = d->rot;
	if (modlayer::layerOf(core) != modlayer::layerOf(d))
	{
		modlayer::assignLayer(core, modlayer::layerOf(d));
	}
	// Sammelpunkt faehrt mit (wie in Supreme Commander: neue Einheiten sammeln sich hinter dem Fatboy), bis der Spieler ihn versetzt
	FLAG_POSITION *fp = core->pFunctionality != nullptr ? core->pFunctionality->factory.psAssemblyPoint : nullptr;
	if (fp == nullptr)
	{
		return;
	}
	const Vector2i want = autoFlagPos(d);
	auto it = s_autoFlag.find(core->id);
	const Vector2i cur = fp->coords.xy();
	bool following;
	if (it == s_autoFlag.end())
	{
		// neu (oder nach dem Laden): folgen, wenn der Punkt noch in der Naehe des Fahrzeugs liegt
		following = iHypot(cur - want) < DIST_TILE * 3;
	}
	else
	{
		following = iHypot(cur - it->second) < DIST_TILE / 2;   // weit weg von der zuletzt gesetzten Stelle = vom Spieler versetzt
	}
	if (!following)
	{
		s_autoFlag[core->id] = Vector2i(-100000, -100000);   // folgt nicht mehr
		return;
	}
	if (it == s_autoFlag.end() || iHypot(want - it->second) > DIST_TILE * 3 / 4)
	{
		setAssemblyPoint(gameWorld, fp, want.x, want.y, core->player, true);
		s_autoFlag[core->id] = fp->coords.xy();
	}
}
} // namespace

bool isHost(const DROID *psDroid)
{
	return coreStats(psDroid) != nullptr;
}

STRUCTURE *coreOf(const DROID *psDroid)
{
	if (psDroid == nullptr || psDroid->player >= MAX_PLAYERS || !isHost(psDroid))
	{
		return nullptr;
	}
	for (STRUCTURE *s : gameWorld.objects.structures[psDroid->player])
	{
		if (s != nullptr && !s->died && s->isMobileCore() && s->modHostId == psDroid->id)
		{
			return s;
		}
	}
	return nullptr;
}

DROID *hostOf(const STRUCTURE *psCore)
{
	if (psCore == nullptr || !psCore->isMobileCore() || psCore->modHostId == 0 || psCore->player >= MAX_PLAYERS)
	{
		return nullptr;
	}
	DROID *d = IdToDroid(gameWorld.objects, psCore->modHostId, psCore->player);
	return (d != nullptr && !d->died) ? d : nullptr;
}

void update()
{
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		// Kerne dieses Spielers: Fahrzeug-ID -> Kern; Kerne ohne gueltiges Fahrzeug sammeln
		std::map<uint32_t, STRUCTURE *> byHost;
		std::vector<STRUCTURE *> orphans, unlinked;
		for (STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s == nullptr || s->died || !s->isMobileCore())
			{
				continue;
			}
			if (s->modHostId == 0)
			{
				unlinked.push_back(s);      // nach dem Laden ohne Verknuepfung (z. B. Wiederholung): unten dem naechsten Fatboy zuordnen
				continue;
			}
			DROID *d = IdToDroid(gameWorld.objects, s->modHostId, p);
			if (d == nullptr || d->died || coreStats(d) != s->pStructureType || byHost.count(s->modHostId))
			{
				orphans.push_back(s);
				continue;
			}
			byHost[s->modHostId] = s;
		}
		std::vector<DROID *> needCore;
		for (DROID *d : gameWorld.objects.droids[p])
		{
			if (d == nullptr || d->died || !isHost(d))
			{
				continue;
			}
			auto it = byHost.find(d->id);
			if (it != byHost.end())
			{
				followHost(it->second, d);
			}
			else
			{
				needCore.push_back(d);
			}
		}
		// unverknuepfte Kerne (Laden ohne ID) dem naechsten Fatboy ohne Kern geben - feste Reihenfolge, auf allen Rechnern gleich
		for (STRUCTURE *s : unlinked)
		{
			DROID *best = nullptr;
			int bestD = 0;
			for (DROID *d : needCore)
			{
				if (coreStats(d) != s->pStructureType) { continue; }
				const int dd = iHypot((d->pos - s->pos).xy());
				if (best == nullptr || dd < bestD) { best = d; bestD = dd; }
			}
			if (best != nullptr)
			{
				s->modHostId = best->id;
				needCore.erase(std::find(needCore.begin(), needCore.end(), best));
				followHost(s, best);
			}
			else
			{
				orphans.push_back(s);
			}
		}
		for (DROID *d : needCore)
		{
			STRUCTURE_STATS *st = coreStats(d);
			const Vector2i cp = corePos(d);
			STRUCTURE *core = buildStructureDir(gameWorld, st, cp.x, cp.y, d->rot.direction, p, false, generateSynchronisedObjectId(), false, false, /*ignoreLimits=*/true);
			if (core == nullptr)
			{
				continue;
			}
			core->modHostId = d->id;
			if (modlayer::layerOf(core) != modlayer::layerOf(d))
			{
				modlayer::assignLayer(core, modlayer::layerOf(d));
			}
			buildingComplete(core, gameWorld);
			followHost(core, d);
			syncDebug("mobile factory core %u for droid %u (p%u)", core->id, d->id, p);
		}
		for (STRUCTURE *s : orphans)
		{
			s_autoFlag.erase(s->id);
			syncDebug("mobile factory core %u removed (host %u gone)", s->id, s->modHostId);
			removeStruct(s, true, gameWorld);
		}
	}
}

void onDroidClicked(DROID *psDroid)
{
	if (psDroid == nullptr || psDroid->player != selectedPlayer || !psDroid->selected)
	{
		return;
	}
	STRUCTURE *core = coreOf(psDroid);
	if (core != nullptr && core->status == SS_BUILT)
	{
		intShowMobileFactory(core);
	}
}

void reset()
{
	s_autoFlag.clear();
}
}
