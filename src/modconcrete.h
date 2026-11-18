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

/// (18.11.) Gebaeude auf Beton zeichnen ihre harte Fundament-Platte (baseModel) nicht - der Beton mit weichem Rand ersetzt sie
bool replacesBasePlate(const STRUCTURE *psStruct);

/// MOD (17.11., "Die Betontexturen laufen nirgends weich aus"): Grafikstufe "Klassisch" - Decal-Nummer fuer den Gelaende-Shader.
/// Ein Feld neben Beton (Karten-Beton der Arizona-Kacheln 22/77 oder Beton unter Gebaeuden) bekommt eine Kennung, mit der der Shader
/// den Beton vom gemeinsamen Rand aus weich und unregelmaessig auf das Feld auslaufen laesst; reine Wasserfelder bekommen eine Kennung
/// fuer eine Wasserflaeche im Gelaendedurchgang (vorher blieben sie schwarz). Kodierung (alles ganzzahlig, siehe Shader modcLayer):
///   decal = 1048576 * k + 4096 * Randmaske + eigene Kachel (4095 = keine), k = 1: Wasser, k >= 2: Beton (k - 2 = Feinraster + 2 * Betonkachel)
/// decalNo = bisherige Decal-Nummer des Feldes (-1 = keine), waterOnly = reines Wasserfeld (wuerde sonst nicht gezeichnet).
int classicDecal(WorldMapState &m, int x, int y, int decalNo, bool waterOnly);
}
