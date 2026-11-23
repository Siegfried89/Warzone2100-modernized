// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Wracks (siehe modwreck.h)

#include "modwreck.h"

#include "lib/framework/frame.h"
#include "lib/framework/vector.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/ivisdef.h"
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
#include "projectile.h"
#include "random.h"
#include "stats.h"

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
enum Variant { TIP = 0, FLIP = 1, DOUBLE_FLIP = 2, LAUNCH = 3 };   // LAUNCH (23.11. 03:05): Rumpf von starker Explosion hochgeschleudert

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
	bool blockDone = false;   // Sperre der Kachel schon geprueft/gesetzt (Wrack ist gelandet)
	bool ownBlock = false;    // diese Sperre stammt von uns und wird beim Entfernen wieder aufgehoben
	// (17.11.) Bauplan fuer das Bergen
	modwreck::WreckInfo info;
	bool hasInfo = false;
	// nur Anzeige (beeinflusst den Spielzustand nicht)
	bool startFx = false;
	bool crashPlayed = false;
	bool endFx = false;
	// (23.11. 03:05) hochgeschleuderter Rumpf
	bool hull = false;     // zeigt das Rumpf-Modell (verkohlt)
	int pitchTotal = 0;    // Ueberschlag nach vorn/hinten in Grad
	int height = 0;        // Gipfelhoehe der Flugbahn ueber dem Boden
	int halfL = 12;        // halbe Laenge (Ueberschlag nach vorn)
	int h0 = 0, hE = 0;    // Bodenhoehe am Start / an der Landestelle
};

// der toedliche Treffer (noteKill) - nur fuer genau dieses Fahrzeug in genau diesem Spielschritt
struct KillNote
{
	uint32_t id = 0;
	uint32_t time = 0;
	int fromX = 0, fromY = 0;
	unsigned damage = 0;
	bool strong = false;
};
KillNote s_kill;

std::deque<Wreck> s_wrecks;
std::vector<FEATURE_STATS *> s_stats[3];   // S / M / L
std::set<const FEATURE_STATS *> s_all;
std::map<std::string, FEATURE_STATS *> s_byBody;
size_t s_cachedNum = 0;
bool s_cached = false;
unsigned s_lastSec = 0;
bool s_have = false;
int s_trackRev = -2, s_trackCrash[3] = {-2, -2, -2};   // -2 = noch nicht gesucht

constexpr unsigned LIFE = 1500000;         // Wrack bleibt 25 Minuten (Wunsch: mindestens 20), kann vorher geborgen werden
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

// MOD (Wunsch 14.11.): ein gelandetes Wrack sperrt seine Kachel fuer Bodeneinheiten (Panzerwrack blockiert die Fahrer dahinter)
void releaseBlock(GameWorld &world, Wreck &w)
{
	if (!w.ownBlock) { return; }
	w.ownBlock = false;
	auxClearBlocking(world.map, map_coord(w.xe), map_coord(w.ye), FEATURE_BLOCKED);
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

Pose launchPose(const Wreck &w, unsigned t)
{
	// Drehpunkt = Modellmitte (das Rumpf-Wrack wird beim Zeichnen auf seine Mitte gesetzt)
	Pose p;
	const float u = w.dur > 0 ? std::min(1.f, static_cast<float>(t) / static_cast<float>(w.dur)) : 1.f;
	constexpr float F = 0.84f;   // Anteil der Flugzeit, danach Aufprall und Nachfedern
	float rollDeg, pitchDeg, ground, air;
	if (u < F)
	{
		const float s = u / F;
		p.x = w.x0 + static_cast<int>(static_cast<float>(w.xe - w.x0) * s);
		p.y = w.y0 + static_cast<int>(static_cast<float>(w.ye - w.y0) * s);
		rollDeg = static_cast<float>(w.rollTotal) * s;
		pitchDeg = static_cast<float>(w.pitchTotal) * s;
		ground = static_cast<float>(w.h0) + static_cast<float>(w.hE - w.h0) * s;
		air = static_cast<float>(w.height) * 4.f * s * (1.f - s);
	}
	else
	{
		const float b = (u - F) / (1.f - F);
		p.x = w.xe; p.y = w.ye;
		const float wob = std::sin(b * 9.4248f) * (1.f - b);
		rollDeg = static_cast<float>(w.rollTotal) + 9.f * wob;
		pitchDeg = static_cast<float>(w.pitchTotal) - 6.f * wob;
		ground = static_cast<float>(w.hE);
		air = 16.f * std::fabs(std::sin(b * 3.14159f)) * (1.f - b);
	}
	p.roll = static_cast<int>(rollDeg * 65536.f / 360.f) & 0xFFFF;
	p.pitch = static_cast<int>(pitchDeg * 65536.f / 360.f) & 0xFFFF;
	const float lift = static_cast<float>(liftFor(w.halfH, w.halfW, p.roll)) + std::fabs(std::sin(pitchDeg * 0.01745329f)) * static_cast<float>(w.halfL);
	p.z = static_cast<int>(ground + lift + air);
	return p;
}

Pose poseAt(const Wreck &w, unsigned t)   // t = Millisekunden seit dem Tod
{
	if (w.variant == LAUNCH) { return launchPose(w, t); }
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

// (23.11. 03:05) Rumpf von einer starken Explosion hochgeschleudert: dreht sich in der Luft, faellt herab und bleibt als Wrack liegen
bool launchHull(const DROID *psDel, GameWorld &world)
{
	const BODY_STATS *body = psDel->getBodyStats();
	if (body == nullptr || body->pIMD == nullptr) { return false; }
	const iIMDShape *bm = body->pIMD->displayModel();
	if (bm == nullptr) { return false; }
	// Feature-Art (Typ/Groesse) wie bei den normalen Wracks; angezeigt wird der echte Rumpf
	FEATURE_STATS *stat = nullptr;
	{
		const auto it = s_byBody.find(body->id.toStdString());
		if (it != s_byBody.end()) { stat = it->second; }
		int cls = body->size <= SIZE_LIGHT ? 0 : (body->size == SIZE_MEDIUM ? 1 : 2);
		if (stat == nullptr && s_stats[cls].empty()) { for (int c = 0; c < 3; c++) { if (!s_stats[c].empty()) { cls = c; break; } } }
		if (stat == nullptr && !s_stats[cls].empty()) { stat = s_stats[cls][gameRand(static_cast<uint32_t>(s_stats[cls].size()))]; }
	}
	if (stat == nullptr) { return false; }
	const int x = psDel->pos.x, y = psDel->pos.y;
	const int dx = x - s_kill.fromX, dy = y - s_kill.fromY;
	uint16_t dir = (std::abs(dx) + std::abs(dy) < 8) ? static_cast<uint16_t>(gameRand(65536)) : static_cast<uint16_t>(iAtan2(dx, dy));
	dir = static_cast<uint16_t>(dir + static_cast<int>(gameRand(DEG(50))) - DEG(25));
	const int strength = static_cast<int>(std::min<unsigned>(s_kill.damage, 2000));
	const int dist = 90 + static_cast<int>(gameRand(170)) + strength / 10;
	// Landestelle: auf der Karte, kein Wasser/Steilhang, frei, nicht zu nah an einem anderen Wrack - notfalls naeher, notfalls am Todespunkt
	auto okAt = [&](int px, int py) {
		if (!worldOnMap(world.map, px, py)) { return false; }
		const int tx = map_coord(px), ty = map_coord(py);
		if (tx <= 0 || ty <= 0 || tx >= world.map.width - 1 || ty >= world.map.height - 1) { return false; }
		MAPTILE *t = mapTile(world.map, tx, ty);
		if (t == nullptr || t->psObject != nullptr) { return false; }
		const unsigned char tt = terrainType(t);
		if (tt == TER_WATER || tt == TER_CLIFFFACE || fpathBlockingTile(world.map, tx, ty, PROPULSION_TYPE_WHEELED)) { return false; }
		for (const Wreck &o : s_wrecks)
		{
			const int ex = o.xe - px, ey = o.ye - py;
			if (ex * ex + ey * ey < MIN_DIST * MIN_DIST) { return false; }
		}
		return true;
	};
	int xe = x, ye = y;
	bool found = false;
	for (int r = dist; r >= 0; r -= 24)
	{
		const int px = x + iSinR(dir, r), py = y + iCosR(dir, r);
		if (okAt(px, py)) { xe = px; ye = py; found = true; break; }
	}
	if (!found) { return false; }

	while (s_wrecks.size() >= MAX_WRECKS)
	{
		FEATURE *old = static_cast<FEATURE *>(getBaseObjFromId(world.objects.features[0], s_wrecks.front().id));
		releaseBlock(world, s_wrecks.front());
		if (old != nullptr && !old->died) { removeFeature(old, world); }
		s_wrecks.pop_front();
	}
	FEATURE *f = buildFeature(world, stat, xe, ye, false);
	if (f == nullptr) { return false; }
	f->sDisplay.imd = body->pIMD;   // der echte Rumpf liegt da (Turm und Laufwerk sind weggeflogen)
	for (int p = 0; p < MAX_PLAYERS; p++) { f->visible[p] = psDel->visible[p]; }

	Wreck w;
	w.id = f->id;
	w.born = gameTime;
	w.x0 = x; w.y0 = y; w.xe = xe; w.ye = ye;
	w.dirMove = dir;
	w.variant = LAUNCH;
	w.hull = true;
	w.halfH = std::max(2, (bm->max.y - bm->min.y) / 2);
	w.halfW = std::max(2, (bm->max.x - bm->min.x) / 2);
	w.halfL = std::max(2, (bm->max.z - bm->min.z) / 2);
	w.height = 170 + static_cast<int>(gameRand(150)) + std::min(220, strength / 5);
	w.dur = static_cast<unsigned>(1700 + static_cast<int>(gameRand(700)) + w.height * 2);
	const int sign = gameRand(2) == 0 ? 1 : -1;
	const unsigned r = gameRand(100);
	const int finalRoll = r < 50 ? 180 : (r < 75 ? 90 : 270);                    // meist auf dem Dach, sonst auf der Seite
	w.rollTotal = sign * (360 * (1 + static_cast<int>(gameRand(2))) + finalRoll);
	w.pitchTotal = gameRand(3) == 0 ? 0 : (gameRand(2) == 0 ? 360 : -360);       // ueberschlaegt sich oft auch nach vorn/hinten
	w.h0 = map_Height(world.map, x, y);
	w.hE = map_Height(world.map, xe, ye);
	const int fr = static_cast<int>((static_cast<int64_t>(w.rollTotal) * 65536 / 360) & 0xFFFF);
	featureSetWreckPose(f, psDel->rot.direction, fr, w.hE + liftFor(w.halfH, w.halfW, fr));
	w.info.droidType = static_cast<uint8_t>(psDel->droidType);
	w.info.numWeaps = static_cast<uint8_t>(std::min<unsigned>(psDel->numWeaps, MAX_WEAPONS));
	for (int c = 0; c < DROID_MAXCOMP; ++c) { w.info.parts[c] = psDel->asBits[c]; }
	for (unsigned i = 0; i < w.info.numWeaps; ++i) { w.info.weaps[i] = psDel->asWeaps[i].nStat; }
	w.info.slots = psDel->modSlots;
	w.info.stats = stat;
	w.hasInfo = true;
	s_wrecks.push_back(w);
	return true;
}

/// Anzeige beim Hochschleudern: Turm(e) und Laufwerk fliegen weg, grosse Explosion, Truemmer, Funken
void launchStartFx(const Wreck &w, int z, unsigned it)
{
	Vector3i pos(w.x0, z + 10, w.y0);
	addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_LARGE, false, nullptr, 0, it);
	for (int k = 0; k < 3; ++k)
	{
		Vector3i ep(w.x0 + rand() % 41 - 20, z + 6 + rand() % 20, w.y0 + rand() % 41 - 20);
		addEffect(&ep, EFFECT_EXPLOSION, EXPLOSION_TYPE_MEDIUM, false, nullptr, 0, it);
	}
	addEffect(&pos, EFFECT_DESTRUCTION, DESTRUCTION_TYPE_DROID, false, nullptr, 0, it);
	audio_PlayStaticTrack(w.x0, w.y0, ID_SOUND_EXPLOSION);
	auto fling = [&](const iIMDBaseShape *base, int h) {
		if (base == nullptr) { return; }
		Vector3i p(w.x0, z + h, w.y0);
		Vector3f vel(static_cast<float>(rand() % 700 - 350), 0.f, static_cast<float>(rand() % 700 - 350));
		addEffect(&p, EFFECT_GRAVITON, GRAVITON_TYPE_TURRET, true, base->displayModel(), 0, it, nullptr, &vel);
	};
	for (unsigned i = 0; i < w.info.numWeaps && i < MAX_WEAPONS; ++i)
	{
		const unsigned st = w.info.weaps[i];
		if (st == 0 || st >= asWeaponStats.size()) { continue; }
		fling(asWeaponStats[st].pMountGraphic, 20);
		fling(asWeaponStats[st].pIMD, 22);
	}
	const unsigned prop = w.info.parts[COMP_PROPULSION];
	if (prop > 0 && prop < asPropulsionStats.size())
	{
		fling(asPropulsionStats[prop].pIMD, 6);
		fling(asPropulsionStats[prop].pIMD, 6);
	}
}

bool charredWreck(const FEATURE *f)
{
	for (const Wreck &w : s_wrecks) { if (w.id == f->id) { return w.hull; } }
	return false;
}

bool isWreckStats(const FEATURE_STATS *stats)
{
	if (stats == nullptr) { return false; }
	loadStats();
	return s_all.find(stats) != s_all.end();
}

void noteKill(const DROID *psDroid, unsigned damage, const PROJECTILE *psProjectile, int weaponSubClass, bool isDamagePerSecond)
{
	s_kill = KillNote();
	if (psDroid == nullptr || isDamagePerSecond) { return; }
	const WEAPON_STATS *ws = psProjectile != nullptr ? psProjectile->psWStats : nullptr;
	const int radius = (ws != nullptr && psProjectile->player < MAX_PLAYERS) ? static_cast<int>(ws->upgrade[psProjectile->player].radius) : 0;
	bool explosive;
	if (psProjectile == nullptr)
	{
		explosive = damage >= 250;   // Explosion ohne Geschoss (Munitionsdepot, Oellager, Fassstapel)
	}
	else
	{
		switch (weaponSubClass)
		{
		case WSC_HOWITZERS: case WSC_MORTARS: case WSC_BOMB: case WSC_MISSILE: case WSC_SLOWMISSILE: case WSC_SLOWROCKET: case WSC_LAS_SAT:
			explosive = true; break;
		case WSC_ROCKET: case WSC_CANNON: case WSC_GAUSS:
			explosive = radius >= 48; break;
		default:
			explosive = radius >= 96; break;
		}
	}
	const unsigned need = std::max<unsigned>(110, psDroid->originalBody / 4);
	if (!explosive || damage < need) { return; }
	s_kill.id = psDroid->id;
	s_kill.time = gameTime;
	s_kill.damage = damage;
	s_kill.strong = true;
	s_kill.fromX = psProjectile != nullptr ? psProjectile->pos.x : psDroid->pos.x;
	s_kill.fromY = psProjectile != nullptr ? psProjectile->pos.y : psDroid->pos.y;
}

bool charred(const FEATURE *f)
{
	return f != nullptr && charredWreck(f);
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

	// (23.11. 03:05) starke Explosion: Rumpf fliegt
	if (s_kill.strong && s_kill.id == psDel->id && s_kill.time == gameTime)
	{
		s_kill = KillNote();
		if (launchHull(psDel, world)) { return true; }
	}

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
		releaseBlock(world, s_wrecks.front());
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
	// Bauplan merken (Bergen: Abschlepp-Lkw bringt das Wrack zur Reparaturstation)
	static_assert(DROID_MAXCOMP <= 16, "WreckInfo zu klein");
	w.info.droidType = static_cast<uint8_t>(psDel->droidType);
	w.info.numWeaps = static_cast<uint8_t>(std::min<unsigned>(psDel->numWeaps, MAX_WEAPONS));
	for (int c = 0; c < DROID_MAXCOMP; ++c) { w.info.parts[c] = psDel->asBits[c]; }
	for (unsigned i = 0; i < w.info.numWeaps; ++i) { w.info.weaps[i] = psDel->asWeaps[i].nStat; }
	w.info.slots = psDel->modSlots;
	w.info.stats = stat;
	w.hasInfo = true;
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
		if (!w->blockDone && age >= w->dur)
		{
			// gelandet: Kachel sperren, falls sie nicht ohnehin gesperrt ist (Gebaeude, Fels, Wasser)
			w->blockDone = true;
			const int tx = map_coord(w->xe), ty = map_coord(w->ye);
			if (tx > 0 && ty > 0 && tx < world.map.width - 1 && ty < world.map.height - 1 && !fpathBlockingTile(world.map, tx, ty, PROPULSION_TYPE_WHEELED))
			{
				auxSetBlocking(world.map, tx, ty, FEATURE_BLOCKED);
				w->ownBlock = true;
			}
		}
		// ---- Todesanimation: Anzeige-Effekte in jedem Spielschritt (nur lokal, aendert den Spielzustand nicht)
		if (age <= w->dur + 100)
		{
			const Pose p = poseAt(*w, std::min(age, w->dur));
			const bool seen = visibleAt(p.x, p.y);
			if (!w->startFx && w->variant == LAUNCH)
			{
				w->startFx = true;
				if (visibleAt(w->x0, w->y0)) { launchStartFx(*w, w->h0, it); }
			}
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
			if (!w->crashPlayed && age >= (w->variant == LAUNCH ? w->dur * 84 / 100 : w->dur / 4))
			{
				w->crashPlayed = true;
				if (seen)
				{
					const int vi = std::min(w->variant, 2);   // LAUNCH: der schwere Aufprall
					const char *snd = vi == TIP ? "wreckcrash_a.ogg" : (vi == FLIP ? "wreckcrash_b.ogg" : "wreckcrash_c.ogg");
					playNamed(snd, s_trackCrash[vi], p.x, p.y);
					if (w->variant == LAUNCH)
					{
						Vector3i dp(p.x, w->hE + 4, p.y);
						addEffect(&dp, EFFECT_EXPLOSION, EXPLOSION_TYPE_KICKUP, false, nullptr, 0, it);
						addEffect(&dp, EFFECT_EXPLOSION, EXPLOSION_TYPE_SMALL, false, nullptr, 0, it);
					}
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
			releaseBlock(world, *w);
			w = s_wrecks.erase(w);
			continue;
		}
		if (age >= LIFE)
		{
			releaseBlock(world, *w);
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

bool wreckInfo(const FEATURE *f, WreckInfo &out)
{
	if (f == nullptr || f->died) { return false; }
	for (const Wreck &w : s_wrecks)
	{
		if (w.id != f->id) { continue; }
		if (!w.hasInfo || gameTime - w.born < w.dur) { return false; }   // noch in der Todesanimation
		out = w.info;
		return true;
	}
	return false;
}

std::vector<SavedWreck> saveState()
{
	std::vector<SavedWreck> out;
	for (const Wreck &w : s_wrecks)
	{
		SavedWreck s;
		s.featureId = w.id;
		s.age = gameTime - w.born;
		s.xe = w.xe; s.ye = w.ye;
		s.halfH = w.halfH; s.halfW = w.halfW;
		s.rollTotal = w.rollTotal;
		s.hasInfo = w.hasInfo;
		s.info = w.info;
		s.hull = w.hull;
		out.push_back(s);
	}
	return out;
}

void loadState(const std::vector<SavedWreck> &wrecks, GameWorld &world)
{
	loadStats();
	for (const SavedWreck &s : wrecks)
	{
		FEATURE *f = static_cast<FEATURE *>(getBaseObjFromId(world.objects.features[0], s.featureId));
		if (f == nullptr || f->died || !isWreckStats(f->psStats)) { continue; }
		Wreck w;
		w.id = f->id;
		w.born = gameTime >= s.age ? gameTime - s.age : 0;
		w.x0 = s.xe; w.y0 = s.ye; w.xe = s.xe; w.ye = s.ye;
		w.dirMove = f->rot.direction;
		w.variant = TIP;
		w.dur = 0;                       // gelandet (keine Todesanimation mehr)
		w.rollTotal = s.rollTotal;
		w.halfH = s.halfH; w.halfW = s.halfW;
		w.startFx = w.crashPlayed = w.endFx = true;
		w.info = s.info;
		w.hasInfo = s.hasInfo;
		if (s.hull && s.hasInfo && s.info.parts[COMP_BODY] < asBodyStats.size() && asBodyStats[s.info.parts[COMP_BODY]].pIMD != nullptr)
		{
			w.hull = true;   // (23.11.) hochgeschleuderter Rumpf: wieder das Rumpf-Modell zeigen
			f->sDisplay.imd = asBodyStats[s.info.parts[COMP_BODY]].pIMD;
		}
		// Lage wie vor dem Speichern (der Spielstand kennt die Hoehe des liegenden Wracks nicht)
		const int finalRoll = static_cast<int>((static_cast<int64_t>(w.rollTotal) * 65536 / 360) & 0xFFFF);
		const int lift = std::abs(iCosR(static_cast<uint16_t>(finalRoll), w.halfH)) + std::abs(iSinR(static_cast<uint16_t>(finalRoll), w.halfW)) + 1;
		featureSetWreckPose(f, f->rot.direction, finalRoll, map_Height(world.map, w.xe, w.ye) + lift);
		s_wrecks.push_back(w);
	}
}

bool takeWreck(FEATURE *f, GameWorld &world, WreckInfo &out)
{
	if (f == nullptr || f->died) { return false; }
	for (auto w = s_wrecks.begin(); w != s_wrecks.end(); ++w)
	{
		if (w->id != f->id) { continue; }
		if (!w->hasInfo || gameTime - w->born < w->dur) { return false; }
		out = w->info;
		releaseBlock(world, *w);
		s_wrecks.erase(w);
		removeFeature(f, world);
		return true;
	}
	return false;
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
