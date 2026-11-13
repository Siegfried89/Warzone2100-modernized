// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Energiewirtschaft nach Metal Fatigue (Plan L): Lavafelder, Hover-Trucks schuerfen, Solarpaneele, Energiebanken (Obergrenze).
//
// - Lavafeld (Feature "A0LavaField", 2x2 Felder): hat 4000 MJ Vorrat. Ein gewaehlter Hover-Truck faehrt per Klick an den Rand (gewoehnlicher, abgeglichener
//   Bewegungsbefehl); steht er dort untaetig, schuerft er 3 MJ je Sekunde aufs Konto, bis das Feld leer ist (dann verschwindet es). Jeder neue Befehl bricht ab.
// - Solarpaneel (Struktur "A0SolarPanel"): +1 MJ je Sekunde (unter Tage nur ein Viertel).
// - Energiebank (Struktur "A0EnergyBank"): +1000 MJ Obergrenze; Grundobergrenze 4000 MJ (nur fuer Spieler mit Fass-Wirtschaft, sonst WZ-ueblich unbegrenzt).
// - Verteilung: einmal zu Spielbeginn werden zufaellig (gameRand, auf allen Rechnern gleich) Lavafelder auf der Oberflaeche verteilt, wenn die Karte keine hat.
// Alles haengt nur von Spielzustand und gameRand ab, rechnet also auf allen Rechnern gleich.
#pragma once

#include "lib/framework/vector.h"

struct DROID;
struct FEATURE;
struct BASE_OBJECT;
struct GameWorld;

namespace modlava
{
/// ist das Objekt ein Lavafeld?
bool isLava(const BASE_OBJECT *psObj);
/// kann dieses Fahrzeug schuerfen? (Hover-Baufahrzeug, kein Bohrfahrzeug, keine Infanterie)
bool isMiner(const DROID *psDroid);
/// Befehl an alle gewaehlten Hover-Trucks des Spielers: an den Rand des Lavafelds fahren und dort schuerfen. true, wenn mindestens einer ihn bekam
bool orderSelected(unsigned player, FEATURE *psLava);
/// verbleibender Vorrat (MJ) oder -1
int remaining(const FEATURE *psLava);
/// Obergrenze des Spielers in MJ (0 = unbegrenzt)
int energyCap(unsigned player);
/// jeden Spielschritt aus der Spielschleife
void tick(GameWorld &world);
/// neues Spiel
void reset();
}
