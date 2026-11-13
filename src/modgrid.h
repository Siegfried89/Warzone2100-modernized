// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Stromnetz. Strommasten (Struktur-Typ GENERIC, Kennung beginnt mit "A0PowerPole") bilden Leitungen,
// Kraftwerke speisen sie ein, Gebaeude mit "requiresPower" arbeiten nur im Versorgungsbereich eines gespeisten Mastes.
// Das Kabel zwischen verbundenen Masten wird als schwarze, durchhaengende Linie gezeichnet.
#pragma once

#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include <glm/fwd.hpp>

struct STRUCTURE;
struct STRUCTURE_STATS;

namespace modgrid
{
/// Strommast?
bool isPole(const STRUCTURE_STATS *psStats);
/// einmal pro Spielschritt aus der Spielschleife aufrufen (rechnet das Netz hoechstens einmal je Spielsekunde neu)
void update();
/// ist das Gebaeude (mit requiresPower) gerade versorgt? Gebaeude ohne Eintrag (gerade erst gebaut) gelten bis zur naechsten Berechnung als unversorgt
bool isPowered(const STRUCTURE *psStruct);
/// PowerDown (MF): Aufruf am Anfang des Gebaeude-Updates. Haengt ein verwaltetes Gebaeude mit "requiresPower" nicht am Netz, wird es abgeschaltet
/// (Fabrik/Forschung gehen auf Halt, nichts arbeitet, kein Feuer); beim Wiederkehren fahren sie nacheinander hoch (1,2 s Abstand). true = Update ueberspringen.
bool powerDownGate(STRUCTURE *psStruct);
/// ist das Gebaeude gerade abgeschaltet (PowerDown)? (nur fuer Anzeige/Abfragen)
bool isPoweredDown(const STRUCTURE *psStruct);
/// zeichnet Kabel und (im Anzeigemodus 4 "Stromnetz") Versorgungskreise; nach dem 3D-Bild aufrufen
void drawOverlay(const glm::mat4 &perspectiveViewMatrix);
/// beim Spielende / Ladevorgang
void reset();
}
