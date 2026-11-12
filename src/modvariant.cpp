// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Modell-Leiter fuer Scavenger-Infanterie, siehe modvariant.h
#include "modvariant.h"

#include "lib/framework/frame.h"

#include "droid.h"
#include "stats.h"

#include <cstdint>
#include <string>
#include <vector>

namespace modvariant
{
// kleine Mischfunktion (splitmix), damit aus Einheiten-ID und Rang eine gleichmaessig verteilte Zahl wird
static uint32_t mix(uint32_t a, uint32_t b)
{
	uint64_t z = (static_cast<uint64_t>(a) << 32) ^ (static_cast<uint64_t>(b) * 0x9E3779B97F4A7C15ULL) ^ 0xD1B54A32D192ED03ULL;
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
	z = z ^ (z >> 31);
	return static_cast<uint32_t>(z >> 16);
}

static bool inGroup(const BODY_STATS &b, const std::string &group)
{
	return !b.variantGroup.isEmpty() && group == b.variantGroup.toUtf8();
}

static void applyBody(DROID *psDroid, size_t bodyIndex, bool rename)
{
	const BODY_STATS *oldStats = psDroid->getBodyStats();
	const bool nameWasModelName = (oldStats != nullptr) && (std::string(psDroid->aName) == getStatsName(oldStats) || std::string(psDroid->aName).empty());
	psDroid->asBits[COMP_BODY] = static_cast<uint8_t>(bodyIndex);
	if (rename || nameWasModelName)
	{
		droidSetName(psDroid, getStatsName(&asBodyStats[bodyIndex]));
	}
}

void onFactoryDroid(DROID *psDroid)
{
	if (psDroid == nullptr) { return; }
	const BODY_STATS *cur = psDroid->getBodyStats();
	if (cur == nullptr || !cur->variantRoll || cur->variantGroup.isEmpty()) { return; }
	const std::string group = cur->variantGroup.toUtf8();

	uint32_t total = 0;
	for (const BODY_STATS &b : asBodyStats)
	{
		if (inGroup(b, group) && b.variantWeight > 0) { total += static_cast<uint32_t>(b.variantWeight); }
	}
	if (total == 0) { return; }
	uint32_t r = mix(psDroid->id, 0x5CA1AB1Eu) % total;
	size_t pick = asBodyStats.size();
	for (size_t i = 0; i < asBodyStats.size(); ++i)
	{
		const BODY_STATS &b = asBodyStats[i];
		if (!inGroup(b, group) || b.variantWeight <= 0) { continue; }
		if (r < static_cast<uint32_t>(b.variantWeight)) { pick = i; break; }
		r -= static_cast<uint32_t>(b.variantWeight);
	}
	if (pick >= asBodyStats.size()) { return; }

	const int rank = asBodyStats[pick].variantRank;
	applyBody(psDroid, pick, true);

	// Erfahrungsbonus: seltene Modelle kommen mit ihrem Startrang
	if (rank > 0 && psDroid->asBits[COMP_BRAIN] < asBrainStats.size())
	{
		const auto &th = asBrainStats[psDroid->asBits[COMP_BRAIN]].upgrade[psDroid->player].rankThresholds;
		if (static_cast<size_t>(rank) < th.size())
		{
			const uint32_t want = static_cast<uint32_t>(th[rank]) * 65536u;
			if (psDroid->experience < want) { psDroid->experience = want; }
		}
	}
}

void onRankGained(DROID *psDroid, unsigned newRank)
{
	if (psDroid == nullptr) { return; }
	const BODY_STATS *cur = psDroid->getBodyStats();
	if (cur == nullptr || cur->variantGroup.isEmpty()) { return; }
	const std::string group = cur->variantGroup.toUtf8();
	const size_t curIdx = psDroid->asBits[COMP_BODY];

	// Kandidaten: alle Modelle der Gruppe, deren Stufe genau dem neuen Rang entspricht
	std::vector<size_t> cand;
	for (size_t i = 0; i < asBodyStats.size(); ++i)
	{
		const BODY_STATS &b = asBodyStats[i];
		if (i != curIdx && inGroup(b, group) && b.variantRank == static_cast<int>(newRank)) { cand.push_back(i); }
	}
	if (cand.empty()) { return; }
	applyBody(psDroid, cand[mix(psDroid->id, newRank + 77u) % cand.size()], false);
}
}
