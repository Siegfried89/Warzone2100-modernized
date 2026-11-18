// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Scheinwerfer an Fahrzeugen bei Nacht (siehe modheadlights.h)
#include "modheadlights.h"

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/pielighting.h"
#include "lib/ivis_opengl/piepalette.h"

#include "baseobject.h"
#include "display3d.h"
#include "droid.h"
#include "game_world.h"
#include "map.h"
#include "modlayer.h"
#include "modweather.h"
#include "stats.h"
#include "terrain.h"

#include <algorithm>
#include <cmath>
#include <vector>

#ifndef GLM_ENABLE_EXPERIMENTAL
	#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>

namespace modheadlights
{
namespace
{
constexpr size_t DOUBLE_LIT = 40;    // so viele Fahrzeuge (die naechsten zur Kamera) werfen zwei Lichtkegel
constexpr size_t SINGLE_LIT = 60;    // die naechsten danach einen gemeinsamen
constexpr float TILT = 0.16f;        // Kegel leicht nach unten (ca. 9 Grad)
constexpr float BEAM_TILT = 0.07f;   // sichtbarer Lichtkegel: flacher (ca. 4 Grad), damit er erst ein Stueck vor dem Fahrzeug im Boden versinkt

bool eligible(const DROID *d)
{
	if (d == nullptr || d->died || d->visibleForLocalDisplay() != UBYTE_MAX)   // nur ganz sichtbare (keine Radar-Punkte)
	{
		return false;
	}
	if (d->droidType == DROID_PERSON || d->isCyborg() || d->isTransporter())
	{
		return false;
	}
	const PROPULSION_STATS *prop = d->getPropulsionStats();
	if (prop == nullptr || prop->propulsionType == PROPULSION_TYPE_LIFT || prop->propulsionType == PROPULSION_TYPE_LEGGED)
	{
		return false;   // Flieger und Laeufer (Combots) sind keine Fahrzeuge
	}
	return d->sDisplay.frameNumber == frameGetFrameNumber();   // in diesem Bild gezeichnet (auf dem Schirm)
}

/// wie displayComponentObject(): Lage des Fahrzeugs im Bild (ohne Wackeln bei E-Treffern)
glm::mat4 droidMatrix(const DROID *d)
{
	const Spacetime st = interpolateObjectSpacetime(d, graphicsTime);
	glm::vec3 position(static_cast<float>(st.pos.x), static_cast<float>(st.pos.z), static_cast<float>(-st.pos.y));
	position.y += getTerrainVisualObjectHeightDelta(gameWorld.map, st.pos.x, st.pos.y, st.pos.z);
	position.y += modlayer::transitionOffset(d, graphicsTime);
	return glm::translate(position) *
		glm::rotate(UNDEG(-st.rot.direction), glm::vec3(0.f, 1.f, 0.f)) *
		glm::rotate(UNDEG(st.rot.pitch), glm::vec3(1.f, 0.f, 0.f)) *
		glm::rotate(UNDEG(st.rot.roll), glm::vec3(0.f, 0.f, 1.f));
}

/// rechte Lampe im Modell (die linke ist gespiegelt): aus body.json, sonst aus den Abmessungen des Rumpfs (vorn = -z)
glm::vec3 lampLocal(const DROID *d, float &halfWidth)
{
	const BODY_STATS *b = d->getBodyStats();
	const iIMDShape *imd = (b != nullptr && b->pIMD != nullptr) ? b->pIMD->displayModel() : nullptr;
	halfWidth = 12.f;
	if (imd != nullptr)
	{
		halfWidth = std::max(4.f, static_cast<float>(std::max(-imd->min.x, imd->max.x)));
	}
	if (b != nullptr && b->hasHeadlight)
	{
		return glm::vec3(std::abs(b->headlight[0]), b->headlight[1], b->headlight[2]);
	}
	if (imd == nullptr)
	{
		return glm::vec3(6.f, 10.f, -20.f);
	}
	const float y = static_cast<float>(imd->min.y) + 0.45f * static_cast<float>(imd->max.y - imd->min.y);
	return glm::vec3(0.55f * halfWidth, y, static_cast<float>(imd->min.z) + 1.f);
}

/// Ruecklicht rechts im Modell (hinten = +z): aussen, halbe Hoehe
glm::vec3 tailLocal(const DROID *d, float halfWidth)
{
	const BODY_STATS *b = d->getBodyStats();
	const iIMDShape *imd = (b != nullptr && b->pIMD != nullptr) ? b->pIMD->displayModel() : nullptr;
	if (imd == nullptr)
	{
		return glm::vec3(0.6f * halfWidth, 10.f, 20.f);
	}
	const float y = static_cast<float>(imd->min.y) + 0.5f * static_cast<float>(imd->max.y - imd->min.y);
	return glm::vec3(0.62f * halfWidth, y, static_cast<float>(imd->max.z) - 0.5f);
}

struct Cand
{
	glm::vec3 lamp[2];      // Bild-Koordinaten
	glm::vec3 dir;          // Bild-Koordinaten, normiert
	int64_t d2;
	float scale;
};

Vector3i toGame(const glm::vec3 &r)
{
	return Vector3i(static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(-r.z));
}
} // namespace

void draw3D(const glm::mat4 &viewMatrix)
{
	const float k = modweather::visualNight();
	if (k < 0.05f)
	{
		return;
	}
	static const iIMDBaseShape *s_shape = nullptr, *s_tail = nullptr, *s_beam = nullptr;
	static unsigned s_gen = 0;
	if (modelCacheStale(s_gen))
	{
		s_shape = modelGet(WzString::fromUtf8("modheadlamp.pie"));
		s_tail = modelGet(WzString::fromUtf8("modtaillamp.pie"));
		s_beam = modelGet(WzString::fromUtf8("modheadbeam.pie"));
	}
	const iIMDShape *shape = s_shape != nullptr ? s_shape->displayModel() : nullptr;
	const iIMDShape *tail = s_tail != nullptr ? s_tail->displayModel() : nullptr;
	const iIMDShape *beam = s_beam != nullptr ? s_beam->displayModel() : nullptr;
	const int beamAlpha = std::clamp(static_cast<int>(150.f * k), 0, 255);   // sichtbarer Lichtkegel in der Luft (WZ-Intro)
	const int tailAlpha = std::clamp(static_cast<int>(210.f * k), 0, 255);
	const glm::mat4 bill(glm::transpose(glm::mat3(viewMatrix)));
	const int alpha = std::clamp(static_cast<int>(255.f * k), 0, 255);
	const modlayer::Layer view = modlayer::current();
	std::vector<Cand> cands;
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (const DROID *d : gameWorld.objects.droids[p])
		{
			if (!eligible(d) || modlayer::layerOf(d) != view)
			{
				continue;
			}
			float halfW = 12.f;
			const glm::vec3 L = lampLocal(d, halfW);
			const glm::mat4 M = droidMatrix(d);
			Cand c;
			for (int s = 0; s < 2; ++s)
			{
				const float sx = s == 0 ? L.x : -L.x;
				c.lamp[s] = glm::vec3(M * glm::vec4(sx, L.y, L.z - 1.5f, 1.f));   // knapp vor der Panzerung (von hinten verdeckt)
			}
			c.dir = glm::normalize(glm::mat3(M) * glm::vec3(0.f, -TILT, -1.f));
			c.scale = std::clamp(halfW * 0.028f, 0.15f, 0.6f);
			const int64_t dx = static_cast<int64_t>(c.lamp[0].x) - playerPos.p.x, dz = static_cast<int64_t>(-c.lamp[0].z) - playerPos.p.z;
			c.d2 = dx * dx + dz * dz;
			if (shape != nullptr)
			{
				for (int s = 0; s < 2; ++s)
				{
					const glm::mat4 bb = glm::translate(glm::mat4(1.f), c.lamp[s]) * glm::mat4(bill) * glm::scale(glm::mat4(1.f), glm::vec3(c.scale));
					pie_Draw3DShape(shape, 0, 0, WZCOL_WHITE, pie_ADDITIVE, alpha, bb, viewMatrix, 0.f, true);
				}
			}
			// sichtbarer Lichtkegel vor jeder Lampe (wie im WZ-Intro): leicht nach unten, verblasst nach vorn, versinkt im Boden
			if (beam != nullptr)
			{
				const float len = std::clamp(halfW * 7.f + 60.f, 90.f, 400.f);
				for (int s = 0; s < 2; ++s)
				{
					const float sx = s == 0 ? L.x : -L.x;
					const glm::mat4 bm = M * glm::translate(glm::vec3(sx, L.y, L.z - 1.f)) * glm::rotate(-BEAM_TILT, glm::vec3(1.f, 0.f, 0.f))
					                     * glm::scale(glm::vec3(len, len, len));
					pie_Draw3DShape(beam, 0, 0, WZCOL_WHITE, pie_ADDITIVE, beamAlpha, bm, viewMatrix, 0.f, true);
				}
			}
			// zwei rote Ruecklichter hinten (wie am Lkw im Intro), kleiner, ohne Lichtwurf
			if (tail != nullptr)
			{
				const glm::vec3 T = tailLocal(d, halfW);
				for (int s = 0; s < 2; ++s)
				{
					const glm::vec3 tp = glm::vec3(M * glm::vec4(s == 0 ? T.x : -T.x, T.y, T.z + 1.f, 1.f));
					const glm::mat4 bb = glm::translate(glm::mat4(1.f), tp) * glm::mat4(bill) * glm::scale(glm::mat4(1.f), glm::vec3(c.scale * 0.55f));
					pie_Draw3DShape(tail, 0, 0, WZCOL_WHITE, pie_ADDITIVE, tailAlpha, bb, viewMatrix, 0.f, true);
				}
			}
			cands.push_back(c);
		}
	}
	if (cands.empty())
	{
		return;
	}
	// Lichtkegel: die naechsten zur Kamera mit zwei Kegeln, die folgenden mit einem gemeinsamen
	const size_t n = std::min(cands.size(), DOUBLE_LIT + SINGLE_LIT);
	std::partial_sort(cands.begin(), cands.begin() + static_cast<std::ptrdiff_t>(n), cands.end(), [](const Cand &a, const Cand &b) { return a.d2 < b.d2; });
	auto &lights = getCurrentLightingData().lights;
	for (size_t i = 0; i < n; ++i)
	{
		const Cand &c = cands[i];
		const glm::vec3 dg(c.dir.x, c.dir.y, -c.dir.z);
		auto spot = [&](const glm::vec3 &pos, float intensity, unsigned range)
		{
			LIGHT l;
			l.position = toGame(pos);
			l.range = range;
			l.colour = pal_Colour(255, 238, 205);   // Halogen, warmweiss
			l.intensity = intensity * k;
			l.direction = Vector3f(dg.x, dg.y, dg.z);
			l.cosOuter = 0.90f;                       // halber Oeffnungswinkel ca. 26 Grad
			lights.push_back(l);
		};
		const unsigned range = static_cast<unsigned>(std::clamp(460.f * (0.8f + c.scale), 380.f, 760.f));
		if (i < DOUBLE_LIT)
		{
			spot(c.lamp[0], 1.35f, range);
			spot(c.lamp[1], 1.35f, range);
		}
		else
		{
			spot((c.lamp[0] + c.lamp[1]) * 0.5f, 1.9f, range);
		}
	}
}
}
