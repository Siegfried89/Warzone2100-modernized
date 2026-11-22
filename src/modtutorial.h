// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 22.11. 18:27 "Tutorial"): gefuehrte Einfuehrung in die Mod-Systeme. Laeuft in der Herausforderung "Tutorial: Warzone
// modernized" (Karte Wuestenland, challenges/tutorial_modernized.json im Mod-Paket): ein Fenster oben links zeigt den aktuellen Schritt
// mit kurzer Anleitung; ist die Aufgabe im Spiel erfuellt (aus dem Spielzustand geprueft), hakt es ab und geht zum naechsten.
// Schritte: Auswaehlen/Kamera, Bohrturm, Raffinerie, Kraftwerk + Umspannwerk, Produktion, Graben/Stellung, Schlachtplan,
// Tunnel, Untergrund, Gleise + Lokschuppen, Bruecke. Bereits erledigte Schritte bleiben erledigt; "Weiter" ueberspringt.
#pragma once

namespace modtutorial
{
/// einmal pro Bild (legt das Fenster an, prueft die Schritte)
void tick();
void shutdown();
}
