// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Gelaende verformen - Krater durch Beschuss (3 Groessen), Deckung fuer Infanterie in Kratern, Zuschaufeln durch Baufahrzeuge.
//
// Das Gelaende besteht aus Eckpunkten (Hoehe je Kachelecke). Ein Krater senkt die Eckpunkte im Umkreis schalenfoermig ab und hebt
// einen kleinen Wall am Rand an. Fuer jeden veraenderten Eckpunkt merkt sich das Modul die urspruengliche Hoehe, damit Bagger die
// Mulde wieder auffuellen koennen und sich mehrere Einschlaege zu groesseren Mulden verbinden (die Tiefe ist nach unten begrenzt).
// Alles laeuft im Spielschritt und ist deterministisch (kein Zufall, keine Anzeige-Abhaengigkeit), gilt also auf allen Rechnern gleich.
// Aenderungen, die von aussen kommen (Bauwerksfundamente, Gruben), werden erkannt: stimmt die Hoehe nicht mehr mit der vom Modul
// gesetzten ueberein, wird der Eckpunkt vergessen und nie "zurueckgesetzt".
#pragma once

#include "lib/framework/vector.h"

struct BASE_OBJECT;
struct DROID;
struct PROJECTILE;
struct WorldMapState;

namespace modterrain
{
/// Krater der Groesse 1 (leicht) .. 3 (schwer) an einem Weltpunkt der aktiven Karte. false, wenn dort kein Krater moeglich ist
/// (Wasser, Klippe, Bauwerk, Kartenrand).
bool addCrater(int worldX, int worldY, int size);

/// Beim Geschoss-Einschlag aufrufen: legt je nach Waffe (Kanone, Moerser, Haubitze, Bombe) und Treffpunkt (nur Boden) einen Krater an.
void onProjectileImpact(const PROJECTILE *psProj);

/// Tiefe (Weltmasseinheiten) der Mulde auf dieser Kachel: Mittel der vier Eckpunkte gegenueber der Ausgangshoehe; 0 = eben/erhoeht.
int depthAtTile(int tileX, int tileY);
/// Bietet die Mulde auf diesem Feld Deckung (fuer Infanterie)?
bool tileGivesCover(int tileX, int tileY);
/// Steht die Einheit (Infanterie) in einer Mulde mit Deckung?
bool droidInCover(const DROID *psDroid);

/// Mulde um die Kachel ein Stueck auffuellen (amount je Eckpunkt in Hoeheneinheiten). Gibt die verbleibende Tiefe zurueck.
int fillTile(int tileX, int tileY, int amount);
/// Kachel (Mitte) der naechsten Mulde im Umkreis (Kacheln) um einen Weltpunkt mit Mindesttiefe; false, wenn keine.
bool findDent(const Vector2i &worldCenter, int radiusTiles, int minDepth, Vector2i &outTile);

/// Eine Kachel absenken (Graben) und optional links/rechts davon einen kleinen Erdwall anheben (dir: 0..65535, Laengsrichtung
/// des Grabens). Wird vom Befestigungsbau fuer reine Erdgraeben verwendet.
bool dig(int tileX, int tileY, int depth, int bermHeight, unsigned dirAngle);

/// bei neuem Spiel
void reset();
}
