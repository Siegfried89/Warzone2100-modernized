// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 23.11. 01:35): Verbundrumpfe aus dem Rumpf-Baukasten (siehe modcompbody.h)
#include <nlohmann/json.hpp> // Must come before WZ includes

#include "modcompbody.h"

#include "lib/framework/frame.h"
#include "lib/framework/file.h"
#include "lib/framework/physfs_ext.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/pietypes.h"
#include "lib/netplay/netplay.h"

#include "game.h"
#include "stats.h"
#include "droid.h"
#include "statsdef.h"
#include "basedef.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <functional>
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
	bool anim = false;                               // (27.11.) Animation (Bewegungs-Bausteine/Posen)
	Def animDef;                                     // Teile samt Animation (nur wenn anim)
	std::vector<int> levelPart;                      // Teil je Modell-Ebene
	std::vector<int> mountPart;                      // Teil je Anschluss (-1 = am Rumpf)
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
		if (p.cut != 0) { e["cut"] = { p.cut, p.cutAngle, p.cutPos, p.cutFlip ? 1 : 0 }; }   // (27.11.) Schnitt
		if (p.group != 0) { e["group"] = p.group; }                                          // (27.11.) Verbund
		// (27.11.) Animation
		if (p.parent >= 0) { e["parent"] = p.parent; }
		if (glm::length(p.pivot) > 1e-4f) { e["pivot"] = vec3(p.pivot); }
		if (p.motion.type != MOT_NONE)
		{
			e["motion"] = { { "type", p.motion.type }, { "axis", p.motion.axis }, { "when", p.motion.when }, { "amount", p.motion.amount },
			                { "speed", p.motion.speed }, { "phase", p.motion.phase } };
		}
		bool anyKey = false;
		for (int c = 0; c < CLIP_COUNT; ++c) { anyKey = anyKey || !p.keys[c].empty(); }
		if (anyKey)
		{
			nlohmann::json ks = nlohmann::json::array();
			for (int c = 0; c < CLIP_COUNT; ++c)
			{
				nlohmann::json kc = nlohmann::json::array();
				for (const PoseKey &k : p.keys[c]) { kc.push_back({ { "r", vec3(k.rot) }, { "p", vec3(k.pos) } }); }
				ks.push_back(kc);
			}
			e["keys"] = ks;
		}
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
	{
		bool anyPart = false;
		for (const Mount &m : d.mounts) { anyPart = anyPart || m.part >= 0; }
		if (anyPart)   // (27.11.) Teil je Waffenplatz (Animation)
		{
			nlohmann::json mp = nlohmann::json::array();
			for (const Mount &m : d.mounts) { mp.push_back(m.part); }
			j["mountParts"] = mp;
		}
	}
	if (d.poseCount[0] > 0 || d.poseCount[1] > 0 || d.poseCount[2] > 0)   // (27.11.) Posen
	{
		j["poses"] = { d.poseCount[0], d.poseCount[1], d.poseCount[2] };
		j["poseSeconds"] = { d.poseSeconds[0], d.poseSeconds[1], d.poseSeconds[2] };
	}
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
		if (e.contains("cut") && e["cut"].is_array() && e["cut"].size() >= 4)   // (27.11.) Schnitt
		{
			const auto &c = e["cut"];
			if (c[0].is_number_integer()) { p.cut = std::clamp(c[0].get<int>(), 0, 3); }
			if (c[1].is_number()) { p.cutAngle = std::remainder(c[1].get<float>(), 360.f); }
			if (c[2].is_number()) { p.cutPos = std::clamp(c[2].get<float>(), -1000.f, 1000.f); }
			if (c[3].is_number()) { p.cutFlip = c[3].get<float>() != 0.f; }
		}
		p.group = std::max(0, e.value("group", 0));
		// (27.11.) Animation
		p.parent = e.value("parent", -1);
		if (e.contains("pivot")) { p.pivot = vec3(e["pivot"]); }
		if (e.contains("motion") && e["motion"].is_object())
		{
			const auto &m = e["motion"];
			p.motion.type = std::clamp(m.value("type", 0), 0, MOT_COUNT - 1);
			p.motion.axis = std::clamp(m.value("axis", 0), 0, 2);
			p.motion.when = std::clamp(m.value("when", 1), 0, WHEN_COUNT - 1);
			p.motion.amount = std::clamp(m.value("amount", 0.f), -360.f, 360.f);
			p.motion.speed = std::clamp(m.value("speed", 1.f), -20.f, 20.f);
			p.motion.phase = m.value("phase", 0.f);
		}
		if (e.contains("keys") && e["keys"].is_array())
		{
			for (size_t c = 0; c < e["keys"].size() && c < static_cast<size_t>(CLIP_COUNT); ++c)
			{
				const auto &kc = e["keys"][c];
				if (!kc.is_array()) { continue; }
				for (const auto &k : kc)
				{
					if (p.keys[c].size() >= static_cast<size_t>(MAX_POSES)) { break; }
					PoseKey pk;
					if (k.is_object() && k.contains("r")) { pk.rot = vec3(k["r"]); }
					if (k.is_object() && k.contains("p")) { pk.pos = vec3(k["p"]); }
					p.keys[c].push_back(pk);
				}
			}
		}
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
	if (j.contains("mountParts") && j["mountParts"].is_array())   // (27.11.)
	{
		for (size_t k = 0; k < j["mountParts"].size() && k < d.mounts.size(); ++k)
		{
			if (j["mountParts"][k].is_number_integer()) { d.mounts[k].part = j["mountParts"][k].get<int>(); }
		}
	}
	if (j.contains("poses") && j["poses"].is_array())   // (27.11.) Posen
	{
		for (size_t c = 0; c < j["poses"].size() && c < static_cast<size_t>(CLIP_COUNT); ++c)
		{
			if (j["poses"][c].is_number_integer()) { d.poseCount[c] = std::clamp(j["poses"][c].get<int>(), 0, MAX_POSES); }
		}
		if (j.contains("poseSeconds") && j["poseSeconds"].is_array())
		{
			for (size_t c = 0; c < j["poseSeconds"].size() && c < static_cast<size_t>(CLIP_COUNT); ++c)
			{
				if (j["poseSeconds"][c].is_number()) { d.poseSeconds[c] = std::clamp(j["poseSeconds"][c].get<float>(), 0.1f, 30.f); }
			}
		}
	}
	for (Part &p : d.parts)   // Kreise/ungueltige Eltern verhindern
	{
		if (p.parent < 0 || p.parent >= static_cast<int>(d.parts.size())) { p.parent = -1; }
	}
	for (size_t i = 0; i < d.parts.size(); ++i)
	{
		int q = d.parts[i].parent, guard = 0;
		while (q >= 0 && guard++ < 64) { if (q == static_cast<int>(i)) { d.parts[i].parent = -1; break; } q = d.parts[static_cast<size_t>(q)].parent; }
		if (guard >= 64) { d.parts[i].parent = -1; }
	}
	for (Mount &m : d.mounts) { if (m.part >= static_cast<int>(d.parts.size())) { m.part = -1; } }
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
	// (z.B. SC_UEL0401 = Fatboy): 2. Buchstabe Fraktion (E UEF, A Aeon, R Cybran, X Kampagne), 3. Buchstabe Art (L Land, A Luft, S See,
	// B Gebaeude, C zivil)
	if (startsWith(id, "SC_") && id.size() >= 6)
	{
		static const char *const FAC[4] = { "UEF", "Aeon", "Cybran", "Campaign" };
		static const char *const KIND[3] = { "Land", "Air", "Naval" };
		const char f = id[4], t = id[5];
		const int fi = f == 'E' ? 0 : (f == 'A' ? 1 : (f == 'R' ? 2 : 3));
		b.source = "Supreme Commander"; b.sourceRank = 10;
		if (t == 'B' || t == 'C')   // (Wunsch 27.11. 20:38) Gebaeude (B) und zivile Bauten (C) - im Rumpf-Baukasten unter "Buildings"
		{
			b.group = std::string(FAC[fi]) + (t == 'B' ? " - Buildings" : " - Civilian"); b.groupRank = 20 + fi * 2 + (t == 'C' ? 1 : 0);
			return;
		}
		const int ti = t == 'A' ? 1 : (t == 'S' ? 2 : 0);
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
// verschiedenen Stellen (jeder Rumpf hat seine eigene UV-Aufteilung).
// (Wunsch 27.11. 23:58 "die Texturzuweisung funktioniert noch nicht so richtig, da wahrscheinlich das UV immer von Rumpf zu Rumpf abweicht")
// Statt Umfaerben jetzt UV-Uebertragung: jede Flaeche des Teils sucht auf dem anderen Rumpf die Flaeche an derselben Stelle mit derselben
// Blickrichtung (beide Modelle auf ihren Umriss normiert - oben auf oben, Seite auf Seite, vorn auf vorn) und nimmt deren Texturstelle;
// die Ecken landen auf derselben zusammenhaengenden Texturinsel. Das Teil zeigt so das Muster, die Platten und die Teamfarbe des anderen
// Rumpfes (dessen Seite samt Teamfarben-, Normalen- und Glanzkarte), in seiner eigenen Form.
namespace
{
glm::vec3 triNormal(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c)
{
	const glm::vec3 n = glm::cross(b - a, c - a);
	const float l = glm::length(n);
	return l > 1e-9f ? n / l : glm::vec3(0.f, 1.f, 0.f);
}

/// naechster Punkt auf dem Dreieck abc zu p, mit Schwerpunktkoordinaten (Ericson, Real-Time Collision Detection 5.1.5)
glm::vec3 closestOnTri(const glm::vec3 &p, const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, glm::vec3 &w)
{
	const glm::vec3 ab = b - a, ac = c - a, ap = p - a;
	const float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
	if (d1 <= 0.f && d2 <= 0.f) { w = glm::vec3(1, 0, 0); return a; }
	const glm::vec3 bp = p - b;
	const float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
	if (d3 >= 0.f && d4 <= d3) { w = glm::vec3(0, 1, 0); return b; }
	const float vc = d1 * d4 - d3 * d2;
	if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f) { const float v = d1 / (d1 - d3); w = glm::vec3(1.f - v, v, 0.f); return a + v * ab; }
	const glm::vec3 cp = p - c;
	const float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
	if (d6 >= 0.f && d5 <= d6) { w = glm::vec3(0, 0, 1); return c; }
	const float vb = d5 * d2 - d1 * d6;
	if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f) { const float t = d2 / (d2 - d6); w = glm::vec3(1.f - t, 0.f, t); return a + t * ac; }
	const float va = d3 * d6 - d5 * d4;
	if (va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f)
	{
		const float t = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		w = glm::vec3(0.f, 1.f - t, t);
		return b + t * (c - b);
	}
	const float den = 1.f / (va + vb + vc);
	const float v = vb * den, t = vc * den;
	w = glm::vec3(1.f - v - t, v, t);
	return a + ab * v + ac * t;
}

/// Texturstellen (UV) der Dreiecke I (Punkte P, Modellraum des Teils) vom anderen Rumpf src uebernehmen
void transferUV(const std::vector<glm::vec3> &P, const std::vector<std::array<uint32_t, 3>> &I, std::vector<std::array<Vector2f, 3>> &UV, const iIMDShape &src)
{
	if (P.empty() || I.empty() || src.points.empty() || src.polys.empty()) { return; }
	auto bounds = [](const auto &pts, glm::vec3 &lo, glm::vec3 &hi) {
		lo = glm::vec3(1e9f); hi = glm::vec3(-1e9f);
		for (const auto &v : pts) { const glm::vec3 q(v.x, v.y, v.z); lo = glm::min(lo, q); hi = glm::max(hi, q); }
	};
	glm::vec3 tlo, thi, slo, shi;
	bounds(P, tlo, thi);
	bounds(src.points, slo, shi);
	const glm::vec3 tsz = glm::max(thi - tlo, glm::vec3(1e-3f)), ssz = glm::max(shi - slo, glm::vec3(1e-3f));
	struct STri { glm::vec3 p[3]; glm::vec3 n; Vector2f uv[3]; uint32_t idx[3]; };
	std::vector<STri> S;
	for (const iIMDPoly &poly : src.polys)
	{
		STri t;
		bool ok = true;
		for (int k = 0; k < 3; ++k)
		{
			if (poly.pindex[k] >= src.points.size()) { ok = false; break; }
			const Vector3f &v = src.points[poly.pindex[k]];
			t.p[k] = (glm::vec3(v.x, v.y, v.z) - slo) / ssz;
			t.uv[k] = poly.texCoord[k];
			t.idx[k] = poly.pindex[k];
		}
		if (!ok) { continue; }
		t.n = triNormal(t.p[0], t.p[1], t.p[2]);
		S.push_back(t);
	}
	if (S.empty()) { return; }
	// Texturinseln: Dreiecke mit gemeinsamer Kante, gleichen Texturstellen daran und aehnlicher Richtung
	std::vector<size_t> parent(S.size());
	for (size_t i = 0; i < S.size(); ++i) { parent[i] = i; }
	std::function<size_t(size_t)> find = [&](size_t x) { while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; } return x; };
	auto uvAt = [&S](size_t t, uint32_t vi, Vector2f &out) { for (int k = 0; k < 3; ++k) { if (S[t].idx[k] == vi) { out = S[t].uv[k]; return true; } } return false; };
	std::map<std::pair<uint32_t, uint32_t>, std::vector<size_t>> edges;
	for (size_t i = 0; i < S.size(); ++i)
	{
		for (int k = 0; k < 3; ++k)
		{
			const uint32_t a = S[i].idx[k], b = S[i].idx[(k + 1) % 3];
			edges[std::make_pair(std::min(a, b), std::max(a, b))].push_back(i);
		}
	}
	for (const auto &e : edges)
	{
		const std::vector<size_t> &l = e.second;
		for (size_t x = 0; x < l.size(); ++x)
		{
			for (size_t y = x + 1; y < l.size(); ++y)
			{
				Vector2f u1, u2, v1, v2;
				if (!uvAt(l[x], e.first.first, u1) || !uvAt(l[y], e.first.first, u2) || !uvAt(l[x], e.first.second, v1) || !uvAt(l[y], e.first.second, v2)) { continue; }
				const bool same = std::fabs(u1.x - u2.x) < 1e-4f && std::fabs(u1.y - u2.y) < 1e-4f && std::fabs(v1.x - v2.x) < 1e-4f && std::fabs(v1.y - v2.y) < 1e-4f;
				if (same && glm::dot(S[l[x]].n, S[l[y]].n) > 0.5f) { parent[find(l[x])] = find(l[y]); }
			}
		}
	}
	std::vector<size_t> island(S.size());
	for (size_t i = 0; i < S.size(); ++i) { island[i] = find(i); }
	for (size_t t = 0; t < I.size() && t < UV.size(); ++t)
	{
		glm::vec3 q[3];
		bool ok = true;
		for (int k = 0; k < 3; ++k)
		{
			if (I[t][k] >= P.size()) { ok = false; break; }
			q[k] = (P[I[t][k]] - tlo) / tsz;
		}
		if (!ok) { continue; }
		const glm::vec3 nt = triNormal(q[0], q[1], q[2]), cen = (q[0] + q[1] + q[2]) / 3.f;
		// passende Flaeche: nah an der Mitte, gleiche Blickrichtung
		size_t best = SIZE_MAX;
		float bestScore = 1e9f;
		for (size_t i = 0; i < S.size(); ++i)
		{
			const float dn = glm::dot(nt, S[i].n);
			if (dn < 0.2f) { continue; }
			glm::vec3 w;
			const float sc = glm::length(closestOnTri(cen, S[i].p[0], S[i].p[1], S[i].p[2], w) - cen) + 0.6f * (1.f - dn);
			if (sc < bestScore) { bestScore = sc; best = i; }
		}
		if (best == SIZE_MAX) { continue; }
		// Ecken auf dieselbe Insel
		for (int k = 0; k < 3; ++k)
		{
			float bd = 1e9f;
			Vector2f buv = UV[t][k];
			for (size_t i = 0; i < S.size(); ++i)
			{
				if (island[i] != island[best] || glm::dot(nt, S[i].n) <= 0.2f) { continue; }
				glm::vec3 w;
				const float d = glm::length(closestOnTri(q[k], S[i].p[0], S[i].p[1], S[i].p[2], w) - q[k]);
				if (d < bd)
				{
					bd = d;
					buv = Vector2f(w.x * S[i].uv[0].x + w.y * S[i].uv[1].x + w.z * S[i].uv[2].x, w.x * S[i].uv[0].y + w.y * S[i].uv[1].y + w.z * S[i].uv[2].y);
				}
			}
			UV[t][k] = buv;
		}
	}
}
} // namespace

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

/// (27.11.) Ebene an der Schnittebene kappen (Teil-Raum): Dreiecke auf der anderen Seite fallen weg, geteilte werden zugeschnitten,
/// die Schnittflaeche wird je Umriss als Faecher geschlossen (Umlaufsinn entgegen dem Rand der bleibenden Flaechen = nach aussen)
void clipLevel(std::vector<glm::vec3> &P, std::vector<std::array<uint32_t, 3>> &I, std::vector<std::array<Vector2f, 3>> &UV, const glm::vec3 &n, const glm::vec3 &c)
{
	std::vector<float> dist(P.size());
	for (size_t k = 0; k < P.size(); ++k) { dist[k] = glm::dot(n, P[k] - c); }
	std::map<std::pair<uint32_t, uint32_t>, uint32_t> edgePt;
	struct V { uint32_t i; Vector2f uv; bool cutPt; };
	std::vector<std::array<uint32_t, 3>> outI;
	std::vector<std::array<Vector2f, 3>> outUV;
	struct Seg { uint32_t a, b; Vector2f ua, ub; };
	std::vector<Seg> segs;
	auto cutPoint = [&](uint32_t a, uint32_t b) {
		const std::pair<uint32_t, uint32_t> key(std::min(a, b), std::max(a, b));
		const auto it = edgePt.find(key);
		if (it != edgePt.end()) { return it->second; }
		const float t = dist[key.first] / (dist[key.first] - dist[key.second]);
		P.push_back(glm::mix(P[key.first], P[key.second], t));
		dist.push_back(0.f);
		const uint32_t idx = static_cast<uint32_t>(P.size() - 1);
		edgePt[key] = idx;
		return idx;
	};
	for (size_t t = 0; t < I.size(); ++t)
	{
		const auto &tri = I[t];
		if (tri[0] >= dist.size() || tri[1] >= dist.size() || tri[2] >= dist.size()) { continue; }
		const bool in0 = dist[tri[0]] >= 0.f, in1 = dist[tri[1]] >= 0.f, in2 = dist[tri[2]] >= 0.f;
		if (in0 && in1 && in2) { outI.push_back(tri); outUV.push_back(UV[t]); continue; }
		if (!in0 && !in1 && !in2) { continue; }
		std::vector<V> poly;
		for (int k = 0; k < 3; ++k)
		{
			const uint32_t a = tri[k], b = tri[(k + 1) % 3];
			const Vector2f ua = UV[t][k], ub = UV[t][(k + 1) % 3];
			const bool ia = dist[a] >= 0.f, ib = dist[b] >= 0.f;
			if (ia) { poly.push_back({ a, ua, false }); }
			if (ia != ib)
			{
				const float u = dist[a] / (dist[a] - dist[b]);
				poly.push_back({ cutPoint(a, b), ua + (ub - ua) * u, true });
			}
		}
		for (size_t k = 1; k + 1 < poly.size(); ++k)
		{
			outI.push_back({ poly[0].i, poly[k].i, poly[k + 1].i });
			outUV.push_back({ poly[0].uv, poly[k].uv, poly[k + 1].uv });
		}
		// Rand auf der Ebene: vom Austritts- zum Eintrittspunkt (in Umlaufrichtung des bleibenden Stuecks)
		for (size_t k = 0; k < poly.size(); ++k)
		{
			const V &x = poly[k], &y = poly[(k + 1) % poly.size()];
			if (x.cutPt && y.cutPt) { segs.push_back({ x.i, y.i, x.uv, y.uv }); }
		}
	}
	// Deckel: Punkte nach Lage zusammenfassen (Naehte), Umrisse verketten, je Umriss ein Faecher um seine Mitte
	std::map<std::array<int64_t, 3>, uint32_t> canon;
	auto canonOf = [&](uint32_t i) {
		const glm::vec3 &q = P[i];
		const std::array<int64_t, 3> k = { std::llround(q.x * 512.0), std::llround(q.y * 512.0), std::llround(q.z * 512.0) };
		const auto it = canon.find(k);
		if (it != canon.end()) { return it->second; }
		canon[k] = i;
		return i;
	};
	std::map<uint32_t, size_t> fromSeg;
	for (size_t k = 0; k < segs.size(); ++k) { segs[k].a = canonOf(segs[k].a); segs[k].b = canonOf(segs[k].b); if (segs[k].a != segs[k].b) { fromSeg[segs[k].a] = k; } }
	std::vector<bool> used(segs.size(), false);
	for (size_t s0 = 0; s0 < segs.size(); ++s0)
	{
		if (used[s0] || segs[s0].a == segs[s0].b) { continue; }
		std::vector<size_t> chain;
		size_t cur = s0;
		while (!used[cur] && chain.size() < 100000)
		{
			used[cur] = true;
			chain.push_back(cur);
			const auto it = fromSeg.find(segs[cur].b);
			if (it == fromSeg.end()) { break; }
			cur = it->second;
		}
		if (chain.size() < 2) { continue; }
		glm::vec3 mid(0.f);
		Vector2f muv(0.f, 0.f);
		for (size_t k : chain) { mid += P[segs[k].a]; muv = muv + segs[k].ua; }
		mid /= static_cast<float>(chain.size());
		muv = muv * (1.f / static_cast<float>(chain.size()));
		P.push_back(mid);
		const uint32_t m = static_cast<uint32_t>(P.size() - 1);
		for (size_t k : chain)
		{
			outI.push_back({ segs[k].b, segs[k].a, m });
			outUV.push_back({ segs[k].ub, segs[k].ua, muv });
		}
	}
	I.swap(outI);
	UV.swap(outUV);
}

std::string buildPie(const Def &d, std::vector<Vector3i> &connectorsOut, std::vector<int> *levelPartOut = nullptr)
{
	struct Lv { const iIMDShape *s; glm::mat4 m; bool mirrored; const iIMDShape *tex; int part; };   // tex: Ebene des anderen Rumpfes (Farben)
	std::vector<Lv> levels;
	for (size_t pi = 0; pi < d.parts.size(); ++pi)
	{
		const Part &p = d.parts[pi];
		const BodyInfo *b = bodyInfo(p.body);
		if (b == nullptr || b->model == nullptr || b->model->displayModel() == nullptr) { continue; }
		const glm::mat4 m = partMatrix(p);
		const bool mirrored = glm::determinant(glm::mat3(m)) < 0.f;
		// (27.11.) Farben eines anderen Rumpfes: dessen erste Ebene mit Textur (UV-Uebertragung, siehe transferUV)
		const BodyInfo *texSrc = (!p.texFrom.empty() && p.texFrom != p.body) ? bodyInfo(p.texFrom) : nullptr;
		const iIMDShape *texLvl = nullptr;
		if (texSrc != nullptr && texSrc->model != nullptr)
		{
			for (const iIMDShape *sl = texSrc->model->displayModel(); sl != nullptr; sl = sl->next.get())
			{
				if (!sl->points.empty() && !sl->polys.empty() && !sl->tilesetTextureFiles[0].texfile.empty()) { texLvl = sl; break; }
			}
		}
		for (const iIMDShape *lvl = b->model->displayModel(); lvl != nullptr; lvl = lvl->next.get())
		{
			if (lvl->points.empty() || lvl->polys.empty()) { continue; }
			if (lvl->tilesetTextureFiles[0].texfile.empty()) { continue; }
			levels.push_back({ lvl, m, mirrored, texLvl, static_cast<int>(pi) });
		}
	}
	if (levelPartOut != nullptr) { levelPartOut->clear(); for (const Lv &lv : levels) { levelPartOut->push_back(lv.part); } }
	std::ostringstream o;
	o.setf(std::ios::fixed); o.precision(3);
	if (levels.empty()) { return std::string(); }
	o << "PIE 4\n";
	o << "TYPE " << std::hex << levels[0].s->flags << std::dec << "\n";
	texLines(o, levels[0].tex != nullptr ? *levels[0].tex : *levels[0].s);
	o << "LEVELS " << levels.size() << "\n";
	for (size_t li = 0; li < levels.size(); ++li)
	{
		const iIMDShape &s = *levels[li].s;
		const glm::mat4 &m = levels[li].m;
		o << "LEVEL " << (li + 1) << "\n";
		o << "TYPE " << std::hex << s.flags << std::dec << "\n";
		texLines(o, levels[li].tex != nullptr ? *levels[li].tex : s);
		// (27.11.) Geometrie im Teil-Raum, ggf. geschnitten
		std::vector<glm::vec3> P;
		std::vector<std::array<uint32_t, 3>> I;
		std::vector<std::array<Vector2f, 3>> UV;
		P.reserve(s.points.size());
		for (const Vector3f &v : s.points) { P.emplace_back(v.x, v.y, v.z); }
		for (const iIMDPoly &poly : s.polys) { I.push_back({ poly.pindex[0], poly.pindex[1], poly.pindex[2] }); UV.push_back(poly.texCoord); }
		if (levels[li].tex != nullptr) { transferUV(P, I, UV, *levels[li].tex); }   // (27.11. 23:58) Texturstellen des anderen Rumpfes
		{
			glm::vec3 cn, cc;
			if (levels[li].part >= 0 && levels[li].part < static_cast<int>(d.parts.size()) && cutPlane(d.parts[static_cast<size_t>(levels[li].part)], cn, cc))
			{
				clipLevel(P, I, UV, cn, cc);
			}
		}
		o << "POINTS " << P.size() << "\n";
		for (const glm::vec3 &v : P)
		{
			const glm::vec4 q = m * glm::vec4(v, 1.f);
			o << "\t" << q.x << " " << q.y << " " << q.z << "\n";
		}
		o << "POLYGONS " << I.size() << "\n";
		o.precision(6);
		const bool mir = levels[li].mirrored;
		for (size_t t = 0; t < I.size(); ++t)
		{
			static const size_t ordN[3] = { 0, 1, 2 }, ordM[3] = { 0, 2, 1 };   // gespiegelt: Umlaufsinn tauschen
			const size_t *ord = mir ? ordM : ordN;
			o << "\t200 3 " << I[t][ord[0]] << " " << I[t][ord[1]] << " " << I[t][ord[2]];
			for (int k = 0; k < 3; ++k) { o << " " << UV[t][ord[k]].x << " " << UV[t][ord[k]].y; }
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

// ---------------------------------------------------------------- (Wunsch 27.11. 20:56 "A+B") Animation ohne Skelett
namespace
{
/// Teil, in dessen (Ruhe-)Umriss der Punkt liegt - das kleinste; -1 = keins
int partContaining(const Def &d, const glm::vec3 &pt)
{
	int best = -1;
	float bestVol = 1e30f;
	for (size_t i = 0; i < d.parts.size(); ++i)
	{
		const BodyInfo *b = bodyInfo(d.parts[i].body);
		const iIMDShape *m = (b != nullptr && b->model != nullptr) ? b->model->displayModel() : nullptr;
		if (m == nullptr) { continue; }
		const glm::mat4 Mi = glm::inverse(partMatrix(d.parts[i]));
		const glm::vec3 q = glm::vec3(Mi * glm::vec4(pt, 1.f));
		const float pad = 3.f;
		if (q.x < m->min.x - pad || q.x > m->max.x + pad || q.y < m->min.y - pad || q.y > m->max.y + pad || q.z < m->min.z - pad || q.z > m->max.z + pad) { continue; }
		const glm::mat3 M3(partMatrix(d.parts[i]));
		const float vol = std::fabs(glm::determinant(M3)) * std::max(1.f, static_cast<float>(m->max.x - m->min.x)) * std::max(1.f, static_cast<float>(m->max.y - m->min.y))
		                  * std::max(1.f, static_cast<float>(m->max.z - m->min.z));
		if (vol < bestVol) { bestVol = vol; best = static_cast<int>(i); }
	}
	return best;
}

glm::mat4 rotYXZ(const glm::vec3 &r)
{
	glm::mat4 m(1.f);
	m = glm::rotate(m, glm::radians(r.x), glm::vec3(0.f, 1.f, 0.f));
	m = glm::rotate(m, glm::radians(r.y), glm::vec3(1.f, 0.f, 0.f));
	m = glm::rotate(m, glm::radians(r.z), glm::vec3(0.f, 0.f, 1.f));
	return m;
}

float smooth01(float x)
{
	x = std::clamp(x, 0.f, 1.f);
	return x * x * (3.f - 2.f * x);
}

float whenWeight(int when, const AnimCtx &c)
{
	switch (when)
	{
	case WHEN_MOVING: return c.moveW;
	case WHEN_STANDING: return 1.f - c.moveW;
	case WHEN_FIRING: return c.fireW;
	default: return 1.f;
	}
}

float whenClock(int when, const AnimCtx &c)
{
	switch (when)
	{
	case WHEN_MOVING: return c.moveClock;
	case WHEN_STANDING: return c.standClock;
	case WHEN_FIRING: return c.fireClock;
	default: return c.time;
	}
}

/// Bewegungs-Baustein: Drehung (Grad wie PoseKey::rot) und Verschiebung
void motionDelta(const Motion &m, const AnimCtx &c, glm::vec3 &rot, glm::vec3 &pos)
{
	if (m.type == MOT_NONE) { return; }
	const float w = whenWeight(m.when, c);
	const float t = whenClock(m.when, c);
	const float ph = 6.2831853f * (t * m.speed + m.phase);
	const int ax = std::clamp(m.axis, 0, 2);
	auto addRot = [&rot, ax](float deg) {
		if (ax == 0) { rot.y += deg; }        // um x: nicken
		else if (ax == 1) { rot.x += deg; }   // um y: drehen
		else { rot.z += deg; }                // um z: rollen
	};
	switch (m.type)
	{
	case MOT_LEG:
	case MOT_SWAY:
		if (w > 1e-3f) { addRot(m.amount * std::sin(ph) * w); }
		break;
	case MOT_SPIN:   // laeuft nur mit seiner Uhr weiter (bleibt beim Anhalten stehen, ohne Sprung)
		addRot(std::fmod(360.f * (t * m.speed + m.phase), 360.f));
		break;
	case MOT_BOB:
		if (w > 1e-3f) { pos[ax] += m.amount * std::sin(ph) * w; }
		break;
	case MOT_PUMP:
		if (w > 1e-3f) { pos[ax] += m.amount * (0.5f - 0.5f * std::cos(ph)) * w; }
		break;
	default: break;
	}
}

PoseKey keyAt(const Part &p, int clip, int k)
{
	return (k >= 0 && k < static_cast<int>(p.keys[clip].size())) ? p.keys[clip][static_cast<size_t>(k)] : PoseKey();
}

/// Posen-Beitrag eines Ablaufs
void poseDelta(const Def &d, const Part &p, int clip, const AnimCtx &c, glm::vec3 &rot, glm::vec3 &pos)
{
	const int n = std::clamp(d.poseCount[clip], 0, MAX_POSES);
	if (n <= 0) { return; }
	const float secs = std::max(0.1f, d.poseSeconds[clip]);
	PoseKey a, b;
	float f = 0.f, w = 1.f;
	if (clip == CLIP_FIRING)
	{
		// einmal je Schuss: Ruhe -> Pose 1 -> ... -> Pose n -> Ruhe
		const float u = c.fireAge / secs;
		if (u >= 1.f || u < 0.f) { return; }
		const float sgm = u * static_cast<float>(n + 1);
		const int k = std::min(static_cast<int>(sgm), n);
		a = k == 0 ? PoseKey() : keyAt(p, clip, k - 1);
		b = k >= n ? PoseKey() : keyAt(p, clip, k);
		f = smooth01(sgm - static_cast<float>(k));
	}
	else
	{
		w = clip == CLIP_MOVING ? c.moveW : 1.f - c.moveW;
		if (w <= 1e-3f) { return; }
		if (n == 1) { a = b = keyAt(p, clip, 0); }
		else
		{
			const float u = (clip == CLIP_MOVING ? c.moveClock : c.standClock) / secs;
			const float sgm = (u - std::floor(u)) * static_cast<float>(n);
			const int k = std::min(static_cast<int>(sgm), n - 1);
			a = keyAt(p, clip, k);
			b = keyAt(p, clip, (k + 1) % n);
			f = smooth01(sgm - static_cast<float>(k));
		}
	}
	rot += glm::mix(a.rot, b.rot, f) * w;
	pos += glm::mix(a.pos, b.pos, f) * w;
}
} // namespace

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
	std::vector<int> lvlPart;
	const std::string pie = buildPie(d, conns, &lvlPart);
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
	// (27.11.) Animation: Teil je Ebene und je Anschluss
	sl.anim = hasAnim(d);
	sl.levelPart.clear(); sl.mountPart.clear(); sl.animDef = Def();
	if (sl.anim)
	{
		sl.animDef = d;
		sl.levelPart = lvlPart;
		std::vector<Mount> mounts = d.mounts;
		if (mounts.empty() && !d.parts.empty()) { mounts = defaultMounts(d.parts[0]); mounts.resize(std::min<size_t>(mounts.size(), MAX_SLOTS)); for (Mount &m : mounts) { m.part = 0; } }
		for (const Mount &m : mounts) { sl.mountPart.push_back(m.part >= 0 ? m.part : partContaining(d, m.pos)); }
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

// ---------------------------------------------------------------- (27.11.) Animation: oeffentliche Schnittstelle
bool hasAnim(const Def &d)
{
	for (const Part &p : d.parts)
	{
		if (p.motion.type != MOT_NONE) { return true; }
		for (int c = 0; c < CLIP_COUNT; ++c)
		{
			if (d.poseCount[c] <= 0) { continue; }
			for (const PoseKey &k : p.keys[c]) { if (glm::length(k.rot) > 1e-3f || glm::length(k.pos) > 1e-3f) { return true; } }
		}
	}
	return false;
}

glm::vec3 pivotOf(const Part &p)
{
	return glm::vec3(partMatrix(p) * glm::vec4(p.pivot, 1.f));
}

glm::vec3 pivotPreset(const Part &p, int where)
{
	const BodyInfo *b = bodyInfo(p.body);
	const iIMDShape *m = (b != nullptr && b->model != nullptr) ? b->model->displayModel() : nullptr;
	if (m == nullptr) { return glm::vec3(0.f); }
	const float cx = (m->min.x + m->max.x) * 0.5f, cz = (m->min.z + m->max.z) * 0.5f;
	const float y = where <= 0 ? static_cast<float>(m->min.y) : (where == 1 ? (m->min.y + m->max.y) * 0.5f : static_cast<float>(m->max.y));
	return glm::vec3(cx, y, cz);
}

Motion defaultMotion(int type)
{
	Motion m;
	m.type = std::clamp(type, 0, MOT_COUNT - 1);
	switch (m.type)
	{
	case MOT_LEG: m.axis = 0; m.when = WHEN_MOVING; m.amount = 25.f; m.speed = 1.f; break;
	case MOT_SPIN: m.axis = 1; m.when = WHEN_ALWAYS; m.amount = 0.f; m.speed = 0.25f; break;
	case MOT_BOB: m.axis = 1; m.when = WHEN_MOVING; m.amount = 2.f; m.speed = 2.f; break;
	case MOT_PUMP: m.axis = 2; m.when = WHEN_FIRING; m.amount = 4.f; m.speed = 3.f; break;
	case MOT_SWAY: m.axis = 2; m.when = WHEN_STANDING; m.amount = 4.f; m.speed = 0.3f; break;
	default: break;
	}
	return m;
}

const char *motionName(int type)
{
	static const char *const N[MOT_COUNT] = { "None", "Leg", "Spin", "Bob", "Pump", "Sway" };
	return N[std::clamp(type, 0, MOT_COUNT - 1)];
}

const char *whenName(int when)
{
	static const char *const N[WHEN_COUNT] = { "Always", "Moving", "Standing", "Firing" };
	return N[std::clamp(when, 0, WHEN_COUNT - 1)];
}

void evalAnim(const Def &d, const AnimCtx &c, std::vector<glm::mat4> &partW)
{
	const size_t n = d.parts.size();
	std::vector<glm::mat4> L(n, glm::mat4(1.f));
	for (size_t i = 0; i < n; ++i)
	{
		const Part &p = d.parts[i];
		glm::vec3 rot(0.f), pos(0.f);
		for (int k = 0; k < CLIP_COUNT; ++k) { poseDelta(d, p, k, c, rot, pos); }
		motionDelta(p.motion, c, rot, pos);
		if (glm::length(rot) < 1e-4f && glm::length(pos) < 1e-4f) { continue; }
		const glm::vec3 P = pivotOf(p);
		L[i] = glm::translate(glm::mat4(1.f), P + pos) * rotYXZ(rot) * glm::translate(glm::mat4(1.f), -P);
	}
	// (27.11.) Verbund (Join): alle Teile eines Verbunds bewegen sich wie sein erstes Teil (Fuehrer) - ein starres Stueck
	std::vector<int> leader(n);
	for (size_t i = 0; i < n; ++i)
	{
		leader[i] = static_cast<int>(i);
		if (d.parts[i].group == 0) { continue; }
		for (size_t j = 0; j < i; ++j) { if (d.parts[j].group == d.parts[i].group) { leader[i] = static_cast<int>(j); break; } }
	}
	partW.assign(n, glm::mat4(1.f));
	for (size_t i = 0; i < n; ++i)
	{
		if (leader[i] != static_cast<int>(i)) { continue; }
		glm::mat4 w = L[i];
		int q = d.parts[i].parent, guard = 0;
		while (q >= 0 && q < static_cast<int>(n) && guard++ < 32)
		{
			q = leader[static_cast<size_t>(q)];
			if (q == static_cast<int>(i)) { break; }
			w = L[static_cast<size_t>(q)] * w;
			q = d.parts[static_cast<size_t>(q)].parent;
		}
		partW[i] = w;
	}
	for (size_t i = 0; i < n; ++i) { if (leader[i] != static_cast<int>(i)) { partW[i] = partW[static_cast<size_t>(leader[i])]; } }
}

bool animated(size_t bodyIdx)
{
	return isComposite(bodyIdx) && s_slots[bodyIdx - s_first].anim;
}

bool animate(size_t bodyIdx, const AnimCtx &c, std::vector<glm::mat4> &partW)
{
	if (!animated(bodyIdx)) { return false; }
	evalAnim(s_slots[bodyIdx - s_first].animDef, c, partW);
	return true;
}

int levelPart(size_t bodyIdx, size_t level)
{
	if (!isComposite(bodyIdx)) { return -1; }
	const Slot &sl = s_slots[bodyIdx - s_first];
	return level < sl.levelPart.size() ? sl.levelPart[level] : -1;
}

int mountPart(size_t bodyIdx, size_t connector)
{
	if (!isComposite(bodyIdx)) { return -1; }
	const Slot &sl = s_slots[bodyIdx - s_first];
	return connector < sl.mountPart.size() ? sl.mountPart[connector] : -1;
}


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

bool cutPlane(const Part &p, glm::vec3 &n, glm::vec3 &c)
{
	if (p.cut <= 0 || p.cut > 3) { return false; }
	const BodyInfo *b = bodyInfo(p.body);
	const iIMDShape *m = (b != nullptr && b->model != nullptr) ? b->model->displayModel() : nullptr;
	if (m == nullptr) { return false; }
	const float a = glm::radians(p.cutAngle);
	if (p.cut == 1) { n = glm::vec3(std::cos(a), 0.f, -std::sin(a)); }
	else if (p.cut == 2) { n = glm::vec3(std::sin(a), 0.f, std::cos(a)); }
	else { n = glm::vec3(0.f, std::cos(a), std::sin(a)); }
	if (p.cutFlip) { n = -n; }
	const glm::vec3 mid((m->min.x + m->max.x) * 0.5f, (m->min.y + m->max.y) * 0.5f, (m->min.z + m->max.z) * 0.5f);
	c = mid + n * (p.cutFlip ? -p.cutPos : p.cutPos);   // Versatz in Richtung der Grund-Normale (unabhaengig von "andere Haelfte")
	return true;
}

const iIMDShape *bakedShape(const Part &src)
{
	if (src.cut == 0) { return bakedShape(src.body, src.flip, src.texFrom); }
	const bool tex = !src.texFrom.empty() && src.texFrom != src.body;
	static std::map<std::string, iIMDBaseShape *> cache;
	static unsigned gen = 0;
	if (modelCacheStale(gen)) { cache.clear(); }
	char cbuf[96];
	snprintf(cbuf, sizeof(cbuf), "_c%d_%d_%d_%d", src.cut, static_cast<int>(std::lround(src.cutAngle * 10.f)), static_cast<int>(std::lround(src.cutPos * 10.f)), src.cutFlip ? 1 : 0);
	const std::string name = "compcut_" + slug(src.body) + "_" + std::to_string(src.flip & 7) + (tex ? "_" + slug(src.texFrom) : std::string()) + cbuf + ".pie";
	auto it = cache.find(name);
	if (it != cache.end()) { return it->second != nullptr ? it->second->displayModel() : nullptr; }
	cache[name] = nullptr;
	Def d;
	d.id = "cut";
	Part p;
	p.body = src.body;
	p.flip = src.flip & 7;
	if (tex) { p.texFrom = src.texFrom; }
	p.cut = src.cut; p.cutAngle = src.cutAngle; p.cutPos = src.cutPos; p.cutFlip = src.cutFlip;
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

// ---------------------------------------------------------------- (27.11. 23:19) eigene Animations-Saetze
namespace
{
constexpr const char *SET_DIR = "userdata/animsets";
}

std::vector<AnimSetInfo> animSets()
{
	std::vector<AnimSetInfo> out;
	if (!PHYSFS_exists(SET_DIR)) { return out; }
	std::vector<std::string> files;
	WZ_PHYSFS_enumerateFiles(SET_DIR, [&](const char *f) -> bool {
		const std::string n(f);
		if (n.size() > 5 && n.compare(n.size() - 5, 5, ".json") == 0) { files.push_back(n); }
		return true;
	});
	std::sort(files.begin(), files.end());
	for (const std::string &f : files)
	{
		Def d;
		const std::string path = std::string(SET_DIR) + "/" + f;
		if (!loadAnimSet(path, d)) { continue; }
		AnimSetInfo i; i.name = d.name.empty() ? f.substr(0, f.size() - 5) : d.name; i.path = path; i.parts = d.parts.size();
		out.push_back(i);
	}
	return out;
}

bool saveAnimSet(const Def &d, const std::string &name, std::string &err)
{
	if (d.parts.empty()) { err = "Nothing to save - the hull has no parts."; return false; }
	Def s = d;
	s.name = name.empty() ? std::string("My set") : name;
	s.id = "AnimSet_" + slug(s.name);
	s.preset = false; s.deleted = false;
	PHYSFS_mkdir("userdata");
	PHYSFS_mkdir(SET_DIR);
	const std::string path = std::string(SET_DIR) + "/" + slug(s.name) + ".json";
	if (!saveJSONToFile(toJson(s), path.c_str())) { err = "Could not write " + path; return false; }
	return true;
}

bool loadAnimSet(const std::string &path, Def &out)
{
	std::vector<char> buf;
	if (!loadFileToBufferVector(path.c_str(), buf, false, true)) { return false; }
	try
	{
		const nlohmann::json j = nlohmann::json::parse(buf.data());
		return fromJson(j, out);
	}
	catch (const std::exception &e)
	{
		debug(LOG_ERROR, "Animations-Satz %s ist kein gueltiges JSON (%s)", path.c_str(), e.what());
		return false;
	}
}

bool deleteAnimSet(const std::string &path)
{
	return path.compare(0, std::strlen(SET_DIR), SET_DIR) == 0 && PHYSFS_delete(path.c_str()) != 0;
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
