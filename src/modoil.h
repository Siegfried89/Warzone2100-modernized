// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Oelkreislauf. Oeltuerme erzeugen Faesser (bis 5 am Turm) statt direkt Energie, Scav-Lkw mit Ladeflaeche (Autopilot) bringen sie zum
// Kraftwerk (oder zuerst ins Lager = Wasserturm, +10 %), jedes Fass haelt das Kraftwerk (und damit das Stromnetz) 3 Minuten am Laufen.
// Oelpipelines verbinden Oelturm und Kraftwerk/Lager ohne Lkw. Zerstoerte Pipeline-Stuecke werden zum Bruchstueck (Feuer, Rauch) und
// durch Reparatur wieder heil. Fass-Haufen (OilDrum5 / OilDrum10) und beladene Lkw explodieren mit Kettenreaktion.
// Gilt nur im Mehrspieler/Gefecht fuer menschliche Spieler und nur, wenn die Mod-Daten (A0BaBaPipeline) geladen sind.
#pragma once

#include <cstdint>
#include <glm/fwd.hpp>

struct STRUCTURE;
struct FEATURE;
struct DROID;

namespace modoil
{
/// gilt die Fass-Wirtschaft fuer diesen Spieler?
bool manages(int player);
/// aus power.cpp: Oelpumpe foerdert (amountFP Energie in Festkomma mit fpOne = 1,0) -> wird zu Fass-Fortschritt am Turm
void extract(const STRUCTURE *psDerrick, int64_t amountFP, int64_t fpOne);
/// jeden Spielschritt aus der Spielschleife (Explosionen sofort, Logistik einmal je Spielsekunde)
void tick();
/// laeuft das Kraftwerk (hat es Brennstoff)? Immer wahr fuer Spieler ohne Fass-Wirtschaft.
bool generatorRunning(const STRUCTURE *psGen);
/// Hooks beim Zerstoeren (vor dem Entfernen aufrufen)
void onStructureDestroyed(const STRUCTURE *psDel);
void onFeatureDestroyed(const FEATURE *psDel);
void onDroidDestroyed(const DROID *psDel);
/// Faesser-Anzeige (Anzeigemodus 4), nach dem 3D-Bild aufrufen
void drawOverlay(const glm::mat4 &perspectiveViewMatrix);
/// beim Spielende / Ladevorgang
void reset();
}
