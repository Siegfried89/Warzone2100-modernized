// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Mod-Zustand im Spielstand - siehe modsave.h
#include <nlohmann/json.hpp> // Must come before WZ includes

#include "modsave.h"
#include "modslots_json.h"
#include "modshield.h"

#include "lib/framework/frame.h"
#include "lib/framework/physfs_ext.h"
#include "lib/framework/file.h"
#include "game.h"
#include "modfuel.h"
#include "modoil.h"
#include "modterrain.h"
#include "modwreck.h"
#include "modrecover.h"
#include "modlayer.h"
#include "modcrew.h"
#include "modtunnel.h"
#include "modpaint.h"
#include "featuredef.h"
#include "feature.h"
#include "game_world.h"
#include "stats.h"

#include <vector>

namespace modsave
{
namespace
{
std::string s_pending;

// (17.11.) Bauplan eines Wracks als Namen (robust gegen geaenderte Datenreihenfolge)
const COMPONENT_STATS *compStats(int type, uint32_t idx)
{
	switch (type)
	{
	case COMP_BODY: return idx < asBodyStats.size() ? &asBodyStats[idx] : nullptr;
	case COMP_BRAIN: return idx < asBrainStats.size() ? &asBrainStats[idx] : nullptr;
	case COMP_PROPULSION: return idx < asPropulsionStats.size() ? &asPropulsionStats[idx] : nullptr;
	case COMP_REPAIRUNIT: return idx < asRepairStats.size() ? &asRepairStats[idx] : nullptr;
	case COMP_ECM: return idx < asECMStats.size() ? &asECMStats[idx] : nullptr;
	case COMP_SENSOR: return idx < asSensorStats.size() ? &asSensorStats[idx] : nullptr;
	case COMP_CONSTRUCT: return idx < asConstructStats.size() ? &asConstructStats[idx] : nullptr;
	case COMP_WEAPON: return idx < asWeaponStats.size() ? &asWeaponStats[idx] : nullptr;
	default: return nullptr;
	}
}

nlohmann::json infoToJson(const modwreck::WreckInfo &in)
{
	nlohmann::json j = nlohmann::json::object();
	j["type"] = in.droidType;
	nlohmann::json parts = nlohmann::json::array();
	for (int c = 0; c < DROID_MAXCOMP; ++c)
	{
		const COMPONENT_STATS *s = compStats(c, in.parts[c]);
		parts.push_back(s != nullptr ? s->id.toUtf8() : std::string());
	}
	j["parts"] = parts;
	nlohmann::json weaps = nlohmann::json::array();
	for (unsigned i = 0; i < in.numWeaps && i < MAX_WEAPONS; ++i)
	{
		const COMPONENT_STATS *s = compStats(COMP_WEAPON, in.weaps[i]);
		weaps.push_back(s != nullptr ? s->id.toUtf8() : std::string());
	}
	j["weapons"] = weaps;
	if (!in.slots.isDefault()) { j["modSlots"] = modslots::toJson(in.slots); }   // (23.11. 02:16)
	j["model"] = in.stats != nullptr ? in.stats->id.toUtf8() : std::string();
	return j;
}

bool infoFromJson(const nlohmann::json &j, modwreck::WreckInfo &out)
{
	if (!j.is_object()) { return false; }
	out = modwreck::WreckInfo();
	out.droidType = static_cast<uint8_t>(j.value("type", 0));
	const auto parts = j.find("parts");
	if (parts == j.end() || !parts->is_array()) { return false; }
	for (int c = 0; c < DROID_MAXCOMP && c < static_cast<int>(parts->size()); ++c)
	{
		const std::string nm = (*parts)[c].is_string() ? (*parts)[c].get<std::string>() : std::string();
		const int idx = nm.empty() ? 0 : getCompFromName(static_cast<COMPONENT_TYPE>(c), WzString::fromUtf8(nm));
		out.parts[c] = static_cast<uint32_t>(std::max(0, idx));
	}
	if (out.parts[COMP_BODY] == 0 && parts->size() > 0 && (*parts)[0].is_string() && !(*parts)[0].get<std::string>().empty())
	{
		const int idx = getCompFromName(COMP_BODY, WzString::fromUtf8((*parts)[0].get<std::string>()));
		if (idx < 0) { return false; }   // Rumpf gibt es nicht mehr
	}
	const auto weaps = j.find("weapons");
	if (weaps != j.end() && weaps->is_array())
	{
		for (const auto &w : *weaps)
		{
			if (out.numWeaps >= MAX_WEAPONS || !w.is_string()) { break; }
			const int idx = getCompFromName(COMP_WEAPON, WzString::fromUtf8(w.get<std::string>()));
			if (idx > 0) { out.weaps[out.numWeaps++] = static_cast<uint32_t>(idx); }
		}
	}
	const auto ms = j.find("modSlots");
	if (ms != j.end()) { modslots::fromJson(*ms, out.slots); }
	const std::string model = j.value("model", std::string());
	for (const FEATURE_STATS &fs : asFeatureStats) { if (fs.id.toUtf8() == model) { out.stats = &fs; break; } }
	return true;
}

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
		std::vector<int> shields;
		modshield::saveState(shields);   // MOD (18.11.): Schildkuppeln
		j["shields"] = shields;
		j["craters"] = terrain;
		j["fuel"] = fuel;
		j["oil"] = oil;
		// (17.11.) Wracks (Alter, Lage, Bauplan) und Abschlepp-Lkw mit Ladung - nach dem Laden bergbar
		nlohmann::json wr = nlohmann::json::array();
		for (const modwreck::SavedWreck &w : modwreck::saveState())
		{
			nlohmann::json e = nlohmann::json::object();
			e["id"] = w.featureId; e["age"] = w.age; e["x"] = w.xe; e["y"] = w.ye;
			e["hh"] = w.halfH; e["hw"] = w.halfW; e["roll"] = w.rollTotal;
			if (w.hasInfo) { e["info"] = infoToJson(w.info); }
			wr.push_back(e);
		}
		j["wrecks"] = wr;
		nlohmann::json tw = nlohmann::json::array();
		for (const auto &t : modrecover::saveTows())
		{
			nlohmann::json e = nlohmann::json::object();
			e["droid"] = t.first;
			e["info"] = infoToJson(t.second);
			tw.push_back(e);
		}
		j["tows"] = tw;
		// (Wunsch 17.11.) Untergrund/Orbit: Ebene je Objekt + gebohrte Untergrund-Felder
		std::vector<int64_t> lobjs;
		std::vector<int> ldrills;
		modlayer::saveState(lobjs, ldrills);
		j["layerObjs"] = lobjs;
		j["layerDrills"] = ldrills;
		std::vector<int64_t> crew;
		modcrew::saveState(crew);   // MOD (18.11.): Piloten mit Namen/Stufen, Combots ohne Besatzung
		j["crew"] = crew;
		std::vector<int64_t> tunnels, plan;
		modtunnel::saveState(tunnels);   // MOD (22.11.): Tunnel, Grabungen, Einheiten im Gang
		j["tunnels"] = tunnels;
		modpaint::savePlan(plan);        // MOD (22.11.): Angriffsplaner (Zonen, Brueckenkoepfe, Vektoren, Einheiten)
		j["planner"] = plan;
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
		std::vector<int> shields;
		readArray(*j, "shields", shields);
		modshield::loadState(shields);
		std::vector<modwreck::SavedWreck> wrecks;
		const auto wr = j->find("wrecks");
		if (wr != j->end() && wr->is_array())
		{
			for (const auto &e : *wr)
			{
				if (!e.is_object()) { continue; }
				modwreck::SavedWreck w;
				w.featureId = e.value("id", 0u);
				w.age = e.value("age", 0u);
				w.xe = e.value("x", 0); w.ye = e.value("y", 0);
				w.halfH = e.value("hh", 12); w.halfW = e.value("hw", 12); w.rollTotal = e.value("roll", 90);
				const auto in = e.find("info");
				w.hasInfo = in != e.end() && infoFromJson(*in, w.info);
				wrecks.push_back(w);
			}
		}
		modwreck::loadState(wrecks, gameWorld);
		std::vector<std::pair<uint32_t, modwreck::WreckInfo>> tows;
		const auto tw = j->find("tows");
		if (tw != j->end() && tw->is_array())
		{
			for (const auto &e : *tw)
			{
				modwreck::WreckInfo info;
				const auto in = e.is_object() ? e.find("info") : e.end();
				if (e.is_object() && in != e.end() && infoFromJson(*in, info)) { tows.emplace_back(e.value("droid", 0u), info); }
			}
		}
		modrecover::loadTows(tows);
		std::vector<int64_t> lobjs;
		std::vector<int> ldrills;
		readArray(*j, "layerObjs", lobjs);
		readArray(*j, "layerDrills", ldrills);
		modlayer::loadState(lobjs, ldrills);   // eingesetzt im ersten Spielschritt (wenn die Zusatzkarten stehen)
		std::vector<int64_t> crew;
		readArray(*j, "crew", crew);
		modcrew::loadState(crew);
		std::vector<int64_t> tunnels, plan;
		readArray(*j, "tunnels", tunnels);
		modtunnel::loadState(tunnels);
		readArray(*j, "planner", plan);
		modpaint::loadPlan(plan);
		debug(LOG_INFO, "MOD: Mod-Zustand geladen (%zu Krater, %zu Tankfuellungen)", terrain.size() / 6, fuel.size() / 2);
	}
	catch (const std::exception &e)
	{
		debug(LOG_ERROR, "MOD: Mod-Zustand: %s", e.what());
	}
}
}
