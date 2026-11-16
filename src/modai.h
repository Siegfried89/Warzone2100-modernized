// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 16.11. 19:28: "Alle Funktionen auch die KI wissen lassen. Alles."): Engine-Helfer, der die Computer-Spieler die
// Mod-Systeme benutzen laesst, die ihre Skripte (Nexus/Cobra/BoneCrusher) nicht kennen. Laeuft im Spielschritt (deterministisch, gameRand),
// nur fuer Computer-Spieler (nicht Scavenger, nicht Menschen). Stufe 1:
//  - Tankstelle bauen, sobald die KI Fahrzeuge mit Treibstoff hat und keine Tankstelle (Reparatureinrichtung)
//  - Befestigungsbau um die Basis (alle paar Minuten ein untaetiger Lkw, Groesse waechst mit der Spielzeit)
//  - Aussenposten an weit entfernten Oeltuermen
//  - Flieger in den Orbit schicken, wenn der Gegner dort etwas hat (Solarpaneele, Flieger), und dort angreifen
#pragma once

struct GameWorld;

namespace modai
{
void tick(GameWorld &world);
void reset();
}
