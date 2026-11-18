// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: GDI Juggernaut verankert sich zum Feuern wie in C&C3 (siehe modgdiart.h)
#include "modgdiart.h"

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/piedef.h"

#include "droid.h"
#include "game_world.h"
#include "move.h"
#include "stats.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#ifndef GLM_ENABLE_EXPERIMENTAL
	#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>

namespace modgdiart
{
namespace
{
enum Mode : uint8_t { MOBILE = 0, DEPLOYING, DEPLOYED, UNDEPLOYING };
struct State
{
	Mode mode = MOBILE;
	UDWORD since = 0;
};
constexpr UDWORD DEPLOY_MS = 2000;     // anhalten, Stuetzen aus, Rumpf hoch, Rohre hoch (C&C/TS: gut 2 s)
constexpr UDWORD UNDEPLOY_MS = 1400;   // alles wieder einfahren
constexpr float LIFT = 4.0f;           // so weit hebt sich der Aufbau (Modell-Einheiten; Mech-Beine ~54 hoch)
// (19:23, Bild C&C3 "so sehen die Stuetzen aus. sie klappen hinten vom Bein runter"): je Bein eine Hydraulikstuetze, Gelenk hinten am
// Schienbein der Mech-Beine (mech_std.pie: Knie hinten oben, Schienbein schraeg nach vorn zum Fuss); eingeklappt liegt sie am Bein an
// (zeigt nach oben), ausgeklappt steht sie schraeg nach hinten auf dem Boden.
constexpr float HINGE_X = 10.f, HINGE_Y = 20.f, HINGE_Z = 12.f;
constexpr float STRUT_K = 2.f;         // Stuetzen-Modelle (modgdistrut_*, gdiart.py) doppelt so gross
constexpr float ANG_STOWED = 2.8798f;  // 165 Grad: am Bein, nach oben
constexpr float ANG_DEPLOYED = 0.6632f;// 38 Grad aus der Senkrechten nach hinten
constexpr float STRUT_OUT = 0.1047f;   // 6 Grad nach aussen
constexpr float CYL_LEN = 8.5f;        // Gelenk bis Kolbenende bei eingefahrenem Kolben (Modell-Einheiten vor STRUT_K)

std::unordered_map<uint32_t, State> s_state;
int s_weapon[2] = { -2, -2 };

bool isGdiStat(uint32_t stat)
{
	static const char *const ids[2] = { "MechGDIArtillery", "MechGDIHowitzer" };   // GDI Juggernaut (Kanonen / Haubitzen)
	for (int k = 0; k < 2; ++k)
	{
		if (s_weapon[k] == -2) { s_weapon[k] = getCompFromID(COMP_WEAPON, WzString::fromUtf8(ids[k])); }
		if (s_weapon[k] > 0 && stat == static_cast<uint32_t>(s_weapon[k])) { return true; }
	}
	return false;
}

float smoothStep(float a, float b, float x)
{
	const float t = std::clamp((x - a) / (b - a), 0.f, 1.f);
	return t * t * (3.f - 2.f * t);
}

const State *stateOf(const DROID *d)
{
	if (d == nullptr || s_state.empty()) { return nullptr; }
	const auto it = s_state.find(d->id);
	return it == s_state.end() ? nullptr : &it->second;
}

bool wantsToMove(const DROID *d)
{
	return d->sMove.Status != MOVEINACTIVE && d->sMove.Status != MOVEHOVER;
}
} // namespace

bool isGdiWeapon(uint32_t weaponStat)
{
	return isGdiStat(weaponStat);
}

bool isGdi(const DROID *d)
{
	return d != nullptr && d->numWeaps > 0 && isGdiStat(d->asWeaps[0].nStat);
}

bool readyToFire(DROID *d)
{
	if (!isGdi(d)) { return true; }
	if (stateOf(d) == nullptr && wantsToMove(d)) { return false; }   // im Marsch wird nicht gefeuert und nicht verankert
	State &s = s_state[d->id];
	switch (s.mode)
	{
	case DEPLOYED:
		return true;
	case MOBILE:
		s.mode = DEPLOYING;   // erst verankern (nur im Stand, siehe oben - sonst stockt er unterwegs bei jedem Ziel)
		s.since = gameTime;
		return false;
	case DEPLOYING:
		if (gameTime - s.since >= DEPLOY_MS)
		{
			s.mode = DEPLOYED;
			return true;
		}
		return false;
	case UNDEPLOYING:
	default:
		return false;         // faehrt gerade ein, weil er laufen soll
	}
}

bool halted(const DROID *d)
{
	const State *s = stateOf(d);
	return s != nullptr && s->mode != MOBILE;
}

void tick()
{
	if (s_state.empty()) { return; }
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (DROID *d : gameWorld.objects.droids[p])
		{
			if (d->died) { continue; }
			const auto it = s_state.find(d->id);
			if (it == s_state.end()) { continue; }
			State &s = it->second;
			const bool move = wantsToMove(d);
			switch (s.mode)
			{
			case DEPLOYING:
			case DEPLOYED:
				if (move)
				{
					// einfahren; beim halb Verankerten nur den ausgefahrenen Teil zurueck (Anzeige laeuft stetig weiter)
					const UDWORD done = s.mode == DEPLOYED ? UNDEPLOY_MS : std::min<UDWORD>(UNDEPLOY_MS, (gameTime - s.since) * UNDEPLOY_MS / DEPLOY_MS);
					s.mode = UNDEPLOYING;
					s.since = gameTime - (UNDEPLOY_MS - done);
				}
				else if (s.mode == DEPLOYING && gameTime - s.since >= DEPLOY_MS)
				{
					s.mode = DEPLOYED;
				}
				break;
			case UNDEPLOYING:
				if (gameTime - s.since >= UNDEPLOY_MS) { s.mode = MOBILE; }
				break;
			case MOBILE:
			default:
				break;
			}
		}
	}
	// erledigte und verschwundene Eintraege aufraeumen (Reihenfolge egal: jeder Eintrag fuer sich)
	for (auto it = s_state.begin(); it != s_state.end();)
	{
		it = it->second.mode == MOBILE ? s_state.erase(it) : std::next(it);
	}
	if (s_state.size() > 4096) { s_state.clear(); }
}

float deployFraction(const DROID *d, UDWORD time)
{
	const State *s = stateOf(d);
	if (s == nullptr) { return 0.f; }
	const float age = static_cast<float>(static_cast<int>(time - s->since));
	switch (s->mode)
	{
	case DEPLOYING: return std::clamp(age / static_cast<float>(DEPLOY_MS), 0.f, 1.f);
	case DEPLOYED: return 1.f;
	case UNDEPLOYING: return 1.f - std::clamp(age / static_cast<float>(UNDEPLOY_MS), 0.f, 1.f);
	default: return 0.f;
	}
}

float torsoLift(const DROID *d, UDWORD time)
{
	if (!isGdi(d)) { return 0.f; }
	return LIFT * smoothStep(0.45f, 0.75f, deployFraction(d, time));
}

uint16_t displayPitch(const DROID *d, uint16_t aimPitch, UDWORD time)
{
	if (!isGdi(d)) { return aimPitch; }
	const float b = smoothStep(0.6f, 1.f, deployFraction(d, time));
	int aim = angleDelta(aimPitch);
	if (aim < DEG(20)) { aim = DEG(52); }   // noch ohne Ziel: Steilfeuer-Grundstellung
	return static_cast<uint16_t>(static_cast<int>(b * static_cast<float>(aim)));
}

bool drawStruts(const DROID *d, const glm::mat4 &bodyMatrix, const glm::mat4 &viewMatrix, int colour, PIELIGHT brightness, int pieFlag, float heightAboveMap)
{
	if (!isGdi(d)) { return false; }
	const float f = deployFraction(d, graphicsTime);
	if (f <= 0.f) { return false; }
	static const iIMDBaseShape *s_cyl = nullptr, *s_pis = nullptr, *s_foot = nullptr;
	static unsigned s_gen = 0;
	if (modelCacheStale(s_gen))
	{
		s_cyl = modelGet(WzString::fromUtf8("modgdistrut_c.pie"));
		s_pis = modelGet(WzString::fromUtf8("modgdistrut_p.pie"));
		s_foot = modelGet(WzString::fromUtf8("modgdistrut_f.pie"));
	}
	if (s_cyl == nullptr || s_pis == nullptr || s_foot == nullptr) { return false; }
	const float sw = smoothStep(0.f, 0.5f, f);     // herunterklappen
	const float ex = smoothStep(0.35f, 0.6f, f);   // Kolben ausfahren bis zum Boden
	const float len = HINGE_Y / (std::cos(ANG_DEPLOYED) * std::cos(STRUT_OUT));
	const float slide = len / STRUT_K - CYL_LEN;
	const float th = ANG_STOWED + (ANG_DEPLOYED - ANG_STOWED) * sw;
	const glm::mat4 K = glm::scale(glm::vec3(STRUT_K));
	bool drew = false;
	for (int sx = -1; sx <= 1; sx += 2)
	{
		const glm::mat4 S = bodyMatrix * glm::translate(glm::vec3(sx * HINGE_X, HINGE_Y, HINGE_Z))
		                    * glm::rotate(sx * STRUT_OUT * sw, glm::vec3(0.f, 0.f, 1.f))
		                    * glm::rotate(-th, glm::vec3(1.f, 0.f, 0.f)) * K;
		drew |= pie_Draw3DShape(s_cyl->displayModel(), 0, colour, brightness, pieFlag, 0, S, viewMatrix, -heightAboveMap);
		drew |= pie_Draw3DShape(s_pis->displayModel(), 0, colour, brightness, pieFlag, 0, S * glm::translate(glm::vec3(0.f, -(1.f + slide * ex), 0.f)), viewMatrix, -heightAboveMap);
		// Fussplatte liegt flach (Lage der Beine) am Ende des Kolbens
		const glm::vec4 end = glm::inverse(bodyMatrix) * S * glm::vec4(0.f, -(CYL_LEN + slide * ex), 0.f, 1.f);
		const glm::mat4 F = bodyMatrix * glm::translate(glm::vec3(end)) * K;
		drew |= pie_Draw3DShape(s_foot->displayModel(), 0, colour, brightness, pieFlag, 0, F, viewMatrix, -heightAboveMap);
	}
	return drew;
}

bool drawHousing(const DROID *d, const glm::mat4 &mountMatrix, const glm::mat4 &viewMatrix, int colour, PIELIGHT brightness, int pieFlag, int pieFlagData, float heightAboveMap)
{
	if (!isGdi(d)) { return false; }
	static const iIMDBaseShape *s_turret = nullptr;
	static unsigned s_gen = 0;
	if (modelCacheStale(s_gen)) { s_turret = modelGet(WzString::fromUtf8("mech_gditurret.pie")); }
	if (s_turret == nullptr) { return false; }
	return pie_Draw3DShape(s_turret->displayModel(), 0, colour, brightness, pieFlag, pieFlagData, mountMatrix, viewMatrix, -heightAboveMap);
}

void reset()
{
	s_state.clear();
	s_weapon[0] = s_weapon[1] = -2;
}
}
