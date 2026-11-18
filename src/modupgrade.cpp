// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Ausbau-Bauwerke wie Module (siehe modupgrade.h)
#include <nlohmann/json.hpp> // vor den WZ-Headern
#include "modupgrade.h"

#include "lib/framework/frame.h"
#include "lib/framework/file.h"

#include "structure.h"
#include "stats.h"
#include "structuredef.h"

#include <string>
#include <unordered_map>

namespace modupgrade
{
namespace
{
bool s_loaded = false;
std::unordered_map<std::string, std::string> s_base;   // Ausbau-ID -> Grundbauwerk-ID

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
}
