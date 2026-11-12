// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Formationsbefehle (Ring, Linie, Keil, Kolonne, Artillerie-Staffel)
#pragma once

namespace modformation
{
enum Type { RING = 0, LINE, WEDGE, COLUMN, ARTY, COUNT };

/// Ordnet die ausgewaehlten Einheiten des lokalen Spielers in der Formation an (Bewegungsbefehle).
void apply(Type type);
const char *tip(Type type);
}
