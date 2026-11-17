// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Infanterie in Deckung bringen (siehe modcover.h)

#include "modcover.h"

#include "lib/framework/frame.h"

#include "console.h"
#include "droid.h"
#include "feature.h"
#include "game_world.h"
#include "map.h"
#include "modoil.h"
#include "modterrain.h"
#include "objmem.h"
#include "order.h"
#include "structure.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <queue>
#include <set>
#include <utility>

namespace modcover
{
namespace
{
constexpr int NETWORK_MAX_TILES = 600;
constexpr int COVER_RANGE_TILES = 20;    // "Take cover": so weit sucht jeder Infanterist nach einer Stellung
constexpr int OFFSET = 30;               // zwei Plaetze je Feld, so weit links/rechts der Feldmitte

enum class Kind { TRENCH, HOUSE, CRATER };

struct Slot
{
	int tx, ty;
	Kind kind;
	int used = 0;
};

bool ownTrenchTile(unsigned player, int x, int y)
{
	if (!tileOnMap(gameWorld.map, x, y)) { return false; }
	const MAPTILE *t = mapTile(gameWorld.map, x, y);
	if (t == nullptr || t->psObject == nullptr || t->psObject->type != OBJ_STRUCTURE) { return false; }
	const STRUCTURE *s = static_cast<const STRUCTURE *>(t->psObject);
	return !s->died && s->player == player && s->pStructureType != nullptr && s->pStructureType->infantryPassable && s->status == SS_BUILT;
}

bool houseTile(int x, int y)
{
	if (!tileOnMap(gameWorld.map, x, y)) { return false; }
	const MAPTILE *t = mapTile(gameWorld.map, x, y);
	return t != nullptr && t->psObject != nullptr && t->psObject->type == OBJ_FEATURE && modoil::tileHasHouse(gameWorld.map, x, y)
	       && modoil::tilePassableForInfantry(gameWorld.map, x, y);
}

int64_t dist2(int ax, int ay, int bx, int by)
{
	const int64_t dx = ax - bx, dy = ay - by;
	return dx * dx + dy * dy;
}

Vector2i slotPos(const Slot &s, int k)
{
	const int cx = world_coord(s.tx) + TILE_UNITS / 2, cy = world_coord(s.ty) + TILE_UNITS / 2;
	const int off = std::min(OFFSET, TILE_UNITS / 4);
	if (k == 0) { return Vector2i(cx - off, cy); }
	if (k == 1) { return Vector2i(cx + off, cy); }
	return Vector2i(cx, cy + ((k & 1) ? off : -off));
}

void sendTo(DROID *d, const Vector2i &p)
{
	orderDroidLoc(d, DORDER_MOVE, static_cast<UDWORD>(p.x), static_cast<UDWORD>(p.y), ModeQueue);
}

/// Infanteristen auf Plaetze verteilen: Runde fuer Runde (Kapazitaet 1, dann 2 ...) geht jeder zum naechsten freien Platz
int assign(std::vector<DROID *> &droids, std::vector<Slot> &slots, int maxPerSlot, int64_t maxDist2, std::vector<DROID *> *left)
{
	int sent = 0;
	std::vector<bool> done(droids.size(), false);
	for (int cap = 1; cap <= maxPerSlot; ++cap)
	{
		for (size_t i = 0; i < droids.size(); ++i)
		{
			if (done[i]) { continue; }
			DROID *d = droids[i];
			int best = -1;
			int64_t bestD = 0;
			for (size_t k = 0; k < slots.size(); ++k)
			{
				const Slot &s = slots[k];
				if (s.used >= cap) { continue; }
				if (s.kind == Kind::HOUSE && !modoil::isHouseOccupant(d)) { continue; }
				const int64_t dd = dist2(d->pos.x, d->pos.y, world_coord(s.tx) + TILE_UNITS / 2, world_coord(s.ty) + TILE_UNITS / 2);
				if (dd > maxDist2) { continue; }
				if (best < 0 || dd < bestD) { best = static_cast<int>(k); bestD = dd; }
			}
			if (best < 0) { continue; }
			Slot &s = slots[static_cast<size_t>(best)];
			sendTo(d, slotPos(s, s.used));
			++s.used;
			done[i] = true;
			++sent;
		}
	}
	if (left != nullptr)
	{
		for (size_t i = 0; i < droids.size(); ++i) { if (!done[i]) { left->push_back(droids[i]); } }
	}
	return sent;
}
} // namespace

bool distributeOnNetwork(unsigned player, const STRUCTURE *start)
{
	if (player >= MAX_PLAYERS || start == nullptr) { return false; }
	std::vector<DROID *> droids;
	for (DROID *d : gameWorld.objects.droids[player])
	{
		if (d->selected && !d->died && modoil::isInfantry(d)) { droids.push_back(d); }
	}
	if (droids.size() < 2) { return false; }   // ein einzelner Mann: normaler Befehl
	const int sx = map_coord(start->pos.x), sy = map_coord(start->pos.y);
	if (!ownTrenchTile(player, sx, sy)) { return false; }
	// zusammenhaengendes Grabennetz (8 Nachbarn)
	std::vector<Slot> slots;
	std::set<std::pair<int, int>> seen;
	std::queue<std::pair<int, int>> q;
	q.push({sx, sy});
	seen.insert({sx, sy});
	while (!q.empty() && static_cast<int>(slots.size()) < NETWORK_MAX_TILES)
	{
		const auto c = q.front();
		q.pop();
		slots.push_back(Slot{c.first, c.second, Kind::TRENCH, 0});
		for (int dy = -1; dy <= 1; ++dy)
		{
			for (int dx = -1; dx <= 1; ++dx)
			{
				if (dx == 0 && dy == 0) { continue; }
				const std::pair<int, int> n(c.first + dx, c.second + dy);
				if (seen.count(n) != 0 || !ownTrenchTile(player, n.first, n.second)) { continue; }
				seen.insert(n);
				q.push(n);
			}
		}
	}
	// wer am naechsten am angeklickten Graben ist, sucht sich zuerst einen Platz
	const int cx = start->pos.x, cy = start->pos.y;
	std::sort(droids.begin(), droids.end(), [&](const DROID *a, const DROID *b) {
		const int64_t da = dist2(a->pos.x, a->pos.y, cx, cy), db = dist2(b->pos.x, b->pos.y, cx, cy);
		return da != db ? da < db : a->id < b->id;
	});
	std::vector<DROID *> left;
	const int sent = assign(droids, slots, 3, INT64_MAX, &left);
	for (DROID *d : left) { sendTo(d, Vector2i(cx, cy)); }   // Netz voll: zum angeklickten Graben
	if (player == selectedPlayer)
	{
		char msg[160];
		snprintf(msg, sizeof(msg), _("Trench network: %d infantry spread over %d trench positions"), sent, static_cast<int>(slots.size()));
		addConsoleMessage(msg, DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
	}
	return true;
}

int takeCover(unsigned player)
{
	if (player >= MAX_PLAYERS) { return 0; }
	std::vector<DROID *> droids;
	bool anySelected = false;
	for (DROID *d : gameWorld.objects.droids[player])
	{
		if (d->selected && !d->died && modoil::isInfantry(d)) { anySelected = true; break; }
	}
	std::map<std::pair<int, int>, int> inCover;   // schon besetzte Felder (zaehlen als belegt)
	for (DROID *d : gameWorld.objects.droids[player])
	{
		if (d->died || !modoil::isInfantry(d) || (anySelected && !d->selected)) { continue; }
		const int tx = map_coord(d->pos.x), ty = map_coord(d->pos.y);
		const bool covered = modoil::droidInTrench(d) || modterrain::droidInCover(d);
		if (covered && (d->order.type == DORDER_NONE || d->order.type == DORDER_GUARD || d->order.type == DORDER_HOLD))
		{
			inCover[{tx, ty}]++;   // sitzt schon in Deckung - bleibt
			continue;
		}
		droids.push_back(d);
	}
	if (droids.empty())
	{
		if (player == selectedPlayer) { addConsoleMessage(_("Take cover: all infantry is already in cover"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
		return 0;
	}
	// Suchbereich: rund um alle Infanteristen
	int x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
	for (const DROID *d : droids)
	{
		x0 = std::min(x0, map_coord(d->pos.x)); y0 = std::min(y0, map_coord(d->pos.y));
		x1 = std::max(x1, map_coord(d->pos.x)); y1 = std::max(y1, map_coord(d->pos.y));
	}
	x0 = std::max(1, x0 - COVER_RANGE_TILES); y0 = std::max(1, y0 - COVER_RANGE_TILES);
	x1 = std::min(gameWorld.map.width - 2, x1 + COVER_RANGE_TILES); y1 = std::min(gameWorld.map.height - 2, y1 + COVER_RANGE_TILES);
	std::vector<Slot> slots;
	for (int y = y0; y <= y1; ++y)
	{
		for (int x = x0; x <= x1; ++x)
		{
			Kind k;
			if (ownTrenchTile(player, x, y)) { k = Kind::TRENCH; }
			else if (houseTile(x, y)) { k = Kind::HOUSE; }
			else if (modterrain::tileGivesCover(x, y)) { k = Kind::CRATER; }
			else { continue; }
			Slot s{x, y, k, 0};
			const auto it = inCover.find({x, y});
			if (it != inCover.end()) { s.used = it->second; }
			slots.push_back(s);
		}
	}
	// Graeben zuerst (beste Deckung), dann Haeuser, dann Krater - innerhalb gleicher Art der naechste Platz
	std::stable_sort(slots.begin(), slots.end(), [](const Slot &a, const Slot &b) { return static_cast<int>(a.kind) < static_cast<int>(b.kind); });
	std::sort(droids.begin(), droids.end(), [](const DROID *a, const DROID *b) { return a->id < b->id; });
	const int64_t range = static_cast<int64_t>(world_coord(COVER_RANGE_TILES));
	int sent = 0;
	// erst nur Graeben, dann Graeben + Haeuser, dann alles - so fuellen sich zuerst die besten Stellungen in Reichweite
	for (int stage = 0; stage < 3 && !droids.empty(); ++stage)
	{
		std::vector<Slot> pool;
		std::vector<size_t> idx;
		for (size_t k = 0; k < slots.size(); ++k)
		{
			if (static_cast<int>(slots[k].kind) <= stage) { pool.push_back(slots[k]); idx.push_back(k); }
		}
		if (pool.empty()) { continue; }
		std::vector<DROID *> left;
		sent += assign(droids, pool, 2, range * range, &left);
		for (size_t k = 0; k < pool.size(); ++k) { slots[idx[k]].used = pool[k].used; }
		droids.swap(left);
	}
	if (player == selectedPlayer)
	{
		char msg[160];
		if (sent > 0)
		{
			snprintf(msg, sizeof(msg), _("Take cover: %d infantry moving to trenches, pits and buildings"), sent);
		}
		else
		{
			snprintf(msg, sizeof(msg), "%s", _("Take cover: no free trenches, pits or buildings nearby"));
		}
		addConsoleMessage(msg, DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
	}
	return sent;
}
} // namespace modcover
