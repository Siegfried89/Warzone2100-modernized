// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Siedlungen mit abwechselnden Haeusern (siehe modsettle.h)
#include "modsettle.h"

#include <algorithm>
#include <climits>
#include <string>
#include <vector>

#include "lib/framework/frame.h"
#include "game_world.h"
#include "map.h"
#include "objmem.h"
#include "structure.h"

namespace modsettle
{
namespace
{
char keyOf(const STRUCTURE_STATS *s)
{
	// A0SettleDesert -> 'D', A0SettleWinter -> 'W', A0SettleCity -> 'C'
	const std::string id = s->id.toUtf8();
	return id.size() > 8 ? id[8] : 0;
}

std::vector<STRUCTURE_STATS *> variantsOf(char key)
{
	std::vector<STRUCTURE_STATS *> v;
	const std::string pre = std::string("A0SettleV-") + key + "-";
	for (unsigned i = 0; i < numStructureStats; ++i)
	{
		if (asStructureStats[i].id.toUtf8().rfind(pre, 0) == 0) { v.push_back(&asStructureStats[i]); }
	}
	return v;
}

/// Abstand (in Feldern) zwischen den Grundflaechen zweier Rechtecke (0 = beruehren/ueberlappen)
int gapTiles(const Vector2i &a0, const Vector2i &as, const Vector2i &b0, const Vector2i &bs)
{
	const int gx = std::max({0, b0.x - (a0.x + as.x), a0.x - (b0.x + bs.x)});
	const int gy = std::max({0, b0.y - (a0.y + as.y), a0.y - (b0.y + bs.y)});
	return std::max(gx, gy);
}
}

bool isSet(const STRUCTURE_STATS *s)
{
	return s != nullptr && s->id.startsWith("A0Settle") && !s->id.startsWith("A0SettleV-");
}

STRUCTURE_STATS *chooseVariant(STRUCTURE_STATS *set, unsigned player, Vector2i &pos, uint16_t direction)
{
	if (!isSet(set)) { return set; }
	const std::vector<STRUCTURE_STATS *> vars = variantsOf(keyOf(set));
	if (vars.empty()) { return set; }
	const Vector2i setSize = set->size(direction);
	const Vector2i t0(map_coord(pos.x - setSize.x * TILE_UNITS / 2 + TILE_UNITS / 2), map_coord(pos.y - setSize.y * TILE_UNITS / 2 + TILE_UNITS / 2));
	STRUCTURE_STATS *best = nullptr, *bestAny = nullptr;
	int64_t bestScore = INT64_MAX, bestAnyScore = INT64_MAX;
	Vector2i bestPos = pos, bestAnyPos = pos;
	for (size_t k = 0; k < vars.size(); ++k)
	{
		STRUCTURE_STATS *v = vars[k];
		const Vector2i vs = v->size(direction);
		const Vector2i c(world_coord(t0.x) + vs.x * TILE_UNITS / 2, world_coord(t0.y) + vs.y * TILE_UNITS / 2);
		if (!validLocation(gameWorld, v, c, direction, player, false)) { continue; }
		bool sameNear = false;
		int count = 0;
		for (unsigned p = 0; p < MAX_PLAYERS; ++p)
		{
			for (const STRUCTURE *s : gameWorld.objects.structures[p])
			{
				if (s->died || s->pStructureType != v) { continue; }
				const StructureBounds b = getStructureBounds(s);
				const int gap = gapTiles(t0, vs, b.map, b.size);
				if (gap <= 2) { sameNear = true; }
				if (gap <= 16) { count++; }
			}
		}
		const uint32_t h = (static_cast<uint32_t>(t0.x) * 73856093u) ^ (static_cast<uint32_t>(t0.y) * 19349663u) ^ (static_cast<uint32_t>(k + 1) * 83492791u);
		const int64_t score = static_cast<int64_t>(count) * 100000 + static_cast<int64_t>(h % 99991u);
		if (score < bestAnyScore) { bestAnyScore = score; bestAny = v; bestAnyPos = c; }
		if (!sameNear && score < bestScore) { bestScore = score; best = v; bestPos = c; }
	}
	if (best == nullptr) { best = bestAny; bestPos = bestAnyPos; }
	if (best == nullptr) { return set; }
	pos = bestPos;
	return best;
}
}
