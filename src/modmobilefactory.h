// MOD (18.11. 16:24 "den Fatboy aus Supreme Commander nachbauen ... und eine Fabrikenfunktion wie in SupCom hinzufuegen"):
// fahrende Fabrik. Ein Rumpf mit "mobileFactory": "<Fabrik-Kern>" (body.json) traegt einen unsichtbaren Fabrik-Kern
// (structure.json, Typ FACTORY, "mobileFactoryCore": 1). Der Kern ist eine ganz normale Fabrik - Bauliste, Warteschlange,
// Endlosschleife, Sammelpunkt mit Nummer, Strom, Forschungs-Upgrades, Speichern/Laden, Netzwerk -, liegt aber auf keinem
// Feld, blockiert nichts, wird nicht gezeichnet, ist kein Ziel und sieht nichts. modmobilefactory haelt ihn am Heck des
// Fahrzeugs: neue Einheiten rollen hinten heraus, der Sammelpunkt faehrt mit (bis der Spieler ihn selbst versetzt).
// Faellt das Fahrzeug, verschwindet der Kern (laufende Produktion wird erstattet wie bei jeder Fabrik).
#pragma once

#include <cstdint>

struct DROID;
struct STRUCTURE;

namespace modmobilefactory
{
bool isHost(const DROID *psDroid);                 ///< Rumpf mit Fabrik-Kern?
STRUCTURE *coreOf(const DROID *psDroid);           ///< Fabrik-Kern dieses Fahrzeugs (oder nullptr)
void update();                                     ///< je Spieltakt (auf allen Rechnern gleich): Kerne anlegen, mitfuehren, entfernen
void onDroidClicked(DROID *psDroid);               ///< Bedienung: Klick auf den eigenen Fatboy oeffnet seine Bauliste
void reset();                                      ///< neues Spiel / Laden
}
