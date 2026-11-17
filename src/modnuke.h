// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 17.11. 21:13/21:17): "Das Atomkraftwerk wird zur dritten Energiequelle: Uran - Atomkraftwerk. Tier 1 Oel, Tier 2 Solar,
// Tier 3 Atom. Atom gibt am meisten und am stetigsten. Das Uran kommt aus dem Bergbau im Untergrund." Die Kuehltuerme dampfen weiss,
// wenn es laeuft. (21:17) Keine Bau-Bedingung fuer Kuehltuerme: das Atomkraftwerk ist schon komplett (Reaktor, 2 Kuehltuerme, 2 Kuppeln).
//
// Atomkraftwerk "A0NuclearPlant" (2 x 2 Felder): verbraucht Uran (1 Einheit je 90 s), liefert solange gleichmaessig Energie (+5 je Sekunde,
// Tag und Nacht) und speist das Stromnetz als stetige Quelle (150 Leistung - mehr als jedes Oelkraftwerk; Oelkraftwerke gehen in Bereitschaft).
// Erstbeladung beim Bau: 2 Einheiten. Uranbergwerk "A0UraniumMine": foerdert nur im Untergrund (1 Einheit je 30 s, Lager hoechstens 20).
// Spiellogik nur aus Spielzeit und Spielzustand (mehrspielersicher); der Dampf ist reine Anzeige.
#pragma once

#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include <glm/fwd.hpp>

struct STRUCTURE;
struct STRUCTURE_STATS;

namespace modnuke
{
constexpr int GRID_CAPACITY = 150;     // Leistung im Stromnetz (Oelkraftwerk 40 + 20 je Modul, Solarpaneel 1)

bool isPlant(const STRUCTURE_STATS *psStats);
bool isMine(const STRUCTURE_STATS *psStats);
/// laeuft das Atomkraftwerk gerade (hat Uran)?
bool running(const STRUCTURE *psStruct);
/// Uranvorrat des Spielers (Einheiten)
int uranium(int player);
/// jeden Spielschritt aus der Spielschleife (rechnet einmal je Spielsekunde)
void tick();
/// weisser Dampf ueber den Kuehltuermen laufender Kraftwerke
void draw3D(const glm::mat4 &viewMatrix);
void reset();
}
