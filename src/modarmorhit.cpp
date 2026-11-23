// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Treffer auf Panzerung - Funken und Metallklang (siehe modarmorhit.h)
#include "modarmorhit.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/sound/audio.h"

#include "droid.h"
#include "effects.h"
#include "projectile.h"
#include "stats.h"

#include <cmath>
#include <cstdlib>

namespace modarmorhit
{
namespace
{
const iIMDShape *sparkModel()
{
	static iIMDBaseShape *s_spark = nullptr;
	static unsigned s_tried = 0;
	if (modelCacheStale(s_tried)) { s_spark = modelGet("modspark.pie"); }
	return s_spark ? s_spark->displayModel() : nullptr;
}

const iIMDShape *flashModel()
{
	static iIMDBaseShape *s_flash = nullptr;
	static unsigned s_tried = 0;
	if (modelCacheStale(s_tried)) { s_flash = modelGet("fxmethit.pie"); }
	return s_flash ? s_flash->displayModel() : nullptr;
}

int s_tracks[6] = {-2, -2, -2, -2, -2, -2};
unsigned s_lastSound = 0;

void playClatter(int x, int y)
{
	if (graphicsTime - s_lastSound < 70) { return; }   // nicht zu viele auf einmal (Salven)
	s_lastSound = graphicsTime;
	const int k = rand() % 6;
	if (s_tracks[k] == -2)
	{
		char name[32];
		snprintf(name, sizeof(name), "armorhit_%d.ogg", k);
		s_tracks[k] = audio_GetTrackID(name);
	}
	if (s_tracks[k] >= 0) { audio_PlayStaticTrack(x, y, s_tracks[k]); }
}
}

void sparks(const Vector3i &pos, int count, float speed)
{
	const iIMDShape *m = sparkModel();
	if (m == nullptr) { return; }
	for (int i = 0; i < count; ++i)
	{
		// Halbkugel nach oben, etwas zur Seite gestreut
		const float a = static_cast<float>(rand() % 6283) / 1000.f;
		const float up = 0.25f + static_cast<float>(rand() % 75) / 100.f;
		const float side = std::sqrt(std::max(0.f, 1.f - up * up));
		const float v = speed * (0.45f + static_cast<float>(rand() % 70) / 100.f);
		Vector3f vel(std::cos(a) * side * v, up * v * 1.15f, std::sin(a) * side * v);
		effectGiveAuxVar(static_cast<UDWORD>(260 + rand() % 420));
		Vector3i p = pos;
		addEffect(&p, EFFECT_GRAVITON, GRAVITON_TYPE_SPARK, true, m, 0, graphicsTime, nullptr, &vel);
	}
}

void onHit(const PROJECTILE *proj, const WEAPON_STATS *stats, const Vector3i &pos)
{
	if (proj == nullptr || stats == nullptr || proj->psDest == nullptr || proj->psDest->type != OBJ_DROID) { return; }
	const DROID *d = static_cast<const DROID *>(proj->psDest);
	if (d->droidType == DROID_PERSON || d->isCyborg()) { return; }   // nur Fahrzeuge mit Metallrumpf
	bool heavy = false, light = false;
	switch (stats->weaponSubClass)
	{
	case WSC_CANNON: case WSC_GAUSS: case WSC_ROCKET: case WSC_MISSILE: case WSC_SLOWROCKET: case WSC_SLOWMISSILE:
		heavy = true; break;
	case WSC_MGUN: case WSC_AAGUN:
		light = true; break;
	default:
		break;
	}
	if (!heavy && !light) { return; }
	// etwas ueber dem Bodenpunkt des Treffers (Rumpfhoehe)
	Vector3i p = pos;
	if (p.y < d->pos.z + 6) { p.y = d->pos.z + 6 + rand() % 6; }
	if (light)
	{
		if (rand() % 3 == 0) { sparks(p, 2 + rand() % 2, 160.f); }
		return;
	}
	sparks(p, 14 + rand() % 9, 300.f);
	if (const iIMDShape *fl = flashModel())
	{
		effectSetSize(150);
		addEffect(&p, EFFECT_EXPLOSION, EXPLOSION_TYPE_SPECIFIED, true, fl, 1, graphicsTime);
	}
	playClatter(d->pos.x, d->pos.y);
}
}
