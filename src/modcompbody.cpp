// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 23.11. 01:35): Verbundrumpfe aus dem Rumpf-Baukasten (siehe modcompbody.h)
#include <nlohmann/json.hpp> // Must come before WZ includes

#include "modcompbody.h"

#include "lib/framework/frame.h"
#include "lib/framework/file.h"
#include "lib/framework/physfs_ext.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/png_util.h"   // (27.11.) umgefaerbte Texturen
#include "lib/ivis_opengl/pietypes.h"
#include "lib/netplay/netplay.h"

#include "game.h"
#include "stats.h"
#include "droid.h"
#include "statsdef.h"
#include "basedef.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>

static_assert(modcompbody::MAX_SLOTS <= MAX_WEAPONS, "Waffenplaetze eines Verbundrumpfes > MAX_WEAPONS");

namespace modcompbody
{

namespace
{

constexpr size_t FREE_SLOTS = 24;                    // neue Entwuerfe je Spielsitzung (danach: neues Spiel starten)
constexpr const char *USER_DIR = "userdata/composites";
constexpr const char *PACK_DIR = "composites";
constexpr const char *FREE_PREFIX = "CompBodyFree";

std::vector<Def> s_defs;                             // alle bekannten Bauplaene
struct Slot
{
	size_t bodyIdx = 0;
	std::string defId;                               // leer = frei
	glm::mat4 prop = glm::mat4(1.f);
	bool propShift = false;
	float lift = 0.f;                                // (24.11.) Rumpf ueber das Laufwerk heben (Teile unter dem Fahrgestell)
	std::vector<size_t> partIdx;                     // Teil-Rumpfe (fuer die Verfuegbarkeit)
	std::vector<float> yaw;                          // Blickrichtung je Anschluss (Grad)
	std::vector<float> pitch, roll;                  // (27.11.) Neigung je Anschluss (Grad)
	std::vector<glm::mat4> driveM;                   // (27.11.) Antriebsplaetze: Lage
	std::vector<size_t> driveBody;                   // (27.11.) Antriebsplaetze: Rumpf (asBodyStats), dessen Laufwerk-Modelle sie tragen
};
std::vector<Slot> s_slots;
size_t s_first = 0;
std::function<void(COMPONENT_STATS *, const WzString &)> s_lookup;

std::string slug(const std::string &name)
{
	std::string out;
	for (char ch : name)
	{
		const unsigned char c = static_cast<unsigned char>(ch);
		if (std::isalnum(c)) { out += static_cast<char>(c); }
		else if (c == ' ' || c == '-' || c == '_') { if (!out.empty() && out.back() != '_') { out += '_'; } }
	}
	while (!out.empty() && out.back() == '_') { out.pop_back(); }
	if (out.empty()) { out = "Entwurf"; }
	if (out.size() > 40) { out.resize(40); }
	return out;
}

nlohmann::json vec3(const glm::vec3 &v) { return nlohmann::json::array({ v.x, v.y, v.z }); }
glm::vec3 vec3(const nlohmann::json &j)
{
	glm::vec3 v(0.f);
	if (j.is_array()) { for (size_t k = 0; k < 3 && k < j.size(); ++k) { if (j[k].is_number()) { v[static_cast<int>(k)] = j[k].get<float>(); } } }
	return v;
}

nlohmann::json toJson(const Def &d)
{
	nlohmann::json j = nlohmann::json::object();
	j["id"] = d.id;
	j["name"] = d.name;
	j["version"] = 1;
	nlohmann::json parts = nlohmann::json::array();
	for (const Part &p : d.parts)
	{
		nlohmann::json e = nlohmann::json::object();
		e["body"] = p.body; e["pos"] = vec3(p.pos); e["rot"] = vec3(p.rot);
		if (std::fabs(p.scale - 1.f) > 1e-4f) { e["scale"] = p.scale; }
		if (p.flip != 0) { e["flip"] = p.flip; }
		if (glm::length(p.stretch - glm::vec3(1.f)) > 1e-4f) { e["stretch"] = vec3(p.stretch); }   // (24.11.) Form
		if (!p.texFrom.empty() && p.texFrom != p.body) { e["tex"] = p.texFrom; }                  // (27.11.) Farben eines anderen Rumpfes
		parts.push_back(e);
	}
	j["parts"] = parts;
	nlohmann::json mounts = nlohmann::json::array();
	for (const Mount &m : d.mounts)
	{
		nlohmann::json e = vec3(m.pos);
		const bool tilt = std::fabs(m.pitch) > 1e-3f || std::fabs(m.roll) > 1e-3f;
		if (std::fabs(m.yaw) > 1e-3f || tilt) { e.push_back(m.yaw); }   // [x, y, z, Blickrichtung]
		if (tilt) { e.push_back(m.pitch); e.push_back(m.roll); }        // (27.11.) [..., Nase, Rollen]
		mounts.push_back(e);
	}
	j["mounts"] = mounts;
	if (!d.drives.empty())   // (27.11.) Antriebsplaetze
	{
		nlohmann::json drives = nlohmann::json::array();
		for (const Drive &dr : d.drives)
		{
			nlohmann::json e = nlohmann::json::object();
			e["pos"] = vec3(dr.pos);
			if (std::fabs(dr.yaw) > 1e-3f) { e["yaw"] = dr.yaw; }
			if (std::fabs(dr.scale - 1.f) > 1e-4f) { e["scale"] = dr.scale; }
			if (!dr.body.empty()) { e["body"] = dr.body; }
			drives.push_back(e);
		}
		j["drives"] = drives;
	}
	if (d.deleted) { j["deleted"] = true; }
	return j;
}

bool fromJson(const nlohmann::json &j, Def &d)
{
	if (!j.is_object() || !j.contains("parts") || !j["parts"].is_array()) { return false; }
	d.id = j.value("id", std::string());
	d.name = j.value("name", d.id);
	d.deleted = j.value("deleted", false);
	d.parts.clear(); d.mounts.clear(); d.drives.clear();
	for (const auto &e : j["parts"])
	{
		if (!e.is_object() || !e.contains("body")) { continue; }
		Part p;
		p.body = e.value("body", std::string());
		if (e.contains("pos")) { p.pos = vec3(e["pos"]); }
		if (e.contains("rot")) { p.rot = vec3(e["rot"]); }
		p.scale = std::clamp(e.value("scale", 1.f), 0.1f, 4.f);
		p.flip = std::clamp(e.value("flip", 0), 0, 7);
		if (e.contains("stretch")) { p.stretch = glm::clamp(vec3(e["stretch"]), glm::vec3(0.25f), glm::vec3(4.f)); }
		p.texFrom = e.value("tex", std::string());
		d.parts.push_back(p);
	}
	if (j.contains("mounts") && j["mounts"].is_array())
	{
		for (const auto &m : j["mounts"])
		{
			Mount mm; mm.pos = vec3(m);
			if (m.is_array() && m.size() >= 4 && m[3].is_number()) { mm.yaw = std::remainder(m[3].get<float>(), 360.f); }
			if (m.is_array() && m.size() >= 6 && m[4].is_number() && m[5].is_number())
			{
				mm.pitch = std::remainder(m[4].get<float>(), 360.f);
				mm.roll = std::remainder(m[5].get<float>(), 360.f);
			}
			d.mounts.push_back(mm);
		}
	}
	if (j.contains("drives") && j["drives"].is_array())   // (27.11.) Antriebsplaetze
	{
		for (const auto &e : j["drives"])
		{
			if (!e.is_object() || d.drives.size() >= MAX_DRIVES) { continue; }
			Drive dr;
			if (e.contains("pos")) { dr.pos = vec3(e["pos"]); }
			dr.yaw = std::remainder(e.value("yaw", 0.f), 360.f);
			dr.scale = std::clamp(e.value("scale", 1.f), 0.25f, 4.f);
			dr.body = e.value("body", std::string());
			d.drives.push_back(dr);
		}
	}
	return !d.id.empty() && !d.parts.empty();
}

void readDir(const char *dir, bool preset, std::map<std::string, Def> &out)
{
	if (!PHYSFS_exists(dir)) { return; }
	std::vector<std::string> files;
	WZ_PHYSFS_enumerateFiles(dir, [&](const char *f) -> bool {
		const std::string n(f);
		if (n.size() > 5 && n.compare(n.size() - 5, 5, ".json") == 0) { files.push_back(n); }
		return true;
	});
	std::sort(files.begin(), files.end());
	for (const std::string &f : files)
	{
		const std::string path = std::string(dir) + "/" + f;
		std::vector<char> buf;
		if (!loadFileToBufferVector(path.c_str(), buf, false, true)) { continue; }
		try
		{
			const nlohmann::json j = nlohmann::json::parse(buf.data());
			Def d;
			if (!fromJson(j, d)) { debug(LOG_ERROR, "Verbundrumpf %s: ungueltig", path.c_str()); continue; }
			d.preset = preset;
			out[d.id] = d;   // Benutzerordner wird nach dem Paket gelesen -> hat Vorrang
		}
		catch (const std::exception &e)
		{
			debug(LOG_ERROR, "Verbundrumpf %s ist kein gueltiges JSON (%s)", path.c_str(), e.what());
		}
	}
}

BODY_STATS *bodyByIdMutable(const std::string &id)
{
	for (BODY_STATS &b : asBodyStats) { if (b.id.toStdString() == id) { return &b; } }
	return nullptr;
}

bool isUsableChassisPart(const BODY_STATS *b)
{
	return b != nullptr && b->pIMD != nullptr && b->pIMD->displayModel() != nullptr && !checkIfZNullStat(b);
}

// ---------------------------------------------------------------- Katalog (Spiel: asBodyStats, Hauptmenue: stats/body.json)
struct Catalog
{
	std::vector<BodyInfo> all;
	std::map<std::string, size_t> byId;
	bool game = false;
	uint32_t statsGen = 0;
	size_t count = 0;
	unsigned modelGen = 0;
	bool built = false;
};
Catalog s_cat;

bool startsWith(const std::string &s, const char *p) { return s.rfind(p, 0) == 0; }
bool endsWith(const std::string &s, const char *p)
{
	const size_t n = std::strlen(p);
	return s.size() >= n && s.compare(s.size() - n, n, p) == 0;
}
bool contains(const std::string &s, const char *p) { return s.find(p) != std::string::npos; }

/// taugt der Rumpf als Teil? (keine Infanterie, Cyborgs, Platzhalter, Stellvertreter-Modelle, Verbundrumpfe)
bool usableAsPart(const std::string &id, const std::string &cls, const std::string &model)
{
	if (id.empty() || startsWith(id, "ZNULL") || startsWith(id, "CompBody") || startsWith(id, "B1") || startsWith(id, "MFT_")) { return false; }
	if (cls == "Cyborgs" || cls == "Babas_Person") { return false; }
	if (model.empty() || model == "mfproxy.pie" || startsWith(model, "exbloke")) { return false; }
	return true;
}

const char *const WZ_FACTION_BODIES[] = {"Body1REC", "Body5REC", "Body11ABT", "Body4ABT", "Body8MBT", "Body12SUP", "Body2SUP", "Body6SUPP", "Body9REC",
                                        "Body3MBT", "Body7ABT", "Body10MBT", "Body13SUP", "Body14SUP"};

/// Herkunft (Spiel/Mod) und Fraktion/Gruppe nach ID und Modell
void classify(BodyInfo &b, const std::string &cls, const std::string &model)
{
	const std::string &id = b.id;
	static const struct { const char *id; const char *faction; int rank; } WZ[] = {
		{ "Body1REC", "Project", 0 }, { "Body5REC", "Project", 0 }, { "Body11ABT", "Project", 0 },
		{ "Body4ABT", "New Paradigm", 1 }, { "Body8MBT", "New Paradigm", 1 }, { "Body12SUP", "New Paradigm", 1 },
		{ "Body2SUP", "Collective", 2 }, { "Body6SUPP", "Collective", 2 }, { "Body9REC", "Collective", 2 },
		{ "Body3MBT", "Nexus", 3 }, { "Body7ABT", "Nexus", 3 }, { "Body10MBT", "Nexus", 3 }, { "Body13SUP", "Nexus", 3 }, { "Body14SUP", "Nexus", 3 },
	};
	// (Wunsch 23.11. 23:24 "da fehlt noch die Einteilung nach Projekt, New Paradigm, Kollektiv, Nexus - oben bei den anderen wie MF, Mods"):
	// jede WZ-Fraktion ist eine eigene Herkunft (eigener Reiter), die WZ-Transporter bleiben unter "Warzone 2100"
	for (const auto &w : WZ)
	{
		if (id == w.id) { b.source = w.faction; b.sourceRank = w.rank; b.group = w.faction; b.groupRank = 0; return; }
	}
	if (startsWith(id, "B3MF"))
	{
		b.source = "Metal Fatigue"; b.sourceRank = 7;
		if (contains(id, "Tank") || contains(id, "Arty"))
		{
			if (contains(id, "Rimtech")) { b.group = "Rimtech"; b.groupRank = 0; }
			else if (contains(id, "MilAgro")) { b.group = "Mil-Agro"; b.groupRank = 1; }
			else { b.group = "Neuropa"; b.groupRank = 2; }
		}
		else if (startsWith(id, "B3MFJet")) { b.group = "Hoverjets"; b.groupRank = 3; }
		else { b.group = "Vehicles"; b.groupRank = 4; }
		return;
	}
	if (startsWith(model, "w2_"))
	{
		b.source = "WZ2120"; b.sourceRank = 6;
		b.group = b.size <= SIZE_LIGHT ? "Light" : (b.size == SIZE_MEDIUM ? "Medium" : "Heavy");
		b.groupRank = b.size;
		return;
	}
	if (id == "RigBody" || id == "PitbullBody" || id == "MammothBody" || id == "HarvesterBody") { b.source = "C&C 3"; b.sourceRank = 8; b.group = "GDI"; return; }
	if (startsWith(id, "NTWShip") || id == "Body101") { b.source = "Naval"; b.sourceRank = 9; b.group = "Ships"; return; }   // (24.11.) Zerstoerer (morefort) zu den Schiffen
	if (startsWith(id, "UEF")) { b.source = "Supreme Commander"; b.sourceRank = 10; b.group = "UEF - Air"; b.groupRank = 1; return; }
	// (Wunsch 27.11. 19:27 "saemtliche 3d Einheiten, Modelle von SupCom in den SupCom-Ordner in der Hull Forge") Kennung "SC_" + SupCom-Code
	// (z.B. SC_UEL0401 = Fatboy): 2. Buchstabe Fraktion (E UEF, A Aeon, R Cybran, X Kampagne), 3. Buchstabe Art (L Land, A Luft, S See)
	if (startsWith(id, "SC_") && id.size() >= 6)
	{
		static const char *const FAC[4] = { "UEF", "Aeon", "Cybran", "Campaign" };
		static const char *const KIND[3] = { "Land", "Air", "Naval" };
		const char f = id[4], t = id[5];
		const int fi = f == 'E' ? 0 : (f == 'A' ? 1 : (f == 'R' ? 2 : 3));
		const int ti = t == 'A' ? 1 : (t == 'S' ? 2 : 0);
		b.source = "Supreme Commander"; b.sourceRank = 10;
		b.group = std::string(FAC[fi]) + " - " + KIND[ti]; b.groupRank = fi * 3 + ti;
		return;
	}
	if (cls == "Babas" || endsWith(id, "-Scav") || endsWith(id, "-Ultimate") || startsWith(id, "Scav") || startsWith(id, "B2") || startsWith(id, "B3body")
	    || startsWith(id, "B4body") || id == "BusBody" || id == "FireBody" || contains(id, "Chopper") || startsWith(id, "Train"))
	{
		b.source = "Scavengers"; b.sourceRank = 5;
		if (endsWith(id, "-Ultimate") || id == "B2crane2") { b.group = "Ultimate Scavengers"; b.groupRank = 1; }
		else if (startsWith(id, "Train")) { b.group = "Trains"; b.groupRank = 4; }
		else if (contains(id, "Chopper")) { b.group = "Helicopters"; b.groupRank = 3; }
		else if (endsWith(id, "-Scav") || startsWith(id, "B2Scav") || startsWith(id, "ScavAPC")) { b.group = "Scavenger vehicles"; b.groupRank = 2; }
		else { b.group = "Scavengers"; b.groupRank = 0; }
		return;
	}
	if (cls == "Transports") { b.source = "Warzone 2100"; b.sourceRank = 4; b.group = "Transports"; b.groupRank = 0; return; }
	b.source = "Mods"; b.sourceRank = 11; b.group = "Extra hulls"; b.groupRank = 0;
}

int sizeFromText(const std::string &s)
{
	if (s == "MEDIUM") { return SIZE_MEDIUM; }
	if (s == "HEAVY") { return SIZE_HEAVY; }
	if (s == "SUPER HEAVY") { return SIZE_SUPER_HEAVY; }
	return SIZE_LIGHT;
}

unsigned jsonU(const nlohmann::json &e, const char *k)
{
	auto it = e.find(k);
	if (it == e.end() || !it->is_number()) { return 0; }
	return static_cast<unsigned>(std::max(0.0, it->get<double>()));
}

void finishCatalog(Catalog &c)
{
	std::stable_sort(c.all.begin(), c.all.end(), [](const BodyInfo &a, const BodyInfo &b) {
		if (a.sourceRank != b.sourceRank) { return a.sourceRank < b.sourceRank; }
		if (a.group != b.group) { return a.groupRank != b.groupRank ? a.groupRank < b.groupRank : a.group < b.group; }
		if (a.size != b.size) { return a.size < b.size; }
		return a.name < b.name;
	});
	// dasselbe Modell nur einmal in der Liste (der baubare Eintrag zuerst)
	std::map<const iIMDBaseShape *, size_t> seen;
	for (size_t i = 0; i < c.all.size(); ++i)
	{
		BodyInfo &b = c.all[i];
		c.byId[b.id] = i;
		auto it = seen.find(b.model);
		if (it == seen.end()) { seen[b.model] = i; continue; }
		BodyInfo &first = c.all[it->second];
		// (23.11. 23:24 "im Hull Designer sollten alle Vanilla-Ruempfe vorhanden sein"): die fruehere Herkunft (WZ-Fraktion vor Mod)
		// gewinnt immer - vorher verdraengte ein baubarer Mod-Rumpf mit demselben Modell den Python ("Superbody")
		if (b.designable && !first.designable && b.sourceRank == first.sourceRank) { first.listed = false; it->second = i; }
		else { b.listed = false; }
	}
}

void buildFromStats(Catalog &c)
{
	for (size_t i = 0; i < asBodyStats.size(); ++i)
	{
		const BODY_STATS &s = asBodyStats[i];
		if (s.pIMD == nullptr || s.pIMD->displayModel() == nullptr || checkIfZNullStat(&s) || isComposite(i)) { continue; }
		const std::string id = s.id.toStdString();
		const std::string model = s.pIMD->filename.toStdString();
		const std::string cls = s.bodyClass.toStdString();
		if (!usableAsPart(id, cls, model)) { continue; }
		BodyInfo b;
		b.id = id;
		b.name = getLocalizedStatsName(&s);
		if (b.name.empty()) { b.name = id; }
		b.model = s.pIMD;
		b.hp = s.base.hitpoints; b.armour = s.base.armour; b.thermal = static_cast<unsigned>(std::max(0, s.base.thermal));
		b.weight = s.weight; b.power = s.base.power; b.cost = s.buildPower; b.buildPoints = s.buildPoints;
		b.weaponSlots = std::max(1u, s.weaponSlots);
		b.size = static_cast<int>(s.size);
		b.designable = s.designable;
		b.statIdx = i;
		// (27.11.) Laufwerk fuer die Vorschau der Antriebsplaetze: Ketten, sonst Halbketten, Raeder, Hover
		for (const char *pid : { "tracked01", "HalfTrack", "wheeled01", "hover01" })
		{
			const int pi = getCompFromID(COMP_PROPULSION, WzString::fromUtf8(pid));
			if (pi < 0) { continue; }
			const size_t k = static_cast<size_t>(pi) * NUM_PROP_SIDES + LEFT_PROP;
			if (k < s.ppIMDList.size() && s.ppIMDList[k] != nullptr) { b.driveModel = s.ppIMDList[k]; break; }
		}
		classify(b, cls, model);
		c.all.push_back(b);
	}
}

void buildFromJson(Catalog &c)
{
	if (!PHYSFS_exists("stats/body.json")) { return; }
	auto j = parseJsonFile("stats/body.json");
	if (!j.has_value() || !j->is_object()) { return; }
	for (auto it = j->begin(); it != j->end(); ++it)
	{
		const nlohmann::json &e = it.value();
		if (!e.is_object()) { continue; }
		const std::string id = it.key();
		const std::string model = e.value("model", std::string());
		const std::string cls = e.value("class", std::string());
		if (!usableAsPart(id, cls, model)) { continue; }
		iIMDBaseShape *shape = modelGet(WzString::fromUtf8(model));
		if (shape == nullptr || shape->displayModel() == nullptr) { continue; }
		BodyInfo b;
		b.id = id;
		b.name = e.value("name", id);
		b.name.erase(std::remove(b.name.begin(), b.name.end(), '*'), b.name.end());
		if (b.name.empty()) { b.name = id; }
		b.model = shape;
		b.hp = jsonU(e, "hitpoints"); b.armour = jsonU(e, "armourKinetic"); b.thermal = jsonU(e, "armourHeat");
		b.weight = jsonU(e, "weight"); b.power = jsonU(e, "powerOutput"); b.cost = jsonU(e, "buildPower"); b.buildPoints = jsonU(e, "buildPoints");
		b.weaponSlots = std::max(1u, jsonU(e, "weaponSlots"));
		b.size = sizeFromText(e.value("size", std::string("LIGHT")));
		auto des = e.find("designable");
		b.designable = des != e.end() && ((des->is_number() && des->get<double>() != 0.0) || (des->is_boolean() && des->get<bool>()));
		auto pem = e.find("propulsionExtraModels");   // (27.11.) Laufwerk fuer die Vorschau der Antriebsplaetze
		if (pem != e.end() && pem->is_object())
		{
			for (const char *pid : { "tracked01", "HalfTrack", "wheeled01", "hover01" })
			{
				auto pe = pem->find(pid);
				if (pe == pem->end() || !pe->is_object()) { continue; }
				const std::string lm = pe->value("left", std::string());
				iIMDBaseShape *dm = lm.empty() ? nullptr : modelGet(WzString::fromUtf8(lm));
				if (dm != nullptr) { b.driveModel = dm; break; }
			}
		}
		classify(b, cls, model);
		c.all.push_back(b);
	}
}

const Catalog &cat()
{
	const bool game = !asBodyStats.empty();
	unsigned mg = s_cat.modelGen;
	const bool modelsChanged = modelCacheStale(mg);
	if (s_cat.built && !modelsChanged && s_cat.game == game && (!game || (s_cat.statsGen == statsGeneration() && s_cat.count == asBodyStats.size()))) { return s_cat; }
	Catalog c;
	c.game = game;
	c.statsGen = statsGeneration();
	c.count = asBodyStats.size();
	c.modelGen = mg;
	c.built = true;
	if (game) { buildFromStats(c); } else { buildFromJson(c); }
	finishCatalog(c);
	s_cat = std::move(c);
	return s_cat;
}

// ---------------------------------------------------------------- (27.11.) Farben eines anderen Rumpfes
// Wunsch 27.11. 19:07: "einem Viper-Body kurzerhand die Kollektiv-Textur geben". Alle WZ-Ruempfe liegen auf derselben Texturseite, nur an
// verschiedenen Stellen - ein Seitentausch ginge also nicht. Stattdessen wird die Textur des Teils in den Farbverlauf des anderen Rumpfes
// umgefaerbt: helle Stellen bekommen dessen helle Farben, dunkle dessen dunkle (Angleich ueber die Helligkeitsverteilung). Muster, Kanten und
// Details des Teils bleiben, Farbgebung und Stimmung kommen vom anderen Rumpf. Die umgefaerbte Seite liegt einmal im Benutzerordner
// (texpages/modtex_...png) - Teamfarben-, Normalen- und Glanzkarten bleiben die des Teils (gleiche Aufteilung).
namespace
{
std::map<std::string, std::shared_ptr<iV_Image>> s_texImages;   // geladene Seiten (nur waehrend des Umfaerbens)
std::map<std::string, bool> s_texDone;                          // erzeugte Seiten in diesem Programmlauf (true = vorhanden)

std::shared_ptr<iV_Image> loadPage(const std::string &file)
{
	auto it = s_texImages.find(file);
	if (it != s_texImages.end()) { return it->second; }
	auto img = std::make_shared<iV_Image>();
	const std::string path = "texpages/" + file;
	if (!PHYSFS_exists(path.c_str()) || !iV_loadImage_PNG2(path.c_str(), *img, true, true) || img->channels() != 4) { img.reset(); }
	s_texImages[file] = img;
	return img;
}

/// Texel (Pixelindizes) unter den UV-Dreiecken der Ebenen eines Modells, die die Seite file benutzen
void texelsOf(const iIMDShape *model, const std::string &file, unsigned w, unsigned h, std::vector<uint32_t> &out)
{
	std::vector<uint8_t> seen(static_cast<size_t>(w) * h, 0);
	for (const iIMDShape *lvl = model; lvl != nullptr; lvl = lvl->next.get())
	{
		if (lvl->tilesetTextureFiles[0].texfile != file) { continue; }
		for (const iIMDPoly &poly : lvl->polys)
		{
			float px[3], py[3];
			for (int k = 0; k < 3; ++k)
			{
				px[k] = std::clamp(poly.texCoord[k].x, 0.f, 1.f) * static_cast<float>(w);
				py[k] = std::clamp(poly.texCoord[k].y, 0.f, 1.f) * static_cast<float>(h);
			}
			const int x0 = std::max(0, static_cast<int>(std::floor(std::min({ px[0], px[1], px[2] }))));
			const int x1 = std::min(static_cast<int>(w) - 1, static_cast<int>(std::ceil(std::max({ px[0], px[1], px[2] }))));
			const int y0 = std::max(0, static_cast<int>(std::floor(std::min({ py[0], py[1], py[2] }))));
			const int y1 = std::min(static_cast<int>(h) - 1, static_cast<int>(std::ceil(std::max({ py[0], py[1], py[2] }))));
			const float area = (px[1] - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (py[1] - py[0]);
			if (std::fabs(area) < 1e-6f) { continue; }
			for (int y = y0; y <= y1; ++y)
			{
				for (int x = x0; x <= x1; ++x)
				{
					const float cx = static_cast<float>(x) + 0.5f, cy = static_cast<float>(y) + 0.5f;
					const float w0 = ((px[1] - cx) * (py[2] - cy) - (px[2] - cx) * (py[1] - cy)) / area;
					const float w1 = ((px[2] - cx) * (py[0] - cy) - (px[0] - cx) * (py[2] - cy)) / area;
					const float w2 = 1.f - w0 - w1;
					const float e = -0.02f;   // Kanten mitnehmen
					if (w0 < e || w1 < e || w2 < e) { continue; }
					const size_t i = static_cast<size_t>(y) * w + static_cast<size_t>(x);
					if (!seen[i]) { seen[i] = 1; out.push_back(static_cast<uint32_t>(i)); }
				}
			}
		}
	}
}

inline int lumOf(const unsigned char *p) { return (p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8; }

/// Name der umgefaerbten Seite fuer die Ebenen von target mit der Seite file in den Farben von source; leer = geht nicht (eigene Textur)
std::string recoloredPage(const BodyInfo &target, const std::string &file, const BodyInfo &source)
{
	if (file.empty() || target.model == nullptr || source.model == nullptr || target.id == source.id) { return std::string(); }
	const std::string stem = file.substr(0, file.find_last_of('.'));
	const std::string name = "modtex_" + slug(target.id) + "_" + slug(source.id) + "_" + slug(stem) + ".png";
	auto done = s_texDone.find(name);
	if (done != s_texDone.end()) { return done->second ? name : std::string(); }
	s_texDone[name] = false;
	const std::shared_ptr<iV_Image> page = loadPage(file);
	if (!page) { debug(LOG_INFO, "MOD Hull Forge: Textur %s nicht als PNG lesbar - Teil behaelt seine Farben", file.c_str()); return std::string(); }
	// Farbverlauf des anderen Rumpfes: alle seine Texel (auch auf anderen Seiten), nach Helligkeit sortiert, in 256 Stufen gemittelt
	std::vector<std::array<int, 4>> src;   // Helligkeit, r, g, b
	for (const iIMDShape *lvl = source.model->displayModel(); lvl != nullptr; lvl = lvl->next.get())
	{
		const std::string &sf = lvl->tilesetTextureFiles[0].texfile;
		if (sf.empty()) { continue; }
		const std::shared_ptr<iV_Image> sp = loadPage(sf);
		if (!sp) { continue; }
		std::vector<uint32_t> idx;
		texelsOf(lvl, sf, sp->width(), sp->height(), idx);
		const unsigned char *b = sp->bmp();
		for (uint32_t i : idx)
		{
			const unsigned char *q = b + static_cast<size_t>(i) * 4;
			if (q[3] < 32) { continue; }
			src.push_back({ lumOf(q), q[0], q[1], q[2] });
		}
		break;   // die erste Ebene genuegt (weitere Ebenen sind Detailstufen desselben Rumpfes)
	}
	std::vector<uint32_t> tgt;
	texelsOf(target.model->displayModel(), file, page->width(), page->height(), tgt);
	if (src.size() < 64 || tgt.size() < 16) { s_texImages.clear(); return std::string(); }
	std::sort(src.begin(), src.end(), [](const std::array<int, 4> &a, const std::array<int, 4> &c) { return a[0] < c[0]; });
	unsigned char ramp[256][3];
	for (int k = 0; k < 256; ++k)
	{
		const size_t a = src.size() * static_cast<size_t>(k) / 256, e = std::max(a + 1, src.size() * static_cast<size_t>(k + 1) / 256);
		long r = 0, g = 0, bl = 0;
		for (size_t i = a; i < e && i < src.size(); ++i) { r += src[i][1]; g += src[i][2]; bl += src[i][3]; }
		const long n = static_cast<long>(std::max<size_t>(1, std::min(e, src.size()) - a));
		ramp[k][0] = static_cast<unsigned char>(r / n); ramp[k][1] = static_cast<unsigned char>(g / n); ramp[k][2] = static_cast<unsigned char>(bl / n);
	}
	// Helligkeitsverteilung des Teils -> Rang (0..255) je Helligkeit
	unsigned char *pb = page->bmp_w();
	std::vector<uint32_t> hist(256, 0);
	for (uint32_t i : tgt) { hist[static_cast<size_t>(lumOf(pb + static_cast<size_t>(i) * 4))]++; }
	unsigned char rank[256];
	uint64_t acc = 0;
	for (int l = 0; l < 256; ++l)
	{
		const uint64_t mid = acc + hist[static_cast<size_t>(l)] / 2;
		rank[l] = static_cast<unsigned char>(std::min<uint64_t>(255, mid * 256 / std::max<size_t>(1, tgt.size())));
		acc += hist[static_cast<size_t>(l)];
	}
	for (uint32_t i : tgt)
	{
		unsigned char *q = pb + static_cast<size_t>(i) * 4;
		const unsigned char *c = ramp[rank[lumOf(q)]];
		q[0] = c[0]; q[1] = c[1]; q[2] = c[2];
	}
	PHYSFS_mkdir("texpages");
	const std::string out = "texpages/" + name;
	const IMGSaveError err = iV_saveImage_PNG(out.c_str(), page.get());
	s_texImages.clear();   // die Seite wurde veraendert - beim naechsten Mal neu laden
	if (!err.noError()) { debug(LOG_INFO, "MOD Hull Forge: %s nicht geschrieben (%s)", out.c_str(), err.text.c_str()); return std::string(); }
	debug(LOG_INFO, "MOD Hull Forge: %s in den Farben von %s (%zu Texel)", target.id.c_str(), source.id.c_str(), tgt.size());
	s_texDone[name] = true;
	return name;
}
} // namespace

// ---------------------------------------------------------------- Modell (PIE 4, eine Ebene je Teil-Ebene, eigene Texturen)
void texLines(std::ostringstream &o, const iIMDShape &lvl, const std::string &texOverride = std::string())
{
	for (size_t t = 0; t < lvl.tilesetTextureFiles.size(); ++t)
	{
		const TilesetTextureFiles &f = lvl.tilesetTextureFiles[t];
		if (!f.texfile.empty()) { o << "TEXTURE " << t << " " << ((t == 0 && !texOverride.empty()) ? texOverride : f.texfile) << "\n"; }
	}
	for (size_t t = 0; t < lvl.tilesetTextureFiles.size(); ++t)
	{
		const TilesetTextureFiles &f = lvl.tilesetTextureFiles[t];
		if (!f.tcmaskfile.empty()) { o << "TCMASK " << t << " " << f.tcmaskfile << "\n"; }
	}
	for (size_t t = 0; t < lvl.tilesetTextureFiles.size(); ++t)
	{
		const TilesetTextureFiles &f = lvl.tilesetTextureFiles[t];
		if (!f.normalfile.empty()) { o << "NORMALMAP " << t << " " << f.normalfile << "\n"; }
	}
	for (size_t t = 0; t < lvl.tilesetTextureFiles.size(); ++t)
	{
		const TilesetTextureFiles &f = lvl.tilesetTextureFiles[t];
		if (!f.specfile.empty()) { o << "SPECULARMAP " << t << " " << f.specfile << "\n"; }
	}
}

std::string buildPie(const Def &d, std::vector<Vector3i> &connectorsOut)
{
	struct Lv { const iIMDShape *s; glm::mat4 m; bool mirrored; std::string tex; };
	std::vector<Lv> levels;
	for (const Part &p : d.parts)
	{
		const BodyInfo *b = bodyInfo(p.body);
		if (b == nullptr || b->model == nullptr || b->model->displayModel() == nullptr) { continue; }
		const glm::mat4 m = partMatrix(p);
		const bool mirrored = glm::determinant(glm::mat3(m)) < 0.f;
		const BodyInfo *texSrc = (!p.texFrom.empty() && p.texFrom != p.body) ? bodyInfo(p.texFrom) : nullptr;   // (27.11.)
		for (const iIMDShape *lvl = b->model->displayModel(); lvl != nullptr; lvl = lvl->next.get())
		{
			if (lvl->points.empty() || lvl->polys.empty()) { continue; }
			if (lvl->tilesetTextureFiles[0].texfile.empty()) { continue; }
			levels.push_back({ lvl, m, mirrored, texSrc != nullptr ? recoloredPage(*b, lvl->tilesetTextureFiles[0].texfile, *texSrc) : std::string() });
		}
	}
	std::ostringstream o;
	o.setf(std::ios::fixed); o.precision(3);
	if (levels.empty()) { return std::string(); }
	o << "PIE 4\n";
	o << "TYPE " << std::hex << levels[0].s->flags << std::dec << "\n";
	texLines(o, *levels[0].s, levels[0].tex);
	o << "LEVELS " << levels.size() << "\n";
	for (size_t li = 0; li < levels.size(); ++li)
	{
		const iIMDShape &s = *levels[li].s;
		const glm::mat4 &m = levels[li].m;
		o << "LEVEL " << (li + 1) << "\n";
		o << "TYPE " << std::hex << s.flags << std::dec << "\n";
		texLines(o, s, levels[li].tex);
		o << "POINTS " << s.points.size() << "\n";
		for (const Vector3f &v : s.points)
		{
			const glm::vec4 q = m * glm::vec4(v.x, v.y, v.z, 1.f);
			o << "\t" << q.x << " " << q.y << " " << q.z << "\n";
		}
		o << "POLYGONS " << s.polys.size() << "\n";
		o.precision(6);
		const bool mir = levels[li].mirrored;
		for (const iIMDPoly &poly : s.polys)
		{
			static const size_t ordN[3] = { 0, 1, 2 }, ordM[3] = { 0, 2, 1 };   // gespiegelt: Umlaufsinn tauschen
			const size_t *ord = mir ? ordM : ordN;
			o << "\t200 3 " << poly.pindex[ord[0]] << " " << poly.pindex[ord[1]] << " " << poly.pindex[ord[2]];
			for (int k = 0; k < 3; ++k) { o << " " << poly.texCoord[ord[k]].x << " " << poly.texCoord[ord[k]].y; }
			o << "\n";
		}
		o.precision(3);
		if (li == 0)
		{
			std::vector<Mount> mounts = d.mounts;
			if (mounts.empty() && !d.parts.empty()) { mounts = defaultMounts(d.parts[0]); }
			if (mounts.size() > MAX_SLOTS) { mounts.resize(MAX_SLOTS); }
			connectorsOut.clear();
			if (!mounts.empty())
			{
				o << "CONNECTORS " << mounts.size() << "\n";
				for (const Mount &mm : mounts)
				{
					const Vector3i c(static_cast<int>(std::lround(mm.pos.x)), static_cast<int>(std::lround(mm.pos.z)), static_cast<int>(std::lround(mm.pos.y)));
					connectorsOut.push_back(c);
					o << "\t" << c.x << " " << c.y << " " << c.z << "\n";
				}
			}
		}
	}
	return o.str();
}

// ---------------------------------------------------------------- Werte
void applyStats(BODY_STATS &out, const Def &d, size_t slotIdx)
{
	const BODY_STATS *chassis = d.parts.empty() ? nullptr : bodyByIdMutable(d.parts[0].body);
	if (chassis == nullptr) { return; }
	const WzString oldId = out.id;
	const size_t keepIndex = out.index;
	const unsigned keepRef = out.ref;
	out = *chassis;                                   // Laufwerk-Modelle, Klasse, Sonderwerte vom Fahrgestell
	out.index = keepIndex;
	out.ref = keepRef;
	out.id = WzString::fromUtf8(d.id);
	out.name = WzString::fromUtf8(d.name);
	out.designable = !d.deleted;
	out.altIMDs.clear();
	out.variantGroup = WzString();
	out.variantWeight = 0;
	out.variantRoll = false;
	out.hasHeadlight = false;
	out.gaitStride = 0; out.gaitRunStride = 0; out.gaitBlends = 1;
	const Totals t = totals(d);
	out.base.hitpoints = t.hp;
	out.base.armour = t.armour;
	out.base.thermal = static_cast<int>(t.thermal);
	out.base.power = t.power;
	out.weight = t.weight;
	out.buildPower = t.cost;
	out.buildPoints = t.buildPoints;
	out.size = static_cast<BODY_SIZE>(std::clamp(t.size, 0, static_cast<int>(SIZE_SUPER_HEAVY)));
	out.weaponSlots = std::max(1u, std::min<unsigned>(t.slots, MAX_SLOTS));
	// Forschungs-Verbesserungen wie beim Fahrgestell (Verhaeltnis Ausbau/Grundwert)
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		const auto &cu = chassis->upgrade[p];
		const auto &cb = chassis->base;
		auto ratio = [](double up, double base) { return base > 0.0 ? up / base : 1.0; };
		out.upgrade[p] = out.base;
		out.upgrade[p].hitpoints = static_cast<unsigned>(std::lround(out.base.hitpoints * ratio(cu.hitpoints, cb.hitpoints)));
		out.upgrade[p].hitpointPct = cu.hitpointPct;
		out.upgrade[p].armour = static_cast<unsigned>(std::lround(out.base.armour * ratio(cu.armour, cb.armour)));
		out.upgrade[p].thermal = static_cast<int>(std::lround(out.base.thermal * ratio(cu.thermal, cb.thermal)));
		out.upgrade[p].power = static_cast<unsigned>(std::lround(out.base.power * ratio(cu.power, cb.power)));
		out.upgrade[p].resistance = cu.resistance;
	}
	// Modell
	std::vector<Vector3i> conns;
	const std::string pie = buildPie(d, conns);
	if (!pie.empty())
	{
		const WzString modelName = WzString::fromUtf8("compbody_" + slug(d.id) + ".pie");
		iIMDBaseShape *shape = modelRegisterFromText(modelName, pie);
		if (shape != nullptr) { out.pIMD = shape; }
	}
	int extent = 0;
	if (out.pIMD != nullptr) { extent = std::max({ std::abs(out.pIMD->min.x), std::abs(out.pIMD->max.x), std::abs(out.pIMD->min.z), std::abs(out.pIMD->max.z) }); }
	out.collisionRadius = std::clamp(extent * 3 / 4, 0, 512);
	if (s_lookup) { s_lookup(&out, oldId); }
	// Fahrgestell-Lage fuer die Laufwerke, Teile fuer die Verfuegbarkeit
	Slot &sl = s_slots[slotIdx];
	sl.defId = d.id;
	sl.prop = partMatrix(d.parts[0]);
	sl.propShift = (glm::length(d.parts[0].pos) > 1e-3f || glm::length(d.parts[0].rot) > 1e-3f || std::fabs(d.parts[0].scale - 1.f) > 1e-3f
	               || glm::length(d.parts[0].stretch - glm::vec3(1.f)) > 1e-3f);
	sl.lift = d.drives.empty() ? liftOf(d) : 0.f;   // (27.11.) Antriebsplaetze stehen da, wo sie gesetzt sind
	sl.driveM.clear(); sl.driveBody.clear();
	for (const Drive &dr : d.drives)
	{
		if (sl.driveM.size() >= MAX_DRIVES) { break; }
		const BODY_STATS *db = bodyByIdMutable(dr.body.empty() ? d.parts[0].body : dr.body);
		if (db == nullptr) { db = chassis; }
		sl.driveM.push_back(driveMatrix(dr));
		sl.driveBody.push_back(db->index);
	}
	sl.yaw.clear(); sl.pitch.clear(); sl.roll.clear();
	{
		std::vector<Mount> mounts = d.mounts;
		if (mounts.empty() && !d.parts.empty()) { mounts = defaultMounts(d.parts[0]); }
		for (const Mount &m : mounts) { sl.yaw.push_back(m.yaw); sl.pitch.push_back(m.pitch); sl.roll.push_back(m.roll); }
	}
	sl.partIdx.clear();
	for (const Part &p : d.parts)
	{
		const BODY_STATS *b = bodyByIdMutable(p.body);
		if (b != nullptr) { sl.partIdx.push_back(b->index); }
	}
}

void makeFree(BODY_STATS &out, size_t k, const BODY_STATS *tmpl)
{
	const WzString oldId = out.id;
	const size_t keepIndex = out.index;
	const unsigned keepRef = out.ref;
	if (tmpl != nullptr) { out = *tmpl; }
	out.index = keepIndex;
	out.ref = keepRef;
	char buf[48];
	snprintf(buf, sizeof(buf), "%s%02zu", FREE_PREFIX, k);
	out.id = WzString::fromUtf8(buf);
	out.name = WzString::fromUtf8("(frei)");
	out.designable = false;
	out.altIMDs.clear();
	out.variantGroup = WzString();
	if (s_lookup) { s_lookup(&out, oldId); }
}

} // namespace

// ---------------------------------------------------------------- Stats-Laden
size_t prepare()
{
	std::map<std::string, Def> all;
	readDir(PACK_DIR, true, all);
	readDir(USER_DIR, false, all);
	s_defs.clear();
	for (auto &kv : all) { s_defs.push_back(kv.second); }
	return s_defs.size() + FREE_SLOTS;
}

void registerAll(size_t first, size_t count, const std::function<void(COMPONENT_STATS *, const WzString &)> &lookup)
{
	s_lookup = lookup;
	s_first = first;
	s_slots.assign(count, Slot());
	const BODY_STATS *tmpl = nullptr;
	for (size_t i = 0; i < first && i < asBodyStats.size(); ++i)
	{
		if (asBodyStats[i].designable && isUsableChassisPart(&asBodyStats[i])) { tmpl = &asBodyStats[i]; break; }
	}
	size_t k = 0;
	for (size_t i = 0; i < count; ++i)
	{
		const size_t idx = first + i;
		if (idx >= asBodyStats.size()) { break; }
		BODY_STATS &out = asBodyStats[idx];
		out.index = idx;
		out.ref = STAT_BODY + static_cast<unsigned>(idx);
		out.compType = COMP_BODY;
		s_slots[i].bodyIdx = idx;
		bool filled = false;
		while (k < s_defs.size() && !filled)
		{
			const Def &d = s_defs[k++];
			if (d.parts.empty() || bodyByIdMutable(d.parts[0].body) == nullptr) { debug(LOG_ERROR, "Verbundrumpf %s: Fahrgestell %s fehlt", d.id.c_str(), d.parts.empty() ? "?" : d.parts[0].body.c_str()); continue; }
			applyStats(out, d, i);
			filled = true;
		}
		if (!filled) { makeFree(out, i, tmpl); }
	}
}

void shutdown()
{
	s_slots.clear();
	s_lookup = nullptr;
}

// ---------------------------------------------------------------- Laufzeit
bool isComposite(size_t bodyIdx)
{
	if (bodyIdx < s_first || bodyIdx >= s_first + s_slots.size()) { return false; }
	return !s_slots[bodyIdx - s_first].defId.empty();
}

size_t driveCount(size_t bodyIdx)
{
	if (!isComposite(bodyIdx)) { return 0; }
	return s_slots[bodyIdx - s_first].driveM.size();
}

bool driveInfo(size_t bodyIdx, size_t i, glm::mat4 &m, size_t &sizeBodyIdx)
{
	if (!isComposite(bodyIdx)) { return false; }
	const Slot &sl = s_slots[bodyIdx - s_first];
	if (i >= sl.driveM.size() || i >= sl.driveBody.size() || sl.driveBody[i] >= asBodyStats.size()) { return false; }
	m = sl.driveM[i];
	sizeBodyIdx = sl.driveBody[i];
	return true;
}

glm::mat4 driveMatrix(const Drive &dr)
{
	return glm::translate(glm::mat4(1.f), dr.pos) * glm::rotate(glm::mat4(1.f), glm::radians(-dr.yaw), glm::vec3(0.f, 1.f, 0.f))
	     * glm::scale(glm::mat4(1.f), glm::vec3(std::clamp(dr.scale, 0.25f, 4.f)));
}

bool centerDef(Def &d)
{
	float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
	for (const Part &p : d.parts)
	{
		const BodyInfo *b = bodyInfo(p.body);
		const iIMDShape *m = (b != nullptr && b->model != nullptr) ? b->model->displayModel() : nullptr;
		if (m == nullptr) { continue; }
		const glm::mat4 pm = partMatrix(p);
		for (int c = 0; c < 8; ++c)
		{
			const glm::vec4 v = pm * glm::vec4((c & 1) ? m->max.x : m->min.x, (c & 2) ? m->max.y : m->min.y, (c & 4) ? m->max.z : m->min.z, 1.f);
			x0 = std::min(x0, v.x); x1 = std::max(x1, v.x); z0 = std::min(z0, v.z); z1 = std::max(z1, v.z);
		}
	}
	if (x1 < x0 || z1 < z0) { return false; }
	const float cx = std::round((x0 + x1) * 0.5f * 2.f) / 2.f, cz = std::round((z0 + z1) * 0.5f * 2.f) / 2.f;   // halbe Einheiten
	if (std::fabs(cx) < 0.5f && std::fabs(cz) < 0.5f) { return false; }
	const glm::vec3 sh(cx, 0.f, cz);
	for (Part &p : d.parts) { p.pos -= sh; }
	for (Mount &m : d.mounts) { m.pos -= sh; }
	for (Drive &dr : d.drives) { dr.pos -= sh; }
	return true;
}

float propLift(size_t bodyIdx)
{
	if (!isComposite(bodyIdx)) { return 0.f; }
	return s_slots[bodyIdx - s_first].lift;
}

float liftOf(const Def &d)
{
	// (Wunsch 24.11. 01:23 "alle Antriebe ... nicht sichtbar"): liegen Teile tiefer als das Fahrgestell (Teil 0), steckte das Laufwerk
	// unsichtbar im Rumpf (z.B. "Vulture": Tiger oben als Fahrgestell, Python und Deck darunter). Der Rumpf steht dann so weit hoeher,
	// dass das unterste Teil so auf dem Laufwerk sitzt wie sonst das Fahrgestell.
	if (d.parts.empty()) { return 0.f; }
	float lowest = 1e9f, chassisLow = 1e9f;
	for (size_t i = 0; i < d.parts.size(); ++i)
	{
		const BodyInfo *b = bodyInfo(d.parts[i].body);
		const iIMDShape *m = (b != nullptr && b->model != nullptr) ? b->model->displayModel() : nullptr;
		if (m == nullptr) { continue; }
		const glm::mat4 pm = partMatrix(d.parts[i]);
		float lo = 1e9f;
		for (int c = 0; c < 8; ++c)
		{
			const glm::vec4 v((c & 1) ? m->max.x : m->min.x, (c & 2) ? m->max.y : m->min.y, (c & 4) ? m->max.z : m->min.z, 1.f);
			lo = std::min(lo, (pm * v).y);
		}
		lowest = std::min(lowest, lo);
		if (i == 0) { chassisLow = lo; }
	}
	if (lowest > 1e8f || chassisLow > 1e8f) { return 0.f; }
	return std::max(0.f, chassisLow - lowest);
}

const glm::mat4 *propOffset(size_t bodyIdx)
{
	if (!isComposite(bodyIdx)) { return nullptr; }
	const Slot &sl = s_slots[bodyIdx - s_first];
	return sl.propShift ? &sl.prop : nullptr;
}

bool mountYaw(size_t bodyIdx, int connector, float &yawDeg)
{
	if (!isComposite(bodyIdx)) { return false; }
	const Slot &sl = s_slots[bodyIdx - s_first];
	yawDeg = (connector >= 0 && static_cast<size_t>(connector) < sl.yaw.size()) ? sl.yaw[static_cast<size_t>(connector)] : 0.f;
	return true;
}

bool mountTilt(size_t bodyIdx, int connector, float &yawDeg, float &pitchDeg, float &rollDeg)
{
	if (!isComposite(bodyIdx) || connector < 0) { return false; }
	const Slot &sl = s_slots[bodyIdx - s_first];
	const size_t c = static_cast<size_t>(connector);
	if (c >= sl.pitch.size() || c >= sl.roll.size() || c >= sl.yaw.size()) { return false; }
	yawDeg = sl.yaw[c]; pitchDeg = sl.pitch[c]; rollDeg = sl.roll[c];
	return std::fabs(pitchDeg) > 1e-3f || std::fabs(rollDeg) > 1e-3f;
}

glm::mat4 tiltMatrix(float yawDeg, float pitchDeg, float rollDeg)
{
	return glm::rotate(glm::mat4(1.f), glm::radians(-yawDeg), glm::vec3(0.f, 1.f, 0.f))
	     * glm::rotate(glm::mat4(1.f), glm::radians(pitchDeg), glm::vec3(1.f, 0.f, 0.f))
	     * glm::rotate(glm::mat4(1.f), glm::radians(rollDeg), glm::vec3(0.f, 0.f, 1.f))
	     * glm::rotate(glm::mat4(1.f), glm::radians(yawDeg), glm::vec3(0.f, 1.f, 0.f));
}

bool tiltedAim(size_t bodyIdx, int connector, uint16_t &dir, uint16_t &pitch)
{
	float y = 0.f, p = 0.f, r = 0.f;
	if (!mountTilt(bodyIdx, connector, y, p, r)) { return false; }
	constexpr float K = 6.28318530718f / 65536.f;
	const float d = static_cast<float>(dir) * K, q = static_cast<float>(static_cast<int16_t>(pitch)) * K;
	// Rohr-Richtung im Rumpf (Zeichnen: Schwenk R_y(-d), Hoehe R_x(+q), Rohr nach -z) in die Achsen des Platzes drehen
	const glm::vec3 A(std::sin(d) * std::cos(q), std::sin(q), -std::cos(d) * std::cos(q));
	const glm::vec3 L = glm::transpose(glm::mat3(tiltMatrix(y, p, r))) * A;
	const float q2 = std::asin(std::clamp(L.y, -1.f, 1.f)), d2 = std::atan2(L.x, -L.z);
	dir = static_cast<uint16_t>(static_cast<int>(std::lround(d2 / K)) & 0xFFFF);
	pitch = static_cast<uint16_t>(static_cast<int>(std::lround(q2 / K)) & 0xFFFF);
	return true;
}

bool weaponArc(const BASE_OBJECT *psObj, int weaponSlot, uint16_t &yaw)
{
	if (psObj == nullptr || psObj->type != OBJ_DROID) { return false; }
	const DROID *d = static_cast<const DROID *>(psObj);
	if (weaponSlot < 0 || weaponSlot >= static_cast<int>(MAX_WEAPONS) || d->isVtol()) { return false; }
	float deg = 0.f;
	if (!mountYaw(d->asBits[COMP_BODY], d->modSlots.conn(weaponSlot), deg)) { return false; }
	yaw = static_cast<uint16_t>(static_cast<int>(std::lround(deg * 65536.f / 360.f)) & 0xFFFF);
	return true;
}

void refreshAvailability(unsigned player)
{
	if (player >= MAX_PLAYERS || apCompLists[player][COMP_BODY] == nullptr) { return; }
	const bool network = NetPlay.bComms;
	for (const Slot &sl : s_slots)
	{
		if (sl.bodyIdx >= asBodyStats.size()) { continue; }
		UBYTE &state = apCompLists[player][COMP_BODY][sl.bodyIdx];
		if (sl.defId.empty() || network || !asBodyStats[sl.bodyIdx].designable) { state = UNAVAILABLE; continue; }
		bool all = true;
		for (size_t pi : sl.partIdx)
		{
			// (17:55 "sofern man die entsprechenden Forschungen dafuer hat"): erforschbare Teile muessen erforscht sein; Rumpfe, die es nie
			// im Designer gibt (Scavenger-, Mod-Rumpfe), halten nichts auf
			if (pi >= asBodyStats.size()) { all = false; break; }
			const bool needsResearch = asBodyStats[pi].designable || isWzFactionBody(asBodyStats[pi].id.toStdString());   // (23.11. 23:24)
			if (needsResearch && apCompLists[player][COMP_BODY][pi] != AVAILABLE) { all = false; break; }
		}
		state = all ? AVAILABLE : UNAVAILABLE;
	}
}

// ---------------------------------------------------------------- Katalog
bool isWzFactionBody(const std::string &id)
{
	for (const char *b : WZ_FACTION_BODIES) { if (id == b) { return true; } }
	return false;
}

const std::vector<BodyInfo> &catalog() { return cat().all; }

const BodyInfo *bodyInfo(const std::string &id)
{
	const Catalog &c = cat();
	auto it = c.byId.find(id);
	return it != c.byId.end() ? &c.all[it->second] : nullptr;
}

bool inGame() { return !asBodyStats.empty(); }

void refreshDefs()
{
	std::map<std::string, Def> all;
	readDir(PACK_DIR, true, all);
	readDir(USER_DIR, false, all);
	if (!inGame() || s_slots.empty())
	{
		s_defs.clear();
		for (auto &kv : all) { s_defs.push_back(kv.second); }
		return;
	}
	// im Spiel: angemeldete Bauplaene bleiben, wie sie sind (Plaetze); neu hinzugekommene kommen dazu (bauen erst nach dem Speichern hier)
	for (auto &kv : all)
	{
		if (find(kv.first) == nullptr) { s_defs.push_back(kv.second); }
	}
}

std::string previewModel(const Def &d)
{
	std::vector<Vector3i> conns;
	const std::string pie = buildPie(d, conns);
	if (pie.empty()) { return std::string(); }
	const std::string name = "compbody_" + slug(d.id) + ".pie";
	return modelRegisterFromText(WzString::fromUtf8(name), pie) != nullptr ? name : std::string();
}

const char *sizeText(int size)
{
	switch (size)
	{
	case SIZE_MEDIUM: return "MEDIUM";
	case SIZE_HEAVY: return "HEAVY";
	case SIZE_SUPER_HEAVY: return "SUPER HEAVY";
	default: return "LIGHT";
	}
}

// ---------------------------------------------------------------- Rumpf-Baukasten
const std::vector<Def> &defs() { return s_defs; }

const Def *find(const std::string &id)
{
	for (const Def &d : s_defs) { if (d.id == id) { return &d; } }
	return nullptr;
}

glm::mat4 partMatrix(const Part &p, bool withFlip)
{
	glm::mat4 m = glm::translate(glm::mat4(1.f), p.pos);
	m = glm::rotate(m, glm::radians(p.rot.x), glm::vec3(0.f, 1.f, 0.f));
	m = glm::rotate(m, glm::radians(p.rot.y), glm::vec3(1.f, 0.f, 0.f));
	m = glm::rotate(m, glm::radians(p.rot.z), glm::vec3(0.f, 0.f, 1.f));
	glm::vec3 sc = p.scale * p.stretch;   // (24.11.) Form je Achse
	if (withFlip)
	{
		if (p.flip & 1) { sc.x = -sc.x; }
		if (p.flip & 2) { sc.y = -sc.y; }
		if (p.flip & 4) { sc.z = -sc.z; }
	}
	return glm::scale(m, sc);
}

const iIMDShape *bakedShape(const std::string &bodyId, int flipMask, const std::string &texFrom)
{
	const bool tex = !texFrom.empty() && texFrom != bodyId;
	if ((flipMask & 7) == 0 && !tex) { return nullptr; }
	static std::map<std::string, iIMDBaseShape *> cache;
	static unsigned gen = 0;
	if (modelCacheStale(gen)) { cache.clear(); }
	const std::string name = "compflip_" + slug(bodyId) + "_" + std::to_string(flipMask & 7) + (tex ? "_" + slug(texFrom) : std::string()) + ".pie";
	auto it = cache.find(name);
	if (it != cache.end()) { return it->second != nullptr ? it->second->displayModel() : nullptr; }
	cache[name] = nullptr;
	Def d;
	d.id = "flip";
	Part p;
	p.body = bodyId;
	p.flip = flipMask & 7;
	if (tex) { p.texFrom = texFrom; }
	d.parts.push_back(p);
	Mount none;
	d.mounts.push_back(none);
	std::vector<Vector3i> conns;
	const std::string pie = buildPie(d, conns);
	if (pie.empty()) { return nullptr; }
	iIMDBaseShape *shape = modelRegisterFromText(WzString::fromUtf8(name), pie);
	cache[name] = shape;
	return shape != nullptr ? shape->displayModel() : nullptr;
}

Totals totals(const Def &d)
{
	Totals t;
	double hp = 0, arm = 0, therm = 0, cost = 0, costMax = 0, pts = 0, ptsMax = 0;
	int maxSize = 0, heavy = 0;
	for (const Part &p : d.parts)
	{
		const BodyInfo *b = bodyInfo(p.body);
		if (b == nullptr) { continue; }
		const double h = b->hp;
		hp += h; arm += h * b->armour; therm += h * b->thermal;
		t.weight += b->weight;
		t.power += b->power;
		cost += b->cost; costMax = std::max(costMax, static_cast<double>(b->cost));
		pts += b->buildPoints; ptsMax = std::max(ptsMax, static_cast<double>(b->buildPoints));
		maxSize = std::max(maxSize, static_cast<int>(b->size));
		if (b->size >= SIZE_HEAVY) { ++heavy; }
	}
	t.hp = static_cast<unsigned>(std::lround(hp));
	t.armour = hp > 0 ? static_cast<unsigned>(std::lround(arm / hp)) : 0;
	t.thermal = hp > 0 ? static_cast<unsigned>(std::lround(therm / hp)) : 0;
	// teuerstes Teil voll, jedes weitere zur Haelfte (Wunsch: "addiert, aber noch bezahlbar")
	t.cost = static_cast<unsigned>(std::lround(costMax + 0.5 * (cost - costMax)));
	t.buildPoints = static_cast<unsigned>(std::lround(ptsMax + 0.5 * (pts - ptsMax)));
	t.size = std::min(static_cast<int>(SIZE_SUPER_HEAVY), maxSize + (d.parts.size() >= 3 || heavy >= 2 ? 1 : 0));
	t.slots = static_cast<unsigned>(d.mounts.empty() && !d.parts.empty() ? defaultMounts(d.parts[0]).size() : d.mounts.size());
	return t;
}

std::vector<Mount> defaultMounts(const Part &chassis)
{
	std::vector<Mount> out;
	const BodyInfo *b = bodyInfo(chassis.body);
	if (b == nullptr || b->model == nullptr) { return out; }
	const glm::mat4 m = partMatrix(chassis);
	const size_t n = std::min<size_t>(std::max<unsigned>(1u, b->weaponSlots), b->model->connectors.size());
	for (size_t i = 0; i < n; ++i)
	{
		const Vector3i &c = b->model->connectors[i];   // x, Tiefe, Hoehe
		const glm::vec4 q = m * glm::vec4(static_cast<float>(c.x), static_cast<float>(c.z), static_cast<float>(c.y), 1.f);
		Mount mm; mm.pos = glm::vec3(q);
		out.push_back(mm);
	}
	return out;
}

bool save(Def &d, std::string &err)
{
	if (d.parts.empty()) { err = "Add at least one hull."; return false; }
	if (bodyInfo(d.parts[0].body) == nullptr) { err = "Unknown chassis."; return false; }
	if (d.name.empty()) { d.name = "Neuer Rumpf"; }
	if (d.mounts.size() > MAX_SLOTS) { d.mounts.resize(MAX_SLOTS); }
	if (d.drives.size() > MAX_DRIVES) { d.drives.resize(MAX_DRIVES); }
	centerDef(d);   // (Wunsch 27.11. 19:16) Grundriss mittig - passt dann im Einheiten-Designer und im Spiel
	if (d.id.empty())
	{
		const std::string base = "CompBody_" + slug(d.name);
		std::string id = base;
		for (int n = 2; find(id) != nullptr || bodyByIdMutable(id) != nullptr; ++n) { id = base + "_" + std::to_string(n); }
		d.id = id;
	}
	d.deleted = false;
	// Platz suchen: vorhandener mit derselben ID, sonst ein freier
	size_t slotIdx = s_slots.size();
	for (size_t i = 0; i < s_slots.size(); ++i) { if (s_slots[i].defId == d.id) { slotIdx = i; break; } }
	if (slotIdx == s_slots.size())
	{
		for (size_t i = 0; i < s_slots.size(); ++i) { if (s_slots[i].defId.empty()) { slotIdx = i; break; } }
	}
	const bool game = inGame() && !s_slots.empty();
	if (game && slotIdx == s_slots.size()) { err = "Saved - usable after the next game start (no free slot left in this game)."; }
	// Datei schreiben
	PHYSFS_mkdir("userdata");
	PHYSFS_mkdir(USER_DIR);
	const std::string path = std::string(USER_DIR) + "/" + slug(d.id) + ".json";
	if (!saveJSONToFile(toJson(d), path.c_str())) { err = "Could not write " + path; return false; }
	d.preset = false;
	bool known = false;
	for (Def &x : s_defs) { if (x.id == d.id) { x = d; known = true; break; } }
	if (!known) { s_defs.push_back(d); }
	if (!game) { return true; }   // Hauptmenue: Datei genuegt - das naechste Spiel meldet den Rumpf an
	if (slotIdx < s_slots.size())
	{
		const size_t idx = s_slots[slotIdx].bodyIdx;
		if (idx < asBodyStats.size()) { applyStats(asBodyStats[idx], d, slotIdx); }
		for (unsigned p = 0; p < MAX_PLAYERS; ++p) { refreshAvailability(p); }
	}
	return true;
}

bool remove(const std::string &id, std::string &err)
{
	Def *d = nullptr;
	for (Def &x : s_defs) { if (x.id == id) { d = &x; break; } }
	if (d == nullptr) { err = "Unknown hull."; return false; }
	d->deleted = true;
	// Datei behalten (geloescht markiert), damit alte Spielstaende mit diesem Rumpf weiter laden
	PHYSFS_mkdir("userdata");
	PHYSFS_mkdir(USER_DIR);
	const std::string path = std::string(USER_DIR) + "/" + slug(d->id) + ".json";
	if (!saveJSONToFile(toJson(*d), path.c_str())) { err = "Could not write " + path; return false; }
	for (Slot &sl : s_slots)
	{
		if (sl.defId == id && sl.bodyIdx < asBodyStats.size()) { asBodyStats[sl.bodyIdx].designable = false; }
	}
	for (unsigned p = 0; p < MAX_PLAYERS; ++p) { refreshAvailability(p); }
	return true;
}

}
