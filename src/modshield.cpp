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

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace modshield
{
namespace
{
struct State
{
	int hp = 0;                 // aktuelle Schildpunkte
	int frac = 0;               // Rest der Aufladung (Tausendstel)
	bool down = false;          // zusammengebrochen
	uint32_t downUntil = 0;     // Ausfall bis (Spielzeit)
	bool powered = true;
	// nur Anzeige
	uint32_t lastHit = 0;
	uint32_t downAt = 0;
	uint32_t upAt = 0;
};

struct Active
{
	const STRUCTURE *s;
	int64_t cx, cy, cz, r;
	modlayer::Layer layer;
};

struct Hit
{
	glm::vec3 p;                // Einschlagpunkt (Welt, Anzeige-Koordinaten x, Hoehe, -y)
	glm::vec3 n;                // nach aussen
	uint32_t t;                 // Anzeigezeit
	uint32_t shieldId;
	float size;
};

std::map<uint32_t, State> s_state;          // nach Bau-ID geordnet (gleiche Reihenfolge auf allen Rechnern)
std::vector<Active> s_active;
std::vector<Hit> s_hits;
int s_sndHit = -2, s_sndDown = -2, s_sndUp = -2;

bool hasShieldStats(const STRUCTURE *s)
{
	return s != nullptr && s->pStructureType != nullptr && s->pStructureType->shieldRadius > 0 && s->pStructureType->shieldHP > 0;
}

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
} // namespace

void tick()
{
	s_active.clear();
	const uint32_t dt = deltaGameTime;
	// Zustand fuer jedes fertige Schild-Bauwerk; verschwundene Bauwerke werden unten entfernt
	std::vector<uint32_t> alive;
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died || s->status != SS_BUILT || !hasShieldStats(s)) { continue; }
			const STRUCTURE_STATS *st = s->pStructureType;
			alive.push_back(s->id);
			State &S = s_state[s->id];
			const bool powered = !modgrid::isPoweredDown(s);
			if (powered && !S.powered) { S.upAt = gameTime; }
			S.powered = powered;
			if (S.down && gameTime >= S.downUntil)
			{
				S.down = false;
				S.hp = 0;
				S.upAt = gameTime;
				const int t = track(s_sndUp, "modshield_up.ogg");
				if (t >= 0) { audio_PlayStaticTrack(s->pos.x, s->pos.y, t); }
			}
			if (!powered)
			{
				// ohne Strom bricht die Kuppel langsam zusammen (10 % je Sekunde)
				S.hp = std::max(0, S.hp - static_cast<int>(int64_t(st->shieldHP) * dt / 10000));
				continue;
			}
			if (S.down) { continue; }
			// aufladen (ganzzahlig, Rest in Tausendsteln)
			S.frac += st->shieldRegen * static_cast<int>(dt);
			if (S.frac >= 1000) { S.hp += S.frac / 1000; S.frac %= 1000; }
			S.hp = std::min(S.hp, st->shieldHP);
			if (S.hp > 0)
			{
				s_active.push_back({s, s->pos.x, s->pos.y, s->pos.z, st->shieldRadius, modlayer::layerOf(s)});
			}
		}
	}
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
		if (A.cz + qzs < A.cz - 16) { continue; }                 // unter der Erde: keine Kuppel
		if (best == nullptr || tn < bestT || (tn == bestT && A.s->id < best->s->id)) { best = &A; bestT = tn; }
	}
	if (best == nullptr) { return false; }
	const WEAPON_STATS *w = proj->psWStats;
	const int dmg = std::max(1, std::max(weaponDamage(*w, owner), static_cast<int>(w->upgrade[owner].radiusDamage)));
	State &S = s_state[best->s->id];
	S.hp -= dmg;
	S.lastHit = gameTime;
	const STRUCTURE_STATS *st = best->s->pStructureType;
	if (S.hp <= 0)
	{
		S.hp = 0;
		S.frac = 0;
		S.down = true;
		S.downUntil = gameTime + static_cast<uint32_t>(std::max(1000, st->shieldRecharge));
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
		h.t = graphicsTime;
		h.shieldId = best->s->id;
		h.size = std::clamp(18.f + static_cast<float>(dmg) * 0.35f, 18.f, 90.f);
		if (s_hits.size() > 96) { s_hits.erase(s_hits.begin()); }
		s_hits.push_back(h);
	}
	return true;
}

int shieldPercent(const STRUCTURE *s)
{
	if (!hasShieldStats(s) || s->status != SS_BUILT) { return -1; }
	const auto it = s_state.find(s->id);
	if (it == s_state.end()) { return 0; }
	return std::clamp(it->second.hp * 100 / std::max(1, s->pStructureType->shieldHP), 0, 100);
}

void draw3D(const glm::mat4 &viewMatrix)
{
	static iIMDBaseShape *dome = nullptr, *ring = nullptr, *flash = nullptr;
	static bool tried = false;
	static unsigned gen = 0;
	if (modelCacheStale(gen)) { tried = false; dome = ring = flash = nullptr; }
	if (!tried)
	{
		tried = true;
		dome = modelGet("modshielddome.pie");
		ring = modelGet("modshieldrim.pie");
		flash = modelGet("modshieldhit.pie");
	}
	if (dome == nullptr || s_state.empty()) { return; }
	const float now = static_cast<float>(graphicsTime);
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died || s->status != SS_BUILT || !hasShieldStats(s)) { continue; }
			if (!s->visibleForLocalDisplay() || modlayer::hiddenInView(s)) { continue; }
			const auto it = s_state.find(s->id);
			if (it == s_state.end()) { continue; }
			const State &S = it->second;
			const STRUCTURE_STATS *st = s->pStructureType;
			const float frac = static_cast<float>(S.hp) / static_cast<float>(std::max(1, st->shieldHP));
			// Zusammenbruch: kurzes helles Aufflackern, dann aus; Aufbau: von unten "einblenden"
			float collapse = 0.f;
			if (S.down)
			{
				const float since = now - static_cast<float>(S.downAt);
				if (since > 650.f) { continue; }
				collapse = 1.f - since / 650.f;
			}
			else if (S.hp <= 0) { continue; }
			const float R = static_cast<float>(st->shieldRadius);
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
			const glm::vec3 c(static_cast<float>(s->pos.x), static_cast<float>(s->pos.z), -static_cast<float>(s->pos.y));
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
				glm::mat4 mr = glm::translate(glm::mat4(1.f), c + glm::vec3(0.f, 2.f, 0.f)) * glm::scale(glm::mat4(1.f), glm::vec3(rr, 1.f, rr));
				pie_Draw3DShape(ring->displayModel(), 0, 0, tint, pie_ADDITIVE, std::clamp(alpha * 3 / 2 + 30, 0, 255), mr, viewMatrix, 0.f, true);
			}
		}
	}
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
