// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Munition fuer Einheiten (Spieler). Jede Einheit mit Feuerwaffe hat einen Vorrat (in Schuessen, je Waffenart); der Vorrat wird in
// 5 Abschnitten angezeigt (zweiter, gelber Balken). Ohne Munition feuert die Waffe nur noch 10-mal so langsam. Nachschub bekommen
// Einheiten in der Naehe eigener Fabriken, des Hauptquartiers, Reparaturanlagen, Aussenposten und Munitionskisten (Bauwerke mit
// "Outpost" oder "AmmoBox" im Namen); Munitionskisten fuellen schneller. KI-Spieler, Combots (Energie/Nahkampf) und Energiewaffen
// brauchen keine Munition. Alles im Spielschritt, deterministisch.
#pragma once

struct DROID;
struct GameWorld;
struct WEAPON_STATS;

namespace modammo
{
/// braucht die Einheit Munition? (Spieler, Feuerwaffe mit Munitionsart)
bool usesAmmo(const DROID *psDroid);
/// angezeigte Abschnitte 0..5; -1, wenn die Einheit keine Munition braucht
int segments(const DROID *psDroid);
/// 1 normal, 10 wenn leer: Faktor auf die Feuerpause
unsigned firePauseFactor(const DROID *psDroid);
/// ein Schuss wurde abgegeben
void onShot(const DROID *psDroid);
/// Nachschub (alle 500 ms) und Aufraeumen
void tick(GameWorld &world);
void reset();
}
