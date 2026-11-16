// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Mod-Zustand im Spielstand - siehe modsave.h
#include <nlohmann/json.hpp> // Must come before WZ includes

#include "modsave.h"

#include "lib/framework/frame.h"
#include "lib/framework/physfs_ext.h"
#include "lib/framework/file.h"
#include "game.h"
#include "modfuel.h"
#include "modoil.h"
#include "modterrain.h"

#include <vector>

namespace modsave
{
namespace
{
std::string s_pending;

template<typename T> void readArray(const nlohmann::json &j, const char *key, std::vector<T> &out)
{
	out.clear();
	const auto it = j.find(key);
	if (it == j.end() || !it->is_array()) { return; }
	for (const auto &v : *it)
	{
		if (v.is_number_integer()) { out.push_back(v.get<T>()); }
	}
}
}

void write(const std::string &folder)
{
	try
	{
		nlohmann::json j = nlohmann::json::object();
		j["version"] = 1;
		std::vector<int> terrain, fuel;
		std::vector<int64_t> oil;
		modterrain::saveState(terrain);
		modfuel::saveState(fuel);
		modoil::saveState(oil);
		j["craters"] = terrain;
		j["fuel"] = fuel;
		j["oil"] = oil;
		const std::string path = folder + "/modstate.json";
		const std::string text = j.dump();
		if (!saveFile(path.c_str(), text.c_str(), text.size()))
		{
			debug(LOG_ERROR, "MOD: Mod-Zustand konnte nicht gespeichert werden (%s)", path.c_str());
		}
	}
	catch (const std::exception &e)
	{
		debug(LOG_ERROR, "MOD: Mod-Zustand: %s", e.what());
	}
}

void setPending(const std::string &folder)
{
	s_pending = folder;
}

void applyPending()
{
	if (s_pending.empty()) { return; }
	const std::string path = s_pending + "/modstate.json";
	s_pending.clear();
	if (!PHYSFS_exists(path.c_str())) { return; }
	const auto j = parseJsonFile(path.c_str());
	if (!j.has_value() || !j->is_object()) { return; }
	try
	{
		std::vector<int> terrain, fuel;
		std::vector<int64_t> oil;
		readArray(*j, "craters", terrain);
		readArray(*j, "fuel", fuel);
		readArray(*j, "oil", oil);
		modterrain::loadState(terrain);
		modfuel::loadState(fuel);
		modoil::loadState(oil);
		debug(LOG_INFO, "MOD: Mod-Zustand geladen (%zu Krater, %zu Tankfuellungen)", terrain.size() / 6, fuel.size() / 2);
	}
	catch (const std::exception &e)
	{
		debug(LOG_ERROR, "MOD: Mod-Zustand: %s", e.what());
	}
}
}
