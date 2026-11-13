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
	bool helpPending = false;
	bool killPending = false;
	unsigned lastTarget = 0;
};

std::unordered_map<unsigned, State> s_state;
std::unordered_map<std::string, int> s_catIdx;
std::vector<int> s_catBase;          // Anfang der Stimmen-Nummern je Kategorie in s_track
std::vector<int> s_track;            // -2 = noch nicht gesucht, -1 = nicht vorhanden
std::vector<int> s_lastLine;         // zuletzt gesprochene Zeile je Kategorie (nicht zweimal hintereinander)
unsigned s_nextTick = 0;
unsigned s_nextAny = 0;              // allgemeine Pause zwischen zwei Rufen (Gefecht soll lebendig, nicht uebervoll sein)
unsigned s_nextArty = 0;
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
	bool arty;
};

std::string catName(Cls c, const char *suffix)
{
	return std::string(clsPrefix(c)) + "_" + suffix;
}
} // namespace

void reset()
{
	s_state.clear();
	s_nextTick = s_nextAny = s_nextArty = s_nextPrune = 0;
	s_nextAck = 0;
}

void onDroidDamaged(const DROID *psVictim, const PROJECTILE *psProj)
{
	if (psVictim == nullptr || psProj == nullptr) { return; }    // Brand/Dauerschaden: kein Ruf
	State &st = s_state[psVictim->id];
	st.lastTouch = gameTime;
	if (gameTime - st.hitCountReset > 3500) { st.hitCount = 0; }
	st.hitCountReset = gameTime;
	st.hitCount++;
	st.hitTime = gameTime;
	st.hitArty = isArtillery(psProj, psVictim->lastHitWeapon);
	if (psVictim->died && psProj->psSource != nullptr && psProj->psSource->type == OBJ_DROID && !psProj->psSource->died)
	{
		State &as = s_state[psProj->psSource->id];
		as.lastTouch = gameTime;
		as.killPending = true;
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
	if (psDroid == nullptr || psDroid->player != selectedPlayer || psDroid->isTransporter() || psDroid->isVtol()) { return; }
	if (realTime < s_nextAck) { return; }     // eine Einheit der Auswahl antwortet, nicht alle zugleich
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
	auto addCand = [&](int prio, const DROID *d, const std::string &cat, unsigned pause, bool arty)
	{
		if (cands.size() >= 64) { return; }
		cands.push_back({prio, d, cat, pause, arty});
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
				addCand(7, d, "help_call", 9000, false);
				continue;
			}
			if (hitNow)
			{
				if (st.hitArty)
				{
					if (now >= s_nextArty)
					{
						addCand(8, d, std::string("arty_") + clsPrefix(cls), 8000, true);
					}
					st.hitTime = 0;
					continue;
				}
				const bool low = d->originalBody > 0 && d->body * 100 < d->originalBody * 30;
				if (low && cls != CLS_TRN)
				{
					addCand(6, d, catName(cls, "retreat"), 9000, false);
				}
				else if (cls == CLS_INF)
				{
					const unsigned r = rnd() % 8;
					const char *k = (r < 3) ? "cover" : (r < 5) ? "hit" : (r < 7) ? "help" : "pinned";
					if (std::strcmp(k, "pinned") == 0 && st.hitCount < 3) { k = "cover"; }
					addCand(5, d, catName(cls, k), 4500 + rnd() % 3500, false);
				}
				else if (cls == CLS_TNK)
				{
					addCand(5, d, catName(cls, "hit"), 7000 + rnd() % 5000, false);
				}
				else
				{
					addCand(5, d, "trn_fire", 8000, false);
				}
				st.hitTime = 0;
				continue;
			}
			if (retreating && cls != CLS_TRN)
			{
				addCand(4, d, catName(cls, "retreat"), 14000, false);
				continue;
			}
			if (st.killPending)
			{
				st.killPending = false;
				if (cls != CLS_TRN)
				{
					addCand(3, d, catName(cls, "kill"), 5000 + rnd() % 4000, false);
					continue;
				}
			}
			if (cls != CLS_TRN && now >= st.nextAmmo && modammo::usesAmmo(d) && modammo::segments(d) == 0)
			{
				st.nextAmmo = now + 30000;
				addCand(4, d, catName(cls, "ammo"), 6000, false);
				continue;
			}
			if (fighting && cls != CLS_TRN)
			{
				BASE_OBJECT *tgt = d->psActionTarget[0];
				const unsigned tid = tgt != nullptr ? tgt->id : 0;
				if (tgt != nullptr && tid != st.lastTarget)
				{
					st.lastTarget = tid;
					if (cls == CLS_INF)
					{
						const bool armor = tgt->type == OBJ_DROID && !static_cast<const DROID *>(tgt)->isCyborg()
						                   && static_cast<const DROID *>(tgt)->droidType != DROID_PERSON;
						addCand(armor ? 5 : 3, d, armor ? "inf_tanks" : "inf_enemy", 6000 + rnd() % 4000, false);
					}
					else
					{
						addCand(3, d, "tnk_contact", 8000 + rnd() % 5000, false);
					}
					continue;
				}
				if (d->action == DACTION_MOVETOATTACK && (rnd() % 5u) == 0)
				{
					addCand(2, d, cls == CLS_INF ? "inf_attack" : "tnk_advance", 12000 + rnd() % 6000, false);
					continue;
				}
			}
			else if (!fighting)
			{
				st.lastTarget = 0;
				// unterwegs zum Gefecht: Panzer melden sich
				if (cls == CLS_TNK && (d->order.type == DORDER_SCOUT || d->order.type == DORDER_ATTACK) && d->action == DACTION_MOVE && (rnd() % 24u) == 0)
				{
					addCand(1, d, "tnk_advance", 16000 + rnd() % 8000, false);
				}
			}
		}
	}
	if (cands.empty()) { return; }

	// beste Rufe (hoechste Prioritaet), unter gleichen zufaellig
	int best = -1; unsigned ties = 0;
	for (size_t i = 0; i < cands.size(); i++)
	{
		if (best < 0 || cands[i].prio > cands[static_cast<size_t>(best)].prio) { best = static_cast<int>(i); ties = 1; }
		else if (cands[i].prio == cands[static_cast<size_t>(best)].prio && rnd() % (++ties) == 0) { best = static_cast<int>(i); }
	}
	const Cand &c = cands[static_cast<size_t>(best)];
	const std::string &cat = c.cat;
	State &st = s_state[c.d->id];
	if (say(cat.c_str(), c.d))
	{
		st.nextSpeak = now + c.unitPause;
		s_nextAny = now + 400 + rnd() % 700;
		if (c.arty) { s_nextArty = now + 9000 + rnd() % 6000; }
	}
	else
	{
		st.nextSpeak = now + 3000;    // Datei fehlt: nicht jeden Schritt neu versuchen
	}
}
}
