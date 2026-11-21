// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Metal-Fatigue-Nahkampf ("Brawl") fuer Combots - neu (Wunsch 21.11. 15:25 "Denkst du die Combots funktionieren so? Ich denke nicht").
//
// Vorher (Aufzeichnung 21.11. 15:24): Duell schon ab 2 Feldern Abstand, keiner ging naeher -> Hiebe 1,5-2 Felder vor dem Gegner in die Luft;
// jeder feindliche Combot in 2 Feldern wurde sofort "gegriffen" und beide angehalten (Spieler-Befehle uebergangen); ein Combot konnte einen
// Gegner "duellieren", der schon mit einem anderen kaempfte (Schlaege in den Ruecken, Takt ueber beide Duelle durcheinander); Block und
// Ausweichen nur unsichtbar im Schaden; Schaden und Trefferpunkte geschaetzt.
//
// Jetzt wie MF (mfatigue.exe CRobot::RequestedBrawlBrain, objinfo.tbd):
//  - Ziel ist das Angriffsziel von Warzone (Befehl des Spielers bzw. eigene Zielwahl) - nichts wird mehr ungefragt gegriffen. Warzone fuehrt
//    den Combot bis auf die Waffenreichweite heran; die ist jetzt die Schlagweite des Arms (mfreach.py: weiteste Streckung der Waffe in den
//    MF-Angriffen) - die Klinge trifft den Gegner.
//  - Brawl-Anfrage: wer zuschlaegt, fordert den Gegner. Ist der frei (kein anderer Brawl, kein laufender Fahr- oder Angriffsbefehl auf ein
//    anderes Ziel), nimmt er an: er nimmt den Angreifer als Ziel, dreht sich zu ihm und schlaegt zurueck. Ist er beschaeftigt, schlaegt der
//    Angreifer einseitig zu (MF: die Anfrage wird abgelehnt, der Angriff bleibt).
//  - Takt: je Arm der MF-Takt (+0x40) nach dem Treffer; im Paar wartet jeder, bis der Schlag des anderen gefallen ist -> abwechselnd.
//  - Treffer: MF-Treffsicherheit (+0x3c) des Arms; daneben = der Gegner pariert (sichtbar: er fuehrt seine Waffe dem Schlag entgegen) oder
//    weicht aus. Ein Schild-Arm faengt Schlaege ab (Schaden am Schild). Schaden = MF-Nahkampfschaden mech./Energie (+0x34/+0x38) minus den
//    MF-Schilden des getroffenen Teils; Arme und Beine haben eigene MF-Trefferpunkte und fallen bei 0 ab (modcombot::meleeHit).
//  - Der Schaden faellt genau im Treffer-Moment der Animation (weiteste Streckung, modcombot richtet die Animation darauf aus).

#include "modduel.h"

#include "lib/framework/frame.h"
#include "lib/framework/vector.h"
#include "lib/framework/math_ext.h"
#include "lib/framework/trig.h"
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
#include "projectile.h"
#include "random.h"
#include "stats.h"

#include <cstdint>
#include <unordered_map>
#include <cmath>
#include <algorithm>

namespace modduel
{

bool isCombot(const DROID *d);
static bool canDuel(const DROID *d);

namespace
{

constexpr int SLACK = 40;              // Welteinheiten Spiel um die Schlagweite (Warzone haelt knapp innerhalb der Reichweite an)
constexpr int BREAK_RANGE = 448;       // darueber ist ein Brawl vorbei
constexpr unsigned HIT_DELAY = HIT_DELAY_MS;
constexpr unsigned PAIR_GAP = 250;     // im Paar: Pause nach dem Schlag des anderen, bevor man selbst ausholt
constexpr int FACE_DEG = 35;           // so genau muss er zum Gegner schauen, um auszuholen
constexpr int TURN_DEG_PER_S = 270;    // Drehtempo zum Gegner

enum Outcome { HIT = 0, PARRY, SHIELD };
enum Part { TORSO = 0, RARM, LARM, LEGS, NPARTS };

struct Duel
{
	uint32_t opponent = 0;
	bool paired = false;      // der Gegner hat die Brawl-Anfrage angenommen (schlaegt zurueck)
	unsigned nextSwing = 0;   // gameTime, ab wann wieder ausgeholt werden darf
	unsigned hitAt = 0;       // gameTime des Treffermoments, 0 = kein Schlag unterwegs
	int slot = -1;            // Waffenplatz des laufenden Schlags
	unsigned swing = 0;       // zaehlt die Schlaege (zwei Nahkampfarme wechseln sich ab)
	int outcome = HIT;
	unsigned parryAt = 0;     // dieser Combot pariert einen Schlag, der zu dieser Zeit trifft
	int parrySlot = -1;
	// alte Einzel-Combots (MFW_, nicht modular): Trefferpunkte je Teil
	int partHp[NPARTS] = { 0, 0, 0, 0 };
	bool severed[NPARTS] = { false, false, false, false };
	bool init = false;
};

std::unordered_map<uint32_t, Duel> s_duels;

DROID *findDroid(uint32_t id)
{
	for (int p = 0; p < MAX_PLAYERS; ++p)
	{
		for (DROID *d : gameWorld.objects.droids[p])
		{
			if (d != nullptr && d->id == id && !d->died) { return d; }
		}
	}
	return nullptr;
}

int dist2d(const DROID *a, const DROID *b)
{
	const int dx = a->pos.x - b->pos.x, dy = a->pos.y - b->pos.y;
	return static_cast<int>(std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy));
}

bool enemies(const DROID *a, const DROID *b)
{
	return a->player != b->player && !aiCheckAlliances(a->player, b->player);
}

/// Waffenplaetze der Nahkampfarme (modular), sonst Platz 0 (alte Einzel-Combots)
int meleeSlots(const DROID *d, int out[2])
{
	int n = 0;
	if (modcombot::isModular(d))
	{
		const modcombot::Loadout lo = modcombot::loadoutOf(d);
		if (lo.armR && lo.armR->st.melee && lo.slotR >= 0 && d->asWeaps[lo.slotR].nStat != 0) { out[n++] = lo.slotR; }
		if (lo.armL && lo.armL->st.melee && lo.slotL >= 0 && d->asWeaps[lo.slotL].nStat != 0) { out[n++] = lo.slotL; }
	}
	else if (d->numWeaps > 0 && d->asWeaps[0].nStat != 0)
	{
		out[n++] = 0;
	}
	return n;
}

/// Schlagweite (Welteinheiten) = groesste Reichweite der Nahkampfwaffen (mfreach.py hat sie aus den MF-Angriffen gesetzt)
int strikeRange(const DROID *d)
{
	int slots[2];
	const int n = meleeSlots(d, slots);
	int r = 0;
	for (int i = 0; i < n; ++i)
	{
		const unsigned ws = d->asWeaps[slots[i]].nStat;
		if (ws > 0 && ws < asWeaponStats.size()) { r = std::max(r, proj_GetLongRange(asWeaponStats[ws], d->player)); }
	}
	return r > 0 ? r : 128;
}

bool inAttackAction(const DROID *d)
{
	switch (d->action)
	{
	case DACTION_ATTACK: case DACTION_MOVETOATTACK: case DACTION_ROTATETOATTACK:
		return true;
	default:
		return false;
	}
}

/// darf der Gegner die Brawl-Anfrage annehmen? (frei, kein Fahrbefehl, kein Angriffsbefehl auf ein anderes Ziel)
bool freeFor(const DROID *t, const DROID *attacker)
{
	if (!canDuel(t)) { return false; }
	const auto it = s_duels.find(t->id);
	if (it != s_duels.end() && it->second.opponent != 0 && it->second.opponent != attacker->id)
	{
		const DROID *o = findDroid(it->second.opponent);
		if (o != nullptr && dist2d(t, o) <= BREAK_RANGE) { return false; }   // steckt in einem anderen Brawl
	}
	switch (t->order.type)
	{
	case DORDER_NONE: case DORDER_GUARD: case DORDER_HOLD: case DORDER_STOP:
		return true;
	case DORDER_ATTACK:
		return t->order.psObj == attacker || t->order.psObj == nullptr || t->order.psObj->type != OBJ_DROID || !isCombot(static_cast<const DROID *>(t->order.psObj));
	default:
		return false;
	}
}

void turnToward(DROID *d, const DROID *t)
{
	const uint16_t want = calcDirection(d->pos.x, d->pos.y, t->pos.x, t->pos.y);
	const int diff = static_cast<int16_t>(want - d->rot.direction);
	const int step = std::max(1, static_cast<int>(DEG(TURN_DEG_PER_S) * static_cast<int64_t>(deltaGameTime) / GAME_TICKS_PER_SEC));
	if (std::abs(diff) <= step) { d->rot.direction = want; }
	else { d->rot.direction = static_cast<uint16_t>(d->rot.direction + (diff > 0 ? step : -step)); }
}

bool facing(const DROID *d, const DROID *t)
{
	const uint16_t want = calcDirection(d->pos.x, d->pos.y, t->pos.x, t->pos.y);
	return std::abs(static_cast<int>(static_cast<int16_t>(want - d->rot.direction))) <= DEG(FACE_DEG);
}

// ---------------------------------------------------------------- Rumpf-Wackeln getroffener Panzer (nur Anzeige)
struct Wobble
{
	unsigned start = 0;
	float dirRel = 0.f;
	float amp = 0.f;
};
std::unordered_map<uint32_t, Wobble> s_wobble;

void noteWobble(const DROID *victim, const DROID *attacker, int dmg)
{
	if (victim == nullptr || attacker == nullptr || modcombot::isModular(victim)) { return; }
	const float dx = static_cast<float>(victim->pos.x - attacker->pos.x);
	const float dy = static_cast<float>(victim->pos.y - attacker->pos.y);
	const float worldAng = std::atan2(dx, -dy);
	const float face = static_cast<float>(victim->rot.direction) * 6.2831853f / 65536.f;
	Wobble w;
	w.start = gameTime;
	w.dirRel = worldAng - face;
	w.amp = std::clamp(2.0f + static_cast<float>(dmg) * 0.05f, 2.0f, 6.0f);
	s_wobble[victim->id] = w;
}

// ---------------------------------------------------------------- alte Einzel-Combots (MFW_)
void initParts(Duel &du, const DROID *d)
{
	const int hp = static_cast<int>(d->originalBody);
	du.partHp[TORSO] = hp * 50 / 100;
	du.partHp[RARM] = hp * 15 / 100;
	du.partHp[LARM] = hp * 15 / 100;
	du.partHp[LEGS] = hp * 20 / 100;
	du.init = true;
}

void severOld(DROID *d, Duel &du, Part p)
{
	du.severed[p] = true;
	const Vector3i pos(d->pos.x, d->pos.y, d->pos.z + 24);
	addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_SMALL, false, nullptr, 0, gameTime);
	if (p == RARM)
	{
		for (unsigned i = 0; i < d->numWeaps; ++i) { d->asWeaps[i].ammo = 0; }
		if (d->numWeaps > 0) { d->asWeaps[0].nStat = 0; }
	}
	else if (p == LEGS)
	{
		d->baseSpeed = 1;
	}
}

/// Funken an der Stelle, wo sich die Waffen treffen (Mitte zwischen beiden, Brusthoehe)
void sparks(const DROID *a, const DROID *b, bool big)
{
	if (!a->visibleForLocalDisplay() && !b->visibleForLocalDisplay()) { return; }
	const Vector3i pos((a->pos.x + b->pos.x) / 2, (a->pos.z + b->pos.z) / 2 + 60, (a->pos.y + b->pos.y) / 2);
	addEffect(&pos, EFFECT_EXPLOSION, big ? EXPLOSION_TYPE_SMALL : EXPLOSION_TYPE_LASER, false, nullptr, 0, gameTime);
}

void kill(DROID *victim)
{
	victim->body = 0;
	destroyDroid(victim, gameTime, gameWorld);
}

void applyHit(DROID *att, Duel &ad, DROID *victim)
{
	const modcombot::Part *arm = modcombot::armInSlot(att, ad.slot);
	const int hth = modcombot::isModular(att) ? modcombot::meleePercent(att) : 100;   // MF HTH-Upgrade-Beine
	if (modcombot::isModular(victim))
	{
		float mD = 0.f, eD = 0.f;
		if (arm != nullptr && (arm->st.md > 0.f || arm->st.ed > 0.f)) { mD = arm->st.md; eD = arm->st.ed; }
		else
		{
			// alte Daten ohne MF-Werte: Warzone-Schaden der Waffe als mechanischer Schaden in MF-Groesse (Torso 179)
			const unsigned ws = ad.slot >= 0 ? att->asWeaps[ad.slot].nStat : 0;
			const int wd = (ws > 0 && ws < asWeaponStats.size()) ? weaponDamage(asWeaponStats[ws], att->player) : 40;
			mD = static_cast<float>(wd) * 0.4f;
		}
		mD = mD * static_cast<float>(hth) / 100.f;
		eD = eD * static_cast<float>(hth) / 100.f;
		int forceSlot = -1;
		if (ad.outcome == SHIELD)
		{
			// der Schild-Arm faengt den Schlag ab
			const modcombot::Loadout lo = modcombot::loadoutOf(victim);
			if (lo.armR && lo.armR->st.blk > 0) { forceSlot = modcombot::SLOT_RARM; }
			else if (lo.armL && lo.armL->st.blk > 0) { forceSlot = modcombot::SLOT_LARM; }
		}
		int partHit = -1;
		const int body = modcombot::meleeHit(victim, mD, eD, modcombot::isModular(att) ? modcombot::severPercent(att) : 100, partHit, forceSlot);
		victim->timeLastHit = gameTime;
		if (body > 0)
		{
			if (static_cast<int>(victim->body) <= body) { kill(victim); return; }
			victim->body -= static_cast<UDWORD>(body);
		}
		return;
	}
	// Nicht-modularer Gegner (alter Einzel-Combot): Warzone-Schaden der Waffe, Teile mit eigenen Trefferpunkten
	int dmg = 40;
	const unsigned ws = ad.slot >= 0 ? att->asWeaps[ad.slot].nStat : 0;
	if (ws > 0 && ws < asWeaponStats.size()) { dmg = std::max(10, weaponDamage(asWeaponStats[ws], att->player)); }
	dmg = dmg * hth / 100;
	if (ad.outcome == SHIELD) { dmg = dmg * 15 / 100; }
	noteWobble(victim, att, dmg);
	auto vit = s_duels.find(victim->id);
	Duel tmp;
	Duel &vd = vit != s_duels.end() ? vit->second : tmp;
	if (!vd.init) { initParts(vd, victim); }
	const unsigned r2 = gameRand(100);
	const Part part = r2 < 35 ? TORSO : r2 < 60 ? RARM : r2 < 80 ? LARM : LEGS;
	vd.partHp[part] -= dmg;
	if (vd.partHp[part] <= 0 && !vd.severed[part] && part != TORSO) { severOld(victim, vd, part); }
	victim->timeLastHit = gameTime;
	if (static_cast<int>(victim->body) <= dmg) { kill(victim); return; }
	victim->body -= static_cast<UDWORD>(dmg);
}

void beginSwing(DROID *d)
{
	d->animationEvent = ANIM_EVENT_FIRING;
	d->timeAnimationStarted = gameTime;
}

void endDuel(DROID *d)
{
	const auto it = s_duels.find(d->id);
	if (it == s_duels.end()) { return; }
	const uint32_t opp = it->second.opponent;
	s_duels.erase(it);
	if (d->animationEvent == ANIM_EVENT_FIRING) { resetObjectAnimationState(d); }
	const auto o = s_duels.find(opp);
	if (o != s_duels.end() && o->second.opponent == d->id) { o->second.paired = false; }
}

/// Ziel fuer den Nahkampf: Angriffsziel von Warzone (feindlicher Combot), sonst der angenommene Brawl-Gegner
DROID *chooseTarget(DROID *d)
{
	BASE_OBJECT *at = d->psActionTarget[0];
	if (inAttackAction(d) && at != nullptr && at->type == OBJ_DROID && !at->died)
	{
		DROID *t = static_cast<DROID *>(at);
		if (isCombot(t) && enemies(d, t)) { return t; }
	}
	const auto it = s_duels.find(d->id);
	if (it != s_duels.end() && it->second.paired)
	{
		DROID *o = findDroid(it->second.opponent);
		if (o != nullptr && enemies(d, o) && dist2d(d, o) <= BREAK_RANGE) { return o; }
	}
	return nullptr;
}

} // namespace

bool hullWobble(const DROID *d, float &pitchDeg, float &rollDeg)
{
	if (d == nullptr || s_wobble.empty()) { return false; }
	const auto it = s_wobble.find(d->id);
	if (it == s_wobble.end()) { return false; }
	const float t = static_cast<float>(gameTime - it->second.start) / 1000.f;
	if (t > 1.6f) { return false; }
	const float v = it->second.amp * std::exp(-t * 3.2f) * std::sin(t * 15.f);
	pitchDeg = v * std::cos(it->second.dirRel);
	rollDeg = v * std::sin(it->second.dirRel);
	return true;
}

int swingSlot(const DROID *d)
{
	if (d == nullptr) { return -1; }
	const auto it = s_duels.find(d->id);
	if (it == s_duels.end()) { return -1; }
	if (it->second.slot >= 0) { return it->second.slot; }
	int slots[2];
	return meleeSlots(d, slots) > 0 ? slots[0] : -1;
}

float swingNeedMF(const DROID *d)
{
	if (d == nullptr) { return 0.f; }
	const auto it = s_duels.find(d->id);
	if (it == s_duels.end()) { return 0.f; }
	const DROID *o = findDroid(it->second.opponent);
	if (o == nullptr) { return 0.f; }
	const float k = std::max(0.1f, modcombot::worldPerMF());
	return std::max(0.f, static_cast<float>(dist2d(d, o)) / k - modcombot::frontMF());
}

unsigned parryAt(const DROID *d, int &slot)
{
	slot = -1;
	if (d == nullptr) { return 0; }
	const auto it = s_duels.find(d->id);
	if (it == s_duels.end() || it->second.parryAt == 0 || gameTime > it->second.parryAt + 200) { return 0; }
	slot = it->second.parrySlot;
	return it->second.parryAt;
}

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
	}
	if (!canDuel(d) || d->order.type == DORDER_MOVE || d->order.type == DORDER_RTB || d->order.type == DORDER_RTR)
	{
		// Fernkampf-Modus, kein Nahkampfarm, oder er soll weg (Rueckzug): kein Brawl
		endDuel(d);
		return;
	}

	DROID *t = chooseTarget(d);
	if (t == nullptr) { endDuel(d); return; }
	const int dist = dist2d(d, t);
	const int reach = strikeRange(d);

	Duel &du = s_duels[d->id];
	if (du.opponent != t->id)
	{
		const unsigned keepParry = du.parryAt;
		const int keepSlot = du.parrySlot;
		du = Duel();
		du.opponent = t->id;
		du.nextSwing = gameTime + 200 + gameRand(500);
		du.parryAt = keepParry; du.parrySlot = keepSlot;
	}
	if (du.hitAt != 0 && gameTime >= du.hitAt)
	{
		// Treffer-Moment: der Ausgang wurde beim Ausholen gewuerfelt (der Gegner konnte parieren)
		du.hitAt = 0;
		if (dist <= reach + 2 * SLACK && !t->died)
		{
			if (du.outcome == PARRY) { sparks(d, t, false); }
			else
			{
				if (du.outcome == SHIELD) { sparks(d, t, true); }
				applyHit(d, du, t);
				if (t->died) { endDuel(d); return; }
			}
		}
	}

	if (dist > reach + SLACK)
	{
		// noch nicht auf Schlagweite: Warzone fuehrt ihn heran (DACTION_MOVETOATTACK). Ein Angenommener ohne eigenes Angriffsziel bekommt es.
		if (du.paired && !inAttackAction(d) && freeFor(d, t)) { orderDroidObj(d, DORDER_ATTACK, t, ModeImmediate); }
		return;
	}

	// Brawl-Anfrage an den Gegner (MF CRobot::RequestedBrawl): frei -> er nimmt an, nimmt uns als Ziel und schlaegt zurueck
	if (canDuel(t))
	{
		auto tit = s_duels.find(t->id);
		const bool already = tit != s_duels.end() && tit->second.opponent == d->id;
		if (already || freeFor(t, d))
		{
			Duel &td = s_duels[t->id];
			if (td.opponent != d->id)
			{
				const unsigned keepParry = td.parryAt;
				const int keepSlot = td.parrySlot;
				td = Duel();
				td.opponent = d->id;
				td.nextSwing = std::max(gameTime + 600, du.nextSwing + 700);   // wer gefordert wird, holt etwas spaeter aus
				td.parryAt = keepParry; td.parrySlot = keepSlot;
			}
			td.paired = true;
			du.paired = true;
			if (t->psActionTarget[0] != d && !(t->order.type == DORDER_ATTACK && t->order.psObj == d))
			{
				orderDroidObj(t, DORDER_ATTACK, d, ModeImmediate);
			}
		}
		else
		{
			du.paired = false;   // Gegner beschaeftigt: einseitiger Angriff (er dreht sich nicht um)
		}
	}

	// Gegner anblicken; die Nahkampfwaffen feuern nicht auf Warzone-Art (der Brawl ersetzt das), Fernwaffen schon
	turnToward(d, t);
	int slots[2];
	const int nMelee = meleeSlots(d, slots);
	for (int i = 0; i < nMelee; ++i) { d->asWeaps[slots[i]].lastFired = gameTime; }
	if (nMelee == 0) { return; }

	// ausholen
	const Duel *od = nullptr;
	{
		const auto oit = s_duels.find(t->id);
		if (oit != s_duels.end() && oit->second.opponent == d->id) { od = &oit->second; }
	}
	const bool oppBlowComing = od != nullptr && od->hitAt != 0;
	const bool parrying = du.parryAt != 0 && gameTime <= du.parryAt + PAIR_GAP;
	if (du.hitAt == 0 && gameTime >= du.nextSwing && !oppBlowComing && !parrying && facing(d, t))
	{
		du.slot = slots[du.swing % static_cast<unsigned>(nMelee)];
		++du.swing;
		beginSwing(d);
		du.hitAt = gameTime + HIT_DELAY;
		const modcombot::Part *arm = modcombot::armInSlot(d, du.slot);
		const float cyc = arm != nullptr ? arm->st.cyc : 0.75f;
		const float acc = arm != nullptr ? arm->st.acc : 0.85f;
		// naechster Schlag: nach dem Treffer der Nachschwung der Animation (~0,4 s) plus der MF-Takt des Arms
		du.nextSwing = du.hitAt + 400 + static_cast<unsigned>(std::lround(cyc * 1000.f));
		// Ausgang jetzt wuerfeln, damit der Gegner rechtzeitig parieren kann
		du.outcome = HIT;
		if (gameRand(1000) >= static_cast<uint32_t>(acc * 1000.f)) { du.outcome = PARRY; }
		else if (canDuel(t) && gameRand(100) < static_cast<uint32_t>(std::max(0, modcombot::blockBonus(t)))) { du.outcome = SHIELD; }
		if (du.outcome != HIT)
		{
			// der Gegner fuehrt seine Waffe (bzw. den Schild) dem Schlag entgegen - nur wenn er schaut und nicht selbst gerade zuschlaegt
			auto tit = s_duels.find(t->id);
			const bool tSwinging = tit != s_duels.end() && tit->second.hitAt != 0;
			int tslots[2];
			const int tn = meleeSlots(t, tslots);
			if (!tSwinging && tn > 0 && facing(t, d))
			{
				Duel &td = s_duels[t->id];
				if (td.opponent == 0) { td.opponent = d->id; }
				td.parryAt = du.hitAt;
				int ps = tslots[0];
				if (du.outcome == SHIELD)
				{
					const modcombot::Loadout lo = modcombot::loadoutOf(t);
					if (lo.armR && lo.armR->st.blk > 0 && lo.slotR >= 0) { ps = lo.slotR; }
					else if (lo.armL && lo.armL->st.blk > 0 && lo.slotL >= 0) { ps = lo.slotL; }
				}
				td.parrySlot = ps;
				td.nextSwing = std::max(td.nextSwing, du.hitAt + PAIR_GAP);
			}
		}
	}
	else if (d->animationEvent == ANIM_EVENT_FIRING && du.hitAt == 0 && gameTime > d->timeAnimationStarted + 2500)
	{
		resetObjectAnimationState(d);
	}
}

void updateLeggedAnimation(DROID *d, int moveSpeed)
{
	if (d == nullptr || d->animationEvent == ANIM_EVENT_DYING || !isCombot(d)) { return; }
	if (s_duels.count(d->id)) { return; } // im Brawl steuert der Brawl die Animation
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
