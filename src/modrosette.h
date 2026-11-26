// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 17.11. 14:14): Mod-Rosette im WZ-Stil.
//
// "ALLE neuen Befehlsbuttons in unserem Mod sollen im WZ Stil sein. Auch die Sensorbuttons links unten ueber der Rosette. Am besten eine
// neue Rosette im WZ Stil darueber mit: Formationen, Feuermodus, und was dir sonst noch einfaellt, wo wir unsere ganzen Befehle besser
// und schoener verstecken koennen. Aber diese Zonen zeichnen sollte weiterhin so einfach und schnell mit einem Klick moeglich sein.
// Vielleicht ist rechts ueber der Map der richtige Ort unsere neue ModRosette zu machen. Und bitte so schoen wie die originale WZ."
//
// Rechts ueber der Minikarte: sieben Sechseck-Knoepfe wie die WZ-Rosette (Bilder images/modrosette.img, Mod-Daten rosette.py):
//   oben          Schlachtplan (Zonen zeichnen) - blendet die Schlachtplan-Leiste ein/aus; sie bleibt offen, Zeichnen = ein Klick
//   oben rechts   Feuermodus (Feuer frei / Erwidern / Feuer halten, wie im WZ-Befehlsmenue) fuer die Auswahl
//   unten rechts  Reichweitenringe (Sensor/Waffen/beides/Stromnetz) - ein Klick schaltet weiter (vorher Knopf links unten)
//   unten         Ebenen (Orbit/Oberflaeche/Untergrund; vorher Knoepfe oben rechts)
//   unten links   Infanterie & Befehle (Deckung, Ausschwaermen, Bergen, Bauen, Artillerie-Modus)
//   oben links    Formationen (Ring, Linie, Keil, Kolonne, Artillerie-Staffel; vorher Knoepfe links unten)
//   Mitte         alle Leisten zu / wieder auf
// Ein Klick auf einen Knopf oeffnet links daneben eine Leiste mit Befehlsknoepfen im Stil der WZ-Befehlsknoepfe (mit Tooltip).
#pragma once

#include <memory>

class WIDGET;

namespace modrosette
{
/// (Wunsch 26.11.) die Mod-Befehle (frueher eigenes Fenster "Orders": Deckung, Ausschwaermen, Bergen, Infanterie baut, voll autonom,
/// Minen raeumen, Absitzen, Aussteigen) als Knopfzeile im WZ-Befehlsfenster - nur die, die zur Auswahl passen. Liefert die Hoehe.
int addOrderButtons(const std::shared_ptr<WIDGET> &form, int x, int y, int width);
/// jeden Frame aus intRunWidgets (wie modpaint::tick)
void tick();
void shutdown();
/// Schlachtplan-Leiste gewuenscht? (modpaint fragt das ab)
bool planOpen();
/// gibt es die Rosette (dann zeigt modlayer keine eigenen Knoepfe mehr oben rechts)?
bool active();
}
