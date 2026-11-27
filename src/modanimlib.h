// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 27.11. 23:19 / 23:38): fertige Animationen fuer Ueber-Rumpfe (Hull Forge, Liste "ANIMATIONS" - Reiter "Library").
// Ganze Gangarten finden die Beine selbst: Teile, die auf dem Boden stehen (Unterschenkel), und das Teil, an dem sie oben haengen
// (Oberschenkel, zur Rumpfmitte hin). Jedes Bein hebt (Huefte), schwingt (Huefte, nur seitliche Beine) und pendelt im Knie; die Beine
// wechseln sich ab (reihum ums Fahrgestell abwechselnd zwei Gruppen) bzw. gehen nacheinander (Krabbler). Der Rumpf in der Mitte federt.
//  - "Dragoon Walk": 4 Posen, Paare im Wechsel (der erste Satz fuer den Dragoon - "SEHR cool, auf jeden Fall speichern")
//  - "Monkeylord Walk": 6 Posen wie der Monkeylord aus Supreme Commander - beide Teile jedes Beins arbeiten: Knie hoch, Unterschenkel
//    klappt beim Anheben ein und greift weit vor; der Rumpf taucht ein, schaukelt und dreht sich mit
//  - "Crawler Walk": 8 Posen, die Beine reihum (Welle)
// Einzelne Animationen wirken auf das gewaehlte Teil (sonst die Rumpfmitte): Drehen (Radar), Schweben, Atmen, Rueckstoss, Wippen.
#pragma once

#include <string>
#include "modcompbody.h"

namespace modanimlib
{
enum Gait { GAIT_WALK = 0, GAIT_MONKEY, GAIT_CRAWL, GAIT_COUNT };
const char *gaitName(int g);
const char *gaitInfo(int g);
/// Gangart auf den ganzen Rumpf legen (ersetzt die bisherige Animation); false + Meldung, wenn keine Beine gefunden werden
bool applyGait(modcompbody::Def &d, int gait, std::string &msg);

enum Single { ONE_SPIN = 0, ONE_HOVER, ONE_BREATH, ONE_RECOIL, ONE_SWAY, ONE_COUNT };
const char *singleName(int s);
const char *singleInfo(int s);
/// welcher Ablauf zeigt die einzelne Animation am besten (fuer die Vorschau): CLIP_MOVING / CLIP_STANDING / CLIP_FIRING
int singleClip(int s);
/// einzelne Animation auf das Teil sel (sel < 0: Rumpfmitte)
bool applySingle(modcompbody::Def &d, int sel, int what, std::string &msg);

/// ganze Animation weg (Posen, Bausteine); Aufhaengung/Drehpunkte bleiben
void clearAll(modcompbody::Def &d);
/// eigener Satz (modcompbody::loadAnimSet): Animation Teil fuer Teil uebernehmen (nach Nummer)
bool applySet(modcompbody::Def &d, const modcompbody::Def &set, std::string &msg);

/// kurze Beschreibung der Posen eines Teils in einem Ablauf ("turn 22  tilt 28  move 2")
std::string describeKeys(const modcompbody::Part &p, int clip);
/// kurze Beschreibung des Bausteins ("Spin  always")
std::string describeMotion(const modcompbody::Motion &m);
}
