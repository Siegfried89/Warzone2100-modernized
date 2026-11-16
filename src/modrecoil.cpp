// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Rueckstoss-Wackeln (siehe modrecoil.h)
#include "modrecoil.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "lib/framework/frame.h"
#include "lib/framework/vector.h"
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
	if (settled(s.hull) && settled(s.prop))
	{
		s_shake.erase(it);
		return false;
	}
	hull = glm::rotate(glm::radians(s.hull.p), glm::vec3(1.f, 0.f, 0.f)) * glm::rotate(glm::radians(s.hull.r), glm::vec3(0.f, 0.f, 1.f));
	prop = glm::rotate(glm::radians(s.prop.p), glm::vec3(1.f, 0.f, 0.f)) * glm::rotate(glm::radians(s.prop.r), glm::vec3(0.f, 0.f, 1.f));
	return true;
}

bool duck(const DROID *d, unsigned renderTime, glm::mat4 &out)
{
	if (d == nullptr || d->droidType != DROID_PERSON || d->timeLastHit == 0) { return false; }
	// timeLastHit steht in Spielzeit, renderTime in Bildzeit: beide laufen gleich, die Bildzeit kann der Spielzeit knapp voraus sein
	const int since = static_cast<int>(renderTime) - static_cast<int>(d->timeLastHit);
	if (since < 0 || since > 4200) { return false; }
	// Kopf runter wie beim Rennen in Deckung: schnell hinein (0,25 s), halten bis 3 s, dann langsam aufrichten
	float k;
	if (since < 250) { k = static_cast<float>(since) / 250.f; }
	else if (since < 3000) { k = 1.f; }
	else { k = 1.f - static_cast<float>(since - 3000) / 1200.f; }
	k = std::clamp(k, 0.f, 1.f);
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
