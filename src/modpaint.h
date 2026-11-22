// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: "Frei malen" - Schlachtfeld-Planung durch Zeichnen auf der Karte (Wunsch 14.11., Punkt 13).
//
// Acht Knoepfe (unten rechts): Bewegung (gruen), Sammelpunkt (orange), Angriff (rot), Verteidigung (blau), Artilleriebasis bauen (braun),
// Artillerie (hellgold), Flieger (gelb), Combot-Gebiet (violett). Nach dem Klick auf einen Knopf zieht man mit gedrueckter linker Maustaste
// eine Linie / einen Bogen / ein Gebiet auf die Karte; Rechtsklick oder Esc bricht ab. Beim Loslassen bekommen die passenden Einheiten
// ihre Auftraege: die gewaehlten Einheiten, oder - wenn nichts gewaehlt ist - die passenden eigenen Einheiten in der Naehe der Zeichnung.
//  - Bewegung: fahren den gezeichneten Weg ab.  Sammelpunkt: versammeln sich in der Mitte.  Angriff: ruecken entlang des Vektors vor
//    (greifen unterwegs an).  Verteidigung: Kampfeinheiten verteilen sich auf der Linie, Baufahrzeuge und Infanterie mit Spaten heben
//    entlang der Linie Graeben aus.  Artilleriebasis: Baufahrzeuge bauen in der Mitte 4 Artilleriestellungen, einen Sensorturm und
//    eine Mauer rundherum (die beste verfuegbare Ausstattung).  Artillerie: beschiesst das gezeichnete Gebiet (verteilt).
//    Flieger: patrouillieren ueber dem Gebiet.  Combots: patrouillieren selbststaendig durch das Gebiet.
// (17.11.) Dazu: Panzersperrenfeld (gruene Pyramiden, Schachbrettfeld aus Panzersperren), Minenfeld (Minen-Zeichen, verstreute Minen),
// Offensivlinie (rot, Querstriche zum Feind): mehr Kampfeinheiten in zwei Reihen, die Gegner verfolgen, nur wenige kurze Graeben + 2 MG-Posten.
// Alle Auftraege laufen als gewoehnliche Befehle (wie ein Mausklick) - also auch im Mehrspieler synchron. Die Zeichnung bleibt
// einige Sekunden sichtbar und verblasst dann (nur Anzeige).
#pragma once

#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include <glm/fwd.hpp>
#include <cstdint>
#include <functional>
#include <vector>

namespace modpaint
{
/// einmal pro Bild aus intRunWidgets() (legt das Feld an / zeigt / versteckt es)
void tick();
/// aus processMouseClickInput(): true = die Maus gehoert gerade dem Zeichnen (normale Klickverarbeitung ueberspringen)
bool processMouse(bool overUi, const Vector2i &worldPos);
/// Zeichnungen im 3D-Bild einblenden
void drawOverlay(const glm::mat4 &perspectiveViewMatrix);
/// (17.11.) Zeichnungen auf der grossen Karte (modbigmap): toScreen rechnet Weltpunkte in Bildschirmpunkte um
void drawOnMap(const std::function<bool(const Vector2i &, Vector2i &)> &toScreen);
/// Beschriftung am Mauszeiger, solange ein Zeichenmodus aktiv ist (sonst nullptr)
const char *activeLabel();
/// (17.11.) Zeichenmodus nach Beschriftung waehlen (z. B. "Salvage" aus der Mod-Rosette)
void selectMode(const char *label);
/// (18.11.) Oberkante des Stapels unten rechts ohne Minikarte (Schlachtplan offen: seine Oberkante, sonst Bildschirmunterkante)
int stackTop();
/// beim Beenden der Oberflaeche
void shutdown();
/// (22.11.) Angriffsplaner im Spielstand (Zonen, Leuchtfeuer, Brueckenkoepfe, Vektoren, Einheiten, Transporter)
void savePlan(std::vector<int64_t> &out);
void loadPlan(const std::vector<int64_t> &in);
/// (22.11.) hat der Spieler einen Angriffsplan angelegt? (Tutorial)
bool planHasElements();
}
