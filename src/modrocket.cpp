// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Raketensilo (siehe modrocket.h)
#include "modrocket.h"

#include <algorithm>
#include <map>
#include <vector>
#include <set>

#include "lib/framework/frame.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/pielighting.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"

#include "component.h"
#include "console.h"
#include "droid.h"
#include "effects.h"
#include "game_world.h"
#include "map.h"
#include "modgrid.h"
#include "modlayer.h"
#include "modlayermap.h"
#include "modsat.h"
#include "order.h"
#include "power.h"
#include "structure.h"

#ifndef GLM_ENABLE_EXPERIMENTAL
	#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace modrocket
{
namespace
{
constexpr int ROCKET_COST = 250;
constexpr unsigned BUILD_MS = 75000;
constexpr unsigned COUNTDOWN_MS = 8000;
// (Wunsch 17.11. 22:13) Ankunft im Orbit: die Rakete taucht aus der verschwommenen Planetenebene auf, wirft gleich die Boosterstufe ab
// (faellt nach unten weg), gleitet langsam fast bis an die Insel, die Spitze oeffnet sich und die Pioniere schweben hinauf auf die Insel.
constexpr unsigned ARRIVE_START_MS = 5000;   // Beginn der Ankunft (nach dem Start an der Oberflaeche)
constexpr unsigned EMERGE_MS = 7000;         // (22:42) "taucht sehr langsam auf, fast realistisch langsam, dauert einige Sekunden"
constexpr unsigned GLIDE_MS = 9000;          // langsames Heranschweben an die Insel
constexpr unsigned NOSE_MS = 1500;           // Spitze oeffnet sich
constexpr unsigned EXIT_MS = 1000;           // dann steigen die Pioniere aus (bzw. der Satellit schwebt hinaus)
constexpr unsigned SINK_MS = 6000;           // leere Rakete sinkt zurueck in die Tiefe
constexpr unsigned UNLOAD_MS = ARRIVE_START_MS + EMERGE_MS + GLIDE_MS + NOSE_MS;              // Aussteigen
constexpr unsigned LAUNCH_MS = UNLOAD_MS + EXIT_MS + SINK_MS;                                  // dann ist das Silo wieder frei
constexpr size_t CAPACITY = 3;
constexpr int BOARD_R = DIST_TILE * 7 / 5;
constexpr float ROCKET_H = 300.f;         // Hoehe des Raketenmodells (rocketsilo.py, Starship-Stil)

enum Stage { ST_ASSEMBLE = 0, ST_READY, ST_COUNTDOWN, ST_LAUNCH };
struct Silo
{
	int stage = ST_ASSEMBLE;
	bool paid = false;
	unsigned progress = 0;
	unsigned t0 = 0;
	std::vector<uint32_t> crew;
	bool told = false;
	bool sat = false;     // (21:34) Nutzlast ist ein NASDA-Lasersatellit statt Pionieren (modsat)
	bool unloaded = false;
	int arrX = 0, arrY = 0;   // Ankunftspunkt im Orbit (neben der Insel, ueber der Leere)
	int landX = 0, landY = 0; // erster Landeplatz auf der Insel
};
std::map<uint32_t, Silo> s_silo;
std::map<uint32_t, uint32_t> s_aboard;    // Einheit -> Silo

DROID *findDroid(unsigned player, uint32_t id)
{
	for (DROID *d : gameWorld.objects.droids[player]) { if (d->id == id && !d->died) { return d; } }
	return nullptr;
}

/// Ankunftspunkt: ein Feld der Leere (kein Inselboden) 2-6 Felder neben dem Landeplatz, moeglichst nah
Vector2i arrivalPoint(const Vector2i &land)
{
	const int tx = map_coord(land.x), ty = map_coord(land.y);
	for (int r = 2; r <= 6; ++r)
	{
		int best = -1, bx = 0, by = 0;
		for (int dy = -r; dy <= r; ++dy)
		{
			for (int dx = -r; dx <= r; ++dx)
			{
				if (std::max(std::abs(dx), std::abs(dy)) != r) { continue; }
				const int x = tx + dx, y = ty + dy;
				if (x < 1 || y < 1 || x >= gameWorld.map.width - 1 || y >= gameWorld.map.height - 1) { continue; }
				if (!modlayermap::isSolid(modlayer::ORBIT, x, y)) { continue; }
				const int d2 = dx * dx + dy * dy;
				if (best < 0 || d2 < best) { best = d2; bx = x; by = y; }
			}
		}
		if (best >= 0) { return Vector2i(world_coord(bx) + TILE_UNITS / 2, world_coord(by) + TILE_UNITS / 2); }
	}
	return Vector2i(land.x + 3 * TILE_UNITS, land.y);
}

/// Landeplatz im Orbit nahe (x, y): naechstes begehbares freies Feld der Orbit-Karte
bool pickLanding(int wx, int wy, Vector2i &out)
{
	if (!modlayermap::worldsExist()) { return false; }
	modlayermap::Scope sc(modlayer::ORBIT);
	UDWORD tx = static_cast<UDWORD>(map_coord(wx)), ty = static_cast<UDWORD>(map_coord(wy));
	if (!pickATileGen(gameWorld, &tx, &ty, LOOK_FOR_EMPTY_TILE, zonedPAT)) { return false; }
	out = Vector2i(world_coord(static_cast<int>(tx)) + TILE_UNITS / 2, world_coord(static_cast<int>(ty)) + TILE_UNITS / 2);
	return true;
}

void landCrew(Silo &S, unsigned player)
{
	int k = 0;
	for (uint32_t id : S.crew)
	{
		s_aboard.erase(id);
		DROID *d = findDroid(player, id);
		if (d == nullptr) { continue; }
		// (22:13) aus der geoeffneten Spitze: die Pioniere (mit Flugfaehigkeit) schweben das letzte Stueck hinauf auf die Insel
		if (S.arrX == 0 && S.arrY == 0) { continue; }   // kein Ankunftspunkt: sie stehen schon auf ihrem Landeplatz
		const Vector2i dest = d->pos.xy();
		d->pos.x = S.arrX + (k - 1) * 10;
		d->pos.y = S.arrY + (k % 2) * 8;
		initDroidMovement(d);
		orderDroidLoc(d, DORDER_MOVE, dest.x, dest.y, ModeImmediate);
		++k;
	}
	if (player == selectedPlayer && !S.crew.empty())
	{
		addConsoleMessage(_("Rocket: the engineers have landed on an orbit island"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
	}
	S.crew.clear();
}
} // namespace

bool isSilo(const STRUCTURE_STATS *st)
{
	return st != nullptr && st->id.startsWith("A0RocketSilo");
}

bool aboard(const DROID *d)
{
	return d != nullptr && !s_aboard.empty() && s_aboard.count(d->id) != 0;
}

void tick()
{
	std::vector<uint32_t> live;
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died || !isSilo(s->pStructureType)) { continue; }
			live.push_back(s->id);
			if (s->status != SS_BUILT) { continue; }   // z. B. Satelliten-Modul im Bau: Rakete wartet
			Silo &S = s_silo[s->id];
			// (Wunsch 18.11. 13:20 "Ich konnte auf das Raketensilo draufbauen, weiss aber nicht, was das gebracht hat"): Hinweis, sobald das
			// Satelliten-Modul fertig ist, und solange es auf die Rakete wartet
			{
				static std::set<uint32_t> s_satTold;
				if (s->capacity > 0 && !S.sat && p == selectedPlayer && s_satTold.insert(s->id).second)
				{
					addConsoleMessage(S.stage == ST_READY
						? "Raketensilo: NASDA-Satellit eingebaut - die Rakete steht bereit und bringt ihn jetzt in die Umlaufbahn."
						: "Raketensilo: NASDA-Satellit eingebaut. Er fliegt mit der naechsten fertigen Rakete in die Umlaufbahn - danach kann die Satelliten-Uplink-Zentrale den Laser abfeuern (Zentrale waehlen, Ziel anklicken).",
						DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
				}
				if (s->capacity == 0) { s_satTold.erase(s->id); }
			}
			switch (S.stage)
			{
			case ST_ASSEMBLE:
				if (!S.paid)
				{
					if (getPower(p) < ROCKET_COST) { break; }
					usePower(static_cast<int>(p), ROCKET_COST);
					S.paid = true;
					S.progress = 0;
				}
				if (!modgrid::isPoweredDown(s)) { S.progress += deltaGameTime; }
				if (S.progress >= BUILD_MS)
				{
					S.stage = ST_READY;
					S.told = false;
					if (p == selectedPlayer) { addConsoleMessage(_("Rocket ready: send up to 3 cyborg engineers to the rocket silo"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
				}
				break;
			case ST_READY:
			case ST_COUNTDOWN:
			{
				// (Wunsch 22:13) Satelliten-Modul am Silo: die bereitstehende Rakete nimmt den Satelliten mit (das Modul wird eingeladen)
				if (S.stage == ST_READY && S.crew.empty() && !S.sat && s->capacity > 0 && modsat::satellites(static_cast<int>(p)) < modsat::MAX_SATELLITES)
				{
					S.sat = true;
					S.stage = ST_COUNTDOWN;
					S.t0 = gameTime;
					s->capacity = 0;
					if (!s->pStructureType->pIMD.empty()) { s->sDisplay.imd = s->pStructureType->pIMD[0]; }
					Vector2i land;
					if (pickLanding(s->pos.x, s->pos.y, land)) { S.landX = land.x; S.landY = land.y; const Vector2i a = arrivalPoint(land); S.arrX = a.x; S.arrY = a.y; }
					if (p == selectedPlayer) { addConsoleMessage(_("Rocket silo: NASDA satellite loaded - countdown"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
				}
				// Einsteigen: Cyborg-Pioniere am Silo (ohne anderen Auftrag oder mit dem Silo als Ziel)
				for (DROID *d : gameWorld.objects.droids[p])
				{
					if (S.sat || S.crew.size() >= CAPACITY) { break; }
					if (d->died || d->droidType != DROID_CYBORG_CONSTRUCT || aboard(d) || modlayer::layerOf(d) != modlayer::SURFACE || modlayer::inTransition(d)) { continue; }
					if (iHypot(d->pos.xy() - s->pos.xy()) > BOARD_R) { continue; }
					const bool idle = d->order.type == DORDER_NONE;
					const bool toSilo = (d->order.type == DORDER_MOVE || d->order.type == DORDER_SCOUT) && iHypot(d->order.pos - s->pos.xy()) <= BOARD_R;
					if (!idle && !toSilo) { continue; }
					Vector2i land;
					if (!pickLanding(s->pos.x + static_cast<int>(S.crew.size()) * TILE_UNITS, s->pos.y, land)) { continue; }
					if (S.crew.empty()) { S.landX = land.x; S.landY = land.y; const Vector2i a = arrivalPoint(land); S.arrX = a.x; S.arrY = a.y; }
					orderDroid(d, DORDER_STOP, ModeImmediate);
					d->selected = false;
					d->pos.x = land.x;
					d->pos.y = land.y;
					modlayer::moveToLayer(d, modlayer::ORBIT);
					initDroidMovement(d);
					s_aboard[d->id] = s->id;
					S.crew.push_back(d->id);
					if (S.stage == ST_READY) { S.stage = ST_COUNTDOWN; S.t0 = gameTime; }
				}
				if (S.stage == ST_COUNTDOWN)
				{
					const unsigned el = gameTime - S.t0;
					if (el >= COUNTDOWN_MS || (S.crew.size() >= CAPACITY && el >= 3000))
					{
						S.stage = ST_LAUNCH;
						S.t0 = gameTime;
						audio_PlayStaticTrack(s->pos.x, s->pos.y, ID_SOUND_EXPLOSION);
						if (p == selectedPlayer) { addConsoleMessage(_("Rocket launched!"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
					}
				}
				break;
			}
			case ST_LAUNCH:
				if (!S.unloaded && gameTime - S.t0 >= UNLOAD_MS)
				{
					S.unloaded = true;
					if (S.sat) { modsat::addSatellite(static_cast<int>(p)); }
					else { landCrew(S, p); }
				}
				if (gameTime - S.t0 >= LAUNCH_MS)
				{
					S = Silo();   // naechste Rakete
				}
				break;
			}
		}
	}
	// zerstoerte Silos: Mannschaft landet trotzdem (sie sitzt schon in der Kapsel), Eintrag weg
	for (auto it = s_silo.begin(); it != s_silo.end();)
	{
		if (std::find(live.begin(), live.end(), it->first) != live.end()) { ++it; continue; }
		for (uint32_t id : it->second.crew) { s_aboard.erase(id); }
		it = s_silo.erase(it);
	}
}

namespace
{
const iIMDBaseShape *s_rocket = nullptr, *s_flame = nullptr, *s_booster = nullptr, *s_ship = nullptr, *s_noseL = nullptr, *s_noseR = nullptr, *s_satModel = nullptr;

void drawShape(const iIMDBaseShape *shape, unsigned player, const glm::mat4 &m, const glm::mat4 &viewMatrix)
{
	if (shape != nullptr) { pie_Draw3DShape(shape->displayModel(), 0, getPlayerColour(player), WZCOL_WHITE, 0, 0, m, viewMatrix, 0.f); }
}

void drawFlame(const glm::vec3 &at, float size, const glm::mat4 &viewMatrix)
{
	if (s_flame == nullptr) { return; }
	const glm::mat4 bill(glm::transpose(glm::mat3(viewMatrix)));
	const float fl = size * (1.f + 0.15f * static_cast<float>((graphicsTime / 40u) % 3u));
	const glm::mat4 fm = glm::translate(glm::mat4(1.f), at) * glm::mat4(bill) * glm::scale(glm::mat4(1.f), glm::vec3(fl));
	pie_Draw3DShape(s_flame->displayModel(), 0, 0, WZCOL_WHITE, pie_ADDITIVE, 210, fm, viewMatrix, 0.f, true);
}

float easeOut(float u)
{
	u = std::clamp(u, 0.f, 1.f);
	return 1.f - (1.f - u) * (1.f - u);
}

/// (Wunsch 22:13) Ankunft im Orbit (nur Anzeige): auftauchen, Booster abwerfen, heranschweben, Spitze oeffnen, aussteigen, zuruecksinken
void drawArrivals(const glm::mat4 &viewMatrix)
{
	static std::map<uint32_t, unsigned> s_sepPuff;
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died || !isSilo(s->pStructureType)) { continue; }
			const auto it = s_silo.find(s->id);
			if (it == s_silo.end()) { continue; }
			const Silo &S = it->second;
			if (S.stage != ST_LAUNCH || (S.arrX == 0 && S.arrY == 0)) { continue; }
			const int tt = static_cast<int>(graphicsTime) - static_cast<int>(S.t0 + ARRIVE_START_MS);
			if (tt < 0 || tt > static_cast<int>(LAUNCH_MS - ARRIVE_START_MS)) { continue; }
			const float t = static_cast<float>(tt) / 1000.f;
			const float ground = static_cast<float>(modlayermap::heightAt(modlayer::ORBIT, S.landX, S.landY));
			const float bStart = ground - 1100.f, bSep = ground - 800.f, bFinal = ground + 10.f - 252.f;
			const float tEm = EMERGE_MS / 1000.f, tGl = GLIDE_MS / 1000.f, tNo = NOSE_MS / 1000.f, tEx = EXIT_MS / 1000.f;
			float b;
			if (t < tEm) { b = bStart + (bSep - bStart) * (t / tEm); }
			else if (t < tEm + tGl) { b = bSep + (bFinal - bSep) * easeOut((t - tEm) / tGl); }
			else if (t < tEm + tGl + tNo + tEx) { b = bFinal; }
			else { const float ts = t - (tEm + tGl + tNo + tEx); b = bFinal - 70.f * ts * ts; }
			const glm::vec3 base(static_cast<float>(S.arrX), b, static_cast<float>(-S.arrY));
			if (t < tEm)
			{
				drawShape(s_rocket, p, glm::translate(glm::mat4(1.f), base), viewMatrix);
				drawFlame(base + glm::vec3(0.f, -14.f, 0.f), 1.4f, viewMatrix);
				continue;
			}
			// Boosterstufe bricht weg und faellt nach unten (kommt nicht wieder an)
			const float tsep = t - tEm;
			if (tsep < 5.f)
			{
				const float fall = 85.f * tsep * tsep;
				const glm::mat4 bm = glm::translate(glm::mat4(1.f), glm::vec3(base.x + 8.f * tsep, bSep - fall, base.z))
				                     * glm::rotate(glm::mat4(1.f), 0.22f * tsep, glm::vec3(0.f, 0.f, 1.f));
				drawShape(s_booster, p, bm, viewMatrix);
				unsigned &np = s_sepPuff[s->id];
				if (tsep < 0.8f && graphicsTime >= np)
				{
					np = graphicsTime + 90;
					Vector3i pp(S.arrX, static_cast<int>(bSep + 150.f), S.arrY);
					addEffect(&pp, EFFECT_SMOKE, SMOKE_TYPE_STEAM, false, nullptr, 0);
				}
			}
			// Starship-Oberstufe: Schiff + Nasenhaelften (oeffnen sich kurz vor der Insel)
			drawShape(s_ship, p, glm::translate(glm::mat4(1.f), base), viewMatrix);
			if (t < tEm + tGl) { drawFlame(base + glm::vec3(0.f, 140.f, 0.f), 0.7f, viewMatrix); }
			const float open = 1.3f * easeOut((t - tEm - tGl) / tNo);
			const glm::mat4 nl = glm::translate(glm::mat4(1.f), base + glm::vec3(-16.f, 252.f, 0.f)) * glm::rotate(glm::mat4(1.f), open, glm::vec3(0.f, 0.f, 1.f));
			const glm::mat4 nr = glm::translate(glm::mat4(1.f), base + glm::vec3(16.f, 252.f, 0.f)) * glm::rotate(glm::mat4(1.f), -open, glm::vec3(0.f, 0.f, 1.f));
			drawShape(s_noseL, p, nl, viewMatrix);
			drawShape(s_noseR, p, nr, viewMatrix);
			// Nutzlast Satellit: schwebt aus der offenen Spitze nach oben in die Umlaufbahn
			const float tu = t - (tEm + tGl + tNo);
			if (S.sat && tu > 0.f && tu < 5.f)
			{
				const glm::mat4 sm = glm::translate(glm::mat4(1.f), base + glm::vec3(0.f, 262.f + 60.f * tu * tu, 0.f))
				                     * glm::rotate(glm::mat4(1.f), 0.5f * tu, glm::vec3(0.f, 1.f, 0.f)) * glm::scale(glm::mat4(1.f), glm::vec3(0.6f));
				drawShape(s_satModel, p, sm, viewMatrix);
			}
		}
	}
}
}

void draw3D(const glm::mat4 &viewMatrix)
{
	if (s_silo.empty()) { return; }
	static unsigned s_tried = 0;   // MOD (18.11.): Modell-Generation (Neuladen zwischen Spielen)
	if (modelCacheStale(s_tried))
	{
		s_rocket = modelGet(WzString::fromUtf8("modrocket.pie"));
		s_flame = modelGet(WzString::fromUtf8("fxicbm.pie"));
		s_booster = modelGet(WzString::fromUtf8("modrocket_booster.pie"));
		s_ship = modelGet(WzString::fromUtf8("modrocket_ship.pie"));
		s_noseL = modelGet(WzString::fromUtf8("modrocket_nosel.pie"));
		s_noseR = modelGet(WzString::fromUtf8("modrocket_noser.pie"));
		s_satModel = modelGet(WzString::fromUtf8("modsatellite.pie"));
	}
	if (s_rocket == nullptr) { return; }
	if (modlayer::current() == modlayer::ORBIT) { drawArrivals(viewMatrix); return; }
	if (modlayer::current() != modlayer::SURFACE) { return; }
	static std::map<uint32_t, unsigned> s_nextPuff;
	const glm::mat4 bill(glm::transpose(glm::mat3(viewMatrix)));
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died || s->status != SS_BUILT || !isSilo(s->pStructureType) || !s->visibleForLocalDisplay()) { continue; }
			const auto it = s_silo.find(s->id);
			if (it == s_silo.end()) { continue; }
			const Silo &S = it->second;
			const float ground = static_cast<float>(map_Height(gameWorld.map, s->pos.x, s->pos.y));
			float lift = 0.f;
			bool flame = false, steam = false;
			if (S.stage == ST_ASSEMBLE)
			{
				if (!S.paid) { continue; }
				const float k = std::min(1.f, static_cast<float>(S.progress) / static_cast<float>(BUILD_MS));
				lift = -(1.f - k) * ROCKET_H;            // faehrt beim Bau aus dem Silo hoch
			}
			else if (S.stage == ST_COUNTDOWN) { steam = true; }
			else if (S.stage == ST_LAUNCH)
			{
				const float t = static_cast<float>(graphicsTime - S.t0) / 1000.f;
				lift = t < 0.f ? 0.f : 130.f * t * t;
				flame = true;
				steam = t < 1.5f;
				if (lift > 6000.f || t > ARRIVE_START_MS / 1000.f) { continue; }
			}
			const glm::vec3 base(static_cast<float>(s->pos.x), ground + lift, static_cast<float>(-s->pos.y));
			const glm::mat4 m = glm::translate(glm::mat4(1.f), base);
			pie_Draw3DShape(s_rocket->displayModel(), 0, getPlayerColour(p), WZCOL_WHITE, 0, 0, m, viewMatrix, 0.f);
			unsigned &next = s_nextPuff[s->id];
			if ((steam || flame) && graphicsTime >= next)
			{
				next = graphicsTime + (flame ? 70u : 220u);
				const int r = flame ? 30 : 50;
				Vector3i pp(s->pos.x + (static_cast<int>(graphicsTime % 37u) - 18) * r / 18, static_cast<int>(base.y) + (flame ? -20 : 10),
				            s->pos.y + (static_cast<int>(graphicsTime % 23u) - 11) * r / 11);
				addEffect(&pp, EFFECT_SMOKE, flame ? SMOKE_TYPE_BILLOW : SMOKE_TYPE_STEAM, false, nullptr, 0);
			}
			if (flame && s_flame != nullptr)
			{
				const float fl = 1.6f + 0.25f * static_cast<float>((graphicsTime / 40u) % 3u);
				const glm::mat4 fm = glm::translate(glm::mat4(1.f), base + glm::vec3(0.f, -10.f, 0.f)) * glm::mat4(bill) * glm::scale(glm::mat4(1.f), glm::vec3(fl));
				pie_Draw3DShape(s_flame->displayModel(), 0, 0, WZCOL_WHITE, pie_ADDITIVE, 230, fm, viewMatrix, 0.f, true);
				LIGHT l;
				l.position = Vector3i(s->pos.x, static_cast<int>(base.y), s->pos.y);
				l.range = 900;
				l.colour = pal_Colour(255, 170, 80);
				l.intensity = 2.5f;
				getCurrentLightingData().lights.push_back(l);
			}
		}
	}
}

void reset()
{
	s_silo.clear();
	s_aboard.clear();
}
}
