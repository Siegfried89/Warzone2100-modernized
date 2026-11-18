// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: NASDA-Lasersatelliten (siehe modsat.h)
#include "modsat.h"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include "lib/framework/frame.h"
#include "lib/framework/math_ext.h"
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
#include "display3d.h"
#include "effects.h"
#include "game_world.h"
#include "map.h"
#include "modlayer.h"
#include "modlayermap.h"
#include "structure.h"

#ifndef GLM_ENABLE_EXPERIMENTAL
	#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace modsat
{
namespace
{
constexpr unsigned SELF_LAUNCH_MS = 6000;       // Start von der Orbit-Insel nach 6 s
constexpr unsigned SHOW_RISE_MS = 4000;         // so lange sieht man ihn aufsteigen

int s_sats[MAX_PLAYERS] = {};
unsigned s_firedAt[MAX_PLAYERS] = {};       // Spielzeit des letzten Laserschusses (nur fuer die Anzeige)
struct Beam { int x, y; unsigned t0; };
std::vector<Beam> s_beams;                   // (22:42) gruener Laserstrahl vom Himmel am Einschlagort (nur Anzeige)
// (22:42) rote blinkende Lichter am Satelliten (Modellpunkte, Satellitenmitte = Ursprung, wie satellite.py)
const glm::vec4 BLINK_PTS[] = {{-17.f, 30.f, -15.5f, 1.f}, {17.f, 30.f, -15.5f, 1.f}, {-17.f, 30.f, 15.5f, 1.f}, {17.f, 30.f, 15.5f, 1.f},
                               {-66.f, 31.5f, 0.f, 1.f}, {66.f, 31.5f, 0.f, 1.f}, {0.f, 26.f, 62.f, 1.f}};
std::set<uint32_t> s_remove;                    // eingeladene / gestartete Satelliten-Bauwerke (im naechsten Schritt entfernen)
std::map<uint32_t, unsigned> s_launchAt;        // Satellit auf der Orbit-Insel -> Startzeit
std::set<uint32_t> s_told;
struct Rise { int x, y, z; unsigned t0; uint16_t dir; int player; };
std::vector<Rise> s_rise;                       // nur Anzeige

void say(unsigned player, const char *msg)
{
	if (player == selectedPlayer) { addConsoleMessage(msg, DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
}

unsigned pauseMs(int n)
{
	return n >= 3 ? 150000u : (n == 2 ? 200000u : 300000u);
}

bool isUplinkLaser(const STRUCTURE *s)
{
	return s != nullptr && s->pStructureType != nullptr && s->pStructureType->type == REF_SAT_UPLINK;
}
}

bool isSatellite(const STRUCTURE_STATS *psStats)
{
	return psStats != nullptr && psStats->id.startsWith("A0LaserSatellite");
}

int satellites(int player)
{
	return (player >= 0 && player < MAX_PLAYERS) ? s_sats[player] : 0;
}

bool canFire(const STRUCTURE *psStruct)
{
	if (!isUplinkLaser(psStruct)) { return true; }   // die alte Lasersatelliten-Kommandozentrale wie bisher
	const int n = satellites(psStruct->player);
	if (n <= 0) { return false; }
	const unsigned last = psStruct->asWeaps[0].lastFired;
	return last == 0 || gameTime - last >= pauseMs(n);
}

void denied(const STRUCTURE *psStruct)
{
	if (!isUplinkLaser(psStruct) || psStruct->player != selectedPlayer) { return; }
	const int n = satellites(psStruct->player);
	if (n <= 0)
	{
		addConsoleMessage(_("No laser satellite in orbit - build a NASDA satellite (rocket silo or orbit island)"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		return;
	}
	const unsigned el = gameTime - psStruct->asWeaps[0].lastFired;
	const unsigned left = el < pauseMs(n) ? (pauseMs(n) - el) / 1000 : 0;
	char buf[160];
	ssprintf(buf, _("Laser satellite recharging: %u s"), left);
	addConsoleMessage(buf, DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
}

bool isModule(const STRUCTURE_STATS *psStats)
{
	return psStats != nullptr && psStats->id.compare("A0SatelliteModule") == 0;
}

int moduleStatIndex()
{
	static const STRUCTURE_STATS *s_base = nullptr;
	static int s_idx = -1;
	if (s_base != asStructureStats || s_idx < 0 || static_cast<unsigned>(s_idx) >= numStructureStats || !isModule(&asStructureStats[s_idx]))
	{
		s_base = asStructureStats;
		s_idx = -1;
		for (unsigned i = 0; i < numStructureStats; ++i) { if (isModule(&asStructureStats[i])) { s_idx = static_cast<int>(i); break; } }
	}
	return s_idx;
}

void addSatellite(int player)
{
	if (player < 0 || player >= MAX_PLAYERS) { return; }
	s_sats[player] = std::min(MAX_SATELLITES, s_sats[player] + 1);
	if (static_cast<unsigned>(player) == selectedPlayer)
	{
		char buf[200];
		ssprintf(buf, _("Laser satellite in orbit (%d of %d) - the satellite uplink center can now fire the laser"), s_sats[player], MAX_SATELLITES);
		addConsoleMessage(buf, DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		audio_PlayTrack(ID_SOUND_UPLINK);
	}
}

void fired(int player, int targetX, int targetY)
{
	if (player >= 0 && player < MAX_PLAYERS) { s_firedAt[player] = std::max(1u, gameTime); }
	s_beams.push_back({targetX, targetY, gameTime + 3800});   // der Strahl kommt mit dem Countdown (4 s) herunter
}

void tick()
{
	std::vector<uint32_t> launched;
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died || s->status != SS_BUILT || !isSatellite(s->pStructureType) || s_remove.count(s->id) != 0) { continue; }
			{
				// auf der Orbit-Insel gebaut: startet von selbst
				auto it = s_launchAt.find(s->id);
				if (it == s_launchAt.end())
				{
					if (s_sats[p] >= MAX_SATELLITES)
					{
						if (s_told.insert(s->id).second) { say(p, _("Laser satellite: already 3 satellites in orbit")); }
						continue;
					}
					s_launchAt[s->id] = gameTime + SELF_LAUNCH_MS;
					say(p, _("Laser satellite: launching from the orbit island"));
					continue;
				}
				if (gameTime < it->second) { continue; }
				launched.push_back(s->id);
				s_rise.push_back({s->pos.x, s->pos.y, s->pos.z, graphicsTime, s->rot.direction, static_cast<int>(p)});
				addSatellite(static_cast<int>(p));
				continue;
			}
		}
	}
	for (uint32_t id : launched) { s_remove.insert(id); s_launchAt.erase(id); }
	// eingeladene / gestartete Satelliten-Bauwerke entfernen (ausserhalb der Schleifen ueber die Bauwerke)
	if (!s_remove.empty())
	{
		std::vector<STRUCTURE *> gone;
		for (unsigned p = 0; p < MAX_PLAYERS; ++p)
		{
			for (STRUCTURE *s : gameWorld.objects.structures[p]) { if (!s->died && s_remove.count(s->id) != 0) { gone.push_back(s); } }
		}
		s_remove.clear();
		for (STRUCTURE *s : gone)
		{
			s_told.erase(s->id);
			removeStruct(s, true, gameWorld);
		}
	}
}

namespace
{
const iIMDBaseShape *s_glowShape = nullptr, *s_beamShape = nullptr;

/// rote Blinklichter an einem Satelliten (m = Satellitenmitte), versetzt blinkend
void drawBlink(const glm::mat4 &m, const glm::mat4 &viewMatrix, unsigned seed, float scale)
{
	if (s_glowShape == nullptr) { return; }
	const glm::mat4 bill(glm::transpose(glm::mat3(viewMatrix)));
	for (size_t i = 0; i < sizeof(BLINK_PTS) / sizeof(BLINK_PTS[0]); ++i)
	{
		const unsigned ph = (graphicsTime + seed * 337u + static_cast<unsigned>(i) * 171u) % 1300u;
		if (ph > 360u) { continue; }   // kurz an, lange aus
		const glm::vec3 wp = glm::vec3(m * BLINK_PTS[i]);
		const glm::mat4 gm = glm::translate(glm::mat4(1.f), wp) * glm::mat4(bill) * glm::scale(glm::mat4(1.f), glm::vec3(0.55f * scale));
		pie_Draw3DShape(s_glowShape->displayModel(), 0, 0, pal_RGBA(255, 30, 20, 255), pie_ADDITIVE, 240, gm, viewMatrix, 0.f, true);
	}
}

/// gruener Laserstrahl vom Himmel (senkrechter Leuchtzylinder, additiv)
void drawBeams(const glm::mat4 &viewMatrix)
{
	for (auto it = s_beams.begin(); it != s_beams.end();)
	{
		const int el = static_cast<int>(graphicsTime) - static_cast<int>(it->t0);
		if (el > 1800) { it = s_beams.erase(it); continue; }
		if (el >= 0 && s_beamShape != nullptr)
		{
			const float k = el < 250 ? static_cast<float>(el) / 250.f : 1.f - static_cast<float>(el - 250) / 1550.f;
			const float ground = static_cast<float>(map_Height(gameWorld.map, it->x, it->y));
			for (int layer = 0; layer < 2; ++layer)
			{
				const float rad = (layer == 0 ? 34.f : 14.f) * (0.7f + 0.3f * k);
				const glm::mat4 bm = glm::translate(glm::mat4(1.f), glm::vec3(static_cast<float>(it->x), ground, static_cast<float>(-it->y)))
				                     * glm::scale(glm::mat4(1.f), glm::vec3(rad, 2600.f, rad));
				pie_Draw3DShape(s_beamShape->displayModel(), 0, 0, layer == 0 ? pal_RGBA(60, 255, 110, 255) : pal_RGBA(230, 255, 235, 255), pie_ADDITIVE,
				                static_cast<int>((layer == 0 ? 150.f : 230.f) * k), bm, viewMatrix, 0.f, true);
			}
			LIGHT lt;
			lt.position = Vector3i(it->x, static_cast<int>(ground) + 60, it->y);
			lt.range = 1100;
			lt.colour = pal_Colour(60, 255, 110);
			lt.intensity = 3.5f * k;
			getCurrentLightingData().lights.push_back(lt);
		}
		++it;
	}
}
}

void draw3D(const glm::mat4 &viewMatrix)
{
	const modlayer::Layer view = modlayer::current();
	static const iIMDBaseShape *s_model = nullptr;
	static unsigned s_tried = 0;   // MOD (18.11.): Modell-Generation (Neuladen zwischen Spielen)
	if (modelCacheStale(s_tried))
	{
		s_model = modelGet(WzString::fromUtf8("modsatellite.pie"));
		s_glowShape = modelGet(WzString::fromUtf8("modnightlight.pie"));
		s_beamShape = modelGet(WzString::fromUtf8("modlaserbeam.pie"));
	}
	if (view == modlayer::SURFACE) { drawBeams(viewMatrix); }
	// Satelliten als Bauwerk (Orbit-Insel) und als Modul am Raketensilo blinken rot
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died || s->status != SS_BUILT || modlayer::layerOf(s) != view || !s->visibleForLocalDisplay()) { continue; }
			const bool sat = isSatellite(s->pStructureType);
			const bool silo = !sat && s->capacity > 0 && moduleStatIndex() >= 0 && s->pStructureType->id.startsWith("A0RocketSilo");
			if (!sat && !silo) { continue; }
			const glm::mat4 sm = glm::translate(glm::mat4(1.f), glm::vec3(static_cast<float>(s->pos.x), static_cast<float>(s->pos.z), static_cast<float>(-s->pos.y)))
			                     * glm::rotate(glm::mat4(1.f), UNDEG(-s->rot.direction), glm::vec3(0.f, 1.f, 0.f));
			const glm::mat4 m = sat ? sm * glm::translate(glm::mat4(1.f), glm::vec3(0.f, 50.f, 0.f))
			                        : sm * glm::translate(glm::mat4(1.f), glm::vec3(36.f, 85.4f, 36.f)) * glm::scale(glm::mat4(1.f), glm::vec3(0.6f));
			drawBlink(m, viewMatrix, s->id, sat ? 1.f : 0.7f);
		}
	}
	if (view != modlayer::ORBIT) { return; }
	if (s_model == nullptr) { return; }
	const iIMDBaseShape *s_glow = s_glowShape;
	const iIMDShape *shape = s_model->displayModel();
	static unsigned s_nextPuff = 0;
	const bool puff = graphicsTime >= s_nextPuff;
	if (puff) { s_nextPuff = graphicsTime + 160; }

	// Countdown auf der Orbit-Insel: Steuerduesen zischen
	if (puff)
	{
		for (const auto &kv : s_launchAt)
		{
			for (unsigned p = 0; p < MAX_PLAYERS; ++p)
			{
				for (const STRUCTURE *s : gameWorld.objects.structures[p])
				{
					if (s->id != kv.first || !s->visibleForLocalDisplay()) { continue; }
					Vector3i pp(s->pos.x + static_cast<int>(graphicsTime % 41u) - 20, s->pos.z + 30, s->pos.y + static_cast<int>(graphicsTime % 29u) - 14);
					addEffect(&pp, EFFECT_SMOKE, SMOKE_TYPE_STEAM, false, nullptr, 0);
				}
			}
		}
	}
	// Aufstieg von der Orbit-Insel
	for (auto it = s_rise.begin(); it != s_rise.end();)
	{
		const unsigned el = graphicsTime - it->t0;
		if (graphicsTime < it->t0 || el > SHOW_RISE_MS) { it = s_rise.erase(it); continue; }
		const float t = static_cast<float>(el) / 1000.f;
		const float lift = 50.f + 70.f * t * t;
		const glm::mat4 m = glm::translate(glm::mat4(1.f), glm::vec3(static_cast<float>(it->x), static_cast<float>(it->z) + lift, static_cast<float>(-it->y)))
		                    * glm::rotate(glm::mat4(1.f), UNDEG(-it->dir) + t * 0.6f, glm::vec3(0.f, 1.f, 0.f));
		pie_Draw3DShape(shape, 0, getPlayerColour(it->player), WZCOL_WHITE, 0, 0, m, viewMatrix, 0.f);
		if (puff)
		{
			Vector3i pp(it->x, it->z + static_cast<int>(lift) - 40, it->y);
			addEffect(&pp, EFFECT_SMOKE, SMOKE_TYPE_STEAM, false, nullptr, 0);
		}
		++it;
	}
	// Satelliten in der Umlaufbahn ziehen langsam am Orbit-Himmel vorbei (um den Blickpunkt)
	const int cx = playerPos.p.x, cy = playerPos.p.z;
	const int ground = modlayermap::heightAt(modlayer::ORBIT, cx, cy);
	int k = 0;
	for (int p = 0; p < MAX_PLAYERS; ++p)
	{
		for (int i = 0; i < s_sats[p]; ++i, ++k)
		{
			const float a = static_cast<float>(graphicsTime % 120000u) / 120000.f * 6.2831853f + static_cast<float>(k) * 2.0944f + static_cast<float>(p) * 0.7f;
			const float rad = 1100.f + 140.f * static_cast<float>(k % 3);
			const glm::vec3 pos(static_cast<float>(cx) + rad * std::cos(a), static_cast<float>(ground) + 420.f + 60.f * static_cast<float>(k % 3),
			                    -(static_cast<float>(cy) + rad * std::sin(a)));
			const glm::mat4 m = glm::translate(glm::mat4(1.f), pos) * glm::rotate(glm::mat4(1.f), -a, glm::vec3(0.f, 1.f, 0.f))
			                    * glm::scale(glm::mat4(1.f), glm::vec3(1.8f));
			pie_Draw3DShape(shape, 0, getPlayerColour(p), WZCOL_WHITE, 0, 0, m, viewMatrix, 0.f);
			drawBlink(m, viewMatrix, static_cast<unsigned>(k + 1), 1.8f);
			// (21:55) feuernder Satellit: die 10 Linsen des Emitterrings gluehen rosa-rot, rotes Licht
			const unsigned since = gameTime - s_firedAt[p];
			if (s_firedAt[p] != 0 && since < 5000u && s_glow != nullptr)
			{
				const float k01 = since < 600u ? static_cast<float>(since) / 600.f : 1.f - static_cast<float>(since - 600u) / 4400.f;
				const glm::mat4 bill(glm::transpose(glm::mat3(viewMatrix)));
				for (int l = 0; l <= 10; ++l)
				{
					const float la = 6.2831853f * static_cast<float>(l) / 10.f;
					const glm::vec4 lp = l == 10 ? glm::vec4(0.f, -40.f, 0.f, 1.f) : glm::vec4(16.f * std::cos(la), -40.f, 16.f * std::sin(la), 1.f);
					const glm::vec3 wp = glm::vec3(m * lp);
					const float sc = (l == 10 ? 2.6f : 1.4f) * (0.6f + 0.4f * k01);
					const glm::mat4 gm = glm::translate(glm::mat4(1.f), wp) * glm::mat4(bill) * glm::scale(glm::mat4(1.f), glm::vec3(sc));
					pie_Draw3DShape(s_glow->displayModel(), 0, 0, pal_RGBA(255, 90, 150, 255), pie_ADDITIVE, static_cast<int>(60.f + 190.f * k01), gm, viewMatrix, 0.f, true);
				}
				LIGHT lt;
				lt.position = Vector3i(static_cast<int>(pos.x), static_cast<int>(pos.y), static_cast<int>(-pos.z));
				lt.range = 1400;
				lt.colour = pal_Colour(255, 70, 110);
				lt.intensity = 3.f * k01;
				getCurrentLightingData().lights.push_back(lt);
			}
		}
	}
}

void reset()
{
	std::fill(std::begin(s_sats), std::end(s_sats), 0);
	std::fill(std::begin(s_firedAt), std::end(s_firedAt), 0u);
	s_beams.clear();
	s_remove.clear();
	s_launchAt.clear();
	s_told.clear();
	s_rise.clear();
}
}
