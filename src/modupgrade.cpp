// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Ausbau-Bauwerke wie Module (siehe modupgrade.h)
#include <nlohmann/json.hpp> // vor den WZ-Headern
#include "modupgrade.h"

#include "lib/framework/frame.h"
#include "lib/framework/file.h"

#include "structure.h"
#include "stats.h"
#include "structuredef.h"
#include "map.h"
#include "game_world.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace modupgrade
{
namespace
{
bool s_loaded = false;
std::unordered_map<std::string, std::string> s_base;   // Ausbau-ID -> Grundbauwerk-ID
// (24.11.) Grundbauwerk-ID -> (gesetztes Bauwerk -> Ergebnis)
std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> s_combine;

void load()
{
	s_loaded = true;
	std::vector<char> buf;
	if (!loadFileToBufferVector("stats/structupgrades.json", buf, false, false) || buf.empty()) { return; }
	nlohmann::json j = nlohmann::json::parse(buf.begin(), buf.end(), nullptr, false);
	if (j.is_discarded() || !j.is_object()) { return; }
	for (auto it = j.begin(); it != j.end(); ++it)
	{
		if (it.value().is_string()) { s_base[it.key()] = it.value().get<std::string>(); }
	}
	buf.clear();
	if (!loadFileToBufferVector("stats/structcombine.json", buf, false, false) || buf.empty()) { return; }
	nlohmann::json c = nlohmann::json::parse(buf.begin(), buf.end(), nullptr, false);
	if (c.is_discarded() || !c.is_object()) { return; }
	for (auto it = c.begin(); it != c.end(); ++it)
	{
		if (!it.value().is_object()) { continue; }
		for (auto jt = it.value().begin(); jt != it.value().end(); ++jt)
		{
			if (jt.value().is_string()) { s_combine[it.key()].emplace_back(jt.key(), jt.value().get<std::string>()); }
		}
	}
}

STRUCTURE_STATS *statsOf(const std::string &id)
{
	for (UDWORD i = 0; i < numStructureStats; ++i) { if (asStructureStats[i].id.toStdString() == id) { return &asStructureStats[i]; } }
	return nullptr;
}

const std::string *baseOf(const STRUCTURE_STATS *psStats)
{
	if (psStats == nullptr) { return nullptr; }
	if (!s_loaded) { load(); }
	if (s_base.empty()) { return nullptr; }
	const auto it = s_base.find(psStats->id.toStdString());
	return it != s_base.end() ? &it->second : nullptr;
}
}

bool isUpgrade(const STRUCTURE_STATS *psStats)
{
	return baseOf(psStats) != nullptr;
}

STRUCTURE_STATS *upgradeFor(const STRUCTURE *psOld, unsigned player)
{
	if (psOld == nullptr || psOld->pStructureType == nullptr || player >= MAX_PLAYERS || apStructTypeLists[player] == nullptr) { return nullptr; }
	if (!s_loaded) { load(); }
	// (24.11.) Hineinbauen: das gewoehnliche Bauwerk (Betonbunker), das in dieses Grundbauwerk passt und verfuegbar ist
	if (psOld->status == SS_BUILT && psOld->player == player && !psOld->died)
	{
		const auto ct = s_combine.find(psOld->pStructureType->id.toStdString());
		if (ct != s_combine.end())
		{
			for (const auto &pr : ct->second)
			{
				STRUCTURE_STATS *st = statsOf(pr.first);
				if (st == nullptr || statsOf(pr.second) == nullptr) { continue; }
				const int idx = static_cast<int>(st - asStructureStats);
				if (apStructTypeLists[player][idx] == AVAILABLE) { return st; }
			}
		}
	}
	if (s_base.empty()) { return nullptr; }
	const std::string oldId = psOld->pStructureType->id.toStdString();
	for (UDWORD i = 0; i < numStructureStats; ++i)
	{
		STRUCTURE_STATS *st = &asStructureStats[i];
		const auto it = s_base.find(st->id.toStdString());
		if (it == s_base.end() || it->second != oldId) { continue; }
		if (apStructTypeLists[player][i] != AVAILABLE) { continue; }
		if (canReplace(st, psOld, player)) { return st; }
	}
	return nullptr;
}

bool canReplace(const STRUCTURE_STATS *psNew, const STRUCTURE *psOld, unsigned player)
{
	const std::string *base = baseOf(psNew);
	if (base == nullptr || psOld == nullptr || psOld->died || psOld->pStructureType == nullptr) { return false; }
	if (psOld->status != SS_BUILT || player >= MAX_PLAYERS || psOld->player != player) { return false; }   // nur das eigene, fertige Grundbauwerk
	return psOld->pStructureType->id.toStdString() == *base;
}

STRUCTURE_STATS *combineResult(const STRUCTURE_STATS *psPlaced, const STRUCTURE *psOld, unsigned player)
{
	if (psPlaced == nullptr || psOld == nullptr || psOld->died || psOld->pStructureType == nullptr) { return nullptr; }
	if (psOld->status != SS_BUILT || player >= MAX_PLAYERS || psOld->player != player) { return nullptr; }
	if (!s_loaded) { load(); }
	if (s_combine.empty()) { return nullptr; }
	const auto ct = s_combine.find(psOld->pStructureType->id.toStdString());
	if (ct == s_combine.end()) { return nullptr; }
	const std::string placed = psPlaced->id.toStdString();
	for (const auto &pr : ct->second)
	{
		if (pr.first == placed) { return statsOf(pr.second); }
	}
	return nullptr;
}

STRUCTURE_STATS *combineAt(const STRUCTURE_STATS *psPlaced, int worldX, int worldY, unsigned player)
{
	if (psPlaced == nullptr || !worldOnMap(gameWorld.map, worldX, worldY)) { return nullptr; }
	if (!s_loaded) { load(); }
	if (s_combine.empty()) { return nullptr; }
	const MAPTILE *psTile = worldTile(gameWorld.map, worldX, worldY);
	if (psTile == nullptr || !TileHasStructure(psTile)) { return nullptr; }
	return combineResult(psPlaced, static_cast<const STRUCTURE *>(psTile->psObject), player);
}

bool combinesInto(const STRUCTURE_STATS *psPlaced, const STRUCTURE_STATS *psResult)
{
	if (psPlaced == nullptr || psResult == nullptr) { return false; }
	if (!s_loaded) { load(); }
	const std::string placed = psPlaced->id.toStdString(), result = psResult->id.toStdString();
	for (const auto &kv : s_combine)
	{
		for (const auto &pr : kv.second) { if (pr.first == placed && pr.second == result) { return true; } }
	}
	return false;
}
}
