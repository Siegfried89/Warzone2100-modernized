// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Modularer Combot-Zusammenbau zur Laufzeit.
// Vorwaertskinematik (FK) der 15 MF-Gelenke mit den Original-Animationen. Fehlende Teile (Arme/Beine) werden einfach
// nicht gezeichnet, die Animation laeuft fuer alle uebrigen Teile unveraendert weiter.

#include <nlohmann/json.hpp> // vor den WZ-Headern
#include "lib/framework/modtime.h"

#include "modcombot.h"

#include "lib/framework/frame.h"
#include "lib/framework/file.h"
#include "lib/framework/vector.h"
#include "lib/framework/fixedpoint.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/piepalette.h"

#include "baseobject.h"
#include "droid.h"
#include "effects.h"
#include "feature.h"
#include "game_world.h"
#include "map.h"
#include "modlayermap.h"
#include "modlayer.h"
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

struct Anim
{
	int nf = 0;
	int ms = 1;                              // Millisekunden pro Bild
	glm::vec3 first = glm::vec3(0.f), mean = glm::vec3(0.f);
	std::vector<glm::vec3> pos;
	std::vector<std::array<uint32_t, 15>> w;
};

bool s_loaded = false;
bool s_ok = false;
float s_scale = 1.8f;
float s_show = 1.f;   // MOD (19.11. 23:00 "die Groessenordnung auch"): Anzeige-Vergroesserung im Spiel (parts.json "show"), Baustation unveraendert
int s_armb[2][3] = {{9, 10, 11}, {12, 13, 14}};
std::vector<Part> s_parts;
std::map<std::string, std::vector<Anim>> s_filesAnims;   // je RobotParts-Datei, in Dateireihenfolge
std::map<int, Anim> s_baseAnims;                         // animbase (Index = Chunk-Nummer)
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
};
std::unordered_map<unsigned, Corpse> s_corpses;
unsigned s_corpseNext = 1000;

Anim readAnim(const nlohmann::json &j)
{
	Anim a;
	a.nf = j.value("nf", 0);
	const double t = j.value("t", 1.0);
	a.ms = std::max(1, static_cast<int>(std::lround(t / std::max(1, a.nf - 1) * 1000.0)));
	for (const auto &p : j["pos"])
	{
		a.pos.emplace_back(p[0].get<float>(), p[1].get<float>(), p[2].get<float>());
	}
	for (const auto &row : j["w"])
	{
		std::array<uint32_t, 15> r{};
		for (size_t i = 0; i < 15 && i < row.size(); ++i) { r[i] = row[i].get<uint32_t>(); }
		a.w.push_back(r);
	}
	if (a.pos.size() != static_cast<size_t>(a.nf) || a.w.size() != static_cast<size_t>(a.nf) || a.nf < 2) { a.nf = 0; return a; }
	a.first = a.pos[0];
	for (const auto &p : a.pos) { a.mean += p; }
	a.mean /= static_cast<float>(a.nf);
	return a;
}

glm::vec3 jv(const nlohmann::json &j, const char *key)
{
	if (!j.contains(key) || !j[key].is_array() || j[key].size() < 3) { return glm::vec3(0.f); }
	return glm::vec3(j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>());
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
	if (pj.contains("armb") && pj["armb"].is_array() && pj["armb"].size() == 2)
	{
		for (int s = 0; s < 2; ++s) { for (int i = 0; i < 3; ++i) { s_armb[s][i] = pj["armb"][s][i].get<int>(); } }
	}
	for (const auto &d : pj["parts"])
	{
		Part p;
		p.id = d.value("id", "");
		p.file = d.value("file", "");
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
		s_partIndex[p.id] = s_parts.size();
		s_parts.push_back(std::move(p));
	}
	std::vector<char> ab;
	if (loadFileToBufferVector("mfmodular/anims.json", ab, false, false) && !ab.empty())
	{
		nlohmann::json aj = nlohmann::json::parse(ab.begin(), ab.end(), nullptr, false);
		if (!aj.is_discarded())
		{
			for (auto it = aj.begin(); it != aj.end(); ++it)
			{
				if (!it.value().is_array()) { continue; }
				if (it.key() == "_base")
				{
					for (const auto &e : it.value())
					{
						Anim a = readAnim(e);
						if (a.nf > 0) { s_baseAnims[e.value("idx", -1)] = std::move(a); }
					}
				}
				else
				{
					std::vector<Anim> v;
					for (const auto &e : it.value())
					{
						Anim a = readAnim(e);
						if (a.nf > 0) { v.push_back(std::move(a)); }
					}
					if (!v.empty()) { s_filesAnims[it.key()] = std::move(v); }
				}
			}
		}
	}
	debug(LOG_INFO, "modcombot: %zu Teile, %zu Animationsdateien", s_parts.size(), s_filesAnims.size());
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

// MF-Eulerreihenfolge 'zxy', Winkel in Grad
M3 euler(const float *deg)
{
	const float k = 3.14159265358979f / 180.f;
	return mul(mul(rotZ(deg[2] * k), rotX(deg[0] * k)), rotY(deg[1] * k));
}

struct Pose
{
	M3 R[15];
	glm::vec3 p[15];
	bool present[15];
};

// Animation zum Zeitpunkt abtasten: 10-Bit-Eulerwinkel je Gelenk, linear zwischen den Bildern interpoliert
void sample(const Anim &a, unsigned elapsed, bool loop, float ang[15][3], glm::vec3 &root, bool centerMean)
{
	int f0, f1;
	float fr = 0.f;
	const unsigned frameIdx = elapsed / static_cast<unsigned>(a.ms);
	if (loop)
	{
		f0 = static_cast<int>(frameIdx % static_cast<unsigned>(a.nf));
		f1 = (f0 + 1) % a.nf;
		fr = static_cast<float>(elapsed % static_cast<unsigned>(a.ms)) / static_cast<float>(a.ms);
	}
	else
	{
		f0 = static_cast<int>(std::min<unsigned>(frameIdx, static_cast<unsigned>(a.nf - 1)));
		f1 = std::min(f0 + 1, a.nf - 1);
		fr = (f0 == a.nf - 1) ? 0.f : static_cast<float>(elapsed % static_cast<unsigned>(a.ms)) / static_cast<float>(a.ms);
	}
	for (int b = 0; b < 15; ++b)
	{
		for (int ax = 0; ax < 3; ++ax)
		{
			const int v0 = static_cast<int>((a.w[f0][b] >> (10 * ax)) & 1023u);
			const int v1 = static_cast<int>((a.w[f1][b] >> (10 * ax)) & 1023u);
			const int s0 = v0 >= 512 ? v0 - 1024 : v0;
			const int d = ((v1 - v0 + 512 + 1024) & 1023) - 512;
			ang[b][ax] = (static_cast<float>(s0) + static_cast<float>(d) * fr) * 360.f / 1024.f;
		}
	}
	root = a.pos[f0] + (a.pos[f1] - a.pos[f0]) * fr;
	// Die Original-Gehanimation enthaelt den Vorwaertsweg; das Modell soll aber am Droiden bleiben
	const glm::vec3 c = centerMean ? a.mean : a.first;
	root.x -= c.x;
	root.y -= c.y;
}

const Part *defaultLegs()
{
	auto it = s_partIndex.find("Basic_L0");
	return it == s_partIndex.end() ? nullptr : &s_parts[it->second];
}

void computePose(const Loadout &lo, const float ang[15][3], const glm::vec3 &root, Pose &T)
{
	for (int i = 0; i < 15; ++i) { T.present[i] = false; T.R[i] = ident(); T.p[i] = glm::vec3(0.f); }
	const Part *L = lo.legs ? lo.legs : defaultLegs();
	if (!L || !lo.torso) { return; }
	const M3 R0 = euler(ang[0]);
	const glm::vec3 p0 = root;
	T.R[0] = R0; T.p[0] = p0; T.present[0] = true;
	static const int legB[2][3] = {{1, 2, 3}, {4, 5, 6}};
	const glm::vec3 hip[2] = {L->hipR, L->hipL}, kn[2] = {L->kneeA, L->kneeB}, an[2] = {L->ankA, L->ankB};
	for (int s = 0; s < 2; ++s)
	{
		M3 R = mul(R0, euler(ang[legB[s][0]]));
		glm::vec3 p = p0 + mv(R0, hip[s]);
		T.R[legB[s][0]] = R; T.p[legB[s][0]] = p; T.present[legB[s][0]] = true;
		p = p + mv(R, kn[s]);
		R = mul(R, euler(ang[legB[s][1]]));
		T.R[legB[s][1]] = R; T.p[legB[s][1]] = p; T.present[legB[s][1]] = true;
		p = p + mv(R, an[s]);
		R = mul(R, euler(ang[legB[s][2]]));
		T.R[legB[s][2]] = R; T.p[legB[s][2]] = p; T.present[legB[s][2]] = true;
	}
	const M3 R7 = mul(R0, euler(ang[7]));
	const glm::vec3 p7 = p0 + mv(R0, L->mount);
	T.R[7] = R7; T.p[7] = p7; T.present[7] = true;
	// MOD (16.11. 18:36): der Kopf sitzt am Hals, nicht im Torso-Ursprung (war unsichtbar im Rumpf)
	T.R[8] = mul(R7, euler(ang[8])); T.p[8] = p7 + mv(R7, lo.torso->neck); T.present[8] = true;
	for (int s = 0; s < 2; ++s)
	{
		const Part *A = s == 0 ? lo.armR : lo.armL;
		if (!A) { continue; }
		const glm::vec3 sh = s == 0 ? lo.torso->shR : lo.torso->shL;
		const int *b = s_armb[s];
		M3 R = mul(R7, euler(ang[b[0]]));
		glm::vec3 p = p7 + mv(R7, sh);
		T.R[b[0]] = R; T.p[b[0]] = p; T.present[b[0]] = true;
		for (int i = 1; i <= 2; ++i)
		{
			const glm::vec3 off = (static_cast<size_t>(i - 1) < A->chain.size()) ? A->chain[static_cast<size_t>(i - 1)] : glm::vec3(0.f);
			p = p + mv(R, off);
			R = mul(R, euler(ang[b[i]]));
			T.R[b[i]] = R; T.p[b[i]] = p; T.present[b[i]] = true;
		}
	}
}

// MF-Koordinaten (x,y,z hoch) -> Modellraum (x,z,-y), skaliert
glm::mat4 boneMatrix(const M3 &R, const glm::vec3 &p)
{
	M3 C{};
	C.a[0][0] = 1.f; C.a[1][2] = 1.f; C.a[2][1] = -1.f;
	const M3 RE = mul(mul(C, R), transp(C));
	glm::mat4 g(1.f);
	for (int r = 0; r < 3; ++r) { for (int c = 0; c < 3; ++c) { g[c][r] = RE.a[r][c]; } }
	g[3] = glm::vec4(p.x * s_scale, p.z * s_scale, -p.y * s_scale, 1.f);
	return g;
}

const Anim *baseAnim(int idx)
{
	auto it = s_baseAnims.find(idx);
	return it == s_baseAnims.end() ? nullptr : &it->second;
}

const Anim *fileAnim(const std::string &file, size_t idx)
{
	auto it = s_filesAnims.find(file);
	if (it == s_filesAnims.end() || it->second.empty()) { return nullptr; }
	return &it->second[std::min(idx, it->second.size() - 1)];
}

// Angriffsanimation der Waffendatei: die Dateien enthalten Paare (rechts, links); die letzte Gruppe ist der Angriff
const Anim *attackAnim(const Loadout &lo)
{
	const Part *a = lo.armR ? lo.armR : lo.armL;
	if (!a) { return fileAnim("Basic", 2); }
	auto it = s_filesAnims.find(a->file);
	if (it == s_filesAnims.end() || it->second.empty()) { return fileAnim("Basic", 2); }
	const size_t n = it->second.size();
	size_t idx = (n - 1) / 2 * 2;
	if (a->side < 0 && idx + 1 < n) { ++idx; }
	return &it->second[idx];
}

void dropHeight(const Part *L, float &drop)
{
	drop = 0.f;
	if (!L) { return; }
	drop = std::max(0.f, -(L->hipR.z + L->kneeA.z + L->ankA.z)) * 0.85f;
}

// Welche Einzelteile (Index in pieNames) gehoeren zu welchem Gelenk
void emitBone(const Part *p, const std::vector<int> &idx, const glm::mat4 &m, const DrawPiece &emit)
{
	for (int i : idx)
	{
		if (i < 0 || static_cast<size_t>(i) >= p->pieNames.size()) { continue; }
		const iIMDShape *s = p->shape(static_cast<size_t>(i));
		if (s) { emit(s, m, p); }
	}
}

std::vector<int> range(size_t from, size_t to, size_t count)
{
	std::vector<int> v;
	for (size_t i = from; i < to && i < count; ++i) { v.push_back(static_cast<int>(i)); }
	return v;
}

} // namespace

// ---------------------------------------------------------------- Part

const iIMDShape *Part::shape(size_t i) const
{
	if (!m_tried)
	{
		m_tried = true;
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
struct Air { float cur = 0.f; unsigned last = 0; unsigned nextPuff = 0; bool want = false; unsigned since = 0; };   // since: Beginn von Abheben/Landen
std::unordered_map<uint32_t, Air> s_air;
constexpr float FLY_HEIGHT = 170.f;      // Flughoehe ueber dem Boden
constexpr float FLY_RISE_MS = 1200.f;    // so lange dauert Abheben / Landen (MOD 17.11.: passend zu den MF-Animationen, etwas schneller abgespielt)

bool overVoid(const DROID *d)
{
	if (!tileOnMap(gameWorld.map, map_coord(d->pos.x), map_coord(d->pos.y))) { return false; }
	const MAPTILE *t = mapTile(gameWorld.map, map_coord(d->pos.x), map_coord(d->pos.y));
	return TileNumber_tile(t->texture) == MOD_TILE_VOID;
}
}

float flightOffset(const DROID *d)
{
	if (d == nullptr || d->died || !canFly(d)) { return 0.f; }
	bool want = overVoid(d);
	if (!want && d->sMove.Status != MOVEINACTIVE && d->sMove.Status != MOVEHOVER)
	{
		const int dist = iHypot(d->sMove.destination - d->pos.xy());
		want = dist > DIST_TILE * 5 / 2;   // weitere Strecke: abheben (kurze Wege werden gelaufen)
	}
	Air &a = s_air[d->id];
	const unsigned now = graphicsTime;
	if (want != a.want) { a.want = want; a.since = now; }
	const float dt = a.last == 0 || now < a.last ? 0.f : static_cast<float>(std::min(now - a.last, 200u));
	a.last = now;
	const float step = dt / FLY_RISE_MS;
	a.cur = want ? std::min(1.f, a.cur + step) : std::max(0.f, a.cur - step);
	if (a.cur > 0.05f && now >= a.nextPuff)
	{
		// Duesenstrahl unter den Fuessen (nur Anzeige)
		a.nextPuff = now + 110;
		const float h = a.cur * a.cur * (3.f - 2.f * a.cur) * FLY_HEIGHT;
		Vector3i p(d->pos.x + (rand() % 21 - 10), d->pos.z + static_cast<int>(h) - 6, d->pos.y + (rand() % 21 - 10));
		addEffect(&p, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING, false, nullptr, 0);
	}
	if (s_air.size() > 512)
	{
		for (auto it = s_air.begin(); it != s_air.end();) { it = (now - it->second.last > 10000) ? s_air.erase(it) : std::next(it); }
	}
	return a.cur * a.cur * (3.f - 2.f * a.cur) * FLY_HEIGHT;
}

static bool airborne(const DROID *d)
{
	const auto it = s_air.find(d->id);
	return it != s_air.end() && it->second.cur > 0.4f;
}

namespace
{
std::unordered_map<uint32_t, unsigned> s_fieldReady;   // Kraftfeld: ab wann wieder bereit (gameTime)
constexpr unsigned FIELD_RECHARGE_MS = 8000;
constexpr int SONAR_RADIUS = 9 * TILE_SPLIT;           // 9 Original-Kacheln (in Feinfeldern)

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
	const int x = d->pos.x + static_cast<int>(gameRand(129)) - 64;
	const int y = d->pos.y + static_cast<int>(gameRand(129)) - 64;
	if (x < 0 || y < 0 || !tileOnMap(gameWorld.map, map_coord(x), map_coord(y))) { return; }
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

// MOD (16.11. 18:27): Gehen = der einzige echte Lauf-Zyklus der MF-Daten (HighSpeed). "Basic" Nr. 0 war ein Ausfallschritt
// des Waffenarms (sah aus wie Angreifen im Laufen). Die Abspielgeschwindigkeit folgt der Bewegung -> die Fuesse gleiten nicht.
constexpr float WALK_STRIDE_MF = 44.f;   // Bodenweg je Laufzyklus in MF-Einheiten (aus der Fussbahn gemessen)

const Anim *walkAnim()
{
	const Anim *a = fileAnim("HighSpeed", 0);
	return a != nullptr ? a : fileAnim("Basic", 0);
}

struct WalkPhase
{
	unsigned last = 0;
	double ms = 0.0;
};
std::unordered_map<uint32_t, WalkPhase> s_walk;

unsigned walkPhaseMs(const DROID *d)
{
	const Anim *a = walkAnim();
	if (s_walk.size() > 4000) { s_walk.clear(); }
	WalkPhase &w = s_walk[d->id];
	const unsigned now = graphicsTime;
	if (w.last == 0 || now < w.last || now - w.last > 1000) { w.last = now; }
	const float cycleMs = a != nullptr ? static_cast<float>(a->nf * a->ms) : 1100.f;
	const float spd = std::fabs(static_cast<float>(d->sMove.speed));
	const float strideWorld = WALK_STRIDE_MF * s_scale * s_show;   // (23:00) groesser angezeigt -> laengere Schritte
	float rate = spd > 1.f ? spd * cycleMs / 1000.f / strideWorld : 0.5f;
	rate = std::clamp(rate, 0.25f, 3.f);
	w.ms += static_cast<double>(now - w.last) * static_cast<double>(rate);
	w.last = now;
	if (w.ms > 3.6e6) { w.ms = std::fmod(w.ms, static_cast<double>(cycleMs)); }
	return static_cast<unsigned>(w.ms);
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

void drawLoadout(const Loadout &lo, AnimKind kind, unsigned elapsedMs, int dieVariant, const glm::mat4 &base, const DrawPiece &emit)
{
	if (!ready() || !lo.torso) { return; }
	const Anim *an = nullptr;
	bool loop = true, centerMean = true;
	switch (kind)
	{
	case ANIM_WALK:
		an = walkAnim();
		break;
	case ANIM_FIRE: an = attackAnim(lo); centerMean = false; break;
	case ANIM_DIE: an = baseAnim(50 + (dieVariant % 5 + 5) % 5); loop = false; centerMean = false; break;
	// MOD (Wunsch 17.11. 15:46 Punkt 10): MF-Jetpack-Animationen - Abheben (aus der Ruhehaltung), Schweben (Schleife), Landen
	case ANIM_FLY_UP: an = fileAnim("JetPack", 2); loop = false; centerMean = false; break;
	case ANIM_FLY: an = fileAnim("JetPack", 0); centerMean = false; break;
	case ANIM_FLY_DOWN: an = fileAnim("JetPack", 3); loop = false; centerMean = false; break;
	default: an = baseAnim(30); break;
	}
	if (an == nullptr) { an = baseAnim(30); }
	if (an == nullptr) { an = fileAnim("Basic", 0); }
	if (an == nullptr) { return; }
	float ang[15][3];
	glm::vec3 root(0.f);
	if (kind == ANIM_WALK && an == walkAnim() && an->nf > 0 && an->ms > 0)
	{
		// MOD (17.11., Wunsch "Der Combot verwechselt die Laufrichtungsanimation"): der MF-Lauf-Zyklus (HighSpeed) laeuft in den Daten
		// rueckwaerts (der Standfuss wandert nach vorn) - rueckwaerts abgespielt geht der Combot vorwaerts in Blickrichtung
		const unsigned cycle = static_cast<unsigned>(an->nf) * static_cast<unsigned>(an->ms);
		elapsedMs = (cycle - (elapsedMs % cycle)) % cycle;
	}
	sample(*an, elapsedMs, loop, ang, root, centerMean);
	if (kind == ANIM_FIRE || kind == ANIM_FLY_UP || kind == ANIM_FLY || kind == ANIM_FLY_DOWN)
	{
		// MOD (17.11., "waehrend des Angreifens gleitet er schraeg zur Seite weg"): die Angriffsanimationen enthalten einen Ausfallschritt
		// (bis ~15 MF-Einheiten schraeg) - der Combot bleibt am Platz der Einheit, nur das Heben/Senken bleibt
		root.x = 0.f;
		root.y = 0.f;
	}
	const bool noLegs = lo.legs == nullptr || (lo.severed & SEV_LEGS) != 0;
	if (noLegs)
	{
		float drop = 0.f;
		dropHeight(lo.legs ? lo.legs : defaultLegs(), drop);
		root.z -= drop;
	}
	Pose T;
	computePose(lo, ang, root, T);
	if (!T.present[7]) { return; }
	const DrawPiece em = [&](const iIMDShape *s, const glm::mat4 &m, const Part *p) { emit(s, base * m, p); };

	if (!noLegs)
	{
		const size_t n = lo.legs->pieNames.size();
		emitBone(lo.legs, range(0, 1, n), boneMatrix(T.R[0], T.p[0]) , em);
		for (int b = 1; b <= 5; ++b) { emitBone(lo.legs, range(static_cast<size_t>(b), static_cast<size_t>(b) + 1, n), boneMatrix(T.R[b], T.p[b]), em); }
		emitBone(lo.legs, range(6, n, n), boneMatrix(T.R[6], T.p[6]), em);
	}
	{
		const size_t n = lo.torso->pieNames.size();
		std::vector<int> a = range(0, 1, n);
		for (int i : range(2, n, n)) { a.push_back(i); }
		emitBone(lo.torso, a, boneMatrix(T.R[7], T.p[7]), em);
		emitBone(lo.torso, range(1, 2, n), boneMatrix(T.R[8], T.p[8]), em);
		if (s_emitGlow && lo.torso->eyePiece >= 0 && kind != ANIM_DIE)
		{
			const iIMDShape *g = glowShape(lo.torso->eyeGlow);
			if (g != nullptr)
			{
				const glm::mat4 bm = lo.torso->eyePiece == 1 ? boneMatrix(T.R[8], T.p[8]) : boneMatrix(T.R[7], T.p[7]);
				em(g, bm * glm::translate(glm::mat4(1.f), lo.torso->eyePos), &s_fxBillboard);   // Lichthof (siehe draw)
			}
		}
	}
	for (int s = 0; s < 2; ++s)
	{
		const Part *A = s == 0 ? lo.armR : lo.armL;
		if (!A) { continue; }
		const size_t n = A->pieNames.size();
		const int *b = s_armb[s];
		emitBone(A, range(0, 1, n), boneMatrix(T.R[b[0]], T.p[b[0]]), em);
		emitBone(A, range(1, 2, n), boneMatrix(T.R[b[1]], T.p[b[1]]), em);
		emitBone(A, range(2, n, n), boneMatrix(T.R[b[2]], T.p[b[2]]), em);
		if (s_emitGlow && A->fxPiece >= 0 && kind != ANIM_DIE)
		{
			const iIMDShape *g = glowShape(A->fxModel);
			const int bi = b[std::min(2, A->fxPiece)];
			if (g != nullptr) { em(g, boneMatrix(T.R[bi], T.p[bi]) * glm::translate(glm::mat4(1.f), A->fxPos), &s_fxAttached); }
		}
	}
}

bool draw(DROID *d, bool bButton, const glm::mat4 &model, const glm::mat4 &view, int colour, PIELIGHT brightness, int pieFlag, int pieFlagData)
{
	if (d == nullptr || !isModular(d)) { return false; }
	Loadout lo = loadoutOf(d);
	if (!lo.torso) { return false; }
	AnimKind kind = ANIM_IDLE;
	if (!bButton)
	{
		switch (d->animationEvent)
		{
		case ANIM_EVENT_ACTIVE: kind = ANIM_WALK; break;
		case ANIM_EVENT_FIRING: kind = ANIM_FIRE; break;
		case ANIM_EVENT_DYING: kind = ANIM_DIE; break;
		default: break;
		}
	}
	unsigned elapsed = bButton ? graphicsTime : (graphicsTime > d->timeAnimationStarted ? graphicsTime - d->timeAnimationStarted : 0u);
	if (!bButton && kind != ANIM_DIE && kind != ANIM_FIRE)
	{
		// MOD (17.11. Punkt 10): Flug-Phasen mit den MF-Jetpack-Animationen (Abheben / Schweben / Landen); vorher nur Ruhehaltung
		const auto ait = s_air.find(d->id);
		if (ait != s_air.end() && (ait->second.cur > 0.f || ait->second.want))
		{
			const Air &a = ait->second;
			const unsigned dt = graphicsTime > a.since ? graphicsTime - a.since : 0u;
			if (a.want && a.cur < 1.f && fileAnim("JetPack", 2) != nullptr)
			{
				kind = ANIM_FLY_UP; elapsed = static_cast<unsigned>(static_cast<float>(dt) * 2000.f / FLY_RISE_MS);
			}
			else if (!a.want && fileAnim("JetPack", 3) != nullptr)
			{
				kind = ANIM_FLY_DOWN; elapsed = static_cast<unsigned>(static_cast<float>(dt) * 2333.f / FLY_RISE_MS);
			}
			else if (a.want && fileAnim("JetPack", 0) != nullptr)
			{
				kind = ANIM_FLY; elapsed = graphicsTime + d->id * 97u;
			}
			else if (kind == ANIM_WALK && airborne(d))
			{
				kind = ANIM_IDLE;   // alte Pakete ohne Fluganimation: Beine laufen nicht
			}
		}
	}
	if (!bButton && kind == ANIM_WALK) { elapsed = walkPhaseMs(d); }   // MOD: Schrittfolge passend zur Geschwindigkeit
	bool drew = false;
	const float stretch = -(static_cast<float>(d->heightAboveMap));
	if (!bButton && kind != ANIM_DIE && hasAbility(d, "cloak"))
	{
		pieFlag |= pie_TRANSLUCENT;   // Tarnung: nur zu sehen als Schemen
		pieFlagData = 110;
	}
	s_emitGlow = !bButton && !(pieFlag & pie_TRANSLUCENT);
	const glm::mat4 shown = (bButton || s_show == 1.f) ? model : model * glm::scale(glm::mat4(1.f), glm::vec3(s_show));   // (23:00) im Spiel groesser
	drawLoadout(lo, kind, elapsed, static_cast<int>(d->id), shown, [&](const iIMDShape *s, const glm::mat4 &m, const Part *p) {
		if (drawEffectPiece(s, m, view, p, d->id)) { return; }
		if (pie_Draw3DShape(s, 0, colour, brightness, pieFlag, pieFlagData, m, view, stretch, true)) { drew = true; }
	});
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
	s_corpses[id] = c;
	return id;
}

bool drawCorpse(unsigned id, const glm::mat4 &model, const glm::mat4 &view, int colour)
{
	auto it = s_corpses.find(id);
	if (it == s_corpses.end()) { return false; }
	const Corpse &c = it->second;
	const unsigned elapsed = graphicsTime > c.birth ? graphicsTime - c.birth : 0u;
	const PIELIGHT light = pal_SetBrightness(255);
	const glm::mat4 shown = s_show == 1.f ? model : model * glm::scale(glm::mat4(1.f), glm::vec3(s_show));
	drawLoadout(c.lo, ANIM_DIE, elapsed, c.variant, shown, [&](const iIMDShape *s, const glm::mat4 &m, const Part *) {
		pie_Draw3DShape(s, 0, colour, light, pie_SHADOW, 0, m, view, 0.f, true);
	});
	return true;
}

}
