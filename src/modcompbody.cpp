// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 23.11. 01:35): Verbundrumpfe aus dem Rumpf-Baukasten (siehe modcompbody.h)
#include <nlohmann/json.hpp> // Must come before WZ includes

#include "modcompbody.h"

#include "lib/framework/frame.h"
#include "lib/framework/file.h"
#include "lib/framework/physfs_ext.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
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
		parts.push_back(e);
	}
	j["parts"] = parts;
	nlohmann::json mounts = nlohmann::json::array();
	for (const Mount &m : d.mounts)
	{
		nlohmann::json e = vec3(m.pos);
		if (std::fabs(m.yaw) > 1e-3f) { e.push_back(m.yaw); }   // [x, y, z, Blickrichtung]
		mounts.push_back(e);
	}
	j["mounts"] = mounts;
	if (d.deleted) { j["deleted"] = true; }
	return j;
}

bool fromJson(const nlohmann::json &j, Def &d)
{
	if (!j.is_object() || !j.contains("parts") || !j["parts"].is_array()) { return false; }
	d.id = j.value("id", std::string());
	d.name = j.value("name", d.id);
	d.deleted = j.value("deleted", false);
	d.parts.clear(); d.mounts.clear();
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
		d.parts.push_back(p);
	}
	if (j.contains("mounts") && j["mounts"].is_array())
	{
		for (const auto &m : j["mounts"])
		{
			Mount mm; mm.pos = vec3(m);
			if (m.is_array() && m.size() >= 4 && m[3].is_number()) { mm.yaw = std::remainder(m[3].get<float>(), 360.f); }
			d.mounts.push_back(mm);
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
	if (id.empty() || startsWith(id, "ZNULL") || startsWith(id, "CompBody") || startsWith(id, "B1") || startsWith(id, "MFT_") || id == "MFCrewBody") { return false; }
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
	if (startsWith(id, "UEF")) { b.source = "Supreme Commander"; b.sourceRank = 10; b.group = "UEF transports"; return; }
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

// ---------------------------------------------------------------- Modell (PIE 4, eine Ebene je Teil-Ebene, eigene Texturen)
void texLines(std::ostringstream &o, const iIMDShape &lvl)
{
	for (size_t t = 0; t < lvl.tilesetTextureFiles.size(); ++t)
	{
		const TilesetTextureFiles &f = lvl.tilesetTextureFiles[t];
		if (!f.texfile.empty()) { o << "TEXTURE " << t << " " << f.texfile << "\n"; }
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
	struct Lv { const iIMDShape *s; glm::mat4 m; bool mirrored; };
	std::vector<Lv> levels;
	for (const Part &p : d.parts)
	{
		const BodyInfo *b = bodyInfo(p.body);
		if (b == nullptr || b->model == nullptr || b->model->displayModel() == nullptr) { continue; }
		const glm::mat4 m = partMatrix(p);
		const bool mirrored = glm::determinant(glm::mat3(m)) < 0.f;
		for (const iIMDShape *lvl = b->model->displayModel(); lvl != nullptr; lvl = lvl->next.get())
		{
			if (lvl->points.empty() || lvl->polys.empty()) { continue; }
			if (lvl->tilesetTextureFiles[0].texfile.empty()) { continue; }
			levels.push_back({ lvl, m, mirrored });
		}
	}
	std::ostringstream o;
	o.setf(std::ios::fixed); o.precision(3);
	if (levels.empty()) { return std::string(); }
	o << "PIE 4\n";
	o << "TYPE " << std::hex << levels[0].s->flags << std::dec << "\n";
	texLines(o, *levels[0].s);
	o << "LEVELS " << levels.size() << "\n";
	for (size_t li = 0; li < levels.size(); ++li)
	{
		const iIMDShape &s = *levels[li].s;
		const glm::mat4 &m = levels[li].m;
		o << "LEVEL " << (li + 1) << "\n";
		o << "TYPE " << std::hex << s.flags << std::dec << "\n";
		texLines(o, s);
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
	sl.lift = liftOf(d);
	sl.yaw.clear();
	{
		std::vector<Mount> mounts = d.mounts;
		if (mounts.empty() && !d.parts.empty()) { mounts = defaultMounts(d.parts[0]); }
		for (const Mount &m : mounts) { sl.yaw.push_back(m.yaw); }
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

const iIMDShape *flippedShape(const std::string &bodyId, int flipMask)
{
	if ((flipMask & 7) == 0) { return nullptr; }
	static std::map<std::string, iIMDBaseShape *> cache;
	static unsigned gen = 0;
	if (modelCacheStale(gen)) { cache.clear(); }
	const std::string name = "compflip_" + slug(bodyId) + "_" + std::to_string(flipMask & 7) + ".pie";
	auto it = cache.find(name);
	if (it != cache.end()) { return it->second != nullptr ? it->second->displayModel() : nullptr; }
	cache[name] = nullptr;
	Def d;
	d.id = "flip";
	Part p;
	p.body = bodyId;
	p.flip = flipMask & 7;
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
