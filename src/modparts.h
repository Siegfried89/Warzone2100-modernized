// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Teile-Schaden fuer alle Bodenfahrzeuge (Wunsch 14.11., Punkt 19). Turm und Laufwerk haben eigene Lebenspunkte neben dem Rumpf.
//  - Jeder Treffer geht ausserdem mit einer gewissen Wahrscheinlichkeit auf den Turm oder das Laufwerk (Artillerie trifft oefter den Turm).
//  - Turm zerstoert: er fliegt spektakulaer davon (Halterung und Waffe als Truemmer mit Rauchspur, Explosion beim Aufprall), das Fahrzeug
//    hat keine Waffe mehr und der Schuetze jubelt (Schlachtruf "cheer").
//  - Laufwerk zerstoert: das Fahrzeug bleibt stehen (kann sich noch drehen und, falls der Turm lebt, schiessen), sackt ab und qualmt.
//  - Sind Turm UND Laufwerk weg, liegt der Rumpf frei und nimmt 50 % mehr Schaden - ist er hin, explodiert das Fahrzeug und wird zum Wrack (modwreck).
//  - Reparatur: die Teile heilen mit dem Rumpf mit; ein zerstoertes Teil kommt erst bei voller Rumpfgesundheit zurueck (Turm wird neu aufgesetzt).
// Gilt fuer alle Spieler gleich und deterministisch (gameRand nur im Schadensschritt). Nicht fuer Infanterie, Cyborgs, Transporter, VTOLs und
// die modularen Combots (die haben ihr eigenes Gliedmassen-System in modcombot).
#pragma once

#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)

struct DROID;
struct GameWorld;
struct PROJECTILE;

namespace modparts
{
/// true, wenn das Teile-System fuer dieses Fahrzeug gilt
bool applies(const DROID *psDroid);
/// aus droidDamage(): Faktor auf den Schaden (liegt der Rumpf frei, 150 %)
unsigned scaleDamage(const DROID *psDroid, unsigned damage);
/// aus droidDamage(): der Rumpf hat hpLost Lebenspunkte verloren (Fahrzeug lebt noch); psProj darf nullptr sein (dann ohne Wirkung)
void onHit(DROID *psDroid, int hpLost, const PROJECTILE *psProj);
/// Laufwerk zerstoert: das Fahrzeug darf sich nicht fortbewegen
bool immobile(const DROID *psDroid);
/// Turm zerstoert
bool turretGone(const DROID *psDroid);
/// Anzeige: Fahrzeug mit zerstoertem Laufwerk sackt ab und neigt sich (true = out ist gesetzt)
bool slump(const DROID *psDroid, glm::mat4 &out);
/// Lebenspunkte des Teils in Prozent (0-100); -1 = Fahrzeug hat das Teil nicht
int partPercent(const DROID *psDroid, bool turret);
/// jeden Spielschritt aus der Spielschleife (Ausfuehrung der Zerstoerung, Qualm, Heilung; rechnet selbst nur alle 250 ms)
void tick(GameWorld &world);
/// beim Spielende / Ladevorgang
void reset();
}
