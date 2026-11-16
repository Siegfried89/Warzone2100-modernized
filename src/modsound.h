// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 16.11. 20:08): Metal-Fatigue-Klaenge an den richtigen Stellen. Die Zuordnung kommt aus den MF-Daten (objinfo.tbd ->
// sound.tbd, siehe MF-Analyse.md "Sounds"): Gebaeude mit MF-Modell spielen beim Anwaehlen ihren MF-Auswahlklang und beim
// Zerstoeren den MF-Einsturz. Die Liste schreibt das Mod-Skript mfstructsnd.py nach audio/mf/mf_struct.txt
// (Zeilen "<Bauwerk-Kennung> <Auswahl.ogg> <Zerstoerung.ogg>", "-" = keiner). Reine Anzeige/Ton, aendert das Spiel nicht.
#pragma once

struct STRUCTURE;

namespace modsound
{
/// eigenes Bauwerk wurde vom Spieler angewaehlt
void structureSelected(const STRUCTURE *psStructure);
/// Bauwerk wird zerstoert (Ton am Ort)
void structureDestroyed(const STRUCTURE *psStructure);
}
