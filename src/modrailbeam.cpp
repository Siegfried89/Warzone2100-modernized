// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: blaue Railgun-Strahlen (siehe modrailbeam.h)
#include "modrailbeam.h"

#include <cmath>
#include <vector>

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/pielighting.h"
#include "lib/ivis_opengl/piepalette.h"

#include "modlayer.h"
#include "projectiledef.h"
#include "stats.h"

#ifndef GLM_ENABLE_EXPERIMENTAL
	#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace modrailbeam
{
namespace
{
constexpr unsigned LIFE_MS = 320;
struct Beam
{
	glm::vec3 a, b;   // Weltkoordinaten der Anzeige (x, Hoehe, -y)
	unsigned t0;
	modlayer::Layer layer;
	bool heavy;
};
std::vector<Beam> s_beams;
const iIMDBaseShape *s_shape = nullptr;
unsigned s_gen = 0;

bool isRail(const WEAPON_STATS *s, bool &heavy)
{
	if (s == nullptr) { return false; }
	heavy = s->id.startsWith("ZoneRail");
	return heavy || s->id.startsWith("CommandoRail");
}
}

void onFire(const PROJECTILE *p)
{
	bool heavy = false;
	if (p == nullptr || !p->bVisible || !isRail(p->psWStats, heavy)) { return; }
	if (s_beams.size() > 64) { s_beams.erase(s_beams.begin()); }
	Beam b;
	b.a = glm::vec3(static_cast<float>(p->src.x), static_cast<float>(p->src.z), -static_cast<float>(p->src.y));
	b.b = glm::vec3(static_cast<float>(p->dst.x), static_cast<float>(p->dst.z), -static_cast<float>(p->dst.y));
	b.t0 = graphicsTime;
	b.layer = p->psSource != nullptr ? modlayer::layerOf(p->psSource) : modlayer::current();
	b.heavy = heavy;
	s_beams.push_back(b);
}

void draw3D(const glm::mat4 &viewMatrix)
{
	if (s_beams.empty()) { return; }
	if (modelCacheStale(s_gen)) { s_shape = modelGet(WzString::fromUtf8("modlaserbeam.pie")); }   // Einheitszylinder (satellite.py)
	const modlayer::Layer view = modlayer::current();
	for (auto it = s_beams.begin(); it != s_beams.end();)
	{
		const unsigned el = graphicsTime - it->t0;
		if (el > LIFE_MS) { it = s_beams.erase(it); continue; }
		if (s_shape == nullptr || it->layer != view) { ++it; continue; }
		const float k = 1.f - static_cast<float>(el) / static_cast<float>(LIFE_MS);
		const glm::vec3 d = it->b - it->a;
		const float len = glm::length(d);
		if (len < 1.f) { ++it; continue; }
		const glm::vec3 dir = d / len;
		const glm::vec3 up(0.f, 1.f, 0.f);
		glm::mat4 rot(1.f);
		const glm::vec3 axis = glm::cross(up, dir);
		const float s = glm::length(axis);
		const float c = glm::dot(up, dir);
		if (s > 1e-4f) { rot = glm::rotate(glm::mat4(1.f), std::atan2(s, c), axis / s); }
		else if (c < 0.f) { rot = glm::rotate(glm::mat4(1.f), 3.14159265f, glm::vec3(1.f, 0.f, 0.f)); }
		const float w = it->heavy ? 1.f : 0.6f;
		for (int layer = 0; layer < 2; ++layer)
		{
			const float rad = (layer == 0 ? 4.2f : 1.3f) * w * (0.6f + 0.4f * k);
			const glm::mat4 m = glm::translate(glm::mat4(1.f), it->a) * rot * glm::scale(glm::mat4(1.f), glm::vec3(rad, len, rad));
			pie_Draw3DShape(s_shape->displayModel(), 0, 0, layer == 0 ? pal_RGBA(40, 130, 255, 255) : pal_RGBA(200, 235, 255, 255), pie_ADDITIVE,
			                static_cast<int>((layer == 0 ? 170.f : 245.f) * k), m, viewMatrix, 0.f, true);
		}
		LIGHT lt;
		lt.position = Vector3i(static_cast<int>(it->b.x), static_cast<int>(it->b.y) + 20, static_cast<int>(-it->b.z));
		lt.range = it->heavy ? 420 : 260;
		lt.colour = pal_Colour(60, 140, 255);
		lt.intensity = 2.2f * k;
		getCurrentLightingData().lights.push_back(lt);
		++it;
	}
}

void reset()
{
	s_beams.clear();
}
}
