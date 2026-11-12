// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Funkgebrabbel (siehe modradio.h)

#include "modradio.h"

#include "lib/framework/frame.h"
#include "lib/framework/vector.h"
#include "lib/gamelib/gtime.h"
#include "lib/sound/audio.h"

#include "display3d.h"
#include "feature.h"
#include "game_world.h"
#include "map.h"
#include "objmem.h"
#include "structure.h"

#include <cstdint>
#include <string>
#include <vector>

namespace modradio
{
namespace
{
constexpr int NUM_CLIPS = 4;
const char *const CLIP_FILES[NUM_CLIPS] = {"atc_a.ogg", "atc_b.ogg", "atc_c.ogg", "atc_d.ogg"};
constexpr int HEAR_RANGE = 14 * TILE_UNITS;     // Kamera hoechstens so weit entfernt (Weltkoordinaten)
constexpr unsigned MIN_GAP_MS = 6000;           // kleinste Pause zwischen zwei Durchsagen
constexpr unsigned MAX_EXTRA_MS = 11000;        // dazu zufaellig 0..MAX_EXTRA_MS

int s_track[NUM_CLIPS] = {-2, -2, -2, -2};      // -2 = Sound noch nicht gesucht, -1 = nicht vorhanden
unsigned s_next = 0;                            // fruehester Zeitpunkt der naechsten Durchsage (gameTime)
unsigned s_rng = 0x2545F491u;                   // eigener Zufall (nur Ton, nicht synchronisationsrelevant)
int s_last = -1;

unsigned rnd()
{
	s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
	return s_rng;
}

bool visibleAt(int x, int y)
{
	MAPTILE *t = mapTile(gameWorld.map, map_coord(x), map_coord(y));
	return t != nullptr && TEST_TILE_VISIBLE_TO_SELECTEDPLAYER(t);
}

bool nearCamera(int x, int y, const Vector2i &cam)
{
	const int64_t dx = x - cam.x, dy = y - cam.y;
	return dx * dx + dy * dy <= static_cast<int64_t>(HEAR_RANGE) * HEAR_RANGE;
}
} // namespace

void reset()
{
	s_next = 0;
	s_last = -1;
	for (int &t : s_track) { t = -2; }
}

void tick(GameWorld &world)
{
	const unsigned now = gameTime;
	if (now < s_next) { return; }
	if (s_next == 0) { s_next = now + 2000 + rnd() % 4000; return; }   // nach Spielstart / Laden nicht sofort
	s_next = now + MIN_GAP_MS + rnd() % MAX_EXTRA_MS;

	const Vector2i cam = getPlayerPos();
	std::vector<Vector2i> sources;
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		for (const STRUCTURE *s : world.objects.structures[p])
		{
			if (s == nullptr || s->died || s->status != SS_BUILT || s->pStructureType == nullptr || s->pStructureType->type != REF_HQ) { continue; }
			if (nearCamera(s->pos.x, s->pos.y, cam) && visibleAt(s->pos.x, s->pos.y)) { sources.push_back(Vector2i(s->pos.x, s->pos.y)); }
		}
	}
	for (const FEATURE *f : world.objects.features[0])
	{
		if (f == nullptr || f->died || f->psStats == nullptr) { continue; }
		if (f->psStats->id.toUtf8() != "AirTrafficControl") { continue; }
		if (nearCamera(f->pos.x, f->pos.y, cam) && visibleAt(f->pos.x, f->pos.y)) { sources.push_back(Vector2i(f->pos.x, f->pos.y)); }
	}
	if (sources.empty()) { return; }

	const Vector2i src = sources[rnd() % sources.size()];
	int clip = static_cast<int>(rnd() % NUM_CLIPS);
	if (clip == s_last) { clip = (clip + 1) % NUM_CLIPS; }     // nicht zweimal dasselbe Stueck hintereinander
	s_last = clip;
	if (s_track[clip] == -2) { s_track[clip] = audio_GetTrackID(CLIP_FILES[clip]); }
	if (s_track[clip] >= 0) { audio_PlayStaticTrack(src.x, src.y, s_track[clip]); }
	// langes Stueck: naechste Durchsage erst danach
	s_next += 4000;
}
}
