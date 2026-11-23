// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 23.11. 01:35): Verbundrumpfe ("Ueber-Rumpfe") aus dem Rumpf-Baukasten.
// Ein Verbundrumpf besteht aus mehreren vorhandenen Rumpfen (Teile), jeder frei verschoben und gedreht (Durchdringen erlaubt), und frei
// gesetzten Waffenplaetzen. Er wird als ganz normaler Rumpf (BODY_STATS) angemeldet: Werte zusammengerechnet, Modell aus den Teilen
// erzeugt (PIE 4 mit eigener Textur je Ebene), Laufwerke wie beim ersten Teil (Fahrgestell, folgen dessen Lage).
// Bauplaene: Vorlagen im Mod-Paket (composites/*.json) und eigene im Benutzerordner (userdata/composites/*.json, haben Vorrang).
#pragma once

#include "lib/framework/frame.h"
#include "lib/framework/vector.h"
#include "lib/framework/wzstring.h"
#include <glm/mat4x4.hpp>
#include <functional>
#include <string>
#include <vector>

struct COMPONENT_STATS;
struct BODY_STATS;
struct iIMDShape;

namespace modcompbody
{

/// Waffenplaetze je Verbundrumpf (Wunsch 23.11. 02:16: bis 30 - Turm-Plaetze-Leiste im Design-Bildschirm, MAX_WEAPONS)
constexpr unsigned MAX_SLOTS = 30;

struct Part
{
	std::string body;                 ///< ID des Teil-Rumpfes
	glm::vec3 pos = glm::vec3(0.f);    ///< Lage (x, y = oben, z = hinten; vorn ist -z)
	glm::vec3 rot = glm::vec3(0.f);    ///< Drehung in Grad: Gieren (um y), Nicken (um x), Rollen (um z)
	float scale = 1.f;
	int flip = 0;                      ///< Spiegelung der Teil-Geometrie: Bit 0 = x, Bit 1 = y, Bit 2 = z (Wunsch 23.11. 02:16 "Flip")
};

struct Mount
{
	glm::vec3 pos = glm::vec3(0.f);    ///< Waffenplatz (Modellkoordinaten)
};

struct Def
{
	std::string id;                   ///< Rumpf-ID ("CompBody_...")
	std::string name;                 ///< Anzeigename
	std::vector<Part> parts;          ///< Teil 0 = Fahrgestell (Laufwerke, Klasse)
	std::vector<Mount> mounts;        ///< Waffenplaetze (bis MAX_SLOTS) - jeder ein Turm-Platz fuer eine Waffe oder einen Systemturm
	bool preset = false;              ///< Vorlage aus dem Mod-Paket (noch nicht im Benutzerordner)
	bool deleted = false;             ///< geloescht: bleibt fuer alte Spielstaende angemeldet, aber nicht mehr baubar
};

struct Totals
{
	unsigned hp = 0, armour = 0, thermal = 0, weight = 0, power = 0, cost = 0, buildPoints = 0;
	int size = 0;                     ///< BODY_SIZE
	unsigned slots = 0;
};

// ---------------------------------------------------------------- Stats-Laden (stats.cpp)
/// Bauplaene von Platte lesen und die Zahl der zusaetzlichen Rumpf-Plaetze liefern (gespeicherte + freie fuer neue Entwuerfe)
size_t prepare();
/// nach dem Laden aller normalen Rumpfe (samt Laufwerk-Modellen): die Plaetze first .. first+count-1 fuellen.
/// lookup(stats, alteID) traegt die (neue) ID in die Such-Tabellen der Stats ein.
void registerAll(size_t first, size_t count, const std::function<void(COMPONENT_STATS *, const WzString &)> &lookup);
/// beim Entladen der Stats
void shutdown();

// ---------------------------------------------------------------- Laufzeit
bool isComposite(size_t bodyIdx);
/// Lage des Fahrgestells (Teil 0) fuer die Laufwerk-Modelle; nullptr = keine Verschiebung
const glm::mat4 *propOffset(size_t bodyIdx);
/// Verfuegbarkeit fuer einen Spieler: baubar, wenn alle Teil-Rumpfe erforscht sind (nicht in Netzwerkspielen)
void refreshAvailability(unsigned player);

// ---------------------------------------------------------------- Rumpf-Baukasten
const std::vector<Def> &defs();
const Def *find(const std::string &id);
glm::mat4 partMatrix(const Part &p, bool withFlip = true);
/// gespiegelte Fassung eines Rumpf-Modells fuer die Vorschau im Baukasten (Umlaufsinn richtig); nullptr = keine
const iIMDShape *flippedShape(const std::string &bodyId, int flipMask);
Totals totals(const Def &d);
/// Rumpf zu einer ID (nullptr = unbekannt)
const BODY_STATS *bodyById(const std::string &id);
/// speichern (Datei im Benutzerordner) und sofort als Rumpf anmelden bzw. aktualisieren; d.id wird bei neuen Entwuerfen vergeben
bool save(Def &d, std::string &err);
/// loeschen (Datei weg, Rumpf nicht mehr baubar)
bool remove(const std::string &id, std::string &err);
/// Standard-Waffenplaetze aus den Anschluessen des Fahrgestells
std::vector<Mount> defaultMounts(const Part &chassis);

}
