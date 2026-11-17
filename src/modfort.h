// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 17.11. 13:13): Infanterie-Stellungen (Graeben, Gruben) als zusammenhaengendes Netz.
//
// "Die Stellungen haben noch offene Stellen und sind nicht richtig verbunden. Ausserdem braucht es eine Oeffnung zum Inneren. Es ist
// bloed, wenn sie durch die Sandsackwand von aussen hindurch und hinein laufen. Ausserdem fuer Fahrzeuge nicht befahrbar - ausser
// schwere Panzer mit Ketten und mindestens mittlerem Rumpf."
//
// - Je Feld einer Stellung (Graben oder Grube, Liste in mf/fort.txt) bestimmt das Modul die offenen Seiten: zu jeder angrenzenden
//   eigenen Stellung (Graben an Graben, Graben an Grube) und Oeffnungen zum Inneren (zur eigenen Basis hin: Hauptquartier, Fabriken,
//   Kraftwerke, Forschung): je zusammenhaengendem Stellungsnetz am basisnahen Feld, bei langen Linien etwa alle fuenf Felder.
// - Gezeichnet wird das passende Modell <kuerzel>_m<maske>.pie (Gaenge zu den offenen Seiten, sonst Sandsackwand) - die Stellungen
//   schliessen lueckenlos aneinander.
// - Wegsuche der Infanterie: ein Stellungsfeld wird nur ueber eine offene Seite betreten und verlassen (keine Abkuerzung durch die
//   Sandsackwand, nicht schraeg).
// - Fahrzeuge kommen nicht hindurch - nur Kettenfahrzeuge mit mittlerem oder schwerem Rumpf ueberfahren Stellungen.
// Die Netz-Daten entstehen im Spielschritt aus dem Spielzustand (auf allen Rechnern gleich) und werden alle 2 Spielsekunden erneuert.
#pragma once

#include <cstdint>
#include <vector>

struct DROID;
struct STRUCTURE;
struct iIMDShape;

namespace modfort
{
/// jeden Spielschritt (loop.cpp)
void tick();
void reset();

/// Netzdaten des Feldes (Oberflaeche): 0 = keine Stellung, sonst 0x10 | offene Seiten (1 Nord, 2 Ost, 4 Sued, 8 West)
uint8_t sides(int tileX, int tileY);
/// Kopie fuer eine Wegsuche (Breite x Hoehe der aktiven Karte); leer, wenn es keine Stellungen gibt oder eine andere Ebene aktiv ist
void copySides(std::vector<uint8_t> &out);
/// Darf die Wegsuche zwischen zwei benachbarten Feldern wechseln? (sides: copySides-Daten)
bool edgeOpen(const std::vector<uint8_t> &sides, int width, int x0, int y0, int x1, int y1);

/// ist das ein Stellungsbauwerk (Graben/Grube aus mf/fort.txt)?
bool isFort(const STRUCTURE *psStructure);
/// Modell mit den offenen Seiten fuer die Anzeige (ohne Drehung zu zeichnen); nullptr = normales Modell
const iIMDShape *displayModel(const STRUCTURE *psStructure);

/// Kettenfahrzeug mit mittlerem oder schwerem Rumpf: darf Stellungen ueberfahren
bool heavyCrosser(const DROID *psDroid);
/// steht auf dem Feld eine Stellung (beliebiger Spieler)?
bool fortTile(int tileX, int tileY);
}
