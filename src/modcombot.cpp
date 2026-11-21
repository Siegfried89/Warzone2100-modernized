// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Modularer Combot-Zusammenbau zur Laufzeit.
// (20.11.) Neu nach Metal Fatigue: Skelett mit den 28 MF-Knochenplaetzen je Einzelteil (parts.json "bones"), Animator wie MF
// (Grund-Schicht mit Ueberblenden + Angriffs-Schicht mit Knochengewichten), Gang-Stufen nach Tempo, Ereignis-Klaenge.
// Fehlende Teile (Arme/Beine) werden einfach nicht gezeichnet, die Animation laeuft fuer alle uebrigen Teile weiter.

#include <nlohmann/json.hpp> // vor den WZ-Headern
#include "lib/framework/modtime.h"

#include "modcombot.h"

#include "lib/framework/frame.h"
#include "lib/framework/file.h"
#include "lib/framework/frameresource.h"
#include "lib/framework/vector.h"
#include "lib/framework/fixedpoint.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/sound/audio.h"

#include "baseobject.h"
#include "droid.h"
#include "effects.h"
#include "feature.h"
#include "fpath.h"
#include "game_world.h"
#include "map.h"
#include "modlayermap.h"
#include "modlayer.h"
#include "modduel.h"
#include "modcrew.h"
#include "random.h"
#include "stats.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <map>
#include <unordered_map>

namespace modcombot
{

namespace
{

// ---------------------------------------------------------------- Daten
// MOD (Wunsch 20.11. 21:43 "die combots animationen sind immer noch KOMPLETT falsch"): Animationen 1:1 nach Metal Fatigue
// (mfatigue.exe / SGLRendEng.dll, Daten aus mfanim3.py): 28 Knochenplaetze, Gewicht je Knochen, Schichten (Grund + Angriff),
// Ueberblenden, benannte Animationen (stand, walk-Stufen, standturn, attackidlestill, death_*, Jetpack), Ereignisse (Schritt-Klang usw.)

constexpr int NSLOT = 32;      // 28 MF-Plaetze; 31 = Einzelteil ohne eigene Animation (bewegt sich starr mit dem Elternteil)

struct Anim
{
	std::string key;
	int nf = 0;
	float T = 0.f;                      // Dauer (s)
	float blendIn = 0.2f;               // Ueberblendzeit beim Wechsel auf diese Animation (s)
	float blendOut = 0.2f;              // Ausblenden am Ende (s)
	float tFire = -1.f;                 // Angriff: Zeitpunkt des Schusses (Ereignis 0x1..0x10), -1 = keiner
	float tHit = -1.f;                  // Nahkampf: Treffer-Moment (0x200)
	float tOut = -1.f;                  // Beginn des Ausblendens (0x40000000)
	std::vector<int8_t> slots;          // Spalte -> Knochenplatz
	int8_t idx[NSLOT];                  // Knochenplatz -> Spalte (-1 = nicht in der Animation)
	float wt[NSLOT];                    // Gewicht je Knochenplatz (MF: 0.995 = voll, 0.01 = fast nichts, 0 = gar nicht)
	std::vector<glm::vec3> pos;         // Becken-Lage je Bild (MF-Einheiten, z hoch)
	std::vector<uint32_t> w;            // nf * nb gepackte Winkel (3 x 10 Bit, 1024 = 360 Grad)
	std::vector<float> times;           // leer = gleichmaessige Bilder
	std::vector<std::pair<float, uint32_t>> ev;   // Ereignisse (Zeit, Bits): 0x10 Schritt, 0x20/0x40 Servos, 0x200 Treffer
	int nb() const { return static_cast<int>(slots.size()); }
};

struct Tiers
{
	float thr[4] = { 11.5f, 15.f, 18.5f, 22.f };   // MF-Tempo-Schwellen (MF-Einheiten/s)
	const Anim *walk[4] = { nullptr, nullptr, nullptr, nullptr };
};
struct JetSet { const Anim *fly = nullptr, *idle = nullptr, *takeoff = nullptr, *land = nullptr; };
struct ArmAnims { std::vector<const Anim *> R, L, X; };
struct PartSounds { int fire = -1, melee = -1, destroy = -1, move = -1; };

bool s_loaded = false;
bool s_ok = false;
bool s_mirrored = false;         // (20.11.) Teile in WZ-Handhabung gespiegelt (mfmirror.py) -> Abbildung MF (x,y,z) -> Modell (x,z,y)
float s_scale = 1.8f;
float s_show = 1.f;   // MOD (19.11. 23:00 "die Groessenordnung auch"): Anzeige-Vergroesserung im Spiel (parts.json "show"), Baustation unveraendert
float s_front = 8.2f;  // (21.11.) Brusttiefe des Grund-Combots in der Kampfhaltung (MF-Einheiten, parts.json "front" von mfreach.py)
std::vector<Part> s_parts;
std::unordered_map<std::string, Anim> s_anims;
const Anim *s_stand = nullptr, *s_standCrewless = nullptr, *s_standTurn = nullptr, *s_attackIdle = nullptr;
const Anim *s_death[5] = { nullptr, nullptr, nullptr, nullptr, nullptr };
const Anim *s_hit[4] = { nullptr, nullptr, nullptr, nullptr };
JetSet s_jetTorso, s_jetLegs;
std::map<std::string, Tiers> s_tiers;          // Beinsorte (Datei) -> Gang-Stufen
std::map<std::string, ArmAnims> s_armAnims;    // Waffenarm (Datei) -> Angriffe
std::map<std::string, PartSounds> s_partSnd;   // "Datei:Art" -> MF-Klaenge
int s_sndStep = -1, s_sndServoA = -1, s_sndServoB = -1;
std::unordered_map<std::string, size_t> s_partIndex;
std::unordered_map<const void *, const Part *> s_bodyCache, s_propCache;
std::unordered_map<unsigned, const Part *> s_weapCache;

struct State
{
	unsigned severed = 0;
	int hp[SLOT_COUNT] = { 0, 0, 0, 0 };
	int hpMax[SLOT_COUNT] = { 1, 1, 1, 1 };
	WeaponMode mode = MODE_ALL;
};
std::unordered_map<unsigned, State> s_state;

struct Corpse
{
	Loadout lo;
	unsigned birth = 0;
	int variant = 0;
	int x = 0, y = 0;          // Weltlage (fuer den Klang)
	bool boomed = false;       // MF-Ereignis 0x1 der Todesanimation (Aufschlag/Zerfall) schon gespielt
};
std::unordered_map<unsigned, Corpse> s_corpses;
unsigned s_corpseNext = 1000;

bool readAnim(const std::string &key, const nlohmann::json &j, Anim &a)
{
	a.key = key;
	a.nf = j.value("nf", 0);
	a.T = j.value("T", 0.f);
	a.blendIn = std::clamp(j.value("bi", 0.2f), 0.05f, 0.6f);
	a.blendOut = std::clamp(j.value("bo", 0.2f), 0.05f, 0.6f);
	std::fill(std::begin(a.idx), std::end(a.idx), static_cast<int8_t>(-1));
	std::fill(std::begin(a.wt), std::end(a.wt), 0.f);
	if (!j.contains("s") || !j.contains("w") || !j.contains("p")) { return false; }
	const auto &s = j["s"];
	const auto &wt = j.contains("wt") ? j["wt"] : nlohmann::json::array();
	for (size_t i = 0; i < s.size(); ++i)
	{
		const int sl = s[i].get<int>();
		const int8_t col = static_cast<int8_t>(a.slots.size());
		a.slots.push_back(static_cast<int8_t>(sl >= 0 && sl < NSLOT ? sl : NSLOT - 1));
		if (sl >= 0 && sl < NSLOT)
		{
			a.idx[sl] = col;
			a.wt[sl] = std::clamp(i < wt.size() ? wt[i].get<float>() : 1.f, 0.f, 1.f);
		}
	}
	for (const auto &p : j["p"]) { a.pos.emplace_back(p[0].get<float>(), p[1].get<float>(), p[2].get<float>()); }
	const size_t nb = a.slots.size();
	for (const auto &row : j["w"])
	{
		for (size_t i = 0; i < nb; ++i) { a.w.push_back(i < row.size() ? row[i].get<uint32_t>() : 0u); }
	}
	if (j.contains("t")) { for (const auto &t : j["t"]) { a.times.push_back(t.get<float>()); } }
	if (j.contains("ev")) { for (const auto &e : j["ev"]) { a.ev.emplace_back(e[0].get<float>(), e[1].get<uint32_t>()); } }
	for (const auto &e : a.ev)
	{
		if ((e.second & 0x1Fu) != 0 && a.tFire < 0.f) { a.tFire = e.first; }
		if ((e.second & 0x200u) != 0 && a.tHit < 0.f) { a.tHit = e.first; }
		if ((e.second & 0x40000000u) != 0 && a.tOut < 0.f) { a.tOut = e.first; }
	}
	return a.nf >= 2 && a.T > 0.f && a.pos.size() == static_cast<size_t>(a.nf) && a.w.size() == nb * static_cast<size_t>(a.nf)
	       && (a.times.empty() || a.times.size() == static_cast<size_t>(a.nf));
}

glm::vec3 jv(const nlohmann::json &j, const char *key)
{
	if (!j.contains(key) || !j[key].is_array() || j[key].size() < 3) { return glm::vec3(0.f); }
	return glm::vec3(j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>());
}

const Anim *animByKey(const nlohmann::json &j)
{
	if (!j.is_string()) { return nullptr; }
	auto it = s_anims.find(j.get<std::string>());
	return it == s_anims.end() ? nullptr : &it->second;
}

// alte parts.json ohne MF-Skelett: Knochen aus den bisherigen Feldern (Beine 7, Torso 2, Arm 3 Teile)
void fallbackBones(Part &p)
{
	const size_t n = p.pieNames.size();
	p.bones.assign(n, PieceBone());
	auto set = [&](size_t i, int slot, int parent, const glm::vec3 &off) { if (i < n) { p.bones[i].slot = slot; p.bones[i].parent = parent; p.bones[i].off = off; } };
	if (p.kind == KIND_LEG)
	{
		set(0, 0, -1, glm::vec3(0.f)); set(1, 1, 0, p.hipR); set(2, 2, 1, p.kneeA); set(3, 3, 2, p.ankA);
		set(4, 6, 0, p.hipL); set(5, 7, 4, p.kneeB); set(6, 8, 5, p.ankB);
		for (size_t i = 7; i < n; ++i) { set(i, 31, 6, glm::vec3(0.f)); }
	}
	else if (p.kind == KIND_TORSO)
	{
		set(0, 11, -1, glm::vec3(0.f)); set(1, 12, 0, p.neck);
		for (size_t i = 2; i < n; ++i) { set(i, 31, 0, glm::vec3(0.f)); }
	}
	else
	{
		const int b = p.side < 0 ? 18 : 13;
		set(0, b, -1, glm::vec3(0.f));
		set(1, b + 1, 0, p.chain.size() > 0 ? p.chain[0] : glm::vec3(0.f));
		set(2, b + 2, 1, p.chain.size() > 1 ? p.chain[1] : glm::vec3(0.f));
		for (size_t i = 3; i < n; ++i) { set(i, 31, 2, glm::vec3(0.f)); }   // Anbauten sind in den Rahmen der Hand gebaut
	}
}

bool loadAll()
{
	ModLoadTimer modTimer("Combot-Teile", 50);   // MOD 16.11.
	std::vector<char> buf;
	if (!loadFileToBufferVector("mfmodular/parts.json", buf, false, false) || buf.empty()) { return false; }
	nlohmann::json pj = nlohmann::json::parse(buf.begin(), buf.end(), nullptr, false);
	if (pj.is_discarded() || !pj.contains("parts")) { return false; }
	s_scale = pj.value("scale", 1.8f);
	s_show = std::clamp(pj.value("show", 1.f), 0.25f, 4.f);
	s_mirrored = pj.value("mirrored", 0) != 0;
	s_front = std::clamp(pj.value("front", 8.2f), 2.f, 30.f);
	for (const auto &d : pj["parts"])
	{
		Part p;
		p.id = d.value("id", "");
		p.file = d.value("file", "");
		p.animFile = d.value("anim", p.file);
		p.name = d.value("name", p.id);
		p.feature = d.value("feature", "");
		const std::string k = d.value("kind", "");
		p.kind = k == "torso" ? KIND_TORSO : (k == "leg" ? KIND_LEG : KIND_ARM);
		p.side = d.value("side", 0);
		p.corp = d.value("corp", -1);
		for (const auto &n : d["pies"]) { p.pieNames.push_back(n.get<std::string>()); }
		if (d.contains("abil")) { for (const auto &n : d["abil"]) { p.abil.push_back(n.get<std::string>()); } }
		if (d.contains("st"))
		{
			const auto &s = d["st"];
			p.st.hp = s.value("hp", 0); p.st.ak = s.value("ak", 0); p.st.ah = s.value("ah", 0); p.st.spd = s.value("spd", 0);
			p.st.dmg = s.value("dmg", 0); p.st.rng = s.value("rng", 0); p.st.blk = s.value("blk", 0); p.st.melee = s.value("melee", 0) != 0;
			// (21.11.) echte MF-Werte (mfunit.py / mfreach.py)
			p.st.mfhp = s.value("mfhp", 0.f); p.st.ms = std::clamp(s.value("ms", 0.f), 0.f, 0.95f); p.st.es = std::clamp(s.value("es", 0.f), 0.f, 0.95f);
			p.st.md = s.value("md", 0.f); p.st.ed = s.value("ed", 0.f); p.st.cyc = std::clamp(s.value("cyc", 0.75f), 0.2f, 30.f);
			p.st.acc = std::clamp(s.value("acc", 0.85f), 0.3f, 1.f); p.st.rch = s.value("rch", 0.f);
		}
		if (d.contains("rch") && d["rch"].is_object())
		{
			for (auto it = d["rch"].begin(); it != d["rch"].end(); ++it)
			{
				if (it.value().is_array() && it.value().size() >= 2)
				{
					p.reach.emplace_back(it.key(), glm::vec2(it.value()[0].get<float>(), it.value()[1].get<float>()));
				}
			}
		}
		p.shR = jv(d, "shR"); p.shL = jv(d, "shL");
		if (d.contains("neck")) { p.neck = jv(d, "neck"); }
		p.hipR = jv(d, "hipR"); p.hipL = jv(d, "hipL"); p.mount = jv(d, "mount");
		p.kneeA = jv(d, "kneeA"); p.ankA = jv(d, "ankA"); p.kneeB = jv(d, "kneeB"); p.ankB = jv(d, "ankB");
		if (d.contains("eye") && d["eye"].is_object())
		{
			const auto &e = d["eye"];
			p.eyePiece = e.value("piece", -1);
			if (e.contains("pos") && e["pos"].is_array() && e["pos"].size() == 3) { p.eyePos = glm::vec3(e["pos"][0].get<float>(), e["pos"][1].get<float>(), e["pos"][2].get<float>()); }
			p.eyeGlow = e.value("glow", "");
		}
		if (d.contains("fx") && d["fx"].is_object())
		{
			const auto &e = d["fx"];
			p.fxPiece = e.value("piece", -1);
			if (e.contains("pos") && e["pos"].is_array() && e["pos"].size() == 3) { p.fxPos = glm::vec3(e["pos"][0].get<float>(), e["pos"][1].get<float>(), e["pos"][2].get<float>()); }
			p.fxModel = e.value("model", "");
		}
		if (d.contains("chain"))
		{
			for (const auto &c : d["chain"]) { p.chain.emplace_back(c[0].get<float>(), c[1].get<float>(), c[2].get<float>()); }
		}
		if (p.pieNames.empty() || p.id.empty()) { continue; }
		// (20.11.) MF-Skelett je Einzelteil: [Platz, Elternteil, Versatz xyz, Bauversatz xyz]
		if (d.contains("bones") && d["bones"].is_array() && d["bones"].size() == p.pieNames.size())
		{
			for (const auto &b : d["bones"])
			{
				PieceBone pb;
				if (b.is_array() && b.size() >= 8)
				{
					pb.slot = b[0].get<int>(); pb.parent = b[1].get<int>();
					pb.off = glm::vec3(b[2].get<float>(), b[3].get<float>(), b[4].get<float>());
					pb.rel = glm::vec3(b[5].get<float>(), b[6].get<float>(), b[7].get<float>());
				}
				p.bones.push_back(pb);
			}
		}
		if (p.bones.size() != p.pieNames.size()) { fallbackBones(p); }
		for (size_t i = 0; i < p.bones.size(); ++i)
		{
			PieceBone &b = p.bones[i];
			if (b.slot < 0 || b.slot >= NSLOT) { b.slot = NSLOT - 1; }
			if (i == 0 || b.parent < 0 || static_cast<size_t>(b.parent) >= i) { b.parent = i == 0 ? -1 : 0; }   // Eltern stehen vor den Kindern
		}
		p.ground = d.value("ground", 0.f);
		p.sole = d.value("sole", -2.8f);
		p.legWeapon = d.value("legweapon", 0) != 0;
		s_partIndex[p.id] = s_parts.size();
		s_parts.push_back(std::move(p));
	}
	std::vector<char> ab;
	if (loadFileToBufferVector("mfmodular/anims.json", ab, false, false) && !ab.empty())
	{
		nlohmann::json aj = nlohmann::json::parse(ab.begin(), ab.end(), nullptr, false);
		if (!aj.is_discarded() && aj.value("v", 0) == 3 && aj.contains("anims"))
		{
			for (auto it = aj["anims"].begin(); it != aj["anims"].end(); ++it)
			{
				Anim a;
				if (readAnim(it.key(), it.value(), a)) { s_anims.emplace(it.key(), std::move(a)); }
			}
			const auto &b = aj["base"];
			s_stand = animByKey(b["stand"]);
			s_standCrewless = animByKey(b["standcrewless"]);
			s_standTurn = animByKey(b["standturn"]);
			s_attackIdle = animByKey(b["attackidlestill"]);
			for (int i = 0; i < 5 && i < static_cast<int>(b["death"].size()); ++i) { s_death[i] = animByKey(b["death"][i]); }
			for (int i = 0; i < 4 && i < static_cast<int>(b["hit"].size()); ++i) { s_hit[i] = animByKey(b["hit"][i]); }
			auto jet = [&](const nlohmann::json &j, JetSet &s) {
				s.fly = animByKey(j["Fly"]); s.idle = animByKey(j["Idle"]); s.takeoff = animByKey(j["TakeOff"]); s.land = animByKey(j["Land"]);
			};
			if (aj.contains("jet")) { jet(aj["jet"]["torso"], s_jetTorso); jet(aj["jet"]["legs"], s_jetLegs); }
			if (aj.contains("legs"))
			{
				for (auto it = aj["legs"].begin(); it != aj["legs"].end(); ++it)
				{
					Tiers t;
					for (int i = 0; i < 4; ++i)
					{
						t.thr[i] = it.value()["thr"][i].get<float>();
						t.walk[i] = animByKey(it.value()["walk"][i]);
					}
					s_tiers[it.key()] = t;
				}
			}
			if (aj.contains("arms"))
			{
				for (auto it = aj["arms"].begin(); it != aj["arms"].end(); ++it)
				{
					ArmAnims a;
					for (const auto &k : it.value()["R"]) { if (const Anim *x = animByKey(k)) { a.R.push_back(x); } }
					for (const auto &k : it.value()["L"]) { if (const Anim *x = animByKey(k)) { a.L.push_back(x); } }
					if (it.value().contains("X")) { for (const auto &k : it.value()["X"]) { if (const Anim *x = animByKey(k)) { a.X.push_back(x); } } }
					s_armAnims[it.key()] = a;
				}
			}
			if (aj.contains("snd"))
			{
				const auto &s = aj["snd"];
				auto gi = [](const nlohmann::json &j, const char *k) { return j.contains(k) && j[k].is_number_integer() ? j[k].get<int>() : -1; };
				s_sndStep = gi(s, "step"); s_sndServoA = gi(s, "servoA"); s_sndServoB = gi(s, "servoB");
				if (s.contains("parts"))
				{
					for (auto it = s["parts"].begin(); it != s["parts"].end(); ++it)
					{
						PartSounds ps;
						ps.fire = gi(it.value(), "fire"); ps.melee = gi(it.value(), "melee"); ps.destroy = gi(it.value(), "destroy"); ps.move = gi(it.value(), "move");
						s_partSnd[it.key()] = ps;
					}
				}
			}
		}
		else
		{
			debug(LOG_ERROR, "modcombot: mfmodular/anims.json hat nicht das neue Format (v3) - Combots stehen in der Ruhehaltung");
		}
	}
	debug(LOG_INFO, "modcombot: %zu Teile, %zu Animationen, %zu Beinsorten, %zu Waffenarme, gespiegelt %d", s_parts.size(), s_anims.size(),
	      s_tiers.size(), s_armAnims.size(), s_mirrored ? 1 : 0);
	return !s_parts.empty();
}

// ---------------------------------------------------------------- Mathe (3x3, zeilenweise)

struct M3 { float a[3][3]; };

M3 ident() { M3 m{}; m.a[0][0] = m.a[1][1] = m.a[2][2] = 1.f; return m; }

M3 mul(const M3 &x, const M3 &y)
{
	M3 r{};
	for (int i = 0; i < 3; ++i)
	{
		for (int j = 0; j < 3; ++j)
		{
			float s = 0.f;
			for (int k = 0; k < 3; ++k) { s += x.a[i][k] * y.a[k][j]; }
			r.a[i][j] = s;
		}
	}
	return r;
}

M3 transp(const M3 &x)
{
	M3 r{};
	for (int i = 0; i < 3; ++i) { for (int j = 0; j < 3; ++j) { r.a[i][j] = x.a[j][i]; } }
	return r;
}

glm::vec3 mv(const M3 &m, const glm::vec3 &v)
{
	return glm::vec3(m.a[0][0] * v.x + m.a[0][1] * v.y + m.a[0][2] * v.z,
	                 m.a[1][0] * v.x + m.a[1][1] * v.y + m.a[1][2] * v.z,
	                 m.a[2][0] * v.x + m.a[2][1] * v.y + m.a[2][2] * v.z);
}

M3 rotX(float a) { const float c = std::cos(a), s = std::sin(a); M3 m = ident(); m.a[1][1] = c; m.a[1][2] = -s; m.a[2][1] = s; m.a[2][2] = c; return m; }
M3 rotY(float a) { const float c = std::cos(a), s = std::sin(a); M3 m = ident(); m.a[0][0] = c; m.a[0][2] = s; m.a[2][0] = -s; m.a[2][2] = c; return m; }
M3 rotZ(float a) { const float c = std::cos(a), s = std::sin(a); M3 m = ident(); m.a[0][0] = c; m.a[0][1] = -s; m.a[1][0] = s; m.a[1][1] = c; return m; }

// MF CMatrix44::SetRotYXZPos (nachgerechnet): R = Rz(z) * Rx(x) * Ry(y); Winkel in Grad, Bits 0-9 = x, 10-19 = y, 20-29 = z
M3 euler(const float *deg)
{
	const float k = 3.14159265358979f / 180.f;
	return mul(mul(rotZ(deg[2] * k), rotX(deg[0] * k)), rotY(deg[1] * k));
}

float decode10(uint32_t w, int ax)
{
	const int v = static_cast<int>((w >> (10 * ax)) & 1023u);
	return static_cast<float>(v >= 512 ? v - 1024 : v) * (360.f / 1024.f);
}

float wrap180(float d)
{
	d = std::fmod(d + 180.f, 360.f);
	if (d < 0.f) { d += 360.f; }
	return d - 180.f;
}

// Haltung: Becken-Lage + Winkel je Knochenplatz. Ohne Animation = MF-Ruhehaltung ("stand": alle Winkel 0, Becken 21,2 hoch)
struct Pose
{
	glm::vec3 root = glm::vec3(0.f, -0.6f, 21.2f);
	float a[NSLOT][3] = {};
};

void frameAt(const Anim &an, float t, bool loop, int &f0, int &f1, float &fr)
{
	f0 = f1 = 0; fr = 0.f;
	if (an.nf < 2 || an.T <= 0.f) { return; }
	if (loop)
	{
		t = std::fmod(t, an.T);
		if (t < 0.f) { t += an.T; }
	}
	else
	{
		t = std::clamp(t, 0.f, an.T);
	}
	if (an.times.empty())
	{
		const float fp = t / an.T * static_cast<float>(an.nf - 1);
		f0 = std::clamp(static_cast<int>(fp), 0, an.nf - 1);
		f1 = std::min(f0 + 1, an.nf - 1);
		fr = f0 == an.nf - 1 ? 0.f : std::clamp(fp - static_cast<float>(f0), 0.f, 1.f);
	}
	else
	{
		const auto it = std::upper_bound(an.times.begin(), an.times.end(), t);
		f1 = std::clamp(static_cast<int>(it - an.times.begin()), 1, an.nf - 1);
		f0 = f1 - 1;
		const float d = an.times[static_cast<size_t>(f1)] - an.times[static_cast<size_t>(f0)];
		fr = d > 1e-6f ? std::clamp((t - an.times[static_cast<size_t>(f0)]) / d, 0.f, 1.f) : 0.f;
	}
}

// Animation in die Haltung mischen (MF-Animator: Winkel auf dem kuerzesten Weg, je Knochen mit dem Knochengewicht der Animation x k).
// legsOff: Beine/Becken (Plaetze 0-10) nicht anfassen - Angriff im Gehen, die Beine laufen weiter
void applyAnim(Pose &P, const Anim &an, float t, bool loop, float k, bool legsOff = false)
{
	if (k <= 0.f || an.nf < 2) { return; }
	int f0, f1;
	float fr;
	frameAt(an, t, loop, f0, f1, fr);
	const int nb = an.nb();
	for (int i = 0; i < nb; ++i)
	{
		const int sl = an.slots[static_cast<size_t>(i)];
		if (sl < 0 || sl >= NSLOT) { continue; }
		float w = std::min(1.f, an.wt[sl] * k);
		if (legsOff && sl <= 10) { w = 0.f; }
		if (w <= 0.f) { continue; }
		if (an.wt[sl] >= 0.99f) { w = std::min(1.f, k); }   // MF 0.995 = voll
		const uint32_t w0 = an.w[static_cast<size_t>(f0 * nb + i)], w1 = an.w[static_cast<size_t>(f1 * nb + i)];
		for (int ax = 0; ax < 3; ++ax)
		{
			const float v0 = decode10(w0, ax), v1 = decode10(w1, ax);
			const float v = v0 + wrap180(v1 - v0) * fr;
			P.a[sl][ax] += wrap180(v - P.a[sl][ax]) * w;
		}
	}
	if (an.idx[0] >= 0 && !legsOff)
	{
		const glm::vec3 r = an.pos[static_cast<size_t>(f0)] + (an.pos[static_cast<size_t>(f1)] - an.pos[static_cast<size_t>(f0)]) * fr;
		const float w = an.wt[0] >= 0.99f ? std::min(1.f, k) : std::min(1.f, an.wt[0] * k);
		P.root += (r - P.root) * w;
	}
}

const Part *defaultLegs()
{
	auto it = s_partIndex.find("Basic_L0");
	return it == s_partIndex.end() ? nullptr : &s_parts[it->second];
}

// MF (x rechts, y hinten, z hoch - linkshaendig) -> Modell (x rechts, y hoch, z hinten): gespiegelt (x, z, y).
// Alte, ungespiegelte Teile (vor mfmirror.py) mit der frueheren Drehung (x, z, -y).
glm::vec3 toModel(const glm::vec3 &v)
{
	return s_mirrored ? glm::vec3(v.x, v.z, v.y) : glm::vec3(v.x, v.z, -v.y);
}

glm::mat4 boneMatrix(const M3 &R, const glm::vec3 &p)
{
	M3 C{};
	C.a[0][0] = 1.f;
	C.a[1][2] = 1.f;
	C.a[2][1] = s_mirrored ? 1.f : -1.f;
	const M3 RE = mul(mul(C, R), transp(C));
	glm::mat4 g(1.f);
	for (int r = 0; r < 3; ++r) { for (int c = 0; c < 3; ++c) { g[c][r] = RE.a[r][c]; } }
	const glm::vec3 q = toModel(p) * s_scale;
	g[3] = glm::vec4(q, 1.f);
	return g;
}

struct XF
{
	M3 R = ident();
	glm::vec3 p = glm::vec3(0.f);
};

// Weltlage aller Einzelteile eines Bauteils entlang seines MF-Skeletts. slotShift: Arm an der anderen Schulter (Plaetze 13-17 <-> 18-22)
void partTransforms(const Part *part, const XF &root, const Pose &P, int slotShift, std::vector<XF> &out)
{
	static const float zero[3] = { 0.f, 0.f, 0.f };
	const size_t n = part->pieNames.size();
	out.assign(n, root);
	for (size_t i = 0; i < n && i < part->bones.size(); ++i)
	{
		const PieceBone &b = part->bones[i];
		int sl = b.slot;
		if (slotShift != 0 && sl >= 13 && sl <= 22) { sl += slotShift; }
		const float *ang = (sl >= 0 && sl < 28) ? P.a[sl] : zero;
		if (i == 0 || b.parent < 0)
		{
			out[i].p = i == 0 ? root.p : root.p + mv(root.R, b.off);
			out[i].R = mul(root.R, euler(ang));
		}
		else
		{
			const XF &pa = out[static_cast<size_t>(b.parent)];
			out[i].p = pa.p + mv(pa.R, b.off);
			out[i].R = mul(pa.R, euler(ang));
		}
	}
}

float dropHeight(const Part *L)
{
	if (!L) { return 0.f; }
	return std::max(0.f, -(L->hipR.z + L->kneeA.z + L->ankA.z)) * 0.85f;
}

void emitPart(const Part *p, const std::vector<XF> &X, const glm::mat4 &base, const DrawPiece &emit)
{
	for (size_t i = 0; i < X.size(); ++i)
	{
		const iIMDShape *s = p->shape(i);
		if (!s) { continue; }
		glm::mat4 m = base * boneMatrix(X[i].R, X[i].p);
		if (i < p->bones.size())
		{
			const glm::vec3 &rel = p->bones[i].rel;
			if (rel.x != 0.f || rel.y != 0.f || rel.z != 0.f)
			{
				// (mfparts.py) Anbauteil der Arme ist um "rel" in den Rahmen der Hand verschoben gebaut - hier wieder herausnehmen
				m = m * glm::translate(glm::mat4(1.f), -toModel(rel) * s_scale);
			}
		}
		emit(s, m, p);
	}
}

} // namespace

// ---------------------------------------------------------------- Part

const iIMDShape *Part::shape(size_t i) const
{
	// MOD (20.11., Absturz 21:26 im Bau-Menue nach Spielstand laden): die gemerkten Modellzeiger zeigten nach dem Neuladen der Modelle
	// auf freigegebenen Speicher - wie die anderen Zwischenspeicher an der Modell-Generation festmachen
	if (modelCacheStale(m_gen))
	{
		m_shapes.clear();
		for (const auto &n : pieNames)
		{
			iIMDBaseShape *b = modelGet(WzString::fromUtf8(n));
			m_shapes.push_back(b ? b->displayModel() : nullptr);
		}
	}
	return i < m_shapes.size() ? m_shapes[i] : nullptr;
}

bool Part::hasAbility(const char *a) const
{
	for (const auto &s : abil) { if (s == a) { return true; } }
	return false;
}

bool Part::reachOf(const std::string &animKey, float &reachMF, float &impactSec) const
{
	for (const auto &r : reach)
	{
		if (r.first == animKey) { reachMF = r.second.x; impactSec = r.second.y; return true; }
	}
	return false;
}

// ---------------------------------------------------------------- Zugriff

float modelScale()
{
	return s_scale;
}

bool ready()
{
	if (!s_loaded)
	{
		s_loaded = true;
		s_ok = loadAll();
	}
	return s_ok;
}

const std::vector<Part> &parts()
{
	ready();
	return s_parts;
}

const Part *find(const std::string &id)
{
	if (!ready()) { return nullptr; }
	auto it = s_partIndex.find(id);
	return it == s_partIndex.end() ? nullptr : &s_parts[it->second];
}

static const Part *byStatId(const std::string &id, const char *prefix)
{
	const size_t n = std::char_traits<char>::length(prefix);
	if (id.compare(0, n, prefix) != 0) { return nullptr; }
	return find(id.substr(n));
}

const Part *partOfBody(const BODY_STATS *b)
{
	if (b == nullptr || !ready()) { return nullptr; }
	auto it = s_bodyCache.find(b);
	if (it != s_bodyCache.end()) { return it->second; }
	const Part *p = byStatId(b->id.toUtf8(), "MFT_");
	s_bodyCache[b] = p;
	return p;
}

const Part *partOfPropulsion(const PROPULSION_STATS *pr)
{
	if (pr == nullptr || !ready()) { return nullptr; }
	auto it = s_propCache.find(pr);
	if (it != s_propCache.end()) { return it->second; }
	const Part *p = byStatId(pr->id.toUtf8(), "MFL_");
	s_propCache[pr] = p;
	return p;
}

const Part *partOfWeapon(unsigned weaponIndex)
{
	if (weaponIndex == 0 || weaponIndex >= asWeaponStats.size() || !ready()) { return nullptr; }
	auto it = s_weapCache.find(weaponIndex);
	if (it != s_weapCache.end()) { return it->second; }
	const Part *p = byStatId(asWeaponStats[weaponIndex].id.toUtf8(), "MFA_");
	s_weapCache[weaponIndex] = p;
	return p;
}

bool isModular(const DROID *d)
{
	if (d == nullptr) { return false; }
	return partOfBody(d->getBodyStats()) != nullptr;
}

unsigned severedMask(const DROID *d)
{
	if (d == nullptr) { return 0; }
	auto it = s_state.find(d->id);
	return it == s_state.end() ? 0u : it->second.severed;
}

static State &stateOf(const DROID *d)
{
	State &s = s_state[d->id];
	if (s.hpMax[SLOT_TORSO] <= 1 && d->originalBody > 0)
	{
		const int base = static_cast<int>(d->originalBody);
		s.hpMax[SLOT_TORSO] = std::max(1, base);
		s.hpMax[SLOT_RARM] = std::max(1, base * 18 / 100);
		s.hpMax[SLOT_LARM] = std::max(1, base * 18 / 100);
		s.hpMax[SLOT_LEGS] = std::max(1, base * 22 / 100);
		// (21.11.) wie in MF: jedes Teil hat seine eigenen Trefferpunkte (Hammerarm 338, Katana 173, Grund-Torso 179, Grund-Beine 114 ...)
		// - umgerechnet im Verhaeltnis zum Torso, dessen Warzone-Trefferpunkte der Rumpf hat
		const Loadout lo = loadoutOf(d);
		if (lo.torso != nullptr && lo.torso->st.mfhp > 0.f)
		{
			const float f = static_cast<float>(base) / lo.torso->st.mfhp;
			if (lo.armR && lo.armR->st.mfhp > 0.f) { s.hpMax[SLOT_RARM] = std::max(1, static_cast<int>(lo.armR->st.mfhp * f)); }
			if (lo.armL && lo.armL->st.mfhp > 0.f) { s.hpMax[SLOT_LARM] = std::max(1, static_cast<int>(lo.armL->st.mfhp * f)); }
			if (lo.legs && lo.legs->st.mfhp > 0.f) { s.hpMax[SLOT_LEGS] = std::max(1, static_cast<int>(lo.legs->st.mfhp * f)); }
		}
		for (int i = 0; i < SLOT_COUNT; ++i) { s.hp[i] = s.hpMax[i]; }
	}
	return s;
}

Loadout loadoutOf(const DROID *d)
{
	Loadout lo;
	if (d == nullptr) { return lo; }
	lo.torso = partOfBody(d->getBodyStats());
	lo.legs = partOfPropulsion(d->getPropulsionStats());
	lo.severed = severedMask(d);
	for (unsigned i = 0; i < d->numWeaps && i < MAX_WEAPONS; ++i)
	{
		if (d->asWeaps[i].nStat == 0) { continue; }
		const Part *a = partOfWeapon(d->asWeaps[i].nStat);
		if (a == nullptr || a->kind != KIND_ARM) { continue; }
		const bool rightFree = lo.armR == nullptr, leftFree = lo.armL == nullptr;
		if (a->side > 0 && rightFree) { lo.armR = a; lo.slotR = static_cast<int>(i); }
		else if (a->side < 0 && leftFree) { lo.armL = a; lo.slotL = static_cast<int>(i); }
		else if (rightFree) { lo.armR = a; lo.slotR = static_cast<int>(i); }
		else if (leftFree) { lo.armL = a; lo.slotL = static_cast<int>(i); }
	}
	if (lo.severed & SEV_RARM) { lo.armR = nullptr; }
	if (lo.severed & SEV_LARM) { lo.armL = nullptr; }
	return lo;
}

void forget(unsigned droidId)
{
	s_state.erase(droidId);
}

int blockBonus(const DROID *d)
{
	if (!isModular(d)) { return 0; }
	const Loadout lo = loadoutOf(d);
	int b = 0;
	if (lo.armR) { b += lo.armR->st.blk; }
	if (lo.armL) { b += lo.armL->st.blk; }
	if (lo.torso && lo.torso->hasAbility("shield")) { b += 20; }
	return std::min(b, 60);
}

bool hasAbility(const DROID *d, const char *ability)
{
	if (!isModular(d)) { return false; }
	const Loadout lo = loadoutOf(d);
	return (lo.torso && lo.torso->hasAbility(ability)) || (lo.legs && lo.legs->hasAbility(ability))
	       || (lo.armR && lo.armR->hasAbility(ability)) || (lo.armL && lo.armL->hasAbility(ability));
}

int muzzleConnector(const DROID *d, int weaponSlot)
{
	if (d == nullptr || weaponSlot < 0 || weaponSlot >= static_cast<int>(d->numWeaps) || !isModular(d)) { return -1; }
	const Loadout lo = loadoutOf(d);
	if (weaponSlot == lo.slotR) { return 0; }
	if (weaponSlot == lo.slotL) { return 1; }
	// eingebaute Waffe ("MFB_<Teil>"): Torso oben, Beine unten vorn
	const std::string wid = d->getWeaponStats(weaponSlot)->id.toUtf8();
	if (wid.rfind("MFB_", 0) == 0 && lo.legs != nullptr && wid.compare(4, std::string::npos, lo.legs->id) == 0) { return 3; }
	return 2;
}

bool canFly(const DROID *d)
{
	if (d == nullptr) { return false; }
	if (d->droidType == DROID_CYBORG_CONSTRUCT) { return true; }   // (Wunsch 17.11. 20:59) Cyborg-Pioniere mit Sprungduesen
	const Part *t = partOfBody(d->getBodyStats());
	if (t == nullptr) { return false; }
	const Part *l = partOfPropulsion(d->getPropulsionStats());
	return t->hasAbility("fly") || t->hasAbility("jump") || (l != nullptr && (l->hasAbility("jump") || l->hasAbility("fly")));
}

namespace
{
struct Air { float cur = 0.f; unsigned last = 0; unsigned nextPuff = 0; bool want = false; unsigned since = 0; unsigned nextSnd = 0; };   // since: Beginn von Abheben/Landen
std::unordered_map<uint32_t, Air> s_air;
const JetSet &jetSetOf(const DROID *d);

// fliegt, hebt ab oder landet gerade (die Lande-Animation laeuft nach dem Aufsetzen noch am Boden zu Ende)
bool airActive(const DROID *d, const Air &a)
{
	if (a.cur > 0.f || a.want) { return true; }
	if (a.since == 0 || graphicsTime < a.since) { return false; }
	const Anim *land = jetSetOf(d).land;
	return land != nullptr && static_cast<float>(graphicsTime - a.since) < land->T * 1000.f;
}
constexpr float FLY_HEIGHT = 170.f;      // Flughoehe ueber dem Boden

bool overVoid(const DROID *d)
{
	if (!tileOnMap(gameWorld.map, map_coord(d->pos.x), map_coord(d->pos.y))) { return false; }
	const MAPTILE *t = mapTile(gameWorld.map, map_coord(d->pos.x), map_coord(d->pos.y));
	return TileNumber_tile(t->texture) == MOD_TILE_VOID;
}

// (20.11. 21:43 "Die Milagro sprungduesen funktionieren nicht (Pathfinding)"): Flug-Combots planen ihren Weg wie Flieger (modlayer) -
// zu Fuss duerfen sie dann aber nicht durch Klippen, Wasser, Gebaeude oder Graeben "laufen". Ist ein solches Feld unter ihnen oder auf
// dem Weg vor ihnen, heben sie ab und landen erst wieder auf begehbarem Boden.
bool groundBlocked(int wx, int wy)
{
	const int tx = map_coord(wx), ty = map_coord(wy);
	if (!tileOnMap(gameWorld.map, tx, ty)) { return false; }
	return fpathBlockingTile(gameWorld.map, tx, ty, PROPULSION_TYPE_WHEELED);   // so laufen Combots am Boden (modlayer::effectivePropulsion)
}

bool obstacleAhead(const DROID *d)
{
	if (groundBlocked(d->pos.x, d->pos.y)) { return true; }
	const MOVE_CONTROL &m = d->sMove;
	if (m.Status == MOVEINACTIVE || m.Status == MOVEHOVER) { return false; }
	Vector2i from = d->pos.xy();
	int budget = 3 * 128;   // drei Felder voraus
	for (size_t i = static_cast<size_t>(std::max(0, m.pathIndex)); i < m.asPath.size() && budget > 0; ++i)
	{
		const Vector2i to = m.asPath[i];
		const int len = iHypot(to - from);
		const int steps = std::max(1, len / 48);
		for (int s = 1; s <= steps && budget > 0; ++s)
		{
			const Vector2i p = from + (to - from) * s / steps;
			if (groundBlocked(p.x, p.y)) { return true; }
			budget -= std::max(1, len / steps);
		}
		from = to;
	}
	return false;
}

// (20.11. 23:03) Die MF-Fluganimationen enthalten das Ducken vor dem Absprung und das Abfedern nach dem Aufsetzen als tiefe
// Becken-Lage (bis 31 Einheiten unter dem Stand) - MF hebt dazu die Einheit selbst an. Hier: die Fuesse werden beim Zeichnen nie unter
// den Boden gelassen (drawPose), und die Flughoehe ist beim Landen genau dann 0, wenn die Fuesse der Lande-Animation am tiefsten sind
// (Aufsetzen); der Rest der Animation (Abfedern, Aufrichten) spielt am Boden.
float lowestSole(const Part *L, const Anim &an, float t)
{
	Pose P;
	applyAnim(P, an, t, false, 1.f);
	XF root;
	root.p = P.root;
	root.p.z -= L->ground;
	std::vector<XF> X;
	partTransforms(L, root, P, 0, X);
	float lowest = 1e9f;
	for (size_t i = 0; i < X.size() && i < L->bones.size(); ++i)
	{
		if (L->bones[i].slot == 3 || L->bones[i].slot == 8) { lowest = std::min(lowest, X[i].p.z + L->sole); }
	}
	return lowest;
}

float landTouchSeconds(const Anim *land)
{
	static std::unordered_map<const Anim *, float> cache;
	if (land == nullptr) { return 1.5f; }
	const auto it = cache.find(land);
	if (it != cache.end()) { return it->second; }
	float best = land->T;
	const Part *L = defaultLegs();
	if (L != nullptr)
	{
		float lowest = 1e9f;
		for (int i = 0; i <= 60; ++i)
		{
			const float t = land->T * static_cast<float>(i) / 60.f;
			const float z = lowestSole(L, *land, t);
			if (z < lowest - 0.05f) { lowest = z; best = t; }
		}
	}
	best = std::clamp(best, 0.4f, land->T);
	cache[land] = best;
	return best;
}

const JetSet &jetSetOf(const DROID *d)
{
	const Part *t = partOfBody(d->getBodyStats());
	if (t != nullptr && t->hasAbility("fly") && s_jetTorso.fly != nullptr) { return s_jetTorso; }
	return s_jetLegs.fly != nullptr ? s_jetLegs : s_jetTorso;
}

int trackOfSound(int n)
{
	static std::unordered_map<int, int> cache;
	if (n < 0) { return -1; }
	auto it = cache.find(n);
	if (it != cache.end()) { return it->second; }
	char nm[32];
	snprintf(nm, sizeof(nm), "mfs%03d.ogg", n);
	const int id = resPresent("WAV", nm) ? audio_GetTrackID(nm) : -1;   // aelteres Paket ohne diesen Klang: still
	cache[n] = id;
	return id;
}

void playMF(const DROID *d, int n)
{
	if (d == nullptr || n < 0 || !d->visibleForLocalDisplay()) { return; }
	const int id = trackOfSound(n);
	if (id >= 0) { audio_PlayStaticTrack(d->pos.x, d->pos.y, id); }
}
}

float flightOffset(const DROID *d)
{
	if (d == nullptr || d->died || !canFly(d)) { return 0.f; }
	ready();
	bool want = overVoid(d) || obstacleAhead(d);
	if (!want && d->sMove.Status != MOVEINACTIVE && d->sMove.Status != MOVEHOVER)
	{
		const int dist = iHypot(d->sMove.destination - d->pos.xy());
		want = dist > 320;   // weitere Strecke (ueber 2,5 Felder): abheben, kurze Wege werden gelaufen
	}
	Air &a = s_air[d->id];
	const unsigned now = graphicsTime;
	// einmal abgehoben bleibt er mindestens bis zum Ende des Abhebens + kurz in der Luft (kein Zappeln an Hinderniskanten)
	if (!want && a.want && a.since != 0 && now >= a.since && now - a.since < 2800u) { want = true; }
	if (want != a.want) { a.want = want; a.since = now; }
	const float dt = a.last == 0 || now < a.last ? 0.f : static_cast<float>(std::min(now - a.last, 200u));
	a.last = now;
	// MF: Abheben/Landen dauern so lange wie die MF-Animationen (TakeOff 2,0 s, Land 2,3 s)
	const JetSet &js = jetSetOf(d);
	const float riseMs = js.takeoff != nullptr ? js.takeoff->T * 1000.f : 1500.f;
	const float landMs = landTouchSeconds(js.land) * 1000.f;   // am Boden, wenn die Lande-Animation aufsetzt
	a.cur = want ? std::min(1.f, a.cur + dt / riseMs) : std::max(0.f, a.cur - dt / landMs);
	if (a.cur > 0.05f && now >= a.nextPuff)
	{
		// Duesenstrahl unter den Fuessen (nur Anzeige)
		a.nextPuff = now + 110;
		const float h = a.cur * a.cur * (3.f - 2.f * a.cur) * FLY_HEIGHT;
		Vector3i p(d->pos.x + (rand() % 21 - 10), d->pos.z + static_cast<int>(h) - 6, d->pos.y + (rand() % 21 - 10));
		addEffect(&p, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING, false, nullptr, 0);
	}
	if (a.want && a.cur > 0.f && now >= a.nextSnd)
	{
		// MF JetPackTorso: Bewegungs-Klang = Duesenschub
		const auto it = s_partSnd.find("JetPack:torso");
		playMF(d, it != s_partSnd.end() ? it->second.move : -1);
		a.nextSnd = now + 1150;
	}
	if (s_air.size() > 512)
	{
		for (auto it = s_air.begin(); it != s_air.end();) { it = (now - it->second.last > 10000) ? s_air.erase(it) : std::next(it); }
	}
	return a.cur * a.cur * (3.f - 2.f * a.cur) * FLY_HEIGHT;
}

namespace
{
std::unordered_map<uint32_t, unsigned> s_fieldReady;   // Kraftfeld: ab wann wieder bereit (gameTime)
constexpr unsigned FIELD_RECHARGE_MS = 8000;
constexpr int SONAR_RADIUS = 9;                        // Kacheln

bool partHas(const DROID *d, const char *a)
{
	// schnelle Pruefung nur ueber Torso und Beine (zwischengespeicherte Zuordnung Stat -> Teil)
	const Part *t = partOfBody(d->getBodyStats());
	if (t == nullptr) { return false; }
	const Part *l = partOfPropulsion(d->getPropulsionStats());
	return t->hasAbility(a) || (l != nullptr && l->hasAbility(a));
}
}

bool absorbShot(DROID *d)
{
	// MF Kraftfeld-Torso: faengt genau ein Geschoss ab, laedt dann nach (8 s); in der Zeit wie ein Basis-Torso
	if (d == nullptr || d->died || !isModular(d) || !partHas(d, "forcefield")) { return false; }
	unsigned &ready = s_fieldReady[d->id];
	if (gameTime < ready) { return false; }
	ready = gameTime + FIELD_RECHARGE_MS;
	Vector3i p(d->pos.x, d->pos.z + 40, d->pos.y);
	effectGiveAuxVar(60);
	addEffect(&p, EFFECT_EXPLOSION, EXPLOSION_TYPE_PLASMA, false, nullptr, 0, gameTime - deltaGameTime + 1);
	return true;
}

int sensorPercent(const DROID *d)
{
	// MF Recon-Teil: deutlich weitere Sicht
	return (d != nullptr && isModular(d) && partHas(d, "recon")) ? 160 : 100;
}

int meleePercent(const DROID *attacker)
{
	// MF HTH-Upgrade-Beine: +50 % Nahkampfschaden
	return (attacker != nullptr && isModular(attacker) && partHas(attacker, "hth")) ? 150 : 100;
}

int severPercent(const DROID *attacker)
{
	// MF Steady-Beine: stehen fester - Treffer auf Teile wirken staerker (mehr Abschlaege)
	return (attacker != nullptr && isModular(attacker) && partHas(attacker, "steady")) ? 150 : 100;
}

void updateAbilities(DROID *d)
{
	if (d == nullptr || d->died || !isModular(d)) { return; }
	const bool secondTick = gameTime / 1000 != (gameTime - deltaGameTime) / 1000;
	// Selbstreparatur: jede Sekunde 2% der Trefferpunkte
	if (secondTick && d->body < d->originalBody && hasAbility(d, "repair"))
	{
		d->body = std::min<UDWORD>(d->originalBody, d->body + std::max<UDWORD>(1, d->originalBody / 50));
	}
	// MF Sonar-Beine: lichten den Schleier im Untergrund (alle 2 s, Umkreis 9 Kacheln)
	if (secondTick && (gameTime / 1000) % 2 == 0 && d->player < MAX_PLAYER_SLOTS && modlayer::layerOf(d) == modlayer::UNDERGROUND && partHas(d, "sonar"))
	{
		modlayermap::Scope sc(modlayer::UNDERGROUND);
		const int tx = map_coord(d->pos.x), ty = map_coord(d->pos.y);
		for (int dy = -SONAR_RADIUS; dy <= SONAR_RADIUS; ++dy)
		{
			for (int dx = -SONAR_RADIUS; dx <= SONAR_RADIUS; ++dx)
			{
				if (dx * dx + dy * dy > SONAR_RADIUS * SONAR_RADIUS || !tileOnMap(gameWorld.map, tx + dx, ty + dy)) { continue; }
				SET_TILE_VISIBLE(d->player, mapTile(gameWorld.map, tx + dx, ty + dy));
			}
		}
	}
	if (s_fieldReady.size() > 1024)
	{
		for (auto it = s_fieldReady.begin(); it != s_fieldReady.end();) { it = (gameTime > it->second + 60000) ? s_fieldReady.erase(it) : std::next(it); }
	}
}

static void spawnSeveredFeature(const DROID *d, const Part *p)
{
	if (p == nullptr || p->feature.empty()) { return; }
	FEATURE_STATS *fs = nullptr;
	for (auto &s : asFeatureStats)
	{
		if (s.id.toUtf8() == p->feature) { fs = &s; break; }
	}
	if (fs == nullptr || fs->psImd == nullptr) { return; }
	// (21.11., Log "Salvage ... already placed ... - removing it"): ein freies Feld in der Naehe suchen, sonst verdraengt das neue Teil
	// ein schon liegendes
	int x = -1, y = -1;
	for (int attempt = 0; attempt < 12; ++attempt)
	{
		const int r = 64 + attempt * 24;
		const int cx = d->pos.x + static_cast<int>(gameRand(static_cast<uint32_t>(2 * r + 1))) - r;
		const int cy = d->pos.y + static_cast<int>(gameRand(static_cast<uint32_t>(2 * r + 1))) - r;
		if (cx < 0 || cy < 0 || !tileOnMap(gameWorld.map, map_coord(cx), map_coord(cy))) { continue; }
		const MAPTILE *mt = mapTile(gameWorld.map, map_coord(cx), map_coord(cy));
		if (mt->psObject != nullptr) { continue; }   // dort liegt schon etwas (Teil, Wrack, Bauwerk)
		x = cx; y = cy;
		break;
	}
	if (x < 0) { return; }
	FEATURE *f = buildFeature(gameWorld, fs, static_cast<UDWORD>(x), static_cast<UDWORD>(y), false);
	if (f != nullptr) { f->rot.direction = gameRand(DEG(360)); }
}

void severPart(DROID *d, unsigned bit)
{
	if (d == nullptr || !isModular(d)) { return; }
	Loadout lo = loadoutOf(d);
	if (lo.severed & bit) { return; }
	const Part *gone = nullptr;
	int slot = -1;
	if (bit == SEV_RARM) { gone = lo.armR; slot = lo.slotR; }
	else if (bit == SEV_LARM) { gone = lo.armL; slot = lo.slotL; }
	else if (bit == SEV_LEGS) { gone = lo.legs; }
	stateOf(d).severed = lo.severed | bit;
	if (bit == SEV_LEGS) { d->baseSpeed = std::min<UDWORD>(d->baseSpeed, 14); }   // ohne Beine nur noch Kriechtempo
	if (slot >= 0 && static_cast<unsigned>(slot) < MAX_WEAPONS)
	{
		d->asWeaps[slot].ammo = 0;
		d->asWeaps[slot].nStat = 0;
	}
	spawnSeveredFeature(d, gone);
	if (gone != nullptr)
	{
		// MF: Zerstoerungs-Klang des Teils (UnitParams +0xa4)
		const auto it = s_partSnd.find(gone->animFile + (bit == SEV_LEGS ? ":leg" : ":arm"));
		if (it != s_partSnd.end()) { playMF(d, it->second.destroy); }
	}
}

int partHealth(const DROID *d, PartSlot slot)
{
	if (d == nullptr || !isModular(d)) { return -1; }
	const Loadout lo = loadoutOf(d);
	if (slot == SLOT_TORSO)
	{
		return d->originalBody > 0 ? static_cast<int>(std::min<UDWORD>(100, d->body * 100 / d->originalBody)) : 0;
	}
	const bool present = slot == SLOT_RARM ? lo.armR != nullptr : (slot == SLOT_LARM ? lo.armL != nullptr : (lo.legs != nullptr && !(lo.severed & SEV_LEGS)));
	if (!present) { return -1; }
	const State &s = stateOf(d);
	return std::max(0, std::min(100, s.hp[slot] * 100 / std::max(1, s.hpMax[slot])));
}

const Part *partInSlot(const DROID *d, PartSlot slot)
{
	if (d == nullptr || !isModular(d)) { return nullptr; }
	const Loadout lo = loadoutOf(d);
	switch (slot)
	{
	case SLOT_TORSO: return lo.torso;
	case SLOT_RARM: return lo.armR;
	case SLOT_LARM: return lo.armL;
	case SLOT_LEGS: return (lo.severed & SEV_LEGS) ? nullptr : lo.legs;
	default: return nullptr;
	}
}

int hitRandomPart(DROID *d, int damage)
{
	if (d == nullptr || d->died || !isModular(d) || damage <= 0) { return -1; }
	State &s = stateOf(d);
	int w[SLOT_COUNT];
	w[SLOT_TORSO] = 40;
	w[SLOT_RARM] = partInSlot(d, SLOT_RARM) ? 20 : 0;
	w[SLOT_LARM] = partInSlot(d, SLOT_LARM) ? 20 : 0;
	w[SLOT_LEGS] = partInSlot(d, SLOT_LEGS) ? 20 : 0;
	const int total = w[0] + w[1] + w[2] + w[3];
	int roll = static_cast<int>(gameRand(static_cast<uint32_t>(total)));
	int slot = SLOT_TORSO;
	for (int i = 0; i < SLOT_COUNT; ++i)
	{
		if (roll < w[i]) { slot = i; break; }
		roll -= w[i];
	}
	if (slot != SLOT_TORSO)
	{
		s.hp[slot] -= damage;
		if (s.hp[slot] <= 0)
		{
			s.hp[slot] = 0;
			severPart(d, slot == SLOT_RARM ? SEV_RARM : (slot == SLOT_LARM ? SEV_LARM : SEV_LEGS));
		}
	}
	return slot;
}

// (21.11.) MF-Nahkampftreffer: Teil waehlen, MF-Schaden abzueglich der MF-Schilde dieses Teils
int meleeHit(DROID *d, float mD, float eD, int severPct, int &slotOut, int forceSlot)
{
	slotOut = -1;
	if (d == nullptr || d->died || !isModular(d)) { return 0; }
	State &s = stateOf(d);
	const Loadout lo = loadoutOf(d);
	int w[SLOT_COUNT];
	w[SLOT_TORSO] = 40;
	w[SLOT_RARM] = partInSlot(d, SLOT_RARM) ? 20 : 0;
	w[SLOT_LARM] = partInSlot(d, SLOT_LARM) ? 20 : 0;
	w[SLOT_LEGS] = partInSlot(d, SLOT_LEGS) ? 20 : 0;
	int roll = static_cast<int>(gameRand(static_cast<uint32_t>(w[0] + w[1] + w[2] + w[3])));
	int slot = SLOT_TORSO;
	for (int i = 0; i < SLOT_COUNT; ++i)
	{
		if (roll < w[i]) { slot = i; break; }
		roll -= w[i];
	}
	if (forceSlot >= 0 && forceSlot < SLOT_COUNT && w[forceSlot] > 0) { slot = forceSlot; }   // Schild faengt den Schlag ab
	slotOut = slot;
	const Part *hit = slot == SLOT_TORSO ? lo.torso : (slot == SLOT_RARM ? lo.armR : (slot == SLOT_LARM ? lo.armL : lo.legs));
	const float ms = hit ? hit->st.ms : 0.f, es = hit ? hit->st.es : 0.f;
	float dmgMF = std::max(0.f, mD) * (1.f - ms) + std::max(0.f, eD) * (1.f - es);
	// MF-Trefferpunkte -> Warzone-Trefferpunkte (der Rumpf traegt die des Torsos)
	const float f = (lo.torso != nullptr && lo.torso->st.mfhp > 0.f) ? static_cast<float>(d->originalBody) / lo.torso->st.mfhp : 1.f;
	if (slot == SLOT_TORSO) { return std::max(1, static_cast<int>(std::lround(dmgMF * f))); }
	dmgMF = dmgMF * static_cast<float>(std::max(0, severPct)) / 100.f;
	s.hp[slot] -= std::max(1, static_cast<int>(std::lround(dmgMF * f)));
	if (s.hp[slot] <= 0)
	{
		s.hp[slot] = 0;
		severPart(d, slot == SLOT_RARM ? SEV_RARM : (slot == SLOT_LARM ? SEV_LARM : SEV_LEGS));
	}
	return 0;
}

const Part *armInSlot(const DROID *d, int weaponSlot)
{
	if (d == nullptr || weaponSlot < 0 || !isModular(d)) { return nullptr; }
	const Loadout lo = loadoutOf(d);
	if (weaponSlot == lo.slotR) { return lo.armR; }
	if (weaponSlot == lo.slotL) { return lo.armL; }
	return nullptr;
}

float worldPerMF()
{
	return s_scale * s_show;
}

float frontMF()
{
	return s_front;
}

WeaponMode weaponMode(const DROID *d)
{
	if (d == nullptr) { return MODE_ALL; }
	auto it = s_state.find(d->id);
	return it == s_state.end() ? MODE_ALL : it->second.mode;
}

void setWeaponMode(DROID *d, WeaponMode m)
{
	if (d == nullptr || !isModular(d)) { return; }
	stateOf(d).mode = m;
}

void enforceWeaponMode(DROID *d)
{
	if (d == nullptr || d->died || !isModular(d)) { return; }
	const WeaponMode m = weaponMode(d);
	if (m == MODE_ALL) { return; }
	for (unsigned i = 0; i < d->numWeaps && i < MAX_WEAPONS; ++i)
	{
		if (d->asWeaps[i].nStat == 0) { continue; }
		const Part *a = partOfWeapon(d->asWeaps[i].nStat);
		if (a == nullptr) { continue; }
		if ((m == MODE_MELEE && !a->st.melee) || (m == MODE_RANGED && a->st.melee))
		{
			d->asWeaps[i].lastFired = gameTime;   // gesperrte Waffe feuert nicht
		}
	}
}

bool hasMeleeArm(const DROID *d)
{
	if (d == nullptr || !isModular(d)) { return false; }
	const Loadout lo = loadoutOf(d);
	return (lo.armR && lo.armR->st.melee) || (lo.armL && lo.armL->st.melee);
}

std::string weaponLabel(const DROID *d, PartSlot arm)
{
	const Part *p = partInSlot(d, arm);
	if (p == nullptr) { return std::string(); }
	std::string n = p->name;
	// "Axe Arm R" -> "Axe"
	const size_t k = n.find(" Arm");
	if (k != std::string::npos) { n = n.substr(0, k); }
	return n;
}

// ---------------------------------------------------------------- Zeichnen

// MOD (Wunsch 16.11. 21:41): leuchtendes Auge - nur beim Zeichnen lebender Combots im Spiel (nicht Designer/Leichen) mitgeben
static bool s_emitGlow = false;
static const Part s_fxBillboard;   // Kennung fuer den Zeichner: Lichthof zur Kamera gedreht
static const Part s_fxAttached;    // Kennung: leuchtender Anbau, mit dem Knochen bewegt

static const iIMDShape *glowShape(const std::string &name)
{
	static std::map<std::string, const iIMDShape *> cache;
	static unsigned cacheGen = 0;
	if (modelCacheStale(cacheGen)) { cache.clear(); }   // MOD (18.11.): Modelle wurden neu geladen
	auto it = cache.find(name);
	if (it != cache.end()) { return it->second; }
	const iIMDBaseShape *b = name.empty() ? nullptr : modelGet(WzString::fromUtf8(name));
	const iIMDShape *sh = b ? b->displayModel() : nullptr;
	cache[name] = sh;
	return sh;
}

void setEmitEffects(bool on)
{
	s_emitGlow = on;
}

bool drawEffectPiece(const iIMDShape *s, const glm::mat4 &m, const glm::mat4 &view, const Part *p, unsigned seed)
{
	if (p == &s_fxAttached)
	{
		// (Wunsch 19.11. 23:00 "Energieschilde sehe ich nicht ... Supreme-Commander-Schild, nur viel sichtbarer, deutlich blauer"):
		// leuchtender Anbau mit dem Arm: zuerst eine durchscheinende blaue Flaeche (deckt auch auf hellem Boden), dann das Leuchten
		// additiv darueber; beides pulsiert leicht und flackert wie ein Energiefeld
		const float t = static_cast<float>(graphicsTime % 600000u) * 0.001f;
		const float ph = static_cast<float>(seed % 53u);
		const float pulse = std::sin(t * 3.7f + ph) * 0.6f + std::sin(t * 11.3f + ph * 0.7f) * 0.25f;
		const int base = std::clamp(static_cast<int>(150.f + 25.f * pulse), 0, 255);
		const int glow = std::clamp(static_cast<int>(215.f + 40.f * pulse), 0, 255);
		pie_Draw3DShape(s, 0, 0, WZCOL_WHITE, pie_TRANSLUCENT, base, m, view, 0.f, true);
		pie_Draw3DShape(s, 0, 0, WZCOL_WHITE, pie_ADDITIVE, glow, m, view, 0.f, true);
		return true;
	}
	if (p == &s_fxBillboard)
	{
		// Lichthof des Auges: zur Kamera gedreht, langsam drehende kurze Strahlen, leichtes Flackern, additiv - nicht zu hell
		const float t = static_cast<float>(graphicsTime % 600000u) * 0.001f;
		const float ph = static_cast<float>(seed % 97u);
		const float k = 1.f + 0.07f * std::sin(t * 7.3f + ph) + 0.04f * std::sin(t * 17.1f + ph * 0.5f);
		const float sc = glm::length(glm::vec3(m[0])) * k;
		const glm::mat3 camRot(view);
		const glm::mat4 bb = glm::translate(glm::mat4(1.f), glm::vec3(m[3])) * glm::mat4(glm::transpose(camRot))
		                     * glm::rotate(glm::mat4(1.f), t * 0.35f + ph, glm::vec3(0.f, 0.f, 1.f)) * glm::scale(glm::mat4(1.f), glm::vec3(sc));
		const int alpha = std::clamp(static_cast<int>(165.f + 35.f * std::sin(t * 5.1f + ph)), 0, 255);
		pie_Draw3DShape(s, 0, 0, WZCOL_WHITE, pie_ADDITIVE, alpha, bb, view, 0.f, true);
		return true;
	}
	return false;
}

// ---------------------------------------------------------------- Zusammenbau zeichnen

namespace
{
// (20.11.) MF-Beine mit eingebauter Waffe (Laser-, PowerPulse-, DrunkMissile-Beine u. a.) stecken als Waffe im Arm-Platz:
// sie ersetzen die Beine der Vorlage (sonst hingen zwei Beine an der Schulter)
const Part *visualLegs(const Loadout &lo)
{
	if (lo.armR && lo.armR->legWeapon) { return lo.armR; }
	if (lo.armL && lo.armL->legWeapon) { return lo.armL; }
	return lo.legs;
}

// Zusammenbau in einer Haltung: Beine (Becken = Wurzel), Torso am Beckenhaken, Arme an den Schultern - jedes Einzelteil an seinem
// MF-Gelenk (parts.json "bones")
void drawPose(const Loadout &lo, const Pose &P, const glm::mat4 &base, const DrawPiece &emit, bool clampFeet = false, float floorZ = 0.f)
{
	const Part *VL = visualLegs(lo);
	const Part *L = VL ? VL : defaultLegs();
	if (!L || !lo.torso) { return; }
	const bool noLegs = VL == nullptr || (lo.severed & SEV_LEGS) != 0;
	XF root;
	root.p = P.root;
	root.p.z -= L->ground;
	if (noLegs) { root.p.z -= dropHeight(L); }
	std::vector<XF> legsX;
	partTransforms(L, root, P, 0, legsX);
	if (legsX.empty()) { return; }
	if (clampFeet && !noLegs)
	{
		// Flug (Abheben/Landen): der Boden liegt floorZ (MF-Einheiten) unter dem Ursprung - die Fuesse bleiben darueber
		float lowest = 1e9f;
		for (size_t i = 0; i < legsX.size() && i < L->bones.size(); ++i)
		{
			if (L->bones[i].slot == 3 || L->bones[i].slot == 8) { lowest = std::min(lowest, legsX[i].p.z + L->sole); }
		}
		if (lowest < 1e8f && lowest < floorZ)
		{
			root.p.z += floorZ - lowest;
			partTransforms(L, root, P, 0, legsX);
		}
	}
	const DrawPiece em = [&](const iIMDShape *s, const glm::mat4 &m, const Part *p) { emit(s, m, p); };
	if (!noLegs) { emitPart(L, legsX, base, em); }
	XF tr;
	tr.R = legsX[0].R;
	tr.p = legsX[0].p + mv(legsX[0].R, L->mount);
	std::vector<XF> torsoX;
	partTransforms(lo.torso, tr, P, 0, torsoX);
	if (torsoX.empty()) { return; }
	emitPart(lo.torso, torsoX, base, em);
	if (s_emitGlow && lo.torso->eyePiece >= 0 && static_cast<size_t>(lo.torso->eyePiece) < torsoX.size())
	{
		const iIMDShape *g = glowShape(lo.torso->eyeGlow);
		if (g != nullptr)
		{
			const XF &x = torsoX[static_cast<size_t>(lo.torso->eyePiece)];
			emit(g, base * boneMatrix(x.R, x.p) * glm::translate(glm::mat4(1.f), lo.torso->eyePos), &s_fxBillboard);   // Lichthof (siehe draw)
		}
	}
	for (int s = 0; s < 2; ++s)
	{
		const Part *A = s == 0 ? lo.armR : lo.armL;
		if (!A || A->legWeapon) { continue; }   // Beine mit Waffe: oben als Beine gezeichnet
		XF ar;
		ar.R = torsoX[0].R;
		ar.p = torsoX[0].p + mv(torsoX[0].R, s == 0 ? lo.torso->shR : lo.torso->shL);
		// Arm an der "falschen" Schulter (Loadout-Ausweichplatz): die Knochenplaetze der Schulter nehmen
		const int partBase = (!A->bones.empty() && A->bones[0].slot >= 18 && A->bones[0].slot <= 22) ? 18 : 13;
		const int wantBase = s == 0 ? 13 : 18;
		std::vector<XF> armX;
		partTransforms(A, ar, P, wantBase - partBase, armX);
		emitPart(A, armX, base, em);
		if (s_emitGlow && A->fxPiece >= 0 && static_cast<size_t>(A->fxPiece) < armX.size())
		{
			const iIMDShape *g = glowShape(A->fxModel);
			const XF &x = armX[static_cast<size_t>(A->fxPiece)];
			if (g != nullptr) { emit(g, base * boneMatrix(x.R, x.p) * glm::translate(glm::mat4(1.f), A->fxPos), &s_fxAttached); }
		}
	}
}

const Tiers *tiersOf(const Part *legs)
{
	if (legs == nullptr) { return nullptr; }
	auto it = s_tiers.find(legs->animFile);
	if (it == s_tiers.end()) { it = s_tiers.find("Basic"); }
	return it == s_tiers.end() ? nullptr : &it->second;
}

const std::vector<const Anim *> *attacksOf(const Part *arm, bool right)
{
	if (arm == nullptr) { return nullptr; }
	// MF: jeder Waffenarm hat eigene Angriffe je Seite (rechts: Plaetze 13-17, links 18-22); fehlt die Seite, die allgemeinen
	// Angriffe derselben Seite (animoverlay.tbd) - nie die der anderen Seite (die bewegt den anderen Arm)
	auto it = s_armAnims.find(arm->animFile);
	if (it != s_armAnims.end())
	{
		const auto &v = right ? it->second.R : it->second.L;
		if (!v.empty()) { return &v; }
	}
	it = s_armAnims.find("animoverlay");
	if (it == s_armAnims.end()) { return nullptr; }
	const auto &v = right ? it->second.R : it->second.L;
	return v.empty() ? nullptr : &v;
}
} // namespace

void drawLoadout(const Loadout &lo, AnimKind kind, unsigned elapsedMs, int dieVariant, const glm::mat4 &base, const DrawPiece &emit)
{
	if (!ready() || !lo.torso) { return; }
	Pose P;
	const float t = static_cast<float>(elapsedMs) * 0.001f;
	const JetSet &js = (lo.torso->hasAbility("fly") && s_jetTorso.fly) ? s_jetTorso : s_jetLegs;
	switch (kind)
	{
	case ANIM_WALK:
		if (const Tiers *ti = tiersOf(visualLegs(lo))) { if (ti->walk[1]) { applyAnim(P, *ti->walk[1], t, true, 1.f); } }
		break;
	case ANIM_FIRE:
		if (const auto *v = attacksOf(lo.armR ? lo.armR : lo.armL, lo.armR != nullptr))
		{
			if (s_attackIdle) { applyAnim(P, *s_attackIdle, t, true, 1.f); }
			applyAnim(P, *(*v)[0], t, true, 1.f);
		}
		break;
	case ANIM_DIE:
		if (const Anim *a = s_death[(dieVariant % 5 + 5) % 5]) { applyAnim(P, *a, t, false, 1.f); }
		break;
	case ANIM_FLY_UP: if (js.takeoff) { applyAnim(P, *js.takeoff, t, false, 1.f); } break;
	case ANIM_FLY: if (js.fly) { applyAnim(P, *js.fly, t, true, 1.f); } break;
	case ANIM_FLY_DOWN: if (js.land) { applyAnim(P, *js.land, t, false, 1.f); } break;
	default:
		if (s_stand) { applyAnim(P, *s_stand, t, true, 1.f); }
		break;
	}
	drawPose(lo, P, base, emit);
}

namespace
{
// ---------------------------------------------------------------- Zustand je Combot (nur Anzeige, wie der MF-Animator)
// MF CRobot-Beine (mfatigue.exe 0x4a23b0/0x4a27c0): Gehen = Gang-Stufe nach Hoechsttempo, Abspielrate = Tempo / Stufentempo,
// unter 15 % Tempo Kampfhaltung; Drehen am Platz = "standturn"; stehend im Kampf = "attackidlestill", sonst "stand".
// Angriffe als zweite Schicht (Knochengewichte der Animation), je Schuss einmal von vorn; Ueberblenden mit der MF-Zeit der Animation.
struct Track
{
	const Anim *an = nullptr;
	float t = 0.f;
	bool loop = true;
};

struct DroidAnim
{
	unsigned lastGfx = 0;
	unsigned lastGame = 0;
	Vector2i lastPos = Vector2i(0, 0);
	uint16_t lastDir = 0;
	float speed = 0.f;            // Welteinheiten / s (aus der Bewegung gemessen)
	float turn = 0.f;             // Grad / s
	Track cur, prev;
	float fade = 1.f, fadeDur = 0.2f;
	Track over;
	float overFade = 0.f;
	float overIn = 0.2f;          // Einblendzeit (s) der laufenden Angriffs-Animation
	float overFrom = 0.f;         // Zeitpunkt in der Animation, ab dem eingeblendet wird
	bool overStarted = false;
	Track overOld;                // abgeloeste Angriffs-Animation blendet weich aus (MF: mehrere Angriffs-Schichten)
	float overOldFade = 0.f;
	const Part *overArm = nullptr;
	unsigned lastFired[MAX_WEAPONS] = {};
	unsigned lastSwing = 0;
	unsigned combatUntil = 0;
	unsigned shots = 0;
	unsigned lastBody = 0;
	unsigned nextFlinch = 0;
	unsigned lastParry = 0;       // (21.11.) zuletzt gezeigte Parade (Spielzeit des abgewehrten Schlags)
	float overCut = -1.f;         // (21.11.) Parade: ab dieser Zeit der Animation ausblenden (kein Nachschwung), -1 = ganz abspielen
};
std::unordered_map<uint32_t, DroidAnim> s_da;

void setBase(DroidAnim &da, const Anim *an, bool loop, bool keepPhase = false)
{
	if (an == nullptr || da.cur.an == an) { return; }
	da.prev = da.cur;
	const float ph = (keepPhase && da.cur.an && da.cur.an->T > 0.f) ? std::fmod(da.cur.t, da.cur.an->T) / da.cur.an->T : 0.f;
	da.cur.an = an;
	da.cur.t = ph * an->T;
	da.cur.loop = loop;
	da.fade = da.prev.an ? 0.f : 1.f;
	da.fadeDur = std::max(0.08f, an->blendIn);
}

// Ereignisse einer Animation zwischen t0 und t1 (mit Schleife) -> MF-Klaenge. Die Bits bedeuten je Schicht etwas anderes (mfatigue.exe):
// Grund-Schicht (Beine, CRobot +0xf0): 0x10 = Fuss setzt auf -> Schritt-Klang. Angriffs-Schicht (Arm): 0x1..0x10 = Schuss (macht hier
// die Waffe selbst), 0x20 / 0x40 = Servo-Klaenge (Robot +0xf4 / +0xf8), 0x200 = Treffer-Moment (Arm UnitParams +0x9c), 0x40000000 /
// 0x80000000 = Ausblenden / Ende.
void events(const DROID *d, const Anim *an, float t0, float t1, bool loop, bool base, const Part *arm)
{
	if (an == nullptr || an->ev.empty() || t1 <= t0 || an->T <= 0.f) { return; }
	auto fire = [&](uint32_t bits) {
		if (base)
		{
			if ((bits & 0x10) != 0) { playMF(d, s_sndStep); }
			return;
		}
		if ((bits & 0x20) != 0) { playMF(d, s_sndServoA); }
		if ((bits & 0x40) != 0) { playMF(d, s_sndServoB); }
		if ((bits & 0x200) != 0 && arm != nullptr)
		{
			const auto it = s_partSnd.find(arm->animFile + ":arm");
			int n = it != s_partSnd.end() ? it->second.melee : -1;
			if (n < 0 && arm->st.melee) { n = (arm->st.dmg >= 110) ? 104 : 103; }   // MF ohne eigenen Treffer-Klang: Klinge / schwerer Schlag
			playMF(d, n);
		}
	};
	float pos = loop ? std::fmod(t0, an->T) : t0;
	float remain = t1 - t0;
	bool fromStart = t0 <= 0.f;   // Ereignis genau bei 0 gehoert zum ersten Bild
	for (int lap = 0; lap < 4 && remain > 0.f; ++lap)
	{
		const float end = std::min(an->T, pos + remain);
		for (const auto &ev : an->ev)
		{
			if ((fromStart ? ev.first >= pos : ev.first > pos) && ev.first <= end) { fire(ev.second); }
		}
		remain -= end - pos;
		if (!loop || end < an->T) { break; }
		pos = 0.f;
		fromStart = true;    // neue Runde: Ereignisse bei 0 wieder ausloesen
	}
}

bool inCombat(const DROID *d)
{
	switch (d->action)
	{
	case DACTION_ATTACK: case DACTION_ROTATETOATTACK: case DACTION_MOVETOATTACK: case DACTION_VTOLATTACK:
		return true;
	default:
		return false;
	}
}

// eine Haltung fuer diesen Combot ausrechnen (Grund-Schicht + Angriff), Zeiten fortschreiben, Klaenge ausloesen
void animate(const DROID *d, const Loadout &lo, Pose &P)
{
	DroidAnim &da = s_da[d->id];
	const unsigned now = graphicsTime;
	float dt = (da.lastGfx == 0 || now < da.lastGfx) ? 0.f : static_cast<float>(std::min(now - da.lastGfx, 250u)) * 0.001f;
	const bool newFrame = now != da.lastGfx;
	da.lastGfx = now;
	if (!newFrame) { dt = 0.f; }

	// Bewegung messen (Spielzeit-Schritte): echtes Tempo und Drehrate - auch Schieben und Rutschen zaehlt, damit nie etwas gleitet
	if (gameTime != da.lastGame)
	{
		const float dtg = da.lastGame == 0 ? 0.f : static_cast<float>(gameTime - da.lastGame) * 0.001f;
		if (dtg > 0.f && dtg < 1.f)
		{
			const float v = std::min(static_cast<float>(iHypot(d->pos.xy() - da.lastPos)) / dtg, std::max(50.f, static_cast<float>(d->baseSpeed) * 3.f));
			const float tr = std::fabs(static_cast<float>(static_cast<int16_t>(d->rot.direction - da.lastDir))) * 360.f / 65536.f / dtg;
			da.speed = da.speed * 0.45f + v * 0.55f;
			da.turn = da.turn * 0.45f + tr * 0.55f;
		}
		da.lastGame = gameTime;
		da.lastPos = d->pos.xy();
		da.lastDir = d->rot.direction;
	}

	const float K = s_scale * s_show;                 // Welteinheiten je MF-Einheit
	const float vmf = da.speed / std::max(0.01f, K);
	const Tiers *ti = tiersOf(visualLegs(lo));
	int tier = 3;
	float tierThr = 22.f;
	if (ti != nullptr)
	{
		const float S = static_cast<float>(d->baseSpeed) / std::max(0.01f, K);   // Hoechsttempo in MF-Einheiten
		tier = 0;
		while (tier < 3 && S > ti->thr[tier]) { ++tier; }
		tierThr = ti->thr[tier];
	}

	// Angriffs-Ausloeser: neuer Schuss einer Waffe, oder neuer Schlag im Duell (modduel setzt FIRING mit neuer Startzeit)
	int trigArm = -1;   // 0 rechts, 1 links
	bool trigSwing = false;   // Duell-Schlag (Treffer-Moment abstimmen) statt Schuss
	const Part *builtinPart = nullptr;   // Schuss der eingebauten Waffe: dieses Teil (Torso oder Beine)
	for (unsigned i = 0; i < d->numWeaps && i < MAX_WEAPONS; ++i)
	{
		const unsigned lf = d->asWeaps[i].lastFired;
		// (21.11.) im Brawl stempelt modduel die Nahkampfwaffen jeden Schritt (Warzone soll nicht feuern) - das ist kein Schuss; die
		// Schlaege kommen allein ueber den Brawl (FIRING mit neuer Startzeit). Vorher: Dauerschleife von Hieben in die Luft.
		if (modduel::meleeSuppressed(d, static_cast<int>(i))) { da.lastFired[i] = lf; continue; }
		if (lf != 0 && lf != da.lastFired[i] && lf + 400 >= gameTime)
		{
			da.combatUntil = gameTime + 3000;   // jede Waffe (auch die Beinwaffe ohne Arm): Kampfhaltung
			const Part *wa = static_cast<int>(i) == lo.slotR ? lo.armR : (static_cast<int>(i) == lo.slotL ? lo.armL : nullptr);
			if (wa != nullptr && !wa->legWeapon) { trigArm = static_cast<int>(i) == lo.slotR ? 0 : 1; }
			else if (wa == nullptr)
			{
				// eingebaute Waffe von Torso oder Beinen (Stat "MFB_<Teil>"): welches Teil schiesst?
				const std::string wid = d->getWeaponStats(static_cast<int>(i))->id.toUtf8();
				if (wid.rfind("MFB_", 0) == 0)
				{
					const std::string pid = wid.substr(4);
					const Part *vl = visualLegs(lo);
					builtinPart = (lo.torso != nullptr && lo.torso->id == pid) ? lo.torso : ((vl != nullptr && vl->id == pid) ? vl : nullptr);
				}
			}
		}
		da.lastFired[i] = lf;
	}
	if (d->animationEvent == ANIM_EVENT_FIRING && d->timeAnimationStarted != da.lastSwing)
	{
		// nur echte Duell-Schlaege (modduel); FIRING ohne Duell heisst nur "steht und kaempft" - das ist die Kampfhaltung, kein Schlag
		da.lastSwing = d->timeAnimationStarted;
		const int ss = modduel::swingSlot(d);
		const Part *sa = ss < 0 ? nullptr : (ss == lo.slotL ? lo.armL : lo.armR);
		if (sa != nullptr && !sa->legWeapon)
		{
			trigSwing = true;
			trigArm = ss == lo.slotL ? 1 : 0;
		}
	}
	const Air *air = nullptr;
	{
		const auto it = s_air.find(d->id);
		if (it != s_air.end() && airActive(d, it->second)) { air = &it->second; }
	}

	// ---- Grund-Schicht waehlen
	float baseRate = 1.f;
	bool baseClock = true;   // false: Abheben/Landen laufen nach der Flug-Uhr (flightOffset)
	const bool moving = vmf >= 0.15f * tierThr;
	if (air != nullptr)
	{
		const JetSet &js = jetSetOf(d);
		const float since = now > air->since ? static_cast<float>(now - air->since) * 0.001f : 0.f;
		if (air->want && air->cur < 1.f && js.takeoff) { setBase(da, js.takeoff, false); da.cur.t = std::min(since, js.takeoff->T); baseClock = false; }
		else if (!air->want && js.land) { setBase(da, js.land, false); da.cur.t = std::min(since, js.land->T); baseClock = false; }
		else if (moving && js.fly) { setBase(da, js.fly, true); }
		else if (js.idle) { setBase(da, js.idle, true); }
	}
	else if (s_standCrewless != nullptr && modcrew::crewless(d))
	{
		// MF "standcrewless": ohne Besatzung sackt der Combot zusammen und bleibt so stehen (einmal abgespielt, letzte Haltung bleibt)
		setBase(da, s_standCrewless, false);
	}
	else if (moving && ti != nullptr && ti->walk[tier] != nullptr)
	{
		setBase(da, ti->walk[tier], true, true);
		baseRate = vmf / std::max(1.f, tierThr);         // MF: Abspielrate = Tempo / Stufentempo (die Fuesse gleiten nicht)
	}
	else if (da.turn > 25.f && s_standTurn != nullptr)
	{
		setBase(da, s_standTurn, true);
	}
	else if ((inCombat(d) || gameTime < da.combatUntil || da.over.an != nullptr) && s_attackIdle != nullptr)
	{
		setBase(da, s_attackIdle, true);
	}
	else if (s_stand != nullptr)
	{
		setBase(da, s_stand, true);
	}

	// ---- Angriff starten (MF: einmal je Schlag/Schuss). Abgestimmt auf Warzone: der Schuss ist schon gefallen -> die Animation beginnt
	// kurz vor ihrem Schuss-Moment; beim Duell-Schlag faellt ihr Treffer-Moment genau auf den Schaden (modduel, Verzoegerung).
	if (trigArm >= 0)
	{
		const Part *arm = trigArm == 0 ? lo.armR : lo.armL;
		const auto *v = attacksOf(arm, trigArm == 0);
		da.combatUntil = gameTime + 3000;
		if (v != nullptr && !v->empty())
		{
			const Anim *an = nullptr;
			float impact = -1.f;
			if (trigSwing && arm != nullptr && !arm->reach.empty())
			{
				// (21.11. "Hiebe ins Leere"): nur Angriffe, deren Waffe im Treffer-Moment bis an den Gegner reicht (Schlagweite je
				// Animation, mfreach.py); reicht keiner, der weiteste
				const float need = modduel::swingNeedMF(d);
				std::vector<const Anim *> fit;
				const Anim *longest = nullptr;
				float lr = -1.f;
				for (const Anim *c : *v)
				{
					float r = 0.f, im = 0.f;
					if (!arm->reachOf(c->key, r, im)) { continue; }
					if (r >= need) { fit.push_back(c); }
					if (r > lr) { lr = r; longest = c; }
				}
				an = !fit.empty() ? fit[da.shots % fit.size()] : longest;
			}
			if (an == nullptr) { an = (*v)[da.shots % v->size()]; }
			{
				float r = 0.f, im = 0.f;
				if (arm != nullptr && arm->reachOf(an->key, r, im)) { impact = im; }
			}
			const bool busy = da.over.an != nullptr && da.over.an != s_hit[0] && da.over.an != s_hit[1] && da.over.an != s_hit[2]
			                  && da.over.an != s_hit[3] && da.over.t < da.over.an->T * 0.75f;
			if (!busy)
			{
				float t0 = 0.f, in = an->blendIn;
				if (trigSwing)
				{
					// Treffer-Moment = weiteste Streckung der Waffe (mfreach.py; das MF-Ereignis 0x200 liegt 0,1-0,2 s davor und loest den
					// Klang aus) - genau dann zieht modduel den Schaden ab
					const float th = impact >= 0.f ? impact : (an->tHit >= 0.f ? an->tHit : an->tFire);
					if (th >= 0.f) { t0 = th - static_cast<float>(modduel::HIT_DELAY_MS) * 0.001f; }   // kann < 0 sein: Schlag beginnt etwas spaeter
				}
				else if (an->tFire >= 0.f)
				{
					in = std::min(an->blendIn, 0.12f);
					t0 = std::max(0.f, an->tFire - in);
				}
				if (da.over.an != nullptr && da.over.t > 0.f && da.overFade > 0.f) { da.overOld = da.over; da.overOldFade = da.overFade; }
				da.over.an = an; da.over.t = t0; da.over.loop = false;
				da.overFade = 0.f;
				da.overIn = in;
				da.overStarted = false;
				da.overArm = arm;
				da.overCut = -1.f;
				++da.shots;
			}
		}
	}

	// ---- (21.11.) Parade: der Gegner schlaegt zu, dieser Combot wehrt ab (modduel hat das schon gewuerfelt) - er fuehrt seine eigene
	// Waffe dem Schlag entgegen: Angriffs-Animation des Nahkampfarms so, dass ihre weiteste Streckung genau auf den Treffer-Moment des
	// Gegners faellt, danach weich aus (kein Nachschwung). MF hat keine eigene Block-Animation; die Waffen treffen sich in der Mitte.
	{
		int pslot = -1;
		const unsigned pa = modduel::parryAt(d, pslot);
		if (pa != 0 && pa != da.lastParry && trigArm < 0 && gameTime <= pa)
		{
			da.lastParry = pa;
			const Part *parm = pslot == lo.slotR ? lo.armR : (pslot == lo.slotL ? lo.armL : nullptr);
			const auto *v = attacksOf(parm, pslot == lo.slotR);
			const bool busy = da.over.an != nullptr && da.over.an != s_hit[0] && da.over.an != s_hit[1] && da.over.an != s_hit[2]
			                  && da.over.an != s_hit[3] && da.over.t < da.over.an->T * 0.75f;
			if (parm != nullptr && v != nullptr && !v->empty() && !busy)
			{
				const Anim *an = (*v)[(da.shots + 1) % v->size()];
				float r = 0.f, im = -1.f;
				if (!parm->reachOf(an->key, r, im)) { im = an->tHit >= 0.f ? an->tHit + 0.15f : an->T * 0.5f; }
				const float until = static_cast<float>(pa - gameTime) * 0.001f;
				if (da.over.an != nullptr && da.over.t > 0.f && da.overFade > 0.f) { da.overOld = da.over; da.overOldFade = da.overFade; }
				da.over.an = an; da.over.t = im - until; da.over.loop = false;
				da.overFade = 0.f;
				da.overIn = std::min(an->blendIn, 0.2f);
				da.overStarted = false;
				da.overArm = parm;
				da.overCut = im + 0.25f;
				da.combatUntil = gameTime + 3000;
			}
		}
	}

	// ---- eingebaute Waffe (MF: Flak-Torso, Laser-Beine, DrunkMissile ...): ihre eigene Schuss-Animation, wenn gerade kein Arm zuschlaegt
	if (builtinPart != nullptr && trigArm < 0 && da.over.an == nullptr)
	{
		const Anim *an = nullptr;
		const auto it = s_armAnims.find(builtinPart->animFile);
		if (it != s_armAnims.end())
		{
			for (const auto *v : { &it->second.X, &it->second.L, &it->second.R })
			{
				for (const Anim *c : *v) { if (an == nullptr && c->tFire >= 0.f) { an = c; } }
			}
		}
		if (an != nullptr)
		{
			da.combatUntil = gameTime + 3000;
			const float in = std::min(an->blendIn, 0.3f);   // z. B. Haubitze: Rohr senkt sich zuegig, dann der Schuss-Moment
			da.over.an = an; da.over.t = std::max(0.f, an->tFire - in); da.over.loop = false;
			da.overFade = 0.f; da.overIn = in; da.overStarted = false; da.overArm = nullptr;
		}
	}

	// ---- Treffer-Zucken (MF animbase hit_*): getroffen und gerade kein Angriff
	if (d->body < da.lastBody && da.over.an == nullptr && trigArm < 0 && gameTime >= da.nextFlinch
	    && (da.lastBody - d->body) * 100u >= std::max(1u, static_cast<unsigned>(d->originalBody)) * 3u)   // spuerbarer Treffer, nicht Dauerfeuer/Brand
	{
		da.nextFlinch = gameTime + 1500;
		const Anim *h = s_hit[(da.shots + d->body) % 4];
		if (h != nullptr) { da.over.an = h; da.over.t = 0.f; da.over.loop = false; da.overFade = 0.f; da.overIn = h->blendIn; da.overStarted = false; da.overArm = nullptr; }
	}
	da.lastBody = d->body;

	// ---- Zeiten fortschreiben + Ereignisse
	if (da.cur.an != nullptr && dt > 0.f && baseClock)
	{
		const float t0 = da.cur.t;
		da.cur.t += dt * baseRate;
		if (!da.cur.loop) { da.cur.t = std::min(da.cur.t, da.cur.an->T); }
		events(d, da.cur.an, t0, da.cur.t, da.cur.loop, true, nullptr);
	}
	if (da.prev.an != nullptr && dt > 0.f) { da.prev.t += dt; }
	if (da.overOld.an != nullptr)
	{
		da.overOld.t = std::min(da.overOld.t + dt, da.overOld.an->T);
		da.overOldFade -= dt / 0.15f;
		if (da.overOldFade <= 0.f) { da.overOld.an = nullptr; }
	}
	if (da.fade < 1.f) { da.fade = std::min(1.f, da.fade + dt / da.fadeDur); }
	if (da.fade >= 1.f) { da.prev.an = nullptr; }
	if (da.over.an != nullptr)
	{
		const Anim &A = *da.over.an;
		const float t0 = da.over.t;
		da.over.t += dt;
		if (da.over.t > 0.f) { events(d, &A, std::max(0.f, t0), da.over.t, false, false, da.overArm); }
		// MF: Einblenden ueber die Einblendzeit ab dem Start, Ausblenden ab dem Ereignis 0x40000000 bis zum Ende
		if (!da.overStarted && da.over.t > 0.f) { da.overStarted = true; da.overFrom = std::max(0.f, t0); }
		const float fin = da.overStarted ? std::min(1.f, (da.over.t - da.overFrom) / std::max(0.04f, da.overIn)) : 0.f;
		const float outFrom = A.tOut >= 0.f && A.tOut < A.T ? A.tOut : std::max(0.f, A.T - A.blendOut);
		const float fout = da.over.t <= outFrom ? 1.f : std::clamp((A.T - da.over.t) / std::max(0.04f, A.T - outFrom), 0.f, 1.f);
		da.overFade = std::min(fin, fout);
		if (da.over.t >= A.T) { da.over.an = nullptr; da.overFade = 0.f; }
		else if (da.overCut >= 0.f && da.over.t >= da.overCut)
		{
			// Parade fertig: weich ausblenden wie eine abgeloeste Angriffs-Animation
			da.overOld = da.over; da.overOldFade = da.overFade;
			da.over.an = nullptr; da.overFade = 0.f; da.overCut = -1.f;
		}
	}

	// ---- Haltung mischen: alte Grund-Animation -> neue (Ueberblenden) -> Angriff (Gewichte je Knochen; im Gehen ohne Beine)
	if (da.prev.an != nullptr) { applyAnim(P, *da.prev.an, da.prev.t, da.prev.loop, 1.f); }
	if (da.cur.an != nullptr) { applyAnim(P, *da.cur.an, da.cur.t, da.cur.loop, da.prev.an ? da.fade : 1.f); }
	if (da.overOld.an != nullptr) { applyAnim(P, *da.overOld.an, da.overOld.t, false, da.overOldFade, moving || air != nullptr); }
	if (da.over.an != nullptr && da.over.t > 0.f) { applyAnim(P, *da.over.an, da.over.t, false, da.overFade, moving || air != nullptr); }

	if (s_da.size() > 4000)
	{
		for (auto it = s_da.begin(); it != s_da.end();) { it = (now - it->second.lastGfx > 20000) ? s_da.erase(it) : std::next(it); }
	}
}
} // namespace

bool draw(DROID *d, bool bButton, const glm::mat4 &model, const glm::mat4 &view, int colour, PIELIGHT brightness, int pieFlag, int pieFlagData)
{
	if (d == nullptr || !isModular(d)) { return false; }
	Loadout lo = loadoutOf(d);
	if (!lo.torso) { return false; }
	Pose P;
	if (bButton)
	{
		if (s_stand) { applyAnim(P, *s_stand, static_cast<float>(graphicsTime) * 0.001f, true, 1.f); }
	}
	else
	{
		animate(d, lo, P);
	}
	bool drew = false;
	const float stretch = -(static_cast<float>(d->heightAboveMap));
	if (!bButton && hasAbility(d, "cloak"))
	{
		pieFlag |= pie_TRANSLUCENT;   // Tarnung: nur zu sehen als Schemen
		pieFlagData = 110;
	}
	s_emitGlow = !bButton && !(pieFlag & pie_TRANSLUCENT);
	const glm::mat4 shown = (bButton || s_show == 1.f) ? model : model * glm::scale(glm::mat4(1.f), glm::vec3(s_show));   // (23:00) im Spiel groesser
	// Flug: der Combot ist um die Flughoehe angehoben gezeichnet (component.cpp, flightOffset) - der Boden liegt so weit unter ihm
	bool clampFeet = false;
	float floorZ = 0.f;
	if (!bButton)
	{
		const auto ait = s_air.find(d->id);
		if (ait != s_air.end() && airActive(d, ait->second))
		{
			const float c = ait->second.cur;
			clampFeet = true;
			floorZ = -(c * c * (3.f - 2.f * c) * FLY_HEIGHT) / std::max(0.01f, s_scale * s_show);
		}
	}
	drawPose(lo, P, shown, [&](const iIMDShape *s, const glm::mat4 &m, const Part *p) {
		if (drawEffectPiece(s, m, view, p, d->id)) { return; }
		if (pie_Draw3DShape(s, 0, colour, brightness, pieFlag, pieFlagData, m, view, stretch, true)) { drew = true; }
	}, clampFeet, floorZ);
	s_emitGlow = false;
	return drew;
}

unsigned registerCorpse(const DROID *d, unsigned birthTime)
{
	if (d == nullptr || !isModular(d)) { return 0; }
	// alte Eintraege aufraeumen
	for (auto it = s_corpses.begin(); it != s_corpses.end();)
	{
		if (it->second.birth + 12000 < birthTime) { it = s_corpses.erase(it); }
		else { ++it; }
	}
	const unsigned id = s_corpseNext++;
	if (s_corpseNext > 60000) { s_corpseNext = 1000; }
	Corpse c;
	c.lo = loadoutOf(d);
	c.birth = birthTime;
	c.variant = static_cast<int>(d->id);
	c.x = d->pos.x;
	c.y = d->pos.y;
	s_corpses[id] = c;
	s_da.erase(d->id);
	return id;
}

bool drawCorpse(unsigned id, const glm::mat4 &model, const glm::mat4 &view, int colour)
{
	auto it = s_corpses.find(id);
	if (it == s_corpses.end()) { return false; }
	Corpse &c = it->second;
	const unsigned elapsed = graphicsTime > c.birth ? graphicsTime - c.birth : 0u;
	const Anim *da = s_death[(c.variant % 5 + 5) % 5];
	if (!c.boomed && da != nullptr && da->tFire >= 0.f && static_cast<float>(elapsed) * 0.001f >= da->tFire)
	{
		// MF: im Moment des Zusammenbruchs der Zerstoerungs-Klang des Torsos (UnitParams +0xa4)
		c.boomed = true;
		const auto st = c.lo.torso ? s_partSnd.find(c.lo.torso->animFile + ":torso") : s_partSnd.end();
		const int n = st != s_partSnd.end() ? st->second.destroy : -1;
		const int tr = trackOfSound(n);
		if (tr >= 0 && elapsed < 6000) { audio_PlayStaticTrack(c.x, c.y, tr); }
	}
	const PIELIGHT light = pal_SetBrightness(255);
	const glm::mat4 shown = s_show == 1.f ? model : model * glm::scale(glm::mat4(1.f), glm::vec3(s_show));
	// MF: eine der fuenf Todesanimationen (death_kneel / break / collapse / deadarm / stun), einmal abgespielt, letzte Haltung bleibt
	drawLoadout(c.lo, ANIM_DIE, elapsed, c.variant, shown, [&](const iIMDShape *s, const glm::mat4 &m, const Part *) {
		pie_Draw3DShape(s, 0, colour, light, pie_SHADOW, 0, m, view, 0.f, true);
	});
	return true;
}

}
