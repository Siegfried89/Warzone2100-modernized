// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 17.11. 18:48): "Bei mittlerer bis grosser Zoomstufe kann man in Zukunft umschalten zwischen normalen Sprites und taktischen
// Symbolen (wie in Supreme Commander). Die taktischen Symbole sollen sich an der Realitaet orientieren, aber cool aussehen."
//
// Symbole nach dem militaerischen Vorbild (APP-6 / MIL-STD-2525, wie auf den Beispielbildern):
//  - Rahmen nach Zugehoerigkeit: eigene/verbuendete Einheiten hellblaues Rechteck, Gegner rote Raute; Luftfahrzeuge Kuppel (Gegner:
//    Raute), Schiffe Kreis; unten ein schmaler Streifen in der Spielerfarbe, darunter der Zustandsbalken (gruen/gelb/rot).
//  - Zeichen nach Einheitenart: Infanterie (X), Panzer (Kette = Oval), Radfahrzeug (Oval mit Raedern), Artillerie (Punkt),
//    Flugabwehr (Kuppel), Panzerabwehr (Dach), Aufklaerung/Sensor (Schraegstrich), Pioniere (Bruecke), Instandsetzung (Schraubenschluessel),
//    Gefechtsstand (Fahne), Transport (Rad), Combots (Oval + X = mechanisiert), Flugzeug (Flugzeugsymbol), Hubschrauber (Rotor).
//  - Groesse ueber dem Rahmen: Punkte nach Rumpfgroesse (leicht 1, mittel 2, schwer 3).
// Umschalten ueber den Knopf "Symbols" an der Minikarte. Eingeschaltet ersetzen die Symbole ab mittlerer Zoomstufe die Modelle/Sprites.
// (19.11. 20:27) Solange die Alt-Taste gedrueckt ist, sind die Symbole immer an (auch nah herangezoomt).
// Nur Anzeige.
#pragma once

#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include <glm/fwd.hpp>

struct DROID;
struct STRUCTURE;

namespace modtactical
{
/// ab dieser Kamera-Entfernung erscheinen die Symbole (mittlere Zoomstufe)
constexpr float SYMBOL_DIST = 3000.f;
bool enabled();
void setEnabled(bool on);
void toggle();
/// eingeschaltet und weit genug herausgezoomt: Modelle durch Symbole ersetzen
bool active();
/// Symbole aller sichtbaren Einheiten ueber das 3D-Bild zeichnen (wenn aktiv)
void drawOverlay(const glm::mat4 &perspectiveViewMatrix);
/// ein Einheiten-Symbol an Bildschirmstelle (cx, cy) zeichnen - auch fuer die grosse Karte (modbigmap); scale 1 = normale Groesse
void drawDroidSymbol(const DROID *psDroid, float cx, float cy, float scale);
/// kleines Bauwerks-Symbol (Rahmen + Zeichen) fuer die grosse Karte
void drawStructureSymbol(const STRUCTURE *psStruct, float cx, float cy, float scale);
}
