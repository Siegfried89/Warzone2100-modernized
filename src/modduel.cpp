// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Metal-Fatigue-Duellsystem fuer Combots.
// Ablauf wie in Metal Fatigue: zwei Combots treten in Nahkampfreichweite, bleiben stehen, drehen sich zueinander
// und schlagen abwechselnd zu. Der Verteidiger blockt zufaellig, weicht gelegentlich aus oder wird an einem
// zufaelligen Koerperteil getroffen. Teile mit 0 Trefferpunkten werden abgeschlagen (Arm: Waffe weg, Beine: Kriechtempo).

#include "modduel.h"

#include "lib/framework/frame.h"
#include "lib/framework/vector.h"
#include "lib/framework/math_ext.h"
#include "lib/netplay/netplay.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "baseobject.h"
#include "modcombot.h"

#include "action.h"
#include "ai.h"
#include "droid.h"
#include "effects.h"
#include "game_world.h"
#include "geometry.h"
#include "move.h"
#include "order.h"
#include "random.h"
#include "stats.h"

#include <cstdint>
#include <unordered_map>

namespace modduel
{

bool isCombot(const DROID *d);
static bool canDuel(const DROID *d);

namespace
{

constexpr int ENGAGE_RANGE = 256;     // Welteinheiten (Kachel = 128): Duell beginnt
constexpr int BREAK_RANGE = 448;      // darueber wird das Duell abgebrochen
constexpr unsigned ROUND_TIME = 1700; // ms zwischen zwei Schlaegen (langsamer MF-Kampf)
constexpr unsigned HIT_DELAY = 800;   // ms nach Schlagbeginn bis zum Treffer
constexpr unsigned BLOCK_PCT = 32;
constexpr unsigned DODGE_PCT = 12;

enum Part { TORSO = 0, RARM, LARM, LEGS, NPARTS };

struct Duel
{
	uint32_t opponent = 0;
	bool myTurn = false;
	unsigned nextSwing = 0;   // gameTime, wann der naechste Schlag beginnt
	unsigned hitAt = 0;       // gameTime des Treffermoments, 0 = kein Schlag unterwegs
	int partHp[NPARTS] = { 0, 0, 0, 0 };
	bool severed[NPARTS] = { false, false, false, false };
	bool init = false;
	unsigned swing = 0;       // zaehlt die Schlaege (modulare Combots wechseln den Arm)
};

std::unordered_map<uint32_t, Duel> s_duels;

DROID *findDroid(uint32_t id)
{
	for (int p = 0; p < MAX_PLAYERS; ++p)
	{
		for (DROID *d : gameWorld.objects.droids[p])
		{
			if (d != nullptr && d->id == id) { return d; }
		}
	}
	return nullptr;
}

int dist2d(const DROID *a, const DROID *b)
{
	const int dx = a->pos.x - b->pos.x, dy = a->pos.y - b->pos.y;
	return static_cast<int>(std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy));
}

void initParts(Duel &du, const DROID *d)
{
	const int hp = static_cast<int>(d->originalBody);
	du.partHp[TORSO] = hp * 50 / 100;
	du.partHp[RARM] = hp * 15 / 100;
	du.partHp[LARM] = hp * 15 / 100;
	du.partHp[LEGS] = hp * 20 / 100;
	du.init = true;
}

void sever(DROID *d, Duel &du, Part p)
{
	du.severed[p] = true;
	const Vector3i pos(d->pos.x, d->pos.y, d->pos.z + 24);
	addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_SMALL, false, nullptr, 0, gameTime);
	if (p == RARM)
	{
		// Waffenarm weg: Waffe faellt aus
		for (unsigned i = 0; i < d->numWeaps; ++i) { d->asWeaps[i].ammo = 0; }
		if (d->numWeaps > 0) { d->asWeaps[0].nStat = 0; }
	}
	else if (p == LEGS)
	{
		d->baseSpeed = 1; // kriecht nur noch
	}
}

void applyDamage(DROID *victim, Duel &vd, const DROID *attacker, Duel &ad)
{
	int dmg = 40;
	const bool modAtt = modcombot::isModular(attacker);
	if (modAtt)
	{
		// zuschlagender Arm: die Nahkampfarme wechseln sich ab
		const modcombot::Loadout lo = modcombot::loadoutOf(attacker);
		int slots[2];
		int n = 0;
		if (lo.armR && lo.armR->st.melee && lo.slotR >= 0) { slots[n++] = lo.slotR; }
		if (lo.armL && lo.armL->st.melee && lo.slotL >= 0) { slots[n++] = lo.slotL; }
		if (n > 0)
		{
			const int slot = slots[ad.swing % static_cast<unsigned>(n)];
			const unsigned ws = attacker->asWeaps[slot].nStat;
			if (ws > 0 && ws < asWeaponStats.size()) { dmg = std::max(10, weaponDamage(asWeaponStats[ws], attacker->player)); }
		}
		++ad.swing;
	}
	else if (attacker->numWeaps > 0 && attacker->asWeaps[0].nStat > 0 && attacker->asWeaps[0].nStat < asWeaponStats.size())
	{
		dmg = std::max(10, weaponDamage(asWeaponStats[attacker->asWeaps[0].nStat], attacker->player));
	}
	const unsigned roll = gameRand(100);
	const unsigned r2 = gameRand(100);
	const unsigned blockPct = BLOCK_PCT + static_cast<unsigned>(modcombot::blockBonus(victim));
	if (roll < blockPct)
	{
		dmg = dmg * 15 / 100; // geblockt: nur Restschaden
	}
	else if (roll < blockPct + DODGE_PCT)
	{
		return;              // ausgewichen
	}
	if (modcombot::isModular(victim))
	{
		// Teil-Treffer: Torso, Arme, Beine; abgeschlagen wird in modcombot
		const int hit = modcombot::hitRandomPart(victim, dmg);
		(void)hit;
		if (static_cast<int>(victim->body) <= dmg)
		{
			victim->body = 0;
			destroyDroid(victim, gameTime, gameWorld);
			return;
		}
		victim->body -= dmg;
		victim->timeLastHit = gameTime;
		return;
	}
	Part part = r2 < 35 ? TORSO : r2 < 60 ? RARM : r2 < 80 ? LARM : LEGS;
	if (!vd.init) { initParts(vd, victim); }
	vd.partHp[part] -= dmg;
	if (vd.partHp[part] <= 0 && !vd.severed[part] && part != TORSO)
	{
		sever(victim, vd, part);
	}
	if (static_cast<int>(victim->body) <= dmg)
	{
		victim->body = 0;
		destroyDroid(victim, gameTime, gameWorld);
		return;
	}
	victim->body -= dmg;
	victim->timeLastHit = gameTime;
}

void beginSwing(DROID *d)
{
	d->animationEvent = ANIM_EVENT_FIRING;
	d->timeAnimationStarted = gameTime;
}

void endDuel(DROID *d)
{
	s_duels.erase(d->id);
	if (d->animationEvent == ANIM_EVENT_FIRING) { resetObjectAnimationState(d); }
}

} // namespace

bool isCombot(const DROID *d)
{
	if (d == nullptr) { return false; }
	if (modcombot::isModular(d)) { return true; }
	const BODY_STATS *b = d->getBodyStats();
	return b != nullptr && b->id.toUtf8().rfind("MFW_", 0) == 0;
}

static bool canDuel(const DROID *d)
{
	if (!isCombot(d)) { return false; }
	if (!modcombot::isModular(d)) { return true; }
	return modcombot::hasMeleeArm(d) && modcombot::weaponMode(d) != modcombot::MODE_RANGED;
}

void update(DROID *d)
{
	if (d == nullptr || d->died || !isCombot(d)) { return; }
	if (modcombot::isModular(d))
	{
		modcombot::updateAbilities(d);
		modcombot::enforceWeaponMode(d);
		if (!canDuel(d))
		{
			// im Fernkampf-Modus (oder ohne Nahkampfarm) kein Duell: ein laufendes beenden
			if (s_duels.count(d->id)) { endDuel(d); }
			return;
		}
	}

	auto it = s_duels.find(d->id);
	if (it == s_duels.end())
	{
		// Gegner suchen
		if (d->order.type == DORDER_MOVE) { return; } // wer gerade laeuft, flieht / marschiert
		DROID *best = nullptr;
		int bestDist = ENGAGE_RANGE + 1;
		for (int p = 0; p < MAX_PLAYERS; ++p)
		{
			if (p == d->player || aiCheckAlliances(p, d->player)) { continue; }
			for (DROID *o : gameWorld.objects.droids[p])
			{
				if (o == nullptr || o->died || !canDuel(o)) { continue; }
				const int dd = dist2d(d, o);
				if (dd < bestDist) { bestDist = dd; best = o; }
			}
		}
		if (best == nullptr) { return; }
		Duel &a = s_duels[d->id];
		a.opponent = best->id;
		a.myTurn = d->id < best->id; // deterministisch: kleinere ID beginnt
		a.nextSwing = gameTime + (a.myTurn ? 300 : ROUND_TIME / 2);
		initParts(a, d);
		Duel &b = s_duels[best->id];
		if (b.opponent == 0)
		{
			b.opponent = d->id;
			b.myTurn = !a.myTurn;
			b.nextSwing = gameTime + (b.myTurn ? 300 : ROUND_TIME / 2);
			initParts(b, best);
		}
		orderDroid(d, DORDER_STOP, ModeImmediate);
		orderDroid(best, DORDER_STOP, ModeImmediate);
		return;
	}

	Duel &du = it->second;
	DROID *opp = findDroid(du.opponent);
	if (opp == nullptr || opp->died || !canDuel(opp) || dist2d(d, opp) > BREAK_RANGE || d->order.type == DORDER_MOVE)
	{
		endDuel(d);
		return;
	}

	// Gegner anblicken, normale WZ-Schuesse unterdruecken (das Duell ersetzt das Standard-Gefecht)
	d->rot.direction = calcDirection(d->pos.x, d->pos.y, opp->pos.x, opp->pos.y);
	for (unsigned i = 0; i < MAX_WEAPONS && i < std::max(1u, d->numWeaps); ++i) { d->asWeaps[i].lastFired = gameTime; }

	if (du.hitAt != 0 && gameTime >= du.hitAt)
	{
		du.hitAt = 0;
		auto vit = s_duels.find(opp->id);
		if (vit != s_duels.end()) { applyDamage(opp, vit->second, d, du); }
		if (opp->died) { endDuel(d); return; }
	}
	if (du.hitAt == 0 && du.myTurn && gameTime >= du.nextSwing)
	{
		beginSwing(d);
		du.hitAt = gameTime + HIT_DELAY;
		du.nextSwing = gameTime + ROUND_TIME * 2; // zwei Runden: eigene + Gegnerschlag
		auto vit = s_duels.find(opp->id);
		if (vit != s_duels.end())
		{
			vit->second.myTurn = true; // Gegner ist als Naechster dran
			vit->second.nextSwing = gameTime + ROUND_TIME;
		}
		du.myTurn = false;
		// Waffen abwechseln (nur alte Einzel-Combots; modulare wechseln den Arm in applyDamage)
		if (d->numWeaps >= 2 && !modcombot::isModular(d)) { std::swap(d->asWeaps[0], d->asWeaps[1]); }
	}
	else if (d->animationEvent == ANIM_EVENT_FIRING && du.hitAt == 0 && gameTime > du.nextSwing - ROUND_TIME)
	{
		resetObjectAnimationState(d);
	}
}

void updateLeggedAnimation(DROID *d, int moveSpeed)
{
	if (d == nullptr || d->animationEvent == ANIM_EVENT_DYING || !isCombot(d)) { return; }
	if (s_duels.count(d->id)) { return; } // im Duell steuert das Duell die Animation
	const bool attacking = moveSpeed == 0 && (d->action == DACTION_ATTACK || d->action == DACTION_ROTATETOATTACK);
	const UBYTE want = attacking ? ANIM_EVENT_FIRING : (moveSpeed != 0 ? ANIM_EVENT_ACTIVE : ANIM_EVENT_NONE);
	if (d->animationEvent == want) { return; }
	if (want == ANIM_EVENT_NONE)
	{
		resetObjectAnimationState(d);
	}
	else
	{
		d->timeAnimationStarted = gameTime;
		d->animationEvent = want;
	}
}

} // namespace modduel
