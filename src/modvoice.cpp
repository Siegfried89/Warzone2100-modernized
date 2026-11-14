// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Schlachtrufe (siehe modvoice.h)

#include "modvoice.h"

#include "lib/framework/frame.h"
#include "lib/framework/vector.h"
#include "lib/gamelib/gtime.h"
#include "lib/sound/audio.h"

#include "display3d.h"
#include "droid.h"
#include "game_world.h"
#include "map.h"
#include "modammo.h"
#include "order.h"
#include "projectile.h"
#include "statsdef.h"
#include "stats.h"

#include "modvoice_table.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace modvoice
{
namespace
{
constexpr int HEAR_RANGE = 18 * TILE_UNITS;      // nur Einheiten so nah an der Kamera werden gehoert
constexpr unsigned TICK_MS = 250;
constexpr unsigned HIT_WINDOW_MS = 1400;         // so lange gilt ein Treffer als "gerade beschossen"
constexpr int NUM_CATS = static_cast<int>(sizeof(kVoiceCats) / sizeof(kVoiceCats[0]));

enum Cls { CLS_INF = 0, CLS_TNK = 1, CLS_TRN = 2 };

struct State
{
	unsigned lastTouch = 0;
	unsigned nextSpeak = 0;      // fruehestens dann darf diese Einheit wieder rufen
	unsigned nextAmmo = 0;
	unsigned hitTime = 0;        // 0 = kein Treffer vermerkt
	unsigned hitCountReset = 0;
	int hitCount = 0;
	bool hitArty = false;
	bool hitAir = false;         // der Treffer kam von einem Flieger
	bool helpPending = false;
	bool killPending = false;
	unsigned downAt = 0;         // Kamerad in der Naehe wurde zerstoert (0 = nein)
	unsigned lastTarget = 0;
	unsigned lastContact = 0;    // wann diese Einheit zuletzt "Feind gesichtet" gerufen hat
	unsigned nextFireCall = 0;   // naechster Feuerruf
};

std::unordered_map<unsigned, State> s_state;
std::unordered_map<std::string, int> s_catIdx;
std::vector<int> s_catBase;          // Anfang der Stimmen-Nummern je Kategorie in s_track
std::vector<int> s_track;            // -2 = noch nicht gesucht, -1 = nicht vorhanden
std::vector<int> s_lastLine;         // zuletzt gesprochene Zeile je Kategorie (nicht zweimal hintereinander)
unsigned s_nextTick = 0;
unsigned s_nextAny = 0;              // allgemeine Pause zwischen zwei Rufen (Gefecht soll lebendig, nicht uebervoll sein)
unsigned s_nextArty = 0;
unsigned s_nextAir = 0;
unsigned s_nextPrune = 0;
unsigned s_nextAck = 0;              // Echtzeit
unsigned s_rng = 0x9E3779B9u;        // eigener Zufall (nur Ton)

unsigned rnd()
{
	s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
	return s_rng;
}

void initTables()
{
	if (!s_catIdx.empty()) { return; }
	int base = 0;
	for (int i = 0; i < NUM_CATS; i++)
	{
		s_catIdx[kVoiceCats[i].name] = i;
		s_catBase.push_back(base);
		base += kVoiceCats[i].lines * kVoiceCats[i].voices;
	}
	s_track.assign(static_cast<size_t>(base), -2);
	s_lastLine.assign(NUM_CATS, -1);
}

Cls classOf(const DROID *d)
{
	if (d->isTransporter()) { return CLS_TRN; }
	if (d->isCyborg() || d->droidType == DROID_PERSON) { return CLS_INF; }
	return CLS_TNK;
}

const char *clsPrefix(Cls c) { return c == CLS_INF ? "inf" : (c == CLS_TNK ? "tnk" : "trn"); }

bool nearAndVisible(int x, int y, const Vector2i &cam)
{
	const int64_t dx = x - cam.x, dy = y - cam.y;
	if (dx * dx + dy * dy > static_cast<int64_t>(HEAR_RANGE) * HEAR_RANGE) { return false; }
	MAPTILE *t = mapTile(gameWorld.map, map_coord(x), map_coord(y));
	return t != nullptr && TEST_TILE_VISIBLE_TO_SELECTEDPLAYER(t);
}

bool isArtillery(const PROJECTILE *proj, WEAPON_SUBCLASS lastHit)
{
	if (proj != nullptr && proj->psWStats != nullptr)
	{
		return proj->psWStats->movementModel == MM_INDIRECT || proj->psWStats->weaponSubClass == WSC_MORTARS
		       || proj->psWStats->weaponSubClass == WSC_HOWITZERS;
	}
	return lastHit == WSC_MORTARS || lastHit == WSC_HOWITZERS;
}

/// spielt eine zufaellige Zeile der Kategorie mit der Stimme der Einheit; false, wenn es sie nicht gibt
bool say(const char *cat, const DROID *d, int forcedLine = -1)
{
	initTables();
	auto it = s_catIdx.find(cat);
	if (it == s_catIdx.end()) { return false; }
	const int ci = it->second;
	const VoiceCat &vc = kVoiceCats[ci];
	int line = forcedLine;
	if (line < 0 || line >= vc.lines)
	{
		line = static_cast<int>(rnd() % static_cast<unsigned>(vc.lines));
		if (line == s_lastLine[ci] && vc.lines > 1) { line = (line + 1) % vc.lines; }
	}
	s_lastLine[ci] = line;
	const int voice = static_cast<int>(d->id % static_cast<unsigned>(vc.voices));
	int &track = s_track[static_cast<size_t>(s_catBase[ci] + line * vc.voices + voice)];
	if (track == -2)
	{
		char fn[96];
		std::snprintf(fn, sizeof(fn), "%s_%d_v%d.ogg", cat, line, voice);
		track = audio_GetTrackID(fn);
	}
	if (track < 0) { return false; }
	return audio_PlayStaticTrack(d->pos.x, d->pos.y, track);
}

struct Cand
{
	int prio;
	const DROID *d;
	std::string cat;
	unsigned unitPause;     // Pause dieser Einheit danach
	int kind;               // 0 = normal, 1 = Artillerie-Ruf, 2 = Flieger-Ruf (eigene Pausen)
};

std::string catName(Cls c, const char *suffix)
{
	return std::string(clsPrefix(c)) + "_" + suffix;
}
} // namespace

void reset()
{
	s_state.clear();
	s_nextTick = s_nextAny = s_nextArty = s_nextAir = s_nextPrune = 0;
	s_nextAck = 0;
}

void onDroidDamaged(const DROID *psVictim, const PROJECTILE *psProj, bool killed)
{
	if (psVictim == nullptr || psProj == nullptr) { return; }    // Brand/Dauerschaden: kein Ruf
	State &st = s_state[psVictim->id];
	st.lastTouch = gameTime;
	if (gameTime - st.hitCountReset > 3500) { st.hitCount = 0; }
	st.hitCountReset = gameTime;
	st.hitCount++;
	st.hitTime = gameTime;
	st.hitArty = isArtillery(psProj, psVictim->lastHitWeapon);
	st.hitAir = false;
	const BASE_OBJECT *src = psProj->psSource;
	if (src != nullptr && src->type == OBJ_DROID && !src->died)
	{
		st.hitAir = static_cast<const DROID *>(src)->isVtol();
	}
	if (!killed) { return; }
	// der Schuetze ruft "Got him!"
	if (src != nullptr && src->type == OBJ_DROID && !src->died)
	{
		State &as = s_state[src->id];
		as.lastTouch = gameTime;
		as.killPending = true;
	}
	// Kameraden der Gefallenen im Umkreis von 7 Kacheln rufen "Man down!" (die zwei naechsten)
	const int64_t r2 = static_cast<int64_t>(7 * TILE_UNITS) * (7 * TILE_UNITS);
	const DROID *near1 = nullptr, *near2 = nullptr;
	int64_t d1 = r2 + 1, d2 = r2 + 1;
	for (const DROID *d : gameWorld.objects.droids[psVictim->player])
	{
		if (d == nullptr || d == psVictim || d->died || d->isVtol()) { continue; }
		const int64_t dx = d->pos.x - psVictim->pos.x, dy = d->pos.y - psVictim->pos.y;
		const int64_t dd = dx * dx + dy * dy;
		if (dd > r2) { continue; }
		if (dd < d1) { near2 = near1; d2 = d1; near1 = d; d1 = dd; }
		else if (dd < d2) { near2 = d; d2 = dd; }
	}
	for (const DROID *d : {near1, near2})
	{
		if (d != nullptr && !d->isTransporter())
		{
			State &ds = s_state[d->id];
			ds.lastTouch = gameTime;
			ds.downAt = gameTime;
		}
	}
}

void onHelpCall(const DROID *psDroid)
{
	if (psDroid == nullptr) { return; }
	State &st = s_state[psDroid->id];
	st.lastTouch = gameTime;
	st.helpPending = true;
}

void onPlayerOrder(const DROID *psDroid, int order)
{
	if (psDroid == nullptr || psDroid->player != selectedPlayer || psDroid->isVtol()) { return; }
	if (realTime < s_nextAck) { return; }     // eine Einheit der Auswahl antwortet, nicht alle zugleich
	if (psDroid->isTransporter())
	{
		const char *tk = (order == DORDER_DISEMBARK) ? "trn_drop" : ((order == DORDER_MOVE || order == DORDER_RTB || order == DORDER_RTR || order == DORDER_EMBARK) ? "trn_ack" : nullptr);
		if (tk != nullptr && say(tk, psDroid)) { s_nextAck = realTime + 1100; }
		return;
	}
	const char *kind = nullptr;
	switch (order)
	{
	case DORDER_ATTACK:
	case DORDER_SCOUT:
	case DORDER_PATROL:
	case DORDER_FIRESUPPORT:
		kind = (classOf(psDroid) == CLS_INF && (rnd() & 1u)) ? "attack" : "ack";
		break;
	case DORDER_MOVE:
	case DORDER_RTR:
	case DORDER_RTB:
	case DORDER_BUILD:
	case DORDER_LINEBUILD:
	case DORDER_REPAIR:
	case DORDER_DEMOLISH:
	case DORDER_FORTIFY:
	case DORDER_DROIDREPAIR:
	case DORDER_OBSERVE:
	case DORDER_EMBARK:
	case DORDER_DISEMBARK:
		kind = "ack";
		break;
	default:
		return;
	}
	const std::string cat = catName(classOf(psDroid), kind);
	if (say(cat.c_str(), psDroid))
	{
		s_nextAck = realTime + 1100;
	}
}

void tick(GameWorld &world)
{
	const unsigned now = gameTime;
	if (now < s_nextTick) { return; }
	s_nextTick = now + TICK_MS;

	if (now >= s_nextPrune)
	{
		s_nextPrune = now + 20000;
		for (auto it = s_state.begin(); it != s_state.end();)
		{
			if (now - it->second.lastTouch > 30000) { it = s_state.erase(it); } else { ++it; }
		}
	}
	if (now < s_nextAny) { return; }

	const Vector2i cam = getPlayerPos();
	std::vector<Cand> cands;
	auto addCand = [&](int prio, const DROID *d, const std::string &cat, unsigned pause, int kind)
	{
		if (cands.size() >= 64) { return; }
		cands.push_back({prio, d, cat, pause, kind});
	};

	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		for (DROID *d : world.objects.droids[p])
		{
			if (d == nullptr || d->died || d->isVtol()) { continue; }
			if (!nearAndVisible(d->pos.x, d->pos.y, cam)) { continue; }
			auto sit = s_state.find(d->id);
			// Einheiten ohne Eintrag brauchen nur dann einen, wenn sie kaempfen oder Befehle ausfuehren
			const bool fighting = d->action == DACTION_ATTACK || d->action == DACTION_MOVETOATTACK;
			const bool retreating = d->order.type == DORDER_RTR || d->order.type == DORDER_RTB;
			if (sit == s_state.end())
			{
				if (!fighting && !retreating && !(modammo::usesAmmo(d) && modammo::segments(d) == 0)) { continue; }
				sit = s_state.emplace(d->id, State()).first;
			}
			State &st = sit->second;
			st.lastTouch = now;
			if (now < st.nextSpeak) { continue; }
			const Cls cls = classOf(d);
			const bool hitNow = st.hitTime != 0 && now - st.hitTime <= HIT_WINDOW_MS;

			if (st.helpPending)
			{
				st.helpPending = false;
				addCand(7, d, "help_call", 9000, 0);
				continue;
			}
			if (hitNow)
			{
				if (st.hitArty)
				{
					if (now >= s_nextArty)
					{
						addCand(8, d, std::string("arty_") + clsPrefix(cls), 8000, 1);
					}
					st.hitTime = 0;
					continue;
				}
				if (st.hitAir && cls != CLS_TRN)
				{
					if (now >= s_nextAir)
					{
						addCand(7, d, catName(cls, "air"), 8000, 2);
					}
					st.hitTime = 0;
					continue;
				}
				const bool low = d->originalBody > 0 && d->body * 100 < d->originalBody * 30;
				if (low && cls != CLS_TRN)
				{
					addCand(6, d, catName(cls, "retreat"), 9000, 0);
				}
				else if (cls == CLS_INF)
				{
					const unsigned r = rnd() % 9;
					const char *k = (r < 3) ? "cover" : (r < 5) ? "hit" : (r < 6) ? "help" : (r < 7) ? "status" : "pinned";
					if (std::strcmp(k, "pinned") == 0 && st.hitCount < 3) { k = "cover"; }
					addCand(5, d, catName(cls, k), 4500 + rnd() % 3500, 0);
				}
				else if (cls == CLS_TNK)
				{
					addCand(5, d, catName(cls, "hit"), 7000 + rnd() % 5000, 0);
				}
				else
				{
					addCand(5, d, "trn_fire", 8000, 0);
				}
				st.hitTime = 0;
				continue;
			}
			if (st.downAt != 0)
			{
				const bool fresh = now - st.downAt < 3000;
				st.downAt = 0;
				if (fresh && cls != CLS_TRN)
				{
					addCand(6, d, catName(cls, "down"), 7000 + rnd() % 4000, 0);
					continue;
				}
			}
			if (retreating && cls != CLS_TRN)
			{
				addCand(4, d, catName(cls, "retreat"), 14000, 0);
				continue;
			}
			if (st.killPending)
			{
				st.killPending = false;
				if (cls != CLS_TRN)
				{
					addCand(3, d, catName(cls, "kill"), 5000 + rnd() % 4000, 0);
					continue;
				}
			}
			if (cls != CLS_TRN && now >= st.nextAmmo && modammo::usesAmmo(d) && modammo::segments(d) == 0)
			{
				st.nextAmmo = now + 30000;
				addCand(4, d, catName(cls, "ammo"), 6000, 0);
				continue;
			}
			if (fighting && cls != CLS_TRN)
			{
				BASE_OBJECT *tgt = d->psActionTarget[0];
				const unsigned tid = tgt != nullptr ? tgt->id : 0;
				if (tgt != nullptr && tid != st.lastTarget)
				{
					st.lastTarget = tid;
					// was ist das Ziel?
					enum { T_INF, T_ARMOR, T_AIR, T_STRUCT, T_OTHER } tk = T_OTHER;
					if (tgt->type == OBJ_DROID)
					{
						const DROID *td = static_cast<const DROID *>(tgt);
						tk = td->isVtol() ? T_AIR : ((td->isCyborg() || td->droidType == DROID_PERSON) ? T_INF : T_ARMOR);
					}
					else if (tgt->type == OBJ_STRUCTURE)
					{
						tk = T_STRUCT;
					}
					const bool fresh = st.lastContact == 0 || now - st.lastContact > 20000;
					if (!fresh && (rnd() & 1u) && now >= st.nextFireCall)
					{
						// schon im Gefecht: neues Ziel bekommt einen Feuerruf statt "Feind gesichtet"
						st.nextFireCall = now + 12000;
						addCand(2, d, catName(cls, "fire"), 6000 + rnd() % 4000, 0);
						continue;
					}
					st.lastContact = now;
					if (cls == CLS_INF)
					{
						const char *k = (tk == T_ARMOR) ? "inf_tanks" : (tk == T_AIR) ? "inf_air" : "inf_enemy";
						addCand((tk == T_ARMOR || tk == T_AIR) ? 5 : 3, d, k, 6000 + rnd() % 4000, 0);
					}
					else
					{
						const char *k = (tk == T_ARMOR) ? "tnk_armor" : (tk == T_INF) ? "tnk_inf" : (tk == T_AIR) ? "tnk_air" : (tk == T_STRUCT) ? "tnk_struct" : "tnk_contact";
						addCand((tk == T_ARMOR || tk == T_AIR) ? 4 : 3, d, k, 8000 + rnd() % 5000, 0);
					}
					continue;
				}
				if (d->action == DACTION_MOVETOATTACK && (rnd() % 5u) == 0)
				{
					const char *k = (cls == CLS_INF) ? (((rnd() % 3u) == 0) ? "inf_coverme" : "inf_attack") : "tnk_advance";
					addCand(2, d, k, 12000 + rnd() % 6000, 0);
					continue;
				}
				if (d->action == DACTION_ATTACK && now >= st.nextFireCall && (rnd() % 12u) == 0)
				{
					st.nextFireCall = now + 14000;
					addCand(2, d, catName(cls, "fire"), 8000, 0);
					continue;
				}
			}
			else if (!fighting)
			{
				st.lastTarget = 0;
				const bool onTheWay = (d->order.type == DORDER_SCOUT || d->order.type == DORDER_ATTACK) && d->action == DACTION_MOVE;
				// unterwegs zum Gefecht: Panzer melden sich, Infanterie treibt sich an
				if (cls == CLS_TNK && onTheWay && (rnd() % 24u) == 0)
				{
					addCand(1, d, "tnk_advance", 16000 + rnd() % 8000, 0);
				}
				else if (cls == CLS_INF && onTheWay && (rnd() % 30u) == 0)
				{
					addCand(1, d, "inf_attack", 16000 + rnd() % 8000, 0);
				}
			}
		}
	}
	if (cands.empty()) { return; }

	// beste Rufe (hoechste Prioritaet), unter gleichen zufaellig; im grossen Gefecht sprechen zwei Einheiten zugleich
	auto pickBest = [&](const DROID *exclude) -> int
	{
		int best = -1; unsigned ties = 0;
		for (size_t i = 0; i < cands.size(); i++)
		{
			if (cands[i].d == exclude) { continue; }
			if (best < 0 || cands[i].prio > cands[static_cast<size_t>(best)].prio) { best = static_cast<int>(i); ties = 1; }
			else if (cands[i].prio == cands[static_cast<size_t>(best)].prio && rnd() % (++ties) == 0) { best = static_cast<int>(i); }
		}
		return best;
	};
	auto speak = [&](const Cand &c)
	{
		State &st = s_state[c.d->id];
		if (say(c.cat.c_str(), c.d))
		{
			st.nextSpeak = now + c.unitPause;
			if (c.kind == 1) { s_nextArty = now + 9000 + rnd() % 6000; }
			if (c.kind == 2) { s_nextAir = now + 9000 + rnd() % 6000; }
			return true;
		}
		st.nextSpeak = now + 3000;    // Datei fehlt: nicht jeden Schritt neu versuchen
		return false;
	};
	const int b1 = pickBest(nullptr);
	if (b1 < 0) { return; }
	const Cand first = cands[static_cast<size_t>(b1)];
	const bool ok = speak(first);
	if (ok)
	{
		s_nextAny = now + 400 + rnd() % 700;
		if (cands.size() >= 5 && (rnd() & 1u))
		{
			const int b2 = pickBest(first.d);
			if (b2 >= 0) { speak(cands[static_cast<size_t>(b2)]); }
		}
	}
}
}
