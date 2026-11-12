// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Wracks (siehe modwreck.h)

#include "modwreck.h"

#include "lib/framework/frame.h"
#include "lib/framework/vector.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"

#include "droid.h"
#include "effects.h"
#include "feature.h"
#include "fpath.h"
#include "game_world.h"
#include "map.h"
#include "modlayer.h"
#include "objmem.h"
#include "random.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace modwreck
{
namespace
{
enum Variant { TIP = 0, FLIP = 1, DOUBLE_FLIP = 2 };

struct Wreck
{
	uint32_t id;
	unsigned born;
	int x0, y0;        // Todespunkt
	int xe, ye;        // Endlage (= Position des Features)
	int dirMove;       // Rollrichtung (16-Bit-Winkel)
	int variant;
	unsigned dur;      // Dauer der Todesanimation in ms
	int rollTotal;     // Gesamtdrehung in Grad (mit Vorzeichen)
	int halfH, halfW;  // halbe Modellhoehe / -breite (Lage der Drehachse)
	// nur Anzeige (beeinflusst den Spielzustand nicht)
	bool startFx = false;
	bool crashPlayed = false;
	bool endFx = false;
};

std::deque<Wreck> s_wrecks;
std::vector<FEATURE_STATS *> s_stats[3];   // S / M / L
std::set<const FEATURE_STATS *> s_all;
std::map<std::string, FEATURE_STATS *> s_byBody;
size_t s_cachedNum = 0;
bool s_cached = false;
unsigned s_lastSec = 0;
bool s_have = false;
int s_trackRev = -2, s_trackCrash[3] = {-2, -2, -2};   // -2 = noch nicht gesucht

constexpr unsigned LIFE = 300000;          // Wrack bleibt 5 Minuten
constexpr unsigned SMOKE_ALL = 120000;     // die ersten 2 Minuten jede Sekunde Rauch
constexpr unsigned SMOKE_END = 240000;     // bis 4 Minuten jede zweite Sekunde
constexpr unsigned FIRE_TIME = 90000;      // Brandherd nach dem Stillstand
constexpr unsigned MAX_WRECKS = 80;
constexpr int MIN_DIST = 40;               // Mindestabstand zweier Wracks (Weltmasseinheiten)
constexpr unsigned CHANCE = 80;            // Prozent
constexpr int TILE = 128;

void loadStats()
{
	if (s_cached && s_cachedNum == asFeatureStats.size()) { return; }
	s_cached = true;
	s_cachedNum = asFeatureStats.size();
	for (auto &v : s_stats) { v.clear(); }
	s_all.clear();
	s_byBody.clear();
	for (FEATURE_STATS &fs : asFeatureStats)
	{
		const std::string id = fs.id.toStdString();
		if (id.compare(0, 8, "ModWreck") != 0) { continue; }
		s_all.insert(&fs);
		if (id.compare(0, 10, "ModWreckB_") == 0)
		{
			s_byBody[id.substr(10)] = &fs;   // Wrack fuer genau diesen Rumpf
		}
		else if (id.size() >= 10 && (id[8] == 'S' || id[8] == 'M' || id[8] == 'L'))
		{
			s_stats[id[8] == 'S' ? 0 : (id[8] == 'M' ? 1 : 2)].push_back(&fs);   // allgemeines Wrack nach Groesse
		}
	}
}

void playNamed(const char *file, int &cache, int x, int y)
{
	if (cache == -2) { cache = audio_GetTrackID(file); }
	if (cache >= 0) { audio_PlayStaticTrack(x, y, cache); }
}

struct Pose
{
	int x, y, z, pitch, roll;   // Winkel als 16-Bit-Werte (65536 = 360 Grad)
};

int liftFor(int halfH, int halfW, int rollAngle)
{
	// Hoehe der Drehachse ueber dem Boden, wenn das Modell um rollAngle gedreht liegt (Rechteck-Naeherung)
	const float r = UNDEG(static_cast<uint16_t>(rollAngle & 0xFFFF));
	return static_cast<int>(std::fabs(std::cos(r)) * halfH + std::fabs(std::sin(r)) * halfW) + 1;
}

Pose poseAt(const Wreck &w, unsigned t)   // t = Millisekunden seit dem Tod
{
	Pose p;
	const float u = w.dur > 0 ? std::min(1.f, static_cast<float>(t) / static_cast<float>(w.dur)) : 1.f;
	const float ease = 1.f - (1.f - u) * (1.f - u);   // bremst
	const float dist = std::sqrt(static_cast<float>((w.xe - w.x0) * (w.xe - w.x0) + (w.ye - w.y0) * (w.ye - w.y0)));
	const float dx = dist > 0.f ? static_cast<float>(w.xe - w.x0) / dist : 0.f;
	const float dy = dist > 0.f ? static_cast<float>(w.ye - w.y0) / dist : 0.f;
	p.x = w.x0 + static_cast<int>(dx * dist * ease);
	p.y = w.y0 + static_cast<int>(dy * dist * ease);
	float progress;
	if (w.variant == TIP)
	{
		const float v = std::max(0.f, std::min(1.f, (u - 0.08f) / 0.72f));
		progress = v * v * (3.f - 2.f * v);
		if (u > 0.8f) { progress += 0.035f * std::sin((u - 0.8f) / 0.2f * 3.14159f); }   // federt am Ende leicht nach
	}
	else
	{
		const float v = std::max(0.f, (u - 0.06f) / 0.94f);
		progress = 1.f - std::pow(1.f - v, 1.5f);
	}
	const float rollDeg = static_cast<float>(w.rollTotal) * progress;
	p.roll = static_cast<int>(rollDeg * 65536.f / 360.f) & 0xFFFF;
	// leichtes Nicken, bei Ueberschlaegen kurzer Luftsprung
	p.pitch = static_cast<int>(std::sin(u * 6.2832f * (1.f + w.variant)) * (1.f - u) * 6.f * 65536.f / 360.f) & 0xFFFF;
	const float hop = (w.variant == TIP ? 6.f : (w.variant == FLIP ? 30.f : 46.f)) * std::sin(u * 3.14159f);
	const int ground = map_Height(gameWorld.map, p.x, p.y);
	p.z = ground + liftFor(w.halfH, w.halfW, p.roll) + static_cast<int>(hop);
	return p;
}

bool visibleAt(int x, int y)
{
	MAPTILE *t = mapTile(gameWorld.map, map_coord(x), map_coord(y));
	return t != nullptr && TEST_TILE_VISIBLE_TO_SELECTEDPLAYER(t);
}
} // namespace

bool isWreckStats(const FEATURE_STATS *stats)
{
	if (stats == nullptr) { return false; }
	loadStats();
	return s_all.find(stats) != s_all.end();
}

bool pose(const FEATURE *f, unsigned renderTime, Placement &out)
{
	if (f == nullptr) { return false; }
	for (const Wreck &w : s_wrecks)
	{
		if (w.id != f->id) { continue; }
		const unsigned t = renderTime > w.born ? renderTime - w.born : 0;
		if (t >= w.dur) { return false; }
		const Pose p = poseAt(w, t);
		out.x = p.x; out.y = p.y; out.z = p.z; out.pitch = p.pitch; out.roll = p.roll;
		return true;
	}
	return false;
}

bool onDroidDestroyed(const DROID *psDel, GameWorld &world)
{
	if (psDel == nullptr || &world != &gameWorld) { return false; }
	loadStats();
	if (s_all.empty()) { return false; }
	// nur Bodenfahrzeuge: keine Personen, Cyborgs, Transporter, VTOLs
	if (psDel->droidType == DROID_PERSON || psDel->isCyborg() || psDel->isTransporter() || psDel->isVtol()) { return false; }
	if (modlayer::layerOf(psDel) != modlayer::SURFACE) { return false; }

	const int x = psDel->pos.x;
	const int y = psDel->pos.y;
	if (!worldOnMap(world.map, x, y)) { return false; }
	MAPTILE *tile = mapTile(world.map, map_coord(x), map_coord(y));
	if (tile == nullptr || tile->psObject != nullptr) { return false; }   // Bauwerk / Feature auf der Kachel
	const unsigned char tt = terrainType(tile);
	if (tt == TER_WATER || tt == TER_CLIFFFACE) { return false; }

	for (const Wreck &w : s_wrecks)
	{
		const int dx = w.xe - x, dy = w.ye - y;
		if (dx * dx + dy * dy < MIN_DIST * MIN_DIST) { return false; }
	}

	if (gameRand(100) >= CHANCE) { return false; }   // 20 %: normale Zerstoerung

	// ---- Todesanimation festlegen (alles ganzzahlig und aus gameRand: auf allen Rechnern gleich)
	const int v0 = psDel->sMove.speed;
	const bool moving = v0 >= 60;
	const int dirMove = moving ? static_cast<int>(psDel->sMove.moveDir) : static_cast<int>(psDel->rot.direction);
	int variant = TIP;
	int tiles = 0;   // Rollweite in Kacheln
	{
		const unsigned r = gameRand(100);
		if (moving)
		{
			variant = r < 60 ? TIP : (r < 88 ? FLIP : DOUBLE_FLIP);
			if (variant == TIP) { const unsigned q = gameRand(10); tiles = q < 5 ? 1 : (q < 9 ? 2 : 3); }
			else if (variant == FLIP) { tiles = 1; }
			else { tiles = 3; }
		}
	}
	const int sign = gameRand(2) == 0 ? 1 : -1;
	int rollTotal = 90;
	if (variant == FLIP) { rollTotal = gameRand(2) == 0 ? 270 : 180; }
	else if (variant == DOUBLE_FLIP) { rollTotal = gameRand(2) == 0 ? 630 : 540; }
	rollTotal *= sign;

	// Weg pruefen: bei Hindernissen frueher stehen bleiben
	const int h0 = map_Height(world.map, x, y);
	int reach = 0;
	for (int s = 16; s <= tiles * TILE; s += 16)
	{
		const int px = x + iSinR(static_cast<uint16_t>(dirMove), s);
		const int py = y + iCosR(static_cast<uint16_t>(dirMove), s);
		if (!worldOnMap(world.map, px, py)) { break; }
		const int tx = map_coord(px), ty = map_coord(py);
		if (fpathBlockingTile(world.map, tx, ty, PROPULSION_TYPE_WHEELED)) { break; }
		MAPTILE *t = mapTile(world.map, tx, ty);
		if (t == nullptr || t->psObject != nullptr) { break; }
		const unsigned char ttt = terrainType(t);
		if (ttt == TER_WATER || ttt == TER_CLIFFFACE) { break; }
		if (std::abs(map_Height(world.map, px, py) - h0) > 90) { break; }
		reach = s;
	}
	if (reach < 24) { reach = 0; }
	if (reach == 0 && variant != TIP) { variant = TIP; rollTotal = sign * 90; }
	const int xe = x + iSinR(static_cast<uint16_t>(dirMove), reach);
	const int ye = y + iCosR(static_cast<uint16_t>(dirMove), reach);

	const BODY_STATS *body = psDel->getBodyStats();
	const BODY_SIZE size = body != nullptr ? body->size : SIZE_MEDIUM;
	FEATURE_STATS *stat = nullptr;
	if (body != nullptr)
	{
		const auto it = s_byBody.find(body->id.toStdString());
		if (it != s_byBody.end()) { stat = it->second; }
	}
	if (stat == nullptr)
	{
		int cls = size <= SIZE_LIGHT ? 0 : (size == SIZE_MEDIUM ? 1 : 2);
		if (s_stats[cls].empty())
		{
			for (int c = 0; c < 3; c++) { if (!s_stats[c].empty()) { cls = c; break; } }
		}
		if (s_stats[cls].empty()) { return false; }
		stat = s_stats[cls][gameRand(static_cast<uint32_t>(s_stats[cls].size()))];
	}

	while (s_wrecks.size() >= MAX_WRECKS)
	{
		FEATURE *old = static_cast<FEATURE *>(getBaseObjFromId(world.objects.features[0], s_wrecks.front().id));
		if (old != nullptr && !old->died) { removeFeature(old, world); }
		s_wrecks.pop_front();
	}

	FEATURE *f = buildFeature(world, stat, xe, ye, false);
	if (f == nullptr) { return false; }
	// wer das Fahrzeug sah, sieht auch das Wrack sofort (sonst wuerde die Todesanimation die ersten Zehntelsekunden fehlen)
	for (int p = 0; p < MAX_PLAYERS; p++) { f->visible[p] = psDel->visible[p]; }

	Wreck w;
	w.id = f->id;
	w.born = gameTime;
	w.x0 = x; w.y0 = y; w.xe = xe; w.ye = ye;
	w.dirMove = dirMove;
	w.variant = variant;
	const int vEff = std::max(v0, 250);
	const int ms = reach > 0 ? 2000 * reach / vEff : 0;
	w.dur = variant == TIP ? static_cast<unsigned>(std::max(800, std::min(2200, ms)))
	      : (variant == FLIP ? static_cast<unsigned>(std::max(1500, std::min(2000, ms))) : static_cast<unsigned>(std::max(2400, std::min(3200, ms))));
	w.rollTotal = rollTotal;
	const auto *imd = stat->psImd;
	w.halfH = imd != nullptr ? (imd->max.y - imd->min.y) / 2 : 12;
	w.halfW = imd != nullptr ? (imd->max.x - imd->min.x) / 2 : 12;

	// Endlage ins Feature schreiben (Richtung des Fahrzeugs, gekippt wie am Ende der Animation)
	const int finalRoll = static_cast<int>((static_cast<int64_t>(rollTotal) * 65536 / 360) & 0xFFFF);
	const int ground = map_Height(world.map, xe, ye);
	const int lift = std::abs(iCosR(static_cast<uint16_t>(finalRoll), w.halfH)) + std::abs(iSinR(static_cast<uint16_t>(finalRoll), w.halfW)) + 1;
	featureSetWreckPose(f, psDel->rot.direction, finalRoll, ground + lift);
	s_wrecks.push_back(w);
	return true;
}

void tick(GameWorld &world)
{
	if (s_wrecks.empty()) { return; }
	const bool newSecond = !(s_have && gameTime / 1000 == s_lastSec);
	if (newSecond) { s_have = true; s_lastSec = gameTime / 1000; }
	const unsigned it = gameTime - deltaGameTime / 2 + 1;
	for (auto w = s_wrecks.begin(); w != s_wrecks.end();)
	{
		const unsigned age = gameTime - w->born;
		// ---- Todesanimation: Anzeige-Effekte in jedem Spielschritt (nur lokal, aendert den Spielzustand nicht)
		if (age <= w->dur + 100)
		{
			const Pose p = poseAt(*w, std::min(age, w->dur));
			const bool seen = visibleAt(p.x, p.y);
			if (!w->startFx)
			{
				// Beginn: Explosion am Todespunkt, Motorheulen
				w->startFx = true;
				if (visibleAt(w->x0, w->y0))
				{
					Vector3i pos(w->x0, p.z + 14, w->y0);
					addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_MEDIUM, false, nullptr, 0, it);
					audio_PlayStaticTrack(w->x0, w->y0, ID_SOUND_EXPLOSION);
					if (w->xe != w->x0 || w->ye != w->y0) { playNamed("wreckrev.ogg", s_trackRev, w->x0, w->y0); }
				}
			}
			if (seen && age < w->dur)
			{
				// Rauchspur und Funken waehrend der Fahrt
				const int ox = static_cast<int>((age / 100 * 37u + w->id * 11u) % 17u) - 8;
				const int oy = static_cast<int>((age / 100 * 53u + w->id * 7u) % 17u) - 8;
				Vector3i sp(p.x + ox, p.z + 22, p.y + oy);
				addEffect(&sp, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING_HIGH, false, nullptr, 0, it);
				if ((age / 100) % 3 == 0)
				{
					Vector3i ep(p.x + oy, p.z + 12, p.y + ox);
					addEffect(&ep, EFFECT_EXPLOSION, EXPLOSION_TYPE_SMALL, false, nullptr, 0, it);
				}
			}
			// Aufprall-/Schleifgeraeusch (kurz nach Beginn des Kippens; laenger bei Ueberschlaegen)
			if (!w->crashPlayed && age >= w->dur / 4)
			{
				w->crashPlayed = true;
				if (seen)
				{
					const char *snd = w->variant == TIP ? "wreckcrash_a.ogg" : (w->variant == FLIP ? "wreckcrash_b.ogg" : "wreckcrash_c.ogg");
					playNamed(snd, s_trackCrash[w->variant], p.x, p.y);
				}
			}
			if (!w->endFx && age >= w->dur)
			{
				w->endFx = true;
				if (seen)
				{
					// Endlage: Brandherd, der einige Zeit Flammen und Rauch erzeugt
					Vector3i pos(w->xe, p.z + 10, w->ye);
					addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_SMALL, false, nullptr, 0, it);
					effectGiveAuxVar(16);
					effectGiveAuxVarSec(FIRE_TIME);
					Vector3i fp(w->xe, p.z + 8, w->ye);
					addEffect(&fp, EFFECT_FIRE, FIRE_TYPE_SMOKY, false, nullptr, 0, it);
				}
			}
		}
		if (!newSecond) { ++w; continue; }

		FEATURE *f = static_cast<FEATURE *>(getBaseObjFromId(world.objects.features[0], w->id));
		if (f == nullptr || f->died)
		{
			w = s_wrecks.erase(w);
			continue;
		}
		if (age >= LIFE)
		{
			removeFeature(f, world);
			w = s_wrecks.erase(w);
			continue;
		}
		const bool smoke = age >= w->dur && (age < SMOKE_ALL || (age < SMOKE_END && ((gameTime / 1000) & 1) == 0));
		if (smoke && f->visibleForLocalDisplay())
		{
			// Versatz nur aus der Objekt-ID (keine Spielzufallszahl: dieser Zweig haengt von der lokalen Sichtbarkeit ab)
			const int ox = static_cast<int>((w->id * 37u) % 21u) - 10;
			const int oy = static_cast<int>((w->id * 53u) % 21u) - 10;
			Vector3i sp(w->xe + ox, f->pos.z + 20, w->ye + oy);
			addEffect(&sp, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING_HIGH, false, nullptr, 0, it);
		}
		++w;
	}
}

void reset()
{
	s_wrecks.clear();
	s_cached = false;
	s_cachedNum = 0;
	s_have = false;
	s_trackRev = -2;
	for (int &t : s_trackCrash) { t = -2; }
	for (auto &v : s_stats) { v.clear(); }
	s_all.clear();
	s_byBody.clear();
}
}
