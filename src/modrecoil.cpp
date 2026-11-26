// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Rueckstoss-Wackeln (siehe modrecoil.h)
#include "modrecoil.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "lib/framework/frame.h"
#include "lib/framework/vector.h"
#include "lib/gamelib/gtime.h"
#include "droid.h"
#include "stats.h"
#ifndef GLM_ENABLE_EXPERIMENTAL
	#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>   // glm::rotate/translate/scale ohne Matrix-Argument

namespace modrecoil
{
namespace
{
// Zwei gedaempfte Schwinger je Fahrzeug (jeweils Nicken und Rollen, in Grad): "hull" = Rumpf mit Turm, "prop" = Ketten/Beine.
struct Osc { float p = 0.f, r = 0.f, vp = 0.f, vr = 0.f; };
struct Shake
{
	Osc hull, prop;
	unsigned last = 0;
	unsigned rumbleStart = 0, rumbleLen = 0;   // Motor-Anlassen: Zittern des Rumpfes (Spielzeit)
	float rumbleAmp = 0.f;
};
std::unordered_map<unsigned, Shake> s_shake;

constexpr float W_HULL = 15.0f, Z_HULL = 0.22f;    // Eigenfrequenz (rad/s) und Daempfung: der Rumpf federt weich und etwas laenger
constexpr float W_PROP = 23.0f, Z_PROP = 0.34f;    // Ketten: schneller und straffer - daher nicht synchron zum Rumpf
constexpr float MAX_DEG = 3.2f;

void step(Osc &o, float w, float z, float dt)
{
	// halbimplizites Euler-Verfahren in kleinen Teilschritten
	const int n = std::max(1, static_cast<int>(std::ceil(dt / 0.008f)));
	const float h = dt / static_cast<float>(n);
	for (int i = 0; i < n; ++i)
	{
		o.vp += (-w * w * o.p - 2.f * z * w * o.vp) * h;
		o.vr += (-w * w * o.r - 2.f * z * w * o.vr) * h;
		o.p += o.vp * h;
		o.r += o.vr * h;
	}
	o.p = std::clamp(o.p, -MAX_DEG, MAX_DEG);
	o.r = std::clamp(o.r, -MAX_DEG, MAX_DEG);
}

bool settled(const Osc &o)
{
	return std::fabs(o.p) < 0.02f && std::fabs(o.r) < 0.02f && std::fabs(o.vp) < 0.3f && std::fabs(o.vr) < 0.3f;
}

bool eligible(const DROID *d)
{
	if (d == nullptr) { return false; }
	switch (d->droidType)
	{
	case DROID_WEAPON:
	case DROID_DEFAULT:
		break;
	default:
		return false;                                      // Infanterie/Cyborgs, Transporter, Bautrupps usw. wackeln nicht
	}
	const PROPULSION_STATS *ps = d->getPropulsionStats();
	if (ps == nullptr || ps->propulsionType == PROPULSION_TYPE_LIFT) { return false; }
	return true;
}
} // namespace

void onShot(const DROID *d, const WEAPON_STATS *st, int weaponDir)
{
	if (st == nullptr || !eligible(d)) { return; }
	const int dmg = std::max(1, weaponDamage(*st, d->player));
	const int pause = weaponFirePause(*st, d->player);
	float amp = 0.25f + 0.09f * std::sqrt(static_cast<float>(dmg));     // Grad Ausschlag je Schuss
	if (pause > 0 && pause < 500) { amp *= 0.6f; }                      // Schnellfeuer: viele kleine Stoesse statt eines grossen
	amp = std::clamp(amp, 0.3f, 2.4f);
	// Der Rueckstoss wirft den Rumpf nach hinten: schiesst der Turm nach vorn, hebt sich der Bug (positives Nicken); Rollen quer dazu.
	const float dirRel = static_cast<float>(weaponDir) * 6.2831853f / 65536.f;
	const float cp = std::cos(dirRel), sr = std::sin(dirRel);
	if (s_shake.size() > 600) { s_shake.clear(); }                     // reine Anzeige: lieber verwerfen als wachsen lassen
	Shake &s = s_shake[d->id];
	s.hull.vp += amp * W_HULL * cp;
	s.hull.vr += amp * W_HULL * sr;
	// Ketten bekommen nur etwa die Haelfte und eine leicht andere Richtung (die Aufhaengung nimmt den Stoss mit Verzoegerung auf)
	s.prop.vp += -0.5f * amp * W_PROP * cp * 0.8f;
	s.prop.vr += 0.5f * amp * W_PROP * sr;
	const float vmaxH = W_HULL * MAX_DEG, vmaxP = W_PROP * MAX_DEG;     // Geschwindigkeit begrenzen (Fahrzeuge ausserhalb des Bildes schiessen weiter)
	s.hull.vp = std::clamp(s.hull.vp, -vmaxH, vmaxH);
	s.hull.vr = std::clamp(s.hull.vr, -vmaxH, vmaxH);
	s.prop.vp = std::clamp(s.prop.vp, -vmaxP, vmaxP);
	s.prop.vr = std::clamp(s.prop.vr, -vmaxP, vmaxP);
}

void engineStart(const DROID *d, int cls)
{
	if (d == nullptr || cls <= 0) { return; }
	if (s_shake.size() > 600) { s_shake.clear(); }
	Shake &s = s_shake[d->id];
	s.rumbleStart = gameTime;
	s.rumbleLen = cls == 1 ? 520u : (cls == 2 ? 760u : 900u);
	s.rumbleAmp = cls == 1 ? 0.55f : (cls == 2 ? 0.75f : 0.85f);
	// der Motor faengt an: Drehmoment-Ruck kippt den Rumpf kurz zur Seite und den Bug etwas hoch, die Ketten bleiben fast ruhig
	const float kick = cls == 1 ? 0.9f : (cls == 2 ? 1.2f : 1.35f);
	s.hull.vr += kick * W_HULL;
	s.hull.vp += 0.45f * kick * W_HULL;
	s.prop.vr -= 0.12f * kick * W_PROP;
}

bool shake(const DROID *d, unsigned renderTime, glm::mat4 &hull, glm::mat4 &prop)
{
	if (d == nullptr || s_shake.empty()) { return false; }
	const auto it = s_shake.find(d->id);
	if (it == s_shake.end()) { return false; }
	Shake &s = it->second;
	if (s.last != 0 && renderTime > s.last)
	{
		const float dt = std::min(0.1f, static_cast<float>(renderTime - s.last) / 1000.f);
		step(s.hull, W_HULL, Z_HULL, dt);
		step(s.prop, W_PROP, Z_PROP, dt);
	}
	s.last = renderTime;
	// Motor-Anlassen: kurzes Zittern (schnell ansteigend, dann ausklingend); Ketten nur ein Fuenftel davon
	float rp = 0.f, rr = 0.f;
	bool rumbling = false;
	if (s.rumbleLen != 0)
	{
		const int since = static_cast<int>(renderTime) - static_cast<int>(s.rumbleStart);
		if (since >= static_cast<int>(s.rumbleLen) || since < -200) { s.rumbleLen = 0; }
		else if (since >= 0)
		{
			rumbling = true;
			const float t = static_cast<float>(since) / 1000.f;
			const float x = static_cast<float>(since) / static_cast<float>(s.rumbleLen);
			const float env = std::min(1.f, x * 8.f) * (1.f - x) * (1.f - x);
			rr = s.rumbleAmp * env * std::sin(t * 2.f * 3.1415927f * 11.f);
			rp = 0.6f * s.rumbleAmp * env * std::sin(t * 2.f * 3.1415927f * 14.5f + 1.3f);
		}
		else { rumbling = true; }
	}
	if (!rumbling && settled(s.hull) && settled(s.prop))
	{
		s_shake.erase(it);
		return false;
	}
	hull = glm::rotate(glm::radians(s.hull.p + rp), glm::vec3(1.f, 0.f, 0.f)) * glm::rotate(glm::radians(s.hull.r + rr), glm::vec3(0.f, 0.f, 1.f));
	prop = glm::rotate(glm::radians(s.prop.p + 0.2f * rp), glm::vec3(1.f, 0.f, 0.f)) * glm::rotate(glm::radians(s.prop.r + 0.2f * rr), glm::vec3(0.f, 0.f, 1.f));
	return true;
}

bool duck(const DROID *d, unsigned renderTime, glm::mat4 &out)
{
	// (Wunsch 26.11. 19:32 "Die nach vorne gebeugte Animation sieht schlecht aus. Das ganze Modell lehnt sich stark nach vorne, statt nur der
	// Oberkoerper und Kopf. Die Beinbewegung ist eigentlich ok, aber ohne das Lehnen unten."): die ganze Figur wird nicht mehr gekippt - das
	// Ducken steckt jetzt in den Laufanimationen selbst (Paket: crouchrun.py neigt nur Rumpf, Kopf, Arme und Waffe, die Beine bleiben).
	static const bool kWholeBodyDuck = false;
	if (!kWholeBodyDuck || d == nullptr || d->droidType != DROID_PERSON) { return false; }
	// (Wunsch 26.11. "Die Laufanimation der Infanterie soll nur noch die geduckte Variante sein"): wer laeuft, laeuft geduckt - weich
	// hinein (0,25 s) und nach dem Anhalten wieder aufrichten (0,5 s)
	static std::unordered_map<uint32_t, std::pair<float, unsigned>> s_move;
	if (s_move.size() > 4000) { s_move.clear(); }
	auto &mv = s_move[d->id];
	const bool moving = d->sMove.Status != MOVEINACTIVE && d->sMove.Status != MOVEWAITROUTE && std::abs(d->sMove.speed) > 0;
	const float dt = mv.second == 0 ? 0.f : static_cast<float>(std::min(500u, renderTime - std::min(renderTime, mv.second)));
	mv.second = renderTime;
	mv.first = moving ? std::min(1.f, mv.first + dt / 250.f) : std::max(0.f, mv.first - dt / 500.f);
	float k = mv.first;
	// timeLastHit steht in Spielzeit, renderTime in Bildzeit: beide laufen gleich, die Bildzeit kann der Spielzeit knapp voraus sein
	const int since = d->timeLastHit == 0 ? -1 : static_cast<int>(renderTime) - static_cast<int>(d->timeLastHit);
	if (since >= 0 && since <= 4200)
	{
		// Kopf runter wie beim Rennen in Deckung: schnell hinein (0,25 s), halten bis 3 s, dann langsam aufrichten
		float kh;
		if (since < 250) { kh = static_cast<float>(since) / 250.f; }
		else if (since < 3000) { kh = 1.f; }
		else { kh = 1.f - static_cast<float>(since - 3000) / 1200.f; }
		k = std::max(k, kh);
	}
	k = std::clamp(k, 0.f, 1.f);
	if (k <= 0.f) { return false; }
	const float lean = -26.f * k;                      // negatives Nicken = Oberkoerper nach vorn
	out = glm::scale(glm::vec3(1.f, 1.f - 0.10f * k, 1.f)) * glm::rotate(glm::radians(lean), glm::vec3(1.f, 0.f, 0.f));
	return true;
}

bool hover(const DROID *d, unsigned renderTime, glm::mat4 &out)
{
	if (d == nullptr || (d->droidType != DROID_WEAPON && d->droidType != DROID_DEFAULT && d->droidType != DROID_CONSTRUCT && d->droidType != DROID_REPAIR && d->droidType != DROID_SENSOR && d->droidType != DROID_ECM && d->droidType != DROID_CYBORG_CONSTRUCT)) { return false; }
	const PROPULSION_STATS *ps = d->getPropulsionStats();
	if (ps == nullptr || ps->propulsionType != PROPULSION_TYPE_HOVER) { return false; }
	const bool moving = d->sMove.Status != MOVEINACTIVE;
	const float t = static_cast<float>(renderTime) / 1000.f;
	const float ph = static_cast<float>(d->id % 97u) * 0.37f;                 // jedes Fahrzeug schwebt in eigenem Takt
	const float k = moving ? 1.0f : 0.6f;
	const float up = (2.6f + 1.4f * k) * std::sin(t * 2.1f + ph) + 0.8f * std::sin(t * 4.7f + ph * 1.7f);
	const float pitch = (0.5f + 0.7f * k) * std::sin(t * 1.7f + ph * 0.6f) - (moving ? 1.2f : 0.f);   // in Fahrt leicht nach vorn geneigt
	const float roll = (0.6f + 0.6f * k) * std::sin(t * 2.3f + ph * 1.3f);
	out = glm::translate(glm::vec3(0.f, 4.f + up, 0.f)) * glm::rotate(glm::radians(pitch), glm::vec3(1.f, 0.f, 0.f)) * glm::rotate(glm::radians(roll), glm::vec3(0.f, 0.f, 1.f));
	return true;
}

void reset()
{
	s_shake.clear();
}
} // namespace modrecoil
