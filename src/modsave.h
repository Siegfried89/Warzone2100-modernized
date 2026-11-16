// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 16.11. 19:34, Q95): Mod-Zustand im Spielstand. Krater und Schuetzenloecher, Tankfuellungen und die Oelfaesser
// (Oeltuerme, Lager, Raffinerien, Kraftwerke, Tankstellen) werden beim Speichern in "modstate.json" im Spielstand-Ordner
// abgelegt und nach dem Laden wieder eingesetzt. Aeltere Spielstaende ohne diese Datei laden wie bisher.
#pragma once

#include <string>

namespace modsave
{
/// beim Speichern: folder = Spielstand-Ordner (ohne abschliessenden Schraegstrich)
void write(const std::string &folder);
/// beim Laden: Ordner merken (der Zustand wird eingesetzt, wenn die Welt fertig aufgebaut ist)
void setPending(const std::string &folder);
/// nach dem Laden (Ende der Spielinitialisierung)
void applyPending();
}
