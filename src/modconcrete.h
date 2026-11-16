// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 16.11. 23:30): Betonboden unter Flugkontrollturm, Kran, Fabriken, Kraftwerk und Raffinerie.
//
// Nur Anzeige: die Gelaendekacheln der Karte bleiben unveraendert (Gelaendeart, Fahrgeschwindigkeit, Spielstand). Beim Aufbau des
// Gelaendenetzes fragt terrain.cpp je Feld nach einer Ersatzkachel:
//   - Feld unter einem solchen Gebaeude -> volle Betonkachel (leicht gedreht je Feld, damit es nicht gekachelt aussieht),
//   - Nachbarfeld -> Randkachel, deren Beton vom gemeinsamen Rand aus nach wenigen Pixeln weich ausblendet ("geht ganz wenig und
//     weich auf das angrenzende Feld ueber"). Die 46 Randformen (Kanten + freie Ecken) liefert der Mod als Zusatzkacheln
//     (mf/concrete.txt: erste Kachelnummer und Anzahl). In der Grafikstufe "Klassisch" gibt es keine Mischung -> nur der volle Beton.
// Felder mit eigener Decal-Kachel (Strassen, Krater ...), Wasser und Klippen behalten ihr Aussehen.
#pragma once

struct WorldMapState;
struct STRUCTURE;

namespace modconcrete
{
/// Ersatz-Texturwert (Kachelnummer + Drehbits) fuer Feld (x, y) der Karte oder -1 (keiner). classic: Grafikstufe "Klassisch".
int overrideTexture(WorldMapState &m, int x, int y, bool classic);

/// Ist der Ersatzwert eine Randkachel (und nicht der volle Beton)?
bool isEdgeTile(int tex);

/// Bekommt dieses Gebaeude Beton?
bool wants(const STRUCTURE *psStruct);

/// Grundflaeche eines Gebaeudes wurde belegt oder frei (Bau begonnen / entfernt): Gelaende dort neu aufbauen.
void footprintChanged(const STRUCTURE *psStruct, int tileX, int tileY, int sizeX, int sizeY);

/// Spielbeginn: Kachelliste neu lesen
void reset();
}
