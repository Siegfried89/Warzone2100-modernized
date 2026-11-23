// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 23.11. 01:35): Rumpf-Baukasten im Design-Bildschirm - mehrere Rumpfe zu einem Verbundrumpf ("Ueber-Rumpf")
// zusammensetzen, Teile und Waffenplaetze in kleinen Schritten verschieben/drehen (Durchdringen erlaubt), speichern (Benutzerordner,
// danach ueberall in der Rumpfliste). Auch im Flotten-Editor (der benutzt denselben Design-Bildschirm).
#pragma once

#include "lib/framework/frame.h"
#include "lib/widget/widget.h"

namespace modhullforge
{
bool open();
void close();
bool isOpen();
/// einmal pro Bild aus intRunWidgets() (Tasten, Maus-Drehen, Verfuegbarkeit der Verbundrumpfe)
void tick();
void shutdown();
/// Anzeigefunktion der Einstiegstaste im Design-Bildschirm
void displayEntryButton(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset);
}
