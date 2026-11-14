// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: KI-Schwierigkeit getrennt skalieren (siehe modaidiff.h)

#include "modaidiff.h"

#include "lib/framework/frame.h"
#include "lib/netplay/netplay.h"

#include "multiplay.h"

#include <algorithm>

namespace modaidiff
{
namespace
{
struct Row { int damage; int cost; int production; };

// Reihenfolge wie AIDifficulty: SUPEREASY, EASY, MEDIUM, HARD, INSANE (Prozent)
const Row kTable[5] = {
	{  42, 200,  25 },   // SUPEREASY = MF Leicht
	{  65, 160,  38 },   // EASY      = zwischen Leicht und Normal
	{ 100, 120,  50 },   // MEDIUM    = MF Normal
	{ 120, 110,  75 },   // HARD      = zwischen Normal und Schwer
	{ 144, 100, 100 },   // INSANE    = MF Schwer
};

const Row *rowFor(unsigned player)
{
	if (!bMultiPlayer || player >= NetPlay.players.size())
	{
		return nullptr;
	}
	const int d = static_cast<int>(NetPlay.players[player].difficulty);
	if (d < 0 || d > 4)   // HUMAN (-2) / DISABLED (-1)
	{
		return nullptr;
	}
	return &kTable[d];
}
}

bool isScaled(unsigned player)
{
	return rowFor(player) != nullptr;
}

int damagePercent(unsigned player)
{
	const Row *r = rowFor(player);
	return r ? r->damage : 100;
}

int costPercent(unsigned player)
{
	const Row *r = rowFor(player);
	return r ? r->cost : 100;
}

int productionPercent(unsigned player)
{
	const Row *r = rowFor(player);
	return r ? r->production : 100;
}

int scaleDamage(int damage, unsigned player)
{
	const Row *r = rowFor(player);
	if (!r || damage <= 0)
	{
		return damage;
	}
	return std::max(1, static_cast<int>(static_cast<int64_t>(damage) * r->damage / 100));
}

int scaleCost(int cost, unsigned player)
{
	const Row *r = rowFor(player);
	if (!r || cost <= 0)
	{
		return cost;
	}
	return static_cast<int>(static_cast<int64_t>(cost) * r->cost / 100);
}
}
