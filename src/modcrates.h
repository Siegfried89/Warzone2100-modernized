// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 16.11. 16:18 / 16:29 / 16:33): Munitions-Nachschub als Kreislauf Fabrik -> Lkw -> Depot -> Front.
//  - Fabriken stellen Munition her: alle 45 s eine Kiste, die neben der Fabrik auf einen Kistenhaufen kommt (hoechstens 4).
//  - Lkw mit Ladeflaeche (Scav-Lkw ScavBed1/2/3 = 2/3/4 Kisten, Schaufel-Lkw = 4) holen die Kisten von selbst ab und bringen sie zum
//    naechsten Depot mit Platz (Autopilot, wenn der Lkw frei ist und kein Oel faehrt).
//  - Depots fassen viel (30 Kisten); von dort beliefern sie Einheiten und die Front.
//  - Von Hand: Lkw waehlen, Strg + Klick auf eine eigene Munitionsquelle = laden (bzw. mit Kisten an Bord: ins Depot/die Kiste einlagern);
//    Strg + Klick auf den Boden = hinfahren und ALLE Kisten dort als Haufen abladen.
//  - Depots, Munitionskisten und Haufen haben einen Vorrat (je Kiste 2 volle Ladungen) und versorgen Einheiten, bis er leer ist.
//    Leere Haufen verschwinden; leere Depots/Kisten bleiben stehen und koennen wieder befuellt werden.
//    Leere Einheiten fahren zur naechsten Quelle mit Vorrat.
//  - Kistenhaufen (und beladene Lkw) sind leicht zu zerstoeren und explodieren gewaltig (Kettenreaktion, fliegende Kisten).
// Spielwirkung nur im Spielschritt mit gameTime und fester Reihenfolge; Truemmerflug und Feuer sind Anzeige.
#pragma once

#include <cstdint>
#include <vector>

struct DROID;
struct STRUCTURE;

namespace modcrates
{
constexpr uint16_t CODE_LOAD = 98;     // in DORDER_FORTIFY.direction: an der Quelle laden bzw. einlagern
constexpr uint16_t CODE_UNLOAD = 99;   // in DORDER_FORTIFY.direction: alle Kisten hier abladen

/// Kisten-Platz auf dem Lkw (0 = kann keine Kisten tragen)
int capacity(const DROID *psDroid);
/// geladene Kisten
int crates(const DROID *psDroid);
/// Lkw ist mit Kisten beschaeftigt (geladen oder Auftrag): kein Oel-Autopilot
bool busy(uint32_t droidId);
/// order.cpp DORDER_FORTIFY: true = Kisten-Auftrag uebernommen
bool startOrder(DROID *psDroid, int worldX, int worldY, uint16_t code);
/// Oberflaeche: Strg + Klick auf ein Bauwerk. true = Befehl an gewaehlte Lkw verschickt
bool loadOrderSelected(unsigned player, const STRUCTURE *psStructure);
/// (Wunsch 18.11. 12:43 "Trucks koennen keine Munitionskisten aufnehmen") normaler Klick: ist das ein Bauwerk, an dem dieser Lkw Kisten
/// laden oder einlagern kann (Fabrik mit Kistenhaufen, Haufen, Depot, Munitionskiste)?
bool isCrateTarget(const DROID *psDroid, const STRUCTURE *psStructure);
/// Oberflaeche: soll dieser Lkw bei Strg + Klick auf den Boden abladen (statt Befestigung zu bauen)?
bool wantsUnload(const DROID *psDroid);
/// modammo: Quelle mit Vorrat (Depot, Kiste, Haufen)? Dann hasStock pruefen
bool finite(const STRUCTURE *psSource);
/// modammo: hat die Quelle noch Munition? (unbegrenzte Quellen: immer)
bool hasStock(const STRUCTURE *psSource);
/// modammo: Quelle hat Munition abgegeben (Promille einer vollen Ladung)
void consume(const STRUCTURE *psSource, int permille);
/// Anzeige: Kisten-Plaetze eines Depots/einer Kiste/eines Haufens (-1 = keine Vorrats-Quelle)
int crateCapAt(const STRUCTURE *psSource);
/// Anzeige/Hilfe: Kisten in einer Quelle (-1 = unbegrenzt)
int cratesAt(const STRUCTURE *psSource);
/// Hooks beim Zerstoeren (vor dem Entfernen)
void onStructureDestroyed(const STRUCTURE *psDel);
void onDroidDestroyed(const DROID *psDel);
/// jeden Spielschritt
void tick();
void reset();
}
