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

#include "lib/framework/vector.h"

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
/// (20.11.) Wegpunkt fuer ein Feld in/an einer Stellung: Mitte des Stellungsblocks bzw. Feldmitte (sides: copySides-Daten)
Vector2i pathPoint(const std::vector<uint8_t> &sides, int width, int tileX, int tileY);

/// ist das ein Stellungsbauwerk (Graben/Grube aus mf/fort.txt)?
bool isFort(const STRUCTURE *psStructure);
/// Modell mit den offenen Seiten fuer die Anzeige (ohne Drehung zu zeichnen); nullptr = normales Modell
const iIMDShape *displayModel(const STRUCTURE *psStructure);

/// (17.11. 14:14) tiefe Stellung (Graben/Grube "tief")?
bool isDeep(const STRUCTURE *psStructure);
/// Anzeigehoehe des Modells: tiefe Stellungen stehen auf der Original-Bodenhoehe (Mittel der vier Ecken ohne Absenkung) -
/// die Waelle liegen ueber dem normalen Boden, die Sohle darunter. Sonst fallback (pos.z).
int displayHeight(const STRUCTURE *psStructure, int fallback);

/// Kettenfahrzeug mit mittlerem oder schwerem Rumpf: darf Stellungen ueberfahren
bool heavyCrosser(const DROID *psDroid);
/// steht auf dem Feld eine Stellung (beliebiger Spieler)?
bool fortTile(int tileX, int tileY);

/// (Wunsch 18.11. 13:17 "bei tiefen Graeben versinken die Infanteristen ... die Sandsackwaende/Graeben/Stellungen haben immer noch keine
/// Kollision - genau ueberpruefen"): Infanterie bewegt sich in einer Stellung nur im freien Innenraum (Grube bzw. Grabengang) und in den
/// Gaengen zu den offenen Seiten - nicht mehr durch oder in die Sandsack-/Holzwaende. newPos wird angepasst; true = geaendert.
bool clampInfantry(const DROID *psDroid, int oldX, int oldY, int &newX, int &newY);
/// (Wunsch 20.11. 15:43/15:57 "... rennen stur gegen die Wand ohne nach der Oeffnung zu suchen" / "ohne Verzoegerung"): nach jedem
/// Festhalten durch clampInfantry melden (alte Position) - kommt die Einheit dabei ihrem Wegpunkt nicht naeher, sucht tick() ihr sofort
/// einen neuen Weg (hoechstens einmal je Sekunde; Spiellogik, auf allen Rechnern gleich)
void noteClamped(const DROID *psDroid, int oldX, int oldY);
/// (20.11.) nach der Bewegung aller Einheiten im Spielschritt (loop.cpp): festgehaltene Einheiten bekommen sofort einen neuen Weg
void afterDroidUpdates();
}
