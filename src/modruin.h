// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 22.11. 20:50): "Fuer die meisten Scavenger-Gebaeude (und auch andere Fraktionen) nach der Zerstoerung das Modell 171
// 'Ruined Factory' erscheinen lassen (das ein paar Minuten nach der Explosion stark schwarz raucht). Das kann dann abgerissen werden."
//  - Zerstoerte Basisgebaeude (Kommandozentrale, Fabriken, Forschung, Kraftwerk, Reparatur, Relais, Uplink, Lasersatellit und groessere
//    Mod-Gebaeude ab 2 Feldern) hinterlassen das Bauwerk "A0BuildingRuin" (Vanilla-Modell der Fabrikruine blfactrd, 2x1, im Mod-Paket).
//    Nicht: Mauern, Tuerme, Bunker, Stellungen, Bohrtuerme, Module, Gleise, Bruecken, Siedlungshaeuser.
//  - Die Ruine gehoert dem frueheren Besitzer (Mehrspieler) bzw. in der Kampagne dem Spieler - so kann sie mit Baufahrzeugen abgerissen
//    werden und haelt keine Kampagnen-Basis "am Leben".
//  - Sie raucht nach der Explosion 4 Minuten lang stark schwarz (dunkle Rauchwolken, nur Anzeige).
// Spielzustand: angelegt im naechsten Spielschritt (Warteschlange - destroyStruct laeuft oft mitten in einer Objekt-Schleife), feste
// Reihenfolge, keine Zufallszahlen - auf allen Rechnern gleich.
#pragma once

struct STRUCTURE;

namespace modruin
{
/// structure.cpp destroyStruct, VOR removeStruct: merkt sich, ob und wo eine Ruine entsteht
void onDestroyed(const STRUCTURE *psDel);
/// jeden Spielschritt: Ruinen anlegen, Rauch
void tick();
void reset();
/// ist das eine Ruine?
bool isRuin(const STRUCTURE *s);
}
