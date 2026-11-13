// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Wetter wie in Rome Total War (nur Anzeige, beeinflusst die Spielsimulation nicht und nutzt deshalb gameRand nicht):
//  - Gewitter: Starkregen, dunkle Wolken, viele Blitze (Bildschirmblitz + kurzes Aufhellen der Landschaft) und verzoegerter Donner
//  - Sandsturm: waagerechte Sandschlieren dicht ueber dem Boden, ockerfarbener Dunst und Himmel (Wuestenkarten)
//  - Schneesturm: dichter, schraeger Schnee, weisser Dunst (Schneekarten); Regen und Schnee wie bisher bei ruhigem Wetter
//  - ziehende Wolken: der Himmel dreht sich bei Unwetter schneller; passende umgefaerbte Himmelstexturen (page-436..439)
//  - Nacht: setzt eine Karte/ein Skript den Himmel "page-438-sky-night.png", wird die Landschaft entsprechend abgedunkelt
// Das Wetter wechselt zufaellig (ruhige Phasen und Unwetter mit weichen Uebergaengen). Karten mit Skript-Wetter (Regen/Schnee) bekommen
// zusaetzlich Gewitter bzw. Schneesturm. Wird die Wettereinstellung des Spielers ausgeschaltet, bleibt alles still.
#pragma once

namespace modweather
{
/// jedes Bild (Grafikzeit) aufrufen
void tick();
/// Vollbild-Faerbung und Blitz; in der 2D-Phase vor den Auswahlringen/Beschriftungen aufrufen
void drawOverlay();
/// setSkyBox() hat einen Himmel gesetzt (Karte/Skript): als Grundhimmel merken, Nacht erkennen
void onSkybox(const char *page);
/// welcher Himmel soll gespeichert werden? (Grundhimmel statt des gerade aufgelegten Unwetterhimmels)
const char *baseSkyPage(const char *current);
/// bei neuem Spiel / Laden
void reset();
}
