// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Wetter wie in Rome Total War (nur Anzeige, beeinflusst die Spielsimulation nicht und nutzt deshalb gameRand nicht):
//  - Gewitter: Starkregen, dunkle Wolken, viele Blitze (Bildschirmblitz + kurzes Aufhellen der Landschaft) und verzoegerter Donner
//  - Sandsturm: waagerechte Sandschlieren dicht ueber dem Boden, ockerfarbener Dunst und Himmel (Wuestenkarten)
//  - Schneesturm: dichter, schraeger Schnee, weisser Dunst (Schneekarten); Regen und Schnee wie bisher bei ruhigem Wetter
//  - ziehende Wolken: der Himmel dreht sich bei Unwetter schneller; passende umgefaerbte Himmelstexturen (page-436..439)
//  - Nacht: setzt eine Karte/ein Skript einen "...sky-night.png"-Himmel, wird die Landschaft entsprechend abgedunkelt; (20:36) dazu ein
//    Tag/Nacht-Wechsel (70 % Tag, 30 % Nacht, Sternenhimmel aus dem Nighttime-Mod)
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
/// (17.11. 20:36) Tag/Nacht-Wechsel: 20 Minuten Spielzeit je Tag, davon 30 % Nacht, Daemmerung 1 Minute. Reine Funktionen der Spielzeit
/// (auf allen Rechnern gleich). 0 = Tag, 1 = Nacht (Anzeige mit weicher Daemmerung)
float nightFactor(unsigned gameTimeMs);
/// ist es Nacht? (Spiellogik: Solarpaneele an der Oberflaeche liefern nachts nichts)
bool isNight(unsigned gameTimeMs);
/// wie dunkel ist es gerade sichtbar (0 Tag .. 1 Nacht, mit Daemmerung; Nachtkarten = 1) - fuer Nachtlichter
float visualNight();
}
