// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Forschung durch erbeutete Teile (nach Metal Fatigue). Im Mehrspieler auf Technikstufe 1 kennt jeder Spieler nur die Basis-Teile der
// Combots. Weitere Teile (Torsos, Beine, Arme) liegen als Beute auf dem Boden, wenn Kampfroboter abgeschlagen/zerstoert werden:
// aufsammeln -> das Teil gilt als "gefunden" -> jede Forschungsstation analysiert eines nach dem anderen -> das Teil ist im Designer frei.
// Bereits bekannte Teile bringen nur Schrott (ein Teil der Baukosten als Energie).
// Alles ist Spielschritt-Logik (gameRand, gameTime) und auf allen Rechnern gleich. Der Zustand "gefunden/verfuegbar" steht in den
// normalen Komponentenlisten und wird deshalb mit gespeichert; nur der Analysefortschritt beginnt nach dem Laden von vorn.
#pragma once

struct DROID;
struct FEATURE;

namespace modcombot { struct Part; }

namespace modsalvage
{
/// Beute-Forschung aktiv? (Mehrspieler, Technikstufe 1.) Sonst sind alle Teile von Anfang an frei und Beute gibt nur Energie.
bool researchMode();
/// Teil ist fuer den Spieler noch gesperrt (nur im Beute-Modus; sonst immer false)
bool isLocked(int player, const modcombot::Part *part);
/// Teil ist gefunden, wird aber noch nicht/gerade analysiert
bool isAnalysing(int player, const modcombot::Part *part);
/// Spieler hat die Beute aufgesammelt. true = behandelt (dann keine Oelfass-Energie), false = normales Fass-Verhalten
bool onPickup(const FEATURE *psFeature, int player);
/// Einheit wurde zerstoert: Kampfroboter lassen ein Teil zurueck, andere Kampfeinheiten selten
void onDroidDestroyed(const DROID *psDroid);
/// Artefakt aus einer zerstoerten Fabrik/Forschungsstation aufgesammelt: ein Teil, das der Besitzer kannte, wird "gefunden"
void onArtefact(int toPlayer, int fromPlayer);
/// jeden Spielschritt: Analyse in den Forschungsstationen
void tick();
void reset();
}
