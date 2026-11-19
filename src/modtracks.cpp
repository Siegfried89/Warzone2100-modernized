// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Fahrspuren (siehe modtracks.h) - Abdruck aus der WZ-Bodenkachel "Panzerspuren" (Arizona/Rockies Kachel 51)
#include "modtracks.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/piepalette.h"
#include "component.h"
#include "droid.h"
#include "game_world.h"
#include "loop.h"
#include "map.h"
#include "modlayer.h"
#include "objmem.h"
#include "stats.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <vector>

namespace modtracks
{
namespace
{
enum Kind : uint8_t { TANK = 0, HALF = 1, TYRE = 2 };

constexpr float SEG = 16.f;                 // Abschnittslaenge (Welteinheiten) = ein Viertel der WZ-Kettenkachel (Muster 64 Einheiten)
constexpr size_t MAX_MARKS = 20000;         // Ringspeicher (je Laufwerk ein Eintrag); bei sehr vielen Fahrzeugen verschwinden die aeltesten zuerst
constexpr float DRAW_RANGE = 5200.f;        // weiter weg nicht zeichnen
constexpr uint32_t LIFE_MS[3] = { 120000, 100000, 80000 };   // Kette, Halbkette, Reifen

struct Mark
{
	glm::vec3 c;            // Mitte (Anzeige-Koordinaten: x, Hoehe, -y)
	glm::vec3 right, up, fwd;
	float width, len;
	float alpha;            // 0..1 (Strasse/Beton schwaecher)
	uint32_t t;
	uint8_t kind;
	uint8_t layer;
	uint8_t phase;          // welches Viertel der WZ-Kachel (fortlaufend je Fahrzeug -> nahtloses Muster)
};

struct Last
{
	int32_t x, y;
	uint32_t seen;
	uint8_t phase;
};

std::vector<Mark> s_marks;
size_t s_head = 0;
std::unordered_map<uint32_t, Last> s_last;
uint32_t s_lastPrune = 0;

float heightAt(float x, float y)
{
	const int ix = std::clamp(static_cast<int>(x), 0, gameWorld.map.width * TILE_UNITS - 1);
	const int iy = std::clamp(static_cast<int>(y), 0, gameWorld.map.height * TILE_UNITS - 1);
	return static_cast<float>(map_Height(gameWorld.map, ix, iy));
}

/// Untergrund: 0 = keine Spur (Wasser, Klippe), sonst Deckkraft
float groundFactor(float x, float y)
{
	const int ix = std::clamp(static_cast<int>(x), 0, gameWorld.map.width * TILE_UNITS - 1);
	const int iy = std::clamp(static_cast<int>(y), 0, gameWorld.map.height * TILE_UNITS - 1);
	const MAPTILE *t = worldTile(gameWorld.map, ix, iy);
	if (t == nullptr) { return 0.f; }
	switch (terrainType(t))
	{
	case TER_WATER:
	case TER_CLIFFFACE:
		return 0.f;
	case TER_ROAD:
		return 0.3f;      // Strasse / Beton: nur ein Hauch Gummi und Staub
	case TER_PINKROCK:
	case TER_RUBBLE:
	case TER_SHEETICE:
		return 0.5f;
	default:
		return 1.f;
	}
}

void push(const Mark &m)
{
	if (s_marks.size() < MAX_MARKS) { s_marks.push_back(m); return; }
	s_marks[s_head] = m;
	s_head = (s_head + 1) % MAX_MARKS;
}

/// ein Abschnitt fuer ein Laufwerk: Mitte (Welt x/y), Richtung, Breite
void addMark(float wx, float wy, float dirX, float dirY, float len, float width, Kind kind, uint8_t layer, uint8_t phase)
{
	const float gf = groundFactor(wx, wy);
	if (gf <= 0.f) { return; }
	// Gelaende-Neigung aus den Nachbarhoehen; die Spur schmiegt sich an
	const float h = heightAt(wx, wy);
	const float dhdx = (heightAt(wx + 8.f, wy) - heightAt(wx - 8.f, wy)) / 16.f;
	const float dhdy = (heightAt(wx, wy + 8.f) - heightAt(wx, wy - 8.f)) / 16.f;
	glm::vec3 up = glm::normalize(glm::vec3(-dhdx, 1.f, dhdy));                         // Anzeige: (x, Hoehe, -y)
	glm::vec3 fwd = glm::normalize(glm::vec3(dirX, dirX * dhdx + dirY * dhdy, -dirY));
	glm::vec3 right = glm::normalize(glm::cross(fwd, up));
	up = glm::normalize(glm::cross(right, fwd));
	Mark m;
	m.c = glm::vec3(wx, h + 1.2f, -wy) + up * 0.3f;
	m.right = right; m.up = up; m.fwd = fwd;
	m.width = width;
	m.len = len;
	m.alpha = gf;
	m.t = graphicsTime;
	m.kind = static_cast<uint8_t>(kind);
	m.layer = layer;
	m.phase = static_cast<uint8_t>(phase & 3);
	push(m);
}

void emit(const DROID *d, float x0, float y0, float x1, float y1, uint8_t phase)
{
	const PROPULSION_STATS *ps = d->getPropulsionStats();
	if (ps == nullptr) { return; }
	Kind kind;
	switch (ps->propulsionType)
	{
	case PROPULSION_TYPE_TRACKED: kind = TANK; break;
	case PROPULSION_TYPE_HALF_TRACKED: kind = HALF; break;
	case PROPULSION_TYPE_WHEELED: kind = TYRE; break;
	default: return;
	}
	float lc = 0.f, lw = 0.f, rc = 0.f, rw = 0.f;
	if (!modDroidPropulsionSpan(d, lc, lw, rc, rw)) { return; }
	const float dx = x1 - x0, dy = y1 - y0;
	const float dist = std::sqrt(dx * dx + dy * dy);
	if (dist < 1.f) { return; }
	const float ux = dx / dist, uy = dy / dist;
	// quer zur Fahrtrichtung (in Weltkoordinaten); welche Seite links ist, spielt keine Rolle (beide Laufwerke)
	const float qx = uy, qy = -ux;
	const float mx = 0.5f * (x0 + x1), my = 0.5f * (y0 + y1);
	// Spurbreite: Kette/Halbkette wie das Laufwerk (die WZ-Abdruck-Textur hat links und rechts einen schmalen leeren Rand, der sichtbare
	// Abdruck ist damit gut 85 % des Laufwerks), Reifen etwas schmaler als das Rad-Modell (Profil, nicht Felge)
	const float k = kind == TYRE ? 0.85f : 1.0f;
	const uint8_t layer = static_cast<uint8_t>(modlayer::layerOf(d));
	for (const auto &side : { std::make_pair(lc, lw), std::make_pair(rc, rw) })
	{
		const float off = side.first, w = std::max(2.f, side.second * k);
		addMark(mx + qx * off, my + qy * off, ux, uy, dist, w, kind, layer, phase);
	}
}

/// Abschnittslaenge relativ zur WZ-Kachel: die Kachel zeigt eine Kette von ~11 Einheiten Breite auf 16 Einheiten je Viertel
float segScale(const DROID *d)
{
	float lc = 0.f, lw = 0.f, rc = 0.f, rw = 0.f;
	if (!modDroidPropulsionSpan(d, lc, lw, rc, rw)) { return 1.f; }
	return std::clamp(0.5f * (lw + rw) / 11.f, 1.f, 3.f);
}

void update()
{
	const uint32_t now = graphicsTime;
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (const DROID *d : gameWorld.objects.droids[p])
		{
			if (d == nullptr || d->died) { continue; }
			if (d->isVtol() || d->isTransporter() || d->isFlying()) { continue; }
			if (d->droidType == DROID_PERSON || d->droidType == DROID_CYBORG || d->droidType == DROID_CYBORG_SUPER
			    || d->droidType == DROID_CYBORG_CONSTRUCT || d->droidType == DROID_CYBORG_REPAIR) { continue; }
			if (!d->visibleForLocalDisplay()) { s_last.erase(d->id); continue; }
			// nur an der Oberflaeche (Untergrund-Gaenge und Orbit-Plattformen haben keinen weichen Boden)
			if (modlayer::layerOf(d) != modlayer::SURFACE) { s_last.erase(d->id); continue; }
			auto it = s_last.find(d->id);
			if (it == s_last.end())
			{
				s_last[d->id] = { d->pos.x, d->pos.y, now, 0 };
				continue;
			}
			Last &L = it->second;
			L.seen = now;
			const float dx = static_cast<float>(d->pos.x - L.x), dy = static_cast<float>(d->pos.y - L.y);
			const float dd = dx * dx + dy * dy;
			// (19.11. 17:06 "passen nicht zu den Kettengroessen"): das WZ-Muster waechst mit der Kette - breite Ketten (Fatboy, Mammut)
			// bekommen laengere Abschnitte, damit die Glieder im Abdruck so gross wirken wie an der Kette selbst
			const float seg = SEG * segScale(d);
			if (dd < seg * seg) { continue; }
			if (dd > (6.f * seg) * (6.f * seg))
			{
				// Sprung (Transporter, Ebenenwechsel, Laden): keine Spur ueber die Strecke
				L.x = d->pos.x; L.y = d->pos.y;
				continue;
			}
			emit(d, static_cast<float>(L.x), static_cast<float>(L.y), static_cast<float>(d->pos.x), static_cast<float>(d->pos.y), L.phase);
			L.phase = static_cast<uint8_t>((L.phase + 1) & 3);
			L.x = d->pos.x; L.y = d->pos.y;
		}
	}
	// verschwundene Fahrzeuge vergessen (alle 5 s)
	if (now - s_lastPrune > 5000)
	{
		s_lastPrune = now;
		for (auto it = s_last.begin(); it != s_last.end();)
		{
			if (now - it->second.seen > 5000) { it = s_last.erase(it); }
			else { ++it; }
		}
	}
}
} // namespace

void draw3D(const glm::mat4 &viewMatrix)
{
	// je Art 4 Stuecke = die 4 Viertel der WZ-Kettenkachel (tracks.py)
	static iIMDBaseShape *pieces[3][4] = {};
	static bool tried = false, ok = false;
	static unsigned gen = 0;
	if (modelCacheStale(gen)) { tried = false; }
	if (!tried)
	{
		tried = true;
		ok = true;
		static const char *names[3] = { "tank", "half", "tyre" };
		for (int k = 0; k < 3; ++k)
		{
			for (int q = 0; q < 4; ++q)
			{
				char nm[40];
				snprintf(nm, sizeof(nm), "modtrack_%s_%d.pie", names[k], q);
				pieces[k][q] = modelGet(nm);
				ok = ok && pieces[k][q] != nullptr;
			}
		}
	}
	if (!ok) { return; }
	if (!gamePaused()) { update(); }
	if (s_marks.empty()) { return; }
	const uint32_t now = graphicsTime;
	const glm::vec3 cam = glm::vec3(glm::inverse(viewMatrix)[3]);
	const uint8_t layer = static_cast<uint8_t>(modlayer::current());
	for (const Mark &m : s_marks)
	{
		const uint32_t life = LIFE_MS[m.kind];
		const uint32_t age = now - m.t;
		if (age >= life || m.layer != layer) { continue; }
		const glm::vec3 d = m.c - cam;
		if (d.x * d.x + d.z * d.z > DRAW_RANGE * DRAW_RANGE) { continue; }
		// voll bis 55 % der Lebenszeit, dann weich ausblenden
		const float k = static_cast<float>(age) / static_cast<float>(life);
		const float fade = k < 0.55f ? 1.f : std::max(0.f, 1.f - (k - 0.55f) / 0.45f);
		const int a = static_cast<int>(255.f * m.alpha * fade * fade);
		if (a <= 2) { continue; }
		glm::mat4 M(1.f);
		M[0] = glm::vec4(m.right * (m.width / 100.f), 0.f);
		M[1] = glm::vec4(m.up, 0.f);
		M[2] = glm::vec4(m.fwd * (m.len / 100.f * 1.04f), 0.f);   // 4 % Ueberlappung, keine Luecken in Kurven
		M[3] = glm::vec4(m.c, 1.f);
		pie_Draw3DShape(pieces[m.kind][m.phase & 3]->displayModel(), 0, 0, WZCOL_WHITE, pie_TRANSLUCENT, a, M, viewMatrix, 0.f, true);
	}
}

void reset()
{
	s_marks.clear();
	s_head = 0;
	s_last.clear();
	s_lastPrune = 0;
}
} // namespace modtracks
