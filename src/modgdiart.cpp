// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: GDI-Artillerie verankert sich zum Feuern wie der C&C3-Juggernaut (siehe modgdiart.h)
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
constexpr float LIFT = 2.5f;           // so weit hebt sich der Rumpf
constexpr float HINGE_X = 6.8f, HINGE_Y = 12.5f, HINGE_Z = 1.2f;   // Gelenke der Stuetzen an den Beinen (Rumpf-Modell scbd_std)
constexpr float STRUT_OUT = 0.5236f;   // 30 Grad nach aussen
constexpr float STRUT_SPLAY = 0.384f;  // 22 Grad nach vorn bzw. hinten
constexpr float CYL_LEN = 8.5f;        // Gelenk bis Kolbenende bei eingefahrenem Kolben (Modelle aus gdiart.py)

std::unordered_map<uint32_t, State> s_state;
int s_weapon = -2;

int gdiWeapon()
{
	if (s_weapon == -2) { s_weapon = getCompFromID(COMP_WEAPON, WzString::fromUtf8("CyborgGDIArtillery")); }
	return s_weapon;
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
	const int w = gdiWeapon();
	return w > 0 && weaponStat == static_cast<uint32_t>(w);
}

bool isGdi(const DROID *d)
{
	const int w = gdiWeapon();
	return d != nullptr && w > 0 && d->numWeaps > 0 && d->asWeaps[0].nStat == static_cast<uint32_t>(w);
}

bool readyToFire(DROID *d)
{
	if (!isGdi(d)) { return true; }
	State &s = s_state[d->id];
	switch (s.mode)
	{
	case DEPLOYED:
		return true;
	case MOBILE:
		s.mode = DEPLOYING;   // erst verankern
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
	return LIFT * smoothStep(0.3f, 0.65f, deployFraction(d, time));
}

uint16_t displayPitch(const DROID *d, uint16_t aimPitch, UDWORD time)
{
	if (!isGdi(d)) { return aimPitch; }
	const float b = smoothStep(0.55f, 1.f, deployFraction(d, time));
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
	const float sw = smoothStep(0.f, 0.45f, f);   // ausschwenken
	const float ex = smoothStep(0.1f, 0.5f, f);   // Kolben ausfahren
	const float len = HINGE_Y / (std::cos(STRUT_OUT) * std::cos(STRUT_SPLAY));
	const float slide = len - CYL_LEN;
	bool drew = false;
	for (int sx = -1; sx <= 1; sx += 2)
	{
		for (int sz = -1; sz <= 1; sz += 2)
		{
			const glm::mat4 S = bodyMatrix * glm::translate(glm::vec3(sx * HINGE_X, HINGE_Y, sz * HINGE_Z))
			                    * glm::rotate(sx * STRUT_OUT * sw, glm::vec3(0.f, 0.f, 1.f))
			                    * glm::rotate(-sz * STRUT_SPLAY * sw, glm::vec3(1.f, 0.f, 0.f));
			drew |= pie_Draw3DShape(s_cyl->displayModel(), 0, colour, brightness, pieFlag, 0, S, viewMatrix, -heightAboveMap);
			const float down = 1.f + slide * ex;
			drew |= pie_Draw3DShape(s_pis->displayModel(), 0, colour, brightness, pieFlag, 0, S * glm::translate(glm::vec3(0.f, -down, 0.f)), viewMatrix, -heightAboveMap);
			// Fussplatte liegt flach (Lage des Rumpfs) am Ende des Kolbens
			const glm::vec4 end = glm::inverse(bodyMatrix) * S * glm::vec4(0.f, -(CYL_LEN + slide * ex), 0.f, 1.f);
			const glm::mat4 F = bodyMatrix * glm::translate(glm::vec3(end));
			drew |= pie_Draw3DShape(s_foot->displayModel(), 0, colour, brightness, pieFlag, 0, F, viewMatrix, -heightAboveMap);
		}
	}
	return drew;
}

void reset()
{
	s_state.clear();
	s_weapon = -2;
}
}
