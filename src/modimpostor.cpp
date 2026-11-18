// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Einheiten als Sprites, weit herausgezoomt (siehe modimpostor.h)
#include <nlohmann/json.hpp> // vor den WZ-Headern
#include "modimpostor.h"

#include "lib/framework/frame.h"
#include "lib/framework/file.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/piedef.h"

#include "display3d.h"
#include "droid.h"
#include "stats.h"
#include "statsdef.h"

#include <cmath>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>

#include <glm/glm.hpp>

namespace modimpostor
{
namespace
{
struct Entry
{
	std::string pie;
	glm::vec3 c{0.f};
};

struct Imp
{
	const iIMDBaseShape *shape = nullptr;
	glm::vec3 c{0.f};
};

bool s_loaded = false;
int s_dirs = 8;
std::unordered_map<std::string, Entry> s_index;   // "rumpf|antrieb|turm" (Modell-Dateinamen, klein) -> Bild
std::map<std::tuple<const void *, const void *, const void *>, Imp> s_cache;

void load()
{
	s_loaded = true;
	std::vector<char> buf;
	if (!loadFileToBufferVector("stats/impostors.json", buf, false, false) || buf.empty()) { return; }
	nlohmann::json j = nlohmann::json::parse(buf.begin(), buf.end(), nullptr, false);
	if (j.is_discarded() || !j.is_object() || !j.contains("units") || !j["units"].is_object()) { return; }
	s_dirs = std::max(1, j.value("dirs", 8));
	for (auto it = j["units"].begin(); it != j["units"].end(); ++it)
	{
		const nlohmann::json &v = it.value();
		if (!v.is_object()) { continue; }
		Entry e;
		e.pie = v.value("pie", std::string());
		if (v.contains("c") && v["c"].is_array() && v["c"].size() == 3)
		{
			e.c = glm::vec3(v["c"][0].get<float>(), v["c"][1].get<float>(), v["c"][2].get<float>());
		}
		if (!e.pie.empty()) { s_index[it.key()] = e; }
	}
	debug(LOG_INFO, "Einheiten-Sprites: %zu Zusammenstellungen", s_index.size());
}

std::string nameOf(const iIMDBaseShape *s)
{
	return s != nullptr ? s->filename.toLower().toStdString() : std::string();
}

const Imp *find(const iIMDBaseShape *body, const iIMDBaseShape *prop, const iIMDBaseShape *turret)
{
	const auto key = std::make_tuple(static_cast<const void *>(body), static_cast<const void *>(prop), static_cast<const void *>(turret));
	static unsigned cacheGen = 0;
	if (modelCacheStale(cacheGen)) { s_cache.clear(); }   // MOD (18.11.): Modelle wurden neu geladen (Zeiger als Schluessel ungueltig)
	auto it = s_cache.find(key);
	if (it == s_cache.end())
	{
		Imp imp;
		const std::string b = nameOf(body), p = nameOf(prop), t = nameOf(turret);
		// genau diese Zusammenstellung, sonst Rumpf + Antrieb, sonst nur der Rumpf (selbst entworfene Einheiten)
		for (const std::string &k : { b + "|" + p + "|" + t, b + "|" + p + "|", b + "||" })
		{
			const auto f = s_index.find(k);
			if (f == s_index.end()) { continue; }
			imp.shape = modelGet(WzString::fromUtf8(f->second.pie));
			imp.c = f->second.c;
			if (imp.shape != nullptr) { break; }
		}
		it = s_cache.emplace(key, imp).first;
	}
	return it->second.shape != nullptr ? &it->second : nullptr;
}

const iIMDBaseShape *turretOf(const DROID *d)
{
	switch (d->droidType)
	{
	case DROID_WEAPON:
	case DROID_CYBORG:
	case DROID_CYBORG_SUPER:
	case DROID_COMMAND:
	case DROID_TRANSPORTER:
	case DROID_SUPERTRANSPORTER:
	case DROID_DEFAULT:
		if (d->numWeaps > 0 && d->asWeaps[0].nStat > 0 && d->getWeaponStats(0) != nullptr) { return d->getWeaponStats(0)->pIMD; }
		return nullptr;
	case DROID_SENSOR:
		return d->getSensorStats() != nullptr ? d->getSensorStats()->pIMD : nullptr;
	case DROID_CONSTRUCT:
	case DROID_CYBORG_CONSTRUCT:
		return d->getConstructStats() != nullptr ? d->getConstructStats()->pIMD : nullptr;
	case DROID_REPAIR:
	case DROID_CYBORG_REPAIR:
		return d->getRepairStats() != nullptr ? d->getRepairStats()->pIMD : nullptr;
	case DROID_ECM:
		return d->getECMStats() != nullptr ? d->getECMStats()->pIMD : nullptr;
	default:
		return nullptr;   // Infanterie: Waffe steckt im Koerpermodell
	}
}
}

bool draw(const DROID *psDroid, const glm::mat4 &modelMatrix, const glm::mat4 &viewMatrix, int teamColour, PIELIGHT brightness)
{
	if (psDroid == nullptr || getViewDistance() < LOD_FAR) { return false; }
	if (!s_loaded) { load(); }
	if (s_index.empty()) { return false; }
	const BODY_STATS *bs = psDroid->getBodyStats();
	if (bs == nullptr) { return false; }
	const iIMDBaseShape *body = bs->pickIMD(psDroid->id);
	const size_t pi = static_cast<size_t>(psDroid->asBits[COMP_PROPULSION]) * NUM_PROP_SIDES + LEFT_PROP;
	const iIMDBaseShape *prop = pi < bs->ppIMDList.size() ? bs->ppIMDList[pi] : nullptr;
	const Imp *imp = find(body, prop, turretOf(psDroid));
	if (imp == nullptr) { return false; }

	// Blickrichtung der Kamera im Modellraum der Einheit -> Bildnummer (gleiche Festlegung wie beim Rendern in impostors.py)
	const glm::vec3 camWorld(glm::inverse(viewMatrix)[3]);
	const glm::vec3 camModel(glm::inverse(modelMatrix) * glm::vec4(camWorld, 1.f));
	const glm::vec3 v = camModel - imp->c;
	const float step = 6.28318530718f / static_cast<float>(s_dirs);
	int k = static_cast<int>(std::lround(std::atan2(v.x, v.z) / step));
	k = ((k % s_dirs) + s_dirs) % s_dirs;

	// Viereck zur Kamera gedreht, Mittelpunkt = Mitte des zusammengesetzten Modells
	glm::mat4 bill(glm::transpose(glm::mat3(viewMatrix)));
	bill[3] = modelMatrix * glm::vec4(imp->c, 1.f);
	return pie_Draw3DShape(imp->shape->displayModel(), k, teamColour, brightness, pie_TRANSLUCENT | pie_FORCELIGHT, 255, bill, viewMatrix);
}

void reset()
{
	s_cache.clear();
}
}
