// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Schlachtfeld-Autonomie (Wunsch 14.11., Punkt 12): Infanterie, die ploetzlich unter Beschuss einer schweren Kanone
// (Kanone, Haubitze, Moerser) kommt, zieht sich selbststaendig ein paar Felder vom Schuetzen weg zurueck, wartet kurz und rueckt
// dann wieder dorthin vor, wo sie war (bzw. zu ihrem alten Vorrueck-Ziel). Ausdrueckliche Fahrbefehle des Spielers, "Stellung
// halten" und Einheiten im Graben bleiben unangetastet. Gilt fuer alle Spieler gleich und deterministisch (nur Spielzustand).
#pragma once

struct DROID;
struct PROJECTILE;

namespace modmorale
{
/// aus droidDamage(): Einheit hat hpLost Lebenspunkte durch dieses Geschoss verloren und lebt noch
void onHit(DROID *psDroid, int hpLost, const PROJECTILE *psProj);
/// jeden Spielschritt: Rueckkehr nach der Wartezeit
void tick();
/// Spielende / Laden
void reset();
}
