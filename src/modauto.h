// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 17.11. 15:46, Punkt 17): Knopf "Fully autonomous" je Einheit (Mod-Rosette -> Befehle). Die Einheiten sind auch so schon
// selbststaendig (Vorruecken, Deckung, Ausweichen, Munition, Rueckzug); voll autonom heisst zusaetzlich: steht die Einheit ohne Auftrag,
// sucht sie sich selbst den naechsten sichtbaren Gegner im Umkreis von 26 Feldern und rueckt kaempfend dorthin vor (Vorruecken-Befehl);
// bei schwerem Schaden faehrt sie selbst zur Reparatur (Rueckzug-Stufe wird beim Einschalten gesetzt). Ein eigener Befehl hat Vorrang
// (45 s Spieler-Kontrolle, modcontrol). Zweitbefehl DSO_FULLAUTO / DSS_FULLAUTO_SET (ueber das Netz abgeglichen), Logik nur aus
// Spielzustand und Spielzeit -> auf allen Rechnern gleich. Nicht fuer Transporter, Flieger (VTOL) und unbewaffnete Einheiten.
#pragma once

struct DROID;
struct GameWorld;

namespace modauto
{
bool supported(const DROID *psDroid);
bool isOn(const DROID *psDroid);
/// jeden Spielschritt (rechnet selbst alle 2 s)
void tick(GameWorld &world);
void reset();
}
