// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 28.11. 17:34 "in den Unit Designer einen kleinen Button "Test" - Mini-Testmap integrieren ... Die Einheit geht sofort im
// jetzigen Zustand auf die Testmap zum Ausprobieren. Ein paar Ziele zum Schiessen, ein paar Gegnervarianten"):
// Der Knopf "Test" im Einheiten-Designer startet ohne Umweg das Testgelaende (Karte Sk-Testgelaende-T1, Einstellungen und Aufstellung in
// modtest/unittest.json im Mod-Paket - erzeugt von unittestmap.py). Beim Spielstart stellt dieses Modul auf: die Testeinheit (der Entwurf
// genau so, wie er im Designer steht) am Startplatz, davor die Schiessbahn mit unbewaffneten Zielen, im Osten vier Gegnergruppen
// (Plunderer, Cyborgs, Panzer, befestigte Stellung). Spieler 1 hat keine KI: die Gegner bewachen ihr Lager; "Send enemies" schickt sie
// los. Zerstoerte Testeinheit kommt sofort wieder, Ziele und Gegner nach einer Weile (die Partie endet so nie von selbst).
// Ein kleines Fenster oben links: "New unit" (noch eine Testeinheit), "Send enemies", "Back to designer". Nach dem Spiel geht es
// zurueck in den Einheiten-Designer, der Entwurf bleibt stehen.
#pragma once

#include <string>

namespace modunittest
{
/// vom Einheiten-Designer: Entwurf (Vorlage als JSON-Text, wie das Spiel sie speichert) aufs Testgelaende schicken;
/// fleet = Flotte fuer die Rueckkehr. false + err, wenn Karte oder Aufstellung fehlen.
bool launch(const std::string &designJson, int fleet, std::string &err);
/// beim ersten Aufbau der Spieleinstellungen (Herausforderung): sofort starten? (einmalig)
bool consumeAutostart();
/// jedes Bild im Spiel (aus intRunWidgets): aufstellen, nachschieben, Fenster
void tick();
/// laeuft gerade das Testgelaende (aufgestellt)? Dann gibt es weder Sieg noch Niederlage (Spielregeln-Meldungen werden ignoriert)
bool active();
/// Spielende (interfaceShutdown)
void shutdown();
/// jedes Bild im Hauptmenue: true = gerade vom Testgelaende zurueck (dann in den Einheiten-Designer, Flotte returnFleet())
bool titleReturn();
int returnFleet();
}
