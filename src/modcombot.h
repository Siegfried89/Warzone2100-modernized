// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Modularer Combot-Zusammenbau zur Laufzeit (Torso = Body, Beine = Propulsion, Arme = Waffen).
// Die Teile und die Original-MF-Animationen kommen aus mfmodular/parts.json und mfmodular/anims.json.
#pragma once

#include <functional>
#include <string>
#include <vector>
#include "lib/framework/vector.h"
#include <glm/mat4x4.hpp>

#include "lib/ivis_opengl/pietypes.h"

struct DROID;
struct iIMDShape;
struct BODY_STATS;
struct PROPULSION_STATS;

namespace modcombot
{

enum Kind { KIND_TORSO = 0, KIND_LEG = 1, KIND_ARM = 2 };

struct PartStats
{
	int hp = 0, ak = 0, ah = 0, spd = 0, dmg = 0, rng = 0, blk = 0;
	bool melee = false;
};

/// (20.11.) MF-Skelett eines Einzelteils: Knochenplatz (0-27, 31 = starr am Elternteil), Elternteil, Versatz zum Elternteil
/// (MF-Einheiten) und Bauversatz (Anbauteil ist im Rahmen eines anderen Knochens gebaut)
struct PieceBone
{
	int slot = 31;
	int parent = -1;
	glm::vec3 off = glm::vec3(0.f);
	glm::vec3 rel = glm::vec3(0.f);
};

struct Part
{
	std::string id;       // z.B. "Armor_T0"
	std::string file;     // z.B. "Armor"
	std::string animFile; // MF-Datei der Animationen/Klaenge (= file; Kopien wie das Breitschwert: die Vorlage)
	std::string name;     // Anzeigename
	std::string feature;  // Bergungs-Feature (abgeschlagenes Teil)
	Kind kind = KIND_TORSO;
	int side = 0;         // Arme: +1 rechts, -1 links
	int corp = -1;        // MF-Konzern des Teils (0 Rimtech, 1 Mil-Agro, 2 Neuropa; -1 unbekannt)
	std::vector<std::string> pieNames;
	std::vector<std::string> abil;
	PartStats st;
	glm::vec3 shR = glm::vec3(0.f), shL = glm::vec3(0.f);
	glm::vec3 neck = glm::vec3(0.f, -0.2f, 6.6f);   // Torso: Halspunkt (Kopf = Knochen 8)
	glm::vec3 hipR = glm::vec3(0.f), hipL = glm::vec3(0.f), mount = glm::vec3(0.f);
	glm::vec3 kneeA = glm::vec3(0.f), ankA = glm::vec3(0.f), kneeB = glm::vec3(0.f), ankB = glm::vec3(0.f);
	std::vector<glm::vec3> chain;
	int eyePiece = -1;                          // (16.11.) leuchtendes Auge (Neuropa): Teil-Stueck und Lage darin
	glm::vec3 eyePos = glm::vec3(0.f);
	std::string eyeGlow;                        // Modell des Lichthofs (effects/mfeyeglow.pie)
	int fxPiece = -1;                           // (16.11.) leuchtender Anbau, z. B. Energieschild am Unterarm (additiv)
	glm::vec3 fxPos = glm::vec3(0.f);
	std::string fxModel;
	std::vector<PieceBone> bones;               // je Einzelteil (pieNames)
	float ground = 0.f;                         // Beine: Sohlenhoehe in der Ruhehaltung (MF-Einheiten)
	bool legWeapon = false;                     // (20.11.) MF-Beine mit eingebauter Waffe, als Waffenarm gefuehrt: werden als Beine gezeichnet

	const iIMDShape *shape(size_t i) const;   // laedt das Modell bei Bedarf
	bool hasAbility(const char *a) const;
private:
	mutable std::vector<const iIMDShape *> m_shapes;
	mutable unsigned m_gen = 0;   // MOD (20.11., Absturz 21:26): Modell-Generation - nach dem Neuladen der Modelle neu holen
};

enum Severed { SEV_RARM = 1, SEV_LARM = 2, SEV_LEGS = 4 };
enum WeaponMode { MODE_ALL = 0, MODE_MELEE = 1, MODE_RANGED = 2 };
enum PartSlot { SLOT_TORSO = 0, SLOT_RARM = 1, SLOT_LARM = 2, SLOT_LEGS = 3, SLOT_COUNT = 4 };

struct Loadout
{
	const Part *torso = nullptr, *legs = nullptr, *armR = nullptr, *armL = nullptr;
	int slotR = -1, slotL = -1;     // Waffenplaetze der beiden Arme (nur bei Droiden)
	unsigned severed = 0;           // Severed-Bits
};

enum AnimKind { ANIM_IDLE = 0, ANIM_WALK = 1, ANIM_FIRE = 2, ANIM_DIE = 3, ANIM_FLY_UP = 4, ANIM_FLY = 5, ANIM_FLY_DOWN = 6 };   // MOD (17.11.): Jetpack-Flug

/// Daten laden (einmalig). false = keine modularen Combot-Daten vorhanden.
bool ready();
/// Modellmassstab der Combot-Teile (parts.json "scale"; Designer gleicht damit seine Ansicht ab)
float modelScale();
const std::vector<Part> &parts();
const Part *find(const std::string &id);

const Part *partOfBody(const BODY_STATS *b);
const Part *partOfPropulsion(const PROPULSION_STATS *p);
const Part *partOfWeapon(unsigned weaponIndex);

bool isModular(const DROID *d);
Loadout loadoutOf(const DROID *d);

unsigned severedMask(const DROID *d);
/// Teil abschlagen: Maske setzen, Waffe des Arms entfernen, Bergungs-Feature auf den Boden legen.
void severPart(DROID *d, unsigned bit);
void forget(unsigned droidId);

/// Schaden an einem zufaellig getroffenen vorhandenen Teil (Torso/Arme/Beine); schlaegt das Teil ab, wenn es 0 Trefferpunkte hat.
/// Gibt das getroffene Teil (PartSlot) zurueck, -1 wenn nichts getroffen wurde.
int hitRandomPart(DROID *d, int damage);
/// Zustand eines Teils in Prozent (0..100); -1 = Teil fehlt (abgeschlagen oder nie vorhanden)
int partHealth(const DROID *d, PartSlot slot);
const Part *partInSlot(const DROID *d, PartSlot slot);
/// Waffen-Einsatz: nur Nahkampf, alle oder nur Fernkampf
WeaponMode weaponMode(const DROID *d);
void setWeaponMode(DROID *d, WeaponMode m);
/// Pro Spielschritt: erzwingt den Waffenmodus (sperrt die nicht erlaubten Waffen)
void enforceWeaponMode(DROID *d);
/// hat der Combot mindestens einen funktionierenden Nahkampfarm?
bool hasMeleeArm(const DROID *d);
/// Kurzname der Waffe in einem Waffenplatz (fuer die Anzeige), leer wenn keine Waffe
std::string weaponLabel(const DROID *d, PartSlot arm);

/// Schildwerte (Block-Bonus in Prozent) aller vorhandenen Arme/Torso
int blockBonus(const DROID *d);
bool hasAbility(const DROID *d, const char *ability);
/// (16.11. 19:34) Combot mit Sprungduesen-Beinen oder Jetpack-Torso: fliegt (ueber Hindernisse, zwischen Oberflaeche und Orbit,
/// zwischen Asteroiden). Wegfindung wie ein Flieger.
bool canFly(const DROID *d);
/// Anzeige: Flughoehe (Weltmasseinheiten) - hebt beim Fahren ueber weitere Strecken ab und schwebt ueber der Orbit-Leere
float flightOffset(const DROID *d);
/// einmal pro Spielschritt: Selbstreparatur usw.
void updateAbilities(DROID *d);
/// (16.11.) MF-Faehigkeiten: Kraftfeld faengt ein Geschoss ab (true = Schaden verfaellt), Recon-Sicht (Prozent), HTH-Nahkampf (Prozent),
/// Steady (Prozent fuer Teil-Treffer); Sonar lichtet den Untergrund-Schleier (in updateAbilities)
bool absorbShot(DROID *d);
int sensorPercent(const DROID *d);
int meleePercent(const DROID *attacker);
int severPercent(const DROID *attacker);

using DrawPiece = std::function<void(const iIMDShape *, const glm::mat4 &, const Part *)>;
/// Zusammenbau mit Skelett-Animation berechnen und jedes Einzelteil ueber das Callback zeichnen.
void drawLoadout(const Loadout &lo, AnimKind kind, unsigned elapsedMs, int dieVariant, const glm::mat4 &base, const DrawPiece &emit);
/// (Wunsch 19.11. 23:00 "Energieschilde sehe ich nicht"): beim naechsten drawLoadout auch die Leucht-Anbauten (Energieschild, Auge,
/// Laserklinge) ausgeben - fuer die Baustation; im Spiel macht das draw() selbst
void setEmitEffects(bool on);
/// zeichnet p, falls es ein Leucht-Anbau ist (Schild: blaue Flaeche + Leuchten, pulsierend; Auge: Lichthof) - true = war ein Anbau
bool drawEffectPiece(const iIMDShape *s, const glm::mat4 &m, const glm::mat4 &view, const Part *p, unsigned seed);

/// Todesanimation: Momentaufnahme des Combots (inkl. fehlender Teile) fuer den Engine-Todeseffekt.
/// Gibt die Kennung zurueck, die als Effektgroesse (effectSetSize) uebergeben wird.
unsigned registerCorpse(const DROID *d, unsigned birthTime);
/// Wird aus dem Todeseffekt gezeichnet. true = gezeichnet.
bool drawCorpse(unsigned id, const glm::mat4 &model, const glm::mat4 &view, int colour);

/// Wird aus displayCompObj aufgerufen. true = gezeichnet (Standardpfad ueberspringen).
bool draw(DROID *d, bool bButton, const glm::mat4 &model, const glm::mat4 &view, int colour, PIELIGHT brightness, int pieFlag, int pieFlagData);

}
