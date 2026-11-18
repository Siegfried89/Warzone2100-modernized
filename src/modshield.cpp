// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Schildgeneratoren mit Kuppel (siehe modshield.h)
#include "modshield.h"

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/frameresource.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/gfx_api.h"
#include "lib/ivis_opengl/piestate.h"
#include "lib/ivis_opengl/png_util.h"
#include "lib/sound/audio.h"
#include "ai.h"
#include "component.h"
#include "display3d.h"
#include "game_world.h"
#include "modgrid.h"
#include "modlayer.h"
#include "objmem.h"
#include "projectile.h"
#include "stats.h"
#include "structure.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <iterator>
#include <map>
#include <vector>

namespace modshield
{
namespace
{
struct State
{
	bool init = false;          // erst beim ersten Spieltakt: Schild steht sofort voll (wie Supreme Commander)
	int hp = 0;                 // aktuelle Schildpunkte
	int frac = 0;               // Rest der Aufladung (Tausendstel)
	bool down = false;          // zusammengebrochen
	uint32_t downUntil = 0;     // Ausfall bis (Spielzeit)
	bool powered = true;
	uint32_t powerBackAt = 0;   // Strom wieder da: Kuppel nach 3 s wieder an (Spielzeit)
	uint32_t lastDamage = 0;    // letzter Treffer (Spielzeit) - Aufladung erst danach
	// nur Anzeige
	uint32_t lastHit = 0;
	uint32_t downAt = 0;
	uint32_t upAt = 0;
};

// Kugel wie in Supreme Commander: Mittelpunkt unter dem Boden (Schildgenerator ShieldSize 26 / ShieldVerticalOffset -3 = 3/13 des Radius,
// fahrender Schildgenerator 16 / -3 = 3/8). Sichtbar ist die Kappe darueber; Treffer werden an genau dieser Kugel aufgehalten.
constexpr uint32_t POWER_BACK_MS = 3000;    // ShieldEnergyDrainRechargeTime

/// Werte einer Schildquelle: Schild-Bauwerk (structure.json) oder Fahrzeug mit Schild-Systemturm (sensor.json, wie der fahrende
/// Schildgenerator UEL0307 in Supreme Commander)
struct Params
{
	int radius = 0, hp = 0, regen = 0, recharge = 0, regenDelay = 0;
	int drop = 231;             // Kugelmitte unter dem Boden, Tausendstel des Radius
	bool needsPower = false;    // Bauwerke brauchen Strom, Fahrzeuge nicht
};

struct Active
{
	const BASE_OBJECT *s;
	int64_t cx, cy, cz, r;      // Kugelmitte (unter dem Boden) und Radius
	int64_t gz;                 // Bodenhoehe am Generator
	modlayer::Layer layer;
	Params P;
};

struct Hit
{
	glm::vec3 p;                // Einschlagpunkt (Welt, Anzeige-Koordinaten x, Hoehe, -y)
	glm::vec3 n;                // nach aussen
	glm::vec3 c;                // Kugelmitte (Anzeige-Koordinaten)
	float r;                    // Kugelradius
	uint32_t t;                 // Anzeigezeit
	uint32_t shieldId;
	float size;
};

std::map<uint32_t, State> s_state;          // nach Bau-ID geordnet (gleiche Reihenfolge auf allen Rechnern)
std::vector<Active> s_active;
std::vector<Hit> s_hits;
int s_sndHit = -2, s_sndDown = -2, s_sndUp = -2;

bool shieldParams(const BASE_OBJECT *o, Params &P)
{
	if (o == nullptr || o->died) { return false; }
	if (o->type == OBJ_STRUCTURE)
	{
		const STRUCTURE *s = static_cast<const STRUCTURE *>(o);
		const STRUCTURE_STATS *st = s->pStructureType;
		if (st == nullptr || st->shieldRadius <= 0 || st->shieldHP <= 0 || s->status != SS_BUILT) { return false; }
		P.radius = st->shieldRadius; P.hp = st->shieldHP; P.regen = st->shieldRegen; P.recharge = st->shieldRecharge;
		P.regenDelay = st->shieldRegenDelay; P.drop = st->shieldDrop; P.needsPower = true;
		return true;
	}
	if (o->type == OBJ_DROID)
	{
		const DROID *d = static_cast<const DROID *>(o);
		if (d->droidType != DROID_SENSOR || d->isVtol() || d->isTransporter()) { return false; }
		const SENSOR_STATS *ss = d->getSensorStats();
		if (ss == nullptr || ss->shieldRadius <= 0 || ss->shieldHP <= 0) { return false; }
		P.radius = ss->shieldRadius; P.hp = ss->shieldHP; P.regen = ss->shieldRegen; P.recharge = ss->shieldRecharge;
		P.regenDelay = ss->shieldRegenDelay; P.drop = ss->shieldDrop; P.needsPower = false;
		return true;
	}
	return false;
}

/// alle Schildquellen in fester Reihenfolge (Spieler, Bauwerke, dann Fahrzeuge) - auf allen Rechnern gleich
template<typename F>
void forEachSource(F &&f)
{
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			Params P;
			if (shieldParams(s, P)) { f(static_cast<const BASE_OBJECT *>(s), P); }
		}
		for (const DROID *d : gameWorld.objects.droids[p])
		{
			Params P;
			if (shieldParams(d, P)) { f(static_cast<const BASE_OBJECT *>(d), P); }
		}
	}
}

int64_t dropOf(const Params &P) { return int64_t(P.radius) * P.drop / 1000; }

int track(int &cache, const char *name)
{
	if (cache == -2) { cache = resPresent("WAV", name) ? audio_GetTrackID(name) : -1; }
	return cache;
}

bool passesShields(const WEAPON_STATS *w)
{
	if (w == nullptr) { return true; }
	if (w->weaponSubClass == WSC_LAS_SAT) { return true; }
	return w->id.startsWith("Nuke");   // strategische Waffen (Atomrakete, mobiler Atomwerfer)
}

namespace gpu
{
// Kuppel wie beim UEF-Schildgenerator in Supreme Commander 1: Kugel mit vier gegeneinander laufenden Texturlagen
// (modshield.frag), Einschlag als Kugelkappe mit nach aussen laufendem Ring. Texturen liegen im Mod (texpages/modshield_uef_*.png);
// fehlen sie, gibt es den Ersatz aus Modellen (draw3D).
constexpr int SPH_SEG = 32, SPH_RING = 16;        // wie Sphere01 (960 Dreiecke)
constexpr int CAP_SEG = 32, CAP_RING = 8;         // Kappe bis 22,5 Grad (wie SphereSection01)
constexpr float CAP_DEG = 22.5f;
constexpr float IMPACT_TICKS = 7.f;               // 0,2 s voll, dann in 0,5 s weg

gfx_api::texture *texDome = nullptr, *texGrain = nullptr, *texNoise = nullptr, *texHitRing = nullptr, *texHitDetail = nullptr;
gfx_api::buffer *sphPos = nullptr, *sphUV = nullptr, *sphIdx = nullptr, *capPos = nullptr, *capUV = nullptr, *capIdx = nullptr;
size_t sphCount = 0, capCount = 0;
bool tried = false, ok = false;

gfx_api::texture *loadTexture(const char *file)
{
	iV_Image img;
	if (!iV_loadImage_PNG2(file, img, true, true)) { return nullptr; }
	if (img.channels() != 4 && !img.convert_to_rgba()) { return nullptr; }
	const unsigned w = img.width(), h = img.height();
	if (w == 0 || h == 0) { return nullptr; }
	const size_t levels = 1 + static_cast<size_t>(std::floor(std::log2(static_cast<double>(std::max(w, h)))));
	// Daten (Rauschen, Masken), keine Farben: unkomprimiert und ohne sRGB-Umrechnung, damit die Vergleiche im Shader stimmen
	gfx_api::texture *t = gfx_api::context::get().create_texture(levels, w, h, gfx_api::pixel_format::FORMAT_RGBA8_UNORM_PACK8, file);
	if (t == nullptr) { return nullptr; }
	if (!t->upload(0, img)) { delete t; return nullptr; }
	unsigned cw = w, ch = h;
	for (size_t i = 1; i < levels; ++i)
	{
		cw = std::max(1u, cw >> 1); ch = std::max(1u, ch >> 1);
		if (!img.resize(static_cast<int>(cw), static_cast<int>(ch)) || !t->upload(i, img)) { delete t; return nullptr; }
	}
	return t;
}

void freeAll()
{
	for (gfx_api::texture **t : { &texDome, &texGrain, &texNoise, &texHitRing, &texHitDetail }) { delete *t; *t = nullptr; }
	for (gfx_api::buffer **b : { &sphPos, &sphUV, &sphIdx, &capPos, &capUV, &capIdx }) { delete *b; *b = nullptr; }
	sphCount = capCount = 0;
	ok = false;
}

bool makeMesh(const std::vector<glm::vec3> &pos, const std::vector<glm::vec2> &uv, const std::vector<uint16_t> &idx,
              gfx_api::buffer *&bp, gfx_api::buffer *&bu, gfx_api::buffer *&bi, const char *name)
{
	auto &ctx = gfx_api::context::get();
	bp = ctx.create_buffer_object(gfx_api::buffer::usage::vertex_buffer, gfx_api::context::buffer_storage_hint::static_draw, std::string(name) + "Pos");
	bu = ctx.create_buffer_object(gfx_api::buffer::usage::vertex_buffer, gfx_api::context::buffer_storage_hint::static_draw, std::string(name) + "UV");
	bi = ctx.create_buffer_object(gfx_api::buffer::usage::index_buffer, gfx_api::context::buffer_storage_hint::static_draw, std::string(name) + "Idx");
	if (bp == nullptr || bu == nullptr || bi == nullptr) { return false; }
	bp->upload(pos.size() * sizeof(glm::vec3), pos.data());
	bu->upload(uv.size() * sizeof(glm::vec2), uv.data());
	bi->upload(idx.size() * sizeof(uint16_t), idx.data());
	return true;
}

/// Gitter (Ringe x Segmente) zu Dreiecken; leere Dreiecke an Polen/Mitte weglassen
void gridIndices(int rings, int segs, bool degenerateFirst, bool degenerateLast, std::vector<uint16_t> &idx)
{
	for (int j = 0; j < rings; ++j)
	{
		for (int i = 0; i < segs; ++i)
		{
			const uint16_t a = static_cast<uint16_t>(j * (segs + 1) + i), b = static_cast<uint16_t>(a + 1);
			const uint16_t c = static_cast<uint16_t>(a + segs + 1), d = static_cast<uint16_t>(c + 1);
			if (!(degenerateFirst && j == 0)) { idx.insert(idx.end(), { a, c, b }); }
			if (!(degenerateLast && j == rings - 1)) { idx.insert(idx.end(), { b, c, d }); }
		}
	}
}

bool ensure()
{
	if (tried) { return ok; }
	tried = true;
	ok = false;
	texDome = loadTexture("texpages/modshield_uef_dome.png");
	texGrain = loadTexture("texpages/modshield_uef_grain.png");
	texNoise = loadTexture("texpages/modshield_uef_noise.png");
	texHitRing = loadTexture("texpages/modshield_uef_hitring.png");
	texHitDetail = loadTexture("texpages/modshield_uef_hitdetail.png");
	if (!texDome || !texGrain || !texNoise || !texHitRing || !texHitDetail)
	{
		debug(LOG_INFO, "Schildkuppel: Supreme-Commander-Texturen fehlen - Ersatz aus Modellen");
		freeAll();
		return false;
	}
	// Kugel (Radius 1): v = 0 am unteren Pol .. 1 am oberen, u rundherum
	std::vector<glm::vec3> pos;
	std::vector<glm::vec2> uv;
	std::vector<uint16_t> idx;
	for (int j = 0; j <= SPH_RING; ++j)
	{
		const float lat = (static_cast<float>(j) / SPH_RING - 0.5f) * glm::pi<float>();
		for (int i = 0; i <= SPH_SEG; ++i)
		{
			const float u = static_cast<float>(i) / SPH_SEG;
			const float ang = glm::half_pi<float>() + u * glm::two_pi<float>();
			pos.emplace_back(std::cos(lat) * std::cos(ang), std::sin(lat), std::cos(lat) * std::sin(ang));
			uv.emplace_back(u, static_cast<float>(j) / SPH_RING);
		}
	}
	gridIndices(SPH_RING, SPH_SEG, true, true, idx);
	sphCount = idx.size();
	if (!makeMesh(pos, uv, idx, sphPos, sphUV, sphIdx, "modshield::sphere")) { freeAll(); return false; }
	// Kappe um +z (Radius 1): v = 0 in der Mitte .. 1 am Rand (wie SphereSection01: 0,006 .. 0,986)
	pos.clear(); uv.clear(); idx.clear();
	for (int k = 0; k <= CAP_RING; ++k)
	{
		const float th = glm::radians(CAP_DEG) * static_cast<float>(k) / CAP_RING;
		for (int i = 0; i <= CAP_SEG; ++i)
		{
			const float u = static_cast<float>(i) / CAP_SEG;
			const float ph = u * glm::two_pi<float>();
			pos.emplace_back(std::sin(th) * std::cos(ph), std::sin(th) * std::sin(ph), std::cos(th));
			uv.emplace_back(u, 0.006f + 0.98f * static_cast<float>(k) / CAP_RING);
		}
	}
	gridIndices(CAP_RING, CAP_SEG, true, false, idx);
	capCount = idx.size();
	if (!makeMesh(pos, uv, idx, capPos, capUV, capIdx, "modshield::cap")) { freeAll(); return false; }
	ok = true;
	return true;
}

void shutdown()
{
	freeAll();
	tried = false;
}

inline float fractf(double x) { return static_cast<float>(x - std::floor(x)); }
} // namespace gpu

} // namespace

void tick()
{
	s_active.clear();
	const uint32_t dt = deltaGameTime;
	// Zustand fuer jede Schildquelle (fertiges Schild-Bauwerk, Fahrzeug mit Schild-Systemturm); verschwundene werden unten entfernt
	std::vector<uint32_t> alive;
	forEachSource([&](const BASE_OBJECT *o, const Params &P) {
		alive.push_back(o->id);
		State &S = s_state[o->id];
		if (!S.init)
		{
			// fertig gebaut: Schild steht sofort mit vollen Punkten (Supreme Commander: OnState setzt volle Gesundheit)
			S.init = true;
			S.hp = P.hp;
			S.upAt = gameTime;
		}
		// ohne Strom (nur Bauwerke) ist die Kuppel aus und behaelt ihre Punkte; kommt der Strom zurueck, geht sie nach 3 s wieder an
		const bool powered = !P.needsPower || !modgrid::isPoweredDown(static_cast<const STRUCTURE *>(o));
		if (powered && !S.powered) { S.powerBackAt = gameTime + POWER_BACK_MS; }
		S.powered = powered;
		if (!powered) { return; }
		if (S.powerBackAt != 0)
		{
			if (gameTime < S.powerBackAt) { return; }
			S.powerBackAt = 0;
			S.upAt = gameTime;
			const int t = track(s_sndUp, "modshield_up.ogg");
			if (t >= 0) { audio_PlayStaticTrack(o->pos.x, o->pos.y, t); }
		}
		// zusammengebrochen: nach der Ausfallzeit wieder an - mit vollen Punkten (Supreme Commander: DamageRechargeState)
		if (S.down)
		{
			if (gameTime < S.downUntil) { return; }
			S.down = false;
			S.hp = P.hp;
			S.frac = 0;
			S.upAt = gameTime;
			const int t = track(s_sndUp, "modshield_up.ogg");
			if (t >= 0) { audio_PlayStaticTrack(o->pos.x, o->pos.y, t); }
		}
		// aufladen erst, wenn eine Weile kein Treffer kam (ganzzahlig, Rest in Tausendsteln)
		if (S.hp < P.hp && P.regen > 0 && gameTime - S.lastDamage >= static_cast<uint32_t>(P.regenDelay))
		{
			S.frac += P.regen * static_cast<int>(dt);
			if (S.frac >= 1000) { S.hp += S.frac / 1000; S.frac %= 1000; }
			S.hp = std::min(S.hp, P.hp);
		}
		if (S.hp > 0)
		{
			s_active.push_back({o, o->pos.x, o->pos.y, o->pos.z - dropOf(P), P.radius, o->pos.z, modlayer::layerOf(o), P});
		}
	});
	std::sort(alive.begin(), alive.end());
	for (auto it = s_state.begin(); it != s_state.end();)
	{
		if (!std::binary_search(alive.begin(), alive.end(), it->first)) { it = s_state.erase(it); }
		else { ++it; }
	}
}

bool tryAbsorb(PROJECTILE *proj)
{
	if (s_active.empty() || proj == nullptr || passesShields(proj->psWStats)) { return false; }
	const int owner = static_cast<int>(proj->player);
	const modlayer::Layer layer = modlayer::projectileLayer(proj);
	const Vector3i a = proj->prevSpacetime.pos, b = proj->pos;
	const int64_t dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
	const int64_t dd = dx * dx + dy * dy + dz * dz;
	const Active *best = nullptr;
	int64_t bestT = 0;
	for (const Active &A : s_active)
	{
		const int sp = static_cast<int>(A.s->player);
		if (sp == owner || aiCheckAlliancesInRange(sp, owner) || A.layer != layer) { continue; }
		// grobe Vorpruefung (Rechteck um Kuppel und Strecke)
		if (std::max<int64_t>(a.x, b.x) < A.cx - A.r || std::min<int64_t>(a.x, b.x) > A.cx + A.r) { continue; }
		if (std::max<int64_t>(a.y, b.y) < A.cy - A.r || std::min<int64_t>(a.y, b.y) > A.cy + A.r) { continue; }
		const int64_t r2 = A.r * A.r;
		const int64_t ax = a.x - A.cx, ay = a.y - A.cy, az = a.z - A.cz;
		if (ax * ax + ay * ay + az * az <= r2) { continue; }   // von innen abgefeuert: fliegt hinaus
		// naechster Punkt der Strecke an der Kuppelmitte (t = tn / dd, auf [0,1] begrenzt)
		int64_t tn = -(ax * dx + ay * dy + az * dz);
		if (dd <= 0) { continue; }
		tn = std::clamp<int64_t>(tn, 0, dd);
		const int64_t qx = ax * dd + tn * dx, qy = ay * dd + tn * dy, qz = az * dd + tn * dz;   // (Q - C) * dd
		// |Q - C|^2 <= r^2  <=>  q^2 <= r^2 * dd^2 ; in Gleitkomma-freier Form mit 128-Bit-Ersatz: erst durch dd teilen
		const int64_t qxs = qx / dd, qys = qy / dd, qzs = qz / dd;
		if (qxs * qxs + qys * qys + qzs * qzs > r2) { continue; }
		if (A.cz + qzs < A.gz - 16) { continue; }                 // unter der Erde: keine Kuppel
		if (best == nullptr || tn < bestT || (tn == bestT && A.s->id < best->s->id)) { best = &A; bestT = tn; }
	}
	if (best == nullptr) { return false; }
	const WEAPON_STATS *w = proj->psWStats;
	const int dmg = std::max(1, std::max(weaponDamage(*w, owner), static_cast<int>(w->upgrade[owner].radiusDamage)));
	State &S = s_state[best->s->id];
	S.hp -= dmg;
	S.lastHit = gameTime;
	S.lastDamage = gameTime;
	S.frac = 0;
	const Params &BP = best->P;
	if (S.hp <= 0)
	{
		S.hp = 0;
		S.frac = 0;
		S.down = true;
		S.downUntil = gameTime + static_cast<uint32_t>(std::max(1000, BP.recharge));
		S.downAt = gameTime;
		const int t = track(s_sndDown, "modshield_down.ogg");
		if (t >= 0) { audio_PlayStaticTrack(best->s->pos.x, best->s->pos.y, t); }
	}
	else
	{
		const int t = track(s_sndHit, "modshield_hit.ogg");
		if (t >= 0) { audio_PlayStaticTrack(best->s->pos.x, best->s->pos.y, t); }
	}
	// Anzeige: Einschlagpunkt auf der Kuppel (Gleitkomma, nur fuer das Bild)
	{
		const glm::vec3 A0(static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(a.z));
		const glm::vec3 D(static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz));
		const glm::vec3 C(static_cast<float>(best->cx), static_cast<float>(best->cy), static_cast<float>(best->cz));
		const float R = static_cast<float>(best->r);
		const glm::vec3 m = A0 - C;
		const float qa = glm::dot(D, D), qb = 2.f * glm::dot(m, D), qc = glm::dot(m, m) - R * R;
		float t = 0.f;
		const float disc = qb * qb - 4.f * qa * qc;
		if (qa > 0.f && disc >= 0.f) { t = std::clamp((-qb - std::sqrt(disc)) / (2.f * qa), 0.f, 1.f); }
		const glm::vec3 P = A0 + D * t;
		glm::vec3 n = P - C;
		const float nl = glm::length(n);
		n = nl > 1e-3f ? n / nl : glm::vec3(0.f, 0.f, 1.f);
		Hit h;
		h.p = glm::vec3(P.x, P.z, -P.y);
		h.n = glm::vec3(n.x, n.z, -n.y);
		h.c = glm::vec3(C.x, C.z, -C.y);
		h.r = R;
		h.t = graphicsTime;
		h.shieldId = best->s->id;
		h.size = std::clamp(18.f + static_cast<float>(dmg) * 0.35f, 18.f, 90.f);
		if (s_hits.size() > 96) { s_hits.erase(s_hits.begin()); }
		s_hits.push_back(h);
	}
	return true;
}

int shieldPercent(const BASE_OBJECT *o)
{
	Params P;
	if (!shieldParams(o, P)) { return -1; }
	const auto it = s_state.find(o->id);
	if (it == s_state.end()) { return 0; }
	return std::clamp(it->second.hp * 100 / std::max(1, P.hp), 0, 100);
}

void draw3D(const glm::mat4 &viewMatrix)
{
	// Kuppel wie Supreme Commander (eigenes Grafikprogramm) - wird im Durchsichtigen-Durchgang gezeichnet (drawDomes)
	if (gpu::ensure()) { return; }
	// Ersatz, falls die Texturen fehlen: Kuppel aus Modellen (Fliess-Muster, Lichtrand, Bodensaum)
	// Lichtrand-Varianten (shields.py): Bogen endet 0, 3, 6 .. 90 Grad unter der Waagrechten seiner Ebene, dazu ein geschlossener Ring
	constexpr int EDGE_VARIANTS = 31;
	constexpr float EDGE_STEP_DEG = 3.f;
	static iIMDBaseShape *dome = nullptr, *ring = nullptr, *flash = nullptr, *edgeFull = nullptr;
	static iIMDBaseShape *edges[EDGE_VARIANTS] = {};
	static bool tried = false;
	static unsigned gen = 0;
	if (modelCacheStale(gen)) { tried = false; dome = ring = flash = edgeFull = nullptr; std::fill(std::begin(edges), std::end(edges), nullptr); }
	if (!tried)
	{
		tried = true;
		dome = modelGet("modshielddome.pie");
		ring = modelGet("modshieldrim.pie");
		flash = modelGet("modshieldhit.pie");
		edgeFull = modelGet("modshieldedgefull.pie");
		for (int k = 0; k < EDGE_VARIANTS; ++k)
		{
			char nm[32];
			snprintf(nm, sizeof(nm), "modshieldedge%02d.pie", k);
			edges[k] = modelGet(nm);
		}
	}
	if (dome == nullptr || s_state.empty()) { return; }
	const float now = static_cast<float>(graphicsTime);
	const glm::vec3 cam = glm::vec3(glm::inverse(viewMatrix)[3]);   // Kameraposition (Welt)
	forEachSource([&](const BASE_OBJECT *s, const Params &P) {
			if (!s->visibleForLocalDisplay() || modlayer::hiddenInView(s)) { return; }
			const auto it = s_state.find(s->id);
			if (it == s_state.end()) { return; }
			const State &S = it->second;
			const float frac = static_cast<float>(S.hp) / static_cast<float>(std::max(1, P.hp));
			// Zusammenbruch: kurzes helles Aufflackern, dann aus; Aufbau: von unten "einblenden"
			float collapse = 0.f;
			if (S.down)
			{
				const float since = now - static_cast<float>(S.downAt);
				if (since > 650.f) { return; }
				collapse = 1.f - since / 650.f;
			}
			else if (S.hp <= 0 || !S.powered || S.powerBackAt != 0) { return; }
			const float R = static_cast<float>(P.radius);
			const PIELIGHT team = pal_GetTeamColour(getPlayerColour(s->player));
			// Kuppelfarbe: helles Blau mit etwas Spielerfarbe
			const int cr = std::clamp(70 + team.byte.r / 4, 0, 255), cg = std::clamp(150 + team.byte.g / 6, 0, 255), cb = 255;
			const PIELIGHT tint = pal_RGBA(static_cast<UBYTE>(cr), static_cast<UBYTE>(cg), static_cast<UBYTE>(cb), 255);
			// Helligkeit: schwach im Ruhezustand, heller bei Treffern, flackert bei wenig Punkten
			const float sinceHit = now - static_cast<float>(S.lastHit);
			const float hitBoost = S.lastHit != 0 && sinceHit < 450.f ? (1.f - sinceHit / 450.f) : 0.f;
			float flick = 1.f;
			if (frac < 0.25f) { flick = 0.65f + 0.35f * std::fabs(std::sin(now * 0.021f + static_cast<float>(s->id))); }
			const float sinceUp = now - static_cast<float>(S.upAt);
			const float grow = S.upAt != 0 && sinceUp < 900.f ? std::max(0.05f, sinceUp / 900.f) : 1.f;
			int alpha = static_cast<int>((34.f + 46.f * hitBoost + 30.f * frac) * flick + 200.f * collapse);
			alpha = std::clamp(alpha, 0, 255);
			const glm::vec3 cGround(static_cast<float>(s->pos.x), static_cast<float>(s->pos.z), -static_cast<float>(s->pos.y));   // Boden
			const glm::vec3 c = cGround - glm::vec3(0.f, R * static_cast<float>(P.drop) / 1000.f, 0.f);   // Kugelmitte
			const float rr = R / 100.f;   // Modelle haben Radius 100
			// Kuppel: langsam drehend (Wabenmuster wandert), zweite Lage gegenlaeufig etwas groesser
			const float yaw = now * 0.0060f + static_cast<float>(s->id % 360);
			glm::mat4 m = glm::translate(glm::mat4(1.f), c) * glm::rotate(glm::mat4(1.f), glm::radians(yaw), glm::vec3(0.f, 1.f, 0.f))
			              * glm::scale(glm::mat4(1.f), glm::vec3(rr, rr * grow, rr));
			pie_Draw3DShape(dome->displayModel(), 0, 0, tint, pie_ADDITIVE, alpha, m, viewMatrix, 0.f, true);
			glm::mat4 m2 = glm::translate(glm::mat4(1.f), c) * glm::rotate(glm::mat4(1.f), glm::radians(-yaw * 0.7f + 30.f), glm::vec3(0.f, 1.f, 0.f))
			               * glm::scale(glm::mat4(1.f), glm::vec3(rr * 1.012f, rr * 1.012f * grow, rr * 1.012f));
			pie_Draw3DShape(dome->displayModel(), 0, 0, tint, pie_ADDITIVE, std::clamp(alpha * 2 / 3, 0, 255), m2, viewMatrix, 0.f, true);
			// heller Saum am Boden (wo die Kuppel auf die Erde trifft)
			if (ring != nullptr)
			{
				const float dr = static_cast<float>(P.drop) / 1000.f;
				const float rg = rr * std::sqrt(std::max(0.f, 1.f - dr * dr));   // Bodenkreis der Kugel
				glm::mat4 mr = glm::translate(glm::mat4(1.f), cGround + glm::vec3(0.f, 2.f, 0.f)) * glm::scale(glm::mat4(1.f), glm::vec3(rg, 1.f, rg));
				pie_Draw3DShape(ring->displayModel(), 0, 0, tint, pie_ADDITIVE, std::clamp(alpha * 3 / 2 + 30, 0, 255), mr, viewMatrix, 0.f, true);
			}
			// Lichtrand wie in Supreme Commander: eine Energieblase leuchtet an ihrem Umriss, weil man dort flach durch ihre Haut schaut.
			// Der Umriss einer Kugel (Radius R, Abstand D zur Kamera) ist ein Kreis mit Radius R*sqrt(1-R^2/D^2), senkrecht zur
			// Blickrichtung, um R^2/D zur Kamera hin verschoben. Darauf liegt ein zur Kamera gedrehter Bogen; wo dieser Kreis den Boden
			// schneidet, endet der Bogen weich (Variante nach dem Winkel). Was trotzdem unter dem Gelaende liegt, verdeckt das Gelaende.
			const glm::vec3 toC = c - cam;
			const float D = glm::length(toC);
			if (D > R * 1.15f && (edges[0] != nullptr || edgeFull != nullptr))
			{
				const glm::vec3 dn = toC / D;
				glm::vec3 right = glm::cross(dn, glm::vec3(0.f, 1.f, 0.f));
				const float rl = glm::length(right);
				right = rl < 1e-3f ? glm::vec3(1.f, 0.f, 0.f) : right / rl;
				glm::vec3 upv = glm::cross(right, dn);
				if (upv.y < 0.f) { upv = -upv; }
				const glm::vec3 cc = c - dn * (R * R / D);
				const float rs = R * std::sqrt(std::max(0.f, 1.f - (R / D) * (R / D)));
				const float denom = rs * upv.y;
				const float sa = denom > 1e-3f ? -(cc.y - cGround.y) / denom : -2.f;   // sin des Winkels, an dem der Bogen den Boden trifft
				iIMDBaseShape *shape = nullptr;
				if (sa <= -0.999f) { shape = edgeFull; }
				else
				{
					const float a0 = -glm::degrees(std::asin(std::clamp(sa, -1.f, 0.f)));
					shape = edges[std::clamp(static_cast<int>(std::lround(a0 / EDGE_STEP_DEG)), 0, EDGE_VARIANTS - 1)];
				}
				if (shape != nullptr)
				{
					glm::mat4 rot(1.f);
					rot[0] = glm::vec4(right, 0.f);
					rot[1] = glm::vec4(upv, 0.f);
					rot[2] = glm::vec4(glm::cross(right, upv), 0.f);
					const float es = rs / 100.f;
					const glm::mat4 me = glm::translate(glm::mat4(1.f), cc) * rot * glm::scale(glm::mat4(1.f), glm::vec3(es, es, es));
					const float pulse = 0.92f + 0.08f * std::sin(now * 0.0035f + static_cast<float>(s->id % 97));
					const int ea = static_cast<int>(static_cast<float>(alpha) * 1.5f * pulse * grow * grow);
					pie_Draw3DShape(shape->displayModel(), 0, 0, tint, pie_ADDITIVE, std::clamp(ea, 0, 255), me, viewMatrix, 0.f, true);
				}
			}
	});
	// Einschlaege: Aufblitzen auf der Kuppelwand (zur Wand ausgerichtet), waechst und verblasst in 0,45 s
	if (flash != nullptr)
	{
		s_hits.erase(std::remove_if(s_hits.begin(), s_hits.end(), [&](const Hit &h) { return now - static_cast<float>(h.t) > 450.f; }), s_hits.end());
		for (const Hit &h : s_hits)
		{
			const float k = (now - static_cast<float>(h.t)) / 450.f;
			const float sc = h.size * (0.6f + 1.2f * k) / 100.f;
			// Modell liegt in der x-z-Ebene (Normale +y): auf die Kuppel-Normale drehen
			const glm::vec3 up(0.f, 1.f, 0.f);
			glm::vec3 axis = glm::cross(up, h.n);
			const float al = glm::length(axis);
			glm::mat4 rot(1.f);
			if (al > 1e-4f) { rot = glm::rotate(glm::mat4(1.f), std::acos(std::clamp(glm::dot(up, h.n), -1.f, 1.f)), axis / al); }
			const glm::mat4 m = glm::translate(glm::mat4(1.f), h.p) * rot * glm::scale(glm::mat4(1.f), glm::vec3(sc, sc, sc));
			const int alpha = static_cast<int>(255.f * (1.f - k));
			pie_Draw3DShape(flash->displayModel(), 0, 0, pal_RGBA(170, 220, 255, 255), pie_ADDITIVE, alpha, m, viewMatrix, 0.f, true);
		}
	}
}

void drawDomes(const glm::mat4 &perspectiveMatrix, const glm::mat4 &viewMatrix)
{
	if (s_state.empty() || !gpu::ensure()) { return; }
	const double tk = static_cast<double>(graphicsTime) / 100.0;   // Zeit in Supreme-Commander-Takten (1/10 s)
	const auto &rs = getCurrentRenderState();
	const bool linear = gfx_api::context::get().linearLightingEnabled();
	const glm::vec4 fogColor = pie_SceneColourToVec4(pie_GetFogColour());
	const glm::vec4 fogRange(rs.fogBegin, rs.fogEnd, pie_GetFogEnabled() ? 1.f : 0.f, linear ? 1.f : 0.f);
	gfx_api::constant_buffer_type<SHADER_MODSHIELD> cb {};
	cb.fogColor = fogColor;
	cb.fogRange = fogRange;
	cb.params2 = glm::vec4(1.f, linear ? 1.f : 0.f, 0.f, 0.f);
	// Kuppeln: Lagen-Verschiebung je Takt wie beim UEF-Schild (Lage 2: 0,0003/0,005, Lage 3: -0,001/-0,005, Lage 4: -0,0003/-0,0008)
	cb.shiftA = glm::vec4(gpu::fractf(tk * 0.0003), gpu::fractf(tk * 0.005), gpu::fractf(tk * -0.001), gpu::fractf(tk * -0.005));
	cb.shiftB = glm::vec4(gpu::fractf(tk * -0.0003), gpu::fractf(tk * -0.0008), 0.f, 0.f);
	const float pulseSlow = std::sin(gpu::fractf(0.01 * tk) * glm::pi<float>());
	const float pulseFast = std::sin(gpu::fractf(0.06 * tk) * glm::pi<float>());
	auto &ctx = gfx_api::context::get();
	bool bound = false;
	forEachSource([&](const BASE_OBJECT *s, const Params &P) {
			if (!s->visibleForLocalDisplay() || modlayer::hiddenInView(s)) { return; }
			const auto it = s_state.find(s->id);
			if (it == s_state.end()) { return; }
			const State &S = it->second;
			if (S.down || S.hp <= 0 || !S.powered || S.powerBackAt != 0) { return; }   // aus: wie dort einfach weg
			const float R = static_cast<float>(P.radius);
			const glm::vec3 c(static_cast<float>(s->pos.x), static_cast<float>(s->pos.z) - R * static_cast<float>(P.drop) / 1000.f,
			                  -static_cast<float>(s->pos.y));
			const glm::mat4 model = glm::translate(glm::mat4(1.f), c) * glm::scale(glm::mat4(1.f), glm::vec3(R, R, R));
			const glm::mat4 mv = viewMatrix * model;
			cb.ModelViewProjectionMatrix = perspectiveMatrix * mv;
			cb.ModelViewMatrix = mv;
			const float frac = std::clamp(static_cast<float>(S.hp) / static_cast<float>(std::max(1, P.hp)), 0.f, 1.f);
			cb.params = glm::vec4(pulseSlow, pulseFast, frac, 0.f);
			if (!bound)
			{
				gfx_api::ModShieldDomePSO::get().bind();
				gfx_api::ModShieldDomePSO::get().bind_textures(gpu::texDome, gpu::texGrain, gpu::texNoise);
				ctx.bind_index_buffer(*gpu::sphIdx, gfx_api::index_type::u16);
				gfx_api::ModShieldDomePSO::get().bind_vertex_buffers(gpu::sphPos, gpu::sphUV);
				bound = true;
			}
			gfx_api::ModShieldDomePSO::get().bind_constants(cb);
			gfx_api::ModShieldDomePSO::get().draw_elements(gpu::sphCount, 0);
	});
	if (bound)
	{
		gfx_api::ModShieldDomePSO::get().unbind_vertex_buffers(gpu::sphPos, gpu::sphUV);
		ctx.unbind_index_buffer(*gpu::sphIdx);
	}
	// Einschlaege: Kugelkappe um den Treffpunkt, Ring laeuft nach aussen (additiv)
	const uint32_t maxAge = static_cast<uint32_t>(gpu::IMPACT_TICKS * 100.f);
	s_hits.erase(std::remove_if(s_hits.begin(), s_hits.end(), [&](const Hit &h) { return graphicsTime - h.t > maxAge; }), s_hits.end());
	if (s_hits.empty()) { return; }
	bound = false;
	for (const Hit &h : s_hits)
	{
		const float ti = static_cast<float>(graphicsTime - h.t) / 100.f;
		// z-Achse der Kappe auf die Trefferrichtung drehen
		const glm::vec3 z = h.n;
		const glm::vec3 helper = std::fabs(z.y) < 0.95f ? glm::vec3(0.f, 1.f, 0.f) : glm::vec3(1.f, 0.f, 0.f);
		const glm::vec3 x = glm::normalize(glm::cross(helper, z));
		const glm::vec3 y = glm::cross(z, x);
		glm::mat4 rot(1.f);
		rot[0] = glm::vec4(x, 0.f); rot[1] = glm::vec4(y, 0.f); rot[2] = glm::vec4(z, 0.f);
		const glm::mat4 model = glm::translate(glm::mat4(1.f), h.c) * rot * glm::scale(glm::mat4(1.f), glm::vec3(h.r, h.r, h.r));
		const glm::mat4 mv = viewMatrix * model;
		cb.ModelViewProjectionMatrix = perspectiveMatrix * mv;
		cb.ModelViewMatrix = mv;
		cb.shiftA = glm::vec4(-0.003f * ti, -0.15f + (-0.1f + 0.015f * ti / 2.f) * ti, 0.f, 0.f);
		cb.shiftB = glm::vec4(0.f);
		cb.params = glm::vec4(0.f, 0.f, 1.f, 1.f);
		cb.params2 = glm::vec4(std::clamp(1.f - (ti - 2.f) * 0.2f, 0.f, 1.f), linear ? 1.f : 0.f, 0.f, 0.f);
		if (!bound)
		{
			gfx_api::ModShieldImpactPSO::get().bind();
			gfx_api::ModShieldImpactPSO::get().bind_textures(gpu::texHitRing, gpu::texHitRing, gpu::texHitDetail);
			ctx.bind_index_buffer(*gpu::capIdx, gfx_api::index_type::u16);
			gfx_api::ModShieldImpactPSO::get().bind_vertex_buffers(gpu::capPos, gpu::capUV);
			bound = true;
		}
		gfx_api::ModShieldImpactPSO::get().bind_constants(cb);
		gfx_api::ModShieldImpactPSO::get().draw_elements(gpu::capCount, 0);
	}
	if (bound)
	{
		gfx_api::ModShieldImpactPSO::get().unbind_vertex_buffers(gpu::capPos, gpu::capUV);
		ctx.unbind_index_buffer(*gpu::capIdx);
	}
}

void gpuShutdown()
{
	gpu::shutdown();
}

void saveState(std::vector<int> &out)
{
	out.clear();
	for (const auto &kv : s_state)
	{
		const State &S = kv.second;
		out.push_back(static_cast<int>(kv.first));
		out.push_back(S.hp);
		out.push_back(S.down ? static_cast<int>(S.downUntil > gameTime ? S.downUntil - gameTime : 0) : -1);
	}
}

void loadState(const std::vector<int> &in)
{
	s_state.clear();
	for (size_t i = 0; i + 2 < in.size(); i += 3)
	{
		State S;
		S.init = true;
		S.hp = std::max(0, in[i + 1]);
		if (in[i + 2] >= 0) { S.down = true; S.downUntil = gameTime + static_cast<uint32_t>(in[i + 2]); }
		s_state[static_cast<uint32_t>(in[i])] = S;
	}
}

void reset()
{
	s_state.clear();
	s_active.clear();
	s_hits.clear();
	s_sndHit = s_sndDown = s_sndUp = -2;
}
} // namespace modshield
