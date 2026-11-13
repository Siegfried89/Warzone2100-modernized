// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Bohrfahrzeug (MF: CDrillTruck). Im Untergrund ist alles massiver Fels; ein Bohrfahrzeug mit Konstruktionsteil "DrillRig"
// bekommt einen normalen Bewegungsbefehl und bohrt sich geradeaus zum Ziel (je Feld eine Bohrzeit), die Gaenge sind ein Feld breit.
#pragma once

#include "lib/framework/vector.h"

struct DROID;
struct DroidOrder;
struct GameWorld;

namespace modlayerdig
{
/// ist es ein Bohrfahrzeug?
bool isDrill(const DROID *psDroid);
/// aus orderDroidBase: true, wenn der Befehl hier uebernommen wurde (Bohrfahrzeug im Untergrund mit Bewegungsbefehl); sonst wird
/// ein altes Bohrziel der Einheit verworfen und false geliefert
bool onOrder(DROID *psDroid, const DroidOrder *psOrder);
/// jeden Spielschritt aus der Spielschleife
void tick(GameWorld &world);
/// neues Spiel
void reset();
}
