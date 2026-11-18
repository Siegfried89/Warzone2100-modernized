// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: dramatische Rufe bei Gefechten (siehe moddrama.h)
#include "moddrama.h"
#include "lib/framework/frameresource.h"

#include "lib/framework/frame.h"
#include "lib/framework/vector.h"
#include "lib/gamelib/gtime.h"
#include "lib/sound/audio.h"

#include "display3d.h"
#include "game_world.h"
#include "map.h"
#include "modlayer.h"
#include "objectdef.h"
#include "projectile.h"

#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <vector>

namespace moddrama
{
namespace
{
constexpr int CELL = 8 * TILE_UNITS;            // Gefechtsfeld: 8 x 8 Felder
constexpr int NEAR_DIST = 10 * TILE_UNITS;      // bis hier: Nah-Fassung am Ort des Gefechts
constexpr int FAR_DIST = 40 * TILE_UNITS;       // bis hier: Fern-Fassung
constexpr int FAR_PLACE = 7 * TILE_UNITS;       // Fern-Fassung erklingt in dieser Entfernung aus der Richtung des Gefechts (sonst unhoerbar leise)
constexpr unsigned TICK_MS = 500;
constexpr float START_HITS = 5.0f;              // so viele Treffer kurz hintereinander = ein Gefecht beginnt
constexpr float HALF_LIFE_MS = 4000.0f;
constexpr unsigned QUIET_MS = 9000;             // so lange ohne Treffer = Gefecht vorbei
constexpr unsigned GLOBAL_GAP_MS = 3500;        // Abstand zwischen zwei dramatischen Rufen insgesamt

struct Cell
{
	float heat = 0.0f;
	unsigned lastHit = 0;
	unsigned nextCry = 0;
	bool active = false;
	bool own = false;            // eigene Einheiten/Gebaeude beteiligt
	int layer = 0;
	Vector2i pos{0, 0};          // letzter Treffer
};

std::unordered_map<int64_t, Cell> s_cells;
unsigned s_nextTick = 0;
unsigned s_lastCry = 0;
uint32_t s_seed = 0x5eed1234u;
struct Tracks { std::vector<int> n, f; bool probed = false; };
Tracks s_start, s_fight;

uint32_t rnd()
{
	s_seed = s_seed * 1664525u + 1013904223u;
	return s_seed >> 8;
}

void probe(Tracks &t, const char *kind)
{
	if (t.probed) { return; }
	t.probed = true;
	for (int i = 0; i < 32; ++i)
	{
		char n[64], f[64];
		std::snprintf(n, sizeof(n), "drama_%s_%d_n.ogg", kind, i);
		std::snprintf(f, sizeof(f), "drama_%s_%d_f.ogg", kind, i);
		// (18.11., Log: "Unable to find data for drama_start_10_n.ogg") erst pruefen, ob die Datei geladen ist - das Ende der Liste ist kein Fehler
		if (!resPresent("WAV", n) || !resPresent("WAV", f)) { break; }
		const int tn = audio_GetTrackID(n), tf = audio_GetTrackID(f);
		if (tn < 0 || tf < 0) { break; }
		t.n.push_back(tn);
		t.f.push_back(tf);
	}
}

/// spielt einen Ruf der Art fuer ein Gefecht an pos; false, wenn nicht hoerbar
bool cry(Tracks &t, const char *kind, const Cell &c)
{
	probe(t, kind);
	if (t.n.empty()) { return false; }
	if (c.layer != static_cast<int>(modlayer::current())) { return false; }
	const Vector2i cam = getPlayerPos();
	const float dx = static_cast<float>(c.pos.x - cam.x), dy = static_cast<float>(c.pos.y - cam.y);
	const float d = std::sqrt(dx * dx + dy * dy);
	if (d > static_cast<float>(FAR_DIST)) { return false; }
	if (!c.own)
	{
		MAPTILE *tile = worldOnMap(gameWorld.map, c.pos.x, c.pos.y) ? mapTile(gameWorld.map, map_coord(c.pos.x), map_coord(c.pos.y)) : nullptr;
		if (tile == nullptr || !TEST_TILE_VISIBLE_TO_SELECTEDPLAYER(tile)) { return false; }
	}
	const size_t k = rnd() % t.n.size();
	if (d <= static_cast<float>(NEAR_DIST))
	{
		return audio_PlayStaticTrack(c.pos.x, c.pos.y, t.n[k]);
	}
	const float s = static_cast<float>(FAR_PLACE) / d;
	return audio_PlayStaticTrack(cam.x + static_cast<int>(dx * s), cam.y + static_cast<int>(dy * s), t.f[k]);
}

int64_t keyOf(int x, int y)
{
	return (static_cast<int64_t>(x / CELL) << 32) ^ static_cast<int64_t>(static_cast<uint32_t>(y / CELL));
}
} // namespace

void onHit(const BASE_OBJECT *victim, const PROJECTILE *psProj)
{
	if (victim == nullptr || psProj == nullptr) { return; }   // Brand/Dauerschaden zaehlt nicht
	Cell &c = s_cells[keyOf(victim->pos.x, victim->pos.y)];
	const unsigned now = gameTime;
	if (c.lastHit != 0 && now > c.lastHit)
	{
		c.heat *= std::exp2(-static_cast<float>(now - c.lastHit) / HALF_LIFE_MS);
	}
	c.heat += 1.0f;
	c.lastHit = now;
	c.pos = Vector2i(victim->pos.x, victim->pos.y);
	c.layer = static_cast<int>(modlayer::layerOf(victim));
	const BASE_OBJECT *src = psProj->psSource;
	if (victim->player == selectedPlayer || (src != nullptr && src->player == selectedPlayer)) { c.own = true; }
	s_seed ^= static_cast<uint32_t>(victim->id) * 2654435761u;
}

void tick()
{
	const unsigned now = gameTime;
	if (now < s_nextTick) { return; }
	s_nextTick = now + TICK_MS;
	Cell *best = nullptr;
	bool bestStart = false;
	float bestScore = 0.0f;
	const Vector2i cam = getPlayerPos();
	for (auto it = s_cells.begin(); it != s_cells.end();)
	{
		Cell &c = it->second;
		const unsigned since = now - c.lastHit;
		if (since > 30000) { it = s_cells.erase(it); continue; }
		const float heat = c.heat * std::exp2(-static_cast<float>(since) / HALF_LIFE_MS);
		if (c.active && since > QUIET_MS) { c.active = false; c.own = false; }
		bool start = false;
		if (!c.active && heat >= START_HITS)
		{
			c.active = true;
			start = true;
			c.nextCry = now;
		}
		if (c.active && now >= c.nextCry && heat >= 1.5f)
		{
			// naehere und heftigere Gefechte zuerst; Gefechtsbeginn hat Vorrang
			const float dx = static_cast<float>(c.pos.x - cam.x), dy = static_cast<float>(c.pos.y - cam.y);
			const float score = heat / (1.0f + std::sqrt(dx * dx + dy * dy) / static_cast<float>(NEAR_DIST)) * (start ? 3.0f : 1.0f);
			if (score > bestScore) { bestScore = score; best = &c; bestStart = start; }
		}
		++it;
	}
	if (best == nullptr || now - s_lastCry < GLOBAL_GAP_MS) { return; }
	if (cry(bestStart ? s_start : s_fight, bestStart ? "start" : "fight", *best))
	{
		s_lastCry = now;
	}
	best->nextCry = now + 7000 + rnd() % 7000;
}

void reset()
{
	s_cells.clear();
	s_nextTick = 0;
	s_lastCry = 0;
	s_start = Tracks();
	s_fight = Tracks();
}
} // namespace moddrama
