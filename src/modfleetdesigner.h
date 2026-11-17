// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 17.11. 21:48): "Wo ist der Einheiteneditor?" - Knopf "Unit designer" im Flotten-Menue: startet im Hintergrund die erste
// Kampagnenkarte und oeffnet sofort den Design-Bildschirm mit allen Teilen (das Spiel filtert wie immer, was im Designer erscheint).
// Entwuerfe speichert man wie im Spiel mit dem Stern. Schliesst man den Designer, geht es zurueck ins Hauptmenue und direkt ins
// Flotten-Menue; die gewaehlte Flotte uebernimmt die gespeicherten Entwuerfe (oder eine erste Flotte entsteht).
#pragma once

namespace modfleetdesigner
{
/// vom Flotten-Menue: Designer-Sitzung starten
void request();
/// laeuft gerade eine Designer-Sitzung?
bool active();
/// jedes Bild im Spiel (aus intRunWidgets): Designer oeffnen, nach dem Schliessen ins Hauptmenue zurueck
void tick();
/// jedes Bild im Hauptmenue: true = gerade aus dem Designer zurueck (dann ins Flotten-Menue wechseln)
bool titleReturn();
/// das Flotten-Menue fragt nach dem Oeffnen: gerade aus dem Designer zurueck? (einmalig)
bool consumeFleetUpdate();
}
