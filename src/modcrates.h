// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 16.11. 16:18): Munitionskisten mit dem Lkw transportieren und ablegen.
//  - Lkw mit Ladeflaeche (Scav-Lkw ScavBed1/2/3, Schaufel-Lkw) laden am Munitionsdepot Kisten: Lkw waehlen, Strg + Klick auf das eigene Depot.
//  - Strg + Klick auf den Boden: der Lkw faehrt hin und laedt ALLE Kisten dort ab (ein Haufen, Bauwerk "A0BaBaAmmoBoxDrop<n>").
//  - Der Kistenhaufen versorgt Einheiten mit Munition wie eine Munitionskiste (modammo: "AmmoBox" im Namen); leere Einheiten fahren
//    immer zur naechsten Quelle (Depot oder Kistenhaufen, gleich gewertet).
//  - Kistenhaufen (und beladene Lkw) sind leicht zu zerstoeren und explodieren gewaltig: grosser Radius, starker Schaden fuer alle,
//    viele Teile fliegen brennend durch die Luft; benachbarte Haufen gehen in einer Kettenreaktion mit hoch.
// Spielwirkung nur im Spielschritt mit gameTime und fester Reihenfolge; Truemmerflug und Feuer sind Anzeige.
#pragma once

#include <cstdint>
#include <vector>

struct DROID;
struct STRUCTURE;

namespace modcrates
{
constexpr uint16_t CODE_LOAD = 98;     // in DORDER_FORTIFY.direction: Kisten am Depot laden
constexpr uint16_t CODE_UNLOAD = 99;   // in DORDER_FORTIFY.direction: alle Kisten hier abladen

/// Kisten-Platz auf dem Lkw (0 = kann keine Kisten tragen)
int capacity(const DROID *psDroid);
/// geladene Kisten
int crates(const DROID *psDroid);
/// Lkw ist mit Kisten beschaeftigt (geladen oder Lade-/Ablade-Auftrag): kein Oel-Autopilot
bool busy(uint32_t droidId);
/// order.cpp DORDER_FORTIFY: true = Kisten-Auftrag uebernommen
bool startOrder(DROID *psDroid, int worldX, int worldY, uint16_t code);
/// Oberflaeche: Strg + Klick auf ein Bauwerk. true = Lade-Befehl an gewaehlte Lkw verschickt
bool loadOrderSelected(unsigned player, const STRUCTURE *psStructure);
/// Oberflaeche: soll dieser Lkw bei Strg + Klick auf den Boden abladen (statt Befestigung zu bauen)?
bool wantsUnload(const DROID *psDroid);
/// Hooks beim Zerstoeren (vor dem Entfernen)
void onStructureDestroyed(const STRUCTURE *psDel);
void onDroidDestroyed(const DROID *psDel);
/// jeden Spielschritt
void tick();
void reset();
}
