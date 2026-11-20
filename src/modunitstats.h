// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 17.11. 22:42): "Es waere gut, wenn die Einheiten so ein Menue mit ihren Werten haetten (senkrechte Leiste mit Sinnbild + Zahl
// je Wert). Standardmaessig ein, kann aber umgeschaltet werden."
// Werteleiste oben links fuer die gewaehlte eigene Einheit: Schaden, Schuss/Minute, Reichweite (Felder), Panzerung (kinetisch / thermisch),
// Lebenspunkte, Tempo, Gewicht, Kosten, Sichtweite (Felder), Abschuesse. Knopf darueber (WZ-Befehlsknopf mit Leuchtsinnbild) schaltet sie
// ein/aus; Standard: ein. Nur Anzeige.
#pragma once

struct IMAGEFILE;

namespace modunitstats
{
/// jedes Bild aus intRunWidgets
void tick();
void shutdown();
/// MOD (20.11.): die im Spiel gemeinsam genutzte Bildliste images/modfleets.img (einmal geladen, in shutdown() freigegeben) -
/// ein zweites Laden derselben Datei legte doppelte Texturseiten an ("tex page ... already exists")
IMAGEFILE *fleetImages();
}
