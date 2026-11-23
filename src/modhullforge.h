// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 23.11. 01:35): Rumpf-Baukasten - mehrere Rumpfe zu einem Verbundrumpf ("Ueber-Rumpf") zusammensetzen, Teile und
// Waffenplaetze verschieben/drehen (Durchdringen erlaubt), speichern (Benutzerordner, danach ueberall in der Rumpfliste).
// (Wunsch 23.11. 17:55) Ein reines Menue-Werkzeug: laeuft im Hauptmenue ohne Spiel (Flotten-Menue, Einheiten-Designer) und im Spiel
// (Design-Bildschirm); beide benutzen dieselben Bauplaene.
#pragma once

#include "lib/framework/frame.h"
#include "lib/widget/widget.h"

namespace modhullforge
{
/// oeffnen; inMenu = im Hauptmenue (kein Spiel - alle Rumpfe aus stats/body.json)
bool open(bool inMenu = false);
void close();
bool isOpen();
/// einmal pro Bild: im Spiel aus intRunWidgets(), im Hauptmenue aus der Titel-Oberflaeche (Tasten, Maus, Verfuegbarkeit)
void tick();
void shutdown();
/// Anzeigefunktion der Einstiegstaste im Design-Bildschirm
void displayEntryButton(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset);
}
