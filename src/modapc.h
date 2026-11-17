// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 17.11. 21:01): "WW2-Halbkettenfahrzeug als Militaertransporter, oben offen, mit Infanterie beladbar, 8 Mann" (Scav-Modelle).
//
// Schuetzenpanzer: Einheiten mit Rumpf "ScavAPC..." nehmen bis zu 8 Infanteristen auf. Einsteigen: Infanterie auswaehlen und auf den
// Transporter klicken (Befehl "begleiten") oder neben ihn schicken - wer ankommt, steigt ein und steht dann im offenen Laderaum (faehrt
// mit, kann weder treffen noch getroffen werden). Absteigen: Knopf "Unload" in der Rosette (Zweitbefehl, ueber das Netz abgeglichen),
// oder der Infanterist bekommt einen neuen Befehl; wird der Transporter zerstoert, springen alle ab. Spiellogik nur aus Spielzustand.
#pragma once

#include <cstdint>

struct DROID;
struct SIMPLE_OBJECT;

namespace modapc
{
/// Schuetzenpanzer (Rumpf-Kennung beginnt mit "ScavAPC")?
bool isApc(const DROID *psDroid);
/// sitzt dieses Objekt gerade in einem Schuetzenpanzer?
bool aboard(const SIMPLE_OBJECT *psObj);
/// Zahl der Insassen
int load(const DROID *psApc);
constexpr int CAPACITY = 8;
/// jeden Spielschritt aus der Spielschleife (nach den Einheiten)
void tick();
void reset();
}
